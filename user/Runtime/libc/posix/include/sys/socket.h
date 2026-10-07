/*
 * sys/socket.h -- BSD sockets on Onyx (libonyxposix socket.c, over the kapi v75 sock_* calls).
 * IPv4 TCP and UDP; AF_INET6 and socket (AF_UNIX) -> EAFNOSUPPORT. (v76) socketpair (AF_UNIX,
 * SOCK_STREAM / SOCK_SEQPACKET / SOCK_DGRAM): local sockets, with SCM_RIGHTS through sendmsg /
 * recvmsg (ipc.c). The layouts are Linux's (sa_family_t 16-bit, sockaddr_storage 128 bytes,
 * MSG_NOSIGNAL 0x4000, struct cmsghdr with a size_t length).
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
#define CMSG_NXTHDR(m, c)	__onyx_cmsg_nxthdr ((m), (c))
static __inline__ struct cmsghdr *__onyx_cmsg_nxthdr (const struct msghdr *__m, const struct cmsghdr *__c)
{
	unsigned char *__end = (unsigned char *) __m->msg_control + __m->msg_controllen;
	if (__c->cmsg_len < sizeof (struct cmsghdr))
		return (struct cmsghdr *) 0;
	struct cmsghdr *__n = (struct cmsghdr *) ((unsigned char *) __c + CMSG_ALIGN (__c->cmsg_len));
	if ((unsigned char *) (__n + 1) > __end || (unsigned char *) __n + CMSG_ALIGN (__n->cmsg_len) > __end)
		return (struct cmsghdr *) 0;
	return __n;
}

/* (v76) the ancillary data of a local socket (sendmsg / recvmsg) */
#define SCM_RIGHTS	1		/* int[]: descriptors passed */
#define SCM_CREDENTIALS	2		/* (not sent on Onyx) */
struct ucred				/* SO_PEERCRED */
{
	pid_t pid;
	uid_t uid;
	gid_t gid;
};

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
#define SO_PASSCRED	16
#define SO_PEERCRED	17		/* struct ucred: the peer's pid (local sockets) */
#define SO_PROTOCOL	38
#define SO_DOMAIN	39

#define MSG_OOB		0x1
#define MSG_PEEK	0x2
#define MSG_DONTROUTE	0x4
#define MSG_CTRUNC	0x8
#define MSG_TRUNC	0x20
#define MSG_DONTWAIT	0x40
#define MSG_EOR		0x80
#define MSG_WAITALL	0x100
#define MSG_NOSIGNAL	0x4000
#define MSG_CMSG_CLOEXEC 0x40000000	/* recvmsg: the descriptors received get FD_CLOEXEC */

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
