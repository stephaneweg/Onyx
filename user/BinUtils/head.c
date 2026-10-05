//
// head -- the first lines of a file.
//   usage: head [-n N | -N] [-c N] [file ...]
// The first 10 lines, or N with -n N (-5 is -n 5); -c N: the first N bytes. No file: stdin.
// Several files: a "==> name <==" line before each.
//
// Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence (see tool.h).
//
#define TOOL_NAME "head"
#include "tool.h"

int tool_main (int argc, char **argv)
{
	long n = 10, bytes = -1; int a = 1;
	for (; a < argc && argv[a][0] == '-' && argv[a][1]; a++)
	{
		const char *o = argv[a] + 1;
		long *dst = &n;
		if (*o == 'n' || *o == 'c')
		{
			if (*o == 'c') dst = &bytes;
			o++;
			if (*o == '\0' && a + 1 < argc) o = argv[++a];
		}
		if (!t_atol (o, dst) || *dst < 0) { t_puts ("usage: head [-n N] [-c N] [file ...]\n"); return 2; }
	}
	int rc = 0, files = argc - a;
	for (int i = a; i < argc || (files == 0 && i == a); i++)
	{
		const char *path = files ? argv[i] : 0;
		struct t_file *f = t_open (path);
		if (f == 0) { t_err ("cannot open", path); rc = 1; continue; }
		if (files > 1) { if (i > a) t_putc ('\n'); t_puts ("==> "); t_puts (path); t_puts (" <==\n"); }
		long left = bytes >= 0 ? bytes : n; int c;
		while (left > 0 && (c = t_getc (f)) >= 0)
		{
			t_putc ((char) c);
			if (bytes >= 0 || c == '\n') left--;
		}
		t_close (f);
	}
	return rc;
}
