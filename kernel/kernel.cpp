//
// kernel.cpp
//
#include "kernel.h"
#include <circle/machineinfo.h>
#include <circle/memory.h>
#include <circle/sched/task.h>
#include <circle/synchronize.h>
#include <circle/util.h>
#include <kern/trapframe.h>
#include <kern/syscall.h>
#include <kern/addrspace.h>
#include <kern/layout.h>

static const char FromKernel[] = "kernel";

// Issue a syscall via SVC (AArch64 ABI: x8 = number, x0..x2 = args, x0 = result).
// Used here for an EL1 self-test of the syscall path before EL0 processes exist.
static long DoSyscall (unsigned long nNum, unsigned long a0, unsigned long a1, unsigned long a2)
{
	register unsigned long x8 asm ("x8") = nNum;
	register unsigned long x0 asm ("x0") = a0;
	register unsigned long x1 asm ("x1") = a1;
	register unsigned long x2 asm ("x2") = a2;

	asm volatile ("svc #0"
		      : "+r" (x0)
		      : "r" (x8), "r" (x1), "r" (x2)
		      : "memory");

	return (long) x0;
}

//
// Demo worker for milestone #3: a kernel thread that logs at a fixed interval and
// then sleeps, yielding the CPU. Two of these at different intervals make the
// scheduler's interleaving visible over the serial console. (Cooperative for now:
// the sleep is what yields; timer-IRQ preemption arrives in #4.)
//
class CHeartbeatTask : public CTask
{
public:
	CHeartbeatTask (CLogger *pLogger, const char *pName, unsigned nIntervalMs)
	:	m_pLogger (pLogger),
		m_pName (pName),
		m_nIntervalMs (nIntervalMs)
	{
		SetName (pName);
	}

	void Run (void) override
	{
		unsigned nCount = 0;
		while (1)
		{
			m_pLogger->Write (m_pName, LogNotice, "beat %u (every %u ms)",
					  ++nCount, m_nIntervalMs);

			CScheduler::Get ()->MsSleep (m_nIntervalMs);
		}
	}

private:
	CLogger	   *m_pLogger;
	const char *m_pName;
	unsigned    m_nIntervalMs;
};

//
// Milestone #5 demo: a kernel thread that builds a private address space, loads
// the EL0 user stub into it, switches TTBR0, and drops to EL0. From then on this
// task IS a user process: it runs the stub in EL0, is preempted by the timer, and
// resumes in EL0 via the trap frame. Proves page-table isolation + the EL0 entry.
//
extern "C" u8 user_stub_begin[];
extern "C" u8 user_stub_end[];

class CUserTestTask : public CTask
{
public:
	CUserTestTask (CLogger *pLogger)
	:	m_pLogger (pLogger)
	{
		SetName ("user-test");
	}

	void Run (void) override
	{
		CAddressSpace *pAS = new CAddressSpace ();
		if (pAS == 0 || !pAS->IsValid ())
		{
			m_pLogger->Write ("user-test", LogError, "address space creation failed");
			return;
		}

		// Map and fill the code page (written via its kernel identity address, so
		// the user RO+X mapping does not block the load).
		TKPageAttr CodeAttr = KPAGE_ATTR_USER_CODE;
		void *pCode = pAS->MapNewPage (USER_LOAD_BASE, CodeAttr);
		TKPageAttr DataAttr = KPAGE_ATTR_USER_DATA;
		void *pStack = pAS->MapNewPage (USER_STACK_TOP - KPAGE_SIZE, DataAttr);
		if (pCode == 0 || pStack == 0)
		{
			m_pLogger->Write ("user-test", LogError, "user page mapping failed");
			return;
		}

		size_t nStubSize = (size_t) (user_stub_end - user_stub_begin);
		memcpy (pCode, user_stub_begin, nStubSize);
		SyncDataAndInstructionCache ();		// make the freshly written code executable

		// Become this address space, then drop to EL0 at the stub entry.
		SetUserData (pAS, TASK_USER_DATA_USER);
		pAS->Activate ();

		m_pLogger->Write ("user-test", LogNotice,
				  "entering EL0 at %lp (ASID %u), %u-byte stub",
				  (void *) USER_LOAD_BASE, (unsigned) pAS->GetASID (),
				  (unsigned) nStubSize);

		enter_user (USER_LOAD_BASE, USER_STACK_TOP, 0);
		// not reached
	}

private:
	CLogger *m_pLogger;
};

CKernel::CKernel (void)
:	m_Timer (&m_Interrupt),
	m_Logger (m_Options.GetLogLevel (), &m_Timer)
{
	m_ActLED.Blink (5);		// visible sign of life before the console is up
}

CKernel::~CKernel (void)
{
}

boolean CKernel::Initialize (void)
{
	boolean bOK = TRUE;

	if (bOK)
	{
		bOK = m_Serial.Initialize (115200);
	}

	if (bOK)
	{
		// Log straight to the serial port (no framebuffer in this milestone).
		bOK = m_Logger.Initialize (&m_Serial);
	}

	if (bOK)
	{
		bOK = m_Interrupt.Initialize ();
	}

	if (bOK)
	{
		bOK = m_Timer.Initialize ();
	}

	if (bOK)
	{
		// Take over exception handling from Circle: install our VBAR_EL1 (EL0/EL1
		// vectors + trap frame + syscall path), and drive the scheduler's time
		// slice from the 100 Hz timer tick -> preemptive multitasking (#4).
		install_vectors ();
		m_Timer.RegisterPeriodicHandler (PeriodicTick);

		// Per-process address spaces (#5): remember the kernel TTBR0 and switch
		// TTBR0/ASID on every task switch based on the task's address space.
		AddrSpaceInit ();
		m_Scheduler.RegisterTaskSwitchHandler (AddressSpaceTaskSwitch);
	}

	return bOK;
}

TShutdownMode CKernel::Run (void)
{
	m_Logger.Write (FromKernel, LogNotice,
			"Multi-process kernel on Circle -- milestone #4 (vectors + preemption)");
	m_Logger.Write (FromKernel, LogNotice, "Compiled on " __DATE__ " " __TIME__);

	CMachineInfo *pInfo = CMachineInfo::Get ();
	m_Logger.Write (FromKernel, LogNotice, "Running on %s, %lu MB RAM",
			pInfo->GetMachineName (),
			(unsigned long) (CMemorySystem::Get ()->GetMemSize () / 0x100000));

	// --- Milestone #3 demo: kernel threads scheduled by our CScheduler ---------
	//
	// m_Scheduler is already constructed (it created the "main" task -- this very
	// context). Spawn two worker threads with different periods, then let this
	// (main) thread also participate. With no preemption yet, control passes
	// whenever a thread sleeps; the interleaving over serial shows the scheduler
	// and the block/wake path working.
	// Self-test the syscall path from EL1 (proves SVC -> our vectors -> dispatch).
	// Once EL0 processes exist (#6) this same path serves user mode.
	static const char SyscallMsg[] = "hello from a syscall (SVC trap from EL1)";
	DoSyscall (SYS_write, /*fd*/ 1, (unsigned long) SyscallMsg, sizeof SyscallMsg - 1);

	m_Logger.Write (FromKernel, LogNotice, "starting scheduler with 2 worker threads + 1 EL0 process");

	new CHeartbeatTask (&m_Logger, "beat-A", 700);
	new CHeartbeatTask (&m_Logger, "beat-B", 1100);

	// Milestone #5: an isolated EL0 process loaded into its own address space.
	new CUserTestTask (&m_Logger);

	unsigned nUptime = 0;
	while (1)
	{
		m_Scheduler.MsSleep (5000);
		m_Logger.Write (FromKernel, LogNotice, "main: uptime ~%u s", nUptime += 5);
	}

	return ShutdownHalt;
}
