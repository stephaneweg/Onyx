//
// uikit/widget.h -- Widget: the base node of the recursive tree (port of VMKernel's
// GUI_ELEMENT). Owns a Canvas; lives in a sibling linked-list tree. Subclasses
// override the virtual onDraw/onMouse/onKey. Two damage bits drive lazy recompose:
//   shouldRedraw = MY content changed (repaint me) ; valid = my subtree is current.
//
#ifndef _uikit_widget_h
#define _uikit_widget_h

#include "uikit/abi.h"		// uikit as a shared library: the rules that keep old programs working
#include "uikit/canvas.h"
#include "appkit/appkit.h"		// kapi_font_width/height (uk_fw/uk_fh)
#include "uikit/theme.h"		// the palette
#include "uikit/paint.h"		// the painter
#include "uikit/text.h"		// the text face (uk_set_textface): uk_fw / uk_fh follow it

namespace uikit {

// ---- shared helpers + theme palette (ported from uikit's Ui defaults) --------
// uk_fh: the line height widgets lay text out with; uk_fw: a character's width (a digit's with a
// proportional face installed -- measure real text with uk_tw / uk_text_w).
static inline int uk_len (const char *s) { int n = 0; while (s && s[n]) n++; return n; }
static inline int uk_fw  () { if (uk_face_) return uk_face_fw_; int f = kapi_font_width  (); return f < 1 ? 8  : f; }
static inline int uk_fh  () { if (uk_face_) return uk_face_->height (); int f = kapi_font_height (); return f < 1 ? 16 : f; }

// (the palette -- C_BG, C_FACE, C_TEXT, C_ACCENT, C_FIELD... -- is the theme's: uikit/theme.h)

// ---- integrated vertical scrollbar (Textarea / RichTextBox) ------------------
// A widget that scrolls its own content reserves UK_SBW px on its right edge and shows a
// draggable thumb there only when the content overflows. uk_thumb computes the thumb
// rect; uk_thumb_pos is the drag inverse; uk_draw_vscroll paints it.
static const int UK_SBW = 10;			// reserved right-edge gutter width

struct UkThumb { bool show; int y, h; };	// thumb top/height within a track of trackH px

static inline UkThumb uk_thumb (long total, long view, long pos, int trackH)
{
	UkThumb t;
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
static inline long uk_thumb_pos (int cy, int trackH, long total, long view, int thumbH)
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
static inline void uk_draw_vscroll (Canvas &cv, int x, int y, int w, int trackH,
				    const UkThumb &t, unsigned bg, bool hot = false)
{
	uk_scroll_bar (cv, x, y, w, trackH, true, t.y, t.h < trackH ? t.h : 0, bg, hot ? UK_HOT : UK_NORMAL);
}

// A list's own scroll bar under the mouse (a widget that draws uk_draw_vscroll): its thumb dragged,
// a press on its groove a page up / down. The bar: barW px from barX, its track trackH px from ty;
// total / view / *pos in the list's units (rows, px). Called from onMouse before the rows are looked
// at: true = the bar took the event (*pos may have moved: repaint, and leave the rows alone). The
// thumb held goes on following the pointer outside the bar (mx < 0: the pointer left, it is let go).
struct UkBarDrag
{
	bool held, wasDown;
	UkBarDrag () : held (false), wasDown (false) {}
	bool mouse (int mx, int my, int bl, int barX, int barW, int ty, int trackH, long total, long view, long *pos)
	{
		bool down = bl != 0, press = down && !wasDown;
		wasDown = down;
		if (held)
		{
			if (!down || mx < 0) held = false;
			else { UkThumb t = uk_thumb (total, view, *pos, trackH); *pos = uk_thumb_pos (my - ty, trackH, total, view, t.h); }
			return true;
		}
		if (total <= view || mx < barX || mx >= barX + barW || my < ty || my >= ty + trackH) return false;
		if (press)
		{
			UkThumb t = uk_thumb (total, view, *pos, trackH);
			int cy = my - ty;
			if (cy >= t.y && cy < t.y + t.h) held = true;
			else
			{
				long p = *pos + (cy < t.y ? -view : view), most = total - view;
				*pos = p < 0 ? 0 : p > most ? most : p;
			}
		}
		return true;
	}
};

// The pointer's shape (kapi v81, KAPI_CURSOR_*). A widget asks for its shape from its onMouse, each time
// the pointer moves over it: uk_cursor (KAPI_CURSOR_TEXT) in a text field, _HAND over a link, _SIZE_H
// on a column's edge... Root::ptrEvent (root.cpp) starts each pointer event with the arrow and tells the kernel
// when what the widgets asked for has changed -- so a widget that asks nothing shows the arrow, and
// nobody has to put it back.
void uk_cursor (int shape);

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
	// The reserve (uikit is a shared library: uikit/abi.h). Programs allocate and derive widgets with
	// the layout they were built with: a field added by a later version of the library goes HERE
	// (or behind `ext`, which the library may allocate) -- never in the middle of a class, never
	// at the end of a derived one.
	void	*ext = 0;
	unsigned long reserved_[4] = { 0, 0, 0, 0 };

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
	// The reserve of virtual functions (uikit/abi.h): a program's vtables are built by its compiler
	// with the slots of the headers it was built with. A virtual added to Widget by a later version
	// of the library TAKES ONE OF THESE (renamed, same place): an older program's widgets then
	// answer with this empty default. Never add a virtual anywhere else in an exposed class.
	virtual void uk_reserved0 () {}
	virtual void uk_reserved1 () {}
	virtual void uk_reserved2 () {}
	virtual void uk_reserved3 () {}
	virtual void uk_reserved4 () {}
	virtual void uk_reserved5 () {}
	virtual void uk_reserved6 () {}
	virtual void uk_reserved7 () {}
};

} // namespace uikit

#endif
