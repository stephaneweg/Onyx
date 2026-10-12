//
// uikit/objnav.h -- A CANVAS OF OBJECTS BY THE KEYS (pocket, console: a D-pad, no pointer): the objects of a slide, the
// controls of a form being designed. No free pointer: the D-pad goes FROM OBJECT TO OBJECT (the nearest in that
// direction, by geometry -- as the focus ring goes over controls), M starts the MOVE mode and S the SIZE mode, in which
// the D-pad nudges the chosen object (8 px a press; with Shift: 1 px -- the size mode moves its bottom right corner), A /
// Enter keeps, B / Esc cancels, S or M switches the mode. Written once here for every such view (Slides, QBStudio's
// forms: docs/compact-shell/mockups/pocketterm/apps/ANSWERS.md, "One selection pattern for a canvas of objects").
//
// The view keeps a UkObjNav (a plain structure, as UkKeyPointer) and, where the keys drive (uk_keys_drive ()), gives
// its keys to uk_objnav_key with its objects' rectangles in ITS units (a slide's, a form's pixels); what comes back says
// what to do -- another object chosen, a mode begun, the rectangle changed (give it to the object), kept, cancelled
// (give nav.from back). Its drawing ends with uk_objnav_draw (the chosen object's frame and handles; in a mode the
// outline of where it was, its place or size said in a pill, the bounds' centre lines lit when the object is on them);
// its hint line (uikit/padhint.h) shows uk_objnav_hints' -- the mode's name, MOVE or SIZE, is the line's pill.
//
//   bool View::onKey (long k) {
//     if (!uk_keys_drive ()) ...the desktop's keys...
//     UkObjRect r[64]; int n = ...the objects' rectangles...;
//     switch (uk_objnav_key (nav, k, r, n, &page)) {
//     case UK_OBJ_PICKED: ...select object nav.sel...; return true;
//     case UK_OBJ_BEGAN: ...begin an undo step...; return true;
//     case UK_OBJ_CHANGED: ...the object's rectangle = nav.now...; return true;
//     case UK_OBJ_KEPT: ...end the undo step...; return true;
//     case UK_OBJ_CANCELLED: ...the object's rectangle = nav.from, the undo step dropped...; return true;
//     case UK_OBJ_USED: return true; }
//     ...the view's other keys (Enter edits, Backspace deletes)...
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef _uikit_objnav_h
#define _uikit_objnav_h

#include "uikit/canvas.h"
#include "uikit/padhint.h"

namespace uikit {

struct UkObjRect { int x, y, w, h; };
enum { UK_OBJ_PICK = 0, UK_OBJ_MOVE = 1, UK_OBJ_SIZE = 2 };		// UkObjNav::mode
enum { UK_OBJ_NO = 0,		// not a key of this mechanism: the view's own
       UK_OBJ_PICKED,		// nav.sel is another object
       UK_OBJ_BEGAN,		// a mode began (nav.mode): nav.from = nav.now = the object's rectangle
       UK_OBJ_CHANGED,		// nav.now changed: the object takes it
       UK_OBJ_KEPT,		// A / Enter: the mode ended, nav.now stays
       UK_OBJ_CANCELLED,	// B / Esc: the mode ended, the object takes nav.from back
       UK_OBJ_USED };		// taken, nothing changed (an edge reached, no object that way)

struct UkObjNav
{
	int	  sel = -1;			// the chosen object: its index in the view's list (-1: none)
	int	  mode = UK_OBJ_PICK;
	UkObjRect from = { 0, 0, 0, 0 }, now = { 0, 0, 0, 0 };	// in a mode: where the object was, where it is
	int	  step = 8, fine = 1;		// a nudge in the view's units (8 px on the screen), with Shift
	int	  minW = 8, minH = 8;		// the smallest the size mode goes
	int	  hintStep = 8, hintFine = 1;	// what the hints say of a nudge: pixels of the screen ("8 px", "+ 1 px")
};

// A key (see above). rects[0 .. n - 1]: the objects, in the view's units; bounds: the rectangle an object stays in
// while moved or sized (0: anywhere). nav.sel past the list is brought back into it.
int uk_objnav_key (UkObjNav &nav, long key, const UkObjRect *rects, int n, const UkObjRect *bounds = 0);
// A mode begun or ended by the view itself (a new object put "already in the move mode"; a view that loses the keys).
void uk_objnav_begin (UkObjNav &nav, int mode, const UkObjRect &rect);
void uk_objnav_end (UkObjNav &nav);
// The mechanism's hints now, for the hint line -> how many (out: room for 5); *mode: the pill's word (MOVE, SIZE), 0.
int uk_objnav_hints (const UkObjNav &nav, UkPadHint *out, const char **mode);
// Drawn last, over the view's own picture, in the canvas' pixels: `now` the chosen object's rectangle (its frame, its
// eight handles); in a mode `from` (0: none) the outline of where it was, `label` (0: none) its place or size in a pill
// over it ("X 448  Y 174"), `bounds` (0: none) the page whose centre lines light up while the object is centred on them.
void uk_objnav_draw (Canvas &cv, const UkObjNav &nav, const UkObjRect &now, const UkObjRect *from = 0, const char *label = 0, const UkObjRect *bounds = 0);

} // namespace uikit

#endif
