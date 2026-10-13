//
// wipeout_uikit.cpp -- the few UIKit things wipEout (C) uses: the language, the window's menu bar.
//
// Copyright (c) 2026 the Onyx authors. MIT licence.
//
#include "uikit/uikit.h"
#include "uikit/lang.h"

extern "C" void onyx_ui_init (void) { uk_lang_init (); }
extern "C" const char *onyx_tr (const char *en) { return uk_tr (en); }

extern "C" void onyx_menu (void (*full) (void), void (*quit) (void))
{
	uikit::uk_decorate_window ();
	static uikit::Menu menu;
	menu.menu (TR ("Game"));
	menu.item (TR ("Full Screen"), "F11", 0, full);
	menu.separator ();
	menu.item (TR ("Quit"), "", 0, quit);
	menu.publish ();
}
