//
// shelf -- the Shelf: a strip along the bottom of the screen that keeps references to
// files, folders and apps, organised in tabs (NeXTSTEP-style shelf).
//
//   * Drop files (e.g. from the File Viewer) on it: they are added to the current tab
//     (or to the tab you drop them on). The files themselves stay where they are.
//   * Click an item: open it in a NEW instance of its app (SD:/etc/fileassoc.ini;
//     folders open in the File Viewer, .app bundles and programs run).
//   * Drag an item: onto a File Viewer folder = move it there (Ctrl = copy) -- the item
//     stays and follows the file; onto an app window = the app opens it; onto the
//     desktop = remove it from the shelf. The File Viewer reports every move / rename
//     over IPC (service "shelf", shelfmsg.h), so references stay up to date; items whose
//     file is gone (deleted, trashed) drop off within ~2 s.
//   * The Trash, at the right end: drop items on it to move them to the Trash; click it
//     to open the Trash in the File Viewer.
//   * Tabs: click to switch, "+" adds one, double-click renames (type, Enter / Esc),
//     "-" (right end of the tab strip) removes the current tab -- after a confirmation
//     (apps/ask, ask.h) if it holds items. The wheel scrolls a long tab.
//
// Saved in SD:/etc/shelf.ini ("tab = Name" then "item = path" lines). The window spans
// the whole bottom of the screen (above the panel if the panel is at the bottom); it is
// a normal borderless one (WIN_FLAG_SYSTEM): other windows cover it, a click brings it
// forward. Uses drag & drop (ABI v42).
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
#define SH		100			// window height
#define TAB_H		22			// tab strip
#define CELL		80			// item cell width
#define ICON		40
#define TRASH_W		84			// trash cell at the right end
#define MAXTABS		8
#define MAXI		40
#define PANEL_BAR	60			// the panel's thickness (panel/main.cpp BAR)
#define DRAG_START	6

static const unsigned S_BG = 0x001C232C, S_EDGE = 0x00485870, S_TAB = 0x00283240,
	S_TABON = 0x00303D4D, S_TXT = 0x00E0E6EE, S_DIMT = 0x008A96A8, S_SEL = 0x00355070,
	S_HOT = 0x0090C0FF;

enum { K_FILE, K_DIR, K_APP, K_PROG, K_IMAGE, K_TEXT };

struct Item { char path[200]; char label[40]; int kind; unsigned *icon; int iw, ih; };
struct Tab  { char name[24]; Item items[MAXI]; int n; int scroll; };

static Tab  g_tabs[MAXTABS];
static int  g_ntabs = 0, g_cur = 0;
static int  g_W = 800, g_fw = 8, g_fh = 16;
static int  g_tabX[MAXTABS + 1], g_tabRight = 0;	// tab strip hit-test (right edges), "+"

// ---- items --------------------------------------------------------------------------
static bool ends_ci (const char *s, const char *ext)
{
	int n = fs_len (s), e = fs_len (ext);
	if (n < e) return false;
	for (int i = 0; i < e; i++) if (fs_lower (s[n - e + i]) != fs_lower (ext[i])) return false;
	return true;
}

// A path served by a file-system provider (FTP:..., FTPS:...), not the card. Opening a
// remote FILE downloads it whole, so remote items are never opened just to look at them:
// remote_stat asks the parent folder's listing instead (one small request).
static bool is_remote (const char *p) { return !(fs_lower (p[0]) == 's' && fs_lower (p[1]) == 'd' && p[2] == ':'); }
static bool remote_stat (const char *path, bool *isdir)
{
	char dir[256]; fs_copy (dir, path, sizeof dir);
	int n = fs_len (dir); while (n > 0 && dir[n - 1] == '/') dir[--n] = '\0';
	int k = n; while (k > 0 && dir[k - 1] != '/') k--;
	if (k == 0) { *isdir = true; return true; }		// "FTP:host" itself
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

static void item_init (Item &it, const char *path)
{
	fs_copy (it.path, path, sizeof it.path);
	it.icon = 0; it.iw = it.ih = 0;
	const char *base = fs_basename (path);
	fs_copy (it.label, base, sizeof it.label);
	bool remote = is_remote (path), dir = false;
	if (remote) remote_stat (path, &dir);
	else dir = fs_is_dir (path);
	if (dir)
	{
		it.kind = K_DIR;
		if (ends_ci (path, ".app"))
		{
			it.kind = K_APP;
			it.label[fs_len (it.label) - 4] = '\0';
			char p[260];
			fs_join (p, sizeof p, path, "app.txt");		// friendly name
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
	// The kind comes from fileassoc.ini (ext = app): imageview = an image, tinypad = text.
	char app[48];
	bool assoc = fa_app_for (path, app, sizeof app);
	if (assoc && fs_ci_cmp (app, "imageview") == 0) it.kind = K_IMAGE;
	else if (assoc && fs_ci_cmp (app, "tinypad") == 0) it.kind = K_TEXT;
	else if (!assoc && !remote && fa_is_program (path)) it.kind = K_PROG;	// (reads 4 bytes)
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
	if (t.n >= MAXI || tab_has (t, path)) return;
	bool d;
	if (is_remote (path) ? !remote_stat (path, &d) : !fs_exists (path)) return;
	item_init (t.items[t.n++], path);
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
	t.n = 0; t.scroll = 0;
}

// ---- persistence ----------------------------------------------------------------------
static void save (void)
{
	static char buf[MAXTABS * (MAXI * 210 + 40) + 64];
	int p = 0;
	const char *hdr = "# Onyx Shelf (apps/shelf): tab = name, then item = path lines\n";
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

// Drop the items whose file is gone (moved away / deleted).
static bool prune (void)
{
	bool changed = false;
	for (int t = 0; t < g_ntabs; t++)
		for (int i = g_tabs[t].n - 1; i >= 0; i--)
			if (!is_remote (g_tabs[t].items[i].path) && !fs_exists (g_tabs[t].items[i].path))
			{ tab_remove (g_tabs[t], i); changed = true; }	// (remote items: not polled)
	return changed;
}

// A file / folder moved from -> to (SHELF_MSG_MOVED): items on it, or inside it, follow.
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
			item_free (it);
			item_init (it, np);			// new path, label, icon
			changed = true;
		}
	return changed;
}

// Shorten a label to maxc characters keeping its extension visible: "sunset-big.png" ->
// "sunse..png" (not "sunset-b.."), so an image still reads as an image.
static void fit_label (char *s, int maxc)
{
	int n = fs_len (s);
	if (n <= maxc) return;
	int dot = -1; for (int i = n - 1; i > 0; i--) if (s[i] == '.') { dot = i; break; }
	int ext = dot > 0 ? n - dot : 0;				// ".png" = 4
	if (ext > 0 && ext <= 5 && maxc - ext - 2 >= 2)
	{
		int keep = maxc - ext - 2;
		char tail[8]; for (int i = 0; i < ext; i++) tail[i] = s[dot + i];
		s[keep] = '.'; s[keep + 1] = '.';
		for (int i = 0; i < ext; i++) s[keep + 2 + i] = tail[i];
		s[keep + 2 + ext] = '\0';
	}
	else { s[maxc - 2] = '.'; s[maxc - 1] = '.'; s[maxc] = '\0'; }
}

// ---- geometry -----------------------------------------------------------------------------
static int items_w (void) { return g_W - TRASH_W; }
static int item_at (int mx, int my)		// index in the current tab, -1 = none
{
	if (my < TAB_H || mx >= items_w ()) return -1;
	int i = g_tabs[g_cur].scroll + (mx - 4) / CELL;
	return (mx >= 4 && i < g_tabs[g_cur].n) ? i : -1;
}
static bool on_trash (int mx, int my) { return my >= TAB_H && mx >= items_w (); }
#define MINUS_W		22			// the "-" (remove tab) button, right end of the tab strip
#define TAB_MINUS	(MAXTABS + 1)
static int tab_at (int mx, int my)		// tab index, MAXTABS = "+", TAB_MINUS = "-", -1 = none
{
	if (my >= TAB_H) return -1;
	if (mx >= items_w () - MINUS_W - 4 && mx < items_w () - 4) return TAB_MINUS;
	for (int t = 0; t < g_ntabs; t++) if (mx < g_tabX[t]) return t;
	if (mx < g_tabRight) return MAXTABS;
	return -1;
}

// ---- glyphs -------------------------------------------------------------------------------
static void glyph (Canvas &cv, int x, int y, const Item &it)
{
	if (it.icon)					// an app icon (magenta = transparent)
	{
		for (int j = 0; j < it.ih && j < ICON; j++)
			for (int i = 0; i < it.iw && i < ICON; i++)
			{
				unsigned c = it.icon[j * it.iw + i] & 0xFFFFFF;
				if (c != 0xFF00FF) cv.pixel (x + i, y + j, c);
			}
		return;
	}
	switch (it.kind)
	{
	case K_DIR:					// a manila folder
		cv.fillRect (x + 2, y + 8, 16, 6, 0x00C89A48);
		cv.fillRect (x + 2, y + 12, 36, 24, 0x00E0B45C);
		cv.frameRect (x + 2, y + 12, 36, 24, 0x00906A28);
		break;
	case K_APP: case K_PROG:			// a window with a prompt
		cv.fillRect (x + 3, y + 6, 34, 28, 0x00101418);
		cv.frameRect (x + 3, y + 6, 34, 28, 0x0080C8FF);
		cv.fillRect (x + 3, y + 6, 34, 5, 0x0080C8FF);
		cv.text (x + 7, y + 14, ">_", 0x0060FF90);
		break;
	case K_IMAGE:					// a landscape
		cv.fillRect (x + 4, y + 6, 32, 28, 0x0070B8F0);
		cv.fillRect (x + 4, y + 24, 32, 10, 0x0050A050);
		cv.fillRect (x + 24, y + 10, 6, 6, 0x00FFE070);
		cv.frameRect (x + 4, y + 6, 32, 28, 0x00E0E6EE);
		break;
	default:					// a document (lines if text)
		cv.fillRect (x + 8, y + 3, 24, 34, 0x00F0F0F0);
		cv.frameRect (x + 8, y + 3, 24, 34, 0x00808890);
		if (it.kind == K_TEXT) for (int l = 0; l < 5; l++) cv.fillRect (x + 12, y + 9 + l * 5, 16, 2, 0x00707880);
		break;
	}
}

static void trash_glyph (Canvas &cv, int x, int y, bool full)
{
	cv.fillRect (x + 8, y + 6, 24, 3, 0x00A0A8B0);			// lid
	cv.fillRect (x + 16, y + 3, 8, 3, 0x00A0A8B0);
	cv.fillRect (x + 10, y + 10, 20, 26, 0x00707880);		// can
	for (int l = 0; l < 3; l++) cv.fillRect (x + 14 + l * 5, y + 13, 2, 20, 0x00505860);
	if (full) cv.fillRect (x + 12, y + 7, 16, 3, 0x00F0F0F0);	// paper sticking out
}

// ---- the window ---------------------------------------------------------------------------
class ShelfRoot : public Root
{
public:
	int armItem = -1, armX = 0, armY = 0;	// press on an item (click or drag)
	bool dragging = false;
	int dragItem = -1, dragTab = -1;	// our drag's source
	int hotItem = -2, hotTab = -1;		// drop highlight (-1 = items area, -3 = trash)
	int editTab = -1; char editBuf[24]; int editLen = 0;	// tab rename
	unsigned lastTabClick = 0; int lastTab = -1;
	bool trashFull = false;
	void *ask = 0; int askTab = -1;		// pending "remove this tab?" (apps/ask)
	unsigned lastPoll = 0;

	ShelfRoot (int x, int y, int w, int h)
	  : Root (x, y, w, h, "shelf", WIN_FLAG_BORDERLESS | WIN_FLAG_SYSTEM) {}

	void onDraw () override
	{
		canvas.clear (S_BG);
		canvas.fillRect (0, 0, width, 1, S_EDGE);
		// Tabs.
		int x = 6;
		for (int t = 0; t < g_ntabs; t++)
		{
			const char *nm = (t == editTab) ? editBuf : g_tabs[t].name;
			int w = fs_len (nm) * g_fw + 16 + (t == editTab ? g_fw : 0);
			canvas.fillRect (x, 3, w, TAB_H - 3, t == g_cur ? S_TABON : S_TAB);
			if (t == g_cur) canvas.fillRect (x, 3, w, 2, S_HOT);
			if (hotTab == t) canvas.frameRect (x, 3, w, TAB_H - 3, S_HOT);
			canvas.text (x + 8, 4 + (TAB_H - 3 - g_fh) / 2, nm, t == g_cur ? S_TXT : S_DIMT);
			if (t == editTab) canvas.fillRect (x + 8 + editLen * g_fw, 6, 2, g_fh, S_HOT);	// caret
			x += w + 2;
			g_tabX[t] = x;
		}
		canvas.fillRect (x, 3, 22, TAB_H - 3, S_TAB);
		canvas.text (x + 7, 4 + (TAB_H - 3 - g_fh) / 2, "+", S_DIMT);
		g_tabRight = x + 22;
		int mxb = items_w () - MINUS_W - 4;			// "-": remove the current tab
		canvas.fillRect (mxb, 3, MINUS_W, TAB_H - 3, S_TAB);
		canvas.text (mxb + (MINUS_W - g_fw) / 2, 4 + (TAB_H - 3 - g_fh) / 2, "-", g_ntabs > 1 ? S_TXT : S_TABON);
		canvas.fillRect (0, TAB_H, width, 1, S_TABON);

		// Items of the current tab.
		Tab &tb = g_tabs[g_cur];
		int maxc = (CELL - 6) / g_fw;
		for (int i = tb.scroll; i < tb.n; i++)
		{
			int cx = 4 + (i - tb.scroll) * CELL;
			if (cx + CELL > items_w ()) break;
			if (i == armItem && !dragging) canvas.fillRect (cx + 2, TAB_H + 3, CELL - 4, SH - TAB_H - 6, S_SEL);
			glyph (canvas, cx + (CELL - ICON) / 2, TAB_H + 6, tb.items[i]);
			char lab[40]; fs_copy (lab, tb.items[i].label, sizeof lab);
			fit_label (lab, maxc);
			int lw = fs_len (lab) * g_fw;
			canvas.text (cx + (CELL - lw) / 2, TAB_H + 50, lab, S_TXT);
		}
		if (tb.n == 0)
			canvas.text (12, TAB_H + 30, "Drop files, folders or apps here", S_DIMT);
		if (hotItem == -1) canvas.frameRect (2, TAB_H + 2, items_w () - 4, SH - TAB_H - 4, S_HOT);
		if (tb.scroll > 0) canvas.text (items_w () - 20, TAB_H + 2, "<", S_DIMT);

		// The Trash.
		int tx = items_w ();
		canvas.fillRect (tx, TAB_H + 4, 1, SH - TAB_H - 8, S_TABON);
		if (hotItem == -3) canvas.fillRect (tx + 3, TAB_H + 3, TRASH_W - 6, SH - TAB_H - 6, S_SEL);
		trash_glyph (canvas, tx + (TRASH_W - ICON) / 2, TAB_H + 6, trashFull);
		canvas.text (tx + (TRASH_W - 5 * g_fw) / 2, TAB_H + 50, "Trash", S_TXT);
	}

	void removeTab (int t)
	{
		if (t < 0 || t >= g_ntabs || g_ntabs <= 1) return;
		while (g_tabs[t].n > 0) tab_remove (g_tabs[t], g_tabs[t].n - 1);
		for (int j = t; j + 1 < g_ntabs; j++) g_tabs[j] = g_tabs[j + 1];
		g_ntabs--;
		g_tabs[g_ntabs].n = 0;
		if (g_cur >= g_ntabs) g_cur = g_ntabs - 1;
		save ();
		invalidate (true);
	}
	void askRemoveTab ()
	{
		if (g_ntabs <= 1 || ask != 0) return;		// keep one tab; one question at a time
		Tab &tb = g_tabs[g_cur];
		if (tb.n == 0) { removeTab (g_cur); return; }
		static char msg[120]; int p = 0;
		const char *a = "Remove tab \""; for (int i = 0; a[i]; i++) msg[p++] = a[i];
		for (int i = 0; tb.name[i]; i++) msg[p++] = tb.name[i];
		const char *b = "\" and its items? Files are kept."; for (int i = 0; b[i]; i++) msg[p++] = b[i];
		msg[p] = '\0';
		ask = ask_begin ("Remove tab", msg, "Remove", "Cancel");
		askTab = g_cur;
		if (ask == 0) notify ("Shelf", "Cannot show the confirmation (apps/ask missing?).");
	}

	// Each frame: the pending question, move reports from the File Viewer, and every
	// ~2 s items whose file is gone + the Trash state.
	void onTick () override
	{
		if (ask != 0)
		{
			int r = ask_poll (ask);
			if (r >= 0) { void *h = ask; ask = 0; (void) h; if (r == 1) removeTab (askTab); askTab = -1; }
		}
		bool changed = false;
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
		unsigned now = kapi_get_ticks ();
		if (now - lastPoll >= 200)
		{
			lastPoll = now;
			if (!dragging && prune ()) changed = true;
			bool full = trash_count () > 0;
			if (full != trashFull) { trashFull = full; invalidate (true); }
		}
		if (changed) { save (); invalidate (true); }
	}

	void startEdit (int t)
	{
		editTab = t; fs_copy (editBuf, g_tabs[t].name, sizeof editBuf); editLen = fs_len (editBuf);
	}
	void endEdit (bool keep)
	{
		if (editTab >= 0 && keep && editLen > 0) { fs_copy (g_tabs[editTab].name, editBuf, sizeof g_tabs[editTab].name); save (); }
		editTab = -1;
	}

	bool onMouse (int mx, int my, int bl, int br, int, int wheel) override
	{
		if (mx < 0) { pressed = false; armItem = -1; invalidate (true); return false; }
		Tab &tb = g_tabs[g_cur];
		if (wheel)
		{
			int vis = (items_w () - 4) / CELL, mx2 = tb.n - vis; if (mx2 < 0) mx2 = 0;
			tb.scroll -= wheel; if (tb.scroll < 0) tb.scroll = 0; if (tb.scroll > mx2) tb.scroll = mx2;
			invalidate (true);
			return true;
		}
		if (!bl && !br)
		{
			if (pressed && armItem >= 0 && !dragging)	// a click (no drag): open it
			{
				if (!fa_open (tb.items[armItem].path))
					notify ("Shelf", "No application to open this item.");
			}
			pressed = false; armItem = -1;
			invalidate (true);
			return true;
		}
		if (pressed)
		{
			int dx = mx - armX, dy = my - armY;
			if (armItem >= 0 && !dragging && dx * dx + dy * dy > DRAG_START * DRAG_START)
			{
				const Item &it = tb.items[armItem];
				if (kapi_drag_begin (DND_FILES, it.path, (unsigned) fs_len (it.path) + 1, it.label))
				{
					dragging = true; dragItem = armItem; dragTab = g_cur;
				}
				armItem = -1;
				invalidate (true);
			}
			return true;
		}
		pressed = true;
		if (bl)
		{
			if (editTab >= 0) endEdit (true);
			int t = tab_at (mx, my);
			if (t == MAXTABS)			// "+": a new tab
			{
				char nm[24] = "Tab "; int n = 4, v = g_ntabs + 1;
				if (v >= 10) nm[n++] = (char) ('0' + v / 10);
				nm[n++] = (char) ('0' + v % 10); nm[n] = '\0';
				new_tab (nm);
				g_cur = g_ntabs - 1;
				save ();
			}
			else if (t == TAB_MINUS) askRemoveTab ();
			else if (t >= 0)
			{
				unsigned now = kapi_get_ticks ();
				if (t == lastTab && now - lastTabClick < 70) startEdit (t);	// double-click: rename
				g_cur = t; lastTab = t; lastTabClick = now;
			}
			else if (on_trash (mx, my)) fa_open (TRASH_FILES);
			else { armItem = item_at (mx, my); armX = mx; armY = my; }
			invalidate (true);
		}
		return true;
	}

	bool onKey (long k) override
	{
		if (editTab < 0) return false;
		if (k == KEY_ENTER) endEdit (true);
		else if (k == 27) endEdit (false);
		else if (k == KEY_BACKSPACE) { if (editLen > 0) editBuf[--editLen] = '\0'; }
		else if (k >= ' ' && k < 127 && editLen < (int) sizeof editBuf - 1) { editBuf[editLen++] = (char) k; editBuf[editLen] = '\0'; }
		invalidate (true);
		return true;
	}

	void onDragOver (int x, int y, bool leave, unsigned) override
	{
		int hi = -2, ht = -1;
		if (!leave)
		{
			if (on_trash (x, y)) hi = -3;
			else if (y >= TAB_H) hi = -1;
			else { int t = tab_at (x, y); if (t >= 0 && t < g_ntabs) ht = t; }
		}
		if (hi != hotItem || ht != hotTab) { hotItem = hi; hotTab = ht; invalidate (true); }
	}

	void onDrop (int x, int y, int type, const char *data, int, unsigned) override
	{
		hotItem = -2; hotTab = -1;
		if (type != DND_FILES) { invalidate (true); return; }
		bool toTrash = on_trash (x, y);
		int t = tab_at (x, y);
		Tab &dst = (t >= 0 && t < g_ntabs) ? g_tabs[t] : g_tabs[g_cur];
		int trashed = 0;
		for (const char *p = data; *p; )
		{
			char path[200]; int n = 0;
			while (*p && *p != '\n') { if (n < (int) sizeof path - 1) path[n++] = *p; p++; }
			if (*p == '\n') p++;
			path[n] = '\0';
			if (n == 0) continue;
			if (toTrash) { if (trash_move (path)) trashed++; }
			else tab_add (dst, path);
		}
		if (toTrash)
		{
			prune ();
			if (trashed) notify ("Trash", trashed == 1 ? "1 item moved to the Trash." : "Items moved to the Trash.");
		}
		save ();
		trashFull = trash_count () > 0;
		invalidate (true);
	}

	void onDragDone (int, unsigned flags) override
	{
		dragging = false;
		// Dragged onto the desktop: take it off the shelf. Moved by the target (a File
		// Viewer folder): its SHELF_MSG_MOVED updates the item (onTick); gone (the Trash):
		// the periodic prune drops it.
		if ((flags & DND_F_DESKTOP) && !(flags & DND_F_CANCEL) && dragTab >= 0 && dragTab < g_ntabs)
			tab_remove (g_tabs[dragTab], dragItem);
		save ();
		trashFull = trash_count () > 0;
		dragItem = dragTab = -1;
		invalidate (true);
	}
};

int main (void)
{
	int sw = 1024, sh = 768;
	kapi_screen_size (&sw, &sh);
	g_fw = kapi_font_width ();  if (g_fw < 1) g_fw = 8;
	g_fh = kapi_font_height (); if (g_fh < 1) g_fh = 16;

	// The whole bottom edge -- above the panel if the panel is at the bottom (its
	// config.ini position 4); a side panel just overlaps its end (z-order decides).
	int pos = 3;
	if (app_ini_load_path ("SD:apps/panel.app/config.ini") >= 0) pos = app_ini_get_int (0, "position", 3);
	int x0 = 0, y0 = sh - SH, w = sw;
	if (pos == 4) y0 = sh - SH - PANEL_BAR - 6;
	g_W = w;

	kapi_ipc_register (SHELF_SERVICE);		// the File Viewer's move reports
	load ();
	ShelfRoot root (x0, y0, w, SH);
	if (root.canvas.px == 0) return 1;
	root.trashFull = trash_count () > 0;
	root.run ();
	return 0;
}
