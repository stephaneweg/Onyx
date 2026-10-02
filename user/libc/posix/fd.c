/*
 * fd.c -- the descriptor table (libonyxposix, docs/POSIX-PLAN.md §3.4).
 *
 * fds[0..1023] -> reference-counted open-file descriptions (struct __onyx_ofd), each over one
 * kernel object: a v75 file handle, a directory's path, a pipe end, a stream, a socket, a /dev
 * pseudo file, the console. dup / dup2 / dup3 / F_DUPFD share a description (and so its offset:
 * the kernel keeps the offset per handle, one handle per description); FD_CLOEXEC is per
 * descriptor (kept; there is no exec). A description is released -- its kernel object closed --
 * when its last descriptor closes AND no call is using it (__onyx_fd_get takes a reference, so
 * a close from another thread does not pull it from under a read in progress).
 *
 * fd 0 is the task's stdin (kapi stdin_read), 1 and 2 its stdout (kapi stdout_write) -- what a
 * terminal or a shell pipe reads.
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
#include <strings.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include "posix_internal.h"

static struct
{
	struct __onyx_ofd *d;
	int cloexec;
} s_fd[ONYX_FD_MAX];
static volatile unsigned s_fdLock;
static int s_inited;

void __onyx_ofd_lock (struct __onyx_ofd *d) { __onyx_lock (&d->lock); }
void __onyx_ofd_unlock (struct __onyx_ofd *d) { __onyx_unlock (&d->lock); }

struct __onyx_ofd *__onyx_ofd_new (int type, int flags)
{
	struct __onyx_ofd *d = (struct __onyx_ofd *) calloc (1, sizeof *d);
	if (d == 0)
	{
		errno = ENOMEM;
		return 0;
	}
	d->type = type;
	d->refs = 1;
	d->flags = flags;
	return d;
}

static struct __onyx_ofd *console (int which)
{
	struct __onyx_ofd *d = __onyx_ofd_new (ONYX_FD_CONSOLE, which == 0 ? O_RDONLY : O_WRONLY);
	if (d)
		d->console = which;
	return d;
}

void __onyx_fd_init (void)
{
	if (s_inited)
		return;
	s_inited = 1;
	for (int i = 0; i < 3; i++)
		if (s_fd[i].d == 0)
			s_fd[i].d = console (i);
}

struct __onyx_ofd *__onyx_fd_get (int fd)
{
	if (!s_inited)
		__onyx_fd_init ();
	if (fd < 0 || fd >= ONYX_FD_MAX)
	{
		errno = EBADF;
		return 0;
	}
	__onyx_lock (&s_fdLock);
	struct __onyx_ofd *d = s_fd[fd].d;
	if (d)
		__atomic_add_fetch (&d->refs, 1, __ATOMIC_ACQ_REL);
	__onyx_unlock (&s_fdLock);
	if (d == 0)
		errno = EBADF;
	return d;
}

void __onyx_fd_put (struct __onyx_ofd *d)
{
	if (d && __atomic_sub_fetch (&d->refs, 1, __ATOMIC_ACQ_REL) == 0)
		__onyx_ofd_release (d);
}

int __onyx_fd_install (struct __onyx_ofd *d, int min, int cloexec)
{
	if (!s_inited)
		__onyx_fd_init ();
	if (min < 0)
		min = 0;
	__onyx_lock (&s_fdLock);
	for (int i = min; i < ONYX_FD_MAX; i++)
		if (s_fd[i].d == 0)
		{
			s_fd[i].d = d;
			s_fd[i].cloexec = cloexec;
			__onyx_unlock (&s_fdLock);
			return i;
		}
	__onyx_unlock (&s_fdLock);
	__onyx_fd_put (d);
	errno = EMFILE;
	return -1;
}

int __onyx_fd_close (int fd)
{
	if (!s_inited)
		__onyx_fd_init ();
	if (fd < 0 || fd >= ONYX_FD_MAX)
		return ONYX_ERR (EBADF);
	__onyx_lock (&s_fdLock);
	struct __onyx_ofd *d = s_fd[fd].d;
	s_fd[fd].d = 0;
	s_fd[fd].cloexec = 0;
	__onyx_unlock (&s_fdLock);
	if (d == 0)
		return ONYX_ERR (EBADF);
	int r = 0;
	if (__atomic_sub_fetch (&d->refs, 1, __ATOMIC_ACQ_REL) == 0)
		r = __onyx_ofd_release (d);
	return r;
}

int __onyx_fd_cloexec (int fd, int set)
{
	if (fd < 0 || fd >= ONYX_FD_MAX)
		return ONYX_ERR (EBADF);
	__onyx_lock (&s_fdLock);
	int r = -1;
	if (s_fd[fd].d)
	{
		r = s_fd[fd].cloexec;
		if (set >= 0)
			s_fd[fd].cloexec = set;
	}
	__onyx_unlock (&s_fdLock);
	if (r < 0)
		errno = EBADF;
	return r;
}

/* The old calls (LFILE): a path unlinked while descriptions hold it for writing -- they must not
 * write it back at their close (the file stays readable through them until then). */
int __onyx_lfile_unlinked (const char *path)
{
	char a[ONYX_PATH_MAX], b[ONYX_PATH_MAX];
	if (__onyx_abspath (path, a, sizeof a) != 0)
		return 0;
	int n = 0;
	__onyx_lock (&s_fdLock);
	for (int i = 0; i < ONYX_FD_MAX; i++)
	{
		struct __onyx_ofd *d = s_fd[i].d;
		if (d && d->type == ONYX_FD_LFILE && d->whole && !d->unlinked && d->path &&
		    __onyx_abspath (d->path, b, sizeof b) == 0 && strcasecmp (a, b) == 0)
		{
			d->unlinked = 1;
			n++;
		}
	}
	__onyx_unlock (&s_fdLock);
	return n;
}

/* ---- dup ---- */
int dup (int fd)
{
	struct __onyx_ofd *d = __onyx_fd_get (fd);
	if (d == 0)
		return -1;
	return __onyx_fd_install (d, 0, 0);		/* (the reference moves to the new fd) */
}

int dup3 (int fd, int fd2, int flags)
{
	if (fd2 < 0 || fd2 >= ONYX_FD_MAX)
		return ONYX_ERR (EBADF);
	if (fd == fd2)
		return ONYX_ERR (EINVAL);
	struct __onyx_ofd *d = __onyx_fd_get (fd);
	if (d == 0)
		return -1;
	__onyx_lock (&s_fdLock);
	struct __onyx_ofd *old = s_fd[fd2].d;
	s_fd[fd2].d = d;
	s_fd[fd2].cloexec = (flags & O_CLOEXEC) != 0;
	__onyx_unlock (&s_fdLock);
	if (old)
		__onyx_fd_put (old);
	return fd2;
}

int dup2 (int fd, int fd2)
{
	if (fd == fd2)
	{
		struct __onyx_ofd *d = __onyx_fd_get (fd);
		if (d == 0)
			return -1;
		__onyx_fd_put (d);
		return fd2;
	}
	return dup3 (fd, fd2, 0);
}

/* ---- fcntl ---- */
int fcntl (int fd, int cmd, ...)
{
	va_list ap;
	va_start (ap, cmd);
	long arg = va_arg (ap, long);
	va_end (ap);

	struct __onyx_ofd *d = __onyx_fd_get (fd);
	if (d == 0)
		return -1;
	int r = 0;
	switch (cmd)
	{
	case F_DUPFD:
	case F_DUPFD_CLOEXEC:
		__atomic_add_fetch (&d->refs, 1, __ATOMIC_ACQ_REL);
		r = __onyx_fd_install (d, (int) arg, cmd == F_DUPFD_CLOEXEC);
		break;
	case F_GETFD:
		r = __onyx_fd_cloexec (fd, -1) > 0 ? FD_CLOEXEC : 0;
		break;
	case F_SETFD:
		__onyx_fd_cloexec (fd, (arg & FD_CLOEXEC) != 0);
		break;
	case F_GETFL:
		r = d->flags;
		break;
	case F_SETFL:
	{
		int keep = d->flags & ~(O_APPEND | O_NONBLOCK);
		int nb = (arg & O_NONBLOCK) != 0;
		if (nb != ((d->flags & O_NONBLOCK) != 0) &&
		    (d->type == ONYX_FD_SOCKET || d->type == ONYX_FD_LSOCKET))
			__onyx_sock_set_nonblock (d, nb);
		d->flags = keep | (int) (arg & (O_APPEND | O_NONBLOCK));
		break;
	}
	case F_GETLK:
	{
		struct flock *fl = (struct flock *) arg;
		if (fl)
			fl->l_type = F_UNLCK;		/* no lock manager: nothing ever conflicts */
		break;
	}
	case F_SETLK:
	case F_SETLKW:
		break;
	case F_GETOWN:
	case F_SETOWN:
		break;
	default:
		r = ONYX_ERR (EINVAL);
	}
	__onyx_fd_put (d);
	return r;
}

int _fcntl (int fd, int cmd, int arg) { return fcntl (fd, cmd, (long) arg); }

int flock (int fd, int op)
{
	(void) op;
	struct __onyx_ofd *d = __onyx_fd_get (fd);
	if (d == 0)
		return -1;
	__onyx_fd_put (d);
	return 0;
}

int lockf (int fd, int cmd, off_t len)
{
	(void) cmd; (void) len;
	return flock (fd, 0);
}

int getdtablesize (void) { return ONYX_FD_MAX; }

/* ---- pipes ---- */
int pipe2 (int fds[2], int flags)
{
	if (kapi__core () != 0)
		return ONYX_ERR (ENOSYS);
	void *h = kapi_pipe ();
	if (h == 0)
		return ONYX_ERR (ENFILE);
	struct __onyx_pipe *p = (struct __onyx_pipe *) calloc (1, sizeof *p);
	struct __onyx_ofd *r = __onyx_ofd_new (ONYX_FD_PIPE_R, O_RDONLY | (flags & O_NONBLOCK));
	struct __onyx_ofd *w = __onyx_ofd_new (ONYX_FD_PIPE_W, O_WRONLY | (flags & O_NONBLOCK));
	if (p == 0 || r == 0 || w == 0)
	{
		free (p);
		free (r);
		free (w);
		kapi_stream_close (h);
		return ONYX_ERR (ENOMEM);
	}
	p->h = h;
	p->readers = 1;
	p->writers = 1;
	r->pipe = p;
	w->pipe = p;
	r->stream = h;
	w->stream = h;
	int a = __onyx_fd_install (r, 0, (flags & O_CLOEXEC) != 0);
	if (a < 0)
	{
		__onyx_fd_put (w);
		return -1;
	}
	int b = __onyx_fd_install (w, 0, (flags & O_CLOEXEC) != 0);
	if (b < 0)
	{
		__onyx_fd_close (a);
		return -1;
	}
	fds[0] = a;
	fds[1] = b;
	return 0;
}

int pipe (int fds[2]) { return pipe2 (fds, 0); }

int mkfifo (const char *path, mode_t mode) { (void) path; (void) mode; return ONYX_ERR (ENOSYS); }
int mknod (const char *path, mode_t mode, dev_t dev) { (void) path; (void) mode; (void) dev; return ONYX_ERR (EPERM); }
