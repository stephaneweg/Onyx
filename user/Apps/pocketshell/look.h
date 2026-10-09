//
// look.h -- pocketshell's drawing pieces for the launcher v2 (docs/COMPACT-SHELL-STUDY.md section 6.2, the approved
// mock-ups docs/compact-shell/mockups/pocket-home-v2*.png): rounded boxes of any radius, anti-aliased (their
// signed distance -- UIKit's uk_rbox stops at 16 px, the search field at 1.5 is 25), their outlines of any thickness,
// soft shadows and glows (a Gaussian's edge: what a blurred box looks like), pictures clipped to a rounded box (the
// running apps' thumbnails), text with the query's match in Aqua and a line cut to keep that match in view.
// Integer only (the apps are built -mgeneral-regs-only). Part of main.cpp (one unit).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//

// A pixel of c at opacity a (0..255) over the canvas (clipped).
static inline void lk_px (Canvas &cv, int x, int y, unsigned c, int a)
{
	if ((unsigned) x >= (unsigned) cv.w || (unsigned) y >= (unsigned) cv.h || a <= 0) return;
	unsigned &d = cv.px[y * cv.stride + x];
	if (a >= 255) { d = c & 0xFFFFFF; return; }
	unsigned dr = (d >> 16) & 255, dg = (d >> 8) & 255, db = d & 255;
	unsigned cr = (c >> 16) & 255, cg = (c >> 8) & 255, cb = c & 255;
	dr += (int) ((cr - dr) * a) / 255; dg += (int) ((cg - dg) * a) / 255; db += (int) ((cb - db) * a) / 255;
	d = ((dr & 255) << 16) | ((dg & 255) << 8) | (db & 255);
}

static int lk_isqrt (int v)
{
	if (v <= 0) return 0;
	int r = 0, b = 1 << 30;
	while (b > v) b >>= 2;
	while (b) { if (v >= r + b) { v -= r + b; r = (r >> 1) + b; } else r >>= 1; b >>= 2; }
	return r;
}

// A rounded box in 1/16 px: x, y, w, h, r -> the signed distance of pixel (px, py)'s centre to its edge, 1/16 px
// (negative inside).
struct LkBox { int cx2, cy2, hw, hh, r; };		// (its centre doubled, its half sizes less the radius, the radius)
static LkBox lk_box (int x, int y, int w, int h, int r)
{
	if (r * 2 > w) r = w / 2;
	if (r * 2 > h) r = h / 2;
	if (r < 0) r = 0;
	LkBox b = { (2 * x + w) * 16, (2 * y + h) * 16, (w * 16) / 2 - r * 16, (h * 16) / 2 - r * 16, r * 16 };
	return b;
}
static inline int lk_sd (const LkBox &b, int px, int py)
{
	int qx = (px * 32 + 16 - b.cx2) / 2, qy = (py * 32 + 16 - b.cy2) / 2;
	if (qx < 0) qx = -qx;
	if (qy < 0) qy = -qy;
	qx -= b.hw; qy -= b.hh;
	int out;
	if (qx > 0 && qy > 0) out = lk_isqrt (qx * qx + qy * qy);
	else out = qx > qy ? qx : qy;				// (one of them <= 0: the nearest straight edge)
	return out - b.r;
}
static inline int lk_cov (int sd)			// a signed distance -> the pixel's coverage 0..255
{
	int c = 8 - sd;					// (a one-pixel ramp across the edge)
	return c <= 0 ? 0 : c >= 16 ? 255 : c * 255 / 16;
}

// A rounded box filled with a vertical gradient top -> bottom, at opacity alpha.
static void lk_fill (Canvas &cv, int x, int y, int w, int h, int r, unsigned top, unsigned bottom, int alpha = 255)
{
	if (w <= 0 || h <= 0) return;
	LkBox b = lk_box (x, y, w, h, r);
	if (r * 2 > w) r = w / 2;
	if (r * 2 > h) r = h / 2;
	for (int j = 0; j < h; j++)
	{
		int py = y + j;
		if ((unsigned) py >= (unsigned) cv.h) continue;
		unsigned c = h > 1 ? uk_mix (top, bottom, j * 256 / (h - 1)) : top;
		bool corner = j < r + 1 || j >= h - r - 1;
		int x0 = x, x1 = x + w;
		if (!corner)
		{
			if (alpha >= 255) { cv.fillRect (x, py, w, 1, c); continue; }
			for (int px = x0; px < x1; px++) lk_px (cv, px, py, c, alpha);
			continue;
		}
		for (int px = x0; px < x1; px++)
		{
			int k = px - x;
			if (k > r + 1 && k < w - r - 2)			// (the row's middle: inside)
			{
				if (alpha >= 255) { cv.fillRect (px, py, w - r - 2 - k, 1, c); px = x + w - r - 3; }
				else lk_px (cv, px, py, c, alpha);
				continue;
			}
			int a = lk_cov (lk_sd (b, px, py));
			if (a) lk_px (cv, px, py, c, a * alpha / 255);
		}
	}
}

// Its outline, t16 sixteenths of a pixel thick (inside the box), colour c at opacity alpha.
static void lk_ring (Canvas &cv, int x, int y, int w, int h, int r, int t16, unsigned c, int alpha = 255)
{
	if (w <= 0 || h <= 0) return;
	LkBox b = lk_box (x, y, w, h, r);
	int band = (t16 + 31) / 16 + 1;					// (the pixels near the edge only)
	if (r * 2 > w) r = w / 2;
	if (r * 2 > h) r = h / 2;
	for (int j = 0; j < h; j++)
	{
		int py = y + j;
		if ((unsigned) py >= (unsigned) cv.h) continue;
		bool full = j < r + band || j >= h - r - band;
		for (int px = x; px < x + w; px++)
		{
			int k = px - x;
			if (!full && k >= band && k < w - band) { px = x + w - band - 1; continue; }
			int sd = lk_sd (b, px, py);
			int a = lk_cov (sd) - lk_cov (sd + t16);
			if (a > 0) lk_px (cv, px, py, c, a * alpha / 255);
		}
	}
}

// 1 - the normal law's distribution at s / 8 standard deviations (s = -24..24): a blurred edge's opacity, 0..256
// (T: 256 x Q (s / 8)).
static int lk_edge (int s)
{
	static const unsigned char T[25] = { 128, 115, 103, 91, 79, 68, 58, 49, 41, 33, 27, 21, 17, 13, 10, 7, 6, 4, 3, 2, 1, 1, 1, 0, 0 };
	if (s >= 24) return 0;
	if (s <= -24) return 256;
	return s >= 0 ? T[s] : 256 - T[-s];
}
// A soft shadow (or, in Aqua, a glow) under a rounded box: blur = its standard deviation in pixels, dy its drop.
static void lk_shadow (Canvas &cv, int x, int y, int w, int h, int r, int blur, int alpha, int dy = 0, unsigned c = 0x060E1C)
{
	if (blur < 1) blur = 1;
	y += dy;
	LkBox b = lk_box (x, y, w, h, r);
	int m = blur * 3 + 1;
	for (int py = y - m; py < y + h + m; py++)
	{
		if ((unsigned) py >= (unsigned) cv.h) continue;
		bool mid = py >= y + r + m && py < y + h - r - m;
		for (int px = x - m; px < x + w + m; px++)
		{
			if (mid && px >= x + m && px < x + w - m) { px = x + w - m - 1; continue; }	// (deep inside: the box covers it)
			int sd = lk_sd (b, px, py);				// 1/16 px
			int a = lk_edge (sd / (2 * blur)) * alpha / 256;		// (sd / 16 / blur * 8)
			if (a > 0) lk_px (cv, px, py, c, a);
		}
	}
}

// A picture (0xRRGGBB, pw x ph, the rows from sy) in the rounded box x, y, w, h: its corners cut, anti-aliased.
static void lk_picture (Canvas &cv, int x, int y, int w, int h, int r, const unsigned *pic, int pw, int ph)
{
	LkBox b = lk_box (x, y, w, h, r);
	for (int j = 0; j < h && j < ph; j++)
		for (int i = 0; i < w && i < pw; i++)
		{
			bool edge = j <= r || j >= h - r - 1 || i <= r || i >= w - r - 1;
			int a = edge ? lk_cov (lk_sd (b, x + i, y + j)) : 255;
			if (a) lk_px (cv, x + i, y + j, pic[j * pw + i], a);
		}
}

// ---- text with the query's match -------------------------------------------------------------------------------
// Where q is in s, ignoring the case (ASCII) -> its offset, -1 none.
static int lk_find (const char *s, const char *q)
{
	if (!q || !q[0]) return -1;
	for (int i = 0; s[i]; i++)
	{
		int k = 0;
		while (q[k] && s[i + k] && lx_low (s[i + k]) == lx_low (q[k])) k++;
		if (!q[k]) return i;
	}
	return -1;
}
// s at x, y in ink, the part matching q in hl -> the width drawn.
static int lk_text_hl (Canvas &cv, int x, int y, const char *s, const char *q, unsigned ink, unsigned hl, int style = 0)
{
	int i = lk_find (s, q), n = (int) strlen (q ? q : "");
	if (i < 0) { uk_text (cv, x, y, s, ink, style); return uk_tw (s, style); }
	char a[160];
	int x0 = x;
	int k = i < 159 ? i : 159; memcpy (a, s, (size_t) k); a[k] = 0;
	uk_text (cv, x, y, a, ink, style); x += uk_tw (a, style);
	k = n < 159 ? n : 159; memcpy (a, s + i, (size_t) k); a[k] = 0;
	uk_text (cv, x, y, a, hl, style); x += uk_tw (a, style);
	uk_text (cv, x, y, s + i + n, ink, style); x += uk_tw (s + i + n, style);
	return x - x0;
}
// s cut to w pixels into o, keeping q's match in view ("... its password, the country") -> o.
static const char *lk_excerpt (const char *s, const char *q, int w, char *o, int cap, int style = 0)
{
	if (uk_tw (s, style) <= w) { fs_copy (o, s, cap); return o; }
	int i = lk_find (s, q), n = (int) strlen (q ? q : "");
	char head[200];
	int k = i + n < 199 ? i + n : 199;
	if (i >= 0) { memcpy (head, s, (size_t) k); head[k] = 0; }
	if (i < 0 || uk_tw (head, style) < w - uk_tw ("...", style)) { uk_text_fit (s, w, o, cap, style); return o; }
	// drop the words before the match until "... " + the rest fits
	const char *p = s;
	char t[240];
	for (;;)
	{
		const char *sp = p;
		while (*sp && *sp != ' ' && sp < s + i) sp++;
		if (*sp != ' ' || sp >= s + i) { p = s + i; break; }
		p = sp + 1;
		int m = 0;
		lx_cat (t, sizeof t, &m, "... "); lx_cat (t, sizeof t, &m, p);
		if (uk_tw (t, style) <= w) break;
	}
	int m = 0;
	lx_cat (t, sizeof t, &m, "... "); lx_cat (t, sizeof t, &m, p);
	uk_text_fit (t, w, o, cap, style);
	return o;
}
