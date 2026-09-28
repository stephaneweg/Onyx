//
// same.c -- SameGame. A grid of coloured blocks; click a block and its connected
// same-colour group (>= 2) vanishes, the column blocks fall, and empty columns
// collapse left. Score grows with bigger groups. Mouse-driven via the canvas-click
// event (kapi_set_click_handler); 'r' starts a new board.
//
#include "kapi.h"
#include "wtk/wtk.h"

#define GW	18
#define GH	13
#define CELL	24
#define OX	12
#define OY	30
#define W	(OX * 2 + GW * CELL)
#define H	(OY + GH * CELL + 12)
#define NCOL	4

static unsigned *fb;
static wtk::Canvas g_cv, g_bg;		// the window's canvas; its static background, drawn once

static const unsigned COLORS[NCOL + 1] = {
	0x00000000, 0x00e05050, 0x0050b060, 0x004080e0, 0x00e0c040
};

static unsigned char g_grid[GH][GW];	// 0 empty, else 1..NCOL
static int g_mark[GH][GW];
static int g_score, g_over;
static unsigned g_rng;

static unsigned rnd (void) { g_rng = g_rng * 1103515245u + 12345u; return g_rng >> 16; }

static void restart (void)
{
	for (int r = 0; r < GH; r++)
		for (int c = 0; c < GW; c++)
			g_grid[r][c] = (unsigned char) (1 + rnd () % NCOL);
	g_score = 0;
	g_over = 0;
}

// Flood-fill the same-colour group at (c,r) into g_mark; return its size. Cells are
// marked when pushed, so each is pushed at most once (stack bound = grid size).
static int flood (int c0, int r0, int col)
{
	for (int r = 0; r < GH; r++) for (int c = 0; c < GW; c++) g_mark[r][c] = 0;

	static int stk[GW * GH][2];
	int sp = 0, n = 0;
	g_mark[r0][c0] = 1;
	stk[sp][0] = c0; stk[sp][1] = r0; sp++;
	while (sp > 0)
	{
		sp--;
		int c = stk[sp][0], r = stk[sp][1];
		n++;
		static const int dc[4] = { -1, 1, 0, 0 }, dr[4] = { 0, 0, -1, 1 };
		for (int k = 0; k < 4; k++)
		{
			int nc = c + dc[k], nr = r + dr[k];
			if (nc < 0 || nc >= GW || nr < 0 || nr >= GH) continue;
			if (g_mark[nr][nc] || g_grid[nr][nc] != col) continue;
			g_mark[nr][nc] = 1;
			stk[sp][0] = nc; stk[sp][1] = nr; sp++;
		}
	}
	return n;
}

static void collapse (void)
{
	// Gravity: each column's blocks fall to the bottom.
	for (int c = 0; c < GW; c++)
	{
		int write = GH - 1;
		for (int r = GH - 1; r >= 0; r--)
			if (g_grid[r][c]) g_grid[write--][c] = g_grid[r][c];
		while (write >= 0) g_grid[write--][c] = 0;
	}
	// Remove empty columns by shifting the rest left.
	int wcol = 0;
	for (int c = 0; c < GW; c++)
	{
		if (g_grid[GH - 1][c] == 0) continue;		// column empty (after gravity)
		if (wcol != c)
			for (int r = 0; r < GH; r++) { g_grid[r][wcol] = g_grid[r][c]; }
		wcol++;
	}
	for (int c = wcol; c < GW; c++) for (int r = 0; r < GH; r++) g_grid[r][c] = 0;
}

static int moves_left (void)
{
	for (int r = 0; r < GH; r++)
		for (int c = 0; c < GW; c++)
		{
			if (!g_grid[r][c]) continue;
			if (c + 1 < GW && g_grid[r][c + 1] == g_grid[r][c]) return 1;
			if (r + 1 < GH && g_grid[r + 1][c] == g_grid[r][c]) return 1;
		}
	return 0;
}

static void play (int c, int r)
{
	if (c < 0 || c >= GW || r < 0 || r >= GH) return;
	int col = g_grid[r][c];
	if (col == 0) return;
	int n = flood (c, r, col);
	if (n < 2) return;
	for (int y = 0; y < GH; y++) for (int x = 0; x < GW; x++) if (g_mark[y][x]) g_grid[y][x] = 0;
	g_score += (n - 1) * (n - 1);			// bigger groups score much more
	collapse ();
	if (!moves_left ()) g_over = 1;
}

static void on_click (unsigned long s, int ev, long val)
{
	(void) s;
	if (ev != GUI_EVENT_CANVAS_CLICK || g_over) return;
	int cx = (int) ((val >> 16) & 0xFFFF);
	int cy = (int) (val & 0xFFFF);
	play ((cx - OX) / CELL, (cy - OY) / CELL);
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

static int itoa (int v, char *b)
{
	char t[12]; int n = 0, p = 0;
	if (v == 0) t[n++] = '0';
	while (v) { t[n++] = (char) ('0' + v % 10); v /= 10; }
	while (n) b[p++] = t[--n];
	b[p] = '\0';
	return p;
}

// The theme's look (wtk/paint.h): the face around, the board in a sunken well (its own dark);
// drawn once into g_bg, copied at each frame.
static void paint_bg (void)
{
	using namespace wtk;
	g_bg.alloc (W, H);
	g_bg.clear (C_BG);
	wk_text_l (g_bg, OX, 4, 20, "Score:", C_TEXT);
	const char *hint = "click a group  r: new";
	wk_text_l (g_bg, W - OX - wk_text_w (hint), 4, 20, hint, C_DIS);
	wk_sunken (g_bg, OX - 3, OY - 3, GW * CELL + 5, GH * CELL + 5, 5, 0x00141820);
}

// A message box over the board (the theme's dialog: a title strip, the face, an outline).
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
	char buf[16]; itoa (g_score, buf);
	wtk::wk_text_l (g_cv, OX + 7 * kapi_font_width (), 4, 20, buf, wtk::C_TEXT, 2);

	for (int r = 0; r < GH; r++)
		for (int c = 0; c < GW; c++)
			if (g_grid[r][c])
				fill_rect (OX + c * CELL, OY + r * CELL, CELL - 1, CELL - 1,
					   COLORS[g_grid[r][c]]);

	if (g_over) msgbox (OX + GW * CELL / 2, OY + GH * CELL / 2, "No more moves", "r: new game");
}

int main (void)
{
	fb = kapi_create_window (W, H, "same");
	if (fb == 0) return 1;
	wtk::wk_decorate_window ();			// (reads the theme: the palette)
	g_cv.adopt (fb, W, H);
	paint_bg ();

	g_rng = kapi_get_ticks () | 1u;
	kapi_set_click_handler (on_click);
	kapi_set_key_handler (on_key);
	restart ();

	while (!should_exit ())
	{
		pump_events ();
		redraw ();
		msleep (16);
	}
	return 0;
}
