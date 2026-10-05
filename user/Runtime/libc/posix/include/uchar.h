/*
 * uchar.h -- C11 Unicode characters (libonyxposix's overlay: newlib has no <uchar.h>; ICU's C
 * headers include it). char16_t / char32_t (and C23's char8_t) for C (C++ has them built in);
 * mbrtoc16, c16rtomb, mbrtoc32, c32rtomb (and mbrtoc8, c8rtomb) in uchar.c. Onyx's multibyte
 * encoding is always UTF-8, whatever the locale: these convert UTF-8 <-> UTF-16 / UTF-32.
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
#ifndef _ONYX_UCHAR_H
#define _ONYX_UCHAR_H

#define __need_size_t
#include <stddef.h>
#include <wchar.h>		/* mbstate_t */

#ifndef __cplusplus
typedef __CHAR16_TYPE__ char16_t;
typedef __CHAR32_TYPE__ char32_t;
#if defined (__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
typedef unsigned char char8_t;
#endif
#endif

#define __STDC_UTF_16__ 1
#define __STDC_UTF_32__ 1

#ifdef __cplusplus
extern "C" {
#endif

size_t mbrtoc16 (char16_t *__restrict __pc16, const char *__restrict __s, size_t __n,
		 mbstate_t *__restrict __ps);
size_t c16rtomb (char *__restrict __s, char16_t __c16, mbstate_t *__restrict __ps);
size_t mbrtoc32 (char32_t *__restrict __pc32, const char *__restrict __s, size_t __n,
		 mbstate_t *__restrict __ps);
size_t c32rtomb (char *__restrict __s, char32_t __c32, mbstate_t *__restrict __ps);
#if (defined (__STDC_VERSION__) && __STDC_VERSION__ >= 202311L) || defined (__cpp_char8_t)
size_t mbrtoc8 (char8_t *__restrict __pc8, const char *__restrict __s, size_t __n,
		mbstate_t *__restrict __ps);
size_t c8rtomb (char *__restrict __s, char8_t __c8, mbstate_t *__restrict __ps);
#endif

#ifdef __cplusplus
}
#endif

#endif /* _ONYX_UCHAR_H */
