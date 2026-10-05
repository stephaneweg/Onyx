//
// main.cpp -- Elegant, Onyx's graphics server (SD:/bin/elegant): the windows, their composition and
// the routing of the input, in a user process instead of the kernel (docs/GUI-USERSPACE-STUDY.md).
//
// Stage 1 (this file): the kernel's window manager still runs the desktop. Elegant only proves that
// the same window manager works in a user process:
//
//   elegant --demo     takes the full screen (as any program may) and shows three windows of its
//                      own, composed by ITS window manager from the pointer the kernel sends it:
//                      drag them by their title, raise them, close them. Esc ends it.
//
// The next stages give it the display, the raw input and the programs' windows (the plan: docs/HANDOFF.md).
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

static int demo (void)
{
	int w = 0, h = 0;
	unsigned *fb = kapi_fullscreen_begin (&w, &h);
	if (fb == 0 || w <= 0 || h <= 0) { say ("elegant: no full screen\n"); return 1; }
	if (!el_core_start (w, h)) { kapi_fullscreen_end (); say ("elegant: no memory\n"); return 1; }
	el_core_wallpaper (0x00204060, 24, kapi_get_ticks () | 1);

	static const struct { int x, y, w, h; unsigned face, title; const char *name; } W[3] =
	{
		{  80, 100, 360, 240, 0x00406080, 0x003060C0, "one" },
		{ 260, 220, 400, 260, 0x00806040, 0x00C07030, "two" },
		{ 520, 140, 320, 220, 0x00408050, 0x0030A060, "three" },
	};
	int nOpen = 0;
	for (int i = 0; i < 3; i++)
	{
		int id = el_core_window_add (W[i].x, W[i].y, W[i].w, W[i].h, W[i].name, 0, (unsigned) kapi_getpid (0));
		if (id < 0) continue;
		demo_paint (id, W[i].face, W[i].title);
		nOpen++;
	}

	kapi_set_pointer_handler (demo_pointer);
	kapi_set_key_handler (demo_key);
	while (!s_bQuit && !kapi_should_exit () && nOpen > 0)
	{
		kapi_pump_wait (16);
		for (int id = 0; id < EL_WINDOWS_MAX; id++)
			if (el_core_window_closing (id)) { el_core_window_remove (id); nOpen--; }
		if (el_core_compose (fb, w, h)) kapi_present_fb ();
	}
	kapi_fullscreen_end ();
	return 0;
}

int main (void)
{
	char args[64];
	args[0] = 0;
	kapi_get_args (args, sizeof args);
	const char *a = args;
	while (*a == ' ') a++;
	const char *d = "--demo";
	int i = 0;
	while (d[i] != 0 && a[i] == d[i]) i++;
	if (d[i] == 0 && (a[i] == 0 || a[i] == ' ')) return demo ();

	say ("Elegant, Onyx's graphics server -- stage 1: the kernel's window manager runs the desktop.\n"
	     "  elegant --demo    its own window manager on the full screen (Esc ends it)\n");
	return 0;
}
