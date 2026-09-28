//
// spin.cpp -- M1 preemption test: a deliberate CPU hog that NEVER yields.
//
// Under the cooperative scheduler, launching this freezes the WHOLE machine: it
// never calls present()/yield()/msleep(), so no other task ever runs again. With
// track-A preemption (timer slice -> trampoline -> Yield on the app's own stack),
// the 50 ms slice forces a switch out of it, so the compositor, panel and other
// apps stay responsive while it spins. That responsiveness IS the test.
//
// It draws ONCE at startup (it cannot redraw -- drawing would yield), then loops
// forever in its own user-VA code (exactly the context the preempt gate targets:
// EL1t + ELR in user VA). To stop it, kill it from the task manager (taskman): it
// never checks should_exit(), so the window close box can't reach it -- which is
// the whole point (it never yields).
//
#include "kapi.h"
#include "wtk/wtk.h"		// wtk window decoration

#define W 272
#define H 116

int main (void)
{
	unsigned *fb = create_window (W, H, "spin: CPU hog (no yield)");
	if (fb == 0)
	{
		return 1;
	}

	wtk::wk_decorate_window ();			// (reads the theme: the palette)
	using namespace wtk;
	for (int i = 0; i < W * H; i++)
	{
		fb[i] = C_BG;
	}
	draw_text (fb, W, H, 12, 16, "CPU hog -- never yields.",        C_TEXT, 1, 2);
	draw_text (fb, W, H, 12, 40, "Cooperative kernel: UI FREEZES.", wk_mix (C_TEXT, 0x00E03C3C, 150));
	draw_text (fb, W, H, 12, 58, "Preemptive (track A): UI alive.", wk_mix (C_TEXT, 0x0030A050, 160));
	draw_text (fb, W, H, 12, 88, "Stop me from taskman.",           C_DIS);
	present ();

	// Pure busy loop in user VA: NO present/yield/msleep anywhere. The empty
	// `asm volatile` consumes `acc` each iteration, so -O2 cannot optimise the loop
	// away. This is the worst case for a cooperative kernel and the cleanest proof
	// for a preemptive one.
	unsigned long acc = 0;
	unsigned long i   = 0;
	for (;;)
	{
		acc += (++i) * 2654435761UL;
		asm volatile ("" :: "r" (acc) : "memory");
	}
	// not reached
}
