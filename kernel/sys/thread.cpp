//
// thread.cpp -- an app's threads, mutexes / events / barriers, and calls posted to its
// event pump (ABI v67). The model is in kern/thread.h.
//
#include <kern/thread.h>
#include <kern/addrspace.h>
#include <kern/gui/window.h>
#include <kern/kapi_abi.h>		// KAPI_WAIT_FOREVER
#include <kern/layout.h>		// KERNEL_IDENTITY_END
#include <kern/el0.h>			// a thread at EL0: El0Enter, its stacks
#include <kern/uaccess.h>		// the app's pointers (name, code, word, post)
#include <kern/vm.h>			// (v75) lazy stacks, the pins, the zapped frames' waiters
#include <kern/kapi_abi.h>		// (v75) struct kapi_thread_attr / kapi_thread_info, KAPI_E*
#include <circle/sched/scheduler.h>
#include <circle/sched/task.h>
#include <circle/timer.h>
#include <circle/string.h>
#include <circle/util.h>
#include <circle/new.h>
#include <circle/spinlock.h>
#include <circle/synchronize.h>

extern "C" void kapi_exit (int nStatus);

static void WordWaitsFree (CAddressSpace *pAS);

enum { SYNC_MUTEX = 1, SYNC_EVENT, SYNC_BARRIER };

struct TSyncObj
{
	u8	 nType;			// SYNC_*
	boolean	 bClosed;		// kapi_sync_close'd, still waited on: the last waiter frees it
	boolean	 bManual;		// event: manual reset
	boolean	 bState;		// event: signalled
	unsigned nSlot;			// its index in CProcThreads::pObj
	unsigned nWaiters;		// tasks blocked on Ev
	CTask	*pOwner;		// mutex: the owner (0: free)
	unsigned nCount;		// mutex: recursion depth; barrier: its parties
	unsigned nArrived, nGen;	// barrier: arrived this round, the round
	CSynchronizationEvent Ev;	// only ever Pulse()d: a condition variable
};

CProcThreads::CProcThreads (void)
:	nNextTid (2),
	nSeq (0),
	nJoinWaiters (0),
	nPostHead (0),
	nPostTail (0),
	nPostsLost (0),
	nWakeWaiters (0)
{
	memset (Rec, 0, sizeof Rec);
	for (unsigned i = 0; i < SYNC_OBJS_MAX; i++) pObj[i] = 0;
}

// Wake every task waiting on Ev, leaving it cleared: a condition variable's broadcast
// (Circle's Pulse is private). Kernel code is not preempted between the two.
static inline void Pulse (CSynchronizationEvent &Ev)
{
	Ev.Set ();
	Ev.Clear ();
}

static CTask *CurrentTask (void)
{
	return CScheduler::IsActive () ? CScheduler::Get ()->GetCurrentTask () : 0;
}

static CAddressSpace *CurrentAS (void)
{
	CTask *pTask = CurrentTask ();
	return pTask != 0 ? (CAddressSpace *) pTask->GetUserData (TASK_USER_DATA_USER) : 0;
}

CProcThreads *ThreadsOf (CAddressSpace *pAS, boolean bCreate)
{
	if (pAS == 0) return 0;
	CProcThreads *pT = pAS->GetThreads ();
	if (pT == 0 && bCreate)
	{
		pT = new CProcThreads;
		pAS->SetThreads (pT);
	}
	return pT;
}

static CProcThreads *MyThreads (boolean bCreate)
{
	return ThreadsOf (CurrentAS (), bCreate);
}

void ThreadsFree (CAddressSpace *pAS)
{
	WordWaitsFree (pAS);				// (its word waiters are on its tasks' stacks)
	CProcThreads *pT = pAS->GetThreads ();
	if (pT == 0) return;
	if (pAS->GetWindow () != 0) pAS->GetWindow ()->SetWake (0);
	// Every task of the process has ended (the space dies with its last one), but killed
	// ones may still be on an object's wait list: a Pulse unlinks them (they stay
	// terminated), then the event can go.
	for (unsigned i = 0; i < SYNC_OBJS_MAX; i++)
	{
		if (pT->pObj[i] != 0)
		{
			Pulse (pT->pObj[i]->Ev);
			delete pT->pObj[i];
			pT->pObj[i] = 0;
		}
	}
	Pulse (pT->DoneEv);
	Pulse (pT->WakeEv);
	pAS->SetThreads (0);
	delete pT;
}

void ThreadsEndProcess (void)
{
	CAddressSpace *pAS = CurrentAS ();
	if (pAS != 0 && CScheduler::IsActive ())
	{
		CScheduler::Get ()->TerminateGroup (pAS);	// (all but the caller)
	}
}

// ---- waiting ------------------------------------------------------------------

// The deadline of a wait of nTimeoutMs from now (KAPI_WAIT_FOREVER: none), in clock ticks (us).
static unsigned Deadline (unsigned nTimeoutMs)
{
	if (nTimeoutMs > 1800000) nTimeoutMs = 1800000;		// (30 min: the clock's range)
	return CTimer::Get ()->GetClockTicks () + nTimeoutMs * 1000;
}

// Block on Ev until it is pulsed or the deadline passes. FALSE: the deadline has passed
// (the caller checks its condition again either way before giving up).
static boolean WaitPulse (CSynchronizationEvent &Ev, unsigned &nWaiters, boolean bForever,
			  unsigned nDeadline)
{
	if (bForever)
	{
		nWaiters++;
		Ev.Wait ();
		nWaiters--;
		return TRUE;
	}
	int nLeft = (int) (nDeadline - CTimer::Get ()->GetClockTicks ());
	if (nLeft <= 0) return FALSE;
	nWaiters++;
	Ev.WaitWithTimeout ((unsigned) nLeft);
	nWaiters--;
	return TRUE;
}

static void FreeObj (CProcThreads *pT, TSyncObj *o)
{
	pT->pObj[o->nSlot] = 0;
	delete o;
}

// Wait on an object. 0: woken (check again), -1: the deadline passed, -2: the object was
// closed meanwhile (freed here by its last waiter).
static int WaitObj (CProcThreads *pT, TSyncObj *o, boolean bForever, unsigned nDeadline)
{
	boolean bOk = WaitPulse (o->Ev, o->nWaiters, bForever, nDeadline);
	if (o->bClosed)
	{
		if (o->nWaiters == 0) FreeObj (pT, o);
		return -2;
	}
	return bOk ? 0 : -1;
}

static TSyncObj *Obj (CProcThreads *pT, int h, unsigned nType)
{
	if (pT == 0 || h < 1 || h > SYNC_OBJS_MAX) return 0;
	TSyncObj *o = pT->pObj[h - 1];
	if (o == 0 || o->bClosed || (nType != 0 && o->nType != nType)) return 0;
	return o;
}

static int NewObj (unsigned nType)
{
	CProcThreads *pT = MyThreads (TRUE);
	if (pT == 0) return -1;
	for (unsigned i = 0; i < SYNC_OBJS_MAX; i++)
	{
		if (pT->pObj[i] != 0) continue;
		TSyncObj *o = new TSyncObj;
		if (o == 0) return -1;
		o->nType = (u8) nType;
		o->bClosed = o->bManual = o->bState = FALSE;
		o->nSlot = i;
		o->nWaiters = 0;
		o->pOwner = 0;
		o->nCount = o->nArrived = o->nGen = 0;
		pT->pObj[i] = o;
		return (int) i + 1;
	}
	return -1;					// (SYNC_OBJS_MAX in use)
}

// ---- threads ------------------------------------------------------------------

class CUserThreadTask : public CTask
{
public:
	// fn runs at EL0 (kern/el0.h) on the user stack whose top is ulUserStack, and returns into
	// the blob's El0ThreadReturn (thread_exit); this task's own stack is the thread's kernel stack.
	// (v75) TPIDR_EL0 starts as ulTls (its TLS: 0 for kapi_thread_create); the task switch
	// (Circle's TaskSwitch) keeps whatever the thread writes there.
	CUserThreadTask (CAddressSpace *pAS, u64 ulFunc, u64 ulArg, u64 ulUserStack, u64 ulTls)
	:	CTask (EL0_KSTACK_SIZE),
		m_ulFunc (ulFunc),
		m_ulArg (ulArg),
		m_ulUserStack (ulUserStack),
		m_ulTls (ulTls)
	{
		// (no Yield between the CTask constructor, which made it ready, and here: it
		// is first scheduled in its process's address space)
		SetUserData (pAS, TASK_USER_DATA_USER);
		pAS->AddTask (this);
	}

	void Run (void) override
	{
		asm volatile ("msr tpidr_el0, %0" :: "r" (m_ulTls) : "memory");
		El0Enter (m_ulFunc, m_ulUserStack, m_ulArg, El0ThreadReturnVA ());	// (no return)
	}

private:
	u64 m_ulFunc, m_ulArg;
	u64 m_ulUserStack;
	u64 m_ulTls;
};

static TThreadRec *RecOf (CProcThreads *pT, CTask *pTask)
{
	for (unsigned i = 0; i < THREAD_RECS; i++)
		if (pT->Rec[i].nTid != 0 && pT->Rec[i].pTask == pTask) return &pT->Rec[i];
	return 0;
}

// A new thread of the current process (pName: the kernel's copy, may be 0). -> its tid (>= 2), or
// -KAPI_EAGAIN (THREADS_MAX running), -KAPI_ENOMEM, -KAPI_EINVAL.
static int CreateThread (u64 ulFunc, u64 ulArg, u64 nStackSize, const char *pName, u64 ulTls,
			 boolean bDetached, int nPrio)
{
	CAddressSpace *pAS = CurrentAS ();
	CProcThreads *pT = ThreadsOf (pAS, TRUE);
	if (pT == 0) return -KAPI_ENOMEM;
	if (ulFunc == 0 || !IS_USER_VA (ulFunc)) return -KAPI_EINVAL;

	unsigned nRunning = 0;
	TThreadRec *pRec = 0, *pOldest = 0;
	for (unsigned i = 0; i < THREAD_RECS; i++)
	{
		TThreadRec *r = &pT->Rec[i];
		if (r->nTid == 0) { if (pRec == 0) pRec = r; continue; }
		if (!r->bDone) { nRunning++; continue; }
		if (pOldest == 0 || (int) (r->nSeq - pOldest->nSeq) < 0) pOldest = r;
	}
	if (nRunning >= THREADS_MAX) return -KAPI_EAGAIN;	// the per-process limit
	if (pRec == 0) pRec = pOldest;			// (an ended one never joined: forgotten)
	if (pRec == 0) return -KAPI_EAGAIN;

	// The thread's user stack (nStackSize) at the top of the slot of its record: a LAZY region
	// (kern/vm.h: its pages filled on first touch), the rest of the slot unmapped -- the guard
	// below it. A slot reused at the same size keeps its region (its pages were dropped when the
	// last thread there ended); another size replaces it. The task's own stack is only its
	// kernel stack (kern/el0.h).
	u64 ulTop = USER_THREAD_STACKS + (u64) (pRec - pT->Rec + 1) * USER_THREAD_SLOT;
	if (VmMapStack (pAS, ulTop, nStackSize, USER_THREAD_SLOT) != 0)
	{
		return -KAPI_ENOMEM;
	}
	CUserThreadTask *pTask = new CUserThreadTask (pAS, ulFunc, ulArg, ulTop, ulTls);
	if (pTask == 0)
	{
		return -KAPI_ENOMEM;
	}
	unsigned nTid = pT->nNextTid++;
	pRec->nTid = nTid;
	pRec->bDone = FALSE;
	pRec->bDetached = bDetached;
	pRec->nCode = 0;
	pRec->nSeq = 0;
	pRec->pTask = pTask;
	pRec->ulStackLo = KPAGE_ALIGN_DOWN (ulTop - nStackSize);
	pRec->ulStackHi = ulTop;

	// "<app>:<name>" (or "<app>:<tid>"): the app's name is the caller's, after its last '/'
	// (a tool's task is named by its path, "SD:/bin/threadtest") and up to a ':' (a thread)
	CString Name;
	const char *pMine = CurrentTask ()->GetName ();
	for (const char *p = pMine; *p != '\0'; p++) if (*p == '/') pMine = p + 1;
	unsigned n = 0;
	char App[32];
	while (pMine[n] != '\0' && pMine[n] != ':' && n < sizeof App - 1) { App[n] = pMine[n]; n++; }
	App[n] = '\0';
	if (pName != 0 && pName[0] != '\0')
	{
		Name.Format ("%s:%s", App, pName);		// (the kernel's copy)
	}
	else
	{
		Name.Format ("%s:%u", App, nTid);
	}
	pTask->SetName (Name);

	if (nPrio > 0)
	{
		CScheduler::Get ()->SetPriority (pTask, 1);	// ("real time", as thread_priority)
	}
	return (int) nTid;
}

int kapi_thread_create (int (*pFunc) (void *), void *pArg, unsigned nStackSize, const char *pName)
{
	CUserStr Given (pName, 32, TRUE);		// (its name: 31 characters kept)
	if (pFunc == 0 || (!Given.OK () && !Given.IsNull ())) return -1;

	if (nStackSize == 0) nStackSize = THREAD_STACK_DEFAULT;
	if (nStackSize < THREAD_STACK_MIN) nStackSize = THREAD_STACK_MIN;
	if (nStackSize > THREAD_STACK_MAX) nStackSize = THREAD_STACK_MAX;
	nStackSize = (nStackSize + 15) & ~15u;

	int r = CreateThread ((u64) pFunc, (u64) pArg, nStackSize, Given.Get (), 0, FALSE, 0);
	return r > 0 ? r : (r == -KAPI_EAGAIN ? -2 : -1);	// (the v67 values)
}

// (v75) A thread with its attributes (struct kapi_thread_attr): its TLS (the initial TPIDR_EL0),
// an 8 MB lazy stack by default, detached or not, its priority.
int kapi_thread_create_ex (const struct kapi_thread_attr *pAttr)
{
	struct kapi_thread_attr A;
	if (pAttr == 0 || !UserGet (&A, pAttr)) return -KAPI_EFAULT;
	if (   A.fn == 0 || (A.flags & ~(unsigned) KAPI_THREAD_DETACHED) != 0
	    || A.prio < 0 || A.prio > 1 || A.reserved[0] != 0 || A.reserved[1] != 0)
	{
		return -KAPI_EINVAL;
	}
	u64 nStack = A.stack_size != 0 ? A.stack_size : THREAD_STACK_DEFAULT_EX;
	if (nStack < THREAD_STACK_MIN || nStack > THREAD_STACK_MAX) return -KAPI_EINVAL;
	nStack = (nStack + 15) & ~(u64) 15;
	CUserStr Given (A.name, 32, TRUE);
	if (!Given.OK () && !Given.IsNull ()) return -KAPI_EFAULT;
	return CreateThread (A.fn, A.arg, nStack, Given.Get (), A.tls,
			     (A.flags & KAPI_THREAD_DETACHED) != 0, A.prio);
}

// (v75) A thread's stack bounds and state (tid 0: the caller, 1: the main thread).
int kapi_thread_info (int nTid, struct kapi_thread_info *pOut)
{
	CAddressSpace *pAS = CurrentAS ();
	if (pAS == 0) return -KAPI_ESRCH;
	CTask *pMe = CurrentTask ();
	CProcThreads *pT = ThreadsOf (pAS, FALSE);
	if (nTid == 0)
	{
		TThreadRec *r = (pMe != pAS->GetMainTask () && pT != 0) ? RecOf (pT, pMe) : 0;
		nTid = r != 0 ? (int) r->nTid : 1;
	}
	struct kapi_thread_info Out;
	memset (&Out, 0, sizeof Out);
	Out.tid = nTid;
	if (nTid == 1)
	{
		TVma V;					// (the main stack: below USER_STACK_TOP)
		if (!VmRegionAt (pAS, USER_STACK_TOP - 1, &V)) return -KAPI_ESRCH;
		Out.stack_lo = V.ulStart;
		Out.stack_hi = V.ulEnd;
		Out.state = 0;
	}
	else
	{
		TThreadRec *r = 0;
		for (unsigned i = 0; pT != 0 && i < THREAD_RECS && r == 0; i++)
			if (nTid >= 2 && pT->Rec[i].nTid == (unsigned) nTid) r = &pT->Rec[i];
		if (r == 0) return -KAPI_ESRCH;
		Out.stack_lo = r->ulStackLo;
		Out.stack_hi = r->ulStackHi;
		Out.state = r->bDone ? 1 : 0;
	}
	Out.guard = Out.stack_lo - VmRegionEndBelow (pAS, Out.stack_lo);
	return UserPut (pOut, Out) ? 0 : -KAPI_EFAULT;
}

void kapi_thread_exit (int nCode)
{
	CAddressSpace *pAS = CurrentAS ();
	CTask *pMe = CurrentTask ();
	if (pAS == 0 || pMe == pAS->GetMainTask ())
	{
		kapi_exit (nCode);			// the main thread: the process ends
	}

	CProcThreads *pT = ThreadsOf (pAS, FALSE);
	VmUnpinTask (pAS, pMe);				// (v75: its kapi never returns)
	if (pT != 0)
	{
		TThreadRec *r = RecOf (pT, pMe);
		if (r != 0)
		{
			// (v75) Its user stack's pages dropped (the region stays for the slot's next
			// thread); a detached thread's record is free at once.
			VmDiscard (pAS, r->ulStackLo, r->ulStackHi);
			r->bDone = TRUE;
			r->nCode = nCode;
			r->pTask = 0;
			r->nSeq = ++pT->nSeq;
			if (r->bDetached) r->nTid = 0;
		}
		// Its mutexes are abandoned: released, so the other threads do not wait for ever.
		for (unsigned i = 0; i < SYNC_OBJS_MAX; i++)
		{
			TSyncObj *o = pT->pObj[i];
			if (o != 0 && o->nType == SYNC_MUTEX && o->pOwner == pMe)
			{
				o->pOwner = 0;
				o->nCount = 0;
				Pulse (o->Ev);
			}
		}
		Pulse (pT->DoneEv);			// (its joiners)
	}
	pMe->Terminate ();				// the reaper frees it; the process goes on
	for (;;) { }
}

int kapi_thread_join (int nTid, unsigned nTimeoutMs, int *pCode)
{
	CProcThreads *pT = MyThreads (FALSE);
	if (pT == 0 || nTid < 2) return -2;
	if (pCode != 0 && !UserRange (pCode, sizeof *pCode)) return -2;	// (before it is joined)
	TThreadRec *r = 0;
	for (unsigned i = 0; i < THREAD_RECS && r == 0; i++)
		if (pT->Rec[i].nTid == (unsigned) nTid) r = &pT->Rec[i];
	if (r == 0) return -2;				// no such thread (or already joined)
	if (!r->bDone && r->pTask == CurrentTask ()) return -3;	// itself

	boolean bForever = nTimeoutMs == KAPI_WAIT_FOREVER;
	unsigned nDeadline = Deadline (nTimeoutMs);
	for (;;)
	{
		if (r->nTid != (unsigned) nTid) return -2;	// joined by another thread meanwhile
		if (r->bDone)
		{
			if (pCode != 0) UserPut (pCode, r->nCode);
			r->nTid = 0;				// (the record is free again)
			return 0;
		}
		if (nTimeoutMs == 0 || !WaitPulse (pT->DoneEv, pT->nJoinWaiters, bForever, nDeadline))
		{
			return -1;
		}
	}
}

int kapi_thread_self (void)
{
	CAddressSpace *pAS = CurrentAS ();
	CTask *pMe = CurrentTask ();
	if (pAS == 0 || pMe == pAS->GetMainTask ()) return 1;
	CProcThreads *pT = ThreadsOf (pAS, FALSE);
	TThreadRec *r = pT != 0 ? RecOf (pT, pMe) : 0;
	return r != 0 ? (int) r->nTid : 1;
}

// ---- mutexes ------------------------------------------------------------------

int kapi_mutex_create (void)
{
	return NewObj (SYNC_MUTEX);
}

int kapi_mutex_lock (int h, unsigned nTimeoutMs)
{
	CProcThreads *pT = MyThreads (FALSE);
	TSyncObj *o = Obj (pT, h, SYNC_MUTEX);
	if (o == 0) return -2;
	CTask *pMe = CurrentTask ();
	boolean bForever = nTimeoutMs == KAPI_WAIT_FOREVER;
	unsigned nDeadline = Deadline (nTimeoutMs);
	for (;;)
	{
		if (o->pOwner == 0) { o->pOwner = pMe; o->nCount = 1; return 0; }
		if (o->pOwner == pMe) { o->nCount++; return 0; }	// (recursive)
		if (nTimeoutMs == 0) return -1;
		int r = WaitObj (pT, o, bForever, nDeadline);
		if (r != 0) return r;
	}
}

int kapi_mutex_unlock (int h)
{
	TSyncObj *o = Obj (MyThreads (FALSE), h, SYNC_MUTEX);
	if (o == 0) return -2;
	if (o->pOwner != CurrentTask ()) return -1;	// not ours
	if (--o->nCount == 0)
	{
		o->pOwner = 0;
		Pulse (o->Ev);
	}
	return 0;
}

// ---- events -------------------------------------------------------------------

int kapi_event_create (int bManualReset, int bInitial)
{
	int h = NewObj (SYNC_EVENT);
	if (h > 0)
	{
		TSyncObj *o = MyThreads (FALSE)->pObj[h - 1];
		o->bManual = bManualReset ? TRUE : FALSE;
		o->bState = bInitial ? TRUE : FALSE;
	}
	return h;
}

int kapi_event_set (int h)
{
	TSyncObj *o = Obj (MyThreads (FALSE), h, SYNC_EVENT);
	if (o == 0) return -2;
	o->bState = TRUE;
	Pulse (o->Ev);			// (auto reset: the first waiter to run takes it)
	return 0;
}

int kapi_event_reset (int h)
{
	TSyncObj *o = Obj (MyThreads (FALSE), h, SYNC_EVENT);
	if (o == 0) return -2;
	o->bState = FALSE;
	return 0;
}

int kapi_event_wait (int h, unsigned nTimeoutMs)
{
	CProcThreads *pT = MyThreads (FALSE);
	TSyncObj *o = Obj (pT, h, SYNC_EVENT);
	if (o == 0) return -2;
	boolean bForever = nTimeoutMs == KAPI_WAIT_FOREVER;
	unsigned nDeadline = Deadline (nTimeoutMs);
	for (;;)
	{
		if (o->bState)
		{
			if (!o->bManual) o->bState = FALSE;
			return 0;
		}
		if (nTimeoutMs == 0) return -1;
		int r = WaitObj (pT, o, bForever, nDeadline);
		if (r != 0) return r;
	}
}

// ---- barriers -----------------------------------------------------------------

int kapi_barrier_create (unsigned nCount)
{
	if (nCount == 0) return -1;
	int h = NewObj (SYNC_BARRIER);
	if (h > 0) MyThreads (FALSE)->pObj[h - 1]->nCount = nCount;
	return h;
}

int kapi_barrier_wait (int h)
{
	CProcThreads *pT = MyThreads (FALSE);
	TSyncObj *o = Obj (pT, h, SYNC_BARRIER);
	if (o == 0) return -2;
	unsigned nGen = o->nGen;
	if (++o->nArrived >= o->nCount)
	{
		o->nArrived = 0;
		o->nGen++;
		Pulse (o->Ev);
		return 1;				// the last one in (the "serial" thread)
	}
	while (o->nGen == nGen)
	{
		int r = WaitObj (pT, o, TRUE, 0);
		if (r != 0) return r;
	}
	return 0;
}

int kapi_sync_close (int h)
{
	CProcThreads *pT = MyThreads (FALSE);
	TSyncObj *o = Obj (pT, h, 0);
	if (o == 0) return -2;
	o->bClosed = TRUE;
	Pulse (o->Ev);				// its waiters return -2
	if (o->nWaiters == 0) FreeObj (pT, o);	// (else its last waiter frees it)
	return 0;
}

// ---- posted calls and the pump ------------------------------------------------

int kapi_post (void (*pFunc) (void *, long), void *pCtx, long lValue)
{
	CProcThreads *pT = MyThreads (TRUE);
	if (pT == 0 || pFunc == 0) return -2;
	unsigned nNext = (pT->nPostHead + 1) % POSTS_MAX;
	if (nNext == pT->nPostTail)
	{
		pT->nPostsLost++;
		return -1;				// full: the pump is not keeping up
	}
	TPost &P = pT->Posts[pT->nPostHead];
	P.ulFunc = (u64) pFunc;
	P.ulCtx = (u64) pCtx;
	P.lValue = lValue;
	pT->nPostHead = nNext;
	Pulse (pT->WakeEv);
	return 0;
}

// (v73) pump_wait's sleep, without the pump: the user-side pump_wait / wait_for_exit (the EL0
// table's, kern/el0.h) sleep here, then pump themselves.
int kapi_pump_sleep (unsigned nTimeoutMs)
{
	CAddressSpace *pAS = CurrentAS ();
	CProcThreads *pT = ThreadsOf (pAS, TRUE);
	if (pT == 0) return -1;
	CWindow *pWin = pAS->GetWindow ();
	if (pWin != 0) pWin->SetWake (&pT->WakeEv);

	// (no Yield between these checks and the wait: a post or an event cannot slip in)
	unsigned nPending = (pT->nPostHead + POSTS_MAX - pT->nPostTail) % POSTS_MAX;
	if (pWin != 0) nPending += pWin->QueuedEvents () + (pWin->ShouldExit () ? 1 : 0);
	if (nPending == 0 && nTimeoutMs != 0)
	{
		WaitPulse (pT->WakeEv, pT->nWakeWaiters, nTimeoutMs == KAPI_WAIT_FOREVER,
			   Deadline (nTimeoutMs));
		nPending = (pT->nPostHead + POSTS_MAX - pT->nPostTail) % POSTS_MAX;
		if (pWin != 0) nPending += pWin->QueuedEvents () + (pWin->ShouldExit () ? 1 : 0);
	}
	return (int) nPending;
}

// (v73) The next posted call, not run: the user-side pump (kern/el0.h) runs it at EL0.
int kapi_pop_post (struct kapi_posted *pPost)
{
	CProcThreads *pT = MyThreads (FALSE);
	if (pT == 0 || pPost == 0 || pT->nPostTail == pT->nPostHead) return 0;
	struct kapi_posted Out;				// (made here, copied out whole: kern/uaccess.h)
	memset (&Out, 0, sizeof Out);
	TPost P = pT->Posts[pT->nPostTail];
	Out.fn = P.ulFunc;
	Out.ctx = P.ulCtx;
	Out.value = P.lValue;
	if (!UserPut (pPost, Out)) return 0;		// (a bad pointer: the post stays queued)
	pT->nPostTail = (pT->nPostTail + 1) % POSTS_MAX;
	return 1;
}

// ---- word waits (a futex) -----------------------------------------------------

struct TWordWaiter
{
	u64		       ulPhys;		// the word's physical address (== its identity address)
	unsigned	       nExpected;
	CAddressSpace	      *pAS;		// (unlinked when the process dies)
	CSynchronizationEvent *pEv;
	volatile boolean       bWoken;
	TWordWaiter	      *pNext;
};

static TWordWaiter *s_pWordWaiters = 0;
static CSpinLock s_WordLock (IRQ_LEVEL);		// (the tick walks the list in its interrupt)

// The physical address of a word of the calling process (any address it can read: its data,
// its heap, a surface, its stack). (v75) Probed as the app's (kern/uaccess.h: a lazy page filled,
// the page pinned for the call -- so its frame stays while the caller waits on it), then
// translated by the MMU (AT S1E0R; S1E1R for a kernel caller). FALSE: not the caller's, not
// readable, or out of the kernel's identity map (the tick could not read it there).
static boolean WordPhys (const volatile unsigned *pWord, u64 *pPhys)
{
	if (!UserReadable ((const void *) pWord, sizeof *pWord))
	{
		return FALSE;
	}
	u64 nFlags, nPAR;
	asm volatile ("mrs %0, daif; msr daifset, #3" : "=r" (nFlags) :: "memory");	// (PAR_EL1 kept)
	if (UserIsKernelCaller ())
	{
		asm volatile ("at s1e1r, %1\n\tisb\n\tmrs %0, par_el1" : "=r" (nPAR) : "r" (pWord) : "memory");
	}
	else
	{
		asm volatile ("at s1e0r, %1\n\tisb\n\tmrs %0, par_el1" : "=r" (nPAR) : "r" (pWord) : "memory");
	}
	asm volatile ("msr daif, %0" :: "r" (nFlags) : "memory");
	if (nPAR & 1)
	{
		return FALSE;				// (the translation faulted)
	}
	u64 ulPhys = (nPAR & 0x0000FFFFFFFFF000ULL) | ((u64) (uintptr) pWord & 0xFFF);
	if (ulPhys + sizeof (unsigned) > KERNEL_IDENTITY_END)
	{
		return FALSE;
	}
	*pPhys = ulPhys;
	return TRUE;
}

static void UnlinkWaiter (TWordWaiter *pW)		// (s_WordLock held)
{
	for (TWordWaiter **pp = &s_pWordWaiters; *pp != 0; pp = &(*pp)->pNext)
	{
		if (*pp == pW)
		{
			*pp = pW->pNext;
			return;
		}
	}
}

int kapi_wait_word (volatile unsigned *pWord, unsigned nExpected, unsigned nTimeoutMs)
{
	u64 ulPhys;
	if (   pWord == 0
	    || ((uintptr) pWord & 3) != 0
	    || !CScheduler::IsActive ()
	    || !UserRange ((const void *) pWord, sizeof *pWord)	// (the caller's: kern/uaccess.h)
	    || !WordPhys (pWord, &ulPhys))
	{
		return -1;
	}
	if (*pWord != nExpected) return 0;
	if (nTimeoutMs == 0) return 1;

	CSynchronizationEvent Ev;			// (on this task's stack: identity-mapped)
	TWordWaiter W;
	W.ulPhys = ulPhys;
	W.nExpected = nExpected;
	W.pAS = CurrentAS ();
	W.pEv = &Ev;
	W.bWoken = FALSE;

	// Linked first, then the word read again: a change after this is seen by the tick or
	// wakes us (Set before our Wait leaves the event set: Wait returns at once). The IRQ stays
	// masked until the task is on the event's list: Circle's Wait tests the state, then blocks
	// -- a tick's Set in between would be lost. (Yield keeps each task's own DAIF: the others
	// run with their IRQs; we come back masked and unmask here.)
	u64 nFlags;
	asm volatile ("mrs %0, daif; msr daifset, #2" : "=r" (nFlags) :: "memory");
	s_WordLock.Acquire ();
	W.pNext = s_pWordWaiters;
	s_pWordWaiters = &W;
	DataMemBarrier ();
	boolean bChanged = *(volatile unsigned *) (uintptr) ulPhys != nExpected;
	if (bChanged) UnlinkWaiter (&W);
	s_WordLock.Release ();
	if (!bChanged)
	{
		if (nTimeoutMs == KAPI_WAIT_FOREVER)
		{
			Ev.Wait ();
		}
		else
		{
			if (nTimeoutMs > 1800000) nTimeoutMs = 1800000;	// (30 min: the clock's range)
			Ev.WaitWithTimeout (nTimeoutMs * 1000);
		}
	}
	asm volatile ("msr daif, %0" :: "r" (nFlags) : "memory");
	if (bChanged) return 0;

	s_WordLock.Acquire ();
	if (!W.bWoken) UnlinkWaiter (&W);		// (the timeout: still linked)
	boolean bWoken = W.bWoken;
	s_WordLock.Release ();
	if (bWoken || *(volatile unsigned *) (uintptr) ulPhys != nExpected) return 0;
	return 1;
}

// Wake (and unlink) the waiters on ulPhys, or -- ulPhys 0, the tick -- those whose word has
// changed. -> how many.
static int WakeWords (u64 ulPhys)
{
	int n = 0;
	s_WordLock.Acquire ();
	TWordWaiter **pp = &s_pWordWaiters;
	while (*pp != 0)
	{
		TWordWaiter *pW = *pp;
		if (  ulPhys != 0
		    ? pW->ulPhys == ulPhys
		    : *(volatile unsigned *) (uintptr) pW->ulPhys != pW->nExpected)
		{
			*pp = pW->pNext;
			pW->bWoken = TRUE;
			pW->pEv->Set ();			// (its task made ready; it runs later)
			n++;
			continue;
		}
		pp = &pW->pNext;
	}
	s_WordLock.Release ();
	return n;
}

int kapi_wake_word (volatile unsigned *pWord)
{
	u64 ulPhys;
	if (   pWord == 0 || ((uintptr) pWord & 3) != 0
	    || !UserRange ((const void *) pWord, sizeof *pWord)
	    || !WordPhys (pWord, &ulPhys)) return -1;
	return WakeWords (ulPhys);
}

void WordWaitTick (void)
{
	if (s_pWordWaiters == 0) return;		// (nobody sleeps on a word: nothing to read)
	WakeWords (0);
}

// (v75, kern/vm.h) A frame leaves its space: the waiters on a word in it woken (they return,
// look again: a spurious wake is allowed), so the tick never reads a freed frame for them.
void WordWaitsZap (u64 ulPhys, u64 nLen)
{
	if (s_pWordWaiters == 0) return;
	s_WordLock.Acquire ();
	TWordWaiter **pp = &s_pWordWaiters;
	while (*pp != 0)
	{
		TWordWaiter *pW = *pp;
		if (pW->ulPhys >= ulPhys && pW->ulPhys - ulPhys < nLen)
		{
			*pp = pW->pNext;
			pW->bWoken = TRUE;
			pW->pEv->Set ();
			continue;
		}
		pp = &pW->pNext;
	}
	s_WordLock.Release ();
}

static void WordWaitsFree (CAddressSpace *pAS)
{
	s_WordLock.Acquire ();
	TWordWaiter **pp = &s_pWordWaiters;
	while (*pp != 0)
	{
		if ((*pp)->pAS == pAS) *pp = (*pp)->pNext;	// (a killed task's: never runs again)
		else pp = &(*pp)->pNext;
	}
	s_WordLock.Release ();
}

// ---- priority -----------------------------------------------------------------

int kapi_thread_priority (int nTid, int nPrio)
{
	CAddressSpace *pAS = CurrentAS ();
	if (pAS == 0 || nPrio > 1) return -1;
	CTask *pTask = 0;
	if (nTid == 0)
	{
		pTask = CurrentTask ();
	}
	else if (nTid == 1)
	{
		pTask = pAS->GetMainTask ();
	}
	else
	{
		CProcThreads *pT = ThreadsOf (pAS, FALSE);
		for (unsigned i = 0; pT != 0 && i < THREAD_RECS; i++)
			if (pT->Rec[i].nTid == (unsigned) nTid && !pT->Rec[i].bDone) pTask = pT->Rec[i].pTask;
	}
	if (   pTask == 0
	    || !CScheduler::Get ()->IsValidTask (pTask)
	    || pTask->GetUserData (TASK_USER_DATA_USER) != pAS)
	{
		return -2;
	}
	int nOld = CScheduler::Get ()->SetPriority (pTask, nPrio < 0 ? -1 : nPrio);
	return nOld < 0 ? -2 : nOld;
}
