//
// exception.cpp
//
// C side of our exception handling (assembly in vectors.S). Reuses Circle's
// ExceptionHandler() for register dumps on kernel faults, and Circle's
// InterruptHandler() for GIC dispatch (called directly from the IRQ stub).
//
#include <kern/trapframe.h>
#include <circle/sched/scheduler.h>
#include <circle/exception.h>		// EXCEPTION_* codes
#include <circle/exceptionstub.h>	// TAbortFrame, ExceptionHandler()
#include <circle/logger.h>
#include <circle/startup.h>		// halt()
#include <circle/types.h>

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
	// Called after Circle's InterruptHandler has already EOI'd. If the running
	// task's time slice expired (OnTimerTick set the flag), reschedule now. The
	// current task is still ready, so Yield() either switches to another ready
	// task or returns immediately -- it never enters the idle/wfi path here.
	if (CScheduler::IsActive () && CScheduler::Get ()->IsReschedPending ())
	{
		CScheduler::Get ()->Yield ();
	}
}

void PeriodicTick (void)
{
	// Runs inside the timer IRQ (within InterruptHandler), 100 times per second.
	if (CScheduler::IsActive ())
	{
		CScheduler::Get ()->OnTimerTick ();
	}
}
