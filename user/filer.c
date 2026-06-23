//
// filer.c -- a file manager. Browses the SD card via kapi_opendir/readdir, shows
// directories (first) and files with sizes, and navigates in/out. Mouse: click a
// row to select (clicking a folder enters it). Keyboard: up/down to move, Enter to
// open a folder, Backspace to go up.
//
#include "kapi.h"

#define W	460
#define H	380
#define MAXE	256
#define NAMELEN	96
#define LISTY	34

static unsigned *fb;
static int g_fw = 8, g_fh = 16, g_rows = 1;

static char     g_path[256] = "SD:/";
static char     g_name[MAXE][NAMELEN];
static unsigned g_size[MAXE];
static char     g_isdir[MAXE];
static int      g_count = 0;
static int      g_sel = 0, g_top = 0;

static int slen (const char *s) { int n = 0; while (s[n]) n++; return n; }
static void scopy (char *d, const char *s, int cap)
{
	int i = 0;
	for (; s[i] && i < cap - 1; i++) d[i] = s[i];
	d[i] = '\0';
}

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
	// files: selection only (open/run comes once apps take arguments)
}

static void fix_scroll (void)
{
	if (g_sel < 0) g_sel = 0;
	if (g_sel >= g_count) g_sel = g_count - 1;
	if (g_sel < g_top) g_top = g_sel;
	if (g_sel >= g_top + g_rows) g_top = g_sel - g_rows + 1;
	if (g_top < 0) g_top = 0;
}

static void on_key (unsigned long s, int ev, long key)
{
	(void) s;
	if (ev != GUI_EVENT_KEY) return;
	switch (key)
	{
	case KEY_UP:   g_sel--; break;
	case KEY_DOWN: g_sel++; break;
	case KEY_PGUP: g_sel -= g_rows; break;
	case KEY_PGDN: g_sel += g_rows; break;
	case KEY_ENTER: open_sel (); break;
	case KEY_BACKSPACE: go_up (); break;
	}
	fix_scroll ();
}

static void on_click (unsigned long s, int ev, long val)
{
	(void) s;
	if (ev != GUI_EVENT_CANVAS_CLICK) return;
	int cy = (int) (val & 0xFFFF);
	int row = (cy - LISTY) / g_fh;
	if (row < 0) return;
	int idx = g_top + row;
	if (idx >= g_count) return;
	g_sel = idx;
	if (g_isdir[idx]) open_sel ();			// click a folder to enter it
	fix_scroll ();
}

static void fill_rect (int x, int y, int w, int h, unsigned c)
{
	for (int yy = y; yy < y + h && yy < H; yy++)
		for (int xx = x; xx < x + w && xx < W; xx++)
			if (xx >= 0 && yy >= 0) fb[yy * W + xx] = c;
}

static void redraw (void)
{
	fill_rect (0, 0, W, H, 0x00202830);
	fill_rect (0, 0, W, LISTY - 2, 0x00303d4d);
	kapi_draw_text (8, 9, g_path, 0x00e0e0e0);

	for (int r = 0; r < g_rows; r++)
	{
		int idx = g_top + r;
		if (idx >= g_count) break;
		int y = LISTY + r * g_fh;
		if (idx == g_sel) fill_rect (0, y, W, g_fh, 0x00355070);

		unsigned col = g_isdir[idx] ? 0x0080c8ff : 0x00d8d8d8;
		char line[NAMELEN + 8];
		int p = 0;
		if (g_isdir[idx]) line[p++] = '[';
		for (int i = 0; g_name[idx][i] && p < (int) sizeof (line) - 2; i++) line[p++] = g_name[idx][i];
		if (g_isdir[idx]) line[p++] = ']';
		line[p] = '\0';
		kapi_draw_text (10, y + 1, line, col);

		if (!g_isdir[idx])
		{
			char sz[16]; itoa ((int) g_size[idx], sz);
			int tx = W - 12 - slen (sz) * g_fw;
			kapi_draw_text (tx, y + 1, sz, 0x0090a0a8);
		}
	}
}

int main (void)
{
	fb = kapi_create_window (W, H, "filer");
	if (fb == 0) return 1;
	g_fw = kapi_font_width ();  if (g_fw < 1) g_fw = 8;
	g_fh = kapi_font_height (); if (g_fh < 1) g_fh = 16;
	g_rows = (H - LISTY) / g_fh;
	if (g_rows < 1) g_rows = 1;

	kapi_set_key_handler (on_key);
	kapi_set_click_handler (on_click);
	read_dir ();

	while (!should_exit ())
	{
		pump_events ();
		redraw ();
		msleep (16);
	}
	return 0;
}
