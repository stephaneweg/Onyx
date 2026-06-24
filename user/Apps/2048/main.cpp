//
// 2048 -- slide tiles with the arrow keys; equal tiles merge. 'r' restarts.
//
#include "kapi.h"
#include "uikit.hpp"
#include "applib.h"

#define N	4
#define CELL	68
#define GAP	6
#define OX	8
#define OY	44
#define W	(OX * 2 + N * CELL + (N - 1) * GAP)
#define H	(OY + N * CELL + (N - 1) * GAP + 8)

static unsigned *fb;
static int g_grid[N][N];
static int g_score, g_over;
static unsigned g_rng;

static unsigned rnd (void) { g_rng = g_rng * 1103515245u + 12345u; return g_rng >> 16; }

static void add_tile (void)
{
	int empty[N * N], ne = 0;
	for (int r = 0; r < N; r++) for (int c = 0; c < N; c++) if (!g_grid[r][c]) empty[ne++] = r * N + c;
	if (ne == 0) return;
	int e = empty[rnd () % ne];
	g_grid[e / N][e % N] = (rnd () % 10 == 0) ? 4 : 2;
}

static void restart (void)
{
	for (int r = 0; r < N; r++) for (int c = 0; c < N; c++) g_grid[r][c] = 0;
	g_score = 0; g_over = 0;
	add_tile (); add_tile ();
}

// Slide+merge one line (4 cells) toward index 0. Returns 1 if it changed.
static int slide (int *v)
{
	int tmp[N], n = 0, changed = 0;
	for (int i = 0; i < N; i++) if (v[i]) tmp[n++] = v[i];
	for (int i = n; i < N; i++) tmp[i] = 0;
	for (int i = 0; i < N - 1; i++)
		if (tmp[i] && tmp[i] == tmp[i + 1])
		{
			tmp[i] *= 2; g_score += tmp[i];
			for (int j = i + 1; j < N - 1; j++) tmp[j] = tmp[j + 1];
			tmp[N - 1] = 0;
		}
	for (int i = 0; i < N; i++) { if (v[i] != tmp[i]) changed = 1; v[i] = tmp[i]; }
	return changed;
}

// dir: 0 left, 1 right, 2 up, 3 down.
static void move (int dir)
{
	int changed = 0;
	for (int k = 0; k < N; k++)
	{
		int line[N];
		for (int i = 0; i < N; i++)
		{
			int r, c;
			if (dir == 0) { r = k; c = i; }
			else if (dir == 1) { r = k; c = N - 1 - i; }
			else if (dir == 2) { r = i; c = k; }
			else { r = N - 1 - i; c = k; }
			line[i] = g_grid[r][c];
		}
		if (slide (line)) changed = 1;
		for (int i = 0; i < N; i++)
		{
			int r, c;
			if (dir == 0) { r = k; c = i; }
			else if (dir == 1) { r = k; c = N - 1 - i; }
			else if (dir == 2) { r = i; c = k; }
			else { r = N - 1 - i; c = k; }
			g_grid[r][c] = line[i];
		}
	}
	if (changed) add_tile ();

	// Game over: no empty cell and no equal neighbour.
	int movable = 0;
	for (int r = 0; r < N; r++) for (int c = 0; c < N; c++)
	{
		if (!g_grid[r][c]) movable = 1;
		if (c + 1 < N && g_grid[r][c] == g_grid[r][c + 1]) movable = 1;
		if (r + 1 < N && g_grid[r][c] == g_grid[r + 1][c]) movable = 1;
	}
	g_over = !movable;
}

static void on_key (unsigned long s, int ev, long key)
{
	(void) s;
	if (ev != GUI_EVENT_KEY) return;
	if (key == 'r' || key == 'R') { restart (); return; }
	if (g_over) return;
	if (key == KEY_LEFT) move (0);
	else if (key == KEY_RIGHT) move (1);
	else if (key == KEY_UP) move (2);
	else if (key == KEY_DOWN) move (3);
}

static void fill_rect (int x, int y, int w, int h, unsigned c)
{
	for (int yy = y; yy < y + h && yy < H; yy++)
		for (int xx = x; xx < x + w && xx < W; xx++)
			if (xx >= 0 && yy >= 0) fb[yy * W + xx] = c;
}

static unsigned tile_color (int v)
{
	switch (v)
	{
	case 2: return 0x00eee4da; case 4: return 0x00ede0c8;
	case 8: return 0x00f2b179; case 16: return 0x00f59563;
	case 32: return 0x00f67c5f; case 64: return 0x00f65e3b;
	case 128: return 0x00edcf72; case 256: return 0x00edcc61;
	case 512: return 0x00edc850; case 1024: return 0x00edc53f;
	default: return 0x00edc22e;
	}
}

static void redraw (void)
{
	fill_rect (0, 0, W, H, 0x00bbada0);
	char buf[16]; ax_itoa (g_score, buf);
	kapi_draw_text (8, 14, "score:", 0x00ffffff);
	kapi_draw_text (8 + 7 * kapi_font_width (), 14, buf, 0x00fff0d0);
	if (g_over) kapi_draw_text (W - 90, 14, "game over r", 0x00ffe0e0);

	for (int r = 0; r < N; r++)
		for (int c = 0; c < N; c++)
		{
			int x = OX + c * (CELL + GAP), y = OY + r * (CELL + GAP);
			int v = g_grid[r][c];
			fill_rect (x, y, CELL, CELL, v ? tile_color (v) : 0x00cdc1b4);
			if (v)
			{
				char t[8]; int n = ax_itoa (v, t);
				int tx = x + (CELL - n * kapi_font_width ()) / 2;
				int ty = y + (CELL - kapi_font_height ()) / 2;
				kapi_draw_text (tx, ty, t, v <= 4 ? 0x00776e65 : 0x00f9f6f2);
			}
		}
}

int main (void)
{
	fb = kapi_create_window (W, H, "2048");
	if (fb == 0) return 1;
	ui::decorate_window ();
	g_rng = kapi_get_ticks () | 1u;
	kapi_set_key_handler (on_key);
	restart ();
	while (!should_exit ()) { pump_events (); redraw (); msleep (16); }
	return 0;
}
