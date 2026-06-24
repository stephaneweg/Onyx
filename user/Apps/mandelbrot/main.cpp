//
// fractal browser -- a fixed-point escape-time explorer (Q4.28 in int64, no FP).
// A dropdown (top-left) picks the fractal: Mandelbrot / Julia / Burning Ship /
// Tricorn. Click to zoom in (recenters on the click), 'o' zooms out, 'r' resets.
// Iteration count maps to colour. Renders progressively (yields between row bands).
//
#include "kapi.h"
#include "uikit.hpp"
#include "applib.h"

#define W	340
#define RH	240			// render height (rows); status below
#define H	(RH + 16)

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

// Fractal-type dropdown (top-left, drawn over the canvas; see applib.h).
static const char *const FRACTALS[] = { "Mandelbrot", "Julia", "Burning Ship", "Tricorn" };
static ax_dropdown g_dd = { 4, 4, 120, 18, FRACTALS, 4, FR_MANDEL, 0 };

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
	// status bar: controls + zoom depth + iteration budget
	for (int i = 0; i < W * 16; i++) fb[RH * W + i] = 0x00181c20;
	char s[64]; int p = 0;
	const char *t = "click:in  o:out  r:reset   z="; for (int i = 0; t[i]; i++) s[p++] = t[i];
	p += ax_itoa (g_zoom, s + p);
	s[p++] = ' '; s[p++] = 'i'; s[p++] = 't'; s[p++] = '=';
	p += ax_itoa (g_maxit, s + p); s[p] = '\0';
	kapi_draw_text (6, RH + 1, s, 0x0090b0a0);
	g_dirty = 0;
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
	fb = kapi_create_window (W, H, "fractal");
	if (fb == 0) return 1;
	ui::decorate_window ();
	reset_view ();
	kapi_set_click_handler (on_click);
	kapi_set_key_handler (on_key);
	while (!should_exit ())
	{
		pump_events ();
		if (g_dirty) render ();
		ax_dropdown_draw (&g_dd, fb, W, H);	// overlay on top, every frame
		msleep (30);
	}
	return 0;
}
