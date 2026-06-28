//
// eyes -- a desktop gadget: two googly eyes whose pupils follow the mouse pointer
// (kapi_cursor_pos gives the cursor relative to this window). Drag the title bar to
// move it like any window.
//
#include "kapi.h"
#include "wtk/wtk.h"

#define W	180
#define H	110

static unsigned *fb;

static unsigned isqrt (unsigned n)
{
	if (n == 0) return 0;
	unsigned x = n, y = (x + 1) / 2;
	while (y < x) { x = y; y = (x + n / x) / 2; }
	return x;
}

static void fill_rect (int x, int y, int w, int h, unsigned c)
{
	for (int yy = y; yy < y + h && yy < H; yy++)
		for (int xx = x; xx < x + w && xx < W; xx++)
			if (xx >= 0 && yy >= 0) fb[yy * W + xx] = c;
}

static void disc (int cx, int cy, int r, unsigned c)
{
	for (int y = -r; y <= r; y++)
		for (int x = -r; x <= r; x++)
			if (x * x + y * y <= r * r) fill_rect (cx + x, cy + y, 1, 1, c);
}

static void draw_eye (int ex, int ey, int R, int pr, int mx, int my)
{
	disc (ex, ey, R, 0x00ffffff);			// white
	int dx = mx - ex, dy = my - ey;
	int maxoff = R - pr - 2;
	unsigned d = isqrt ((unsigned) (dx * dx + dy * dy));
	int px = ex, py = ey;
	if ((int) d > maxoff && d > 0)
	{
		px = ex + dx * maxoff / (int) d;
		py = ey + dy * maxoff / (int) d;
	}
	else { px = ex + dx; py = ey + dy; }
	disc (px, py, pr, 0x00101018);			// pupil
}

int main (void)
{
	fb = kapi_create_window (W, H, "eyes");
	if (fb == 0) return 1;
	wtk::wk_decorate_window ();

	while (!should_exit ())
	{
		pump_events ();
		int mx, my;
		kapi_cursor_pos (&mx, &my);
		for (int i = 0; i < W * H; i++) fb[i] = 0x00b8c0c8;	// face
		draw_eye (50, 55, 34, 12, mx, my);
		draw_eye (130, 55, 34, 12, mx, my);
		msleep (16);
	}
	return 0;
}
