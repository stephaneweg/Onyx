//
// uikit/port/elegant.h -- the protocol between UIKit's desktop port and Elegant, the graphics server
// (SD:/bin/elegant).
//
// PRIVATE to the desktop UIKit's port (uikit/port/port_desktop.cpp) and Elegant (user/Servers/elegant): no
// program includes it (wstest, the protocol's test, apart). A program's window calls are UIKit's uk_win_*
// functions (uikit/win.h); the desktop port sends the requests below. Until 2026-10-08 this file was AppKit's
// (user/Kits/appkit/elegant.h) and the window calls AppKit's kapi_* ones (docs/POCKETUI-TECH-STUDY.md phase
// P2: each graphics server's UIKit is the only code that speaks its server's protocol).
//
// A request is the kernel's KAPI_WS_CALL (kern/kapi_abi.h): an operation, four numbers, up to
// KAPI_WS_DATA_MAX bytes each way; the caller waits for the answer, and the kernel tells Elegant
// who asks (the pid). The answer's status is what the call returns, or EL_E_*. The numbers are
// append-only (a UIKit and an Elegant of different builds may meet).
//
// What is NOT a request:
//  - the pixels: the window's canvas and its frame's two copies are memory shared with Elegant, at
//    the addresses they always had (EL_VA_*);
//  - uk_win_present: the kernel's KAPI_WS_KICK (Elegant is told the program's pixels changed);
//  - the events: Elegant puts them in the program's queue, in the kernel -- the pump
//    (kapi_pump_events, kapi_pump_wait, kapi_should_exit, kapi_post: AppKit's) is unchanged.
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
#ifndef _uikit_port_elegant_h
#define _uikit_port_elegant_h

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

// The caller's window's frame (uk_win_chrome): out: struct el_frame -> 1, 0 no window. Elegant
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

// The caller's window's client size, within the canvas it has (uk_win_resize): a = w, h -> 1.
#define EL_OP_RESIZE		5
// ... its canvas (and frame) growing when needed (uk_win_resize2): a = w, h; out: int, the
// canvas's row in pixels -> 1, 0 refused. The pixels are at the same addresses, new memory.
#define EL_OP_GROW		6
#define EL_OP_ALPHA		7	// a[0] = 0..255 (uk_win_alpha) -> 1
#define EL_OP_RESIZABLE		8	// a = on, min w, min h (uk_win_resizable) -> 0, -1
#define EL_OP_GEOMETRY		9	// out: struct kapi_win_geom (uk_win_geometry) -> 0
#define EL_OP_CURSOR		10	// a[0] = KAPI_CURSOR_* (uk_win_cursor) -> the shape before, -1

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

// The windows, for the dock, the remote desktop... (uk_win_list...): a[0] = how many at most, out:
// struct kapi_win_info[] -> how many; a[0] = a window's id (0: the caller's, where the call has it).
#define EL_OP_WIN_LIST		14
#define EL_OP_WIN_RAISE		15
#define EL_OP_WIN_CLOSE		16
#define EL_OP_WIN_MINIMISE	17
#define EL_OP_WIN_DESK		18	// a = id, desk (uk_win_to_desk)
#define EL_OP_DESK		19	// a = set, count (uk_win_desk)
#define EL_OP_WHEEL		20	// a[0] = lines a notch, -1: asked -> the lines a notch

// The open programs by their names (uk_win_apps: out, one a line -> how many; uk_win_app_raise,
// uk_win_app_toggle's closing half: in, the name -> 1 done, 0 no such program).
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

#define EL_OP_WALLPAPER_GEN	26	// a = base colour, points, seed (uk_win_wallpaper_generate) -> 1
#define EL_OP_POINTER		27	// out: int x, y from the caller's client area (uk_win_cursor_pos) -> 1

// The wallpaper: the caller's copy of it, screen-sized, at EL_VA_WALLPAPER (uk_win_wallpaper_buffer;
// out: int w, h) -> 1, 0; what the caller drew there made the wallpaper (uk_win_wallpaper_commit) -> 1.
#define EL_OP_WALLPAPER_BUF	28
#define EL_OP_WALLPAPER_COMMIT	29
#define EL_VA_WALLPAPER		KAPI_WS_VA_WALLPAPER

// A window's pixels read (uk_win_read): a = the window's id (KAPI_WIN_DESKTOP: the desktop),
// the part (0 client, 1 / 2 the frame's active / inactive copy), x << 32 | y, w << 32 | h (the low
// halves unsigned); out: struct el_read -> 0 (the pixels are at EL_VA_XFER, its row `w` pixels),
// -1 no such window or part.
#define EL_OP_WIN_READ		30
#define EL_VA_XFER		KAPI_WS_VA_XFER
struct el_read
{
	int	 w, h;				// what was read, clipped to the part (0: nothing)
};

// The pointer's shape shown now (uk_win_cursor_shown: the remote desktop) -> KAPI_CURSOR_*.
#define EL_OP_CURSOR_SHOWN	31

// The window as it was last presented, for who reads it from outside (uk_win_read: the remote
// desktop). A program paints in its canvas, the memory Elegant reads: a reader that comes between a
// repaint's first stroke and its present would see the picture half made (a flicker on the remote
// screen). So the canvas' shared memory holds, after the canvas, a page of control (struct el_shot)
// and a second copy of the pixels: while Elegant wants it (a reader is there), the port's uk_win_present
// copies the canvas there before it tells Elegant -- the reader is given that copy, always whole.
// EL_OP_SHOT: out: struct el_shot_info -> 1, 0: this window has none (the read is the live canvas').
// Asked again after the window is made or grown (the memory may be another one).
#define EL_OP_SHOT		32
struct el_shot_info
{
	unsigned ctl_off;			// from EL_VA_CANVAS: the control page,
	unsigned copy_off;			// the copy,
	unsigned cap;				// its bytes at most
};
#define EL_SHOT_MAGIC		0x544F4853u	// "SHOT"
struct el_shot					// (the control page's first bytes)
{
	unsigned magic;				// EL_SHOT_MAGIC (Elegant's)
	unsigned want;				// Elegant's: 1 while someone reads this window
	unsigned bytes;				// Elegant's: the canvas' bytes to copy (its rows shown)
	unsigned seq;				// UIKit's: raised before a copy (odd: being made) and after it
};

// ---- a program's other windows (v94, docs/MULTI-WINDOW-STUDY.md) --------------------------------
// A program may have several windows: its first one (number 0, the one it always had) and its
// windows 1 .. EL_WINDOWS_MORE. A request's window is in its operation's high bits:
// op = EL_OP_* | window << EL_OP_WINDOW_SHIFT (0: the first; uk_win_select). EL_OP_CREATE
// with a window number makes that one: its canvas at EL_VA_WIN (w, 0), its frame's copies at
// EL_VA_WIN (w, 1) and (w, 2). Its events come with sender = its number; its close box (or "Quit" while
// it is the active one) does not end the program: GUI_EVENT_WINCTL KAPI_FRAME_CLOSE to its pointer
// handler -- the program destroys it (EL_OP_DESTROY) or keeps it.
#define EL_OP_WINDOW_SHIFT	16
#define EL_OP_MASK		0xFFFF
#define EL_WINDOWS_MORE		KAPI_WS_WINDOWS_MORE
#define EL_VA_WIN(w, p)		KAPI_WS_VA_WIN (w, p)
#define EL_OP_DESTROY		33	// the caller's window (its number in the op) closed -> 1, 0 none

// ---- the status area's icons (v95; uk_win_tray_*) -----------------------------------------
// One icon a program, kept by Elegant (dropped when the program ends). EL_OP_TRAY_SET: a[0] = the handler;
// in: struct el_tray -> 1, 0 no room. EL_OP_TRAY_CLEAR -> 1. EL_OP_TRAY_LIST: a[0] = max; out: struct
// kapi_tray_info each -> how many. EL_OP_TRAY_ICON: a[0] = pid; out: the pixels -> 1, 0 none.
// EL_OP_TRAY_ACTIVATE: a = pid, KAPI_TRAY_* (OPEN: the program's first window raised, back from minimised)
// -> 1, 0 none.
#define EL_OP_TRAY_SET		34
#define EL_OP_TRAY_CLEAR	35
#define EL_OP_TRAY_LIST		36
#define EL_OP_TRAY_ICON		37
#define EL_OP_TRAY_ACTIVATE	38

// A window moved (uk_win_place, v96): a = its id (0: the caller's), x, y -- its client area's top left on the
// screen -> 0, -1 no such window (or the desktop's, a topmost one).
#define EL_OP_WIN_MOVE		39
struct el_tray
{
	char	 tip[56];
	unsigned px[KAPI_TRAY_PX * KAPI_TRAY_PX];
};

#endif
