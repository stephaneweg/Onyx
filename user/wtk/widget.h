//
// wtk/widget.h -- Widget: the base node of the recursive tree (port of VMKernel's
// GUI_ELEMENT). Owns a Canvas; lives in a sibling linked-list tree. Subclasses
// override the virtual onDraw/onMouse/onKey. Two damage bits drive lazy recompose:
//   shouldRedraw = MY content changed (repaint me) ; valid = my subtree is current.
//
#ifndef _wtk_widget_h
#define _wtk_widget_h

#include "wtk/canvas.h"
#include "kapi.h"		// kapi_font_width/height (wk_fw/wk_fh)

namespace wtk {

// ---- shared helpers + theme palette (ported from uikit's Ui defaults) --------
static inline int wk_len (const char *s) { int n = 0; while (s && s[n]) n++; return n; }
static inline int wk_fw  () { int f = kapi_font_width  (); return f < 1 ? 8  : f; }
static inline int wk_fh  () { int f = kapi_font_height (); return f < 1 ? 16 : f; }

static const unsigned C_BG      = 0x00202830, C_FACE   = 0x00566074, C_FACE_HI = 0x00697690,
		      C_FACE_DN = 0x00404A5A, C_BORDER = 0x00161C24, C_TEXT   = 0x00FFFFFF,
		      C_ACCENT  = 0x0060FF90, C_DIS    = 0x008891A0, C_FIELD  = 0x00141A22;

// ---- integrated vertical scrollbar (Textarea / RichTextBox) ------------------
// A widget that scrolls its own content reserves WK_SBW px on its right edge and shows a
// draggable thumb there only when the content overflows. wk_thumb computes the thumb
// rect; wk_thumb_pos is the drag inverse; wk_draw_vscroll paints it.
static const int WK_SBW = 10;			// reserved right-edge gutter width

struct WkThumb { bool show; int y, h; };	// thumb top/height within a track of trackH px

static inline WkThumb wk_thumb (long total, long view, long pos, int trackH)
{
	WkThumb t;
	t.show = (total > view) && (view > 0) && (trackH > 6);
	if (!t.show) { t.y = 0; t.h = trackH; return t; }
	int th = (int) (view * trackH / total);
	if (th < 14) th = 14;
	if (th > trackH) th = trackH;
	long range = total - view;
	int trange = trackH - th;
	int ty = (range > 0) ? (int) (pos * trange / range) : 0;
	if (ty < 0) ty = 0;
	if (ty > trange) ty = trange;
	t.h = th; t.y = ty;
	return t;
}

// A cursor offset `cy` within the track -> scroll pos in [0, total-view] (thumb centred).
static inline long wk_thumb_pos (int cy, int trackH, long total, long view, int thumbH)
{
	int trange = trackH - thumbH; if (trange < 1) trange = 1;
	int p = cy - thumbH / 2;
	if (p < 0) p = 0;
	if (p > trange) p = trange;
	long range = total - view; if (range < 0) range = 0;
	return range * p / trange;
}

// Paint the track + thumb into a canvas at the right-edge gutter (x,y, w x trackH).
static inline void wk_draw_vscroll (Canvas &cv, int x, int y, int w, int trackH,
				    const WkThumb &t, unsigned trackCol, unsigned thumbCol)
{
	cv.fillRect (x, y, w, trackH, trackCol);
	if (t.h < trackH) cv.fillRect (x + 1, y + t.y, w - 2, t.h, thumbCol);
}

class Widget;
typedef void (*Action) (Widget &);		// fired on click/toggle/change; gets the widget

// Anchors (WinForms-style): which parent edges a child keeps a constant distance to as
// the parent resizes. The DEFAULT container layout() honours these so a child resizes /
// repositions passively (the app never recomputes geometry on resize):
//   LEFT+RIGHT  -> width stretches      | LEFT only  -> pinned left (default, fixed w)
//   TOP+BOTTOM  -> height stretches     | RIGHT only -> rides the right edge (reposition)
//   FILL        -> stretches both       | BOTTOM only-> rides the bottom edge (reposition)
// Resizing an anchored child cascades into ITS layout() (e.g. a grid re-flows its keys).
enum {
	ANCHOR_LEFT = 1, ANCHOR_TOP = 2, ANCHOR_RIGHT = 4, ANCHOR_BOTTOM = 8,
	ANCHOR_FILL = ANCHOR_LEFT | ANCHOR_TOP | ANCHOR_RIGHT | ANCHOR_BOTTOM
};

class Widget
{
public:
	Canvas	 canvas;
	int	 left, top, width, height;	// geometry RELATIVE to the parent
	int	 scrollX, scrollY;		// content offset (scrollview)
	int	 tag;				// app/dialog-defined id (e.g. dialog button result)
	int	 colSpan, rowSpan;		// cells spanned in a UniformGridLayout (default 1)
	int	 anchor;			// ANCHOR_* mask (default LEFT|TOP = pinned top-left)
	int	 lytW, lytH;			// my size at the last layout (for anchor deltas)
	bool	 transparent;			// blit me with the magenta key?
	bool	 valid;				// subtree composited & current  (IsValid)
	bool	 shouldRedraw;			// my own content needs repaint   (ShouldRedraw)
	bool	 hidden;			// skip me in composite + hit-test (e.g. an inactive tab)
	bool	 hasFocus, canFocus, catchOutside;
	bool	 modal;				// a modal child captures ALL of the parent's input
	bool	 disabled, hover, pressed;	// interaction state (widgets use these)

	Widget	*parent, *firstChild, *lastChild, *prevSib, *nextSib;
	Widget	*prevHandled;			// last child that took the mouse (for mouse-leave)

	Widget (int l, int t, int w, int h);
	virtual ~Widget ();

	// ---- damage ----------------------------------------------------------
	void invalidate (bool redraw);		// redraw=true: repaint me; false: just recompose up
	virtual void resizeTo (int w, int h);	// virtual: a surface-backed view overrides to pin its size
	void setBounds (int w, int h);		// resize the LOGICAL size (no canvas realloc) + relayout;
						// for surface-backed widgets (adopted over-allocated buffer)

	// ---- tree ------------------------------------------------------------
	void addChild    (Widget *c);
	void removeChild (Widget *c);
	void bringToFront ();			// move me last in z-order (topmost)

	// ---- recursive composite + event routing ----------------------------
	void draw ();				// recompose this subtree's canvas (lazy)
	bool handleMouse (int mx, int my, int bl, int br, int bm, int wheel);	// (mx,my) local
	bool handleKey   (long k);

	// ---- focus -----------------------------------------------------------
	void clearFocusTree ();
	void focusPathUp ();
	void setFocus ();			// focus me (clear the tree, light up my path)

	// ---- overridables ----------------------------------------------------
	virtual void onDraw () {}					// paint own content into `canvas`
	virtual bool onMouse (int, int, int, int, int, int) { return false; }
	virtual bool onKey (long) { return false; }
	// Reposition/resize children on resize / child add/remove. The DEFAULT applies the
	// children's ANCHOR_* (passive resize); layout containers (Splitter, StackPanel,
	// UniformGridLayout, TabHost) override with explicit placement. Never call directly.
	virtual void layout ();
};

} // namespace wtk

#endif
