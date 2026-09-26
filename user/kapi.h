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
#define WIN_FLAG_BACKMOST	(1u << 1)	// pinned to the bottom of the z-order (shell desktop)
#define WIN_FLAG_TOPMOST	(1u << 2)	// pinned to the top, never active (the menu bar)
#define WIN_FLAG_TRANSPARENT	(1u << 3)	// magenta (0xFF00FF) client pixels are see-through
#define WIN_FLAG_SYSTEM		(1u << 4)	// system component: not listed as an open app (panel taskbar)

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
// widget toolkits (uikit.h). value packs (wheel<<48)|(changed<<40)|(buttons<<32)|(x<<16)|y,
// all client-relative; decode with the GUI_PTR_* macros below.
#define GUI_EVENT_PTR_MOVE	8	// cursor moved
#define GUI_EVENT_PTR_DOWN	9	// a button went down (changed = which: 1/2/4)
#define GUI_EVENT_PTR_UP	10	// a button went up (changed = which)
#define GUI_EVENT_PTR_ENTER	11	// cursor entered the client area
#define GUI_EVENT_PTR_LEAVE	12	// cursor left the client area
#define GUI_EVENT_PTR_WHEEL	13	// scroll wheel turned (GUI_PTR_WHEEL = signed notch delta)
#define GUI_EVENT_MENU		14	// menu-bar command chosen (value = item id, ABI v39)
#define MENU_QUIT		(-1)	// kapi_menu_command id: close the active app
// Drag & drop (ABI v42) -- to the pointer handler (see kapi_drag_begin):
#define GUI_EVENT_DROP		15	// dropped on us: GUI_PTR_X/Y + GUI_DND_FLAGS; kapi_drag_data
#define GUI_EVENT_DRAG_OVER	16	// a drag hovers us (GUI_DND_FLAGS & DND_F_LEAVE: it left)
#define GUI_EVENT_DRAG_DONE	17	// to the source: GUI_DND_PID (0 = none) + GUI_DND_FLAGS
#define GUI_DND_FLAGS(v)	((unsigned) (((unsigned long) (v) >> 32) & 0xFF))
#define GUI_DND_PID(v)		((int) ((unsigned long) (v) & 0xFFFFFFFF))
#define DND_F_COPY		1	// Ctrl held: copy instead of move
#define DND_F_CANCEL		2	// DRAG_DONE: cancelled (Esc)
#define DND_F_DESKTOP		4	// DRAG_DONE: dropped on the desktop / no window
#define DND_F_LEAVE		8	// DRAG_OVER: the drag left this window
#define DND_TEXT		1	// payload types (= CLIP_TEXT / CLIP_FILES)
#define DND_FILES		2
#define MOD_CTRL		1	// kapi_get_modifiers
#define MOD_SHIFT		2
#define MOD_ALT			4
#define WIN_MENU_MAX_USER	2048	// max menu spec length (= the kernel's WIN_MENU_MAX)
#define GUI_PTR_Y(v)		((int) ((unsigned long) (v) & 0xFFFF))
#define GUI_PTR_X(v)		((int) (((unsigned long) (v) >> 16) & 0xFFFF))
#define GUI_PTR_BUTTONS(v)	((int) (((unsigned long) (v) >> 32) & 0xFF))	// held mask
#define GUI_PTR_CHANGED(v)	((int) (((unsigned long) (v) >> 40) & 0xFF))	// 1 left/2 right/4 mid
#define GUI_PTR_WHEEL(v)	((int) (signed char) (((unsigned long) (v) >> 48) & 0xFF)) // +fwd/-back

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
#define KEY_F1			0x110	// .. KEY_F12 = 0x11B (KEY_F1 + n - 1)
#define KEY_F12			0x11B

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

// Modal-dialog button sets (used by the user-side ui::MessageBox in uidialog.hpp;
// the kernel message_box/file dialogs were removed -- the toolkit draws its own).
#define MB_OK		0
#define MB_OKCANCEL	1
#define MB_YESNO	2
#define MB_YESNOCANCEL	3	// wtk: Yes = 1, No = 2, Cancel / Esc = 0
// Run an ELF at an absolute path with an argv string (fire-and-forget). 1/0.
static inline int  kapi_exec (const char *path, const char *args) { return KT->exec (path, args); }
// Framebuffer size in pixels (for edge-pinned borderless windows).
static inline void kapi_screen_size (int *w, int *h) { KT->screen_size (w, h); }
static inline int  kapi_wallpaper_generate (unsigned base, int pts, unsigned seed) { return KT->wallpaper_generate (base, pts, seed); }
// App-drawn wallpaper: get the shared screen-sized buffer, draw into it, then commit.
static inline unsigned *kapi_wallpaper_buffer (int *w, int *h) { return KT->wallpaper_buffer (w, h); }
static inline void kapi_wallpaper_commit (void) { KT->wallpaper_commit (); }
static inline void kapi_present (void) { KT->present (); }
static inline unsigned kapi_get_ticks (void) { return KT->get_ticks (); }
static inline void kapi_msleep (unsigned ms) { KT->msleep (ms); }
static inline void kapi_yield (void) { KT->yield (); }
static inline void kapi_exit (int s) { KT->exit (s); }

// (The kernel-drawn widget wrappers -- kapi_add_*/kapi_widget_* -- were removed: every
// app now builds its UI with the user-side uikit.hpp toolkit. See uikit.hpp.)

// --- events ------------------------------------------------------------------
static inline void kapi_pump_events (void) { KT->pump_events (); }
static inline void kapi_wait_for_exit (void) { KT->wait_for_exit (); }
static inline int  kapi_should_exit (void) { return KT->should_exit (); }

// --- app-drawn text + keyboard -----------------------------------------------
static inline void kapi_draw_text (int x, int y, const char *s, unsigned c) { KT->draw_text (x, y, s, c); }
// Draw kernel-font text into an arbitrary app-mapped 0x00RRGGBB buffer (e.g. a window-
// chrome copy from kapi_get_chrome). Transparent background; dst must be a user VA.
static inline void kapi_draw_text_buf (unsigned *dst, int dw, int dh, int x, int y, const char *s, unsigned c) { KT->draw_text_buf (dst, dw, dh, x, y, s, c); }
// Window surfaces for a user-side chrome drawer (ABI v28). Returns 1 + fills *out
// (content canvas + active/inactive chrome copies + insets + title), or 0 if no window.
static inline int  kapi_get_chrome (struct kapi_chrome *out) { return KT->get_chrome (out); }
static inline int  kapi_font_width (void) { return KT->font_width (); }
static inline int  kapi_font_height (void) { return KT->font_height (); }
static inline void kapi_set_key_handler (gui_handler fn) { KT->set_key_handler (fn); }
static inline void kapi_set_click_handler (gui_handler fn) { KT->set_click_handler (fn); }
static inline void kapi_set_pointer_handler (gui_handler fn) { KT->set_pointer_handler (fn); }
// Memory snapshot (KB): total RAM, free, app-owned, page size. Any pointer may be 0.
static inline int kapi_meminfo (unsigned long *total_kb, unsigned long *free_kb, unsigned long *app_kb, unsigned *page_kb) { return KT->meminfo (total_kb, free_kb, app_kb, page_kb); }
// ABI v33: firmware-detected board RAM + app page-pool (HIGH zone) total/free, the bytes
// reclaimed above 4GB, and the high-segment count. All KB; any pointer may be 0. (detected =
// physical board RAM e.g. 8192 MB; apppool = the zone backing app frames via palloc_high.)
static inline int kapi_ram_detail (unsigned long *detected_kb, unsigned long *apppool_kb, unsigned long *apppool_free_kb, unsigned long *above4g_kb, unsigned *nsegments) { return KT->ram_detail (detected_kb, apppool_kb, apppool_free_kb, above4g_kb, nsegments); }
// ABI v34: scroll-wheel speed = lines scrolled per notch, applied system-wide (clamped
// 1..16). The theme editor sets + persists it (SD:/etc/theme.txt wheelspeed=N).
static inline void kapi_set_wheel_speed (int lines_per_notch) { KT->set_wheel_speed (lines_per_notch); }
static inline int  kapi_get_wheel_speed (void) { return KT->get_wheel_speed (); }
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
// mkdir / remove / rename: 0 = success, -1 = failure (FatFs result).
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
// TCP server side (ABI v37). tcp_listen: listening handle >=0 / <0 (-6 port in use).
// tcp_accept: BLOCKS until a peer connects; connected handle >=0 + peer IP / <0.
static inline int  kapi_tcp_listen (unsigned port) { return KT->tcp_listen (port); }
static inline int  kapi_tcp_accept (int listen_sock, char *ip, unsigned cap) { return KT->tcp_accept (listen_sock, ip, cap); }

// Remote screen (ABI v38). screen_grab: composite the screen into dst (w*h 0x00RRGGBB,
// w/h = kapi_screen_size) -> 1 / 0, or 2 = unchanged since the previous grab into the same
// dst (left as is: skip the diff). inject_pointer/inject_key: input as if from the USB
// mouse (buttons bit0 L, bit1 R, bit2 M; wheel = notches) / keyboard (cooked string).
static inline int  kapi_screen_grab (unsigned *dst, int w, int h) { return KT->screen_grab (dst, w, h); }
static inline void kapi_inject_pointer (int x, int y, unsigned buttons, int wheel) { KT->inject_pointer (x, y, buttons, wheel); }
static inline void kapi_inject_key (const char *keys) { KT->inject_key (keys); }

// System menu bar (ABI v39). set_menu: declare this app's menus (spec lines "M<title>",
// "I<id>\t<label>\t<shortcut>", "-") + the GUI_EVENT_MENU handler -- apps normally use
// wtk::Menu. get_menu / menu_command: for the menu-bar app (active window's spec+title ->
// change serial, 0 = none; send item id, MENU_QUIT closes the active app).
static inline int      kapi_set_menu (const char *spec, gui_handler h) { return KT->set_menu (spec, h); }
static inline unsigned kapi_get_menu (char *buf, unsigned cap, char *title, unsigned tcap) { return KT->get_menu (buf, cap, title, tcap); }
static inline int      kapi_menu_command (int id) { return KT->menu_command (id); }

// Named IPC services (ABI v40): ipc_register -> become service `name` (1 / 0 taken);
// ipc_lookup -> its pid or 0. Messages go through kapi_mailbox_send / _recv (<= 512 B).
static inline int  kapi_ipc_register (const char *name) { return KT->ipc_register (name); }
static inline int  kapi_ipc_lookup (const char *name)   { return KT->ipc_lookup (name); }
// System clipboard (ABI v40): see clipboard.h for the helpers. Types:
#define CLIP_TEXT	1		// plain text
#define CLIP_FILES	2		// file/folder paths, '\n'-separated (copied)
#define CLIP_FILES_CUT	3		// same, cut (the paste moves them)
static inline int  kapi_clipboard_set (int type, const void *d, unsigned n) { return KT->clipboard_set (type, d, n); }
static inline int  kapi_clipboard_get (int *type, void *b, unsigned cap, unsigned *serial) { return KT->clipboard_get (type, b, cap, serial); }
// Window opacity 0..255 (ABI v40; fades) and end of session (0 = halt, 1 = restart).
static inline void kapi_set_window_alpha (int a) { KT->set_window_alpha (a); }
#define SHUTDOWN_HALT		0
#define SHUTDOWN_RESTART	1
static inline void kapi_shutdown (int mode) { KT->shutdown (mode); }

// Full-screen apps (ABI v41): fullscreen_begin -> a screen-sized 0x00RRGGBB back buffer
// (w/h filled); the desktop stops drawing and all input (screen coords) comes to you.
// Draw, present_fb to show it; fullscreen_end (or exiting) gives the desktop back.
static inline unsigned *kapi_fullscreen_begin (int *w, int *h) { return KT->fullscreen_begin (w, h); }
static inline void kapi_present_fb (void) { KT->present_fb (); }
static inline void kapi_fullscreen_end (void) { KT->fullscreen_end (); }

// Drag & drop (ABI v42). drag_begin: while the left button is held (from a pointer-move
// handler, after a few pixels of motion), drag (type, data <= 4 KB) with `label` on the
// cursor -> 1 / 0. The target gets GUI_EVENT_DROP and reads the payload with drag_data
// (copies <= cap, returns the full length); the source gets GUI_EVENT_DRAG_DONE.
// get_modifiers: MOD_* held now; inject_modifiers: set them (vncd).
static inline int  kapi_drag_begin (int type, const void *data, unsigned len, const char *label) { return KT->drag_begin (type, data, len, label); }
static inline int  kapi_drag_data (int *type, void *buf, unsigned cap) { return KT->drag_data (type, buf, cap); }
static inline unsigned kapi_get_modifiers (void) { return KT->get_modifiers (); }
static inline void kapi_inject_modifiers (unsigned mods) { KT->inject_modifiers (mods); }

// Network tools (ABI v43). net_ping: one ICMP echo (RTT in us, or -1 down / -3 unresolved /
// -4 timeout / -5 send failed). net_resolve: DNS -> dotted IP (1/0). net_info: netstat text.
static inline int kapi_net_ping (const char *host, unsigned seq, unsigned timeout_ms, char *ip, unsigned cap) { return KT->net_ping (host, seq, timeout_ms, ip, cap); }
static inline int kapi_net_resolve (const char *host, char *ip, unsigned cap) { return KT->net_resolve (host, ip, cap); }
static inline int kapi_net_info (char *buf, unsigned cap) { return KT->net_info (buf, cap); }

// User-space file-system providers (ABI v44, kern/vfs.h): a provider app serves every path
// starting with its prefix ("FTP:"); the file kapis on those paths reach it as requests.
#define VFS_OP_OPEN	1	// path -> status = fid (>= 0); data = u32 file size
#define VFS_OP_READ	2	// a0 fid, a1 offset, a2 length -> data, status = n
#define VFS_OP_CLOSE	3	// a0 fid
#define VFS_OP_LIST	4	// path -> data = (u32 size LE, u8 is_dir, name '\0')*, status = count
#define VFS_OP_SAVE	5	// path, payload (vfs_req_data) -> status = bytes written
#define VFS_OP_MKDIR	6	// path -> 0 / -1
#define VFS_OP_REMOVE	7	// path -> 0 / -1
#define VFS_OP_RENAME	8	// path -> path2: 0 / -1
static inline int kapi_vfs_register (const char *prefix) { return KT->vfs_register (prefix); }
static inline int kapi_vfs_next (struct kapi_vfs_req *req, int blocking) { return KT->vfs_next (req, blocking); }
static inline int kapi_vfs_req_data (unsigned id, void *buf, unsigned cap, unsigned offset) { return KT->vfs_req_data (id, buf, cap, offset); }
static inline int kapi_vfs_reply (unsigned id, int status, const void *data, unsigned len) { return KT->vfs_reply (id, status, data, len); }

// Wi-Fi scan (ABI v45): the access points around, strongest first (struct kapi_wlan_ap:
// ssid, bssid, security WLAN_SEC_OPEN/WEP/WPA/WPA2, channel, freq MHz, level dBm,
// connected). Takes ~3 s (blocks the caller); returns how many (0 = none / no Wi-Fi).
static inline int kapi_wlan_scan (struct kapi_wlan_ap *out, int max) { return KT->wlan_scan (out, max); }

// Sound (ABI v46), on the 3.5 mm jack. First acquire the output (1 ok, 0 another app has
// it, -1 no audio); only the owner plays, and its release -- or its exit -- silences it.
//   kapi_sound_start (voice 0..15, milli-Hz (440 Hz = 440000), SOUND_SQUARE/SINE/TRIANGLE/
//                     SAW/NOISE, volume 0..255): the note plays until kapi_sound_stop (voice)
//                     (-1 = all). Short attack / release ramps: no clicks.
//   kapi_sound_write (s16 L/R frames at SOUND_RATE, n): a PCM stream mixed with the voices;
//                     returns the frames taken (0 = full, retry later) -- audio players.
static inline int  kapi_sound_acquire (void) { return KT->sound_acquire (); }
static inline void kapi_sound_release (void) { KT->sound_release (); }
static inline int  kapi_sound_start (int voice, unsigned millihz, int wave, int volume) { return KT->sound_start (voice, millihz, wave, volume); }
static inline int  kapi_sound_stop (int voice) { return KT->sound_stop (voice); }
static inline int  kapi_sound_write (const short *frames, unsigned n) { return KT->sound_write (frames, n); }
static inline int  kapi_sound_status (unsigned *rate, unsigned *free_frames, unsigned *owner) { return KT->sound_status (rate, free_frames, owner); }
// FM (ABI v47): give a voice a 2-operator FM instrument (struct kapi_fm_instrument, see
// kern/kapi_abi.h), then kapi_sound_start (voice, milliHz, SOUND_FM, volume) keys it on and
// kapi_sound_stop (voice) keys it off (its release rate fades it out).
static inline int  kapi_sound_instrument (int voice, const struct kapi_fm_instrument *ins) { return KT->sound_instrument (voice, ins); }

// Held keys (ABI v48), for games (key events only report presses): 1 while `key` is held
// and this window has the keyboard. key = KEY_UP/DOWN/LEFT/RIGHT, KEY_ENTER, 27 (Esc), ' ',
// 'a'..'z' (US position of the key on a USB keyboard), '0'..'9'. Returns 0 on an older kernel.
static inline int  kapi_key_held (int key) { return KT->version >= 48 ? KT->key_held (key) : 0; }
static inline void kapi_inject_key_held (int key, int down) { if (KT->version >= 48) KT->inject_key_held (key, down); }
// Run a program under another process name (ABI v49): a runner running an app is named
// after the app (its window, list_windows, raise_app). See launch.h.
static inline int  kapi_exec_as (const char *path, const char *args, const char *name) { return KT->version >= 49 ? KT->exec_as (path, args, name) : KT->exec (path, args); }
// USB gamepads (v50): the raw state of pad 0..3 (1 = there); user/gamepad.h maps its buttons.
static inline int  kapi_pad_state (int index, struct kapi_pad *out) { return KT->version >= 50 ? KT->pad_state (index, out) : 0; }

// Reboot the machine (ABI v25). Does not return. Use to apply settings the kernel
// only reads at boot -- e.g. after wpaconf rewrites SD:/etc/wpa_supplicant.conf.
static inline void kapi_reboot (void) { KT->reboot (); }

// 1 if a USB keyboard is attached & ready, else 0 (ABI v26). The `keyb` tool polls
// this at boot before applying a layout (it can run before USB enumeration finishes).
static inline int kapi_kbd_ready (void) { return KT->kbd_ready (); }

// Load a keyboard layout from a .kmap blob (ABI v27): "OKM1" + u16 rows + u16 cols +
// table. The kernel copies it; the caller frees `data`. 1 on success. See /etc/keymaps.
static inline int kapi_set_keymap_data (const char *name, const void *data, unsigned len) { return KT->set_keymap_data (name, data, len); }

// Hardware RNG (ABI v30): fill buf[len] with random bytes from the Pi's HW RNG. For
// cryptographic seeding -- the TLS entropy source uses it. Returns bytes written.
static inline int kapi_random (void *buf, unsigned len) { return KT->random (buf, len); }

// Shell surfaces (ABI v35): shared 0x00RRGGBB pixel buffers for the activity-shell
// compositor. The shell creates one sized to a viewport, passes its id to an app; both
// map it (same physical frames, own VA), the app draws + presents, the shell composites.
static inline int       kapi_surface_create (int w, int h)         { return KT->surface_create (w, h); }
static inline unsigned *kapi_surface_map (int id)                  { return KT->surface_map (id); }
static inline int       kapi_surface_size (int id, int *w, int *h) { return KT->surface_size (id, w, h); }
static inline void      kapi_surface_present (int id)              { KT->surface_present (id); }
static inline int       kapi_surface_destroy (int id)              { return KT->surface_destroy (id); }

// Activity-shell IPC (ABI v35): the kernel routes opaque {from_pid,type,bytes} messages
// between per-process mailboxes. A user compositor calls kapi_register_shell to become
// THE shell; apps post to it with kapi_shell_request; the shell replies / pushes async
// events with kapi_mailbox_send; both drain with kapi_mailbox_recv (fills *from_pid /
// *type, returns the payload length, or -1 if empty; blocking != 0 waits).
static inline int kapi_register_shell (void)                                              { return KT->register_shell (); }
static inline int kapi_shell_request (int type, const void *in, unsigned len)             { return KT->shell_request (type, in, len); }
static inline int kapi_mailbox_send (int target_pid, int type, const void *in, unsigned len) { return KT->mailbox_send (target_pid, type, in, len); }
static inline int kapi_mailbox_recv (int *from_pid, int *type, void *buf, unsigned cap, int blocking) { return KT->mailbox_recv (from_pid, type, buf, cap, blocking); }

// Memory primitives (ABI v36): the kernel's (Circle's) memset/memcpy/memmove. Real,
// weak symbols (not static inline) so the linker can see them: the freestanding app
// Makefiles alias the C names onto them (-Wl,--defsym,memset=kapi_memset ...), which
// resolves the calls GCC emits on its own (array/struct init and copies).
#ifdef __cplusplus
extern "C" {
#endif
__attribute__ ((weak)) void *kapi_memset (void *dst, int c, unsigned long n)                 { return KT->memset (dst, c, n); }
__attribute__ ((weak)) void *kapi_memcpy (void *dst, const void *src, unsigned long n)       { return KT->memcpy (dst, src, n); }
__attribute__ ((weak)) void *kapi_memmove (void *dst, const void *src, unsigned long n)      { return KT->memmove (dst, src, n); }
#ifdef __cplusplus
}
#endif

// Friendly aliases used by the demos.
static inline unsigned *create_window (int w, int h, const char *t) { return kapi_create_window (w, h, t); }
static inline void      present (void)             { kapi_present (); }
static inline unsigned  get_ticks (void)           { return kapi_get_ticks (); }
static inline void      msleep (unsigned ms)       { kapi_msleep (ms); }
static inline void      pump_events (void)         { kapi_pump_events (); }
static inline int       should_exit (void)         { return kapi_should_exit (); }

#endif
