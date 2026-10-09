//
// k_base.cpp -- the kapi page and the base system calls (onyxrun.h): the process (exit, its arguments, its
// environment, its pid, its folder), the time, the kernel log and the console, the memory (sbrk, vm_*, code_alloc),
// random bytes, what the kernel is, the libraries. As kernel/sys/kapi.cpp, procx.cpp, vm.cpp do them.
//
// MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors.
//
#include "onyxrun.h"
#include "el0blob.inc"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <chrono>
#include <thread>
#include <random>

Process g_Proc;

void rlog (const char *fmt, ...)
{
	char b[1024];
	va_list ap;
	va_start (ap, fmt);
	vsnprintf (b, sizeof b, fmt, ap);
	va_end (ap);
	fprintf (stderr, "onyxrun: %s\n", b);
	fflush (stderr);
}

// ---- the kapi page (kernel/sys/el0.cpp El0Init) -----------------------------------------------------------------
static bool s_UserSide[KAPI_SLOTS_HOST];
bool kapi_user_side (unsigned slot) { return slot < KAPI_SLOTS_HOST && s_UserSide[slot]; }

void kapi_init_table (void)
{
	hm_map (KAPI_TABLE_VA, HM_GRAIN, MEM_R, "the kapi table");
	hm_map (KAPI_STUBS_VA, HM_GRAIN, MEM_R | MEM_X, "the kapi stubs");
	u64 *T = (u64 *) KAPI_TABLE_VA;
	u32 *C = (u32 *) KAPI_STUBS_VA;
	memcpy ((u8 *) C + EL0_BLOB_OFFSET, s_El0Blob, sizeof s_El0Blob);
	T[0] = KAPI_ABI_VERSION;
	for (unsigned n = 1; n < KAPI_SLOTS_HOST; n++)
	{
		u32 *p = C + n * (EL0_STUB_SIZE / 4);
		p[0] = 0xD2800008u | (n << 5);		// movz x8, #n
		p[1] = 0xD4000001u;			// svc  #0
		p[2] = 0xD65F03C0u;			// ret
		p[3] = 0xD503201Fu;			// nop
		T[n] = KAPI_STUBS_VA + (u64) n * EL0_STUB_SIZE;
	}
	struct { unsigned slot; u64 off; } User[] =
	{
		{ (unsigned) SLOT (memcpy), BLOB_El0Memcpy }, { (unsigned) SLOT (memmove), BLOB_El0Memmove },
		{ (unsigned) SLOT (memset), BLOB_El0Memset }, { (unsigned) SLOT (pump_events), BLOB_El0PumpEvents },
		{ (unsigned) SLOT (wait_for_exit), BLOB_El0WaitForExit }, { (unsigned) SLOT (pump_wait), BLOB_El0PumpWait },
	};
	for (auto &u : User)
	{
		T[u.slot] = KAPI_STUBS_VA + EL0_BLOB_OFFSET + u.off;
		s_UserSide[u.slot] = true;
	}
}

u64 el0_main_return (void)	{ return KAPI_STUBS_VA + EL0_BLOB_OFFSET + BLOB_El0MainReturn; }
u64 el0_thread_return (void)	{ return KAPI_STUBS_VA + EL0_BLOB_OFFSET + BLOB_El0ThreadReturn; }
u64 el0_core_return (void)	{ return KAPI_STUBS_VA + EL0_BLOB_OFFSET + BLOB_El0CoreReturn; }

// ---- helpers -----------------------------------------------------------------------------------------------
// A string of the program's (NUL-terminated, at most max bytes) -> false: not readable.
bool guest_str (const char *p, std::string &out, size_t max)
{
	out.clear ();
	if (!p) return false;
	for (size_t i = 0; i < max; i++)
	{
		if (!hm_ok (p + i, 1, MEM_R)) return false;
		if (p[i] == '\0') return true;
		out += p[i];
	}
	return false;
}

// s into buf[cap] (cut, NUL-terminated) -> its whole length.
int str_out (char *buf, unsigned cap, const std::string &s)
{
	if (buf && cap > 0 && hm_ok (buf, cap, MEM_W))
	{
		size_t n = s.size () < cap - 1 ? s.size () : cap - 1;
		memcpy (buf, s.data (), n);
		buf[n] = '\0';
	}
	return (int) s.size ();
}

static int block_out (char *buf, unsigned cap, const std::vector<char> &b)
{
	if (buf && cap > 0)
	{
		unsigned n = cap < b.size () ? cap : (unsigned) b.size ();
		if (!hm_ok (buf, n, MEM_W)) return -KAPI_EFAULT;
		memcpy (buf, b.data (), n);
	}
	return (int) b.size ();
}

template <class T> static void put (T *p, T v) { if (p && hm_ok (p, sizeof (T), MEM_W)) *p = v; }

static u64 s_Start = 0;
static u64 now_us (void)
{
	u64 t = (u64) std::chrono::duration_cast<std::chrono::microseconds> (std::chrono::steady_clock::now ().time_since_epoch ()).count ();
	if (s_Start == 0) s_Start = t;
	return t - s_Start;
}
static struct StartClock { StartClock () { now_us (); } } s_StartClock;

static long long utc_us (void)
{
	return (long long) std::chrono::duration_cast<std::chrono::microseconds> (std::chrono::system_clock::now ().time_since_epoch ()).count ();
}

static int tz_minutes (void)
{
	time_t t = time (0);
	struct tm lt, gt;
#ifdef _WIN32
	localtime_s (&lt, &t); gmtime_s (&gt, &t);
#else
	localtime_r (&t, &lt); gmtime_r (&t, &gt);
#endif
	int d = (lt.tm_hour - gt.tm_hour) * 60 + (lt.tm_min - gt.tm_min);
	int dd = lt.tm_yday - gt.tm_yday;
	if (dd == 1 || dd < -1) d += 24 * 60; else if (dd == -1 || dd > 1) d -= 24 * 60;
	return d;
}

// ---- the process -------------------------------------------------------------------------------------------
static void k_exit (int status)		{ cpu_exit_process (status); }
static int k_should_exit (void)		{ return 0; }
static void k_yield (void)		{ std::this_thread::yield (); }
static void k_msleep (unsigned ms)	{ std::this_thread::sleep_for (std::chrono::milliseconds (ms)); }
static int k_sleep_us (unsigned long long us)	{ std::this_thread::sleep_for (std::chrono::microseconds (us)); return 0; }
static unsigned k_get_ticks (void)	{ return (unsigned) (now_us () / 10000); }	// (HZ 100)

static int k_get_args (char *buf, unsigned cap)
{
	// the line after the program's name: argv[1..] joined by spaces (quoted when they hold one)
	std::string s;
	const std::vector<char> &a = g_Proc.argv;
	size_t i = strlen (a.data ()) + 1;
	for (bool first = true; i < a.size () && a[i]; i += strlen (&a[i]) + 1, first = false)
	{
		if (!first) s += ' ';
		std::string w = &a[i];
		if (w.find (' ') != std::string::npos) s += "\"" + w + "\""; else s += w;
	}
	return str_out (buf, cap, s);
}
static int k_get_argv (char *buf, unsigned cap)	{ return block_out (buf, cap, g_Proc.argv); }
static int k_get_env (char *buf, unsigned cap)	{ return block_out (buf, cap, g_Proc.env); }
static int k_getpid (int which)			{ return which == 0 ? g_Proc.pid : which == 1 ? 1 : -KAPI_EINVAL; }

static int k_app_dir (char *buf, unsigned cap)
{
	return str_out (buf, cap, "SD:apps/" + g_Proc.name + ".app/");
}

static int k_get_datetime (int *y, int *mo, int *d, int *h, int *mi, int *s)
{
	time_t t = time (0);
	struct tm lt;
#ifdef _WIN32
	localtime_s (&lt, &t);
#else
	localtime_r (&t, &lt);
#endif
	put (y, lt.tm_year + 1900); put (mo, lt.tm_mon + 1); put (d, lt.tm_mday);
	put (h, lt.tm_hour); put (mi, lt.tm_min); put (s, lt.tm_sec);
	return 1;
}

// The guest's counter (CNTPCT_EL0 as the guest reads it) and its frequency (cpu.cpp).
void guest_counter (u64 *cnt, u64 *freq);

static int k_clock_info (struct kapi_clock_info *out)
{
	if (!hm_ok (out, sizeof *out, MEM_W)) return -KAPI_EFAULT;
	struct kapi_clock_info C;
	memset (&C, 0, sizeof C);
	u64 cnt = 0, freq = 1;
	guest_counter (&cnt, &freq);
	C.cnt = cnt; C.freq = freq;
	C.utc_us = utc_us ();
	C.tz_minutes = tz_minutes ();
	C.flags = 1;				// (KAPI_CLOCK_VALID: the PC's clock is set)
	u64 up = now_us ();
	C.boot_cnt = cnt - (u64) ((unsigned __int128) up * freq / 1000000);
	*out = C;
	return 0;
}

static int k_set_timezone (int) { return 1; }

// ---- the console, the kernel log -----------------------------------------------------------------------------
static int k_write (int, const void *b, unsigned n)
{
	if (!hm_ok (b, n, MEM_R)) return -1;
	std::string s ((const char *) b, n < 128 ? n : 128);
	rlog ("[kmsg] app: %s", s.c_str ());
	return (int) n;
}

static int k_stdout_write (const void *b, unsigned n)
{
	if (!hm_ok (b, n, MEM_R)) return -1;
	fwrite (b, 1, n, stdout);
	fflush (stdout);
	return (int) n;
}

static int k_stdin_read (void *b, unsigned n)
{
	if (!hm_ok (b, n, MEM_W)) return -1;
	size_t r = fread (b, 1, n, stdin);		// (a line at a time on a console)
	return (int) r;
}

// The process's own console streams: one non-zero handle each (k_files.cpp knows them).
void *stream_stdin (void);
void *stream_stdout (void);
static void *k_stdin_stream (void)	{ return stream_stdin (); }
static void *k_stdout_stream (void)	{ return stream_stdout (); }

static int k_klog_read (int *, char *, unsigned, char *, unsigned) { return 0; }
static int k_set_verbose (int on) { (void) on; return 0; }
static int k_get_verbose (void) { return 0; }

// ---- the memory ------------------------------------------------------------------------------------------------
static void *k_sbrk (long long inc)
{
	BigLockHold L;
	Process &P = g_Proc;
	u64 old = P.heapBrk;
	if (inc == 0) return (void *) old;
	if (inc < 0)
	{
		u64 dec = (u64) -inc;
		P.heapBrk = dec >= old - P.heapBase ? P.heapBase : old - dec;
		return (void *) old;			// (the pages kept mapped: the next growth reuses them)
	}
	if ((u64) inc > USER_HEAP_MAX - old) return (void *) -1;
	u64 want = old + (u64) inc;
	if (want > P.heapTop)
	{
		// grown by at least 1 MB at a time (fewer regions for the engines)
		u64 top = (want + 0xFFFFF) & ~0xFFFFFULL;
		if (top > USER_HEAP_MAX) top = (want + HM_GRAIN - 1) & ~(HM_GRAIN - 1);
		if (!hm_map (P.heapTop, top - P.heapTop, MEM_R | MEM_W, "the heap")) return (void *) -1;
		P.heapTop = top;
	}
	P.heapBrk = want;
	return (void *) old;
}

static int k_meminfo (u64 *total, u64 *freekb, u64 *app, unsigned *page)
{
	put (total, (u64) 4 << 20); put (freekb, (u64) 3 << 20); put (app, (u64) 64 << 10); put (page, 64u);
	return 1;
}

static unsigned prot_from (unsigned kprot)
{
	return ((kprot & KAPI_PROT_READ) ? MEM_R : 0) | ((kprot & KAPI_PROT_WRITE) ? MEM_W | MEM_R : 0) | ((kprot & KAPI_PROT_EXEC) ? MEM_X | MEM_R : 0);
}

static long long k_vm_map (unsigned long long addr, unsigned long long len, unsigned prot, unsigned flags)
{
	if (len == 0) return -KAPI_EINVAL;
	len = (len + HM_GRAIN - 1) & ~(HM_GRAIN - 1);
	BigLockHold L;
	u64 a = 0;
	if (flags & (KAPI_MAP_FIXED | KAPI_MAP_FIXED_NOREPLACE))
	{
		if ((addr & (HM_GRAIN - 1)) || addr < USER_MMAP_BASE || addr + len > USER_MMAP_END) return -KAPI_EINVAL;
		HmRegion r;
		bool taken = hm_next (addr, &r) && r.va < addr + len;
		if (taken && (flags & KAPI_MAP_FIXED_NOREPLACE)) return -KAPI_EEXIST;
		if (taken) hm_unmap (addr, len);
		a = addr;
	}
	else
	{
		if (addr >= USER_MMAP_BASE && addr + len <= USER_MMAP_END && (addr & (HM_GRAIN - 1)) == 0)
		{
			HmRegion r;
			if (!(hm_next (addr, &r) && r.va < addr + len)) a = addr;
		}
		if (a == 0) a = hm_find_free (USER_MMAP_BASE, USER_MMAP_END, len);
		if (a == 0) return -KAPI_ENOMEM;
	}
	if (!hm_map (a, len, prot_from (prot), "vm_map")) return -KAPI_ENOMEM;
	return (long long) a;
}

static bool in_mmap (u64 a, u64 len) { return a >= USER_MMAP_BASE && a + len <= USER_MMAP_END && a + len >= a; }

static int k_vm_unmap (unsigned long long addr, unsigned long long len)
{
	if ((addr & (HM_GRAIN - 1)) || len == 0 || !in_mmap (addr, len)) return -KAPI_EINVAL;
	BigLockHold L;
	hm_unmap (addr, len);
	return 0;
}

static int k_vm_protect (unsigned long long addr, unsigned long long len, unsigned prot)
{
	if ((addr & (HM_GRAIN - 1)) || len == 0 || !in_mmap (addr, len)) return -KAPI_EINVAL;
	BigLockHold L;
	hm_protect (addr, len, prot == 0 ? 0 : prot_from (prot));
	return 0;
}

static int k_vm_advise (unsigned long long addr, unsigned long long len, int advice)
{
	if (advice == KAPI_MADV_DONTNEED || advice == KAPI_MADV_FREE)
	{
		BigLockHold L;
		u64 a = (addr + HM_GRAIN - 1) & ~(HM_GRAIN - 1), e = (addr + len) & ~(HM_GRAIN - 1);
		for (u64 p = a; p < e; p += HM_GRAIN)
		{
			HmRegion r;
			if (hm_find (p, &r) && (r.prot & MEM_W)) memset ((void *) p, 0, HM_GRAIN);
		}
	}
	return 0;
}

static int k_vm_query (unsigned long long addr, struct kapi_vm_region *out)
{
	if (!hm_ok (out, sizeof *out, MEM_W)) return -KAPI_EFAULT;
	BigLockHold L;
	HmRegion r;
	int ret;
	if (hm_find (addr, &r)) ret = 0;
	else if (hm_next (addr, &r)) ret = 1;
	else return -KAPI_ENOMEM;
	struct kapi_vm_region R;
	memset (&R, 0, sizeof R);
	R.start = r.va; R.end = r.va + r.len;
	R.prot = (r.prot & MEM_R ? KAPI_PROT_READ : 0) | (r.prot & MEM_W ? KAPI_PROT_WRITE : 0) | (r.prot & MEM_X ? KAPI_PROT_EXEC : 0);
	R.kind = r.what == "vm_map" ? KAPI_VMK_ANON : r.what == "the heap" ? KAPI_VMK_HEAP : r.what.compare (0, 5, "stack") == 0
		 ? KAPI_VMK_STACK : r.what.compare (0, 7, "program") == 0 || r.what.compare (0, 7, "library") == 0 ? KAPI_VMK_IMAGE : KAPI_VMK_FIXED;
	R.resident = (unsigned) (r.len / HM_GRAIN);
	*out = R;
	return ret;
}

static int k_vm_stats (int pid, struct kapi_vm_stats *out)
{
	if (pid != 0 && pid != g_Proc.pid) return -KAPI_ESRCH;
	if (!hm_ok (out, sizeof *out, MEM_W)) return -KAPI_EFAULT;
	BigLockHold L;
	struct kapi_vm_stats S;
	memset (&S, 0, sizeof S);
	for (auto &r : hm_regions ()) { S.resident += r.len; if (r.prot & MEM_W) S.writable += r.len; }
	*out = S;
	return 0;
}

static void *k_code_alloc (unsigned long long size)
{
	if (size == 0) return 0;
	size = (size + HM_GRAIN - 1) & ~(HM_GRAIN - 1);
	BigLockHold L;
	u64 a = hm_find_free (USER_CODE_BASE, USER_CODE_END, size);
	if (a == 0 || !hm_map (a, size, MEM_RWX, "code_alloc")) return 0;
	return (void *) a;
}

// ---- misc ----------------------------------------------------------------------------------------------------
static int k_random (void *b, unsigned n)
{
	if (!hm_ok (b, n, MEM_W)) return 0;
	static std::random_device rd;
	for (unsigned i = 0; i < n; i++) ((u8 *) b)[i] = (u8) rd ();
	return (int) n;
}

static int k_kernel_info (char *buf, unsigned cap)
{
	char s[256];
	snprintf (s, sizeof s, "name Onyx\nabi %u\nbuilt onyxrun\nrev onyxrun\nmachine aarch64\nmodel onyxrun (Unicorn on a PC)\nram 4096\n",
		  (unsigned) KAPI_ABI_VERSION);
	return str_out (buf, cap, s);
}

static const void *k_lib_open (const char *name, unsigned minVersion, int *err)
{
	std::string n;
	int e = 0;
	u64 t = 0;
	if (!guest_str (name, n, 300)) e = -KAPI_EFAULT;
	else t = load_library (n.c_str (), minVersion, &e);
	if (err) put (err, e);
	return (const void *) t;
}

static int k_screen_size_none (void) { return 0; }

KAPI (exit, k_exit);
KAPI (should_exit, k_should_exit);
KAPI (yield, k_yield);
KAPI (msleep, k_msleep);
KAPI (sleep_us, k_sleep_us);
KAPI (get_ticks, k_get_ticks);
KAPI (get_args, k_get_args);
KAPI (get_argv, k_get_argv);
KAPI (get_env, k_get_env);
KAPI (getpid, k_getpid);
KAPI (app_dir, k_app_dir);
KAPI (get_datetime, k_get_datetime);
KAPI (clock_info, k_clock_info);
KAPI (set_timezone, k_set_timezone);
KAPI (write, k_write);
KAPI (stdout_write, k_stdout_write);
KAPI (stdin_read, k_stdin_read);
KAPI (stdin_stream, k_stdin_stream);
KAPI (stdout_stream, k_stdout_stream);
KAPI (klog_read, k_klog_read);
KAPI (set_verbose, k_set_verbose);
KAPI (get_verbose, k_get_verbose);
KAPI (sbrk, k_sbrk);
KAPI (meminfo, k_meminfo);
KAPI (vm_map, k_vm_map);
KAPI (vm_unmap, k_vm_unmap);
KAPI (vm_protect, k_vm_protect);
KAPI (vm_advise, k_vm_advise);
KAPI (vm_query, k_vm_query);
KAPI (vm_stats, k_vm_stats);
KAPI (code_alloc, k_code_alloc);
KAPI (random, k_random);
KAPI (kernel_info, k_kernel_info);
KAPI (lib_open, k_lib_open);
KAPI (kbd_ready, k_screen_size_none);
