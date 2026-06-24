//
// demoB.c -- EL0 windowed demo: an animated colour field. Independent process from
// demoA; both run at the same time (preemption) and the compositor shows both.
//
#include "kapi.h"
#include "uikit.hpp"		// ui::decorate_window

#define W 260
#define H 200

static unsigned *fb;

int main (void)
{
	fb = create_window (W, H, "demo B: colour field");
	if (fb == 0)
	{
		return 1;
	}
	ui::decorate_window ();			// user-side window chrome

	unsigned t = 0;
	while (!should_exit ())
	{
		for (int y = 0; y < H; y++)
		{
			for (int x = 0; x < W; x++)
			{
				unsigned r = (unsigned) (x + t) & 0xFF;
				unsigned g = (unsigned) (y + (t >> 1)) & 0xFF;
				unsigned b = (unsigned) ((x + y + t) >> 1) & 0xFF;
				fb[y * W + x] = (r << 16) | (g << 8) | b;
			}
		}

		t += 4;
		present ();
		pump_events ();
		msleep (20);
	}

	return 0;
}
