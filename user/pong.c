//
// pong -- two paddles, a bouncing ball. Left paddle: W/S. Right paddle: up/down.
// First to 9 wins; 'r' resets. App-drawn; integer physics.
//
#include "kapi.h"
#include "applib.h"

#define W	480
#define H	320
#define PW	8		// paddle width
#define PH	56		// paddle height
#define BS	8		// ball size

static unsigned *fb;
static int g_ly, g_ry;			// paddle top-Y
static int g_bx, g_by, g_vx, g_vy;	// ball
static int g_ls, g_rs;			// scores
static int g_up_l, g_dn_l, g_up_r, g_dn_r;	// held keys

static void serve (int dir)
{
	g_bx = W / 2; g_by = H / 2;
	g_vx = dir; g_vy = 2;
}
static void reset (void)
{
	g_ly = g_ry = (H - PH) / 2; g_ls = g_rs = 0;
	g_up_l = g_dn_l = g_up_r = g_dn_r = 0;
	serve (3);
}

static void step (void)
{
	if (g_up_l) g_ly -= 6;
	if (g_dn_l) g_ly += 6;
	if (g_up_r) g_ry -= 6;
	if (g_dn_r) g_ry += 6;
	if (g_ly < 0) g_ly = 0;
	else if (g_ly > H - PH) g_ly = H - PH;
	if (g_ry < 0) g_ry = 0;
	else if (g_ry > H - PH) g_ry = H - PH;

	g_bx += g_vx; g_by += g_vy;
	if (g_by < 0) { g_by = 0; g_vy = -g_vy; }
	if (g_by > H - BS) { g_by = H - BS; g_vy = -g_vy; }

	// Left paddle at x=16..16+PW, right at W-16-PW..W-16.
	if (g_bx <= 16 + PW && g_bx >= 16 && g_by + BS >= g_ly && g_by <= g_ly + PH && g_vx < 0)
		{ g_vx = -g_vx; g_vx++; }
	if (g_bx + BS >= W - 16 - PW && g_bx + BS <= W - 16 && g_by + BS >= g_ry && g_by <= g_ry + PH && g_vx > 0)
		{ g_vx = -g_vx; g_vx--; }

	if (g_bx < 0) { g_rs++; serve (3); }
	if (g_bx > W) { g_ls++; serve (-3); }
}

static void on_key (unsigned long s, int ev, long key)
{
	(void) s;
	int down = (ev == GUI_EVENT_KEY);
	if (!down) return;
	switch (key)
	{
	case 'w': case 'W': g_up_l = 1; g_dn_l = 0; break;
	case 's': case 'S': g_dn_l = 1; g_up_l = 0; break;
	case KEY_UP:   g_up_r = 1; g_dn_r = 0; break;
	case KEY_DOWN: g_dn_r = 1; g_up_r = 0; break;
	case 'r': case 'R': reset (); break;
	}
	// (No key-up events: paddles nudge per press; hold = repeated presses.)
}

static void fill_rect (int x, int y, int w, int h, unsigned c)
{
	for (int yy = y; yy < y + h && yy < H; yy++)
		for (int xx = x; xx < x + w && xx < W; xx++)
			if (xx >= 0 && yy >= 0) fb[yy * W + xx] = c;
}

static void redraw (void)
{
	fill_rect (0, 0, W, H, 0x00101814);
	for (int y = 0; y < H; y += 16) fill_rect (W / 2 - 1, y, 2, 8, 0x00304038);	// net
	fill_rect (16, g_ly, PW, PH, 0x00ffffff);
	fill_rect (W - 16 - PW, g_ry, PW, PH, 0x00ffffff);
	fill_rect (g_bx, g_by, BS, BS, 0x0060ff90);
	char l[4], r[4]; ax_itoa (g_ls, l); ax_itoa (g_rs, r);
	kapi_draw_text (W / 2 - 40, 10, l, 0x00ffffff);
	kapi_draw_text (W / 2 + 32, 10, r, 0x00ffffff);
}

int main (void)
{
	fb = kapi_create_window (W, H, "pong");
	if (fb == 0) return 1;
	kapi_set_key_handler (on_key);
	reset ();
	while (!should_exit ()) { pump_events (); step (); redraw (); msleep (16); }
	return 0;
}
