/*
 * pthread.c -- POSIX threads over the Onyx kernel threads (libonyxposix, docs/POSIX-PLAN.md
 * §3.4): create / join / detach / exit / self, attributes, keys, names, scheduling, the stubs.
 *
 * pthread_create allocates the thread's block (tls.c: struct __onyx_thread + TCB + TLS image)
 * and starts a kernel thread on it: kapi thread_create_ex (v75: the stack size, lazy, up to
 * 16 MB; the TLS pointer; the name), or, on a kernel without it, kapi thread_create (v67; the
 * stack is mapped at once, so the default is 1 MB there) and the start routine sets TPIDR_EL0
 * itself. The kernel thread is always joinable: a joiner sleeps on the block's state word (a
 * futex), then joins the kernel thread (it is ending) and frees the block. A detached thread's
 * block is freed by a later pthread_create (or pthread_detach) once it has ended. 32 threads may
 * run besides the main one (the kernel's limit): pthread_create -> EAGAIN beyond.
 *
 * The end of a thread: its cleanup handlers, its keys' destructors (4 rounds), its emutls
 * copies, then the result and the state word (joiners woken), then kapi_thread_exit. The main
 * thread's pthread_exit waits for the others, then exit (0). Cancellation is not supported
 * (pthread_cancel -> ENOSYS).
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
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "posix_internal.h"

#define LOAD(p)		__atomic_load_n ((p), __ATOMIC_ACQUIRE)
#define STORE(p, v)	__atomic_store_n ((p), (v), __ATOMIC_RELEASE)

static inline struct __onyx_thread *T (pthread_t t) { return (struct __onyx_thread *) t; }

static int valid (pthread_t t)
{
	if (t == 0)
		return 0;
	int ok = 0;
	__onyx_thread_list_lock ();
	for (struct __onyx_thread *p = __onyx_thread_list_first (); p != 0; p = p->next)
		if (p == T (t))
		{
			ok = p->magic == ONYX_THREAD_MAGIC;
			break;
		}
	__onyx_thread_list_unlock ();
	return ok;
}

/* ---- stack bounds ---- */
static void stack_bounds (struct __onyx_thread *t, int tid)
{
	struct kapi_thread_info ti;
	if (kapi_thread_info (tid, &ti) == 0 && ti.stack_hi > ti.stack_lo)
	{
		t->stack_lo = ti.stack_lo;
		t->stack_hi = ti.stack_hi;
		t->guard = ti.guard;
		return;
	}
	unsigned long sp;
	__asm__ volatile ("mov %0, sp" : "=r" (sp));
	if (t->is_main)
	{
		/* (kern/layout.h: USER_STACK_TOP 16 GB, USER_STACK_SIZE 1 MB by default -- the app's own
		   size may be larger: the bounds given are the part sure to be there) */
		unsigned long long top = 16ULL << 30;
		if (sp >= top || sp < top - (64ULL << 20))
			top = (sp + ONYX_PAGE - 1) & ~(ONYX_PAGE - 1);	/* (another layout) */
		t->stack_hi = top;
		t->stack_lo = top - (1ULL << 20);
		t->guard = 0;
		return;
	}
	/* a v67 thread: its stack top is 64 KB-aligned, the SP starts just below it */
	t->stack_hi = (sp + ONYX_PAGE - 1) & ~(ONYX_PAGE - 1);
	t->stack_lo = t->stack_hi - t->stack_size;
	t->guard = ONYX_PAGE;
}

/* ---- the end of a thread ---- */
static void run_cleanups (struct __onyx_thread *t)
{
	while (t->cleanup)
	{
		struct __onyx_cleanup *c = t->cleanup;
		t->cleanup = c->__prev;
		c->__routine (c->__arg);
	}
}

__attribute__ ((noreturn)) static void thread_finish (struct __onyx_thread *t, void *result)
{
	run_cleanups (t);
	__onyx_keys_run_destructors (t);
	__onyx_emutls_free (t);
	t->result = result;
	STORE (&t->state, ONYX_T_FINISHED);
	__onyx_futex_wake ((volatile unsigned *) &t->state);
	/* nothing of the block is touched from here: a joiner may free it once the kernel
	   thread is gone */
	kapi_thread_exit (0);
	for (;;)
		;
}

static int thread_entry (void *p)
{
	struct __onyx_thread *t = (struct __onyx_thread *) p;
	unsigned long tp = __onyx_thread_tp (t);
	__asm__ volatile ("msr tpidr_el0, %0" :: "r" (tp) : "memory");
	t->tid = kapi_thread_self ();
	stack_bounds (t, 0);
	thread_finish (t, t->start (t->arg));
}

/* the blocks of detached threads that ended */
static void reap_detached (void)
{
	for (;;)
	{
		struct __onyx_thread *dead = 0;
		__onyx_thread_list_lock ();
		for (struct __onyx_thread *p = __onyx_thread_list_first (); p != 0; p = p->next)
			if (!p->is_main && p->detached && LOAD (&p->state) == ONYX_T_FINISHED)
			{
				dead = p;
				break;
			}
		__onyx_thread_list_unlock ();
		if (dead == 0)
			return;
		int code;
		kapi_thread_join (dead->tid, KAPI_WAIT_FOREVER, &code);	/* (it is ending) */
		__onyx_thread_list_remove (dead);
		__onyx_thread_free (dead);
	}
}

int pthread_create (pthread_t *out, const pthread_attr_t *attr, void *(*fn) (void *), void *arg)
{
	if (fn == 0)
		return EINVAL;
	reap_detached ();

	struct __onyx_thread *t = __onyx_thread_alloc ();
	if (t == 0)
		return EAGAIN;
	t->start = fn;
	t->arg = arg;
	t->detached = attr != 0 && attr->__detachstate == PTHREAD_CREATE_DETACHED;
	t->prio = attr != 0 && attr->__inheritsched == PTHREAD_EXPLICIT_SCHED && attr->__priority > 0;
	unsigned long ss = attr != 0 ? attr->__stacksize : 0;
	strcpy (t->name, "pthread");
	__onyx_thread_list_add (t);

	struct kapi_thread_attr ka;
	memset (&ka, 0, sizeof ka);
	ka.fn = (unsigned long long) (unsigned long) thread_entry;
	ka.arg = (unsigned long long) (unsigned long) t;
	t->stack_size = ss ? ss : ONYX_STACK_DEFAULT;
	if (t->stack_size < PTHREAD_STACK_MIN) t->stack_size = PTHREAD_STACK_MIN;
	if (t->stack_size > ONYX_STACK_MAX) t->stack_size = ONYX_STACK_MAX;
	t->stack_size = (t->stack_size + 15) & ~15UL;
	ka.stack_size = t->stack_size;
	ka.tls = __onyx_thread_tp (t);
	ka.name = t->name;
	ka.prio = t->prio;
	int tid = kapi_thread_create_ex (&ka);
	if (tid == -KAPI_ENOSYS)
	{
		/* a kernel without v75 threads: the v67 call, its stack mapped at once */
		if (ss == 0)
			t->stack_size = ONYX_STACK_DEFAULT_V67;
		tid = kapi_thread_create (thread_entry, t, (unsigned) t->stack_size, t->name);
		if (tid >= 2 && t->prio)
			kapi_thread_priority (tid, 1);
		if (tid < 0)
			tid = tid == -1 ? -KAPI_ENOMEM : -KAPI_EAGAIN;
	}
	if (tid < 0)
	{
		__onyx_thread_list_remove (t);
		__onyx_thread_free (t);
		return tid == -KAPI_EINVAL ? EINVAL : EAGAIN;
	}
	t->tid = tid;
	*out = (pthread_t) t;
	return 0;
}

static int join (pthread_t th, void **ret, int mode, const struct timespec *abs)
{
	struct __onyx_thread *t = T (th);
	if (!valid (th))
		return ESRCH;
	if (t == __onyx_self ())
		return EDEADLK;
	if (t->detached || t->is_main)
		return EINVAL;
	while (LOAD (&t->state) != ONYX_T_FINISHED)
	{
		unsigned ms = KAPI_WAIT_FOREVER;
		if (mode == 1)
			return EBUSY;
		if (mode == 2)
		{
			long long left = __onyx_ms_until (CLOCK_REALTIME, abs);
			if (left < 0)
				return ETIMEDOUT;
			ms = (unsigned) left;
		}
		__onyx_futex_wait ((volatile unsigned *) &t->state, ONYX_T_RUNNING, ms);
	}
	int code;
	kapi_thread_join (t->tid, KAPI_WAIT_FOREVER, &code);	/* (it is ending: -2 = gone already) */
	if (ret)
		*ret = t->result;
	__onyx_thread_list_remove (t);
	__onyx_thread_free (t);
	return 0;
}

int pthread_join (pthread_t t, void **ret) { return join (t, ret, 0, 0); }
int pthread_tryjoin_np (pthread_t t, void **ret) { return join (t, ret, 1, 0); }
int pthread_timedjoin_np (pthread_t t, void **ret, const struct timespec *abs) { return join (t, ret, 2, abs); }

int pthread_detach (pthread_t th)
{
	struct __onyx_thread *t = T (th);
	if (!valid (th))
		return ESRCH;
	if (t->detached || t->is_main)
		return EINVAL;
	t->detached = 1;
	if (t != __onyx_self ())
		reap_detached ();
	return 0;
}

void pthread_exit (void *result)
{
	struct __onyx_thread *t = __onyx_self ();
	if (!t->is_main)
		thread_finish (t, result);
	/* the main thread: the process lives on while other threads run; it ends with the last */
	run_cleanups (t);
	__onyx_keys_run_destructors (t);
	for (;;)
	{
		struct __onyx_thread *running = 0;
		__onyx_thread_list_lock ();
		for (struct __onyx_thread *p = __onyx_thread_list_first (); p != 0; p = p->next)
			if (!p->is_main && LOAD (&p->state) != ONYX_T_FINISHED)
			{
				running = p;
				break;
			}
		__onyx_thread_list_unlock ();
		if (running == 0)
			break;
		__onyx_futex_wait ((volatile unsigned *) &running->state, ONYX_T_RUNNING, 1000);
	}
	exit (0);
}

pthread_t pthread_self (void) { return (pthread_t) __onyx_self (); }
int pthread_equal (pthread_t a, pthread_t b) { return a == b; }
int pthread_yield (void) { kapi_yield (); return 0; }
int sched_yield (void) { if (kapi__core () == 0) kapi_yield (); else kapi__pause (); return 0; }

int pthread_setname_np (pthread_t th, const char *name)
{
	if (name == 0)
		return EINVAL;
	if (strlen (name) >= sizeof T (th)->name)
		return ERANGE;
	strcpy (T (th)->name, name);		/* (the kernel's name is the one given at creation) */
	return 0;
}

int pthread_getname_np (pthread_t th, char *buf, size_t len)
{
	size_t n = strlen (T (th)->name);
	if (buf == 0 || len <= n)
		return ERANGE;
	memcpy (buf, T (th)->name, n + 1);
	return 0;
}

int pthread_getattr_np (pthread_t th, pthread_attr_t *a)
{
	struct __onyx_thread *t = T (th);
	pthread_attr_init (a);
	if (t->stack_hi == 0)
		stack_bounds (t, t->is_main ? 1 : t->tid);
	a->__detachstate = t->detached ? PTHREAD_CREATE_DETACHED : PTHREAD_CREATE_JOINABLE;
	a->__stackaddr = (void *) (unsigned long) t->stack_lo;
	a->__stacksize = (unsigned long) (t->stack_hi - t->stack_lo);
	a->__guardsize = (unsigned long) t->guard;
	a->__priority = t->prio;
	return 0;
}

int pthread_getcpuclockid (pthread_t t, clockid_t *c)
{
	(void) t;
	*c = CLOCK_THREAD_CPUTIME_ID;
	return 0;
}

/* ---- scheduling: one policy (the kernel's round robin); priority > 0 = "real time" ---- */
int pthread_setschedparam (pthread_t th, int policy, const struct sched_param *p)
{
	(void) policy;
	struct __onyx_thread *t = T (th);
	t->prio = p != 0 && p->sched_priority > 0;
	if (kapi__core () == 0)
		kapi_thread_priority (t->is_main ? 1 : t->tid, t->prio);
	return 0;
}

int pthread_getschedparam (pthread_t th, int *policy, struct sched_param *p)
{
	*policy = T (th)->prio ? SCHED_FIFO : SCHED_OTHER;
	memset (p, 0, sizeof *p);
	p->sched_priority = T (th)->prio;
	return 0;
}

int pthread_setschedprio (pthread_t th, int prio)
{
	struct sched_param p;
	memset (&p, 0, sizeof p);
	p.sched_priority = prio;
	return pthread_setschedparam (th, SCHED_OTHER, &p);
}

static int s_concurrency;
int pthread_setconcurrency (int n) { if (n < 0) return EINVAL; s_concurrency = n; return 0; }
int pthread_getconcurrency (void) { return s_concurrency; }

int sched_get_priority_max (int policy) { return policy == SCHED_OTHER ? 0 : 99; }
int sched_get_priority_min (int policy) { (void) policy; return 0; }
int sched_getscheduler (pid_t pid) { (void) pid; return SCHED_OTHER; }
int sched_setscheduler (pid_t pid, int policy, const struct sched_param *p)
{
	(void) pid; (void) policy; (void) p;
	return SCHED_OTHER;
}
int sched_getparam (pid_t pid, struct sched_param *p) { (void) pid; memset (p, 0, sizeof *p); return 0; }
int sched_setparam (pid_t pid, const struct sched_param *p) { (void) pid; (void) p; return 0; }
int sched_rr_get_interval (pid_t pid, struct timespec *ts)
{
	(void) pid;
	ts->tv_sec = 0;
	ts->tv_nsec = 20000000;			/* the kernel's slice: 20 ms */
	return 0;
}
int sched_getcpu (void) { return (int) kapi__core (); }

/* ---- attributes ---- */
int pthread_attr_init (pthread_attr_t *a)
{
	memset (a, 0, sizeof *a);
	a->__flags = 1;
	a->__guardsize = ONYX_PAGE;
	return 0;
}
int pthread_attr_destroy (pthread_attr_t *a) { a->__flags = 0; return 0; }
int pthread_attr_setdetachstate (pthread_attr_t *a, int s)
{
	if (s != PTHREAD_CREATE_JOINABLE && s != PTHREAD_CREATE_DETACHED)
		return EINVAL;
	a->__detachstate = s;
	return 0;
}
int pthread_attr_getdetachstate (const pthread_attr_t *a, int *s) { *s = a->__detachstate; return 0; }
int pthread_attr_setstacksize (pthread_attr_t *a, size_t n)
{
	if (n < PTHREAD_STACK_MIN)
		return EINVAL;
	a->__stacksize = n > ONYX_STACK_MAX ? ONYX_STACK_MAX : n;
	return 0;
}
int pthread_attr_getstacksize (const pthread_attr_t *a, size_t *n)
{
	*n = a->__stacksize ? a->__stacksize : ONYX_STACK_DEFAULT;
	return 0;
}
int pthread_attr_setstack (pthread_attr_t *a, void *addr, size_t n) { (void) a; (void) addr; (void) n; return ENOTSUP; }
int pthread_attr_getstack (const pthread_attr_t *a, void **addr, size_t *n)
{
	*addr = a->__stackaddr;
	*n = a->__stacksize;
	return 0;
}
int pthread_attr_setstackaddr (pthread_attr_t *a, void *addr) { (void) a; (void) addr; return ENOTSUP; }
int pthread_attr_getstackaddr (const pthread_attr_t *a, void **addr) { *addr = a->__stackaddr; return 0; }
int pthread_attr_setguardsize (pthread_attr_t *a, size_t n) { a->__guardsize = n; return 0; }
int pthread_attr_getguardsize (const pthread_attr_t *a, size_t *n) { *n = a->__guardsize; return 0; }
int pthread_attr_setschedparam (pthread_attr_t *a, const struct sched_param *p) { a->__priority = p->sched_priority; return 0; }
int pthread_attr_getschedparam (const pthread_attr_t *a, struct sched_param *p)
{
	memset (p, 0, sizeof *p);
	p->sched_priority = a->__priority;
	return 0;
}
int pthread_attr_setschedpolicy (pthread_attr_t *a, int p) { a->__schedpolicy = p; return 0; }
int pthread_attr_getschedpolicy (const pthread_attr_t *a, int *p) { *p = a->__schedpolicy; return 0; }
int pthread_attr_setinheritsched (pthread_attr_t *a, int i)
{
	if (i != PTHREAD_INHERIT_SCHED && i != PTHREAD_EXPLICIT_SCHED)
		return EINVAL;
	a->__inheritsched = i;
	return 0;
}
int pthread_attr_getinheritsched (const pthread_attr_t *a, int *i) { *i = a->__inheritsched; return 0; }
int pthread_attr_setscope (pthread_attr_t *a, int s)
{
	if (s == PTHREAD_SCOPE_PROCESS)
		return ENOTSUP;
	if (s != PTHREAD_SCOPE_SYSTEM)
		return EINVAL;
	a->__scope = s;
	return 0;
}
int pthread_attr_getscope (const pthread_attr_t *a, int *s) { *s = a->__scope; return 0; }

/* ---- keys ---- */
static struct
{
	volatile int used;
	void (*dtor) (void *);
} s_keys[ONYX_KEYS_MAX];
static volatile unsigned s_keyLock;

int pthread_key_create (pthread_key_t *k, void (*dtor) (void *))
{
	__onyx_lock (&s_keyLock);
	for (unsigned i = 0; i < ONYX_KEYS_MAX; i++)
		if (!s_keys[i].used)
		{
			s_keys[i].used = 1;
			s_keys[i].dtor = dtor;
			/* a reused key starts empty in every thread */
			__onyx_thread_list_lock ();
			for (struct __onyx_thread *p = __onyx_thread_list_first (); p != 0; p = p->next)
				p->specific[i] = 0;
			__onyx_thread_list_unlock ();
			__onyx_unlock (&s_keyLock);
			*k = i;
			return 0;
		}
	__onyx_unlock (&s_keyLock);
	return EAGAIN;
}

int pthread_key_delete (pthread_key_t k)
{
	if (k >= ONYX_KEYS_MAX || !s_keys[k].used)
		return EINVAL;
	s_keys[k].used = 0;
	s_keys[k].dtor = 0;
	return 0;
}

void *pthread_getspecific (pthread_key_t k)
{
	return k < ONYX_KEYS_MAX ? __onyx_self ()->specific[k] : 0;
}

int pthread_setspecific (pthread_key_t k, const void *v)
{
	if (k >= ONYX_KEYS_MAX || !s_keys[k].used)
		return EINVAL;
	__onyx_self ()->specific[k] = (void *) v;
	return 0;
}

void __onyx_keys_run_destructors (struct __onyx_thread *t)
{
	for (int round = 0; round < PTHREAD_DESTRUCTOR_ITERATIONS; round++)
	{
		int any = 0;
		for (unsigned i = 0; i < ONYX_KEYS_MAX; i++)
		{
			void (*dtor) (void *) = s_keys[i].dtor;
			void *v = t->specific[i];
			if (v != 0 && s_keys[i].used && dtor != 0)
			{
				t->specific[i] = 0;
				dtor (v);
				any = 1;
			}
		}
		if (!any)
			break;
	}
}

/* ---- cleanup handlers ---- */
void __onyx_cleanup_push (struct __onyx_cleanup *c, void (*fn) (void *), void *arg)
{
	struct __onyx_thread *t = __onyx_self ();
	c->__routine = fn;
	c->__arg = arg;
	c->__prev = t->cleanup;
	t->cleanup = c;
}

void __onyx_cleanup_pop (struct __onyx_cleanup *c, int execute)
{
	struct __onyx_thread *t = __onyx_self ();
	t->cleanup = c->__prev;
	if (execute)
		c->__routine (c->__arg);
}

/* ---- cancellation (not supported), fork handlers (no fork) ---- */
int pthread_cancel (pthread_t t) { (void) t; return ENOSYS; }
int pthread_setcancelstate (int s, int *old)
{
	struct __onyx_thread *t = __onyx_self ();
	if (old)
		*old = t->cancel_state;
	t->cancel_state = s;
	return 0;
}
int pthread_setcanceltype (int type, int *old) { (void) type; if (old) *old = PTHREAD_CANCEL_DEFERRED; return 0; }
void pthread_testcancel (void) { }
int pthread_atfork (void (*a) (void), void (*b) (void), void (*c) (void)) { (void) a; (void) b; (void) c; return 0; }

/* Linked with -u __onyx_pthread_anchor (onyx.specs): the whole thread layer is in the program,
 * so libstdc++'s weak references (gthr-posix.h, under WP-TC's toolchain) find it. */
void *const __onyx_pthread_anchor[] =
{
	(void *) pthread_create, (void *) pthread_join, (void *) pthread_detach, (void *) pthread_once,
	(void *) pthread_mutex_lock, (void *) pthread_mutex_trylock, (void *) pthread_mutex_unlock,
	(void *) pthread_mutex_timedlock, (void *) pthread_mutex_clocklock,
	(void *) pthread_cond_wait, (void *) pthread_cond_timedwait, (void *) pthread_cond_clockwait,
	(void *) pthread_cond_signal, (void *) pthread_cond_broadcast,
	(void *) pthread_rwlock_rdlock, (void *) pthread_rwlock_wrlock, (void *) pthread_rwlock_unlock,
	(void *) pthread_key_create, (void *) pthread_key_delete, (void *) pthread_getspecific,
	(void *) pthread_setspecific, (void *) pthread_cancel, (void *) sched_yield,
};
