/*
 * sys/un.h -- AF_UNIX addresses. Onyx has no Unix-domain sockets: socket (AF_UNIX) fails with
 * EAFNOSUPPORT; socketpair (AF_UNIX, SOCK_STREAM) gives two pipes (libonyxposix socket.c).
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
#ifndef _SYS_UN_H
#define _SYS_UN_H

#include <sys/socket.h>

struct sockaddr_un
{
	sa_family_t sun_family;
	char sun_path[108];
};

#endif /* _SYS_UN_H */
