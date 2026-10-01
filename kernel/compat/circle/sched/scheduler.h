//
// scheduler.h  -- SHADOW of <circle/sched/scheduler.h>
//
// This file replaces Circle's scheduler.h on our include path (kernel/compat is
// searched before circle/include). It keeps Circle's CScheduler public API and
// the private "friend" protocol (AddTask/RemoveTask/BlockTask/WakeTasks +
// wake-ticks-as-timeout-flag convention) BYTE-FOR-BYTE compatible, so the
// reused Circle classes -- CTask, CSynchronizationEvent, CMutex, CSemaphore,
// CPipe -- and all drivers keep working unchanged.
//
// What we add for preemption (piège 2): OnTimerTick()/m_bResched, and a wfi-based
// idle instead of Circle's busy-spin. The timer-IRQ-driven preemptive switch is
// wired in milestone #4 (once we own VBAR_EL1); the scheduler itself is ready for
// it now.
//
// Derived from Circle (GPLv3), Copyright (C) 2015-2026 R. Stange. Modifications
// for the multi-process kernel. Same include guard so it truly shadows.
//
#ifndef _circle_sched_scheduler_h
#define _circle_sched_scheduler_h

#include <circle/sched/task.h>
#include <circle/spinlock.h>
#include <circle/device.h>
#include <circle/sysconfig.h>
#include <circle/macros.h>
#include <circle/types.h>

enum TTaskFlags		///< for EnumerateTasks()
{
	TaskFlagNone		= 0,
	TaskFlagRunning		= BIT (0),
	TaskFlagSuspended	= BIT (1)
};

typedef void TSchedulerTaskHandler (CTask *pTask);

#define SCHED_CORES	4		// (a power of two: ThisCore masks the MPIDR)

// Stall watchdog (diagnostics): a task that kept the CPU for more than SCHED_STALL_US
// without passing through Yield() -- kernel code, which is not preempted. While it lasts,
// the IRQ exit path samples where it is (the interrupted PC and LR); the next Yield()
// closes the report, and the reaper task writes it to the kernel log (kmsg).
#define STALL_SAMPLES	8
struct TStallReport
{
	char	 Name[24];			// the task that did not yield
	unsigned nMs;				// how long it kept the CPU
	unsigned nSamples;			// (0: IRQs were masked the whole time)
	u64	 PC[STALL_SAMPLES];		// where it was, every ~SCHED_STALL_SAMPLE_US
	u64	 LR[STALL_SAMPLES];
};

// A task in the scheduler's list, with the scheduler's own state about it (CTask's layout is
// Circle's, shared with its prebuilt libraries: it cannot grow). The list is circular and has
// no size limit (Circle's MAX_TASKS table is gone).
struct TSchedNode
{
	CTask	   *pTask;
	TSchedNode *pNext;		// the next task in round-robin order (circular)
	TSchedNode *pReapNext;		// (ReapTerminatedTasks: its batch)
	u8	    nNoKill;		// EnterNoKill depth
	u8	    nPreemptStreak;	// preemptions since its last voluntary yield
					// (>= SCHED_HOG_STREAK: a CPU hog)
	boolean	    bKillPending;	// TerminateTask waits for LeaveNoKill
	boolean	    bKilled;		// terminated from outside (TerminateTask / TerminateGroup)
	u8	    nPrio;		// 0 normal, SCHED_PRIO_HIGH: picked first when ready (v68)
	boolean	    bPrioSpent;		// preempted at its slice's end: normal until it yields
};

// Task priorities (ABI v68 kapi_thread_priority): a "real time" task (an audio pump) is picked
// before the round-robin ones whenever it is ready, and a timer tick preempts an app running its
// own code for it. It keeps that only while it yields by itself: preempted at the end of a slice
// (a busy loop), it is an ordinary task until its next voluntary yield -- it cannot starve the
// system. A yield while still ready (a spin on a lock) goes to the others too.
#define SCHED_PRIO_NORMAL	0
#define SCHED_PRIO_HIGH		1

/// \note Round-robin policy, plus "real time" tasks (SetPriority). Preemption-ready: see
///       OnTimerTick(). The block/wake/sleep protocol is identical to Circle's.

class CScheduler /// Preemptive-ready scheduler controlling which task runs (replaces Circle's)
{
public:
	CScheduler (void);
	~CScheduler (void);

	/// \brief Switch to the next ready task (voluntary). Also the switch entry
	///	   used by the preemptive IRQ path in milestone #4.
	void Yield (void);

	void Sleep (unsigned nSeconds);
	void MsSleep (unsigned nMilliSeconds);
	void usSleep (unsigned nMicroSeconds);

	CTask *GetCurrentTask (void);
	CTask *GetTask (const char *pTaskName);
	// Like GetTask but skips tasks that have already terminated (awaiting reap), so
	// a name that briefly survives teardown isn't mistaken for a live task.
	CTask *GetRunningTask (const char *pTaskName);
	boolean IsValidTask (CTask *pTask);

	// Externally terminate another task (e.g. the task manager killing an app): mark
	// it Terminated so GetNextTask skips it and the reaper frees it. No-op on the
	// current task (a task ends itself by returning / kapi_exit). An app's task takes
	// its whole process with it: every task with the same TASK_USER_DATA_USER pointer
	// (the address space: the app's threads) -- see TerminateGroup.
	void TerminateTask (CTask *pTask);

	// Terminate every task whose TASK_USER_DATA_USER is pKey (!= 0), except the current
	// one: a process's threads, when it is killed or when one of them ends the process.
	// A killed task is freed only once no task of its group is left alive (it may have
	// been killed while waiting on an event of the process: the wait lists die with it).
	void TerminateGroup (void *pKey);

	// Tasks of the group pKey that are not terminated (the process's live threads).
	unsigned CountGroup (void *pKey);

	// No-kill section of the CURRENT task (nestable): while it holds a kernel resource
	// across a Yield (the FatFs volume lock, while the SD driver waits), TerminateTask
	// only marks it; LeaveNoKill ends the task then, once the resource is free.
	void EnterNoKill (void);
	void LeaveNoKill (void);

	void RegisterTaskSwitchHandler (TSchedulerTaskHandler *pHandler);
	void RegisterTaskTerminationHandler (TSchedulerTaskHandler *pHandler);

	void SuspendNewTasks (void);
	void ResumeNewTasks (void);

	boolean EnumerateTasks (
		boolean (*pCallback) (CTask *pTask, const char *pName,
				      TTaskState State, TTaskFlags Flags,
				      void *pParam),
		void *pParam
	);

	void ListTasks (CDevice *pTarget);

	/// \brief Free tasks that have ended (TaskStateTerminated). Call from a normal
	///	   task context (e.g. the kernel main/janitor loop) -- NOT from inside
	///	   the scheduler core. A terminated task stops being scheduled
	///	   immediately (GetNextTask skips it); this reaps it later in a safe
	///	   context: runs the termination handler (frees the address space),
	///	   removes it from the task list and deletes it.
	/// \return number of tasks reaped this call.
	unsigned ReapTerminatedTasks (void);

	// ---- Additions for preemption (milestone #4 wires the call site) --------

	/// \brief Account one scheduler tick (call from the timer IRQ, 100 Hz).
	///	   When the current task's time slice expires it sets the resched
	///	   flag. It NEVER switches by itself -- the IRQ exit path checks
	///	   IsReschedPending() and calls Yield() at a safe point.
	/// \note Callable from interrupt context.
	void OnTimerTick (void);

	/// \return Is a reschedule pending (time slice expired)?
	boolean IsReschedPending (void) const	{ return m_bResched; }

	/// \brief Clear the resched flag (the IRQ exit path calls this around Yield).
	void ClearResched (void)		{ m_bResched = FALSE; }

	/// \brief Yield, handing the CPU to pTask next if it is ready (instead of the
	///	   next task in round-robin order). Used to run a freshly created service
	///	   task (the compositor) right away.
	void YieldTo (CTask *pTask);

	/// \brief Called when the current task -- an app in its own code -- is preempted
	///	   (just before its Yield). A task preempted SCHED_HOG_STREAK times in a row
	///	   without a voluntary yield is a CPU hog: it opens a burst (SCHED_BURST_US)
	///	   during which GetNextTask skips the hogs, until the burst ends or no other
	///	   task is ready. (An app that computes > 1 slice now and then but yields in
	///	   between -- vncd encoding a frame, then sending it -- is not a hog.) Circle's net / WLAN code waits in Yield() loops
	///	   (qlock, sleep, CNetTask, socket send/recv -- also inside an app's kapi call,
	///	   e.g. vncd); without the burst every one of those yields would cost a whole
	///	   hog slice, and a CPU-bound app starves the network. The streak is reset
	///	   by the task's next voluntary Yield.
	void OnPreempt (void);

	/// \brief Priority of pTask (SCHED_PRIO_*; -1 only asks) -> the previous one, -1 no such task.
	int SetPriority (CTask *pTask, int nPrio);
	/// \brief Boot-time tuning (cmdline.txt): the app time slice in 10 ms ticks
	///	   (slice=, default SCHED_SLICE_TICKS) and the hog logic on/off (hogsched=0
	///	   = plain round-robin, as before OnPreempt existed) -- for A/B testing.
	void Configure (unsigned nSliceTicks, boolean bHogSched);

	/// \brief Stall watchdog: called on every IRQ exit with the interrupted PC/LR.
	/// \note Callable from interrupt context only.
	void StallSample (u64 ulPC, u64 ulLR);

	/// \brief Pop the oldest finished stall report (normal task context).
	/// \return FALSE if there is none. *pLost: reports dropped (ring full) so far.
	boolean TakeStallReport (TStallReport *pReport, unsigned *pLost);

	// One scheduler per core that runs tasks: core 0 (the kernel, every process) and, with
	// netcore=1, core 3 (the network stack: Circle's net / WLAN tasks, the socket workers).
	// Get () is the scheduler of the core it is called on, so Circle's tasks, events and
	// sleeps work unchanged on either core. The other cores have none (IsActive () = FALSE).
	static CScheduler *Get (void);

	// Clock ticks (us) of the last Yield() entry on this core: how long the current task has
	// run without yielding (the SD driver's wait hook: kernel/sys/fslock.cpp).
	unsigned GetLastYield (void) const
	{
		return m_nLastYield;
	}

	static boolean IsActive (void)
	{
		return s_pThis[ThisCore ()] != 0 ? TRUE : FALSE;
	}

	static unsigned ThisCore (void)
	{
		u64 nMPIDR;
		asm volatile ("mrs %0, mpidr_el1" : "=r" (nMPIDR));
		return (unsigned) (nMPIDR & (SCHED_CORES - 1));
	}

private:
	void AddTask (CTask *pTask);
	friend class CTask;

	boolean BlockTask (CTask **ppWaitListHead, unsigned nMicroSeconds);
	void WakeTasks (CTask **ppWaitListHead); // can be called from interrupt context
	friend class CSynchronizationEvent;

	void RemoveTask (CTask *pTask);
	TSchedNode *GetNextTask (void);	// the next task to run, 0 if none
	TSchedNode *ScanTasks (unsigned nTicks, boolean bSkipHogs);	// one round-robin pass
	TSchedNode *ScanPrio (unsigned nTicks, boolean bTake);	// a ready "real time" task
	TSchedNode *FindNode (CTask *pTask);
	TSchedNode *FindPrev (TSchedNode *pNode);	// its predecessor in the circle
	boolean GroupAlive (void *pKey);	// a task of the group pKey is not terminated

private:
	TSchedNode *m_pHead;	// the list (circular): its first ...
	TSchedNode *m_pTail;	// ... and its last task (m_pTail->pNext == m_pHead)
	unsigned m_nTasks;

	CTask *m_pCurrent;
	TSchedNode *m_pCurNode;	// m_pCurrent's node
	TSchedNode *m_pScan;	// the round-robin scan starts after this one

	CTask *m_pIdleTask;	// run only when nothing else is ready (deprioritized)

	TSchedulerTaskHandler *m_pTaskSwitchHandler;
	TSchedulerTaskHandler *m_pTaskTerminationHandler;

	int m_iSuspendNewTasks;

	// Preemption state (additions)
	volatile boolean m_bResched;	// set by OnTimerTick when the slice expires
	unsigned m_nSliceTicks;		// scheduler ticks left in the current slice
	boolean m_bPreempting;		// the current Yield comes from OnPreempt
	unsigned m_nSliceCfg;		// slice length, ticks (Configure)
	boolean m_bHogSched;		// hog detection + bursts enabled (Configure)
	volatile boolean m_bBurst;	// burst in progress: hogs are skipped (OnPreempt)
	unsigned m_nBurstEnd;		// its end, in clock ticks (us)
	unsigned m_nPrioTasks;		// tasks with nPrio > 0 (none: no extra scan at all)
	volatile boolean m_bPrioPreempt; // the pending resched is for a "real time" task

	// Stall watchdog
	unsigned m_nLastYield;		// clock ticks (us) of the last Yield() entry
	unsigned m_nLastSample;		// ... of the last StallSample() taken
	TStallReport m_Stall;		// the stall in progress (m_Stall.nSamples samples)
#define STALL_RING	4
	TStallReport m_StallRing[STALL_RING];
	unsigned m_nStallIn, m_nStallOut, m_nStallLost;

	unsigned m_nCore;		// the core this scheduler runs

	static CScheduler *s_pThis[SCHED_CORES];
};

// Length of a task's time slice, in 100 Hz scheduler ticks (10 ms each).
#ifndef SCHED_SLICE_TICKS
#define SCHED_SLICE_TICKS	2		// 20 ms quantum
#endif

// Length of the burst that follows a hog's preemption, in microseconds: the tasks that
// yield voluntarily get up to this much CPU before the hog runs again (it keeps all of
// the CPU when nobody else needs it).
#ifndef SCHED_BURST_US
#define SCHED_BURST_US		60000		// 60 ms: a hog keeps <= ~25 % while others need the CPU
#endif

// Stall watchdog: a task running this long without a Yield() is reported (kmsg), with a
// sample of where it was every SCHED_STALL_SAMPLE_US.
#ifndef SCHED_STALL_US
#define SCHED_STALL_US		100000		// 100 ms: six frames at 60 Hz
#endif
#ifndef SCHED_STALL_SAMPLE_US
#define SCHED_STALL_SAMPLE_US	50000
#endif

// Preemptions in a row (no voluntary yield in between) that make a task a CPU hog.
#ifndef SCHED_HOG_STREAK
#define SCHED_HOG_STREAK	2
#endif

#endif
