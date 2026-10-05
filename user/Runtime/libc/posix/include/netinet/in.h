/*
 * netinet/in.h -- Internet addresses on Onyx (libonyxposix; Linux layouts). IPv4 only in the
 * kernel: the IPv6 types exist for code that names them, an AF_INET6 socket is EAFNOSUPPORT.
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
#ifndef _NETINET_IN_H
#define _NETINET_IN_H

#include <sys/types.h>
#include <stdint.h>
#include <sys/socket.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint16_t in_port_t;
typedef uint32_t in_addr_t;

struct in_addr
{
	in_addr_t s_addr;			/* network byte order */
};

struct sockaddr_in				/* 16 bytes */
{
	sa_family_t sin_family;
	in_port_t sin_port;			/* network byte order */
	struct in_addr sin_addr;
	unsigned char sin_zero[8];
};

struct in6_addr
{
	union
	{
		uint8_t __s6_addr[16];
		uint16_t __s6_addr16[8];
		uint32_t __s6_addr32[4];
	} __in6_u;
};
#define s6_addr		__in6_u.__s6_addr
#define s6_addr16	__in6_u.__s6_addr16
#define s6_addr32	__in6_u.__s6_addr32

struct sockaddr_in6				/* 28 bytes */
{
	sa_family_t sin6_family;
	in_port_t sin6_port;
	uint32_t sin6_flowinfo;
	struct in6_addr sin6_addr;
	uint32_t sin6_scope_id;
};

struct ip_mreq
{
	struct in_addr imr_multiaddr;
	struct in_addr imr_interface;
};

extern const struct in6_addr in6addr_any;
extern const struct in6_addr in6addr_loopback;
#define IN6ADDR_ANY_INIT	{ { { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 } } }
#define IN6ADDR_LOOPBACK_INIT	{ { { 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1 } } }

#define IPPROTO_IP	0
#define IPPROTO_ICMP	1
#define IPPROTO_TCP	6
#define IPPROTO_UDP	17
#define IPPROTO_IPV6	41
#define IPPROTO_ICMPV6	58
#define IPPROTO_RAW	255

#define INADDR_ANY		((in_addr_t) 0x00000000)
#define INADDR_BROADCAST	((in_addr_t) 0xFFFFFFFF)
#define INADDR_NONE		((in_addr_t) 0xFFFFFFFF)
#define INADDR_LOOPBACK		((in_addr_t) 0x7F000001)	/* host byte order */

#define INET_ADDRSTRLEN		16
#define INET6_ADDRSTRLEN	46

#define IP_TOS			1
#define IP_TTL			2
#define IP_MULTICAST_IF		32
#define IP_MULTICAST_TTL	33
#define IP_MULTICAST_LOOP	34
#define IP_ADD_MEMBERSHIP	35
#define IP_DROP_MEMBERSHIP	36
#define IPV6_V6ONLY		26

#define IN6_IS_ADDR_UNSPECIFIED(a) \
	((a)->s6_addr32[0] == 0 && (a)->s6_addr32[1] == 0 && (a)->s6_addr32[2] == 0 && (a)->s6_addr32[3] == 0)
#define IN6_IS_ADDR_LOOPBACK(a) \
	((a)->s6_addr32[0] == 0 && (a)->s6_addr32[1] == 0 && (a)->s6_addr32[2] == 0 && \
	 (a)->s6_addr32[3] == __builtin_bswap32 (1))
#define IN6_IS_ADDR_V4MAPPED(a) \
	((a)->s6_addr32[0] == 0 && (a)->s6_addr32[1] == 0 && (a)->s6_addr32[2] == __builtin_bswap32 (0xFFFF))
#define IN6_IS_ADDR_LINKLOCAL(a)	(((a)->s6_addr[0] == 0xFE) && (((a)->s6_addr[1] & 0xC0) == 0x80))
#define IN6_IS_ADDR_MULTICAST(a)	((a)->s6_addr[0] == 0xFF)

/* byte order (AArch64 is little-endian): functions (socket.c), and macros as glibc */
#ifndef __onyx_byteorder_defined
#define __onyx_byteorder_defined
uint32_t htonl (uint32_t);
uint16_t htons (uint16_t);
uint32_t ntohl (uint32_t);
uint16_t ntohs (uint16_t);
#define htonl(x)	__builtin_bswap32 ((uint32_t) (x))
#define ntohl(x)	__builtin_bswap32 ((uint32_t) (x))
#define htons(x)	__builtin_bswap16 ((uint16_t) (x))
#define ntohs(x)	__builtin_bswap16 ((uint16_t) (x))
#endif

#ifdef __cplusplus
}
#endif

#endif /* _NETINET_IN_H */
