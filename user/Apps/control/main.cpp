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
// The window: the applets as links in a navigation SidePanel at its left (uikit/sidepanel.h: their pictures, their
// titles -- and their lines of help on a big window), the chosen one in the pane filling the rest under its title and
// help. It opens on the applet shown last (SD:/etc/control.ini "last = <target>"), else the first; it is resizable.
// A link chosen starts its target with "--applet <surface> <pid>": the applet (a uikit app) draws into the pane --
// a shared surface the pane's size, 700 x 470 at least (the applets' own layouts: a smaller pane shows it with scroll
// bars) -- and the Control Panel copies it into its window when the applet says so (AP_PRESENT), and sends it the
// pointer and the keys; F6 moves the keys between the links and the applet. `control <target>` opens that applet at
// once (the dock's Panel Settings..., pocket's Settings). One Control Panel at a time (it is the IPC service
// AP_SERVICE, "control"): a second `control <target>` asks it for that applet (AP_OPEN) and brings it to the front.
//
// The same binary in every mode (docs/POCKETUI-TECH-STUDY.md phase P6): the desktop and pocket's landscape as above
// (PocketUI fills the window; Up / Down and Enter in the links, Right or Tab into the applet); pocket's portrait -- and a window
// under 600 px wide: the list of the applets (one column: picture, title, help, a chevron)
// and, an applet open, a bar with "< Control Panel" and its title to go back; console: the SidePanel's column (the
// d-pad, L1 / R1 -- Ctrl+Page Up / Down -- the applets).
//
#include "appkit/appkit.h"
#include "systemkit/systemkit.h"
#include "uikit/bmp.h"
#include "filekit/filekit.h"
#include "uikit/uikit.h"
#include "fontkit/uikitface.h"		// FreeType's text (DejaVu Sans) for every widget

using namespace uikit;

#define AW	700			// the applets' size (their layouts: the least pane)
#define AH	470
#define SIDE0	204			// the links' width (a window under 1000 px wide)
#define PHDR	56			// the pane's head: the applet's title and help
#define BAR	46			// portrait's bar ("< Control Panel")
#define W0	(SIDE0 + AW)		// the window at first
#define H0	(PHDR + AH)
#define LINKS	"SD:/apps/control.app/applets"
#define LAST	"SD:/etc/control.ini"	// the applet shown last
#define MAXL	32
#define ROWH	78			// portrait's list: a card

// The links' names and texts are shown in the system's language (TR (l.name), TR (l.text)): the
// card's applets' words, said here for tools/lang/check.py (a new applet: its two lines here, and
// in lang/fr.txt).
// TR: Theme
// TR: Colours of the windows, menu bar, dock; the wallpaper
// TR: Mode
// TR: The interface: desktop, pocket or console
// TR: Display
// TR: The screen's resolution, changed at once
// TR: Panel
// TR: The dock's drawers and launchers; the workspaces
// TR: Sound
// TR: The volume, mute, a test sound
// TR: Preload
// TR: The programs loaded at boot and kept in memory
// TR: Keyboard & Mouse
// TR: The keyboard's layout; the mouse wheel's speed
// TR: Language & Region
// TR: The language of the programs; the time zone
// TR: Printers
// TR: The printers (PDF, network), the default one, a test page, the print queue
// TR: Gamepad
// TR: The USB gamepads: what they send, their buttons mapped
// TR: Wi-Fi
// TR: The Wi-Fi network: its name, its password, the country
// TR: Packages
// TR: Updates, the apps installed, more apps to install
// TR: App Settings
// TR: An app's own settings (its config.ini), key by key
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
static int       g_sid;				// the applets' surface
static unsigned *g_spx;				// ... mapped here
static int       g_sw, g_sh;			// ... its size: the pane's, AW x AH at least (the applets' own layouts)
static int       g_cur = -1;			// the applet shown (a link), -1: none (portrait: the list)
static int       g_pid;				// its process (0: not up yet)
static unsigned  g_t0;				// when it was started / asked to close
static bool      g_closing;			// AP_CLOSE sent, AP_EXIT awaited
static bool      g_fresh;			// it drew since we last copied
static char      g_status[96];
static int       g_ax, g_ay, g_aw = AW, g_ah = AH;	// the pane: where the applet shows in the window
static int       g_vx, g_vy;			// ... the surface scrolled by (a pane smaller than the applet)
static SidePanel *g_side;			// the applets as links (the window's left; hidden in portrait)
static FtTextFace *g_big;			// the pane's title

class ControlRoot;
static ControlRoot *g_root;

// The last applet shown, kept for the next start (SD:/etc/control.ini "last = <target>").
static void last_save (int i)
{
	if (i < 0 || i >= g_nl) return;
	char b[96]; int n = 0;
	lx_cat (b, sizeof b, &n, "last = "); lx_cat (b, sizeof b, &n, g_link[i].target); lx_cat (b, sizeof b, &n, "\n");
	kapi_save_file (LAST, b, (unsigned) n);
}
static int last_read (void)
{
	void *f = kapi_open (LAST);
	if (f == 0) return -1;
	char b[128]; int n = kapi_read (f, b, sizeof b - 1); kapi_close (f);
	if (n <= 0) return -1;
	b[n] = 0;
	char *v = b; while (*v && *v != '=') v++;
	if (*v != '=') return -1;
	v++; while (*v == ' ') v++;
	trim (v);
	return link_by_target (v);
}

// The surface the pane's size, the applets' at least (made again only when that changed: before an applet starts).
static bool surface_fit (void)
{
	int w = g_aw > AW ? g_aw : AW, h = g_ah > AH ? g_ah : AH;
	if (g_sid > 0 && g_sw == w && g_sh == h) return true;
	if (g_sid > 0) kapi_surface_destroy (g_sid);
	g_sid = kapi_surface_create (w, h);
	g_spx = g_sid > 0 ? kapi_surface_map (g_sid) : 0;
	if (g_spx == 0) { g_sid = 0; g_sw = g_sh = 0; return false; }
	g_sw = w; g_sh = h; g_vx = g_vy = 0;
	return true;
}

static void start_applet (int i)
{
	if (i < 0 || i >= g_nl) return;
	if (!surface_fit ()) { fs_copy (g_status, TR ("No memory for the applets' pane."), sizeof g_status); g_cur = i; g_pid = 0; return; }
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
	for (unsigned k = 0; k < (unsigned) (g_sw * g_sh); k++) g_spx[k] = C_BG;	// (a blank pane meanwhile)
	g_vx = g_vy = 0;
	char name[48]; int m = 0; lx_cat (name, sizeof name, &m, path ? "applet" : t);
	if (g_side) g_side->select (i);
	last_save (i);
	if (!kapi_exec_as (exe, args, name))
	{
		fs_copy (g_status, TR ("Cannot start this applet (its program is missing?)"), sizeof g_status);
		g_cur = i; g_pid = 0; g_closing = false;
		return;
	}
	g_cur = i; g_pid = 0; g_closing = false; g_fresh = true;
	g_t0 = kapi_get_ticks ();
	g_status[0] = 0;
}

// Ask the applet shown to end (its AP_EXIT, or a second later: killed).
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
static int g_winW = W0;			// the window's width (place)
// Portrait -- and a window under 600 px wide: the list, then an applet under a bar.
static bool narrow () { return uk_size_class () == UK_SC_NARROW || g_winW < 600; }

class ControlRoot : public Root
{
public:
	int hot = -1, down = -1;		// portrait's list: the link under the pointer / pressed
	int scroll = 0;				// ... scrolled (px)
	UkBarDrag bar;				// ... its scroll bar
	bool backHot = false;			// portrait's bar: "< Control Panel" under the pointer
	int ptrButtons = 0;			// the pane: the buttons held (the events sent)
	bool inPane = false;
	int drag = 0;				// the pane's scroll bars: 1 the vertical one dragged, 2 the horizontal one
	int wanted = -1;			// the applet to show once the one shown has ended
	bool sized = false;			// the window has its size (pocket: PocketUI fills it at once) -- the surface made then
	unsigned started = 0;

	ControlRoot () : Root (W0, H0, TR ("Control Panel")) {}

	int listH () const { return 24 + g_nl * ROWH; }
	void scrollTo (int v)
	{
		int most = listH () - height;
		if (v > most) v = most;
		if (v < 0) v = 0;
		if (v != scroll) { scroll = v; invalidate (true); }
	}
	void reveal (int i)			// the i-th applet's card wholly shown
	{
		int y = 12 + i * ROWH;
		if (y - 12 < scroll) scrollTo (y - 12);
		else if (y + ROWH + 4 > scroll + height) scrollTo (y + ROWH + 4 - height);
	}
	bool listShown () const { return narrow () && g_cur < 0 && wanted < 0; }
	int titled () const { return wanted >= 0 ? wanted : g_cur; }	// the applet the pane is about

	// ---- the geometry --------------------------------------------------------------------------------
	// The links' width: 204 px on the desktop's window and pocket's 800 x 480, more on a big one (the help lines).
	int sideWidth () const { return width < 1000 ? SIDE0 : width < 1200 ? 240 : 300; }
	void place ()
	{
		g_winW = width;
		if (narrow ())
		{
			g_side->hidden = true;
			g_ax = 0; g_ay = BAR; g_aw = width; g_ah = height - BAR;
		}
		else
		{
			g_side->hidden = false;
			g_side->place (0, 0, sideWidth (), height);
			int sw = g_side->reservedWidth ();
			g_ax = sw; g_ay = PHDR; g_aw = width - sw; g_ah = height - PHDR;
		}
		if (g_aw < 40) g_aw = 40;
		if (g_ah < 40) g_ah = 40;
		clampView ();
	}
	void clampView ()
	{
		if (g_vx > g_sw - g_aw) g_vx = g_sw - g_aw;
		if (g_vy > g_sh - g_ah) g_vy = g_sh - g_ah;
		if (g_vx < 0) g_vx = 0;
		if (g_vy < 0) g_vy = 0;
	}
	void onResized () override { onSizeClass (0); }
	void onSizeClass (int) override
	{
		place ();
		if (!narrow () && g_cur < 0 && wanted < 0 && sized) firstShow ();	// (landscape again: an applet shown)
		invalidate (true);
	}
	void firstShow ()			// the last applet shown, else the first
	{
		int i = last_read ();
		show (i >= 0 ? i : 0);
	}

	// ---- drawing ---------------------------------------------------------------------------
	void drawHeader ()			// the pane's head: the applet's title and its line of help
	{
		int x = g_ax, w = width - g_ax;
		canvas.fillRect (x, 0, w, PHDR, C_BG);
		int t = titled ();
		const char *title = t >= 0 ? TR (g_link[t].name) : TR ("Control Panel");
		int th;
		{
			UkFaceScope fs (g_big);
			uk_text (canvas, x + 16, 9, title, C_TEXT, 2);
			th = uk_fh ();
		}
		if (t >= 0 && g_link[t].text[0])
		{
			char b[160]; uk_text_fit (TR (g_link[t].text), w - 32, b, sizeof b);
			uk_text (canvas, x + 16, 9 + th + 3, b, uk_mix (C_BG, C_TEXT, 170));
		}
		uk_etch_h (canvas, x + 12, PHDR - 2, w - 24, C_BG);
	}
	void drawBar ()				// portrait, an applet open: "< Control Panel" and its title
	{
		canvas.fillRect (0, 0, width, BAR, C_BG);
		const char *back = TR ("Control Panel");
		int bw = 34 + uk_text_w (back, 0);
		if (backHot) uk_rbox (canvas, 6, 7, bw, BAR - 14, 6, uk_tone (C_BG, 150), uk_tone (C_BG, 136));
		uk_glyph (canvas, WKG_CHEV_LEFT, 20, BAR / 2, 12, C_ACCENT);
		uk_text_l (canvas, 32, 0, BAR, back, C_ACCENT);
		int t = titled ();
		if (t >= 0)
		{
			char b[64]; int room = width - (bw + 24) - 12;
			uk_text_fit (TR (g_link[t].name), room, b, sizeof b, 2);
			uk_text_l (canvas, bw + 24, 0, BAR, b, C_TEXT, 2);
		}
		uk_etch_h (canvas, 0, BAR - 2, width, C_BG);
	}
	void drawList ()			// portrait: the applets, one a card -- its picture, title, help and a chevron
	{
		canvas.fillRect (0, 0, width, height, C_BG);
		if (g_nl == 0) { uk_text_c (canvas, 0, 0, width, 60, TR ("No applet: SD:/apps/control.app/applets/*.lnk"), C_DIS); return; }
		int fh = uk_fh ();
		for (int i = 0; i < g_nl; i++)
		{
			const Link &l = g_link[i];
			int x = 12, y = 12 + i * ROWH - scroll, w = width - 24, h = ROWH - 8;
			if (y + h <= 0 || y >= height) continue;
			bool on = i == hot;
			if (on) uk_hilite (canvas, x, y, w, h, 8, down == i);
			else uk_rbox (canvas, x, y, w, h, 8, uk_tone (C_BG, 150), uk_tone (C_BG, 136), 120);
			if (l.icon)
				for (int j = 0; j < l.ih && j < 40; j++)
					for (int k = 0; k < l.iw && k < 40; k++)
					{
						unsigned c = l.icon[j * l.iw + k] & 0xFFFFFF;
						int py = y + (h - 40) / 2 + j;
						if (c != 0xFF00FF && py >= 0 && py < height) canvas.pixel (x + 12 + k, py, c);
					}
			unsigned ink = on ? uk_hilite_ink (down == i) : C_TEXT;
			uk_text (canvas, x + 64, y + 10, TR (l.name), ink, 2);
			uk_glyph (canvas, WKG_CHEV_RIGHT, x + w - 18, y + h / 2, 9, on ? ink : uk_mix (C_BG, C_TEXT, 150));
			int maxw = w - 76 - 24, line = 0;		// the help, over two lines at most (the face is proportional)
			const char *p = TR (l.text);
			while (*p && line < 2)
			{
				int len = 0, cut = -1;
				while (p[len] && uk_tw_n (p, len + 1) <= maxw) { if (p[len] == ' ') cut = len; len++; }
				if (p[len] && cut > 0) len = cut;
				if (len == 0) len = 1;
				char t[80]; int k = 0;
				for (int q = 0; q < len && k < 79; q++) t[k++] = p[q];
				t[k] = 0;
				canvas.text (x + 64, y + 14 + fh + line * (fh + 1), t, on ? ink : uk_mix (C_BG, C_TEXT, 170));
				p += len; while (*p == ' ') p++;
				line++;
			}
		}
		if (listH () > height)
			uk_draw_vscroll (canvas, width - UK_SBW - 2, 2, UK_SBW, height - 4, uk_thumb (listH (), height, scroll, height - 4), C_BG, bar.held);
	}
	void drawPane ()
	{
		canvas.fillRect (g_ax, g_ay, g_aw, g_ah, C_BG);
		if (g_pid <= 0 || g_closing || g_spx == 0)
		{
			const char *s = g_status[0] ? g_status : g_closing ? TR ("Closing...") : TR ("Starting...");
			uk_text_c (canvas, g_ax, g_ay, g_aw, g_ah, s, C_DIS);
			return;
		}
		int cw = g_sw - g_vx < g_aw ? g_sw - g_vx : g_aw, ch = g_sh - g_vy < g_ah ? g_sh - g_vy : g_ah;
		for (int y = 0; y < ch && g_ay + y < height; y++)
		{
			unsigned *d = canvas.px + (long) (g_ay + y) * canvas.stride + g_ax;
			const unsigned *s = g_spx + (long) (g_vy + y) * g_sw + g_vx;
			for (int x = 0; x < cw && g_ax + x < width; x++) d[x] = s[x] & 0x00FFFFFFu;
		}
		int sb = UK_SBW;					// the pane's bars: the applet bigger than the pane
		if (g_sh > g_ah) uk_scroll_bar (canvas, g_ax + g_aw - sb - 2, g_ay + 2, sb, g_ah - 4 - (g_sw > g_aw ? sb : 0), true,
					       (g_ah - 4) * g_vy / g_sh, (g_ah - 4) * g_ah / g_sh, C_BG, drag == 1 ? UK_PRESSED : UK_NORMAL);
		if (g_sw > g_aw) uk_scroll_bar (canvas, g_ax + 2, g_ay + g_ah - sb - 2, g_aw - 4 - (g_sh > g_ah ? sb : 0), sb, false,
					       (g_aw - 4) * g_vx / g_sw, (g_aw - 4) * g_aw / g_sw, C_BG, drag == 2 ? UK_PRESSED : UK_NORMAL);
	}

	void onDraw () override
	{
		if (listShown ()) { drawList (); return; }
		drawPane ();
		if (narrow ()) drawBar (); else drawHeader ();
	}

	// ---- each frame: the applet's messages ---------------------------------------------------
	void onTick () override
	{
		if (!sized && (uk_size_class () == UK_SC_REGULAR || kapi_get_ticks () - started > 30 || width != W0 || height != H0))
		{
			sized = true; place ();
			if (wanted < 0 && g_cur < 0 && !narrow () && g_nl > 0) { int i = last_read (); wanted = i >= 0 ? i : 0; }
		}
		int from = 0, type = 0, n;
		unsigned char buf[80];
		while ((n = kapi_mailbox_recv (&from, &type, buf, sizeof buf - 1, 0)) >= 0)
		{
			if (type == AP_OPEN)				// (another `control <target>`: that applet)
			{
				buf[n < (int) sizeof buf - 1 ? n : (int) sizeof buf - 1] = 0;
				int i = link_by_target ((const char *) buf);
				if (i >= 0) show (i);
				continue;
			}
			if (g_cur < 0) continue;
			if (type == AP_HELLO && g_pid == 0 && !g_closing) { g_pid = from; g_fresh = true; }
			else if (from != g_pid) continue;
			else if (type == AP_PRESENT) g_fresh = true;
			else if (type == AP_EXIT) { g_pid = 0; g_cur = -1; g_closing = false; }
			else if (type == AP_THEME)			// a new theme: ours, and the applet again
			{
				uk_theme_reload ();
				bg = C_BG;
				g_side->setColors (UK_AUTO, UK_AUTO);
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
			fs_copy (g_status, TR ("The applet did not start."), sizeof g_status);
		if (g_cur < 0 && wanted >= 0 && sized) { int w = wanted; wanted = -1; start_applet (w); place (); }
		if (g_fresh || g_cur < 0 || g_closing) { g_fresh = false; invalidate (true); }
	}

	void show (int i)
	{
		if (i < 0 || i >= g_nl || g_cur == i) return;
		if (g_cur >= 0) { wanted = i; close_applet (); }
		else if (!sized) wanted = i;			// (pocket: once the window has its size)
		else { start_applet (i); place (); }
		if (g_side) g_side->select (i);
		invalidate (true);
	}
	void home ()					// portrait: back to the list; else the links focused
	{
		if (!narrow ()) { g_side->setFocus (); invalidate (true); return; }
		wanted = -1; close_applet (); g_side->select (-1); place (); invalidate (true);
	}

	// ---- the pointer ------------------------------------------------------------------------------
	void sendPtr (int ev, int x, int y, int buttons, int changed, int wheel)
	{
		ApPtr e = { ev, x, y, buttons, changed, wheel };
		kapi_mailbox_send (g_pid, AP_PTR, &e, sizeof e);
	}
	bool paneMouse (int mx, int my, int bl, int btn, int wheel)	// -> true: the pane's (the applet's or its bars')
	{
		bool vb = g_sh > g_ah, hb = g_sw > g_aw;
		bool onV = vb && mx >= g_ax + g_aw - UK_SBW - 6 && mx < g_ax + g_aw && my >= g_ay && my < g_ay + g_ah;
		bool onH = hb && !onV && my >= g_ay + g_ah - UK_SBW - 6 && my < g_ay + g_ah && mx >= g_ax && mx < g_ax + g_aw;
		if (drag || ((onV || onH) && bl && !ptrButtons))	// the bars: the surface scrolled
		{
			if (!bl) { drag = 0; invalidate (true); return true; }
			if (!drag) drag = onV ? 1 : 2;
			if (drag == 1) g_vy = (my - g_ay) * (g_sh - g_ah) / (g_ah > 1 ? g_ah : 1);
			else g_vx = (mx - g_ax) * (g_sw - g_aw) / (g_aw > 1 ? g_aw : 1);
			clampView (); invalidate (true);
			return true;
		}
		if ((onV || onH) && wheel && !ptrButtons)
		{
			if (onV) g_vy -= wheel * 40; else g_vx -= wheel * 40;
			clampView (); invalidate (true);
			return true;
		}
		bool paneShown = g_cur >= 0 && g_pid > 0 && !g_closing;
		if (!paneShown || mx < 0 || ((my < g_ay || mx < g_ax) && !ptrButtons)) return false;
		if (bl && !ptrButtons && g_side->hasFocus) { clearFocusTree (); hasFocus = true; }	// (the keys the applet's again)
		int x = mx - g_ax + g_vx, y = my - g_ay + g_vy;
		if (!inPane) { inPane = true; sendPtr (GUI_EVENT_PTR_ENTER, x, y, btn, 0, 0); }
		if (wheel) sendPtr (GUI_EVENT_PTR_WHEEL, x, y, btn, 0, wheel);
		else if (btn != ptrButtons)
		{
			int changed = btn ^ ptrButtons;
			sendPtr ((btn & changed) ? GUI_EVENT_PTR_DOWN : GUI_EVENT_PTR_UP, x, y, btn, changed, 0);
		}
		else sendPtr (GUI_EVENT_PTR_MOVE, x, y, btn, 0, 0);
		ptrButtons = btn;
		return true;
	}
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override
	{
		int btn = (bl ? 1 : 0) | (br ? 2 : 0) | (bm ? 4 : 0);
		if (!listShown () && paneMouse (mx, my, bl, btn, wheel)) return true;
		if (inPane && g_cur >= 0 && g_pid > 0 && !g_closing) sendPtr (GUI_EVENT_PTR_LEAVE, -1, -1, 0, 0, 0);
		inPane = false; ptrButtons = 0;
		if (mx < 0) { if (hot >= 0 || backHot) { hot = -1; backHot = false; invalidate (true); } pressed = false; return false; }
		bool onBack = narrow () && g_cur >= 0 && my < BAR && mx < 40 + uk_text_w (TR ("Control Panel"), 0);
		if (onBack != backHot) { backHot = onBack; invalidate (true); }
		int h = -1;
		if (listShown ())
		{
			if (listH () > height)				// the list's wheel and scroll bar
			{
				if (wheel) scrollTo (scroll - wheel * (ROWH / 2));
				long pos = scroll;
				if (bar.mouse (mx, my, bl, width - UK_SBW - 4, UK_SBW + 4, 2, height - 4, listH (), height, &pos))
				{
					scrollTo ((int) pos);
					if (hot >= 0) { hot = -1; invalidate (true); }
					pressed = false;
					return true;
				}
			}
			int row = (my - 12 + scroll) / ROWH, y = 12 + row * ROWH - scroll;
			if (my + scroll >= 12 && mx >= 12 && mx < width - 12 && my < y + ROWH - 8 && row >= 0 && row < g_nl) h = row;
		}
		if (h != hot) { hot = h; invalidate (true); }
		if (bl && !pressed) { pressed = true; down = onBack ? -2 : h; invalidate (true); }
		else if (!bl && pressed)
		{
			pressed = false;
			if (down == -2 && onBack) home ();
			else if (down >= 0 && down == h) show (h);
			down = -1;
			invalidate (true);
		}
		return true;
	}

	bool onKey (long k) override
	{
		if ((k == KEY_PGUP || k == KEY_PGDN) && (kapi_get_modifiers () & MOD_CTRL) && uk_size_class () != UK_SC_REGULAR)	// L1 / R1: the applets
		{
			int c = titled ();
			int i = (c < 0 ? (k == KEY_PGDN ? -1 : g_nl) : c) + (k == KEY_PGDN ? 1 : -1);
			if (i >= 0 && i < g_nl) show (i);
			return true;
		}
		if (!narrow () && g_side->hasFocus && (k == KEY_RIGHT || k == KEY_TAB) && g_cur >= 0)	// (the links: -> or Tab enters the applet)
		{
			clearFocusTree (); hasFocus = true;
			invalidate (true);
			return true;
		}
		if (!narrow () && k == KEY_F1 + 5)			// F6: the links / the applet
		{
			if (g_side->hasFocus) { clearFocusTree (); hasFocus = true; } else g_side->setFocus ();
			invalidate (true);
			return true;
		}
		if (narrow () && g_cur >= 0 && k == 27) { home (); return true; }	// (portrait: back to the list)
		if (g_cur >= 0 && g_pid > 0 && !g_closing)
		{
			ApKey key = { (int) k, kapi_get_modifiers () };
			kapi_mailbox_send (g_pid, AP_KEY, &key, sizeof key);
			return true;
		}
		if (listShown () && g_nl > 0)				// the list: arrows + Enter
		{
			int h = hot < 0 ? 0 : hot;
			if (k == KEY_DOWN) h++;
			else if (k == KEY_UP) h--;
			else if (k == KEY_ENTER && hot >= 0) { show (hot); return true; }
			else return false;
			if (h < 0) h = 0;
			if (h >= g_nl) h = g_nl - 1;
			if (h != hot) { hot = h; reveal (h); invalidate (true); }
			return true;
		}
		return false;
	}
};

// The links' icons in the SidePanel: the applet's picture, scaled to the box.
static void link_icon (Canvas &cv, int id, int x, int y, int size, unsigned, bool)
{
	if (id < 0 || id >= g_nl || g_link[id].icon == 0) return;
	const Link &l = g_link[id];
	for (int j = 0; j < size; j++)
		for (int k = 0; k < size; k++)
		{
			unsigned c = l.icon[(j * l.ih / size) * l.iw + k * l.iw / size] & 0xFFFFFF;
			if (c != 0xFF00FF) cv.pixel (x + k, y + j, c);
		}
}
static void side_pick (SidePanel &, int id) { if (g_root) g_root->show (id); }

// ---- the menu --------------------------------------------------------------------------------------
static Menu g_menu;
static void on_home () { if (g_root) g_root->home (); }
static void on_quit () { uk_win_menu_command (MENU_QUIT); }
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
	if (uk_win_app_raise ("control")) return;
	struct kapi_win_info L[40];
	int n = uk_win_list (L, 40);
	for (int i = 0; i < n; i++)
		if (fs_ci_cmp (L[i].title, TR ("Control Panel")) == 0) { uk_win_raise (L[i].id); return; }
}

int main (void)
{
	ft_uikit_install ("DejaVu Sans", 13);		// (before the widgets; false: the bitmap font)
	uk_lang_init ();				// the words in the system's language (the face first: UTF-8)
	char args[64]; kapi_get_args (args, sizeof args);
	if (!kapi_ipc_register (AP_SERVICE))			// one Control Panel: that one shows the applet asked, in front
	{
		trim (args);
		int host = kapi_ipc_lookup (AP_SERVICE);
		if (args[0] && host > 0) kapi_mailbox_send (host, AP_OPEN, args, (unsigned) fs_len (args) + 1);
		raise_other ();
		return 0;
	}
	g_self = kapi_ipc_lookup (AP_SERVICE);
	read_links ();
	ControlRoot root;
	if (root.canvas.px == 0) return 1;
	g_root = &root;
	root.started = kapi_get_ticks ();
	g_big = new FtTextFace; if (!g_big->open ("DejaVu Sans", 17)) { delete g_big; g_big = 0; }
	g_side = new SidePanel (0, 0, SIDE0, H0, UK_SP_LEFT, UK_SP_NAVIGATION);
	g_side->setRail (false);				// (pocket's landscape: the links whole, never a rail of icons)
	g_side->setIconFn (link_icon);
	for (int i = 0; i < g_nl; i++)
	{
		g_side->addItem (i, TR (g_link[i].name), g_link[i].icon ? i : -1);
		g_side->setSubtitle (i, TR (g_link[i].text));
	}
	g_side->onSelect = side_pick;
	root.addChild (g_side);
	root.setResizable (true);
	root.setMinSize (360, 320);
	root.place ();
	g_menu.menu (TR ("Settings"));
	g_menu.item (TR ("All Settings"), "", 0, on_home);
	g_menu.separator ();
	for (int i = 0; i < g_nl && i < 16; i++) g_menu.item (TR (g_link[i].name), "", 0, ON_L[i]);
	g_menu.separator ();
	g_menu.item (TR ("Quit"), "^Q", UK_CTRL ('Q'), on_quit);
	g_menu.publish ();
	trim (args);
	int first = args[0] ? link_by_target (args) : -1;
	if (first >= 0) root.show (first);			// (else: the last one, once the window has its size --
	root.run ();						//  portrait: the list)
	// ending: the applet shown too (it ends by itself when we are gone; a word first)
	if (g_pid > 0) kapi_mailbox_send (g_pid, AP_CLOSE, 0, 0);
	return 0;
}
