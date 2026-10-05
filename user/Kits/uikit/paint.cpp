//
// uikit/paint.cpp -- the procedural painter (uikit/paint.h): shades, gradients, rounded boxes from
// per-radius corner tables, the push button, raised / sunken faces, glyphs. Integer only.
//
#include "uikit/paint.h"
#include "uikit/theme.h"
#include "uikit/font.h"
#include "uikit/widget.h"

namespace uikit {

// ---- colours ----------------------------------------------------------------------------------------
unsigned uk_tone (unsigned c, int level)
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

unsigned uk_mix (unsigned a, unsigned b, int t)
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

int uk_bright (unsigned c)
{
	return (int) ((((c >> 16) & 0xFF) * 77 + ((c >> 8) & 0xFF) * 151 + (c & 0xFF) * 28) >> 8);
}

unsigned uk_ink_on (unsigned c) { return uk_bright (c) > 140 ? 0x00201C1Au : 0x00FFFFFFu; }
unsigned uk_ink_for (unsigned bg) { return uk_ink_on (bg) == uk_ink_on (C_BG) ? C_TEXT : uk_ink_on (bg); }

// ---- the corner tables --------------------------------------------------------------------------------
const UkCorner &uk_corner (int r)
{
	static UkCorner s_t[17];
	static bool s_done[17];
	if (r < 1) r = 1;
	if (r > 16) r = 16;
	if (!s_done[r])
	{
		UkCorner &t = s_t[r];
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
	int which = bottom ? (right ? UK_BR : UK_BL) : (right ? UK_TR : UK_TL);
	if (!(corners & which)) return 255;
	const UkCorner &t = uk_corner (r);
	if (ci < t.off[cj]) return 0;
	if (ci < t.off[cj] + t.n[cj]) return t.a[cj][ci - t.off[cj]];
	return 255;
}

// The alpha mode (uk_paint_alpha): the canvas's pixels carry a transparency in their top byte (a
// WIN_FLAG_ALPHA window: the dock, the agenda); a blend over a see-through pixel then leaves the
// colour itself, partly see-through -- not the colour darkened by the black below it.
static bool s_alphaMode = false;
void uk_paint_alpha (bool on) { s_alphaMode = on; }

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
	*p = uk_over (*p, c, a);
}

void uk_blend_px (Canvas &cv, int x, int y, unsigned c, int a) { blend_px (cv, x, y, c, a); }

static int clamp_r (int r, int w, int h)
{
	if (r > w / 2) r = w / 2;
	if (r > h / 2) r = h / 2;
	if (r > 16) r = 16;
	return r < 0 ? 0 : r;
}

void uk_rbox (Canvas &cv, int x, int y, int w, int h, int r, unsigned top, unsigned bottom,
	      int alpha, int corners)
{
	if (w <= 0 || h <= 0) return;
	r = clamp_r (r, w, h);
	const UkCorner &t = uk_corner (r > 0 ? r : 1);
	for (int j = 0; j < h; j++)
	{
		int yy = y + j;
		if (yy < 0 || yy >= cv.h) continue;
		unsigned c = h > 1 ? uk_mix (top, bottom, j * 256 / (h - 1)) : top;
		int cj = -1; bool bot = false;
		if (r > 0) { if (j < r) cj = j; else if (j >= h - r) { cj = h - 1 - j; bot = true; } }
		int lo = 0, ln = 0, ro = 0, rn = 0;			// each side: its offset, its edge pixels
		if (cj >= 0)
		{
			if (corners & (bot ? UK_BL : UK_TL)) { lo = t.off[cj]; ln = t.n[cj]; }
			if (corners & (bot ? UK_BR : UK_TR)) { ro = t.off[cj]; rn = t.n[cj]; }
		}
		for (int k = 0; k < ln; k++) blend_px (cv, x + lo + k, yy, c, t.a[cj][k] * alpha / 255);
		for (int k = 0; k < rn; k++) blend_px (cv, x + w - 1 - ro - k, yy, c, t.a[cj][k] * alpha / 255);
		int x0 = x + lo + ln, x1 = x + w - ro - rn;		// the straight span
		if (x0 < 0) x0 = 0;
		if (x1 > cv.w) x1 = cv.w;
		unsigned *p = cv.px + (long) yy * cv.stride;
		if (alpha >= 255) for (int xx = x0; xx < x1; xx++) p[xx] = c;
		else if (s_alphaMode) for (int xx = x0; xx < x1; xx++) blend_px (cv, xx, yy, c, alpha);
		else for (int xx = x0; xx < x1; xx++) p[xx] = uk_over (p[xx], c, alpha);
	}
}

void uk_rline (Canvas &cv, int x, int y, int w, int h, int r, unsigned c, int alpha, int corners)
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
void uk_framed (Canvas &cv, int x, int y, int w, int h, unsigned face, int state,
		int *bx, int *by, int *bw, int *bh)
{
	// (no frame, no well any more: the drop-down's raised face, the whole box the button's)
	uk_raised (cv, x, y, w, h, h < 18 || w < 18 ? 3 : 5, face, state);
	int d = (state & 15) == UK_PRESSED ? 1 : 0;
	if (bx) *bx = x + d;
	if (by) *by = y + d;
	if (bw) *bw = w;
	if (bh) *bh = h;
}

void uk_raised (Canvas &cv, int x, int y, int w, int h, int r, unsigned face, int state)
{
	int t = 176, b = 112;
	if ((state & 15) == UK_HOT) { t = 192; b = 124; }
	else if ((state & 15) == UK_PRESSED) { t = 104; b = 120; }
	else if ((state & 15) == UK_DISABLED) { t = 150; b = 124; }
	uk_rbox (cv, x, y, w, h, r, uk_tone (face, t), uk_tone (face, b));
	uk_rline (cv, x, y, w, h, r, uk_tone (face, 70), 170);
	if ((state & 15) != UK_PRESSED && w > 2 * r + 2)
		for (int i = x + (r > 1 ? r : 1); i < x + w - (r > 1 ? r : 1); i++) blend_px (cv, i, y + 1, 0x00FFFFFF, 90);
	if (state & UK_FOCUS) uk_rline (cv, x + 1, y + 1, w - 2, h - 2, r > 1 ? r - 1 : 0, C_ACCENT, 255);
}

void uk_sunken (Canvas &cv, int x, int y, int w, int h, int r, unsigned bg, bool focus)
{
	uk_rbox (cv, x, y, w, h, r, bg, bg);
	for (int i = x + (r > 1 ? r : 1); i < x + w - (r > 1 ? r : 1); i++)	// a shadow along the top
	{
		blend_px (cv, i, y + 1, 0x00000000, 34);
		blend_px (cv, i, y + 2, 0x00000000, 14);
	}
	uk_rline (cv, x, y, w, h, r, focus ? C_ACCENT : uk_tone (C_FACE, 72), focus ? 255 : 210);
	if (focus) uk_rline (cv, x + 1, y + 1, w - 2, h - 2, r > 1 ? r - 1 : 0, C_ACCENT, 110);
}

// (a groove's two lines on a face: dark over light -- on a dark face both darker than it)
static inline unsigned etch_dark (unsigned face) { return uk_tone (face, uk_bright (face) < 110 ? 40 : 88); }
static inline unsigned etch_light (unsigned face) { return uk_tone (face, uk_bright (face) < 110 ? 96 : 196); }

void uk_etch_h (Canvas &cv, int x, int y, int w, unsigned face)
{
	for (int i = 0; i < w; i++) { blend_px (cv, x + i, y, etch_dark (face), 255); blend_px (cv, x + i, y + 1, etch_light (face), 255); }
}
void uk_etch_v (Canvas &cv, int x, int y, int h, unsigned face)
{
	for (int j = 0; j < h; j++) { blend_px (cv, x, y + j, etch_dark (face), 255); blend_px (cv, x + 1, y + j, etch_light (face), 255); }
}

void uk_etch_box (Canvas &cv, int x, int y, int w, int h, int r, unsigned face)
{
	uk_rline (cv, x + 1, y + 1, w - 1, h - 1, r, uk_bright (face) < 110 ? etch_light (face) : uk_tone (face, 200), 255);
	uk_rline (cv, x, y, w - 1, h - 1, r, etch_dark (face), 255);
}

// ---- the controls' marks ----------------------------------------------------------------------------------
static void mark_field (Canvas &cv, int x, int y, int w, int h, int r, int st, bool focus)
{
	unsigned bg = st == UK_DISABLED ? uk_tone (C_FACE, 150) : st == UK_PRESSED ? uk_tone (C_FIELD, 118) : C_FIELD;
	uk_rbox (cv, x, y, w, h, r, st == UK_DISABLED ? bg : uk_tone (bg, 112), bg);
	unsigned ol = focus || st == UK_HOT ? C_ACCENT : uk_tone (C_FACE, 72);
	uk_rline (cv, x, y, w, h, r, ol, focus ? 255 : st == UK_HOT ? 200 : st == UK_DISABLED ? 120 : 210);
}

static void mark_on (Canvas &cv, int x, int y, int w, int h, int r, int st, bool focus)
{
	unsigned a = st == UK_DISABLED ? uk_mix (C_ACCENT, C_FACE, 150) : C_ACCENT;
	int lift = st == UK_HOT ? 14 : st == UK_PRESSED ? -12 : 0;
	uk_rbox (cv, x, y, w, h, r, uk_tone (a, 156 + lift), uk_tone (a, 112 + lift));
	uk_rline (cv, x, y, w, h, r, focus ? uk_tone (a, 40) : uk_tone (a, 70), focus ? 255 : 220);
}

void uk_check_mark (Canvas &cv, int x, int y, int s, bool checked, int state)
{
	int st = state & 15, r = s / 5 < 2 ? 2 : s / 5;
	bool focus = (state & UK_FOCUS) != 0;
	if (checked)
	{
		mark_on (cv, x, y, s, s, r, st, focus);
		uk_glyph (cv, WKG_CHECK, x + s / 2, y + s / 2, s * 3 / 4, st == UK_DISABLED ? C_FIELD : 0x00FFFFFF);
	}
	else mark_field (cv, x, y, s, s, r, st, focus);
}

void uk_radio_mark (Canvas &cv, int x, int y, int s, bool checked, int state)
{
	int st = state & 15;
	bool focus = (state & UK_FOCUS) != 0;
	if (checked)
	{
		mark_on (cv, x, y, s, s, s / 2, st, focus);
		int d = s * 2 / 5 < 4 ? 4 : s * 2 / 5;
		uk_rbox (cv, x + (s - d) / 2, y + (s - d) / 2, d, d, d / 2, 0x00FFFFFF, uk_tone (C_FIELD, 118));
	}
	else mark_field (cv, x, y, s, s, s / 2, st, focus);
}

void uk_switch_mark (Canvas &cv, int x, int y, int w, int h, bool on, int state)
{
	int st = state & 15, r = h / 2;
	bool focus = (state & UK_FOCUS) != 0;
	if (on) mark_on (cv, x, y, w, h, r, st, focus);
	else
	{
		unsigned t = st == UK_DISABLED ? uk_tone (C_FACE, 150) : uk_tone (C_FACE, 104);
		uk_rbox (cv, x, y, w, h, r, uk_tone (t, 96), t);
		uk_rline (cv, x, y, w, h, r, focus ? C_ACCENT : uk_tone (C_FACE, 66), focus ? 255 : 200);
	}
	int d = h - 4, kx = on ? x + w - 2 - d : x + 2;			// the knob
	unsigned k = st == UK_DISABLED ? uk_tone (C_FACE, 190) : st == UK_HOT ? 0x00FFFFFF : uk_tone (C_FIELD, 132);
	uk_rbox (cv, kx, y + 2, d, d, d / 2, k, uk_tone (C_FACE, 172));
	uk_rline (cv, kx, y + 2, d, d, d / 2, uk_tone (C_FACE, 70), 150);
}

void uk_scroll_bar (Canvas &cv, int x, int y, int w, int h, bool vertical, int pos, int len,
		    unsigned bg, int state)
{
	int across = vertical ? w : h;
	int r = across / 2;
	uk_rbox (cv, x, y, w, h, r, uk_mix (bg, 0, 34), uk_mix (bg, 0, 18));	// the groove
	if (len <= 0) return;
	int t = (state & 15) == UK_HOT || (state & 15) == UK_PRESSED;
	unsigned f = t ? uk_mix (C_FACE, C_ACCENT, 70) : C_FACE;
	int tx = vertical ? x + 1 : x + pos, ty = vertical ? y + pos : y + 1;
	int tw = vertical ? w - 2 : len, th = vertical ? len : h - 2;
	int tr = (across - 2) / 2;
	uk_rbox (cv, tx, ty, tw, th, tr, uk_tone (f, vertical ? 170 : 176), uk_tone (f, vertical ? 128 : 112));
	uk_rline (cv, tx, ty, tw, th, tr, uk_tone (C_FACE, 70), 160);
}

void uk_slider_mark (Canvas &cv, int x, int y, int w, int h, int fill, int kx, int kw, int state)
{
	int st = state & 15, gh = h < 12 ? 4 : 6, gy = y + (h - gh) / 2;
	uk_rbox (cv, x, gy, w, gh, gh / 2, uk_tone (C_FACE, 92), uk_tone (C_FACE, 140));
	if (fill > 0)
	{
		unsigned a = st == UK_DISABLED ? uk_mix (C_ACCENT, C_FACE, 150) : C_ACCENT;
		uk_rbox (cv, x, gy, fill < gh ? gh : fill, gh, gh / 2, uk_tone (a, 150), uk_tone (a, 108));
	}
	uk_raised (cv, kx, y + 1, kw, h - 2, kw / 2 < 6 ? kw / 2 : 5, C_FACE, state);
}

void uk_progress_bar (Canvas &cv, int x, int y, int w, int h, int fill)
{
	int r = h / 2 < 5 ? h / 2 : 5;
	uk_sunken (cv, x, y, w, h, r, C_FIELD, false);
	if (fill > 2)
	{
		int fw = fill - 2 < 2 * (r - 1) ? 2 * (r - 1) : fill - 2;
		if (fw > w - 2) fw = w - 2;
		uk_rbox (cv, x + 1, y + 1, fw, h - 2, r > 1 ? r - 1 : 0, uk_tone (C_ACCENT, 168), uk_tone (C_ACCENT, 104));
	}
}

void uk_corner_key (Canvas &cv, int x, int y, int w, int h, int r)
{
	r = clamp_r (r, w, h);
	if (r <= 0) return;
	const UkCorner &t = uk_corner (r);
	for (int j = 0; j < r; j++)
	{
		int n = t.off[j];
		while (n < t.off[j] + t.n[j] && t.a[j][n - t.off[j]] < 128) n++;	// the mostly-outside ones
		for (int i = 0; i < n; i++)
		{
			cv.pixel (x + i, y + j, UK_TRANSPARENT_KEY);
			cv.pixel (x + w - 1 - i, y + j, UK_TRANSPARENT_KEY);
			cv.pixel (x + i, y + h - 1 - j, UK_TRANSPARENT_KEY);
			cv.pixel (x + w - 1 - i, y + h - 1 - j, UK_TRANSPARENT_KEY);
		}
	}
}

void uk_popup (Canvas &cv, int x, int y, int w, int h, int r, unsigned face)
{
	unsigned ol = uk_tone (C_FACE, 70);
	cv.fillRect (x, y, w, h, ol);
	uk_rbox (cv, x, y, w, h, r, uk_tone (face, 140), face);
	uk_rline (cv, x, y, w, h, r, ol, 230);
	uk_corner_key (cv, x, y, w, h, r);
}

void uk_hilite (Canvas &cv, int x, int y, int w, int h, int r, bool strong)
{
	if (strong) uk_rbox (cv, x, y, w, h, r, uk_tone (C_ACCENT, 142), uk_tone (C_ACCENT, 118));
	else uk_rbox (cv, x, y, w, h, r, uk_mix (C_FIELD, C_ACCENT, 76), uk_mix (C_FIELD, C_ACCENT, 92));
}

unsigned uk_hilite_ink (bool strong) { return strong ? C_SEL_TEXT : C_FIELD_TEXT; }

// The bead: each pixel's coverage of the disc (and of the disc 1 px smaller: the rim between
// them) counted on 4 x 4 samples; in half-pixel units, its centre at (d, d), its radius d.
void uk_bead (Canvas &cv, int x, int y, int d, unsigned c)
{
	if (d < 4) return;
	const int R2o = (4 * d) * (4 * d), R2i = (4 * d - 8) * (4 * d - 8);	// (eighth-pixel units)
	const int hy0 = d / 10, hy1 = d * 11 / 20;			// the gloss: these rows, an ellipse
	const int ax = d * 4 * 34 / 100, ay = (hy1 - hy0) * 4;		// its half-axes (eighths)
	const int hcx = 4 * d, hcy = (hy0 + hy1) * 4;			// its centre (eighths)
	unsigned top = uk_tone (c, 82), bottom = uk_tone (c, 158), rim = uk_tone (c, 52);
	for (int j = 0; j < d; j++)
	{
		unsigned body = uk_mix (top, bottom, j * 256 / (d - 1));
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
				if (a > 0) px = uk_over (px, 0x00FFFFFF, a * ch / 16);
			}
			if (co > ci) px = uk_mix (px, rim, (co - ci) * 256 / co);	// the rim
			blend_px (cv, x + i, y + j, px, co * 255 / 16);
		}
	}
}

void uk_title_strip (Canvas &cv, int x, int y, int w, int h, const char *s, int r)
{
	unsigned f = C_FRAME_ACTIVE, top = uk_tone (f, 164), bot = uk_tone (f, 115), mid = uk_tone (f, 140);
	if (UK_STYLE == UK_STYLE_MILK)					// (as the frames' title bars: Milk's
	{ top = uk_bright (f) < 110 ? f : uk_tone (f, 230); bot = C_FACE; mid = uk_mix (top, bot, 128); }	//  melts into the box's face)
	uk_rbox (cv, x, y, w, h, r, top, bot, 255, UK_TL | UK_TR);
	for (int i = x + (r > 1 ? r : 1); i < x + w - (r > 1 ? r : 1); i++) blend_px (cv, i, y + 1, 0x00FFFFFF, 110);
	if (s) uk_text_c (cv, x, y, w, h, s, uk_ink_on (mid), 2);
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

void uk_glyph (Canvas &cv, int kind, int cx, int cy, int size, unsigned c)
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
int uk_text_w (const char *s, int style)
{
	if (uk_face_) return s ? uk_face_->width (s, style) : 0;	// an app's face (uikit/text.h)
	Font &f = font ();
	return uk_len (s) * (f.valid () ? f.width () : uk_fw ());
}
static int text_h (void) { Font &f = font (); return f.valid () ? f.height () : uk_fh (); }

void uk_text_l (Canvas &cv, int x, int y, int h, const char *s, unsigned c, int style)
{
	if (uk_face_) { if (s) uk_face_->draw (cv, x, y + (h - uk_face_->height ()) / 2, s, c, style); return; }
	Font &f = font ();
	int ty = y + (h - text_h ()) / 2;
	if (f.valid ()) cv.drawFont (x, ty, s, f, c, 1, style);
	else cv.text (x, ty, s, c);
}

void uk_text_c (Canvas &cv, int x, int y, int w, int h, const char *s, unsigned c, int style)
{
	uk_text_l (cv, x + (w - uk_text_w (s, style)) / 2, y, h, s, c, style);
}

} // namespace uikit
