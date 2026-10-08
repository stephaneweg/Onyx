//
// life -- Conway's Game of Life. Click cells to toggle; space run/pause, s step,
// c clear, r random. App-drawn grid via canvas-click.
//
#include "appkit/appkit.h"
#include "uikit/uikit.h"

#define GW	48
#define GH	34
#define CELL	12
#define OX	8
#define OY	28
#define W	(OX * 2 + GW * CELL)
#define H	(OY + GH * CELL + 8)

static unsigned *fb;
static uikit::Canvas g_cv, g_bg;		// the window's canvas; its static background, drawn once
static unsigned char g_cell[GH][GW], g_next[GH][GW];
static int g_run = 0, g_frames = 0, g_gen = 0;
static unsigned g_rng;

static unsigned rnd (void) { g_rng = g_rng * 1103515245u + 12345u; return g_rng >> 16; }

static void clear (void) { for (int r = 0; r < GH; r++) for (int c = 0; c < GW; c++) g_cell[r][c] = 0; g_gen = 0; }
static void randomize (void)
{
	for (int r = 0; r < GH; r++) for (int c = 0; c < GW; c++) g_cell[r][c] = (rnd () % 4 == 0);
	g_gen = 0;
}

static void step (void)
{
	for (int r = 0; r < GH; r++)
		for (int c = 0; c < GW; c++)
		{
			int n = 0;
			for (int dr = -1; dr <= 1; dr++)
				for (int dc = -1; dc <= 1; dc++)
				{
					if (!dr && !dc) continue;
					int rr = r + dr, cc = c + dc;
					if (rr >= 0 && rr < GH && cc >= 0 && cc < GW && g_cell[rr][cc]) n++;
				}
			g_next[r][c] = (g_cell[r][c]) ? (n == 2 || n == 3) : (n == 3);
		}
	for (int r = 0; r < GH; r++) for (int c = 0; c < GW; c++) g_cell[r][c] = g_next[r][c];
	g_gen++;
}

static void on_key (unsigned long s, int ev, long key)
{
	(void) s;
	if (ev != GUI_EVENT_KEY) return;
	if (key == ' ') g_run = !g_run;
	else if (key == 's' || key == 'S') step ();
	else if (key == 'c' || key == 'C') clear ();
	else if (key == 'r' || key == 'R') randomize ();
}

static void on_click (unsigned long s, int ev, long val)
{
	(void) s;
	if (ev != GUI_EVENT_CANVAS_CLICK && ev != GUI_EVENT_CANVAS_MOTION) return;
	int x = (int) ((val >> 16) & 0xFFFF), y = (int) (val & 0xFFFF);
	int c = (x - OX) / CELL, r = (y - OY) / CELL;
	if (c < 0 || c >= GW || r < 0 || r >= GH) return;
	if (ev == GUI_EVENT_CANVAS_MOTION) g_cell[r][c] = 1;	// drag paints alive
	else g_cell[r][c] ^= 1;
}

static void fill_rect (int x, int y, int w, int h, unsigned c)
{
	for (int yy = y; yy < y + h && yy < H; yy++)
		for (int xx = x; xx < x + w && xx < W; xx++)
			if (xx >= 0 && yy >= 0) fb[yy * W + xx] = c;
}

// The theme's look (uikit/paint.h): the face around, the grid in a sunken well (its own dark);
// drawn once into g_bg, copied at each frame.
static void paint_bg (void)
{
	using namespace uikit;
	g_bg.alloc (W, H);
	g_bg.clear (C_BG);
	uk_sunken (g_bg, OX - 3, OY - 3, GW * CELL + 5, GH * CELL + 5, 5, 0x00101418);
}

static void redraw (void)
{
	using namespace uikit;
	g_cv.putOther (g_bg, 0, 0, false);
	int fw = kapi_font_width ();
	uk_text_l (g_cv, OX, 3, 20, g_run ? "Running" : "Paused", C_TEXT, 2);
	uk_text_l (g_cv, OX + 9 * fw, 3, 20, g_run ? "space: pause  s: step  c: clear  r: random"
					      : "space: run  s: step  c: clear  r: random", C_DIS);
	for (int r = 0; r < GH; r++)
		for (int c = 0; c < GW; c++)
			if (g_cell[r][c])
				fill_rect (OX + c * CELL, OY + r * CELL, CELL - 1, CELL - 1, 0x0060e090);
}

int main (void)
{
	fb = uk_win_create (W, H, "life");
	if (fb == 0) return 1;
	uikit::uk_decorate_window ();			// (reads the theme: the palette)
	g_cv.adopt (fb, W, H);
	paint_bg ();
	g_rng = kapi_get_ticks () | 1u;
	uk_win_on_key (on_key);
	uk_win_on_click (on_click);
	randomize ();
	while (!should_exit ())
	{
		pump_events ();
		if (g_run && ++g_frames >= 6) { step (); g_frames = 0; }
		redraw ();
		present ();				// (the frame drawn into the canvas: shown -- the compositor redraws only what it is told)
		msleep (16);
	}
	return 0;
}
