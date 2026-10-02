/*
 * libgen.c -- POSIX basename / dirname (newlib's libc has only the GNU basename). Onyx paths:
 * "SD:/" (a volume's root) and "/" are roots. Built without _GNU_SOURCE: <string.h> would declare
 * the GNU basename.
 *
 * Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence: Permission is
 * hereby granted, free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished to
 * do so, subject to the following conditions: The above copyright notice and this permission
 * notice shall be included in all copies or substantial portions of the Software. THE SOFTWARE
 * IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
 */
#undef _GNU_SOURCE
#include <string.h>
#include <stddef.h>

static size_t root_len (const char *p)
{
	const char *c = strchr (p, ':');
	const char *s = strchr (p, '/');
	if (c && (s == 0 || c < s))
		return (size_t) (c - p) + 1 + (c[1] == '/');
	return p[0] == '/' ? 1 : 0;
}

char *basename (char *p)
{
	static char dot[] = ".";
	if (p == 0 || *p == '\0')
		return dot;
	size_t r = root_len (p), n = strlen (p);
	while (n > r && p[n - 1] == '/')
		p[--n] = '\0';
	if (n == r)
		return r ? p : dot;
	char *b = p + n;
	while (b > p + r && b[-1] != '/')
		b--;
	return b;
}
char *__xpg_basename (char *p) __attribute__ ((alias ("basename")));

char *dirname (char *p)
{
	static char dot[] = ".";
	if (p == 0 || *p == '\0')
		return dot;
	size_t r = root_len (p), n = strlen (p);
	while (n > r && p[n - 1] == '/')
		n--;
	while (n > r && p[n - 1] != '/')
		n--;
	if (n == 0)
		return dot;
	while (n > r && p[n - 1] == '/')
		n--;
	p[n] = '\0';
	return p;
}
