/*
 * arpa/inet.h -- address conversions on Onyx (libonyxposix netdb.c).
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
#ifndef _ARPA_INET_H
#define _ARPA_INET_H

#include <netinet/in.h>

#ifdef __cplusplus
extern "C" {
#endif

in_addr_t inet_addr (const char *);
int inet_aton (const char *, struct in_addr *);
char *inet_ntoa (struct in_addr);
const char *inet_ntop (int, const void *__restrict, char *__restrict, socklen_t);
int inet_pton (int, const char *__restrict, void *__restrict);
in_addr_t inet_network (const char *);

#ifdef __cplusplus
}
#endif

#endif /* _ARPA_INET_H */
