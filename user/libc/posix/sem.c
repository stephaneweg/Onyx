/*
 * sem.c -- POSIX unnamed semaphores (libonyxposix): the count is the futex word.
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
#include <semaphore.h>
#include <errno.h>
#include "posix_internal.h"

#define LOAD(p)		__atomic_load_n ((p), __ATOMIC_ACQUIRE)

int sem_init (sem_t *s, int pshared, unsigned value)
{
	(void) pshared;
	if (value > SEM_VALUE_MAX)
		return ONYX_ERR (EINVAL);
	s->__value = value;
	s->__waiters = 0;
	return 0;
}

int sem_destroy (sem_t *s) { (void) s; return 0; }

static int sem_take (sem_t *s, int mode, clockid_t clock, const struct timespec *abs)
{
	for (;;)
	{
		unsigned v = LOAD (&s->__value);
		if (v > 0)
		{
			if (__atomic_compare_exchange_n (&s->__value, &v, v - 1, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
				return 0;
			continue;
		}
		if (mode == 1)
			return ONYX_ERR (EAGAIN);
		unsigned ms = KAPI_WAIT_FOREVER;
		if (abs)
		{
			if (abs->tv_nsec < 0 || abs->tv_nsec >= 1000000000L)
				return ONYX_ERR (EINVAL);
			long long left = __onyx_ms_until (clock, abs);
			if (left < 0)
				return ONYX_ERR (ETIMEDOUT);
			ms = (unsigned) left;
		}
		__atomic_add_fetch (&s->__waiters, 1, __ATOMIC_ACQ_REL);
		__onyx_futex_wait (&s->__value, 0, ms);
		__atomic_sub_fetch (&s->__waiters, 1, __ATOMIC_ACQ_REL);
	}
}

int sem_wait (sem_t *s) { return sem_take (s, 0, CLOCK_REALTIME, 0); }
int sem_trywait (sem_t *s) { return sem_take (s, 1, CLOCK_REALTIME, 0); }
int sem_timedwait (sem_t *s, const struct timespec *abs) { return sem_take (s, 2, CLOCK_REALTIME, abs); }
int sem_clockwait (sem_t *s, clockid_t c, const struct timespec *abs) { return sem_take (s, 2, c, abs); }

int sem_post (sem_t *s)
{
	unsigned v = LOAD (&s->__value);
	do
	{
		if (v >= SEM_VALUE_MAX)
			return ONYX_ERR (EOVERFLOW);
	}
	while (!__atomic_compare_exchange_n (&s->__value, &v, v + 1, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE));
	if (LOAD (&s->__waiters) != 0)
		__onyx_futex_wake (&s->__value);
	return 0;
}

int sem_getvalue (sem_t *s, int *v) { *v = (int) LOAD (&s->__value); return 0; }

sem_t *sem_open (const char *name, int flags, ...) { (void) name; (void) flags; errno = ENOSYS; return SEM_FAILED; }
int sem_close (sem_t *s) { (void) s; return ONYX_ERR (ENOSYS); }
int sem_unlink (const char *name) { (void) name; return ONYX_ERR (ENOSYS); }
