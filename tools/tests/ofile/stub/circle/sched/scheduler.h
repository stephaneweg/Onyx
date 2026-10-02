// host stub of the kernel's CScheduler for tools/tests/run_ofile_test.sh (one flow, no tasks)
#ifndef _circle_sched_scheduler_h
#define _circle_sched_scheduler_h
#include <circle/sched/task.h>
class CScheduler
{
public:
	static bool IsActive (void)		{ return false; }
	static CScheduler *Get (void)		{ return 0; }
	CTask *GetCurrentTask (void)		{ return 0; }
	void Yield (void)			{}
	void EnterNoKill (void)			{}
	void LeaveNoKill (void)			{}
	void usSleep (unsigned)			{}
};
#endif
