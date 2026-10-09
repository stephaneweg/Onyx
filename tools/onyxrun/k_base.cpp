//
// k_base.cpp -- the kapi page and the base system calls (onyxrun.h): the process (exit, its arguments, its
// environment, its pid, its folder), the time, the kernel log and the console, the memory (sbrk, vm_*, code_alloc),
// random bytes, what the kernel is, the libraries. As kernel/sys/kapi.cpp, procx.cpp, vm.cpp do them.
//
// MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors.
//
#include "onyxrun.h"
#include "kobj.h"
#include "el0blob.inc"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <chrono>
#include <thread>
#include <random>

Runner g_Run;

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

void kapi_init_table (Mem &M)
{
	M.map (KAPI_TABLE_VA, HM_GRAIN, MEM_R, "the kapi table", KAPI_VMK_FIXED);
	M.map (KAPI_STUBS_VA, HM_GRAIN, MEM_R | MEM_X, "the kapi stubs", KAPI_VMK_FIXED);
	u64 *T = (u64 *) M.g2h (KAPI_TABLE_VA, HM_GRAIN, 0);
	u32 *C = (u32 *) M.g2h (KAPI_STUBS_VA, HM_GRAIN, 0);
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

// ---- the guest's strings ---------------------------------------------------------------------------------------
bool gstr (u64 va, std::string &out, size_t max)
{
	out.clear ();
	Proc *P = cur ();
	if (!P || !va) return false;
	for (size_t i = 0; i < max; )
	{
		const Region *r = P->mem.find (va + i);
		if (!r || !(r->prot & MEM_R)) return false;
		const char *h = (const char *) r->host + (va + i - r->va);
		size_t n = r->va + r->len - (va + i);
		for (size_t k = 0; k < n && i < max; k++, i++)
		{
			if (h[k] == '\0') return true;
			out += h[k];
		}
	}
	return false;
}

int gstr_out (u64 buf, unsigned cap, const std::string &s)
{
	if (buf && cap > 0)
	{
		size_t n = s.size () < cap - 1 ? s.size () : cap - 1;
		u8 *h = GB (buf, n + 1, MEM_W);
		if (h) { memcpy (h, s.data (), n); h[n] = 0; }
	}
	return (int) s.size ();
}

static int block_out (u64 buf, unsigned cap, const std::vector<char> &b)
{
	if (buf && cap > 0)
	{
		unsigned n = cap < b.size () ? cap : (unsigned) b.size ();
		u8 *h = GB (buf, n, MEM_W);
		if (!h) return -KAPI_EFAULT;
		memcpy (h, b.data (), n);
	}
	return (int) b.size ();
}

// ---- time -------------------------------------------------------------------------------------------------------
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

// A sleep of the calling thread, cut in slices: its process's end ends it.
void sleep_ms_checked (u64 ms)
{
	auto until = std::chrono::steady_clock::now () + std::chrono::milliseconds (ms);
	for (;;)
	{
		check_dying ();
		auto now = std::chrono::steady_clock::now ();
		if (now >= until) return;
		auto step = until - now;
		if (step > std::chrono::milliseconds (50)) step = std::chrono::milliseconds (50);
		std::this_thread::sleep_for (step);
	}
}

static void k_exit (int status)		{ proc_exit (cur (), status, KAPI_PROC_EXITED); }
static void k_yield (void)		{ std::this_thread::yield (); }
static void k_msleep (unsigned ms)	{ sleep_ms_checked (ms); }
static int k_sleep_us (unsigned long long us)
{
	if (us >= 50000) sleep_ms_checked (us / 1000);
	else std::this_thread::sleep_for (std::chrono::microseconds (us));
	return 0;
}
static unsigned k_get_ticks (void)	{ return (unsigned) (now_us () / 10000); }	// (HZ 100)

static int k_get_datetime (u64 y, u64 mo, u64 d, u64 h, u64 mi, u64 s)
{
	time_t t = time (0);
	struct tm lt;
#ifdef _WIN32
	localtime_s (&lt, &t);
#else
	localtime_r (&t, &lt);
#endif
	gput<int> (y, lt.tm_year + 1900); gput<int> (mo, lt.tm_mon + 1); gput<int> (d, lt.tm_mday);
	gput<int> (h, lt.tm_hour); gput<int> (mi, lt.tm_min); gput<int> (s, lt.tm_sec);
	return 1;
}

static int k_clock_info (u64 out)
{
	struct kapi_clock_info *o = G<struct kapi_clock_info> (out, MEM_W);
	if (!o) return -KAPI_EFAULT;
	struct kapi_clock_info C;
	memset (&C, 0, sizeof C);
	u64 cnt = 0, freq = 1;
	guest_counter (&cnt, &freq);
	C.cnt = cnt; C.freq = freq;
	C.utc_us = utc_us ();
	C.tz_minutes = tz_minutes ();
	C.flags = 1;				// (KAPI_CLOCK_VALID: the PC's clock is set)
	C.boot_cnt = cnt - (u64) ((unsigned __int128) now_us () * freq / 1000000);
	*o = C;
	return 0;
}

static int k_set_timezone (int) { return 1; }

// ---- the process ----------------------------------------------------------------------------------------------
static int k_get_args (u64 buf, unsigned cap)
{
	// the line after the program's name: argv[1..] joined by spaces (quoted when they hold one)
	Proc *P = cur ();
	std::string s;
	const std::vector<char> &a = P->argv;
	size_t i = strlen (a.data ()) + 1;
	for (bool first = true; i < a.size () && a[i]; i += strlen (&a[i]) + 1, first = false)
	{
		if (!first) s += ' ';
		std::string w = &a[i];
		if (w.find (' ') != std::string::npos) s += "\"" + w + "\""; else s += w;
	}
	return gstr_out (buf, cap, s);
}
static int k_get_argv (u64 buf, unsigned cap)	{ return block_out (buf, cap, cur ()->argv); }
static int k_get_env (u64 buf, unsigned cap)	{ return block_out (buf, cap, cur ()->env); }
static int k_getpid (int which)			{ Proc *P = cur (); return which == 0 ? P->pid : which == 1 ? (P->ppid > 0 ? P->ppid : 1) : -KAPI_EINVAL; }
static int k_app_dir (u64 buf, unsigned cap)	{ return gstr_out (buf, cap, "SD:apps/" + cur ()->name + ".app/"); }
static int k_should_exit (void)			{ Proc *P = cur (); std::lock_guard<std::mutex> L (P->m); return P->exitAsked ? 1 : 0; }

// ---- the console, the kernel log -----------------------------------------------------------------------------
static int k_write (int, u64 b, unsigned n)
{
	if (n > 128) n = 128;
	u8 *h = GB (b, n, MEM_R);
	if (!h) return -1;
	rlog ("[kmsg] %s: %.*s", cur ()->name.c_str (), (int) n, (const char *) h);
	return (int) n;
}

static int k_stdout_write (u64 b, unsigned n)
{
	u8 *h = GB (b, n, MEM_R);
	Proc *P = cur ();
	if (!h || !P->stdoutStream) return -1;
	return obj_write (*P->stdoutStream, h, n, false);
}

static int k_stdin_read (u64 b, unsigned n)
{
	u8 *h = GB (b, n, MEM_W);
	Proc *P = cur ();
	if (!h || !P->stdinStream) return -1;
	int r = obj_read (*P->stdinStream, h, n, false);
	return r < 0 ? 0 : r;
}

static u64 k_stdin_stream (void)	{ Proc *P = cur (); return P->stdinStream ? h_add (P, P->stdinStream) : 0; }
static u64 k_stdout_stream (void)	{ Proc *P = cur (); return P->stdoutStream ? h_add (P, P->stdoutStream) : 0; }

static int k_klog_read (u64, u64, unsigned, u64, unsigned) { return 0; }
static int k_set_verbose (int) { return 0; }
static int k_get_verbose (void) { return 0; }

// ---- the memory ------------------------------------------------------------------------------------------------
static u64 k_sbrk (long long inc)
{
	Proc *P = cur ();
	PLock L (P);
	u64 old = P->heapBrk;
	if (inc == 0) return old;
	if (inc < 0)
	{
		u64 dec = (u64) -inc;
		P->heapBrk = dec >= old - P->heapBase ? P->heapBase : old - dec;
		return old;				// (the pages kept mapped: the next growth reuses them)
	}
	if ((u64) inc > USER_HEAP_MAX - old) return (u64) -1;
	u64 want = old + (u64) inc;
	if (want > P->heapTop)
	{
		// grown by at least 1 MB at a time (fewer regions for the engines)
		u64 top = (want + 0xFFFFF) & ~0xFFFFFULL;
		if (top > USER_HEAP_MAX) top = ALIGN_UP (want);
		if (!P->mem.map (P->heapTop, top - P->heapTop, MEM_R | MEM_W, "the heap", KAPI_VMK_HEAP)) return (u64) -1;
		P->heapTop = top;
	}
	P->heapBrk = want;
	return old;
}

static int k_meminfo (u64 total, u64 freekb, u64 app, u64 page)
{
	gput<u64> (total, (u64) 4 << 20); gput<u64> (freekb, (u64) 3 << 20); gput<u64> (app, (u64) 64 << 10); gput<unsigned> (page, 64u);
	return 1;
}

static unsigned prot_from (unsigned kprot)
{
	return ((kprot & KAPI_PROT_READ) ? MEM_R : 0) | ((kprot & KAPI_PROT_WRITE) ? MEM_W | MEM_R : 0) | ((kprot & KAPI_PROT_EXEC) ? MEM_X | MEM_R : 0);
}

long long vm_map_in (Proc *P, u64 addr, u64 len, unsigned prot, unsigned flags, const char *what, u8 *sharedHost)
{
	if (len == 0) return -KAPI_EINVAL;
	len = ALIGN_UP (len);
	PLock L (P);
	u64 a = 0;
	if (flags & (KAPI_MAP_FIXED | KAPI_MAP_FIXED_NOREPLACE))
	{
		if ((addr & (HM_GRAIN - 1)) || addr < USER_MMAP_BASE || addr + len > USER_MMAP_END) return -KAPI_EINVAL;
		const Region *r = P->mem.next (addr);
		bool taken = r && r->va < addr + len;
		if (taken && (flags & KAPI_MAP_FIXED_NOREPLACE)) return -KAPI_EEXIST;
		if (taken) P->mem.unmap (addr, len);
		a = addr;
	}
	else
	{
		if (addr >= USER_MMAP_BASE && addr + len <= USER_MMAP_END && (addr & (HM_GRAIN - 1)) == 0)
		{
			const Region *r = P->mem.next (addr);
			if (!(r && r->va < addr + len)) a = addr;
		}
		if (a == 0) a = P->mem.find_free (USER_MMAP_BASE, USER_MMAP_END, len);
		if (a == 0) return -KAPI_ENOMEM;
	}
	bool ok = sharedHost ? P->mem.map_shared (a, len, prot_from (prot), what, sharedHost, KAPI_VMK_SHM)
			     : P->mem.map (a, len, prot_from (prot), what, KAPI_VMK_ANON);
	return ok ? (long long) a : -KAPI_ENOMEM;
}

static long long k_vm_map (unsigned long long addr, unsigned long long len, unsigned prot, unsigned flags)
{
	return vm_map_in (cur (), addr, len, prot, flags, "vm_map", 0);
}

static bool in_mmap (u64 a, u64 len) { return a >= USER_MMAP_BASE && a + len <= USER_MMAP_END && a + len >= a; }

static int k_vm_unmap (unsigned long long addr, unsigned long long len)
{
	if ((addr & (HM_GRAIN - 1)) || len == 0 || !in_mmap (addr, len)) return -KAPI_EINVAL;
	Proc *P = cur ();
	PLock L (P);
	P->mem.unmap (addr, len);
	return 0;
}

static int k_vm_protect (unsigned long long addr, unsigned long long len, unsigned prot)
{
	if ((addr & (HM_GRAIN - 1)) || len == 0 || !in_mmap (addr, len)) return -KAPI_EINVAL;
	Proc *P = cur ();
	PLock L (P);
	P->mem.protect (addr, len, prot == 0 ? 0 : prot_from (prot));
	return 0;
}

static int k_vm_advise (unsigned long long addr, unsigned long long len, int advice)
{
	if (advice == KAPI_MADV_DONTNEED || advice == KAPI_MADV_FREE)
	{
		Proc *P = cur ();
		PLock L (P);
		u64 a = ALIGN_UP (addr), e = (addr + len) & ~(HM_GRAIN - 1);
		for (u64 p = a; p < e; p += HM_GRAIN)
		{
			const Region *r = P->mem.find (p);
			if (r && (r->prot & MEM_W) && (r->kind == KAPI_VMK_ANON || r->kind == KAPI_VMK_HEAP)) memset (r->host + (p - r->va), 0, HM_GRAIN);
		}
	}
	return 0;
}

static int k_vm_query (unsigned long long addr, u64 out)
{
	struct kapi_vm_region *o = G<struct kapi_vm_region> (out, MEM_W);
	if (!o) return -KAPI_EFAULT;
	Proc *P = cur ();
	PLock L (P);
	const Region *r = P->mem.find (addr);
	int ret = 0;
	if (!r) { r = P->mem.next (addr); ret = 1; }
	if (!r) return -KAPI_ENOMEM;
	struct kapi_vm_region R;
	memset (&R, 0, sizeof R);
	R.start = r->va; R.end = r->va + r->len;
	R.prot = (r->prot & MEM_R ? KAPI_PROT_READ : 0) | (r->prot & MEM_W ? KAPI_PROT_WRITE : 0) | (r->prot & MEM_X ? KAPI_PROT_EXEC : 0);
	R.kind = (unsigned) r->kind;
	R.resident = (unsigned) (r->len / HM_GRAIN);
	*o = R;
	return ret;
}

static int k_vm_stats (int pid, u64 out)
{
	Proc *P = pid == 0 ? cur () : proc_find (pid);
	if (!P || P->ended) return -KAPI_ESRCH;
	struct kapi_vm_stats *o = G<struct kapi_vm_stats> (out, MEM_W);
	if (!o) return -KAPI_EFAULT;
	struct kapi_vm_stats S;
	memset (&S, 0, sizeof S);
	{
		PLock L (P);
		for (auto &kv : P->mem.regions) { S.resident += kv.second.len; if (kv.second.prot & MEM_W) S.writable += kv.second.len; }
	}
	*o = S;
	return 0;
}

static u64 k_code_alloc (unsigned long long size)
{
	if (size == 0) return 0;
	size = ALIGN_UP (size);
	Proc *P = cur ();
	PLock L (P);
	u64 a = P->mem.find_free (USER_CODE_BASE, USER_CODE_END, size);
	if (a == 0 || !P->mem.map (a, size, MEM_RWX, "code_alloc", KAPI_VMK_FIXED)) return 0;
	return a;
}

// ---- misc ----------------------------------------------------------------------------------------------------
static int k_random (u64 b, unsigned n)
{
	u8 *h = GB (b, n, MEM_W);
	if (!h) return 0;
	static std::random_device rd;
	static std::mutex m;
	std::lock_guard<std::mutex> L (m);
	for (unsigned i = 0; i < n; i++) h[i] = (u8) rd ();
	return (int) n;
}

static int k_kernel_info (u64 buf, unsigned cap)
{
	char s[256];
	snprintf (s, sizeof s, "name Onyx\nabi %u\nbuilt onyxrun\nrev onyxrun\nmachine aarch64\nmodel onyxrun (Unicorn on a PC)\nram 4096\n",
		  (unsigned) KAPI_ABI_VERSION);
	return gstr_out (buf, cap, s);
}

static u64 k_lib_open (u64 name, unsigned minVersion, u64 err)
{
	std::string n;
	int e = 0;
	u64 t = 0;
	if (!gstr (name, n, 300)) e = -KAPI_EFAULT;
	else t = load_library (cur (), n.c_str (), minVersion, &e);
	if (err) gput<int> (err, e);
	return t;
}

static int k_kbd_ready (void) { return 1; }

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
KAPI (kbd_ready, k_kbd_ready);
