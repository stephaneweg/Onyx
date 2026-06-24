//
// demoF.cpp -- a borderless "panel": a frameless window pinned at the left edge with
// a column of buttons that launch the other demos via kapi_launch. Exercises both
// WIN_FLAG_BORDERLESS (no title bar / border / close box, explicit position) and
// kapi_launch (spawning another app from app context). A proto-version of the Maynard
// sidebar -- now built on the user-side uikit toolkit (the kernel widget API is gone).
//
#include "kapi.h"
#include "uikit.hpp"

#define W	52
#define H	220

static ui::Ui *g_ui;

static void launchA (ui::Widget &) { kapi_launch ("demoA"); }
static void launchB (ui::Widget &) { kapi_launch ("demoB"); }
static void launchC (ui::Widget &) { kapi_launch ("demoC"); }
static void launchD (ui::Widget &) { kapi_launch ("demoD"); }
static void launchE (ui::Widget &) { kapi_launch ("demoE"); }

static void on_evt (unsigned long s, int ev, long v) { g_ui->onEvent (s, ev, v); }

int main (void)
{
	// Borderless, pinned just inside the left edge. No close box => runs forever
	// (a panel), so it is not draggable and cannot be closed by the user.
	unsigned *fb = kapi_create_window_ex (4, 80, W, H, "panel", WIN_FLAG_BORDERLESS);
	if (fb == 0)
	{
		return 1;
	}

	ui::Ui ui (fb, W, H); g_ui = &ui;	// borderless: no chrome
	ui.col_bg = 0x00303848;
	ui.button (6,   6, 40, 36, "A", launchA);
	ui.button (6,  48, 40, 36, "B", launchB);
	ui.button (6,  90, 40, 36, "C", launchC);
	ui.button (6, 132, 40, 36, "D", launchD);
	ui.button (6, 174, 40, 36, "E", launchE);

	kapi_set_pointer_handler (on_evt);
	kapi_set_key_handler (on_evt);

	while (!should_exit ())
	{
		pump_events ();
		if (ui.dirty ()) { ui.background (); ui.drawAll (); present (); }
		msleep (16);
	}
	return 0;
}
