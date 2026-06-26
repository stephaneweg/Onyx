//
// exception.cpp
//
// C side of our exception handling (assembly in vectors.S). Reuses Circle's
// ExceptionHandler() for register dumps on kernel faults, and Circle's
// InterruptHandler() for GIC dispatch (called directly from the IRQ stub).
//
#include <kern/trapframe.h>
#include <kern/layout.h>		// IS_USER_VA (preempt-gate classification)
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

	unsigned nEC = (unsigned) ((Frame.esr_el1 >> 26) & 0x3F);

	// Paint a visible red panic (EC/ELR/FAR) on the displayed framebuffer -- the
	// boot console is no longer scanned out once the compositor runs.
	PanicToScreen (nEC, pFrame->elr_el1, Frame.far_el1, pFrame->spsr_el1);

	ExceptionHandler (nException, &Frame);		// logs all registers; never returns

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

// ---- Track A preemption: the timer-IRQ exit redirect ------------------------
//
// Handed to the trampoline (vectors.S) when we preempt an app: its interrupted PC
// and PSTATE, to be restored when it is rescheduled. C linkage so the asm sees them;
// the single-core, IRQ-masked write->read handoff makes plain globals safe (nothing
// else runs between KernelIRQExit writing them and the trampoline reading them).
extern "C" {
	void PreemptTrampoline (void);		// vectors.S
	u64  g_PreemptELR  = 0;
	u64  g_PreemptSPSR = 0;
}

// The trampoline bl's this: the ordinary cooperative switch, but reached at EL1t on
// the preempted app's own SP_EL0 stack -- where Yield's SP_EL0/TTBR0 swap is correct.
extern "C" void PreemptDoYield (void)
{
	CScheduler::Get ()->Yield ();
}

void KernelIRQExit (TTrapFrame *pFrame)
{
	// Track-A preemptive reschedule, run at the end of every IRQ. Switch ONLY when a
	// time slice has expired AND the interrupted context was an app running its OWN
	// code -- EL1t (SPSR.M == 0b0100) with a user-VA return PC. The user-VA test IS
	// the kernel lock: an app inside a kapi_* call has a kernel-VA ELR and is left
	// alone (it may hold a kernel resource; those kapis yield cooperatively anyway).
	// Kernel threads (EL1h, or EL1t with a kernel-VA PC) are never preempted.
	if (   !CScheduler::IsActive ()
	    || !CScheduler::Get ()->IsReschedPending ())
	{
		return;
	}

	if (   (pFrame->spsr_el1 & 0xF) != 0x4		// not EL1t
	    || !IS_USER_VA (pFrame->elr_el1))		// not in the app's own code
	{
		return;
	}

	// Redirect the IRQ return to the trampoline, which performs the real switch at
	// EL1t on the app's stack. Stash the app's PC/PSTATE for it to restore on resume,
	// and mask IRQ+FIQ so the trampoline can't be re-preempted before it parks.
	CScheduler::Get ()->ClearResched ();
	g_PreemptELR     = pFrame->elr_el1;
	g_PreemptSPSR    = pFrame->spsr_el1;
	pFrame->elr_el1  = (u64) &PreemptTrampoline;
	pFrame->spsr_el1 = pFrame->spsr_el1 | (1u << 7) | (1u << 6);	// set I + F

	// IMPORTANT: do NOT log (CLogger::Write) here. It writes to the screen and
	// formats via the heap -- not safe from IRQ context, and doubly unsafe right
	// before this self-induced context switch (a throttled trace here hung the
	// machine on the switch that immediately followed a logged preemption). For
	// observability, bump a plain counter and read it later from a normal task.
}

void PeriodicTick (void)
{
	// Runs inside the timer IRQ (within InterruptHandler), 100 times per second.
	if (CScheduler::IsActive ())
	{
		CScheduler::Get ()->OnTimerTick ();
	}
}
