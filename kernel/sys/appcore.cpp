//
// appcore.cpp -- cores 2 and 3 as a resource an app can acquire (see kern/appcore.h).
//
// Core side: each core waits in AppCoreLoop (WFE). When its owner starts a job, it loads
// the owner's TTBR0 (its ASID too), flushes its own TLB, and enters fn (arg) at EL0 on the
// app's stack (kern/el0.h) -- like the app itself on core 0 --, returning into the EL0 blob's
// El0CoreReturn, whose system call ends the job (AppCoreOnEl0Sync -> AppCoreEl0Done): back to
// the kernel address space and to sleep.
//
// Stopping a running job (release, app exit or kill): core 0 raises bAbort and sends the
// core an IPI. The core's IRQ comes through OUR vectors (installed on cores 2-3 too: the EL0
// ones while the job runs), and its exit (AppCoreOnIRQExit) rewrites the trap frame to return
// into AppCoreRestart, on the core's own kernel stack -- the job is simply dropped. A fault in
// the job (bad access, any system call but its end...) is caught the same way (AppCoreOnFault).
//
// Everything here is plain memory shared between cores (inner-shareable, cache coherent)
// with explicit barriers; core 0 is the only writer of the ownership.
//
// (v75) Demand paging (kern/vm.h). No allocator runs on cores 2-3, so the memory a job touches is
// filled beforehand: core_acquire fills the app's heap and makes it eager (filled as it grows),
// core_run fills the top 256 KB of the job's stack. Anything else unfilled -- a vm_map'ed buffer,
// deeper in a lazy stack -- takes the slow path: the job's translation fault posts a PAGE-IN
// request in its TAppCore (nPageIn = 1) and waits (WFE, IRQs on: a stop still drops it); on core 0
// the 100 Hz tick (an IoWait tick hook) wakes the PAGER task, which fills the page (and the next
// few of the region) by a software walk of the owner's tables, answers 2 (retry) or 3 (a real
// fault: CORE_FAULT) and SEVs. Latency: up to one tick plus a schedule.
//
// (v75) TLS: a job starts with its caller's TPIDR_EL0 (read at core_run: the kernel never changes
// it), so it shares the caller's thread-local data (errno included).
//
#include <kern/appcore.h>
#include <kern/addrspace.h>
#include <kern/trapframe.h>
#include <kern/layout.h>
#include <kern/el0.h>			// a job at EL0: El0Enter, El0CoreReturnVA
#include <kern/vm.h>			// (v75) demand paging: the page-ins, the eager heap
#include <kern/iowait.h>		// (v75) the pager's tick hook
#include <circle/sched/task.h>
#include <circle/sched/synchronizationevent.h>
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

extern "C" void install_vectors (void);
void ActivateKernelAddressSpace (void);

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
	volatile u64		ulTls;		// (v75) the job's TPIDR_EL0 (its caller's)
	volatile unsigned	nPageIn;	// (v75) 0 none, 1 asked, 2 filled (retry), 3 refused
	volatile u64		ulPageInVA;	// (v75) the page asked for
	volatile boolean	bPageInWrite;
};

#define PAGEIN_NONE	0
#define PAGEIN_ASKED	1
#define PAGEIN_DONE	2
#define PAGEIN_FAILED	3
#define PAGEIN_AHEAD	8		// pages filled from the faulting one on (one request)
#define JOB_STACK_FILL	0x40000		// core_run fills the top 256 KB of the job's stack

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
			asm volatile ("msr tpidr_el0, %0" :: "r" (C.ulTls) : "memory");	// (v75: its TLS)
			// fn at EL0 on the app's stack, returning into the blob's El0CoreReturn, whose
			// system call ends it (AppCoreOnEl0Sync -> AppCoreEl0Done, on a fresh kernel stack).
			// Its IRQ (the stop IPI) and its faults come through the EL0 vectors
			// (arch/aarch64/el0.S) to AppCoreOnIRQExit / AppCoreOnFault.
			El0Enter (C.ulFunc, C.ulStack, C.ulArg, El0CoreReturnVA ());	// (no return)
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
	C.nPageIn = PAGEIN_NONE;				// (a page-in it waited for: dropped)
	Barrier ();
	C.bAbort = FALSE;					// = "stopped" for core 0
	Barrier ();
	AppCoreLoop (nCore);
}

// A job returned (El0CoreReturn): here at EL1t on the core's own kernel stack (the frame was
// redirected).
extern "C" void __attribute__ ((noreturn)) AppCoreEl0Done (unsigned nCore)
{
	TAppCore &C = s_Core[nCore];
	ActivateKernelAddressSpace ();
	Barrier ();
	if (C.nState == CORE_RUNNING) C.nState = CORE_IDLE;
	Barrier ();
	asm volatile ("sev");
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

// A fault in the job (an EL0 synchronous exception: anything but its end and an emulated ID
// register read): recorded for kapi_core_state / the log, the job dropped.
static void AppCoreOnFault (TTrapFrame *pFrame)
{
	unsigned nCore = CMultiCoreSupport::ThisCore ();
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
}

// (v75) A translation fault of the job at a user address: a page-in asked of core 0 (the pager
// task), the core waiting meanwhile -- WFE with the IRQs on, so a stop IPI still drops the job
// (AppCoreOnIRQExit redirects the IRQ's own frame). TRUE: the page is there, retry the access.
static boolean PageIn (unsigned nCore)
{
	u64 ulESR, ulFAR;
	asm volatile ("mrs %0, esr_el1" : "=r" (ulESR));
	asm volatile ("mrs %0, far_el1" : "=r" (ulFAR));
	unsigned nEC = (unsigned) (ulESR >> 26) & 0x3F;
	if (   (nEC != 0x24 && nEC != 0x20)			// a data abort from EL0, (v78) an instruction abort
	    || (ulESR & (1ULL << 10)) != 0			// (FnV: no address)
	    || (ulESR & 0x3C) != 0x04			// a translation fault
	    || !IS_USER_VA (ulFAR))
	{
		return FALSE;
	}
	// (an instruction abort: the page filled as read; where the region is not executable the
	// retried fetch then takes a permission fault, which is the program's end)
	TAppCore &C = s_Core[nCore];
	C.ulPageInVA = ulFAR & ~(u64) KPAGE_MASK;
	C.bPageInWrite = (ulESR & (1ULL << 6)) != 0;
	Barrier ();
	C.nPageIn = PAGEIN_ASKED;
	Barrier ();
	asm volatile ("sev");
	asm volatile ("msr daifclr, #2" ::: "memory");
	while (C.nPageIn == PAGEIN_ASKED)
	{
		asm volatile ("wfe" ::: "memory");
	}
	asm volatile ("msr daifset, #2" ::: "memory");
	Barrier ();
	boolean bOK = C.nPageIn == PAGEIN_DONE;
	C.nPageIn = PAGEIN_NONE;
	Barrier ();
	return bOK;		// (no TLBI: an invalid entry is never cached; the eret retries)
}

void AppCoreOnEl0Sync (TTrapFrame *pFrame, boolean bDone)
{
	unsigned nCore = CMultiCoreSupport::ThisCore ();
	if (nCore < APPCORE_FIRST || nCore >= CORES) return;	// (no EL0 code runs elsewhere)
	if (!bDone)
	{
		if (PageIn (nCore))
		{
			return;				// (v75) filled: the access retried
		}
		AppCoreOnFault (pFrame);		// (a system call there is a fault too: EC 0x15)
		return;
	}
	Redirect (pFrame, nCore);
	pFrame->elr_el1 = (u64) &AppCoreEl0Done;
}

// ---- core 0 side ---------------------------------------------------------------------

static CAddressSpace *CurrentAS (void)
{
	if (!CScheduler::IsActive ()) return 0;
	return (CAddressSpace *) CScheduler::Get ()->GetCurrentTask ()->GetUserData (TASK_USER_DATA_USER);
}

// (v75) The pager: a kernel task on core 0 that serves the app cores' page-in requests.
class CVmPagerTask : public CTask
{
public:
	CVmPagerTask (void)
	{
		SetName ("vmpager");
	}

	void Run (void) override
	{
		for (;;)
		{
			// (woken by the tick hook; the timeout covers a hook that could not be added)
			m_Event.WaitWithTimeout (s_bHooked ? 1000000 : 10000);
			m_Event.Clear ();
			Serve ();
		}
	}

	void Wake (void)	{ m_Event.Set (); }

	static volatile boolean s_bHooked;

private:
	static void Serve (void)
	{
		for (unsigned n = APPCORE_FIRST; n <= APPCORE_LAST && n < CORES; n++)
		{
			TAppCore &C = s_Core[n];
			if (C.nPageIn != PAGEIN_ASKED) continue;
			Barrier ();
			CAddressSpace *pAS = C.pOwner;		// (core 0's: stable here)
			u64 ulVA = C.ulPageInVA;
			int r = pAS != 0 ? VmFaultIn (pAS, ulVA, C.bPageInWrite) : -1;
			if (r > 0)				// (filled: the next pages of a lazy region too)
			{
				for (unsigned k = 1; k < PAGEIN_AHEAD; k++)
				{
					if (VmFaultIn (pAS, ulVA + (u64) k * KPAGE_SIZE, FALSE) <= 0) break;
				}
			}
			Barrier ();
			C.nPageIn = r >= 0 ? PAGEIN_DONE : PAGEIN_FAILED;
			Barrier ();
			asm volatile ("sev");
		}
	}

	CSynchronizationEvent m_Event;
};

volatile boolean CVmPagerTask::s_bHooked = FALSE;
static CVmPagerTask *s_pPager = 0;

// The 100 Hz tick (IRQ, core 0): a request pending -> the pager woken.
static void AppCorePageInTick (void)
{
	if (s_pPager == 0) return;
	for (unsigned n = APPCORE_FIRST; n <= APPCORE_LAST && n < CORES; n++)
	{
		if (s_Core[n].nPageIn == PAGEIN_ASKED)
		{
			s_pPager->Wake ();
			return;
		}
	}
}

// The pager made on the first core_acquire (core 0, a task).
static void StartPager (void)
{
	if (s_pPager != 0) return;
	s_pPager = new CVmPagerTask;
	if (s_pPager != 0)
	{
		CVmPagerTask::s_bHooked = IoWaitAddTickHook (AppCorePageInTick);
	}
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
			// (v75) Its jobs touch what the main thread allocated: the heap filled now and
			// as it grows (kern/vm.h); the pager for the rest. (May yield: the core is ours.)
			StartPager ();
			VmEagerHeap (pAS);
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
	// (v75) The top of its stack filled now (the rest, if touched: page-ins); its TLS: ours.
	VmPopulate (pAS, ulStack > JOB_STACK_FILL ? ulStack - JOB_STACK_FILL : 0, ulStack, FALSE);
	u64 ulTls;
	asm volatile ("mrs %0, tpidr_el0" : "=r" (ulTls));
	p->ulTls = ulTls;
	p->nPageIn = PAGEIN_NONE;
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
