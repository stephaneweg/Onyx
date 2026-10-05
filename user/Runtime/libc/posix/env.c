/*
 * env.c -- the environment and the arguments a program starts with (libonyxposix,
 * docs/POSIX-PLAN.md §3.4).
 *
 * crt0posix builds argc / argv from kapi get_argv (v75: the spawner's argv block, argv[0] the
 * program's path) and environ from get_env (the environment the process was given: its
 * spawner's, or the system's default, SD:/etc/environment). On an older kernel: argv is the
 * kapi get_args line split on spaces (an argument in double quotes may hold spaces), argv[0]
 * "a.out", and the environment the defaults below. getenv / setenv / unsetenv / putenv are
 * newlib's, on this environ. TZ is set from the kernel's time zone when the environment has none
 * (time.c).
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
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include "posix_internal.h"

static char *s_noenv[1] = { 0 };
char **environ = s_noenv;
char *__onyx_progname = "a.out";
char *program_invocation_name = "a.out";
char *program_invocation_short_name = "a.out";

static char *s_defaults[] =
{
	"HOME=SD:/home", "PATH=SD:/bin", "TMPDIR=RAM:/tmp", "LANG=C.UTF-8", "TERM=dumb", 0
};

/* a kapi block call (get_argv / get_env): the whole block in a malloc'd buffer -> its size */
static int get_block (int (*fn) (char *, unsigned), char **out)
{
	unsigned cap = 4096;
	for (;;)
	{
		char *b = (char *) malloc (cap + 2);
		if (b == 0)
			return -KAPI_ENOMEM;
		int n = fn (b, cap);
		if (n < 0)
		{
			free (b);
			return n;
		}
		if ((unsigned) n <= cap || cap >= (1U << 20))
		{
			if ((unsigned) n > cap)
				n = (int) cap;
			b[n] = '\0';
			b[n + 1] = '\0';
			*out = b;
			return n;
		}
		free (b);
		cap = (unsigned) n;
	}
}

/* the strings of a block ("a\0b\0\0") -> a NULL-ended vector (pointing into the block) */
static char **split_block (char *b, int n, int *count)
{
	int c = 0;
	for (int i = 0; i < n && b[i] != '\0'; i += (int) strlen (b + i) + 1)
		c++;
	char **v = (char **) malloc ((c + 1) * sizeof *v);
	if (v == 0)
		return 0;
	int k = 0;
	for (int i = 0; i < n && b[i] != '\0' && k < c; i += (int) strlen (b + i) + 1)
		v[k++] = b + i;
	v[k] = 0;
	*count = k;
	return v;
}

static int kenv (char *b, unsigned cap) { return kapi_get_env (b, cap); }
static int kargv (char *b, unsigned cap) { return kapi_get_argv (b, cap); }

void __onyx_env_init (void)
{
	char *b;
	int n = get_block (kenv, &b);
	if (n >= 0)
	{
		int c;
		char **v = split_block (b, n, &c);
		if (v)
			environ = v;
	}
	else
		environ = s_defaults;

	/* TZ from the kernel's offset when the environment has none (no daylight saving rules) */
	if (getenv ("TZ") == 0)
	{
		int tz = __onyx_tz_minutes ();
		char z[32];
		if (tz == 0)
			strcpy (z, "UTC0");
		else
		{
			int a = tz < 0 ? -tz : tz;
			/* POSIX TZ: the offset to ADD to the local time to get UTC */
			snprintf (z, sizeof z, "LCL%c%02d:%02d", tz > 0 ? '-' : '+', a / 60, a % 60);
		}
		setenv ("TZ", z, 1);
	}
	tzset ();
}

int __onyx_args_init (char ***pargv)
{
	char *b;
	int argc = 0;
	char **argv = 0;
	int n = get_block (kargv, &b);
	if (n > 0)
		argv = split_block (b, n, &argc);
	if (argv == 0 || argc == 0)
	{
		/* an older kernel: the argument line, argv[0] unknown */
		char *line = 0;
		int m = get_block (kapi_get_args, &line);
		int cap = 8;
		argv = (char **) malloc (cap * sizeof *argv);
		if (argv == 0)
		{
			static char *none[2] = { "a.out", 0 };
			*pargv = none;
			return 1;
		}
		argc = 0;
		argv[argc++] = "a.out";
		char *p = m > 0 ? line : 0;
		while (p && *p)
		{
			while (*p == ' ' || *p == '\t')
				p++;
			if (*p == '\0')
				break;
			char *start, *end;
			if (*p == '"')
			{
				start = ++p;
				while (*p && *p != '"')
					p++;
			}
			else
			{
				start = p;
				while (*p && *p != ' ' && *p != '\t')
					p++;
			}
			end = p;
			if (*p)
				p++;
			*end = '\0';
			if (argc + 2 > cap)
			{
				cap *= 2;
				char **nv = (char **) realloc (argv, cap * sizeof *argv);
				if (nv == 0)
					break;
				argv = nv;
			}
			argv[argc++] = start;
		}
		argv[argc] = 0;
	}
	__onyx_progname = argv[0];
	program_invocation_name = argv[0];
	const char *s = argv[0];
	for (const char *q = argv[0]; *q; q++)
		if (*q == '/' || *q == ':')
			s = q + 1;
	program_invocation_short_name = (char *) s;
	*pargv = argv;
	return argc;
}

const char *getprogname (void) { return program_invocation_short_name; }
void setprogname (const char *name) { program_invocation_short_name = (char *) name; }

int clearenv (void)
{
	environ = s_noenv;
	return 0;
}
