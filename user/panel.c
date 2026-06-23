//
// panel.c -- the Maynard-style sidebar (the shell). A borderless window pinned at
// the left edge with:
//   * a column of quicklaunch icons read from SD:apps/quicklaunch.txt (each
//     launches its app via kapi_launch),
//   * an "apps" button (the 9-squares glyph) that toggles the app-list popup
//     (kapi_toggle_app "applist" -- a second click closes it),
//   * a clock at the bottom (kapi_get_datetime).
// It also sets the desktop wallpaper at startup. Being borderless, it has no close
// box (it runs for the session) and is not draggable; app windows can cover it and
// a click raises it again (normal z-order).
//
#include "kapi.h"
#include "applib.h"

#define W	60
#define H	462
#define MAXQ	12			// max quicklaunch entries

static unsigned     *fb;
static char          g_names[MAXQ][24];	// app folder name per quicklaunch icon
static unsigned long g_handles[MAXQ];	// the icon widget handle
static int           g_count = 0;
static unsigned long g_clock = 0;

static void clearbg (unsigned c)
{
	for (int i = 0; i < W * H; i++) fb[i] = c;
}

// Launch the app whose icon was clicked (matched by widget handle).
static void on_icon (unsigned long sender, int ev, long val)
{
	(void) ev; (void) val;
	for (int i = 0; i < g_count; i++)
	{
		if (g_handles[i] == sender)
		{
			kapi_launch (g_names[i]);
			return;
		}
	}
}

static void on_apps (unsigned long sender, int ev, long val)
{
	(void) sender; (void) ev; (void) val;
	kapi_toggle_app ("applist");		// open, or close if already open
}

// Read SD:apps/quicklaunch.txt (one app name per line) and add an icon per entry.
static void load_quicklaunch (void)
{
	void *f = kapi_open ("SD:apps/quicklaunch.txt");
	if (f == 0)
	{
		return;
	}
	static char buf[512];
	int n = kapi_read (f, buf, sizeof (buf) - 1);
	kapi_close (f);
	if (n <= 0)
	{
		return;
	}
	buf[n] = '\0';

	int y = 6;
	int i = 0;
	while (i < n && g_count < MAXQ)
	{
		char line[24];
		int li = 0;
		while (i < n && buf[i] != '\n' && buf[i] != '\r')
		{
			char c = buf[i++];
			if (c != ' ' && c != '\t' && li < (int) sizeof (line) - 1)
			{
				line[li++] = c;
			}
		}
		while (i < n && (buf[i] == '\n' || buf[i] == '\r')) i++;
		line[li] = '\0';
		if (li == 0 || line[0] == '#')
		{
			continue;
		}

		// Store the name, then add an icon (icon.bmp from the app's folder).
		int k = 0;
		for (; line[k] != '\0' && k < (int) sizeof (g_names[0]) - 1; k++)
		{
			g_names[g_count][k] = line[k];
		}
		g_names[g_count][k] = '\0';

		char path[64];
		ax_app_path (path, sizeof (path), g_names[g_count], ".app/icon.bmp");
		g_handles[g_count] = kapi_add_icon (8, y, 44, 44, path, "", on_icon);
		g_count++;
		y += 50;
	}
}

int main (void)
{
	fb = kapi_create_window_ex (2, 4, W, H, "panel", WIN_FLAG_BORDERLESS);
	if (fb == 0)
	{
		return 1;
	}
	clearbg (0x00222a36);
	kapi_set_wallpaper ("SD:apps/panel.app/wallpaper.bmp");

	load_quicklaunch ();

	// "apps" button (9-squares glyph) above the clock, and the clock at the bottom.
	kapi_add_icon (8, H - 96, 44, 44, "SD:apps/panel.app/apps.bmp", "", on_apps);
	g_clock = kapi_add_label (6, H - 22, W - 8, 16, "--:--");

	int last_min = -1;
	while (!should_exit ())
	{
		int hh = 0, mm = 0;
		kapi_get_datetime (0, 0, 0, &hh, &mm, 0);
		if (mm != last_min)			// repaint the clock only on change
		{
			char clk[6];
			ax_fmt2 (clk, hh);
			clk[2] = ':';
			ax_fmt2 (clk + 3, mm);
			clk[5] = '\0';
			kapi_widget_set_text (g_clock, clk);
			last_min = mm;
		}

		pump_events ();
		msleep (50);
	}
	return 0;
}
