//
// uniq -- drop the repeated lines that follow each other (sort first to drop them all).
//   usage: uniq [-c] [-d] [-u] [-i] [file]
// -c: each line preceded by how many times it came; -d: only the repeated lines; -u: only the
// lines that are not repeated; -i: ignoring case. No file: stdin.
//
// Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence (see tool.h).
//
#define TOOL_NAME "uniq"
#include "tool.h"

static int o_count, o_dup, o_single, o_icase;

static void emit (const char *s, long n)
{
	if (s == 0 || (o_dup && n < 2) || (o_single && n > 1)) return;
	if (o_count) { t_putnumw (n, 7); t_putc (' '); }
	t_puts (s); t_putc ('\n');
}

int tool_main (int argc, char **argv)
{
	int a = 1;
	for (; a < argc && argv[a][0] == '-' && argv[a][1]; a++)
		for (const char *o = argv[a] + 1; *o; o++)
			switch (*o)
			{
			case 'c': o_count = 1; break;
			case 'd': o_dup = 1; break;
			case 'u': o_single = 1; break;
			case 'i': o_icase = 1; break;
			default:  t_puts ("usage: uniq [-c] [-d] [-u] [-i] [file]\n"); return 2;
			}
	struct t_file *f = t_open (a < argc ? argv[a] : 0);
	if (f == 0) { t_err ("cannot open", argv[a]); return 1; }
	char *prev = 0, *s; long n = 0, len;
	while ((s = t_getline (f, &len)) != 0)
	{
		if (prev && (o_icase ? t_strcasecmp (s, prev) : t_strcmp (s, prev)) == 0) { n++; continue; }
		emit (prev, n);
		t_free (prev);
		prev = t_strndup (s, len); n = 1;
	}
	emit (prev, n);
	t_close (f);
	return 0;
}
