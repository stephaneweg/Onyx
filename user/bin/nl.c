//
// nl -- number the lines.
//   usage: nl [-b a] [file ...]
// Each line preceded by its number and a tab; an empty line is not numbered (-b a: all are).
// No file: stdin; several files: numbered as one text.
//
// Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence (see tool.h).
//
#define TOOL_NAME "nl"
#include "tool.h"

int tool_main (int argc, char **argv)
{
	int all = 0, a = 1;
	if (a + 1 < argc && t_streq (argv[a], "-b")) { all = argv[a + 1][0] == 'a'; a += 2; }
	else if (a < argc && t_streq (argv[a], "-ba")) { all = 1; a++; }
	long n = 0; int rc = 0, files = argc - a;
	for (int i = a; i < argc || (files == 0 && i == a); i++)
	{
		const char *path = files ? argv[i] : 0;
		struct t_file *f = t_open (path);
		if (f == 0) { t_err ("cannot open", path); rc = 1; continue; }
		char *s; long len;
		while ((s = t_getline (f, &len)) != 0)
		{
			if (len > 0 || all) { t_putnumw (++n, 6); t_putc ('\t'); }
			t_put (s, len); t_putc ('\n');
		}
		t_close (f);
	}
	return rc;
}
