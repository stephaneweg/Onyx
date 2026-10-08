//
// fractal browser -- a fixed-point escape-time explorer (Q4.28 in int64, no FP).
// A dropdown (top-left) picks the fractal: Mandelbrot / Julia / Burning Ship /
// Tricorn. Click to zoom in (recenters on the click), 'o' zooms out, 'r' resets.
// Iteration count maps to colour. Renders progressively (yields between row bands).
//
#include "appkit/appkit.h"
#include "uikit/uikit.h"

#define W	340
#define RH	240			// render height (rows); status below
#define SBH	22			// the status bar
#define H	(RH + SBH)

// Fractal types (dropdown order).
#define FR_MANDEL	0
#define FR_JULIA	1
#define FR_SHIP		2
#define FR_TRICORN	3

// Fixed-point Q4.28 in int64: 28 fractional bits (was 24). During iteration |z|<2
// until escape, so |a|,|b| <= 2^29 and a*b <= 2^58 -- safe in signed i64 (< 2^63).
// More fractional bits => more zoom levels before pixels collapse to one point.
typedef long long i64;
#define FB	28
#define FONE	(1LL << FB)
#define MINITER	96
#define MAXITER	512			// cap (deep zoom needs more iterations for detail)
static i64 fmul (i64 a, i64 b) { return (a * b) >> FB; }

static unsigned *fb;
static i64 g_cx, g_cy, g_span;		// view center + width in the complex plane
static int g_zoom = 0;			// zoom level (drives the iteration budget)
static int g_maxit = MINITER;		// current iteration count (set per render)
static int g_type = FR_MANDEL;		// current fractal
static int g_dirty = 1;

// Julia additive constant (~ -0.8 + 0.156i), a classic dendrite.
#define JCR	(-(FONE * 4 / 5))
#define JCI	(FONE * 156 / 1000)

// Fractal-type dropdown (top-left, drawn over the canvas by dd_draw). The picture is this program's own
// canvas, not a uikit window of widgets: the drop-down is drawn and hit-tested here (uikit's Dropdown is
// the widget for everything else).
typedef struct {
	int x, y, w, h;			// closed box rect (canvas coords)
	const char *const *opts;	// option labels
	int nopts;
	int sel;			// selected index
	int open;			// list expanded?
} ax_dropdown;
// A click on the canvas -> 1 if the drop-down took it (opened, closed, or an option chosen: sel).
static int ax_dropdown_click (ax_dropdown *d, int cx, int cy)
{
	if (d->open)
	{
		if (cx >= d->x && cx < d->x + d->w && cy >= d->y + d->h
		    && cy < d->y + d->h * (1 + d->nopts))
		{
			d->sel = (cy - (d->y + d->h)) / d->h;
			d->open = 0;
			return 1;
		}
		d->open = 0;		// click elsewhere: close
	}
	if (cx >= d->x && cx < d->x + d->w && cy >= d->y && cy < d->y + d->h)
	{
		d->open = !d->open;
		return 1;
	}
	return 0;
}
static const char *const FRACTALS[] = { "Mandelbrot", "Julia", "Burning Ship", "Tricorn" };
static ax_dropdown g_dd = { 6, 6, 132, 24, FRACTALS, 4, FR_MANDEL, 0 };

static void reset_view (void)
{
	// Mandelbrot is centred on the cardioid; the others look best centred on 0.
	g_cx = (g_type == FR_MANDEL) ? -(FONE / 2) : 0;
	g_cy = 0;
	g_span = 3 * FONE;
	g_zoom = 0;
	g_dirty = 1;
}

static unsigned color (int it)
{
	if (it >= g_maxit) return 0x00000000;			// inside the set
	unsigned r = (unsigned) (it * 8) & 0xFF;
	unsigned g = (unsigned) (it * 5 + 40) & 0xFF;
	unsigned b = (unsigned) (it * 11 + 80) & 0xFF;
	return (r << 16) | (g << 8) | b;
}

static void render (void)
{
	// More iterations as we zoom in, so deep regions keep revealing detail.
	g_maxit = MINITER + g_zoom * 32;
	if (g_maxit > MAXITER) g_maxit = MAXITER;

	i64 spanx = g_span;
	i64 spany = (i64) ((g_span * RH) / W);
	i64 x0 = g_cx - spanx / 2;
	i64 y0 = g_cy - spany / 2;
	i64 stepx = spanx / W;
	i64 stepy = spany / RH;
	i64 esc = 4 * FONE;

	for (int py = 0; py < RH; py++)
	{
		i64 ci = y0 + (i64) py * stepy;
		for (int px = 0; px < W; px++)
		{
			i64 cr = x0 + (i64) px * stepx;
			// z starts at the pixel for Julia (c is fixed), at 0 otherwise (c = pixel).
			i64 zr, zi, ar, ai;
			if (g_type == FR_JULIA) { zr = cr; zi = ci; ar = JCR; ai = JCI; }
			else                    { zr = 0;  zi = 0;  ar = cr;  ai = ci; }
			int it = 0;
			for (; it < g_maxit; it++)
			{
				i64 xr = zr, xi = zi;
				if (g_type == FR_SHIP)		// burning ship folds onto |Re|,|Im|
				{
					if (xr < 0) xr = -xr;
					if (xi < 0) xi = -xi;
				}
				i64 zr2 = fmul (xr, xr), zi2 = fmul (xi, xi);
				if (zr2 + zi2 > esc) break;
				i64 cross = 2 * fmul (xr, xi);
				zi = (g_type == FR_TRICORN ? -cross : cross) + ai;	// tricorn = conj(z)^2
				zr = zr2 - zi2 + ar;
			}
			fb[py * W + px] = color (it);
		}
		if ((py & 15) == 0) kapi_yield ();		// progressive display
	}
	// status bar (the theme's face): controls + zoom depth + iteration budget
	using namespace uikit;
	Canvas cv; cv.adopt (fb, W, H);
	uk_rbox (cv, 0, RH, W, SBH, 0, uk_tone (C_FACE, 150), uk_tone (C_FACE, 120));
	uk_etch_h (cv, 0, RH, W, C_FACE);
	char s[64]; int p = 0;
	const char *t = "click:in  o:out  r:reset   z="; for (int i = 0; t[i]; i++) s[p++] = t[i];
	p += ax_itoa (g_zoom, s + p);
	s[p++] = ' '; s[p++] = 'i'; s[p++] = 't'; s[p++] = '=';
	p += ax_itoa (g_maxit, s + p); s[p] = '\0';
	uk_text_l (cv, 8, RH + 2, SBH - 2, s, C_TEXT);
	g_dirty = 0;
}

// The fractal drop-down, drawn over the picture after each render: the theme's raised box (the
// chosen fractal, a chevron) and, open, its list in a light floating panel -- its rows where
// ax_dropdown_click finds them (d->h px each, under the box), the chosen one in the accent.
static void dd_draw (const ax_dropdown *d)
{
	using namespace uikit;
	Canvas cv; cv.adopt (fb, W, H);
	int fh = uk_fh (), k = d->open ? 1 : 0;
	uk_raised (cv, d->x, d->y, d->w, d->h, 5, C_FACE, d->open ? UK_PRESSED : UK_NORMAL);
	if (d->sel >= 0 && d->sel < d->nopts) cv.text (d->x + 9 + k, d->y + (d->h - fh) / 2 + k, d->opts[d->sel], C_TEXT);
	uk_glyph (cv, d->open ? WKG_CHEV_UP : WKG_CHEV_DOWN, d->x + d->w - 13 + k, d->y + d->h / 2 + k, 9, C_TEXT);
	if (!d->open) return;
	int ly = d->y + d->h, lh = d->nopts * d->h;
	uk_rbox (cv, d->x, ly, d->w, lh, 6, uk_tone (C_FIELD, 140), C_FIELD);
	uk_rline (cv, d->x, ly, d->w, lh, 6, uk_tone (C_FACE, 70), 230);
	for (int i = 0; i < d->nopts; i++)
	{
		int ry = ly + i * d->h;
		if (i == d->sel) uk_hilite (cv, d->x + 3, ry + 2, d->w - 6, d->h - 4, 4, true);
		cv.text (d->x + 10, ry + (d->h - fh) / 2, d->opts[i], i == d->sel ? uk_hilite_ink (true) : C_FIELD_TEXT);
	}
}

static void on_click (unsigned long s, int ev, long val)
{
	(void) s;
	if (ev != GUI_EVENT_CANVAS_CLICK) return;
	int px = (int) ((val >> 16) & 0xFFFF), py = (int) (val & 0xFFFF);

	// The fractal-type dropdown takes the click first (open / close / select).
	int wasopen = g_dd.open, old = g_dd.sel;
	if (ax_dropdown_click (&g_dd, px, py))
	{
		if (g_dd.sel != old) { g_type = g_dd.sel; reset_view (); }
		g_dirty = 1;			// re-render to erase the (closed) list area
		return;
	}
	if (wasopen && !g_dd.open)		// click-away dismissed the list: just close it
	{
		g_dirty = 1;
		return;
	}

	if (py >= RH) return;
	// Stop zooming once a pixel step would lose sub-pixel precision (fixed-point
	// floor), otherwise the whole view collapses to one colour. ~20 levels at Q4.28.
	if ((g_span / 2) / W < 2) return;
	i64 spanx = g_span, spany = (i64) ((g_span * RH) / W);
	g_cx = (g_cx - spanx / 2) + (i64) px * (spanx / W);	// recenter on the click
	g_cy = (g_cy - spany / 2) + (i64) py * (spany / RH);
	g_span /= 2;						// zoom in
	g_zoom++;
	g_dirty = 1;
}

static void on_key (unsigned long s, int ev, long key)
{
	(void) s;
	if (ev != GUI_EVENT_KEY) return;
	if (key == 'o' || key == 'O') { g_span *= 2; if (g_zoom > 0) g_zoom--; g_dirty = 1; }
	else if (key == 'r' || key == 'R') reset_view ();
}

int main (void)
{
	fb = uk_win_create (W, H, "fractal");
	if (fb == 0) return 1;
	uikit::uk_decorate_window ();
	reset_view ();
	uk_win_on_click (on_click);
	uk_win_on_key (on_key);
	while (!should_exit ())
	{
		pump_events ();
		if (g_dirty) { render (); dd_draw (&g_dd); }	// the drop-down over it (once: its edges blend)
		present ();				// (the frame drawn into the canvas: shown -- the compositor redraws only what it is told)
		msleep (30);
	}
	return 0;
}
