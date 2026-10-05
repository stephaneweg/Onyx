/*
 * byteswap.h -- glibc's byte swaps, on GCC's builtins.
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
#ifndef _BYTESWAP_H
#define _BYTESWAP_H

#define bswap_16(x)	__builtin_bswap16 (x)
#define bswap_32(x)	__builtin_bswap32 (x)
#define bswap_64(x)	__builtin_bswap64 (x)

#endif /* _BYTESWAP_H */
