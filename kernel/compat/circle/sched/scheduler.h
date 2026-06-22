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
	boolean IsValidTask (CTask *pTask);

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

private:
	CTask *m_pTask[MAX_TASKS];
	unsigned m_nTasks;

	CTask *m_pCurrent;
	unsigned m_nCurrent;	// index into m_pTask

	TSchedulerTaskHandler *m_pTaskSwitchHandler;
	TSchedulerTaskHandler *m_pTaskTerminationHandler;

	int m_iSuspendNewTasks;

	// Preemption state (additions)
	volatile boolean m_bResched;	// set by OnTimerTick when the slice expires
	unsigned m_nSliceTicks;		// scheduler ticks left in the current slice

	CSpinLock m_SpinLock;

	static CScheduler *s_pThis;
};

// Length of a task's time slice, in 100 Hz scheduler ticks (10 ms each).
#ifndef SCHED_SLICE_TICKS
#define SCHED_SLICE_TICKS	5		// 50 ms quantum
#endif

#endif
