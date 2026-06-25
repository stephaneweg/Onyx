/*
 * sys/socket.h -- minimal BSD-sockets shim for the NetSurf port on Onyx (newlib).
 *
 * newlib (aarch64-none-elf) ships <sys/select.h> (fd_set) but no <sys/socket.h>. NetSurf's
 * utils/inet.h (pulled by content/fetch.h into every core TU) needs the sockets headers to
 * exist. Onyx does TCP through the kapi, not BSD sockets, so this provides just the TYPES
 * the NetSurf headers reference -- enough to compile. The actual transport is the Onyx
 * fetcher (user/netsurf/onyx_fetch.c). Part of the brick-9 platform layer.
 */
#ifndef _ONYX_COMPAT_SYS_SOCKET_H
#define _ONYX_COMPAT_SYS_SOCKET_H 1

#include <sys/types.h>
#include <sys/select.h>		/* fd_set, FD_* */

typedef unsigned int socklen_t;
typedef unsigned short sa_family_t;

#define AF_UNSPEC 0
#define AF_INET   2
#define AF_INET6  10
#define PF_INET   AF_INET

#define SOCK_STREAM 1
#define SOCK_DGRAM  2

struct sockaddr {
	sa_family_t sa_family;
	char        sa_data[14];
};

struct sockaddr_storage {
	sa_family_t ss_family;
	char        __ss_pad[126];
};

#endif /* _ONYX_COMPAT_SYS_SOCKET_H */
