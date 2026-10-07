//
// raster.h -- Paint's drawing, to the pixel (no anti-aliasing: a pixel is set or not): a brush's
// stamp (a square, or a disc), Bresenham's lines of stamps, the ellipse in a box (Zingl's
// algorithm: exact for even and odd sizes), polygons filled by their pixels' centres and outlined
// with the stamp, the shapes -- a rectangle, a rounded one, an ellipse, and the polygons inscribed
// in the ellipse of their box (triangle, diamond, pentagon, hexagon, octagon, stars), a right
// triangle, an arrow, a heart --, the flood fill, and the blocks of pixels flipped and turned.
// Integer only (the trigonometry: uikit's uk_sin / uk_cos x 16384).
//
#ifndef _paint_raster_h
#define _paint_raster_h

#ifdef USE_IMAGEKIT
#include "imagekit/imagekit.h"
#endif
#include "pdoc.h"
#include "uikit/vpaint.h"

namespace pd {

// A target: a w x h buffer; what is drawn grows `dirty`.
struct Target { unsigned *px; int w, h; Rect dirty; };
static inline Target target (unsigned *px, int w, int h) { Target t; t.px = px; t.w = w; t.h = h; t.dirty = norect (); return t; }

static inline void plot (Target &t, int x, int y, unsigned c)
{
	if ((unsigned) x >= (unsigned) t.w || (unsigned) y >= (unsigned) t.h) return;
	t.px[(unsigned) y * t.w + x] = c;
	t.dirty.add (x, y);
}
static inline void hspan (Target &t, int x0, int x1, int y, unsigned c)	// [x0, x1]
{
	if ((unsigned) y >= (unsigned) t.h) return;
	x0 = pmax (x0, 0); x1 = pmin (x1, t.w - 1);
	if (x1 < x0) return;
	unsigned *p = t.px + (unsigned) y * t.w;
	for (int x = x0; x <= x1; x++) p[x] = c;
	t.dirty.add (mkrect (x0, y, x1 + 1, y + 1));
}

// The brush's stamp centred on (x, y): size 1 a pixel; a square, or a disc (round).
static void stamp (Target &t, int x, int y, int size, bool round, unsigned c)
{
	if (size <= 1) { plot (t, x, y, c); return; }
	int r0 = -(size / 2), r1 = r0 + size - 1;			// (an even size: one more below / right)
	if (!round) { for (int j = r0; j <= r1; j++) hspan (t, x + r0, x + r1, y + j, c); return; }
	// a disc: the pixels whose centre lies within size / 2 of the stamp's centre
	int d2 = size * size;						// (in half-pixel units: (2r)^2)
	for (int j = r0; j <= r1; j++)
	{
		int cy = 2 * j + 1 - (size & 1 ? 1 : 0) * 1 - (size & 1 ? 0 : 1);	// (the row's centre, x2, from the stamp's)
		int lo = 1, hi = 0;
		for (int i = r0; i <= r1; i++)
		{
			int cx = 2 * i + 1 - (size & 1 ? 1 : 0) - (size & 1 ? 0 : 1);
			if (cx * cx + cy * cy <= d2) { if (lo > hi) lo = hi = i; else hi = i; }
		}
		if (lo <= hi) hspan (t, x + lo, x + hi, y + j, c);
	}
}

// A line of stamps from (x0, y0) to (x1, y1), both ends included.
static void line (Target &t, int x0, int y0, int x1, int y1, int size, bool round, unsigned c)
{
	int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
	int dy = -(y1 > y0 ? y1 - y0 : y0 - y1), sy = y0 < y1 ? 1 : -1, err = dx + dy;
	for (;;)
	{
		stamp (t, x0, y0, size, round, c);
		if (x0 == x1 && y0 == y1) break;
		int e2 = 2 * err;
		if (e2 >= dy) { err += dy; x0 += sx; }
		if (e2 <= dx) { err += dx; y0 += sy; }
	}
}

// The ellipse in the box [x0, x1] x [y0, y1] (Zingl): its outline with the stamp, its inside filled.
static void ellipse (Target &t, int x0, int y0, int x1, int y1, int size, bool outline, unsigned oc, bool fill, unsigned fc)
{
	if (x0 > x1) { int k = x0; x0 = x1; x1 = k; }
	if (y0 > y1) { int k = y0; y0 = y1; y1 = k; }
	for (int pass = fill ? 0 : 1; pass < (outline ? 2 : 1); pass++)
	{
		long long a = x1 - x0, b = y1 - y0, b1 = b & 1;
		long long dx = 4 * (1 - a) * b * b, dy = 4 * (b1 + 1) * a * a;
		long long err = dx + dy + b1 * a * a, e2;
		int X0 = x0, X1 = x1, Y0 = y0 + (int) ((b + 1) / 2), Y1;
		Y1 = Y0 - (int) b1;
		a *= 8 * a; b1 = 8 * b * b;
		do
		{
			if (pass == 0) { hspan (t, X0, X1, Y0, fc); hspan (t, X0, X1, Y1, fc); }
			else { stamp (t, X1, Y0, size, true, oc); stamp (t, X0, Y0, size, true, oc); stamp (t, X0, Y1, size, true, oc); stamp (t, X1, Y1, size, true, oc); }
			e2 = 2 * err;
			if (e2 <= dy) { Y0++; Y1--; err += dy += a; }
			if (e2 >= dx || 2 * err > dy) { X0++; X1--; err += dx += b1; }
		} while (X0 <= X1);
		while (Y0 - Y1 <= y1 - y0 && Y0 <= y1)			// (the tips of a flat ellipse)
		{
			if (pass == 0) { hspan (t, X0 - 1, X1 + 1, Y0, fc); hspan (t, X0 - 1, X1 + 1, Y1, fc); }
			else { stamp (t, X0 - 1, Y0, size, true, oc); stamp (t, X1 + 1, Y0, size, true, oc); stamp (t, X0 - 1, Y1, size, true, oc); stamp (t, X1 + 1, Y1, size, true, oc); }
			Y0++; Y1--;
		}
	}
}

// A polygon (points in 1/16 px, n of them): its pixels whose centre is inside (even-odd) filled.
static void fill_poly (Target &t, const int *xy, int n, unsigned c)
{
	if (n < 3) return;
	int ymin = xy[1], ymax = xy[1];
	for (int i = 1; i < n; i++) { ymin = pmin (ymin, xy[2 * i + 1]); ymax = pmax (ymax, xy[2 * i + 1]); }
	int py0 = pmax (0, ymin >> 4), py1 = pmin (t.h - 1, (ymax >> 4) + 1);
	int *xs = new int[n + 1];
	for (int py = py0; py <= py1; py++)
	{
		int sy = py * 16 + 8, k = 0;					// (the row's pixel centres)
		for (int i = 0; i < n; i++)
		{
			int j = (i + 1) % n, ya = xy[2 * i + 1], yb = xy[2 * j + 1];
			if (ya == yb || sy < pmin (ya, yb) || sy >= pmax (ya, yb)) continue;
			int x = xy[2 * i] + (int) ((long long) (sy - ya) * (xy[2 * j] - xy[2 * i]) / (yb - ya));
			int m = k++;
			while (m > 0 && xs[m - 1] > x) { xs[m] = xs[m - 1]; m--; }
			xs[m] = x;
		}
		for (int m = 0; m + 1 < k; m += 2)
		{
			int a = (xs[m] - 8 + 15) >> 4, b = (xs[m + 1] - 8) >> 4;	// (the centres x*16+8 in [xa, xb])
			if (xs[m + 1] - 8 >= 0 && ((xs[m + 1] - 8) & 15) == 0) b--;	// (a centre on the right edge: out)
			hspan (t, a, b, py, c);
		}
	}
	delete[] xs;
}
// Its outline: lines of stamps between its points (rounded to pixels).
static void stroke_poly (Target &t, const int *xy, int n, bool closed, int size, unsigned c)
{
	for (int i = 0; i + 1 < n + (closed ? 1 : 0); i++)
	{
		int j = (i + 1) % n;
		line (t, (xy[2 * i] + 8) >> 4, (xy[2 * i + 1] + 8) >> 4, (xy[2 * j] + 8) >> 4, (xy[2 * j + 1] + 8) >> 4, size, false, c);
	}
}

// ---- the shapes ------------------------------------------------------------------------------------------
enum { SH_LINE, SH_RECT, SH_RRECT, SH_ELLIPSE, SH_TRIANGLE, SH_RTRIANGLE, SH_DIAMOND, SH_PENTAGON, SH_HEXAGON,
       SH_OCTAGON, SH_STAR4, SH_STAR5, SH_STAR6, SH_ARROW, SH_HEART, SH_COUNT };
static const char *const SHAPE_NAMES[SH_COUNT] = { "Line", "Rectangle", "Rounded rectangle", "Ellipse", "Triangle",
	"Right triangle", "Diamond", "Pentagon", "Hexagon", "Octagon", "Four-point star", "Five-point star",
	"Six-point star", "Arrow", "Heart" };

// The outline of shape k in the box [x0, x1] x [y0, y1] (pixels, inclusive) as a polygon (1/16 px,
// the pixels' centres), into xy (room for 256 points); its point count.
static int shape_points (int k, int x0, int y0, int x1, int y1, int *xy)
{
	int X0 = x0 * 16 + 8, Y0 = y0 * 16 + 8, X1 = x1 * 16 + 8, Y1 = y1 * 16 + 8;
	int cx = (X0 + X1) / 2, cy = (Y0 + Y1) / 2, rx = (X1 - X0) / 2, ry = (Y1 - Y0) / 2;
	int n = 0;
	auto pt = [&] (int x, int y) { xy[2 * n] = x; xy[2 * n + 1] = y; n++; };
	auto regular = [&] (int sides, int start, int inner) {		// (inscribed in the box's ellipse; inner: a star's
		for (int i = 0; i < sides * (inner ? 2 : 1); i++)		//  inner radius, per 1000)
		{
			int d = start + 360 * i / (sides * (inner ? 2 : 1));
			int r = inner && (i & 1) ? inner : 1000;
			pt (cx + (int) ((long long) rx * r / 1000 * uikit::uk_cos (d) / 16384), cy - (int) ((long long) ry * r / 1000 * uikit::uk_sin (d) / 16384));
		}
	};
	switch (k)
	{
	case SH_RECT: pt (X0, Y0); pt (X1, Y0); pt (X1, Y1); pt (X0, Y1); break;
	case SH_RRECT:
	{
		int r = pmin (X1 - X0, Y1 - Y0) / 5;
		int ccx[4] = { X1 - r, X0 + r, X0 + r, X1 - r }, ccy[4] = { Y0 + r, Y0 + r, Y1 - r, Y1 - r };
		for (int q = 0; q < 4; q++)
			for (int i = 0; i <= 8; i++) { int d = q * 90 + 90 * i / 8; pt (ccx[q] + r * uikit::uk_cos (d) / 16384, ccy[q] - r * uikit::uk_sin (d) / 16384); }
		break;
	}
	case SH_TRIANGLE: pt (cx, Y0); pt (X1, Y1); pt (X0, Y1); break;
	case SH_RTRIANGLE: pt (X0, Y0); pt (X1, Y1); pt (X0, Y1); break;
	case SH_DIAMOND: pt (cx, Y0); pt (X1, cy); pt (cx, Y1); pt (X0, cy); break;
	case SH_PENTAGON: regular (5, 90, 0); break;
	case SH_HEXAGON: regular (6, 0, 0); break;
	case SH_OCTAGON: regular (8, 22, 0); break;
	case SH_STAR4: regular (4, 90, 400); break;
	case SH_STAR5: regular (5, 90, 382); break;
	case SH_STAR6: regular (6, 90, 520); break;
	case SH_ARROW:
	{
		int hx = X1 - (X1 - X0) * 2 / 5, t0 = Y0 + (Y1 - Y0) / 4, t1 = Y1 - (Y1 - Y0) / 4;
		pt (X0, t0); pt (hx, t0); pt (hx, Y0); pt (X1, cy); pt (hx, Y1); pt (hx, t1); pt (X0, t1);
		break;
	}
	case SH_HEART:
	{
		// x = 16 sin^3 t, y = 13 cos t - 5 cos 2t - 2 cos 3t - cos 4t (the classic curve), fitted to the box
		const int N = 72;
		for (int i = 0; i < N; i++)
		{
			int d = 360 * i / N;
			long long s = uikit::uk_sin (d), c = uikit::uk_cos (d);
			long long hx = 16 * s * s / 16384 * s / 16384;			// (x 16384)
			long long hy = 13 * c - 5 * uikit::uk_cos (2 * d) - 2 * uikit::uk_cos (3 * d) - uikit::uk_cos (4 * d);
			// hx in [-16, 16] x 16384, hy in about [-17, 12] x 16384: to the box
			pt (cx + (int) (hx * rx / (16 * 16384)), Y0 + (int) ((12 * 16384 - hy) * (Y1 - Y0) / (29 * 16384)));
		}
		break;
	}
	default: break;
	}
	return n;
}

// Shape k in the box (x0, y0) - (x1, y1): the outline (size, colour oc) and / or the inside (fc).
static void shape (Target &t, int k, int x0, int y0, int x1, int y1, int size, bool outline, unsigned oc, bool fill, unsigned fc)
{
	if (k == SH_LINE) { line (t, x0, y0, x1, y1, size, true, oc); return; }
	if (x0 > x1) { int q = x0; x0 = x1; x1 = q; }
	if (y0 > y1) { int q = y0; y0 = y1; y1 = q; }
	if (k == SH_ELLIPSE) { ellipse (t, x0, y0, x1, y1, size, outline, oc, fill, fc); return; }
	if (k == SH_RECT)
	{
		if (fill) for (int y = y0; y <= y1; y++) hspan (t, x0, x1, y, fc);
		if (outline)								// (the thickness inside the box)
			for (int i = 0; i < size && x0 + i <= x1 - i && y0 + i <= y1 - i; i++)
			{
				hspan (t, x0 + i, x1 - i, y0 + i, oc); hspan (t, x0 + i, x1 - i, y1 - i, oc);
				for (int y = y0 + i; y <= y1 - i; y++) { plot (t, x0 + i, y, oc); plot (t, x1 - i, y, oc); }
			}
		return;
	}
	int xy[512];
	int n = shape_points (k, x0, y0, x1, y1, xy);
	if (fill) fill_poly (t, xy, n, fc);
	if (outline) stroke_poly (t, xy, n, true, size, oc);
}

// ---- the flood fill ----------------------------------------------------------------------------------------
// The region of (x, y)'s colour (4-connected) painted c; its box (to keep for undo first: `before`
// is called with it before anything changes).
static Rect flood (Target &t, int x, int y, unsigned c, void (*before) (Rect))
{
	if ((unsigned) x >= (unsigned) t.w || (unsigned) y >= (unsigned) t.h) return norect ();
	unsigned old = t.px[(unsigned) y * t.w + x];
	if (old == c) return norect ();
	// first the region (a mask), then its box kept, then the paint
	unsigned char *m = new unsigned char[(unsigned) t.w * t.h];
	for (int i = 0; i < t.w * t.h; i++) m[i] = 0;
	int cap = 4096, sp = 0, *st = new int[cap * 2];
	Rect box = norect ();
	st[0] = x; st[1] = y; sp = 1;
	while (sp > 0)
	{
		sp--;
		int sx = st[2 * sp], sy = st[2 * sp + 1];
		unsigned *row = t.px + (unsigned) sy * t.w;
		unsigned char *mr = m + (unsigned) sy * t.w;
		if (mr[sx] || row[sx] != old) continue;
		int l = sx, r = sx;
		while (l > 0 && !mr[l - 1] && row[l - 1] == old) l--;
		while (r < t.w - 1 && !mr[r + 1] && row[r + 1] == old) r++;
		for (int i = l; i <= r; i++) mr[i] = 1;
		box.add (mkrect (l, sy, r + 1, sy + 1));
		for (int d = -1; d <= 1; d += 2)				// (the rows above and below: a seed a run)
		{
			int ny = sy + d;
			if (ny < 0 || ny >= t.h) continue;
			unsigned *nr = t.px + (unsigned) ny * t.w; unsigned char *nm = m + (unsigned) ny * t.w;
			for (int i = l; i <= r; i++)
			{
				if (nm[i] || nr[i] != old) continue;
				if (sp == cap) { int *s2 = new int[cap * 4]; for (int k = 0; k < sp * 2; k++) s2[k] = st[k]; delete[] st; st = s2; cap *= 2; }
				st[2 * sp] = i; st[2 * sp + 1] = ny; sp++;
				while (i + 1 <= r && nr[i + 1] == old && !nm[i + 1]) i++;
			}
		}
	}
	if (before) before (box);
	for (int yy = box.y0; yy < box.y1; yy++)
		for (int xx = box.x0; xx < box.x1; xx++) if (m[(unsigned) yy * t.w + xx]) t.px[(unsigned) yy * t.w + xx] = c;
	t.dirty.add (box);
	delete[] m; delete[] st;
	return box;
}

// ---- blocks of pixels --------------------------------------------------------------------------------------
static void flip_h (unsigned *p, int w, int h) { for (int y = 0; y < h; y++) for (int a = 0, b = w - 1; a < b; a++, b--) { unsigned t = p[y * w + a]; p[y * w + a] = p[y * w + b]; p[y * w + b] = t; } }
static void flip_v (unsigned *p, int w, int h) { for (int a = 0, b = h - 1; a < b; a++, b--) for (int x = 0; x < w; x++) { unsigned t = p[a * w + x]; p[a * w + x] = p[b * w + x]; p[b * w + x] = t; } }
// Turned a quarter (clockwise: cw), a new buffer h x w.
static unsigned *rotate (const unsigned *p, int w, int h, bool cw)
{
	unsigned *o = new unsigned[(unsigned) w * h];
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
		{
			int nx = cw ? h - 1 - y : y, ny = cw ? x : w - 1 - x;	// (the new picture is h wide)
			o[(unsigned) ny * h + nx] = p[(unsigned) y * w + x];
		}
	return o;
}
// Scaled to nw x nh (smooth: averaged when it shrinks, bilinear when it grows; else the nearest pixel).
static unsigned *scale (const unsigned *p, int w, int h, int nw, int nh, bool smooth)
{
	unsigned *o = new unsigned[(unsigned) nw * nh];
#ifdef USE_IMAGEKIT						// (ImageKit's resize: right in alpha, bilinear when it grows)
	if (smooth) { ik_scale (p, w, h, w, 0, 0, w, h, o, nw, nw, nh); return o; }
#endif
	for (int y = 0; y < nh; y++)
		for (int x = 0; x < nw; x++)
		{
			if (!smooth) { o[(unsigned) y * nw + x] = p[(unsigned) ((long long) y * h / nh) * w + (int) ((long long) x * w / nw)]; continue; }
			int sx0 = (int) ((long long) x * w / nw), sx1 = pmax (sx0 + 1, (int) ((long long) (x + 1) * w / nw));
			int sy0 = (int) ((long long) y * h / nh), sy1 = pmax (sy0 + 1, (int) ((long long) (y + 1) * h / nh));
			unsigned a = 0, r = 0, g = 0, b = 0, n = 0;
			for (int yy = sy0; yy < sy1 && yy < h; yy++)
				for (int xx = sx0; xx < sx1 && xx < w; xx++)
				{
					unsigned c = p[(unsigned) yy * w + xx], ca = c >> 24;
					a += ca; r += ((c >> 16) & 255) * ca; g += ((c >> 8) & 255) * ca; b += (c & 255) * ca; n++;
				}
			if (!n || !a) { o[(unsigned) y * nw + x] = 0; continue; }
			o[(unsigned) y * nw + x] = (a / n) << 24 | (r / a) << 16 | (g / a) << 8 | (b / a);
		}
	return o;
}

} // namespace pd

#endif
