//
// demoA.c -- EL0 windowed demo: a bouncing box. Draws directly into the window
// canvas the kernel mapped in (shared-buffer model), then present()s each frame.
//
#include "usys.h"

#define W 240
#define H 180

static unsigned *fb;

static void clear (unsigned c)
{
	for (int i = 0; i < W * H; i++)
	{
		fb[i] = c;
	}
}

static void fillrect (int x0, int y0, int x1, int y1, unsigned c)
{
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 >= W) x1 = W - 1;
	if (y1 >= H) y1 = H - 1;
	for (int y = y0; y <= y1; y++)
	{
		for (int x = x0; x <= x1; x++)
		{
			fb[y * W + x] = c;
		}
	}
}

int main (void)
{
	fb = create_window (W, H, "demo A: bouncing box");
	if (fb == 0)
	{
		return 1;
	}

	int x = 10, y = 10, dx = 3, dy = 2, s = 36;
	for (;;)
	{
		clear (0x00102030);

		x += dx;
		y += dy;
		if (x < 0)        { x = 0;        dx = -dx; }
		if (x + s > W)    { x = W - s;    dx = -dx; }
		if (y < 0)        { y = 0;        dy = -dy; }
		if (y + s > H)    { y = H - s;    dy = -dy; }

		fillrect (x, y, x + s, y + s, 0x00FFC040);
		fillrect (x + 6, y + 6, x + s - 6, y + s - 6, 0x00804000);

		present ();
		msleep (16);
	}

	return 0;
}
