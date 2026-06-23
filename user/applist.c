//
// applist.c -- the app-list popup launched by the panel's "apps" button. A
// borderless window showing a grid of icons (one per app under /apps, via
// kapi_list_apps). Clicking an icon launches that app and closes the popup
// (kapi_exit). A second click on the panel's "apps" button closes it too (the panel
// uses kapi_toggle_app). The shell components (panel, applist) are hidden from the
// list.
//
#include "kapi.h"
#include "applib.h"

#define W	224
#define H	300
#define MAXAPPS	15			// WIN_MAX_WIDGETS is 16 (1 used by the title)
#define COLS	3
#define CELLW	68
#define CELLH	66

static unsigned     *fb;
static char          g_names[MAXAPPS][24];
static unsigned long g_handles[MAXAPPS];
static int           g_count = 0;

static void clearbg (unsigned c)
{
	for (int i = 0; i < W * H; i++) fb[i] = c;
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
		if (li == 0)
		{
			continue;
		}
		// Hide the shell's own components from the drawer.
		if (ax_streq (name, "panel") || ax_streq (name, "applist"))
		{
			continue;
		}

		int k = 0;
		for (; name[k] != '\0' && k < (int) sizeof (g_names[0]) - 1; k++)
		{
			g_names[g_count][k] = name[k];
		}
		g_names[g_count][k] = '\0';

		int col = g_count % COLS;
		int row = g_count / COLS;
		int x = 12 + col * CELLW;
		int y = 30 + row * CELLH;

		char path[64];
		ax_app_path (path, sizeof (path), g_names[g_count], ".app/icon.bmp");
		g_handles[g_count] = kapi_add_icon (x, y, CELLW - 8, CELLH - 8, path,
						    g_names[g_count], on_icon);
		g_count++;
	}
}

// Mirror the panel's edge metrics so the popup hugs the "apps" button.
#define BAR	60

int main (void)
{
	// Open next to the panel's apps button, on whichever edge the panel uses.
	int pos = 1;
	if (app_ini_load_path ("SD:apps/panel.app/config.ini") > 0)
		pos = app_ini_get_int (0, "position", 1);
	if (pos < 1 || pos > 4) pos = 1;

	int sw = 800, sh = 600;
	kapi_screen_size (&sw, &sh);
	if (sw < 320) sw = 800;
	if (sh < 240) sh = 600;

	int x0, y0;
	switch (pos)
	{
	case 3:  x0 = sw - BAR - 4 - W; y0 = 48;            break;	// right bar -> left of it
	case 2:  x0 = 4;                y0 = BAR + 4;        break;	// top bar -> below
	case 4:  x0 = 4;                y0 = sh - BAR - 4 - H; break;	// bottom bar -> above
	default: x0 = BAR + 4;          y0 = 48;            break;	// left bar -> right of it
	}

	fb = kapi_create_window_ex (x0, y0, W, H, "applist", WIN_FLAG_BORDERLESS);
	if (fb == 0)
	{
		return 1;
	}
	clearbg (0x00141c26);

	kapi_add_label (12, 8, W - 24, 16, "Applications");
	add_apps ();

	while (!should_exit ())
	{
		pump_events ();
		msleep (16);
	}
	return 0;
}
