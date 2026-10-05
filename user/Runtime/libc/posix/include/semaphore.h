/*
 * semaphore.h -- POSIX unnamed semaphores on Onyx (libonyxposix, sem.c): a counter and the
 * kernel's futex. Named semaphores (sem_open) are not supported (ENOSYS). Part of the WP-TC
 * toolchain overlay (the sem_t layout is frozen with the pthread types).
 *
 * ---------------------------------------------------------------------------------------------
 * MIT License
 *
 * Copyright (c) 2026 Stéphane Wegener and the Onyx contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 * and associated documentation files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
 * BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 * ---------------------------------------------------------------------------------------------
 */
#ifndef _SEMAPHORE_H
#define _SEMAPHORE_H

#include <sys/types.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct					/* 8 bytes */
{
	volatile unsigned __value;		/* 0: the count (the futex word) */
	volatile unsigned __waiters;		/* 4 */
} sem_t;

#define SEM_FAILED	((sem_t *) 0)
#define SEM_VALUE_MAX	0x7FFFFFFF

int sem_init (sem_t *, int, unsigned);
int sem_destroy (sem_t *);
int sem_wait (sem_t *);
int sem_trywait (sem_t *);
int sem_timedwait (sem_t *__restrict, const struct timespec *__restrict);
int sem_clockwait (sem_t *__restrict, clockid_t, const struct timespec *__restrict);
int sem_post (sem_t *);
int sem_getvalue (sem_t *__restrict, int *__restrict);
sem_t *sem_open (const char *, int, ...);
int sem_close (sem_t *);
int sem_unlink (const char *);

#ifdef __cplusplus
}
#endif

#endif /* _SEMAPHORE_H */
