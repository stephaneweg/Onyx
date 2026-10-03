// onyxcores.c -- WebKit's tile rasterisation on Onyx's app cores (kapi v51: kapi_core_acquire /
// kapi_core_run; docs/03 "App cores"). WebKit's compositor (USE(GRAPHICS_LAYER_ONYX),
// LayerTreeHostOnyx) records what a frame has to paint into Skia pictures on the main thread and has
// them replayed into the tiles' pixels here: on up to two app cores, and on the main thread too.
// Source/WebKit/Shared/onyx/OnyxCores.h declares these functions; this file is linked with the
// program (build-web.sh, build-wk2test.sh) with the four --wrap flags below.
//
// An app core is not a thread: its code makes no kernel call, its waits spin, and it starts with the
// TLS of whoever called kapi_core_run. What is done here so that Skia may run there:
//
// - Its own TLS. A worker sets TPIDR_EL0 to a block of its own (libonyxposix's __onyx_thread_alloc:
//   the program's TLS image copied): errno, newlib's reentrancy data and every thread_local of
//   Skia's and libstdc++'s are the worker's, not the main thread's. (libonyxposix itself still
//   takes the code of an app core for the main thread: pthread_self, the pthread keys -- nothing
//   Skia's raster uses.) No WTF and no WebCore code runs on a worker: Skia only.
//
// - malloc is newlib's, for every core: its lock is a compare-and-swap word, correct between cores
//   (an app core spins on it), and its _sbrk goes through libonyxposix's app-core RPC, turned on
//   here: the kernel call is made on core 0 (onyx_rpc_serve) -- by the main thread while it waits
//   for the jobs or for a lock, and by a thread kept for that, the server, which runs whenever the
//   main thread does not: asleep on something no wrapper below knows (the guard of a static
//   variable a worker is initialising: seen on the bench), or in a loop of Skia's (SkOnce) until
//   its time slice ends. Without the server, a worker that needs the kernel inside what the main
//   thread waits for would wait for the main thread: for ever. So does every file call made on a
//   worker (a font file read by FreeType), and a worker's abort (its message, its _exit).
//   libonyxposix routes newlib's stubs (_read, _write, _open, _lseek, _fstat, _close, _sbrk,
//   _gettimeofday, _exit); pread and pwrite are its own entries, straight to the kernel -- and
//   Skia's file stream reads with pread (FreeType loading a face or a glyph: the bench's core
//   test, "kapi slot 208"): the two are wrapped here and sent through the same RPC.
//     -Wl,--wrap=pread -Wl,--wrap=pwrite
//
// - Core 0 never sleeps on a lock while the workers run. A thread of core 0 that finds a lock
//   taken sleeps in the kernel (a futex) until the lock's owner wakes it -- but an app core cannot
//   make that call: its unlock is only seen by the kernel's 10 ms check of the sleeping words. And
//   the main thread asleep on the malloc lock while its owner, on an app core, waits for the main
//   thread to serve its _sbrk would never wake. So, while a batch runs, the lock entries wrapped
//   at link time (newlib's locks: malloc, stdio; Skia's semaphores; pthread mutexes) try, yield
//   and try again on core 0 -- and the main thread serves the RPC between two tries:
//     -Wl,--wrap=__retarget_lock_acquire -Wl,--wrap=__retarget_lock_acquire_recursive
//     -Wl,--wrap=sem_wait -Wl,--wrap=pthread_mutex_lock
//   Outside a batch (the workers asleep in wfe) the four are the real ones.
//
// - No other allocator. The program's operator new must be libstdc++'s (malloc): a program that
//   includes user/onyxpp.hpp gets operators over umm.h, a heap that calls kapi_sbrk itself -- the
//   first new of Skia's that grows it on an app core is a kernel call there, the job is stopped
//   (the Pi's "appcore: core 2: fault EC=0x15"), and the main thread waits for good on umm's lock.
//   build-web.sh compiles the window with -DONYX_HOSTED_NEW and checks the linked program.
//
// A worker that stops (see onyx_cores_run): the kernel ends a job that faults or makes a kernel
// call; the main thread, which asks the core's state at every turn of its wait, takes the job
// back, gives the cores back and rasterises alone from then on. A worker that died with a lock
// in its hand is another matter: nobody may free that lock for it (what it protected is half
// written). It is told -- malloc's lock: at once, by its owner; anything else (a mutex of Skia's,
// a guard, a once): by the server thread, which sees a batch that lost a worker finish no job for
// 2 s, or any batch finish none for 5 s -- and the process ends with status 70, for WebKit to
// start a new web process: not a frozen page.
// (Not covered: a lock left by a dead worker that only another thread of core 0 ever wants.)
//
// The bench (tools/tests/posixsim): a fake core is a host thread on which kapi__core () is the
// core's number, as on the Pi (run.sh's corereg.py), and every kapi call made there ends the
// program with the slot and the callers -- or, POSIXSIM_CORE_FAULT=1, stops the job as the Pi's
// kernel does. ONYX_CORES_TEST (the environment, the bench and the Pi alike) makes a worker fail
// at its 5th job: "call" (a kernel call), "lock" (the same with malloc's lock taken), "stall" (a
// loop for ever): what the main thread does of it is tests 4e of tools/webkit/test-webkit.sh.
//
// Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence (see fetch.sh).
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <semaphore.h>
#include <stdlib.h>
#include <string.h>
#include <sys/lock.h>
#include <malloc.h>
#include <unistd.h>
#include "posix_internal.h"

#define MAX_WORKERS	2
#define STACK_SIZE	(1024 * 1024)

#define STALL_NS	2000000000ULL		// a batch without a job finished for that long: a worker is stuck
#define STUCK_NS	5000000000ULL		// ... and the main thread did not take it back: it is stuck too
#define EXIT_LOCK_LOST	70			// the process's status when a dead worker held a lock

typedef void (*onyx_job_fn) (void *arg);

// newlib's locks as libonyxposix makes them (newlib_syscalls.c): the owner of a recursive one is
// the thread's id on core 0, minus the core on an app core.
struct __lock { volatile unsigned lk; volatile int owner; int count; };
extern struct __lock __lock___malloc_recursive_mutex;

void onyx_rpc_enable (int on);			// libonyxposix (newlib_syscalls.c)
void onyx_rpc_serve (void);
long onyx_rpc3 (long (*fn) (long, long, long), long a, long b, long c);

struct batch
{
	onyx_job_fn fn;
	onyx_job_fn reset;			// before a job is run a second time (0: nothing)
	void **args;
	int count;
	volatile int next;			// the next job to take
	volatile int done;			// jobs finished
};

struct worker
{
	int core;				// the app core (-1: none)
	struct __onyx_thread *tls;		// its TLS block
	unsigned char *stack;
	volatile int busy;			// it may be looking at s_batch
	volatile int job;			// the job it runs (-1: none): run again by the main thread if it faults
	volatile int stop;
	volatile unsigned jobsDone;
};

static struct worker s_w[MAX_WORKERS];
static int s_nw;				// workers started
static int s_users;
static struct batch *volatile s_batch;		// the batch in flight (0: none)
static volatile unsigned s_generation;		// counts the batches: what an idle worker waits on
static volatile int s_active;			// a batch runs: core 0's locks do not sleep
static volatile int s_failed;			// a worker faulted: no more batches, no more workers (for good)
static char s_why[160];				// what happened to it
static int s_test;				// ONYX_CORES_TEST: 1 call, 2 lock, 3 stall (0: none)
static pthread_t s_main;
static pthread_t s_server;			// the thread that serves the workers' kernel calls and watches the batch
static int s_hasServer;
static volatile unsigned s_serverGen;		// what it sleeps on between two batches
static volatile unsigned s_serveLock;
static void *server_main (void *arg);
static __thread int t_worker;			// 1 on a worker (its own TLS: see above)

// A side job: one job beside the batches, given by any thread of core 0 (the media player's video
// thread: a picture decoded and converted), which waits for it. A worker takes it before a batch's
// next job. While one is posted or runs, core 0's locks do not sleep (as during a batch).
enum { SIDE_FREE, SIDE_POSTED, SIDE_RUNNING, SIDE_DONE };
static struct { onyx_job_fn fn; void *arg; volatile int state; volatile int worker; } s_side;
static volatile unsigned s_sideLock;		// one poster at a time
static volatile int s_sideActive;
static volatile unsigned s_sideDone;		// side jobs run on a worker (the log)

static inline unsigned long long now_ns (void)
{
	struct timespec ts;
	clock_gettime (CLOCK_MONOTONIC, &ts);	// (libonyxposix reads the counter: no kernel call on an app core)
	return (unsigned long long) ts.tv_sec * 1000000000ULL + (unsigned long long) ts.tv_nsec;
}
unsigned long long onyx_cores_now_ns (void) { return now_ns (); }

// A worker's kernel call, if one waits, made here (by one thread of core 0 at a time).
static inline void serve (void)
{
	if (__atomic_exchange_n (&s_serveLock, 1, __ATOMIC_ACQUIRE))
		return;
	onyx_rpc_serve ();
	__atomic_store_n (&s_serveLock, 0, __ATOMIC_RELEASE);
}

static inline void wake_all (void)
{
#if defined(__aarch64__)
	__asm__ volatile ("dsb ish; sev" ::: "memory");
#endif
	kapi_wake_word (&s_generation);		// (the bench's workers sleep there; nobody does on the Pi)
}

// ONYX_CORES_TEST: the first worker fails, in the middle of a batch (see the top).
static void test_failure (void)
{
	int how = s_test;
	if (how == 3)
		for (;;) kapi__pause ();
	if (how == 2)
		__malloc_lock (_REENT);
	KT->yield ();				// (a kernel call on an app core: the kernel stops the job)
	for (;;) kapi__pause ();
}

// The side job, if one waits (a worker).
static void take_side (struct worker *w)
{
	int posted = SIDE_POSTED;
	if (__atomic_load_n (&s_side.state, __ATOMIC_SEQ_CST) != SIDE_POSTED
	    || !__atomic_compare_exchange_n (&s_side.state, &posted, SIDE_RUNNING, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
		return;
	__atomic_store_n (&s_side.worker, (int) (w - s_w), __ATOMIC_SEQ_CST);
	s_side.fn (s_side.arg);
	__atomic_store_n (&s_side.state, SIDE_DONE, __ATOMIC_SEQ_CST);
	__atomic_fetch_add (&s_sideDone, 1, __ATOMIC_RELAXED);
}

// Jobs taken one by one until none is left (the workers and the main thread).
static void take_jobs (struct batch *b, struct worker *w)
{
	for (;;)
	{
		if (w) take_side (w);			// (a picture to decode does not wait for the batch's end)
		int i = __atomic_fetch_add (&b->next, 1, __ATOMIC_SEQ_CST);
		if (i >= b->count)
			break;
		if (w) __atomic_store_n (&w->job, i, __ATOMIC_SEQ_CST);
		else serve ();			// (the main thread, between two jobs of its own)
		if (w && s_test && w == &s_w[0] && w->jobsDone == 4)
			test_failure ();
		b->fn (b->args[i]);
		// (counted, then no longer the worker's: stopped between the two, its job is only done twice)
		__atomic_fetch_add (&b->done, 1, __ATOMIC_SEQ_CST);
		if (w) { __atomic_store_n (&w->job, -1, __ATOMIC_SEQ_CST); w->jobsDone++; }
	}
}

static void worker_main (void *arg)		// on the app core
{
	struct worker *w = (struct worker *) arg;
#if defined(__aarch64__)
	__asm__ volatile ("msr tpidr_el0, %0" :: "r" (__onyx_thread_tp (w->tls)) : "memory");
#endif
	t_worker = 1;
	unsigned seen = __atomic_load_n (&s_generation, __ATOMIC_SEQ_CST);
	while (!__atomic_load_n (&w->stop, __ATOMIC_SEQ_CST))
	{
		// busy is set before the batch is looked at and cleared after: the main thread, which takes
		// the batch away and then waits for busy to be 0, knows nobody still reads it
		__atomic_store_n (&w->busy, 1, __ATOMIC_SEQ_CST);
		take_side (w);
		struct batch *b = __atomic_load_n (&s_batch, __ATOMIC_SEQ_CST);
		if (b)
			take_jobs (b, w);
		__atomic_store_n (&w->busy, 0, __ATOMIC_SEQ_CST);

		// asleep until the next batch (the main thread's sev; a wfe also ends by itself now and then)
		for (;;)
		{
			unsigned g = __atomic_load_n (&s_generation, __ATOMIC_SEQ_CST);
			if (g != seen || __atomic_load_n (&w->stop, __ATOMIC_SEQ_CST)) { seen = g; break; }
			if (__atomic_load_n (&s_side.state, __ATOMIC_SEQ_CST) == SIDE_POSTED) break;
			if (kapi__core () != 0)
				__asm__ volatile ("wfe" ::: "memory");
			else
				kapi_wait_word (&s_generation, g, 100);		// (the bench: a host thread may sleep)
		}
	}
}

int onyx_cores_count (void) { return s_failed ? 0 : s_nw; }

// Up to `wanted` app cores taken and started (the cores the kernel has free: 2 and 3 when no
// emulator and no network core holds them) -> how many. Counted: each start has its stop. (The
// kernel fills the program's heap when a core is acquired, and keeps it filled: a job's malloc
// takes no page fault.)
int onyx_cores_start (int wanted)
{
	if (s_users++ > 0)
		return onyx_cores_count ();
	s_main = pthread_self ();
	s_nw = 0;
	if (s_failed)				// (a worker faulted once: this process rasterises alone)
		return 0;
	const char *t = getenv ("ONYX_CORES_TEST");
	s_test = t == 0 ? 0 : !strcmp (t, "call") ? 1 : !strcmp (t, "lock") ? 2 : !strcmp (t, "stall") ? 3 : 0;
	if (wanted > MAX_WORKERS) wanted = MAX_WORKERS;
	for (int i = 0; i < wanted; i++)
	{
		struct worker *w = &s_w[s_nw];
		// (its stack and its TLS block are made once, and used again at the next start)
		w->busy = 0; w->stop = 0; w->job = -1; w->jobsDone = 0;
		w->core = kapi_core_acquire ();
		if (w->core < 0)
			break;
		if (w->tls == 0) w->tls = __onyx_thread_alloc ();
		if (w->stack == 0) w->stack = (unsigned char *) memalign (65536, STACK_SIZE);
		if (w->tls == 0 || w->stack == 0)
		{
			kapi_core_release (w->core);
			break;
		}
		w->tls->tid = 1000 + w->core;			// (what a recursive lock takes for its owner on the bench)
		memset (w->stack, 0, STACK_SIZE);		// (its pages are there before the core runs on them)
		if (kapi_core_run (w->core, worker_main, w, w->stack + STACK_SIZE) != 0)
		{
			kapi_core_release (w->core);
			break;
		}
		s_nw++;
	}
	if (s_nw > 0)
	{
		onyx_rpc_enable (1);
		// (the server thread: made once, asleep between the batches)
		if (!s_hasServer && pthread_create (&s_server, 0, server_main, 0) == 0)
			s_hasServer = 1;
	}
	return s_nw;
}

static void stop_workers (void)
{
	for (int i = 0; i < s_nw; i++)
		__atomic_store_n (&s_w[i].stop, 1, __ATOMIC_SEQ_CST);
	__atomic_fetch_add (&s_generation, 1, __ATOMIC_SEQ_CST);
	wake_all ();
	unsigned long long end = now_ns () + 500000000ULL;
	for (int i = 0; i < s_nw; i++)
	{
		if (s_w[i].core < 0)			// (it faulted: given back already)
			continue;
		while (kapi_core_state (s_w[i].core) == KAPI_CORE_RUNNING && now_ns () < end)
		{
			wake_all ();
			serve ();
			sched_yield ();
		}
		kapi_core_release (s_w[i].core);
		s_w[i].core = -1;
		// (the stack and the TLS block are kept for the next start)
	}
	s_nw = 0;
	onyx_rpc_enable (0);
}

void onyx_cores_stop (void)
{
	if (s_users == 0 || --s_users > 0)
		return;
	stop_workers ();
}

static char *add_str (char *p, const char *s) { while (*s) *p++ = *s++; *p = 0; return p; }
static char *add_int (char *p, int v)
{
	char d[12]; int n = 0;
	if (v < 0) { *p++ = '-'; v = -v; }
	do { d[n++] = (char) ('0' + v % 10); v /= 10; } while (v);
	while (n > 0) *p++ = d[--n];
	*p = 0;
	return p;
}

// What happened to the worker that failed (0: none did). The text is kept: one line of the log.
const char *onyx_cores_failure (void) { return s_failed ? s_why : 0; }

// A dead worker held a lock: said, and the process ends (see the top).
static void lock_lost (const char *what)
{
	char line[300], *p = line;
	p = add_str (p, "web: gpu: an app core faulted while it rasterised (");
	p = add_str (p, s_why[0] ? s_why : "a worker stopped");
	p = add_str (p, ") and it held ");
	p = add_str (p, what);
	p = add_str (p, ", which nobody can free for it: this process ends\n");
	write (2, line, (size_t) (p - line));
	_exit (EXIT_LOCK_LOST);
}

// The same end when no worker faulted and yet nothing moves: the main thread is not in its wait
// (which would have taken a stuck worker's job back after 2 s).
static void batch_stuck (void)
{
	static const char line[] = "web: gpu: the app cores' raster is stuck (no job finished for 5 s, no core faulted, "
		"the main thread waits for something): this process ends\n";
	write (2, line, sizeof line - 1);
	_exit (EXIT_LOCK_LOST);
}

// The server thread, during a batch: the workers' kernel calls, and the batch watched (see the top).
static void watch_batch (void)
{
	static unsigned long long lastLook, since;
	static unsigned batch;
	static int done, wasLost;

	unsigned long long now = now_ns ();
	if (now - lastLook < 5000000ULL)
		return;
	lastLook = now;
	struct batch *b = __atomic_load_n (&s_batch, __ATOMIC_SEQ_CST);
	if (b == 0)
		return;
	unsigned g = __atomic_load_n (&s_generation, __ATOMIC_SEQ_CST);	// (which batch)
	int lost = s_failed;
	for (int i = 0; i < s_nw; i++)
	{
		int core = s_w[i].core;
		if (core > 0 && kapi_core_state (core) == KAPI_CORE_FAULT)
		{
			lost = 1;
			if (!s_why[0])
			{
				char *p = add_str (s_why, "core ");
				p = add_int (p, core);
				add_str (p, ": its job faulted, see the kernel's log");
			}
			// (malloc's lock is its own: no need to wait to know)
			if (__lock___malloc_recursive_mutex.count > 0 && __lock___malloc_recursive_mutex.owner == -core)
				lock_lost ("malloc's lock");
		}
	}
	int d = __atomic_load_n (&b->done, __ATOMIC_SEQ_CST);
	if (g != batch || d != done || lost != wasLost)
	{
		// (a worker just lost: the main thread is given the 2 s to take its job back)
		batch = g; done = d; wasLost = lost; since = now;
		return;
	}
	if (lost && now - since > STALL_NS)
		lock_lost ("something the main thread waits for");
	if (now - since > STUCK_NS)
		batch_stuck ();
}

static void *server_main (void *arg)
{
	(void) arg;
	for (;;)
	{
		unsigned g = __atomic_load_n (&s_serverGen, __ATOMIC_SEQ_CST);
		if (!__atomic_load_n (&s_active, __ATOMIC_SEQ_CST))
		{
			kapi_wait_word (&s_serverGen, g, KAPI_WAIT_FOREVER);
			continue;
		}
		serve ();
		watch_batch ();
		sched_yield ();
	}
	return 0;
}

// The worker has stopped (its job faulted) or is stuck: its core is given back -- which stops it if
// it still runs -- and the job it had is done again here.
static void retire_worker (struct batch *b, struct worker *w, int stalled)
{
	int core = w->core;
	kapi_core_release (core);
	w->core = -1;
	int job = __atomic_load_n (&w->job, __ATOMIC_SEQ_CST);
	__atomic_store_n (&w->job, -1, __ATOMIC_SEQ_CST);
	__atomic_store_n (&w->busy, 0, __ATOMIC_SEQ_CST);
	if (!s_failed)
	{
		char *p = s_why;
		p = add_str (p, "core ");
		p = add_int (p, core);
		p = add_str (p, stalled ? ": no job finished for 2 s, stopped" : ": its job faulted, see the kernel's log");
		p = add_str (p, "; job ");
		p = add_int (p, job);
		p = add_str (p, " of ");
		p = add_int (p, b->count);
	}
	s_failed = 1;
	if (__lock___malloc_recursive_mutex.count > 0 && __lock___malloc_recursive_mutex.owner == -core)
		lock_lost ("malloc's lock");
	if (job >= 0 && job < b->count)
	{
		if (b->reset)
			b->reset (b->args[job]);
		b->fn (b->args[job]);
		__atomic_fetch_add (&b->done, 1, __ATOMIC_SEQ_CST);
	}
}

// The workers that faulted (or, stalled: all that still hold a job) are retired -> how many.
static int check_workers (struct batch *b, int stalled)
{
	int n = 0;
	for (int i = 0; i < s_nw; i++)
	{
		struct worker *w = &s_w[i];
		if (w->core < 0)
			continue;
		int faulted = kapi_core_state (w->core) == KAPI_CORE_FAULT;
		if (!faulted && !(stalled && __atomic_load_n (&w->job, __ATOMIC_SEQ_CST) >= 0))
			continue;
		retire_worker (b, w, !faulted);
		n++;
	}
	return n;
}

// fn (args[i]) for every i < count, on the workers and -- mainToo -- on the calling (main) thread;
// back when all are done. -> 0, or -1: a worker stopped (it faulted, or no job was finished for
// 2 s). Its job was run again here, after reset (args[i]) (0: none) had put the job's output back
// as it was given; the cores are given back, onyx_cores_count () is 0 for the rest of the
// process's life and onyx_cores_failure () says what happened.
int onyx_cores_run (onyx_job_fn fn, onyx_job_fn reset, void **args, int count, int mainToo)
{
	if (count <= 0)
		return 0;
	struct batch b = { fn, reset, args, count, 0, 0 };
	if (s_nw == 0 || s_failed)
	{
		take_jobs (&b, 0);
		return 0;
	}

	__atomic_store_n (&s_batch, &b, __ATOMIC_SEQ_CST);
	__atomic_store_n (&s_active, 1, __ATOMIC_SEQ_CST);
	__atomic_fetch_add (&s_generation, 1, __ATOMIC_SEQ_CST);
	wake_all ();
	if (s_hasServer)
	{
		__atomic_fetch_add (&s_serverGen, 1, __ATOMIC_SEQ_CST);
		kapi_wake_word (&s_serverGen);
	}

	if (mainToo)
		take_jobs (&b, 0);

	// the main thread waits: it serves the workers' kernel calls, lets the other threads of core 0
	// run, and asks at every turn whether a worker has stopped
	int lost = 0;
	int seen = __atomic_load_n (&b.done, __ATOMIC_SEQ_CST);
	unsigned long long since = now_ns ();
	while (seen < count)
	{
		serve ();
		unsigned long long now = now_ns ();
		int n = check_workers (&b, now - since > STALL_NS);
		if (n)
		{
			lost += n;
			take_jobs (&b, 0);		// (what is left: here, with whoever still works)
		}
		int done = __atomic_load_n (&b.done, __ATOMIC_SEQ_CST);
		if (done != seen || n) { seen = done; since = now_ns (); }
		else sched_yield ();
	}

	// the batch is taken away; then nobody may still be looking at it
	__atomic_store_n (&s_batch, 0, __ATOMIC_SEQ_CST);
	since = now_ns ();
	for (int i = 0; i < s_nw; i++)
		while (s_w[i].core >= 0 && __atomic_load_n (&s_w[i].busy, __ATOMIC_SEQ_CST))
		{
			serve ();
			if (kapi_core_state (s_w[i].core) == KAPI_CORE_FAULT || now_ns () - since > STALL_NS)
			{
				retire_worker (&b, &s_w[i], kapi_core_state (s_w[i].core) != KAPI_CORE_FAULT);
				lost++;
				break;
			}
			sched_yield ();
		}
	__atomic_store_n (&s_active, 0, __ATOMIC_SEQ_CST);

	if (lost || s_failed)
	{
		s_failed = 1;
		stop_workers ();
		return -1;
	}
	return 0;
}

// fn (arg) run on a worker, the caller (a thread of core 0, any) waiting for it -> 0 done; -1 not
// run: no worker, another side job is there, or none took it in 30 ms (the workers are in long jobs
// of a batch) -- the caller runs it itself; -2: the worker stopped in it (it faulted, or 2 s passed):
// what fn was writing is in an unknown state, the cores are given up for the rest of the process's
// life (as after a batch's failure). The caller serves the workers' kernel calls while it waits.
int onyx_cores_offload (onyx_job_fn fn, void *arg)
{
	if (s_nw == 0 || s_failed || t_worker)
		return -1;
	if (__atomic_exchange_n (&s_sideLock, 1, __ATOMIC_ACQUIRE))
		return -1;
	s_side.fn = fn;
	s_side.arg = arg;
	__atomic_store_n (&s_sideActive, 1, __ATOMIC_SEQ_CST);
	__atomic_store_n (&s_side.state, SIDE_POSTED, __ATOMIC_SEQ_CST);
	__atomic_fetch_add (&s_generation, 1, __ATOMIC_SEQ_CST);
	wake_all ();

	int r = 0;
	unsigned long long start = now_ns ();
	for (unsigned turn = 0; ; turn++)
	{
		int st = __atomic_load_n (&s_side.state, __ATOMIC_SEQ_CST);
		if (st == SIDE_DONE)
			break;
		serve ();
		unsigned long long waited = now_ns () - start;
		if (st == SIDE_POSTED && (waited > 30000000ULL || s_nw == 0 || s_failed))
		{
			// nobody took it: taken back (unless a worker takes it just now)
			int posted = SIDE_POSTED;
			if (__atomic_compare_exchange_n (&s_side.state, &posted, SIDE_FREE, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
			{
				r = -1;
				break;
			}
			continue;
		}
		if (st == SIDE_RUNNING)
		{
			struct worker *w = &s_w[__atomic_load_n (&s_side.worker, __ATOMIC_SEQ_CST)];
			int core = w->core;
			int faulted = core > 0 && kapi_core_state (core) == KAPI_CORE_FAULT;
			if (faulted || core < 0 || waited > STALL_NS)
			{
				if (!s_failed)
				{
					char *p = add_str (s_why, "core ");
					p = add_int (p, core);
					add_str (p, faulted ? ": a side job (a picture's decoding) faulted, see the kernel's log" : ": a side job did not end in 2 s, stopped");
				}
				s_failed = 1;
				if (core > 0)
				{
					if (__lock___malloc_recursive_mutex.count > 0 && __lock___malloc_recursive_mutex.owner == -core)
						lock_lost ("malloc's lock");
					kapi_core_release (core);	// (which stops it if it still runs)
					w->core = -1;
					__atomic_store_n (&w->busy, 0, __ATOMIC_SEQ_CST);
				}
				r = -2;
				break;
			}
		}
		// the first two milliseconds turn by turn with core 0's other threads, then asleep in steps
		if (waited < 2000000ULL) sched_yield ();
		else usleep (500);
	}
	__atomic_store_n (&s_side.state, SIDE_FREE, __ATOMIC_SEQ_CST);
	__atomic_store_n (&s_sideActive, 0, __ATOMIC_SEQ_CST);
	__atomic_store_n (&s_sideLock, 0, __ATOMIC_RELEASE);
	return r;
}

unsigned onyx_cores_side_jobs_done (void) { return __atomic_load_n (&s_sideDone, __ATOMIC_RELAXED); }

// How many jobs each worker has run since it started (the log).
unsigned onyx_cores_jobs_done (int worker) { return worker >= 0 && worker < MAX_WORKERS ? s_w[worker].jobsDone : 0; }

// ---- the locks of core 0 while a batch runs (see the top) ---------------------------------------------

struct __lock;
void __real___retarget_lock_acquire (_LOCK_T lock);
void __real___retarget_lock_acquire_recursive (_LOCK_T lock);
int __retarget_lock_try_acquire (_LOCK_T lock);
int __retarget_lock_try_acquire_recursive (_LOCK_T lock);
int __real_sem_wait (sem_t *s);
int __real_pthread_mutex_lock (pthread_mutex_t *m);

// 1: this entry must not sleep (a batch runs, and this is a thread of core 0, not a worker)
static inline int must_spin (void)
{
	return (__atomic_load_n (&s_active, __ATOMIC_RELAXED) || __atomic_load_n (&s_sideActive, __ATOMIC_RELAXED)) && !t_worker;
}

// One turn of a wait for a lock on core 0 during a batch: the main thread serves the workers'
// kernel calls (the lock's owner may be waiting for one).
static inline void spin_turn (void)
{
	// (during a side job any thread serves: the server thread is not woken for those)
	if (pthread_equal (pthread_self (), s_main) || __atomic_load_n (&s_sideActive, __ATOMIC_RELAXED))
		serve ();
	sched_yield ();
}

void __wrap___retarget_lock_acquire (_LOCK_T lock)
{
	if (!must_spin ()) { __real___retarget_lock_acquire (lock); return; }
	while (!__retarget_lock_try_acquire (lock))
	{
		if (!must_spin ()) { __real___retarget_lock_acquire (lock); return; }
		spin_turn ();
	}
}

void __wrap___retarget_lock_acquire_recursive (_LOCK_T lock)
{
	if (!must_spin ()) { __real___retarget_lock_acquire_recursive (lock); return; }
	while (!__retarget_lock_try_acquire_recursive (lock))
	{
		if (!must_spin ()) { __real___retarget_lock_acquire_recursive (lock); return; }
		spin_turn ();
	}
}

int __wrap_sem_wait (sem_t *s)
{
	if (!must_spin ())
		return __real_sem_wait (s);
	int e = errno;
	while (sem_trywait (s) != 0)
	{
		if (errno != EAGAIN)
			return -1;
		if (!must_spin ()) { errno = e; return __real_sem_wait (s); }
		spin_turn ();
	}
	errno = e;
	return 0;
}

int __wrap_pthread_mutex_lock (pthread_mutex_t *m)
{
	if (!must_spin ())
		return __real_pthread_mutex_lock (m);
	for (;;)
	{
		int r = pthread_mutex_trylock (m);
		if (r != EBUSY)
			return r;
		// (an error-checking mutex locked twice by its owner, and the like: the real one says)
		if (!must_spin () || m->__type == PTHREAD_MUTEX_ERRORCHECK)
			return __real_pthread_mutex_lock (m);
		spin_turn ();
	}
}

// ---- pread / pwrite on a worker: through the app-core RPC (see the top) -------------------------------

ssize_t __real_pread (int fd, void *buf, size_t n, off_t off);
ssize_t __real_pwrite (int fd, const void *buf, size_t n, off_t off);

struct pio { int fd; void *buf; size_t n; off_t off; };

static long rpc_pread (long p, long b, long c)
{
	(void) b; (void) c;
	struct pio *io = (struct pio *) p;
	return __real_pread (io->fd, io->buf, io->n, io->off);
}

static long rpc_pwrite (long p, long b, long c)
{
	(void) b; (void) c;
	struct pio *io = (struct pio *) p;
	return __real_pwrite (io->fd, io->buf, io->n, io->off);
}

ssize_t __wrap_pread (int fd, void *buf, size_t n, off_t off)
{
	if (!t_worker)
		return __real_pread (fd, buf, n, off);
	struct pio io = { fd, buf, n, off };
	return onyx_rpc3 (rpc_pread, (long) &io, 0, 0);
}

ssize_t __wrap_pwrite (int fd, const void *buf, size_t n, off_t off)
{
	if (!t_worker)
		return __real_pwrite (fd, buf, n, off);
	struct pio io = { fd, (void *) buf, n, off };
	return onyx_rpc3 (rpc_pwrite, (long) &io, 0, 0);
}
