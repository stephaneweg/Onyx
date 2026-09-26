//
// doom_wtk.cpp -- the few wtk things Doom (C) uses: the window chrome and the menu bar.
//
#include "wtk/wtk.h"

extern "C" void onyx_decorate (void) { wtk::wk_decorate_window (); }

extern "C" void onyx_menu (void (*full) (void), void (*quit) (void))
{
	static wtk::Menu menu;
	menu.menu ("Game");
	menu.item ("Full Screen", "F11", 0, full);
	menu.separator ();
	menu.item ("Quit", "", 0, quit);
	menu.publish ();
}
