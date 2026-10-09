//
// uikit/internal/adapt_int.h -- the adaptive layer's inside (uikit/adapt.h), shared by UIKit's sources: NOT a header
// of the programs (its functions are in namespace uikit::internal, left out of the table: libgen's --exclude).
//
//   - a widget's extension (Widget::ext): the state a class gained after its layout was frozen (uikit/abi.h rule 2) --
//     a TabStrip's edge and style, a ToolBar's priorities, a DataGrid's roles, any widget's input type -- behind one
//     head (ExtHead) so that Widget's destructor frees it whatever made it;
//   - the size class, asked of the port once and again after a resize; told to the Roots (onSizeClass) and to the
//     adaptive widgets (their onClass) when it changes;
//   - the console's navigation keys (L1 / R1), the focus ring and the focused control told to the server.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. Permission is hereby
// granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions: The above copyright notice and this permission notice shall
// be included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS
// IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
//
#ifndef _uikit_internal_adapt_int_h
#define _uikit_internal_adapt_int_h

#include "uikit/adapt.h"

namespace uikit {
class Root;
namespace internal {

// ---- a widget's extension (plain structures: an extension, and a class UIKit keeps for itself, has no virtual function
// outside an anonymous namespace -- libgen --vtables would copy its vtable into the programs) -----------------------------------------------------------------------------------
enum { EXT_MAGIC = 0x55784B31u };	// "1KxU"
enum { EXT_PLAIN = 1, EXT_TABSTRIP, EXT_TOOLBAR, EXT_DATAGRID, EXT_SIDEPANEL, EXT_FORM, EXT_TREEVIEW, EXT_POPUP, EXT_MODAL };
struct ExtHead
{
	unsigned magic;			// EXT_MAGIC
	unsigned short kind;		// EXT_*
	short	 inType;		// UK_IN_* + 1 (0: not said)
	void	 (*destroy) (ExtHead *);	// frees the whole extension (the class's part with its head)
	void	 (*onClass) (Widget *, ExtHead *);	// the size class changed (0: nothing to do)
	Widget	*owner;
	ExtHead	*nextAdaptive;		// (the adaptive widgets' list: those with onClass)
	ExtHead () : magic (EXT_MAGIC), kind (EXT_PLAIN), inType (0), destroy (0), onClass (0), owner (0), nextAdaptive (0) {}
};
ExtHead *ext_head (Widget *w);		// its extension, 0: none
void ext_set (Widget *w, ExtHead *h, unsigned short kind);	// (ext_of's: an older plain one's input type kept)
void ext_free (Widget *w);		// (Widget's destructor)
// The class's extension of w, made the first time (T derives from ExtHead).
template <class T> T *ext_of (Widget *w, unsigned short kind)
{
	ExtHead *h = ext_head (w);
	if (h != 0 && h->kind == kind) return (T *) h;
	T *t = new T ();			// (no virtual function in an extension: its vtable would be copied into the programs)
	t->destroy = [] (ExtHead *x) { delete (T *) x; };
	ext_set (w, t, kind);
	return t;
}
void adaptive_add (ExtHead *h);		// told when the size class changes (h->onClass)
void adaptive_remove (ExtHead *h);

// ---- the size class -------------------------------------------------------------------------------------------
void class_dirty ();			// asked again at the next uk_size_class (a resize, the screen changed)
void class_check ();			// (Root::step) changed: each Root's onSizeClass, each adaptive widget's onClass
bool ring_on ();			// the focus ring drawn, the arrows move the focus (pocket, console)
// (tests on the PC: the mode, the class and the profile forced; -1 -1 -1: the server's again)
void force_class (int mode, int size_class, int profile);

// ---- the window's helpers -------------------------------------------------------------------------------------
// The console's L1 / R1 owner: a TabStrip with setNav (prio 2), a navigation SidePanel (1); F6 a SidePanel's focus.
void nav_register (Widget *w, int prio, bool (*step) (Widget *, int dir));
void nav_unregister (Widget *w);
bool key_fallback (Root *r, long k);	// a key nobody took (Root::keyEvent) -> true used here
void focus_ring (Canvas &cv, Widget *c, int x, int y);	// the ring around c, at x, y of cv (Widget::draw)
void root_tick (Root *r);
void redraw_soon (Widget *w);		// w's window drawn again at its next turn (a widget that moved while drawn)		// the focused control's rectangle, a field's type -> the server
void each_root (void (*fn) (Root *));
unsigned ptr_release (int *x, int *y);	// the left button's releases counted (root.cpp), where the last one was (window)	// each of the program's windows (root.cpp)
Root *root_of (Widget *w);		// the Root a widget is in, 0
void abs_pos (Widget *w, int *x, int *y);	// its top left in its Root
// A dim: the parent's pixels under (x, y, w, h) darkened into cv (the dialogs' sheet, a drawer's scrim).
// The window's top dialog, when it is a sheet (taller or wider than the window: pocket, console), scrolled by dx, dy
// pixels (its content moved right / down) -> true it scrolled (dialog.cpp).
bool sheet_scroll (Root *r, int dx, int dy);
void dim_copy (Canvas &cv, Widget *from, int x, int y, int w, int h, int amount);

} // namespace internal
} // namespace uikit

#endif
