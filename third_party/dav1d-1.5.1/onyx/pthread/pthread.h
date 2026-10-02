/*
 * third_party/dav1d-1.5.1/onyx/pthread/pthread.h -- the threads dav1d asks for, on the Pi
 * (aarch64-none-elf + newlib: no pthreads). user/av opens dav1d with n_threads = 1 and
 * max_frame_delay = 1: it decodes on the caller's thread and never waits on a condition, so the
 * mutexes and conditions are no-ops and pthread_create fails (dav1d then stays single-threaded).
 * Everything is static inline under dav1d-private names: nothing is exported to the rest of Jet.
 * Found before newlib's <pthread.h> (-I of this directory, the Pi's build only).
 */
#ifndef ONYX_DAV1D_PTHREAD_H
#define ONYX_DAV1D_PTHREAD_H
#include <errno.h>
#include <stddef.h>

typedef struct { int unused; } onyx_d_pthread_t;
typedef struct { size_t stack; } onyx_d_pthread_attr_t;
typedef struct { int locked; } onyx_d_pthread_mutex_t;
typedef struct { int unused; } onyx_d_pthread_cond_t;
typedef struct { int done; } onyx_d_pthread_once_t;

#define pthread_t onyx_d_pthread_t
#define pthread_attr_t onyx_d_pthread_attr_t
#define pthread_mutex_t onyx_d_pthread_mutex_t
#define pthread_cond_t onyx_d_pthread_cond_t
#define pthread_once_t onyx_d_pthread_once_t
#define PTHREAD_MUTEX_INITIALIZER { 0 }
#define PTHREAD_ONCE_INIT { 0 }

static inline int onyx_d_attr_init(pthread_attr_t *a) { a->stack = 0; return 0; }
static inline int onyx_d_attr_destroy(pthread_attr_t *a) { (void) a; return 0; }
static inline int onyx_d_attr_setstacksize(pthread_attr_t *a, size_t s) { a->stack = s; return 0; }
static inline int onyx_d_create(pthread_t *t, const pthread_attr_t *a, void *(*f)(void *), void *arg)
{ (void) t; (void) a; (void) f; (void) arg; return EAGAIN; }
static inline int onyx_d_join(pthread_t t, void **r) { (void) t; (void) r; return 0; }
static inline int onyx_d_mutex_init(pthread_mutex_t *m, const void *a) { (void) a; m->locked = 0; return 0; }
static inline int onyx_d_mutex_destroy(pthread_mutex_t *m) { (void) m; return 0; }
static inline int onyx_d_mutex_lock(pthread_mutex_t *m) { m->locked = 1; return 0; }
static inline int onyx_d_mutex_unlock(pthread_mutex_t *m) { m->locked = 0; return 0; }
static inline int onyx_d_cond_init(pthread_cond_t *c, const void *a) { (void) c; (void) a; return 0; }
static inline int onyx_d_cond_destroy(pthread_cond_t *c) { (void) c; return 0; }
static inline int onyx_d_cond_wait(pthread_cond_t *c, pthread_mutex_t *m) { (void) c; (void) m; return 0; }
static inline int onyx_d_cond_signal(pthread_cond_t *c) { (void) c; return 0; }
static inline int onyx_d_cond_broadcast(pthread_cond_t *c) { (void) c; return 0; }
static inline int onyx_d_once(pthread_once_t *o, void (*f)(void))
{ if (!o->done) { o->done = 1; f(); } return 0; }
static inline pthread_t onyx_d_self(void) { pthread_t t = { 0 }; return t; }

#define pthread_attr_init onyx_d_attr_init
#define pthread_attr_destroy onyx_d_attr_destroy
#define pthread_attr_setstacksize onyx_d_attr_setstacksize
#define pthread_create onyx_d_create
#define pthread_join onyx_d_join
#define pthread_mutex_init onyx_d_mutex_init
#define pthread_mutex_destroy onyx_d_mutex_destroy
#define pthread_mutex_lock onyx_d_mutex_lock
#define pthread_mutex_unlock onyx_d_mutex_unlock
#define pthread_cond_init onyx_d_cond_init
#define pthread_cond_destroy onyx_d_cond_destroy
#define pthread_cond_wait onyx_d_cond_wait
#define pthread_cond_signal onyx_d_cond_signal
#define pthread_cond_broadcast onyx_d_cond_broadcast
#define pthread_once onyx_d_once
#define pthread_self onyx_d_self
#endif
