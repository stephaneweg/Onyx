//
// applist.c -- the app-list popup launched by the panel's "apps" button. A
// borderless window showing a scrollable grid of icons (one per app under /apps,
// via kapi_list_apps). Clicking an icon launches that app and closes the popup
// (kapi_exit). A second click on the panel's "apps" button closes it too.
//
// The kernel doesn't clip widgets to the window, so scrolling is done by WHOLE rows:
// rows inside the viewport are positioned, rows outside are hidden (zero rect). A
// vertical scrollbar (value 0..100 -> top row) drives it. The popup opens centered
// on whichever edge the panel uses (apps/panel.app/config.ini position).
//
#include "kapi.h"
#include "applib.h"

#define W		240
#define H		460
#define MAXAPPS		32			// WIN_MAX_WIDGETS is 40 (title + scrollbar too)
#define COLS		3
#define LX		6			// grid left margin
#define TITLE_H		18
#define VIEW_Y		28			// grid viewport top
#define SB_W		14			// scrollbar width
#define CELLW		((W - SB_W - 4 - LX) / COLS)
#define CELLH		70
#define BAR		60			// must match the panel's bar thickness

static unsigned     *fb;
static char          g_names[MAXAPPS][24];
static unsigned long g_handles[MAXAPPS];
static int           g_count = 0;

static unsigned long g_sb = 0;			// vertical scrollbar (0 if not needed)
static int           g_vis_rows = 1;		// rows that fit in the viewport
static int           g_total_rows = 1;
static int           g_max_top = 0;		// max top-row (total - visible)

static void clearbg (unsigned c)
{
	for (int i = 0; i < W * H; i++) fb[i] = c;
}

// Position icons for a given top row: rows in [top, top+vis_rows) are shown, the
// rest hidden (zero rect, which DrawTo skips) so nothing spills past the viewport.
static void reposition (int top)
{
	if (top < 0) top = 0;
	if (top > g_max_top) top = g_max_top;
	for (int i = 0; i < g_count; i++)
	{
		int row = i / COLS, col = i % COLS;
		if (row >= top && row < top + g_vis_rows)
		{
			int x = LX + col * CELLW;
			int y = VIEW_Y + (row - top) * CELLH;
			kapi_widget_set_rect (g_handles[i], x, y, CELLW - 8, CELLH - 8);
		}
		else
		{
			kapi_widget_set_rect (g_handles[i], 0, 0, 0, 0);	// hidden
		}
	}
}

static void on_scroll (unsigned long sender, int ev, long val)
{
	(void) sender; (void) ev; (void) val;
	if (g_max_top <= 0) return;
	int v = kapi_widget_get_value (g_sb);		// 0 (top) .. 100 (bottom)
	int top = (v * g_max_top + 50) / 100;
	reposition (top);
}

// Launch the clicked app, then close the popup.
static void on_icon (unsigned long sender, int ev, long val)
{
	(void) ev; (void) val;
	for (int i = 0; i < g_count; i++)
	{
		if (g_handles[i] == sender)
		{
			kapi_launch (g_names[i]);
			kapi_exit (0);		// popup closes on launch (does not return)
		}
	}
}

static void add_apps (void)
{
	static char list[1024];
	kapi_list_apps (list, sizeof (list));

	int i = 0;
	while (list[i] != '\0' && g_count < MAXAPPS)
	{
		char name[24];
		int li = 0;
		while (list[i] != '\0' && list[i] != '\n')
		{
			if (li < (int) sizeof (name) - 1) name[li++] = list[i];
			i++;
		}
		if (list[i] == '\n') i++;
		name[li] = '\0';
		if (li == 0) continue;
		// Hide the shell's own components from the drawer.
		if (ax_streq (name, "panel") || ax_streq (name, "applist")) continue;

		int k = 0;
		for (; name[k] != '\0' && k < (int) sizeof (g_names[0]) - 1; k++)
			g_names[g_count][k] = name[k];
		g_names[g_count][k] = '\0';

		char path[64];
		ax_app_path (path, sizeof (path), g_names[g_count], ".app/icon.bmp");
		// Created hidden; reposition() places the visible rows.
		g_handles[g_count] = kapi_add_icon (0, 0, 0, 0, path, g_names[g_count], on_icon);
		g_count++;
	}
}

int main (void)
{
	// Open next to the panel's "apps" button, on whichever edge the panel uses, and
	// centered along that edge to match the (centered) bar.
	int pos = 1;
	if (app_ini_load_path ("SD:apps/panel.app/config.ini") > 0)
		pos = app_ini_get_int (0, "position", 1);
	if (pos < 1 || pos > 4) pos = 1;

	int sw = 800, sh = 600;
	kapi_screen_size (&sw, &sh);
	if (sw < 320) sw = 800;
	if (sh < 240) sh = 600;

	int cy = (sh - H) / 2; if (cy < 4) cy = 4;	// vertical-bar: center popup vertically
	int cx = (sw - W) / 2; if (cx < 4) cx = 4;	// horizontal-bar: center popup horizontally
	int x0, y0;
	switch (pos)
	{
	case 3:  x0 = sw - BAR - 4 - W; y0 = cy;            break;	// right bar -> left of it
	case 2:  x0 = cx;               y0 = BAR + 4;        break;	// top bar -> below
	case 4:  x0 = cx;               y0 = sh - BAR - 4 - H; break;	// bottom bar -> above
	default: x0 = BAR + 4;          y0 = cy;            break;	// left bar -> right of it
	}

	fb = kapi_create_window_ex (x0, y0, W, H, "applist", WIN_FLAG_BORDERLESS);
	if (fb == 0) return 1;
	clearbg (0x00141c26);

	kapi_add_label (LX + 2, 6, W - 24, 16, "Applications");
	add_apps ();

	// Lay out the scroll geometry, add a scrollbar only if the grid overflows.
	g_total_rows = (g_count + COLS - 1) / COLS;
	g_vis_rows   = (H - VIEW_Y - 6) / CELLH;
	if (g_vis_rows < 1) g_vis_rows = 1;
	g_max_top    = (g_total_rows > g_vis_rows) ? g_total_rows - g_vis_rows : 0;
	if (g_max_top > 0)
		g_sb = kapi_add_scrollbar_v (W - SB_W - 2, VIEW_Y, SB_W, H - VIEW_Y - 6, on_scroll);
	reposition (0);

	while (!should_exit ())
	{
		pump_events ();
		msleep (16);
	}
	return 0;
}
