//
// core.h -- the graphics servers' window manager (user/Servers/common/: Elegant's and PocketUI's), as the rest
// of a server sees it.
//
// The window manager and the compositor themselves are the kernel's sources (kernel/gui/window.cpp,
// gimage.cpp), built for a user process with the stand-ins of port/ -- one code until the kernel's
// copy is retired. Their classes stay behind this file: core.cpp is the only one that includes
// kern/gui/window.h (its constants are also AppKit's, under the same names).
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
#ifndef ELEGANT_CORE_H
#define ELEGANT_CORE_H

#define EL_WINDOWS_MAX	64		// (= wm/kern/gui/window.h WM_MAX_WINDOWS; 16 until v94)

// The window manager made, for a screen of w x h. 1, or 0: no memory.
int el_core_start (int w, int h);
// A generated wallpaper (the kernel's: cells tinted onto `base`).
void el_core_wallpaper (unsigned base, int points, unsigned seed);

// A window: its outer top left, its client size, WIN_FLAG_* -> Elegant's number for it (0 .. EL_WINDOWS_MAX - 1),
// or -1. Its pixels: the client area, and the frame's two copies (active 1 / inactive 0; 0 for a
// borderless window), 0x00RRGGBB, the row `*w` pixels.
int el_core_window_add (int x, int y, int w, int h, const char *title, unsigned flags, unsigned owner_pid);
unsigned *el_core_window_canvas (int id, int *w, int *h);
unsigned *el_core_window_frame (int id, int active, int *w, int *h);
void el_core_window_present (int id);		// its client area changed
int el_core_window_closing (int id);		// its close button was used
void el_core_window_remove (int id);

// ---- a program's window (its pixels are memory shared with the program) -------------------------

// The pixels of the windows made or grown from now on are `pid`'s: shared with that program (0:
// Elegant's own memory -- its demonstration's windows).
// win: which of the program's windows (0: its first; 1 .. EL_WINDOWS_MORE its others, uikit/port/elegant.h).
void el_core_owner (unsigned pid, int win = 0);
// Shared memory for a window's pixels, given by the server's kernel side (serve.cpp): `bytes` for
// `pid`'s window, part 0 its canvas / 1, 2 its frame's active and inactive copies -> its address
// here (64 KB aligned, zeroed), 0: none; freed by that address. el_shared_is: is p such memory?
void *el_shared_alloc (unsigned pid, int part, unsigned long bytes);
void el_shared_free (void *p);
int el_shared_is (const void *p);

// A canvas' copy as last presented (uikit/port/elegant.h, EL_OP_SHOT; core.cpp): where it is from the
// canvas -> 1, 0 it has none; a reader's access to it; the tick that stops the copies nobody reads.
int el_core_shot_info (const void *pCanvas, unsigned *pnCtlOff, unsigned *pnCopyOff, unsigned *pnCap);
const void *el_core_shot_read (const void *pCanvas, unsigned nBytes, unsigned *pnSeq);
int el_core_shot_same (const void *pCanvas, unsigned nSeq);
void el_core_shot_tick (void);

int el_core_window_of (unsigned pid);		// a program's window (any of them), -1: none
int el_core_window_of_win (unsigned pid, int win);	// a program's window number win (0: its first), -1: none
int el_core_window_win (int id);		// ... a window's number in its program (0: its first)
void el_core_window_closing_clear (int id);	// its close was told to its program (a window that is not its first)
unsigned long long el_core_window_pointer_handler (int id);
unsigned el_core_window_pid (int id);		// a window's program (0: no such window)
struct el_core_frame				// (the protocol's struct el_frame, uikit/port/elegant.h)
{
	int	 content_w, content_h;
	int	 frame_w, frame_h;
	int	 inset_l, inset_r, inset_t, inset_b;
	char	 title[48];
};
int el_core_window_frame_info (int id, struct el_core_frame *out);	// (the frame taken as about to be drawn)
void el_core_window_handler (int id, int kind, unsigned long long fn);	// kind: 0 key, 1 click, 2 pointer
void el_core_window_move (int id, int x, int y);
struct el_core_event				// an event for the window's program (the kernel's struct kapi_event)
{
	unsigned long long handler, sender;
	long long value;
	int	 event;
	unsigned mods;
};
int el_core_window_event_peek (int id, struct el_core_event *out);	// the next one, left queued -> 1, 0 none
void el_core_window_event_drop (int id);				// ... taken

// A program's request (uikit/port/elegant.h) -> its status; *out_len bytes of out (KAPI_WS_DATA_MAX of
// room) go with it. (ops.cpp)
long el_op (unsigned pid, int op, const long *a, const unsigned char *in, unsigned in_len,
	    unsigned char *out, unsigned *out_len);
void el_core_program_gone (unsigned pid);	// a program ended: what it had beside its windows freed
unsigned el_core_focus_pid (void);
unsigned el_core_active_id (void);		// the window that has the keyboard: its id (uk_win_list's), 0: none
void el_core_wheel (int lines);			// the wheel's lines a notch (1 .. 16)		// the program that has the keyboard, 0: none

// What the requests need from the kernel (serve.cpp): pid's windows become the server's -> 1;
// a live process's name -> its length (0: none).
int el_sys_attach (unsigned pid);
int el_sys_name (unsigned pid, char *buf, unsigned cap);
// ... KAPI_WS_STATE_BYTES the kernel keeps for an attached program: written (set) / read -> 1;
// the attached programs' pids -> how many.
int el_sys_clients (unsigned *pids, int max);
int el_sys_state (unsigned pid, void *bytes, int set);
// The programs' windows' places kept in the kernel when they change (not `self`'s own windows): a
// server started again puts each window back where it was.
void el_core_save_states (unsigned self);
int el_core_restore_all (void);			// started again: every attached program's window made anew -> how many

void el_core_screen (int w, int h);		// the screen's size changed: the windows kept on it, told
void el_core_redraw (void);			// the whole screen at the next composition
void el_core_fullscreen (unsigned pid, int on);	// a program took / gave back the full screen

// The keyboard's cooked string (characters, VT100 escapes), to the window that has the keys.
void el_core_key (const char *keys);
void el_core_modifiers (unsigned mods);

// The pointer, in screen coordinates: buttons bit 0 left, 1 right, 2 middle; wheel in notches.
void el_core_pointer (int x, int y, unsigned buttons, int wheel);

// What changed since the last call drawn into `screen` (w x h, the row w pixels) -> 0: nothing
// changed, nothing drawn; -1: the whole screen; n > 0: n rectangles, in rects (x, y, w, h each; at
// most EL_RECTS_MAX; rects 0: not wanted).
#define EL_RECTS_MAX	16
int el_core_compose (unsigned *screen, int w, int h, int *rects);

#endif
