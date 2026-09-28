//
// wtk/paint.h -- the procedural painter of the desktop's look: no skin bitmap, everything drawn by
// code from a few theme colours (wtk/theme.h), integer arithmetic only (the apps build with
// -mgeneral-regs-only).
//
//   * A shade from a colour: a grey profile -- level 128 = the colour itself, above toward white,
//     below toward black -- so any hue lightens as well as darkens (wk_tone).
//   * A gradient: a colour a row, computed at the size being drawn (a button stretches at will).
//   * Rounded corners without a formula at draw time: per radius, a table made once of each of the
//     corner's rows' first pixel and the opacities of its partly covered pixels (the anti-
//     aliasing); a row is then a straight span plus a few pixels blended over what the canvas
//     holds -- so a widget first fills its canvas with its parent's background.
//   * The user's framed button: a raised frame, a sunken well, the button set in it (raised;
//     pressed: flush with the well, a shade darker).
//   * Small glyphs (a check, arrows, a cross...) from their geometry, anti-aliased by coverage.
//
#ifndef _wtk_paint_h
#define _wtk_paint_h

#include "wtk/canvas.h"

namespace wtk {

// ---- colours ----------------------------------------------------------------------------------------
unsigned wk_tone (unsigned c, int level);		// the grey profile: 128 = c, 255 = white, 0 = black
unsigned wk_mix (unsigned a, unsigned b, int t);	// t = 0 (a) .. 256 (b)
static inline unsigned wk_over (unsigned dst, unsigned c, int alpha)	// c at opacity 0..255 over dst
{ return wk_mix (dst, c, alpha + (alpha >> 7)); }
int      wk_bright (unsigned c);			// 0..255 (0.30 R + 0.59 G + 0.11 B)
unsigned wk_ink_on (unsigned c);			// dark or white text on c

// ---- shapes -----------------------------------------------------------------------------------------
// The rounded corner of radius r (1..16): for each of its rows (the top one first), the x offset
// of its first pixel, the number of partly covered pixels from there and their opacities (0..255).
struct WkCorner { int r; unsigned char off[16], n[16], a[16][16]; };
const WkCorner &wk_corner (int r);

// A rounded box [x, x + w) x [y, y + h), corners of radius r (0 = square; which ones: WK_TL |
// WK_TR | WK_BL | WK_BR, all by default), filled with a vertical gradient from `top` (its first
// row) to `bottom` (its last), at opacity alpha (255 = opaque); the corners' edges blended over
// what the canvas holds.
enum { WK_TL = 1, WK_TR = 2, WK_BL = 4, WK_BR = 8, WK_ALL = 15 };
void wk_rbox (Canvas &cv, int x, int y, int w, int h, int r, unsigned top, unsigned bottom,
	      int alpha = 255, int corners = WK_ALL);
// Its 1-px outline in colour c at opacity alpha.
void wk_rline (Canvas &cv, int x, int y, int w, int h, int r, unsigned c, int alpha = 255,
	       int corners = WK_ALL);

// ---- the look's pieces ------------------------------------------------------------------------------
enum { WK_NORMAL = 0, WK_HOT = 1, WK_PRESSED = 2, WK_DISABLED = 3, WK_FOCUS = 0x10 };
// The framed button in the box, from its face colour; returns the button's own box in *bx..*bh
// (the label's: already 1 px lower right when pressed) if the pointers are given.
void wk_framed (Canvas &cv, int x, int y, int w, int h, unsigned face, int state,
		int *bx = 0, int *by = 0, int *bw = 0, int *bh = 0);
// A raised face (a header, a tab, a handle, a scroll bar's thumb): a gradient, a light top edge,
// an outline.
void wk_raised (Canvas &cv, int x, int y, int w, int h, int r, unsigned face, int state = WK_NORMAL);
// A sunken field (a text box, a list, a display): its background, a shadow along the top, an
// outline -- the accent's when focused.
void wk_sunken (Canvas &cv, int x, int y, int w, int h, int r, unsigned bg, bool focus = false);
// An etched line (a groove: dark over light) across a face.
void wk_etch_h (Canvas &cv, int x, int y, int w, unsigned face);
void wk_etch_v (Canvas &cv, int x, int y, int h, unsigned face);
// An etched rounded frame (a group box's), the same groove all round.
void wk_etch_box (Canvas &cv, int x, int y, int w, int h, int r, unsigned face);

// The controls' marks, s x s at (x, y) (state: WK_NORMAL / HOT / PRESSED / DISABLED | FOCUS):
// a check box (a sunken rounded field; checked: the accent, a white check), a radio button
// (the same, round; checked: a white dot on the accent), a switch (a pill: the accent when on,
// a knob that slides) w x h.
void wk_check_mark (Canvas &cv, int x, int y, int s, bool checked, int state);
void wk_radio_mark (Canvas &cv, int x, int y, int s, bool checked, int state);
void wk_switch_mark (Canvas &cv, int x, int y, int w, int h, bool on, int state);

// A scroll bar in the box: a groove (the track, a shade of `bg`), the thumb a raised pill at
// [pos, pos + len) along it (state: WK_HOT while pointed / dragged).
void wk_scroll_bar (Canvas &cv, int x, int y, int w, int h, bool vertical, int pos, int len,
		    unsigned bg, int state = WK_NORMAL);
// A slider's groove (the accent up to `fill` px) and its knob at `kx` (a raised rounded
// thumb), across w x h.
void wk_slider_mark (Canvas &cv, int x, int y, int w, int h, int fill, int kx, int kw, int state);
// A bar that fills (a progress bar): a sunken track, `fill` px of the accent's gradient.
void wk_progress_bar (Canvas &cv, int x, int y, int w, int h, int fill);

// A floating panel (a drop-down list, a menu, a drawer, a tooltip): a light face, rounded, an
// outline; its corners' outside set to the magenta key (the widget is blitted transparent: what
// lies below shows there), their inside blended toward the outline.
void wk_popup (Canvas &cv, int x, int y, int w, int h, int r, unsigned face);
// Set the pixels of the box's rounded corners that are mostly outside to the magenta key.
void wk_corner_key (Canvas &cv, int x, int y, int w, int h, int r);
// A highlighted row (a selection, the item under the pointer): the accent, rounded; dimmer
// when its list does not have the focus. The text on it: wk_hilite_ink.
void wk_hilite (Canvas &cv, int x, int y, int w, int h, int r, bool strong = true);
unsigned wk_hilite_ink (bool strong = true);
// A small window-like title strip (a dialog's): the active frame's colour, a gradient, its text
// bold and centred.
void wk_title_strip (Canvas &cv, int x, int y, int w, int h, const char *s, int r = 0);

// ---- glyphs -----------------------------------------------------------------------------------------
enum { WKG_CHECK, WKG_UP, WKG_DOWN, WKG_LEFT, WKG_RIGHT, WKG_CLOSE, WKG_MIN, WKG_MAX, WKG_MENU,
       WKG_DOT, WKG_PLUS, WKG_MINUS, WKG_RESTORE, WKG_CHEV_UP, WKG_CHEV_DOWN, WKG_CHEV_LEFT,
       WKG_CHEV_RIGHT, WKG_RING };
// A glyph centred on (cx, cy), about `size` px across, in colour c.
void wk_glyph (Canvas &cv, int kind, int cx, int cy, int size, unsigned c);

// ---- text -------------------------------------------------------------------------------------------
// The theme's text in a box: centred, or left-aligned at x (vertically centred).
void wk_text_c (Canvas &cv, int x, int y, int w, int h, const char *s, unsigned c, int style = 0);
void wk_text_l (Canvas &cv, int x, int y, int h, const char *s, unsigned c, int style = 0);
int  wk_text_w (const char *s, int style = 0);

} // namespace wtk

#endif
