//
// wstest.c -- a window served by Elegant, the graphics server, asked for by hand: the test of the
// kernel's mechanisms for the programs' windows (kapi v89: the shared buffers, the requests, the
// event queue) before AppKit's window calls use them.
//
//   run SD:/bin/elegant --display      (the server, with its demonstration)
//   run SD:/bin/wstest                 a window of 320 x 200: each click in it changes its colour;
//                                      its close button ends the program
//
// It talks the protocol itself (appkit/elegant.h) -- a program never does: this is a test.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. Permission is hereby
// granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions: The above copyright notice and this permission notice shall
// be included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS
// IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
//
#include "appkit/appkit.h"
#include "appkit/elegant.h"

#define W	320
#define H	200

static unsigned s_Colour = 0x00804080;
static int s_nClicks = 0;

static long call (int op, long a0, long a1, long a2, long a3, const void *in, unsigned in_len, void *out, unsigned cap)
{
	struct kapi_ws_call c;
	kapi_memset (&c, 0, sizeof c);
	c.op = op; c.in = in; c.in_len = in_len; c.out = out; c.out_cap = cap;
	c.a[0] = a0; c.a[1] = a1; c.a[2] = a2; c.a[3] = a3;
	return kapi_ws_ctl (KAPI_WS_CALL, (long) &c, 0, 0);
}

static void paint (void)
{
	unsigned *p = (unsigned *) EL_VA_CANVAS;
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++)
			p[y * W + x] = ((x / 20 + y / 20) & 1) ? s_Colour : (s_Colour >> 1) & 0x007F7F7F;
	kapi_ws_ctl (KAPI_WS_KICK, 0, 0, 0);
}

static void on_pointer (unsigned long sender, int event, gui_value value)
{
	(void) sender; (void) value;
	if (event != GUI_EVENT_PTR_DOWN) return;
	s_nClicks++;
	s_Colour = (s_Colour * 5 + 0x00234567) & 0x00FFFFFF;
	paint ();
}

int main (void)
{
	if (kapi_ws_ctl (KAPI_WS_ACTIVE, 0, 0, 0) <= 0)
	{
		ax_puts ("wstest: no graphics server owns the display (run SD:/bin/elegant --display)\n");
		return 1;
	}
	struct el_create c;
	kapi_memset (&c, 0, sizeof c);
	c.title[0] = 'w'; c.title[1] = 's'; c.title[2] = 't'; c.title[3] = 'e'; c.title[4] = 's'; c.title[5] = 't';
	long r = call (EL_OP_CREATE, -1, -1, W, H, &c, sizeof c, 0, 0);
	if (r != 1) { ax_puts ("wstest: the window was refused\n"); return 1; }

	struct el_frame f;
	if (call (EL_OP_FRAME, 0, 0, 0, 0, 0, 0, &f, sizeof f) == 1 && f.frame_w > 0)
	{
		unsigned *on = (unsigned *) EL_VA_FRAME, *off = (unsigned *) EL_VA_FRAME_OFF;
		for (int y = 0; y < f.frame_h; y++)
			for (int x = 0; x < f.frame_w; x++)
			{
				int close = y >= KAPI_FRAME_BTN_Y && y < KAPI_FRAME_BTN_Y + KAPI_FRAME_BTN_H
					 && x >= f.frame_w - KAPI_FRAME_BTN_EDGE - KAPI_FRAME_BTN_W && x < f.frame_w - KAPI_FRAME_BTN_EDGE;
				on[y * f.frame_w + x] = close ? 0x00C04040 : y < f.inset_t ? 0x00B040B0 : 0x00303038;
				off[y * f.frame_w + x] = close ? 0x00C04040 : y < f.inset_t ? 0x00909098 : 0x00303038;
			}
	}
	call (EL_OP_HANDLER, EL_HANDLER_POINTER, (long) on_pointer, 0, 0, 0, 0, 0, 0);
	paint ();

	while (!kapi_should_exit ()) kapi_pump_wait (200);

	char line[64] = "wstest: closed after     clicks\n";
	int n = s_nClicks;
	for (int i = 23; i >= 21; i--) { line[i] = (char) ('0' + n % 10); n /= 10; if (n == 0) break; }
	ax_puts (line);
	return 0;
}
