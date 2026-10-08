//
// el0test -- protected mode (kern/el0.h): what a program running at EL0 -- every app, tool and
// plugin -- may and may not do, checked from the inside. Prints one line per check, then PASS /
// FAIL (a process not at EL0 is a failure: a kernel that still runs apps at EL1).
//
//   el0test            the checks: the mode, the user-side memcpy / memmove / memset, the
//                      counters, the core number, a kapi with buffers on the stack, threads and a
//                      mutex, a post run by the user-side pump, a job on an app core (at EL0).
//   el0test fault      a write into the kernel's memory: the process is killed (kmsg "el0: ...
//                      killed: a data abort ..."), the shell's prompt comes back.
//   el0test exec       a jump into the kernel's code: killed (an instruction abort).
//   el0test sysreg     mrs sctlr_el1, an EL1 register: killed (an undefined instruction).
//   el0test corefault  a job that faults on an app core: the job stops (state FAULT), the process
//                      goes on.
//
// (The function named *_el0scan_expected holds the privileged instruction on purpose:
// tools/el0scan.sh does not report it.)
//
// MIT licence (Onyx). Copyright (c) 2026 Stephane Wegener and the Onyx contributors.
//
#define KAPI_INLINE			// (a test of the kernel's table itself: read here, not through AppKit)
#include "appkit/appkit.h"
#include "uikit/win.h"		// the window API (UIKit's: uk_win_*)

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

// ---- 1. the memory primitives (the table's: user-side when protected) ------------------------

static unsigned char s_A[4096], s_B[4096];

static int mem_checks (void)
{
	int bad = 0;
	static const unsigned sizes[] = { 0, 1, 7, 15, 16, 17, 63, 64, 65, 200, 1000, 4000 };
	for (unsigned k = 0; k < sizeof sizes / sizeof sizes[0]; k++)
	{
		unsigned n = sizes[k];
		for (unsigned off = 0; off < 3; off++)
		{
			for (unsigned i = 0; i < sizeof s_A; i++) { s_A[i] = (unsigned char) (i * 7 + 3); s_B[i] = 0xEE; }
			KT->memcpy (s_B + off, s_A + 1, n);
			for (unsigned i = 0; i < n; i++) if (s_B[off + i] != s_A[1 + i]) bad++;
			if (s_B[off + n] != 0xEE) bad++;			// nothing past the end

			KT->memset (s_B + off, 0x5A, n);
			for (unsigned i = 0; i < n; i++) if (s_B[off + i] != 0x5A) bad++;
			if (s_B[off + n] != 0xEE) bad++;

			// overlapping moves, both ways
			for (unsigned i = 0; i < sizeof s_A; i++) s_A[i] = (unsigned char) i;
			KT->memmove (s_A + 5 + off, s_A + 3, n);			// up (backward copy)
			for (unsigned i = 0; i < n; i++) if (s_A[5 + off + i] != (unsigned char) (3 + i)) bad++;
			for (unsigned i = 0; i < sizeof s_A; i++) s_A[i] = (unsigned char) i;
			KT->memmove (s_A + 1, s_A + 4 + off, n);			// down (forward copy)
			for (unsigned i = 0; i < n; i++) if (s_A[1 + i] != (unsigned char) (4 + off + i)) bad++;
		}
	}
	return bad;
}

// ---- 3. threads ------------------------------------------------------------------------------

static int s_mutex;
static volatile long s_counter;

static int count_thread (void *arg)
{
	char local[64];					// (its own stack: written, read back)
	for (int i = 0; i < 64; i++) local[i] = (char) i;
	for (int i = 0; i < 20000; i++)
	{
		kapi_mutex_lock (s_mutex, KAPI_WAIT_FOREVER);
		long v = s_counter;
		for (volatile int d = 0; d < 2; d++) { }
		s_counter = v + 1;
		kapi_mutex_unlock (s_mutex);
	}
	return 40 + (int) (long) arg + local[63] - 63;
}

static volatile int s_posted;
static volatile int s_postedOnMain;

static void on_post (void *ctx, long value)
{
	(void) ctx;
	s_posted += (int) value;
	if (kapi_thread_self () == 1) s_postedOnMain = 1;
}

static int post_thread (void *arg)
{
	(void) arg;
	kapi_msleep (30);
	kapi_post (on_post, 0, 7);
	return 0;
}

// ---- 4. an app core ----------------------------------------------------------------------------

static unsigned char s_CoreStack[64 * 1024] __attribute__ ((aligned (16)));
static volatile unsigned s_coreId, s_coreSum, s_coreDone;
static unsigned char s_CoreBuf[256];

static void core_job (void *arg)
{
	(void) arg;
	s_coreId = kapi__core ();
	KT->memset (s_CoreBuf, 3, sizeof s_CoreBuf);		// (user-side code on the app core too)
	unsigned sum = 0;
	for (unsigned i = 0; i < sizeof s_CoreBuf; i++) sum += s_CoreBuf[i];
	s_coreSum = sum;
	asm volatile ("dmb ish" ::: "memory");
	s_coreDone = 1;
}

static void core_fault_job (void *arg)
{
	(void) arg;
	*(volatile unsigned *) 0x1000 = 1;		// the kernel's memory (EL1-only): a fault
}

static unsigned long __attribute__ ((noinline)) sysreg_el0scan_expected (void)
{
	unsigned long v;
	asm volatile ("mrs %0, sctlr_el1" : "=r" (v));	// an EL1 register: undefined at EL0
	return v;
}

static int wait_core (int c, unsigned ms)
{
	unsigned t0 = kapi_get_ticks ();
	int st;
	while ((st = kapi_core_state (c)) == KAPI_CORE_RUNNING && (kapi_get_ticks () - t0) * 10 < ms)
		kapi_msleep (5);
	return st;
}

int main (void)
{
	char args[32];
	kapi_get_args (args, sizeof args);
	int prot = kapi_is_protected ();
	ax_puts ("el0test: kapi v");
	put_num ((long) KT->version);
	ax_putln (prot ? ", PROTECTED (EL0)" : ", NOT protected: every app should run at EL0");

	if (ax_streq (args, "fault") || ax_streq (args, "exec") || ax_streq (args, "sysreg"))
	{
		if (!prot)
		{
			ax_putln ("FAIL: not at EL0 -- a fault here would not be an app's (not done)");
			return 1;
		}
		ax_putln ("now faulting: this process must be killed, the system must go on");
		if (ax_streq (args, "fault")) *(volatile unsigned *) 0x80000 = 0;		// the kernel image
		else if (ax_streq (args, "exec")) ((void (*) (void)) 0x80000) ();
		else (void) sysreg_el0scan_expected ();
		ax_putln ("FAIL: still running");
		return 1;
	}

	if (ax_streq (args, "corefault"))
	{
		if (!prot) { ax_putln ("FAIL: not at EL0 (not done)"); return 1; }
		int c = kapi_core_acquire ();
		if (c < 0) { ax_putln ("no free app core"); return 1; }
		kapi_core_run (c, core_fault_job, 0, s_CoreStack + sizeof s_CoreStack);
		check ("a faulting job on the app core stopped (state FAULT)", wait_core (c, 1000) == KAPI_CORE_FAULT,
		       kapi_core_state (c));
		kapi_core_release (c);
		ax_putln (s_fail == 0 ? "PASS" : "FAIL");
		return s_fail;
	}

	check ("the process runs at EL0", prot, prot);

	// 1. memory primitives
	check ("memcpy / memset / memmove (sizes, offsets, overlaps): bytes wrong", mem_checks () == 0, 0);
	check ("the table's memcpy is user code next to the table",
	       (unsigned long long) KT->memcpy > KAPI_TABLE_VA, 1);
	check ("the table's exit is a stub (a system call)",
	       (unsigned long long) KT->exit > KAPI_TABLE_VA, 1);

	// 2. what EL0 may read: the counters, the core number; a kapi writing to the stack
	unsigned long f, c0, c1;
	asm volatile ("mrs %0, cntfrq_el0" : "=r" (f));
	asm volatile ("isb; mrs %0, cntpct_el0" : "=r" (c0));
	kapi_msleep (20);
	asm volatile ("isb; mrs %0, cntpct_el0" : "=r" (c1));
	check ("cntfrq_el0 readable (Hz)", f > 0, (long) f);
	check ("cntpct_el0 moves across msleep (20) (us)", c1 > c0, f ? (long) ((c1 - c0) * 1000000 / f) : 0);
	check ("kapi__core () on the main thread", kapi__core () == 0, (long) kapi__core ());
	char cwd[128];					// (on the user stack)
	int n = kapi_getcwd (cwd, sizeof cwd);
	check ("getcwd into a stack buffer", n > 0 && cwd[0] != '\0', n);
	struct kapi_win_info wins[4];
	int nw = uk_win_list (wins, 4);
	check ("win_list into a stack array (the desktop first)", nw >= 1 && wins[0].id == KAPI_WIN_DESKTOP, nw);

	// 3. threads (their user stacks, thread_exit by return), a mutex, a post and the user-side pump
	s_mutex = kapi_mutex_create ();
	int tid[3];
	for (int i = 0; i < 3; i++) tid[i] = kapi_thread_create (count_thread, (void *) (long) i, 32 * 1024, "count");
	int codes = 0;
	for (int i = 0; i < 3; i++)
	{
		int code = -1;
		kapi_thread_join (tid[i], KAPI_WAIT_FOREVER, &code);
		if (code == 40 + i) codes++;
	}
	check ("3 threads: their exit codes", codes == 3, codes);
	check ("3 x 20000 increments under the mutex", s_counter == 60000, s_counter);
	s_posted = 0; s_postedOnMain = 0;
	int pt = kapi_thread_create (post_thread, 0, 0, "post");
	unsigned t0 = kapi_get_ticks ();
	while (s_posted == 0 && kapi_get_ticks () - t0 < 200) kapi_pump_wait (100);
	kapi_thread_join (pt, 1000, 0);
	check ("a post run by pump_wait, on the main thread", s_posted == 7 && s_postedOnMain, s_posted);

	// 4. an app core: the job at EL0, the user-side memset there, the core's number
	int c = kapi_core_acquire ();
	if (c < 0) ax_putln ("  --    no free app core (skipped)");
	else
	{
		s_coreDone = 0;
		kapi_core_run (c, core_job, 0, s_CoreStack + sizeof s_CoreStack);
		int st = wait_core (c, 1000);
		check ("a job on the app core returned (state IDLE)", st == KAPI_CORE_IDLE && s_coreDone, st);
		check ("kapi__core () on the app core", s_coreId == (unsigned) c, (long) s_coreId);
		check ("memset on the app core", s_coreSum == 3 * sizeof s_CoreBuf, (long) s_coreSum);
		kapi_core_release (c);
	}

	ax_putln (s_fail == 0 ? "PASS" : "FAIL");
	return s_fail;
}
