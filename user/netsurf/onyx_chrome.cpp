//
// onyx_chrome.cpp -- NetSurf's window on Onyx (see onyx_chrome.h): a wtk window whose top band
// is a native toolbar -- back, forward, reload / stop, home, the address field -- above the page
// NetSurf draws (the "onyx" libnsfb surface gets the rest of the window's canvas). The window's
// frame is wtk's: its close box and its window menu close NetSurf, maximise resizes the page.
//
// The events: the kapi pointer / key handlers are ours. The band's pointer events go to the wtk
// tree, the page's to the surface's handlers (their y less the band's height); a press keeps
// its side until the button is released (a drag out of a button, a text selection out of the
// page). Keys go to the address field while it has the focus, else to the page -- but for the
// shortcuts (the menu's, F5, Esc, Alt+Left / Right, F6).
//
#include "wtk/wtk.h"
#include "onyx_chrome.h"

using namespace wtk;

namespace {

const int TB = ONYX_TOOLBAR_H;
const int BTN_W = 34, BTN_H = 30, BTN_Y = (TB - BTN_H) / 2 - 1, GAP = 4, PAD = 6;
const int URL_MAX = 2048;

// ---- a toolbar button: the theme's framed button with a glyph --------------------------------
class ToolButton : public Widget
{
public:
	int glyph;
	void (*cmd) (void);
	ToolButton (int l, int t, int g, void (*c) (void)) : Widget (l, t, BTN_W, BTN_H), glyph (g), cmd (c) {}
	void setGlyph (int g) { if (g != glyph) { glyph = g; invalidate (true); } }
	void setDisabled (bool d) { if (d != disabled) { disabled = d; if (d) hover = pressed = false; invalidate (true); } }
	void onDraw () override
	{
		canvas.clear (bgColor ());
		int st = disabled ? WK_DISABLED : pressed ? WK_PRESSED : hover ? WK_HOT : WK_NORMAL;
		int bx, by, bw, bh;
		wk_framed (canvas, 0, 0, width, height, C_FACE, st, &bx, &by, &bw, &bh);
		wk_glyph (canvas, glyph, bx + bw / 2, by + bh / 2, 13, disabled ? C_DIS : C_TEXT);
	}
	bool onMouse (int mx, int, int bl, int, int, int) override
	{
		if (mx < 0) { if (hover || pressed) { hover = pressed = false; invalidate (true); } return false; }
		if (disabled) return true;
		bool wh = hover, wp = pressed;
		hover = true;
		if (bl && !pressed) pressed = true;
		else if (!bl && pressed) { pressed = false; if (cmd) cmd (); }
		if (hover != wh || pressed != wp) invalidate (true);
		return true;
	}
};

// ---- the address field: a one-line editor (wtk's Textbox holds 63 characters) ----------------
void clip_copy (const char *s, int n) { kapi_clipboard_set (CLIP_TEXT, s, (unsigned) n); }

class UrlField : public Widget
{
public:
	char text[URL_MAX];	// what is shown / edited
	char page[URL_MAX];	// the page's address (Esc goes back to it)
	int  len, caret, start;
	bool all;		// the whole text selected (a click into the field, Ctrl+A)
	UrlField (int l, int t, int w, int h) : Widget (l, t, w, h), len (0), caret (0), start (0), all (false)
	{ canFocus = true; text[0] = page[0] = '\0'; }

	void setPage (const char *s)
	{
		int n = 0;
		for (; s && s[n] && n < URL_MAX - 1; n++) page[n] = s[n];
		page[n] = '\0';
		if (!hasFocus) { revert (); invalidate (true); }	// (not while it is being edited)
	}
	void revert ()
	{
		for (len = 0; page[len]; len++) text[len] = page[len];
		text[len] = '\0'; caret = len; start = 0; all = false;
	}
	void focusIn () { setFocus (); all = true; caret = len; invalidate (true); }
	void focusOut () { if (hasFocus) { hasFocus = false; revert (); invalidate (true); } }	// (back to the page)

	int visible () const { int n = (width - 2 * PAD) / wk_fw (); return n < 1 ? 1 : n; }
	void onDraw () override
	{
		int fw = wk_fw (), fh = wk_fh ();
		canvas.clear (bgColor ());
		wk_sunken (canvas, 0, 0, width, height, 4, C_FIELD, hasFocus);
		int vis = visible ();
		if (caret < start) start = caret;
		if (caret > start + vis - 1) start = caret - (vis - 1);
		if (start < 0) start = 0;
		char buf[URL_MAX]; int j = 0;
		for (int c = start; c < len && j < vis; c++) buf[j++] = text[c];
		buf[j] = '\0';
		int ty = (height - fh) / 2;
		if (hasFocus && all && len > 0)
		{
			canvas.fillRect (PAD - 1, ty - 1, j * fw + 2, fh + 2, C_ACCENT);
			canvas.text (PAD, ty, buf, C_SEL_TEXT);
		}
		else canvas.text (PAD, ty, buf, C_FIELD_TEXT);
		if (hasFocus && !all)
			canvas.fillRect (PAD + (caret - start) * fw, ty, 1, fh, C_ACCENT);
	}
	bool onMouse (int mx, int, int bl, int, int, int) override
	{
		if (mx < 0 || !bl) return mx >= 0;
		if (!hasFocus) { focusIn (); return true; }
		int c = start + (mx - PAD + wk_fw () / 2) / wk_fw ();
		caret = c < 0 ? 0 : c > len ? len : c;
		all = false;
		invalidate (true);
		return true;
	}
	void erase () { len = caret = start = 0; text[0] = '\0'; all = false; }
	bool onKey (long k) override
	{
		if (k == KEY_ENTER) { onyx_browser_go (text); focusOut (); return true; }
		if (k == 27) { focusOut (); return true; }
		if (k == 1) { all = true; invalidate (true); return true; }			// ^A
		if (k == 3) { clip_copy (text, len); return true; }				// ^C
		if (k == 24) { clip_copy (text, len); erase (); invalidate (true); return true; }	// ^X
		if (k == 22)									// ^V
		{
			static char clip[URL_MAX];
			int type = 0; unsigned serial = 0;
			int n = kapi_clipboard_get (&type, clip, sizeof clip - 1, &serial);
			if (n > 0 && type == CLIP_TEXT)
			{
				if (all) erase ();
				for (int i = 0; i < n && len < URL_MAX - 1; i++)
				{
					char ch = clip[i];
					if (ch == '\r' || ch == '\n' || ch == '\t') ch = ' ';
					if ((unsigned char) ch < 32) continue;
					for (int m = len; m > caret; m--) text[m] = text[m - 1];
					text[caret++] = ch; len++;
				}
				text[len] = '\0';
				invalidate (true);
			}
			return true;
		}
		switch (k)
		{
		case KEY_LEFT:  if (all) caret = 0; else if (caret > 0) caret--; all = false; break;
		case KEY_RIGHT: if (!all && caret < len) caret++; all = false; break;
		case KEY_HOME:  caret = 0; all = false; break;
		case KEY_END:   caret = len; all = false; break;
		case KEY_BACKSPACE:
			if (all) erase ();
			else if (caret > 0) { for (int m = caret - 1; m < len; m++) text[m] = text[m + 1]; caret--; len--; }
			break;
		case KEY_DEL:
			if (all) erase ();
			else if (caret < len) { for (int m = caret; m < len; m++) text[m] = text[m + 1]; len--; }
			break;
		default:
			if (k < 32 || k > 126) return false;
			if (all) erase ();
			if (len >= URL_MAX - 1) return true;
			for (int m = len; m > caret; m--) text[m] = text[m - 1];
			text[caret++] = (char) k; len++; text[len] = '\0';
			break;
		}
		invalidate (true);
		return true;
	}
};

// ---- the window ----------------------------------------------------------------------------
bool g_resized;

class NsWindow : public Root
{
public:
	ToolButton *back, *fwd, *reload, *home;
	UrlField   *url;
	bool        busy;
	NsWindow (int w, int h) : Root (w, h + TB, "NetSurf"), busy (false)
	{
		int x = PAD;
		back   = new ToolButton (x, BTN_Y, WKG_CHEV_LEFT, onyx_browser_back);     x += BTN_W + GAP;
		fwd    = new ToolButton (x, BTN_Y, WKG_CHEV_RIGHT, onyx_browser_forward); x += BTN_W + GAP;
		reload = new ToolButton (x, BTN_Y, WKG_RELOAD, reload_or_stop);           x += BTN_W + GAP;
		home   = new ToolButton (x, BTN_Y, WKG_HOME, onyx_browser_home);          x += BTN_W + GAP + 2;
		url    = new UrlField (x, BTN_Y, width - x - PAD, BTN_H);
		url->anchor = ANCHOR_LEFT | ANCHOR_RIGHT | ANCHOR_TOP;
		back->setDisabled (true);
		fwd->setDisabled (true);
		addChild (back); addChild (fwd); addChild (reload); addChild (home); addChild (url);
		setResizable (true);
	}
	// Only the band is the window's own: the rest is the page's (NetSurf draws it).
	void onDraw () override
	{
		canvas.fillRect (0, 0, width, TB, C_BG);
		wk_etch_h (canvas, 0, TB - 2, width, C_BG);
	}
	void onResized () override { g_resized = true; }
	static void reload_or_stop ();
	bool pageFocus () const { return !url->hasFocus; }
};

NsWindow *g_win;
onyx_chrome_handler g_pagePtr, g_pageKey;
int g_grab;		// 0 none, 1 the band, 2 the page: where the pressed buttons went
bool g_shown;		// the band drawn at least once

void NsWindow::reload_or_stop () { if (g_win && g_win->busy) onyx_browser_stop (); else onyx_browser_reload (); }

bool has_modal (void)
{
	for (Widget *c = g_win ? g_win->firstChild : 0; c; c = c->nextSib)
		if (c->modal) return true;
	return false;
}

// The pointer value with y moved up by the band (clamped to the page's top).
long page_value (long v)
{
	long y = GUI_PTR_Y (v) - TB;
	if (y < 0) y = 0;
	return (v & ~0xFFFFL) | (y & 0xFFFF);
}

void ptr_event (unsigned long sender, int ev, long v)
{
	static int bl, br, bm;
	if (g_win == 0) return;
	if (ev == GUI_EVENT_WINCTL)			// a title button (v64)
	{
		if (v == KAPI_FRAME_MENU) { g_win->windowMenu (); onyx_browser_redraw (); }
		else if (v == KAPI_FRAME_MAXIMISE) g_win->maximise (!g_win->maximised ());
		return;
	}
	if (ev == GUI_EVENT_DROP)			// something dropped on the window: open it
	{
		static char data[URL_MAX];
		int type = 0, n = kapi_drag_data (&type, data, sizeof data - 1);
		if (n > 0)
		{
			data[n < URL_MAX - 1 ? n : URL_MAX - 1] = '\0';
			for (char *p = data; *p; p++) if (*p == '\n') { *p = '\0'; break; }	// (the first path)
			onyx_browser_go (data);
		}
		return;
	}
	if (ev < GUI_EVENT_PTR_MOVE || ev > GUI_EVENT_PTR_WHEEL) return;
	int c = GUI_PTR_CHANGED (v);
	if (ev == GUI_EVENT_PTR_DOWN) { if (c & 1) bl = 1; if (c & 2) br = 1; if (c & 4) bm = 1; }
	if (ev == GUI_EVENT_PTR_UP)   { if (c & 1) bl = 0; if (c & 2) br = 0; if (c & 4) bm = 0; }
	int x = GUI_PTR_X (v), y = GUI_PTR_Y (v);
	bool band = has_modal () || (g_grab ? g_grab == 1 : y < TB);
	if (ev == GUI_EVENT_PTR_DOWN && !g_grab) g_grab = band ? 1 : 2;
	if (ev == GUI_EVENT_PTR_LEAVE)
	{
		g_win->handleMouse (-1, -1, 0, 0, 0, 0);
		if (g_pagePtr) g_pagePtr (sender, ev, v);
	}
	else if (band)
	{
		g_win->handleMouse (x, y, bl, br, bm, ev == GUI_EVENT_PTR_WHEEL ? GUI_PTR_WHEEL (v) : 0);
		if (ev == GUI_EVENT_PTR_MOVE && g_pagePtr) g_pagePtr (sender, GUI_EVENT_PTR_LEAVE, 0);
	}
	else
	{
		if (ev == GUI_EVENT_PTR_DOWN && !g_win->pageFocus ()) g_win->url->focusOut ();
		g_win->handleMouse (-1, -1, 0, 0, 0, 0);		// (the band: the pointer left it)
		if (g_pagePtr) g_pagePtr (sender, ev, page_value (v));
	}
	if (ev == GUI_EVENT_PTR_UP && !bl && !br && !bm) g_grab = 0;
}

void key_event (unsigned long sender, int ev, long k)
{
	if (g_win == 0 || ev != GUI_EVENT_KEY) return;
	if (has_modal ()) { g_win->handleKey (k); return; }
	if (Menu::current () && Menu::current ()->shortcut (k)) return;
	unsigned mods = kapi_get_modifiers ();
	if ((mods & MOD_ALT) && k == KEY_LEFT)  { onyx_browser_back (); return; }
	if ((mods & MOD_ALT) && k == KEY_RIGHT) { onyx_browser_forward (); return; }
	if (k == KEY_F1 + 4) { onyx_browser_reload (); return; }		// F5
	if (k == KEY_F1 + 5) { g_win->url->focusIn (); return; }		// F6
	if (!g_win->pageFocus ()) { g_win->handleKey (k); return; }
	if (k == 27 && g_win->busy) { onyx_browser_stop (); return; }
	if (g_pageKey) g_pageKey (sender, ev, k);
}

// ---- the system menu bar -------------------------------------------------------------------
void m_location () { if (g_win) g_win->url->focusIn (); }
void m_back ()     { onyx_browser_back (); }
void m_forward ()  { onyx_browser_forward (); }
void m_reload ()   { onyx_browser_reload (); }
void m_stop ()     { onyx_browser_stop (); }
void m_home ()     { onyx_browser_home (); }

Menu g_menu;

} // namespace

// ---- the C interface -------------------------------------------------------------------------
extern "C" {

unsigned *onyx_chrome_open (int w, int h, int *stride)
{
	if (g_win) return onyx_chrome_page (stride, 0, 0);
	g_win = new NsWindow (w, h);
	g_menu.menu ("File");
	g_menu.item ("Open Location...", "^L", WK_CTRL ('L'), m_location);
	g_menu.menu ("Navigate");
	g_menu.item ("Back", "Alt+Left", 0, m_back);
	g_menu.item ("Forward", "Alt+Right", 0, m_forward);
	g_menu.separator ();
	g_menu.item ("Reload", "^R", WK_CTRL ('R'), m_reload);
	g_menu.item ("Stop", "Esc", 0, m_stop);
	g_menu.separator ();
	g_menu.item ("Home", "", 0, m_home);
	g_menu.publish ();
	kapi_set_pointer_handler (ptr_event);
	kapi_set_key_handler (key_event);
	return onyx_chrome_page (stride, 0, 0);
}

unsigned *onyx_chrome_page (int *stride, int *w, int *h)
{
	if (g_win == 0) return 0;
	Canvas &c = g_win->canvas;
	if (stride) *stride = c.stride;
	if (w) *w = c.w;
	if (h) *h = c.h - TB;
	return c.px + (long) TB * c.stride;
}

void onyx_chrome_default_size (int *w, int *h)
{
	int sw = 1024, sh = 768;
	kapi_screen_size (&sw, &sh);
	int pw = sw - 64, ph = sh - TB - 28 - 8 - 24 - 90;	// the band, the title + border, the menu bar, the dock
	if (pw > 1280) pw = 1280;
	if (ph > 960) ph = 960;
	if (pw < 480) pw = 480;
	if (ph < 300) ph = 300;
	*w = pw; *h = ph;
}

void onyx_chrome_theme (unsigned *face, unsigned *shade)
{
	*face = C_BG;
	*shade = wk_tone (C_BG, 96);
}

void onyx_chrome_set_page_handlers (onyx_chrome_handler ptr, onyx_chrome_handler key)
{
	g_pagePtr = ptr;
	g_pageKey = key;
}

int onyx_chrome_pump (void)
{
	if (g_win == 0) return 0;
	kapi_pump_events ();
	if (!g_win->valid || !g_shown)
	{
		g_win->draw ();
		g_shown = true;
		kapi_present ();
	}
	int r = g_resized;
	g_resized = false;
	return r;
}

void onyx_chrome_present (void)
{
	if (g_win && !g_win->valid) g_win->draw ();
	kapi_present ();
}

void onyx_chrome_set_url (const char *url)
{
	if (g_win) g_win->url->setPage (url);
}

void onyx_chrome_set_busy (int busy)
{
	if (g_win == 0) return;
	g_win->busy = busy != 0;
	g_win->reload->setGlyph (busy ? WKG_CLOSE : WKG_RELOAD);
}

void onyx_chrome_set_nav (int can_back, int can_forward)
{
	if (g_win == 0) return;
	g_win->back->setDisabled (!can_back);
	g_win->fwd->setDisabled (!can_forward);
}

} // extern "C"
