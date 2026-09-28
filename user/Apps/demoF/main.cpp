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

// The panel's face (the theme's), a 1-px outline round it (a borderless window: no frame).
class PanelRoot : public Root
{
public:
	PanelRoot () : Root (4, 80, W, H, "panel", WIN_FLAG_BORDERLESS) {}
	void onDraw () override
	{
		wk_rbox (canvas, 0, 0, width, height, 0, wk_tone (bg, 150), wk_tone (bg, 118));
		wk_rline (canvas, 0, 0, width, height, 0, wk_tone (bg, 70), 220);
	}
};

static void launchA (Widget &) { kapi_launch ("demoA"); }
static void launchB (Widget &) { kapi_launch ("demoB"); }
static void launchC (Widget &) { kapi_launch ("demoC"); }
static void launchD (Widget &) { kapi_launch ("demoD"); }
static void launchE (Widget &) { kapi_launch ("demoE"); }

int main (void)
{
	// Borderless, pinned just inside the left edge. No close box => runs forever (a panel).
	PanelRoot root;
	root.addChild (new Button (6,   6, 40, 36, "A", launchA));
	root.addChild (new Button (6,  48, 40, 36, "B", launchB));
	root.addChild (new Button (6,  90, 40, 36, "C", launchC));
	root.addChild (new Button (6, 132, 40, 36, "D", launchD));
	root.addChild (new Button (6, 174, 40, 36, "E", launchE));
	root.run ();
	return 0;
}
