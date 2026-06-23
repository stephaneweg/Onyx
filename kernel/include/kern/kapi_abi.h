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
#define KAPI_ABI_VERSION	20

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
	int (*set_wallpaper) (const char *path);
	int (*wallpaper_generate) (unsigned base, int points, unsigned seed);
	void (*present) (void);
	unsigned (*get_ticks) (void);
	void (*msleep) (unsigned ms);
	void (*yield) (void);
	void (*exit) (int status);

	// --- widgets ---
	unsigned long (*add_button) (int, int, int, int, const char *, gui_handler);
	unsigned long (*add_label) (int, int, int, int, const char *);
	unsigned long (*add_checkbox) (int, int, int, int, const char *, gui_handler);
	unsigned long (*add_textbox) (int, int, int, int, gui_handler);
	unsigned long (*add_progress) (int, int, int, int);
	unsigned long (*add_slider) (int, int, int, int, gui_handler);
	unsigned long (*add_textarea) (int, int, int, int, gui_handler);
	unsigned long (*add_scrollbar_v) (int, int, int, int, gui_handler);
	unsigned long (*add_scrollbar_h) (int, int, int, int, gui_handler);
	unsigned long (*add_icon) (int, int, int, int, const char *, const char *,
				   gui_handler);

	int (*widget_get_text) (unsigned long, char *, unsigned);
	void (*widget_set_text) (unsigned long, const char *);
	int (*widget_get_checked) (unsigned long);
	int (*widget_get_value) (unsigned long);
	void (*widget_set_value) (unsigned long, int);
	void (*widget_set_rect) (unsigned long, int, int, int, int);
	void (*widget_set_icon) (unsigned long, const char *);

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

	// --- v9 additions ---
	// Modal message box (sync): blocks until answered. buttons = MB_OK/MB_OKCANCEL/
	// MB_YESNO. Returns 1 (OK/Yes) or 0 (Cancel/No).
	int   (*message_box) (const char *title, const char *text, int buttons);

	// --- v10 additions (modal file dialogs, sync) ---
	// Returns 1 + fills out[] with the chosen path, or 0 if cancelled.
	int   (*file_open) (char *out, unsigned cap, const char *start_dir);
	int   (*file_save) (char *out, unsigned cap, const char *start_dir, const char *def_name);

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

	// --- v16 additions (theme editor) ---
	// Re-tint the window chrome at runtime: active + inactive skin tints + title text
	// colour (0x00RRGGBB). Persist by also writing SD:skins/theme.txt (read at boot).
	void (*set_window_theme) (unsigned active, unsigned inactive, unsigned text);

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
};

#ifdef __cplusplus
}
#endif

#endif // _kern_kapi_abi_h
