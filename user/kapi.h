//
// kapi.h -- kernel API for EL1 apps. Apps call the kernel through the ABI table the
// kernel publishes at a fixed virtual address (kern/kapi_abi.h), NOT by linking
// against kernel symbol addresses. So an app binary keeps working against any kernel
// that exposes the same ABI -- it does not need rebuilding when the kernel changes.
//
// These thin inline wrappers just dispatch through that table. `gui_handler` and the
// table layout come from <kern/kapi_abi.h> (shared with the kernel).
//
#ifndef _kapi_h
#define _kapi_h

#include <kern/kapi_abi.h>

// The kernel-published ABI table, mapped read-only at KAPI_TABLE_VA in every app.
#define KT	((const struct TKApiTable *) KAPI_TABLE_VA)

// Window creation flags (must match kern/gui/window.h).
#define WIN_FLAG_BORDERLESS	(1u << 0)	// no title bar / border / close box

// Event kinds (must match kern/gui/window.h).
#define GUI_EVENT_CLICK		1
#define GUI_EVENT_CHECK_CHANGED	2
#define GUI_EVENT_TEXT_CHANGED	3
#define GUI_EVENT_VALUE_CHANGED	4
#define GUI_EVENT_KEY		5	// key pressed; value = char or KEY_* code
#define GUI_EVENT_CANVAS_CLICK	6	// client-area click; value = (clientX<<16)|clientY

// Logical key codes (GUI_EVENT_KEY value). Printable keys are their ASCII value.
#define KEY_BACKSPACE		8
#define KEY_TAB			9
#define KEY_ENTER		13
#define KEY_UP			0x100
#define KEY_DOWN		0x101
#define KEY_LEFT		0x102
#define KEY_RIGHT		0x103
#define KEY_HOME		0x104
#define KEY_END			0x105
#define KEY_PGUP		0x106
#define KEY_PGDN		0x107
#define KEY_DEL			0x108

// --- windowing ---------------------------------------------------------------
static inline unsigned *kapi_create_window (int w, int h, const char *t) { return KT->create_window (w, h, t); }
static inline unsigned *kapi_create_window_ex (int x, int y, int w, int h, const char *t, unsigned f) { return KT->create_window_ex (x, y, w, h, t, f); }
static inline unsigned *kapi_resize_window (int w, int h) { return KT->resize_window (w, h); }
static inline int  kapi_launch (const char *n) { return KT->launch (n); }
static inline int  kapi_toggle_app (const char *n) { return KT->toggle_app (n); }
static inline int  kapi_raise_app (const char *n) { return KT->raise_app (n); }
static inline int  kapi_list_windows (char *b, unsigned s) { return KT->list_windows (b, s); }
static inline int  kapi_set_wallpaper (const char *p) { return KT->set_wallpaper (p); }
static inline int  kapi_wallpaper_generate (unsigned base, int pts, unsigned seed) { return KT->wallpaper_generate (base, pts, seed); }
static inline void kapi_present (void) { KT->present (); }
static inline unsigned kapi_get_ticks (void) { return KT->get_ticks (); }
static inline void kapi_msleep (unsigned ms) { KT->msleep (ms); }
static inline void kapi_yield (void) { KT->yield (); }
static inline void kapi_exit (int s) { KT->exit (s); }

// --- widgets -----------------------------------------------------------------
static inline unsigned long kapi_add_button (int x, int y, int w, int h, const char *l, gui_handler fn) { return KT->add_button (x, y, w, h, l, fn); }
static inline unsigned long kapi_add_label (int x, int y, int w, int h, const char *t) { return KT->add_label (x, y, w, h, t); }
static inline unsigned long kapi_add_checkbox (int x, int y, int w, int h, const char *l, gui_handler fn) { return KT->add_checkbox (x, y, w, h, l, fn); }
static inline unsigned long kapi_add_textbox (int x, int y, int w, int h, gui_handler fn) { return KT->add_textbox (x, y, w, h, fn); }
static inline unsigned long kapi_add_progress (int x, int y, int w, int h) { return KT->add_progress (x, y, w, h); }
static inline unsigned long kapi_add_slider (int x, int y, int w, int h, gui_handler fn) { return KT->add_slider (x, y, w, h, fn); }
static inline unsigned long kapi_add_textarea (int x, int y, int w, int h, gui_handler fn) { return KT->add_textarea (x, y, w, h, fn); }
static inline unsigned long kapi_add_scrollbar_v (int x, int y, int w, int h, gui_handler fn) { return KT->add_scrollbar_v (x, y, w, h, fn); }
static inline unsigned long kapi_add_scrollbar_h (int x, int y, int w, int h, gui_handler fn) { return KT->add_scrollbar_h (x, y, w, h, fn); }
static inline unsigned long kapi_add_icon (int x, int y, int w, int h, const char *bmp, const char *l, gui_handler fn) { return KT->add_icon (x, y, w, h, bmp, l, fn); }

static inline int  kapi_widget_get_text (unsigned long wd, char *b, unsigned m) { return KT->widget_get_text (wd, b, m); }
static inline void kapi_widget_set_text (unsigned long wd, const char *t) { KT->widget_set_text (wd, t); }
static inline int  kapi_widget_get_checked (unsigned long wd) { return KT->widget_get_checked (wd); }
static inline int  kapi_widget_get_value (unsigned long wd) { return KT->widget_get_value (wd); }
static inline void kapi_widget_set_value (unsigned long wd, int v) { KT->widget_set_value (wd, v); }
static inline void kapi_widget_set_rect (unsigned long wd, int x, int y, int w, int h) { KT->widget_set_rect (wd, x, y, w, h); }
static inline void kapi_widget_set_icon (unsigned long wd, const char *bmp) { KT->widget_set_icon (wd, bmp); }

// --- events ------------------------------------------------------------------
static inline void kapi_pump_events (void) { KT->pump_events (); }
static inline void kapi_wait_for_exit (void) { KT->wait_for_exit (); }
static inline int  kapi_should_exit (void) { return KT->should_exit (); }

// --- app-drawn text + keyboard -----------------------------------------------
static inline void kapi_draw_text (int x, int y, const char *s, unsigned c) { KT->draw_text (x, y, s, c); }
static inline int  kapi_font_width (void) { return KT->font_width (); }
static inline int  kapi_font_height (void) { return KT->font_height (); }
static inline void kapi_set_key_handler (gui_handler fn) { KT->set_key_handler (fn); }
static inline void kapi_set_click_handler (gui_handler fn) { KT->set_click_handler (fn); }

// --- enumeration + clock -----------------------------------------------------
static inline int  kapi_list_apps (char *b, unsigned s) { return KT->list_apps (b, s); }
static inline int  kapi_get_datetime (int *y, int *mo, int *d, int *h, int *mi, int *se) { return KT->get_datetime (y, mo, d, h, mi, se); }
static inline int  kapi_app_dir (char *b, unsigned s) { return KT->app_dir (b, s); }

// --- console + files ---------------------------------------------------------
static inline int  kapi_write (int fd, const void *b, unsigned n) { return KT->write (fd, b, n); }
static inline void *kapi_open (const char *p) { return KT->open (p); }
static inline int  kapi_read (void *h, void *b, unsigned n) { return KT->read (h, b, n); }
static inline unsigned kapi_fsize (void *h) { return KT->fsize (h); }
static inline void kapi_close (void *h) { KT->close (h); }
static inline int  kapi_save_file (const char *p, const void *b, unsigned n) { return KT->save_file (p, b, n); }

// --- directory listing -------------------------------------------------------
static inline void *kapi_opendir (const char *p) { return KT->opendir (p); }
static inline int  kapi_readdir (void *d, struct kapi_dirent *e) { return KT->readdir (d, e); }
static inline void kapi_closedir (void *d) { KT->closedir (d); }

// --- stdio / streams / processes ---------------------------------------------
static inline void *kapi_pipe (void) { return KT->pipe (); }
static inline void *kapi_file_in (const char *p) { return KT->file_in (p); }
static inline void *kapi_file_out (const char *p, int append) { return KT->file_out (p, append); }
static inline int  kapi_stream_read (void *h, void *b, unsigned n) { return KT->stream_read (h, b, n); }
static inline int  kapi_stream_read_nb (void *h, void *b, unsigned n) { return KT->stream_read_nb (h, b, n); }
static inline int  kapi_stream_write (void *h, const void *b, unsigned n) { return KT->stream_write (h, b, n); }
static inline void kapi_stream_close (void *h) { KT->stream_close (h); }
static inline void kapi_stream_eof (void *h) { KT->stream_eof (h); }
static inline int  kapi_proc_done (void *proc) { return KT->proc_done (proc); }
static inline int  kapi_stdin_read (void *b, unsigned n) { return KT->stdin_read (b, n); }
static inline int  kapi_stdout_write (const void *b, unsigned n) { return KT->stdout_write (b, n); }
static inline void *kapi_spawn (const char *path, const char *args, void *in, void *out) { return KT->spawn (path, args, in, out); }
static inline int  kapi_wait (void *proc) { return KT->wait (proc); }
static inline int  kapi_get_args (char *b, unsigned n) { return KT->get_args (b, n); }

// Friendly aliases used by the demos.
static inline unsigned *create_window (int w, int h, const char *t) { return kapi_create_window (w, h, t); }
static inline void      present (void)             { kapi_present (); }
static inline unsigned  get_ticks (void)           { return kapi_get_ticks (); }
static inline void      msleep (unsigned ms)       { kapi_msleep (ms); }
static inline unsigned long add_button (int x, int y, int w, int h, const char *l, gui_handler h2) { return kapi_add_button (x, y, w, h, l, h2); }
static inline void      pump_events (void)         { kapi_pump_events (); }
static inline int       should_exit (void)         { return kapi_should_exit (); }

#endif
