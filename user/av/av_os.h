/*
 * user/av/av_os.h -- the media library's few platform calls: a thread, a lock, a nap, a clock, a file.
 *
 * On Onyx (and the PC builds that link a kapi: the NetSurf bench's fakekapi, Windows' winkapi)
 * the kapi's threads (v67) and its user-space lock; with AV_POSIX (the standalone tools:
 * tools/tests/av) pthreads. Locks are short (a queue's pointers): kapi_lock spins / yields.
 */
#ifndef ONYX_AV_OS_H
#define ONYX_AV_OS_H

#include <stdint.h>

#ifdef AV_POSIX
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

typedef pthread_mutex_t av_lock_t;
#define AV_LOCK_INIT PTHREAD_MUTEX_INITIALIZER
static inline void av_lock_init(av_lock_t *l) { pthread_mutex_init(l, 0); }
static inline void av_lock(av_lock_t *l) { pthread_mutex_lock(l); }
static inline void av_unlock(av_lock_t *l) { pthread_mutex_unlock(l); }
static inline void av_sleep_ms(unsigned ms) { usleep(ms * 1000); }

typedef struct { pthread_t t; int ok; } av_thread_t;
struct av__tstart { int (*fn)(void *); void *arg; };
static void *av__trampoline(void *p)
{
	struct av__tstart s = *(struct av__tstart *) p;
	free(p);
	s.fn(s.arg);
	return 0;
}
static inline int av_thread_start(av_thread_t *t, int (*fn)(void *), void *arg, const char *name)
{
	struct av__tstart *s = (struct av__tstart *) malloc(sizeof *s);
	(void) name;
	if (!s) return -1;
	s->fn = fn; s->arg = arg;
	t->ok = pthread_create(&t->t, 0, av__trampoline, s) == 0;
	if (!t->ok) free(s);
	return t->ok ? 0 : -1;
}
static inline void av_thread_join(av_thread_t *t) { if (t->ok) pthread_join(t->t, 0); t->ok = 0; }
/* a file read in pieces (the player's file mode) */
typedef FILE *av_file_t;
static inline av_file_t av_file_open(const char *path) { return fopen(path, "rb"); }
static inline int64_t av_file_size(av_file_t f)
{
	long n;
	fseek(f, 0, SEEK_END);
	n = ftell(f);
	fseek(f, 0, SEEK_SET);
	return n;
}
static inline long av_file_read_at(av_file_t f, int64_t pos, void *buf, size_t n)
{
	if (fseek(f, (long) pos, SEEK_SET) != 0)
		return -1;
	return (long) fread(buf, 1, n, f);
}
static inline void av_file_close(av_file_t f) { fclose(f); }
static inline int64_t av_os_now_us(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t) ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

#else	/* the kapi */
#include "appkit/appkit.h"
#if !defined(__aarch64__) || defined(ONYX_HOST_SIM) || defined(_WIN32)
#include <time.h>
#endif

typedef volatile int av_lock_t;
#define AV_LOCK_INIT 0
static inline void av_lock_init(av_lock_t *l) { *l = 0; }
static inline void av_lock(av_lock_t *l) { kapi_lock(l); }
static inline void av_unlock(av_lock_t *l) { kapi_unlock(l); }
static inline void av_sleep_ms(unsigned ms) { kapi_msleep(ms); }

typedef struct { int tid; } av_thread_t;
static inline int av_thread_start(av_thread_t *t, int (*fn)(void *), void *arg, const char *name)
{
	/* 1 MB: the decoders run on these threads (dav1d's assembly keeps blocks on the stack: it
	 * gives its own threads 1 MB; libopus' VAR_ARRAYS) */
	t->tid = kapi_thread_create(fn, arg, 1024 * 1024, name);
	return t->tid >= 2 ? 0 : -1;
}
static inline void av_thread_join(av_thread_t *t)
{
	if (t->tid >= 2) kapi_thread_join(t->tid, KAPI_WAIT_FOREVER, 0);
	t->tid = 0;
}
/* a file read in pieces through the kapi (any volume; newlib's fopen would load it whole) */
typedef void *av_file_t;
static inline av_file_t av_file_open(const char *path) { return kapi_open(path); }
static inline int64_t av_file_size(av_file_t f) { return (int64_t) kapi_fsize64(f); }
static inline long av_file_read_at(av_file_t f, int64_t pos, void *buf, size_t n)
{
	if (kapi_seek(f, (unsigned long long) pos) != 0)
		return -1;
	return kapi_read(f, buf, (unsigned) n);
}
static inline void av_file_close(av_file_t f) { kapi_close(f); }
static inline int64_t av_os_now_us(void)
{
#if defined(__aarch64__) && !defined(ONYX_HOST_SIM) && !defined(_WIN32)
	/* the ARM counter (kapi_clock_us's, without its 32-bit wrap) */
	unsigned long c, f;
	__asm__ volatile ("isb\n\tmrs %0, cntpct_el0\n\tmrs %1, cntfrq_el0" : "=r" (c), "=r" (f));
	return (int64_t) (c / f) * 1000000 + (int64_t) ((c % f) * 1000000 / f);
#else
	/* the PC builds: the stand-in kernels' ticks are 10 ms -- the host's clock */
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t) ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
#endif
}
#endif

#endif
