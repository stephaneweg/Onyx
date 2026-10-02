//
// padjust.h -- Paint's colour adjustments and filters (the Colours and Filters menus, the fx button):
// brightness / contrast, hue / saturation / lightness, desaturate, colorize, the channels remapped
// (each of R, G, B taken from another channel), invert, sepia, posterize, threshold; blur, sharpen,
// pixelate. Each acts on the selection (of the current layer), the whole current layer, or every
// layer (the whole picture). Its dialog shows it at once on the picture: the pixels changed are kept
// first as they were, each new setting applied to them again; Cancel puts them back; OK is one undo.
//
// MIT licence (Onyx).
//
#ifndef _paint_padjust_h
#define _paint_padjust_h

#include <math.h>
#include "psel.h"

namespace pd {

enum { AJ_BRIGHT, AJ_HUE, AJ_DESAT, AJ_COLORIZE, AJ_REMAP, AJ_INVERT, AJ_SEPIA, AJ_POSTER, AJ_THRESH,
       AJ_BLUR, AJ_SHARPEN, AJ_PIXEL, AJ_COUNT };
enum { AJ_FIRST_FILTER = AJ_BLUR };
static const char *const ADJUST_NAMES[AJ_COUNT] = { "Brightness / Contrast...", "Hue / Saturation...", "Desaturate...", "Colorize...",
	"Remap the Channels...", "Invert Colours...", "Sepia...", "Posterize...", "Threshold...", "Blur...", "Sharpen...", "Pixelate..." };
enum { SC_SELECTION, SC_LAYER, SC_ALL };
static const char *const SCOPE_NAMES[3] = { "The selection", "The layer", "Every layer" };
enum { CH_RED, CH_GREEN, CH_BLUE, CH_BLACK, CH_WHITE };
static const char *const CHANNEL_NAMES[5] = { "Red", "Green", "Blue", "Black (0)", "White (255)" };

// The part adjusted: its box, the scope, each layer's pixels there as they were (0: not adjusted).
struct AdjustJob { Rect r; int scope; unsigned *orig[MAXLAYERS]; };
static AdjustJob g_aj;

static inline int aj_w () { return g_aj.r.x1 - g_aj.r.x0; }
static void adjust_free () { for (int k = 0; k < MAXLAYERS; k++) { delete[] g_aj.orig[k]; g_aj.orig[k] = 0; } }
static void adjust_begin (int scope)
{
	adjust_free ();
	if (scope == SC_SELECTION && !has_sel ()) scope = SC_LAYER;
	Rect r = scope == SC_SELECTION ? g_sel : mkrect (0, 0, D.w, D.h);
	r.clip (D.w, D.h);
	g_aj.r = r; g_aj.scope = scope;
	unsigned n = (unsigned) pmax (1, aj_w () * (r.y1 - r.y0));
	for (int k = 0; k < D.n; k++)
	{
		if (scope != SC_ALL && k != D.cur) continue;
		unsigned *o = g_aj.orig[k] = new unsigned[n];
		for (int y = r.y0; y < r.y1; y++) for (int x = r.x0; x < r.x1; x++) o[(unsigned) (y - r.y0) * aj_w () + (x - r.x0)] = D.lay[k].px[(unsigned) y * D.w + x];
	}
}
static inline unsigned aj_orig (int k, int x, int y)
{
	Rect &r = g_aj.r;
	x = pclamp (x, r.x0, r.x1 - 1); y = pclamp (y, r.y0, r.y1 - 1);
	return g_aj.orig[k][(unsigned) (y - r.y0) * aj_w () + (x - r.x0)];
}
static void adjust_shown () { if (g_aj.scope == SC_ALL) compose_all (); else compose (g_aj.r); }
static void adjust_restore ()
{
	Rect r = g_aj.r;
	for (int k = 0; k < D.n; k++)
		if (g_aj.orig[k]) for (int y = r.y0; y < r.y1; y++) for (int x = r.x0; x < r.x1; x++) D.lay[k].px[(unsigned) y * D.w + x] = aj_orig (k, x, y);
}
static void adjust_cancel () { adjust_restore (); adjust_shown (); adjust_free (); }
// Done: the change kept (undone as one step: the tiles of the layer, or the whole picture).
static void adjust_commit ()
{
	Rect r = g_aj.r;
	unsigned n = (unsigned) pmax (1, aj_w () * (r.y1 - r.y0));
	unsigned *now[MAXLAYERS] = { 0 };
	for (int k = 0; k < D.n; k++)					// (the result kept, the pixels as they were put back...)
	{
		if (!g_aj.orig[k]) continue;
		now[k] = new unsigned[n];
		for (int y = r.y0; y < r.y1; y++) for (int x = r.x0; x < r.x1; x++) now[k][(unsigned) (y - r.y0) * aj_w () + (x - r.x0)] = D.lay[k].px[(unsigned) y * D.w + x];
	}
	adjust_restore ();
	if (g_aj.scope == SC_ALL) rec_whole (); else { rec_begin (); rec_touch (r); }	// (... recorded for the undo ...)
	for (int k = 0; k < D.n; k++)					// (... and the result again)
	{
		if (!now[k]) continue;
		for (int y = r.y0; y < r.y1; y++) for (int x = r.x0; x < r.x1; x++) D.lay[k].px[(unsigned) y * D.w + x] = now[k][(unsigned) (y - r.y0) * aj_w () + (x - r.x0)];
		delete[] now[k];
	}
	if (g_aj.scope != SC_ALL) rec_end ();
	adjust_shown ();
	adjust_free ();
}

static inline int clamp8 (float v) { return v < 0 ? 0 : v > 255 ? 255 : (int) (v + 0.5f); }
static void rgb_hsl (float r, float g, float b, float &h, float &s, float &l)
{
	float mx = fmaxf (r, fmaxf (g, b)), mn = fminf (r, fminf (g, b));
	l = (mx + mn) / 2;
	if (mx == mn) { h = s = 0; return; }
	float d = mx - mn;
	s = l > 0.5f ? d / (2 - mx - mn) : d / (mx + mn);
	if (mx == r) h = (g - b) / d + (g < b ? 6 : 0); else if (mx == g) h = (b - r) / d + 2; else h = (r - g) / d + 4;
	h /= 6;
}
static float hue2 (float p, float q, float t)
{
	if (t < 0) t += 1;
	if (t > 1) t -= 1;
	if (t < 1.0f / 6) return p + (q - p) * 6 * t;
	if (t < 0.5f) return q;
	if (t < 2.0f / 3) return p + (q - p) * (2.0f / 3 - t) * 6;
	return p;
}
static void hsl_rgb (float h, float s, float l, float &r, float &g, float &b)
{
	if (s <= 0) { r = g = b = l; return; }
	float q = l < 0.5f ? l * (1 + s) : l + s - l * s, p = 2 * l - q;
	r = hue2 (p, q, h + 1.0f / 3); g = hue2 (p, q, h); b = hue2 (p, q, h - 1.0f / 3);
}
static inline float luma (float r, float g, float b) { return 0.299f * r + 0.587f * g + 0.114f * b; }

// One pixel of layer k adjusted (a, b, c: the dialog's values; the filters read the pixels around it).
static unsigned adjust_px (int kind, int k, int x, int y, int a, int b, int c)
{
	unsigned o = aj_orig (k, x, y), al = o & 0xFF000000u;
	float R = (o >> 16) & 255, G = (o >> 8) & 255, B = o & 255;
	switch (kind)
	{
	case AJ_BRIGHT:					// a: brightness -100..100, b: contrast -100..100
	{
		float f = (259.0f * (b * 1.275f + 255)) / (255 * (259 - b * 1.275f)), off = a * 1.275f;
		R = f * (R - 128) + 128 + off; G = f * (G - 128) + 128 + off; B = f * (B - 128) + 128 + off;
		break;
	}
	case AJ_HUE:					// a: hue -180..180, b: saturation -100..100, c: lightness -100..100
	{
		float h, s, l; rgb_hsl (R / 255, G / 255, B / 255, h, s, l);
		h += a / 360.0f; h -= floorf (h);
		s = b >= 0 ? s + (1 - s) * b / 100.0f * (s > 0 ? 1 : 0) : s * (1 + b / 100.0f);
		l = c >= 0 ? l + (1 - l) * c / 100.0f : l * (1 + c / 100.0f);
		hsl_rgb (h, s, l, R, G, B); R *= 255; G *= 255; B *= 255;
		break;
	}
	case AJ_DESAT:					// a: how much, 0..100
	{
		float v = luma (R, G, B), t = a / 100.0f;
		R += (v - R) * t; G += (v - G) * t; B += (v - B) * t;
		break;
	}
	case AJ_COLORIZE:				// a: hue 0..360, b: saturation 0..100, c: lightness -100..100
	{
		float l = luma (R, G, B) / 255;
		l = c >= 0 ? l + (1 - l) * c / 100.0f : l * (1 + c / 100.0f);
		hsl_rgb (a / 360.0f, b / 100.0f, l, R, G, B); R *= 255; G *= 255; B *= 255;
		break;
	}
	case AJ_REMAP:					// a, b, c: where red, green and blue come from (CH_*)
	{
		float in[5] = { R, G, B, 0, 255 };
		R = in[pclamp (a, 0, 4)]; G = in[pclamp (b, 0, 4)]; B = in[pclamp (c, 0, 4)];
		break;
	}
	case AJ_INVERT: R = 255 - R; G = 255 - G; B = 255 - B; break;
	case AJ_SEPIA:					// a: how much, 0..100
	{
		float r2 = 0.393f * R + 0.769f * G + 0.189f * B, g2 = 0.349f * R + 0.686f * G + 0.168f * B, b2 = 0.272f * R + 0.534f * G + 0.131f * B, t = a / 100.0f;
		R += (r2 - R) * t; G += (g2 - G) * t; B += (b2 - B) * t;
		break;
	}
	case AJ_POSTER:					// a: levels 2..16
	{
		float st = 255.0f / (a - 1);
		R = floorf (R / st + 0.5f) * st; G = floorf (G / st + 0.5f) * st; B = floorf (B / st + 0.5f) * st;
		break;
	}
	case AJ_THRESH: { float v = luma (R, G, B) >= a ? 255 : 0; R = G = B = v; break; }	// a: 0..255
	case AJ_BLUR: case AJ_SHARPEN:			// a: radius 1..20 (blur) / amount 0..300 (sharpen, radius 1)
	{
		int rad = kind == AJ_BLUR ? a : 1;
		float sr = 0, sg = 0, sb = 0, sa = 0; int n = 0;
		for (int j = -rad; j <= rad; j += pmax (1, rad / 4))
			for (int i = -rad; i <= rad; i += pmax (1, rad / 4))
			{
				if (i * i + j * j > rad * rad + rad) continue;
				unsigned q = aj_orig (k, x + i, y + j); float qa = q >> 24;
				sr += ((q >> 16) & 255) * qa; sg += ((q >> 8) & 255) * qa; sb += (q & 255) * qa; sa += qa; n++;
			}
		if (kind == AJ_BLUR)
		{
			if (sa <= 0) return 0;
			R = sr / sa; G = sg / sa; B = sb / sa; al = (unsigned) clamp8 (sa / n) << 24;
		}
		else if (sa > 0)
		{
			float f = a / 100.0f;
			R = R + (R - sr / sa) * f; G = G + (G - sg / sa) * f; B = B + (B - sb / sa) * f;
		}
		break;
	}
	case AJ_PIXEL:					// a: the blocks' size 2..64 (on the picture's grid)
	{
		int bx = x - (x % a), by = y - (y % a);
		float sr = 0, sg = 0, sb = 0, sa = 0; int n = 0;
		for (int j = 0; j < a; j += pmax (1, a / 6))
			for (int i = 0; i < a; i += pmax (1, a / 6))
			{
				unsigned q = aj_orig (k, bx + i, by + j); float qa = q >> 24;
				sr += ((q >> 16) & 255) * qa; sg += ((q >> 8) & 255) * qa; sb += (q & 255) * qa; sa += qa; n++;
			}
		if (sa <= 0) return 0;
		R = sr / sa; G = sg / sa; B = sb / sa; al = (unsigned) clamp8 (sa / n) << 24;
		break;
	}
	}
	return al | (unsigned) clamp8 (R) << 16 | (unsigned) clamp8 (G) << 8 | (unsigned) clamp8 (B);
}
// The adjustment applied to the original (again: a new setting), inside the selection for its scope.
static void adjust_apply (int kind, int a, int b, int c)
{
	Rect r = g_aj.r;
	for (int k = 0; k < D.n; k++)
	{
		if (!g_aj.orig[k]) continue;
		for (int y = r.y0; y < r.y1; y++)
			for (int x = r.x0; x < r.x1; x++)
			{
				unsigned o = aj_orig (k, x, y);
				int s = g_aj.scope == SC_SELECTION ? sel_at (x, y) : 255;
				unsigned v = s ? adjust_px (kind, k, x, y, a, b, c) : o;
				if (s && s < 255) v = over (o, v | 0xFF000000u, (unsigned) ((v >> 24) * s / 255));
				D.lay[k].px[(unsigned) y * D.w + x] = v;
			}
	}
	adjust_shown ();
}

} // namespace pd

#endif
