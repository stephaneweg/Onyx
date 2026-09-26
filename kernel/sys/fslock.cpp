//
// fslock.cpp -- the FatFs volume lock and the SD driver's wait hook (Onyx side of two
// weak hooks in our Circle fork: addon/fatfs/ffsystem.cpp, addon/SDCard/emmc.cpp).
//
// The kernel is not preempted, so a task waiting for the SD card in a busy loop held
// the CPU for the whole command: a directory walk (the menu bar reading every app.txt)
// or a big read froze the GUI and the network 100-200 ms at a time. Now the driver
// yields while the card keeps it waiting. That is only safe because the FatFs volume
// lock is a SLEEPING lock here (its waiters yield): with Circle's spin lock, a second
// task entering FatFs would spin forever against the holder it never lets run.
//
// The holder is also in a no-kill section (CScheduler::EnterNoKill): killed meanwhile,
// it ends only once it has released the lock -- a dead owner would lock the card forever.
//
#include <circle/sched/scheduler.h>
#include <circle/types.h>
#include <fatfs/ff.h>

static CTask   *volatile s_pOwner[FF_VOLUMES + 1];
static unsigned          s_nDepth[FF_VOLUMES + 1];

static inline boolean IrqsOn (void)
{
	u64 nDAIF;
	asm volatile ("mrs %0, daif" : "=r" (nDAIF));
	return (nDAIF & (1 << 7)) == 0;
}

void OnyxFsLockTake (int vol)
{
	if (!CScheduler::IsActive () || vol < 0 || vol > FF_VOLUMES)
	{
		return;
	}
	CScheduler *pSched = CScheduler::Get ();
	CTask *pMe = pSched->GetCurrentTask ();
	for (;;)
	{
		// Kernel code is not preempted and no IRQ handler uses FatFs: test-and-set
		// needs no atomics on this core.
		if (s_pOwner[vol] == 0)
		{
			s_pOwner[vol] = pMe;
			s_nDepth[vol] = 1;
			pSched->EnterNoKill ();
			return;
		}
		if (s_pOwner[vol] == pMe)
		{
			s_nDepth[vol]++;
			return;
		}
		pSched->Yield ();			// the holder is waiting for the card
	}
}

void OnyxFsLockGive (int vol)
{
	if (!CScheduler::IsActive () || vol < 0 || vol > FF_VOLUMES)
	{
		return;
	}
	CScheduler *pSched = CScheduler::Get ();
	if (s_pOwner[vol] != pSched->GetCurrentTask () || s_nDepth[vol] == 0)
	{
		return;
	}
	if (--s_nDepth[vol] == 0)
	{
		s_pOwner[vol] = 0;
		pSched->LeaveNoKill ();			// (ends the task here if it was killed)
	}
}

void OnyxDriverWait (void)
{
	// Only from a task with IRQs on: a caller that masked them wants the wait atomic
	// (the reaper's teardown), and nothing may switch from interrupt context.
	if (CScheduler::IsActive () && IrqsOn ())
	{
		CScheduler::Get ()->Yield ();
	}
}
