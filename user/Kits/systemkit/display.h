//
// display.h -- the screen's resolutions: the sizes Onyx offers, the one kept for the next start (SD:/cmdline.txt's
// width= / height=; the size now is AppKit's kapi_screen_size, changed at once by kapi_screen_set), and the GAMES'
// own sizes in console mode -- an app's app.txt `resolution = 800x600` (the emulators say theirs), overridden by
// SD:/etc/console.ini [screen] `<app> = 1024x768` or `<app> = system`. The Display applet, the console's Display page
// and the console's home (which applies a game's size while it is in front) share it. C and C++.
//
//   for (int i = 0; i < display_modes (); i++) { int w, h; const char *what = display_mode (i, &w, &h); ... }
//   display_save_size (1920, 1080);                       // kept for the next start
//   int w, h; if (display_game_size ("n64emu", &w, &h)) ... // the size that app gets in console mode (0: the system's)
//   display_game_set ("n64emu", 1024, 768);               // DISPLAY_OWN: its own again, DISPLAY_SYSTEM: the system's
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
#ifndef _display_onyx_h
#define _display_onyx_h
#include "appkit/appkit.h"
#include "sk_api.h"

#define DISPLAY_CMDLINE		"SD:/cmdline.txt"
#define DISPLAY_CONSOLE_INI	"SD:/etc/console.ini"
#define DISPLAY_OWN		0	// display_game_set: the app's own size again (its line removed)
#define DISPLAY_SYSTEM		-1	// ... the system's size while it is in front

// The sizes offered: how many; the i-th's width and height -> its words in English ("16:9, Full HD": the caller
// translates them), 0 out of range.
SK_API int display_modes (void);
SK_API const char *display_mode (int i, int *w, int *h);
// The size kept for the next start: written into SD:/cmdline.txt (its other options kept) -> 1, 0 not written; read
// -> 1 (w, h set), 0 none said.
SK_API int display_save_size (int w, int h);
SK_API int display_saved_size (int *w, int *h);
// Another kernel option of SD:/cmdline.txt (its other options kept), for the next start: key=value written (value
// 0: the option removed) -> 1, 0 not written; read -> its length (value copied, NUL-ended), -1 not there.
SK_API int display_save_option (const char *key, const char *value);
SK_API int display_saved_option (const char *key, char *value, int cap);
// "800x600" -> 1 (w, h set: 640 x 480 .. 2560 x 1600, w even), 0 not a size.
SK_API int display_parse_size (const char *s, int *w, int *h);

// An app's own size in console mode (its app.txt's `resolution =`) -> 1, 0 none.
SK_API int display_game_own (const char *app, int *w, int *h);
// What SD:/etc/console.ini [screen] says for it -> 1 a size (w, h), DISPLAY_SYSTEM "system", DISPLAY_OWN nothing.
SK_API int display_game_said (const char *app, int *w, int *h);
// The size it gets while in front in console mode (the line said, else its own) -> 1 (w, h), 0 the system's.
SK_API int display_game_size (const char *app, int *w, int *h);
// Its line in [screen] set (w > 0: w x h), or DISPLAY_SYSTEM, or removed (DISPLAY_OWN) -> 1 written, 0 not.
SK_API int display_game_set (const char *app, int w, int h);
// The app in front asks the console's home for a screen size of its own while it stays in front (a BASIC game: its
// SCREEN mode's), or gives it back (0, 0) -- over what display_game_size says for it; forgotten when the app ends.
// Not while it is full screen (the screen cannot change then): ask, wait for kapi_screen_size to say it, then go
// full screen. -> 1 sent, 0 no console's home runs (the desktop, the pocket mode: nothing to do).
SK_API int display_game_ask (int w, int h);
// Every app's line removed from [screen] (all their own again) -> 1 written, 0 not.
SK_API int display_game_reset (void);

// THE SCREEN AT THE SIZE OF THE APP IN FRONT (2026-10-26: the console's home and the pocket shell share it): while an
// app is in front the screen has its size -- display_game_size's, or what it asked (display_game_ask, the shell's
// SHELL_MSG_SCREEN) --, at home or with another app in front the system's again (the screen's before; a change made
// meanwhile, the Display applet, followed). Only a size set here is undone. The shell keeps one struct:
//
//   static struct display_follow g_follow;  display_follow_init (&g_follow, "pocketshell");
//   display_follow_launch (&g_follow, "cosmozoom");      // the shell opens it: its size now, before it reads the screen's
//   display_follow_ask (&g_follow, front, w, h);         // SHELL_MSG_SCREEN received (front: the app in front, "" none)
//   display_follow_tick (&g_follow, front, names, n);    // every 0.1 s: front ("" at home), the running programs' names
struct display_follow
{
	char	 shell[16];			// its log lines' prefix ("consolehome: the screen at 800 x 600 for gbaemu")
	int	 sys_w, sys_h, ours;		// the system's size; the screen has an app's size set here
	char	 ask_app[32];			// an app's own size asked while it runs ("*": before it was seen in front)
	int	 ask_w, ask_h;
	char	 launch[32];			// an app just opened: its size before it comes in front (10 s at most)
	unsigned launch_t;
	char	 cache_app[32];			// display_game_size's answer for the last app asked (read again after 5 s)
	int	 cache_w, cache_h;
	unsigned cache_t;
	int	 fail_w, fail_h;		// a size refused (kapi_screen_set: a full-screen program), tried again after 2 s
	unsigned fail_t;
	char	 said[96];			// the last line logged (a refusal said once)
};
SK_API void display_follow_init (struct display_follow *f, const char *shell);
// An app the shell is about to open (not running yet): its own size set at once -> 1 it has one, 0 none.
SK_API int display_follow_launch (struct display_follow *f, const char *app);
// The app in front asked a size (0, 0: gives it back) -- one of display_modes (), else forgotten.
SK_API void display_follow_ask (struct display_follow *f, const char *front, int w, int h);
// Every 0.1 s or so: the app in front (0 or "": none, the home) and the names of the programs running (an app that
// asked a size and ended is forgotten).
SK_API void display_follow_tick (struct display_follow *f, const char *front, const char *const *running, int n);

#if defined (SK_BODIES_INLINE) && !defined (SK_IMPL)
#include "display.inc"
#endif

#endif
