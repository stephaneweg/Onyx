#include "wtk/root.h"
#include "wtk/menu.h"		// Menu::shortcut (keys go through the menu first)
#include "wtk/skin.h"		// wk_decorate_window
#include "wtk/font.h"		// wtk::init (load the global font family at startup)
#include "wtk/dialog.h"		// PopupMenu (the window menu)
#include "applib.h"		// should_exit, msleep, pump_events

namespace wtk {

Root::Root (int w, int h, const char *title) : Widget (0, 0, w, h), bg (C_BG),
  m_tipBox (0), m_mx (-1), m_my (-1), m_moveT (0), m_tipDone (true),
  m_resizable (false), m_maxed (false), m_rx (0), m_ry (0), m_rw (w), m_rh (h)
{ init (kapi_create_window (w, h, title)); }

Root::Root (int x, int y, int w, int h, const char *title, unsigned flags)
  : Widget (0, 0, w, h), bg (C_BG),
    m_tipBox (0), m_mx (-1), m_my (-1), m_moveT (0), m_tipDone (true),
    m_resizable (false), m_maxed (false), m_rx (0), m_ry (0), m_rw (w), m_rh (h)
{ init (kapi_create_window_ex (x, y, w, h, title, flags)); }

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
	while (!should_exit ())
	{
		pump_events ();
		onTick ();
		tooltipTick ();
		if (!valid) { draw (); kapi_present (); }
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

void Root::windowMenu ()
{
	enum { WM_RESTORE = 1, WM_MAXIMISE, WM_MINIMISE, WM_CLOSE };
	tooltipHide ();
	PopupMenu m (2, 0);
	if (m_maxed) m.add ("Restore", WM_RESTORE);
	else m.add ("Maximise", WM_MAXIMISE, m_resizable);
	m.add ("Minimise", WM_MINIMISE);
	m.separator ();
	m.add ("Close", WM_CLOSE, true, "Ctrl+Q");
	switch (m.run ())
	{
	case WM_RESTORE:  maximise (false); break;
	case WM_MAXIMISE: maximise (true); break;
	case WM_MINIMISE: kapi_win_minimise (0); break;
	case WM_CLOSE:    kapi_menu_command (MENU_QUIT); break;
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
