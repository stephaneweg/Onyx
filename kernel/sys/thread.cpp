//
// thread.cpp -- an app's threads, mutexes / events / barriers, and calls posted to its
// event pump (ABI v67). The model is in kern/thread.h.
//
#include <kern/thread.h>
#include <kern/addrspace.h>
#include <kern/gui/window.h>
#include <kern/kapi_abi.h>		// KAPI_WAIT_FOREVER
#include <circle/sched/scheduler.h>
#include <circle/sched/task.h>
#include <circle/timer.h>
#include <circle/string.h>
#include <circle/util.h>
#include <circle/new.h>

extern "C" void kapi_exit (int nStatus);
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
	CUserThreadTask (unsigned nStackSize, CAddressSpace *pAS, u64 ulFunc, u64 ulArg)
	:	CTask (nStackSize),
		m_ulFunc (ulFunc),
		m_ulArg (ulArg)
	{
		// (no Yield between the CTask constructor, which made it ready, and here: it
		// is first scheduled in its process's address space)
		SetUserData (pAS, TASK_USER_DATA_USER);
		pAS->AddTask (this);
	}

	void Run (void) override
	{
		int nCode = ((int (*) (void *)) m_ulFunc) ((void *) m_ulArg);	// the app's code
		kapi_thread_exit (nCode);
	}

private:
	u64 m_ulFunc, m_ulArg;
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

	CUserThreadTask *pTask = new CUserThreadTask (nStackSize, pAS, (u64) pFunc, (u64) pArg);
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


int kapi_pump_wait (unsigned nTimeoutMs)
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
	kapi_pump_events ();
	return (int) nPending;
}
