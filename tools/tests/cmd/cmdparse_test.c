/*
 * cmdparse_test.c -- cmd's command-line parser (user/bin/cmdparse.h) on the PC: words, quotes,
 * backslash escapes, pipes, redirections, the errors. Run by tools/tests/run_cmd_test.sh.
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
#include "../../../user/bin/cmdparse.h"

static struct CmdStage st[CMD_MAXSTAGES];
static int fails, checks;

/* a stage as text: words in [ ], then <in >out or >>out */
static void show (const struct CmdStage *s, char *o, size_t cap)
{
	o[0] = 0;
	for (int i = 0; i < s->argc; i++)
	{
		strncat (o, "[", cap - strlen (o) - 1);
		strncat (o, cmd_arg (s, i), cap - strlen (o) - 1);
		strncat (o, "]", cap - strlen (o) - 1);
	}
	if (s->infile[0]) { strncat (o, " <", cap - strlen (o) - 1); strncat (o, s->infile, cap - strlen (o) - 1); }
	if (s->outfile[0]) { strncat (o, s->append ? " >>" : " >", cap - strlen (o) - 1); strncat (o, s->outfile, cap - strlen (o) - 1); }
}

/* want: the stages joined with " | ", or "ERR" */
static void t (const char *line, const char *want)
{
	char got[8192] = "", one[4096];
	const char *err;
	int n = cmd_parse (line, st, CMD_MAXSTAGES, &err);
	if (n < 0) snprintf (got, sizeof got, "ERR");
	else
		for (int i = 0; i < n; i++)
		{
			show (&st[i], one, sizeof one);
			if (i) strcat (got, " | ");
			strcat (got, one);
		}
	checks++;
	if (strcmp (got, want) != 0)
	{
		fails++;
		printf ("FAIL: %s\n   got:  %s\n   want: %s%s%s\n", line, got, want, n < 0 ? "\n   err:  " : "", n < 0 ? err : "");
	}
}

/* a line's commands: each in { }, followed by its operator (; & |) -- cmd_next */
static void l (const char *line, const char *want)
{
	char got[8192] = "", seg[CMD_LINE];
	int pos = 0, op;
	while (cmd_next (line, &pos, seg, sizeof seg, &op))
	{
		strcat (got, "{"); strcat (got, seg); strcat (got, "}");
		if (op) { char o[2] = { (char) op, 0 }; strcat (got, o); }
	}
	checks++;
	if (strcmp (got, want) != 0) { fails++; printf ("FAIL list: %s\n   got:  %s\n   want: %s\n", line, got, want); }
}

/* a command with its $ variables replaced -- cmd_expand */
static void x (const struct CmdVars *v, const char *line, const char *want)
{
	char got[CMD_LINE];
	checks++;
	if (cmd_expand (line, got, sizeof got, v) < 0 || strcmp (got, want) != 0)
	{ fails++; printf ("FAIL expand: %s\n   got:  %s\n   want: %s\n", line, got, want); }
}

int main (void)
{
	/* the old, unquoted usage */
	t ("", "");
	t ("   ", "");
	t ("ls", "[ls]");
	t ("ls  -l   SD:/apps", "[ls][-l][SD:/apps]");
	t ("cat a.txt > b.txt", "[cat][a.txt] >b.txt");
	t ("cat a.txt>b.txt", "[cat][a.txt] >b.txt");
	t ("echo hi >> log", "[echo][hi] >>log");
	t ("sort < in.txt", "[sort] <in.txt");
	t ("sort<in.txt>out.txt", "[sort] <in.txt >out.txt");
	t ("ls | grep app | sort", "[ls] | [grep][app] | [sort]");
	t ("ls|sort", "[ls] | [sort]");
	t ("cat < a | sort > b", "[cat] <a | [sort] >b");
	t ("\tls\t-l", "[ls][-l]");
	t ("cd SD:/apps", "[cd][SD:/apps]");

	/* quotes */
	t ("jsc -e \"print(1 + 2)\"", "[jsc][-e][print(1 + 2)]");
	t ("jsc -e \"let a = []; for (let i = 0; i < 300000; i++) a.push({i})\"",
	   "[jsc][-e][let a = []; for (let i = 0; i < 300000; i++) a.push({i})]");
	t ("jsc -e \"[1,2].map(x => x * 2)\"", "[jsc][-e][[1,2].map(x => x * 2)]");
	t ("jsc -e 'print(1 | 2)'", "[jsc][-e][print(1 | 2)]");
	t ("jsc -e 'print(\"hi\")'", "[jsc][-e][print(\"hi\")]");
	t ("jsc -e \"print('hi')\"", "[jsc][-e][print('hi')]");
	t ("echo \"a  b\" c", "[echo][a  b][c]");
	t ("echo ab\"c d\"'e f'g", "[echo][abc de fg]");
	t ("cd \"SD:/My Files\"", "[cd][SD:/My Files]");
	t ("cat \"my file.txt\" > \"out file.txt\"", "[cat][my file.txt] >out file.txt");
	t ("echo x >>\"a b\"", "[echo][x] >>a b");
	t ("echo \"a|b\" | sort", "[echo][a|b] | [sort]");
	t ("echo \"\" '' x", "[echo][x]");
	t ("echo \"a\"\"b\"", "[echo][ab]");

	/* backslashes */
	t ("jsc -e \"print(\\\"hi\\\")\"", "[jsc][-e][print(\"hi\")]");
	t ("jsc -e \"print('a\\nb')\"", "[jsc][-e][print('a\\nb')]");
	t ("echo \"a\\\\b\"", "[echo][a\\b]");
	t ("echo 'a\\b' 'c\\'", "[echo][a\\b][c\\]");
	t ("echo a\\ b", "[echo][a b]");
	t ("echo a\\>b \\| \\<", "[echo][a>b][|][<]");
	t ("echo \\\"x\\\" it\\'s", "[echo][\"x\"][it's]");
	t ("echo SD:\\dir\\file", "[echo][SD:\\dir\\file]");
	t ("echo a\\\\b", "[echo][a\\b]");

	/* errors */
	t ("echo \"abc", "ERR");
	t ("echo 'abc", "ERR");
	t ("| ls", "ERR");
	t ("ls |", "ERR");
	t ("ls | | sort", "ERR");
	t ("ls >", "ERR");
	t ("ls > | sort", "ERR");
	t ("cat < > x", "ERR");
	t ("< in.txt", "ERR");
	t ("\"\" | sort", "ERR");
	t ("a|b|c|d|e|f", "[a] | [b] | [c] | [d] | [e] | [f]");
	t ("a|b|c|d|e|f|g", "ERR");

	/* a long line: one stage holds it */
	{
		static char big[CMD_LINE];
		strcpy (big, "jsc -e \"");
		size_t n = strlen (big);
		while (n < sizeof big - 3) big[n++] = 'x';
		big[n++] = '"'; big[n] = 0;
		const char *err;
		int r = cmd_parse (big, st, CMD_MAXSTAGES, &err);
		checks++;
		if (r != 1 || st[0].argc != 3 || strlen (cmd_arg (&st[0], 2)) != sizeof big - 3 - 8)
		{ fails++; printf ("FAIL: a %zu-character line\n", strlen (big)); }
	}

	/* command lists: ; && || and comments */
	l ("ls", "{ls}");
	l ("a ; b", "{a };{ b}");
	l ("a;b;", "{a};{b};");
	l ("a && b || c", "{a }&{ b }|{ c}");
	l ("ls | sort || echo no", "{ls | sort }|{ echo no}");
	l ("jsc -e \"a; b && c || d\" ; x", "{jsc -e \"a; b && c || d\" };{ x}");
	l ("echo 'a;b' ; echo c\\;d", "{echo 'a;b' };{ echo c\\;d}");
	l ("echo \"a\\\";b\" ; c", "{echo \"a\\\";b\" };{ c}");
	l ("ls # a comment ; rm x", "{ls }");
	l ("# only a comment", "{}");
	l ("echo a#b c #d", "{echo a#b c }");
	l ("echo '#' \"#x\" ; ls", "{echo '#' \"#x\" };{ ls}");
	l ("wget http://h/?a=1&b=2", "{wget http://h/?a=1&b=2}");

	/* script variables */
	{
		static const char *const av[] = { "run.sh", "one", "two words", "3" };
		struct CmdVars v = { 4, av, 7 };
		x (&v, "echo $1 $2 $3 $4.", "echo one two words 3 .");
		x (&v, "echo $0 $# $?", "echo run.sh 3 7");
		x (&v, "echo $*", "echo one two words 3");
		x (&v, "echo \"$1\" '$1' \\$1", "echo \"one\" '$1' $1");
		x (&v, "grep \"end$\" f ; echo $ $x 5$", "grep \"end$\" f ; echo $ $x 5$");
		x (&v, "echo \"it's $1\" 'a\"$1'", "echo \"it's one\" 'a\"$1'");
		x (&v, "echo a\\ b \"c\\\"$1\"", "echo a\\ b \"c\\\"one\"");
		struct CmdVars none = { 0, 0, -9 };
		x (&none, "echo [$1] $# $? [$*]", "echo [] 0 -9 []");
		char small[8];
		checks++;
		if (cmd_expand ("echo $1$1$1", small, sizeof small, &v) != -1) { fails++; printf ("FAIL: an expansion too long accepted\n"); }
	}

	printf ("%d checks, %d failed\n", checks, fails);
	return fails != 0;
}
