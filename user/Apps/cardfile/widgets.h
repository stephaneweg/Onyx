//
// widgets.h -- Cardfile's controls, drawn in the theme's look (uikit/paint.h):
//   * the icons (uikit/vpaint.h: anti-aliased, from their geometry), the toolbar and its buttons, the
//     view switch (Form | List | Design: one segment lit in the accent);
//   * the editors of a form's fields: a line of any length (LineEdit: the selection -- Shift + the
//     arrows, a drag, a double click on a word, ^A --, the clipboard ^X ^C ^V, words with Ctrl; a
//     filter of the characters a number or a date accepts), a text of several lines wrapped at the
//     words (MemoEdit), a date (DateEdit: typed, or picked on a calendar dropping from its button),
//     a colour (ColorEdit: a swatch and its code; a palette drops from it, More Colours... opens the
//     colour dialog), a choice (ChoiceBox: the list drops from it, a letter jumps to an option), a
//     yes / no check box;
//   * the search box (a magnifier, a clear button), the record navigator at the window's foot.
//
#ifndef _cardfile_widgets_h
#define _cardfile_widgets_h

#include "uikit/uikit.h"
#include "systemkit/clipboard.h"
#include "model.h"

namespace cf {

using namespace uikit;

enum { TB_H = 40, BTN = 28, NAV_H = 30, ED_H = 26 };
static const unsigned C_ERROR = 0x00C8402E;		// an invalid value's outline

// Where a widget is in the window (its parents' scroll counted).
static void abs_pos (Widget *w, int *x, int *y)
{
	int ax = 0, ay = 0;
	for (Widget *p = w; p && p->parent; p = p->parent) { ax += p->left - p->parent->scrollX; ay += p->top - p->parent->scrollY; }
	*x = ax; *y = ay;
}

// ---- the icons (20 x 20 px at x, y) ------------------------------------------------------------------------
enum { IC_NEW, IC_OPEN, IC_SAVE, IC_UNDO, IC_REDO, IC_REC_NEW, IC_REC_DUP, IC_REC_DEL, IC_FORM, IC_LIST, IC_DESIGN,
       IC_SEARCH, IC_COUNT };

// A rounded box with a 1.5 px border: the border's colour filled, the inside over it.
static void icon_box (Canvas &cv, VPath &p, int x, int y, int w, int h, int r, unsigned border, unsigned fill, int A)
{
	p.clear (); p.rrect (x, y, w, h, r); p.fill (cv, border, A);
	p.clear (); p.rrect (x + 22, y + 22, w - 44, h - 44, r > 22 ? r - 22 : 4); p.fill (cv, fill, A);
}

static void draw_icon (Canvas &cv, int k, int x, int y, unsigned ink, bool off = false)
{
	unsigned dim = uk_mix (ink, C_BG, 150);
	if (off) ink = dim;
	int A = off ? 110 : 255;
	VPath p;
	int X = V (x), Y = V (y);
	unsigned paper = 0xFFFFFF, accent = off ? dim : C_ACCENT;
	switch (k)
	{
	case IC_NEW:
	{
		int pts[10] = { X + V (4), Y + V (2), X + V (12), Y + V (2), X + V (16), Y + V (6), X + V (16), Y + V (18), X + V (4), Y + V (18) };
		p.poly (pts, 5); p.fill (cv, paper, A);
		p.clear (); p.polyline (pts, 5, 22, true); p.fill (cv, ink);
		int f[6] = { X + V (12), Y + V (2), X + V (12), Y + V (6), X + V (16), Y + V (6) };
		p.clear (); p.polyline (f, 3, 20); p.fill (cv, ink);
		p.clear (); p.rect (X + V (7), Y + V (9), V (6), 24); p.rect (X + V (7), Y + V (12), V (6), 24); p.rect (X + V (7), Y + V (15), V (4), 24);
		p.fill (cv, accent);
		break;
	}
	case IC_OPEN:
	{
		int back[12] = { X + V (2), Y + V (5), X + V (7), Y + V (5), X + V (9), Y + V (7), X + V (16), Y + V (7), X + V (16), Y + V (16), X + V (2), Y + V (16) };
		p.poly (back, 6); p.fill (cv, 0xD9A23A, A);
		p.clear (); p.polyline (back, 6, 20, true); p.fill (cv, 0x8A5D12, A);
		int front[8] = { X + V (4), Y + V (10), X + V (19), Y + V (10), X + V (16), Y + V (16), X + V (2), Y + V (16) };
		p.clear (); p.poly (front, 4); p.fill (cv, 0xF2C45A, A);
		p.clear (); p.polyline (front, 4, 20, true); p.fill (cv, 0x8A5D12, A);
		break;
	}
	case IC_SAVE:
		p.rrect (X + V (3), Y + V (3), V (14), V (14), V (2)); p.fill (cv, 0x3569B5, A);
		p.clear (); p.rect (X + V (6), Y + V (3), V (8), V (5)); p.fill (cv, 0xDCE6F2, A);
		p.clear (); p.rect (X + V (11), Y + V (4), V (2), V (3)); p.fill (cv, 0x3569B5, A);
		p.clear (); p.rrect (X + V (5), Y + V (11), V (10), V (6), V (1)); p.fill (cv, 0xFFFFFF, A);
		p.clear (); p.rect (X + V (7), Y + V (13), V (6), 12); p.rect (X + V (7), Y + V (15), V (6), 12); p.fill (cv, 0x9AA8BC, A);
		break;
	case IC_UNDO: case IC_REDO:
	{
		bool r = k == IC_REDO;
		int cx = X + V (10), cy = Y + V (12);
		if (r) p.arc (cx, cy, V (6), 180, -60, 30); else p.arc (cx, cy, V (6), 0, 240, 30);
		p.fill (cv, ink);
		p.clear ();
		if (r) p.arrowHead (X + V (17), Y + V (13), -80, V (6), V (4));
		else p.arrowHead (X + V (3), Y + V (13), 260, V (6), V (4));
		p.fill (cv, ink);
		break;
	}
	case IC_REC_NEW: case IC_REC_DEL: case IC_REC_DUP:
	{
		if (k == IC_REC_DUP) icon_box (cv, p, X + V (6), Y + V (2), V (13), V (11), V (2), ink, paper, A);
		int cx = k == IC_REC_DUP ? 1 : 2, cy = k == IC_REC_DUP ? 7 : 3;
		icon_box (cv, p, X + V (cx), Y + V (cy), V (14), V (11), V (2), ink, paper, A);
		p.clear ();
		p.rect (X + V (cx + 3), Y + V (cy + 3), V (3), 22); p.rect (X + V (cx + 7), Y + V (cy + 3), V (5), 22);
		p.rect (X + V (cx + 3), Y + V (cy + 6), V (3), 22); p.rect (X + V (cx + 7), Y + V (cy + 6), V (5), 22);
		p.fill (cv, accent);
		if (k != IC_REC_DUP)
		{
			unsigned badge = off ? dim : k == IC_REC_NEW ? 0x2E9E4F : 0xD0453A;
			p.clear (); p.circle (X + V (15), Y + V (15), V (5)); p.fill (cv, badge, A);
			p.clear (); p.rect (X + V (12), Y + V (15) - 12, V (6), 24);
			if (k == IC_REC_NEW) p.rect (X + V (15) - 12, Y + V (12), 24, V (6));
			p.fill (cv, 0xFFFFFF, A);
		}
		break;
	}
	case IC_FORM:					// a card: two labels and their fields
		icon_box (cv, p, X + V (2), Y + V (3), V (16), V (14), V (2), ink, paper, A);
		p.clear (); p.rect (X + V (5), Y + V (7), V (3), 24); p.rect (X + V (5), Y + V (11), V (3), 24); p.fill (cv, ink);
		p.clear (); p.rrect (X + V (9), Y + V (6), V (6), V (3), 10); p.rrect (X + V (9), Y + V (10), V (6), V (3), 10);
		p.fill (cv, accent);
		break;
	case IC_LIST:					// a table: its header, its rows
		icon_box (cv, p, X + V (2), Y + V (3), V (16), V (14), V (2), ink, paper, A);
		p.clear (); p.rect (X + V (3) + 8, Y + V (4) + 8, V (15) - 16, V (3)); p.fill (cv, accent);
		p.clear (); p.rect (X + V (4), Y + V (10), V (12), 20); p.rect (X + V (4), Y + V (13), V (12), 20);
		p.rect (X + V (8), Y + V (8), 20, V (8)); p.fill (cv, ink);
		break;
	case IC_DESIGN:					// a set square and a pencil
	{
		int tri[6] = { X + V (2), Y + V (4), X + V (2), Y + V (18), X + V (16), Y + V (18) };
		p.poly (tri, 3); p.fill (cv, paper, A);
		p.clear (); p.polyline (tri, 3, 22, true); p.fill (cv, ink);
		int in[6] = { X + V (5), Y + V (11), X + V (5), Y + V (15), X + V (9), Y + V (15) };
		p.clear (); p.polyline (in, 3, 18, true); p.fill (cv, ink);
		p.clear (); p.line (X + V (9), Y + V (13), X + V (17), Y + V (5), 56); p.fill (cv, off ? dim : 0xE0A030, A);
		p.clear (); p.line (X + V (16), Y + V (6), X + V (18), Y + V (4), 56); p.fill (cv, off ? dim : 0xD06070, A);
		int tip[6] = { X + V (7) + 8, Y + V (15) - 8, X + V (9) - 6, Y + V (11) + 10, X + V (11) - 6, Y + V (13) + 6 };
		p.clear (); p.poly (tip, 3); p.fill (cv, ink);
		break;
	}
	case IC_SEARCH:
		p.circle (X + V (8), Y + V (8), V (6)); p.hole (X + V (8), Y + V (8), V (5) - 8);
		p.line (X + V (12), Y + V (12), X + V (17), Y + V (17), 40);
		p.fill (cv, ink);
		break;
	}
}

// The navigator's glyphs: |< < > >| and +, centred on (cx, cy).
enum { NG_FIRST, NG_PREV, NG_NEXT, NG_LAST, NG_NEW };
static void nav_glyph (Canvas &cv, int g, int cx, int cy, unsigned c)
{
	VPath p;
	int X = V (cx), Y = V (cy);
	if (g == NG_NEW) { p.rect (X - V (5), Y - 12, V (10), 24); p.rect (X - 12, Y - V (5), 24, V (10)); p.fill (cv, c); return; }
	bool left = g == NG_FIRST || g == NG_PREV;
	int dx = g == NG_FIRST ? V (2) : g == NG_LAST ? -V (2) : 0;
	int t[6];
	if (left) { t[0] = X + V (3) + dx; t[1] = Y - V (5); t[2] = X + V (3) + dx; t[3] = Y + V (5); t[4] = X - V (4) + dx; t[5] = Y; }
	else { t[0] = X - V (3) + dx; t[1] = Y - V (5); t[2] = X - V (3) + dx; t[3] = Y + V (5); t[4] = X + V (4) + dx; t[5] = Y; }
	p.poly (t, 3);
	if (g == NG_FIRST) p.rect (X - V (5), Y - V (5), 28, V (10));
	if (g == NG_LAST) p.rect (X + V (5) - 28, Y - V (5), 28, V (10));
	p.fill (cv, c);
}

// ---- the toolbar -------------------------------------------------------------------------------------------
class ToolButton : public Widget
{
public:
	int icon; void (*cb) ();
	ToolButton (int ic, const char *tip_, void (*cb_) ()) : Widget (0, 0, BTN, BTN), icon (ic), cb (cb_), m_hot (false) { tip = tip_; }
	void setDisabled (bool d) { if (d != disabled) { disabled = d; invalidate (true); } }
	void onDraw () override
	{
		unsigned bg = bgColor ();
		canvas.clear (bg);
		if (!disabled && m_hot)
		{
			bool dn = pressed;
			uk_rbox (canvas, 0, 0, width, height, 5, dn ? uk_tone (bg, 110) : uk_tone (bg, 150), dn ? uk_tone (bg, 118) : uk_tone (bg, 138));
			uk_rline (canvas, 0, 0, width, height, 5, uk_tone (bg, 96), 150);
		}
		int d = pressed && m_hot ? 1 : 0;
		draw_icon (canvas, icon, (width - 20) / 2 + d, (height - 20) / 2 + d, uk_ink_for (bg), disabled);
	}
	bool onMouse (int mx, int my, int bl, int, int, int wheel) override
	{
		if (wheel) return false;
		bool in = mx >= 0 && my >= 0 && mx < width && my < height;
		if (in != m_hot) { m_hot = in; invalidate (true); }
		if (disabled) return in;
		if (bl && in && !pressed) { pressed = true; invalidate (true); }
		else if (!bl && pressed) { pressed = false; invalidate (true); if (in && cb) cb (); }
		return in;
	}
private:
	bool m_hot;
};

class ToolBar : public Widget
{
public:
	ToolBar (int l, int t, int w) : Widget (l, t, w, TB_H), m_x (8), m_nsep (0) {}
	void add (Widget *w, int gap = 2)
	{
		m_x += gap;
		w->left = m_x; w->top = (TB_H - w->height) / 2;
		addChild (w);
		m_x += w->width;
	}
	void sep () { if (m_nsep < 16) m_sepX[m_nsep++] = m_x + 6; m_x += 13; }
	void onDraw () override
	{
		canvas.fillRect (0, 0, width, height, C_BG);
		for (int i = 0; i < m_nsep; i++) uk_etch_v (canvas, m_sepX[i], 8, height - 16, C_BG);
		uk_etch_h (canvas, 0, height - 2, width, C_BG);
	}
private:
	int m_x, m_nsep, m_sepX[16];
};

// Three segments side by side, one lit (the view shown): an icon and a name each.
class ViewSwitch : public Widget
{
public:
	enum { N = 3 };
	int cur; void (*onPick) (int);
	ViewSwitch (void (*cb) (int)) : Widget (0, 0, 3 * 88, BTN), cur (0), onPick (cb), m_hot (-1), m_down (-1) { canFocus = false; }
	void set (int v) { if (v != cur) { cur = v; invalidate (true); } }
	void onDraw () override
	{
		static const char *const NAME[N] = { "Form", "List", "Design" };
		static const int ICON[N] = { IC_FORM, IC_LIST, IC_DESIGN };
		unsigned bg = bgColor ();
		canvas.clear (bg);
		uk_rbox (canvas, 0, 0, width, height, 6, uk_tone (C_BUTTON, 176), uk_tone (C_BUTTON, 118));
		int sw = width / N;
		for (int i = 0; i < N; i++)
		{
			int x = i * sw, w = i == N - 1 ? width - x : sw;
			int corners = (i == 0 ? UK_TL | UK_BL : 0) | (i == N - 1 ? UK_TR | UK_BR : 0);
			if (i == cur) uk_rbox (canvas, x, 0, w, height, corners ? 6 : 0, uk_tone (C_ACCENT, 142), uk_tone (C_ACCENT, 112), 255, corners);
			else if (i == m_hot) uk_rbox (canvas, x, 0, w, height, corners ? 6 : 0, uk_tone (C_BUTTON, m_down == i ? 120 : 196), uk_tone (C_BUTTON, m_down == i ? 132 : 140), 255, corners);
			if (i > 0 && i != cur && i - 1 != cur) canvas.fillRect (x, 5, 1, height - 10, uk_tone (C_BUTTON, 96));
			unsigned ink = i == cur ? C_SEL_TEXT : C_BUTTON_TEXT;
			int tw = uk_text_w (NAME[i], i == cur ? 2 : 0), gx = x + (w - 20 - 5 - tw) / 2;
			draw_icon (canvas, ICON[i], gx, (height - 20) / 2, ink);
			uk_text_l (canvas, gx + 25, 0, height, NAME[i], ink, i == cur ? 2 : 0);
		}
		uk_rline (canvas, 0, 0, width, height, 6, uk_tone (C_BUTTON, 70), 190);
	}
	bool onMouse (int mx, int my, int bl, int, int, int wheel) override
	{
		if (wheel) return false;
		bool in = mx >= 0 && my >= 0 && mx < width && my < height;
		int h = in ? mx * N / width : -1;
		if (h != m_hot) { m_hot = h; invalidate (true); }
		if (bl && in && m_down < 0) { m_down = h; invalidate (true); }
		else if (!bl && m_down >= 0)
		{
			int d = m_down; m_down = -1; invalidate (true);
			if (d == h && onPick) onPick (d);
		}
		return in;
	}
private:
	int m_hot, m_down;
};

// ---- text being edited -------------------------------------------------------------------------------------
// A growing buffer, the caret, the selection's other end (-1: none); single-line: a line break or a
// tab typed or pasted becomes a space.
struct TextCore
{
	char *buf; int len, cap, max, caret, anchor; bool multi;
	TextCore (int maxLen, bool multiLine) : buf (0), len (0), cap (0), max (maxLen), caret (0), anchor (-1), multi (multiLine) { reserve (63); }
	~TextCore () { delete [] buf; }
	void reserve (int n)
	{
		if (n + 1 <= cap) return;
		int nc = cap ? cap : 64;
		while (nc < n + 1) nc *= 2;
		char *nb = new char[nc];
		for (int i = 0; i < len; i++) nb[i] = buf[i];
		nb[len] = '\0';
		delete [] buf;
		buf = nb; cap = nc;
	}
	bool hasSel () const { return anchor >= 0 && anchor != caret; }
	int  selA () const { return hasSel () ? imin (anchor, caret) : caret; }
	int  selB () const { return hasSel () ? imax (anchor, caret) : caret; }
	void set (const char *s)
	{
		len = 0; caret = 0; anchor = -1;
		reserve (imin (slen (s), max));
		buf[0] = '\0';
		insert (s, slen (s), 0);
		caret = len; anchor = -1;
	}
	bool same (const char *s) const { for (int i = 0; i < len; i++) if (s[i] != buf[i]) return false; return s[len] == '\0'; }
	void remove (int at, int n)
	{
		if (n <= 0) return;
		for (int i = at; i + n <= len; i++) buf[i] = buf[i + n];
		len -= n;
	}
	void delSel () { if (hasSel ()) { int a = selA (); remove (a, selB () - a); caret = a; } anchor = -1; }
	// s[0..n) at the caret (the selection replaced); `accept` filters the characters. -> how many went in.
	int insert (const char *s, int n, bool (*accept) (long))
	{
		delSel ();
		int room = max - len, k = 0;
		if (room <= 0 || n <= 0) return 0;
		char small[64], *tmp = imin (n, room) <= 64 ? small : new char[imin (n, room)];
		for (int i = 0; i < n && k < room; i++)		// (the characters kept, then moved in at once)
		{
			char c = s[i];
			if (c == '\r') continue;
			if (c == '\t' || (!multi && c == '\n')) c = ' ';
			if ((unsigned char) c < 32 && c != '\n') continue;
			if (accept && !accept ((unsigned char) c)) continue;
			tmp[k++] = c;
		}
		reserve (len + k);
		for (int j = len - 1; j >= caret; j--) buf[j + k] = buf[j];
		for (int i = 0; i < k; i++) buf[caret + i] = tmp[i];
		len += k; caret += k;
		buf[len] = '\0';
		if (tmp != small) delete [] tmp;
		return k;
	}
	static bool wordc (char c) { unsigned char u = (unsigned char) c; return (u >= '0' && u <= '9') || (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || u == '_' || u >= 0xC0; }
	int wordLeft (int p) const { while (p > 0 && !wordc (buf[p - 1])) p--; while (p > 0 && wordc (buf[p - 1])) p--; return p; }
	int wordRight (int p) const { while (p < len && wordc (buf[p])) p++; while (p < len && !wordc (buf[p])) p++; return p; }
	void selectWord (int p)
	{
		int s = iclamp (p, 0, len), e = s;
		while (s > 0 && wordc (buf[s - 1])) s--;
		while (e < len && wordc (buf[e])) e++;
		anchor = s; caret = e;
	}
	void moveTo (int p, bool shift)
	{
		p = iclamp (p, 0, len);
		if (shift) { if (anchor < 0) anchor = caret; }
		else anchor = -1;
		caret = p;
	}
	void copy () const { if (hasSel ()) clip_set_text_n (buf + selA (), selB () - selA ()); }
	// The editing keys both editors share: 0 not one, 1 done (nothing changed), 2 the text changed.
	int editKey (long k, bool (*accept) (long))
	{
		bool ctrl = (kapi_get_modifiers () & MOD_CTRL) != 0;
		if ((k >= 32 && k <= 126) || (k >= 0xA0 && k <= 0xFF))
		{
			char c = (char) k;
			if (accept && !accept (k)) return 1;
			return insert (&c, 1, 0) ? 2 : 1;
		}
		if (k == KEY_BACKSPACE)
		{
			if (hasSel ()) { delSel (); return 2; }
			if (caret == 0) return 1;
			int p = ctrl ? wordLeft (caret) : caret - 1;
			remove (p, caret - p); caret = p;
			return 2;
		}
		if (k == KEY_DEL)
		{
			if (hasSel ()) { delSel (); return 2; }
			if (caret >= len) return 1;
			int p = ctrl ? wordRight (caret) : caret + 1;
			remove (caret, p - caret);
			return 2;
		}
		if (k == UK_CTRL ('A')) { anchor = 0; caret = len; return 1; }
		if (k == UK_CTRL ('C')) { copy (); return 1; }
		if (k == UK_CTRL ('X')) { if (!hasSel ()) return 1; copy (); delSel (); return 2; }
		if (k == UK_CTRL ('V'))
		{
			static char b[16384];
			int n = clip_get_text (b, sizeof b);
			return n > 0 && insert (b, n, accept) ? 2 : 1;
		}
		return 0;
	}
};

// ---- a line of text ------------------------------------------------------------------------------------------
// Any length (up to VAL_MAX - 1), scrolled to the caret; right-aligned (a number) while it fits.
// Enter: onEnter if set, else to the parent (the form: the next field); Tab, Esc, Up, Down: the parent's.
class LineEdit : public Widget
{
public:
	TextCore t;
	bool rightAlign, error, readonly;
	int padL, padR;				// px kept free inside, left and right (an icon, a button)
	const char *placeholder;		// shown dim while empty (0: none)
	bool (*accept) (long ch);		// the characters it takes (0: all)
	Action onChange, onEnter;
	LineEdit (int l, int t_, int w, int h = ED_H)
		: Widget (l, t_, w, h), t (VAL_MAX - 1, false), rightAlign (false), error (false), readonly (false), padL (0), padR (0),
		  placeholder (0), accept (0), onChange (0), onEnter (0), m_scroll (0), m_drag (false), m_lastClick (0)
	{ canFocus = true; }
	const char *text () const { return t.buf; }
	void setText (const char *s) { t.set (s ? s : ""); m_scroll = 0; invalidate (true); }
	void selectAll () { t.anchor = 0; t.caret = t.len; invalidate (true); }
	void setError (bool e) { if (e != error) { error = e; invalidate (true); } }

	int textX ()				// where the text starts (the right alignment, the scroll)
	{
		int fw = uk_fw (), area = width - padL - padR - 12;
		if (rightAlign && t.len * fw <= area) return width - padR - 6 - t.len * fw;
		return padL + 6 - m_scroll * fw;
	}
	void scrollToCaret ()
	{
		int fw = uk_fw (), vis = (width - padL - padR - 12) / fw; if (vis < 1) vis = 1;
		if (t.caret < m_scroll) m_scroll = t.caret;
		if (t.caret > m_scroll + vis) m_scroll = t.caret - vis;
		if (m_scroll > t.len - vis) m_scroll = imax (0, t.len - vis);
		if (m_scroll < 0) m_scroll = 0;
	}
	virtual void drawExtra () {}		// (a subclass's icon or button, over the field)
	void onDraw () override
	{
		int fw = uk_fw (), fh = uk_fh ();
		canvas.clear (bgColor ());
		bool focus = hasFocus && !disabled;
		uk_sunken (canvas, 0, 0, width, height, 4, disabled ? uk_tone (C_FACE, 150) : C_FIELD, focus && !error);
		if (error) { uk_rline (canvas, 0, 0, width, height, 4, C_ERROR, 255); uk_rline (canvas, 1, 1, width - 2, height - 2, 3, C_ERROR, focus ? 160 : 90); }
		if (focus) scrollToCaret (); else m_scroll = 0;		// (without the keyboard: its start shown)
		int x0 = textX (), ty = (height - fh) / 2;
		Canvas clip; clip.adopt (canvas.px + padL + 3, imax (1, width - padL - padR - 6), height, canvas.stride);
		int cx = x0 - padL - 3;
		if (t.len == 0 && placeholder && !focus) uk_text_l (clip, 3, 0, height, placeholder, uk_mix (C_FIELD, C_FIELD_TEXT, 120), 1);
		if (focus && t.hasSel ())
			clip.fillRect (cx + t.selA () * fw, ty, (t.selB () - t.selA ()) * fw, fh, uk_mix (C_FIELD, C_ACCENT, 96));
		int first = imax (0, -cx / fw - 1), last = imin (t.len, first + clip.w / fw + 3);
		if (last > first)
		{
			char save = t.buf[last]; t.buf[last] = '\0';
			clip.text (cx + first * fw, ty, t.buf + first, disabled ? C_DIS : C_FIELD_TEXT);
			t.buf[last] = save;
		}
		if (focus) clip.fillRect (cx + t.caret * fw - (t.caret == t.len && rightAlign ? 1 : 0), ty, 2, fh, C_ACCENT);
		drawExtra ();
	}
	int posAt (int mx) { int fw = uk_fw (); int p = (mx - textX () + fw / 2) / fw; return iclamp (p, 0, t.len); }
	bool onMouse (int mx, int my, int bl, int, int, int wheel) override
	{
		if (wheel) return false;
		if (mx < 0) { if (m_drag) { m_drag = false; catchOutside = false; } pressed = false; return false; }
		if (disabled) return true;
		(void) my;
		if (bl && !pressed)
		{
			pressed = true;
			bool shift = (kapi_get_modifiers () & MOD_SHIFT) != 0;
			if (!hasFocus) setFocus ();
			unsigned now = kapi_get_ticks ();
			int p = posAt (mx);
			if (now - m_lastClick < 40 && !shift) { t.selectWord (p); m_lastClick = 0; }
			else { t.moveTo (p, shift); if (!shift) t.anchor = p; m_drag = true; catchOutside = true; m_lastClick = now; }
			invalidate (true);
		}
		else if (bl && m_drag)
		{
			int p = posAt (mx);
			if (p != t.caret) { t.caret = p; invalidate (true); }
		}
		else if (!bl)
		{
			pressed = false;
			if (m_drag) { m_drag = false; catchOutside = false; if (t.anchor == t.caret) t.anchor = -1; }
		}
		return true;
	}
	bool onKey (long k) override
	{
		if (disabled) return false;
		bool shift = (kapi_get_modifiers () & MOD_SHIFT) != 0, ctrl = (kapi_get_modifiers () & MOD_CTRL) != 0;
		switch (k)
		{
		case KEY_LEFT:
			if (t.hasSel () && !shift) t.moveTo (t.selA (), false);
			else t.moveTo (ctrl ? t.wordLeft (t.caret) : t.caret - 1, shift);
			invalidate (true); return true;
		case KEY_RIGHT:
			if (t.hasSel () && !shift) t.moveTo (t.selB (), false);
			else t.moveTo (ctrl ? t.wordRight (t.caret) : t.caret + 1, shift);
			invalidate (true); return true;
		case KEY_HOME: if (ctrl) return false; t.moveTo (0, shift); invalidate (true); return true;	// (Ctrl: the form's)
		case KEY_END: if (ctrl) return false; t.moveTo (t.len, shift); invalidate (true); return true;
		case KEY_ENTER: if (onEnter) { onEnter (*this); return true; } return false;
		}
		if (readonly && k != UK_CTRL ('C') && k != UK_CTRL ('A')) return false;
		int r = t.editKey (k, accept);
		if (!r) return false;
		invalidate (true);
		if (r == 2) edited ();
		return true;
	}
	virtual void edited () { setError (false); if (onChange) onChange (*this); }	// (edited: no longer marked wrong)
private:
	int m_scroll; bool m_drag; unsigned m_lastClick;
};

// The characters numbers and dates take.
static bool accept_int (long c) { return (c >= '0' && c <= '9') || c == '-' || c == '+' || c == ' ' || c == '\''; }
static bool accept_dec (long c) { return accept_int (c) || c == '.' || c == ','; }
static bool accept_date (long c) { return (c >= '0' && c <= '9') || c == '/' || c == '-' || c == '.' || c == ' '; }
static bool accept_column (long c) { return c < 128 && col_char ((char) c); }

// ---- a text of several lines, wrapped at the words -------------------------------------------------------------
class MemoEdit : public Widget
{
public:
	TextCore t;
	bool error;
	Action onChange;
	MemoEdit (int l, int t_, int w, int h, int maxLen = 32000)
		: Widget (l, t_, w, h), t (maxLen, true), error (false), onChange (0), m_ls (0), m_nls (0), m_lcap (0), m_top (0),
		  m_goal (-1), m_cols (1), m_drag (0), m_lastClick (0)
	{ canFocus = true; rewrap (); }
	~MemoEdit () { delete [] m_ls; }
	const char *text () const { return t.buf; }
	void setText (const char *s) { t.set (s ? s : ""); t.caret = 0; m_top = 0; m_goal = -1; rewrap (); invalidate (true); }
	void setError (bool e) { if (e != error) { error = e; invalidate (true); } }
	void resizeTo (int w, int h) override { Widget::resizeTo (w, h); rewrap (); }

	// The visual lines: where each starts (a line of the text cut at the last space that fits).
	void rewrap ()
	{
		int fw = uk_fw ();
		m_cols = imax (4, (width - 12 - UK_SBW) / fw);
		m_nls = 0;
		int i = 0;
		for (;;)
		{
			if (m_nls == m_lcap) { int nc = m_lcap ? m_lcap * 2 : 64; int *n = new int[nc]; for (int k = 0; k < m_nls; k++) n[k] = m_ls[k]; delete [] m_ls; m_ls = n; m_lcap = nc; }
			m_ls[m_nls++] = i;
			int j = i, sp = -1;
			while (j < t.len && t.buf[j] != '\n' && j - i < m_cols) { if (t.buf[j] == ' ') sp = j; j++; }
			if (j >= t.len) break;
			if (t.buf[j] == '\n') { i = j + 1; continue; }
			if (t.buf[j] == ' ') i = j + 1;			// (the space at the edge: the break)
			else if (sp > i) i = sp + 1;
			else i = j;
		}
	}
	int lineOf (int p) const { int k = 0; while (k + 1 < m_nls && m_ls[k + 1] <= p) k++; return k; }
	int lineEnd (int k) const					// a visual line's last position (before its break)
	{
		if (k + 1 >= m_nls) return t.len;
		int e = m_ls[k + 1];
		if (e > 0 && (t.buf[e - 1] == '\n' || t.buf[e - 1] == ' ') && e - 1 >= m_ls[k]) return e - 1;
		return e;
	}
	int rows () const { return imax (1, (height - 6) / uk_fh ()); }
	void scrollToCaret ()
	{
		int k = lineOf (t.caret), r = rows ();
		if (k < m_top) m_top = k;
		if (k >= m_top + r) m_top = k - r + 1;
		clampTop ();
	}
	void clampTop () { m_top = iclamp (m_top, 0, imax (0, m_nls - rows ())); }
	void onDraw () override
	{
		int fw = uk_fw (), fh = uk_fh ();
		canvas.clear (bgColor ());
		bool focus = hasFocus && !disabled;
		uk_sunken (canvas, 0, 0, width, height, 4, disabled ? uk_tone (C_FACE, 150) : C_FIELD, focus && !error);
		if (error) uk_rline (canvas, 0, 0, width, height, 4, C_ERROR, 255);
		int R = rows (), sa = t.selA (), sb = t.selB ();
		unsigned selc = uk_mix (C_FIELD, C_ACCENT, focus ? 96 : 52);
		char line[512];
		for (int r = 0; r < R && m_top + r < m_nls; r++)
		{
			int k = m_top + r, s = m_ls[k], e = lineEnd (k), y = 3 + r * fh;
			if (t.hasSel () && sb > s && sa <= e)
			{
				int a = imax (sa, s) - s, b = imin (sb, e) - s + (sb > e && k + 1 < m_nls ? 1 : 0);
				if (b > a) canvas.fillRect (6 + a * fw, y, (b - a) * fw, fh, selc);
			}
			int n = imin (e - s, (int) sizeof line - 1);
			for (int i = 0; i < n; i++) line[i] = t.buf[s + i];
			line[n] = '\0';
			if (n) canvas.text (6, y, line, disabled ? C_DIS : C_FIELD_TEXT);
		}
		if (focus)
		{
			int k = lineOf (t.caret);
			if (k >= m_top && k < m_top + R) canvas.fillRect (6 + (t.caret - m_ls[k]) * fw, 3 + (k - m_top) * fh, 2, fh, C_ACCENT);
		}
		UkThumb th = uk_thumb (m_nls, R, m_top, height - 6);
		if (th.show) uk_draw_vscroll (canvas, width - UK_SBW - 3, 3, UK_SBW, height - 6, th, C_FIELD, m_drag == 2);
	}
	int posAt (int mx, int my)
	{
		int fw = uk_fw (), fh = uk_fh ();
		int k = iclamp (m_top + (my - 3) / fh, 0, m_nls - 1);
		int c = (mx - 6 + fw / 2) / fw;
		return iclamp (m_ls[k] + imax (0, c), m_ls[k], lineEnd (k));
	}
	bool onMouse (int mx, int my, int bl, int, int, int wheel) override
	{
		if (wheel)
		{
			if (m_nls <= rows ()) return false;		// (nothing to scroll: the form scrolls)
			m_top -= wheel; clampTop (); invalidate (true);
			return true;
		}
		if (mx < 0) { if (m_drag) { m_drag = 0; catchOutside = false; invalidate (true); } pressed = false; return false; }
		if (disabled) return true;
		UkThumb th = uk_thumb (m_nls, rows (), m_top, height - 6);
		if (bl && !pressed)
		{
			pressed = true;
			if (!hasFocus) setFocus ();
			if (th.show && mx >= width - UK_SBW - 4)
			{
				m_drag = 2; catchOutside = true;
				m_top = (int) uk_thumb_pos (my - 3, height - 6, m_nls, rows (), th.h); clampTop ();
				invalidate (true);
				return true;
			}
			bool shift = (kapi_get_modifiers () & MOD_SHIFT) != 0;
			unsigned now = kapi_get_ticks ();
			int p = posAt (mx, my);
			if (now - m_lastClick < 40 && !shift) { t.selectWord (p); m_lastClick = 0; }
			else { t.moveTo (p, shift); if (!shift) t.anchor = p; m_drag = 1; catchOutside = true; m_lastClick = now; }
			m_goal = -1;
			invalidate (true);
		}
		else if (bl && m_drag == 2)
		{
			m_top = (int) uk_thumb_pos (my - 3, height - 6, m_nls, rows (), th.h); clampTop ();
			invalidate (true);
		}
		else if (bl && m_drag == 1)
		{
			if (my < 0 && m_top > 0) m_top--;
			if (my >= height && m_top < m_nls - rows ()) m_top++;
			int p = posAt (mx, my);
			if (p != t.caret) { t.caret = p; invalidate (true); }
		}
		else if (!bl)
		{
			pressed = false;
			if (m_drag) { m_drag = 0; catchOutside = false; if (t.anchor == t.caret) t.anchor = -1; invalidate (true); }
		}
		return true;
	}
	bool onKey (long k) override
	{
		if (disabled) return false;
		int mods = kapi_get_modifiers ();
		bool shift = (mods & MOD_SHIFT) != 0, ctrl = (mods & MOD_CTRL) != 0;
		int kl = lineOf (t.caret);
		switch (k)
		{
		case KEY_LEFT:
			if (t.hasSel () && !shift) t.moveTo (t.selA (), false); else t.moveTo (ctrl ? t.wordLeft (t.caret) : t.caret - 1, shift);
			m_goal = -1; break;
		case KEY_RIGHT:
			if (t.hasSel () && !shift) t.moveTo (t.selB (), false); else t.moveTo (ctrl ? t.wordRight (t.caret) : t.caret + 1, shift);
			m_goal = -1; break;
		case KEY_HOME: t.moveTo (ctrl ? 0 : m_ls[kl], shift); m_goal = -1; break;
		case KEY_END: t.moveTo (ctrl ? t.len : lineEnd (kl), shift); m_goal = -1; break;
		case KEY_UP: case KEY_DOWN: case KEY_PGUP: case KEY_PGDN:
		{
			int by = k == KEY_UP ? -1 : k == KEY_DOWN ? 1 : k == KEY_PGUP ? -(rows () - 1) : rows () - 1;
			if ((k == KEY_UP && kl == 0) || (k == KEY_DOWN && kl == m_nls - 1)) return false;	// (the form: the next field)
			if (m_goal < 0) m_goal = t.caret - m_ls[kl];
			int nk = iclamp (kl + by, 0, m_nls - 1);
			t.moveTo (imin (m_ls[nk] + m_goal, lineEnd (nk)), shift);
			break;
		}
		case KEY_ENTER: { char nl = '\n'; t.insert (&nl, 1, 0); m_goal = -1; rewrap (); scrollToCaret (); invalidate (true); edited (); return true; }
		default:
		{
			int r = t.editKey (k, 0);
			if (!r) return false;
			m_goal = -1;
			if (r == 2) { rewrap (); scrollToCaret (); invalidate (true); edited (); return true; }
		}
		}
		scrollToCaret ();
		invalidate (true);
		return true;
	}
	virtual void edited () { if (onChange) onChange (*this); }
private:
	int *m_ls, m_nls, m_lcap, m_top, m_goal, m_cols, m_drag; unsigned m_lastClick;
};

// ---- a list dropping from a box (a choice's options) ------------------------------------------------------------
class PickList : public Modal
{
public:
	PickList (int x, int y, int w, int boxH, const char *const *items, int n, int sel, int maxRows = 12)
		: Modal (w, 8 + imin (n, maxRows) * (uk_fh () + 8)), m_items (items), m_n (n), m_sel (sel), m_hot (sel), m_top (0),
		  m_rows (imin (n, maxRows)), m_rowH (uk_fh () + 8)
	{
		left = x; top = y + boxH + 2;
		Root *r = Root::current ();
		if (r)
		{
			if (top + height > r->height && y - height - 2 >= 0) top = y - height - 2;	// (above the box)
			if (top + height > r->height) top = imax (0, r->height - height);
			if (left + width > r->width) left = imax (0, r->width - width);
		}
		if (sel >= m_rows) m_top = imin (sel - m_rows / 2, m_n - m_rows);
		if (m_top < 0) m_top = 0;
	}
	int pick () { int r = run (); return r > 0 ? r - 1 : -1; }
	void onDraw () override
	{
		canvas.clear (UK_TRANSPARENT_KEY);
		uk_popup (canvas, 0, 0, width, height, 7, C_FIELD);
		int sbw = m_n > m_rows ? UK_SBW + 2 : 0;
		for (int r = 0; r < m_rows && m_top + r < m_n; r++)
		{
			int i = m_top + r, y = 4 + r * m_rowH;
			bool hot = i == m_hot;
			if (hot) uk_hilite (canvas, 4, y, width - 8 - sbw, m_rowH, 5, true);
			else if (i == m_sel) uk_rline (canvas, 4, y, width - 8 - sbw, m_rowH, 5, uk_mix (C_FIELD, C_ACCENT, 160));
			Canvas c; c.adopt (canvas.px + 10, imax (1, width - 20 - sbw), height, canvas.stride);
			uk_text_l (c, 0, y, m_rowH, m_items[i], hot ? C_SEL_TEXT : C_FIELD_TEXT, i == 0 && m_items[i][0] == '(' ? 1 : 0);
		}
		if (sbw) { UkThumb t = uk_thumb (m_n, m_rows, m_top, height - 8); uk_draw_vscroll (canvas, width - UK_SBW - 4, 4, UK_SBW, height - 8, t, C_FIELD); }
	}
	bool onMouse (int mx, int my, int bl, int, int, int wheel) override
	{
		bool in = mx >= 0 && my >= 0 && mx < width && my < height;
		if (wheel) { if (in) { m_top = iclamp (m_top - wheel, 0, imax (0, m_n - m_rows)); invalidate (true); } return true; }
		bool onBar = m_n > m_rows && in && mx >= width - UK_SBW - 6;
		int hot = in && !onBar && my >= 4 ? m_top + (my - 4) / m_rowH : -1;
		if (hot >= m_n) hot = -1;
		if (hot >= 0 && hot != m_hot) { m_hot = hot; invalidate (true); }
		if (onBar && bl)
		{
			m_top = (int) uk_thumb_pos (my - 4, height - 8, m_n, m_rows, uk_thumb (m_n, m_rows, m_top, height - 8).h);
			invalidate (true);
			return true;
		}
		if (bl && !pressed) { pressed = true; if (!in) close (0); }
		else if (!bl && pressed) { pressed = false; if (hot >= 0) close (hot + 1); }
		return true;
	}
	bool onKey (long k) override
	{
		if (k == 27) { close (0); return true; }
		if (k == KEY_ENTER || k == ' ') { close (m_hot + 1); return true; }
		int d = k == KEY_UP ? -1 : k == KEY_DOWN ? 1 : k == KEY_PGUP ? -m_rows : k == KEY_PGDN ? m_rows : k == KEY_HOME ? -m_n : k == KEY_END ? m_n : 0;
		if (!d) return true;
		m_hot = iclamp (m_hot + d, 0, m_n - 1);
		if (m_hot < m_top) m_top = m_hot;
		if (m_hot >= m_top + m_rows) m_top = m_hot - m_rows + 1;
		invalidate (true);
		return true;
	}
private:
	const char *const *m_items; int m_n, m_sel, m_hot, m_top, m_rows, m_rowH;
};

// ---- a choice among a field's options ---------------------------------------------------------------------------
// Its options: "(none)" then the field's; a value not among them (a file edited by hand) is added at the
// end so that it shows and stays. Up / Down choose the previous / next, Enter / Space / a click drop the
// list, Delete / Backspace: (none), a letter: the next option starting with it.
class ChoiceBox : public Widget
{
public:
	enum { MAXO = MAXCH + 2 };
	const char *opt[MAXO]; int nopt, sel; bool withNone;
	Action onChange;
	ChoiceBox (int l, int t_, int w) : Widget (l, t_, w, ED_H), nopt (1), sel (0), withNone (true), onChange (0), m_hot (false) { canFocus = true; opt[0] = TR ("(none)"); m_extra[0] = '\0'; }
	void setField (const Field &f)
	{
		withNone = true; nopt = 1;
		for (int i = 0; i < f.nch && nopt < MAXO - 1; i++) opt[nopt++] = f.ch[i];
		m_base = nopt;
	}
	// A plain list instead (no "(none)"): sel is the option's index.
	void setOptions (const char *const *o, int n)
	{
		withNone = false; nopt = imin (n, MAXO); m_base = nopt;
		for (int i = 0; i < nopt; i++) opt[i] = o[i];
		sel = 0; invalidate (true);
	}
	void setValue (const char *v)
	{
		nopt = m_base; sel = 0;
		if (*v)
		{
			for (int i = 1; i < nopt; i++) if (seq (opt[i], v)) sel = i;
			if (!sel) { scpy (m_extra, v, sizeof m_extra); opt[nopt] = m_extra; sel = nopt++; }
		}
		invalidate (true);
	}
	const char *value () const { return sel > 0 ? opt[sel] : ""; }
	void choose (int i)
	{
		i = iclamp (i, 0, nopt - 1);
		if (i == sel) return;
		sel = i; invalidate (true);
		if (onChange) onChange (*this);
	}
	void onDraw () override
	{
		canvas.clear (bgColor ());
		int st = disabled ? UK_DISABLED : pressed ? UK_PRESSED : m_hot ? UK_HOT : UK_NORMAL;
		if (hasFocus && !disabled) st |= UK_FOCUS;
		uk_raised (canvas, 0, 0, width, height, 5, C_BUTTON, st);
		unsigned ink = disabled ? uk_mix (C_BUTTON, C_BUTTON_TEXT, 110) : C_BUTTON_TEXT;
		Canvas c; c.adopt (canvas.px + 9, imax (1, width - 34), height, canvas.stride);
		if (sel > 0 || !withNone) uk_text_l (c, 0, 0, height, opt[sel], ink);
		else uk_text_l (c, 0, 0, height, TR ("(none)"), uk_mix (C_BUTTON, C_BUTTON_TEXT, 150), 1);
		uk_glyph (canvas, WKG_CHEV_DOWN, width - 13, height / 2, 9, ink);
	}
	void drop ()
	{
		int x, y; abs_pos (this, &x, &y);
		int w = width;
		for (int i = 0; i < nopt; i++) w = imax (w, uk_text_w (opt[i]) + 34);
		PickList pl (x, y, imin (w, 420), height, opt, nopt, sel);
		int r = pl.pick ();
		setFocus ();
		if (r >= 0) choose (r);
	}
	bool onMouse (int mx, int my, int bl, int, int, int wheel) override
	{
		if (wheel) return false;
		bool in = mx >= 0 && my >= 0 && mx < width && my < height;
		if (in != m_hot) { m_hot = in; invalidate (true); }
		if (disabled) return in;
		if (bl && in && !pressed) { pressed = true; setFocus (); invalidate (true); }
		else if (!bl && pressed) { pressed = false; invalidate (true); if (in) drop (); }
		return in;
	}
	bool onKey (long k) override
	{
		if (disabled) return false;
		if (k == KEY_UP) { choose (sel - 1); return true; }
		if (k == KEY_DOWN) { if (kapi_get_modifiers () & MOD_ALT) drop (); else choose (sel + 1); return true; }
		if (k == KEY_ENTER || k == ' ') { drop (); return true; }
		if (k == KEY_DEL || k == KEY_BACKSPACE) { if (withNone) choose (0); return true; }
		if ((k > ' ' && k <= 126) || (k >= 0xC0 && k <= 0xFF))		// the next option starting with it
		{
			unsigned char c = fold ((unsigned char) k);
			for (int n = 1; n <= nopt; n++)
			{
				int i = (sel + n) % nopt;
				if ((i > 0 || !withNone) && fold ((unsigned char) opt[i][0]) == c) { choose (i); break; }
			}
			return true;
		}
		return false;
	}
private:
	bool m_hot; int m_base = 1; char m_extra[VAL_MAX];
};

// ---- yes / no -----------------------------------------------------------------------------------------------------
class YesNoBox : public Checkbox
{
public:
	YesNoBox (int l, int t_, int w) : Checkbox (l, t_, w, ED_H, "", false, 0, C_BG) {}
	unsigned bgColor () override { return parent ? parent->bgColor () : C_BG; }
	void onDraw () override { bg = bgColor (); Checkbox::onDraw (); }
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override
	{
		if (wheel) return false;			// (the form scrolls)
		return Checkbox::onMouse (mx, my, bl, br, bm, wheel);
	}
};

// ---- a date: typed, or picked on a calendar --------------------------------------------------------------------------
class CalPopup : public Modal
{
public:
	Calendar *cal; int result;			// 1 a day picked (cal's), 2 cleared, 0 nothing
	CalPopup (int x, int y, int boxH, int yy, int mm, int dd) : Modal (CAL_W + 12, CAL_H + 12 + 34), result (0)
	{
		left = x; top = y + boxH + 2;
		Root *r = Root::current ();
		if (r)
		{
			if (top + height > r->height && y - height - 2 >= 0) top = y - height - 2;
			if (top + height > r->height) top = imax (0, r->height - height);
			if (left + width > r->width) left = imax (0, r->width - width);
		}
		cal = new Calendar (6, 6, yy, mm, dd, picked);
		addChild (cal);
		Button *b = new Button (6, CAL_H + 12, 80, 28, TR ("Today"), button); b->tag = 3; addChild (b);
		b = new Button (width - 86, CAL_H + 12, 80, 28, TR ("Clear"), button); b->tag = 2; addChild (b);
		cal->setFocus ();
	}
	static void picked (Widget &w) { ((Modal *) w.parent)->close (1); }
	static void button (Widget &w)
	{
		CalPopup *p = (CalPopup *) w.parent;
		if (w.tag == 3) { int y, m, d; today (&y, &m, &d); p->cal->setDate (y, m, d); p->close (1); }
		else p->close (2);
	}
	void onDraw () override { canvas.clear (UK_TRANSPARENT_KEY); uk_popup (canvas, 0, 0, width, height, 8, C_FACE); }
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		bool in = mx >= 0 && my >= 0 && mx < width && my < height;
		if (bl && !pressed) { pressed = true; if (!in) close (0); }
		else if (!bl) pressed = false;
		return true;
	}
	bool onKey (long k) override { if (k == 27) { close (0); return true; } return false; }
};

class DateEdit : public LineEdit
{
public:
	DateEdit (int l, int t_, int w) : LineEdit (l, t_, w), m_btnHot (false) { padR = 24; accept = accept_date; placeholder = TR ("DD/MM/YYYY"); }
	void drawExtra () override
	{
		int bw = 20, bx = width - bw - 3, bh = height - 6;
		uk_raised (canvas, bx, 3, bw, bh, 3, C_BUTTON, disabled ? UK_DISABLED : m_btnHot ? UK_HOT : UK_NORMAL);
		int gx = bx + (bw - 11) / 2, gy = 3 + (bh - 10) / 2;		// a small calendar page
		uk_rbox (canvas, gx, gy, 11, 10, 2, 0x00FFFFFF, uk_tone (C_FIELD, 120));
		uk_rbox (canvas, gx, gy, 11, 3, 1, 0x00D05048, 0x00B8403A, 255, UK_TL | UK_TR);
		uk_rline (canvas, gx, gy, 11, 10, 2, uk_tone (C_FACE, 70), 200);
		for (int i = 0; i < 3; i++) canvas.fillRect (gx + 2 + i * 3, gy + 5, 2, 2, C_TEXT);
	}
	void drop ()
	{
		int y, m, d;
		if (!parse_date (text (), &y, &m, &d)) today (&y, &m, &d);
		int ax, ay; abs_pos (this, &ax, &ay);
		CalPopup cp (ax, ay, height, y, m, d);
		int r = cp.run ();
		setFocus ();
		if (r == 1) { char s[12]; show_date (cp.cal->year, cp.cal->month, cp.cal->day, s); setText (s); edited (); }
		else if (r == 2 && t.len) { setText (""); edited (); }
	}
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override
	{
		bool onBtn = mx >= width - 24 && mx < width && my >= 0 && my < height;
		if (onBtn != m_btnHot) { m_btnHot = onBtn; invalidate (true); }
		if (onBtn && !wheel && !disabled)
		{
			if (bl && !pressed) { pressed = true; if (!hasFocus) setFocus (); return true; }
			if (!bl && pressed) { pressed = false; drop (); return true; }
			return true;
		}
		return LineEdit::onMouse (mx, my, bl, br, bm, wheel);
	}
	bool onKey (long k) override
	{
		if (k == KEY_DOWN && (kapi_get_modifiers () & MOD_ALT)) { drop (); return true; }
		return LineEdit::onKey (k);
	}
private:
	bool m_btnHot;
};

// ---- a colour -------------------------------------------------------------------------------------------------------
static const unsigned PALETTE[40] = {
	0x000000, 0x404040, 0x707070, 0x9E9E9E, 0xC4C4C4, 0xE2E2E2, 0xF4F4F4, 0xFFFFFF,
	0x7F1D1D, 0xC0392B, 0xE74C3C, 0xF1948A, 0xE67E22, 0xF39C12, 0xF7C948, 0xFFF3B0,
	0x145A32, 0x1E8449, 0x27AE60, 0x82E0AA, 0x0E6655, 0x16A085, 0x48C9B0, 0xB9F2E3,
	0x1B2A6B, 0x1F4E9C, 0x2E86DE, 0x85C1E9, 0x4A235A, 0x7D3C98, 0xA569BD, 0xD7BDE2,
	0x6E2C00, 0xA0522D, 0xD2A679, 0xF5DEB3, 0x880E4F, 0xC2185B, 0xF06292, 0xF8BBD0 };

// The palette dropping from a colour: "No colour", 40 swatches, "More Colours..." (the colour dialog).
class ColorPopup : public Modal
{
public:
	enum { CS = 18, GAP = 4, PAD = 8, PER = 8, ROWH = 26 };
	unsigned color; bool none;
	ColorPopup (int x, int y, int boxH, unsigned cur, bool isNone)
		: Modal (2 * PAD + PER * (CS + GAP) - GAP, 2 * PAD + 2 * ROWH + 12 + 5 * (CS + GAP) - GAP), color (cur), none (isNone), m_hot (-3)
	{
		left = x; top = y + boxH + 2;
		Root *r = Root::current ();
		if (r)
		{
			if (top + height > r->height && y - height - 2 >= 0) top = y - height - 2;
			if (top + height > r->height) top = imax (0, r->height - height);
			if (left + width > r->width) left = imax (0, r->width - width);
		}
	}
	// 1 a colour chosen (color), 2 none, 0 nothing
	int pick ()
	{
		int r = run ();
		if (r == 2) return 2;
		if (r == 3) { unsigned c = color; if (uk_color_dialog (&c, TR ("Colour"))) { color = c; return 1; } return 0; }
		if (r >= 10) { color = PALETTE[r - 10]; return 1; }
		return 0;
	}
	void onDraw () override
	{
		canvas.clear (UK_TRANSPARENT_KEY);
		uk_popup (canvas, 0, 0, width, height, 7, C_FIELD);
		int w = width - 2 * PAD;
		if (m_hot == -1) uk_hilite (canvas, PAD - 3, PAD, w + 6, ROWH, 5, true);
		canvas.frameRect (PAD + 2, PAD + 5, 16, 16, uk_mix (C_FIELD, C_FIELD_TEXT, 150));
		for (int k = 0; k < 12; k++) canvas.pixel (PAD + 4 + k, PAD + 18 - k, 0xC0392B);
		uk_text_l (canvas, PAD + 26, PAD, ROWH, TR ("No colour"), m_hot == -1 ? C_SEL_TEXT : C_FIELD_TEXT);
		int y0 = PAD + ROWH + 6;
		for (int i = 0; i < 40; i++)
		{
			int x = PAD + (i % PER) * (CS + GAP), y = y0 + (i / PER) * (CS + GAP);
			uk_rbox (canvas, x, y, CS, CS, 3, PALETTE[i], PALETTE[i]);
			bool cur = !none && PALETTE[i] == color;
			uk_rline (canvas, x, y, CS, CS, 3, i == m_hot || cur ? C_ACCENT : uk_mix (PALETTE[i], 0, 70), i == m_hot || cur ? 255 : 150);
			if (i == m_hot || cur) uk_rline (canvas, x - 1, y - 1, CS + 2, CS + 2, 4, C_ACCENT, i == m_hot ? 255 : 150);
		}
		int yb = height - PAD - ROWH;
		if (m_hot == -2) uk_hilite (canvas, PAD - 3, yb, w + 6, ROWH, 5, true);
		static const unsigned rb[4] = { 0xE74C3C, 0xF1C40F, 0x2ECC71, 0x3498DB };
		for (int k = 0; k < 4; k++) canvas.fillRect (PAD + 2 + (k & 1) * 8, yb + 5 + (k >> 1) * 8, 8, 8, rb[k]);
		uk_text_l (canvas, PAD + 26, yb, ROWH, TR ("More Colours..."), m_hot == -2 ? C_SEL_TEXT : C_FIELD_TEXT);
	}
	int hitAt (int mx, int my)
	{
		if (mx < PAD - 3 || mx >= width - PAD + 3 || my < 0 || my >= height) return -3;
		if (my >= PAD && my < PAD + ROWH) return -1;
		if (my >= height - PAD - ROWH && my < height - PAD) return -2;
		int y0 = PAD + ROWH + 6;
		if (my < y0) return -3;
		int c = (mx - PAD) / (CS + GAP), r = (my - y0) / (CS + GAP);
		if (c < 0 || c >= PER || r > 4) return -3;
		return r * PER + c;
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		int h = hitAt (mx, my);
		if (h != m_hot) { m_hot = h; invalidate (true); }
		bool in = mx >= 0 && my >= 0 && mx < width && my < height;
		if (bl && !pressed) { pressed = true; if (!in) close (0); }
		else if (!bl && pressed) { pressed = false; if (h == -1) close (2); else if (h == -2) close (3); else if (h >= 0) close (10 + h); }
		return true;
	}
	bool onKey (long k) override { if (k == 27) close (0); return true; }
private:
	int m_hot;
};

// A colour field: the swatch and the code in a field; a click (Enter, Space) drops the palette,
// Delete empties it.
class ColorEdit : public Widget
{
public:
	unsigned color; bool none;
	Action onChange;
	ColorEdit (int l, int t_, int w) : Widget (l, t_, w, ED_H), color (0), none (true), onChange (0), m_hot (false) { canFocus = true; }
	void setValue (const char *v)
	{
		unsigned c;
		if (*v && parse_color (v, &c)) { color = c; none = false; } else none = true;
		invalidate (true);
	}
	void value (char *out) const { if (none) out[0] = '\0'; else fmt_color (color, out); }
	void onDraw () override
	{
		canvas.clear (bgColor ());
		bool focus = hasFocus && !disabled;
		uk_sunken (canvas, 0, 0, width, height, 4, disabled ? uk_tone (C_FACE, 150) : C_FIELD, focus);
		int sw = 34, sh = height - 10;
		if (none)
		{
			uk_rline (canvas, 5, 5, sw, sh, 3, uk_mix (C_FIELD, C_FIELD_TEXT, 110), 200);
			for (int k = 0; k < sh - 4; k++) canvas.pixel (7 + k * (sw - 4) / (sh - 4), 7 + (sh - 5) - k, 0xC0392B);
			uk_text_l (canvas, sw + 12, 0, height, TR ("None"), uk_mix (C_FIELD, C_FIELD_TEXT, 130), 1);
		}
		else
		{
			uk_rbox (canvas, 5, 5, sw, sh, 3, color, color);
			uk_rline (canvas, 5, 5, sw, sh, 3, uk_mix (color, 0, 90), 200);
			char h[8]; fmt_color (color, h);
			uk_text_l (canvas, sw + 12, 0, height, h, disabled ? C_DIS : C_FIELD_TEXT);
		}
		int bx = width - 23, bh = height - 6;
		uk_raised (canvas, bx, 3, 20, bh, 3, C_BUTTON, disabled ? UK_DISABLED : m_hot ? UK_HOT : UK_NORMAL);
		uk_glyph (canvas, WKG_CHEV_DOWN, bx + 10, height / 2, 8, C_BUTTON_TEXT);
	}
	void drop ()
	{
		int x, y; abs_pos (this, &x, &y);
		ColorPopup cp (x, y, height, color, none);
		int r = cp.pick ();
		setFocus ();
		if (r == 1 && (none || cp.color != color)) { color = cp.color; none = false; invalidate (true); if (onChange) onChange (*this); }
		else if (r == 2 && !none) { none = true; invalidate (true); if (onChange) onChange (*this); }
	}
	bool onMouse (int mx, int my, int bl, int, int, int wheel) override
	{
		if (wheel) return false;
		bool in = mx >= 0 && my >= 0 && mx < width && my < height;
		if (in != m_hot) { m_hot = in; invalidate (true); }
		if (disabled) return in;
		if (bl && in && !pressed) { pressed = true; setFocus (); }
		else if (!bl && pressed) { pressed = false; if (in) drop (); }
		return in;
	}
	bool onKey (long k) override
	{
		if (disabled) return false;
		if (k == KEY_ENTER || k == ' ' || (k == KEY_DOWN && (kapi_get_modifiers () & MOD_ALT))) { drop (); return true; }
		if (k == KEY_DEL || k == KEY_BACKSPACE) { if (!none) { none = true; invalidate (true); if (onChange) onChange (*this); } return true; }
		return false;
	}
private:
	bool m_hot;
};

// ---- the search box --------------------------------------------------------------------------------------------------
// A magnifier, the words typed (the records shown are those holding them all), a cross that empties it.
// Esc empties it too; Enter / Down hand the keyboard back (onEnter).
class SearchBox : public LineEdit
{
public:
	SearchBox (int w) : LineEdit (0, 0, w, BTN), m_xHot (false) { padL = 22; padR = 22; placeholder = TR ("Search"); tip = TR ("Search the records (Ctrl+F)"); }
	void drawExtra () override
	{
		draw_icon (canvas, IC_SEARCH, 6, (height - 20) / 2 + 2, uk_mix (C_FIELD, C_FIELD_TEXT, 150));
		if (t.len) uk_glyph (canvas, WKG_CLOSE, width - 12, height / 2, 8, m_xHot ? C_ACCENT : uk_mix (C_FIELD, C_FIELD_TEXT, 150));
	}
	void clear () { if (t.len) { setText (""); edited (); } }
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override
	{
		bool onX = t.len && mx >= width - 22 && mx < width && my >= 0 && my < height;
		if (onX != m_xHot) { m_xHot = onX; invalidate (true); }
		if (onX && bl && !pressed) { pressed = true; return true; }
		if (onX && !bl && pressed) { pressed = false; clear (); return true; }
		return LineEdit::onMouse (mx, my, bl, br, bm, wheel);
	}
	bool onKey (long k) override
	{
		if (k == 27) { if (t.len) clear (); else if (onEnter) onEnter (*this); return true; }
		if (k == KEY_DOWN || k == KEY_TAB) { if (onEnter) onEnter (*this); return true; }
		return LineEdit::onKey (k);
	}
private:
	bool m_xHot;
};

// ---- the record navigator and the status, at the window's foot -------------------------------------------------------
// |< < "Record 3 of 12" > >| + (the form and the list); then what the view shows (a search, the sort, the
// fields); at the right, the file's name (modified: a dot before it).
class NavBar : public Widget
{
public:
	enum { B_FIRST, B_PREV, B_NEXT, B_LAST, B_NEW, B_POS, NB };
	char pos[48], info[128], file[128]; bool nav, modified, canPrev, canNext, recDirty;
	void (*onNav) (int b);
	NavBar (int l, int t_, int w) : Widget (l, t_, w, NAV_H), nav (true), modified (false), canPrev (false), canNext (false), recDirty (false),
		onNav (0), m_hot (-1), m_down (-1) { pos[0] = info[0] = file[0] = '\0'; }
	void set (bool nav_, const char *p, bool prev, bool next, const char *i, const char *f, bool mod, bool dirty)
	{
		if (nav_ == nav && seq (pos, p) && prev == canPrev && next == canNext && seq (info, i) && seq (file, f) && mod == modified && dirty == recDirty) return;
		nav = nav_; scpy (pos, p, sizeof pos); canPrev = prev; canNext = next; scpy (info, i, sizeof info); scpy (file, f, sizeof file);
		modified = mod; recDirty = dirty;
		invalidate (true);
	}
	int bx (int b) const { return b < B_NEXT ? 8 + b * 26 : b == B_POS ? 8 + 2 * 26 + 4 : 8 + 2 * 26 + 4 + posW () + 4 + (b - B_NEXT) * 26 + (b == B_NEW ? 6 : 0); }
	int bw (int b) const { return b == B_POS ? posW () : 24; }
	int posW () const { return imax (136, uk_text_w (pos) + 36); }
	void onDraw () override
	{
		uk_rbox (canvas, 0, 0, width, height, 0, uk_tone (C_BG, 150), uk_tone (C_BG, 120));
		uk_etch_h (canvas, 0, 0, width, C_BG);
		unsigned ink = uk_ink_for (C_BG), dim = uk_mix (C_BG, ink, 170);
		int x = 12;
		if (nav)
		{
			for (int b = 0; b < NB; b++)
			{
				int X = bx (b), W = bw (b), y = 4, h = height - 7;
				bool en = b == B_POS || b == B_NEW || (b <= B_PREV ? canPrev : canNext);
				if (b == B_POS)
				{
					uk_sunken (canvas, X, y, W, h, 4, uk_mix (C_FIELD, C_BG, 60), m_hot == b);
					if (recDirty) uk_glyph (canvas, WKG_DOT, X + 11, height / 2, 8, C_ACCENT);
					uk_text_c (canvas, X + 8, y, W - 16, h, pos, C_FIELD_TEXT);
					continue;
				}
				if (en && (m_hot == b || m_down == b))
					uk_rbox (canvas, X, y, W, h, 4, uk_tone (C_BG, m_down == b ? 110 : 170), uk_tone (C_BG, m_down == b ? 120 : 140));
				nav_glyph (canvas, b == B_FIRST ? NG_FIRST : b == B_PREV ? NG_PREV : b == B_NEXT ? NG_NEXT : b == B_LAST ? NG_LAST : NG_NEW,
					   X + W / 2, height / 2, en ? (b == B_NEW ? C_ACCENT : ink) : uk_mix (C_BG, ink, 90));
			}
			x = bx (B_NEW) + 24 + 14;
			uk_etch_v (canvas, x - 7, 6, height - 12, C_BG);
		}
		Canvas c; c.adopt (canvas.px + x, imax (1, width - x - 12 - uk_text_w (file) - 30), height, canvas.stride);
		uk_text_l (c, 0, 1, height - 1, info, dim);
		int fx = width - 12 - uk_text_w (file);
		if (modified) uk_glyph (canvas, WKG_DOT, fx - 12, height / 2, 8, C_ACCENT);
		uk_text_l (canvas, fx, 1, height - 1, file, dim);
	}
	bool onMouse (int mx, int my, int bl, int, int, int wheel) override
	{
		if (wheel) return false;
		int h = -1;
		if (nav && my >= 0 && my < height) for (int b = 0; b < NB; b++) if (mx >= bx (b) && mx < bx (b) + bw (b)) h = b;
		if (h != m_hot) { m_hot = h; invalidate (true); }
		if (bl && m_down < 0 && h >= 0) { m_down = h; invalidate (true); }
		else if (!bl && m_down >= 0)
		{
			int d = m_down; m_down = -1; invalidate (true);
			if (d == h && onNav) onNav (d);
		}
		return mx >= 0 && my >= 0 && mx < width && my < height;
	}
private:
	int m_hot, m_down;
};

} // namespace cf

#endif
