//
// ls -- list folders and files.
//   usage: ls [-l] [path ...]          (default: the current folder)
// A folder: its entries, one per line, the folders with a trailing '/'. A file: its name (so
// that a pattern works: ls *.txt). -l: each entry's size in bytes before its name. Several
// paths: the files first, then each folder under a "name:" line.
//
#define TOOL_NAME "ls"
#include "tool.h"

static int o_long;

static void entry (const char *name, int is_dir, long long size)
{
	if (o_long) { if (is_dir) t_puts ("         -"); else t_putnumw (size, 10); t_putc (' '); }
	t_puts (name);
	if (is_dir) t_putc ('/');
	t_putc ('\n');
}

static int list (const char *path)
{
	void *d = t_opendir (path);
	if (d == 0) { t_err ("cannot open", path); return 1; }
	struct t_dirent e;
	while (t_readdir (d, path, &e)) entry (e.name, e.is_dir, e.size);
	t_closedir (d);
	return 0;
}

int tool_main (int argc, char **argv)
{
	int a = 1, rc = 0;
	if (a < argc && t_streq (argv[a], "-l")) { o_long = 1; a++; }
	if (a >= argc) return list (".");
	if (argc - a == 1 && t_stat (argv[a], 0) != 1) return list (argv[a]);	// one folder: its entries alone

	int nfiles = 0;
	for (int i = a; i < argc; i++)				// the files, then the folders
	{
		long long size = 0;
		int kind = t_stat (argv[i], &size);
		if (kind == 1) { entry (argv[i], 0, size); nfiles++; }
		else if (kind == 0) { t_err ("cannot open", argv[i]); rc = 1; }
	}
	int first = nfiles == 0;
	for (int i = a; i < argc; i++)
	{
		if (t_stat (argv[i], 0) != 2) continue;
		if (!first) t_putc ('\n');
		first = 0;
		t_puts (argv[i]); t_puts (":\n");
		rc |= list (argv[i]);
	}
	return rc;
}
