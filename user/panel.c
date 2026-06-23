//
// panel.c -- the Maynard-style launcher bar, which doubles as a taskbar. A
// borderless window pinned to one edge of the screen; its position is configurable
// via SD:apps/panel.app/config.ini (position = 1 left, 2 top, 3 right, 4 bottom).
// Left/right give a vertical bar, top/bottom a horizontal one. Along the bar's main
// axis, from the pinned corner outward:
//
//   [apps] button (9-squares) -- toggles the app-list popup
//   --- separator ---
//   quicklaunch icons (apps/quicklaunch.txt) -- launch, or RAISE if already open
//   --- separator ---
//   open-apps section -- one icon per running app not already pinned
//   --- separator ---
//   clock (kapi_get_datetime)
//
// An open app shows a green triangle badge on its icon; clicking an open app's icon
// raises its window instead of launching a new instance. Sets the wallpaper at boot.
//
#include "kapi.h"
#include "applib.h"

#define BAR		60		// bar thickness (cross axis)
#define MAXLEN		760		// over-allocated canvas length (main axis)
#define ICON		40
#define INSET		10		// cross-axis inset for icons
#define STEP		44
#define QMAX		6		// max quicklaunch icons
#define TMAX		4		// max open-apps (taskbar) slots
#define OMAX		16		// max windows reported
#define CLOCK_W		44		// clock width when horizontal

#define BG		0x00222a36
#define SEP		0x00425068

static unsigned     *fb;

// Orientation / placement (resolved from config.ini at startup).
static int           g_pos = 1;		// 1 left / 2 top / 3 right / 4 bottom
static int           g_vert = 1;	// vertical bar (left/right)?
static int           g_stride = BAR;	// canvas row width (BAR vertical, MAXLEN horiz)
static int           g_sw = 800, g_sh = 600;	// screen size

// Quicklaunch (pinned) icons.
static char          g_ql_name[QMAX][24];
static unsigned long g_ql_handle[QMAX];
static int           g_ql_count = 0;

// Open-apps (taskbar) slots -- repurposed as apps open/close.
static char          g_tb_name[TMAX][24];	// app currently shown in this slot ("" = empty)
static unsigned long g_tb_handle[TMAX];

static unsigned long g_apps_handle = 0;
static unsigned long g_clock_handle = 0;

static int           g_tb_start = 0;		// main-axis offset where the taskbar begins
static int           g_last_tb_count = -1;	// to detect layout changes
static int           g_last_min = -1;

// ---- helpers ----------------------------------------------------------------

static void copy_name (char *dst, const char *src)
{
	int k = 0;
	for (; src[k] != '\0' && k < 23; k++) dst[k] = src[k];
	dst[k] = '\0';
}

// Place an icon at main-axis offset `a` (cross axis = INSET).
static void place_icon (unsigned long h, int a)
{
	if (g_vert) kapi_widget_set_rect (h, INSET, a, ICON, ICON);
	else        kapi_widget_set_rect (h, a, INSET, ICON, ICON);
}

// Draw a separator (2 px line perpendicular to the main axis) at offset `a`.
static void sep_at (int a)
{
	for (int t = 0; t < 2; t++)
	{
		if (g_vert)
			for (int x = 4; x < BAR - 4; x++) fb[(a + t) * g_stride + x] = SEP;
		else
			for (int y = 4; y < BAR - 4; y++) fb[y * g_stride + (a + t)] = SEP;
	}
}

// Clear the used part of the canvas (main-axis length `len`).
static void clear_used (int len)
{
	if (g_vert)
	{
		for (int i = 0; i < BAR * len; i++) fb[i] = BG;
	}
	else
	{
		for (int y = 0; y < BAR; y++)
			for (int x = 0; x < len; x++) fb[y * g_stride + x] = BG;
	}
}

// Find the app name bound to a clicked widget handle (quicklaunch or taskbar).
static const char *name_for (unsigned long h)
{
	for (int i = 0; i < g_ql_count; i++)
		if (g_ql_handle[i] == h) return g_ql_name[i];
	for (int s = 0; s < TMAX; s++)
		if (g_tb_handle[s] == h && g_tb_name[s][0] != '\0') return g_tb_name[s];
	return 0;
}

static void on_icon (unsigned long sender, int ev, long val)
{
	(void) ev; (void) val;
	const char *n = name_for (sender);
	if (n != 0)
	{
		if (kapi_raise_app (n) == 0)	// already open -> raise; else launch
		{
			kapi_launch (n);
		}
	}
}

static void on_apps (unsigned long sender, int ev, long val)
{
	(void) sender; (void) ev; (void) val;
	kapi_toggle_app ("applist");
}

// ---- open-app set -----------------------------------------------------------

static char g_open[OMAX][24];
static int  g_open_count = 0;

static void refresh_open (void)
{
	static char buf[512];
	kapi_list_windows (buf, sizeof (buf));
	g_open_count = 0;

	int i = 0;
	while (buf[i] != '\0' && g_open_count < OMAX)
	{
		char name[24];
		int li = 0;
		while (buf[i] != '\0' && buf[i] != '\n')
		{
			if (li < 23) name[li++] = buf[i];
			i++;
		}
		if (buf[i] == '\n') i++;
		name[li] = '\0';
		if (li > 0) copy_name (g_open[g_open_count++], name);
	}
}

static int is_open (const char *n)
{
	for (int i = 0; i < g_open_count; i++)
		if (ax_streq (g_open[i], n)) return 1;
	return 0;
}

static int is_pinned (const char *n)
{
	for (int i = 0; i < g_ql_count; i++)
		if (ax_streq (g_ql_name[i], n)) return 1;
	return 0;
}

// ---- layout / drawing -------------------------------------------------------

// Redraw the canvas background + separators for a given taskbar count.
static void redraw_bg (int tb_count, int content_len)
{
	clear_used (content_len);
	sep_at (48);						// below the apps button
	sep_at (g_tb_start - 4);				// launchers | running
	sep_at (g_tb_start + tb_count * STEP + 1);		// running | clock
}

// Move the clock label to the far end of the bar (main-axis = content_len).
static void place_clock (int content_len)
{
	if (g_vert)
		kapi_widget_set_rect (g_clock_handle, 6, content_len - 18, BAR - 8, 16);
	else
		kapi_widget_set_rect (g_clock_handle, content_len - CLOCK_W - 4,
				      (BAR - 16) / 2, CLOCK_W, 16);
}

// ---- main -------------------------------------------------------------------

static void load_quicklaunch (void)
{
	void *f = kapi_open ("SD:apps/quicklaunch.txt");
	if (f == 0) return;
	static char buf[512];
	int n = kapi_read (f, buf, sizeof (buf) - 1);
	kapi_close (f);
	if (n <= 0) return;
	buf[n] = '\0';

	int i = 0;
	while (i < n && g_ql_count < QMAX)
	{
		char line[24];
		int li = 0;
		while (i < n && buf[i] != '\n' && buf[i] != '\r')
		{
			char c = buf[i++];
			if (c != ' ' && c != '\t' && li < 23) line[li++] = c;
		}
		while (i < n && (buf[i] == '\n' || buf[i] == '\r')) i++;
		line[li] = '\0';
		if (li == 0 || line[0] == '#') continue;

		copy_name (g_ql_name[g_ql_count], line);
		char path[64];
		ax_app_path (path, sizeof (path), g_ql_name[g_ql_count], ".app/icon.bmp");
		g_ql_handle[g_ql_count] =
			kapi_add_icon (0, 0, ICON, ICON, path, "", on_icon);
		place_icon (g_ql_handle[g_ql_count], 54 + g_ql_count * STEP);
		g_ql_count++;
	}
}

int main (void)
{
	// Resolve placement from config.ini (own folder); default to the left edge.
	app_ini_load ("config.ini");
	g_pos = app_ini_get_int (0, "position", 1);
	if (g_pos < 1 || g_pos > 4) g_pos = 1;
	g_vert = (g_pos == 1 || g_pos == 3);

	kapi_screen_size (&g_sw, &g_sh);
	if (g_sw < 320) g_sw = 800;			// sane fallback
	if (g_sh < 240) g_sh = 600;

	// Window geometry: a bar of thickness BAR, over-allocated to MAXLEN along its
	// main axis, pinned to the chosen edge. It resizes down to fit its content.
	int win_w, win_h, x0, y0;
	if (g_vert)
	{
		win_w = BAR; win_h = MAXLEN; g_stride = BAR;
		x0 = (g_pos == 1) ? 2 : g_sw - BAR - 2;		// left or right
		y0 = 4;
	}
	else
	{
		win_w = MAXLEN; win_h = BAR; g_stride = MAXLEN;
		x0 = 4;
		y0 = (g_pos == 2) ? 2 : g_sh - BAR - 2;		// top or bottom
	}

	fb = kapi_create_window_ex (x0, y0, win_w, win_h, "panel", WIN_FLAG_BORDERLESS);
	if (fb == 0) return 1;
	clear_used (MAXLEN);
	// Generate the wallpaper once at startup (toroidal Voronoi, blue base, varies
	// per boot) instead of loading a BMP.
	kapi_wallpaper_generate (0x004878B0, 28, 0);

	// Apps button (at the pinned corner), then the pinned quicklaunch icons.
	g_apps_handle = kapi_add_icon (0, 0, ICON, ICON, "SD:apps/panel.app/apps.bmp",
				       "", on_apps);
	place_icon (g_apps_handle, 4);
	load_quicklaunch ();
	g_tb_start = 54 + g_ql_count * STEP + 6;

	// Pre-create the open-apps slot pool, hidden until assigned.
	for (int s = 0; s < TMAX; s++)
	{
		g_tb_name[s][0] = '\0';
		g_tb_handle[s] = kapi_add_icon (0, 0, 0, 0, 0, "", on_icon);
	}

	// Clock (repositioned to the far end on every layout change).
	g_clock_handle = kapi_add_label (0, 0, CLOCK_W, 16, "--:--");

	for (;;)
	{
		refresh_open ();

		// Pinned icons: badge those that are open.
		for (int i = 0; i < g_ql_count; i++)
		{
			kapi_widget_set_value (g_ql_handle[i], is_open (g_ql_name[i]) ? 1 : 0);
		}

		// Build the taskbar list: open apps that aren't pinned and aren't shell
		// components, deduplicated, capped at TMAX.
		char want[TMAX][24];
		int tb_count = 0;
		for (int i = 0; i < g_open_count && tb_count < TMAX; i++)
		{
			const char *n = g_open[i];
			if (is_pinned (n) || ax_streq (n, "panel") || ax_streq (n, "applist"))
				continue;
			int dup = 0;
			for (int j = 0; j < tb_count; j++)
				if (ax_streq (want[j], n)) { dup = 1; break; }
			if (!dup) copy_name (want[tb_count++], n);
		}

		// Assign / hide slots; reload the icon image only when a slot's app changes.
		for (int s = 0; s < TMAX; s++)
		{
			const char *desired = (s < tb_count) ? want[s] : "";
			if (!ax_streq (g_tb_name[s], desired))
			{
				if (desired[0] != '\0')
				{
					char path[64];
					ax_app_path (path, sizeof (path), desired, ".app/icon.bmp");
					kapi_widget_set_icon (g_tb_handle[s], path);
				}
				else
				{
					kapi_widget_set_icon (g_tb_handle[s], 0);
				}
				copy_name (g_tb_name[s], desired);
			}
			if (s < tb_count)
			{
				place_icon (g_tb_handle[s], g_tb_start + s * STEP);
				kapi_widget_set_value (g_tb_handle[s], 1);	// running badge
			}
			else
			{
				kapi_widget_set_rect (g_tb_handle[s], 0, 0, 0, 0);
			}
		}

		// Resize the bar to fit, and (only when the count changes) move the clock
		// and repaint the separators.
		int clock_len = g_vert ? 22 : (CLOCK_W + 8);
		int content_len = g_tb_start + tb_count * STEP + 6 + clock_len;
		if (content_len > MAXLEN) content_len = MAXLEN;
		if (g_vert) kapi_resize_window (BAR, content_len);
		else        kapi_resize_window (content_len, BAR);
		if (tb_count != g_last_tb_count)
		{
			redraw_bg (tb_count, content_len);
			place_clock (content_len);
			g_last_tb_count = tb_count;
		}

		// Clock, refreshed on the minute.
		int hh = 0, mm = 0;
		kapi_get_datetime (0, 0, 0, &hh, &mm, 0);
		if (mm != g_last_min)
		{
			char clk[6];
			ax_fmt2 (clk, hh);
			clk[2] = ':';
			ax_fmt2 (clk + 3, mm);
			clk[5] = '\0';
			kapi_widget_set_text (g_clock_handle, clk);
			g_last_min = mm;
		}

		pump_events ();
		msleep (150);
	}
	return 0;
}
