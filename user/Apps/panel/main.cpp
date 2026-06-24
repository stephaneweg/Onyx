//
// panel/main.cpp -- the launcher/taskbar bar (C++ port, on uikit.hpp). Borderless bar
// pinned to a screen edge (config.ini position). Apps button + quicklaunch + dynamic
// taskbar are ui::Icon; the clock is a ui::Label; the bar background + separators are
// app-drawn. Off kernel widgets.
//
#include "kapi.h"
#include "applib.h"
#include "uikit.hpp"

#define BAR	60		// bar thickness (cross axis)
#define MAXLEN	760		// over-allocated canvas length (main axis)
#define ICON	40
#define INSET	10		// cross-axis inset for icons
#define STEP	44
#define QMAX	6
#define TMAX	4
#define OMAX	16
#define CLOCK_W	44
#define BG	0x00222a36
#define SEP	0x00425068

static unsigned  *fb;
static ui::Ui    *g_ui;
static int        g_pos = 1, g_vert = 1, g_stride = BAR, g_sw = 800, g_sh = 600;

static char       g_ql_name[QMAX][24]; static ui::Icon *g_ql_icon[QMAX]; static int g_ql_count = 0;
static char       g_tb_name[TMAX][24]; static ui::Icon *g_tb_icon[TMAX];
static ui::Icon  *g_apps_icon = 0;
static ui::Label *g_clock = 0;
static int        g_tb_start = 0, g_last_tb_count = -1, g_last_min = -1;
static int        g_content_len = BAR, g_tb_count = 0;	// persisted for the redraw

static void copy_name (char *d, const char *s) { int k = 0; for (; s[k] && k < 23; k++) d[k] = s[k]; d[k] = '\0'; }
static void place_icon (ui::Icon *ic, int a) { if (g_vert) ic->setRect (INSET, a, ICON, ICON); else ic->setRect (a, INSET, ICON, ICON); }
static void sep_at (int a)
{
	for (int t = 0; t < 2; t++)
	{
		if (g_vert) for (int x = 4; x < BAR - 4; x++) fb[(a + t) * g_stride + x] = SEP;
		else        for (int y = 4; y < BAR - 4; y++) fb[y * g_stride + (a + t)] = SEP;
	}
}
static void clear_used (int len)
{
	if (g_vert) { for (int i = 0; i < BAR * len; i++) fb[i] = BG; }
	else        { for (int y = 0; y < BAR; y++) for (int x = 0; x < len; x++) fb[y * g_stride + x] = BG; }
}

static const char *name_for (ui::Widget *w)
{
	for (int i = 0; i < g_ql_count; i++) if (g_ql_icon[i] == w) return g_ql_name[i];
	for (int s = 0; s < TMAX; s++) if (g_tb_icon[s] == w && g_tb_name[s][0] != '\0') return g_tb_name[s];
	return 0;
}
static void on_icon (ui::Widget &w) { const char *n = name_for (&w); if (n != 0) { if (kapi_raise_app (n) == 0) kapi_launch (n); } }
static void on_apps (ui::Widget &) { kapi_toggle_app ("applist"); }
static void on_evt (unsigned long s, int ev, long v) { g_ui->onEvent (s, ev, v); }

static char g_open[OMAX][24]; static int g_open_count = 0;
static void refresh_open (void)
{
	static char buf[512];
	kapi_list_windows (buf, sizeof (buf));
	g_open_count = 0;
	int i = 0;
	while (buf[i] != '\0' && g_open_count < OMAX)
	{
		char name[24]; int li = 0;
		while (buf[i] != '\0' && buf[i] != '\n') { if (li < 23) name[li++] = buf[i]; i++; }
		if (buf[i] == '\n') i++;
		name[li] = '\0';
		if (li > 0) copy_name (g_open[g_open_count++], name);
	}
}
static int is_open (const char *n)   { for (int i = 0; i < g_open_count; i++) if (ax_streq (g_open[i], n)) return 1; return 0; }
static int is_pinned (const char *n) { for (int i = 0; i < g_ql_count; i++) if (ax_streq (g_ql_name[i], n)) return 1; return 0; }

static void redraw (void)		// bar background + separators, then the widgets on top
{
	clear_used (g_content_len);
	sep_at (48);					// below the apps button
	sep_at (g_tb_start - 4);				// launchers | running
	sep_at (g_tb_start + g_tb_count * STEP + 1);	// running | clock
	g_ui->drawAll ();
	present ();
}

static void load_quicklaunch (ui::Ui &ui)
{
	void *f = kapi_open ("SD:etc/quicklaunch.txt");
	if (f == 0) return;
	static char buf[512];
	int n = kapi_read (f, buf, sizeof (buf) - 1);
	kapi_close (f);
	if (n <= 0) return;
	buf[n] = '\0';
	int i = 0;
	while (i < n && g_ql_count < QMAX)
	{
		char line[24]; int li = 0;
		while (i < n && buf[i] != '\n' && buf[i] != '\r') { char c = buf[i++]; if (c != ' ' && c != '\t' && li < 23) line[li++] = c; }
		while (i < n && (buf[i] == '\n' || buf[i] == '\r')) i++;
		line[li] = '\0';
		if (li == 0 || line[0] == '#') continue;
		copy_name (g_ql_name[g_ql_count], line);
		char path[64];
		ax_app_path (path, sizeof (path), g_ql_name[g_ql_count], ".app/icon.bmp");
		g_ql_icon[g_ql_count] = &ui.icon (0, 0, ICON, ICON, path, "", on_icon);
		place_icon (g_ql_icon[g_ql_count], 54 + g_ql_count * STEP);
		g_ql_count++;
	}
}

static void poll_and_layout (void)	// the heavy pass (open apps, taskbar, clock, geometry)
{
	refresh_open ();
	for (int i = 0; i < g_ql_count; i++) g_ql_icon[i]->setBadge (is_open (g_ql_name[i]));

	char want[TMAX][24]; int tb_count = 0;
	for (int i = 0; i < g_open_count && tb_count < TMAX; i++)
	{
		const char *n = g_open[i];
		if (is_pinned (n) || ax_streq (n, "panel") || ax_streq (n, "applist")) continue;
		int dup = 0; for (int j = 0; j < tb_count; j++) if (ax_streq (want[j], n)) { dup = 1; break; }
		if (!dup) copy_name (want[tb_count++], n);
	}
	for (int s = 0; s < TMAX; s++)
	{
		const char *desired = (s < tb_count) ? want[s] : "";
		if (!ax_streq (g_tb_name[s], desired))
		{
			if (desired[0] != '\0') { char path[64]; ax_app_path (path, sizeof (path), desired, ".app/icon.bmp"); g_tb_icon[s]->setIcon (path); }
			else g_tb_icon[s]->setIcon (0);
			copy_name (g_tb_name[s], desired);
		}
		if (s < tb_count) { place_icon (g_tb_icon[s], g_tb_start + s * STEP); g_tb_icon[s]->setBadge (true); }
		else g_tb_icon[s]->setRect (0, 0, 0, 0);
	}
	g_tb_count = tb_count;

	int clock_len = g_vert ? 22 : (CLOCK_W + 8);
	g_content_len = g_tb_start + tb_count * STEP + 6 + clock_len;
	if (g_content_len > MAXLEN) g_content_len = MAXLEN;
	if (g_vert) kapi_resize_window (BAR, g_content_len); else kapi_resize_window (g_content_len, BAR);
	if (tb_count != g_last_tb_count)
	{
		if (g_vert) g_clock->setRect (6, g_content_len - 18, BAR - 8, 16);
		else        g_clock->setRect (g_content_len - CLOCK_W - 4, (BAR - 16) / 2, CLOCK_W, 16);
		int cross0 = g_vert ? ((g_pos == 1) ? 2 : g_sw - BAR - 2) : ((g_pos == 2) ? 2 : g_sh - BAR - 2);
		int main0 = ((g_vert ? g_sh : g_sw) - g_content_len) / 2; if (main0 < 2) main0 = 2;
		if (g_vert) kapi_move_window (cross0, main0); else kapi_move_window (main0, cross0);
		g_last_tb_count = tb_count;
	}
	int hh = 0, mm = 0;
	kapi_get_datetime (0, 0, 0, &hh, &mm, 0);
	if (mm != g_last_min) { char clk[6]; ax_fmt2 (clk, hh); clk[2] = ':'; ax_fmt2 (clk + 3, mm); clk[5] = '\0'; g_clock->setText (clk); g_last_min = mm; }

	g_ui->markDirty ();		// the poll may have changed badges/taskbar/clock
}

int main (void)
{
	app_ini_load ("config.ini");
	g_pos = app_ini_get_int (0, "position", 1); if (g_pos < 1 || g_pos > 4) g_pos = 1;
	g_vert = (g_pos == 1 || g_pos == 3);
	kapi_screen_size (&g_sw, &g_sh); if (g_sw < 320) g_sw = 800; if (g_sh < 240) g_sh = 600;

	int win_w, win_h, x0, y0, cross0;
	if (g_vert) { win_w = BAR; win_h = MAXLEN; g_stride = BAR; cross0 = (g_pos == 1) ? 2 : g_sw - BAR - 2; x0 = cross0; y0 = 2; }
	else        { win_w = MAXLEN; win_h = BAR; g_stride = MAXLEN; cross0 = (g_pos == 2) ? 2 : g_sh - BAR - 2; x0 = 2; y0 = cross0; }

	fb = kapi_create_window_ex (x0, y0, win_w, win_h, "panel", WIN_FLAG_BORDERLESS);
	if (fb == 0) return 1;

	ui::Ui ui (fb, win_w, win_h); g_ui = &ui; ui.col_bg = BG;
	clear_used (MAXLEN);

	g_apps_icon = &ui.icon (0, 0, ICON, ICON, "SD:apps/panel.app/apps.bmp", "", on_apps);
	place_icon (g_apps_icon, 4);
	load_quicklaunch (ui);
	g_tb_start = 54 + g_ql_count * STEP + 6;
	for (int s = 0; s < TMAX; s++) { g_tb_name[s][0] = '\0'; g_tb_icon[s] = &ui.icon (0, 0, 0, 0, 0, "", on_icon); }
	g_clock = &ui.label (0, 0, CLOCK_W, 16, "--:--");

	kapi_set_pointer_handler (on_evt);
	kapi_set_key_handler (on_evt);

	int frame = 0;
	for (;;)
	{
		pump_events ();
		if (frame % 10 == 0) poll_and_layout ();	// ~every 160 ms
		if (ui.dirty ()) redraw ();
		msleep (16);
		frame++;
	}
	return 0;
}
