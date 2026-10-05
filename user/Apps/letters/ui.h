//
// ui.h -- Letters' controls around the page: the toolbar's buttons (an icon; toggled: lit; split:
// an arrow part opening a palette), the toolbars, the pick boxes (a value shown -- a font's name in
// that font, a style in its look -- and a list dropping from it, over the window), the editable
// size box, the colour palettes, the ruler (the text's width on the page, the indents' markers
// dragged), the status bar.
//
#ifndef _writer_ui_h
#define _writer_ui_h

#include "icons.h"
#include "view.h"

namespace wr {

using namespace uikit;

enum { TB_H = 34, BTN = 28, SPLIT_W = 13, RULER_H = 26, STATUS_H = 24 };

// ---- a toolbar button -----------------------------------------------------------------------------------
class ToolButton : public Widget
{
public:
	int icon; void (*cb) (); bool on; bool split; unsigned bar; void (*arrow) (ToolButton &);
	ToolButton (int ic, const char *tip_, void (*cb_) (), bool split_ = false)
		: Widget (0, 0, split_ ? BTN + SPLIT_W : BTN, BTN), icon (ic), cb (cb_), on (false), split (split_),
		  bar (0xFF000000u), arrow (0), m_part (-1), m_down (-1) { tip = tip_; }
	void setOn (bool v) { if (v != on) { on = v; invalidate (true); } }
	void setBar (unsigned c) { if (c != bar) { bar = c; invalidate (true); } }
	void setDisabled (bool d) { if (d != disabled) { disabled = d; invalidate (true); } }
	unsigned bgColor () override { return parent ? parent->bgColor () : C_BG; }
	void onDraw () override
	{
		unsigned bg = bgColor ();
		canvas.clear (bg);
		int bw = split ? BTN : width;
		if (on) { uk_rbox (canvas, 0, 0, bw, height, 5, uk_mix (bg, C_ACCENT, 60), uk_mix (bg, C_ACCENT, 70)); uk_rline (canvas, 0, 0, bw, height, 5, uk_mix (bg, C_ACCENT, 150)); }
		if (!disabled && m_part >= 0)
		{
			bool dn = m_down >= 0;
			unsigned a = dn ? uk_tone (bg, 110) : uk_tone (bg, 150), b = dn ? uk_tone (bg, 118) : uk_tone (bg, 138);
			if (!on || split) uk_rbox (canvas, 0, 0, width, height, 5, a, b);
			uk_rline (canvas, 0, 0, width, height, 5, uk_tone (bg, 96), 150);
			if (split) canvas.fillRect (BTN, 4, 1, height - 8, uk_tone (bg, 110));
		}
		unsigned ink = uk_ink_for (bg);
		int d = m_down == 0 ? 1 : 0;
		draw_icon (canvas, icon, (bw - 20) / 2 + d, (height - 20) / 2 + d - (bar != 0xFF000000u ? 2 : 0), ink, disabled);
		if (bar != 0xFF000000u)
		{
			unsigned c = bar == 0xFE000000u ? uk_mix (bg, ink, 60) : bar;
			canvas.fillRect ((bw - 18) / 2 + d, height - 7 + d, 18, 4, c);
			if (bar == 0xFE000000u) canvas.frameRect ((bw - 18) / 2 + d, height - 7 + d, 18, 4, uk_mix (bg, ink, 120));
		}
		if (split) uk_glyph (canvas, WKG_CHEV_DOWN, BTN + SPLIT_W / 2, height / 2 + 1, 7, disabled ? uk_mix (bg, ink, 110) : ink);
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		int part = mx < 0 || my < 0 || mx >= width || my >= height ? -1 : split && mx >= BTN ? 1 : 0;
		if (part != m_part) { m_part = part; invalidate (true); }
		if (disabled) return part >= 0;
		if (bl && m_down < 0 && part >= 0) { m_down = part; invalidate (true); }
		else if (!bl && m_down >= 0)
		{
			int was = m_down; m_down = -1; invalidate (true);
			if (part == was)
			{
				if (was == 1 && arrow) arrow (*this);
				else if (cb) cb ();
			}
		}
		return part >= 0;
	}
private:
	int m_part, m_down;			// the part under the pointer, the one pressed (-1 none)
};

// ---- a toolbar ----------------------------------------------------------------------------------------
class ToolBar : public Widget
{
public:
	ToolBar (int l, int t, int w) : Widget (l, t, w, TB_H), m_x (6), m_nsep (0) {}
	void add (Widget *w, int gap = 1)
	{
		m_x += gap;
		w->left = m_x; w->top = (TB_H - w->height) / 2;
		addChild (w);
		m_x += w->width;
	}
	void sep () { if (m_nsep < 16) m_sepX[m_nsep++] = m_x + 5; m_x += 11; }
	void onDraw () override
	{
		canvas.fillRect (0, 0, width, height, C_BG);
		for (int i = 0; i < m_nsep; i++) uk_etch_v (canvas, m_sepX[i], 7, height - 14, C_BG);
	}
private:
	int m_x, m_nsep, m_sepX[16];
};

// ---- a list dropping from a box (over the window) --------------------------------------------------------
typedef void (*RowDraw) (Canvas &cv, int i, int x, int y, int w, int h, unsigned ink);

class ListPopup : public Modal
{
public:
	ListPopup (int x, int y, int w, int n, int rowH, int sel, RowDraw draw, int maxRows = 14)
		: Modal (w, 8 + (n < maxRows ? n : maxRows) * rowH), m_n (n), m_rowH (rowH), m_sel (sel), m_hot (sel),
		  m_top (0), m_rows (n < maxRows ? n : maxRows), m_draw (draw)
	{
		left = x; top = y;
		Root *r = Root::current ();
		if (r)
		{
			if (top + height > r->height) top = wmax (0, y - height - 30);
			if (left + width > r->width) left = wmax (0, r->width - width);
		}
		if (sel >= m_rows) m_top = wmin (sel - m_rows / 2, m_n - m_rows);
		if (m_top < 0) m_top = 0;
	}
	int pick () { int r = run (); return r > 0 ? r - 1 : -1; }
	void onDraw () override
	{
		canvas.clear (UK_TRANSPARENT_KEY);
		uk_popup (canvas, 0, 0, width, height, 7, C_FIELD);
		int sbw = m_n > m_rows ? UK_SBW + 2 : 0;
		for (int r = 0; r < m_rows; r++)
		{
			int i = m_top + r, y = 4 + r * m_rowH;
			if (i >= m_n) break;
			bool hot = i == m_hot;
			if (hot) uk_hilite (canvas, 4, y, width - 8 - sbw, m_rowH, 5, true);
			else if (i == m_sel) uk_rline (canvas, 4, y, width - 8 - sbw, m_rowH, 5, uk_mix (C_FIELD, C_ACCENT, 160));
			m_draw (canvas, i, 10, y, width - 20 - sbw, m_rowH, hot ? C_SEL_TEXT : C_FIELD_TEXT);
		}
		if (sbw)
		{
			UkThumb t = uk_thumb (m_n, m_rows, m_top, height - 8);
			uk_draw_vscroll (canvas, width - UK_SBW - 4, 4, UK_SBW, height - 8, t, C_FIELD);
		}
	}
	bool onMouse (int mx, int my, int bl, int, int, int wheel) override
	{
		bool in = mx >= 0 && my >= 0 && mx < width && my < height;
		if (wheel && in) { m_top = wclamp (m_top - wheel, 0, wmax (0, m_n - m_rows)); invalidate (true); return true; }
		int hot = in && my >= 4 ? m_top + (my - 4) / m_rowH : -1;
		if (hot >= m_n || (m_n > m_rows && mx >= width - UK_SBW - 6)) hot = -1;
		if (hot != m_hot && hot >= 0) { m_hot = hot; invalidate (true); }
		if (m_n > m_rows && in && bl && mx >= width - UK_SBW - 6)	// (the scroll bar)
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
		if (k == KEY_ENTER) { close (m_hot + 1); return true; }
		int d = k == KEY_UP ? -1 : k == KEY_DOWN ? 1 : k == KEY_PGUP ? -m_rows : k == KEY_PGDN ? m_rows : 0;
		if (!d) return true;
		m_hot = wclamp (m_hot + d, 0, m_n - 1);
		if (m_hot < m_top) m_top = m_hot;
		if (m_hot >= m_top + m_rows) m_top = m_hot - m_rows + 1;
		invalidate (true);
		return true;
	}
private:
	int m_n, m_rowH, m_sel, m_hot, m_top, m_rows;
	RowDraw m_draw;
};

// A box showing a value (drawn by the app) with an arrow: a click drops its list.
class PickBox : public Widget
{
public:
	void (*drawValue) (Canvas &cv, int x, int y, int w, int h, unsigned ink);
	void (*onClick) (PickBox &);
	PickBox (int w, const char *tip_, void (*dv) (Canvas &, int, int, int, int, unsigned), void (*oc) (PickBox &))
		: Widget (0, 0, w, 26), drawValue (dv), onClick (oc), m_hot (false) { tip = tip_; }
	unsigned bgColor () override { return parent ? parent->bgColor () : C_BG; }
	void onDraw () override
	{
		canvas.clear (bgColor ());
		uk_sunken (canvas, 0, 0, width, height, 5, C_FIELD, m_hot);
		drawValue (canvas, 7, 2, width - 28, height - 4, C_FIELD_TEXT);
		uk_raised (canvas, width - 20, 3, 17, height - 6, 3, C_BUTTON, m_hot ? UK_HOT : UK_NORMAL);
		uk_glyph (canvas, WKG_CHEV_DOWN, width - 12, height / 2, 8, C_BUTTON_TEXT);
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		bool in = mx >= 0 && my >= 0 && mx < width && my < height;
		if (in != m_hot) { m_hot = in; invalidate (true); }
		if (bl && in && !pressed) pressed = true;
		else if (!bl && pressed) { pressed = false; if (in && onClick) onClick (*this); }
		return in;
	}
	// Where its list drops (root coordinates).
	void below (int *x, int *y)
	{
		int ax = 0, ay = 0;
		for (Widget *w = this; w && w->parent; w = w->parent) { ax += w->left - w->parent->scrollX; ay += w->top - w->parent->scrollY; }
		*x = ax; *y = ay + height + 2;
	}
private:
	bool m_hot;
};

// The size box: a number typed (Enter applies it) or picked from its list.
class SizeBox : public Textbox
{
public:
	void (*onPick) (int halfPoints);
	SizeBox (int w, void (*op) (int)) : Textbox (0, 0, w, 26, "12", 0), onPick (op), m_arrowHot (false)
	{ padR = 18; tip = "Font size"; cb = enter; s_me = this; }
	void setValue (int hp)
	{
		if (hasFocus) return;
		char b[16]; int n = 0; int v = hp / 2;
		char t[8]; int j = 0; do { t[j++] = (char) ('0' + v % 10); v /= 10; } while (v);
		while (j) b[n++] = t[--j];
		if (hp & 1) { b[n++] = '.'; b[n++] = '5'; }
		b[n] = 0;
		int k = 0; while (b[k] && text[k] == b[k]) k++;
		if (b[k] || text[k]) setText (b);
	}
	unsigned bgColor () override { return parent ? parent->bgColor () : C_BG; }
	void onDraw () override
	{
		Textbox::onDraw ();
		uk_raised (canvas, width - 18, 3, 15, height - 6, 3, C_BUTTON, m_arrowHot ? UK_HOT : UK_NORMAL);
		uk_glyph (canvas, WKG_CHEV_DOWN, width - 11, height / 2, 8, C_BUTTON_TEXT);
	}
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override
	{
		bool ah = mx >= width - 18 && mx < width && my >= 0 && my < height;
		if (ah != m_arrowHot) { m_arrowHot = ah; invalidate (true); }
		if (ah && bl && !pressed) { pressed = true; return true; }
		if (ah && !bl && pressed) { pressed = false; drop (); return true; }
		return Textbox::onMouse (mx, my, bl, br, bm, wheel);
	}
private:
	bool m_arrowHot;
	static SizeBox *s_me;
	static void rowDraw (Canvas &cv, int i, int x, int y, int, int h, unsigned ink)
	{
		char b[8]; int v = SIZES[i] / 2, n = 0; char t[8]; int j = 0;
		do { t[j++] = (char) ('0' + v % 10); v /= 10; } while (v);
		while (j) b[n++] = t[--j];
		b[n] = 0;
		uk_text_l (cv, x, y, h, b, ink);
	}
	static void enter (Widget &w)
	{
		SizeBox &s = (SizeBox &) w;
		int v = 0, frac = 0; bool dot = false, any = false;
		for (const char *p = s.text; *p; p++)
		{
			if (*p >= '0' && *p <= '9') { if (dot) { if (!frac) frac = *p - '0'; } else v = v * 10 + (*p - '0'); any = true; }
			else if (*p == '.' || *p == ',') dot = true;
		}
		if (any && v >= 1 && v <= 1638 && s.onPick) s.onPick (v * 2 + (frac >= 5 ? 1 : 0));
	}
	void drop ()
	{
		int ax = 0, ay = 0;
		for (Widget *w = this; w && w->parent; w = w->parent) { ax += w->left; ay += w->top; }
		int n = (int) (sizeof SIZES / sizeof SIZES[0]), sel = -1;
		for (int i = 0; i < n; i++) { int v = 0; for (const char *p = text; *p >= '0' && *p <= '9'; p++) v = v * 10 + (*p - '0'); if (SIZES[i] == v * 2) sel = i; }
		ListPopup lp (ax, ay + height + 2, width + 20, n, 24, sel, rowDraw, 12);
		int r = lp.pick ();
		if (r >= 0 && onPick) onPick (SIZES[r]);
	}
};
SizeBox *SizeBox::s_me;

// ---- the colour palettes ----------------------------------------------------------------------------------
// A grid of colours dropped from a split button: the automatic one (or none) on top, the colours,
// "More Colours..." (the colour dialog) below. pick (): the colour, AUTO, or -1 (nothing).
class ColorPopup : public Modal
{
public:
	enum { CS = 18, PAD = 8 };
	ColorPopup (int x, int y, const unsigned *cols, int n, int perRow, const char *autoLabel)
		: Modal (PAD * 2 + perRow * (CS + 4) - 4, 0), m_cols (cols), m_n (n), m_per (perRow), m_auto (autoLabel), m_hot (-3)
	{
		int rows = (n + perRow - 1) / perRow;
		resizeTo (width, PAD + 26 + 6 + rows * (CS + 4) + 6 + 26 + PAD);
		left = x; top = y;
		Root *r = Root::current ();
		if (r) { if (left + width > r->width) left = r->width - width; if (top + height > r->height) top = wmax (0, r->height - height); }
	}
	long pick ()
	{
		int r = run ();
		if (r == 1) return (long) AUTO;
		if (r == 2) { unsigned c = m_last; if (uk_color_dialog (&c, "More Colours")) { m_last = c; return (long) c; } return -1; }
		if (r >= 3) return (long) m_cols[r - 3];
		return -1;
	}
	static unsigned m_last;
	void onDraw () override
	{
		canvas.clear (UK_TRANSPARENT_KEY);
		uk_popup (canvas, 0, 0, width, height, 7, C_FIELD);
		int w = width - 2 * PAD;
		if (m_hot == -1) uk_hilite (canvas, PAD - 2, PAD, w + 4, 26, 5, true);
		canvas.frameRect (PAD + 4, PAD + 5, 16, 16, uk_mix (C_FIELD, C_FIELD_TEXT, 150));
		if (m_auto[0] == 'A') canvas.fillRect (PAD + 6, PAD + 7, 12, 12, 0);
		else { for (int k = 0; k < 12; k++) canvas.pixel (PAD + 6 + k, PAD + 18 - k, 0xC0392B); }
		uk_text_l (canvas, PAD + 28, PAD, 26, m_auto, m_hot == -1 ? C_SEL_TEXT : C_FIELD_TEXT);
		int y0 = PAD + 26 + 6;
		for (int i = 0; i < m_n; i++)
		{
			int x = PAD + (i % m_per) * (CS + 4), y = y0 + (i / m_per) * (CS + 4);
			canvas.fillRect (x, y, CS, CS, m_cols[i]);
			canvas.frameRect (x, y, CS, CS, uk_mix (m_cols[i], 0x000000, 60));
			if (i == m_hot) { canvas.frameRect (x - 2, y - 2, CS + 4, CS + 4, C_ACCENT); canvas.frameRect (x - 1, y - 1, CS + 2, CS + 2, 0xFFFFFF); }
		}
		int yb = height - PAD - 26;
		if (m_hot == -2) uk_hilite (canvas, PAD - 2, yb, w + 4, 26, 5, true);
		uk_text_l (canvas, PAD + 28, yb, 26, "More Colours...", m_hot == -2 ? C_SEL_TEXT : C_FIELD_TEXT);
		static const unsigned rb[4] = { 0xE74C3C, 0xF1C40F, 0x2ECC71, 0x3498DB };
		for (int k = 0; k < 4; k++) canvas.fillRect (PAD + 4 + (k & 1) * 8, yb + 5 + (k >> 1) * 8, 8, 8, rb[k]);
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		int hot = hitAt (mx, my);
		if (hot != m_hot) { m_hot = hot; invalidate (true); }
		bool in = mx >= 0 && my >= 0 && mx < width && my < height;
		if (bl && !pressed) { pressed = true; if (!in) close (0); }
		else if (!bl && pressed)
		{
			pressed = false;
			if (hot == -1) close (1); else if (hot == -2) close (2); else if (hot >= 0) close (hot + 3);
		}
		return true;
	}
	bool onKey (long k) override { if (k == 27) close (0); return true; }
private:
	const unsigned *m_cols; int m_n, m_per; const char *m_auto; int m_hot;
	int hitAt (int mx, int my)
	{
		if (mx < PAD - 2 || mx >= width - PAD + 2) return -3;
		if (my >= PAD && my < PAD + 26) return -1;
		if (my >= height - PAD - 26 && my < height - PAD) return -2;
		int y0 = PAD + 26 + 6;
		if (my < y0) return -3;
		int c = (mx - PAD) / (CS + 4), r = (my - y0) / (CS + 4);
		if (c < 0 || c >= m_per) return -3;
		int i = r * m_per + c;
		return i < m_n ? i : -3;
	}
};
unsigned ColorPopup::m_last = 0x3366CC;

// The text colours: ten hues and their tints / shades (as a word processor's), the highlights.
static unsigned g_textCols[60], g_hiliteCols[15] = {
	0xFFFF00, 0x00FF00, 0x00FFFF, 0xFF00FF, 0x3366FF, 0xFF0000, 0x000080, 0x008080,
	0x008000, 0x800080, 0x800000, 0x808000, 0x808080, 0xC0C0C0, 0x000000 };
static void make_palettes ()
{
	static const unsigned base[10] = { 0xFFFFFF, 0x000000, 0xE7E6E6, 0x44546A, 0x4472C4, 0xED7D31, 0xA5A5A5, 0xFFC000, 0x5B9BD5, 0x70AD47 };
	static const unsigned std[10] = { 0xC00000, 0xFF0000, 0xFFC000, 0xFFFF00, 0x92D050, 0x00B050, 0x00B0F0, 0x0070C0, 0x002060, 0x7030A0 };
	for (int c = 0; c < 10; c++)
	{
		g_textCols[c] = base[c];
		unsigned b = base[c];
		bool dark = c == 1;
		g_textCols[10 + c] = c == 0 ? 0xF2F2F2 : dark ? 0x7F7F7F : uk_mix (b, 0xFFFFFF, 205);
		g_textCols[20 + c] = c == 0 ? 0xD9D9D9 : dark ? 0x595959 : uk_mix (b, 0xFFFFFF, 154);
		g_textCols[30 + c] = c == 0 ? 0xBFBFBF : dark ? 0x3F3F3F : uk_mix (b, 0xFFFFFF, 102);
		g_textCols[40 + c] = c == 0 ? 0xA6A6A6 : dark ? 0x262626 : uk_mix (b, 0x000000, 64);
		g_textCols[50 + c] = std[c];
	}
}

// ---- the ruler -------------------------------------------------------------------------------------------
// The page's width at the view's zoom and place: the margins grey, the paragraph's box white (the
// text's, or a table's cell's), a centimetre's numbers from its left edge; the paragraph's indents:
// the first line's (a triangle on top), the hanging one's (below; its little box moves both), the
// right one's; in a table, its columns' edges. Dragged, they change the selected paragraphs' indents
// (one edit), a column's width; the margins' edges drag the page's margins.
enum { RM_NONE, RM_FIRST, RM_HANG, RM_LEFT, RM_RIGHT, RM_MLEFT, RM_MRIGHT, RM_COL };
static bool g_inches;				// the ruler's unit (else centimetres)

class Ruler : public Widget
{
public:
	PageView *view;
	void (*onDone) ();
	Ruler (int l, int t, int w, PageView *v) : Widget (l, t, w, RULER_H), view (v), onDone (0), m_drag (RM_NONE), m_hot (RM_NONE), m_col (0), m_colX (0)
	{ tip = "Drag the markers: the first line's indent (top), the hanging and left indents (bottom), the right indent; a table's column edges; the grey edges: the margins"; }
	// The paragraph at the caret's box on the ruler, its table (0: none).
	const Para *box (int *bl, int *br, const Table **t)
	{
		view->relayout ();
		view->placeStory ();
		const Para *q = L.d->p[sel_a ().p];
		int ox = view->originX ();
		*bl = ox + (q->x64 >> 6); *br = *bl + (q->w64 >> 6);
		*t = L.d->cur == SY_BODY ? para_table (*L.d, q) : 0;
		return q;
	}
	int colEdge (const Table *t, int c) { return view->originX () + ((t->x64 + t->colX64[c]) >> 6); }
	void onDraw () override
	{
		canvas.fillRect (0, 0, width, height, C_BG);
		int bl, br; const Table *t;
		const Para *q = box (&bl, &br, &t);
		int ox = view->originX ();
		int x0 = ox, x1 = ox + L.pageW;
		int tl = ox + (L.ml64 >> 6), tr = ox + L.pageW - (L.mr64 >> 6);
		int y0 = 5, h = height - 10;
		unsigned band = uk_mix (C_BG, 0xFFFFFF, 90), white = 0xFFFFFF, ink = uk_mix (0xFFFFFF, 0x000000, 170);
		clipFill (x0, y0, x1 - x0, h, band);
		clipFill (t ? bl : tl, y0, (t ? br - bl : tr - tl), h, white);
		clipFill (x0, y0, x1 - x0, 1, uk_tone (C_BG, 90));
		clipFill (x0, y0 + h - 1, x1 - x0, 1, uk_tone (C_BG, 110));
		// the ticks: from the box's left edge both ways (the numbers every 1, 2, 5 or 10 units: apart
		// enough at the zoom; the small ticks while they are 4 px apart)
		int unitTw = g_inches ? 1440 : 567, sub = g_inches ? 8 : 4;
		int unitPx = tw2px64 (unitTw) >> 6;
		int every = unitPx >= 26 ? 1 : unitPx >= 13 ? 2 : unitPx >= 6 ? 5 : 10;
		bool subs = unitPx / sub >= 4;
		fnt::Font *nf = fnt::get (fnt::find ("DejaVu Sans"), 0, 9 * 64);
		for (int dir = -1; dir <= 1; dir += 2)
			for (int k = dir < 0 ? 1 : 0; ; k++)
			{
				int tw = dir * k * unitTw / sub;
				int x = bl + (tw2px64 (tw) >> 6);
				if (x < x0 || x > x1) break;
				if (x < 0 || x >= width) continue;
				int m = k % sub, u = k / sub;
				if (m == 0 && k && u % every == 0)
				{
					char b[6]; int n = 0, v = u; char tt[6]; int j = 0;
					while (v) { tt[j++] = (char) ('0' + v % 10); v /= 10; }
					while (j) b[n++] = tt[--j];
					b[n] = 0;
					if (nf) { int w = fnt::str_w (nf, b); fnt::draw_str (canvas, nf, x * 64 - w / 2, y0 + h / 2 + 4, b, ink); }
				}
				else if (k && (m == 0 || subs))
				{
					int th = m == 0 ? 5 : m == sub / 2 ? 5 : 2;
					canvas.fillRect (x, y0 + h / 2 - th / 2, 1, th, uk_mix (0xFFFFFF, 0x000000, 120));
				}
			}
		// a table's column edges
		if (t)
			for (int c = 0; c <= t->ncols; c++)
			{
				int x = colEdge (t, c);
				bool hot = c > 0 && ((m_hot == RM_COL && m_col == c - 1) || (m_drag == RM_COL && m_col == c - 1));
				if (m_drag == RM_COL && m_col == c - 1) x = m_colX;
				clipFill (x - 3, y0 + 1, 7, h - 2, hot ? C_ACCENT : uk_mix (C_BG, 0x000000, 60));
				clipFill (x - 1, y0 + 3, 3, h - 6, hot ? 0xFFFFFF : uk_mix (C_BG, 0xFFFFFF, 120));
			}
		// the markers (the paragraph at the caret)
		int lx = bl + (tw2px64 (q->pf.left) >> 6), fx = bl + (tw2px64 (q->pf.left + q->pf.first) >> 6);
		int rx = br - (tw2px64 (q->pf.right) >> 6);
		unsigned mk = uk_mix (C_BG, 0x000000, 110), mkHot = C_ACCENT;
		triangle (fx, y0 - 1, true, m_hot == RM_FIRST || m_drag == RM_FIRST ? mkHot : mk);
		triangle (lx, y0 + h - 7, false, m_hot == RM_HANG || m_drag == RM_HANG ? mkHot : mk);
		unsigned lb = m_hot == RM_LEFT || m_drag == RM_LEFT ? mkHot : mk;
		canvas.fillRect (lx - 4, y0 + h, 9, 4, lb);
		triangle (rx, y0 + h - 7, false, m_hot == RM_RIGHT || m_drag == RM_RIGHT ? mkHot : mk);
		if (m_drag == RM_MLEFT || m_drag == RM_MRIGHT || m_hot == RM_MLEFT || m_hot == RM_MRIGHT)
		{
			int mx = m_hot == RM_MLEFT || m_drag == RM_MLEFT ? tl : tr;
			canvas.fillRect (mx - 1, y0, 2, h, C_ACCENT);
		}
	}
	bool onMouse (int mx, int my, int bl0, int, int, int) override
	{
		if (mx < 0 && m_drag == RM_NONE) { if (m_hot) { m_hot = RM_NONE; invalidate (true); } return false; }
		int bl, br; const Table *t;
		const Para *q = box (&bl, &br, &t);
		int ox = view->originX ();
		int tl = ox + (L.ml64 >> 6), tr = ox + L.pageW - (L.mr64 >> 6);
		if (m_drag == RM_NONE)
		{
			int lx = bl + (tw2px64 (q->pf.left) >> 6), fx = bl + (tw2px64 (q->pf.left + q->pf.first) >> 6), rx = br - (tw2px64 (q->pf.right) >> 6);
			int hot = RM_NONE;
			if (my < RULER_H / 2 && mx >= fx - 5 && mx <= fx + 5) hot = RM_FIRST;
			else if (my >= RULER_H - 6 && mx >= lx - 5 && mx <= lx + 5) hot = RM_LEFT;
			else if (my >= RULER_H / 2 && mx >= lx - 5 && mx <= lx + 5) hot = RM_HANG;
			else if (my >= RULER_H / 2 && mx >= rx - 5 && mx <= rx + 5) hot = RM_RIGHT;
			if (!hot && t)
				for (int c = 1; c <= t->ncols; c++) { int x = colEdge (t, c); if (mx >= x - 3 && mx <= x + 3) { hot = RM_COL; m_col = c - 1; break; } }
			if (!hot && !t && mx >= tl - 3 && mx <= tl + 3) hot = RM_MLEFT;
			else if (!hot && !t && mx >= tr - 3 && mx <= tr + 3) hot = RM_MRIGHT;
			if (hot != m_hot) { m_hot = hot; invalidate (true); }
			if (bl0 && hot != RM_NONE)
			{
				m_drag = hot; catchOutside = true; m_colX = mx;
				Pos a = sel_a (), b = sel_b ();
				if (hot <= RM_RIGHT) ed_begin (a.p, b.p - a.p + 1, ED_OTHER);
			}
			return mx >= 0;
		}
		// dragging: the new place, in twips from the box's left edge (snapped to a 16th of the unit)
		int snap = g_inches ? 90 : 71;
		if (m_drag == RM_COL)
		{
			m_colX = mx;
			invalidate (true);
			if (!bl0)
			{
				int xTw = px642tw ((mx - ox) * 64 - t->x64);
				xTw = (xTw + snap / 2) / snap * snap;
				m_drag = RM_NONE; catchOutside = false;
				ed_table_col_edge (sel_a ().p, m_col, xTw);
				view->invalidate (true);
				if (onDone) onDone ();
			}
			return true;
		}
		int tw = px642tw ((mx - bl) << 6);
		tw = (tw + (tw >= 0 ? snap / 2 : -snap / 2)) / snap * snap;
		Pos a = sel_a (), b = sel_b ();
		int textTw = px642tw (q->w64);
		for (int p = a.p; p <= b.p && m_drag <= RM_RIGHT; p++)
		{
			Para *pq = L.d->p[p];
			int first = pq->pf.left + pq->pf.first;		// (the first line's absolute place)
			switch (m_drag)
			{
			case RM_FIRST: pq->pf.first = (short) wclamp (tw - pq->pf.left, -pq->pf.left, textTw - pq->pf.left - pq->pf.right - 200); break;
			case RM_HANG: { int l = wclamp (tw, 0, textTw - pq->pf.right - 300); pq->pf.left = (short) l; pq->pf.first = (short) (first - l); break; }
			case RM_LEFT: { int l = wclamp (tw, wmax (0, -pq->pf.first), textTw - pq->pf.right - 300); pq->pf.left = (short) l; break; }
			case RM_RIGHT: pq->pf.right = (short) wclamp (textTw - tw, 0, textTw - pq->pf.left - 300); break;
			}
			pq->dirty = true;
		}
		if (m_drag == RM_MLEFT || m_drag == RM_MRIGHT)
		{
			PageSetup &pw = L.d->page;
			int abs = px642tw ((mx - ox) << 6);
			abs = (abs + snap / 2) / snap * snap;
			if (m_drag == RM_MLEFT) pw.left = wclamp (abs, 0, pw.w - pw.right - 1440);
			else pw.right = wclamp (pw.w - abs, 0, pw.w - pw.left - 1440);
		}
		g_relayout = true;
		view->invalidate (true);
		invalidate (true);
		if (!bl0)
		{
			if (m_drag <= RM_RIGHT) { doc_end_edit (*L.d, b.p - a.p + 1, g_caret, g_anchor); }
			else L.d->changes++;
			m_drag = RM_NONE; catchOutside = false;
			changed ();
			if (onDone) onDone ();
		}
		return true;
	}
private:
	int m_drag, m_hot, m_col, m_colX;
	void clipFill (int x, int y, int w, int h, unsigned c)
	{
		int x1 = wmin (x + w, width); x = wmax (x, 0);
		if (x1 > x) canvas.fillRect (x, y, x1 - x, h, c);
	}
	void triangle (int x, int y, bool down, unsigned c)
	{
		VPath p;
		int pts[6];
		if (down) { pts[0] = V (x) - 72; pts[1] = V (y); pts[2] = V (x) + 88; pts[3] = V (y); pts[4] = V (x) + 8; pts[5] = V (y + 6); }
		else { pts[0] = V (x) + 8; pts[1] = V (y); pts[2] = V (x) + 88; pts[3] = V (y + 6); pts[4] = V (x) - 72; pts[5] = V (y + 6); }
		p.poly (pts, 3);
		p.fill (canvas, c);
		p.clear (); p.polyline (pts, 3, 12, true); p.fill (canvas, uk_mix (c, 0, 90));
	}
};

// ---- the status bar ---------------------------------------------------------------------------------------
class StatusBar : public Widget
{
public:
	char left[160], mid[80];
	void (*zoomBy) (int dir);
	StatusBar (int l, int t, int w) : Widget (l, t, w, STATUS_H), zoomBy (0), m_hot (0) { left[0] = mid[0] = 0; }
	void set (const char *l, const char *m)
	{
		if (!same (left, l) || !same (mid, m)) { scpy (left, l, sizeof left); scpy (mid, m, sizeof mid); invalidate (true); }
	}
	void onDraw () override
	{
		canvas.fillRect (0, 0, width, height, C_BG);
		uk_etch_h (canvas, 0, 0, width, C_BG);
		unsigned ink = uk_ink_for (C_BG), dim = uk_mix (C_BG, ink, 170);
		uk_text_l (canvas, 10, 1, height - 1, left, dim);
		uk_text_l (canvas, width / 2 - uk_text_w (mid) / 2, 1, height - 1, mid, dim);
		// the zoom: - 100% +
		char z[8]; int n = 0, v = L.zoom; char t[6]; int j = 0;
		while (v) { t[j++] = (char) ('0' + v % 10); v /= 10; }
		while (j) z[n++] = t[--j];
		z[n++] = '%'; z[n] = 0;
		int zw = 52, x = width - 12 - 18 - zw - 18;
		zoomButton (x, false); uk_text_c (canvas, x + 18, 1, zw, height - 1, z, dim); zoomButton (x + 18 + zw, true);
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		int zw = 52, x = width - 12 - 18 - zw - 18;
		int hot = my >= 0 && my < height ? (mx >= x && mx < x + 18 ? 1 : mx >= x + 18 + zw && mx < x + 36 + zw ? 2 : 0) : 0;
		if (hot != m_hot) { m_hot = hot; invalidate (true); }
		if (bl && !pressed && hot) { pressed = true; if (zoomBy) zoomBy (hot == 1 ? -1 : 1); }
		if (!bl) pressed = false;
		return mx >= 0;
	}
private:
	int m_hot;
	static bool same (const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
	void zoomButton (int x, bool plus)
	{
		unsigned ink = uk_ink_for (C_BG);
		bool hot = m_hot == (plus ? 2 : 1);
		if (hot) uk_rbox (canvas, x, 3, 18, height - 6, 4, uk_tone (C_BG, 150), uk_tone (C_BG, 138));
		uk_glyph (canvas, plus ? WKG_PLUS : WKG_MINUS, x + 9, height / 2, 8, ink);
	}
};

} // namespace wr

#endif
