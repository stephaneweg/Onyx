//
// web/webview.cpp -- Web's web view (roadmap step 5 of docs/08-WEBKIT-PORT.md): the page alone, shown in
// another app's window -- Mail's HTML messages. The browser's one program started by the host as
//
//   web --applet <surface id> <host pid> <host's IPC service>
//
// main () hands that run here. A uikit Root in the applet mode (uikit/root.cpp: it adopts the host's surface,
// takes the pointer and the keys from the host, tells it what it drew) filled with the page; JavaScript
// off; a clicked link not followed but told to the host; the host's own messages (webview_proto.h): the
// page's size, the HTML to show (a file), an address to load. What happens goes to the kernel log
// ("webview: ..." lines).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (see LICENSE).
//
#include "appkit/appkit.h"
#include "uikit/uikit.h"
#include "engine.h"
#include "webview_proto.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace uikit;

static void say (const char *s)		// (one write: one line of the kernel log)
{
	char b[600];
	int n = snprintf (b, sizeof b, "webview: %s\n", s);
	if (n > (int) sizeof b - 1) n = (int) sizeof b - 1;
	fwrite (b, 1, (size_t) n, stderr);
	fflush (stderr);
}

// A string to the host (cut to a mailbox's 512 bytes).
static void tell (int type, const char *s)
{
	char b[512];
	snprintf (b, sizeof b, "%s", s ? s : "");
	uk_applet_send (type, b, (unsigned) strlen (b) + 1);
}

// ---- the page (as the browser's PageView, main.cpp) ---------------------------------------------------------
class ViewPage : public Widget
{
public:
	int dx, dy, dw, dh;			// what changed since the last paint (dw 0: nothing)
	int buttons;				// the pointer's buttons held, as last seen

	ViewPage (int l, int t, int w, int h) : Widget (l, t, w, h), dx (0), dy (0), dw (w), dh (h), buttons (0)
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
		if (mx < 0) { engine_mouse_leave (); buttons = 0; return false; }	// (it left the host's box)
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
		if (k == UK_CTRL ('C')) { engine_command ("Copy"); return true; }	// (no menu here: the host has it)
		if (k == UK_CTRL ('A')) { engine_command ("SelectAll"); return true; }
		engine_key (k, kapi_get_modifiers ());
		return true;
	}
};

static ViewPage *g_page;
static Root     *g_root;
static int       g_maxW, g_maxH;		// the surface's size: the page's at most

class ViewRoot : public Root
{
public:
	ViewRoot () : Root (640, 480, "Web view") {}	// (an applet: the surface's size, whatever is asked)
	void onTick () override { engine_cycle (); }
};

// ---- the engine's calls -------------------------------------------------------------------------------------------
static void on_needs_display (int x, int y, int w, int h) { g_page->damage (x, y, w, h); }
static void on_title (const char *) {}
static void on_url (const char *) {}
static void on_loading (bool loading, double) { if (!loading) uk_applet_send (WV_LOADED); }
static void on_history (bool, bool) {}
static void on_status (const char *s) { tell (WV_STATUS, s); }
static void on_open_window (const char *url) { tell (WV_LINK, url); }	// (target=_blank: the host's too)
static void on_load_failed (const char *url, const char *why)
{
	char b[560]; snprintf (b, sizeof b, "could not load %.300s: %.200s", url, why);
	say (b);
}
static void on_process_ended () { say ("the web process ended"); uk_applet_send (WV_ENDED); }
static void on_alert (const char *) {}				// (no scripts)
static bool on_confirm (const char *) { return false; }
static void on_key_not_handled (long, unsigned) {}

static const EngineClient s_client = {
	on_needs_display, on_title, on_url, on_loading, on_history, on_status, on_open_window,
	on_load_failed, on_process_ended, on_alert, on_confirm, on_key_not_handled,
};

static void on_link (const char *url) { tell (WV_LINK, url); }

// ---- the host's messages ----------------------------------------------------------------------------------------
static char *read_file (const char *path)
{
	void *f = kapi_open (path);
	if (f == 0) return 0;
	unsigned cap = 1 << 16, n = 0;
	char *b = (char *) malloc (cap + 1);
	while (b)
	{
		if (n == cap)
		{
			char *nb = (char *) realloc (b, cap * 2 + 1);
			if (nb == 0) { free (b); b = 0; break; }
			b = nb; cap *= 2;
		}
		int k = kapi_read (f, b + n, cap - n);
		if (k <= 0) break;
		n += (unsigned) k;
	}
	kapi_close (f);
	if (b) b[n] = 0;
	return b;
}

static void set_size (int w, int h)
{
	if (w < 1) w = 1;
	if (h < 1) h = 1;
	if (w > g_maxW) w = g_maxW;
	if (h > g_maxH) h = g_maxH;
	if (w == g_root->width && h == g_root->height) return;
	g_root->setBounds (w, h);			// (the surface kept: its top-left w x h; the page follows: ANCHOR_FILL)
	engine_resize (g_page->width, g_page->height);
	char b[80]; snprintf (b, sizeof b, "size %d x %d (the page %d x %d)", w, h, g_page->width, g_page->height);
	say (b);
	g_page->damage (0, 0, g_page->width, g_page->height);
}

static void on_host (int type, const void *data, int len)
{
	const char *s = (const char *) data;
	switch (type)
	{
	case WV_SIZE:
		if (len >= (int) (2 * sizeof (int))) { int wh[2]; memcpy (wh, data, sizeof wh); set_size (wh[0], wh[1]); }
		break;
	case WV_HTML:
	{
		char *html = read_file (s);
		if (html == 0) { char b[300]; snprintf (b, sizeof b, "cannot read %.256s", s); say (b); break; }
		engine_load_html (html, "");
		free (html);
		break;
	}
	case WV_URL: engine_load (s); break;
	case WV_COMMAND: engine_command (s); break;
	default: break;					// (WV_PING: nothing to do)
	}
}

// ---- the view ---------------------------------------------------------------------------------------------------
int webview_main (int argc, char **argv)
{
	(void) argc; (void) argv;
	if (!uk_applet ())
	{
		say ("not started by a host (--applet <surface> <host pid> <host's service>)");
		return 1;
	}
	ViewRoot root;
	g_root = &root;
	if (root.canvas.px == 0) return 1;
	g_maxW = root.width; g_maxH = root.height;
	g_page = new ViewPage (0, 0, root.width, root.height);
	root.addChild (g_page);
	uk_applet_on_message (on_host);
	engine_set_scripts (false);
	engine_set_link_handler (on_link);
	if (!engine_init (&s_client, g_page->width, g_page->height))
	{
		say ("the web engine could not start (see kmsg)");
		uk_applet_send (WV_ENDED);
		return 1;
	}
	say ("up");
	g_page->setFocus ();
	root.run ();
	say ("the host closed the view");
	return 0;
}
