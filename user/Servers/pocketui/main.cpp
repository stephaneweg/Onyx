//
// main.cpp -- PocketUI, the graphics server of the pocket and console modes (SD:/bin/pocketui;
// docs/POCKETUI-TECH-STUDY.md section 7, phase P3). The kernel starts it instead of Elegant when SD:/etc/system.ini
// says "shell = pocket" or "shell = console" (kapi v97, kernel/sys/wsrv.cpp):
//
//   pocketui --serve [--restart] --mode pocket|console
//
// What it shares with Elegant is ../common/ (the loop, the routing, the window manager and compositor, the
// requests' decoding): this program adds its policy (wm.cpp: filled windows, cards, one app in front), its status
// band (band.cpp: pocket mode) -- and its UIKit: the pocket build of UIKit (SD:/lib/pocket/uikit.so, its port
// speaking PocketUI's protocol: uikit/port/pocket.h) is loaded under the name SD:/lib/uikit.so (kapi_lib_open_as),
// after the server's role is taken and before the display is (every program of the session then maps it when it
// asks for uikit.so) -- and kept: PocketUI holds its reference while it lives; the kernel keeps the alias for a
// PocketUI started again (--restart), drops it when another server is started.
//
// No shell programs yet (the status bar, the launcher, the switcher: pocketshell, phase P5); no session file (the
// desktop's SD:/etc/autostart still runs: its menu bar and dock are refused, wm.cpp).
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
#include "onyxpp.hpp"
#include "core.h"
#include "policy.h"
#include "uikit/port/pocket.h"
#include "pocketui.h"

#define POCKET_UIKIT	"SD:/lib/pocket/uikit.so"	// PocketUI's UIKit (built from UIKit's sources with its port)
#define UIKIT		"SD:/lib/uikit.so"		// ... the name the programs open it by

extern "C" int memcmp (const void *a, const void *b, __SIZE_TYPE__ n)
{
	const unsigned char *p = (const unsigned char *) a, *q = (const unsigned char *) b;
	for (; n > 0; n--, p++, q++) if (*p != *q) return *p < *q ? -1 : 1;
	return 0;
}

extern "C" unsigned el_port_ticks (void)		// (common/port/circle/timer.h)
{
	return kapi_get_ticks ();
}

static const void *s_pUikit;				// the pocket UIKit's table: kept mapped while PocketUI lives

static void say (const char *a, const char *b = "", const char *c = "")
{
	ax_puts (a); ax_puts (b); ax_puts (c);
}

static void num (int v, char *out)
{
	char d[12]; int i = 0, k = 0;
	unsigned u = v < 0 ? (unsigned) -v : (unsigned) v;
	do { d[i++] = (char) ('0' + u % 10); u /= 10; } while (u != 0);
	if (v < 0) out[k++] = '-';
	while (i > 0) out[k++] = d[--i];
	out[k] = 0;
}

// (wm.cpp's policy, its "registered" hook) The server's role is ours, the display not taken yet: the pocket
// UIKit under the name SD:/lib/uikit.so. Asked again by a PocketUI started again: the kernel gives the orphaned
// alias back (the same image the session's programs map).
void pk_main_registered (int)
{
	int err = 0;
	s_pUikit = kapi_lib_open_as (POCKET_UIKIT, UIKIT, 1, &err);
	if (s_pUikit != 0) { say ("pocketui: " POCKET_UIKIT " is " UIKIT " for this session\n"); return; }
	char e[16];
	num (err, e);
	say ("pocketui: " POCKET_UIKIT " not loaded as " UIKIT " (error ", e,
	     "): the programs get the desktop's UIKit (their windows framed as Elegant's)\n");
}

static int arg_is (const char *a, const char *d)
{
	int i = 0;
	while (d[i] != 0 && a[i] == d[i]) i++;
	return d[i] == 0 && (a[i] == 0 || a[i] == ' ');
}

int main (void)
{
	char args[96];
	args[0] = 0;
	kapi_get_args (args, sizeof args);
	int serve = 0, restart = 0;
	for (const char *a = args; *a; )
	{
		while (*a == ' ') a++;
		if (arg_is (a, "--serve")) serve = 1;
		else if (arg_is (a, "--restart")) restart = 1;
		else if (arg_is (a, "--mode"))
		{
			a += 6;
			while (*a == ' ') a++;
			if (arg_is (a, "console")) g_nPkMode = PK_MODE_CONSOLE;
			else if (arg_is (a, "pocket")) g_nPkMode = PK_MODE_POCKET;
		}
		while (*a && *a != ' ') a++;
	}
	if (serve) return el_serve (0, restart);

	say ("PocketUI, the graphics server of the pocket and console modes (Onyx's desktop: Elegant).\n"
	     "  pocketui --serve [--restart] --mode pocket|console\n"
	     "The kernel starts it when SD:/etc/system.ini says shell = pocket (or console): the apps one at a time,\n"
	     "filled under a status band, the small ones as cards; Alt+Tab brings the app at the back to the front.\n");
	return 0;
}
