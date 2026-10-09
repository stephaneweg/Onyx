//
// uikit/adapt.h -- the adaptive layer: what an app asks to lay itself out in every mode (docs/POCKETUI-TECH-STUDY.md
// sections 6.5-6.14, phase P6; docs/03 "The adaptive widgets"). One binary runs on the desktop's UIKit (Elegant:
// lib/uikit.so) and on PocketUI's (pocket and console: lib/pocket/uikit.so, loaded under the same name): the app
// declares WHAT its parts are -- a navigation or inspector SidePanel (uikit/sidepanel.h), tools with priorities
// (uikit/toolbar.h), tabs (uikit/tabstrip.h), a dialog of rows (uikit/form.h), columns with roles
// (uikit/datagrid.h), a field's type -- and each UIKit renders it for its mode. On the desktop every call below
// answers what today is (the size class regular, today's metrics): an app that uses them looks the same there.
//
//   int sc = uk_size_class ();               // UK_SC_REGULAR (desktop), _COMPACT, _NARROW (portrait), _CONSOLE
//   const UkMetrics &m = uk_metrics ();       // the rows', buttons', rail's sizes of the profile
//   int gw = uk_scroll_gutter ();            // what a scrolled view leaves at its right for the bar (10; 0 in pocket)
//   uk_set_input_type (urlBox, UK_IN_URL);    // the on-screen keyboard's layout (pocket), "a keyboard is needed" (console)
//   class MyRoot : public Root { void onSizeClass (int sc) override { ...lay out again... } };
//
// The size class (uikit/win.h's UK_SC_*): the server's (uk_win_server), asked once and again when the window or
// the screen changes size; Root::onSizeClass (uikit/root.h) is called on each Root when it changes.
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
#ifndef _uikit_adapt_h
#define _uikit_adapt_h

#include "uikit/widget.h"
#include "uikit/win.h"			// UK_MODE_*, UK_SC_*

namespace uikit {

// ---- the mode and the size class ---------------------------------------------------------------------------
int uk_size_class ();			// UK_SC_REGULAR (the desktop), UK_SC_COMPACT, UK_SC_NARROW, UK_SC_CONSOLE
int uk_mode ();				// UK_MODE_DESKTOP, UK_MODE_POCKET, UK_MODE_CONSOLE
static inline bool uk_compact () { return uk_size_class () != UK_SC_REGULAR; }	// (any of the small ones)

// ---- the metrics: the profile's sizes (SD:/etc/theme.txt "metrics = regular | compact | touch", pocket) -------
enum { UK_PROFILE_REGULAR = 0, UK_PROFILE_COMPACT = 1, UK_PROFILE_TOUCH = 2, UK_PROFILE_CONSOLE = 3 };
struct UkMetrics
{
	int	 profile;			// UK_PROFILE_* (the desktop: regular; console: its own)
	int	 scale;				// percent (100; "scale =" of theme.txt in pocket) -- the sizes below include it
	int	 row;				// a list's, a grid's row (regular: the text's height + 8)
	int	 menuRow;			// a popup menu's row
	int	 button;			// a push button's height
	int	 field;				// a text field's height
	int	 tab;				// a tab strip's height
	int	 toolbar;			// a toolbar's height (and a tool's 28 + 6)
	int	 rail;				// a SidePanel's rail: its width (48 lp)
	int	 scrollbar;			// the scroll bar as drawn (regular 10; pocket's overlay 4)
	int	 gutter;			// what a scrolled view leaves for it (uk_scroll_gutter)
	int	 pad;				// the padding between controls
	int	 touch;				// a touch target's least size (0: no touch)
	int	 reserved[8];
};
const UkMetrics &uk_metrics ();
int uk_scroll_gutter ();		// UK_SBW (10) on the desktop, 0 under the overlay bars (pocket, console)
int uk_lp (int v);			// logical pixels at the metrics' scale (v on the desktop)

// The app draws only through UIKit (its widgets and Canvas' methods): its window may be given device pixels when the
// display is scaled (the native scale of docs/POCKETUI-TECH-STUDY.md section 6.3, phase P10) -- said to the server,
// kept; nothing changes on the desktop.
void uk_logical_units (bool on);
bool uk_logical_units_on ();

// ---- a field's type: the on-screen keyboard's layout (pocket), "a keyboard is needed" (console) ----------------
enum { UK_IN_TEXT = 0, UK_IN_NUMBER, UK_IN_DECIMAL, UK_IN_URL, UK_IN_EMAIL, UK_IN_PASSWORD, UK_IN_TERMINAL, UK_IN_SEARCH };
void uk_set_input_type (Widget *w, int type);	// any widget that takes text (a Textbox, a terminal's view...)
int  uk_input_type (Widget *w);		// UK_IN_* (a Textbox with `password` set: UK_IN_PASSWORD; else UK_IN_TEXT)

// ---- the focus by the arrows (pocket, console: a d-pad) ----------------------------------------------------------
// The focus moved from the focused control of root's tree to the nearest focusable one in that direction (dx, dy:
// -1, 0, 1) -> true moved. UIKit does it itself with the arrows nobody took, in pocket and console.
bool uk_focus_move (Widget *root, int dx, int dy);

// ---- the console's shoulders -------------------------------------------------------------------------------------
// L1 / R1 (on a keyboard: Ctrl+Page Up / Ctrl+Page Down) step through the window's tabs, else through its navigation
// SidePanel's sections; L2 / R2 (Page Up / Page Down) page a list. The pad's buttons are given these keys by the
// console's shell (phase P9).

} // namespace uikit

#endif
