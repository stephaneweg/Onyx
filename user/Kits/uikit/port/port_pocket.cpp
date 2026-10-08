//
// uikit/port/port_pocket.cpp -- the pocket port: UIKit's window driver for PocketUI, the graphics server of the
// pocket and console modes (SD:/bin/pocketui), speaking the protocol of port/pocket.h through the kernel's
// transport. Compiled into UIKit by uikit/port.cpp when UK_PORT_POCKET is defined: SD:/lib/pocket/uikit.so,
// the UIKit PocketUI loads under the name SD:/lib/uikit.so (kapi_lib_open_as), built from the same sources
// and objects as the desktop's lib/uikit.so but this file, its export table checked identical slot by slot
// (tools/libgen/abi_same.py; docs/POCKETUI-TECH-STUDY.md section 5).
//
// PocketUI's protocol starts from Elegant's operations (port/pocket.h): the client is the desktop port's
// (port/client.inc: the window state, the replay when the server is started again), with:
//   - the hello: the first request is PK_OP_HELLO; a server that does not answer it (Elegant: a program of the
//     other session that kept this UIKit) is not spoken to -- the window calls fail, the reason logged once;
//   - uk_win_server: PocketUI's answer (its mode, the work area under the status band, the size class);
//   - uk_shell_*: PocketUI's operations (PK_OP_SHELL...; it answers -KAPI_ENOSYS until phase P5).
// The frames: PocketUI says which windows have one (a card: UIKit's skin draws its Milk title as on the
// desktop; a filled window: none, EL_OP_FRAME's insets 0) -- nothing here draws differently.
//
// On a PC (no UK_PORT_WIRE) the stand-in kernel's window manager, as the desktop port's (port/host.inc), with
// uk_win_server saying pocket.
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
#include "uikit/port/pocket.h"

namespace uikit {
namespace port {

static void server_name (struct uk_win_server_info *out)	// (what PocketUI would be: the screen whole, pocket)
{
	const char *n = PK_PROTO_NAME;
	for (unsigned i = 0; i < sizeof out->name; i++) out->name[i] = 0;
	for (unsigned i = 0; n[i] != 0 && i + 1 < sizeof out->name; i++) out->name[i] = n[i];
	out->mode = UK_MODE_POCKET;
	int w = 0, h = 0;
	kapi_screen_size (&w, &h);
	out->screen_w = w; out->screen_h = h;
	out->work_x = 0; out->work_y = 0; out->work_w = w; out->work_h = h;
	out->scale = 100;
	out->size_class = h > w ? UK_SC_NARROW : UK_SC_COMPACT;
	for (int i = 0; i < 8; i++) out->reserved[i] = 0;
}

static void server_copy (struct uk_win_server_info *out, const struct pk_server *s)
{
	for (unsigned i = 0; i + 1 < sizeof out->name && i < sizeof s->name && s->name[i] != 0; i++) out->name[i] = s->name[i];
	out->mode = s->mode == PK_MODE_CONSOLE ? UK_MODE_CONSOLE : UK_MODE_POCKET;
	out->screen_w = s->screen_w; out->screen_h = s->screen_h;
	out->work_x = s->work_x; out->work_y = s->work_y; out->work_w = s->work_w; out->work_h = s->work_h;
	out->scale = s->scale > 0 ? s->scale : 100;
	out->size_class = s->size_class;
}

#if defined (__aarch64__) || defined (UK_PORT_WIRE)
// ===== On Onyx: PocketUI's client ===============================================================

static long ws_raw (int op, long a0, long a1, long a2, long a3, const void *in, unsigned in_len, void *out, unsigned cap);

// The hello (port/pocket.h PK_OP_HELLO): -1 not asked yet, 0 the server is another one's, 1 PocketUI.
static int pk__hello = -1;
static struct pk_server pk__server;

static int pk_hello (void)
{
	if (pk__hello >= 0) return pk__hello;
	struct pk_hello h;
	__builtin_memset (&h, 0, sizeof h);
	const char *n = PK_PROTO_NAME;
	for (unsigned i = 0; n[i] != 0 && i + 1 < sizeof h.proto; i++) h.proto[i] = n[i];
	h.version = PK_PROTO_VERSION;
	long r = ws_raw (PK_OP_HELLO, 0, 0, 0, 0, &h, sizeof h, &pk__server, sizeof pk__server);
	if (r == -KAPI_ESRCH) return 0;			// (no server at all: asked again at the next call)
	pk__hello = r >= 1 ? 1 : 0;
	if (!pk__hello)
		ax_puts ("uikit: this program's UIKit is PocketUI's (SD:/lib/pocket/uikit.so), the graphics server is another: "
			 "its windows are refused -- start it again\n");
	return pk__hello;
}

#define WS_HELLO()		pk_hello ()
#define WS_HELLO_AGAIN()	(pk__hello = -1)
#include "uikit/port/client.inc"

// ---- the server -----------------------------------------------------------------------------------

int server (struct uk_win_server_info *out)
{
	if (out == 0 || out->size < sizeof *out) return 0;
	server_name (out);
	if (kapi_ws_ctl (KAPI_WS_ACTIVE, 0, 0, 0) <= 0) return 0;
	struct pk_server s;
	__builtin_memset (&s, 0, sizeof s);
	if (ws_raw (PK_OP_SERVER, 0, 0, 0, 0, 0, 0, &s, sizeof s) != 1) return 0;	// (not PocketUI)
	server_copy (out, &s);
	return 1;
}

// ---- the shell's calls (uk_shell_*): PocketUI's operations ------------------------------------------

long shell (int op, long a0, long a1, long a2, long a3, const void *in, unsigned in_len, void *out, unsigned cap)
{
	static const int s_Op[] = { PK_OP_SHELL, PK_OP_EVENTS, PK_OP_KEYS, PK_OP_THUMB, PK_OP_FRONT, PK_OP_SPLIT, PK_OP_DIM };
	if (op < 0 || op >= (int) (sizeof s_Op / sizeof s_Op[0])) return -KAPI_EINVAL;
	if (!pk_hello ()) return -KAPI_ENOSYS;
	long r = ws_raw (s_Op[op], a0, a1, a2, a3, in, in_len, out, cap);
	return r == EL_E_BADOP ? -KAPI_ENOSYS : r;
}

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
	(void) server_copy;
	return 1;
}

long shell (int, long, long, long, long, const void *, unsigned, void *, unsigned) { return -KAPI_ENOSYS; }

#undef HT
#endif

} // namespace port
} // namespace uikit
