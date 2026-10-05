//
// wsrv.h -- what the kernel gives the graphics server (Elegant, a user process: SD:/bin/elegant),
// kapi v89: MECHANISMS only -- the windows, their order, the focus, the routing of the input and
// the composition are the server's.
//
//  - the ROLE: one process at a time is the display server (kapi_ws_ctl KAPI_WS_REGISTER); the
//    other operations are its own. It ends, or stays silent too long: the kernel takes the
//    display and the input back by itself.
//  - the DISPLAY: while the server owns it (KAPI_WS_DISPLAY), the kernel's compositor draws
//    nothing; the server sends the rectangles it composed (KAPI_WS_PRESENT: copied into the
//    off-screen buffer, sent by the frame buffer's DMA).
//  - the RAW INPUT: while the server owns the display, the mouse, the keys, the modifiers and the
//    held keys -- the USB ones and the injected ones (vncd, rdpd: kapi_inject_*) -- go to a ring
//    the server reads (KAPI_WS_INPUT) instead of the kernel's window manager.
//  - ONE WAIT (KAPI_WS_WAIT): until something is there for the server.
//
// Core 0. The input's feeders may run in an interrupt (the USB callbacks): the ring has a lock;
// the rest is task context on a kernel that is not preempted.
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
#ifndef _kern_wsrv_h
#define _kern_wsrv_h

#include <circle/types.h>

// Does the server own the display now? (the compositor: draw nothing; kapi_screen_grab: the
// off-screen buffer is what is shown; a full screen, a resolution change: not now)
boolean WsDisplayOwned (void);

// The compositor, at each of its turns while the server owns the display: a server that has
// called nothing for WS_SILENT_MS loses it (a hang must not leave the screen frozen).
void WsWatch (void);

// The input's feeders (kernel.cpp's USB callbacks -- an interrupt is allowed --, kapi_inject_*):
// TRUE: the server has it (queued for it), the kernel's window manager must not see it.
boolean WsInputPointer (int x, int y, unsigned nButtons, int nWheel);
boolean WsInputKey (const char *pString);		// the keyboard's cooked string
boolean WsInputMods (unsigned nMods);			// MOD_*
boolean WsInputHeldUsb (const unsigned char RawKeys[6]);	// the USB report's usage codes
boolean WsInputHeld (int nKey, boolean bDown);		// an injected key down / up (logical code)

// A process is gone (its teardown: interrupts masked, nothing may wait).
void WsOnProcessGone (unsigned nPid);

// (kernel.cpp) A rectangle of the off-screen buffer sent to the display by a task that is not the
// compositor (w = 0: the whole screen): waits, yielding, for the display's DMA to be free, then
// for its own to end.
void DisplayPresentRect (unsigned x, unsigned y, unsigned w, unsigned h);

#endif
