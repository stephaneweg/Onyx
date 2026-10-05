//
// elegant.h -- the protocol between AppKit and Elegant, the graphics server (SD:/bin/elegant).
//
// PRIVATE to AppKit and Elegant: no program includes it. A program's window calls are AppKit's
// kapi_* functions (appkit.h), as they always were; when Elegant owns the display, their bodies
// (appkit_calls.inc) send the requests below instead of calling the kernel's window manager. What
// Elegant brings that is new is shown to the programs by UIKit.
//
// A request is the kernel's KAPI_WS_CALL (kern/kapi_abi.h): an operation, four numbers, up to
// KAPI_WS_DATA_MAX bytes each way; the caller waits for the answer, and the kernel tells Elegant
// who asks (the pid). The answer's status is what the call returns, or EL_E_*. The numbers are
// append-only (an AppKit and an Elegant of different builds may meet).
//
// What is NOT a request:
//  - the pixels: the window's canvas and its frame's two copies are memory shared with Elegant, at
//    the addresses they always had (EL_VA_*);
//  - kapi_present: the kernel's KAPI_WS_KICK (Elegant is told the program's pixels changed);
//  - the events: Elegant puts them in the program's queue, in the kernel -- the pump
//    (kapi_pump_events, kapi_pump_wait, kapi_should_exit, kapi_post) is unchanged.
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
#ifndef _appkit_elegant_h
#define _appkit_elegant_h

// Where a program's window is in its memory (kern/layout.h USER_WINDOW_*, kern/kapi_abi.h KAPI_WS_VA_*).
#define EL_VA_CANVAS		KAPI_WS_VA_CANVAS
#define EL_VA_FRAME		KAPI_WS_VA_FRAME
#define EL_VA_FRAME_OFF		KAPI_WS_VA_FRAME_OFF

#define EL_E_NOWINDOW		(-1000)		// the caller has no window
#define EL_E_BADOP		(-1001)		// an operation this Elegant does not know
#define EL_E_NOMEM		(-1002)

// ---- the operations (a[0..3]: the numbers; in / out: the bytes) ----------------------------------

// The caller's window made: a = x, y (x < 0 or y < 0: placed by Elegant), client w, h; in: struct
// el_create -> 1 (the canvas is at EL_VA_CANVAS, its row w pixels), 0: refused. A caller that has a
// window already: 1, nothing changed.
#define EL_OP_CREATE		1
struct el_create
{
	unsigned flags;				// WIN_FLAG_*
	char	 title[64];
};

// The caller's window's frame (kapi_get_chrome): out: struct el_frame -> 1, 0 no window. Elegant
// takes the frame as about to be drawn again.
#define EL_OP_FRAME		2
struct el_frame
{
	int	 content_w, content_h;		// the client area
	int	 frame_w, frame_h;		// each copy's size (0 0: a borderless window)
	int	 inset_l, inset_r, inset_t, inset_b;
	char	 title[48];
};

// The caller's handlers: a[0] = EL_HANDLER_*, a[1] = the function's address (0: none) -> 1.
#define EL_OP_HANDLER		3
#define EL_HANDLER_KEY		0
#define EL_HANDLER_CLICK	1
#define EL_HANDLER_POINTER	2

// The caller's window moved: a = x, y (the frame's top left) -> 1.
#define EL_OP_MOVE		4

// The caller's window's client size, within the canvas it has (kapi_resize_window): a = w, h -> 1.
#define EL_OP_RESIZE		5
// ... its canvas (and frame) growing when needed (kapi_resize_window2): a = w, h; out: int, the
// canvas's row in pixels -> 1, 0 refused. The pixels are at the same addresses, new memory.
#define EL_OP_GROW		6
#define EL_OP_ALPHA		7	// a[0] = 0..255 (kapi_set_window_alpha) -> 1
#define EL_OP_RESIZABLE		8	// a = on, min w, min h (kapi_win_resizable) -> 0, -1
#define EL_OP_GEOMETRY		9	// out: struct kapi_win_geom (kapi_win_geometry) -> 0
#define EL_OP_CURSOR		10	// a[0] = KAPI_CURSOR_* (kapi_set_cursor) -> the shape before, -1

// The menus (the menu bar is a program): the caller's (a[0] = its handler, in: the spec's text) -> 1;
// the active window's (out: struct el_menu) -> its serial; a command to it (a[0] = id) -> 1, 0.
#define EL_OP_MENU_SET		11
#define EL_OP_MENU_GET		12
#define EL_OP_MENU_COMMAND	13
struct el_menu
{
	unsigned serial;
	char	 title[64];
	char	 spec[2048];			// (WIN_MENU_MAX; sent up to its end)
};

// The windows, for the dock, the remote desktop... (kapi_win_*): a[0] = how many at most, out:
// struct kapi_win_info[] -> how many; a[0] = a window's id (0: the caller's, where the call has it).
#define EL_OP_WIN_LIST		14
#define EL_OP_WIN_RAISE		15
#define EL_OP_WIN_CLOSE		16
#define EL_OP_WIN_MINIMISE	17
#define EL_OP_WIN_DESK		18	// a = id, desk (kapi_win_desk)
#define EL_OP_DESK		19	// a = set, count (kapi_desk)
#define EL_OP_WHEEL		20	// a[0] = lines a notch, -1: asked -> the lines a notch

// The open programs by their names (kapi_list_windows: out, one a line -> how many; kapi_raise_app,
// kapi_toggle_app's closing half: in, the name -> 1 done, 0 no such program).
#define EL_OP_APP_LIST		21
#define EL_OP_APP_RAISE		22
#define EL_OP_APP_CLOSE		23

// Drag and drop: begun by the caller (in: struct el_drag + the payload) -> 1, 0; the payload of the
// drag going on or just dropped (out: struct el_drag + the payload) -> its length.
#define EL_OP_DRAG_BEGIN	24
#define EL_OP_DRAG_DATA		25
struct el_drag
{
	int	 type;
	char	 label[48];
};
#define EL_DRAG_MAX		(KAPI_WS_DATA_MAX - sizeof (struct el_drag))

#define EL_OP_WALLPAPER_GEN	26	// a = base colour, points, seed (kapi_wallpaper_generate) -> 1
#define EL_OP_POINTER		27	// out: int x, y from the caller's client area (kapi_cursor_pos) -> 1

// The wallpaper: the caller's copy of it, screen-sized, at EL_VA_WALLPAPER (kapi_wallpaper_buffer;
// out: int w, h) -> 1, 0; what the caller drew there made the wallpaper (kapi_wallpaper_commit) -> 1.
#define EL_OP_WALLPAPER_BUF	28
#define EL_OP_WALLPAPER_COMMIT	29
#define EL_VA_WALLPAPER		KAPI_WS_VA_WALLPAPER

// A window's pixels read (kapi_win_read): a = the window's id (KAPI_WIN_DESKTOP: the desktop),
// the part (0 client, 1 / 2 the frame's active / inactive copy), x << 32 | y, w << 32 | h (the low
// halves unsigned); out: struct el_read -> 0 (the pixels are at EL_VA_XFER, its row `w` pixels),
// -1 no such window or part.
#define EL_OP_WIN_READ		30
#define EL_VA_XFER		KAPI_WS_VA_XFER
struct el_read
{
	int	 w, h;				// what was read, clipped to the part (0: nothing)
};

// The pointer's shape shown now (kapi_cursor_shown: the remote desktop) -> KAPI_CURSOR_*.
#define EL_OP_CURSOR_SHOWN	31

#endif
