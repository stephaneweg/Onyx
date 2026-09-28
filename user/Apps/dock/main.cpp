//
// dock -- the dock: the modernised CDE's Front Panel, along the bottom of the screen (it replaces
// the Shelf and the panel). From the left:
//   * the launchers of the apps' categories (Productivity, Internet, Graphics, Games -- System on
//     the right; app.txt): a click opens the category's DRAWER above it -- its apps and their
//     icons; a click starts one, or brings it back if it runs (minimised too). The drawer's tab
//     hangs from the dock's top edge. A dot under a launcher when one of its apps runs, and in
//     the drawer beside the app;
//   * the SWITCHER: the Shelf's tabs (Shelf, Documents, Apps...), each its own colour. A click
//     opens the tab's drawer: its items (files, folders, apps) -- a click opens one (in a new
//     instance of its app, SD:/etc/fileassoc.ini), a drag takes it out (onto a File Viewer
//     folder: moved there, Ctrl = copied; onto an app: the app opens it; onto the desktop: off
//     the shelf). Drop files on a tab or its drawer to add them. "+" adds a tab; a double click
//     renames one; a right click: Rename, Remove. Beside the tabs the lock (apps/lock), the gear
//     (the Settings: apps/config) and the power button (apps/shutdown);
//   * the Terminal, the File Viewer (a click starts it or brings it back) and the Trash (drop
//     files on it to move them there; a click opens it in the File Viewer).
// A click outside an open drawer closes it; the pointer on a launcher names it.
//
// The window: borderless, topmost (above the apps; never the keys'), a system one (not listed as
// an open app), see-through where there is no dock (WIN_FLAG_ALPHA: its rounded corners, the gap
// below it -- the clicks there go to what is below). A drawer (or a name) grows it upward; while
// a drawer is open it covers the screen, almost clear, to catch a click elsewhere. It stands on
// the screen's bottom edge, so the work area (maximised windows) ends above it.
//
// The Shelf's items stay in SD:/etc/shelf.ini ("tab = Name" then "item = path" lines); the File
// Viewer reports moves and renames over IPC (service "shelf", shelfmsg.h). Names are asked by
// apps/ask (the dock never has the keyboard).
//
#include "kapi.h"
#include "applib.h"
#include "bmp.hpp"
#include "fsutil.h"
#include "fileassoc.h"
#include "trash.h"
#include "notify.h"
#include "ask.h"
#include "shelfmsg.h"
#include "wtk/wtk.h"

using namespace wtk;

#define SHELF_INI	"SD:/etc/shelf.ini"
#define MAXTABS		8
#define MAXI		40
#define MAXAPPS		40

enum {
	DH = 80,		// the dock's height
	GAP = 12,		// below it (see-through)
	CW = 60,		// a launcher's width
	ICON = 40,		// the apps' icons (icon.bmp)
	SI = 30,		// ... in a category's drawer
	RH = 38,		// a row of a category's drawer
	COLW = 206,		// a column of it
	MAXROWS = 11,
	CELLW = 84, CELLH = 74,	// an item of a tab's drawer
	TABW = 86, TABH = 23,	// a tab of the switcher
	LABEL_H = 30,		// the name shown above a launcher
	DRAG_START = 6
};

// ---- the apps and their categories ---------------------------------------------------------------
struct App { char name[32]; char label[40]; unsigned *icon; int iw, ih; unsigned small[SI * SI]; bool running; };
struct Cat { const char *name; const char *iconApp; App *apps[MAXAPPS]; int n; unsigned *icon; int iw, ih; bool running; };

static App  g_apps[160];
static int  g_napps;
static Cat  g_cats[5] = {
	{ "Productivity", "tinypad", {}, 0, 0, 0, 0, false },
	{ "Internet",     "netsurf", {}, 0, 0, 0, 0, false },
	{ "Graphics",     "paint",   {}, 0, 0, 0, 0, false },
	{ "Games",        "tetris",  {}, 0, 0, 0, 0, false },
	{ "System",       "config",  {}, 0, 0, 0, 0, false },
};
enum { CAT_SYSTEM = 4, NCATS = 5 };

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

static void blit_icon (Canvas &cv, int x, int y, const unsigned *px, int w, int h)
{
	for (int j = 0; j < h; j++)
		for (int i = 0; i < w; i++)
		{
			unsigned c = px[j * w + i] & 0xFFFFFF;
			if (c != 0xFF00FF) cv.pixel (x + i, y + j, c);
		}
}

static void blit_small (Canvas &cv, int x, int y, const unsigned *px, int s)
{
	for (int j = 0; j < s; j++)
		for (int i = 0; i < s; i++)
		{
			unsigned c = px[j * s + i], t = c >> 24;
			if (t == 255) continue;
			int xx = x + i, yy = y + j;
			if (xx < 0 || yy < 0 || xx >= cv.w || yy >= cv.h) continue;
			unsigned *p = cv.px + (long) yy * cv.stride + xx;
			*p = t == 0 ? (c & 0xFFFFFF) : wk_over (*p, c & 0xFFFFFF, 255 - (int) t);
		}
}

static App *find_app (const char *name)
{
	for (int i = 0; i < g_napps; i++) if (fs_ci_cmp (g_apps[i].name, name) == 0) return &g_apps[i];
	return 0;
}

static void scan_apps (void)
{
	static char list[4096];
	int n = kapi_list_apps (list, sizeof list);
	(void) n;
	for (char *p = list; *p && g_napps < (int) (sizeof g_apps / sizeof g_apps[0]); )
	{
		char *e = p; while (*e && *e != '\n') e++;
		char c = *e; *e = '\0';
		App &a = g_apps[g_napps];
		fs_copy (a.name, p, sizeof a.name);
		fs_copy (a.label, p, sizeof a.label);
		a.icon = 0; a.iw = a.ih = 0; a.running = false;
		char path[160], cat[32] = "";
		fs_copy (path, "SD:/apps/", sizeof path);
		int k = fs_len (path);
		for (int i = 0; a.name[i] && k < 140; i++) path[k++] = a.name[i];
		path[k] = '\0';
		char q[180];						// SD:/apps/<name>.app/app.txt
		fs_copy (q, path, sizeof q); k = fs_len (q);
		const char *suffix = ".app/app.txt"; for (int i = 0; suffix[i] && k < 178; i++) q[k++] = suffix[i];
		q[k] = '\0';
		if (app_ini_load_path (q) >= 0)
		{
			const char *nm = app_ini_get (0, "name", 0);
			if (nm && nm[0]) fs_copy (a.label, nm, sizeof a.label);
			const char *ct = app_ini_get (0, "category", 0);
			if (ct) fs_copy (cat, ct, sizeof cat);
		}
		int ci = -1;
		for (int i = 0; i < NCATS; i++) if (fs_ci_cmp (cat, g_cats[i].name) == 0) ci = i;
		if (ci >= 0 && g_cats[ci].n < MAXAPPS)
		{
			const char *ic = ".app/icon.bmp"; fs_copy (q, path, sizeof q); k = fs_len (q);
			for (int i = 0; ic[i] && k < 178; i++) q[k++] = ic[i];
			q[k] = '\0';
			a.icon = ui::bmp_decode (q, &a.iw, &a.ih);
			if (a.icon && a.iw > 0 && a.ih > 0) scale_icon (a.icon, a.iw, a.ih, a.small, SI);
			else for (int i = 0; i < SI * SI; i++) a.small[i] = 0xFF000000u;
			g_cats[ci].apps[g_cats[ci].n++] = &a;
		}
		g_napps++;
		*e = c;
		p = *e ? e + 1 : e;
	}
	for (int c = 0; c < NCATS; c++)				// each drawer by name
		for (int i = 1; i < g_cats[c].n; i++)
			for (int j = i; j > 0 && fs_ci_cmp (g_cats[c].apps[j - 1]->label, g_cats[c].apps[j]->label) > 0; j--)
			{ App *t = g_cats[c].apps[j]; g_cats[c].apps[j] = g_cats[c].apps[j - 1]; g_cats[c].apps[j - 1] = t; }
	for (int c = 0; c < NCATS; c++)				// the launchers' icons
	{
		App *a = find_app (g_cats[c].iconApp);
		if (a == 0 || a->icon == 0) a = g_cats[c].n ? g_cats[c].apps[0] : 0;
		if (a && a->icon) { g_cats[c].icon = a->icon; g_cats[c].iw = a->iw; g_cats[c].ih = a->ih; }
	}
}

// The apps with a window (kapi_list_windows): their dots. -> changed?
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
	for (int c = 0; c < NCATS; c++)
	{
		bool r = false;
		for (int i = 0; i < g_cats[c].n; i++) if (g_cats[c].apps[i]->running) r = true;
		g_cats[c].running = r;
	}
	return changed;
}

static void launch_or_raise (const char *name)
{
	if (!kapi_raise_app (name)) kapi_launch (name);
}

// ---- the Shelf's tabs and items (from apps/shelf) ------------------------------------------------
enum { K_FILE, K_DIR, K_APP, K_PROG, K_IMAGE, K_TEXT };
struct Item { char path[200]; char label[40]; int kind; unsigned *icon; int iw, ih; };
struct Tab  { char name[24]; Item items[MAXI]; int n; };

static Tab g_tabs[MAXTABS];
static int g_ntabs = 0, g_cur = 0;

static bool ends_ci (const char *s, const char *ext)
{
	int n = fs_len (s), e = fs_len (ext);
	if (n < e) return false;
	for (int i = 0; i < e; i++) if (fs_lower (s[n - e + i]) != fs_lower (ext[i])) return false;
	return true;
}

// A path served by a file-system provider (FTP:...), not the card: never opened just to look.
static bool is_remote (const char *p) { return !(fs_lower (p[0]) == 's' && fs_lower (p[1]) == 'd' && p[2] == ':'); }
static bool remote_stat (const char *path, bool *isdir)
{
	char dir[256]; fs_copy (dir, path, sizeof dir);
	int n = fs_len (dir); while (n > 0 && dir[n - 1] == '/') dir[--n] = '\0';
	int k = n; while (k > 0 && dir[k - 1] != '/') k--;
	if (k == 0) { *isdir = true; return true; }
	const char *name = path + k;
	char nm[128]; int j = 0; while (name[j] && name[j] != '/' && j < 127) { nm[j] = name[j]; j++; } nm[j] = '\0';
	dir[k] = '\0';
	void *d = kapi_opendir (dir);
	if (!d) return false;
	struct kapi_dirent e; bool found = false;
	while (kapi_readdir (d, &e)) if (fs_ci_cmp (e.name, nm) == 0) { found = true; *isdir = e.is_dir != 0; break; }
	kapi_closedir (d);
	return found;
}

static void item_init (Item &it, const char *path, int isdir = -1)
{
	fs_copy (it.path, path, sizeof it.path);
	it.icon = 0; it.iw = it.ih = 0;
	const char *base = fs_basename (path);
	fs_copy (it.label, base, sizeof it.label);
	bool remote = is_remote (path), dir = false;
	if (isdir >= 0) dir = isdir != 0;
	else if (remote) { dir = true; for (int i = 0; base[i]; i++) if (base[i] == '.') dir = false; }
	else dir = fs_is_dir (path);
	if (dir)
	{
		it.kind = K_DIR;
		if (ends_ci (path, ".app"))
		{
			it.kind = K_APP;
			it.label[fs_len (it.label) - 4] = '\0';
			char p[260];
			fs_join (p, sizeof p, path, "app.txt");
			if (app_ini_load_path (p) >= 0)
			{
				const char *nm = app_ini_get (0, "name", 0);
				if (nm && nm[0]) fs_copy (it.label, nm, sizeof it.label);
			}
			fs_join (p, sizeof p, path, "icon.bmp");
			it.icon = ui::bmp_decode (p, &it.iw, &it.ih);
		}
		return;
	}
	char app[48];
	bool assoc = fa_app_for (path, app, sizeof app);
	if (assoc && fs_ci_cmp (app, "imageview") == 0) it.kind = K_IMAGE;
	else if (assoc && fs_ci_cmp (app, "tinypad") == 0) it.kind = K_TEXT;
	else if (!assoc && !remote && fa_is_program (path)) it.kind = K_PROG;
	else it.kind = K_FILE;
}

static void item_free (Item &it) { delete [] it.icon; it.icon = 0; }

static bool tab_has (const Tab &t, const char *path)
{
	for (int i = 0; i < t.n; i++) if (fs_ci_cmp (t.items[i].path, path) == 0) return true;
	return false;
}

static void tab_add (Tab &t, const char *path)
{
	int isdir = -1;
	char p[200]; fs_copy (p, path, sizeof p);
	int n = fs_len (p);
	if (n > 1 && p[n - 1] == '/' && is_remote (p)) { p[--n] = '\0'; isdir = 1; }
	if (t.n >= MAXI || tab_has (t, p)) return;
	item_init (t.items[t.n++], p, isdir);
}

static void tab_remove (Tab &t, int i)
{
	if (i < 0 || i >= t.n) return;
	item_free (t.items[i]);
	for (int j = i; j + 1 < t.n; j++) t.items[j] = t.items[j + 1];
	t.n--;
	t.items[t.n].icon = 0;
}

static void new_tab (const char *name)
{
	if (g_ntabs >= MAXTABS) return;
	Tab &t = g_tabs[g_ntabs++];
	fs_copy (t.name, name, sizeof t.name);
	t.n = 0;
}

static void save (void)
{
	static char buf[MAXTABS * (MAXI * 210 + 40) + 64];
	int p = 0;
	const char *hdr = "# Onyx Shelf (apps/dock: the switcher's tabs): tab = name, then item = path lines\n";
	for (int i = 0; hdr[i]; i++) buf[p++] = hdr[i];
	for (int t = 0; t < g_ntabs; t++)
	{
		const char *k = "tab = "; for (int i = 0; k[i]; i++) buf[p++] = k[i];
		for (int i = 0; g_tabs[t].name[i]; i++) buf[p++] = g_tabs[t].name[i];
		buf[p++] = '\n';
		for (int j = 0; j < g_tabs[t].n; j++)
		{
			const char *q = "item = "; for (int i = 0; q[i]; i++) buf[p++] = q[i];
			for (int i = 0; g_tabs[t].items[j].path[i]; i++) buf[p++] = g_tabs[t].items[j].path[i];
			if (g_tabs[t].items[j].kind == K_DIR && is_remote (g_tabs[t].items[j].path)) buf[p++] = '/';
			buf[p++] = '\n';
		}
	}
	kapi_save_file (SHELF_INI, buf, (unsigned) p);
}

static void load (void)
{
	void *f = kapi_open (SHELF_INI);
	if (f != 0)
	{
		static char buf[16384];
		int n = kapi_read (f, buf, sizeof buf - 1);
		kapi_close (f);
		if (n < 0) n = 0;
		buf[n] = '\0';
		n = fs_text_fix (buf, n);
		for (int i = 0; i < n; )
		{
			char line[256]; int k = 0;
			while (i < n && buf[i] != '\n') { if (buf[i] != '\r' && k < 255) line[k++] = buf[i]; i++; }
			i++;
			line[k] = '\0';
			int s = 0; while (line[s] == ' ' || line[s] == '\t') s++;
			if (line[s] == '#' || line[s] == '\0') continue;
			int eq = s; while (line[eq] && line[eq] != '=') eq++;
			if (!line[eq]) continue;
			int ke = eq; while (ke > s && line[ke - 1] == ' ') ke--;
			line[ke] = '\0';
			const char *v = line + eq + 1; while (*v == ' ' || *v == '\t') v++;
			if (fs_ci_cmp (line + s, "tab") == 0) new_tab (v);
			else if (fs_ci_cmp (line + s, "item") == 0 && g_ntabs > 0) tab_add (g_tabs[g_ntabs - 1], v);
		}
	}
	if (g_ntabs == 0) { new_tab ("Shelf"); new_tab ("Documents"); new_tab ("Apps"); }
}

static void forget_path (const char *path)
{
	int fl = fs_len (path);
	for (int t = 0; t < g_ntabs; t++)
		for (int i = g_tabs[t].n - 1; i >= 0; i--)
		{
			const char *ip = g_tabs[t].items[i].path;
			bool inside = true;
			for (int k = 0; k < fl; k++) if (fs_lower (ip[k]) != fs_lower (path[k])) { inside = false; break; }
			if (inside && (ip[fl] == '\0' || ip[fl] == '/')) tab_remove (g_tabs[t], i);
		}
}

static bool moved (const char *from, const char *to)
{
	bool changed = false;
	int fl = fs_len (from);
	for (int t = 0; t < g_ntabs; t++)
		for (int i = 0; i < g_tabs[t].n; i++)
		{
			Item &it = g_tabs[t].items[i];
			bool same = fs_ci_cmp (it.path, from) == 0, inside = true;
			for (int k = 0; k < fl; k++) if (fs_lower (it.path[k]) != fs_lower (from[k])) { inside = false; break; }
			inside = inside && it.path[fl] == '/';
			if (!same && !inside) continue;
			char np[200];
			fs_copy (np, to, sizeof np);
			if (inside)
			{
				int p = fs_len (np);
				for (int k = fl; it.path[k] && p < (int) sizeof np - 1; k++) np[p++] = it.path[k];
				np[p] = '\0';
			}
			int wasDir = (it.kind == K_DIR || it.kind == K_APP) ? 1 : 0;
			item_free (it);
			item_init (it, np, is_remote (np) ? wasDir : -1);
			changed = true;
		}
	return changed;
}

// "sunset-big.png" -> "sunse..png": the extension stays visible.
static void fit_label (char *s, int maxc)
{
	int n = fs_len (s);
	if (n <= maxc) return;
	int dot = -1; for (int i = n - 1; i > 0; i--) if (s[i] == '.') { dot = i; break; }
	int ext = dot > 0 ? n - dot : 0;
	if (ext > 0 && ext <= 5 && maxc - ext - 2 >= 2)
	{
		int keep = maxc - ext - 2;
		char tail[8]; for (int i = 0; i < ext; i++) tail[i] = s[dot + i];
		s[keep] = '.'; s[keep + 1] = '.';
		for (int i = 0; i < ext; i++) s[keep + 2 + i] = tail[i];
		s[keep + 2 + ext] = '\0';
	}
	else if (maxc >= 2) { s[maxc - 2] = '.'; s[maxc - 1] = '.'; s[maxc] = '\0'; }
}

// Checked only when an item is used (clicked, dragged): a missing one is announced and dropped.
static bool still_there (int tab, int i)
{
	Item &it = g_tabs[tab].items[i];
	bool d;
	if (is_remote (it.path) ? remote_stat (it.path, &d) : fs_exists (it.path)) return true;
	static char msg[160];
	int p = 0;
	for (int k = 0; it.label[k] && p < 100; k++) msg[p++] = it.label[k];
	const char *t = " no longer exists: removed from the shelf.";
	for (int k = 0; t[k] && p < (int) sizeof msg - 1; k++) msg[p++] = t[k];
	msg[p] = '\0';
	notify ("Shelf", msg);
	tab_remove (g_tabs[tab], i);
	save ();
	return false;
}

// ---- drawing: the icons of the items, the Trash ------------------------------------------------------
static void item_glyph (Canvas &cv, int x, int y, const Item &it)
{
	if (it.icon) { blit_icon (cv, x, y, it.icon, it.iw < ICON ? it.iw : ICON, it.ih < ICON ? it.ih : ICON); return; }
	switch (it.kind)
	{
	case K_DIR:					// a manila folder
		wk_rbox (cv, x + 3, y + 7, 16, 8, 2, 0x00D8AA52, 0x00C89A48);
		wk_rbox (cv, x + 3, y + 11, 34, 25, 3, 0x00EEC46C, 0x00D8A850);
		wk_rline (cv, x + 3, y + 11, 34, 25, 3, 0x00906A28, 200);
		break;
	case K_APP: case K_PROG:			// a window with a prompt
		wk_rbox (cv, x + 3, y + 6, 34, 28, 3, 0x00283038, 0x00101418);
		wk_rbox (cv, x + 3, y + 6, 34, 6, 3, 0x0080C8FF, 0x0060A8E0, 255, WK_TL | WK_TR);
		wk_rline (cv, x + 3, y + 6, 34, 28, 3, 0x0080C8FF, 220);
		cv.text (x + 7, y + 14, ">_", 0x0060FF90);
		break;
	case K_IMAGE:					// a landscape
		wk_rbox (cv, x + 4, y + 6, 32, 28, 3, 0x0090CCF8, 0x0070B8F0);
		cv.fillRect (x + 5, y + 24, 30, 9, 0x0050A050);
		wk_rbox (cv, x + 24, y + 10, 7, 7, 3, 0x00FFE890, 0x00FFD050);
		wk_rline (cv, x + 4, y + 6, 32, 28, 3, 0x00606870, 200);
		break;
	default:					// a page (lines if text)
		wk_rbox (cv, x + 8, y + 3, 24, 34, 2, 0x00FFFFFF, 0x00E8E8E8);
		wk_rline (cv, x + 8, y + 3, 24, 34, 2, 0x00808890, 220);
		if (it.kind == K_TEXT) for (int l = 0; l < 5; l++) cv.fillRect (x + 12, y + 9 + l * 5, 16, 2, 0x00909AA4);
		break;
	}
}

static void trash_glyph (Canvas &cv, int cx, int y, bool full, unsigned face)
{
	unsigned c = wk_tone (face, 64), hi = wk_tone (face, 188);
	if (full) wk_rbox (cv, cx - 8, y + 2, 16, 9, 2, 0x00FFFFFF, 0x00E4E4E4);	// paper sticking out
	wk_rbox (cv, cx - 14, y + 8, 28, 5, 2, c, c);				// the lid, its handle
	wk_rbox (cv, cx - 5, y + 4, 10, 5, 2, c, c);
	wk_rbox (cv, cx - 11, y + 15, 22, 28, 4, wk_tone (face, 76), c);		// the can
	for (int k = -1; k <= 1; k++) wk_rbox (cv, cx + k * 6 - 1, y + 19, 2, 20, 1, hi, wk_tone (face, 150));	// its ribs
}

// ---- the dock ---------------------------------------------------------------------------------------
static int g_sw = 1024, g_sh = 768, g_top = 30;			// the screen, the menu bar's height

enum { SL_CAT, SL_APP, SL_TRASH };
struct Slot { int kind, x, cat; const char *app, *tip; };
static Slot g_slot[10]; static int g_nslot;
static int  g_DX, g_DY, g_DW;					// the dock on the screen
static int  g_swX, g_swW, g_cols;				// the switcher (dock coordinates), its columns

static const unsigned SWITCH_COL[4] = { 0x007B8CA2, 0x0093ABBF, 0x004992A7, 0x00B7878D };

static void layout (void)
{
	int x = 12; g_nslot = 0;
	for (int c = 0; c < 4; c++) { g_slot[g_nslot++] = { SL_CAT, x, c, 0, g_cats[c].name }; x += CW; }
	int slots = g_ntabs + (g_ntabs < MAXTABS ? 1 : 0);	// the tabs and "+", two rows
	g_cols = (slots + 1) / 2; if (g_cols < 2) g_cols = 2;
	g_swX = x + 6; g_swW = 56 + g_cols * (TABW + 4);
	x = g_swX + g_swW + 6;
	g_slot[g_nslot++] = { SL_CAT, x, CAT_SYSTEM, 0, g_cats[CAT_SYSTEM].name }; x += CW;
	g_slot[g_nslot++] = { SL_APP, x, -1, "terminal", "Terminal" }; x += CW;
	g_slot[g_nslot++] = { SL_APP, x, -1, "fileviewer", "File Viewer" }; x += CW;
	g_slot[g_nslot++] = { SL_TRASH, x, -1, 0, "Trash" }; x += CW;
	g_DW = x + 12;
	g_DX = (g_sw - g_DW) / 2;
	g_DY = g_sh - DH - GAP;
}

// The switcher's slot i (tabs, then "+"): its box, dock coordinates.
static void tab_box (int i, int *x, int *y)
{
	*x = g_swX + 30 + (i % g_cols) * (TABW + 4);
	*y = 16 + (i / g_cols) * (TABH + 3);
}

// What the pointer is on (screen coordinates).
enum { H_NONE, H_SLOT, H_TAB, H_PLUS, H_LOCK, H_GEAR, H_POWER, H_DOCK, H_CATITEM, H_SHELFITEM, H_DRAWER };
struct Hit { int what, index; };

// The drawers: which is open (a category's, a tab's), its box (screen).
enum { D_NONE, D_CAT, D_TAB, D_MENU };	// (D_MENU: a pop-up menu is shown, no drawer)
static int g_drawer = D_NONE, g_dIndex = -1;
static int g_px, g_py, g_pw, g_ph, g_pcx;				// the open drawer's box, its pointer's x

static void cat_drawer_box (int c)
{
	Cat &k = g_cats[c];
	int n = k.n > 0 ? k.n : 1, cols = (n + MAXROWS - 1) / MAXROWS, rows = n < MAXROWS ? n : MAXROWS;
	g_pw = cols * COLW + 12; g_ph = 44 + rows * RH + 8;
	for (int i = 0; i < g_nslot; i++) if (g_slot[i].kind == SL_CAT && g_slot[i].cat == c) g_pcx = g_DX + g_slot[i].x + CW / 2;
	g_px = g_pcx - g_pw / 2;
	if (g_px < 4) g_px = 4;
	if (g_px + g_pw > g_sw - 4) g_px = g_sw - 4 - g_pw;
	g_py = g_DY - 12 - g_ph;
}

static int tab_cols (const Tab &t) { return t.n > 30 ? 7 : 5; }
static void tab_drawer_box (int t)
{
	int cols = tab_cols (g_tabs[t]), rows = (g_tabs[t].n + cols - 1) / cols;
	if (rows < 1) rows = 1;
	g_pw = cols * CELLW + 16; g_ph = 44 + rows * CELLH + 10;
	g_pcx = g_DX + g_swX + g_swW / 2;
	g_px = g_pcx - g_pw / 2;
	if (g_px < 4) g_px = 4;
	if (g_px + g_pw > g_sw - 4) g_px = g_sw - 4 - g_pw;
	g_py = g_DY - 12 - g_ph;
}

static Hit hit_test (int sx, int sy)
{
	Hit h = { H_NONE, -1 };
	if ((g_drawer == D_CAT || g_drawer == D_TAB) && sx >= g_px && sx < g_px + g_pw && sy >= g_py && sy < g_py + g_ph)
	{
		h.what = H_DRAWER;
		if (g_drawer == D_CAT)
		{
			Cat &k = g_cats[g_dIndex];
			int col = (sx - g_px - 6) / COLW, row = (sy - g_py - 42) / RH;
			int i = col * MAXROWS + row;
			if (sy >= g_py + 42 && row < MAXROWS && sx >= g_px + 6 && i >= 0 && i < k.n) { h.what = H_CATITEM; h.index = i; }
		}
		else
		{
			Tab &t = g_tabs[g_dIndex];
			int cols = tab_cols (t), col = (sx - g_px - 8) / CELLW, row = (sy - g_py - 42) / CELLH;
			int i = row * cols + col;
			if (sy >= g_py + 42 && sx >= g_px + 8 && col < cols && i >= 0 && i < t.n) { h.what = H_SHELFITEM; h.index = i; }
		}
		return h;
	}
	int x = sx - g_DX, y = sy - g_DY;
	if (x < 0 || y < 0 || x >= g_DW || y >= DH) return h;
	h.what = H_DOCK;
	for (int i = 0; i < g_nslot; i++)
		if (x >= g_slot[i].x && x < g_slot[i].x + CW) { h.what = H_SLOT; h.index = i; return h; }
	int slots = g_ntabs + (g_ntabs < MAXTABS ? 1 : 0);
	for (int i = 0; i < slots; i++)
	{
		int bx, by; tab_box (i, &bx, &by);
		if (x >= bx && x < bx + TABW && y >= by && y < by + TABH) { h.what = i < g_ntabs ? H_TAB : H_PLUS; h.index = i; return h; }
	}
	if (x >= g_swX && x < g_swX + 28) { h.what = y < 40 ? H_LOCK : H_GEAR; return h; }
	if (x >= g_swX + g_swW - 30 && x < g_swX + g_swW && y >= 36) { h.what = H_POWER; return h; }
	return h;
}

static const char *hit_tip (const Hit &h)
{
	switch (h.what)
	{
	case H_SLOT:  return g_slot[h.index].tip;
	case H_LOCK:  return "Lock the screen";
	case H_GEAR:  return "Settings";
	case H_POWER: return "Shut down";
	case H_PLUS:  return "New tab";
	}
	return 0;
}

class DockRoot : public Root
{
public:
	int wx, wy;				// the window on the screen
	Hit hot = { H_NONE, -1 }, down = { H_NONE, -1 };
	int downX = 0, downY = 0;		// the press (screen)
	bool dragging = false; int dragItem = -1, dragTab = -1;
	Hit dropHot = { H_NONE, -1 };		// what a drag hovers
	bool trashFull = false;
	unsigned lastPoll = 0, lastTrash = 0, lastTabClick = 0; int lastTab = -1;
	int askKind = 0, askTab = -1;		// a name asked: 1 a new tab, 2 a tab renamed
	const char *tip = 0;			// the name shown above the dock (the pointer's launcher)

	DockRoot (int x, int y, int w, int h)
	  : Root (x, y, w, h, "dock", WIN_FLAG_BORDERLESS | WIN_FLAG_TOPMOST | WIN_FLAG_SYSTEM | WIN_FLAG_ALPHA),
	    wx (x), wy (y) {}

	// The window for what is shown: the dock (+ the name above it), or the screen (a drawer).
	void placeWindow ()
	{
		int x = g_DX, y = g_DY, w = g_DW, h = DH + GAP;
		if (g_drawer != D_NONE) { x = 0; y = g_top; w = g_sw; h = g_sh - g_top; }
		else if (tip) { y -= LABEL_H; h += LABEL_H; }
		if (x == wx && y == wy && w == width && h == height) return;
		int stride = w;
		unsigned *fb = kapi_resize_window2 (w, h, &stride);
		if (fb == 0) return;
		canvas.adopt (fb, w, h, stride);
		width = w; height = h;
		kapi_move_window (x, y);
		wx = x; wy = y;
		invalidate (true);
	}

	void openDrawer (int kind, int index)
	{
		if (g_drawer == kind && g_dIndex == index) kind = D_NONE;	// (again: closed)
		g_drawer = kind; g_dIndex = kind == D_NONE ? -1 : index;
		if (kind == D_CAT) cat_drawer_box (index);
		else if (kind == D_TAB) tab_drawer_box (index);
		tip = 0;
		placeWindow ();
		invalidate (true);
	}
	void closeDrawer () { if (g_drawer != D_NONE) openDrawer (D_NONE, -1); }

	// ---- drawing -----------------------------------------------------------------------------
	void panel (int x, int y, int w, int h, int r, unsigned top, unsigned bottom, unsigned edge)
	{
		wk_rbox (canvas, x, y, w, h, r, top, bottom);
		wk_rline (canvas, x, y, w, h, r, edge, 170);
		for (int i = x + r; i < x + w - r; i++)
		{
			unsigned *p = canvas.px + (long) (y + 1) * canvas.stride + i;
			if (i >= 0 && i < canvas.w && y + 1 >= 0 && y + 1 < canvas.h) *p = wk_over (*p, 0x00FFFFFF, 130);
		}
	}

	void drawDock (int ox, int oy)
	{
		unsigned d = C_DOCK, ink = wk_ink_on (d);
		panel (ox, oy, g_DW, DH, 14, wk_tone (d, 172), wk_tone (d, 120), wk_tone (d, 70));
		for (int i = 0; i < g_nslot; i++)
		{
			Slot &s = g_slot[i];
			int x = ox + s.x, cx = x + CW / 2;
			bool h = hot.what == H_SLOT && hot.index == i, pr = h && down.what == H_SLOT && down.index == i;
			bool drop = s.kind == SL_TRASH && dropHot.what == H_SLOT && dropHot.index == i;
			if (h || drop)
				wk_rbox (canvas, x + 4, oy + 17, CW - 8, 54, 8, pr ? wk_tone (d, 108) : wk_tone (d, 200),
					 pr ? wk_tone (d, 118) : wk_tone (d, 160), drop ? 255 : 150);
			if (drop) wk_rline (canvas, x + 4, oy + 17, CW - 8, 54, 8, C_ACCENT, 255);
			if (s.kind == SL_CAT)
			{
				Cat &k = g_cats[s.cat];
				if (k.icon) blit_icon (canvas, cx - ICON / 2, oy + 24, k.icon, k.iw < ICON ? k.iw : ICON, k.ih < ICON ? k.ih : ICON);
				bool o = g_drawer == D_CAT && g_dIndex == s.cat;	// the drawer's tab, at the top edge
				if (o) wk_rbox (canvas, cx - 16, oy + 1, 32, 13, 6, wk_tone (C_ACCENT, 170), C_ACCENT, 255, WK_BL | WK_BR);
				else wk_rbox (canvas, cx - 16, oy + 1, 32, 13, 6, wk_tone (d, 118), wk_tone (d, 106), 255, WK_BL | WK_BR);
				wk_rline (canvas, cx - 16, oy + 1, 32, 13, 6, wk_tone (d, 70), 140, WK_BL | WK_BR);
				wk_glyph (canvas, o ? WKG_DOWN : WKG_UP, cx, oy + 7, 10, o ? 0x00FFFFFF : ink);
				if (k.running) wk_rbox (canvas, cx - 3, oy + DH - 9, 6, 5, 2, C_ACCENT, C_ACCENT);
			}
			else if (s.kind == SL_APP)
			{
				App *a = find_app (s.app);
				if (a && a->icon) blit_icon (canvas, cx - ICON / 2, oy + 24, a->icon, a->iw < ICON ? a->iw : ICON, a->ih < ICON ? a->ih : ICON);
				if (a && a->running) wk_rbox (canvas, cx - 3, oy + DH - 9, 6, 5, 2, C_ACCENT, C_ACCENT);
			}
			else trash_glyph (canvas, cx, oy + 22, trashFull, d);
			if (i + 1 < g_nslot && g_slot[i + 1].x == s.x + CW)		// a groove between launchers
				for (int j = oy + 12; j < oy + DH - 12; j++)
				{
					canvas.pixel (x + CW - 1, j, wk_over (canvas.px[(long) j * canvas.stride + x + CW - 1], wk_tone (d, 76), 150));
					canvas.pixel (x + CW, j, wk_over (canvas.px[(long) j * canvas.stride + x + CW], 0x00FFFFFF, 110));
				}
		}
		// the grooves round the switcher
		int gx[2] = { ox + g_swX - 7, ox + g_swX + g_swW + 5 };
		for (int k = 0; k < 2; k++)
			for (int j = oy + 12; j < oy + DH - 12; j++)
			{
				canvas.pixel (gx[k], j, wk_over (canvas.px[(long) j * canvas.stride + gx[k]], wk_tone (d, 76), 150));
				canvas.pixel (gx[k] + 1, j, wk_over (canvas.px[(long) j * canvas.stride + gx[k] + 1], 0x00FFFFFF, 110));
			}
		// the switcher: a recessed panel, the tabs each its colour, the lock, the gear, the power
		int sx = ox + g_swX;
		wk_rbox (canvas, sx, oy + 8, g_swW, DH - 16, 10, wk_tone (d, 104), wk_tone (d, 122), 200);
		wk_rline (canvas, sx, oy + 8, g_swW, DH - 16, 10, wk_tone (d, 76), 120);
		int slots = g_ntabs + (g_ntabs < MAXTABS ? 1 : 0);
		for (int i = 0; i < slots; i++)
		{
			int bx, by; tab_box (i, &bx, &by);
			bx += ox; by += oy;
			bool plus = i >= g_ntabs, cur = !plus && i == g_cur;
			bool open = !plus && g_drawer == D_TAB && g_dIndex == i;
			bool h = (hot.what == H_TAB || hot.what == H_PLUS) && hot.index == i;
			bool drop = dropHot.what == H_TAB && dropHot.index == i;
			unsigned col = plus ? wk_tone (d, 150) : SWITCH_COL[i % 4];
			if (cur || open) wk_rbox (canvas, bx, by, TABW, TABH, 7, wk_tone (col, 102), col);
			else wk_rbox (canvas, bx, by, TABW, TABH, 7, wk_tone (col, h ? 186 : 166), wk_tone (col, h ? 128 : 116));
			wk_rline (canvas, bx, by, TABW, TABH, 7, drop || open ? C_ACCENT : wk_tone (col, 64), drop || open ? 255 : 200);
			if (cur || open) for (int j = 1; j < 5; j++) for (int q = bx + 3; q < bx + TABW - 3; q++)
				canvas.pixel (q, by + j, wk_over (canvas.px[(long) (by + j) * canvas.stride + q], 0, 50 - j * 10));
			if (plus) wk_glyph (canvas, WKG_PLUS, bx + TABW / 2, by + TABH / 2, 9, wk_ink_on (col));
			else
			{
				char nm[24]; fs_copy (nm, g_tabs[i].name, sizeof nm);
				int maxc = (TABW - 8) / wk_text_w ("M", cur ? 2 : 0);
				if (fs_len (nm) > maxc && maxc > 2) { nm[maxc - 2] = '.'; nm[maxc - 1] = '.'; nm[maxc] = '\0'; }
				wk_text_c (canvas, bx, by + (cur ? 1 : 0), TABW, TABH, nm, wk_ink_on (col), cur ? 2 : 0);
			}
		}
		bool hl = hot.what == H_LOCK, hg = hot.what == H_GEAR, hp = hot.what == H_POWER;
		if (hl) wk_rbox (canvas, sx + 3, oy + 14, 24, 25, 6, 0x00FFFFFF, 0x00FFFFFF, 80);
		if (hg) wk_rbox (canvas, sx + 3, oy + 41, 24, 25, 6, 0x00FFFFFF, 0x00FFFFFF, 80);
		if (hp) wk_rbox (canvas, sx + g_swW - 29, oy + 40, 24, 25, 6, 0x00FFFFFF, 0x00FFFFFF, 80);
		wk_glyph (canvas, WKG_LOCK, sx + 15, oy + 27, 15, ink);
		wk_glyph (canvas, WKG_GEAR, sx + 15, oy + 53, 17, ink);
		wk_glyph (canvas, WKG_POWER, sx + g_swW - 17, oy + 53, 16, 0x00BE322C);
	}

	// A drawer: a light panel, its pointer toward its launcher, its title.
	void drawerFrame (int x, int y, const char *title, const char *sub)
	{
		unsigned f = C_FIELD;
		panel (x, y, g_pw, g_ph, 12, wk_tone (f, 140), f, wk_tone (C_DOCK, 64));
		int cx = g_pcx - wx;					// the pointer, below it
		for (int j = 0; j < 8; j++)
			for (int i = -8 + j; i <= 8 - j; i++) canvas.pixel (cx + i, y + g_ph - 1 + j, f);
		for (int j = 0; j < 8; j++)
		{
			wk_blend_px (canvas, cx - 9 + j, y + g_ph - 1 + j, wk_tone (C_DOCK, 64), 170);
			wk_blend_px (canvas, cx + 9 - j, y + g_ph - 1 + j, wk_tone (C_DOCK, 64), 170);
		}
		wk_text_l (canvas, x + 14, y + 6, 26, title, C_FIELD_TEXT, 2);
		if (sub) canvas.text (x + g_pw - 14 - wk_text_w (sub), y + 6 + (26 - wk_fh ()) / 2, sub, wk_mix (f, C_FIELD_TEXT, 140));
		wk_etch_h (canvas, x + 10, y + 36, g_pw - 20, f);
	}

	void drawCatDrawer (int x, int y)
	{
		Cat &k = g_cats[g_dIndex];
		drawerFrame (x, y, k.name, 0);
		for (int i = 0; i < k.n; i++)
		{
			App &a = *k.apps[i];
			int ix = x + 6 + (i / MAXROWS) * COLW, iy = y + 42 + (i % MAXROWS) * RH;
			bool h = hot.what == H_CATITEM && hot.index == i;
			if (h) wk_hilite (canvas, ix, iy, COLW - 6, RH - 2, 7, true);
			blit_small (canvas, ix + 8, iy + 3, a.small, SI);
			unsigned ink = h ? C_SEL_TEXT : C_FIELD_TEXT;
			wk_text_l (canvas, ix + 46, iy, RH - 2, a.label, ink);
			if (a.running) wk_rbox (canvas, ix + COLW - 20, iy + RH / 2 - 4, 6, 6, 3, h ? ink : C_ACCENT, h ? ink : C_ACCENT);
		}
		if (k.n == 0) wk_text_l (canvas, x + 14, y + 42, RH, "No apps", wk_mix (C_FIELD, C_FIELD_TEXT, 140));
	}

	void drawTabDrawer (int x, int y)
	{
		Tab &t = g_tabs[g_dIndex];
		char sub[16]; int n = t.n, p = 0;
		if (n >= 10) sub[p++] = (char) ('0' + n / 10);
		sub[p++] = (char) ('0' + n % 10);
		const char *w = n == 1 ? " item" : " items"; for (int i = 0; w[i]; i++) sub[p++] = w[i];
		sub[p] = '\0';
		drawerFrame (x, y, t.name, sub);
		int cols = tab_cols (t), maxc = (CELLW - 6) / wk_fw ();
		for (int i = 0; i < t.n; i++)
		{
			int cx = x + 8 + (i % cols) * CELLW, cy = y + 42 + (i / cols) * CELLH;
			bool h = hot.what == H_SHELFITEM && hot.index == i && !dragging;
			if (h) wk_hilite (canvas, cx + 1, cy, CELLW - 2, CELLH - 2, 8, true);
			item_glyph (canvas, cx + (CELLW - ICON) / 2, cy + 4, t.items[i]);
			char lab[40]; fs_copy (lab, t.items[i].label, sizeof lab);
			fit_label (lab, maxc);
			wk_text_c (canvas, cx, cy + 46, CELLW, 22, lab, h ? C_SEL_TEXT : C_FIELD_TEXT);
		}
		if (t.n == 0) wk_text_c (canvas, x, y + 42, g_pw, CELLH, "Drop files, folders or apps here", wk_mix (C_FIELD, C_FIELD_TEXT, 140));
		if (dropHot.what == H_DRAWER) wk_rline (canvas, x + 2, y + 2, g_pw - 4, g_ph - 4, 10, C_ACCENT, 255);
	}

	void drawTip (int ox, int oy)
	{
		int tw = wk_text_w (tip) + 20, th = wk_fh () + 8, cx = -1;
		for (int i = 0; i < g_nslot; i++) if (hot.what == H_SLOT && hot.index == i) cx = ox + g_slot[i].x + CW / 2;
		if (cx < 0) cx = ox + g_swX + (hot.what == H_POWER ? g_swW - 17 : hot.what == H_PLUS ? g_swW / 2 : 15);
		int x = cx - tw / 2;
		if (x < 0) x = 0;
		if (x + tw > width) x = width - tw;
		int y = oy - LABEL_H + 2;
		wk_rbox (canvas, x, y, tw, th, 7, wk_tone (C_FIELD, 150), C_FIELD);
		wk_rline (canvas, x, y, tw, th, 7, wk_tone (C_DOCK, 64), 170);
		wk_text_c (canvas, x, y, tw, th, tip, C_FIELD_TEXT);
	}

	void onDraw () override
	{
		// see-through where nothing is; almost clear (catching a click) while a drawer is open
		canvas.clear (g_drawer != D_NONE ? 0xFE000000u : 0xFF000000u);
		wk_paint_alpha (true);
		int ox = g_DX - wx, oy = g_DY - wy;
		drawDock (ox, oy);
		if (g_drawer == D_CAT) drawCatDrawer (g_px - wx, g_py - wy);
		else if (g_drawer == D_TAB) drawTabDrawer (g_px - wx, g_py - wy);
		else if (tip) drawTip (ox, oy);
		wk_paint_alpha (false);
	}

	// ---- the tabs -----------------------------------------------------------------------------
	void removeTab (int t)
	{
		if (t < 0 || t >= g_ntabs || g_ntabs <= 1) return;
		while (g_tabs[t].n > 0) tab_remove (g_tabs[t], g_tabs[t].n - 1);
		for (int j = t; j + 1 < g_ntabs; j++) g_tabs[j] = g_tabs[j + 1];
		g_ntabs--;
		g_tabs[g_ntabs].n = 0;
		if (g_cur >= g_ntabs) g_cur = g_ntabs - 1;
		save ();
		relayout ();
	}
	void relayout ()
	{
		closeDrawer ();
		layout ();
		placeWindow ();
		invalidate (true);
	}
	void askName (int kind, int t)
	{
		if (askKind) return;
		char def[24];
		if (kind == 1)
		{
			fs_copy (def, "Tab ", sizeof def);
			int n = 4, v = g_ntabs + 1;
			if (v >= 10) def[n++] = (char) ('0' + v / 10);
			def[n++] = (char) ('0' + v % 10); def[n] = '\0';
		}
		else fs_copy (def, g_tabs[t].name, sizeof def);
		if (ask_text_begin (kind == 1 ? "New tab" : "Rename tab", "Name:", kind == 1 ? "Create" : "Rename", "Cancel", def))
		{ askKind = kind; askTab = t; }
		else notify ("Dock", "Cannot ask for the name (apps/ask missing?).");
	}
	void tabMenu (int t)
	{
		closeDrawer ();
		g_drawer = D_MENU; g_dIndex = -1; tip = 0;	// (the menu needs room: the window covers
		placeWindow ();					// the screen while it is shown)
		draw (); kapi_present ();
		int bx, by; tab_box (t, &bx, &by);
		PopupMenu menu (g_DX + bx - wx, g_DY - 64 - wy);
		menu.add ("Rename...", 1);
		menu.add ("Remove tab", 2, g_ntabs > 1);
		int r = menu.run ();
		g_drawer = D_NONE;
		placeWindow ();
		if (r == 1) askName (2, t);
		else if (r == 2)
		{
			if (g_tabs[t].n == 0) removeTab (t);
			else if (removeAsk == 0)
			{
				static char msg[120]; int p = 0;
				const char *a = "Remove tab \""; for (int i = 0; a[i]; i++) msg[p++] = a[i];
				for (int i = 0; g_tabs[t].name[i] && p < 80; i++) msg[p++] = g_tabs[t].name[i];
				const char *b = "\" and its items? Files are kept."; for (int i = 0; b[i]; i++) msg[p++] = b[i];
				msg[p] = '\0';
				removeAsk = ask_begin ("Remove tab", msg, "Remove", "Cancel");
				removeTabIx = t;
			}
		}
		invalidate (true);
	}
	void *removeAsk = 0; int removeTabIx = -1;

	// ---- each frame ---------------------------------------------------------------------------
	void onTick () override
	{
		unsigned now = kapi_get_ticks ();
		if (now - lastPoll >= 50) { lastPoll = now; if (poll_running ()) invalidate (true); }
		if (now - lastTrash >= 200)
		{
			lastTrash = now;
			bool full = trash_count () > 0;
			if (full != trashFull) { trashFull = full; invalidate (true); }
		}
		if (askKind)
		{
			char name[64];
			int r = ask_text_poll (name, sizeof name);
			if (r >= 0)
			{
				if (r == 1 && name[0])
				{
					for (int i = 0; name[i]; i++) if (name[i] == '\n' || name[i] == '\r') name[i] = '\0';
					if (askKind == 1) { new_tab (name); g_cur = g_ntabs - 1; }
					else if (askTab >= 0 && askTab < g_ntabs) fs_copy (g_tabs[askTab].name, name, sizeof g_tabs[askTab].name);
					save ();
					relayout ();
				}
				askKind = 0; askTab = -1;
			}
		}
		if (removeAsk)
		{
			int r = ask_poll (removeAsk);
			if (r >= 0) { removeAsk = 0; if (r == 1) removeTab (removeTabIx); removeTabIx = -1; }
		}
		bool changed = false;					// the File Viewer's move reports
		static char buf[520];
		int from = 0, type = 0, n;
		while ((n = kapi_mailbox_recv (&from, &type, buf, sizeof buf - 1, 0)) >= 0)
		{
			if (type != SHELF_MSG_MOVED) continue;
			buf[n] = '\0';
			const char *to = buf; while (*to) to++;
			to++;
			if (to < buf + n && moved (buf, to)) changed = true;
		}
		if (changed) { save (); invalidate (true); }
	}

	// ---- the pointer ------------------------------------------------------------------------------
	void setHot (const Hit &h)
	{
		if (h.what == hot.what && h.index == hot.index) return;
		hot = h;
		const char *t = g_drawer == D_NONE ? hit_tip (h) : 0;
		if (t != tip) { tip = t; placeWindow (); }
		invalidate (true);
	}

	void act (const Hit &h, int sx, int sy)
	{
		switch (h.what)
		{
		case H_SLOT:
		{
			Slot &s = g_slot[h.index];
			if (s.kind == SL_CAT) openDrawer (D_CAT, s.cat);
			else if (s.kind == SL_APP) { closeDrawer (); launch_or_raise (s.app); }
			else { closeDrawer (); fa_open (TRASH_FILES); }
			break;
		}
		case H_TAB:
		{
			unsigned now = kapi_get_ticks ();
			if (h.index == lastTab && now - lastTabClick < 40) { closeDrawer (); askName (2, h.index); lastTab = -1; break; }
			lastTab = h.index; lastTabClick = now;
			g_cur = h.index;
			openDrawer (D_TAB, h.index);
			break;
		}
		case H_PLUS:  closeDrawer (); askName (1, -1); break;
		case H_LOCK:  closeDrawer (); kapi_launch ("lock"); break;
		case H_GEAR:  closeDrawer (); launch_or_raise ("config"); break;
		case H_POWER: closeDrawer (); kapi_launch ("shutdown"); break;
		case H_CATITEM:
		{
			App *a = g_cats[g_dIndex].apps[h.index];
			closeDrawer ();
			launch_or_raise (a->name);
			break;
		}
		case H_SHELFITEM:
		{
			int t = g_dIndex;
			if (!still_there (t, h.index)) { tab_drawer_box (t); placeWindow (); invalidate (true); break; }
			if (!fa_open (g_tabs[t].items[h.index].path)) notify ("Shelf", "No application to open this item.");
			closeDrawer ();
			break;
		}
		case H_NONE: closeDrawer (); break;			// elsewhere: the drawer closes
		}
		(void) sx; (void) sy;
	}

	bool onMouse (int mx, int my, int bl, int br, int, int) override
	{
		if (mx < 0)						// the pointer left (onto a see-through part)
		{
			Hit none = { H_NONE, -1 };
			if (!pressed) setHot (none);
			return false;
		}
		int sx = mx + wx, sy = my + wy;
		Hit h = hit_test (sx, sy);
		if (!dragging) setHot (h);
		if (br && !pressed && h.what == H_TAB) { tabMenu (h.index); return true; }
		if (bl && !pressed) { pressed = true; down = h; downX = sx; downY = sy; invalidate (true); return true; }
		if (bl && pressed)
		{
			// a shelf item dragged out of its drawer
			int dx = sx - downX, dy = sy - downY;
			if (down.what == H_SHELFITEM && !dragging && dx * dx + dy * dy > DRAG_START * DRAG_START)
			{
				int t = g_dIndex;
				if (!still_there (t, down.index)) { down.what = H_NONE; tab_drawer_box (t); placeWindow (); invalidate (true); return true; }
				const Item &it = g_tabs[t].items[down.index];
				if (kapi_drag_begin (DND_FILES, it.path, (unsigned) fs_len (it.path) + 1, it.label))
				{ dragging = true; dragItem = down.index; dragTab = t; }
				down.what = H_NONE;
				invalidate (true);
			}
			return true;
		}
		if (!bl && pressed)
		{
			pressed = false;
			Hit d = down; down.what = H_NONE;
			if (!dragging && d.what == h.what && d.index == h.index) act (h, sx, sy);
			invalidate (true);
		}
		return true;
	}

	// ---- drag and drop ----------------------------------------------------------------------------
	Hit dropTarget (int x, int y)
	{
		Hit h = hit_test (x + wx, y + wy);
		if (h.what == H_SHELFITEM) h.what = H_DRAWER;
		if (h.what == H_DRAWER && g_drawer != D_TAB) h.what = H_NONE;
		if (h.what == H_SLOT && g_slot[h.index].kind != SL_TRASH) h.what = H_NONE;
		if (h.what != H_TAB && h.what != H_DRAWER && h.what != H_SLOT) h.what = H_NONE;
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
		bool toTrash = h.what == H_SLOT;
		Tab &dst = h.what == H_TAB ? g_tabs[h.index] : g_tabs[g_drawer == D_TAB ? g_dIndex : g_cur];
		int trashed = 0;
		for (const char *p = data; *p; )
		{
			char path[200]; int n = 0;
			while (*p && *p != '\n') { if (n < (int) sizeof path - 1) path[n++] = *p; p++; }
			if (*p == '\n') p++;
			path[n] = '\0';
			if (n == 0) continue;
			if (toTrash) { if (trash_move (path)) { trashed++; forget_path (path); } }
			else tab_add (dst, path);
		}
		if (toTrash && trashed) notify ("Trash", trashed == 1 ? "1 item moved to the Trash." : "Items moved to the Trash.");
		save ();
		trashFull = trash_count () > 0;
		if (g_drawer == D_TAB) { tab_drawer_box (g_dIndex); placeWindow (); }
		invalidate (true);
	}

	void onDragDone (int, unsigned flags) override
	{
		dragging = false;
		// onto the desktop: off the shelf (moved elsewhere: the File Viewer reports it)
		if ((flags & DND_F_DESKTOP) && !(flags & DND_F_CANCEL) && dragTab >= 0 && dragTab < g_ntabs)
			tab_remove (g_tabs[dragTab], dragItem);
		save ();
		trashFull = trash_count () > 0;
		dragItem = dragTab = -1;
		if (g_drawer == D_TAB) { tab_drawer_box (g_dIndex); placeWindow (); }
		invalidate (true);
	}
};

int main (void)
{
	kapi_screen_size (&g_sw, &g_sh);
	wtk::init ();
	scan_apps ();
	kapi_ipc_register (SHELF_SERVICE);		// the File Viewer's move reports
	load ();
	layout ();
	DockRoot root (g_DX, g_DY, g_DW, DH + GAP);
	if (root.canvas.px == 0) return 1;
	struct kapi_win_geom g;
	if (kapi_win_geometry (&g) == 0 && g.ay > 0) g_top = g.ay;
	root.trashFull = trash_count () > 0;
	poll_running ();
	root.run ();
	return 0;
}
