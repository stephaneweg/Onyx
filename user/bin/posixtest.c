/*
 * posixtest.c -- /bin/posixtest: the conformance test of libonyxposix, the POSIX layer
 * (docs/POSIX-PLAN.md §3.4; docs/03 §5.4).
 *
 *   posixtest [group...]     groups: mem thread file io time proc net misc cxx (default: all), and
 *                            loop (TCP / UDP over 127.0.0.1: not in the default, see group_loop)
 *   posixtest file SD:/tmp   the file group in another directory (default RAM:/posixtest and /tmp)
 *
 * Each check prints PASS, FAIL (with what was seen) or SKIP; then a summary; the exit status is
 * the number of failures. A check that needs a kernel piece not merged yet (the v75 calls answer
 * ENOSYS until WP-MEM, WP-FILE/PROC and WP-NET land) is reported "SKIP (kernel ENOSYS)", so the
 * test is usable at every merge stage; what libonyxposix's fallbacks do on an older kernel is
 * still checked. The net group needs the network (Wi-Fi up): SKIP otherwise.
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <spawn.h>
#include <pthread.h>
#include <semaphore.h>
#include <dirent.h>
#include <poll.h>
#include <netdb.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <dlfcn.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/utsname.h>
#include <sys/random.h>
#include <sys/resource.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "kapi.h"

extern char **environ;

/* ---- reporting ---- */
static int s_pass, s_fail, s_skip;
static const char *s_group = "";

static void pass (const char *name)
{
	s_pass++;
	printf ("PASS  %s: %s\n", s_group, name);
}

static void fail (const char *name, const char *fmt, ...)
{
	char why[200];
	va_list ap;
	va_start (ap, fmt);
	vsnprintf (why, sizeof why, fmt, ap);
	va_end (ap);
	s_fail++;
	printf ("FAIL  %s: %s (%s)\n", s_group, name, why);
}

static void skip (const char *name, const char *why)
{
	s_skip++;
	printf ("SKIP  %s: %s (%s)\n", s_group, name, why);
}

#define CHECK(name, cond, ...) do { if (cond) pass (name); else fail (name, __VA_ARGS__); } while (0)
#define NOSYS		"kernel ENOSYS"

/* ---- the kernel's v75 pieces (each answers -KAPI_ENOSYS until its work package lands) ---- */
static int k_vm, k_threads, k_files, k_proc, k_clock, k_sleep, k_env, k_net, k_poll, k_pipe_nb, k_ipc;

static void probe_kernel (void)
{
	struct kapi_vm_region r;
	struct kapi_thread_info ti;
	struct kapi_stat st;
	struct kapi_proc_status ps;
	struct kapi_clock_info ci;
	char b[8];
	k_vm = kapi_vm_query (0, &r) != -KAPI_ENOSYS;
	k_threads = kapi_thread_info (0, &ti) != -KAPI_ENOSYS;
	k_files = kapi_path_stat ("SD:/", &st) != -KAPI_ENOSYS;
	k_proc = kapi_proc_wait (0, KAPI_WAIT_NOHANG, &ps) != -KAPI_ENOSYS;
	k_clock = kapi_clock_info (&ci) != -KAPI_ENOSYS;
	k_sleep = kapi_sleep_us (0) != -KAPI_ENOSYS;
	k_env = kapi_get_env (b, sizeof b) != -KAPI_ENOSYS;
	k_net = kapi_sock_close (-1) != -KAPI_ENOSYS;
	k_poll = kapi_poll (0, 0, 0) != -KAPI_ENOSYS;
	k_pipe_nb = kapi_stream_write_nb (0, b, 0) != -KAPI_ENOSYS;
	k_ipc = kapi_shm_ctl (0, 0, 0) != -KAPI_ENOSYS;
	printf ("posixtest: kapi v%u; v75 pieces: vm %s, threads %s, files %s, processes %s, clock %s, "
		"sleep %s, environment %s, sockets %s, poll %s, pipe writes %s\n", KT->version,
		k_vm ? "yes" : "no", k_threads ? "yes" : "no", k_files ? "yes" : "no", k_proc ? "yes" : "no",
		k_clock ? "yes" : "no", k_sleep ? "yes" : "no", k_env ? "yes" : "no", k_net ? "yes" : "no",
		k_poll ? "yes" : "no", k_pipe_nb ? "yes" : "no");
	printf ("posixtest: v76 IPC (local sockets, SCM_RIGHTS, shared memory) %s\n", k_ipc ? "yes" : "no");
}

static long long ms_now (void)
{
	struct timespec t;
	clock_gettime (CLOCK_MONOTONIC, &t);
	return (long long) t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static void abs_in_ms (clockid_t c, long ms, struct timespec *ts)
{
	clock_gettime (c, ts);
	ts->tv_nsec += (ms % 1000) * 1000000L;
	ts->tv_sec += ms / 1000 + ts->tv_nsec / 1000000000L;
	ts->tv_nsec %= 1000000000L;
}

/* ================================================================================== mem == */
static void group_mem (void)
{
	s_group = "mem";
	CHECK ("sysconf (_SC_PAGESIZE) = 65536", sysconf (_SC_PAGESIZE) == 65536, "%ld", sysconf (_SC_PAGESIZE));
	CHECK ("getpagesize () = 65536", getpagesize () == 65536, "%d", getpagesize ());

	size_t n = 1 << 20;
	unsigned char *p = (unsigned char *) mmap (0, n, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (p == MAP_FAILED)
		fail ("mmap anonymous 1 MB", "errno %d", errno);
	else
	{
		int zero = 1;
		for (size_t i = 0; i < n; i += 4096)
			if (p[i])
				zero = 0;
		CHECK ("mmap anonymous: zero-filled, 64 KB-aligned", zero && ((unsigned long) p & 0xFFFF) == 0, "%p", p);
		for (size_t i = 0; i < n; i++)
			p[i] = (unsigned char) i;
		int ok = 1;
		for (size_t i = 0; i < n; i++)
			if (p[i] != (unsigned char) i)
				ok = 0;
		CHECK ("mmap anonymous: write and read back 1 MB", ok, "mismatch");
		CHECK ("munmap", munmap (p, n) == 0, "errno %d", errno);
	}

	errno = 0;					/* (v78) executable memory: a JIT's */
	unsigned *x = (unsigned *) mmap (0, 65536, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	CHECK ("mmap PROT_EXEC (a JIT's memory)", x != MAP_FAILED, "errno %d", errno);
	if (x != MAP_FAILED)
	{
		x[0] = 0x52800540;			/* mov w0, #42 */
		x[1] = 0xD65F03C0;			/* ret */
		__builtin___clear_cache ((char *) x, (char *) (x + 2));
		CHECK ("generated code runs", ((int (*) (void)) x) () == 42, "wrong result");
		CHECK ("mprotect PROT_READ | PROT_EXEC", mprotect (x, 65536, PROT_READ | PROT_EXEC) == 0, "errno %d", errno);
		munmap (x, 65536);
	}

	void *al = 0;
	CHECK ("posix_memalign 64 KB", posix_memalign (&al, 65536, 100000) == 0 && ((unsigned long) al & 0xFFFF) == 0, "failed");
	free (al);

	if (!k_vm)
	{
		skip ("PROT_NONE reservation of 1 GB + mprotect commit", NOSYS);
		skip ("partial munmap (split), MAP_FIXED, MAP_FIXED_NOREPLACE", NOSYS);
		skip ("madvise (MADV_DONTNEED) zero-fills", NOSYS);
		skip ("4 GB-aligned reservation (JSC's pattern)", NOSYS);
	}
	else
	{
		unsigned long g = 1UL << 30;
		unsigned char *r = (unsigned char *) mmap (0, g, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
		if (r == MAP_FAILED)
			fail ("PROT_NONE reservation of 1 GB + mprotect commit", "mmap errno %d", errno);
		else
		{
			int a = mprotect (r + (g / 2), 65536, PROT_READ | PROT_WRITE);
			if (a == 0)
				r[g / 2 + 100] = 42;
			CHECK ("PROT_NONE reservation of 1 GB + mprotect commit", a == 0 && r[g / 2 + 100] == 42, "mprotect errno %d", errno);

			void *f = mmap (r + 65536 * 4, 65536, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
			void *nr = mmap (r + 65536 * 4, 65536, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
			int split = munmap (r + 65536 * 8, 65536 * 2);
			struct kapi_vm_region q1, q2;
			int s1 = kapi_vm_query ((unsigned long) r + 65536 * 7, &q1);
			int s2 = kapi_vm_query ((unsigned long) r + 65536 * 11, &q2);
			CHECK ("partial munmap (split), MAP_FIXED, MAP_FIXED_NOREPLACE",
			       f == (void *) (r + 65536 * 4) && nr == MAP_FAILED && split == 0 && s1 == 0 && s2 == 0 && q1.end <= q2.start,
			       "fixed %p noreplace %p unmap %d query %d %d", f, nr, split, s1, s2);

			unsigned char *c = r + g / 2;
			c[0] = 7;
			int m = madvise (c, 65536, MADV_DONTNEED);
			CHECK ("madvise (MADV_DONTNEED) zero-fills", m == 0 && c[0] == 0 && c[100] == 0, "madvise %d, %d", m, c[0]);
			munmap (r, g);
		}

		/* WTF's OSAllocator: reserve twice the size, keep the aligned part, trim the edges */
		unsigned long four = 4UL << 30;
		unsigned char *big = (unsigned char *) mmap (0, 2 * four, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
		if (big == MAP_FAILED)
			fail ("4 GB-aligned reservation (JSC's pattern)", "mmap 8 GB errno %d", errno);
		else
		{
			unsigned long b = (unsigned long) big, al4 = (b + four - 1) & ~(four - 1);
			int r1 = al4 > b ? munmap (big, al4 - b) : 0;
			int r2 = munmap ((void *) (al4 + four), b + 2 * four - (al4 + four));
			int r3 = mprotect ((void *) al4, 65536, PROT_READ | PROT_WRITE);
			if (r3 == 0)
				*(volatile int *) al4 = 1;
			CHECK ("4 GB-aligned reservation (JSC's pattern)", r1 == 0 && r2 == 0 && r3 == 0, "%d %d %d errno %d", r1, r2, r3, errno);
			munmap ((void *) al4, four);
		}
	}

	/* a file, MAP_PRIVATE */
	const char *path = "/tmp/posixtest.map";
	int fd = open (path, O_RDWR | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		fail ("mmap of a file (MAP_PRIVATE)", "open errno %d", errno);
	else
	{
		char buf[3000];
		for (int i = 0; i < (int) sizeof buf; i++)
			buf[i] = (char) ('a' + i % 26);
		write (fd, buf, sizeof buf);
		char *m = (char *) mmap (0, sizeof buf, PROT_READ, MAP_PRIVATE, fd, 0);
		CHECK ("mmap of a file (MAP_PRIVATE)", m != MAP_FAILED && memcmp (m, buf, sizeof buf) == 0, "errno %d", errno);
		if (m != MAP_FAILED)
			munmap (m, sizeof buf);
		errno = 0;
		void *s = mmap (0, sizeof buf, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
		CHECK ("mmap MAP_SHARED + PROT_WRITE of a file -> ENOTSUP", s == MAP_FAILED && errno == ENOTSUP, "%p errno %d", s, errno);
		close (fd);
		unlink (path);
	}
}

/* =============================================================================== thread == */
static pthread_mutex_t s_mx = PTHREAD_MUTEX_INITIALIZER;
static long s_counter;
static __thread int t_local = 1;

static void *t_echo (void *a) { return a; }

static void *t_count (void *a)
{
	(void) a;
	for (int i = 0; i < 2000; i++)
	{
		pthread_mutex_lock (&s_mx);
		long v = s_counter;
		if ((i & 63) == 0)
			sched_yield ();
		s_counter = v + 1;
		pthread_mutex_unlock (&s_mx);
	}
	return 0;
}

static pthread_mutex_t s_pm = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_pc = PTHREAD_COND_INITIALIZER;
static int s_turn, s_rounds;
static void *t_pong (void *a)
{
	(void) a;
	pthread_mutex_lock (&s_pm);
	while (s_rounds < 10000)
	{
		while (s_turn != 1)
			pthread_cond_wait (&s_pc, &s_pm);
		s_turn = 0;
		s_rounds++;
		pthread_cond_broadcast (&s_pc);
	}
	pthread_mutex_unlock (&s_pm);
	return 0;
}

static pthread_once_t s_once = PTHREAD_ONCE_INIT;
static int s_onceRuns;
static void once_fn (void) { s_onceRuns++; sched_yield (); }
static void *t_once (void *a) { (void) a; pthread_once (&s_once, once_fn); return 0; }

static pthread_key_t s_key;
static volatile int s_dtorSeen;
static void key_dtor (void *v) { if (v == (void *) 0x1234) s_dtorSeen = 1; }
static void *t_key (void *a) { (void) a; pthread_setspecific (s_key, (void *) 0x1234); return pthread_getspecific (s_key); }

static void *t_tls (void *a)
{
	int me = (int) (long) a;
	t_local = me;
	errno = 1000 + me;
	for (int i = 0; i < 200; i++)
	{
		sched_yield ();
		if (t_local != me || errno != 1000 + me)
			return (void *) 1;
	}
	return 0;
}

static pthread_rwlock_t s_rw = PTHREAD_RWLOCK_INITIALIZER;
static volatile int s_readers, s_maxReaders, s_writerBad;
static void *t_reader (void *a)
{
	(void) a;
	for (int i = 0; i < 50; i++)
	{
		pthread_rwlock_rdlock (&s_rw);
		int n = __atomic_add_fetch (&s_readers, 1, __ATOMIC_SEQ_CST);
		if (n > s_maxReaders)
			s_maxReaders = n;
		/* (the first rounds hold the lock a little: another reader can come in meanwhile) */
		for (int k = 0; i < 3 && k < 50 && s_readers < 2; k++)
			usleep (1000);
		if (s_readers > s_maxReaders)
			s_maxReaders = s_readers;
		sched_yield ();
		__atomic_sub_fetch (&s_readers, 1, __ATOMIC_SEQ_CST);
		pthread_rwlock_unlock (&s_rw);
	}
	return 0;
}
static void *t_writer (void *a)
{
	(void) a;
	for (int i = 0; i < 50; i++)
	{
		pthread_rwlock_wrlock (&s_rw);
		if (s_readers != 0)
			s_writerBad = 1;
		sched_yield ();
		pthread_rwlock_unlock (&s_rw);
	}
	return 0;
}

static sem_t s_items;
static volatile int s_consumed;
static void *t_consume (void *a)
{
	(void) a;
	for (int i = 0; i < 1000; i++)
	{
		sem_wait (&s_items);
		s_consumed++;
	}
	return 0;
}

static pthread_barrier_t s_bar;
static volatile int s_serial;
static void *t_barrier (void *a)
{
	(void) a;
	for (int r = 0; r < 10; r++)
		if (pthread_barrier_wait (&s_bar) == PTHREAD_BARRIER_SERIAL_THREAD)
			__atomic_add_fetch (&s_serial, 1, __ATOMIC_SEQ_CST);
	return 0;
}

static void *t_bounds (void *a)
{
	(void) a;
	int local;
	pthread_attr_t at;
	void *lo;
	size_t sz;
	if (pthread_getattr_np (pthread_self (), &at) != 0 || pthread_attr_getstack (&at, &lo, &sz) != 0)
		return (void *) 1;
	return (char *) &local >= (char *) lo && (char *) &local < (char *) lo + sz ? 0 : (void *) 2;
}

static volatile int s_detachedRan;
static void *t_detached (void *a) { (void) a; s_detachedRan = 1; return 0; }

static pthread_spinlock_t s_spin;
static long s_spinCount;
static void *t_spin (void *a)
{
	(void) a;
	for (int i = 0; i < 1000; i++)
	{
		pthread_spin_lock (&s_spin);
		s_spinCount++;
		pthread_spin_unlock (&s_spin);
	}
	return 0;
}

static void group_thread (void)
{
	s_group = "thread";
	pthread_t t[32];

	/* 100 threads, in waves of 25 */
	int ok = 1, made = 0;
	for (int w = 0; w < 4; w++)
	{
		for (int i = 0; i < 25; i++)
			if (pthread_create (&t[i], 0, t_echo, (void *) (long) (w * 25 + i + 1)) != 0)
				ok = 0;
			else
				made++;
		for (int i = 0; i < 25; i++)
		{
			void *r = 0;
			if (pthread_join (t[i], &r) != 0 || r != (void *) (long) (w * 25 + i + 1))
				ok = 0;
		}
	}
	CHECK ("100 threads in waves of 25: create, join, results", ok && made == 100, "%d made", made);

	s_counter = 0;
	for (int i = 0; i < 8; i++)
		pthread_create (&t[i], 0, t_count, 0);
	for (int i = 0; i < 8; i++)
		pthread_join (t[i], 0);
	CHECK ("mutex: a counter under contention (8 x 2000)", s_counter == 16000, "%ld", s_counter);

	pthread_mutexattr_t ma;
	pthread_mutex_t rm, em, tm = PTHREAD_MUTEX_INITIALIZER;
	pthread_mutexattr_init (&ma);
	pthread_mutexattr_settype (&ma, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init (&rm, &ma);
	int r1 = pthread_mutex_lock (&rm), r2 = pthread_mutex_lock (&rm), r3 = pthread_mutex_trylock (&rm);
	int u1 = pthread_mutex_unlock (&rm), u2 = pthread_mutex_unlock (&rm), u3 = pthread_mutex_unlock (&rm);
	int u4 = pthread_mutex_unlock (&rm);
	CHECK ("mutex: recursive", !r1 && !r2 && !r3 && !u1 && !u2 && !u3 && u4 == EPERM, "%d %d %d %d %d %d %d", r1, r2, r3, u1, u2, u3, u4);
	pthread_mutexattr_settype (&ma, PTHREAD_MUTEX_ERRORCHECK);
	pthread_mutex_init (&em, &ma);
	pthread_mutex_lock (&em);
	int e1 = pthread_mutex_lock (&em);
	pthread_mutex_unlock (&em);
	int e2 = pthread_mutex_unlock (&em);
	CHECK ("mutex: errorcheck (EDEADLK, EPERM)", e1 == EDEADLK && e2 == EPERM, "%d %d", e1, e2);

	pthread_mutex_lock (&tm);
	struct timespec ts;
	abs_in_ms (CLOCK_REALTIME, 50, &ts);
	long long t0 = ms_now ();
	int tl = pthread_mutex_timedlock (&tm, &ts);
	long long dt = ms_now () - t0;
	pthread_mutex_unlock (&tm);
	CHECK ("mutex: timedlock times out (50 ms)", tl == ETIMEDOUT && dt >= 40 && dt < 500, "%d after %lld ms", tl, dt);

	s_turn = 0;
	s_rounds = 0;
	pthread_create (&t[0], 0, t_pong, 0);
	pthread_mutex_lock (&s_pm);
	while (s_rounds < 10000)
	{
		while (s_turn != 0 && s_rounds < 10000)
			pthread_cond_wait (&s_pc, &s_pm);
		if (s_rounds >= 10000)
			break;
		s_turn = 1;
		pthread_cond_broadcast (&s_pc);
	}
	pthread_mutex_unlock (&s_pm);
	pthread_join (t[0], 0);
	CHECK ("condition variable: ping-pong 10000 times", s_rounds == 10000, "%d", s_rounds);

	pthread_cond_t c1 = PTHREAD_COND_INITIALIZER, c2;
	pthread_condattr_t ca;
	pthread_condattr_init (&ca);
	pthread_condattr_setclock (&ca, CLOCK_MONOTONIC);
	pthread_cond_init (&c2, &ca);
	pthread_mutex_lock (&tm);
	abs_in_ms (CLOCK_REALTIME, 100, &ts);
	t0 = ms_now ();
	int w1 = pthread_cond_timedwait (&c1, &tm, &ts);
	long long d1 = ms_now () - t0;
	abs_in_ms (CLOCK_MONOTONIC, 100, &ts);
	t0 = ms_now ();
	int w2 = pthread_cond_timedwait (&c2, &tm, &ts);
	long long d2 = ms_now () - t0;
	pthread_mutex_unlock (&tm);
	CHECK ("cond timedwait 100 ms, CLOCK_REALTIME", w1 == ETIMEDOUT && d1 >= 90 && d1 < 250, "%d after %lld ms", w1, d1);
	CHECK ("cond timedwait 100 ms, CLOCK_MONOTONIC (condattr_setclock)", w2 == ETIMEDOUT && d2 >= 90 && d2 < 250, "%d after %lld ms", w2, d2);

	s_readers = s_maxReaders = s_writerBad = 0;
	for (int i = 0; i < 4; i++)
		pthread_create (&t[i], 0, t_reader, 0);
	pthread_create (&t[4], 0, t_writer, 0);
	for (int i = 0; i < 5; i++)
		pthread_join (t[i], 0);
	CHECK ("rwlock: readers together, the writer alone", s_maxReaders >= 2 && !s_writerBad, "max readers %d, writer saw readers %d", s_maxReaders, s_writerBad);

	for (int i = 0; i < 10; i++)
		pthread_create (&t[i], 0, t_once, 0);
	for (int i = 0; i < 10; i++)
		pthread_join (t[i], 0);
	CHECK ("pthread_once from 10 threads: once", s_onceRuns == 1, "%d runs", s_onceRuns);

	pthread_key_create (&s_key, key_dtor);
	void *kv = 0;
	pthread_create (&t[0], 0, t_key, 0);
	pthread_join (t[0], &kv);
	CHECK ("keys: a value per thread, its destructor at the end", kv == (void *) 0x1234 && s_dtorSeen && pthread_getspecific (s_key) == 0,
	       "value %p, destructor %d", kv, s_dtorSeen);
	pthread_key_delete (s_key);

	errno = 0;
	t_local = 99;
	ok = 1;
	for (int i = 0; i < 4; i++)
		pthread_create (&t[i], 0, t_tls, (void *) (long) (i + 1));
	for (int i = 0; i < 4; i++)
	{
		void *r;
		pthread_join (t[i], &r);
		if (r != 0)
			ok = 0;
	}
	CHECK ("__thread: one copy per thread", ok && t_local == 99, "main sees %d", t_local);
	CHECK ("errno: one per thread", ok && errno == 0, "main's errno %d", errno);

	sem_init (&s_items, 0, 0);
	s_consumed = 0;
	pthread_create (&t[0], 0, t_consume, 0);
	for (int i = 0; i < 1000; i++)
	{
		sem_post (&s_items);
		if ((i & 31) == 0)
			sched_yield ();
	}
	pthread_join (t[0], 0);
	abs_in_ms (CLOCK_REALTIME, 30, &ts);
	int sw = sem_timedwait (&s_items, &ts);
	CHECK ("semaphores: 1000 items, then a timed wait times out", s_consumed == 1000 && sw == -1 && errno == ETIMEDOUT, "%d consumed, %d", s_consumed, sw);

	pthread_barrier_init (&s_bar, 0, 5);
	s_serial = 0;
	for (int i = 0; i < 5; i++)
		pthread_create (&t[i], 0, t_barrier, 0);
	for (int i = 0; i < 5; i++)
		pthread_join (t[i], 0);
	CHECK ("barrier: 5 threads x 10 rounds, one serial a round", s_serial == 10, "%d", s_serial);

	void *br = (void *) 9;
	pthread_attr_t at;
	pthread_attr_init (&at);
	pthread_attr_setstacksize (&at, 256 * 1024);
	pthread_create (&t[0], &at, t_bounds, 0);
	pthread_join (t[0], &br);
	int local;
	void *lo;
	size_t sz;
	pthread_attr_t mat;
	pthread_getattr_np (pthread_self (), &mat);
	pthread_attr_getstack (&mat, &lo, &sz);
	int mainOk = (char *) &local >= (char *) lo && (char *) &local < (char *) lo + sz;
	if (!k_threads)
		CHECK ("pthread_getattr_np: the stack holds a local (v67 estimate)", br == 0 && mainOk, "thread %p, main %d", br, mainOk);
	else
		CHECK ("pthread_getattr_np: the stack holds a local (thread_info)", br == 0 && mainOk, "thread %p, main %d", br, mainOk);

	pthread_attr_setdetachstate (&at, PTHREAD_CREATE_DETACHED);
	pthread_create (&t[0], &at, t_detached, 0);
	for (int i = 0; i < 200 && !s_detachedRan; i++)
		usleep (1000);
	CHECK ("a detached thread runs", s_detachedRan, "not run");

	pthread_spin_init (&s_spin, 0);
	for (int i = 0; i < 4; i++)
		pthread_create (&t[i], 0, t_spin, 0);
	for (int i = 0; i < 4; i++)
		pthread_join (t[i], 0);
	CHECK ("spin lock counter (4 x 1000)", s_spinCount == 4000, "%ld", s_spinCount);

	char name[32];
	pthread_setname_np (pthread_self (), "tester");
	pthread_getname_np (pthread_self (), name, sizeof name);
	CHECK ("self, equal, setname / getname, sched_yield",
	       pthread_equal (pthread_self (), pthread_self ()) && !strcmp (name, "tester") && sched_yield () == 0, "name '%s'", name);
}

/* ================================================================================= file == */
static void file_group_in (const char *dir)
{
	char p[400], q[400], d2[400];
	char tag[64];
	snprintf (tag, sizeof tag, "file %s", dir);
	s_group = tag;
	mkdir (dir, 0755);
	snprintf (p, sizeof p, "%s/a.dat", dir);
	snprintf (q, sizeof q, "%s/b.dat", dir);
	unlink (p);
	unlink (q);

	int fd = open (p, O_RDWR | O_CREAT | O_EXCL, 0644);
	int fd2 = open (p, O_RDWR | O_CREAT | O_EXCL, 0644);
	CHECK ("open O_CREAT|O_EXCL: new, then EEXIST", fd >= 0 && fd2 < 0 && errno == EEXIST, "%d %d errno %d", fd, fd2, errno);
	if (fd < 0)
		return;

	static unsigned char buf[100000], back[100000];
	for (int i = 0; i < (int) sizeof buf; i++)
		buf[i] = (unsigned char) (i * 7 + 3);
	ssize_t w = write (fd, buf, sizeof buf);
	off_t o = lseek (fd, 0, SEEK_SET);
	ssize_t r = read (fd, back, sizeof back);
	CHECK ("write / lseek / read 100 KB", w == (ssize_t) sizeof buf && o == 0 && r == w && !memcmp (buf, back, sizeof buf), "%zd %ld %zd", w, (long) o, r);

	/* pread / pwrite at random offsets, two descriptors, against a model */
	/* (the old calls hold a writable file per descriptor: one descriptor there) */
	int fdb = k_files ? open (p, O_RDWR) : dup (fd);
	unsigned seed = 12345;
	int ok = fdb >= 0;
	for (int k = 0; k < 1000 && ok; k++)
	{
		seed = seed * 1103515245 + 12345;
		off_t at = (off_t) (seed % (sizeof buf - 200));
		int len = (int) (seed >> 20) % 200 + 1;
		int f = (k & 1) ? fd : fdb;
		if (k % 3 == 0)
		{
			for (int j = 0; j < len; j++)
				buf[at + j] = (unsigned char) (k + j);
			if (pwrite (f, buf + at, (size_t) len, at) != len)
				ok = 0;
		}
		else if (pread (f, back, (size_t) len, at) != len || memcmp (back, buf + at, (size_t) len))
			ok = 0;
	}
	CHECK (k_files ? "pread / pwrite: 1000 random operations on two descriptors" : "pread / pwrite: 1000 random operations (one description)", ok, "mismatch");
	if (fdb >= 0)
		close (fdb);

	struct stat st;
	CHECK ("ftruncate grow (zeros) and fstat", ftruncate (fd, 200000) == 0 && fstat (fd, &st) == 0 && st.st_size == 200000 &&
	       pread (fd, back, 100, 150000) == 100 && back[0] == 0 && back[99] == 0, "size %ld", (long) st.st_size);
	CHECK ("ftruncate shrink", ftruncate (fd, 1000) == 0 && fstat (fd, &st) == 0 && st.st_size == 1000, "size %ld", (long) st.st_size);
	CHECK ("fsync", fsync (fd) == 0, "errno %d", errno);

	int dfd = dup (fd);
	lseek (fd, 10, SEEK_SET);
	CHECK ("dup shares the offset", dfd >= 0 && lseek (dfd, 0, SEEK_CUR) == 10, "%ld", (long) lseek (dfd, 0, SEEK_CUR));
	int d2fd = dup2 (fd, 50);
	CHECK ("dup2 onto 50", d2fd == 50 && lseek (50, 0, SEEK_CUR) == 10, "%d", d2fd);
	close (50);
	close (dfd);
	int fl = fcntl (fd, F_GETFL);
	fcntl (fd, F_SETFD, FD_CLOEXEC);
	CHECK ("fcntl F_GETFL / F_SETFD", (fl & O_ACCMODE) == O_RDWR && fcntl (fd, F_GETFD) == FD_CLOEXEC, "fl %x", fl);
	close (fd);

	/* O_APPEND from two descriptors */
	int a1 = open (q, O_WRONLY | O_CREAT | O_APPEND, 0644), a2 = open (q, O_WRONLY | O_APPEND);
	write (a1, "one ", 4);
	write (a2, "two ", 4);
	write (a1, "three", 5);
	close (a1);
	close (a2);
	char tb[32] = "";
	int qfd = open (q, O_RDONLY);
	ssize_t qn = qfd >= 0 ? read (qfd, tb, sizeof tb - 1) : -1;
	if (qfd >= 0)
		close (qfd);
	if (!k_files)
		skip ("O_APPEND from two descriptors", NOSYS " (the old calls hold a file per descriptor)");
	else
		CHECK ("O_APPEND from two descriptors", qn == 13 && !memcmp (tb, "one two three", 13), "'%s'", tb);

	/* rename onto an existing name */
	CHECK ("rename replaces its target", rename (p, q) == 0 && stat (p, &st) != 0 && stat (q, &st) == 0 && st.st_size == 1000,
	       "errno %d size %ld", errno, (long) st.st_size);

	/* unlink while open */
	int u = open (q, O_RDONLY);
	int ur = unlink (q);
	char ub[16];
	if (!k_files)
		skip ("unlink of an open file: still readable, gone from its directory", NOSYS);
	else
		CHECK ("unlink of an open file: still readable, gone from its directory",
		       u >= 0 && ur == 0 && pread (u, ub, 16, 0) == 16 && stat (q, &st) != 0, "%d %d", u, ur);
	if (u >= 0)
		close (u);
	unlink (q);

	/* directories */
	snprintf (d2, sizeof d2, "%s/sub", dir);
	rmdir (d2);
	int m1 = mkdir (d2, 0755), m2 = mkdir (d2, 0755);
	int e2 = errno;
	CHECK ("mkdir, then EEXIST", m1 == 0 && m2 == -1 && e2 == EEXIST, "%d %d errno %d", m1, m2, e2);
	char f1[700], f2[700];
	snprintf (f1, sizeof f1, "%s/x.txt", d2);
	char longname[201];
	memset (longname, 'n', 200);
	longname[200] = '\0';
	snprintf (f2, sizeof f2, "%s/%.200s", d2, longname);
	close (open (f1, O_WRONLY | O_CREAT, 0644));
	int lfd = open (f2, O_WRONLY | O_CREAT, 0644);
	if (lfd >= 0)
		close (lfd);
	int rn = rmdir (d2);
	CHECK ("rmdir of a full directory -> ENOTEMPTY", rn == -1 && errno == ENOTEMPTY, "%d errno %d", rn, errno);
	DIR *dd = opendir (d2);
	int seenX = 0, seenLong = 0, n = 0;
	struct dirent *e;
	while (dd && (e = readdir (dd)) != 0)
	{
		n++;
		if (!strcasecmp (e->d_name, "x.txt") && e->d_type == DT_REG)
			seenX = 1;
		if (strlen (e->d_name) == 200)
			seenLong = 1;
	}
	if (dd)
		closedir (dd);
	CHECK ("opendir / readdir", seenX, "%d entries", n);
	if (!k_files)
		skip ("readdir: a 200-character name", NOSYS);
	else
		CHECK ("readdir: a 200-character name", seenLong && lfd >= 0, "entries %d", n);
	unlink (f1);
	unlink (f2);
	CHECK ("rmdir of an empty directory", rmdir (d2) == 0, "errno %d", errno);

	/* access, realpath, getcwd / chdir */
	snprintf (p, sizeof p, "%s/c.txt", dir);
	close (open (p, O_WRONLY | O_CREAT, 0644));
	CHECK ("access F_OK / W_OK, a missing file ENOENT", access (p, F_OK) == 0 && access (p, W_OK) == 0 &&
	       access ("/tmp/posixtest-missing", F_OK) == -1 && errno == ENOENT, "errno %d", errno);
	char cwd[300], rp[300], back2[300];
	getcwd (cwd, sizeof cwd);
	int cd = chdir (dir);
	char *rr = realpath ("c.txt", rp);
	getcwd (back2, sizeof back2);
	chdir (cwd);
	CHECK ("chdir, getcwd, realpath of a relative name", cd == 0 && rr != 0 && strstr (rp, "/c.txt") && strchr (rp, ':'),
	       "chdir %d, realpath '%s', cwd '%s'", cd, rr ? rp : "(null)", back2);
	unlink (p);

	/* stdio */
	snprintf (p, sizeof p, "%s/s.txt", dir);
	FILE *f = fopen (p, "w+");
	char line[64] = "";
	long pos = -1;
	if (f)
	{
		fprintf (f, "hello %d\nworld\n", 42);
		fseek (f, 0, SEEK_SET);
		fgets (line, sizeof line, f);
		fseek (f, 0, SEEK_END);
		pos = ftell (f);
		fclose (f);
	}
	CHECK ("stdio: fopen, fprintf, fseek, fgets, ftell", f && !strcmp (line, "hello 42\n") && pos == 15, "'%s' %ld", line, pos);
	if (k_files && stat (p, &st) == 0)
	{
		long long now = time (0);
		CHECK ("stat: mtime close to now", st.st_mtime > now - 5 && st.st_mtime < now + 5, "mtime %lld now %lld", (long long) st.st_mtime, now);
	}
	else
		skip ("stat: mtime close to now", NOSYS);
	unlink (p);
}

static void group_file (const char *where)
{
	if (where)
	{
		file_group_in (where);
		return;
	}
	file_group_in ("RAM:/posixtest");
	file_group_in ("/tmp/posixtest");	/* (/tmp is RAM:/tmp) */

	s_group = "file";
	char tmpl[] = "/tmp/ptXXXXXX";
	int fd = mkstemp (tmpl);
	struct stat st;
	CHECK ("mkstemp in /tmp (RAM:/tmp)", fd >= 0 && fstat (fd, &st) == 0 && S_ISREG (st.st_mode), "fd %d errno %d", fd, errno);
	if (fd >= 0)
	{
		close (fd);
		unlink (tmpl);
	}
	FILE *t = tmpfile ();
	CHECK ("tmpfile", t != 0, "errno %d", errno);
	if (t)
		fclose (t);
}

/* =================================================================================== io == */
static int s_pipeW;
static void *t_late_write (void *a)
{
	(void) a;
	usleep (50000);
	write (s_pipeW, "x", 1);
	return 0;
}

static void group_io (void)
{
	s_group = "io";
	int pf[2];
	if (pipe (pf) != 0)
	{
		fail ("pipe", "errno %d", errno);
		return;
	}
	char b[16] = "";
	ssize_t w = write (pf[1], "hello", 5), r = read (pf[0], b, sizeof b);
	CHECK ("pipe: write then read", w == 5 && r == 5 && !memcmp (b, "hello", 5), "%zd %zd", w, r);

	fcntl (pf[0], F_SETFL, O_NONBLOCK);
	r = read (pf[0], b, sizeof b);
	CHECK ("pipe O_NONBLOCK: an empty read -> EAGAIN", r == -1 && errno == EAGAIN, "%zd errno %d", r, errno);

	struct pollfd p = { pf[0], POLLIN, 0 };
	long long t0 = ms_now ();
	int pr = poll (&p, 1, 100);
	long long dt = ms_now () - t0;
	CHECK ("poll timeout 100 ms on an empty pipe", pr == 0 && dt >= 90 && dt < 300, "%d after %lld ms", pr, dt);

	pthread_t th;
	s_pipeW = pf[1];
	pthread_create (&th, 0, t_late_write, 0);
	t0 = ms_now ();
	pr = poll (&p, 1, 2000);
	dt = ms_now () - t0;
	pthread_join (th, 0);
	CHECK ("poll wakes when another thread writes (50 ms)", pr == 1 && (p.revents & POLLIN) && dt >= 40 && dt < 200, "%d revents %x after %lld ms", pr, p.revents, dt);
	read (pf[0], b, sizeof b);

	fd_set rs;
	FD_ZERO (&rs);
	FD_SET (pf[0], &rs);
	struct timeval tv = { 0, 50000 };
	t0 = ms_now ();
	int sr = select (pf[0] + 1, &rs, 0, 0, &tv);
	dt = ms_now () - t0;
	CHECK ("select timeout 50 ms", sr == 0 && dt >= 40 && dt < 250, "%d after %lld ms", sr, dt);

	if (!k_pipe_nb)
		skip ("pipe O_NONBLOCK: a full pipe -> EAGAIN", NOSYS);
	else
	{
		fcntl (pf[1], F_SETFL, O_NONBLOCK);
		static char big[4096];
		int full = 0;
		for (int i = 0; i < 1000 && !full; i++)
			if (write (pf[1], big, sizeof big) < 0 && errno == EAGAIN)
				full = 1;
		CHECK ("pipe O_NONBLOCK: a full pipe -> EAGAIN", full, "never full");
		while (read (pf[0], big, sizeof big) > 0)
			;
	}
	close (pf[1]);
	fcntl (pf[0], F_SETFL, 0);
	r = read (pf[0], b, sizeof b);
	CHECK ("pipe: the reader sees the end once the writer closes", r == 0, "%zd", r);
	close (pf[0]);

	unsigned char rnd[64];
	int ufd = open ("/dev/urandom", O_RDONLY);
	int nz = 0;
	if (ufd >= 0 && read (ufd, rnd, sizeof rnd) == (ssize_t) sizeof rnd)
		for (int i = 0; i < 64; i++)
			nz += rnd[i] != 0;
	CHECK ("/dev/urandom", ufd >= 0 && nz > 32, "%d non-zero bytes", nz);
	if (ufd >= 0)
		close (ufd);
	int zf = open ("/dev/zero", O_RDONLY), nf = open ("/dev/null", O_WRONLY);
	memset (rnd, 1, sizeof rnd);
	CHECK ("/dev/zero and /dev/null", zf >= 0 && nf >= 0 && read (zf, rnd, 64) == 64 && rnd[63] == 0 && write (nf, rnd, 64) == 64, "%d %d", zf, nf);
	close (zf);
	close (nf);
}

/* ================================================================================= time == */
static void group_time (void)
{
	s_group = "time";
	struct timespec a, b;
	int mono = 1;
	clock_gettime (CLOCK_MONOTONIC, &a);
	for (int i = 0; i < 10000; i++)
	{
		clock_gettime (CLOCK_MONOTONIC, &b);
		if (b.tv_sec < a.tv_sec || (b.tv_sec == a.tv_sec && b.tv_nsec < a.tv_nsec))
			mono = 0;
		a = b;
	}
	usleep (2000);
	clock_gettime (CLOCK_MONOTONIC, &b);
	CHECK ("CLOCK_MONOTONIC never goes back, and moves", mono && (b.tv_sec > a.tv_sec || b.tv_nsec > a.tv_nsec), "");

	struct timeval tv;
	clock_gettime (CLOCK_REALTIME, &a);
	gettimeofday (&tv, 0);
	long long d = ((long long) tv.tv_sec - a.tv_sec) * 1000000 + (tv.tv_usec - a.tv_nsec / 1000);
	CHECK ("CLOCK_REALTIME agrees with gettimeofday and time", d >= 0 && d < 20000 && labs ((long) (time (0) - tv.tv_sec)) <= 1, "%lld us", d);
	if (!k_clock)
		skip ("clock_info: the date is UTC with the kernel's zone", NOSYS);
	else
		CHECK ("the date is after 2025 (RTC / NTP)", a.tv_sec > 1735689600, "%lld", (long long) a.tv_sec);

	long long worst = 0, sum = 0;
	for (int i = 0; i < 10; i++)
	{
		struct timespec rq = { 0, 20000000 };
		long long t0 = ms_now ();
		nanosleep (&rq, 0);
		long long e = ms_now () - t0 - 20;
		sum += e;
		if (e > worst)
			worst = e;
		if (e < -1)
			worst = 1000;
	}
	CHECK ("nanosleep 20 ms x 10: never early, mean late < 15 ms", worst < 100 && sum / 10 < 15, "worst %lld, mean %lld ms", worst, sum / 10);

	struct timespec res;
	CHECK ("clock_getres", clock_getres (CLOCK_MONOTONIC, &res) == 0 && res.tv_sec == 0 && res.tv_nsec > 0 && res.tv_nsec < 1000000, "%ld ns", res.tv_nsec);

	time_t t = 1700000000;
	struct tm g, l;
	char *oldtz = getenv ("TZ") ? strdup (getenv ("TZ")) : 0;
	setenv ("TZ", "CET-1", 1);
	tzset ();
	gmtime_r (&t, &g);
	localtime_r (&t, &l);
	CHECK ("localtime_r with TZ=CET-1 is an hour ahead of gmtime_r", (l.tm_hour - g.tm_hour + 24) % 24 == 1, "%d vs %d", l.tm_hour, g.tm_hour);
	struct tm rt = g;
	CHECK ("timegm (gmtime_r (t)) = t", timegm (&rt) == t, "%lld", (long long) timegm (&rt));
	if (oldtz)
	{
		setenv ("TZ", oldtz, 1);
		free (oldtz);
	}
	else
		unsetenv ("TZ");
	tzset ();
}

/* ================================================================================= proc == */
static const char *s_self = "SD:/bin/posixtest";

/* (ipc group) the child: a socket at fd (as WebKit's auxiliary processes: its number in argv), the
 * parent's CLOEXEC end must not be here, the dup2 target is the same kind of socket. It receives a
 * memfd by SCM_RIGHTS, maps it MAP_SHARED, checks the parent's bytes, writes its own, says "ok". */
static int ipc_child (int fd, int cloexec_fd, int dup_fd)
{
	if (fcntl (cloexec_fd, F_GETFD) != -1 || errno != EBADF)
		return 20;				/* (the CLOEXEC end leaked into the child) */
	int t = 0;
	socklen_t tl = sizeof t;
	if (getsockopt (dup_fd, SOL_SOCKET, SO_TYPE, &t, &tl) != 0 || t != SOCK_SEQPACKET)
		return 21;
	char b[16];
	char cbuf[CMSG_SPACE (sizeof (int))];
	struct iovec v = { b, sizeof b };
	struct msghdr m;
	memset (&m, 0, sizeof m);
	m.msg_iov = &v;
	m.msg_iovlen = 1;
	m.msg_control = cbuf;
	m.msg_controllen = sizeof cbuf;
	ssize_t n = recvmsg (fd, &m, MSG_CMSG_CLOEXEC);
	struct cmsghdr *c = CMSG_FIRSTHDR (&m);
	if (n != 3 || c == 0 || c->cmsg_type != SCM_RIGHTS)
		return 22;
	int mfd;
	memcpy (&mfd, CMSG_DATA (c), sizeof mfd);
	struct stat st;
	if (fstat (mfd, &st) != 0 || st.st_size != 100000)
		return 23;
	unsigned char *p = (unsigned char *) mmap (0, 100000, PROT_READ | PROT_WRITE, MAP_SHARED, mfd, 0);
	if (p == MAP_FAILED || p[0] != 0x11 || p[99999] != 0x22)
		return 24;
	p[50000] = 0x33;
	munmap (p, 100000);
	close (mfd);
	return send (fd, "ok", 2, MSG_NOSIGNAL) == 2 ? 30 : 25;
}

static int child_main (int argc, char **argv)
{
	const char *mode = argc > 2 ? argv[2] : "";
	if (!strcmp (mode, "exit42"))
		return 42;
	if (!strcmp (mode, "crash"))
	{
		volatile int *volatile bad = (volatile int *) 16;
		*bad = 1;
		return 0;
	}
	if (!strcmp (mode, "env"))
	{
		const char *v = getenv ("POSIXTEST_X");
		return v && !strcmp (v, "hello world") ? 7 : 8;
	}
	if (!strcmp (mode, "argv"))
		return argc == 5 && !strcmp (argv[3], "a b") && !strcmp (argv[4], "\"q\"") ? 9 : 10;
	if (!strcmp (mode, "ipc") && argc == 6)
		return ipc_child (atoi (argv[3]), atoi (argv[4]), atoi (argv[5]));
	return 1;
}

static int spawn_wait (char *const *argv, char *const *envp, int *status)
{
	pid_t pid;
	int r = posix_spawn (&pid, s_self, 0, 0, argv, envp);
	if (r != 0)
		return -r;
	return waitpid (pid, status, 0) == pid ? 0 : -1000 - errno;
}

static void group_proc (void)
{
	s_group = "proc";
	CHECK ("getpid, getppid", getpid () > 0 && getppid () > 0, "%d %d", getpid (), getppid ());

	setenv ("POSIXTEST_Y", "1", 1);
	char *y = getenv ("POSIXTEST_Y");
	unsetenv ("POSIXTEST_Y");
	CHECK ("setenv / getenv / unsetenv", y && !strcmp (y, "1") && getenv ("POSIXTEST_Y") == 0, "");
	if (!k_env)
		skip ("the environment came from the kernel (HOME, PATH)", NOSYS);
	else
		CHECK ("the environment came from the kernel (HOME, PATH)", getenv ("HOME") && getenv ("PATH"), "");

	int st = 0;
	char *a1[] = { (char *) s_self, "--child", "exit42", 0 };
	int r = spawn_wait (a1, environ, &st);
	CHECK ("posix_spawn of itself, waitpid: exit 42", r == 0 && WIFEXITED (st) && WEXITSTATUS (st) == 42, "%d status %x", r, st);

	if (!k_proc)
	{
		/* (the old spawn passes one line, no environment, and a crash shows up as exit 0) */
		skip ("argv: an argument with a space, one with quotes", NOSYS);
		skip ("the child gets the environment", NOSYS);
		skip ("a crashing child: WIFSIGNALED, SIGSEGV", NOSYS);
	}
	else
	{
		/* (no empty argument: the kernel's argv block ends at an empty string) */
		char *a2[] = { (char *) s_self, "--child", "argv", "a b", "\"q\"", 0 };
		r = spawn_wait (a2, environ, &st);
		CHECK ("argv: an argument with a space, one with quotes", r == 0 && WIFEXITED (st) && WEXITSTATUS (st) == 9, "%d status %x", r, st);

		setenv ("POSIXTEST_X", "hello world", 1);
		char *a3[] = { (char *) s_self, "--child", "env", 0 };
		r = spawn_wait (a3, environ, &st);
		CHECK ("the child gets the environment", r == 0 && WIFEXITED (st) && WEXITSTATUS (st) == 7, "%d status %x", r, st);
		unsetenv ("POSIXTEST_X");

		char *a4[] = { (char *) s_self, "--child", "crash", 0 };
		r = spawn_wait (a4, environ, &st);
		CHECK ("a crashing child: WIFSIGNALED, SIGSEGV", r == 0 && WIFSIGNALED (st) && WTERMSIG (st) == SIGSEGV, "%d status %x", r, st);
	}

	CHECK ("waitpid with no child -> ECHILD", waitpid (-1, &st, WNOHANG) == -1 && errno == ECHILD, "errno %d", errno);
	CHECK ("fork -> ENOSYS", fork () == -1 && errno == ENOSYS, "errno %d", errno);
}

/* ================================================================================== ipc == */
static int send_fds (int s, const char *data, const int *fds, int n)
{
	char cbuf[CMSG_SPACE (8 * sizeof (int))];
	struct iovec v[3] = { { (void *) data, 1 }, { (void *) (data + 1), 1 }, { (void *) (data + 2), strlen (data) - 2 } };
	struct msghdr m;
	memset (&m, 0, sizeof m);
	memset (cbuf, 0, sizeof cbuf);
	m.msg_iov = v;
	m.msg_iovlen = 3;
	if (n > 0)
	{
		m.msg_control = cbuf;
		m.msg_controllen = CMSG_SPACE (n * sizeof (int));
		struct cmsghdr *c = CMSG_FIRSTHDR (&m);
		c->cmsg_level = SOL_SOCKET;
		c->cmsg_type = SCM_RIGHTS;
		c->cmsg_len = CMSG_LEN (n * sizeof (int));
		memcpy (CMSG_DATA (c), fds, n * sizeof (int));
	}
	return (int) sendmsg (s, &m, MSG_NOSIGNAL);
}

/* -> bytes; the fds received in fds (*n in: room, out: count), *mflags */
static int recv_fds (int s, char *data, size_t cap, int *fds, int *n, int *mflags, int flags)
{
	char cbuf[CMSG_SPACE (8 * sizeof (int))];
	struct iovec v = { data, cap };
	struct msghdr m;
	memset (&m, 0, sizeof m);
	m.msg_iov = &v;
	m.msg_iovlen = 1;
	m.msg_control = cbuf;
	m.msg_controllen = CMSG_LEN (*n * sizeof (int));	/* (room for exactly *n) */
	int r = (int) recvmsg (s, &m, flags);
	int got = 0;
	for (struct cmsghdr *c = r >= 0 ? CMSG_FIRSTHDR (&m) : 0; c; c = CMSG_NXTHDR (&m, c))
		if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_RIGHTS)
		{
			int k = (int) ((c->cmsg_len - CMSG_LEN (0)) / sizeof (int));
			memcpy (fds + got, CMSG_DATA (c), k * sizeof (int));
			got += k;
		}
	*n = got;
	*mflags = m.msg_flags;
	return r;
}

static void group_ipc (void)
{
	s_group = "ipc";
	if (!k_ipc)
	{
		int sv[2];
		CHECK ("socketpair (AF_UNIX) without the kernel's v76 -> EOPNOTSUPP",
		       socketpair (AF_UNIX, SOCK_STREAM, 0, sv) == -1 && errno == EOPNOTSUPP, "errno %d", errno);
		skip ("local sockets, SCM_RIGHTS, memfd, shm_open, posix_spawn with descriptors", NOSYS);
		return;
	}
	CHECK ("socket (AF_UNIX) -> EAFNOSUPPORT (no named local sockets)",
	       socket (AF_UNIX, SOCK_STREAM, 0) == -1 && errno == EAFNOSUPPORT, "errno %d", errno);

	/* SEQPACKET (WebKit's connection), CLOEXEC */
	int sp[2];
	int r = socketpair (AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, sp);
	CHECK ("socketpair (AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC)", r == 0 && (fcntl (sp[0], F_GETFD) & FD_CLOEXEC), "r %d errno %d", r, errno);
	if (r != 0)
		return;
	char b[64];
	send (sp[0], "hello", 5, 0);
	send (sp[0], "you", 3, 0);
	ssize_t a1 = recv (sp[1], b, sizeof b, 0), a2 = recv (sp[1], b + 8, sizeof b - 8, 0);
	CHECK ("SEQPACKET keeps the boundaries", a1 == 5 && a2 == 3 && !memcmp (b, "hello", 5) && !memcmp (b + 8, "you", 3), "%d %d", (int) a1, (int) a2);
	struct pollfd pf = { sp[1], POLLIN, 0 };
	long long t0 = ms_now ();
	int pr = poll (&pf, 1, 100);
	long long dt = ms_now () - t0;
	CHECK ("poll on an empty local socket: the timeout", pr == 0 && dt >= 90 && dt < 400, "%d in %lld ms", pr, dt);
	send (sp[0], "x", 1, 0);
	pr = poll (&pf, 1, 1000);
	CHECK ("poll: POLLIN", pr == 1 && (pf.revents & POLLIN), "%d %x", pr, pf.revents);
	recv (sp[1], b, sizeof b, 0);
	struct ucred cr;
	socklen_t cl = sizeof cr;
	int dom = 0;
	socklen_t dl = sizeof dom;
	memset (&cr, 0, sizeof cr);
	CHECK ("SO_PEERCRED: our pid; SO_DOMAIN AF_UNIX",
	       getsockopt (sp[0], SOL_SOCKET, SO_PEERCRED, &cr, &cl) == 0 && cr.pid == getpid ()
	       && getsockopt (sp[0], SOL_SOCKET, SO_DOMAIN, &dom, &dl) == 0 && dom == AF_UNIX, "pid %d dom %d", (int) cr.pid, dom);
	struct sockaddr_un un;
	socklen_t ul = sizeof un;
	memset (&un, 0, sizeof un);
	CHECK ("getsockname: AF_UNIX, unnamed", getsockname (sp[0], (struct sockaddr *) &un, &ul) == 0 && un.sun_family == AF_UNIX
	       && ul == sizeof (sa_family_t), "len %u", (unsigned) ul);

	/* memfd: ftruncate, fstat, seals, two MAP_SHARED mappings alias, MAP_PRIVATE copies */
	int mfd = memfd_create ("posixtest", MFD_CLOEXEC | MFD_ALLOW_SEALING);
	struct stat st;
	r = mfd >= 0 && ftruncate (mfd, 100000) == 0 && fstat (mfd, &st) == 0 && st.st_size == 100000 && S_ISREG (st.st_mode);
	CHECK ("memfd_create, ftruncate, fstat", r, "fd %d errno %d", mfd, errno);
	unsigned char *m1 = (unsigned char *) mmap (0, 100000, PROT_READ | PROT_WRITE, MAP_SHARED, mfd, 0);
	unsigned char *m2 = (unsigned char *) mmap (0, 100000, PROT_READ, MAP_SHARED, mfd, 0);
	int alias = m1 != MAP_FAILED && m2 != MAP_FAILED && m1 != m2;
	if (alias)
	{
		m1[0] = 0x11;
		m1[99999] = 0x22;
		alias = m2[0] == 0x11 && m2[99999] == 0x22;
	}
	CHECK ("mmap MAP_SHARED twice: the same pages", alias, "%p %p", (void *) m1, (void *) m2);
	unsigned char *mp = (unsigned char *) mmap (0, 100000, PROT_READ | PROT_WRITE, MAP_PRIVATE, mfd, 0);
	int copy = mp != MAP_FAILED && mp[0] == 0x11;
	if (copy)
	{
		mp[0] = 0x44;
		copy = m1[0] == 0x11;
		munmap (mp, 100000);
	}
	CHECK ("mmap MAP_PRIVATE of it: a copy", copy, "");
	r = fcntl (mfd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW);
	CHECK ("F_ADD_SEALS / F_GET_SEALS; ftruncate then EPERM", r == 0
	       && fcntl (mfd, F_GET_SEALS) == (F_SEAL_SHRINK | F_SEAL_GROW) && ftruncate (mfd, 5) == -1 && errno == EPERM, "r %d errno %d", r, errno);

	/* sendmsg / recvmsg with SCM_RIGHTS: a memfd and a pipe's write end, 3 iovecs */
	int pp[2];
	pipe (pp);
	int out[2] = { mfd, pp[1] };
	int sn = send_fds (sp[0], "fds!", out, 2);
	int in[8], n = 8, mf = 0;
	int rn = recv_fds (sp[1], b, sizeof b, in, &n, &mf, 0);
	CHECK ("sendmsg / recvmsg: 3 iovecs, 2 descriptors (CMSG_FIRSTHDR / CMSG_NXTHDR)", sn == 4 && rn == 4 && !memcmp (b, "fds!", 4)
	       && n == 2 && in[0] != mfd && in[1] != pp[1] && mf == 0, "%d %d n %d flags %x", sn, rn, n, mf);
	if (n == 2)
	{
		unsigned char *m3 = (unsigned char *) mmap (0, 100000, PROT_READ, MAP_SHARED, in[0], 0);
		CHECK ("the memfd received maps the same object", m3 != MAP_FAILED && m3[99999] == 0x22, "");
		if (m3 != MAP_FAILED)
			munmap (m3, 100000);
		ssize_t w = write (in[1], "pipe", 4);
		char pb[8];
		ssize_t k = read (pp[0], pb, sizeof pb);
		CHECK ("the pipe end received writes into our pipe", w == 4 && k == 4 && !memcmp (pb, "pipe", 4), "%d %d", (int) w, (int) k);
		close (in[0]);
		close (in[1]);
	}
	/* MSG_CTRUNC (room for 1 of 3), MSG_CMSG_CLOEXEC */
	int three[3] = { mfd, pp[0], pp[1] };
	send_fds (sp[0], "abc", three, 3);
	n = 1;
	rn = recv_fds (sp[1], b, sizeof b, in, &n, &mf, MSG_CMSG_CLOEXEC);
	CHECK ("MSG_CTRUNC: room for 1 of 3; MSG_CMSG_CLOEXEC", rn == 3 && n == 1 && (mf & MSG_CTRUNC)
	       && (fcntl (in[0], F_GETFD) & FD_CLOEXEC), "%d n %d flags %x", rn, n, mf);
	if (n == 1)
		close (in[0]);

	/* STREAM and DGRAM pairs */
	int ss[2], ds[2];
	r = socketpair (AF_UNIX, SOCK_STREAM, 0, ss);
	send (ss[0], "ab", 2, 0);
	send (ss[0], "cd", 2, 0);
	usleep (10000);
	ssize_t k = recv (ss[1], b, sizeof b, 0);
	CHECK ("SOCK_STREAM pair: the bytes of two sends", r == 0 && k == 4 && !memcmp (b, "abcd", 4), "%d", (int) k);
	close (ss[0]);
	ssize_t e0 = recv (ss[1], b, sizeof b, 0);
	ssize_t e1 = send (ss[1], "x", 1, MSG_NOSIGNAL);
	CHECK ("SOCK_STREAM: the peer closed -> 0, and EPIPE back", e0 == 0 && e1 == -1 && errno == EPIPE, "%d %d errno %d", (int) e0, (int) e1, errno);
	close (ss[1]);
	r = socketpair (AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK, 0, ds);
	CHECK ("SOCK_DGRAM | SOCK_NONBLOCK pair: EAGAIN when empty", r == 0 && recv (ds[1], b, sizeof b, 0) == -1 && errno == EAGAIN, "errno %d", errno);
	if (r == 0)
	{
		close (ds[0]);
		close (ds[1]);
	}

	/* shm_open / shm_unlink */
	shm_unlink ("/posixtest");
	int s1 = shm_open ("/posixtest", O_RDWR | O_CREAT | O_EXCL, 0600);
	int s2 = s1 >= 0 ? shm_open ("/posixtest", O_RDWR, 0) : -1;
	r = s1 >= 0 && s2 >= 0 && ftruncate (s1, 4096) == 0 && (fcntl (s1, F_GETFD) & FD_CLOEXEC);
	unsigned char *q1 = r ? (unsigned char *) mmap (0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, s1, 0) : MAP_FAILED;
	unsigned char *q2 = r ? (unsigned char *) mmap (0, 4096, PROT_READ, MAP_SHARED, s2, 0) : MAP_FAILED;
	int same = q1 != MAP_FAILED && q2 != MAP_FAILED;
	if (same)
	{
		q1[7] = 9;
		same = q2[7] == 9;
	}
	CHECK ("shm_open twice: the same object (FD_CLOEXEC)", same, "%d %d", s1, s2);
	CHECK ("shm_unlink, then the name is gone", shm_unlink ("/posixtest") == 0 && shm_open ("/posixtest", O_RDWR, 0) == -1
	       && errno == ENOENT, "errno %d", errno);
	if (q1 != MAP_FAILED) munmap (q1, 4096);
	if (q2 != MAP_FAILED) munmap (q2, 4096);
	if (s1 >= 0) close (s1);
	if (s2 >= 0) close (s2);

	/* posix_spawn: the client end (no CLOEXEC) at its number; the server end (CLOEXEC) not given;
	 * a dup2 onto 30; the child receives the memfd and writes into it */
	int cs[2];
	socketpair (AF_UNIX, SOCK_SEQPACKET, 0, cs);
	fcntl (cs[0], F_SETFD, FD_CLOEXEC);			/* (the server end: ours only) */
	posix_spawn_file_actions_t fa;
	posix_spawn_file_actions_init (&fa);
	posix_spawn_file_actions_adddup2 (&fa, cs[1], 30);
	char fdn[3][12];
	snprintf (fdn[0], sizeof fdn[0], "%d", cs[1]);
	snprintf (fdn[1], sizeof fdn[1], "%d", cs[0]);
	snprintf (fdn[2], sizeof fdn[2], "%d", 30);
	char *av[] = { (char *) s_self, "--child", "ipc", fdn[0], fdn[1], fdn[2], 0 };
	pid_t pid = 0;
	r = posix_spawn (&pid, s_self, &fa, 0, av, environ);
	posix_spawn_file_actions_destroy (&fa);
	close (cs[1]);
	int one[1] = { mfd };
	sn = send_fds (cs[0], "mem", one, 1);
	char ok[4] = "";
	ssize_t okn = recv (cs[0], ok, sizeof ok, 0);
	int status = 0;
	pid_t w = r == 0 ? waitpid (pid, &status, 0) : -1;
	CHECK ("posix_spawn: the socket at its number in the child, CLOEXEC kept out, a dup2 onto 30",
	       r == 0 && w == pid && WIFEXITED (status) && WEXITSTATUS (status) == 30, "spawn %d status %x", r, status);
	CHECK ("the child got the memfd, mapped it, wrote; we see it", sn == 3 && okn == 2 && m1 != MAP_FAILED && m1[50000] == 0x33,
	       "%d %d %x", sn, (int) okn, m1 != MAP_FAILED ? m1[50000] : 0);
	close (cs[0]);
	if (m1 != MAP_FAILED) munmap (m1, 100000);
	if (m2 != MAP_FAILED) munmap (m2, 100000);
	close (mfd);
	close (pp[0]);
	close (pp[1]);
	close (sp[0]);
	close (sp[1]);
}

/* ================================================================================== net == */
static void group_net (void)
{
	s_group = "net";
	struct addrinfo h, *ai = 0;
	memset (&h, 0, sizeof h);
	h.ai_socktype = SOCK_STREAM;
	int g = getaddrinfo ("10.1.2.3", "https", &h, &ai);
	CHECK ("getaddrinfo numeric host + service name", g == 0 && ai && ai->ai_family == AF_INET &&
	       ntohs (((struct sockaddr_in *) ai->ai_addr)->sin_port) == 443 &&
	       ((struct sockaddr_in *) ai->ai_addr)->sin_addr.s_addr == inet_addr ("10.1.2.3"), "%d", g);
	if (ai)
		freeaddrinfo (ai);
	h.ai_family = AF_INET6;
	CHECK ("getaddrinfo AF_INET6 -> EAI_FAMILY", getaddrinfo ("localhost", "80", &h, &ai) == EAI_FAMILY, "");
	h.ai_family = AF_UNSPEC;
	h.ai_flags = AI_NUMERICHOST;
	CHECK ("getaddrinfo AI_NUMERICHOST with a name -> EAI_NONAME", getaddrinfo ("example.com", "80", &h, &ai) == EAI_NONAME, "");

	unsigned char a6[16];
	char s6[64], s4[16];
	struct in_addr a4;
	CHECK ("inet_pton / inet_ntop (IPv4, IPv6)",
	       inet_pton (AF_INET, "192.168.1.20", &a4) == 1 && !strcmp (inet_ntop (AF_INET, &a4, s4, sizeof s4), "192.168.1.20") &&
	       inet_pton (AF_INET6, "2001:db8::1", a6) == 1 && !strcmp (inet_ntop (AF_INET6, a6, s6, sizeof s6), "2001:db8::1"),
	       "%s %s", s4, s6);
	CHECK ("socket (AF_INET6) -> EAFNOSUPPORT", socket (AF_INET6, SOCK_STREAM, 0) == -1 && errno == EAFNOSUPPORT, "errno %d", errno);

	char ip[32];
	if (kapi_net_status (ip, sizeof ip) == 0)
	{
		skip ("DNS, TCP connect / send / recv, MSG_PEEK, names, UDP", "no network");
		return;
	}
	memset (&h, 0, sizeof h);
	h.ai_socktype = SOCK_STREAM;
	ai = 0;
	g = getaddrinfo ("example.com", "80", &h, &ai);
	CHECK ("getaddrinfo (\"example.com\") through the kernel's DNS", g == 0 && ai, "%s", gai_strerror (g));
	if (g != 0)
		return;

	int s = socket (AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
	int c = connect (s, ai->ai_addr, ai->ai_addrlen);
	int ce = errno;
	if (c == -1 && ce == EINPROGRESS)
	{
		struct pollfd p = { s, POLLOUT, 0 };
		int pr = poll (&p, 1, 10000);
		int soerr = -1;
		socklen_t sl = sizeof soerr;
		getsockopt (s, SOL_SOCKET, SO_ERROR, &soerr, &sl);
		CHECK ("non-blocking connect: EINPROGRESS, poll (POLLOUT), SO_ERROR 0", pr == 1 && (p.revents & POLLOUT) && soerr == 0, "poll %d revents %x so_error %d", pr, p.revents, soerr);
	}
	else if (!k_net)
		CHECK ("connect (the old tcp_connect: blocking)", c == 0, "errno %d", ce);
	else
		fail ("non-blocking connect: EINPROGRESS, poll (POLLOUT), SO_ERROR 0", "connect %d errno %d", c, ce);
	freeaddrinfo (ai);

	const char *req = "HEAD / HTTP/1.0\r\nHost: example.com\r\n\r\n";
	ssize_t w = send (s, req, strlen (req), MSG_NOSIGNAL);
	CHECK ("send", w == (ssize_t) strlen (req), "%zd errno %d", w, errno);
	struct pollfd p = { s, POLLIN, 0 };
	poll (&p, 1, 10000);
	char peek[8] = "", got[8] = "";
	ssize_t pk = recv (s, peek, 7, MSG_PEEK);
	fcntl (s, F_SETFL, 0);
	ssize_t rv = recv (s, got, 7, MSG_WAITALL);
	CHECK ("MSG_PEEK, then the same bytes read", pk == 7 && rv == 7 && !memcmp (peek, got, 7) && !memcmp (got, "HTTP/1.", 7), "'%s' '%s'", peek, got);

	struct sockaddr_in me, peer;
	socklen_t ml = sizeof me, pl = sizeof peer;
	int n1 = getsockname (s, (struct sockaddr *) &me, &ml), n2 = getpeername (s, (struct sockaddr *) &peer, &pl);
	if (!k_net)
		skip ("getsockname / getpeername", NOSYS);
	else
		CHECK ("getsockname / getpeername", n1 == 0 && n2 == 0 && ntohs (peer.sin_port) == 80 && ntohs (me.sin_port) != 0, "%d %d", n1, n2);
	char drain[4096];
	while (recv (s, drain, sizeof drain, 0) > 0)
		;
	close (s);

	if (!k_net)
	{
		skip ("UDP: a DNS query with sendto / recvfrom", NOSYS);
		return;
	}
	int u = socket (AF_INET, SOCK_DGRAM, 0);
	struct sockaddr_in dns;
	memset (&dns, 0, sizeof dns);
	dns.sin_family = AF_INET;
	dns.sin_port = htons (53);
	dns.sin_addr.s_addr = inet_addr ("8.8.8.8");
	unsigned char q[] = { 0x12, 0x34, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 7, 'e', 'x', 'a', 'm', 'p', 'l', 'e', 3, 'c', 'o', 'm', 0, 0, 1, 0, 1 };
	struct timeval to = { 3, 0 };
	setsockopt (u, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof to);
	ssize_t us = sendto (u, q, sizeof q, 0, (struct sockaddr *) &dns, sizeof dns);
	unsigned char ans[512];
	struct sockaddr_in from;
	socklen_t fl = sizeof from;
	ssize_t ur = recvfrom (u, ans, sizeof ans, 0, (struct sockaddr *) &from, &fl);
	CHECK ("UDP: a DNS query with sendto / recvfrom", us == (ssize_t) sizeof q && ur > 12 && ans[0] == 0x12 && ans[1] == 0x34 && ntohs (from.sin_port) == 53,
	       "sent %zd, got %zd errno %d", us, ur, errno);
	close (u);
}

/* ================================================================================= loop == */
/* TCP and UDP between two sockets of this program on 127.0.0.1 -- not in the default groups:
 * Onyx's network stack may not loop back (the PC bench, tools/tests/posixsim, runs it). */
static void *t_server (void *a)
{
	int ls = (int) (long) a;
	struct pollfd p = { ls, POLLIN, 0 };
	if (poll (&p, 1, 5000) != 1)
		return (void *) 1;
	struct sockaddr_in peer;
	socklen_t pl = sizeof peer;
	int c = accept4 (ls, (struct sockaddr *) &peer, &pl, SOCK_CLOEXEC);
	if (c < 0)
		return (void *) 2;
	char b[16];
	ssize_t n = recv (c, b, sizeof b, MSG_WAITALL & 0);
	if (n != 4 || memcmp (b, "ping", 4))
	{
		close (c);
		return (void *) 3;
	}
	send (c, "pong!", 5, MSG_NOSIGNAL);
	usleep (50000);
	close (c);
	return 0;
}

static void group_loop (void)
{
	s_group = "loop";
	/* (without the v75 sockets: the old tcp_* calls -- a fixed port, a blocking accept) */
	struct sockaddr_in a;
	memset (&a, 0, sizeof a);
	a.sin_family = AF_INET;
	a.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
	a.sin_port = k_net ? 0 : htons (47123);
	int ls = socket (AF_INET, SOCK_STREAM | (k_net ? SOCK_NONBLOCK : 0), 0);
	socklen_t al = sizeof a;
	int ok = ls >= 0 && bind (ls, (struct sockaddr *) &a, sizeof a) == 0 && listen (ls, 4) == 0 &&
		 (!k_net || (getsockname (ls, (struct sockaddr *) &a, &al) == 0 && a.sin_port != 0));
	CHECK (k_net ? "socket, bind (port 0), listen, getsockname" : "socket, bind, listen (the old tcp_listen)", ok, "errno %d", errno);
	if (!ok)
		return;
	if (k_net)
	{
		int e = accept (ls, 0, 0);
		CHECK ("accept on a non-blocking socket with nobody -> EAGAIN", e == -1 && errno == EAGAIN, "%d errno %d", e, errno);
	}

	pthread_t th;
	pthread_create (&th, 0, t_server, (void *) (long) ls);
	int s = socket (AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
	int c = connect (s, (struct sockaddr *) &a, sizeof a);
	int ce = errno;
	struct pollfd p = { s, POLLOUT, 0 };
	int pr = poll (&p, 1, 3000);
	int soerr = -1;
	socklen_t sl = sizeof soerr;
	getsockopt (s, SOL_SOCKET, SO_ERROR, &soerr, &sl);
	CHECK ("non-blocking connect, poll (POLLOUT), SO_ERROR", (c == 0 || ce == EINPROGRESS) && pr == 1 && (p.revents & POLLOUT) && soerr == 0,
	       "connect %d errno %d, poll %d revents %x, so_error %d", c, ce, pr, p.revents, soerr);
	ssize_t w = send (s, "ping", 4, MSG_NOSIGNAL);
	p.events = POLLIN;
	pr = poll (&p, 1, 3000);
	char pk[8] = "", got[8] = "";
	ssize_t r1 = recv (s, pk, 5, MSG_PEEK);
	ssize_t r2 = recv (s, got, 5, 0);
	CHECK ("send, poll (POLLIN), MSG_PEEK then recv", w == 4 && pr == 1 && r1 == 5 && r2 == 5 && !memcmp (pk, "pong!", 5) && !memcmp (got, "pong!", 5),
	       "send %zd poll %d peek %zd recv %zd", w, pr, r1, r2);
	struct sockaddr_in peer;
	socklen_t pl = sizeof peer;
	CHECK ("getpeername", getpeername (s, (struct sockaddr *) &peer, &pl) == 0 && peer.sin_port == a.sin_port, "port %d", ntohs (peer.sin_port));
	if (!k_net)
		fcntl (s, F_SETFL, 0);			/* (the old recv: blocking until the end) */
	pr = poll (&p, 1, 3000);
	ssize_t r3 = recv (s, got, sizeof got, 0);
	CHECK ("the peer closes: poll wakes, recv -> 0", pr == 1 && r3 == 0, "poll %d revents %x recv %zd errno %d", pr, p.revents, r3, errno);
	void *sr = (void *) 9;
	pthread_join (th, &sr);
	CHECK ("the server side: poll on the listening socket, accept4, recv", sr == 0, "%p", sr);
	close (s);
	if (!k_net)
	{
		close (ls);
		skip ("connect to a closed port, UDP", NOSYS);
		return;
	}

	/* nothing listens on the listening socket's port once it is closed */
	close (ls);
	int s2 = socket (AF_INET, SOCK_STREAM, 0);
	int c2 = connect (s2, (struct sockaddr *) &a, sizeof a);
	CHECK ("connect to a closed port -> ECONNREFUSED", c2 == -1 && errno == ECONNREFUSED, "%d errno %d", c2, errno);
	close (s2);

	int u1 = socket (AF_INET, SOCK_DGRAM, 0), u2 = socket (AF_INET, SOCK_DGRAM, 0);
	struct sockaddr_in ua = a;
	ua.sin_port = 0;
	al = sizeof ua;
	bind (u1, (struct sockaddr *) &ua, sizeof ua);
	getsockname (u1, (struct sockaddr *) &ua, &al);
	struct timeval to = { 2, 0 };
	setsockopt (u1, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof to);
	ssize_t us = sendto (u2, "hey", 3, 0, (struct sockaddr *) &ua, sizeof ua);
	char ub[8] = "";
	struct sockaddr_in from;
	socklen_t fl = sizeof from;
	ssize_t ur = recvfrom (u1, ub, sizeof ub, 0, (struct sockaddr *) &from, &fl);
	CHECK ("UDP: sendto / recvfrom (SO_RCVTIMEO)", us == 3 && ur == 3 && !memcmp (ub, "hey", 3) && from.sin_addr.s_addr == htonl (INADDR_LOOPBACK),
	       "sent %zd got %zd errno %d", us, ur, errno);
	close (u1);
	close (u2);
}

/* ================================================================================= misc == */
static volatile int s_sig;
static void on_sig (int s) { s_sig = s; }

static void group_misc (void)
{
	s_group = "misc";
	CHECK ("sysconf (_SC_NPROCESSORS_ONLN) = 1", sysconf (_SC_NPROCESSORS_ONLN) == 1, "%ld", sysconf (_SC_NPROCESSORS_ONLN));
	struct utsname u;
	CHECK ("uname: Onyx, aarch64", uname (&u) == 0 && !strcmp (u.sysname, "Onyx") && !strcmp (u.machine, "aarch64"), "%s %s", u.sysname, u.machine);
	unsigned char r[32];
	memset (r, 0, sizeof r);
	int nz = 0;
	ssize_t gr = getrandom (r, sizeof r, 0);
	for (int i = 0; i < 32; i++)
		nz += r[i] != 0;
	CHECK ("getrandom, getentropy", gr == 32 && nz > 16 && getentropy (r, 32) == 0, "%zd %d", gr, nz);

	struct sigaction sa, old;
	memset (&sa, 0, sizeof sa);
	sa.sa_handler = on_sig;
	sigaction (SIGUSR1, &sa, &old);
	raise (SIGUSR1);
	CHECK ("sigaction + raise: the handler runs", s_sig == SIGUSR1, "%d", s_sig);
	s_sig = 0;
	kill (getpid (), SIGUSR1);
	CHECK ("kill (getpid (), sig): the handler runs", s_sig == SIGUSR1, "%d", s_sig);
	CHECK ("signal (SIGPIPE, SIG_IGN), kill (self, 0)", signal (SIGPIPE, SIG_IGN) != SIG_ERR && kill (getpid (), 0) == 0, "");
	sigset_t ss;
	sigemptyset (&ss);
	sigaddset (&ss, SIGINT);
	CHECK ("sigprocmask / sigismember", sigprocmask (SIG_BLOCK, &ss, 0) == 0 && sigismember (&ss, SIGINT) == 1, "");

	struct rlimit rl;
	CHECK ("getrlimit (RLIMIT_NOFILE) = 1024", getrlimit (RLIMIT_NOFILE, &rl) == 0 && rl.rlim_cur == 1024, "%llu", (unsigned long long) rl.rlim_cur);
	char hn[64];
	CHECK ("gethostname", gethostname (hn, sizeof hn) == 0 && hn[0], "");
	CHECK ("isatty (1) on the console, not on a pipe", isatty (1) == 1, "");
	errno = 0;
	void *dl = dlopen ("libfoo.so", 0);
	CHECK ("dlopen fails cleanly (static programs)", dl == 0 && dlerror () != 0, "");
	CHECK ("strerror (ENOSYS)", strerror (ENOSYS) && strlen (strerror (ENOSYS)) > 3, "");
}

/* The C++ part is its own program, built with WP-TC's aarch64-onyx-elf (posixtest-cxx.cpp), next
 * to this one: run it (its PASS / FAIL lines on the same output), one check on its exit status
 * (its number of failures). */
static void group_cxx (void)
{
	s_group = "cxx";
	char path[256];
	const char *slash = strrchr (s_self, '/');
	size_t n = slash ? (size_t) (slash - s_self) : 0;
	if (n == 0 || n + sizeof "/posixtest-cxx" > sizeof path)
	{
		strcpy (path, "SD:/bin");
		n = strlen (path);
	}
	else
		memcpy (path, s_self, n);
	strcpy (path + n, "/posixtest-cxx");
	struct stat st;
	if (stat (path, &st) != 0)
	{
		skip ("std::thread, mutex, condition_variable, thread_local, statics, filesystem, steady_clock",
		      "no posixtest-cxx next to posixtest (built with the aarch64-onyx-elf toolchain, WP-TC)");
		return;
	}
	fflush (stdout);
	char *argv[] = { path, 0 };
	extern char **environ;
	pid_t pid;
	int r = posix_spawn (&pid, path, 0, 0, argv, environ);
	int status = 0;
	if (r == 0 && waitpid (pid, &status, 0) != pid)
		r = errno;
	CHECK ("posixtest-cxx: std::thread, mutex, condition_variable, thread_local, statics, filesystem, "
	       "steady_clock, async / future, exceptions across threads",
	       r == 0 && WIFEXITED (status) && WEXITSTATUS (status) == 0,
	       "spawn %d, status %x: %d failed", r, status, WIFEXITED (status) ? WEXITSTATUS (status) : -1);
}

int main (int argc, char **argv)
{
	if (argc > 2 && !strcmp (argv[1], "--child"))
		return child_main (argc, argv);
	if (argv[0] && strchr (argv[0], ':'))
		s_self = argv[0];

	setvbuf (stdout, 0, _IOLBF, 0);
	probe_kernel ();
	static const char *all[] = { "mem", "thread", "file", "io", "time", "proc", "ipc", "net", "misc", "cxx", 0 };
	const char *const *groups = all;
	const char *sel[16];
	const char *fileDir = 0;
	if (argc > 1)
	{
		int n = 0;
		for (int i = 1; i < argc && n < 15; i++)
		{
			if (strchr (argv[i], ':') || argv[i][0] == '/')
				fileDir = argv[i];
			else
				sel[n++] = argv[i];
		}
		sel[n] = 0;
		if (n)
			groups = sel;
	}
	for (int i = 0; groups[i]; i++)
	{
		const char *g = groups[i];
		if (!strcmp (g, "mem")) group_mem ();
		else if (!strcmp (g, "thread")) group_thread ();
		else if (!strcmp (g, "file")) group_file (fileDir);
		else if (!strcmp (g, "io")) group_io ();
		else if (!strcmp (g, "time")) group_time ();
		else if (!strcmp (g, "proc")) group_proc ();
		else if (!strcmp (g, "ipc")) group_ipc ();
		else if (!strcmp (g, "net")) group_net ();
		else if (!strcmp (g, "loop")) group_loop ();
		else if (!strcmp (g, "misc")) group_misc ();
		else if (!strcmp (g, "cxx")) group_cxx ();
		else printf ("posixtest: no group '%s' (mem thread file io time proc ipc net misc cxx loop)\n", g);
	}
	printf ("posixtest: %d passed, %d failed, %d skipped\n", s_pass, s_fail, s_skip);
	return s_fail;
}
