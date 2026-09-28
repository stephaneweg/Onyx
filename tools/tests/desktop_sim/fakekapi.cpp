//
// fakekapi.cpp -- a host (PC) stand-in for the kernel, to run Onyx's wtk apps (user/Apps/<app>/
// main.cpp) on Linux and see what they draw: their client canvas AND their window frame (the two
// chrome copies wtk draws, kapi v28 get_chrome), as the compositor would show them.
//
// The apps call the kernel through the kapi table at a fixed address (kern/kapi_abi.h:
// KAPI_TABLE_VA); here a table is mapped there and filled with host functions: files are read
// from sdcard/ ("SD:/x" -> sdcard/x), the window's canvas is a plain buffer, pump_events plays a
// script of pointer / key events, and a "dump" step writes the window to a file (then
// tools/tests/desktop_sim/compose.py lays the dumps over a wallpaper as the compositor would).
//
// The script (environment SIM, ';'-separated), one step at each msleep () of the app:
//   wait                 nothing (a turn of the app's loop)
//   down X Y / up X Y / move X Y / wheel X Y N    pointer events (client coordinates)
//   key CODE             a key (a KEY_* number, or a character)
//   winctl N             a title button for the app (GUI_EVENT_WINCTL: 0 the window menu, 2 maximise)
//   dump FILE            the window: "ELSM" w h x y (int32), then w * h pixels 0xTTRRGGBB
//                        (TT = transparency: 0 opaque) -- its frame (the active copy, or the
//                        inactive one with SIM_INACTIVE=1) around its client canvas
//   exit
// SIM_POS="x,y": the window's outer top-left (else centred); SIM_ARGS: the app's arguments;
// SIM_SD: the SD card's directory (default sdcard).
//
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <algorithm>
#include <string>
#include <vector>
#include "kapi.h"

// The kernel's frame metrics (kernel/include/kern/gui/window.h: WIN_TITLEBAR_H, WIN_BORDER).
#ifndef SIM_TITLE_H
#define SIM_TITLE_H	28
#endif
#ifndef SIM_BORDER
#define SIM_BORDER	4
#endif

static TKApiTable *T;
static unsigned *g_canvas; static int g_cw, g_ch, g_x, g_y, g_lw, g_lh; static unsigned g_flags;
static unsigned *g_act, *g_ina; static int g_ow, g_oh;			// the chrome copies
static char g_title[48];
static gui_handler g_ptr, g_key;
static unsigned g_ticks = 1000;
static std::vector<std::string> g_script; static size_t g_step;

static std::string sdpath (const char *p)
{
	std::string s (p);
	if (s.compare (0, 3, "SD:") == 0) s = s.substr (3);
	while (!s.empty () && s[0] == '/') s = s.substr (1);
	return std::string (getenv ("SIM_SD") ? getenv ("SIM_SD") : "sdcard") + "/" + s;
}

// ---- the kernel's 8 x 16 font (circle/lib/font8x16.cpp: 16 bytes a glyph from 0x21) --------------
static unsigned char g_font[256][16];
static void load_font (void)
{
	FILE *f = fopen ("circle/lib/font8x16.cpp", "rb");
	if (f == 0) return;
	std::string s; char buf[4096]; size_t n;
	while ((n = fread (buf, 1, sizeof buf, f)) > 0) s.append (buf, n);
	fclose (f);
	size_t a = s.find ("font_data[]"); if (a == std::string::npos) return;
	a = s.find ('{', a); size_t b = s.find ("};", a);
	std::string body = s.substr (a, b - a);
	std::vector<int> v;
	for (size_t i = 0; i < body.size (); i++)
	{
		if (body[i] == '/' && i + 1 < body.size () && body[i + 1] == '/') { i = body.find ('\n', i); if (i == std::string::npos) break; continue; }
		if (body[i] == '0' && i + 1 < body.size () && (body[i + 1] == 'x' || body[i + 1] == 'X')) { v.push_back ((int) strtol (body.c_str () + i, 0, 16)); i += 3; }
	}
	for (size_t g = 0; g * 16 + 15 < v.size () && 0x21 + g < 256; g++)
		for (int r = 0; r < 16; r++) g_font[0x21 + g][r] = (unsigned char) v[g * 16 + r];
}
static void draw_text_buf (unsigned *dst, int dw, int dh, int x, int y, const char *s, unsigned c)
{
	for (int i = 0; s && s[i]; i++)
		for (int r = 0; r < 16; r++)
		{
			unsigned bits = g_font[(unsigned char) s[i]][r];
			for (int k = 0; k < 8; k++)
				if (bits & (0x80 >> k))
				{
					int xx = x + i * 8 + k, yy = y + r;
					if (xx >= 0 && xx < dw && yy >= 0 && yy < dh) dst[yy * dw + xx] = c;
				}
		}
}

// ---- files ---------------------------------------------------------------------------------------
static void *f_open (const char *p) { return fopen (sdpath (p).c_str (), "rb"); }
static int f_read (void *h, void *b, unsigned n) { return (int) fread (b, 1, n, (FILE *) h); }
static int f_write (int fd, const void *b, unsigned n) { return (int) fwrite (b, 1, n, fd == 2 ? stderr : stdout); }
static int save_file (const char *p, const void *b, unsigned n) { FILE *f = fopen (sdpath (p).c_str (), "wb"); if (!f) return -1; fwrite (b, 1, n, f); fclose (f); return 0; }
static unsigned f_fsize (void *h) { FILE *f = (FILE *) h; long c = ftell (f); fseek (f, 0, SEEK_END); long n = ftell (f); fseek (f, c, SEEK_SET); return (unsigned) n; }
static void f_close (void *h) { fclose ((FILE *) h); }
struct SimDir { DIR *d; std::string path; };
static void *f_opendir (const char *p) { DIR *d = opendir (sdpath (p).c_str ()); if (!d) return 0; return new SimDir { d, sdpath (p) }; }
static int f_readdir (void *h, struct kapi_dirent *e)
{
	SimDir *sd = (SimDir *) h;
	for (;;)
	{
		dirent *de = readdir (sd->d); if (!de) return 0;
		if (!strcmp (de->d_name, ".") || !strcmp (de->d_name, "..")) continue;
		memset (e, 0, sizeof *e); snprintf (e->name, sizeof e->name, "%s", de->d_name);
		struct stat st; std::string full = sd->path + "/" + de->d_name;
		if (stat (full.c_str (), &st) == 0) { e->size = (unsigned) st.st_size; e->is_dir = S_ISDIR (st.st_mode) ? 1 : 0; }
		return 1;
	}
}
static void f_closedir (void *h) { SimDir *sd = (SimDir *) h; closedir (sd->d); delete sd; }
static int list_apps (char *buf, unsigned cap)
{
	std::vector<std::string> v;
	DIR *d = opendir (sdpath ("SD:/apps").c_str ());
	if (d) { while (dirent *e = readdir (d)) { std::string n = e->d_name; if (n.size () > 4 && n.substr (n.size () - 4) == ".app") v.push_back (n.substr (0, n.size () - 4)); } closedir (d); }
	std::sort (v.begin (), v.end ());
	std::string all; for (auto &n : v) all += n + "\n";
	snprintf (buf, cap, "%s", all.c_str ());
	return (int) v.size ();
}

// ---- the window ------------------------------------------------------------------------------------
static void place (int ow, int oh)
{
	const char *p = getenv ("SIM_POS");
	if (p && sscanf (p, "%d,%d", &g_x, &g_y) == 2) return;
	g_x = (1024 - ow) / 2; g_y = 32 + (736 - (oh > 736 ? 736 : oh)) / 2;
}
static void make_chrome (void)
{
	free (g_act); free (g_ina); g_act = g_ina = 0; g_ow = g_lw; g_oh = g_lh;
	if (g_flags & WIN_FLAG_BORDERLESS) return;
	g_ow = g_lw + 2 * SIM_BORDER; g_oh = g_lh + SIM_TITLE_H + SIM_BORDER;
	g_act = (unsigned *) calloc ((size_t) g_ow * g_oh, 4); g_ina = (unsigned *) calloc ((size_t) g_ow * g_oh, 4);
}
static unsigned *create_ex (int x, int y, int w, int h, const char *t, unsigned f)
{
	g_canvas = (unsigned *) calloc ((size_t) w * h, 4); g_cw = w; g_ch = h; g_lw = w; g_lh = h; g_flags = f;
	snprintf (g_title, sizeof g_title, "%s", t ? t : "");
	make_chrome ();
	if (getenv ("SIM_POS")) place (g_ow, g_oh); else { g_x = x; g_y = y; }
	return g_canvas;
}
static unsigned *create (int w, int h, const char *t)
{
	unsigned *p = create_ex (0, 0, w, h, t, 0);
	place (g_ow, g_oh);
	return p;
}
static unsigned *resize (int w, int h)
{
	if ((size_t) w * h > (size_t) g_cw * g_ch) { g_canvas = (unsigned *) realloc (g_canvas, (size_t) w * h * 4); g_cw = w; g_ch = h; }
	g_lw = w; g_lh = h; make_chrome ();
	return g_canvas;
}
static void move_window (int x, int y) { g_x = x; g_y = y; }
// (v64) the work area: the screen less the menu bar (30) and the dock (84)
static int win_geometry (struct kapi_win_geom *o)
{
	memset (o, 0, sizeof *o);
	o->x = g_x; o->y = g_y; o->w = g_ow; o->h = g_oh; o->cw = g_lw; o->ch = g_lh;
	o->ax = 0; o->ay = 30; o->aw = 1024; o->ah = 768 - 30 - 84; o->state = KAPI_WIN_KEYS;
	return 0;
}
static int win_minimise (unsigned id) { fprintf (stderr, "sim: win_minimise %u\n", id); return 0; }
static unsigned *resize2 (int w, int h, int *stride)
{
	if (w > g_cw || h > g_ch)
	{
		int nw = w > g_cw ? w : g_cw, nh = h > g_ch ? h : g_ch;
		unsigned *n = (unsigned *) calloc ((size_t) nw * nh, 4);
		free (g_canvas); g_canvas = n; g_cw = nw; g_ch = nh;
	}
	g_lw = w; g_lh = h; make_chrome ();
	if (stride) *stride = g_cw;
	return g_canvas;
}
static void set_ptr (gui_handler h) { g_ptr = h; }
static void set_key (gui_handler h) { g_key = h; }
static void screen_size (int *w, int *h) { if (w) *w = 1024; if (h) *h = 768; }
static int font_w (void) { return 8; }
static int font_h (void) { return 16; }
static void h_present (void) {}
static int get_chrome (struct kapi_chrome *out)
{
	memset (out, 0, sizeof *out);
	out->content = g_canvas; out->content_w = g_lw; out->content_h = g_lh;
	if (g_act == 0) return 1;
	out->active = g_act; out->inactive = g_ina; out->chrome_w = g_ow; out->chrome_h = g_oh;
	out->inset_l = SIM_BORDER; out->inset_r = SIM_BORDER; out->inset_t = SIM_TITLE_H; out->inset_b = SIM_BORDER;
	snprintf (out->title, sizeof out->title, "%s", g_title);
	return 1;
}

static void dump (const char *file)
{
	FILE *f = fopen (file, "wb");
	int hdr[5] = { 0x4D534C45, g_ow, g_oh, g_x, g_y };		// "ELSM"
	fwrite (hdr, 4, 5, f);
	unsigned *frame = getenv ("SIM_INACTIVE") ? g_ina : g_act;
	std::vector<unsigned> px ((size_t) g_ow * g_oh);
	for (int j = 0; j < g_oh; j++)
		for (int i = 0; i < g_ow; i++)
		{
			int cx = i - (g_act ? SIM_BORDER : 0), cy = j - (g_act ? SIM_TITLE_H : 0);
			bool client = cx >= 0 && cy >= 0 && cx < g_lw && cy < g_lh;
			px[(size_t) j * g_ow + i] = client ? g_canvas[(size_t) cy * g_cw + cx]
							   : frame[(size_t) j * g_ow + i];
		}
	if (!(g_flags & (1u << 5)))					// (not a see-through window: its client opaque)
		for (int j = 0; j < g_lh; j++)
			for (int i = 0; i < g_lw; i++) px[(size_t) (j + (g_act ? SIM_TITLE_H : 0)) * g_ow + i + (g_act ? SIM_BORDER : 0)] &= 0x00FFFFFFu;
	fwrite (px.data (), 4, px.size (), f);
	fclose (f);
	fprintf (stderr, "sim: dumped %dx%d at %d,%d -> %s\n", g_ow, g_oh, g_x, g_y, file);
}

// ---- the event script -------------------------------------------------------------------------------
static void pump (void) {}
static void step (void)
{
	if (g_step >= g_script.size ()) { fprintf (stderr, "sim: end of the script\n"); exit (0); }
	std::string st = g_script[g_step++];
	char cmd[32] = "", arg[256] = ""; int a = 0, b = 0, c = 0;
	sscanf (st.c_str (), "%31s", cmd);
	auto ptrev = [] (int ev, int x, int y, int btn, int chg, int wheel)
	{
		long v = ((long) (wheel & 0xFF) << 48) | ((long) chg << 40) | ((long) btn << 32) | ((long) x << 16) | (long) y;
		if (g_ptr) g_ptr (0, ev, v);
	};
	if (!strcmp (cmd, "down")) { sscanf (st.c_str (), "%*s %d %d", &a, &b); ptrev (GUI_EVENT_PTR_DOWN, a, b, 1, 1, 0); }
	else if (!strcmp (cmd, "up")) { sscanf (st.c_str (), "%*s %d %d", &a, &b); ptrev (GUI_EVENT_PTR_UP, a, b, 0, 1, 0); }
	else if (!strcmp (cmd, "move")) { sscanf (st.c_str (), "%*s %d %d", &a, &b); ptrev (GUI_EVENT_PTR_MOVE, a, b, 0, 0, 0); }
	else if (!strcmp (cmd, "wheel")) { sscanf (st.c_str (), "%*s %d %d %d", &a, &b, &c); ptrev (GUI_EVENT_PTR_WHEEL, a, b, 0, 0, c); }
	else if (!strcmp (cmd, "key")) { sscanf (st.c_str (), "%*s %255s", arg); long k = arg[1] ? strtol (arg, 0, 0) : arg[0]; if (g_key) g_key (0, GUI_EVENT_KEY, k); }
	else if (!strcmp (cmd, "winctl")) { sscanf (st.c_str (), "%*s %d", &a); if (g_ptr) g_ptr (0, GUI_EVENT_WINCTL, a); }
	else if (!strcmp (cmd, "dump")) { sscanf (st.c_str (), "%*s %255s", arg); dump (arg); }
	else if (!strcmp (cmd, "exit")) exit (0);
}
static void h_msleep (unsigned ms) { g_ticks += ms / 10 + 1; step (); }
static unsigned h_get_ticks (void) { return g_ticks; }
static int h_should_exit (void) { return 0; }
static void yield (void) {}

// ---- the system --------------------------------------------------------------------------------------
static int get_datetime (int *y, int *mo, int *d, int *h, int *mi, int *s)
{ if (y) *y = 2026; if (mo) *mo = 9; if (d) *d = 28; if (h) *h = 12; if (mi) *mi = 34; if (s) *s = 0; return 1; }
static int net_status (char *ip, unsigned n) { if (ip && n) snprintf (ip, n, "192.168.1.42"); return 1; }
static int sound_volume (int, int) { return 7; }
static int stdout_write (const void *b, unsigned n) { return (int) fwrite (b, 1, n, stderr); }
static void *sbrk (long n) { static char *arena = (char *) malloc (512u << 20), *top = arena; char *p = top; top += n; return p; }
static int pad_state (int, struct kapi_pad *) { return 0; }
static unsigned get_mods (void) { return 0; }
static int launch (const char *n) { fprintf (stderr, "sim: launch %s\n", n); return 1; }
static int raise_app (const char *n) { fprintf (stderr, "sim: raise_app %s\n", n); return 0; }
static int exec (const char *p, const char *a) { fprintf (stderr, "sim: exec %s %s\n", p, a); return 1; }
static int exec_as (const char *p, const char *a, const char *n) { fprintf (stderr, "sim: exec_as %s %s (%s)\n", p, a, n); return 1; }
static int menu_command (int id) { fprintf (stderr, "sim: menu_command %d\n", id); return 1; }
static int set_menu (const char *, gui_handler) { return 1; }
static unsigned get_menu (char *buf, unsigned cap, char *title, unsigned tcap)
{
	const char *m = getenv ("SIM_MENU");				// "Title|spec", "\n" written '/', '\t' '~'
	if (m == 0 || !*m) { if (buf && cap) buf[0] = 0; if (title && tcap) title[0] = 0; return 0; }
	std::string s (m); size_t bar = s.find ('|');
	std::string t = s.substr (0, bar), spec = bar == std::string::npos ? "" : s.substr (bar + 1);
	std::string sp; for (char ch : spec) sp += ch == '/' ? '\n' : ch == '~' ? '\t' : ch;
	snprintf (buf, cap, "%s", sp.c_str ()); snprintf (title, tcap, "%s", t.c_str ());
	return 5;
}
// SIM_RUNNING="terminal,tetris": the apps with a window
static int list_windows (char *b, unsigned n)
{
	const char *r = getenv ("SIM_RUNNING");
	std::string s; int k = 0;
	if (r) for (const char *p = r; *p; p++) { if (*p == ',') { s += '\n'; k++; } else s += *p; }
	if (r && *r) { s += '\n'; k++; }
	if (b && n) snprintf (b, n, "%s", s.c_str ());
	return k;
}
// SIM_WALL=RRGGBB: the wallpaper the apps read (the agenda's ink); none: a dark blue
static unsigned *wallpaper_buffer (int *w, int *h)
{
	static unsigned *buf;
	if (!buf)
	{
		buf = (unsigned *) malloc (1024 * 768 * 4);
		const char *e = getenv ("SIM_WALL");
		unsigned c = e ? (unsigned) strtoul (e, 0, 16) : 0x00283C58;
		for (int i = 0; i < 1024 * 768; i++) buf[i] = c;
	}
	if (w) *w = 1024; if (h) *h = 768;
	return buf;
}
static int mailbox_recv (int *, int *, void *, unsigned, int) { return -1; }
static int get_keymap (char *b, unsigned n) { if (b && n) snprintf (b, n, "FR"); return 2; }
// (more of the system, answered simply: enough for the apps to show themselves)
static int app_dir (char *b, unsigned n) { if (b && n) snprintf (b, n, "SD:apps/app.app/"); return (int) strlen (b); }
static int f_mkdir (const char *p) { return mkdir (sdpath (p).c_str (), 0755) == 0 ? 0 : -1; }
static int f_remove (const char *) { return -1; }
static int f_rename (const char *, const char *) { return -1; }
static int list_tasks (char *b, unsigned n) { if (b && n) snprintf (b, n, "Rk idle\nSk compositor\nRa menubar\nRa dock\nRa terminal\n"); return 5; }
static int sound_acquire (void) { return -1; }
static void sound_release (void) {}
static int sound_start (int, unsigned, int, int) { return -1; }
static int sound_stop (int) { return -1; }
static int sound_write (const short *, unsigned) { return -1; }
static int sound_status (unsigned *r, unsigned *f, unsigned *o) { if (r) *r = 44100; if (f) *f = 0; if (o) *o = 0; return -1; }
static int proc_done (void *) { return 1; }
static int h_wait (void *) { return 0; }
static int stream_read (void *, void *, unsigned) { return 0; }
static int stream_read_nb (void *, void *, unsigned) { return 0; }
static int stream_write (void *, const void *, unsigned n) { return (int) n; }
static void stream_eof (void *) {}
static int stdin_read (void *, unsigned) { return 0; }
static int f_seek (void *h, unsigned long long pos) { return fseek ((FILE *) h, (long) pos, SEEK_SET) == 0 ? 0 : -1; }
static unsigned long long f_fsize64 (void *h) { return f_fsize (h); }
static int net_info (char *b, unsigned n) { if (b && n) snprintf (b, n, "ip 192.168.1.42\n"); return 1; }
static void h_exit (int st) { fprintf (stderr, "sim: exit %d\n", st); exit (0); }
static int toggle_app (const char *n) { fprintf (stderr, "sim: toggle_app %s\n", n); return 1; }
static int ram_detail (unsigned long *a, unsigned long *b, unsigned long *c, unsigned long *d, unsigned *e)
{ if (a) *a = 4194304; if (b) *b = 3145728; if (c) *c = 2097152; if (d) *d = 0; if (e) *e = 1; return 1; }
static int s_wheel = 2;
static void set_wheel (int v) { s_wheel = v; }
static int get_wheel (void) { return s_wheel; }
static int h_kill (const char *n) { fprintf (stderr, "sim: kill %s\n", n); return 1; }
static int set_keymap_data (const char *, const void *, unsigned) { return 1; }
static int mailbox_send (int, int, const void *, unsigned) { return 0; }
static int drag_begin (int, const void *, unsigned, const char *) { return 0; }
static void *spawn (const char *p, const char *a, void *, void *) { fprintf (stderr, "sim: spawn %s %s\n", p, a ? a : ""); return 0; }
static void *h_pipe (void) { return 0; }
static void stream_close (void *) {}
static int get_args (char *b, unsigned n) { const char *a = getenv ("SIM_ARGS"); snprintf (b, n, "%s", a ? a : ""); return (int) strlen (b); }
static int clipboard_set (int, const void *, unsigned) { return 1; }
static int clipboard_get (int *t, void *, unsigned, unsigned *serial) { if (t) *t = 0; if (serial) *serial = 0; return 0; }
static void set_click (gui_handler) {}
static int key_held (int) { return 0; }
static void cursor_pos (int *x, int *y) { if (x) *x = -1; if (y) *y = -1; }
static void set_alpha (int) {}
static int ipc_register (const char *) { return 0; }
static int ipc_lookup (const char *) { return 0; }
static int shell_request (int, const void *, unsigned) { return -1; }	// (not in the activity shell)
static int random_fill (void *b, unsigned n) { for (unsigned i = 0; i < n; i++) ((unsigned char *) b)[i] = (unsigned char) rand (); return (int) n; }

static void unimplemented (void)
{
	fprintf (stderr, "sim: an unimplemented kapi was called (from %p)\n", __builtin_return_address (0));
	exit (2);
}

static void setup (void)
{
	void *p = mmap ((void *) KAPI_TABLE_VA, 65536, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
	if (p == MAP_FAILED) { perror ("mmap"); exit (1); }
	T = (TKApiTable *) p;
	void **slots = (void **) T;
	for (size_t i = 0; i < sizeof (TKApiTable) / sizeof (void *); i++) slots[i] = (void *) unimplemented;
	T->version = KAPI_ABI_VERSION;
	T->create_window = create; T->create_window_ex = create_ex; T->resize_window = resize; T->move_window = move_window;
	T->set_pointer_handler = set_ptr; T->set_key_handler = set_key; T->screen_size = screen_size;
	T->font_width = font_w; T->font_height = font_h; T->present = h_present; T->pump_events = pump;
	T->msleep = h_msleep; T->get_ticks = h_get_ticks; T->should_exit = h_should_exit; T->yield = yield;
	T->open = f_open; T->read = f_read; T->write = f_write; T->fsize = f_fsize; T->close = f_close; T->save_file = save_file;
	T->opendir = f_opendir; T->readdir = f_readdir; T->closedir = f_closedir; T->list_apps = list_apps;
	T->get_datetime = get_datetime; T->net_status = net_status; T->sound_volume = sound_volume;
	T->stdout_write = stdout_write; T->sbrk = sbrk; T->pad_state = pad_state; T->get_modifiers = get_mods;
	T->launch = launch; T->raise_app = raise_app; T->exec = exec; T->exec_as = exec_as;
	T->menu_command = menu_command; T->set_menu = set_menu; T->get_menu = get_menu;
	T->list_windows = list_windows; T->draw_text_buf = draw_text_buf;
	T->get_chrome = get_chrome; T->get_args = get_args; T->clipboard_set = clipboard_set;
	T->clipboard_get = clipboard_get; T->set_click_handler = set_click; T->key_held = key_held;
	T->cursor_pos = cursor_pos; T->set_window_alpha = set_alpha; T->random = random_fill;
	T->ipc_register = ipc_register; T->ipc_lookup = ipc_lookup;
	T->shell_request = shell_request;
	T->win_minimise = win_minimise; T->win_geometry = win_geometry; T->resize_window2 = resize2;
	T->mailbox_recv = mailbox_recv; T->mailbox_send = mailbox_send; T->drag_begin = drag_begin;
	T->spawn = spawn; T->pipe = h_pipe; T->stream_close = stream_close;
	T->wallpaper_buffer = wallpaper_buffer;
	T->get_keymap = get_keymap; T->set_wheel_speed = set_wheel; T->get_wheel_speed = get_wheel;
	T->kill = h_kill; T->set_keymap_data = set_keymap_data;
	T->app_dir = app_dir; T->mkdir = f_mkdir; T->remove = f_remove; T->rename = f_rename; T->list_tasks = list_tasks;
	T->sound_acquire = sound_acquire; T->sound_release = sound_release; T->sound_start = sound_start;
	T->sound_stop = sound_stop; T->sound_write = sound_write; T->sound_status = sound_status;
	T->proc_done = proc_done; T->wait = h_wait; T->stream_read = stream_read; T->stream_read_nb = stream_read_nb;
	T->stream_write = stream_write; T->stream_eof = stream_eof; T->stdin_read = stdin_read;
	T->seek = f_seek; T->fsize64 = f_fsize64; T->net_info = net_info; T->exit = h_exit; T->toggle_app = toggle_app;
	T->ram_detail = ram_detail;
	load_font ();
	const char *sc = getenv ("SIM");
	std::string s = sc ? sc : "wait;dump out.elsm;exit";
	size_t i = 0;
	while (i <= s.size ()) { size_t j = s.find (';', i); if (j == std::string::npos) j = s.size (); if (j > i) g_script.push_back (s.substr (i, j - i)); i = j + 1; }
}

// (a static object's constructor, after the globals above: a constructor-attribute function would
// run before std::vector's own initialisation and see its work undone)
static struct SimInit { SimInit () { setup (); } } s_init;
