//
// doom_uikit.cpp -- the few uikit things Doom (C) uses: the window chrome and the menu bar.
//
#include "uikit/uikit.h"

extern "C" void onyx_decorate (void) { uikit::uk_decorate_window (); }

extern "C" void onyx_menu (void (*full) (void), void (*quit) (void))
{
	static uikit::Menu menu;
	menu.menu ("Game");
	menu.item ("Full Screen", "F11", 0, full);
	menu.separator ();
	menu.item ("Quit", "", 0, quit);
	menu.publish ();
}
