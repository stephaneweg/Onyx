/*
 * sys/_pthreadtypes.h -- the pthread types of Onyx (libonyxposix, docs/POSIX-PLAN.md §3.4).
 *
 * Replaces newlib's (whose types are 32-bit object ids for an RTOS). <sys/types.h> includes it.
 * The layouts below are an ABI: once the aarch64-onyx-elf toolchain (WP-TC) builds libgcc and
 * libstdc++ against this header (its overlay, §2.2 (b)), they are FROZEN -- a change means
 * rebuilding the toolchain. Every object is plain memory with a zero initializer: the
 * implementation (user/Runtime/libc/posix/pthread*.c) is a futex word (kapi wait_word / wake_word) and a
 * compare-and-swap fast path.
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
#ifndef _SYS__PTHREADTYPES_H_
#define _SYS__PTHREADTYPES_H_

#include <sys/sched.h>				/* struct sched_param */

/* A thread: the address of its control block (struct __onyx_thread, just below its TCB). */
typedef unsigned long pthread_t;

typedef struct					/* 48 bytes */
{
	int __flags;				/*  0: 1 = initialised */
	int __detachstate;			/*  4: PTHREAD_CREATE_* */
	unsigned long __stacksize;		/*  8: 0 = the default (8 MB) */
	void *__stackaddr;			/* 16: (pthread_attr_setstack: ENOTSUP) */
	unsigned long __guardsize;		/* 24: kept, not used (the kernel's guard) */
	int __schedpolicy;			/* 32: SCHED_* */
	int __inheritsched;			/* 36: PTHREAD_*_SCHED */
	int __priority;				/* 40: sched_param.sched_priority (> 0: "real time") */
	int __scope;				/* 44: PTHREAD_SCOPE_* */
} pthread_attr_t;

typedef struct					/* 16 bytes */
{
	volatile unsigned __lock;		/*  0: 0 free, 1 locked, 2 locked with waiters */
	int __type;				/*  4: PTHREAD_MUTEX_* */
	volatile int __owner;			/*  8: the owner's id (recursive / errorcheck) */
	int __count;				/* 12: recursion depth */
} pthread_mutex_t;

typedef struct					/* 8 bytes */
{
	int __type;				/* PTHREAD_MUTEX_* */
	int __pshared;				/* PTHREAD_PROCESS_* (kept) */
} pthread_mutexattr_t;

typedef struct					/* 16 bytes */
{
	volatile unsigned __seq;		/*  0: bumped by signal / broadcast (the futex word) */
	int __clock;				/*  4: the timedwait clock (0 = CLOCK_REALTIME) */
	volatile unsigned __waiters;		/*  8 */
	int __pad;				/* 12 */
} pthread_cond_t;

typedef struct					/* 8 bytes */
{
	int __clock;				/* clockid_t (0 = CLOCK_REALTIME) */
	int __pshared;
} pthread_condattr_t;

typedef unsigned pthread_key_t;			/* 0 .. PTHREAD_KEYS_MAX - 1 */

typedef struct					/* 4 bytes */
{
	volatile int __state;			/* 0 not run, 1 running, 2 done, 3 running + waiters */
} pthread_once_t;

typedef struct					/* 16 bytes */
{
	volatile int __state;			/*  0: -1 a writer, n > 0 readers, 0 free */
	volatile unsigned __seq;		/*  4: the futex word (bumped at each unlock) */
	volatile unsigned __waiters;		/*  8 */
	volatile unsigned __wwait;		/* 12: writers waiting (they go first) */
} pthread_rwlock_t;

typedef struct					/* 8 bytes */
{
	int __pshared;
	int __pad;
} pthread_rwlockattr_t;

typedef struct					/* 16 bytes */
{
	volatile unsigned __seq;		/*  0: the round (the futex word) */
	volatile unsigned __in;			/*  4: arrived in this round */
	unsigned __count;			/*  8 */
	int __pad;				/* 12 */
} pthread_barrier_t;

typedef struct					/* 4 bytes */
{
	int __pshared;
} pthread_barrierattr_t;

typedef volatile int pthread_spinlock_t;

#endif /* _SYS__PTHREADTYPES_H_ */
