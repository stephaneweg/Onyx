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

class Widget;
typedef void (*Action) (Widget &);		// fired on click/toggle/change; gets the widget

class Widget
{
public:
	Canvas	 canvas;
	int	 left, top, width, height;	// geometry RELATIVE to the parent
	int	 scrollX, scrollY;		// content offset (scrollview)
	bool	 transparent;			// blit me with the magenta key?
	bool	 valid;				// subtree composited & current  (IsValid)
	bool	 shouldRedraw;			// my own content needs repaint   (ShouldRedraw)
	bool	 hasFocus, canFocus, catchOutside;
	bool	 disabled, hover, pressed;	// interaction state (widgets use these)

	Widget	*parent, *firstChild, *lastChild, *prevSib, *nextSib;
	Widget	*prevHandled;			// last child that took the mouse (for mouse-leave)

	Widget (int l, int t, int w, int h);
	virtual ~Widget ();

	// ---- damage ----------------------------------------------------------
	void invalidate (bool redraw);		// redraw=true: repaint me; false: just recompose up
	void resizeTo   (int w, int h);

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
};

} // namespace wtk

#endif
