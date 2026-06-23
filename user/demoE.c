//
// demoE.c -- multi-line editable text area + a scroll view (V/H scrollbars driving
// a scrollable content grid). The textarea is rendered by the kernel widget; the
// scroll view is app-drawn raw pixels (apps have no text font), offset by the
// scrollbar values.
//
#include "kapi.h"

#define W	320
#define H	240

// Scroll-view viewport (client-relative) and its larger virtual content.
#define VP_X	10
#define VP_Y	88
#define VP_W	252
#define VP_H	110
#define CONTENT	400			// content is CONTENT x CONTENT pixels
#define CELL	40			// grid cell size

static unsigned *fb;
static int	 g_ScrollX = 0;		// content offset shown at the viewport origin
static int	 g_ScrollY = 0;

static void clear (unsigned c)
{
	for (int i = 0; i < W * H; i++) fb[i] = c;
}

// Redraw the viewport: a CONTENT x CONTENT colour grid, offset by (g_ScrollX/Y).
static void draw_viewport (void)
{
	for (int vy = 0; vy < VP_H; vy++)
	{
		int cy = g_ScrollY + vy;
		unsigned *row = &fb[(VP_Y + vy) * W + VP_X];
		for (int vx = 0; vx < VP_W; vx++)
		{
			int cx = g_ScrollX + vx;
			unsigned col;
			if ((cx % CELL) == 0 || (cy % CELL) == 0)
			{
				col = 0x00FFFFFF;			// grid lines
			}
			else
			{
				unsigned r = (unsigned) (cy / CELL) * 25 + 30;
				unsigned g = (unsigned) (cx / CELL) * 25 + 30;
				col = (r << 16) | (g << 8) | 0x50;
			}
			row[vx] = col;
		}
	}
}

static void on_vscroll (unsigned long sender, int event, long value)
{
	(void) sender; (void) event;
	g_ScrollY = (int) value * (CONTENT - VP_H) / 100;
	draw_viewport ();
}

static void on_hscroll (unsigned long sender, int event, long value)
{
	(void) sender; (void) event;
	g_ScrollX = (int) value * (CONTENT - VP_W) / 100;
	draw_viewport ();
}

int main (void)
{
	fb = create_window (W, H, "textarea + scrollview");
	if (fb == 0)
	{
		return 1;
	}
	clear (0x00283848);

	kapi_add_label   (10, 8, 290, 14, "multi-line editable + scroll view:");
	kapi_add_textarea (10, 26, 290, 52, 0);		// click to focus, then type

	kapi_add_scrollbar_v (266, VP_Y, 12, VP_H, on_vscroll);
	kapi_add_scrollbar_h (VP_X, 202, VP_W, 12, on_hscroll);

	draw_viewport ();

	while (!should_exit ())
	{
		pump_events ();
		msleep (16);
	}

	return 0;
}
