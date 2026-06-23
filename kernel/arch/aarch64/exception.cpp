//
// exception.cpp
//
// C side of our exception handling (assembly in vectors.S). Reuses Circle's
// ExceptionHandler() for register dumps on kernel faults, and Circle's
// InterruptHandler() for GIC dispatch (called directly from the IRQ stub).
//
#include <kern/trapframe.h>
#include <kern/gui/gimage.h>
#include <circle/sched/scheduler.h>
#include <circle/2dgraphics.h>
#include <circle/string.h>
#include <circle/exception.h>		// EXCEPTION_* codes
#include <circle/exceptionstub.h>	// TAbortFrame, ExceptionHandler()
#include <circle/logger.h>
#include <circle/startup.h>		// halt()
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

	CString Line;
	Line.Format ("EC=0x%x  ELR=%lp", nEC, (void *) ulELR);
	Img.DrawText (16, 40, (const char *) Line, 0x00FFFF00);
	Line.Format ("FAR=%lp  SPSR=%lp", (void *) ulFAR, (void *) ulSPSR);
	Img.DrawText (16, 56, (const char *) Line, 0x00FFFF00);

	s_pPanicGraphics->UpdateDisplay ();				// push to the display
}

// ESR_EL1 exception classes we care about
#define EC_SVC64		0x15	// SVC from AArch64
#define EC_IABORT_LOWER		0x20	// instruction abort from a lower EL (EL0)
#define EC_IABORT_SAME		0x21
#define EC_DABORT_LOWER		0x24	// data abort from a lower EL (EL0)
#define EC_DABORT_SAME		0x25

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

	// Paint a visible panic on the displayed framebuffer FIRST (the boot console
	// is no longer on screen once the compositor runs).
	PanicToScreen ((unsigned) ((Frame.esr_el1 >> 26) & 0x3F),
		       pFrame->elr_el1, Frame.far_el1, pFrame->spsr_el1);

	ExceptionHandler (nException, &Frame);		// never returns

	halt ();
}

void SyncHandlerEL1 (TTrapFrame *pFrame)
{
	unsigned nEC = (unsigned) (ReadESR () >> 26) & 0x3F;

	if (nEC == EC_SVC64)
	{
		// SVC from EL1: used to exercise the syscall path before EL0 exists.
		SyscallEntry (pFrame);
		return;
	}

	// Any other synchronous exception at EL1 is a kernel bug.
	DumpAndHalt (EXCEPTION_SYNCHRONOUS, pFrame);
}

void SyncHandlerEL0 (TTrapFrame *pFrame)
{
	unsigned nEC = (unsigned) (ReadESR () >> 26) & 0x3F;

	switch (nEC)
	{
	case EC_SVC64:
		SyscallEntry (pFrame);
		return;

	case EC_IABORT_LOWER:
	case EC_DABORT_LOWER:
		// User fault. No process model yet (#5/#6): we will later deliver a
		// signal / terminate the offending process. For now, dump and halt.
		CLogger::Get ()->Write ("exc", LogError,
					"EL0 abort EC=%#x ELR=%lp FAR=%lp",
					nEC, (void *) pFrame->elr_el1, (void *) ReadFAR ());
		DumpAndHalt (EXCEPTION_SYNCHRONOUS, pFrame);
		return;

	default:
		DumpAndHalt (EXCEPTION_SYNCHRONOUS, pFrame);
		return;
	}
}

void BadModeHandler (TTrapFrame *pFrame)
{
	DumpAndHalt (EXCEPTION_UNEXPECTED, pFrame);
}

void KernelIRQExit (void)
{
	// IMPORTANT: no preemptive reschedule here. Circle runs threads in EL1t
	// (SP_EL0) while exceptions run in EL1h (SP_EL1, the shared exception stack).
	// A context switch from the IRQ handler would swap SP_EL1, not the thread's
	// SP_EL0 -- it cannot correctly preempt an EL1t thread. So we schedule
	// COOPERATIVELY (like Circle's own scheduler): threads switch when they call
	// Yield()/MsSleep()/present(). The time-slice flag is left unused for now.
	// (True preemption would need a full trap-frame switch honoring SP_EL0.)
}

void PeriodicTick (void)
{
	// Runs inside the timer IRQ (within InterruptHandler), 100 times per second.
	if (CScheduler::IsActive ())
	{
		CScheduler::Get ()->OnTimerTick ();
	}
}
