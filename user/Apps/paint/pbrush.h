//
// pbrush.h -- Paint's brushes: a stroke engine. A stroke keeps how much each pixel is covered (0..255,
// its own buffer): a dab raises the coverage of the pixels it touches to its own (no build-up where
// the dabs overlap -- but the airbrush's, which adds); each pixel is then the layer's pixel as it was
// before the stroke (the undo's tiles keep it) with the colour laid over it at that coverage x the
// opacity x the selection's. The eraser takes the alpha away the same way.
//
// The kinds: Pencil (hard square pixels, as before), Brush (round, a smooth edge), Soft round (its
// hardness: the part fully covered, the rest fading), Calligraphy (a flat nib at 45 degrees), Airbrush
// (sprayed dots that build up), Marker (a square-ish nib, translucent: half opacity by default), Crayon
// (grain), and the patterns -- Dots, Lines, Checks, Bricks, Hatching, Grid: colour 1 where the
// pattern's pixel is set, on the picture's grid (the strokes join seamlessly).
//
// MIT licence (Onyx).
//
#ifndef _paint_pbrush_h
#define _paint_pbrush_h

#include <math.h>
#include "psel.h"

namespace pd {

enum { BR_PENCIL, BR_BRUSH, BR_SOFT, BR_CALLIG, BR_AIR, BR_MARKER, BR_CRAYON,
       BR_DOTS, BR_LINES, BR_CHECKS, BR_BRICKS, BR_HATCH, BR_GRID, BR_COUNT };
enum { BR_FIRST_PATTERN = BR_DOTS, NPATTERNS = BR_COUNT - BR_DOTS };
static const char *const BRUSH_NAMES[BR_COUNT] = { "Pencil", "Brush", "Soft round", "Calligraphy", "Airbrush", "Marker", "Crayon",
	"Dots", "Lines", "Checks", "Bricks", "Hatching", "Grid" };
static const int BRUSH_OPACITY[BR_COUNT] = { 100, 100, 100, 100, 100, 50, 100, 100, 100, 100, 100, 100, 100 };

// A pattern's pixel (k: BR_DOTS..): set or not, at the picture's pixel (x, y).
static inline bool pat_on (int k, int x, int y)
{
	switch (k)
	{
	case BR_DOTS: return (x & 3) == 0 && (y & 3) == 0;
	case BR_LINES: return ((x + y) & 3) == 0;
	case BR_CHECKS: return ((x / 4 + y / 4) & 1) == 0;
	case BR_BRICKS: return (y & 7) == 0 || ((x + ((y >> 3) & 1) * 8) & 15) == 0;
	case BR_HATCH: return (x + y) % 6 == 0 || ((x - y) % 6 + 6) % 6 == 0;
	case BR_GRID: return (x & 7) == 0 || (y & 7) == 0;
	}
	return true;
}
static inline unsigned hash2 (int x, int y) { unsigned h = (unsigned) x * 374761393u + (unsigned) y * 668265263u; h = (h ^ (h >> 13)) * 1274126177u; return h ^ (h >> 16); }

class Stroke
{
public:
	Stroke () : m_cov (0), m_cap (0) {}

	// A stroke begins at (x, y) (pixel centres: + 0.5). size: the width in px; opacity, hardness 0..100.
	void begin (int kind, int size, int opacity, int hardness, unsigned colour, bool erase, float x, float y)
	{
		unsigned need = (unsigned) D.w * D.h;
		if (m_cap < need) { delete[] m_cov; m_cov = new unsigned char[need]; m_cap = need; for (unsigned i = 0; i < need; i++) m_cov[i] = 0; }
		m_kind = kind; m_size = size < 1 ? 1 : size; m_op = opacity * 255 / 100; m_hard = hardness / 100.0f;
		m_col = colour; m_erase = erase; m_touched = norect ();
		m_seed = 12345u;
		m_lx = x; m_ly = y; m_rest = 0;
		m_ix = (int) floorf (x); m_iy = (int) floorf (y);
		rec_begin ();
		dab (x, y);
	}
	// The pointer moved to (x, y): dabs along the way.
	void to (float x, float y)
	{
		if (m_kind == BR_PENCIL)			// (one stamp a pixel, Bresenham's)
		{
			int x1 = (int) floorf (x), y1 = (int) floorf (y);
			int dx = x1 > m_ix ? x1 - m_ix : m_ix - x1, sx = m_ix < x1 ? 1 : -1;
			int dy = -(y1 > m_iy ? y1 - m_iy : m_iy - y1), sy = m_iy < y1 ? 1 : -1, err = dx + dy;
			while (m_ix != x1 || m_iy != y1)
			{
				int e2 = 2 * err;
				if (e2 >= dy) { err += dy; m_ix += sx; }
				if (e2 <= dx) { err += dx; m_iy += sy; }
				dab (m_ix + 0.5f, m_iy + 0.5f);
			}
			return;
		}
		float dx = x - m_lx, dy = y - m_ly, len = sqrtf (dx * dx + dy * dy);
		float step = spacing ();
		if (len <= 0) return;
		float t = step - m_rest;
		while (t <= len) { dab (m_lx + dx * t / len, m_ly + dy * t / len); t += step; }
		m_rest = len - (t - step);
		m_lx = x; m_ly = y;
	}
	// Done: the undo record closed, the coverage cleared where it was used.
	void end ()
	{
		rec_end ();
		Rect r = m_touched;
		for (int y = r.y0; y < r.y1; y++) for (int x = r.x0; x < r.x1; x++) m_cov[(unsigned) y * D.w + x] = 0;
		m_touched = norect ();
	}
	// The coverage of a dab (for the gallery's samples and the cursor): kind's, at offset (dx, dy) of the centre.
	static float shape (int kind, float dx, float dy, float r, float hard)
	{
		float d = sqrtf (dx * dx + dy * dy);
		switch (kind)
		{
		case BR_SOFT:
		{
			float in = r * hard;
			if (d <= in) return 1;
			if (d >= r) return 0;
			float t = (d - in) / (r - in);
			return 1 - t * t * (3 - 2 * t);
		}
		case BR_CALLIG:
		{
			float u = (dx + dy) * 0.7071f, v = (dy - dx) * 0.7071f, a = r, b = r * 0.22f < 0.8f ? 0.8f : r * 0.22f;
			float e = sqrtf ((u / a) * (u / a) + (v / b) * (v / b));
			float c = (1 - e) * b + 0.5f;
			return c < 0 ? 0 : c > 1 ? 1 : c;
		}
		case BR_MARKER:
		{
			float m = fabsf (dx) > fabsf (dy) * 1.4f ? fabsf (dx) : fabsf (dy) * 1.4f;
			float c = r + 0.5f - m;
			return c < 0 ? 0 : c > 1 ? 1 : c;
		}
		default:
		{
			float c = r + 0.5f - d;
			return c < 0 ? 0 : c > 1 ? 1 : c;
		}
		}
	}

private:
	unsigned char *m_cov; unsigned m_cap;
	int m_kind, m_size, m_op; float m_hard;
	unsigned m_col; bool m_erase;
	Rect m_touched;
	float m_lx, m_ly, m_rest; int m_ix, m_iy;
	unsigned m_seed;

	float spacing () const
	{
		float r = m_size * 0.5f;
		if (m_kind == BR_AIR) return r * 0.45f > 1 ? r * 0.45f : 1;
		float s = r * (m_kind == BR_SOFT ? 0.18f : 0.25f);
		return s < 0.7f ? 0.7f : s;
	}
	unsigned rnd () { m_seed = m_seed * 1103515245u + 12345u; return m_seed >> 8; }

	// One pixel's coverage raised to c (or, the airbrush, c added); the pixel made again.
	inline void cover (int x, int y, int c, bool add)
	{
		if ((unsigned) x >= (unsigned) D.w || (unsigned) y >= (unsigned) D.h || c <= 0) return;
		unsigned char &v = m_cov[(unsigned) y * D.w + x];
		int nv = add ? pmin (255, v + c) : pmax ((int) v, c);
		if (nv == v) return;
		v = (unsigned char) nv;
		int s = sel_at (x, y);
		if (!s) return;
		int a = nv * m_op / 255 * s / 255;
		unsigned base = rec_orig (x, y), &d = D.lay[D.cur].px[(unsigned) y * D.w + x];
		if (m_erase) { unsigned ba = base >> 24; d = (base & 0xFFFFFF) | ((ba * (unsigned) (255 - a) + 127) / 255) << 24; if (!(d >> 24)) d = 0; }
		else d = a ? over (base, m_col | 0xFF000000u, (unsigned) a) : base;
	}
	void dab (float cx, float cy)
	{
		float r = m_size * 0.5f;
		int x0, y0, x1, y1;					// [x0, x1] x [y0, y1]
		if (m_kind == BR_PENCIL)
		{
			int ix = (int) floorf (cx), iy = (int) floorf (cy), a = -(m_size / 2);
			x0 = ix + a; y0 = iy + a; x1 = x0 + m_size - 1; y1 = y0 + m_size - 1;
		}
		else { x0 = (int) floorf (cx - r - 1); y0 = (int) floorf (cy - r - 1); x1 = (int) ceilf (cx + r + 1); y1 = (int) ceilf (cy + r + 1); }
		Rect b = mkrect (x0, y0, x1 + 1, y1 + 1); b.clip (D.w, D.h);
		if (b.empty ()) return;
		rec_touch (b);
		m_touched.add (b);
		if (m_kind == BR_PENCIL) { for (int y = b.y0; y < b.y1; y++) for (int x = b.x0; x < b.x1; x++) cover (x, y, 255, false); }
		else if (m_kind == BR_AIR)
		{
			int n = 4 + (int) (r * r * 0.12f);
			for (int i = 0; i < n; i++)
			{
				// (a gaussian-like spread: the mean of two uniforms, in a disc)
				float u = ((rnd () & 1023) + (rnd () & 1023)) / 1023.0f - 1, v = ((rnd () & 1023) + (rnd () & 1023)) / 1023.0f - 1;
				if (u * u + v * v > 1) continue;
				cover ((int) floorf (cx + u * r), (int) floorf (cy + v * r), 70, true);
			}
		}
		else
		{
			float h = m_kind == BR_SOFT ? m_hard : 1;
			for (int y = b.y0; y < b.y1; y++)
				for (int x = b.x0; x < b.x1; x++)
				{
					float c = shape (m_kind, x + 0.5f - cx, y + 0.5f - cy, r, h);
					if (c <= 0) continue;
					int ci = (int) (c * 255 + 0.5f);
					if (m_kind == BR_CRAYON) { unsigned g = hash2 (x, y) & 255; ci = g < 70 ? 0 : ci * (int) g / 255; }
					else if (m_kind >= BR_FIRST_PATTERN && !pat_on (m_kind, x, y)) continue;
					cover (x, y, ci, false);
				}
		}
		compose (b);
	}
};

} // namespace pd

#endif
