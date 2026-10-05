//
// Apps/photos/imgops.h -- what Photos does to pixels (0x00RRGGBB, or 0xAARRGGBB as img_load gives them): turned the
// way the camera says (EXIF orientation), a quarter turn, straightened by a small angle (bilinear, enlarged so no
// corner is empty), cropped; made smaller well (each pixel the average of those it covers) or larger (bilinear); the
// adjustments (exposure, contrast, highlights, shadows, saturation, warmth, sharpness) and the filters, as one tone
// curve and a colour pass; Enhance (automatic) from the histogram.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
//
#ifndef _photos_imgops_h
#define _photos_imgops_h

#include <math.h>
#include <string.h>
#ifdef USE_IMAGEKIT
#include "imagekit/imagekit.h"
#endif

namespace photos {

struct Pix
{
	unsigned *px; int w, h;
	Pix () : px (0), w (0), h (0) {}
	void alloc (int ww, int hh) { delete[] px; w = ww; h = hh; px = ww > 0 && hh > 0 ? new unsigned[(size_t) ww * hh] : 0; if (!px) w = h = 0; }
	void free_ () { delete[] px; px = 0; w = h = 0; }
	void take (Pix &o) { delete[] px; px = o.px; w = o.w; h = o.h; o.px = 0; o.w = o.h = 0; }
	void copy_of (const Pix &o) { alloc (o.w, o.h); if (px) memcpy (px, o.px, (size_t) w * h * 4); }
};

// the top byte set (opaque) -- the encoders (JPEG, PNG, the PDF's, the Clipboard's) read it as the alpha
static void opaque (Pix &p) { for (size_t i = 0, n = (size_t) p.w * p.h; i < n; i++) p.px[i] |= 0xFF000000u; }

// the alpha laid on a colour (bg), the result opaque
static void flatten (Pix &p, unsigned bg)
{
	for (size_t i = 0, n = (size_t) p.w * p.h; i < n; i++)
	{
		unsigned c = p.px[i], a = c >> 24;
		if (a == 255) { p.px[i] = c & 0xFFFFFF; continue; }
		unsigned r = (((c >> 16) & 255) * a + ((bg >> 16) & 255) * (255 - a)) / 255;
		unsigned g = (((c >> 8) & 255) * a + ((bg >> 8) & 255) * (255 - a)) / 255;
		unsigned b = ((c & 255) * a + (bg & 255) * (255 - a)) / 255;
		p.px[i] = r << 16 | g << 8 | b;
	}
}

// EXIF's orientation (1..8) applied: the pixels as the photo is seen
static void orient (Pix &p, int o)
{
	if (o <= 1 || o > 8 || !p.px) return;
	bool turn = o >= 5;
	int W = turn ? p.h : p.w, H = turn ? p.w : p.h;
	Pix q; q.alloc (W, H); if (!q.px) return;
	for (int y = 0; y < p.h; y++)
		for (int x = 0; x < p.w; x++)
		{
			int X, Y;
			switch (o)
			{
			case 2: X = p.w - 1 - x; Y = y; break;
			case 3: X = p.w - 1 - x; Y = p.h - 1 - y; break;
			case 4: X = x; Y = p.h - 1 - y; break;
			case 5: X = y; Y = x; break;
			case 6: X = p.h - 1 - y; Y = x; break;
			case 7: X = p.h - 1 - y; Y = p.w - 1 - x; break;
			default: X = y; Y = p.w - 1 - x; break;	// 8
			}
			q.px[(size_t) Y * W + X] = p.px[(size_t) y * p.w + x];
		}
	p.take (q);
}
// a quarter turn clockwise (k times)
static void rotate90 (Pix &p, int k)
{
	k &= 3;
	if (k == 1) orient (p, 6); else if (k == 2) orient (p, 3); else if (k == 3) orient (p, 8);
}

// (sx, sy, sw, sh) of src -> dst (dw x dh): smaller: each pixel the average of those it covers; larger: bilinear
static void scale_into (const unsigned *src, int srcW, int srcH, int sx, int sy, int sw, int sh, unsigned *dst, int dstStride, int dw, int dh)
{
#ifdef USE_IMAGEKIT						// (the app on Onyx: ImageKit's resize -- one for the system)
	ik_scale_rgb (src, srcW, srcH, srcW, sx, sy, sw, sh, dst, dstStride, dw, dh);
#else
	if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;
	if (sx < 0) sx = 0; if (sy < 0) sy = 0; if (sx + sw > srcW) sw = srcW - sx; if (sy + sh > srcH) sh = srcH - sy;
	if (sw >= dw && sh >= dh)
	{
		for (int y = 0; y < dh; y++)
		{
			int y0 = sy + (int) ((long long) y * sh / dh), y1 = sy + (int) ((long long) (y + 1) * sh / dh); if (y1 <= y0) y1 = y0 + 1;
			int ys = 1 + (y1 - y0) / 4;			// (at most ~4 rows looked at: fast enough, smooth enough)
			unsigned *o = dst + (size_t) y * dstStride;
			for (int x = 0; x < dw; x++)
			{
				int x0 = sx + (int) ((long long) x * sw / dw), x1 = sx + (int) ((long long) (x + 1) * sw / dw); if (x1 <= x0) x1 = x0 + 1;
				int xs = 1 + (x1 - x0) / 4;
				unsigned r = 0, g = 0, b = 0, c = 0;
				for (int yy = y0; yy < y1; yy += ys)
				{
					const unsigned *row = src + (size_t) yy * srcW;
					for (int xx = x0; xx < x1; xx += xs) { unsigned p = row[xx]; r += p >> 16 & 255; g += p >> 8 & 255; b += p & 255; c++; }
				}
				o[x] = (r / c) << 16 | (g / c) << 8 | (b / c);
			}
		}
		return;
	}
	// bilinear (16.16)
	long long kx = ((long long) sw << 16) / dw, ky = ((long long) sh << 16) / dh;
	for (int y = 0; y < dh; y++)
	{
		long long fy = ((long long) sy << 16) + ky * y + ky / 2 - 32768; if (fy < 0) fy = 0;
		int y0 = (int) (fy >> 16); if (y0 >= srcH - 1) y0 = srcH - 1;
		int y1 = y0 + 1 < srcH ? y0 + 1 : y0; unsigned wy = (unsigned) ((fy >> 8) & 255);
		unsigned *o = dst + (size_t) y * dstStride;
		for (int x = 0; x < dw; x++)
		{
			long long fx = ((long long) sx << 16) + kx * x + kx / 2 - 32768; if (fx < 0) fx = 0;
			int x0 = (int) (fx >> 16); if (x0 >= srcW - 1) x0 = srcW - 1;
			int x1 = x0 + 1 < srcW ? x0 + 1 : x0; unsigned wx = (unsigned) ((fx >> 8) & 255);
			unsigned a = src[(size_t) y0 * srcW + x0], b = src[(size_t) y0 * srcW + x1], c = src[(size_t) y1 * srcW + x0], d = src[(size_t) y1 * srcW + x1];
			unsigned v = 0;
			for (int s = 0; s < 24; s += 8)
			{
				unsigned t = (((a >> s) & 255) * (256 - wx) + ((b >> s) & 255) * wx) >> 8, u = (((c >> s) & 255) * (256 - wx) + ((d >> s) & 255) * wx) >> 8;
				v |= ((t * (256 - wy) + u * wy) >> 8) << s;
			}
			o[x] = v;
		}
	}
#endif
}
// the whole picture made to fit within maxW x maxH (proportions kept; never made larger when !grow)
static void fit (const Pix &src, Pix &dst, int maxW, int maxH, bool grow = false)
{
	if (!src.px) { dst.free_ (); return; }
	int w = maxW, h = (int) ((long long) src.h * maxW / src.w);
	if (h > maxH) { h = maxH; w = (int) ((long long) src.w * maxH / src.h); }
	if (!grow && (w > src.w || h > src.h)) { w = src.w; h = src.h; }
	if (w < 1) w = 1; if (h < 1) h = 1;
	dst.alloc (w, h); if (!dst.px) return;
	scale_into (src.px, src.w, src.h, 0, 0, src.w, src.h, dst.px, w, w, h);
}
// the middle cut to w x h's shape and brought to it (a square thumbnail)
static void cover (const Pix &src, unsigned *dst, int w, int h)
{
	int cw = src.w, ch = (int) ((long long) src.w * h / w); if (ch > src.h) { ch = src.h; cw = (int) ((long long) src.h * w / h); }
	scale_into (src.px, src.w, src.h, (src.w - cw) / 2, (src.h - ch) / 2, cw, ch, dst, w, w, h);
}

// straightened by deg (a few degrees), enlarged so that no empty corner shows; same size
static void straighten (Pix &p, float deg)
{
	if (deg > -0.05f && deg < 0.05f) return;
	float a = deg * 3.14159265f / 180.0f, ca = cosf (a), sa = sinf (a); float as = sa < 0 ? -sa : sa;
	float r = (float) p.w / p.h; float k = ca + as * (r > 1 ? r : 1 / r);
	Pix q; q.alloc (p.w, p.h); if (!q.px) return;
	float cx = p.w * 0.5f, cy = p.h * 0.5f;
	// destination (x, y) -> source: rotate by -a, shrink by 1/k
	int C = (int) (ca / k * 65536), S = (int) (sa / k * 65536);
	for (int y = 0; y < p.h; y++)
	{
		float dy = y + 0.5f - cy;
		for (int x = 0; x < p.w; x++)
		{
			float dx = x + 0.5f - cx;
			int fx = (int) ((dx * C + dy * S) + cx * 65536.0f) - 32768, fy = (int) ((-dx * S + dy * C) + cy * 65536.0f) - 32768;
			int x0 = fx >> 16, y0 = fy >> 16;
			if (x0 < 0) x0 = 0, fx = 0; if (y0 < 0) y0 = 0, fy = 0;
			if (x0 >= p.w - 1) x0 = p.w - 2 < 0 ? 0 : p.w - 2, fx = x0 << 16 | 0xFFFF; if (y0 >= p.h - 1) y0 = p.h - 2 < 0 ? 0 : p.h - 2, fy = y0 << 16 | 0xFFFF;
			unsigned wx = (unsigned) (fx >> 8) & 255, wy = (unsigned) (fy >> 8) & 255;
			int x1 = x0 + 1 < p.w ? x0 + 1 : x0, y1 = y0 + 1 < p.h ? y0 + 1 : y0;
			unsigned A = p.px[(size_t) y0 * p.w + x0], B = p.px[(size_t) y0 * p.w + x1], Cc = p.px[(size_t) y1 * p.w + x0], D = p.px[(size_t) y1 * p.w + x1], v = 0;
			for (int s = 0; s < 24; s += 8)
			{
				unsigned t = (((A >> s) & 255) * (256 - wx) + ((B >> s) & 255) * wx) >> 8, u = (((Cc >> s) & 255) * (256 - wx) + ((D >> s) & 255) * wx) >> 8;
				v |= ((t * (256 - wy) + u * wy) >> 8) << s;
			}
			q.px[(size_t) y * p.w + x] = v;
		}
	}
	p.take (q);
}

// cropped to (x, y, w, h)
static void crop (Pix &p, int x, int y, int w, int h)
{
	if (x < 0) x = 0; if (y < 0) y = 0; if (x + w > p.w) w = p.w - x; if (y + h > p.h) h = p.h - y;
	if (w < 1 || h < 1 || (x == 0 && y == 0 && w == p.w && h == p.h)) return;
	Pix q; q.alloc (w, h); if (!q.px) return;
	for (int j = 0; j < h; j++) memcpy (q.px + (size_t) j * w, p.px + (size_t) (y + j) * p.w + x, (size_t) w * 4);
	p.take (q);
}

// ---- the adjustments ------------------------------------------------------------------------------------------------------------
enum { A_EXPOSURE, A_CONTRAST, A_HIGHLIGHTS, A_SHADOWS, A_SATURATION, A_WARMTH, A_SHARPNESS, A_N };
enum { FL_NONE, FL_BW, FL_WARM, FL_COOL, FL_VINTAGE, FL_VIVID, FL_N };
static const char *const ADJ_NAME[A_N] = { "Exposure", "Contrast", "Highlights", "Shadows", "Saturation", "Warmth", "Sharpness" };
static const char *const FILTER_NAME[FL_N] = { "Original", "Black and white", "Warm", "Cool", "Vintage", "Vivid" };

struct Adjust
{
	int v[A_N];			// -100..100 (sharpness 0..100)
	int filter;
	bool identity () const { for (int i = 0; i < A_N; i++) if (v[i]) return false; return filter == FL_NONE; }
};

// the tone curve (one for the three channels) and the colour pass
struct ToneMap
{
	unsigned char lut[256];
	int sat;			// 256 = as is
	int warmR, warmB;		// 256 = as is
	bool mono; int fade;		// fade: lift the blacks (0..60)
	int sharp;			// 0..256
	void build (const Adjust &a)
	{
		int e = a.v[A_EXPOSURE], c = a.v[A_CONTRAST], hi = a.v[A_HIGHLIGHTS], sh = a.v[A_SHADOWS], s = a.v[A_SATURATION], w = a.v[A_WARMTH];
		mono = false; fade = 0;
		switch (a.filter)
		{
		case FL_BW: mono = true; c += 12; break;
		case FL_WARM: w += 55; s += 12; break;
		case FL_COOL: w -= 55; s -= 5; break;
		case FL_VINTAGE: s -= 35; w += 22; c -= 12; fade = 34; break;
		case FL_VIVID: s += 40; c += 15; break;
		}
		float ex = powf (2.0f, e / 100.0f), ct = 1.0f + c / 100.0f;
		for (int i = 0; i < 256; i++)
		{
			float x = i / 255.0f * ex;
			float ws = 1.0f - x / 0.55f; if (ws < 0) ws = 0; ws *= ws;
			x += sh / 100.0f * 0.32f * ws;
			float wh = (x - 0.45f) / 0.55f; if (wh < 0) wh = 0; if (wh > 1) wh = 1; wh *= wh;
			x += hi / 100.0f * 0.30f * wh;
			x = (x - 0.5f) * ct + 0.5f;
			x = fade / 255.0f + x * (1.0f - fade / 255.0f);
			int v = (int) (x * 255.0f + 0.5f); lut[i] = (unsigned char) (v < 0 ? 0 : v > 255 ? 255 : v);
		}
		sat = 256 + s * 256 / 100; if (sat < 0) sat = 0;
		warmR = 256 + w * 64 / 100; warmB = 256 - w * 64 / 100;
		sharp = a.v[A_SHARPNESS] * 256 / 100;
	}
	inline unsigned apply (unsigned p) const
	{
		int r = lut[p >> 16 & 255], g = lut[p >> 8 & 255], b = lut[p & 255];
		int l = (r * 77 + g * 150 + b * 29) >> 8;
		if (mono) { r = g = b = l; }
		else if (sat != 256) { r = l + ((r - l) * sat >> 8); g = l + ((g - l) * sat >> 8); b = l + ((b - l) * sat >> 8); }
		if (warmR != 256) { r = r * warmR >> 8; b = b * warmB >> 8; }
		r = r < 0 ? 0 : r > 255 ? 255 : r; g = g < 0 ? 0 : g > 255 ? 255 : g; b = b < 0 ? 0 : b > 255 ? 255 : b;
		return (unsigned) r << 16 | (unsigned) g << 8 | (unsigned) b;
	}
};

// the adjustments applied to p (in place)
static void adjust (Pix &p, const Adjust &a)
{
	if (a.identity () || !p.px) return;
	ToneMap t; t.build (a);
	size_t n = (size_t) p.w * p.h;
	if (t.sharp > 0 && p.w > 2 && p.h > 2)
	{	// an unsharp mask (3 x 3), the rows kept as they were
		unsigned *prev = new unsigned[p.w], *cur = new unsigned[p.w];
		memcpy (prev, p.px, (size_t) p.w * 4);
		for (int y = 1; y < p.h - 1; y++)
		{
			memcpy (cur, p.px + (size_t) y * p.w, (size_t) p.w * 4);
			const unsigned *nx = p.px + (size_t) (y + 1) * p.w;
			unsigned *o = p.px + (size_t) y * p.w;
			for (int x = 1; x < p.w - 1; x++)
			{
				unsigned v = 0;
				for (int s = 0; s < 24; s += 8)
				{
					int c = cur[x] >> s & 255;
					int bl = ((prev[x] >> s & 255) + (nx[x] >> s & 255) + (cur[x - 1] >> s & 255) + (cur[x + 1] >> s & 255)) >> 2;
					int r = c + ((c - bl) * t.sharp >> 7);
					v |= (unsigned) (r < 0 ? 0 : r > 255 ? 255 : r) << s;
				}
				o[x] = v;
			}
			unsigned *sw = prev; prev = cur; cur = sw;
		}
		delete[] prev; delete[] cur;
	}
	for (size_t i = 0; i < n; i++) p.px[i] = t.apply (p.px[i]);
}

// Enhance (automatic): exposure, contrast, shadows, highlights, saturation from the picture's histogram
static void auto_enhance (const Pix &p, Adjust &a)
{
	unsigned hist[256]; memset (hist, 0, sizeof hist);
	size_t n = (size_t) p.w * p.h, step = n > 200000 ? n / 200000 : 1, cnt = 0;
	long long satSum = 0;
	for (size_t i = 0; i < n; i += step)
	{
		unsigned c = p.px[i]; int r = c >> 16 & 255, g = c >> 8 & 255, b = c & 255;
		int l = (r * 77 + g * 150 + b * 29) >> 8; hist[l]++; cnt++;
		int mx = r > g ? (r > b ? r : b) : (g > b ? g : b), mn = r < g ? (r < b ? r : b) : (g < b ? g : b); satSum += mx - mn;
	}
	if (!cnt) return;
	auto pct = [&] (unsigned q) { unsigned long long s = 0, want = (unsigned long long) cnt * q / 1000; for (int i = 0; i < 256; i++) { s += hist[i]; if (s >= want) return i; } return 255; };
	int lo = pct (5), mid = pct (500), hi = pct (995);
	int e = mid < 30 ? 60 : (int) (log2f (118.0f / mid) * 100.0f * 0.7f); if (e > 60) e = 60; if (e < -50) e = -50;
	int span = hi - lo; int c = span < 40 ? 40 : (int) ((220.0f / span - 1.0f) * 60.0f); if (c > 40) c = 40; if (c < -10) c = -10;
	int sh = lo > 40 ? 0 : (40 - lo) / 2 + (mid < 90 ? 15 : 0); if (sh > 35) sh = 35;
	int hl = hi > 245 ? -25 : 0;
	int avgSat = (int) (satSum / (long long) cnt);
	int s = avgSat < 60 ? 22 : avgSat < 100 ? 12 : 4;
	a.v[A_EXPOSURE] = e; a.v[A_CONTRAST] = c; a.v[A_SHADOWS] = sh; a.v[A_HIGHLIGHTS] = hl; a.v[A_SATURATION] = s;
	if (a.v[A_SHARPNESS] < 20) a.v[A_SHARPNESS] = 20;
}

} // namespace photos

#endif
