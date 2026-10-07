// (Elegant's stand-in for Circle's timer) The ticks, HZ a second: the kernel's (main.cpp).
#ifndef _circle_timer_h
#define _circle_timer_h
#define HZ		100
extern "C" unsigned el_port_ticks (void);
class CTimer
{
public:
	static CTimer *Get (void)	{ return (CTimer *) 1; }	// (no state: never read)
	unsigned GetTicks (void)	{ return el_port_ticks (); }
};
#endif
