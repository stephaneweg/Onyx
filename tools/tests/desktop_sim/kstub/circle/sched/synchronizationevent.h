// (host stub) The event a window pulses when it queues an event (CWindow::SetWake): counted, so a
// test can tell that the owner's pump was woken.
#ifndef _circle_sched_synchronizationevent_h
#define _circle_sched_synchronizationevent_h
class CSynchronizationEvent
{
public:
	CSynchronizationEvent (void) : m_nSets (0) {}
	void Set (void)		{ m_nSets++; }
	void Clear (void)	{}
	unsigned m_nSets;
};
#endif
