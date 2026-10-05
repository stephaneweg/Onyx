/*
 * unistd.h -- newlib's, plus the calls libonyxposix provides that newlib declares only for
 * _GNU_SOURCE (pipe2, dup3: a configure script finds them by linking, then the code must see
 * them).
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
#ifndef _ONYX_UNISTD_H
#define _ONYX_UNISTD_H

#include_next <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif
int pipe2 (int __fildes[2], int __flags);
int dup3 (int __fildes, int __fildes2, int __flags);
#ifdef __cplusplus
}
#endif

#endif /* _ONYX_UNISTD_H */
