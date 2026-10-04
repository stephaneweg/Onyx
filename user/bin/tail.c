//
// tail -- the last lines of a file.
//   usage: tail [-n N | -N | -n +N] [-c N] [-f] [file]
// The last 10 lines, or N with -n N (-5 is -n 5); -n +N: from line N on; -c N: the last N
// bytes. -f: then keeps printing what is added to the file (a log being written), until
// Ctrl-C. No file: stdin.
//
// Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence (see tool.h).
//
#define TOOL_NAME "tail"
#include "tool.h"

int tool_main (int argc, char **argv)
{
	long n = 10, bytes = -1; int from = 0, follow = 0, a = 1;
	for (; a < argc && argv[a][0] == '-' && argv[a][1]; a++)
	{
		const char *o = argv[a] + 1;
		if (t_streq (o, "f")) { follow = 1; continue; }
		long *dst = &n;
		if (*o == 'n' || *o == 'c')
		{
			if (*o == 'c') dst = &bytes;
			o++;
			if (*o == '\0' && a + 1 < argc) o = argv[++a];
		}
		if (dst == &n && *o == '+') from = 1;
		if (!t_atol (o, dst) || *dst < 0) { t_puts ("usage: tail [-n N | -n +N] [-c N] [-f] [file]\n"); return 2; }
	}
	const char *path = a < argc ? argv[a] : 0;
	long len;
	char *b = t_slurp (path, &len);
	if (b == 0) { t_err ("cannot open", path); return 1; }

	long start = 0;
	if (bytes >= 0) start = len > bytes ? len - bytes : 0;
	else if (from)						// from line n on
	{
		for (long line = 1; line < n && start < len; start++) if (b[start] == '\n') line++;
	}
	else if (n == 0) start = len;
	else
	{
		long seen = 0;
		start = len;
		if (start > 0 && b[start - 1] == '\n') start--;	// (the last line's own '\n')
		while (start > 0)
		{
			if (b[start - 1] == '\n' && ++seen >= n) break;
			start--;
		}
	}
	t_put (b + start, len - start);
	t_free (b);
	if (!follow || path == 0 || t_streq (path, "-")) return 0;

	long long at = len, size;
	for (;;)							// (ended by Ctrl-C)
	{
		t_sleep_ms (500);
		if (t_stat (path, &size) != 1 || size == at) continue;
		if (size < at) at = 0;					// (the file started again)
		struct t_file *f = t_open (path);
		if (f == 0) continue;
		char tmp[1024]; long k; long long pos = 0;
		while ((k = t_read (f, tmp, sizeof tmp)) > 0)
		{
			if (pos + k > at) { long skip = at > pos ? (long) (at - pos) : 0; t_put (tmp + skip, k - skip); }
			pos += k;
		}
		t_close (f);
		at = pos;
		t_flush ();
	}
}
