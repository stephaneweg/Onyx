//
// voronoy.c -- a desktop-wallpaper generator that runs as a userland app. It asks
// the kernel for the shared wallpaper buffer (kapi_wallpaper_buffer), paints a
// toroidal Voronoi field into it (ported from the old kernel GenerateWallpaper),
// commits it as the live background, then exits. The buffer's frames are
// kernel-owned, so the wallpaper persists after this app is gone.
//
// It is meant to run from autostart, but can also be re-run from the app drawer to
// reshuffle the background. Later, other apps can paint different wallpapers the
// same way -- the kernel no longer hardcodes the wallpaper style.
//
#include "kapi.h"
#include "applib.h"

#define NPTS	28			// default Voronoi seed count (config: points)
#define NPTS_MAX 64			// fixed array bound
#define ADIV	2			// render at half-res, then upscale (faster)
#define BASE	0x004878B0		// default base colour (config: base = 0xRRGGBB)

// Parse "0xRRGGBB" (or a decimal) from a config string; def if empty/invalid.
static unsigned parse_color (const char *s, unsigned def)
{
	if (s == 0) return def;
	while (*s == ' ' || *s == '\t') s++;
	unsigned v = 0; int any = 0, hex = 0;
	if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { s += 2; hex = 1; }
	while (*s != '\0')
	{
		char c = *s; int d;
		if (c >= '0' && c <= '9') d = c - '0';
		else if (hex && c >= 'a' && c <= 'f') d = c - 'a' + 10;
		else if (hex && c >= 'A' && c <= 'F') d = c - 'A' + 10;
		else break;
		v = v * (hex ? 16 : 10) + d; s++; any = 1;
	}
	return any ? v : def;
}

// Integer square root (no FP in EL1 apps).
static unsigned isqrt (unsigned n)
{
	if (n == 0) return 0;
	unsigned x = n, y = (x + 1) / 2;
	while (y < x) { x = y; y = (x + n / x) / 2; }
	return x;
}

// Tint BASE by a brightness from the distance (near a seed = dark cell centre, far =
// bright edge), floored so cells never go fully black. From SimpleOS ComputeColor().
static unsigned tint (unsigned base, unsigned dist)
{
	const unsigned mn = 96;
	unsigned cc = mn + dist * (255 - mn) / 255;
	if (cc > 255) cc = 255;
	unsigned r = (((base >> 16) & 0xFF) * cc) >> 8;
	unsigned g = (((base >> 8)  & 0xFF) * cc) >> 8;
	unsigned b = (( base        & 0xFF) * cc) >> 8;
	return (r << 16) | (g << 8) | b;
}

int main (void)
{
	int w = 0, h = 0;
	unsigned *bg = kapi_wallpaper_buffer (&w, &h);
	if (bg == 0 || w <= 0 || h <= 0) return 1;

	int mx = w / ADIV, my = h / ADIV;
	if (mx < 1 || my < 1) return 1;

	// Config (own folder): base colour + seed count, both optional.
	unsigned base = BASE;
	int npts = NPTS;
	if (app_ini_load ("config.ini") >= 0)
	{
		base = parse_color (app_ini_get (0, "base", 0), BASE);
		npts = app_ini_get_int (0, "points", NPTS);
	}
	if (npts < 1) npts = 1;
	if (npts > NPTS_MAX) npts = NPTS_MAX;

	// Seed points (half-res space). Seed the RNG from the timer so it varies per run.
	int px[NPTS_MAX], py[NPTS_MAX];
	unsigned rng = kapi_get_ticks () | 1u;
	for (int i = 0; i < npts; i++)
	{
		rng = rng * 1103515245u + 12345u; px[i] = (int) (rng % (unsigned) mx);
		rng = rng * 1103515245u + 12345u; py[i] = (int) (rng % (unsigned) my);
	}

	for (int y = 0; y < my; y++)
	{
		for (int x = 0; x < mx; x++)
		{
			unsigned best = 0xFFFFFFFF;
			for (int i = 0; i < npts; i++)
			{
				// Toroidal axis distance, normalised to 0..128 (wraps at 128
				// so the field tiles seamlessly).
				int dx = x > px[i] ? x - px[i] : px[i] - x;
				dx = dx * 256 / mx; if (dx > 128) dx = 256 - dx;
				int dy = y > py[i] ? y - py[i] : py[i] - y;
				dy = dy * 256 / my; if (dy > 128) dy = 256 - dy;

				unsigned d = isqrt ((unsigned) (dx * dx + dy * dy));
				if (d > 255) d = 255;
				if (d < best) best = d;
			}
			unsigned col = tint (base, best);
			for (int j = 0; j < ADIV; j++)			// upscale the half-res cell
				for (int k = 0; k < ADIV; k++)
					bg[(y * ADIV + j) * w + (x * ADIV + k)] = col;
		}
		if ((y & 15) == 0) kapi_yield ();			// cooperative: long compute
	}

	kapi_wallpaper_commit ();		// make it the live desktop background
	return 0;				// exit; the wallpaper persists
}
