// host stub of Circle scheduler.h (tools/tests/run_ipc_test.sh): one task, whose process the test
// switches (two handle tables standing for two processes)
#ifndef _circle_sched_scheduler_h
#define _circle_sched_scheduler_h
#include <circle/sched/task.h>
class CScheduler
{
public:
	static bool IsActive (void)		{ return true; }
	static CScheduler *Get (void)		{ static CScheduler s; return &s; }
	CTask *GetCurrentTask (void)		{ static CTask t; return &t; }
	void Yield (void)			{}
};
#endif
