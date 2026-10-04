//
// grep -- print the lines that match a pattern.
//   usage: grep [-i] [-v] [-n] [-c] [-q] [-F] <pattern> [file ...]
// The pattern is a regular expression (regex.h: . * [set] ^ $ \( \) \+ \?); -F: a plain text.
// -i ignores case, -v keeps the lines that do NOT match, -n numbers them, -c only counts them,
// -q prints nothing (the exit code says it). No file: stdin. Several files: each line is
// preceded by its file's name. Exit code 0 a line matched, 1 none, 2 an error.
//
#define TOOL_NAME "grep"
#include "tool.h"
#include "regex.h"

static int o_icase, o_inv, o_num, o_count, o_quiet, o_fixed;

static int fixed (const char *pat, const char *s, long len)
{
	int pl = t_strlen (pat);
	for (long i = 0; i + pl <= len; i++)
	{
		int j = 0;
		while (j < pl && (o_icase ? t_lower ((unsigned char) s[i + j]) == t_lower ((unsigned char) pat[j])
					  : s[i + j] == pat[j])) j++;
		if (j == pl) return 1;
	}
	return 0;
}

static long grep_file (const char *pat, const char *path, int show_name)
{
	struct t_file *f = t_open (path);
	if (f == 0) { t_err ("cannot open", path); return -1; }
	long len, lineno = 0, hits = 0;
	char *s;
	struct re_match m;
	while ((s = t_getline (f, &len)) != 0)
	{
		lineno++;
		int hit = o_fixed ? fixed (pat, s, len) : re_search (pat, s, s + len, s, o_icase, &m);
		if (hit == o_inv) continue;
		hits++;
		if (o_quiet) break;
		if (o_count) continue;
		if (show_name) { t_puts (path); t_putc (':'); }
		if (o_num) { t_putnum (lineno); t_putc (':'); }
		t_put (s, len); t_putc ('\n');
	}
	t_close (f);
	if (o_count && !o_quiet)
	{
		if (show_name) { t_puts (path); t_putc (':'); }
		t_putnum (hits); t_putc ('\n');
	}
	return hits;
}

int tool_main (int argc, char **argv)
{
	int a = 1;
	for (; a < argc && argv[a][0] == '-' && argv[a][1]; a++)
	{
		if (t_streq (argv[a], "--")) { a++; break; }
		for (const char *o = argv[a] + 1; *o; o++)
			switch (*o)
			{
			case 'i': o_icase = 1; break;
			case 'v': o_inv = 1; break;
			case 'n': o_num = 1; break;
			case 'c': o_count = 1; break;
			case 'q': o_quiet = 1; break;
			case 'F': o_fixed = 1; break;
			default:  t_err ("unknown option", argv[a]); return 2;
			}
	}
	if (a >= argc) { t_puts ("usage: grep [-i] [-v] [-n] [-c] [-q] [-F] <pattern> [file ...]\n"); return 2; }
	const char *pat = argv[a++];
	long total = 0; int bad = 0;
	if (a >= argc) total = grep_file (pat, 0, 0);
	else
		for (int i = a; i < argc; i++)
		{
			long h = grep_file (pat, argv[i], argc - a > 1);
			if (h < 0) bad = 1; else total += h;
		}
	return bad ? 2 : total > 0 ? 0 : 1;
}
