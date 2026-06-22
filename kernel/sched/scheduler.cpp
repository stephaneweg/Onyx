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

CScheduler *CScheduler::s_pThis = 0;

CScheduler::CScheduler (void)
:	m_nTasks (0),
	m_pCurrent (0),
	m_nCurrent (0),
	m_pTaskSwitchHandler (0),
	m_pTaskTerminationHandler (0),
	m_iSuspendNewTasks (0),
	m_bResched (FALSE),
	m_nSliceTicks (SCHED_SLICE_TICKS)
{
	assert (s_pThis == 0);
	s_pThis = this;

	m_pCurrent = new CTask (0);		// represents the main task currently running
	assert (m_pCurrent != 0);
	m_pCurrent->SetName ("main");
}

CScheduler::~CScheduler (void)
{
	m_pTaskSwitchHandler = 0;
	m_pTaskTerminationHandler = 0;

	s_pThis = 0;
}

void CScheduler::Yield (void)
{
	// Pick the next ready task. If none is ready (all sleeping/blocked), wait for
	// an interrupt (timer tick or device IRQ) to make one ready, then rescan.
	// This replaces Circle's busy-spin + assert(m_nTasks>0). IRQs must be enabled
	// here (they are at task level); the periodic 100 Hz tick guarantees we wake
	// at least every 10 ms to re-evaluate sleep/timeout deadlines.
	while ((m_nCurrent = GetNextTask ()) == MAX_TASKS)
	{
		asm volatile ("wfi");
	}

	assert (m_nCurrent < MAX_TASKS);
	CTask *pNext = m_pTask[m_nCurrent];
	assert (pNext != 0);
	if (m_pCurrent == pNext)
	{
		// Still the only runnable task: give it a fresh slice and continue.
		m_nSliceTicks = SCHED_SLICE_TICKS;
		m_bResched = FALSE;
		return;
	}

	TTaskRegisters *pOldRegs = m_pCurrent->GetRegs ();
	m_pCurrent = pNext;
	TTaskRegisters *pNewRegs = m_pCurrent->GetRegs ();

	// The task that gets control now starts a fresh time slice.
	m_nSliceTicks = SCHED_SLICE_TICKS;
	m_bResched = FALSE;

	if (m_pTaskSwitchHandler != 0)
	{
		(*m_pTaskSwitchHandler) (m_pCurrent);
	}

	assert (pOldRegs != 0);
	assert (pNewRegs != 0);
	TaskSwitch (pOldRegs, pNewRegs);
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

	for (unsigned i = 0; i < m_nTasks; i++)
	{
		CTask *pTask = m_pTask[i];

		if (   pTask != 0
		    && strcmp (pTask->GetName (), pTaskName) == 0)
		{
			return pTask;
		}
	}

	return 0;
}

boolean CScheduler::IsValidTask (CTask *pTask)
{
	for (unsigned i = 0; i < m_nTasks; i++)
	{
		if (m_pTask[i] != 0 && m_pTask[i] == pTask)
		{
			return TRUE;
		}
	}

	return FALSE;
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
		for (unsigned i = 0; i < m_nTasks; i++)
		{
			if (m_pTask[i] != 0 && m_pTask[i]->GetState () == TaskStateNew)
			{
				m_pTask[i]->Start ();
			}
		}
	}
}

boolean CScheduler::EnumerateTasks (boolean (*pCallback) (CTask *pTask, const char *pName,
							  TTaskState State, TTaskFlags Flags,
							  void *pParam),
				    void *pParam)
{
	for (unsigned i = 0; i < m_nTasks; i++)
	{
		CTask *pTask = m_pTask[i];
		if (pTask == 0)
		{
			continue;
		}

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

	for (unsigned i = 0; i < m_nTasks; i++)
	{
		CTask *pTask = m_pTask[i];
		if (pTask == 0)
		{
			continue;
		}

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

void CScheduler::AddTask (CTask *pTask)
{
	assert (pTask != 0);

	if (m_iSuspendNewTasks)
	{
		pTask->SetState (TaskStateNew);
	}

	for (unsigned i = 0; i < m_nTasks; i++)
	{
		if (m_pTask[i] == 0)
		{
			m_pTask[i] = pTask;

			return;
		}
	}

	if (m_nTasks >= MAX_TASKS)
	{
		CLogger::Get ()->Write (FromScheduler, LogPanic, "System limit of tasks exceeded");
	}

	m_pTask[m_nTasks++] = pTask;
}

void CScheduler::RemoveTask (CTask *pTask)
{
	for (unsigned i = 0; i < m_nTasks; i++)
	{
		if (m_pTask[i] == pTask)
		{
			m_pTask[i] = 0;

			if (i == m_nTasks-1)
			{
				m_nTasks--;
			}

			return;
		}
	}

	assert (0);
}

boolean CScheduler::BlockTask (CTask **ppWaitListHead, unsigned nMicroSeconds)
{
	assert (ppWaitListHead != 0);
	assert (m_pCurrent->m_pWaitListNext == 0);
	assert (m_pCurrent != 0);
	assert (m_pCurrent->GetState () == TaskStateReady);

	m_SpinLock.Acquire ();

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

	m_SpinLock.Release ();

	Yield ();

	m_SpinLock.Acquire ();

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

	m_SpinLock.Release ();

	// GetWakeTicks() is zero if the timeout expired, non-zero if event-signalled.
	return m_pCurrent->GetWakeTicks () == 0;
}

void CScheduler::WakeTasks (CTask **ppWaitListHead)
{
	assert (ppWaitListHead != 0);

	m_SpinLock.Acquire ();

	CTask *pTask = *ppWaitListHead;
	*ppWaitListHead = 0;

	while (pTask)
	{
#ifdef NDEBUG
		if (   pTask == 0
		    || (   pTask->GetState () != TaskStateBlocked
		        && pTask->GetState () != TaskStateBlockedWithTimeout))
		{
			CLogger::Get ()->Write (FromScheduler, LogPanic, "Tried to wake non-blocked task");
		}
#else
		assert (pTask != 0);
		assert (   pTask->GetState () == TaskStateBlocked
		        || pTask->GetState () == TaskStateBlockedWithTimeout);
#endif

		pTask->SetState (TaskStateReady);

		CTask *pNext = pTask->m_pWaitListNext;
		pTask->m_pWaitListNext = 0;
		pTask = pNext;
	}

	m_SpinLock.Release ();
}

unsigned CScheduler::GetNextTask (void)
{
	unsigned nTask = m_nCurrent < MAX_TASKS ? m_nCurrent : 0;

	unsigned nTicks = CTimer::Get ()->GetClockTicks ();

	for (unsigned i = 1; i <= m_nTasks; i++)
	{
		if (++nTask >= m_nTasks)
		{
			nTask = 0;
		}

		CTask *pTask = m_pTask[nTask];
		if (pTask == 0)
		{
			continue;
		}

		if (pTask->IsSuspended ())
		{
			continue;
		}

		switch (pTask->GetState ())
		{
		case TaskStateReady:
			return nTask;

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
			return nTask;

		case TaskStateSleeping:
			if ((int) (pTask->GetWakeTicks () - nTicks) > 0)
			{
				continue;
			}
			pTask->SetState (TaskStateReady);
			return nTask;

		case TaskStateTerminated:
			if (pTask == m_pCurrent)
			{
				// Cannot delete the currently executing task (we run on its
				// stack!). Wait for another task to yield and sweep it.
				continue;
			}

			if (m_pTaskTerminationHandler != 0)
			{
				(*m_pTaskTerminationHandler) (pTask);
			}
			RemoveTask (pTask);
			delete pTask;
			return MAX_TASKS;

		default:
			assert (0);
			break;
		}
	}

	return MAX_TASKS;
}

CScheduler *CScheduler::Get (void)
{
	assert (s_pThis != 0);
	return s_pThis;
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
