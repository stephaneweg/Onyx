/*
 * netinet/tcp.h -- TCP socket options on Onyx: accepted and ignored by setsockopt (Circle's TCP
 * has no Nagle switch nor keep-alive tuning).
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
#ifndef _NETINET_TCP_H
#define _NETINET_TCP_H

#define TCP_NODELAY		1
#define TCP_MAXSEG		2
#define TCP_KEEPIDLE		4
#define TCP_KEEPINTVL		5
#define TCP_KEEPCNT		6
#define TCP_FASTOPEN		23
#define TCP_FASTOPEN_CONNECT	30

#endif /* _NETINET_TCP_H */
