//
// autostart_test.cpp -- SystemKit's autostart helper (user/Kits/systemkit/autostart.h: autostart_has,
// autostart_ensure) on the PC: the desktop simulator's fakekapi.o, the file in a fresh SIM_WRITES each case.
// Run by tools/tests/run_notes_test.sh from the repository's root, with SIM_SD an empty folder (so "no file"
// is no file, not the card's); the card's own sdcard/etc/autostart, as shipped, is seeded in one case.
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
#include "systemkit/systemkit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/stat.h>

static int g_fail, g_checks;
#define CHECK(c) do { g_checks++; if (!(c)) { fprintf (stderr, "autostart: FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)
#define CHECK_EQ(a, b) do { std::string x_ = (a), y_ = (b); g_checks++; if (x_ != y_) { fprintf (stderr, "autostart: FAIL %s:%d:\n--- got:\n%s--- wanted:\n%s---\n", __FILE__, __LINE__, x_.c_str (), y_.c_str ()); g_fail++; } } while (0)

static std::string W;
static const char *CMD = "run stickies", *AFTER = "run agenda", *COMMENT = "# Stickies: the pinned notes on the desktop (Notes, View menu)";

static std::string slurp (const std::string &p)
{
	FILE *f = fopen (p.c_str (), "rb");
	if (!f) return "<none>";
	std::string s; char b[4096]; size_t n;
	while ((n = fread (b, 1, sizeof b, f)) > 0) s.append (b, n);
	fclose (f);
	return s;
}
static std::string file () { return slurp (W + "/etc/autostart"); }
// A fresh card whose SD:/etc/autostart holds `text` (none: no file).
static void seed (const char *text)
{
	if (system (("rm -rf '" + W + "' && mkdir -p '" + W + "/etc'").c_str ()) != 0) exit (2);
	if (!text) return;
	FILE *f = fopen ((W + "/etc/autostart").c_str (), "wb");
	fwrite (text, 1, strlen (text), f);
	fclose (f);
}
// ensure on `before`: -> its result; the file must then be `after`
static void ensure_case (int line, const char *before, int want, const char *after)
{
	seed (before);
	int r = autostart_ensure (CMD, AFTER, COMMENT);
	g_checks++;
	if (r != want) { fprintf (stderr, "autostart: FAIL %s:%d: autostart_ensure -> %d, not %d\n", __FILE__, line, r, want); g_fail++; }
	CHECK_EQ (file (), after ? after : before ? before : "<none>");
}
#define ENSURE(before, want, after) ensure_case (__LINE__, before, want, after)

int main ()
{
	const char *w = getenv ("SIM_WRITES");
	if (!w || !*w) { fprintf (stderr, "autostart: SIM_WRITES not set\n"); return 2; }
	W = w;
	const std::string C = std::string (COMMENT) + "\n";

	// after the agenda's line, active
	ENSURE ("run menubar\nrun agenda\nkeyb FR\npreload /boot\n", 2,
		("run menubar\nrun agenda\n" + C + "run stickies\nkeyb FR\npreload /boot\n").c_str ());
	// after the agenda's line held back by Setup: held back as well
	ENSURE ("run setup\n#setup: run dock\n#setup: run agenda\nkeyb FR\npreload /boot\n", 2,
		("run setup\n#setup: run dock\n#setup: run agenda\n" + C + "#setup: run stickies\nkeyb FR\npreload /boot\n").c_str ());
	// the agenda's line indented, with arguments: still the agenda's
	ENSURE ("  run agenda --small\npreload /boot\n", 2, ("  run agenda --small\n" + C + "run stickies\npreload /boot\n").c_str ());
	// "run agendas" is not "run agenda"; no agenda line: before preload, which stays last
	ENSURE ("run agendas\nkeyb FR\n# the end\npreload /boot\nsleep 1\n", 2,
		("run agendas\nkeyb FR\n# the end\n" + C + "run stickies\npreload /boot\nsleep 1\n").c_str ());
	// "#setup: preload" counts as the preload line too (the line itself is not held back)
	ENSURE ("keyb FR\n#setup: preload /boot\n", 2, ("keyb FR\n" + C + "run stickies\n#setup: preload /boot\n").c_str ());
	// neither: at the end; also a last line without its '\n'
	ENSURE ("keyb FR\n", 2, ("keyb FR\n" + C + "run stickies\n").c_str ());
	ENSURE ("keyb FR", 2, ("keyb FR\n" + C + "run stickies\n").c_str ());
	ENSURE ("", 2, (C + "run stickies\n").c_str ());
	// no file: made with the line
	ENSURE (0, 2, (C + "run stickies\n").c_str ());
	// "\r\n" lines (a file edited on Windows): matched, kept
	ENSURE ("run agenda\r\nkeyb FR\r\n", 2, ("run agenda\r\n" + C + "run stickies\nkeyb FR\r\n").c_str ());
	// already there -> 1, the file byte for byte the same
	ENSURE ("run agenda\nrun stickies\npreload /boot\n", 1, 0);
	ENSURE ("run agenda\n\t  run stickies\npreload /boot\n", 1, 0);
	ENSURE ("run stickies --x\n", 1, 0);
	ENSURE ("run   stickies\n", 1, 0);
	ENSURE ("#setup: run agenda\n#setup: run stickies\npreload /boot\n", 1, 0);
	ENSURE ("#setup:run stickies\n", 1, 0);
	// a plain comment, or Setup's own "#setup#" comment lines, are not the line
	ENSURE ("run agenda\n# run stickies\n#setup# run stickies\n", 2,
		("run agenda\n" + C + "run stickies\n# run stickies\n#setup# run stickies\n").c_str ());
	// "run stickiesX" is another program
	ENSURE ("run stickiesX\n", 2, ("run stickiesX\n" + C + "run stickies\n").c_str ());
	// called twice: one line
	seed ("run agenda\npreload /boot\n");
	CHECK (autostart_ensure (CMD, AFTER, COMMENT) == 2);
	CHECK (autostart_ensure (CMD, AFTER, COMMENT) == 1);
	CHECK_EQ (file (), "run agenda\n" + C + "run stickies\npreload /boot\n");
	// no comment, no "after" rule
	seed ("run agenda\npreload /boot\n");
	CHECK (autostart_ensure (CMD, 0, 0) == 2);
	CHECK_EQ (file (), "run agenda\nrun stickies\npreload /boot\n");
	CHECK (autostart_ensure (0, 0, 0) == 0 && autostart_ensure ("", 0, 0) == 0);
	// too big (20 KB): left alone
	std::string big;
	while (big.size () < 20000) big += "# a long comment line, many of them, to make the file larger than the helper takes\n";
	ENSURE (big.c_str (), 0, 0);
	CHECK (autostart_has ("run anything") == 0);
	// read-only: not written
	seed ("run agenda\n");
	setenv ("SIM_ROFS", "SD:/etc", 1);
	CHECK (autostart_ensure (CMD, AFTER, COMMENT) == 0);
	unsetenv ("SIM_ROFS");
	CHECK_EQ (file (), "run agenda\n");

	// autostart_has
	seed ("run agenda\n#setup: run dock\n# run clock\nkeyb FR\n");
	CHECK (autostart_has ("run agenda") == 1);
	CHECK (autostart_has ("run dock") == 1);
	CHECK (autostart_has ("keyb") == 1 && autostart_has ("keyb FR") == 1 && autostart_has ("keyb BE") == 0);
	CHECK (autostart_has ("run clock") == 0);
	CHECK (autostart_has ("run") == 1 && autostart_has ("ru") == 0 && autostart_has ("") == 0 && autostart_has (0) == 0);
	seed (0);
	CHECK (autostart_has ("run agenda") == 0);

	// the card's own file, as shipped (Setup pending): the line goes after "#setup: run agenda", held back,
	// preload /boot still the last line
	std::string card = slurp ("sdcard/etc/autostart");
	CHECK (card != "<none>");
	seed (card.c_str ());
	if (autostart_has (CMD))					// (the card already ships it: unchanged)
	{
		CHECK (autostart_ensure (CMD, AFTER, COMMENT) == 1);
		CHECK_EQ (file (), card);
	}
	else
	{
		CHECK (autostart_ensure (CMD, AFTER, COMMENT) == 2);
		std::string f = file ();
		size_t a = f.find ("#setup: run agenda\n");
		CHECK (a != std::string::npos && f.compare (a + 19, C.size () + 21, C + "#setup: run stickies\n") == 0);
		CHECK (f.size () == card.size () + C.size () + 21);
		CHECK (f.size () >= 14 && f.compare (f.size () - 14, 14, "preload /boot\n") == 0);
		CHECK (autostart_ensure (CMD, AFTER, COMMENT) == 1);
	}

	if (g_fail) { fprintf (stderr, "autostart: %d of %d checks FAILED\n", g_fail, g_checks); return 1; }
	fprintf (stderr, "autostart: %d checks passed\n", g_checks);
	return 0;
}
