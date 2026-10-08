//
// sim_probe.cpp -- checks the desktop simulator's stand-in kernel (tools/tests/desktop_sim/fakekapi.cpp)
// gives what the Notes / Stickies tests rely on (AutoDev round 1, step 0): SIM_SERVICES, SIM_ROFS, the
// script step "copy", SIM_CURSOR=follow, SIM_STAT's kapi_path_stat / kapi_clock_info, the window's flags
// logged. Linked with fakekapi.o; run by tools/tests/run_notes_test.sh, one case an invocation (the
// environment and the script set there), from the repository's root. Prints "probe <case>: ok" when
// every check of the case passed; the shell checks the log lines besides.
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
#include "uikit/win.h"		// the window API (UIKit's: uk_win_*)
#include "systemkit/systemkit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail;
#define CHECK(c) do { if (!(c)) { fprintf (stderr, "probe: FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

static int read_card (const char *p, char *buf, int cap)
{
	void *f = kapi_open (p);
	if (!f) return -1;
	int n = kapi_read (f, buf, (unsigned) cap - 1);
	kapi_close (f);
	if (n < 0) n = 0;
	buf[n] = 0;
	return n;
}

int main (int argc, char **argv)
{
	const char *c = argc > 1 ? argv[1] : "";
	if (!strcmp (c, "notify"))			// SIM_SERVICES=notify, SIM="exit": no msleep may run
	{
		CHECK (kapi_ipc_lookup ("notify") == 700);
		CHECK (notify ("Notes", "Note moved to the Trash") == 0);
		fprintf (stderr, "probe notify: sent\n");
	}
	else if (!strcmp (c, "lookup"))			// SIM_SERVICES=- SIM_MBOX=...
	{
		CHECK (kapi_ipc_lookup ("notes") == 0);
		CHECK (kapi_ipc_lookup ("stickies") == 0);
		CHECK (kapi_ipc_register ("notes") == 1);
		CHECK (kapi_ipc_register ("stickies") == 1);
		int from = 0, type = 0; char b[64];
		CHECK (kapi_mailbox_recv (&from, &type, b, sizeof b, 0) >= 0 && type == 21 && from == 9);	// (SIM_MBOX still read)
	}
	else if (!strcmp (c, "listed"))			// SIM_SERVICES=notify,notes,stickies
	{
		CHECK (kapi_ipc_lookup ("notify") == 700);
		CHECK (kapi_ipc_lookup ("notes") == 701);
		CHECK (kapi_ipc_lookup ("stickies") == 702);
		CHECK (kapi_ipc_lookup ("dock") == 0);
		const char m[] = "SD:/Notes/a.txt";
		CHECK (kapi_mailbox_send (701, 3, m, sizeof m) == 0);
	}
	else if (!strcmp (c, "rofs"))			// SIM_ROFS=SD:/Notes,SD:/etc
	{
		CHECK (kapi_save_file ("SD:/Notes/a.txt", "x", 1) < 0);
		CHECK (kapi_save_file ("SD:/notes/b.txt", "x", 1) < 0);		// (case-insensitive)
		CHECK (kapi_mkdir ("SD:/Notes/sub") < 0);
		CHECK (kapi_save_file ("SD:/etc/autostart", "x", 1) < 0);
		CHECK (kapi_save_file ("SD:/NotesX/a.txt", "x", 1) == 1);		// (a prefix of a name: not under)
		CHECK (kapi_save_file ("SD:/x.txt", "yz", 2) == 2);
		CHECK (kapi_rename ("SD:/x.txt", "SD:/Notes/x.txt") < 0);
		CHECK (kapi_remove ("SD:/x.txt") == 0);
		CHECK (kapi_file_out ("SD:/Notes/c.txt", 0) == 0);
	}
	else if (!strcmp (c, "copy"))			// SIM="copy SRC SD:/Notes/b.txt;wait;exit"
	{
		char b[256];
		CHECK (read_card ("SD:/Notes/b.txt", b, sizeof b) < 0);	// not yet
		kapi_msleep (10);					// (the step: the copy)
		CHECK (read_card ("SD:/Notes/b.txt", b, sizeof b) > 0 && !strncmp (b, "Shopping\n", 9));
	}
	else if (!strcmp (c, "follow"))			// SIM_CURSOR=follow, SIM="down 50 10;move 150 12;up 150 12;exit"
	{
		CHECK (uk_win_create_ex (300, 40, 240, 200, "probe", WIN_FLAG_BORDERLESS | WIN_FLAG_BACKMOST | WIN_FLAG_SYSTEM | WIN_FLAG_ALPHA) != 0);
		int x = 0, y = 0;
		uk_win_cursor_pos (&x, &y);
		CHECK (x == -1 && y == -1);				// (no pointer step yet)
		kapi_msleep (10);					// down 50 10
		uk_win_cursor_pos (&x, &y);
		CHECK (x == 350 && y == 50);
		uk_win_move (400, 42);				// (the widget follows the drag)
		kapi_msleep (10);					// move 150 12: from the origin at the press
		uk_win_cursor_pos (&x, &y);
		CHECK (x == 450 && y == 52);
	}
	else if (!strcmp (c, "stat"))			// SIM_STAT=1; argv[2] a card path whose host file is dated 2026-09-20 08:00 UTC
	{
		struct kapi_stat st;
		CHECK (kapi_path_stat (argv[2], &st) == 0);
		CHECK (st.mtime == 1789891200LL && st.size == 9 && (st.mode & KAPI_S_IFMT) == KAPI_S_IFREG);
		CHECK (kapi_path_stat ("SD:/Notes/none.txt", &st) == -KAPI_ENOENT);
		struct kapi_clock_info ci;
		CHECK (kapi_clock_info (&ci) == 0);
		CHECK (ci.utc_us == 1790598840LL * 1000000 && ci.tz_minutes == 0 && (ci.flags & KAPI_CLOCK_REALTIME_VALID));
	}
	else if (!strcmp (c, "nostat"))			// SIM_STAT unset: as before
	{
		struct kapi_stat st; struct kapi_clock_info ci;
		CHECK (kapi_path_stat ("SD:/x.txt", &st) == -KAPI_ENOSYS);
		CHECK (kapi_clock_info (&ci) == -KAPI_ENOSYS);
	}
	else { fprintf (stderr, "probe: no case %s\n", c); return 2; }
	if (g_fail) return 1;
	fprintf (stderr, "probe %s: ok\n", c);
	return 0;
}
