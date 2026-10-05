//
// dock -- the dock: the modernised CDE's Front Panel, along the bottom of the screen (it replaces
// the Shelf and the panel). In its middle the workspaces and the system buttons; its buttons --
// the drawers, the launchers, the Trash, in this order -- shared evenly on the two sides (the odd
// one out at the left):
//   * the DRAWERS (SD:/etc/dock.ini, dockconf.h -- the Control Panel's Panel applet sets them): a
//     launcher each, the icon of its group's main app -- a click starts that app (or brings it
//     back); the strip on its top edge opens the group's drawer above it (its apps and their
//     icons; a click starts one, or brings it back, minimised too), as Xfce's launchers. A dot
//     under a launcher when one of its apps has a window on this workspace, and in the drawer
//     beside the app. Files dropped on a launcher are opened by its app;
//   * the launchers (the Terminal, the File Viewer... dock.ini) and the Trash (files dropped on it
//     go there; a click opens it in the File Viewer);
//   * in the middle, the WORKSPACES (virtual desktops, kernel v65): a small square each, the
//     current one lit, its windows drawn small in it; a click shows it (Ctrl+Alt+Left / Right
//     too). Beside them, at the left the lock (apps/lock) over the gear (the Control Panel:
//     apps/control), at the right the power (apps/shutdown) over the clipboard (apps/clipboard).
// A click outside an open drawer closes it; the pointer on a launcher names it. A right click on
// the dock: Panel Settings... (the Control Panel's Panel applet).
//
// The window: borderless, topmost (above the apps; never the keys'), a system one (not listed as
// an open app, on every workspace), see-through where there is no dock (WIN_FLAG_ALPHA: its
// rounded corners, the gap below it -- the clicks there go to what is below). A drawer (or a name)
// grows it upward; while a drawer is open it covers the screen, almost clear, to catch a click
// elsewhere. It stands on the screen's bottom edge, so the work area (maximised windows) ends
// above it. IPC service "dock": DOCK_MSG_RELOAD reads dock.ini and the theme again.
//
#include "appkit/appkit.h"
#include "bmp.hpp"
#include "fsutil.h"
#include "fileassoc.h"
#include "launch.h"
#include "trash.h"
#include "notify.h"
#include "dockconf.h"
#include "uikit/uikit.h"

using namespace uikit;

#define MAXAPPS		48

enum {
	DH = 80,		// the dock's height
	GAP = 12,		// below it (see-through)
	CW = 60,		// a launcher's width
	ICON = 40,		// the apps' icons (icon.bmp)
	SI = 30,		// ... in a drawer
	RH = 38,		// a row of a drawer
	COLW = 206,		// a column of it
	MAXROWS = 11,
	STRIP_H = 16,		// a drawer launcher's strip (its top edge: opens the drawer)
	SQW = 34, SQH = 25,	// a workspace's square
	SQGAP = 5,
	LABEL_H = 30,		// the name shown above a launcher
};

// ---- the apps ------------------------------------------------------------------------------------
struct App { char name[32]; char label[40]; char cat[24]; unsigned *icon; int iw, ih; unsigned *small; bool tried, running; };
static App  g_apps[160];
static int  g_napps;

static App *find_app (const char *name)
{
	for (int i = 0; i < g_napps; i++) if (fs_ci_cmp (g_apps[i].name, name) == 0) return &g_apps[i];
	return 0;
}

// An icon (magenta = see-through) scaled to s x s: 4 x 4 samples a pixel, the colour of the
// opaque ones, the opacity their share -- into 0xTTRRGGBB (TT: the transparency).
static void scale_icon (const unsigned *src, int sw, int sh, unsigned *dst, int s)
{
	for (int j = 0; j < s; j++)
		for (int i = 0; i < s; i++)
		{
			unsigned r = 0, g = 0, b = 0, n = 0;
			for (int sy = 0; sy < 4; sy++)
				for (int sx = 0; sx < 4; sx++)
				{
					int x = ((i * 4 + sx) * sw + sw / 2) / (s * 4), y = ((j * 4 + sy) * sh + sh / 2) / (s * 4);
					unsigned c = src[y * sw + x] & 0xFFFFFF;
					if (c == 0xFF00FF) continue;
					r += (c >> 16) & 255; g += (c >> 8) & 255; b += c & 255; n++;
				}
			dst[j * s + i] = n == 0 ? 0xFF000000u
				: ((255 - n * 255 / 16) << 24) | ((r / n) << 16) | ((g / n) << 8) | (b / n);
		}
}

// Its icon, read the first time it is wanted (and its small copy for the drawers).
static void need_icon (App &a)
{
	if (a.tried) return;
	a.tried = true;
	char q[180]; int k = 0;
	lx_cat (q, sizeof q, &k, "SD:/apps/"); lx_cat (q, sizeof q, &k, a.name); lx_cat (q, sizeof q, &k, ".app/icon.bmp");
	a.icon = ui::bmp_decode (q, &a.iw, &a.ih);
	a.small = new unsigned[SI * SI];
	if (a.icon && a.iw > 0 && a.ih > 0) scale_icon (a.icon, a.iw, a.ih, a.small, SI);
	else for (int i = 0; i < SI * SI; i++) a.small[i] = 0xFF000000u;
}

static void scan_apps (void)
{
	for (int i = 0; i < g_napps; i++) { delete [] g_apps[i].icon; delete [] g_apps[i].small; }
	g_napps = 0;
	static char list[4096];
	kapi_list_apps (list, sizeof list);
	for (char *p = list; *p && g_napps < (int) (sizeof g_apps / sizeof g_apps[0]); )
	{
		char *e = p; while (*e && *e != '\n') e++;
		char c = *e; *e = '\0';
		App &a = g_apps[g_napps++];
		fs_copy (a.name, p, sizeof a.name);
		fs_copy (a.label, p, sizeof a.label);
		a.cat[0] = 0; a.icon = 0; a.small = 0; a.iw = a.ih = 0; a.tried = a.running = false;
		char q[180]; int k = 0;
		lx_cat (q, sizeof q, &k, "SD:/apps/"); lx_cat (q, sizeof q, &k, a.name); lx_cat (q, sizeof q, &k, ".app/app.txt");
		if (app_ini_load_path (q) >= 0)
		{
			const char *nm = app_ini_get (0, "name", 0);
			if (nm && nm[0]) fs_copy (a.label, nm, sizeof a.label);
			const char *ct = app_ini_get (0, "category", 0);
			if (ct) fs_copy (a.cat, ct, sizeof a.cat);
		}
		*e = c;
		p = *e ? e + 1 : e;
	}
}

static void blit_icon (Canvas &cv, int x, int y, const unsigned *px, int w, int h)
{
	if (px == 0) return;
	for (int j = 0; j < h; j++)
		for (int i = 0; i < w; i++)
		{
			unsigned c = px[j * w + i] & 0xFFFFFF;
			if (c != 0xFF00FF) cv.pixel (x + i, y + j, c);
		}
}

static void blit_small (Canvas &cv, int x, int y, const unsigned *px, int s)
{
	if (px == 0) return;
	for (int j = 0; j < s; j++)
		for (int i = 0; i < s; i++)
		{
			unsigned c = px[j * s + i], t = c >> 24;
			if (t == 255) continue;
			int xx = x + i, yy = y + j;
			if (xx < 0 || yy < 0 || xx >= cv.w || yy >= cv.h) continue;
			unsigned *p = cv.px + (long) yy * cv.stride + xx;
			*p = t == 0 ? (c & 0xFFFFFF) : uk_over (*p, c & 0xFFFFFF, 255 - (int) t);
		}
}

static void app_icon (Canvas &cv, int cx, int y, App *a)
{
	if (a == 0) return;
	need_icon (*a);
	if (a->icon) blit_icon (cv, cx - ICON / 2, y, a->icon, a->iw < ICON ? a->iw : ICON, a->ih < ICON ? a->ih : ICON);
}

// (each click said in the kernel log, kmsg: a launcher that starts nothing can be told apart --
//  no click, its window raised, its main not found)
static void dock_log (const char *a, const char *b)
{
	char t[160]; int n = 0;
	lx_cat (t, sizeof t, &n, "dock: "); lx_cat (t, sizeof t, &n, a); lx_cat (t, sizeof t, &n, b); lx_cat (t, sizeof t, &n, "\n");
	kapi_stdout_write (t, (unsigned) n);
}
static void launch_or_raise (const char *name)
{
	if (kapi_raise_app (name)) { dock_log (name, ": its window raised"); return; }
	if (lx_launch (name, 0)) dock_log (name, ": started");
	else dock_log (name, ": NOT started (no SD:/apps/<name>.app/main, nor a main.<ext>)");
}

// ---- the drawers, the launchers, the workspaces (dock.ini) ----------------------------------------
struct Drawer { char cat[24]; App *main; App *apps[MAXAPPS]; int n; bool running; };
static Drawer   g_dr[DOCK_MAXDRAWERS];
static int      g_ndr;
static DockConf g_conf;

static void build (void)
{
	dockconf_load (g_conf);
	g_ndr = 0;
	for (int d = 0; d < g_conf.ndrawers && g_ndr < DOCK_MAXDRAWERS; d++)
	{
		Drawer &k = g_dr[g_ndr];
		fs_copy (k.cat, g_conf.drawer[d].cat, sizeof k.cat);
		k.n = 0; k.running = false;
		for (int i = 0; i < g_napps && k.n < MAXAPPS; i++)
			if (fs_ci_cmp (g_apps[i].cat, k.cat) == 0) k.apps[k.n++] = &g_apps[i];
		for (int i = 1; i < k.n; i++)					// by name
			for (int j = i; j > 0 && fs_ci_cmp (k.apps[j - 1]->label, k.apps[j]->label) > 0; j--)
			{ App *t = k.apps[j]; k.apps[j] = k.apps[j - 1]; k.apps[j - 1] = t; }
		k.main = find_app (g_conf.drawer[d].app);
		if (k.main == 0 && k.n > 0) k.main = k.apps[0];
		g_ndr++;
	}
	// the kernel's workspaces: as many as named
	kapi_desk (-1, g_conf.ndesks > 0 ? g_conf.ndesks : 1);
}

// The apps with a window on this workspace (kapi_list_windows): their dots. -> changed?
static bool poll_running (void)
{
	static char buf[2048];
	kapi_list_windows (buf, sizeof buf);
	bool changed = false;
	for (int i = 0; i < g_napps; i++)
	{
		bool r = false;
		for (char *p = buf; *p; )
		{
			char *e = p; while (*e && *e != '\n') e++;
			int n = (int) (e - p);
			if (n == fs_len (g_apps[i].name))
			{
				bool same = true;
				for (int k = 0; k < n; k++) if (fs_lower (p[k]) != fs_lower (g_apps[i].name[k])) { same = false; break; }
				if (same) { r = true; break; }
			}
			p = *e ? e + 1 : e;
		}
		if (r != g_apps[i].running) { g_apps[i].running = r; changed = true; }
	}
	for (int d = 0; d < g_ndr; d++)
	{
		bool r = false;
		for (int i = 0; i < g_dr[d].n; i++) if (g_dr[d].apps[i]->running) r = true;
		g_dr[d].running = r;
	}
	return changed;
}

// The windows on each workspace, small (the squares): their place on the screen, focused or not.
struct Mini { int desk; short x, y, w, h; bool keys; };
static Mini     g_mini[24];
static int      g_nmini;
static unsigned g_miniSig;

static bool poll_minis (void)
{
	struct kapi_win_info L[24];
	int n = kapi_win_list (L, 24);
	g_nmini = 0;
	unsigned sig = 5381;
	for (int i = 0; i < n && g_nmini < 24; i++)
	{
		const kapi_win_info &w = L[i];
		if (w.id == KAPI_WIN_DESKTOP || (w.flags & (WIN_FLAG_BORDERLESS | WIN_FLAG_TOPMOST | WIN_FLAG_BACKMOST | WIN_FLAG_SYSTEM))) continue;
		if (w.state & KAPI_WIN_MINIMISED) continue;
		Mini &m = g_mini[g_nmini++];
		m.desk = KAPI_WIN_DESK (w.state);
		m.x = (short) (w.x - w.il); m.y = (short) (w.y - w.it);
		m.w = (short) (w.ow > 0 ? w.ow : w.w); m.h = (short) (w.oh > 0 ? w.oh : w.h);
		m.keys = (w.state & KAPI_WIN_KEYS) != 0;
		sig = sig * 33 + (unsigned) (m.desk + 1) * 7 + (unsigned) m.x * 3 + (unsigned) m.y * 5 + (unsigned) m.w + (unsigned) m.h * 11 + (m.keys ? 1 : 0);
	}
	bool changed = sig != g_miniSig;
	g_miniSig = sig;
	return changed;
}

static void trash_glyph (Canvas &cv, int cx, int y, bool full, unsigned face)
{
	unsigned c = uk_tone (face, 64), hi = uk_tone (face, 188);
	if (full) uk_rbox (cv, cx - 8, y + 2, 16, 9, 2, 0x00FFFFFF, 0x00E4E4E4);	// paper sticking out
	uk_rbox (cv, cx - 14, y + 8, 28, 5, 2, c, c);				// the lid, its handle
	uk_rbox (cv, cx - 5, y + 4, 10, 5, 2, c, c);
	uk_rbox (cv, cx - 11, y + 15, 22, 28, 4, uk_tone (face, 76), c);		// the can
	for (int k = -1; k <= 1; k++) uk_rbox (cv, cx + k * 6 - 1, y + 19, 2, 20, 1, hi, uk_tone (face, 150));	// its ribs
}

// ---- the dock's layout -------------------------------------------------------------------------------
static int g_sw = 1024, g_sh = 768, g_top = 30;			// the screen, the menu bar's height

enum { SL_DRAWER, SL_APP, SL_TRASH };
struct Slot { int kind, x, index; App *app; };
static Slot g_slot[DOCK_MAXDRAWERS + DOCK_MAXLAUNCHERS + 2]; static int g_nslot, g_nleft;	// (g_nleft: left of the middle)
static int  g_DX, g_DY, g_DW;					// the dock on the screen
static int  g_pgX, g_pgW, g_pgCols, g_pgRows, g_ndesk;		// the workspaces' panel (dock coordinates)

// The buttons in their order -- the drawers, the launchers, the Trash --, half of them on each side
// of the middle (the workspaces, the lock, the gear, the power; the odd one out at the left).
static void layout (void)
{
	Slot all[DOCK_MAXDRAWERS + DOCK_MAXLAUNCHERS + 2]; int n = 0;
	for (int d = 0; d < g_ndr; d++) all[n++] = { SL_DRAWER, 0, d, g_dr[d].main };
	for (int i = 0; i < g_conf.nlaunchers; i++)
	{
		App *a = find_app (g_conf.launcher[i]);
		if (a != 0) all[n++] = { SL_APP, 0, i, a };
	}
	all[n++] = { SL_TRASH, 0, -1, 0 };
	g_nleft = (n + 1) / 2;
	int x = 12; g_nslot = 0;
	for (int i = 0; i < g_nleft; i++) { all[i].x = x; g_slot[g_nslot++] = all[i]; x += CW; }
	g_ndesk = g_conf.ndesks < 1 ? 1 : g_conf.ndesks;
	g_pgRows = g_ndesk <= 3 ? 1 : 2;
	g_pgCols = (g_ndesk + g_pgRows - 1) / g_pgRows;
	g_pgX = x + (g_nleft ? 8 : 0);
	g_pgW = 30 + g_pgCols * (SQW + SQGAP) - SQGAP + 6 + 30;
	x = g_pgX + g_pgW + (n > g_nleft ? 8 : 0);
	for (int i = g_nleft; i < n; i++) { all[i].x = x; g_slot[g_nslot++] = all[i]; x += CW; }
	g_DW = x + 12;
	g_DX = (g_sw - g_DW) / 2;
	g_DY = g_sh - DH - GAP;
}

// A workspace's square (dock coordinates).
static void desk_box (int i, int *x, int *y)
{
	*x = g_pgX + 30 + (i % g_pgCols) * (SQW + SQGAP);
	*y = g_pgRows == 1 ? (DH - SQH) / 2 : (DH - 2 * SQH - 6) / 2 + (i / g_pgCols) * (SQH + 6);
}

// What the pointer is on (screen coordinates).
enum { H_NONE, H_SLOT, H_STRIP, H_DESK, H_LOCK, H_GEAR, H_POWER, H_DOCK, H_ITEM, H_DRAWER, H_CLIP };
struct Hit { int what, index; };

// The open drawer, its box (screen).
static int g_open = -1;						// the drawer shown (-1: none)
static bool g_menuUp = false;					// a pop-up menu is shown (the window grown)
static int g_px, g_py, g_pw, g_ph, g_pcx;			// the open drawer's box, its pointer's x

static void drawer_box (int d)
{
	Drawer &k = g_dr[d];
	int n = k.n > 0 ? k.n : 1, cols = (n + MAXROWS - 1) / MAXROWS, rows = n < MAXROWS ? n : MAXROWS;
	g_pw = cols * COLW + 12; g_ph = 44 + rows * RH + 8;
	for (int i = 0; i < g_nslot; i++) if (g_slot[i].kind == SL_DRAWER && g_slot[i].index == d) g_pcx = g_DX + g_slot[i].x + CW / 2;
	g_px = g_pcx - g_pw / 2;
	if (g_px < 4) g_px = 4;
	if (g_px + g_pw > g_sw - 4) g_px = g_sw - 4 - g_pw;
	g_py = g_DY - 12 - g_ph;
}

static Hit hit_test (int sx, int sy)
{
	Hit h = { H_NONE, -1 };
	if (g_open >= 0 && sx >= g_px && sx < g_px + g_pw && sy >= g_py && sy < g_py + g_ph)
	{
		h.what = H_DRAWER;
		Drawer &k = g_dr[g_open];
		int col = (sx - g_px - 6) / COLW, row = (sy - g_py - 42) / RH;
		int i = col * MAXROWS + row;
		if (sy >= g_py + 42 && row < MAXROWS && sx >= g_px + 6 && i >= 0 && i < k.n) { h.what = H_ITEM; h.index = i; }
		return h;
	}
	int x = sx - g_DX, y = sy - g_DY;
	if (x < 0 || y < 0 || x >= g_DW || y >= DH) return h;
	h.what = H_DOCK;
	for (int i = 0; i < g_nslot; i++)
		if (x >= g_slot[i].x && x < g_slot[i].x + CW)
		{
			h.what = g_slot[i].kind == SL_DRAWER && y < STRIP_H + 2 ? H_STRIP : H_SLOT;
			h.index = i;
			return h;
		}
	for (int i = 0; i < g_ndesk; i++)
	{
		int bx, by; desk_box (i, &bx, &by);
		if (x >= bx - 2 && x < bx + SQW + 2 && y >= by - 2 && y < by + SQH + 2) { h.what = H_DESK; h.index = i; return h; }
	}
	if (x >= g_pgX && x < g_pgX + 28) { h.what = y < DH / 2 ? H_LOCK : H_GEAR; return h; }
	if (x >= g_pgX + g_pgW - 30 && x < g_pgX + g_pgW) { h.what = y < DH / 2 ? H_POWER : H_CLIP; return h; }
	return h;
}

// The clipboard's glyph (a board with its clip), centred at (cx, cy), about 16 px
static void clip_glyph (Canvas &cv, int cx, int cy, unsigned ink)
{
	int x0 = V (cx - 6), y0 = V (cy - 6), x1 = V (cx + 6), y1 = V (cy + 8);
	int board[] = { x0, y0, x1, y0, x1, y1, x0, y1 };
	VPath o; o.polyline (board, 4, 28, true); o.fill (cv, ink);
	VPath c; c.rrect (V (cx - 3), V (cy - 8), V (6), V (4), V (1)); c.fill (cv, ink);
	VPath l; l.rect (V (cx - 3), V (cy), V (6), 24); l.rect (V (cx - 3), V (cy + 3), V (6), 24); l.fill (cv, ink);
}

static char g_tipBuf[64];
static const char *hit_tip (const Hit &h)
{
	switch (h.what)
	{
	case H_SLOT:
	{
		const Slot &s = g_slot[h.index];
		if (s.kind == SL_TRASH) return "Trash";
		return s.app ? s.app->label : 0;
	}
	case H_STRIP:
	{
		int p = 0; const Drawer &k = g_dr[g_slot[h.index].index];
		for (int i = 0; k.cat[i] && p < 40; i++) g_tipBuf[p++] = k.cat[i];
		const char *t = " (open the drawer)"; for (int i = 0; t[i]; i++) g_tipBuf[p++] = t[i];
		g_tipBuf[p] = 0;
		return g_tipBuf;
	}
	case H_DESK:
	{
		int p = 0; const char *t = "Workspace ";
		for (int i = 0; t[i]; i++) g_tipBuf[p++] = t[i];
		g_tipBuf[p++] = (char) ('1' + h.index);
		const char *nm = h.index < g_conf.ndesks ? g_conf.desk[h.index] : "";
		if (nm[0] && !(nm[0] == '1' + h.index && nm[1] == 0))
		{
			g_tipBuf[p++] = ':'; g_tipBuf[p++] = ' ';
			for (int i = 0; nm[i] && p < 60; i++) g_tipBuf[p++] = nm[i];
		}
		g_tipBuf[p] = 0;
		return g_tipBuf;
	}
	case H_LOCK:  return "Lock the screen";
	case H_GEAR:  return "Control Panel";
	case H_POWER: return "Shut down";
	case H_CLIP:  return "Clipboard";
	}
	return 0;
}

class DockRoot : public Root
{
public:
	int wx, wy;				// the window on the screen
	Hit hot = { H_NONE, -1 }, down = { H_NONE, -1 };
	Hit dropHot = { H_NONE, -1 };		// what a drag hovers
	bool trashFull = false;
	unsigned lastPoll = 0, lastTrash = 0, lastMini = 0;
	unsigned settleUntil = 0;				// (placeWindow)
	int lastDeskInfo = -1;
	const char *tip = 0;			// the name shown above the dock (the pointer's launcher)

	DockRoot (int x, int y, int w, int h)
	  : Root (x, y, w, h, "dock", WIN_FLAG_BORDERLESS | WIN_FLAG_TOPMOST | WIN_FLAG_SYSTEM | WIN_FLAG_ALPHA),
	    wx (x), wy (y) {}

	// The window for what is shown: the dock and the room for a name above it, or the screen (a
	// drawer). The room for the name is always there (see-through, the clicks go through it): the
	// window grew and moved up when a name came -- the pointer's moves already on their way were
	// then read 30 pixels off, and a pointer on a drawer's strip was taken to be on the launcher
	// under it.
	void placeWindow ()
	{
		int x = g_DX, y = g_DY - LABEL_H, w = g_DW, h = DH + GAP + LABEL_H;
		if (g_open >= 0 || g_menuUp) { x = 0; y = g_top; w = g_sw; h = g_sh - g_top; }
		if (x == wx && y == wy && w == width && h == height) return;
		settleUntil = kapi_get_ticks () + 6;		// (onMouse: the moves sent for the old place)
		int stride = w;
		unsigned *fb = kapi_resize_window2 (w, h, &stride);
		if (fb == 0) return;
		canvas.adopt (fb, w, h, stride);
		width = w; height = h;
		kapi_move_window (x, y);
		wx = x; wy = y;
		invalidate (true);
	}

	void openDrawer (int d)
	{
		if (g_open == d) d = -1;				// (again: closed)
		g_open = d;
		if (d >= 0) drawer_box (d);
		tip = 0;
		placeWindow ();
		invalidate (true);
	}
	void closeDrawer () { if (g_open >= 0) openDrawer (-1); }

	// The screen's new size (kernel v66): along its bottom again, laid out for its width.
	void onDisplayResize (int w, int h) override
	{
		g_sw = w; g_sh = h;
		g_open = -1; tip = 0;
		::layout ();
		placeWindow ();
		invalidate (true);
	}

	void reload ()
	{
		uk_theme_reload ();
		closeDrawer ();
		scan_apps ();
		build ();
		layout ();
		poll_running ();
		poll_minis ();
		placeWindow ();
		invalidate (true);
	}

	// ---- drawing -----------------------------------------------------------------------------
	void panel (int x, int y, int w, int h, int r, unsigned top, unsigned bottom, unsigned edge)
	{
		uk_rbox (canvas, x, y, w, h, r, top, bottom);
		uk_rline (canvas, x, y, w, h, r, edge, 170);
		for (int i = x + r; i < x + w - r; i++)
		{
			unsigned *p = canvas.px + (long) (y + 1) * canvas.stride + i;
			if (i >= 0 && i < canvas.w && y + 1 >= 0 && y + 1 < canvas.h) *p = uk_over (*p, 0x00FFFFFF, 130);
		}
	}

	void groove (int x, int oy)
	{
		unsigned d = C_DOCK;
		for (int j = oy + 12; j < oy + DH - 12; j++)
		{
			canvas.pixel (x, j, uk_over (canvas.px[(long) j * canvas.stride + x], uk_tone (d, 76), 150));
			canvas.pixel (x + 1, j, uk_over (canvas.px[(long) j * canvas.stride + x + 1], 0x00FFFFFF, 110));
		}
	}

	void drawDesks (int ox, int oy)
	{
		unsigned d = C_DOCK, ink = uk_ink_on (d);
		int sx = ox + g_pgX;
		uk_rbox (canvas, sx, oy + 8, g_pgW, DH - 16, 10, uk_tone (d, 104), uk_tone (d, 122), 200);
		uk_rline (canvas, sx, oy + 8, g_pgW, DH - 16, 10, uk_tone (d, 76), 120);
		int info = kapi_desk (-1, 0), cur = KAPI_DESK_CUR (info);
		for (int i = 0; i < g_ndesk; i++)
		{
			int bx, by; desk_box (i, &bx, &by);
			bx += ox; by += oy;
			bool on = i == cur, h = hot.what == H_DESK && hot.index == i;
			unsigned wall = on ? uk_mix (C_ACCENT, 0x00FFFFFF, 60) : uk_tone (d, h ? 150 : 128);
			uk_rbox (canvas, bx, by, SQW, SQH, 4, uk_tone (wall, on ? 150 : 140), wall);
			// its windows, small (the one with the keys in the front frame's colour)
			bool any = false;
			for (int m = 0; m < g_nmini; m++)
			{
				const Mini &w = g_mini[m];
				if (w.desk != i && w.desk != -1) continue;
				if (w.desk == -1 && !on) continue;
				int x0 = bx + 2 + w.x * (SQW - 4) / g_sw, y0 = by + 2 + (w.y - g_top) * (SQH - 4) / (g_sh - g_top);
				int x1 = bx + 2 + (w.x + w.w) * (SQW - 4) / g_sw, y1 = by + 2 + (w.y - g_top + w.h) * (SQH - 4) / (g_sh - g_top);
				if (x0 < bx + 2) x0 = bx + 2;
				if (y0 < by + 2) y0 = by + 2;
				if (x1 > bx + SQW - 2) x1 = bx + SQW - 2;
				if (y1 > by + SQH - 2) y1 = by + SQH - 2;
				if (x1 - x0 < 3 || y1 - y0 < 3) continue;
				unsigned f = w.keys && on ? C_FRAME_ACTIVE : C_FRAME_INACTIVE;
				canvas.fillRect (x0, y0, x1 - x0, y1 - y0, uk_tone (f, 176));
				canvas.frameRect (x0, y0, x1 - x0, y1 - y0, uk_tone (f, 70));
				canvas.fillRect (x0 + 1, y0 + 1, x1 - x0 - 2, 2, uk_tone (f, 128));
				any = true;
			}
			if (!any)
			{
				char n[2] = { (char) ('1' + i), 0 };
				uk_text_c (canvas, bx, by, SQW, SQH, n, on ? uk_ink_on (wall) : uk_mix (wall, uk_ink_on (wall), 150), on ? 2 : 0);
			}
			uk_rline (canvas, bx, by, SQW, SQH, 4, on ? C_ACCENT : uk_tone (d, 70), on ? 255 : 170);
			if (on) uk_rline (canvas, bx - 1, by - 1, SQW + 2, SQH + 2, 5, C_ACCENT, 110);
		}
		// the left column: the lock, the gear (the Control Panel); the right one: the power, the clipboard
		bool hl = hot.what == H_LOCK, hg = hot.what == H_GEAR, hp = hot.what == H_POWER, hc = hot.what == H_CLIP;
		if (hl) uk_rbox (canvas, sx + 3, oy + 14, 24, 25, 6, 0x00FFFFFF, 0x00FFFFFF, 80);
		if (hg) uk_rbox (canvas, sx + 3, oy + 41, 24, 25, 6, 0x00FFFFFF, 0x00FFFFFF, 80);
		if (hp) uk_rbox (canvas, sx + g_pgW - 29, oy + 14, 24, 25, 6, 0x00FFFFFF, 0x00FFFFFF, 80);
		if (hc) uk_rbox (canvas, sx + g_pgW - 29, oy + 41, 24, 25, 6, 0x00FFFFFF, 0x00FFFFFF, 80);
		uk_glyph (canvas, WKG_LOCK, sx + 15, oy + 27, 15, ink);
		uk_glyph (canvas, WKG_GEAR, sx + 15, oy + 53, 17, ink);
		uk_glyph (canvas, WKG_POWER, sx + g_pgW - 17, oy + 27, 16, 0x00BE322C);
		clip_glyph (canvas, sx + g_pgW - 17, oy + 53, ink);
	}

	void drawDock (int ox, int oy)
	{
		unsigned d = C_DOCK, ink = uk_ink_on (d);
		panel (ox, oy, g_DW, DH, 14, uk_tone (d, 172), uk_tone (d, 120), uk_tone (d, 70));
		for (int i = 0; i < g_nslot; i++)
		{
			Slot &s = g_slot[i];
			int x = ox + s.x, cx = x + CW / 2;
			bool hIcon = hot.what == H_SLOT && hot.index == i, hStrip = hot.what == H_STRIP && hot.index == i;
			bool pr = hIcon && down.what == H_SLOT && down.index == i;
			bool drop = dropHot.what == H_SLOT && dropHot.index == i;
			if (hIcon || drop)
				uk_rbox (canvas, x + 4, oy + 19, CW - 8, 54, 8, pr ? uk_tone (d, 108) : uk_tone (d, 200),
					 pr ? uk_tone (d, 118) : uk_tone (d, 160), drop ? 255 : 150);
			if (drop) uk_rline (canvas, x + 4, oy + 19, CW - 8, 54, 8, C_ACCENT, 255);
			if (s.kind == SL_DRAWER)
			{
				const Drawer &k = g_dr[s.index];
				app_icon (canvas, cx, oy + 26, k.main);
				// the strip at the top edge: its drawer (open: in the accent)
				bool o = g_open == s.index;
				unsigned top = o ? uk_tone (C_ACCENT, 170) : uk_tone (d, hStrip ? 150 : 118);
				unsigned bot = o ? C_ACCENT : uk_tone (d, hStrip ? 132 : 106);
				uk_rbox (canvas, cx - 20, oy + 1, 40, STRIP_H - 1, 6, top, bot, 255, UK_BL | UK_BR);
				uk_rline (canvas, cx - 20, oy + 1, 40, STRIP_H - 1, 6, hStrip ? C_ACCENT : uk_tone (d, 70), hStrip ? 220 : 140, UK_BL | UK_BR);
				uk_glyph (canvas, o ? WKG_CHEV_DOWN : WKG_CHEV_UP, cx, oy + 1 + (STRIP_H - 1) / 2, 10, o ? 0x00FFFFFF : ink);
				if (k.running) uk_rbox (canvas, cx - 3, oy + DH - 9, 6, 5, 2, C_ACCENT, C_ACCENT);
			}
			else if (s.kind == SL_APP)
			{
				app_icon (canvas, cx, oy + 26, s.app);
				if (s.app && s.app->running) uk_rbox (canvas, cx - 3, oy + DH - 9, 6, 5, 2, C_ACCENT, C_ACCENT);
			}
			else trash_glyph (canvas, cx, oy + 24, trashFull, d);
			if (i + 1 < g_nslot && g_slot[i + 1].x == s.x + CW) groove (x + CW - 1, oy);	// between launchers
		}
		if (g_nleft) groove (ox + g_pgX - 5, oy);				// round the middle
		if (g_nslot > g_nleft) groove (ox + g_pgX + g_pgW + 3, oy);
		drawDesks (ox, oy);
	}

	// A drawer: a light panel, its pointer toward its launcher, its title.
	void drawerFrame (int x, int y, const char *title)
	{
		unsigned f = C_FIELD;
		panel (x, y, g_pw, g_ph, 12, uk_tone (f, 140), f, uk_tone (C_DOCK, 64));
		int cx = g_pcx - wx;					// the pointer, below it
		for (int j = 0; j < 8; j++)
			for (int i = -8 + j; i <= 8 - j; i++) canvas.pixel (cx + i, y + g_ph - 1 + j, f);
		for (int j = 0; j < 8; j++)
		{
			uk_blend_px (canvas, cx - 9 + j, y + g_ph - 1 + j, uk_tone (C_DOCK, 64), 170);
			uk_blend_px (canvas, cx + 9 - j, y + g_ph - 1 + j, uk_tone (C_DOCK, 64), 170);
		}
		uk_text_l (canvas, x + 14, y + 6, 26, title, C_FIELD_TEXT, 2);
		uk_etch_h (canvas, x + 10, y + 36, g_pw - 20, f);
	}

	void drawDrawer (int x, int y)
	{
		Drawer &k = g_dr[g_open];
		drawerFrame (x, y, k.cat);
		for (int i = 0; i < k.n; i++)
		{
			App &a = *k.apps[i];
			need_icon (a);
			int ix = x + 6 + (i / MAXROWS) * COLW, iy = y + 42 + (i % MAXROWS) * RH;
			bool h = hot.what == H_ITEM && hot.index == i;
			if (h) uk_hilite (canvas, ix, iy, COLW - 6, RH - 2, 7, true);
			blit_small (canvas, ix + 8, iy + 3, a.small, SI);
			unsigned ink = h ? C_SEL_TEXT : C_FIELD_TEXT;
			uk_text_l (canvas, ix + 46, iy, RH - 2, a.label, ink, &a == k.main ? 2 : 0);
			if (a.running) uk_rbox (canvas, ix + COLW - 20, iy + RH / 2 - 4, 6, 6, 3, h ? ink : C_ACCENT, h ? ink : C_ACCENT);
		}
		if (k.n == 0) uk_text_l (canvas, x + 14, y + 42, RH, "No apps in this group", uk_mix (C_FIELD, C_FIELD_TEXT, 140));
	}

	void drawTip (int ox, int oy)
	{
		int tw = uk_text_w (tip) + 20, th = uk_fh () + 8, cx = ox + g_DW / 2;
		if (hot.what == H_SLOT || hot.what == H_STRIP) cx = ox + g_slot[hot.index].x + CW / 2;
		else if (hot.what == H_DESK) { int bx, by; desk_box (hot.index, &bx, &by); cx = ox + bx + SQW / 2; }
		else if (hot.what == H_LOCK || hot.what == H_GEAR) cx = ox + g_pgX + 15;
		else if (hot.what == H_POWER || hot.what == H_CLIP) cx = ox + g_pgX + g_pgW - 17;
		int x = cx - tw / 2;
		if (x < 0) x = 0;
		if (x + tw > width) x = width - tw;
		int y = oy - LABEL_H + 2;
		uk_rbox (canvas, x, y, tw, th, 7, uk_tone (C_FIELD, 150), C_FIELD);
		uk_rline (canvas, x, y, tw, th, 7, uk_tone (C_DOCK, 64), 170);
		uk_text_c (canvas, x, y, tw, th, tip, C_FIELD_TEXT);
	}

	void onDraw () override
	{
		// see-through where nothing is; almost clear (catching a click) while a drawer is open
		canvas.clear (g_open >= 0 || g_menuUp ? 0xFE000000u : 0xFF000000u);
		uk_paint_alpha (true);
		int ox = g_DX - wx, oy = g_DY - wy;
		drawDock (ox, oy);
		if (g_open >= 0) drawDrawer (g_px - wx, g_py - wy);
		else if (tip) drawTip (ox, oy);
		uk_paint_alpha (false);
	}

	// ---- each frame ---------------------------------------------------------------------------
	void onTick () override
	{
		unsigned now = kapi_get_ticks ();
		if (now - lastPoll >= 50) { lastPoll = now; if (poll_running ()) invalidate (true); }
		if (now - lastMini >= 25) { lastMini = now; if (poll_minis ()) invalidate (true); }
		int info = kapi_desk (-1, 0) & 0xFFFF;
		if (info != lastDeskInfo) { lastDeskInfo = info; invalidate (true); }
		if (now - lastTrash >= 200)
		{
			lastTrash = now;
			bool full = trash_count () > 0;
			if (full != trashFull) { trashFull = full; invalidate (true); }
		}
		static char buf[64];
		int from = 0, type = 0;
		bool again = false;
		while (kapi_mailbox_recv (&from, &type, buf, sizeof buf, 0) >= 0)
			if (type == DOCK_MSG_RELOAD) again = true;
		if (again) reload ();
	}

	// ---- the pointer ------------------------------------------------------------------------------
	void setHot (const Hit &h)
	{
		if (h.what == hot.what && h.index == hot.index) return;
		hot = h;
		const char *t = g_open < 0 && !g_menuUp ? hit_tip (h) : 0;
		tip = t;
		invalidate (true);
	}

	void act (const Hit &h)
	{
		switch (h.what)
		{
		case H_SLOT:
		{
			Slot &s = g_slot[h.index];
			closeDrawer ();
			if (s.kind == SL_TRASH) { trash_ensure (); lx_launch ("fileviewer", "trash"); }
			else if (s.app) launch_or_raise (s.app->name);
			break;
		}
		case H_STRIP: openDrawer (g_slot[h.index].index); break;
		case H_DESK:  closeDrawer (); kapi_desk (h.index, 0); invalidate (true); break;
		case H_LOCK:  closeDrawer (); kapi_launch ("lock"); break;
		case H_GEAR:  closeDrawer (); launch_or_raise ("control"); break;
		case H_POWER: closeDrawer (); kapi_launch ("shutdown"); break;
		case H_CLIP:  closeDrawer (); launch_or_raise ("clipboard"); break;
		case H_ITEM:
		{
			App *a = g_dr[g_open].apps[h.index];
			closeDrawer ();
			launch_or_raise (a->name);
			break;
		}
		case H_NONE: closeDrawer (); break;			// elsewhere: the drawer closes
		}
	}

	// A right click on the dock: its settings (the Control Panel's Panel applet).
	void dockMenu (int sx)
	{
		closeDrawer ();
		g_menuUp = true; tip = 0;				// (the menu needs room: the window
		placeWindow ();						// covers the screen while it is shown)
		draw (); uk_present ();
		PopupMenu menu (sx - wx - 20, g_DY - 70 - wy);
		menu.add ("Panel Settings...", 1);
		menu.add ("Control Panel", 2);
		int r = menu.run ();
		g_menuUp = false;
		placeWindow ();
		if (r == 1) kapi_exec ("SD:apps/control.app/main", "dockconf");
		else if (r == 2) launch_or_raise ("control");
		invalidate (true);
	}

	bool onMouse (int mx, int my, int bl, int br, int, int) override
	{
		if (mx < 0)						// the pointer left (onto a see-through part)
		{
			Hit none = { H_NONE, -1 };
			if (!pressed) setHot (none);
			return false;
		}
		// (the window has just changed place -- a drawer opened or closed: the moves that were sent
		// for where it was are not read with the new origin)
		if ((int) (kapi_get_ticks () - settleUntil) < 0)
		{
			if (!bl && pressed) { pressed = false; down.what = H_NONE; invalidate (true); }
			return true;
		}
		int sx = mx + wx, sy = my + wy;
		Hit h = hit_test (sx, sy);
		setHot (h);
		if (br && !pressed && g_open < 0 && h.what != H_NONE && h.what != H_DRAWER && h.what != H_ITEM) { dockMenu (sx); return true; }
		if (bl && !pressed) { pressed = true; down = h; invalidate (true); return true; }
		if (!bl && pressed)
		{
			pressed = false;
			Hit d = down; down.what = H_NONE;
			if (d.what == h.what && d.index == h.index) act (h);
			invalidate (true);
		}
		return true;
	}

	// ---- drag and drop: files onto the Trash, or onto a launcher (its app opens them) --------------
	Hit dropTarget (int x, int y)
	{
		Hit h = hit_test (x + wx, y + wy);
		if (h.what == H_STRIP) h.what = H_SLOT;
		if (h.what != H_SLOT) h.what = H_NONE;
		return h;
	}

	void onDragOver (int x, int y, bool leave, unsigned) override
	{
		Hit h = { H_NONE, -1 };
		if (!leave) h = dropTarget (x, y);
		if (h.what != dropHot.what || h.index != dropHot.index) { dropHot = h; invalidate (true); }
	}

	void onDrop (int x, int y, int type, const char *data, int, unsigned) override
	{
		Hit h = dropTarget (x, y);
		dropHot.what = H_NONE;
		if (type != DND_FILES || h.what == H_NONE) { invalidate (true); return; }
		const Slot &s = g_slot[h.index];
		int done = 0;
		for (const char *p = data; *p; )
		{
			char path[200]; int n = 0;
			while (*p && *p != '\n') { if (n < (int) sizeof path - 1) path[n++] = *p; p++; }
			if (*p == '\n') p++;
			path[n] = '\0';
			if (n == 0) continue;
			if (s.kind == SL_TRASH) { if (trash_move (path)) done++; }
			else if (s.app && lx_launch (s.app->name, path)) done++;
		}
		if (s.kind == SL_TRASH && done) notify ("Trash", done == 1 ? "1 item moved to the Trash." : "Items moved to the Trash.");
		else if (s.kind == SL_TRASH) notify ("Trash", "Could not move it to the Trash.");
		trashFull = trash_count () > 0;
		invalidate (true);
	}
};

int main (void)
{
	kapi_screen_size (&g_sw, &g_sh);
	uikit::init ();
	kapi_ipc_register (DOCK_SERVICE);		// (DOCK_MSG_RELOAD: the Panel applet)
	scan_apps ();
	build ();
	layout ();
	// (made at the dock's own height, then grown by the name's room: the kernel's work area leaves
	// out a bottom window's smallest height -- the dock, not the room above it)
	DockRoot root (g_DX, g_DY, g_DW, DH + GAP);
	if (root.canvas.px == 0) return 1;
	root.placeWindow ();
	struct kapi_win_geom g;
	if (kapi_win_geometry (&g) == 0 && g.ay > 0) g_top = g.ay;
	root.trashFull = trash_count () > 0;
	poll_running ();
	poll_minis ();
	root.run ();
	return 0;
}
