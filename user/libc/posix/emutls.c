/*
 * emutls.c -- __thread / thread_local under the interim toolchain (libonyxposix, option (a) of
 * docs/POSIX-PLAN.md §2.2).
 *
 * aarch64-none-elf's GCC is configured --disable-tls: a __thread variable is an "emulated TLS"
 * control object (__emutls_v.<name>) and every access calls __emutls_get_address. libgcc's
 * version, built for "single" threads, keeps ONE copy for the process. This one, linked before
 * libgcc, keeps a vector per thread in its block (struct __onyx_thread): an object's index is
 * given once (under a lock), its copy is allocated at the thread's first access (the
 * initialiser's image, or zeros) and freed when the thread ends (pthread.c). C++ thread_local
 * destructors are libstdc++'s business (__cxa_thread_atexit; single-threaded under (a)).
 *
 * Under WP-TC's aarch64-onyx-elf (--enable-tls) nothing calls this: TLS is native (tls.c).
 * Code on an app core shares the main thread's copies (posix_internal.h, __onyx_self).
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
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <stdint.h>
#include "posix_internal.h"

/* libgcc's layout (libgcc/emutls.c) */
struct __emutls_object
{
	size_t size;
	size_t align;
	union
	{
		uintptr_t offset;		/* the index + 1 (0: not given yet) */
		void *ptr;
	} loc;
	void *templ;
};


static volatile unsigned s_lock;
static uintptr_t s_count;

void *__emutls_get_address (void *o)
{
	struct __emutls_object *obj = (struct __emutls_object *) o;
	uintptr_t idx = __atomic_load_n (&obj->loc.offset, __ATOMIC_ACQUIRE);
	if (idx == 0)
	{
		__onyx_lock (&s_lock);
		idx = obj->loc.offset;
		if (idx == 0)
		{
			idx = ++s_count;
			__atomic_store_n (&obj->loc.offset, idx, __ATOMIC_RELEASE);
		}
		__onyx_unlock (&s_lock);
	}

	struct __onyx_thread *t = __onyx_self ();
	if (idx > t->emutls_n)
	{
		unsigned long n = t->emutls_n ? t->emutls_n * 2 : 16;
		while (n < idx)
			n *= 2;
		void **v = (void **) realloc (t->emutls, n * sizeof (void *));
		if (v == 0)
			abort ();			/* as libgcc: no way to report it */
		memset (v + t->emutls_n, 0, (n - t->emutls_n) * sizeof (void *));
		t->emutls = v;
		t->emutls_n = n;
	}
	void *p = t->emutls[idx - 1];
	if (p == 0)
	{
		size_t align = obj->align < sizeof (void *) ? sizeof (void *) : obj->align;
		p = memalign (align, obj->size ? obj->size : 1);
		if (p == 0)
			abort ();
		if (obj->templ)
			memcpy (p, obj->templ, obj->size);
		else
			memset (p, 0, obj->size);
		t->emutls[idx - 1] = p;
	}
	return p;
}

void __emutls_register_common (void *o, size_t size, size_t align, void *templ)
{
	struct __emutls_object *obj = (struct __emutls_object *) o;
	if (obj->size < size)
	{
		obj->size = size;
		obj->templ = 0;
	}
	if (obj->align < align)
		obj->align = align;
	if (templ != 0 && size == obj->size)
		obj->templ = templ;
}

/* A thread ends: its copies go (after its key destructors ran). */
void __onyx_emutls_free (struct __onyx_thread *t)
{
	if (t->emutls == 0)
		return;
	for (unsigned long i = 0; i < t->emutls_n; i++)
		free (t->emutls[i]);
	free (t->emutls);
	t->emutls = 0;
	t->emutls_n = 0;
}
