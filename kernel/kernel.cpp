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

//
// A windowed animation drawn by a kernel thread (stand-in for an EL0 process until
// #6). It only touches its own window canvas; the compositor presents it. Two of
// these with different styles animate concurrently via preemption -- the structure
// of the end goal (two windowed programs running at the same time).
//
class CWindowDemoTask : public CTask
{
public:
	CWindowDemoTask (CWindow *pWindow, unsigned nStyle, const char *pName)
	:	m_pWindow (pWindow), m_nStyle (nStyle)
	{
		SetName (pName);
	}

	void Run (void) override
	{
		GImage *c = m_pWindow->Canvas ();
		int w = c->Width ();
		int h = c->Height ();
		unsigned t = 0;

		// bouncing box state (style 0)
		int bx = 4, by = 4, dx = 3, dy = 2, bs = 28;

		for (;;)
		{
			if (m_nStyle == 0)
			{
				c->Clear (0x00101820);
				bx += dx; by += dy; bs = bs;
				if (bx < 0)          { bx = 0;          dx = -dx; }
				if (bx + bs > w)     { bx = w - bs;     dx = -dx; }
				if (by < 0)          { by = 0;          dy = -dy; }
				if (by + bs > h)     { by = h - bs;     dy = -dy; }
				c->FillRectangle (bx, by, bx + bs, by + bs, 0x00FFC040);
				c->DrawRectangle (bx, by, bx + bs, by + bs, 0x00FFFFFF);
			}
			else
			{
				// moving rays from the centre
				c->Clear (0x00201018);
				int cx = w / 2, cy = h / 2;
				for (int k = 0; k < 12; k++)
				{
					int ang = ((int) t + k * 30) % 360;
					// crude sin/cos via small integer table-free approximation
					int ex = cx + (int) ((w / 2 - 4) * CosDeg (ang) / 1000);
					int ey = cy + (int) ((h / 2 - 4) * SinDeg (ang) / 1000);
					c->DrawLine (cx, cy, ex, ey, 0x0040FFA0 + (u32) (k * 0x0A0A00));
				}
			}

			t++;
			CScheduler::Get ()->MsSleep (20);	// ~50 fps
		}
	}

private:
	// Tiny fixed-point cos/sin in milli-units (-1000..1000), no FPU/library needed.
	static int CosDeg (int deg);
	static int SinDeg (int deg)	{ return CosDeg (deg - 90); }

	CWindow *m_pWindow;
	unsigned m_nStyle;
};

// 16-entry quarter-wave cosine table (0..90deg), milli-units; mirrored for full circle.
int CWindowDemoTask::CosDeg (int deg)
{
	static const int Q[16] =
	{ 1000, 995, 980, 957, 924, 882, 831, 773, 707, 634, 556, 471, 383, 290, 195, 98 };

	deg %= 360; if (deg < 0) deg += 360;
	int idx, sign;
	if (deg < 90)        { idx = deg;        sign =  1; }
	else if (deg < 180)  { idx = 180 - deg;  sign = -1; }
	else if (deg < 270)  { idx = deg - 180;  sign = -1; }
	else                 { idx = 360 - deg;  sign =  1; }
	if (idx >= 90) idx = 89;
	return sign * Q[idx * 16 / 90];
}

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

	// Milestone #5: an isolated EL0 process loaded into its own address space.
	new CUserTestTask (&m_Logger);

	// Milestone #10: two windows, each animated by its own (kernel) thread, with a
	// compositor presenting both. This is the end-goal structure -- two windowed
	// programs running at the same time -- with the owners becoming EL0 ELF
	// processes once the loader (#6) lands.
	if (m_bGraphics)
	{
		CWindow *pWinA = new CWindow (40, 50, 240, 180, "demo A");
		CWindow *pWinB = new CWindow (330, 130, 260, 200, "demo B");
		m_WindowManager.Add (pWinA);
		m_WindowManager.Add (pWinB);

		new CWindowDemoTask (pWinA, 0, "demoA");
		new CWindowDemoTask (pWinB, 1, "demoB");
		new CCompositorTask (&m_2DGraphics, &m_WindowManager);

		m_Logger.Write (FromKernel, LogNotice, "compositor + 2 windowed demos started");
	}

	unsigned nUptime = 0;
	while (1)
	{
		m_Scheduler.MsSleep (5000);
		m_Logger.Write (FromKernel, LogNotice, "main: uptime ~%u s", nUptime += 5);
	}

	return ShutdownHalt;
}
