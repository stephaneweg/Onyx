//
// ui_base.h -- the spreadsheet's controls around the grid, in Letters' look: the toolbars' buttons (an
// icon; toggled: lit; split: an arrow part that drops a palette), the toolbars, the pick boxes (a value
// shown -- a font's name in that font -- and a list dropping from it), the size box, the colour
// palettes; the icons Letters does not have (drawn from their geometry: uikit/vpaint.h, 20 x 20 px).
//
#ifndef _sheet_ui_base_h
#define _sheet_ui_base_h

#include "Apps/letters/icons.h"

namespace ss {

using namespace uikit;

enum { TB_H = 34, BTN = 28, SPLIT_W = 13 };

// A floating panel drawn inside a widget (a tip over the grid): uk_popup's look, its corners blended over
// what lies below (no magenta key: the widget is not blitted transparent), a soft shadow.
static void panel_in (Canvas &cv, int x, int y, int w, int h, int r, unsigned face)
{
	uk_rbox (cv, x + 1, y + 2, w, h, r, 0, 0, 36);
	uk_rbox (cv, x, y, w, h, r, uk_tone (face, 140), face);
	uk_rline (cv, x, y, w, h, r, uk_tone (C_FACE, 70), 230);
}

// ---- the icons -------------------------------------------------------------------------------------------
// Below 100: Letters' (wr::IC_*); from 100: the spreadsheet's own.
enum { SI_WRAP = 100, SI_MERGE, SI_CURRENCY, SI_PERCENT, SI_THOUSANDS, SI_DECPLUS, SI_DECMINUS, SI_BORDERS,
       SI_SORTASC, SI_SORTDESC, SI_CHART, SI_FUNC, SI_SUM, SI_FREEZE, SI_INSROW, SI_INSCOL, SI_DELROW, SI_DELCOL,
       SI_ACCEPT, SI_CANCEL, SI_VTOP, SI_VMID, SI_VBOT, SI_CLEAR, SI_FILL, SI_FILTER, SI_SHEETADD };

// From 200: an app's own icons (Slides: its drawer set here).
static void (*g_appIcon) (Canvas &cv, int k, int x, int y, unsigned ink, bool off);
static void sheet_icon (Canvas &cv, int k, int x, int y, unsigned ink, bool off)
{
	if (k < 100) { wr::draw_icon (cv, k, x, y, ink, off); return; }
	if (k >= 200) { if (g_appIcon) g_appIcon (cv, k, x, y, ink, off); return; }
	if (wr::g_icFam < 0) { wr::g_icFam = fnt::find ("DejaVu Sans"); wr::g_icFamSerif = fnt::find ("DejaVu Serif"); if (wr::g_icFamSerif < 0) wr::g_icFamSerif = wr::g_icFam; }
	unsigned dim = uk_mix (ink, C_BG, 150);
	if (off) ink = dim;
	unsigned acc = off ? dim : C_ACCENT;
	int A = off ? 110 : 255;
	VPath p;
	int X = V (x), Y = V (y);
	auto grid = [&] (int gx, int gy, int w, int h, int nc, int nr) {	// a little table
		p.clear (); p.rect (X + V (gx), Y + V (gy), V (w), V (h)); p.fill (cv, 0xFFFFFF, A);
		p.clear ();
		int fr[8] = { X + V (gx), Y + V (gy), X + V (gx + w), Y + V (gy), X + V (gx + w), Y + V (gy + h), X + V (gx), Y + V (gy + h) };
		p.polyline (fr, 4, 18, true);
		for (int i = 1; i < nc; i++) p.rect (X + V (gx + w * i / nc) - 8, Y + V (gy), 16, V (h));
		for (int i = 1; i < nr; i++) p.rect (X + V (gx), Y + V (gy + h * i / nr) - 8, V (w), 16);
		p.fill (cv, uk_mix (ink, 0xFFFFFF, 90));
	};
	switch (k)
	{
	case SI_WRAP:
	{
		p.rect (X + V (2), Y + V (3), V (15), 26); p.rect (X + V (2), Y + V (15), V (7), 26); p.fill (cv, ink);
		p.clear (); p.arc (X + V (13), Y + V (11), V (4), 90, -90, 26); p.fill (cv, acc);
		p.clear (); p.line (X + V (13), Y + V (15), X + V (11), Y + V (15), 26); p.arrowHead (X + V (10), Y + V (15), 180, V (4), V (3)); p.fill (cv, acc);
		p.clear (); p.rect (X + V (2), Y + V (9), V (11), 26); p.fill (cv, ink);
		break;
	}
	case SI_MERGE:
		grid (1, 3, 18, 14, 2, 1);
		p.clear (); p.line (X + V (3), Y + V (10), X + V (8), Y + V (10), 24); p.arrowHead (X + V (9), Y + V (10), 0, V (4), V (3));
		p.line (X + V (17), Y + V (10), X + V (12), Y + V (10), 24); p.arrowHead (X + V (11), Y + V (10), 180, V (4), V (3));
		p.fill (cv, acc);
		break;
	case SI_CURRENCY:
		p.rrect (X + V (1), Y + V (5), V (18), V (11), V (2)); p.fill (cv, 0x7CB26A, A);
		p.clear (); p.circle (X + V (10), Y + V (10) + 8, V (3)); p.fill (cv, 0xE8F3E3, A);
		wr::icon_letter (cv, x + 10, y + 14, 0x20AC, wr::g_icFam, fnt::BOLD, 7, 0x2E6B2A);
		break;
	case SI_PERCENT: wr::icon_letter (cv, x + 10, y + 16, '%', wr::g_icFam, fnt::BOLD, 15, ink); break;
	case SI_THOUSANDS:
		wr::icon_letter (cv, x + 5, y + 15, '0', wr::g_icFam, fnt::BOLD, 10, ink);
		wr::icon_letter (cv, x + 9, y + 17, ',', wr::g_icFam, fnt::BOLD, 12, acc);
		wr::icon_letter (cv, x + 15, y + 15, '0', wr::g_icFam, fnt::BOLD, 10, ink);
		break;
	case SI_DECPLUS: case SI_DECMINUS:
	{
		bool plus = k == SI_DECPLUS;
		wr::icon_letter (cv, x + 7, y + 10, '.', wr::g_icFam, fnt::BOLD, 10, ink);
		wr::icon_letter (cv, x + 11, y + 9, '0', wr::g_icFam, fnt::BOLD, 8, ink);
		if (plus) wr::icon_letter (cv, x + 16, y + 9, '0', wr::g_icFam, fnt::BOLD, 8, ink);
		p.clear ();
		if (plus) { p.line (X + V (14), Y + V (15), X + V (5), Y + V (15), 22); p.arrowHead (X + V (3), Y + V (15), 180, V (4), V (3)); }
		else { p.line (X + V (5), Y + V (15), X + V (14), Y + V (15), 22); p.arrowHead (X + V (16), Y + V (15), 0, V (4), V (3)); }
		p.fill (cv, acc);
		break;
	}
	case SI_BORDERS:
		grid (2, 2, 16, 16, 2, 2);
		p.clear (); p.rect (X + V (2), Y + V (16), V (16), V (2) + 4); p.fill (cv, ink);
		break;
	case SI_SORTASC: case SI_SORTDESC:
	{
		bool asc = k == SI_SORTASC;
		wr::icon_letter (cv, x + 5, y + 8, asc ? 'A' : 'Z', wr::g_icFam, fnt::BOLD, 8, ink);
		wr::icon_letter (cv, x + 5, y + 18, asc ? 'Z' : 'A', wr::g_icFam, fnt::BOLD, 8, ink);
		p.line (X + V (14), Y + V (2), X + V (14), Y + V (14), 26); p.arrowHead (X + V (14), Y + V (18), -90, V (5), V (3));
		p.fill (cv, acc);
		break;
	}
	case SI_CHART:
		p.rect (X + V (2), Y + V (18), V (17), 20); p.rect (X + V (2), Y + V (2), 20, V (17)); p.fill (cv, ink);
		p.clear (); p.rect (X + V (5), Y + V (10), V (3), V (8)); p.fill (cv, 0x4472C4, A);
		p.clear (); p.rect (X + V (10), Y + V (5), V (3), V (13)); p.fill (cv, 0xED7D31, A);
		p.clear (); p.rect (X + V (15), Y + V (8), V (3), V (10)); p.fill (cv, 0x70AD47, A);
		break;
	case SI_FUNC:
		wr::icon_letter (cv, x + 7, y + 15, 'f', wr::g_icFamSerif, fnt::ITALIC | fnt::BOLD, 15, ink);
		wr::icon_letter (cv, x + 14, y + 16, 'x', wr::g_icFamSerif, fnt::ITALIC, 11, ink);
		break;
	case SI_SUM: wr::icon_letter (cv, x + 10, y + 16, 0x3A3, wr::g_icFam, fnt::BOLD, 15, ink); break;
	case SI_FREEZE:
		grid (2, 2, 16, 16, 3, 3);
		p.clear (); p.rect (X + V (7) + 4, Y + V (2), 24, V (16)); p.rect (X + V (2), Y + V (7) + 4, V (16), 24); p.fill (cv, acc);
		break;
	case SI_INSROW: case SI_DELROW: case SI_INSCOL: case SI_DELCOL:
	{
		bool row = k == SI_INSROW || k == SI_DELROW, ins = k == SI_INSROW || k == SI_INSCOL;
		if (row) grid (1, 6, 12, 12, 2, 2); else grid (6, 1, 12, 12, 2, 2);
		p.clear ();
		if (row) p.rect (X + V (1), Y + V (6), V (12), V (4)); else p.rect (X + V (6), Y + V (1), V (4), V (12));
		p.fill (cv, ins ? 0x70AD47 : 0xE06666, A);
		p.clear ();
		int cx = row ? 16 : 16, cy = row ? 4 : 16;
		p.rect (X + V (cx - 3), Y + V (cy) - 12, V (6), 24);
		if (ins) p.rect (X + V (cx) - 12, Y + V (cy - 3), 24, V (6));
		p.fill (cv, ins ? 0x2E7D32 : 0xC62828, A);
		break;
	}
	case SI_ACCEPT:
	{
		int pts[6] = { X + V (3), Y + V (10), X + V (8), Y + V (15), X + V (17), Y + V (4) };
		p.polyline (pts, 3, 44); p.fill (cv, off ? dim : 0x2E8B3D);
		break;
	}
	case SI_CANCEL:
		p.line (X + V (4), Y + V (4), X + V (16), Y + V (16), 40); p.line (X + V (16), Y + V (4), X + V (4), Y + V (16), 40);
		p.fill (cv, off ? dim : 0xC0392B);
		break;
	case SI_VTOP: case SI_VMID: case SI_VBOT:
	{
		int ly = k == SI_VTOP ? 2 : k == SI_VMID ? 9 : 16;
		p.rect (X + V (2), Y + V (ly), V (16), 24); p.fill (cv, acc);
		int ty = k == SI_VTOP ? 5 : k == SI_VMID ? 4 : 3;
		p.clear (); p.rect (X + V (6), Y + V (ty), V (8), 22); p.rect (X + V (6), Y + V (ty + 3), V (8), 22);
		if (k != SI_VMID) p.rect (X + V (6), Y + V (ty + 6), V (8), 22); else p.rect (X + V (6), Y + V (ty + 9), V (8), 22);
		p.fill (cv, ink);
		break;
	}
	case SI_CLEAR:
	{
		int body[8] = { X + V (7), Y + V (4), X + V (17), Y + V (10), X + V (12), Y + V (17), X + V (2), Y + V (11) };
		p.poly (body, 4); p.fill (cv, 0xF4A3B5, A);
		int tip[8] = { X + V (7), Y + V (14), X + V (12), Y + V (17), X + V (2), Y + V (11), X + V (4), Y + V (10) };
		p.clear (); p.poly (tip, 4); p.fill (cv, 0x9BB7D4, A);
		p.clear (); p.polyline (body, 4, 18, true); p.fill (cv, ink);
		break;
	}
	case SI_FILL:
	{
		int b[8] = { X + V (9), Y + V (2), X + V (16), Y + V (9), X + V (9), Y + V (16), X + V (2), Y + V (9) };
		p.poly (b, 4); p.fill (cv, 0xFFFFFF, A);
		p.clear (); p.polyline (b, 4, 20, true); p.fill (cv, ink);
		p.clear (); p.circle (X + V (17), Y + V (13), V (2)); p.fill (cv, acc);
		break;
	}
	case SI_FILTER:
	{
		int f[12] = { X + V (2), Y + V (3), X + V (18), Y + V (3), X + V (12), Y + V (10), X + V (12), Y + V (17), X + V (8), Y + V (15), X + V (8), Y + V (10) };
		p.poly (f, 6); p.fill (cv, uk_mix (acc, 0xFFFFFF, 100), A);
		p.clear (); p.polyline (f, 6, 18, true); p.fill (cv, ink);
		break;
	}
	case SI_SHEETADD:
		p.rect (X + V (9), Y + V (3), V (2), V (14)); p.rect (X + V (3), Y + V (9), V (14), V (2)); p.fill (cv, ink);
		break;
	}
}

// ---- a toolbar button -------------------------------------------------------------------------------------
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
		sheet_icon (canvas, icon, (bw - 20) / 2 + d, (height - 20) / 2 + d - (bar != 0xFF000000u ? 2 : 0), ink, disabled);
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
	// Where a palette drops from (root coordinates).
	void below (int *x, int *y)
	{
		int ax = 0, ay = 0;
		for (Widget *w = this; w && w->parent; w = w->parent) { ax += w->left; ay += w->top; }
		*x = ax; *y = ay + height + 2;
	}
private:
	int m_part, m_down;
};

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
	void sep () { if (m_nsep < 24) m_sepX[m_nsep++] = m_x + 5; m_x += 11; }
	int end () const { return m_x; }
	void onDraw () override
	{
		canvas.fillRect (0, 0, width, height, C_BG);
		for (int i = 0; i < m_nsep; i++) uk_etch_v (canvas, m_sepX[i], 7, height - 14, C_BG);
	}
private:
	int m_x, m_nsep, m_sepX[24];
};

// ---- a list dropping over the window ------------------------------------------------------------------------
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
			if (top + height > r->height) top = imax (0, y - height - 30);
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
		for (int r = 0; r < m_rows; r++)
		{
			int i = m_top + r, y = 4 + r * m_rowH;
			if (i >= m_n) break;
			bool hot = i == m_hot;
			if (hot) uk_hilite (canvas, 4, y, width - 8 - sbw, m_rowH, 5, true);
			else if (i == m_sel) uk_rline (canvas, 4, y, width - 8 - sbw, m_rowH, 5, uk_mix (C_FIELD, C_ACCENT, 160));
			m_draw (canvas, i, 10, y, width - 20 - sbw, m_rowH, hot ? C_SEL_TEXT : C_FIELD_TEXT);
		}
		if (sbw) { UkThumb t = uk_thumb (m_n, m_rows, m_top, height - 8); uk_draw_vscroll (canvas, width - UK_SBW - 4, 4, UK_SBW, height - 8, t, C_FIELD); }
	}
	bool onMouse (int mx, int my, int bl, int, int, int wheel) override
	{
		bool in = mx >= 0 && my >= 0 && mx < width && my < height;
		if (wheel && in) { m_top = iclamp (m_top - wheel, 0, imax (0, m_n - m_rows)); invalidate (true); return true; }
		int hot = in && my >= 4 ? m_top + (my - 4) / m_rowH : -1;
		if (hot >= m_n || (m_n > m_rows && mx >= width - UK_SBW - 6)) hot = -1;
		if (hot != m_hot && hot >= 0) { m_hot = hot; invalidate (true); }
		if (m_n > m_rows && in && bl && mx >= width - UK_SBW - 6)
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
		m_hot = iclamp (m_hot + d, 0, m_n - 1);
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
	void below (int *x, int *y)
	{
		int ax = 0, ay = 0;
		for (Widget *w = this; w && w->parent; w = w->parent) { ax += w->left - w->parent->scrollX; ay += w->top - w->parent->scrollY; }
		*x = ax; *y = ay + height + 2;
	}
private:
	bool m_hot;
};

// ---- the colour palettes -------------------------------------------------------------------------------------
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
		if (r) { if (left + width > r->width) left = r->width - width; if (top + height > r->height) top = imax (0, r->height - height); }
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

// The colours: ten hues and their tints / shades (as an office suite's), the standard ones.
static unsigned g_cols[60];
static void make_palettes ()
{
	static const unsigned base[10] = { 0xFFFFFF, 0x000000, 0xE7E6E6, 0x44546A, 0x4472C4, 0xED7D31, 0xA5A5A5, 0xFFC000, 0x5B9BD5, 0x70AD47 };
	static const unsigned std[10] = { 0xC00000, 0xFF0000, 0xFFC000, 0xFFFF00, 0x92D050, 0x00B050, 0x00B0F0, 0x0070C0, 0x002060, 0x7030A0 };
	for (int c = 0; c < 10; c++)
	{
		unsigned b = base[c];
		bool dark = c == 1;
		g_cols[c] = b;
		g_cols[10 + c] = c == 0 ? 0xF2F2F2 : dark ? 0x7F7F7F : uk_mix (b, 0xFFFFFF, 205);
		g_cols[20 + c] = c == 0 ? 0xD9D9D9 : dark ? 0x595959 : uk_mix (b, 0xFFFFFF, 154);
		g_cols[30 + c] = c == 0 ? 0xBFBFBF : dark ? 0x3F3F3F : uk_mix (b, 0xFFFFFF, 102);
		g_cols[40 + c] = c == 0 ? 0xA6A6A6 : dark ? 0x262626 : uk_mix (b, 0x000000, 64);
		g_cols[50 + c] = std[c];
	}
}

} // namespace ss

#endif
