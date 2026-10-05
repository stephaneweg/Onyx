//
// fakekapi.cpp -- a host (PC) stand-in for the kernel, to run Onyx's uikit apps (user/Apps/<app>/
// main.cpp) on Linux and see what they draw: their client canvas AND their window frame (the two
// chrome copies uikit draws, kapi v28 get_chrome), as the compositor would show them.
//
// The apps call the kernel through the kapi table at a fixed address (kern/kapi_abi.h:
// KAPI_TABLE_VA); here a table is mapped there and filled with host functions: files are read
// from sdcard/ ("SD:/x" -> sdcard/x), the window's canvas is a plain buffer, pump_events plays a
// script of pointer / key events, and a "dump" step writes the window to a file (then
// tools/tests/desktop_sim/compose.py lays the dumps over a wallpaper as the compositor would).
//
// The script (environment SIM, ';'-separated), one step at each msleep () of the app:
//   wait                 nothing (a turn of the app's loop)
//   down X Y / up X Y / move X Y / wheel X Y N    pointer events (client coordinates; a
//                        press is also a canvas click, a move with a button held a drag)
//   rdown X Y / rup X Y  the right button
//   key CODE             a key (a KEY_* number, or a character)
//   mods N               the modifier keys held from now on (kapi_get_modifiers: 1 Ctrl, 2 Shift, 4 Alt)
//   winctl N             a title button for the app (GUI_EVENT_WINCTL: 0 the window menu, 2 maximise)
//   menu N               the app's menu item N chosen in the menu bar (GUI_EVENT_MENU)
//   dump FILE            the window: "ELSM" w h x y (int32), then w * h pixels 0xTTRRGGBB
//                        (TT = transparency: 0 opaque) -- its frame (the active copy, or the
//                        inactive one with SIM_INACTIVE=1) around its client canvas
//   quit                 the window closed (kapi_should_exit () from now on: the app's loop ends
//                        and what it does before returning from main runs)
//   exit                 the process ends here (what follows the app's loop never runs)
// SIM_POS="x,y": the window's outer top-left (else centred); SIM_ARGS: the app's arguments;
// SIM_SD: the SD card's directory (default sdcard), only read; SIM_WRITES: where what the apps
// save goes (default /tmp/onyx_sim_writes); SIM_OVERLAY: directories ("a:b") whose files are read
// instead of the card's (sample data: tools/tests/desktop_sim/sd); SIM_PIPE: what a spawned
// program (the terminal's shell) writes, read back from its pipe; SIM_NET: what a server sends
// on a TCP connection (irc) -- "\n" a new line, "\r" a return, "\e" an escape; SIM_CURSOR="x,y":
// the pointer for kapi_cursor_pos; SIM_SLEEP=1: msleep really sleeps (an app whose timers read
// the clock: NetSurf); SIM_MENU, SIM_RUNNING, SIM_WALL: below.
// SIM_APPLET=1: the app runs as a Control Panel applet (its arguments "--applet 1 99", the host
// pid 99 alive, a 700 x 470 surface -- dumped instead of a window); SIM_SURFACE=FILE.elsm: the
// pixels a surface is filled with when an applet says hello (SIM_MAIL); SIM_MAIL="type:pid": one
// message of that type from that pid in the mailbox (a Control Panel applet's AP_HELLO: 40:7);
// SIM_MBOX="type:pid:payload\n...": canned mailbox messages (irc's conversation windows), any
// service looked up is pid 7; SIM_DESKS="cur,count": the workspaces (kapi v65); SIM_VOLS: the volumes besides the card (below);
// SIM_WALLDUMP=FILE.elsm: the wallpaper an app makes live (voronoy) written there.
// SIM_GRAB=FILE.elsm: what kapi_screen_grab gives (the screen, e.g. screenshots/desktop.png made an .elsm:
// Screenshot's captures); a full-screen app (kapi_fullscreen_begin) is dumped as its whole buffer. SIM_WINS'
// windows may end with ",title" (kapi_win_list's).
// SIM_RAM: the folder that stands for the RAM: volume (the kernel's RAM file system: until the Pi
// restarts) -- the same folder for several runs is several launches within one boot; unset, each
// run has its own (a fresh temporary folder, deleted at its end: a boot of its own).
// SIM_REALNET=1: the TCP sockets are the PC's (a real connection: an HTTP client against a local
// server); with SIM_SLEEP=1 the script's steps take real time, for the answers to come.
// Threads (kapi v67) are the PC's too (pthreads); kapi_post runs at the next pump_events; a
// thread's msleep only sleeps (the script is stepped by the app's main thread only).
//
#include <sys/mman.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <ftw.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <string>
#include <vector>
#include <set>
#include <map>
#include <deque>
#include "kapi.h"

// The kernel's frame metrics (kernel/include/kern/gui/window.h: WIN_TITLEBAR_H, WIN_BORDER).
#ifndef SIM_TITLE_H
#define SIM_TITLE_H	28
#endif
#ifndef SIM_BORDER
#define SIM_BORDER	4
#endif

/* the host program's last words before a SIM exit (the NetSurf bench: NS_PROF's samples) */
extern "C" void onyx_host_exit_hook (void) __attribute__ ((weak));

static TKApiTable *T;
static unsigned *g_canvas; static int g_cw, g_ch, g_stride, g_x, g_y, g_lw, g_lh; static unsigned g_flags;
static unsigned *g_act, *g_ina; static int g_ow, g_oh;			// the chrome copies
static char g_title[48];
static gui_handler g_ptr, g_key, g_click, g_menuFn; static int g_btn;	// (the buttons held)
static unsigned g_ticks = 1000;
static std::vector<std::string> g_script; static size_t g_step;
static unsigned g_mods;					// (the script's "mods")
static std::string g_dragData;				// (the script's last "drop")
static bool g_quit;					// (the script's "quit": the window closed)

// The card is only READ: what an app writes (a saved file, a folder) goes to SIM_WRITES (default
// /tmp/onyx_sim_writes), read back from there first -- never into sdcard/ nor the samples.
static std::string ramdir (void);
static bool is_ram (const char *p)					// "RAM:..." (the RAM volume)
{
	return p && (p[0] == 'R' || p[0] == 'r') && (p[1] == 'A' || p[1] == 'a') && (p[2] == 'M' || p[2] == 'm') && p[3] == ':';
}
static std::string relpath (const char *p)
{
	std::string s (p ? p : "");
	size_t c = s.find (':');					// "SD:", "SD1:"... -> the card
	if (c != std::string::npos && c < 5) s = s.substr (c + 1);
	while (!s.empty () && s[0] == '/') s = s.substr (1);
	return s;
}
static std::string writes (void) { return getenv ("SIM_WRITES") ? getenv ("SIM_WRITES") : "/tmp/onyx_sim_writes"; }
static std::string sdpath (const char *p)
{
	if (is_ram (p)) return ramdir () + "/" + relpath (p);
	std::string s = relpath (p), card = std::string (getenv ("SIM_SD") ? getenv ("SIM_SD") : "sdcard") + "/" + s;
	struct stat st, cs;
	bool onCard = stat (card.c_str (), &cs) == 0;
	if (stat ((writes () + "/" + s).c_str (), &st) == 0 && (S_ISREG (st.st_mode) || !onCard))	// (a folder of
		return writes () + "/" + s;						// the card's: the card's)
	const char *ov = getenv ("SIM_OVERLAY");			// (sample files, not folders: looked for there next;
	for (std::string dirs = ov ? ov : ""; !dirs.empty (); )	// "a:b": in a, then in b)
	{
		size_t colon = dirs.find (':');
		std::string dir = dirs.substr (0, colon), f = dir + "/" + s;
		if (!dir.empty () && stat (f.c_str (), &st) == 0 && S_ISREG (st.st_mode)) return f;
		dirs = colon == std::string::npos ? "" : dirs.substr (colon + 1);
	}
	return card;
}
static std::string wpath (const char *p)				// where a write goes (its folders made)
{
	std::string base = is_ram (p) ? ramdir () : writes ();
	std::string full = base + "/" + relpath (p);
	mkdir (base.c_str (), 0755);
	if (is_ram (p)) return full;					// (RAM: as the kernel's: no folder made)
	for (size_t i = base.size () + 1; i < full.size (); i++)
		if (full[i] == '/') mkdir (full.substr (0, i).c_str (), 0755);
	return full;
}

// RAM: -- SIM_RAM, or this run's own temporary folder (removed at the end: the next run, a new boot)
static std::string g_ramdir;
static int rm_one (const char *f, const struct stat *, int, struct FTW *) { return remove (f); }
static void ram_cleanup (void) { if (!g_ramdir.empty () && !getenv ("SIM_RAM")) nftw (g_ramdir.c_str (), rm_one, 16, FTW_DEPTH | FTW_PHYS); }
static std::string ramdir (void)
{
	if (!g_ramdir.empty ()) return g_ramdir;
	if (getenv ("SIM_RAM") && getenv ("SIM_RAM")[0]) { g_ramdir = getenv ("SIM_RAM"); mkdir (g_ramdir.c_str (), 0755); return g_ramdir; }
	char t[] = "/tmp/onyx_sim_ram.XXXXXX";
	g_ramdir = mkdtemp (t) ? t : "/tmp/onyx_sim_ram";
	mkdir (g_ramdir.c_str (), 0755);
	atexit (ram_cleanup);
	return g_ramdir;
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
// As the kernel's: the bytes written, or -1 (it answered 0: a caller testing "== 0" passed here, failed on the Pi).
static int save_file (const char *p, const void *b, unsigned n) { FILE *f = fopen (wpath (p).c_str (), "wb"); if (!f) return -1; size_t w = n ? fwrite (b, 1, n, f) : 0; if (fclose (f) != 0) return -1; return (int) w; }
static unsigned f_fsize (void *h) { FILE *f = (FILE *) h; long c = ftell (f); fseek (f, 0, SEEK_END); long n = ftell (f); fseek (f, c, SEEK_SET); return (unsigned) n; }
static void f_close (void *h) { fclose ((FILE *) h); }
struct SimDir { DIR *d; std::string path; };
// SIM_VOLS="SD1,VD0": the other volumes there (the card's partitions 2..4, the disk images) -- none
// by default, as on a card with one partition ("SD:" and "SD0:" are the card).
static bool volume_there (const char *p)
{
	const char *c = p ? strchr (p, ':') : 0;
	if (!c || c - p > 4) return true;
	std::string v (p, (size_t) (c - p));
	if (v == "SD" || v == "SD0" || (v != "VD0" && v != "VD1" && v != "VD2" && v != "VD3" && v != "SD1" && v != "SD2" && v != "SD3")) return true;
	std::string list = std::string (",") + (getenv ("SIM_VOLS") ? getenv ("SIM_VOLS") : "") + ",";
	return list.find ("," + v + ",") != std::string::npos;
}
static void *f_opendir (const char *p)
{
	if (!volume_there (p)) return 0;
	DIR *d = opendir (sdpath (p).c_str ()); if (!d) return 0; return new SimDir { d, sdpath (p) };
}
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
static void screen_size (int *w, int *h);
static unsigned *create_ex (int x, int y, int w, int h, const char *t, unsigned f)
{
	int sw, sh; screen_size (&sw, &sh);
	if (w <= 0 || h <= 0 || w > (sw > 1024 ? sw : 1024) || h > (sh > 768 ? sh : 768))	// (the kernel's limits: the screen)
	{
		fprintf (stderr, "sim: no window for %d x %d (the kernel makes none bigger than the screen)\n", w, h);
		return 0;
	}
	g_canvas = (unsigned *) calloc ((size_t) w * h, 4); g_cw = w; g_ch = h; g_stride = w; g_lw = w; g_lh = h; g_flags = f;
	snprintf (g_title, sizeof g_title, "%s", t ? t : "");
	make_chrome ();
	if (getenv ("SIM_POS")) place (g_ow, g_oh); else { g_x = x; g_y = y; }
	return g_canvas;
}
static unsigned *create (int w, int h, const char *t)
{
	unsigned *p = create_ex (0, 0, w, h, t, 0);
	if (p) place (g_ow, g_oh);
	return p;
}
static unsigned *resize (int w, int h)
{
	if (w > g_cw) w = g_cw;					// (the kernel's SetLogicalSize: within the canvas
	if (h > g_ch) h = g_ch;					// made at create, its rows still g_cw apart)
	if (w < 1) w = 1;
	if (h < 1) h = 1;
	g_lw = w; g_lh = h; make_chrome ();
	return g_canvas;
}
static void move_window (int x, int y) { g_x = x; g_y = y; }
// (v64) the work area: the screen less the menu bar (30) and the dock (84)
static void screen_size (int *w, int *h);
// the window's state (KAPI_WIN_*): the SIM step "winstate <n>" (4 minimised, 8 on another desk, 0 unfocused)
static unsigned g_winstate = KAPI_WIN_KEYS;
static int win_geometry (struct kapi_win_geom *o)
{
	memset (o, 0, sizeof *o);
	int sw, sh; screen_size (&sw, &sh);
	o->x = g_x; o->y = g_y; o->w = g_ow; o->h = g_oh; o->cw = g_lw; o->ch = g_lh;
	o->ax = 0; o->ay = 30; o->aw = sw; o->ah = sh - 30 - 84; o->state = g_winstate;
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
	g_stride = g_cw; g_lw = w; g_lh = h; make_chrome ();
	if (stride) *stride = g_cw;
	return g_canvas;
}
static void set_ptr (gui_handler h) { g_ptr = h; }
static void set_key (gui_handler h) { g_key = h; }
/* the screen: 1024x768, or SIM_SCREEN=<w>x<h> (a bigger desktop for a bigger window) */
static void screen_size (int *w, int *h)
{
	int sw = 1024, sh = 768;
	const char *e = getenv ("SIM_SCREEN");
	if (e && sscanf (e, "%dx%d", &sw, &sh) != 2) { sw = 1024; sh = 768; }
	if (w) *w = sw;
	if (h) *h = sh;
}
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

static unsigned *g_surf; static int g_surfW = 700, g_surfH = 470;	// (the one surface: applets)
static unsigned *g_fs; static int g_fsW, g_fsH;			// (a full-screen app's buffer, while it is)
static unsigned *fs_begin (int *w, int *h)
{
	screen_size (&g_fsW, &g_fsH);
	if (!g_fs) g_fs = (unsigned *) calloc ((size_t) g_fsW * g_fsH, 4);
	if (w) *w = g_fsW; if (h) *h = g_fsH;
	return g_fs;
}
static void fs_present (void) {}
static void fs_end (void) { g_fs = 0; }
static int screen_grab (unsigned *dst, int w, int h)
{
	int sw, sh; screen_size (&sw, &sh);
	if (!dst || w != sw || h != sh) return 0;
	for (long i = 0; i < (long) w * h; i++) dst[i] = 0x3A4A5E;
	const char *e = getenv ("SIM_GRAB");
	FILE *f = e ? fopen (e, "rb") : 0;
	if (!f) return 1;
	int hdr[5] = { 0 };
	if (fread (hdr, 4, 5, f) == 5 && hdr[1] > 0 && hdr[2] > 0)
	{
		std::vector<unsigned> px ((size_t) hdr[1] * hdr[2]);
		if (fread (px.data (), 4, px.size (), f) == px.size ())
			for (int y = 0; y < h && y < hdr[2]; y++)
				for (int x = 0; x < w && x < hdr[1]; x++) dst[(long) y * w + x] = px[(size_t) y * hdr[1] + x] & 0x00FFFFFFu;
	}
	fclose (f);
	return 1;
}
static void dump (const char *file)
{
	if (g_fs)							// a full-screen app: the screen it shows
	{
		FILE *f = fopen (file, "wb");
		int hdr[5] = { 0x4D534C45, g_fsW, g_fsH, 0, 0 };
		fwrite (hdr, 4, 5, f);
		std::vector<unsigned> px ((size_t) g_fsW * g_fsH);
		for (size_t i = 0; i < px.size (); i++) px[i] = g_fs[i] & 0x00FFFFFFu;
		fwrite (px.data (), 4, px.size (), f);
		fclose (f);
		fprintf (stderr, "sim: dumped the full screen %dx%d -> %s\n", g_fsW, g_fsH, file);
		return;
	}
	if (g_canvas == 0 && g_surf)					// an applet: its surface
	{
		FILE *f = fopen (file, "wb");
		int hdr[5] = { 0x4D534C45, g_surfW, g_surfH, 0, 0 };
		fwrite (hdr, 4, 5, f);
		std::vector<unsigned> px ((size_t) g_surfW * g_surfH);
		for (size_t i = 0; i < px.size (); i++) px[i] = g_surf[i] & 0x00FFFFFFu;
		fwrite (px.data (), 4, px.size (), f);
		fclose (f);
		fprintf (stderr, "sim: dumped the applet's surface %dx%d -> %s\n", g_surfW, g_surfH, file);
		return;
	}
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
			px[(size_t) j * g_ow + i] = client ? g_canvas[(size_t) cy * g_stride + cx]
							   : frame[(size_t) j * g_ow + i];
		}
	if (!(g_flags & (1u << 5)))					// (not a see-through window: its client opaque)
		for (int j = 0; j < g_lh; j++)
			for (int i = 0; i < g_lw; i++) px[(size_t) (j + (g_act ? SIM_TITLE_H : 0)) * g_ow + i + (g_act ? SIM_BORDER : 0)] &= 0x00FFFFFFu;
	if (g_flags & (1u << 3))					// (WIN_FLAG_TRANSPARENT: magenta = see-through)
		for (size_t i = 0; i < px.size (); i++) if ((px[i] & 0x00FFFFFFu) == 0x00FF00FFu) px[i] = 0xFF000000u;
	fwrite (px.data (), 4, px.size (), f);
	fclose (f);
	fprintf (stderr, "sim: dumped %dx%d at %d,%d -> %s\n", g_ow, g_oh, g_x, g_y, file);
}

// ---- the event script -------------------------------------------------------------------------------
// ---- threads (kapi v67): pthreads; kapi_post's calls run by the main thread's pump ------------------------
static pthread_t g_mainThread;
static bool g_mainSet;
static bool on_main (void) { return !g_mainSet || pthread_equal (pthread_self (), g_mainThread); }
struct Posted { void (*fn) (void *, long); void *ctx; long v; };
static std::vector<Posted> g_posts;
static pthread_mutex_t g_postLock = PTHREAD_MUTEX_INITIALIZER;
static void run_posts (void)
{
	std::vector<Posted> q;
	pthread_mutex_lock (&g_postLock); q.swap (g_posts); pthread_mutex_unlock (&g_postLock);
	for (auto &p : q) p.fn (p.ctx, p.v);
}
static int h_post (void (*fn) (void *, long), void *ctx, long v)
{
	pthread_mutex_lock (&g_postLock); g_posts.push_back ({ fn, ctx, v }); pthread_mutex_unlock (&g_postLock);
	return 0;
}
struct SimThread { int (*fn) (void *); void *arg; int code; volatile bool done; bool joined; pthread_t th; };
static std::vector<SimThread *> g_threads;
static pthread_mutex_t g_thrLock = PTHREAD_MUTEX_INITIALIZER;
static void *thread_main (void *p) { SimThread *t = (SimThread *) p; t->code = t->fn (t->arg); t->done = true; return 0; }
static int h_thread_create (int (*fn) (void *), void *arg, unsigned stack, const char *)
{
	SimThread *t = new SimThread { fn, arg, 0, false, false, pthread_t () };
	pthread_attr_t a; pthread_attr_init (&a);
	pthread_attr_setstacksize (&a, stack ? std::max (stack, 1u << 20) : 1u << 20);
	pthread_mutex_lock (&g_thrLock);
	if (pthread_create (&t->th, &a, thread_main, t) != 0) { pthread_mutex_unlock (&g_thrLock); delete t; return -1; }
	g_threads.push_back (t);
	int tid = (int) g_threads.size () + 1;
	pthread_mutex_unlock (&g_thrLock);
	return tid;
}
static void h_thread_exit (int code) { (void) code; pthread_exit (0); }
static int h_thread_join (int tid, unsigned ms, int *code)
{
	pthread_mutex_lock (&g_thrLock);
	SimThread *t = tid >= 2 && tid - 2 < (int) g_threads.size () ? g_threads[tid - 2] : 0;
	pthread_mutex_unlock (&g_thrLock);
	if (!t || t->joined) return -2;
	if (ms != 0xFFFFFFFFu) { unsigned w = 0; while (!t->done && w < ms) { usleep (1000); w++; } if (!t->done) return -1; }
	pthread_join (t->th, 0); t->joined = true;
	if (code) *code = t->code;
	return 0;
}
// events (kapi v67): manual or auto reset, on a mutex and a condition (the apps' worker threads)
struct SimEvent { pthread_mutex_t m; pthread_cond_t c; bool manual, set; };
static std::vector<SimEvent *> g_events;
static int h_event_create (int manual, int initial)
{
	SimEvent *e = new SimEvent; pthread_mutex_init (&e->m, 0); pthread_cond_init (&e->c, 0); e->manual = manual != 0; e->set = initial != 0;
	pthread_mutex_lock (&g_thrLock); g_events.push_back (e); int h = (int) g_events.size (); pthread_mutex_unlock (&g_thrLock);
	return h;
}
static SimEvent *ev_of (int h) { pthread_mutex_lock (&g_thrLock); SimEvent *e = h > 0 && h <= (int) g_events.size () ? g_events[h - 1] : 0; pthread_mutex_unlock (&g_thrLock); return e; }
static int h_event_set (int h) { SimEvent *e = ev_of (h); if (!e) return -2; pthread_mutex_lock (&e->m); e->set = true; pthread_cond_broadcast (&e->c); pthread_mutex_unlock (&e->m); return 0; }
static int h_event_reset (int h) { SimEvent *e = ev_of (h); if (!e) return -2; pthread_mutex_lock (&e->m); e->set = false; pthread_mutex_unlock (&e->m); return 0; }
static int h_event_wait (int h, unsigned ms)
{
	SimEvent *e = ev_of (h); if (!e) return -2;
	pthread_mutex_lock (&e->m);
	if (!e->set && ms)
	{
		struct timespec ts; clock_gettime (CLOCK_REALTIME, &ts);
		unsigned w = ms == 0xFFFFFFFFu ? 3600000u : ms;
		ts.tv_sec += w / 1000; ts.tv_nsec += (long) (w % 1000) * 1000000; if (ts.tv_nsec >= 1000000000) { ts.tv_sec++; ts.tv_nsec -= 1000000000; }
		while (!e->set) if (pthread_cond_timedwait (&e->c, &e->m, &ts) != 0) break;
	}
	int r = e->set ? 0 : -1;
	if (e->set && !e->manual) e->set = false;
	pthread_mutex_unlock (&e->m);
	return r;
}
static int h_thread_self (void)
{
	if (on_main ()) return 1;
	pthread_mutex_lock (&g_thrLock);
	int r = 0;
	for (size_t i = 0; i < g_threads.size (); i++) if (pthread_equal (g_threads[i]->th, pthread_self ())) r = (int) i + 2;
	pthread_mutex_unlock (&g_thrLock);
	return r;
}
static void pump (void) { if (on_main ()) run_posts (); }
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
	auto click = [] (int ev, int x, int y)				// (the legacy canvas-click handler too)
	{ if (g_click) g_click (0, ev, ((long) g_btn << 32) | ((long) x << 16) | (long) y); };
	if (!strcmp (cmd, "down") || !strcmp (cmd, "rdown"))
	{
		int bit = cmd[0] == 'r' ? 2 : 1;
		sscanf (st.c_str (), "%*s %d %d", &a, &b); g_btn |= bit;
		ptrev (GUI_EVENT_PTR_DOWN, a, b, g_btn, bit, 0); click (GUI_EVENT_CANVAS_CLICK, a, b);
	}
	else if (!strcmp (cmd, "up") || !strcmp (cmd, "rup"))
	{
		int bit = cmd[0] == 'r' ? 2 : 1;
		sscanf (st.c_str (), "%*s %d %d", &a, &b); g_btn &= ~bit; ptrev (GUI_EVENT_PTR_UP, a, b, g_btn, bit, 0);
	}
	else if (!strcmp (cmd, "move"))
	{
		sscanf (st.c_str (), "%*s %d %d", &a, &b); ptrev (GUI_EVENT_PTR_MOVE, a, b, g_btn, 0, 0);
		if (g_btn) click (GUI_EVENT_CANVAS_MOTION, a, b);
	}
	else if (!strcmp (cmd, "wheel")) { sscanf (st.c_str (), "%*s %d %d %d", &a, &b, &c); ptrev (GUI_EVENT_PTR_WHEEL, a, b, 0, 0, c); }
	else if (!strcmp (cmd, "key")) { sscanf (st.c_str (), "%*s %255s", arg); long k = arg[1] ? strtol (arg, 0, 0) : arg[0]; if (g_key) g_key (0, GUI_EVENT_KEY, k); }
	else if (!strcmp (cmd, "menu")) { sscanf (st.c_str (), "%*s %d", &a); if (g_menuFn) g_menuFn (0, GUI_EVENT_MENU, a); }
	// drag & drop (ABI v42) from another app: "dragover X Y [FLAGS]" (FLAGS 1 = Ctrl, 4 = left),
	// "drop X Y PATH|PATH... [FLAGS]" (the paths a DND_FILES payload, '|' for the newlines)
	else if (!strcmp (cmd, "dragover"))
	{
		c = 0; sscanf (st.c_str (), "%*s %d %d %d", &a, &b, &c);
		if (g_ptr) g_ptr (0, GUI_EVENT_DRAG_OVER, ((long) c << 32) | ((long) a << 16) | (long) b);
	}
	else if (!strcmp (cmd, "drop"))
	{
		c = 0; sscanf (st.c_str (), "%*s %d %d %255s %d", &a, &b, arg, &c);
		g_dragData = arg; for (char &ch : g_dragData) if (ch == '|') ch = '\n';
		if (g_ptr) g_ptr (0, GUI_EVENT_DROP, ((long) c << 32) | ((long) a << 16) | (long) b);
	}
	else if (!strcmp (cmd, "mods")) { sscanf (st.c_str (), "%*s %d", &a); g_mods = (unsigned) a; }
	else if (!strcmp (cmd, "winstate")) { sscanf (st.c_str (), "%*s %d", &a); g_winstate = (unsigned) a; }
	else if (!strcmp (cmd, "winctl")) { sscanf (st.c_str (), "%*s %d", &a); if (g_ptr) g_ptr (0, GUI_EVENT_WINCTL, a); }
	else if (!strcmp (cmd, "dump")) { sscanf (st.c_str (), "%*s %255s", arg); dump (arg); }
	// "waitlog N TEXT": stay on this step (one main-loop turn each) until the app's log (SIM_LOG, the
	// file its stdout/stderr go to) holds TEXT, or N turns passed -- a test waits for what it
	// expects rather than a fixed count of turns (a loaded machine is slower)
	else if (!strcmp (cmd, "waitlog"))
	{
		static size_t turns; static size_t at = (size_t) -1;
		if (at != g_step) { at = g_step; turns = 0; }
		sscanf (st.c_str (), "%*s %d %255[^\n]", &a, arg);
		bool found = false;
		if (const char *lp = getenv ("SIM_LOG"))
			if (FILE *f = fopen (lp, "rb"))
			{
				std::string all; char buf[65536]; size_t n;
				while ((n = fread (buf, 1, sizeof buf, f)) > 0) all.append (buf, n);
				fclose (f);
				found = all.find (arg) != std::string::npos;
			}
		if (!found && (int) ++turns < a) g_step--;
		else at = (size_t) -1;
	}
	else if (!strcmp (cmd, "quit")) g_quit = true;
	else if (!strcmp (cmd, "exit")) { if (onyx_host_exit_hook) onyx_host_exit_hook (); exit (0); }
}
static void h_msleep (unsigned ms)
{
	if (!on_main ()) { usleep (ms * 1000); return; }	// (a thread's: the script is the main thread's)
	g_ticks += ms / 10 + 1;
	if (getenv ("SIM_SLEEP")) usleep (ms * 1000);	// real time (NetSurf's scheduler reads the clock)
	step ();
}
// SIM_REALCLOCK: the ticks are the PC's monotonic clock (a test whose threads wait on the network)
static unsigned h_get_ticks (void)
{
	if (getenv ("SIM_REALCLOCK")) { struct timespec t; clock_gettime (CLOCK_MONOTONIC, &t); return (unsigned) (t.tv_sec * 100 + t.tv_nsec / 10000000); }
	return g_ticks;
}
static int h_should_exit (void) { return g_quit; }
static int h_pump_wait (unsigned ms) { h_msleep (ms < 16 ? ms : 16); pump (); return 0; }
static void yield (void) {}

// ---- the system --------------------------------------------------------------------------------------
static int get_datetime (int *y, int *mo, int *d, int *h, int *mi, int *s)
{
	if (getenv ("SIM_REALNET")) {		// the real network: the real clock (TLS checks the dates)
		time_t t = time (0); struct tm tm; localtime_r (&t, &tm);
		if (y) *y = tm.tm_year + 1900; if (mo) *mo = tm.tm_mon + 1; if (d) *d = tm.tm_mday;
		if (h) *h = tm.tm_hour; if (mi) *mi = tm.tm_min; if (s) *s = tm.tm_sec;
		return 1;
	}
	if (y) *y = 2026; if (mo) *mo = 9; if (d) *d = 28; if (h) *h = 12; if (mi) *mi = 34; if (s) *s = 0; return 1;
}
static int net_status (char *ip, unsigned n) { if (ip && n) snprintf (ip, n, "192.168.1.42"); return 1; }
static int sound_volume (int, int) { return 7; }
static int stdout_write (const void *b, unsigned n) { return (int) fwrite (b, 1, n, stderr); }
static void *h_sbrk (long n) { static char *arena = (char *) malloc (512u << 20), *top = arena; char *p = top; top += n; return p; }
static int pad_state (int, struct kapi_pad *) { return 0; }
static unsigned get_mods (void) { return g_mods; }
static int launch (const char *n)
{
	fprintf (stderr, "sim: launch %s\n", n);
	// (no services without SIM_IPC: clipd cannot come -- clipboard.h falls back at once to the
	// kernel's clipboard instead of waiting a second for it)
	if (!getenv ("SIM_IPC") && n && !strcmp (n, "clipd")) return 0;
	return 1;
}
static int raise_app (const char *n) { fprintf (stderr, "sim: raise_app %s\n", n); return 0; }
static int exec (const char *p, const char *a) { fprintf (stderr, "sim: exec %s %s\n", p, a); return 1; }
static int exec_as (const char *p, const char *a, const char *n) { fprintf (stderr, "sim: exec_as %s %s (%s)\n", p, a, n); return 1; }
static int menu_command (int id) { fprintf (stderr, "sim: menu_command %d\n", id); return 1; }
static int set_menu (const char *, gui_handler h) { g_menuFn = h; return 1; }
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
// the wallpaper made live (voronoy): SIM_WALLDUMP=FILE.elsm -- the buffer written there (a dump)
static void wallpaper_commit (void)
{
	const char *f = getenv ("SIM_WALLDUMP");
	int w, h; unsigned *b = wallpaper_buffer (&w, &h);
	FILE *fp = f ? fopen (f, "wb") : 0;
	if (!fp) return;
	int hdr[5] = { 0x4D534C45, w, h, 0, 0 };			// "ELSM" w h x y
	fwrite (hdr, 4, 5, fp);
	for (int i = 0; i < w * h; i++) { unsigned c = b[i] & 0x00FFFFFFu; fwrite (&c, 4, 1, fp); }
	fclose (fp);
	fprintf (stderr, "sim: the wallpaper -> %s\n", f);
}
static int sipc_recv (int *, int *, void *, unsigned, int);
static int mailbox_recv (int *f, int *t, void *b, unsigned c, int bl) { return getenv ("SIM_IPC") ? sipc_recv (f, t, b, c, bl) : -1; }
// (the BASIC runtime: started as an app -- no stdout --, its folder the current one, no GPU)
static int h_chdir (const char *) { return 0; }
static void *h_stdout_stream (void) { return 0; }
static int h_kbd_ready (void) { return 1; }
static int h_gpu_info (char *b, unsigned n) { if (b && n) snprintf (b, n, "no GPU (the simulator)"); return 0; }
static int get_keymap (char *b, unsigned n) { if (b && n) snprintf (b, n, "FR"); return 2; }
// (more of the system, answered simply: enough for the apps to show themselves)
static int app_dir (char *b, unsigned n)				// SD:apps/<the program's name>.app/
{
	char exe[512]; ssize_t k = readlink ("/proc/self/exe", exe, sizeof exe - 1); exe[k > 0 ? k : 0] = 0;
	const char *name = getenv ("SIM_APP") ? getenv ("SIM_APP") : strrchr (exe, '/') ? strrchr (exe, '/') + 1 : "app";
	if (b && n) snprintf (b, n, "SD:apps/%s.app/", name);
	return b ? (int) strlen (b) : 0;
}
static int f_mkdir (const char *p) { return mkdir (wpath (p).c_str (), 0755) == 0 ? 0 : -1; }
// (the card is only read: a file is removed / renamed on RAM:, or among what the apps wrote --
//  SIM_WRITES --, never on the card)
static bool written (const char *p) { std::string f = sdpath (p), w = writes () + "/"; return f.compare (0, w.size (), w) == 0; }
static int f_remove (const char *p)
{
	if (!is_ram (p) && !written (p)) return -1;
	std::string f = sdpath (p); return (remove (f.c_str ()) == 0 || rmdir (f.c_str ()) == 0) ? 0 : -1;
}
static int f_rename (const char *a, const char *b)
{
	if (!(is_ram (a) && is_ram (b)) && !(written (a) && !is_ram (b)))
		return -1;
	if (!is_ram (b))						// (into the writes: its own path there)
	{
		struct stat st; std::string to = wpath (b);
		if (stat (to.c_str (), &st) == 0) return -1;
		return rename (sdpath (a).c_str (), to.c_str ()) == 0 ? 0 : -1;
	}
	struct stat st; std::string to = sdpath (b);
	if (stat (to.c_str (), &st) == 0) return -1;			// (there already: as the kernel's)
	return rename (sdpath (a).c_str (), to.c_str ()) == 0 ? 0 : -1;
}
// (v71) a volume's room: RAM: (128 MB, what its folder holds), the card (its folder's file system)
static unsigned long long g_ramUsed;
static int sum_one (const char *, const struct stat *st, int kind, struct FTW *) { if (kind == FTW_F) g_ramUsed += (unsigned long long) st->st_size; return 0; }
static int vol_info (const char *p, struct kapi_vol_info *o)
{
	if (!p || !o) return -1;
	memset (o, 0, sizeof *o);
	if (is_ram (p))
	{
		g_ramUsed = 0; nftw (ramdir ().c_str (), sum_one, 16, FTW_PHYS);
		o->total = 128ull << 20; o->used = g_ramUsed; o->free = o->used < o->total ? o->total - o->used : 0;
		o->flags = KAPI_VOL_RAM; snprintf (o->type, sizeof o->type, "RAM");
		return 0;
	}
	if (!volume_there (p)) return -1;
	o->total = 8ull << 30; o->free = 4ull << 30; o->used = o->total - o->free; snprintf (o->type, sizeof o->type, "FAT32");
	return 0;
}
static int list_tasks (char *b, unsigned n)
{
	if (b && n) snprintf (b, n, "Rk idle\nSk compositor\nSk usb\nSk net\nSa voronoy\nRa menubar\nRa dock\nSa agenda\n"
			     "Sa notifyd\nRa terminal\nSa tinycalc\nRa taskman\n");
	return 12;
}
// Sound: none, unless SIM_SOUND=1 -- then a stand-in output: the PCM stream is "played" at
// 44100 frames a second of real time from a 0.5 s queue (sound_write takes what fits,
// sound_status gives the free frames), with SIM_SOUNDOUT=<file> the frames played written to
// the file (s16 L R) -- the media tests hear with it.
static pthread_mutex_t g_sndLock = PTHREAD_MUTEX_INITIALIZER;
static bool g_sndOwned;
static double g_sndStart;		// when the queue's frame 0 plays (s)
static long long g_sndQueued;		// frames written since the start
static FILE *g_sndOut;
static const unsigned SND_CAP = 22050;
static double snd_now (void) { struct timespec t; clock_gettime (CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }
static long long snd_played (void) { return (long long) ((snd_now () - g_sndStart) * 44100); }
static int sound_acquire (void)
{
	if (!getenv ("SIM_SOUND")) return -1;
	pthread_mutex_lock (&g_sndLock);
	int r = g_sndOwned ? 0 : 1;
	if (r) { g_sndOwned = true; g_sndStart = snd_now (); g_sndQueued = 0; }
	if (r && !g_sndOut && getenv ("SIM_SOUNDOUT")) g_sndOut = fopen (getenv ("SIM_SOUNDOUT"), "wb");
	pthread_mutex_unlock (&g_sndLock);
	if (r) fprintf (stderr, "sim: sound acquired\n");
	return r;
}
static void sound_release (void) { pthread_mutex_lock (&g_sndLock); g_sndOwned = false; pthread_mutex_unlock (&g_sndLock); }
static int sound_start (int, unsigned, int, int) { return -1; }
static int sound_stop (int) { return -1; }
static int sound_write (const short *p, unsigned n)
{
	if (!getenv ("SIM_SOUND")) return -1;
	pthread_mutex_lock (&g_sndLock);
	long long played = snd_played ();
	if (g_sndQueued < played)
	{	// an underrun: silence was played meanwhile
		if (g_sndOut) { static short z[2048]; for (long long k = g_sndQueued; k < played; k += 1024) fwrite (z, 4, (size_t) std::min (1024LL, played - k), g_sndOut); }
		g_sndQueued = played;
	}
	long long q = g_sndQueued - played;
	unsigned room = q >= SND_CAP ? 0 : (unsigned) (SND_CAP - q);
	if (n > room) n = room;
	if (g_sndOut && n) fwrite (p, 4, n, g_sndOut);
	g_sndQueued += n;
	pthread_mutex_unlock (&g_sndLock);
	return (int) n;
}
static int sound_status (unsigned *r, unsigned *f, unsigned *o)
{
	if (r) *r = 44100;
	if (!getenv ("SIM_SOUND")) { if (f) *f = 0; if (o) *o = 0; return -1; }
	pthread_mutex_lock (&g_sndLock);
	long long q = g_sndQueued - snd_played ();
	if (q < 0) q = 0;
	if (f) *f = (unsigned) (q >= SND_CAP ? 0 : SND_CAP - q);
	if (o) *o = g_sndOwned ? 1 : 0;
	pthread_mutex_unlock (&g_sndLock);
	return 0;
}
// (v68) low-latency sound, word waits, priorities, MIDI: no MIDI, a single thread
static int sound_config (int chunk, int ahead) { return getenv ("SIM_SOUND") ? 0 * (chunk + ahead) : -1; }
static struct kapi_sound_ring *sound_map (void) { return 0; }
static int wait_word (volatile unsigned *a, unsigned v, unsigned ms)
{
	if (a == 0) return -1;
	if (*a != v) return 0;
	if (ms != 0) h_msleep (ms < 16 ? ms : 16);	// (nobody else could change it: a short nap)
	return *a != v ? 0 : 1;
}
static int wake_word (volatile unsigned *a) { return a != 0 ? 0 : -1; }
static int thread_priority (int, int) { return -2; }
static int midi_read (struct kapi_midi_event *, int) { return 0; }
static int midi_devices (void) { return 0; }
// the monitor (SIM_NATIVE="<w>x<h>", else a Full HD one), the time zone
static int screen_native (int *w, int *h)
{
	const char *e = getenv ("SIM_NATIVE");
	if (!e || sscanf (e, "%dx%d", w, h) != 2) { *w = 1920; *h = 1080; }
	return 1;
}
static int set_timezone (int m) { return m >= -720 && m <= 840; }
static int proc_done (void *p) { return p == (void *) 0x5000 ? 0 : 1; }	// (the SIM_PIPE program: running)
static int h_wait (void *) { return 0; }
// A canned stream (SIM_PIPE, SIM_NET): its text ("\n" a new line, "\r" a return, "\e" an escape)
// read once, then nothing more
struct Canned { const char *env; std::string s; size_t pos; bool init; };
static int canned_read (Canned &c, void *b, unsigned n)
{
	if (!c.init)
	{
		c.init = true;
		const char *e = getenv (c.env);
		for (size_t i = 0; e && e[i]; i++)
			if (e[i] == '\\' && e[i + 1] == 'n') { c.s += '\n'; i++; }
			else if (e[i] == '\\' && e[i + 1] == 'r') { c.s += '\r'; i++; }
			else if (e[i] == '\\' && e[i + 1] == 'e') { c.s += '\x1b'; i++; }
			else c.s += e[i];
	}
	unsigned k = (unsigned) std::min ((size_t) n, c.s.size () - c.pos);
	memcpy (b, c.s.data () + c.pos, k); c.pos += k;
	return (int) k;
}
// SIM_PIPE: a spawned program's output (the terminal's shell), from the pipes (handles 1, 2, ...)
static Canned g_pipeOut = { "SIM_PIPE" };
static int pipe_read (void *h, void *b, unsigned n)
{
	if (!h || (unsigned long) h > 64) return 0;
	return canned_read (g_pipeOut, b, n);
}
// SIM_NET: what the server sends (irc) on a connection; none: no network
static Canned g_netIn = { "SIM_NET" };
// SIM_REALNET: the PC's sockets (the handle: the file descriptor + 1000)
// SIM_SOCKETS=N: the kernel's socket table (16 on the Pi, for every app) -- a connect past N
// open sockets fails as the kernel's does (-2: the table full)
static int g_socks;
static int tcp_connect (const char *host, unsigned port)
{
	fprintf (stderr, "sim: tcp_connect %s:%u\n", host, port);
	if (!getenv ("SIM_REALNET")) return getenv ("SIM_NET") ? 3 : -5;
	if (getenv ("SIM_SOCKETS") && __atomic_load_n (&g_socks, __ATOMIC_SEQ_CST) >= atoi (getenv ("SIM_SOCKETS"))) {
		fprintf (stderr, "sim: tcp_connect %s:%u: no socket free\n", host, port);
		return -2;
	}
	// behind an HTTPS_PROXY (http://host:port): a CONNECT tunnel, except to this machine
	const char *px = getenv ("HTTPS_PROXY");
	char phost[256]; unsigned pport = 0;
	bool local = !strcmp (host, "localhost") || !strncmp (host, "127.", 4);
	if (px && !local && sscanf (px, "http://%255[^:/]:%u", phost, &pport) == 2) {}
	else pport = 0;
	struct addrinfo hints, *res = 0; memset (&hints, 0, sizeof hints);
	hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
	char ps[16]; snprintf (ps, sizeof ps, "%u", pport ? pport : port);
	if (getaddrinfo (pport ? phost : host, ps, &hints, &res) != 0 || !res) return -4;
	int fd = socket (res->ai_family, res->ai_socktype, res->ai_protocol);
	if (fd < 0 || connect (fd, res->ai_addr, res->ai_addrlen) != 0) { if (fd >= 0) close (fd); freeaddrinfo (res); return -5; }
	freeaddrinfo (res);
	int one = 1; setsockopt (fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
	if (pport) {
		char rq[600]; int n = snprintf (rq, sizeof rq, "CONNECT %s:%u HTTP/1.1\r\nHost: %s:%u\r\n\r\n", host, port, host, port);
		if (send (fd, rq, (size_t) n, MSG_NOSIGNAL) != n) { close (fd); return -5; }
		// the proxy's answer, up to its blank line
		char ans[2048]; int got = 0;
		while (got < (int) sizeof ans - 1) {
			ssize_t k = recv (fd, ans + got, 1, 0);
			if (k <= 0) { close (fd); return -5; }
			got++; ans[got] = 0;
			if (got >= 4 && !memcmp (ans + got - 4, "\r\n\r\n", 4)) break;
		}
		if (strncmp (ans + 8, " 200", 4)) { fprintf (stderr, "sim: proxy refused %s:%u: %.60s\n", host, port, ans); close (fd); return -5; }
	}
	__atomic_add_fetch (&g_socks, 1, __ATOMIC_SEQ_CST);
	return fd + 1000;
}
static int tcp_send (int s, const void *b, unsigned n)
{
	if (s < 1000) return (int) n;
	unsigned o = 0;
	while (o < n) { ssize_t k = send (s - 1000, (const char *) b + o, n - o, MSG_NOSIGNAL); if (k <= 0) return -1; o += (unsigned) k; }
	return (int) n;
}
static int tcp_recv (int s, void *b, unsigned n)
{
	if (s < 1000) return canned_read (g_netIn, b, n);
	ssize_t k = recv (s - 1000, b, n, MSG_DONTWAIT);
	if (k > 0) return (int) k;
	if (k < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;
	return -1;
}
// net_resolve: the PC's resolver (SIM_REALNET; behind a proxy the name may not resolve here: 0)
static int net_resolve (const char *host, char *ip, unsigned cap)
{
	if (!getenv ("SIM_REALNET") || host == 0) return 0;
	struct addrinfo hints, *res = 0; memset (&hints, 0, sizeof hints);
	hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo (host, "443", &hints, &res) != 0 || !res) return 0;
	struct sockaddr_in *a = (struct sockaddr_in *) res->ai_addr;
	const unsigned char *b = (const unsigned char *) &a->sin_addr;
	if (ip && cap) snprintf (ip, cap, "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
	freeaddrinfo (res);
	return 1;
}
static void tcp_close (int s) { if (s >= 1000) { close (s - 1000); __atomic_sub_fetch (&g_socks, 1, __ATOMIC_SEQ_CST); } }
// the Wi-Fi around (the Wi-Fi menu)
static int wlan_scan (struct kapi_wlan_ap *o, int max)
{
	static const struct { const char *ssid; int level, sec, ch, con; } L[] = {
		{ "Maison", -41, WLAN_SEC_WPA2, 6, 1 }, { "Voisin-5G", -57, WLAN_SEC_WPA2, 36, 0 },
		{ "FreeWifi", -66, WLAN_SEC_OPEN, 11, 0 }, { "Livebox-1280", -73, WLAN_SEC_WPA2, 1, 0 },
		{ "", -81, WLAN_SEC_WPA2, 13, 0 } };
	int n = 0;
	for (auto &a : L)
	{
		if (n >= max) break;
		memset (&o[n], 0, sizeof o[n]);
		snprintf (o[n].ssid, sizeof o[n].ssid, "%s", a.ssid);
		o[n].level = a.level; o[n].security = (unsigned char) a.sec; o[n].channel = (unsigned char) a.ch;
		o[n].connected = (unsigned char) a.con; o[n].freq = a.ch > 14 ? 5000 + 5 * a.ch : 2407 + 5 * a.ch;
		n++;
	}
	return n;
}
static int wlan_reconnect (void) { return 0; }
// SIM_WINS="x,y,w,h,desk,keys;...": the windows (the dock's workspaces draw them small)
static int win_list (struct kapi_win_info *o, int max)
{
	const char *e = getenv ("SIM_WINS");
	int n = 0;
	for (const char *p = e; p && *p && n < max; )
	{
		int x, y, w, h, d, k;
		if (sscanf (p, "%d,%d,%d,%d,%d,%d", &x, &y, &w, &h, &d, &k) != 6) break;
		memset (&o[n], 0, sizeof o[n]);
		o[n].id = (unsigned) n + 1; o[n].x = x + 4; o[n].y = y + 28; o[n].w = w - 8; o[n].h = h - 32;
		o[n].ow = w; o[n].oh = h; o[n].il = 4; o[n].it = 28; o[n].alpha = 255;
		o[n].state = (k ? KAPI_WIN_KEYS : 0) | (unsigned) ((d + 1) & 0xFF) << 8;
		n++;
		for (int commas = 0; *p && *p != ';' && commas < 6; p++) if (*p == ',') commas++;	// (",title": its title)
		if (p[-1] == ',' ) { int t = 0; while (*p && *p != ';' && t < 47) o[n - 1].title[t++] = *p++; o[n - 1].title[t] = 0; }
		while (*p && *p != ';') p++;
		if (*p == ';') p++;
	}
	return n;
}
// the kernel's text straight into the window (mandelbrot's status line)
static void draw_text (int x, int y, const char *s, unsigned c) { draw_text_buf (g_canvas, g_stride, g_lh, x, y, s, c); }
// file streams (kapi_file_in / kapi_file_out): a FILE * behind a tagged handle, told apart from the
// canned pipes (small numbers) by a set of the handles made
struct FStream { FILE *f; };
static std::set<void *> g_fstreams;
static pthread_mutex_t g_fsLock = PTHREAD_MUTEX_INITIALIZER;
static bool is_fstream (void *h) { pthread_mutex_lock (&g_fsLock); bool r = g_fstreams.count (h) != 0; pthread_mutex_unlock (&g_fsLock); return r; }
static void *fstream_make (FILE *f)
{
	if (!f) return 0;
	FStream *s = new FStream; s->f = f;
	pthread_mutex_lock (&g_fsLock); g_fstreams.insert (s); pthread_mutex_unlock (&g_fsLock);
	return s;
}
static void *file_in (const char *p) { return fstream_make (fopen (sdpath (p).c_str (), "rb")); }
static void *file_out (const char *p, int append)
{
	std::string w = wpath (p);
	if (append)						// (appending to a card file: from its copy)
	{
		struct stat st; std::string src = sdpath (p);
		if (stat (w.c_str (), &st) != 0 && src != w) { FILE *a = fopen (src.c_str (), "rb"), *b = fopen (w.c_str (), "wb");
			char buf[65536]; size_t n; while (a && b && (n = fread (buf, 1, sizeof buf, a)) > 0) fwrite (buf, 1, n, b);
			if (a) fclose (a); if (b) fclose (b); }
	}
	return fstream_make (fopen (w.c_str (), append ? "ab" : "wb"));
}
static int stream_read (void *h, void *b, unsigned n) { if (is_fstream (h)) return (int) fread (b, 1, n, ((FStream *) h)->f); return pipe_read (h, b, n); }
static int stream_read_nb (void *h, void *b, unsigned n) { return stream_read (h, b, n); }
static int stream_write (void *h, const void *b, unsigned n) { if (is_fstream (h)) return (int) fwrite (b, 1, n, ((FStream *) h)->f); return (int) n; }
static void stream_eof (void *) {}
static int stdin_read (void *, unsigned) { return 0; }
static int f_seek (void *h, unsigned long long pos) { return fseek ((FILE *) h, (long) pos, SEEK_SET) == 0 ? 0 : -1; }
static unsigned long long f_fsize64 (void *h) { return f_fsize (h); }
static int net_info (char *b, unsigned n) { if (b && n) snprintf (b, n, "ip 192.168.1.42\n"); return 1; }
static void h_exit (int st) { fprintf (stderr, "sim: exit %d\n", st); if (onyx_host_exit_hook) onyx_host_exit_hook (); exit (0); }
static int toggle_app (const char *n) { fprintf (stderr, "sim: toggle_app %s\n", n); return 1; }
static int ram_detail (unsigned long *a, unsigned long *b, unsigned long *c, unsigned long *d, unsigned *e)
{ if (a) *a = 4194304; if (b) *b = 3145728; if (c) *c = 2097152; if (d) *d = 0; if (e) *e = 1; return 1; }
static int s_wheel = 2;
static void set_wheel (int v) { s_wheel = v; }
static int get_wheel (void) { return s_wheel; }
static int h_kill (const char *n) { fprintf (stderr, "sim: kill %s\n", n); return 1; }
static int set_keymap_data (const char *, const void *, unsigned) { return 1; }
static int sipc_send (int, int, const void *, unsigned);
static int mailbox_send (int to, int type, const void *d, unsigned n) { return getenv ("SIM_IPC") ? sipc_send (to, type, d, n) : 0; }
static int drag_begin (int, const void *, unsigned, const char *) { return 0; }
static int drag_data (int *type, void *buf, unsigned cap)
{
	if (type) *type = DND_FILES;
	unsigned n = (unsigned) g_dragData.size ();
	if (buf && cap) memcpy (buf, g_dragData.c_str (), n < cap ? n : cap);
	return (int) n;
}
static void *spawn (const char *p, const char *a, void *, void *)
{
	fprintf (stderr, "sim: spawn %s %s\n", p, a ? a : "");
	return getenv ("SIM_PIPE") ? (void *) 0x5000 : 0;
}
static unsigned long g_pipes;
static void *h_pipe (void) { return getenv ("SIM_PIPE") && g_pipes < 64 ? (void *) ++g_pipes : 0; }
static void stream_close (void *h)
{
	if (!is_fstream (h)) return;
	pthread_mutex_lock (&g_fsLock); g_fstreams.erase (h); pthread_mutex_unlock (&g_fsLock);
	fclose (((FStream *) h)->f); delete (FStream *) h;
}
static void h_reboot (void) { printf ("[sim: reboot]\n"); fflush (stdout); _exit (0); }
static int get_args (char *b, unsigned n)
{
	const char *a = getenv ("SIM_APPLET") ? "--applet 1 99" : getenv ("SIM_ARGS");
	snprintf (b, n, "%s", a ? a : "");
	return (int) strlen (b);
}
// The clipboard: one typed blob, 64 KB at most, as the kernel's (kapi.cpp). SIM_CLIP: what it
// holds at the start ("text", or "files:PATH" -- CLIP_FILES); SIM_CLIPFILE: each set written
// there (its bytes exactly; its type on stdout: "SIM-CLIPBOARD type=T len=N").
static std::string g_clip;
static int g_clipType = -1;		// (-1: SIM_CLIP not read yet)
static unsigned g_clipSerial;
static void clip_init (void)
{
	if (g_clipType >= 0) return;
	g_clipType = 0;
	const char *c = getenv ("SIM_CLIP");
	if (!c || !*c) return;
	if (!strncmp (c, "files:", 6)) { g_clip = c + 6; g_clipType = 2; }
	else { g_clip = c; g_clipType = 1; }
}
static int clipboard_set (int type, const void *d, unsigned n)
{
	clip_init ();
	if (n > 64 * 1024) n = 64 * 1024;
	if (!d) n = 0;
	g_clip.assign ((const char *) (d ? d : ""), n);
	g_clipType = n ? type : 0;
	g_clipSerial++;
	printf ("SIM-CLIPBOARD type=%d len=%u\n", g_clipType, n); fflush (stdout);
	if (getenv ("SIM_CLIPFILE"))
	{
		FILE *f = fopen (getenv ("SIM_CLIPFILE"), "wb");
		if (f) { fwrite (g_clip.data (), 1, g_clip.size (), f); fclose (f); }
	}
	return (int) n;
}
static int clipboard_get (int *t, void *b, unsigned cap, unsigned *serial)
{
	clip_init ();
	if (t) *t = g_clipType;
	if (serial) *serial = g_clipSerial;
	unsigned n = (unsigned) g_clip.size () < cap ? (unsigned) g_clip.size () : cap;
	if (b && n) memcpy (b, g_clip.data (), n);
	return (int) g_clip.size ();
}
static void set_click (gui_handler h) { g_click = h; }
static int key_held (int) { return 0; }
// SIM_CURSOR="x,y": the pointer, relative to the client area (the eyes look at it); none: away
static void cursor_pos (int *x, int *y)
{
	int cx = -1, cy = -1; const char *e = getenv ("SIM_CURSOR");
	if (e) sscanf (e, "%d,%d", &cx, &cy);
	if (x) *x = cx; if (y) *y = cy;
}
static void set_alpha (int) {}
// SIM_IPC=1: services and mailboxes inside this process -- each thread that registers a service is a
// "process" of its own (its pid), the others are pid 1 (a client and its services as threads: the
// shared clipboard's test)
struct SimMsg { int from, type; std::string bytes; };
static std::map<std::string, int> g_ipcNames;
static std::map<pthread_t, int> g_ipcThread;
static std::map<int, std::deque<SimMsg> > g_ipcBox;
static pthread_mutex_t g_ipcLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_ipcCond = PTHREAD_COND_INITIALIZER;
static int g_ipcNext = 100;
static bool sim_ipc (void) { return getenv ("SIM_IPC") != 0; }
static int ipc_self (void) { auto it = g_ipcThread.find (pthread_self ()); return it == g_ipcThread.end () ? 1 : it->second; }
static int sipc_register (const char *n)
{
	pthread_mutex_lock (&g_ipcLock);
	int r = 0;
	if (!g_ipcNames.count (n)) { int pid = g_ipcNext++; g_ipcNames[n] = pid; g_ipcThread[pthread_self ()] = pid; r = 1; }
	pthread_mutex_unlock (&g_ipcLock);
	return r;
}
static int sipc_lookup (const char *n)
{
	pthread_mutex_lock (&g_ipcLock);
	auto it = g_ipcNames.find (n); int r = it == g_ipcNames.end () ? 0 : it->second;
	pthread_mutex_unlock (&g_ipcLock);
	return r;
}
static int sipc_send (int to, int type, const void *d, unsigned n)
{
	if (n > 512) return -1;
	pthread_mutex_lock (&g_ipcLock);
	g_ipcBox[to].push_back ({ ipc_self (), type, std::string ((const char *) d, n) });
	pthread_cond_broadcast (&g_ipcCond);
	pthread_mutex_unlock (&g_ipcLock);
	return 0;
}
static int sipc_recv (int *from, int *type, void *buf, unsigned cap, int blocking)
{
	pthread_mutex_lock (&g_ipcLock);
	int me = ipc_self ();
	while (blocking && g_ipcBox[me].empty ()) pthread_cond_wait (&g_ipcCond, &g_ipcLock);
	if (g_ipcBox[me].empty ()) { pthread_mutex_unlock (&g_ipcLock); return -1; }
	SimMsg m = g_ipcBox[me].front (); g_ipcBox[me].pop_front ();
	pthread_mutex_unlock (&g_ipcLock);
	if (from) *from = m.from; if (type) *type = m.type;
	unsigned k = (unsigned) m.bytes.size () < cap ? (unsigned) m.bytes.size () : cap;
	memcpy (buf, m.bytes.data (), k);
	return (int) k;
}
static int ipc_register (const char *n) { if (sim_ipc ()) return sipc_register (n); return !strcmp (n, "control") || !strcmp (n, "dock"); }	// (the only ones)
static int ipc_lookup (const char *n)
{
	if (sim_ipc ()) return sipc_lookup (n);
	if (getenv ("SIM_APPLET") && !strcmp (n, "control")) return 99;	// (the applet's host: there)
	if (getenv ("SIM_MAIL") && !strcmp (n, "control")) return 5;		// (the Control Panel: us)
	if (getenv ("SIM_MBOX")) return 7;					// (SIM_MBOX's sender: any service)
	return 0;
}
// (v35) the surfaces: one, made or mapped, SIM_SURFACE's pixels poured into it on AP_HELLO
static void surface_fill (void)
{
	const char *f = getenv ("SIM_SURFACE");
	FILE *fp = f ? fopen (f, "rb") : 0;
	if (!fp) return;
	int hdr[5]; if (fread (hdr, 4, 5, fp) == 5 && hdr[1] == g_surfW && hdr[2] == g_surfH)
		if (fread (g_surf, 4, (size_t) g_surfW * g_surfH, fp) != (size_t) g_surfW * g_surfH) fprintf (stderr, "sim: short surface\n");
	fclose (fp);
}
static int surface_create (int w, int h) { g_surfW = w; g_surfH = h; if (!g_surf) g_surf = (unsigned *) calloc ((size_t) w * h, 4); return 1; }
static unsigned *surface_map (int) { if (!g_surf) g_surf = (unsigned *) calloc ((size_t) g_surfW * g_surfH, 4); return g_surf; }
static int surface_size (int, int *w, int *h) { if (w) *w = g_surfW; if (h) *h = g_surfH; return 1; }
// (v65) the workspaces
static int s_desk = 0, s_desks = 4;
static int desk (int set, int count)
{
	static bool init = false;
	if (!init) { init = true; const char *e = getenv ("SIM_DESKS"); if (e) sscanf (e, "%d,%d", &s_desk, &s_desks); }
	if (count > 0) s_desks = count;
	if (set >= 0 && set < s_desks) s_desk = set;
	return s_desk | (s_desks << 8);
}
static int win_desk (unsigned, int n) { return n < -1 ? s_desk : n; }
static int shell_request (int, const void *, unsigned) { return -1; }	// (not in the activity shell)
static int random_fill (void *b, unsigned n) { for (unsigned i = 0; i < n; i++) ((unsigned char *) b)[i] = (unsigned char) rand (); return (int) n; }

// memmon's figures; SIM_NOTE="Title|Text": one notification in the mailbox (notifyd); SIM_PAD=1:
// a gamepad in slot 1 (an Xbox-like pad, two buttons held: padconf)
// (the Task Manager's system calls per second: made up from the pid)
static int proc_stats (int pid, struct kapi_syscall_stats *o) { memset (o, 0, sizeof *o); o->rate = (unsigned) (pid * 137 % 900); return 0; }
static int list_procs (char *b, unsigned n)
{ if (b && n) snprintf (b, n, "0 k R 0 idle\n1 k S 2 compositor\n2 k S 0 usb\n3 k S 0 net\n7 a R 38 menubar\n8 a S 52 dock\n9 a R 120 terminal\n10 a S 21 agenda\n11 a S 12 notifyd\n12 a R 1850 web\n13 a S 722 koton\n14 a S 608 media\n15 a S 228 mail\n16 a R 96 taskman\n"); return 14; }
static int meminfo (unsigned long *t, unsigned long *f, unsigned long *a, unsigned *pk)
{ if (t) *t = 3145728; if (f) *f = 2097152; if (a) *a = 409600; if (pk) *pk = 64; return 1; }
static int mailbox_recv_note (int *from, int *type, void *buf, unsigned cap, int blocking)
{
	if (getenv ("SIM_IPC")) return sipc_recv (from, type, buf, cap, blocking);
	static bool mailed = false;
	const char *mail = getenv ("SIM_MAIL");
	if (mail && !mailed)						// one message (an applet's hello)
	{
		mailed = true;
		int t = 0, pid = 0; sscanf (mail, "%d:%d", &t, &pid);
		if (from) *from = pid; if (type) *type = t;
		if (t == 40) surface_fill ();
		int wh[2] = { g_surfW, g_surfH };
		unsigned n = cap < sizeof wh ? cap : (unsigned) sizeof wh;
		memcpy (buf, wh, n);
		return (int) n;
	}
	// SIM_MBOX: canned messages, "type:pid:payload" a line ("\n" between them, "\t" a tab), one a call
	static Canned mbox = { "SIM_MBOX" };
	if (getenv ("SIM_MBOX"))
	{
		if (!mbox.init) { char c; canned_read (mbox, &c, 0); }
		if (mbox.pos >= mbox.s.size ()) return -1;
		size_t eol = mbox.s.find ('\n', mbox.pos); if (eol == std::string::npos) eol = mbox.s.size ();
		std::string l = mbox.s.substr (mbox.pos, eol - mbox.pos); mbox.pos = eol + 1;
		int t = 0, pid = 0, at = 0; sscanf (l.c_str (), "%d:%d:%n", &t, &pid, &at);
		std::string m = l.substr (at), d;
		for (size_t i = 0; i < m.size (); i++)
			if (m[i] == '\\' && i + 1 < m.size () && m[i + 1] == 't') { d += '\t'; i++; } else d += m[i];
		unsigned n = (unsigned) d.size () < cap ? (unsigned) d.size () : cap;
		memcpy (buf, d.data (), n);
		if (from) *from = pid; if (type) *type = t;
		return (int) n;
	}
	static bool done = false;
	const char *e = getenv ("SIM_NOTE");
	if (done || !e) return -1;
	done = true;
	std::string s (e); size_t bar = s.find ('|');
	std::string t = s.substr (0, bar), x = bar == std::string::npos ? "" : s.substr (bar + 1);
	std::string m = t; m.push_back ('\0'); m += x; m.push_back ('\0');
	unsigned n = (unsigned) m.size () < cap ? (unsigned) m.size () : cap;
	memcpy (buf, m.data (), n);
	if (from) *from = 3; if (type) *type = 1;
	return (int) n;
}
static int ipc_register_note (const char *n) { return getenv ("SIM_NOTE") ? 1 : ipc_register (n); }
static int pad_state_sim (int i, struct kapi_pad *p)
{
	if (!getenv ("SIM_PAD") || i != 0) return 0;
	memset (p, 0, sizeof *p);
	p->vid = 0x045E; p->pid = 0x028E; p->props = 1; p->focus = 1; p->seq = 7;
	p->nbuttons = 15; p->buttons = (1u << 0) | (1u << 5);
	p->naxes = 6;
	for (int k = 0; k < 6; k++) { p->axes[k].minimum = -32768; p->axes[k].maximum = 32767; p->axes[k].value = k == 0 ? 12000 : k == 1 ? -8000 : 0; }
	p->nhats = 1; p->hats[0] = 8;
	return 1;
}

static void unimplemented (void)
{
	fprintf (stderr, "sim: an unimplemented kapi was called (from %p)\n", __builtin_return_address (0));
	exit (2);
}

// the software V3D (tools/tests/gpucomp/hostkapi.cpp), when linked in (NetSurf's host.mk SOFTGPU=1)
extern "C" void hostkapi_install_gpu (TKApiTable *t) __attribute__ ((weak));

static void setup (void)
{
	void *p = mmap ((void *) KAPI_TABLE_VA, 65536, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
	if (p == MAP_FAILED) { perror ("mmap"); exit (1); }
	T = (TKApiTable *) p;
	void **slots = (void **) T;
	for (size_t i = 0; i < sizeof (TKApiTable) / sizeof (void *); i++) slots[i] = (void *) unimplemented;
	T->version = KAPI_ABI_VERSION;
	// (v75) the POSIX entries absent here: 0, so kapi.h's wrappers return -KAPI_ENOSYS
	for (size_t i = __builtin_offsetof (TKApiTable, vm_map) / 8; i < sizeof (TKApiTable) / 8; i++) ((void **) T)[i] = 0;
	T->create_window = create; T->create_window_ex = create_ex; T->resize_window = resize; T->move_window = move_window;
	T->set_pointer_handler = set_ptr; T->set_key_handler = set_key; T->screen_size = screen_size;
	T->font_width = font_w; T->font_height = font_h; T->present = h_present; T->pump_events = pump;
	T->screen_grab = screen_grab; T->fullscreen_begin = fs_begin; T->present_fb = fs_present; T->fullscreen_end = fs_end;
	T->msleep = h_msleep; T->get_ticks = h_get_ticks; T->should_exit = h_should_exit; T->yield = yield;
	T->open = f_open; T->read = f_read; T->write = f_write; T->fsize = f_fsize; T->close = f_close; T->save_file = save_file;
	T->opendir = f_opendir; T->readdir = f_readdir; T->closedir = f_closedir; T->list_apps = list_apps;
	T->get_datetime = get_datetime; T->net_status = net_status; T->sound_volume = sound_volume;
	T->stdout_write = stdout_write; T->sbrk = h_sbrk; T->pad_state = pad_state; T->get_modifiers = get_mods;
	T->launch = launch; T->raise_app = raise_app; T->exec = exec; T->exec_as = exec_as;
	T->menu_command = menu_command; T->set_menu = set_menu; T->get_menu = get_menu;
	T->list_windows = list_windows; T->draw_text_buf = draw_text_buf;
	T->get_chrome = get_chrome; T->get_args = get_args; T->clipboard_set = clipboard_set;
	T->clipboard_get = clipboard_get; T->set_click_handler = set_click; T->key_held = key_held;
	T->cursor_pos = cursor_pos; T->set_window_alpha = set_alpha; T->random = random_fill; T->reboot = h_reboot;
	T->ipc_register = ipc_register; T->ipc_lookup = ipc_lookup;
	T->shell_request = shell_request;
	T->win_minimise = win_minimise; T->win_geometry = win_geometry; T->resize_window2 = resize2;
	T->mailbox_recv = mailbox_recv; T->mailbox_send = mailbox_send; T->drag_begin = drag_begin; T->drag_data = drag_data;
	T->spawn = spawn; T->pipe = h_pipe; T->stream_close = stream_close;
	T->wallpaper_buffer = wallpaper_buffer; T->wallpaper_commit = wallpaper_commit;
	T->chdir = h_chdir; T->stdout_stream = h_stdout_stream; T->kbd_ready = h_kbd_ready; T->gpu_info = h_gpu_info;
	T->get_keymap = get_keymap; T->set_wheel_speed = set_wheel; T->get_wheel_speed = get_wheel;
	T->kill = h_kill; T->set_keymap_data = set_keymap_data;
	T->app_dir = app_dir; T->mkdir = f_mkdir; T->remove = f_remove; T->rename = f_rename; T->list_tasks = list_tasks;
	T->sound_acquire = sound_acquire; T->sound_release = sound_release; T->sound_start = sound_start;
	T->sound_stop = sound_stop; T->sound_write = sound_write; T->sound_status = sound_status;
	T->proc_done = proc_done; T->wait = h_wait; T->stream_read = stream_read; T->file_in = file_in; T->file_out = file_out; T->stream_read_nb = stream_read_nb;
	T->stream_write = stream_write; T->stream_eof = stream_eof; T->stdin_read = stdin_read;
	T->vol_info = vol_info;
	T->seek = f_seek; T->fsize64 = f_fsize64; T->net_info = net_info; T->exit = h_exit; T->toggle_app = toggle_app;
	T->ram_detail = ram_detail; T->draw_text = draw_text; T->win_list = win_list;
	T->list_procs = list_procs; T->proc_stats = proc_stats; T->meminfo = meminfo; T->mailbox_recv = mailbox_recv_note;
	T->ipc_register = ipc_register_note; T->pad_state = pad_state_sim;
	T->tcp_connect = tcp_connect; T->tcp_send = tcp_send; T->tcp_recv = tcp_recv; T->tcp_close = tcp_close;
	T->net_resolve = net_resolve;
	T->wlan_scan = wlan_scan; T->wlan_reconnect = wlan_reconnect;
	T->surface_create = surface_create; T->surface_map = surface_map; T->surface_size = surface_size;
	T->desk = desk; T->win_desk = win_desk;
	T->sound_config = sound_config; T->sound_map = sound_map; T->wait_word = wait_word;
	T->wake_word = wake_word; T->thread_priority = thread_priority;
	T->midi_read = midi_read; T->midi_devices = midi_devices;
	T->screen_native = screen_native; T->set_timezone = set_timezone;
	T->thread_create = h_thread_create; T->thread_exit = h_thread_exit; T->thread_join = h_thread_join; T->thread_self = h_thread_self;
	T->event_create = h_event_create; T->event_set = h_event_set; T->event_reset = h_event_reset; T->event_wait = h_event_wait;
	T->post = h_post; T->pump_wait = h_pump_wait;
	g_mainThread = pthread_self (); g_mainSet = true;
	load_font ();
	const char *sc = getenv ("SIM");
	std::string s = sc ? sc : "wait;dump out.elsm;exit";
	size_t i = 0;
	while (i <= s.size ()) { size_t j = s.find (';', i); if (j == std::string::npos) j = s.size (); if (j > i) g_script.push_back (s.substr (i, j - i)); i = j + 1; }
	// GPC_SOFTGPU=1: the GPU compositing service's GPU path on the software V3D (when linked in)
	if (hostkapi_install_gpu && getenv ("GPC_SOFTGPU")) hostkapi_install_gpu (T);
}

// (a static object's constructor, after the globals above: a constructor-attribute function would
// run before std::vector's own initialisation and see its work undone)
static struct SimInit { SimInit () { setup (); } } s_init;
