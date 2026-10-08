// (Elegant's stand-in) The event a window pulses when it queues an event (CWindow::SetWake): the
// kernel's own pump wake. Not used by the server yet (stage 3: the kernel's per-process queue).
#ifndef _circle_sched_synchronizationevent_h
#define _circle_sched_synchronizationevent_h
class CSynchronizationEvent
{
public:
	void Set (void)		{}
	void Clear (void)	{}
};
#endif
