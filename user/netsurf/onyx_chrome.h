/*
 * onyx_chrome.h -- NetSurf's window on Onyx: a native (wtk) window whose top band is the
 * toolbar -- back, forward, reload / stop, home, the address field -- and whose rest is the
 * page, drawn by NetSurf through the "onyx" libnsfb surface (user/nsfb/onyx_surface.c). The
 * window's own frame (its close box, its window menu, maximise) is the only frame: NetSurf's
 * fbtk toolbar and close button are not made (fb_toolbar_layout "").
 *
 * Implemented in onyx_chrome.cpp (C++ / wtk); called by the surface and by the framebuffer
 * frontend (frontends/framebuffer/gui.c, the "Onyx:" parts), which in turn implements the
 * onyx_browser_* commands the toolbar runs.
 */
#ifndef ONYX_CHROME_H
#define ONYX_CHROME_H

#ifdef __cplusplus
extern "C" {
#endif

/* the toolbar band's height, in pixels, above the page */
#define ONYX_TOOLBAR_H 40

/* --- the surface (user/nsfb/onyx_surface.c) ------------------------------------------ */

/* Make the window: its client area w x (h + the band). Returns the page's first pixel
 * (0x00RRGGBB, `*stride` pixels a row), or 0. */
unsigned *onyx_chrome_open(int w, int h, int *stride);

/* The page's pixels, stride and size now (after a resize of the window). */
unsigned *onyx_chrome_page(int *stride, int *w, int *h);

/* The page's events: the surface's handlers (the kapi pointer / key handler signature),
 * given the page's coordinates (the band's height taken off). The value has 64 bits (a
 * pointer event packs its wheel, buttons, x, y): long long, as long has 32 on Windows
 * (pc/Jet: Jet Browser's Windows build). */
typedef void (*onyx_chrome_handler)(unsigned long sender, int event, long long value);
void onyx_chrome_set_page_handlers(onyx_chrome_handler ptr, onyx_chrome_handler key);

/* Pump the window's events (the band's go to wtk, the page's to the handlers) and redraw
 * the band when it changed. Returns 1 when the window was resized (the page's geometry
 * changed: onyx_chrome_page), 0 else. */
int onyx_chrome_pump(void);
int onyx_chrome_pump_wait(int ms);	/* the same, waiting up to ms for an event / a post */
/* Pump the events without waiting: 1 when a click, a wheel turn, a key, a resize or the close
 * box came since (framebuffer/schedule.c: the main loop takes them before more callbacks). */
int onyx_chrome_input_pending(void);
/* Pump the events while a script runs long (qjs.c's interrupt handler): they are kept and
 * handled at the next pump, after the script. */
void onyx_chrome_pump_deferred(void);

/* The part of the page the compositor writes itself (NetSurf's view, composited: its
 * pixels are not in the back buffer): left out of the back buffer's copies to the canvas.
 * x1 <= x0: none. (user/nsfb/onyx_surface.c; frontends/framebuffer/onyx_comp.c) */
void onyx_surface_hole(int x0, int y0, int x1, int y1);
/* The canvas's page area now: its first pixel, stride (pixels), size. */
unsigned *onyx_surface_canvas(int *stride, int *w, int *h);

/* Show the window's pixels. */
void onyx_chrome_present(void);
/* the page redrawn: presented by onyx_chrome_flush(), once a main-loop iteration */
void onyx_chrome_present_later(void);
void onyx_chrome_flush(void);
/* Onyx (docs/06 §32): whether the window is seen -- 0 focused, 1 shown without the keyboard,
 * 2 hidden (minimised, on another workspace, covered whole by opaque windows). */
int onyx_chrome_view_state(void);

/* A page size for a new window that fits the screen (the band, the frame, the menu bar
 * and the dock left out). */
void onyx_chrome_default_size(int *w, int *h);

/* The theme's colours (0x00RRGGBB) for NetSurf's own furniture (the scroll bars, the status
 * line): the face, and a darker shade of it (the scroll bars' thumbs). */
void onyx_chrome_theme(unsigned *face, unsigned *shade);

/* --- NetSurf's state (frontends/framebuffer/gui.c) ----------------------------------- */
void onyx_chrome_set_url(const char *url);
void onyx_chrome_set_busy(int busy);			/* loading: reload becomes stop */
void onyx_chrome_set_nav(int can_back, int can_forward);
/* The page's security and its site's version: the padlock left of the address field, the
 * pill right of it (both hidden for a page that is not http / https). sec: ONYX_SEC_*;
 * mode: -1 none, else utils/useragent.h's USER_AGENT_STANDARD / MOBILE / DESKTOP / CUSTOM. */
enum { ONYX_SEC_NONE = 0,	/* not a web page (file:, about:): no padlock */
       ONYX_SEC_INSECURE,	/* http: a grey, struck padlock ("Not secure") */
       ONYX_SEC_SECURE,		/* https, the certificate verified: green */
       ONYX_SEC_MIXED,		/* ... some of the page's parts over http: green, said so */
       ONYX_SEC_BROKEN };	/* https past a certificate warning ("Proceed"): red */
void onyx_chrome_set_site(int sec, int mode);

/* --- Onyx (docs/06 §38): the zoom, the status bar, the downloads -------------------- */

/* The page's zoom (percent), shown by the toolbar's zoom control ("-  100 %  +"). */
void onyx_chrome_set_zoom(int percent);

/* The status bar (the window's bottom band, ONYX_STATUSBAR_H high, shown by default): its
 * left part tells the page's state -- "Loading...", "Ready", "404 Not Found" (error: in red);
 * the link under the pointer replaces it while there is one (UTF-8, NULL: none). Only the
 * bar is repainted, and only when its text changes. */
#define ONYX_STATUSBAR_H 22
void onyx_chrome_set_state(const char *text, int error);
void onyx_chrome_set_link(const char *text);
/* Show / hide the bar (the page area grows or shrinks: a resize of the page). */
void onyx_chrome_show_status_bar(int shown);
int  onyx_chrome_status_bar_shown(void);

/* The downloads, for the toolbar's download button (its menu: progress, cancel) and the
 * status bar. state: ONYX_DL_*. The chrome copies what it shows. */
enum { ONYX_DL_ASK = 0, ONYX_DL_RUNNING, ONYX_DL_DONE, ONYX_DL_FAILED, ONYX_DL_CANCELLED };
struct onyx_dl_info {
	int id;
	const char *name;		/* the file's name (ASCII: safe for the card) */
	const char *path;		/* where it is saved ("" while asked) */
	const char *error;		/* why it failed, or "" */
	unsigned long long got, total;	/* bytes so far, in all (0: not known) */
	int state;
};
void onyx_chrome_downloads(const struct onyx_dl_info *list, int n);
/* The Save dialog (wtk's FileDialog, in save mode): starting in dir, the name filled in
 * (ASCII). 1 and the path chosen in path, 0 cancelled. Asks before replacing a file. */
int  onyx_chrome_save_dialog(const char *dir, const char *name, char *path, int cap);
/* A message box over the page (OK). */
void onyx_chrome_message(const char *title, const char *text);
/* A desktop notification (notifyd), when it runs; else nothing. */
void onyx_chrome_notify(const char *title, const char *text);

/* --- Onyx (docs/06 §40): find in page, the context menu, copying -------------------- */

/* The find bar (above the status bar; Ctrl+F, Edit > Find in Page...): the search's state --
 * the current match (0-based, -1 none) and the count (-1: not searched). */
void onyx_chrome_find_result(int index, int count);
int  onyx_chrome_find_shown(void);
void onyx_chrome_find_open(void);
/* A new page shown while the bar is open: its words searched again (the view not moved). */
void onyx_chrome_find_again(void);

/* The page's context menu (a right press on the page, x y the page's): what is under the
 * pointer (ONYX_CTXF_*) -> the item chosen (ONYX_CMD_*), 0 none. */
enum { ONYX_CTXF_LINK = 1, ONYX_CTXF_IMAGE = 2, ONYX_CTXF_IMAGE_PIXELS = 4,	/* (decoded: copyable) */
       ONYX_CTXF_SELECTION = 8, ONYX_CTXF_EDITABLE = 16, ONYX_CTXF_CAN_CUT = 32,
       ONYX_CTXF_BACK = 64, ONYX_CTXF_FORWARD = 128 };
enum { ONYX_CMD_NONE = 0, ONYX_CMD_BACK, ONYX_CMD_FORWARD, ONYX_CMD_RELOAD, ONYX_CMD_CUT,
       ONYX_CMD_COPY, ONYX_CMD_PASTE, ONYX_CMD_SELECT_ALL, ONYX_CMD_FIND, ONYX_CMD_OPEN_LINK,
       ONYX_CMD_SAVE_LINK, ONYX_CMD_COPY_LINK, ONYX_CMD_OPEN_IMAGE, ONYX_CMD_SAVE_IMAGE,
       ONYX_CMD_COPY_IMAGE, ONYX_CMD_COPY_IMAGE_URL };
int  onyx_chrome_context_menu(int x, int y, int flags);

/* w x h pixels 0xAARRGGBB written as a PNG file (img/pngsave.hpp): 1 done, 0 failed. */
int  onyx_chrome_save_png(const char *path, const unsigned *px, int w, int h);

/* gui.c / onyx_edit.c: the words (UTF-8) searched in the page -- dir 0 a new search (the
 * first match; typed: after a pause on a page slow to search), 1 the next match, -1 the one
 * before (around at the ends), 2 again without moving the view; match_case: exact letters
 * and accents. "" clears. */
void onyx_browser_find(const char *utf8, int dir, int match_case);
void onyx_browser_find_close(void);		/* the highlights cleared */
void onyx_browser_context_menu(int x, int y);	/* a right press on the page (its coordinates) */

/* --- the commands the toolbar / the menu run (implemented in gui.c) ------------------ */
void onyx_browser_zoom(int step);		/* +1 in, -1 out, 0 back to 100 % (Chrome's steps) */
void onyx_browser_set_status_bar(int shown);	/* View > Status Bar (kept in jet.app's view file) */
void onyx_browser_download_cancel(int id);	/* a download stopped, its file removed */
void onyx_browser_downloads_clear(void);	/* the finished ones out of the list */
void onyx_browser_back(void);
void onyx_browser_forward(void);
void onyx_browser_reload(void);
void onyx_browser_stop(void);
void onyx_browser_set_site_mode(int mode);	/* the site's version (USER_AGENT_*), reloaded */
const char *onyx_browser_site(void);		/* the page's site ("bbc.co.uk"), or "" */
int  onyx_browser_show_certificate(void);	/* the page's certificates in the viewer: 0 none */
void onyx_browser_home(void);
void onyx_browser_go(const char *text);		/* an address typed (a URL, or a host) */
void onyx_browser_redraw(void);			/* the page covered by a pop-up: redraw it */

/* The History dialog's pages: NetSurf's global history (kept on the card, in
 * SD:/apps/jet.app/History), the most recent first. `fn` is called for each page with
 * its address, its title (UTF-8, "" if none) and the time of its last visit (seconds since
 * 1970); returns how many. */
typedef void (*onyx_history_fn)(void *ctx, const char *url, const char *title, long long when);
int  onyx_browser_history(onyx_history_fn fn, void *ctx);
void onyx_browser_history_forget(const char *url);	/* take one page out of the history */
void onyx_browser_history_clear(void);		/* forget every page */

#ifdef __cplusplus
}
#endif

#endif /* ONYX_CHROME_H */
