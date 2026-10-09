//
// uikit/port/port.h -- UIKit's port: what differs between the graphics servers (INTERNAL to UIKit: no program
// includes it; its functions are not entries of the table -- libgen's --exclude '^_ZN5uikit4port').
//
// Each graphics server has its UIKit, built from the same sources (docs/POCKETUI-TECH-STUDY.md section 5): the
// same headers and objects, one port each. The port is the only code that speaks its server's protocol: the
// window driver (the requests, the per-program window state, the replay when the server is started again). The
// window API's entries (uikit/win.h, uk_win_*: win.cpp) are common and thin: each one calls the function of the
// same name here. The ports:
//   port/port_desktop.cpp  Elegant, the desktop (its protocol: port/elegant.h) -- lib/uikit.so
//   port/port_pocket.cpp   PocketUI, pocket and console (its protocol: port/pocket.h) -- lib/pocket/uikit.so
// Both share the client of a server that speaks Elegant's operations (port/client.inc) and, on a PC, the relay to
// the stand-in kernel (port/host.inc). uikit/port.cpp compiles the one chosen (UK_PORT_POCKET: the pocket port;
// else the desktop's).
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
#ifndef _uikit_port_port_h
#define _uikit_port_port_h

#include "uikit/win.h"

namespace uikit {
namespace port {

// (what each one does: its uk_win_* in uikit/win.h)
unsigned *create (int w, int h, const char *title);
unsigned *create_ex (int x, int y, int w, int h, const char *title, unsigned flags);
int win_new (int x, int y, int w, int h, const char *title, unsigned flags, unsigned **canvas);
int select (int win);
void destroy (int win);

void move (int x, int y);
unsigned *resize (int w, int h);
unsigned *resize2 (int w, int h, int *stride);
int resizable (int on, int min_w, int min_h);
void alpha (int a);
int geometry (struct kapi_win_geom *out);
int chrome (struct kapi_chrome *out);
void present ();
void draw_text (int x, int y, const char *s, unsigned c);
void on_key (gui_handler fn);
void on_click (gui_handler fn);
void on_pointer (gui_handler fn);
int cursor (int shape);
void cursor_pos (int *x, int *y);
int cursor_shown ();

int menu_set (const char *spec, gui_handler h);
unsigned menu_get (char *buf, unsigned cap, char *title, unsigned tcap);
int menu_command (int id);

int list (struct kapi_win_info *out, int max);
int raise (unsigned id);
int close (unsigned id);
int minimise (unsigned id);
int place (unsigned id, int x, int y);
int to_desk (unsigned id, int n);
int desk (int set, int count);
int read (unsigned id, int part, int x, int y, int w, int h, unsigned *dst, int stride);
int apps (char *b, unsigned s);
int app_raise (const char *name);
int app_toggle (const char *name);

unsigned *wallpaper_buffer (int *w, int *h);
void wallpaper_commit ();
int wallpaper_generate (unsigned base, int pts, unsigned seed);

int drag_begin (int type, const void *data, unsigned len, const char *label);
int drag_data (int *type, void *buf, unsigned cap);

int tray_set (const unsigned *px, const char *tip, gui_handler h);
void tray_clear ();
int tray_list (struct kapi_tray_info *out, int max);
int tray_icon (unsigned pid, unsigned *px);
int tray_activate (unsigned pid, int kind);

int wheel_get ();
void wheel_set (int lines);

unsigned *fullscreen_begin (int *w, int *h);
void fullscreen_end ();

int server (struct uk_win_server_info *out);
// The shell's calls (uk_shell_*): op one of SHELL_* -> the server's answer, -KAPI_ENOSYS where it has none.
enum { SHELL_REGISTER, SHELL_EVENTS, SHELL_KEYS, SHELL_THUMB, SHELL_FRONT, SHELL_SPLIT, SHELL_DIM, SHELL_TASKS, SHELL_GRAB };
long shell (int op, long a0, long a1, long a2, long a3, const void *in, unsigned in_len, void *out, unsigned cap);

// The adaptive layer (uikit/adapt.h; docs/POCKETUI-TECH-STUDY.md section 6.14): the server's mode (UK_MODE_*), size
// class (UK_SC_*), the metrics' profile (UK_PROFILE_*) and scale (percent) -> 1 said, 0 nothing said (the desktop: the
// caller keeps regular, today's sizes).
int adapt_info (int *mode, int *size_class, int *profile, int *scale);
// The focused control's rectangle in the current window's client area (the server's viewport keeps it shown);
// the focused field's type (UK_IN_*, -1: no field -- the on-screen keyboard's layout); the app draws only through
// UIKit (uk_logical_units). The desktop: nothing.
void focus_rect (int x, int y, int w, int h);
void text_hint (int type);
void logical_units (int on);

} // namespace port
} // namespace uikit

#endif
