//
// ikso.c -- ImageKit as a shared library (SD:/lib/imagekit.so): what its code takes
// from a C library. Its memory is the importer's (user/lib.h TLibImports, through librt.cpp): one
// heap in the process -- malloc and newlib's own _malloc_r family both go there. No file streams
// (the files are read through the kapi). The string functions, qsort, snprintf and the maths come
// from the toolchain's newlib, linked into the library.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. Permission is hereby
// granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions: The above copyright notice and this permission notice shall
// be included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS
// IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
//
#include <stddef.h>
#include <string.h>
#include <reent.h>
#include "lib.h"

void *onyx_lib_alloc (lib_size_t n);
void onyx_lib_free (void *p);

// (the table's init, ik_lib_init: ikcore.cpp)

// malloc / realloc / free over the importer's allocator: a block remembers its size (realloc).
#define HDR	16				// (keeps the 16-byte alignment)

void *malloc (size_t n)
{
	size_t *p = (size_t *) onyx_lib_alloc (n + HDR);
	if (p == 0) return 0;
	p[0] = n;
	return (char *) p + HDR;
}
void free (void *p)
{
	if (p != 0) onyx_lib_free ((char *) p - HDR);
}
void *realloc (void *p, size_t n)
{
	if (p == 0) return malloc (n);
	if (n == 0) { free (p); return 0; }
	size_t old = *(size_t *) ((char *) p - HDR);
	if (n <= old) return p;
	void *q = malloc (n);
	if (q == 0) return 0;
	memcpy (q, p, old);
	free (p);
	return q;
}
void *calloc (size_t a, size_t b)
{
	void *p = malloc (a * b);
	if (p != 0) memset (p, 0, a * b);
	return p;
}
// (newlib's own allocations: snprintf's buffers)
void *_malloc_r (struct _reent *r, size_t n)			{ (void) r; return malloc (n); }
void _free_r (struct _reent *r, void *p)			{ (void) r; free (p); }
void *_realloc_r (struct _reent *r, void *p, size_t n)		{ (void) r; return realloc (p, n); }
void *_calloc_r (struct _reent *r, size_t a, size_t b)		{ (void) r; return calloc (a, b); }

// assert (the codecs'): the program ends, with a line in the kernel log.
void __assert_func (const char *file, int line, const char *func, const char *expr)
{
	(void) file; (void) line; (void) func;
	static const char m[] = "imagekit: assertion failed: ";
	kapi_write (1, m, sizeof m - 1);
	if (expr != 0) kapi_write (1, expr, (unsigned) strlen (expr));
	kapi_exit (134);
	for (;;) {}
}
