/*
 * cmdscript_test.c -- cmd's script language (user/bin/cmdscript.h) on the PC: what replaces a $
 * (arguments, variables, $(command), $((arithmetic))), `test`, the command lists with `!` and
 * assignments, the blocks (if / elif / else, while, until, for, break, continue, nested). The
 * shell's side is a stand-in here: `echo` writes into a log, the log is what a script "printed".
 * Run by tools/tests/run_cmd_test.sh.
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
#include <stdlib.h>
#include <string.h>
#define CS_MALLOC(n)	malloc (n)
#define CS_FREE(p)	free (p)
#include "../../../user/bin/cmdscript.h"

static int fails, checks;

/* ---- the stand-in shell ---- */
static char g_log[65536];			/* what the commands printed */
static char g_names[64][CS_NAME], g_values[64][512];
static int  g_nvars, g_stop, g_runs;
static char g_err[256];
static struct CmdVars g_vars;
static struct CsHost g_host;

static const char *var_get (const char *name)
{
	for (int i = 0; i < g_nvars; i++) if (!strcmp (g_names[i], name)) return g_values[i];
	return 0;
}
static void var_set (const char *name, const char *value)
{
	int i = 0;
	while (i < g_nvars && strcmp (g_names[i], name)) i++;
	if (i == g_nvars) { if (g_nvars == 64) return; g_nvars++; }
	snprintf (g_names[i], CS_NAME, "%s", name);
	snprintf (g_values[i], sizeof g_values[i], "%s", value);
}
static int file_kind (const char *p) { return !strcmp (p, "file.txt") ? 1 : !strcmp (p, "folder") ? 2 : 0; }
static int stop (void) { return g_stop; }
static void error (const char *m) { snprintf (g_err, sizeof g_err, "%s", m); strcat (g_log, "<error: "); strcat (g_log, m); strcat (g_log, ">\n"); }

static int pipeline (const char *cmd)
{
	static struct CmdStage st[2];
	const char *err;
	if (++g_runs > 100000) { g_stop = 1; return 99; }		/* (a loop that never ends) */
	int n = cmd_parse (cmd, st, 2, &err);
	if (n < 0) { error (err); return 2; }
	if (n == 0) return g_vars.status;
	const char *av[64]; int ac = 0;
	for (int i = 0; i < st[0].argc && ac < 64; i++) av[ac++] = cmd_arg (&st[0], i);
	if (!strcmp (av[0], "echo"))
	{
		for (int i = 1; i < ac; i++) { if (i > 1) strcat (g_log, " "); strcat (g_log, av[i]); }
		strcat (g_log, "\n");
		return 0;
	}
	if (!strcmp (av[0], "true")) return 0;
	if (!strcmp (av[0], "false")) return 1;
	if (!strcmp (av[0], "code")) return ac > 1 ? atoi (av[1]) : 0;
	if (!strcmp (av[0], "test")) return cs_test (ac - 1, av + 1, file_kind);
	if (!strcmp (av[0], "["))
	{
		if (strcmp (av[ac - 1], "]")) { error ("[: missing ]"); return 2; }
		return cs_test (ac - 2, av + 1, file_kind);
	}
	strcat (g_log, av[0]); strcat (g_log, ": command not found\n");
	return 127;
}

/* $(command): the log it makes */
static int capture (const char *cmd, char *out, int cap)
{
	char saved[sizeof g_log];
	strcpy (saved, g_log);
	g_log[0] = 0;
	cs_run_line (&g_host, cmd);
	int n = (int) strlen (g_log);
	if (n >= cap) { strcpy (g_log, saved); return -1; }
	strcpy (out, g_log);
	strcpy (g_log, saved);
	return n;
}

static int fake_glob (const char *pat, char *out, int cap, int *count)
{
	(void) cap;
	*count = 0;
	if (strcmp (pat, "*.txt")) return 0;
	memcpy (out, "a.txt\0b c.txt", 14);
	*count = 2;
	return 14;
}

static void reset (void)
{
	static const char *const av[] = { "run.sh", "one", "two words", "3" };
	g_log[0] = g_err[0] = 0; g_nvars = g_stop = g_runs = 0;
	g_vars.argc = 4; g_vars.argv = av; g_vars.status = 0; g_vars.get = var_get; g_vars.run = capture;
	g_host.vars = &g_vars; g_host.pipeline = pipeline; g_host.set = var_set; g_host.stop = stop; g_host.error = error;
	cmd_glob_hook = fake_glob;
}

/* a command with its $ replaced */
static void x (const char *line, const char *want)
{
	char got[CMD_LINE];
	checks++;
	int r = cmd_expand (line, got, sizeof got, &g_vars);
	if (r < 0) snprintf (got, sizeof got, "ERR%d", r);
	if (strcmp (got, want) != 0) { fails++; printf ("FAIL expand: %s\n   got:  %s\n   want: %s\n", line, got, want); }
}

/* a script -> what it printed */
static void s (const char *name, const char *script, const char *want)
{
	reset ();
	char *text = strdup (script);
	int n;
	char **lines = cs_lines (text, &n);
	cs_exec (&g_host, lines, 0, n);
	checks++;
	if (strcmp (g_log, want) != 0) { fails++; printf ("FAIL script %s\n--- got\n%s--- want\n%s", name, g_log, want); }
	free (lines); free (text);
}

static void tst (int want, int argc, ...)
{
	const char *av[16];
	__builtin_va_list ap;
	__builtin_va_start (ap, argc);
	for (int i = 0; i < argc; i++) av[i] = __builtin_va_arg (ap, const char *);
	__builtin_va_end (ap);
	checks++;
	int r = cs_test (argc, av, file_kind);
	if (r != want) { fails++; printf ("FAIL test:"); for (int i = 0; i < argc; i++) printf (" %s", av[i]); printf (" -> %d, want %d\n", r, want); }
}

static void ar (const char *e, long long want, int wantrc)
{
	long long v = 0;
	checks++;
	int rc = cs_arith (e, &g_vars, &v);
	if (rc != wantrc || (rc == 0 && v != want)) { fails++; printf ("FAIL arith: %s -> %lld (rc %d), want %lld (rc %d)\n", e, v, rc, want, wantrc); }
}

int main (void)
{
	reset ();
	g_vars.status = 7;
	var_set ("name", "Onyx"); var_set ("n", "41"); var_set ("path", "SD:\\my dir\\a;b"); var_set ("q", "say \"hi\" | x");

	/* the arguments */
	x ("echo $1 $2 $3 $4.", "echo one two words 3 .");
	x ("echo $0 $# $?", "echo run.sh 3 7");
	x ("echo $*", "echo one two words 3");
	x ("echo \"$1\" '$1' \\$1", "echo \"one\" '$1' $1");
	x ("grep \"end$\" f ; echo $ 5$", "grep \"end$\" f ; echo $ 5$");
	x ("echo \"it's $1\" 'a\"$1'", "echo \"it's one\" 'a\"$1'");
	x ("echo a\\ b \"c\\\"$1\"", "echo a\\ b \"c\\\"one\"");

	/* variables */
	x ("echo $name ${name}s $nameless [$unset]", "echo Onyx Onyxs  []");
	x ("echo \"$name\" '$name' \\$name", "echo \"Onyx\" '$name' $name");
	x ("cd $path", "cd SD:\\\\my dir\\\\a\\;b");
	x ("cd \"$path\"", "cd \"SD:\\\\my dir\\\\a;b\"");
	x ("echo $q \"$q\"", "echo say \\\"hi\\\" \\| x \"say \\\"hi\\\" | x\"");
	x ("echo ${name", "ERR-2");
	x ("echo ${}", "ERR-2");

	/* arithmetic */
	x ("echo $((1 + 2 * 3)) $(( (1 + 2) * 3 )) $((n + 1)) $(($n+1))", "echo 7 9 42 42");
	x ("echo $((10 / 3)) $((10 % 3)) $((-7 / 2)) $((2 - 5))", "echo 3 1 -3 -3");
	x ("echo $((3 > 2)) $((3 <= 2)) $((1 == 1 && 2 != 2)) $((0 || 5)) $((!0))", "echo 1 0 0 1 1");
	x ("echo $((0x10 + 1)) $((6 & 3)) $((6 | 3)) $((6 ^ 3)) $((1 << 4)) $((~0))", "echo 17 2 7 5 16 -1");
	x ("echo $((unset_var + 2)) $(($# * 2))", "echo 2 6");
	x ("echo $((1 / 0))", "ERR-3");
	x ("echo $((1 +))", "ERR-2");
	x ("echo $((1 2))", "ERR-2");
	x ("echo $((1 + 2)", "ERR-2");
	ar ("2 + 3 * 4 - 1", 13, 0); ar ("(2 + 3) * (4 - 1)", 15, 0); ar ("1 + n", 42, 0); ar ("7 % 0", 0, -2);
	ar ("-(-3)", 3, 0); ar ("1 < 2 == 1", 1, 0); ar ("2 * (3", 0, -1); ar ("", 0, -1);
	ar ("-9223372036854775807 - 1", (-9223372036854775807LL - 1), 0); ar ("(-9223372036854775807 - 1) / -1", (-9223372036854775807LL - 1), 0);

	/* $(command) */
	x ("echo $(echo hello) \"$(echo a  b)\" $(echo a  b)", "echo hello \"a b\" a b");
	x ("echo $(echo $name $(echo deep)) [$(true)]", "echo Onyx deep []");
	x ("echo $(echo \"a)b\") $(echo 'x;y')", "echo a)b x\\;y");
	x ("echo $(echo one", "ERR-2");
	{
		char small[8];
		checks++;
		if (cmd_expand ("echo $1$1$1", small, sizeof small, &g_vars) != CS_ETOOLONG) { fails++; printf ("FAIL: an expansion too long accepted\n"); }
	}

	/* test */
	tst (0, 3, "a", "=", "a"); tst (1, 3, "a", "=", "b"); tst (0, 3, "a", "!=", "b"); tst (0, 3, "", "=", "");
	tst (0, 3, "5", "-eq", "5"); tst (1, 3, "5", "-ne", "5"); tst (0, 3, "-2", "-lt", "10"); tst (1, 3, "10", "-lt", "9");
	tst (0, 3, "3", "-le", "3"); tst (0, 3, "4", "-gt", "3"); tst (0, 3, "3", "-ge", "3"); tst (1, 3, "2", "-ge", "3");
	tst (0, 2, "-z", ""); tst (1, 2, "-z", "x"); tst (0, 2, "-n", "x"); tst (1, 2, "-n", ""); tst (0, 1, "x"); tst (1, 1, ""); tst (1, 0);
	tst (0, 2, "-e", "file.txt"); tst (0, 2, "-f", "file.txt"); tst (1, 2, "-d", "file.txt"); tst (0, 2, "-d", "folder");
	tst (0, 2, "-e", "folder"); tst (1, 2, "-e", "nothing"); tst (1, 2, "-f", "folder");
	tst (0, 3, "!", "-e", "nothing"); tst (1, 4, "!", "a", "=", "a");
	tst (0, 7, "a", "=", "a", "-a", "1", "-lt", "2"); tst (1, 7, "a", "=", "a", "-a", "2", "-lt", "1");
	tst (0, 7, "a", "=", "b", "-o", "1", "-lt", "2"); tst (1, 5, "-f", "x", "-o", "-d", "y");
	tst (2, 3, "a", "-zz", "b"); tst (2, 2, "-q", "x"); tst (2, 4, "a", "=", "b", "c");

	/* lines: lists, !, assignments */
	s ("list", "echo a ; echo b && echo c || echo d\nfalse && echo no\nfalse || echo yes", "a\nb\nc\nyes\n");
	s ("status", "code 3\necho $?\ncode 3 || echo failed $?\ntrue\necho $?", "3\nfailed 3\n0\n");
	s ("not", "! false && echo inverted\n! true || echo inverted too\n! code 5 ; echo $?", "inverted\ninverted too\n0\n");
	s ("assign", "a=1\nb=hello world\nc=\"x  y\"\nd=$a$a\ne=\necho $a [$b] [$c] $d [$e]", "1 [hello world] [x y] 11 []\n");
	s ("assign quoted", "c=\"x  y\"\necho \"$c\"\nmsg='a | b ; c'\necho $msg", "x  y\na | b ; c\n");
	s ("assign arith", "i=5\ni=$((i * 2 + 1))\necho $i ; j=$(echo sub) ; echo $j", "11\nsub\n");
	s ("assign no glob", "p=*.txt\necho \"$p\"\necho $p", "*.txt\na.txt b c.txt\n");
	s ("comment", "# nothing\necho a # trailing\n   # indented", "a\n");
	s ("not found", "nosuch arg\necho $?", "nosuch: command not found\n127\n");

	/* if */
	s ("if true", "if true\necho yes\nfi\necho after", "yes\nafter\n");
	s ("if false", "if false\necho yes\nfi\necho after", "after\n");
	s ("if else", "if [ $1 = two ]\necho A\nelse\necho B\nfi", "B\n");
	s ("if then", "if [ \"$2\" = \"two words\" ] ; then\n  echo A\nelse\n  echo B\nfi", "A\n");
	s ("if then line", "if test $# -eq 3\nthen\necho three\nfi", "three\n");
	s ("elif", "x=2\nif [ $x = 1 ]; then\necho one\nelif [ $x = 2 ]; then\necho two\nelif [ $x = 2 ]\necho again\nelse\necho other\nfi", "two\n");
	s ("elif else", "x=9\nif [ $x = 1 ]\necho one\nelif [ $x = 2 ]\necho two\nelse\necho other\nfi", "other\n");
	s ("if nested", "if true\nif false\necho a\nelse\necho b\nif true\necho c\nfi\nfi\nelse\necho d\nfi\necho e", "b\nc\ne\n");
	s ("if list cond", "if false || true && [ -f file.txt ]\necho ok\nfi", "ok\n");
	s ("if not", "if ! [ -e nothing ]\necho missing\nfi", "missing\n");
	s ("if no fi", "if true\necho a", "<error: if without its fi>\n");
	s ("stray fi", "echo a\nfi\necho b", "a\n<error: fi without its if>\n");
	s ("stray else", "else\necho b", "<error: else without its if>\n");

	/* while / until */
	s ("while", "i=0\nwhile [ $i -lt 3 ]\necho $i\ni=$((i + 1))\ndone\necho end $i", "0\n1\n2\nend 3\n");
	s ("while do", "i=3\nwhile [ $i -gt 0 ] ; do\n  i=$((i - 1))\ndone\necho $i", "0\n");
	s ("until", "i=0\nuntil [ $i -ge 2 ]\ndo\ni=$((i + 1))\necho $i\ndone", "1\n2\n");
	s ("break continue", "i=0\nwhile true\ni=$((i + 1))\nif [ $i = 2 ]\ncontinue\nfi\nif [ $i -gt 4 ]\nbreak\nfi\necho $i\ndone\necho out", "1\n3\n4\nout\n");
	s ("while never", "while false\necho no\ndone\necho ok", "ok\n");
	s ("while no done", "while true\necho a", "<error: while without its done>\n");
	s ("stray done", "done", "<error: done without its loop>\n");

	/* for */
	s ("for", "for f in a b \"c d\"\necho [$f]\ndone", "[a]\n[b]\n[c d]\n");
	s ("for do", "for f in 1 2 ; do\necho $f\ndone\necho last $f", "1\n2\nlast 2\n");
	s ("for args", "for a in $*\necho $a\ndone", "one\ntwo\nwords\n3\n");
	s ("for quoted arg", "for a in \"$2\" $3\necho [$a]\ndone", "[two words]\n[3]\n");
	s ("for glob", "for f in *.txt *.zip\necho [$f]\ndone", "[a.txt]\n[b c.txt]\n[*.zip]\n");
	s ("for subst", "for w in $(echo x y) z\necho $w\ndone", "x\ny\nz\n");
	s ("for empty", "for w in $unset\necho $w\ndone\necho ok", "ok\n");
	s ("for nested", "for a in 1 2\nfor b in x y\nif [ $a$b = 1y ]\ncontinue\nfi\necho $a$b\ndone\ndone", "1x\n2x\n2y\n");
	s ("for break", "for a in 1 2 3\nfor b in x y\nbreak\ndone\nif [ $a = 2 ]\nbreak\nfi\necho $a\ndone", "1\n");
	s ("for bad", "for x\necho a\ndone", "<error: for: `for name in words` expected>\n");
	s ("sum", "sum=0\nfor n in 1 2 3 4 5 6 7 8 9 10\nsum=$((sum + n))\ndone\necho $sum", "55\n");
	s ("table", "i=1\nwhile [ $i -le 3 ]\necho \"$i x 7 = $((i * 7))\"\ni=$((i + 1))\ndone", "1 x 7 = 7\n2 x 7 = 14\n3 x 7 = 21\n");
	s ("crlf", "if true\r\necho dos\r\nfi\r\n", "dos\n");

	/* a loop that never ends is stopped from outside (the shell: Ctrl-C) */
	s ("stop", "while true\ntrue\ndone\necho not reached", "");

	printf ("cmdscript: %d checks, %d failed\n", checks, fails);
	return fails != 0;
}
