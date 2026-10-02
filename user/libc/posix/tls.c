/*
 * tls.c -- thread blocks, TPIDR_EL0 and errno (libonyxposix, docs/POSIX-PLAN.md §3.4).
 *
 * The AArch64 TLS layout, "variant 1": TPIDR_EL0 (the thread pointer, TP) points at a 16-byte TCB
 * and the thread's static TLS block follows it, at TP + 16 rounded up to the TLS segment's
 * alignment -- where the linker's local-exec offsets expect it. libonyxposix keeps its own
 * thread control block (struct __onyx_thread) just BELOW the TCB, so pthread_self () is
 * TP - sizeof (struct __onyx_thread): no system call. A thread's block is one allocation:
 *
 *     [ ... | struct __onyx_thread | TCB (16) | pad | .tdata image | .tbss zeros ]
 *                                  ^ TP
 *
 * The main thread's block is static (or reserved in .bss by onyx-posix.ld when the program has a
 * PT_TLS segment); crt0posix sets TP before any other C code runs. A new thread gets its TP from
 * the kernel (v75 thread_create_ex's `tls`), and sets it again itself (the v67 fallback).
 * The kernel keeps TPIDR_EL0 per task (TaskSwitch saves it), and never writes it.
 *
 * Under the interim toolchain (aarch64-none-elf, --disable-tls) the program has no PT_TLS:
 * __thread goes through emutls (emutls.c keeps a per-thread vector in the block). Under WP-TC's
 * aarch64-onyx-elf it is native TLS, laid out here.
 *
 * errno: newlib 4.4 is built without _REENT_THREAD_LOCAL, so its errno is _impure_ptr->_errno,
 * one for the process. __errno () is overridden: the main thread keeps newlib's (so a
 * single-threaded program behaves exactly as before), every other thread gets its own. What
 * newlib sets through its reentrancy structure directly (__errno_r (ptr): strtol's ERANGE, the
 * maths functions) still lands in the shared one -- a worker thread does not see it. Every errno
 * libonyxposix sets goes through __errno (). Fully per-thread errno needs WP-TC's newlib
 * (--enable-newlib-reent-thread-local).
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
#include <reent.h>
#include "posix_internal.h"

/* The TLS segment, from onyx-posix.ld (weak: 0 when the program has none, or is linked with
 * another script). */
extern char __onyx_tdata_start[] __attribute__ ((weak));
extern char __onyx_tdata_size[] __attribute__ ((weak));	/* absolute symbols: their address */
extern char __onyx_tls_size[] __attribute__ ((weak));	/* is the value */
extern char __onyx_tls_align[] __attribute__ ((weak));
extern char __onyx_main_tls_area[] __attribute__ ((weak));
extern char __onyx_main_tls_reserve[] __attribute__ ((weak));

#define TCB_SIZE	16

static inline unsigned long tls_size (void) { return (unsigned long) __onyx_tls_size; }
static inline unsigned long tdata_size (void) { return (unsigned long) __onyx_tdata_size; }
static inline unsigned long tls_align (void)
{
	unsigned long a = (unsigned long) __onyx_tls_align;
	return a < 16 ? 16 : a;
}
static inline unsigned long align_up (unsigned long v, unsigned long a) { return (v + a - 1) & ~(a - 1); }

/* The main thread's block when the program has no TLS segment. */
static struct
{
	struct __onyx_thread t;
	unsigned char tcb[TCB_SIZE];
} __attribute__ ((aligned (64))) s_main =
{
	.t = { .self = &s_main.t, .magic = ONYX_THREAD_MAGIC, .tid = 1, .is_main = 1,
	       .name = "main" },
};

struct __onyx_thread *__onyx_main_thread = &s_main.t;

static volatile unsigned s_listLock;
static struct __onyx_thread *s_list = &s_main.t;

unsigned long __onyx_thread_tp (struct __onyx_thread *t)
{
	return (unsigned long) t + sizeof (struct __onyx_thread);
}

static void copy_tls_image (unsigned long tp)
{
	unsigned long n = tls_size ();
	if (n == 0)
		return;
	unsigned char *block = (unsigned char *) tp + align_up (TCB_SIZE, tls_align ());
	unsigned long d = tdata_size ();
	if (d)
		memcpy (block, __onyx_tdata_start, d);
	memset (block + d, 0, n - d);
}

/* crt0posix, first thing: the main thread's TP. No errno, no malloc, no TLS before it. */
void __onyx_tls_init_main (void)
{
	struct __onyx_thread *t = &s_main.t;
	unsigned long n = tls_size ();
	if (n != 0 && __onyx_main_tls_area != 0)
	{
		/* the area onyx-posix.ld reserved: [struct | TCB | pad | TLS block] */
		unsigned long a = tls_align ();
		unsigned long need = align_up (sizeof (struct __onyx_thread), a) + align_up (TCB_SIZE, a) + n + a;
		if (need <= (unsigned long) __onyx_main_tls_reserve)
		{
			unsigned long base = align_up ((unsigned long) __onyx_main_tls_area, a);
			unsigned long tp = base + align_up (sizeof (struct __onyx_thread), a);
			t = (struct __onyx_thread *) (tp - sizeof (struct __onyx_thread));
			memcpy (t, &s_main.t, sizeof *t);
			t->self = t;
			s_list = t;
			__onyx_main_thread = t;
		}
	}
	unsigned long tp = __onyx_thread_tp (t);
	((void **) tp)[0] = t;				/* the TCB: (dtv) -> the block, for a debugger */
	((void **) tp)[1] = 0;
	copy_tls_image (tp);
#if defined(__aarch64__)
	__asm__ volatile ("msr tpidr_el0, %0" :: "r" (tp) : "memory");
#endif
}

/* A new thread's block (zeroed, its TLS image copied), or 0. */
struct __onyx_thread *__onyx_thread_alloc (void)
{
	unsigned long a = tls_align ();
	unsigned long p = align_up (sizeof (struct __onyx_thread), a);
	unsigned long size = p + align_up (TCB_SIZE, a) + tls_size ();
	unsigned char *block = (unsigned char *) memalign (a, size);
	if (block == 0)
		return 0;
	memset (block, 0, size);
	struct __onyx_thread *t = (struct __onyx_thread *) (block + p - sizeof (struct __onyx_thread));
	t->self = t;
	t->magic = ONYX_THREAD_MAGIC;
	t->block = block;
	unsigned long tp = __onyx_thread_tp (t);
	((void **) tp)[0] = t;
	copy_tls_image (tp);
	return t;
}

void __onyx_thread_free (struct __onyx_thread *t)
{
	if (t == 0 || t->block == 0)
		return;
	t->magic = 0;
	free (t->block);
}

void __onyx_thread_list_lock (void) { __onyx_lock (&s_listLock); }
void __onyx_thread_list_unlock (void) { __onyx_unlock (&s_listLock); }
struct __onyx_thread *__onyx_thread_list_first (void) { return s_list; }

void __onyx_thread_list_add (struct __onyx_thread *t)
{
	__onyx_lock (&s_listLock);
	t->next = s_list;
	s_list = t;
	__onyx_unlock (&s_listLock);
}

void __onyx_thread_list_remove (struct __onyx_thread *t)
{
	__onyx_lock (&s_listLock);
	for (struct __onyx_thread **pp = &s_list; *pp != 0; pp = &(*pp)->next)
		if (*pp == t)
		{
			*pp = t->next;
			break;
		}
	__onyx_unlock (&s_listLock);
}

/* ---- errno ---- */
int *__errno (void)
{
	struct __onyx_thread *t = __onyx_self ();
	if (t == 0 || t->is_main)
		return &_REENT->_errno;
	return &t->err;
}
