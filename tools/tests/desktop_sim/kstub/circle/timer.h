// (host stub) The timer: its ticks (HZ a second) are the test's to set (g_nStubTicks).
#ifndef _circle_timer_h
#define _circle_timer_h
#define HZ		100
extern unsigned g_nStubTicks;
class CTimer { public: static CTimer *Get (void) { static CTimer t; return &t; } unsigned GetTicks (void) { return g_nStubTicks; } };
#endif
