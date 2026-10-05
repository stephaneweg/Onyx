/*
 * net/if.h -- network interfaces: Onyx has one ("wlan0"); if_nametoindex / if_indextoname know
 * it, the interface ioctls are ENOTTY (libonyxposix misc.c).
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
#ifndef _NET_IF_H
#define _NET_IF_H

#include <sys/socket.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IF_NAMESIZE	16
#define IFNAMSIZ	IF_NAMESIZE

#define IFF_UP		0x1
#define IFF_BROADCAST	0x2
#define IFF_LOOPBACK	0x8
#define IFF_RUNNING	0x40
#define IFF_MULTICAST	0x1000

struct if_nameindex
{
	unsigned if_index;
	char *if_name;
};

struct ifreq
{
	char ifr_name[IFNAMSIZ];
	union
	{
		struct sockaddr ifru_addr;
		short ifru_flags;
		int ifru_ivalue;
		char ifru_pad[24];
	} ifr_ifru;
};
#define ifr_addr	ifr_ifru.ifru_addr
#define ifr_flags	ifr_ifru.ifru_flags

unsigned if_nametoindex (const char *);
char *if_indextoname (unsigned, char *);
struct if_nameindex *if_nameindex (void);
void if_freenameindex (struct if_nameindex *);

#ifdef __cplusplus
}
#endif

#endif /* _NET_IF_H */
