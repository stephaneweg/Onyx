//
// fileviewer -- a NeXTSTEP-style column browser (Miller columns) for the SD card.
//
// Each column lists one folder; selecting a folder opens its content in the next column,
// so the whole path stays on screen and going back is one click on an earlier column (or
// on the path bar above them). Selecting a file shows a preview column: size, type, the
// first lines of a text file, a scaled-down BMP, or an app bundle's icon + name.
//
// Mouse: click = select (a folder opens in the next column), double-click = open (a
// file in tinypad / run a program / launch a .app), wheel = scroll the column under the
// cursor, click a path segment = jump back to that folder.
// Keys: Up/Down/PgUp/PgDn/Home/End move, Right enters a folder, Left/Backspace goes back,
// Enter opens, a letter jumps to the next name starting with it, Del deletes,
// Ctrl-C/X/V copy/cut/paste, Ctrl-N new folder, Ctrl-R rename, Ctrl-L refresh.
// These commands are in the system menu bar (wtk::Menu): File (Open, New Folder,
// Rename..., Move to Trash (Del), Delete Permanently..., Refresh), Edit (Copy, Cut,
// Paste -- the system clipboard) and Go (SD Card, Trash, Restore from Trash, Empty
// Trash...). Del moves to SD:/.Trash (trash.h); inside the Trash it deletes for good.
// Hidden entries (names starting with '.') are not listed. Operations act on the
// selection of the active column; Paste and New Folder target the active column's folder.
//
#include "kapi.h"
#include "bmp.hpp"
#include "clipboard.h"
#include "fsutil.h"
#include "trash.h"
#include "notify.h"
#include "fileassoc.h"
#include "shelfmsg.h"
#include "img/imgload.hpp"		// preview: BMP GIF PNG JPEG PCX WebP (codecs in libwtk)
#include "wtk/wtk.h"

using namespace wtk;

#define W	800
#define H	520
#define VIS	4			// columns visible at once
#define COLW	(W / VIS)
#define TB_H	0			// (no toolbar: commands are in the menu bar)
#define BC_H	24			// path bar
#define SB_H	12			// horizontal scrollbar
#define ST_H	20			// status bar
#define COL_Y	(TB_H + BC_H)
#define COL_H	(H - COL_Y - SB_H - ST_H)
#define MAXCOL	16
#define MAXE	256
#define NAMEL	72
#define CLICK_DELAY 70			// double-click window, HZ ticks (~700 ms)

static const unsigned
	C_COL     = 0x00181E26, C_COLSEP  = 0x00303A48, C_SEL_ACT = 0x00355070,
	C_SEL_OLD = 0x003A4452, C_DIRTXT  = 0x0080C8FF, C_FILETXT = 0x00D8D8D8,
	C_APPTXT  = 0x0090F0A0, C_DIMTXT  = 0x008A96A8, C_BAR     = 0x00303D4D,
	C_PATH    = 0x00E0E0E0, C_PATHSEP = 0x00607080;

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
static bool is_remote (const char *path) { return !(lower (path[0]) == 's' && lower (path[1]) == 'd' && path[2] == ':'); }
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
	if (e && !e->isdir) { fmt_size (sz, e->size); status (n, " items   -   "); int q = slen (g_status);
		for (int i = 0; e->name[i] && q < (int) sizeof g_status - 20; i++) g_status[q++] = e->name[i];
		g_status[q++] = ' '; g_status[q++] = '('; for (int i = 0; sz[i]; i++) g_status[q++] = sz[i]; g_status[q++] = ')'; g_status[q] = '\0'; }
	else status (n, " items");
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
		kapi_launch (name);
		status ("Launched ", name);
	}
	else if (e->isdir) { if (c + 1 < g_ncol && g_col[c + 1].count > 0) select (c + 1, 0); }
	else
	{
		// SD:/etc/fileassoc.ini ("ext = app"), else an ELF program runs (fileassoc.h).
		char app[48];
		if (fa_app_for (path, app, sizeof app) && fa_open (path)) status ("Opened in ", app);
		else if (fa_open (path)) status ("Running ", e->name);
		else status ("No application to open ", e->name);
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
		tb = new Textbox (12, wk_fh () + 16, width - 24, 26, init, dlg_enter);
		tb->caret = slen (init);
		tb->hasFocus = true;
		addChild (tb);
		Button *b;
		b = new Button (width - 180, height - 36, 82, 28, "OK", dlg_btn);     b->tag = 1; addChild (b);
		b = new Button (width - 92,  height - 36, 82, 28, "Cancel", dlg_btn); b->tag = 0; addChild (b);
	}
	void onButton (int tag) override { close (tag); }
	bool onKey (long k) override { if (k == 27) { close (0); return true; } return false; }
	void onDraw () override
	{
		canvas.clear (C_FACE_DN);
		canvas.frameRect (0, 0, width, height, C_ACCENT);
		canvas.fillRect (0, 0, width, wk_fh () + 8, C_FACE);
		canvas.text (8, 4, m_title, C_TEXT);
	}
};

static bool ask_name (const char *title, const char *init, char *out, int cap)
{
	InputBox box (title, init);
	if (!box.run () || box.tb->text[0] == '\0') return false;
	for (int i = 0; box.tb->text[i]; i++)
		if (box.tb->text[i] == '/' || box.tb->text[i] == '\\' || box.tb->text[i] == ':') { wk_messagebox ("Invalid name", "A name cannot contain / \\ or :", MB_OK); return false; }
	scopy (out, box.tb->text, cap);
	return true;
}

static void op_new_folder ()
{
	char name[NAMEL];
	if (!ask_name ("New folder in this column", "New Folder", name, sizeof name)) return;
	char p[300]; join (p, sizeof p, g_col[g_active].path, name);
	if (exists (p) || kapi_mkdir (p) != 0) { status ("Could not create folder ", name); return; }
	refresh ();
	for (int i = 0; i < g_col[g_active].count; i++)
		if (ci_cmp (g_col[g_active].e[i].name, name) == 0) { select (g_active, i); break; }
	status ("Created folder ", name);
}
static void op_rename ()
{
	const Entry *e = sel_entry (g_active);
	if (!e) { status ("Select something to rename"); return; }
	char name[NAMEL], old[NAMEL];
	scopy (old, e->name, sizeof old);
	if (!ask_name ("Rename", old, name, sizeof name) || ci_cmp (name, old) == 0) return;
	char s[300], d[300];
	join (s, sizeof s, g_col[g_active].path, old);
	join (d, sizeof d, g_col[g_active].path, name);
	if (exists (d) || kapi_rename (s, d) != 0) { status ("Could not rename to ", name); return; }
	shelf_moved (s, d);
	g_col[g_active].sel = -1; g_ncol = g_active + 1;
	refresh ();
	for (int i = 0; i < g_col[g_active].count; i++)
		if (ci_cmp (g_col[g_active].e[i].name, name) == 0) { select (g_active, i); break; }
	status ("Renamed to ", name);
}
static void op_delete_permanently ();
static void op_delete ()			// Del: move to the trash (in the trash: for good)
{
	const Entry *e = sel_entry (g_active);
	if (!e) { status ("Select something to delete"); return; }
	if (in_trash ()) { op_delete_permanently (); return; }
	char path[300]; join (path, sizeof path, g_col[g_active].path, e->name);
	char name[NAMEL]; scopy (name, e->name, sizeof name);
	bool ok = trash_move (path);
	g_col[g_active].sel = -1; g_ncol = g_active + 1;
	refresh ();
	status (ok ? "Moved to the Trash: " : "Could not move to the Trash: ", name);
}

static void op_delete_permanently ()
{
	const Entry *e = sel_entry (g_active);
	if (!e) { status ("Select something to delete"); return; }
	char msg[128]; int p = 0;
	const char *a = e->isdir ? "Delete the folder for good\n" : "Delete for good\n";
	for (int i = 0; a[i]; i++) msg[p++] = a[i];
	for (int i = 0; e->name[i] && p < 100; i++) msg[p++] = e->name[i];
	if (e->isdir) { const char *b = "\nand everything in it?"; for (int i = 0; b[i]; i++) msg[p++] = b[i]; }
	else msg[p++] = '?';
	msg[p] = '\0';
	if (!wk_messagebox ("Delete", msg, MB_YESNO)) return;
	char path[300]; join (path, sizeof path, g_col[g_active].path, e->name);
	char name[NAMEL]; scopy (name, e->name, sizeof name);
	bool ok;
	if (in_trash () && g_active == 0) ok = trash_purge (name);	// drop its info file too
	else ok = e->isdir ? remove_tree (path, 0) : kapi_remove (path) == 0;
	g_col[g_active].sel = -1; g_ncol = g_active + 1;
	refresh ();
	status (ok ? "Deleted " : "Could not delete ", name);
}

static void show_root (const char *path)
{
	load_col (0, path);
	g_ncol = 1; g_active = 0; g_first = 0;
	preview_build ();
	update_status ();
}
static void op_open_trash () { trash_ensure (); show_root (TRASH_FILES); status ("Trash: Restore puts an item back, Del deletes it for good"); }
static void op_show_sd ()    { show_root ("SD:/"); }

// Go > Connect to Server...: an FTP / FTPS address (served by /bin/ftpfs, ABI v44).
static void op_connect ()
{
	static char last[200] = "FTP:";
	InputBox box ("Server: FTP:host/path or FTPS:...", last);
	if (!box.run () || box.tb->text[0] == '\0') return;
	char addr[200];
	const char *t = box.tb->text;
	bool hasPrefix = false;
	for (int i = 0; t[i] && t[i] != '/'; i++) if (t[i] == ':' && i >= 3) { hasPrefix = true; break; }
	if (!hasPrefix) { scopy (addr, "FTP:", sizeof addr); int n = slen (addr); scopy (addr + n, t, sizeof addr - n); }
	else scopy (addr, t, sizeof addr);
	scopy (last, addr, sizeof last);
	status ("Connecting to ", addr);
	if (g_root) { g_root->draw (); kapi_present (); }
	void *d = kapi_opendir (addr);
	if (d == 0) { status ("Cannot connect to ", addr); notify ("File Viewer", "Connection failed (address, login or network?)."); return; }
	kapi_closedir (d);
	show_root (addr);
	status ("Connected: ", addr);
}
static void op_restore ()
{
	const Entry *e = sel_entry (0);
	if (!in_trash () || !e || g_active != 0) { status ("Select an item in the Trash to restore"); return; }
	char name[NAMEL]; scopy (name, e->name, sizeof name);
	char where[300];
	bool ok = trash_restore (name, where, sizeof where);
	g_col[0].sel = -1; g_ncol = 1;
	refresh ();
	status (ok ? "Restored to " : "Could not restore ", ok ? where : name);
	if (ok) notify ("Trash", where);
}
static void op_empty_trash ()
{
	int n = trash_count ();
	if (n == 0) { status ("The Trash is empty"); return; }
	if (!wk_messagebox ("Empty Trash", "Delete everything in the Trash for good?", MB_YESNO)) return;
	trash_empty ();
	if (in_trash ()) { g_col[0].sel = -1; g_ncol = 1; refresh (); }
	status ("The Trash was emptied");
	notify ("Trash", "The Trash was emptied.");
}
// Copy / Cut put the selected path on the SYSTEM clipboard (shared with every app and
// every File Viewer window); Paste reads it back.
static void clip_set (bool cut)
{
	const Entry *e = sel_entry (g_active);
	if (!e) { status ("Select something to ", cut ? "cut" : "copy"); return; }
	char p[300];
	join (p, sizeof p, g_col[g_active].path, e->name);
	clip_set_files (p, cut ? 1 : 0);
	status (cut ? "Cut: " : "Copied: ", e->name);
}
// Move (or copy) src into folder dir under a unique name. false + a status message if it
// cannot (missing, a folder into itself). Used by Paste and by drag & drop.
static bool transfer (const char *src, const char *dir, bool move)
{
	if (!exists (src)) { status ("No longer exists: ", src); return false; }
	bool isDir = fs_is_dir (src);
	char parent[300]; fs_dirname (parent, sizeof parent, src);
	if (move && ci_cmp (parent, dir) == 0) return true;		// already there
	int n = slen (src);
	if (isDir && ci_cmp (dir, src) == 0) { status ("Cannot put a folder into itself"); return false; }
	bool inside = true; for (int i = 0; i < n; i++) if (lower (dir[i]) != lower (src[i])) { inside = false; break; }
	if (isDir && inside && dir[n] == '/') { status ("Cannot put a folder inside itself"); return false; }
	char dst[300];
	unique_name (dst, sizeof dst, dir, fs_basename (src));
	if (move)
	{
		if (kapi_rename (src, dst) == 0)		// (kapi: 0 = ok)
		{
			shelf_moved (src, dst);			// the Shelf's references follow
			return true;
		}
		if (is_remote (src) == is_remote (dir)) return false;	// same volume: a real failure
		// Across volumes (SD <-> FTP), rename cannot work: copy, then delete the source.
		if (!(isDir ? copy_tree (src, dst, 0) : copy_file (src, dst))) return false;
		if (isDir) remove_tree (src, 0); else kapi_remove (src);
		shelf_moved (src, dst);
		return true;
	}
	return isDir ? copy_tree (src, dst, 0) : copy_file (src, dst);
}

static void op_copy () { clip_set (false); }
static void op_cut ()  { clip_set (true); }
static void op_paste ()
{
	char g_clip[256]; int cutFlag = 0;
	if (!clip_get_file (g_clip, sizeof g_clip, &cutFlag)) { status ("Nothing to paste"); return; }
	bool g_clipCut = cutFlag != 0;
	const char *dir = g_col[g_active].path;
	char before[160]; scopy (before, g_status, sizeof before);
	bool ok = transfer (g_clip, dir, g_clipCut);
	if (ok && g_clipCut) clip_clear ();
	refresh ();
	if (ok) status ("Pasted into ", dir);
	else if (ci_cmp (before, g_status) == 0) status ("Paste failed into ", dir);
	notify ("File Viewer", ok ? (g_clipCut ? "Item moved." : "Item copied.") : "Paste failed.");
}
static void op_refresh () { refresh (); status ("Refreshed"); }
static void op_open () { open_entry (g_active); }

static void on_hscroll (Widget &w)
{
	g_first = ((Scrollbar &) w).value;
	if (g_root) g_root->invalidate (true);
}

// ---- the window --------------------------------------------------------------------------
static int g_crumbX[MAXCOL + 1];		// path-bar segment right edges (hit-test)
static int g_vdrag = -1;			// column whose scrollbar is being dragged

// Drag & drop (ABI v42). Source: press on a row, move > DRAG_START px -> kapi_drag_begin
// with its path. Target: the folder under the cursor -- a plain-folder row, else the
// column's own folder -- gets the dropped paths (move; Ctrl = copy; in the Trash view:
// move to the Trash). g_dropSlot / g_dropRow = the highlighted target (-1 = none).
#define DRAG_START	6
static int g_armSlot = -1, g_armRow = -1, g_armX = 0, g_armY = 0;
static bool g_dragging = false;
static int g_dropSlot = -1, g_dropRow = -1;

// The drop target folder at (mx,my): its path in out, the slot / row to highlight.
static bool drop_target_at (int mx, int my, char *out, int cap, int *pSlot, int *pRow)
{
	*pSlot = -1; *pRow = -1;
	if (my < COL_Y || my >= COL_Y + COL_H || mx < 0 || mx >= W) return false;
	int slot = g_first + mx / COLW;
	if (slot >= g_ncol) slot = g_ncol - 1;		// the preview / empty slots: deepest folder
	if (slot < 0) return false;
	const Column &k = g_col[slot];
	int row = k.top + (my - COL_Y) / g_rowH;
	if (slot == g_first + mx / COLW && row < k.count && k.e[row].isdir && !k.e[row].isapp)
	{
		join (out, cap, k.path, k.e[row].name);	// onto a folder row: into that folder
		*pSlot = slot; *pRow = row;
		return true;
	}
	scopy (out, k.path, cap);			// elsewhere in the column: its folder
	*pSlot = slot;
	return true;
}

// Each column has its own vertical scrollbar (WK_SBW px, at its right edge) when its
// folder has more entries than fit: drag the thumb, or click the track to jump there.
static bool col_overflows (int slot) { return slot >= 0 && slot < g_ncol && g_col[slot].count > g_rows; }
static void vscroll_to (int slot, int my)
{
	Column &k = g_col[slot];
	WkThumb t = wk_thumb (k.count, g_rows, k.top, COL_H);
	k.top = (int) wk_thumb_pos (my - COL_Y, COL_H, k.count, g_rows, t.h);
	int maxTop = k.count - g_rows; if (maxTop < 0) maxTop = 0;
	if (k.top > maxTop) k.top = maxTop;
	if (k.top < 0) k.top = 0;
}
static unsigned g_lastTick = 0; static int g_lastSlot = -1, g_lastRow = -1;

static void draw_arrow (Canvas &cv, int x, int y, unsigned c)	// small right-pointing triangle
{
	for (int i = 0; i < 4; i++) cv.fillRect (x + i, y + i, 1, 9 - 2 * i, c);
}

class ViewerRoot : public Root
{
public:
	ViewerRoot () : Root (W, H, "File Viewer") {}

	void drawPathBar ()
	{
		canvas.fillRect (0, TB_H, W, BC_H, C_BAR);
		int x = 8, y = TB_H + (BC_H - g_fh) / 2;
		for (int c = 0; c < g_ncol; c++)
		{
			const char *seg;
			char buf[NAMEL];
			if (c == 0)
			{
				if (in_trash ()) seg = "Trash";
				else if (!is_remote (g_col[0].path)) seg = "SD:";
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
					seg = buf;
				}
			}
			else { const Entry &e = g_col[c - 1].e[g_col[c - 1].sel]; scopy (buf, e.name, sizeof buf); seg = buf; }
			if (c > 0) { draw_arrow (canvas, x, y + (g_fh - 9) / 2, C_PATHSEP); x += 10; }
			canvas.text (x, y, seg, c == g_active ? C_ACCENT : C_PATH);
			x += slen (seg) * g_fw + 6;
			g_crumbX[c] = x;
			if (x > W - 40) { for (int k = c + 1; k < g_ncol; k++) g_crumbX[k] = x; break; }
		}
	}

	void drawColumn (int slot, int x)
	{
		const Column &k = g_col[slot];
		canvas.fillRect (x, COL_Y, COLW, COL_H, C_COL);
		canvas.fillRect (x + COLW - 1, COL_Y, 1, COL_H, C_COLSEP);
		int maxChars = (COLW - 30) / g_fw;
		for (int r = 0; r < g_rows; r++)
		{
			int idx = k.top + r;
			if (idx >= k.count) break;
			const Entry &e = k.e[idx];
			int y = COL_Y + r * g_rowH;
			if (idx == k.sel) canvas.fillRect (x, y, COLW - 1, g_rowH, slot == g_active ? C_SEL_ACT : C_SEL_OLD);
			char name[NAMEL]; scopy (name, e.label, sizeof name);
			if (slen (name) > maxChars) { name[maxChars - 2] = '.'; name[maxChars - 1] = '.'; name[maxChars] = '\0'; }
			unsigned col = e.isapp ? C_APPTXT : e.isdir ? C_DIRTXT : C_FILETXT;
			canvas.text (x + 8, y + 2, name, col);
			if (e.isdir && !e.isapp) draw_arrow (canvas, x + COLW - 16, y + (g_rowH - 9) / 2, C_DIMTXT);
		}
		if (g_dropSlot == slot)				// drop target highlight
		{
			if (g_dropRow >= k.top && g_dropRow < k.top + g_rows)
				canvas.frameRect (x + 1, COL_Y + (g_dropRow - k.top) * g_rowH, COLW - 3, g_rowH, 0x0090C0FF);
			else if (g_dropRow < 0)
				canvas.frameRect (x + 1, COL_Y + 1, COLW - 3, COL_H - 2, 0x0090C0FF);
		}
		WkThumb t = wk_thumb (k.count, g_rows, k.top, COL_H);
		if (t.show) wk_draw_vscroll (canvas, x + COLW - 1 - WK_SBW, COL_Y, WK_SBW, COL_H, t, C_COLSEP,
					     g_vdrag == slot ? C_FACE_HI : C_FACE);
		if (k.count == 0) canvas.text (x + 8, COL_Y + 4, "(empty)", C_DIMTXT);
	}

	void drawPreview (int x)
	{
		canvas.fillRect (x, COL_Y, COLW, COL_H, C_COL);
		const Entry *e = sel_entry (g_ncol - 1);
		if (!e) return;
		int y = COL_Y + 8, tx = x + 8, maxChars = (COLW - 16) / g_fw;

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
					if (g_pvKind == PV_APP && (c & 0xFFFFFF) == WK_TRANSPARENT_KEY) continue;
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

		char line[80];
		scopy (line, g_pvKind == PV_APP ? g_pvTitle : e->name, sizeof line);
		if (slen (line) > maxChars) { line[maxChars - 2] = '.'; line[maxChars - 1] = '.'; line[maxChars] = '\0'; }
		canvas.text (tx, y, line, C_TEXT); y += g_fh + 6;
		if (g_pvKind == PV_APP)					// the bundle's folder name
		{
			scopy (line, e->name, sizeof line);
			if (slen (line) > maxChars) { line[maxChars - 2] = '.'; line[maxChars - 1] = '.'; line[maxChars] = '\0'; }
			canvas.text (tx, y, line, C_DIMTXT); y += g_fh + 2;
		}

		const char *kind =
			g_pvKind == PV_APP     ? "Application" :
			g_pvKind == PV_IMAGE   ? "Image" :
			g_pvKind == PV_PROGRAM ? "Program" :
			g_pvKind == PV_TEXT    ? "Text" :
			g_pvKind == PV_EMPTY   ? "Empty file" : "File";
		if (g_pvKind == PV_IMAGE)				// "PNG image"
		{
			char k[24]; int p = 0;
			for (int i = 0; g_pvFormat[i] && p < 12; i++) k[p++] = g_pvFormat[i];
			const char *t = " image"; for (int i = 0; t[i]; i++) k[p++] = t[i];
			k[p] = '\0';
			canvas.text (tx, y, k, C_DIMTXT);
		}
		else canvas.text (tx, y, kind, C_DIMTXT);
		y += g_fh + 2;
		if (g_pvKind != PV_APP) { char sz[24]; fmt_size (sz, e->size); canvas.text (tx, y, sz, C_DIMTXT); y += g_fh + 2; }
		if (g_pvKind == PV_APP || g_pvKind == PV_PROGRAM)
			{ canvas.text (tx, y, "Double-click to run", C_DIMTXT); y += g_fh + 2; }
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
			canvas.fillRect (x + 4, y - 2, COLW - 9, COL_Y + COL_H - y - 2, C_FIELD);
			const char *p = g_pvText;
			while (*p && y + g_fh < COL_Y + COL_H - 4)
			{
				int n = 0;
				while (p[n] && p[n] != '\n' && n < maxChars) { line[n] = p[n] == '\t' ? ' ' : p[n]; if (line[n] == '\r') line[n] = ' '; n++; }
				line[n] = '\0';
				canvas.text (tx, y, line, C_FILETXT);
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
		int t = total_slots ();
		for (int s = 0; s < VIS; s++)
		{
			int slot = g_first + s, x = s * COLW;
			if (slot < g_ncol) drawColumn (slot, x);
			else if (slot == g_ncol && preview_on ()) drawPreview (x);
			else { canvas.fillRect (x, COL_Y, COLW, COL_H, C_COL); canvas.fillRect (x + COLW - 1, COL_Y, 1, COL_H, C_COLSEP); }
		}
		(void) t;
		canvas.fillRect (0, H - ST_H, W, ST_H, C_BAR);
		canvas.text (8, H - ST_H + (ST_H - g_fh) / 2, g_status, C_PATH);
	}

	bool onMouse (int mx, int my, int bl, int, int, int wheel) override
	{
		if (mx < 0) { pressed = false; g_vdrag = -1; return false; }	// left the window
		if (g_vdrag >= 0)					// dragging a column's scrollbar
		{
			if (!bl) { g_vdrag = -1; pressed = false; }
			else vscroll_to (g_vdrag, my);
			invalidate (true);
			return true;
		}
		if (wheel && my >= COL_Y && my < COL_Y + COL_H)
		{
			int slot = g_first + mx / COLW;
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
				if (kapi_drag_begin (DND_FILES, path, (unsigned) slen (path) + 1, e.label))
					g_dragging = true;
				g_armSlot = -1;
			}
			return true;
		}
		pressed = true;

		if (my >= TB_H && my < TB_H + BC_H)			// path bar
		{
			for (int c = 0; c < g_ncol; c++) if (mx < g_crumbX[c]) { jump_to (c); break; }
			invalidate (true);
			return true;
		}
		if (my < COL_Y || my >= COL_Y + COL_H) return true;

		int slot = g_first + mx / COLW;
		unsigned now = kapi_get_ticks ();
		if (slot == g_ncol && preview_on ())		// preview: double-click opens
		{
			if (g_lastSlot == slot && now - g_lastTick < CLICK_DELAY) { open_entry (g_ncol - 1); g_lastSlot = -1; }
			else { g_lastSlot = slot; g_lastTick = now; }
			invalidate (true);
			return true;
		}
		if (slot >= g_ncol) return true;
		if (col_overflows (slot) && mx % COLW >= COLW - 1 - WK_SBW)	// the column's scrollbar
		{
			g_vdrag = slot; vscroll_to (slot, my);
			invalidate (true);
			return true;
		}
		int row = g_col[slot].top + (my - COL_Y) / g_rowH;
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
	}

	void onDrop (int x, int y, int type, const char *data, int, unsigned flags) override
	{
		g_dropSlot = g_dropRow = -1;
		char dir[300]; int s, r;
		if (type != DND_FILES || !drop_target_at (x, y, dir, sizeof dir, &s, &r)) { invalidate (true); return; }
		bool copy = (flags & DND_F_COPY) != 0, toTrash = in_trash () && s >= 0 && ci_cmp (dir, TRASH_FILES) == 0;
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
		if (done) status (toTrash ? "Moved to the Trash" : copy ? "Copied into " : "Moved into ", toTrash ? "" : dir);
		if (failed) notify ("File Viewer", "Some items could not be moved.");
		invalidate (true);
	}

	void onDragDone (int, unsigned) override
	{
		g_dragging = false;
		refresh ();				// the target may have moved it away
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

int main (void)
{
	g_fw = kapi_font_width ();  if (g_fw < 1) g_fw = 8;
	g_fh = kapi_font_height (); if (g_fh < 1) g_fh = 16;
	g_rowH = g_fh + 4;
	g_rows = COL_H / g_rowH; if (g_rows < 1) g_rows = 1;

	for (int c = 0; c < MAXCOL; c++) { g_col[c].e = new Entry[MAXE]; g_col[c].count = 0; g_col[c].sel = -1; }

	ViewerRoot root;
	if (root.canvas.px == 0) return 1;
	g_root = &root;

	// Commands live in the system menu bar (shortcuts handled by wtk::Menu).
	static Menu menu;
	menu.menu ("File");
	menu.item ("Open",       "Enter", 0,             op_open);
	menu.item ("New Folder", "^N",    WK_CTRL ('N'), op_new_folder);
	menu.item ("Rename...",  "^R",    WK_CTRL ('R'), op_rename);
	menu.separator ();
	menu.item ("Move to Trash",       "Del", KEY_DEL, op_delete);
	menu.item ("Delete Permanently...", "",  0,       op_delete_permanently);
	menu.separator ();
	menu.item ("Refresh",    "^L",    WK_CTRL ('L'), op_refresh);
	menu.menu ("Go");
	menu.item ("SD Card",    "",      0,             op_show_sd);
	menu.item ("Trash",      "",      0,             op_open_trash);
	menu.item ("Connect to Server...", "", 0,       op_connect);
	menu.separator ();
	menu.item ("Restore from Trash", "", 0,          op_restore);
	menu.item ("Empty Trash...",     "", 0,          op_empty_trash);
	menu.menu ("Edit");
	menu.item ("Copy",       "^C",    WK_CTRL ('C'), op_copy);
	menu.item ("Cut",        "^X",    WK_CTRL ('X'), op_cut);
	menu.item ("Paste",      "^V",    WK_CTRL ('V'), op_paste);
	menu.publish ();
	g_hsb = new Scrollbar (0, COL_Y + COL_H, W, SB_H, false, 1, 0, on_hscroll);
	root.addChild (g_hsb);

	// Start at the root; an argument (e.g. `run fileviewer SD:/apps`) opens that path.
	char args[256];
	kapi_get_args (args, sizeof args);
	load_col (0, "SD:/");
	g_ncol = 1; g_active = 0;
	if (ci_cmp (args, TRASH_FILES) == 0 || ci_cmp (args, "trash") == 0)	// (hidden: not walkable)
		op_open_trash ();
	else if (args[0] && is_remote (args))			// FTP:host/path (ftpfs)
		show_root (args);
	else if (args[0] == 'S' && args[1] == 'D' && args[2] == ':')
	{
		const char *p = args + 3; if (*p == '/') p++;
		while (*p)
		{
			char seg[NAMEL]; int n = 0;
			while (*p && *p != '/' && n < NAMEL - 1) seg[n++] = *p++;
			seg[n] = '\0';
			if (*p == '/') p++;
			int idx = -1;
			for (int i = 0; i < g_col[g_ncol - 1].count; i++) if (ci_cmp (g_col[g_ncol - 1].e[i].name, seg) == 0) { idx = i; break; }
			if (idx < 0) break;
			select (g_ncol - 1, idx);
		}
	}
	update_status ();
	root.run ();
	return 0;
}
