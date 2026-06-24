//
// applist/main.cpp -- the app-list popup (C++ port, on uikit.hpp). A borderless window
// with a scrollable grid of ui::Icon (one per app under /apps), driven by a ui::Scrollbar.
// Clicking an icon launches that app and closes the popup. Off kernel widgets.
//
#include "kapi.h"
#include "applib.h"
#include "uikit.hpp"

#define W		240
#define H		460
#define MAXAPPS		32
#define COLS		3
#define LX		6			// grid left margin
#define VIEW_Y		28			// grid viewport top
#define SB_W		14			// scrollbar width
#define CELLW		((W - SB_W - 4 - LX) / COLS)
#define CELLH		70
#define BAR		60			// must match the panel's bar thickness

static ui::Ui       *g_ui;
static char          g_names[MAXAPPS][24];
static ui::Icon     *g_icons[MAXAPPS];
static int           g_count = 0;
static ui::Scrollbar *g_sb = 0;
static int           g_vis_rows = 1, g_total_rows = 1, g_max_top = 0;

// Show rows [top, top+vis_rows); hide the rest (zero rect) so nothing spills.
static void reposition (int top)
{
	if (top < 0) top = 0;
	if (top > g_max_top) top = g_max_top;
	for (int i = 0; i < g_count; i++)
	{
		int row = i / COLS, col = i % COLS;
		if (row >= top && row < top + g_vis_rows)
			g_icons[i]->setRect (LX + col * CELLW, VIEW_Y + (row - top) * CELLH, CELLW - 8, CELLH - 8);
		else
			g_icons[i]->setRect (0, 0, 0, 0);
	}
	g_ui->markDirty ();
}

static void on_scroll (ui::Widget &w) { reposition (((ui::Scrollbar &) w).value); }
static void on_icon (ui::Widget &w) { kapi_launch (g_names[w.tag]); kapi_exit (0); }	// no return
static void on_evt (unsigned long s, int ev, long v) { g_ui->onEvent (s, ev, v); }

static void add_apps (ui::Ui &ui)
{
	static char list[1024];
	kapi_list_apps (list, sizeof (list));
	int i = 0;
	while (list[i] != '\0' && g_count < MAXAPPS)
	{
		char name[24]; int li = 0;
		while (list[i] != '\0' && list[i] != '\n') { if (li < 23) name[li++] = list[i]; i++; }
		if (list[i] == '\n') i++;
		name[li] = '\0';
		if (li == 0) continue;
		if (ax_streq (name, "panel") || ax_streq (name, "applist")) continue;	// hide shell
		int k = 0; for (; name[k] != '\0' && k < 23; k++) g_names[g_count][k] = name[k];
		g_names[g_count][k] = '\0';
		char path[64];
		ax_app_path (path, sizeof (path), g_names[g_count], ".app/icon.bmp");
		g_icons[g_count] = &ui.icon (0, 0, 0, 0, path, g_names[g_count], on_icon);
		g_icons[g_count]->tag = g_count;
		g_count++;
	}
}

int main (void)
{
	int pos = 1;
	if (app_ini_load_path ("SD:apps/panel.app/config.ini") > 0) pos = app_ini_get_int (0, "position", 1);
	if (pos < 1 || pos > 4) pos = 1;

	int sw = 800, sh = 600;
	kapi_screen_size (&sw, &sh);
	if (sw < 320) sw = 800;
	if (sh < 240) sh = 600;
	int cy = (sh - H) / 2; if (cy < 4) cy = 4;
	int cx = (sw - W) / 2; if (cx < 4) cx = 4;
	int x0, y0;
	switch (pos)
	{
	case 3:  x0 = sw - BAR - 4 - W; y0 = cy;              break;	// right bar -> left of it
	case 2:  x0 = cx;               y0 = BAR + 4;         break;	// top bar -> below
	case 4:  x0 = cx;               y0 = sh - BAR - 4 - H; break;	// bottom bar -> above
	default: x0 = BAR + 4;          y0 = cy;              break;	// left bar -> right of it
	}

	unsigned *fb = kapi_create_window_ex (x0, y0, W, H, "applist", WIN_FLAG_BORDERLESS);
	if (fb == 0) return 1;

	ui::Ui ui (fb, W, H); g_ui = &ui;
	ui.col_bg = 0x00141c26;
	ui.label (LX + 2, 6, W - 24, 16, "Applications");
	add_apps (ui);

	g_total_rows = (g_count + COLS - 1) / COLS;
	g_vis_rows   = (H - VIEW_Y - 6) / CELLH; if (g_vis_rows < 1) g_vis_rows = 1;
	g_max_top    = (g_total_rows > g_vis_rows) ? g_total_rows - g_vis_rows : 0;
	if (g_max_top > 0)
		g_sb = &ui.scrollbar (W - SB_W - 2, VIEW_Y, SB_W, H - VIEW_Y - 6, true, g_max_top, 0, on_scroll);
	reposition (0);

	kapi_set_pointer_handler (on_evt);
	kapi_set_key_handler (on_evt);
	while (!should_exit ())
	{
		pump_events ();
		if (ui.dirty ()) { ui.background (); ui.drawAll (); present (); }
		msleep (16);
	}
	return 0;
}
