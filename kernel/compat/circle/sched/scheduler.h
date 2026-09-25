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

/// \note Round-robin policy, no priorities (yet). Preemption-ready: see
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
	// current task (a task ends itself by returning / kapi_exit).
	void TerminateTask (CTask *pTask);

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

	/// \brief Called (IRQ masked) when the current task -- an app in its own code --
	///	   is preempted: flag it as a CPU hog and open a burst (SCHED_BURST_US) during
	///	   which GetNextTask skips the preempted tasks, until the burst ends or no
	///	   other task is ready. Circle's net / WLAN code waits in Yield() loops
	///	   (qlock, sleep, CNetTask, socket send/recv -- also inside an app's kapi call,
	///	   e.g. vncd); without the burst every one of those yields would cost a whole
	///	   hog slice, and a CPU-bound app starves the network. The flag is cleared
	///	   when the task runs again.
	void OnPreempt (void);

	static CScheduler *Get (void);

	static boolean IsActive (void)
	{
		return s_pThis != 0 ? TRUE : FALSE;
	}

private:
	void AddTask (CTask *pTask);
	friend class CTask;

	boolean BlockTask (CTask **ppWaitListHead, unsigned nMicroSeconds);
	void WakeTasks (CTask **ppWaitListHead); // can be called from interrupt context
	friend class CSynchronizationEvent;

	void RemoveTask (CTask *pTask);
	unsigned GetNextTask (void); // returns index into m_pTask or MAX_TASKS if none
	unsigned ScanTasks (unsigned nTicks, boolean bSkipHogs);	// one round-robin pass

private:
	CTask *m_pTask[MAX_TASKS];
	unsigned m_nTasks;

	CTask *m_pCurrent;
	unsigned m_nCurrent;	// index into m_pTask

	CTask *m_pIdleTask;	// run only when nothing else is ready (deprioritized)

	TSchedulerTaskHandler *m_pTaskSwitchHandler;
	TSchedulerTaskHandler *m_pTaskTerminationHandler;

	int m_iSuspendNewTasks;

	// Preemption state (additions)
	volatile boolean m_bResched;	// set by OnTimerTick when the slice expires
	unsigned m_nSliceTicks;		// scheduler ticks left in the current slice
	boolean m_bPreempted[MAX_TASKS]; // per slot: parked by a preemption (a CPU hog)
	volatile boolean m_bBurst;	// burst in progress: hogs are skipped (OnPreempt)
	unsigned m_nBurstEnd;		// its end, in clock ticks (us)

	CSpinLock m_SpinLock;

	static CScheduler *s_pThis;
};

// Length of a task's time slice, in 100 Hz scheduler ticks (10 ms each).
#ifndef SCHED_SLICE_TICKS
#define SCHED_SLICE_TICKS	2		// 20 ms quantum
#endif

// Length of the burst that follows an app preemption, in microseconds: the tasks that
// yield voluntarily get up to this much CPU before the hog runs again (= one slice, so
// a hog keeps at most ~half of the CPU while others need it, and all of it otherwise).
#ifndef SCHED_BURST_US
#define SCHED_BURST_US		20000		// 20 ms
#endif

#endif
