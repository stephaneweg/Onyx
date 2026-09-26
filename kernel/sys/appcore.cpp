//
// appcore.cpp -- cores 2 and 3 as a resource an app can acquire (see kern/appcore.h).
//
// Core side: each core waits in AppCoreLoop (WFE). When its owner starts a job, it loads
// the owner's TTBR0 (its ASID too), flushes its own TLB, and calls fn (arg) on the app's
// stack at EL1t -- like the app itself on core 0. When fn returns it goes back to the
// kernel address space and to sleep.
//
// Stopping a running job (release, app exit or kill): core 0 raises bAbort and sends the
// core an IPI. The core's IRQ comes through OUR vectors (installed on cores 2-3 too), and
// its exit (AppCoreOnIRQExit) rewrites the trap frame to return into AppCoreRestart, on
// the core's own kernel stack -- the job is simply dropped. A fault in the job (bad
// access...) is caught the same way (AppCoreOnFault) instead of a kernel panic.
//
// Everything here is plain memory shared between cores (inner-shareable, cache coherent)
// with explicit barriers; core 0 is the only writer of the ownership.
//
#include <kern/appcore.h>
#include <kern/addrspace.h>
#include <kern/trapframe.h>
#include <kern/layout.h>
#include <circle/multicore.h>
#include <circle/sched/scheduler.h>
#include <circle/timer.h>
#include <circle/logger.h>
#include <circle/types.h>

#ifndef IS_USER_VA
#define IS_USER_VA(va)	((u64) (va) >= USER_VA_BASE && (u64) (va) < USER_VA_END)
#endif

#define IPI_APPCORE_STOP	(IPI_USER + 0)
#define STOP_TIMEOUT_US		200000

extern "C" void AppCoreCall (u64 ulFunc, u64 ulArg, u64 ulStack);
extern "C" void install_vectors (void);
void ActivateKernelAddressSpace (void);

// fn (arg) on the app's stack; x19 keeps the kernel SP (callee-saved: fn preserves it).
asm (
	".text\n"
	".global AppCoreCall\n"
	".type AppCoreCall, %function\n"
	"AppCoreCall:\n"
	"	stp	x29, x30, [sp, #-32]!\n"
	"	str	x19, [sp, #16]\n"
	"	mov	x19, sp\n"
	"	mov	sp, x2\n"
	"	mov	x16, x0\n"
	"	mov	x0, x1\n"
	"	mov	x29, xzr\n"
	"	blr	x16\n"
	"	mov	sp, x19\n"
	"	ldr	x19, [sp, #16]\n"
	"	ldp	x29, x30, [sp], #32\n"
	"	ret\n"
);

struct TAppCore
{
	volatile boolean	bStarted;	// the core runs AppCoreLoop
	volatile boolean	bLost;		// did not answer a stop: never used again
	CAddressSpace *volatile	pOwner;		// (core 0 only)
	volatile u64		ulTTBR0, ulFunc, ulArg, ulStack;
	volatile int		nState;		// CORE_IDLE / CORE_RUNNING / CORE_FAULT
	volatile boolean	bGo;		// core 0 -> core: start the job
	volatile boolean	bAbort;		// core 0 -> core: drop it (cleared = done)
	volatile u64		ulFaultPC, ulFaultAddr;
	volatile unsigned	nFaultEC;
	volatile boolean	bFaultLogged;
	u64			ulStackTop;	// the core's own kernel stack (restarts)
};

static TAppCore s_Core[CORES];

static inline void Barrier (void)	{ asm volatile ("dsb ish" ::: "memory"); }
static inline void LocalTLBFlush (void)	{ asm volatile ("tlbi vmalle1; dsb nsh; isb" ::: "memory"); }

static void __attribute__ ((noreturn)) AppCoreLoop (unsigned nCore)
{
	TAppCore &C = s_Core[nCore];
	asm volatile ("msr daifclr, #2" ::: "memory");		// IRQs on (the stop IPI)
	for (;;)
	{
		if (C.bAbort)					// (asked while idle)
		{
			if (C.nState == CORE_RUNNING) C.nState = CORE_IDLE;
			Barrier ();
			C.bAbort = FALSE;
			Barrier ();
		}
		if (C.bGo)
		{
			C.bGo = FALSE;
			Barrier ();
			asm volatile ("msr ttbr0_el1, %0; isb" :: "r" (C.ulTTBR0) : "memory");
			LocalTLBFlush ();			// (an ASID may have been reused)
			AppCoreCall (C.ulFunc, C.ulArg, C.ulStack);
			ActivateKernelAddressSpace ();
			Barrier ();
			C.nState = CORE_IDLE;
			Barrier ();
			asm volatile ("sev");
			continue;
		}
		asm volatile ("wfe");
	}
}

// A dropped (or faulted) job resumes here, at EL1t on the core's own kernel stack.
extern "C" void __attribute__ ((noreturn)) AppCoreRestart (unsigned nCore)
{
	TAppCore &C = s_Core[nCore];
	ActivateKernelAddressSpace ();				// out of the app's space first
	LocalTLBFlush ();
	if (C.nState == CORE_RUNNING) C.nState = CORE_IDLE;	// (CORE_FAULT stays)
	C.bGo = FALSE;
	Barrier ();
	C.bAbort = FALSE;					// = "stopped" for core 0
	Barrier ();
	AppCoreLoop (nCore);
}

void AppCoreMain (unsigned nCore)
{
	if (nCore < APPCORE_FIRST || nCore > APPCORE_LAST || nCore >= CORES) return;
	install_vectors ();					// our IRQ/sync paths on this core too
	TAppCore &C = s_Core[nCore];
	u64 ulSP;
	asm volatile ("mov %0, sp" : "=r" (ulSP));
	C.ulStackTop = ulSP & ~15ULL;
	C.nState = CORE_IDLE;
	Barrier ();
	C.bStarted = TRUE;
	Barrier ();
	AppCoreLoop (nCore);
}

static void Redirect (TTrapFrame *pFrame, unsigned nCore)
{
	pFrame->elr_el1  = (u64) &AppCoreRestart;
	pFrame->spsr_el1 = 0x4 | (1 << 7) | (1 << 6);		// EL1t, IRQ + FIQ masked
	pFrame->sp_el0   = s_Core[nCore].ulStackTop;
	pFrame->x[0]     = nCore;
}

boolean AppCoreOnIRQExit (TTrapFrame *pFrame)
{
	unsigned nCore = CMultiCoreSupport::ThisCore ();
	if (nCore < APPCORE_FIRST || nCore >= CORES) return FALSE;
	if (s_Core[nCore].bAbort)
	{
		Redirect (pFrame, nCore);
	}
	return TRUE;
}

boolean AppCoreOnFault (TTrapFrame *pFrame)
{
	unsigned nCore = CMultiCoreSupport::ThisCore ();
	if (nCore < APPCORE_FIRST || nCore >= CORES) return FALSE;
	TAppCore &C = s_Core[nCore];
	u64 ulESR, ulFAR;
	asm volatile ("mrs %0, esr_el1" : "=r" (ulESR));
	asm volatile ("mrs %0, far_el1" : "=r" (ulFAR));
	C.nFaultEC     = (unsigned) (ulESR >> 26) & 0x3F;
	C.ulFaultPC    = pFrame->elr_el1;
	C.ulFaultAddr  = ulFAR;
	C.bFaultLogged = FALSE;
	C.nState       = CORE_FAULT;
	Barrier ();
	Redirect (pFrame, nCore);
	return TRUE;
}

// ---- core 0 side ---------------------------------------------------------------------

static CAddressSpace *CurrentAS (void)
{
	if (!CScheduler::IsActive ()) return 0;
	return (CAddressSpace *) CScheduler::Get ()->GetCurrentTask ()->GetUserData (TASK_USER_DATA_USER);
}

static TAppCore *Owned (int nCore, CAddressSpace *pAS)
{
	if (pAS == 0 || nCore < APPCORE_FIRST || nCore > APPCORE_LAST || nCore >= CORES) return 0;
	TAppCore *p = &s_Core[nCore];
	return p->pOwner == pAS ? p : 0;
}

static void LogFault (unsigned nCore)
{
	TAppCore &C = s_Core[nCore];
	if (C.nState != CORE_FAULT || C.bFaultLogged) return;
	C.bFaultLogged = TRUE;
	CLogger::Get ()->Write ("appcore", LogWarning, "core %u: fault EC=%#x at pc %lx (address %lx), job stopped",
				nCore, C.nFaultEC, (unsigned long) C.ulFaultPC, (unsigned long) C.ulFaultAddr);
}

// Drop the job on nCore (if any) and wait for the core to be back in its loop.
static boolean StopCore (unsigned nCore)
{
	TAppCore &C = s_Core[nCore];
	if (C.bLost) return FALSE;
	C.bGo = FALSE;
	Barrier ();
	if (C.nState != CORE_RUNNING) return TRUE;		// idle / faulted: nothing runs
	C.bAbort = TRUE;
	Barrier ();
	CMultiCoreSupport::SendIPI (nCore, IPI_APPCORE_STOP);
	unsigned nStart = CTimer::Get ()->GetClockTicks ();
	while (C.bAbort)
	{
		if (CTimer::Get ()->GetClockTicks () - nStart > STOP_TIMEOUT_US)
		{
			C.bLost = TRUE;
			CLogger::Get ()->Write ("appcore", LogError,
						"core %u does not answer (interrupts masked?): retired", nCore);
			return FALSE;
		}
	}
	return TRUE;
}

int kapi_core_acquire (void)
{
	CAddressSpace *pAS = CurrentAS ();
	if (pAS == 0) return -1;
	for (unsigned n = APPCORE_FIRST; n <= APPCORE_LAST && n < CORES; n++)
	{
		TAppCore &C = s_Core[n];
		if (C.bStarted && !C.bLost && C.pOwner == 0)
		{
			C.pOwner = pAS;
			C.nState = CORE_IDLE;
			Barrier ();
			return (int) n;
		}
	}
	return -1;
}

int kapi_core_run (int nCore, void (*pFunc) (void *), void *pArg, void *pStackTop)
{
	CAddressSpace *pAS = CurrentAS ();
	TAppCore *p = Owned (nCore, pAS);
	if (p == 0 || p->bLost || p->nState == CORE_RUNNING) return -1;
	u64 ulStack = (u64) pStackTop & ~15ULL;
	if (!IS_USER_VA (pFunc) || !IS_USER_VA (ulStack - 16)) return -1;
	p->ulTTBR0 = pAS->GetTTBR0 ();
	p->ulFunc  = (u64) pFunc;
	p->ulArg   = (u64) pArg;
	p->ulStack = ulStack;
	p->nState  = CORE_RUNNING;
	Barrier ();
	p->bGo = TRUE;
	Barrier ();
	asm volatile ("sev");
	return 0;
}

int kapi_core_state (int nCore)
{
	TAppCore *p = Owned (nCore, CurrentAS ());
	if (p == 0) return CORE_NOTYOURS;
	LogFault ((unsigned) nCore);
	return p->nState;
}

void kapi_core_release (int nCore)
{
	TAppCore *p = Owned (nCore, CurrentAS ());
	if (p == 0) return;
	StopCore ((unsigned) nCore);
	LogFault ((unsigned) nCore);
	p->pOwner = 0;
	Barrier ();
}

boolean AppCoreReleaseAS (CAddressSpace *pAS)
{
	boolean bOK = TRUE;
	for (unsigned n = APPCORE_FIRST; n <= APPCORE_LAST && n < CORES; n++)
	{
		TAppCore &C = s_Core[n];
		if (C.pOwner != pAS) continue;
		if (!StopCore (n)) bOK = FALSE;
		LogFault (n);
		C.pOwner = 0;
		Barrier ();
	}
	return bOK;
}
