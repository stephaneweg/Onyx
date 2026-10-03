//
// Apps/mail/webview.h -- an HTML message shown by WebKit: Web's web view (Apps/web/webview.cpp, the browser's one
// program, SD:/apps/web.app/main, run as an applet: applet_proto.h, Apps/web/webview_proto.h) draws the message's
// body into a surface Mail shows in its reading pane, where its own renderer (mail/html.h) draws it otherwise. Mail
// does not link WebKit: one process more, started at the first HTML message, kept while Mail runs (the kernel
// shares the program's image with the browser's windows).
//
// The HTML given to the view is the message's own, with a Content-Security-Policy first: nothing from the internet
// (pictures, styles, fonts: tracking) until the user asks ("Show the pictures"), then https: / http: pictures, styles
// and fonts; never scripts (off in the view too), frames, objects. The cid: pictures (inline attachments) are put in
// as data: URLs. A link clicked comes back to Mail (WV_LINK: its own checks: mailto: writes, http(s): asked, then Web).
//
// When the program is not on the card, when Mail cannot be an IPC service (the desktop simulator), when the view does
// not start or its engine ends: Mail's own renderer, as before.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
//
#ifndef _mail_webview_h
#define _mail_webview_h

#include "Apps/mail/app.h"
#include "applet_proto.h"
#include "Apps/web/webview_proto.h"

namespace mailapp {

static void wv_repaint ();			// read.h: the reading pane drawn again
static void wv_link (const char *url);		// read.h: a link clicked in the view

enum { WS_UNKNOWN, WS_OFF, WS_IDLE, WS_STARTING, WS_UP };
struct WebView
{
	int state;
	char service[24];			// Mail's IPC service (the view ends when it is gone)
	int self;				// our pid
	int sid; unsigned *spx; int sw, sh;	// the surface (made once: the work area's size)
	int pid;				// the view's process (0: not up yet)
	unsigned t0, pingT, sentT;
	int w, h;				// the page's size told
	char key[240];				// the message shown (account, folder, uid, remote allowed)
	bool loaded;				// its page drawn since it was given
	char path[64];				// the HTML handed over (RAM:)
	char hover[512];			// the link under the pointer
	// in the reading pane (its coordinates), as drawn last: the box, its visible height
	bool shown; int bx, by, bw, bh, clipH;
	int ptrButtons, prevBtn; bool inBox, focus;
};
static WebView g_wv;

// The view can be used (its program on the card, Mail an IPC service) -- asked once.
static bool wv_usable ()
{
	if (g_wv.state != WS_UNKNOWN) return g_wv.state != WS_OFF;
	g_wv.state = WS_OFF;
	void *f = kapi_open (WV_PROGRAM);
	if (!f) return false;
	kapi_close (f);
	bool reg = false;					// ("mail-1", or the next free: one per Mail running)
	for (int i = 1; i <= 8 && !reg; i++)
	{
		snprintf (g_wv.service, sizeof g_wv.service, "mail-%d", i);
		reg = kapi_ipc_register (g_wv.service) != 0;
	}
	if (!reg) return false;					// (the simulator: no; the table full)
	g_wv.self = kapi_ipc_lookup (g_wv.service);
	if (g_wv.self <= 0) return false;
	snprintf (g_wv.path, sizeof g_wv.path, "RAM:/mailview-%d.html", g_wv.self);
	g_wv.state = WS_IDLE;
	return true;
}

static void wv_fail (const char *why)
{
	if (g_wv.pid > 0) kapi_mailbox_send (g_wv.pid, AP_CLOSE, 0, 0);
	g_wv.pid = 0; g_wv.state = WS_OFF; g_wv.shown = false;
	fprintf (stderr, "mail: the web view: %s -- Mail's own renderer\n", why);
	wv_repaint ();
}

// The view started (the surface made at the first use).
static void wv_start ()
{
	if (g_wv.state != WS_IDLE) return;
	if (g_wv.sid <= 0)
	{
		int w = 1920, h = 1080;
		struct kapi_win_geom g;
		if (kapi_win_geometry (&g) == 0 && g.aw > 0 && g.ah > 0) { w = g.aw; h = g.ah; }
		g_wv.sid = kapi_surface_create (w, h);
		g_wv.spx = g_wv.sid > 0 ? kapi_surface_map (g_wv.sid) : 0;
		if (!g_wv.spx || !kapi_surface_size (g_wv.sid, &g_wv.sw, &g_wv.sh) || g_wv.sw <= 0 || g_wv.sh <= 0) { g_wv.sid = 0; wv_fail ("no surface"); return; }
	}
	char args[96]; snprintf (args, sizeof args, "--applet %d %d %s", g_wv.sid, g_wv.self, g_wv.service);
	if (!kapi_exec_as (WV_PROGRAM, args, "webview")) { wv_fail ("not started"); return; }
	g_wv.state = WS_STARTING; g_wv.pid = 0; g_wv.t0 = kapi_get_ticks ();
	g_wv.w = g_wv.h = 0; g_wv.key[0] = 0; g_wv.loaded = false;
}

// ---- the HTML handed over -----------------------------------------------------------------------------------------------------
// (the CSP: no scripts, nothing from the internet unless allowed)
static const char *wv_policy (bool remote)
{
	return remote ? "default-src 'none'; img-src data: cid: https: http:; style-src 'unsafe-inline' https: http:; font-src data: https: http:"
		      : "default-src 'none'; img-src data: cid:; style-src 'unsafe-inline'; font-src data:";
}

// Content from the internet in this HTML? (pictures, backgrounds, styles: what the policy holds back)
static bool wv_has_remote (const char *h, int n)
{
	static const char *const KEYS[] = { "src=", "background=", "url(", "srcset=", "poster=" };
	for (int i = 0; i < n; i++)
	{
		for (unsigned k = 0; k < sizeof KEYS / sizeof KEYS[0]; k++)
		{
			int l = (int) strlen (KEYS[k]);
			if (i + l > n || !ieqn (h + i, KEYS[k], l)) continue;
			int j = i + l;
			while (j < n && (h[j] == ' ' || h[j] == '"' || h[j] == '\'' || h[j] == '\t' || h[j] == '\n')) j++;
			if ((j + 7 <= n && ieqn (h + j, "http://", 7)) || (j + 8 <= n && ieqn (h + j, "https://", 8)) || (j + 2 <= n && h[j] == '/' && h[j + 1] == '/')) return true;
		}
		if (i + 5 <= n && ieqn (h + i, "<link", 5))		// (a style sheet from the internet)
		{
			int e = i; while (e < n && h[e] != '>') e++;
			for (int j = i; j + 4 <= e; j++) if (ieqn (h + j, "http", 4) || (h[j] == '/' && h[j + 1] == '/')) return true;
		}
		if (i + 7 <= n && ieqn (h + i, "@import", 7)) return true;
	}
	return false;
}

// a cid: picture as a data: URL (false: not in the message)
static bool wv_cid_data (const Mime &m, const char *cid, Buf &out)
{
	int k = m.by_cid (cid);
	if (k < 0)
	{	// (written %-encoded in the HTML: "%40" for '@')
		char d[160]; int n = 0;
		for (const char *p = cid; *p && n < (int) sizeof d - 1; p++)
		{
			if (p[0] == '%' && isxdigit ((unsigned char) p[1]) && isxdigit ((unsigned char) p[2]))
			{ char hx[3] = { p[1], p[2], 0 }; d[n++] = (char) strtol (hx, 0, 16); p += 2; }
			else d[n++] = *p;
		}
		d[n] = 0;
		k = m.by_cid (d);
	}
	if (k < 0) return false;
	const Part &p = m.parts[k];
	Buf b; m.decoded (p, b);
	out.add ("data:"); out.add (p.type[0] ? p.type : "application"); out.addc ('/'); out.add (p.sub[0] ? p.sub : "octet-stream");
	out.add (";base64,");
	b64_encode (out, b.c (), b.n, 0);
	return true;
}

// The message's HTML as the view gets it: after its <!DOCTYPE> (kept: the page's mode), the charset and the policy;
// its cid: pictures put in.
static void wv_build (const Mime &m, int part, bool remote, Buf &o)
{
	Buf t; m.text (m.parts[part], t);
	const char *h = t.c (); int n = t.n, i = 0;
	if (n >= 3 && (unsigned char) h[0] == 0xEF && (unsigned char) h[1] == 0xBB && (unsigned char) h[2] == 0xBF) i = 3;
	int s = i; while (s < n && (h[s] == ' ' || h[s] == '\t' || h[s] == '\r' || h[s] == '\n')) s++;
	if (s + 9 <= n && ieqn (h + s, "<!doctype", 9))
	{
		int e = s; while (e < n && h[e] != '>') e++;
		if (e < n) e++;
		o.add (h + s, e - s);
		i = e;
	}
	o.add ("<meta charset=\"utf-8\"><meta http-equiv=\"Content-Security-Policy\" content=\"");
	o.add (wv_policy (remote));
	o.add ("\">");
	while (i < n)
	{
		if (i + 4 <= n && ieqn (h + i, "cid:", 4) && i > 0 && strchr ("\"'( =", h[i - 1]))
		{
			int j = i + 4; char cid[160]; int k = 0;
			while (j < n && !strchr ("\"'() <>\t\r\n", h[j])) { if (k < (int) sizeof cid - 1) cid[k++] = h[j]; j++; }
			cid[k] = 0;
			if (k && wv_cid_data (m, cid, o)) { i = j; continue; }
		}
		o.addc (h[i++]);
	}
}

// ---- in the reading pane -----------------------------------------------------------------------------------------------------
// The message given to the view (once; again when the pictures are allowed): its HTML written to RAM:, its path sent.
static void wv_give (const char *key, const Mime &m, int part, bool remote)
{
	if (g_wv.state == WS_IDLE) wv_start ();
	if (g_wv.state != WS_UP || !strcmp (g_wv.key, key)) return;
	Buf o; wv_build (m, part, remote, o);
	if (kapi_save_file (g_wv.path, o.c (), (unsigned) o.n) < 0) { wv_fail ("the HTML could not be written"); return; }
	if (!kapi_mailbox_send (g_wv.pid, WV_HTML, g_wv.path, (unsigned) strlen (g_wv.path) + 1)) { wv_fail ("it ended"); return; }
	scpy (g_wv.key, key, sizeof g_wv.key);
	g_wv.loaded = false; g_wv.sentT = kapi_get_ticks (); g_wv.hover[0] = 0;
}

// The box (pane coordinates x, y, w x h; visible above clipH): the page's size told, its pixels copied.
static void wv_draw (Canvas &cv, int x, int y, int w, int h, int clipH)
{
	if (g_wv.sh > 0 && h > g_wv.sh) h = g_wv.sh;
	if (g_wv.sw > 0 && w > g_wv.sw) w = g_wv.sw;
	g_wv.shown = true; g_wv.bx = x; g_wv.by = y; g_wv.bw = w; g_wv.bh = h; g_wv.clipH = clipH;
	if (g_wv.state == WS_UP && (w != g_wv.w || h != g_wv.h))
	{
		int wh[2] = { w, h };
		if (kapi_mailbox_send (g_wv.pid, WV_SIZE, wh, sizeof wh)) { g_wv.w = w; g_wv.h = h; }
	}
	int y0 = y < 0 ? 0 : y, y1 = y + h < clipH ? y + h : clipH;
	if (y1 <= y0) return;
	bool pixels = g_wv.state == WS_UP && g_wv.loaded && g_wv.w == w && g_wv.h == h;
	if (!pixels)
	{
		cv.fillRect (x, y0, w, y1 - y0, 0xFFFFFF);
		if (y + 40 >= y0 && y + 40 < y1) text_c (cv, x, y + 20, w, 40, g_wv.state == WS_STARTING ? "Opening the message..." : "", col_dim ());
		return;
	}
	for (int yy = y0; yy < y1; yy++)
	{
		unsigned *d = cv.px + (long) yy * cv.stride + x;
		const unsigned *s = g_wv.spx + (long) (yy - y) * g_wv.sw;
		for (int i = 0; i < w; i++) d[i] = s[i] & 0x00FFFFFFu;
	}
	if (g_wv.hover[0] && g_wv.inBox)				// the link under the pointer: where it goes
	{
		int hw = tw (g_wv.hover, F_SMALL) + 16; if (hw > w - 8) hw = w - 8;
		int hy = (y + h < clipH ? y + h : clipH) - 26;
		if (hy > y0)
		{
			wk_fill_round (cv, x + 4, hy, hw, 22, 4, col_line ());
			wk_fill_round (cv, x + 5, hy + 1, hw - 2, 20, 4, C_FIELD);
			text_v (cv, x + 12, hy, 22, g_wv.hover, C_FIELD_TEXT, F_SMALL, 0, hw - 16);
		}
	}
}

static void wv_send_ptr (int ev, int x, int y, int buttons, int changed, int wheel)
{
	ApPtr p = { ev, x, y, buttons, changed, wheel };
	kapi_mailbox_send (g_wv.pid, AP_PTR, &p, sizeof p);
}

// The pointer (pane coordinates): the view's when over its box (a press begun there stays with it) -> taken
static bool wv_mouse (int mx, int my, int bl, int br, int bm, int wheel)
{
	int btn = (bl ? 1 : 0) | (br ? 2 : 0) | (bm ? 4 : 0);
	int prev = g_wv.prevBtn; g_wv.prevBtn = btn;
	bool up = g_wv.state == WS_UP && g_wv.shown && g_wv.loaded;
	bool inside = up && mx >= g_wv.bx && mx < g_wv.bx + g_wv.bw && my >= g_wv.by && my < g_wv.by + g_wv.bh && my >= 0 && my < g_wv.clipH;
	if (up && ((inside && !(btn && !g_wv.ptrButtons && prev)) || g_wv.ptrButtons))
	{
		int x = mx - g_wv.bx, y = my - g_wv.by;
		if (!g_wv.inBox) { g_wv.inBox = true; wv_send_ptr (GUI_EVENT_PTR_ENTER, x, y, btn, 0, 0); }
		if (wheel) wv_send_ptr (GUI_EVENT_PTR_WHEEL, x, y, btn, 0, wheel);
		else if (btn != g_wv.ptrButtons)
		{
			for (int b = 1; b <= 4; b <<= 1)
				if ((btn ^ g_wv.ptrButtons) & b) wv_send_ptr ((btn & b) ? GUI_EVENT_PTR_DOWN : GUI_EVENT_PTR_UP, x, y, btn, b, 0);
			if (btn & ~g_wv.ptrButtons) g_wv.focus = true;
		}
		else wv_send_ptr (GUI_EVENT_PTR_MOVE, x, y, btn, 0, 0);
		g_wv.ptrButtons = btn;
		return true;
	}
	if (g_wv.inBox && g_wv.pid > 0) { wv_send_ptr (GUI_EVENT_PTR_LEAVE, -1, -1, 0, 0, 0); if (g_wv.hover[0]) { g_wv.hover[0] = 0; wv_repaint (); } }
	g_wv.inBox = false; g_wv.ptrButtons = 0;
	if (btn && !prev) g_wv.focus = false;			// (a click elsewhere: the keys are Mail's again)
	return false;
}

// A key for the page (it was clicked last) -> taken
static bool wv_key (long k)
{
	if (!g_wv.focus || !g_wv.shown || g_wv.state != WS_UP) return false;
	ApKey key = { (int) k, kapi_get_modifiers () };
	kapi_mailbox_send (g_wv.pid, AP_KEY, &key, sizeof key);
	return true;
}

// Each frame (Mail's onTick): the view's messages; its start awaited; is it still there?
static void wv_tick ()
{
	if (g_wv.state < WS_STARTING) return;
	int from = 0, type = 0, n;
	char buf[513];
	char link[512]; link[0] = 0;
	while ((n = kapi_mailbox_recv (&from, &type, buf, sizeof buf - 1, 0)) >= 0)
	{
		if (n > (int) sizeof buf - 1) n = (int) sizeof buf - 1;
		buf[n] = 0;
		if (type == AP_HELLO && g_wv.state == WS_STARTING) { g_wv.pid = from; g_wv.state = WS_UP; g_wv.pingT = kapi_get_ticks (); wv_repaint (); continue; }
		if (from != g_wv.pid || g_wv.pid <= 0) continue;
		if (type == AP_PRESENT) { if (g_wv.shown) wv_repaint (); }
		else if (type == WV_LOADED) { g_wv.loaded = true; wv_repaint (); }
		else if (type == WV_STATUS) { scpy (g_wv.hover, buf, sizeof g_wv.hover); wv_repaint (); }
		else if (type == WV_LINK) scpy (link, buf, sizeof link);
		else if (type == WV_ENDED) { wv_fail ("its engine ended"); return; }
		else if (type == AP_EXIT) { g_wv.pid = 0; wv_fail ("it ended"); return; }
	}
	unsigned now = kapi_get_ticks ();
	if (g_wv.state == WS_STARTING && now - g_wv.t0 > 2500) { wv_fail ("it did not start (25 s)"); return; }
	if (g_wv.state == WS_UP)
	{
		if (!g_wv.loaded && g_wv.key[0] && now - g_wv.sentT > 300) { g_wv.loaded = true; wv_repaint (); }	// (3 s: shown as it is)
		if (now - g_wv.pingT > 100)
		{
			g_wv.pingT = now;
			if (!kapi_mailbox_send (g_wv.pid, WV_PING, 0, 0)) { g_wv.pid = 0; wv_fail ("it is gone"); return; }
		}
	}
	if (link[0]) wv_link (link);
}

// Mail ends: the view too (it would by itself: our service gone), the HTML handed over removed.
static void wv_end ()
{
	if (g_wv.pid > 0) kapi_mailbox_send (g_wv.pid, AP_CLOSE, 0, 0);
	if (g_wv.path[0]) kapi_remove (g_wv.path);
}

} // namespace mailapp

#endif
