//
// applist/main.cpp -- the app-list popup (wtk port). A borderless window with a scrollable
// grid of wtk::Icon (one per app under /apps), driven by a wtk::Scrollbar. Clicking an icon
// launches that app and closes the popup. Icons that scroll out of view are parked off the
// canvas (the toolkit clips them away).
//
#include "kapi.h"
#include "applib.h"
#include "wtk/wtk.h"

using namespace wtk;

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
#define OFFSCREEN	(W + 64)		// park hidden icons here (clipped away)

static char       g_names[MAXAPPS][24];
static Icon      *g_icons[MAXAPPS];
static int        g_count = 0;
static Root      *g_root = 0;
static int        g_vis_rows = 1, g_total_rows = 1, g_max_top = 0;

// Show rows [top, top+vis_rows); park the rest off-canvas so nothing spills.
static void reposition (int top)
{
	if (top < 0) top = 0;
	if (top > g_max_top) top = g_max_top;
	for (int i = 0; i < g_count; i++)
	{
		int row = i / COLS, col = i % COLS;
		if (row >= top && row < top + g_vis_rows)
		{ g_icons[i]->left = LX + col * CELLW; g_icons[i]->top = VIEW_Y + (row - top) * CELLH; }
		else
		{ g_icons[i]->left = OFFSCREEN; g_icons[i]->top = VIEW_Y; }
	}
	if (g_root) g_root->invalidate (true);
}

static void on_scroll (Widget &w) { reposition (((Scrollbar &) w).value); }
static void on_icon (Widget &w) { kapi_launch (g_names[w.tag]); kapi_exit (0); }	// no return

static void add_apps (Root &root)
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
		if (ax_streq (name, "panel") || ax_streq (name, "applist")
		    || ax_streq (name, "shell")) continue;	// hide the shell components
		int k = 0; for (; name[k] != '\0' && k < 23; k++) g_names[g_count][k] = name[k];
		g_names[g_count][k] = '\0';
		char path[64];
		ax_app_path (path, sizeof (path), g_names[g_count], ".app/icon.bmp");
		g_icons[g_count] = new Icon (OFFSCREEN, VIEW_Y, CELLW - 8, CELLH - 8, path, g_names[g_count], on_icon, 0x00141c26);
		g_icons[g_count]->tag = g_count;
		root.addChild (g_icons[g_count]);
		g_count++;
	}
}

int main (void)
{
	// Centred on screen. (Was anchored next to the panel bar; the shell launches it now,
	// independent of panel -- it just opens in the middle of the display.)
	int sw = 800, sh = 600;
	kapi_screen_size (&sw, &sh);
	if (sw < 320) sw = 800;
	if (sh < 240) sh = 600;
	int x0 = (sw - W) / 2; if (x0 < 4) x0 = 4;
	int y0 = (sh - H) / 2; if (y0 < 4) y0 = 4;

	Root root (x0, y0, W, H, "applist", WIN_FLAG_BORDERLESS);
	if (root.canvas.px == 0) return 1;
	g_root = &root;
	root.setBg (0x00141c26);
	root.addChild (new Label (LX + 2, 6, W - 24, 16, "Applications", C_TEXT, 0x00141c26));
	add_apps (root);

	g_total_rows = (g_count + COLS - 1) / COLS;
	g_vis_rows   = (H - VIEW_Y - 6) / CELLH; if (g_vis_rows < 1) g_vis_rows = 1;
	g_max_top    = (g_total_rows > g_vis_rows) ? g_total_rows - g_vis_rows : 0;
	if (g_max_top > 0)
		root.addChild (new Scrollbar (W - SB_W - 2, VIEW_Y, SB_W, H - VIEW_Y - 6, true, g_max_top, 0, on_scroll));
	reposition (0);

	root.run ();
	return 0;
}
