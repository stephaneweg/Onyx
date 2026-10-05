//
// ftso.c -- FreeType as a shared library (SD:/lib/ft.so; docs/SHARED-LIBS-PLAN.md): what the library
// takes from a C library. Its memory is the importer's (user/Runtime/lib.h TLibImports, through librt.cpp):
// one heap in the process. FreeType's file streams (FT_New_Face by path) are not used on Onyx --
// the apps read a font file and call FT_New_Memory_Face (ft/fonts.h) --: fopen fails. The rest
// (the string functions, qsort, setjmp) comes from the toolchain's newlib, linked into the library.
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
#include <stdio.h>
#include <string.h>
#include "lib.h"

int onyx_lib_init (const struct TLibImports *imp);	// (librt.cpp)
void *onyx_lib_alloc (lib_size_t n);
void onyx_lib_free (void *p);

// The table's init: once per process.
int ft_lib_init (const struct TLibImports *imp)
{
	return onyx_lib_init (imp) < 0 ? -1 : 0;
}

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

// No file streams (see above).
FILE *fopen (const char *path, const char *mode)		{ (void) path; (void) mode; return 0; }
int fclose (FILE *f)						{ (void) f; return -1; }
size_t fread (void *p, size_t s, size_t n, FILE *f)		{ (void) p; (void) s; (void) n; (void) f; return 0; }
int fseek (FILE *f, long o, int w)				{ (void) f; (void) o; (void) w; return -1; }
long ftell (FILE *f)						{ (void) f; return -1; }
