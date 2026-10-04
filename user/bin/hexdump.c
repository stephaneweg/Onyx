//
// hexdump -- show the bytes of a file: the offset, 16 bytes in hexadecimal, the same as text.
//   usage: hexdump [-s OFFSET] [-n COUNT] [file]
// -s: skip OFFSET bytes first; -n: stop after COUNT bytes. No file: stdin.
//
// Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence (see tool.h).
//
#define TOOL_NAME "hexdump"
#include "tool.h"

static void hex (unsigned long v, int digits)
{
	for (int i = digits - 1; i >= 0; i--) t_putc ("0123456789abcdef"[(v >> (4 * i)) & 15]);
}

int tool_main (int argc, char **argv)
{
	long skip = 0, count = -1; int a = 1;
	for (; a + 1 < argc && argv[a][0] == '-' && argv[a][1]; a += 2)
	{
		long *dst = argv[a][1] == 's' ? &skip : argv[a][1] == 'n' ? &count : 0;
		if (dst == 0 || argv[a][2] || !t_atol (argv[a + 1], dst) || *dst < 0)
		{ t_puts ("usage: hexdump [-s OFFSET] [-n COUNT] [file]\n"); return 2; }
	}
	const char *path = a < argc ? argv[a] : 0;
	struct t_file *f = t_open (path);
	if (f == 0) { t_err ("cannot open", path); return 1; }
	unsigned long off = 0;
	for (; (long) off < skip; off++) if (t_getc (f) < 0) break;
	unsigned char row[16];
	for (;;)
	{
		int n = 0, c;
		while (n < 16 && count != 0 && (c = t_getc (f)) >= 0) { row[n++] = (unsigned char) c; if (count > 0) count--; }
		if (n == 0) break;
		hex (off, 8); t_puts ("  ");
		for (int i = 0; i < 16; i++)
		{
			if (i < n) hex (row[i], 2); else t_puts ("  ");
			t_putc (' ');
			if (i == 7) t_putc (' ');
		}
		t_puts (" |");
		for (int i = 0; i < n; i++) t_putc (row[i] >= 32 && row[i] < 127 ? (char) row[i] : '.');
		t_puts ("|\n");
		off += (unsigned long) n;
		if (n < 16) break;
	}
	hex (off, 8); t_putc ('\n');
	t_close (f);
	return 0;
}
