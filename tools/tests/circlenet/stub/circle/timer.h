// Host stub of Circle's CTimer for the TCP test (tools/tests/circlenet): a simulated clock the
// test advances, and kernel timers fired by CTimer::Advance.
#ifndef _circle_timer_h
#define _circle_timer_h

#include <circle/types.h>

#define HZ		100
#define MSEC2HZ(msec)	((msec) * HZ / 1000)
#define CLOCKHZ		1000000

typedef uintptr TKernelTimerHandle;
typedef void TKernelTimerHandler (TKernelTimerHandle hTimer, void *pParam, void *pContext);

class CTimer
{
public:
	CTimer (void);
	static CTimer *Get (void);

	unsigned GetTicks (void) const;
	unsigned GetTime (void) const;
	static unsigned GetClockTicks (void);

	TKernelTimerHandle StartKernelTimer (unsigned nDelay, TKernelTimerHandler *pHandler,
					     void *pParam = 0, void *pContext = 0);
	void CancelKernelTimer (TKernelTimerHandle hTimer);

	void Advance (unsigned nTicks);		// the test: time passes, timers fire

private:
	struct TTimer { bool bUsed; unsigned nElapsesAt; TKernelTimerHandler *pHandler; void *pParam, *pContext; };
	enum { MaxTimers = 64 };
	TTimer m_Timers[MaxTimers];
	unsigned m_nTicks;
	static CTimer *s_pThis;
};

#endif
