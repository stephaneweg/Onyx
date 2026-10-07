//
// Apps/media/ui.h -- Media Player's drawing helpers: the faces (sizes of DejaVu Sans), text that fits, the
// times, the icons (drawn from their geometry: uikit/vpaint.h), small lists of ints.
//
#ifndef _media_ui_h
#define _media_ui_h

#include "fontkit/uikitface.h"
#include "uikit/uikit.h"
#include "covers.h"

namespace media {

using namespace uikit;

// ---- the faces --------------------------------------------------------------------------------------------
enum { F_UI, F_SMALL, F_MID, F_BIG, F_H2, F_H1, F_N };
static FtTextFace *g_face[F_N];
static void faces_open ()
{
	static const int SZ[F_N] = { 13, 11, 15, 16, 20, 27 };
	for (int i = 0; i < F_N; i++) { g_face[i] = new FtTextFace; if (!g_face[i]->open ("DejaVu Sans", SZ[i])) { delete g_face[i]; g_face[i] = 0; } }
}
static inline int tw (const char *s, int f = F_UI, int style = 0) { UkFaceScope sc (f == F_UI ? 0 : g_face[f]); return uk_tw (s, style); }
static inline int fh (int f = F_UI) { UkFaceScope sc (f == F_UI ? 0 : g_face[f]); return uk_fh (); }
// s at x, the line's top y (a line of the face's height), cut with "..." past w (0: not cut)
static inline void text (Canvas &cv, int x, int y, const char *s, unsigned c, int f = F_UI, int style = 0, int w = 0)
{
	UkFaceScope sc (f == F_UI ? 0 : g_face[f]);
	if (w > 0 && uk_tw (s, style) > w) { char b[300]; uk_text_fit (s, w, b, sizeof b, style); uk_text (cv, x, y, b, c, style); }
	else uk_text (cv, x, y, s, c, style);
}
// centred vertically in a box of height h
static inline void text_v (Canvas &cv, int x, int y, int h, const char *s, unsigned c, int f = F_UI, int style = 0, int w = 0) { text (cv, x, y + (h - fh (f)) / 2, s, c, f, style, w); }
static inline void text_r (Canvas &cv, int xr, int y, int h, const char *s, unsigned c, int f = F_UI, int style = 0) { text (cv, xr - tw (s, f, style), y + (h - fh (f)) / 2, s, c, f, style); }
static inline void text_c (Canvas &cv, int x, int y, int w, int h, const char *s, unsigned c, int f = F_UI, int style = 0) { text (cv, x + (w - tw (s, f, style)) / 2, y + (h - fh (f)) / 2, s, c, f, style); }

static inline void fmt_time (char *b, int cap, long long ms)
{
	if (ms < 0) ms = 0;
	int s = (int) (ms / 1000);
	if (s >= 3600) snprintf (b, cap, "%d:%02d:%02d", s / 3600, s / 60 % 60, s % 60); else snprintf (b, cap, "%d:%02d", s / 60, s % 60);
}
static inline void fmt_long (char *b, int cap, long long ms)
{
	int m = (int) ((ms + 30000) / 60000);
	if (m >= 60) snprintf (b, cap, "%d h %02d min", m / 60, m % 60); else snprintf (b, cap, "%d min", m);
}

// ---- the icons --------------------------------------------------------------------------------------------
enum { I_HOME, I_PERSON, I_DISC, I_NOTE, I_TAG, I_FOLDER, I_HEART, I_HEART_O, I_CLOCK, I_LIST, I_PLUS, I_SEARCH, I_PLAY, I_PAUSE,
       I_PREV, I_NEXT, I_SHUFFLE, I_REPEAT, I_REPEAT1, I_VOLUME, I_MUTE, I_QUEUE, I_MINI, I_MORE, I_BACK, I_FWD, I_DOWN, I_EXPAND,
       I_CLOSE, I_GRID, I_ROWS, I_STAR, I_FILM, I_TV, I_FULL, I_UNFULL, I_BACK10, I_FWD10, I_CHECK };
static void icon (Canvas &cv, int id, int x, int y, int s, unsigned c)
{
	VPath p;
	int X = V (x), Y = V (y), u = V (s) / 24;			// u: 1/24 of the box
#define PX(a) (X + (a) * u)
#define PY(b) (Y + (b) * u)
	switch (id)
	{
	case I_HOME: { int t[] = { PX (12), PY (2), PX (23), PY (12), PX (20), PY (12), PX (20), PY (22), PX (14), PY (22), PX (14), PY (15), PX (10), PY (15), PX (10), PY (22),
				   PX (4), PY (22), PX (4), PY (12), PX (1), PY (12) }; p.poly (t, 11); } break;
	case I_PERSON: p.circle (PX (12), PY (7), 5 * u); p.ellipse (PX (12), PY (22), 9 * u, 8 * u); p.fill (cv, c); return;
	case I_DISC: p.arc (PX (12), PY (12), 9 * u, 0, 360, 2 * u + u / 2); p.circle (PX (12), PY (12), 3 * u); break;
	case I_NOTE: p.ellipse (PX (8), PY (18), 4 * u, 3 * u); p.rect (PX (10), PY (3), 2 * u, 15 * u);
		{ int t[] = { PX (10), PY (3), PX (20), PY (6), PX (20), PY (10), PX (12), PY (7) }; p.poly (t, 4); } break;
	case I_TAG: { int t[] = { PX (2), PY (2), PX (13), PY (2), PX (22), PY (12), PX (12), PY (22), PX (2), PY (13) }; p.poly (t, 5); p.hole (PX (7), PY (7), 2 * u); } break;
	case I_FOLDER: p.rrect (PX (1), PY (6), 22 * u, 15 * u, 2 * u); p.rrect (PX (1), PY (3), 9 * u, 5 * u, 2 * u); p.fill (cv, 0xD6AA5A); return;
	case I_HEART:
		p.circle (PX (8), PY (9), 5 * u + u / 2); p.circle (PX (16), PY (9), 5 * u + u / 2);
		{ int t[] = { PX (3), PY (11), PX (21), PY (11), PX (12), PY (21) }; p.poly (t, 3); }
		break;
	case I_HEART_O:
		p.arc (PX (8), PY (9), 4 * u + u / 2, 20, 200, 2 * u); p.arc (PX (16), PY (9), 4 * u + u / 2, -20, 160, 2 * u);
		p.line (PX (4), PY (11), PX (12), PY (20), 2 * u); p.line (PX (20), PY (11), PX (12), PY (20), 2 * u);
		break;
	case I_CLOCK: p.arc (PX (12), PY (12), 9 * u, 0, 360, 2 * u); { int t[] = { PX (12), PY (6), PX (12), PY (12), PX (16), PY (15) }; p.polyline (t, 3, 2 * u); } break;
	case I_LIST: for (int k = 0; k < 3; k++) { p.rect (PX (8), PY (5 + k * 6), 14 * u, 2 * u); p.circle (PX (4), PY (6 + k * 6), u + u / 2); } break;
	case I_PLUS: p.rect (PX (11), PY (4), 2 * u, 16 * u); p.rect (PX (4), PY (11), 16 * u, 2 * u); break;
	case I_SEARCH: p.arc (PX (10), PY (10), 6 * u, 0, 360, 2 * u); p.line (PX (15), PY (15), PX (21), PY (21), 3 * u); break;
	case I_PLAY: { int t[] = { PX (7), PY (4), PX (20), PY (12), PX (7), PY (20) }; p.poly (t, 3); } break;
	case I_PAUSE: p.rrect (PX (6), PY (4), 4 * u, 16 * u, u); p.rrect (PX (14), PY (4), 4 * u, 16 * u, u); break;
	case I_PREV: p.rect (PX (4), PY (5), 3 * u, 14 * u); { int t[] = { PX (20), PY (5), PX (20), PY (19), PX (8), PY (12) }; p.poly (t, 3); } break;
	case I_NEXT: p.rect (PX (17), PY (5), 3 * u, 14 * u); { int t[] = { PX (4), PY (5), PX (4), PY (19), PX (16), PY (12) }; p.poly (t, 3); } break;
	case I_SHUFFLE:
		{ int a[] = { PX (2), PY (7), PX (8), PY (7), PX (15), PY (17), PX (19), PY (17) }; p.polyline (a, 4, 2 * u);
		  int b[] = { PX (2), PY (17), PX (8), PY (17), PX (15), PY (7), PX (19), PY (7) }; p.polyline (b, 4, 2 * u);
		  p.arrowHead (PX (22), PY (7), 0, 5 * u, 3 * u); p.arrowHead (PX (22), PY (17), 0, 5 * u, 3 * u); } break;
	case I_REPEAT: case I_REPEAT1:
		{ int a[] = { PX (4), PY (12), PX (4), PY (7), PX (19), PY (7) }; p.polyline (a, 3, 2 * u);
		  int b[] = { PX (20), PY (12), PX (20), PY (17), PX (5), PY (17) }; p.polyline (b, 3, 2 * u);
		  p.arrowHead (PX (22), PY (7), 0, 5 * u, 3 * u); p.arrowHead (PX (2), PY (17), 180, 5 * u, 3 * u); }
		if (id == I_REPEAT1) { p.fill (cv, c); VPath o; o.rect (PX (11), PY (9), 2 * u, 6 * u); o.fill (cv, c); return; }
		break;
	case I_VOLUME: case I_MUTE:
		{ int t[] = { PX (2), PY (9), PX (7), PY (9), PX (13), PY (3), PX (13), PY (21), PX (7), PY (15), PX (2), PY (15) }; p.poly (t, 6); }
		if (id == I_VOLUME) { p.arc (PX (13), PY (12), 5 * u, -45, 45, 2 * u); p.arc (PX (13), PY (12), 9 * u, -45, 45, 2 * u); }
		else { p.line (PX (16), PY (8), PX (23), PY (16), 2 * u); p.line (PX (23), PY (8), PX (16), PY (16), 2 * u); }
		break;
	case I_QUEUE: for (int k = 0; k < 3; k++) p.rect (PX (2), PY (5 + k * 6), 12 * u, 2 * u);
		{ int t[] = { PX (16), PY (11), PX (23), PY (16), PX (16), PY (21) }; p.poly (t, 3); } break;
	case I_MINI: p.rect (PX (2), PY (4), 20 * u, 2 * u); p.rect (PX (2), PY (18), 20 * u, 2 * u); p.rect (PX (2), PY (4), 2 * u, 16 * u); p.rect (PX (20), PY (4), 2 * u, 16 * u);
		p.rrect (PX (11), PY (11), 8 * u, 6 * u, u); break;
	case I_MORE: for (int k = 0; k < 3; k++) p.circle (PX (5 + k * 7), PY (12), 2 * u); break;
	case I_BACK: { int t[] = { PX (15), PY (4), PX (7), PY (12), PX (15), PY (20) }; p.polyline (t, 3, 3 * u); } break;
	case I_FWD: { int t[] = { PX (9), PY (4), PX (17), PY (12), PX (9), PY (20) }; p.polyline (t, 3, 3 * u); } break;
	case I_DOWN: { int t[] = { PX (4), PY (8), PX (12), PY (16), PX (20), PY (8) }; p.polyline (t, 3, 3 * u); } break;
	case I_EXPAND: p.rect (PX (3), PY (3), 18 * u, 2 * u); p.rect (PX (3), PY (19), 18 * u, 2 * u); p.rect (PX (3), PY (3), 2 * u, 18 * u); p.rect (PX (19), PY (3), 2 * u, 18 * u); break;
	case I_CLOSE: p.line (PX (5), PY (5), PX (19), PY (19), 2 * u + u / 2); p.line (PX (19), PY (5), PX (5), PY (19), 2 * u + u / 2); break;
	case I_GRID: for (int k = 0; k < 4; k++) p.rrect (PX (3 + (k % 2) * 10), PY (3 + (k / 2) * 10), 8 * u, 8 * u, u); break;
	case I_ROWS: for (int k = 0; k < 3; k++) p.rrect (PX (3), PY (4 + k * 7), 18 * u, 3 * u, u); break;
	case I_STAR:
		{ int t[20]; for (int k = 0; k < 10; k++) { int rr = k % 2 ? 4 : 10; t[2 * k] = PX (12) + rr * u * uk_cos (k * 36 - 90) / 16384; t[2 * k + 1] = PY (12) + rr * u * uk_sin (k * 36 - 90) / 16384; } p.poly (t, 10); } break;
	case I_FILM:
		p.rrect (PX (2), PY (4), 20 * u, 16 * u, 2 * u);
		for (int k = 0; k < 4; k++) { p.hole (PX (5), PY (7 + k * 3 + (k > 1 ? 1 : 0)), u); p.hole (PX (19), PY (7 + k * 3 + (k > 1 ? 1 : 0)), u); }
		p.fill (cv, c); { VPath w; w.rect (PX (8), PY (6), 8 * u, 12 * u); w.fill (cv, 0xFFFFFF, 70); } return;
	case I_TV:
		p.rect (PX (2), PY (5), 20 * u, 2 * u); p.rect (PX (2), PY (17), 20 * u, 2 * u); p.rect (PX (2), PY (5), 2 * u, 14 * u); p.rect (PX (20), PY (5), 2 * u, 14 * u);
		p.line (PX (8), PY (1), PX (12), PY (5), 2 * u); p.line (PX (16), PY (1), PX (12), PY (5), 2 * u); p.rect (PX (8), PY (20), 8 * u, 2 * u); break;
	case I_FULL: case I_UNFULL:
		for (int k = 0; k < 4; k++)
		{
			int cx = k % 2 ? 21 : 3, cy = k / 2 ? 21 : 3, dx = k % 2 ? -1 : 1, dy = k / 2 ? -1 : 1;
			if (id == I_UNFULL) { cx = k % 2 ? 15 : 9; cy = k / 2 ? 15 : 9; dx = -dx; dy = -dy; }
			int t[] = { PX (cx), PY (cy + 6 * dy), PX (cx), PY (cy), PX (cx + 6 * dx), PY (cy) }; p.polyline (t, 3, 2 * u + u / 2);
		}
		break;
	case I_BACK10: { int t[] = { PX (12), PY (5), PX (12), PY (19), PX (2), PY (12) }; p.poly (t, 3); int q[] = { PX (22), PY (5), PX (22), PY (19), PX (12), PY (12) }; p.poly (q, 3); } break;
	case I_FWD10: { int t[] = { PX (2), PY (5), PX (2), PY (19), PX (12), PY (12) }; p.poly (t, 3); int q[] = { PX (12), PY (5), PX (12), PY (19), PX (22), PY (12) }; p.poly (q, 3); } break;
	case I_CHECK: { int t[] = { PX (4), PY (12), PX (10), PY (18), PX (20), PY (6) }; p.polyline (t, 3, 3 * u); } break;
	}
#undef PX
#undef PY
	p.fill (cv, c);
}
// a few bars moving (the song playing)
static void eq_bars (Canvas &cv, int x, int y, int s, unsigned c, unsigned t)
{
	for (int k = 0; k < 4; k++)
	{
		int h = s * (30 + (int) ((uk_sin ((int) (t * 37 + k * 71)) + 16384) * 70 / 32768)) / 100;
		cv.fillRect (x + k * s * 26 / 100, y + s - h, s * 18 / 100 + 1, h, c);
	}
}

} // namespace media

#endif
