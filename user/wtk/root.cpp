#include "wtk/root.h"
#include "wtk/menu.h"		// Menu::shortcut (keys go through the menu first)
#include "wtk/skin.h"		// wk_decorate_window
#include "wtk/font.h"		// wtk::init (load the global font family at startup)
#include "wtk/dialog.h"		// PopupMenu (the window menu)
#include "applib.h"		// should_exit, msleep, pump_events
#include "applet_proto.h"	// the applet mode (a Control Panel applet)

namespace wtk {

// ---- the applet mode ---------------------------------------------------------------------------
static int s_apSurf = -1;			// -1: not asked yet, 0: not an applet
static int s_apHost, s_apW, s_apH;
static unsigned *s_apPx;
static bool s_apClosed, s_apBye;
static unsigned s_apCheck;

static int ap_num (const char *&p)
{
	while (*p == ' ') p++;
	int v = 0; bool any = false;
	while (*p >= '0' && *p <= '9') { v = v * 10 + (*p++ - '0'); any = true; }
	return any ? v : -1;
}

bool wk_applet ()
{
	if (s_apSurf < 0)
	{
		s_apSurf = 0;
		char a[96]; a[0] = 0;
		kapi_get_args (a, sizeof a);
		const char *key = "--applet ";
		int k = 0; while (key[k] && a[k] == key[k]) k++;
		if (key[k] != 0) return false;
		const char *p = a + k;
		int sid = ap_num (p), host = ap_num (p), w = 0, h = 0;
		unsigned *px = sid > 0 && host > 0 ? kapi_surface_map (sid) : 0;
		if (px == 0 || !kapi_surface_size (sid, &w, &h) || w <= 0 || h <= 0) return false;
		s_apSurf = sid; s_apHost = host; s_apPx = px; s_apW = w; s_apH = h;
		s_apCheck = kapi_get_ticks ();
	}
	return s_apSurf > 0;
}

// The host's messages: the pointer and the keys re-packed as the kernel's events (the same
// handlers as a window's), its AP_CLOSE; every half second: is it still there?
static void ap_pump ()
{
	int from = 0, type = 0, n;
	unsigned char buf[64];
	while ((n = kapi_mailbox_recv (&from, &type, buf, sizeof buf, 0)) >= 0)
	{
		if (from != s_apHost) continue;
		if (type == AP_PTR && n >= (int) sizeof (ApPtr))
		{
			ApPtr e; unsigned char *d = (unsigned char *) &e;
			for (unsigned i = 0; i < sizeof e; i++) d[i] = buf[i];
			int x = e.x < 0 ? 0 : e.x, y = e.y < 0 ? 0 : e.y;
			long v = ((long) (e.wheel & 0xFF) << 48) | ((long) (e.changed & 0xFF) << 40)
			       | ((long) (e.buttons & 0xFF) << 32) | ((long) (x & 0xFFFF) << 16) | (long) (y & 0xFFFF);
			Root::ptrEvent (0, e.event, v);
		}
		else if (type == AP_KEY && n >= (int) sizeof (ApKey))
		{
			ApKey k; unsigned char *d = (unsigned char *) &k;
			for (unsigned i = 0; i < sizeof k; i++) d[i] = buf[i];
			Root::keyEvent (0, GUI_EVENT_KEY, k.key);
		}
		else if (type == AP_CLOSE) s_apClosed = true;
	}
	unsigned now = kapi_get_ticks ();
	if (now - s_apCheck >= 50)
	{
		s_apCheck = now;
		if (kapi_ipc_lookup (AP_SERVICE) != s_apHost) s_apClosed = true;	// (the host is gone)
	}
}

void wk_pump ()
{
	pump_events ();
	if (wk_applet ()) ap_pump ();
}

void wk_present ()
{
	if (!wk_applet ()) { kapi_present (); return; }
	if (s_apClosed) return;
	int r[4] = { 0, 0, 0, 0 };
	kapi_mailbox_send (s_apHost, AP_PRESENT, r, sizeof r);
	kapi_yield ();
}

bool wk_applet_send (int type, const void *data, unsigned len)
{
	return wk_applet () && kapi_mailbox_send (s_apHost, type, data, len) >= 0;
}

bool wk_quit ()
{
	if (!wk_applet ()) return should_exit () != 0;
	if (s_apClosed && !s_apBye)			// (once: the host stops reading the surface)
	{
		s_apBye = true;
		kapi_mailbox_send (s_apHost, AP_EXIT, 0, 0);
	}
	return s_apClosed;
}

Root::Root (int w, int h, const char *title) : Widget (0, 0, w, h), bg (C_BG),
  m_tipBox (0), m_mx (-1), m_my (-1), m_moveT (0), m_tipDone (true),
  m_resizable (false), m_maxed (false), m_rx (0), m_ry (0), m_rw (w), m_rh (h)
{
	if (wk_applet ()) initApplet ();
	else init (kapi_create_window (w, h, title));
}

Root::Root (int x, int y, int w, int h, const char *title, unsigned flags)
  : Widget (0, 0, w, h), bg (C_BG),
    m_tipBox (0), m_mx (-1), m_my (-1), m_moveT (0), m_tipDone (true),
    m_resizable (false), m_maxed (false), m_rx (0), m_ry (0), m_rw (w), m_rh (h)
{
	if (wk_applet ()) initApplet ();
	else init (kapi_create_window_ex (x, y, w, h, title, flags));
}

// An applet: its Root is the host's pane (the surface's size, whatever the app asked for).
void Root::initApplet ()
{
	width = s_apW; height = s_apH; lytW = width; lytH = height;
	canvas.adopt (s_apPx, width, height);
	wtk::init ();
	bg = C_BG;
	active () = this;
	hasFocus = true;
	int m[2] = { width, height };
	kapi_mailbox_send (s_apHost, AP_HELLO, m, sizeof m);
}

void Root::init (unsigned *fb)
{
	canvas.adopt (fb, width, height);		// the root draws straight into the window canvas
	wk_window_state (WK_WIN_MENU);			// (it answers the window menu: ptrEvent)
	wk_decorate_window ();				// title bar / borders / close box (no-op if borderless)
	wtk::init ();					// load the global font family once (SD:/fonts/ns-sans.fnt)
	bg = C_BG;					// (the theme is read by now: SD:/etc/theme.txt)
	active () = this;
	hasFocus = true;
}

void Root::onDraw () { canvas.clear (bg); }		// client-area background

void Root::attach ()
{
	kapi_set_pointer_handler (ptrEvent);
	kapi_set_key_handler (keyEvent);
}

void Root::run ()
{
	attach ();
	while (!wk_quit ())
	{
		wk_pump ();
		onTick ();
		tooltipTick ();
		if (!valid) { draw (); wk_present (); }
		msleep (16);
	}
}

Root *&Root::active () { static Root *p = 0; return p; }
Root *Root::current () { return active (); }

void Root::ptrEvent (unsigned long, int ev, long v)
{
	static int bl = 0, br = 0, bm = 0;		// persistent button state across events
	Root *r = active ();
	if (r == 0) return;
	if (ev >= GUI_EVENT_PTR_MOVE && ev <= GUI_EVENT_PTR_WHEEL)		// tooltips: note the rest
	{
		r->tooltipHide ();
		r->m_mx = ev == GUI_EVENT_PTR_LEAVE ? -1 : GUI_PTR_X (v);
		r->m_my = GUI_PTR_Y (v);
		r->m_moveT = kapi_get_ticks ();
		r->m_tipDone = ev != GUI_EVENT_PTR_MOVE && ev != GUI_EVENT_PTR_ENTER;	// clicks: no tip
	}
	int c = GUI_PTR_CHANGED (v);
	switch (ev)
	{
	case GUI_EVENT_PTR_DOWN:
		if (c & 1) bl = 1;
		if (c & 2) br = 1;
		if (c & 4) bm = 1;
		break;
	case GUI_EVENT_PTR_UP:
		if (c & 1) bl = 0;
		if (c & 2) br = 0;
		if (c & 4) bm = 0;
		break;
	case GUI_EVENT_PTR_LEAVE:
		r->handleMouse (-1, -1, 0, 0, 0, 0);
		return;
	case GUI_EVENT_PTR_WHEEL:		// route a scroll notch to the widget under the cursor
		r->handleMouse (GUI_PTR_X (v), GUI_PTR_Y (v), bl, br, bm, GUI_PTR_WHEEL (v));
		return;
	case GUI_EVENT_DROP:			// drag & drop (ABI v42)
	{
		static char data[4097];
		int type = 0, n = kapi_drag_data (&type, data, sizeof data - 1);
		if (n > (int) sizeof data - 1) n = (int) sizeof data - 1;
		if (n < 0) n = 0;
		data[n] = '\0';
		r->onDrop (GUI_PTR_X (v), GUI_PTR_Y (v), type, data, n, GUI_DND_FLAGS (v));
		return;
	}
	case GUI_EVENT_DRAG_OVER:
		r->onDragOver (GUI_PTR_X (v), GUI_PTR_Y (v), (GUI_DND_FLAGS (v) & DND_F_LEAVE) != 0, GUI_DND_FLAGS (v));
		return;
	case GUI_EVENT_DRAG_DONE:
		r->onDragDone (GUI_DND_PID (v), GUI_DND_FLAGS (v));
		return;
	case GUI_EVENT_WINCTL:			// a title button (v64): the window menu, maximise
		if (v == KAPI_FRAME_MENU) r->windowMenu ();
		else if (v == KAPI_FRAME_MAXIMISE) r->maximise (!r->maximised ());
		return;
	default:
		break;
	}
	r->handleMouse (GUI_PTR_X (v), GUI_PTR_Y (v), bl, br, bm, 0);
}

void Root::keyEvent (unsigned long, int ev, long v)
{
	Root *r = active ();
	if (r == 0 || ev != GUI_EVENT_KEY) return;
	if (Menu::current () && Menu::current ()->shortcut (v)) return;	// menu shortcuts first
	r->handleKey (v);
}

// ---- tooltips -------------------------------------------------------------------------------
// ---- the frame's buttons (v64) -------------------------------------------------------------
void Root::setResizable (bool on)
{
	m_resizable = on;
	wk_window_state (WK_WIN_MENU | (m_resizable ? WK_WIN_RESIZABLE : 0) | (m_maxed ? WK_WIN_MAXIMISED : 0));
	wk_decorate_window ();
}

void Root::maximise (bool on)
{
	if (!m_resizable || on == m_maxed) return;
	struct kapi_win_geom g;
	if (kapi_win_geometry (&g) != 0) return;
	int x, y, cw, ch;
	if (on)
	{
		m_rx = g.x; m_ry = g.y; m_rw = width; m_rh = height;
		x = g.ax; y = g.ay; cw = g.aw - (g.w - g.cw); ch = g.ah - (g.h - g.ch);
	}
	else { x = m_rx; y = m_ry; cw = m_rw; ch = m_rh; }
	if (cw < 1 || ch < 1) return;
	int stride = cw;
	unsigned *fb = kapi_resize_window2 (cw, ch, &stride);
	if (fb == 0) return;
	canvas.adopt (fb, cw, ch, stride);
	kapi_move_window (x, y);
	m_maxed = on;
	width = cw; height = ch;
	layout ();					// (the anchors, the layouts)
	invalidate (true);
	wk_window_state (WK_WIN_MENU | (m_resizable ? WK_WIN_RESIZABLE : 0) | (m_maxed ? WK_WIN_MAXIMISED : 0));
	wk_decorate_window ();
	onResized ();
}

// The workspaces' names (SD:/etc/dock.ini, "desk = name" lines: the Control Panel's Panel
// applet writes them) -> how many were named.
static int desk_names (char names[][24], int max)
{
	void *f = kapi_open ("SD:/etc/dock.ini");
	if (f == 0) return 0;
	static char buf[2048];
	int n = kapi_read (f, buf, sizeof buf - 1), k = 0;
	kapi_close (f);
	if (n < 0) n = 0;
	buf[n] = 0;
	for (char *p = buf; *p && k < max; )
	{
		char *l = p; while (*p && *p != '\n') p++;
		if (*p) *p++ = 0;
		while (*l == ' ' || *l == '\t') l++;
		if (l[0] != 'd' || l[1] != 'e' || l[2] != 's' || l[3] != 'k') continue;
		char *v = l + 4; while (*v == ' ' || *v == '\t') v++;
		if (*v != '=') continue;
		v++; while (*v == ' ' || *v == '\t') v++;
		int j = 0; while (v[j] && v[j] != '\r' && j < 23) { names[k][j] = v[j]; j++; }
		while (j > 0 && names[k][j - 1] == ' ') j--;
		names[k][j] = 0;
		k++;
	}
	return k;
}

void Root::windowMenu ()
{
	enum { WM_RESTORE = 1, WM_MAXIMISE, WM_MINIMISE, WM_CLOSE, WM_ALLDESKS, WM_ONEDESK, WM_DESK0 = 20 };
	tooltipHide ();
	PopupMenu m (2, 0);
	if (m_maxed) m.add ("Restore", WM_RESTORE);
	else m.add ("Maximise", WM_MAXIMISE, m_resizable);
	m.add ("Minimise", WM_MINIMISE);
	// (v65) the workspaces: this window to another one, or on every one
	int info = kapi_desk (-1, 0), count = KAPI_DESK_COUNT (info), cur = KAPI_DESK_CUR (info);
	int mine = kapi_win_desk (0, -2);
	static char label[KAPI_DESK_MAX][40];
	if (count > 1 && mine >= -1)
	{
		char names[KAPI_DESK_MAX][24];
		int named = desk_names (names, KAPI_DESK_MAX);
		m.separator ();
		for (int d = 0; d < count && d < KAPI_DESK_MAX; d++)
		{
			if (d == (mine >= 0 ? mine : cur)) continue;
			int p = 0;
			const char *t = "Move to ";
			for (int i = 0; t[i]; i++) label[d][p++] = t[i];
			if (d < named && names[d][0]) for (int i = 0; names[d][i] && p < 38; i++) label[d][p++] = names[d][i];
			else
			{
				const char *w = "Workspace "; for (int i = 0; w[i]; i++) label[d][p++] = w[i];
				label[d][p++] = (char) ('1' + d);
			}
			label[d][p] = 0;
			m.add (label[d], WM_DESK0 + d);
		}
		if (mine >= 0) m.add ("On All Workspaces", WM_ALLDESKS);
		else m.add ("On This Workspace Only", WM_ONEDESK);
	}
	m.separator ();
	m.add ("Close", WM_CLOSE, true, "Ctrl+Q");
	int r = m.run ();
	switch (r)
	{
	case WM_RESTORE:  maximise (false); break;
	case WM_MAXIMISE: maximise (true); break;
	case WM_MINIMISE: kapi_win_minimise (0); break;
	case WM_CLOSE:    kapi_menu_command (MENU_QUIT); break;
	case WM_ALLDESKS: kapi_win_desk (0, -1); break;
	case WM_ONEDESK:  kapi_win_desk (0, cur); break;
	default:
		if (r >= WM_DESK0 && r < WM_DESK0 + KAPI_DESK_MAX) kapi_win_desk (0, r - WM_DESK0);
		break;
	}
}

class ToolTipBox : public Widget
{
public:
	const char *text;
	ToolTipBox (int l, int t, int w, int h, const char *s) : Widget (l, t, w, h), text (s) { transparent = true; }
	void onDraw () override
	{
		wk_popup (canvas, 0, 0, width, height, 5, 0x00FFF8D6);
		canvas.text (6, (height - wk_fh ()) / 2, text, 0x00201C1A);
	}
};

// The deepest visible widget under (x, y) (w-local coords) that has a tip.
static Widget *tip_at (Widget *w, int x, int y)
{
	Widget *best = w->tip ? w : 0;
	for (Widget *c = w->lastChild; c; c = c->prevSib)
	{
		if (c->hidden) continue;
		int cx = x + w->scrollX - c->left, cy = y + w->scrollY - c->top;
		if (cx < 0 || cy < 0 || cx >= c->width || cy >= c->height) continue;
		Widget *d = tip_at (c, cx, cy);
		return d ? d : best;
	}
	return best;
}

void Root::tooltipHide ()
{
	if (m_tipBox == 0) return;
	removeChild (m_tipBox);
	delete m_tipBox;
	m_tipBox = 0;
	invalidate (true);
}

void Root::tooltipTick ()
{
	if (m_tipDone || m_tipBox || m_mx < 0 || kapi_get_ticks () - m_moveT < 60) return;
	m_tipDone = true;
	for (Widget *c = firstChild; c; c = c->nextSib) if (c->modal) return;	// not over a dialog
	Widget *w = tip_at (this, m_mx, m_my);
	if (w == 0 || w == this) return;
	int tw = wk_len (w->tip) * wk_fw () + 12, th = wk_fh () + 8;
	int x = m_mx + 12, y = m_my + 20;
	if (x + tw > width) x = width - tw - 2;
	if (y + th > height) y = m_my - th - 4;
	if (x < 0) x = 0;
	if (y < 0) y = 0;
	m_tipBox = new ToolTipBox (x, y, tw, th, w->tip);
	addChild (m_tipBox);
	invalidate (true);
}

} // namespace wtk
