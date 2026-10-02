/*
 * path.c -- POSIX paths on Onyx (libonyxposix): /tmp, /dev, realpath, the working directory.
 *
 * Onyx paths name a volume: "SD:/a/b", "SD1:/x", "USB:/y", "RAM:/z". The kernel resolves "x",
 * "./x", "../x" against the working directory and "/x" against the working directory's volume,
 * so POSIX-style relative and rooted paths work as they are. Two names are mapped here:
 *  - "/tmp" and "/tmp/..." -> "RAM:/tmp/..." (the RAM volume, kernel v71; created at the first
 *    use), so mkstemp / tmpfile / TMPDIR-less code write to memory, not the card;
 *  - "/dev/null", "/dev/zero", "/dev/urandom", "/dev/random", "/dev/stdin|stdout|stderr",
 *    "/dev/tty": pseudo files (fd.c).
 * realpath gives the absolute, normalised Onyx form ("SD:/a/b"); there are no links.
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
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <errno.h>
#include <sys/stat.h>
#include "posix_internal.h"

static volatile int s_tmpMade;

static int starts_with_dir (const char *p, const char *dir)
{
	size_t n = strlen (dir);
	return strncmp (p, dir, n) == 0 && (p[n] == '\0' || p[n] == '/');
}

char *__onyx_path (const char *in, char *buf, size_t cap)
{
	if (in == 0)
	{
		errno = EFAULT;
		return 0;
	}
	if (starts_with_dir (in, "/tmp"))
	{
		if (!s_tmpMade && kapi__core () == 0)
		{
			kapi_mkdir ("RAM:/tmp");	/* (-1 when it exists, or no RAM: volume) */
			s_tmpMade = 1;
		}
		if (strlen (in) + 4 >= cap)
		{
			errno = ENAMETOOLONG;
			return 0;
		}
		strcpy (buf, "RAM:");
		strcat (buf, in);
		return buf;
	}
	if (strlen (in) >= cap)
	{
		errno = ENAMETOOLONG;
		return 0;
	}
	strcpy (buf, in);
	return buf;
}

int __onyx_dev_type (const char *p)
{
	if (strncmp (p, "/dev/", 5) != 0)
		return 0;
	p += 5;
	if (!strcmp (p, "null")) return ONYX_FD_NULL;
	if (!strcmp (p, "zero")) return ONYX_FD_ZERO;
	if (!strcmp (p, "urandom") || !strcmp (p, "random")) return ONYX_FD_RANDOM;
	if (!strcmp (p, "stdin") || !strcmp (p, "stdout") || !strcmp (p, "stderr") ||
	    !strcmp (p, "tty") || !strcmp (p, "console") || !strncmp (p, "fd/", 3))
		return ONYX_FD_CONSOLE;
	return 0;
}

/* the length of a volume prefix ("SD:", "RAM:", "USB1:"), 0 if none */
static unsigned volume_prefix (const char *p)
{
	unsigned i = 0;
	while (p[i] != '\0' && p[i] != '/' && p[i] != ':' && i < 16)
		i++;
	return p[i] == ':' && i > 0 ? i + 1 : 0;
}

int __onyx_abspath (const char *in, char *out, size_t cap)
{
	char raw[ONYX_PATH_MAX * 2];
	char tmp[ONYX_PATH_MAX];
	if (__onyx_path (in, tmp, sizeof tmp) == 0)
		return -1;
	in = tmp;
	unsigned nv = volume_prefix (in);
	if (nv)
		snprintf (raw, sizeof raw, "%s", in);
	else
	{
		char cwd[ONYX_PATH_MAX];
		if (kapi_getcwd (cwd, sizeof cwd) <= 0)
			strcpy (cwd, "SD:/");
		if (in[0] == '/')
		{
			unsigned n = volume_prefix (cwd);
			if (n == 0)
				snprintf (raw, sizeof raw, "SD:%s", in);
			else
				snprintf (raw, sizeof raw, "%.*s%s", (int) n, cwd, in);
		}
		else
			snprintf (raw, sizeof raw, "%s/%s", cwd, in);
	}

	/* normalise: the volume upper-cased (SD0: is SD:), then ".", ".." and "//" */
	nv = volume_prefix (raw);
	size_t o = 0;
	if (nv == 4 && (raw[0] == 'S' || raw[0] == 's') && (raw[1] == 'D' || raw[1] == 'd') && raw[2] == '0')
	{
		memcpy (out, "SD:", 3);
		o = 3;
	}
	else
		for (unsigned k = 0; k < nv && o < cap - 1; k++)
			out[o++] = raw[k] >= 'a' && raw[k] <= 'z' ? (char) (raw[k] - 32) : raw[k];
	size_t base = o;
	size_t starts[128];
	int depth = 0;
	size_t i = nv;
	while (raw[i] != '\0')
	{
		while (raw[i] == '/')
			i++;
		if (raw[i] == '\0')
			break;
		size_t j = i;
		while (raw[j] != '\0' && raw[j] != '/')
			j++;
		size_t len = j - i;
		if (len == 1 && raw[i] == '.')
			;
		else if (len == 2 && raw[i] == '.' && raw[i + 1] == '.')
		{
			if (depth > 0)
				o = starts[--depth];
		}
		else
		{
			if (depth < 128)
				starts[depth++] = o;
			if (o + len + 2 >= cap)
			{
				errno = ENAMETOOLONG;
				return -1;
			}
			out[o++] = '/';
			memcpy (out + o, raw + i, len);
			o += len;
		}
		i = j;
	}
	if (o == base)
		out[o++] = '/';
	out[o] = '\0';
	return 0;
}

char *realpath (const char *path, char *resolved)
{
	if (path == 0)
	{
		errno = EINVAL;
		return 0;
	}
	if (*path == '\0')
	{
		errno = ENOENT;
		return 0;
	}
	char buf[ONYX_PATH_MAX];
	if (__onyx_abspath (path, buf, sizeof buf) != 0)
		return 0;
	struct stat st;
	if (__onyx_path_stat (buf, &st) != 0)
		return 0;
	if (resolved == 0)
		return strdup (buf);
	strncpy (resolved, buf, PATH_MAX - 1);
	resolved[PATH_MAX - 1] = '\0';
	return resolved;
}

char *canonicalize_file_name (const char *path) { return realpath (path, 0); }

char *getcwd (char *buf, size_t size)
{
	char tmp[ONYX_PATH_MAX];
	int n = kapi_getcwd (tmp, sizeof tmp);
	if (n <= 0)
	{
		errno = ENOENT;
		return 0;
	}
	size_t len = strlen (tmp);
	if (buf == 0)
	{
		if (size == 0)
			size = len + 1;
		if (size <= len)
		{
			errno = ERANGE;
			return 0;
		}
		buf = (char *) malloc (size);
		if (buf == 0)
		{
			errno = ENOMEM;
			return 0;
		}
	}
	else if (size == 0)
	{
		errno = EINVAL;
		return 0;
	}
	else if (size <= len)
	{
		errno = ERANGE;
		return 0;
	}
	memcpy (buf, tmp, len + 1);
	return buf;
}

char *getwd (char *buf) { return getcwd (buf, PATH_MAX); }
char *get_current_dir_name (void) { return getcwd (0, 0); }

int chdir (const char *path)
{
	char p[ONYX_PATH_MAX];
	if (__onyx_path (path, p, sizeof p) == 0)
		return -1;
	struct stat st;
	if (__onyx_path_stat (p, &st) != 0)
		return -1;
	if (!S_ISDIR (st.st_mode))
		return ONYX_ERR (ENOTDIR);
	if (!kapi_chdir (p))
		return ONYX_ERR (ENOENT);
	return 0;
}

int fchdir (int fd)
{
	struct __onyx_ofd *d = __onyx_fd_get (fd);
	if (d == 0)
		return -1;
	int r;
	if (d->type != ONYX_FD_DIR || d->path == 0)
		r = ONYX_ERR (ENOTDIR);
	else
		r = kapi_chdir (d->path) ? 0 : ONYX_ERR (ENOENT);
	__onyx_fd_put (d);
	return r;
}

char *__onyx_at_path (int dirfd, const char *name, char *buf, size_t cap)
{
	if (name == 0)
	{
		errno = EFAULT;
		return 0;
	}
	if (dirfd == AT_FDCWD || name[0] == '/' || volume_prefix (name))
		return __onyx_path (name, buf, cap);
	struct __onyx_ofd *d = __onyx_fd_get (dirfd);
	if (d == 0)
		return 0;
	char *r = 0;
	if (d->type != ONYX_FD_DIR || d->path == 0)
		errno = ENOTDIR;
	else if ((size_t) snprintf (buf, cap, "%s/%s", d->path, name) >= cap)
		errno = ENAMETOOLONG;
	else
		r = buf;
	__onyx_fd_put (d);
	return r;
}
