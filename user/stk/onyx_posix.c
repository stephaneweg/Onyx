/*
 * onyx_posix.c -- the POSIX file calls SuperTuxKart and its Irrlicht use and that newlib's syscall
 * layer (../libc/onyx_syscalls.c) does not give: the directory listing (compat/dirent.h), stat by
 * name, mkdir, chdir / getcwd, access. On the kapi's FatFs calls.
 *
 * stat: a directory is what kapi_opendir opens; a file is what kapi_open opens (its size from
 * kapi_fsize). No times (st_mtime 0): STK compares file dates only for its caches.
 */
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include "appkit/appkit.h"
#include "compat/dirent.h"

struct __onyx_dir
{
	void *h;
	char path[256];
	long pos;
	struct dirent ent;
};

DIR *opendir (const char *path)
{
	void *h = kapi_opendir (path && *path ? path : ".");
	if (!h)
	{
		errno = ENOENT;
		return 0;
	}
	DIR *d = (DIR *) calloc (1, sizeof *d);
	if (!d)
	{
		kapi_closedir (h);
		errno = ENOMEM;
		return 0;
	}
	d->h = h;
	strncpy (d->path, path && *path ? path : ".", sizeof d->path - 1);
	return d;
}

struct dirent *readdir (DIR *d)
{
	struct kapi_dirent e;
	if (!d || !d->h || kapi_readdir (d->h, &e) != 1)
		return 0;
	memset (&d->ent, 0, sizeof d->ent);
	strncpy (d->ent.d_name, e.name, sizeof d->ent.d_name - 1);
	d->ent.d_type = e.is_dir ? DT_DIR : DT_REG;
	d->ent.d_ino = (ino_t) ++d->pos;
	return &d->ent;
}

int closedir (DIR *d)
{
	if (!d)
		return -1;
	if (d->h)
		kapi_closedir (d->h);
	free (d);
	return 0;
}

void rewinddir (DIR *d)
{
	if (!d)
		return;
	if (d->h)
		kapi_closedir (d->h);
	d->h = kapi_opendir (d->path);
	d->pos = 0;
}

long telldir (DIR *d) { return d ? d->pos : -1; }

void seekdir (DIR *d, long pos)
{
	rewinddir (d);
	while (d && d->pos < pos && readdir (d))
		;
}

static int stat_path (const char *path, struct stat *st)
{
	memset (st, 0, sizeof *st);
	void *dh = kapi_opendir (path);
	if (dh)
	{
		kapi_closedir (dh);
		st->st_mode = S_IFDIR | 0777;
		return 0;
	}
	void *fh = kapi_open (path);
	if (fh)
	{
		st->st_size = kapi_fsize (fh);
		kapi_close (fh);
		st->st_mode = S_IFREG | 0666;
		st->st_nlink = 1;
		return 0;
	}
	errno = ENOENT;
	return -1;
}

int _stat (const char *path, struct stat *st) { return stat_path (path, st); }
int stat (const char *path, struct stat *st) { return stat_path (path, st); }
int lstat (const char *path, struct stat *st) { return stat_path (path, st); }

int access (const char *path, int mode)
{
	struct stat st;
	(void) mode;
	return stat_path (path, &st);
}

int mkdir (const char *path, mode_t mode)
{
	(void) mode;
	if (kapi_mkdir (path) == 0)
		return 0;
	errno = EEXIST;
	return -1;
}

int chdir (const char *path)
{
	if (kapi_chdir (path))
		return 0;
	errno = ENOENT;
	return -1;
}

char *getcwd (char *buf, size_t size)
{
	if (!buf)
	{
		size = size ? size : 256;
		buf = (char *) malloc (size);
		if (!buf)
			return 0;
	}
	kapi_getcwd (buf, (unsigned) size);
	return buf;
}
