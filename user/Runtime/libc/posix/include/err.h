/*
 * err.h -- BSD's err / warn family (libonyxposix misc.c): "<program>: <message>[: <errno text>]"
 * on stderr.
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
#ifndef _ERR_H
#define _ERR_H

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

void err (int, const char *, ...) __attribute__ ((__noreturn__, __format__ (__printf__, 2, 3)));
void errx (int, const char *, ...) __attribute__ ((__noreturn__, __format__ (__printf__, 2, 3)));
void warn (const char *, ...) __attribute__ ((__format__ (__printf__, 1, 2)));
void warnx (const char *, ...) __attribute__ ((__format__ (__printf__, 1, 2)));
void verr (int, const char *, va_list) __attribute__ ((__noreturn__));
void verrx (int, const char *, va_list) __attribute__ ((__noreturn__));
void vwarn (const char *, va_list);
void vwarnx (const char *, va_list);

#ifdef __cplusplus
}
#endif

#endif /* _ERR_H */
