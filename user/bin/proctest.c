//
// proctest -- the processes and the clock of kapi v75 (WP-FILE/PROC, docs/POSIX-PLAN.md §3.2):
// getpid, get_argv / get_env, spawn_ex of itself with an argv and an environment (the child
// checks them, its old get_args string and its cwd, and exits 42), the environment inherited,
// the child's stdout on a pipe, proc_wait's reasons (an exit code, a fault -> KAPI_PROC_FAULT,
// a kill -> KAPI_PROC_KILLED) and WAIT_NOHANG / WAIT_KEEP, clock_info (monotonic, against
// get_datetime), sleep_us (1 / 5 / 20 ms: min / mean / max). One PASS / FAIL line per check; the
// exit code is the number of failures.
//
//   proctest                 the tests
//   proctest child <what>    (the children it spawns: exit42, sleep, fault, env <hash>, echo)
//
// MIT License. Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. Permission is hereby
// granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the Software is furnished to
// do so, subject to the following conditions: the above copyright notice and this permission
// notice shall be included in all copies or substantial portions of the Software. THE SOFTWARE IS
// PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO
// EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
#include "kapi.h"
#include "applib.h"

static int s_pass, s_fail;

static void put_num (long long v)
{
	char b[24]; int n = 0;
	if (v < 0) { kapi_stdout_write ("-", 1); v = -v; }
	do { b[n++] = (char) ('0' + v % 10); v /= 10; } while (v != 0);
	while (n > 0) kapi_stdout_write (&b[--n], 1);
}

static void check (const char *what, int ok, long long got)
{
	ax_puts (ok ? "PASS " : "FAIL ");
	ax_puts (what);
	ax_puts (" (");
	put_num (got);
	ax_putln (")");
	if (ok) s_pass++; else s_fail++;
}

static unsigned long long now_us (void)
{
	unsigned long long c, f;
	__asm__ volatile ("isb\n\tmrs %0, cntpct_el0\n\tmrs %1, cntfrq_el0" : "=r" (c), "=r" (f));
	return c / (f / 1000000);				// (54 MHz: an exact divisor)
}

// FNV-1a of a block (its bytes up to its final empty string)
static unsigned block_hash (const char *p, int n)
{
	unsigned h = 2166136261u;
	for (int i = 0; i < n; i++) { h ^= (unsigned char) p[i]; h *= 16777619u; }
	return h;
}

static const char *next_str (const char *p) { while (*p) p++; return p + 1; }

static char s_blk[65536];
static char s_self[256];

// ---- the children ------------------------------------------------------------------------------

static int child (const char *what)
{
	if (ax_streq (what, "exit42"))
	{
		// argv = { self, "child", "exit42", "a b", "c" }
		int n = kapi_get_argv (s_blk, sizeof s_blk);
		const char *p = s_blk;
		const char *want[] = { "child", "exit42", "a b", "c" };
		p = next_str (p);					// (argv[0]: the path)
		for (int i = 0; i < 4; i++, p = next_str (p)) if (!ax_streq (p, want[i])) return 1;
		if (*p != '\0' || n != (int) (p - s_blk) + 1) return 2;
		char a[256];
		kapi_get_args (a, sizeof a);
		if (!ax_streq (a, "child exit42 \"a b\" c")) return 3;
		n = kapi_get_env (s_blk, sizeof s_blk);
		if (n != 13 || !ax_streq (s_blk, "FOO=bar") || !ax_streq (s_blk + 8, "X=1")) return 4;
		kapi_getcwd (a, sizeof a);
		if (!ax_streq (a, "RAM:/")) return 5;
		if (kapi_getpid (1) <= 0) return 6;
		return 42;
	}
	if (ax_streq (what, "sleep")) { kapi_msleep (400); return 7; }
	if (ax_streq (what, "long")) { for (;;) kapi_msleep (100); }
	if (ax_streq (what, "fault"))
	{
		static volatile unsigned long s_bad = 0x800000000ul;	// (32 GB: nothing mapped there)
		*(volatile int *) s_bad = 1;
		return 0;
	}
	if (what[0] == 'e' && what[1] == 'n' && what[2] == 'v' && what[3] == ' ')	// "env <hash>"
	{
		unsigned want = 0;
		for (const char *p = what + 4; *p >= '0' && *p <= '9'; p++) want = want * 10 + (unsigned) (*p - '0');
		int n = kapi_get_env (s_blk, sizeof s_blk);
		return n > 0 && block_hash (s_blk, n) == want ? 0 : 1;
	}
	if (ax_streq (what, "echo")) { ax_puts ("hello from the child"); return 0; }
	return 99;
}

// ---- the tests ---------------------------------------------------------------------------------

static long long spawn (const char *argv, int argv_len, const char *envp, const char *cwd, void *out)
{
	(void) argv_len;
	struct kapi_spawn_attr a;
	for (unsigned i = 0; i < sizeof a; i++) ((char *) &a)[i] = 0;
	a.path = s_self;
	a.argv = argv;
	a.envp = envp;
	a.cwd = cwd;
	a.out = out;
	return kapi_spawn_ex (&a);
}

// "<self>\0child\0<what>\0\0" into b
static int argv_of (char *b, const char *what)
{
	int o = 0;
	for (const char *p = s_self; *p; p++) b[o++] = *p;
	b[o++] = '\0';
	for (const char *p = "child"; *p; p++) b[o++] = *p;
	b[o++] = '\0';
	for (const char *p = what; *p; p++) b[o++] = *p;
	b[o++] = '\0';
	b[o++] = '\0';
	return o;
}

static void procs (void)
{
	int pid = kapi_getpid (0);
	check ("getpid", pid > 0, pid);
	check ("getpid (1): the parent's", kapi_getpid (1) >= 0, kapi_getpid (1));
	check ("getpid (7): EINVAL", kapi_getpid (7) == -KAPI_EINVAL, 0);
	int n = kapi_get_argv (s_blk, sizeof s_blk);
	check ("get_argv: argv[0], the block", n > 2 && s_blk[0] != '\0' && s_blk[n - 1] == '\0', n);
	char small[4];
	check ("get_argv: its size with a small buffer", kapi_get_argv (small, sizeof small) == n, kapi_get_argv (small, sizeof small));
	int ne = kapi_get_env (s_blk, sizeof s_blk);
	check ("get_env: a block", ne >= 1 && s_blk[ne - 1] == '\0', ne);
	unsigned envhash = block_hash (s_blk, ne);

	// spawn_ex with argv, env, cwd -> 42
	static char av[1024];
	int o = 0;
	for (const char *p = s_self; *p; p++) av[o++] = *p;
	av[o++] = '\0';
	const char *args[] = { "child", "exit42", "a b", "c" };
	for (int i = 0; i < 4; i++) { for (const char *p = args[i]; *p; p++) av[o++] = *p; av[o++] = '\0'; }
	av[o++] = '\0';
	long long h = spawn (av, o, "FOO=bar\0X=1\0", "RAM:/", 0);
	check ("spawn_ex", h > 0, h);
	struct kapi_proc_status st;
	int r = kapi_proc_wait ((void *) h, KAPI_WAIT_NOHANG | KAPI_WAIT_KEEP, &st);
	check ("its pid at once (NOHANG | KEEP)", (r == 0 && st.pid > 0) || (r == 1 && st.pid > 0), st.pid);
	r = kapi_proc_wait ((void *) h, 0, &st);
	check ("argv / env / cwd / get_args round trip: exit 42", r == 1 && st.code == 42 && st.reason == KAPI_PROC_EXITED, st.code);
	check ("the handle closed by the wait", kapi_proc_wait ((void *) h, 0, &st) == -KAPI_EBADF, 0);

	// NOHANG on a running child
	av[0] = 0;
	o = argv_of (av, "sleep");
	h = spawn (av, o, 0, 0, 0);
	r = kapi_proc_wait ((void *) h, KAPI_WAIT_NOHANG, &st);
	check ("NOHANG: running -> 0", r == 0 && st.pid > 0 && st.reason == -1, r);
	r = kapi_proc_wait ((void *) h, KAPI_WAIT_KEEP, &st);
	check ("then it ends: 7", r == 1 && st.code == 7, st.code);
	r = kapi_proc_wait ((void *) h, KAPI_WAIT_NOHANG, &st);
	check ("KEEP: waited again (done: 1)", r == 1 && st.code == 7, r);

	// the environment inherited (envp 0)
	char what[32] = "env ";
	unsigned v = envhash; char num[12]; int k = 0;
	do { num[k++] = (char) ('0' + v % 10); v /= 10; } while (v != 0);
	int w = 4;
	while (k > 0) what[w++] = num[--k];
	what[w] = '\0';
	o = argv_of (av, what);
	h = spawn (av, o, 0, 0, 0);
	r = kapi_proc_wait ((void *) h, 0, &st);
	check ("the environment inherited (envp 0)", r == 1 && st.code == 0, st.code);

	// its stdout on a pipe
	void *pipe = kapi_pipe ();
	o = argv_of (av, "echo");
	h = spawn (av, o, 0, 0, pipe);
	kapi_proc_wait ((void *) h, 0, &st);
	char out[64]; int got = 0, m;
	while (got < 63 && (m = kapi_stream_read_nb (pipe, out + got, 63 - got)) > 0) got += m;
	out[got] = '\0';
	check ("its stdout on a pipe", ax_streq (out, "hello from the child"), got);
	kapi_stream_close (pipe);

	// a fault
	o = argv_of (av, "fault");
	h = spawn (av, o, 0, 0, 0);
	r = kapi_proc_wait ((void *) h, 0, &st);
	check ("a fault: KAPI_PROC_FAULT, -11", r == 1 && st.reason == KAPI_PROC_FAULT && st.code == -11, st.reason);

	// a kill
	o = argv_of (av, "long");
	h = spawn (av, o, 0, 0, 0);
	kapi_proc_wait ((void *) h, KAPI_WAIT_NOHANG | KAPI_WAIT_KEEP, &st);
	kapi_msleep (100);
	r = kapi_kill_pid (st.pid, 1);
	int r2 = kapi_proc_wait ((void *) h, 0, &st);
	check ("a kill: KAPI_PROC_KILLED, -9", r == 1 && r2 == 1 && st.reason == KAPI_PROC_KILLED && st.code == -9, st.reason);

	// the old spawn / wait: a killed child's status
	check ("a missing program: ENOENT", ({ struct kapi_spawn_attr a = { 0 }; a.path = "SD:/nope/nope"; kapi_spawn_ex (&a); }) == -KAPI_ENOENT, 0);
	check ("spawn_ex (0): EFAULT", kapi_spawn_ex (0) == -KAPI_EFAULT, 0);
	check ("proc_wait: a bad handle: EBADF", kapi_proc_wait ((void *) 0x7777, 0, &st) == -KAPI_EBADF, 0);
}

static void clocks (void)
{
	struct kapi_clock_info a, b;
	check ("clock_info", kapi_clock_info (&a) == 0, 0);
	check ("its frequency", a.freq >= 1000000, (long long) a.freq);
	check ("boot_cnt <= cnt", a.boot_cnt > 0 && a.boot_cnt <= a.cnt, (long long) ((a.cnt - a.boot_cnt) / a.freq));
	int mono = 1;
	unsigned long long last = 0;
	for (int i = 0; i < 2000; i++)
	{
		unsigned long long c;
		__asm__ volatile ("isb\n\tmrs %0, cntpct_el0" : "=r" (c));
		if (c < last) mono = 0;
		last = c;
		if (i % 200 == 0) kapi_yield ();
	}
	kapi_clock_info (&b);
	check ("the counter is monotonic (EL0 reads, across yields)", mono && b.cnt > a.cnt, mono);
	check ("the realtime sample moves with it", b.utc_us >= a.utc_us, (long long) (b.utc_us - a.utc_us));
	int y, mo, d, hh, mi, se;
	int valid = kapi_get_datetime (&y, &mo, &d, &hh, &mi, &se);
	check ("REALTIME_VALID = get_datetime's", ((b.flags & KAPI_CLOCK_REALTIME_VALID) != 0) == (valid != 0), (long long) b.flags);
	if (valid)
	{
		long long t = b.utc_us / 1000000 + b.tz_minutes * 60;	// (local seconds)
		long long sec = t % 86400;
		long long dt = sec - (hh * 3600 + mi * 60 + se);
		if (dt > 43200) dt -= 86400;
		if (dt < -43200) dt += 86400;
		check ("clock_info = get_datetime (local time of day, s)", dt >= -2 && dt <= 2, dt);
	}
	static const unsigned us[] = { 1000, 5000, 20000, 300 };
	for (int k = 0; k < 4; k++)
	{
		unsigned long long mn = ~0ull, mx = 0, sum = 0;
		for (int i = 0; i < 20; i++)
		{
			unsigned long long t0 = now_us ();
			kapi_sleep_us (us[k]);
			unsigned long long dt = now_us () - t0;
			if (dt < mn) mn = dt;
			if (dt > mx) mx = dt;
			sum += dt;
		}
		ax_puts ("     sleep_us (");
		put_num (us[k]);
		ax_puts ("): min ");
		put_num ((long long) mn);
		ax_puts (", mean ");
		put_num ((long long) (sum / 20));
		ax_puts (", max ");
		put_num ((long long) mx);
		ax_putln (" us");
		check ("sleep_us: never short, mean within +12 ms", mn + 50 >= us[k] && sum / 20 <= us[k] + 12000, (long long) (sum / 20));
	}
}

int main (void)
{
	int n = kapi_get_argv (s_blk, sizeof s_blk);
	if (n == -KAPI_ENOSYS)
	{
		ax_putln ("proctest: this kernel has no v75 processes (get_argv: ENOSYS)");
		return 1;
	}
	int i = 0;
	for (; s_blk[i] != '\0' && i < (int) sizeof s_self - 1; i++) s_self[i] = s_blk[i];
	s_self[i] = '\0';
	const char *p1 = next_str (s_blk);			// argv[1] == "child": argv[2] says what
	if (n > (int) (p1 - s_blk) + 1 && ax_streq (p1, "child"))
	{
		static char what[256];
		const char *p2 = next_str (p1);
		int k = 0;
		for (; p2[k] != '\0' && k < (int) sizeof what - 1; k++) what[k] = p2[k];
		what[k] = '\0';
		return child (what);
	}
	procs ();
	clocks ();
	ax_puts ("proctest: ");
	put_num (s_pass);
	ax_puts (" passed, ");
	put_num (s_fail);
	ax_putln (" failed");
	return s_fail;
}
