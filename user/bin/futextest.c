//
// futextest -- exercises the word waits of kapi v68 (kapi_wait_word / kapi_wake_word) and
// kapi_thread_priority: the immediate returns, a timeout, a thread woken by wake_word, the
// same word seen through two mappings of a shared surface (the waiters are keyed by the
// physical address), a word changed by an APP CORE without any wake (the kernel's tick
// notices it within ~10 ms), bad addresses, and a thread made "real time". Prints one line
// per check and PASS / FAIL.
//
#include "kapi.h"
#include "applib.h"

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

static volatile unsigned s_word;

// ---- a thread that sleeps on a word ------------------------------------------------

struct waiter
{
	volatile unsigned *word;
	unsigned	   expected;
	volatile int	   result;
	volatile unsigned  woke_us;
};

static int waiter_thread (void *arg)
{
	struct waiter *w = (struct waiter *) arg;
	w->result = kapi_wait_word (w->word, w->expected, 3000);
	w->woke_us = kapi_clock_us ();
	return 0;
}

// ---- an app core that changes a word (no kapi call there) --------------------------

static unsigned char s_Stack[16 * 1024] __attribute__ ((aligned (16)));
static volatile unsigned s_core_word;
static volatile unsigned s_core_wrote_us;

static void core_job (void *arg)
{
	(void) arg;
	unsigned t0 = kapi_clock_us ();
	while (kapi_clock_us () - t0 < 200000) { }	// 200 ms, then the word changes
	s_core_wrote_us = kapi_clock_us ();
	__asm__ volatile ("dmb ish" ::: "memory");
	s_core_word = 1;
}

// ---- a busy "real time" thread must not freeze the others ---------------------------

static volatile int s_spin_stop;
static volatile unsigned s_spins;
static int spinner (void *arg)
{
	(void) arg;
	while (!s_spin_stop) s_spins++;			// never sleeps: its priority is spent
	return 0;
}

int main (void)
{
	if (KT->version < 68) { ax_putln ("futextest: the kernel is older than v68"); return 1; }
	ax_putln ("futextest: kapi v68 word waits");

	// 1. the immediate returns
	s_word = 5;
	int r = kapi_wait_word (&s_word, 4, 1000);
	check ("value differs -> 0 at once", r == 0, r);
	r = kapi_wait_word (&s_word, 5, 0);
	check ("timeout 0, equal -> 1", r == 1, r);

	// 2. a timeout
	unsigned t0 = kapi_clock_us ();
	r = kapi_wait_word (&s_word, 5, 50);
	unsigned dt = kapi_clock_us () - t0;
	check ("50 ms timeout -> 1", r == 1, r);
	check ("... after >= 45 ms (us)", dt >= 45000 && dt < 500000, (long) dt);

	// 3. a thread woken by wake_word
	struct waiter w = { &s_word, 5, 99, 0 };
	int tid = kapi_thread_create (waiter_thread, &w, 0, "waiter");
	kapi_msleep (30);
	check ("the thread sleeps", w.result == 99, w.result);
	s_word = 6;
	r = kapi_wake_word (&s_word);
	int code;
	kapi_thread_join (tid, 2000, &code);
	check ("wake_word woke 1", r == 1, r);
	check ("the waiter got 0", w.result == 0, w.result);

	// 4. the same word through two mappings of one surface (a cross-process word)
	int sid = kapi_surface_create (16, 1);
	unsigned *a = sid > 0 ? kapi_surface_map (sid) : 0;
	unsigned *b = sid > 0 ? kapi_surface_map (sid) : 0;
	if (a != 0 && b != 0 && a != b)
	{
		a[0] = 7;
		struct waiter w2 = { a, 7, 99, 0 };
		tid = kapi_thread_create (waiter_thread, &w2, 0, "surface");
		kapi_msleep (30);
		b[0] = 8;					// the other mapping
		r = kapi_wake_word (b);
		kapi_thread_join (tid, 2000, &code);
		check ("woken through the other mapping", r == 1 && w2.result == 0, w2.result);
		kapi_surface_destroy (sid);
	}
	else
	{
		check ("surface mapped twice", 0, sid);
	}

	// 5. a word changed by an app core, never woken: the tick notices it
	int core = kapi_core_acquire ();
	if (core >= 0)
	{
		s_core_word = 0;
		kapi_core_run (core, core_job, 0, s_Stack + sizeof s_Stack);
		r = kapi_wait_word (&s_core_word, 0, 2000);
		unsigned woke = kapi_clock_us ();
		check ("app core's write wakes the waiter", r == 0 && s_core_word == 1, r);
		long lat = (long) (woke - s_core_wrote_us);
		check ("... within 30 ms (us)", lat >= 0 && lat < 30000, lat);
		while (kapi_core_state (core) == KAPI_CORE_RUNNING) kapi_msleep (5);
		kapi_core_release (core);
	}
	else
	{
		ax_putln ("  (no free app core: the tick check skipped)");
	}

	// 6. bad addresses
	r = kapi_wait_word ((volatile unsigned *) ((unsigned long) &s_word + 1), 0, 10);
	check ("unaligned -> -1", r == -1, r);
	r = kapi_wait_word ((volatile unsigned *) (40UL << 30), 0, 10);
	check ("unmapped -> -1", r == -1, r);

	// 7. priorities
	r = kapi_thread_priority (0, -1);
	check ("my priority: 0", r == 0, r);
	s_spin_stop = 0;
	tid = kapi_thread_create (spinner, 0, 0, "spin");
	r = kapi_thread_priority (tid, 1);
	check ("spinner made real time (was 0)", r == 0, r);
	r = kapi_thread_priority (tid, -1);
	check ("... now 1", r == 1, r);
	t0 = kapi_clock_us ();
	kapi_msleep (100);					// the busy real-time thread must let us run
	dt = kapi_clock_us () - t0;
	check ("main still runs beside it (ms)", dt < 400000, (long) (dt / 1000));
	s_spin_stop = 1;
	kapi_thread_join (tid, 2000, &code);
	check ("the spinner ran", s_spins > 0, (long) (s_spins > 0));
	r = kapi_thread_priority (99, 1);
	check ("no such thread -> -2", r == -2, r);

	ax_putln (s_fail == 0 ? "PASS" : "FAIL");
	return s_fail == 0 ? 0 : 1;
}
