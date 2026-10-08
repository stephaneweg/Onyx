//
// uikit/port/pocket.h -- the protocol between UIKit's pocket port and PocketUI, the graphics server of the
// pocket and console modes (SD:/bin/pocketui; docs/POCKETUI-TECH-STUDY.md sections 4.3 and 7).
//
// PRIVATE to the pocket UIKit's port (uikit/port/port_pocket.cpp, built into SD:/lib/pocket/uikit.so) and
// PocketUI (user/Servers/pocketui): no program includes it. A program's window calls are UIKit's uk_win_*
// functions (uikit/win.h); under PocketUI the program maps the pocket UIKit (the server loads it under the
// name SD:/lib/uikit.so: kapi_lib_open_as), whose port sends the requests below.
//
// The transport is the kernel's, as Elegant's (kern/kapi_abi.h: KAPI_WS_CALL a request -- an operation, four
// numbers, up to KAPI_WS_DATA_MAX bytes each way --, KAPI_WS_KICK a present, the events in the program's queue,
// the pixels memory shared with the server at KAPI_WS_VA_*).
//
// THE OPERATIONS. PocketUI's protocol starts from Elegant's (the study's recommendation): every operation whose
// meaning is the same keeps Elegant's number and structures -- uikit/port/elegant.h, included below, EL_OP_CREATE
// (1) .. EL_OP_WIN_MOVE (39), the window number in the high bits (EL_OP_WINDOW_SHIFT) -- so the server decodes
// them with the code it shares with Elegant (user/Servers/common/ops.cpp) and this port reuses the desktop
// port's client (uikit/port/client.inc: the window state, the replay after a server's restart). What PocketUI
// gives them is its POLICY (one app in front, no workspaces):
//
//   EL_OP_CREATE      a program's MAIN window (its first): FILLED -- frameless, at the work area's top left -- or, of a
//                     fixed size smaller than the work area, CENTRED (frameless) over a matte of its colour; a CARD
//                     (framed, centred) for its other windows (dialogs) and the apps of SD:/etc/pocketui.ini [cards]
//                     (or app.txt "pocket = card"). A borderless or topmost window (a popup, a toast) where asked, kept
//                     on the screen. The shell's (PK_OP_SHELL) backmost window: its home, at the work area; its topmost
//                     ones: where asked. Another backmost window, or a topmost one standing on the screen's top or
//                     bottom edge (the desktop's dock; a second menu bar), refused.
//   EL_OP_RESIZABLE   on: the window FILLS the work area -- its frame dropped (a card), GUI_EVENT_WINRESIZE
//                     to the work area's size (UIKit's Root applies it as a frame dragged).
//   EL_OP_FRAME       insets 0 0 for a filled window.
//   EL_OP_MOVE, EL_OP_WIN_MOVE   ignored for a filled window (it stays at the work area's top left);
//                     honoured for a card and a popup.
//   EL_OP_GEOMETRY    the work area is the screen less the status band (pocket; console: the whole screen).
//   EL_OP_DESK        one workspace: the count is 1, asking for others changes nothing.
//   EL_OP_WIN_DESK    -1 (every window on the one desk), nothing moved.
//   EL_OP_WIN_MINIMISE  the window set behind (hidden until raised: Alt+Tab, uk_win_raise).
//   the others        as Elegant's.
//
// Its own operations are in a range of their own (PK_OP_*, from 0x100: Elegant answers EL_E_BADOP to them).
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
#ifndef _uikit_port_pocket_h
#define _uikit_port_pocket_h

#include "uikit/port/elegant.h"		// the operations PocketUI shares with Elegant (their numbers, their structures)
#include "uikit/win.h"			// (struct uk_shell_task, UK_SHELL_*: the shell's operations below)

#define PK_PROTO_NAME		"pocketui"	// struct pk_hello's name
#define PK_PROTO_VERSION	2		// PK_OP_HELLO's answer (this protocol's revision; 2: the shell's operations, P5)

#define PK_MODE_POCKET		1		// (= uikit/win.h UK_MODE_POCKET)
#define PK_MODE_CONSOLE		2		// (= UK_MODE_CONSOLE)

// The port's first request: the protocol it speaks -> PK_PROTO_VERSION, out: struct pk_server (the server's
// mode and screen). Another server (Elegant: EL_E_BADOP) does not understand this UIKit: its window calls fail
// (the program logs "this program's UIKit is PocketUI's" -- a program of another session, docs/POCKETUI-TECH-STUDY.md
// section 3.4). in: struct pk_hello.
#define PK_OP_HELLO		0x100
struct pk_hello
{
	char	 proto[16];			// PK_PROTO_NAME
	unsigned version;			// PK_PROTO_VERSION
};

// The server, its mode and its screen (uk_win_server) -> 1, out: struct pk_server.
#define PK_OP_SERVER		0x101
struct pk_server
{
	char	 name[16];			// "pocketui"
	int	 mode;				// PK_MODE_*
	int	 screen_w, screen_h;
	int	 work_x, work_y, work_w, work_h;	// where a filled window goes (the screen less the status band)
	int	 scale;				// the composition's scale, in percent (100)
	int	 size_class;			// uikit/win.h UK_SC_*
	int	 band_h;			// the status band's height (0: none -- console)
};

// THE SHELL'S OPERATIONS (uikit/win.h uk_shell_*, phase P5; docs/POCKETUI-TECH-STUDY.md section 4.3). The shell is ONE
// program (pocketshell in pocket, consolehome in console): the first to ask PK_OP_SHELL, or any once that one has
// ended. Its windows: BACKMOST = its home (placed at the work area's top left, borderless, never above an app; the
// keys' when no app is in front: HOME), TOPMOST or borderless = its overlays and toasts (where it asks, even across
// the screen's top edge or off the screen); they are never "apps" (not fronted, not set aside, not listed).
#define PK_OP_SHELL		0x110		// "I am the shell" -> 1; -KAPI_EBUSY: another running program is
#define PK_OP_EVENTS		0x111		// a[0] = its handler: UK_SHELL_EVENT / UK_SHELL_KEYEV pushed to it -> 1
#define PK_OP_KEYS		0x112		// a[0] = n, in: int keys[n] (UK_SHELL_KEY): taken before the front app -> n
#define PK_OP_THUMB		0x113		// a = id, w, h (<= 512): its client area scaled into the caller's transfer
						// buffer (KAPI_WS_VA_XFER, w a row) -> 1, 0 no such window, -1 no memory
#define PK_OP_FRONT		0x114		// a = id, front: 1 its program in front; 0 with id 0: home; 0: behind -> 1, 0
#define PK_OP_SPLIT		0x115		// a = left id, right id -- (not yet: -KAPI_ENOSYS; split view, P8)
#define PK_OP_DIM		0x116		// a[0] = alpha -- (not yet: -KAPI_ENOSYS; the overlays draw their own dim)
#define PK_OP_TASKS		0x117		// a[0] = max: out: struct uk_shell_task each, the most recent first -> how many
#define PK_OP_GRAB		0x118		// a[0] = on: every key and the modifiers' changes to the shell -> 1
// The shell's operations from another program than the shell: -KAPI_EPERM (PK_OP_TASKS and PK_OP_THUMB: anyone --
// a task manager, a remote desktop may show them).

#endif
