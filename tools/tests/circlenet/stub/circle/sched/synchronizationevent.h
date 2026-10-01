// Host stub (tools/tests/circlenet): no tasks -- a wait returns at once ("timed out" when the
// event is not set).
#ifndef _circle_sched_synchronizationevent_h
#define _circle_sched_synchronizationevent_h
#include <circle/types.h>
class CSynchronizationEvent
{
public:
	CSynchronizationEvent (boolean bState = FALSE) : m_bState (bState) {}
	boolean GetState (void) const { return m_bState; }
	void Clear (void) { m_bState = FALSE; }
	void Set (void) { m_bState = TRUE; }
	void Wait (void) {}
	boolean WaitWithTimeout (unsigned) { return !m_bState; }
private:
	volatile boolean m_bState;
};
#endif
