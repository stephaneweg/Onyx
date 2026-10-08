//
// policy.h -- what a graphics server adds to the code it shares with the others (user/Servers/common/):
// its POLICY. The common code -- the kernel's plumbing (serve.cpp), the routing of the input and of the
// programs' events (route.cpp), the window store and the compositor (core.cpp, wm/), the decoding of the
// programs' requests (ops.cpp) -- is the same for Elegant (the desktop: overlapping windows, workspaces) and
// PocketUI (pocket and console: one app in front, filled windows, cards). Each server defines g_pWsPolicy;
// every hook may be 0 (the common behaviour: Elegant's, which has none). docs/POCKETUI-TECH-STUDY.md
// section 7.1, docs/02-KERNEL-INTERNALS.md section 10.
//
// One thread calls these (the server's loop): no lock.
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
#ifndef SERVERS_COMMON_POLICY_H
#define SERVERS_COMMON_POLICY_H

struct ws_policy
{
	const char *name;		// the server's name: its log lines' prefix ("elegant", "pocketui")

	// A program's window about to be made (a request EL_OP_CREATE): its place (x or y < 0: placed by the
	// server) and its flags may be changed (its client size: never smaller -- the program draws in a canvas of
	// the size it asked) -> 1 make it (refused if bigger than the screen, as Elegant does), 2 make it even if
	// it is bigger than the screen (what does not fit is not shown), 0 refused (the program's create fails).
	int  (*create) (unsigned pid, int win, int *x, int *y, int *w, int *h, unsigned *flags);
	// Every request, before the common decoding (op without its window bits, win: which of the caller's
	// windows) -> 1 answered here (*status and out, *out_len bytes), 0 the common decoding's.
	int  (*op) (unsigned pid, int op, int win, const long *a, const unsigned char *in, unsigned in_len,
		    unsigned char *out, unsigned *out_len, long *status);
	// The keyboard's cooked string (the modifiers held: 1 Ctrl, 2 Shift, 4 Alt), before the window
	// manager routes it -> 1 taken here (the system's keys).
	int  (*key) (const char *keys, unsigned mods);
	// Once a turn of the loop, after the requests, before the programs' events are sent.
	void (*tick) (unsigned self);
	// The screen's size changed (the window manager has the new one).
	void (*screen) (int w, int h);
	// The server holds the graphics server's role (KAPI_WS_REGISTER), the display not taken yet: what every program
	// of the session must find from its start is set now (PocketUI: its UIKit under the name SD:/lib/uikit.so).
	void (*registered) (int restart);
	// The window manager made, for a screen of w x h; restart: started again by the kernel after a server
	// that ended (the programs' windows are back already).
	void (*start) (int w, int h, int restart);

	// Elegant's demonstration (--display): its windows made -> how many; those closed since, removed.
	int  (*demo_scene) (int w, int h);
	int  (*demo_closed) (unsigned self);

	// (2026-10-08, PocketUI's P5) The modifiers held changed: 1 Ctrl, 2 Shift, 4 Alt, KAPI_WS_MOD_SUPER (8: the
	// window manager never sees that one) -- the shell's keys: a Super pressed and released alone, Alt let go
	// over the switcher. (The key hook gets them too, with each key.)
	void (*mods) (unsigned mods);

	// (2026-10-08, PocketUI's P6) The pointer, before the window manager (screen coordinates; buttons bit 0 left, 1 right,
	// 2 middle; the wheel in notches) -> 1 taken here (PocketUI's viewport: a window bigger than the work area scrolled).
	int  (*pointer) (int x, int y, unsigned buttons, int wheel);
};
#define WS_POLICY_HAS_MODS	1		// (the hook above: a test built against both revisions knows it)
#define WS_POLICY_HAS_POINTER	1

extern const struct ws_policy *g_pWsPolicy;	// (the server's: its main.cpp or its policy's file)

// ---- what the common code offers the servers (route.cpp, serve.cpp) ----------------------------------

// The keys to the policy, then to the window manager (the window that has the keys).
void ws_route_key (const char *keys, unsigned mods);
// A turn of the routing: the policy's tick, the events the window manager queued sent to the programs (a
// program whose first window was closed asked to end), the windows' places kept in the kernel, the
// program that has the keys told to the kernel.
void ws_route_turn (unsigned self);
extern unsigned g_nWsPosted;			// the events sent (the 5 s statistics)

// The graphics server: the display and the raw input taken from the kernel, the programs' windows served
// until the kernel asks it to end (KAPI_WS_IN_QUIT), or the display is taken back. demo: Elegant's
// demonstration (ended by Esc, its last window closed, or 60 s). restart: started again by the kernel.
int el_serve (int demo, int restart);

#endif
