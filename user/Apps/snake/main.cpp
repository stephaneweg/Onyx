//
// snake.c -- Snake. App-drawn grid + uk_win_draw_text score; arrow keys steer (no
// reversing), 'r' restarts. Eat food to grow; hitting a wall or yourself ends it.
//
#include "appkit/appkit.h"
#include "uikit/uikit.h"

#define GW	24
#define GH	18
#define CELL	18
#define OX	12
#define OY	30
#define W	(OX * 2 + GW * CELL)
#define H	(OY + GH * CELL + 12)
#define NCELLS	(GW * GH)

static unsigned *fb;
static uikit::Canvas g_cv, g_bg;		// the window's canvas; its static background, drawn once

static int g_sx[NCELLS], g_sy[NCELLS];	// body; [0] = head
static int g_len;
static int g_dx, g_dy;			// current direction
static int g_ndx, g_ndy;		// next direction (from keys)
static int g_fx, g_fy;			// food
static int g_score, g_over;
static unsigned g_rng;
static int g_frames, g_lastmove;

static unsigned rnd (void) { g_rng = g_rng * 1103515245u + 12345u; return g_rng >> 16; }

static void place_food (void)
{
	for (;;)
	{
		int x = (int) (rnd () % GW), y = (int) (rnd () % GH);
		int on = 0;
		for (int i = 0; i < g_len; i++) if (g_sx[i] == x && g_sy[i] == y) { on = 1; break; }
		if (!on) { g_fx = x; g_fy = y; return; }
	}
}

static void restart (void)
{
	g_len = 4;
	for (int i = 0; i < g_len; i++) { g_sx[i] = GW / 2 - i; g_sy[i] = GH / 2; }
	g_dx = 1; g_dy = 0; g_ndx = 1; g_ndy = 0;
	g_score = 0; g_over = 0; g_frames = 0; g_lastmove = 0;
	place_food ();
}

static void step (void)
{
	g_dx = g_ndx; g_dy = g_ndy;
	int nx = g_sx[0] + g_dx, ny = g_sy[0] + g_dy;

	if (nx < 0 || nx >= GW || ny < 0 || ny >= GH) { g_over = 1; return; }
	int grow = (nx == g_fx && ny == g_fy);
	for (int i = 0; i < g_len; i++)
	{
		if (!grow && i == g_len - 1) continue;	// tail vacates this move
		if (g_sx[i] == nx && g_sy[i] == ny) { g_over = 1; return; }
	}

	if (grow && g_len < NCELLS) g_len++;
	for (int i = g_len - 1; i > 0; i--) { g_sx[i] = g_sx[i - 1]; g_sy[i] = g_sy[i - 1]; }
	g_sx[0] = nx; g_sy[0] = ny;
	if (grow) { g_score += 10; place_food (); }
}

static void on_key (unsigned long s, int ev, long key)
{
	(void) s;
	if (ev != GUI_EVENT_KEY) return;
	if (g_over) { if (key == 'r' || key == ' ') restart (); return; }
	switch (key)
	{
	case KEY_LEFT:  if (g_dx == 0) { g_ndx = -1; g_ndy = 0; } break;
	case KEY_RIGHT: if (g_dx == 0) { g_ndx =  1; g_ndy = 0; } break;
	case KEY_UP:    if (g_dy == 0) { g_ndx = 0; g_ndy = -1; } break;
	case KEY_DOWN:  if (g_dy == 0) { g_ndx = 0; g_ndy =  1; } break;
	case 'r': case 'R': restart (); break;
	}
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

static void cell (int x, int y, unsigned c) { fill_rect (OX + x * CELL, OY + y * CELL, CELL - 1, CELL - 1, c); }

// The theme's look (uikit/paint.h): the face around, the field sunken (its own dark); drawn once
// into g_bg, copied at each frame.
static void paint_bg (void)
{
	using namespace uikit;
	g_bg.alloc (W, H);
	g_bg.clear (C_BG);
	uk_text_l (g_bg, OX, 4, 20, "Score:", C_TEXT);
	uk_sunken (g_bg, OX - 3, OY - 3, GW * CELL + 5, GH * CELL + 5, 5, 0x000c0e12);
}

// A message box over the field (the theme's dialog: a title strip, the face, an outline).
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
	char buf[16];
	itoa (g_score, buf);
	uikit::uk_text_l (g_cv, OX + 7 * kapi_font_width (), 4, 20, buf, uikit::C_TEXT, 2);

	cell (g_fx, g_fy, 0x00ff4040);				// food
	for (int i = 0; i < g_len; i++)
		cell (g_sx[i], g_sy[i], i == 0 ? 0x0080ff80 : 0x0040c040);

	if (g_over) msgbox (OX + GW * CELL / 2, OY + GH * CELL / 2, "Game over", "r: restart");
}

int main (void)
{
	fb = uk_win_create (W, H, "snake");
	if (fb == 0) return 1;
	uikit::uk_decorate_window ();			// (reads the theme: the palette)
	g_cv.adopt (fb, W, H);
	paint_bg ();

	g_rng = kapi_get_ticks () | 1u;
	uk_win_on_key (on_key);
	restart ();

	while (!should_exit ())
	{
		pump_events ();
		g_frames++;
		if (!g_over)
		{
			int speed = 8 - g_score / 60;		// faster as you grow
			if (speed < 3) speed = 3;
			if (g_frames - g_lastmove >= speed) { step (); g_lastmove = g_frames; }
		}
		redraw ();
		present ();				// (the frame drawn into the canvas: shown -- the compositor redraws only what it is told)
		msleep (16);
	}
	return 0;
}
