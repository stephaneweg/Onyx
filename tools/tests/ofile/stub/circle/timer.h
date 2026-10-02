// host stub of Circle's CTimer for tools/tests/run_ofile_test.sh (the harness defines it)
#ifndef _circle_timer_h
#define _circle_timer_h
#include <circle/types.h>
class CTimer
{
public:
	static CTimer *Get (void);
	int GetTimeZone (void) const;
	unsigned GetUniversalTime (void) const;
	static unsigned GetClockTicks (void);
};
#endif
