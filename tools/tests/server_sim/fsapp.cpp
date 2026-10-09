//
// fsapp.cpp -- a program that takes the full screen, for tools/tests/server_sim (a BASIC program's way: Arkanoid,
// user/Libs/basic/runtime.cpp -- a window first, then uk_win_fullscreen_begin, its frames by kapi_present_fb).
// Its window: 320 x 200, its own colour; F: the full screen (painted red, shown); G: given back.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#include "appkit/appkit.h"
#include "uikit/win.h"

static unsigned *s_pFs;
static int s_nW, s_nH;
static volatile int s_nKey;

static void on_key (unsigned long, int ev, long v) { if (ev == GUI_EVENT_KEY) s_nKey = (int) v; }

int main (void)
{
	unsigned *c = uk_win_create (320, 200, "fsapp");
	if (c == 0) return 1;
	for (int i = 0; i < 320 * 200; i++) c[i] = 0x00336699;
	uk_win_on_key (on_key);
	uk_win_present ();
	while (!kapi_should_exit ())
	{
		pump_events ();
		int k = s_nKey; s_nKey = 0;
		if ((k == 'f' || k == 'F') && s_pFs == 0)
		{
			s_pFs = uk_win_fullscreen_begin (&s_nW, &s_nH);
			for (int i = 0; s_pFs != 0 && i < s_nW * s_nH; i++) s_pFs[i] = 0x00CC2020;
			if (s_pFs != 0) kapi_present_fb ();
		}
		else if ((k == 'g' || k == 'G') && s_pFs != 0) { uk_win_fullscreen_end (); s_pFs = 0; uk_win_present (); }
		else if (s_pFs != 0) kapi_present_fb ();
		kapi_msleep (16);
	}
	return 0;
}
