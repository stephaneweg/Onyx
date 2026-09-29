//
// bars.h -- around the grid: the formula bar (the Name Box -- the cell or range chosen; a reference typed
// there goes to it --, the function wizard and AutoSum buttons, while a cell is edited Cancel / Accept, and
// the input line: the cell's content as it is typed, a formula's references coloured, edited there as in
// the cell), the sheets' tabs (a click shows one, a double click renames it, "+" adds one; the arrows
// scroll them), the status bar (the sheet, the mode, the sum / average / count of the selection, the zoom).
//
#ifndef _sheet_bars_h
#define _sheet_bars_h

#include "grid.h"

namespace ss {

enum { FBAR_H = 32, TABS_H = 28, STATUS_H = 24 };

// ---- the input line ---------------------------------------------------------------------------------------
class InputLine : public Widget
{
public:
	GridView *g;
	void (*onFocusEdit) ();					// a click into it: the cell edited here
	InputLine (int l, int t, int w, int h, GridView *g_) : Widget (l, t, w, h), g (g_), onFocusEdit (0), m_scroll (0) { canFocus = true; }
	unsigned bgColor () override { return parent ? parent->bgColor () : C_BG; }
	const char *text (int *n)
	{
		if (g->ed.on) { *n = g->ed.t.n; return g->ed.t.b ? g->ed.t.b : ""; }
		Sheet *s = g->S ();
		m_show.clear ();
		cell_edit_text (*g->b, s, s->cells.get (s->curR, s->curC), m_show);
		*n = m_show.n; return m_show.b ? m_show.b : "";
	}
	void onDraw () override
	{
		canvas.clear (bgColor ());
		wk_sunken (canvas, 0, 0, width, height, 5, C_FIELD, hasFocus);
		fnt::Font *f = ui_font (13);
		int n; const char *t = text (&n);
		int lineEnd = 0; while (lineEnd < n && t[lineEnd] != '\n') lineEnd++;
		RefHi refs[64]; int nr = g->ed.on ? edit_refs (*g->b, g->ed, refs, 64) : 0;
		int caretX = g->ed.on ? u8_width (f, t, imin (g->ed.caret, lineEnd)) >> 6 : 0;
		int room = width - 16;
		if (caretX - m_scroll > room) m_scroll = caretX - room + 20;
		if (caretX < m_scroll) m_scroll = imax (0, caretX - 20);
		if (!g->ed.on) m_scroll = 0;
		int base = height / 2 + (f->ascent >> 7) + 1;
		int x64 = (8 - m_scroll) * 64;
		Rect clip = { 2, 3, height - 3, width - 4 };
		int i = 0, l; unsigned prev = 0;
		int s0 = g->ed.on ? g->ed.s0 () : 0, s1 = g->ed.on ? g->ed.s1 () : 0;
		while (i < lineEnd)
		{
			unsigned cp = u8_dec (t + i, n - i, &l);
			if (prev) x64 += fnt::kern (f, prev, cp);
			int adv = fnt::advance (f, cp);
			unsigned c = C_FIELD_TEXT;
			for (int k = 0; k < nr; k++) if (i >= refs[k].at && i < refs[k].at + refs[k].len) c = refs[k].col;
			if (i >= s0 && i < s1) { int X0 = imax (x64 >> 6, clip.c0), X1 = imin ((x64 + adv) >> 6, clip.c1); if (X1 > X0) canvas.fillRect (X0, 5, X1 - X0, height - 10, wk_mix (C_FIELD, C_ACCENT, 110)); }
			fnt::draw (canvas, f, x64, base, cp, c, clip.c0, clip.r0, clip.c1, clip.r1);
			x64 += adv; prev = cp; i += l;
		}
		if (lineEnd < n) fnt::draw (canvas, f, x64 + 4 * 64, base, 0x21B5, wk_mix (C_FIELD, C_FIELD_TEXT, 120), clip.c0, clip.r0, clip.c1, clip.r1);	// (more lines)
		if (g->ed.on && hasFocus) { int cx = 8 - m_scroll + caretX; if (cx >= 3 && cx < width - 3) canvas.fillRect (cx, 6, 1, height - 12, C_FIELD_TEXT); }
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		bool in = mx >= 0 && my >= 0 && mx < width && my < height;
		if (bl && in && !pressed)
		{
			pressed = true;
			setFocus ();
			if (!g->ed.on) { g->beginEdit (false, 0); g->ed.typed = false; }
			// the caret where it was clicked
			fnt::Font *f = ui_font (13);
			int n; const char *t = text (&n);
			int x = mx - 8 + m_scroll, i = 0, l, w = 0;
			while (i < n && t[i] != '\n') { unsigned cp = u8_dec (t + i, n - i, &l); int a = fnt::advance (f, cp) >> 6; if (w + a / 2 > x) break; w += a; i += l; }
			g->ed.caret = g->ed.anchor = i; g->ed.pointing = false;
			if (onFocusEdit) onFocusEdit ();
			invalidate (true);
			return true;
		}
		if (bl && pressed && g->ed.on)				// (a drag: the selection)
		{
			fnt::Font *f = ui_font (13);
			int n; const char *t = text (&n);
			int x = mx - 8 + m_scroll, i = 0, l, w = 0;
			while (i < n && t[i] != '\n') { unsigned cp = u8_dec (t + i, n - i, &l); int a = fnt::advance (f, cp) >> 6; if (w + a / 2 > x) break; w += a; i += l; }
			if (i != g->ed.caret) { g->ed.caret = i; invalidate (true); g->invalidate (true); }
			return true;
		}
		if (!bl) pressed = false;
		return in;
	}
	bool onKey (long k) override
	{
		if (!g->ed.on) return false;
		// the editing keys as in the cell, but the arrows move the caret here
		bool wasTyped = g->ed.typed;
		g->ed.typed = false;
		bool r = g->onKey (k);
		if (g->ed.on) g->ed.typed = wasTyped && false;
		invalidate (true);
		return r;
	}
private:
	int m_scroll;
	Buf m_show;
};

// The Name Box: the cell or range; a reference typed (or a sheet's "Sheet2!B4") goes there.
// A click into it selects its text (what is typed replaces it); Esc goes back to the grid.
class NameBox : public Textbox
{
public:
	void (*onGo) (const char *);
	void (*onLeave) ();
	NameBox (int w) : Textbox (0, 0, w, 26, "A1", enter), onGo (0), onLeave (0), m_all (false) { tip = "Name Box: the cell chosen; type a reference (B7, C2:F9, Sheet2!A1) and press Enter to go there"; }
	unsigned bgColor () override { return parent ? parent->bgColor () : C_BG; }
	void show (const char *s) { if (!hasFocus) { setText (s); m_all = false; } }
	void selectAll () { m_all = true; caret = (int) strlen (text); invalidate (true); }
	void onDraw () override
	{
		Textbox::onDraw ();
		if (m_all && hasFocus && text[0])			// (the text selected: tinted)
		{
			int fw = wk_fw (), fh = wk_fh (), n = imin ((int) strlen (text), (width - 12) / fw);
			int y0 = (height - fh) / 2;
			for (int y = y0; y < y0 + fh; y++)
				for (int x = 6; x < 6 + n * fw; x++) { unsigned &p = canvas.px[y * canvas.stride + x]; p = wk_mix (p, C_ACCENT, 90); }
		}
	}
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override
	{
		bool had = hasFocus;
		bool r = Textbox::onMouse (mx, my, bl, br, bm, wheel);
		if (bl && mx >= 0 && !had) selectAll ();
		else if (bl && mx >= 0) { m_all = false; invalidate (true); }
		return r;
	}
	bool onKey (long k) override
	{
		if (k == 27) { m_all = false; if (onLeave) onLeave (); return true; }
		if (m_all)
		{
			m_all = false;
			if ((k >= 32 && k <= 126) || (k >= 0xA0 && k <= 0xFF) || k == KEY_BACKSPACE || k == KEY_DEL) { text[0] = 0; caret = 0; if (k == KEY_BACKSPACE || k == KEY_DEL) { invalidate (true); return true; } }
		}
		return Textbox::onKey (k);
	}
private:
	bool m_all;
	static void enter (Widget &w) { NameBox &n = (NameBox &) w; n.m_all = false; if (n.onGo) n.onGo (n.text); }
};

class FormulaBar : public Widget
{
public:
	NameBox *name;
	ToolButton *fx, *sum, *cancel, *accept;
	InputLine *line;
	GridView *g;
	FormulaBar (int l, int t, int w, GridView *g_, void (*fxCb) (), void (*sumCb) (), void (*cancelCb) (), void (*acceptCb) ())
		: Widget (l, t, w, FBAR_H), g (g_)
	{
		name = new NameBox (108); name->left = 6; name->top = 3; addChild (name);
		fx = new ToolButton (SI_FUNC, "Insert Function...", fxCb); fx->left = 120; fx->top = 2; addChild (fx);
		sum = new ToolButton (SI_SUM, "AutoSum: the sum of the numbers above or at the left", sumCb); sum->left = 150; sum->top = 2; addChild (sum);
		cancel = new ToolButton (SI_CANCEL, "Cancel (Esc)", cancelCb); cancel->left = 180; cancel->top = 2; addChild (cancel);
		accept = new ToolButton (SI_ACCEPT, "Accept (Enter)", acceptCb); accept->left = 210; accept->top = 2; addChild (accept);
		line = new InputLine (242, 3, w - 248, 26, g); line->anchor = ANCHOR_LEFT | ANCHOR_RIGHT | ANCHOR_TOP; addChild (line);
	}
	void onDraw () override
	{
		canvas.fillRect (0, 0, width, height, C_BG);
		wk_etch_v (canvas, 116, 7, height - 14, C_BG);
	}
	void sync ()
	{
		cancel->setDisabled (!g->ed.on); accept->setDisabled (!g->ed.on);
		Sheet *s = g->S ();
		Rect r = g->sel ();
		char t[48];
		if (g->ed.on) cell_name (g->ed.r, g->ed.c, t);
		else if (r.r0 == r.r1 && r.c0 == r.c1) cell_name (s->curR, s->curC, t);
		else if (merge_at (s, r.r0, r.c0) >= 0 && s->merges[merge_at (s, r.r0, r.c0)].r1 == r.r1 && s->merges[merge_at (s, r.r0, r.c0)].c1 == r.c1) cell_name (r.r0, r.c0, t);
		else if (g->wholeCols () && !g->wholeRows ()) { char a[8], c2[8]; col_name (r.c0, a); col_name (r.c1, c2); snprintf (t, sizeof t, "%s:%s", a, c2); }
		else if (g->wholeRows () && !g->wholeCols ()) snprintf (t, sizeof t, "%d:%d", r.r0 + 1, r.r1 + 1);
		else if (g->wholeRows () && g->wholeCols ()) scpy (t, "A1:XFD1048576", sizeof t);
		else { char a[20], c2[20]; cell_name (r.r0, r.c0, a); cell_name (r.r1, r.c1, c2); snprintf (t, sizeof t, "%s:%s", a, c2); }
		name->show (t);
		line->invalidate (true);
	}
};

// ---- the sheets' tabs -------------------------------------------------------------------------------------
class SheetTabs : public Widget
{
public:
	Book *b;
	int first;						// the first tab shown
	void (*onPick) (int i);
	void (*onAdd) ();
	void (*onRename) (int i);
	void (*onMenu) (int i, int x, int y);
	SheetTabs (int l, int t, int w, Book *b_) : Widget (l, t, w, TABS_H), b (b_), first (0), onPick (0), onAdd (0), onRename (0), onMenu (0), m_hot (-9), m_last (0), m_lastI (-1) {}
	int tabW (int i) { fnt::Font *f = ui_font (12, i == b->active); return imax (60, (u8_width (f, b->sh[i]->name, (int) strlen (b->sh[i]->name)) >> 6) + 26); }
	enum { NAV_W = 84 };
	int hitAt (int mx, int my)
	{
		if (my < 0 || my >= height) return -9;
		if (mx < 28) return -1;					// (previous)
		if (mx < 56) return -2;					// (next)
		if (mx < NAV_W) return -3;				// (add)
		int x = NAV_W + 4;
		for (int i = first; i < b->ns; i++) { int w = tabW (i); if (mx >= x && mx < x + w) return i; x += w - 1; if (x > width) break; }
		return -9;
	}
	void onDraw () override
	{
		canvas.fillRect (0, 0, width, height, C_BG);
		canvas.fillRect (0, 0, width, 1, wk_tone (C_BG, 110));
		unsigned ink = C_TEXT;
		// the arrows and "+"
		for (int k = 0; k < 3; k++)
		{
			int x = k * 28 + 2;
			if (m_hot == -1 - k) wk_rbox (canvas, x, 3, 24, height - 6, 4, wk_tone (C_BG, 150), wk_tone (C_BG, 138));
			if (k < 2) wk_glyph (canvas, k == 0 ? WKG_CHEV_LEFT : WKG_CHEV_RIGHT, x + 12, height / 2, 8, ink);
			else sheet_icon (canvas, SI_SHEETADD, x + 2, height / 2 - 10, ink, false);
		}
		int x = NAV_W + 4;
		fnt::Font *f = ui_font (12), *fb = ui_font (12, true);
		for (int i = first; i < b->ns && x < width; i++)
		{
			int w = tabW (i);
			bool act = i == b->active, hot = i == m_hot;
			unsigned bg = act ? 0xFFFFFF : hot ? wk_tone (C_BG, 150) : wk_tone (C_BG, 132);
			VPath p;
			int pts[8] = { V (x), V (0), V (x + w), V (0), V (x + w - 5), V (height - 3), V (x + 5), V (height - 3) };
			p.poly (pts, 4); p.fill (canvas, bg);
			p.clear (); p.polyline (pts, 4, 16, true); p.fill (canvas, wk_tone (C_BG, 96));
			if (act) canvas.fillRect (x + 1, 0, w - 2, 2, 0xFFFFFF);
			if (b->sh[i]->tab != AUTO) canvas.fillRect (x + 8, height - 7, w - 16, 3, b->sh[i]->tab);
			text_at (canvas, act ? fb : f, x + w / 2, height / 2 + 4, b->sh[i]->name, act ? 0x202124 : ink, 1, Rect { 0, x + 4, height - 1, x + w - 5 });
			x += w - 1;
		}
	}
	bool onMouse (int mx, int my, int bl, int br, int, int wheel) override
	{
		int h = hitAt (mx, my);
		if (h != m_hot) { m_hot = h; invalidate (true); }
		if (wheel && h != -9) { first = iclamp (first - wheel, 0, b->ns - 1); invalidate (true); return true; }
		if (br && !m_r) { m_r = true; if (h >= 0 && onMenu) { if (onPick && h != b->active) onPick (h); onMenu (h, mx, my); } return true; }
		if (!br) m_r = false;
		if (bl && !pressed && h != -9)
		{
			pressed = true;
			unsigned now = kapi_get_ticks ();
			if (h == -1) { if (first > 0) first--; invalidate (true); }
			else if (h == -2) { if (first < b->ns - 1) first++; invalidate (true); }
			else if (h == -3) { if (onAdd) onAdd (); }
			else if (h >= 0)
			{
				bool dbl = h == m_lastI && now - m_last < 40;
				m_last = now; m_lastI = h;
				if (onPick && h != b->active) onPick (h);
				if (dbl && onRename) onRename (h);
			}
			return true;
		}
		if (!bl) pressed = false;
		return h != -9;
	}
	void showActive ()
	{
		if (b->active < first) first = b->active;
		int x = NAV_W + 4;
		for (int i = first; i <= b->active && i < b->ns; i++) x += tabW (i) - 1;
		while (x > width && first < b->active) { x -= tabW (first) - 1; first++; }
		invalidate (true);
	}
private:
	int m_hot; unsigned m_last; int m_lastI; bool m_r = false;
};

// ---- the status bar ---------------------------------------------------------------------------------------
class StatusBar : public Widget
{
public:
	char left[64], mid[160];
	int zoom;
	void (*onZoom) (int dir);				// -1, +1; 0: back to 100 %
	StatusBar (int l, int t, int w) : Widget (l, t, w, STATUS_H), zoom (100), onZoom (0) { left[0] = mid[0] = 0; }
	void set (const char *l, const char *m, int z)
	{
		if (strcmp (l, left) || strcmp (m, mid) || z != zoom) { scpy (left, l, sizeof left); scpy (mid, m, sizeof mid); zoom = z; invalidate (true); }
	}
	void onDraw () override
	{
		canvas.fillRect (0, 0, width, height, C_BG);
		canvas.fillRect (0, 0, width, 1, wk_tone (C_BG, 110));
		fnt::Font *f = ui_font (12);
		text_at (canvas, f, 10, height / 2 + 5, left, C_TEXT, 0, Rect { 0, 0, height - 1, width / 3 });
		text_at (canvas, f, width - 110, height / 2 + 5, mid, C_TEXT, 2, Rect { 0, width / 3, height - 1, width - 104 });
		char z[16]; snprintf (z, sizeof z, "%d%%", zoom);
		wk_etch_v (canvas, width - 102, 4, height - 8, C_BG);
		wk_glyph (canvas, WKG_MINUS, width - 88, height / 2, 8, C_TEXT);
		text_at (canvas, f, width - 52, height / 2 + 5, z, C_TEXT, 1, Rect { 0, width - 80, height - 1, width - 24 });
		wk_glyph (canvas, WKG_PLUS, width - 14, height / 2, 8, C_TEXT);
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		bool in = mx >= 0 && my >= 0 && mx < width && my < height;
		if (bl && in && !pressed)
		{
			pressed = true;
			if (mx >= width - 98 && mx < width - 76 && onZoom) onZoom (-1);
			else if (mx >= width - 24 && onZoom) onZoom (1);
			else if (mx >= width - 76 && mx < width - 24 && onZoom) onZoom (0);
		}
		if (!bl) pressed = false;
		return in;
	}
};

} // namespace ss

#endif
