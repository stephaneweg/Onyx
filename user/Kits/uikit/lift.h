// uikit/lift.h -- (the library's own: not for the programs) a widget's open list over everything.
// Every parent clips its children: a drop-down in a narrow panel had its list cut by the panel, and
// drawn under what comes after the panel. While its list is open, the widget is therefore lifted out of
// its parent and made the last child of its window -- or of the dialog it is in (a modal child takes
// all its parent's input: a list lifted past it could not be clicked) --, at the place it has on the
// screen; closed, it goes back where it was, at its place among its parent's children (taken out
// without a re-flow: a layout container does not close the gap meanwhile).
// What is kept while lifted is in the widget's reserve (reserved_[0..3]: its parent, the child that
// followed it, its place, its anchors).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. Permission is hereby
// granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions: The above copyright notice and this permission notice shall
// be included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS
// IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
#ifndef _uikit_lift_h
#define _uikit_lift_h

#include "uikit/widget.h"

namespace uikit {

static inline bool uk_lifted (const Widget *w) { return w->reserved_[0] != 0; }

// Before the widget grows into its open list.
static inline void uk_lift (Widget *w)
{
	Widget *old = w->parent, *t = old;
	if (!old || uk_lifted (w)) return;
	while (t->parent && !t->modal) t = t->parent;		// its window, or the dialog it is in
	if (t == old) return;					// (already a child of it: nothing clips it more)
	int x = w->left - old->scrollX, y = w->top - old->scrollY;
	for (Widget *p = old; p != t; p = p->parent) { x += p->left - p->parent->scrollX; y += p->top - p->parent->scrollY; }
	bool focus = w->hasFocus;
	w->reserved_[0] = (unsigned long) old; w->reserved_[1] = (unsigned long) w->nextSib;
	w->reserved_[2] = ((unsigned long) (unsigned) w->left << 32) | (unsigned) w->top; w->reserved_[3] = (unsigned long) w->anchor;
	if (old->prevHandled == w) old->prevHandled = 0;
	if (w->prevSib) w->prevSib->nextSib = w->nextSib; else old->firstChild = w->nextSib;
	if (w->nextSib) w->nextSib->prevSib = w->prevSib; else old->lastChild = w->prevSib;
	w->parent = 0; w->prevSib = w->nextSib = 0;
	old->invalidate (true);
	w->left = x + t->scrollX; w->top = y + t->scrollY; w->anchor = ANCHOR_LEFT | ANCHOR_TOP;
	t->addChild (w);
	if (focus) w->setFocus ();
}

// After it shrank back to its box.
static inline void uk_unlift (Widget *w)
{
	Widget *old = (Widget *) w->reserved_[0], *t = w->parent;
	if (!old) return;
	bool focus = w->hasFocus;
	if (t) t->removeChild (w);
	w->left = (int) (w->reserved_[2] >> 32); w->top = (int) (unsigned) w->reserved_[2]; w->anchor = (int) w->reserved_[3];
	Widget *b = 0;						// the child it stood before, if it is still there
	for (Widget *c = old->firstChild; c; c = c->nextSib) if (c == (Widget *) w->reserved_[1]) b = c;
	w->parent = old; w->nextSib = b; w->prevSib = b ? b->prevSib : old->lastChild;
	if (w->prevSib) w->prevSib->nextSib = w; else old->firstChild = w;
	if (b) b->prevSib = w; else old->lastChild = w;
	w->reserved_[0] = w->reserved_[1] = w->reserved_[2] = w->reserved_[3] = 0;
	w->invalidate (true); old->invalidate (true);
	if (t) t->invalidate (true);
	if (focus) w->setFocus ();
}

} // namespace uikit

#endif
