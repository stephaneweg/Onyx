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

#endif
