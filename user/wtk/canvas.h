//
// wtk/canvas.h -- Canvas: a 0x00RRGGBB pixel buffer with blit/fill/text (port of
// VMKernel's GIMAGE). Each Widget owns one. It either OWNS its pixels (alloc) or
// borrows them (adopt, e.g. the window canvas from kapi_create_window).
//
#ifndef _wtk_canvas_h
#define _wtk_canvas_h

namespace wtk {

#define WK_TRANSPARENT_KEY 0x00FF00FFu		// magenta = skipped on a transparent blit

class Canvas
{
public:
	unsigned *px; int w, h; bool owns;

	Canvas ();
	~Canvas ();

	void release ();
	bool alloc  (int W, int H);			// own a fresh WxH buffer
	void adopt  (unsigned *p, int W, int H);	// borrow someone else's buffer (window canvas)
	bool resize (int W, int H);			// content is lost (caller repaints)

	void clear     (unsigned c);
	void pixel     (int x, int y, unsigned c);
	void fillRect  (int x, int y, int rw, int rh, unsigned c);
	void frameRect (int x, int y, int rw, int rh, unsigned c);
	void text      (int x, int y, const char *s, unsigned c);
	// Blit src into this canvas at (dx,dy); if transparent, magenta pixels are skipped.
	// Clipped to this canvas -> the parent only takes what fits (free clipping).
	void putOther  (const Canvas &src, int dx, int dy, bool transparent);
};

} // namespace wtk

#endif
