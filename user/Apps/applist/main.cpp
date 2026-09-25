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

#define W		444			// square: 6 columns of 70 px + margins + scrollbar
#define H		444
#define MAXAPPS		96		// (was 32: newer apps past it were silently dropped)
#define COLS		6
#define LX		6			// grid left margin
#define VIEW_Y		28			// grid viewport top
#define SB_W		14			// scrollbar width
#define CELLW		((W - SB_W - 4 - LX) / COLS)
#define CELLH		70
#define BAR		60			// must match the panel's bar thickness
#define MENUBAR_H	34			// keep clear of the system menu bar at the top
#define GAP		6			// space between the panel and the popup
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

static bool ci_less (const char *a, const char *b)
{
	for (;; a++, b++)
	{
		char x = (*a >= 'A' && *a <= 'Z') ? (char) (*a + 32) : *a;
		char y = (*b >= 'A' && *b <= 'Z') ? (char) (*b + 32) : *b;
		if (x != y || !x) return x < y;
	}
}

// A shell component (panel, menu bar, notifications...) declares "category = Shell" in
// its app.txt: it is started by autostart and not offered in the list.
static bool is_system_app (const char *name)
{
	char path[64];
	ax_app_path (path, sizeof (path), name, ".app/app.txt");
	if (app_ini_load_path (path) < 0) return false;
	return ax_streq (app_ini_get (0, "category", ""), "Shell");
}

static void add_apps (Root &root)
{
	static char list[2048];
	kapi_list_apps (list, sizeof (list));
	int i = 0;
	while (list[i] != '\0' && g_count < MAXAPPS)
	{
		char name[24]; int li = 0;
		while (list[i] != '\0' && list[i] != '\n') { if (li < 23) name[li++] = list[i]; i++; }
		if (list[i] == '\n') i++;
		name[li] = '\0';
		if (li == 0) continue;
		if (is_system_app (name)) continue;		// shell components (category = Shell)
		// Insert in alphabetical (case-insensitive) order -- the directory order is just
		// the order the folders were copied onto the card.
		int pos = g_count;
		while (pos > 0 && ci_less (name, g_names[pos - 1])) pos--;
		for (int j = g_count; j > pos; j--)
			for (int k = 0; k < 24; k++) g_names[j][k] = g_names[j - 1][k];
		int k = 0; for (; name[k] != '\0' && k < 23; k++) g_names[pos][k] = name[k];
		g_names[pos][k] = '\0';
		g_count++;
	}
	for (int n = 0; n < g_count; n++)
	{
		char path[64];
		ax_app_path (path, sizeof (path), g_names[n], ".app/icon.bmp");
		g_icons[n] = new Icon (OFFSCREEN, VIEW_Y, CELLW - 8, CELLH - 8, path, g_names[n], on_icon, 0x00141c26);
		g_icons[n]->tag = n;
		root.addChild (g_icons[n]);
	}
}

int main (void)
{
	// Open right next to the panel, beside its "apps" button: the panel's edge comes from
	// its config.ini (position 1 left / 2 top / 3 right / 4 bottom), and along that edge
	// we align on the pointer -- it is on the button that just launched us.
	int sw = 800, sh = 600;
	kapi_screen_size (&sw, &sh);
	if (sw < 320) sw = 800;
	if (sh < 240) sh = 600;
	int pos = 3;
	if (app_ini_load_path ("SD:apps/panel.app/config.ini")) pos = app_ini_get_int (0, "position", 3);
	int cx = sw / 2, cy = sh / 2;
	kapi_cursor_pos (&cx, &cy);
	int x0, y0;
	switch (pos)
	{
	case 1:  x0 = 2 + BAR + GAP;            y0 = cy - 24; break;	// panel on the left
	case 2:  x0 = cx - 24;                  y0 = 2 + BAR + GAP; break;	// top
	case 4:  x0 = cx - 24;                  y0 = sh - 2 - BAR - GAP - H; break;	// bottom
	default: x0 = sw - 2 - BAR - GAP - W;   y0 = cy - 24; break;	// right (default)
	}
	if (x0 > sw - W - 4) x0 = sw - W - 4;
	if (x0 < 4) x0 = 4;
	if (y0 > sh - H - 4) y0 = sh - H - 4;
	if (y0 < MENUBAR_H) y0 = MENUBAR_H;

	Root root (x0, y0, W, H, "applist", WIN_FLAG_BORDERLESS | WIN_FLAG_SYSTEM);
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
