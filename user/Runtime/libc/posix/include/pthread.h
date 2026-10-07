/*
 * pthread.h -- POSIX threads on Onyx (libonyxposix, docs/POSIX-PLAN.md §3.4; docs/03 §5.4).
 *
 * Threads are the kernel's (kapi v67 threads; v75 thread_create_ex with the stack size and the
 * TLS pointer), all of a process on core 0. Synchronisation is user space: a compare-and-swap,
 * and the kernel's futex (wait_word / wake_word) when a thread must sleep. Replaces newlib's
 * <pthread.h>, which declares nothing on this target. Part of the WP-TC toolchain overlay: GCC's
 * gthr-posix.h reads it (with the _POSIX_* options of <sys/features.h>).
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
#ifndef _PTHREAD_H
#define _PTHREAD_H

#include <sys/features.h>			/* the _POSIX_* options gthr-posix.h tests */
#include <sys/types.h>				/* the types: <sys/_pthreadtypes.h> */
#include <sched.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- constants (frozen with the types) ---- */
#define PTHREAD_CREATE_JOINABLE		0
#define PTHREAD_CREATE_DETACHED		1

#define PTHREAD_INHERIT_SCHED		0
#define PTHREAD_EXPLICIT_SCHED		1

#define PTHREAD_SCOPE_SYSTEM		0
#define PTHREAD_SCOPE_PROCESS		1

#define PTHREAD_PROCESS_PRIVATE		0
#define PTHREAD_PROCESS_SHARED		1

#define PTHREAD_MUTEX_NORMAL		0
#define PTHREAD_MUTEX_RECURSIVE		1
#define PTHREAD_MUTEX_ERRORCHECK	2
#define PTHREAD_MUTEX_DEFAULT		PTHREAD_MUTEX_NORMAL
#define PTHREAD_MUTEX_FAST_NP		PTHREAD_MUTEX_NORMAL
#define PTHREAD_MUTEX_RECURSIVE_NP	PTHREAD_MUTEX_RECURSIVE
#define PTHREAD_MUTEX_ERRORCHECK_NP	PTHREAD_MUTEX_ERRORCHECK

#define PTHREAD_MUTEX_STALLED		0
#define PTHREAD_MUTEX_ROBUST		1

#define PTHREAD_PRIO_NONE		0
#define PTHREAD_PRIO_INHERIT		1
#define PTHREAD_PRIO_PROTECT		2

#define PTHREAD_CANCEL_ENABLE		0
#define PTHREAD_CANCEL_DISABLE		1
#define PTHREAD_CANCEL_DEFERRED		0
#define PTHREAD_CANCEL_ASYNCHRONOUS	1
#define PTHREAD_CANCELED		((void *) -1)

#define PTHREAD_BARRIER_SERIAL_THREAD	(-1)

#define PTHREAD_STACK_MIN		16384		/* the kernel's minimum (16 KB) */
#define PTHREAD_KEYS_MAX		128
#define PTHREAD_DESTRUCTOR_ITERATIONS	4
#define PTHREAD_THREADS_MAX		32		/* running besides the main one (the kernel's) */

#define PTHREAD_MUTEX_INITIALIZER		{ 0, PTHREAD_MUTEX_NORMAL, 0, 0 }
#define PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP	{ 0, PTHREAD_MUTEX_RECURSIVE, 0, 0 }
#define PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP	{ 0, PTHREAD_MUTEX_ERRORCHECK, 0, 0 }
#define PTHREAD_COND_INITIALIZER		{ 0, 0, 0, 0 }
#define PTHREAD_RWLOCK_INITIALIZER		{ 0, 0, 0, 0 }
#define PTHREAD_ONCE_INIT			{ 0 }

/* ---- threads ---- */
int pthread_create (pthread_t *__restrict, const pthread_attr_t *__restrict,
		    void *(*) (void *), void *__restrict);
int pthread_join (pthread_t, void **);
int pthread_tryjoin_np (pthread_t, void **);
int pthread_timedjoin_np (pthread_t, void **, const struct timespec *);
int pthread_detach (pthread_t);
void pthread_exit (void *) __attribute__ ((__noreturn__));
pthread_t pthread_self (void);
int pthread_equal (pthread_t, pthread_t);
int pthread_yield (void);
int pthread_setname_np (pthread_t, const char *);
int pthread_getname_np (pthread_t, char *, size_t);
int pthread_getattr_np (pthread_t, pthread_attr_t *);
int pthread_getcpuclockid (pthread_t, clockid_t *);
int pthread_setschedparam (pthread_t, int, const struct sched_param *);
int pthread_getschedparam (pthread_t, int *__restrict, struct sched_param *__restrict);
int pthread_setschedprio (pthread_t, int);
int pthread_setconcurrency (int);
int pthread_getconcurrency (void);
int pthread_atfork (void (*) (void), void (*) (void), void (*) (void));
/* pthread_kill / pthread_sigmask: <signal.h> */

/* cancellation: not supported (pthread_cancel -> ENOSYS); the state calls are accepted */
int pthread_cancel (pthread_t);
int pthread_setcancelstate (int, int *);
int pthread_setcanceltype (int, int *);
void pthread_testcancel (void);

struct __onyx_cleanup
{
	void (*__routine) (void *);
	void *__arg;
	struct __onyx_cleanup *__prev;
};
void __onyx_cleanup_push (struct __onyx_cleanup *, void (*) (void *), void *);
void __onyx_cleanup_pop (struct __onyx_cleanup *, int);
#define pthread_cleanup_push(routine, arg) \
	do { struct __onyx_cleanup __onyx_cb; __onyx_cleanup_push (&__onyx_cb, (routine), (arg));
#define pthread_cleanup_pop(execute) \
	__onyx_cleanup_pop (&__onyx_cb, (execute)); } while (0)

/* ---- attributes ---- */
int pthread_attr_init (pthread_attr_t *);
int pthread_attr_destroy (pthread_attr_t *);
int pthread_attr_setdetachstate (pthread_attr_t *, int);
int pthread_attr_getdetachstate (const pthread_attr_t *, int *);
int pthread_attr_setstacksize (pthread_attr_t *, size_t);
int pthread_attr_getstacksize (const pthread_attr_t *__restrict, size_t *__restrict);
int pthread_attr_setstack (pthread_attr_t *, void *, size_t);
int pthread_attr_getstack (const pthread_attr_t *__restrict, void **__restrict, size_t *__restrict);
int pthread_attr_setstackaddr (pthread_attr_t *, void *);
int pthread_attr_getstackaddr (const pthread_attr_t *__restrict, void **__restrict);
int pthread_attr_setguardsize (pthread_attr_t *, size_t);
int pthread_attr_getguardsize (const pthread_attr_t *__restrict, size_t *__restrict);
int pthread_attr_setschedparam (pthread_attr_t *__restrict, const struct sched_param *__restrict);
int pthread_attr_getschedparam (const pthread_attr_t *__restrict, struct sched_param *__restrict);
int pthread_attr_setschedpolicy (pthread_attr_t *, int);
int pthread_attr_getschedpolicy (const pthread_attr_t *__restrict, int *__restrict);
int pthread_attr_setinheritsched (pthread_attr_t *, int);
int pthread_attr_getinheritsched (const pthread_attr_t *__restrict, int *__restrict);
int pthread_attr_setscope (pthread_attr_t *, int);
int pthread_attr_getscope (const pthread_attr_t *__restrict, int *__restrict);

/* ---- mutexes ---- */
int pthread_mutex_init (pthread_mutex_t *__restrict, const pthread_mutexattr_t *__restrict);
int pthread_mutex_destroy (pthread_mutex_t *);
int pthread_mutex_lock (pthread_mutex_t *);
int pthread_mutex_trylock (pthread_mutex_t *);
int pthread_mutex_timedlock (pthread_mutex_t *__restrict, const struct timespec *__restrict);
int pthread_mutex_clocklock (pthread_mutex_t *__restrict, clockid_t, const struct timespec *__restrict);
int pthread_mutex_unlock (pthread_mutex_t *);
int pthread_mutex_consistent (pthread_mutex_t *);
int pthread_mutexattr_init (pthread_mutexattr_t *);
int pthread_mutexattr_destroy (pthread_mutexattr_t *);
int pthread_mutexattr_settype (pthread_mutexattr_t *, int);
int pthread_mutexattr_gettype (const pthread_mutexattr_t *__restrict, int *__restrict);
int pthread_mutexattr_setpshared (pthread_mutexattr_t *, int);
int pthread_mutexattr_getpshared (const pthread_mutexattr_t *__restrict, int *__restrict);
int pthread_mutexattr_setprotocol (pthread_mutexattr_t *, int);
int pthread_mutexattr_getprotocol (const pthread_mutexattr_t *__restrict, int *__restrict);
int pthread_mutexattr_setrobust (pthread_mutexattr_t *, int);
int pthread_mutexattr_getrobust (const pthread_mutexattr_t *__restrict, int *__restrict);

/* ---- condition variables ---- */
int pthread_cond_init (pthread_cond_t *__restrict, const pthread_condattr_t *__restrict);
int pthread_cond_destroy (pthread_cond_t *);
int pthread_cond_wait (pthread_cond_t *__restrict, pthread_mutex_t *__restrict);
int pthread_cond_timedwait (pthread_cond_t *__restrict, pthread_mutex_t *__restrict,
			    const struct timespec *__restrict);
int pthread_cond_clockwait (pthread_cond_t *__restrict, pthread_mutex_t *__restrict, clockid_t,
			    const struct timespec *__restrict);
int pthread_cond_signal (pthread_cond_t *);
int pthread_cond_broadcast (pthread_cond_t *);
int pthread_condattr_init (pthread_condattr_t *);
int pthread_condattr_destroy (pthread_condattr_t *);
int pthread_condattr_setclock (pthread_condattr_t *, clockid_t);
int pthread_condattr_getclock (const pthread_condattr_t *__restrict, clockid_t *__restrict);
int pthread_condattr_setpshared (pthread_condattr_t *, int);
int pthread_condattr_getpshared (const pthread_condattr_t *__restrict, int *__restrict);

/* ---- once, keys ---- */
int pthread_once (pthread_once_t *, void (*) (void));
int pthread_key_create (pthread_key_t *, void (*) (void *));
int pthread_key_delete (pthread_key_t);
void *pthread_getspecific (pthread_key_t);
int pthread_setspecific (pthread_key_t, const void *);

/* ---- reader-writer locks ---- */
int pthread_rwlock_init (pthread_rwlock_t *__restrict, const pthread_rwlockattr_t *__restrict);
int pthread_rwlock_destroy (pthread_rwlock_t *);
int pthread_rwlock_rdlock (pthread_rwlock_t *);
int pthread_rwlock_tryrdlock (pthread_rwlock_t *);
int pthread_rwlock_timedrdlock (pthread_rwlock_t *__restrict, const struct timespec *__restrict);
int pthread_rwlock_clockrdlock (pthread_rwlock_t *__restrict, clockid_t, const struct timespec *__restrict);
int pthread_rwlock_wrlock (pthread_rwlock_t *);
int pthread_rwlock_trywrlock (pthread_rwlock_t *);
int pthread_rwlock_timedwrlock (pthread_rwlock_t *__restrict, const struct timespec *__restrict);
int pthread_rwlock_clockwrlock (pthread_rwlock_t *__restrict, clockid_t, const struct timespec *__restrict);
int pthread_rwlock_unlock (pthread_rwlock_t *);
int pthread_rwlockattr_init (pthread_rwlockattr_t *);
int pthread_rwlockattr_destroy (pthread_rwlockattr_t *);
int pthread_rwlockattr_setpshared (pthread_rwlockattr_t *, int);
int pthread_rwlockattr_getpshared (const pthread_rwlockattr_t *__restrict, int *__restrict);

/* ---- barriers, spin locks ---- */
int pthread_barrier_init (pthread_barrier_t *__restrict, const pthread_barrierattr_t *__restrict, unsigned);
int pthread_barrier_destroy (pthread_barrier_t *);
int pthread_barrier_wait (pthread_barrier_t *);
int pthread_barrierattr_init (pthread_barrierattr_t *);
int pthread_barrierattr_destroy (pthread_barrierattr_t *);
int pthread_barrierattr_setpshared (pthread_barrierattr_t *, int);
int pthread_barrierattr_getpshared (const pthread_barrierattr_t *__restrict, int *__restrict);
int pthread_spin_init (pthread_spinlock_t *, int);
int pthread_spin_destroy (pthread_spinlock_t *);
int pthread_spin_lock (pthread_spinlock_t *);
int pthread_spin_trylock (pthread_spinlock_t *);
int pthread_spin_unlock (pthread_spinlock_t *);

#ifdef __cplusplus
}
#endif

#endif /* _PTHREAD_H */
