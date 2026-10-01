//
// pc/macOS/headless.cpp -- the screen's half of the host kapi without a screen, for pc/macOS/check.sh on
// Linux: the same hostkapi.cpp (files, the card's two folders, programs started, arguments) under Ledger
// and Writer, driven by a script of events, their window written to a picture at the end.
//
//   HEADLESS_SCRIPT   ';'-separated steps, one at each turn of the app's loop (pump_events):
//                     wait | down X Y | up X Y | move X Y | key CODE | mods N | menu ID | drop PATH | quit
//                     (after the last one: 20 turns more, then quit); none: 200 turns, then quit
//   HEADLESS_DUMP     the window written there at the end (a binary PPM)
// Both are taken from the environment (a program started by the app does not run the script again).
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <string>
#include <vector>
#include "host.h"

static unsigned *g_canvas; static int g_cw, g_ch;
static char g_title[64];
static gui_handler g_ptr, g_key, g_click, g_menuFn;
static bool g_quit, g_made;
static unsigned g_mods;
static std::string g_drop, g_dump, g_clip;
static std::vector<std::string> g_steps;
static size_t g_step;
static int g_left = 200;
struct Ev { int to; int ev; long v; };
static std::vector<Ev> g_evq;
static void push (int to, int ev, long v) { g_evq.push_back (Ev { to, ev, v }); }
static long ptrval (int x, int y, int b, int changed) { return ((long) (changed & 0xFF) << 40) | ((long) (b & 0xFF) << 32) | ((long) x << 16) | (long) y; }

static void dump ()
{
	if (g_dump.empty () || !g_canvas) return;
	FILE *f = fopen (g_dump.c_str (), "wb");
	if (!f) return;
	fprintf (f, "P6\n%d %d\n255\n", g_cw, g_ch);
	for (int i = 0; i < g_cw * g_ch; i++) { unsigned p = g_canvas[i]; unsigned char c[3] = { (unsigned char) (p >> 16), (unsigned char) (p >> 8), (unsigned char) p }; fwrite (c, 1, 3, f); }
	fclose (f);
	g_dump.clear ();
}
static void step ()
{
	if (g_step >= g_steps.size ()) { if (--g_left <= 0) g_quit = true; return; }
	std::string s = g_steps[g_step++];
	int x = 0, y = 0; char w[32] = ""; char arg[512] = "";
	sscanf (s.c_str (), "%31s", w);
	if (!strcmp (w, "down") && sscanf (s.c_str (), "%*s %d %d", &x, &y) == 2)
	{ push (0, GUI_EVENT_PTR_DOWN, ptrval (x, y, 1, 1)); push (3, GUI_EVENT_CANVAS_CLICK, (1l << 32) | ((long) x << 16) | y); }
	else if (!strcmp (w, "up") && sscanf (s.c_str (), "%*s %d %d", &x, &y) == 2) push (0, GUI_EVENT_PTR_UP, ptrval (x, y, 0, 1));
	else if (!strcmp (w, "move") && sscanf (s.c_str (), "%*s %d %d", &x, &y) == 2) push (0, GUI_EVENT_PTR_MOVE, ptrval (x, y, 0, 0));
	else if (!strcmp (w, "key") && sscanf (s.c_str (), "%*s %d", &x) == 1) push (1, GUI_EVENT_KEY, x);
	else if (!strcmp (w, "mods") && sscanf (s.c_str (), "%*s %d", &x) == 1) g_mods = (unsigned) x;
	else if (!strcmp (w, "menu") && sscanf (s.c_str (), "%*s %d", &x) == 1) push (2, GUI_EVENT_MENU, x);
	else if (!strcmp (w, "drop") && sscanf (s.c_str (), "%*s %511[^\n]", arg) == 1) { g_drop = arg; push (0, GUI_EVENT_DROP, ((long) DND_F_COPY << 32) | (10l << 16) | 10); }
	else if (!strcmp (w, "quit")) g_quit = true;
	if (g_step >= g_steps.size ()) g_left = 20;
}

static void read_env ();
static unsigned *make_canvas (int w, int h) { unsigned *p = (unsigned *) calloc ((size_t) w * h, 4); return p; }
static unsigned *create_ex (int, int, int w, int h, const char *title, unsigned)
{
	if (g_made) return 0;
	g_made = true;
	read_env ();
	snprintf (g_title, sizeof g_title, "%s", title ? title : "Onyx");
	g_canvas = make_canvas (w, h); g_cw = w; g_ch = h;
	push (0, GUI_EVENT_WINCTL, KAPI_FRAME_MAXIMISE);		// (as cocoa.mm: the window's size is the app's)
	return g_canvas;
}
static unsigned *create (int w, int h, const char *t) { return create_ex (0, 0, w, h, t, 0); }
static unsigned *resize2 (int w, int h, int *stride)
{
	if (w < 1 || h < 1) return 0;
	unsigned *p = make_canvas (w, h);
	if (!p) return 0;
	free (g_canvas); g_canvas = p; g_cw = w; g_ch = h;
	if (stride) *stride = w;
	return p;
}
static unsigned *resize (int w, int h) { return resize2 (w, h, 0); }
static void move_window (int, int) { }
static void h_present (void) { }
static void screen_size (int *w, int *h) { if (w) *w = 1440; if (h) *h = 900; }
static int win_geometry (struct kapi_win_geom *g)
{
	if (!g_canvas) return -1;
	g->x = 0; g->y = 0; g->w = g_cw; g->h = g_ch; g->cw = g_cw; g->ch = g_ch;
	g->ax = 0; g->ay = 0; g->aw = g_cw; g->ah = g_ch; g->state = KAPI_WIN_KEYS;
	return 0;
}
static int win_minimise (unsigned) { return 0; }
static int desk (int, int) { return 1 << 8; }
static int win_desk (unsigned, int) { return 0; }
static int get_chrome (struct kapi_chrome *o)
{
	if (!g_canvas) return 0;
	memset (o, 0, sizeof *o);
	o->content = g_canvas; o->content_w = g_cw; o->content_h = g_ch;
	snprintf (o->title, sizeof o->title, "%s", g_title);
	return 1;
}
static void set_ptr (gui_handler f) { g_ptr = f; }
static void set_key (gui_handler f) { g_key = f; }
static void set_click (gui_handler f) { g_click = f; }
static unsigned get_mods (void) { return g_mods; }
static void cursor_pos (int *x, int *y) { if (x) *x = 0; if (y) *y = 0; }
static int font_w (void) { return 8; }
static int font_h (void) { return 16; }
static void draw_text_buf (unsigned *, int, int, int, int, const char *, unsigned) { }
static void draw_text (int, int, const char *, unsigned) { }
static void pump (void)
{
	if (g_made) step ();
	std::vector<Ev> q; q.swap (g_evq);
	for (auto &e : q)
	{
		if (e.to == 0 && g_ptr) g_ptr (0, e.ev, e.v);
		else if (e.to == 1 && g_key) g_key (0, e.ev, e.v);
		else if (e.to == 2 && g_menuFn) g_menuFn (0, e.ev, e.v);
		else if (e.to == 3 && g_click) g_click (0, e.ev, e.v);
	}
	if (g_quit) dump ();
}
static int pump_wait (unsigned) { usleep (1000); pump (); return 0; }
static int post (void (*fn) (void *, long), void *ctx, long v) { fn (ctx, v); return 0; }
static int h_should_exit (void) { return g_quit ? 1 : 0; }
static int set_menu (const char *, gui_handler fn) { g_menuFn = fn; return 1; }
static unsigned get_menu (char *, unsigned, char *, unsigned) { return 0; }
static int menu_command (int id) { if (id == MENU_QUIT) g_quit = true; else push (2, GUI_EVENT_MENU, id); return 1; }
static int drag_data (int *type, void *buf, unsigned cap)
{
	if (type) *type = DND_FILES;
	unsigned n = (unsigned) g_drop.size ();
	memcpy (buf, g_drop.data (), n < cap ? n : cap);
	return (int) n;
}
static int drag_begin (int, const void *, unsigned, const char *) { return 0; }
static int clipboard_set (int, const void *d, unsigned n) { g_clip.assign ((const char *) d, n); return 1; }
static int clipboard_get (int *type, void *buf, unsigned cap, unsigned *serial)
{
	if (serial) *serial = 0;
	if (type) *type = g_clip.empty () ? 0 : CLIP_TEXT;
	memcpy (buf, g_clip.data (), g_clip.size () < cap ? g_clip.size () : cap);
	return (int) g_clip.size ();
}
static void h_exit (int s) { dump (); exit (s); }

static void read_env ()		// (at the window's making: this file's globals are made after gui_setup runs)
{
	const char *s = getenv ("HEADLESS_SCRIPT");
	if (s)
	{
		std::string all = s;
		size_t i = 0;
		while (i <= all.size ())
		{
			size_t j = all.find (';', i); if (j == std::string::npos) j = all.size ();
			std::string st = all.substr (i, j - i);
			while (!st.empty () && st[0] == ' ') st.erase (0, 1);
			if (!st.empty ()) g_steps.push_back (st);
			i = j + 1;
		}
		unsetenv ("HEADLESS_SCRIPT");
	}
	const char *d = getenv ("HEADLESS_DUMP");
	if (d) { g_dump = d; unsetenv ("HEADLESS_DUMP"); }
}

void gui_fatal (const char *msg) { fprintf (stderr, "onyx: %s\n", msg); _exit (2); }

void gui_setup (TKApiTable *T)
{
	T->create_window = create; T->create_window_ex = create_ex; T->resize_window = resize; T->resize_window2 = resize2; T->move_window = move_window;
	T->set_pointer_handler = set_ptr; T->set_key_handler = set_key; T->set_click_handler = set_click; T->screen_size = screen_size;
	T->font_width = font_w; T->font_height = font_h; T->present = h_present; T->pump_events = pump; T->pump_wait = pump_wait; T->post = post;
	T->should_exit = h_should_exit; T->exit = h_exit;
	T->draw_text = draw_text; T->draw_text_buf = draw_text_buf; T->get_chrome = get_chrome; T->win_geometry = win_geometry;
	T->win_minimise = win_minimise; T->desk = desk; T->win_desk = win_desk; T->cursor_pos = cursor_pos; T->get_modifiers = get_mods;
	T->set_menu = set_menu; T->get_menu = get_menu; T->menu_command = menu_command; T->drag_data = drag_data; T->drag_begin = drag_begin;
	T->clipboard_set = clipboard_set; T->clipboard_get = clipboard_get;
}
