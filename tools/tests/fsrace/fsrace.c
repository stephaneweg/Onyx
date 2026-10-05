// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors
//
// fsrace -- a Pi test: an app takes the full screen and sends its first frame while the desktop's
// last frame is still on its way to the display (the compositor yields during its display DMA).
// Before the fix of 2026-10-05 (kernel: DisplayPresentIdle) kapi_present_fb then waited for that DMA
// without yielding, the compositor never ran again to end it: core 0 stopped, the hang watchdog
// restarted the Pi 15 s later ("display: waiting for the display DMA" in SD:/etc/lastcrash.txt).
//
//   usage: fsrace [rounds]          (default 300; prints a line every 20 rounds, then "fsrace: ok")
//
// Each round: give the screen back (the compositor redraws the whole desktop at its next pass and
// sends it), keep the CPU for a while that changes from round to round (an app's own code: the
// compositor runs when the timer preempts it, then yields during its DMA), take the full screen
// again and present at once. Build and run: tools/tests/fsrace/run_pi.py.
//
#include "kapi.h"
#include "applib.h"

int main (void)
{
	char args[32];
	kapi_get_args (args, sizeof (args));
	int i = 0, rounds = 0;
	while (args[i] == ' ') i++;
	while (args[i] >= '0' && args[i] <= '9') rounds = rounds * 10 + (args[i++] - '0');
	if (rounds <= 0) rounds = 300;

	int w = 0, h = 0;
	for (int r = 0; r < rounds; r++)
	{
		unsigned *fb = kapi_fullscreen_begin (&w, &h);
		if (fb == 0) { ax_putln ("fsrace: no full screen"); return 1; }
		unsigned colour = (r & 1) ? 0x00203040 : 0x00403020;
		for (long k = 0; k < (long) w * h; k += 64) fb[k] = colour;	// (a little of it: be quick)
		kapi_present_fb ();
		kapi_fullscreen_end ();
		// 0 .. 40 ms in steps of 0.7 ms: somewhere in there the compositor is in its DMA
		unsigned wait = (unsigned) (r % 58) * 700, t0 = kapi_clock_us ();
		while (kapi_clock_us () - t0 < wait) {}
		if (r % 20 == 19)
		{
			char b[12]; ax_itoa (r + 1, b);
			ax_puts ("fsrace: "); ax_puts (b); ax_putln (" rounds");
		}
	}
	kapi_fullscreen_end ();
	ax_putln ("fsrace: ok");
	return 0;
}
