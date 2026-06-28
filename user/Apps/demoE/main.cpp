//
// demoE/main.cpp -- multi-line editable Textarea + a scroll view (wtk port). The
// Textarea (wtk) is an editable widget; the scroll view is a custom Widget drawing an
// app-owned colour grid, scrolled by a vertical + horizontal wtk Scrollbar (value 0..100).
//
#include "wtk/wtk.h"

using namespace wtk;

#define W	320
#define H	240
#define VP_X	10			// scroll-view viewport (root-relative)
#define VP_Y	88
#define VP_W	252
#define VP_H	110
#define CONTENT	400			// virtual content is CONTENT x CONTENT pixels
#define CELL	40

// A viewport onto a CONTENT x CONTENT colour grid, offset by offx/offy (its own canvas).
class GridView : public Widget
{
public:
	int offx, offy;
	GridView (int l, int t, int w, int h) : Widget (l, t, w, h), offx (0), offy (0) {}
	void onDraw () override
	{
		for (int vy = 0; vy < height; vy++)
		{
			int cy = offy + vy;
			for (int vx = 0; vx < width; vx++)
			{
				int cx = offx + vx;
				unsigned col;
				if ((cx % CELL) == 0 || (cy % CELL) == 0) col = 0x00FFFFFF;
				else { unsigned r = (unsigned) (cy / CELL) * 25 + 30, g = (unsigned) (cx / CELL) * 25 + 30; col = (r << 16) | (g << 8) | 0x50; }
				canvas.px[vy * canvas.stride + vx] = col;	// stride, not width (grow-only buffers)
			}
		}
	}
};

static GridView *g_grid;

static void on_vscroll (Widget &w) { g_grid->offy = ((Scrollbar &) w).value * (CONTENT - VP_H) / 100; g_grid->invalidate (true); }
static void on_hscroll (Widget &w) { g_grid->offx = ((Scrollbar &) w).value * (CONTENT - VP_W) / 100; g_grid->invalidate (true); }

int main (void)
{
	Root root (W, H, "textarea + scrollview");
	root.setBg (0x00283848);

	root.addChild (new Label (10, 8, 290, 14, "multi-line editable + scroll view:", C_TEXT, 0x00283848));

	Textarea *ta = new Textarea (10, 26, 290, 52, 1024);
	ta->setContent ("click to focus, then type.\nmulti-line editing in the\nwtk Textarea.");
	root.addChild (ta);

	g_grid = new GridView (VP_X, VP_Y, VP_W, VP_H);
	root.addChild (g_grid);
	root.addChild (new Scrollbar (266, VP_Y, 12, VP_H, true,  100, 0, on_vscroll));	// vertical
	root.addChild (new Scrollbar (VP_X, 202, VP_W, 12, false, 100, 0, on_hscroll));	// horizontal

	root.run ();
	return 0;
}
