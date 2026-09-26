//
// coretest -- exercise the app cores (kapi v51): acquire core 2/3, run a job there,
// compare with the same job on core 0, stop a job that never ends, catch a job's fault,
// use both cores at once. "coretest exit" leaves a job spinning and exits: the kernel
// must stop it on its own (check kmsg: no "does not answer").
//
#include "kapi.h"
#include "applib.h"

static unsigned char s_Stack[2][64 * 1024] __attribute__ ((aligned (16)));

struct job
{
	volatile unsigned limit;
	volatile unsigned result;
	volatile unsigned counter;
	volatile int      stop;
};
static struct job s_Job[2];

static void say (const char *a, int v, const char *b)
{
	char n[16]; int k = ax_itoa (v, n); n[k] = 0;
	ax_puts (a); ax_puts (n); ax_putln (b);
}

static unsigned count_primes (unsigned limit)
{
	unsigned count = 0;
	for (unsigned n = 2; n < limit; n++)
	{
		unsigned d = 2;
		for (; d * d <= n; d++) if (n % d == 0) break;
		if (d * d > n) count++;
	}
	return count;
}

// --- the jobs (run on the app core: no kapi call, no allocation) ---
static void job_primes (void *arg)  { struct job *j = arg; j->result = count_primes (j->limit); }
static void job_forever (void *arg) { struct job *j = arg; for (;;) j->counter++; }
static void job_polite (void *arg)  { struct job *j = arg; while (!j->stop) j->counter++; }
static void job_fault (void *arg)			// 40 GB: user VA, nothing mapped there
{
	(void) arg;
	volatile unsigned *volatile p = (volatile unsigned *) (40ULL << 30);
	*p = 1;
}

static void *top (int i) { return s_Stack[i] + sizeof s_Stack[i]; }

static int wait_idle (int core, unsigned ms)
{
	unsigned t0 = kapi_get_ticks ();
	int st;
	while ((st = kapi_core_state (core)) == KAPI_CORE_RUNNING)
	{
		if ((kapi_get_ticks () - t0) * 10 > ms) break;
		kapi_msleep (5);
	}
	return st;
}

int main (void)
{
	char args[32]; kapi_get_args (args, sizeof args);
	int c = kapi_core_acquire ();
	if (c < 0) { ax_putln ("coretest: no free app core (kernel < v51, or both taken)"); return 1; }
	say ("acquired core ", c, "");

	if (ax_streq (args, "exit"))
	{
		kapi_core_run (c, job_forever, &s_Job[0], top (0));
		ax_putln ("job spinning on the core; exiting without release (the kernel stops it)");
		return 0;
	}

	// 1. the same computation on the app core, then on core 0
	s_Job[0].limit = 300000; s_Job[0].result = 0;
	unsigned t0 = kapi_get_ticks ();
	if (kapi_core_run (c, job_primes, &s_Job[0], top (0)) != 0) { ax_putln ("core_run failed"); return 1; }
	int st = wait_idle (c, 30000);
	unsigned t1 = kapi_get_ticks ();
	say ("primes < 300000 on the app core: ", (int) s_Job[0].result, st == KAPI_CORE_IDLE ? "" : "  (NOT finished!)");
	say ("  time (10 ms ticks): ", (int) (t1 - t0), "");
	t0 = kapi_get_ticks ();
	unsigned r0 = count_primes (300000);
	t1 = kapi_get_ticks ();
	say ("primes on core 0: ", (int) r0, r0 == s_Job[0].result ? "  (same: OK)" : "  (DIFFERENT!)");
	say ("  time (10 ms ticks): ", (int) (t1 - t0), "");

	// 2. a polite job stopped by a flag
	s_Job[0].stop = 0; s_Job[0].counter = 0;
	kapi_core_run (c, job_polite, &s_Job[0], top (0));
	kapi_msleep (100);
	s_Job[0].stop = 1;
	st = wait_idle (c, 1000);
	say ("polite job: counted ", (int) (s_Job[0].counter / 1000), "k in ~100 ms");
	ax_putln (st == KAPI_CORE_IDLE ? "  stopped by its flag: OK" : "  did NOT stop!");

	// 3. a job that never ends, dropped by release
	s_Job[0].counter = 0;
	kapi_core_run (c, job_forever, &s_Job[0], top (0));
	kapi_msleep (50);
	unsigned seen = s_Job[0].counter;
	kapi_core_release (c);
	unsigned after = s_Job[0].counter;
	kapi_msleep (20);
	ax_putln (seen > 0 && s_Job[0].counter == after ? "endless job: runs, then release stopped it: OK"
							  : "endless job: NOT stopped (or never ran)!");
	c = kapi_core_acquire ();
	say ("re-acquired core ", c, "");
	if (c < 0) return 1;

	// 4. a job that faults
	kapi_core_run (c, job_fault, 0, top (0));
	st = wait_idle (c, 1000);
	ax_putln (st == KAPI_CORE_FAULT ? "faulting job: caught (see kmsg), machine alive: OK" : "faulting job: NOT reported!");

	// 5. both app cores at once
	int c2 = kapi_core_acquire ();
	if (c2 < 0) ax_putln ("second core: none free (skipped)");
	else
	{
		s_Job[0].limit = s_Job[1].limit = 300000;
		s_Job[0].result = s_Job[1].result = 0;
		t0 = kapi_get_ticks ();
		kapi_core_run (c, job_primes, &s_Job[0], top (0));
		kapi_core_run (c2, job_primes, &s_Job[1], top (1));
		wait_idle (c, 30000); wait_idle (c2, 30000);
		t1 = kapi_get_ticks ();
		say ("cores 2 + 3 together, each the same count: ", (int) s_Job[1].result,
		     s_Job[0].result == r0 && s_Job[1].result == r0 ? "  (OK)" : "  (WRONG!)");
		say ("  time (10 ms ticks): ", (int) (t1 - t0), "");
		kapi_core_release (c2);
	}
	kapi_core_release (c);
	ax_putln ("done");
	return 0;
}
