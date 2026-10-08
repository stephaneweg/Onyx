//
// Apps/photos/ui.h -- Photos' drawing helpers: the faces (DejaVu Sans at a few sizes), text cut to fit, the icons
// (drawn from their geometry: uikit/vpaint.h), the hit lists of the parts drawn by hand, the colours of the light
// library and of the dark viewer and editor.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
//
#ifndef _photos_ui_h
#define _photos_ui_h

#include "fontkit/uikitface.h"
#include "uikit/uikit.h"
#include "Apps/photos/lib.h"

namespace photos {

using namespace uikit;

// ---- the faces --------------------------------------------------------------------------------------------------------
enum { F_UI, F_SMALL, F_MID, F_H2, F_H1, F_TINY, F_N };
static FtTextFace *g_face[F_N];
static void faces_open ()
{
	static const int SZ[F_N] = { 13, 11, 14, 16, 22, 9 };
	for (int i = 1; i < F_N; i++) { g_face[i] = new FtTextFace; if (!g_face[i]->open ("DejaVu Sans", SZ[i])) { delete g_face[i]; g_face[i] = 0; } }
}
static inline int B_ (int style) { return style == 1 ? 2 : style; }		// (1 = bold here: uikit's 2)
static inline int tw (const char *s, int f = F_UI, int style = 0) { UkFaceScope sc (f == F_UI ? 0 : g_face[f]); return uk_tw (s, B_ (style)); }
static inline int fh (int f = F_UI) { UkFaceScope sc (f == F_UI ? 0 : g_face[f]); return uk_fh (); }
static inline void text (Canvas &cv, int x, int y, const char *s, unsigned c, int f = F_UI, int style = 0, int w = 0)
{
	UkFaceScope sc (f == F_UI ? 0 : g_face[f]);
	style = B_ (style);
	if (w > 0 && uk_tw (s, style) > w) { char b[600]; uk_text_fit (s, w, b, sizeof b, style); uk_text (cv, x, y, b, c, style); }
	else uk_text (cv, x, y, s, c, style);
}
static inline void text_v (Canvas &cv, int x, int y, int h, const char *s, unsigned c, int f = F_UI, int style = 0, int w = 0) { text (cv, x, y + (h - fh (f)) / 2, s, c, f, style, w); }
static inline void text_r (Canvas &cv, int xr, int y, int h, const char *s, unsigned c, int f = F_UI, int style = 0) { text (cv, xr - tw (s, f, style), y + (h - fh (f)) / 2, s, c, f, style); }
static inline void text_c (Canvas &cv, int x, int y, int w, int h, const char *s, unsigned c, int f = F_UI, int style = 0) { text (cv, x + (w - tw (s, f, style)) / 2, y + (h - fh (f)) / 2, s, c, f, style); }
static inline void fill_round (Canvas &cv, int x, int y, int w, int h, int r, unsigned c, int alpha = 255) { uk_rbox (cv, x, y, w, h, r, c, c, alpha); }
static void disc (Canvas &cv, int cx, int cy, int r, unsigned c, int alpha = 255) { VPath p; p.circle (V (cx), V (cy), V (r)); p.fill (cv, c, alpha); }
static void ring (Canvas &cv, int cx, int cy, int r, unsigned c, int w = 2) { VPath p; p.arc (V (cx), V (cy), V (r), 0, 360, V (w)); p.fill (cv, c); }

// ---- the colours --------------------------------------------------------------------------------------------------------------
// the library follows the desktop's theme; the viewer and the editor are dark, as photo apps are
static unsigned col_side () { return uk_mix (C_BG, C_FIELD, 80); }
static unsigned col_dim () { return uk_mix (C_FIELD, C_FIELD_TEXT, 140); }
static unsigned col_faint () { return uk_mix (C_FIELD, C_FIELD_TEXT, 80); }
static unsigned col_line () { return uk_mix (C_FIELD, C_FIELD_TEXT, 30); }
static unsigned col_sel () { return uk_mix (C_FIELD, C_ACCENT, 70); }
static const unsigned D_BG = 0x1E2024, D_BAR = 0x282A30, D_PANEL = 0x2A2D33, D_TEXT = 0xEBEBEE, D_DIM = 0xA0A4AA, D_FAINT = 0x6E7279, D_LINE = 0x3C4048, D_BTN = 0x464A52, D_FIELD = 0x34383F;
static const unsigned RED = 0xE0483E, AMBER = 0xF2B12E;

// a text field that says what goes in it while empty
class HintBox : public Textbox
{
public:
	char hint[120];
	HintBox (int l, int t, int w, int h, const char *hn) : Textbox (l, t, w, h, "") { scpy (hint, hn, sizeof hint); }
	void onDraw () override
	{
		Textbox::onDraw ();
		if (!text[0] && !hasFocus && hint[0]) text_v (canvas, 9, 0, height, hint, uk_mix (C_FIELD, C_FIELD_TEXT, 110), F_UI, 0, width - 16 - padR);
	}
};

// ---- the hit lists ---------------------------------------------------------------------------------------------------------
struct Hit { int x, y, w, h, kind, a, b; };
struct HitList
{
	Hit *h; int n, cap;
	HitList () : h (0), n (0), cap (0) {}
	void clear () { n = 0; }
	void add (int x, int y, int w, int hh, int kind, int a = 0, int b = 0)
	{
		if (n == cap) { int c = cap ? cap * 2 : 256; Hit *t = (Hit *) realloc (h, sizeof (Hit) * c); if (!t) return; h = t; cap = c; }
		h[n++] = Hit { x, y, w, hh, kind, a, b };
	}
	const Hit *at (int x, int y) const { for (int i = n - 1; i >= 0; i--) if (x >= h[i].x && y >= h[i].y && x < h[i].x + h[i].w && y < h[i].y + h[i].h) return &h[i]; return 0; }
};

// ---- the icons -----------------------------------------------------------------------------------------------------------------
enum { I_PHOTOS, I_HEART, I_HEART_O, I_CLOCK, I_ALBUM, I_FOLDER, I_PLAY, I_PAUSE, I_SEARCH, I_INFO, I_ROTATE_L, I_ROTATE_R, I_EDIT, I_SHARE, I_TRASH,
       I_BACK, I_NEXT, I_CROP, I_SUN, I_MAGIC, I_PLUS, I_CHECK, I_CLOSE, I_FILTERS, I_MINUS, I_GRID, I_SMALLPIC, I_BIGPIC };
static void icon (Canvas &cv, int id, int x, int y, int s, unsigned c)
{
	VPath p;
	int X = V (x), Y = V (y), u = V (s) / 24, w2 = 2 * u;
#define PX(a) (X + (a) * u)
#define PY(b) (Y + (b) * u)
	switch (id)
	{
	case I_PHOTOS: case I_SMALLPIC: case I_BIGPIC:
		p.rrect (PX (2), PY (4), 20 * u, 16 * u, 2 * u); p.fill (cv, c);
		{ VPath h; int t[] = { PX (4), PY (18), PX (10), PY (10), PX (14), PY (15), PX (16), PY (13), PX (20), PY (18) }; h.poly (t, 5); h.circle (PX (16), PY (8), 2 * u); h.fill (cv, 0xFFFFFF, 220); }
		return;
	case I_HEART: case I_HEART_O:
	{
		int t[] = { PX (12), PY (21), PX (3), PY (12), PX (2), PY (8), PX (4), PY (4), PX (8), PY (3), PX (12), PY (6), PX (16), PY (3), PX (20), PY (4), PX (22), PY (8), PX (21), PY (12) };
		VPath q; q.circle (PX (7), PY (8), 5 * u); q.circle (PX (17), PY (8), 5 * u);
		int tri[] = { PX (2) + u / 2, PY (10), PX (22) - u / 2, PY (10), PX (12), PY (21) };
		if (id == I_HEART) { q.poly (tri, 3); q.fill (cv, c); return; }
		p.polyline (t, 10, w2, true); break;
	}
	case I_CLOCK: p.arc (PX (12), PY (12), 9 * u, 0, 360, w2); p.line (PX (12), PY (7), PX (12), PY (12), w2); p.line (PX (12), PY (12), PX (16), PY (14), w2); break;
	case I_ALBUM: p.rrect (PX (7), PY (3), 14 * u, 14 * u, 2 * u); p.fill (cv, c, 150); { VPath h; h.rrect (PX (3), PY (7), 14 * u, 14 * u, 2 * u); h.fill (cv, c); } return;
	case I_FOLDER: p.rrect (PX (1), PY (6), 22 * u, 15 * u, 2 * u); p.rrect (PX (1), PY (3), 9 * u, 5 * u, 2 * u); break;
	case I_PLAY: { int t[] = { PX (6), PY (4), PX (20), PY (12), PX (6), PY (20) }; p.poly (t, 3); } break;
	case I_PAUSE: p.rect (PX (6), PY (4), 4 * u, 16 * u); p.rect (PX (14), PY (4), 4 * u, 16 * u); break;
	case I_SEARCH: p.arc (PX (10), PY (10), 6 * u, 0, 360, w2); p.line (PX (15), PY (15), PX (21), PY (21), 3 * u); break;
	case I_INFO: p.arc (PX (12), PY (12), 9 * u, 0, 360, w2); p.rect (PX (11), PY (10), w2, 7 * u); p.rect (PX (11), PY (6), w2, w2); break;
	case I_ROTATE_L: case I_ROTATE_R:
	{
		bool L = id == I_ROTATE_L;
		if (L) { p.arc (PX (12), PY (13), 7 * u, 200, 470, w2); int t[] = { PX (2), PY (9), PX (8), PY (5), PX (9), PY (12) }; p.poly (t, 3); }
		else { p.arc (PX (12), PY (13), 7 * u, 70, 340, w2); int t[] = { PX (22), PY (9), PX (16), PY (5), PX (15), PY (12) }; p.poly (t, 3); }
		break;
	}
	case I_EDIT: { int t[] = { PX (4), PY (20), PX (5), PY (15), PX (16), PY (4), PX (20), PY (8), PX (9), PY (19) }; p.poly (t, 5); } break;
	case I_SHARE:
		p.line (PX (12), PY (3), PX (12), PY (15), w2);
		{ int t[] = { PX (7), PY (8), PX (12), PY (3), PX (17), PY (8) }; p.polyline (t, 3, w2); }
		{ int t[] = { PX (8), PY (11), PX (4), PY (11), PX (4), PY (21), PX (20), PY (21), PX (20), PY (11), PX (16), PY (11) }; p.polyline (t, 6, w2); }
		break;
	case I_TRASH: p.rect (PX (3), PY (5), 18 * u, w2); p.rect (PX (9), PY (2), 6 * u, 3 * u); p.rrect (PX (5), PY (8), 14 * u, 14 * u, u); p.fill (cv, c); { VPath h; h.rect (PX (9), PY (11), w2, 8 * u); h.rect (PX (13), PY (11), w2, 8 * u); h.fill (cv, 0xFFFFFF, 200); } return;
	case I_BACK: { int t[] = { PX (15), PY (5), PX (8), PY (12), PX (15), PY (19) }; p.polyline (t, 3, w2 + u / 2); } break;
	case I_NEXT: { int t[] = { PX (9), PY (5), PX (16), PY (12), PX (9), PY (19) }; p.polyline (t, 3, w2 + u / 2); } break;
	case I_CROP: { int a[] = { PX (6), PY (2), PX (6), PY (18), PX (22), PY (18) }; p.polyline (a, 3, w2); int b[] = { PX (2), PY (6), PX (18), PY (6), PX (18), PY (22) }; p.polyline (b, 3, w2); } break;
	case I_SUN:
		p.circle (PX (12), PY (12), 4 * u);
		for (int k = 0; k < 8; k++) { static const int D[8][4] = { { 12, 2, 12, 5 }, { 12, 19, 12, 22 }, { 2, 12, 5, 12 }, { 19, 12, 22, 12 }, { 5, 5, 7, 7 }, { 17, 17, 19, 19 }, { 5, 19, 7, 17 }, { 17, 7, 19, 5 } }; p.line (PX (D[k][0]), PY (D[k][1]), PX (D[k][2]), PY (D[k][3]), w2); }
		break;
	case I_MAGIC:
		p.line (PX (3), PY (21), PX (15), PY (9), 3 * u);
		{ int t[] = { PX (18), PY (2), PX (19), PY (5), PX (22), PY (6), PX (19), PY (7), PX (18), PY (10), PX (17), PY (7), PX (14), PY (6), PX (17), PY (5) }; p.poly (t, 8); }
		p.circle (PX (21), PY (13), u + u / 2); p.circle (PX (10), PY (4), u + u / 2);
		break;
	case I_PLUS: p.rect (PX (5), PY (11), 14 * u, w2); p.rect (PX (11), PY (5), w2, 14 * u); break;
	case I_MINUS: p.rect (PX (5), PY (11), 14 * u, w2); break;
	case I_CHECK: { int t[] = { PX (4), PY (12), PX (10), PY (18), PX (20), PY (6) }; p.polyline (t, 3, 3 * u); } break;
	case I_CLOSE: p.line (PX (6), PY (6), PX (18), PY (18), w2); p.line (PX (18), PY (6), PX (6), PY (18), w2); break;
	case I_FILTERS: p.arc (PX (9), PY (9), 6 * u, 0, 360, w2); p.arc (PX (15), PY (9), 6 * u, 0, 360, w2); p.arc (PX (12), PY (15), 6 * u, 0, 360, w2); break;
	case I_GRID: for (int k = 0; k < 4; k++) p.rrect (PX (3 + (k & 1) * 10), PY (3 + (k >> 1) * 10), 8 * u, 8 * u, u); break;
	}
#undef PX
#undef PY
	p.fill (cv, c);
}

// the selection's mark: a ticked disc, or an empty ring
static void check_mark (Canvas &cv, int x, int y, bool on, unsigned ringCol)
{
	if (on) { disc (cv, x + 11, y + 11, 11, C_ACCENT); ring (cv, x + 11, y + 11, 11, 0xFFFFFF, 2); icon (cv, I_CHECK, x + 4, y + 4, 14, 0xFFFFFF); }
	else { if (ringCol == 0xFFFFFF) disc (cv, x + 11, y + 11, 10, 0x000000, 40); ring (cv, x + 11, y + 11, 10, ringCol, 2); }
}

// ---- dates ------------------------------------------------------------------------------------------------------------------------
static const char *const MONTHS[12] = { TRN ("January"), TRN ("February"), TRN ("March"), TRN ("April"), TRN ("May"), TRN ("June"), TRN ("July"), TRN ("August"), TRN ("September"), TRN ("October"), TRN ("November"), TRN ("December") };
static const char *const WDAYS[7] = { TRN ("Thursday"), TRN ("Friday"), TRN ("Saturday"), TRN ("Sunday"), TRN ("Monday"), TRN ("Tuesday"), TRN ("Wednesday") };
static long long day_of (long long t) { return t >= 0 ? t / 86400 : (t - 86399) / 86400; }
// "Saturday 27 September 2026"
static void fmt_day (long long t, char *b, int cap)
{
	long long d = day_of (t); int y, m, dd; civil (d, &y, &m, &dd);
	long long today = day_of (now_local ());
	if (today && d == today) snprintf (b, cap, "%s", TR ("Today"));
	else if (today && d == today - 1) snprintf (b, cap, "%s", TR ("Yesterday"));
	else snprintf (b, cap, "%s %d %s %d", TR (WDAYS[((d % 7) + 7) % 7]), dd, TR (MONTHS[m - 1]), y);
}
static void fmt_time (long long t, char *b, int cap) { long long s = t - day_of (t) * 86400; snprintf (b, cap, "%02d:%02d", (int) (s / 3600), (int) (s / 60 % 60)); }
static int year_of (long long t) { int y, m, d; civil (day_of (t), &y, &m, &d); return y; }
static void fmt_size (unsigned long long n, char *b, int cap)
{
	if (n < 1024) snprintf (b, cap, TR ("%llu bytes"), n);
	else if (n < 1024 * 1024) snprintf (b, cap, TR ("%llu KB"), (n + 512) / 1024);
	else snprintf (b, cap, TR ("%llu.%llu MB"), n / 1048576, n % 1048576 * 10 / 1048576);
}

} // namespace photos

#endif
