/*
 * onyx_gthreads.c -- the out-of-line half of compat/bits/gthr-default.h: libstdc++'s thread layer
 * on the Onyx kapi v67 threads (docs/03 §5.2), for the SuperTuxKart port.
 *
 * Threads: kapi_thread_create with a 1 MB stack (STK's threads -- audio, loading, the network --
 * run ordinary C++), the gthread's void *(*) (void *) called through a small heap record.
 * Locks: the kapi_lock word (an exclusive swap, a yield while held). Timed waits: 1 ms sleeps
 * against gettimeofday (the gthreads deadlines are CLOCK_REALTIME timespecs).
 * Keys: a small table of (tid, value) pairs a key -- libstdc++ uses keys for little (not TLS).
 */
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include "appkit/appkit.h"
#include "stk/compat/bits/gthr-default.h"

#define STK_THREAD_STACK	(1024 * 1024)
#define ETIMEDOUT_		116
#define EAGAIN_			11
#define EINVAL_			22

typedef struct { void *(*fn) (void *); void *arg; } TStart;

static int thread_entry (void *p)
{
	TStart s = *(TStart *) p;
	free (p);
	s.fn (s.arg);
	return 0;
}

int __onyx_gthread_create (__gthread_t *t, void *(*fn) (void *), void *arg)
{
	TStart *s = (TStart *) malloc (sizeof *s);
	if (!s)
		return EAGAIN_;
	s->fn = fn;
	s->arg = arg;
	int tid = kapi_thread_create (thread_entry, s, STK_THREAD_STACK, "stk");
	if (tid < 0)
	{
		free (s);
		return EAGAIN_;
	}
	*t = tid;
	return 0;
}

int __onyx_gthread_join (__gthread_t t, void **ret)
{
	int code;
	if (ret)
		*ret = 0;
	return kapi_thread_join (t, KAPI_WAIT_FOREVER, &code) == 0 ? 0 : EINVAL_;
}

/* POSIX sleeps: libstdc++'s this_thread::__sleep_for (no nanosleep in this newlib) calls them. */
unsigned sleep (unsigned s) { kapi_msleep (s * 1000); return 0; }
int usleep (unsigned long us) { if (us < 1000) kapi_yield (); else kapi_msleep ((unsigned) (us / 1000)); return 0; }

int  __onyx_gthread_self (void) { return kapi_thread_self (); }
void __onyx_gthread_yield (void) { kapi_yield (); }
void __onyx_gthread_lock (volatile int *l) { kapi_lock (l); }

/* ms until an absolute CLOCK_REALTIME deadline (<= 0: passed) */
static long ms_until (const __gthread_time_t *abs)
{
	struct timeval now;
	gettimeofday (&now, 0);
	return (long) (abs->tv_sec - now.tv_sec) * 1000 + (abs->tv_nsec / 1000 - now.tv_usec) / 1000;
}

int __onyx_gthread_lock_until (volatile int *l, const __gthread_time_t *abs)
{
	while (__atomic_exchange_n (l, 1, __ATOMIC_ACQUIRE))
	{
		if (ms_until (abs) <= 0)
			return ETIMEDOUT_;
		kapi_msleep (1);
	}
	return 0;
}

/* The sequence is read before the mutex is released: a notify after that is never missed. */
int __onyx_gthread_cond_wait (__gthread_cond_t *c, __gthread_mutex_t *m, const __gthread_time_t *abs)
{
	unsigned seq = __atomic_load_n (&c->seq, __ATOMIC_ACQUIRE);
	int r = 0, spins = 0;
	__atomic_store_n (&m->lock, 0, __ATOMIC_RELEASE);
	while (__atomic_load_n (&c->seq, __ATOMIC_ACQUIRE) == seq)
	{
		if (abs && ms_until (abs) <= 0)
		{
			r = ETIMEDOUT_;
			break;
		}
		if (++spins < 8)
			kapi_yield ();		/* a quick hand-over between two threads */
		else
			kapi_msleep (1);
	}
	kapi_lock (&m->lock);
	return r;
}

int __onyx_gthread_cond_wait_recursive (__gthread_cond_t *c, __gthread_recursive_mutex_t *m)
{
	unsigned seq = __atomic_load_n (&c->seq, __ATOMIC_ACQUIRE);
	int self = m->owner, count = m->count;
	m->owner = 0;
	m->count = 0;
	__atomic_store_n (&m->lock, 0, __ATOMIC_RELEASE);
	while (__atomic_load_n (&c->seq, __ATOMIC_ACQUIRE) == seq)
		kapi_msleep (1);
	kapi_lock (&m->lock);
	m->owner = self;
	m->count = count;
	return 0;
}

/* ---- keys ---- */
#define MAX_KEYS	32
#define MAX_SLOTS	40		/* the kapi's 32 threads + the main one, with room */
typedef struct { int used; void (*dtor) (void *); int tid[MAX_SLOTS]; const void *val[MAX_SLOTS]; } TKey;
static TKey s_key[MAX_KEYS];
static volatile int s_keyLock;

int __onyx_gthread_key_create (__gthread_key_t *k, void (*dtor) (void *))
{
	kapi_lock (&s_keyLock);
	for (int i = 0; i < MAX_KEYS; i++)
		if (!s_key[i].used)
		{
			memset (&s_key[i], 0, sizeof s_key[i]);
			s_key[i].used = 1;
			s_key[i].dtor = dtor;	/* not called: a kapi thread's end is not hooked */
			*k = i;
			kapi_unlock (&s_keyLock);
			return 0;
		}
	kapi_unlock (&s_keyLock);
	return EAGAIN_;
}

int __onyx_gthread_key_delete (__gthread_key_t k)
{
	if (k < 0 || k >= MAX_KEYS)
		return EINVAL_;
	s_key[k].used = 0;
	return 0;
}

void *__onyx_gthread_getspecific (__gthread_key_t k)
{
	if (k < 0 || k >= MAX_KEYS)
		return 0;
	int self = kapi_thread_self ();
	for (int i = 0; i < MAX_SLOTS; i++)
		if (s_key[k].tid[i] == self)
			return (void *) s_key[k].val[i];
	return 0;
}

int __onyx_gthread_setspecific (__gthread_key_t k, const void *v)
{
	if (k < 0 || k >= MAX_KEYS)
		return EINVAL_;
	int self = kapi_thread_self (), freeSlot = -1;
	kapi_lock (&s_keyLock);
	for (int i = 0; i < MAX_SLOTS; i++)
	{
		if (s_key[k].tid[i] == self)
		{
			s_key[k].val[i] = v;
			kapi_unlock (&s_keyLock);
			return 0;
		}
		if (freeSlot < 0 && (s_key[k].tid[i] == 0 || !s_key[k].val[i]))
			freeSlot = i;
	}
	if (freeSlot < 0)
	{
		kapi_unlock (&s_keyLock);
		return EAGAIN_;
	}
	s_key[k].tid[freeSlot] = self;
	s_key[k].val[freeSlot] = v;
	kapi_unlock (&s_keyLock);
	return 0;
}
