//
// demoF.cpp -- a borderless "panel": a frameless window pinned at the left edge with a
// column of buttons that launch the other demos via kapi_launch. Exercises both
// WIN_FLAG_BORDERLESS (no title bar / border / close box, explicit position) and
// kapi_launch. Built on the wtk widget toolkit.
//
#include "wtk/wtk.h"

using namespace wtk;

#define W	52
#define H	220

static void launchA (Widget &) { kapi_launch ("demoA"); }
static void launchB (Widget &) { kapi_launch ("demoB"); }
static void launchC (Widget &) { kapi_launch ("demoC"); }
static void launchD (Widget &) { kapi_launch ("demoD"); }
static void launchE (Widget &) { kapi_launch ("demoE"); }

int main (void)
{
	// Borderless, pinned just inside the left edge. No close box => runs forever (a panel).
	Root root (4, 80, W, H, "panel", WIN_FLAG_BORDERLESS);
	root.bg = 0x00303848;
	root.addChild (new Button (6,   6, 40, 36, "A", launchA));
	root.addChild (new Button (6,  48, 40, 36, "B", launchB));
	root.addChild (new Button (6,  90, 40, 36, "C", launchC));
	root.addChild (new Button (6, 132, 40, 36, "D", launchD));
	root.addChild (new Button (6, 174, 40, 36, "E", launchE));
	root.run ();
	return 0;
}
