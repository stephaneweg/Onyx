//
// tetris.c -- Tetris. App-drawn playfield (raw pixels) + kapi_draw_text for the
// score; keyboard via the window key handler. Gravity is frame-counted (no timer
// dependency). Keys: left/right move, up rotate, down soft-drop, space hard-drop,
// 'r' restart.
//
#include "kapi.h"
#include "wtk/wtk.h"

#define COLS	10
#define ROWS	20
#define CELL	18
#define FX	12			// playfield origin
#define FY	12
#define W	(FX + COLS * CELL + 132)
#define H	(FY + ROWS * CELL + 12)
#define SX	(FX + COLS * CELL + 14)	// the side panel
#define SW	(W - SX - 12)

static unsigned *fb;
static wtk::Canvas g_cv, g_bg;		// the window's canvas; its static background, drawn once

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

// The theme's look (wtk/paint.h): the face around, the well sunken (its own dark), the score
// and the lines in dark LCD wells, the keys dimmed; drawn once into g_bg, copied at each frame.
static void paint_bg (void)
{
	using namespace wtk;
	g_bg.alloc (W, H);
	g_bg.clear (C_BG);
	wk_sunken (g_bg, FX - 3, FY - 3, COLS * CELL + 5, ROWS * CELL + 5, 5, 0x00101014);
	wk_text_l (g_bg, SX, FY, 16, "Score", C_TEXT, 2);
	wk_sunken (g_bg, SX, FY + 20, SW, 26, 5, 0x005C6478);
	wk_text_l (g_bg, SX, FY + 56, 16, "Lines", C_TEXT, 2);
	wk_sunken (g_bg, SX, FY + 76, SW, 26, 5, 0x005C6478);
	static const char *const keys[5] = { "left/right", "up: rotate", "dn: soft", "spc: drop", "r: restart" };
	for (int i = 0; i < 5; i++) wk_text_l (g_bg, SX, FY + 124 + i * 18, 16, keys[i], C_DIS);
}

// A message box over the well (the theme's dialog: a title strip, the face, an outline).
static void msgbox (int cx, int cy, const char *title, const char *text)
{
	using namespace wtk;
	int th = wk_fh () + 10, w = wk_text_w (text) + 56, h = th + wk_fh () + 24;
	int x = cx - w / 2, y = cy - h / 2;
	wk_rbox (g_cv, x, y, w, h, 8, C_FACE, C_FACE);
	wk_title_strip (g_cv, x + 1, y + 1, w - 2, th, title, 7);
	wk_rline (g_cv, x, y, w, h, 8, WK_OUTLINE == 2 ? 0 : wk_tone (C_FRAME_ACTIVE, 44), 255);
	wk_text_c (g_cv, x, y + th, w, h - th, text, C_TEXT);
}

static void redraw (void)
{
	g_cv.putOther (g_bg, 0, 0, false);

	for (int r = 0; r < ROWS; r++)
		for (int c = 0; c < COLS; c++)
			if (g_field[r][c]) draw_cell (c, r, COLORS[g_field[r][c]]);

	if (!g_over)
		for (int y = 0; y < 4; y++)
			for (int x = 0; x < 4; x++)
				if (cell_filled (g_type, g_rot, x, y) && g_py + y >= 0)
					draw_cell (g_px + x, g_py + y, COLORS[g_type + 1]);

	char buf[16]; int n;
	n = itoa (g_score, buf); wtk::draw_text (fb, W, H, SX + SW - 10 - n * kapi_font_width (), FY + 25, buf, 0x00F0F4F8, 1, 2);
	n = itoa (g_lines, buf); wtk::draw_text (fb, W, H, SX + SW - 10 - n * kapi_font_width (), FY + 81, buf, 0x00F0F4F8, 1, 2);
	if (g_over) msgbox (FX + COLS * CELL / 2, FY + ROWS * CELL / 2, "Game over", "r: retry");
}

int main (void)
{
	fb = kapi_create_window (W, H, "tetris");
	if (fb == 0) return 1;
	wtk::wk_decorate_window ();			// (reads the theme: the palette)
	g_cv.adopt (fb, W, H);
	paint_bg ();

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
