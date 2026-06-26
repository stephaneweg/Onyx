//
// wtk/canvas.cpp -- Canvas implementation (compiled into libwtk.a).
//
#include "canvas.h"
#include "kapi.h"		// kapi_draw_text_buf
// operator new[]/delete[] resolve at link from the app's onyxpp.hpp (do NOT include it
// here: its non-inline __cxa_* symbols are "include once per app").

namespace wtk {

Canvas::Canvas () : px (0), w (0), h (0), owns (false) {}
Canvas::~Canvas () { release (); }

void Canvas::release () { if (owns && px) delete [] px; px = 0; w = h = 0; owns = false; }

bool Canvas::alloc (int W, int H)
{
	release ();
	if (W < 1) W = 1;
	if (H < 1) H = 1;
	px = new unsigned[(unsigned) (W * H)];
	w = W; h = H; owns = (px != 0);
	return px != 0;
}

void Canvas::adopt (unsigned *p, int W, int H) { release (); px = p; w = W; h = H; owns = false; }

bool Canvas::resize (int W, int H) { return alloc (W, H); }

void Canvas::clear (unsigned c) { for (int i = 0; i < w * h; i++) px[i] = c; }

void Canvas::pixel (int x, int y, unsigned c)
{ if (x >= 0 && x < w && y >= 0 && y < h) px[y * w + x] = c; }

void Canvas::fillRect (int x, int y, int rw, int rh, unsigned c)
{
	for (int yy = y; yy < y + rh; yy++)
	{
		if (yy < 0 || yy >= h) continue;
		for (int xx = x; xx < x + rw; xx++)
			if (xx >= 0 && xx < w) px[yy * w + xx] = c;
	}
}

void Canvas::frameRect (int x, int y, int rw, int rh, unsigned c)
{
	fillRect (x, y, rw, 1, c);
	fillRect (x, y + rh - 1, rw, 1, c);
	fillRect (x, y, 1, rh, c);
	fillRect (x + rw - 1, y, 1, rh, c);
}

void Canvas::text (int x, int y, const char *s, unsigned c) { kapi_draw_text_buf (px, w, h, x, y, s, c); }

void Canvas::putOther (const Canvas &src, int dx, int dy, bool transparent)
{
	for (int y = 0; y < src.h; y++)
	{
		int yy = dy + y;
		if (yy < 0 || yy >= h) continue;
		for (int x = 0; x < src.w; x++)
		{
			int xx = dx + x;
			if (xx < 0 || xx >= w) continue;
			unsigned p = src.px[y * src.w + x];
			if (transparent && p == WK_TRANSPARENT_KEY) continue;
			px[yy * w + xx] = p;
		}
	}
}

} // namespace wtk
