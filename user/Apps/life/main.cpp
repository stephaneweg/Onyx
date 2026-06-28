//
// life -- Conway's Game of Life. Click cells to toggle; space run/pause, s step,
// c clear, r random. App-drawn grid via canvas-click.
//
#include "kapi.h"
#include "wtk/wtk.h"

#define GW	48
#define GH	34
#define CELL	12
#define OX	8
#define OY	24
#define W	(OX * 2 + GW * CELL)
#define H	(OY + GH * CELL + 8)

static unsigned *fb;
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

static void redraw (void)
{
	fill_rect (0, 0, W, H, 0x00101418);
	wtk::draw_text (fb, W, H, 8, 6, g_run ? "running  space:pause s c r" : "paused   space:run s c r",
			0x0090a0b0);
	for (int r = 0; r < GH; r++)
		for (int c = 0; c < GW; c++)
			if (g_cell[r][c])
				fill_rect (OX + c * CELL, OY + r * CELL, CELL - 1, CELL - 1, 0x0060e090);
}

int main (void)
{
	fb = kapi_create_window (W, H, "life");
	if (fb == 0) return 1;
	wtk::wk_decorate_window ();
	g_rng = kapi_get_ticks () | 1u;
	kapi_set_key_handler (on_key);
	kapi_set_click_handler (on_click);
	randomize ();
	while (!should_exit ())
	{
		pump_events ();
		if (g_run && ++g_frames >= 6) { step (); g_frames = 0; }
		redraw ();
		msleep (16);
	}
	return 0;
}
