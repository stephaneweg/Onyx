//
// panel.c -- the Maynard-style sidebar, which doubles as a taskbar. A borderless,
// left-pinned window whose height grows/shrinks as apps open and close
// (kapi_resize_window over an over-allocated canvas). Top to bottom:
//
//   [apps] button (9-squares) -- toggles the app-list popup
//   --- separator ---
//   quicklaunch icons (apps/quicklaunch.txt) -- launch, or RAISE if already open
//   --- separator ---
//   open-apps section -- one icon per running app not already pinned
//   --- separator ---
//   clock (kapi_get_datetime)
//
// An open app shows a green triangle badge on its icon (whether pinned or in the
// open-apps section); clicking an open app's icon raises its window instead of
// launching a new instance. Sets the desktop wallpaper at startup.
//
#include "kapi.h"
#include "applib.h"

#define W		60
#define WINH		472		// max (over-allocated) canvas height
#define ICON		40
#define X		10
#define STEP		44
#define QMAX		6		// max quicklaunch icons
#define TMAX		4		// max open-apps (taskbar) slots
#define OMAX		16		// max windows reported

#define BG		0x00222a36
#define SEP		0x00425068

static unsigned     *fb;

// Quicklaunch (pinned) icons.
static char          g_ql_name[QMAX][24];
static unsigned long g_ql_handle[QMAX];
static int           g_ql_count = 0;

// Open-apps (taskbar) slots -- repurposed as apps open/close.
static char          g_tb_name[TMAX][24];	// app currently shown in this slot ("" = empty)
static unsigned long g_tb_handle[TMAX];

static unsigned long g_apps_handle = 0;
static unsigned long g_clock_handle = 0;

static int           g_tb_start = 0;		// canvas-y where the taskbar section begins
static int           g_last_tb_count = -1;	// to detect layout changes
static int           g_last_min = -1;

// ---- helpers ----------------------------------------------------------------

static void copy_name (char *dst, const char *src)
{
	int k = 0;
	for (; src[k] != '\0' && k < 23; k++) dst[k] = src[k];
	dst[k] = '\0';
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

static void clear_to (int h)
{
	for (int i = 0; i < W * h; i++) fb[i] = BG;
}

static void hline (int y)
{
	for (int t = 0; t < 2; t++)
		for (int x = 4; x < W - 4; x++)
			fb[(y + t) * W + x] = SEP;
}

// Redraw the canvas background + separators for a given taskbar count.
static void redraw_bg (int tb_count, int content_h)
{
	clear_to (content_h);
	hline (48);						// below the apps button
	hline (g_tb_start - 4);					// launchers | running
	hline (g_tb_start + tb_count * STEP + 1);		// running | clock
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

	int ql_start = 54;
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
			kapi_add_icon (X, ql_start + g_ql_count * STEP, ICON, ICON,
				       path, "", on_icon);
		g_ql_count++;
	}
}

int main (void)
{
	fb = kapi_create_window_ex (2, 4, W, WINH, "panel", WIN_FLAG_BORDERLESS);
	if (fb == 0) return 1;
	clear_to (WINH);
	// Generate the wallpaper once at startup (toroidal Voronoi, blue base, varies
	// per boot) instead of loading a BMP.
	kapi_wallpaper_generate (0x004878B0, 28, 0);

	// Apps button (top), then the pinned quicklaunch icons.
	g_apps_handle = kapi_add_icon (X, 4, ICON, ICON, "SD:apps/panel.app/apps.bmp",
				       "", on_apps);
	load_quicklaunch ();
	g_tb_start = 54 + g_ql_count * STEP + 6;

	// Pre-create the open-apps slot pool, hidden until assigned.
	for (int s = 0; s < TMAX; s++)
	{
		g_tb_name[s][0] = '\0';
		g_tb_handle[s] = kapi_add_icon (0, 0, 0, 0, 0, "", on_icon);
	}

	// Clock (repositioned to the bottom on every resize).
	g_clock_handle = kapi_add_label (6, WINH - 22, W - 8, 16, "--:--");

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
				kapi_widget_set_rect (g_tb_handle[s], X,
						      g_tb_start + s * STEP, ICON, ICON);
				kapi_widget_set_value (g_tb_handle[s], 1);	// running badge
			}
			else
			{
				kapi_widget_set_rect (g_tb_handle[s], 0, 0, 0, 0);
			}
		}

		// Resize the panel to fit, and (only when the count changes) move the clock
		// and repaint the separators.
		int content_h = g_tb_start + tb_count * STEP + 6 + 18;
		if (content_h > WINH) content_h = WINH;
		kapi_resize_window (W, content_h);
		if (tb_count != g_last_tb_count)
		{
			redraw_bg (tb_count, content_h);
			kapi_widget_set_rect (g_clock_handle, 6, content_h - 18, W - 8, 16);
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
