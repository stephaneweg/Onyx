//
// uikit/win.h -- UIKit's window API: a program's windows and what it asks of the graphics server, as plain C
// functions (uk_win_*, C linkage: entries of SD:/lib/uikit.so; a C program includes this header alone and links
// lib/uikit.imp_c.a, a C++ one has it through uikit/uikit.h and links lib/uikit.imp.a).
//
// Until 2026-10-08 these were AppKit's kapi_* window calls (kapi_create_window, kapi_present, kapi_set_menu,
// kapi_win_list...). The graphics server is a program of its own (Elegant on the desktop; PocketUI, the pocket
// and console modes' server, to come -- docs/POCKETUI-TECH-STUDY.md), and each server has its UIKit, loaded under
// the name SD:/lib/uikit.so: UIKit's port (uikit/port/) is the only code that speaks its server's protocol
// (Elegant's: uikit/port/elegant.h). So the window API is UIKit's, and a program's binary runs on every server.
// AppKit keeps the kernel's side: the transport (kapi_ws_ctl), the event pump (kapi_pump_*, kapi_should_exit),
// the screen (kapi_screen_size), the full screen's kernel primitives (kapi_present_fb, kapi_fullscreen_direct).
//
// The renaming, for a program written with the old names: kapi_create_window -> uk_win_create, and every other
// window call kapi_<name> -> uk_win_<name> without a doubled "win_" (kapi_win_list -> uk_win_list), with these:
// kapi_move_window -> uk_win_move, kapi_resize_window(2) -> uk_win_resize(2), kapi_set_window_alpha ->
// uk_win_alpha, kapi_get_chrome -> uk_win_chrome, kapi_set_key / click / pointer_handler -> uk_win_on_key /
// _on_click / _on_pointer, kapi_set_cursor -> uk_win_cursor, kapi_set_menu / get_menu -> uk_win_menu_set /
// _menu_get, kapi_win_move -> uk_win_place, kapi_win_desk -> uk_win_to_desk, kapi_list_windows -> uk_win_apps,
// kapi_raise_app / toggle_app -> uk_win_app_raise / _app_toggle, kapi_get / set_wheel_speed -> uk_win_wheel_get /
// _wheel_set, kapi_fullscreen_begin / _end -> uk_win_fullscreen_begin / _end.
//
// The window flags (WIN_FLAG_*), the events (GUI_EVENT_*, GUI_PTR_*, KEY_*) and the structures (struct kapi_chrome,
// kapi_win_geom, kapi_win_info, kapi_tray_info) keep their names and their definitions (appkit/appkit.h,
// kern/kapi_abi.h): the events still come through the kernel's pump.
//
// A program's windows: its first (0, made by uk_win_create / _ex) and others (uk_win_new); the calls below that
// act on "this window" act on the one uk_win_select chose (0 until then). What each window was asked is kept by
// UIKit and asked again of a graphics server started again (a crash, a restart): the program does nothing.
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
#ifndef _uikit_win_h
#define _uikit_win_h

#include "appkit/appkit.h"		// gui_handler, WIN_FLAG_*, GUI_EVENT_*, the structures (kern/kapi_abi.h)

#ifdef __cplusplus
extern "C" {
#endif

// ---- the program's windows ------------------------------------------------------------------------------
// The program's first window, a client area of w x h pixels titled title, placed by the server -> its canvas
// (0x00RRGGBB pixels, w a row), 0 on failure (no server, bigger than the screen, no memory).
unsigned *uk_win_create (int w, int h, const char *title);
// The program's first window at x, y (its frame's top left; negative: placed by the server) with the flags
// (WIN_FLAG_*) -> its canvas, 0 on failure.
unsigned *uk_win_create_ex (int x, int y, int w, int h, const char *title, unsigned flags);
// Another window of the program (after its first: 1 .. KAPI_WS_WINDOWS_MORE of them), as uk_win_create_ex -> its
// number (> 0) and its canvas in *canvas, -1 (no room, no server). The calls act on it once uk_win_select chose it.
int uk_win_new (int x, int y, int w, int h, const char *title, unsigned flags, unsigned **canvas);
// The window the calls act on (0: the first; -1: only asked) -> the one they acted on until now, -1 no such window.
int uk_win_select (int win);
void uk_win_destroy (int win);		// window win (> 0) closed and forgotten (the first: the program's end)

// ---- this window ----------------------------------------------------------------------------------------
void uk_win_move (int x, int y);	// its frame moved to x, y (screen coordinates)
// Its client area set to w x h, within the canvas it was created with (which stays) -> the canvas, 0 no window.
unsigned *uk_win_resize (int w, int h);
// As uk_win_resize, but the canvas and the frame's copies grow past their first size when needed (new memory at
// the same addresses: their pixels are lost -- redraw them; the frame: uk_win_chrome again) -> the canvas,
// *stride its pixels a row; 0 (no memory: the size kept).
unsigned *uk_win_resize2 (int w, int h, int *stride);
// on != 0: its edges and corners can be dragged, its client area never under min_w x min_h -> 0, -1 (no window, a
// borderless or fixed one). At the release its pointer handler gets GUI_EVENT_WINRESIZE: the program resizes itself.
int uk_win_resizable (int on, int min_w, int min_h);
void uk_win_alpha (int alpha);		// its opacity, 0 .. 255 (fades)
// Its frame's place and size, its client area's size, the work area (the screen less the menu bar and the dock)
// and its KAPI_WIN_* state into *out -> 0, -1 no window.
int uk_win_geometry (struct kapi_win_geom *out);
// Its surfaces for the frame UIKit draws (uikit/skin.cpp): the client canvas, the frame's active and inactive
// copies (0: borderless), their size, the insets, the title -> 1, 0 no window.
int uk_win_chrome (struct kapi_chrome *out);
void uk_win_present (void);		// its pixels changed: the server shows them
// One line of text in the kernel's bitmap font at x, y of its canvas, colour c (0x00RRGGBB), the background kept.
void uk_win_draw_text (int x, int y, const char *s, unsigned c);
void uk_win_on_key (gui_handler fn);		// GUI_EVENT_KEY to fn (value: the character or KEY_*)
void uk_win_on_click (gui_handler fn);		// GUI_EVENT_CANVAS_CLICK / _MOTION to fn (the old click handler)
void uk_win_on_pointer (gui_handler fn);	// the whole pointer stream to fn (GUI_EVENT_PTR_*, drops, WINCTL, WINRESIZE)
// The pointer's shape over its client area (KAPI_CURSOR_*), kept until changed -> the shape it had, -1.
int uk_win_cursor (int shape);
void uk_win_cursor_pos (int *x, int *y);	// the pointer, relative to its client area
int uk_win_cursor_shown (void);		// 1 the pointer is drawn, 0 hidden (keyboard only), -1 not known

// ---- the menu bar ---------------------------------------------------------------------------------------
// This program's menus (the first window's) and the handler that gets GUI_EVENT_MENU (value: the item's id). The
// spec, '\n'-separated: "M<title>" a menu, "I<id>\t<label>\t<shortcut>" an item, "-" a separator (programs
// normally use uikit::Menu) -> 1, 0.
int uk_win_menu_set (const char *spec, gui_handler h);
// For the menu bar: the active window's spec and title -> a serial that changes with them (0: none).
unsigned uk_win_menu_get (char *buf, unsigned cap, char *title, unsigned tcap);
int uk_win_menu_command (int id);	// item id sent to the active window (MENU_QUIT: asked to close) -> 1 delivered

// ---- every window: the shell's, the remote desktop's -----------------------------------------------------
int uk_win_list (struct kapi_win_info *out, int max);	// the windows, bottom to top (at most max) -> how many
int uk_win_raise (unsigned id);		// window id to the front (it gets the keys) -> 0, -1
int uk_win_close (unsigned id);		// window id asked to close (as its close box) -> 0, -1
int uk_win_minimise (unsigned id);	// window id (0: the caller's) minimised until raised -> 0, -1
int uk_win_place (unsigned id, int x, int y);	// window id's frame moved to x, y -> 0, -1
// Window id (0: the caller's) to workspace n (-1: every one; -2: only asked) -> its workspace (-1: all), -3 no window.
int uk_win_to_desk (unsigned id, int n);
// The workspaces: set >= 0 shows that one, count > 0 sets how many (1 .. KAPI_DESK_MAX); -1 / 0 keep them -> the
// current one | the count << 8 | a counter bumped at every change << 16.
int uk_win_desk (int set, int count);
// The pixels of a rectangle of window id's client area (part 0) or of its frame (1 active, 2 inactive) into dst
// (stride pixels a row), clipped to it -> 0, -1. KAPI_WIN_DESKTOP: the desktop, read whole.
int uk_win_read (unsigned id, int part, int x, int y, int w, int h, unsigned *dst, int stride);
// The names of the open apps (a window on the current workspace, not a WIN_FLAG_SYSTEM one), one a line -> how many.
int uk_win_apps (char *b, unsigned s);
int uk_win_app_raise (const char *name);	// app name's window to the front -> 1, 0 not running / no window
// App name toggled -> 0 it was running and is asked to close, 1 it was started, -1 on error.
int uk_win_app_toggle (const char *name);

// ---- the wallpaper --------------------------------------------------------------------------------------
unsigned *uk_win_wallpaper_buffer (int *w, int *h);	// the screen-sized wallpaper to draw into -> its pixels, 0
void uk_win_wallpaper_commit (void);			// ... drawn: shown
int uk_win_wallpaper_generate (unsigned base, int pts, unsigned seed);	// the server's own Voronoi wallpaper

// ---- drag and drop --------------------------------------------------------------------------------------
// A drag from this window while the left button is held: type DND_TEXT / DND_FILES ('\n'-separated paths), len
// bytes (4 KB at most), a label on the pointer -> 1 started. The targets get GUI_EVENT_DRAG_OVER / _DROP, the
// source GUI_EVENT_DRAG_DONE (to the pointer handler).
int uk_win_drag_begin (int type, const void *data, unsigned len, const char *label);
int uk_win_drag_data (int *type, void *buf, unsigned cap);	// the last drop's payload (cap bytes) -> its length

// ---- the status area (the menu bar's tray) ----------------------------------------------------------------
// This program's icon (KAPI_TRAY_PX square, 0xTTRRGGBB) and tip; h gets GUI_EVENT_TRAY (KAPI_TRAY_OPEN, _MENU)
// -> 1, 0. (uikit::uk_tray does it from a picture.)
int uk_win_tray_set (const unsigned *px, const char *tip, gui_handler h);
void uk_win_tray_clear (void);
int uk_win_tray_list (struct kapi_tray_info *out, int max);	// for the menu bar: the icons -> how many
int uk_win_tray_icon (unsigned pid, unsigned *px);		// ... one's pixels -> 1, 0
int uk_win_tray_activate (unsigned pid, int kind);		// ... clicked (KAPI_TRAY_OPEN, _MENU) -> 1, 0

// ---- the wheel ------------------------------------------------------------------------------------------
int uk_win_wheel_get (void);		// lines scrolled a notch, system-wide
void uk_win_wheel_set (int lines);	// ... set (1 .. 16; the theme editor keeps it in theme.txt)

// ---- the full screen ------------------------------------------------------------------------------------
// The display for this program alone (the server told, then the kernel's kapi_fullscreen_begin) -> the screen's
// pixels (*w x *h; kapi_present_fb shows them), 0 refused. uk_win_fullscreen_end gives it back.
unsigned *uk_win_fullscreen_begin (int *w, int *h);
void uk_win_fullscreen_end (void);

// ---- the graphics server --------------------------------------------------------------------------------
#define UK_MODE_DESKTOP		0	// uk_win_server's mode: Elegant's desktop
#define UK_MODE_POCKET		1	// PocketUI's pocket mode
#define UK_MODE_CONSOLE		2	// PocketUI's console mode
#define UK_SC_REGULAR		0	// size classes: the desktop
#define UK_SC_COMPACT		1	// a small landscape screen
#define UK_SC_NARROW		2	// a portrait screen
#define UK_SC_CONSOLE		3	// a television, a pad
struct uk_win_server_info
{
	unsigned size;			// set by the caller: sizeof (struct uk_win_server_info) (it may grow at its end)
	char	 name[16];		// the server's name ("elegant")
	int	 mode;			// UK_MODE_*
	int	 screen_w, screen_h;	// the screen, in the windows' pixels
	int	 work_x, work_y, work_w, work_h;	// the work area: where a filled window goes
	int	 scale;			// the composition's scale, in percent (100: none)
	int	 size_class;		// UK_SC_*
	int	 reserved[8];
};
// The server this program's UIKit speaks to (each server has its UIKit) -> 1, 0 none runs (*out then says what
// the desktop would be).
int uk_win_server (struct uk_win_server_info *out);

// ---- the shell's calls (PocketUI's: the pocket and console shells) -----------------------------------------
// The desktop's UIKit answers -KAPI_ENOSYS to each: Elegant's shell programs (menubar, dock) use the calls above.
// Their meaning is PocketUI's (docs/POCKETUI-TECH-STUDY.md section 4.3; phase P5, docs/03 "The pocket shell"):
// the shell (pocketshell; consolehome later) is the program whose windows are the home behind the apps (its
// BACKMOST window: placed at the work area, the keys' when no app is in front), its overlays (its TOPMOST
// windows: where it asks, even across the screen's top edge) and its toasts.
//
// "I am the shell" -> 1; -KAPI_EBUSY another running program is; -KAPI_ENOSYS not PocketUI. Asked first, before its
// windows; UIKit asks it again of a PocketUI started again (with the events' handler and the keys).
int uk_shell_register (void);
// The windows' news to fn, as events UK_SHELL_EVENT (value: UK_SHELL_EV_*) and UK_SHELL_KEYEV (value: a key,
// UK_SHELL_KEY) -> 1. No polling: the switcher, the launcher's running apps read uk_shell_tasks when told.
int uk_shell_events (gui_handler fn);
#define UK_SHELL_EVENT		64	// the event: value UK_SHELL_EV_*
#define UK_SHELL_EV_TASKS	1	// a program's window opened, closed, retitled, minimised; the front one changed; home
#define UK_SHELL_EV_AREA	2	// the work area changed (the menu bar came or went, the screen's size): uk_win_server
#define UK_SHELL_KEYEV		65	// the event: value UK_SHELL_KEY (mods, code) -- a system key, or every key while grabbed
// A key as the shell names it: the modifiers (1 Ctrl, 2 Shift, 4 Alt, 8 Super) and the code (a character, KEY_*,
// KEY_ENTER, KEY_BACKSPACE, 0x1b Esc, 9 Tab; or one of the two below).
#define UK_SHELL_KEY(mods, code)	((int) (((unsigned) (mods) << 16) | (unsigned) (code)))
#define UK_SHELL_KEY_MODS(k)		((unsigned) (k) >> 16)
#define UK_SHELL_KEY_CODE(k)		((int) ((unsigned) (k) & 0xFFFF))
#define UK_SHELL_MOD_SUPER	8	// a Super key (the Windows key)
#define UK_SHELL_KEY_SUPER	0x1000	// a Super pressed and released with no key between (Home)
#define UK_SHELL_KEY_HELD	0x1001	// (grabbed) the modifiers held changed: UK_SHELL_KEY_MODS says which now
// The system keys the shell takes before the front app (UK_SHELL_KEY each; count 0: none) -> how many. A program
// in full screen keeps every key.
int uk_shell_keys (const int *keys, int count);
// While on: every key and every change of the modifiers to the shell (an overlay is up: the switcher, quick
// settings) -> 1.
int uk_shell_grab (int on);
// Window id's client area as last presented, scaled to w x h (at most 512 x 512) into dst (w a row) -> 1, 0 no
// such window, -1 refused.
int uk_shell_thumb (unsigned id, unsigned *dst, int w, int h);
// front 1: window id's program to the front (home left); front 0 and id 0: HOME -- every app set aside, the shell's
// home has the keys; front 0 and an id: its program behind the others -> 1, 0 no such window.
int uk_shell_front (unsigned id, int front);
int uk_shell_split (unsigned left, unsigned right);	// two programs' windows side by side (0 0: none) -> 1, 0 the work area is too small or upright (PocketUI: P8)
int uk_shell_dim (int alpha);				// a dim behind an overlay -- (not yet: the overlays draw theirs)
// The running programs, the most recently in front first: each its topmost window -> how many (none in front of
// them: home).
struct uk_shell_task
{
	unsigned id;			// its topmost window (uk_win_list's id: uk_shell_thumb, uk_win_close, uk_shell_front)
	unsigned pid;
	unsigned flags;			// UK_TASK_*
	int	 w, h;			// that window's client area
	char	 title[48];		// its title
	char	 name[24];		// the program's name (its app: SD:/apps/<name>.app)
};
#define UK_TASK_FRONT		1	// the program in front
#define UK_TASK_CARD		2	// its window is a card (a small fixed window, framed)
#define UK_TASK_MINIMISED	4	// every window of it minimised
#define UK_TASKS_MAX		32
int uk_shell_tasks (struct uk_shell_task *out, int max);

#ifdef __cplusplus
}
#endif

// Friendly aliases used by the demos (they were appkit.h's).
static inline unsigned *create_window (int w, int h, const char *t) { return uk_win_create (w, h, t); }
static inline void      present (void)             { uk_win_present (); }

#endif
