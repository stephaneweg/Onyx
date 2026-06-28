//
// wtk/widget.cpp -- Widget engine: tree, two-bit damage, recursive composite +
// mouse/key routing, focus. Compiled into libwtk.a.
//
#include "wtk/widget.h"

namespace wtk {

Widget::Widget (int l, int t, int w, int h)
  : left (l), top (t), width (w), height (h), scrollX (0), scrollY (0), tag (0),
    colSpan (1), rowSpan (1), anchor (ANCHOR_LEFT | ANCHOR_TOP), lytW (w), lytH (h),
    transparent (false), valid (false), shouldRedraw (true), hidden (false),
    hasFocus (false), canFocus (false), catchOutside (false), modal (false),
    disabled (false), hover (false), pressed (false),
    parent (0), firstChild (0), lastChild (0), prevSib (0), nextSib (0), prevHandled (0)
{ canvas.alloc (w, h); }

Widget::~Widget ()
{ Widget *c = firstChild; while (c) { Widget *n = c->nextSib; removeChild (c); delete c; c = n; } }

// ---- damage ------------------------------------------------------------------
void Widget::invalidate (bool redraw)
{
	valid = false;
	if (redraw) shouldRedraw = true;
	if (parent) parent->invalidate (false);		// up: ancestors only re-blit
}

void Widget::resizeTo (int w, int h)
{
	if (w == width && h == height) return;
	width = w; height = h; canvas.resize (w, h); layout (); invalidate (true);
}

// Resize a surface-backed widget (adopted, over-allocated buffer): change only the
// logical size + relayout/cascade, WITHOUT reallocating the canvas (which would drop the
// shared surface). resizeTo is for owned canvases; setBounds for surface-backed ones.
void Widget::setBounds (int w, int h)
{
	if (w == width && h == height) return;
	width = w; height = h;
	canvas.setLogical (w, h);		// keep the adopted buffer + stride
	layout ();				// cascade (anchors / grid reflow)
	invalidate (true);
}

// ---- default layout: apply each child's anchor as I resize ------------------
// dW/dH = how much I grew since my children were last placed. A child rides the
// right/bottom edge (anchor RIGHT/BOTTOM without its opposite) or stretches (both
// opposite edges). Resizing a child cascades into its own layout().
void Widget::layout ()
{
	int dW = width - lytW, dH = height - lytH;
	if (dW == 0 && dH == 0) return;			// no growth (e.g. a plain addChild)
	for (Widget *c = firstChild; c; c = c->nextSib)
	{
		int a = c->anchor;
		bool L = (a & ANCHOR_LEFT) != 0,  R = (a & ANCHOR_RIGHT) != 0;
		bool T = (a & ANCHOR_TOP) != 0,   B = (a & ANCHOR_BOTTOM) != 0;
		if (R && !L) c->left += dW;		// ride the right edge
		if (B && !T) c->top  += dH;		// ride the bottom edge
		int nw = c->width  + ((L && R) ? dW : 0);	// stretch between left+right
		int nh = c->height + ((T && B) ? dH : 0);	// stretch between top+bottom
		if (nw != c->width || nh != c->height) c->resizeTo (nw, nh);
	}
	lytW = width; lytH = height;
}

// ---- tree --------------------------------------------------------------------
void Widget::addChild (Widget *c)
{
	if (c == 0 || c->parent) return;
	c->parent = this; c->prevSib = lastChild; c->nextSib = 0;
	if (lastChild) lastChild->nextSib = c; else firstChild = c;
	lastChild = c;
	layout ();			// a layout container re-flows to include the new child
	invalidate (true);
}

void Widget::removeChild (Widget *c)
{
	if (c == 0 || c->parent != this) return;
	if (prevHandled == c) prevHandled = 0;
	if (c->prevSib) c->prevSib->nextSib = c->nextSib; else firstChild = c->nextSib;
	if (c->nextSib) c->nextSib->prevSib = c->prevSib; else lastChild = c->prevSib;
	c->parent = 0; c->prevSib = c->nextSib = 0;
	layout ();			// a layout container re-flows the remaining children
	invalidate (true);
}

void Widget::bringToFront ()
{ if (parent && nextSib) { Widget *p = parent; p->removeChild (this); p->addChild (this); } }

// ---- recursive composite (port of GUI_ELEMENT.Draw) --------------------------
void Widget::draw ()
{
	if (valid) return;					// subtree already current
	if (shouldRedraw) { onDraw (); shouldRedraw = false; }
	for (Widget *c = firstChild; c; c = c->nextSib)
	{
		if (c->hidden) continue;			// an inactive tab: not composited
		c->draw ();					// refresh the child's own canvas if dirty
		canvas.putOther (c->canvas, c->left - scrollX, c->top - scrollY, c->transparent);
	}
	valid = true;
}

// ---- recursive mouse routing (port of GUI_ELEMENT.HandleMouse) ---------------
bool Widget::handleMouse (int mx, int my, int bl, int br, int bm, int wheel)
{
	// A modal child (a dialog) captures ALL of our input until it is removed: route
	// exclusively to the topmost modal child, never descending into the rest of the tree.
	for (Widget *n = lastChild; n; n = n->prevSib)
		if (n->modal)
		{
			int cmx = mx + scrollX, cmy = my + scrollY;
			bool h = (mx < 0) ? n->handleMouse (-1, -1, 0, 0, 0, 0)
					  : n->handleMouse (cmx - n->left, cmy - n->top, bl, br, bm, wheel);
			if (prevHandled && prevHandled != n) prevHandled->handleMouse (-1, -1, 0, 0, 0, 0);
			prevHandled = n;
			return h;
		}

	bool handled = false; Widget *who = 0;
	for (Widget *n = lastChild; n && !handled; n = n->prevSib)	// topmost first
	{
		if (n->hidden) continue;				// an inactive tab: not hit-tested
		int cmx = mx + scrollX, cmy = my + scrollY;		// account for our scroll
		bool over = cmx >= n->left && cmy >= n->top
			 && cmx < n->left + n->width && cmy < n->top + n->height;
		if (over || n->catchOutside)
			if (n->handleMouse (cmx - n->left, cmy - n->top, bl, br, bm, wheel))
			{ handled = true; who = n; }
		if (over) break;		// the one under the cursor blocks those behind it
	}
	if (!handled) handled = onMouse (mx, my, bl, br, bm, wheel);
	if (prevHandled && prevHandled != who)
		prevHandled->handleMouse (-1, -1, 0, 0, 0, 0);		// tell the old one: mouse left
	prevHandled = who;
	return handled;
}

bool Widget::handleKey (long k)
{
	// A modal child captures all keys too (route exclusively to the topmost one).
	for (Widget *n = lastChild; n; n = n->prevSib)
		if (n->modal) return n->handleKey (k);
	if (!hasFocus) return false;
	for (Widget *c = firstChild; c; c = c->nextSib)
		if (!c->hidden && c->hasFocus && c->handleKey (k)) return true;
	return onKey (k);
}

// ---- focus -------------------------------------------------------------------
void Widget::clearFocusTree () { hasFocus = false; for (Widget *c = firstChild; c; c = c->nextSib) c->clearFocusTree (); }
void Widget::focusPathUp ()    { hasFocus = true;  if (parent) parent->focusPathUp (); }
void Widget::setFocus ()
{
	Widget *r = this; while (r->parent) r = r->parent;	// root
	r->clearFocusTree ();
	focusPathUp ();
}

} // namespace wtk
