//
// minesweeper -- left-click reveals, right-click flags. Reveal a 0-cell to flood its
// neighbours; hit a mine and you lose; clear all safe cells to win. 'r' restarts.
//
#include "kapi.h"
#include "applib.h"

#define GW	16
#define GH	12
#define CELL	24
#define OX	8
#define OY	28
#define W	(OX * 2 + GW * CELL)
#define H	(OY + GH * CELL + 8)
#define NMINES	30

static unsigned *fb;
static char g_mine[GH][GW], g_open[GH][GW], g_flag[GH][GW], g_adj[GH][GW];
static int  g_lost, g_won;
static unsigned g_rng;

static unsigned rnd (void) { g_rng = g_rng * 1103515245u + 12345u; return g_rng >> 16; }

static void restart (void)
{
	for (int r = 0; r < GH; r++) for (int c = 0; c < GW; c++)
		{ g_mine[r][c] = g_open[r][c] = g_flag[r][c] = g_adj[r][c] = 0; }
	g_lost = g_won = 0;
	int placed = 0;
	while (placed < NMINES)
	{
		int c = rnd () % GW, r = rnd () % GH;
		if (!g_mine[r][c]) { g_mine[r][c] = 1; placed++; }
	}
	for (int r = 0; r < GH; r++) for (int c = 0; c < GW; c++)
	{
		int n = 0;
		for (int dr = -1; dr <= 1; dr++) for (int dc = -1; dc <= 1; dc++)
		{
			int rr = r + dr, cc = c + dc;
			if (rr >= 0 && rr < GH && cc >= 0 && cc < GW && g_mine[rr][cc]) n++;
		}
		g_adj[r][c] = (char) n;
	}
}

static void check_win (void)
{
	for (int r = 0; r < GH; r++) for (int c = 0; c < GW; c++)
		if (!g_mine[r][c] && !g_open[r][c]) return;
	g_won = 1;
}

static void reveal (int c0, int r0)
{
	static int stk[GW * GH][2];
	int sp = 0;
	if (g_open[r0][c0] || g_flag[r0][c0]) return;
	if (g_mine[r0][c0]) { g_lost = 1; return; }
	stk[sp][0] = c0; stk[sp][1] = r0; sp++;
	while (sp > 0)
	{
		sp--;
		int c = stk[sp][0], r = stk[sp][1];
		if (g_open[r][c] || g_flag[r][c]) continue;
		g_open[r][c] = 1;
		if (g_adj[r][c] == 0)
			for (int dr = -1; dr <= 1; dr++) for (int dc = -1; dc <= 1; dc++)
			{
				int rr = r + dr, cc = c + dc;
				if (rr >= 0 && rr < GH && cc >= 0 && cc < GW
				    && !g_open[rr][cc] && !g_mine[rr][cc] && sp < GW * GH)
				{ stk[sp][0] = cc; stk[sp][1] = rr; sp++; }
			}
	}
	check_win ();
}

static void on_click (unsigned long s, int ev, long val)
{
	(void) s;
	if (ev != GUI_EVENT_CANVAS_CLICK || g_lost || g_won) return;
	unsigned btn = (unsigned) ((val >> 32) & 0xFF);
	int x = (int) ((val >> 16) & 0xFFFF), y = (int) (val & 0xFFFF);
	int c = (x - OX) / CELL, r = (y - OY) / CELL;
	if (c < 0 || c >= GW || r < 0 || r >= GH) return;
	if (btn & 2) { if (!g_open[r][c]) g_flag[r][c] ^= 1; }	// right: flag
	else reveal (c, r);					// left: reveal
}

static void on_key (unsigned long s, int ev, long key)
{
	(void) s;
	if (ev == GUI_EVENT_KEY && (key == 'r' || key == 'R')) restart ();
}

static void fill_rect (int x, int y, int w, int h, unsigned c)
{
	for (int yy = y; yy < y + h && yy < H; yy++)
		for (int xx = x; xx < x + w && xx < W; xx++)
			if (xx >= 0 && yy >= 0) fb[yy * W + xx] = c;
}

static void redraw (void)
{
	fill_rect (0, 0, W, H, 0x00202830);
	kapi_draw_text (8, 8, g_lost ? "BOOM!  r:restart"
				     : (g_won ? "CLEARED!  r:restart" : "L:reveal  R:flag  r:restart"),
			g_lost ? 0x00ff6060 : (g_won ? 0x0060ff90 : 0x0090a0b0));

	static const unsigned numcol[9] = { 0, 0x004090ff, 0x0040c060, 0x00ff6060, 0x00d080ff,
					    0x00ffa040, 0x0040d0d0, 0x00d0d0d0, 0x00a0a0a0 };
	for (int r = 0; r < GH; r++)
		for (int c = 0; c < GW; c++)
		{
			int x = OX + c * CELL, y = OY + r * CELL;
			if (g_open[r][c] || (g_lost && g_mine[r][c]))
			{
				fill_rect (x, y, CELL - 1, CELL - 1, 0x00303a44);
				if (g_mine[r][c]) fill_rect (x + 7, y + 7, CELL - 15, CELL - 15, 0x00ff5050);
				else if (g_adj[r][c])
				{
					char d[2] = { (char) ('0' + g_adj[r][c]), 0 };
					kapi_draw_text (x + CELL / 2 - 3, y + 4, d, numcol[(int) g_adj[r][c]]);
				}
			}
			else
			{
				fill_rect (x, y, CELL - 1, CELL - 1, 0x00586472);
				if (g_flag[r][c]) fill_rect (x + 6, y + 6, CELL - 13, CELL - 13, 0x00ffd040);
			}
		}
}

int main (void)
{
	fb = kapi_create_window (W, H, "minesweeper");
	if (fb == 0) return 1;
	g_rng = kapi_get_ticks () | 1u;
	kapi_set_click_handler (on_click);
	kapi_set_key_handler (on_key);
	restart ();
	while (!should_exit ()) { pump_events (); redraw (); msleep (16); }
	return 0;
}
