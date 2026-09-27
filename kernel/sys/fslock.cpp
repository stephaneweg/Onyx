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
#include <kern/crashlog.h>
#include <circle/sched/scheduler.h>
#include <circle/types.h>
#include <fatfs/ff.h>

// The SD card's partitions (FatFs volumes SD, SD1..SD3: 0..3) share ONE lock: the driver
// yields in the middle of a command, and another task must not send the same card a
// command meanwhile through another volume.
static inline int LockSlot (int vol) { return vol >= 0 && vol <= 3 ? 0 : vol; }

static CTask   *s_pOwner[FF_VOLUMES + 1];
static unsigned          s_nDepth[FF_VOLUMES + 1];

static inline boolean IrqsOn (void)
{
	u64 nDAIF;
	asm volatile ("mrs %0, daif" : "=r" (nDAIF));
	return (nDAIF & (1 << 7)) == 0;
}

void OnyxFsLockTake (int vol)
{
	vol = LockSlot (vol);
	if (!CScheduler::IsActive () || vol < 0 || vol > FF_VOLUMES)
	{
		return;
	}
	CScheduler *pSched = CScheduler::Get ();
	CTask *pMe = pSched->GetCurrentTask ();
	for (;;)
	{
		// Kernel code is not preempted and no IRQ handler uses FatFs, but the network
		// core (netcore=1: core 3 reads the WLAN firmware, wpa_supplicant.conf) takes it
		// too: an atomic test-and-set.
		CTask *pFree = 0;
		if (__atomic_compare_exchange_n (&s_pOwner[vol], &pFree, pMe, FALSE,
						 __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
		{
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
	vol = LockSlot (vol);
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
		__atomic_store_n (&s_pOwner[vol], (CTask *) 0, __ATOMIC_RELEASE);
		pSched->LeaveNoKill ();			// (ends the task here if it was killed)
	}
}

void OnyxDriverWait (void)
{
	// Only from a task with IRQs on: a caller that masked them wants the wait atomic
	// (the reaper's teardown), and nothing may switch from interrupt context.
	if (CScheduler::IsActive () && IrqsOn () && !g_bCrashDumping)	// (core 1's crash dump: never)
	{
		CScheduler::Get ()->Yield ();
	}
}
