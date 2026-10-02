// host stub of Circle scheduler.h (tools/tests/run_image_test.sh): a yield or a sleep hands the
// processor to the test's next cooperative task (imagetest.cpp: TestYield), as the kernel's
// non-preemptive scheduler does.
#ifndef _circle_sched_scheduler_h
#define _circle_sched_scheduler_h
void TestYield (void);
class CScheduler
{
public:
	static bool IsActive (void)		{ return true; }
	static CScheduler *Get (void)		{ static CScheduler s; return &s; }
	void Yield (void)			{ TestYield (); }
	void MsSleep (unsigned)			{ TestYield (); }
};
#endif
