//
// wtk/widget.cpp -- Widget engine: tree, two-bit damage, recursive composite +
// mouse/key routing, focus. Compiled into libwtk.a.
//
#include "wtk/widget.h"

namespace wtk {

Widget::Widget (int l, int t, int w, int h)
  : left (l), top (t), width (w), height (h), scrollX (0), scrollY (0),
    transparent (false), valid (false), shouldRedraw (true),
    hasFocus (false), canFocus (false), catchOutside (false),
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
	width = w; height = h; canvas.resize (w, h); invalidate (true);
}

// ---- tree --------------------------------------------------------------------
void Widget::addChild (Widget *c)
{
	if (c == 0 || c->parent) return;
	c->parent = this; c->prevSib = lastChild; c->nextSib = 0;
	if (lastChild) lastChild->nextSib = c; else firstChild = c;
	lastChild = c;
	invalidate (true);
}

void Widget::removeChild (Widget *c)
{
	if (c == 0 || c->parent != this) return;
	if (prevHandled == c) prevHandled = 0;
	if (c->prevSib) c->prevSib->nextSib = c->nextSib; else firstChild = c->nextSib;
	if (c->nextSib) c->nextSib->prevSib = c->prevSib; else lastChild = c->prevSib;
	c->parent = 0; c->prevSib = c->nextSib = 0;
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
		c->draw ();					// refresh the child's own canvas if dirty
		canvas.putOther (c->canvas, c->left - scrollX, c->top - scrollY, c->transparent);
	}
	valid = true;
}

// ---- recursive mouse routing (port of GUI_ELEMENT.HandleMouse) ---------------
bool Widget::handleMouse (int mx, int my, int bl, int br, int bm, int wheel)
{
	bool handled = false; Widget *who = 0;
	for (Widget *n = lastChild; n && !handled; n = n->prevSib)	// topmost first
	{
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
	if (!hasFocus) return false;
	for (Widget *c = firstChild; c; c = c->nextSib)
		if (c->hasFocus && c->handleKey (k)) return true;
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
