//
// web/main.cpp -- Web, the WebKit browser's window (roadmap step 1 of docs/08-WEBKIT-PORT.md): the
// toolbar (back, forward, reload / stop, home, the address), the page, the status bar; the menus in the
// Onyx menu bar. One page per window -- no tabs (the user's choice): a link that asks for a new window
// (target=_blank, window.open) starts this program again on it (the kernel shares the program's image).
//
//   web [url]        (no url: the start page, SD:/apps/web.app/start.html)
//
// The page is the engine's (engine.h): WebKit (engine_webkit.cpp) on the Pi, a picture in the desktop
// simulator (engine_mock.cpp). What happens is written to the kernel log (`kmsg`): "web: ..." lines.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (see LICENSE).
//
#include "kapi.h"
#include "wtk/wtk.h"
#include "wtk/toolbar.h"
#include "wtk/paint.h"
#include "wtk/skin.h"
#include "engine.h"
#include <stdio.h>
#include <string.h>

using namespace wtk;

#define W0	1000		// the first size (the window fits the work area)
#define H0	700
#define TB_H	40		// the toolbar
#define SB_H	22		// the status bar
#define HOME	"file:///apps/web.app/start.html"

static void say (const char *s)		// (one write: one line of the kernel log)
{
	char b[600];
	int n = snprintf (b, sizeof b, "web: %s\n", s);
	if (n > (int) sizeof b - 1) n = (int) sizeof b - 1;
	fwrite (b, 1, (size_t) n, stderr);
	fflush (stderr);
}

// ---- the page -------------------------------------------------------------------------------------------

// The page area: the engine paints into its canvas; the pointer and the keys go to the engine.
class PageView : public Widget
{
public:
	int dx, dy, dw, dh;			// what changed since the last paint (dw 0: nothing)
	int buttons;				// the pointer's buttons held, as last seen

	PageView (int l, int t, int w, int h) : Widget (l, t, w, h), dx (0), dy (0), dw (w), dh (h), buttons (0)
	{
		canFocus = true;
		anchor = ANCHOR_FILL;
	}
	void damage (int x, int y, int w, int h)
	{
		if (dw == 0) { dx = x; dy = y; dw = w; dh = h; }
		else
		{
			int x1 = dx + dw > x + w ? dx + dw : x + w, y1 = dy + dh > y + h ? dy + dh : y + h;
			dx = dx < x ? dx : x; dy = dy < y ? dy : y;
			dw = x1 - dx; dh = y1 - dy;
		}
		invalidate (true);
	}
	void onDraw () override
	{
		if (canvas.px == 0) return;
		if (dw == 0) { dx = 0; dy = 0; dw = width; dh = height; }	// (a recomposite: all of it)
		int x = dx < 0 ? 0 : dx, y = dy < 0 ? 0 : dy;
		int w = dx + dw > width ? width - x : dx + dw - x, h = dy + dh > height ? height - y : dy + dh - y;
		if (w > 0 && h > 0) engine_paint (canvas.px, canvas.stride, x, y, w, h);
		dw = 0;
	}
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override
	{
		int b = (bl ? 1 : 0) | (br ? 2 : 0) | (bm ? 4 : 0);
		unsigned mods = kapi_get_modifiers ();
		if (wheel) engine_wheel (mx, my, wheel, mods);
		int changed = b ^ buttons;
		if (changed)
		{
			if (b & ~buttons) setFocus ();
			for (int bit = 1; bit <= 4; bit <<= 1)		// one event a button
				if (changed & bit) engine_mouse (mx, my, (buttons ^ bit) & 7, bit, mods), buttons ^= bit;
		}
		else if (!wheel) engine_mouse (mx, my, b, 0, mods);
		buttons = b;
		return true;
	}
	bool onKey (long k) override
	{
		engine_key (k, kapi_get_modifiers ());
		return true;
	}
};

// ---- the window ------------------------------------------------------------------------------------------

// The address field: a click into it (when it has not the keyboard yet) empties it for a new address, as
// a browser selects it all; Esc gives the page's address back and the keyboard back to the page.
static char g_pageUrl[Textbox::TEXT_CAP];
static void focus_page ();
class UrlField : public Textbox
{
public:
	UrlField (int l, int t, int w, int h, Action enter) : Textbox (l, t, w, h, "", enter) {}
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override
	{
		if (bl && !hasFocus) { setText (""); caret = 0; }
		return Textbox::onMouse (mx, my, bl, br, bm, wheel);
	}
	bool onKey (long k) override
	{
		if (k == 27) { setText (g_pageUrl); focus_page (); return true; }
		return Textbox::onKey (k);
	}
};

static PageView   *g_page;
static UrlField   *g_url;
static Label      *g_status;
static ToolButton *g_back, *g_fwd, *g_reload;
static char        g_title[256];
static char        g_statusText[512];
static bool        g_loading;
static double      g_progress;
static char        g_hover[512];

static void glyph (Canvas &cv, int id, int x, int y, int s, unsigned ink, bool off)
{
	wk_glyph (cv, id, x + s / 2, y + s / 2, s, off ? wk_mix (ink, C_BG, 170) : ink);
}

static void draw_title ();

class WebRoot : public Root
{
public:
	WebRoot () : Root (W0, H0, "Web") {}
	void onTick () override { engine_cycle (); }
	void onResized () override
	{
		engine_resize (g_page->width, g_page->height);
		g_page->damage (0, 0, g_page->width, g_page->height);
		draw_title ();
	}
	void onDrop (int, int, int type, const char *data, int, unsigned) override
	{
		if (type == DND_TEXT) engine_load (data);
		else if (type == DND_FILES)
		{
			char p[512]; int n = 0;
			while (data[n] && data[n] != '\n' && n < (int) sizeof p - 1) { p[n] = data[n]; n++; }
			p[n] = 0;
			engine_load (p);
		}
	}
};
static WebRoot *g_root;

// The window's title: the page's (the frame is drawn by the app: wtk/skin.cpp -- drawn again here with
// the page's title over the kernel's, which stays the program's).
static void draw_title ()
{
	struct kapi_chrome c;
	if (!kapi_get_chrome (&c) || !c.active) return;
	const char *t = g_title[0] ? g_title : "Web";
	wk_draw_frame (c.active, c.chrome_w, c.chrome_h, c.inset_t, t, C_FRAME_ACTIVE, true);
	if (c.inactive) wk_draw_frame (c.inactive, c.chrome_w, c.chrome_h, c.inset_t, t, C_FRAME_INACTIVE, false);
	g_root->invalidate (false);
}

static void show_status ()
{
	char b[512];
	if (g_hover[0]) snprintf (b, sizeof b, "%s", g_hover);
	else if (g_loading) snprintf (b, sizeof b, "Loading... %d %%", (int) (g_progress * 100));
	else snprintf (b, sizeof b, "%s", g_statusText);
	g_status->setText (b);
}

// ---- the engine's calls ------------------------------------------------------------------------------------

static void on_needs_display (int x, int y, int w, int h) { g_page->damage (x, y, w, h); }
// The title's face has Latin-1's letters: the typographic punctuation pages use made plain.
static void plain_title (const char *t, char *o, int cap)
{
	static const struct { const char *u8, *ascii; } map[] = {
		{ "\xE2\x80\x94", " - " }, { "\xE2\x80\x93", "-" }, { "\xE2\x80\x98", "'" }, { "\xE2\x80\x99", "'" },
		{ "\xE2\x80\x9C", "\"" }, { "\xE2\x80\x9D", "\"" }, { "\xE2\x80\xA6", "..." }, { "\xC2\xA0", " " },
		{ "\xE2\x80\xA2", "-" }, { "\xC2\xB7", "-" }, { "\xE2\x80\xAF", " " } };
	int n = 0;
	while (*t && n < cap - 4)
	{
		bool done = false;
		for (unsigned i = 0; i < sizeof map / sizeof map[0] && !done; i++)
		{
			int l = (int) strlen (map[i].u8);
			if (!strncmp (t, map[i].u8, l))
			{
				for (const char *a = map[i].ascii; *a && n < cap - 1; a++) o[n++] = *a;
				t += l; done = true;
			}
		}
		if (!done) o[n++] = *t++;
	}
	o[n] = 0;
}
static void on_title (const char *t)
{
	plain_title (t, g_title, sizeof g_title);
	draw_title ();
}
static void on_url (const char *u)
{
	snprintf (g_pageUrl, sizeof g_pageUrl, "%s", u);
	if (!g_url->hasFocus) g_url->setText (u);
}
static void focus_page () { g_page->setFocus (); g_url->invalidate (true); }
static void on_loading (bool loading, double p)
{
	g_loading = loading; g_progress = p;
	if (!loading) snprintf (g_statusText, sizeof g_statusText, "Done");
	g_reload->setGlyph (WKT_NONE)->setIcon (glyph, loading ? WKG_CLOSE : WKG_RELOAD);
	g_reload->tip = loading ? "Stop (Esc)" : "Reload (Ctrl+R)";
	g_reload->invalidate (true);
	show_status ();
}
static void on_history (bool b, bool f)
{
	g_back->setDisabled (!b);
	g_fwd->setDisabled (!f);
}
static void on_status (const char *s)
{
	snprintf (g_hover, sizeof g_hover, "%s", s);
	show_status ();
}
static void on_open_window (const char *url) { engine_new_window (url); }
static void on_load_failed (const char *url, const char *why)
{
	snprintf (g_statusText, sizeof g_statusText, "Could not load %s: %s", url, why);
	show_status ();
}
static void on_process_ended ()
{
	snprintf (g_statusText, sizeof g_statusText, "The page stopped working: Reload to load it again");
	show_status ();
}
static void on_alert (const char *t) { wk_messagebox ("This page says", t, MB_OK); }
static bool on_confirm (const char *t) { return wk_messagebox ("This page asks", t, MB_OKCANCEL) == 1; }

static void op_back ();
static void op_forward ();
static void op_reload ();
static void op_location ();
static void on_key_not_handled (long k, unsigned mods)
{
	if ((mods & MOD_ALT) && k == KEY_LEFT) op_back ();
	else if ((mods & MOD_ALT) && k == KEY_RIGHT) op_forward ();
	else if (k == KEY_BACKSPACE && !(mods & (MOD_CTRL | MOD_ALT))) op_back ();
	else if (k == 27 && g_loading) engine_stop ();
	else if (k == KEY_F1 + 4) op_reload ();				// F5
	else if (k == KEY_F1 + 5) op_location ();			// F6
}

static const EngineClient s_client = {
	on_needs_display, on_title, on_url, on_loading, on_history, on_status, on_open_window,
	on_load_failed, on_process_ended, on_alert, on_confirm, on_key_not_handled,
};

// ---- the commands ------------------------------------------------------------------------------------------

static void op_back () { engine_back (); g_page->setFocus (); }
static void op_forward () { engine_forward (); g_page->setFocus (); }
static void op_reload () { if (g_loading) engine_stop (); else engine_reload (); }
static void op_home () { engine_load (HOME); g_page->setFocus (); }
static void op_location () { g_url->setText (""); g_url->setFocus (); g_url->caret = 0; g_url->invalidate (true); }
static void op_go (Widget &) { engine_load (g_url->text); g_page->setFocus (); }
static void op_new_window () { engine_new_window (HOME); }
static void op_open_file ()
{
	char p[256];
	if (wk_file_open (p, sizeof p, "SD:/")) engine_load (p);
}
static void op_close () { kapi_exit (0); }
// Edit: the address field's when it has the keyboard, else the page's.
static void op_cut () { if (g_url->hasFocus) g_url->onKey (WK_CTRL ('X')); else engine_command ("Cut"); }
static void op_copy () { if (g_url->hasFocus) g_url->onKey (WK_CTRL ('C')); else engine_command ("Copy"); }
static void op_paste () { if (g_url->hasFocus) g_url->onKey (WK_CTRL ('V')); else engine_command ("Paste"); }
static void op_select_all () { if (g_url->hasFocus) op_location (); else engine_command ("SelectAll"); }
static void op_zoom_in () { engine_zoom (engine_zoom_factor () * 1.1); }
static void op_zoom_out () { engine_zoom (engine_zoom_factor () / 1.1); }
static void op_zoom_reset () { engine_zoom (1); }
static void op_about () { wk_messagebox ("About Web", "Web -- the Onyx browser on WebKit.\nWebKit: LGPL-2.1 (see SD:/docs/licences).", MB_OK); }

static void cb_back (Widget &) { op_back (); }
static void cb_forward (Widget &) { op_forward (); }
static void cb_reload (Widget &) { op_reload (); }
static void cb_home (Widget &) { op_home (); }

static ToolButton *tool (int id, const char *tip, Action cb)
{
	ToolButton *b = (new ToolButton (30, 28, tip, cb))->setIcon (glyph, id);
	b->iconSize = 16;
	return b;
}

int main (int argc, char **argv)
{
	if (engine_is_auxiliary (argc, argv))			// WebKit's web or network process
		return engine_auxiliary_main (argc, argv);

	char url[1024] = "";
	if (argc > 1) snprintf (url, sizeof url, "%s", argv[1]);
	else kapi_get_args (url, sizeof url);
	say (url[0] ? url : "(the start page)");

	WebRoot root;
	g_root = &root;
	if (root.canvas.px == 0) return 1;

	ToolBar *tb = new ToolBar (0, 0, W0, TB_H);
	tb->line = true;
	tb->anchor = ANCHOR_LEFT | ANCHOR_TOP | ANCHOR_RIGHT;
	tb->add (g_back = tool (WKG_CHEV_LEFT, "Back (Alt+Left)", cb_back), 6);
	tb->add (g_fwd = tool (WKG_CHEV_RIGHT, "Forward (Alt+Right)", cb_forward));
	tb->add (g_reload = tool (WKG_RELOAD, "Reload (Ctrl+R)", cb_reload));
	tb->add (tool (WKG_HOME, "Home", cb_home));
	g_back->setDisabled (true);
	g_fwd->setDisabled (true);
	root.addChild (tb);
	int ux = tb->next () + 8;
	g_url = new UrlField (ux, 7, W0 - ux - 10, 26, op_go);
	g_url->maxLen = Textbox::TEXT_CAP - 1;
	g_url->anchor = ANCHOR_LEFT | ANCHOR_TOP | ANCHOR_RIGHT;
	root.addChild (g_url);

	g_page = new PageView (0, TB_H, W0, H0 - TB_H - SB_H);
	root.addChild (g_page);
	g_status = new Label (10, H0 - SB_H + 3, W0 - 20, SB_H - 6, "", C_TEXT, C_BG);
	g_status->anchor = ANCHOR_LEFT | ANCHOR_BOTTOM | ANCHOR_RIGHT;
	root.addChild (g_status);

	static Menu menu;
	menu.menu ("File");
	menu.item ("New Window", "^N", WK_CTRL ('N'), op_new_window);
	menu.item ("Open File...", "^O", WK_CTRL ('O'), op_open_file);
	menu.item ("Open Location", "^L", WK_CTRL ('L'), op_location);
	menu.separator ();
	menu.item ("Close Window", "^W", WK_CTRL ('W'), op_close);
	menu.menu ("Edit");
	menu.item ("Cut", "^X", WK_CTRL ('X'), op_cut);
	menu.item ("Copy", "^C", WK_CTRL ('C'), op_copy);
	menu.item ("Paste", "^V", WK_CTRL ('V'), op_paste);
	menu.item ("Select All", "^A", WK_CTRL ('A'), op_select_all);
	menu.menu ("View");
	menu.item ("Reload", "^R", WK_CTRL ('R'), op_reload);
	menu.separator ();
	menu.item ("Zoom In", "", 0, op_zoom_in);
	menu.item ("Zoom Out", "", 0, op_zoom_out);
	menu.item ("Actual Size", "", 0, op_zoom_reset);
	menu.menu ("Go");
	menu.item ("Back", "Alt+Left", 0, op_back);
	menu.item ("Forward", "Alt+Right", 0, op_forward);
	menu.item ("Home", "", 0, op_home);
	menu.menu ("Help");
	menu.item ("About Web", "", 0, op_about);
	menu.publish ();

	root.setResizable (true);
	root.fitWorkArea ();
	if (!engine_init (&s_client, g_page->width, g_page->height))
	{
		wk_messagebox ("Web", "The web engine could not start (see kmsg).", MB_OK);
		return 1;
	}
	engine_load (url[0] ? url : HOME);
	g_page->setFocus ();
	draw_title ();
	root.run ();
	say ("the window closes");
	return 0;
}
