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
 * given the page's coordinates (the band's height taken off). */
typedef void (*onyx_chrome_handler)(unsigned long sender, int event, long value);
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

/* --- the commands the toolbar / the menu run (implemented in gui.c) ------------------ */
void onyx_browser_back(void);
void onyx_browser_forward(void);
void onyx_browser_reload(void);
void onyx_browser_stop(void);
void onyx_browser_toggle_desktop(void);		/* the site's desktop / mobile version */
void onyx_browser_home(void);
void onyx_browser_go(const char *text);		/* an address typed (a URL, or a host) */
void onyx_browser_redraw(void);			/* the page covered by a pop-up: redraw it */

/* The History dialog's pages: NetSurf's global history (kept on the card, in
 * SD:/apps/netsurf.app/History), the most recent first. `fn` is called for each page with
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
