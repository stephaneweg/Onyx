//
// demoE/main.cpp -- multi-line editable Textarea + a scroll view (C++ port). The
// Textarea (uikit.hpp) is an editable widget; the scroll view is an app-drawn colour
// grid scrolled by a vertical + horizontal uikit Scrollbar (value 0..100).
//
#include "kapi.h"
#include "uikit.hpp"

#define W	320
#define H	240
#define VP_X	10			// scroll-view viewport (client-relative)
#define VP_Y	88
#define VP_W	252
#define VP_H	110
#define CONTENT	400			// virtual content is CONTENT x CONTENT pixels
#define CELL	40

static unsigned *fb;
static ui::Ui   *g_ui;
static int g_ScrollX = 0, g_ScrollY = 0;

static void draw_viewport (void)	// CONTENT x CONTENT colour grid, offset by g_Scroll*
{
	for (int vy = 0; vy < VP_H; vy++)
	{
		int cy = g_ScrollY + vy;
		unsigned *row = &fb[(VP_Y + vy) * W + VP_X];
		for (int vx = 0; vx < VP_W; vx++)
		{
			int cx = g_ScrollX + vx;
			unsigned col;
			if ((cx % CELL) == 0 || (cy % CELL) == 0) col = 0x00FFFFFF;
			else { unsigned r = (unsigned) (cy / CELL) * 25 + 30, g = (unsigned) (cx / CELL) * 25 + 30; col = (r << 16) | (g << 8) | 0x50; }
			row[vx] = col;
		}
	}
}

static void on_vscroll (ui::Widget &w) { g_ScrollY = ((ui::Scrollbar &) w).value * (CONTENT - VP_H) / 100; g_ui->markDirty (); }
static void on_hscroll (ui::Widget &w) { g_ScrollX = ((ui::Scrollbar &) w).value * (CONTENT - VP_W) / 100; g_ui->markDirty (); }
static void on_evt (unsigned long s, int ev, long v) { g_ui->onEvent (s, ev, v); }

int main (void)
{
	fb = kapi_create_window (W, H, "textarea + scrollview");
	if (fb == 0) return 1;

	ui::Ui ui (fb, W, H); g_ui = &ui;
	ui.col_bg = 0x00283848;

	ui.label (10, 8, 290, 14, "multi-line editable + scroll view:");
	ui::Textarea &ta = ui.textarea (10, 26, 290, 52, 1024);
	ta.setContent ("click to focus, then type.\nmulti-line editing in the\nuser-side uikit Textarea.");
	ui.scrollbar (266, VP_Y, 12, VP_H, true,  100, 0, on_vscroll);	// vertical
	ui.scrollbar (VP_X, 202, VP_W, 12, false, 100, 0, on_hscroll);	// horizontal

	kapi_set_pointer_handler (on_evt);
	kapi_set_key_handler (on_evt);

	while (!should_exit ())
	{
		pump_events ();
		if (ui.dirty ())
		{
			for (int i = 0; i < W * H; i++) fb[i] = ui.col_bg;	// clear (app-drawn grid + widgets on top)
			draw_viewport ();
			ui.drawAll ();
			present ();
		}
		msleep (16);
	}
	return 0;
}
