//
// shutdown -- the end-of-session dialog (menu bar > Onyx > Shut Down...).
//
// Restart: unmount the SD card and reboot. Shut Down: unmount, then halt -- the screen
// keeps the last frame ("It is now safe to turn off the Raspberry Pi") and the ACT LED
// goes dark. Cancel (or Esc / the close box) just closes the dialog. Uses the ABI v40
// kapi_shutdown (the SD unmount flushes FatFs, so no file is left half-written).
//
#include "kapi.h"
#include "wtk/wtk.h"

using namespace wtk;

#define W	380
#define H	150

static Label *g_msg;
static Root  *g_root;

static void finish (int mode, const char *text)
{
	g_msg->setText (text);
	g_root->draw (); kapi_present ();
	kapi_msleep (400);				// let the compositor show it
	kapi_shutdown (mode);				// does not return
}
static void on_restart (Widget &)  { finish (SHUTDOWN_RESTART, "Restarting..."); }
static void on_shutdown (Widget &) { finish (SHUTDOWN_HALT, "It is now safe to turn off the Raspberry Pi."); }
static void on_cancel (Widget &)   { kapi_exit (0); }

class ShutRoot : public Root
{
public:
	ShutRoot (int x, int y) : Root (x, y, W, H, "Shut Down", 0) {}
	bool onKey (long k) override { if (k == 27) { kapi_exit (0); return true; } return false; }
};

int main (void)
{
	int sw = 1024, sh = 768;
	kapi_screen_size (&sw, &sh);
	ShutRoot root ((sw - W) / 2, (sh - H) / 2 - 40);
	if (root.canvas.px == 0) return 1;
	g_root = &root;

	root.addChild (new Label (16, 14, W - 32, 18, "Do you want to shut down Onyx?", C_TEXT, root.bg));
	g_msg = new Label (16, 38, W - 32, 18, "Open documents are not saved automatically.", C_DIS, root.bg);
	root.addChild (g_msg);
	root.addChild (new Button (W - 3 * 112, H - 44, 104, 30, "Restart",   on_restart));
	root.addChild (new Button (W - 2 * 112, H - 44, 104, 30, "Shut Down", on_shutdown));
	root.addChild (new Button (W - 1 * 112, H - 44, 104, 30, "Cancel",    on_cancel));
	root.run ();
	return 0;
}
