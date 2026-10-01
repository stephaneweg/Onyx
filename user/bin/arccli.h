//
// bin/arccli.h -- what zip and unzip share: the command line split into words (quotes kept
// together), a name matched against a pattern (* and ?, or a folder: everything under it), a
// path of the disk made an archive name. On the Archiver's engine (Apps/archiver/ops.h).
//
#ifndef _bin_arccli_h
#define _bin_arccli_h

#include <stdio.h>
#include "Apps/archiver/ops.h"

using namespace arc;

// The arguments (kapi_get_args) split into argv-like words
static inline int cli_args (char *buf, int cap, char **av, int max)
{
	int n = kapi_get_args (buf, (unsigned) cap - 1);
	buf[n > 0 && n < cap ? n : 0] = 0;
	int ac = 0; char *s = buf, *w = buf;
	while (*s && ac < max)
	{
		while (*s == ' ' || *s == '\t') s++;
		if (!*s) break;
		av[ac++] = w;
		char q = 0;
		while (*s && (q || (*s != ' ' && *s != '\t')))
		{
			if (!q && (*s == '"' || *s == '\'')) { q = *s++; continue; }
			if (q && *s == q) { q = 0; s++; continue; }
			*w++ = *s++;
		}
		if (*s) s++;
		*w++ = 0;
	}
	return ac;
}

// * and ? (case aside: FAT is)
static inline bool glob (const char *p, const char *s)
{
	for (; *p; p++, s++)
	{
		if (*p == '*')
		{
			while (p[1] == '*') p++;
			if (!p[1]) return true;
			for (; *s; s++) if (glob (p + 1, s)) return true;
			return false;
		}
		if (!*s) return false;
		if (*p != '?' && lower (*p) != lower (*s)) return false;
	}
	return !*s;
}
// an entry chosen by the patterns (none: all of them)
static inline bool chosen (const char *name, char **pat, int np)
{
	if (!np) return true;
	for (int i = 0; i < np; i++)
	{
		char p[300]; scopy (p, pat[i], sizeof p);
		int n = (int) strlen (p); while (n && p[n - 1] == '/') p[--n] = 0;
		if (glob (p, name) || under (name, p)) return true;
	}
	return false;
}
// "SD:/a/./b/../c.txt" -> "a/b/c.txt" (no volume, no leading '/', no '.' or '..')
static inline void archive_name (const char *path, char *out, int cap)
{
	const char *s = path;
	const char *colon = strchr (s, ':'); if (colon && colon - s < 6) s = colon + 1;
	safe_rel (s, out, cap);
}
static inline void dir_of (const char *name, char *out, int cap)
{
	scopy (out, name, cap);
	char *sl = strrchr (out, '/'); if (sl) *sl = 0; else out[0] = 0;
}
static inline void pct (u64 size, u64 packed, char *out, int cap)
{
	int p = size && packed < size ? (int) ((size - packed) * 100 / size) : 0;
	snprintf (out, (size_t) cap, "%d%%", p);
}

#endif
