// host stub of Circle timer.h (tools/tests/run_ipc_test.sh): a clock the test advances
#ifndef _circle_timer_h
#define _circle_timer_h
#include <circle/types.h>
class CTimer
{
public:
	static unsigned GetClockTicks (void);		// (ipchost.cpp: +1 ms per call)
};
#endif
