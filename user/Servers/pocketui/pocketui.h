//
// pocketui.h -- PocketUI's own parts, as its files see each other (main.cpp: the arguments, the alias; wm.cpp: the
// policy; band.cpp: the status band). What it shares with Elegant: ../common/ (policy.h).
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
#ifndef POCKETUI_H
#define POCKETUI_H

#define PK_BAND_H	24		// the status band's height (pocket; console has none)

extern int g_nPkMode;			// PK_MODE_POCKET / PK_MODE_CONSOLE (uikit/port/pocket.h)
extern unsigned g_nPkSelf;		// PocketUI's own pid (its own windows: the band)

// The status band (band.cpp): made for a screen w wide (0: none -- console), drawn again when what it shows
// changed (the front app's title, the minute) -> its window's number, -1 none.
int pk_band_make (int w);
void pk_band_update (const char *title);
int pk_band_id (void);

// The policy's state, for a test on the PC (tools/tests/server_sim): a window's kind (PK_KIND_*), the program in front.
#define PK_KIND_NONE	0
#define PK_KIND_OWN	1		// PocketUI's own (the band)
#define PK_KIND_POPUP	2		// a borderless or topmost window of a program: where it asked
#define PK_KIND_CARD	3		// a framed window that fits the work area: centred
#define PK_KIND_FILL	4		// frameless, at the work area's top left (its size: the work area's when resizable)
#define PK_KIND_BAR	5		// the desktop's global menu bar (pocket): the top band, PocketUI's own band then hidden
#define PK_KIND_HOME	6		// the shell's backmost window: its home (the launcher), at the work area
#define PK_KIND_SHELL	7		// the shell's topmost or borderless windows: its overlays, its toasts (where asked)
#define PK_KIND_CENTRE	8		// a program's main window of a fixed size (not resizable, smaller than the work area):
					// frameless, centred in the work area over a matte of its own background colour
// The apps shown as cards rather than full screen: SD:/etc/pocketui.ini's [cards] (one app's name a line), or their
// app.txt's "pocket = card" -> 1 (wm.cpp; read again at each program's first window).
#define PK_INI		"SD:/etc/pocketui.ini"
int pk_card_app (const char *name);
int pk_matte (void);			// 1: the matte is shown (behind a centred main window)
int pk_kind (int id);
unsigned pk_front_pid (void);
int pk_bar_id (void);			// the global menu bar's window, -1: none
unsigned pk_shell_pid (void);		// the shell (uk_shell_register), 0: none
int pk_home (void);			// 1: home -- no app in front, the shell's home shown

#endif
