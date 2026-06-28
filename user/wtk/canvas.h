//
// wtk/canvas.h -- Canvas: a 0x00RRGGBB pixel buffer with blit/fill/text (port of
// VMKernel's GIMAGE). Each Widget owns one. It either OWNS its pixels (alloc) or
// borrows them (adopt, e.g. the window canvas from kapi_create_window).
//
// `stride` is the buffer's real row width in pixels; `w` is the LOGICAL (visible) width.
// They differ when a Canvas shows a sub-rect of an over-allocated buffer -- e.g. a shell
// surface allocated at screen width but displayed at the (smaller, resizable) viewport
// size. All blits step rows by `stride` and clamp to `w`.
//
#ifndef _wtk_canvas_h
#define _wtk_canvas_h

namespace wtk {

#define WK_TRANSPARENT_KEY 0x00FF00FFu		// magenta = skipped on a transparent blit

class Font;					// wtk/font.h -- bitmap glyph table (drawFont)

class Canvas
{
public:
	unsigned *px; int w, h, stride; bool owns;
	int capH;					// allocated rows (stride = allocated row width)

	Canvas ();
	~Canvas ();

	void release ();
	bool alloc  (int W, int H);			// own a fresh WxH buffer (stride = W)
	void adopt  (unsigned *p, int W, int H);	// borrow a buffer, stride = W (window canvas)
	void adopt  (unsigned *p, int W, int H, int Stride);	// borrow a sub-rect of a wider buffer
	// Grow-only: reuses the buffer when W<=stride && H<=capH (just changes the logical
	// size); otherwise reallocates to ~2x (amortised). So a resizing widget (a key in a
	// reflowing grid) rarely reallocates. Content is lost on a real grow (caller repaints).
	bool resize (int W, int H);
	void setLogical (int W, int H);			// change the visible w/h only (buffer/stride kept)

	void clear     (unsigned c);
	void pixel     (int x, int y, unsigned c);
	void fillRect  (int x, int y, int rw, int rh, unsigned c);
	void frameRect (int x, int y, int rw, int rh, unsigned c);
	void text      (int x, int y, const char *s, unsigned c);
	// Draw `s` from a loaded bitmap Font at (x,y), top-left, scaled by `scale` (>=1), in
	// `style` (0=reg 1=italic 2=bold 3=bold+italic). Only ink pixels are written (the
	// background shows through). Implemented in font.cpp.
	void drawFont  (int x, int y, const char *s, const Font &f, unsigned color, int scale = 1, int style = 0);
	// Blit src into this canvas at (dx,dy); if transparent, magenta pixels are skipped.
	// Clipped to this canvas -> the parent only takes what fits (free clipping).
	void putOther  (const Canvas &src, int dx, int dy, bool transparent);
};

} // namespace wtk

#endif
