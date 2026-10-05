//
// diff -- compare two text files line by line.
//   usage: diff [-q] <file1> <file2>
// Prints what to change in file1 to get file2, the classic way: "3c3" (line 3 changed),
// "5a6,7" (lines added after 5), "8,9d7" (lines deleted), with the lines of file1 after "<"
// and those of file2 after ">". Nothing printed: the files are the same. -q: only says whether
// they differ. Exit code 0 the same, 1 different, 2 an error.
//
// Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence (see tool.h).
//
#define TOOL_NAME "diff"
#include "tool.h"

static void range (long a, long b)
{
	t_putnum (a);
	if (b > a) { t_putc (','); t_putnum (b); }
}

int tool_main (int argc, char **argv)
{
	int quiet = 0, a = 1;
	if (a < argc && t_streq (argv[a], "-q")) { quiet = 1; a++; }
	if (argc - a != 2) { t_puts ("usage: diff [-q] <file1> <file2>\n"); return 2; }
	long la, lb, n, m;
	char *ba = t_slurp (argv[a], &la);
	if (ba == 0) { t_err ("cannot open", argv[a]); return 2; }
	char *bb = t_slurp (argv[a + 1], &lb);
	if (bb == 0) { t_err ("cannot open", argv[a + 1]); return 2; }
	int same = la == lb;
	for (long i = 0; same && i < la; i++) if (ba[i] != bb[i]) same = 0;
	if (same) return 0;
	if (quiet) { t_puts ("Files "); t_puts (argv[a]); t_puts (" and "); t_puts (argv[a + 1]); t_puts (" differ\n"); return 1; }

	int nla, nlb;
	char **A = t_lines (ba, la, &n, &nla), **B = t_lines (bb, lb, &m, &nlb);
	// the common start and end set aside, the longest common subsequence of the middle
	long pre = 0, suf = 0;
	while (pre < n && pre < m && t_streq (A[pre], B[pre])) pre++;
	while (suf < n - pre && suf < m - pre && t_streq (A[n - 1 - suf], B[m - 1 - suf])) suf++;
	long N = n - pre - suf, M = m - pre - suf;
	if ((N + 1) * (M + 1) > 24L * 1024 * 1024) { t_err ("the files differ too much to be compared (too many lines)", 0); return 2; }
	unsigned short *L = (unsigned short *) t_malloc ((N + 1) * (M + 1) * (long) sizeof *L);
#define AT(i, j) L[(i) * (M + 1) + (j)]
	for (long i = N; i >= 0; i--)
		for (long j = M; j >= 0; j--)
		{
			if (i == N || j == M) AT (i, j) = 0;
			else if (t_streq (A[pre + i], B[pre + j])) AT (i, j) = (unsigned short) (AT (i + 1, j + 1) + 1);
			else AT (i, j) = AT (i + 1, j) >= AT (i, j + 1) ? AT (i + 1, j) : AT (i, j + 1);
		}
	long i = 0, j = 0;
	while (i < N || j < M)
	{
		if (i < N && j < M && t_streq (A[pre + i], B[pre + j])) { i++; j++; continue; }
		long i0 = i, j0 = j;				// a hunk: up to the next common line
		while (i < N || j < M)
		{
			if (i < N && j < M && t_streq (A[pre + i], B[pre + j])) break;
			if (j >= M || (i < N && AT (i + 1, j) >= AT (i, j + 1))) i++;
			else j++;
		}
		long da = i - i0, db = j - j0;
		if (da && db) { range (pre + i0 + 1, pre + i); t_putc ('c'); range (pre + j0 + 1, pre + j); }
		else if (da)  { range (pre + i0 + 1, pre + i); t_putc ('d'); t_putnum (pre + j0); }
		else          { t_putnum (pre + i0); t_putc ('a'); range (pre + j0 + 1, pre + j); }
		t_putc ('\n');
		for (long k = i0; k < i; k++) { t_puts ("< "); t_puts (A[pre + k]); t_putc ('\n'); }
		if (da && db) t_puts ("---\n");
		for (long k = j0; k < j; k++) { t_puts ("> "); t_puts (B[pre + k]); t_putc ('\n'); }
	}
	if (nla != nlb) t_puts (nla ? "\\ No newline at end of file2\n" : "\\ No newline at end of file1\n");
	return 1;
}
