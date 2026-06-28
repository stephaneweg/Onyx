//
// wtk/skin.h -- 9-slice bitmap skin (port of uikit's Skin / the kernel CSkin) plus
// user-side window decoration. A skin BMP holds `count` states stacked vertically
// (button.bmp: normal/hover/pressed); margins mark the fixed corners/edges, the middle
// tiles. Magenta (WK_TRANSPARENT_KEY) is the transparency key. Skins draw into RAW
// 0x00RRGGBB buffers -- a Canvas's `px`, or a window-chrome buffer from kapi_get_chrome.
//
#ifndef _wtk_skin_h
#define _wtk_skin_h

#include "wtk/canvas.h"			// WK_TRANSPARENT_KEY

namespace wtk {

// Multiply a 0x00RRGGBB pixel by a 0x00RRGGBB tint (per channel /255). 0xFFFFFF = no-op.
static inline unsigned wk_tint (unsigned c, unsigned t)
{
	if (t == 0xFFFFFF) return c;
	unsigned r = (((c >> 16) & 0xFF) * ((t >> 16) & 0xFF)) / 255;
	unsigned g = (((c >> 8)  & 0xFF) * ((t >> 8)  & 0xFF)) / 255;
	unsigned b = (( c        & 0xFF) * ( t        & 0xFF)) / 255;
	return (r << 16) | (g << 8) | b;
}

class Skin
{
public:
	unsigned *pix; int imgW, imgH, count, sw, sh, ml, mr, mt, mb;
	Skin ();
	~Skin ();
	bool valid () const { return pix != 0 && sh > 0; }
	bool load (const char *path, int cnt, int l, int r, int t, int b);
	// Blit a magenta-keyed sub-rect of the skin into a raw target (fb, W x H).
	void blit (unsigned *fb, int W, int H, int sx, int sy, int bw, int bh,
		   int dx, int dy, unsigned tint = 0xFFFFFF);
	// 9-slice draw of `state` into the (x,y,w,h) box: fixed corners, tiled edges/centre.
	void drawOn (unsigned *fb, int W, int H, int state, int x, int y, int w, int h,
		     unsigned tint = 0xFFFFFF);
};

// The shared button skin (SD:/skins/button.bmp), loaded once; flat fallback if absent.
Skin &wk_button_skin ();

// Draw the standard window chrome (title bar + borders + close box + title text) into
// both chrome copies of this window. No-op for a borderless window / no window. Drawn
// once (guarded); Root calls it after creating its window.
void wk_decorate_window ();

} // namespace wtk

#endif
