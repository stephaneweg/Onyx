//
// uikit/paint.h -- the procedural painter of the desktop's look: no skin bitmap, everything drawn by
// code from a few theme colours (uikit/theme.h), integer arithmetic only (the apps build with
// -mgeneral-regs-only).
//
//   * A shade from a colour: a grey profile -- level 128 = the colour itself, above toward white,
//     below toward black -- so any hue lightens as well as darkens (uk_tone).
//   * A gradient: a colour a row, computed at the size being drawn (a button stretches at will).
//   * Rounded corners without a formula at draw time: per radius, a table made once of each of the
//     corner's rows' first pixel and the opacities of its partly covered pixels (the anti-
//     aliasing); a row is then a straight span plus a few pixels blended over what the canvas
//     holds -- so a widget first fills its canvas with its parent's background.
//   * The push button: a raised face (a gradient, a light top edge, an outline; pressed: a shade
//     darker), the same as a drop-down's box -- no frame round it.
//   * Small glyphs (a check, arrows, a cross...) from their geometry, anti-aliased by coverage.
//
#ifndef _uikit_paint_h
#define _uikit_paint_h

#include "uikit/canvas.h"

namespace uikit {

// ---- the canvas -------------------------------------------------------------------------------------
// The alpha mode: the canvas's pixels carry a transparency in their top byte (0 opaque .. 255
// see-through: a WIN_FLAG_ALPHA window -- clear it to 0xFF000000 first); what is drawn over a
// see-through pixel keeps its own colour, partly see-through where it is anti-aliased. Off by
// default (an opaque canvas: the blends are over what it holds).
void uk_paint_alpha (bool on);
// One pixel of c at opacity a (0..255) over what the canvas holds (the alpha mode heeded).
void uk_blend_px (Canvas &cv, int x, int y, unsigned c, int a);

// ---- colours ----------------------------------------------------------------------------------------
unsigned uk_tone (unsigned c, int level);		// the grey profile: 128 = c, 255 = white, 0 = black
unsigned uk_mix (unsigned a, unsigned b, int t);	// t = 0 (a) .. 256 (b)
static inline unsigned uk_over (unsigned dst, unsigned c, int alpha)	// c at opacity 0..255 over dst
{ return uk_mix (dst, c, alpha + (alpha >> 7)); }
int      uk_bright (unsigned c);			// 0..255 (0.30 R + 0.59 G + 0.11 B)
unsigned uk_ink_on (unsigned c);			// dark or white text on c

// ---- shapes -----------------------------------------------------------------------------------------
// The rounded corner of radius r (1..16): for each of its rows (the top one first), the x offset
// of its first pixel, the number of partly covered pixels from there and their opacities (0..255).
struct UkCorner { int r; unsigned char off[16], n[16], a[16][16]; };
const UkCorner &uk_corner (int r);

// A rounded box [x, x + w) x [y, y + h), corners of radius r (0 = square; which ones: UK_TL |
// UK_TR | UK_BL | UK_BR, all by default), filled with a vertical gradient from `top` (its first
// row) to `bottom` (its last), at opacity alpha (255 = opaque); the corners' edges blended over
// what the canvas holds.
enum { UK_TL = 1, UK_TR = 2, UK_BL = 4, UK_BR = 8, UK_ALL = 15 };
void uk_rbox (Canvas &cv, int x, int y, int w, int h, int r, unsigned top, unsigned bottom,
	      int alpha = 255, int corners = UK_ALL);
// Its 1-px outline in colour c at opacity alpha.
void uk_rline (Canvas &cv, int x, int y, int w, int h, int r, unsigned c, int alpha = 255,
	       int corners = UK_ALL);

// ---- the look's pieces ------------------------------------------------------------------------------
enum { UK_NORMAL = 0, UK_HOT = 1, UK_PRESSED = 2, UK_DISABLED = 3, UK_FOCUS = 0x10 };
// The push button in the box, from its face colour: a raised face as a drop-down's (uk_raised;
// it had a frame and a well round it: the name); returns the label's box in *bx..*bh (already
// 1 px lower right when pressed) if the pointers are given.
void uk_framed (Canvas &cv, int x, int y, int w, int h, unsigned face, int state,
		int *bx = 0, int *by = 0, int *bw = 0, int *bh = 0);
// A raised face (a header, a tab, a handle, a scroll bar's thumb): a gradient, a light top edge,
// an outline.
void uk_raised (Canvas &cv, int x, int y, int w, int h, int r, unsigned face, int state = UK_NORMAL);
// A sunken field (a text box, a list, a display): its background, a shadow along the top, an
// outline -- the accent's when focused.
void uk_sunken (Canvas &cv, int x, int y, int w, int h, int r, unsigned bg, bool focus = false);
// An etched line (a groove: dark over light) across a face.
void uk_etch_h (Canvas &cv, int x, int y, int w, unsigned face);
void uk_etch_v (Canvas &cv, int x, int y, int h, unsigned face);
// An etched rounded frame (a group box's), the same groove all round.
void uk_etch_box (Canvas &cv, int x, int y, int w, int h, int r, unsigned face);

// The controls' marks, s x s at (x, y) (state: UK_NORMAL / HOT / PRESSED / DISABLED | FOCUS):
// a check box (a sunken rounded field; checked: the accent, a white check), a radio button
// (the same, round; checked: a white dot on the accent), a switch (a pill: the accent when on,
// a knob that slides) w x h.
void uk_check_mark (Canvas &cv, int x, int y, int s, bool checked, int state);
void uk_radio_mark (Canvas &cv, int x, int y, int s, bool checked, int state);
void uk_switch_mark (Canvas &cv, int x, int y, int w, int h, bool on, int state);

// A scroll bar in the box: a groove (the track, a shade of `bg`), the thumb a raised pill at
// [pos, pos + len) along it (state: UK_HOT while pointed / dragged).
void uk_scroll_bar (Canvas &cv, int x, int y, int w, int h, bool vertical, int pos, int len,
		    unsigned bg, int state = UK_NORMAL);
// A slider's groove (the accent up to `fill` px) and its knob at `kx` (a raised rounded
// thumb), across w x h.
void uk_slider_mark (Canvas &cv, int x, int y, int w, int h, int fill, int kx, int kw, int state);
// A bar that fills (a progress bar): a sunken track, `fill` px of the accent's gradient.
void uk_progress_bar (Canvas &cv, int x, int y, int w, int h, int fill);

// A floating panel (a drop-down list, a menu, a drawer, a tooltip): a light face, rounded, an
// outline; its corners' outside set to the magenta key (the widget is blitted transparent: what
// lies below shows there), their inside blended toward the outline.
void uk_popup (Canvas &cv, int x, int y, int w, int h, int r, unsigned face);
// Set the pixels of the box's rounded corners that are mostly outside to the magenta key.
void uk_corner_key (Canvas &cv, int x, int y, int w, int h, int r);
// A highlighted row (a selection, the item under the pointer): the accent, rounded; dimmer
// when its list does not have the focus. The text on it: uk_hilite_ink.
void uk_hilite (Canvas &cv, int x, int y, int w, int h, int r, bool strong = true);
unsigned uk_hilite_ink (bool strong = true);
// A small window-like title strip (a dialog's): the active frame's colour, a gradient (Milk's down
// to C_FACE: the box under it, the dialog's face), its text bold and centred.
void uk_title_strip (Canvas &cv, int x, int y, int w, int h, const char *s, int r = 0);
// A glossy bead d x d at (x, y) in colour c (the Milk theme's title buttons, as OS X's): a sphere
// of gel -- darker at the top, lit at the bottom, a white gloss over its upper half, a dark rim;
// anti-aliased over what the canvas holds.
void uk_bead (Canvas &cv, int x, int y, int d, unsigned c);

// ---- glyphs -----------------------------------------------------------------------------------------
enum { WKG_CHECK, WKG_UP, WKG_DOWN, WKG_LEFT, WKG_RIGHT, WKG_CLOSE, WKG_MIN, WKG_MAX, WKG_MENU,
       WKG_DOT, WKG_PLUS, WKG_MINUS, WKG_RESTORE, WKG_CHEV_UP, WKG_CHEV_DOWN, WKG_CHEV_LEFT,
       WKG_CHEV_RIGHT, WKG_RING, WKG_LOCK, WKG_GEAR, WKG_POWER,
       WKG_RELOAD, WKG_HOME,	// (a circular arrow, a house: a browser's toolbar)
       WKG_HISTORY };		// (a clock: a browser's history)
// A glyph centred on (cx, cy), about `size` px across, in colour c.
void uk_glyph (Canvas &cv, int kind, int cx, int cy, int size, unsigned c);

// ---- text -------------------------------------------------------------------------------------------
// The theme's text in a box: centred, or left-aligned at x (vertically centred).
void uk_text_c (Canvas &cv, int x, int y, int w, int h, const char *s, unsigned c, int style = 0);
void uk_text_l (Canvas &cv, int x, int y, int h, const char *s, unsigned c, int style = 0);
// The text colour for a background: C_TEXT on the window's face and on what is as light (or as
// dark), else black or white (uk_ink_on): a label on a program's own colour stays readable.
unsigned uk_ink_for (unsigned bg);
int  uk_text_w (const char *s, int style = 0);

} // namespace uikit

#endif
