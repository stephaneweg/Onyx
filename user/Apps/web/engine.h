//
// web/engine.h -- what the browser's window (main.cpp, the "chrome") asks of the page engine. Two
// implementations: engine_webkit.cpp (WebKit2 through its C API and the Onyx view: the real browser,
// built by tools/webkit/build-web.sh with the POSIX toolchain) and engine_mock.cpp (a picture for a
// page: the desktop simulator, tools/tests/desktop_sim, where the window is tried and photographed).
//
// The engine draws into the window's canvas (0x00RRGGBB) and calls the chrome back through
// EngineClient. Everything happens on the window's thread: the chrome calls engine_cycle () from
// its loop (Root::onTick) and the engine runs its own work there.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (see LICENSE).
//
#ifndef _web_engine_h
#define _web_engine_h

// What the page tells the window.
struct EngineClient
{
	void (*needsDisplay) (int x, int y, int w, int h);	// these pixels changed: engine_paint them
	void (*titleChanged) (const char *title);		// UTF-8 ("" none)
	void (*urlChanged) (const char *url);			// the address to show
	void (*loadingChanged) (bool loading, double progress);	// progress 0..1
	void (*historyChanged) (bool canBack, bool canForward);
	void (*statusText) (const char *text);			// the link under the pointer ("" none)
	void (*openWindow) (const char *url);			// target=_blank, window.open: a new window
	void (*loadFailed) (const char *url, const char *why);
	void (*processEnded) ();				// the web process ended (crash, killed)
	void (*alert) (const char *text);			// window.alert
	bool (*confirm) (const char *text);			// window.confirm
	void (*keyNotHandled) (long key, unsigned mods);	// a key the page did not use: the window's shortcuts
	// A <select>'s list: its items (flags: ENGINE_ITEM_*), the one selected (-1 none), the box
	// (page coordinates). The window answers later with engine_popup_select.
	void (*showPopup) (const char *const *items, const unsigned char *flags, int count, int selected,
			   int x, int y, int w, int h);
	void (*hidePopup) ();
	void (*findResult) (int matches);			// after engine_find: the matches (0: none)
	// A download: ENGINE_DL_* events, its id, its file (path; for FAILED: why), the bytes.
	void (*download) (int id, int event, const char *text, long long done, long long total);
};

enum { ENGINE_ITEM_ENABLED = 1, ENGINE_ITEM_SEPARATOR = 2, ENGINE_ITEM_LABEL = 4 };
enum { ENGINE_DL_STARTED, ENGINE_DL_PROGRESS, ENGINE_DL_FINISHED, ENGINE_DL_FAILED, ENGINE_DL_CANCELLED };

// The program's own role (one program, three roles: docs/08-WEBKIT-PORT.md): true when this run is
// one of WebKit's auxiliary processes; then engine_auxiliary_main () is the whole program.
bool engine_is_auxiliary (int argc, char **argv);
int  engine_auxiliary_main (int argc, char **argv);

bool engine_init (const EngineClient *client, int w, int h);	// the page area's size
void engine_new_window (const char *url);			// this program started again on url
void engine_cycle ();						// the engine's work (from the window's loop)
void engine_resize (int w, int h);
void engine_paint (unsigned *px, int stride, int x, int y, int w, int h);	// into the canvas
void engine_set_active (bool active);				// the window is in front

void engine_load (const char *url);				// a URL, or what the user typed
void engine_back ();
void engine_forward ();
void engine_reload ();
void engine_stop ();
void engine_zoom (double factor);				// 1 = 100 %
double engine_zoom_factor ();
void engine_command (const char *name);				// "Copy", "Cut", "Paste", "SelectAll"
void engine_popup_select (int index);				// the <select> list's answer (-1: none)
void engine_find (const char *text, bool backwards);		// highlights and goes to the next match
void engine_find_done ();
// What was under the pointer last (a link's, an image's address; "" none): the right click's menu.
void engine_hit (char *link, int linkCap, char *image, int imageCap);
void engine_download_url (const char *url, const char *path);	// path "": SD:/Downloads/<its name>
void engine_download_cancel (int id);
// The system clipboard, the window's calls (kapi.h): copies leave the page, other programs' come in.
void engine_set_clipboard (void (*write) (const char *, unsigned long), unsigned long (*read) (char *, unsigned long),
			   unsigned (*serial) ());
// The embedded web view (webview.cpp: Mail's HTML messages): given HTML (baseUrl may be ""),
// JavaScript on or off (before the first load), and the links: with a handler set, a click on a link
// (or a form sent) is not followed but given to it.
void engine_load_html (const char *html, const char *baseUrl);
void engine_set_scripts (bool enabled);
void engine_set_link_handler (void (*clicked) (const char *url));
// Helper processes started from this program (the downloads' window): argv[1] the role.
int  engine_spawn_self (const char *role, int *toChild, int *fromChild);	// -> pid (0: failed)

// Input, in the page area's coordinates. buttons: bit 0 left, 1 right, 2 middle (held after the
// event); changed: the button pressed or released (0 a move). mods: MOD_* (kapi.h).
void engine_mouse (int x, int y, int buttons, int changed, unsigned mods);
void engine_wheel (int x, int y, int notches, unsigned mods);	// + towards the top
void engine_key (long key, unsigned mods);			// a wtk / kapi key code (KEY_*, or the character)
void engine_mouse_leave ();

#endif
