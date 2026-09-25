//
// plasma -- full-screen plasma effect: the demo of the full-screen app API (ABI v41).
// kapi_fullscreen_begin hands us a screen-sized back buffer, the desktop stops being
// drawn, and every frame is shown with kapi_present_fb. Integer maths only (a sine
// table + a colour palette). Esc, Enter, q or a click quits (the desktop comes back).
//
#include "kapi.h"

static int  g_sin[256];			// sin table, -127..127
static unsigned g_pal[256];
static volatile int g_quit = 0;

static void keys (unsigned long, int ev, long v)
{
	if (ev == GUI_EVENT_KEY && (v == 27 || v == KEY_ENTER || v == 'q' || v == 'Q')) g_quit = 1;
}
static void ptr (unsigned long, int ev, long v)
{
	if (ev == GUI_EVENT_PTR_DOWN && GUI_PTR_CHANGED (v)) g_quit = 1;
}

int main (void)
{
	// Sine table from a quarter-wave parabola approximation (no FP needed).
	for (int i = 0; i < 256; i++)
	{
		int x = i & 127;				// 0..127 over half a period
		int y = x * (128 - x) / 32;			// parabola 0..128
		if (y > 127) y = 127;
		g_sin[i] = (i < 128) ? y : -y;
	}
	for (int i = 0; i < 256; i++)			// smooth cyclic palette
	{
		int r = 128 + g_sin[i & 255];
		int g = 128 + g_sin[(i + 85) & 255];
		int b = 128 + g_sin[(i + 170) & 255];
		g_pal[i] = ((unsigned) (r > 255 ? 255 : r) << 16) | ((unsigned) (g > 255 ? 255 : g) << 8) | (unsigned) (b > 255 ? 255 : b);
	}

	int W = 0, H = 0;
	unsigned *fb = kapi_fullscreen_begin (&W, &H);
	if (fb == 0) return 1;
	kapi_set_key_handler (keys);
	kapi_set_pointer_handler (ptr);

	for (unsigned t = 0; !g_quit && !kapi_should_exit (); t += 2)
	{
		// Render at half resolution and double the pixels (cheap, still smooth).
		for (int y = 0; y < H; y += 2)
		{
			int sy1 = g_sin[((y >> 1) + t) & 255];
			int sy2 = g_sin[((y >> 2) + (t >> 1)) & 255];
			unsigned *row = fb + y * W;
			for (int x = 0; x < W; x += 2)
			{
				int v = g_sin[((x >> 1) + t) & 255] + sy1
				      + g_sin[((x >> 2) + (y >> 2) + (t >> 2)) & 255] + sy2;
				unsigned c = g_pal[(v >> 1) & 255];
				row[x] = c; row[x + 1] = c;
				row[x + W] = c; row[x + W + 1] = c;
			}
		}
		kapi_present_fb ();
		kapi_pump_events ();
	}
	kapi_fullscreen_end ();
	return 0;
}
