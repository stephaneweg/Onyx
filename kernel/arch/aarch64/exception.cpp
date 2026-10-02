//
// exception.cpp
//
// C side of our exception handling for the exceptions taken at EL1 (assembly in vectors.S):
// the kernel's own code. Apps run at EL0 and their exceptions take the EL0 vectors instead
// (arch/aarch64/el0.S, sys/el0.cpp): a system call, an emulated ID register read, or a fault
// that kills the app. So a synchronous exception here is the kernel's -- a fault-safe copy of
// an app's memory (kern/uaccess.h) recovers, anything else is a kernel bug: the post-mortem
// console and the crash record. An IRQ here interrupted kernel code, which is never preempted.
// Circle's InterruptHandler() does the GIC dispatch (called directly from the IRQ stub).
//
#include <kern/trapframe.h>
#include <kern/appcore.h>
#include <kern/uaccess.h>		// UAccessFixup
#include <kern/vm.h>			// VmKernelFault (v75)
#include <kern/crashlog.h>
#include <kern/thread.h>		// WordWaitTick (v68)
#include <kern/iowait.h>		// IoWaitTick (v75)
#include <circle/multicore.h>
#include <kern/gui/gimage.h>
#include <circle/sched/scheduler.h>
#include <circle/2dgraphics.h>
#include <circle/string.h>
#include <circle/exception.h>		// EXCEPTION_* codes
#include <circle/exceptionstub.h>	// TAbortFrame, ExceptionHandler()
#include <circle/logger.h>
#include <circle/startup.h>		// halt()
#include <circle/actled.h>		// panic SOS on the ACT LED (headless)
#include <circle/timer.h>		// SimpleMsDelay
#include <circle/bcmframebuffer.h>	// the panic screen, by the CPU
#include <circle/synchronize.h>
#include <circle/util.h>
#include <circle/types.h>

// Panic surface: the framebuffer actually shown on HDMI (the compositor's
// C2DGraphics). Once the compositor runs, the boot console (m_Screen) is no
// longer scanned out, so an exception dump there is invisible. We paint a
// visible red panic with the key registers onto the displayed buffer instead.
static C2DGraphics *s_pPanicGraphics = 0;

void SetPanicGraphics (C2DGraphics *p2D)
{
	s_pPanicGraphics = p2D;
}

// Text without the heap: an exception handler runs with the FIQs masked, and the heap's spin lock
// (IRQ level) asserts then -- the report was lost behind "synchronize64.cpp(64): assertion failed:
// nTargetLevel == FIQ_LEVEL || !(nFlags & 0x40)". A small appender into a caller's buffer instead.
struct TPanicText
{
	char *p; unsigned n, cap;
	TPanicText (char *pBuf, unsigned nCap) : p (pBuf), n (0), cap (nCap) { p[0] = 0; }
	TPanicText &s (const char *q) { while (q && *q && n + 1 < cap) p[n++] = *q++; p[n] = 0; return *this; }
	TPanicText &x (u64 v)			// 0x..., the digits it needs
	{
		char d[20]; int k = 0;
		do { d[k++] = "0123456789ABCDEF"[v & 15]; v >>= 4; } while (v);
		s ("0x");
		while (k && n + 1 < cap) p[n++] = d[--k];
		p[n] = 0; return *this;
	}
};

static void PanicToScreen (unsigned nEC, u64 ulELR, u64 ulFAR, u64 ulSPSR)
{
	if (s_pPanicGraphics == 0)
	{
		return;
	}

	int nW = (int) s_pPanicGraphics->GetWidth ();
	int nH = (int) s_pPanicGraphics->GetHeight ();
	GImage Img ((u32 *) s_pPanicGraphics->GetBuffer (), nW, nH);

	Img.Clear (0x00500000);						// dark red
	Img.DrawText (16, 16, "*** KERNEL PANIC (exception) ***", 0x00FFFFFF);

	static char Line[96];
	TPanicText (Line, sizeof Line).s ("EC=").x (nEC).s ("  ELR=").x (ulELR);
	Img.DrawText (16, 40, Line, 0x00FFFF00);
	TPanicText (Line, sizeof Line).s ("FAR=").x (ulFAR).s ("  SPSR=").x (ulSPSR);
	Img.DrawText (16, 56, Line, 0x00FFFF00);

	// Copied into the displayed frame buffer by the CPU, not by UpdateDisplay's DMA: the
	// compositor's display DMA may be in flight (its channel taken, its completion never
	// seen with the IRQs masked here), and waiting for it hung the panic before the screen
	// and the SOS -- a panic then looked like a silent hang.
	CBcmFrameBuffer *pFB = (CBcmFrameBuffer *) s_pPanicGraphics->GetDisplay ();
	u8 *pDst = pFB != 0 ? (u8 *) (uintptr) pFB->GetBuffer () : 0;
	if (pDst == 0 || pFB->GetDepth () != 32) return;
	unsigned nPitch = pFB->GetPitch ();
	const u8 *pSrc = (const u8 *) s_pPanicGraphics->GetBuffer ();
	for (int y = 0; y < nH; y++)
		memcpy (pDst + (unsigned) y * nPitch, pSrc + (unsigned) y * (unsigned) nW * 4, (unsigned) nW * 4);
	CleanDataCacheRange ((uintptr) pDst, (unsigned) nH * nPitch);
}

// Headless sign of a kernel panic: blink SOS (... --- ...) on the green ACT LED forever,
// so a panic is told apart from a hang (LED frozen) without a screen or serial cable.
static void PanicBlink (boolean bForever)
{
	CActLED *pLED = CActLED::Get ();
	if (pLED == 0)
	{
		if (bForever) halt ();
		return;
	}
	static const unsigned Pattern[] = { 150, 150, 150, 450, 450, 450, 150, 150, 150 };
	do
	{
		for (unsigned i = 0; i < sizeof Pattern / sizeof Pattern[0]; i++)
		{
			pLED->On ();  CTimer::SimpleMsDelay (Pattern[i]);
			pLED->Off (); CTimer::SimpleMsDelay (150);
		}
		CTimer::SimpleMsDelay (1200);
	}
	while (bForever);
}

static inline u64 ReadESR (void)
{
	u64 nValue;
	asm volatile ("mrs %0, esr_el1" : "=r" (nValue));
	return nValue;
}

static inline u64 ReadFAR (void)
{
	u64 nValue;
	asm volatile ("mrs %0, far_el1" : "=r" (nValue));
	return nValue;
}

// Build a Circle TAbortFrame from our trap frame and hand it to Circle's
// ExceptionHandler(), which logs all registers and halts. Does not return.
static void DumpAndHalt (unsigned nException, TTrapFrame *pFrame)
{
	TAbortFrame Frame;
	Frame.esr_el1  = ReadESR ();
	Frame.spsr_el1 = pFrame->spsr_el1;
	Frame.x30      = pFrame->x[30];
	Frame.elr_el1  = pFrame->elr_el1;
	Frame.sp_el0   = pFrame->sp_el0;
	Frame.sp_el1   = (u64) pFrame + TF_SIZE;	// kernel SP at the moment of trap
	Frame.far_el1  = ReadFAR ();
	Frame.unused   = 0;

	unsigned nEC = (unsigned) ((Frame.esr_el1 >> 26) & 0x3F);

	// The crash record first (read back after the watchdog reboot: SD:/etc/lastcrash.txt) --
	// without the heap (the FIQs are masked here: its lock would assert, the report lost).
	{
		static char Line[200];
		TPanicText (Line, sizeof Line).s ("an exception: EC=").x (nEC).s (" ELR=").x (pFrame->elr_el1)
			.s (" FAR=").x (Frame.far_el1).s (" SPSR=").x (pFrame->spsr_el1).s (" LR=").x (pFrame->x[30])
			.s (" SP=").x (Frame.sp_el1).s (" task ")
			.s (CScheduler::IsActive () && CScheduler::Get ()->GetCurrentTask ()
			    ? CScheduler::Get ()->GetCurrentTask ()->GetName () : "-");
		CrashLogPanic (Line);
		CrashLogStack (pFrame);		// (the return addresses on the faulting context's stack)
	}

	// Paint a visible red panic (EC/ELR/FAR) on the displayed framebuffer -- the
	// boot console is no longer scanned out once the compositor runs.
	PanicToScreen (nEC, pFrame->elr_el1, Frame.far_el1, pFrame->spsr_el1);

	// Log the essentials (the boot console shows it if the compositor has not taken the
	// display yet; kmsg is gone with the system), then blink SOS instead of Circle's
	// ExceptionHandler, whose register dump would go to the same invisible console and
	// then halt with a frozen LED -- indistinguishable from a hang when headless.
	// (No CLogger here: it formats on the heap -- the assertion above. The crash record has it all.)
	(void) nException;
	PanicBlink (FALSE);			// one SOS first
	CrashLogDumpNow ("an exception (kernel panic)");	// core 1: the report, then a restart
	PanicBlink (TRUE);				// (hangreboot=0: SOS for ever)
}

void SyncHandlerEL1 (TTrapFrame *pFrame)
{
	// A fault-safe copy (kern/uaccess.h) hit an app's bad pointer: it returns a failure.
	if (UAccessFixup (pFrame, ReadESR ()))
	{
		return;
	}

	// (v75) The safety net: kernel code touched an unfilled page of the current app's lazy
	// region outside the helpers (kern/vm.h) -- filled, logged once, the access retried.
	if (VmKernelFault (pFrame, ReadESR (), ReadFAR ()))
	{
		return;
	}

	// Any other synchronous exception at EL1 is a kernel bug (an app's own faults are taken at
	// EL0: sys/el0.cpp kills the app).
	DumpAndHalt (EXCEPTION_SYNCHRONOUS, pFrame);
}

void BadModeHandler (TTrapFrame *pFrame)
{
	DumpAndHalt (EXCEPTION_UNEXPECTED, pFrame);
}

// ---- The end of an IRQ taken at EL1 ------------------------------------------------
//
// The interrupted code is the kernel's (a kernel task, a system call in progress, the idle
// loop): it is never preempted -- the kernel is non-preemptive, the classic Unix model (it holds
// its resources without locks between its own Yields). The time slice of an app ends when an IRQ
// finds it at EL0 (sys/el0.cpp, El0IrqExit); a pending reschedule waits for that, or for the
// task's next Yield. Only the watchdog samples are taken here.
void KernelIRQExit (TTrapFrame *pFrame)
{
#ifdef ARM_ALLOW_MULTI_CORE
	// Cores 2-3 (app cores) take their IRQs (the stop IPI) through our vectors too; no
	// scheduling there.
	if (CMultiCoreSupport::ThisCore () != 0)
	{
		AppCoreOnIRQExit (pFrame);
		return;
	}
#endif

	if (!CScheduler::IsActive ())
	{
		return;
	}

	CrashLogSample (pFrame);			// (the crash record: where core 0 was)

	// Stall watchdog: where is the current task, if it has not yielded for too long?
	CScheduler::Get ()->StallSample (pFrame->elr_el1, pFrame->x[30]);
}

void PeriodicTick (void)
{
	// Runs inside the timer IRQ (within InterruptHandler), 100 times per second.
	if (CScheduler::IsActive ())
	{
		WordWaitTick ();			// (v68) words changed without a wake (app cores)
		IoWaitTick ();				// (v75) the readiness wait's tick hooks
		CScheduler::Get ()->OnTimerTick ();	// (after: a "real time" task it woke preempts)
	}
}
