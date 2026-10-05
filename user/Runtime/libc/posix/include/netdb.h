/*
 * netdb.h -- name resolution on Onyx (libonyxposix netdb.c): getaddrinfo / getnameinfo /
 * gethostbyname over the kernel's DNS (kapi net_resolve, v43: IPv4, one address a name).
 * Services: numeric, and the few well-known names (http, https, ftp, smtp, imap(s), pop3(s),
 * domain).
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
#ifndef _NETDB_H
#define _NETDB_H

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#ifdef __cplusplus
extern "C" {
#endif

struct addrinfo
{
	int ai_flags;
	int ai_family;
	int ai_socktype;
	int ai_protocol;
	socklen_t ai_addrlen;
	struct sockaddr *ai_addr;
	char *ai_canonname;
	struct addrinfo *ai_next;
};

struct hostent
{
	char *h_name;
	char **h_aliases;
	int h_addrtype;
	int h_length;
	char **h_addr_list;
};
#define h_addr	h_addr_list[0]

struct servent
{
	char *s_name;
	char **s_aliases;
	int s_port;				/* network byte order */
	char *s_proto;
};

struct protoent
{
	char *p_name;
	char **p_aliases;
	int p_proto;
};

#define AI_PASSIVE		0x0001
#define AI_CANONNAME		0x0002
#define AI_NUMERICHOST		0x0004
#define AI_V4MAPPED		0x0008
#define AI_ALL			0x0010
#define AI_ADDRCONFIG		0x0020
#define AI_NUMERICSERV		0x0400

#define NI_NUMERICHOST		0x0001
#define NI_NUMERICSERV		0x0002
#define NI_NOFQDN		0x0004
#define NI_NAMEREQD		0x0008
#define NI_DGRAM		0x0010
#define NI_MAXHOST		1025
#define NI_MAXSERV		32

#define EAI_BADFLAGS		(-1)
#define EAI_NONAME		(-2)
#define EAI_AGAIN		(-3)
#define EAI_FAIL		(-4)
#define EAI_FAMILY		(-6)
#define EAI_SOCKTYPE		(-7)
#define EAI_SERVICE		(-8)
#define EAI_MEMORY		(-10)
#define EAI_SYSTEM		(-11)
#define EAI_OVERFLOW		(-12)
#define EAI_NODATA		(-5)
#define EAI_ADDRFAMILY		(-9)

#define HOST_NOT_FOUND		1
#define TRY_AGAIN		2
#define NO_RECOVERY		3
#define NO_DATA			4
#define NO_ADDRESS		NO_DATA

extern int h_errno;

int getaddrinfo (const char *__restrict, const char *__restrict, const struct addrinfo *__restrict,
		 struct addrinfo **__restrict);
void freeaddrinfo (struct addrinfo *);
const char *gai_strerror (int);
int getnameinfo (const struct sockaddr *__restrict, socklen_t, char *__restrict, socklen_t,
		 char *__restrict, socklen_t, int);
struct hostent *gethostbyname (const char *);
struct hostent *gethostbyaddr (const void *, socklen_t, int);
int gethostbyname_r (const char *__restrict, struct hostent *__restrict, char *__restrict, size_t,
		     struct hostent **__restrict, int *__restrict);
const char *hstrerror (int);
struct servent *getservbyname (const char *, const char *);
struct servent *getservbyport (int, const char *);
struct protoent *getprotobyname (const char *);
struct protoent *getprotobynumber (int);

#ifdef __cplusplus
}
#endif

#endif /* _NETDB_H */
