//
// appkit.h -- AppKit: what a program includes to talk to the system.
//
// A program calls the functions declared here (kapi_*: the names of AppKit's table, kept as they
// were); they are AppKit's (SD:/lib/appkit.so, user/Kits/appkit), which the kernel binds to every program.
// A program never talks to the kernel itself. Their bodies are in appkit_calls.inc, beside this file.
// The structures and constants shared with the kernel come from <kern/kapi_abi.h>.
//
// (This header was user/kapi.h until 2026-10-05. There is no kapi.h any more: what talks to the kernel
//  is in this folder only.)
//
#ifndef _appkit_h
#define _appkit_h

#include <kern/kapi_abi.h>

// ---- how a program reaches the kernel: AppKit (2026-10-05) -------------------------------------------
// The kapi_* functions declared below (KAPI_FN) are AppKit's (SD:/lib/appkit.so, user/Kits/appkit: the ONE
// interface between the programs and the kernel -- loaded by the kernel and bound to every program
// with no call of theirs). A program calls them BY NAME, through the import stubs linked into it
// (lib/appkit_stubs.o). THIS HEADER ONLY DECLARES THEM: their bodies -- the calls through the kernel's
// table -- are in appkit_calls.inc (beside this file), compiled into AppKit (appkit/appkit.c) and nowhere
// else. So the kernel's table can be restructured -- entries moved, removed, merged -- by adapting
// that file and rebuilding AppKit alone: no program changes.
//
// The one exception: with KAPI_INLINE (the tests of the table itself: el0test, faulttest), or in a PC
// build (the simulator's stand-in kernel has a table of its own and no AppKit), the bodies are
// compiled inline into the program -- the include at this header's end.
//
// What stays inline here makes no call to the kernel (the spin locks, kapi_clock_us, the sound ring's
// writer): an app core may use it.
#ifdef __cplusplus
#define KAPI_C	extern "C"
#else
#define KAPI_C
#endif
#if defined (KAPI_INLINE) || !defined (__aarch64__)
#define KAPI_FN	static inline
#else
#define KAPI_FN	KAPI_C
#endif

// The kernel's kapi version (KAPI_ABI_VERSION as the running kernel says it): what a program tests
// before a call that came late (it was the first word of the kernel's table).
KAPI_FN unsigned kapi_abi_version (void);
// The address of a slot of the kernel's table (its index in 8-byte words): for the tests of the
// table itself. Nothing else needs it.
KAPI_FN unsigned long long kapi_table_slot (unsigned slot);

// Window creation flags (must match kern/gui/window.h).
#define WIN_FLAG_BORDERLESS	(1u << 0)	// no title bar / border / close box
#define WIN_FLAG_BACKMOST	(1u << 1)	// pinned to the bottom of the z-order (shell desktop)
#define WIN_FLAG_TOPMOST	(1u << 2)	// pinned to the top, never active (the menu bar)
#define WIN_FLAG_TRANSPARENT	(1u << 3)	// magenta (0xFF00FF) client pixels are see-through
#define WIN_FLAG_SYSTEM		(1u << 4)	// system component: not listed as an open app (panel taskbar)
#define WIN_FLAG_ALPHA		(1u << 5)	// (v64, borderless) the pixels' top byte is a transparency
						// (0 opaque .. 255 see-through; clicks there go below)
#define WIN_FLAG_FIXED		(1u << 6)	// (v69) not movable, no title buttons, kept centred

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
#define GUI_EVENT_DISPLAY_RESIZE 19	// (v66) the screen's size changed: GUI_DISPLAY_W / _H (value)
#define GUI_DISPLAY_W(v)	((int) (((v) >> 16) & 0xFFFF))
#define GUI_DISPLAY_H(v)	((int) ((v) & 0xFFFF))
#define GUI_EVENT_WINCTL	18	// (v64) a title button: value KAPI_FRAME_MENU (the window menu)
					// or KAPI_FRAME_MAXIMISE (also a double click on the title)
#define GUI_DND_FLAGS(v)	((unsigned) (((unsigned long long) (v) >> 32) & 0xFF))
#define GUI_DND_PID(v)		((int) ((unsigned long long) (v) & 0xFFFFFFFF))
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
#define GUI_PTR_Y(v)		((int) ((unsigned long long) (v) & 0xFFFF))
#define GUI_PTR_X(v)		((int) (((unsigned long long) (v) >> 16) & 0xFFFF))
#define GUI_PTR_BUTTONS(v)	((int) (((unsigned long long) (v) >> 32) & 0xFF))	// held mask
#define GUI_PTR_CHANGED(v)	((int) (((unsigned long long) (v) >> 40) & 0xFF))	// 1 left/2 right/4 mid
#define GUI_PTR_WHEEL(v)	((int) (signed char) (((unsigned long long) (v) >> 48) & 0xFF)) // +fwd/-back

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
KAPI_FN unsigned * kapi_create_window (int w, int h, const char *t);
KAPI_FN unsigned * kapi_create_window_ex (int x, int y, int w, int h, const char *t, unsigned f);
KAPI_FN unsigned * kapi_resize_window (int w, int h);
KAPI_FN void kapi_move_window (int x, int y);
KAPI_FN int kapi_launch (const char *n);
KAPI_FN int kapi_toggle_app (const char *n);
KAPI_FN int kapi_raise_app (const char *n);
KAPI_FN int kapi_list_windows (char *b, unsigned s);
KAPI_FN int kapi_list_tasks (char *b, unsigned s);
KAPI_FN int kapi_kill (const char *name);
// ps / kill by PID. list_procs: lines "<pid> <a|k> <state> <name>". kill_pid:
// force 0 = clean close, 1 = hard terminate; 1 ok / 0 no such pid / -1 protected.
KAPI_FN int kapi_list_procs (char *b, unsigned s);
KAPI_FN int kapi_kill_pid (int pid, int force);
// Keyboard layout: switch among the compiled-in country maps; read the current one.
KAPI_FN int kapi_set_keymap (const char *name);
KAPI_FN int kapi_get_keymap (char *b, unsigned s);

// Modal-dialog button sets (used by the user-side ui::MessageBox in uidialog.hpp;
// the kernel message_box/file dialogs were removed -- the toolkit draws its own).
#define MB_OK		0
#define MB_OKCANCEL	1
#define MB_YESNO	2
#define MB_YESNOCANCEL	3	// uikit: Yes = 1, No = 2, Cancel / Esc = 0
// Run an ELF at an absolute path with an argv string (fire-and-forget). 1/0.
KAPI_FN int kapi_exec (const char *path, const char *args);
// Framebuffer size in pixels (for edge-pinned borderless windows).
KAPI_FN void kapi_screen_size (int *w, int *h);
KAPI_FN int kapi_wallpaper_generate (unsigned base, int pts, unsigned seed);
// App-drawn wallpaper: get the shared screen-sized buffer, draw into it, then commit.
KAPI_FN unsigned * kapi_wallpaper_buffer (int *w, int *h);
KAPI_FN void kapi_wallpaper_commit (void);
KAPI_FN void kapi_present (void);
KAPI_FN unsigned kapi_get_ticks (void);
KAPI_FN void kapi_msleep (unsigned ms);
KAPI_FN void kapi_yield (void);
KAPI_FN void kapi_exit (int s);

// (The kernel-drawn widget wrappers -- kapi_add_*/kapi_widget_* -- were removed: every
// app now builds its UI with the user-side uikit.hpp toolkit. See uikit.hpp.)

// --- events ------------------------------------------------------------------
KAPI_FN void kapi_pump_events (void);
KAPI_FN void kapi_wait_for_exit (void);
KAPI_FN int kapi_should_exit (void);

// --- app-drawn text + keyboard -----------------------------------------------
KAPI_FN void kapi_draw_text (int x, int y, const char *s, unsigned c);
// Draw kernel-font text into an arbitrary app-mapped 0x00RRGGBB buffer (e.g. a window-
// chrome copy from kapi_get_chrome). Transparent background; dst must be a user VA.
KAPI_FN void kapi_draw_text_buf (unsigned *dst, int dw, int dh, int x, int y, const char *s, unsigned c);
// Window surfaces for a user-side chrome drawer (ABI v28). Returns 1 + fills *out
// (content canvas + active/inactive chrome copies + insets + title), or 0 if no window.
KAPI_FN int kapi_get_chrome (struct kapi_chrome *out);
KAPI_FN int kapi_font_width (void);
KAPI_FN int kapi_font_height (void);
KAPI_FN void kapi_set_key_handler (gui_handler fn);
KAPI_FN void kapi_set_click_handler (gui_handler fn);
KAPI_FN void kapi_set_pointer_handler (gui_handler fn);
// Memory snapshot (KB): total RAM, free, app-owned, page size. Any pointer may be 0.
KAPI_FN int kapi_meminfo (unsigned long *total_kb, unsigned long *free_kb, unsigned long *app_kb, unsigned *page_kb);
// ABI v33: firmware-detected board RAM + app page-pool (HIGH zone) total/free, the bytes
// reclaimed above 4GB, and the high-segment count. All KB; any pointer may be 0. (detected =
// physical board RAM e.g. 8192 MB; apppool = the zone backing app frames via palloc_high.)
KAPI_FN int kapi_ram_detail (unsigned long *detected_kb, unsigned long *apppool_kb, unsigned long *apppool_free_kb, unsigned long *above4g_kb, unsigned *nsegments);
// ABI v34: scroll-wheel speed = lines scrolled per notch, applied system-wide (clamped
// 1..16). The theme editor sets + persists it (SD:/etc/theme.txt wheelspeed=N).
KAPI_FN void kapi_set_wheel_speed (int lines_per_notch);
KAPI_FN int kapi_get_wheel_speed (void);
// Per-process heap: move the break by `inc` bytes (Unix sbrk); returns the previous
// break or (void*)-1. The user allocator (umm.h) is built on this; apps rarely call it.
KAPI_FN void * kapi_sbrk (long inc);

// --- enumeration + clock -----------------------------------------------------
KAPI_FN int kapi_list_apps (char *b, unsigned s);
KAPI_FN int kapi_get_datetime (int *y, int *mo, int *d, int *h, int *mi, int *se);
KAPI_FN int kapi_app_dir (char *b, unsigned s);

// --- console + files ---------------------------------------------------------
KAPI_FN int kapi_write (int fd, const void *b, unsigned n);
KAPI_FN void * kapi_open (const char *p);
KAPI_FN int kapi_read (void *h, void *b, unsigned n);
KAPI_FN unsigned kapi_fsize (void *h);
KAPI_FN void kapi_close (void *h);
// save_file: the whole file written (created / replaced): the bytes written (>= 0; an empty file:
// 0 -- test < 0 for a failure, or == n), or -1.
KAPI_FN int kapi_save_file (const char *p, const void *b, unsigned n);
// Working directory: chdir (1/0) + getcwd. Relative paths in file ops resolve here.
KAPI_FN int kapi_chdir (const char *p);
KAPI_FN int kapi_getcwd (char *b, unsigned n);

// --- directory listing -------------------------------------------------------
KAPI_FN void * kapi_opendir (const char *p);
KAPI_FN int kapi_readdir (void *d, struct kapi_dirent *e);
KAPI_FN void kapi_closedir (void *d);
// mkdir / remove / rename: 0 = success, -1 = failure (FatFs result).
KAPI_FN int kapi_mkdir (const char *p);
KAPI_FN int kapi_remove (const char *p);
KAPI_FN int kapi_rename (const char *from, const char *to);
KAPI_FN void kapi_cursor_pos (int *x, int *y);

// --- stdio / streams / processes ---------------------------------------------
KAPI_FN void * kapi_pipe (void);
KAPI_FN void * kapi_file_in (const char *p);
KAPI_FN void * kapi_file_out (const char *p, int append);
KAPI_FN int kapi_stream_read (void *h, void *b, unsigned n);
KAPI_FN int kapi_stream_read_nb (void *h, void *b, unsigned n);
KAPI_FN int kapi_stream_write (void *h, const void *b, unsigned n);
KAPI_FN void kapi_stream_close (void *h);
KAPI_FN void kapi_stream_eof (void *h);
KAPI_FN int kapi_proc_done (void *proc);
KAPI_FN int kapi_stdin_read (void *b, unsigned n);
KAPI_FN int kapi_stdout_write (const void *b, unsigned n);
KAPI_FN void * kapi_spawn (const char *path, const char *args, void *in, void *out);
KAPI_FN int kapi_wait (void *proc);
KAPI_FN int kapi_get_args (char *b, unsigned n);
// This task's own stdin/stdout stream handles (for a shell wiring children). 0 = none.
KAPI_FN void * kapi_stdin (void);
KAPI_FN void * kapi_stdout (void);
// Read the next kernel log event (real-time tee). 1 + fills fields, or 0 if empty.
KAPI_FN int kapi_klog_read (int *sev, char *src, unsigned sc, char *msg, unsigned mc);
// Verbose kernel logging: toggle (1/0) at runtime + read the current state.
KAPI_FN int kapi_set_verbose (int on);
KAPI_FN int kapi_get_verbose (void);

// TCP/IP sockets over WLAN (ABI v21). net_status: 1 + dotted IP into ip[] if the
// link is up, else 0. tcp_connect: host = dotted-quad or DNS name; >=0 handle / <0
// error. tcp_send: blocking, the bytes queued / <0 -- a short count: a 5 s timeout after those
// (they are sent; send the rest again). tcp_recv: NON-BLOCKING -- >0 bytes,
// 0 nothing yet, <0 closed/error. tcp_close: drop the connection.
KAPI_FN int kapi_net_status (char *ip, unsigned cap);
KAPI_FN int kapi_tcp_connect (const char *host, unsigned port);
KAPI_FN int kapi_tcp_send (int sock, const void *buf, unsigned len);
KAPI_FN int kapi_tcp_recv (int sock, void *buf, unsigned len);
KAPI_FN void kapi_tcp_close (int sock);
// TCP server side (ABI v37). tcp_listen: listening handle >=0 / <0 (-6 port in use).
// tcp_accept: BLOCKS until a peer connects; connected handle >=0 + peer IP / <0.
KAPI_FN int kapi_tcp_listen (unsigned port);
KAPI_FN int kapi_tcp_accept (int listen_sock, char *ip, unsigned cap);

// Remote screen (ABI v38). screen_grab: composite the screen into dst (w*h 0x00RRGGBB,
// w/h = kapi_screen_size) -> 1 / 0, or 2 = unchanged since the previous grab into the same
// dst (left as is: skip the diff). inject_pointer/inject_key: input as if from the USB
// mouse (buttons bit0 L, bit1 R, bit2 M; wheel = notches) / keyboard (cooked string).
KAPI_FN int kapi_screen_grab (unsigned *dst, int w, int h);
KAPI_FN void kapi_inject_pointer (int x, int y, unsigned buttons, int wheel);
KAPI_FN void kapi_inject_key (const char *keys);

// System menu bar (ABI v39). set_menu: declare this app's menus (spec lines "M<title>",
// "I<id>\t<label>\t<shortcut>", "-") + the GUI_EVENT_MENU handler -- apps normally use
// uikit::Menu. get_menu / menu_command: for the menu-bar app (active window's spec+title ->
// change serial, 0 = none; send item id, MENU_QUIT closes the active app).
KAPI_FN int kapi_set_menu (const char *spec, gui_handler h);
KAPI_FN unsigned kapi_get_menu (char *buf, unsigned cap, char *title, unsigned tcap);
KAPI_FN int kapi_menu_command (int id);

// Named IPC services (ABI v40): ipc_register -> become service `name` (1 / 0 taken);
// ipc_lookup -> its pid or 0. Messages go through kapi_mailbox_send / _recv (<= 512 B).
KAPI_FN int kapi_ipc_register (const char *name);
KAPI_FN int kapi_ipc_lookup (const char *name);
// System clipboard (ABI v40): see clipboard.h for the helpers. Types:
#define CLIP_TEXT	1		// plain text
#define CLIP_FILES	2		// file/folder paths, '\n'-separated (copied)
#define CLIP_FILES_CUT	3		// same, cut (the paste moves them)
KAPI_FN int kapi_clipboard_set (int type, const void *d, unsigned n);
KAPI_FN int kapi_clipboard_get (int *type, void *b, unsigned cap, unsigned *serial);
// Window opacity 0..255 (ABI v40; fades) and end of session (0 = halt, 1 = restart).
KAPI_FN void kapi_set_window_alpha (int a);
#define SHUTDOWN_HALT		0
#define SHUTDOWN_RESTART	1
KAPI_FN void kapi_shutdown (int mode);

// Full-screen apps (ABI v41): fullscreen_begin -> a screen-sized 0x00RRGGBB back buffer
// (w/h filled); the desktop stops drawing and all input (screen coords) comes to you.
// Draw, present_fb to show it; fullscreen_end (or exiting) gives the desktop back.
KAPI_FN unsigned * kapi_fullscreen_begin (int *w, int *h);
KAPI_FN void kapi_present_fb (void);
KAPI_FN void kapi_fullscreen_end (void);

// Drag & drop (ABI v42). drag_begin: while the left button is held (from a pointer-move
// handler, after a few pixels of motion), drag (type, data <= 4 KB) with `label` on the
// cursor -> 1 / 0. The target gets GUI_EVENT_DROP and reads the payload with drag_data
// (copies <= cap, returns the full length); the source gets GUI_EVENT_DRAG_DONE.
// get_modifiers: MOD_* held now; inject_modifiers: set them (vncd).
KAPI_FN int kapi_drag_begin (int type, const void *data, unsigned len, const char *label);
KAPI_FN int kapi_drag_data (int *type, void *buf, unsigned cap);
KAPI_FN unsigned kapi_get_modifiers (void);
KAPI_FN void kapi_inject_modifiers (unsigned mods);

// Network tools (ABI v43). net_ping: one ICMP echo (RTT in us, or -1 down / -3 unresolved /
// -4 timeout / -5 send failed). net_resolve: DNS -> dotted IP (1/0). net_info: netstat text.
KAPI_FN int kapi_net_ping (const char *host, unsigned seq, unsigned timeout_ms, char *ip, unsigned cap);
KAPI_FN int kapi_net_resolve (const char *host, char *ip, unsigned cap);
KAPI_FN int kapi_net_info (char *buf, unsigned cap);

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
KAPI_FN int kapi_vfs_register (const char *prefix);
KAPI_FN int kapi_vfs_next (struct kapi_vfs_req *req, int blocking);
KAPI_FN int kapi_vfs_req_data (unsigned id, void *buf, unsigned cap, unsigned offset);
KAPI_FN int kapi_vfs_reply (unsigned id, int status, const void *data, unsigned len);

// Wi-Fi scan (ABI v45): the access points around, strongest first (struct kapi_wlan_ap:
// ssid, bssid, security WLAN_SEC_OPEN/WEP/WPA/WPA2, channel, freq MHz, level dBm,
// connected). Takes ~3 s (blocks the caller); returns how many (0 = none / no Wi-Fi).
KAPI_FN int kapi_wlan_scan (struct kapi_wlan_ap *out, int max);

// Sound (ABI v46), on the 3.5 mm jack. First acquire the output (1 ok, 0 another app has
// it, -1 no audio); only the owner plays, and its release -- or its exit -- silences it.
//   kapi_sound_start (voice 0..15, milli-Hz (440 Hz = 440000), SOUND_SQUARE/SINE/TRIANGLE/
//                     SAW/NOISE, volume 0..255): the note plays until kapi_sound_stop (voice)
//                     (-1 = all). Short attack / release ramps: no clicks.
//   kapi_sound_write (s16 L/R frames at SOUND_RATE, n): a PCM stream mixed with the voices;
//                     returns the frames taken (0 = full, retry later) -- audio players.
KAPI_FN int kapi_sound_acquire (void);
KAPI_FN void kapi_sound_release (void);
// (RETIRED 2026-10-05: the voices are AudioKit's -- ak_fm_start / ak_fm_stop / ak_fm_instrument,
// audiokit/audiokit.h --; these three answer -1)
KAPI_FN int kapi_sound_start (int voice, unsigned millihz, int wave, int volume);
KAPI_FN int kapi_sound_stop (int voice);
KAPI_FN int kapi_sound_write (const short *frames, unsigned n);
KAPI_FN int kapi_sound_status (unsigned *rate, unsigned *free_frames, unsigned *owner);
// FM (ABI v47): give a voice a 2-operator FM instrument (struct kapi_fm_instrument, see
// kern/kapi_abi.h), then kapi_sound_start (voice, milliHz, SOUND_FM, volume) keys it on and
// kapi_sound_stop (voice) keys it off (its release rate fades it out).
KAPI_FN int kapi_sound_instrument (int voice, const struct kapi_fm_instrument *ins);

// Held keys (ABI v48), for games (key events only report presses): 1 while `key` is held
// and this window has the keyboard. key = KEY_UP/DOWN/LEFT/RIGHT, KEY_ENTER, 27 (Esc), ' ',
// 'a'..'z' (US position of the key on a USB keyboard), '0'..'9'. Returns 0 on an older kernel.
KAPI_FN int kapi_key_held (int key);
KAPI_FN void kapi_inject_key_held (int key, int down);
// Run a program under another process name (ABI v49): a runner running an app is named
// after the app (its window, list_windows, raise_app). See launch.h.
KAPI_FN int kapi_exec_as (const char *path, const char *args, const char *name);
// USB gamepads (v50): the raw state of pad 0..3 (1 = there); user/gamepad.h maps its buttons.
KAPI_FN int kapi_pad_state (int index, struct kapi_pad *out);
// App cores (v51): acquire core 2 or 3, run a function of this app there (no kapi call and
// no malloc in it: compute, and exchange data through memory), poll its state, release.
KAPI_FN int kapi_core_acquire (void);
KAPI_FN int kapi_core_run (int core, void (*fn) (void *), void *arg, void *stack_top);
KAPI_FN int kapi_core_state (int core);
KAPI_FN void kapi_core_release (int core);
// The V3D GPU (v52): gpu_info brings it up (1 = usable, buf says what / why not); gpu_draw
// renders a depth-tested triangle list (NDC positions + RGBA8 colours) into pixels.
KAPI_FN int kapi_gpu_info (char *buf, unsigned cap);
KAPI_FN int kapi_gpu_draw (const struct kapi_gpu_vertex *v, unsigned n, unsigned clear, unsigned *pixels, int w, int h, int stride);
// The GPU's full pipeline (v53): textures (0xAARRGGBB; handle < 0 = a new one, pixels 0 =
// free), and gpu_render: batches (each: its vertices, matrix, texture, blending, depth test,
// culling) drawn in one frame into f->pixels. See kern/kapi_abi.h.
KAPI_FN int kapi_gpu_texture (int handle, const unsigned *pixels, int w, int h, int stride);
KAPI_FN int kapi_gpu_render (const struct kapi_gpu_frame *f, const struct kapi_gpu_vertex3 *v, unsigned nv,
				    const struct kapi_gpu_batch *b, unsigned nb);
// Full screen straight into the displayed framebuffer (v55; after kapi_fullscreen_begin):
// its pixels (stride in pixels), or 0 (keep drawing into the back buffer + present_fb).
KAPI_FN unsigned * kapi_fullscreen_direct (int *w, int *h, int *stride);
// The windows as objects (v56, the remote desktop rdpd): list (bottom to top), a client
// rectangle's pixels, to the front, close.
KAPI_FN int kapi_win_list (struct kapi_win_info *out, int max);
KAPI_FN int kapi_win_read (unsigned id, int part, int x, int y, int w, int h, unsigned *dst, int stride);
KAPI_FN int kapi_win_raise (unsigned id);
KAPI_FN int kapi_win_close (unsigned id);
// The read position of an opened file (v57): 0, or -1 (an older kernel, a file not seekable).
KAPI_FN int kapi_seek (void *h, unsigned long long pos);
// Writable + executable memory for generated code, a JIT (v58): 0 on an older kernel / full.
// After writing code: clean the D-cache / invalidate the I-cache over it (__builtin___clear_cache).
KAPI_FN void * kapi_code_alloc (unsigned long size);
// A file's whole size (v59: over 4 GB on exFAT; an older kernel: fsize).
KAPI_FN unsigned long long kapi_fsize64 (void *h);
// (v71) vol_info: a volume's room -- "SD:", "SD1:", "RAM:" (the RAM volume: files in memory until
// the Pi restarts), a path on it -> 0 and *out (total / free / used bytes, its type, KAPI_VOL_RAM),
// -1 no such volume / an older kernel. (RAM: paths work with every file call above from v71.)
KAPI_FN int kapi_vol_info (const char *path, struct kapi_vol_info *out);
// (v73) The event pump's kernel half -- what kapi_pump_events does, step by step, for a pump of the
// app's own (a protected app's table runs its pump that way, kern/el0.h). pop_event: the window's
// next event -> 1 (*ev; its handler NOT called), 0 none; event_mods: what kapi_get_modifiers says
// while a key handler runs (ev->mods), returns the previous value to put back; pop_post: the next
// kapi_post call -> 1 (*p, not run), 0 none; pump_sleep: kapi_pump_wait without the pump (-> how
// many are pending). An older kernel: 0 / 0xFFFFFFFF / 0 / 0 (no sleep).
KAPI_FN int kapi_pop_event (struct kapi_event *ev);
KAPI_FN unsigned kapi_event_mods (unsigned mods);
KAPI_FN int kapi_pop_post (struct kapi_posted *p);
KAPI_FN int kapi_pump_sleep (unsigned timeout_ms);
// (v74) A process's system calls (pid 0: the caller): the total, the rate per second, the 8 table
// slots most called (user/BinUtils/kapi_names.h names them) -> 0, -1 no such process / older kernel, -2 bad pointer.
KAPI_FN int kapi_proc_stats (int pid, struct kapi_syscall_stats *out);
// (v75) The POSIX layer's kernel half (docs/POSIX-PLAN.md; the structures and KAPI_* values in
// kern/kapi_abi.h). Every call returns >= 0 on success, -KAPI_Exxx (newlib's errno value) on
// failure, and -KAPI_ENOSYS on an older kernel or until the call is implemented. libonyxposix
// (user/libc/posix) wraps them as the POSIX functions.
// v75 WP-MEM: virtual memory (vm_map = mmap, vm_protect = mprotect, vm_advise = madvise...),
// threads with their stack size and TLS (struct kapi_thread_attr).
KAPI_FN long long kapi_vm_map (unsigned long long addr, unsigned long long len, unsigned prot, unsigned flags);
KAPI_FN int kapi_vm_unmap (unsigned long long addr, unsigned long long len);
KAPI_FN int kapi_vm_protect (unsigned long long addr, unsigned long long len, unsigned prot);
KAPI_FN int kapi_vm_advise (unsigned long long addr, unsigned long long len, int advice);
KAPI_FN int kapi_vm_query (unsigned long long addr, struct kapi_vm_region *out);
KAPI_FN int kapi_vm_stats (int pid, struct kapi_vm_stats *out);
KAPI_FN int kapi_thread_create_ex (const struct kapi_thread_attr *attr);
KAPI_FN int kapi_thread_info (int tid, struct kapi_thread_info *out);
// v75 WP-FILE/PROC: open files with 64-bit offsets (pread / pwrite), stat, unlink / rename of open
// files, dir_read (255-character names), non-blocking pipe writes; spawn_ex / proc_wait,
// argv / environment blocks, getpid, clock_info (CNTPCT + UTC sample), sleep_us.
// file_read / file_write: off -1 = at the handle's offset (advanced; O_APPEND writes at the end),
// else a pread / pwrite; a write past the end fills the gap with zeros. A file unlinked or renamed
// while open stays usable through its handles. proc_wait (h, KAPI_WAIT_NOHANG, &st) on a running
// child -> 0 with st.pid set and st.reason -1 (spawn_ex returns once the child has its pid).
// get_argv / get_env (0, 0) -> the block's size. docs/02 §8 "v75: files and processes".
KAPI_FN long long kapi_file_open (const char *path, unsigned flags, unsigned mode);
KAPI_FN long long kapi_file_read (long long h, void *buf, unsigned long long len, long long off);
KAPI_FN long long kapi_file_write (long long h, const void *buf, unsigned long long len, long long off);
KAPI_FN long long kapi_file_seek (long long h, long long off, int whence);
KAPI_FN int kapi_file_truncate (long long h, long long size);
KAPI_FN int kapi_file_sync (long long h);
KAPI_FN int kapi_file_stat (long long h, struct kapi_stat *out);
KAPI_FN int kapi_file_close (long long h);
KAPI_FN int kapi_path_stat (const char *path, struct kapi_stat *out);
KAPI_FN int kapi_path_unlink (const char *path, unsigned flags);
KAPI_FN int kapi_path_mkdir (const char *path, unsigned mode);
KAPI_FN int kapi_path_rename (const char *from, const char *to);
KAPI_FN int kapi_path_utime (const char *path, long long mtime);
KAPI_FN int kapi_dir_read (void *dir, struct kapi_dirent2 *out);
KAPI_FN int kapi_stream_write_nb (void *h, const void *buf, unsigned len);
KAPI_FN long long kapi_spawn_ex (const struct kapi_spawn_attr *a);
KAPI_FN int kapi_proc_wait (void *proc, unsigned flags, struct kapi_proc_status *out);
KAPI_FN int kapi_get_argv (char *buf, unsigned cap);
KAPI_FN int kapi_get_env (char *buf, unsigned cap);
KAPI_FN int kapi_getpid (int which);
KAPI_FN int kapi_clock_info (struct kapi_clock_info *out);
KAPI_FN int kapi_sleep_us (unsigned long long us);
// v75 WP-NET: BSD sockets (IPv4 TCP / UDP: non-blocking connect, accept, MSG_PEEK...) and poll
// over sockets, streams and files.
KAPI_FN int kapi_sock_open (int type, unsigned flags);
KAPI_FN int kapi_sock_connect (int s, const struct kapi_sockaddr *to);
KAPI_FN int kapi_sock_bind (int s, const struct kapi_sockaddr *addr);
KAPI_FN int kapi_sock_listen (int s, int backlog);
KAPI_FN int kapi_sock_accept (int s, struct kapi_sockaddr *peer, unsigned flags);
KAPI_FN long long kapi_sock_send (int s, const void *buf, unsigned long long len, unsigned flags, const struct kapi_sockaddr *to);
KAPI_FN long long kapi_sock_recv (int s, void *buf, unsigned long long len, unsigned flags, struct kapi_sockaddr *from);
KAPI_FN int kapi_sock_shutdown (int s, int how);
KAPI_FN int kapi_sock_close (int s);
KAPI_FN int kapi_sock_getopt (int s, int opt, int *value);
KAPI_FN int kapi_sock_setopt (int s, int opt, int value);
KAPI_FN int kapi_sock_name (int s, int peer, struct kapi_sockaddr *out);
KAPI_FN int kapi_poll (struct kapi_pollfd *fds, unsigned n, int timeout_ms);
// (v76, WP-IPC: docs/POSIX-PLAN.md §14) Local sockets (sock_pair: STREAM / SEQPACKET / DGRAM;
// numbers >= KAPI_SOCK_LOCAL_BASE, served by every sock_* call and poll), messages carrying handles
// (sock_sendmsg / sock_recvmsg: struct kapi_msghdr, struct kapi_handle_xfer), shared memory objects
// (shm_create / shm_open / shm_unlink / shm_ctl / shm_map), handle_close, spawn_ex2 (handles given
// to the child) and get_handles (the child's side). -KAPI_ENOSYS on an older kernel.
KAPI_FN int kapi_sock_pair (int type, unsigned flags, int *sv);
KAPI_FN long long kapi_sock_sendmsg (int s, const struct kapi_msghdr *m, unsigned flags);
KAPI_FN long long kapi_sock_recvmsg (int s, struct kapi_msghdr *m, unsigned flags);
KAPI_FN long long kapi_shm_create (unsigned long long size, unsigned flags);
KAPI_FN long long kapi_shm_open (const char *name, unsigned oflags, unsigned mode);
KAPI_FN int kapi_shm_unlink (const char *name);
KAPI_FN long long kapi_shm_ctl (long long h, int op, unsigned long long arg);
KAPI_FN long long kapi_shm_map (long long h, unsigned long long addr, unsigned long long len, unsigned prot,
				      unsigned flags, unsigned long long off);
KAPI_FN int kapi_handle_close (long long h);
KAPI_FN long long kapi_spawn_ex2 (const struct kapi_spawn_attr *a, const struct kapi_handle_xfer *handles, unsigned n);
KAPI_FN int kapi_get_handles (struct kapi_handle_xfer *out, unsigned cap);
// (v77) Program images (docs/02 section 7): a program is loaded once and shared by its processes;
// its key is its canonical path (lower case, the volume first). image_preload: loaded ahead and
// kept (returns at once; a run of that path then reads nothing from the card); image_unload: its
// pin and its name taken away (freed with its last process); image_list: the live images (path
// 0), or the one a run of path would map (1 / 0). -KAPI_ENOSYS on an older kernel.
KAPI_FN int kapi_image_preload (const char *path);
KAPI_FN int kapi_image_unload (const char *path);
KAPI_FN int kapi_image_list (const char *path, struct kapi_image_info *out, unsigned cap);
// (v79) What the running kernel is: "key value" lines (name, abi, built, rev, machine, model, ram;
// keys may be added) -> the text's length; -KAPI_ENOSYS (and "") on an older kernel. /bin/uname.
KAPI_FN int kapi_kernel_info (char *buf, unsigned cap);
// (v80) The cores: each one's role (KAPI_CORE_*), the microseconds it was busy, an app core's owner
// -> 0; -KAPI_ENOSYS (out zeroed) on an older kernel. Two reads make a load.
KAPI_FN int kapi_cpu_stats (struct kapi_cpu_stats *out);
// (v80) The bytes pid's sockets received and sent, its open sockets (pid 0: every process's) -> 0;
// -KAPI_ENOSYS (out zeroed) on an older kernel.
KAPI_FN int kapi_net_stats (int pid, struct kapi_net_stats *out);
// (v81) The pointer's shape over this window (KAPI_CURSOR_*) -> the shape it had; -1 on an older
// kernel (the arrow stays). uikit: uk_cursor, from a widget's onMouse.
KAPI_FN int kapi_set_cursor (int shape);
// (v82) This window can be resized by its frame (min_w x min_h: its smallest client area) -> 0; -1
// on an older kernel, or for a borderless / fixed window. At the release of a drag the pointer
// handler gets GUI_EVENT_WINRESIZE: GUI_WINRESIZE_X / _Y (the frame's new top left), _W / _H (the
// client area's new size) of its value; the app applies them. uikit: Root::setResizable.
#define GUI_EVENT_WINRESIZE	20
#define GUI_WINRESIZE_X(v)	((int) (short) ((unsigned long long) (v) >> 48))
#define GUI_WINRESIZE_Y(v)	((int) (short) ((unsigned long long) (v) >> 32))
#define GUI_WINRESIZE_W(v)	((int) (((unsigned long long) (v) >> 16) & 0xFFFF))
#define GUI_WINRESIZE_H(v)	((int) ((unsigned long long) (v) & 0xFFFF))
KAPI_FN int kapi_win_resizable (int on, int min_w, int min_h);
// (v83) A shared library (docs/SHARED-LIBS-PLAN.md): "uikit" is SD:/lib/uikit.so, anything with a '/'
// or a ':' a path -> its export table (unsigned version, size; int (*init) (const TLibImports *);
// then its entries), mapped in this process until it ends; 0 with *err = -KAPI_E* (-KAPI_ENOTSUP:
// the library is older than min_version; -KAPI_ENOSYS on an older kernel). Apps do not call this:
// the library's bind object does, before main (user/lib.h).
KAPI_FN const void * kapi_lib_open (const char *name, unsigned min_version, int *err);
// (v84) The sound's output: KAPI_SND_OUT_AUTO / _JACK / _USB / _HDMI (-1: only ask) -> what plays
// now, what is asked and which outputs are there (KAPI_SND_OUT_NOW / _ASKED / _HAS of the result);
// -KAPI_ENOSYS on an older kernel (the jack only). The Sound applet; kept in SD:/etc/sound.ini.
KAPI_FN int kapi_sound_output (int out);
// (v85) the sound's mixer: the programs that play (each has a channel), a channel's volume and mute
KAPI_FN int kapi_sound_clients (struct kapi_sound_client *out, int max);
KAPI_FN int kapi_sound_client_volume (unsigned pid, int volume, int mute);
// (v73) Is this process protected (EL0, kern/el0.h)? Its table's memcpy is then user code, next
// to the table, instead of the kernel's.
KAPI_FN int kapi_is_protected (void);
// the master volume 0..10 and mute (-1: keep) -> volume | 0x100 if muted (older kernel: 10, not muted)
KAPI_FN int kapi_sound_volume (int volume, int mute);
// wpa_supplicant.conf read again + DHCP again, no reboot (-1: none / older kernel)
KAPI_FN int kapi_wlan_reconnect (void);
// v61: draws with the app's own QPU shaders (user/v3d/qpu.h builds them): gpu_program makes /
// replaces / frees (p = 0) a program, gpu_render2 draws batches of them (kapi_abi.h).
KAPI_FN int kapi_gpu_program (int handle, const struct kapi_gpu_program *p);
KAPI_FN int kapi_gpu_render2 (const struct kapi_gpu_frame *f, const float *v, unsigned nv, unsigned stride,
				    const struct kapi_gpu_batch2 *b, unsigned nb, const unsigned *uni, unsigned nuni);
// (v62) gpu_render3: the batches' vertices where they are (kapi_gpu_batch3: off, stride), x / y
// framed by the kernel on the way (view: x' = v0 x + v1 w, y' = v2 y + v3 w; 0: as they are)
KAPI_FN int kapi_gpu_render3 (const struct kapi_gpu_frame *f, const float *v, unsigned nfloats,
				    const struct kapi_gpu_batch3 *b, unsigned nb, const unsigned *uni, unsigned nuni, const float *view);
// (v63) gpu_vbuf: memory the GPU reads too -- gpu_render3 draws vertices there in place (framed in
// place: view 0 to draw them again; the clipped triangles into the room past nfloats) -> 0 none
KAPI_FN void * kapi_gpu_vbuf (unsigned bytes);
// (v70) gpu_texture_rect: a rectangle x, y, w x h of a texture's pixels (0xAARRGGBB) replaced, the
// rest kept -> 0, -1 no GPU / older kernel, -2 bad arguments. (Also v70: KAPI_GPU_F_ALPHA, a frame's
// target keeping its alpha; the GPU compositing service over these: user/gpucomp/gpucomp.h.)
KAPI_FN int kapi_gpu_texture_rect (int handle, int x, int y, int w, int h, const unsigned *pixels, int stride);
// (v64) the windows of the modernised CDE desktop: minimise one (0: mine; back with win_raise /
// raise_app), my window's place and size and the work area (the screen less the menu bar and the
// dock), resize my window letting its canvas grow (*stride: its pixels a row; redraw everything,
// the frame too) -> 0 on an older kernel / no memory.
KAPI_FN int kapi_win_minimise (unsigned id);
KAPI_FN int kapi_win_geometry (struct kapi_win_geom *out);
KAPI_FN unsigned * kapi_resize_window2 (int w, int h, int *stride);
// (v65) the workspaces (virtual desktops): kapi_desk (set, count) shows desk `set` (-1 keeps it) and
// sets how many there are (0 keeps it) -> the current desk | the count << 8 | a change counter << 16
// (KAPI_DESK_CUR / _COUNT / _GEN); an older kernel: one desk. kapi_win_desk: window id (0: mine) to
// desk n (-1: every desk; -2: only asked) -> its desk, -3 none.
#define KAPI_DESK_CUR(i)	((i) & 0xFF)
#define KAPI_DESK_COUNT(i)	(((i) >> 8) & 0xFF)
#define KAPI_DESK_GEN(i)	(((unsigned) (i) >> 16) & 0x7FFF)
KAPI_FN int kapi_desk (int set, int count);
KAPI_FN int kapi_win_desk (unsigned id, int n);
// (v66) screen_set: the screen's resolution now (640 x 480 .. 2560 x 1600, w even); every window
// kept on the screen and sent GUI_EVENT_DISPLAY_RESIZE -> 0; -1 out of bounds; -2 not now (a
// full-screen app...); -3 the firmware refused it (old size kept); -4 an older kernel. Not kept
// across a reboot (SD:/cmdline.txt width= / height= are).
KAPI_FN int kapi_screen_set (int w, int h);

// (v67) Threads: more tasks in this process (its memory, window, files, sockets), preempted like
// it; the process ends with its main thread (tid 1). Timeouts in ms: 0 = only try,
// KAPI_WAIT_FOREVER = none. An older kernel: -3 / nothing (a single thread).
// kapi_thread_create (fn, arg, stack_size (0: 256 KB), name (0: its tid)) -> tid >= 2, -1 no
// memory, -2 too many (32); fn's return value is its exit code. kapi_thread_join -> 0 (*code),
// -1 timeout, -2 no such thread, -3 itself.
// The GUI belongs to the thread that pumps (the main one): a worker hands its result over with
// kapi_post (fn, ctx, value), which the pump (kapi_pump_events / kapi_pump_wait /
// kapi_wait_for_exit) runs on its thread. kapi_pump_wait (ms) sleeps until an event / a post /
// the close box, then pumps.
KAPI_FN int kapi_thread_create (int (*fn) (void *), void *arg, unsigned stack_size, const char *name);
KAPI_FN void kapi_thread_exit (int code);
KAPI_FN int kapi_thread_join (int tid, unsigned timeout_ms, int *code);
KAPI_FN int kapi_thread_self (void);
// Synchronisation objects (handles > 0; 256 per process). mutex: recursive, released if its
// owner thread ends; lock -> 0, -1 timeout, -2 bad handle. event: manual reset (stays set until
// reset) or auto (a wait takes it); wait -> 0, -1, -2. barrier: count threads meet; wait -> 1 for
// the last one in, 0 the others. kapi_sync_close frees any of them.
KAPI_FN int kapi_mutex_create (void);
KAPI_FN int kapi_mutex_lock (int h, unsigned timeout_ms);
KAPI_FN int kapi_mutex_unlock (int h);
KAPI_FN int kapi_event_create (int manual_reset, int initial);
KAPI_FN int kapi_event_set (int h);
KAPI_FN int kapi_event_reset (int h);
KAPI_FN int kapi_event_wait (int h, unsigned timeout_ms);
KAPI_FN int kapi_barrier_create (unsigned count);
KAPI_FN int kapi_barrier_wait (int h);
KAPI_FN int kapi_sync_close (int h);
KAPI_FN int kapi_post (void (*fn) (void *ctx, long value), void *ctx, long value);
KAPI_FN int kapi_pump_wait (unsigned timeout_ms);

// A user-space lock between this process's threads (the allocators, newlib): an atomic swap,
// and a yield while another thread holds it -- nothing to create, a zeroed int is free. Not
// recursive. (Threads all run on core 0; the swap is still an exclusive load / store pair,
// since the timer may preempt a thread anywhere in its own code.)
// (The PC builds of the apps -- the desktop simulator, the NetSurf bench -- get the compiler's
// atomics and no cores.)
static inline int kapi__xchg (volatile int *p, int v)
{
#if defined(__aarch64__)
	int old; unsigned fail;
	__asm__ volatile ("1: ldaxr %w0, [%2]\n\tstxr %w1, %w3, [%2]\n\tcbnz %w1, 1b"
			  : "=&r" (old), "=&r" (fail) : "r" (p), "r" (v) : "memory");
	return old;
#else
	return __atomic_exchange_n (p, v, __ATOMIC_ACQUIRE);
#endif
}
// The core this code runs on (0: the main one; 2, 3: an app core). The kernel publishes it in
// TPIDRRO_EL0 (v73, kern/el0.h): every app runs at EL0, where MPIDR_EL1 may not be read (the app
// would be killed) -- never read MPIDR_EL1 in an app.
static inline unsigned kapi__core (void)
{
#if defined(__aarch64__)
	unsigned long m;
	__asm__ volatile ("mrs %0, tpidrro_el0" : "=r" (m));
	return (unsigned) (m & 3);
#else
	return 0;
#endif
}
#if defined(__aarch64__)
static inline void kapi__pause (void) { __asm__ volatile ("yield"); }
static inline void kapi__release (volatile int *l) { __asm__ volatile ("stlr wzr, [%0]" :: "r" (l) : "memory"); }
static inline void kapi__dmb (void) { __asm__ volatile ("dmb ish" ::: "memory"); }
#else
static inline void kapi__pause (void) { }
static inline void kapi__release (volatile int *l) { __atomic_store_n (l, 0, __ATOMIC_RELEASE); }
static inline void kapi__dmb (void) { __atomic_thread_fence (__ATOMIC_SEQ_CST); }
#endif
// (on an app core -- kapi_core_run's code, which makes no kapi call -- it spins instead)
KAPI_FN void kapi_lock (volatile int *l);
static inline void kapi_unlock (volatile int *l) { kapi__release (l); }

// (v68) A futex. kapi_wait_word (addr, expected, timeout_ms) sleeps while *addr == expected ->
// 0 woken / the value differs, 1 timeout (0 ms: only check), -1 a bad address (not 4-byte
// aligned, unmapped); may wake spuriously: loop on the condition. kapi_wake_word (addr) wakes
// its sleepers -> how many. Works across processes on a shared surface (keyed by the physical
// address). A word changed by an app core (no kapi there) is noticed at the next 10 ms tick.
// kapi_thread_priority (tid 0 self / 1 main / >= 2, prio 0 normal / 1 real time / -1 ask) ->
// the previous one: a real-time thread (an audio pump) runs first whenever it is ready, as long
// as it sleeps before its time slice ends. An older kernel: -1 / -2.
KAPI_FN int kapi_wait_word (volatile unsigned *addr, unsigned expected, unsigned timeout_ms);
KAPI_FN int kapi_wake_word (volatile unsigned *addr);
KAPI_FN int kapi_thread_priority (int tid, int prio);

// (v68) USB MIDI input: class-compliant devices (keyboards, interfaces), found when plugged
// in, even later. kapi_midi_read (ev, max) takes up to max queued events (struct
// kapi_midi_event: time_us, cable, status, data1, data2, device, length), oldest first, never
// waits -> how many; one queue for the system (256 events). kapi_midi_devices () -> attached.
KAPI_FN int kapi_midi_read (struct kapi_midi_event *ev, int max);
KAPI_FN int kapi_midi_devices (void);

// (v69) kapi_screen_native (&w, &h): the monitor's own resolution, from its EDID -> 1, 0 unknown.
// kapi_set_timezone (minutes): the local time's offset from UTC at once (-720 .. 840) -> 1 ok.
// WIN_FLAG_FIXED (a window's flags): not movable, no minimise / maximise / close, kept centred.
KAPI_FN int kapi_screen_native (int *w, int *h);
KAPI_FN int kapi_set_timezone (int minutes);
// The kernel's microsecond clock (CTimer::GetClockTicks: the ARM counter, same formula), the
// time base of kapi_midi_event.time_us. No kapi call: an app core may read it too.
static inline unsigned kapi_clock_us (void)
{
#ifdef __aarch64__
	unsigned long c, f;
	__asm__ volatile ("isb\n\tmrs %0, cntpct_el0\n\tmrs %1, cntfrq_el0" : "=r" (c), "=r" (f));
	return (unsigned) (c * 1000000UL / f);
#else
	return kapi_get_ticks () * 10000u;		// (the PC's stand-in kernel: its 100 Hz ticks)
#endif
}

// (v68) Low-latency sound, for the sound owner (kapi_sound_acquire). kapi_sound_config (chunk
// frames 64..1024 (0: 1024), chunks ahead 1..4 (0: 4)) -> the latency now in frames ((ahead + 1)
// x chunk: 1024 x 4 ~116 ms by default, 256 x 2 ~17 ms), -1 not the owner / older kernel. Keep
// the sound_write stream shallow too (it holds up to 0.5 s): write when kapi_sound_status's free
// frames show little is queued. The defaults come back when the owner releases the output.
// kapi_sound_map () -> the mapped PCM ring (struct kapi_sound_ring, kern/kapi_abi.h), mixed
// until the owner releases the output, or 0. It is plain memory: an app core (kapi_core_run)
// fills it with kapi_sound_ring_write, which makes no kapi call.
KAPI_FN int kapi_sound_config (int chunk_frames, int ahead);
KAPI_FN struct kapi_sound_ring * kapi_sound_map (void);
// Frames the ring can take now.
static inline unsigned kapi_sound_ring_free (const struct kapi_sound_ring *r)
	{ return r->frames - (r->wr - r->rd); }
// Copy up to n s16 stereo frames into the ring -> the frames taken (no kapi call: app cores too).
static inline unsigned kapi_sound_ring_write (struct kapi_sound_ring *r, const short *frames, unsigned n)
{
	unsigned wr = r->wr, room = r->frames - (wr - r->rd), mask = r->frames - 1;
	if (n > room) n = room;
	for (unsigned i = 0; i < n; i++)
	{
		unsigned j = ((wr + i) & mask) * 2;
		r->data[j] = frames[i * 2]; r->data[j + 1] = frames[i * 2 + 1];
	}
	kapi__dmb ();					// (the frames before the index)
	r->wr = wr + n;
	return n;
}

// Reboot the machine (ABI v25). Does not return. Use to apply settings the kernel
// only reads at boot -- e.g. after wpaconf rewrites SD:/etc/wpa_supplicant.conf.
KAPI_FN void kapi_reboot (void);

// 1 if a USB keyboard is attached & ready, else 0 (ABI v26). The `keyb` tool polls
// this at boot before applying a layout (it can run before USB enumeration finishes).
KAPI_FN int kapi_kbd_ready (void);

// Load a keyboard layout from a .kmap blob (ABI v27): "OKM1" + u16 rows + u16 cols +
// table. The kernel copies it; the caller frees `data`. 1 on success. See /etc/keymaps.
KAPI_FN int kapi_set_keymap_data (const char *name, const void *data, unsigned len);

// Hardware RNG (ABI v30): fill buf[len] with random bytes from the Pi's HW RNG. For
// cryptographic seeding -- the TLS entropy source uses it. Returns bytes written.
KAPI_FN int kapi_random (void *buf, unsigned len);

// Shell surfaces (ABI v35): shared 0x00RRGGBB pixel buffers for the activity-shell
// compositor. The shell creates one sized to a viewport, passes its id to an app; both
// map it (same physical frames, own VA), the app draws + presents, the shell composites.
KAPI_FN int kapi_surface_create (int w, int h);
KAPI_FN unsigned * kapi_surface_map (int id);
KAPI_FN int kapi_surface_size (int id, int *w, int *h);
KAPI_FN void kapi_surface_present (int id);
KAPI_FN int kapi_surface_destroy (int id);

// Activity-shell IPC (ABI v35): the kernel routes opaque {from_pid,type,bytes} messages
// between per-process mailboxes. A user compositor calls kapi_register_shell to become
// THE shell; apps post to it with kapi_shell_request; the shell replies / pushes async
// events with kapi_mailbox_send; both drain with kapi_mailbox_recv (fills *from_pid /
// *type, returns the payload length, or -1 if empty; blocking != 0 waits).
KAPI_FN int kapi_register_shell (void);
KAPI_FN int kapi_shell_request (int type, const void *in, unsigned len);
KAPI_FN int kapi_mailbox_send (int target_pid, int type, const void *in, unsigned len);
KAPI_FN int kapi_mailbox_recv (int *from_pid, int *type, void *buf, unsigned cap, int blocking);

// Memory primitives (ABI v36): the kernel's (Circle's) memset/memcpy/memmove. Real
// symbols (AppKit's; weak ones in a program that has the bodies inline) so the linker can see
// them: the freestanding app Makefiles alias the C names onto them
// (-Wl,--defsym,memset=kapi_memset ...), which resolves the calls GCC emits on its own
// (array/struct init and copies).
#ifndef _WIN32		// (the Windows build of the apps, pc/Koton: its C library's; no weak symbols there)
#ifdef __cplusplus
extern "C" {
#endif
void *kapi_memset (void *dst, int c, unsigned long n);
void *kapi_memcpy (void *dst, const void *src, unsigned long n);
void *kapi_memmove (void *dst, const void *src, unsigned long n);
#ifdef __cplusplus
}
#endif
#endif

// Friendly aliases used by the demos.
static inline unsigned *create_window (int w, int h, const char *t) { return kapi_create_window (w, h, t); }
static inline void      present (void)             { kapi_present (); }
static inline unsigned  get_ticks (void)           { return kapi_get_ticks (); }
static inline void      msleep (unsigned ms)       { kapi_msleep (ms); }
static inline void      pump_events (void)         { kapi_pump_events (); }
static inline int       should_exit (void)         { return kapi_should_exit (); }

// ---- AppKit's small services (appkit_lib.inc; they were user/applib.h until 2026-10-05) --------------
// For every program, with or without a C library.

// Strings. ax_strcat: src appended to dst at *pos (advanced), never past cap, always NUL-terminated.
// ax_app_path: "SD:apps/<name><suffix>" (a suffix: ".app/icon.bmp"). ax_streq: 1 = the same.
// ax_itoa: a signed int in decimal -> its length. ax_fmt2: two digits, zero-padded, into d[0], d[1].
KAPI_FN void ax_strcat (char *dst, int cap, int *pos, const char *src);
KAPI_FN void ax_app_path (char *dst, int cap, const char *name, const char *suffix);
KAPI_FN int  ax_streq (const char *a, const char *b);
KAPI_FN int  ax_strlen (const char *s);
KAPI_FN int  ax_itoa (int v, char *b);
KAPI_FN void ax_fmt2 (char *d, int v);

// The console: a string / a line to the standard output.
KAPI_FN void ax_puts (const char *s);
KAPI_FN void ax_putln (const char *s);

// A minimal .ini reader: [section] headers and key=value lines, ';' or '#' comments, the spaces around
// trimmed. One file loaded at a time, a program (the store is the program's own); a load replaces the
// one before. Limits: INI_MAX entries, INI_STRLEN - 1 characters a name or a value, INI_BUFSZ - 1 bytes
// of file.
//   app_ini_load (filename)      <the program's own folder>/filename -> the number of entries, -1 absent
//   app_ini_load_path (path)     any file
//   app_ini_get (section, key, def), app_ini_get_int (...)   section 0 or "": the keys before any [section]
//   app_ini_count (), app_ini_section / app_ini_key / app_ini_value (i)   the entries, in the file's order
#define INI_MAX		64
#define INI_STRLEN	64
#define INI_BUFSZ	2048
KAPI_FN int app_ini_load_path (const char *path);
KAPI_FN int app_ini_load (const char *filename);
KAPI_FN const char *app_ini_get (const char *section, const char *key, const char *def);
KAPI_FN int app_ini_get_int (const char *section, const char *key, int def);
KAPI_FN int app_ini_count (void);
KAPI_FN const char *app_ini_section (int i);
KAPI_FN const char *app_ini_key (int i);
KAPI_FN const char *app_ini_value (int i);

// The keyboard layout <name> ("FR", "BE"...): SD:/etc/keymaps/<name>.kmap (kapi_set_keymap_data), else a
// map the kernel has (kapi_set_keymap). Non-zero = done. The keyboard must be up (kapi_kbd_ready).
KAPI_FN int ax_load_keymap (const char *name);

// (the tests of the kernel's table, and the PC builds: the bodies inline -- see this header's top)
#if (defined (KAPI_INLINE) || !defined (__aarch64__)) && !defined (KAPI_IMPL)
#include "appkit_calls.inc"
#include "appkit_lib.inc"
#endif

#endif
