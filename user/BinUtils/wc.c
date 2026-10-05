//
// wc -- count lines, words and bytes.
//   usage: wc [-l] [-w] [-c] [file ...]
// Without an option: "<lines> <words> <bytes>"; -l, -w, -c keep only those. No file: stdin.
// Several files: one line each (its name after the counts), then a "total" line.
//
#define TOOL_NAME "wc"
#include "tool.h"

static int o_l, o_w, o_c;

static void show (long l, long w, long c, const char *name)
{
	int first = 1;
	if (o_l) { t_putnum (l); first = 0; }
	if (o_w) { if (!first) t_putc (' '); t_putnum (w); first = 0; }
	if (o_c) { if (!first) t_putc (' '); t_putnum (c); }
	if (name) { t_putc (' '); t_puts (name); }
	t_putc ('\n');
}

int tool_main (int argc, char **argv)
{
	int a = 1;
	for (; a < argc && argv[a][0] == '-' && argv[a][1]; a++)
		for (const char *o = argv[a] + 1; *o; o++)
			switch (*o)
			{
			case 'l': o_l = 1; break;
			case 'w': o_w = 1; break;
			case 'c': o_c = 1; break;
			default:  t_puts ("usage: wc [-l] [-w] [-c] [file ...]\n"); return 2;
			}
	if (!o_l && !o_w && !o_c) o_l = o_w = o_c = 1;

	long tl = 0, tw = 0, tc = 0; int rc = 0, files = argc - a;
	for (int i = a; i < argc || (files == 0 && i == a); i++)
	{
		const char *path = files ? argv[i] : 0;
		struct t_file *f = t_open (path);
		if (f == 0) { t_err ("cannot open", path); rc = 1; continue; }
		long l = 0, w = 0, c = 0; int inword = 0, ch;
		while ((ch = t_getc (f)) >= 0)
		{
			c++;
			if (ch == '\n') l++;
			if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') inword = 0;
			else if (!inword) { inword = 1; w++; }
		}
		t_close (f);
		show (l, w, c, path);
		tl += l; tw += w; tc += c;
	}
	if (files > 1) show (tl, tw, tc, "total");
	return rc;
}
