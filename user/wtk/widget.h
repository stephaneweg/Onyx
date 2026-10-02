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
#include "wtk/theme.h"		// the palette
#include "wtk/paint.h"		// the painter
#include "wtk/text.h"		// the text face (wk_set_textface): wk_fw / wk_fh follow it

namespace wtk {

// ---- shared helpers + theme palette (ported from uikit's Ui defaults) --------
// wk_fh: the line height widgets lay text out with; wk_fw: a character's width (a digit's with a
// proportional face installed -- measure real text with wk_tw / wk_text_w).
static inline int wk_len (const char *s) { int n = 0; while (s && s[n]) n++; return n; }
static inline int wk_fw  () { if (wk_face_) return wk_face_fw_; int f = kapi_font_width  (); return f < 1 ? 8  : f; }
static inline int wk_fh  () { if (wk_face_) return wk_face_->height (); int f = kapi_font_height (); return f < 1 ? 16 : f; }

// (the palette -- C_BG, C_FACE, C_TEXT, C_ACCENT, C_FIELD... -- is the theme's: wtk/theme.h)

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

// Paint the bar (a groove in a shade of `bg`, the thumb a raised pill) at the right-edge
// gutter (x,y, w x trackH); hot: pointed / dragged.
static inline void wk_draw_vscroll (Canvas &cv, int x, int y, int w, int trackH,
				    const WkThumb &t, unsigned bg, bool hot = false)
{
	wk_scroll_bar (cv, x, y, w, trackH, true, t.y, t.h < trackH ? t.h : 0, bg, hot ? WK_HOT : WK_NORMAL);
}

class Widget;
class RadioButton;
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
	const char *tip;			// tooltip text (0 = none): Root shows it after a hover pause

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
	// Tab / Shift+Tab (back): the focus to the next / previous control of this subtree, in the
	// order they were added (depth first; hidden or disabled ones skipped, the ends wrap). fields:
	// only the text fields (isField). Done by handleKey when no one takes the Tab: in a dialog
	// (a modal) every control; in a window, from a text field to the next text field.
	bool tabFocus (bool back, bool fields);

	// ---- overridables ----------------------------------------------------
	virtual void onDraw () {}					// paint own content into `canvas`
	virtual bool onMouse (int, int, int, int, int, int) { return false; }
	virtual bool onKey (long) { return false; }
	virtual RadioButton *asRadio () { return 0; }	// (no RTTI) a RadioButton says so
	virtual bool isField () { return false; }	// a text field (Textbox and kin): a Tab stop in a window
	virtual void onTabFocus () {}			// focused by Tab (a field: its caret at the end)
	// The colour behind this widget (its parent's background): what its rounded, anti-aliased
	// corners blend into. A container with its own background returns it.
	virtual unsigned bgColor () { return parent ? parent->bgColor () : C_BG; }
	// Reposition/resize children on resize / child add/remove. The DEFAULT applies the
	// children's ANCHOR_* (passive resize); layout containers (Splitter, StackPanel,
	// UniformGridLayout, TabHost) override with explicit placement. Never call directly.
	virtual void layout ();
};

} // namespace wtk

#endif
