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
	unsigned CurrentSlot (void);	// m_pCurrent's index in m_pTask, or MAX_TASKS

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
	u8 m_nPreemptStreak[MAX_TASKS];	// per slot: preemptions since its last voluntary
					// yield (>= SCHED_HOG_STREAK: a CPU hog)
	boolean m_bPreempting;		// the current Yield comes from OnPreempt
	unsigned m_nSliceCfg;		// slice length, ticks (Configure)
	boolean m_bHogSched;		// hog detection + bursts enabled (Configure)
	volatile boolean m_bBurst;	// burst in progress: hogs are skipped (OnPreempt)
	unsigned m_nBurstEnd;		// its end, in clock ticks (us)

	// Stall watchdog
	unsigned m_nLastYield;		// clock ticks (us) of the last Yield() entry
	unsigned m_nLastSample;		// ... of the last StallSample() taken
	TStallReport m_Stall;		// the stall in progress (m_Stall.nSamples samples)
#define STALL_RING	4
	TStallReport m_StallRing[STALL_RING];
	unsigned m_nStallIn, m_nStallOut, m_nStallLost;

	CSpinLock m_SpinLock;

	static CScheduler *s_pThis;
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
