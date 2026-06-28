//
// filer.c -- a file manager (wtk port). Browses the SD card via kapi_opendir/readdir,
// shows directories (first) and files with sizes, and navigates in/out. The window is a
// Root subclass that draws the listing itself and routes mouse/keys; a wtk Scrollbar
// child scrolls the list. Mouse: click a row to select (double-click / Enter opens it).
// Keyboard: up/down to move, Enter to open a folder, Backspace to go up, d/r/n manage.
//
#include "kapi.h"
#include "wtk/wtk.h"		// recursive widget toolkit + wk_messagebox

using namespace wtk;

#define W	460
#define H	380
#define MAXE	256
#define NAMELEN	96
#define LISTY	34
#define CLICK_DELAY	70	// double-click window in HZ(=100) ticks (~700 ms)
#define SB_W	14	// scrollbar column width (right edge of the list)

static int g_fw = 8, g_fh = 16, g_rows = 1;

static char     g_path[256] = "SD:/";
static char     g_name[MAXE][NAMELEN];
static unsigned g_size[MAXE];
static char     g_isdir[MAXE];
static int      g_count = 0;
static int      g_sel = 0, g_top = 0;
static Scrollbar *g_sb = 0;			// vertical scrollbar (driven by / driving g_top)
static Root      *g_root = 0;			// the window (invalidated to repaint the list)

static int      g_mode = 0;		// 0 browse, 1 rename, 2 new folder
static char     g_inb[NAMELEN];
static int      g_inlen = 0;

static int slen (const char *s) { int n = 0; while (s[n]) n++; return n; }
static void scopy (char *d, const char *s, int cap)
{
	int i = 0;
	for (; s[i] && i < cap - 1; i++) d[i] = s[i];
	d[i] = '\0';
}

// Case-insensitive check that `name` ends with ".<ext>".
static int ext_is (const char *name, const char *ext)
{
	int n = slen (name), e = slen (ext);
	if (n < e + 1 || name[n - e - 1] != '.') return 0;
	const char *p = name + n - e;
	for (int i = 0; i < e; i++)
	{
		char a = p[i], b = ext[i];
		if (a >= 'A' && a <= 'Z') a += 32;
		if (b >= 'A' && b <= 'Z') b += 32;
		if (a != b) return 0;
	}
	return 1;
}

static void open_file (int idx);		// defined after full_path

static int itoa (int v, char *b)
{
	char t[12]; int n = 0, p = 0;
	if (v == 0) t[n++] = '0';
	while (v) { t[n++] = (char) ('0' + v % 10); v /= 10; }
	while (n) b[p++] = t[--n];
	b[p] = '\0';
	return p;
}

// Read g_path into the entry arrays. Adds a ".." entry first unless at the root.
static void read_dir (void)
{
	g_count = 0; g_sel = 0; g_top = 0;

	int root = (g_path[0] == 'S' && g_path[1] == 'D' && g_path[2] == ':' &&
		    (g_path[3] == '\0' || (g_path[3] == '/' && g_path[4] == '\0')));
	if (!root)
	{
		scopy (g_name[g_count], "..", NAMELEN);
		g_size[g_count] = 0; g_isdir[g_count] = 1; g_count++;
	}

	void *d = kapi_opendir (g_path);
	if (d == 0) return;
	struct kapi_dirent ent;
	// directories first, then files (two passes for a tidy listing)
	for (int pass = 0; pass < 2; pass++)
	{
		void *dd = (pass == 0) ? d : kapi_opendir (g_path);
		if (dd == 0) break;
		while (g_count < MAXE && kapi_readdir (dd, &ent))
		{
			if ((pass == 0) != (ent.is_dir != 0)) continue;	// pass0=dirs, pass1=files
			scopy (g_name[g_count], ent.name, NAMELEN);
			g_size[g_count] = ent.size;
			g_isdir[g_count] = (char) ent.is_dir;
			g_count++;
		}
		kapi_closedir (dd);
	}
}

// Build the path for entering child `name` (g_path keeps no trailing slash except root).
static void enter_dir (const char *name)
{
	int n = slen (g_path);
	if (!(n > 0 && g_path[n - 1] == '/'))
	{
		if (n < (int) sizeof (g_path) - 1) { g_path[n++] = '/'; g_path[n] = '\0'; }
	}
	for (int i = 0; name[i] && n < (int) sizeof (g_path) - 1; i++) g_path[n++] = name[i];
	g_path[n] = '\0';
	read_dir ();
}

static void go_up (void)
{
	int n = slen (g_path);
	if (n > 0 && g_path[n - 1] == '/') n--;		// ignore a trailing slash
	while (n > 0 && g_path[n - 1] != '/') n--;	// drop the last component
	if (n > 0) n--;					// drop the '/' itself
	g_path[n] = '\0';
	if (slen (g_path) <= 3) scopy (g_path, "SD:/", sizeof (g_path));	// clamp to root
	read_dir ();
}

static void open_sel (void)
{
	if (g_sel < 0 || g_sel >= g_count) return;
	if (g_isdir[g_sel])
	{
		if (g_name[g_sel][0] == '.' && g_name[g_sel][1] == '.') go_up ();
		else enter_dir (g_name[g_sel]);
	}
	else open_file (g_sel);				// .txt/.ini -> tinypad, .elf -> run
}

static void fix_scroll (void)
{
	if (g_sel < 0) g_sel = 0;
	if (g_sel >= g_count) g_sel = g_count - 1;
	if (g_sel < g_top) g_top = g_sel;
	if (g_sel >= g_top + g_rows) g_top = g_sel - g_rows + 1;
	if (g_top < 0) g_top = 0;
}

// Largest valid g_top: the listing's last g_rows entries fill the view (0 if it all fits).
static int max_top (void) { int m = g_count - g_rows; return m < 0 ? 0 : m; }

// Refresh the scrollbar's range from the current entry count and slave its thumb to g_top.
// Called from onDraw, so keyboard navigation (which moves g_top via fix_scroll) drags the
// thumb too, and a fresh directory listing rescales it.
static void sync_sb (void)
{
	if (g_top > max_top ()) g_top = max_top ();
	if (g_top < 0) g_top = 0;
	if (g_sb && (g_sb->vmax != (max_top () < 1 ? 1 : max_top ()) || g_sb->value != g_top))
	{ g_sb->vmax = max_top (); g_sb->value = g_top; g_sb->invalidate (true); }
}

// Scrollbar drag -> set the view top. We leave g_sel alone: the user is scrolling the view,
// not moving the selection (matching how desktop file managers behave).
static void on_scroll (Widget &w)
{
	g_top = ((Scrollbar &) w).value;
	if (g_top > max_top ()) g_top = max_top ();
	if (g_top < 0) g_top = 0;
	if (g_root) g_root->invalidate (true);
}

// Build "<g_path>/<name>" into out.
static void full_path (const char *name, char *out, int cap)
{
	int p = 0;
	for (; g_path[p] && p < cap - 1; p++) out[p] = g_path[p];
	if (p > 0 && out[p - 1] != '/' && p < cap - 1) out[p++] = '/';
	for (int k = 0; name[k] && p < cap - 1; k++) out[p++] = name[k];
	out[p] = '\0';
}

// Open a file by double-click / Enter: text-ish types open in tinypad (file passed
// as argv); .elf programs are run directly. Other types are left alone.
static void open_file (int idx)
{
	if (idx < 0 || idx >= g_count) return;
	char path[300];
	full_path (g_name[idx], path, sizeof path);

	if (ext_is (g_name[idx], "txt") || ext_is (g_name[idx], "ini") ||
	    ext_is (g_name[idx], "md")  || ext_is (g_name[idx], "log") ||
	    ext_is (g_name[idx], "cfg") || ext_is (g_name[idx], "conf") ||
	    ext_is (g_name[idx], "csv") || ext_is (g_name[idx], "c") ||
	    ext_is (g_name[idx], "h")   || ext_is (g_name[idx], "sh"))
	{
		kapi_exec ("SD:apps/tinypad.app/main.elf", path);	// open in the editor
	}
	else if (ext_is (g_name[idx], "elf"))
	{
		kapi_exec (path, "");					// run the program
	}
}

static void delete_sel (void)
{
	if (g_sel < 0 || g_sel >= g_count) return;
	if (g_isdir[g_sel] && g_name[g_sel][0] == '.' && g_name[g_sel][1] == '.') return;	// not ".."
	if (!wk_messagebox ("Delete", g_name[g_sel], MB_YESNO)) return;				// confirm
	char path[300];
	full_path (g_name[g_sel], path, sizeof path);
	kapi_remove (path);			// fails on non-empty dirs (FatFs)
	read_dir ();
}

static void commit_input (void)
{
	g_inb[g_inlen] = '\0';
	if (g_inlen > 0)
	{
		char dst[300];
		full_path (g_inb, dst, sizeof dst);
		if (g_mode == 1 && g_sel >= 0 && g_sel < g_count)
		{
			char src[300];
			full_path (g_name[g_sel], src, sizeof src);
			kapi_rename (src, dst);
		}
		else if (g_mode == 2)
		{
			kapi_mkdir (dst);
		}
	}
	g_mode = 0; g_inlen = 0; g_inb[0] = '\0';
	read_dir ();
}

// Single click selects a row; a second click on the same row within ~0.4 s is a
// double-click: it opens the entry (folder -> enter, file -> tinypad/run).
static unsigned g_last_tick = 0;
static int      g_last_idx = -1;

// The window: draws the listing + the rename/new input bar, and routes mouse/keys. A
// wtk Scrollbar child (right column) is handled before us, so our onMouse only sees the
// list area; unfocused keys fall through to onKey (no text widget steals focus).
class FilerRoot : public Root
{
public:
	FilerRoot (int w, int h, const char *t) : Root (w, h, t) {}

	void onDraw () override
	{
		sync_sb ();				// clamp g_top + slave the scrollbar thumb to it

		canvas.clear (0x00202830);
		canvas.fillRect (0, 0, W, LISTY - 2, 0x00303d4d);
		canvas.text (8, 9, g_path, 0x00e0e0e0);
		canvas.text (W - 150, 9, "d:del r:ren n:new", 0x0090a0b0);

		for (int r = 0; r < g_rows; r++)
		{
			int idx = g_top + r;
			if (idx >= g_count) break;
			int y = LISTY + r * g_fh;
			if (idx == g_sel) canvas.fillRect (0, y, W - SB_W, g_fh, 0x00355070);

			unsigned col = g_isdir[idx] ? 0x0080c8ff : 0x00d8d8d8;
			char line[NAMELEN + 8];
			int p = 0;
			if (g_isdir[idx]) line[p++] = '[';
			for (int i = 0; g_name[idx][i] && p < (int) sizeof (line) - 2; i++) line[p++] = g_name[idx][i];
			if (g_isdir[idx]) line[p++] = ']';
			line[p] = '\0';
			canvas.text (10, y + 1, line, col);

			if (!g_isdir[idx])
			{
				char sz[16]; itoa ((int) g_size[idx], sz);
				int tx = W - SB_W - 12 - slen (sz) * g_fw;
				canvas.text (tx, y + 1, sz, 0x0090a0a8);
			}
		}

		if (g_mode != 0)			// rename / new-folder input bar
		{
			int y = H - g_fh - 4;
			canvas.fillRect (0, y - 2, W, g_fh + 6, 0x00102030);
			const char *lbl = (g_mode == 1) ? "rename: " : "new folder: ";
			canvas.text (6, y, lbl, 0x00ffd070);
			int lx = 6 + slen (lbl) * g_fw;
			canvas.text (lx, y, g_inb, 0x00ffffff);
			canvas.fillRect (lx + g_inlen * g_fw, y, 2, g_fh, 0x0060ff90);
		}
	}

	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		if (mx < 0) { pressed = false; return false; }
		if (!bl) { pressed = false; return true; }
		if (pressed) return true;			// only the press edge selects
		pressed = true;
		if (mx >= W - SB_W) return true;		// scrollbar column isn't a row hit
		int row = (my - LISTY) / g_fh;
		if (row < 0) return true;
		int idx = g_top + row;
		if (idx >= g_count) return true;

		// Double-click: same row, clicked again within CLICK_DELAY.
		unsigned now = kapi_get_ticks ();
		int dbl = (idx == g_last_idx) && (now - g_last_tick) < CLICK_DELAY;
		g_sel = idx;
		fix_scroll ();
		if (dbl) { g_last_idx = -1; open_sel (); }	// consume, so a 3rd click starts over
		else     { g_last_idx = idx; g_last_tick = now; }
		invalidate (true);
		return true;
	}

	bool onKey (long key) override
	{
		if (g_mode != 0)			// rename / new-folder input
		{
			if (key == KEY_ENTER) commit_input ();
			else if (key == 27) { g_mode = 0; g_inlen = 0; }	// Esc cancels
			else if (key == KEY_BACKSPACE) { if (g_inlen > 0) g_inb[--g_inlen] = '\0'; }
			else if (key >= ' ' && key < 0x7f && g_inlen < (int) sizeof g_inb - 1)
				{ g_inb[g_inlen++] = (char) key; g_inb[g_inlen] = '\0'; }
			invalidate (true);
			return true;
		}

		switch (key)
		{
		case KEY_UP:   g_sel--; break;
		case KEY_DOWN: g_sel++; break;
		case KEY_PGUP: g_sel -= g_rows; break;
		case KEY_PGDN: g_sel += g_rows; break;
		case KEY_ENTER: open_sel (); break;		// open file / run .elf (also enters dirs)
		case ' ':					// space: enter the selected folder
			if (g_sel >= 0 && g_sel < g_count && g_isdir[g_sel]) open_sel ();
			break;
		case KEY_BACKSPACE: go_up (); break;
		case KEY_DEL: case 'd': case 'D': delete_sel (); break;
		case 'r': case 'R':			// rename selected (prefill its name)
			if (g_sel >= 0 && g_sel < g_count && g_name[g_sel][0] != '.')
			{
				g_mode = 1; g_inlen = 0;
				for (int i = 0; g_name[g_sel][i] && g_inlen < (int) sizeof g_inb - 1; i++)
					g_inb[g_inlen++] = g_name[g_sel][i];
				g_inb[g_inlen] = '\0';
			}
			break;
		case 'n': case 'N': g_mode = 2; g_inlen = 0; g_inb[0] = '\0'; break;	// new folder
		default: return false;
		}
		fix_scroll ();
		invalidate (true);
		return true;
	}
};

int main (void)
{
	g_fw = kapi_font_width ();  if (g_fw < 1) g_fw = 8;
	g_fh = kapi_font_height (); if (g_fh < 1) g_fh = 16;
	g_rows = (H - LISTY) / g_fh;
	if (g_rows < 1) g_rows = 1;

	FilerRoot root (W, H, "filer");
	if (root.canvas.px == 0) return 1;
	g_root = &root;

	// Vertical scrollbar down the right edge of the list (range set later by sync_sb).
	g_sb = new Scrollbar (W - SB_W, LISTY, SB_W, H - LISTY, true, 1, 0, on_scroll);
	root.addChild (g_sb);

	read_dir ();
	root.run ();
	return 0;
}
