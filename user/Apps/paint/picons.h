//
// picons.h -- Paint's icons, 20 x 20, drawn from their geometry (wtk's VPath: anti-aliased, any colour
// of the theme), and the shapes' little pictures for the gallery.
//
// MIT licence (Onyx).
//
#ifndef _paint_picons_h
#define _paint_picons_h

#include "pview.h"

namespace pd {

using namespace wtk;

// ---- icons (20 x 20 at (x, y)) -------------------------------------------------------------------------------
enum { I_PASTE, I_CUT, I_COPY, I_SELECT, I_CROP, I_RESIZE, I_ROTATE, I_PENCIL, I_FILL, I_ERASER, I_PICKER, I_ZOOM,
       I_BRUSH, I_OUTLINE, I_FILLSHAPE, I_SIZE, I_EDITCOL, I_UNDO, I_REDO, I_EYE, I_EYEOFF, I_ADD, I_DUP, I_TRASH,
       I_UP, I_DOWN, I_MERGE, I_POINTER, I_SELSIZE, I_IMGSIZE, I_GRID, I_FIT, I_LASSO, I_WAND, I_TEXT, I_GRADIENT,
       I_FX, I_PROPS };

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
	case I_LASSO:
	{
		int e[64]; int n = 0;
		for (int a = 0; a < 360; a += 15) { e[2 * n] = X + V (10) + V (8) * wk_cos (a) / 16384; e[2 * n + 1] = Y + V (8) - V (5) * wk_sin (a) / 16384; n++; }
		for (int i = 0; i < n; i += 2) { int j = (i + 1) % n; p.line (e[2 * i], e[2 * i + 1], e[2 * j], e[2 * j + 1], 20); }
		L (V (6), V (12), V (5), V (16), 20); L (V (5), V (16), V (8), V (19), 20); p.fill (cv, ink);
		break;
	}
	case I_WAND:
	{
		L (V (3), V (18), V (12), V (9), 40); p.fill (cv, ink);
		p.clear (); int st[20]; for (int i = 0; i < 10; i++) { int r = i & 1 ? V (2) : V (5); st[2 * i] = X + V (14) + r * wk_cos (90 + i * 36) / 16384; st[2 * i + 1] = Y + V (6) - r * wk_sin (90 + i * 36) / 16384; }
		p.poly (st, 10); p.fill (cv, 0xECB428, A);
		break;
	}
	case I_TEXT:
	{
		int a[6] = { X + V (3), Y + V (16), X + V (10), Y + V (2), X + V (17), Y + V (16) };
		p.polyline (a, 3, 36, false); L (V (6), V (11), V (14), V (11), 28); p.fill (cv, ink);
		p.clear (); p.rect (X + V (2), Y + V (18), V (16), 26); p.fill (cv, C_ACCENT, A);
		break;
	}
	case I_GRADIENT:
		for (int i = 0; i < 14; i++) { p.clear (); p.rect (X + V (3 + i), Y + V (4), V (1) + 2, V (12)); p.fill (cv, wk_mix (0x3C6EC8, 0xFAC878, i * 256 / 13), A); }
		p.clear (); { int f[8] = { X + V (3), Y + V (4), X + V (17), Y + V (4), X + V (17), Y + V (16), X + V (3), Y + V (16) }; p.polyline (f, 4, 18, true); } p.fill (cv, ink);
		break;
	case I_FX:
		wk_text_c (cv, x, y, 20, 20, "fx", ink, 2);
		break;
	case I_PROPS:
		for (int i = 0; i < 3; i++) { p.rect (X + V (3), Y + V (4 + i * 5), V (14), 24); p.circle (X + V (6 + i * 4), Y + V (4 + i * 5) + 12, V (2)); }
		p.fill (cv, ink);
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

} // namespace pd

#endif
