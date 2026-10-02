/*
 * endian.h -- byte order (glibc's names over newlib's <sys/endian.h>). AArch64: little-endian.
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
#ifndef _ONYX_ENDIAN_H
#define _ONYX_ENDIAN_H

#include <sys/endian.h>

#ifndef __LITTLE_ENDIAN
#define __LITTLE_ENDIAN	1234
#define __BIG_ENDIAN	4321
#define __PDP_ENDIAN	3412
#endif
#ifndef __BYTE_ORDER
#define __BYTE_ORDER	__LITTLE_ENDIAN
#endif

#endif /* _ONYX_ENDIAN_H */
