//
// render.h -- a slide drawn: every object rendered into a layer of its own (0xAARRGGBB premultiplied:
// its shape filled and outlined -- uikit/vpaint.h's anti-aliased paths --, its picture, its table, its
// chart, its text), the layers kept as textures and composited by the GPU (user/Libs/gpucomp: each layer
// moved, rotated, faded by its matrix and opacity -- an object dragged, a slide show's effects cost no
// new drawing); the CPU's path when there is no GPU. The background, the master's decorations, the
// footer and the slide's number are layers too.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef _slides_render_h
#define _slides_render_h

#include "text.h"
#include "uikit/uikit.h"
#include "gpucomp/gpucomp.h"

namespace sl {

using namespace uikit;

// ---- the shapes' outlines -----------------------------------------------------------------------------------------
// The shape's outline in px (x, y, w, h: its box), as polygons into p (1/16 px). closed: a filled shape.
static inline int Q (float v) { return (int) (v * 16.0f + (v >= 0 ? 0.5f : -0.5f)); }
struct PolyB { int xy[2 * 96]; int n = 0; void add (float x, float y) { if (n < 96) { xy[2 * n] = Q (x); xy[2 * n + 1] = Q (y); n++; } } };
static void reg_poly (PolyB &b, float cx, float cy, float rx, float ry, int n, float rot0, float inner = 0)
{
	int m = inner > 0 ? 2 * n : n;
	for (int i = 0; i < m; i++)
	{
		float a = rot0 + (float) i * 6.2831853f / m;
		float k = inner > 0 && (i & 1) ? inner : 1.0f;
		b.add (cx + rx * k * cosf (a), cy + ry * k * sinf (a));
	}
}
// The outline of a shape as one polygon (the curved ones approximated): false for the ones VPath draws itself.
static bool shape_poly (int sh, float x, float y, float w, float h, PolyB &b)
{
	b.n = 0;
	float cx = x + w / 2, cy = y + h / 2, r = x + w, B = y + h;
	switch (sh)
	{
	case SH_RECT: b.add (x, y); b.add (r, y); b.add (r, B); b.add (x, B); return true;
	case SH_TRIANGLE: b.add (cx, y); b.add (r, B); b.add (x, B); return true;
	case SH_RTRIANGLE: b.add (x, y); b.add (r, B); b.add (x, B); return true;
	case SH_DIAMOND: b.add (cx, y); b.add (r, cy); b.add (cx, B); b.add (x, cy); return true;
	case SH_PENTAGON: reg_poly (b, cx, cy + h * 0.05f, w / 2, h * 0.53f, 5, -1.5707963f); return true;
	case SH_HEXAGON: { float k = w * 0.25f; b.add (x + k, y); b.add (r - k, y); b.add (r, cy); b.add (r - k, B); b.add (x + k, B); b.add (x, cy); return true; }
	case SH_OCTAGON: { float k = fminf (w, h) * 0.29f; b.add (x + k, y); b.add (r - k, y); b.add (r, y + k); b.add (r, B - k); b.add (r - k, B); b.add (x + k, B); b.add (x, B - k); b.add (x, y + k); return true; }
	case SH_STAR5: reg_poly (b, cx, cy + h * 0.05f, w / 2, h * 0.53f, 5, -1.5707963f, 0.42f); return true;
	case SH_STAR4: reg_poly (b, cx, cy, w / 2, h / 2, 4, -1.5707963f, 0.32f); return true;
	case SH_ARROW_R: case SH_ARROW_L: case SH_ARROW_U: case SH_ARROW_D:
	{
		static const float P[7][2] = { { 0, 0.3f }, { 0.6f, 0.3f }, { 0.6f, 0 }, { 1, 0.5f }, { 0.6f, 1 }, { 0.6f, 0.7f }, { 0, 0.7f } };
		for (int i = 0; i < 7; i++)
		{
			float u = P[i][0], v = P[i][1];
			if (sh == SH_ARROW_L) u = 1 - u;
			if (sh == SH_ARROW_U) { float t = u; u = v; v = 1 - t; }
			if (sh == SH_ARROW_D) { float t = u; u = v; v = t; }
			b.add (x + u * w, y + v * h);
		}
		return true;
	}
	case SH_ARROW_LR:
	{
		static const float P[10][2] = { { 0, 0.5f }, { 0.25f, 0 }, { 0.25f, 0.3f }, { 0.75f, 0.3f }, { 0.75f, 0 }, { 1, 0.5f }, { 0.75f, 1 }, { 0.75f, 0.7f }, { 0.25f, 0.7f }, { 0.25f, 1 } };
		for (int i = 0; i < 10; i++) b.add (x + P[i][0] * w, y + P[i][1] * h);
		return true;
	}
	case SH_CHEVRON: { float k = fminf (w * 0.4f, h * 0.5f); b.add (x, y); b.add (r - k, y); b.add (r, cy); b.add (r - k, B); b.add (x, B); b.add (x + k, cy); return true; }
	case SH_HOMEPLATE: { float k = fminf (w * 0.4f, h * 0.5f); b.add (x, y); b.add (r - k, y); b.add (r, cy); b.add (r - k, B); b.add (x, B); return true; }
	case SH_PLUS: { float k = fminf (w, h) * 0.3f; b.add (x + k, y); b.add (r - k, y); b.add (r - k, y + k); b.add (r, y + k); b.add (r, B - k); b.add (r - k, B - k); b.add (r - k, B); b.add (x + k, B); b.add (x + k, B - k); b.add (x, B - k); b.add (x, y + k); b.add (x + k, y + k); return true; }
	case SH_PARALLELOGRAM: { float k = w * 0.22f; b.add (x + k, y); b.add (r, y); b.add (r - k, B); b.add (x, B); return true; }
	case SH_TRAPEZOID: { float k = w * 0.2f; b.add (x + k, y); b.add (r - k, y); b.add (r, B); b.add (x, B); return true; }
	case SH_CALLOUT:
	{
		float bb = y + h * 0.78f, rr = fminf (w, h) * 0.1f;
		// a rounded box with a tail (the corners cut by short chords)
		b.add (x + rr, y); b.add (r - rr, y); b.add (r, y + rr); b.add (r, bb - rr); b.add (r - rr, bb);
		b.add (x + w * 0.38f, bb); b.add (x + w * 0.16f, B); b.add (x + w * 0.22f, bb);
		b.add (x + rr, bb); b.add (x, bb - rr); b.add (x, y + rr);
		return true;
	}
	case SH_HEART:
	{
		for (int i = 0; i <= 40; i++)
		{
			float t = (float) i / 40 * 6.2831853f;
			float hx = 16 * powf (sinf (t), 3), hy = 13 * cosf (t) - 5 * cosf (2 * t) - 2 * cosf (3 * t) - cosf (4 * t);
			b.add (cx + hx / 34.0f * w, y + h * 0.42f - hy / 30.0f * h);
		}
		return true;
	}
	case SH_FLOW_DOC:
	{
		b.add (x, y); b.add (r, y); b.add (r, y + h * 0.85f);
		for (int i = 0; i <= 16; i++) { float t = (float) i / 16; b.add (r - t * w, y + h * 0.85f + sinf (t * 6.2831853f) * h * 0.08f); }
		return true;
	}
	default: return false;
	}
}
// The shape's outline added to a path (filled when fill, else its stroke `lw` px wide).
static void shape_path (VPath &p, int sh, float x, float y, float w, float h, int radiusPm, float lw, bool fill)
{
	PolyB b;
	if (sh == SH_ELLIPSE || sh == SH_DONUT)
	{
		if (fill) { p.ellipse (Q (x + w / 2), Q (y + h / 2), Q (w / 2), Q (h / 2)); return; }
		reg_poly (b, x + w / 2, y + h / 2, w / 2, h / 2, 72, 0);
		p.polyline (b.xy, b.n, Q (lw), true);
		return;
	}
	if (sh == SH_ROUND || sh == SH_FLOW_TERM)
	{
		float rr = fminf (w, h) * (sh == SH_FLOW_TERM ? 0.5f : radiusPm / 1000.0f);
		if (fill) { p.rrect (Q (x), Q (y), Q (w), Q (h), Q (rr)); return; }
		// the stroke: the corners' arcs and the sides
		b.n = 0;
		for (int c = 0; c < 4; c++)
		{
			float ccx = c == 0 || c == 3 ? x + rr : x + w - rr, ccy = c < 2 ? y + rr : y + h - rr;
			float a0 = (float) (c == 0 ? 180 : c == 1 ? 270 : c == 2 ? 0 : 90) * 0.01745329f;
			for (int k = 0; k <= 8; k++) { float a = a0 + k * 0.19634954f; b.add (ccx + rr * cosf (a), ccy + rr * sinf (a)); }
		}
		p.polyline (b.xy, b.n, Q (lw), true);
		return;
	}
	if (sh == SH_CLOUD)
	{
		static const float C[7][3] = { { 0.22f, 0.55f, 0.2f }, { 0.36f, 0.32f, 0.22f }, { 0.6f, 0.28f, 0.24f }, { 0.8f, 0.45f, 0.2f },
					       { 0.74f, 0.7f, 0.2f }, { 0.48f, 0.74f, 0.22f }, { 0.5f, 0.52f, 0.25f } };
		for (int i = 0; i < 7; i++)
		{
			if (fill) p.ellipse (Q (x + C[i][0] * w), Q (y + C[i][1] * h), Q (C[i][2] * w), Q (C[i][2] * h * 1.1f));
			else if (i < 6) { b.n = 0; reg_poly (b, x + C[i][0] * w, y + C[i][1] * h, C[i][2] * w, C[i][2] * h * 1.1f, 36, 0); p.polyline (b.xy, b.n, Q (lw), true); }
		}
		return;
	}
	if (sh == SH_CAN)
	{
		float e = h * 0.12f;
		if (fill) { p.rect (Q (x), Q (y + e), Q (w), Q (h - 2 * e)); p.ellipse (Q (x + w / 2), Q (y + e), Q (w / 2), Q (e)); p.ellipse (Q (x + w / 2), Q (y + h - e), Q (w / 2), Q (e)); return; }
		b.n = 0; reg_poly (b, x + w / 2, y + e, w / 2, e, 48, 0); p.polyline (b.xy, b.n, Q (lw), true);
		p.line (Q (x), Q (y + e), Q (x), Q (y + h - e), Q (lw)); p.line (Q (x + w), Q (y + e), Q (x + w), Q (y + h - e), Q (lw));
		b.n = 0; for (int k = 0; k <= 24; k++) { float a = k * 3.14159265f / 24; b.add (x + w / 2 + w / 2 * cosf (a), y + h - e + e * sinf (a)); }
		p.polyline (b.xy, b.n, Q (lw), false);
		return;
	}
	if (!shape_poly (sh, x, y, w, h, b)) shape_poly (SH_RECT, x, y, w, h, b);
	if (fill) p.poly (b.xy, b.n); else p.polyline (b.xy, b.n, Q (lw), true);
}

// ---- a layer's buffer ---------------------------------------------------------------------------------------------
struct Layer
{
	unsigned *px; int w, h;		// premultiplied
	float ox, oy;			// its top-left, px from the slide's top-left (unrotated)
	float cx, cy;			// the rotation's centre (px, the slide's)
	int rot; bool flipH, flipV;
	Layer () : px (0), w (0), h (0), ox (0), oy (0), cx (0), cy (0), rot (0), flipH (false), flipV (false) {}
	void alloc (int W, int H) { free (px); w = W < 1 ? 1 : W; h = H < 1 ? 1 : H; px = (unsigned *) calloc ((size_t) w * h, 4); }
	void release () { free (px); px = 0; }
	Surf surf () { Surf s; s.px = px; s.w = w; s.h = h; s.stride = w; s.pm = true; s.clip_all (); return s; }
};

// A path's coverage (drawn by VPath in uikit's alpha mode on a scratch canvas) blended into a layer in a colour,
// or a gradient (g0 -> g1 along angle, over the box bx, by, bw, bh), at opacity alpha.
static unsigned *g_scratch; static int g_scratchN;
static void path_into (Layer &L, VPath &p, const Fill &f, const Deck &d, float bx, float by, float bw, float bh, int dx = 0, int dy = 0, unsigned shadowC = 0xFF000000u, int blur = 0)
{
	int n = L.w * L.h;
	if (n > g_scratchN) { free (g_scratch); g_scratch = (unsigned *) malloc ((size_t) n * 4); g_scratchN = n; }
	for (int i = 0; i < n; i++) g_scratch[i] = 0xFF000000u;
	Canvas cv; cv.adopt (g_scratch, L.w, L.h);
	uk_paint_alpha (true);
	p.fill (cv, 0x000000, 255, dx, dy);
	uk_paint_alpha (false);
	if (blur > 0)						// (a shadow: a box blur of the coverage, two passes)
	{
		unsigned char *a = (unsigned char *) malloc ((size_t) n), *t = (unsigned char *) malloc ((size_t) n);
		for (int i = 0; i < n; i++) a[i] = (unsigned char) (255 - (g_scratch[i] >> 24));
		for (int pass = 0; pass < 2; pass++)
		{
			for (int y = 0; y < L.h; y++) for (int x = 0; x < L.w; x++)
			{
				int s = 0, c = 0;
				for (int k = -blur; k <= blur; k++) { int xx = x + k; if (xx >= 0 && xx < L.w) { s += a[y * L.w + xx]; c++; } }
				t[y * L.w + x] = (unsigned char) (s / (c ? c : 1));
			}
			for (int y = 0; y < L.h; y++) for (int x = 0; x < L.w; x++)
			{
				int s = 0, c = 0;
				for (int k = -blur; k <= blur; k++) { int yy = y + k; if (yy >= 0 && yy < L.h) { s += t[yy * L.w + x]; c++; } }
				a[y * L.w + x] = (unsigned char) (s / (c ? c : 1));
			}
		}
		for (int i = 0; i < n; i++) g_scratch[i] = (unsigned) (255 - a[i]) << 24;
		free (a); free (t);
	}
	bool grad = f.type == FILL_GRADIENT && shadowC == 0xFF000000u;
	unsigned c1 = shadowC != 0xFF000000u ? shadowC : d.rgb (f.c1), c2 = d.rgb (f.c2);
	float ca = cosf (f.angle * 0.01745329f), sa = sinf (f.angle * 0.01745329f);
	float span = fabsf (bw * ca) + fabsf (bh * sa); if (span < 1) span = 1;
	float gx0 = bx + bw / 2 - ca * span / 2, gy0 = by + bh / 2 - sa * span / 2;
	for (int y = 0; y < L.h; y++)
	{
		unsigned *row = L.px + y * L.w;
		for (int x = 0; x < L.w; x++)
		{
			int a = 255 - (int) (g_scratch[y * L.w + x] >> 24);
			if (!a) continue;
			unsigned c = c1;
			if (grad)
			{
				float t = ((x + 0.5f - gx0) * ca + (y + 0.5f - gy0) * sa) / span;
				int k = (int) (iclamp ((int) (t * 256), 0, 256));
				c = uk_mix (c1, c2, k);
			}
			blend_pm (row[x], c, a * f.alpha / 255);
		}
	}
}

// ---- the arrow heads of a line -------------------------------------------------------------------------------------
static void arrow_head (VPath &p, float x, float y, float ang, int kind, float lw)
{
	float len = fmaxf (lw * 4.5f, 6), half = fmaxf (lw * 2.4f, 3.5f);
	int deg = (int) (ang * 57.29578f);
	if (kind == AH_ARROW) p.arrowHead (Q (x), Q (y), -deg, Q (len), Q (half));
	else if (kind == AH_OPEN)
	{
		float a1 = ang + 2.6f, a2 = ang - 2.6f;
		p.line (Q (x), Q (y), Q (x + len * cosf (a1)), Q (y + len * sinf (a1)), Q (lw));
		p.line (Q (x), Q (y), Q (x + len * cosf (a2)), Q (y + len * sinf (a2)), Q (lw));
	}
	else if (kind == AH_DOT) p.circle (Q (x), Q (y), Q (half));
}

// ---- pictures -------------------------------------------------------------------------------------------------------
// The picture (cropped) into the box x, y, w, h of the layer, bilinear (downscaled: averaged over the source).
static void picture_into (Layer &L, const Picture &pc, const short *crop, float x, float y, float w, float h, int alpha, VPath *mask)
{
	float sx0 = pc.w * crop[0] / 1000.0f, sy0 = pc.h * crop[1] / 1000.0f;
	float sw = pc.w * (1000 - crop[0] - crop[2]) / 1000.0f, sh = pc.h * (1000 - crop[1] - crop[3]) / 1000.0f;
	if (sw < 1 || sh < 1) return;
	int x0 = imax (0, (int) x), y0 = imax (0, (int) y), x1 = imin (L.w, (int) ceilf (x + w)), y1 = imin (L.h, (int) ceilf (y + h));
	float kx = sw / w, ky = sh / h;
	int box = kx > 1.5f ? (int) kx : 1;			// (a big shrink: a few samples averaged)
	unsigned char *m = 0;
	if (mask)						// (a rounded picture: the mask's coverage)
	{
		int n = L.w * L.h;
		if (n > g_scratchN) { free (g_scratch); g_scratch = (unsigned *) malloc ((size_t) n * 4); g_scratchN = n; }
		for (int i = 0; i < n; i++) g_scratch[i] = 0xFF000000u;
		Canvas cv; cv.adopt (g_scratch, L.w, L.h);
		uk_paint_alpha (true); mask->fill (cv, 0); uk_paint_alpha (false);
		m = (unsigned char *) g_scratch;		// (read through the top byte below)
	}
	for (int j = y0; j < y1; j++)
	{
		float vy = sy0 + (j + 0.5f - y) * ky - 0.5f;
		for (int i = x0; i < x1; i++)
		{
			float ux = sx0 + (i + 0.5f - x) * kx - 0.5f;
			unsigned r = 0, g = 0, b = 0, a = 0;
			int ns = box > 1 ? (box > 4 ? 4 : box) : 1;
			for (int sj = 0; sj < ns; sj++) for (int si = 0; si < ns; si++)
			{
				float uu = ux + (ns > 1 ? (si - (ns - 1) / 2.0f) * kx / ns : 0), vv = vy + (ns > 1 ? (sj - (ns - 1) / 2.0f) * ky / ns : 0);
				int ix = (int) floorf (uu), iy = (int) floorf (vv);
				float fx = uu - ix, fy = vv - iy;
				int ax = iclamp (ix, 0, pc.w - 1), bx2 = iclamp (ix + 1, 0, pc.w - 1), ay = iclamp (iy, 0, pc.h - 1), by2 = iclamp (iy + 1, 0, pc.h - 1);
				unsigned p00 = pc.px[ay * pc.w + ax], p10 = pc.px[ay * pc.w + bx2], p01 = pc.px[by2 * pc.w + ax], p11 = pc.px[by2 * pc.w + bx2];
				int w00 = (int) ((1 - fx) * (1 - fy) * 256), w10 = (int) (fx * (1 - fy) * 256), w01 = (int) ((1 - fx) * fy * 256), w11 = 256 - w00 - w10 - w01;
				for (int k = 0; k < 4; k++)
				{
					unsigned q = k == 0 ? p00 : k == 1 ? p10 : k == 2 ? p01 : p11; int wgt = k == 0 ? w00 : k == 1 ? w10 : k == 2 ? w01 : w11;
					unsigned qa = q >> 24;
					a += qa * wgt; r += ((q >> 16) & 255) * qa / 255 * wgt; g += ((q >> 8) & 255) * qa / 255 * wgt; b += (q & 255) * qa / 255 * wgt;
				}
			}
			unsigned div = 256u * ns * ns;
			a /= div; r /= div; g /= div; b /= div;
			unsigned cov = (unsigned) alpha;
			if (m) cov = cov * (255 - (g_scratch[j * L.w + i] >> 24)) / 255;
			a = a * cov / 255; r = r * cov / 255; g = g * cov / 255; b = b * cov / 255;
			unsigned &d = L.px[j * L.w + i];
			unsigned ia = 255 - a;
			unsigned dA = (d >> 24) * ia / 255 + a, dR = ((d >> 16) & 255) * ia / 255 + r, dG = ((d >> 8) & 255) * ia / 255 + g, dB = (d & 255) * ia / 255 + b;
			d = dA << 24 | (dR > 255 ? 255 : dR) << 16 | (dG > 255 ? 255 : dG) << 8 | (dB > 255 ? 255 : dB);
		}
	}
}

// ---- a table ---------------------------------------------------------------------------------------------------------
// Its rows' heights: each at least its text's (layouts at the table's own scale).
static void table_heights (const Deck &d, Object &o, int *rh)
{
	Table &t = *o.tbl;
	for (int r = 0; r < t.rows; r++)
	{
		rh[r] = t.rowH[r];
		for (int c = 0; c < t.cols; c++)
		{
			Object cell; cell.w = t.colW[c]; cell.h = t.rowH[r]; cell.tb.copy_from (t.at (r, c));
			TextLayout L; layout_text (d, cell.tb, PH_NONE, inner_w (cell), 1000, L);
			int need = (int) (L.height + cell.tb.inset[1] + cell.tb.inset[3]) + 20;
			if (need > rh[r]) rh[r] = need;
		}
	}
}
static void table_fit (const Deck &d, Object &o)	// the object's height follows its rows
{
	if (!o.tbl) return;
	int rh[64], sum = 0, sw = 0;
	table_heights (d, o, rh);
	for (int r = 0; r < o.tbl->rows && r < 64; r++) sum += rh[r];
	for (int c = 0; c < o.tbl->cols; c++) sw += o.tbl->colW[c];
	o.h = sum; o.w = sw;
}
static void table_into (const Deck &d, Object &o, Layer &L, float pad, float sc, int alpha)
{
	Table &t = *o.tbl;
	int rh[64]; table_heights (d, o, rh);
	unsigned acc = d.rgb (THEME | TC_ACC1), band = d.rgb (THEME | TC_LT2), grid = uk_mix (d.rgb (THEME | TC_LT2), 0x000000, 40);
	Surf s = L.surf ();
	float y = pad;
	for (int r = 0; r < t.rows && r < 64; r++)
	{
		float x = pad;
		for (int c = 0; c < t.cols; c++)
		{
			float w = t.colW[c] * sc, h = rh[r] * sc;
			const Fill &cf = t.cfill[r * t.cols + c];
			unsigned bg = 0xFFFFFF;
			if (cf.type == FILL_SOLID) bg = d.rgb (cf.c1);
			else if (cf.type == FILL_INHERIT) bg = t.header && r == 0 ? acc : t.banded && (r & 1) == (t.header ? 0 : 1) ? band : 0xFFFFFF;
			if (cf.type != FILL_NONE) surf_rect (s, x, y, w, h, bg, alpha);
			Object cell; cell.w = t.colW[c]; cell.h = rh[r]; cell.tb.copy_from (t.at (r, c)); cell.tb.anchor = AN_MIDDLE;
			if (t.header && r == 0)
				for (int i = 0; i < cell.tb.p.n; i++) { Para *q = cell.tb.p[i]; for (int k = 0; k <= q->len; k++) { CharFmt &f = k < q->len ? q->cf[k] : q->end; if (f.color == AUTO) f.color = THEME | TC_LT1; if (!(f.set & CF_BOLD)) { f.flags |= CF_BOLD; f.set |= CF_BOLD; } } }
			TextLayout tl; layout_text (d, cell.tb, PH_NONE, inner_w (cell), 1000, tl);
			draw_text (d, cell, tl, s, x, y, sc, 0, alpha);
			x += w;
		}
		y += rh[r] * sc;
	}
	// the lines: under each row, the outline
	y = pad;
	float tw = 0; for (int c = 0; c < t.cols; c++) tw += t.colW[c] * sc;
	for (int r = 0; r < t.rows && r < 64; r++) { y += rh[r] * sc; if (r + 1 < t.rows) surf_rect (s, pad, y - 0.5f, tw, fmaxf (1, sc * 18), grid, alpha); }
	surf_rect (s, pad, y - fmaxf (1, sc * 40), tw, fmaxf (1.5f, sc * 40), acc, alpha);
}

// ---- a chart ----------------------------------------------------------------------------------------------------------
static const unsigned SERIES_COL[8] = { THEME | TC_ACC1, THEME | TC_ACC2, THEME | TC_ACC3, THEME | TC_ACC4, THEME | TC_ACC5, THEME | TC_ACC6, 0x7F7F7F, 0x2F2F2F };
static void chart_text (const Deck &d, Surf &s, float x, float y, const char *t, int pt10, float sc, unsigned col, int align, bool bold, int alpha)
{
	fnt::Font *f = fnt::get (family_of (d.theme.minor), bold ? fnt::BOLD : 0, (int) (pt10 * 3.5277778f * sc * 64));
	if (!f) return;
	float w = 0; for (const char *p = t; *p; ) { int l; unsigned c = ss::u8_dec (p, (int) strlen (p), &l); w += fnt::advance (f, c) / 64.0f; p += l > 0 ? l : 1; }
	if (align == 1) x -= w / 2; else if (align == 2) x -= w;
	for (const char *p = t; *p; ) { int l; unsigned c = ss::u8_dec (p, (int) strlen (p), &l); surf_glyph (s, f, x, (int) (y + f->ascent / 64.0f), c, col, alpha); x += fnt::advance (f, c) / 64.0f; p += l > 0 ? l : 1; }
}
static void nice_scale (double hi, double *top, double *step)
{
	if (hi <= 0) hi = 1;
	double e = pow (10, floor (log10 (hi))), m = hi / e;
	double s = m <= 1.5 ? 0.25 : m <= 3 ? 0.5 : m <= 6 ? 1 : 2;
	*step = s * e; *top = ceil (hi / *step) * *step;
}
static void fmt_num (double v, char *o, int cap)
{
	if (fabs (v) >= 1000) { long long k = (long long) llround (v); char t[32]; snprintf (t, sizeof t, "%lld", k < 0 ? -k : k); int n = (int) strlen (t), j = 0; char r[40]; if (k < 0) r[j++] = '-'; for (int i = 0; i < n; i++) { r[j++] = t[i]; if ((n - i - 1) % 3 == 0 && i < n - 1) r[j++] = ','; } r[j] = 0; scpy (o, r, cap); }
	else if (v == floor (v)) snprintf (o, cap, "%d", (int) v);
	else snprintf (o, cap, "%.1f", v);
}
static void chart_into (const Deck &d, Object &o, Layer &L, float pad, float sc, int alpha)
{
	Chart &c = *o.chart;
	Surf s = L.surf ();
	float X = pad, Y = pad, W = o.w * sc, H = o.h * sc;
	unsigned ink = d.rgb (THEME | TC_DK1), dim = uk_mix (ink, 0xFFFFFF, 140), gl = uk_mix (ink, 0xFFFFFF, 225);
	float top = Y;
	if (c.title[0]) { chart_text (d, s, X + W / 2, Y, c.title, 180, sc, ink, 1, true, alpha); top += 180 * 3.53f * sc * 1.6f; }
	float legendH = c.legend && c.nser > 0 && c.type != CH_PIE ? 140 * 3.53f * sc * 1.8f : 0;
	if (c.type == CH_PIE)
	{
		double sum = 0; for (int i = 0; i < c.ncat; i++) sum += fabs (c.val[0][i]);
		float r = fminf (W * 0.32f, (Y + H - top) * 0.46f), cx = X + W * 0.36f, cy = top + (Y + H - top) / 2;
		double a = -1.5707963;
		for (int i = 0; i < c.ncat && sum > 0; i++)
		{
			double sweep = fabs (c.val[0][i]) / sum * 6.2831853;
			VPath p; int pts[2 * 64]; int n = 0;
			pts[n++] = Q (cx); pts[n++] = Q (cy);
			int steps = (int) (sweep / 0.1) + 2; if (steps > 60) steps = 60;
			for (int k = 0; k <= steps; k++) { double t = a + sweep * k / steps; pts[n++] = Q (cx + r * (float) cos (t)); pts[n++] = Q (cy + r * (float) sin (t)); }
			p.poly (pts, n / 2);
			Fill f = fill_solid (SERIES_COL[i % 8]); f.alpha = (unsigned char) alpha;
			path_into (L, p, f, d, 0, 0, 0, 0);
			a += sweep;
		}
		// the legend at the right
		float ly = cy - c.ncat * 160 * 3.53f * sc / 2;
		for (int i = 0; i < c.ncat; i++)
		{
			surf_rect (s, X + W * 0.72f, ly + i * 160 * 3.53f * sc, 140 * 3.53f * sc * 0.7f, 140 * 3.53f * sc * 0.7f, d.rgb (SERIES_COL[i % 8]), alpha);
			char t[64]; snprintf (t, sizeof t, "%s  %.0f%%", c.cat[i], sum > 0 ? fabs (c.val[0][i]) / sum * 100 : 0.0);
			chart_text (d, s, X + W * 0.72f + 180 * sc, ly + i * 160 * 3.53f * sc - 10 * sc, t, 140, sc, ink, 0, false, alpha);
		}
		return;
	}
	double hi = 0; for (int k = 0; k < c.nser; k++) for (int i = 0; i < c.ncat; i++) if (c.val[k][i] > hi) hi = c.val[k][i];
	double vt, vs; nice_scale (hi, &vt, &vs);
	float axisW = 0;
	char lab[32];
	fmt_num (vt, lab, sizeof lab);
	axisW = (float) strlen (lab) * 140 * 3.53f * sc * 0.55f + 120 * sc;
	float gx0 = X + axisW, gy0 = top + 60 * sc, gx1 = X + W - 60 * sc, gy1 = Y + H - legendH - 140 * 3.53f * sc * 1.6f;
	if (gy1 <= gy0 + 10 || gx1 <= gx0 + 10) return;
	for (double v = 0; v <= vt + vs / 2; v += vs)
	{
		float y = gy1 - (float) (v / vt) * (gy1 - gy0);
		surf_rect (s, gx0, y, gx1 - gx0, fmaxf (1, sc * 10), gl, alpha);
		fmt_num (v, lab, sizeof lab);
		chart_text (d, s, gx0 - 60 * sc, y - 70 * 3.53f * sc * 0.6f, lab, 120, sc, dim, 2, false, alpha);
	}
	float slot = (gx1 - gx0) / (c.ncat ? c.ncat : 1);
	for (int i = 0; i < c.ncat; i++) chart_text (d, s, gx0 + slot * (i + 0.5f), gy1 + 40 * sc, c.cat[i], 120, sc, dim, 1, false, alpha);
	if (c.type == CH_COLUMN || c.type == CH_BAR)
	{
		float bw = slot * 0.72f / (c.nser ? c.nser : 1);
		for (int i = 0; i < c.ncat; i++)
			for (int k = 0; k < c.nser; k++)
			{
				float h = (float) (c.val[k][i] / vt) * (gy1 - gy0);
				float x = gx0 + slot * i + slot * 0.14f + bw * k;
				surf_rect (s, x, gy1 - h, bw - fmaxf (1, sc * 15), h, d.rgb (SERIES_COL[k % 8]), alpha);
				if (c.labels) { fmt_num (c.val[k][i], lab, sizeof lab); chart_text (d, s, x + bw / 2, gy1 - h - 120 * 3.53f * sc * 1.2f, lab, 100, sc, ink, 1, false, alpha); }
			}
	}
	else
	{
		for (int k = 0; k < c.nser; k++)
		{
			VPath p; int pts[2 * 24]; int n = 0;
			for (int i = 0; i < c.ncat; i++) { pts[n++] = Q (gx0 + slot * (i + 0.5f)); pts[n++] = Q (gy1 - (float) (c.val[k][i] / vt) * (gy1 - gy0)); }
			Fill f = fill_solid (SERIES_COL[k % 8]); f.alpha = (unsigned char) alpha;
			if (c.type == CH_AREA)
			{
				int pa[2 * 26]; int m = 0; for (int i = 0; i < n; i++) pa[m++] = pts[i];
				pa[m++] = pts[n - 2]; pa[m++] = Q (gy1); pa[m++] = pts[0]; pa[m++] = Q (gy1);
				p.poly (pa, m / 2); f.alpha = (unsigned char) (alpha * 150 / 255);
			}
			else { p.polyline (pts, n / 2, Q (fmaxf (1.5f, sc * 60)), false); for (int i = 0; i < c.ncat; i++) p.circle (pts[2 * i], pts[2 * i + 1], Q (fmaxf (2, sc * 90))); }
			path_into (L, p, f, d, 0, 0, 0, 0);
		}
	}
	surf_rect (s, gx0, gy1, gx1 - gx0, fmaxf (1, sc * 20), dim, alpha);
	if (legendH > 0)
	{
		float tot = 0; for (int k = 0; k < c.nser; k++) tot += (strlen (c.ser[k]) * 0.55f * 140 * 3.53f + 1000) * sc;
		float lx = X + (W - tot) / 2, ly = Y + H - legendH * 0.75f;
		for (int k = 0; k < c.nser; k++)
		{
			float q = 140 * 3.53f * sc * 0.7f; surf_rect (s, lx, ly + 140 * 3.53f * sc * 0.25f, q, q, d.rgb (SERIES_COL[k % 8]), alpha);
			chart_text (d, s, lx + 140 * 3.53f * sc * 1.1f, ly, c.ser[k], 140, sc, ink, 0, false, alpha);
			lx += (strlen (c.ser[k]) * 0.55f * 140 * 3.53f + 1000) * sc;
		}
	}
}

// ---- an object into its layer -----------------------------------------------------------------------------------------
// sc: px a hmm; edit: the editor (an empty placeholder: its prompt, a dashed box). The layer: the object's box and a
// margin (its line, its shadow), unrotated (the compositor turns it).
static void render_object (const Deck &d, Object &o, float sc, Layer &L, bool edit)
{
	float lw = o.line.type != LN_NONE ? fmaxf (0.75f, o.line.width * sc) : 0;
	float pad = ceilf (lw + 2 + (o.shadow ? 260 * sc : 0));
	if (o.kind == OB_LINE) pad = ceilf (fmaxf (lw * 5, 4) + 2);
	if (o.kind == OB_TABLE) table_fit (d, o);
	float W = o.w * sc, H = o.h * sc;
	int lwpx = (int) ceilf (W + 2 * pad), lhpx = (int) ceilf (H + 2 * pad);
	if (lwpx > 4096) lwpx = 4096; if (lhpx > 4096) lhpx = 4096;
	L.alloc (lwpx, lhpx);
	L.ox = o.x * sc - pad; L.oy = o.y * sc - pad;
	L.cx = (o.x + o.w / 2.0f) * sc; L.cy = (o.y + o.h / 2.0f) * sc;
	L.rot = o.rot; L.flipH = o.kind != OB_LINE && o.flipH; L.flipV = o.kind != OB_LINE && o.flipV;
	if (o.kind == OB_LINE)
	{
		float x0 = pad, y0 = pad, x1 = pad + W, y1 = pad + H;
		if (o.flipH) { float t = x0; x0 = x1; x1 = t; }
		if (o.flipV) { float t = y0; y0 = y1; y1 = t; }
		VPath p;
		float ang = atan2f (y1 - y0, x1 - x0);
		float sx0 = x0, sy0 = y0, sx1 = x1, sy1 = y1;
		if (o.line.head1 == AH_ARROW) { sx1 -= cosf (ang) * lw * 3.5f; sy1 -= sinf (ang) * lw * 3.5f; }
		if (o.line.head0 == AH_ARROW) { sx0 += cosf (ang) * lw * 3.5f; sy0 += sinf (ang) * lw * 3.5f; }
		if (o.line.type == LN_SOLID) p.line (Q (sx0), Q (sy0), Q (sx1), Q (sy1), Q (fmaxf (lw, 1)));
		else
		{
			float len = sqrtf ((sx1 - sx0) * (sx1 - sx0) + (sy1 - sy0) * (sy1 - sy0)), dash = o.line.type == LN_DASH ? lw * 4 : lw * 1.2f, gap = lw * 2.5f;
			for (float t = 0; t < len; t += dash + gap)
			{
				float t1 = fminf (len, t + dash);
				p.line (Q (sx0 + (sx1 - sx0) * t / len), Q (sy0 + (sy1 - sy0) * t / len), Q (sx0 + (sx1 - sx0) * t1 / len), Q (sy0 + (sy1 - sy0) * t1 / len), Q (fmaxf (lw, 1)));
			}
		}
		if (o.line.head1) arrow_head (p, x1, y1, ang, o.line.head1, lw);
		if (o.line.head0) arrow_head (p, x0, y0, ang + 3.14159265f, o.line.head0, lw);
		path_into (L, p, fill_solid (o.line.color), d, 0, 0, 0, 0);
		return;
	}
	int sh = o.kind == OB_SHAPE ? o.shape : SH_RECT;
	if (o.kind == OB_PICTURE && o.shape != SH_RECT) sh = o.shape;
	// the shadow
	if (o.shadow && (o.fill.type != FILL_NONE || o.kind == OB_PICTURE || o.kind == OB_TABLE))
	{
		VPath p; shape_path (p, sh, pad, pad, W, H, o.radius, 0, true);
		Fill f = fill_none (); f.alpha = 90;
		path_into (L, p, f, d, 0, 0, 0, 0, (int) (90 * sc + 1), (int) (130 * sc + 1), 0x000000, (int) (60 * sc + 1));
	}
	// the fill
	if ((o.fill.type == FILL_SOLID || o.fill.type == FILL_GRADIENT) && o.kind != OB_TABLE)
	{
		VPath p; shape_path (p, sh, pad, pad, W, H, o.radius, 0, true);
		path_into (L, p, o.fill, d, pad, pad, W, H);
	}
	// a picture
	if (o.kind == OB_PICTURE)
	{
		Picture *pc = pic (o.img);
		if (pc)
		{
			VPath m; bool rounded = sh != SH_RECT;
			if (rounded) shape_path (m, sh, pad, pad, W, H, o.radius, 0, true);
			picture_into (L, *pc, o.crop, pad, pad, W, H, 255, rounded ? &m : 0);
		}
		else
		{
			Surf s = L.surf (); surf_rect (s, pad, pad, W, H, 0xE8E4E0);
			surf_rect (s, pad, pad + H / 2, W, 1, 0xB0A8A0); surf_rect (s, pad + W / 2, pad, 1, H, 0xB0A8A0);
		}
	}
	if (o.kind == OB_TABLE && o.tbl) table_into (d, o, L, pad, sc, 255);
	if (o.kind == OB_CHART && o.chart) chart_into (d, o, L, pad, sc, 255);
	// the outline
	if (o.line.type != LN_NONE && o.kind != OB_TABLE)
	{
		VPath p;
		if (o.line.type == LN_SOLID) shape_path (p, sh, pad, pad, W, H, o.radius, lw, false);
		else
		{
			PolyB b; if (!shape_poly (sh, pad, pad, W, H, b)) shape_poly (SH_RECT, pad, pad, W, H, b);
			float dash = o.line.type == LN_DASH ? lw * 4 : lw * 1.2f, gap = lw * 2.5f;
			for (int i = 0; i < b.n; i++)
			{
				float ax = b.xy[2 * i] / 16.0f, ay = b.xy[2 * i + 1] / 16.0f, bx = b.xy[2 * ((i + 1) % b.n)] / 16.0f, by = b.xy[2 * ((i + 1) % b.n) + 1] / 16.0f;
				float len = sqrtf ((bx - ax) * (bx - ax) + (by - ay) * (by - ay));
				for (float t = 0; t < len; t += dash + gap) { float t1 = fminf (len, t + dash); p.line (Q (ax + (bx - ax) * t / len), Q (ay + (by - ay) * t / len), Q (ax + (bx - ax) * t1 / len), Q (ay + (by - ay) * t1 / len), Q (lw)); }
			}
		}
		path_into (L, p, fill_solid (o.line.color), d, 0, 0, 0, 0);
	}
	// an empty placeholder in the editor: a dashed frame
	if (edit && o.ph != PH_NONE && o.tb.empty () && o.kind == OB_TEXT && o.fill.type == FILL_NONE)
	{
		Surf s = L.surf ();
		for (float t = 0; t < W; t += 8) { surf_rect (s, pad + t, pad, fminf (4, W - t), 1, 0xA0A0A0); surf_rect (s, pad + t, pad + H - 1, fminf (4, W - t), 1, 0xA0A0A0); }
		for (float t = 0; t < H; t += 8) { surf_rect (s, pad, pad + t, 1, fminf (4, H - t), 0xA0A0A0); surf_rect (s, pad + W - 1, pad + t, 1, fminf (4, H - t), 0xA0A0A0); }
	}
	// the text
	if (o.kind != OB_PICTURE && o.kind != OB_TABLE && o.kind != OB_CHART)
	{
		if (!o.tb.p.n) o.tb.ensure ();
		TextLayout tl; layout_object (d, o, tl);
		Surf s = L.surf ();
		draw_text (d, o, tl, s, pad, pad, sc, edit && o.ph != PH_NONE ? PH_PROMPTS[(int) o.ph] : 0);
	}
	else if (o.kind == OB_PICTURE && edit && o.ph == PH_PICTURE && o.img < 0)
	{
		Surf s = L.surf ();
		chart_text (d, s, pad + W / 2, pad + H / 2 - 100 * sc, PH_PROMPTS[PH_PICTURE], 160, sc, 0x707070, 1, false, 255);
	}
}

// ---- the slide's background, the footer, the number --------------------------------------------------------------------
static Fill slide_bg (const Deck &d, const Slide &s) { return s.bg.type == FILL_INHERIT ? d.masterBg : s.bg; }
// The footer's and the number's text boxes (made for a slide; the caller deletes them).
static void footer_objects (const Deck &d, const Slide &s, int index, Vec<Object *> &out)
{
	if (!s.masterObjects) return;
	int W = d.sw, H = d.sh;
	CharFmt f = cf_inherit (); f.size = 120; f.color = 0x707C84;
	if (d.footer && (d.footerText[0]))
	{
		Object *o = new Object; o->id = -1; o->x = W * 57 / 1000; o->y = H * 945 / 1000; o->w = W * 60 / 100; o->h = H * 45 / 1000;
		o->tb.inset[1] = o->tb.inset[3] = 0; o->tb.inset[0] = 0; o->tb.set_text (d.footerText, f); out.push (o);
	}
	if (d.number && s.layout != LY_TITLE)
	{
		Object *o = new Object; o->id = -2; o->x = W * 80 / 100; o->y = H * 945 / 1000; o->w = W * 143 / 1000; o->h = H * 45 / 1000;
		o->tb.inset[1] = o->tb.inset[3] = 0; o->tb.inset[2] = 0;
		char t[16]; snprintf (t, sizeof t, "%d", index + 1);
		CharFmt g = f; g.color = THEME | TC_ACC1; g.flags = CF_BOLD; g.set = CF_BOLD;
		ParaFmt pf = pf_inherit (); pf.align = AL_RIGHT;
		o->tb.set_text (t, g, &pf); out.push (o);
	}
}
// The background into a layer the slide's size (opaque).
static void render_bg (const Deck &d, const Slide &s, float sc, Layer &L)
{
	int W = (int) ceilf (d.sw * sc), H = (int) ceilf (d.sh * sc);
	L.alloc (W, H); L.ox = L.oy = 0; L.rot = 0; L.cx = W / 2.0f; L.cy = H / 2.0f;
	Fill f = slide_bg (d, s);
	unsigned c1 = d.rgb (f.c1), c2 = d.rgb (f.c2);
	float ca = cosf (f.angle * 0.01745329f), sa = sinf (f.angle * 0.01745329f);
	float span = fabsf (W * ca) + fabsf (H * sa); if (span < 1) span = 1;
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++)
		{
			unsigned c = c1;
			if (f.type == FILL_GRADIENT) { float t = ((x - W / 2.0f) * ca + (y - H / 2.0f) * sa) / span + 0.5f; c = uk_mix (c1, c2, iclamp ((int) (t * 256), 0, 256)); }
			else if (f.type == FILL_NONE) c = 0xFFFFFF;
			L.px[y * W + x] = 0xFF000000u | c;
		}
}

// ---- the compositor: the layers kept as textures ------------------------------------------------------------------------
static void *gp_alloc (unsigned long n) { return malloc (n); }
static void gp_free (void *p) { free (p); }

// An FNV hash of what an object's pixels depend on (not its place, its rotation: those are the matrix's).
struct Hash { unsigned h; Hash () : h (2166136261u) {} void add (const void *p, int n) { const unsigned char *b = (const unsigned char *) p; for (int i = 0; i < n; i++) { h ^= b[i]; h *= 16777619u; } } void addi (int v) { add (&v, 4); } };
static void hash_tb (Hash &h, const TextBody &tb)
{
	h.addi (tb.p.n); h.addi (tb.anchor); h.addi (tb.fit); h.addi (tb.wrap); h.add (tb.inset, sizeof tb.inset);
	for (int i = 0; i < tb.p.n; i++) { const Para *q = tb.p[i]; h.addi (q->len); h.add (q->ch, 4 * q->len); h.add (q->cf, sizeof (CharFmt) * q->len); h.add (&q->pf, sizeof q->pf); h.add (&q->end, sizeof q->end); }
}
static unsigned obj_hash (const Deck &d, const Object &o, float sc, bool edit)
{
	Hash h;
	h.addi (o.kind); h.addi (o.shape); h.addi (o.ph); h.addi (o.w); h.addi (o.h); h.addi (o.radius); h.addi (o.shadow); h.addi (o.img);
	h.addi (o.kind == OB_LINE ? o.flipH * 2 + o.flipV : 0);
	h.add (&o.fill, sizeof o.fill); h.add (&o.line, sizeof o.line); h.add (o.crop, sizeof o.crop);
	hash_tb (h, o.tb);
	if (o.tbl) { h.addi (o.tbl->rows); h.addi (o.tbl->cols); h.add (o.tbl->colW, 4 * o.tbl->cols); h.add (o.tbl->rowH, 4 * o.tbl->rows); h.addi (o.tbl->header); h.addi (o.tbl->banded); for (int i = 0; i < o.tbl->rows * o.tbl->cols; i++) { hash_tb (h, o.tbl->cell[i]); h.add (&o.tbl->cfill[i], sizeof (Fill)); } }
	if (o.chart) h.add (o.chart, sizeof (Chart));
	h.add (&sc, sizeof sc); h.addi (edit);
	h.add (d.theme.col, sizeof d.theme.col); h.add (d.theme.major, sizeof d.theme.major); h.add (d.theme.minor, sizeof d.theme.minor);
	h.add (d.style, sizeof d.style);
	for (int i = 0; i < d.font.n; i++) h.add (d.font[i], (int) strlen (d.font[i]));
	return h.h;
}

struct TexEnt { unsigned key; gpc_tex *tex; int w, h; float ox, oy; unsigned used; };
struct Compositor
{
	gpc_ctx *g;
	Vec<TexEnt> tex;
	unsigned frame;
	Compositor () : g (0), frame (0) {}
	void init (bool cpu = false) { if (!g) { gpc_config c = { gp_alloc, gp_free, cpu ? GPC_F_CPU : 0u }; g = gpc_create (&c); } }
	// the texture of a key (rendered by fn when absent): its size, its offset from the object's top-left (px)
	TexEnt *find (unsigned key) { for (int i = 0; i < tex.n; i++) if (tex[i].key == key) { tex[i].used = frame; return &tex[i]; } return 0; }
	TexEnt *put (unsigned key, Layer &L, float objX, float objY)
	{
		TexEnt e; e.key = key; e.w = L.w; e.h = L.h; e.ox = L.ox - objX; e.oy = L.oy - objY; e.used = frame;
		e.tex = gpc_tex_create (g, L.w, L.h, L.px, L.w);
		tex.push (e);
		return &tex[tex.n - 1];
	}
	void sweep (unsigned keepFrames = 2)		// the textures not used lately dropped
	{
		for (int i = 0; i < tex.n; )
			if (frame - tex[i].used > keepFrames) { gpc_tex_destroy (g, tex[i].tex); tex.erase (i); }
			else i++;
	}
	void drop_all () { for (int i = 0; i < tex.n; i++) gpc_tex_destroy (g, tex[i].tex); tex.clear (); }
};

// A layer's matrix: the texture at (ox, oy) of the slide (px), the slide at (sx, sy) of the target, the object
// turned by rot about (cx, cy), flipped; then scaled by k about the object's centre (an effect's zoom).
static void layer_matrix (gpc_layer &g, float sx, float sy, float ox, float oy, float cx, float cy, int rot, bool fh, bool fv, float k = 1, float dx = 0, float dy = 0)
{
	gpc_matrix_identity (&g.m);
	gpc_matrix_translate (&g.m, sx + cx + dx, sy + cy + dy);
	if (rot) gpc_matrix_rotate (&g.m, rot * 0.01745329f);
	if (k != 1 || fh || fv) gpc_matrix_scale (&g.m, (fh ? -k : k), (fv ? -k : k));
	gpc_matrix_translate (&g.m, ox - cx, oy - cy);
}

// What a slide's frame is made of: its layers in order, each its object's id (0: the background, -1 / -2: footer,
// number, < -100: a master decoration), its texture.
struct FrameLayer { int id; TexEnt t; float ox, oy, cx, cy; int rot; bool fh, fv; };	// (t: a copy -- the cache's array moves as it grows)
struct Frame { Vec<FrameLayer> l; float sc; };

// The slide's layers (textures made as needed) at scale sc. skip: an object left out (being edited elsewhere).
static void build_frame (Compositor &C, const Deck &d, Slide &s, int index, float sc, bool edit, Frame &F)
{
	F.l.clear (); F.sc = sc;
	// the background
	{
		Hash h; h.addi (0x5EED); Fill f = slide_bg (d, s); h.add (&f, sizeof f); h.add (d.theme.col, sizeof d.theme.col); h.add (&sc, sizeof sc); h.addi (d.sw); h.addi (d.sh);
		TexEnt *t = C.find (h.h);
		if (!t) { Layer L; render_bg (d, s, sc, L); t = C.put (h.h, L, 0, 0); L.release (); }
		FrameLayer fl; fl.id = 0; fl.t = *t; fl.ox = 0; fl.oy = 0; fl.cx = fl.cy = 0; fl.rot = 0; fl.fh = fl.fv = false;
		F.l.push (fl);
	}
	Vec<Object *> extra;
	if (s.masterObjects) for (int i = 0; i < d.decor.n; i++) extra.push (d.decor[i]);
	Vec<Object *> made; footer_objects (d, s, index, made);
	for (int i = 0; i < made.n; i++) extra.push (made[i]);
	for (int i = 0; i < extra.n + s.obj.n; i++)
	{
		bool isExtra = i < extra.n;
		Object &o = isExtra ? *extra[i] : *s.obj[i - extra.n];
		unsigned key = obj_hash (d, o, sc, edit && !isExtra);
		TexEnt *t = C.find (key);
		if (!t) { Layer L; render_object (d, o, sc, L, edit && !isExtra); t = C.put (key, L, o.x * sc, o.y * sc); L.release (); }
		FrameLayer fl; fl.id = isExtra ? (o.id < 0 ? o.id : -100 - i) : o.id; fl.t = *t;
		fl.ox = o.x * sc + t->ox; fl.oy = o.y * sc + t->oy;
		fl.cx = (o.x + o.w / 2.0f) * sc; fl.cy = (o.y + o.h / 2.0f) * sc;
		fl.rot = o.kind == OB_LINE ? 0 : o.rot; fl.fh = o.kind != OB_LINE && o.flipH; fl.fv = o.kind != OB_LINE && o.flipV;
		F.l.push (fl);
	}
	for (int i = 0; i < made.n; i++) delete made[i];
}

// The frame composited into a target at (sx, sy): each layer at its place (move: an object's offset, px, while dragged).
struct Move { int id; float dx, dy, k; int alpha; int rot; int clip[4]; };	// clip: x, y, w, h in the target (w <= 0: none)
static inline Move move_of (int id) { Move m; m.id = id; m.dx = m.dy = 0; m.k = 1; m.alpha = 255; m.rot = 0; m.clip[0] = m.clip[1] = m.clip[2] = m.clip[3] = 0; return m; }
static void composite_frame (Compositor &C, Frame &F, unsigned *px, int w, int h, int stride, float sx, float sy, unsigned clearC,
			     const Move *mv = 0, int nmv = 0, int skipId = 0x7FFFFFFF, int clip = 1)
{
	Vec<gpc_layer> L;
	float sw = F.l.n ? F.l[0].t.w : 0, sh = F.l.n ? F.l[0].t.h : 0;
	for (int i = 0; i < F.l.n; i++)
	{
		FrameLayer &f = F.l[i];
		if (f.id == skipId) continue;
		gpc_layer g; gpc_layer_init (&g, f.t.tex);
		float dx = 0, dy = 0, k = 1; int a = 255, rot = 0; const int *mc = 0;
		for (int m = 0; m < nmv; m++) if (mv[m].id == f.id) { dx = mv[m].dx; dy = mv[m].dy; k = mv[m].k; a = mv[m].alpha; rot = mv[m].rot; if (mv[m].clip[2] > 0) mc = mv[m].clip; }
		if (a <= 0) continue;
		layer_matrix (g, sx, sy, f.ox, f.oy, f.cx, f.cy, f.rot + rot, f.fh, f.fv, k, dx, dy);
		if (clip) { g.clip[0] = (int) sx; g.clip[1] = (int) sy; g.clip[2] = (int) sw; g.clip[3] = (int) sh; }
		if (mc)
		{
			int x0 = imax (mc[0], g.clip[2] > 0 ? g.clip[0] : mc[0]), y0 = imax (mc[1], g.clip[2] > 0 ? g.clip[1] : mc[1]);
			int x1 = imin (mc[0] + mc[2], g.clip[2] > 0 ? g.clip[0] + g.clip[2] : mc[0] + mc[2]), y1 = imin (mc[1] + mc[3], g.clip[2] > 0 ? g.clip[1] + g.clip[3] : mc[1] + mc[3]);
			g.clip[0] = x0; g.clip[1] = y0; g.clip[2] = imax (1, x1 - x0); g.clip[3] = imax (1, y1 - y0);
			if (x1 <= x0 || y1 <= y0) continue;
		}
		g.opacity = (unsigned) a;
		if (f.id == 0) g.flags |= GPC_L_OPAQUE;
		L.push (g);
	}
	gpc_target t; t.pixels = px; t.w = w; t.h = h; t.stride = stride; t.flags = 0;
	gpc_composite (C.g, &t, L.a, L.n, clearC, GPC_C_CLEAR);
}

// The whole slide flattened into an opaque buffer (a thumbnail, the PDF, the show's next slide): w x h px.
static void flatten_slide (Compositor &C, const Deck &d, Slide &s, int index, unsigned *px, int w, int h, int stride)
{
	float sc = (float) w / d.sw;
	Frame F; build_frame (C, d, s, index, sc, false, F);
	composite_frame (C, F, px, w, h, stride, 0, 0, 0xFFFFFF);
}

} // namespace sl

#endif
