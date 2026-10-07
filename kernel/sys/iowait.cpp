//
// iowait.cpp -- the shared readiness wait (see kern/iowait.h). Owner: WP-0 (shared by the v75
// work packages, docs/POSIX-PLAN.md §3).
//
// ---------------------------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2026 Stéphane Wegener and the Onyx contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software
// and associated documentation files (the "Software"), to deal in the Software without
// restriction, including without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
// BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
// ---------------------------------------------------------------------------------------------
//
#include <kern/iowait.h>
#include <kern/kapi_abi.h>		// KAPI_WAIT_FOREVER
#include <circle/sched/synchronizationevent.h>
#include <circle/sched/scheduler.h>
#include <circle/timer.h>
#include <circle/synchronize.h>

static volatile u32 s_nGen = 0;
static CSynchronizationEvent s_Event;		// only ever pulsed (Set, then Clear)
static volatile unsigned s_nWaiters = 0;

static void (*s_pHook[IOWAIT_TICK_HOOKS]) (void);
static volatile unsigned s_nHooks = 0;

u32 IoGen (void)
{
	return s_nGen;
}

void IoWake (void)
{
	s_nGen = s_nGen + 1;
	if (s_nWaiters != 0)
	{
		s_Event.Set ();				// (the sleepers made ready; they run later)
		s_Event.Clear ();
	}
}

// (WP-FILE/PROC) A task killed while it sleeps on the event would stay on its wait list, and the
// reaper would free it there: the next IoWake would walk freed memory. So each sleep is in a
// no-kill section of at most IOWAIT_SLICE_MS: killed meanwhile, the task wakes at the end of
// the slice, off the list, and ends in LeaveNoKill. A longer wait is a series of slices.
#define IOWAIT_SLICE_MS	100

int IoWait (u32 nGen, unsigned nTimeoutMs)
{
	if (s_nGen != nGen) return 0;
	if (nTimeoutMs == 0) return 1;

	boolean bForever = nTimeoutMs == KAPI_WAIT_FOREVER;
	if (nTimeoutMs > 1800000) nTimeoutMs = 1800000;		// (30 min: the clock's range)
	unsigned nStart = CTimer::GetClockTicks ();
	CScheduler *pSched = CScheduler::IsActive () ? CScheduler::Get () : 0;
	for (;;)
	{
		unsigned nSlice = IOWAIT_SLICE_MS;
		if (!bForever)
		{
			unsigned nPast = (CTimer::GetClockTicks () - nStart) / 1000;
			if (nPast >= nTimeoutMs) return 1;
			if (nTimeoutMs - nPast < nSlice) nSlice = nTimeoutMs - nPast;
		}

		if (pSched != 0) pSched->EnterNoKill ();
		// The IRQ masked from the check until the task is on the event's list: Circle's Wait
		// tests the state, then blocks -- an IoWake from an IRQ in between would be lost. (Yield
		// keeps each task's own DAIF: the others run with their IRQs; we come back masked and
		// restore.)
		u64 nFlags;
		asm volatile ("mrs %0, daif; msr daifset, #2" : "=r" (nFlags) :: "memory");
		if (s_nGen == nGen)
		{
			s_nWaiters = s_nWaiters + 1;
			s_Event.WaitWithTimeout (nSlice * 1000);
			s_nWaiters = s_nWaiters - 1;
		}
		asm volatile ("msr daif, %0" :: "r" (nFlags) : "memory");
		if (pSched != 0) pSched->LeaveNoKill ();	// (killed meanwhile: it ends here)

		if (s_nGen != nGen) return 0;
	}
}

boolean IoWaitAddTickHook (void (*pfn) (void))
{
	if (pfn == 0) return FALSE;
	u64 nFlags;
	asm volatile ("mrs %0, daif; msr daifset, #2" : "=r" (nFlags) :: "memory");
	boolean bOk = FALSE;
	if (s_nHooks < IOWAIT_TICK_HOOKS)
	{
		s_pHook[s_nHooks] = pfn;
		DataMemBarrier ();			// (the hook before the count)
		s_nHooks = s_nHooks + 1;
		bOk = TRUE;
	}
	asm volatile ("msr daif, %0" :: "r" (nFlags) : "memory");
	return bOk;
}

void IoWaitTick (void)
{
	unsigned n = s_nHooks;
	for (unsigned i = 0; i < n; i++)
	{
		s_pHook[i] ();
	}
}
