//
// wtk/paint.cpp -- the procedural painter (wtk/paint.h): shades, gradients, rounded boxes from
// per-radius corner tables, the framed button, raised / sunken faces, glyphs. Integer only.
//
#include "wtk/paint.h"
#include "wtk/theme.h"
#include "wtk/font.h"
#include "wtk/widget.h"

namespace wtk {

// ---- colours ----------------------------------------------------------------------------------------
unsigned wk_tone (unsigned c, int level)
{
	if (level < 0) level = 0;
	if (level > 255) level = 255;
	unsigned o = 0;
	for (int sh = 0; sh <= 16; sh += 8)
	{
		int v = (int) ((c >> sh) & 0xFF);
		v = level >= 128 ? v + (255 - v) * (level - 128) / 127 : v * level / 128;
		o |= (unsigned) v << sh;
	}
	return o;
}

unsigned wk_mix (unsigned a, unsigned b, int t)
{
	if (t <= 0) return a & 0x00FFFFFFu;
	if (t >= 256) return b & 0x00FFFFFFu;
	unsigned o = 0;
	for (int sh = 0; sh <= 16; sh += 8)
	{
		int va = (int) ((a >> sh) & 0xFF), vb = (int) ((b >> sh) & 0xFF);
		o |= (unsigned) ((va * (256 - t) + vb * t) >> 8) << sh;
	}
	return o;
}

int wk_bright (unsigned c)
{
	return (int) ((((c >> 16) & 0xFF) * 77 + ((c >> 8) & 0xFF) * 151 + (c & 0xFF) * 28) >> 8);
}

unsigned wk_ink_on (unsigned c) { return wk_bright (c) > 140 ? 0x00201C1Au : 0x00FFFFFFu; }
unsigned wk_ink_for (unsigned bg) { return wk_ink_on (bg) == wk_ink_on (C_BG) ? C_TEXT : wk_ink_on (bg); }

// ---- the corner tables --------------------------------------------------------------------------------
const WkCorner &wk_corner (int r)
{
	static WkCorner s_t[17];
	static bool s_done[17];
	if (r < 1) r = 1;
	if (r > 16) r = 16;
	if (!s_done[r])
	{
		WkCorner &t = s_t[r];
		t.r = r;
		long R = r * 32, R2 = R * R;				// (1/32 px: the samples' centres are odd)
		for (int j = 0; j < r; j++)
		{
			int cov[16];
			for (int i = 0; i < r; i++)
			{
				int n = 0;
				for (int sy = 0; sy < 16; sy++)
					for (int sx = 0; sx < 16; sx++)
					{
						long dx = i * 32 + sx * 2 + 1 - R, dy = j * 32 + sy * 2 + 1 - R;
						if (dx * dx + dy * dy <= R2) n++;
					}
				cov[i] = n * 255 / 256;
				if (n == 256) cov[i] = 255;
			}
			int off = 0;
			while (off < r && cov[off] == 0) off++;
			int k = 0;
			while (off + k < r && cov[off + k] < 255 && k < 16) { t.a[j][k] = (unsigned char) cov[off + k]; k++; }
			t.off[j] = (unsigned char) off; t.n[j] = (unsigned char) k;
		}
		s_done[r] = true;
	}
	return s_t[r];
}

// The coverage (0..255) of pixel (i, j) of a w x h rounded box (radius r, `corners` rounded).
static int rcov (int i, int j, int w, int h, int r, int corners)
{
	if (i < 0 || j < 0 || i >= w || j >= h) return 0;
	if (r <= 0) return 255;
	int ci = -1, cj = -1; bool right = false, bottom = false;
	if (j < r) cj = j; else if (j >= h - r) { cj = h - 1 - j; bottom = true; }
	if (i < r) ci = i; else if (i >= w - r) { ci = w - 1 - i; right = true; }
	if (ci < 0 || cj < 0) return 255;
	int which = bottom ? (right ? WK_BR : WK_BL) : (right ? WK_TR : WK_TL);
	if (!(corners & which)) return 255;
	const WkCorner &t = wk_corner (r);
	if (ci < t.off[cj]) return 0;
	if (ci < t.off[cj] + t.n[cj]) return t.a[cj][ci - t.off[cj]];
	return 255;
}

// The alpha mode (wk_paint_alpha): the canvas's pixels carry a transparency in their top byte (a
// WIN_FLAG_ALPHA window: the dock, the agenda); a blend over a see-through pixel then leaves the
// colour itself, partly see-through -- not the colour darkened by the black below it.
static bool s_alphaMode = false;
void wk_paint_alpha (bool on) { s_alphaMode = on; }

static inline void blend_px (Canvas &cv, int x, int y, unsigned c, int a)
{
	if (x < 0 || y < 0 || x >= cv.w || y >= cv.h || a <= 0) return;
	unsigned *p = cv.px + (long) y * cv.stride + x;
	if (a >= 255) { *p = c & 0x00FFFFFFu; return; }
	if (s_alphaMode && (*p >> 24))
	{
		unsigned od = 255 - (*p >> 24), oa = (unsigned) a + od * (255 - (unsigned) a) / 255;	// opacities
		if (oa == 0) return;
		unsigned o = 0, d = *p;
		for (int sh = 0; sh <= 16; sh += 8)
		{
			unsigned cs = (c >> sh) & 0xFF, ds = (d >> sh) & 0xFF;
			o |= ((cs * (unsigned) a + ds * od * (255 - (unsigned) a) / 255) / oa) << sh;
		}
		*p = ((255 - oa) << 24) | o;
		return;
	}
	*p = wk_over (*p, c, a);
}

void wk_blend_px (Canvas &cv, int x, int y, unsigned c, int a) { blend_px (cv, x, y, c, a); }

static int clamp_r (int r, int w, int h)
{
	if (r > w / 2) r = w / 2;
	if (r > h / 2) r = h / 2;
	if (r > 16) r = 16;
	return r < 0 ? 0 : r;
}

void wk_rbox (Canvas &cv, int x, int y, int w, int h, int r, unsigned top, unsigned bottom,
	      int alpha, int corners)
{
	if (w <= 0 || h <= 0) return;
	r = clamp_r (r, w, h);
	const WkCorner &t = wk_corner (r > 0 ? r : 1);
	for (int j = 0; j < h; j++)
	{
		int yy = y + j;
		if (yy < 0 || yy >= cv.h) continue;
		unsigned c = h > 1 ? wk_mix (top, bottom, j * 256 / (h - 1)) : top;
		int cj = -1; bool bot = false;
		if (r > 0) { if (j < r) cj = j; else if (j >= h - r) { cj = h - 1 - j; bot = true; } }
		int lo = 0, ln = 0, ro = 0, rn = 0;			// each side: its offset, its edge pixels
		if (cj >= 0)
		{
			if (corners & (bot ? WK_BL : WK_TL)) { lo = t.off[cj]; ln = t.n[cj]; }
			if (corners & (bot ? WK_BR : WK_TR)) { ro = t.off[cj]; rn = t.n[cj]; }
		}
		for (int k = 0; k < ln; k++) blend_px (cv, x + lo + k, yy, c, t.a[cj][k] * alpha / 255);
		for (int k = 0; k < rn; k++) blend_px (cv, x + w - 1 - ro - k, yy, c, t.a[cj][k] * alpha / 255);
		int x0 = x + lo + ln, x1 = x + w - ro - rn;		// the straight span
		if (x0 < 0) x0 = 0;
		if (x1 > cv.w) x1 = cv.w;
		unsigned *p = cv.px + (long) yy * cv.stride;
		if (alpha >= 255) for (int xx = x0; xx < x1; xx++) p[xx] = c;
		else if (s_alphaMode) for (int xx = x0; xx < x1; xx++) blend_px (cv, xx, yy, c, alpha);
		else for (int xx = x0; xx < x1; xx++) p[xx] = wk_over (p[xx], c, alpha);
	}
}

void wk_rline (Canvas &cv, int x, int y, int w, int h, int r, unsigned c, int alpha, int corners)
{
	if (w <= 0 || h <= 0) return;
	r = clamp_r (r, w, h);
	int ri = r > 0 ? r - 1 : 0;
	for (int j = 0; j < h; j++)
	{
		int yy = y + j;
		if (yy < 0 || yy >= cv.h) continue;
		bool edgeRow = j <= r || j >= h - 1 - r;
		for (int i = 0; i < w; i++)
		{
			if (!edgeRow && i > r && i < w - 1 - r) { i = w - 2 - r; continue; }	// (the inside)
			int a = rcov (i, j, w, h, r, corners) - rcov (i - 1, j - 1, w - 2, h - 2, ri, corners);
			if (a > 0) blend_px (cv, x + i, yy, c, a * alpha / 255);
		}
	}
}

// ---- the look's pieces ----------------------------------------------------------------------------------
void wk_framed (Canvas &cv, int x, int y, int w, int h, unsigned face, int state,
		int *bx, int *by, int *bw, int *bh)
{
	int st = state & 15;
	int m = h < 18 || w < 18 ? 0 : h < 26 ? 1 : 2;		// the frame's size: small, medium, large
	static const int R0[3] = { 3, 5, 7 }, I1[3] = { 1, 2, 3 }, R1[3] = { 2, 4, 5 }, I2[3] = { 2, 3, 5 }, R2[3] = { 2, 3, 4 };
	// the frame: raised
	wk_rbox (cv, x, y, w, h, R0[m], wk_tone (face, 185), wk_tone (face, 110));
	wk_rline (cv, x, y, w, h, R0[m], wk_tone (face, 79), 200);
	// the well: sunken (dark at the top)
	int i1 = I1[m], i2 = I2[m];
	wk_rbox (cv, x + i1, y + i1, w - 2 * i1, h - 2 * i1, R1[m], wk_tone (face, 90), wk_tone (face, 172));
	// the button in it
	int X = x + i2, Y = y + i2, Wd = w - 2 * i2, Ht = h - 2 * i2;
	switch (st)
	{
	case WK_PRESSED:
		wk_rbox (cv, X, Y, Wd, Ht, R2[m], wk_tone (face, 108), wk_tone (face, 118));
		break;
	case WK_DISABLED:
		wk_rbox (cv, X, Y, Wd, Ht, R2[m], wk_tone (face, 150), wk_tone (face, 128));
		break;
	case WK_HOT:
		wk_rbox (cv, X, Y, Wd, Ht, R2[m], wk_tone (face, 212), wk_tone (face, 136));
		wk_rline (cv, X, Y, Wd, Ht, R2[m], 0x00FFFFFF, 110);
		break;
	default:
		wk_rbox (cv, X, Y, Wd, Ht, R2[m], wk_tone (face, 198), wk_tone (face, 122));
		wk_rline (cv, X, Y, Wd, Ht, R2[m], 0x00FFFFFF, 90);
		break;
	}
	if (state & WK_FOCUS) wk_rline (cv, x + i1, y + i1, w - 2 * i1, h - 2 * i1, R1[m], C_ACCENT, 255);
	int d = st == WK_PRESSED ? 1 : 0;
	if (bx) *bx = X + d;
	if (by) *by = Y + d;
	if (bw) *bw = Wd;
	if (bh) *bh = Ht;
}

void wk_raised (Canvas &cv, int x, int y, int w, int h, int r, unsigned face, int state)
{
	int t = 176, b = 112;
	if ((state & 15) == WK_HOT) { t = 192; b = 124; }
	else if ((state & 15) == WK_PRESSED) { t = 104; b = 120; }
	else if ((state & 15) == WK_DISABLED) { t = 150; b = 124; }
	wk_rbox (cv, x, y, w, h, r, wk_tone (face, t), wk_tone (face, b));
	wk_rline (cv, x, y, w, h, r, wk_tone (face, 70), 170);
	if ((state & 15) != WK_PRESSED && w > 2 * r + 2)
		for (int i = x + (r > 1 ? r : 1); i < x + w - (r > 1 ? r : 1); i++) blend_px (cv, i, y + 1, 0x00FFFFFF, 90);
	if (state & WK_FOCUS) wk_rline (cv, x + 1, y + 1, w - 2, h - 2, r > 1 ? r - 1 : 0, C_ACCENT, 255);
}

void wk_sunken (Canvas &cv, int x, int y, int w, int h, int r, unsigned bg, bool focus)
{
	wk_rbox (cv, x, y, w, h, r, bg, bg);
	for (int i = x + (r > 1 ? r : 1); i < x + w - (r > 1 ? r : 1); i++)	// a shadow along the top
	{
		blend_px (cv, i, y + 1, 0x00000000, 34);
		blend_px (cv, i, y + 2, 0x00000000, 14);
	}
	wk_rline (cv, x, y, w, h, r, focus ? C_ACCENT : wk_tone (C_FACE, 72), focus ? 255 : 210);
	if (focus) wk_rline (cv, x + 1, y + 1, w - 2, h - 2, r > 1 ? r - 1 : 0, C_ACCENT, 110);
}

void wk_etch_h (Canvas &cv, int x, int y, int w, unsigned face)
{
	for (int i = 0; i < w; i++) { blend_px (cv, x + i, y, wk_tone (face, 88), 255); blend_px (cv, x + i, y + 1, wk_tone (face, 196), 255); }
}
void wk_etch_v (Canvas &cv, int x, int y, int h, unsigned face)
{
	for (int j = 0; j < h; j++) { blend_px (cv, x, y + j, wk_tone (face, 88), 255); blend_px (cv, x + 1, y + j, wk_tone (face, 196), 255); }
}

void wk_etch_box (Canvas &cv, int x, int y, int w, int h, int r, unsigned face)
{
	wk_rline (cv, x + 1, y + 1, w - 1, h - 1, r, wk_tone (face, 200), 255);
	wk_rline (cv, x, y, w - 1, h - 1, r, wk_tone (face, 88), 255);
}

// ---- the controls' marks ----------------------------------------------------------------------------------
static void mark_field (Canvas &cv, int x, int y, int w, int h, int r, int st, bool focus)
{
	unsigned bg = st == WK_DISABLED ? wk_tone (C_FACE, 150) : st == WK_PRESSED ? wk_tone (C_FIELD, 118) : C_FIELD;
	wk_rbox (cv, x, y, w, h, r, st == WK_DISABLED ? bg : wk_tone (bg, 112), bg);
	unsigned ol = focus || st == WK_HOT ? C_ACCENT : wk_tone (C_FACE, 72);
	wk_rline (cv, x, y, w, h, r, ol, focus ? 255 : st == WK_HOT ? 200 : st == WK_DISABLED ? 120 : 210);
}

static void mark_on (Canvas &cv, int x, int y, int w, int h, int r, int st, bool focus)
{
	unsigned a = st == WK_DISABLED ? wk_mix (C_ACCENT, C_FACE, 150) : C_ACCENT;
	int lift = st == WK_HOT ? 14 : st == WK_PRESSED ? -12 : 0;
	wk_rbox (cv, x, y, w, h, r, wk_tone (a, 156 + lift), wk_tone (a, 112 + lift));
	wk_rline (cv, x, y, w, h, r, focus ? wk_tone (a, 40) : wk_tone (a, 70), focus ? 255 : 220);
}

void wk_check_mark (Canvas &cv, int x, int y, int s, bool checked, int state)
{
	int st = state & 15, r = s / 5 < 2 ? 2 : s / 5;
	bool focus = (state & WK_FOCUS) != 0;
	if (checked)
	{
		mark_on (cv, x, y, s, s, r, st, focus);
		wk_glyph (cv, WKG_CHECK, x + s / 2, y + s / 2, s * 3 / 4, st == WK_DISABLED ? C_FIELD : 0x00FFFFFF);
	}
	else mark_field (cv, x, y, s, s, r, st, focus);
}

void wk_radio_mark (Canvas &cv, int x, int y, int s, bool checked, int state)
{
	int st = state & 15;
	bool focus = (state & WK_FOCUS) != 0;
	if (checked)
	{
		mark_on (cv, x, y, s, s, s / 2, st, focus);
		int d = s * 2 / 5 < 4 ? 4 : s * 2 / 5;
		wk_rbox (cv, x + (s - d) / 2, y + (s - d) / 2, d, d, d / 2, 0x00FFFFFF, wk_tone (C_FIELD, 118));
	}
	else mark_field (cv, x, y, s, s, s / 2, st, focus);
}

void wk_switch_mark (Canvas &cv, int x, int y, int w, int h, bool on, int state)
{
	int st = state & 15, r = h / 2;
	bool focus = (state & WK_FOCUS) != 0;
	if (on) mark_on (cv, x, y, w, h, r, st, focus);
	else
	{
		unsigned t = st == WK_DISABLED ? wk_tone (C_FACE, 150) : wk_tone (C_FACE, 104);
		wk_rbox (cv, x, y, w, h, r, wk_tone (t, 96), t);
		wk_rline (cv, x, y, w, h, r, focus ? C_ACCENT : wk_tone (C_FACE, 66), focus ? 255 : 200);
	}
	int d = h - 4, kx = on ? x + w - 2 - d : x + 2;			// the knob
	unsigned k = st == WK_DISABLED ? wk_tone (C_FACE, 190) : st == WK_HOT ? 0x00FFFFFF : wk_tone (C_FIELD, 132);
	wk_rbox (cv, kx, y + 2, d, d, d / 2, k, wk_tone (C_FACE, 172));
	wk_rline (cv, kx, y + 2, d, d, d / 2, wk_tone (C_FACE, 70), 150);
}

void wk_scroll_bar (Canvas &cv, int x, int y, int w, int h, bool vertical, int pos, int len,
		    unsigned bg, int state)
{
	int across = vertical ? w : h;
	int r = across / 2;
	wk_rbox (cv, x, y, w, h, r, wk_mix (bg, 0, 34), wk_mix (bg, 0, 18));	// the groove
	if (len <= 0) return;
	int t = (state & 15) == WK_HOT || (state & 15) == WK_PRESSED;
	unsigned f = t ? wk_mix (C_FACE, C_ACCENT, 70) : C_FACE;
	int tx = vertical ? x + 1 : x + pos, ty = vertical ? y + pos : y + 1;
	int tw = vertical ? w - 2 : len, th = vertical ? len : h - 2;
	int tr = (across - 2) / 2;
	wk_rbox (cv, tx, ty, tw, th, tr, wk_tone (f, vertical ? 170 : 176), wk_tone (f, vertical ? 128 : 112));
	wk_rline (cv, tx, ty, tw, th, tr, wk_tone (C_FACE, 70), 160);
}

void wk_slider_mark (Canvas &cv, int x, int y, int w, int h, int fill, int kx, int kw, int state)
{
	int st = state & 15, gh = h < 12 ? 4 : 6, gy = y + (h - gh) / 2;
	wk_rbox (cv, x, gy, w, gh, gh / 2, wk_tone (C_FACE, 92), wk_tone (C_FACE, 140));
	if (fill > 0)
	{
		unsigned a = st == WK_DISABLED ? wk_mix (C_ACCENT, C_FACE, 150) : C_ACCENT;
		wk_rbox (cv, x, gy, fill < gh ? gh : fill, gh, gh / 2, wk_tone (a, 150), wk_tone (a, 108));
	}
	wk_raised (cv, kx, y + 1, kw, h - 2, kw / 2 < 6 ? kw / 2 : 5, C_FACE, state);
}

void wk_progress_bar (Canvas &cv, int x, int y, int w, int h, int fill)
{
	int r = h / 2 < 5 ? h / 2 : 5;
	wk_sunken (cv, x, y, w, h, r, C_FIELD, false);
	if (fill > 2)
	{
		int fw = fill - 2 < 2 * (r - 1) ? 2 * (r - 1) : fill - 2;
		if (fw > w - 2) fw = w - 2;
		wk_rbox (cv, x + 1, y + 1, fw, h - 2, r > 1 ? r - 1 : 0, wk_tone (C_ACCENT, 168), wk_tone (C_ACCENT, 104));
	}
}

void wk_corner_key (Canvas &cv, int x, int y, int w, int h, int r)
{
	r = clamp_r (r, w, h);
	if (r <= 0) return;
	const WkCorner &t = wk_corner (r);
	for (int j = 0; j < r; j++)
	{
		int n = t.off[j];
		while (n < t.off[j] + t.n[j] && t.a[j][n - t.off[j]] < 128) n++;	// the mostly-outside ones
		for (int i = 0; i < n; i++)
		{
			cv.pixel (x + i, y + j, WK_TRANSPARENT_KEY);
			cv.pixel (x + w - 1 - i, y + j, WK_TRANSPARENT_KEY);
			cv.pixel (x + i, y + h - 1 - j, WK_TRANSPARENT_KEY);
			cv.pixel (x + w - 1 - i, y + h - 1 - j, WK_TRANSPARENT_KEY);
		}
	}
}

void wk_popup (Canvas &cv, int x, int y, int w, int h, int r, unsigned face)
{
	unsigned ol = wk_tone (C_FACE, 70);
	cv.fillRect (x, y, w, h, ol);
	wk_rbox (cv, x, y, w, h, r, wk_tone (face, 140), face);
	wk_rline (cv, x, y, w, h, r, ol, 230);
	wk_corner_key (cv, x, y, w, h, r);
}

void wk_hilite (Canvas &cv, int x, int y, int w, int h, int r, bool strong)
{
	if (strong) wk_rbox (cv, x, y, w, h, r, wk_tone (C_ACCENT, 142), wk_tone (C_ACCENT, 118));
	else wk_rbox (cv, x, y, w, h, r, wk_mix (C_FIELD, C_ACCENT, 76), wk_mix (C_FIELD, C_ACCENT, 92));
}

unsigned wk_hilite_ink (bool strong) { return strong ? C_SEL_TEXT : C_FIELD_TEXT; }

// The bead: each pixel's coverage of the disc (and of the disc 1 px smaller: the rim between
// them) counted on 4 x 4 samples; in half-pixel units, its centre at (d, d), its radius d.
void wk_bead (Canvas &cv, int x, int y, int d, unsigned c)
{
	if (d < 4) return;
	const int R2o = (4 * d) * (4 * d), R2i = (4 * d - 8) * (4 * d - 8);	// (eighth-pixel units)
	const int hy0 = d / 10, hy1 = d * 11 / 20;			// the gloss: these rows, an ellipse
	const int ax = d * 4 * 34 / 100, ay = (hy1 - hy0) * 4;		// its half-axes (eighths)
	const int hcx = 4 * d, hcy = (hy0 + hy1) * 4;			// its centre (eighths)
	unsigned top = wk_tone (c, 82), bottom = wk_tone (c, 158), rim = wk_tone (c, 52);
	for (int j = 0; j < d; j++)
	{
		unsigned body = wk_mix (top, bottom, j * 256 / (d - 1));
		for (int i = 0; i < d; i++)
		{
			int co = 0, ci = 0, ch = 0;
			for (int sy = 0; sy < 4; sy++)
				for (int sx = 0; sx < 4; sx++)
				{
					int X = 8 * i + 2 * sx + 1 - 4 * d, Y = 8 * j + 2 * sy + 1 - 4 * d;
					int r2 = X * X + Y * Y;
					if (r2 <= R2o) co++;
					if (r2 <= R2i) ci++;
					int ex = 8 * i + 2 * sx + 1 - hcx, ey = 8 * j + 2 * sy + 1 - hcy;
					if ((long) ex * ex * ay * ay + (long) ey * ey * ax * ax <= (long) ax * ax * ay * ay) ch++;
				}
			if (co == 0) continue;
			unsigned px = body;
			if (ch)						// the gloss: white, fading downward
			{
				int a = 230 - (j - hy0) * 200 / (hy1 - hy0 + 1);
				if (a > 0) px = wk_over (px, 0x00FFFFFF, a * ch / 16);
			}
			if (co > ci) px = wk_mix (px, rim, (co - ci) * 256 / co);	// the rim
			blend_px (cv, x + i, y + j, px, co * 255 / 16);
		}
	}
}

void wk_title_strip (Canvas &cv, int x, int y, int w, int h, const char *s, int r)
{
	unsigned f = C_FRAME_ACTIVE, top = wk_tone (f, 164), bot = wk_tone (f, 115), mid = wk_tone (f, 140);
	if (WK_STYLE == WK_STYLE_MILK)					// (as the frames' title bars: Milk's
	{ top = wk_tone (f, 230); bot = C_FACE; mid = wk_mix (top, bot, 128); }	//  melts into the box's face)
	wk_rbox (cv, x, y, w, h, r, top, bot, 255, WK_TL | WK_TR);
	for (int i = x + (r > 1 ? r : 1); i < x + w - (r > 1 ? r : 1); i++) blend_px (cv, i, y + 1, 0x00FFFFFF, 110);
	if (s) wk_text_c (cv, x, y, w, h, s, wk_ink_on (mid), 2);
}

// ---- glyphs -------------------------------------------------------------------------------------------
// Distance tests in 1/16 px, the glyph's centre at (0, 0).
static long seg_d2 (long px, long py, long ax, long ay, long bx, long by)
{
	long vx = bx - ax, vy = by - ay, wx = px - ax, wy = py - ay;
	long num = wx * vx + wy * vy, den = vx * vx + vy * vy;
	if (num <= 0 || den == 0) return wx * wx + wy * wy;
	if (num >= den) { long ex = px - bx, ey = py - by; return ex * ex + ey * ey; }
	return wx * wx + wy * wy - num * num / den;
}
static bool in_tri (long px, long py, long ax, long ay, long bx, long by, long cx, long cy)
{
	long d1 = (px - bx) * (ay - by) - (ax - bx) * (py - by);
	long d2 = (px - cx) * (by - cy) - (bx - cx) * (py - cy);
	long d3 = (px - ax) * (cy - ay) - (cx - ax) * (py - ay);
	bool neg = d1 < 0 || d2 < 0 || d3 < 0, pos = d1 > 0 || d2 > 0 || d3 > 0;
	return !(neg && pos);
}
static bool glyph_in (int kind, long px, long py, long s)	// s: the glyph's size, 1/16 px
{
	long t = s / 7 < 22 ? 22 : s / 7;				// a stroke: ~1.4 px or more
	long t2 = (t / 2) * (t / 2);
	switch (kind)
	{
	case WKG_CHECK:
		return seg_d2 (px, py, -s * 36 / 100, 0, -s * 10 / 100, s * 26 / 100) <= t2
		    || seg_d2 (px, py, -s * 10 / 100, s * 26 / 100, s * 40 / 100, -s * 30 / 100) <= t2;
	case WKG_UP:    return in_tri (px, py, -s / 2, s / 4, s / 2, s / 4, 0, -s / 4);
	case WKG_DOWN:  return in_tri (px, py, -s / 2, -s / 4, s / 2, -s / 4, 0, s / 4);
	case WKG_LEFT:  return in_tri (px, py, s / 4, -s / 2, s / 4, s / 2, -s / 4, 0);
	case WKG_RIGHT: return in_tri (px, py, -s / 4, -s / 2, -s / 4, s / 2, s / 4, 0);
	case WKG_CLOSE:
		return seg_d2 (px, py, -s * 38 / 100, -s * 38 / 100, s * 38 / 100, s * 38 / 100) <= t2
		    || seg_d2 (px, py, s * 38 / 100, -s * 38 / 100, -s * 38 / 100, s * 38 / 100) <= t2;
	case WKG_MIN:   return px >= -s * 30 / 100 && px <= s * 30 / 100 && py >= s * 12 / 100 && py <= s * 12 / 100 + t + 8;
	case WKG_MAX:
	{
		long a = s * 40 / 100;
		bool out = px >= -a && px <= a && py >= -a && py <= a;
		bool in = px > -a + t && px < a - t && py > -a + t + 8 && py < a - t;
		return out && !in;
	}
	case WKG_MENU:  return px >= -s * 40 / 100 && px <= s * 40 / 100 && py >= -s * 12 / 100 && py <= s * 12 / 100;
	case WKG_RESTORE:
	{
		long o = s * 30 / 100, lo = -s * 42 / 100, hi = s * 42 / 100;
		bool fOut = px >= lo && px <= hi - o && py >= lo + o && py <= hi;
		bool fIn = px > lo + t && px < hi - o - t && py > lo + o + t + 8 && py < hi - t;
		bool bOut = px >= lo + o && px <= hi && py >= lo && py <= hi - o;
		bool bIn = px > lo + o + t && px < hi - t && py > lo + t && py < hi - o - t;
		return (fOut && !fIn) || (bOut && !bIn && !fOut);
	}
	case WKG_CHEV_UP:
		return seg_d2 (px, py, -s * 45 / 100, s * 22 / 100, 0, -s * 22 / 100) <= t2
		    || seg_d2 (px, py, 0, -s * 22 / 100, s * 45 / 100, s * 22 / 100) <= t2;
	case WKG_CHEV_DOWN:
		return seg_d2 (px, py, -s * 45 / 100, -s * 22 / 100, 0, s * 22 / 100) <= t2
		    || seg_d2 (px, py, 0, s * 22 / 100, s * 45 / 100, -s * 22 / 100) <= t2;
	case WKG_CHEV_LEFT:
		return seg_d2 (px, py, s * 22 / 100, -s * 45 / 100, -s * 22 / 100, 0) <= t2
		    || seg_d2 (px, py, -s * 22 / 100, 0, s * 22 / 100, s * 45 / 100) <= t2;
	case WKG_CHEV_RIGHT:
		return seg_d2 (px, py, -s * 22 / 100, -s * 45 / 100, s * 22 / 100, 0) <= t2
		    || seg_d2 (px, py, s * 22 / 100, 0, -s * 22 / 100, s * 45 / 100) <= t2;
	case WKG_RING:
	{
		long R = s / 2 - t / 2, d2 = px * px + py * py;
		long lo = R - t / 2, hi = R + t / 2;
		return d2 >= lo * lo && d2 <= hi * hi;
	}
	case WKG_LOCK:
	{
		long bx = s * 40 / 100, R = s * 24 / 100, cy = -s * 8 / 100;
		if (px >= -bx && px <= bx && py >= -s * 4 / 100 && py <= s / 2) return true;	// the body
		long d2 = px * px + (py - cy) * (py - cy), lo = R - t / 2, hi = R + t / 2;	// the shackle
		if (py <= cy && d2 >= lo * lo && d2 <= hi * hi) return true;
		return py > cy && py < 0 && ((px >= R - t / 2 && px <= R + t / 2) || (px >= -R - t / 2 && px <= -R + t / 2));
	}
	case WKG_GEAR:
	{
		long d2 = px * px + py * py, hole = s * 15 / 100, body = s * 34 / 100;
		if (d2 < hole * hole) return false;
		if (d2 <= body * body) return true;
		long L = s * 48 / 100, W = s * 10 / 100;		// four bars through the centre: 8 teeth
		long u = (px + py) * 181 / 256, v = (py - px) * 181 / 256;		// (45 degrees turned)
		return (px >= -L && px <= L && py >= -W && py <= W) || (py >= -L && py <= L && px >= -W && px <= W)
		    || (u >= -L && u <= L && v >= -W && v <= W) || (v >= -L && v <= L && u >= -W && u <= W);
	}
	case WKG_POWER:
	{
		long R = s * 38 / 100, d2 = px * px + py * py, lo = R - t / 2, hi = R + t / 2;
		bool ring = d2 >= lo * lo && d2 <= hi * hi && !(py < 0 && px > -s * 20 / 100 && px < s * 20 / 100);
		bool bar = px >= -t / 2 && px <= t / 2 && py >= -s / 2 && py <= 0;
		return ring || bar;
	}
	case WKG_DOT:   return px * px + py * py <= (s / 2) * (s / 2);
	case WKG_PLUS:  return (px >= -s / 2 && px <= s / 2 && py >= -t / 2 && py <= t / 2) || (py >= -s / 2 && py <= s / 2 && px >= -t / 2 && px <= t / 2);
	case WKG_MINUS: return px >= -s / 2 && px <= s / 2 && py >= -t / 2 && py <= t / 2;
	case WKG_RELOAD:
	{
		// a ring open at the top right, its upper end an arrowhead pointing clockwise
		long R = s * 34 / 100, d2 = px * px + py * py, lo = R - t / 2, hi = R + t / 2;
		bool gap = px > 0 && py < 0 && -py < px * 3;			// (the top-right opening)
		if (d2 >= lo * lo && d2 <= hi * hi && !gap) return true;
		long ax = R / 3, ay = -R;					// the arrow at the arc's upper end
		return in_tri (px, py, ax - s * 6 / 100, ay - s * 20 / 100, ax - s * 6 / 100, ay + s * 20 / 100,
			       ax + s * 22 / 100, ay);
	}
	case WKG_HOME:
	{
		// a house: the roof, the walls, a door
		if (in_tri (px, py, -s * 50 / 100, -s * 2 / 100, s * 50 / 100, -s * 2 / 100, 0, -s * 48 / 100)) return true;
		bool body = px >= -s * 32 / 100 && px <= s * 32 / 100 && py >= -s * 4 / 100 && py <= s * 42 / 100;
		bool door = px > -s * 10 / 100 && px < s * 10 / 100 && py > s * 12 / 100;
		return body && !door;
	}
	case WKG_HISTORY:
	{
		// a clock: its face a ring, the hands at three o'clock (the minute hand up, the hour
		// hand to the right)
		long R = s * 42 / 100, d2 = px * px + py * py, lo = R - t / 2, hi = R + t / 2;
		if (d2 >= lo * lo && d2 <= hi * hi) return true;
		return seg_d2 (px, py, 0, 0, 0, -R * 68 / 100) <= t2 || seg_d2 (px, py, 0, 0, R * 52 / 100, 0) <= t2;
	}
	}
	return false;
}

void wk_glyph (Canvas &cv, int kind, int cx, int cy, int size, unsigned c)
{
	long s = (long) size * 16;
	int half = size / 2 + 2;
	for (int y = cy - half; y <= cy + half; y++)
		for (int x = cx - half; x <= cx + half; x++)
		{
			int n = 0;
			for (int sy = 0; sy < 4; sy++)
				for (int sx = 0; sx < 4; sx++)
					if (glyph_in (kind, (x - cx) * 16 + sx * 4 + 2 - 8, (y - cy) * 16 + sy * 4 + 2 - 8, s)) n++;
			if (n) blend_px (cv, x, y, c, n * 255 / 16);
		}
}

// ---- text -----------------------------------------------------------------------------------------------
int wk_text_w (const char *s, int style)
{
	if (wk_face_) return s ? wk_face_->width (s, style) : 0;	// an app's face (wtk/text.h)
	Font &f = font ();
	return wk_len (s) * (f.valid () ? f.width () : wk_fw ());
}
static int text_h (void) { Font &f = font (); return f.valid () ? f.height () : wk_fh (); }

void wk_text_l (Canvas &cv, int x, int y, int h, const char *s, unsigned c, int style)
{
	if (wk_face_) { if (s) wk_face_->draw (cv, x, y + (h - wk_face_->height ()) / 2, s, c, style); return; }
	Font &f = font ();
	int ty = y + (h - text_h ()) / 2;
	if (f.valid ()) cv.drawFont (x, ty, s, f, c, 1, style);
	else cv.text (x, ty, s, c);
}

void wk_text_c (Canvas &cv, int x, int y, int w, int h, const char *s, unsigned c, int style)
{
	wk_text_l (cv, x + (w - wk_text_w (s, style)) / 2, y, h, s, c, style);
}

} // namespace wtk
