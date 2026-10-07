//
// Apps/pdf/ui.h -- the PDF Viewer's drawing helpers: the faces (sizes of DejaVu Sans), text that fits, the icons
// (drawn from their geometry: uikit/vpaint.h), the hit lists of the custom-drawn parts.
//
#ifndef _pdf_ui_h
#define _pdf_ui_h

#include "fontkit/uikitface.h"
#include "uikit/uikit.h"

namespace pdfv {

using namespace uikit;

// ---- the faces --------------------------------------------------------------------------------------------
enum { F_UI, F_SMALL, F_MID, F_H2, F_H1, F_TINY, F_N };
static FtTextFace *g_face[F_N];
static void faces_open ()
{
	static const int SZ[F_N] = { 13, 11, 15, 18, 26, 8 };
	for (int i = 1; i < F_N; i++) { g_face[i] = new FtTextFace; if (!g_face[i]->open ("DejaVu Sans", SZ[i])) { delete g_face[i]; g_face[i] = 0; } }
}
static inline int tw (const char *s, int f = F_UI, int style = 0) { UkFaceScope sc (f == F_UI ? 0 : g_face[f]); return uk_tw (s, style); }
static inline int fh (int f = F_UI) { UkFaceScope sc (f == F_UI ? 0 : g_face[f]); return uk_fh (); }
// s at x, the line's top y; cut with "..." past w (0: not cut)
static inline void text (Canvas &cv, int x, int y, const char *s, unsigned c, int f = F_UI, int style = 0, int w = 0)
{
	UkFaceScope sc (f == F_UI ? 0 : g_face[f]);
	if (w > 0 && uk_tw (s, style) > w) { char b[400]; uk_text_fit (s, w, b, sizeof b, style); uk_text (cv, x, y, b, c, style); }
	else uk_text (cv, x, y, s, c, style);
}
static inline void text_v (Canvas &cv, int x, int y, int h, const char *s, unsigned c, int f = F_UI, int style = 0, int w = 0) { text (cv, x, y + (h - fh (f)) / 2, s, c, f, style, w); }
static inline void text_r (Canvas &cv, int xr, int y, int h, const char *s, unsigned c, int f = F_UI, int style = 0) { text (cv, xr - tw (s, f, style), y + (h - fh (f)) / 2, s, c, f, style); }
static inline void text_c (Canvas &cv, int x, int y, int w, int h, const char *s, unsigned c, int f = F_UI, int style = 0) { text (cv, x + (w - tw (s, f, style)) / 2, y + (h - fh (f)) / 2, s, c, f, style); }

// ---- the hit lists ---------------------------------------------------------------------------------------------
struct Hit { int x, y, w, h, kind, a, b; };
struct HitList
{
	Hit h[400]; int n;
	HitList () : n (0) {}
	void clear () { n = 0; }
	void add (int x, int y, int w, int hh, int kind, int a = 0, int b = 0) { if (n < 400) h[n++] = Hit { x, y, w, hh, kind, a, b }; }
	const Hit *at (int x, int y) const { for (int i = n - 1; i >= 0; i--) if (x >= h[i].x && y >= h[i].y && x < h[i].x + h[i].w && y < h[i].y + h[i].h) return &h[i]; return 0; }
};

// ---- the icons ---------------------------------------------------------------------------------------------------
enum { I_SIDEBAR, I_OPEN, I_UP, I_DOWN, I_MINUS, I_PLUS, I_SINGLE, I_SCROLL, I_TWO, I_ROTATE, I_FULL, I_SEARCH, I_MORE, I_CLOSE,
       I_PDF, I_HOME, I_FOLDER, I_RIGHT, I_LEFT, I_LOCK, I_CHECK };
static void icon (Canvas &cv, int id, int x, int y, int s, unsigned c)
{
	VPath p;
	int X = V (x), Y = V (y), u = V (s) / 24;
#define PX(a) (X + (a) * u)
#define PY(b) (Y + (b) * u)
	switch (id)
	{
	case I_SIDEBAR:
		p.rrect (PX (2), PY (4), 20 * u, 16 * u, 2 * u); p.fill (cv, c);
		{ VPath h; h.rect (PX (10), PY (6), 10 * u, 12 * u); h.fill (cv, uk_mix (c, 0xFFFFFF, 230)); }
		return;
	case I_OPEN: p.rrect (PX (1), PY (6), 22 * u, 15 * u, 2 * u); p.rrect (PX (1), PY (3), 9 * u, 5 * u, 2 * u); p.fill (cv, 0xD6AA5A); return;
	case I_FOLDER: p.rrect (PX (1), PY (6), 22 * u, 15 * u, 2 * u); p.rrect (PX (1), PY (3), 9 * u, 5 * u, 2 * u); p.fill (cv, 0xD6AA5A); return;
	case I_UP: { int t[] = { PX (5), PY (15), PX (12), PY (8), PX (19), PY (15) }; p.polyline (t, 3, 2 * u + u / 2); } break;
	case I_DOWN: { int t[] = { PX (5), PY (9), PX (12), PY (16), PX (19), PY (9) }; p.polyline (t, 3, 2 * u + u / 2); } break;
	case I_LEFT: { int t[] = { PX (15), PY (5), PX (8), PY (12), PX (15), PY (19) }; p.polyline (t, 3, 2 * u + u / 2); } break;
	case I_RIGHT: { int t[] = { PX (9), PY (5), PX (16), PY (12), PX (9), PY (19) }; p.polyline (t, 3, 2 * u + u / 2); } break;
	case I_MINUS: p.rect (PX (5), PY (11), 14 * u, 2 * u + u / 2); break;
	case I_PLUS: p.rect (PX (5), PY (11), 14 * u, 2 * u + u / 2); p.rect (PX (11), PY (5), 2 * u + u / 2, 14 * u); break;
	case I_SINGLE:
		p.rrect (PX (6), PY (2), 12 * u, 20 * u, u); p.fill (cv, c);
		{ VPath b; b.rect (PX (8), PY (4), 8 * u, 16 * u); b.fill (cv, 0xFFFFFF); }
		return;
	case I_SCROLL:
		p.rrect (PX (6), PY (0), 12 * u, 10 * u, u); p.rrect (PX (6), PY (13), 12 * u, 10 * u, u); p.fill (cv, c);
		{ VPath b; b.rect (PX (8), PY (2), 8 * u, 6 * u); b.rect (PX (8), PY (15), 8 * u, 6 * u); b.fill (cv, 0xFFFFFF); }
		return;
	case I_TWO:
		p.rrect (PX (1), PY (3), 10 * u, 18 * u, u); p.rrect (PX (13), PY (3), 10 * u, 18 * u, u); p.fill (cv, c);
		{ VPath b; b.rect (PX (3), PY (5), 6 * u, 14 * u); b.rect (PX (15), PY (5), 6 * u, 14 * u); b.fill (cv, 0xFFFFFF); }
		return;
	case I_ROTATE: p.arc (PX (12), PY (13), 8 * u, -200, 70, 2 * u + u / 2); { int t[] = { PX (1), PY (7), PX (8), PY (7), PX (4), PY (14) }; p.poly (t, 3); } break;
	case I_FULL:
		{ int a[] = { PX (3), PY (9), PX (3), PY (3), PX (9), PY (3) }; p.polyline (a, 3, 2 * u + u / 2);
		  int b[] = { PX (15), PY (3), PX (21), PY (3), PX (21), PY (9) }; p.polyline (b, 3, 2 * u + u / 2);
		  int d[] = { PX (3), PY (15), PX (3), PY (21), PX (9), PY (21) }; p.polyline (d, 3, 2 * u + u / 2);
		  int e[] = { PX (15), PY (21), PX (21), PY (21), PX (21), PY (15) }; p.polyline (e, 3, 2 * u + u / 2); } break;
	case I_SEARCH: p.arc (PX (10), PY (10), 6 * u, 0, 360, 2 * u); p.line (PX (15), PY (15), PX (21), PY (21), 3 * u); break;
	case I_MORE: for (int k = 0; k < 3; k++) p.circle (PX (12), PY (5 + k * 7), 2 * u); break;
	case I_CLOSE: p.line (PX (6), PY (6), PX (18), PY (18), 2 * u); p.line (PX (18), PY (6), PX (6), PY (18), 2 * u); break;
	case I_HOME: { int t[] = { PX (12), PY (2), PX (23), PY (12), PX (20), PY (12), PX (20), PY (22), PX (14), PY (22), PX (14), PY (15), PX (10), PY (15), PX (10), PY (22),
				   PX (4), PY (22), PX (4), PY (12), PX (1), PY (12) }; p.poly (t, 11); } break;
	case I_PDF:
		{ int t[] = { PX (4), PY (0), PX (15), PY (0), PX (20), PY (5), PX (20), PY (24), PX (4), PY (24) }; p.poly (t, 5); p.fill (cv, 0xFFFFFF);
		  VPath o; o.polyline (t, 5, u + u / 2, true); o.fill (cv, 0x968C86);
		  VPath r; r.rrect (PX (1), PY (12), 16 * u, 7 * u, u); r.fill (cv, 0xC83C32); }
		return;
	case I_LOCK: p.arc (PX (12), PY (9), 5 * u, 180, 360, 2 * u + u / 2); p.rect (PX (7), PY (9), 2 * u + u / 2, 3 * u); p.rect (PX (15), PY (9), 2 * u + u / 2, 3 * u); p.rrect (PX (4), PY (11), 16 * u, 12 * u, 2 * u); break;
	case I_CHECK: { int t[] = { PX (4), PY (12), PX (10), PY (18), PX (20), PY (6) }; p.polyline (t, 3, 3 * u); } break;
	}
#undef PX
#undef PY
	p.fill (cv, c);
}

} // namespace pdfv

#endif
