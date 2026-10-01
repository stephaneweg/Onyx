//
// onyx_chrome.cpp -- NetSurf's window on Onyx (see onyx_chrome.h): a wtk window whose top band
// is a native toolbar -- back, forward, reload / stop, home, the history, the padlock, the address
// field, the site's version (the blue pill: Standard / Mobile / Desktop) -- above the page
// NetSurf draws (the "onyx" libnsfb surface gets the rest of the window's canvas). The window's
// frame is wtk's: its close box and its window menu close NetSurf, maximise resizes the page.
//
// The events: the kapi pointer / key handlers are ours. The band's pointer events go to the wtk
// tree, the page's to the surface's handlers (their y less the band's height); a press keeps
// its side until the button is released (a drag out of a button, a text selection out of the
// page). Keys go to the address field while it has the focus, else to the page -- but for the
// shortcuts (the menu's, F5, Esc, Alt+Left / Right, F6, Ctrl+H).
//
// The history is a native dialog (Navigate > History..., Ctrl+H, the clock button): the pages
// visited, the most recent first, from NetSurf's global history (gui.c keeps it on the card).
// The app is Jet Browser ("Jet" in the window's title): Onyx's web browser, based on NetSurf;
// Help > About Jet Browser... credits NetSurf and the libraries (their licences).
//
// docs/06 §38: right of the pill the zoom control ("-  100%  +"; Ctrl+- / Ctrl++ / Ctrl+0,
// Ctrl+wheel), then the downloads' button (its menu: progress, cancel); the status bar at the
// window's bottom (the page's state, the link under the pointer: View > Hide Status Bar); the
// Save dialog of a download (frontends/framebuffer/onyx_download.c asks for it).
//
#include <time.h>
#include "wtk/wtk.h"
#include "onyx_chrome.h"
#include "onyx_io.h"
#include "img/pngsave.hpp"	// (docs/06 §40: a copied image, as a PNG file)
#ifdef ONYX_HOST_SIM
#include <stdio.h>		// (the PC bench: the bar's text and the band's places logged)
#endif

using namespace wtk;

namespace {

const int TB = ONYX_TOOLBAR_H;
const int SB = ONYX_STATUSBAR_H;	// the status bar, at the window's bottom (when shown)
const int BTN_W = 34, BTN_H = 30, BTN_Y = (TB - BTN_H) / 2 - 1, GAP = 4, PAD = 6;
const int URL_MAX = 2048;
const int LOCK_W = 32;		// the padlock's half pill, left of the address field
bool g_sbOn = true;		// the status bar shown (View > Status Bar; gui.c keeps the choice)

// ---- a toolbar button: the theme's framed button with a glyph --------------------------------
class ToolButton : public Widget
{
public:
	int glyph;
	void (*cmd) (void);
	ToolButton (int l, int t, int g, void (*c) (void)) : Widget (l, t, BTN_W, BTN_H), glyph (g), cmd (c) {}
	ToolButton (int l, int t, int w, int h, int g, void (*c) (void)) : Widget (l, t, w, h), glyph (g), cmd (c) {}
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
// It holds Latin-1 (the bitmap font's; 0x80 is the euro, as the keyboard's keys: kapi.h) -- one
// byte a character, one cell; UTF-8 out (the address opened, the clipboard) and in (pasted).
int latin1_to_utf8 (const char *s, int n, char *out, int cap)
{
	int j = 0;
	for (int i = 0; i < n; i++)
	{
		unsigned c = (unsigned char) s[i];
		if (c == 0x80) c = 0x20AC;
		if (c < 0x80) { if (j + 1 >= cap) break; out[j++] = (char) c; }
		else if (c < 0x800) { if (j + 2 >= cap) break; out[j++] = (char) (0xC0 | c >> 6); out[j++] = (char) (0x80 | (c & 0x3F)); }
		else { if (j + 3 >= cap) break; out[j++] = (char) (0xE0 | c >> 12); out[j++] = (char) (0x80 | ((c >> 6) & 0x3F)); out[j++] = (char) (0x80 | (c & 0x3F)); }
	}
	out[j] = '\0';
	return j;
}
// The next character of UTF-8 s (n bytes) at *i, as the field's byte (Latin-1, the euro 0x80);
// -1: none in Latin-1 (dropped). A stray byte is taken as Latin-1.
int utf8_to_latin1 (const char *s, int n, int *i)
{
	unsigned c = (unsigned char) s[*i];
	int k = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
	if (k == 0 || *i + k >= n) { (*i)++; return (int) c; }	// (ASCII, or cut short)
	unsigned u = c & (0x3F >> k);
	for (int m = 1; m <= k; m++)
	{
		unsigned b = (unsigned char) s[*i + m];
		if ((b & 0xC0) != 0x80) { (*i)++; return (int) c; }
		u = u << 6 | (b & 0x3F);
	}
	*i += k + 1;
	if (u == 0x20AC) return 0x80;
	return u <= 0xFF ? (int) u : -1;
}
void clip_copy (const char *s, int n)
{
	static char u[URL_MAX * 3];
	int m = latin1_to_utf8 (s, n, u, sizeof u);
	kapi_clipboard_set (CLIP_TEXT, u, (unsigned) m);
}

// A one-line editor of Latin-1 text (the address field, the find bar's field): the caret, the
// whole text selected (a click into it, Ctrl+A), Ctrl+C / X / V with the system's clipboard
// (UTF-8 there), the arrows, Home / End, Backspace / Del, the characters typed.
class LineEdit : public Widget
{
public:
	char text[URL_MAX];	// what is shown / edited
	int  len, caret, start;
	bool all;		// the whole text selected (a click into the field, Ctrl+A)
	LineEdit (int l, int t, int w, int h) : Widget (l, t, w, h), len (0), caret (0), start (0), all (false)
	{ canFocus = true; text[0] = '\0'; }

	int visible (int w) const { int n = (w - 2 * PAD) / wk_fw (); return n < 1 ? 1 : n; }
	// The field's face from x0 to x1 (its rounded ends past the canvas when joined), the
	// text in [PAD, tw) of it
	void drawField (int xl, int xr, unsigned face, int tw)
	{
		int fw = wk_fw (), fh = wk_fh ();
		wk_sunken (canvas, xl, 0, width - xl + xr, height, 4, face, hasFocus);
		int vis = visible (tw);
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
	void clickAt (int mx)
	{
		int c = start + (mx - PAD + wk_fw () / 2) / wk_fw ();
		caret = c < 0 ? 0 : c > len ? len : c;
		all = false;
		invalidate (true);
	}
	void erase () { len = caret = start = 0; text[0] = '\0'; all = false; }
	void setText (const char *s)
	{
		for (len = 0; s && s[len] && len < URL_MAX - 1; len++) text[len] = s[len];
		text[len] = '\0'; caret = len; start = 0; all = false;
	}
	// The editing keys; true: taken (*changed: the text changed)
	bool editKey (long k, bool *changed)
	{
		*changed = false;
		if (k == 1) { all = true; invalidate (true); return true; }				// ^A
		if (k == 3) { clip_copy (text, len); return true; }					// ^C
		if (k == 24) { clip_copy (text, len); erase (); *changed = true; invalidate (true); return true; }	// ^X
		if (k == 22)										// ^V
		{
			static char clip[URL_MAX];
			int type = 0; unsigned serial = 0;
			int n = kapi_clipboard_get (&type, clip, sizeof clip - 1, &serial);
			if (n > (int) sizeof clip - 1) n = (int) sizeof clip - 1;
			if (n > 0 && type == CLIP_TEXT)
			{
				if (all) erase ();
				for (int i = 0; i < n && len < URL_MAX - 1; )
				{
					int c = utf8_to_latin1 (clip, n, &i);
					if (c == '\r' || c == '\n' || c == '\t') c = ' ';
					if (c < 32 || (c >= 0x7F && c < 0xA0 && c != 0x80)) continue;
					char ch = (char) c;
					for (int m = len; m > caret; m--) text[m] = text[m - 1];
					text[caret++] = ch; len++;
				}
				text[len] = '\0';
				*changed = true;
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
			else break;
			*changed = true;
			break;
		case KEY_DEL:
			if (all) erase ();
			else if (caret < len) { for (int m = caret; m < len; m++) text[m] = text[m + 1]; len--; }
			else break;
			*changed = true;
			break;
		default:
			if (k < 32 || (k > 126 && k != 0x80 && k < 0xA0) || k > 0xFF) return false;	// (Latin-1, the euro)
			if (all) erase ();
			if (len >= URL_MAX - 1) return true;
			for (int m = len; m > caret; m--) text[m] = text[m - 1];
			text[caret++] = (char) k; len++; text[len] = '\0';
			*changed = true;
			break;
		}
		invalidate (true);
		return true;
	}
};

class UrlField : public LineEdit
{
public:
	char page[URL_MAX];	// the page's address (Esc goes back to it)
	bool joinL, joinR;	// the padlock / the pill against its left / right side (square there)
	UrlField (int l, int t, int w, int h) : LineEdit (l, t, w, h), joinL (false), joinR (false)
	{ page[0] = '\0'; }

	void setPage (const char *s)
	{
		int n = 0;
		for (; s && s[n] && n < URL_MAX - 1; n++) page[n] = s[n];
		page[n] = '\0';
		if (!hasFocus) { revert (); invalidate (true); }	// (not while it is being edited)
	}
	void revert () { setText (page); }
	void focusIn () { setFocus (); all = true; caret = len; invalidate (true); joined (); }
	void focusOut () { if (hasFocus) { hasFocus = false; revert (); invalidate (true); joined (); } }	// (back to the page)
	void joined ();		// the padlock and the pill redrawn (they show the field's focus)

	void onDraw () override
	{
		canvas.clear (bgColor ());
		// (a side joined to the padlock or the pill: the field's rounded end drawn past the
		// canvas -- cut off: the field and its neighbours read as one control)
		drawField (joinL ? -6 : 0, joinR ? 6 : 0, C_FIELD, width);
	}
	bool onMouse (int mx, int, int bl, int, int, int) override
	{
		if (mx < 0 || !bl) return mx >= 0;
		if (!hasFocus) { focusIn (); return true; }
		clickAt (mx);
		return true;
	}
	bool onKey (long k) override
	{
		if (k == KEY_ENTER)
		{
			static char u[URL_MAX * 3];
			latin1_to_utf8 (text, len, u, sizeof u);
			onyx_browser_go (u); focusOut (); return true;
		}
		if (k == 27) { focusOut (); return true; }
		bool ch;
		return editKey (k, &ch);
	}
};

// ---- the padlock and the site's version: half pills against the address field -------------
// The modes of utils/useragent.h (USER_AGENT_*: the C header is NetSurf's).
enum { MODE_NONE = -1, MODE_STANDARD = 0, MODE_MOBILE = 1, MODE_DESKTOP = 2, MODE_CUSTOM = 3 };
const char *const MODE_LABEL[] = { "Standard", "Mobile", "Desktop", "Custom" };
const char *const MODE_WHAT[] = { "Jet Browser's own User-Agent", "Chrome on Android",
				  "Chrome on Windows", "set in jet.ini" };

const unsigned SEC_GREEN = 0x1E8E3E, SEC_RED = 0xD93025;

UrlField *g_url;	// (the padlock and the pill show its focus: one control)

// The top shadow a sunken field has (wk_sunken), across [x0, x1)
void field_shadow (Canvas &cv, int x0, int x1)
{
	for (int i = x0; i < x1; i++) { wk_blend_px (cv, i, 1, 0, 34); wk_blend_px (cv, i, 2, 0, 14); }
}

// The padlock: a half pill on the field's left (rounded on its left end), the field's own
// face tinted -- green for a verified https page, red for one past a certificate warning,
// grey and struck for http; hidden for the rest. A click (Enter, Space) shows the page's
// certificates (the viewer, about:certificate).
class LockSeg : public Widget
{
public:
	int sec;
	char tipText[160];
	LockSeg (int l, int t, int w, int h) : Widget (l, t, w, h), sec (ONYX_SEC_NONE)
	{ canFocus = true; hidden = true; tipText[0] = '\0'; tip = tipText; }
	unsigned ink () const
	{
		return sec == ONYX_SEC_BROKEN ? SEC_RED : sec == ONYX_SEC_INSECURE ?
			wk_mix (C_FIELD_TEXT, C_FIELD, 110) : SEC_GREEN;
	}
	void set (int s)
	{
		if (s == sec) return;
		sec = s;
		const char *t = s == ONYX_SEC_SECURE ? "Secure connection: the certificate is valid. Click: the certificate" :
			s == ONYX_SEC_MIXED ? "Secure connection, but parts of the page came over http. Click: the certificate" :
			s == ONYX_SEC_BROKEN ? "Not secure: the certificate has a problem (accepted with Proceed). Click: the certificate" :
			"Not secure: the page came over http (not encrypted)";
		int i = 0;
		for (; t[i] && i < (int) sizeof tipText - 1; i++) tipText[i] = t[i];
		tipText[i] = '\0';
		invalidate (true);
	}
	void onDraw () override
	{
		canvas.clear (bgColor ());
		int r = height / 2, st = pressed ? 2 : hover ? 1 : 0;
		bool tint = sec != ONYX_SEC_INSECURE;
		unsigned face = tint ? wk_mix (C_FIELD, ink (), 30 + 18 * st) : st ? wk_tone (C_FIELD, 128 - 10 * st) : C_FIELD;
		bool focus = hasFocus || (g_url && g_url->hasFocus);
		wk_rbox (canvas, 0, 0, width + 6, height, r, face, face, 255, WK_TL | WK_BL);
		field_shadow (canvas, r, width);
		wk_rline (canvas, 0, 0, width + 6, height, r, focus ? C_ACCENT : wk_tone (C_FACE, 72),
			  focus ? 255 : 210, WK_TL | WK_BL);
		if (hasFocus) wk_rline (canvas, 1, 1, width + 4, height - 2, r - 1, C_ACCENT, 110, WK_TL | WK_BL);
		canvas.fillRect (width - 1, 6, 1, height - 12, wk_mix (C_FIELD, C_FIELD_TEXT, 48));	// (the join)
		int cx = width / 2 + 1, cy = height / 2;
		wk_glyph (canvas, WKG_LOCK, cx, cy, 14, ink ());
		if (sec == ONYX_SEC_INSECURE)		// (struck through: not secure)
			for (int k = -7; k <= 7; k++)
			{
				wk_blend_px (canvas, cx + k, cy + k, face, 255);
				wk_blend_px (canvas, cx + k + 1, cy + k, face, 255);
				wk_blend_px (canvas, cx + k, cy + k - 1, SEC_RED, 220);
			}
	}
	void click ();
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		if (mx < 0) { if (hover || pressed) { hover = pressed = false; invalidate (true); } return false; }
		bool wh = hover, wp = pressed;
		hover = mx < width && my >= 0 && my < height;
		if (bl && !pressed) pressed = true;
		else if (!bl && pressed) { pressed = false; if (hover) { invalidate (true); click (); return true; } }
		if (hover != wh || pressed != wp) invalidate (true);
		return true;
	}
	bool onKey (long k) override
	{
		if (k == KEY_ENTER || k == ' ') { click (); return true; }
		return false;
	}
};

// The pill's menu: the site's name, then the three versions -- the current one checked, each
// with what it sends; for a site jet.ini names ("Custom"), the versions greyed and why.
class ModeMenu : public Modal
{
public:
	enum { PADY = 4 };
	int cur, hot, col;	// col: where the versions' descriptions start
	char head[160];
	ModeMenu (int right, int y, int mode, const char *site) : Modal (100, 40), cur (mode), hot (-1)
	{
		const char *a = "Version of ";
		int n = 0;
		for (; *a; a++) head[n++] = *a;
		for (; site && *site && n < (int) sizeof head - 1; site++) head[n++] = *site;
		head[n] = '\0';
		int w = wk_text_w (head, 2) + 28;
		col = 0;
		for (int i = 0; i < 3; i++) { int t = wk_text_w (MODE_LABEL[i], 2); if (t > col) col = t; }
		col += 34 + 16;
		for (int i = 0; i < 3; i++)
		{
			int iw = col + wk_text_w (MODE_WHAT[i]) + 14;
			if (iw > w) w = iw;
		}
		if (cur == MODE_CUSTOM)
		{
			int iw = 28 + wk_text_w ("Set by jet.ini's [sites]: edit it there") ;
			if (iw > w) w = iw;
		}
		int h = 2 * PADY + rowH () * (cur == MODE_CUSTOM ? 5 : 4) + 9;
		resizeTo (w, h);
		left = right - w; top = y;
		if (left < 2) left = 2;
		if (cur >= 0 && cur < 3) hot = cur;
	}
	static int rowH () { return wk_fh () + 10; }
	int rowY (int i) const { return PADY + rowH () + 9 + i * rowH (); }	// version i (0..2)
	bool enabled () const { return cur != MODE_CUSTOM; }
	int rowAt (int mx, int my) const
	{
		if (!enabled () || mx < 0 || mx >= width) return -1;
		for (int i = 0; i < 3; i++) if (my >= rowY (i) && my < rowY (i) + rowH ()) return i;
		return -1;
	}
	void onDraw () override
	{
		int fh = wk_fh (), rh = rowH ();
		canvas.clear (WK_TRANSPARENT_KEY);
		wk_popup (canvas, 0, 0, width, height, 7, C_FIELD);
		wk_text_l (canvas, 14, PADY, rh, head, wk_mix (C_FIELD, C_FIELD_TEXT, 170), 2);
		wk_etch_h (canvas, 8, PADY + rh + 3, width - 16, C_FIELD);
		unsigned dim = wk_mix (C_FIELD, C_FIELD_TEXT, 110);
		for (int i = 0; i < 3; i++)
		{
			int y = rowY (i);
			bool h = i == hot && enabled ();
			if (h) wk_hilite (canvas, PADY, y, width - 2 * PADY, rh, 5, true);
			unsigned ink = !enabled () ? dim : h ? C_SEL_TEXT : C_FIELD_TEXT;
			if (i == cur) wk_glyph (canvas, WKG_CHECK, 19, y + rh / 2, 11, h ? C_SEL_TEXT : C_ACCENT);
			wk_text_l (canvas, 34, y, rh, MODE_LABEL[i], ink, 2);
			wk_text_l (canvas, col, y, rh, MODE_WHAT[i], h ? C_SEL_TEXT : dim);
		}
		if (cur == MODE_CUSTOM)
			wk_text_l (canvas, 14, rowY (3), rh, "Set by jet.ini's [sites]: edit it there", C_FIELD_TEXT);
		(void) fh;
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		bool in = mx >= 0 && my >= 0 && mx < width && my < height;
		int h = in ? rowAt (mx, my) : -1;
		if (h >= 0 && h != hot) { hot = h; invalidate (true); }
		if (bl && !pressed) { pressed = true; if (!in) close (0); }
		else if (!bl && pressed) { pressed = false; if (in && h >= 0) close (h + 1); }
		return true;
	}
	bool onKey (long k) override
	{
		if (k == 27 || k == 9) { close (0); return true; }
		if (!enabled ()) { if (k == KEY_ENTER || k == ' ') close (0); return true; }
		if (k == KEY_DOWN) { hot = hot < 0 ? 0 : (hot + 1) % 3; invalidate (true); }
		else if (k == KEY_UP) { hot = hot <= 0 ? 2 : hot - 1; invalidate (true); }
		else if ((k == KEY_ENTER || k == ' ') && hot >= 0) close (hot + 1);
		return true;
	}
};

// The site's version: a blue half pill on the field's right (rounded on its right end) -- the
// version's name and a small arrow; a click (Enter, Space, Down) opens its menu.
class ModePill : public Widget
{
public:
	int mode;
	bool menuOpen;		// (drawn pressed while its menu is shown)
	char tipText[200];
	ModePill (int l, int t, int w, int h) : Widget (l, t, w, h), mode (MODE_NONE), menuOpen (false)
	{ canFocus = true; hidden = true; tipText[0] = '\0'; tip = tipText; }
	static int needW ()
	{
		int w = 0;
		for (int i = 0; i < 4; i++) { int t = wk_text_w (MODE_LABEL[i], 2); if (t > w) w = t; }
		return 12 + w + 8 + 10 + BTN_H / 2 - 2;
	}
	void set (int m, const char *site)
	{
		if (m == mode) return;
		mode = m;
		if (m >= 0)
		{
			const char *parts[] = { "This site's version: ", MODE_LABEL[m], " (", MODE_WHAT[m],
				m == MODE_CUSTOM ? ": not changed here)" : "). Click to change it", 0 };
			int n = 0;
			for (int i = 0; parts[i]; i++)
				for (const char *q = parts[i]; *q && n < (int) sizeof tipText - 1; q++) tipText[n++] = *q;
			tipText[n] = '\0';
		}
		(void) site;
		invalidate (true);
	}
	void onDraw () override
	{
		canvas.clear (bgColor ());
		if (mode < 0) return;
		int r = height / 2, st = pressed || menuOpen ? 2 : hover ? 1 : 0;
		bool custom = mode == MODE_CUSTOM;
		unsigned top = custom ? 0x7C8899 : st == 2 ? 0x2459B8 : st == 1 ? 0x5C9BF5 : 0x4C8DF0;
		unsigned bot = custom ? 0x5E6B7D : st == 2 ? 0x2D68CF : st == 1 ? 0x2F6FDD : 0x2563D6;
		unsigned edge = custom ? 0x4E5A6A : 0x1C4FA8;
		wk_rbox (canvas, -6, 0, width + 6, height, r, top, bot, 255, WK_TR | WK_BR);
		for (int i = 1; i < width - r; i++) wk_blend_px (canvas, i, 1, 0xFFFFFF, st == 2 ? 20 : 70);	// (a light top)
		wk_rline (canvas, -6, 0, width + 6, height, r, edge, 255, WK_TR | WK_BR);
		canvas.fillRect (0, 0, 1, height, edge);				// (the join)
		if (hasFocus) wk_rline (canvas, 1, 2, width - 3, height - 4, r - 2, 0xFFFFFF, 170, WK_ALL);
		int off = st == 2 ? 1 : 0;
		wk_text_l (canvas, 11 + off, off, height, MODE_LABEL[mode], 0xFFFFFF, 2);
		if (!custom) wk_glyph (canvas, WKG_CHEV_DOWN, width - r + 1 + off, height / 2 + 1 + off, 9, 0xFFFFFF);
	}
	void openMenu ();
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		if (mx < 0) { if (hover || pressed) { hover = pressed = false; invalidate (true); } return false; }
		bool wh = hover, wp = pressed;
		hover = mx < width && my >= 0 && my < height;
		if (bl && !pressed) { pressed = true; invalidate (true); openMenu (); return true; }	// (opens on the press, as a menu)
		else if (!bl && pressed) pressed = false;
		if (hover != wh || pressed != wp) invalidate (true);
		return true;
	}
	bool onKey (long k) override
	{
		if (k == KEY_ENTER || k == ' ' || k == KEY_DOWN) { openMenu (); return true; }
		return false;
	}
};

void to_latin1 (const char *s, char *out, int cap);	// (below: UTF-8 -> the bitmap font's Latin-1)

// ---- the zoom control (docs/06 §38): "-  100%  +" -----------------------------------------
// Two small framed buttons and the zoom between them: - zooms out, + in (Chrome's steps), a
// click on the percentage goes back to 100 %. Ctrl+- / Ctrl++ / Ctrl+0 and Ctrl+wheel do the same.
class ZoomCtl : public Widget
{
public:
	enum { SEG = 26 };
	int pct, hot, down;		// the zoom; the part under the pointer, pressed (0 -, 1 %, 2 +)
	char label[8];
	ZoomCtl (int l, int t) : Widget (l, t, needW (), BTN_H), pct (100), hot (-1), down (-1)
	{ label[0] = '\0'; set (100); tip = "Zoom: - out (Ctrl+-), + in (Ctrl++), the % back to 100 % (Ctrl+0)"; }
	static int needW () { return 2 * SEG + wk_text_w ("500%", 2) + 12; }
	void set (int p)
	{
		pct = p;
		int n = 0, v = p;
		char t[6]; int k = 0;
		do { t[k++] = (char) ('0' + v % 10); v /= 10; } while (v && k < 5);
		while (k) label[n++] = t[--k];
		label[n++] = '%'; label[n] = '\0';
		invalidate (true);
	}
	int partAt (int mx) const { return mx < SEG ? 0 : mx >= width - SEG ? 2 : 1; }
	bool can (int part) const { return part == 0 ? pct > 25 : part == 2 ? pct < 500 : pct != 100; }
	void onDraw () override
	{
		canvas.clear (bgColor ());
		for (int part = 0; part <= 2; part += 2)
		{
			int x = part == 0 ? 0 : width - SEG;
			int st = !can (part) ? WK_DISABLED : down == part ? WK_PRESSED : hot == part ? WK_HOT : WK_NORMAL;
			int bx, by, bw, bh;
			wk_framed (canvas, x, 0, SEG, height, C_FACE, st, &bx, &by, &bw, &bh);
			wk_glyph (canvas, part == 0 ? WKG_MINUS : WKG_PLUS, bx + bw / 2, by + bh / 2, 11,
				  st == WK_DISABLED ? C_DIS : C_TEXT);
		}
		int lx = SEG + 2, lw = width - 2 * SEG - 4;
		if (hot == 1 && can (1)) wk_rbox (canvas, lx, 3, lw, height - 6, 5,
			wk_tone (C_BG, down == 1 ? 112 : 140), wk_tone (C_BG, down == 1 ? 104 : 132));
		wk_text_c (canvas, lx, 0, lw, height, label, pct == 100 ? C_TEXT : C_ACCENT, 2);
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		if (mx < 0) { if (hot >= 0 || down >= 0) { hot = down = -1; invalidate (true); } return false; }
		int h = my >= 0 && my < height && mx < width ? partAt (mx) : -1, wh = hot, wd = down;
		hot = h;
		if (bl && down < 0) down = h;
		else if (!bl && down >= 0)
		{
			int was = down; down = -1;
			if (was == h && can (h)) onyx_browser_zoom (h == 0 ? -1 : h == 2 ? 1 : 0);
		}
		if (hot != wh || down != wd) invalidate (true);
		return true;
	}
};

// ---- the downloads (docs/06 §38): the toolbar's button and its menu -------------------------
// The list gui.c's onyx_download.c publishes (onyx_chrome_downloads), copied.
struct DlRow
{
	int id, state;
	char name[64], path[512], error[160];
	unsigned long long got, total;
};
enum { DL_ROWS = 16 };
DlRow g_dl[DL_ROWS];
int g_ndl;

// "1.2 MB", "340 KB", "87 bytes"
void tell_size (unsigned long long n, char *out, int cap)
{
	const char *u = n >= 1048576 ? "MB" : n >= 1024 ? "KB" : "bytes";
	unsigned long long whole = n >= 1048576 ? n / 1048576 : n >= 1024 ? n / 1024 : n;
	unsigned tenth = n >= 1048576 ? (unsigned) (n % 1048576 * 10 / 1048576) : 0;
	char t[24]; int k = 0, o = 0;
	do { t[k++] = (char) ('0' + whole % 10); whole /= 10; } while (whole && k < 20);
	while (k && o < cap - 12) out[o++] = t[--k];
	if (n >= 1048576 && whole < 100) { out[o++] = '.'; out[o++] = (char) ('0' + tenth); }
	out[o++] = ' ';
	for (; *u && o < cap - 1; u++) out[o++] = *u;
	out[o] = '\0';
}

// A download's state in words: "1.2 MB of 2.6 MB (45%)", "Done: 2.6 MB", "Failed: ...", "Cancelled"
void tell_dl (const DlRow &d, char *out, int cap)
{
	char a[24], b[24];
	int o = 0;
	auto put = [&] (const char *s) { for (; *s && o < cap - 1; s++) out[o++] = *s; out[o] = '\0'; };
	out[0] = '\0';
	tell_size (d.got, a, sizeof a);
	switch (d.state)
	{
	case ONYX_DL_ASK: put ("Waiting for a place to save it..."); break;
	case ONYX_DL_RUNNING:
		put (a);
		if (d.total > 0)
		{
			tell_size (d.total, b, sizeof b);
			put (" of "); put (b);
			char pc[8]; int p = (int) (d.got * 100 / d.total), k = 0;
			if (p > 100) p = 100;
			pc[k++] = ' '; pc[k++] = '(';
			if (p >= 100) pc[k++] = '1';
			if (p >= 10) pc[k++] = (char) ('0' + p / 10 % 10);
			pc[k++] = (char) ('0' + p % 10); pc[k++] = '%'; pc[k++] = ')'; pc[k] = '\0';
			put (pc);
		}
		break;
	case ONYX_DL_DONE: put ("Done: "); put (a); put (" in "); put (d.path); break;
	case ONYX_DL_FAILED: put ("Failed: "); put (d.error[0] ? d.error : "an error"); break;
	default: put ("Cancelled"); break;
	}
}

bool dl_active (const DlRow &d) { return d.state == ONYX_DL_ASK || d.state == ONYX_DL_RUNNING; }

// The button: an arrow down; while a download runs, its progress along the button's foot.
class DlButton : public ToolButton
{
public:
	DlButton (int l, int t, void (*c) (void)) : ToolButton (l, t, WKG_DOWN, c)
	{ hidden = true; tip = "Downloads"; }
	void onDraw () override
	{
		ToolButton::onDraw ();
		unsigned long long got = 0, total = 0;
		bool run = false, fail = false;
		for (int i = 0; i < g_ndl; i++)
		{
			if (dl_active (g_dl[i])) { run = true; got += g_dl[i].got; total += g_dl[i].total ? g_dl[i].total : g_dl[i].got + 1; }
			if (i == 0 && g_dl[i].state == ONYX_DL_FAILED) fail = true;
		}
		if (run)
		{
			int w = width - 12, fill = total ? (int) (got * (unsigned long long) w / total) : 0;
			canvas.fillRect (6, height - 7, w, 3, wk_tone (C_FACE, 96));
			canvas.fillRect (6, height - 7, fill, 3, C_ACCENT);
		}
		else if (g_ndl > 0)		// (the last one's end: a dot, green or red)
			canvas.fillRect (width - 10, 5, 4, 4, fail ? SEC_RED : SEC_GREEN);
	}
};

// Its menu: the downloads, the newest first -- a name, its state under it; a click on one that
// runs (or Del) cancels it, after a question; "Clear the list" takes the finished ones out.
class DlMenu : public Modal
{
public:
	enum { PADY = 4 };
	int hot;
	DlMenu (int right, int y) : Modal (380, 40), hot (-1)
	{
		int w = 380;
		for (int i = 0; i < g_ndl; i++)
		{
			char t[700]; tell_dl (g_dl[i], t, sizeof t);
			int iw = wk_text_w (t) + 28, nw = wk_text_w (g_dl[i].name, 2) + 28;
			if (iw > w) w = iw;
			if (nw > w) w = nw;
		}
		if (w > 640) w = 640;
		resizeTo (w, 2 * PADY + rowH () * (g_ndl > 0 ? g_ndl : 1) + clearH ());
		left = right - w; top = y;
		if (left < 2) left = 2;
	}
	static int rowH () { return 2 * wk_fh () + 12; }
	static bool anyEnded () { for (int i = 0; i < g_ndl; i++) if (!dl_active (g_dl[i])) return true; return false; }
	int clearH () const { return anyEnded () ? wk_fh () + 18 : 0; }	// ("Clear the list": the ended ones)
	int rowAt (int my) const
	{
		if (my < PADY) return -1;
		int r = (my - PADY) / rowH ();
		if (r < g_ndl) return r;
		return clearH () > 0 && my >= height - clearH () ? DL_ROWS : -1;	// (Clear the list)
	}
	void onDraw () override
	{
		int fh = wk_fh (), rh = rowH ();
		canvas.clear (WK_TRANSPARENT_KEY);
		wk_popup (canvas, 0, 0, width, height, 7, C_FIELD);
		unsigned dim = wk_mix (C_FIELD, C_FIELD_TEXT, 110);
		if (g_ndl == 0) wk_text_l (canvas, 14, PADY, rh, "No downloads.", dim);
		for (int i = 0; i < g_ndl; i++)
		{
			int y = PADY + i * rh;
			bool h = i == hot && dl_active (g_dl[i]);
			if (h) wk_hilite (canvas, PADY, y, width - 2 * PADY, rh, 5, true);
			char t[700]; tell_dl (g_dl[i], t, sizeof t);
			char line[200]; fit (line, t, (width - 28) / wk_fw ());
			wk_text_l (canvas, 14, y + 4, fh, g_dl[i].name, h ? C_SEL_TEXT : C_FIELD_TEXT, 2);
			wk_text_l (canvas, 14, y + 6 + fh, fh, line, h ? C_SEL_TEXT :
				   g_dl[i].state == ONYX_DL_FAILED ? SEC_RED : dim);
			if (h) wk_text_l (canvas, width - 20 - wk_text_w ("Cancel"), y + 4, fh, "Cancel", C_SEL_TEXT);
			if (i + 1 < g_ndl) canvas.fillRect (10, y + rh - 1, width - 20, 1, wk_mix (C_FIELD, C_FIELD_TEXT, 28));
		}
		if (clearH () > 0)
		{
			int y = height - clearH ();
			wk_etch_h (canvas, 8, y, width - 16, C_FIELD);
			if (hot == DL_ROWS) wk_hilite (canvas, PADY, y + 3, width - 2 * PADY, clearH () - 6, 5, true);
			wk_text_l (canvas, 14, y + 3, clearH () - 6, "Clear the list", hot == DL_ROWS ? C_SEL_TEXT : C_FIELD_TEXT);
		}
	}
	void fit (char *out, const char *s, int max)
	{
		int n = wk_len (s);
		if (max < 4) max = 4;
		if (n <= max) { for (int i = 0; i <= n; i++) out[i] = s[i]; return; }
		int k = 0;
		for (; k < max - 3 && k < 196; k++) out[k] = s[k];
		out[k++] = '.'; out[k++] = '.'; out[k++] = '.'; out[k] = '\0';
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		bool in = mx >= 0 && my >= 0 && mx < width && my < height;
		int h = in ? rowAt (my) : -1;
		if (h != hot) { hot = h; invalidate (true); }
		if (bl && !pressed) { pressed = true; if (!in) close (0); }
		else if (!bl && pressed)
		{
			pressed = false;
			if (in && h == DL_ROWS) close (DL_ROWS + 1);
			else if (in && h >= 0 && dl_active (g_dl[h])) close (h + 1);
		}
		return true;
	}
	bool onKey (long k) override
	{
		if (k == 27 || k == 9 || k == KEY_ENTER) close (0);
		return true;
	}
};

// ---- the status bar (docs/06 §38) ------------------------------------------------------------
// The window's bottom band: the page's state on the left ("Loading... 12 of 30", "Ready", "404
// Not Found" in red) -- or, while the pointer is on a link, its address (cut in its middle when
// too long); on the right a download's progress. Repainted alone, when its text changes.
class StatusBar : public Widget
{
public:
	char state[160], link[URL_MAX], right[200];
	bool error;
	StatusBar (int l, int t, int w) : Widget (l, t, w, SB), error (false)
	{ state[0] = link[0] = right[0] = '\0'; anchor = ANCHOR_LEFT | ANCHOR_RIGHT | ANCHOR_BOTTOM; }
	static bool same (const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
	static void copy (char *d, const char *s, int cap) { int i = 0; for (; s && s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = '\0'; }
	void setState (const char *t, bool err)
	{
		char b[160]; to_latin1 (t ? t : "", b, sizeof b);
		if (same (b, state) && err == error) return;
		copy (state, b, sizeof state); error = err;
		if (!link[0]) invalidate (true);
	}
	void setLink (const char *t)
	{
		static char b[URL_MAX];
		to_latin1 (t ? t : "", b, sizeof b);
		if (same (b, link)) return;
		copy (link, b, sizeof link);
		invalidate (true);
	}
	void setRight (const char *t)
	{
		if (same (t, right)) return;
		copy (right, t, sizeof right);
		invalidate (true);
	}
	// s in at most max columns: its middle replaced by "..." when too long
	static void middle (char *out, const char *s, int max)
	{
		int n = wk_len (s);
		if (max < 7) max = 7;
		if (n <= max) { copy (out, s, URL_MAX); return; }
		int head = (max - 3) * 3 / 5, tail = max - 3 - head, o = 0;
		for (int i = 0; i < head; i++) out[o++] = s[i];
		out[o++] = '.'; out[o++] = '.'; out[o++] = '.';
		for (int i = n - tail; i < n; i++) out[o++] = s[i];
		out[o] = '\0';
	}
	void onDraw () override
	{
		canvas.clear (C_BG);
		wk_etch_h (canvas, 0, 0, width, C_BG);
		int fh = wk_fh (), ty = 2, th = height - 2;
		int rw = right[0] ? wk_text_w (right) + 16 : 0;
		if (rw > width / 2) rw = width / 2;
		int avail = width - 16 - rw;
		static char line[URL_MAX];
		if (link[0])
		{
			middle (line, link, avail / wk_fw ());
			wk_text_l (canvas, 8, ty, th, line, C_TEXT);
		}
		else
		{
			middle (line, state, avail / wk_fw ());
			wk_text_l (canvas, 8, ty, th, line, error ? 0xC5221F : wk_mix (C_BG, C_TEXT, 200));
		}
		if (rw)
		{
			canvas.fillRect (width - rw, 5, 1, height - 8, wk_tone (C_BG, 100));
			char r[200]; middle (r, right, (rw - 16) / wk_fw ());
			wk_text_l (canvas, width - rw + 8, ty, th, r, wk_mix (C_BG, C_TEXT, 200));
		}
		(void) fh;
#ifdef ONYX_HOST_SIM
		printf ("ONYX-STATUSBAR %s=%s%s%s%s\n", link[0] ? "link" : "state", link[0] ? link : state,
			!link[0] && error ? " (error)" : "", right[0] ? " | " : "", right);
		fflush (stdout);
#endif
	}
};

// ---- the find bar (docs/06 §40) -------------------------------------------------------------
// Above the status bar while shown (Ctrl+F, Edit > Find in Page...; the page that much shorter):
// the words to find (Latin-1, as the address field; searched as UTF-8), "3 of 17" in the field's
// right end -- the field tinted red when nothing matches --, the previous / next match (Shift+
// Enter / Enter, Shift+F3 / F3), Match case, and x (Esc) that closes it and clears the
// highlights. Typing searches as it goes (gui.c waits for a pause on a page slow to search).
const int FB = 30;			// its height
const int FIND_W = 300;			// the field's width
void find_typed ();
void find_step (int dir);
void find_close ();

class FindField : public LineEdit
{
public:
	int idx, cnt;		// the current match (0-based, -1 none), how many (-1: not searched)
	FindField (int l, int t, int w, int h) : LineEdit (l, t, w, h), idx (-1), cnt (-1) {}
	void count (char *out) const
	{
		if (cnt < 0 || len == 0) { out[0] = '\0'; return; }
		if (cnt == 0) { const char *t = "No matches"; int i = 0; for (; t[i]; i++) out[i] = t[i]; out[i] = '\0'; return; }
		char a[12], b[12]; int na = 0, nb = 0, v = idx + 1, o = 0;
		do { a[na++] = (char) ('0' + v % 10); v /= 10; } while (v && na < 11);
		v = cnt;
		do { b[nb++] = (char) ('0' + v % 10); v /= 10; } while (v && nb < 11);
		while (na) out[o++] = a[--na];
		out[o++] = ' '; out[o++] = 'o'; out[o++] = 'f'; out[o++] = ' ';
		while (nb) out[o++] = b[--nb];
		out[o] = '\0';
	}
	void onDraw () override
	{
		canvas.clear (bgColor ());
		char c[32]; count (c);
		int cw = c[0] ? wk_text_w (c) + 10 : 0;
		bool none = cnt == 0 && len > 0;
		drawField (0, 0, none ? wk_mix (C_FIELD, 0xF28B82, 96) : C_FIELD, width - cw);
		if (cw)
			wk_text_l (canvas, width - cw, 0, height, c,
				   none ? 0xC5221F : wk_mix (C_FIELD, C_FIELD_TEXT, 150));
	}
	bool onMouse (int mx, int, int bl, int, int, int) override
	{
		if (mx < 0 || !bl) return mx >= 0;
		if (!hasFocus) { setFocus (); all = true; caret = len; invalidate (true); return true; }
		clickAt (mx);
		return true;
	}
	bool onKey (long k) override
	{
		if (k == KEY_ENTER) { find_step ((kapi_get_modifiers () & MOD_SHIFT) ? -1 : 1); return true; }
		if (k == 27) { find_close (); return true; }
		bool ch;
		bool took = editKey (k, &ch);
		if (ch) { cnt = -1; idx = -1; invalidate (true); find_typed (); }
		return took;
	}
};

// Match case: a check box and its words
class CaseToggle : public Widget
{
public:
	bool on;
	CaseToggle (int l, int t) : Widget (l, t, 24 + wk_text_w ("Match case"), 24), on (false)
	{ tip = "Upper and lower case, and accents, as typed"; }
	void onDraw () override
	{
		canvas.clear (bgColor ());
		wk_check_mark (canvas, 2, (height - 14) / 2, 14, on, pressed ? WK_PRESSED : hover ? WK_HOT : WK_NORMAL);
		wk_text_l (canvas, 22, 0, height, "Match case", C_TEXT);
	}
	bool onMouse (int mx, int, int bl, int, int, int) override
	{
		if (mx < 0) { if (hover || pressed) { hover = pressed = false; invalidate (true); } return false; }
		bool wh = hover, wp = pressed;
		hover = true;
		if (bl && !pressed) pressed = true;
		else if (!bl && pressed) { pressed = false; on = !on; invalidate (true); find_typed (); }
		if (hover != wh || pressed != wp) invalidate (true);
		return true;
	}
};

void find_prev () { find_step (-1); }
void find_next () { find_step (1); }

class FindBar : public Widget
{
public:
	FindField  *field;
	ToolButton *prev, *next, *close;
	CaseToggle *mcase;
	FindBar (int l, int t, int w) : Widget (l, t, w, FB)
	{
		anchor = ANCHOR_LEFT | ANCHOR_RIGHT | ANCHOR_BOTTOM;
		int y = 3, h = FB - 6, x = 8;
		field = new FindField (x, y, FIND_W, h);		x += FIND_W + 6;
		prev  = new ToolButton (x, y, 28, h, WKG_CHEV_UP, find_prev);	x += 28 + 2;
		next  = new ToolButton (x, y, 28, h, WKG_CHEV_DOWN, find_next);	x += 28 + 12;
		mcase = new CaseToggle (x, y);
		close = new ToolButton (w - 8 - 28, y, 28, h, WKG_CLOSE, find_close);
		close->anchor = ANCHOR_RIGHT | ANCHOR_TOP;
		prev->tip = "Previous match (Shift+Enter, Shift+F3)";
		next->tip = "Next match (Enter, F3)";
		close->tip = "Close the find bar (Esc)";
		addChild (field); addChild (prev); addChild (next); addChild (mcase); addChild (close);
	}
	void onDraw () override
	{
		canvas.clear (C_BG);
		wk_etch_h (canvas, 0, 0, width, C_BG);
	}
};

// ---- the history dialog --------------------------------------------------------------------
// A page visited: its address, its title and its time, as the dialog shows them (the wtk font
// is Latin-1: the UTF-8 title converted).
struct Visit
{
	char *url;		// the address (to go back to it)
	char  title[160];	// Latin-1
	char  when[24];		// "14:05", "Mon 14:05", "12 Sep", "12 Sep 2025"
	char  low[160];		// the title, lower case (the filter)
};

// UTF-8 -> Latin-1 (the wtk font's): a few typographic marks folded to ASCII, the rest '?'.
void to_latin1 (const char *s, char *out, int cap)
{
	const unsigned char *p = (const unsigned char *) s;
	int n = 0;
	while (p && *p && n < cap - 1)
	{
		unsigned c = *p++;
		if (c >= 0x80)
		{
			int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
			if (extra == 0) c = '?';
			else c &= extra == 3 ? 0x07 : extra == 2 ? 0x0F : 0x1F;
			for (; extra > 0 && (*p & 0xC0) == 0x80; extra--) c = (c << 6) | (*p++ & 0x3F);
			if (c == 0x2018 || c == 0x2019) c = '\'';
			else if (c == 0x201C || c == 0x201D) c = '"';
			else if (c == 0x2013 || c == 0x2014 || c == 0x2022) c = '-';
			else if (c == 0x2026 && n < cap - 3) { out[n++] = '.'; out[n++] = '.'; c = '.'; }
			else if (c == 0xA0) c = ' ';
			else if (c > 0xFF) c = '?';
		}
		if (c < 32) c = ' ';
		if (c == ' ' && (n == 0 || out[n - 1] == ' ')) continue;	// (spaces squashed)
		out[n++] = (char) c;
	}
	while (n > 0 && out[n - 1] == ' ') n--;
	out[n] = '\0';
}

char lower_l1 (char ch)
{
	unsigned c = (unsigned char) ch;
	if ((c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xDE && c != 0xD7)) c += 32;
	return (char) c;
}

// Does `hay` (lower case) contain `needle` (as typed)?
bool contains (const char *hay, const char *needle)
{
	if (needle[0] == '\0') return true;
	for (; *hay; hay++)
	{
		int i = 0;
		while (needle[i] && hay[i] && hay[i] == lower_l1 (needle[i])) i++;
		if (needle[i] == '\0') return true;
	}
	return false;
}

// `s` in at most `max` characters (an ellipsis at the end when cut).
void fit (char *out, const char *s, int max)
{
	int n = wk_len (s);
	if (max < 4) max = 4;
	if (n <= max) { for (int i = 0; i <= n; i++) out[i] = s[i]; return; }
	int k = 0;
	for (; k < max - 3; k++) out[k] = s[k];
	out[k++] = '.'; out[k++] = '.'; out[k++] = '.'; out[k] = '\0';
}

// When a page was visited, told the way a person would: the time today, the day this week,
// else the date.
void tell_time (long long when, char *out)
{
	static const char *const MON[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
					   "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
	static const char *const DAY[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
	time_t t = (time_t) when, now = time (0);
	struct tm a, b;
	gmtime_r (&t, &a);		// (Onyx's clock is the local time, counted as UTC)
	gmtime_r (&now, &b);
	long days = (long) (now / 86400) - (long) (t / 86400);
	char *o = out;
	if (days == 0 || days < 0) { }
	else if (days < 7) { for (const char *d = DAY[a.tm_wday]; *d; ) *o++ = *d++; *o++ = ' '; }
	else
	{
		if (a.tm_mday >= 10) *o++ = (char) ('0' + a.tm_mday / 10);
		*o++ = (char) ('0' + a.tm_mday % 10); *o++ = ' ';
		for (const char *m = MON[a.tm_mon % 12]; *m; ) *o++ = *m++;
		if (a.tm_year != b.tm_year)
		{
			int y = a.tm_year + 1900;
			*o++ = ' '; *o++ = (char) ('0' + y / 1000 % 10); *o++ = (char) ('0' + y / 100 % 10);
			*o++ = (char) ('0' + y / 10 % 10); *o++ = (char) ('0' + y % 10);
		}
		*o = '\0';
		return;
	}
	*o++ = (char) ('0' + a.tm_hour / 10); *o++ = (char) ('0' + a.tm_hour % 10); *o++ = ':';
	*o++ = (char) ('0' + a.tm_min / 10);  *o++ = (char) ('0' + a.tm_min % 10);
	*o = '\0';
}

class HistoryDialog;

// The list: a page a row -- its title, its address under it (dimmer), its time at the right.
// Click selects, double-click / Enter opens; the wheel, the scroll bar and the keys scroll.
class HistoryList : public Widget
{
public:
	HistoryDialog *dlg;
	int sel, top;
	bool thumb;
	unsigned lastClick; int lastRow;
	HistoryList (int l, int t, int w, int h, HistoryDialog *d)
	  : Widget (l, t, w, h), dlg (d), sel (-1), top (0), thumb (false), lastClick (0), lastRow (-1)
	{ canFocus = true; }
	static int rowH () { return 2 * wk_fh () + 10; }
	int rows () const { int r = (height - 4) / rowH (); return r < 1 ? 1 : r; }
	int count () const;
	void scrollTo (int t)
	{
		int mx = count () - rows (); if (mx < 0) mx = 0;
		if (t > mx) t = mx;
		if (t < 0) t = 0;
		if (t != top) { top = t; invalidate (true); }
	}
	void select (int i)
	{
		int n = count ();
		if (n == 0) { sel = -1; invalidate (true); return; }
		if (i < 0) i = 0;
		if (i >= n) i = n - 1;
		sel = i;
		if (sel < top) scrollTo (sel);
		if (sel >= top + rows ()) scrollTo (sel - rows () + 1);
		invalidate (true);
	}
	void onDraw () override;
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override;
	bool onKey (long k) override;
};

// The filter: a Textbox that tells the dialog when its text changed.
class FilterBox : public Textbox
{
public:
	HistoryDialog *dlg;
	FilterBox (int l, int t, int w, int h, HistoryDialog *d) : Textbox (l, t, w, h, ""), dlg (d) {}
	bool onKey (long k) override;
};

void hd_button (Widget &w) { if (w.parent) ((Modal *) w.parent)->onButton (w.tag); }

class HistoryDialog : public Modal
{
public:
	enum { OPEN = 1, DELETE = 2, CLEAR = 3 };
	Visit *all; int n, cap;			// every page, the most recent first
	int *vis; int nvis;			// the ones the filter lets through
	HistoryList *list;
	FilterBox *filter;
	char *chosen;				// the address to go to (Open), or 0
	HistoryDialog (int W, int H) : Modal (W < 760 + 40 ? W - 40 : 760, H < 540 + 40 ? H - 40 : 540),
		all (0), n (0), cap (0), vis (0), nvis (0), chosen (0)
	{
		left = (W - width) / 2; top = (H - height) / 2;
		int fh = wk_fh (), y = titleH () + 10;
		filter = new FilterBox (70, y, width - 80, fh + 10, this);
		addChild (filter);
		y += fh + 18;
		list = new HistoryList (10, y, width - 20, height - y - 50, this);
		addChild (list);
		int by = height - 38;
		Button *b;
		b = new Button (10, by, 82, 28, "Open", hd_button);        b->tag = OPEN;   addChild (b);
		b = new Button (100, by, 82, 28, "Delete", hd_button);     b->tag = DELETE; addChild (b);
		b = new Button (190, by, 96, 28, "Clear all", hd_button);  b->tag = CLEAR;  addChild (b);
		b = new Button (width - 92, by, 82, 28, "Close", hd_button); b->tag = 0;    addChild (b);
		load ();
	}
	~HistoryDialog ()
	{
		for (int i = 0; i < n; i++) delete [] all[i].url;
		delete [] all; delete [] vis; delete [] chosen;
	}
	static void add (void *ctx, const char *url, const char *title, long long when)
	{
		HistoryDialog *d = (HistoryDialog *) ctx;
		if (d->n == d->cap)
		{
			int nc = d->cap ? d->cap * 2 : 128;
			Visit *nv = new Visit[nc];
			for (int i = 0; i < d->n; i++) nv[i] = d->all[i];
			delete [] d->all; d->all = nv; d->cap = nc;
		}
		Visit &v = d->all[d->n++];
		int len = wk_len (url);
		v.url = new char[len + 1];
		for (int i = 0; i <= len; i++) v.url[i] = url[i];
		to_latin1 (title, v.title, sizeof v.title);
		if (v.title[0] == '\0') to_latin1 (url, v.title, sizeof v.title);	// (no title: its address)
		int i = 0;
		for (; v.title[i]; i++) v.low[i] = lower_l1 (v.title[i]);
		v.low[i] = '\0';
		tell_time (when, v.when);
	}
	void load ()
	{
		for (int i = 0; i < n; i++) delete [] all[i].url;
		n = 0;
		onyx_browser_history (add, this);
		delete [] vis;
		vis = new int[n > 0 ? n : 1];
		refilter ();
	}
	void refilter ()
	{
		const char *f = filter->text;
		nvis = 0;
		for (int i = 0; i < n; i++)
			if (contains (all[i].low, f) || contains (all[i].url, f)) vis[nvis++] = i;
		list->top = 0;
		list->select (0);
		invalidate (true);
	}
	Visit *at (int row) { return row >= 0 && row < nvis ? &all[vis[row]] : 0; }
	void open (int row)
	{
		Visit *v = at (row);
		if (v == 0) return;
		int len = wk_len (v->url);
		chosen = new char[len + 1];
		for (int i = 0; i <= len; i++) chosen[i] = v->url[i];
		close (OPEN);
	}
	void forget (int row)
	{
		Visit *v = at (row);
		if (v == 0) return;
		onyx_browser_history_forget (v->url);
		int keep = list->sel;
		load ();
		list->select (keep);
	}
	void onButton (int tag) override
	{
		if (tag == OPEN) open (list->sel);
		else if (tag == DELETE) forget (list->sel);
		else if (tag == CLEAR)
		{
			if (n > 0 && wk_messagebox ("Clear history", "Forget every page visited?", MB_YESNO) == 1)
			{ onyx_browser_history_clear (); load (); }
			filter->setFocus ();
		}
		else close (0);
	}
	bool onKey (long k) override
	{
		if (k == 27) { close (0); return true; }
		if (k == KEY_ENTER) { open (list->sel); return true; }
		return list->onKey (k);			// (the filter has the focus: the list's keys)
	}
	bool onMouse (int, int, int, int, int, int) override { return true; }	// (modal: all of it)
	void onDraw () override
	{
		drawBox ("History");
		int fh = wk_fh ();
		wk_text_l (canvas, 12, filter->top, filter->height, "Find:", C_TEXT);
		char buf[64];
		int k = 0, v = nvis;
		char num[12]; int m = 0;
		do { num[m++] = (char) ('0' + v % 10); v /= 10; } while (v && m < 11);
		while (m > 0) buf[k++] = num[--m];
		const char *tail = nvis == 1 ? " page" : " pages";
		for (int i = 0; tail[i]; i++) buf[k++] = tail[i];
		buf[k] = '\0';
		wk_text_l (canvas, width - 102 - wk_text_w (buf), height - 38, 28, buf, C_DIS);
		(void) fh;
	}
};

int HistoryList::count () const { return dlg->nvis; }

void HistoryList::onDraw ()
{
	int fw = wk_fw (), fh = wk_fh (), rh = rowH (), R = rows ();
	WkThumb t = wk_thumb (count (), R, top, height - 4);
	int tw = width - (t.show ? WK_SBW + 2 : 0);
	canvas.clear (bgColor ());
	wk_sunken (canvas, 0, 0, width, height, 4, C_FIELD, hasFocus);
	if (count () == 0)
	{
		const char *e = dlg->n == 0 ? "No page visited yet." : "No page matches.";
		wk_text_c (canvas, 0, 0, width, height, e, C_DIS);
		return;
	}
	for (int r = 0; r < R && top + r < count (); r++)
	{
		int i = top + r, y = 2 + r * rh;
		Visit *v = dlg->at (i);
		bool s = i == sel;
		if (s) wk_hilite (canvas, 3, y + 1, tw - 6, rh - 2, 4, true);
		unsigned ink = s ? wk_hilite_ink (true) : C_FIELD_TEXT;
		unsigned dim = s ? wk_hilite_ink (true) : wk_mix (C_FIELD_TEXT, C_FIELD, 110);
		int tcols = (tw - 16) / fw - wk_len (v->when) - 2;
		char line[200];
		fit (line, v->title, tcols);
		wk_text_l (canvas, 10, y + 4, fh, line, ink, 2);			// the title, bold
		wk_text_l (canvas, tw - 8 - wk_text_w (v->when), y + 4, fh, v->when, dim);
		fit (line, v->url, (tw - 16) / fw);
		wk_text_l (canvas, 10, y + 5 + fh, fh, line, dim);		// its address
		if (!s && top + r + 1 < count ())
			canvas.fillRect (8, y + rh - 1, tw - 16, 1, wk_mix (C_FIELD, C_FIELD_TEXT, 28));
	}
	if (t.show) wk_draw_vscroll (canvas, width - WK_SBW - 2, 2, WK_SBW, height - 4, t, C_FIELD, thumb);
}

bool HistoryList::onMouse (int mx, int my, int bl, int, int, int wheel)
{
	if (mx < 0) { pressed = false; thumb = false; return false; }
	if (wheel) { scrollTo (top - wheel); return true; }
	WkThumb t = wk_thumb (count (), rows (), top, height - 4);
	if (thumb)
	{
		if (!bl) { thumb = false; invalidate (true); }
		else scrollTo ((int) wk_thumb_pos (my - 2, height - 4, count (), rows (), t.h));
		return true;
	}
	if (bl && !pressed)
	{
		pressed = true; setFocus ();
		if (t.show && mx >= width - WK_SBW - 2)
		{ thumb = true; invalidate (true); scrollTo ((int) wk_thumb_pos (my - 2, height - 4, count (), rows (), t.h)); return true; }
		int i = top + (my - 2) / rowH ();
		if (i >= count ()) return true;
		unsigned now = kapi_get_ticks ();
		bool dbl = i == lastRow && now - lastClick < 70;
		select (i);
		if (dbl) { dlg->open (i); lastRow = -1; }
		else { lastRow = i; lastClick = now; }
	}
	else if (!bl) pressed = false;
	return true;
}

bool HistoryList::onKey (long k)
{
	int R = rows ();
	switch (k)
	{
	case KEY_UP:   select (sel - 1); return true;
	case KEY_DOWN: select (sel + 1); return true;
	case KEY_PGUP: select (sel - R); return true;
	case KEY_PGDN: select (sel + R); return true;
	case KEY_HOME: select (0); return true;
	case KEY_END:  select (count () - 1); return true;
	case KEY_ENTER: dlg->open (sel); return true;
	case KEY_DEL:  if (hasFocus) dlg->forget (sel); return true;	// (the filter's Del edits it)
	}
	if ((k >= 32 && k <= 126) || (k >= 0xA0 && k <= 0xFF) || k == KEY_BACKSPACE)
	{	// typing in the list: into the filter
		dlg->filter->setFocus ();
		return dlg->filter->onKey (k);
	}
	return false;
}

bool FilterBox::onKey (long k)
{
	char was[64];
	for (int i = 0; i < 64; i++) was[i] = text[i];
	if (k == KEY_ENTER) return false;			// (the dialog's: open the page)
	bool r = Textbox::onKey (k);
	for (int i = 0; i < 64; i++)
		if (was[i] != text[i]) { dlg->refilter (); break; }
		else if (was[i] == '\0') break;
	return r;
}

void open_history ();
void open_downloads ();		// (the downloads' button, File > Downloads...)

// ---- the window ----------------------------------------------------------------------------
bool g_resized;

class NsWindow : public Root
{
public:
	ToolButton *back, *fwd, *reload, *home, *hist;
	UrlField   *url;
	LockSeg    *lock;
	ModePill   *mode;
	ZoomCtl    *zoom;		// (docs/06 §38) the zoom control, right of the field
	DlButton   *dl;		// ... the downloads' button, at the right end (while there are some)
	StatusBar  *status;		// ... the status bar, at the bottom (View > Status Bar)
	FindBar    *find;		// (docs/06 §40) the find bar, above the status bar (Ctrl+F)
	int         urlX;		// where the band's right part starts (the padlock, the field)
	bool        busy;
	NsWindow (int w, int h) : Root (w, h + TB + (g_sbOn ? SB : 0), "Jet"), busy (false)
	{
		int x = PAD;
		back   = new ToolButton (x, BTN_Y, WKG_CHEV_LEFT, onyx_browser_back);     x += BTN_W + GAP;
		fwd    = new ToolButton (x, BTN_Y, WKG_CHEV_RIGHT, onyx_browser_forward); x += BTN_W + GAP;
		reload = new ToolButton (x, BTN_Y, WKG_RELOAD, reload_or_stop);           x += BTN_W + GAP;
		home   = new ToolButton (x, BTN_Y, WKG_HOME, onyx_browser_home);          x += BTN_W + GAP;
		hist   = new ToolButton (x, BTN_Y, WKG_HISTORY, open_history);             x += BTN_W + GAP + 2;
		urlX   = x;
		url    = new UrlField (x, BTN_Y, width - x - PAD, BTN_H);
		url->anchor = ANCHOR_LEFT | ANCHOR_RIGHT | ANCHOR_TOP;
		g_url  = url;
		lock   = new LockSeg (x, BTN_Y, LOCK_W, BTN_H);
		mode   = new ModePill (width - PAD - ModePill::needW (), BTN_Y, ModePill::needW (), BTN_H);
		mode->anchor = ANCHOR_RIGHT | ANCHOR_TOP;
		zoom   = new ZoomCtl (width - PAD - ZoomCtl::needW (), BTN_Y);
		zoom->anchor = ANCHOR_RIGHT | ANCHOR_TOP;
		dl     = new DlButton (width - PAD - BTN_W, BTN_Y, open_downloads);
		dl->anchor = ANCHOR_RIGHT | ANCHOR_TOP;
		status = new StatusBar (0, height - SB, width);
		status->hidden = !g_sbOn;
		find   = new FindBar (0, height - SB - FB, width);
		find->hidden = true;
		status->setState ("Ready", false);
		back->setDisabled (true);
		fwd->setDisabled (true);
		back->tip = "Back (Alt+Left)"; fwd->tip = "Forward (Alt+Right)"; reload->tip = "Reload (F5)";
		home->tip = "Home"; hist->tip = "History (Ctrl+H)";
		addChild (back); addChild (fwd); addChild (reload); addChild (home); addChild (hist); addChild (url);
		addChild (lock); addChild (mode); addChild (zoom); addChild (dl); addChild (status); addChild (find);
		setResizable (true);
		layoutBand ();
	}
	// The page's height in the window: less the band, the status bar and the find bar (when shown).
	int pageH () const { return height - TB - (status->hidden ? 0 : SB) - (find->hidden ? 0 : FB); }
	// The field between the padlock and the pill (each there or not): its place and its ends;
	// right of them the zoom control, then the downloads' button (when there are downloads).
	void layoutBand ()
	{
		int dlw = dl->hidden ? 0 : BTN_W + GAP;
		int right = width - PAD - dlw - zoom->width - GAP - 2;	// (the field's group ends there)
		int zl = zoom->left, ml = mode->left;
		dl->left = width - PAD - BTN_W;
		zoom->left = width - PAD - dlw - zoom->width;
		mode->left = right - mode->width;
		if (zl != zoom->left || ml != mode->left) invalidate (true);	// (the band behind them)
		int x0 = urlX + (lock->hidden ? 0 : LOCK_W);
		int x1 = right - (mode->hidden ? 0 : mode->width);
		if (url->left != x0 || url->width != x1 - x0 || url->joinL != !lock->hidden || url->joinR != !mode->hidden)
		{
			url->left = x0;
			url->joinL = !lock->hidden; url->joinR = !mode->hidden;
			url->resizeTo (x1 - x0, url->height);
			url->invalidate (true);
			invalidate (false);
		}
		if (status->top != height - SB || status->width != width)
		{
			status->top = height - SB;
			status->resizeTo (width, SB);
		}
		int ft = height - (status->hidden ? 0 : SB) - FB;
		if (find->top != ft || find->width != width)
		{
			find->top = ft;
			find->resizeTo (width, FB);
			find->invalidate (true);
		}
	}
	// The band's keyboard: the field (F6), Tab to the pill, Shift+Tab to the padlock.
	bool bandFocus () const { return url->hasFocus || lock->hasFocus || mode->hasFocus || find->field->hasFocus; }
	void blurBand ()
	{
		if (find->field->hasFocus) { clearFocusTree (); find->field->invalidate (true); }
		if (url->hasFocus) url->focusOut ();
		if (lock->hasFocus || mode->hasFocus) { clearFocusTree (); lock->invalidate (true); mode->invalidate (true); url->invalidate (true); }
	}
	void tab (bool back)
	{
		Widget *order[3] = { lock, url, mode };
		int at = lock->hasFocus ? 0 : url->hasFocus ? 1 : 2;
		for (int n = 0; n < 3; n++)
		{
			at += back ? -1 : 1;
			if (at < 0 || at > 2) { blurBand (); return; }		// (out of the band: the page)
			if (!order[at]->hidden) break;
		}
		if (url->hasFocus) url->focusOut ();
		if (order[at] == url) { url->focusIn (); return; }
		order[at]->setFocus ();
		url->invalidate (true); lock->invalidate (true); mode->invalidate (true);
	}
	// Only the band is the window's own: the rest is the page's (NetSurf draws it).
	void onDraw () override
	{
		canvas.fillRect (0, 0, width, TB, C_BG);
		wk_etch_h (canvas, 0, TB - 2, width, C_BG);
	}
	void onResized () override { g_resized = true; layoutBand (); }
	static void reload_or_stop ();
	bool pageFocus () const { return !bandFocus (); }
};

NsWindow *g_win;
onyx_chrome_handler g_pagePtr, g_pageKey;
int g_grab;		// 0 none, 1 the band, 2 the page: where the pressed buttons went
bool g_shown;		// the band drawn at least once

bool has_modal (void);

void NsWindow::reload_or_stop () { if (g_win && g_win->busy) onyx_browser_stop (); else onyx_browser_reload (); }

void UrlField::joined ()
{
	if (g_win == 0) return;
	if (!g_win->lock->hidden) g_win->lock->invalidate (true);
	if (!g_win->mode->hidden) g_win->mode->invalidate (true);
}

// The padlock: the page's certificates in the viewer (or, none known, what the padlock says).
void LockSeg::click ()
{
	if (g_win == 0 || hidden) return;
	g_win->blurBand ();
	if (sec == ONYX_SEC_INSECURE)
	{
		wk_messagebox ("Not secure", "This page came over http:\nnot encrypted, no certificate.", MB_OK);
		onyx_browser_redraw ();
		return;
	}
	if (!onyx_browser_show_certificate ())
	{
		wk_messagebox (sec == ONYX_SEC_BROKEN ? "Not secure" : "Secure connection",
			       sec == ONYX_SEC_BROKEN ? "The certificate has a problem:\naccepted with Proceed." :
			       "The certificate was verified;\nits details were not kept.", MB_OK);
		onyx_browser_redraw ();
	}
}

// The pill's menu, under it (its right edge on the pill's): the version chosen -> the page
// loaded again with it.
void ModePill::openMenu ()
{
	if (g_win == 0 || hidden || mode < 0 || has_modal ()) return;
	if (g_url && g_url->hasFocus) g_url->focusOut ();
	bool kb = hasFocus;
	ModeMenu *m = new ModeMenu (left + width, top + height + 2, mode, onyx_browser_site ());
	menuOpen = true;
	invalidate (true);
	int r = m->run ();
	delete m;
	menuOpen = pressed = hover = false;
	if (kb) setFocus ();
	invalidate (true);
	onyx_browser_redraw ();			// (the page under the menu)
	if (r > 0 && r - 1 != mode) onyx_browser_set_site_mode (r - 1);
}

// The downloads' menu, under its button: a running one clicked -> stopped (after a question);
// "Clear the list" -> the finished ones out.
void open_downloads ()
{
	if (g_win == 0 || has_modal ()) return;
	if (g_url && g_url->hasFocus) g_url->focusOut ();
	DlButton *b = g_win->dl;
	int right = b->hidden ? g_win->width - PAD : b->left + b->width;
	DlMenu *m = new DlMenu (right, BTN_Y + BTN_H + 2);
	int r = m->run ();
	delete m;
	onyx_browser_redraw ();			// (the page under the menu)
	if (r == DL_ROWS + 1) { onyx_browser_downloads_clear (); return; }
	if (r > 0 && r <= g_ndl && dl_active (g_dl[r - 1]))
	{
		static char q[200];
		int id = g_dl[r - 1].id, n = 0;
		const char *a = "Stop downloading ";
		for (; *a; a++) q[n++] = *a;
		for (const char *c = g_dl[r - 1].name; *c && n < 190; c++) q[n++] = *c;
		q[n++] = '?'; q[n] = '\0';
		if (wk_messagebox ("Cancel the download", q, MB_YESNO) == 1) onyx_browser_download_cancel (id);
		onyx_browser_redraw ();
	}
}

bool has_modal (void)
{
	for (Widget *c = g_win ? g_win->firstChild : 0; c; c = c->nextSib)
		if (c->modal) return true;
	return false;
}

// The pointer value with y moved up by the band (clamped to the page's top).
gui_value page_value (gui_value v)
{
	gui_value y = GUI_PTR_Y (v) - TB;
	if (y < 0) y = 0;
	return (v & ~(gui_value) 0xFFFF) | (y & 0xFFFF);
}

unsigned g_inputs;	// the clicks, wheel turns and keys so far (onyx_chrome_input_pending)
bool g_ctxUp;		// a right press opened the context menu: its release is not the page's

// Events taken while a script runs (onyx_chrome_pump_deferred): kept, handled after it.
struct DeferredEvent { unsigned long sender; int ev; gui_value v; bool key; };
enum { DEFER_MAX = 256 };
DeferredEvent g_deferred[DEFER_MAX];
int g_ndeferred;
bool g_defer, g_pumping;

bool defer_event (unsigned long sender, int ev, gui_value v, bool key)
{
	if (!g_defer) return false;
	bool move = !key && ev == GUI_EVENT_PTR_MOVE;
	if (move && g_ndeferred > 0 && !g_deferred[g_ndeferred - 1].key &&
	    g_deferred[g_ndeferred - 1].ev == GUI_EVENT_PTR_MOVE)
		g_deferred[g_ndeferred - 1] = { sender, ev, v, key };	// (a move replaces the move before)
	else if (g_ndeferred < DEFER_MAX)
		g_deferred[g_ndeferred++] = { sender, ev, v, key };	// (full: this one lost)
	return true;
}

void ptr_event (unsigned long sender, int ev, gui_value v);
void key_event (unsigned long sender, int ev, gui_value k);

void replay_deferred ()
{
	for (int i = 0; i < g_ndeferred; i++)
	{
		DeferredEvent e = g_deferred[i];
		if (e.key) key_event (e.sender, e.ev, e.v);
		else ptr_event (e.sender, e.ev, e.v);
	}
	g_ndeferred = 0;
}

void ptr_event (unsigned long sender, int ev, gui_value v)
{
	static int bl, br, bm;
	if (g_win == 0) return;
	if (defer_event (sender, ev, v, false)) return;
	if (ev == GUI_EVENT_PTR_DOWN || ev == GUI_EVENT_PTR_UP || ev == GUI_EVENT_PTR_WHEEL)
	{
		g_inputs++;
		onyx_io_activity ();		// (the card's writers wait: docs/06 §32)
	}
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
	if (ev == GUI_EVENT_PTR_UP && (c & 2) && g_ctxUp) { g_ctxUp = false; return; }	// (its menu's)
	bool band = has_modal () || (g_grab ? g_grab == 1 : y < TB || y >= TB + g_win->pageH ());
	if (ev == GUI_EVENT_PTR_DOWN && (c & 2) && !band && !g_grab && !bl && !bm)
	{	// (docs/06 §40) a right press on the page: its context menu (the page does not get it)
		if (!g_win->pageFocus ()) g_win->blurBand ();
		g_win->handleMouse (-1, -1, 0, 0, 0, 0);
		g_ctxUp = true;
		onyx_browser_context_menu (x, y - TB);
		return;
	}
	if (ev == GUI_EVENT_PTR_WHEEL && !band && (kapi_get_modifiers () & MOD_CTRL))
	{	// Ctrl+wheel: the page's zoom (docs/06 §38), not a scroll
		int n = GUI_PTR_WHEEL (v);
		if (n != 0) onyx_browser_zoom (n > 0 ? 1 : -1);
		return;
	}
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
		if (ev == GUI_EVENT_PTR_DOWN && !g_win->pageFocus ()) g_win->blurBand ();
		g_win->handleMouse (-1, -1, 0, 0, 0, 0);		// (the band: the pointer left it)
		if (g_pagePtr) g_pagePtr (sender, ev, page_value (v));
	}
	if (ev == GUI_EVENT_PTR_UP && !bl && !br && !bm) g_grab = 0;
}

void key_event (unsigned long sender, int ev, gui_value k)
{
	if (g_win == 0 || ev != GUI_EVENT_KEY) return;
	if (defer_event (sender, ev, k, true)) return;
	g_inputs++;
	onyx_io_activity ();
	if (has_modal ()) { g_win->handleKey (k); return; }
	unsigned mods = kapi_get_modifiers ();
	if ((mods & MOD_CTRL) && !(mods & MOD_ALT))
	{	// the zoom (docs/06 §38): Ctrl++ (or Ctrl+=, the keypad's +), Ctrl+-, Ctrl+0 -- the
		// kernel sends these keys with Ctrl held as their characters (kernel.cpp's keymap)
		if (k == '+' || k == '=') { onyx_browser_zoom (1); return; }
		if (k == '-' || k == '_') { onyx_browser_zoom (-1); return; }
		if (k == '0') { onyx_browser_zoom (0); return; }
	}
	if (k == KEY_BACKSPACE && (mods & MOD_CTRL)) { open_history (); return; }	// Ctrl+H (^H is 8)
	if (k == KEY_F1 + 2 || (k == 7 && (mods & MOD_CTRL)))		// F3, Ctrl+G (Shift: back)
	{ find_step ((mods & MOD_SHIFT) ? -1 : 1); return; }
	if (Menu::current () && Menu::current ()->shortcut (k)) return;
	if ((mods & MOD_ALT) && k == KEY_LEFT)  { onyx_browser_back (); return; }
	if ((mods & MOD_ALT) && k == KEY_RIGHT) { onyx_browser_forward (); return; }
	if (k == KEY_F1 + 4) { onyx_browser_reload (); return; }		// F5
	if (k == KEY_F1 + 5) { if (g_win->url->hasFocus) g_win->blurBand (); else g_win->url->focusIn (); return; }	// F6
	if (!g_win->pageFocus ())
	{
		bool infind = g_win->find->field->hasFocus;
		if (k == 9) { if (!infind) g_win->tab ((mods & MOD_SHIFT) != 0); return; }	// Tab, Shift+Tab: the band's parts
		if (k == 27 && !g_win->url->hasFocus && !infind) { g_win->blurBand (); return; }
		g_win->handleKey (k);
		return;
	}
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
void m_site ()     { if (g_win) g_win->mode->openMenu (); }	// the site's version: the pill's menu
void m_cert ()     { if (g_win) g_win->lock->click (); }	// the padlock's: the certificate
void m_zoom_in ()  { onyx_browser_zoom (1); }
void m_zoom_out () { onyx_browser_zoom (-1); }
void m_zoom_100 () { onyx_browser_zoom (0); }
void m_status ()   { onyx_browser_set_status_bar (!g_sbOn); }

// ---- find in page (docs/06 §40) --------------------------------------------------------------
// The field's words (UTF-8) searched by gui.c: dir 0 a new search (typed: the first match), 1 the
// next match, -1 the one before, 2 again without moving the view (a new page loaded).
void find_search (int dir)
{
	if (g_win == 0) return;
	FindField *f = g_win->find->field;
	static char u[URL_MAX * 3];
	latin1_to_utf8 (f->text, f->len, u, sizeof u);
	onyx_browser_find (u, dir, g_win->find->mcase->on ? 1 : 0);
}
void find_typed () { find_search (0); }

// Ctrl+F, Edit > Find in Page...: the bar shown (the page shorter), its field focused with its
// words selected (typing replaces them; Enter finds them again)
void find_open ()
{
	if (g_win == 0 || has_modal ()) return;
	FindBar *b = g_win->find;
	if (g_win->url->hasFocus) g_win->url->focusOut ();
	if (b->hidden)
	{
		b->hidden = false;
		g_win->layoutBand ();
		g_win->invalidate (true);
		g_resized = true;		// (the page's area changed: the surface asks again)
		if (b->field->len > 0) find_search (0);	// (the words of the last time: found again)
	}
	b->field->setFocus ();
	b->field->all = true;
	b->field->caret = b->field->len;
	b->field->invalidate (true);
}

// Enter / F3 (1), Shift+Enter / Shift+F3 (-1): the next / previous match, around at the ends
void find_step (int dir)
{
	if (g_win == 0 || has_modal ()) return;
	if (g_win->find->hidden || g_win->find->field->len == 0) { find_open (); return; }
	find_search (dir);
}

// x, Esc in the field: the bar hidden, the highlights cleared (its words kept for the next time)
void find_close ()
{
	if (g_win == 0 || g_win->find->hidden) return;
	FindBar *b = g_win->find;
	if (b->field->hasFocus) g_win->clearFocusTree ();
	b->hidden = true;
	b->field->cnt = b->field->idx = -1;
	g_win->layoutBand ();
	g_win->invalidate (true);
	g_resized = true;
	onyx_browser_find_close ();
}

void m_find ()      { find_open (); }
void m_find_next () { find_step (1); }
void m_find_prev () { find_step (-1); }

// Edit's Cut / Copy / Paste / Select All: the key's, where the keys go (the address field, the
// find bar's field, else the page: its selection, its form field)
void route_key (long k)
{
	if (g_win == 0) return;
	if (!g_win->pageFocus ()) { g_win->handleKey (k); return; }
	if (g_pageKey) g_pageKey (0, GUI_EVENT_KEY, k);
}
void m_cut ()    { route_key (24); }
void m_copy ()   { route_key (3); }
void m_paste ()  { route_key (22); }
void m_selall () { route_key (1); }

// The History dialog: shown over the page (NetSurf waits meanwhile); a page chosen is opened.
void open_history ()
{
	if (g_win == 0 || has_modal ()) return;
	if (g_win->url->hasFocus) g_win->url->focusOut ();
	HistoryDialog *d = new HistoryDialog (g_win->width, g_win->height);
	d->filter->setFocus ();
	int r = d->run ();
	char *go = r == HistoryDialog::OPEN ? d->chosen : 0;
	d->chosen = 0;
	delete d;
	onyx_browser_redraw ();			// (the page under the dialog)
	if (go) { onyx_browser_go (go); delete [] go; }
}

// The About box (Help > About Jet Browser...): the name, NetSurf's copyright and licence (GPL v2),
// the libraries' licences. Their full texts: about:licence, about:credits, SD:/res/fonts/LICENSE*.
const char *const ABOUT[] = {
	"NetSurf: Copyright (c) 2003-2023 The NetSurf Developers.",
	"Free software under the GNU General Public License, version 2,",
	"WITHOUT ANY WARRANTY; NetSurf's artwork under the MIT licence.",
	"Jet Browser's changes to NetSurf are under the same terms.",
	"",
	"With: libcss, libdom, libhubbub, libparserutils, libwapcaplet,",
	"libnsutils, libnsgif, libnsbmp, libnsfb (MIT, The NetSurf Developers);",
	"QuickJS-ng (MIT), wasm3 (MIT), Mbed TLS (Apache 2.0),",
	"FreeType (FreeType Licence), zlib (zlib), libpng (libpng),",
	"libjpeg (IJG), libwebp (BSD), brotli (MIT), zstd (BSD),",
	"nghttp2 (MIT), plutovg / plutosvg (MIT); the fonts DejaVu,",
	"Liberation, Selawik, Gelasio (free font licences).",
	"",
	"The full texts: about:licence, about:credits, SD:/res/fonts/.",
};
const int ABOUT_N = (int) (sizeof ABOUT / sizeof ABOUT[0]);

class AboutDialog : public Modal
{
public:
	AboutDialog (int W, int H) : Modal (W < 600 + 40 ? W - 40 : 600,
		H < box_h () + 40 ? H - 40 : box_h ())
	{
		left = (W - width) / 2; top = (H - height) / 2;
		Button *b = new Button (width - 92, height - 38, 82, 28, "OK", hd_button);
		b->tag = 0; addChild (b);
	}
	static int box_h () { return Modal::titleH () + 16 + (wk_fh () + 2) * (ABOUT_N + 2) + 50; }
	bool onKey (long k) override
	{
		if (k == 27 || k == KEY_ENTER) { close (0); return true; }
		return false;
	}
	bool onMouse (int, int, int, int, int, int) override { return true; }	// (modal: all of it)
	void onDraw () override
	{
		drawBox ("About Jet Browser");
		int fh = wk_fh (), y = titleH () + 10;
		wk_text_l (canvas, 14, y, fh + 2, "Jet Browser -- the Onyx web browser, based on NetSurf", C_TEXT, 2);
		y += 2 * (fh + 2);
		for (int i = 0; i < ABOUT_N; i++, y += fh + 2)
			wk_text_l (canvas, 14, y, fh + 2, ABOUT[i], C_TEXT);
	}
};

void open_about ()
{
	if (g_win == 0 || has_modal ()) return;
	if (g_win->url->hasFocus) g_win->url->focusOut ();
	AboutDialog *d = new AboutDialog (g_win->width, g_win->height);
	d->run ();
	delete d;
	onyx_browser_redraw ();			// (the page under the dialog)
}

// The menus, twice: View's last item reads "Hide Status Bar" or "Show Status Bar" (wtk's
// menus have no check mark): the one that fits is published.
Menu g_menus[2];

void build_menu (Menu &m, bool sbOn)
{
	m.menu ("File");
	m.item ("Open Location...", "^L", WK_CTRL ('L'), m_location);
	m.separator ();
	m.item ("Downloads...", "", 0, open_downloads);
	m.menu ("Edit");				// (docs/06 §40; key_event: the keys)
	m.item ("Cut", "^X", 0, m_cut);
	m.item ("Copy", "^C", 0, m_copy);
	m.item ("Paste", "^V", 0, m_paste);
	m.item ("Select All", "^A", 0, m_selall);
	m.separator ();
	m.item ("Find in Page...", "^F", WK_CTRL ('F'), m_find);
	m.item ("Find Next", "F3", 0, m_find_next);
	m.item ("Find Previous", "Shift+F3", 0, m_find_prev);
	m.menu ("View");
	m.item ("Zoom In", "Ctrl++", 0, m_zoom_in);		// (key_event: the keys)
	m.item ("Zoom Out", "Ctrl+-", 0, m_zoom_out);
	m.item ("Actual Size", "Ctrl+0", 0, m_zoom_100);
	m.separator ();
	m.item (sbOn ? "Hide Status Bar" : "Show Status Bar", "", 0, m_status);
	m.menu ("Navigate");
	m.item ("Back", "Alt+Left", 0, m_back);
	m.item ("Forward", "Alt+Right", 0, m_forward);
	m.separator ();
	m.item ("Reload", "^R", WK_CTRL ('R'), m_reload);
	m.item ("Stop", "Esc", 0, m_stop);
	m.separator ();
	m.item ("Site Version (Standard / Mobile / Desktop)...", "", 0, m_site);
	m.item ("Page Security / Certificate...", "", 0, m_cert);
	m.separator ();
	m.item ("Home", "", 0, m_home);
	m.item ("History...", "^H", 0, open_history);	// (^H: key_event -- it is Backspace's code)
	m.menu ("Help");
	m.item ("About Jet Browser...", "", 0, open_about);
}

} // namespace

// ---- the C interface -------------------------------------------------------------------------
extern "C" {

unsigned *onyx_chrome_open (int w, int h, int *stride)
{
	if (g_win) return onyx_chrome_page (stride, 0, 0);
	g_win = new NsWindow (w, h);
	build_menu (g_menus[0], false);
	build_menu (g_menus[1], true);
	g_menus[g_sbOn ? 1 : 0].publish ();
#ifdef ONYX_HOST_SIM
	printf ("ONYX-CHROME w=%d h=%d zoom=%d,%d,%d dl=%d status=%d\n", g_win->width, g_win->height,
		g_win->zoom->left, g_win->zoom->width, ZoomCtl::SEG, g_win->width - PAD - BTN_W, g_win->status->top);
	fflush (stdout);
#endif
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
	if (h) *h = g_win->pageH ();	// (the band above, the status bar below)
	return c.px + (long) TB * c.stride;
}

void onyx_chrome_default_size (int *w, int *h)
{
	int sw = 1024, sh = 768;
	kapi_screen_size (&sw, &sh);
	int pw = sw - 64, ph = sh - TB - (g_sbOn ? SB : 0) - 28 - 8 - 24 - 90;	// the band, the status bar, the title + border, the menu bar, the dock
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
	return onyx_chrome_pump_wait (0);
}

// Pump the window's events, first waiting up to ms for one (an input event, a post from a
// fetch thread, the close box) -- not a blind sleep. 1: the window was resized.
int onyx_chrome_pump_wait (int ms)
{
	if (g_win == 0) return 0;
	if (g_ndeferred > 0) { replay_deferred (); ms = 0; }	// (taken while a script ran)
	g_pumping = true;
	if (ms > 0) kapi_pump_wait ((unsigned) ms);
	else kapi_pump_events ();
	g_pumping = false;
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

// The scheduler's question between two callbacks: did the user click, turn the wheel, type,
// resize or close since? The events are taken (the page's wait in the surface's ring for the
// main loop, which then runs before the next callbacks: a heavy page's timers and scripts
// no longer keep a click waiting for the whole budget).
int onyx_chrome_input_pending (void)
{
	unsigned before = g_inputs;
	if (g_win == 0) return 0;
	if (g_ndeferred > 0) return 1;
	g_pumping = true;
	kapi_pump_events ();
	g_pumping = false;
	return g_inputs != before || g_resized || kapi_should_exit ();
}

// While a script runs long (its interrupt handler, every 100 ms): the window's events taken
// (the system's queue drained -- no "not pumping" freeze, nothing dropped) but kept, handled
// once the script is done (a click then, as the user did it); the fetch threads' posts run.
void onyx_chrome_pump_deferred (void)
{
	if (g_win == 0 || g_pumping || g_defer) return;
	g_defer = true;
	g_pumping = true;
	kapi_pump_events ();
	g_pumping = false;
	g_defer = false;
}

void onyx_chrome_present (void)
{
	if (g_win && !g_win->valid) g_win->draw ();
	kapi_present ();
}

// The page's redraws say only that the window changed; the main loop presents once an
// iteration, after all of them (a scroll's moved pixels and its new band shown together).
static bool g_present_due;

void onyx_chrome_present_later (void)
{
	g_present_due = true;
}

void onyx_chrome_flush (void)
{
	if (!g_present_due) return;
	g_present_due = false;
	onyx_chrome_present ();
}

// Onyx (docs/06 §32): whether the window is seen -- 2 hidden (minimised, on another workspace,
// covered whole by opaque windows above it), 1 shown without the keyboard, 0 focused. The
// window manager's own view (kapi_win_geometry; kapi_win_list for what lies above).
namespace {
struct VRect { int x0, y0, x1, y1; };

// r less the rectangle c, into out (at most 4 pieces) -> how many
int vr_cut (const VRect &r, const VRect &c, VRect *out)
{
	if (c.x1 <= r.x0 || c.x0 >= r.x1 || c.y1 <= r.y0 || c.y0 >= r.y1) { out[0] = r; return 1; }
	int n = 0;
	int my0 = c.y0 > r.y0 ? c.y0 : r.y0, my1 = c.y1 < r.y1 ? c.y1 : r.y1;
	if (c.y0 > r.y0) out[n++] = { r.x0, r.y0, r.x1, c.y0 };
	if (c.y1 < r.y1) out[n++] = { r.x0, c.y1, r.x1, r.y1 };
	if (c.x0 > r.x0) out[n++] = { r.x0, my0, c.x0, my1 };
	if (c.x1 < r.x1) out[n++] = { c.x1, my0, r.x1, my1 };
	return n;
}

bool covered (const struct kapi_win_geom &g)
{
	static struct kapi_win_info w[64];
	int n = kapi_win_list (w, 64), me = -1;
	for (int i = 0; i < n; i++)		// (mine: where my frame and client area are)
		if (w[i].id != KAPI_WIN_DESKTOP && w[i].ow == g.w && w[i].oh == g.h &&
		    w[i].x - w[i].il == g.x && w[i].y - w[i].it == g.y && w[i].w == g.cw && w[i].h == g.ch)
			me = i;
	if (me < 0) return false;
	enum { MAXR = 32 };
	VRect vis[MAXR], next[MAXR];
	int nv = 1;
	vis[0] = { w[me].x, w[me].y, w[me].x + w[me].w, w[me].y + w[me].h };
	for (int i = me + 1; i < n && nv > 0; i++)	// (the list: bottom to top)
	{
		const struct kapi_win_info &o = w[i];
		if (o.state & (KAPI_WIN_MINIMISED | KAPI_WIN_OFFDESK)) continue;
		if (o.alpha < 255 || (o.flags & (WIN_FLAG_TRANSPARENT | WIN_FLAG_ALPHA))) continue;
		VRect c = { o.x, o.y, o.x + o.w, o.y + o.h };	// (its client area: the frame may be shaped)
		int nn = 0;
		for (int k = 0; k < nv; k++)
		{
			VRect piece[4];
			int np = vr_cut (vis[k], c, piece);
			if (nn + np > MAXR) return false;	// (too broken up: say seen)
			for (int j = 0; j < np; j++) next[nn++] = piece[j];
		}
		for (int k = 0; k < nn; k++) vis[k] = next[k];
		nv = nn;
	}
	return nv == 0;
}
}

int onyx_chrome_view_state (void)
{
	struct kapi_win_geom g;
	if (g_win == 0 || kapi_win_geometry (&g) != 0) return 0;	// (an older kernel: focused)
	if (g.state & (KAPI_WIN_MINIMISED | KAPI_WIN_OFFDESK)) return 2;
	if (covered (g)) return 2;
	return (g.state & KAPI_WIN_KEYS) ? 0 : 1;
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

void onyx_chrome_set_site (int sec, int mode)
{
	if (g_win == 0) return;
	bool lh = sec == ONYX_SEC_NONE, mh = mode < 0;
	if (!lh) g_win->lock->set (sec);
	if (!mh) g_win->mode->set (mode, 0);
	if (lh != g_win->lock->hidden || mh != g_win->mode->hidden)
	{
		if (lh && g_win->lock->hasFocus) g_win->blurBand ();
		if (mh && g_win->mode->hasFocus) g_win->blurBand ();
		g_win->lock->hidden = lh;
		g_win->mode->hidden = mh;
		g_win->lock->hover = g_win->lock->pressed = false;
		g_win->layoutBand ();
		g_win->invalidate (true);	// (the band behind what appeared / went: repainted)
	}
}

void onyx_chrome_set_nav (int can_back, int can_forward)
{
	if (g_win == 0) return;
	g_win->back->setDisabled (!can_back);
	g_win->fwd->setDisabled (!can_forward);
}

// ---- Onyx (docs/06 §38): the zoom, the status bar, the downloads --------------------------------

void onyx_chrome_set_zoom (int percent)
{
	if (g_win && g_win->zoom->pct != percent) g_win->zoom->set (percent);
}

void onyx_chrome_set_state (const char *text, int error)
{
	if (g_win) g_win->status->setState (text, error != 0);
}

void onyx_chrome_set_link (const char *text)
{
	if (g_win) g_win->status->setLink (text);
}

int onyx_chrome_status_bar_shown (void)
{
	return g_sbOn;
}

void onyx_chrome_show_status_bar (int shown)
{
	bool on = shown != 0;
	if (g_win == 0) { g_sbOn = on; return; }	// (before the window: its first size)
	if (on == g_sbOn) return;
	g_sbOn = on;
	g_win->status->hidden = !on;
	g_win->layoutBand ();
	g_win->invalidate (true);
	g_menus[on ? 1 : 0].publish ();
	g_resized = true;		// (the page's area changed: the surface asks again)
}

void onyx_chrome_downloads (const struct onyx_dl_info *list, int n)
{
	if (n > DL_ROWS) n = DL_ROWS;
	g_ndl = n;
	const struct onyx_dl_info *run = 0, *last = n > 0 ? &list[0] : 0;
	for (int i = 0; i < n; i++)
	{
		DlRow &d = g_dl[i];
		d.id = list[i].id; d.state = list[i].state; d.got = list[i].got; d.total = list[i].total;
		StatusBar::copy (d.name, list[i].name, sizeof d.name);
		StatusBar::copy (d.path, list[i].path, sizeof d.path);
		StatusBar::copy (d.error, list[i].error, sizeof d.error);
		if (run == 0 && list[i].state == ONYX_DL_RUNNING) run = &list[i];
	}
	if (g_win == 0) return;
	bool hide = n == 0;
	if (hide != g_win->dl->hidden)
	{
		g_win->dl->hidden = hide;
		g_win->dl->hover = g_win->dl->pressed = false;
		g_win->layoutBand ();
		g_win->invalidate (true);
	}
	g_win->dl->invalidate (true);
	// the status bar's right part: the download that runs, else the last one's end
	char t[200]; int o = 0;
	auto put = [&] (const char *x) { for (; *x && o < (int) sizeof t - 1; x++) t[o++] = *x; t[o] = '\0'; };
	t[0] = '\0';
	const struct onyx_dl_info *d = run ? run : last;
	if (d && d->state != ONYX_DL_ASK)
	{
		DlRow r; r.state = d->state; r.got = d->got; r.total = d->total;
		StatusBar::copy (r.path, d->path, sizeof r.path); StatusBar::copy (r.error, d->error, sizeof r.error);
		char st[700]; tell_dl (r, st, sizeof st);
		if (d->state == ONYX_DL_RUNNING) { put ("Downloading "); put (d->name); put (": "); put (st); }
		else if (d->state == ONYX_DL_DONE)
		{
			char sz[24]; tell_size (d->got, sz, sizeof sz);
			put ("Downloaded "); put (d->name); put (" ("); put (sz); put (")");
		}
		else { put (d->state == ONYX_DL_FAILED ? "Download failed: " : "Download cancelled: "); put (d->name); }
	}
	g_win->status->setRight (t);
}

// The Save dialog: wtk's FileDialog, asking before a file is replaced.
namespace {
class SaveDialog : public FileDialog
{
public:
	SaveDialog (const char *dir, const char *name) : FileDialog (dir, name, true, false) {}
	void onButton (int tag) override
	{
		if (tag == 1 && fileName ()[0] != '\0')
		{
			char p[512];
			getResult (p, sizeof p);
			void *h = kapi_open (p);
			if (h != 0)
			{
				kapi_close (h);
				if (wk_messagebox ("Replace the file", "A file of that name is already there.\nReplace it?",
						   MB_YESNO) != 1)
					return;
			}
		}
		FileDialog::onButton (tag);
	}
};
}

int onyx_chrome_save_dialog (const char *dir, const char *name, char *path, int cap)
{
	if (g_win == 0 || cap < 2) return 0;
	if (g_win->url->hasFocus) g_win->url->focusOut ();
	SaveDialog *d = new SaveDialog (dir, name);
	int r = d->run ();
	if (r == 1) d->getResult (path, (unsigned) cap);
	delete d;
	onyx_browser_redraw ();			// (the page under the dialog)
	return r == 1;
}

// ---- Onyx (docs/06 §40): find in page, the context menu, a copied image ---------------------

void onyx_chrome_find_result (int index, int count)
{
	if (g_win == 0) return;
	FindField *f = g_win->find->field;
	if (f->idx == index && f->cnt == count) return;
	f->idx = index; f->cnt = count;
	f->invalidate (true);
#ifdef ONYX_HOST_SIM
	char c[32]; f->count (c);
	printf ("ONYX-FINDBAR %s\n", c[0] ? c : "(none)");
	fflush (stdout);
#endif
}

int onyx_chrome_find_shown (void)
{
	return g_win && !g_win->find->hidden;
}

void onyx_chrome_find_open (void)
{
	find_open ();
}

void onyx_chrome_find_again (void)
{
	if (g_win && !g_win->find->hidden && g_win->find->field->len > 0) find_search (2);
}

int onyx_chrome_context_menu (int x, int y, int flags)
{
	if (g_win == 0 || has_modal ()) return 0;
	if (g_win->url->hasFocus) g_win->url->focusOut ();
	int type = 0;
	bool paste = kapi_clipboard_get (&type, 0, 0, 0) > 0 && type == CLIP_TEXT;
	bool sel = (flags & ONYX_CTXF_SELECTION) != 0;
	PopupMenu *m = new PopupMenu (x, y + TB);
	if (flags & ONYX_CTXF_LINK)
	{
		m->add ("Open Link", ONYX_CMD_OPEN_LINK);
		m->add ("Save Link As...", ONYX_CMD_SAVE_LINK);
		m->add ("Copy Link Address", ONYX_CMD_COPY_LINK);
		m->separator ();
	}
	if (flags & ONYX_CTXF_IMAGE)
	{
		m->add ("Open Image", ONYX_CMD_OPEN_IMAGE);
		m->add ("Save Image As...", ONYX_CMD_SAVE_IMAGE);
		m->add ("Copy Image", ONYX_CMD_COPY_IMAGE, (flags & ONYX_CTXF_IMAGE_PIXELS) != 0);
		m->add ("Copy Image Address", ONYX_CMD_COPY_IMAGE_URL);
		m->separator ();
	}
	if (flags & ONYX_CTXF_EDITABLE)
	{
		m->add ("Cut", ONYX_CMD_CUT, sel && (flags & ONYX_CTXF_CAN_CUT), "Ctrl+X");
		m->add ("Copy", ONYX_CMD_COPY, sel, "Ctrl+C");
		m->add ("Paste", ONYX_CMD_PASTE, paste, "Ctrl+V");
		m->add ("Select All", ONYX_CMD_SELECT_ALL, true, "Ctrl+A");
		m->separator ();
	}
	else if (sel)
	{
		m->add ("Copy", ONYX_CMD_COPY, true, "Ctrl+C");
		m->separator ();
	}
	if (!(flags & (ONYX_CTXF_LINK | ONYX_CTXF_IMAGE | ONYX_CTXF_EDITABLE)) && !sel)
	{
		m->add ("Back", ONYX_CMD_BACK, (flags & ONYX_CTXF_BACK) != 0, "Alt+Left");
		m->add ("Forward", ONYX_CMD_FORWARD, (flags & ONYX_CTXF_FORWARD) != 0, "Alt+Right");
		m->add ("Reload", ONYX_CMD_RELOAD, true, "F5");
		m->separator ();
	}
	if (!(flags & ONYX_CTXF_EDITABLE)) m->add ("Select All", ONYX_CMD_SELECT_ALL, true, "Ctrl+A");
	m->add ("Find in Page...", ONYX_CMD_FIND, true, "Ctrl+F");
	int r = m->run ();
	delete m;
	onyx_browser_redraw ();			// (the page under the menu)
	return r < 0 ? 0 : r;
}

int onyx_chrome_save_png (const char *path, const unsigned *px, int w, int h)
{
	if (px == 0 || w <= 0 || h <= 0) return 0;
	bool alpha = false;
	for (long i = 0, n = (long) w * h; i < n && !alpha; i++) alpha = (px[i] >> 24) != 255;
	unsigned len = 0;
	unsigned char *png = pngsave::png_encode (px, w, h, alpha, &len);
	if (png == 0) return 0;
	int ok = kapi_save_file (path, png, len) == (int) len;
	delete [] png;
	return ok;
}

void onyx_chrome_message (const char *title, const char *text)
{
	if (g_win == 0) return;
	wk_messagebox (title, text, MB_OK);
	onyx_browser_redraw ();
}

// A notification by notifyd (notify.h's message), when it runs: not launched here (that waits).
void onyx_chrome_notify (const char *title, const char *text)
{
#if !defined(_WIN32) && !defined(ONYX_HOST_SIM)
	int pid = kapi_ipc_lookup ("notify");
	if (pid == 0) return;
	static char msg[500];
	int n = 0;
	for (int i = 0; title && title[i] && n < 80; i++) msg[n++] = title[i];
	msg[n++] = '\0';
	for (int i = 0; text && text[i] && n < 498; i++) msg[n++] = text[i];
	msg[n++] = '\0';
	kapi_mailbox_send (pid, 1, msg, (unsigned) n);	// (NOTIFY_MSG_SHOW)
#else
	(void) title; (void) text;
#endif
}

} // extern "C"
