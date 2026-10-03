//
// web/engine_webkit.cpp -- engine.h on WebKit2: its C API and the Onyx view (WKView, WKPagePrivateOnyx,
// WKRunLoop, WKEventOnyx: tools/webkit/patches). One program plays the three roles: started again by
// WebKit with --onyx-webkit-process=web|network, it runs that process (engine_auxiliary_main).
//
// What happens is written to the program's standard error -- the kernel log (`kmsg`) for an Onyx app:
// lines "web: ...", and WebKit's own WTFLogAlways lines from the three processes.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (see LICENSE).
//
#include "config.h"		// (WebKit's: compiled with the command of one of WebKit's sources)

#include "engine.h"

#include <WebKit/WKAuxiliaryProcessOnyx.h>
#include <WebKit/WKContext.h>
#include <WebKit/WKContextConfigurationOnyx.h>
#include <WebKit/WKContextConfigurationRef.h>
#include <WebKit/WKErrorRef.h>
#include <WebKit/WKEventOnyx.h>
#include <WebKit/WKGeometry.h>
#include <WebKit/WKHitTestResult.h>
#include <WebKit/WKNavigationActionRef.h>
#include <WebKit/WKPage.h>
#include <WebKit/WKPageConfigurationRef.h>
#include <WebKit/WKPageNavigationClient.h>
#include <WebKit/WKPagePrivateOnyx.h>
#include <WebKit/WKPageUIClient.h>
#include <WebKit/WKPreferencesRef.h>
#include <WebKit/WKRunLoop.h>
#include <WebKit/WKString.h>
#include <WebKit/WKType.h>
#include <WebKit/WKURL.h>
#include <WebKit/WKURLRequest.h>
#include <WebKit/WKView.h>
#include <WebKit/WKWebsiteDataStoreRef.h>
#include <spawn.h>
#include <stdarg.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <time.h>

// (kapi.h's key codes and modifiers; not the header: it is the newlib apps' and declares the kernel table)
#define MOD_CTRL	1
#define MOD_SHIFT	2
#define MOD_ALT		4
#define KEY_BACKSPACE	8
#define KEY_TAB		9
#define KEY_ENTER	13
#define KEY_UP		0x100
#define KEY_DOWN	0x101
#define KEY_LEFT	0x102
#define KEY_RIGHT	0x103
#define KEY_HOME	0x104
#define KEY_END		0x105
#define KEY_PGUP	0x106
#define KEY_PGDN	0x107
#define KEY_DEL		0x108
#define KEY_F1		0x110
#define KEY_F12		0x11B

static const EngineClient *s_client;
static WKContextRef s_context;
static WKViewRef s_view;
static WKPageRef s_page;
static int s_w, s_h;
static double s_start;

// What the window was told last (engine_cycle compares and calls back on a change).
static std::string s_title, s_url;
static bool s_loading, s_canBack, s_canForward;
static double s_progress;

static double now ()
{
	struct timespec ts;
	clock_gettime (CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void say (const char *fmt, ...)
{
	char b[512];
	va_list ap;
	va_start (ap, fmt);
	vsnprintf (b, sizeof b, fmt, ap);
	va_end (ap);
	fprintf (stderr, "web: [%.2f s] %s\n", now () - s_start, b);
}

static std::string str (WKStringRef s)
{
	if (!s)
		return { };
	size_t cap = WKStringGetMaximumUTF8CStringSize (s);
	std::string r (cap, '\0');
	size_t n = WKStringGetUTF8CString (s, r.data (), cap);
	r.resize (n ? n - 1 : 0);
	return r;
}

static std::string url_str (WKURLRef u)
{
	if (!u)
		return { };
	WKStringRef s = WKURLCopyString (u);
	std::string r = str (s);
	if (s)
		WKRelease (s);
	return r;
}

// ---- the program's roles ------------------------------------------------------------------------------

bool engine_is_auxiliary (int argc, char **argv) { return WKIsAuxiliaryProcessOnyx (argc, argv); }

int engine_auxiliary_main (int argc, char **argv)
{
	s_start = now ();
	fprintf (stderr, "web: %s process starts\n", argc > 1 ? argv[1] : "?");
	int r = WKAuxiliaryProcessMainOnyx (argc, argv);
	fprintf (stderr, "web: %s process ends (%d)\n", argc > 1 ? argv[1] : "?", r);
	return r;
}

// ---- the page's calls ------------------------------------------------------------------------------------

static void needsDisplay (WKViewRef, WKRect r, const void *)
{
	if (s_client->needsDisplay)
		s_client->needsDisplay ((int) r.origin.x, (int) r.origin.y, (int) r.size.width, (int) r.size.height);
}

static void processCrashed (WKViewRef, const void *)
{
	say ("the web process ended");
	if (s_client->processEnded)
		s_client->processEnded ();
}

static void processRelaunched (WKViewRef, const void *) { say ("a new web process"); }

static void keyNotHandled (WKViewRef, WKKeyboardEvent e, const void *);

static void didStartProvisional (WKPageRef, WKNavigationRef, WKTypeRef, const void *) { say ("load started"); }
static void didCommit (WKPageRef, WKNavigationRef, WKTypeRef, const void *) { say ("load committed"); }
static void didFinish (WKPageRef, WKNavigationRef, WKTypeRef, const void *) { say ("load finished"); }

static void loadError (const char *when, WKErrorRef e)
{
	WKStringRef d = e ? WKErrorCopyLocalizedDescription (e) : nullptr;
	WKStringRef dom = e ? WKErrorCopyDomain (e) : nullptr;
	WKURLRef u = e ? WKErrorCopyFailingURL (e) : nullptr;
	std::string why = str (d), url = url_str (u);
	int code = e ? WKErrorGetErrorCode (e) : 0;
	say ("load failed (%s): %s %d: %s -- %s", when, str (dom).c_str (), code, why.c_str (), url.c_str ());
	// A load the user stopped, or a navigation that became a download: no error page.
	if (code != -999 && code != 102 && s_client->loadFailed)
		s_client->loadFailed (url.c_str (), why.c_str ());
	if (d) WKRelease (d);
	if (dom) WKRelease (dom);
	if (u) WKRelease (u);
}

static void didFailProvisional (WKPageRef, WKNavigationRef, WKErrorRef e, WKTypeRef, const void *) { loadError ("before it began", e); }
static void didFail (WKPageRef, WKNavigationRef, WKErrorRef e, WKTypeRef, const void *) { loadError ("while loading", e); }
static void webProcessDidCrash (WKPageRef, const void *) { say ("the web process crashed"); }

// target=_blank, window.open: a new window -- a new launch of this program (the user's choice: with
// the program preloaded it is quick). No page is made here.
static WKPageRef createNewPage (WKPageRef, WKPageConfigurationRef, WKNavigationActionRef action, WKWindowFeaturesRef, const void *)
{
	WKURLRequestRef req = WKNavigationActionCopyRequest (action);
	WKURLRef u = req ? WKURLRequestCopyURL (req) : nullptr;
	std::string url = url_str (u);
	if (u) WKRelease (u);
	if (req) WKRelease (req);
	say ("a new window for %s", url.c_str ());
	if (!url.empty () && url != "about:blank" && s_client->openWindow)
		s_client->openWindow (url.c_str ());
	return nullptr;
}

static void mouseOverElement (WKPageRef, WKHitTestResultRef hit, WKEventModifiers, WKTypeRef, const void *)
{
	WKURLRef u = hit ? WKHitTestResultCopyAbsoluteLinkURL (hit) : nullptr;
	std::string url = url_str (u);
	if (u) WKRelease (u);
	if (s_client->statusText)
		s_client->statusText (url.c_str ());
}

static void runAlert (WKPageRef, WKStringRef text, WKFrameRef, WKSecurityOriginRef, WKPageRunJavaScriptAlertResultListenerRef listener, const void *)
{
	if (s_client->alert)
		s_client->alert (str (text).c_str ());
	WKPageRunJavaScriptAlertResultListenerCall (listener);
}

static void runConfirm (WKPageRef, WKStringRef text, WKFrameRef, WKSecurityOriginRef, WKPageRunJavaScriptConfirmResultListenerRef listener, const void *)
{
	bool ok = s_client->confirm ? s_client->confirm (str (text).c_str ()) : false;
	WKPageRunJavaScriptConfirmResultListenerCall (listener, ok);
}

// A new window: this program started again with the URL (the kernel shares its image: quick).
extern "C" char *program_invocation_name;
extern "C" char **environ;

// This program's file: argv[0] when it is a path (a start from the shell), else the app's place (a
// start from the dock or `run web` may give the name only).
static const char *self_path ()
{
	const char *a = program_invocation_name;
	if (a && (strchr (a, ':') || a[0] == '/')) return a;
	return "SD:/apps/web.app/main";
}

void engine_new_window (const char *url)
{
	const char *self = self_path ();
	char *argv[] = { const_cast<char *> (self), const_cast<char *> (url), nullptr };
	pid_t pid;
	int r = posix_spawn (&pid, self, nullptr, nullptr, argv, environ);
	say ("new window for %s: %s", url, r ? strerror (r) : "started");
}

// ---- init, the loop ------------------------------------------------------------------------------------

bool engine_init (const EngineClient *client, int w, int h)
{
	s_start = now ();
	s_client = client;
	s_w = w; s_h = h;
	say ("WebKit starts (%d x %d)", w, h);
	WKRunLoopInitializeMain ();

	WKContextConfigurationRef cc = WKContextConfigurationCreate ();
	// The web and the network process: this same program (its role by an argument).
	WKStringRef self = WKStringCreateWithUTF8CString (self_path ());
	WKContextConfigurationSetWebProcessPath (cc, self);
	WKContextConfigurationSetNetworkProcessPath (cc, self);
	WKRelease (self);
	say ("program %s (argv[0] %s)", self_path (), program_invocation_name ? program_invocation_name : "-");
	s_context = WKContextCreateWithConfiguration (cc);
	WKRelease (cc);

	WKPageConfigurationRef pc = WKPageConfigurationCreate ();
	WKPageConfigurationSetContext (pc, s_context);
	// The data (cookies, local storage, the caches) in SD:/var/webkit (WebsiteDataStoreOnyx).
	WKPageConfigurationSetWebsiteDataStore (pc, WKWebsiteDataStoreGetDefaultDataStore ());

	s_view = WKViewCreate (pc);
	WKRelease (pc);
	if (!s_view) {
		say ("no view");
		return false;
	}
	WKViewClientV0 vc;
	memset (&vc, 0, sizeof vc);
	vc.base.version = 0;
	vc.setViewNeedsDisplay = needsDisplay;
	vc.didNotHandleKeyEvent = keyNotHandled;
	vc.webProcessCrashed = processCrashed;
	vc.webProcessDidRelaunch = processRelaunched;
	WKViewSetViewClient (s_view, &vc.base);
	WKViewSetSize (s_view, WKSizeMake (w, h));
	WKViewSetVisible (s_view, true);
	WKViewSetActive (s_view, true);
	WKViewSetFocus (s_view, true);

	s_page = WKViewGetPage (s_view);
	WKStringRef app = WKStringCreateWithUTF8CString ("Onyx");
	WKPageSetApplicationNameForUserAgent (s_page, app);
	WKRelease (app);

	WKPageNavigationClientV0 nc;
	memset (&nc, 0, sizeof nc);
	nc.base.version = 0;
	nc.didStartProvisionalNavigation = didStartProvisional;
	nc.didCommitNavigation = didCommit;
	nc.didFinishNavigation = didFinish;
	nc.didFailProvisionalNavigation = didFailProvisional;
	nc.didFailNavigation = didFail;
	nc.webProcessDidCrash = webProcessDidCrash;
	WKPageSetPageNavigationClient (s_page, &nc.base);

	WKPageUIClientV19 uc;
	memset (&uc, 0, sizeof uc);
	uc.base.version = 19;
	uc.createNewPage = createNewPage;
	uc.mouseDidMoveOverElement = mouseOverElement;
	uc.runJavaScriptAlert = runAlert;
	uc.runJavaScriptConfirm = runConfirm;
	WKPageSetPageUIClient (s_page, &uc.base);
	say ("WebKit ready");
	return true;
}

void engine_cycle ()
{
	if (!s_page)
		return;
	WKRunLoopCycleMain ();

	// The page's state, polled (the window shows what changed).
	WKStringRef t = WKPageCopyTitle (s_page);
	std::string title = str (t);
	if (t) WKRelease (t);
	if (title != s_title) {
		s_title = title;
		if (s_client->titleChanged) s_client->titleChanged (title.c_str ());
	}
	WKURLRef u = WKPageCopyActiveURL (s_page);
	std::string url = url_str (u);
	if (u) WKRelease (u);
	if (url != s_url) {
		s_url = url;
		say ("address %s", url.c_str ());
		if (s_client->urlChanged) s_client->urlChanged (url.c_str ());
	}
	double p = WKPageGetEstimatedProgress (s_page);
	bool loading = p > 0 && p < 1;
	if (loading != s_loading || (loading && p - s_progress > 0.05)) {
		s_loading = loading; s_progress = p;
		if (s_client->loadingChanged) s_client->loadingChanged (loading, p);
	}
	bool b = WKPageCanGoBack (s_page), f = WKPageCanGoForward (s_page);
	if (b != s_canBack || f != s_canForward) {
		s_canBack = b; s_canForward = f;
		if (s_client->historyChanged) s_client->historyChanged (b, f);
	}
}

void engine_resize (int w, int h)
{
	s_w = w; s_h = h;
	if (s_view) WKViewSetSize (s_view, WKSizeMake (w, h));
}

void engine_paint (unsigned *px, int stride, int x, int y, int w, int h)
{
	if (!s_page)
		return;
	WKPagePaint (s_page, reinterpret_cast<unsigned char *> (px), WKSizeMake (s_w, s_h), (uint32_t) stride * 4, WKRectMake (x, y, w, h));
	// The canvas is 0x00RRGGBB: WebKit wrote an alpha byte.
	for (int j = y; j < y + h && j < s_h; j++)
		for (int i = x; i < x + w && i < s_w; i++)
			px[j * stride + i] &= 0x00FFFFFFu;
}

void engine_set_active (bool active)
{
	if (!s_view) return;
	WKViewSetActive (s_view, active);
	WKViewSetFocus (s_view, active);
}

// ---- navigation ----------------------------------------------------------------------------------------

// What the user typed, made a URL: a scheme kept; a path a file; a word with a dot a site (https); the
// rest a search.
static std::string to_url (const char *in)
{
	std::string s = in;
	while (!s.empty () && (s.back () == ' ' || s.back () == '\t')) s.pop_back ();
	size_t b = s.find_first_not_of (" \t");
	s = b == std::string::npos ? std::string () : s.substr (b);
	if (s.empty ()) return "about:blank";
	if (s.find ("://") != std::string::npos || !s.compare (0, 6, "about:") || !s.compare (0, 5, "data:"))
		return s;
	if (s[0] == '/') return "file://" + s;
	if (s.size () > 3 && s[2] == ':' && (s[3] == '/' || s[3] == '\0'))	// SD:/x -> its path
		return "file://" + s.substr (3);
	if (s.find (' ') == std::string::npos && s.find ('.') != std::string::npos)
		return "https://" + s;
	std::string q;
	for (unsigned char c : s) {
		if (isalnum (c) || c == '-' || c == '_' || c == '.') q += (char) c;
		else if (c == ' ') q += '+';
		else { char h[4]; snprintf (h, sizeof h, "%%%02X", c); q += h; }
	}
	return "https://duckduckgo.com/html/?q=" + q;
}

void engine_load (const char *typed)
{
	if (!s_page) return;
	std::string url = to_url (typed);
	say ("load %s", url.c_str ());
	WKURLRef u = WKURLCreateWithUTF8CString (url.c_str ());
	WKPageLoadURL (s_page, u);
	WKRelease (u);
}

void engine_back () { if (s_page && WKPageCanGoBack (s_page)) WKPageGoBack (s_page); }
void engine_forward () { if (s_page && WKPageCanGoForward (s_page)) WKPageGoForward (s_page); }
void engine_reload () { if (s_page) WKPageReload (s_page); }
void engine_stop () { if (s_page) WKPageStopLoading (s_page); }

static double s_zoom = 1;
void engine_zoom (double f)
{
	if (!s_page) return;
	s_zoom = f < 0.3 ? 0.3 : f > 3 ? 3 : f;
	WKPageSetPageZoomFactor (s_page, s_zoom);
}
double engine_zoom_factor () { return s_zoom; }

void engine_command (const char *name)
{
	if (!s_page) return;
	WKStringRef c = WKStringCreateWithUTF8CString (name);
	WKPageExecuteCommand (s_page, c);
	WKRelease (c);
}

// ---- input ---------------------------------------------------------------------------------------------

static uint32_t wk_mods (unsigned m)
{
	uint32_t r = 0;
	if (m & MOD_SHIFT) r |= kWKEventModifiersShiftKey;
	if (m & MOD_CTRL) r |= kWKEventModifiersControlKey;
	if (m & MOD_ALT) r |= kWKEventModifiersAltKey;
	return r;
}

static int s_buttons;
void engine_mouse (int x, int y, int buttons, int changed, unsigned mods)
{
	if (!s_page) return;
	WKEventType type = kWKEventMouseMove;
	WKEventMouseButton button = kWKEventMouseButtonNoButton;
	if (changed) {
		type = (buttons & changed) ? kWKEventMouseDown : kWKEventMouseUp;
		button = (changed & 1) ? kWKEventMouseButtonLeftButton : (changed & 2) ? kWKEventMouseButtonRightButton : kWKEventMouseButtonMiddleButton;
	}
	uint32_t held = ((buttons & 1) ? kWKEventMouseButtonsLeft : 0) | ((buttons & 2) ? kWKEventMouseButtonsRight : 0)
		| ((buttons & 4) ? kWKEventMouseButtonsMiddle : 0);
	s_buttons = buttons;
	WKPoint p = WKPointMake (x, y);
	WKPageHandleMouseEvent (s_page, WKMouseEventMake (type, button, held, p, p, 0, wk_mods (mods)));
}

void engine_mouse_leave ()
{
	if (s_client && s_client->statusText) s_client->statusText ("");
}

void engine_wheel (int x, int y, int notches, unsigned mods)
{
	if (!s_page || !notches) return;
	WKPoint p = WKPointMake (x, y);
	WKWheelEvent e = WKWheelEventMake (p, p, WKSizeMake (0, notches * 40.0 * 3), WKSizeMake (0, notches), wk_mods (mods));
	WKPageHandleWheelEvent (s_page, e);
}

// A key code (kapi's) -> Windows' virtual key code, the text it types (UTF-8, "" none).
static int vk_of (long k, unsigned mods, char *text)
{
	text[0] = 0;
	if ((mods & MOD_CTRL) && k >= 1 && k <= 26) return 'A' + (int) k - 1;	// Ctrl+letter (^A = 1)
	switch (k) {
	case KEY_BACKSPACE: return 0x08;
	case KEY_TAB: strcpy (text, "\t"); return 0x09;
	case KEY_ENTER: strcpy (text, "\r"); return 0x0D;
	case 27: return 0x1B;
	case KEY_PGUP: return 0x21;
	case KEY_PGDN: return 0x22;
	case KEY_END: return 0x23;
	case KEY_HOME: return 0x24;
	case KEY_LEFT: return 0x25;
	case KEY_UP: return 0x26;
	case KEY_RIGHT: return 0x27;
	case KEY_DOWN: return 0x28;
	case KEY_DEL: return 0x2E;
	}
	if (k >= KEY_F1 && k <= KEY_F12) return 0x70 + (int) (k - KEY_F1);
	// A character: its UTF-8 (Latin-1 0xA0..0xFF, 0x80 the euro sign: the keymaps' codes).
	unsigned cp = k >= 32 && k <= 126 ? (unsigned) k : k >= 0xA0 && k <= 0xFF ? (unsigned) k : k == 0x80 ? 0x20AC : 0;
	if (!cp) return 0;
	if (cp < 0x80) { text[0] = (char) cp; text[1] = 0; }
	else if (cp < 0x800) { text[0] = (char) (0xC0 | cp >> 6); text[1] = (char) (0x80 | (cp & 0x3F)); text[2] = 0; }
	else { text[0] = (char) (0xE0 | cp >> 12); text[1] = (char) (0x80 | ((cp >> 6) & 0x3F)); text[2] = (char) (0x80 | (cp & 0x3F)); text[3] = 0; }
	if (cp >= 'a' && cp <= 'z') return 'A' + (int) (cp - 'a');
	if ((cp >= 'A' && cp <= 'Z') || (cp >= '0' && cp <= '9') || cp == ' ') return (int) cp;
	switch (cp) {
	case ';': case ':': return 0xBA;
	case '=': case '+': return 0xBB;
	case ',': case '<': return 0xBC;
	case '-': case '_': return 0xBD;
	case '.': case '>': return 0xBE;
	case '/': case '?': return 0xBF;
	case '`': case '~': return 0xC0;
	case '[': case '{': return 0xDB;
	case '\\': case '|': return 0xDC;
	case ']': case '}': return 0xDD;
	case '\'': case '"': return 0xDE;
	}
	return 0xE5;	// (VK_PROCESSKEY: a character with no key of its own -- its text is what counts)
}

void engine_key (long k, unsigned mods)
{
	if (!s_page) return;
	char text[8];
	int vk = vk_of (k, mods, text);
	if (!vk) return;
	uint32_t m = wk_mods (mods);
	WKPageHandleKeyboardEvent (s_page, WKKeyboardEventMake (kWKEventKeyDown, vk, text, m, kWKKeyboardEventFlagsNone));
	WKPageHandleKeyboardEvent (s_page, WKKeyboardEventMake (kWKEventKeyUp, vk, "", m, kWKKeyboardEventFlagsNone));
}

// The page did not use a key: back to the window (its shortcuts), as the code it came as.
static void keyNotHandled (WKViewRef, WKKeyboardEvent e, const void *)
{
	if (e.type != kWKEventKeyDown || !s_client->keyNotHandled) return;
	unsigned mods = ((e.modifiers & kWKEventModifiersShiftKey) ? MOD_SHIFT : 0) | ((e.modifiers & kWKEventModifiersControlKey) ? MOD_CTRL : 0)
		| ((e.modifiers & kWKEventModifiersAltKey) ? MOD_ALT : 0);
	long k = 0;
	switch (e.virtualKeyCode) {
	case 0x25: k = KEY_LEFT; break;
	case 0x27: k = KEY_RIGHT; break;
	case 0x26: k = KEY_UP; break;
	case 0x28: k = KEY_DOWN; break;
	case 0x24: k = KEY_HOME; break;
	case 0x23: k = KEY_END; break;
	case 0x21: k = KEY_PGUP; break;
	case 0x22: k = KEY_PGDN; break;
	case 0x08: k = KEY_BACKSPACE; break;
	case 0x1B: k = 27; break;
	default:
		if (e.virtualKeyCode >= 0x70 && e.virtualKeyCode <= 0x7B) k = KEY_F1 + (e.virtualKeyCode - 0x70);
		else if ((mods & MOD_CTRL) && e.virtualKeyCode >= 'A' && e.virtualKeyCode <= 'Z') k = e.virtualKeyCode - 'A' + 1;
	}
	if (k) s_client->keyNotHandled (k, mods);
}
