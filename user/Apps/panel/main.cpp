//
// panel/main.cpp -- the launcher/taskbar bar (wtk port). Borderless bar pinned to a
// screen edge (config.ini position). Apps button + quicklaunch + dynamic taskbar are
// wtk::Icon; the clock is a wtk::Label; the bar background + separators are drawn by the
// PanelRoot itself. The bar polls every ~160 ms (open apps / taskbar / clock / geometry),
// so it runs a custom loop instead of Root::run() and feeds the pointer stream into the
// widget tree by hand.
//
#include "kapi.h"
#include "applib.h"
#include "wtk/wtk.h"

using namespace wtk;

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
#define OFFP	4000		// park hidden icons off the canvas (clipped away)

static int        g_pos = 1, g_vert = 1, g_sw = 800, g_sh = 600;
static int        g_win_w = BAR, g_win_h = MAXLEN;

static char       g_ql_name[QMAX][24]; static Icon *g_ql_icon[QMAX]; static int g_ql_count = 0;
static char       g_tb_name[TMAX][24]; static Icon *g_tb_icon[TMAX];
static Icon      *g_apps_icon = 0;
static Label     *g_clock = 0;
static Root      *g_root = 0;
static int        g_tb_start = 0, g_last_tb_count = -1, g_last_min = -1;
static int        g_content_len = BAR, g_tb_count = 0;	// persisted for the redraw

static void copy_name (char *d, const char *s) { int k = 0; for (; s[k] && k < 23; k++) d[k] = s[k]; d[k] = '\0'; }
static void place_icon (Icon *ic, int a) { if (g_vert) { ic->left = INSET; ic->top = a; } else { ic->left = a; ic->top = INSET; } }
static void hide_icon  (Icon *ic) { ic->left = OFFP; ic->top = OFFP; }

static const char *name_for (Widget *w)
{
	for (int i = 0; i < g_ql_count; i++) if (g_ql_icon[i] == w) return g_ql_name[i];
	for (int s = 0; s < TMAX; s++) if (g_tb_icon[s] == w && g_tb_name[s][0] != '\0') return g_tb_name[s];
	return 0;
}
static void on_icon (Widget &w) { const char *n = name_for (&w); if (n != 0) { if (kapi_raise_app (n) == 0) kapi_launch (n); } }
static void on_apps (Widget &) { kapi_toggle_app ("applist"); }

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

// The bar: draws its own background + the three separators, behind the icon/clock widgets.
class PanelRoot : public Root
{
public:
	PanelRoot (int x, int y, int w, int h, const char *t, unsigned flags) : Root (x, y, w, h, t, flags) {}
	void sep_at (int a)
	{ if (g_vert) canvas.fillRect (4, a, BAR - 8, 2, SEP); else canvas.fillRect (a, 4, 2, BAR - 8, SEP); }
	void onDraw () override
	{
		if (g_vert) canvas.fillRect (0, 0, BAR, g_content_len, BG);
		else        canvas.fillRect (0, 0, g_content_len, BAR, BG);
		sep_at (48);					// below the apps button
		sep_at (g_tb_start - 4);				// launchers | running
		sep_at (g_tb_start + g_tb_count * STEP + 1);	// running | clock
	}
};

static void load_quicklaunch (Root &root)
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
		g_ql_icon[g_ql_count] = new Icon (0, 0, ICON, ICON, path, "", on_icon, BG);
		root.addChild (g_ql_icon[g_ql_count]);
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
		else hide_icon (g_tb_icon[s]);
	}
	g_tb_count = tb_count;

	int clock_len = g_vert ? 22 : (CLOCK_W + 8);
	g_content_len = g_tb_start + tb_count * STEP + 6 + clock_len;
	if (g_content_len > MAXLEN) g_content_len = MAXLEN;
	if (g_vert) kapi_resize_window (BAR, g_content_len); else kapi_resize_window (g_content_len, BAR);
	if (tb_count != g_last_tb_count)
	{
		if (g_vert) { g_clock->left = 6; g_clock->top = g_content_len - 18; g_clock->resizeTo (BAR - 8, 16); }
		else        { g_clock->left = g_content_len - CLOCK_W - 4; g_clock->top = (BAR - 16) / 2; g_clock->resizeTo (CLOCK_W, 16); }
		int cross0 = g_vert ? ((g_pos == 1) ? 2 : g_sw - BAR - 2) : ((g_pos == 2) ? 2 : g_sh - BAR - 2);
		int main0 = ((g_vert ? g_sh : g_sw) - g_content_len) / 2; if (main0 < 2) main0 = 2;
		if (g_vert) kapi_move_window (cross0, main0); else kapi_move_window (main0, cross0);
		g_last_tb_count = tb_count;
	}
	int hh = 0, mm = 0;
	kapi_get_datetime (0, 0, 0, &hh, &mm, 0);
	if (mm != g_last_min) { char clk[6]; ax_fmt2 (clk, hh); clk[2] = ':'; ax_fmt2 (clk + 3, mm); clk[5] = '\0'; g_clock->setText (clk); g_last_min = mm; }

	if (g_root) g_root->invalidate (true);		// the poll may have changed badges/taskbar/clock
}

// Feed the kapi pointer stream into the widget tree (Root::ptrEvent is private, so the
// custom loop -- needed for the periodic poll -- routes events itself).
static void ptr_event (unsigned long, int ev, long v)
{
	static int bl = 0, br = 0, bm = 0;
	if (g_root == 0) return;
	int c = GUI_PTR_CHANGED (v);
	switch (ev)
	{
	case GUI_EVENT_PTR_DOWN: if (c & 1) bl = 1; if (c & 2) br = 1; if (c & 4) bm = 1; break;
	case GUI_EVENT_PTR_UP:   if (c & 1) bl = 0; if (c & 2) br = 0; if (c & 4) bm = 0; break;
	case GUI_EVENT_PTR_LEAVE: g_root->handleMouse (-1, -1, 0, 0, 0, 0); return;
	case GUI_EVENT_PTR_WHEEL: g_root->handleMouse (GUI_PTR_X (v), GUI_PTR_Y (v), bl, br, bm, GUI_PTR_WHEEL (v)); return;
	default: break;
	}
	g_root->handleMouse (GUI_PTR_X (v), GUI_PTR_Y (v), bl, br, bm, 0);
}

int main (void)
{
	app_ini_load ("config.ini");
	g_pos = app_ini_get_int (0, "position", 1); if (g_pos < 1 || g_pos > 4) g_pos = 1;
	g_vert = (g_pos == 1 || g_pos == 3);
	kapi_screen_size (&g_sw, &g_sh); if (g_sw < 320) g_sw = 800; if (g_sh < 240) g_sh = 600;

	int x0, y0, cross0;
	if (g_vert) { g_win_w = BAR; g_win_h = MAXLEN; cross0 = (g_pos == 1) ? 2 : g_sw - BAR - 2; x0 = cross0; y0 = 2; }
	else        { g_win_w = MAXLEN; g_win_h = BAR; cross0 = (g_pos == 2) ? 2 : g_sh - BAR - 2; x0 = 2; y0 = cross0; }

	PanelRoot root (x0, y0, g_win_w, g_win_h, "panel", WIN_FLAG_BORDERLESS);
	if (root.canvas.px == 0) return 1;
	g_root = &root;

	g_apps_icon = new Icon (0, 0, ICON, ICON, "SD:apps/panel.app/apps.bmp", "", on_apps, BG);
	root.addChild (g_apps_icon);
	place_icon (g_apps_icon, 4);
	load_quicklaunch (root);
	g_tb_start = 54 + g_ql_count * STEP + 6;
	for (int s = 0; s < TMAX; s++)
	{
		g_tb_name[s][0] = '\0';
		g_tb_icon[s] = new Icon (OFFP, OFFP, ICON, ICON, 0, "", on_icon, BG);
		root.addChild (g_tb_icon[s]);
	}
	g_clock = new Label (0, 0, CLOCK_W, 16, "--:--", C_TEXT, BG);
	root.addChild (g_clock);

	kapi_set_pointer_handler (ptr_event);

	int frame = 0;
	for (;;)
	{
		pump_events ();
		if (frame % 10 == 0) poll_and_layout ();	// ~every 160 ms
		if (!root.valid) { root.draw (); kapi_present (); }
		msleep (16);
		frame++;
	}
	return 0;
}
