//
// control -- the Control Panel: the system's settings in one window, each an APPLET shown inside
// it (applet_proto.h). Its home lists the applets, one a link file in SD:/apps/control.app/applets
// (sorted by their names: "10-theme.lnk", "20-dockconf.lnk"...):
//
//     name   = Theme                           the title shown
//     icon   = SD:/apps/theme.app/icon.bmp      a 40 x 40 BMP (magenta: see-through)
//     target = theme                           the applet: an app's name (SD:/apps/<name>.app)
//     text   = Colours of the windows, ...      a line of help under the title
//
// A click on one starts its target with "--applet <surface> <pid>": the applet (a wtk app) draws
// into the Control Panel's pane -- a shared surface made once, the pane's size -- and the Control
// Panel copies it into its window when the applet says so (AP_PRESENT), and sends it the pointer
// and the keys. "Control Panel" in the path bar (or the menu's All Settings) closes the applet and
// comes back to the list. `control <target>` opens that applet at once (the dock's Panel
// Settings...). One Control Panel at a time (it is the IPC service AP_SERVICE, "control").
//
#include "kapi.h"
#include "applib.h"
#include "bmp.hpp"
#include "fsutil.h"
#include "launch.h"
#include "notify.h"
#include "applet_proto.h"
#include "wtk/wtk.h"
#include "ft/wtkface.h"		// FreeType's text (DejaVu Sans) for every widget

using namespace wtk;

#define W	700
#define HDR	46			// the path bar
#define PH	470			// the applets' pane (below it)
#define H	(HDR + PH)
#define LINKS	"SD:/apps/control.app/applets"
#define MAXL	32
#define ROWH	78
#define COLW2	(W / 2)

struct Link { char file[48]; char name[40]; char target[64]; char text[120]; unsigned *icon; int iw, ih; };
static Link g_link[MAXL];
static int  g_nl;

// ---- the links -------------------------------------------------------------------------------------
static void trim (char *s)
{
	int n = fs_len (s);
	while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r')) s[--n] = 0;
}

static void read_link (Link &l, const char *path)
{
	l.name[0] = l.target[0] = l.text[0] = 0; l.icon = 0; l.iw = l.ih = 0;
	void *f = kapi_open (path);
	if (f == 0) return;
	char buf[1024];
	int n = kapi_read (f, buf, sizeof buf - 1);
	kapi_close (f);
	if (n <= 0) return;
	buf[n] = 0;
	char icon[160] = "";
	for (char *p = buf; *p; )
	{
		char *line = p; while (*p && *p != '\n') p++;
		if (*p) *p++ = 0;
		while (*line == ' ' || *line == '\t') line++;
		if (*line == ';' || *line == '#') continue;
		char *eq = line; while (*eq && *eq != '=') eq++;
		if (*eq != '=') continue;
		char *ke = eq; while (ke > line && (ke[-1] == ' ' || ke[-1] == '\t')) ke--;
		*ke = 0;
		char *v = eq + 1; while (*v == ' ' || *v == '\t') v++;
		trim (v);
		if (fs_ci_cmp (line, "name") == 0) fs_copy (l.name, v, sizeof l.name);
		else if (fs_ci_cmp (line, "target") == 0) fs_copy (l.target, v, sizeof l.target);
		else if (fs_ci_cmp (line, "text") == 0) fs_copy (l.text, v, sizeof l.text);
		else if (fs_ci_cmp (line, "icon") == 0) fs_copy (icon, v, sizeof icon);
	}
	if (icon[0]) l.icon = ui::bmp_decode (icon, &l.iw, &l.ih);
	if (l.name[0] == 0) fs_copy (l.name, l.target, sizeof l.name);
}

static void read_links (void)
{
	g_nl = 0;
	void *d = kapi_opendir (LINKS);
	if (d == 0) return;
	struct kapi_dirent e;
	while (g_nl < MAXL && kapi_readdir (d, &e))
	{
		int n = fs_len (e.name);
		if (e.is_dir || n < 5 || fs_ci_cmp (e.name + n - 4, ".lnk") != 0) continue;
		fs_copy (g_link[g_nl].file, e.name, sizeof g_link[g_nl].file);
		g_nl++;
	}
	kapi_closedir (d);
	for (int i = 1; i < g_nl; i++)						// by their file names
		for (int j = i; j > 0 && fs_ci_cmp (g_link[j - 1].file, g_link[j].file) > 0; j--)
		{ Link t = g_link[j]; g_link[j] = g_link[j - 1]; g_link[j - 1] = t; }
	for (int i = 0; i < g_nl; i++)
	{
		char p[200]; fs_join (p, sizeof p, LINKS, g_link[i].file);
		read_link (g_link[i], p);
	}
}

static int link_by_target (const char *t)
{
	for (int i = 0; i < g_nl; i++) if (fs_ci_cmp (g_link[i].target, t) == 0) return i;
	return -1;
}

// ---- the applet shown ------------------------------------------------------------------------------
static int       g_self;			// our pid (the service's)
static int       g_sid;				// the pane's surface
static unsigned *g_spx;				// ... mapped here
static int       g_cur = -1;			// the applet shown (a link), -1: the list
static int       g_pid;				// its process (0: not up yet)
static unsigned  g_t0;				// when it was started / asked to close
static bool      g_closing;			// AP_CLOSE sent, AP_EXIT awaited
static bool      g_fresh;			// it drew since we last copied
static char      g_status[96];

class ControlRoot;
static ControlRoot *g_root;

static void start_applet (int i)
{
	if (i < 0 || i >= g_nl || g_sid <= 0) return;
	char exe[160]; int n = 0;
	const char *t = g_link[i].target;
	bool path = false; for (int k = 0; t[k]; k++) if (t[k] == ':' || t[k] == '/') path = true;
	if (path) lx_cat (exe, sizeof exe, &n, t);
	else { lx_cat (exe, sizeof exe, &n, "SD:/apps/"); lx_cat (exe, sizeof exe, &n, t); lx_cat (exe, sizeof exe, &n, ".app/main"); }
	char args[48]; int a = 0;
	lx_cat (args, sizeof args, &a, "--applet ");
	char num[12]; ax_itoa (g_sid, num); lx_cat (args, sizeof args, &a, num);
	lx_cat (args, sizeof args, &a, " ");
	ax_itoa (g_self, num); lx_cat (args, sizeof args, &a, num);
	for (unsigned k = 0; k < (unsigned) (PH * W); k++) g_spx[k] = C_BG;	// (a blank pane meanwhile)
	char name[48]; int m = 0; lx_cat (name, sizeof name, &m, path ? "applet" : t);
	if (!kapi_exec_as (exe, args, name))
	{
		fs_copy (g_status, "Cannot start this applet (its program is missing?)", sizeof g_status);
		g_cur = i; g_pid = 0; g_closing = false;
		return;
	}
	g_cur = i; g_pid = 0; g_closing = false; g_fresh = true;
	g_t0 = kapi_get_ticks ();
	g_status[0] = 0;
}

// Ask the applet shown to end (its AP_EXIT, or a second later: killed), then the list again.
static void close_applet (void)
{
	if (g_cur < 0) return;
	if (g_pid > 0 && !g_closing)
	{
		kapi_mailbox_send (g_pid, AP_CLOSE, 0, 0);
		g_closing = true;
		g_t0 = kapi_get_ticks ();
		return;
	}
	if (g_pid <= 0) g_cur = -1;
}

// ---- the window ---------------------------------------------------------------------------------------
class ControlRoot : public Root
{
public:
	int hot = -1, down = -1;		// the list: the link under the pointer / pressed
	int scroll = 0;				// the list scrolled (px): more applets than the pane shows
	WkBarDrag bar;				// ... its scroll bar
	int listH () const { return 24 + ((g_nl + 1) / 2) * ROWH; }
	void scrollTo (int v)
	{
		int most = listH () - PH;
		if (v > most) v = most;
		if (v < 0) v = 0;
		if (v != scroll) { scroll = v; invalidate (true); }
	}
	void reveal (int i)			// the i-th applet's card wholly in the pane
	{
		int y = 12 + (i / 2) * ROWH;
		if (y - 12 < scroll) scrollTo (y - 12);
		else if (y + ROWH + 4 > scroll + PH) scrollTo (y + ROWH + 4 - PH);
	}
	bool crumbHot = false;
	int ptrButtons = 0;			// the pane: the buttons held (the events sent)
	bool inPane = false;
	int wanted = -1;			// the applet to show once the one shown has ended

	ControlRoot () : Root (W, H, "Control Panel") {}

	// ---- drawing ---------------------------------------------------------------------------
	void drawBar ()
	{
		// the path bar: the window's face, a field with the path in it (elementary's style)
		canvas.fillRect (0, 0, W, HDR, C_BG);
		wk_sunken (canvas, 10, 7, W - 20, HDR - 14, 6, wk_mix (C_BG, C_FIELD, 150), false);
		int fh = wk_fh (), y = (HDR - fh) / 2, x = 22;
		const char *root = "Control Panel";
		int rw = wk_text_w (root, 2);
		bool link = g_cur >= 0;
		unsigned ink = link ? (crumbHot ? C_ACCENT : C_FIELD_TEXT) : wk_tone (C_ACCENT, 84);
		wk_text (canvas, x, y, root, ink, 2);
		if (link && crumbHot) canvas.fillRect (x, y + fh, rw, 1, C_ACCENT);
		if (!link) canvas.fillRect (x, y + fh + 1, rw, 2, C_ACCENT);
		x += rw + 10;
		if (link)
		{
			wk_glyph (canvas, WKG_CHEV_RIGHT, x + 2, HDR / 2, 9, wk_mix (C_FIELD, C_FIELD_TEXT, 120));
			x += 16;
			const char *nm = g_link[g_cur].name;
			wk_text (canvas, x, y, nm, wk_tone (C_ACCENT, 84), 2);
			canvas.fillRect (x, y + fh + 1, wk_text_w (nm, 2), 2, C_ACCENT);
		}
		wk_etch_h (canvas, 0, HDR - 2, W, C_BG);
	}

	void drawList ()
	{
		canvas.fillRect (0, HDR, W, PH, C_BG);
		if (g_nl == 0)
		{
			wk_text_c (canvas, 0, HDR, W, 60, "No applet: SD:/apps/control.app/applets/*.lnk", C_DIS);
			return;
		}
		int fh = wk_fh ();
		for (int i = 0; i < g_nl; i++)
		{
			const Link &l = g_link[i];
			int x = 14 + (i % 2) * COLW2, y = HDR + 12 + (i / 2) * ROWH - scroll, w = COLW2 - 28, h = ROWH - 8;
			if (y + h <= HDR || y >= H) continue;			// (scrolled out of the pane)
			bool on = i == hot;
			if (on) wk_hilite (canvas, x, y, w, h, 8, down == i);
			else wk_rbox (canvas, x, y, w, h, 8, wk_tone (C_BG, 150), wk_tone (C_BG, 136), 120);
			if (l.icon)
				for (int j = 0; j < l.ih && j < 40; j++)
					for (int k = 0; k < l.iw && k < 40; k++)
					{
						unsigned c = l.icon[j * l.iw + k] & 0xFFFFFF;
						int py = y + (h - 40) / 2 + j;
						if (c != 0xFF00FF && py >= HDR && py < H) canvas.pixel (x + 12 + k, py, c);
					}
			unsigned ink = on ? wk_hilite_ink (down == i) : C_TEXT;
			wk_text (canvas, x + 64, y + 10, l.name, ink, 2);
			// the help, over two lines at most
			int maxw = w - 76, line = 0;				// (measured in pixels: the face is proportional)
			const char *p = l.text;
			while (*p && line < 2)
			{
				int len = 0, cut = -1;
				while (p[len] && wk_tw_n (p, len + 1) <= maxw) { if (p[len] == ' ') cut = len; len++; }
				if (p[len] && cut > 0) len = cut;
				if (len == 0) len = 1;
				char t[80]; int k = 0;
				for (int q = 0; q < len && k < 79; q++) t[k++] = p[q];
				t[k] = 0;
				canvas.text (x + 64, y + 14 + fh + line * (fh + 1), t, on ? ink : wk_mix (C_BG, C_TEXT, 170));
				p += len; while (*p == ' ') p++;
				line++;
			}
		}
		if (listH () > PH)
			wk_draw_vscroll (canvas, W - WK_SBW - 2, HDR + 2, WK_SBW, PH - 4, wk_thumb (listH (), PH, scroll, PH - 4), C_BG, bar.held);
	}

	void drawPane ()
	{
		if (g_pid <= 0 || g_closing)
		{
			canvas.fillRect (0, HDR, W, PH, C_BG);
			const char *s = g_status[0] ? g_status : g_closing ? "Closing..." : "Starting...";
			wk_text_c (canvas, 0, HDR, W, PH, s, C_DIS);
			return;
		}
		for (int y = 0; y < PH; y++)
		{
			unsigned *d = canvas.px + (long) (HDR + y) * canvas.stride;
			const unsigned *s = g_spx + (long) y * W;
			for (int x = 0; x < W; x++) d[x] = s[x] & 0x00FFFFFFu;
		}
	}

	void onDraw () override
	{
		if (g_cur < 0) drawList (); else drawPane ();
		drawBar ();					// (after the list: a card scrolled under the bar is covered)
	}

	// ---- each frame: the applet's messages ---------------------------------------------------
	void onTick () override
	{
		int from = 0, type = 0, n;
		unsigned char buf[64];
		while ((n = kapi_mailbox_recv (&from, &type, buf, sizeof buf, 0)) >= 0)
		{
			if (g_cur < 0) continue;
			if (type == AP_HELLO && g_pid == 0 && !g_closing) { g_pid = from; g_fresh = true; }
			else if (from != g_pid) continue;
			else if (type == AP_PRESENT) g_fresh = true;
			else if (type == AP_EXIT) { g_pid = 0; g_cur = -1; g_closing = false; }
			else if (type == AP_THEME)			// a new theme: ours, and the applet again
			{
				wk_theme_reload ();
				bg = C_BG;
				wanted = g_cur;
				close_applet ();
			}
		}
		unsigned now = kapi_get_ticks ();
		if (g_cur >= 0 && g_closing && g_pid > 0 && now - g_t0 > 100)	// (1 s: it did not answer)
		{
			kapi_kill_pid (g_pid, 1);
			g_pid = 0; g_cur = -1; g_closing = false;
		}
		if (g_cur >= 0 && !g_closing && g_pid == 0 && !g_status[0] && now - g_t0 > 500)
			fs_copy (g_status, "The applet did not start.", sizeof g_status);
		if (g_cur < 0 && wanted >= 0) { int w = wanted; wanted = -1; start_applet (w); }
		if (g_fresh || g_cur < 0 || g_closing) { g_fresh = false; invalidate (true); }
	}

	void show (int i)
	{
		if (g_cur == i) return;
		if (g_cur >= 0) { wanted = i; close_applet (); }
		else start_applet (i);
		invalidate (true);
	}
	void home () { wanted = -1; close_applet (); invalidate (true); }

	// ---- the pointer ------------------------------------------------------------------------------
	void sendPtr (int ev, int x, int y, int buttons, int changed, int wheel)
	{
		ApPtr p = { ev, x, y, buttons, changed, wheel };
		kapi_mailbox_send (g_pid, AP_PTR, &p, sizeof p);
	}

	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override
	{
		int btn = (bl ? 1 : 0) | (br ? 2 : 0) | (bm ? 4 : 0);
		bool paneShown = g_cur >= 0 && g_pid > 0 && !g_closing;
		// the pane: the applet's (a drag begun there stays with it)
		if (paneShown && mx >= 0 && (my >= HDR || ptrButtons))
		{
			int x = mx, y = my - HDR;
			if (!inPane) { inPane = true; sendPtr (GUI_EVENT_PTR_ENTER, x, y, btn, 0, 0); }
			if (wheel) sendPtr (GUI_EVENT_PTR_WHEEL, x, y, btn, 0, wheel);
			else if (btn != ptrButtons)
			{
				for (int b = 1; b <= 4; b <<= 1)
					if ((btn ^ ptrButtons) & b) sendPtr ((btn & b) ? GUI_EVENT_PTR_DOWN : GUI_EVENT_PTR_UP, x, y, btn, b, 0);
			}
			else sendPtr (GUI_EVENT_PTR_MOVE, x, y, btn, 0, 0);
			ptrButtons = btn;
			if (crumbHot) { crumbHot = false; invalidate (true); }
			return true;
		}
		if (inPane && paneShown) sendPtr (GUI_EVENT_PTR_LEAVE, -1, -1, 0, 0, 0);
		inPane = false; ptrButtons = 0;
		if (mx < 0) { if (hot >= 0 || crumbHot) { hot = -1; crumbHot = false; invalidate (true); } pressed = false; return false; }
		// the path bar: "Control Panel" -> the list
		bool onCrumb = my < HDR && mx >= 16 && mx < 24 + wk_text_w ("Control Panel", 2) && g_cur >= 0;
		if (onCrumb != crumbHot) { crumbHot = onCrumb; invalidate (true); }
		int h = -1;
		if (g_cur < 0 && listH () > PH)
		{	// the list's wheel and scroll bar
			if (wheel) scrollTo (scroll - wheel * (ROWH / 2));
			long pos = scroll;
			if (bar.mouse (mx, my, bl, W - WK_SBW - 4, WK_SBW + 4, HDR + 2, PH - 4, listH (), PH, &pos))
			{
				scrollTo ((int) pos);
				if (hot >= 0) { hot = -1; invalidate (true); }
				pressed = bl != 0; down = -1;
				return true;
			}
		}
		if (g_cur < 0 && my >= HDR)
		{
			int col = mx / COLW2, row = (my - HDR - 12 + scroll) / ROWH, i = row * 2 + col;
			int x = 14 + col * COLW2, y = HDR + 12 + row * ROWH - scroll;
			if (my + scroll >= HDR + 12 && mx >= x && mx < x + COLW2 - 28 && my < y + ROWH - 8 && i >= 0 && i < g_nl) h = i;
		}
		if (h != hot) { hot = h; invalidate (true); }
		if (bl && !pressed) { pressed = true; down = h; if (onCrumb) down = -2; invalidate (true); }
		else if (!bl && pressed)
		{
			pressed = false;
			if (down == -2 && onCrumb) home ();
			else if (down >= 0 && down == h) show (h);
			down = -1;
			invalidate (true);
		}
		return true;
	}

	bool onKey (long k) override
	{
		if (g_cur >= 0 && g_pid > 0 && !g_closing)
		{
			ApKey key = { (int) k, kapi_get_modifiers () };
			kapi_mailbox_send (g_pid, AP_KEY, &key, sizeof key);
			return true;
		}
		if (g_cur < 0 && g_nl > 0)				// the list: arrows + Enter
		{
			int h = hot < 0 ? 0 : hot;
			if (k == KEY_RIGHT) h++;
			else if (k == KEY_LEFT) h--;
			else if (k == KEY_DOWN) h += 2;
			else if (k == KEY_UP) h -= 2;
			else if (k == KEY_ENTER && hot >= 0) { show (hot); return true; }
			else return false;
			if (h < 0) h = 0;
			if (h >= g_nl) h = g_nl - 1;
			hot = h; reveal (h); invalidate (true);
			return true;
		}
		return false;
	}
};

// ---- the menu --------------------------------------------------------------------------------------
static Menu g_menu;
static void on_home () { if (g_root) g_root->home (); }
static void on_quit () { kapi_menu_command (MENU_QUIT); }
static void on_l0 () { if (g_root) g_root->show (0); }  static void on_l1 () { if (g_root) g_root->show (1); }
static void on_l2 () { if (g_root) g_root->show (2); }  static void on_l3 () { if (g_root) g_root->show (3); }
static void on_l4 () { if (g_root) g_root->show (4); }  static void on_l5 () { if (g_root) g_root->show (5); }
static void on_l6 () { if (g_root) g_root->show (6); }  static void on_l7 () { if (g_root) g_root->show (7); }
static void on_l8 () { if (g_root) g_root->show (8); }  static void on_l9 () { if (g_root) g_root->show (9); }
static void on_l10 () { if (g_root) g_root->show (10); } static void on_l11 () { if (g_root) g_root->show (11); }
static void on_l12 () { if (g_root) g_root->show (12); } static void on_l13 () { if (g_root) g_root->show (13); }
static void on_l14 () { if (g_root) g_root->show (14); } static void on_l15 () { if (g_root) g_root->show (15); }
static const MenuAction ON_L[16] = { on_l0, on_l1, on_l2, on_l3, on_l4, on_l5, on_l6, on_l7, on_l8, on_l9, on_l10, on_l11, on_l12, on_l13, on_l14, on_l15 };

// Another Control Panel runs (perhaps on another workspace): bring it to the front.
static void raise_other (void)
{
	if (kapi_raise_app ("control")) return;
	struct kapi_win_info L[24];
	int n = kapi_win_list (L, 24);
	for (int i = 0; i < n; i++)
		if (fs_ci_cmp (L[i].title, "Control Panel") == 0) { kapi_win_raise (L[i].id); return; }
}

int main (void)
{
	ft_wtk_install ("DejaVu Sans", 13);		// (before the widgets; false: the bitmap font)
	if (!kapi_ipc_register (AP_SERVICE)) { raise_other (); return 0; }
	g_self = kapi_ipc_lookup (AP_SERVICE);
	char args[64]; kapi_get_args (args, sizeof args);
	read_links ();
	ControlRoot root;
	if (root.canvas.px == 0) return 1;
	g_root = &root;
	g_sid = kapi_surface_create (W, PH);
	g_spx = g_sid > 0 ? kapi_surface_map (g_sid) : 0;
	if (g_spx == 0) { g_sid = 0; notify ("Control Panel", "No memory for the applets' pane."); }
	g_menu.menu ("Settings");
	g_menu.item ("All Settings", "", 0, on_home);
	g_menu.separator ();
	for (int i = 0; i < g_nl && i < 16; i++) g_menu.item (g_link[i].name, "", 0, ON_L[i]);
	g_menu.separator ();
	g_menu.item ("Quit", "^Q", WK_CTRL ('Q'), on_quit);
	g_menu.publish ();
	trim (args);
	if (args[0])
	{
		int i = link_by_target (args);
		if (i >= 0) start_applet (i);
	}
	root.run ();
	// ending: the applet shown too (it ends by itself when we are gone; a word first)
	if (g_pid > 0) kapi_mailbox_send (g_pid, AP_CLOSE, 0, 0);
	return 0;
}
