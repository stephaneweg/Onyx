/*
 * socket.c -- BSD sockets (libonyxposix, docs/POSIX-PLAN.md §3.4): a thin mapping onto the
 * kernel's v75 sock_* calls (WP-NET: IPv4 TCP and UDP, non-blocking connect -> EINPROGRESS then
 * poll (POLLOUT) + SO_ERROR, non-blocking accept, MSG_PEEK / MSG_DONTWAIT / MSG_WAITALL,
 * SO_RCVTIMEO / SO_SNDTIMEO, getsockname / getpeername).
 *
 * On a kernel without them (sock_open -> ENOSYS) a TCP socket falls back to the old tcp_* calls
 * ("LSOCKET"): connect blocks (and returns 0 even on a non-blocking socket: allowed), recv
 * polls the old non-blocking tcp_recv (O_NONBLOCK: EAGAIN), bytes read ahead (by poll, by
 * MSG_PEEK) wait in a carry buffer, listen / accept are tcp_listen / tcp_accept (accept blocks).
 * No UDP then.
 *
 * Not on Onyx: AF_INET6 and AF_UNIX sockets (EAFNOSUPPORT), socketpair (EOPNOTSUPP), SIGPIPE
 * (a send on a closed connection fails with EPIPE; MSG_NOSIGNAL is accepted).
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
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include "posix_internal.h"

#undef htonl
#undef htons
#undef ntohl
#undef ntohs
uint32_t htonl (uint32_t x) { return __builtin_bswap32 (x); }
uint16_t htons (uint16_t x) { return __builtin_bswap16 (x); }
uint32_t ntohl (uint32_t x) { return __builtin_bswap32 (x); }
uint16_t ntohs (uint16_t x) { return __builtin_bswap16 (x); }

#define CARRY_CAP	65536
#define KMSG_MASK	(MSG_PEEK | MSG_DONTWAIT | MSG_WAITALL)	/* same values as KAPI_MSG_* */

static struct __onyx_ofd *sock_get (int fd)
{
	struct __onyx_ofd *d = __onyx_fd_get (fd);
	if (d && d->type != ONYX_FD_SOCKET && d->type != ONYX_FD_LSOCKET)
	{
		__onyx_fd_put (d);
		errno = ENOTSOCK;
		return 0;
	}
	return d;
}

static int to_kaddr (const struct sockaddr *sa, socklen_t len, struct kapi_sockaddr *k)
{
	if (sa == 0)
		return ONYX_ERR (EFAULT);
	if (sa->sa_family != AF_INET)
		return ONYX_ERR (EAFNOSUPPORT);
	if (len < sizeof (struct sockaddr_in))
		return ONYX_ERR (EINVAL);
	const struct sockaddr_in *in = (const struct sockaddr_in *) sa;
	memset (k, 0, sizeof *k);
	k->family = KAPI_AF_INET;
	k->port = ntohs (in->sin_port);
	memcpy (k->addr, &in->sin_addr.s_addr, 4);
	return 0;
}

static void from_kaddr (const struct kapi_sockaddr *k, struct sockaddr *sa, socklen_t *len)
{
	if (sa == 0 || len == 0)
		return;
	struct sockaddr_in in;
	memset (&in, 0, sizeof in);
	in.sin_family = AF_INET;
	in.sin_port = htons (k->port);
	memcpy (&in.sin_addr.s_addr, k->addr, 4);
	socklen_t n = *len < sizeof in ? *len : sizeof in;
	memcpy (sa, &in, n);
	*len = sizeof in;
}

/* ---- socket / connect / bind / listen / accept ---- */
int socket (int domain, int type, int proto)
{
	(void) proto;
	if (domain != AF_INET)
		return ONYX_ERR (EAFNOSUPPORT);
	int base = type & 0xF;
	int nb = (type & SOCK_NONBLOCK) != 0;
	int kt = base == SOCK_STREAM ? KAPI_SOCK_STREAM : base == SOCK_DGRAM ? KAPI_SOCK_DGRAM : 0;
	if (kt == 0)
		return ONYX_ERR (EPROTONOSUPPORT);
	if (kapi__core () != 0)
		return ONYX_ERR (ENOSYS);
	int s = kapi_sock_open (kt, nb ? KAPI_SOCKF_NONBLOCK : 0);
	struct __onyx_ofd *d;
	if (s == -KAPI_ENOSYS)
	{
		if (kt != KAPI_SOCK_STREAM)
			return ONYX_ERR (EPROTONOSUPPORT);
		d = __onyx_ofd_new (ONYX_FD_LSOCKET, O_RDWR | (nb ? O_NONBLOCK : 0));
		if (d == 0)
			return -1;
		d->h = -1;
	}
	else if (s < 0)
		return ONYX_ERR (-s);
	else
	{
		d = __onyx_ofd_new (ONYX_FD_SOCKET, O_RDWR | (nb ? O_NONBLOCK : 0));
		if (d == 0)
		{
			kapi_sock_close (s);
			return -1;
		}
		d->h = s;
	}
	d->sotype = base;
	return __onyx_fd_install (d, 0, (type & SOCK_CLOEXEC) != 0);
}

int socketpair (int domain, int type, int proto, int sv[2])
{
	(void) domain; (void) type; (void) proto; (void) sv;
	return ONYX_ERR (EOPNOTSUPP);
}

static int legacy_connect (struct __onyx_ofd *d, const struct kapi_sockaddr *k)
{
	if (d->h >= 0)
		return ONYX_ERR (EISCONN);
	char ip[20];
	snprintf (ip, sizeof ip, "%u.%u.%u.%u", k->addr[0], k->addr[1], k->addr[2], k->addr[3]);
	int h = kapi_tcp_connect (ip, k->port);
	if (h < 0)
		return ONYX_ERR (h == -1 ? ENETUNREACH : ECONNREFUSED);
	d->h = h;
	d->connected = 1;
	struct sockaddr_in *peer = (struct sockaddr_in *) d->peer;
	peer->sin_family = AF_INET;
	peer->sin_port = htons (k->port);
	memcpy (&peer->sin_addr.s_addr, k->addr, 4);
	return 0;
}

int connect (int fd, const struct sockaddr *sa, socklen_t len)
{
	struct kapi_sockaddr k;
	struct __onyx_ofd *d = sock_get (fd);
	if (d == 0)
		return -1;
	int r = to_kaddr (sa, len, &k);
	if (r == 0)
	{
		if (d->type == ONYX_FD_SOCKET)
		{
			r = kapi_sock_connect ((int) d->h, &k);
			r = r < 0 ? ONYX_ERR (-r) : 0;
		}
		else
			r = legacy_connect (d, &k);
	}
	__onyx_fd_put (d);
	return r;
}

int bind (int fd, const struct sockaddr *sa, socklen_t len)
{
	struct kapi_sockaddr k;
	struct __onyx_ofd *d = sock_get (fd);
	if (d == 0)
		return -1;
	int r = to_kaddr (sa, len, &k);
	if (r == 0)
	{
		if (d->type == ONYX_FD_SOCKET)
		{
			r = kapi_sock_bind ((int) d->h, &k);
			r = r < 0 ? ONYX_ERR (-r) : 0;
		}
		else
		{
			struct sockaddr_in *me = (struct sockaddr_in *) d->peer;	/* (kept here until listen) */
			me->sin_family = AF_INET;
			me->sin_port = htons (k.port);
		}
	}
	__onyx_fd_put (d);
	return r;
}

int listen (int fd, int backlog)
{
	struct __onyx_ofd *d = sock_get (fd);
	if (d == 0)
		return -1;
	int r;
	if (d->type == ONYX_FD_SOCKET)
	{
		r = kapi_sock_listen ((int) d->h, backlog);
		r = r < 0 ? ONYX_ERR (-r) : 0;
	}
	else
	{
		unsigned port = ntohs (((struct sockaddr_in *) d->peer)->sin_port);
		int h = port ? kapi_tcp_listen (port) : -1;
		if (h < 0)
			r = ONYX_ERR (h == -6 ? EADDRINUSE : EINVAL);
		else
		{
			d->h = h;
			d->listening = 1;
			r = 0;
		}
	}
	if (r == 0)
		d->listening = 1;
	__onyx_fd_put (d);
	return r;
}

int accept4 (int fd, struct sockaddr *sa, socklen_t *len, int flags)
{
	struct __onyx_ofd *d = sock_get (fd);
	if (d == 0)
		return -1;
	int nb = (flags & SOCK_NONBLOCK) != 0;
	struct __onyx_ofd *n = 0;
	int r = -1;
	if (d->type == ONYX_FD_SOCKET)
	{
		struct kapi_sockaddr k;
		memset (&k, 0, sizeof k);
		int s = kapi_sock_accept ((int) d->h, &k, nb ? KAPI_SOCKF_NONBLOCK : 0);
		if (s < 0)
			errno = -s;
		else if ((n = __onyx_ofd_new (ONYX_FD_SOCKET, O_RDWR | (nb ? O_NONBLOCK : 0))) == 0)
			kapi_sock_close (s);
		else
		{
			n->h = s;
			n->sotype = SOCK_STREAM;
			n->connected = 1;
			from_kaddr (&k, sa, len);
		}
	}
	else if (!d->listening)
		errno = EINVAL;
	else
	{
		char ip[32] = "";
		int s = kapi_tcp_accept ((int) d->h, ip, sizeof ip);	/* (blocks) */
		if (s < 0)
			errno = ECONNABORTED;
		else if ((n = __onyx_ofd_new (ONYX_FD_LSOCKET, O_RDWR | (nb ? O_NONBLOCK : 0))) == 0)
			kapi_tcp_close (s);
		else
		{
			n->h = s;
			n->sotype = SOCK_STREAM;
			n->connected = 1;
			struct sockaddr_in *peer = (struct sockaddr_in *) n->peer;
			peer->sin_family = AF_INET;
			unsigned a, b, c, e;
			if (sscanf (ip, "%u.%u.%u.%u", &a, &b, &c, &e) == 4)
				peer->sin_addr.s_addr = htonl ((a << 24) | (b << 16) | (c << 8) | e);
			if (sa && len)
			{
				socklen_t m = *len < sizeof *peer ? *len : sizeof *peer;
				memcpy (sa, peer, m);
				*len = sizeof *peer;
			}
		}
	}
	if (n)
		r = __onyx_fd_install (n, 0, (flags & SOCK_CLOEXEC) != 0);
	__onyx_fd_put (d);
	return r;
}

int accept (int fd, struct sockaddr *sa, socklen_t *len) { return accept4 (fd, sa, len, 0); }

/* ---- the carry buffer (old sockets, pipes: what poll and MSG_PEEK read ahead) ---- */
size_t __onyx_carry_take (struct __onyx_ofd *d, void *buf, size_t n, int peek)
{
	size_t avail = d->carry_n - d->carry_off;
	if (n > avail)
		n = avail;
	memcpy (buf, d->carry + d->carry_off, n);
	if (!peek)
	{
		d->carry_off += (unsigned) n;
		if (d->carry_off == d->carry_n)
			d->carry_off = d->carry_n = 0;
	}
	return n;
}

int __onyx_carry_fill (struct __onyx_ofd *d)
{
	if (d->carry_n > d->carry_off)
		return 1;
	if (d->eof)
		return -1;
	if (d->carry == 0)
	{
		d->carry = (unsigned char *) malloc (CARRY_CAP);
		if (d->carry == 0)
			return 0;
	}
	int r;
	if (d->type == ONYX_FD_LSOCKET)
	{
		if (d->h < 0 || d->listening)
			return 0;
		r = kapi_tcp_recv ((int) d->h, d->carry, CARRY_CAP);
		if (r < 0)
		{
			d->eof = 1;
			return -1;
		}
	}
	else
	{
		void *h = d->type == ONYX_FD_STREAM || d->type == ONYX_FD_CONSOLE ? d->stream : d->pipe ? d->pipe->h : 0;
		if (h == 0)
			return 0;
		r = kapi_stream_read_nb (h, d->carry, CARRY_CAP);
		if (r == 0)
		{
			d->eof = 1;
			return -1;
		}
		if (r < 0)
			return 0;
	}
	if (r == 0)
		return 0;
	d->carry_n = (unsigned) r;
	d->carry_off = 0;
	return 1;
}

/* ---- send / recv ---- */
static ssize_t legacy_recv (struct __onyx_ofd *d, void *buf, size_t n, int flags)
{
	if (d->h < 0 || d->listening)
		return ONYX_ERR (ENOTCONN);
	int nb = (d->flags & O_NONBLOCK) || (flags & MSG_DONTWAIT);
	unsigned long long end = d->rcvtimeo ? __onyx_mono_ns () + d->rcvtimeo * 1000000ULL : 0;
	size_t got = 0;
	for (;;)
	{
		__onyx_ofd_lock (d);
		int f = __onyx_carry_fill (d);
		if (f > 0)
			got += __onyx_carry_take (d, (char *) buf + got, n - got, (flags & MSG_PEEK) != 0);
		__onyx_ofd_unlock (d);
		if (got == n || (got > 0 && !(flags & MSG_WAITALL)) || (flags & MSG_PEEK && got > 0))
			return (ssize_t) got;
		if (f < 0)
			return (ssize_t) got;		/* the end (or the connection lost) */
		if (nb)
			return got ? (ssize_t) got : ONYX_ERR (EAGAIN);
		if (end && __onyx_mono_ns () >= end)
			return got ? (ssize_t) got : ONYX_ERR (EAGAIN);
		kapi_msleep (1);
	}
}

ssize_t __onyx_sock_recv (struct __onyx_ofd *d, void *buf, size_t n, int flags, void *from)
{
	if (d->type == ONYX_FD_LSOCKET)
		return legacy_recv (d, buf, n, flags);
	struct kapi_sockaddr k;
	memset (&k, 0, sizeof k);
	unsigned kf = (unsigned) (flags & KMSG_MASK);
	long long r = kapi_sock_recv ((int) d->h, buf, n, kf, from ? &k : 0);
	if (r < 0)
		return ONYX_ERR ((int) -r);
	if (from)
		memcpy (from, &k, sizeof k);
	return (ssize_t) r;
}

ssize_t __onyx_sock_send (struct __onyx_ofd *d, const void *buf, size_t n, int flags, const void *to)
{
	if (d->type == ONYX_FD_LSOCKET)
	{
		if (d->h < 0 || d->listening)
			return ONYX_ERR (ENOTCONN);
		size_t done = 0;
		while (done < n)
		{
			unsigned c = n - done > (1U << 20) ? (1U << 20) : (unsigned) (n - done);
			int r = kapi_tcp_send ((int) d->h, (const char *) buf + done, c);
			if (r < 0)
				return done ? (ssize_t) done : ONYX_ERR (EPIPE);
			if (r == 0)
				break;
			done += (unsigned) r;
		}
		return (ssize_t) done;
	}
	unsigned kf = (unsigned) (flags & KMSG_MASK);
	long long r = kapi_sock_send ((int) d->h, buf, n, kf, (const struct kapi_sockaddr *) to);
	return r < 0 ? ONYX_ERR ((int) -r) : (ssize_t) r;
}

ssize_t recvfrom (int fd, void *buf, size_t n, int flags, struct sockaddr *sa, socklen_t *len)
{
	struct __onyx_ofd *d = sock_get (fd);
	if (d == 0)
		return -1;
	struct kapi_sockaddr k;
	ssize_t r = __onyx_sock_recv (d, buf, n, flags, sa ? &k : 0);
	if (r >= 0 && sa)
	{
		if (d->type == ONYX_FD_SOCKET)
			from_kaddr (&k, sa, len);
		else if (len)
		{
			socklen_t m = *len < sizeof (struct sockaddr_in) ? *len : sizeof (struct sockaddr_in);
			memcpy (sa, d->peer, m);
			*len = sizeof (struct sockaddr_in);
		}
	}
	__onyx_fd_put (d);
	return r;
}

ssize_t recv (int fd, void *buf, size_t n, int flags) { return recvfrom (fd, buf, n, flags, 0, 0); }

ssize_t sendto (int fd, const void *buf, size_t n, int flags, const struct sockaddr *sa, socklen_t len)
{
	struct __onyx_ofd *d = sock_get (fd);
	if (d == 0)
		return -1;
	ssize_t r;
	struct kapi_sockaddr k;
	if (sa && to_kaddr (sa, len, &k) != 0)
		r = -1;
	else
		r = __onyx_sock_send (d, buf, n, flags, sa ? &k : 0);
	__onyx_fd_put (d);
	return r;
}

ssize_t send (int fd, const void *buf, size_t n, int flags) { return sendto (fd, buf, n, flags, 0, 0); }

ssize_t sendmsg (int fd, const struct msghdr *m, int flags)
{
	size_t total = 0;
	for (size_t i = 0; i < m->msg_iovlen; i++)
		total += m->msg_iov[i].iov_len;
	char *b = (char *) malloc (total ? total : 1);
	if (b == 0)
		return ONYX_ERR (ENOMEM);
	size_t o = 0;
	for (size_t i = 0; i < m->msg_iovlen; i++)
	{
		memcpy (b + o, m->msg_iov[i].iov_base, m->msg_iov[i].iov_len);
		o += m->msg_iov[i].iov_len;
	}
	ssize_t r = sendto (fd, b, total, flags, (const struct sockaddr *) m->msg_name, m->msg_namelen);
	free (b);
	return r;
}

ssize_t recvmsg (int fd, struct msghdr *m, int flags)
{
	size_t total = 0;
	for (size_t i = 0; i < m->msg_iovlen; i++)
		total += m->msg_iov[i].iov_len;
	char *b = (char *) malloc (total ? total : 1);
	if (b == 0)
		return ONYX_ERR (ENOMEM);
	ssize_t r = recvfrom (fd, b, total, flags, (struct sockaddr *) m->msg_name,
			      m->msg_name ? &m->msg_namelen : 0);
	if (r > 0)
	{
		size_t o = 0;
		for (size_t i = 0; i < m->msg_iovlen && o < (size_t) r; i++)
		{
			size_t c = m->msg_iov[i].iov_len < (size_t) r - o ? m->msg_iov[i].iov_len : (size_t) r - o;
			memcpy (m->msg_iov[i].iov_base, b + o, c);
			o += c;
		}
	}
	m->msg_controllen = 0;
	m->msg_flags = 0;
	free (b);
	return r;
}

/* ---- options ---- */
int __onyx_sock_set_nonblock (struct __onyx_ofd *d, int on)
{
	if (d->type == ONYX_FD_SOCKET)
	{
		int r = kapi_sock_setopt ((int) d->h, KAPI_SO_NONBLOCK, on);
		return r < 0 ? ONYX_ERR (-r) : 0;
	}
	return 0;					/* (an old socket: the flag alone) */
}

static int put_int (void *v, socklen_t *len, int x)
{
	if (v == 0 || len == 0 || *len < sizeof (int))
		return ONYX_ERR (EINVAL);
	memcpy (v, &x, sizeof x);
	*len = sizeof x;
	return 0;
}

int getsockopt (int fd, int level, int opt, void *v, socklen_t *len)
{
	struct __onyx_ofd *d = sock_get (fd);
	if (d == 0)
		return -1;
	int r = 0, x = 0;
	if (level == SOL_SOCKET)
	{
		switch (opt)
		{
		case SO_ERROR:
			if (d->type == ONYX_FD_SOCKET)
			{
				int k = kapi_sock_getopt ((int) d->h, KAPI_SO_ERROR, &x);
				if (k < 0)
					r = ONYX_ERR (-k);
			}
			else
			{
				x = d->so_error;
				d->so_error = 0;
			}
			break;
		case SO_TYPE:		x = d->sotype; break;
		case SO_ACCEPTCONN:	x = d->listening; break;
		case SO_RCVBUF:
		case SO_SNDBUF:		x = 65536; break;
		case SO_RCVTIMEO:
		case SO_SNDTIMEO:
		{
			int ms = 0;
			if (d->type == ONYX_FD_SOCKET)
				kapi_sock_getopt ((int) d->h, opt == SO_RCVTIMEO ? KAPI_SO_RCVTIMEO_MS : KAPI_SO_SNDTIMEO_MS, &ms);
			else
				ms = (int) (opt == SO_RCVTIMEO ? d->rcvtimeo : d->sndtimeo);
			struct timeval tv = { ms / 1000, (ms % 1000) * 1000 };
			if (v == 0 || len == 0 || *len < sizeof tv)
				r = ONYX_ERR (EINVAL);
			else
			{
				memcpy (v, &tv, sizeof tv);
				*len = sizeof tv;
			}
			__onyx_fd_put (d);
			return r;
		}
		case SO_BROADCAST:
			if (d->type == ONYX_FD_SOCKET)
				kapi_sock_getopt ((int) d->h, KAPI_SO_BROADCAST, &x);
			break;
		default:		x = 0; break;	/* SO_KEEPALIVE, SO_REUSEADDR, SO_LINGER...: off */
		}
	}
	else if (level == IPPROTO_TCP)
		x = opt == TCP_NODELAY ? 1 : 0;
	else
		r = ONYX_ERR (ENOPROTOOPT);
	if (r == 0)
		r = put_int (v, len, x);
	__onyx_fd_put (d);
	return r;
}

int setsockopt (int fd, int level, int opt, const void *v, socklen_t len)
{
	struct __onyx_ofd *d = sock_get (fd);
	if (d == 0)
		return -1;
	int r = 0;
	if (level == SOL_SOCKET && (opt == SO_RCVTIMEO || opt == SO_SNDTIMEO))
	{
		struct timeval tv;
		if (v == 0 || len < sizeof tv)
			r = ONYX_ERR (EINVAL);
		else
		{
			memcpy (&tv, v, sizeof tv);
			int ms = (int) (tv.tv_sec * 1000 + tv.tv_usec / 1000);
			if (ms == 0 && tv.tv_usec)
				ms = 1;
			if (d->type == ONYX_FD_SOCKET)
			{
				int k = kapi_sock_setopt ((int) d->h, opt == SO_RCVTIMEO ? KAPI_SO_RCVTIMEO_MS : KAPI_SO_SNDTIMEO_MS, ms);
				if (k < 0)
					r = ONYX_ERR (-k);
			}
			else if (opt == SO_RCVTIMEO)
				d->rcvtimeo = (unsigned) ms;
			else
				d->sndtimeo = (unsigned) ms;
		}
	}
	else if (level == SOL_SOCKET && opt == SO_BROADCAST && d->type == ONYX_FD_SOCKET)
	{
		int x = 0;
		if (v && len >= sizeof x)
			memcpy (&x, v, sizeof x);
		int k = kapi_sock_setopt ((int) d->h, KAPI_SO_BROADCAST, x != 0);
		if (k < 0)
			r = ONYX_ERR (-k);
	}
	else if (level != SOL_SOCKET && level != IPPROTO_TCP && level != IPPROTO_IP && level != IPPROTO_IPV6)
		r = ONYX_ERR (ENOPROTOOPT);
	/* else: TCP_NODELAY, SO_KEEPALIVE, SO_REUSEADDR, SO_RCVBUF / SNDBUF, SO_LINGER... accepted */
	__onyx_fd_put (d);
	return r;
}

/* ---- names, shutdown, close ---- */
static int name (int fd, int peer, struct sockaddr *sa, socklen_t *len)
{
	struct __onyx_ofd *d = sock_get (fd);
	if (d == 0)
		return -1;
	int r = 0;
	if (d->type == ONYX_FD_SOCKET)
	{
		struct kapi_sockaddr k;
		memset (&k, 0, sizeof k);
		int x = kapi_sock_name ((int) d->h, peer, &k);
		if (x < 0)
			r = ONYX_ERR (-x);
		else
			from_kaddr (&k, sa, len);
	}
	else if (peer && !d->connected)
		r = ONYX_ERR (ENOTCONN);
	else if (sa && len)
	{
		struct sockaddr_in in;
		memset (&in, 0, sizeof in);
		in.sin_family = AF_INET;
		if (peer || d->listening)
			memcpy (&in, d->peer, sizeof in);
		socklen_t m = *len < sizeof in ? *len : sizeof in;
		memcpy (sa, &in, m);
		*len = sizeof in;
	}
	__onyx_fd_put (d);
	return r;
}

int getsockname (int fd, struct sockaddr *sa, socklen_t *len) { return name (fd, 0, sa, len); }
int getpeername (int fd, struct sockaddr *sa, socklen_t *len) { return name (fd, 1, sa, len); }

int shutdown (int fd, int how)
{
	struct __onyx_ofd *d = sock_get (fd);
	if (d == 0)
		return -1;
	int r = 0;
	if (how < SHUT_RD || how > SHUT_RDWR)
		r = ONYX_ERR (EINVAL);
	else if (d->type == ONYX_FD_SOCKET)
	{
		int k = kapi_sock_shutdown ((int) d->h, how);
		if (k < 0)
			r = ONYX_ERR (-k);
	}
	else if (!d->connected)
		r = ONYX_ERR (ENOTCONN);
	__onyx_fd_put (d);
	return r;
}

int __onyx_sock_close (struct __onyx_ofd *d)
{
	if (d->type == ONYX_FD_SOCKET)
	{
		int r = kapi_sock_close ((int) d->h);
		return r < 0 ? ONYX_ERR (-r) : 0;
	}
	if (d->h >= 0)
		kapi_tcp_close ((int) d->h);
	return 0;
}

int sockatmark (int fd) { (void) fd; return 0; }
