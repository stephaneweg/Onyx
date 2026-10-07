//
// 2048 -- slide tiles with the arrow keys; equal tiles merge. 'r' restarts.
//
#include "appkit/appkit.h"
#include "uikit/uikit.h"

#define N	4
#define CELL	68
#define GAP	6
#define PAD	10			// the window's margin (the theme's face)
#define OX	(PAD + GAP)		// the first tile
#define OY	54
#define W	(OX * 2 + N * CELL + (N - 1) * GAP)
#define H	(OY + N * CELL + (N - 1) * GAP + GAP + PAD)
#define BW	(N * CELL + (N + 1) * GAP)	// the board

static unsigned *fb;
static uikit::Canvas g_cv, g_bg;		// the window's canvas; its static background, drawn once
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

// The theme's look (uikit/paint.h): the face around, the score in a dark LCD well, the board
// (its own colours) rounded; drawn once into g_bg, copied at each frame under the tiles.
static void paint_bg (void)
{
	using namespace uikit;
	g_bg.alloc (W, H);
	g_bg.clear (C_BG);
	uk_sunken (g_bg, PAD, 10, W - 2 * PAD, 32, 6, 0x005C6478);
	uk_text_l (g_bg, PAD + 12, 10, 32, "Score", 0x00B8C0CC);
	uk_rbox (g_bg, PAD, OY - GAP, BW, BW, 7, 0x00bbada0, 0x00bbada0);
	uk_rline (g_bg, PAD, OY - GAP, BW, BW, 7, 0x009a8c80, 200);
	for (int r = 0; r < N; r++)
		for (int c = 0; c < N; c++)
			uk_rbox (g_bg, OX + c * (CELL + GAP), OY + r * (CELL + GAP), CELL, CELL, 4, 0x00cdc1b4, 0x00cdc1b4);
}

// A message box over the board (the theme's dialog: a title strip, the face, an outline).
static void msgbox (int cx, int cy, const char *title, const char *text)
{
	using namespace uikit;
	int th = uk_fh () + 10, w = uk_text_w (text) + 56, h = th + uk_fh () + 24;
	int x = cx - w / 2, y = cy - h / 2;
	uk_rbox (g_cv, x, y, w, h, 8, C_FACE, C_FACE);
	uk_title_strip (g_cv, x + 1, y + 1, w - 2, th, title, 7);
	uk_rline (g_cv, x, y, w, h, 8, UK_OUTLINE == 2 ? 0 : uk_tone (C_FRAME_ACTIVE, 44), 255);
	uk_text_c (g_cv, x, y + th, w, h - th, text, C_TEXT);
}

static void redraw (void)
{
	g_cv.putOther (g_bg, 0, 0, false);
	char buf[16]; int sn = ax_itoa (g_score, buf);
	uikit::draw_text (fb, W, H, W - PAD - 12 - sn * kapi_font_width (), 18, buf, 0x00F0F4F8, 1, 2);

	for (int r = 0; r < N; r++)
		for (int c = 0; c < N; c++)
		{
			int x = OX + c * (CELL + GAP), y = OY + r * (CELL + GAP);
			int v = g_grid[r][c];
			if (v)
			{
				unsigned tc = tile_color (v);
				uikit::uk_rbox (g_cv, x, y, CELL, CELL, 4, tc, tc);
				char t[8]; int n = ax_itoa (v, t);
				int tx = x + (CELL - n * kapi_font_width ()) / 2;
				int ty = y + (CELL - kapi_font_height ()) / 2;
				uikit::draw_text (fb, W, H, tx, ty, t, v <= 4 ? 0x00776e65 : 0x00f9f6f2, 1, 2);
			}
		}
	if (g_over) msgbox (W / 2, OY + BW / 2 - GAP, "Game over", "r: new game");
}

int main (void)
{
	fb = kapi_create_window (W, H, "2048");
	if (fb == 0) return 1;
	uikit::uk_decorate_window ();			// (reads the theme: the palette)
	g_cv.adopt (fb, W, H);
	paint_bg ();
	g_rng = kapi_get_ticks () | 1u;
	kapi_set_key_handler (on_key);
	restart ();
	while (!should_exit ()) { pump_events (); redraw (); present (); msleep (16); }
	return 0;
}
