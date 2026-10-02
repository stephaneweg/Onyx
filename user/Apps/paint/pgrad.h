//
// pgrad.h -- Paint's gradients: a gradient is stops (a position 0..1000, a colour with its opacity)
// and, between two stops, a midpoint (where the mix is half and half, 0..1000 of the segment); the
// presets (Colour 1 to 2 and Colour 1 to transparent follow the colours chosen), and yours, kept as
// GIMP's gradient files (.ggr, SD:/apps/paint.app/gradients/: GIMP reads them, and Paint reads
// GIMP's -- their segments' blending is taken as linear, their colours as RGB).
//
// Drawn along a line A -> B: linear (from A to B, the colour of the ends past them), bi-linear (the
// same on both sides of A), radial (circles around A, B on the last), square (squares around A),
// conical (the angle around A from the line); repeated or not (sawtooth, triangular); reversed.
//
// MIT licence (Onyx).
//
#ifndef _paint_pgrad_h
#define _paint_pgrad_h

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "pdoc.h"

namespace pd {

enum { GMAXSTOPS = 16, GMAX = 40 };
enum { GS_LINEAR, GS_BILINEAR, GS_RADIAL, GS_SQUARE, GS_CONICAL, GS_COUNT };
enum { GR_NONE, GR_SAW, GR_TRIANGLE };
static const char *const GSHAPE_NAMES[GS_COUNT] = { "Linear", "Bi-linear", "Radial", "Square", "Conical" };
static const char *const GREPEAT_NAMES[3] = { "No repeat", "Sawtooth", "Triangular" };

struct Gradient
{
	char name[40];
	int n;					// stops (>= 2), by position
	int pos[GMAXSTOPS];			// 0..1000
	unsigned col[GMAXSTOPS];		// 0xAARRGGBB, straight alpha
	int mid[GMAXSTOPS];			// stop i -> i + 1: the half-way point, 0..1000 of the segment (500)
	int dyn;				// 1: colour 1 -> 2, 2: colour 1 -> transparent (made from them each time)
	bool user;				// one of yours (saved; editable)
	char file[64];				// its file's name (yours)
};
static Gradient g_grads[GMAX]; static int g_ngrads;
static int g_grad = 0;					// the one in use
static int g_gshape = GS_LINEAR, g_grepeat = GR_NONE; static bool g_greverse;
static const char *GRAD_DIR = "SD:/apps/paint.app/gradients";

static void grad_set (Gradient &g, const char *name, int n, const int *pos, const unsigned *col)
{
	scpy (g.name, name, sizeof g.name); g.n = n; g.dyn = 0; g.user = false; g.file[0] = 0;
	for (int i = 0; i < n; i++) { g.pos[i] = pos[i]; g.col[i] = col[i]; g.mid[i] = 500; }
}
// The colours 1 and 2 put into the presets that follow them.
static void grad_dyn (Gradient &g, unsigned c1, unsigned c2)
{
	if (g.dyn == 1) { g.col[0] = c1 | 0xFF000000u; g.col[1] = c2 | 0xFF000000u; }
	else if (g.dyn == 2) { g.col[0] = c1 | 0xFF000000u; g.col[1] = c1 & 0xFFFFFF; }
}
static void grad_presets ()
{
	g_ngrads = 0;
	{ Gradient &g = g_grads[g_ngrads++]; int p[2] = { 0, 1000 }; unsigned c[2] = { 0xFF000000u, 0xFFFFFFFFu }; grad_set (g, "Colour 1 to 2", 2, p, c); g.dyn = 1; }
	{ Gradient &g = g_grads[g_ngrads++]; int p[2] = { 0, 1000 }; unsigned c[2] = { 0xFF000000u, 0 }; grad_set (g, "Colour 1 to transparent", 2, p, c); g.dyn = 2; }
	{ int p[3] = { 0, 550, 1000 }; unsigned c[3] = { 0xFF244660, 0xFF804E96, 0xFFF6A670 }; grad_set (g_grads[g_ngrads++], "Dusk hills", 3, p, c); }
	{ int p[3] = { 0, 450, 1000 }; unsigned c[3] = { 0xFF343C78, 0xFFDC6E78, 0xFFFCC478 }; grad_set (g_grads[g_ngrads++], "Sunset", 3, p, c); }
	{ int p[3] = { 0, 600, 1000 }; unsigned c[3] = { 0xFF082850, 0xFF1482AA, 0xFFBEECF0 }; grad_set (g_grads[g_ngrads++], "Ocean", 3, p, c); }
	{ int p[6] = { 0, 200, 400, 600, 800, 1000 }; unsigned c[6] = { 0xFFE62828, 0xFFFAA01E, 0xFFF0E628, 0xFF32B450, 0xFF286EDC, 0xFF8C3CC8 }; grad_set (g_grads[g_ngrads++], "Rainbow", 6, p, c); }
	{ int p[5] = { 0, 350, 500, 800, 1000 }; unsigned c[5] = { 0xFF5A5E64, 0xFFE1E4E8, 0xFF969AA0, 0xFFF0F2F4, 0xFF6E7278 }; grad_set (g_grads[g_ngrads++], "Metal", 5, p, c); }
	{ int p[4] = { 0, 400, 750, 1000 }; unsigned c[4] = { 0xFF280000, 0xFFC81E0A, 0xFFFAA014, 0xFFFFFAC8 }; grad_set (g_grads[g_ngrads++], "Fire", 4, p, c); }
}

// The colour at t (0..1) of g: the two stops around it, mixed by the segment's midpoint.
static unsigned grad_at (const Gradient &g, float t)
{
	if (t <= g.pos[0] / 1000.0f) return g.col[0];
	for (int i = 0; i + 1 < g.n; i++)
	{
		float a = g.pos[i] / 1000.0f, b = g.pos[i + 1] / 1000.0f;
		if (t > b) continue;
		float u = b > a ? (t - a) / (b - a) : 1, m = g.mid[i] / 1000.0f;
		if (m < 0.001f) m = 0.001f; else if (m > 0.999f) m = 0.999f;
		u = u <= m ? 0.5f * u / m : 0.5f + 0.5f * (u - m) / (1 - m);
		unsigned c0 = g.col[i], c1 = g.col[i + 1], o = 0;
		// (mixed premultiplied: a clear stop does not darken its neighbour's colour)
		float a0 = (c0 >> 24) / 255.0f, a1 = (c1 >> 24) / 255.0f, al = a0 + (a1 - a0) * u;
		for (int s = 0; s < 24; s += 8)
		{
			float v = al > 0 ? (((c0 >> s) & 255) * a0 * (1 - u) + ((c1 >> s) & 255) * a1 * u) / al : ((c0 >> s) & 255) * (1 - u) + ((c1 >> s) & 255) * u;
			int k = (int) (v + 0.5f); o |= (unsigned) (k < 0 ? 0 : k > 255 ? 255 : k) << s;
		}
		return o | (unsigned) (al * 255 + 0.5f) << 24;
	}
	return g.col[g.n - 1];
}
// 256 colours of g, for drawing a picture's worth of pixels.
static void grad_lut (const Gradient &g, unsigned *lut) { for (int i = 0; i < 256; i++) lut[i] = grad_at (g, i / 255.0f); }

// t (0..1) of the point (x, y) for a gradient along A -> B in the shape, repeat and direction set.
static inline float grad_t (float x, float y, float ax, float ay, float bx, float by)
{
	float dx = bx - ax, dy = by - ay, L2 = dx * dx + dy * dy;
	if (L2 < 1e-6f) return 0;
	float px = x - ax, py = y - ay, t;
	switch (g_gshape)
	{
	case GS_BILINEAR: t = fabsf ((px * dx + py * dy) / L2); break;
	case GS_RADIAL: t = sqrtf ((px * px + py * py) / L2); break;
	case GS_SQUARE: { float L = sqrtf (L2), u = fabsf ((px * dx + py * dy) / L2), v = fabsf ((py * dx - px * dy) / L2); (void) L; t = u > v ? u : v; break; }
	case GS_CONICAL: { float a = atan2f (py * dx - px * dy, px * dx + py * dy); t = a < 0 ? -a / 3.14159265f : a / 3.14159265f; break; }	// (symmetric)
	default: t = (px * dx + py * dy) / L2; break;
	}
	if (g_grepeat == GR_SAW) t = t - floorf (t);
	else if (g_grepeat == GR_TRIANGLE) { t = fmodf (fabsf (t), 2); if (t > 1) t = 2 - t; }
	t = t < 0 ? 0 : t > 1 ? 1 : t;
	return g_greverse ? 1 - t : t;
}

// ---- GIMP's .ggr --------------------------------------------------------------------------------------------
// Read one: its segments become stops (a segment whose left colour differs from the previous one's right:
// two stops at the same place, a sharp edge). False: not a gradient.
static bool ggr_parse (const char *s, unsigned n, Gradient &g)
{
	char line[512]; unsigned i = 0; int ln = 0, segs = -1, done = 0;
	g.n = 0; g.dyn = 0; g.user = true; scpy (g.name, "Untitled", sizeof g.name);
	auto chan = [] (float v) -> unsigned { int k = (int) (v * 255 + 0.5f); return (unsigned) (k < 0 ? 0 : k > 255 ? 255 : k); };
	while (i < n)
	{
		int k = 0;
		while (i < n && s[i] != '\n' && k < (int) sizeof line - 1) line[k++] = s[i++];
		while (i < n && s[i] != '\n') i++;
		i++;
		while (k > 0 && (line[k - 1] == '\r' || line[k - 1] == ' ')) k--;
		line[k] = 0;
		if (ln == 0) { if (!(line[0] == 'G' && line[1] == 'I' && line[2] == 'M' && line[3] == 'P')) return false; ln++; continue; }
		if (line[0] == 'N' && line[1] == 'a' && line[2] == 'm' && line[3] == 'e' && line[4] == ':') { const char *v = line + 5; while (*v == ' ') v++; scpy (g.name, v, sizeof g.name); ln++; continue; }
		if (segs < 0) { segs = atoi (line); if (segs <= 0) return false; ln++; continue; }
		if (done >= segs) break;
		float f[11]; char *p = line;
		for (int j = 0; j < 11; j++) { char *e; f[j] = strtof (p, &e); if (e == p) return false; p = e; }
		unsigned lc = chan (f[3]) << 16 | chan (f[4]) << 8 | chan (f[5]) | chan (f[6]) << 24;
		unsigned rc = chan (f[7]) << 16 | chan (f[8]) << 8 | chan (f[9]) | chan (f[10]) << 24;
		int L = (int) (f[0] * 1000 + 0.5f), Mi = (int) (f[1] * 1000 + 0.5f), R = (int) (f[2] * 1000 + 0.5f);
		if (g.n == 0 || g.col[g.n - 1] != lc || g.pos[g.n - 1] != L) { if (g.n < GMAXSTOPS) { g.pos[g.n] = L; g.col[g.n] = lc; g.mid[g.n] = 500; g.n++; } }
		if (g.n > 0) g.mid[g.n - 1] = R > L ? pclamp ((Mi - L) * 1000 / (R - L), 0, 1000) : 500;
		if (g.n < GMAXSTOPS) { g.pos[g.n] = R; g.col[g.n] = rc; g.mid[g.n] = 500; g.n++; }
		done++; ln++;
	}
	return g.n >= 2;
}
static void put_f (char *&o, float v) { int k = (int) (v * 1000000 + 0.5f); o += sprintf (o, "%d.%06d", k / 1000000, k % 1000000); }
// g as a .ggr file's text (out: room for 4 KB).
static int ggr_write (const Gradient &g, char *out)
{
	char *o = out;
	o += sprintf (o, "GIMP Gradient\nName: %s\n%d\n", g.name, g.n - 1);
	for (int i = 0; i + 1 < g.n; i++)
	{
		float l = g.pos[i] / 1000.0f, r = g.pos[i + 1] / 1000.0f, m = l + (r - l) * g.mid[i] / 1000.0f;
		put_f (o, l); *o++ = ' '; put_f (o, m); *o++ = ' '; put_f (o, r);
		for (int k = 0; k < 2; k++)
		{
			unsigned c = g.col[i + k];
			for (int s = 16; s >= 0; s -= 8) { *o++ = ' '; put_f (o, ((c >> s) & 255) / 255.0f); }
			*o++ = ' '; put_f (o, (c >> 24) / 255.0f);
		}
		o += sprintf (o, " 0 0 0 0\n");
	}
	*o = 0;
	return (int) (o - out);
}
// A name made into a file name.
static void ggr_file_name (const char *name, char *out, int cap)
{
	int k = 0;
	for (const char *s = name; *s && k < cap - 5; s++)
	{
		char c = *s;
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_') out[k++] = c;
		else if (c == ' ' && k) out[k++] = '-';
	}
	if (!k) out[k++] = 'g';
	out[k] = 0;
	scpy (out + k, ".ggr", cap - k);
}
// Yours, from the card (after the presets).
static void grad_load_user ()
{
	void *d = kapi_opendir (GRAD_DIR);
	if (!d) return;
	struct kapi_dirent e;
	while (g_ngrads < GMAX && kapi_readdir (d, &e) > 0)
	{
		if (e.is_dir) continue;
		const char *name = e.name;
		int n = slen (name);
		if (n < 5 || name[n - 4] != '.' || (name[n - 3] | 32) != 'g' || (name[n - 2] | 32) != 'g' || (name[n - 1] | 32) != 'r') continue;
		char path[200]; scpy (path, GRAD_DIR, sizeof path); int k = slen (path); path[k++] = '/'; scpy (path + k, name, (int) sizeof path - k);
		void *f = kapi_open (path);
		if (!f) continue;
		unsigned len = kapi_fsize (f);
		char *b = new char[len + 1];
		int r = kapi_read (f, b, len);
		kapi_close (f);
		if (r > 0)
		{
			Gradient &g = g_grads[g_ngrads];
			if (ggr_parse (b, (unsigned) r, g)) { scpy (g.file, name, sizeof g.file); g_ngrads++; }
		}
		delete[] b;
	}
	kapi_closedir (d);
}
static bool grad_save (Gradient &g)
{
	kapi_mkdir (GRAD_DIR);
	if (!g.file[0]) ggr_file_name (g.name, g.file, sizeof g.file);
	char path[200]; scpy (path, GRAD_DIR, sizeof path); int k = slen (path); path[k++] = '/'; scpy (path + k, g.file, (int) sizeof path - k);
	char *b = new char[GMAXSTOPS * 200 + 200];
	int n = ggr_write (g, b);
	bool ok = kapi_save_file (path, b, (unsigned) n) >= 0;
	delete[] b;
	return ok;
}
static void grad_remove (int i)
{
	if (i < 0 || i >= g_ngrads || !g_grads[i].user) return;
	char path[200]; scpy (path, GRAD_DIR, sizeof path); int k = slen (path); path[k++] = '/'; scpy (path + k, g_grads[i].file, (int) sizeof path - k);
	kapi_remove (path);
	for (int j = i; j + 1 < g_ngrads; j++) g_grads[j] = g_grads[j + 1];
	g_ngrads--;
	if (g_grad >= g_ngrads) g_grad = g_ngrads - 1;
}

} // namespace pd

#endif
