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
#include <kern/elf.h>
#include <kern/gui/gimage.h>

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
// CUserProcessTask (#6): launches an embedded ELF as an isolated EL0 process. The
// kernel thread builds a private address space, loads the ELF segments into it
// (LoadELF), maps a user stack, switches TTBR0, and drops to EL0 at the entry
// point. From then on this task IS the user process, preempted like any other.
//
extern "C" u8 hello_elf_begin[];
extern "C" u8 hello_elf_end[];

class CUserProcessTask : public CTask
{
public:
	CUserProcessTask (const u8 *pELF, unsigned nSize, const char *pName, CLogger *pLogger)
	:	m_pELF (pELF), m_nSize (nSize), m_pName (pName), m_pLogger (pLogger)
	{
		SetName (pName);
	}

	void Run (void) override
	{
		CAddressSpace *pAS = new CAddressSpace ();
		if (pAS == 0 || !pAS->IsValid ())
		{
			m_pLogger->Write (m_pName, LogError, "address space creation failed");
			return;
		}

		u64 ulEntry = 0;
		if (!LoadELF (m_pELF, m_nSize, pAS, &ulEntry))
		{
			m_pLogger->Write (m_pName, LogError, "ELF load failed");
			delete pAS;
			return;
		}

		// User stack (one page below USER_STACK_TOP).
		TKPageAttr StackAttr = KPAGE_ATTR_USER_DATA;
		if (pAS->MapNewPage (USER_STACK_TOP - KPAGE_SIZE, StackAttr) == 0)
		{
			m_pLogger->Write (m_pName, LogError, "stack mapping failed");
			delete pAS;
			return;
		}

		SetUserData (pAS, TASK_USER_DATA_USER);
		pAS->Activate ();

		m_pLogger->Write (m_pName, LogNotice, "entering EL0 at %lp (ASID %u)",
				  (void *) ulEntry, (unsigned) pAS->GetASID ());

		enter_user (ulEntry, USER_STACK_TOP, 0);
		// not reached
	}

private:
	const u8   *m_pELF;
	unsigned    m_nSize;
	const char *m_pName;
	CLogger	   *m_pLogger;
};

//
// Compositor (#10): a kernel thread that composites all windows onto the screen
// and presents at ~60 fps. It is the single owner of UpdateDisplay(); window
// owners only draw into their own canvas.
//
class CCompositorTask : public CTask
{
public:
	CCompositorTask (C2DGraphics *p2D, CWindowManager *pWM)
	:	m_p2D (p2D), m_pWM (pWM)
	{
		SetName ("compositor");
	}

	void Run (void) override
	{
		int nW = (int) m_p2D->GetWidth ();
		int nH = (int) m_p2D->GetHeight ();
		for (;;)
		{
			GImage Screen ((u32 *) m_p2D->GetBuffer (), nW, nH);
			m_pWM->Composite (&Screen);
			m_p2D->UpdateDisplay ();
			CScheduler::Get ()->MsSleep (16);
		}
	}

private:
	C2DGraphics    *m_p2D;
	CWindowManager *m_pWM;
};

// Embedded windowed demo programs (EL0 ELFs). Each create_window()s and animates;
// both run concurrently via preemption and the compositor shows both.
extern "C" u8 demoA_elf_begin[];
extern "C" u8 demoA_elf_end[];
extern "C" u8 demoB_elf_begin[];
extern "C" u8 demoB_elf_end[];

CKernel::CKernel (void)
:	m_Timer (&m_Interrupt),
	m_Logger (m_Options.GetLogLevel (), &m_Timer),
	m_2DGraphics (SCREEN_WIDTH, SCREEN_HEIGHT),
	m_bGraphics (FALSE)
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
		m_Scheduler.RegisterTaskTerminationHandler (AddressSpaceTaskTerminate);
	}

	// Framebuffer is optional (needs an attached display). Do not fail boot if
	// it is unavailable -- the serial console + scheduler still run.
	if (bOK)
	{
		m_bGraphics = m_2DGraphics.Initialize ();
		if (!m_bGraphics)
		{
			m_Logger.Write (FromKernel, LogWarning,
					"no framebuffer/display; graphics demo disabled");
		}
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

	// Milestone #6: an isolated EL0 process loaded from an embedded ELF image.
	new CUserProcessTask (hello_elf_begin,
			      (unsigned) (hello_elf_end - hello_elf_begin),
			      "hello", &m_Logger);

	// End goal: two EL0 ELF processes, each in its own window, animating at the
	// same time (preemption), with the compositor presenting both. demoA/demoB
	// create their windows via the create_window syscall and draw into the shared
	// canvas the kernel maps into their address space.
	if (m_bGraphics)
	{
		new CCompositorTask (&m_2DGraphics, &m_WindowManager);
		new CUserProcessTask (demoA_elf_begin,
				      (unsigned) (demoA_elf_end - demoA_elf_begin),
				      "demoA", &m_Logger);
		new CUserProcessTask (demoB_elf_begin,
				      (unsigned) (demoB_elf_end - demoB_elf_begin),
				      "demoB", &m_Logger);

		m_Logger.Write (FromKernel, LogNotice, "compositor + 2 windowed ELF demos started");
	}

	unsigned nUptime = 0;
	while (1)
	{
		m_Scheduler.MsSleep (5000);
		m_Logger.Write (FromKernel, LogNotice, "main: uptime ~%u s", nUptime += 5);
	}

	return ShutdownHalt;
}
