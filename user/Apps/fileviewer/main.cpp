//
// fileviewer -- a NeXTSTEP-style column browser (Miller columns) for the SD card.
//
// Each column lists one folder; selecting a folder opens its content in the next column,
// so the whole path stays on screen and going back is one click on an earlier column (or
// on the path bar above them). Selecting a file shows a preview column: size, type, the
// first lines of a text file, a scaled-down BMP, or an app bundle's icon + name.
//
// On the left the PLACES, in three groups a click on their title folds or unfolds (as
// elementary OS's Files): Personal -- the folders pinned there (each under a name of its own:
// a folder's right-click menu, Pin to Sidebar..., or Go > Pin This Folder...) and the Trash;
// Computer -- the SD card's partitions (SD:, SD1: .. SD3:, and the VD0: .. disk images to come)
// and RAM:, the volume in memory, when there is one;
// Network -- the servers connected once (Go > Connect to Server..., under a name: a click
// connects again, the login kept by /bin/ftpfs) and Connect to Server... A right click on a
// place: Rename..., Unpin / Forget, Eject (a USB volume: its whole device); files dropped on a pinned folder, the Trash or a volume go
// there. They are kept in SD:/etc/places.ini ("pin = name|path",
// "net = name|FTP:host/folder", "folded = ..."). Above the columns the path bar: the current
// folder's path, each folder a link (its name underlined under the pointer), the one shown in
// the accent.
//
// Mouse: click = select (a folder opens in the next column), double-click = open (a
// file in tinypad / run a program / launch a .app), wheel = scroll the column under the
// cursor, click a path segment = jump back to that folder, right click = the item's menu.
// Keys: Up/Down/PgUp/PgDn/Home/End move, Right enters a folder, Left/Backspace goes back,
// Enter opens, a letter jumps to the next name starting with it, Del deletes,
// Ctrl-C/X/V copy/cut/paste, Ctrl-N new folder, Ctrl-R rename, Ctrl-L refresh.
// These commands are in the system menu bar (uikit::Menu): File (Open, New Folder,
// Rename..., Move to Trash (Del), Delete Permanently..., Refresh), Edit (Copy, Cut,
// Paste -- the system clipboard) and Go (SD Card, Trash, Restore from Trash, Empty
// Trash...). Del moves to SD:/.Trash (trash.h); inside the Trash it deletes for good.
// Hidden entries (names starting with '.') are not listed. Operations act on the
// selection of the active column; Paste and New Folder target the active column's folder.
//
#include "appkit/appkit.h"
#include "systemkit/systemkit.h"
#include "uikit/bmp.h"
#include "filekit/filekit.h"
#include "netkit/netkit.h"
#include "imagekit/img/imgload.hpp"		// preview: BMP GIF PNG JPEG PCX WebP (codecs in libuikit)
#include "uikit/uikit.h"
#include "fontkit/uikitface.h"

using namespace uikit;

// The window follows its size (resizable; PocketUI fills it): 880 x 540 at first, the places at the left, as many
// columns as fit (200 px at least, 3 at first, 6 at most), the path bar across, the status line at the bottom.
#define W0	880
#define H0	540
#define SIDE0	188			// the places (left)
static int g_W = W0, g_H = H0, g_sideW = SIDE0, g_vis = 3;
#define W	g_W
#define H	g_H
#define SIDE_W	g_sideW
#define VIS	g_vis			// columns visible at once
#define COLX	SIDE_W			// the columns' left edge
#define COLW	((W - SIDE_W) / VIS)
#define TB_H	0			// (no toolbar: commands are in the menu bar)
#define BC_H	46			// path bar
#define SB_H	12			// horizontal scrollbar
#define ST_H	22			// status bar
#define COL_Y	(TB_H + BC_H)
#define COL_H	(H - COL_Y - SB_H - ST_H)
#define ROW_PAD	4			// above the first row of a column
#define TXT_PAD	12			// a row's text from the column's left edge
#define MAXCOL	16
#define MAXE	256
#define NAMEL	72
#define CLICK_DELAY 70			// double-click window, HZ ticks (~700 ms)

// The theme's colours (uikit/theme.h: read when drawn). The columns are lists (the field, its
// text; a selection in the accent -- dimmer in the columns without the keyboard), the path and
// status bars the face; folders in the dark accent, app bundles in a dark green.
#define C_COL		C_FIELD
#define C_COLSEP	uk_mix (C_FIELD, C_FIELD_TEXT, 40)
#define C_DIRTXT	uk_tone (C_ACCENT, 84)
#define C_FILETXT	C_FIELD_TEXT
#define C_APPTXT	0x002E7D32
#define C_DIMTXT	uk_mix (C_FIELD, C_FIELD_TEXT, 130)

// name = the file name (all file operations); label = what the column shows -- the same,
// except for an app bundle: its friendly name from app.txt ("demoB.app" -> "Colour Field").
struct Entry { char name[NAMEL]; char label[NAMEL]; unsigned size; unsigned char isdir, isapp; };
struct Column { char path[256]; Entry *e; int count, sel, top; };

static Column g_col[MAXCOL];
static int g_ncol = 0;			// folder columns shown
static int g_active = 0;		// keyboard/operation column
static int g_first = 0;			// first visible slot (horizontal scroll)
static int g_fw = 8, g_fh = 16, g_rowH = 20, g_rows = 1;
static Root *g_root = 0;
static Scrollbar *g_hsb = 0;
static char g_status[160] = "";


// ---- small string helpers ------------------------------------------------------
static int slen (const char *s) { int n = 0; while (s[n]) n++; return n; }
static void scopy (char *d, const char *s, int cap) { int i = 0; for (; s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = '\0'; }
static char lower (char c) { return (c >= 'A' && c <= 'Z') ? (char) (c + 32) : c; }
static int ci_cmp (const char *a, const char *b)
{
	for (;; a++, b++)
	{
		char x = lower (*a), y = lower (*b);
		if (x != y || !x) return (unsigned char) x - (unsigned char) y;
	}
}
static int ends_with (const char *name, const char *ext)	// case-insensitive ".ext"
{
	int n = slen (name), e = slen (ext);
	if (n < e + 1 || name[n - e - 1] != '.') return 0;
	for (int i = 0; i < e; i++) if (lower (name[n - e + i]) != lower (ext[i])) return 0;
	return 1;
}
static void join (char *out, int cap, const char *dir, const char *name)
{
	int p = 0;
	for (; dir[p] && p < cap - 1; p++) out[p] = dir[p];
	if (p > 0 && out[p - 1] != '/' && p < cap - 1) out[p++] = '/';
	for (int k = 0; name[k] && p < cap - 1; k++) out[p++] = name[k];
	out[p] = '\0';
}
static void fmt_size (char *out, unsigned v)		// "123 B" / "12.3 KB" / "4.5 MB"
{
	char t[16]; int n = 0;
	const char *unit = " B";
	unsigned whole = v, frac = 0;
	if (v >= 1024 * 1024) { whole = v / (1024 * 1024); frac = (v % (1024 * 1024)) * 10 / (1024 * 1024); unit = " MB"; }
	else if (v >= 1024)   { whole = v / 1024; frac = (v % 1024) * 10 / 1024; unit = " KB"; }
	if (whole == 0) t[n++] = '0';
	while (whole) { t[n++] = (char) ('0' + whole % 10); whole /= 10; }
	int p = 0; while (n) out[p++] = t[--n];
	if (unit[1] != 'B') { out[p++] = '.'; out[p++] = (char) ('0' + frac); }
	for (int i = 0; unit[i]; i++) out[p++] = unit[i];
	out[p] = '\0';
}
static void status (const char *a, const char *b = "")
{
	int p = 0;
	for (int i = 0; a[i] && p < (int) sizeof g_status - 1; i++) g_status[p++] = a[i];
	for (int i = 0; b[i] && p < (int) sizeof g_status - 1; i++) g_status[p++] = b[i];
	g_status[p] = '\0';
}
static int is_program (const char *path)
{
	void *f = kapi_open (path);
	if (!f) return 0;
	unsigned char m[4] = { 0 };
	int n = kapi_read (f, m, sizeof m);
	kapi_close (f);
	return n == 4 && m[0] == 0x7F && m[1] == 'E' && m[2] == 'L' && m[3] == 'F';
}
// The friendly name of an app bundle: "name = ..." in <appdir>/app.txt. 0 if none.
static int app_name (const char *appdir, char *out, int cap)
{
	char p[320]; join (p, sizeof p, appdir, "app.txt");
	void *f = kapi_open (p);
	if (!f) return 0;
	char b[512]; int n = kapi_read (f, b, sizeof b - 1); kapi_close (f);
	if (n < 0) n = 0;
	b[n] = '\0';
	for (int i = 0; i + 4 <= n; i++)
	{
		if ((i == 0 || b[i - 1] == '\n') && b[i] == 'n' && b[i + 1] == 'a' && b[i + 2] == 'm' && b[i + 3] == 'e')
		{
			int j = i + 4; while (b[j] == ' ' || b[j] == '=' || b[j] == '\t') j++;
			int k = 0; while (b[j] && b[j] != '\n' && b[j] != '\r' && k < cap - 1) out[k++] = b[j++];
			while (k > 0 && (out[k - 1] == ' ' || out[k - 1] == '\t')) k--;
			out[k] = '\0';
			return k > 0;
		}
	}
	return 0;
}

// ---- columns ------------------------------------------------------------------------
static void load_col (int c, const char *path)
{
	Column &k = g_col[c];
	scopy (k.path, path, sizeof k.path);
	k.count = 0; k.sel = -1; k.top = 0;
	void *d = kapi_opendir (path);
	if (d == 0) return;
	struct kapi_dirent ent;
	while (k.count < MAXE && kapi_readdir (d, &ent))
	{
		if (ent.name[0] == '.') continue;		// ".", "..", hidden entries (".Trash")
		Entry &e = k.e[k.count++];
		scopy (e.name, ent.name, NAMEL);
		e.size = ent.size; e.isdir = ent.is_dir ? 1 : 0;
		e.isapp = e.isdir && ends_with (e.name, "app");
		scopy (e.label, e.name, NAMEL);
		if (e.isapp)
		{
			e.label[slen (e.label) - 4] = '\0';		// drop ".app"
			char p[300]; join (p, sizeof p, path, e.name);
			app_name (p, e.label, NAMEL);			// friendly name, if any
		}
	}
	kapi_closedir (d);
	// Sort: plain folders first, then app bundles + files; alphabetical by the shown label.
	for (int i = 1; i < k.count; i++)
	{
		Entry t = k.e[i]; int j = i - 1;
		int tg = (t.isdir && !t.isapp) ? 0 : 1;
		while (j >= 0)
		{
			int g = (k.e[j].isdir && !k.e[j].isapp) ? 0 : 1;
			if (g < tg || (g == tg && ci_cmp (k.e[j].label, t.label) <= 0)) break;
			k.e[j + 1] = k.e[j]; j--;
		}
		k.e[j + 1] = t;
	}
}

static const Entry *sel_entry (int c)
{
	if (c < 0 || c >= g_ncol) return 0;
	const Column &k = g_col[c];
	return (k.sel >= 0 && k.sel < k.count) ? &k.e[k.sel] : 0;
}
static bool preview_on (void)			// the deepest selection is a file / app bundle
{
	const Entry *e = sel_entry (g_ncol - 1);
	return e && (!e->isdir || e->isapp);
}
static int total_slots (void) { return g_ncol + (preview_on () ? 1 : 0); }

static void sync_hsb (void)
{
	int maxf = total_slots () - VIS; if (maxf < 0) maxf = 0;
	if (g_first > maxf) g_first = maxf;
	if (g_first < 0) g_first = 0;
	if (g_hsb && (g_hsb->vmax != (maxf < 1 ? 1 : maxf) || g_hsb->value != g_first))
	{ g_hsb->vmax = maxf < 1 ? 1 : maxf; g_hsb->value = g_first; g_hsb->invalidate (true); }
}
static void show_deepest (void)			// scroll so the deepest columns are visible
{
	int t = total_slots ();
	g_first = t > VIS ? t - VIS : 0;
	if (g_active < g_first) g_first = g_active;
}
static void keep_sel_visible (int c)
{
	Column &k = g_col[c];
	if (k.sel < 0) return;
	if (k.sel < k.top) k.top = k.sel;
	if (k.sel >= k.top + g_rows) k.top = k.sel - g_rows + 1;
	if (k.top < 0) k.top = 0;
}

// ---- preview (decoded once per selection) -----------------------------------------
enum { PV_NONE, PV_TEXT, PV_IMAGE, PV_APP, PV_PROGRAM, PV_BINARY, PV_EMPTY };
static int g_pvKind = PV_NONE;
static char g_pvText[1600];
static unsigned *g_pvImg = 0; static int g_pvW = 0, g_pvH = 0;
static const char *g_pvFormat = "BMP";		// image preview: "PNG", "JPEG", ...
static char g_pvTitle[64];

static void preview_clear (void)
{
	if (g_pvImg) { delete [] g_pvImg; g_pvImg = 0; }
	g_pvKind = PV_NONE; g_pvText[0] = '\0'; g_pvTitle[0] = '\0';
}
// A path served by a file-system provider (FTP:..., FTPS:...), not the SD card: opening a
// file there downloads it whole, so previews are limited to small files.
// The SD card's volumes: SD: (partition 1, the boot one) and SD1: .. SD3: (partitions 2..4, FAT or exFAT).
// RAM:, the volume in memory, is local like them (its files are read in place, not downloaded); so are
// the USB sticks (USB:, USB2:, USB3:).
static int sd_volume (const char *path)			// length of the "SD:" / "SDn:" / "RAM:" prefix, 0 = not a local volume
{
	if (lower (path[0]) == 'r' && lower (path[1]) == 'a' && lower (path[2]) == 'm' && path[3] == ':') return 4;
	if (lower (path[0]) == 'u' && lower (path[1]) == 's' && lower (path[2]) == 'b')	// (v93) USB1:..USB3:, USB1P2:.., USB: (= USB1:)
	{
		if (path[3] == ':') return 4;
		if (path[3] < '1' || path[3] > '3') return 0;
		if (path[4] == ':') return 5;
		return lower (path[4]) == 'p' && path[5] >= '1' && path[5] <= '4' && path[6] == ':' ? 7 : 0;
	}
	if (lower (path[0]) != 's' || lower (path[1]) != 'd') return 0;
	if (path[2] == ':') return 3;
	return path[2] >= '0' && path[2] <= '3' && path[3] == ':' ? 4 : 0;
}
static bool is_remote (const char *path) { return sd_volume (path) == 0; }
// The volume of a path: "SD1:/roms" -> "SD1:" ("SD0:" is "SD:"), "FTP:host/x" -> "FTP:host".
static void volume_of (const char *path, char *out, int cap)
{
	int n = 0;
	if (int v = sd_volume (path))
	{
		for (int i = 0; i < v - 1 && n < cap - 2; i++) out[n++] = (char) (path[i] >= 'a' && path[i] <= 'z' ? path[i] - 32 : path[i]);
		if (n == 3 && out[2] == '0') n = 2;
		out[n++] = ':';
	}
	else for (int i = 0; path[i] && path[i] != '/' && n < cap - 1; i++) out[n++] = lower (path[i]);
	out[n] = '\0';
}
static bool same_volume (const char *a, const char *b)
{
	char va[64], vb[64]; volume_of (a, va, sizeof va); volume_of (b, vb, sizeof vb);
	return ci_cmp (va, vb) == 0;
}
#define REMOTE_PREVIEW_MAX	(1024u * 1024)

static void preview_build (void)
{
	preview_clear ();
	if (!preview_on ()) return;
	const Entry *e = sel_entry (g_ncol - 1);
	char path[300];
	join (path, sizeof path, g_col[g_ncol - 1].path, e->name);
	if (is_remote (path) && !e->isdir && e->size > REMOTE_PREVIEW_MAX) { g_pvKind = PV_BINARY; return; }

	if (e->isapp)
	{
		g_pvKind = PV_APP;
		scopy (g_pvTitle, e->label, sizeof g_pvTitle);	// friendly name (app.txt)
		char p2[320];
		join (p2, sizeof p2, path, "icon.bmp");
		g_pvImg = ui::bmp_decode (p2, &g_pvW, &g_pvH);
		return;
	}
	if (e->size == 0) { g_pvKind = PV_EMPTY; return; }
	if (img_is_image_name (e->name))
	{
		// First frame only (new unsigned[], freed by delete [] like the BMP icons).
		ImgFrames im;
		if (img_load (path, &im))
		{
			g_pvImg = im.px[0]; g_pvW = im.w; g_pvH = im.h; g_pvFormat = im.format;
			for (int i = 1; i < im.n; i++) delete [] im.px[i];
		}
		g_pvKind = g_pvImg ? PV_IMAGE : PV_BINARY;
		return;
	}
	if (is_program (path)) { g_pvKind = PV_PROGRAM; return; }

	void *f = kapi_open (path);
	if (!f) { g_pvKind = PV_BINARY; return; }
	int n = kapi_read (f, g_pvText, sizeof g_pvText - 1);
	kapi_close (f);
	if (n < 0) n = 0;
	g_pvText[n] = '\0';
	for (int i = 0; i < n; i++)
	{
		unsigned char c = (unsigned char) g_pvText[i];
		if (c == 0 || (c < 32 && c != '\n' && c != '\r' && c != '\t')) { g_pvKind = PV_BINARY; g_pvText[0] = '\0'; return; }
	}
	g_pvKind = PV_TEXT;
}

// ---- selection / navigation -----------------------------------------------------------
static void update_status (void)
{
	const Column &k = g_col[g_active];
	char n[16], sz[24];
	int v = k.count, p = 0; char t[12]; int m = 0;
	if (v == 0) t[m++] = '0';
	while (v) { t[m++] = (char) ('0' + v % 10); v /= 10; }
	while (m) n[p++] = t[--m];
	n[p] = '\0';
	const Entry *e = sel_entry (g_active);
	if (e && !e->isdir) { fmt_size (sz, e->size); status (n, TR (" items   -   ")); int q = slen (g_status);
		for (int i = 0; e->name[i] && q < (int) sizeof g_status - 20; i++) g_status[q++] = e->name[i];
		g_status[q++] = ' '; g_status[q++] = '('; for (int i = 0; sz[i]; i++) g_status[q++] = sz[i]; g_status[q++] = ')'; g_status[q] = '\0'; }
	else status (n, TR (" items"));
}

// Select entry idx of column c: a plain folder opens in column c+1, anything else shows
// the preview. Deeper columns are dropped.
static void select (int c, int idx)
{
	Column &k = g_col[c];
	if (idx < 0 || idx >= k.count) return;
	k.sel = idx;
	g_active = c;
	g_ncol = c + 1;
	const Entry &e = k.e[idx];
	if (e.isdir && !e.isapp && c + 1 < MAXCOL)
	{
		char p[256];
		join (p, sizeof p, k.path, e.name);
		load_col (c + 1, p);
		g_ncol = c + 2;
	}
	keep_sel_visible (c);
	preview_build ();
	show_deepest ();
	update_status ();
}

static void go_back (void)			// Left: back to the parent column
{
	if (g_active == 0) return;
	g_col[g_active].sel = -1;
	g_ncol = g_active + 1;			// keep this column visible, unselected
	g_active--;
	preview_build ();
	show_deepest ();
	update_status ();
}

static void jump_to (int c)			// path bar: make column c the deepest
{
	if (c < 0 || c >= g_ncol) return;
	g_col[c].sel = -1;
	g_ncol = c + 1;
	g_active = c;
	preview_build ();
	show_deepest ();
	update_status ();
}

// Reload every shown column, keeping the selections (by name) where they still exist.
static void refresh (void)
{
	char names[MAXCOL][NAMEL];
	int n = g_ncol;
	for (int c = 0; c < n; c++)
	{
		const Entry *e = sel_entry (c);
		scopy (names[c], e ? e->name : "", NAMEL);
	}
	int act = g_active;
	g_ncol = 1;
	load_col (0, g_col[0].path);
	for (int c = 0; c < n; c++)
	{
		if (names[c][0] == '\0') break;
		int idx = -1;
		for (int i = 0; i < g_col[c].count; i++) if (ci_cmp (g_col[c].e[i].name, names[c]) == 0) { idx = i; break; }
		if (idx < 0) break;
		select (c, idx);
	}
	g_active = act < g_ncol ? act : g_ncol - 1;
	preview_build ();
	show_deepest ();
	update_status ();
}

// ---- opening ------------------------------------------------------------------------
static void open_entry (int c)
{
	const Entry *e = sel_entry (c);
	if (!e) return;
	char path[300];
	join (path, sizeof path, g_col[c].path, e->name);
	if (e->isapp)
	{
		char name[NAMEL]; scopy (name, e->name, sizeof name);
		name[slen (name) - 4] = '\0';
		lx_launch (name, 0);
		status (TR ("Launched "), name);
	}
	else if (e->isdir) { if (c + 1 < g_ncol && g_col[c + 1].count > 0) select (c + 1, 0); }
	else
	{
		// SD:/etc/fileassoc.ini ("ext = app"), else an ELF program runs (fileassoc.h).
		char app[48];
		if (fa_app_for (path, app, sizeof app) && fa_open (path)) status (TR ("Opened in "), app);
		else if (fa_open (path)) status (TR ("Running "), e->name);
		else status (TR ("No application to open "), e->name);
	}
}

// ---- file operations (fsutil.h / trash.h) ----------------------------------------------
static bool exists (const char *path) { return fs_exists (path); }
static bool copy_file (const char *src, const char *dst) { return fs_copy_file (src, dst); }
static bool copy_tree (const char *src, const char *dst, int depth) { return fs_copy_tree (src, dst, depth); }
static bool remove_tree (const char *path, int depth) { return fs_remove_tree (path, depth); }
static void unique_name (char *out, int cap, const char *dir, const char *name) { fs_unique_name (out, cap, dir, name); }

// Browsing the trash? (column 0 is then SD:/.Trash/files, shown as "Trash")
static bool in_trash (void) { return ci_cmp (g_col[0].path, TRASH_FILES) == 0; }

// ---- input dialog (rename / new folder) ----------------------------------------------
static void dlg_btn (Widget &w) { ((Modal *) w.parent)->onButton (w.tag); }
static void dlg_enter (Widget &w) { ((Modal *) w.parent)->close (1); }

class InputBox : public Modal
{
	const char *m_title;
public:
	Textbox *tb;
	InputBox (const char *title, const char *init) : Modal (340, 120), m_title (title)
	{
		Root *r = Root::current ();
		left = ((r ? r->width : W) - width) / 2; top = ((r ? r->height : H) - height) / 2;
		tb = new Textbox (12, uk_fh () + 16, width - 24, 26, init, dlg_enter);
		tb->caret = slen (init);
		tb->hasFocus = true;
		addChild (tb);
		Button *b;
		b = new Button (width - 180, height - 36, 82, 28, "OK", dlg_btn);     b->tag = 1; addChild (b);
		b = new Button (width - 92,  height - 36, 82, 28, TR ("Cancel"), dlg_btn); b->tag = 0; addChild (b);
	}
	void onButton (int tag) override { close (tag); }
	bool onKey (long k) override { if (k == 27) { close (0); return true; } return false; }
	void onDraw () override { drawBox (m_title); }	// (the dialogs' box: uikit/dialog.h)
};

static bool ask_name (const char *title, const char *init, char *out, int cap)
{
	InputBox box (title, init);
	if (!box.run () || box.tb->text[0] == '\0') return false;
	for (int i = 0; box.tb->text[i]; i++)
		if (box.tb->text[i] == '/' || box.tb->text[i] == '\\' || box.tb->text[i] == ':') { uk_messagebox (TR ("Invalid name"), TR ("A name cannot contain / \\ or :"), MB_OK); return false; }
	scopy (out, box.tb->text, cap);
	return true;
}

static void op_new_folder ()
{
	char name[NAMEL];
	if (!ask_name (TR ("New folder in this column"), TR ("New Folder"), name, sizeof name)) return;
	char p[300]; join (p, sizeof p, g_col[g_active].path, name);
	if (exists (p) || kapi_mkdir (p) != 0) { status (TR ("Could not create folder "), name); return; }
	refresh ();
	for (int i = 0; i < g_col[g_active].count; i++)
		if (ci_cmp (g_col[g_active].e[i].name, name) == 0) { select (g_active, i); break; }
	status (TR ("Created folder "), name);
}
static void op_rename ()
{
	const Entry *e = sel_entry (g_active);
	if (!e) { status (TR ("Select something to rename")); return; }
	char name[NAMEL], old[NAMEL];
	scopy (old, e->name, sizeof old);
	if (!ask_name (TR ("Rename"), old, name, sizeof name) || ci_cmp (name, old) == 0) return;
	char s[300], d[300];
	join (s, sizeof s, g_col[g_active].path, old);
	join (d, sizeof d, g_col[g_active].path, name);
	if (exists (d) || kapi_rename (s, d) != 0) { status (TR ("Could not rename to "), name); return; }
	g_col[g_active].sel = -1; g_ncol = g_active + 1;
	refresh ();
	for (int i = 0; i < g_col[g_active].count; i++)
		if (ci_cmp (g_col[g_active].e[i].name, name) == 0) { select (g_active, i); break; }
	status (TR ("Renamed to "), name);
}
static void op_delete_permanently ();
static void op_delete ()			// Del: move to the trash (in the trash: for good)
{
	const Entry *e = sel_entry (g_active);
	if (!e) { status (TR ("Select something to delete")); return; }
	if (in_trash ()) { op_delete_permanently (); return; }
	char path[300]; join (path, sizeof path, g_col[g_active].path, e->name);
	char name[NAMEL]; scopy (name, e->name, sizeof name);
	bool ok = trash_move (path);
	g_col[g_active].sel = -1; g_ncol = g_active + 1;
	refresh ();
	status (ok ? TR ("Moved to the Trash: ") : TR ("Could not move to the Trash: "), name);
}

static void op_delete_permanently ()
{
	const Entry *e = sel_entry (g_active);
	if (!e) { status (TR ("Select something to delete")); return; }
	char msg[128]; int p = 0;
	const char *a = e->isdir ? TR ("Delete the folder for good\n") : TR ("Delete for good\n");
	for (int i = 0; a[i] && p < 60; i++) msg[p++] = a[i];
	for (int i = 0; e->name[i] && p < 100; i++) msg[p++] = e->name[i];
	if (e->isdir) { const char *b = TR ("\nand everything in it?"); for (int i = 0; b[i] && p < 126; i++) msg[p++] = b[i]; }
	else msg[p++] = '?';
	msg[p] = '\0';
	if (!uk_messagebox (TR ("Delete"), msg, MB_YESNO)) return;
	char path[300]; join (path, sizeof path, g_col[g_active].path, e->name);
	char name[NAMEL]; scopy (name, e->name, sizeof name);
	bool ok;
	if (in_trash () && g_active == 0) ok = trash_purge (name);	// drop its info file too
	else ok = e->isdir ? remove_tree (path, 0) : kapi_remove (path) == 0;
	g_col[g_active].sel = -1; g_ncol = g_active + 1;
	refresh ();
	status (ok ? TR ("Deleted ") : TR ("Could not delete "), name);
}

static void show_root (const char *path)
{
	load_col (0, path);
	g_ncol = 1; g_active = 0; g_first = 0;
	preview_build ();
	update_status ();
}
static void op_open_trash () { trash_ensure (); show_root (TRASH_FILES); status (TR ("Trash: Restore puts an item back, Del deletes it for good")); }
static void op_show_sd ()    { show_root ("SD:/"); }
static bool volume_mounted (const char *root) { void *d = kapi_opendir (root); if (d) kapi_closedir (d); return d != 0; }
static void show_volume (const char *root)
{
	if (volume_mounted (root)) show_root (root);
	else status (TR ("No FAT / exFAT partition there: "), root);
}
static void op_show_sd1 ()   { show_volume ("SD1:/"); }
static void op_show_sd2 ()   { show_volume ("SD2:/"); }
static void op_show_sd3 ()   { show_volume ("SD3:/"); }
// RAM:, the volume in memory (absent with "ramfs=0" in system.ini)
static void op_show_ram ()   { if (volume_mounted ("RAM:/")) show_root ("RAM:/"); else status (TR ("No RAM: volume"), ""); }

// ---- USB sticks (kapi v93: mounted when plugged in as USB:, USB2:, USB3:) -------------------------
#define MAXVOL	16
static struct kapi_volume g_vols[MAXVOL];
static int g_nvols = 0;
static unsigned g_volSig = 0;			// the volumes' generations, summed: a change -> the places again
static void vols_read (void)
{
	int n = kapi_vol_list (g_vols, MAXVOL, 0);
	g_nvols = n < 0 ? 0 : n > MAXVOL ? MAXVOL : n;
}
static unsigned vols_sig (void)
{
	unsigned s = 0;
	for (int i = 0; i < g_nvols; i++) s = s * 31 + g_vols[i].gen + g_vols[i].state;
	return s;
}
static void vol_root (const struct kapi_volume &v, char *out, int cap)	// "USB2:/"
{
	int n = 0;
	for (; v.name[n] && n < cap - 3; n++) out[n] = v.name[n];
	out[n++] = ':'; out[n++] = '/'; out[n] = 0;
}
static bool is_usb_path (const char *p)
{
	return lower (p[0]) == 'u' && lower (p[1]) == 's' && lower (p[2]) == 'b' && sd_volume (p) > 0;
}
static void op_show_usb ()
{
	vols_read ();
	for (int i = 0; i < g_nvols; i++)
		if ((g_vols[i].flags & KAPI_VF_REMOVABLE) && g_vols[i].state == KAPI_VST_MOUNTED)
		{
			char r[16]; vol_root (g_vols[i], r, sizeof r);
			show_root (r);
			return;
		}
	status (TR ("No USB stick is plugged in (or it is not formatted: Format...)"));
}
// The USB device of a path: 1..3 ("USB2:/x", "USB2P1:/x" -> 2; "USB:" is USB1:), 0 = not on a USB volume.
static int usb_dev (const char *p)
{
	if (!is_usb_path (p)) return 0;
	return p[3] == ':' ? 1 : p[3] - '0';
}
// Eject USB device dev, whole (the kernel ejects all its partitions): the view leaves it first.
static void eject_dev (int dev)
{
	char vol[8] = "USB1:"; vol[3] = (char) ('0' + dev);
	if (usb_dev (g_col[0].path) == dev) show_root ("SD:/");
	int r = kapi_vol_eject (vol, 0);
	if (r == -KAPI_EBUSY)
	{
		if (!uk_messagebox (TR ("Eject"), TR ("Files are still open on this stick (they were saved). Eject it anyway?"), MB_YESNO)) { status (vol, TR (" is still in use")); return; }
		r = kapi_vol_eject (vol, KAPI_EJECT_FORCE);
	}
	if (r == 0) status (vol, TR (" can be removed safely"));
	else status (TR ("Could not eject "), vol);
}
// Eject the USB device shown (else the one plugged in).
static void op_eject ()
{
	int dev = usb_dev (g_col[g_active].path);
	if (!dev)
	{
		vols_read ();
		for (int i = 0; i < g_nvols && !dev; i++)
			if ((g_vols[i].flags & KAPI_VF_REMOVABLE) && g_vols[i].state == KAPI_VST_MOUNTED)
			{
				char r[16]; vol_root (g_vols[i], r, sizeof r);
				dev = usb_dev (r);
			}
	}
	if (!dev) { status (TR ("No USB stick to eject")); return; }
	eject_dev (dev);
}
// The right-click menu's line for a path on a USB volume: "Eject USB1" (a partition's: its whole device).
static void eject_label (const char *path, char *out)
{
	int n = 0;						// (out: 64 bytes at least)
	for (const char *a = TR ("Eject"); *a && n < 30; a++) out[n++] = *a;
	for (const char *a = " USB"; *a; a++) out[n++] = *a;
	out[n++] = (char) ('0' + usb_dev (path));
	if (sd_volume (path) == 7) for (const char *b = TR (" (all its partitions)"); *b && n < 63; b++) out[n++] = *b;
	out[n] = 0;
}
static void op_format ()				// the Disks app: the volumes, Eject, Format
{
	const char *cur = g_col[g_active].path;
	char vol[16] = "";
	if (is_usb_path (cur)) { int n = 0; while (cur[n] && cur[n] != ':' && n < 12) { vol[n] = cur[n]; n++; } vol[n++] = ':'; vol[n] = 0; }
	if (uk_win_app_raise ("disks") == 0) lx_launch ("disks", vol[0] ? vol : 0);
}

// A path shown as columns from its volume's root: "SD1:/roms/gb" -> SD1: | roms | gb.
static void open_path (const char *path)
{
	if (is_remote (path)) { show_root (path); return; }
	char root[8]; volume_of (path, root, sizeof root - 1);
	int rn = 0; while (root[rn]) rn++; root[rn++] = '/'; root[rn] = '\0';
	show_root (root);
	const char *p = path + sd_volume (path); if (*p == '/') p++;
	while (*p)
	{
		char seg[NAMEL]; int n = 0;
		while (*p && *p != '/' && n < NAMEL - 1) seg[n++] = *p++;
		seg[n] = '\0';
		if (*p == '/') p++;
		if (!n) continue;
		int idx = -1;
		for (int i = 0; i < g_col[g_ncol - 1].count; i++) if (ci_cmp (g_col[g_ncol - 1].e[i].name, seg) == 0) { idx = i; break; }
		if (idx < 0) break;
		select (g_ncol - 1, idx);
	}
	update_status ();
}

// ---- the places (the sidebar) ------------------------------------------------------------------
// Three groups: Personal (the pinned folders, the Trash), Computer (the card's partitions, the
// disk images to come), Network (the servers connected once, Connect to Server...). The pins and
// the servers, each under a name, are kept in SD:/etc/places.ini: "pin = name|path",
// "net = name|FTP[S]:host[:port]/folder", "folded = personal,network" (the groups closed).
#define PLACES_INI	"SD:/etc/places.ini"
#define MAXPIN		16
enum { G_PERSONAL, G_COMPUTER, G_NETWORK, NGROUPS };
static const char *const GROUP_NAME[NGROUPS] = { TRN ("Personal"), TRN ("Computer"), TRN ("Network") };
static const char *const GROUP_KEY[NGROUPS] = { "personal", "computer", "network" };
enum { PL_PIN, PL_TRASH, PL_VOL, PL_NET, PL_CONNECT };
struct Place { int kind, group, index; char label[40]; char path[200]; };
static Place g_pl[MAXPIN * 2 + 12];
static int   g_npl;
static bool  g_folded[NGROUPS];
static char  g_pinName[MAXPIN][40], g_pinPath[MAXPIN][200]; static int g_npin;
static char  g_netName[MAXPIN][40], g_netPath[MAXPIN][200]; static int g_nnet;

static void places_save (void)
{
	static char o[8192];
	int p = 0;
	auto put = [&] (const char *t) { while (*t && p < (int) sizeof o - 2) o[p++] = *t++; };
	put ("; The File Viewer's places (its sidebar): pin = name|folder (Personal), net = name|server\n");
	put ("; (Network: FTP:host[:port]/folder, the login kept in ftpfs.ini), folded = the groups closed\n");
	for (int i = 0; i < g_npin; i++) { put ("pin = "); put (g_pinName[i]); put ("|"); put (g_pinPath[i]); put ("\n"); }
	for (int i = 0; i < g_nnet; i++) { put ("net = "); put (g_netName[i]); put ("|"); put (g_netPath[i]); put ("\n"); }
	bool any = false;
	for (int g = 0; g < NGROUPS; g++) if (g_folded[g]) { put (any ? "," : "folded = "); put (GROUP_KEY[g]); any = true; }
	if (any) put ("\n");
	o[p] = 0;
	kapi_save_file (PLACES_INI, o, (unsigned) p);
}

static void places_load (void)
{
	g_npin = g_nnet = 0;
	for (int g = 0; g < NGROUPS; g++) g_folded[g] = false;
	void *f = kapi_open (PLACES_INI);
	if (f == 0) return;
	static char buf[8192];
	int n = kapi_read (f, buf, sizeof buf - 1);
	kapi_close (f);
	if (n <= 0) return;
	buf[n] = 0;
	for (char *p = buf; *p; )
	{
		char *l = p; while (*p && *p != '\n') p++;
		if (*p) *p++ = 0;
		while (*l == ' ' || *l == '\t') l++;
		if (*l == ';' || *l == '#') continue;
		char *eq = l; while (*eq && *eq != '=') eq++;
		if (*eq != '=') continue;
		char *ke = eq; while (ke > l && (ke[-1] == ' ' || ke[-1] == '\t')) ke--;
		*ke = 0;
		char *v = eq + 1; while (*v == ' ' || *v == '\t') v++;
		char *ve = v; while (*ve) ve++;
		while (ve > v && (ve[-1] == ' ' || ve[-1] == '\t' || ve[-1] == '\r')) *--ve = 0;
		char *bar = v; while (*bar && *bar != '|') bar++;
		bool pin = ci_cmp (l, "pin") == 0, net = ci_cmp (l, "net") == 0;
		if ((pin || net) && *bar == '|')
		{
			*bar = 0;
			if (pin && g_npin < MAXPIN) { scopy (g_pinName[g_npin], v, 40); scopy (g_pinPath[g_npin], bar + 1, 200); g_npin++; }
			if (net && g_nnet < MAXPIN) { scopy (g_netName[g_nnet], v, 40); scopy (g_netPath[g_nnet], bar + 1, 200); g_nnet++; }
		}
		else if (ci_cmp (l, "folded") == 0)
			for (int g = 0; g < NGROUPS; g++)
			{
				int k = slen (GROUP_KEY[g]);
				for (char *q = v; *q; q++)
				{
					bool m = true;
					for (int i = 0; i < k && m; i++) m = lower (q[i]) == GROUP_KEY[g][i];
					if (m) g_folded[g] = true;
				}
			}
	}
}

static void add_place (int kind, int group, int index, const char *label, const char *path)
{
	if (g_npl >= (int) (sizeof g_pl / sizeof g_pl[0])) return;
	Place &p = g_pl[g_npl++];
	p.kind = kind; p.group = group; p.index = index;
	scopy (p.label, label, sizeof p.label); scopy (p.path, path, sizeof p.path);
}

static void places_build (void)
{
	g_npl = 0;
	for (int i = 0; i < g_npin; i++) add_place (PL_PIN, G_PERSONAL, i, g_pinName[i], g_pinPath[i]);
	add_place (PL_TRASH, G_PERSONAL, -1, TR ("Trash"), TRASH_FILES);
	add_place (PL_VOL, G_COMPUTER, -1, TR ("SD Card"), "SD:/");
	static const char *const VOLS[][2] = { { "SD1:/", TRN ("SD1: partition 2") }, { "SD2:/", TRN ("SD2: partition 3") },
		{ "SD3:/", TRN ("SD3: partition 4") }, { "VD0:/", TRN ("VD0: disk image") }, { "VD1:/", TRN ("VD1: disk image") },
		{ "VD2:/", TRN ("VD2: disk image") }, { "VD3:/", TRN ("VD3: disk image") }, { "RAM:/", TRN ("RAM: memory") } };
	for (unsigned i = 0; i < sizeof VOLS / sizeof VOLS[0]; i++)
		if (volume_mounted (VOLS[i][0])) add_place (PL_VOL, G_COMPUTER, -1, TR (VOLS[i][1]), VOLS[i][0]);
	vols_read ();						// (v93) the USB sticks mounted
	g_volSig = vols_sig ();
	for (int i = 0; i < g_nvols; i++)
	{
		if (!(g_vols[i].flags & KAPI_VF_REMOVABLE) || g_vols[i].state != KAPI_VST_MOUNTED) continue;
		char r[16], label[48]; vol_root (g_vols[i], r, sizeof r);
		int n = 0;
		for (const char *q = r; *q && *q != '/'; q++) label[n++] = *q;
		label[n++] = ' ';
		const char *t = g_vols[i].label[0] ? g_vols[i].label : TR ("USB stick");
		while (*t && n < (int) sizeof label - 1) label[n++] = *t++;
		label[n] = 0;
		add_place (PL_VOL, G_COMPUTER, -1, label, r);
	}
	for (int i = 0; i < g_nnet; i++) add_place (PL_NET, G_NETWORK, i, g_netName[i], g_netPath[i]);
	add_place (PL_CONNECT, G_NETWORK, -1, TR ("Add a Server..."), "");
}

// The sidebar's rows: a group's title (place -1), then its places unless it is folded.
struct SideRow { int group, place; };
static SideRow g_srow[sizeof g_pl / sizeof g_pl[0] + NGROUPS];
static int     g_nsrow;
static void sp_build (void);
static void side_rows (void)
{
	sp_build ();					// (pocket, console: the SidePanel's items again -- the end of this)
	g_nsrow = 0;
	for (int g = 0; g < NGROUPS; g++)
	{
		g_srow[g_nsrow++] = { g, -1 };
		if (g_folded[g]) continue;
		for (int i = 0; i < g_npl; i++) if (g_pl[i].group == g) g_srow[g_nsrow++] = { g, i };
	}
}

// The place the folder shown is in (the longest path it starts with), -1 none.
static int current_place (void)
{
	if (in_trash ()) { for (int i = 0; i < g_npl; i++) if (g_pl[i].kind == PL_TRASH) return i; return -1; }
	const char *cur = g_col[g_active].path;
	int best = -1, bestLen = -1;
	for (int i = 0; i < g_npl; i++)
	{
		if (g_pl[i].kind == PL_TRASH || g_pl[i].kind == PL_CONNECT) continue;
		const char *pp = g_pl[i].path;
		int n = slen (pp);
		while (n > 0 && pp[n - 1] == '/') n--;
		bool pre = true;
		for (int k = 0; k < n && pre; k++) pre = lower (pp[k]) == lower (cur[k]);
		if (pre && (cur[n] == 0 || cur[n] == '/') && n > bestLen) { best = i; bestLen = n; }
	}
	return best;
}

static void connect_place (const char *addr);
static void op_connect ();

static void open_place (int i)
{
	if (i < 0 || i >= g_npl) return;
	const Place &p = g_pl[i];
	switch (p.kind)
	{
	case PL_PIN:
		if (!is_remote (p.path) && !fs_is_dir (p.path)) { status (TR ("This folder is not there any more: "), p.path); return; }
		open_path (p.path); break;
	case PL_TRASH:   op_open_trash (); break;
	case PL_VOL:     show_volume (p.path); break;
	case PL_NET:     connect_place (p.path); break;
	case PL_CONNECT: op_connect (); break;
	}
}

// Pin a folder to the Personal group, under a name asked for.
static bool ask_name (const char *title, const char *init, char *out, int cap);
static void pin_folder (const char *path)
{
	if (g_npin >= MAXPIN) { status (TR ("16 pinned folders at most")); return; }
	for (int i = 0; i < g_npin; i++) if (ci_cmp (g_pinPath[i], path) == 0) { status (TR ("Already pinned: "), g_pinName[i]); return; }
	const char *base = fs_basename (path);
	char name[40];
	if (!ask_name (TR ("Pin to the sidebar as"), base[0] ? base : path, name, sizeof name)) return;
	scopy (g_pinName[g_npin], name, 40); scopy (g_pinPath[g_npin], path, 200); g_npin++;
	places_save (); places_build (); side_rows ();
	status (TR ("Pinned: "), name);
}

// A network place: add it (or rename it if its server is there already).
static void net_place (const char *name, const char *addr)
{
	for (int i = 0; i < g_nnet; i++)
		if (ci_cmp (g_netPath[i], addr) == 0) { scopy (g_netName[i], name, 40); places_save (); places_build (); side_rows (); return; }
	if (g_nnet >= MAXPIN) return;
	scopy (g_netName[g_nnet], name, 40); scopy (g_netPath[g_nnet], addr, 200); g_nnet++;
	places_save (); places_build (); side_rows ();
}

// Go > Connect to Server...: protocol (FTP / FTPS), server, port, user, password and
// folder. The login goes to /bin/ftpfs over IPC ("ftpfs" service, like `ftpfs login`) --
// never into the path, which the path bar may show or store -- then the
// folder opens as FTP[S]:host[:port]/folder (ABI v44 file-system provider), and the server
// joins the sidebar's Network group under the name given (a click there connects again).
static void connect_picked (Widget &w);
class ConnectDialog : public Modal
{
public:
	RadioButton *ftp, *ftps;
	Combobox *host;
	Textbox *port, *user, *pass, *folder, *name;
	Checkbox *remember;
	Button *forget;
	ftpfs_site sites[FTPFS_MAXSITES]; int nsites;
	char hint[96];
	ConnectDialog () : Modal (400, 346)
	{
		Root *r = Root::current ();
		left = ((r ? r->width : W) - width) / 2; top = ((r ? r->height : H) - height) / 2;
		int y = uk_fh () + 18, lx = 12, fx = 100, rh = 32;
		const char *labels[6] = { TRN ("Protocol"), TRN ("Server"), TRN ("User"), TRN ("Password"), TRN ("Folder"), TRN ("Name") };
		for (int i = 0; i < 6; i++) addChild (new Label (lx, y + i * rh + 4, 86, 20, TR (labels[i]), C_TEXT, C_FACE));
		ftp  = new RadioButton (fx,      y, 80, 24, "FTP",  1, true,  0, C_FACE); addChild (ftp);
		ftps = new RadioButton (fx + 90, y, 180, 24, "FTPS (TLS)", 1, false, 0, C_FACE); addChild (ftps);
		host   = new Combobox (fx, y + rh, 190, 26, "", 0, connect_picked);
		addChild (new Label (fx + 198, y + rh + 4, 36, 20, TR ("Port"), C_TEXT, C_FACE));
		port   = new Textbox (fx + 238, y + rh, width - fx - 250, 26, "21");
		user   = new Textbox (fx, y + 2 * rh, width - fx - 12, 26, "");
		pass   = new Textbox (fx, y + 3 * rh, width - fx - 12, 26, "", dlg_enter);
		pass->password = true;
		folder = new Textbox (fx, y + 4 * rh, width - fx - 12, 26, "/", dlg_enter);
		name   = new Textbox (fx, y + 5 * rh, width - fx - 12, 26, "", dlg_enter);
		name->tip = TR ("The server's name in the sidebar (Network): empty = its address");
		addChild (port); addChild (user); addChild (pass); addChild (folder); addChild (name);
		remember = new Checkbox (fx, y + 6 * rh + 18, width - fx - 12, 24, TR ("Remember password"), true, 0, C_FACE);
		remember->tip = TR ("Kept in SD:/etc/ftpfs.ini (obfuscated, not encrypted) for next boots");
		addChild (remember);
		host->tip = TR ("A name (ftp.example.com) or an IP address; the arrow lists the remembered servers");
		user->tip = TR ("Empty = anonymous");
		ftps->tip = TR ("Explicit TLS on port 21 (AUTH TLS); implicit TLS on port 990");
		Button *b;
		forget = new Button (12, height - 38, 98, 28, TR ("Forget"), dlg_btn); forget->tag = 2;
		forget->tip = TR ("Forget the remembered login of this server");
		addChild (forget);
		b = new Button (width - 196, height - 38, 98, 28, TR ("Connect"), dlg_btn); b->tag = 1; addChild (b);
		b = new Button (width - 92,  height - 38, 82, 28, TR ("Cancel"),  dlg_btn); b->tag = 0; addChild (b);
		addChild (host);					// last: its list opens over the fields below
		reload ();
		host->setFocus ();
	}
	void reload ()
	{
		nsites = ftpfs_load_sites (sites, FTPFS_MAXSITES);
		host->clearOptions ();
		for (int i = 0; i < nsites; i++) host->addOption (sites[i].host);
		forget->disabled = nsites == 0; forget->invalidate (true);
		set_hint ();
	}
	void set_hint ()					// under the fields: what the server list holds
	{
		if (nsites == 0) { scopy (hint, TR ("No remembered server yet (SD:/etc/ftpfs.ini)"), sizeof hint); return; }
		char nb[8]; int k = 0, v = nsites; char t[8]; int m = 0;
		while (v) { t[m++] = (char) ('0' + v % 10); v /= 10; } while (m) nb[k++] = t[--m]; nb[k] = '\0';
		scopy (hint, nb, sizeof hint);
		int n = slen (hint);
		scopy (hint + n, nsites == 1 ? TR (" remembered server: click the arrow or press Down")
					    : TR (" remembered servers: click the arrow or press Down"), sizeof hint - n);
	}
	int saved (const char *h)
	{
		for (int i = 0; i < nsites; i++) if (ci_cmp (sites[i].host, h) == 0) return i;
		return -1;
	}
	void fill (int i)					// a remembered server: pre-fill the form
	{
		if (i < 0 || i >= nsites) return;
		const ftpfs_site &st = sites[i];
		if (st.tls) ftps->select (); else ftp->select ();
		port->setText (st.port[0] ? st.port : "21");
		user->setText (st.user); pass->setText (st.pass);
		folder->setText (st.folder[0] ? st.folder : "/");
		remember->checked = true; remember->invalidate (true);
		invalidate (true);
	}
	void onButton (int tag) override
	{
		if (tag != 2) { close (tag); return; }
		int i = saved (host->text);
		if (i < 0) { uk_messagebox (TR ("Forget"), TR ("This server is not remembered."), MB_OK); return; }
		if (!uk_messagebox (TR ("Forget"), TR ("Forget the remembered login of this server?"), MB_YESNO)) return;
		ftpfs_forget (sites[i].host);
		sites[i] = sites[--nsites];				// (ftpfs rewrites the file: update the list here)
		host->clearOptions ();
		for (int k = 0; k < nsites; k++) host->addOption (sites[k].host);
		forget->disabled = nsites == 0; forget->invalidate (true);
		set_hint ();
		pass->setText ("");
		invalidate (true);
	}
	bool onKey (long k) override
	{
		if (k == 27) { close (0); return true; }
		if (k == '\t')						// Tab: next field
		{
			Textbox *order[6] = { host, port, user, pass, folder, name };
			int cur = -1;
			for (int i = 0; i < 6; i++) if (order[i]->hasFocus) cur = i;
			order[(cur + 1) % 6]->setFocus ();
			invalidate (true);
			return true;
		}
		return false;
	}
	void onDraw () override
	{
		drawBox (TR ("Connect to Server"));
		canvas.text (100, uk_fh () + 18 + 6 * 32 - 2, hint, C_DIS);
	}
};
static void connect_picked (Widget &w)
{
	Combobox &cb = (Combobox &) w;
	((ConnectDialog *) w.parent)->fill (cb.picked);
}

static char s_lastHost[64] = "", s_lastUser[64] = "", s_lastPort[8] = "21", s_lastFolder[64] = "/", s_lastName[40] = "";
static bool s_lastTls = false;
static void op_connect ()
{
	char *lastHost = s_lastHost, *lastUser = s_lastUser, *lastPort = s_lastPort, *lastFolder = s_lastFolder;
	bool &lastTls = s_lastTls;
	ConnectDialog dlg;
	dlg.host->setText (lastHost); dlg.user->setText (lastUser); dlg.port->setText (lastPort);
	dlg.folder->setText (lastFolder); dlg.name->setText (s_lastName);
	if (lastTls) dlg.ftps->select (); else dlg.ftp->select ();
	if (lastHost[0] == '\0' && dlg.nsites > 0) dlg.host->pick (0);	// first time: the first remembered server
	else { int i = dlg.saved (lastHost); if (i >= 0) dlg.pass->setText (dlg.sites[i].pass); }
	dlg.host->setFocus ();
	if (!dlg.run () || dlg.host->text[0] == '\0') return;
	scopy (lastHost, dlg.host->text, sizeof s_lastHost); scopy (lastUser, dlg.user->text, sizeof s_lastUser);
	scopy (lastPort, dlg.port->text, sizeof s_lastPort); scopy (lastFolder, dlg.folder->text, sizeof s_lastFolder);
	scopy (s_lastName, dlg.name->text, sizeof s_lastName);
	lastTls = dlg.ftps->checked;

	// Hand the login to ftpfs. Remember = saved with the port / FTPS / folder (the list
	// pre-fills the form next time); unchecked on a remembered server = forget it.
	ftpfs_site st;
	scopy (st.host, dlg.host->text, sizeof st.host); scopy (st.user, dlg.user->text, sizeof st.user);
	scopy (st.pass, dlg.pass->text, sizeof st.pass); scopy (st.port, lastPort, sizeof st.port);
	scopy (st.folder, lastFolder, sizeof st.folder); st.tls = lastTls ? 1 : 0;
	bool remember = dlg.remember->checked, wasSaved = dlg.saved (st.host) >= 0;
	if (!remember && wasSaved) ftpfs_forget (st.host);
	if ((remember || st.user[0]) && !ftpfs_login_site (&st, remember ? 1 : 0))
	{ status (TR ("Cannot start /bin/ftpfs")); return; }
	char addr[200];
	scopy (addr, lastTls ? "FTPS:" : "FTP:", sizeof addr);
	int n = slen (addr);
	for (int i = 0; dlg.host->text[i] && n < 190; i++) addr[n++] = dlg.host->text[i];
	if (lastPort[0] && !(lastPort[0] == '2' && lastPort[1] == '1' && !lastPort[2]))
	{ addr[n++] = ':'; for (int i = 0; lastPort[i] && n < 196; i++) addr[n++] = lastPort[i]; }
	if (lastFolder[0] != '/') addr[n++] = '/';
	for (int i = 0; lastFolder[i] && n < 198; i++) addr[n++] = lastFolder[i];
	addr[n] = '\0';
	status (TR ("Connecting to "), addr);
	if (g_root) { g_root->draw (); uk_win_present (); }
	void *d = kapi_opendir (addr);
	if (d == 0) { status (TR ("Cannot connect to "), addr); notify (TR ("File Viewer"), TR ("Connection failed (address, login or network?).")); return; }
	kapi_closedir (d);
	show_root (addr);
	net_place (s_lastName[0] ? s_lastName : dlg.host->text, addr);		// (the sidebar's Network)
	status (TR ("Connected: "), addr);
}

// A network place clicked: the saved login of its server handed to ftpfs again (ftpfs.ini), then
// its folder opened -- or, no answer, the Connect dialog filled with it.
static void connect_place (const char *addr)
{
	bool tls = lower (addr[3]) == 's';
	const char *h = addr; while (*h && *h != ':') h++;
	if (*h) h++;
	char host[128], port[8] = ""; int n = 0;
	while (h[n] && h[n] != ':' && h[n] != '/' && n < 127) { host[n] = h[n]; n++; }
	host[n] = 0;
	const char *q = h + n;
	if (*q == ':') { q++; int k = 0; while (*q >= '0' && *q <= '9' && k < 7) port[k++] = *q++; port[k] = 0; }
	ftpfs_site sites[FTPFS_MAXSITES];
	int ns = ftpfs_load_sites (sites, FTPFS_MAXSITES), found = -1;
	for (int i = 0; i < ns && found < 0; i++)
		if (ci_cmp (sites[i].host, host) == 0 && (!port[0] || ci_cmp (sites[i].port, port) == 0)) found = i;
	if (found >= 0) ftpfs_login_site (&sites[found], 0);
	else ftpfs__pid ();					// (anonymous: ftpfs running is enough)
	status (TR ("Connecting to "), addr);
	if (g_root) { g_root->draw (); uk_win_present (); }
	void *d = kapi_opendir (addr);
	if (d != 0) { kapi_closedir (d); show_root (addr); status (TR ("Connected: "), addr); return; }
	status (TR ("Cannot connect to "), addr);
	scopy (s_lastHost, host, sizeof s_lastHost); scopy (s_lastPort, port[0] ? port : "21", sizeof s_lastPort);
	s_lastTls = tls;
	const char *fo = q; scopy (s_lastFolder, *fo ? fo : "/", sizeof s_lastFolder);
	for (int i = 0; i < g_nnet; i++) if (ci_cmp (g_netPath[i], addr) == 0) scopy (s_lastName, g_netName[i], sizeof s_lastName);
	if (found >= 0) scopy (s_lastUser, sites[found].user, sizeof s_lastUser);
	op_connect ();
}
static void op_restore ()
{
	const Entry *e = sel_entry (0);
	if (!in_trash () || !e || g_active != 0) { status (TR ("Select an item in the Trash to restore")); return; }
	char name[NAMEL]; scopy (name, e->name, sizeof name);
	char where[300];
	bool ok = trash_restore (name, where, sizeof where);
	g_col[0].sel = -1; g_ncol = 1;
	refresh ();
	status (ok ? TR ("Restored to ") : TR ("Could not restore "), ok ? where : name);
	if (ok) notify (TR ("Trash"), where);
}
static void op_empty_trash ()
{
	int n = trash_count ();
	if (n == 0) { status (TR ("The Trash is empty")); return; }
	if (!uk_messagebox (TR ("Empty Trash"), TR ("Delete everything in the Trash for good?"), MB_YESNO)) return;
	trash_empty ();
	if (in_trash ()) { g_col[0].sel = -1; g_ncol = 1; refresh (); }
	status (TR ("The Trash was emptied"));
	notify (TR ("Trash"), TR ("The Trash was emptied."));
}
// Copy / Cut put the selected path on the SYSTEM clipboard (shared with every app and
// every File Viewer window); Paste reads it back.
static void clip_set (bool cut)
{
	const Entry *e = sel_entry (g_active);
	if (!e) { status (TR ("Select something to "), cut ? TR ("cut") : TR ("copy")); return; }
	char p[300];
	join (p, sizeof p, g_col[g_active].path, e->name);
	clip_set_files (p, cut ? 1 : 0);
	status (cut ? TR ("Cut: ") : TR ("Copied: "), e->name);
}
// Move (or copy) src into folder dir under a unique name. false + a status message if it
// cannot (missing, a folder into itself). Used by Paste and by drag & drop.
static bool transfer (const char *src, const char *dir, bool move)
{
	if (!exists (src)) { status (TR ("No longer exists: "), src); return false; }
	bool isDir = fs_is_dir (src);
	char parent[300]; fs_dirname (parent, sizeof parent, src);
	if (move && ci_cmp (parent, dir) == 0) return true;		// already there
	int n = slen (src);
	if (isDir && ci_cmp (dir, src) == 0) { status (TR ("Cannot put a folder into itself")); return false; }
	bool inside = true; for (int i = 0; i < n; i++) if (lower (dir[i]) != lower (src[i])) { inside = false; break; }
	if (isDir && inside && dir[n] == '/') { status (TR ("Cannot put a folder inside itself")); return false; }
	char dst[300];
	unique_name (dst, sizeof dst, dir, fs_basename (src));
	if (move)
	{
		if (kapi_rename (src, dst) == 0)		// (kapi: 0 = ok)
		{
			return true;
		}
		if (same_volume (src, dir)) return false;		// same volume: a real failure
		// Across volumes (SD <-> SD1: <-> FTP), rename cannot work: copy, then delete the source.
		if (!(isDir ? copy_tree (src, dst, 0) : copy_file (src, dst))) return false;
		if (isDir) remove_tree (src, 0); else kapi_remove (src);
		return true;
	}
	return isDir ? copy_tree (src, dst, 0) : copy_file (src, dst);
}

static void op_copy () { clip_set (false); }
static void op_cut ()  { clip_set (true); }
static void op_paste ()
{
	char g_clip[256]; int cutFlag = 0;
	if (!clip_get_file (g_clip, sizeof g_clip, &cutFlag)) { status (TR ("Nothing to paste")); return; }
	bool g_clipCut = cutFlag != 0;
	const char *dir = g_col[g_active].path;
	char before[160]; scopy (before, g_status, sizeof before);
	bool ok = transfer (g_clip, dir, g_clipCut);
	if (ok && g_clipCut) clip_clear ();
	refresh ();
	if (ok) status (TR ("Pasted into "), dir);
	else if (ci_cmp (before, g_status) == 0) status (TR ("Paste failed into "), dir);
	notify (TR ("File Viewer"), ok ? (g_clipCut ? TR ("Item moved.") : TR ("Item copied.")) : TR ("Paste failed."));
}
static void op_refresh () { refresh (); status (TR ("Refreshed")); }
static void op_open () { open_entry (g_active); }

static void on_hscroll (Widget &w)
{
	g_first = ((Scrollbar &) w).value;
	if (g_root) g_root->invalidate (true);
}

// ---- the window --------------------------------------------------------------------------
static int g_crumbX[MAXCOL + 1];		// path-bar segment right edges (hit-test)
static int g_vdrag = -1;			// column whose scrollbar is being dragged

// Drag & drop (ABI v42). Source: press on a row, move > DRAG_START px -> uk_win_drag_begin
// with its path. Target: the folder under the cursor -- a plain-folder row, else the
// column's own folder -- gets the dropped paths (move; Ctrl = copy; in the Trash view:
// move to the Trash). g_dropSlot / g_dropRow = the highlighted target (-1 = none).
#define DRAG_START	6
static int g_armSlot = -1, g_armRow = -1, g_armX = 0, g_armY = 0;
static bool g_dragging = false;
static int g_dropSlot = -1, g_dropRow = -1;
static int g_dropSide = -1;			// ... in the sidebar (a g_srow index)

// The drop target folder at (mx,my): its path in out, the slot / row to highlight.
static bool drop_target_at (int mx, int my, char *out, int cap, int *pSlot, int *pRow)
{
	*pSlot = -1; *pRow = -1;
	if (my < COL_Y || my >= COL_Y + COL_H || mx < COLX || mx >= W) return false;
	int slot = g_first + (mx - COLX) / COLW;
	if (slot >= g_ncol) slot = g_ncol - 1;		// the preview / empty slots: deepest folder
	if (slot < 0) return false;
	const Column &k = g_col[slot];
	int row = my >= COL_Y + ROW_PAD ? k.top + (my - COL_Y - ROW_PAD) / g_rowH : k.count;
	if (slot == g_first + (mx - COLX) / COLW && row < k.count && k.e[row].isdir && !k.e[row].isapp)
	{
		join (out, cap, k.path, k.e[row].name);	// onto a folder row: into that folder
		*pSlot = slot; *pRow = row;
		return true;
	}
	scopy (out, k.path, cap);			// elsewhere in the column: its folder
	*pSlot = slot;
	return true;
}

// Each column has its own vertical scrollbar (UK_SBW px, at its right edge) when its
// folder has more entries than fit: drag the thumb, or click the track to jump there.
static bool col_overflows (int slot) { return slot >= 0 && slot < g_ncol && g_col[slot].count > g_rows; }
static void vscroll_to (int slot, int my)
{
	Column &k = g_col[slot];
	UkThumb t = uk_thumb (k.count, g_rows, k.top, COL_H);
	k.top = (int) uk_thumb_pos (my - COL_Y, COL_H, k.count, g_rows, t.h);
	int maxTop = k.count - g_rows; if (maxTop < 0) maxTop = 0;
	if (k.top > maxTop) k.top = maxTop;
	if (k.top < 0) k.top = 0;
}
static unsigned g_lastTick = 0; static int g_lastSlot = -1, g_lastRow = -1;

// (P6) In pocket and console the places are a navigation SidePanel (uikit/sidepanel.h: whole in landscape, a drawer in
// portrait, the d-pad's column in console); 0 on the desktop -- its own sidebar below, as always.
static SidePanel *g_sp;

// The sidebar's row at (mx, my) (an index in g_srow), -1 none.
#define SIDE_RH	(g_fh + 10)
#define SIDE_Y	(BC_H + 6)
static int side_at (int mx, int my)
{
	if (g_sp || mx < 0 || mx >= SIDE_W - 4 || my < SIDE_Y || my >= H - ST_H) return -1;	// (the SidePanel: its own)
	int r = (my - SIDE_Y) / SIDE_RH;
	return r >= 0 && r < g_nsrow ? r : -1;
}
// The sidebar's place files dropped at (mx, my) go to -- a pinned folder, the Trash, a volume (a
// g_srow index) --, -1 none.
static int side_drop_at (int mx, int my)
{
	int r = side_at (mx, my);
	if (r < 0 || g_srow[r].place < 0) return -1;
	int k = g_pl[g_srow[r].place].kind;
	return k == PL_PIN || k == PL_TRASH || k == PL_VOL ? r : -1;
}
// The place (a g_pl index) files dropped at (mx, my) of the window go to, -1 none.
static int drop_place (int mx, int my)
{
	if (g_sp)
	{
		if (g_sp->hidden || mx < g_sp->left || my < g_sp->top || mx >= g_sp->left + g_sp->width || my >= g_sp->top + g_sp->height) return -1;
		int i = g_sp->itemAt (mx - g_sp->left, my - g_sp->top);
		if (i < 0 || i >= g_npl) return -1;
		int k = g_pl[i].kind;
		return k == PL_PIN || k == PL_TRASH || k == PL_VOL ? i : -1;
	}
	int r = side_drop_at (mx, my);
	return r >= 0 ? g_srow[r].place : -1;
}
// The path bar's segment at (mx, my), -1 none.
static int crumb_at (int mx, int my)
{
	if (my < 7 || my >= BC_H - 7 || mx < 20) return -1;
	for (int c = 0; c < g_ncol; c++) if (mx < g_crumbX[c]) return c;
	return -1;
}
static int g_crumbHot = -1, g_sideHot = -1;

// A place's small icon (16 x 16 at x, y).
static void place_glyph (Canvas &cv, int x, int y, int kind, unsigned ink)
{
	switch (kind)
	{
	case PL_PIN:						// a folder
		uk_rbox (cv, x + 1, y + 3, 7, 4, 1, 0x00D8AA52, 0x00C89A48);
		uk_rbox (cv, x + 1, y + 5, 14, 10, 2, 0x00EEC46C, 0x00D8A850);
		uk_rline (cv, x + 1, y + 5, 14, 10, 2, 0x00906A28, 190);
		break;
	case PL_TRASH:						// a can
		uk_rbox (cv, x + 2, y + 2, 12, 2, 1, ink, ink);
		uk_rbox (cv, x + 3, y + 5, 10, 10, 2, uk_mix (ink, 0x00FFFFFF, 60), ink);
		break;
	case PL_VOL:						// a drive, its light
		uk_rbox (cv, x + 1, y + 4, 14, 9, 2, uk_mix (ink, 0x00FFFFFF, 150), uk_mix (ink, 0x00FFFFFF, 90));
		uk_rline (cv, x + 1, y + 4, 14, 9, 2, ink, 170);
		cv.fillRect (x + 11, y + 9, 2, 2, 0x0040C060);
		break;
	case PL_NET:						// a server: two boxes, their lights
		for (int k = 0; k < 2; k++)
		{
			uk_rbox (cv, x + 2, y + 2 + k * 6, 12, 5, 1, uk_mix (ink, 0x00FFFFFF, 120), uk_mix (ink, 0x00FFFFFF, 80));
			cv.fillRect (x + 4, y + 4 + k * 6, 2, 1, 0x0040C060);
		}
		cv.fillRect (x + 7, y + 13, 2, 2, ink);
		break;
	case PL_CONNECT: uk_glyph (cv, WKG_PLUS, x + 8, y + 8, 10, ink); break;
	}
}

static void sp_icon (Canvas &cv, int id, int x, int y, int size, unsigned ink, bool)
{
	if (id >= 0 && id < g_npl) place_glyph (cv, x + (size - 16) / 2, y + (size - 16) / 2, g_pl[id].kind, ink);
}
static void sp_build (void)			// the places as the SidePanel's items: a heading a group (it folds), the places
{
	if (!g_sp) return;
	g_sp->clear ();
	for (int g = 0; g < NGROUPS; g++)
	{
		int h = g_sp->addHeading (TR (GROUP_NAME[g]), UK_SPI_FOLDABLE | (g_folded[g] ? UK_SPI_FOLDED : 0));
		for (int i = 0; i < g_npl; i++) if (g_pl[i].group == g) g_sp->addItem (i, g_pl[i].label, i, 0, h);
	}
	g_sp->select (current_place ());
}

class ViewerRoot : public Root
{
public:
	ViewerRoot () : Root (W, H, TR ("File Viewer")) {}

	// The window's size: the parts placed again (the places' width, the columns that fit, the scroll bar, the rows).
	void relayout ()
	{
		g_W = width; g_H = height;
		if (g_sp)
		{
			g_sp->place (0, BC_H, SIDE0, H - BC_H - ST_H);
			g_sideW = g_sp->reservedWidth ();
		}
		else g_sideW = SIDE0;
		int v = (W - SIDE_W) / 200;
		g_vis = v < 1 ? 1 : v > 6 ? 6 : v;
		g_rows = (COL_H - 2 * ROW_PAD) / g_rowH; if (g_rows < 1) g_rows = 1;
		if (g_hsb) { g_hsb->left = COLX; g_hsb->top = COL_Y + COL_H; g_hsb->resizeTo (W - COLX > 1 ? W - COLX : 1, SB_H); }
		sync_hsb ();
		invalidate (true);
	}
	void onResized () override { relayout (); }
	void onSizeClass (int) override { relayout (); }

	// The path bar (elementary OS's): an entry-like field across the window, the path's folders
	// in it as links -- the one shown in the accent, underlined; the one pointed at underlined.
	void drawPathBar ()
	{
		canvas.fillRect (0, 0, W, BC_H, C_BG);
		canvas.fillRect (0, BC_H - 1, W, 1, uk_tone (C_BG, 100));	// the line under the bar (the Media Player's)
		int fx = 10, fy = 7, fw = W - 20, fh = BC_H - 14;
		unsigned field = uk_mix (C_BG, C_FIELD, 170), ink = uk_ink_on (field), dim = uk_mix (field, ink, 120);
		uk_rbox (canvas, fx, fy, fw, fh, 8, uk_tone (field, 136), field);
		uk_rline (canvas, fx, fy, fw, fh, 8, uk_tone (C_BG, 88), 190);
		int x = fx + 14, y = fy + (fh - g_fh) / 2;
		for (int c = 0; c < g_ncol; c++)
		{
			const char *seg;
			char buf[NAMEL];
			if (c == 0)
			{
				if (in_trash ()) seg = TR ("Trash");
				else if (!is_remote (g_col[0].path))
				{
					volume_of (g_col[0].path, buf, sizeof buf);
					for (int i = 0; i < g_npl; i++)			// (a volume: its place's name)
						if (g_pl[i].kind == PL_VOL && ci_cmp (g_pl[i].path, g_col[0].path) == 0) scopy (buf, g_pl[i].label, sizeof buf);
					seg = buf;
				}
				else					// "FTP:host/dir", without user:password@
				{
					const char *r = g_col[0].path, *colon = r;
					while (*colon && *colon != ':') colon++;
					const char *at = 0; for (const char *q = colon; *q && *q != '/'; q++) if (*q == '@') at = q;
					int n = 0;
					for (const char *q = r; q <= colon && *q && n < NAMEL - 1; q++) buf[n++] = *q;
					for (const char *q = at ? at + 1 : colon + 1; *q && n < NAMEL - 1; q++) buf[n++] = *q;
					while (n > 1 && buf[n - 1] == '/') n--;
					buf[n] = '\0';
					for (int i = 0; i < g_npl; i++)			// (a server of the sidebar: its name)
						if (g_pl[i].kind == PL_NET && ci_cmp (g_pl[i].path, g_col[0].path) == 0) { scopy (buf, g_pl[i].label, sizeof buf); break; }
					seg = buf;
				}
			}
			else { const Entry &e = g_col[c - 1].e[g_col[c - 1].sel]; scopy (buf, e.name, sizeof buf); seg = buf; }
			if (c > 0) { uk_glyph (canvas, WKG_CHEV_RIGHT, x + 3, fy + fh / 2, 9, dim); x += 18; }
			bool cur = c == g_active, hot = c == g_crumbHot;
			int tw = uk_text_w (seg, cur ? 2 : 0);
			if (x + tw > fx + fw - 20) { for (int k = c; k < g_ncol; k++) g_crumbX[k] = fx + fw; break; }
			uk_text (canvas, x, y, seg, cur ? uk_tone (C_ACCENT, 84) : ink, cur ? 2 : 0);
			if (cur) canvas.fillRect (x, y + g_fh + 1, tw, 2, C_ACCENT);
			else if (hot) canvas.fillRect (x, y + g_fh + 1, tw, 1, ink);
			x += tw + 10;
			g_crumbX[c] = x;
		}
	}

	// The places: the window's face, three groups (their titles fold them), the place of the
	// folder shown lit.
	void drawSidebar ()
	{
		if (g_sp) { g_sp->select (current_place ()); return; }	// (pocket, console: the SidePanel draws itself)
		canvas.fillRect (0, BC_H, SIDE_W, H - BC_H - ST_H, C_BG);
		uk_etch_v (canvas, SIDE_W - 2, BC_H + 4, H - BC_H - ST_H - 8, C_BG);
		int cur = current_place ();
		unsigned dim = uk_mix (C_BG, C_TEXT, 150);
		for (int r = 0; r < g_nsrow; r++)
		{
			int y = SIDE_Y + r * SIDE_RH;
			if (y + SIDE_RH > H - ST_H) break;
			const SideRow &sr = g_srow[r];
			bool hot = r == g_sideHot;
			if (sr.place < 0)
			{
				uk_glyph (canvas, g_folded[sr.group] ? WKG_CHEV_RIGHT : WKG_CHEV_DOWN, 14, y + SIDE_RH / 2, 8, hot ? C_TEXT : dim);
				uk_text (canvas, 24, y + (SIDE_RH - g_fh) / 2, TR (GROUP_NAME[sr.group]), hot ? C_TEXT : dim, 2);
				continue;
			}
			const Place &p = g_pl[sr.place];
			bool on = sr.place == cur;
			if (on) uk_hilite (canvas, 8, y + 1, SIDE_W - 20, SIDE_RH - 2, 6, true);
			else if (hot) uk_rbox (canvas, 8, y + 1, SIDE_W - 20, SIDE_RH - 2, 6, uk_tone (C_BG, 160), uk_tone (C_BG, 148));
			unsigned ink = on ? C_SEL_TEXT : C_TEXT;
			place_glyph (canvas, 18, y + (SIDE_RH - 16) / 2, p.kind, ink);
			char lab[64]; uk_text_fit (p.label, SIDE_W - 48, lab, sizeof lab);
			canvas.text (40, y + (SIDE_RH - g_fh) / 2, lab, p.kind == PL_CONNECT && !on ? dim : ink);
			if (r == g_dropSide) uk_rline (canvas, 8, y + 1, SIDE_W - 20, SIDE_RH - 2, 6, C_ACCENT);	// (a drop target)
		}
	}

	void drawColumn (int slot, int x)
	{
		const Column &k = g_col[slot];
		canvas.fillRect (x, COL_Y, COLW, COL_H, C_COL);
		canvas.fillRect (x + COLW - 1, COL_Y, 1, COL_H, C_COLSEP);
		UkThumb t = uk_thumb (k.count, g_rows, k.top, COL_H);
		int sbw = t.show ? UK_SBW + 4 : 0;
		int rw = COLW - 10 - sbw;			// a row's highlight (clear of the scroll bar)
		int chevX = x + COLW - 16 - sbw;		// a folder's arrow: well inside, left of the bar
		int maxW = chevX - 8 - (x + TXT_PAD);
		for (int r = 0; r < g_rows; r++)
		{
			int idx = k.top + r;
			if (idx >= k.count) break;
			const Entry &e = k.e[idx];
			int y = COL_Y + ROW_PAD + r * g_rowH;
			bool sel = idx == k.sel, hot = sel && slot == g_active;
			if (sel) uk_hilite (canvas, x + 4, y + 1, rw, g_rowH - 2, 5, hot);
			char name[NAMEL + 8]; uk_text_fit (e.label, maxW, name, sizeof name);
			unsigned col = hot ? C_SEL_TEXT : e.isapp ? C_APPTXT : e.isdir ? C_DIRTXT : C_FILETXT;
			canvas.text (x + TXT_PAD, y + (g_rowH - g_fh) / 2, name, col);
			if (e.isdir && !e.isapp) uk_glyph (canvas, WKG_CHEV_RIGHT, chevX, y + g_rowH / 2, 8, hot ? C_SEL_TEXT : C_DIMTXT);
		}
		if (g_dropSlot == slot)				// drop target highlight
		{
			if (g_dropRow >= k.top && g_dropRow < k.top + g_rows)
				uk_rline (canvas, x + 3, COL_Y + ROW_PAD + (g_dropRow - k.top) * g_rowH, rw + 2, g_rowH, 5, C_ACCENT);
			else if (g_dropRow < 0)
				uk_rline (canvas, x + 1, COL_Y + 1, COLW - 3, COL_H - 2, 4, C_ACCENT);
		}
		if (t.show) uk_draw_vscroll (canvas, x + COLW - 3 - UK_SBW, COL_Y + 2, UK_SBW, COL_H - 4, t, C_COL, g_vdrag == slot);
		if (k.count == 0) canvas.text (x + TXT_PAD, COL_Y + ROW_PAD + (g_rowH - g_fh) / 2, TR ("(empty)"), C_DIMTXT);
	}

	void drawPreview (int x)
	{
		canvas.fillRect (x, COL_Y, COLW, COL_H, C_COL);
		const Entry *e = sel_entry (g_ncol - 1);
		if (!e) return;
		int y = COL_Y + 8, tx = x + 8, maxW = COLW - 16, maxChars = maxW / 4 < 78 ? maxW / 4 : 78;	// (a line: clipped at the column by its width)

		if ((g_pvKind == PV_APP || g_pvKind == PV_IMAGE) && g_pvImg)
		{
			// Scale the image to fit the column width (nearest neighbour), keep the ratio.
			int maxW = COLW - 16, maxH = COL_H / 2;
			int dw = g_pvW, dh = g_pvH;
			if (g_pvKind == PV_APP) { dw = g_pvW * 2; dh = g_pvH * 2; }	// icons are tiny
			if (dw > maxW) { dh = dh * maxW / dw; dw = maxW; }
			if (dh > maxH) { dw = dw * maxH / dh; dh = maxH; }
			if (dw < 1) dw = 1;
			if (dh < 1) dh = 1;
			int ox = x + (COLW - dw) / 2;
			for (int j = 0; j < dh; j++)
				for (int i = 0; i < dw; i++)
				{
					unsigned c = g_pvImg[(j * g_pvH / dh) * g_pvW + (i * g_pvW / dw)];
					if (g_pvKind == PV_APP && (c & 0xFFFFFF) == UK_TRANSPARENT_KEY) continue;
					unsigned a = c >> 24;
					if (g_pvKind == PV_IMAGE && a != 255)		// alpha over the column
					{
						unsigned r = (((c >> 16) & 255) * a + ((C_COL >> 16) & 255) * (255 - a)) / 255;
						unsigned g = (((c >> 8) & 255) * a + ((C_COL >> 8) & 255) * (255 - a)) / 255;
						unsigned b = ((c & 255) * a + (C_COL & 255) * (255 - a)) / 255;
						c = (r << 16) | (g << 8) | b;
					}
					canvas.pixel (ox + i, y + j, c & 0xFFFFFF);
				}
			y += dh + 10;
		}

		char line[160];
		uk_text_fit (g_pvKind == PV_APP ? g_pvTitle : e->name, maxW, line, sizeof line);
		canvas.text (tx, y, line, C_FILETXT); y += g_fh + 6;
		if (g_pvKind == PV_APP)					// the bundle's folder name
		{
			uk_text_fit (e->name, maxW, line, sizeof line);
			canvas.text (tx, y, line, C_DIMTXT); y += g_fh + 2;
		}

		const char *kind =
			g_pvKind == PV_APP     ? TR ("Application") :
			g_pvKind == PV_IMAGE   ? TR ("Image") :
			g_pvKind == PV_PROGRAM ? TR ("Program") :
			g_pvKind == PV_TEXT    ? TR ("Text") :
			g_pvKind == PV_EMPTY   ? TR ("Empty file") : TR ("File");
		if (g_pvKind == PV_IMAGE)				// "PNG image"
		{
			char k[48]; int p = 0;
			for (int i = 0; g_pvFormat[i] && p < 12; i++) k[p++] = g_pvFormat[i];
			const char *t = TR (" image"); for (int i = 0; t[i] && p < 47; i++) k[p++] = t[i];
			k[p] = '\0';
			canvas.text (tx, y, k, C_DIMTXT);
		}
		else canvas.text (tx, y, kind, C_DIMTXT);
		y += g_fh + 2;
		if (g_pvKind != PV_APP) { char sz[24]; fmt_size (sz, e->size); canvas.text (tx, y, sz, C_DIMTXT); y += g_fh + 2; }
		if (g_pvKind == PV_APP || g_pvKind == PV_PROGRAM)
			{ canvas.text (tx, y, TR ("Double-click to run"), C_DIMTXT); y += g_fh + 2; }
		if (g_pvKind == PV_IMAGE && g_pvImg)
		{
			char d[24]; int p = 0; int v[2] = { g_pvW, g_pvH };
			for (int k = 0; k < 2; k++)
			{
				char t[8]; int n = 0, a = v[k]; if (!a) t[n++] = '0'; while (a) { t[n++] = (char) ('0' + a % 10); a /= 10; }
				while (n) d[p++] = t[--n];
				if (k == 0) { d[p++] = ' '; d[p++] = 'x'; d[p++] = ' '; }
			}
			d[p] = '\0';
			canvas.text (tx, y, d, C_DIMTXT); y += g_fh + 2;
		}

		if (g_pvKind == PV_TEXT)
		{
			y += 6;
			uk_sunken (canvas, x + 4, y - 2, COLW - 9, COL_Y + COL_H - y - 2, 4, C_FIELD);
			const char *p = g_pvText;
			while (*p && y + g_fh < COL_Y + COL_H - 4)
			{
				int n = 0;
				while (p[n] && p[n] != '\n' && n < maxChars) { line[n] = p[n] == '\t' ? ' ' : p[n]; if (line[n] == '\r') line[n] = ' '; n++; }
				line[n] = '\0';
				uk_text_clip (canvas, tx, y, line, C_FILETXT, 0, tx, y, maxW, g_fh);	// (proportional: clipped at the column)
				y += g_fh;
				p += n;
				while (*p && *p != '\n' && n >= maxChars) p++;	// clip long lines
				if (*p == '\n') p++;
			}
		}
	}

	void onDraw () override
	{
		sync_hsb ();
		canvas.clear (C_BG);
		drawPathBar ();
		drawSidebar ();
		for (int s = 0; s < VIS; s++)
		{
			int slot = g_first + s, x = COLX + s * COLW;
			if (slot < g_ncol) drawColumn (slot, x);
			else if (slot == g_ncol && preview_on ()) drawPreview (x);
			else { canvas.fillRect (x, COL_Y, COLW, COL_H, C_COL); canvas.fillRect (x + COLW - 1, COL_Y, 1, COL_H, C_COLSEP); }
		}
		uk_rbox (canvas, 0, H - ST_H, W, ST_H, 0, uk_tone (C_FACE, 160), uk_tone (C_FACE, 124));	// status bar
		uk_etch_h (canvas, 0, H - ST_H, W, C_FACE);
		canvas.text (10, H - ST_H + (ST_H - g_fh) / 2 + 1, g_status, C_TEXT);
	}

	// ---- the right button's menus ---------------------------------------------------------------
	void placeMenu (int r, int mx, int my)
	{
		const SideRow &sr = g_srow[r];
		if (sr.place >= 0) placeMenuOf (sr.place, mx, my);
	}
	void placeMenuOf (int i, int mx, int my)
	{
		if (i < 0 || i >= g_npl) return;
		const Place p = g_pl[i];
		enum { M_OPEN = 1, M_RENAME, M_REMOVE, M_EMPTY, M_EJECT };
		PopupMenu m (mx, my);
		m.add (p.kind == PL_NET ? TR ("Connect") : TR ("Open"), M_OPEN);
		int dev = p.kind == PL_VOL ? usb_dev (p.path) : 0;	// a USB volume: its device ejected from here
		char ej[64];
		if (dev) { eject_label (p.path, ej); m.separator (); m.add (ej, M_EJECT); }
		if (p.kind == PL_PIN || p.kind == PL_NET) { m.add (TR ("Rename..."), M_RENAME); m.add (p.kind == PL_PIN ? TR ("Unpin") : TR ("Forget"), M_REMOVE); }
		if (p.kind == PL_TRASH) { m.separator (); m.add (TR ("Empty Trash..."), M_EMPTY, trash_count () > 0); }
		if (p.kind == PL_CONNECT) return (void) (op_connect ());
		int c = m.run ();
		if (c == M_OPEN) open_place (i);
		else if (c == M_EMPTY) op_empty_trash ();
		else if (c == M_EJECT) { eject_dev (dev); places_build (); side_rows (); }
		else if (c == M_RENAME)
		{
			char name[40];
			if (!ask_name (TR ("Rename the place"), p.label, name, sizeof name)) return;
			if (p.kind == PL_PIN) scopy (g_pinName[p.index], name, 40); else scopy (g_netName[p.index], name, 40);
			places_save (); places_build (); side_rows ();
		}
		else if (c == M_REMOVE)
		{
			if (p.kind == PL_PIN)
			{
				for (int k = p.index; k + 1 < g_npin; k++) { scopy (g_pinName[k], g_pinName[k + 1], 40); scopy (g_pinPath[k], g_pinPath[k + 1], 200); }
				g_npin--;
			}
			else
			{
				for (int k = p.index; k + 1 < g_nnet; k++) { scopy (g_netName[k], g_netName[k + 1], 40); scopy (g_netPath[k], g_netPath[k + 1], 200); }
				g_nnet--;
			}
			places_save (); places_build (); side_rows ();
		}
		invalidate (true);
	}

	// the path bar's first segment, a USB volume: Eject
	void crumbMenu (int mx, int my)
	{
		int dev = usb_dev (g_col[0].path);
		if (!dev) return;
		char ej[64]; eject_label (g_col[0].path, ej);
		PopupMenu m (mx, my);
		m.add (ej, 1);
		if (m.run () == 1) { eject_dev (dev); places_build (); side_rows (); }
		invalidate (true);
	}

	void rowMenu (int slot, int row, int mx, int my)
	{
		if (!(g_col[slot].sel == row && slot + 1 == g_ncol - (g_col[slot].e[row].isdir && !g_col[slot].e[row].isapp ? 1 : 0)))
			select (slot, row);
		g_active = slot;
		invalidate (true); draw (); uk_present ();
		const Entry &e = g_col[slot].e[row];
		bool folder = e.isdir && !e.isapp;
		enum { M_OPEN = 1, M_PIN, M_RENAME, M_TRASH, M_COPY, M_CUT };
		PopupMenu m (mx, my);
		m.add (TR ("Open"), M_OPEN, true, "Enter");
		if (folder) m.add (TR ("Pin to Sidebar..."), M_PIN);
		m.separator ();
		m.add (TR ("Copy"), M_COPY, true, "Ctrl+C");
		m.add (TR ("Cut"), M_CUT, true, "Ctrl+X");
		m.add (TR ("Rename..."), M_RENAME, true, "Ctrl+R");
		m.add (in_trash () ? TR ("Delete Permanently...") : TR ("Move to Trash"), M_TRASH, true, "Del");
		switch (m.run ())
		{
		case M_OPEN:   open_entry (slot); break;
		case M_PIN:    { char p[300]; join (p, sizeof p, g_col[slot].path, e.name); pin_folder (p); break; }
		case M_COPY:   op_copy (); break;
		case M_CUT:    op_cut (); break;
		case M_RENAME: op_rename (); break;
		case M_TRASH:  op_delete (); break;
		}
		invalidate (true);
	}

	bool onMouse (int mx, int my, int bl, int br, int, int wheel) override
	{
		static bool rdown = false;
		if (mx < 0)						// left the window
		{
			pressed = false; g_vdrag = -1; rdown = false;
			if (g_crumbHot >= 0 || g_sideHot >= 0) { g_crumbHot = g_sideHot = -1; invalidate (true); }
			return false;
		}
		if (g_vdrag >= 0)					// dragging a column's scrollbar
		{
			if (!bl) { g_vdrag = -1; pressed = false; }
			else vscroll_to (g_vdrag, my);
			invalidate (true);
			return true;
		}
		int ch = crumb_at (mx, my), sh = side_at (mx, my);
		if (ch != g_crumbHot || sh != g_sideHot) { g_crumbHot = ch; g_sideHot = sh; invalidate (true); }
		if (wheel && my >= COL_Y && my < COL_Y + COL_H && mx >= COLX)
		{
			int slot = g_first + (mx - COLX) / COLW;
			if (slot < g_ncol)
			{
				Column &k = g_col[slot];
				k.top -= wheel;
				int maxTop = k.count - g_rows; if (maxTop < 0) maxTop = 0;
				if (k.top > maxTop) k.top = maxTop;
				if (k.top < 0) k.top = 0;
				invalidate (true);
			}
			return true;
		}
		// the right button: the menu of a place, of an item
		if (br && !rdown && !bl)
		{
			rdown = true;
			if (sh >= 0) { placeMenu (sh, mx, my); return true; }
			if (ch == 0) { crumbMenu (mx, my); return true; }
			if (mx >= COLX && my >= COL_Y + ROW_PAD && my < COL_Y + COL_H)
			{
				int slot = g_first + (mx - COLX) / COLW;
				if (slot < g_ncol)
				{
					int row = g_col[slot].top + (my - COL_Y - ROW_PAD) / g_rowH;
					if (row < g_col[slot].count) rowMenu (slot, row, mx, my);
				}
			}
			return true;
		}
		if (!br) rdown = false;
		if (!bl) { pressed = false; g_armSlot = -1; return true; }
		if (pressed)
		{
			// Button held: past DRAG_START px from the press on a row, start dragging it.
			int dx = mx - g_armX, dy = my - g_armY;
			if (g_armSlot >= 0 && !g_dragging && dx * dx + dy * dy > DRAG_START * DRAG_START
			    && g_armSlot < g_ncol && g_armRow < g_col[g_armSlot].count)
			{
				const Entry &e = g_col[g_armSlot].e[g_armRow];
				char path[300]; join (path, sizeof path, g_col[g_armSlot].path, e.name);
				if (uk_win_drag_begin (DND_FILES, path, (unsigned) slen (path) + 1, e.label))
					g_dragging = true;
				g_armSlot = -1;
			}
			return true;
		}
		pressed = true;

		if (my < BC_H)						// the path bar
		{
			if (ch >= 0) jump_to (ch);
			invalidate (true);
			return true;
		}
		if (mx < COLX)						// the places
		{
			if (sh >= 0)
			{
				const SideRow &sr = g_srow[sh];
				if (sr.place < 0) { g_folded[sr.group] = !g_folded[sr.group]; places_save (); side_rows (); }
				else open_place (sr.place);
			}
			invalidate (true);
			return true;
		}
		if (my < COL_Y || my >= COL_Y + COL_H) return true;

		int slot = g_first + (mx - COLX) / COLW;
		unsigned now = kapi_get_ticks ();
		if (slot == g_ncol && preview_on ())		// preview: double-click opens
		{
			if (g_lastSlot == slot && now - g_lastTick < CLICK_DELAY) { open_entry (g_ncol - 1); g_lastSlot = -1; }
			else { g_lastSlot = slot; g_lastTick = now; }
			invalidate (true);
			return true;
		}
		if (slot >= g_ncol) return true;
		if (col_overflows (slot) && (mx - COLX) % COLW >= COLW - 4 - UK_SBW)	// the column's scrollbar
		{
			g_vdrag = slot; vscroll_to (slot, my);
			invalidate (true);
			return true;
		}
		int row = my >= COL_Y + ROW_PAD ? g_col[slot].top + (my - COL_Y - ROW_PAD) / g_rowH : g_col[slot].count;
		if (row >= g_col[slot].count) { jump_to (slot); invalidate (true); return true; }

		g_armSlot = slot; g_armRow = row; g_armX = mx; g_armY = my;	// a drag may start here
		bool dbl = slot == g_lastSlot && row == g_lastRow && now - g_lastTick < CLICK_DELAY;
		if (!(g_col[slot].sel == row && slot + 1 == g_ncol - (g_col[slot].e[row].isdir && !g_col[slot].e[row].isapp ? 1 : 0)))
			select (slot, row);
		else { g_active = slot; update_status (); }
		if (dbl) { open_entry (slot); g_lastSlot = -1; }
		else { g_lastSlot = slot; g_lastRow = row; g_lastTick = now; }
		invalidate (true);
		return true;
	}

	void onDragOver (int x, int y, bool leave, unsigned) override
	{
		int s = -1, r = -1; char dir[300];
		if (!leave) drop_target_at (x, y, dir, sizeof dir, &s, &r);
		if (s != g_dropSlot || r != g_dropRow) { g_dropSlot = s; g_dropRow = r; invalidate (true); }
		int sd = leave ? -1 : side_drop_at (x, y);
		if (sd != g_dropSide) { g_dropSide = sd; invalidate (true); }
	}

	void onDrop (int x, int y, int type, const char *data, int, unsigned flags) override
	{
		g_dropSlot = g_dropRow = -1;
		int sd = drop_place (x, y);			// onto a place of the sidebar
		g_dropSide = -1;
		char dir[300]; int s = -1, r;
		if (sd >= 0) scopy (dir, g_pl[sd].path, sizeof dir);
		if (type != DND_FILES || (sd < 0 && !drop_target_at (x, y, dir, sizeof dir, &s, &r))) { invalidate (true); return; }
		bool copy = (flags & DND_F_COPY) != 0;
		bool toTrash = sd >= 0 ? g_pl[sd].kind == PL_TRASH : in_trash () && s >= 0 && ci_cmp (dir, TRASH_FILES) == 0;
		int done = 0, failed = 0;
		for (const char *p = data; *p; )
		{
			char src[300]; int n = 0;
			while (*p && *p != '\n') { if (n < (int) sizeof src - 1) src[n++] = *p; p++; }
			if (*p == '\n') p++;
			src[n] = '\0';
			if (n == 0) continue;
			bool ok = toTrash ? trash_move (src) : transfer (src, dir, !copy);
			if (ok) done++; else failed++;
		}
		refresh ();
		if (done) status (toTrash ? TR ("Moved to the Trash") : copy ? TR ("Copied into ") : TR ("Moved into "), toTrash ? "" : dir);
		if (failed) notify (TR ("File Viewer"), TR ("Some items could not be moved."));
		invalidate (true);
	}

	void onDragDone (int, unsigned) override
	{
		g_dragging = false;
		refresh ();				// the target may have moved it away
		invalidate (true);
	}

	// (v93) A USB stick plugged in, ejected or pulled out: the places again; a folder shown on a
	// volume that is gone: back to the SD card.
	void onTick () override
	{
		static unsigned last = 0;
		unsigned now = kapi_get_ticks ();
		if (now - last < 100) return;
		last = now;
		vols_read ();
		if (vols_sig () == g_volSig) return;
		places_build (); side_rows ();
		const char *cur = g_col[g_active].path;
		if (is_usb_path (cur) && !volume_mounted (cur)) { show_root ("SD:/"); status (TR ("The USB stick is not there any more")); }
		invalidate (true);
	}

	bool onKey (long key) override
	{
		Column &k = g_col[g_active];
		switch (key)
		{
		case KEY_UP:   if (k.count) select (g_active, k.sel <= 0 ? 0 : k.sel - 1); break;
		case KEY_DOWN: if (k.count) select (g_active, k.sel < 0 ? 0 : (k.sel + 1 < k.count ? k.sel + 1 : k.sel)); break;
		case KEY_PGUP: if (k.count) select (g_active, k.sel - g_rows < 0 ? 0 : k.sel - g_rows); break;
		case KEY_PGDN: if (k.count) select (g_active, k.sel + g_rows >= k.count ? k.count - 1 : k.sel + g_rows); break;
		case KEY_HOME: if (k.count) select (g_active, 0); break;
		case KEY_END:  if (k.count) select (g_active, k.count - 1); break;
		case KEY_RIGHT:
			if (g_active + 1 < g_ncol && g_col[g_active + 1].count > 0) select (g_active + 1, g_col[g_active + 1].sel >= 0 ? g_col[g_active + 1].sel : 0);
			break;
		case KEY_LEFT: case KEY_BACKSPACE: go_back (); break;
		case KEY_ENTER: open_entry (g_active); break;
		default:
			if (key > ' ' && key < 127 && k.count)	// type-ahead: next name starting with it
			{
				char c = lower ((char) key);
				for (int n = 1; n <= k.count; n++)
				{
					int i = ((k.sel < 0 ? -1 : k.sel) + n) % k.count;
					if (lower (k.e[i].label[0]) == c) { select (g_active, i); break; }
				}
				break;
			}
			return false;
		}
		invalidate (true);
		return true;
	}
};

// Go > Pin This Folder...: the folder selected in the active column, else that column's folder.
static void op_pin ()
{
	const Entry *e = sel_entry (g_active);
	char p[300];
	if (e && e->isdir && !e->isapp) join (p, sizeof p, g_col[g_active].path, e->name);
	else scopy (p, g_col[g_active].path, sizeof p);
	if (in_trash ()) { status (TR ("The Trash has its own place")); return; }
	pin_folder (p);
	if (g_root) g_root->invalidate (true);
}

int main (void)
{
	ft_uikit_install ("DejaVu Sans", 13);			// (FreeType's text: uk_fw / uk_fh follow it)
	uk_lang_init ();					// the words in the system's language (the face first: UTF-8)
	g_fw = uk_fw (); g_fh = uk_fh ();
	g_rowH = g_fh + 8;					// (rows with room: a padding above and below)
	g_rows = (COL_H - 2 * ROW_PAD) / g_rowH; if (g_rows < 1) g_rows = 1;

	for (int c = 0; c < MAXCOL; c++) { g_col[c].e = new Entry[MAXE]; g_col[c].count = 0; g_col[c].sel = -1; }
	places_load ();
	places_build ();
	side_rows ();

	ViewerRoot root;
	if (root.canvas.px == 0) return 1;
	g_root = &root;
	root.setResizable (true);
	root.setMinSize (560, 360);
	if (uk_size_class () != UK_SC_REGULAR)			// pocket, console: the places as a SidePanel
	{
		g_sp = new SidePanel (0, BC_H, SIDE0, H - BC_H - ST_H, UK_SP_LEFT, UK_SP_NAVIGATION);
		g_sp->setIconFn (sp_icon);
		g_sp->onSelect = [] (SidePanel &, int id) { open_place (id); if (g_root) g_root->invalidate (true); };
		g_sp->onItemMenu = [] (SidePanel &, int id, int x, int y) { if (g_root) ((ViewerRoot *) g_root)->placeMenuOf (id, x, y); };
		g_sp->onPresentation = [] (SidePanel &, int) { if (g_root) ((ViewerRoot *) g_root)->relayout (); };
		root.addChild (g_sp);
		sp_build ();
	}

	// Commands live in the system menu bar (shortcuts handled by uikit::Menu).
	static Menu menu;
	menu.menu (TR ("File"));
	menu.item (TR ("Open"),       "Enter", 0,             op_open);
	menu.item (TR ("New Folder"), "^N",    UK_CTRL ('N'), op_new_folder);
	menu.item (TR ("Rename..."),  "^R",    UK_CTRL ('R'), op_rename);
	menu.separator ();
	menu.item (TR ("Move to Trash"),       "Del", KEY_DEL, op_delete);
	menu.item (TR ("Delete Permanently..."), "",  0,       op_delete_permanently);
	menu.separator ();
	menu.item (TR ("Refresh"),    "^L",    UK_CTRL ('L'), op_refresh);
	menu.menu (TR ("Go"));
	menu.item (TR ("SD Card"),    "",      0,             op_show_sd);
	// the card's other FAT / exFAT partitions, when there are some
	if (volume_mounted ("SD1:/")) menu.item (TR ("SD1: (partition 2)"), "", 0, op_show_sd1);
	if (volume_mounted ("SD2:/")) menu.item (TR ("SD2: (partition 3)"), "", 0, op_show_sd2);
	if (volume_mounted ("SD3:/")) menu.item (TR ("SD3: (partition 4)"), "", 0, op_show_sd3);
	if (volume_mounted ("RAM:/")) menu.item (TR ("RAM: (memory)"), "", 0, op_show_ram);
	menu.item (TR ("USB Stick"),  "",      0,             op_show_usb);
	menu.item (TR ("Eject USB Stick"), "^E", UK_CTRL ('E'), op_eject);
	menu.item (TR ("Disks (Format...)"), "", 0,           op_format);
	menu.item (TR ("Trash"),      "",      0,             op_open_trash);
	menu.item (TR ("Connect to Server..."), "", 0,       op_connect);
	menu.separator ();
	menu.item (TR ("Pin This Folder..."), "^D", UK_CTRL ('D'), op_pin);
	menu.separator ();
	menu.item (TR ("Restore from Trash"), "", 0,          op_restore);
	menu.item (TR ("Empty Trash..."),     "", 0,          op_empty_trash);
	menu.menu (TR ("Edit"));
	menu.item (TR ("Copy"),       "^C",    UK_CTRL ('C'), op_copy);
	menu.item (TR ("Cut"),        "^X",    UK_CTRL ('X'), op_cut);
	menu.item (TR ("Paste"),      "^V",    UK_CTRL ('V'), op_paste);
	menu.publish ();
	g_hsb = new Scrollbar (COLX, COL_Y + COL_H, W - COLX, SB_H, false, 1, 0, on_hscroll);
	root.addChild (g_hsb);
	root.relayout ();

	// Start at the root; an argument (e.g. `run fileviewer SD:/apps`) opens that path.
	char args[256];
	kapi_get_args (args, sizeof args);
	load_col (0, "SD:/");
	g_ncol = 1; g_active = 0;
	if (ci_cmp (args, TRASH_FILES) == 0 || ci_cmp (args, "trash") == 0)	// (hidden: not walkable)
		op_open_trash ();
	else if (args[0] && is_remote (args))			// FTP:host/path (ftpfs)
		show_root (args);
	else if (sd_volume (args))					// SD:/..., SD1:/...
		open_path (args);
	update_status ();
	root.run ();
	return 0;
}
