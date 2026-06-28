//
// config -- a configuration editor. Left pane: the list of apps (scrollable, click
// or Up/Down to select). Selecting one loads SD:apps/<name>.app/config.ini into the
// right pane as editable key=value rows: click a key or value cell to edit it, type
// to change it, Enter to commit. The blank row at the bottom adds a new key. Apply
// saves the file; Discard reloads it. Fully app-drawn (canvas + click/key handlers).
//
#include "kapi.h"
#include "wtk/wtk.h"
#include "applib.h"

#define W	560
#define H	400
#define LW	150			// left pane width
#define LISTY	28			// list/rows top
#define KEYX	158			// right pane left
#define VALX	332			// value column left
#define BTN_Y	368
#define MAXAPPS	48
#define MAXKV	32
#define SBW	12			// app-list scrollbar width

static unsigned *fb;
static int g_fw = 8, g_fh = 18, g_vis = 1;

static char g_app[MAXAPPS][24]; static int g_napps = 0, g_appsel = -1, g_apptop = 0;
static char g_cur[24] = "";

static char g_key[MAXKV][32], g_val[MAXKV][64]; static int g_nkv = 0, g_kvtop = 0;
static int  g_focus = -1;		// -1 none; else row*2 + (0=key / 1=value)
static char g_msg[32] = "";

static void scopy (char *d, const char *s, int cap)
{ int i = 0; for (; s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = '\0'; }
static int in_rect (int x, int y, int rx, int ry, int rw, int rh)
{ return x >= rx && x < rx + rw && y >= ry && y < ry + rh; }

static void load_apps (void)
{
	g_napps = 0;
	void *d = kapi_opendir ("SD:apps");
	if (d == 0) return;
	struct kapi_dirent e;
	while (g_napps < MAXAPPS && kapi_readdir (d, &e))
	{
		int n = ax_strlen (e.name);
		if (!e.is_dir || n <= 4) continue;
		if (e.name[n-4] != '.' || e.name[n-3] != 'a' || e.name[n-2] != 'p' || e.name[n-1] != 'p')
			continue;
		char nm[24]; int k = 0;
		for (; k < n - 4 && k < 23; k++) nm[k] = e.name[k];
		nm[k] = '\0';
		scopy (g_app[g_napps++], nm, 24);
	}
	kapi_closedir (d);
}

static void make_path (const char *name, char *out, int cap)
{
	int p = 0;
	ax_strcat (out, cap, &p, "SD:apps/");
	ax_strcat (out, cap, &p, name);
	ax_strcat (out, cap, &p, ".app/config.ini");
}

static void load_config (const char *name)	// also the Discard action
{
	scopy (g_cur, name, sizeof g_cur);
	g_nkv = 0; g_kvtop = 0; g_focus = -1; g_msg[0] = '\0';
	char path[96]; path[0] = '\0'; make_path (name, path, sizeof path);
	if (app_ini_load_path (path) > 0)
		for (int i = 0; i < g_ini_n && g_nkv < MAXKV; i++)
		{
			scopy (g_key[g_nkv], g_ini_key[i], 32);
			scopy (g_val[g_nkv], g_ini_val[i], 64);
			g_nkv++;
		}
}

static void save_config (void)
{
	if (g_cur[0] == '\0') return;
	char path[96]; path[0] = '\0'; make_path (g_cur, path, sizeof path);
	static char buf[2048]; int b = 0;
	for (int i = 0; i < g_nkv; i++)
	{
		if (g_key[i][0] == '\0') continue;
		for (int k = 0; g_key[i][k] && b < (int) sizeof buf - 2; k++) buf[b++] = g_key[i][k];
		if (b < (int) sizeof buf - 2) buf[b++] = '=';
		for (int k = 0; g_val[i][k] && b < (int) sizeof buf - 2; k++) buf[b++] = g_val[i][k];
		if (b < (int) sizeof buf - 1) buf[b++] = '\n';
	}
	scopy (g_msg, kapi_save_file (path, buf, b) >= 0 ? "saved" : "save failed", sizeof g_msg);
}

static void fill (int x, int y, int w, int h, unsigned c) { ax_fill (fb, W, H, x, y, w, h, c); }

// App-list scrollbar geometry (x0/track-top/track-h/thumb-y/thumb-h). 0 if not needed.
static int sb_geom (int *px0, int *pty0, int *pth, int *pthy, int *pthh)
{
	if (g_napps <= g_vis) return 0;
	int ty0 = LISTY, th = g_vis * g_fh, maxtop = g_napps - g_vis;
	int thh = th * g_vis / g_napps; if (thh < 16) thh = 16;
	*px0 = LW - SBW; *pty0 = ty0; *pth = th;
	*pthy = ty0 + (th - thh) * g_apptop / maxtop; *pthh = thh;
	return 1;
}
static void draw_app_scrollbar (void)
{
	int x0, ty0, th, thy, thh;
	if (!sb_geom (&x0, &ty0, &th, &thy, &thh)) return;
	fill (x0, ty0, SBW, th, 0x00202a34);			// track
	fill (x0 + 1, thy, SBW - 2, thh, 0x005a7088);		// thumb
}
// Keep app `idx` within the visible window (called when the selection changes).
static void scroll_to_show (int idx)
{
	if (idx < g_apptop) g_apptop = idx;
	if (idx >= g_apptop + g_vis) g_apptop = idx - g_vis + 1;
}

static void redraw (void)
{
	fill (0, 0, W, H, 0x00202830);
	fill (0, 0, LW, H, 0x00283440);				// left pane
	wtk::draw_text (fb, W, H, 8, 6, "Apps", 0x0090c0ff);
	int maxtop = g_napps - g_vis; if (maxtop < 0) maxtop = 0;	// clamp the scroll offset
	if (g_apptop > maxtop) g_apptop = maxtop;
	if (g_apptop < 0) g_apptop = 0;
	int listw = (g_napps > g_vis) ? LW - SBW : LW;			// room for the scrollbar
	for (int r = 0; r < g_vis; r++)
	{
		int idx = g_apptop + r;
		if (idx >= g_napps) break;
		int y = LISTY + r * g_fh;
		if (idx == g_appsel) fill (0, y, listw, g_fh, 0x00355070);
		wtk::draw_text (fb, W, H, 8, y + 1, g_app[idx], 0x00e0e0e0);
	}
	draw_app_scrollbar ();

	// Right pane header.
	wtk::draw_text (fb, W, H, KEYX, 6, g_cur[0] ? g_cur : "(select an app)", 0x00ffd070);

	// Editable key=value rows + one trailing blank row to add a new key.
	int shown = g_nkv + 1; if (shown > MAXKV) shown = MAXKV;
	for (int r = 0; r < g_vis; r++)
	{
		int idx = g_kvtop + r;
		if (idx >= shown) break;
		int y = LISTY + r * g_fh;
		const char *kk = (idx < g_nkv) ? g_key[idx] : "";
		const char *vv = (idx < g_nkv) ? g_val[idx] : "";
		fill (KEYX, y, VALX - KEYX - 4, g_fh - 2, 0x00303d4d);
		fill (VALX, y, W - VALX - 6, g_fh - 2, 0x00303d4d);
		wtk::draw_text (fb, W, H, KEYX + 4, y + 1, kk, 0x00c8e0c8);
		wtk::draw_text (fb, W, H, VALX + 4, y + 1, vv, 0x00e8e8c0);
		if (g_focus >= 0 && g_focus / 2 == idx)
		{
			int col = g_focus % 2;
			const char *t = col ? vv : kk;
			int cx = (col ? VALX : KEYX) + 4 + ax_strlen (t) * g_fw;
			fill (cx, y, 2, g_fh - 2, 0x0060ff90);
		}
	}

	// Buttons + status.
	fill (380, BTN_Y, 80, 24, 0x00306030); ax_frame (fb, W, H, 380, BTN_Y, 80, 24, 0x00c0c0c0);
	wtk::draw_text (fb, W, H, 404, BTN_Y + 4, "Apply", 0x00ffffff);
	fill (470, BTN_Y, 80, 24, 0x00603030); ax_frame (fb, W, H, 470, BTN_Y, 80, 24, 0x00c0c0c0);
	wtk::draw_text (fb, W, H, 486, BTN_Y + 4, "Discard", 0x00ffffff);
	if (g_msg[0]) wtk::draw_text (fb, W, H, KEYX, BTN_Y + 4, g_msg, 0x0090c090);
}

static void on_click (unsigned long s, int ev, long val)
{
	(void) s;
	if (ev != GUI_EVENT_CANVAS_CLICK) return;
	int x = (int) ((val >> 16) & 0xFFFF), y = (int) (val & 0xFFFF);

	if (in_rect (x, y, 380, BTN_Y, 80, 24)) { save_config (); return; }
	if (in_rect (x, y, 470, BTN_Y, 80, 24)) { if (g_cur[0]) load_config (g_cur); return; }

	int row = (y - LISTY) / g_fh;
	if (y < LISTY || row < 0) { g_focus = -1; return; }

	if (x < LW)						// left pane
	{
		int x0, ty0, th, thy, thh;			// scrollbar: jump (thumb-centered)
		if (sb_geom (&x0, &ty0, &th, &thy, &thh) && x >= x0 && y >= ty0 && y < ty0 + th)
		{
			int maxtop = g_napps - g_vis;
			int t = (y - ty0 - thh / 2) * maxtop / (th - thh);
			if (t < 0) t = 0;
			if (t > maxtop) t = maxtop;
			g_apptop = t;
			return;
		}
		int idx = g_apptop + row;			// else pick an app
		if (idx < g_napps) { g_appsel = idx; scroll_to_show (idx); load_config (g_app[idx]); }
		return;
	}
	if (g_cur[0] == '\0') return;				// right: edit a cell
	int idx = g_kvtop + row;
	if (idx > g_nkv || idx >= MAXKV) { g_focus = -1; return; }
	if (idx == g_nkv) { g_key[g_nkv][0] = '\0'; g_val[g_nkv][0] = '\0'; g_nkv++; g_focus = idx * 2; return; }
	g_focus = idx * 2 + (x >= VALX ? 1 : 0);
}

static void on_key (unsigned long s, int ev, long key)
{
	(void) s;
	if (ev != GUI_EVENT_KEY) return;

	if (g_focus >= 0)					// editing a cell
	{
		int row = g_focus / 2, col = g_focus % 2;
		char *t = col ? g_val[row] : g_key[row];
		int cap = col ? 64 : 32, n = ax_strlen (t);
		if (key == KEY_ENTER || key == 27) g_focus = -1;
		else if (key == KEY_BACKSPACE) { if (n > 0) t[n - 1] = '\0'; }
		else if (key >= ' ' && key < 0x7f && n < cap - 1) { t[n] = (char) key; t[n + 1] = '\0'; }
		return;
	}
	switch (key)						// navigating
	{
	case KEY_UP:   if (g_appsel > 0) { g_appsel--; scroll_to_show (g_appsel); load_config (g_app[g_appsel]); } break;
	case KEY_DOWN: if (g_appsel < g_napps - 1) { g_appsel++; scroll_to_show (g_appsel); load_config (g_app[g_appsel]); } break;
	case KEY_PGUP: g_kvtop -= g_vis - 1; if (g_kvtop < 0) g_kvtop = 0; break;
	case KEY_PGDN: g_kvtop += g_vis - 1; if (g_kvtop > g_nkv) g_kvtop = g_nkv; break;
	}
}

int main (void)
{
	fb = kapi_create_window (W, H, "config");
	if (fb == 0) return 1;
	wtk::wk_decorate_window ();
	g_fw = kapi_font_width ();  if (g_fw < 1) g_fw = 8;
	g_fh = kapi_font_height (); if (g_fh < 1) g_fh = 16;
	g_fh += 2;					// row pitch
	g_vis = (BTN_Y - LISTY) / g_fh; if (g_vis < 1) g_vis = 1;

	load_apps ();
	kapi_set_click_handler (on_click);
	kapi_set_key_handler (on_key);

	while (!should_exit ())
	{
		pump_events ();
		redraw ();
		msleep (16);
	}
	return 0;
}
