//
// demoF.c -- a borderless "panel": a frameless window pinned at the left edge with
// a column of buttons that launch the other demos via kapi_launch. Exercises both
// increment-2 features: WIN_FLAG_BORDERLESS (no title bar / border / close box,
// explicit position) and kapi_launch (spawning another app from app context).
// A proto-version of the Maynard sidebar.
//
#include "kapi.h"

#define W	52
#define H	220

static unsigned *fb;

static void clear (unsigned c)
{
	for (int i = 0; i < W * H; i++) fb[i] = c;
}

static void launchA (unsigned long s, int e, long v) { (void) s; (void) e; (void) v; kapi_launch ("demoA"); }
static void launchB (unsigned long s, int e, long v) { (void) s; (void) e; (void) v; kapi_launch ("demoB"); }
static void launchC (unsigned long s, int e, long v) { (void) s; (void) e; (void) v; kapi_launch ("demoC"); }
static void launchD (unsigned long s, int e, long v) { (void) s; (void) e; (void) v; kapi_launch ("demoD"); }
static void launchE (unsigned long s, int e, long v) { (void) s; (void) e; (void) v; kapi_launch ("demoE"); }

int main (void)
{
	// Borderless, pinned just inside the left edge. No close box => runs forever
	// (a panel), so it is not draggable and cannot be closed by the user.
	fb = kapi_create_window_ex (4, 80, W, H, "panel", WIN_FLAG_BORDERLESS);
	if (fb == 0)
	{
		return 1;
	}
	clear (0x00303848);

	add_button (6,   6, 40, 36, "A", launchA);
	add_button (6,  48, 40, 36, "B", launchB);
	add_button (6,  90, 40, 36, "C", launchC);
	add_button (6, 132, 40, 36, "D", launchD);
	add_button (6, 174, 40, 36, "E", launchE);

	while (!should_exit ())
	{
		pump_events ();
		msleep (16);
	}

	return 0;
}
