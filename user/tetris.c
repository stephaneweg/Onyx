//
// tetris.c -- Tetris. App-drawn playfield (raw pixels) + kapi_draw_text for the
// score; keyboard via the window key handler. Gravity is frame-counted (no timer
// dependency). Keys: left/right move, up rotate, down soft-drop, space hard-drop,
// 'r' restart.
//
#include "kapi.h"

#define COLS	10
#define ROWS	20
#define CELL	18
#define FX	12			// playfield origin
#define FY	12
#define W	(FX + COLS * CELL + 132)
#define H	(FY + ROWS * CELL + 12)

static unsigned *fb;

// 7 tetrominoes x 4 rotations, as 4x4 bitmasks (bit (15-(y*4+x)) = cell x,y filled).
static const unsigned short PIECES[7][4] = {
	{ 0x0F00, 0x2222, 0x00F0, 0x4444 },	// I
	{ 0xCC00, 0xCC00, 0xCC00, 0xCC00 },	// O
	{ 0x0E40, 0x4C40, 0x4E00, 0x4640 },	// T
	{ 0x06C0, 0x8C40, 0x6C00, 0x4620 },	// S
	{ 0x0C60, 0x4C80, 0xC600, 0x2640 },	// Z
	{ 0x44C0, 0x8E00, 0xC880, 0x0E20 },	// J
	{ 0x4460, 0x0E80, 0xC440, 0x2E00 },	// L
};
static const unsigned COLORS[8] = {
	0x00000000, 0x0000ffff, 0x00ffe000, 0x00c000ff,
	0x0000e000, 0x00ff3030, 0x004070ff, 0x00ff9000
};

static unsigned char g_field[ROWS][COLS];	// 0 empty, else piece type+1
static int g_type, g_rot, g_px, g_py;
static int g_score, g_lines, g_over;
static unsigned g_rng;
static int g_frames, g_lastdrop;

static int cell_filled (int type, int rot, int x, int y)
{
	return (PIECES[type][rot] >> (15 - (y * 4 + x))) & 1;
}

static int collide (int type, int rot, int px, int py)
{
	for (int y = 0; y < 4; y++)
		for (int x = 0; x < 4; x++)
			if (cell_filled (type, rot, x, y))
			{
				int gx = px + x, gy = py + y;
				if (gx < 0 || gx >= COLS || gy >= ROWS) return 1;
				if (gy >= 0 && g_field[gy][gx]) return 1;
			}
	return 0;
}

static void lock_piece (void)
{
	for (int y = 0; y < 4; y++)
		for (int x = 0; x < 4; x++)
			if (cell_filled (g_type, g_rot, x, y))
			{
				int gy = g_py + y, gx = g_px + x;
				if (gy >= 0) g_field[gy][gx] = (unsigned char) (g_type + 1);
			}
}

static void clear_lines (void)
{
	for (int r = ROWS - 1; r >= 0; r--)
	{
		int full = 1;
		for (int c = 0; c < COLS; c++) if (!g_field[r][c]) { full = 0; break; }
		if (!full) continue;
		for (int y = r; y > 0; y--)
			for (int c = 0; c < COLS; c++) g_field[y][c] = g_field[y - 1][c];
		for (int c = 0; c < COLS; c++) g_field[0][c] = 0;
		g_lines++;
		g_score += 100;
		r++;					// re-check the row that shifted down
	}
}

static void spawn (void)
{
	g_rng = g_rng * 1103515245u + 12345u;
	g_type = (int) ((g_rng >> 16) % 7);
	g_rot = 0;
	g_px = 3;
	g_py = -1;
	if (collide (g_type, g_rot, g_px, g_py)) g_over = 1;
}

static void restart (void)
{
	for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++) g_field[r][c] = 0;
	g_score = 0; g_lines = 0; g_over = 0; g_frames = 0; g_lastdrop = 0;
	spawn ();
}

static void step_down (void)
{
	if (!collide (g_type, g_rot, g_px, g_py + 1)) g_py++;
	else { lock_piece (); clear_lines (); spawn (); }
}

static void try_rotate (void)
{
	int nr = (g_rot + 1) % 4;
	if (!collide (g_type, nr, g_px, g_py)) { g_rot = nr; return; }
	if (!collide (g_type, nr, g_px - 1, g_py)) { g_px--; g_rot = nr; return; }
	if (!collide (g_type, nr, g_px + 1, g_py)) { g_px++; g_rot = nr; return; }
}

static void on_key (unsigned long s, int ev, long key)
{
	(void) s;
	if (ev != GUI_EVENT_KEY) return;
	if (g_over) { if (key == 'r' || key == ' ') restart (); return; }
	switch (key)
	{
	case KEY_LEFT:  if (!collide (g_type, g_rot, g_px - 1, g_py)) g_px--; break;
	case KEY_RIGHT: if (!collide (g_type, g_rot, g_px + 1, g_py)) g_px++; break;
	case KEY_UP:    try_rotate (); break;
	case KEY_DOWN:  step_down (); g_lastdrop = g_frames; break;
	case ' ':       while (!collide (g_type, g_rot, g_px, g_py + 1)) g_py++;
			step_down (); g_lastdrop = g_frames; break;
	case 'r': case 'R': restart (); break;
	}
}

static void fill_rect (int x, int y, int w, int h, unsigned c)
{
	for (int yy = y; yy < y + h && yy < H; yy++)
		for (int xx = x; xx < x + w && xx < W; xx++)
			if (xx >= 0 && yy >= 0) fb[yy * W + xx] = c;
}

static void draw_cell (int gx, int gy, unsigned col)
{
	int px = FX + gx * CELL, py = FY + gy * CELL;
	fill_rect (px, py, CELL - 1, CELL - 1, col);
}

static int itoa (int v, char *b)
{
	char t[12]; int n = 0, p = 0;
	if (v < 0) { b[p++] = '-'; v = -v; }
	if (v == 0) t[n++] = '0';
	while (v) { t[n++] = (char) ('0' + v % 10); v /= 10; }
	while (n) b[p++] = t[--n];
	b[p] = '\0';
	return p;
}

static void redraw (void)
{
	fill_rect (0, 0, W, H, 0x00181c24);
	fill_rect (FX - 2, FY - 2, COLS * CELL + 3, ROWS * CELL + 3, 0x00404858);
	fill_rect (FX, FY, COLS * CELL, ROWS * CELL, 0x00101014);

	for (int r = 0; r < ROWS; r++)
		for (int c = 0; c < COLS; c++)
			if (g_field[r][c]) draw_cell (c, r, COLORS[g_field[r][c]]);

	if (!g_over)
		for (int y = 0; y < 4; y++)
			for (int x = 0; x < 4; x++)
				if (cell_filled (g_type, g_rot, x, y) && g_py + y >= 0)
					draw_cell (g_px + x, g_py + y, COLORS[g_type + 1]);

	int sx = FX + COLS * CELL + 12;
	char buf[16];
	kapi_draw_text (sx, FY + 4, "SCORE", 0x0090a0b0);
	itoa (g_score, buf); kapi_draw_text (sx, FY + 18, buf, 0x00ffffff);
	kapi_draw_text (sx, FY + 44, "LINES", 0x0090a0b0);
	itoa (g_lines, buf); kapi_draw_text (sx, FY + 58, buf, 0x00ffffff);
	kapi_draw_text (sx, FY + 92,  "left/right", 0x00708090);
	kapi_draw_text (sx, FY + 104, "up: rotate", 0x00708090);
	kapi_draw_text (sx, FY + 116, "dn: soft", 0x00708090);
	kapi_draw_text (sx, FY + 128, "spc: drop", 0x00708090);
	kapi_draw_text (sx, FY + 140, "r: restart", 0x00708090);
	if (g_over)
	{
		kapi_draw_text (sx, FY + 170, "GAME OVER", 0x00ff6060);
		kapi_draw_text (sx, FY + 182, "r = retry", 0x00ffa0a0);
	}
}

int main (void)
{
	fb = kapi_create_window (W, H, "tetris");
	if (fb == 0) return 1;

	g_rng = kapi_get_ticks () | 1u;
	kapi_set_key_handler (on_key);
	restart ();

	while (!should_exit ())
	{
		pump_events ();
		g_frames++;
		if (!g_over)
		{
			int speed = 30 - g_lines;	// faster as lines clear
			if (speed < 5) speed = 5;
			if (g_frames - g_lastdrop >= speed) { step_down (); g_lastdrop = g_frames; }
		}
		redraw ();
		msleep (16);
	}
	return 0;
}
