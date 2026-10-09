//
// scheduler.cpp
//
// Our replacement for Circle's lib/sched/scheduler.cpp. Build note: do NOT compile
// circle/lib/sched/scheduler.cpp -- compile this instead. The other sched files
// (task.cpp, taskswitch.S, synchronizationevent.cpp, mutex.cpp, semaphore.cpp,
// pipe.cpp) are reused from Circle unchanged; they include our shadow scheduler.h.
//
// The block/wake/sleep/timeout protocol is kept identical to Circle's (the reused
// classes depend on it). Differences from upstream:
//   * Yield() waits with wfi when no task is ready, instead of busy-spinning and
//     asserting -- needed once every task may be blocked/sleeping.
//   * OnTimerTick()/m_bResched provide a time slice for preemption; the actual
//     preemptive switch is triggered from the IRQ exit path in milestone #4.
//
// Derived from Circle (GPLv3), Copyright (C) 2015-2026 R. Stange.
//
#include <circle/sched/scheduler.h>
#include <circle/timer.h>
#include <circle/logger.h>
#include <circle/string.h>
#include <circle/util.h>
#include <circle/startup.h>
#include <assert.h>

static const char FromScheduler[] = "sched";

// ---- local IRQ masking (full-DAIF save/restore around a context switch) ------
//
// A context switch must be atomic against the timer IRQ (which calls Yield() via
// KernelIRQExit) and against device IRQs that call WakeTasks(). We disable IRQ
// for the whole switch and restore the *full* DAIF that the suspending task had,
// so voluntary (IRQ-enabled) and preemptive (IRQ-disabled) suspend points each
// resume with their own correct interrupt state.
static inline u64 IrqSave (void)
{
	u64 nFlags;
	asm volatile ("mrs %0, daif; msr daifset, #2" : "=r" (nFlags) :: "memory");
	return nFlags;
}

static inline void IrqRestore (u64 nFlags)
{
	asm volatile ("msr daif, %0" :: "r" (nFlags) : "memory");
}

static inline void IrqEnable (void)
{
	asm volatile ("msr daifclr, #2" ::: "memory");
}

static inline void IrqDisable (void)
{
	asm volatile ("msr daifset, #2" ::: "memory");
}

// Idle task: runs only when nothing else is ready. On core 0 it sleeps in wfi (IRQ
// enabled, so the timer tick wakes it and the IRQ-exit path can preempt it to a woken
// task). The other cores get no timer tick: there it only pauses a little (a sleeping task
// is woken by the clock, read in GetNextTask).
class CIdleTask : public CTask
{
public:
	CIdleTask (boolean bWfi) : m_bWfi (bWfi)
	{
		SetName ("idle");
	}

	void Run (void) override
	{
		for (;;)
		{
			if (m_bWfi) asm volatile ("wfi");
			else for (unsigned i = 0; i < 64; i++) asm volatile ("yield");
			CScheduler::Get ()->Yield ();
		}
	}

private:
	boolean m_bWfi;
};

CScheduler *CScheduler::s_pThis[SCHED_CORES] = { 0, 0, 0, 0 };

// The wait lists of CSynchronizationEvent: one lock for every core's scheduler (an event
// may be set on another core than the one its waiters run on).
static CSpinLock s_WaitLock;

CScheduler::CScheduler (void)
:	m_pHead (0),
	m_pTail (0),
	m_nTasks (0),
	m_pCurrent (0),
	m_pCurNode (0),
	m_pScan (0),
	m_pIdleTask (0),
	m_pTaskSwitchHandler (0),
	m_pTaskTerminationHandler (0),
	m_iSuspendNewTasks (0),
	m_bResched (FALSE),
	m_nSliceTicks (SCHED_SLICE_TICKS),
	m_bBurst (FALSE),
	m_nBurstEnd (0),
	m_nPrioTasks (0),
	m_bPrioPreempt (FALSE),
	m_nLastYield (0),
	m_nBusyUs (0),
	m_nSleptUs (0),
	m_nLastSample (0),
	m_nStallIn (0),
	m_nStallOut (0),
	m_nStallLost (0)
{
	m_Stall.nSamples = 0;
	m_nCore = ThisCore ();
	assert (s_pThis[m_nCore] == 0);
	s_pThis[m_nCore] = this;

	m_bPreempting = FALSE;
	m_nSliceCfg = SCHED_SLICE_TICKS;
	m_bHogSched = TRUE;

	m_pCurrent = new CTask (0);		// represents the main task currently running
	assert (m_pCurrent != 0);
	m_pCurrent->SetName ("main");
	m_pCurNode = FindNode (m_pCurrent);
	assert (m_pCurNode != 0);

	m_pIdleTask = new CIdleTask (m_nCore == 0);	// always-ready fallback (deprioritized)
	assert (m_pIdleTask != 0);
}

CScheduler::~CScheduler (void)
{
	m_pTaskSwitchHandler = 0;
	m_pTaskTerminationHandler = 0;

	s_pThis[m_nCore] = 0;
}

void CScheduler::Yield (void)
{
	// Disable IRQ for the whole switch: this serializes the scheduler against the
	// timer-IRQ preemption path (KernelIRQExit -> Yield) and device IRQs that call
	// WakeTasks(), on this core. We restore the FULL prior DAIF after we are
	// resumed, so each task keeps its own interrupt state across switches.
	u64 nFlags = IrqSave ();

	// Stall watchdog: the task that is leaving ran since the previous Yield() entry.
	// Too long (kernel code is not preempted) -> file a report for the reaper to log.
	unsigned nNow = CTimer::Get ()->GetClockTicks ();
	if (m_pCurrent != m_pIdleTask && m_nLastYield != 0)	// (v80 cpu_stats: the leaving task's time)
	{
		m_nBusyUs = m_nBusyUs + (unsigned) (nNow - m_nLastYield);
	}
	if (   m_pCurrent != m_pIdleTask
	    && (unsigned) (nNow - m_nLastYield) > SCHED_STALL_US
	    && m_nLastYield != 0)
	{
		const char *pName = m_pCurrent != 0 ? m_pCurrent->GetName () : 0;
		strncpy (m_Stall.Name, pName != 0 ? pName : "?", sizeof m_Stall.Name - 1);
		m_Stall.Name[sizeof m_Stall.Name - 1] = '\0';
		m_Stall.nMs = (nNow - m_nLastYield) / 1000;
		if (m_nStallIn - m_nStallOut < STALL_RING)
		{
			m_StallRing[m_nStallIn++ % STALL_RING] = m_Stall;
		}
		else
		{
			m_nStallLost++;
		}
	}
	m_Stall.nSamples = 0;
	m_nLastYield = nNow;

	if (!m_bPreempting && m_pCurNode != 0)	// a voluntary yield: not (or no longer) a hog
	{
		m_pCurNode->nPreemptStreak = 0;
		m_pCurNode->bPrioSpent = FALSE;	// (a "real time" task: its priority back)
	}
	m_bPreempting = FALSE;

	TSchedNode *pNode;
	while ((pNode = GetNextTask ()) == 0)
	{
		// The idle task is always ready, so this should not happen. Defensive:
		// allow an IRQ in (to make progress), then rescan.
		IrqEnable ();
		asm volatile ("wfi");
		IrqDisable ();
	}

	m_pScan = pNode;
	m_pCurNode = pNode;
	CTask *pNext = pNode->pTask;
	assert (pNext != 0);

	// Whichever task runs now starts a fresh time slice -- a single tick for a CPU hog,
	// so the kernel's Yield() loops never wait more than ~10 ms behind it.
	m_nSliceTicks = (m_bHogSched && pNode->nPreemptStreak >= SCHED_HOG_STREAK) ? 1 : m_nSliceCfg;
	m_bResched = FALSE;
	m_bPrioPreempt = FALSE;

	if (m_pCurrent != pNext)
	{
		TTaskRegisters *pOldRegs = m_pCurrent->GetRegs ();
		m_pCurrent = pNext;
		TTaskRegisters *pNewRegs = m_pCurrent->GetRegs ();

		if (m_pTaskSwitchHandler != 0)
		{
			(*m_pTaskSwitchHandler) (m_pCurrent);
		}

		assert (pOldRegs != 0);
		assert (pNewRegs != 0);
		TaskSwitch (pOldRegs, pNewRegs);	// returns when WE are scheduled again
	}

	IrqRestore (nFlags);
}

u64 CScheduler::GetBusyUs (void) const
{
	u64 n = m_nBusyUs;
	unsigned nLast = m_nLastYield;
	if (m_pCurrent != m_pIdleTask && nLast != 0)		// (the task running now, so far)
	{
		unsigned nRun = CTimer::Get ()->GetClockTicks () - nLast;
		if (nRun < 0x80000000u) n += nRun;		// (read on another core: it may have yielded since)
	}
	u64 nSlept = m_nSleptUs;
	return n > nSlept ? n - nSlept : 0;
}

void CScheduler::StallSample (u64 ulPC, u64 ulLR)
{
	// IRQ context (IRQs masked): no lock needed against Yield(), which masks them too.
	if (m_pCurrent == m_pIdleTask || m_nLastYield == 0)
	{
		return;
	}
	unsigned nNow = CTimer::Get ()->GetClockTicks ();
	if (   (unsigned) (nNow - m_nLastYield) <= SCHED_STALL_US
	    || m_Stall.nSamples >= STALL_SAMPLES
	    || (m_Stall.nSamples > 0 && (unsigned) (nNow - m_nLastSample) < SCHED_STALL_SAMPLE_US))
	{
		return;
	}
	m_Stall.PC[m_Stall.nSamples] = ulPC;
	m_Stall.LR[m_Stall.nSamples] = ulLR;
	m_Stall.nSamples++;
	m_nLastSample = nNow;
}

boolean CScheduler::TakeStallReport (TStallReport *pReport, unsigned *pLost)
{
	u64 nFlags = IrqSave ();
	boolean bGot = m_nStallIn != m_nStallOut;
	if (bGot)
	{
		*pReport = m_StallRing[m_nStallOut++ % STALL_RING];
	}
	if (pLost != 0) *pLost = m_nStallLost;
	IrqRestore (nFlags);
	return bGot;
}

void CScheduler::YieldTo (CTask *pTask)
{
	// GetNextTask() scans round-robin starting AFTER m_pScan, so pointing m_pScan
	// just before pTask makes it the first candidate. m_pScan is only the scan start
	// (the switch itself uses m_pCurrent), so this is safe; IRQ is masked so the timer
	// path cannot interleave. If pTask is not ready, Yield() simply picks the next
	// ready task from there.
	u64 nFlags = IrqSave ();
	TSchedNode *pNode = FindNode (pTask);
	if (pNode != 0)
	{
		m_pScan = FindPrev (pNode);
	}
	Yield ();
	IrqRestore (nFlags);
}

void CScheduler::Configure (unsigned nSliceTicks, boolean bHogSched)
{
	u64 nFlags = IrqSave ();
	m_nSliceCfg = nSliceTicks < 1 ? 1 : (nSliceTicks > 100 ? 100 : nSliceTicks);
	m_bHogSched = bHogSched;
	IrqRestore (nFlags);
}

void CScheduler::OnPreempt (void)
{
	u64 nFlags = IrqSave ();
	TSchedNode *pNode = m_pCurNode;
	if (m_bPrioPreempt)
	{
		// Preempted for a "real time" task (OnTimerTick): not the app's slice used up, so
		// not counted towards a hog -- but not a voluntary yield either.
		m_bPrioPreempt = FALSE;
		m_bPreempting = TRUE;
		IrqRestore (nFlags);
		return;
	}
	if (pNode != 0)
	{
		pNode->bPrioSpent = TRUE;	// its whole slice: a "real time" one is normal for now
	}
	if (!m_bHogSched)
	{
		IrqRestore (nFlags);
		return;
	}
	m_bPreempting = TRUE;			// the Yield that follows is not voluntary
	if (pNode != 0)
	{
		if (pNode->nPreemptStreak < 255) pNode->nPreemptStreak++;
		if (pNode->nPreemptStreak >= SCHED_HOG_STREAK)
		{
			m_nBurstEnd = CTimer::Get ()->GetClockTicks () + SCHED_BURST_US;
			m_bBurst = TRUE;
		}
	}
	IrqRestore (nFlags);
}

TSchedNode *CScheduler::FindNode (CTask *pTask)
{
	TSchedNode *p = m_pHead;
	for (unsigned i = 0; i < m_nTasks; i++, p = p->pNext)
	{
		if (p->pTask == pTask) return p;
	}
	return 0;
}

TSchedNode *CScheduler::FindPrev (TSchedNode *pNode)
{
	TSchedNode *p = pNode;
	while (p->pNext != pNode) p = p->pNext;		// (the circle: always found)
	return p;
}

void CScheduler::OnTimerTick (void)
{
	// Called from the timer IRQ at 100 Hz (wired in milestone #4). Decrement the
	// current task's slice; when it runs out, request a reschedule. We only set a
	// flag here -- switching from inside the IRQ is done on the IRQ exit path,
	// where it is safe.
	if (m_nSliceTicks > 0)
	{
		m_nSliceTicks--;
	}

	if (m_nSliceTicks == 0)
	{
		m_bResched = TRUE;
	}

	// A "real time" task is ready (woken by an event, a word, its sleep's end) while an
	// ordinary one runs: preempt it now rather than at its slice's end (the IRQ exit path
	// does it only if the app is in its own code; kernel code yields soon anyway).
	if (   m_nPrioTasks > 0
	    && !m_bResched
	    && m_pCurNode != 0
	    && m_pCurNode->nPrio == SCHED_PRIO_NORMAL
	    && ScanPrio (CTimer::Get ()->GetClockTicks (), FALSE) != 0)
	{
		m_bPrioPreempt = TRUE;
		m_bResched = TRUE;
	}
}

int CScheduler::SetPriority (CTask *pTask, int nPrio)
{
	u64 nFlags = IrqSave ();
	TSchedNode *pNode = FindNode (pTask);
	if (pNode == 0)
	{
		IrqRestore (nFlags);
		return -1;
	}
	int nOld = pNode->nPrio;
	if (nPrio >= 0)
	{
		u8 nNew = nPrio > 0 ? SCHED_PRIO_HIGH : SCHED_PRIO_NORMAL;
		if (nOld == SCHED_PRIO_NORMAL && nNew != SCHED_PRIO_NORMAL) m_nPrioTasks++;
		if (nOld != SCHED_PRIO_NORMAL && nNew == SCHED_PRIO_NORMAL) m_nPrioTasks--;
		pNode->nPrio = nNew;
		pNode->bPrioSpent = FALSE;
	}
	IrqRestore (nFlags);
	return nOld;
}

void CScheduler::Sleep (unsigned nSeconds)
{
	// be sure the clock does not run over taken as signed int
	const unsigned nSleepMax = 1800;	// normally 2147 but to be sure
	while (nSeconds > nSleepMax)
	{
		usSleep (nSleepMax * 1000000);

		nSeconds -= nSleepMax;
	}

	usSleep (nSeconds * 1000000);
}

void CScheduler::MsSleep (unsigned nMilliSeconds)
{
	if (nMilliSeconds > 0)
	{
		usSleep (nMilliSeconds * 1000);
	}
}

void CScheduler::usSleep (unsigned nMicroSeconds)
{
	if (nMicroSeconds > 0)
	{
		unsigned nTicks = nMicroSeconds * (CLOCKHZ / 1000000);

		unsigned nStartTicks = CTimer::Get ()->GetClockTicks ();

		assert (m_pCurrent != 0);
		assert (m_pCurrent->GetState () == TaskStateReady);
		m_pCurrent->SetWakeTicks (nStartTicks + nTicks);
		m_pCurrent->SetState (TaskStateSleeping);

		Yield ();
	}
}

CTask *CScheduler::GetCurrentTask (void)
{
	return m_pCurrent;
}

CTask *CScheduler::GetTask (const char *pTaskName)
{
	assert (pTaskName != 0);

	TSchedNode *p = m_pHead;
	for (unsigned i = 0; i < m_nTasks; i++, p = p->pNext)
	{
		if (strcmp (p->pTask->GetName (), pTaskName) == 0)
		{
			return p->pTask;
		}
	}

	return 0;
}

CTask *CScheduler::GetRunningTask (const char *pTaskName)
{
	assert (pTaskName != 0);

	TSchedNode *p = m_pHead;
	for (unsigned i = 0; i < m_nTasks; i++, p = p->pNext)
	{
		if (   p->pTask->GetState () != TaskStateTerminated
		    && strcmp (p->pTask->GetName (), pTaskName) == 0)
		{
			return p->pTask;
		}
	}

	return 0;
}

void CScheduler::TerminateTask (CTask *pTask)
{
	if (pTask == 0 || pTask == m_pCurrent)
	{
		return;				// can't externally kill the running task
	}
	void *pKey = pTask->GetUserData (TASK_USER_DATA_USER);
	if (pKey != 0)
	{
		TerminateGroup (pKey);		// an app: the whole process (its threads)
		return;
	}
	u64 nFlags = IrqSave ();
	TSchedNode *p = FindNode (pTask);
	if (p != 0 && pTask->GetState () != TaskStateTerminated)
	{
		p->bKilled = TRUE;
		if (p->nNoKill > 0)
		{
			p->bKillPending = TRUE;	// LeaveNoKill ends it, once it holds nothing
		}
		else
		{
			pTask->SetState (TaskStateTerminated);	// GetNextTask skips it; reaper frees it
		}
	}
	IrqRestore (nFlags);
}

void CScheduler::TerminateGroup (void *pKey)
{
	if (pKey == 0)
	{
		return;
	}
	u64 nFlags = IrqSave ();
	TSchedNode *p = m_pHead;
	for (unsigned i = 0; i < m_nTasks; i++, p = p->pNext)
	{
		CTask *pTask = p->pTask;
		if (   pTask == m_pCurrent
		    || pTask->GetUserData (TASK_USER_DATA_USER) != pKey
		    || pTask->GetState () == TaskStateTerminated)
		{
			continue;
		}
		p->bKilled = TRUE;
		if (p->nNoKill > 0)
		{
			p->bKillPending = TRUE;
		}
		else
		{
			pTask->SetState (TaskStateTerminated);
		}
	}
	IrqRestore (nFlags);
}

unsigned CScheduler::CountGroup (void *pKey)
{
	unsigned n = 0;
	TSchedNode *p = m_pHead;
	for (unsigned i = 0; i < m_nTasks; i++, p = p->pNext)
	{
		if (   pKey != 0
		    && p->pTask->GetUserData (TASK_USER_DATA_USER) == pKey
		    && p->pTask->GetState () != TaskStateTerminated)
		{
			n++;
		}
	}
	return n;
}

boolean CScheduler::GroupAlive (void *pKey)
{
	TSchedNode *p = m_pHead;
	for (unsigned i = 0; i < m_nTasks; i++, p = p->pNext)
	{
		if (   p->pTask->GetUserData (TASK_USER_DATA_USER) == pKey
		    && (p->pTask->GetState () != TaskStateTerminated || p->bKillPending))
		{
			return TRUE;
		}
	}
	return FALSE;
}

void CScheduler::EnterNoKill (void)
{
	u64 nFlags = IrqSave ();
	if (m_pCurNode != 0 && m_pCurNode->nNoKill < 255) m_pCurNode->nNoKill++;
	IrqRestore (nFlags);
}

void CScheduler::LeaveNoKill (void)
{
	u64 nFlags = IrqSave ();
	TSchedNode *p = m_pCurNode;
	boolean bEnd = FALSE;
	if (p != 0 && p->nNoKill > 0 && --p->nNoKill == 0 && p->bKillPending)
	{
		p->bKillPending = FALSE;
		bEnd = TRUE;
	}
	IrqRestore (nFlags);
	if (bEnd)
	{
		m_pCurrent->SetState (TaskStateTerminated);	// killed meanwhile: end here
		Yield ();					// never scheduled again
		for (;;) { }
	}
}

boolean CScheduler::IsValidTask (CTask *pTask)
{
	return pTask != 0 && FindNode (pTask) != 0;
}

void CScheduler::RegisterTaskSwitchHandler (TSchedulerTaskHandler *pHandler)
{
	assert (m_pTaskSwitchHandler == 0);
	m_pTaskSwitchHandler = pHandler;
	assert (m_pTaskSwitchHandler != 0);
}

void CScheduler::RegisterTaskTerminationHandler (TSchedulerTaskHandler *pHandler)
{
	assert (m_pTaskTerminationHandler == 0);
	m_pTaskTerminationHandler = pHandler;
	assert (m_pTaskTerminationHandler != 0);
}

void CScheduler::SuspendNewTasks (void)
{
	m_iSuspendNewTasks++;
}

void CScheduler::ResumeNewTasks (void)
{
	assert (m_iSuspendNewTasks > 0);
	m_iSuspendNewTasks--;
	if (m_iSuspendNewTasks == 0)
	{
		TSchedNode *p = m_pHead;
		for (unsigned i = 0; i < m_nTasks; i++, p = p->pNext)
		{
			if (p->pTask->GetState () == TaskStateNew)
			{
				p->pTask->Start ();
			}
		}
	}
}

// The callback must not yield: the reaper could free the task being visited meanwhile.
boolean CScheduler::EnumerateTasks (boolean (*pCallback) (CTask *pTask, const char *pName,
							  TTaskState State, TTaskFlags Flags,
							  void *pParam),
				    void *pParam)
{
	TSchedNode *p = m_pHead;
	for (unsigned i = 0; i < m_nTasks; i++, p = p->pNext)
	{
		CTask *pTask = p->pTask;

		TTaskFlags Flags = TaskFlagNone;
		if (pTask == m_pCurrent)
		{
			Flags = TaskFlagRunning;
		}
		else if (pTask->IsSuspended ())
		{
			Flags = TaskFlagSuspended;
		}

		if (!(*pCallback) (pTask, pTask->GetName (), pTask->GetState (), Flags, pParam))
		{
			return FALSE;
		}
	}

	return TRUE;
}

void CScheduler::ListTasks (CDevice *pTarget)
{
	assert (pTarget != 0);

	static const char Header[] = "#  ADDR     STAT  FL NAME\n";
	pTarget->Write (Header, sizeof Header-1);

	TSchedNode *p = m_pHead;
	for (unsigned i = 0; i < m_nTasks; i++, p = p->pNext)
	{
		CTask *pTask = p->pTask;

		TTaskState State = pTask->GetState ();
		assert (State < TaskStateUnknown);

		// must match TTaskState
		static const char *StateNames[] =
			{"new", "ready", "block", "block", "sleep", "term"};

		CString Line;
		Line.Format ("%02u %08lX %-5s %c%c %s\n",
			     i, (uintptr) pTask,
			     pTask == m_pCurrent ? "run" : StateNames[State],
			     pTask->IsSuspended () ? 'S' : ' ',
			     State == TaskStateBlockedWithTimeout ? 'T' : ' ',
			     pTask->GetName ());

		pTarget->Write (Line, Line.GetLength ());
	}
}

unsigned CScheduler::ReapTerminatedTasks (void)
{
	// Reap tasks that have ended (closed apps, ended threads): run the termination
	// handler (frees the address space with the process's last task), remove from the
	// list, and free the CTask + its stack. Called from the dedicated reaper task. A
	// terminated task is quiescent (GetNextTask skips it, so it never runs again) and
	// is never m_pCurrent here.
	//
	// A task killed from outside may still be on a wait list of its process (a mutex,
	// an event, a socket): it is freed only once its whole group is dead, and the
	// handlers of a batch all run before any task is deleted -- the last one frees the
	// process (its sockets closed, its objects deleted) while the tasks still exist.
	//
	// IRQ is masked for the whole teardown so it is atomic against the timer IRQ
	// (the teardown frees page tables + invalidates the TLB); restored on return.
	u64 nFlags = IrqSave ();

	TSchedNode *pBatch = 0;
	TSchedNode *p = m_pHead;
	for (unsigned i = 0; i < m_nTasks; i++, p = p->pNext)
	{
		CTask *pTask = p->pTask;
		if (   pTask == m_pCurrent
		    || pTask->GetState () != TaskStateTerminated)
		{
			continue;
		}
		void *pKey = pTask->GetUserData (TASK_USER_DATA_USER);
		if (p->bKilled && pKey != 0 && GroupAlive (pKey))
		{
			continue;			// (later, with the rest of its process)
		}
		p->pReapNext = pBatch;
		pBatch = p;
	}

	if (m_pTaskTerminationHandler != 0)
	{
		for (p = pBatch; p != 0; p = p->pReapNext)
		{
			(*m_pTaskTerminationHandler) (p->pTask);	// frees the address space
		}
	}

	unsigned nReaped = 0;
	while (pBatch != 0)
	{
		p = pBatch;
		pBatch = p->pReapNext;
		CTask *pTask = p->pTask;
		RemoveTask (pTask);		// (frees p)
		delete pTask;			// frees the CTask + its stack
		nReaped++;
	}

	IrqRestore (nFlags);
	return nReaped;
}

void CScheduler::AddTask (CTask *pTask)
{
	assert (pTask != 0);

	if (m_iSuspendNewTasks)
	{
		pTask->SetState (TaskStateNew);
	}

	TSchedNode *pNode = new TSchedNode;
	if (pNode == 0)
	{
		CLogger::Get ()->Write (FromScheduler, LogPanic, "No memory for a task");
	}
	pNode->pTask = pTask;
	pNode->pReapNext = 0;
	pNode->nNoKill = 0;
	pNode->nPreemptStreak = 0;
	pNode->bKillPending = FALSE;
	pNode->bKilled = FALSE;
	pNode->nPrio = SCHED_PRIO_NORMAL;
	pNode->bPrioSpent = FALSE;

	u64 nFlags = IrqSave ();		// (the IRQ exit path walks the list)
	if (m_pHead == 0)
	{
		pNode->pNext = pNode;
		m_pHead = m_pTail = m_pScan = pNode;
	}
	else
	{
		pNode->pNext = m_pHead;		// appended: last in round-robin order
		m_pTail->pNext = pNode;
		m_pTail = pNode;
	}
	m_nTasks++;
	IrqRestore (nFlags);
}

void CScheduler::RemoveTask (CTask *pTask)
{
	u64 nFlags = IrqSave ();
	TSchedNode *pNode = FindNode (pTask);
	assert (pNode != 0);
	assert (pNode != m_pCurNode);
	if (pNode != 0)
	{
		TSchedNode *pPrev = FindPrev (pNode);
		pPrev->pNext = pNode->pNext;
		if (m_pHead == pNode) m_pHead = pNode->pNext;
		if (m_pTail == pNode) m_pTail = pPrev;
		if (m_pScan == pNode) m_pScan = pPrev;
		if (pNode->nPrio != SCHED_PRIO_NORMAL) m_nPrioTasks--;
		m_nTasks--;
		delete pNode;
	}
	IrqRestore (nFlags);
}

boolean CScheduler::BlockTask (CTask **ppWaitListHead, unsigned nMicroSeconds,
				const volatile boolean *pState)
{
	assert (ppWaitListHead != 0);
	assert (m_pCurrent->m_pWaitListNext == 0);
	assert (m_pCurrent != 0);
	assert (m_pCurrent->GetState () == TaskStateReady);

	s_WaitLock.Acquire ();

	// The event set between its owner's check (CSynchronizationEvent::Wait) and here -- Set ()
	// on another core or in an interrupt found the list still empty and woke nobody: checked
	// again under the lock WakeTasks takes, so the check and the registration are one step
	// (upstream Circle 51.1, ab0768dc). The already-set answer of WaitWithTimeout.
	if (pState != 0 && *pState)
	{
		s_WaitLock.Release ();
		return nMicroSeconds == 0;
	}

	// Add current task to the waiting task list
	m_pCurrent->m_pWaitListNext = *ppWaitListHead;
	*ppWaitListHead = m_pCurrent;

	if (nMicroSeconds == 0)
	{
		m_pCurrent->SetState (TaskStateBlocked);
	}
	else
	{
		unsigned nTicks = nMicroSeconds * (CLOCKHZ / 1000000);
		unsigned nStartTicks = CTimer::Get ()->GetClockTicks ();

		m_pCurrent->SetWakeTicks (nStartTicks + nTicks);
		m_pCurrent->SetState (TaskStateBlockedWithTimeout);
	}

	s_WaitLock.Release ();

	Yield ();

	s_WaitLock.Acquire ();

	// Remove this task from the wait list in case it was woken by timeout and not
	// by the event signalling (in which case the list is already cleared and this
	// is a no-op). We only walk the list if we were woken by a timeout.
	if (nMicroSeconds > 0 && m_pCurrent->GetWakeTicks () == 0)
	{
		CTask *pPrev = 0;
		CTask *p = *ppWaitListHead;
		while (p)
		{
			if (p == m_pCurrent)
			{
				if (pPrev)
					pPrev->m_pWaitListNext = p->m_pWaitListNext;
				else
					*ppWaitListHead = p->m_pWaitListNext;
			}
			pPrev = p;
			p = p->m_pWaitListNext;
		}
	}
	m_pCurrent->m_pWaitListNext = 0;

	s_WaitLock.Release ();

	// GetWakeTicks() is zero if the timeout expired, non-zero if event-signalled.
	return m_pCurrent->GetWakeTicks () == 0;
}

void CScheduler::WakeTasks (CTask **ppWaitListHead)
{
	assert (ppWaitListHead != 0);

	s_WaitLock.Acquire ();

	CTask *pTask = *ppWaitListHead;
	*ppWaitListHead = 0;

	while (pTask)
	{
		// A task whose timeout already expired (GetNextTask made it Ready, WakeTicks 0) is still
		// on the list until it runs again and unlinks itself: an event set in that window (the
		// V3D's frame-done interrupt, right after one of the drawing task's 2 ms waits ended)
		// found it not blocked -- the assertion halted all four cores (the random N64 freeze).
		// It is awake already: only unlinked here (its own unlinking then finds nothing).
		if (   pTask->GetState () == TaskStateBlocked
		    || pTask->GetState () == TaskStateBlockedWithTimeout)
		{
			pTask->SetState (TaskStateReady);
		}

		CTask *pNext = pTask->m_pWaitListNext;
		pTask->m_pWaitListNext = 0;
		pTask = pNext;
	}

	s_WaitLock.Release ();
}

TSchedNode *CScheduler::GetNextTask (void)
{
	unsigned nTicks = CTimer::Get ()->GetClockTicks ();

	// A "real time" task first, if one is ready (none: not even a scan).
	if (m_nPrioTasks > 0)
	{
		TSchedNode *pNode = ScanPrio (nTicks, TRUE);
		if (pNode != 0)
		{
			return pNode;
		}
	}

	// Burst (after an app was preempted): skip the preempted CPU hogs, until it ends or
	// no other task is ready -- then back to plain round-robin over everyone.
	if (m_bBurst)
	{
		if ((int) (m_nBurstEnd - nTicks) > 0)
		{
			TSchedNode *pNode = ScanTasks (nTicks, TRUE);
			if (pNode != 0)
			{
				return pNode;
			}
		}
		m_bBurst = FALSE;
	}

	return ScanTasks (nTicks, FALSE);
}

// A ready "real time" task (nPrio > 0, its priority not spent), in round-robin order after
// m_pScan; the current task is left out (it yields: the others get their turn -- a spin on a
// lock held by an ordinary thread must not starve that thread). bTake: make it Ready (a
// timeout or a sleep that has ended), as ScanTasks does; FALSE only asks (OnTimerTick, IRQ).
TSchedNode *CScheduler::ScanPrio (unsigned nTicks, boolean bTake)
{
	TSchedNode *pNode = m_pScan != 0 ? m_pScan : m_pHead;
	for (unsigned i = 1; i <= m_nTasks; i++)
	{
		pNode = pNode->pNext;
		if (   pNode->nPrio == SCHED_PRIO_NORMAL
		    || pNode->bPrioSpent
		    || pNode == m_pCurNode)
		{
			continue;
		}
		CTask *pTask = pNode->pTask;
		if (pTask->IsSuspended ())
		{
			continue;
		}
		switch (pTask->GetState ())
		{
		case TaskStateReady:
			return pNode;

		case TaskStateBlockedWithTimeout:
			if ((int) (pTask->GetWakeTicks () - nTicks) > 0)
			{
				continue;
			}
			if (bTake)
			{
				pTask->SetState (TaskStateReady);
				pTask->SetWakeTicks (0);	// flag: timeout expired
			}
			return pNode;

		case TaskStateSleeping:
			if ((int) (pTask->GetWakeTicks () - nTicks) > 0)
			{
				continue;
			}
			if (bTake)
			{
				pTask->SetState (TaskStateReady);
			}
			return pNode;

		default:
			continue;
		}
	}
	return 0;
}

// One round-robin pass starting after m_pScan. bSkipHogs: skip the preempted tasks and
// the idle task -- 0 if no other task is ready.
TSchedNode *CScheduler::ScanTasks (unsigned nTicks, boolean bSkipHogs)
{
	TSchedNode *pNode = m_pScan != 0 ? m_pScan : m_pHead;

	TSchedNode *pIdle = 0;		// remember idle; use only if nothing else

	for (unsigned i = 1; i <= m_nTasks; i++)
	{
		pNode = pNode->pNext;

		CTask *pTask = pNode->pTask;

		if (pTask->IsSuspended ())
		{
			continue;
		}

		if (bSkipHogs && (pTask == m_pIdleTask || pNode->nPreemptStreak >= SCHED_HOG_STREAK))
		{
			continue;			// a CPU hog (or idle): not during a burst
		}

		switch (pTask->GetState ())
		{
		case TaskStateReady:
			if (pTask == m_pIdleTask)
			{
				// Deprioritize: only fall back to idle if no other task
				// is runnable this round.
				pIdle = pNode;
				continue;
			}
			return pNode;

		case TaskStateBlocked:
		case TaskStateNew:
			continue;

		case TaskStateBlockedWithTimeout:
			if ((int) (pTask->GetWakeTicks () - nTicks) > 0)
			{
				continue;
			}
			pTask->SetState (TaskStateReady);
			pTask->SetWakeTicks (0);	// flag: timeout expired
			return pNode;

		case TaskStateSleeping:
			if ((int) (pTask->GetWakeTicks () - nTicks) > 0)
			{
				continue;
			}
			pTask->SetState (TaskStateReady);
			return pNode;

		case TaskStateTerminated:
			// "Ending": no longer schedulable. We do NOT free it here -- doing
			// the heavy teardown (address-space free, TLB ops, heap frees, WM
			// lock) inside the scheduler core, with IRQ masked, is fragile.
			// ReapTerminatedTasks(), called from the kernel janitor loop in a
			// normal context, reaps it safely.
			continue;

		default:
			assert (0);
			break;
		}
	}

	// No regular task is runnable: fall back to the idle task if it is ready.
	return pIdle;
}

CScheduler *CScheduler::Get (void)
{
	// (s_pThis[0] first: Circle's libraries, built with Circle's own scheduler.h, read
	// s_pThis as one pointer in their inline IsActive () -- core 0's scheduler, as before)
	CScheduler *p = s_pThis[ThisCore ()];
	if (p == 0) p = s_pThis[0];		// a core without one (sound, app cores): as before
	assert (p != 0);
	return p;
}

// Weak override (same as Circle): when the scheduler is active and we are on the
// main kernel stack, report the current task's stack instead, so stack-overflow
// checks and the exception handler see the right bounds.
TStackInfo GetCurrentStack (void)
{
	TStackInfo StackInfo = __GetCurrentStackNoWeak ();

	if (   !CScheduler::IsActive ()
	    || StackInfo.Top != MEM_KERNEL_STACK)
	{
		return StackInfo;
	}

	return CScheduler::Get ()->GetCurrentTask ()->GetStack ();
}
