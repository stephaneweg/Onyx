//
// kapi_abi.h -- the kapi ABI shared by the kernel and userland apps.
//
// Instead of linking apps directly against kernel symbol addresses (which move on
// every kernel rebuild), the kernel publishes a function-pointer TABLE at a FIXED
// virtual address, mapped read-only into every app's address space. Apps call the
// kernel through this table, so an app binary keeps working against any kernel that
// exposes the same ABI -- no rebuild needed when the kernel changes.
//
// THE CONTRACT IS: this struct layout + KAPI_TABLE_VA. It is APPEND-ONLY -- never
// reorder or remove fields; add new ones at the end and bump KAPI_ABI_VERSION. Old
// apps then keep working (they only touch the prefix they know).
//
#ifndef _kern_kapi_abi_h
#define _kern_kapi_abi_h

// Fixed user VA where the kernel maps the table (one 64 KB page). Stable forever.
// (Window canvas is at 12 GB, user stack at 16 GB; this sits in the gap at 14 GB.)
#define KAPI_TABLE_VA		(14ULL * 0x40000000ULL)
// v29: COMPAT BREAK -- the kernel-drawn widget API was removed from the table and the
// table consolidated (no gaps), so old app binaries must be rebuilt. (Retro-compat was
// explicitly waived; every app is rebuilt from this tree.)
// v30: + random() -- hardware RNG (Pi RNG) for cryptographic seeding (TLS entropy).
// v33: + ram_detail() -- firmware-detected board RAM + app page-pool total/free (memmon).
// v35: + surface_create/map/size/present/destroy -- shared surfaces (activity shell);
//      + register_shell/shell_request/mailbox_send/mailbox_recv -- activity-shell IPC.
#define KAPI_ABI_VERSION	35

#ifdef __cplusplus
extern "C" {
#endif

// Widget / key event callback: void (sender, GUI_EVENT_*, value).
typedef void (*gui_handler) (unsigned long sender, int event, long value);

// A directory entry from kapi_readdir.
struct kapi_dirent
{
	char     name[128];
	unsigned size;		// bytes (0 for directories)
	int      is_dir;	// 1 if a directory
};

// The calling app's window surfaces, for a user-side chrome (decoration) drawer
// (ABI v28, kapi_get_chrome). `content` is the client canvas; `active`/`inactive` are
// the two pre-composited chrome copies the app draws its title bar / borders / close
// box into (the compositor blits the one matching focus, magenta = transparent). Both
// chrome pointers are 0 for a borderless window. Insets give the client offset inside
// the chrome (client origin = active + (inset_t * chrome_w + inset_l)).
struct kapi_chrome
{
	unsigned *content;		// client canvas (== USER_WINDOW_CANVAS)
	int       content_w, content_h;
	unsigned *active;		// active-focus chrome copy   (0 if borderless)
	unsigned *inactive;		// inactive (unfocused) copy  (0 if borderless)
	int       chrome_w, chrome_h;	// outer size of each chrome copy
	int       inset_l, inset_r, inset_t, inset_b;	// chrome insets (client offset)
	char      title[48];		// window title (kernel-owned copy)
};

struct TKApiTable
{
	unsigned version;		// KAPI_ABI_VERSION the kernel filled

	// --- windowing ---
	unsigned *(*create_window) (int w, int h, const char *title);
	unsigned *(*create_window_ex) (int x, int y, int w, int h, const char *title,
				       unsigned flags);
	unsigned *(*resize_window) (int w, int h);
	int (*launch) (const char *name);
	int (*toggle_app) (const char *name);
	int (*raise_app) (const char *name);
	int (*list_windows) (char *buf, unsigned size);
	int (*wallpaper_generate) (unsigned base, int points, unsigned seed);
	void (*present) (void);
	unsigned (*get_ticks) (void);
	void (*msleep) (unsigned ms);
	void (*yield) (void);
	void (*exit) (int status);

	// (The kernel-drawn widget API -- add_button/label/checkbox/textbox/progress/
	// slider/textarea/scrollbar/icon + widget_get/set_* -- was removed once every app
	// moved to the user-side uikit toolkit. Kernel modal dialogs draw their own
	// controls internally; nothing calls these through the table any more.)

	// --- events ---
	void (*pump_events) (void);
	void (*wait_for_exit) (void);
	int (*should_exit) (void);

	// --- app-drawn text + keyboard ---
	void (*draw_text) (int, int, const char *, unsigned);
	int (*font_width) (void);
	int (*font_height) (void);
	void (*set_key_handler) (gui_handler);

	// --- enumeration + clock ---
	int (*list_apps) (char *, unsigned);
	int (*get_datetime) (int *, int *, int *, int *, int *, int *);

	// --- console + files ---
	int (*write) (int, const void *, unsigned);
	void *(*open) (const char *);
	int (*read) (void *, void *, unsigned);
	unsigned (*fsize) (void *);
	void (*close) (void *);
	int (*save_file) (const char *, const void *, unsigned);

	// --- v1 additions (append below; bump KAPI_ABI_VERSION) ---
	// The calling app's folder: "SD:apps/<name>.app/" into buf. Returns length.
	int (*app_dir) (char *buf, unsigned size);

	// --- v2 additions ---
	// Canvas-click handler: GUI_EVENT_CANVAS_CLICK with (clientX<<16)|clientY when a
	// press lands in the client area on no widget. For app-drawn mouse UIs.
	void (*set_click_handler) (gui_handler);

	// --- v3 additions ---
	// Directory listing (FatFs). opendir returns a handle (0 on failure); readdir
	// fills *ent and returns 1, or 0 at end; closedir releases it.
	void *(*opendir) (const char *path);
	int   (*readdir) (void *dir, struct kapi_dirent *ent);
	void  (*closedir) (void *dir);

	// --- v4 additions (stdio / streams / processes) ---
	void *(*pipe) (void);				// in-memory FIFO stream
	void *(*file_in) (const char *path);		// file -> stream (read)
	void *(*file_out) (const char *path, int append); // stream -> file (trunc/append)
	int   (*stream_read) (void *h, void *buf, unsigned len);   // 0 = EOF
	int   (*stream_write) (void *h, const void *buf, unsigned len);
	void  (*stream_close) (void *h);		// drop a ref
	int   (*stdin_read) (void *buf, unsigned len);	// this task's stdin (0 = EOF)
	int   (*stdout_write) (const void *buf, unsigned len);	// this task's stdout
	void *(*spawn) (const char *path, const char *args, void *in, void *out); // -> proc
	int   (*wait) (void *proc);			// block (cooperative) -> exit status
	int   (*get_args) (char *buf, unsigned size);	// this task's argv string

	// --- v5 additions ---
	int   (*stream_read_nb) (void *h, void *buf, unsigned len); // >0 / 0=EOF / -1=block

	// --- v6 additions ---
	void  (*stream_eof) (void *h);		// signal EOF to readers (writer done)
	int   (*proc_done) (void *proc);	// 1 if a spawned process has finished

	// --- v7 additions ---
	int   (*mkdir) (const char *path);	// 0 ok / -1
	int   (*remove) (const char *path);	// file or empty dir
	int   (*rename) (const char *from, const char *to);
	void  (*cursor_pos) (int *x, int *y);	// cursor, relative to this window's client

	// --- v8 additions ---
	int   (*list_tasks) (char *buf, unsigned size);	// "<state><kind> <name>" per line
	int   (*kill) (const char *name);		// kill an app by name (not kernel/self)

	// (v9/v10 message_box + file_open/file_save were removed: kernel modal dialogs are
	// gone -- apps use the user-side uidialog.hpp (ui::MessageBox / ui::FileDialog).)

	// --- v11 additions ---
	// Run an ELF at an absolute SD path with an argv string (fire-and-forget: no
	// stdio, no wait handle; the task name is derived from the path). Returns 1/0.
	// Used by the file manager to open documents (app + file) and run programs.
	int   (*exec) (const char *path, const char *args);
	// Framebuffer dimensions (for edge-pinned/borderless windows like the panel).
	void  (*screen_size) (int *w, int *h);

	// --- v12 additions ---
	// Move the calling app's window (outer top-left, screen coords). For borderless
	// windows that re-position themselves, e.g. the panel keeping itself centered.
	void  (*move_window) (int x, int y);

	// --- v13 additions (app-drawn desktop wallpaper) ---
	// Map the shared screen-sized wallpaper buffer (0x00RRGGBB) into this app and
	// return its VA (+ dims via w/h). Draw into it, then wallpaper_commit() to make
	// it the live background. Frames are kernel-owned -> persists after the app exits.
	unsigned *(*wallpaper_buffer) (int *w, int *h);
	void      (*wallpaper_commit) (void);

	// --- v14 additions (ps / kill by PID) ---
	// list_procs: one line per task "<pid> <a|k> <state> <name>" (pid 0 = kernel
	// task). kill_pid: nForce 0 = clean close (window exit flag), 1 = hard terminate;
	// returns 1 killed/signalled, 0 no such pid, -1 protected (kernel task or self).
	int (*list_procs) (char *buf, unsigned size);
	int (*kill_pid) (int pid, int force);

	// --- v15 additions (keyboard layout) ---
	// set_keymap: switch to a compiled-in country map ("FR","US","DE","UK","ES",
	// "IT","DV"); 1 ok / 0 unknown-or-no-keyboard. get_keymap: current name into buf.
	int (*set_keymap) (const char *name);
	int (*get_keymap) (char *buf, unsigned size);

	// (v16 set_window_theme removed -- window chrome is drawn user-side; runtime
	// re-theming would be a user-side toolkit concern + a re-decorate broadcast.)

	// --- v17 additions (working directory) ---
	// chdir: set the calling task's cwd (path resolved + verified as a dir); 1/0.
	// getcwd: current cwd into buf. All file kapis resolve relative paths against it,
	// and a spawned child inherits the spawner's cwd.
	int (*chdir) (const char *path);
	int (*getcwd) (char *buf, unsigned size);

	// --- v18 additions (shell as a process) ---
	// This task's own stdin/stdout stream handles, to wire into spawned children
	// (the cmd shell passes its stdin to the first stage; 0 if none).
	void *(*stdin_stream) (void);
	void *(*stdout_stream) (void);

	// --- v19 additions (kernel log viewer) ---
	// Read the next kernel log event (a tee of CLogger's ring; the logs still go to
	// their normal output). 1 + fills severity (0=panic..4=debug)/source/message, or
	// 0 if empty. For a real-time log viewer (kmsg).
	int (*klog_read) (int *severity, char *src, unsigned src_cap, char *msg, unsigned msg_cap);

	// --- v20 additions (verbose logging) ---
	// Toggle / read the kernel's verbose-logging flag (app lifecycle logs). The
	// `verbose` command persists the choice in SD:system.ini.
	int (*set_verbose) (int on);
	int (*get_verbose) (void);

	// --- v21 additions (TCP/IP sockets over WLAN) ---
	// net_status: 1 + dotted IPv4 into ip[] if the link is up, else 0 (ip="").
	// tcp_connect: resolve host (dotted-quad or DNS name) + connect; returns a
	// handle >=0, or <0 on error (-1 no net, -2 too many sockets, -3 DNS fail,
	// -5 connect fail). tcp_send: blocking send (5 s timeout); bytes sent or <0.
	// tcp_recv: NON-BLOCKING; >0 bytes, 0 nothing yet, <0 closed/error. tcp_close:
	// drop the connection + free the handle. Sockets are auto-closed if the owning
	// process dies.
	int  (*net_status) (char *ip, unsigned cap);
	int  (*tcp_connect) (const char *host, unsigned port);
	int  (*tcp_send) (int sock, const void *buf, unsigned len);
	int  (*tcp_recv) (int sock, void *buf, unsigned len);
	void (*tcp_close) (int sock);

	// --- v22 additions (full pointer stream for app-side widget toolkits) ---
	// Opt-in: register a handler that receives GUI_EVENT_PTR_MOVE/DOWN/UP/ENTER/LEAVE
	// for this window's client area, with value = (changed<<40)|(buttons<<32)|
	// (x<<16)|y (client coords; `changed` = the button 1/2/4 for DOWN/UP). Lets a
	// user-space toolkit (uikit.h) own its widgets. The legacy set_click_handler is
	// unchanged.
	void (*set_pointer_handler) (gui_handler fn);

	// --- v23 additions (memory info) ---
	// System memory snapshot, all in KB: *total RAM, *free (unallocated page region +
	// free heap), *app (owned by user processes), *page_kb (page size). Any pointer
	// may be 0. Returns 1. For the memory monitor + accounting.
	int (*meminfo) (unsigned long *total_kb, unsigned long *free_kb,
			unsigned long *app_kb, unsigned *page_kb);

	// --- v24 additions (per-process heap) ---
	// Unix-style sbrk: move the calling app's heap break by `increment` bytes
	// (mapping fresh pages as it grows), return the previous break, or (void*)-1 on
	// failure. The foundation for a user-space allocator (user/umm.h: malloc/free +
	// operator new/delete). Heap pages are owned by the address space -> freed on exit.
	void *(*sbrk) (long increment);

	// --- v25 additions (power) ---
	// Reboot the machine immediately (Circle reboot(); does not return). Used to
	// apply settings that are only read at boot -- e.g. wpaconf rewriting the WLAN
	// config in SD:/etc/wpa_supplicant.conf, which the kernel reads during bring-up.
	void (*reboot) (void);

	// --- v26 additions (keyboard readiness) ---
	// 1 if a USB keyboard is attached & ready, else 0. The kernel no longer applies
	// any keyboard layout from cmdline; the `keyb` tool (run from autostart) polls
	// this then calls set_keymap -- it may start before USB enumeration finishes.
	int (*kbd_ready) (void);

	// --- v27 additions (file-based keymaps) ---
	// Load a keyboard layout from a SD:/etc/keymaps/<X>.kmap blob: header "OKM1" +
	// u16 rows(128) + u16 cols(5) + rows*cols u16 table (row-major m_KeyMap[phy][tab]).
	// The kernel validates + copies it into the live keyboard; the caller frees the
	// buffer. `name` is recorded for get_keymap. Returns 1 on success, 0 otherwise.
	// New layouts can be added as files with no kernel rebuild (see tools/keymaps).
	int (*set_keymap_data) (const char *name, const void *data, unsigned len);

	// --- v28 additions (user-side window chrome) ---
	// get_chrome: fill *out with the calling app's window surfaces (content canvas +
	// the active/inactive chrome copies + insets + title) so a user-side toolkit can
	// draw the title bar / borders / close box. Returns 1, or 0 if the app has no
	// window. draw_text_buf: render kernel-font text (transparent background) into an
	// arbitrary app-mapped 0x00RRGGBB buffer (dstW x dstH) at (x,y) -- used to draw the
	// title into the chrome buffer (the kernel font is the only font apps have). The
	// kernel still owns chrome BEHAVIOUR (title-bar drag, close-box hit-test).
	int  (*get_chrome) (struct kapi_chrome *out);
	void (*draw_text_buf) (unsigned *dst, int dstW, int dstH, int x, int y,
			       const char *s, unsigned color);

	// --- v30 additions (hardware RNG) ---
	// Fill buf[len] with random bytes from the Pi's hardware RNG (Circle
	// CBcmRandomNumberGenerator). For cryptographic seeding -- e.g. the TLS entropy
	// source in user/tls/onyx_tls.hpp. Returns the number of bytes written (== len).
	int (*random) (void *buf, unsigned len);

	// --- v33 additions (memory detail for memmon) ---
	// detected_kb = firmware-reported physical board RAM (CMachineInfo::GetRAMSize) --
	// e.g. 4 GB even though the managed total (meminfo) is ~3 GB on a 4 GB board.
	// apppool_kb / apppool_free_kb = total / free of the HIGH page zone that backs app
	// frames (palloc_high -- ELF segments, heaps, decoded images). Any out-ptr may be 0.
	int (*ram_detail) (unsigned long *detected_kb, unsigned long *apppool_kb,
			   unsigned long *apppool_free_kb, unsigned long *above4g_kb,
			   unsigned *nsegments);

	// --- v34 additions (scroll-wheel speed) ---
	// Lines scrolled per wheel notch, applied system-wide: the WM multiplies the raw
	// notch by this factor before delivering GUI_EVENT_PTR_WHEEL, so every toolkit/app
	// feels it at once. set clamps to [1,16]; the theme editor persists it in
	// SD:/etc/theme.txt (wheelspeed=N) and the kernel restores it at boot.
	void (*set_wheel_speed) (int lines_per_notch);
	int  (*get_wheel_speed) (void);

	// --- v35 additions (shell surfaces -- activity-shell compositor) ---
	// A shared pixel surface (0x00RRGGBB). The shell creates one sized to a viewport
	// (surface_create -> id > 0), passes the id to an app; both map it (surface_map ->
	// the surface VA in the caller's address space; SAME physical frames, so the app
	// draws straight into the pixels the shell composes from). surface_size fills the
	// agreed w/h; surface_present yields toward the compositing shell; surface_destroy
	// frees it (owner only -- also auto-freed when the owner process dies).
	int       (*surface_create) (int w, int h);
	unsigned *(*surface_map) (int id);
	int       (*surface_size) (int id, int *w, int *h);
	void      (*surface_present) (int id);
	int       (*surface_destroy) (int id);

	// Activity-shell IPC (the kernel is a thin message router; see kern/ipc.h). A user
	// compositor calls register_shell to become THE shell. Apps post to it with
	// shell_request (the kernel stamps the caller's pid). The shell replies / pushes
	// async events with mailbox_send(target_pid,...). Both drain with mailbox_recv,
	// which fills *from_pid / *type and returns the payload length (or -1 if empty;
	// blocking != 0 waits for a message). Messages are opaque {from_pid,type,bytes};
	// `type` meaning is the user shell protocol. from_pid 0 == from the kernel.
	int  (*register_shell) (void);
	int  (*shell_request) (int type, const void *in, unsigned len);
	int  (*mailbox_send) (int target_pid, int type, const void *in, unsigned len);
	int  (*mailbox_recv) (int *from_pid, int *type, void *buf, unsigned cap, int blocking);
};

#ifdef __cplusplus
}
#endif

#endif // _kern_kapi_abi_h
