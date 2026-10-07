/*
 * poll.c -- poll, ppoll, select, pselect (libonyxposix, docs/POSIX-PLAN.md §3.4).
 *
 * The kernel's v75 poll (WP-NET) waits on sockets, streams (pipes, the console's stdin) and
 * files together, and wakes when one is ready. Descriptors it does not know (/dev pseudo files,
 * directories, the old LFILE files) are always ready; a pipe with bytes read ahead is ready.
 * When some descriptor needs it -- an old tcp_* socket (LSOCKET) -- or the kernel has no poll
 * (ENOSYS), a user-space loop asks each descriptor (a non-blocking read into its carry buffer:
 * the bytes wait there for the next read) and sleeps 1 ms between turns.
 * select / pselect are built on poll (FD_SETSIZE 1024).
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
#include <errno.h>
#include <poll.h>
#include <sys/select.h>
#include <sys/time.h>
#include "posix_internal.h"

#define IN_BITS		(POLLIN | POLLRDNORM | POLLRDBAND | POLLPRI)
#define OUT_BITS	(POLLOUT | POLLWRNORM | POLLWRBAND)

static void *console_stream (struct __onyx_ofd *d)
{
	if (d->console == 0 && d->stream == 0)
		d->stream = kapi_stdin ();
	return d->console == 0 ? d->stream : 0;
}

/* what a descriptor is ready for now, as the user-space loop sees it */
static short ready_now (struct __onyx_ofd *d, short ev)
{
	short r = 0;
	switch (d->type)
	{
	case ONYX_FD_PIPE_R:
	case ONYX_FD_STREAM:
	{
		__onyx_ofd_lock (d);
		int f = __onyx_carry_fill (d);
		__onyx_ofd_unlock (d);
		if (f > 0)
			r |= POLLIN;
		else if (f < 0)
			r |= POLLIN | POLLHUP;
		if (d->type == ONYX_FD_STREAM)
			r |= POLLOUT;
		break;
	}
	case ONYX_FD_PIPE_W:
		r = d->pipe->readers > 0 || d->pipe->child_reader ? POLLOUT : POLLERR;
		break;
	case ONYX_FD_CONSOLE:
		if (d->console != 0)
			r = POLLOUT;
		else
		{
			if (d->carry_n > d->carry_off)
				r = POLLIN;
			else if (console_stream (d))
			{
				__onyx_ofd_lock (d);		/* (the carry, from the stdin stream) */
				int f = __onyx_carry_fill (d);
				__onyx_ofd_unlock (d);
				r = f > 0 ? POLLIN : f < 0 ? POLLIN | POLLHUP : 0;
			}
			else
				r = POLLIN;		/* (no stream: a read will block on the console) */
		}
		break;
	case ONYX_FD_LSOCKET:
		if (d->listening)
			r = POLLIN;			/* (accept blocks until a peer comes) */
		else if (d->h < 0)
			r = 0;
		else
		{
			__onyx_ofd_lock (d);
			int f = __onyx_carry_fill (d);
			__onyx_ofd_unlock (d);
			r = POLLOUT;
			if (f > 0)
				r |= POLLIN;
			else if (f < 0)
				r |= POLLIN | POLLHUP;
		}
		break;
	case ONYX_FD_SOCKET:
	{
		/* (a kernel with sockets but without poll: ask how much is waiting) */
		int n = 0, e = 0;
		kapi_sock_getopt ((int) d->h, KAPI_SO_NREAD, &n);
		kapi_sock_getopt ((int) d->h, KAPI_SO_ERROR, &e);
		r = POLLOUT;
		if (n > 0)
			r |= POLLIN;
		if (e)
		{
			r |= POLLERR;
			d->so_error = e;
		}
		break;
	}
	default:
		r = POLLIN | POLLOUT;
	}
	return (short) (r & (ev | POLLERR | POLLHUP | POLLNVAL));
}

static short map_events (short ev)
{
	short k = 0;
	if (ev & IN_BITS) k |= KAPI_POLLIN;
	if (ev & OUT_BITS) k |= KAPI_POLLOUT;
	return k;
}

static short unmap_events (short k, short ev)
{
	short r = 0;
	if (k & KAPI_POLLIN) r |= ev & (POLLIN | POLLRDNORM);
	if (k & KAPI_POLLPRI) r |= ev & POLLPRI;
	if (k & KAPI_POLLOUT) r |= ev & (POLLOUT | POLLWRNORM);
	if (k & KAPI_POLLERR) r |= POLLERR;
	if (k & KAPI_POLLHUP) r |= POLLHUP;
	if (k & KAPI_POLLNVAL) r |= POLLNVAL;
	return r;
}

int poll (struct pollfd *fds, nfds_t n, int timeout)
{
	if (n > ONYX_FD_MAX)
		return ONYX_ERR (EINVAL);
	if (n == 0)
	{
		/* nothing to watch: a sleep */
		if (timeout > 0)
			__onyx_sleep_ns ((unsigned long long) timeout * 1000000ULL);
		else if (timeout < 0)
			for (;;)
				__onyx_sleep_ns (1000000000ULL);
		return 0;
	}
	struct __onyx_ofd **d = 0;
	struct kapi_pollfd *k = 0;
	if (n)
	{
		d = (struct __onyx_ofd **) calloc (n, sizeof *d);
		k = (struct kapi_pollfd *) calloc (n, sizeof *k);
		if (d == 0 || k == 0)
		{
			free (d);
			free (k);
			return ONYX_ERR (ENOMEM);
		}
	}
	int user = kapi__core () != 0;		/* the user-space loop for everything */
	int ready = 0;
	for (nfds_t i = 0; i < n; i++)
	{
		fds[i].revents = 0;
		if (fds[i].fd < 0)
			continue;
		int e = errno;
		d[i] = __onyx_fd_get (fds[i].fd);
		errno = e;
		if (d[i] == 0)
		{
			fds[i].revents = POLLNVAL;
			ready++;
			continue;
		}
		struct __onyx_ofd *o = d[i];
		k[i].events = map_events (fds[i].events);
		switch (o->type)
		{
		case ONYX_FD_SOCKET:
			k[i].kind = KAPI_PK_SOCKET;
			k[i].h = (int) o->h;
			break;
		case ONYX_FD_FILE:
			k[i].kind = KAPI_PK_FILE;
			k[i].h = (int) o->h;
			break;
		case ONYX_FD_PIPE_R:
		case ONYX_FD_PIPE_W:
		case ONYX_FD_STREAM:
			if (o->carry_n > o->carry_off || o->eof)
				fds[i].revents = (short) ((POLLIN | (o->eof ? POLLHUP : 0)) & (fds[i].events | POLLHUP));
			k[i].kind = KAPI_PK_STREAM;
			k[i].h = (int) (unsigned long) (o->type == ONYX_FD_STREAM ? o->stream : o->pipe->h);
			break;
		case ONYX_FD_CONSOLE:
			if (o->console == 0 && o->carry_n == o->carry_off && console_stream (o))
			{
				k[i].kind = KAPI_PK_STREAM;
				k[i].h = (int) (unsigned long) o->stream;
			}
			else
				fds[i].revents = ready_now (o, fds[i].events);
			break;
		case ONYX_FD_LSOCKET:
			user = 1;
			break;
		default:
			fds[i].revents = ready_now (o, fds[i].events);
		}
		if (fds[i].revents)
			ready++;
	}

	int r = 0;
	if (!user)
	{
		int kr = kapi_poll (k, (unsigned) n, ready ? 0 : timeout);
		if (kr == -KAPI_ENOSYS)
			user = 1;
		else if (kr < 0)
			r = ONYX_ERR (-kr);
		else
		{
			r = 0;
			for (nfds_t i = 0; i < n; i++)
			{
				if (k[i].kind != KAPI_PK_NONE)
					fds[i].revents |= unmap_events (k[i].revents, fds[i].events);
				if (fds[i].revents)
					r++;
			}
		}
	}
	if (user)
	{
		unsigned long long end = timeout < 0 ? ~0ULL : __onyx_mono_ns () + (unsigned long long) timeout * 1000000ULL;
		for (;;)
		{
			r = 0;
			for (nfds_t i = 0; i < n; i++)
			{
				if (d[i] != 0)
					fds[i].revents = ready_now (d[i], fds[i].events);
				if (fds[i].revents)
					r++;
			}
			if (r || timeout == 0 || __onyx_mono_ns () >= end)
				break;
			__onyx_sleep_ns (1000000ULL);
		}
	}
	for (nfds_t i = 0; i < n; i++)
		if (d[i])
			__onyx_fd_put (d[i]);
	free (d);
	free (k);
	return r;
}

int ppoll (struct pollfd *fds, nfds_t n, const struct timespec *ts, const sigset_t *mask)
{
	(void) mask;
	int ms = -1;
	if (ts)
	{
		long long t = (long long) ts->tv_sec * 1000 + (ts->tv_nsec + 999999) / 1000000;
		ms = t > 0x7FFFFFFF ? 0x7FFFFFFF : (int) t;
	}
	return poll (fds, n, ms);
}

static int do_select (int nfds, fd_set *rd, fd_set *wr, fd_set *ex, int ms)
{
	if (nfds < 0 || nfds > ONYX_FD_MAX)
		return ONYX_ERR (EINVAL);
	struct pollfd *p = (struct pollfd *) calloc (nfds ? (size_t) nfds : 1, sizeof *p);
	if (p == 0)
		return ONYX_ERR (ENOMEM);
	int n = 0;
	for (int fd = 0; fd < nfds; fd++)
	{
		short ev = 0;
		if (rd && FD_ISSET (fd, rd)) ev |= POLLIN;
		if (wr && FD_ISSET (fd, wr)) ev |= POLLOUT;
		if (ex && FD_ISSET (fd, ex)) ev |= POLLPRI;
		if (ev == 0)
			continue;
		p[n].fd = fd;
		p[n].events = ev;
		n++;
	}
	int r = poll (p, (nfds_t) n, ms);
	if (r < 0)
	{
		free (p);
		return -1;
	}
	for (int i = 0; i < n; i++)
		if (p[i].revents & POLLNVAL)
		{
			free (p);
			return ONYX_ERR (EBADF);
		}
	if (rd) FD_ZERO (rd);
	if (wr) FD_ZERO (wr);
	if (ex) FD_ZERO (ex);
	int count = 0;
	for (int i = 0; i < n; i++)
	{
		short re = p[i].revents;
		if (rd && (p[i].events & POLLIN) && (re & (POLLIN | POLLHUP | POLLERR)))
		{
			FD_SET (p[i].fd, rd);
			count++;
		}
		if (wr && (p[i].events & POLLOUT) && (re & (POLLOUT | POLLERR)))
		{
			FD_SET (p[i].fd, wr);
			count++;
		}
		if (ex && (p[i].events & POLLPRI) && (re & POLLPRI))
		{
			FD_SET (p[i].fd, ex);
			count++;
		}
	}
	free (p);
	return count;
}

int select (int nfds, fd_set *rd, fd_set *wr, fd_set *ex, struct timeval *tv)
{
	int ms = -1;
	if (tv)
	{
		long long t = (long long) tv->tv_sec * 1000 + (tv->tv_usec + 999) / 1000;
		ms = t > 0x7FFFFFFF ? 0x7FFFFFFF : (int) t;
	}
	return do_select (nfds, rd, wr, ex, ms);
}

int pselect (int nfds, fd_set *rd, fd_set *wr, fd_set *ex, const struct timespec *ts, const sigset_t *mask)
{
	(void) mask;
	int ms = -1;
	if (ts)
	{
		long long t = (long long) ts->tv_sec * 1000 + (ts->tv_nsec + 999999) / 1000000;
		ms = t > 0x7FFFFFFF ? 0x7FFFFFFF : (int) t;
	}
	return do_select (nfds, rd, wr, ex, ms);
}
