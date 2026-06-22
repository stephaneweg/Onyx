//
// task.cpp
//
// Our replacement for Circle's lib/sched/task.cpp. Build note: compile this
// instead of circle/lib/sched/task.cpp. The ONLY change from upstream is in
// TaskEntry(): a freshly-created task enables IRQ as its first action.
//
// Why: our Yield() runs context switches with IRQ disabled (to serialize against
// the timer-IRQ preemption path). A task resuming from Yield restores its own
// DAIF on the way out, but a task running for the FIRST time enters via TaskEntry
// and would otherwise inherit the switcher's IRQ-disabled state. Enabling IRQ
// here makes every new task start at task level with interrupts on.
//
// Derived from Circle (GPLv3), Copyright (C) 2015-2021 R. Stange.
//
#include <circle/sched/task.h>
#include <circle/sched/scheduler.h>
#include <circle/util.h>
#include <assert.h>

CTask::CTask (unsigned nStackSize, boolean bCreateSuspended)
:	m_State (bCreateSuspended ? TaskStateNew : TaskStateReady),
	m_bSuspended (FALSE),
	m_nStackSize (nStackSize),
	m_pStack (0),
	m_pWaitListNext (0)
{
	for (unsigned i = 0; i < TASK_USER_DATA_SLOTS; i++)
	{
		m_pUserData[i] = 0;
	}

	if (m_nStackSize != 0)
	{
		assert (m_nStackSize >= 1024);
#if AARCH == 32
		assert ((m_nStackSize & 3) == 0);
#else
		assert ((m_nStackSize & 15) == 0);
#endif
		m_pStack = new u8[m_nStackSize];
		assert (m_pStack != 0);

		InitializeRegs ();
	}

	m_Name.Format ("@%lp", this);

	CScheduler::Get ()->AddTask (this);
}

CTask::~CTask (void)
{
	assert (m_State == TaskStateTerminated);
	m_State = TaskStateUnknown;

	delete [] m_pStack;
	m_pStack = 0;
}

void CTask::Start (void)
{
	if (m_State == TaskStateNew)
	{
		m_State = TaskStateReady;
	}
	else
	{
		assert (m_bSuspended);
		m_bSuspended = FALSE;
	}
}

void CTask::Suspend (void)
{
	assert (m_State != TaskStateNew);
	assert (!m_bSuspended);
	m_bSuspended = TRUE;
}

void CTask::Run (void)		// dummy method which is never called
{
	assert (0);
}

void CTask::Terminate (void)
{
	m_State = TaskStateTerminated;
	m_Event.Set ();
	CScheduler::Get ()->Yield ();

	assert (0);
}

void CTask::WaitForTermination (void)
{
	// Before accessing any of our member variables make sure this task object
	// hasn't been deleted by checking it's still registered with the scheduler.
	if (!CScheduler::Get ()->IsValidTask (this))
	{
		return;
	}

	m_Event.Wait ();
}

void CTask::SetName (const char *pName)
{
	m_Name = pName;
}

const char *CTask::GetName (void) const
{
	return m_Name;
}

void CTask::SetUserData (void *pData, unsigned nSlot)
{
	m_pUserData[nSlot] = pData;
}

void *CTask::GetUserData (unsigned nSlot)
{
	return m_pUserData[nSlot];
}

#if AARCH == 32

void CTask::InitializeRegs (void)
{
	memset (&m_Regs, 0, sizeof m_Regs);

	m_Regs.r0 = (u32) this;		// pParam for TaskEntry()

	assert (m_pStack != 0);
	m_Regs.sp = (u32) m_pStack + m_nStackSize;

	m_Regs.lr = (u32) &TaskEntry;

#define VFP_FPEXC_EN	(1 << 30)
	m_Regs.fpexc = VFP_FPEXC_EN;
#define VFP_FPSCR_DN	(1 << 25)	// enable Default NaN mode
	m_Regs.fpscr = VFP_FPSCR_DN;
}

#else

void CTask::InitializeRegs (void)
{
	memset (&m_Regs, 0, sizeof m_Regs);

	m_Regs.x0 = (u64) this;		// pParam for TaskEntry()

	assert (m_pStack != 0);
	m_Regs.sp = (u64) m_pStack + m_nStackSize;

	m_Regs.x30 = (u64) &TaskEntry;

	u64 nFPCR;
	asm volatile ("mrs %0, fpcr" : "=r" (nFPCR));
	m_Regs.fpcr = nFPCR;
}

#endif

void CTask::TaskEntry (void *pParam)
{
#if AARCH == 64
	// A new task is first entered from a context switch that ran with IRQ
	// disabled (see CScheduler::Yield). Enable IRQ so the task runs at task
	// level with interrupts on, like every other task after it resumes.
	asm volatile ("msr daifclr, #2" ::: "memory");
#endif

	CTask *pThis = (CTask *) pParam;
	assert (pThis != 0);

	pThis->Run ();

	pThis->m_State = TaskStateTerminated;
	pThis->m_Event.Set ();
	CScheduler::Get ()->Yield ();

	assert (0);
}
