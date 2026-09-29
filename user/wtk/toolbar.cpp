//
// wtk/toolbar.cpp -- ToolBar, ToolButton and their icons (wtk/toolbar.h). The icons are drawn in a
// 20 x 20 design box scaled to the size asked, from their geometry (wtk/vpaint.h: anti-aliased).
//
#include "wtk/toolbar.h"
#include "wtk/vpaint.h"

namespace wtk {

// ---- the icons -------------------------------------------------------------------------------------------
// Design coordinates in 1/16 of the 20-px box (D (x) = x px of it), scaled to the canvas's 1/16 px.
namespace {
struct Pen
{
	VPath p; int X, Y, S; unsigned ink; Canvas &cv;
	Pen (Canvas &c, int x, int y, int size, unsigned k) : X (V (x)), Y (V (y)), S (size), ink (k), cv (c) {}
	int x (int d) const { return X + d * S / 20; }
	int y (int d) const { return Y + d * S / 20; }
	int w (int d) const { int v = d * S / 20; return v < 18 ? 18 : v; }	// (a stroke: 1.1 px at least)
	void fill () { p.fill (cv, ink); p.clear (); }
	void line (int x0, int y0, int x1, int y1, int wd) { p.line (x (x0), y (y0), x (x1), y (y1), w (wd)); }
	void rect (int x0, int y0, int ww, int hh) { p.rect (x (x0), y (y0), ww * S / 20, hh * S / 20); }
	void rrect (int x0, int y0, int ww, int hh, int r) { p.rrect (x (x0), y (y0), ww * S / 20, hh * S / 20, r * S / 20); }
	void circle (int cx, int cy, int r) { p.circle (x (cx), y (cy), r * S / 20); }
	void arc (int cx, int cy, int r, int a0, int a1, int wd) { p.arc (x (cx), y (cy), r * S / 20, a0, a1, w (wd)); }
	void poly (const int *d, int n) { int q[32]; for (int i = 0; i < n && i < 16; i++) { q[2 * i] = x (d[2 * i]); q[2 * i + 1] = y (d[2 * i + 1]); } p.poly (q, n); }
	void loop (const int *d, int n, int wd, bool closed = true)
	{ int q[32]; for (int i = 0; i < n && i < 16; i++) { q[2 * i] = x (d[2 * i]); q[2 * i + 1] = y (d[2 * i + 1]); } p.polyline (q, n, w (wd), closed); }
	void head (int tx, int ty, int deg, int len, int half) { p.arrowHead (x (tx), y (ty), deg, len * S / 20, half * S / 20); }
};
}

void wk_tool_glyph (Canvas &cv, int kind, int x, int y, int size, unsigned ink)
{
	if (size < 6) size = 6;
	Pen g (cv, x, y, size, ink);
	const int T = V (1) + 10;					// the strokes' width (design): 1.6 px
	switch (kind)
	{
	case WKT_NEW:
	{
		int pg[10] = { V (4), V (2), V (12), V (2), V (16), V (6), V (16), V (18), V (4), V (18) };
		g.loop (pg, 5, T); int f[6] = { V (12), V (2), V (12), V (6), V (16), V (6) }; g.loop (f, 3, T, false);
		g.fill (); break;
	}
	case WKT_OPEN:
	{
		int f[12] = { V (2), V (4), V (8), V (4), V (10), V (6), V (18), V (6), V (18), V (16), V (2), V (16) };
		g.loop (f, 6, T); g.line (V (2), V (9), V (18), V (9), T); g.fill (); break;
	}
	case WKT_SAVE:
	{
		int f[12] = { V (3), V (3), V (14), V (3), V (17), V (6), V (17), V (17), V (3), V (17), V (3), V (3) };
		g.loop (f, 6, T, false); g.rect (V (7), V (3), V (6), V (5)); g.fill ();
		int lab[8] = { V (6), V (11), V (14), V (11), V (14), V (17), V (6), V (17) }; g.loop (lab, 4, T); g.fill (); break;
	}
	case WKT_UNDO:
		g.arc (V (11), V (11), V (6), -40, 165, T); g.head (V (4) + 8, V (13), 250, V (4), V (3)); g.fill (); break;
	case WKT_REDO:
		g.arc (V (9), V (11), V (6), 220, 15, T); g.head (V (16) - 8, V (13), 290, V (4), V (3)); g.fill (); break;
	case WKT_CUT:
		g.arc (V (6), V (15), V (2) + 4, 0, 360, T); g.arc (V (14), V (15), V (2) + 4, 0, 360, T);
		g.line (V (7), V (12), V (14), V (2), T); g.line (V (13), V (12), V (6), V (2), T); g.fill (); break;
	case WKT_COPY:
	{
		int a[8] = { V (3), V (6), V (12), V (6), V (12), V (18), V (3), V (18) }; g.loop (a, 4, T);
		int b[10] = { V (8), V (6), V (8), V (2), V (17), V (2), V (17), V (14), V (12), V (14) }; g.loop (b, 5, T, false);
		g.fill (); break;
	}
	case WKT_PASTE:
	{
		int a[8] = { V (4), V (4), V (16), V (4), V (16), V (18), V (4), V (18) }; g.loop (a, 4, T);
		g.rrect (V (7), V (2), V (6), V (4), V (1)); g.fill (); break;
	}
	case WKT_PLAY:   { int t[6] = { V (6), V (4), V (16), V (10), V (6), V (16) }; g.poly (t, 3); g.fill (); break; }
	case WKT_PAUSE:  g.rect (V (5), V (4), V (3) + 8, V (12)); g.rect (V (11) + 8, V (4), V (3) + 8, V (12)); g.fill (); break;
	case WKT_STOP:   g.rrect (V (5), V (5), V (10), V (10), V (1)); g.fill (); break;
	case WKT_RECORD: g.circle (V (10), V (10), V (5) + 8); g.fill (); break;
	case WKT_TO_START:
	{ g.rect (V (4), V (5), V (2), V (10)); int t[6] = { V (16), V (5), V (16), V (15), V (7), V (10) }; g.poly (t, 3); g.fill (); break; }
	case WKT_TO_END:
	{ int t[6] = { V (4), V (5), V (13), V (10), V (4), V (15) }; g.poly (t, 3); g.rect (V (14), V (5), V (2), V (10)); g.fill (); break; }
	case WKT_REWIND:
	{
		int a[6] = { V (10), V (5), V (10), V (15), V (2), V (10) }, b[6] = { V (18), V (5), V (18), V (15), V (10), V (10) };
		g.poly (a, 3); g.poly (b, 3); g.fill (); break;
	}
	case WKT_FORWARD:
	{
		int a[6] = { V (2), V (5), V (10), V (10), V (2), V (15) }, b[6] = { V (10), V (5), V (18), V (10), V (10), V (15) };
		g.poly (a, 3); g.poly (b, 3); g.fill (); break;
	}
	case WKT_LOOP:
		g.arc (V (7), V (10), V (5), 90, 270, T); g.arc (V (13), V (10), V (5), -90, 90, T);
		g.line (V (7), V (5), V (10), V (5), T); g.line (V (13), V (15), V (10), V (15), T);
		g.head (V (14), V (5), 0, V (4), V (3)); g.head (V (6), V (15), 180, V (4), V (3));
		g.fill (); break;
	case WKT_METRONOME:
	{
		int b[8] = { V (5), V (17), V (15), V (17), V (12), V (3), V (8), V (3) }; g.loop (b, 4, T);
		g.line (V (10), V (14), V (15), V (5), T); g.circle (V (13) + 8, V (7) + 8, V (1) + 8);
		g.line (V (6), V (13), V (14), V (13), T); g.fill (); break;
	}
	case WKT_PLUS:  g.line (V (4), V (10), V (16), V (10), T + 4); g.line (V (10), V (4), V (10), V (16), T + 4); g.fill (); break;
	case WKT_MINUS: g.line (V (4), V (10), V (16), V (10), T + 4); g.fill (); break;
	case WKT_SEARCH:
		g.arc (V (8) + 8, V (8) + 8, V (5), 0, 360, T); g.line (V (12), V (12), V (17), V (17), T + 12); g.fill (); break;
	case WKT_MIXER:
		for (int i = 0; i < 3; i++) g.line (V (5 + 5 * i), V (3), V (5 + 5 * i), V (17), V (1));
		g.fill ();
		g.rrect (V (3), V (11), V (4), V (3), 8); g.rrect (V (8), V (5), V (4), V (3), 8); g.rrect (V (13), V (9), V (4), V (3), 8);
		g.fill (); break;
	case WKT_SPARK:
	{
		int s[16] = { V (9), V (2), V (11), V (8), V (17), V (10), V (11), V (12), V (9), V (18), V (7), V (12), V (1), V (10), V (7), V (8) };
		g.poly (s, 8);
		int t[16] = { V (16), V (1), V (16) + 12, V (3) + 4, V (19), V (4), V (16) + 12, V (4) + 12, V (16), V (7), V (15) + 4, V (4) + 12, V (13), V (4), V (15) + 4, V (3) + 4 };
		g.poly (t, 8); g.fill (); break;
	}
	case WKT_GEAR: wk_glyph (cv, WKG_GEAR, x + size / 2, y + size / 2, size * 16 / 20, ink); break;
	}
}

// ---- ToolButton ------------------------------------------------------------------------------------------
ToolButton::ToolButton (int w, int h, const char *tip_, Action cb)
  : Widget (0, 0, w > 0 ? w : h, h), glyph (WKT_NONE), iconFn (0), iconId (0), iconSize (18), toggle (false), on (false),
    filled (false), raised (false), onColor (WK_AUTO), iconColor (WK_AUTO), onClick (cb), arrow (0), m_part (-1), m_down (-1)
{ tip = tip_; text[0] = '\0'; if (h < 24) iconSize = h - 6 < 8 ? 8 : h - 6; }

ToolButton *ToolButton::setGlyph (int kind) { glyph = kind; invalidate (true); return this; }
ToolButton *ToolButton::setIcon (ToolIconFn fn, int id) { iconFn = fn; iconId = id; invalidate (true); return this; }
ToolButton *ToolButton::setText (const char *s)
{ int i = 0; if (s) for (; s[i] && i < 31; i++) text[i] = s[i]; text[i] = '\0'; invalidate (true); return this; }
ToolButton *ToolButton::setToggle (bool t, bool o) { toggle = t; on = o; invalidate (true); return this; }
ToolButton *ToolButton::setSplit (void (*a) (ToolButton &))
{ if (!arrow && a) resizeTo (width + SPLIT_W, height); arrow = a; invalidate (true); return this; }
ToolButton *ToolButton::fitWidth ()
{
	bool icon = glyph != WKT_NONE || iconFn;
	int w = 12 + (icon ? iconSize : 0) + (text[0] ? (icon ? 5 : 0) + wk_tw (text) : 0) + (arrow ? SPLIT_W : 0);
	if (w < height) w = height;
	resizeTo (w, height);
	return this;
}
void ToolButton::setOn (bool v) { if (v != on) { on = v; invalidate (true); } }
void ToolButton::setDisabled (bool d) { if (d != disabled) { disabled = d; invalidate (true); } }

void ToolButton::onDraw ()
{
	unsigned bg = bgColor (), acc = onColor == WK_AUTO ? C_ACCENT : onColor;
	canvas.clear (bg);
	int bw = arrow ? width - SPLIT_W : width, st = m_part >= 0 && !disabled;
	bool dn = m_down >= 0 && !disabled, full = on && filled;
	unsigned under = bg;						// what the content sits on
	if (full)							// on: filled (a play button)
	{
		int lv = dn ? 110 : st ? 150 : 138;
		wk_rbox (canvas, 0, 0, bw, height, 5, wk_tone (acc, lv + 8), wk_tone (acc, lv - 20));
		wk_rline (canvas, 0, 0, bw, height, 5, wk_tone (acc, 80), 180);
		under = acc;
	}
	else if (raised || st)						// a face: always (raised), or pointed
	{
		unsigned f = raised ? wk_tone (bg, wk_bright (bg) < 100 ? 150 : 140) : bg;
		unsigned a = dn ? wk_tone (f, 110) : wk_tone (f, st ? 156 : 142), b = dn ? wk_tone (f, 118) : wk_tone (f, st ? 136 : 124);
		wk_rbox (canvas, 0, 0, width, height, 5, a, b);
		wk_rline (canvas, 0, 0, width, height, 5, wk_tone (bg, wk_bright (bg) < 100 ? 70 : 96), raised ? 200 : 150);
		under = f;
	}
	if (on && !filled)						// on: the accent's tint
	{
		wk_rbox (canvas, 0, 0, bw, height, 5, wk_mix (under, acc, 60), wk_mix (under, acc, 76));
		wk_rline (canvas, 0, 0, bw, height, 5, wk_mix (under, acc, 150));
		under = wk_mix (under, acc, 68);
	}
	if (arrow && (st || raised)) canvas.fillRect (bw, 4, 1, height - 8, wk_tone (under, 104));
	unsigned ink = full ? wk_ink_on (acc) : wk_ink_for (under);
	if (disabled) ink = wk_mix (under, ink, 100);
	unsigned ic = iconColor != WK_AUTO && !full ? (disabled ? wk_mix (under, iconColor, 110) : iconColor) : ink;
	int d = m_down == 0 ? 1 : 0;
	bool icon = glyph != WKT_NONE || iconFn;
	int tw = text[0] ? wk_tw (text) : 0, total = (icon ? iconSize : 0) + (icon && tw ? 5 : 0) + tw;
	int x = (bw - total) / 2 + d, iy = (height - iconSize) / 2 + d;
	if (icon)
	{
		if (iconFn) iconFn (canvas, iconId, x, iy, iconSize, ic, disabled);
		else wk_tool_glyph (canvas, glyph, x, iy, iconSize, ic);
		x += iconSize + (tw ? 5 : 0);
	}
	if (tw) wk_text_l (canvas, x, d, height, text, ink);
	if (arrow) wk_glyph (canvas, WKG_CHEV_DOWN, bw + SPLIT_W / 2, height / 2 + 1, 7, disabled ? wk_mix (bg, wk_ink_for (bg), 110) : wk_ink_for (bg));
}

bool ToolButton::onMouse (int mx, int my, int bl, int, int, int)
{
	int part = mx < 0 || my < 0 || mx >= width || my >= height ? -1 : arrow && mx >= width - SPLIT_W ? 1 : 0;
	if (part != m_part) { m_part = part; invalidate (true); }
	if (disabled) return part >= 0;
	if (bl && m_down < 0 && part >= 0) { m_down = part; invalidate (true); }
	else if (!bl && m_down >= 0)
	{
		int was = m_down; m_down = -1; invalidate (true);
		if (part == was)
		{
			if (was == 1 && arrow) arrow (*this);
			else { if (toggle) on = !on; if (onClick) onClick (*this); }
		}
	}
	return part >= 0;
}

// ---- ToolBar ---------------------------------------------------------------------------------------------
ToolBar::ToolBar (int l, int t, int w, int h)
  : Widget (l, t, w, h), bg (WK_AUTO), line (false), m_x (6), m_rx (w - 6), m_nsep (0) {}

void ToolBar::add (Widget *w, int gap)
{
	m_x += gap;
	w->left = m_x; w->top = (height - w->height) / 2;
	addChild (w);
	m_x += w->width;
}

void ToolBar::addRight (Widget *w, int gap)
{
	m_rx -= gap + w->width;
	w->left = m_rx; w->top = (height - w->height) / 2;
	w->anchor = ANCHOR_RIGHT | ANCHOR_TOP;
	addChild (w);
}

void ToolBar::sep () { if (m_nsep < 24) m_sepX[m_nsep++] = m_x + 5; m_x += 11; }

void ToolBar::onDraw ()
{
	unsigned b = bgColor ();
	canvas.clear (b);
	for (int i = 0; i < m_nsep; i++) wk_etch_v (canvas, m_sepX[i], 7, height - 14, b);
	if (line) wk_etch_h (canvas, 0, height - 2, width, b);
}

} // namespace wtk
