//
// uikit/win.cpp -- UIKit's window API (uikit/win.h): its entries, common to every UIKit -- each one calls
// the port's function of the same name (uikit/port/port.h), which speaks to this UIKit's graphics server.
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
#include "uikit/win.h"
#include "uikit/port/port.h"

using namespace uikit;

extern "C" {

unsigned *uk_win_create (int w, int h, const char *title) { return port::create (w, h, title); }
unsigned *uk_win_create_ex (int x, int y, int w, int h, const char *title, unsigned flags) { return port::create_ex (x, y, w, h, title, flags); }
int uk_win_new (int x, int y, int w, int h, const char *title, unsigned flags, unsigned **canvas) { return port::win_new (x, y, w, h, title, flags, canvas); }
int uk_win_select (int win) { return port::select (win); }
void uk_win_destroy (int win) { port::destroy (win); }
void uk_win_move (int x, int y) { port::move (x, y); }
unsigned *uk_win_resize (int w, int h) { return port::resize (w, h); }
unsigned *uk_win_resize2 (int w, int h, int *stride) { return port::resize2 (w, h, stride); }
int uk_win_resizable (int on, int min_w, int min_h) { return port::resizable (on, min_w, min_h); }
void uk_win_alpha (int alpha) { port::alpha (alpha); }
int uk_win_geometry (struct kapi_win_geom *out) { return port::geometry (out); }
int uk_win_chrome (struct kapi_chrome *out) { return port::chrome (out); }
void uk_win_present (void) { port::present (); }
void uk_win_draw_text (int x, int y, const char *s, unsigned c) { port::draw_text (x, y, s, c); }
void uk_win_on_key (gui_handler fn) { port::on_key (fn); }
void uk_win_on_click (gui_handler fn) { port::on_click (fn); }
void uk_win_on_pointer (gui_handler fn) { port::on_pointer (fn); }
int uk_win_cursor (int shape) { return port::cursor (shape); }
void uk_win_cursor_pos (int *x, int *y) { port::cursor_pos (x, y); }
int uk_win_cursor_shown (void) { return port::cursor_shown (); }
int uk_win_menu_set (const char *spec, gui_handler h) { return port::menu_set (spec, h); }
unsigned uk_win_menu_get (char *buf, unsigned cap, char *title, unsigned tcap) { return port::menu_get (buf, cap, title, tcap); }
int uk_win_menu_command (int id) { return port::menu_command (id); }
int uk_win_list (struct kapi_win_info *out, int max) { return port::list (out, max); }
int uk_win_raise (unsigned id) { return port::raise (id); }
int uk_win_close (unsigned id) { return port::close (id); }
int uk_win_minimise (unsigned id) { return port::minimise (id); }
int uk_win_place (unsigned id, int x, int y) { return port::place (id, x, y); }
int uk_win_to_desk (unsigned id, int n) { return port::to_desk (id, n); }
int uk_win_desk (int set, int count) { return port::desk (set, count); }
int uk_win_read (unsigned id, int part, int x, int y, int w, int h, unsigned *dst, int stride) { return port::read (id, part, x, y, w, h, dst, stride); }
int uk_win_apps (char *b, unsigned s) { return port::apps (b, s); }
int uk_win_app_raise (const char *name) { return port::app_raise (name); }
int uk_win_app_toggle (const char *name) { return port::app_toggle (name); }
unsigned *uk_win_wallpaper_buffer (int *w, int *h) { return port::wallpaper_buffer (w, h); }
void uk_win_wallpaper_commit (void) { port::wallpaper_commit (); }
int uk_win_wallpaper_generate (unsigned base, int pts, unsigned seed) { return port::wallpaper_generate (base, pts, seed); }
int uk_win_drag_begin (int type, const void *data, unsigned len, const char *label) { return port::drag_begin (type, data, len, label); }
int uk_win_drag_data (int *type, void *buf, unsigned cap) { return port::drag_data (type, buf, cap); }
int uk_win_tray_set (const unsigned *px, const char *tip, gui_handler h) { return port::tray_set (px, tip, h); }
void uk_win_tray_clear (void) { port::tray_clear (); }
int uk_win_tray_list (struct kapi_tray_info *out, int max) { return port::tray_list (out, max); }
int uk_win_tray_icon (unsigned pid, unsigned *px) { return port::tray_icon (pid, px); }
int uk_win_tray_activate (unsigned pid, int kind) { return port::tray_activate (pid, kind); }
int uk_win_wheel_get (void) { return port::wheel_get (); }
void uk_win_wheel_set (int lines) { port::wheel_set (lines); }
unsigned *uk_win_fullscreen_begin (int *w, int *h) { return port::fullscreen_begin (w, h); }
void uk_win_fullscreen_end (void) { port::fullscreen_end (); }
int uk_win_server (struct uk_win_server_info *out) { return port::server (out); }

// ---- the shell's calls: PocketUI's (the desktop's port answers -KAPI_ENOSYS) ----
int uk_shell_register (void) { return (int) port::shell (port::SHELL_REGISTER, 0, 0, 0, 0, 0, 0, 0, 0); }
int uk_shell_events (gui_handler fn) { return (int) port::shell (port::SHELL_EVENTS, (long) fn, 0, 0, 0, 0, 0, 0, 0); }
int uk_shell_keys (const int *keys, int count)
{
	if (keys == 0 || count < 0) count = 0;
	return (int) port::shell (port::SHELL_KEYS, count, 0, 0, 0, keys, (unsigned) count * (unsigned) sizeof (int), 0, 0);
}
int uk_shell_thumb (unsigned id, unsigned *dst, int w, int h)
{
	if (dst == 0 || w <= 0 || h <= 0) return -1;
	return (int) port::shell (port::SHELL_THUMB, (long) id, w, h, 0, 0, 0, dst, (unsigned) w * (unsigned) h * 4u);
}
int uk_shell_front (unsigned id, int front) { return (int) port::shell (port::SHELL_FRONT, (long) id, front, 0, 0, 0, 0, 0, 0); }
int uk_shell_split (unsigned left, unsigned right) { return (int) port::shell (port::SHELL_SPLIT, (long) left, (long) right, 0, 0, 0, 0, 0, 0); }
int uk_shell_dim (int alpha) { return (int) port::shell (port::SHELL_DIM, alpha, 0, 0, 0, 0, 0, 0, 0); }

} // extern "C"
