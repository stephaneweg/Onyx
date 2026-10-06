//
// tools/tests/filekit/kvtest.cpp -- FileKit's key / value text documents (user/Kits/filekit/kvtext.h, fk_kv_*) on the
// PC: parsing, the blocks and the lines, the "|" values and the escapes, changes, the text written and read back, the
// files through the desktop simulator's kapi (fakekapi.cpp: SIM_WRITES; KV_NO_FILES: without them, the AddressSanitizer
// build -- fakekapi maps the kapi table where ASan keeps its shadow). Run by tools/tests/run_kvtext_test.sh.
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
#include "filekit/filekit.h"
#include <stdio.h>
#include <string.h>

static int g_checks, g_fails;
#define CHECK(c) do { g_checks++; if (!(c)) { g_fails++; printf ("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define EQS(a, b) do { g_checks++; const char *a_ = (a), *b_ = (b); if (strcmp (a_, b_)) { g_fails++; printf ("FAIL %s:%d: %s = \"%s\", expected \"%s\"\n", __FILE__, __LINE__, #a, a_, b_); } } while (0)
#define EQI(a, b) do { g_checks++; long a_ = (long) (a), b_ = (long) (b); if (a_ != b_) { g_fails++; printf ("FAIL %s:%d: %s = %ld, expected %ld\n", __FILE__, __LINE__, #a, a_, b_); } } while (0)

// Two documents with the same entries (keys, values, sections, blocks' names)
static bool same_entries (const fk_kv *a, const fk_kv *b)
{
	if (fk_kv_count (a) != fk_kv_count (b) || fk_kv_blocks (a) != fk_kv_blocks (b)) return false;
	for (int i = 0; i < fk_kv_count (a); i++)
		if (strcmp (fk_kv_key (a, i), fk_kv_key (b, i)) || strcmp (fk_kv_value (a, i), fk_kv_value (b, i))
		    || strcmp (fk_kv_section (a, i), fk_kv_section (b, i)) || fk_kv_block (a, i) != fk_kv_block (b, i)) return false;
	for (int k = 1; k <= fk_kv_blocks (a); k++) if (strcmp (fk_kv_block_name (a, k), fk_kv_block_name (b, k))) return false;
	return true;
}

static const char *PACK =
	"# a pack\n"			// 1
	"[pack]\n"			// 2
	"title = 1. Gates\n"		// 3
	"title.fr = 1. Les portes\n"	// 4
	"\n"				// 5
	"[ level ]\n"			// 6
	"id = and\n"			// 7
	"  par =  1 1  \n"		// 8
	"table =\n"			// 9
	"| A B | Out\n"			// 10
	"| 0 0 | 0\n"			// 11
	"|1 1 | 1\n"			// 12
	"solution = part g1 AND\n"	// 13
	"| wire A g1.1\n"		// 14
	"; a comment\n"			// 15
	"| ignored: after a comment\n"	// 16
	"no equal sign here\n"		// 17
	"[level]\n"			// 18
	"id = or\n"			// 19
	"text =\n"			// 20
	"|\n"				// 21
	"| second\n";			// 22

static void test_parse ()
{
	fk_kv *kv = fk_kv_parse (PACK, FK_KV_PIPES);
	CHECK (kv != 0);
	EQI (fk_kv_blocks (kv), 3);
	EQS (fk_kv_block_name (kv, 1), "pack");
	EQS (fk_kv_block_name (kv, 2), "level");			// (trimmed)
	EQS (fk_kv_block_name (kv, 3), "level");
	EQS (fk_kv_block_name (kv, 0), "");
	EQS (fk_kv_block_name (kv, 4), "");
	EQI (fk_kv_block_line (kv, 2), 6);
	EQI (fk_kv_block_line (kv, 3), 18);
	EQI (fk_kv_count (kv), 8);
	EQS (fk_kv_key (kv, 0), "title");
	EQS (fk_kv_value (kv, 1), "1. Les portes");			// key.fr: a key as any other
	EQS (fk_kv_key (kv, 1), "title.fr");
	EQI (fk_kv_block (kv, 0), 1);
	EQS (fk_kv_section (kv, 0), "pack");
	EQS (fk_kv_value (kv, 3), "1 1");				// (trimmed)
	EQI (fk_kv_line (kv, 3), 8);
	EQS (fk_kv_key (kv, 4), "table");
	EQS (fk_kv_value (kv, 4), "A B | Out\n0 0 | 0\n1 1 | 1");
	EQI (fk_kv_line (kv, 4), 10);					// its first "|" line: row j at 10 + j
	EQS (fk_kv_value (kv, 5), "part g1 AND\nwire A g1.1");		// the key's line goes on
	EQI (fk_kv_line (kv, 5), 13);
	EQI (fk_kv_block (kv, 6), 3);
	EQS (fk_kv_value (kv, 7), "\nsecond");			// an empty first "|" line kept
	EQI (fk_kv_line (kv, 7), 21);
	EQS (fk_kv_get (kv, "level", "id", "?"), "and");		// the first match
	EQS (fk_kv_get (kv, "pack", "id", "?"), "?");
	EQS (fk_kv_get (kv, "Level", "id", "?"), "?");			// case-sensitive
	EQS (fk_kv_get (kv, "", "id", "none"), "none");
	EQS (fk_kv_key (kv, 99), ""); EQS (fk_kv_value (kv, -1), ""); EQI (fk_kv_line (kv, 99), 0); EQI (fk_kv_block (kv, 99), 0);
	fk_kv_free (kv);

	// without FK_KV_PIPES the "|" lines are lines without '=' (skipped)
	kv = fk_kv_parse ("t =\n| a\n| b = c\n", 0);
	EQI (fk_kv_count (kv), 2); EQS (fk_kv_value (kv, 0), ""); EQS (fk_kv_key (kv, 1), "| b");
	fk_kv_free (kv);
	// a "|" line with nothing before it; a blank line ends a block
	kv = fk_kv_parse ("| lost\nk = v\n\n| lost too\n", FK_KV_PIPES);
	EQI (fk_kv_count (kv), 1); EQS (fk_kv_value (kv, 0), "v");
	fk_kv_free (kv);
	// \r\n, a BOM, keys before any header, an empty key skipped, "=" in a value
	kv = fk_kv_parse ("\xEF\xBB\xBF" "a = 1\r\n = x\r\nb=2=3\r\n[s]\r\nc = \r\n", 0);
	EQI (fk_kv_count (kv), 3);
	EQS (fk_kv_key (kv, 0), "a"); EQS (fk_kv_value (kv, 0), "1"); EQI (fk_kv_block (kv, 0), 0); EQS (fk_kv_section (kv, 0), "");
	EQS (fk_kv_get (kv, "", "b", ""), "2=3"); EQS (fk_kv_get (kv, 0, "b", ""), "2=3");
	EQS (fk_kv_get (kv, "s", "c", "?"), ""); EQI (fk_kv_line (kv, 2), 5);
	fk_kv_free (kv);
	// empty and null texts
	kv = fk_kv_parse ("", 0); CHECK (kv && fk_kv_count (kv) == 0 && fk_kv_blocks (kv) == 0); fk_kv_free (kv);
	kv = fk_kv_parse (0, 0); CHECK (kv && fk_kv_count (kv) == 0); fk_kv_free (kv);
	fk_kv_free (0);
	EQI (fk_kv_count (0), 0); EQS (fk_kv_get (0, "", "a", "d"), "d");
}

static void test_escapes ()
{
	fk_kv *kv = fk_kv_parse ("[Player]\nand.circuit = part g1 AND 8 4\\nwire A g1.1\nodd = a\\tb\\\\c\\\n", FK_KV_ESCAPES);
	EQS (fk_kv_get (kv, "Player", "and.circuit", ""), "part g1 AND 8 4\nwire A g1.1");
	EQS (fk_kv_get (kv, "Player", "odd", ""), "a\\tb\\c\\");		// (an unknown escape kept as it is)
	CHECK (fk_kv_set (kv, "Player", "x", "a\nb\\c") == 0);
	int n = 0;
	const char *t = fk_kv_text (kv, 0, &n);
	CHECK (strstr (t, "x = a\\nb\\\\c\n") != 0);
	CHECK (n == (int) strlen (t));
	fk_kv *back = fk_kv_parse (t, FK_KV_ESCAPES);
	CHECK (same_entries (kv, back));
	EQS (fk_kv_get (back, "Player", "x", ""), "a\nb\\c");
	fk_kv_free (back); fk_kv_free (kv);
}

static void test_changes ()
{
	fk_kv *kv = fk_kv_new (FK_KV_ESCAPES);
	CHECK (kv != 0);
	EQI (fk_kv_count (kv), 0);
	CHECK (fk_kv_set (kv, "Player", "wire", "3") == 0);		// a new block at the end
	EQI (fk_kv_blocks (kv), 1); EQS (fk_kv_block_name (kv, 1), "Player"); EQI (fk_kv_block_line (kv, 1), 0);
	CHECK (fk_kv_set (kv, "", "pack", "1-gates.circuits") == 0);	// before the first header
	CHECK (fk_kv_set (kv, "Player", "not", "2") == 0);
	CHECK (fk_kv_set (kv, "", "level", "not") == 0);
	EQS (fk_kv_key (kv, 0), "pack"); EQS (fk_kv_key (kv, 1), "level"); EQS (fk_kv_key (kv, 2), "wire"); EQS (fk_kv_key (kv, 3), "not");
	EQI (fk_kv_line (kv, 0), 0);
	CHECK (fk_kv_set (kv, "Player", "wire", "1") == 0);		// replaced, in its place
	EQI (fk_kv_count (kv), 4); EQS (fk_kv_value (kv, 2), "1");
	CHECK (fk_kv_set (kv, "Player", "", "x") == -1);		// (no key)
	int n = 0;
	const char *t = fk_kv_text (kv, "# Circuits -- the player's progress (written by the game)", &n);
	EQS (t, "# Circuits -- the player's progress (written by the game)\npack = 1-gates.circuits\nlevel = not\n\n[Player]\nwire = 1\nnot = 2\n");
	EQI (n, (int) strlen (t));
	t = fk_kv_text (kv, "a comment", 0);
	CHECK (!strncmp (t, "# a comment\npack", 16));
	EQI (fk_kv_remove (kv, "Player", "wire"), 1);
	EQI (fk_kv_remove (kv, "Player", "wire"), 0);
	EQI (fk_kv_remove (kv, "", "nothing"), 0);
	EQI (fk_kv_count (kv), 3);
	EQS (fk_kv_get (kv, "Player", "wire", "gone"), "gone");
	fk_kv_free (kv);

	// repeated blocks: set goes to the FIRST block of that name; unknown keys kept on the way back
	kv = fk_kv_parse ("top = 1\n[a]\nk = 1\n[b]\nk = 2\n[a]\nk = 3\nfuture = kept\n", 0);
	CHECK (fk_kv_set (kv, "a", "new", "n") == 0);
	EQS (fk_kv_key (kv, 2), "new"); EQI (fk_kv_block (kv, 2), 1);
	CHECK (fk_kv_set (kv, "b", "k", "two") == 0);
	t = fk_kv_text (kv, 0, &n);
	EQS (t, "top = 1\n\n[a]\nk = 1\nnew = n\n\n[b]\nk = two\n\n[a]\nk = 3\nfuture = kept\n");
	fk_kv *back = fk_kv_parse (t, 0);
	CHECK (same_entries (kv, back));
	fk_kv_free (back); fk_kv_free (kv);

	// an empty document's text; a value with new lines and neither flag: spaces
	kv = fk_kv_new (0);
	t = fk_kv_text (kv, 0, &n); EQS (t, ""); EQI (n, 0);
	fk_kv_set (kv, "", "k", "a\nb"); EQS (fk_kv_text (kv, 0, 0), "k = a b\n");
	fk_kv_set (kv, "", "e", ""); EQS (fk_kv_text (kv, 0, 0), "k = a b\ne =\n");
	fk_kv_free (kv);
}

static void test_roundtrip_pipes ()
{
	fk_kv *kv = fk_kv_parse (PACK, FK_KV_PIPES);
	int n = 0;
	const char *t = fk_kv_text (kv, "# written", &n);
	CHECK (strstr (t, "table =\n| A B | Out\n| 0 0 | 0\n| 1 1 | 1\n") != 0);
	CHECK (strstr (t, "text =\n| \n| second\n") != 0);
	fk_kv *back = fk_kv_parse (t, FK_KV_PIPES);
	CHECK (same_entries (kv, back));
	// the written text read and written again: the same text
	int n2 = 0;
	char *copy = strdup (t);
	const char *t2 = fk_kv_text (back, "# written", &n2);
	EQS (t2, copy); EQI (n2, n);
	free (copy);
	fk_kv_free (back); fk_kv_free (kv);
	// both flags: escapes win (one line a value)
	kv = fk_kv_new (FK_KV_PIPES | FK_KV_ESCAPES);
	fk_kv_set (kv, "s", "v", "a\nb");
	EQS (fk_kv_text (kv, 0, 0), "[s]\nv = a\\nb\n");
	back = fk_kv_parse (fk_kv_text (kv, 0, 0), FK_KV_PIPES | FK_KV_ESCAPES);
	CHECK (same_entries (kv, back));
	fk_kv_free (back); fk_kv_free (kv);
}

#ifndef KV_NO_FILES
static void test_files ()
{
	// (SIM_WRITES: the run script's own folder; the card's files read from sdcard/)
	CHECK (fk_kv_load ("SD:/no/such/file.ini", 0) == 0);
	fk_kv *kv = fk_kv_new (FK_KV_ESCAPES);
	fk_kv_set (kv, "", "level", "and");
	fk_kv_set (kv, "Player", "and.circuit", "part g1 AND 8 4\nwire A g1.1\nwire B g1.2\nwire g1 Out");
	fk_kv_set (kv, "Player", "seen.and", "1");
	EQI (fk_kv_save (kv, "SD:/apps/kvtest.app/progress.ini", "# kvtest"), 0);
	fk_kv *back = fk_kv_load ("SD:/apps/kvtest.app/progress.ini", FK_KV_ESCAPES);
	CHECK (back != 0);
	CHECK (same_entries (kv, back));
	EQS (fk_kv_get (back, "Player", "and.circuit", ""), "part g1 AND 8 4\nwire A g1.1\nwire B g1.2\nwire g1 Out");
	EQI (fk_kv_line (back, 0), 2);						// (after the comment line)
	fk_kv_free (back);
	// an empty file: an empty document (not 0)
	fk_kv *empty = fk_kv_new (0);
	EQI (fk_kv_save (empty, "SD:/apps/kvtest.app/empty.ini", 0), 0);
	back = fk_kv_load ("SD:/apps/kvtest.app/empty.ini", 0);
	CHECK (back != 0 && fk_kv_count (back) == 0);
	fk_kv_free (back); fk_kv_free (empty); fk_kv_free (kv);
	// a file of the card: Turtle Quest's first pack, its [level] blocks
	kv = fk_kv_load ("SD:/apps/turtle.app/levels/1-first-steps.turtle", FK_KV_PIPES);
	CHECK (kv != 0);
	if (kv)
	{
		CHECK (fk_kv_blocks (kv) > 2);
		EQS (fk_kv_block_name (kv, 1), "pack");
		EQS (fk_kv_get (kv, "level", "id", ""), "hello");
		int m = 0; while (m < fk_kv_count (kv) && strcmp (fk_kv_key (kv, m), "map")) m++;
		EQS (fk_kv_value (kv, m), "#######\n#>...*#\n#######");	// (map =, its "|" lines)
		EQI (fk_kv_line (kv, m), 18);
		fk_kv_free (kv);
	}
}
#endif

int main ()
{
	test_parse ();
	test_escapes ();
	test_changes ();
	test_roundtrip_pipes ();
#ifndef KV_NO_FILES
	test_files ();
#endif
	if (g_fails) { printf ("FAIL kvtext (%d of %d checks)\n", g_fails, g_checks); return 1; }
	printf ("ok   kvtext (%d checks)\n", g_checks);
	return 0;
}
