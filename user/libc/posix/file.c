/*
 * file.c -- open / read / write / lseek / pread / pwrite / ftruncate / fsync / close on the
 * descriptions of fd.c (libonyxposix, docs/POSIX-PLAN.md §3.4).
 *
 * Files are the kernel's v75 open files (kapi file_open...: 64-bit offsets kept per handle,
 * pread / pwrite, O_APPEND / O_EXCL / O_TRUNC, unlink while open): incremental I/O, nothing
 * buffered here. On a kernel without them (file_open -> ENOSYS) a file falls back to the old
 * calls ("LFILE"): opened read-only it reads through a kapi_open handle, seeking with kapi_seek
 * (v57); opened for writing it is held whole in memory and written back by fsync and close
 * (kapi_save_file) -- the model of user/libc/onyx_syscalls.c, which the existing apps keep.
 *
 * Directories open as descriptors of their own (O_DIRECTORY, or O_RDONLY on a directory):
 * fstat, fchdir, fsync (SQLite syncs its directory), openat. Pipes read / write the kernel
 * stream (O_NONBLOCK: stream_read_nb, v75 stream_write_nb). /dev/null, /dev/zero and
 * /dev/urandom are served here.
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
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include "posix_internal.h"

#define CHUNK_MAX	(1U << 30)		/* one kapi call moves at most 1 GB */

/* ---- the old file calls (a kernel without v75 files) ---- */
static int lfile_reserve (struct __onyx_ofd *d, long long need)
{
	if (need <= d->cap)
		return 0;
	long long n = d->cap ? d->cap : 4096;
	while (n < need)
		n *= 2;
	unsigned char *b = (unsigned char *) realloc (d->buf, (size_t) n);
	if (b == 0)
		return ONYX_ERR (ENOMEM);
	d->buf = b;
	d->cap = n;
	return 0;
}

/* read the whole file of an LFILE into its buffer (from its kapi_open handle) */
static int lfile_slurp (struct __onyx_ofd *d)
{
	void *h = kapi_open (d->path);
	if (h == 0)
		return 0;				/* (a new file) */
	unsigned long long sz = kapi_fsize64 (h);
	if (lfile_reserve (d, (long long) sz + 1) != 0)
	{
		kapi_close (h);
		return -1;
	}
	unsigned long long got = 0;
	while (got < sz)
	{
		unsigned n = sz - got > (1U << 20) ? (1U << 20) : (unsigned) (sz - got);
		int r = kapi_read (h, d->buf + got, n);
		if (r <= 0)
			break;
		got += (unsigned) r;
	}
	kapi_close (h);
	d->size = (long long) got;
	return 0;
}

static int lfile_open (struct __onyx_ofd *d, int flags)
{
	int acc = flags & O_ACCMODE;
	int exists;
	struct stat st;
	exists = __onyx_path_stat (d->path, &st) == 0;
	if (exists && S_ISDIR (st.st_mode))
		return ONYX_ERR (EISDIR);
	if (!exists && !(flags & O_CREAT))
		return ONYX_ERR (ENOENT);
	if (exists && (flags & O_CREAT) && (flags & O_EXCL))
		return ONYX_ERR (EEXIST);
	d->type = ONYX_FD_LFILE;
	if (acc == O_RDONLY)
	{
		if (!exists && kapi_save_file (d->path, "", 0) < 0)	/* (O_CREAT: an empty file) */
			return ONYX_ERR (EACCES);
		d->lh = kapi_open (d->path);
		if (d->lh == 0)
			return ONYX_ERR (ENOENT);
		d->size = (long long) kapi_fsize64 (d->lh);
		d->kpos = 0;
		return 0;
	}
	d->whole = 1;
	if (exists && !(flags & O_TRUNC))
	{
		if (lfile_slurp (d) != 0)
			return -1;
	}
	if (!exists || (flags & O_TRUNC))
		d->dirty = 1;				/* (created / emptied: written at close) */
	/* a new file exists at once (O_EXCL of another open, stat), empty until written back */
	if (!exists && kapi_save_file (d->path, "", 0) < 0)
		return ONYX_ERR (EACCES);
	return 0;
}

int __onyx_lfile_flush (struct __onyx_ofd *d)
{
	if (d->type != ONYX_FD_LFILE || !d->whole || !d->dirty || d->unlinked)
		return 0;
	if (kapi_save_file (d->path, d->buf ? d->buf : (unsigned char *) "", (unsigned) d->size) < 0)
		return ONYX_ERR (EIO);
	d->dirty = 0;
	return 0;
}

static ssize_t lfile_pread (struct __onyx_ofd *d, void *buf, size_t n, long long off)
{
	if (off >= d->size)
		return 0;
	if ((long long) n > d->size - off)
		n = (size_t) (d->size - off);
	if (d->whole)
	{
		memcpy (buf, d->buf + off, n);
		return (ssize_t) n;
	}
	if (off != d->kpos)
	{
		if (kapi_seek (d->lh, (unsigned long long) off) != 0)
		{
			/* not seekable (an old kernel, a provider's file): hold it whole */
			kapi_close (d->lh);
			d->lh = 0;
			d->whole = 1;
			if (lfile_slurp (d) != 0)
				return -1;
			return lfile_pread (d, buf, n, off);
		}
		d->kpos = off;
	}
	size_t got = 0;
	while (got < n)
	{
		unsigned c = n - got > (1U << 20) ? (1U << 20) : (unsigned) (n - got);
		int r = kapi_read (d->lh, (char *) buf + got, c);
		if (r <= 0)
			break;
		got += (unsigned) r;
	}
	d->kpos += (long long) got;
	return (ssize_t) got;
}

static ssize_t lfile_pwrite (struct __onyx_ofd *d, const void *buf, size_t n, long long off)
{
	if (!d->whole)
		return ONYX_ERR (EBADF);
	if (lfile_reserve (d, off + (long long) n) != 0)
		return -1;
	if (off > d->size)
		memset (d->buf + d->size, 0, (size_t) (off - d->size));
	memcpy (d->buf + off, buf, n);
	if (off + (long long) n > d->size)
		d->size = off + (long long) n;
	d->dirty = 1;
	return (ssize_t) n;
}

/* ---- open ---- */
int __onyx_open (const char *path, int flags, int mode)
{
	if (path == 0)
		return ONYX_ERR (EFAULT);
	int cloexec = (flags & O_CLOEXEC) != 0;
	int acc = flags & O_ACCMODE;
	int keep = acc | (flags & (O_APPEND | O_NONBLOCK));

	int dev = __onyx_dev_type (path);
	if (dev)
	{
		struct __onyx_ofd *d;
		if (dev == ONYX_FD_CONSOLE)
		{
			int which = 1;
			if (strstr (path, "stdin") || strstr (path, "fd/0"))
				which = 0;
			else if (strstr (path, "stderr") || strstr (path, "fd/2"))
				which = 2;
			if (!strcmp (path, "/dev/tty") || !strcmp (path, "/dev/console"))
				which = acc == O_RDONLY ? 0 : 1;
			d = __onyx_ofd_new (ONYX_FD_CONSOLE, keep);
			if (d)
				d->console = which;
		}
		else
			d = __onyx_ofd_new (dev, keep);
		return d ? __onyx_fd_install (d, 0, cloexec) : -1;
	}

	char p[ONYX_PATH_MAX];
	if (__onyx_path (path, p, sizeof p) == 0)
		return -1;
	if (kapi__core () != 0)
		return ONYX_ERR (ENOSYS);

	if (flags & O_DIRECTORY)
	{
		if (!__onyx_path_is_dir (p))
			return -1;
		struct __onyx_ofd *d = __onyx_ofd_new (ONYX_FD_DIR, O_RDONLY);
		if (d == 0)
			return -1;
		d->path = strdup (p);
		return __onyx_fd_install (d, 0, cloexec);
	}

	unsigned kf = (unsigned) acc;
	if (flags & O_CREAT) kf |= KAPI_O_CREAT;
	if (flags & O_EXCL) kf |= KAPI_O_EXCL;
	if (flags & O_TRUNC) kf |= KAPI_O_TRUNC;
	if (flags & O_APPEND) kf |= KAPI_O_APPEND;
	long long h = kapi_file_open (p, kf, (unsigned) mode);
	if (h == -KAPI_EISDIR && acc == O_RDONLY)
	{
		struct __onyx_ofd *d = __onyx_ofd_new (ONYX_FD_DIR, O_RDONLY);
		if (d == 0)
			return -1;
		d->path = strdup (p);
		return __onyx_fd_install (d, 0, cloexec);
	}
	if (h == -KAPI_ENOSYS)
	{
		/* a kernel without v75 files: the old calls */
		struct __onyx_ofd *d = __onyx_ofd_new (ONYX_FD_LFILE, keep);
		if (d == 0)
			return -1;
		d->path = strdup (p);
		if (lfile_open (d, flags) != 0)
		{
			int e = errno;
			if (d->lh)
				kapi_close (d->lh);
			free (d->buf);
			free (d->path);
			free (d);
			errno = e;
			return -1;
		}
		return __onyx_fd_install (d, 0, cloexec);
	}
	if (h < 0)
	{
		errno = (int) -h;
		return -1;
	}
	struct __onyx_ofd *d = __onyx_ofd_new (ONYX_FD_FILE, keep);
	if (d == 0)
	{
		kapi_file_close (h);
		return -1;
	}
	d->h = h;
	d->kappend = (flags & O_APPEND) != 0;
	d->path = strdup (p);
	return __onyx_fd_install (d, 0, cloexec);
}

/* ---- read / write on a description ---- */
static ssize_t console_read (void *buf, size_t n)
{
	if (n > CHUNK_MAX)
		n = CHUNK_MAX;
	int r = kapi_stdin_read (buf, (unsigned) n);
	return r < 0 ? 0 : r;
}

static ssize_t console_write (const void *buf, size_t n)
{
	size_t done = 0;
	while (done < n)
	{
		unsigned c = n - done > CHUNK_MAX ? CHUNK_MAX : (unsigned) (n - done);
		int r = kapi_stdout_write ((const char *) buf + done, c);
		if (r <= 0)
			break;
		done += (unsigned) r;
	}
	return done == 0 && n != 0 ? ONYX_ERR (EIO) : (ssize_t) done;
}

static ssize_t random_fill (void *buf, size_t n)
{
	size_t done = 0;
	while (done < n)
	{
		unsigned c = n - done > 65536 ? 65536 : (unsigned) (n - done);
		int r = kapi_random ((char *) buf + done, c);
		if (r <= 0)
			return done ? (ssize_t) done : ONYX_ERR (EIO);
		done += (unsigned) r;
	}
	return (ssize_t) done;
}

static ssize_t pipe_read (struct __onyx_ofd *d, void *buf, size_t n)
{
	if (d->carry_n > d->carry_off)
		return (ssize_t) __onyx_carry_take (d, buf, n, 0);
	if (d->eof)
		return 0;
	if (n > CHUNK_MAX)
		n = CHUNK_MAX;
	void *h = d->type == ONYX_FD_STREAM ? d->stream : d->pipe->h;
	if (d->flags & O_NONBLOCK)
	{
		int r = kapi_stream_read_nb (h, buf, (unsigned) n);
		if (r < 0)
			return ONYX_ERR (EAGAIN);
		return r;
	}
	int r = kapi_stream_read (h, buf, (unsigned) n);
	return r < 0 ? ONYX_ERR (EIO) : r;
}

static ssize_t pipe_write (struct __onyx_ofd *d, const void *buf, size_t n)
{
	void *h = d->type == ONYX_FD_STREAM ? d->stream : d->pipe->h;
	if (d->pipe && d->pipe->readers == 0 && !d->pipe->child_reader)
		return ONYX_ERR (EPIPE);		/* (no SIGPIPE on Onyx) */
	size_t done = 0;
	while (done < n)
	{
		unsigned c = n - done > CHUNK_MAX ? CHUNK_MAX : (unsigned) (n - done);
		int r;
		if (d->flags & O_NONBLOCK)
		{
			r = kapi_stream_write_nb (h, (const char *) buf + done, c);
			if (r == -KAPI_ENOSYS)
				r = kapi_stream_write (h, (const char *) buf + done, c);
			else if (r == -KAPI_EAGAIN)
				return done ? (ssize_t) done : ONYX_ERR (EAGAIN);
		}
		else
			r = kapi_stream_write (h, (const char *) buf + done, c);
		if (r <= 0)
			return done ? (ssize_t) done : ONYX_ERR (r < 0 ? -r == KAPI_EBADF ? EBADF : EPIPE : EPIPE);
		done += (unsigned) r;
	}
	return (ssize_t) done;
}

ssize_t __onyx_read (struct __onyx_ofd *d, void *buf, size_t n)
{
	if ((d->flags & O_ACCMODE) == O_WRONLY && d->type != ONYX_FD_CONSOLE)
		return ONYX_ERR (EBADF);
	if (n == 0)
		return 0;
	switch (d->type)
	{
	case ONYX_FD_FILE:
		return (ssize_t) __onyx_sys (kapi_file_read (d->h, buf, n, -1));
	case ONYX_FD_LFILE:
	{
		__onyx_ofd_lock (d);
		ssize_t r = lfile_pread (d, buf, n, d->pos);
		if (r > 0)
			d->pos += r;
		__onyx_ofd_unlock (d);
		return r;
	}
	case ONYX_FD_DIR:
		return ONYX_ERR (EISDIR);
	case ONYX_FD_PIPE_R:
	case ONYX_FD_STREAM:
		return pipe_read (d, buf, n);
	case ONYX_FD_PIPE_W:
		return ONYX_ERR (EBADF);
	case ONYX_FD_SHM:				/* (v76: mmap it) */
		return ONYX_ERR (EINVAL);
	case ONYX_FD_SOCKET:
	case ONYX_FD_LSOCKET:
		return __onyx_sock_recv (d, buf, n, 0, 0);
	case ONYX_FD_NULL:
		return 0;
	case ONYX_FD_ZERO:
		memset (buf, 0, n);
		return (ssize_t) n;
	case ONYX_FD_RANDOM:
		return random_fill (buf, n);
	case ONYX_FD_CONSOLE:
		if (d->console != 0)
			return ONYX_ERR (EBADF);
		if (d->carry_n > d->carry_off)		/* (read ahead by poll) */
			return (ssize_t) __onyx_carry_take (d, buf, n, 0);
		if (d->eof)
			return 0;
		return console_read (buf, n);
	}
	return ONYX_ERR (EBADF);
}

ssize_t __onyx_write (struct __onyx_ofd *d, const void *buf, size_t n)
{
	if ((d->flags & O_ACCMODE) == O_RDONLY && d->type != ONYX_FD_CONSOLE &&
	    d->type != ONYX_FD_SOCKET && d->type != ONYX_FD_LSOCKET)
		return ONYX_ERR (EBADF);
	if (n == 0 && d->type != ONYX_FD_FILE)
		return 0;
	switch (d->type)
	{
	case ONYX_FD_FILE:
	{
		/* O_APPEND set later by fcntl: the kernel's handle was opened without it */
		if ((d->flags & O_APPEND) && !d->kappend)
			kapi_file_seek (d->h, 0, KAPI_SEEK_END);
		return (ssize_t) __onyx_sys (kapi_file_write (d->h, buf, n, -1));
	}
	case ONYX_FD_LFILE:
	{
		__onyx_ofd_lock (d);
		if (d->flags & O_APPEND)
			d->pos = d->size;
		ssize_t r = lfile_pwrite (d, buf, n, d->pos);
		if (r > 0)
			d->pos += r;
		__onyx_ofd_unlock (d);
		return r;
	}
	case ONYX_FD_DIR:
		return ONYX_ERR (EISDIR);
	case ONYX_FD_PIPE_W:
	case ONYX_FD_STREAM:
		return pipe_write (d, buf, n);
	case ONYX_FD_PIPE_R:
		return ONYX_ERR (EBADF);
	case ONYX_FD_SHM:				/* (v76: mmap it) */
		return ONYX_ERR (EINVAL);
	case ONYX_FD_SOCKET:
	case ONYX_FD_LSOCKET:
		return __onyx_sock_send (d, buf, n, 0, 0);
	case ONYX_FD_NULL:
	case ONYX_FD_ZERO:
	case ONYX_FD_RANDOM:
		return (ssize_t) n;
	case ONYX_FD_CONSOLE:
		if (d->console == 0)
			return ONYX_ERR (EBADF);
		return console_write (buf, n);
	}
	return ONYX_ERR (EBADF);
}

ssize_t __onyx_pread (struct __onyx_ofd *d, void *buf, size_t n, off_t off)
{
	if (off < 0)
		return ONYX_ERR (EINVAL);
	if (d->type == ONYX_FD_FILE)
		return (ssize_t) __onyx_sys (kapi_file_read (d->h, buf, n, (long long) off));
	if (d->type == ONYX_FD_LFILE)
	{
		__onyx_ofd_lock (d);
		ssize_t r = lfile_pread (d, buf, n, off);
		__onyx_ofd_unlock (d);
		return r;
	}
	if (d->type == ONYX_FD_ZERO || d->type == ONYX_FD_NULL || d->type == ONYX_FD_RANDOM)
		return __onyx_read (d, buf, n);
	return ONYX_ERR (d->type == ONYX_FD_DIR ? EISDIR : ESPIPE);
}

ssize_t __onyx_pwrite (struct __onyx_ofd *d, const void *buf, size_t n, off_t off)
{
	if (off < 0)
		return ONYX_ERR (EINVAL);
	if ((d->flags & O_ACCMODE) == O_RDONLY)
		return ONYX_ERR (EBADF);
	if (d->type == ONYX_FD_FILE)
		return (ssize_t) __onyx_sys (kapi_file_write (d->h, buf, n, (long long) off));
	if (d->type == ONYX_FD_LFILE)
	{
		__onyx_ofd_lock (d);
		ssize_t r = lfile_pwrite (d, buf, n, off);
		__onyx_ofd_unlock (d);
		return r;
	}
	if (d->type == ONYX_FD_ZERO || d->type == ONYX_FD_NULL)
		return (ssize_t) n;
	return ONYX_ERR (d->type == ONYX_FD_DIR ? EISDIR : ESPIPE);
}

off_t __onyx_lseek (struct __onyx_ofd *d, off_t off, int whence)
{
	if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END)
		return ONYX_ERR (EINVAL);
	switch (d->type)
	{
	case ONYX_FD_FILE:
		return (off_t) __onyx_sys (kapi_file_seek (d->h, (long long) off, whence));
	case ONYX_FD_LFILE:
	{
		__onyx_ofd_lock (d);
		long long base = whence == SEEK_CUR ? d->pos : whence == SEEK_END ? d->size : 0;
		long long np = base + (long long) off;
		off_t r;
		if (np < 0)
			r = ONYX_ERR (EINVAL);
		else
			r = (off_t) (d->pos = np);
		__onyx_ofd_unlock (d);
		return r;
	}
	case ONYX_FD_NULL:
	case ONYX_FD_ZERO:
	case ONYX_FD_RANDOM:
	case ONYX_FD_DIR:
		return 0;
	}
	return ONYX_ERR (ESPIPE);
}

int __onyx_ofd_release (struct __onyx_ofd *d)
{
	int r = 0;
	switch (d->type)
	{
	case ONYX_FD_FILE:
		r = (int) __onyx_sys (kapi_file_close (d->h));
		break;
	case ONYX_FD_LFILE:
		r = __onyx_lfile_flush (d);
		if (d->lh)
			kapi_close (d->lh);
		break;
	case ONYX_FD_PIPE_R:
	case ONYX_FD_PIPE_W:
	{
		struct __onyx_pipe *p = d->pipe;
		if (p->remote)				/* (v76: an end received: its handle is its own) */
		{
			if (d->type == ONYX_FD_PIPE_W)
				kapi_stream_eof (p->h);	/* (one writer less: the kernel counts them) */
			kapi_stream_close (p->h);
			free (p);
			break;
		}
		int readers, writers;
		if (d->type == ONYX_FD_PIPE_R)
			readers = __atomic_sub_fetch (&p->readers, 1, __ATOMIC_ACQ_REL), writers = p->writers;
		else
			writers = __atomic_sub_fetch (&p->writers, 1, __ATOMIC_ACQ_REL), readers = p->readers;
		if (writers == 0 && !p->eof_sent && !p->child_writer)
		{
			p->eof_sent = 1;
			kapi_stream_eof (p->h);		/* the readers see the end */
		}
		if (readers == 0 && writers == 0)
		{
			kapi_stream_close (p->h);
			free (p);
		}
		break;
	}
	case ONYX_FD_STREAM:
		kapi_stream_close (d->stream);
		break;
	case ONYX_FD_SOCKET:
	case ONYX_FD_LSOCKET:
		r = __onyx_sock_close (d);
		break;
	case ONYX_FD_SHM:				/* (v76: its mappings keep the object) */
		r = (int) __onyx_sys (kapi_handle_close (d->h));
		break;
	default:
		break;
	}
	free (d->buf);
	free (d->carry);
	free (d->path);
	free (d);
	return r;
}

/* ---- the POSIX calls ---- */
ssize_t pread (int fd, void *buf, size_t n, off_t off)
{
	struct __onyx_ofd *d = __onyx_fd_get (fd);
	if (d == 0)
		return -1;
	ssize_t r = __onyx_pread (d, buf, n, off);
	__onyx_fd_put (d);
	return r;
}

ssize_t pwrite (int fd, const void *buf, size_t n, off_t off)
{
	struct __onyx_ofd *d = __onyx_fd_get (fd);
	if (d == 0)
		return -1;
	ssize_t r = __onyx_pwrite (d, buf, n, off);
	__onyx_fd_put (d);
	return r;
}

ssize_t readv (int fd, const struct iovec *iov, int cnt)
{
	if (cnt < 0 || cnt > IOV_MAX)
		return ONYX_ERR (EINVAL);
	ssize_t total = 0;
	for (int i = 0; i < cnt; i++)
	{
		if (iov[i].iov_len == 0)
			continue;
		ssize_t r = read (fd, iov[i].iov_base, iov[i].iov_len);
		if (r < 0)
			return total ? total : -1;
		total += r;
		if ((size_t) r < iov[i].iov_len)
			break;
	}
	return total;
}

ssize_t writev (int fd, const struct iovec *iov, int cnt)
{
	if (cnt < 0 || cnt > IOV_MAX)
		return ONYX_ERR (EINVAL);
	ssize_t total = 0;
	for (int i = 0; i < cnt; i++)
	{
		if (iov[i].iov_len == 0)
			continue;
		ssize_t r = write (fd, iov[i].iov_base, iov[i].iov_len);
		if (r < 0)
			return total ? total : -1;
		total += r;
		if ((size_t) r < iov[i].iov_len)
			break;
	}
	return total;
}

ssize_t preadv (int fd, const struct iovec *iov, int cnt, off_t off)
{
	ssize_t total = 0;
	for (int i = 0; i < cnt; i++)
	{
		ssize_t r = pread (fd, iov[i].iov_base, iov[i].iov_len, off + total);
		if (r < 0)
			return total ? total : -1;
		total += r;
		if ((size_t) r < iov[i].iov_len)
			break;
	}
	return total;
}

ssize_t pwritev (int fd, const struct iovec *iov, int cnt, off_t off)
{
	ssize_t total = 0;
	for (int i = 0; i < cnt; i++)
	{
		ssize_t r = pwrite (fd, iov[i].iov_base, iov[i].iov_len, off + total);
		if (r < 0)
			return total ? total : -1;
		total += r;
		if ((size_t) r < iov[i].iov_len)
			break;
	}
	return total;
}

int ftruncate (int fd, off_t len)
{
	if (len < 0)
		return ONYX_ERR (EINVAL);
	struct __onyx_ofd *d = __onyx_fd_get (fd);
	if (d == 0)
		return -1;
	int r;
	if ((d->flags & O_ACCMODE) == O_RDONLY)
		r = ONYX_ERR (EBADF);
	else if (d->type == ONYX_FD_FILE)
		r = (int) __onyx_sys (kapi_file_truncate (d->h, (long long) len));
	else if (d->type == ONYX_FD_SHM)		/* (v76) */
		r = (int) __onyx_sys (kapi_shm_ctl (d->h, KAPI_SHM_SET_SIZE, (unsigned long long) len));
	else if (d->type == ONYX_FD_LFILE && d->whole)
	{
		__onyx_ofd_lock (d);
		r = 0;
		if (len > d->size)
		{
			if (lfile_reserve (d, len) == 0)
				memset (d->buf + d->size, 0, (size_t) (len - d->size));
			else
				r = -1;
		}
		if (r == 0)
		{
			d->size = len;
			d->dirty = 1;
		}
		__onyx_ofd_unlock (d);
	}
	else
		r = ONYX_ERR (EINVAL);
	__onyx_fd_put (d);
	return r;
}

int truncate (const char *path, off_t len)
{
	int fd = __onyx_open (path, O_WRONLY, 0);
	if (fd < 0)
		return -1;
	int r = ftruncate (fd, len);
	int e = errno;
	close (fd);
	errno = e;
	return r;
}

int fsync (int fd)
{
	struct __onyx_ofd *d = __onyx_fd_get (fd);
	if (d == 0)
		return -1;
	int r = 0;
	if (d->type == ONYX_FD_FILE)
		r = (int) __onyx_sys (kapi_file_sync (d->h));
	else if (d->type == ONYX_FD_LFILE)
	{
		__onyx_ofd_lock (d);
		r = __onyx_lfile_flush (d);
		__onyx_ofd_unlock (d);
	}
	else if (d->type == ONYX_FD_SOCKET || d->type == ONYX_FD_LSOCKET || d->type == ONYX_FD_PIPE_R ||
		 d->type == ONYX_FD_PIPE_W)
		r = ONYX_ERR (EINVAL);
	__onyx_fd_put (d);
	return r;
}

int fdatasync (int fd) { return fsync (fd); }
void sync (void) { }

int posix_fallocate (int fd, off_t off, off_t len)
{
	struct stat st;
	if (off < 0 || len <= 0)
		return EINVAL;
	if (fstat (fd, &st) != 0)
		return errno;
	if (st.st_size >= off + len)
		return 0;
	return ftruncate (fd, off + len) == 0 ? 0 : errno;
}

int posix_fadvise (int fd, off_t off, off_t len, int advice)
{
	(void) off; (void) len; (void) advice;
	struct __onyx_ofd *d = __onyx_fd_get (fd);
	if (d == 0)
		return EBADF;
	__onyx_fd_put (d);
	return 0;
}

int creat (const char *path, mode_t mode) { return __onyx_open (path, O_WRONLY | O_CREAT | O_TRUNC, (int) mode); }

int openat (int dirfd, const char *path, int flags, ...)
{
	int mode = 0;
	if (flags & O_CREAT)
	{
		__builtin_va_list ap;
		__builtin_va_start (ap, flags);
		mode = __builtin_va_arg (ap, int);
		__builtin_va_end (ap);
	}
	char p[ONYX_PATH_MAX];
	if (__onyx_at_path (dirfd, path, p, sizeof p) == 0)
		return -1;
	return __onyx_open (p, flags, mode);
}
