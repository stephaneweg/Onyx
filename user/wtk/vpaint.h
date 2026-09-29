//
// wtk/vpaint.h -- shapes from their geometry, anti-aliased: a toolbar's icons (a page, a folder, a
// floppy, arrows, scissors...), markers, anything drawn at any size without a bitmap. A VPath
// gathers outlines -- polygons, rectangles (rounded), discs, strokes (a line, a polyline, an arc:
// round ends and joins) -- then fill () paints their union in one colour over the canvas (the
// non-zero rule: the shapes added are all turned the same way, a hole () the other way).
// Coordinates are in 1/16 px (V (x) = x * 16), integers: the apps build without the FPU.
//
//   VPath p;
//   p.rrect (V (2), V (1), V (12), V (14), V (2));   p.fill (cv, 0xFFFFFF);
//   p.clear (); p.line (V (4), V (5), V (11), V (5), V (1)); p.fill (cv, ink);
//
#ifndef _wtk_vpaint_h
#define _wtk_vpaint_h

#include "wtk/canvas.h"

namespace wtk {

static inline int V (int px) { return px * 16; }

// sin and cos of an angle in degrees, x 16384.
int wk_sin (int deg);
static inline int wk_cos (int deg) { return wk_sin (deg + 90); }

class VPath
{
public:
	VPath ();
	~VPath ();
	void clear ();
	// Outlines (closed): a polygon of n points (xy: x0, y0, x1, y1...), a box, a rounded box,
	// a disc, a hole (a disc cut out of what is under it), an ellipse.
	void poly (const int *xy, int n);
	void rect (int x, int y, int w, int h);
	void rrect (int x, int y, int w, int h, int r);
	void circle (int cx, int cy, int r);
	void hole (int cx, int cy, int r);
	void ellipse (int cx, int cy, int rx, int ry);
	// Strokes w wide: a line, a polyline (closed: a loop), an arc from a0 to a1 degrees
	// (counter-clockwise from 3 o'clock, y down: 90 = 12 o'clock... as on paper).
	void line (int x0, int y0, int x1, int y1, int w);
	void polyline (const int *xy, int n, int w, bool closed = false);
	void arc (int cx, int cy, int r, int a0, int a1, int w);
	// An arrow head at (x, y) pointing toward angle deg, `len` long, `half` wide each side.
	void arrowHead (int x, int y, int deg, int len, int half);
	// Paint it: c at opacity alpha, shifted by (dx, dy) px.
	void fill (Canvas &cv, unsigned c, int alpha = 255, int dx = 0, int dy = 0);
private:
	int *m_x, *m_y, m_n, m_cap;		// the points
	int *m_cs, m_nc, m_ccap;		// each outline's first point
	void add (int x, int y);
	void begin ();
	void end (bool asHole = false);		// close the outline (turned the right way)
};

} // namespace wtk

#endif
