//
// session_test.cpp -- SystemKit's sessions on the PC (user/Kits/systemkit/session.h: the autostart's split, the session
// files' programs, the mode in system.ini; autostart.h generalised to the session files; Setup's autostart_finish
// with the session files): the desktop simulator's fakekapi.o, a fresh SIM_WRITES card each case (SIM_SD an empty
// folder). Run by tools/tests/session/run.sh from the repository's root, with OLD_AUTOSTART the card's autostart
// before the sessions (git's HEAD~ or a copy).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
//
#include "appkit/appkit.h"
#include "systemkit/systemkit.h"
#include "../../../user/Apps/setup/system.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

static int g_fail, g_checks;
#define CHECK(c) do { g_checks++; if (!(c)) { fprintf (stderr, "session: FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)
#define CHECK_EQ(a, b) do { std::string x_ = (a), y_ = (b); g_checks++; if (x_ != y_) { fprintf (stderr, "session: FAIL %s:%d:\n--- got:\n%s--- wanted:\n%s---\n", __FILE__, __LINE__, x_.c_str (), y_.c_str ()); g_fail++; } } while (0)

static std::string W;
static std::string slurp (const std::string &p)
{
	FILE *f = fopen (p.c_str (), "rb");
	if (!f) return "<none>";
	std::string s; char b[4096]; size_t n;
	while ((n = fread (b, 1, sizeof b, f)) > 0) s.append (b, n);
	fclose (f);
	return s;
}
static std::string card (const char *rel) { return slurp (W + "/" + rel); }
static void put (const char *rel, const std::string &text)
{
	std::string p = W + "/" + rel;
	if (system (("mkdir -p \"$(dirname '" + p + "')\"").c_str ()) != 0) exit (2);
	FILE *f = fopen (p.c_str (), "wb"); fwrite (text.data (), 1, text.size (), f); fclose (f);
}
static void fresh () { if (system (("rm -rf '" + W + "' && mkdir -p '" + W + "/etc'").c_str ()) != 0) exit (2); }
static std::vector<std::string> lines (const std::string &s)
{
	std::vector<std::string> v; size_t i = 0;
	while (i < s.size ()) { size_t e = s.find ('\n', i); if (e == std::string::npos) e = s.size (); v.push_back (s.substr (i, e - i)); i = e + 1; }
	return v;
}
static bool has_line (const std::string &s, const std::string &l) { for (auto &x : lines (s)) if (x == l) return true; return false; }
static int index_of (const std::string &s, const std::string &l) { auto v = lines (s); for (size_t i = 0; i < v.size (); i++) if (v[i] == l) return (int) i; return -1; }

int main ()
{
	const char *w = getenv ("SIM_WRITES"), *old = getenv ("OLD_AUTOSTART");
	if (!w || !*w || !old) { fprintf (stderr, "session: SIM_WRITES / OLD_AUTOSTART not set\n"); return 2; }
	W = w;
	const std::string OLD = slurp (old), NEWAS = slurp ("sdcard/etc/autostart"), DESK = slurp ("sdcard/etc/session/desktop");
	const std::string POCKET = slurp ("sdcard/etc/session/pocket"), CONSOLE = slurp ("sdcard/etc/session/console");
	CHECK (OLD != "<none>" && NEWAS != "<none>" && DESK != "<none>" && POCKET != "<none>" && CONSOLE != "<none>");

	// ---- the split of the old card's autostart (what pkg commit does once) ----
	fresh (); put ("etc/autostart", OLD);
	CHECK (session_migrate () == 1);
	std::string sys = card ("etc/autostart"), desk = card ("etc/session/desktop");
	CHECK_EQ (card ("etc/autostart.old"), OLD);
	// every line of the old file is in exactly one of the two, in its order; the desktop's in the desktop's
	{
		auto o = lines (OLD), a0 = lines (sys), d0 = lines (desk);
		std::vector<std::string> a, d;
		for (size_t i = 0; i < a0.size (); i++)
			if (!(a0[i] == "session" || (i + 1 < a0.size () && a0[i + 1] == "session") || (i + 2 < a0.size () && a0[i + 2] == "session"))) a.push_back (a0[i]);
		for (size_t i = 3; i < d0.size (); i++) d.push_back (d0[i]);
		size_t ia = 0, id = 0; bool ok = true;
		for (auto &l : o)
		{
			if (ia < a.size () && a[ia] == l) ia++;
			else if (id < d.size () && d[id] == l) id++;
			else { fprintf (stderr, "session: the old line lost or moved: %s\n", l.c_str ()); ok = false; }
		}
		CHECK (ok && ia == a.size () && id == d.size ());
		CHECK (a0.size () + d0.size () == o.size () + 3 + 3);	// (+ the session's block, + the desktop's head)
	}
	for (const char *l : { "run voronoy", "run setup", "#setup: run menubar", "run notifyd", "#setup: run dock", "#setup: run agenda", "#setup: run stickies",
			       "# System menu bar across the top of the screen: the active app's menus + the clock." })
		{ CHECK (has_line (desk, l) && !has_line (sys, l)); }
	for (const char *l : { "wait pkg commit", "session", "run clockd", "run pkgd", "run clipd", "run printd", "keyb FR", "telnetd", "vncd", "rdpd", "preload /boot" })
		{ CHECK (has_line (sys, l) && !has_line (desk, l)); }
	CHECK (index_of (sys, "session") == index_of (sys, "wait pkg commit") + 3);	// (where voronoy was: after the block's comment)
	CHECK (lines (sys).back () == "preload /boot");
	for (auto &l : lines (desk)) CHECK (l.compare (0, 7, "#setup#") != 0 || has_line (OLD, l));
	CHECK (lines (desk).size () > 3 && index_of (desk, "run setup") > index_of (desk, "run voronoy"));
	// once: nothing the second time; the new card's own autostart: nothing
	CHECK (session_migrate () == 0);
	CHECK_EQ (card ("etc/autostart"), sys);
	fresh (); put ("etc/autostart", NEWAS);
	CHECK (session_migrate () == 0);
	CHECK_EQ (card ("etc/session/desktop"), "<none>");
	// "#session" (turned off by hand), "#setup: session": done
	fresh (); put ("etc/autostart", "#session\nrun menubar\n"); CHECK (session_migrate () == 0);
	fresh (); put ("etc/autostart", "#setup: session --x\nrun menubar\n"); CHECK (session_migrate () == 0);
	// "# session ..." (a comment) is not the line
	fresh (); put ("etc/autostart", "# session files are new\nrun menubar\n"); CHECK (session_migrate () == 1);
	// no autostart: nothing
	fresh (); CHECK (session_migrate () == 0); CHECK_EQ (card ("etc/session/desktop"), "<none>");
	// the user's own lines stay in the autostart (a terminal at boot, a sleep); the comment just above a desktop line
	// moves with it, a comment above a blank line stays
	fresh (); put ("etc/autostart", "wait pkg commit\n# mine\nrun terminal\n\n# the bar\nrun menubar\nsleep 2\n# a note\n\nrun dock\nkeyb BE\npreload /boot\n");
	CHECK (session_migrate () == 1);
	CHECK_EQ (card ("etc/autostart"), std::string ("wait pkg commit\n# mine\nrun terminal\n\n") +
		  "# The session: the programs of the interface's mode (SD:/etc/system.ini \"shell =\": desktop, pocket or\n"
		  "# console) are in SD:/etc/session/<mode>, which /bin/session runs (and switches: the Control Panel's Mode).\n"
		  "session\nsleep 2\n# a note\n\nkeyb BE\npreload /boot\n");
	CHECK (card ("etc/session/desktop").find ("# the bar\nrun menubar\nrun dock\n") != std::string::npos);
	// no desktop line: the session's line before the preload's comment and line; none: at the end
	fresh (); put ("etc/autostart", "telnetd\n# loaded ahead\npreload /boot\n");
	CHECK (session_migrate () == 1);
	std::string a2 = card ("etc/autostart");
	CHECK (index_of (a2, "session") == 3 && index_of (a2, "# loaded ahead") == 4);
	fresh (); put ("etc/autostart", "telnetd");
	CHECK (session_migrate () == 1);
	CHECK (lines (card ("etc/autostart"))[0] == "telnetd" && lines (card ("etc/autostart"))[3] == "session");
	// "\r\n" lines (a file edited on Windows): moved as they are
	fresh (); put ("etc/autostart", "keyb FR\r\nrun menubar\r\npreload /boot\r\n");
	CHECK (session_migrate () == 1);
	CHECK (card ("etc/session/desktop").find ("run menubar\r\n") != std::string::npos);
	CHECK (card ("etc/autostart").find ("keyb FR\r\n") == 0);
	// too big: left alone; read-only: left alone
	{
		std::string big;
		while (big.size () < 20000) big += "# a long comment line, many of them, to make the file larger than the split takes\n";
		fresh (); put ("etc/autostart", big + "run menubar\n");
		CHECK (session_migrate () == -1);
		CHECK_EQ (card ("etc/session/desktop"), "<none>");
	}
	fresh (); put ("etc/autostart", OLD);
	setenv ("SIM_ROFS", "SD:/etc", 1);
	CHECK (session_migrate () == -1);
	unsetenv ("SIM_ROFS");
	CHECK_EQ (card ("etc/autostart"), OLD);

	// ---- the session files' programs, the parsing of a line ----
	fresh (); put ("etc/session/desktop", DESK); put ("etc/session/pocket", POCKET); put ("etc/session/console", CONSOLE);
	char b[1024];
	CHECK (session_programs (SESSION_DESKTOP, b, sizeof b) == 7);
	CHECK_EQ (b, "voronoy\nsetup\nmenubar\nnotifyd\ndock\nagenda\nstickies\n");
	CHECK (session_programs (SESSION_POCKET, b, sizeof b) == 2);
	CHECK_EQ (b, "menubar\npocketshell\n");
	CHECK (session_programs (SESSION_CONSOLE, b, sizeof b) == 2);
	CHECK_EQ (b, "terminal\ngamelib\n");
	CHECK (session_programs (7, b, sizeof b) == 0 && b[0] == 0);
	put ("etc/session/pocket", "  run   tinypad SD:/x.txt\nwait pkg commit\nsleep 3\n#setup: run dock\n# run no\nkeyb FR\nrun SD:/basic/demo.bas\nrun\nwait\n");
	CHECK (session_programs (SESSION_POCKET, b, sizeof b) == 5);
	CHECK_EQ (b, "tinypad\npkg\ndock\nkeyb\ndemo\n");
	CHECK (session_programs (SESSION_POCKET, b, 13) == 2 && session_programs (SESSION_POCKET, b, 12) == 1);		// (what fits)
	fresh (); CHECK (session_programs (SESSION_DESKTOP, b, sizeof b) == 0);
	char path[64];
	CHECK (session_file (SESSION_CONSOLE, path, sizeof path) > 0 && std::string (path) == "SD:/etc/session/console");
	CHECK (session_file (3, path, sizeof path) == 0 && session_file (0, path, 8) == 0);

	// ---- the modes ----
	CHECK (session_modes () == 3);
	CHECK_EQ (session_mode_name (1), "pocket"); CHECK_EQ (session_mode_name (-1), "");
	CHECK (session_mode_find ("Console") == 2 && session_mode_find (" desktop\r") == 0 && session_mode_find ("pocketx") == -1 && session_mode_find (0) == -1);
	fresh ();
	CHECK (session_mode () == SESSION_DESKTOP);				// (no system.ini)
	put ("etc/system.ini", "verbose=0\nshell = POCKET \ntimezone=120\n");
	CHECK (session_mode () == SESSION_POCKET);
	CHECK (session_set_mode (SESSION_CONSOLE) == 1 && session_mode () == SESSION_CONSOLE);
	CHECK_EQ (card ("etc/system.ini"), "verbose=0\nshell=console\ntimezone=120\n");
	CHECK (session_set_mode (5) == 0);
	put ("etc/system.ini", "shell=netbook\n"); CHECK (session_mode () == SESSION_DESKTOP);
	// session_waiting
	fresh (); CHECK (session_waiting (b, sizeof b) == 0);
	put ("tmp/session.wait", "ledger\tLedger - books\ntinypad\tUntitled"); CHECK (session_waiting (b, sizeof b) == 2);

	// ---- autostart_has / autostart_ensure across the files (Notes' Stickies, the Clock's clockd) ----
	fresh ();
	std::string deskNo;
	for (auto &l : lines (DESK)) if (l != "#setup: run stickies" && l.compare (0, 10, "# Stickies") != 0) deskNo += l + "\n";
	put ("etc/autostart", NEWAS); put ("etc/session/desktop", deskNo);
	CHECK (autostart_has ("run agenda") == 1 && autostart_has ("run clockd") == 1 && autostart_has ("run stickies") == 0);
	CHECK (autostart_ensure ("run stickies", "run agenda", "# Stickies: the pinned notes on the desktop (Notes, View menu)") == 2);
	CHECK_EQ (card ("etc/autostart"), NEWAS);				// (untouched)
	CHECK (card ("etc/session/desktop").find ("#setup: run agenda\n# Stickies: the pinned notes on the desktop (Notes, View menu)\n#setup: run stickies\n") != std::string::npos);
	CHECK (autostart_ensure ("run stickies", "run agenda", 0) == 1);
	put ("etc/session/pocket", "run stickies\n");
	CHECK (autostart_has ("run stickies") == 1);
	// clockd, anchored on the line `session`: in the autostart, right after it
	std::string asNo;
	for (auto &l : lines (NEWAS)) if (l != "run clockd") asNo += l + "\n";
	put ("etc/autostart", asNo);
	CHECK (autostart_ensure ("run clockd", "session", "# clockd") == 2);
	CHECK (card ("etc/autostart").find ("\nsession\n# clockd\nrun clockd\n") != std::string::npos);
	// no anchor anywhere: before preload in the autostart
	CHECK (autostart_ensure ("run x", "run nothing", 0) == 2);
	CHECK (card ("etc/autostart").find ("run x\npreload /boot\n") != std::string::npos);
	CHECK_EQ (card ("etc/session/console"), "<none>");

	// ---- Setup's end on the new card: the session's file given back, the autostart's keyb and services ----
	fresh (); put ("etc/autostart", NEWAS); put ("etc/session/desktop", DESK); put ("etc/session/pocket", POCKET); put ("etc/session/console", CONSOLE);
	{
		bool on[4] = { true, false, false, false };
		static char held[2048];
		CHECK (autostart_finish ("BE", on, held, sizeof held));
		CHECK_EQ (held, "run menubar\nrun dock\nrun agenda\nrun stickies\n");
		std::string d = card ("etc/session/desktop"), a = card ("etc/autostart");
		CHECK (d.find ("#setup") == std::string::npos && !has_line (d, "run setup") && has_line (d, "run menubar") && has_line (d, "run voronoy"));
		CHECK (has_line (a, "keyb BE") && has_line (a, "telnetd") && has_line (a, "#vncd") && has_line (a, "#rdpd") && has_line (a, "session"));
		CHECK_EQ (card ("etc/session/pocket"), POCKET);			// (no Setup line: not written)
	}

	if (g_fail) { fprintf (stderr, "session: %d of %d checks FAILED\n", g_fail, g_checks); return 1; }
	fprintf (stderr, "session: %d checks passed\n", g_checks);
	return 0;
}
