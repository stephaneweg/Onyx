//
// thread.h -- an app's threads, their synchronisation objects, and calls posted to the
// app's event pump (ABI v67).
//
// A thread is one more CTask in the app's address space: its TASK_USER_DATA_USER is the
// same CAddressSpace, so the task switch activates the app's page table and every kapi
// sees the same process (window, heap, files, sockets). It runs at EL1 on its own
// kernel-allocated stack (identity-mapped, like the main task's), and the timer preempts
// it in its own code like any app. The process ends with its main task (the others are
// terminated with it), or when a thread calls kapi_exit; killing the app kills them all
// (CScheduler::TerminateGroup).
//
// The objects -- mutex, event, barrier -- are the process's, named by a small handle. A
// kapi runs on core 0 and is not preempted: an object's check-then-wait is atomic, so
// the objects need no lock of their own; a waiter blocks on a CSynchronizationEvent that
// is only ever Pulse()d (a condition variable), and looks its object up again after each
// wake (it may have been closed meanwhile). An object closed while waited on is freed by
// its last waiter; the ones left at the process's end are freed with it (their waiters,
// killed tasks, unlinked first).
//
// Word waits (ABI v68, a futex): kapi_wait_word sleeps while a 32-bit word holds a value,
// kapi_wake_word wakes the sleepers on a word. A waiter is keyed by the word's PHYSICAL
// address (the same word of a shared surface is at another address in each process), and
// lives on its task's stack, linked in one kernel list. Code on an app core changes words
// without any kapi call: WordWaitTick, in the 100 Hz timer interrupt, reads every sleeping
// word (through the kernel's identity map) and wakes those whose value moved. A process
// that dies unlinks its sleepers first (ThreadsFree).
//
// kapi_post queues a call (fn, ctx, value) that the process's event pump runs --
// kapi_pump_events on the thread that pumps (the main one): a worker thread hands its
// result to the UI thread that way, and kapi_pump_wait sleeps until a post or a window
// event arrives.
//
#ifndef _kern_thread_h
#define _kern_thread_h

#include <circle/sched/synchronizationevent.h>
#include <circle/types.h>

class CAddressSpace;
class CTask;
struct TSyncObj;

#define THREADS_MAX		32		// threads a process may run besides its main one
#define THREAD_RECS		(THREADS_MAX * 2)	// ... + the ended ones not joined yet
#define THREAD_STACK_DEFAULT	0x40000		// 256 KB
#define THREAD_STACK_MIN	0x4000		// 16 KB
#define THREAD_STACK_MAX	0x1000000	// 16 MB
#define SYNC_OBJS_MAX		256		// mutexes + events + barriers per process
#define POSTS_MAX		256		// calls posted and not run yet

struct TThreadRec
{
	unsigned nTid;				// 0: a free record
	CTask	*pTask;				// (while it runs)
	boolean	 bDone;				// ended: nCode is its result
	int	 nCode;
	unsigned nSeq;				// (the oldest ended one is reused first)
};

struct TPost
{
	u64	ulFunc;				// void fn (void *ctx, long value), in the app
	u64	ulCtx;
	long	lValue;
};

class CProcThreads
{
public:
	CProcThreads (void);

	TThreadRec	Rec[THREAD_RECS];
	unsigned	nNextTid;		// 1 is the main task
	unsigned	nSeq;
	CSynchronizationEvent DoneEv;		// pulsed when a thread ends (joiners)
	unsigned	nJoinWaiters;

	TSyncObj       *pObj[SYNC_OBJS_MAX];	// handle h = index + 1

	TPost		Posts[POSTS_MAX];	// ring
	unsigned	nPostHead, nPostTail;
	unsigned	nPostsLost;
	CSynchronizationEvent WakeEv;		// pulsed on a post / a window event (pump_wait)
	unsigned	nWakeWaiters;
};

// The process's CProcThreads, made on first use (0: out of memory, or not an app).
CProcThreads *ThreadsOf (CAddressSpace *pAS, boolean bCreate);

// Free a dying process's threads state (~CAddressSpace): every object's waiters unlinked,
// the objects deleted, the window's wake-up pointer cleared.
void ThreadsFree (CAddressSpace *pAS);

// End the other tasks of the current process (it ends: kapi_exit, main returned).
void ThreadsEndProcess (void);

// Run the calls posted to the current process (kapi_pump_events). Returns how many.
unsigned ThreadsRunPosts (CAddressSpace *pAS);

// The timer tick (IRQ, core 0): wake the word waiters whose word has changed.
void WordWaitTick (void);

extern "C" {
int  kapi_thread_create (int (*pFunc) (void *), void *pArg, unsigned nStackSize, const char *pName);
void kapi_thread_exit (int nCode);
int  kapi_thread_join (int nTid, unsigned nTimeoutMs, int *pCode);
int  kapi_thread_self (void);
int  kapi_mutex_create (void);
int  kapi_mutex_lock (int h, unsigned nTimeoutMs);
int  kapi_mutex_unlock (int h);
int  kapi_event_create (int bManualReset, int bInitial);
int  kapi_event_set (int h);
int  kapi_event_reset (int h);
int  kapi_event_wait (int h, unsigned nTimeoutMs);
int  kapi_barrier_create (unsigned nCount);
int  kapi_barrier_wait (int h);
int  kapi_sync_close (int h);
int  kapi_post (void (*pFunc) (void *, long), void *pCtx, long lValue);
int  kapi_pump_wait (unsigned nTimeoutMs);
int  kapi_wait_word (volatile unsigned *pWord, unsigned nExpected, unsigned nTimeoutMs);
int  kapi_wake_word (volatile unsigned *pWord);
int  kapi_thread_priority (int nTid, int nPrio);
}

#endif
