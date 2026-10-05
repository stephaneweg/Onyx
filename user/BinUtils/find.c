//
// find -- walk a folder and its sub-folders, printing what is there.
//   usage: find [folder ...] [-name PATTERN] [-type f|d] [-maxdepth N]
// Without a folder: the current one. -name: only the entries whose name matches PATTERN
// (* any characters, ? one character; upper / lower case alike, as the card's names);
// -type f: files only, -type d: folders only; -maxdepth N: no deeper than N levels.
//   find SD:/docs -name "*.txt"       find . -type d       find -name "read*"
//
// Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence (see tool.h).
//
#define TOOL_NAME "find"
#include "tool.h"

static const char *o_name; static int o_type; static long o_depth = 64;

static int glob (const char *p, const char *s)
{
	for (; *p; p++, s++)
	{
		if (*p == '*')
		{
			while (p[1] == '*') p++;
			for (;; s++)
			{
				if (glob (p + 1, s)) return 1;
				if (*s == '\0') return 0;
			}
		}
		if (*s == '\0') return 0;
		if (*p != '?' && t_lower ((unsigned char) *p) != t_lower ((unsigned char) *s)) return 0;
	}
	return *s == '\0';
}

static void visit (const char *path, const char *name, int is_dir)
{
	if (o_type && (o_type == 'd') != (is_dir != 0)) return;
	if (o_name && !glob (o_name, name)) return;
	t_puts (path); t_putc ('\n');
}

static int walk (const char *dir, long depth)
{
	void *d = t_opendir (dir);
	if (d == 0) { t_err ("cannot open", dir); return 1; }
	// (the entries first, the folder closed, then the sub-folders: one open folder at a time)
	char **sub = 0; long nsub = 0;
	struct t_dirent e;
	int dl = t_strlen (dir), rc = 0;
	while (t_readdir (d, dir, &e))
	{
		int nl = t_strlen (e.name);
		char *p = (char *) t_malloc (dl + nl + 2);
		t_memcpy (p, dir, dl);
		int k = dl;
		if (dl > 0 && dir[dl - 1] != '/') p[k++] = '/';
		t_memcpy (p + k, e.name, nl + 1);
		visit (p, e.name, e.is_dir);
		if (e.is_dir && depth < o_depth)
		{
			sub = (char **) t_realloc (sub, (nsub + 1) * (long) sizeof *sub);
			sub[nsub++] = p;
		}
		else t_free (p);
	}
	t_closedir (d);
	for (long i = 0; i < nsub; i++) { rc |= walk (sub[i], depth + 1); t_free (sub[i]); }
	t_free (sub);
	return rc;
}

int tool_main (int argc, char **argv)
{
	int a = 1, nroots = 0, rc = 0;
	while (a + nroots < argc && argv[a + nroots][0] != '-') nroots++;
	for (int i = a + nroots; i < argc; i += 2)
	{
		if (i + 1 >= argc) { t_err ("a value is missing after", argv[i]); return 2; }
		if (t_streq (argv[i], "-name")) o_name = argv[i + 1];
		else if (t_streq (argv[i], "-type") && (argv[i + 1][0] == 'f' || argv[i + 1][0] == 'd')) o_type = argv[i + 1][0];
		else if (t_streq (argv[i], "-maxdepth") && t_atol (argv[i + 1], &o_depth)) { }
		else { t_puts ("usage: find [folder ...] [-name PATTERN] [-type f|d] [-maxdepth N]\n"); return 2; }
	}
	for (int i = 0; i < nroots || (nroots == 0 && i == 0); i++)
	{
		const char *root = nroots ? argv[a + i] : ".";
		int kind = t_stat (root, 0);
		if (kind == 1) { visit (root, t_basename (root), 0); continue; }
		visit (root, t_basename (root), 1);
		if (o_depth >= 1) rc |= walk (root, 1);
	}
	return rc;
}
