/*
 * guard.c -- thread-safe C++ function-local statics under the interim toolchain (libonyxposix,
 * option (a) of docs/POSIX-PLAN.md §2.2).
 *
 * g++ guards a function-local static's initialisation with __cxa_guard_acquire / release /
 * abort. libsupc++'s, built for "single" threads, takes no lock: two threads may both run the
 * constructor. These, linked before libsupc++, follow the Itanium ABI (the guard's first byte:
 * initialised) and keep a state word in the guard's second half: 0 free, 1 running, 2 running
 * with waiters (the futex). A recursive initialisation deadlocks instead of throwing.
 * Under WP-TC's toolchain libsupc++'s own (gthreads-aware) guards are fine and these are not
 * linked.
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
#include <stdint.h>
#include "posix_internal.h"

typedef uint64_t __guard;

static inline volatile unsigned char *done_byte (__guard *g) { return (volatile unsigned char *) g; }
static inline volatile unsigned *state_word (__guard *g) { return (volatile unsigned *) g + 1; }

int __cxa_guard_acquire (__guard *g);
void __cxa_guard_release (__guard *g);
void __cxa_guard_abort (__guard *g);

int __cxa_guard_acquire (__guard *g)
{
	if (__atomic_load_n (done_byte (g), __ATOMIC_ACQUIRE))
		return 0;
	volatile unsigned *s = state_word (g);
	for (;;)
	{
		unsigned expect = 0;
		if (__atomic_compare_exchange_n (s, &expect, 1, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
		{
			if (__atomic_load_n (done_byte (g), __ATOMIC_ACQUIRE))
			{
				__atomic_store_n (s, 0, __ATOMIC_RELEASE);
				return 0;
			}
			return 1;			/* the caller initialises */
		}
		if (expect == 1)
		{
			unsigned one = 1;
			__atomic_compare_exchange_n (s, &one, 2, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
		}
		__onyx_futex_wait (s, 2, KAPI_WAIT_FOREVER);
		if (__atomic_load_n (done_byte (g), __ATOMIC_ACQUIRE))
			return 0;
	}
}

void __cxa_guard_release (__guard *g)
{
	__atomic_store_n (done_byte (g), 1, __ATOMIC_RELEASE);
	if (__atomic_exchange_n (state_word (g), 0, __ATOMIC_ACQ_REL) == 2)
		__onyx_futex_wake (state_word (g));
}

void __cxa_guard_abort (__guard *g)
{
	if (__atomic_exchange_n (state_word (g), 0, __ATOMIC_ACQ_REL) == 2)
		__onyx_futex_wake (state_word (g));
}
