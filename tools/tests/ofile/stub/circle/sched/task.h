// host stub of Circle's CTask for tools/tests/run_ofile_test.sh
#ifndef _circle_sched_task_h
#define _circle_sched_task_h
#define TASK_USER_DATA_USER	0
class CTask
{
public:
	const char *GetName (void)		{ return "test"; }
	void *GetUserData (unsigned)		{ return 0; }
};
#endif
