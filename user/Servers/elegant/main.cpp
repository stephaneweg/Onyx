//
// main.cpp -- Elegant, Onyx's graphics server (SD:/bin/elegant): the windows, their composition and
// the routing of the input, in a user process instead of the kernel (docs/GUI-USERSPACE-STUDY.md).
//
// Stages 1 and 2a (this file): the kernel's window manager still runs the desktop. Elegant shows
// that the same window manager works in a user process, on three windows of its own (drag them by
// their title, raise them, close them; Esc ends it):
//
//   elegant --demo      on the full screen, as any program may take it: the pointer is what the
//                       kernel's window manager sends a full-screen window;
//   elegant --display   as the graphics server (kapi v89, kern/wsrv.h): it takes the DISPLAY from
//                       the kernel's compositor and reads the RAW INPUT; the desktop is back when
//                       it ends (Esc, its last window closed, 60 s) -- or dies.
//
// While it serves, the programs that ask have their windows there (uikit/port/elegant.h; the plan: docs/HANDOFF.md).
// Since 2026-10-08 (PocketUI's phase P3) the server's code is shared with PocketUI (user/Servers/common/):
// this file is Elegant's own part -- its policy (below) and its demonstration.
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
#include "appkit/appkit.h"
#include "onyxpp.hpp"
#include "core.h"
#include "policy.h"
#include "uikit/port/elegant.h"

extern "C" int memcmp (const void *a, const void *b, __SIZE_TYPE__ n)
{
	const unsigned char *p = (const unsigned char *) a, *q = (const unsigned char *) b;
	for (; n > 0; n--, p++, q++) if (*p != *q) return *p < *q ? -1 : 1;
	return 0;
}

extern "C" unsigned el_port_ticks (void)		// (port/circle/timer.h)
{
	return kapi_get_ticks ();
}

static void say (const char *s)
{
	ax_puts (s);
}

// ---- the demonstration -------------------------------------------------------------------------

static int s_bQuit = 0;

// The pointer, as the kernel sends it to a full-screen window: screen coordinates.
static void demo_pointer (unsigned long, int event, gui_value value)
{
	if (event < GUI_EVENT_PTR_MOVE || event > GUI_EVENT_PTR_WHEEL) return;
	int x = (int) ((value >> 16) & 0xFFFF), y = (int) (value & 0xFFFF);
	unsigned buttons = (unsigned) ((value >> 32) & 0xFF);
	int wheel = event == GUI_EVENT_PTR_WHEEL ? (int) (signed char) ((value >> 48) & 0xFF) : 0;
	el_core_pointer (x, y, buttons, wheel);
}

static void demo_key (unsigned long, int, gui_value value)
{
	if (value == 27) s_bQuit = 1;
}

// A window's content and its frame, drawn as a program would (a plain frame: no title text here).
static void demo_paint (int id, unsigned face, unsigned title)
{
	int w = 0, h = 0;
	unsigned *p = el_core_window_canvas (id, &w, &h);
	if (p != 0)
		for (int y = 0; y < h; y++)
			for (int x = 0; x < w; x++)
				p[y * w + x] = ((x / 16 + y / 16) & 1) ? face : face + 0x00101010;
	for (int active = 0; active < 2; active++)
	{
		unsigned *f = el_core_window_frame (id, active, &w, &h);
		if (f == 0) continue;
		unsigned band = active ? title : 0x00909098;
		for (int y = 0; y < h; y++)
			for (int x = 0; x < w; x++)
				f[y * w + x] = y < KAPI_FRAME_TITLE_H ? band : 0x00303038;
		// the close button's place (the window manager hit-tests it there: KAPI_FRAME_BTN_*)
		for (int y = KAPI_FRAME_BTN_Y; y < KAPI_FRAME_BTN_Y + KAPI_FRAME_BTN_H; y++)
			for (int x = w - KAPI_FRAME_BTN_EDGE - KAPI_FRAME_BTN_W; x < w - KAPI_FRAME_BTN_EDGE; x++)
				if (x >= 0 && y < h) f[y * w + x] = 0x00C04040;
	}
	el_core_window_present (id);
}

static const struct { int x, y, w, h; unsigned face, title; const char *name; } s_Demo[3] =
{
	{  80, 100, 360, 240, 0x00406080, 0x003060C0, "one" },
	{ 260, 220, 400, 260, 0x00806040, 0x00C07030, "two" },
	{ 520, 140, 320, 220, 0x00408050, 0x0030A060, "three" },
};

// The window manager and the three windows, for a screen of w x h -> how many windows.
static int demo_scene (int w, int h)
{
	if (!el_core_start (w, h)) return 0;
	el_core_wallpaper (0x00204060, 24, kapi_get_ticks () | 1);
	int nOpen = 0;
	for (int i = 0; i < 3; i++)
	{
		int id = el_core_window_add (s_Demo[i].x, s_Demo[i].y, s_Demo[i].w, s_Demo[i].h, s_Demo[i].name, 0,
					     (unsigned) kapi_getpid (0));
		if (id < 0) continue;
		demo_paint (id, s_Demo[i].face, s_Demo[i].title);
		nOpen++;
	}
	return nOpen;
}

static int demo_closed (void)			// the windows closed since the last call, removed
{
	int n = 0;
	for (int id = 0; id < EL_WINDOWS_MAX; id++)
		if (el_core_window_closing (id)) { el_core_window_remove (id); n++; }
	return n;
}

// The demonstration is a program of the graphics server that serves (another Elegant): it asks it as UIKit's
// desktop port does (uikit/port/port_desktop.cpp -- Elegant links no UIKit): a request, the server waited for
// 5 s at most when there is none.
static long demo_ask (int op, long a0, long a1, long a2, long a3, const void *in, unsigned in_len)
{
	static int s_bLost;
	struct kapi_ws_call c;
	__builtin_memset (&c, 0, sizeof c);
	c.op = op; c.in = in; c.in_len = in_len;
	c.a[0] = a0; c.a[1] = a1; c.a[2] = a2; c.a[3] = a3;
	long r = kapi_ws_ctl (KAPI_WS_CALL, (long) &c, 0, 0);
	if (r != -KAPI_ESRCH || s_bLost) return r;
	int t = 0;
	for (; t < 100 && kapi_ws_ctl (KAPI_WS_ACTIVE, 0, 0, 0) <= 0; t++) kapi_msleep (50);
	if (t == 100) { s_bLost = 1; return r; }
	return kapi_ws_ctl (KAPI_WS_CALL, (long) &c, 0, 0);
}

static unsigned *demo_fullscreen (int *pW, int *pH)	// (uk_win_fullscreen_begin: the server told, then the kernel's)
{
	struct el_create c;
	__builtin_memset (&c, 0, sizeof c);
	c.flags = WIN_FLAG_BORDERLESS;
	const char *t = "fullscreen";
	for (unsigned i = 0; t[i] != 0; i++) c.title[i] = t[i];
	demo_ask (EL_OP_CREATE, 0, 0, 64, 64, &c, sizeof c);
	return kapi_fullscreen_begin (pW, pH);
}

static int demo (void)
{
	int w = 0, h = 0;
	unsigned *fb = demo_fullscreen (&w, &h);
	if (fb == 0 || w <= 0 || h <= 0) { say ("elegant: no full screen\n"); return 1; }
	int nOpen = demo_scene (w, h);
	if (nOpen == 0) { kapi_fullscreen_end (); say ("elegant: no memory\n"); return 1; }

	demo_ask (EL_OP_HANDLER, EL_HANDLER_POINTER, (long) demo_pointer, 0, 0, 0, 0);	// (uk_win_on_pointer)
	demo_ask (EL_OP_HANDLER, EL_HANDLER_KEY, (long) demo_key, 0, 0, 0, 0);		// (uk_win_on_key)
	while (!s_bQuit && !kapi_should_exit () && nOpen > 0)
	{
		kapi_pump_wait (16);
		nOpen -= demo_closed ();
		if (el_core_compose (fb, w, h, 0) != 0) kapi_present_fb ();
	}
	kapi_fullscreen_end ();
	return 0;
}

// ---- Elegant's policy (../common/policy.h) -------------------------------------------------------
// The graphics server's loop, the routing, the window manager and the requests' decoding are common to
// Elegant and PocketUI (user/Servers/common/, el_serve: the display and the raw input taken from the
// kernel, the programs' windows served). Elegant's policy is the common behaviour -- overlapping windows,
// placed and moved freely, workspaces --: it only adds the wallpaper painted again when it is started
// again, and its demonstration (--display: its three windows; ended by Esc, its last window closed, 60 s).

static int demo_closed (unsigned self)
{
	int n = 0;
	for (int id = 0; id < EL_WINDOWS_MAX; id++)
		if (el_core_window_pid (id) == self && el_core_window_closing (id)) { el_core_window_remove (id); n++; }
	return n;
}

static void start (int, int, int restart)
{
	if (restart) kapi_launch ("voronoy");	// (the wallpaper went with the server before: painted again)
}

static const struct ws_policy s_Elegant =
{
	"elegant",
	0, 0, 0, 0, 0,				// create, op, key, tick, screen: the common behaviour
	0,					// registered: nothing before the display
	start,
	demo_scene, demo_closed,
	0,					// mods: the window manager's own (Ctrl, Shift, Alt)
};
const struct ws_policy *g_pWsPolicy = &s_Elegant;

static int arg_is (const char *a, const char *d)
{
	int i = 0;
	while (d[i] != 0 && a[i] == d[i]) i++;
	return d[i] == 0 && (a[i] == 0 || a[i] == ' ');
}

int main (void)
{
	char args[64];
	args[0] = 0;
	kapi_get_args (args, sizeof args);
	const char *a = args;
	while (*a == ' ') a++;
	if (arg_is (a, "--demo")) return demo ();
	if (arg_is (a, "--display")) return el_serve (1, 0);
	if (arg_is (a, "--serve"))
	{
		const char *b = a + 7;
		while (*b == ' ') b++;
		return el_serve (0, arg_is (b, "--restart"));
	}

	say ("Elegant, Onyx's graphics server -- the kernel's window manager runs the desktop.\n"
	     "  elegant --demo      its own window manager on the full screen (Esc ends it)\n"
	     "  elegant --display   the same as the graphics server: the display and the raw input\n"
	     "                      taken from the kernel (Esc, or 60 s, gives them back); the\n"
	     "                      programs started meanwhile have their windows there\n"
	     "  elegant --serve     the graphics server, without the demonstration and with no end\n");
	return 0;
}
