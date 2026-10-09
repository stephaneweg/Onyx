//
// uikit/port/port_desktop.cpp -- the desktop's port: UIKit's window driver for Elegant, the graphics server
// (SD:/bin/elegant), speaking the protocol of port/elegant.h through the kernel's transport (AppKit's
// kapi_ws_ctl: KAPI_WS_CALL a request, KAPI_WS_KICK "my pixels changed", KAPI_WS_ACTIVE "is there a server").
// Compiled into UIKit by uikit/port.cpp (lib/uikit.so). It was AppKit's appkit_ws.inc and the KAPI_WS bodies
// of appkit_calls.inc until 2026-10-08 (docs/POCKETUI-TECH-STUDY.md phase P2): moved verbatim, the behaviour
// unchanged.
//
// A call that finds no server (it is being started, or started again) waits for it, 5 s at most. The client
// itself is uikit/port/client.inc (shared with the pocket port, whose server PocketUI starts from Elegant's
// operations); this file adds what is the desktop's: no hello, uk_win_server's answer, no shell calls.
//
// On a PC (the desktop simulator, the hosts of the tests, Koton for Windows) there is no Elegant: the
// stand-in kernel has a window manager of its own and the entries of kern/kapi_abi.h's NOT-ON-ONYX part --
// each call is relayed there (uikit/port/host.inc). With UK_PORT_WIRE (tools/tests/server_sim) the client
// speaks to a graphics server built for the PC instead.
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
#include "uikit/win.h"
#include "uikit/port/port.h"

namespace uikit {
namespace port {

static void server_name (struct uk_win_server_info *out)	// (what the desktop is: Elegant's, the screen whole)
{
	const char *n = "elegant";
	for (unsigned i = 0; i < sizeof out->name; i++) out->name[i] = 0;
	for (unsigned i = 0; n[i] != 0 && i + 1 < sizeof out->name; i++) out->name[i] = n[i];
	out->mode = UK_MODE_DESKTOP;
	int w = 0, h = 0;
	kapi_screen_size (&w, &h);
	out->screen_w = w; out->screen_h = h;
	out->work_x = 0; out->work_y = 0; out->work_w = w; out->work_h = h;
	out->scale = 100;
	out->size_class = UK_SC_REGULAR;
	for (int i = 0; i < 8; i++) out->reserved[i] = 0;
}

#if defined (__aarch64__) || defined (UK_PORT_WIRE)
// ===== On Onyx: Elegant's client ================================================================

} // namespace port
} // namespace uikit
#include "uikit/port/elegant.h"
namespace uikit {
namespace port {

#define WS_HELLO()		1		// (Elegant: no hello -- the protocol's first server)
#define WS_HELLO_AGAIN()	((void) 0)
#include "uikit/port/client.inc"

// ---- the server -----------------------------------------------------------------------------------

int server (struct uk_win_server_info *out)
{
	if (out == 0 || out->size < sizeof *out) return 0;
	server_name (out);
	if (kapi_ws_ctl (KAPI_WS_ACTIVE, 0, 0, 0) <= 0) return 0;
	struct kapi_win_geom g;
	if (ws__w[0].made && geometry (&g) == 0 && g.aw > 0 && g.ah > 0)	// (the work area: the program has a window)
	{
		out->work_x = g.ax; out->work_y = g.ay; out->work_w = g.aw; out->work_h = g.ah;
	}
	return 1;
}

long shell (int, long, long, long, long, const void *, unsigned, void *, unsigned) { return -KAPI_ENOSYS; }

#else
// ===== On a PC: the stand-in kernel's window manager (kern/kapi_abi.h, NOT ON ONYX) ======================
#include "uikit/port/host.inc"

int server (struct uk_win_server_info *out)
{
	if (out == 0 || out->size < sizeof *out) return 0;
	server_name (out);
	struct kapi_win_geom g;
	if (HT->version >= 64 && HT->win_geometry && HT->win_geometry (&g) == 0 && g.aw > 0 && g.ah > 0)
	{
		out->work_x = g.ax; out->work_y = g.ay; out->work_w = g.aw; out->work_h = g.ah;
	}
	return 1;
}

long shell (int, long, long, long, long, const void *, unsigned, void *, unsigned) { return -KAPI_ENOSYS; }

#undef HT
#endif

// ---- the adaptive layer: the desktop says nothing (regular, today's sizes: uikit/adapt.cpp) ----------------------
int adapt_info (int *, int *, int *, int *) { return 0; }
void focus_rect (int, int, int, int) {}
void text_hint (int) {}
void logical_units (int) {}

} // namespace port
} // namespace uikit
