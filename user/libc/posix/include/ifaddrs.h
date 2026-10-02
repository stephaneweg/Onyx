/*
 * ifaddrs.h -- getifaddrs on Onyx: ENOSYS (a stub; the address is in kapi net_status).
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
#ifndef _IFADDRS_H
#define _IFADDRS_H

#include <sys/socket.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ifaddrs
{
	struct ifaddrs *ifa_next;
	char *ifa_name;
	unsigned ifa_flags;
	struct sockaddr *ifa_addr;
	struct sockaddr *ifa_netmask;
	struct sockaddr *ifa_broadaddr;
	void *ifa_data;
};
#define ifa_dstaddr	ifa_broadaddr

int getifaddrs (struct ifaddrs **);
void freeifaddrs (struct ifaddrs *);

#ifdef __cplusplus
}
#endif

#endif /* _IFADDRS_H */
