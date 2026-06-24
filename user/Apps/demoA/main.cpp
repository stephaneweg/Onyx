//
// demoA.c -- EL0 windowed demo: a bouncing box. Draws directly into the window
// canvas the kernel mapped in (shared-buffer model), then present()s each frame.
//
#include "kapi.h"
#include "uikit.hpp"		// ui::decorate_window

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
	// Demonstrate direct file access (Option C): read our own ELF off the SD card
	// and report it over the kernel console -- no syscall, a direct kapi call.
	void *f = kapi_open ("SD:apps/demoA.app/main.elf");
	if (f != 0)
	{
		unsigned char hdr[4] = {0, 0, 0, 0};
		kapi_read (f, hdr, 4);
		if (hdr[0] == 0x7F && hdr[1] == 'E' && hdr[2] == 'L' && hdr[3] == 'F')
		{
			const char ok[] = "demoA: read own ELF header from SD (file API works)";
			kapi_write (1, ok, sizeof (ok) - 1);
		}
		kapi_close (f);
	}

	fb = create_window (W, H, "demo A: bouncing box");
	if (fb == 0)
	{
		return 1;
	}
	ui::decorate_window ();			// user-side window chrome

	int x = 10, y = 10, dx = 3, dy = 2, s = 36;
	while (!should_exit ())
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
		pump_events ();
		msleep (16);
	}

	return 0;
}
