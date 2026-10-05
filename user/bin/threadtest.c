//
// threadtest -- exercises the threads of kapi v67: create / join / exit codes, a mutex
// under preemption, the allocator (umm) from several threads, a manual-reset event,
// a barrier, timeouts, the per-process limit, and kapi_post / kapi_pump_wait (a worker
// that blocks while the main thread keeps pumping, its results run on the main thread).
// Prints one line per check and PASS / FAIL. It ends with a thread still running: the
// process must end with its main thread anyway (the prompt comes back).
//
#include "appkit/appkit.h"
#include "applib.h"
#include "umm.h"

static int s_fail;

static void put_num (long v)
{
	char b[24]; int n = 0;
	if (v < 0) { kapi_stdout_write ("-", 1); v = -v; }
	do { b[n++] = (char) ('0' + v % 10); v /= 10; } while (v != 0);
	while (n > 0) kapi_stdout_write (&b[--n], 1);
}

static void check (const char *what, int ok, long got)
{
	ax_puts (ok ? "  ok    " : "  FAIL  ");
	ax_puts (what);
	ax_puts (" (");
	put_num (got);
	ax_putln (")");
	if (!ok) s_fail++;
}

// ---- 1. a mutex under preemption ------------------------------------------------

#define COUNT_THREADS	4
#define COUNT_ITERS	200000

static int s_mutex;
static volatile long s_counter;

static int counter_thread (void *arg)
{
	for (int i = 0; i < COUNT_ITERS; i++)
	{
		kapi_mutex_lock (s_mutex, KAPI_WAIT_FOREVER);
		long v = s_counter;		// read-modify-write: torn without the mutex
		for (volatile int d = 0; d < 3; d++) { }
		s_counter = v + 1;
		kapi_mutex_unlock (s_mutex);
	}
	return 100 + (int) (long) arg;		// its exit code
}

// ---- 2. the allocator from several threads ---------------------------------------

static int alloc_thread (void *arg)
{
	int bad = 0;
	void *p[64];
	for (int round = 0; round < 300; round++)
	{
		for (int i = 0; i < 64; i++)
		{
			unsigned long sz = (unsigned long) ((i * 37 + round) % 3000) + 2;	// (>= 2: b[0] != b[sz - 1])
			p[i] = umm_malloc (sz);
			if (p[i] == 0) { bad++; continue; }
			unsigned char *b = p[i];
			b[0] = (unsigned char) (long) arg; b[sz - 1] = (unsigned char) i;
		}
		for (int i = 0; i < 64; i++)
		{
			if (p[i] == 0) continue;
			unsigned long sz = (unsigned long) ((i * 37 + round) % 3000) + 2;
			unsigned char *b = p[i];
			if (b[0] != (unsigned char) (long) arg || b[sz - 1] != (unsigned char) i) bad++;
			umm_free (p[i]);
		}
	}
	return bad;
}

// ---- 3. a manual-reset event ---------------------------------------------------

static int s_go;
static volatile int s_woken;

static int waiter_thread (void *arg)
{
	(void) arg;
	if (kapi_event_wait (s_go, 2000) == 0) s_woken++;
	return 0;
}

// ---- 4. a barrier ---------------------------------------------------------------

#define BAR_THREADS	4
#define BAR_ROUNDS	5
static int s_bar;
static volatile int s_round[BAR_THREADS];
static volatile int s_barBad, s_serial;

static int barrier_thread (void *arg)
{
	int me = (int) (long) arg;
	for (int r = 0; r < BAR_ROUNDS; r++)
	{
		s_round[me] = r;
		kapi_msleep ((unsigned) (me * 3));	// arrive at different times
		if (kapi_barrier_wait (s_bar) == 1) s_serial++;
		for (int i = 0; i < BAR_THREADS; i++)	// everyone reached round r
			if (s_round[i] < r) s_barBad++;
	}
	return 0;
}

// ---- 5. a timeout ---------------------------------------------------------------

static int trylock_thread (void *arg)
{
	(void) arg;
	return kapi_mutex_lock (s_mutex, 50);	// the main thread holds it: -1
}

// ---- 6. post + pump: a worker that blocks, the main thread keeps pumping ---------

#define WORK_ITEMS	8
static volatile int s_results, s_wrongThread;
static volatile long s_sum;

static void on_result (void *ctx, long value)	// run by the pump, on the main thread
{
	(void) ctx;
	if (kapi_thread_self () != 1) s_wrongThread++;
	s_sum += value;
	s_results++;
}

static int worker_thread (void *arg)
{
	(void) arg;
	for (int i = 1; i <= WORK_ITEMS; i++)
	{
		kapi_msleep (40);			// "waiting for the network"
		kapi_post (on_result, 0, i * 10);
	}
	return 0;
}

// ---- 7. the per-process limit ----------------------------------------------------

static int s_hold;

static int idle_thread (void *arg)
{
	(void) arg;
	kapi_event_wait (s_hold, KAPI_WAIT_FOREVER);
	return 7;
}

// ---- 8. left running when main returns ------------------------------------------

static int forever_thread (void *arg)
{
	(void) arg;
	for (;;) kapi_msleep (100);
	return 0;
}

// ---- `threadtest spin`: threads busy for ever, to be killed (`threadtest bg` starts it
// detached, then `ps` / `kill <pid> --force`: every thread must end, the system stay up) ----

static int spin_mutex_thread (void *arg)
{
	(void) arg;
	for (;;)
	{
		kapi_mutex_lock (s_mutex, KAPI_WAIT_FOREVER);
		for (volatile int d = 0; d < 20000; d++) { }	// held while preempted
		kapi_mutex_unlock (s_mutex);
		void *p = umm_malloc (100);			// the allocator's lock too
		umm_free (p);
	}
	return 0;
}

static int spin (void)
{
	s_mutex = kapi_mutex_create ();
	s_hold = kapi_event_create (1, 0);
	for (int i = 0; i < 3; i++) kapi_thread_create (spin_mutex_thread, 0, 0, "spin");
	for (int i = 0; i < 2; i++) kapi_thread_create (idle_thread, 0, 0, "wait");	// blocked on s_hold
	kapi_thread_create (forever_thread, 0, 0, "sleep");
	for (;;) kapi_pump_wait (100);				// the main thread: in pump_wait
	return 0;
}

int main (void)
{
	char args[32];
	int na = kapi_get_args (args, sizeof args);
	if (na > 0 && args[0] == 's') return spin ();
	if (na > 0 && args[0] == 'b')
	{
		int r = kapi_exec ("SD:/bin/threadtest", "spin");
		ax_puts ("threadtest: `threadtest spin` started detached (");
		put_num (r);
		ax_putln (") -- ps, then kill <pid> --force");
		return 0;
	}

	ax_putln ("threadtest: kapi v67 threads");
	if (kapi_abi_version () < 67)
	{
		ax_putln ("  this kernel has no threads (ABI < 67)");
		return 1;
	}
	check ("main thread is tid 1", kapi_thread_self () == 1, kapi_thread_self ());

	// 1
	s_mutex = kapi_mutex_create ();
	int tid[COUNT_THREADS];
	for (int i = 0; i < COUNT_THREADS; i++)
		tid[i] = kapi_thread_create (counter_thread, (void *) (long) i, 0, "count");
	int codes = 1;
	for (int i = 0; i < COUNT_THREADS; i++)
	{
		int code = -1;
		if (kapi_thread_join (tid[i], KAPI_WAIT_FOREVER, &code) != 0 || code != 100 + i) codes = 0;
	}
	check ("mutex: shared counter", s_counter == (long) COUNT_THREADS * COUNT_ITERS, s_counter);
	check ("join: exit codes", codes, codes);
	check ("join: a joined thread is gone", kapi_thread_join (tid[0], 0, 0) == -2, kapi_thread_join (tid[0], 0, 0));

	// 2
	for (int i = 0; i < COUNT_THREADS; i++)
		tid[i] = kapi_thread_create (alloc_thread, (void *) (long) (i + 1), 0, "alloc");
	int bad = 0;
	for (int i = 0; i < COUNT_THREADS; i++)
	{
		int code = 1;
		kapi_thread_join (tid[i], KAPI_WAIT_FOREVER, &code);
		bad += code;
	}
	check ("umm from 4 threads", bad == 0, bad);

	// 3
	s_go = kapi_event_create (1, 0);
	for (int i = 0; i < COUNT_THREADS; i++) tid[i] = kapi_thread_create (waiter_thread, 0, 0, "wait");
	kapi_msleep (50);
	check ("event: nobody woken before set", s_woken == 0, s_woken);
	kapi_event_set (s_go);
	for (int i = 0; i < COUNT_THREADS; i++) kapi_thread_join (tid[i], KAPI_WAIT_FOREVER, 0);
	check ("event: manual reset wakes all", s_woken == COUNT_THREADS, s_woken);
	int ev2 = kapi_event_create (0, 1);
	int a = kapi_event_wait (ev2, 0), b = kapi_event_wait (ev2, 0);
	check ("event: auto reset taken once", a == 0 && b == -1, b);

	// 4
	s_bar = kapi_barrier_create (BAR_THREADS);
	for (int i = 0; i < BAR_THREADS; i++)
		tid[i] = kapi_thread_create (barrier_thread, (void *) (long) i, 0, "bar");
	for (int i = 0; i < BAR_THREADS; i++) kapi_thread_join (tid[i], KAPI_WAIT_FOREVER, 0);
	check ("barrier: rounds kept together", s_barBad == 0, s_barBad);
	check ("barrier: one last-in per round", s_serial == BAR_ROUNDS, s_serial);

	// 5
	kapi_mutex_lock (s_mutex, KAPI_WAIT_FOREVER);
	int t = kapi_thread_create (trylock_thread, 0, 0, "try");
	int code = 0;
	kapi_thread_join (t, KAPI_WAIT_FOREVER, &code);
	kapi_mutex_unlock (s_mutex);
	check ("mutex: lock timeout", code == -1, code);
	check ("join: timeout", kapi_thread_join (kapi_thread_create (forever_thread, 0, 0, "slow"), 30, 0) == -1, 0);

	// 6
	unsigned pumps = 0, t0 = kapi_get_ticks ();
	t = kapi_thread_create (worker_thread, 0, 0, "worker");
	while (s_results < WORK_ITEMS && kapi_get_ticks () - t0 < 300)
	{
		kapi_pump_wait (1000);			// woken by each post
		pumps++;
	}
	kapi_thread_join (t, KAPI_WAIT_FOREVER, 0);
	check ("post: every result delivered", s_results == WORK_ITEMS && s_sum == 360, s_sum);
	check ("post: run on the main thread", s_wrongThread == 0, s_wrongThread);
	check ("pump_wait: woken per post", pumps >= WORK_ITEMS && pumps <= WORK_ITEMS + 3, (long) pumps);

	// 7 (one "slow" thread from 5 still runs)
	s_hold = kapi_event_create (1, 0);
	int n = 0, ids[40];
	for (; n < 40; n++)
	{
		ids[n] = kapi_thread_create (idle_thread, 0, 16384, 0);
		if (ids[n] < 0) break;
	}
	check ("limit: 32 threads per process", n == 31 && ids[n] == -2, n);
	kapi_event_set (s_hold);
	int all7 = 1;
	for (int i = 0; i < n; i++) { int c = 0; if (kapi_thread_join (ids[i], KAPI_WAIT_FOREVER, &c) != 0 || c != 7) all7 = 0; }
	check ("limit: all joined", all7, all7);

	// 8
	kapi_thread_create (forever_thread, 0, 0, "forever");
	ax_putln (s_fail == 0 ? "threadtest: PASS" : "threadtest: FAIL");
	return s_fail;				// (the threads left end with the process)
}
