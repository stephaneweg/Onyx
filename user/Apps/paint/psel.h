//
// psel.h -- Paint's selection: a rectangle, or a mask of the picture's pixels (0 out, 255 in) made by
// the free-form selection (a lasso: the polygon the pointer drew), the magic wand (the pixels of
// about the same colour as the one clicked: those that touch it, or all of them) or Invert. A new
// selection replaces the old one; with Shift it is added to it, with Alt taken from it. What is
// drawn (strokes, fills, shapes, gradients, adjustments) stays inside the selection; Cut / Copy /
// Delete / a move take its pixels.
//
// The regions (the wand's, the Fill tool's): the pixels whose colour is within a tolerance of the
// clicked one -- the largest difference of its four channels, 0..255 --, joined by their sides
// (contiguous) or anywhere in the picture.
//
// MIT licence (Onyx).
//
#ifndef _paint_psel_h
#define _paint_psel_h

#include "pdoc.h"

namespace pd {

static Rect g_sel;					// the selection's bounds (empty: none)
static unsigned char *g_mask;				// D.w x D.h (0: the whole rectangle g_sel)
enum { SEL_REPLACE, SEL_ADD, SEL_SUB };

static inline bool has_sel () { return !g_sel.empty (); }
// How much (x, y) is selected, 0..255: everything when nothing is.
static inline int sel_at (int x, int y)
{
	if (g_sel.empty ()) return 255;
	if (x < g_sel.x0 || y < g_sel.y0 || x >= g_sel.x1 || y >= g_sel.y1) return 0;
	return g_mask ? g_mask[(unsigned) y * D.w + x] : 255;
}
static void sel_clear () { g_sel = norect (); delete[] g_mask; g_mask = 0; }
static void sel_rect (Rect r) { sel_clear (); r.clip (D.w, D.h); g_sel = r; }

// The bounds of the mask again (none left: no selection).
static void sel_bounds ()
{
	if (!g_mask) return;
	Rect b = norect ();
	for (int y = 0; y < D.h; y++)
	{
		const unsigned char *m = g_mask + (unsigned) y * D.w;
		int x0 = 0; while (x0 < D.w && !m[x0]) x0++;
		if (x0 == D.w) continue;
		int x1 = D.w; while (x1 > x0 && !m[x1 - 1]) x1--;
		b.add (mkrect (x0, y, x1, y + 1));
	}
	if (b.empty ()) { sel_clear (); return; }
	g_sel = b;
}
// The selection as a mask (made from the rectangle when it is one).
static void sel_to_mask ()
{
	if (g_mask) return;
	g_mask = new unsigned char[(unsigned) D.w * D.h];
	for (int i = 0; i < D.w * D.h; i++) g_mask[i] = 0;
	for (int y = g_sel.y0; y < g_sel.y1; y++) for (int x = g_sel.x0; x < g_sel.x1; x++) g_mask[(unsigned) y * D.w + x] = 255;
}
// A new mask m (w x h, its own now) combined into the selection.
static void sel_apply (unsigned char *m, int mode)
{
	if (mode == SEL_REPLACE || g_sel.empty ())
	{
		if (mode == SEL_SUB) { delete[] m; return; }
		sel_clear (); g_mask = m; g_sel = mkrect (0, 0, D.w, D.h); sel_bounds ();
		return;
	}
	sel_to_mask ();
	for (int i = 0; i < D.w * D.h; i++)
		g_mask[i] = mode == SEL_ADD ? (unsigned char) pmax (g_mask[i], m[i]) : (unsigned char) (g_mask[i] * (255 - m[i]) / 255);
	delete[] m;
	g_sel = mkrect (0, 0, D.w, D.h);
	sel_bounds ();
}
static void sel_apply_rect (Rect r, int mode)
{
	r.clip (D.w, D.h);
	if (mode == SEL_REPLACE) { sel_rect (r); return; }
	unsigned char *m = new unsigned char[(unsigned) D.w * D.h];
	for (int i = 0; i < D.w * D.h; i++) m[i] = 0;
	for (int y = r.y0; y < r.y1; y++) for (int x = r.x0; x < r.x1; x++) m[(unsigned) y * D.w + x] = 255;
	sel_apply (m, mode);
}
static void sel_invert ()
{
	if (g_sel.empty ()) { sel_rect (mkrect (0, 0, D.w, D.h)); return; }
	sel_to_mask ();
	for (int i = 0; i < D.w * D.h; i++) g_mask[i] = (unsigned char) (255 - g_mask[i]);
	g_sel = mkrect (0, 0, D.w, D.h);
	sel_bounds ();
}

// A polygon's pixels (their centres inside, even-odd; points in 1/16 px) into a new mask.
static unsigned char *poly_mask (const int *xy, int n)
{
	unsigned char *m = new unsigned char[(unsigned) D.w * D.h];
	for (int i = 0; i < D.w * D.h; i++) m[i] = 0;
	if (n < 3) return m;
	int ymin = xy[1], ymax = xy[1];
	for (int i = 1; i < n; i++) { ymin = pmin (ymin, xy[2 * i + 1]); ymax = pmax (ymax, xy[2 * i + 1]); }
	int py0 = pmax (0, ymin >> 4), py1 = pmin (D.h - 1, (ymax >> 4) + 1);
	int *xs = new int[n + 1];
	for (int py = py0; py <= py1; py++)
	{
		int sy = py * 16 + 8, k = 0;
		for (int i = 0; i < n; i++)
		{
			int j = (i + 1) % n, ya = xy[2 * i + 1], yb = xy[2 * j + 1];
			if (ya == yb || sy < pmin (ya, yb) || sy >= pmax (ya, yb)) continue;
			int x = xy[2 * i] + (int) ((long long) (sy - ya) * (xy[2 * j] - xy[2 * i]) / (yb - ya));
			int q = k++;
			while (q > 0 && xs[q - 1] > x) { xs[q] = xs[q - 1]; q--; }
			xs[q] = x;
		}
		for (int q = 0; q + 1 < k; q += 2)
		{
			int a = pmax (0, (xs[q] - 8 + 15) >> 4), b = pmin (D.w - 1, (xs[q + 1] - 8 - 1) >> 4);
			for (int x = a; x <= b; x++) m[(unsigned) py * D.w + x] = 255;
		}
	}
	delete[] xs;
	return m;
}

// ---- regions of a colour -----------------------------------------------------------------------------------
static inline int coldist (unsigned a, unsigned b)
{
	int d = 0;
	for (int s = 0; s < 32; s += 8) { int k = (int) ((a >> s) & 255) - (int) ((b >> s) & 255); if (k < 0) k = -k; if (k > d) d = k; }
	return d;
}
// The pixels of px (D.w x D.h) within tol of (x, y)'s colour -> a new mask (0 / 255) and its box;
// 0: (x, y) outside. contiguous: joined to it by their sides.
static unsigned char *region (const unsigned *px, int x, int y, int tol, bool contiguous, Rect *box)
{
	*box = norect ();
	if ((unsigned) x >= (unsigned) D.w || (unsigned) y >= (unsigned) D.h) return 0;
	const int W = D.w, H = D.h;
	unsigned seed = px[(unsigned) y * W + x];
	// (fully transparent pixels: one colour, whatever their RGB)
	auto near = [&] (unsigned c) { if (!(c >> 24) && !(seed >> 24)) return true; return coldist (c, seed) <= tol; };
	unsigned char *m = new unsigned char[(unsigned) W * H];
	for (int i = 0; i < W * H; i++) m[i] = 0;
	if (!contiguous)
	{
		for (int yy = 0; yy < H; yy++)
			for (int xx = 0; xx < W; xx++)
				if (near (px[(unsigned) yy * W + xx])) { m[(unsigned) yy * W + xx] = 255; box->add (xx, yy); }
		return m;
	}
	int cap = 4096, sp = 1, *st = new int[cap * 2];
	st[0] = x; st[1] = y;
	while (sp > 0)
	{
		sp--;
		int sx = st[2 * sp], sy = st[2 * sp + 1];
		const unsigned *row = px + (unsigned) sy * W;
		unsigned char *mr = m + (unsigned) sy * W;
		if (mr[sx] || !near (row[sx])) continue;
		int l = sx, r = sx;
		while (l > 0 && !mr[l - 1] && near (row[l - 1])) l--;
		while (r < W - 1 && !mr[r + 1] && near (row[r + 1])) r++;
		for (int i = l; i <= r; i++) mr[i] = 255;
		box->add (mkrect (l, sy, r + 1, sy + 1));
		for (int d = -1; d <= 1; d += 2)
		{
			int ny = sy + d;
			if (ny < 0 || ny >= H) continue;
			const unsigned *nr = px + (unsigned) ny * W; const unsigned char *nm = m + (unsigned) ny * W;
			for (int i = l; i <= r; i++)
			{
				if (nm[i] || !near (nr[i])) continue;
				if (sp == cap) { int *s2 = new int[cap * 4]; for (int k = 0; k < sp * 2; k++) s2[k] = st[k]; delete[] st; st = s2; cap *= 2; }
				st[2 * sp] = i; st[2 * sp + 1] = ny; sp++;
				while (i + 1 <= r && !nm[i + 1] && near (nr[i + 1])) i++;
			}
		}
	}
	delete[] st;
	return m;
}

} // namespace pd

#endif
