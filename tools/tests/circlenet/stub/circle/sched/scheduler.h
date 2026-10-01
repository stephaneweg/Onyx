// Host stub (tools/tests/circlenet)
#ifndef _circle_sched_scheduler_h
#define _circle_sched_scheduler_h
#include <circle/types.h>
class CScheduler
{
public:
	static CScheduler *Get (void) { static CScheduler s; return &s; }
	void Yield (void) {}
};
#endif
