//
// sort -- sort the lines of its input.
//   usage: sort [-r] [-n] [-f] [-u] [file ...]
// -r: in reverse; -n: by the number at the start of each line; -f: ignoring case; -u: one of
// each (equal lines kept once). No file: stdin; several files: sorted together.
//
// Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence (see tool.h).
//
#define TOOL_NAME "sort"
#include "tool.h"

static int o_rev, o_num, o_fold, o_uniq;

static int cmp (const char *a, const char *b)
{
	int r = 0;
	if (o_num)
	{
		long x = 0, y = 0;
		const char *p = a, *q = b;
		while (t_isblank (*p)) p++;
		while (t_isblank (*q)) q++;
		t_number (&p, &x); t_number (&q, &y);
		r = x < y ? -1 : x > y;
	}
	if (r == 0) r = o_fold ? t_strcasecmp (a, b) : t_strcmp (a, b);
	return o_rev ? -r : r;
}

// a merge sort (stable; no worst case on a sorted input)
static void msort (char **v, char **tmp, long n)
{
	if (n < 2) return;
	long h = n / 2, i = 0, j = h, k = 0;
	msort (v, tmp, h); msort (v + h, tmp, n - h);
	while (i < h && j < n) tmp[k++] = cmp (v[j], v[i]) < 0 ? v[j++] : v[i++];
	while (i < h) tmp[k++] = v[i++];
	while (j < n) tmp[k++] = v[j++];
	for (k = 0; k < n; k++) v[k] = tmp[k];
}

int tool_main (int argc, char **argv)
{
	int a = 1;
	for (; a < argc && argv[a][0] == '-' && argv[a][1]; a++)
		for (const char *o = argv[a] + 1; *o; o++)
			switch (*o)
			{
			case 'r': o_rev = 1; break;
			case 'n': o_num = 1; break;
			case 'f': o_fold = 1; break;
			case 'u': o_uniq = 1; break;
			default:  t_puts ("usage: sort [-r] [-n] [-f] [-u] [file ...]\n"); return 2;
			}
	char **all = 0; long total = 0; int rc = 0, files = argc - a;
	for (int i = a; i < argc || (files == 0 && i == a); i++)
	{
		const char *path = files ? argv[i] : 0;
		long len, cnt;
		char *b = t_slurp (path, &len);
		if (b == 0) { t_err ("cannot open", path); rc = 2; continue; }
		char **v = t_lines (b, len, &cnt, 0);
		all = (char **) t_realloc (all, (total + cnt + 1) * (long) sizeof *all);
		for (long k = 0; k < cnt; k++) all[total++] = v[k];
		t_free (v);
	}
	char **tmp = (char **) t_malloc ((total + 1) * (long) sizeof *tmp);
	msort (all, tmp, total);
	for (long k = 0; k < total; k++)
	{
		if (o_uniq && k > 0 && cmp (all[k], all[k - 1]) == 0) continue;
		t_puts (all[k]); t_putc ('\n');
	}
	return rc;
}
