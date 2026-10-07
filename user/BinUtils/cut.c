//
// cut -- keep some columns of each line.
//   usage: cut -f LIST [-d C] [-s] [file ...]      fields, separated by C (a tab by default)
//          cut -c LIST [file ...]                  characters
// LIST: numbers and ranges from 1, separated by commas: 2   1,3   2-4   3-   -2 . With -f, a
// line without the separator is printed whole (-s: dropped). No file: stdin.
//
// Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence (see tool.h).
//
#define TOOL_NAME "cut"
#include "tool.h"

#define MAXR 64
static long r_lo[MAXR], r_hi[MAXR]; static int r_n;

static int parse_list (const char *s)
{
	while (*s)
	{
		long lo = 1, hi = 0x7fffffff;
		if (r_n >= MAXR) return 0;
		if (t_isdigit (*s)) { t_number (&s, &lo); hi = lo; if (*s == '-') { s++; hi = 0x7fffffff; if (t_isdigit (*s)) t_number (&s, &hi); } }
		else if (*s == '-' && t_isdigit (s[1])) { s++; t_number (&s, &hi); }
		else return 0;
		if (lo < 1 || hi < lo) return 0;
		r_lo[r_n] = lo; r_hi[r_n++] = hi;
		if (*s == ',') s++;
		else if (*s) return 0;
	}
	return r_n > 0;
}
static int wanted (long k)
{
	for (int i = 0; i < r_n; i++) if (k >= r_lo[i] && k <= r_hi[i]) return 1;
	return 0;
}

int tool_main (int argc, char **argv)
{
	char delim = '\t'; int mode = 0, only = 0, a = 1;
	for (; a < argc && argv[a][0] == '-' && argv[a][1]; a++)
	{
		char o = argv[a][1];
		const char *v = argv[a] + 2;
		if (o == 's' && *v == '\0') { only = 1; continue; }
		if (o != 'f' && o != 'c' && o != 'd') { mode = 0; break; }
		if (*v == '\0') { if (a + 1 >= argc) { mode = 0; break; } v = argv[++a]; }
		if (o == 'd') delim = v[0];
		else { mode = o; if (!parse_list (v)) { t_err ("invalid list", v); return 2; } }
	}
	if (!mode) { t_puts ("usage: cut -f LIST [-d C] [-s] [file ...]\n       cut -c LIST [file ...]\n"); return 2; }

	int rc = 0, files = argc - a;
	for (int i = a; i < argc || (files == 0 && i == a); i++)
	{
		const char *path = files ? argv[i] : 0;
		struct t_file *f = t_open (path);
		if (f == 0) { t_err ("cannot open", path); rc = 1; continue; }
		char *s; long len;
		while ((s = t_getline (f, &len)) != 0)
		{
			if (mode == 'c')
			{
				for (long k = 0; k < len; k++) if (wanted (k + 1)) t_putc (s[k]);
				t_putc ('\n');
				continue;
			}
			int has = 0;
			for (long k = 0; k < len; k++) if (s[k] == delim) { has = 1; break; }
			if (!has) { if (!only) { t_put (s, len); t_putc ('\n'); } continue; }
			long field = 1, start = 0; int first = 1;
			for (long k = 0; k <= len; k++)
			{
				if (k < len && s[k] != delim) continue;
				if (wanted (field))
				{
					if (!first) t_putc (delim);
					t_put (s + start, k - start); first = 0;
				}
				field++; start = k + 1;
			}
			t_putc ('\n');
		}
		t_close (f);
	}
	return rc;
}
