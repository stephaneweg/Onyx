/*
 * sys/socket.h -- BSD sockets on Onyx (libonyxposix socket.c, over the kapi v75 sock_* calls).
 * IPv4 TCP and UDP; AF_INET6 and AF_UNIX -> EAFNOSUPPORT (socketpair (AF_UNIX) is two pipes).
 * The layouts are Linux's (sa_family_t 16-bit, sockaddr_storage 128 bytes, MSG_NOSIGNAL 0x4000).
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
#ifndef _SYS_SOCKET_H
#define _SYS_SOCKET_H

#include <sys/types.h>
#include <sys/uio.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef __onyx_sa_family_t_defined
#define __onyx_sa_family_t_defined
typedef unsigned short sa_family_t;
#endif
typedef unsigned int socklen_t;

struct sockaddr
{
	sa_family_t sa_family;
	char sa_data[14];
};

struct sockaddr_storage				/* 128 bytes */
{
	sa_family_t ss_family;
	char __ss_pad[118];
	unsigned long __ss_align;
};

struct msghdr
{
	void *msg_name;
	socklen_t msg_namelen;
	struct iovec *msg_iov;
	size_t msg_iovlen;
	void *msg_control;
	size_t msg_controllen;
	int msg_flags;
};

struct cmsghdr
{
	size_t cmsg_len;
	int cmsg_level;
	int cmsg_type;
};
#define CMSG_ALIGN(n)		(((n) + sizeof (size_t) - 1) & ~(sizeof (size_t) - 1))
#define CMSG_DATA(c)		((unsigned char *) ((struct cmsghdr *) (c) + 1))
#define CMSG_SPACE(n)		(CMSG_ALIGN (n) + CMSG_ALIGN (sizeof (struct cmsghdr)))
#define CMSG_LEN(n)		(CMSG_ALIGN (sizeof (struct cmsghdr)) + (n))
#define CMSG_FIRSTHDR(m)	((m)->msg_controllen >= sizeof (struct cmsghdr) ? \
				 (struct cmsghdr *) (m)->msg_control : (struct cmsghdr *) 0)
#define CMSG_NXTHDR(m, c)	((struct cmsghdr *) 0)	/* no ancillary data on Onyx */

struct linger
{
	int l_onoff;
	int l_linger;
};

#define SOCK_STREAM	1
#define SOCK_DGRAM	2
#define SOCK_RAW	3
#define SOCK_SEQPACKET	5
#define SOCK_NONBLOCK	0x800
#define SOCK_CLOEXEC	0x80000

#define AF_UNSPEC	0
#define AF_UNIX		1
#define AF_LOCAL	AF_UNIX
#define AF_INET		2
#define AF_INET6	10
#define PF_UNSPEC	AF_UNSPEC
#define PF_UNIX		AF_UNIX
#define PF_LOCAL	AF_LOCAL
#define PF_INET		AF_INET
#define PF_INET6	AF_INET6

#define SOL_SOCKET	1
#define SO_DEBUG	1
#define SO_REUSEADDR	2
#define SO_TYPE		3
#define SO_ERROR	4
#define SO_DONTROUTE	5
#define SO_BROADCAST	6
#define SO_SNDBUF	7
#define SO_RCVBUF	8
#define SO_KEEPALIVE	9
#define SO_OOBINLINE	10
#define SO_LINGER	13
#define SO_REUSEPORT	15
#define SO_RCVLOWAT	18
#define SO_SNDLOWAT	19
#define SO_RCVTIMEO	20
#define SO_SNDTIMEO	21
#define SO_ACCEPTCONN	30

#define MSG_OOB		0x1
#define MSG_PEEK	0x2
#define MSG_DONTROUTE	0x4
#define MSG_CTRUNC	0x8
#define MSG_TRUNC	0x20
#define MSG_DONTWAIT	0x40
#define MSG_EOR		0x80
#define MSG_WAITALL	0x100
#define MSG_NOSIGNAL	0x4000

#define SHUT_RD		0
#define SHUT_WR		1
#define SHUT_RDWR	2

#define SOMAXCONN	32		/* the kernel's backlog limit */

int socket (int, int, int);
int socketpair (int, int, int, int [2]);
int bind (int, const struct sockaddr *, socklen_t);
int connect (int, const struct sockaddr *, socklen_t);
int listen (int, int);
int accept (int, struct sockaddr *__restrict, socklen_t *__restrict);
int accept4 (int, struct sockaddr *__restrict, socklen_t *__restrict, int);
ssize_t send (int, const void *, size_t, int);
ssize_t recv (int, void *, size_t, int);
ssize_t sendto (int, const void *, size_t, int, const struct sockaddr *, socklen_t);
ssize_t recvfrom (int, void *__restrict, size_t, int, struct sockaddr *__restrict, socklen_t *__restrict);
ssize_t sendmsg (int, const struct msghdr *, int);
ssize_t recvmsg (int, struct msghdr *, int);
int getsockopt (int, int, int, void *__restrict, socklen_t *__restrict);
int setsockopt (int, int, int, const void *, socklen_t);
int getsockname (int, struct sockaddr *__restrict, socklen_t *__restrict);
int getpeername (int, struct sockaddr *__restrict, socklen_t *__restrict);
int shutdown (int, int);
int sockatmark (int);

#ifdef __cplusplus
}
#endif

#endif /* _SYS_SOCKET_H */
