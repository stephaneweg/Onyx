//
// thread.cpp -- an app's threads, mutexes / events / barriers, and calls posted to its
// event pump (ABI v67). The model is in kern/thread.h.
//
#include <kern/thread.h>
#include <kern/addrspace.h>
#include <kern/gui/window.h>
#include <kern/kapi_abi.h>		// KAPI_WAIT_FOREVER
#include <kern/layout.h>		// KERNEL_IDENTITY_END
#include <kern/el0.h>			// protected mode: a thread at EL0
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
extern "C" void kapi_pump_events (void);

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
	// ulUserStack: 0, a legacy thread (fn runs at EL1 on this task's stack); else a protected
	// process's thread (kern/el0.h): fn runs at EL0 on that user stack (its top), this task's
	// stack is its kernel stack, and fn returns into the blob's El0ThreadReturn (thread_exit).
	CUserThreadTask (unsigned nStackSize, CAddressSpace *pAS, u64 ulFunc, u64 ulArg,
			 u64 ulUserStack = 0)
	:	CTask (nStackSize),
		m_ulFunc (ulFunc),
		m_ulArg (ulArg),
		m_ulUserStack (ulUserStack)
	{
		// (no Yield between the CTask constructor, which made it ready, and here: it
		// is first scheduled in its process's address space)
		SetUserData (pAS, TASK_USER_DATA_USER);
		pAS->AddTask (this);
	}

	void Run (void) override
	{
		if (m_ulUserStack != 0)
		{
			El0Enter (m_ulFunc, m_ulUserStack, m_ulArg, El0ThreadReturnVA ());	// (no return)
		}
		int nCode = ((int (*) (void *)) m_ulFunc) ((void *) m_ulArg);	// the app's code
		kapi_thread_exit (nCode);
	}

private:
	u64 m_ulFunc, m_ulArg;
	u64 m_ulUserStack;
};

static TThreadRec *RecOf (CProcThreads *pT, CTask *pTask)
{
	for (unsigned i = 0; i < THREAD_RECS; i++)
		if (pT->Rec[i].nTid != 0 && pT->Rec[i].pTask == pTask) return &pT->Rec[i];
	return 0;
}

int kapi_thread_create (int (*pFunc) (void *), void *pArg, unsigned nStackSize, const char *pName)
{
	CAddressSpace *pAS = CurrentAS ();
	CProcThreads *pT = ThreadsOf (pAS, TRUE);
	if (pT == 0 || pFunc == 0) return -1;

	unsigned nRunning = 0;
	TThreadRec *pRec = 0, *pOldest = 0;
	for (unsigned i = 0; i < THREAD_RECS; i++)
	{
		TThreadRec *r = &pT->Rec[i];
		if (r->nTid == 0) { if (pRec == 0) pRec = r; continue; }
		if (!r->bDone) { nRunning++; continue; }
		if (pOldest == 0 || (int) (r->nSeq - pOldest->nSeq) < 0) pOldest = r;
	}
	if (nRunning >= THREADS_MAX) return -2;		// the per-process limit
	if (pRec == 0) pRec = pOldest;			// (an ended one never joined: forgotten)
	if (pRec == 0) return -2;

	if (nStackSize == 0) nStackSize = THREAD_STACK_DEFAULT;
	if (nStackSize < THREAD_STACK_MIN) nStackSize = THREAD_STACK_MIN;
	if (nStackSize > THREAD_STACK_MAX) nStackSize = THREAD_STACK_MAX;
	nStackSize = (nStackSize + 15) & ~15u;

	unsigned nTid = pT->nNextTid++;
	pRec->nTid = nTid;
	pRec->bDone = FALSE;
	pRec->nCode = 0;
	pRec->nSeq = 0;

	CUserThreadTask *pTask;
	if (!pAS->IsProtected ())
	{
		pTask = new CUserThreadTask (nStackSize, pAS, (u64) pFunc, (u64) pArg);
	}
	else
	{
		// A protected process (kern/el0.h): the thread's user stack in the slot of its record
		// (a slot is reused with its record: its pages stay mapped), a guard below it; the task's
		// own stack is only its kernel stack.
		if (!IS_USER_VA (pFunc))
		{
			pRec->nTid = 0;
			return -1;
		}
		u64 ulTop = USER_THREAD_STACKS + (u64) (pRec - pT->Rec + 1) * USER_THREAD_SLOT;
		if (!pAS->MapStack (ulTop, nStackSize))
		{
			pRec->nTid = 0;
			return -1;				// (out of memory)
		}
		pTask = new CUserThreadTask (EL0_KSTACK_SIZE, pAS, (u64) pFunc, (u64) pArg, ulTop);
	}
	pRec->pTask = pTask;

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
		char Given[32];
		unsigned i = 0;
		for (; pName[i] != '\0' && i < sizeof Given - 1; i++) Given[i] = pName[i];
		Given[i] = '\0';
		Name.Format ("%s:%s", App, Given);
	}
	else
	{
		Name.Format ("%s:%u", App, nTid);
	}
	pTask->SetName (Name);

	return (int) nTid;
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
	if (pT != 0)
	{
		TThreadRec *r = RecOf (pT, pMe);
		if (r != 0)
		{
			r->bDone = TRUE;
			r->nCode = nCode;
			r->pTask = 0;
			r->nSeq = ++pT->nSeq;
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
			if (pCode != 0) *pCode = r->nCode;
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

unsigned ThreadsRunPosts (CAddressSpace *pAS)
{
	CProcThreads *pT = ThreadsOf (pAS, FALSE);
	if (pT == 0) return 0;
	unsigned n = 0;
	// (at most one ring's worth: a call that posts again runs at the next pump)
	while (pT->nPostTail != pT->nPostHead && n < POSTS_MAX)
	{
		TPost P = pT->Posts[pT->nPostTail];
		pT->nPostTail = (pT->nPostTail + 1) % POSTS_MAX;
		((void (*) (void *, long)) P.ulFunc) ((void *) P.ulCtx, P.lValue);	// the app's code
		n++;
	}
	return n;
}


// (v73) pump_wait's sleep, without the pump: a protected process's user-side pump_wait /
// wait_for_exit (kern/el0.h) sleep here, then pump themselves.
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

int kapi_pump_wait (unsigned nTimeoutMs)
{
	int nPending = kapi_pump_sleep (nTimeoutMs);
	if (nPending < 0) return -1;
	kapi_pump_events ();
	return nPending;
}

// (v73) The next posted call, not run: a protected process's user-side pump runs it at EL0.
int kapi_pop_post (struct kapi_posted *pPost)
{
	CProcThreads *pT = MyThreads (FALSE);
	if (pT == 0 || pPost == 0 || pT->nPostTail == pT->nPostHead) return 0;
	TPost P = pT->Posts[pT->nPostTail];
	pT->nPostTail = (pT->nPostTail + 1) % POSTS_MAX;
	pPost->fn = P.ulFunc;
	pPost->ctx = P.ulCtx;
	pPost->value = P.lValue;
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
// its heap, a surface, its stack), by the MMU itself (AT S1E1R on the current TTBR0). FALSE:
// unmapped, or out of the kernel's identity map (the tick could not read it there).
static boolean WordPhys (const volatile unsigned *pWord, u64 *pPhys)
{
	u64 nFlags, nPAR;
	asm volatile ("mrs %0, daif; msr daifset, #3" : "=r" (nFlags) :: "memory");	// (PAR_EL1 kept)
	asm volatile ("at s1e1r, %1\n\tisb\n\tmrs %0, par_el1" : "=r" (nPAR) : "r" (pWord) : "memory");
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
	if (pWord == 0 || ((uintptr) pWord & 3) != 0 || !WordPhys (pWord, &ulPhys)) return -1;
	return WakeWords (ulPhys);
}

void WordWaitTick (void)
{
	if (s_pWordWaiters == 0) return;		// (nobody sleeps on a word: nothing to read)
	WakeWords (0);
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
