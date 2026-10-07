/*
 * lineedit_test.c -- the consoles' line editor (user/Include/lineedit.h, used by the terminal and by
 * telnetd) on the PC: the cursor, insertions and deletions, the history (Up / Down, the line
 * being typed kept, the oldest lines leaving a full history). Run by tools/tests/run_cmd_test.sh.
 *
 * Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence: Permission is
 * hereby granted, free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished to
 * do so, subject to the following conditions: The above copyright notice and this permission
 * notice shall be included in all copies or substantial portions of the Software. THE SOFTWARE
 * IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
 */
#include <stdio.h>
#include <string.h>
#include "../../../user/Include/lineedit.h"

static struct LineEdit e;
static int fails, checks;

static void is (const char *what, const char *want, int cur)
{
	checks++;
	if (strcmp (e.buf, want) != 0 || e.cur != cur || e.len != (int) strlen (want))
	{
		fails++;
		printf ("FAIL %s: got [%s] cur %d len %d, want [%s] cur %d\n", what, e.buf, e.cur, e.len, want, cur);
	}
}
static void type (const char *s) { while (*s) le_insert (&e, *s++); }
static void send (const char *s) { le_clear (&e); type (s); le_commit (&e, 1); }

int main (void)
{
	le_init (&e);
	type ("helo");			is ("typed", "helo", 4);
	le_left (&e);			is ("left", "helo", 3);
	le_insert (&e, 'l');		is ("insert in the middle", "hello", 4);
	le_home (&e); le_delete (&e);	is ("delete at home", "ello", 0);
	le_insert (&e, 'H');		is ("insert at home", "Hello", 1);
	le_end (&e); le_backspace (&e);	is ("backspace at end", "Hell", 4);
	checks++; if (le_right (&e)) { fails++; printf ("FAIL right at the end moved\n"); }
	le_home (&e);
	checks++; if (le_left (&e) || le_backspace (&e)) { fails++; printf ("FAIL left / backspace at home did something\n"); }
	le_right (&e); le_right (&e); le_kill (&e);	is ("kill to the end", "He", 2);
	le_clear (&e);			is ("clear", "", 0);

	/* the history */
	checks++; if (le_up (&e)) { fails++; printf ("FAIL up in an empty history\n"); }
	send ("ls"); send ("cat a"); send ("cat a"); send (""); send ("wc -l");
	checks++; if (le_count (&e) != 3) { fails++; printf ("FAIL history holds %d lines, want 3\n", le_count (&e)); }
	type ("dra");
	le_up (&e);			is ("up 1", "wc -l", 5);
	le_up (&e);			is ("up 2", "cat a", 5);
	le_up (&e);			is ("up 3", "ls", 2);
	checks++; if (le_up (&e)) { fails++; printf ("FAIL up past the oldest\n"); }
	le_down (&e);			is ("down", "cat a", 5);
	le_down (&e); le_down (&e);	is ("down to the typed line", "dra", 3);
	checks++; if (le_down (&e)) { fails++; printf ("FAIL down past the typed line\n"); }
	le_up (&e); le_left (&e); le_insert (&e, 'x');	is ("a recalled line edited", "wc -xl", 5);
	le_commit (&e, 1);		is ("commit empties", "", 0);
	le_up (&e);			is ("the edited line is the newest", "wc -xl", 6);
	le_up (&e);			is ("the original stays", "wc -l", 5);
	le_commit (&e, 0);
	le_up (&e);			is ("a line not remembered", "wc -xl", 6);
	le_commit (&e, 0);

	/* a full history drops its oldest lines; a full line */
	char big[300];
	for (int i = 0; i < 100; i++) { snprintf (big, sizeof big, "%03d %0200d", i, i); send (big); }
	checks++;
	if (e.hlen > LE_HIST || strncmp (le_entry (&e, 1), "099 ", 4) != 0 || le_entry (&e, le_count (&e))[0] != '0')
	{ fails++; printf ("FAIL full history: hlen %d newest [%.4s]\n", e.hlen, le_entry (&e, 1)); }
	int n = le_count (&e);
	for (int i = 0; i < n + 5; i++) le_up (&e);
	checks++; if (e.hpos != n) { fails++; printf ("FAIL hpos %d after too many ups, want %d\n", e.hpos, n); }
	le_commit (&e, 0);
	for (int i = 0; i < LE_LINE + 10; i++) le_insert (&e, 'x');
	checks++; if (e.len != LE_LINE - 1 || e.buf[e.len] != 0) { fails++; printf ("FAIL a full line: len %d\n", e.len); }
	le_commit (&e, 1);
	checks++; if (strlen (le_entry (&e, 1)) != LE_LINE - 1) { fails++; printf ("FAIL the full line in the history\n"); }

	printf ("lineedit: %d checks, %d failed\n", checks, fails);
	return fails != 0;
}
