// host stub of Circle task.h (tools/tests/run_ipc_test.sh)
#ifndef _circle_sched_task_h
#define _circle_sched_task_h
#define TASK_USER_DATA_USER	0
class CTask
{
public:
	void *GetUserData (unsigned);			// (ipchost.cpp: the current fake process)
};
#endif
