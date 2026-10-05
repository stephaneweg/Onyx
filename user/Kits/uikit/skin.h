//
// uikit/skin.h -- 9-slice bitmap skin (port of uikit's Skin / the kernel CSkin) plus
// user-side window decoration (drawn by code: uikit/paint.h). A skin BMP holds `count` states stacked vertically
// (button.bmp: normal/hover/pressed); margins mark the fixed corners/edges, the middle
// tiles. Magenta (UK_TRANSPARENT_KEY) is the transparency key. Skins draw into RAW
// 0x00RRGGBB buffers -- a Canvas's `px`, or a window-chrome buffer from kapi_get_chrome.
//
#ifndef _uikit_skin_h
#define _uikit_skin_h

#include "uikit/canvas.h"			// UK_TRANSPARENT_KEY

namespace uikit {

// Multiply a 0x00RRGGBB pixel by a 0x00RRGGBB tint (per channel /255). 0xFFFFFF = no-op.
static inline unsigned uk_tint (unsigned c, unsigned t)
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

// Draw the window's frame (the modernised CDE: kapi v64 -- a gradient from the theme's frame
// colour, rounded corners, the theme's outline, the title buttons: the window menu, minimise,
// maximise, close; the title in bold) into both chrome copies of this window. No-op for a
// borderless window / no window. Drawn once per size, title, theme and state (guarded); Root
// calls it after creating its window; call it again after kapi_resize_window (the frame then
// follows the new size).
void uk_decorate_window ();
// The frame's state, drawn by uk_decorate_window (Root keeps it): the window can be maximised
// (its maximise button active), it is maximised (the button shows "restore"), the app answers
// the window menu (GUI_EVENT_WINCTL, as a Root does: else its button is greyed -- an app
// drawing its own window without a Root).
enum { UK_WIN_RESIZABLE = 1, UK_WIN_MAXIMISED = 2, UK_WIN_MENU = 4, UK_WIN_FIXED = 8 };	// (FIXED: no buttons)
void uk_window_state (int flags);	// set the frame's state (UK_WIN_* flags) for the next uk_decorate_window
int  uk_window_flags ();	// the UK_WIN_* flags last set (0 at start)
// A frame as uk_decorate_window draws it, into any W x H buffer (T: its title bar's height), in
// the colour `frame` -- a preview (the Theme app). Its corners' outside: see-through (top byte).
void uk_draw_frame (unsigned *fb, int W, int H, int T, const char *title, unsigned frame, bool active);

} // namespace uikit

#endif
