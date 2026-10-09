//
// session.h -- the interface's mode and its session (docs/POCKETUI-TECH-STUDY.md section 8). Onyx has three modes:
//   desktop   Elegant, the desktop (the menu bar, the dock, the agenda...)            SD:/etc/session/desktop
//   pocket    PocketUI, one app at a time on a small screen                           SD:/etc/session/pocket
//   console   PocketUI's console mode: the games, a television, a pad                 SD:/etc/session/console
// The mode is SD:/etc/system.ini's "shell =" (no line: desktop), which the kernel reads to start the matching
// graphics server. The SESSION is the mode's programs: SD:/etc/autostart keeps the system's part (the services:
// clockd, pkgd, clipd, printd, telnetd...) and one line, `session`, where /bin/session runs the file of the mode
// (the same syntax as the autostart: `run menubar`, `#setup: run dock`, `sleep 1`...). Switching the mode closes
// the open programs (an unsaved document asked), ends the session's programs, writes "shell =", has the kernel
// start the other server (KAPI_WS_SWITCH) and runs the other mode's file: /bin/session does it, asked by the
// Control Panel's Mode applet (modeconf) or typed (`session switch pocket`).
// A card from before the sessions keeps the desktop's lines in its autostart until `pkg commit` (at boot) moves
// them into SD:/etc/session/desktop once (session_migrate). C and C++.
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
#ifndef _session_onyx_h
#define _session_onyx_h
#include "appkit/appkit.h"
#include "sk_api.h"
#include "locale.h"			// (system.ini's lines: locale_ini_get / locale_ini_set)

#define SESSION_DESKTOP		0	// the modes (UIKit's UK_MODE_* are the same numbers)
#define SESSION_POCKET		1
#define SESSION_CONSOLE		2
#define SESSION_DIR		"SD:/etc/session"	// SESSION_DIR "/<mode's name>": the mode's programs
#define SESSION_TOOL		"SD:/bin/session"
#define SESSION_WAITING		"SD:/tmp/session.wait"	// the programs that did not close (session_waiting)

// session_switch's flags
#define SESSION_FORCE		1	// the programs still open after the wait are ended (their documents lost)
#define SESSION_NO_ASK		2	// they are not asked again (they were: their question is up), only waited for

// session_switch's answers (/bin/session switch's exit status)
#define SESSION_SWITCHED	0	// the mode asked for runs, its programs started
#define SESSION_FELL_BACK	1	// its server failed: the desktop runs ("shell =" put back to desktop)
#define SESSION_WAITING_APPS	2	// programs did not close in time (session_waiting): nothing switched
#define SESSION_REFUSED		3	// a full-screen program has the display, or a switch is under way
#define SESSION_BAD		4	// an unknown mode, wrong arguments
#define SESSION_FAILED		5	// system.ini not written, no server took the display, the tool missing

SK_API int session_modes (void);				// how many modes: 3
SK_API const char *session_mode_name (int m);		// "desktop", "pocket", "console" ("" out of range)
SK_API int session_mode_find (const char *name);		// the mode of that name (any case) -> SESSION_*, -1 none
// The mode chosen: system.ini's "shell =" (no line, or an unknown word: SESSION_DESKTOP). Read at each call.
// (The server running may differ: its server failed at boot and the kernel started Elegant -- UIKit's
// uk_win_server says which runs.)
SK_API int session_mode (void);
SK_API int session_set_mode (int m);			// "shell = <name>" written -> 1 (0: not written)
SK_API int session_file (int m, char *out, int cap);	// the mode's session file's path ("SD:/etc/session/pocket") -> its length, 0
// The programs the mode's file starts ("menubar", "dock", "notifyd"...: the names their processes have; a `run
// <app>` line gives the app, another line its /bin tool; Setup's held-back "#setup: " lines count), one a line
// into out -> how many (0: no file).
SK_API int session_programs (int m, char *out, int cap);
// The switch to mode m (/bin/session switch, started with the flags; keep_pid: a program kept open meanwhile --
// a Control Panel applet's host --, 0 none; the caller and its parents always are, then ended once the new
// session runs) -> its process handle (kapi_proc_done, kapi_wait: SESSION_* answer), 0 not started.
SK_API void *session_switch_start (int m, int flags, int keep_pid);
SK_API int session_switch (int m, int flags);		// ... and waited for -> SESSION_*
// After SESSION_WAITING_APPS: the programs that did not close, "<name>\t<window title>" a line -> how many.
SK_API int session_waiting (char *out, int cap);
// The autostart of a card from before the sessions split, once: the desktop's lines (voronoy, Setup and its
// "#setup#" lines, menubar, notifyd, dock, agenda, stickies, wifimenu, imageview -- with the comment lines just
// above each) moved into SD:/etc/session/desktop (made anew from them), a line `session` where the first of them
// was, the old file kept as SD:/etc/autostart.old; every other line where it was. Nothing done when the autostart
// has a `session` line already. -> 1 split, 0 nothing to do, -1 not written (the card as it was).
SK_API int session_migrate (void);

#if defined (SK_BODIES_INLINE) && !defined (SK_IMPL)
#include "session.inc"
#endif

#endif
