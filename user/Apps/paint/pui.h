//
// pui.h -- Paint's controls, in the way of Windows 11's: the ribbon (Clipboard, Image -- select,
// crop, resize, rotate / flip --, Tools, Shapes -- a gallery, the outline and the fill --, Size,
// Colours -- colour 1 and colour 2, a palette of twenty, ten custom colours, Edit colours), the
// layers' panel (each layer's thumbnail, name and eye; add, duplicate, delete, move, merge; its
// opacity), the status bar (the pointer, the selection, the picture's size, the zoom); the icons,
// drawn from their geometry (wtk's VPath).
//
#ifndef _paint_pui_h
#define _paint_pui_h

#include "pview.h"

namespace pd {

using namespace wtk;

// ---- icons (20 x 20 at (x, y)) -------------------------------------------------------------------------------
enum { I_PASTE, I_CUT, I_COPY, I_SELECT, I_CROP, I_RESIZE, I_ROTATE, I_PENCIL, I_FILL, I_ERASER, I_PICKER, I_ZOOM,
       I_BRUSH, I_OUTLINE, I_FILLSHAPE, I_SIZE, I_EDITCOL, I_UNDO, I_REDO, I_EYE, I_EYEOFF, I_ADD, I_DUP, I_TRASH,
       I_UP, I_DOWN, I_MERGE, I_POINTER, I_SELSIZE, I_IMGSIZE, I_GRID, I_FIT };

static void icon (Canvas &cv, int k, int x, int y, unsigned ink, bool off = false)
{
	VPath p;
	int X = V (x), Y = V (y);
	unsigned dim = wk_mix (ink, C_BG, 150);
	if (off) ink = dim;
	int A = off ? 110 : 255;
	auto pts = [&] (int *a, int n) { for (int i = 0; i < n; i++) { a[2 * i] = X + a[2 * i] * 16 / 16; a[2 * i + 1] = Y + a[2 * i + 1]; } };
	(void) pts;
	auto L = [&] (int x0, int y0, int x1, int y1, int w) { p.line (X + x0, Y + y0, X + x1, Y + y1, w); };
	switch (k)
	{
	case I_PASTE:
		p.rrect (X + V (3), Y + V (3), V (14), V (16), V (2)); p.fill (cv, 0xB07A3E, A);
		p.clear (); p.rect (X + V (5), Y + V (6), V (10), V (11)); p.fill (cv, 0xFFFFFF, A);
		p.clear (); p.rrect (X + V (7), Y + V (1), V (6), V (4), V (1)); p.fill (cv, wk_mix (ink, 0x909090, 120), A);
		break;
	case I_CUT:
		L (V (6), V (2), V (12), V (13), 26); L (V (14), V (2), V (8), V (13), 26); p.fill (cv, ink);
		p.clear (); p.circle (X + V (6), Y + V (15), V (3)); p.hole (X + V (6), Y + V (15), V (2) - 4);
		p.circle (X + V (14), Y + V (15), V (3)); p.hole (X + V (14), Y + V (15), V (2) - 4); p.fill (cv, 0xC0392B, A);
		break;
	case I_COPY:
		for (int k2 = 0; k2 < 2; k2++)
		{
			int ox = k2 ? V (8) : V (2), oy = k2 ? V (6) : V (2);
			p.clear (); p.rect (X + ox, Y + oy, V (10), V (12)); p.fill (cv, 0xFFFFFF, A);
			p.clear (); int f[8] = { X + ox, Y + oy, X + ox + V (10), Y + oy, X + ox + V (10), Y + oy + V (12), X + ox, Y + oy + V (12) };
			p.polyline (f, 4, 22, true); p.fill (cv, ink);
		}
		break;
	case I_SELECT:
		for (int i = 0; i < 16; i += 4) { p.rect (X + V (2 + i), Y + V (3), V (2), 20); p.rect (X + V (2 + i), Y + V (16) - 4, V (2), 20); }
		for (int i = 0; i < 13; i += 4) { p.rect (X + V (2), Y + V (3 + i), 20, V (2)); p.rect (X + V (18) - 4, Y + V (3 + i), 20, V (2)); }
		p.fill (cv, ink);
		break;
	case I_CROP:
		L (V (5), V (1), V (5), V (15), 36); L (V (5), V (15), V (19), V (15), 36);
		L (V (1), V (5), V (15), V (5), 36); L (V (15), V (5), V (15), V (19), 36);
		p.fill (cv, ink);
		break;
	case I_RESIZE:
		p.rect (X + V (2), Y + V (8), V (10), V (10)); p.fill (cv, 0xFFFFFF, A);
		p.clear (); { int f[8] = { X + V (2), Y + V (8), X + V (12), Y + V (8), X + V (12), Y + V (18), X + V (2), Y + V (18) }; p.polyline (f, 4, 22, true); } p.fill (cv, ink);
		p.clear (); L (V (9), V (11), V (17), V (3), 28); p.arrowHead (X + V (18), Y + V (2), 45, V (6), V (3)); p.fill (cv, C_ACCENT);
		break;
	case I_ROTATE:
		p.arc (X + V (10), Y + V (11), V (6), 200, -60, 30); p.fill (cv, ink);
		p.clear (); p.arrowHead (X + V (16), Y + V (15), -100, V (6), V (4)); p.fill (cv, ink);
		break;
	case I_PENCIL:
	{
		int b[8] = { X + V (15), Y + V (2), X + V (18), Y + V (5), X + V (7), Y + V (16), X + V (4), Y + V (13) };
		p.poly (b, 4); p.fill (cv, 0xF2B632, A);
		p.clear (); int t[6] = { X + V (4), Y + V (13), X + V (7), Y + V (16), X + V (2), Y + V (18) }; p.poly (t, 3); p.fill (cv, 0xE8C9A0, A);
		p.clear (); p.polyline (b, 4, 18, true); p.fill (cv, ink);
		p.clear (); p.circle (X + V (3), Y + V (17), 24); p.fill (cv, ink);
		break;
	}
	case I_FILL:
	{
		int b[8] = { X + V (9), Y + V (2), X + V (17), Y + V (10), X + V (10), Y + V (17), X + V (2), Y + V (9) };
		p.poly (b, 4); p.fill (cv, 0xFFFFFF, A);
		p.clear (); p.polyline (b, 4, 22, true); p.fill (cv, ink);
		p.clear (); int w[8] = { X + V (2), Y + V (9), X + V (17), Y + V (10), X + V (10), Y + V (17), X + V (2), Y + V (10) };
		p.poly (w, 4); p.fill (cv, 0x3569B5, A);
		p.clear (); p.circle (X + V (17), Y + V (15), V (2)); p.fill (cv, 0x3569B5, A);
		break;
	}
	case I_ERASER:
	{
		int b[8] = { X + V (11), Y + V (3), X + V (18), Y + V (10), X + V (11), Y + V (17), X + V (4), Y + V (10) };
		p.poly (b, 4); p.fill (cv, 0xE88AA0, A);
		p.clear (); int w[8] = { X + V (4), Y + V (10), X + V (7) + 8, Y + V (13) + 8, X + V (4) + 8, Y + V (16) + 8, X + V (1), Y + V (13) };
		p.poly (w, 4); p.fill (cv, 0xFFFFFF, A);
		p.clear (); int o[12] = { X + V (11), Y + V (3), X + V (18), Y + V (10), X + V (11), Y + V (17), X + V (5), Y + V (17), X + V (1), Y + V (13), X + V (11), Y + V (3) };
		p.polyline (o, 6, 20, false); p.fill (cv, ink);
		break;
	}
	case I_PICKER:
		L (V (4), V (16), V (12), V (8), 44); p.fill (cv, 0x9BB8D8, A);
		p.clear (); L (V (4), V (16), V (12), V (8), 12); L (V (2), V (18), V (4), V (16), 20); p.fill (cv, ink);
		p.clear (); p.rrect (X + V (11), Y + V (2), V (7), V (7), V (3)); p.fill (cv, ink);
		p.clear (); L (V (10), V (6), V (14), V (10), 30); p.fill (cv, ink);
		break;
	case I_ZOOM:
		p.circle (X + V (8), Y + V (8), V (6)); p.hole (X + V (8), Y + V (8), V (5) - 8); L (V (12), V (12), V (18), V (18), 44); p.fill (cv, ink);
		p.clear (); p.circle (X + V (8), Y + V (8), V (5) - 8); p.fill (cv, 0xDDEBF7, A);
		break;
	case I_BRUSH:
	{
		L (V (17), V (2), V (9), V (10), 36); p.fill (cv, 0x8A5D12, A);
		p.clear (); int t[8] = { X + V (8), Y + V (9), X + V (11), Y + V (12), X + V (6), Y + V (18), X + V (2), Y + V (18) };
		p.poly (t, 4); p.fill (cv, C_ACCENT, A);
		break;
	}
	case I_OUTLINE:
		p.rrect (X + V (3), Y + V (4), V (14), V (12), V (2)); p.hole (0, 0, 0);
		p.clear (); { int f[8] = { X + V (3), Y + V (4), X + V (17), Y + V (4), X + V (17), Y + V (16), X + V (3), Y + V (16) }; p.polyline (f, 4, 36, true); }
		p.fill (cv, ink);
		break;
	case I_FILLSHAPE:
		p.rrect (X + V (3), Y + V (4), V (14), V (12), V (2)); p.fill (cv, C_ACCENT, A);
		break;
	case I_SIZE:
		for (int i = 0; i < 4; i++) p.rect (X + V (2), Y + V (3 + i * 4), V (16), 8 + i * 12);
		p.fill (cv, ink);
		break;
	case I_EDITCOL:
	{
		static const unsigned rb[6] = { 0xE74C3C, 0xF39C12, 0xF1C40F, 0x2ECC71, 0x3498DB, 0x9B59B6 };
		for (int i = 0; i < 6; i++)
		{
			p.clear ();
			int a0 = 90 + i * 60, a1 = a0 + 62;
			int t[8] = { X + V (10), Y + V (10), X + V (10) + V (8) * wk_cos (a0) / 16384, Y + V (10) - V (8) * wk_sin (a0) / 16384,
				     X + V (10) + V (8) * wk_cos ((a0 + a1) / 2) / 16384, Y + V (10) - V (8) * wk_sin ((a0 + a1) / 2) / 16384,
				     X + V (10) + V (8) * wk_cos (a1) / 16384, Y + V (10) - V (8) * wk_sin (a1) / 16384 };
			p.poly (t, 4); p.fill (cv, rb[i], A);
		}
		p.clear (); p.circle (X + V (10), Y + V (10), V (3)); p.fill (cv, 0xFFFFFF, A);
		break;
	}
	case I_UNDO: case I_REDO:
	{
		bool r = k == I_REDO;
		if (r) p.arc (X + V (10), Y + V (12), V (6), 180, -60, 30); else p.arc (X + V (10), Y + V (12), V (6), 0, 240, 30);
		p.fill (cv, ink);
		p.clear (); if (r) p.arrowHead (X + V (17), Y + V (13), -80, V (6), V (4)); else p.arrowHead (X + V (3), Y + V (13), 260, V (6), V (4));
		p.fill (cv, ink);
		break;
	}
	case I_EYE: case I_EYEOFF:
		p.ellipse (X + V (10), Y + V (10), V (8), V (5)); p.fill (cv, k == I_EYE ? 0xFFFFFF : wk_mix (C_BG, 0xFFFFFF, 90), A);
		p.clear (); { int e[] = { X + V (2), Y + V (10), X + V (6), Y + V (6), X + V (10), Y + V (5), X + V (14), Y + V (6), X + V (18), Y + V (10), X + V (14), Y + V (14), X + V (10), Y + V (15), X + V (6), Y + V (14) }; p.polyline (e, 8, 22, true); }
		p.fill (cv, k == I_EYE ? ink : dim);
		if (k == I_EYE) { p.clear (); p.circle (X + V (10), Y + V (10), V (3)); p.fill (cv, ink); }
		else { p.clear (); L (V (3), V (17), V (17), V (3), 26); p.fill (cv, dim); }
		break;
	case I_ADD: p.rect (X + V (9), Y + V (3), V (2), V (14)); p.rect (X + V (3), Y + V (9), V (14), V (2)); p.fill (cv, ink); break;
	case I_DUP:
		for (int k2 = 0; k2 < 2; k2++)
		{
			int ox = k2 ? V (7) : V (2), oy = k2 ? V (7) : V (2);
			p.clear (); p.rect (X + ox, Y + oy, V (11), V (11)); p.fill (cv, k2 ? 0xFFFFFF : wk_mix (C_BG, 0xFFFFFF, 120), A);
			p.clear (); int f[8] = { X + ox, Y + oy, X + ox + V (11), Y + oy, X + ox + V (11), Y + oy + V (11), X + ox, Y + oy + V (11) };
			p.polyline (f, 4, 22, true); p.fill (cv, ink);
		}
		break;
	case I_TRASH:
		p.rect (X + V (4), Y + V (5), V (12), V (2)); p.rect (X + V (8), Y + V (3), V (4), V (2)); p.fill (cv, ink);
		p.clear (); { int b[8] = { X + V (5), Y + V (8), X + V (15), Y + V (8), X + V (14), Y + V (18), X + V (6), Y + V (18) }; p.polyline (b, 4, 22, true); }
		L (V (8), V (10), V (8), V (16), 16); L (V (12), V (10), V (12), V (16), 16); p.fill (cv, ink);
		break;
	case I_UP: case I_DOWN:
		if (k == I_UP) { p.arrowHead (X + V (10), Y + V (3), 90, V (7), V (6)); p.rect (X + V (8), Y + V (9), V (4), V (8)); }
		else { p.arrowHead (X + V (10), Y + V (17), 270, V (7), V (6)); p.rect (X + V (8), Y + V (3), V (4), V (8)); }
		p.fill (cv, ink);
		break;
	case I_MERGE:
		p.rect (X + V (3), Y + V (13), V (14), V (4)); p.fill (cv, ink);
		p.clear (); p.arrowHead (X + V (10), Y + V (12), 270, V (6), V (5)); p.rect (X + V (8), Y + V (2), V (4), V (5)); p.fill (cv, C_ACCENT);
		break;
	case I_POINTER:
	{
		int a[14] = { X + V (4), Y + V (2), X + V (4), Y + V (16), X + V (8), Y + V (12), X + V (11), Y + V (18), X + V (13), Y + V (17), X + V (10), Y + V (11), X + V (15), Y + V (11) };
		p.poly (a, 7); p.fill (cv, 0xFFFFFF, A);
		p.clear (); p.polyline (a, 7, 18, true); p.fill (cv, ink);
		break;
	}
	case I_SELSIZE:
		for (int i = 0; i < 14; i += 4) { p.rect (X + V (3 + i), Y + V (4), V (2), 18); p.rect (X + V (3 + i), Y + V (15), V (2), 18); }
		for (int i = 0; i < 11; i += 4) { p.rect (X + V (3), Y + V (4 + i), 18, V (2)); p.rect (X + V (17), Y + V (4 + i), 18, V (2)); }
		p.fill (cv, ink);
		break;
	case I_GRID:
		for (int i = 0; i < 4; i++) { p.rect (X + V (2 + i * 5), Y + V (2), 20, V (16)); p.rect (X + V (2), Y + V (2 + i * 5), V (16), 20); }
		p.rect (X + V (17) + 12, Y + V (2), 20, V (16)); p.rect (X + V (2), Y + V (17) + 12, V (16), 20);
		p.fill (cv, ink);
		p.clear (); p.rect (X + V (7) + 4, Y + V (7) + 4, V (4), V (4)); p.fill (cv, C_ACCENT, A);
		break;
	case I_FIT:
		p.rect (X + V (5), Y + V (5), V (10), V (10)); p.fill (cv, 0xFFFFFF, A);
		p.clear (); { int f[8] = { X + V (5), Y + V (5), X + V (15), Y + V (5), X + V (15), Y + V (15), X + V (5), Y + V (15) }; p.polyline (f, 4, 20, true); }
		L (V (2), V (2), V (2), V (6), 24); L (V (2), V (2), V (6), V (2), 24); L (V (18), V (2), V (18), V (6), 24); L (V (18), V (2), V (14), V (2), 24);
		L (V (2), V (18), V (2), V (14), 24); L (V (2), V (18), V (6), V (18), 24); L (V (18), V (18), V (18), V (14), 24); L (V (18), V (18), V (14), V (18), 24);
		p.fill (cv, ink);
		break;
	case I_IMGSIZE:
		p.rect (X + V (3), Y + V (4), V (14), V (12)); p.fill (cv, 0xFFFFFF, A);
		p.clear (); { int f[8] = { X + V (3), Y + V (4), X + V (17), Y + V (4), X + V (17), Y + V (16), X + V (3), Y + V (16) }; p.polyline (f, 4, 20, true); }
		p.fill (cv, ink);
		p.clear (); { int m[6] = { X + V (4), Y + V (15), X + V (9), Y + V (9), X + V (13), Y + V (15) }; p.poly (m, 3); } p.fill (cv, 0x4A9A55, A);
		break;
	}
}

// A shape's little picture in the gallery (in a w x h box at x, y).
static void shape_icon (Canvas &cv, int k, int x, int y, int w, int h, unsigned ink)
{
	int xy[512];
	if (k == SH_LINE) { VPath p; p.line (V (x + 2), V (y + h - 3), V (x + w - 3), V (y + 2), 20); p.fill (cv, ink); return; }
	if (k == SH_ELLIPSE) { VPath p; p.ellipse (V (x) + V (w) / 2, V (y) + V (h) / 2, V (w) / 2 - 24, V (h) / 2 - 24); p.hole (0, 0, 0); p.clear ();
		int n = 48; int e[96]; for (int i = 0; i < n; i++) { int d = 360 * i / n; e[2 * i] = V (x) + V (w) / 2 + (V (w) / 2 - 24) * wk_cos (d) / 16384; e[2 * i + 1] = V (y) + V (h) / 2 - (V (h) / 2 - 24) * wk_sin (d) / 16384; }
		p.polyline (e, n, 20, true); p.fill (cv, ink); return; }
	int n = shape_points (k, x + 1, y + 1, x + w - 2, y + h - 2, xy);
	VPath p;
	for (int i = 0; i < n; i++) { xy[2 * i] -= 8; xy[2 * i + 1] -= 8; }	// (pixel centres -> edges)
	p.polyline (xy, n, 20, true);
	p.fill (cv, ink);
}

// ---- the ribbon ----------------------------------------------------------------------------------------------
enum { RIBBON_H = 100, CELL = 26, PAL = 19 };
// Its commands (the app runs them).
enum { C_PASTE, C_CUT, C_COPY, C_SELECT, C_CROP, C_RESIZE, C_ROTATE, C_TOOL, C_SHAPE, C_OUTLINE, C_FILLSHAPE, C_SIZE,
       C_COL1, C_COL2, C_PALETTE, C_CUSTOM, C_EDITCOL, C_UNDO, C_REDO, C_GRID, C_FIT };
static void (*g_ribbonCmd) (int cmd, int arg, int btn, int x, int y);	// (x, y: where a popup opens)

static const unsigned PALETTE[20] = {
	0x000000, 0x7F7F7F, 0x880015, 0xED1C24, 0xFF7F27, 0xFFF200, 0x22B14C, 0x00A2E8, 0x3F48CC, 0xA349A4,
	0xFFFFFF, 0xC3C3C3, 0xB97A57, 0xFFAEC9, 0xFFC90E, 0xEFE4B0, 0xB5E61D, 0x99D9EA, 0x7092BE, 0xC8BFE7 };
static unsigned g_custom[10];					// (0xFF000000 | colour; 0: empty)
static int g_activeCol = 1;					// the palette sets colour 1 or 2

class Ribbon : public Widget
{
public:
	Ribbon (int l, int t, int w) : Widget (l, t, w, RIBBON_H), m_hot (-1), m_down (-1) {}
	unsigned bgColor () override { return C_BG; }
	void onDraw () override
	{
		layoutCells ();
		canvas.fillRect (0, 0, width, height, C_BG);
		wk_etch_h (canvas, 0, height - 2, width, C_BG);
		unsigned ink = wk_ink_for (C_BG), dim = wk_mix (C_BG, ink, 150);
		for (int g = 0; g < m_ng; g++)
		{
			wk_text_c (canvas, m_gx[g], height - 22, m_gw[g], 18, m_gname[g], dim);
			if (g < m_ng - 1) wk_etch_v (canvas, m_gx[g] + m_gw[g] + 4, 8, height - 16, C_BG);
		}
		for (int i = 0; i < m_n; i++) drawCell (i, ink);
	}
	bool onMouse (int mx, int my, int bl, int br, int, int) override
	{
		int hot = cellAt (mx, my);
		if (hot != m_hot) { m_hot = hot; tip = hot >= 0 ? m_cell[hot].tip : 0; invalidate (true); }
		int btn = bl ? 1 : br ? 2 : 0;
		if (btn && m_down < 0 && hot >= 0) { m_down = hot; m_btn = btn; invalidate (true); }
		else if (!btn && m_down >= 0)
		{
			int d = m_down; m_down = -1; invalidate (true);
			if (d == hot && g_ribbonCmd)
			{
				int ax = 0, ay = 0;
				for (Widget *w = this; w && w->parent; w = w->parent) { ax += w->left; ay += w->top; }
				const Cell &c = m_cell[d];
				g_ribbonCmd (c.cmd, c.arg, m_btn, ax + c.x, ay + c.y + c.h + 2);
			}
		}
		return mx >= 0 && my >= 0 && mx < width && my < height;
	}
private:
	struct Cell { int x, y, w, h, cmd, arg; const char *tip; const char *label; int icon; };
	enum { MAXC = 96 };
	Cell m_cell[MAXC]; int m_n;
	int m_gx[8], m_gw[8], m_ng; const char *m_gname[8];
	int m_hot, m_down, m_btn;

	void add (int x, int y, int w, int h, int cmd, int arg, const char *tip, int ic = -1, const char *label = 0)
	{
		if (m_n >= MAXC) return;
		Cell &c = m_cell[m_n++];
		c.x = x; c.y = y; c.w = w; c.h = h; c.cmd = cmd; c.arg = arg; c.tip = tip; c.icon = ic; c.label = label;
	}
	void group (int x, int w, const char *name) { m_gx[m_ng] = x; m_gw[m_ng] = w; m_gname[m_ng] = name; m_ng++; }
	void layoutCells ()
	{
		m_n = 0; m_ng = 0;
		int x = 8, top = 8;
		// Edit: Paste (big), Cut, Copy, Undo, Redo
		add (x, top, 44, 62, C_PASTE, 0, "Paste (Ctrl+V)", I_PASTE, "Paste");
		add (x + 48, top, CELL, CELL, C_CUT, 0, "Cut (Ctrl+X)", I_CUT);
		add (x + 48, top + 32, CELL, CELL, C_COPY, 0, "Copy (Ctrl+C)", I_COPY);
		add (x + 78, top, CELL, CELL, C_UNDO, 0, "Undo (Ctrl+Z)", I_UNDO);
		add (x + 78, top + 32, CELL, CELL, C_REDO, 0, "Redo (Ctrl+Y)", I_REDO);
		group (x, 104, "Edit"); x += 112;
		// Image: Select (big), Crop, Resize, Rotate
		add (x, top, 44, 62, C_SELECT, 0, "Select a rectangle (S)", I_SELECT, "Select");
		add (x + 48, top, 72, 20, C_CROP, 0, "Crop to the selection", I_CROP, "Crop");
		add (x + 48, top + 21, 72, 20, C_RESIZE, 0, "Resize the picture or its canvas (Ctrl+W)", I_RESIZE, "Resize");
		add (x + 48, top + 42, 72, 20, C_ROTATE, 0, "Rotate or flip (the selection, or the picture)", I_ROTATE, "Rotate");
		group (x, 120, "Image"); x += 128;
		// Tools: 3 x 2
		static const int TI[6] = { T_PENCIL, T_FILL, T_ERASER, T_PICKER, T_ZOOM, T_BRUSH };
		static const int TIC[6] = { I_PENCIL, I_FILL, I_ERASER, I_PICKER, I_ZOOM, I_BRUSH };
		static const char *const TT[6] = { "Pencil (P)", "Fill (F)", "Eraser: to transparent (E)", "Colour picker (K)", "Magnifier: left in, right out (Z)", "Brush (B)" };
		for (int i = 0; i < 6; i++) add (x + (i % 3) * (CELL + 4), top + 4 + (i / 3) * (CELL + 6), CELL, CELL, C_TOOL, TI[i], TT[i], TIC[i]);
		group (x, 3 * (CELL + 4) - 4, "Tools"); x += 3 * (CELL + 4) + 4;
		// Shapes: the gallery 5 x 3, Outline / Fill
		for (int k = 0; k < SH_COUNT; k++) add (x + (k % 5) * 23, top + (k / 5) * 22, 22, 21, C_SHAPE, k, SHAPE_NAMES[k]);
		add (x + 5 * 23 + 4, top + 4, 74, 24, C_OUTLINE, 0, "The shapes' outline (colour 1)", I_OUTLINE, "Outline");
		add (x + 5 * 23 + 4, top + 34, 74, 24, C_FILLSHAPE, 0, "The shapes' inside (colour 2)", I_FILLSHAPE, "Fill");
		group (x, 5 * 23 + 78, "Shapes"); x += 5 * 23 + 86;
		// Size
		add (x, top, 44, 62, C_SIZE, 0, "The line's width", I_SIZE, "Size");
		group (x, 44, "Size"); x += 52;
		// Colours: 1, 2, the palette, the custom ones, Edit colours
		add (x, top, 36, 62, C_COL1, 0, "Colour 1: the left button's (the palette sets it when chosen)", -1, "1");
		add (x + 38, top, 36, 62, C_COL2, 0, "Colour 2: the right button's, the shapes' inside", -1, "2");
		int px0 = x + 80;
		for (int i = 0; i < 20; i++) add (px0 + (i % 10) * (PAL + 1), top + (i / 10) * (PAL + 1), PAL, PAL, C_PALETTE, i, "Left: colour 1 (or the chosen one) / right: colour 2");
		for (int i = 0; i < 10; i++) add (px0 + i * (PAL + 1), top + 2 * (PAL + 1) + 1, PAL, PAL, C_CUSTOM, i, "A colour of yours (Edit colours adds them)");
		add (px0 + 10 * (PAL + 1) + 4, top, 44, 62, C_EDITCOL, 0, "Edit colours: any colour (it joins your colours)", I_EDITCOL, "Edit");
		group (x, 80 + 10 * (PAL + 1) + 48, "Colours"); x += 80 + 10 * (PAL + 1) + 56;
		// View: the grid (a toggle), the whole picture
		add (x, top, 62, 28, C_GRID, 0, "Show or hide the pixel grid (Ctrl+G; seen from 300 %)", I_GRID, "Grid");
		add (x, top + 34, 62, 28, C_FIT, 0, "The whole picture in the window", I_FIT, "Fit");
		group (x, 62, "View");
	}
	int cellAt (int mx, int my) const
	{
		for (int i = 0; i < m_n; i++) { const Cell &c = m_cell[i]; if (mx >= c.x && my >= c.y && mx < c.x + c.w && my < c.y + c.h) return i; }
		return -1;
	}
	bool isOn (const Cell &c) const
	{
		switch (c.cmd)
		{
		case C_SELECT: return g_tool == T_SELECT;
		case C_TOOL: return g_tool == c.arg;
		case C_SHAPE: return g_tool == T_SHAPE && g_shape == c.arg;
		case C_OUTLINE: return g_outline;
		case C_FILLSHAPE: return g_fillShape;
		case C_GRID: return g_grid;
		case C_COL1: return g_activeCol == 1;
		case C_COL2: return g_activeCol == 2;
		}
		return false;
	}
	void drawCell (int i, unsigned ink)
	{
		const Cell &c = m_cell[i];
		bool hot = i == m_hot, down = i == m_down, on = isOn (c);
		if (c.cmd == C_PALETTE || c.cmd == C_CUSTOM)
		{
			unsigned col = c.cmd == C_PALETTE ? PALETTE[c.arg] : g_custom[c.arg] & 0xFFFFFF;
			bool empty = c.cmd == C_CUSTOM && !(g_custom[c.arg] >> 24);
			if (empty) { canvas.fillRect (c.x, c.y, c.w, c.h, wk_mix (C_BG, 0xFFFFFF, 90)); canvas.frameRect (c.x, c.y, c.w, c.h, wk_mix (C_BG, ink, 60)); }
			else { canvas.fillRect (c.x, c.y, c.w, c.h, col); canvas.frameRect (c.x, c.y, c.w, c.h, wk_mix (col, 0, 70)); }
			if (hot) { canvas.frameRect (c.x - 2, c.y - 2, c.w + 4, c.h + 4, C_ACCENT); canvas.frameRect (c.x - 1, c.y - 1, c.w + 2, c.h + 2, 0xFFFFFF); }
			return;
		}
		if (on) { wk_rbox (canvas, c.x, c.y, c.w, c.h, 5, wk_mix (C_BG, C_ACCENT, 60), wk_mix (C_BG, C_ACCENT, 72)); wk_rline (canvas, c.x, c.y, c.w, c.h, 5, wk_mix (C_BG, C_ACCENT, 150)); }
		else if (hot || down) { wk_rbox (canvas, c.x, c.y, c.w, c.h, 5, down ? wk_tone (C_BG, 112) : wk_tone (C_BG, 150), down ? wk_tone (C_BG, 120) : wk_tone (C_BG, 138)); wk_rline (canvas, c.x, c.y, c.w, c.h, 5, wk_tone (C_BG, 96), 150); }
		if (c.cmd == C_COL1 || c.cmd == C_COL2)
		{
			unsigned col = (c.cmd == C_COL1 ? g_col1 : g_col2) & 0xFFFFFF;
			int s = 28, sx2 = c.x + (c.w - s) / 2, sy2 = c.y + 6;
			canvas.fillRect (sx2, sy2, s, s, col); canvas.frameRect (sx2, sy2, s, s, wk_mix (col, 0, 80));
			canvas.frameRect (sx2 - 1, sy2 - 1, s + 2, s + 2, wk_mix (C_BG, ink, 70));
			wk_text_c (canvas, c.x, c.y + 40, c.w, 18, c.cmd == C_COL1 ? "1" : "2", ink);
			return;
		}
		if (c.cmd == C_SHAPE) { shape_icon (canvas, c.arg, c.x + 3, c.y + 3, c.w - 6, c.h - 6, ink); return; }
		if (c.cmd == C_SIZE)
		{
			int sz = g_sizes[g_tool];
			int lw = pclamp (sz, 1, 12);
			canvas.fillRect (c.x + 8, c.y + 18 - lw / 2, c.w - 16, lw, ink);
			char b[12]; int n = 0, v = sz; char t[8]; int j = 0; do { t[j++] = (char) ('0' + v % 10); v /= 10; } while (v); while (j) b[n++] = t[--j];
			b[n++] = ' '; b[n++] = 'p'; b[n++] = 'x'; b[n] = 0;
			wk_text_c (canvas, c.x, c.y + 40, c.w, 18, b, ink);
			return;
		}
		if (c.label && c.h >= 50)					// (a big button: its icon, its name below)
		{
			icon (canvas, c.icon, c.x + (c.w - 20) / 2, c.y + 10, ink);
			wk_text_c (canvas, c.x, c.y + 40, c.w, 18, c.label, ink);
			return;
		}
		if (c.label)							// (a wide small one: the icon, the name after it)
		{
			if (c.cmd == C_OUTLINE || c.cmd == C_FILLSHAPE || c.cmd == C_GRID || c.cmd == C_FIT) icon (canvas, c.icon, c.x + 4, c.y + (c.h - 20) / 2, ink);
			else icon (canvas, c.icon, c.x + 2, c.y + (c.h - 20) / 2, ink);
			wk_text_l (canvas, c.x + 26, c.y, c.h, c.label, ink);
			if (c.cmd == C_ROTATE) wk_glyph (canvas, WKG_CHEV_DOWN, c.x + c.w - 7, c.y + c.h / 2, 7, ink);
			return;
		}
		icon (canvas, c.icon, c.x + (c.w - 20) / 2, c.y + (c.h - 20) / 2, ink);
	}
};

// ---- the layers' panel ---------------------------------------------------------------------------------------
enum { PANEL_W = 222, ROW_H = 52, THUMB_W = 60, THUMB_H = 42 };
enum { L_ADD, L_DUP, L_DEL, L_UP, L_DOWN, L_MERGE, L_SELECT, L_TOGGLE, L_RENAME, L_MENU, L_OPACITY };
static void (*g_layerCmd) (int cmd, int arg, int x, int y);

class LayersPanel : public Widget
{
public:
	Slider *opacity;
	LayersPanel (int l, int t, int h) : Widget (l, t, PANEL_W, h), opacity (0), m_hot (-1), m_hotBtn (-1), m_top (0), m_last (0), m_lastRow (-1)
	{
		opacity = new Slider (76, h - 34, PANEL_W - 126, 22, 0, 100, 100, onOpacity, C_BG);
		opacity->anchor = ANCHOR_LEFT | ANCHOR_BOTTOM;
		addChild (opacity);
	}
	unsigned bgColor () override { return C_BG; }
	int listTop () const { return 34 + 32; }
	int listH () const { return height - listTop () - 46; }
	void sync () { int v = (D.lay[D.cur].opacity * 100 + 127) / 255; if (opacity->value != v) { opacity->value = v; opacity->invalidate (true); } invalidate (true); }
	void onDraw () override
	{
		canvas.fillRect (0, 0, width, height, C_BG);
		wk_etch_v (canvas, 0, 0, height, C_BG);
		unsigned ink = wk_ink_for (C_BG), dim = wk_mix (C_BG, ink, 150);
		canvas.drawFont (12, 9, "Layers", font (), ink, 1, 2);
		// the buttons
		static const int BI[6] = { I_ADD, I_DUP, I_TRASH, I_UP, I_DOWN, I_MERGE };
		for (int i = 0; i < 6; i++)
		{
			int x = 10 + i * 34, y = 34;
			bool off = (i == 2 && D.n < 2) || (i == 3 && D.cur >= D.n - 1) || (i == 4 && D.cur == 0) || (i == 5 && D.cur == 0) || (i < 2 && D.n >= MAXLAYERS);
			if (i == m_hotBtn && !off) wk_rbox (canvas, x, y, 28, 26, 5, wk_tone (C_BG, 150), wk_tone (C_BG, 138));
			icon (canvas, BI[i], x + 4, y + 3, ink, off);
		}
		// the list: the top layer first
		int y0 = listTop (), lh = listH ();
		wk_sunken (canvas, 6, y0 - 2, width - 12, lh + 4, 6, C_FIELD);
		int rows = lh / ROW_H;
		m_top = pclamp (m_top, 0, pmax (0, D.n - rows));
		for (int r = 0; r < rows; r++)
		{
			int li = D.n - 1 - (m_top + r);
			if (li < 0) break;
			int y = y0 + r * ROW_H;
			const Layer &l = D.lay[li];
			bool sel = li == D.cur;
			if (sel) wk_hilite (canvas, 9, y + 2, width - 18, ROW_H - 4, 6, true);
			else if (r == m_hot) wk_rbox (canvas, 9, y + 2, width - 18, ROW_H - 4, 6, wk_mix (C_FIELD, C_ACCENT, 30), wk_mix (C_FIELD, C_ACCENT, 30));
			thumb (li, 16, y + (ROW_H - THUMB_H) / 2);
			unsigned tc = sel ? C_SEL_TEXT : C_FIELD_TEXT;
			char nm[32]; scpy (nm, l.name, sizeof nm);
			int maxc = (width - 16 - THUMB_W - 16 - 36) / wk_fw ();
			if (slen (nm) > maxc && maxc > 3) { nm[maxc - 2] = '.'; nm[maxc - 1] = '.'; nm[maxc] = 0; }
			canvas.text (16 + THUMB_W + 10, y + 10, nm, tc);
			char op[8]; int n = 0, v = (l.opacity * 100 + 127) / 255; char t[4]; int j = 0; do { t[j++] = (char) ('0' + v % 10); v /= 10; } while (v); while (j) op[n++] = t[--j]; op[n++] = '%'; op[n] = 0;
			canvas.text (16 + THUMB_W + 10, y + 28, op, sel ? C_SEL_TEXT : wk_mix (C_FIELD, C_FIELD_TEXT, 150));
			icon (canvas, l.visible ? I_EYE : I_EYEOFF, width - 38, y + (ROW_H - 20) / 2, sel ? C_SEL_TEXT : C_FIELD_TEXT);
		}
		if (D.n > rows)
		{
			WkThumb t = wk_thumb (D.n, rows, m_top, lh);
			wk_draw_vscroll (canvas, width - WK_SBW - 8, y0, WK_SBW, lh, t, C_FIELD);
		}
		canvas.text (12, height - 30, "Opacity", dim);
		char pc[8]; int n = 0, v = opacity->value; char t2[4]; int j = 0; do { t2[j++] = (char) ('0' + v % 10); v /= 10; } while (v); while (j) pc[n++] = t2[--j]; pc[n++] = '%'; pc[n] = 0;
		canvas.text (width - 44, height - 30, pc, ink);
	}
	bool onMouse (int mx, int my, int bl, int br, int, int wheel) override
	{
		bool in = mx >= 0 && my >= 0 && mx < width && my < height;
		int y0 = listTop (), rows = listH () / ROW_H;
		if (wheel && in && my >= y0) { m_top = pclamp (m_top - wheel, 0, pmax (0, D.n - rows)); invalidate (true); return true; }
		int hb = in && my >= 34 && my < 60 && mx >= 10 && mx < 10 + 6 * 34 && (mx - 10) % 34 < 28 ? (mx - 10) / 34 : -1;
		int hr = in && my >= y0 && my < y0 + rows * ROW_H ? (my - y0) / ROW_H : -1;
		if (hr >= 0 && D.n - 1 - (m_top + hr) < 0) hr = -1;
		if (hb != m_hotBtn || hr != m_hot) { m_hotBtn = hb; m_hot = hr; tip = hb >= 0 ? BTN_TIPS[hb] : hr >= 0 ? "Click: the layer drawn on; the eye: shown / hidden; double click: its name; right click: more" : 0; invalidate (true); }
		if ((bl || br) && !pressed && in)
		{
			pressed = true;
			int ax = 0, ay = 0;
			for (Widget *w = this; w && w->parent; w = w->parent) { ax += w->left; ay += w->top; }
			if (hb >= 0 && bl) { if (g_layerCmd) g_layerCmd (L_ADD + hb, 0, 0, 0); return true; }
			if (hr >= 0)
			{
				int li = D.n - 1 - (m_top + hr);
				if (br) { if (g_layerCmd) { g_layerCmd (L_SELECT, li, 0, 0); g_layerCmd (L_MENU, li, ax + mx, ay + my); } return true; }
				if (mx >= width - 44) { if (g_layerCmd) g_layerCmd (L_TOGGLE, li, 0, 0); return true; }
				unsigned t = kapi_get_ticks ();
				bool dbl = hr == m_lastRow && t - m_last < 45;
				m_last = t; m_lastRow = hr;
				if (g_layerCmd) g_layerCmd (dbl ? L_RENAME : L_SELECT, li, 0, 0);
			}
			return true;
		}
		if (!bl && !br) pressed = false;
		return in;
	}
private:
	int m_hot, m_hotBtn, m_top; unsigned m_last; int m_lastRow;
	static const char *const BTN_TIPS[6];
	static void onOpacity (Widget &w) { if (g_layerCmd) g_layerCmd (L_OPACITY, ((Slider &) w).value, 0, 0); }
	// A layer's thumbnail: its pixels (the nearest ones) over a checkerboard, the picture's proportions.
	void thumb (int li, int x, int y)
	{
		const Layer &l = D.lay[li];
		int tw = THUMB_W, th = THUMB_H;
		if (D.w * THUMB_H > D.h * THUMB_W) th = pmax (4, D.h * THUMB_W / pmax (1, D.w)); else tw = pmax (4, D.w * THUMB_H / pmax (1, D.h));
		int ox2 = x + (THUMB_W - tw) / 2, oy2 = y + (THUMB_H - th) / 2;
		for (int j = 0; j < th; j++)
			for (int i = 0; i < tw; i++)
			{
				unsigned c = l.px[(unsigned) (j * D.h / th) * D.w + i * D.w / tw], a = c >> 24;
				unsigned ck = ((i >> 2) ^ (j >> 2)) & 1 ? 0xD8D8D8 : 0xFFFFFF;
				canvas.pixel (ox2 + i, oy2 + j, a == 255 ? c & 0xFFFFFF : a ? wk_mix (ck, c & 0xFFFFFF, (int) a + 1) : ck);
			}
		canvas.frameRect (ox2 - 1, oy2 - 1, tw + 2, th + 2, wk_mix (C_FIELD, 0, 90));
	}
};
const char *const LayersPanel::BTN_TIPS[6] = { "New layer", "Duplicate the layer", "Delete the layer", "Move the layer up", "Move the layer down", "Merge the layer into the one below" };

// ---- the status bar --------------------------------------------------------------------------------------------
enum { STATUS_H = 26 };
class StatusBar : public Widget
{
public:
	CanvasView *view;
	void (*zoomCmd) (int what);			// -1 out, +1 in, 0 the list (x of the popup in popX)
	int popX;
	StatusBar (int l, int t, int w, CanvasView *v) : Widget (l, t, w, STATUS_H), view (v), zoomCmd (0), popX (0), m_hot (0) {}
	void onDraw () override
	{
		canvas.fillRect (0, 0, width, height, C_BG);
		wk_etch_h (canvas, 0, 0, width, C_BG);
		unsigned ink = wk_ink_for (C_BG);
		char b[48];
		int x = 10;
		icon (canvas, I_POINTER, x, 3, ink); x += 24;
		if (view->ptrX >= 0) { fmt2 (b, view->ptrX, view->ptrY, ", "); wk_text_l (canvas, x, 1, height - 1, b, ink); }
		x += 110;
		Rect s = D.fl.px ? float_rect () : g_sel;
		icon (canvas, I_SELSIZE, x, 3, ink); x += 24;
		if (!s.empty ()) { fmt2 (b, s.x1 - s.x0, s.y1 - s.y0, " x "); wk_text_l (canvas, x, 1, height - 1, b, ink); }
		x += 110;
		icon (canvas, I_IMGSIZE, x, 3, ink); x += 24;
		fmt2 (b, D.w, D.h, " x "); wk_text_l (canvas, x, 1, height - 1, b, ink);
		// the zoom: - 100% +
		int zx = width - 150;
		button (zx, false);
		char z[12]; int n = 0, v = view->zoom; char t[8]; int j = 0; do { t[j++] = (char) ('0' + v % 10); v /= 10; } while (v); while (j) z[n++] = t[--j]; z[n++] = '%'; z[n] = 0;
		if (m_hot == 3) wk_rbox (canvas, zx + 22, 3, 70, height - 6, 4, wk_tone (C_BG, 150), wk_tone (C_BG, 138));
		wk_text_c (canvas, zx + 22, 1, 70, height - 1, z, ink);
		button (zx + 94, true);
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		int zx = width - 150;
		int hot = my >= 0 && my < height ? (mx >= zx && mx < zx + 20 ? 1 : mx >= zx + 94 && mx < zx + 114 ? 2 : mx >= zx + 22 && mx < zx + 92 ? 3 : 0) : 0;
		if (hot != m_hot) { m_hot = hot; invalidate (true); }
		if (bl && !pressed && hot) { pressed = true; popX = zx + 22; if (zoomCmd) zoomCmd (hot == 1 ? -1 : hot == 2 ? 1 : 0); }
		if (!bl) pressed = false;
		return mx >= 0 && my >= 0 && mx < width && my < height;
	}
private:
	int m_hot;
	static void fmt2 (char *b, int a, int c, const char *sep)
	{
		int n = 0; auto num = [&] (int v) { char t[12]; int j = 0; if (v < 0) { b[n++] = '-'; v = -v; } do { t[j++] = (char) ('0' + v % 10); v /= 10; } while (v); while (j) b[n++] = t[--j]; };
		num (a); for (const char *s = sep; *s; s++) b[n++] = *s; num (c);
		b[n++] = ' '; b[n++] = 'p'; b[n++] = 'x'; b[n] = 0;
	}
	void button (int x, bool plus)
	{
		unsigned ink = wk_ink_for (C_BG);
		if (m_hot == (plus ? 2 : 1)) wk_rbox (canvas, x, 3, 20, height - 6, 4, wk_tone (C_BG, 150), wk_tone (C_BG, 138));
		wk_glyph (canvas, plus ? WKG_PLUS : WKG_MINUS, x + 10, height / 2, 8, ink);
	}
};

} // namespace pd

#endif
