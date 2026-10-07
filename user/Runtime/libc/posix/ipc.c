/*
 * ipc.c -- IPC between processes (libonyxposix, kapi v76, docs/POSIX-PLAN.md §14): socketpair
 * (AF_UNIX: SOCK_STREAM / SOCK_SEQPACKET / SOCK_DGRAM local sockets), sendmsg / recvmsg on them with
 * SCM_RIGHTS, memfd_create / shm_open / shm_unlink (shared memory objects; mmap is mman.c's), and
 * the descriptors a spawned child is given (posix_spawn: proc.c; the child: __onyx_fd_inherit).
 *
 * A descriptor is passed as its kernel handle plus a tag: the description's type, its O_* flags and
 * its socket type ((sotype << 24) | (flags << 8) | type), which the receiver turns back into a
 * description of the same kind: a file (the open-file description shared, its offset too), a pipe
 * end (a pipe of its own over the same kernel stream: the end-of-file is still each process's
 * last write end closing), a stream, a local or an IP socket (the IP socket then belongs to the
 * receiver), a shared memory object. Not passable: directories, /dev/null & co, the old-call
 * files and sockets (EOPNOTSUPP).
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
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/mman.h>
#include "posix_internal.h"

#define TAG_KAPPEND	0x8000		/* in the flags' bits: the kernel handle was opened O_APPEND */

/* ---- descriptions <-> handles ---- */
int __onyx_ofd_xfer (struct __onyx_ofd *d, struct kapi_handle_xfer *x)
{
	memset (x, 0, sizeof *x);
	unsigned flags = (unsigned) d->flags & 0x7FFF;
	if (d->type == ONYX_FD_FILE && d->kappend)
		flags |= TAG_KAPPEND;
	x->tag = (unsigned) d->type | (flags << 8) | ((unsigned) (d->sotype & 0xFF) << 24);
	switch (d->type)
	{
	case ONYX_FD_FILE:
		x->kind = KAPI_HK_OFILE;
		x->h = d->h;
		return 0;
	case ONYX_FD_PIPE_R:
	case ONYX_FD_PIPE_W:
		x->kind = KAPI_HK_STREAM;
		x->h = (long long) (unsigned long) d->pipe->h;
		if (d->type == ONYX_FD_PIPE_W)
			x->flags = KAPI_HXF_WRITER;	/* (the pipe's end waits for this writer too) */
		return 0;
	case ONYX_FD_STREAM:
		x->kind = KAPI_HK_STREAM;
		x->h = (long long) (unsigned long) d->stream;
		return 0;
	case ONYX_FD_SOCKET:
		x->kind = d->h >= KAPI_SOCK_LOCAL_BASE ? KAPI_HK_LSOCK : KAPI_HK_SOCKET;
		x->h = d->h;
		return 0;
	case ONYX_FD_SHM:
		x->kind = KAPI_HK_SHM;
		x->h = d->h;
		return 0;
	default:
		return ONYX_ERR (EOPNOTSUPP);
	}
}

struct __onyx_ofd *__onyx_ofd_from_xfer (const struct kapi_handle_xfer *x)
{
	int type = (int) (x->tag & 0xFF);
	int flags = (int) ((x->tag >> 8) & 0x7FFF);
	int sotype = (int) (x->tag >> 24);
	struct __onyx_ofd *d = 0;
	switch (x->kind)
	{
	case KAPI_HK_OFILE:
		d = __onyx_ofd_new (ONYX_FD_FILE, flags);
		if (d)
		{
			d->h = x->h;
			d->kappend = (x->tag >> 8) & TAG_KAPPEND ? 1 : 0;
		}
		break;
	case KAPI_HK_STREAM:
	{
		void *h = (void *) (unsigned long) x->h;
		if (type == ONYX_FD_PIPE_R || type == ONYX_FD_PIPE_W)
		{
			struct __onyx_pipe *p = (struct __onyx_pipe *) calloc (1, sizeof *p);
			d = p ? __onyx_ofd_new (type, flags) : 0;
			if (d == 0)
			{
				free (p);
				break;
			}
			p->h = h;
			p->remote = 1;			/* (the other end is elsewhere: no EPIPE here) */
			p->readers = 1;
			p->writers = 1;
			d->pipe = p;
			d->stream = h;
		}
		else if ((d = __onyx_ofd_new (ONYX_FD_STREAM, flags)) != 0)
			d->stream = h;
		break;
	}
	case KAPI_HK_SOCKET:
	case KAPI_HK_LSOCK:
		d = __onyx_ofd_new (ONYX_FD_SOCKET, flags ? flags : O_RDWR);
		if (d)
		{
			d->h = x->h;
			d->sotype = sotype ? sotype : SOCK_STREAM;
			d->connected = 1;
		}
		break;
	case KAPI_HK_SHM:
		d = __onyx_ofd_new (ONYX_FD_SHM, flags ? flags : O_RDWR);
		if (d)
			d->h = x->h;
		break;
	default:
		errno = EBADF;
		return 0;
	}
	if (d == 0)					/* (out of memory: the handle closed) */
	{
		if (x->kind == KAPI_HK_SOCKET)
			kapi_sock_close ((int) x->h);
		else
			kapi_handle_close (x->h);
		errno = ENOMEM;
	}
	return d;
}

/* ---- the child's side of posix_spawn ---- */
void __onyx_fd_inherit (void)
{
	struct kapi_handle_xfer x[64];
	int n = kapi_get_handles (x, 64);
	if (n <= 0)
		return;
	struct kapi_handle_xfer *all = x;
	if (n > 64 && (all = (struct kapi_handle_xfer *) malloc ((size_t) n * sizeof *all)) != 0)
		n = kapi_get_handles (all, (unsigned) n);
	else if (n > 64)
		n = 64;
	for (int i = 0; i < n; i++)
	{
		if (all[i].kind == KAPI_HK_NONE || all[i].h < 0)
			continue;
		struct __onyx_ofd *d = __onyx_ofd_from_xfer (&all[i]);
		if (d)
			__onyx_fd_place (d, all[i].fd, 0);
	}
	if (all != x)
		free (all);
}

/* ---- socketpair ---- */
int socketpair (int domain, int type, int proto, int sv[2])
{
	(void) proto;
	if (domain != AF_UNIX)
		return ONYX_ERR (domain == AF_INET || domain == AF_INET6 ? EOPNOTSUPP : EAFNOSUPPORT);
	int base = type & 0xF;
	int nb = (type & SOCK_NONBLOCK) != 0, ce = (type & SOCK_CLOEXEC) != 0;
	int kt = base == SOCK_STREAM ? KAPI_SOCK_STREAM : base == SOCK_SEQPACKET ? KAPI_SOCK_SEQPACKET
	       : base == SOCK_DGRAM ? KAPI_SOCK_DGRAM : 0;
	if (kt == 0)
		return ONYX_ERR (EPROTONOSUPPORT);
	if (kapi__core () != 0)
		return ONYX_ERR (ENOSYS);
	int k[2];
	int r = kapi_sock_pair (kt, nb ? KAPI_SOCKF_NONBLOCK : 0, k);
	if (r == -KAPI_ENOSYS)
		return ONYX_ERR (EOPNOTSUPP);		/* (a kernel before v76) */
	if (r < 0)
		return ONYX_ERR (-r);
	int fd[2] = { -1, -1 };
	for (int i = 0; i < 2; i++)
	{
		struct __onyx_ofd *d = __onyx_ofd_new (ONYX_FD_SOCKET, O_RDWR | (nb ? O_NONBLOCK : 0));
		if (d == 0)
		{
			kapi_sock_close (k[i]);
			if (i == 0)
				kapi_sock_close (k[1]);
			else
				close (fd[0]);
			return -1;
		}
		d->h = k[i];
		d->sotype = base;
		d->connected = 1;
		fd[i] = __onyx_fd_install (d, 0, ce);
		if (fd[i] < 0)
		{
			if (i == 0)
				kapi_sock_close (k[1]);
			else
				close (fd[0]);
			return -1;
		}
	}
	sv[0] = fd[0];
	sv[1] = fd[1];
	return 0;
}

/* ---- sendmsg / recvmsg on a local socket ---- */
#define KMSG_MASK	(MSG_PEEK | MSG_DONTWAIT | MSG_WAITALL)	/* = KAPI_MSG_* */

ssize_t __onyx_local_sendmsg (struct __onyx_ofd *d, const struct msghdr *m, int flags)
{
	/* the descriptors of the SCM_RIGHTS blocks */
	struct kapi_handle_xfer *x = 0;
	unsigned nx = 0;
	if (m->msg_control && m->msg_controllen >= sizeof (struct cmsghdr))
	{
		for (struct cmsghdr *c = CMSG_FIRSTHDR (m); c; c = CMSG_NXTHDR (m, c))
		{
			if (c->cmsg_level != SOL_SOCKET)
				continue;
			if (c->cmsg_type != SCM_RIGHTS)
				continue;			/* (SCM_CREDENTIALS: ignored) */
			if (c->cmsg_len < CMSG_LEN (0))
			{
				free (x);
				return ONYX_ERR (EINVAL);
			}
			unsigned n = (unsigned) ((c->cmsg_len - CMSG_LEN (0)) / sizeof (int));
			if (nx + n > KAPI_IPC_HANDLES_MAX)
			{
				free (x);
				return ONYX_ERR (ETOOMANYREFS);
			}
			struct kapi_handle_xfer *nxp = (struct kapi_handle_xfer *) realloc (x, (nx + n + 1) * sizeof *x);
			if (nxp == 0)
			{
				free (x);
				return ONYX_ERR (ENOMEM);
			}
			x = nxp;
			const int *fds = (const int *) CMSG_DATA (c);
			for (unsigned i = 0; i < n; i++)
			{
				struct __onyx_ofd *o = __onyx_fd_get (fds[i]);
				int r = o ? __onyx_ofd_xfer (o, &x[nx]) : -1;
				if (o)
					__onyx_fd_put (o);
				if (r < 0)
				{
					free (x);
					return -1;		/* (EBADF / EOPNOTSUPP) */
				}
				nx++;
			}
		}
	}
	/* the iovecs (more than the kernel takes: gathered here) */
	struct kapi_iovec kio[KAPI_IPC_IOV_MAX];
	char *flat = 0;
	unsigned niov = (unsigned) m->msg_iovlen;
	if (niov > KAPI_IPC_IOV_MAX)
	{
		size_t total = 0;
		for (unsigned i = 0; i < niov; i++)
			total += m->msg_iov[i].iov_len;
		flat = (char *) malloc (total ? total : 1);
		if (flat == 0)
		{
			free (x);
			return ONYX_ERR (ENOMEM);
		}
		size_t o = 0;
		for (unsigned i = 0; i < niov; i++)
		{
			memcpy (flat + o, m->msg_iov[i].iov_base, m->msg_iov[i].iov_len);
			o += m->msg_iov[i].iov_len;
		}
		kio[0].base = (unsigned long long) (unsigned long) flat;
		kio[0].len = total;
		niov = 1;
	}
	else
		for (unsigned i = 0; i < niov; i++)
		{
			kio[i].base = (unsigned long long) (unsigned long) m->msg_iov[i].iov_base;
			kio[i].len = m->msg_iov[i].iov_len;
		}
	struct kapi_msghdr k;
	memset (&k, 0, sizeof k);
	k.iov = kio;
	k.iovcnt = niov;
	k.handles = x;
	k.nhandles = nx;
	long long r = kapi_sock_sendmsg ((int) d->h, &k, (unsigned) (flags & KMSG_MASK));
	free (flat);
	free (x);
	return r < 0 ? ONYX_ERR ((int) -r) : (ssize_t) r;
}

ssize_t __onyx_local_recvmsg (struct __onyx_ofd *d, struct msghdr *m, int flags)
{
	unsigned cap = 0;
	if (m->msg_control && m->msg_controllen >= CMSG_LEN (sizeof (int)))
		cap = (unsigned) ((m->msg_controllen - CMSG_LEN (0)) / sizeof (int));
	if (cap > KAPI_IPC_HANDLES_MAX)
		cap = KAPI_IPC_HANDLES_MAX;
	struct kapi_handle_xfer *x = cap ? (struct kapi_handle_xfer *) calloc (cap, sizeof *x) : 0;
	if (cap && x == 0)
		return ONYX_ERR (ENOMEM);
	struct kapi_iovec kio[KAPI_IPC_IOV_MAX];
	unsigned niov = (unsigned) m->msg_iovlen;
	char *flat = 0;
	size_t total = 0;
	for (unsigned i = 0; i < niov; i++)
		total += m->msg_iov[i].iov_len;
	if (niov > KAPI_IPC_IOV_MAX)		/* (more than the kernel takes: scattered here) */
	{
		flat = (char *) malloc (total ? total : 1);
		if (flat == 0)
		{
			free (x);
			return ONYX_ERR (ENOMEM);
		}
		kio[0].base = (unsigned long long) (unsigned long) flat;
		kio[0].len = total;
	}
	else
		for (unsigned i = 0; i < niov; i++)
		{
			kio[i].base = (unsigned long long) (unsigned long) m->msg_iov[i].iov_base;
			kio[i].len = m->msg_iov[i].iov_len;
		}
	struct kapi_msghdr k;
	memset (&k, 0, sizeof k);
	k.iov = kio;
	k.iovcnt = flat ? 1 : niov;
	k.handles = x;
	k.nhandles = cap;
	long long r = kapi_sock_recvmsg ((int) d->h, &k, (unsigned) (flags & KMSG_MASK));
	if (r < 0)
	{
		free (flat);
		free (x);
		return ONYX_ERR ((int) -r);
	}
	if (flat)
	{
		size_t o = 0;
		for (unsigned i = 0; i < niov && o < (size_t) r; i++)
		{
			size_t c = m->msg_iov[i].iov_len < (size_t) r - o ? m->msg_iov[i].iov_len : (size_t) r - o;
			memcpy (m->msg_iov[i].iov_base, flat + o, c);
			o += c;
		}
		free (flat);
	}
	/* the handles received -> descriptors in one SCM_RIGHTS block */
	int mflags = 0;
	if (k.flags & KAPI_MSG_TRUNC)
		mflags |= MSG_TRUNC;
	if (k.flags & KAPI_MSG_CTRUNC)
		mflags |= MSG_CTRUNC;
	unsigned nfd = 0;
	if (k.nhandles > 0)
	{
		struct cmsghdr *c = (struct cmsghdr *) m->msg_control;
		int *fds = (int *) CMSG_DATA (c);
		for (unsigned i = 0; i < k.nhandles; i++)
		{
			struct __onyx_ofd *o = x[i].kind != KAPI_HK_NONE ? __onyx_ofd_from_xfer (&x[i]) : 0;
			int fd = o ? __onyx_fd_install (o, 0, (flags & MSG_CMSG_CLOEXEC) != 0) : -1;
			if (fd < 0)
			{
				mflags |= MSG_CTRUNC;
				continue;
			}
			fds[nfd++] = fd;
		}
		if (nfd > 0)
		{
			c->cmsg_len = CMSG_LEN (nfd * sizeof (int));
			c->cmsg_level = SOL_SOCKET;
			c->cmsg_type = SCM_RIGHTS;
		}
	}
	m->msg_controllen = nfd > 0 ? CMSG_SPACE (nfd * sizeof (int)) : 0;
	m->msg_flags = mflags;
	if (m->msg_name && m->msg_namelen >= sizeof (sa_family_t))
	{
		((struct sockaddr *) m->msg_name)->sa_family = AF_UNIX;
		m->msg_namelen = sizeof (sa_family_t);
	}
	else if (m->msg_name)
		m->msg_namelen = 0;
	free (x);
	return (ssize_t) r;
}

/* ---- shared memory objects ---- */
static int shm_fd (long long h, int flags, int cloexec)
{
	struct __onyx_ofd *d = __onyx_ofd_new (ONYX_FD_SHM, flags);
	if (d == 0)
	{
		kapi_handle_close (h);
		return -1;
	}
	d->h = h;
	return __onyx_fd_install (d, 0, cloexec);
}

int memfd_create (const char *name, unsigned int flags)
{
	(void) name;
	if (flags & ~(MFD_CLOEXEC | MFD_ALLOW_SEALING))
		return ONYX_ERR (EINVAL);
	if (kapi__core () != 0)
		return ONYX_ERR (ENOSYS);
	long long h = kapi_shm_create (0, (flags & MFD_ALLOW_SEALING) ? KAPI_SHM_ALLOW_SEALING : 0);
	if (h < 0)
		return ONYX_ERR ((int) -h);
	return shm_fd (h, O_RDWR, (flags & MFD_CLOEXEC) != 0);
}

int shm_open (const char *name, int oflag, mode_t mode)
{
	if (kapi__core () != 0)
		return ONYX_ERR (ENOSYS);
	int acc = oflag & O_ACCMODE;
	if (acc == O_WRONLY)
		return ONYX_ERR (EINVAL);
	unsigned kf = (acc == O_RDWR ? KAPI_O_RDWR : KAPI_O_RDONLY) | ((oflag & O_CREAT) ? KAPI_O_CREAT : 0)
		    | ((oflag & O_EXCL) ? KAPI_O_EXCL : 0) | ((oflag & O_TRUNC) ? KAPI_O_TRUNC : 0);
	long long h = kapi_shm_open (name, kf, (unsigned) mode);
	if (h < 0)
		return ONYX_ERR ((int) -h);
	return shm_fd (h, acc, 1);			/* (POSIX: FD_CLOEXEC set) */
}

int shm_unlink (const char *name)
{
	if (kapi__core () != 0)
		return ONYX_ERR (ENOSYS);
	int r = kapi_shm_unlink (name);
	return r < 0 ? ONYX_ERR (-r) : 0;
}
