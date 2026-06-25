/*
 * netinet/in.h -- minimal shim for the NetSurf port on Onyx (newlib). See sys/socket.h.
 * Provides the IPv4/IPv6 address types NetSurf's headers reference; the real transport is
 * the Onyx kapi (user/netsurf/onyx_fetch.c). Part of the brick-9 platform layer.
 */
#ifndef _ONYX_COMPAT_NETINET_IN_H
#define _ONYX_COMPAT_NETINET_IN_H 1

#include <stdint.h>
#include <sys/socket.h>

typedef uint16_t in_port_t;
typedef uint32_t in_addr_t;

struct in_addr {
	in_addr_t s_addr;
};

struct sockaddr_in {
	sa_family_t    sin_family;
	in_port_t      sin_port;
	struct in_addr sin_addr;
	char           sin_zero[8];
};

struct in6_addr {
	uint8_t s6_addr[16];
};

struct sockaddr_in6 {
	sa_family_t     sin6_family;
	in_port_t       sin6_port;
	uint32_t        sin6_flowinfo;
	struct in6_addr sin6_addr;
	uint32_t        sin6_scope_id;
};

#define INADDR_ANY       ((in_addr_t)0x00000000)
#define INADDR_LOOPBACK  ((in_addr_t)0x7f000001)
#define INET_ADDRSTRLEN  16
#define INET6_ADDRSTRLEN 46

#endif /* _ONYX_COMPAT_NETINET_IN_H */
