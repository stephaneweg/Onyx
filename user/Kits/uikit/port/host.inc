//
// uikit/port/host.inc -- a port's window driver on a PC (the desktop simulator, the hosts of the tests, Koton
// for Windows): there is no graphics server, the stand-in kernel has a window manager of its own and the
// entries of kern/kapi_abi.h's NOT-ON-ONYX part -- each call is relayed there (as appkit_calls.inc's KAPI_HOST
// bodies did). Included INSIDE namespace uikit::port by port_desktop.cpp and port_pocket.cpp (moved verbatim
// from port_desktop.cpp's PC part); the port then defines server () and shell ().
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
#define HT	((const struct TKApiTable *) KAPI_TABLE_VA)

unsigned *create (int w, int h, const char *t) { return HT->create_window (w, h, t); }
unsigned *create_ex (int x, int y, int w, int h, const char *t, unsigned f) { return HT->create_window_ex (x, y, w, h, t, f); }
int win_new (int x, int y, int w, int h, const char *t, unsigned f, unsigned **canvas)
{
	if (HT->win_new) return HT->win_new (x, y, w, h, t, f, canvas);
	if (canvas) *canvas = 0;
	return -1;
}
int select (int win) { if (HT->win_select) return HT->win_select (win); return win <= 0 ? 0 : -1; }
void destroy (int win) { if (HT->win_destroy) HT->win_destroy (win); }

void move (int x, int y) { HT->move_window (x, y); }
unsigned *resize (int w, int h) { return HT->resize_window (w, h); }
unsigned *resize2 (int w, int h, int *stride) { return HT->version >= 64 ? HT->resize_window2 (w, h, stride) : 0; }
int resizable (int on, int min_w, int min_h) { return HT->version >= 82 && HT->win_resizable ? HT->win_resizable (on, min_w, min_h) : -1; }
void alpha (int a) { HT->set_window_alpha (a); }
int geometry (struct kapi_win_geom *out) { return HT->version >= 64 ? HT->win_geometry (out) : -1; }
int chrome (struct kapi_chrome *out) { return HT->get_chrome (out); }
void present () { HT->present (); }
void draw_text (int x, int y, const char *s, unsigned c) { HT->draw_text (x, y, s, c); }
void on_key (gui_handler fn) { HT->set_key_handler (fn); }
void on_click (gui_handler fn) { HT->set_click_handler (fn); }
void on_pointer (gui_handler fn) { HT->set_pointer_handler (fn); }
int cursor (int shape) { return HT->version >= 81 && HT->set_cursor ? HT->set_cursor (shape) : -1; }
void cursor_pos (int *x, int *y) { HT->cursor_pos (x, y); }
int cursor_shown () { return -1; }

int menu_set (const char *spec, gui_handler h) { return HT->set_menu (spec, h); }
unsigned menu_get (char *buf, unsigned cap, char *title, unsigned tcap) { return HT->get_menu (buf, cap, title, tcap); }
int menu_command (int id) { return HT->menu_command (id); }

int list (struct kapi_win_info *out, int max) { return HT->version >= 56 ? HT->win_list (out, max) : 0; }
int raise (unsigned id) { return HT->version >= 56 ? HT->win_raise (id) : -1; }
int close (unsigned id) { return HT->version >= 56 ? HT->win_close (id) : -1; }
int minimise (unsigned id) { return HT->version >= 64 ? HT->win_minimise (id) : -1; }
int place (unsigned id, int x, int y) { if (HT->win_move) return HT->win_move (id, x, y); return -1; }
int to_desk (unsigned id, int n) { return HT->version >= 65 ? HT->win_desk (id, n) : -1; }
int desk (int set, int count) { return HT->version >= 65 ? HT->desk (set, count) : 1 << 8; }
int read (unsigned id, int part, int x, int y, int w, int h, unsigned *dst, int stride)
{
	return HT->version >= 56 ? HT->win_read (id, part, x, y, w, h, dst, stride) : -1;
}
int apps (char *b, unsigned s) { return HT->list_windows (b, s); }
int app_raise (const char *name) { return HT->raise_app (name); }
int app_toggle (const char *name) { return HT->toggle_app (name); }

unsigned *wallpaper_buffer (int *w, int *h) { return HT->wallpaper_buffer (w, h); }
void wallpaper_commit () { HT->wallpaper_commit (); }
int wallpaper_generate (unsigned base, int pts, unsigned seed) { return HT->wallpaper_generate (base, pts, seed); }

int drag_begin (int type, const void *data, unsigned len, const char *label) { return HT->drag_begin (type, data, len, label); }
int drag_data (int *type, void *buf, unsigned cap) { return HT->drag_data (type, buf, cap); }

int tray_set (const unsigned *px, const char *tip, gui_handler h) { if (HT->tray_set) return HT->tray_set (px, tip, h); return 0; }
void tray_clear () { if (HT->tray_clear) HT->tray_clear (); }
int tray_list (struct kapi_tray_info *out, int max) { if (HT->tray_list) return HT->tray_list (out, max); return 0; }
int tray_icon (unsigned pid, unsigned *px) { if (HT->tray_icon) return HT->tray_icon (pid, px); return 0; }
int tray_activate (unsigned pid, int kind) { if (HT->tray_activate) return HT->tray_activate (pid, kind); return 0; }

int wheel_get () { return HT->get_wheel_speed (); }
void wheel_set (int lines) { HT->set_wheel_speed (lines); }

unsigned *fullscreen_begin (int *w, int *h) { return kapi_fullscreen_begin (w, h); }
void fullscreen_end () { kapi_fullscreen_end (); }
