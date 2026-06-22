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
// CUserProcessTask (Option C): launch an ELF as an EL1 app in its own address
// space. The thread builds a private address space, loads the ELF segments
// (EL1-executable), activates the space, and CALLS the entry point directly --
// the app runs in EL1 on this thread's (large) stack and calls kapi_* kernel
// functions directly (resolved at link). Apps are isolated from each other (own
// page tables) but not from the kernel. The thread is preempted like any other.
//
class CUserProcessTask : public CTask
{
public:
	// Large stack: the app and the kernel functions it calls share this thread stack.
	CUserProcessTask (const u8 *pELF, unsigned nSize, const char *pName, CLogger *pLogger)
	:	CTask (0x40000),	// 256 KB
		m_pELF (pELF), m_nSize (nSize), m_pName (pName), m_pLogger (pLogger)
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

		// Become this address space (kernel stays mapped + EL1-accessible).
		SetUserData (pAS, TASK_USER_DATA_USER);
		pAS->Activate ();

		m_pLogger->Write (m_pName, LogNotice, "running (EL1) entry %lp ASID %u",
				  (void *) ulEntry, (unsigned) pAS->GetASID ());

		// Direct EL1 call into the app. It calls kapi_* directly; loops or exits.
		((void (*) (void)) ulEntry) ();

		// App returned: terminate (frees the address space + window).
		CScheduler::Get ()->GetCurrentTask ()->Terminate ();
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

CKernel::CKernel (void)
:	m_Timer (&m_Interrupt),
	m_Logger (m_Options.GetLogLevel (), &m_Timer),
	m_2DGraphics (SCREEN_WIDTH, SCREEN_HEIGHT),
	m_EMMC (&m_Interrupt, &m_Timer, &m_ActLED),
	m_bGraphics (FALSE),
	m_bSDMounted (FALSE)
{
	m_ActLED.Blink (5);		// visible sign of life before the console is up
}

// Read a whole file from the mounted SD card into a freshly allocated buffer.
// Returns the buffer (caller may leak it for a one-shot process load) + its size,
// or 0 on any error.
static u8 *LoadFileFromSD (const char *pPath, unsigned *pSize)
{
	FIL File;
	if (f_open (&File, pPath, FA_READ) != FR_OK)
	{
		return 0;
	}

	unsigned nSize = (unsigned) f_size (&File);
	u8 *pBuffer = new u8[nSize];
	if (pBuffer == 0)
	{
		f_close (&File);
		return 0;
	}

	UINT nRead = 0;
	if (f_read (&File, pBuffer, nSize, &nRead) != FR_OK || nRead != nSize)
	{
		delete [] pBuffer;
		f_close (&File);
		return 0;
	}

	f_close (&File);
	*pSize = nSize;
	return pBuffer;
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

	// SD card is optional too: mount it so apps can be loaded from the card. If it
	// is absent, we fall back to the ELF images embedded in the kernel.
	if (bOK)
	{
		if (m_EMMC.Initialize () && f_mount (&m_FileSystem, "SD:", 1) == FR_OK)
		{
			m_bSDMounted = TRUE;
			m_Logger.Write (FromKernel, LogNotice, "SD card mounted (SD:)");
		}
		else
		{
			m_Logger.Write (FromKernel, LogWarning,
					"no SD card; using embedded apps");
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

	// End goal: two EL1 ELF processes, each in its own window, animating at the
	// same time (preemption), with the compositor presenting both. demoA/demoB
	// create their windows via the create_window syscall and draw into the shared
	// canvas the kernel maps into their address space.
	if (m_bGraphics)
	{
		new CCompositorTask (&m_2DGraphics, &m_WindowManager);

		// Load the two demos from the SD card (Option C: apps link against the
		// kernel's exported symbols, so they cannot be embedded -- they live on
		// the card as distinct ELF files).
		static const char *Names[2] = { "demoA", "demoB" };
		static const char *Paths[2] = { "SD:demoA.elf", "SD:demoB.elf" };

		if (!m_bSDMounted)
		{
			m_Logger.Write (FromKernel, LogError, "no SD card: cannot load demos");
		}
		else
		{
			for (int i = 0; i < 2; i++)
			{
				unsigned nSize = 0;
				const u8 *pElf = LoadFileFromSD (Paths[i], &nSize);
				if (pElf == 0)
				{
					m_Logger.Write (FromKernel, LogError, "cannot load %s", Paths[i]);
					continue;
				}
				m_Logger.Write (FromKernel, LogNotice, "%s: loaded from %s (%u bytes)",
						Names[i], Paths[i], nSize);
				new CUserProcessTask (pElf, nSize, Names[i], &m_Logger);
			}
		}

		m_Logger.Write (FromKernel, LogNotice, "compositor + windowed app(s) started");
	}

	unsigned nUptime = 0;
	while (1)
	{
		m_Scheduler.MsSleep (5000);
		m_Logger.Write (FromKernel, LogNotice, "main: uptime ~%u s", nUptime += 5);
	}

	return ShutdownHalt;
}
