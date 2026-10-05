/*
 * poll.h -- poll on Onyx (libonyxposix poll.c): sockets, pipes, files, the console, over the
 * kapi v75 poll (a user-space loop on an older kernel). select / pselect are built on it.
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
#ifndef _POLL_H
#define _POLL_H

#include <time.h>
#include <signal.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned long nfds_t;

struct pollfd
{
	int fd;
	short events;
	short revents;
};

#define POLLIN		0x001
#define POLLPRI		0x002
#define POLLOUT		0x004
#define POLLERR		0x008
#define POLLHUP		0x010
#define POLLNVAL	0x020
#define POLLRDNORM	0x040
#define POLLRDBAND	0x080
#define POLLWRNORM	0x100
#define POLLWRBAND	0x200
#define POLLRDHUP	0x2000

int poll (struct pollfd *, nfds_t, int);
int ppoll (struct pollfd *, nfds_t, const struct timespec *, const sigset_t *);

#ifdef __cplusplus
}
#endif

#endif /* _POLL_H */
