/*
 * dir.c -- opendir / readdir / scandir (libonyxposix, docs/POSIX-PLAN.md §3.4).
 *
 * A DIR holds a kernel directory handle (kapi opendir) and reads it with v75 dir_read (names up
 * to 255 characters, the size, the mode, the 64-bit id), or kapi readdir (127 characters) on an
 * older kernel. There are no "." / ".." entries. dirfd gives a directory descriptor (fd.c) for
 * fstat / fchdir / openat; fdopendir takes one.
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
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include "posix_internal.h"

struct __onyx_dir
{
	void *h;				/* the kernel handle */
	int fd;					/* its descriptor (dirfd / fdopendir), -1 none */
	int legacy;				/* the kernel has no dir_read */
	long pos;
	char *path;
	struct dirent ent;
};

DIR *opendir (const char *path)
{
	char p[ONYX_PATH_MAX];
	if (__onyx_path (path && *path ? path : ".", p, sizeof p) == 0)
		return 0;
	if (kapi__core () != 0)
	{
		errno = ENOSYS;
		return 0;
	}
	void *h = kapi_opendir (p);
	if (h == 0)
	{
		struct stat st;
		errno = __onyx_path_stat (p, &st) == 0 ? ENOTDIR : ENOENT;
		return 0;
	}
	DIR *d = (DIR *) calloc (1, sizeof *d);
	if (d == 0 || (d->path = strdup (p)) == 0)
	{
		free (d);
		kapi_closedir (h);
		errno = ENOMEM;
		return 0;
	}
	d->h = h;
	d->fd = -1;
	return d;
}

DIR *fdopendir (int fd)
{
	struct __onyx_ofd *o = __onyx_fd_get (fd);
	if (o == 0)
		return 0;
	DIR *d = 0;
	if (o->type != ONYX_FD_DIR || o->path == 0)
		errno = ENOTDIR;
	else if ((d = opendir (o->path)) != 0)
		d->fd = fd;				/* (closedir closes it) */
	__onyx_fd_put (o);
	return d;
}

struct dirent *readdir (DIR *d)
{
	if (d == 0 || d->h == 0)
	{
		errno = EBADF;
		return 0;
	}
	struct dirent *e = &d->ent;
	memset (e, 0, sizeof *e);
	if (!d->legacy)
	{
		struct kapi_dirent2 k;
		int r = kapi_dir_read (d->h, &k);
		if (r == 1)
		{
			k.name[sizeof k.name - 1] = '\0';
			strcpy (e->d_name, k.name);
			e->d_type = (k.mode & KAPI_S_IFMT) == KAPI_S_IFDIR ? DT_DIR : DT_REG;
			e->d_ino = (ino_t) (k.ino ^ (k.ino >> 16) ^ (k.ino >> 32) ^ (k.ino >> 48));
			goto done;
		}
		if (r == 0)
			return 0;			/* the end (errno untouched) */
		if (r != -KAPI_ENOSYS)
		{
			errno = -r;
			return 0;
		}
		d->legacy = 1;
	}
	struct kapi_dirent k;
	if (kapi_readdir (d->h, &k) != 1)
		return 0;
	k.name[sizeof k.name - 1] = '\0';
	strcpy (e->d_name, k.name);
	e->d_type = k.is_dir ? DT_DIR : DT_REG;
	e->d_ino = (ino_t) (d->pos + 1);
done:
	d->pos++;
	e->d_off = d->pos;
	e->d_reclen = sizeof *e;
	return e;
}

int readdir_r (DIR *d, struct dirent *entry, struct dirent **result)
{
	int e = errno;
	errno = 0;
	struct dirent *r = readdir (d);
	if (r == 0)
	{
		*result = 0;
		int err = errno;
		errno = e;
		return err;
	}
	memcpy (entry, r, sizeof *entry);
	*result = entry;
	errno = e;
	return 0;
}

int closedir (DIR *d)
{
	if (d == 0)
	{
		errno = EBADF;
		return -1;
	}
	if (d->h)
		kapi_closedir (d->h);
	if (d->fd >= 0)
		__onyx_fd_close (d->fd);
	free (d->path);
	free (d);
	return 0;
}

void rewinddir (DIR *d)
{
	if (d == 0)
		return;
	if (d->h)
		kapi_closedir (d->h);
	d->h = kapi_opendir (d->path);
	d->pos = 0;
}

long telldir (DIR *d) { return d ? d->pos : -1; }

void seekdir (DIR *d, long pos)
{
	if (d == 0)
		return;
	rewinddir (d);
	while (d->pos < pos && readdir (d))
		;
}

int dirfd (DIR *d)
{
	if (d == 0)
	{
		errno = EINVAL;
		return -1;
	}
	if (d->fd < 0)
	{
		struct __onyx_ofd *o = __onyx_ofd_new (ONYX_FD_DIR, O_RDONLY);
		if (o == 0)
			return -1;
		o->path = strdup (d->path);
		d->fd = __onyx_fd_install (o, 0, 1);
	}
	return d->fd;
}

int alphasort (const struct dirent **a, const struct dirent **b) { return strcoll ((*a)->d_name, (*b)->d_name); }
int versionsort (const struct dirent **a, const struct dirent **b) { return strcmp ((*a)->d_name, (*b)->d_name); }

int scandir (const char *path, struct dirent ***list, int (*sel) (const struct dirent *),
	     int (*cmp) (const struct dirent **, const struct dirent **))
{
	DIR *d = opendir (path);
	if (d == 0)
		return -1;
	struct dirent **v = 0;
	size_t n = 0, cap = 0;
	struct dirent *e;
	while ((e = readdir (d)) != 0)
	{
		if (sel && !sel (e))
			continue;
		if (n == cap)
		{
			cap = cap ? cap * 2 : 32;
			struct dirent **nv = (struct dirent **) realloc (v, cap * sizeof *v);
			if (nv == 0)
				goto oom;
			v = nv;
		}
		v[n] = (struct dirent *) malloc (sizeof *e);
		if (v[n] == 0)
			goto oom;
		memcpy (v[n++], e, sizeof *e);
	}
	closedir (d);
	if (cmp && n > 1)
		qsort (v, n, sizeof *v, (int (*) (const void *, const void *)) cmp);
	*list = v;
	return (int) n;
oom:
	while (n)
		free (v[--n]);
	free (v);
	closedir (d);
	errno = ENOMEM;
	return -1;
}
