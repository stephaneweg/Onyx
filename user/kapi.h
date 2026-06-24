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
#define GUI_EVENT_CANVAS_CLICK	6	// client-area press; value = (buttons<<32)|(x<<16)|y
#define GUI_EVENT_CANVAS_MOTION	7	// drag (button held) over the client area; same value
					// buttons: bit0 left, bit1 right
// Full pointer stream (ABI v22, opt-in via kapi_set_pointer_handler) for app-side
// widget toolkits (uikit.h). value packs (changed<<40)|(buttons<<32)|(x<<16)|y, all
// client-relative; decode with the GUI_PTR_* macros below.
#define GUI_EVENT_PTR_MOVE	8	// cursor moved
#define GUI_EVENT_PTR_DOWN	9	// a button went down (changed = which: 1/2/4)
#define GUI_EVENT_PTR_UP	10	// a button went up (changed = which)
#define GUI_EVENT_PTR_ENTER	11	// cursor entered the client area
#define GUI_EVENT_PTR_LEAVE	12	// cursor left the client area
#define GUI_PTR_Y(v)		((int) ((unsigned long) (v) & 0xFFFF))
#define GUI_PTR_X(v)		((int) (((unsigned long) (v) >> 16) & 0xFFFF))
#define GUI_PTR_BUTTONS(v)	((int) (((unsigned long) (v) >> 32) & 0xFF))	// held mask
#define GUI_PTR_CHANGED(v)	((int) (((unsigned long) (v) >> 40) & 0xFF))	// 1 left/2 right/4 mid

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
static inline void kapi_move_window (int x, int y) { KT->move_window (x, y); }
static inline int  kapi_launch (const char *n) { return KT->launch (n); }
static inline int  kapi_toggle_app (const char *n) { return KT->toggle_app (n); }
static inline int  kapi_raise_app (const char *n) { return KT->raise_app (n); }
static inline int  kapi_list_windows (char *b, unsigned s) { return KT->list_windows (b, s); }
static inline int  kapi_list_tasks (char *b, unsigned s) { return KT->list_tasks (b, s); }
static inline int  kapi_kill (const char *name) { return KT->kill (name); }
// ps / kill by PID. list_procs: lines "<pid> <a|k> <state> <name>". kill_pid:
// force 0 = clean close, 1 = hard terminate; 1 ok / 0 no such pid / -1 protected.
static inline int  kapi_list_procs (char *b, unsigned s) { return KT->list_procs (b, s); }
static inline int  kapi_kill_pid (int pid, int force) { return KT->kill_pid (pid, force); }
// Keyboard layout: switch among the compiled-in country maps; read the current one.
static inline int  kapi_set_keymap (const char *name) { return KT->set_keymap (name); }
static inline int  kapi_get_keymap (char *b, unsigned s) { return KT->get_keymap (b, s); }
// Re-tint window chrome at runtime (theme editor): active/inactive skin + title text.
static inline void kapi_set_window_theme (unsigned a, unsigned i, unsigned t) { KT->set_window_theme (a, i, t); }

// Modal message box (blocks until answered). Returns 1 (OK/Yes) or 0 (Cancel/No).
#define MB_OK		0
#define MB_OKCANCEL	1
#define MB_YESNO	2
static inline int  kapi_message_box (const char *title, const char *text, int buttons) { return KT->message_box (title, text, buttons); }
// Modal file dialogs (block until chosen/cancelled). Return 1 + fill `out`, or 0.
static inline int  kapi_file_open (char *out, unsigned cap, const char *start_dir) { return KT->file_open (out, cap, start_dir); }
static inline int  kapi_file_save (char *out, unsigned cap, const char *start_dir, const char *def_name) { return KT->file_save (out, cap, start_dir, def_name); }
// Run an ELF at an absolute path with an argv string (fire-and-forget). 1/0.
static inline int  kapi_exec (const char *path, const char *args) { return KT->exec (path, args); }
// Framebuffer size in pixels (for edge-pinned borderless windows).
static inline void kapi_screen_size (int *w, int *h) { KT->screen_size (w, h); }
static inline int  kapi_set_wallpaper (const char *p) { return KT->set_wallpaper (p); }
static inline int  kapi_wallpaper_generate (unsigned base, int pts, unsigned seed) { return KT->wallpaper_generate (base, pts, seed); }
// App-drawn wallpaper: get the shared screen-sized buffer, draw into it, then commit.
static inline unsigned *kapi_wallpaper_buffer (int *w, int *h) { return KT->wallpaper_buffer (w, h); }
static inline void kapi_wallpaper_commit (void) { KT->wallpaper_commit (); }
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
static inline void kapi_set_pointer_handler (gui_handler fn) { KT->set_pointer_handler (fn); }
// Memory snapshot (KB): total RAM, free, app-owned, page size. Any pointer may be 0.
static inline int kapi_meminfo (unsigned long *total_kb, unsigned long *free_kb, unsigned long *app_kb, unsigned *page_kb) { return KT->meminfo (total_kb, free_kb, app_kb, page_kb); }
// Per-process heap: move the break by `inc` bytes (Unix sbrk); returns the previous
// break or (void*)-1. The user allocator (umm.h) is built on this; apps rarely call it.
static inline void *kapi_sbrk (long inc) { return KT->sbrk (inc); }

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
// Working directory: chdir (1/0) + getcwd. Relative paths in file ops resolve here.
static inline int  kapi_chdir (const char *p) { return KT->chdir (p); }
static inline int  kapi_getcwd (char *b, unsigned n) { return KT->getcwd (b, n); }

// --- directory listing -------------------------------------------------------
static inline void *kapi_opendir (const char *p) { return KT->opendir (p); }
static inline int  kapi_readdir (void *d, struct kapi_dirent *e) { return KT->readdir (d, e); }
static inline void kapi_closedir (void *d) { KT->closedir (d); }
static inline int  kapi_mkdir (const char *p) { return KT->mkdir (p); }
static inline int  kapi_remove (const char *p) { return KT->remove (p); }
static inline int  kapi_rename (const char *from, const char *to) { return KT->rename (from, to); }
static inline void kapi_cursor_pos (int *x, int *y) { KT->cursor_pos (x, y); }

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
// This task's own stdin/stdout stream handles (for a shell wiring children). 0 = none.
static inline void *kapi_stdin (void) { return KT->stdin_stream (); }
static inline void *kapi_stdout (void) { return KT->stdout_stream (); }
// Read the next kernel log event (real-time tee). 1 + fills fields, or 0 if empty.
static inline int  kapi_klog_read (int *sev, char *src, unsigned sc, char *msg, unsigned mc) { return KT->klog_read (sev, src, sc, msg, mc); }
// Verbose kernel logging: toggle (1/0) at runtime + read the current state.
static inline int  kapi_set_verbose (int on) { return KT->set_verbose (on); }
static inline int  kapi_get_verbose (void) { return KT->get_verbose (); }

// TCP/IP sockets over WLAN (ABI v21). net_status: 1 + dotted IP into ip[] if the
// link is up, else 0. tcp_connect: host = dotted-quad or DNS name; >=0 handle / <0
// error. tcp_send: blocking, bytes sent / <0. tcp_recv: NON-BLOCKING -- >0 bytes,
// 0 nothing yet, <0 closed/error. tcp_close: drop the connection.
static inline int  kapi_net_status (char *ip, unsigned cap) { return KT->net_status (ip, cap); }
static inline int  kapi_tcp_connect (const char *host, unsigned port) { return KT->tcp_connect (host, port); }
static inline int  kapi_tcp_send (int sock, const void *buf, unsigned len) { return KT->tcp_send (sock, buf, len); }
static inline int  kapi_tcp_recv (int sock, void *buf, unsigned len) { return KT->tcp_recv (sock, buf, len); }
static inline void kapi_tcp_close (int sock) { KT->tcp_close (sock); }

// Reboot the machine (ABI v25). Does not return. Use to apply settings the kernel
// only reads at boot -- e.g. after wpaconf rewrites SD:/etc/wpa_supplicant.conf.
static inline void kapi_reboot (void) { KT->reboot (); }

// Friendly aliases used by the demos.
static inline unsigned *create_window (int w, int h, const char *t) { return kapi_create_window (w, h, t); }
static inline void      present (void)             { kapi_present (); }
static inline unsigned  get_ticks (void)           { return kapi_get_ticks (); }
static inline void      msleep (unsigned ms)       { kapi_msleep (ms); }
static inline unsigned long add_button (int x, int y, int w, int h, const char *l, gui_handler h2) { return kapi_add_button (x, y, w, h, l, h2); }
static inline void      pump_events (void)         { kapi_pump_events (); }
static inline int       should_exit (void)         { return kapi_should_exit (); }

#endif
