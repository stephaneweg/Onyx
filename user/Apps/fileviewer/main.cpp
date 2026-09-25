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
// Toolbar: New Folder, Rename, Delete, Copy, Cut, Paste, Refresh. Operations act on the
// selection of the active column; Paste and New Folder target the active column's folder.
//
#include "kapi.h"
#include "bmp.hpp"
#include "wtk/wtk.h"

using namespace wtk;

#define W	800
#define H	520
#define VIS	4			// columns visible at once
#define COLW	(W / VIS)
#define TB_H	36			// toolbar
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

struct Entry { char name[NAMEL]; unsigned size; unsigned char isdir, isapp; };
struct Column { char path[256]; Entry *e; int count, sel, top; };

static Column g_col[MAXCOL];
static int g_ncol = 0;			// folder columns shown
static int g_active = 0;		// keyboard/operation column
static int g_first = 0;			// first visible slot (horizontal scroll)
static int g_fw = 8, g_fh = 16, g_rowH = 20, g_rows = 1;
static Root *g_root = 0;
static Scrollbar *g_hsb = 0;
static char g_status[160] = "";

// Clipboard (a single file or folder).
static char g_clip[256] = "";
static bool g_clipCut = false, g_clipDir = false;

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
static int is_text_name (const char *n)
{
	static const char *ext[] = { "txt", "ini", "md", "log", "cfg", "conf", "csv", "c", "h",
				     "cpp", "hpp", "sh", "html", "htm", "css", "kmap", "json", 0 };
	for (int i = 0; ext[i]; i++) if (ends_with (n, ext[i])) return 1;
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
		if (ent.name[0] == '.' && (ent.name[1] == '\0' || (ent.name[1] == '.' && ent.name[2] == '\0'))) continue;
		Entry &e = k.e[k.count++];
		scopy (e.name, ent.name, NAMEL);
		e.size = ent.size; e.isdir = ent.is_dir ? 1 : 0;
		e.isapp = e.isdir && ends_with (e.name, "app");
	}
	kapi_closedir (d);
	// Sort: plain folders first, then app bundles + files; alphabetical, case-insensitive.
	for (int i = 1; i < k.count; i++)
	{
		Entry t = k.e[i]; int j = i - 1;
		int tg = (t.isdir && !t.isapp) ? 0 : 1;
		while (j >= 0)
		{
			int g = (k.e[j].isdir && !k.e[j].isapp) ? 0 : 1;
			if (g < tg || (g == tg && ci_cmp (k.e[j].name, t.name) <= 0)) break;
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
static char g_pvTitle[64];

static void preview_clear (void)
{
	if (g_pvImg) { delete [] g_pvImg; g_pvImg = 0; }
	g_pvKind = PV_NONE; g_pvText[0] = '\0'; g_pvTitle[0] = '\0';
}
static void preview_build (void)
{
	preview_clear ();
	if (!preview_on ()) return;
	const Entry *e = sel_entry (g_ncol - 1);
	char path[300];
	join (path, sizeof path, g_col[g_ncol - 1].path, e->name);

	if (e->isapp)
	{
		g_pvKind = PV_APP;
		scopy (g_pvTitle, e->name, sizeof g_pvTitle);
		g_pvTitle[slen (g_pvTitle) - 4] = '\0';		// drop ".app"
		char p2[320];
		join (p2, sizeof p2, path, "app.txt");		// "name = Friendly name"
		void *f = kapi_open (p2);
		if (f)
		{
			char b[512]; int n = kapi_read (f, b, sizeof b - 1); kapi_close (f);
			if (n < 0) n = 0;
			b[n] = '\0';
			for (int i = 0; i < n; i++)
			{
				if ((i == 0 || b[i - 1] == '\n') && b[i] == 'n' && b[i + 1] == 'a' && b[i + 2] == 'm' && b[i + 3] == 'e')
				{
					int j = i + 4; while (b[j] == ' ' || b[j] == '=' || b[j] == '\t') j++;
					int k = 0; while (b[j] && b[j] != '\n' && b[j] != '\r' && k < (int) sizeof g_pvTitle - 1) g_pvTitle[k++] = b[j++];
					g_pvTitle[k] = '\0';
					break;
				}
			}
		}
		join (p2, sizeof p2, path, "icon.bmp");
		g_pvImg = ui::bmp_decode (p2, &g_pvW, &g_pvH);
		return;
	}
	if (e->size == 0) { g_pvKind = PV_EMPTY; return; }
	if (ends_with (e->name, "bmp"))
	{
		g_pvImg = ui::bmp_decode (path, &g_pvW, &g_pvH);
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
	else if (is_text_name (e->name)) { kapi_exec ("SD:apps/tinypad.app/main", path); status ("Opened in tinypad: ", e->name); }
	else if (is_program (path)) { kapi_exec (path, ""); status ("Running ", e->name); }
	else status ("No application to open ", e->name);
}

// ---- file operations ------------------------------------------------------------------
static bool exists (const char *path)
{
	void *f = kapi_open (path);
	if (f) { kapi_close (f); return true; }
	void *d = kapi_opendir (path);
	if (d) { kapi_closedir (d); return true; }
	return false;
}

static bool copy_file (const char *src, const char *dst)
{
	void *f = kapi_open (src);
	if (!f) return false;
	unsigned n = kapi_fsize (f);
	unsigned char *buf = new unsigned char[n ? n : 1];
	if (!buf) { kapi_close (f); return false; }
	unsigned done = 0;
	while (done < n)
	{
		int r = kapi_read (f, buf + done, n - done);
		if (r <= 0) break;
		done += (unsigned) r;
	}
	kapi_close (f);
	bool ok = done == n && kapi_save_file (dst, buf, n) != 0;
	delete [] buf;
	return ok;
}

static bool copy_tree (const char *src, const char *dst, int depth)
{
	if (depth > 12) return false;
	void *d = kapi_opendir (src);
	if (!d) return copy_file (src, dst);
	kapi_mkdir (dst);
	struct kapi_dirent ent;
	bool ok = true;
	while (kapi_readdir (d, &ent))
	{
		if (ent.name[0] == '.' && (ent.name[1] == '\0' || ent.name[1] == '.')) continue;
		char s[300], t[300];
		join (s, sizeof s, src, ent.name);
		join (t, sizeof t, dst, ent.name);
		ok = (ent.is_dir ? copy_tree (s, t, depth + 1) : copy_file (s, t)) && ok;
	}
	kapi_closedir (d);
	return ok;
}

static bool remove_tree (const char *path, int depth)
{
	if (depth > 12) return false;
	void *d = kapi_opendir (path);
	if (d)
	{
		struct kapi_dirent ent;
		char names[32][NAMEL]; int dirs[32];
		for (;;)				// delete in batches (don't modify while iterating)
		{
			int n = 0;
			while (n < 32 && kapi_readdir (d, &ent))
			{
				if (ent.name[0] == '.' && (ent.name[1] == '\0' || ent.name[1] == '.')) continue;
				scopy (names[n], ent.name, NAMEL); dirs[n] = ent.is_dir; n++;
			}
			kapi_closedir (d);
			if (n == 0) break;
			for (int i = 0; i < n; i++)
			{
				char s[300]; join (s, sizeof s, path, names[i]);
				if (dirs[i]) remove_tree (s, depth + 1); else kapi_remove (s);
			}
			d = kapi_opendir (path);
			if (!d) break;
		}
	}
	return kapi_remove (path) != 0;
}

// A free name in dir for `name`: "name", then "name copy", "name copy 2", ...
static void unique_name (char *out, int cap, const char *dir, const char *name)
{
	char base[NAMEL], ext[NAMEL] = "";
	scopy (base, name, sizeof base);
	int dot = -1;
	for (int i = 1; base[i]; i++) if (base[i] == '.') dot = i;
	if (dot > 0) { scopy (ext, base + dot, sizeof ext); base[dot] = '\0'; }
	for (int n = 0; n < 100; n++)
	{
		char cand[NAMEL]; int p = 0;
		for (int i = 0; base[i] && p < NAMEL - 20; i++) cand[p++] = base[i];
		if (n > 0) { const char *s = " copy"; for (int i = 0; s[i]; i++) cand[p++] = s[i]; }
		if (n > 1) { cand[p++] = ' '; if (n >= 10) cand[p++] = (char) ('0' + n / 10); cand[p++] = (char) ('0' + n % 10); }
		for (int i = 0; ext[i] && p < NAMEL - 1; i++) cand[p++] = ext[i];
		cand[p] = '\0';
		join (out, cap, dir, cand);
		if (!exists (out)) return;
	}
}

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

static void op_new_folder (Widget &)
{
	char name[NAMEL];
	if (!ask_name ("New folder in this column", "New Folder", name, sizeof name)) return;
	char p[300]; join (p, sizeof p, g_col[g_active].path, name);
	if (exists (p) || !kapi_mkdir (p)) { status ("Could not create folder ", name); return; }
	refresh ();
	for (int i = 0; i < g_col[g_active].count; i++)
		if (ci_cmp (g_col[g_active].e[i].name, name) == 0) { select (g_active, i); break; }
	status ("Created folder ", name);
}
static void op_rename (Widget &)
{
	const Entry *e = sel_entry (g_active);
	if (!e) { status ("Select something to rename"); return; }
	char name[NAMEL], old[NAMEL];
	scopy (old, e->name, sizeof old);
	if (!ask_name ("Rename", old, name, sizeof name) || ci_cmp (name, old) == 0) return;
	char s[300], d[300];
	join (s, sizeof s, g_col[g_active].path, old);
	join (d, sizeof d, g_col[g_active].path, name);
	if (exists (d) || !kapi_rename (s, d)) { status ("Could not rename to ", name); return; }
	g_col[g_active].sel = -1; g_ncol = g_active + 1;
	refresh ();
	for (int i = 0; i < g_col[g_active].count; i++)
		if (ci_cmp (g_col[g_active].e[i].name, name) == 0) { select (g_active, i); break; }
	status ("Renamed to ", name);
}
static void op_delete (Widget &)
{
	const Entry *e = sel_entry (g_active);
	if (!e) { status ("Select something to delete"); return; }
	char msg[128]; int p = 0;
	const char *a = e->isdir ? "Delete the folder\n" : "Delete\n";
	for (int i = 0; a[i]; i++) msg[p++] = a[i];
	for (int i = 0; e->name[i] && p < 100; i++) msg[p++] = e->name[i];
	if (e->isdir) { const char *b = "\nand everything in it?"; for (int i = 0; b[i]; i++) msg[p++] = b[i]; }
	else msg[p++] = '?';
	msg[p] = '\0';
	if (!wk_messagebox ("Delete", msg, MB_YESNO)) return;
	char path[300]; join (path, sizeof path, g_col[g_active].path, e->name);
	char name[NAMEL]; scopy (name, e->name, sizeof name);
	bool ok = e->isdir ? remove_tree (path, 0) : kapi_remove (path) != 0;
	g_col[g_active].sel = -1; g_ncol = g_active + 1;
	refresh ();
	status (ok ? "Deleted " : "Could not delete ", name);
}
static void clip_set (bool cut)
{
	const Entry *e = sel_entry (g_active);
	if (!e) { status ("Select something to ", cut ? "cut" : "copy"); return; }
	join (g_clip, sizeof g_clip, g_col[g_active].path, e->name);
	g_clipCut = cut; g_clipDir = e->isdir;
	status (cut ? "Cut: " : "Copied: ", e->name);
}
static void op_copy (Widget &) { clip_set (false); }
static void op_cut (Widget &)  { clip_set (true); }
static void op_paste (Widget &)
{
	if (g_clip[0] == '\0') { status ("Nothing to paste"); return; }
	const char *name = g_clip; for (const char *p = g_clip; *p; p++) if (*p == '/') name = p + 1;
	const char *dir = g_col[g_active].path;
	char dst[300];
	unique_name (dst, sizeof dst, dir, name);
	// Refuse to paste a folder into itself (or below it).
	int n = slen (g_clip);
	if (g_clipDir && ci_cmp (dir, g_clip) == 0) { status ("Cannot paste a folder into itself"); return; }
	bool inside = true; for (int i = 0; i < n; i++) if (lower (dir[i]) != lower (g_clip[i])) { inside = false; break; }
	if (g_clipDir && inside && dir[n] == '/') { status ("Cannot paste a folder inside itself"); return; }
	bool ok;
	if (g_clipCut) { ok = kapi_rename (g_clip, dst) != 0; if (ok) g_clip[0] = '\0'; }
	else ok = g_clipDir ? copy_tree (g_clip, dst, 0) : copy_file (g_clip, dst);
	refresh ();
	status (ok ? "Pasted into " : "Paste failed into ", dir);
}
static void op_refresh (Widget &) { refresh (); status ("Refreshed"); }

static void on_hscroll (Widget &w)
{
	g_first = ((Scrollbar &) w).value;
	if (g_root) g_root->invalidate (true);
}

// ---- the window --------------------------------------------------------------------------
static int g_crumbX[MAXCOL + 1];		// path-bar segment right edges (hit-test)
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
			if (c == 0) seg = "SD:";
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
			char name[NAMEL]; scopy (name, e.name, sizeof name);
			if (e.isapp) name[slen (name) - 4] = '\0';
			if (slen (name) > maxChars) { name[maxChars - 2] = '.'; name[maxChars - 1] = '.'; name[maxChars] = '\0'; }
			unsigned col = e.isapp ? C_APPTXT : e.isdir ? C_DIRTXT : C_FILETXT;
			canvas.text (x + 8, y + 2, name, col);
			if (e.isdir && !e.isapp) draw_arrow (canvas, x + COLW - 16, y + (g_rowH - 9) / 2, C_DIMTXT);
		}
		WkThumb t = wk_thumb (k.count, g_rows, k.top, COL_H);
		if (t.show) wk_draw_vscroll (canvas, x + COLW - 6, COL_Y, 5, COL_H, t, C_COL, C_FACE);
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
					canvas.pixel (ox + i, y + j, c);
				}
			y += dh + 10;
		}

		char line[80];
		scopy (line, g_pvKind == PV_APP ? g_pvTitle : e->name, sizeof line);
		if (slen (line) > maxChars) { line[maxChars - 2] = '.'; line[maxChars - 1] = '.'; line[maxChars] = '\0'; }
		canvas.text (tx, y, line, C_TEXT); y += g_fh + 6;

		const char *kind =
			g_pvKind == PV_APP     ? "Application" :
			g_pvKind == PV_IMAGE   ? "BMP image" :
			g_pvKind == PV_PROGRAM ? "Program" :
			g_pvKind == PV_TEXT    ? "Text" :
			g_pvKind == PV_EMPTY   ? "Empty file" : "File";
		canvas.text (tx, y, kind, C_DIMTXT); y += g_fh + 2;
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
		canvas.fillRect (0, 0, W, TB_H, C_FACE_DN);
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
		if (mx < 0) { pressed = false; return false; }
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
		if (!bl) { pressed = false; return true; }
		if (pressed) return true;
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
		int row = g_col[slot].top + (my - COL_Y) / g_rowH;
		if (row >= g_col[slot].count) { jump_to (slot); invalidate (true); return true; }

		bool dbl = slot == g_lastSlot && row == g_lastRow && now - g_lastTick < CLICK_DELAY;
		if (!(g_col[slot].sel == row && slot + 1 == g_ncol - (g_col[slot].e[row].isdir && !g_col[slot].e[row].isapp ? 1 : 0)))
			select (slot, row);
		else { g_active = slot; update_status (); }
		if (dbl) { open_entry (slot); g_lastSlot = -1; }
		else { g_lastSlot = slot; g_lastRow = row; g_lastTick = now; }
		invalidate (true);
		return true;
	}

	bool onKey (long key) override
	{
		Column &k = g_col[g_active];
		static Widget dummy (0, 0, 1, 1);
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
		case KEY_DEL:  op_delete (dummy); break;
		case 3:  op_copy (dummy); break;		// Ctrl-C
		case 24: op_cut (dummy); break;			// Ctrl-X
		case 22: op_paste (dummy); break;		// Ctrl-V
		case 14: op_new_folder (dummy); break;		// Ctrl-N
		case 18: op_rename (dummy); break;		// Ctrl-R
		case 12: op_refresh (dummy); break;		// Ctrl-L
		default:
			if (key > ' ' && key < 127 && k.count)	// type-ahead: next name starting with it
			{
				char c = lower ((char) key);
				for (int n = 1; n <= k.count; n++)
				{
					int i = ((k.sel < 0 ? -1 : k.sel) + n) % k.count;
					if (lower (k.e[i].name[0]) == c) { select (g_active, i); break; }
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

	static const struct { const char *label; Action cb; } tools[] = {
		{ "New Folder", op_new_folder }, { "Rename", op_rename }, { "Delete", op_delete },
		{ "Copy", op_copy }, { "Cut", op_cut }, { "Paste", op_paste }, { "Refresh", op_refresh } };
	int x = 6;
	for (unsigned i = 0; i < sizeof tools / sizeof tools[0]; i++)
	{
		int w = slen (tools[i].label) * g_fw + 20;
		root.addChild (new Button (x, 4, w, TB_H - 8, tools[i].label, tools[i].cb));
		x += w + 6;
	}
	g_hsb = new Scrollbar (0, COL_Y + COL_H, W, SB_H, false, 1, 0, on_hscroll);
	root.addChild (g_hsb);

	// Start at the root; an argument (e.g. `run fileviewer SD:/apps`) opens that path.
	char args[256];
	kapi_get_args (args, sizeof args);
	load_col (0, "SD:/");
	g_ncol = 1; g_active = 0;
	if (args[0] == 'S' && args[1] == 'D' && args[2] == ':')
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
