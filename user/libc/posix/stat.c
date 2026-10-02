/*
 * stat.c -- stat, access, unlink / rmdir / mkdir / rename, the *at calls, times, statvfs
 * (libonyxposix, docs/POSIX-PLAN.md §3.4).
 *
 * On the kernel's v75 path calls (path_stat: size, mode, mtime, a stable 64-bit id; path_unlink
 * of an open file -- it stays readable until its last close --, rename replacing its target,
 * mkdir, utime). On a kernel without them, the old calls: stat probes with opendir / open,
 * unlink and rmdir are kapi_remove, rename removes the target first.
 *
 * newlib's struct stat has a 16-bit st_ino and st_dev: st_ino is the kernel's 64-bit id folded
 * to 16 bits (SQLite tells files apart by (dev, ino): two different open databases could share
 * an id -- one in 65536). WP-TC's newlib can widen them.
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
#include <errno.h>
#include <utime.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/statvfs.h>
#include <reent.h>
#include "posix_internal.h"

void __onyx_stat_from_kapi (const struct kapi_stat *ks, struct stat *st)
{
	memset (st, 0, sizeof *st);
	st->st_dev = (dev_t) ks->dev;
	unsigned long long i = ks->ino;
	st->st_ino = (ino_t) (i ^ (i >> 16) ^ (i >> 32) ^ (i >> 48));
	st->st_mode = (mode_t) ks->mode;
	st->st_nlink = 1;
	st->st_size = (off_t) ks->size;
	st->st_atim.tv_sec = (time_t) ks->mtime;
	st->st_mtim.tv_sec = (time_t) ks->mtime;
	st->st_ctim.tv_sec = (time_t) ks->ctime;
	unsigned bs = ks->blksize;
	st->st_blksize = bs < 1024 ? 1024 : bs > 65536 ? 65536 : bs;
	st->st_blocks = (blkcnt_t) ks->blocks;
}

/* a root ("SD:/", "RAM:") */
static int is_root (const char *p)
{
	const char *c = strchr (p, ':');
	return c != 0 && (c[1] == '\0' || (c[1] == '/' && c[2] == '\0'));
}

static int legacy_stat (const char *p, struct stat *st)
{
	memset (st, 0, sizeof *st);
	st->st_nlink = 1;
	st->st_blksize = 4096;
	void *dh = kapi_opendir (p);
	if (dh)
	{
		kapi_closedir (dh);
		st->st_mode = S_IFDIR | 0755;
		return 0;
	}
	void *fh = kapi_open (p);
	if (fh)
	{
		st->st_size = (off_t) kapi_fsize64 (fh);
		st->st_blocks = (st->st_size + 511) / 512;
		kapi_close (fh);
		st->st_mode = S_IFREG | 0644;
		/* an id from the name (FNV-1a of the upper-cased path), as the kernel's */
		char abs[ONYX_PATH_MAX];
		if (__onyx_abspath (p, abs, sizeof abs) == 0)
		{
			unsigned long long h = 0xCBF29CE484222325ULL;
			for (const char *c = abs; *c; c++)
				h = (h ^ (unsigned char) (*c >= 'a' && *c <= 'z' ? *c - 32 : *c)) * 0x100000001B3ULL;
			st->st_ino = (ino_t) (h ^ (h >> 16) ^ (h >> 32) ^ (h >> 48));
		}
		return 0;
	}
	return ONYX_ERR (ENOENT);
}

int __onyx_path_stat (const char *p, struct stat *st)
{
	int dev = __onyx_dev_type (p);
	if (dev)
	{
		memset (st, 0, sizeof *st);
		st->st_mode = S_IFCHR | 0666;
		st->st_nlink = 1;
		st->st_blksize = 1024;
		st->st_rdev = (dev_t) dev;
		return 0;
	}
	if (kapi__core () != 0)
		return ONYX_ERR (ENOSYS);
	struct kapi_stat ks;
	int r = kapi_path_stat (p, &ks);
	if (r == 0)
	{
		__onyx_stat_from_kapi (&ks, st);
		return 0;
	}
	if (r != -KAPI_ENOSYS)
		return ONYX_ERR (-r);
	return legacy_stat (p, st);
}

int __onyx_path_is_dir (const char *p)
{
	struct stat st;
	if (__onyx_path_stat (p, &st) != 0)
		return 0;
	if (!S_ISDIR (st.st_mode))
	{
		errno = ENOTDIR;
		return 0;
	}
	return 1;
}

int __onyx_fstat (struct __onyx_ofd *d, struct stat *st)
{
	memset (st, 0, sizeof *st);
	st->st_nlink = 1;
	st->st_blksize = 4096;
	switch (d->type)
	{
	case ONYX_FD_FILE:
	{
		struct kapi_stat ks;
		long long r = kapi_file_stat (d->h, &ks);
		if (r < 0)
			return ONYX_ERR ((int) -r);
		__onyx_stat_from_kapi (&ks, st);
		return 0;
	}
	case ONYX_FD_LFILE:
	{
		int r = d->path ? legacy_stat (d->path, st) : 0;
		st->st_mode = S_IFREG | 0644;
		st->st_size = d->size;
		st->st_blocks = (d->size + 511) / 512;
		(void) r;
		return 0;
	}
	case ONYX_FD_DIR:
		return __onyx_path_stat (d->path, st);
	case ONYX_FD_PIPE_R:
	case ONYX_FD_PIPE_W:
	case ONYX_FD_STREAM:
		st->st_mode = S_IFIFO | 0600;
		return 0;
	case ONYX_FD_SOCKET:
	case ONYX_FD_LSOCKET:
		st->st_mode = S_IFSOCK | 0600;
		return 0;
	case ONYX_FD_CONSOLE:
		st->st_mode = S_IFCHR | 0620;	/* a tty: newlib keeps stdout line-buffered */
		st->st_blksize = 1024;
		return 0;
	default:
		st->st_mode = S_IFCHR | 0666;
		st->st_blksize = 1024;
		return 0;
	}
}

static long rpc_stat (long path, long st, long c)
{
	(void) c;
	char p[ONYX_PATH_MAX];
	if (__onyx_path ((const char *) path, p, sizeof p) == 0)
		return -1;
	return __onyx_path_stat (p, (struct stat *) st);
}

int _stat (const char *path, struct stat *st) { return (int) onyx_rpc3 (rpc_stat, (long) path, (long) st, 0); }
int stat (const char *path, struct stat *st) { return _stat (path, st); }
int lstat (const char *path, struct stat *st) { return _stat (path, st); }

int fstatat (int dirfd, const char *path, struct stat *st, int flags)
{
	(void) flags;
	char p[ONYX_PATH_MAX];
	if (path != 0 && *path == '\0' && (flags & 0x1000 /* AT_EMPTY_PATH */))
		return fstat (dirfd, st);
	if (__onyx_at_path (dirfd, path, p, sizeof p) == 0)
		return -1;
	return __onyx_path_stat (p, st);
}

int access (const char *path, int mode)
{
	char p[ONYX_PATH_MAX];
	struct stat st;
	if (__onyx_path (path, p, sizeof p) == 0 || __onyx_path_stat (p, &st) != 0)
		return -1;
	if ((mode & W_OK) && !(st.st_mode & 0222))
		return ONYX_ERR (EACCES);
	return 0;
}

int faccessat (int dirfd, const char *path, int mode, int flags)
{
	(void) flags;
	char p[ONYX_PATH_MAX];
	if (__onyx_at_path (dirfd, path, p, sizeof p) == 0)
		return -1;
	return access (p, mode);
}

int eaccess (const char *path, int mode) { return access (path, mode); }
int euidaccess (const char *path, int mode) { return access (path, mode); }

/* ---- unlink, rmdir, mkdir, rename ---- */
static int do_unlink (const char *p, int dir)
{
	if (kapi__core () != 0)
		return ONYX_ERR (ENOSYS);
	int r = kapi_path_unlink (p, dir ? KAPI_UNLINK_DIR : 0);
	if (r != -KAPI_ENOSYS)
		return r < 0 ? ONYX_ERR (-r) : 0;
	/* an older kernel: kapi_remove takes a file or an empty directory */
	struct stat st;
	if (legacy_stat (p, &st) != 0)
		return -1;
	if (dir && !S_ISDIR (st.st_mode))
		return ONYX_ERR (ENOTDIR);
	if (!dir && S_ISDIR (st.st_mode))
		return ONYX_ERR (EISDIR);
	if (is_root (p))
		return ONYX_ERR (EBUSY);
	if (kapi_remove (p) == 0)
		return 0;
	if (dir)
	{
		void *h = kapi_opendir (p);
		struct kapi_dirent e;
		int full = h != 0 && kapi_readdir (h, &e) == 1;
		if (h)
			kapi_closedir (h);
		if (full)
			return ONYX_ERR (ENOTEMPTY);
	}
	return ONYX_ERR (EACCES);
}

static long rpc_unlink (long path, long b, long c)
{
	(void) b; (void) c;
	char p[ONYX_PATH_MAX];
	if (__onyx_path ((const char *) path, p, sizeof p) == 0)
		return -1;
	return do_unlink (p, 0);
}

int _unlink (const char *path) { return (int) onyx_rpc3 (rpc_unlink, (long) path, 0, 0); }
int unlink (const char *path) { return _unlink (path); }

int rmdir (const char *path)
{
	char p[ONYX_PATH_MAX];
	if (__onyx_path (path, p, sizeof p) == 0)
		return -1;
	return do_unlink (p, 1);
}

int remove (const char *path)
{
	char p[ONYX_PATH_MAX];
	struct stat st;
	if (__onyx_path (path, p, sizeof p) == 0 || __onyx_path_stat (p, &st) != 0)
		return -1;
	return do_unlink (p, S_ISDIR (st.st_mode));
}

int _remove_r (struct _reent *r, const char *path) { (void) r; return remove (path); }

int unlinkat (int dirfd, const char *path, int flags)
{
	char p[ONYX_PATH_MAX];
	if (__onyx_at_path (dirfd, path, p, sizeof p) == 0)
		return -1;
	return do_unlink (p, (flags & AT_REMOVEDIR) != 0);
}

int mkdir (const char *path, mode_t mode)
{
	char p[ONYX_PATH_MAX];
	if (__onyx_path (path, p, sizeof p) == 0)
		return -1;
	if (kapi__core () != 0)
		return ONYX_ERR (ENOSYS);
	int r = kapi_path_mkdir (p, (unsigned) mode);
	if (r != -KAPI_ENOSYS)
		return r < 0 ? ONYX_ERR (-r) : 0;
	struct stat st;
	if (legacy_stat (p, &st) == 0)
		return ONYX_ERR (EEXIST);
	return kapi_mkdir (p) == 0 ? 0 : ONYX_ERR (ENOENT);
}

int mkdirat (int dirfd, const char *path, mode_t mode)
{
	char p[ONYX_PATH_MAX];
	if (__onyx_at_path (dirfd, path, p, sizeof p) == 0)
		return -1;
	return mkdir (p, mode);
}

static int do_rename (const char *a, const char *b)
{
	if (kapi__core () != 0)
		return ONYX_ERR (ENOSYS);
	int r = kapi_path_rename (a, b);
	if (r != -KAPI_ENOSYS)
		return r < 0 ? ONYX_ERR (-r) : 0;
	/* an older kernel: FatFs' rename fails onto an existing name -- remove it first */
	struct stat sa, sb;
	if (legacy_stat (a, &sa) != 0)
		return -1;
	if (legacy_stat (b, &sb) == 0)
	{
		char x[ONYX_PATH_MAX], y[ONYX_PATH_MAX];
		if (__onyx_abspath (a, x, sizeof x) == 0 && __onyx_abspath (b, y, sizeof y) == 0 &&
		    strcasecmp (x, y) == 0)
			return 0;
		if (S_ISDIR (sb.st_mode) != S_ISDIR (sa.st_mode))
			return ONYX_ERR (S_ISDIR (sb.st_mode) ? EISDIR : ENOTDIR);
		if (kapi_remove (b) != 0)
			return ONYX_ERR (S_ISDIR (sb.st_mode) ? ENOTEMPTY : EACCES);
	}
	return kapi_rename (a, b) == 0 ? 0 : ONYX_ERR (EXDEV);
}

/* newlib's rename goes through _link_r + _unlink_r: this one replaces it */
int rename (const char *from, const char *to)
{
	char a[ONYX_PATH_MAX], b[ONYX_PATH_MAX];
	if (__onyx_path (from, a, sizeof a) == 0 || __onyx_path (to, b, sizeof b) == 0)
		return -1;
	return do_rename (a, b);
}

int _rename_r (struct _reent *r, const char *from, const char *to) { (void) r; return rename (from, to); }

int renameat (int fda, const char *from, int fdb, const char *to)
{
	char a[ONYX_PATH_MAX], b[ONYX_PATH_MAX];
	if (__onyx_at_path (fda, from, a, sizeof a) == 0 || __onyx_at_path (fdb, to, b, sizeof b) == 0)
		return -1;
	return do_rename (a, b);
}

int _link (const char *a, const char *b) { (void) a; (void) b; return ONYX_ERR (EMLINK); }
int link (const char *a, const char *b) { return _link (a, b); }
int linkat (int fa, const char *a, int fb, const char *b, int f) { (void) fa; (void) fb; (void) f; return _link (a, b); }
int symlink (const char *a, const char *b) { (void) a; (void) b; return ONYX_ERR (EPERM); }
int symlinkat (const char *a, int fd, const char *b) { (void) a; (void) fd; (void) b; return ONYX_ERR (EPERM); }

ssize_t readlink (const char *path, char *buf, size_t n)
{
	(void) buf; (void) n;
	char p[ONYX_PATH_MAX];
	struct stat st;
	if (__onyx_path (path, p, sizeof p) == 0 || __onyx_path_stat (p, &st) != 0)
		return -1;
	return ONYX_ERR (EINVAL);			/* (not a link: there are none) */
}

ssize_t readlinkat (int dirfd, const char *path, char *buf, size_t n)
{
	char p[ONYX_PATH_MAX];
	if (__onyx_at_path (dirfd, path, p, sizeof p) == 0)
		return -1;
	return readlink (p, buf, n);
}

/* ---- permissions (FAT: a read-only attribute, which the kernel does not set yet) ---- */
static mode_t s_umask = 022;
mode_t umask (mode_t m) { mode_t old = s_umask; s_umask = m & 0777; return old; }
int chmod (const char *path, mode_t mode)
{
	(void) mode;
	char p[ONYX_PATH_MAX];
	struct stat st;
	if (__onyx_path (path, p, sizeof p) == 0)
		return -1;
	return __onyx_path_stat (p, &st);
}
int fchmod (int fd, mode_t mode) { (void) mode; struct stat st; return fstat (fd, &st); }
int fchmodat (int dirfd, const char *path, mode_t mode, int flags)
{
	(void) flags;
	char p[ONYX_PATH_MAX];
	if (__onyx_at_path (dirfd, path, p, sizeof p) == 0)
		return -1;
	return chmod (p, mode);
}
int chown (const char *path, uid_t u, gid_t g) { (void) u; (void) g; return chmod (path, 0); }
int lchown (const char *path, uid_t u, gid_t g) { return chown (path, u, g); }
int fchown (int fd, uid_t u, gid_t g) { (void) u; (void) g; return fchmod (fd, 0); }
int fchownat (int dirfd, const char *path, uid_t u, gid_t g, int f) { (void) u; (void) g; return fchmodat (dirfd, path, 0, f); }

/* ---- times ---- */
static int set_mtime (const char *path, long long mtime)
{
	char p[ONYX_PATH_MAX];
	if (__onyx_path (path, p, sizeof p) == 0)
		return -1;
	if (kapi__core () != 0)
		return ONYX_ERR (ENOSYS);
	int r = kapi_path_utime (p, mtime);
	return r < 0 ? ONYX_ERR (-r) : 0;
}

static long long now_s (void) { return __onyx_real_ns () / 1000000000LL; }

int utime (const char *path, const struct utimbuf *t) { return set_mtime (path, t ? (long long) t->modtime : now_s ()); }
int utimes (const char *path, const struct timeval tv[2]) { return set_mtime (path, tv ? (long long) tv[1].tv_sec : now_s ()); }

static long long ts_mtime (const struct timespec ts[2])
{
	if (ts == 0 || ts[1].tv_nsec == UTIME_NOW)
		return now_s ();
	return (long long) ts[1].tv_sec;
}

int utimensat (int dirfd, const char *path, const struct timespec ts[2], int flags)
{
	(void) flags;
	if (ts && ts[1].tv_nsec == UTIME_OMIT)
		return 0;
	char p[ONYX_PATH_MAX];
	if (__onyx_at_path (dirfd, path, p, sizeof p) == 0)
		return -1;
	return set_mtime (p, ts_mtime (ts));
}

int futimens (int fd, const struct timespec ts[2])
{
	if (ts && ts[1].tv_nsec == UTIME_OMIT)
		return 0;
	struct __onyx_ofd *d = __onyx_fd_get (fd);
	if (d == 0)
		return -1;
	int r = d->path ? set_mtime (d->path, ts_mtime (ts)) : ONYX_ERR (EBADF);
	__onyx_fd_put (d);
	return r;
}

int futimes (int fd, const struct timeval tv[2])
{
	struct timespec ts[2];
	if (tv == 0)
		return futimens (fd, 0);
	ts[0].tv_sec = tv[0].tv_sec; ts[0].tv_nsec = tv[0].tv_usec * 1000;
	ts[1].tv_sec = tv[1].tv_sec; ts[1].tv_nsec = tv[1].tv_usec * 1000;
	return futimens (fd, ts);
}

/* ---- statvfs ---- */
int statvfs (const char *path, struct statvfs *s)
{
	char p[ONYX_PATH_MAX];
	if (__onyx_path (path, p, sizeof p) == 0)
		return -1;
	struct kapi_vol_info vi;
	if (kapi__core () != 0 || kapi_vol_info (p, &vi) != 0)
		return ONYX_ERR (ENOENT);
	memset (s, 0, sizeof *s);
	s->f_bsize = s->f_frsize = 4096;
	s->f_blocks = vi.total / 4096;
	s->f_bfree = s->f_bavail = vi.free / 4096;
	s->f_files = 65536;
	s->f_ffree = s->f_favail = 65536;
	s->f_namemax = 255;
	return 0;
}

int fstatvfs (int fd, struct statvfs *s)
{
	struct __onyx_ofd *d = __onyx_fd_get (fd);
	if (d == 0)
		return -1;
	int r = d->path ? statvfs (d->path, s) : ONYX_ERR (EINVAL);
	__onyx_fd_put (d);
	return r;
}
