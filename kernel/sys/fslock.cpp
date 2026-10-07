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
#include <circle/timer.h>
#include <circle/types.h>
#include <fatfs/ff.h>

// One lock per PHYSICAL DRIVE (FatFs' VolToPart, diskio.cpp): the SD card's partitions (SD,
// SD1..SD3) share one, a USB device's volumes (USBn, USBnP1..P4) another. The drivers yield in
// the middle of an operation (the SD driver inside a command, the USB one between two transfers;
// diskio.cpp's bounce buffer and sector cache are per drive): another task must not reach the
// same drive meanwhile through another of its volumes. (FF_VOLUMES: FatFs' system mutex.)
static inline int LockSlot (int vol) { return vol >= 0 && vol < FF_VOLUMES ? VolToPart[vol].pd : vol; }

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

// OnyxDriverPoll: the SD driver's shorter waits (each under 2 ms) call it at each turn. Many in
// a row -- a long multi-block write (FatFs' multi-cluster transfers: one command for a whole
// file's contiguous clusters), the card taking 100-500 us per block -- kept core 0 for
// 200 ms ("stall: jet:cache ran 213 ms without yielding", pc in TimeoutWait). Once the task
// has run DRIVER_SLICE_US since its last yield, it yields here. Between two blocks of a
// transfer is a safe point: the volume lock is held (nobody else sends the card a command), and
// an SDHCI host holds a PIO transfer until its buffer is served (it stops the card's clock on a
// read) -- upstream Circle's NO_BUSY_WAIT yields at every turn of these same waits, and patch 7
// already yielded there when one wait was long. Reads, 40 MB/s, yield about every 400 KB; a
// write at 5-10 MB/s every 50-100 KB.
#define DRIVER_SLICE_US	10000

void OnyxDriverPoll (void)
{
	if (!CScheduler::IsActive ())		// (core 1, 2: no scheduler)
	{
		return;
	}
	CScheduler *pSched = CScheduler::Get ();
	if (   CTimer::GetClockTicks () - pSched->GetLastYield () >= DRIVER_SLICE_US
	    && IrqsOn () && !g_bCrashDumping)
	{
		pSched->Yield ();
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
