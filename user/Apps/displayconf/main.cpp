//
// displayconf -- the Control Panel's Display applet (applet_proto.h; alone, a window of its own): the
// screen's resolution. A size picked in the list and Apply: changed at once (kapi v66 screen_set --
// every window kept on the screen and sent GUI_EVENT_DISPLAY_RESIZE: the menu bar, the dock, the
// notifications place themselves again, a maximised window fills the new work area, one too big is
// shrunk into it) and the wallpaper painted again at the new size (apps/voronoy, as the Theme
// applet); kept in SD:/cmdline.txt (width= / height=) for the next start -- once "Keep this resolution?" was
// answered (15 s, else the size before comes back by itself). An older kernel: kept
// only, applied at the next start. The monitor shows any size (the firmware scales the picture to
// its own mode); its native one is the sharpest.
// On a Pi 5 (kernel_info's "board pi5"): the alpha fix (SD:/cmdline.txt opaque=): the kernel makes the screen's
// pixels opaque, for the bootloaders that do not honour framebuffer_ignore_alpha (the screen black but the boot
// log); off (opaque=0) with a recent bootloader -- a little faster, and the games' direct full-screen mode back.
//
#include "appkit/appkit.h"
#include "uikit/uikit.h"
#include "systemkit/systemkit.h"		// display.h: the sizes, cmdline.txt
#include <stdio.h>
#include <string.h>
#include "fontkit/uikitface.h"		// FreeType's text (DejaVu Sans) for every widget

using namespace uikit;

#define W	700
#define H	470

// The sizes: SystemKit's display.h (display_modes, display_mode). Their words, translated here:
// TR: 4:3, XGA (Onyx's default)
// TR: 16:9, HD
// TR: 16:10, WXGA
// TR: 5:4, SXGA
// TR: 16:9, laptop screens
// TR: 16:10, WXGA+
// TR: 16:9, HD+
// TR: 4:3, UXGA
// TR: 16:10, WSXGA+
// TR: 16:9, Full HD
// TR: 16:10, WUXGA
// TR: 16:9, QHD
#define NMODES	display_modes ()

static ListBox *g_list;
static Root *g_root;
static Label   *g_now, *g_status;

static int put_str (char *b, int n, const char *s) { while (*s) b[n++] = *s++; return n; }
static int put_int (char *b, int n, int v) { return n + ax_itoa (v, b + n); }

// The Pi 5's alpha fix: checked = on (no opaque= line, the default), unchecked = opaque=0 -- at the next start.
static bool on_pi5 (void)
{
	char t[512];
	return kapi_kernel_info (t, sizeof t) > 0 && strstr (t, "\nboard pi5\n") != 0;
}
static void on_alpha (Widget &w)
{
	bool on = ((Checkbox &) w).checked;
	bool ok = display_save_option ("opaque", on ? 0 : "0") != 0;
	g_status->setText (!ok ? TR ("SD:/cmdline.txt could not be written.")
			       : on ? TR ("The alpha fix: on at the next start.") : TR ("The alpha fix: off at the next start (a recent bootloader)."));
}

static void show_now (void)
{
	int w = 0, h = 0; kapi_screen_size (&w, &h);
	char s[64]; int n = put_str (s, 0, TR ("The screen now: "));
	n = put_int (s, n, w); n = put_str (s, n, " x "); n = put_int (s, n, h); s[n] = 0;
	g_now->setText (s);
	for (int i = 0; i < NMODES; i++) { int mw, mh; display_mode (i, &mw, &mh); if (mw == w && mh == h) g_list->setSel (i); }
}

// "Keep this resolution?": 15 s to say Keep, else (Esc, Go back, nothing) the size before comes back by itself --
// a size the monitor cannot show leaves a black screen: it mends itself (2026-10-09, as the console's Display page).
#define KEEP_TICKS	1500
class KeepBox : public Modal
{
	unsigned m_t0;
public:
	KeepBox () : Modal (420, 150)
	{
		Root *r = Root::current ();
		left = ((r ? r->width : W) - width) / 2; top = ((r ? r->height : H) - height) / 2;
		m_t0 = kapi_get_ticks ();
		Button *k = new Button (width - 196, height - 40, 90, 28, TR ("Keep"), btn); k->tag = 1; addChild (k);
		Button *g = new Button (width - 100, height - 40, 90, 28, TR ("Go back"), btn); g->tag = 0; addChild (g);
	}
	static void btn (Widget &w) { if (w.parent) ((Modal *) w.parent)->onButton (w.tag); }
	void onButton (int tag) override { close (tag); }
	bool onKey (long k) override
	{
		if (k == 27) { close (0); return true; }
		if (k == KEY_ENTER) { close (1); return true; }
		return false;
	}
	void onDraw () override
	{
		unsigned el = kapi_get_ticks () - m_t0;
		if (el >= KEEP_TICKS) { close (0); return; }
		drawBox (TR ("Display"));
		char s[96]; int left_s = (int) ((KEEP_TICKS - el + 99) / 100);
		snprintf (s, sizeof s, TR ("Keep this resolution? Back to the one before in %d s."), left_s);
		uk_text (canvas, 16, titleH () + 16, s, C_TEXT);
		int bw = width - 32;
		uk_sunken (canvas, 16, titleH () + 44, bw, 8, 3, C_FIELD);
		int f = (int) ((long) bw * (KEEP_TICKS - el) / KEEP_TICKS);
		if (f > 4) uk_rbox (canvas, 17, titleH () + 45, f - 2, 6, 2, C_ACCENT, C_ACCENT);
		invalidate (true);					// (the count goes on: drawn again)
	}
};

static void on_apply (Widget &)
{
	int i = g_list->sel;
	if (i < 0 || i >= NMODES) { g_status->setText (TR ("Pick a size in the list first.")); return; }
	int w, h;
	display_mode (i, &w, &h);
	int ow = 0, oh = 0; kapi_screen_size (&ow, &oh);
	int r = kapi_screen_set (w, h);
	if (r == 0)
	{
		g_root->draw (); uk_present ();
		KeepBox box;
		if (box.run () != 1)					// (not kept: the size before)
		{
			kapi_screen_set (ow, oh);
			g_status->setText (TR ("The resolution before is back."));
			show_now ();
			return;
		}
		bool saved = display_save_size (w, h) != 0;
		kapi_exec ("SD:apps/voronoy.app/main", "");	// the wallpaper, at the new size
		g_status->setText (saved ? TR ("Applied, and kept for the next start.") : TR ("Applied -- but SD:/cmdline.txt could not be written."));
	}
	else if (r == -4) { bool saved = display_save_size (w, h) != 0; g_status->setText (saved ? TR ("Kept: applied at the next start (this kernel cannot change it now).") : TR ("SD:/cmdline.txt could not be written.")); }
	else if (r == -2) { display_save_size (w, h); g_status->setText (TR ("Not now: a full-screen app owns the display. Kept for the next start.")); }
	else if (r == -3) g_status->setText (TR ("The firmware refused this size: the screen kept its own."));
	else g_status->setText (TR ("This size is not possible."));
	show_now ();
}

class DisplayRoot : public Root
{
public:
	DisplayRoot () : Root (W, H, TR ("Display")) {}
	void onDisplayResize (int, int) override { show_now (); }
};

int main (void)
{
	ft_uikit_install ("DejaVu Sans", 13);		// (before the widgets; false: the bitmap font)
	uk_lang_init ();				// the words in the system's language (the face first: UTF-8)
	DisplayRoot root;
	if (root.canvas.px == 0) return 1;
	g_root = &root;
	int X = root.width > W ? (root.width - W) / 2 : 0;
	GroupBox *gs = new GroupBox (X + 10, 8, W - 20, 330, TR ("Resolution"));
	root.addChild (gs);
	int ct = gs->contentTop () + 6;
	g_now = new Label (14, ct, W - 60, 20, "", C_TEXT, gs->bg); gs->addChild (g_now);
	g_list = new ListBox (14, ct + 30, 330, 240, 0, on_apply);
	for (int i = 0; i < NMODES; i++)
	{
		int mw, mh; const char *what = display_mode (i, &mw, &mh);
		char s[64]; int n = put_int (s, 0, mw); n = put_str (s, n, " x "); n = put_int (s, n, mh);
		while (n < 13) s[n++] = ' ';
		n = put_str (s, n, TR (what)); s[n] = 0;
		g_list->add (s);
	}
	gs->addChild (g_list);
	gs->addChild (new Button (360, ct + 30, 150, 30, TR ("Apply"), on_apply));
	gs->addChild (new Label (360, ct + 74, 300, 20, TR ("At once: the desktop and the"), C_DIS, gs->bg));
	gs->addChild (new Label (360, ct + 94, 300, 20, TR ("windows follow the new size."), C_DIS, gs->bg));
	gs->addChild (new Label (360, ct + 124, 300, 20, TR ("The monitor's own resolution"), C_DIS, gs->bg));
	gs->addChild (new Label (360, ct + 144, 300, 20, TR ("is the sharpest."), C_DIS, gs->bg));

	g_status = new Label (X + 12, 346, W - 24, 22, "", C_TEXT, root.bg);
	root.addChild (g_status);
	if (on_pi5 ())
	{
		char v[8]; bool off = display_saved_option ("opaque", v, sizeof v) > 0 && v[0] == '0';
		root.addChild (new Checkbox (X + 12, 374, W - 24, 24, TR ("Alpha fix: opaque pixels (older Pi 5 bootloaders)"), !off,
					     on_alpha, root.bg));
	}
	root.addChild (new Label (X + 12, H - 60, W - 24, 20, TR ("Kept in SD:/cmdline.txt (width= / height=), read at the start."), C_DIS, root.bg));
	show_now ();
	root.run ();
	return 0;
}
