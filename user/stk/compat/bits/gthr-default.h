/*
 * bits/gthr-default.h -- libstdc++'s thread layer ("gthreads") for Onyx, for the SuperTuxKart port.
 *
 * The Arm GNU toolchain's libstdc++ is configured --disable-threads: its gthr-default.h is
 * gthr-single.h and <mutex>, <condition_variable>, <thread> declare no std::mutex, no
 * std::condition_variable, and std::thread cannot start anything. SuperTuxKart and its Irrlicht use
 * all three (~80 std::mutex, ~30 std::thread, std::recursive_mutex throughout Irrlicht).
 *
 * This header replaces the toolchain's one (user/stk/compat comes first on the include path) and
 * the build defines _GLIBCXX_HAS_GTHREADS: libstdc++'s headers then see a POSIX-like thread layer,
 * implemented here on the Onyx kapi v67 threads (onyx_gthreads.c: kapi_thread_create / join, the
 * kapi_lock spin-and-yield lock). The few members libstdc++ normally compiles into its library
 * (std::thread::_M_start_thread, join, detach; std::condition_variable) are in onyx_gthreads_cxx.cpp.
 *
 * Mutexes and condition variables are plain words (no kapi handle: a process has only 256, and
 * STK makes many mutexes). A condition variable is a sequence number: notify bumps it, a waiter
 * sleeps in 1 ms steps until it changes (spurious wake-ups are allowed by the standard).
 * No TLS: _GLIBCXX_HAVE_TLS stays undefined (Onyx does not switch TPIDR_EL0 per thread yet), so
 * std::call_once takes libstdc++'s mutex-based path.
 */
#ifndef _ONYX_GTHR_DEFAULT_H
#define _ONYX_GTHR_DEFAULT_H

#define __GTHREADS 1
#define __GTHREADS_CXX0X 1
#define __GTHREAD_HAS_COND 1
#define _GTHREAD_USE_MUTEX_TIMEDLOCK 1

#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int __gthread_t;			/* the kapi tid (1 = the main thread) */
typedef int __gthread_key_t;
typedef struct { volatile int done, lock; } __gthread_once_t;
typedef struct { volatile int lock; } __gthread_mutex_t;
typedef struct { volatile int lock; volatile int owner; int count; } __gthread_recursive_mutex_t;
typedef struct { volatile unsigned seq; } __gthread_cond_t;
typedef struct timespec __gthread_time_t;

#define __GTHREAD_ONCE_INIT		{ 0, 0 }
#define __GTHREAD_MUTEX_INIT		{ 0 }
#define __GTHREAD_MUTEX_INIT_FUNCTION	__gthread_mutex_init_function
#define __GTHREAD_RECURSIVE_MUTEX_INIT	{ 0, 0, 0 }
#define __GTHREAD_COND_INIT		{ 0 }
#define __GTHREAD_TIME_INIT		{ 0, 0 }

/* onyx_gthreads.c */
int  __onyx_gthread_create (__gthread_t *t, void *(*fn) (void *), void *arg);
int  __onyx_gthread_join (__gthread_t t, void **ret);
int  __onyx_gthread_self (void);
void __onyx_gthread_yield (void);
void __onyx_gthread_lock (volatile int *l);
int  __onyx_gthread_lock_until (volatile int *l, const __gthread_time_t *abs);
int  __onyx_gthread_cond_wait (__gthread_cond_t *c, __gthread_mutex_t *m, const __gthread_time_t *abs);
int  __onyx_gthread_cond_wait_recursive (__gthread_cond_t *c, __gthread_recursive_mutex_t *m);
int  __onyx_gthread_key_create (__gthread_key_t *k, void (*dtor) (void *));
int  __onyx_gthread_key_delete (__gthread_key_t k);
void *__onyx_gthread_getspecific (__gthread_key_t k);
int  __onyx_gthread_setspecific (__gthread_key_t k, const void *v);

static inline int __gthread_active_p (void) { return 1; }

/* ---- threads ---- */
static inline int __gthread_create (__gthread_t *t, void *(*fn) (void *), void *arg)
	{ return __onyx_gthread_create (t, fn, arg); }
static inline int __gthread_join (__gthread_t t, void **ret) { return __onyx_gthread_join (t, ret); }
static inline int __gthread_detach (__gthread_t t) { (void) t; return 0; }	/* kapi: never joined = forgotten */
static inline int __gthread_equal (__gthread_t a, __gthread_t b) { return a == b; }
static inline __gthread_t __gthread_self (void) { return __onyx_gthread_self (); }
static inline int __gthread_yield (void) { __onyx_gthread_yield (); return 0; }

/* ---- once ---- */
static inline int __gthread_once (__gthread_once_t *o, void (*fn) (void))
{
	if (__atomic_load_n (&o->done, __ATOMIC_ACQUIRE))
		return 0;
	__onyx_gthread_lock (&o->lock);
	if (!o->done)
	{
		fn ();
		__atomic_store_n (&o->done, 1, __ATOMIC_RELEASE);
	}
	__atomic_store_n (&o->lock, 0, __ATOMIC_RELEASE);
	return 0;
}

/* ---- thread-specific keys ---- */
static inline int __gthread_key_create (__gthread_key_t *k, void (*dtor) (void *)) { return __onyx_gthread_key_create (k, dtor); }
static inline int __gthread_key_delete (__gthread_key_t k) { return __onyx_gthread_key_delete (k); }
static inline void *__gthread_getspecific (__gthread_key_t k) { return __onyx_gthread_getspecific (k); }
static inline int __gthread_setspecific (__gthread_key_t k, const void *v) { return __onyx_gthread_setspecific (k, v); }

/* ---- mutexes ---- */
static inline void __gthread_mutex_init_function (__gthread_mutex_t *m) { m->lock = 0; }
static inline int __gthread_mutex_destroy (__gthread_mutex_t *m) { (void) m; return 0; }
static inline int __gthread_mutex_lock (__gthread_mutex_t *m) { __onyx_gthread_lock (&m->lock); return 0; }
static inline int __gthread_mutex_trylock (__gthread_mutex_t *m)
	{ return __atomic_exchange_n (&m->lock, 1, __ATOMIC_ACQUIRE) ? 16 /* EBUSY */ : 0; }
static inline int __gthread_mutex_unlock (__gthread_mutex_t *m)
	{ __atomic_store_n (&m->lock, 0, __ATOMIC_RELEASE); return 0; }
static inline int __gthread_mutex_timedlock (__gthread_mutex_t *m, const __gthread_time_t *abs)
	{ return __onyx_gthread_lock_until (&m->lock, abs); }

static inline int __gthread_recursive_mutex_destroy (__gthread_recursive_mutex_t *m) { (void) m; return 0; }
static inline int __gthread_recursive_mutex_lock (__gthread_recursive_mutex_t *m)
{
	int self = __onyx_gthread_self ();
	if (m->owner == self) { m->count++; return 0; }
	__onyx_gthread_lock (&m->lock);
	m->owner = self; m->count = 1;
	return 0;
}
static inline int __gthread_recursive_mutex_trylock (__gthread_recursive_mutex_t *m)
{
	int self = __onyx_gthread_self ();
	if (m->owner == self) { m->count++; return 0; }
	if (__atomic_exchange_n (&m->lock, 1, __ATOMIC_ACQUIRE))
		return 16;
	m->owner = self; m->count = 1;
	return 0;
}
static inline int __gthread_recursive_mutex_timedlock (__gthread_recursive_mutex_t *m, const __gthread_time_t *abs)
{
	int self = __onyx_gthread_self ();
	if (m->owner == self) { m->count++; return 0; }
	int r = __onyx_gthread_lock_until (&m->lock, abs);
	if (r == 0) { m->owner = self; m->count = 1; }
	return r;
}
static inline int __gthread_recursive_mutex_unlock (__gthread_recursive_mutex_t *m)
{
	if (--m->count == 0)
	{
		m->owner = 0;
		__atomic_store_n (&m->lock, 0, __ATOMIC_RELEASE);
	}
	return 0;
}

/* ---- condition variables ---- */
static inline int __gthread_cond_destroy (__gthread_cond_t *c) { (void) c; return 0; }
static inline int __gthread_cond_signal (__gthread_cond_t *c)
	{ __atomic_add_fetch (&c->seq, 1, __ATOMIC_RELEASE); return 0; }	/* wakes them all: allowed */
static inline int __gthread_cond_broadcast (__gthread_cond_t *c)
	{ __atomic_add_fetch (&c->seq, 1, __ATOMIC_RELEASE); return 0; }
static inline int __gthread_cond_wait (__gthread_cond_t *c, __gthread_mutex_t *m)
	{ return __onyx_gthread_cond_wait (c, m, 0); }
static inline int __gthread_cond_timedwait (__gthread_cond_t *c, __gthread_mutex_t *m, const __gthread_time_t *abs)
	{ return __onyx_gthread_cond_wait (c, m, abs); }
static inline int __gthread_cond_wait_recursive (__gthread_cond_t *c, __gthread_recursive_mutex_t *m)
	{ return __onyx_gthread_cond_wait_recursive (c, m); }

#ifdef __cplusplus
}
#endif

#endif /* _ONYX_GTHR_DEFAULT_H */
