//
// 3dforge/ficons.h -- 3DForge's icons, drawn from their geometry at any size (uikit/vpaint.h) on a 24-unit grid: the
// tools (box, cylinder, sketch, extrude, fillet, chamfer, move, the three operations, measure, export), the sketch's
// (line, rectangle, circle, arc, close) and the view's (home, fit, shading), a few marks.
//
// MIT licence (Onyx).
//
#ifndef _3dforge_ficons_h
#define _3dforge_ficons_h

#include "uikit/uikit.h"
#include "uikit/toolbar.h"

namespace forge {
using namespace uikit;

enum { I_BOX, I_CYL, I_SKETCH, I_EXTRUDE, I_FILLET, I_CHAMFER, I_MOVE, I_UNION, I_SUB, I_INT, I_NEWBODY, I_MEASURE, I_EXPORT,
       I_LINE, I_RECT, I_CIRCLE, I_ARC, I_CLOSE, I_HOME, I_FIT, I_SHADED, I_CHECK, I_INFO, I_EYE, I_WARN, I_COMBINE,
       I_PYRAMID, I_PRISM, I_TAPER, I_TORUS, I_SPHERE, I_SHAPES,
       I_NEW, I_OPEN, I_SAVE, I_UNDO, I_REDO };

// Icon `kind` in the s x s box at (x, y): ink the lines, acc the accent, bg what is behind it.
static void icon (Canvas &cv, int kind, int x, int y, int s, unsigned ink, unsigned acc, unsigned bg)
{
	if (kind >= I_NEW) { static const int map[] = { WKT_NEW, WKT_OPEN, WKT_SAVE, WKT_UNDO, WKT_REDO }; uk_tool_glyph (cv, map[kind - I_NEW], x, y, s, ink); return; }
	if (kind == I_SHAPES)				// (several solids: the bar's button that unfolds the shapes)
	{
		int k = s * 15 / 26;
		icon (cv, I_PYRAMID, x + s * 5 / 26, y - s / 26, k, ink, acc, bg); icon (cv, I_SPHERE, x + s - k + s / 26, y + s - k, k, ink, acc, bg);
		icon (cv, I_BOX, x - s / 26, y + s - k, k, ink, acc, bg); return;
	}
	VPath p; unsigned soft = uk_mix (acc, bg, 150);
	int w = s * 16 * 17 / 240; if (w < 18) w = 18;				// the stroke, 1/16 px
	auto X = [&] (double a) { return x * 16 + (int) (a * s * 16 / 24); };
	auto Y = [&] (double a) { return y * 16 + (int) (a * s * 16 / 24); };
	auto L = [&] (double a) { return (int) (a * s * 16 / 24); };
	auto poly = [&] (const double *q, int n, unsigned fillc, bool fill, unsigned line, bool stroke, int lw = 0)
	{
		int xy[40]; for (int i = 0; i < n; i++) { xy[i * 2] = X (q[i * 2]); xy[i * 2 + 1] = Y (q[i * 2 + 1]); }
		if (fill) { p.clear (); p.poly (xy, n); p.fill (cv, fillc); }
		if (stroke) { p.clear (); p.polyline (xy, n, lw ? lw : w, true); p.fill (cv, line); }
	};
	auto seg = [&] (double x0, double y0, double x1, double y1, unsigned c, int lw = 0) { p.clear (); p.line (X (x0), Y (y0), X (x1), Y (y1), lw ? lw : w); p.fill (cv, c); };
	auto dot = [&] (double cx, double cy, double r, unsigned c) { p.clear (); p.circle (X (cx), Y (cy), L (r)); p.fill (cv, c); };
	auto head = [&] (double cx, double cy, int deg, unsigned c) { p.clear (); p.arrowHead (X (cx), Y (cy), deg, L (6), L (4.2)); p.fill (cv, c); };
	unsigned dim = uk_mix (ink, bg, 110);
	switch (kind)
	{
	case I_BOX: case I_SHADED:
	{
		const double top[] = { 12, 3, 20, 7.5, 12, 12, 4, 7.5 }, hex[] = { 12, 3, 20, 7.5, 20, 16.5, 12, 21, 4, 16.5, 4, 7.5 };
		if (kind == I_SHADED)
		{
			const double l[] = { 4, 7.5, 12, 12, 12, 21, 4, 16.5 }, r[] = { 20, 7.5, 12, 12, 12, 21, 20, 16.5 };
			poly (top, 4, uk_mix (ink, bg, 200), true, 0, false); poly (l, 4, uk_mix (ink, bg, 90), true, 0, false); poly (r, 4, uk_mix (ink, bg, 150), true, 0, false);
			poly (hex, 6, 0, false, ink, true, w * 3 / 4); break;
		}
		poly (top, 4, soft, true, 0, false); poly (hex, 6, 0, false, ink, true);
		seg (4, 7.5, 12, 12, ink); seg (12, 12, 20, 7.5, ink); seg (12, 12, 12, 21, ink); break;
	}
	case I_CYL:
		p.clear (); p.ellipse (X (12), Y (6.5), L (7), L (3.3)); p.fill (cv, soft);
		p.clear (); p.ellipse (X (12), Y (6.5), L (7) + w / 2, L (3.3) + w / 2); p.fill (cv, ink);
		p.clear (); p.ellipse (X (12), Y (6.5), L (7) - w / 2, L (3.3) - w / 2); p.fill (cv, soft);
		seg (5, 6.5, 5, 17.5, ink); seg (19, 6.5, 19, 17.5, ink);
		{ int xy[26]; for (int i = 0; i <= 12; i++) { xy[i * 2] = X (12 + 7 * uk_cos (i * 15) / 16384.0); xy[i * 2 + 1] = Y (17.5 + 3.3 * uk_sin (i * 15) / 16384.0); } p.clear (); p.polyline (xy, 13, w); p.fill (cv, ink); }
		break;
	case I_PYRAMID:
	{
		const double l[] = { 12, 3, 4, 17, 12, 21 }, r[] = { 12, 3, 12, 21, 20, 17 }, o[] = { 12, 3, 20, 17, 12, 21, 4, 17 };
		poly (l, 3, soft, true, 0, false); poly (r, 3, uk_mix (soft, ink, 40), true, 0, false); poly (o, 4, 0, false, ink, true); seg (12, 3, 12, 21, ink); break;
	}
	case I_PRISM: case I_TAPER:
	{
		double t = kind == I_TAPER ? 3.5 : 0;			// (the top narrower)
		const double top[] = { 8 + t * 0.5, 3, 16 - t * 0.5, 3, 20 - t, 6.5, 16 - t * 0.5, 10, 8 + t * 0.5, 10, 4 + t, 6.5 };
		const double side[] = { 4 + t, 6.5, 8 + t * 0.5, 10, 16 - t * 0.5, 10, 20 - t, 6.5, 20, 17.5, 16, 21, 8, 21, 4, 17.5 };
		poly (side, 8, uk_mix (soft, bg, 110), true, ink, true); poly (top, 6, soft, true, ink, true);
		seg (8 + t * 0.5, 10, 8, 21, ink); seg (16 - t * 0.5, 10, 16, 21, ink); break;
	}
	case I_SPHERE:
		dot (12, 12, 9.4, ink); dot (12, 12, 9.4 - 1.6 * 24.0 / s * (w / 27.0), soft);
		{ int xy[26]; for (int i = 0; i <= 12; i++) { xy[i * 2] = X (12 + 8.6 * uk_cos (i * 15) / 16384.0); xy[i * 2 + 1] = Y (12 + 3.2 * uk_sin (i * 15) / 16384.0); } p.clear (); p.polyline (xy, 13, w * 3 / 4); p.fill (cv, ink); }
		break;
	case I_TORUS:
		p.clear (); p.ellipse (X (12), Y (12), L (10), L (6.4)); p.fill (cv, ink);
		p.clear (); p.ellipse (X (12), Y (12), L (10) - w, L (6.4) - w); p.fill (cv, soft);
		p.clear (); p.ellipse (X (12), Y (11.2), L (4.4) + w / 2, L (2.2) + w / 2); p.fill (cv, ink);
		p.clear (); p.ellipse (X (12), Y (11.2), L (4.4) - w / 2, L (2.2) - w / 2); p.fill (cv, bg);
		break;
	case I_SKETCH:
	{
		const double sq[] = { 3, 9, 15, 9, 15, 21, 3, 21 }, pen[] = { 11, 15, 12.2, 11, 19.5, 3.5, 22, 6, 14.8, 13.6 }, tip[] = { 11, 15, 12.2, 11, 14.8, 13.6 };
		poly (sq, 4, soft, true, ink, true); poly (pen, 5, acc, true, 0, false); poly (tip, 3, ink, true, 0, false); break;
	}
	case I_EXTRUDE: case I_EXPORT:
		if (kind == I_EXTRUDE) { const double base[] = { 3, 17.5, 12, 21.5, 21, 17.5, 12, 13.5 }; poly (base, 4, soft, true, ink, true); }
		else { seg (4, 14, 4, 20, ink); seg (4, 20, 20, 20, ink); seg (20, 20, 20, 14, ink); }
		seg (12, kind == I_EXTRUDE ? 17 : 15, 12, 6, acc, w * 5 / 4); head (12, 1.5, 90, acc); break;
	case I_FILLET: case I_CHAMFER:
	{
		double q[40]; int n = 0;
		q[n++] = 4; q[n++] = 21;
		if (kind == I_CHAMFER) { q[n++] = 4; q[n++] = 11; q[n++] = 11; q[n++] = 4; }
		else for (int i = 0; i <= 8; i++) { q[n++] = 12 - 8 * uk_cos (i * 90 / 8) / 16384.0; q[n++] = 12 - 8 * uk_sin (i * 90 / 8) / 16384.0; }
		int cut = n; q[n++] = 21; q[n++] = 4; q[n++] = 21; q[n++] = 21;
		poly (q, n / 2, soft, true, ink, true);
		int xy[20]; int m = 0; for (int i = 2; i < cut; i += 2) { xy[m++] = X (q[i]); xy[m++] = Y (q[i + 1]); }
		p.clear (); p.polyline (xy, m / 2, w * 3 / 2); p.fill (cv, acc); break;
	}
	case I_MOVE:
		seg (12, 5, 12, 19, ink); seg (5, 12, 19, 12, ink);
		p.clear (); p.arrowHead (X (12), Y (2), 90, L (4.5), L (3.4)); p.arrowHead (X (12), Y (22), 270, L (4.5), L (3.4));
		p.arrowHead (X (2), Y (12), 180, L (4.5), L (3.4)); p.arrowHead (X (22), Y (12), 0, L (4.5), L (3.4)); p.fill (cv, ink); break;
	case I_UNION: case I_SUB: case I_INT: case I_NEWBODY: case I_COMBINE:
	{
		const double a[] = { 3, 3, 15, 3, 15, 15, 3, 15 }, b[] = { 9, 9, 21, 9, 21, 21, 9, 21 };
		if (kind == I_UNION || kind == I_COMBINE)
		{
			const double u[] = { 3, 3, 15, 3, 15, 9, 21, 9, 21, 21, 9, 21, 9, 15, 3, 15 };
			poly (u, 8, soft, true, ink, true);
		}
		else if (kind == I_SUB) { poly (a, 4, soft, true, ink, true); poly (b, 4, bg, true, dim, true, w * 3 / 4); seg (9, 15, 9, 9, ink); seg (9, 9, 15, 9, ink); }
		else if (kind == I_INT) { poly (a, 4, 0, false, dim, true, w * 3 / 4); poly (b, 4, 0, false, dim, true, w * 3 / 4); const double c[] = { 9, 9, 15, 9, 15, 15, 9, 15 }; poly (c, 4, soft, true, ink, true); }
		else { const double c[] = { 5, 5, 19, 5, 19, 19, 5, 19 }; poly (c, 4, soft, true, ink, true); seg (12, 8.5, 12, 15.5, acc, w * 5 / 4); seg (8.5, 12, 15.5, 12, acc, w * 5 / 4); }
		break;
	}
	case I_MEASURE:
	{
		const double r[] = { 2, 15, 15, 2, 22, 9, 9, 22 };
		poly (r, 4, soft, true, ink, true);
		for (int i = 0; i < 4; i++) seg (6 + i * 3.2, 11.2 - i * 3.2, 8.2 + i * 3.2, 13.4 - i * 3.2, ink, w * 3 / 4);
		break;
	}
	case I_LINE: seg (4, 20, 20, 4, ink); dot (4, 20, 2.4, acc); dot (20, 4, 2.4, acc); break;
	case I_RECT: { const double r[] = { 4, 6, 20, 6, 20, 18, 4, 18 }; poly (r, 4, soft, true, ink, true); dot (4, 6, 2.4, acc); dot (20, 18, 2.4, acc); break; }
	case I_CIRCLE: dot (12, 12, 8.5 + 0.8, ink); dot (12, 12, 8.5 - 0.8, soft); dot (12, 12, 2.2, acc); break;
	case I_ARC:
		seg (6, 19, 6, 5, dim, w * 3 / 4); seg (6, 19, 20, 19, dim, w * 3 / 4);
		p.clear (); p.arc (X (6), Y (19), L (14), 0, 90, w); p.fill (cv, ink);
		dot (6, 19, 2.2, acc); dot (6, 5, 2.2, acc); dot (20, 19, 2.2, acc); break;
	case I_CLOSE:
		seg (5, 19, 5, 6, ink); seg (5, 6, 19, 6, ink); seg (19, 6, 19, 19, ink);
		for (int i = 0; i < 3; i++) seg (6.5 + i * 4.5, 19, 9 + i * 4.5, 19, acc, w * 5 / 4);
		dot (5, 19, 2.4, acc); dot (19, 19, 2.4, acc); break;
	case I_HOME: { const double h[] = { 4, 12, 12, 4.5, 20, 12, 17.5, 12, 17.5, 19.5, 6.5, 19.5, 6.5, 12 }; poly (h, 7, 0, false, ink, true); break; }
	case I_FIT:
		seg (4, 9, 4, 4, ink); seg (4, 4, 9, 4, ink); seg (15, 4, 20, 4, ink); seg (20, 4, 20, 9, ink);
		seg (20, 15, 20, 20, ink); seg (20, 20, 15, 20, ink); seg (9, 20, 4, 20, ink); seg (4, 20, 4, 15, ink);
		{ const double c[] = { 9, 9, 15, 9, 15, 15, 9, 15 }; poly (c, 4, ink, true, 0, false); } break;
	case I_CHECK: { int xy[] = { X (5), Y (12.5), X (10), Y (17.5), X (19), Y (6.5) }; p.clear (); p.polyline (xy, 3, w * 3 / 2); p.fill (cv, ink); break; }
	case I_INFO: dot (12, 12, 9.2, ink); dot (12, 12, 7.6, bg); seg (12, 11, 12, 16.5, ink, w * 5 / 4); dot (12, 7.5, 1.3, ink); break;
	case I_WARN: { const double t[] = { 12, 3, 22, 20, 2, 20 }; poly (t, 3, 0xF0B43A, true, ink, true, w * 3 / 4); seg (12, 9, 12, 14.5, ink, w * 5 / 4); dot (12, 17.3, 1.2, ink); break; }
	case I_EYE:
		p.clear (); p.ellipse (X (12), Y (12), L (8.5) + w / 2, L (5) + w / 2); p.fill (cv, ink);
		p.clear (); p.ellipse (X (12), Y (12), L (8.5) - w / 2, L (5) - w / 2); p.fill (cv, bg);
		dot (12, 12, 2.6, ink); break;
	}
}
static inline int icon_of_kind (int featKind)
{
	static const int map[F_KINDS] = { I_BOX, I_CYL, I_SKETCH, I_EXTRUDE, I_FILLET, I_CHAMFER, I_MOVE, I_COMBINE, I_PYRAMID, I_PRISM, I_TAPER, I_TORUS, I_SPHERE };
	return map[featKind];
}

// ---- drawing over the view: smooth lines, shapes, the values' tags ----------------------------------------------------
static void ov_line (Canvas &cv, double x0, double y0, double x1, double y1, double w, unsigned c, int alpha = 255)
{
	if (fabs (x0) > 8000 || fabs (y0) > 8000 || fabs (x1) > 8000 || fabs (y1) > 8000) return;
	VPath p; p.line ((int) (x0 * 16), (int) (y0 * 16), (int) (x1 * 16), (int) (y1 * 16), (int) (w * 16)); p.fill (cv, c, alpha);
}
static void ov_dash (Canvas &cv, double x0, double y0, double x1, double y1, double w, unsigned c, double on = 5, double off = 4)
{
	double l = hypot (x1 - x0, y1 - y0); if (l < 1 || l > 4000) return;
	VPath p;
	for (double t = 0; t < l; t += on + off)
	{
		double a = t / l, b = (t + on) / l; if (b > 1) b = 1;
		p.line ((int) ((x0 + (x1 - x0) * a) * 16), (int) ((y0 + (y1 - y0) * a) * 16), (int) ((x0 + (x1 - x0) * b) * 16), (int) ((y0 + (y1 - y0) * b) * 16), (int) (w * 16));
	}
	p.fill (cv, c);
}
static void ov_dot (Canvas &cv, double x, double y, double r, unsigned fill, unsigned line, double lw = 1.6)
{
	if (fabs (x) > 8000 || fabs (y) > 8000) return;
	VPath p; p.circle ((int) (x * 16), (int) (y * 16), (int) ((r + lw / 2) * 16)); p.fill (cv, line);
	p.clear (); p.circle ((int) (x * 16), (int) (y * 16), (int) ((r - lw / 2) * 16)); p.fill (cv, fill);
}
// An arrow from (x0, y0), its head at (x1, y1); knob: a round handle at its foot.
static void ov_arrow (Canvas &cv, double x0, double y0, double x1, double y1, unsigned c, bool knob)
{
	double l = hypot (x1 - x0, y1 - y0); if (l < 2) return;
	double ux = (x1 - x0) / l, uy = (y1 - y0) / l, hl = l < 14 ? l * 0.7 : 11;
	ov_line (cv, x0, y0, x1 - ux * hl * 0.8, y1 - uy * hl * 0.8, 2.4, c);
	int xy[6] = { (int) (x1 * 16), (int) (y1 * 16), (int) ((x1 - ux * hl - uy * hl * 0.45) * 16), (int) ((y1 - uy * hl + ux * hl * 0.45) * 16),
		      (int) ((x1 - ux * hl + uy * hl * 0.45) * 16), (int) ((y1 - uy * hl - ux * hl * 0.45) * 16) };
	VPath p; p.poly (xy, 3); p.fill (cv, c);
	if (knob) ov_dot (cv, x0, y0, 4.5, 0xFFFFFF, c, 2);
}
// A value on the drawing, centred on (x, y): white, rounded; active: being typed (the accent's frame, a caret).
static void ov_tag (Canvas &cv, int x, int y, const char *s, bool active, unsigned frame)
{
	int tw = uk_tw (s, active ? 2 : 0), w = tw + 16, h = 22, l = x - w / 2, t = y - h / 2;
	if (l < 2) l = 2; if (t < 2) t = 2; if (l + w > cv.w - 2) l = cv.w - 2 - w; if (t + h > cv.h - 2) t = cv.h - 2 - h;
	uk_rbox (cv, l, t, w, h, 5, 0xFFFFFF, 0xFFFFFF);
	uk_rline (cv, l, t, w, h, 5, active ? frame : 0x969AA4);
	if (active) uk_rline (cv, l + 1, t + 1, w - 2, h - 2, 4, frame);
	uk_text (cv, l + 8, t + (h - uk_fh ()) / 2, s, 0x1A1A1E, active ? 2 : 0);
	if (active) cv.fillRect (l + 9 + tw, t + 4, 1, h - 8, 0x1A1A1E);
}

} // namespace forge

#endif
