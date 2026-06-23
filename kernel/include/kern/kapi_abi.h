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
#define KAPI_ABI_VERSION	2

#ifdef __cplusplus
extern "C" {
#endif

// Widget / key event callback: void (sender, GUI_EVENT_*, value).
typedef void (*gui_handler) (unsigned long sender, int event, long value);

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
};

#ifdef __cplusplus
}
#endif

#endif // _kern_kapi_abi_h
