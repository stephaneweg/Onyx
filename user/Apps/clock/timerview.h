//
// timerview.h -- the Clock's Timer tab, Time's up, the timer handed to clockd (04 D17, D18, D20; 03 step 9).
// One translation unit with main.cpp. (Developer C: steps 9-11 -- these are the hooks main.cpp calls.)
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
static void timer_build (Page *p)		// the tab's widgets
{
	FootText *ft = new FootText (14, p->height - FOOT + 8, p->width - 28, 30);
	ft->anchor = ANCHOR_LEFT | ANCHOR_RIGHT | ANCHOR_BOTTOM;
	ft->set (TR ("Space: start / pause \xC2\xB7 R: reset \xC2\xB7 it rings even with the Clock closed"));
	p->addChild (ft);
}
static void timer_shown () { g_page[TAB_TIMER]->setFocus (); }	// the tab shown: its focus
static void timer_tick () {}					// each turn of the loop (g_now read)
static bool timer_key (long k, bool ctrl) { (void) k; (void) ctrl; return false; }	// a key the focused widget left
static void timer_menu (Menu &m) { (void) m; }			// its menu (04 §6)
static void timer_resume_handed () {}				// at the start: a [timer] still to come taken back
static void timer_at_exit () {}					// at the end: a running timer handed to clockd
static void ring_timer () { say ("ring timer"); tab_show (TAB_TIMER); }	// --ring timer: Time's up
