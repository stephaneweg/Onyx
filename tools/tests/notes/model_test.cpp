//
// model_test.cpp -- the notes model (user/Apps/notes/notesmodel.cpp) on the PC, against the desktop simulator's
// stand-in kernel (fakekapi.o: the files under SIM_WRITES, its fixed clock Monday 2026-09-28 12:34:00, SIM_STAT=1
// for the files' times). Run by tools/tests/run_notes_test.sh from the repository's root (the sample notes:
// tools/tests/desktop_sim/sd/Notes). Covers AC 16, 17, 18 and the logic of AC 1 (order) and AC 20 (the six
// most recent pinned) of 02-product-analysis.md, the titles, the colours, notes.ini's round trip, the Trash,
// the date labels (G2), Notes' config.ini.
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
#include "Apps/notes/notesmodel.h"
#include "Apps/notes/stickies_proto.h"
#include "appkit/appkit.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/stat.h>
#include <utime.h>

static int g_fail, g_checks;
#define CHECK(c) do { g_checks++; if (!(c)) { fprintf (stderr, "model: FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)
#define CHECK_STR(a, b) do { g_checks++; if (strcmp ((a), (b))) { fprintf (stderr, "model: FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b)); g_fail++; } } while (0)

static std::string W;					// SIM_WRITES
static const char *SAMPLES = "tools/tests/desktop_sim/sd/Notes";
static Notes S;

static std::string slurp (const std::string &p)
{
	FILE *f = fopen (p.c_str (), "rb");
	if (!f) return "<none>";
	std::string s; char b[4096]; size_t n;
	while ((n = fread (b, 1, sizeof b, f)) > 0) s.append (b, n);
	fclose (f);
	return s;
}
static void put (const std::string &p, const std::string &bytes)
{
	FILE *f = fopen (p.c_str (), "wb");
	if (f) { fwrite (bytes.data (), 1, bytes.size (), f); fclose (f); }
}
static bool exists (const std::string &p) { struct stat st; return stat (p.c_str (), &st) == 0; }
// A fresh card: the writes emptied; with the sample notes in Notes/ when samples is true.
static void seed (bool samples)
{
	if (system (("rm -rf '" + W + "' && mkdir -p '" + W + "'").c_str ()) != 0) exit (2);
	if (samples && system (("cp -r " + std::string (SAMPLES) + " '" + W + "/Notes'").c_str ()) != 0) exit (2);
}

static void test_scan_order ()
{
	seed (true);
	CHECK (notes_scan (S) == 6 && S.total == 6);
	static const char *const titles[6] = { "Onyx to-do", "Shopping", "Wi-Fi at the club", "Gift ideas for L\xC3\xA9" "a", "Pi 4 GPIO pins", "Books to read" };
	for (int i = 0; i < 6 && i < S.count; i++) CHECK_STR (S.n[i].title, titles[i]);
	CHECK_STR (S.n[1].file, "note-20260928-091500.txt");
	CHECK_STR (S.n[1].preview, "milk, eggs, butter");
	CHECK (S.n[1].colour == NC_YELLOW && S.n[1].pinned && S.n[1].saved && S.n[1].modified == 20260928091530LL);
	CHECK (S.n[0].colour == NC_BLUE && S.n[2].colour == NC_GREEN && S.n[3].colour == NC_PINK && S.n[4].colour == NC_GREY && S.n[5].colour == NC_PURPLE);
	CHECK (!S.n[3].pinned && !S.n[4].pinned && !S.n[5].pinned);
	CHECK (notes_find (S, "NOTE-20260928-091500.TXT") == 1);		// (FAT: case ignored)
	CHECK (notes_find (S, "SD:/Notes/note-20260902-214000.txt") == 5);
	CHECK (notes_find (S, "note-none.txt") == -1);
	// notes.ini round trip: written back byte for byte (the same order, the same comment line)
	CHECK (notes_save_ini (S));
	CHECK (slurp (W + "/Notes/notes.ini") == slurp (std::string (SAMPLES) + "/notes.ini"));
	// no folder: nothing listed, nothing made
	seed (false);
	CHECK (notes_scan (S) == 0 && S.count == 0);
	CHECK (!exists (W + "/Notes"));
}

static void test_read ()					// AC 16
{
	seed (false);
	mkdir ((W + "/Notes").c_str (), 0755);
	static char buf[NOTE_MAX_BYTES + 1];
	put (W + "/Notes/crlf.txt", "Title\r\nline two\r\n\r\nend\rlast");
	CHECK (notes_read ("crlf.txt", buf, sizeof buf) == 24);
	CHECK_STR (buf, "Title\nline two\n\nend\nlast");
	put (W + "/Notes/bom.txt", "\xEF\xBB\xBFHello\n");
	CHECK (notes_read ("SD:/Notes/bom.txt", buf, sizeof buf) == 6);
	CHECK_STR (buf, "Hello\n");
	put (W + "/Notes/u16.txt", std::string ("\xFF\xFEH\0i\0\r\0\n\0!\0", 12));
	CHECK (notes_read ("u16.txt", buf, sizeof buf) == 4);
	CHECK_STR (buf, "Hi\n!");
	std::string big (NOTE_MAX_BYTES, 'a');
	put (W + "/Notes/max.txt", big);
	CHECK (notes_read ("max.txt", buf, sizeof buf) == NOTE_MAX_BYTES);	// exactly 64 KB: kept
	put (W + "/Notes/over.txt", big + "b");
	CHECK (notes_read ("over.txt", buf, sizeof buf) == -2);			// one byte more: refused, never cut
	put (W + "/Notes/huge.txt", std::string (3 * NOTE_MAX_BYTES, 'c'));
	CHECK (notes_read ("huge.txt", buf, sizeof buf) == -2);
	char small[8];
	CHECK (notes_read ("crlf.txt", small, sizeof small) == -3);
	CHECK (notes_read ("none.txt", buf, sizeof buf) == -1);
	// a file elsewhere (a drop): a path
	put (W + "/drop.md", "# Dropped\r\ntext");
	CHECK (notes_read ("SD:/drop.md", buf, sizeof buf) == 14 && !strcmp (buf, "# Dropped\ntext"));
	// writing: 64 KB at most
	S.count = 0;
	CHECK (notes_write (S, "w.txt", big.c_str (), NOTE_MAX_BYTES, 20260928123400LL) == 0);
	CHECK (notes_write (S, "w2.txt", (big + "b").c_str (), NOTE_MAX_BYTES + 1, 20260928123400LL) == -2);
	CHECK (!exists (W + "/Notes/w2.txt"));
}

static void test_orphans ()					// AC 17
{
	seed (true);
	put (W + "/Notes/notes-crlf.txt", slurp ("tools/tests/desktop_sim/sd/notes-crlf.txt"));
	struct utimbuf t = { 1789891200, 1789891200 };			// 2026-09-20 08:00 UTC (tz 0 in the simulator)
	utime ((W + "/Notes/notes-crlf.txt").c_str (), &t);
	std::string ini = slurp (W + "/Notes/notes.ini") + "\n[note-gone.txt]\ncolour = pink\npinned = 1\nmodified = 20260928120000\n";
	put (W + "/Notes/notes.ini", ini);
	put (W + "/Notes/readme.md", "not a note");
	mkdir ((W + "/Notes/sub.txt").c_str (), 0755);			// (a folder, even named .txt: skipped)
	CHECK (notes_scan (S) == 7 && S.total == 7);
	int i = notes_find (S, "notes-crlf.txt");
	CHECK (i == 4);							// 2026-09-20: after the 25th, before the 15th
	if (i >= 0)
	{
		CHECK (S.n[i].colour == NC_YELLOW && !S.n[i].pinned && S.n[i].saved);
		CHECK (S.n[i].modified == 20260920080000LL);
		CHECK_STR (S.n[i].title, "Meeting with the printer people");
		CHECK_STR (S.n[i].preview, "bring the A3 samples");
	}
	CHECK (notes_find (S, "note-gone.txt") < 0);
	CHECK (notes_save_ini (S));
	std::string after = slurp (W + "/Notes/notes.ini");
	CHECK (after.find ("note-gone.txt") == std::string::npos);		// a section without a file: dropped
	CHECK (after.find ("[notes-crlf.txt]\ncolour = yellow\npinned = 0\nmodified = 20260920080000\n") != std::string::npos);
	// an inline comment and an unknown colour in notes.ini
	put (W + "/Notes/notes.ini", "[note-20260928-091500.txt]\ncolour = GREEN ; the shop\npinned = 1\n[note-20260915-101500.txt]\ncolour = mauve\nmodified = 20260101000000\n");
	notes_scan (S);
	i = notes_find (S, "note-20260928-091500.txt");
	CHECK (i >= 0 && S.n[i].colour == NC_GREEN && S.n[i].pinned);
	i = notes_find (S, "note-20260915-101500.txt");
	CHECK (i >= 0 && S.n[i].colour == NC_YELLOW && !S.n[i].pinned && S.n[i].modified == 20260101000000LL);
}

static void test_names ()					// AC 18
{
	seed (false);
	S.count = 0;
	long long now = notes_now ();
	CHECK (now == 20260928123400LL);
	char a[NOTE_FILE], b[NOTE_FILE], c[NOTE_FILE];
	CHECK (notes_new_name (S, a, sizeof a, now));
	CHECK_STR (a, "note-20260928-123400.txt");
	CHECK (notes_write (S, a, "First\n", 6, now) == 0);
	CHECK (exists (W + "/Notes/note-20260928-123400.txt"));
	CHECK (notes_new_name (S, b, sizeof b, now));
	CHECK_STR (b, "note-20260928-123400-2.txt");
	// a new note not written yet holds its name
	CHECK (notes_add_new (S, now) == 0);
	CHECK_STR (S.n[0].file, "note-20260928-123400-2.txt");
	CHECK (!S.n[0].saved && S.n[0].title[0] == 0 && S.n[0].modified == now);
	CHECK (notes_new_name (S, c, sizeof c, now));
	CHECK_STR (c, "note-20260928-123400-3.txt");
	CHECK (!exists (W + "/Notes/note-20260928-123400-2.txt"));		// (nothing written for it)
	// a file put there by another program takes a name too
	put (W + "/Notes/note-20260928-123400-3.txt", "x");
	CHECK (notes_new_name (S, c, sizeof c, now) && !strcmp (c, "note-20260928-123400-4.txt"));
	char tiny[8];
	CHECK (!notes_new_name (S, tiny, sizeof tiny, now));
	// the unsaved note gets no section in notes.ini; forgotten, it leaves nothing
	CHECK (notes_save_ini (S));
	std::string ini = slurp (W + "/Notes/notes.ini");
	CHECK (ini.find ("-123400-2.txt") == std::string::npos && ini.find ("[note-20260928-123400.txt]") != std::string::npos);
	notes_forget (S, 0);
	CHECK (S.count == 1 && !strcmp (S.n[0].file, "note-20260928-123400.txt"));
	// writing it keeps its name, whatever its title (AC 7)
	CHECK (notes_write (S, a, "Another title\nbody", 18, now + 5) == 0);
	CHECK (notes_find (S, a) == 0 && !strcmp (S.n[0].title, "Another title") && S.n[0].modified == now + 5);
	CHECK (slurp (W + "/Notes/note-20260928-123400.txt") == "Another title\nbody");
}

static void test_titles ()
{
	char t[NOTE_TITLE];
	const char *s = "\n   \n\t  Groceries  \n  second line \n";
	notes_title (s, (int) strlen (s), t, sizeof t); CHECK_STR (t, "Groceries");
	notes_preview (s, (int) strlen (s), t, sizeof t); CHECK_STR (t, "second line");
	notes_title ("", 0, t, sizeof t); CHECK_STR (t, "");
	notes_preview ("Only\n\n  \n", 9, t, sizeof t); CHECK_STR (t, "");
	notes_title ("Title\r\nx", 8, t, sizeof t); CHECK_STR (t, "Title");
	// cut at a character: "Léa" (L, 0xC3 0xA9, a) in 3 bytes of room -> "L"
	char c4[4];
	notes_title ("L\xC3\xA9" "a", 4, c4, sizeof c4); CHECK_STR (c4, "L\xC3\xA9");
	char c3[3];
	notes_title ("L\xC3\xA9" "a", 4, c3, sizeof c3); CHECK_STR (c3, "L");
	// only the first len bytes
	notes_title ("abcdef", 3, t, sizeof t); CHECK_STR (t, "abc");
	std::string longl (300, 'x');
	notes_title (longl.c_str (), 300, t, sizeof t); CHECK ((int) strlen (t) == NOTE_TITLE - 1);
}

static void test_colours ()
{
	for (int c = 0; c < NC_COUNT; c++) CHECK (notes_colour_parse (notes_colour_name (c)) == c);
	CHECK (notes_colour_parse ("Purple") == NC_PURPLE && notes_colour_parse ("gray") == NC_GREY);
	CHECK (notes_colour_parse ("mauve") == NC_YELLOW && notes_colour_parse ("") == NC_YELLOW && notes_colour_parse (0) == NC_YELLOW);
	CHECK_STR (notes_colour_name (99), "yellow");
	CHECK_STR (notes_colour_label (NC_GREY), "Grey");
	CHECK (notes_colour_paper (NC_YELLOW) == 0x00FCE9A6u && notes_colour_dot (NC_YELLOW) == 0x00E8B21Fu);
	CHECK (notes_colour_paper (NC_GREY) == 0x00E4E2DEu && notes_colour_dot (NC_PINK) == 0x00D9667Au);
	CHECK (NOTE_INK == 0x002B2925u);
}

static void test_pinned ()					// AC 20's logic: the six most recent of eight pinned
{
	S.count = 0;
	for (int i = 0; i < 10; i++)
	{
		NoteInfo &e = S.n[S.count++];
		memset (&e, 0, sizeof e);
		snprintf (e.file, sizeof e.file, "n%d.txt", i);
		e.pinned = i != 3 && i != 7;					// 8 pinned
		e.saved = true;
		e.modified = 20260901000000LL + (long long) ((i * 7) % 10) * 1000000;	// (not in order)
	}
	int idx[6];
	int k = notes_pinned (S, idx, 6);
	CHECK (k == 6);
	for (int j = 1; j < k; j++) CHECK (S.n[idx[j - 1]].modified > S.n[idx[j]].modified);
	// the two left out are the oldest pinned: modified days 01 (i 0) and 05 (i 5)... i.e. the smallest
	for (int j = 0; j < k; j++) CHECK (idx[j] != 0);
	CHECK (S.n[idx[5]].modified >= 20260904000000LL);
	CHECK (notes_pinned (S, idx, 0) == 0);
	S.n[9].saved = false;						// (a new note never written: not on the desktop)
	int all[10];
	CHECK (notes_pinned (S, all, 10) == 7);
}

static void test_trash ()					// the Trash (AC 8's model side)
{
	seed (true);
	notes_scan (S);
	CHECK (notes_trash (S, "note-20260915-101500.txt"));
	CHECK (S.count == 5 && notes_find (S, "note-20260915-101500.txt") < 0);
	CHECK (!exists (W + "/Notes/note-20260915-101500.txt"));
	CHECK (exists (W + "/.Trash/files/note-20260915-101500.txt"));
	CHECK (slurp (W + "/Notes/notes.ini").find ("note-20260915-101500") == std::string::npos);
	CHECK (!notes_trash (S, "note-none.txt"));
	// a new note never written: only forgotten
	notes_add_new (S, 20260928123400LL);
	CHECK (S.count == 6 && notes_trash (S, S.n[0].file) && S.count == 5);
}

static void test_rofs ()					// a read-only card: the write fails, nothing changes
{
	seed (true);
	notes_scan (S);
	setenv ("SIM_ROFS", "SD:/Notes", 1);
	CHECK (notes_write (S, S.n[1].file, "Changed\n", 8, 20260928123400LL) == -1);
	CHECK (S.n[1].modified == 20260928091530LL && !strcmp (S.n[1].title, "Shopping"));
	CHECK (!notes_save_ini (S));
	unsetenv ("SIM_ROFS");
	CHECK (slurp (W + "/Notes/note-20260928-091500.txt").compare (0, 9, "Shopping\n") == 0);
}

static void test_many ()					// more than NOTES_MAX: the newest kept
{
	seed (false);
	mkdir ((W + "/Notes").c_str (), 0755);
	std::string ini = "# x\n";
	for (int i = 0; i < NOTES_MAX + 8; i++)
	{
		char n[64]; snprintf (n, sizeof n, "note-%04d.txt", i);
		put (W + "/Notes/" + n, "Note " + std::to_string (i) + "\n");
		char sec[160]; snprintf (sec, sizeof sec, "[%s]\nmodified = %lld\n", n, 20260101000000LL + (long long) i * 100);
		ini += sec;
	}
	put (W + "/Notes/notes.ini", ini);
	CHECK (notes_scan (S) == NOTES_MAX && S.total == NOTES_MAX + 8);
	CHECK (!strcmp (S.n[0].file, "note-0519.txt") && !strcmp (S.n[NOTES_MAX - 1].file, "note-0008.txt"));
	CHECK (notes_add_new (S, 20260928123400LL) == -1);
}

static void test_dates ()					// G2: 04-ux-design.md 2.2
{
	char d[32]; const long long now = 20260928123400LL;		// Monday 28 September 2026
	notes_date_label (20260928091530LL, now, d, sizeof d); CHECK_STR (d, "09:15");
	notes_date_label (20260928235900LL, now, d, sizeof d); CHECK_STR (d, "23:59");
	notes_date_label (20260927183012LL, now, d, sizeof d); CHECK_STR (d, "Yesterday");
	notes_date_label (20260925200210LL, now, d, sizeof d); CHECK_STR (d, "Fri");
	notes_date_label (20260922080000LL, now, d, sizeof d); CHECK_STR (d, "Tue");
	notes_date_label (20260921080000LL, now, d, sizeof d); CHECK_STR (d, "21 Sep");	// (a week ago: the date)
	notes_date_label (20260915101500LL, now, d, sizeof d); CHECK_STR (d, "15 Sep");
	notes_date_label (20260102000000LL, now, d, sizeof d); CHECK_STR (d, "2 Jan");
	notes_date_label (20250915101500LL, now, d, sizeof d); CHECK_STR (d, "15/09/2025");
	notes_date_label (0, now, d, sizeof d); CHECK_STR (d, "");
	notes_date_label (20271001000000LL, now, d, sizeof d); CHECK_STR (d, "01/10/2027");	// (the future, another year)
	// across the year's change: 31 December seen on 1 January
	notes_date_label (20261231220000LL, 20270101080000LL, d, sizeof d); CHECK_STR (d, "Yesterday");
	notes_date_label (20261228220000LL, 20270101080000LL, d, sizeof d); CHECK_STR (d, "Mon");
	notes_date_label (20261201220000LL, 20270101080000LL, d, sizeof d); CHECK_STR (d, "01/12/2026");
	// across a month and February of a leap year
	notes_date_label (20280229100000LL, 20280301090000LL, d, sizeof d); CHECK_STR (d, "Yesterday");
	char small[4];
	notes_date_label (20260928091530LL, now, small, sizeof small); CHECK_STR (small, "09:");
}

static void test_file_time ()
{
	seed (false);
	put (W + "/a.txt", "x");
	struct utimbuf t = { 1790598840, 1790598840 };			// 2026-09-28 12:34:00 UTC
	utime ((W + "/a.txt").c_str (), &t);
	CHECK (notes_file_time ("SD:/a.txt") == 20260928123400LL);
	CHECK (notes_file_time ("SD:/none.txt") == 0);
	CHECK (notes_file_time (0) == 0);
}

static void test_cfg ()
{
	seed (false);
	NotesCfg c;
	notes_cfg_load (c);
	CHECK (c.stickies == 1 && c.last[0] == 0 && c.width == 760 && c.height == 480 && c.split == 250);
	c.stickies = 0; snprintf (c.last, sizeof c.last, "note-20260928-091500.txt"); c.width = 800; c.split = 300;
	CHECK (notes_cfg_save (c));
	CHECK (slurp (W + "/apps/notes.app/config.ini") == "stickies = 0\nlast = note-20260928-091500.txt\nwidth = 800\nheight = 480\nsplit = 300\n");
	NotesCfg d;
	notes_cfg_load (d);
	CHECK (d.stickies == 0 && !strcmp (d.last, c.last) && d.width == 800 && d.height == 480 && d.split == 300);
	// edited by hand (the Control Panel's App Settings): spaces, a comment, a section, an unknown key
	put (W + "/apps/notes.app/config.ini", "; mine\n[notes]\n  stickies=1   ; on\nsplit = 222\ncolour = blue\n");
	notes_cfg_load (d);
	CHECK (d.stickies == 1 && d.split == 222 && d.width == 760 && d.last[0] == 0);
}

int main ()
{
	const char *w = getenv ("SIM_WRITES");
	if (!w || !*w) { fprintf (stderr, "model: SIM_WRITES not set\n"); return 2; }
	W = w;
	CHECK (NOTES_MSG_OPEN != STK_MSG_RELOAD && STK_MSG_RELOAD != STK_MSG_QUIT);
	test_scan_order ();
	test_read ();
	test_orphans ();
	test_names ();
	test_titles ();
	test_colours ();
	test_pinned ();
	test_trash ();
	test_rofs ();
	test_many ();
	test_dates ();
	test_file_time ();
	test_cfg ();
	if (g_fail) { fprintf (stderr, "model: %d of %d checks FAILED\n", g_fail, g_checks); return 1; }
	fprintf (stderr, "model: %d checks passed\n", g_checks);
	return 0;
}
