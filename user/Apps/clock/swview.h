//
// swview.h -- the Clock's Stopwatch tab (04 D19; 03 step 10). One translation unit with main.cpp.
// (Developer C: step 10 -- these are the hooks main.cpp calls.)
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
static void sw_build (Page *p) { (void) p; }		// the tab's widgets
static void sw_shown () { g_page[TAB_SW]->setFocus (); }	// the tab shown: its focus
static void sw_tick () {}				// each turn of the loop
static bool sw_key (long k, bool ctrl) { (void) k; (void) ctrl; return false; }
static void sw_menu (Menu &m) { (void) m; }
static void sw_at_exit () {}				// at the end: the stopwatch kept (sw_* in config.ini)
