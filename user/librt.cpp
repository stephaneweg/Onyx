//
// librt.cpp -- the runtime of an Onyx shared library (user/lib.h, docs/SHARED-LIBS-PLAN.md): linked
// into every library (built -fPIC -fvisibility=hidden: nothing here is exported). It keeps the
// importer's table, runs the library's constructors once per process, and gives the library's code
// what a freestanding program gets from onyxpp.hpp and the Makefile's aliases: operator new /
// delete (over the importer's allocator: one heap in the process), the C memory primitives GCC
// emits calls to (over the kapi's), the few C++ runtime symbols.
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
#include "lib.h"

extern "C" {

typedef void (*lib_ctor_fn) (void);
extern lib_ctor_fn __lib_init_array_start[], __lib_init_array_end[];	// (lib.ld)

static struct TLibImports s_imp;		// this process's (the library's data is per process)
static int s_state;				// 0: not yet, 1: done

// A library's init calls this first -> 1: first call in this process (the constructors ran), 0:
// done already, < 0: the importer's table is unusable.
int onyx_lib_init (const struct TLibImports *imp)
{
	if (s_state != 0) return 0;
	if (imp == 0 || imp->size < __builtin_offsetof (struct TLibImports, data) || imp->alloc == 0 || imp->free == 0) return -1;
	s_imp.size = imp->size; s_imp.alloc = imp->alloc; s_imp.free = imp->free;
	if (imp->size >= sizeof (struct TLibImports)) { s_imp.ndata = imp->ndata; s_imp.data = imp->data; }
	s_state = 1;
	for (lib_ctor_fn *f = __lib_init_array_start; f < __lib_init_array_end; f++) (*f) ();
	return 1;
}

// The importer's i-th shared variable (TLibImports.data) -> 0: it has none there (an older program).
void *onyx_lib_data (unsigned i)	{ return i < s_imp.ndata && s_imp.data != 0 ? s_imp.data[i] : 0; }

// What the program gave this library (a library that opens another hands its allocator on: one heap).
const struct TLibImports *onyx_lib_imports (void)	{ return &s_imp; }
void *onyx_lib_alloc (lib_size_t n)	{ return s_imp.alloc (n); }
void onyx_lib_free (void *p)		{ if (p != 0) s_imp.free (p); }

// The C memory primitives (weak: a library with its own keeps them).
__attribute__ ((weak)) void *memset (void *d, int c, lib_size_t n)		{ return kapi_memset (d, c, n); }
__attribute__ ((weak)) void *memcpy (void *d, const void *s, lib_size_t n)	{ return kapi_memcpy (d, s, n); }
__attribute__ ((weak)) void *memmove (void *d, const void *s, lib_size_t n)	{ return kapi_memmove (d, s, n); }
__attribute__ ((weak)) int memcmp (const void *a, const void *b, lib_size_t n)
{
	const unsigned char *p = (const unsigned char *) a, *q = (const unsigned char *) b;
	for (; n != 0; n--, p++, q++) if (*p != *q) return (int) *p - (int) *q;
	return 0;
}

__attribute__ ((weak)) void __cxa_pure_virtual (void)				{ kapi_exit (127); for (;;) {} }
__attribute__ ((weak)) void *__dso_handle = 0;
__attribute__ ((weak)) int __cxa_atexit (void (*) (void *), void *, void *)	{ return 0; }
__attribute__ ((weak)) int atexit (void (*) (void))				{ return 0; }

}

__attribute__ ((weak)) void *operator new (lib_size_t n)			{ return s_imp.alloc (n); }
__attribute__ ((weak)) void *operator new[] (lib_size_t n)			{ return s_imp.alloc (n); }
__attribute__ ((weak)) void operator delete (void *p) noexcept			{ if (p != 0) s_imp.free (p); }
__attribute__ ((weak)) void operator delete[] (void *p) noexcept		{ if (p != 0) s_imp.free (p); }
__attribute__ ((weak)) void operator delete (void *p, lib_size_t) noexcept	{ if (p != 0) s_imp.free (p); }
__attribute__ ((weak)) void operator delete[] (void *p, lib_size_t) noexcept	{ if (p != 0) s_imp.free (p); }
