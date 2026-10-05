//
// memtest -- the demand paging and the v75 memory calls (kapi vm_map / vm_unmap / vm_protect /
// vm_advise / vm_query / vm_stats / thread_create_ex / thread_info; docs/POSIX-PLAN.md §3.1,
// docs/02 §4 and §8 "v75: memory"). Freestanding, at the kapi level. Every check prints a PASS or
// FAIL line; the last line is "memtest: PASS" or "memtest: FAIL (n)", and the exit status 0 / 1.
//
//   memtest           every check below (bounded: a child touches 256 MB -- a quarter of the free
//                     memory at most -- and must give it all back; a few seconds)
//   memtest oom       ALSO the real out-of-memory kill, twice (each must end where it started): a child
//                     touches pages until the kernel kills
//                     it (it takes the whole app pool down to its 16 MB reserve for a moment: other
//                     apps that fault meanwhile may be killed too -- run it alone)
//   memtest net       ALSO tcp_recv into fresh lazy memory, over a connection to this Pi's own
//                     address (the network up)
//
// The checks: lazy vm_map (resident 0, then 1 page); a 1 GB PROT_NONE reservation committed 64 KB
// at a time with vm_protect; a partial unmap (split: 2 regions); MADV_DONTNEED zero-fills; FIXED /
// FIXED_NOREPLACE and the error values; vm_stats (unmap returns the frames); the lazy heap (sbrk);
// children killed by a write to a READ page, a PROT_NONE read, a thread's and the main thread's
// stack overflow (status -11, reason FAULT when proc_wait exists); kapi I/O in place into fresh
// lazy memory (read of a 1 MB file, get_args, a pipe); threads faulting the same pages; a futex on
// a lazy page; an unmap while another thread blocks in a read into that memory (the deferred zap)
// and while one waits on a word there; TLS per thread (TPIDR_EL0 across 1000 sleeps / yields,
// thread_create_ex's tls); thread_info's bounds; an app core's job touching unfilled memory (the
// pager) and seeing its caller's TLS; the overcommit refusal.
//
// (Internal: "memtest child <what>" is how it runs its own children.)
//
// ---------------------------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2026 Stéphane Wegener and the Onyx contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software
// and associated documentation files (the "Software"), to deal in the Software without
// restriction, including without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
// BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
// ---------------------------------------------------------------------------------------------
//
#include "appkit/appkit.h"
#include "applib.h"

#define PAGE		0x10000ULL
#define RW		(KAPI_PROT_READ | KAPI_PROT_WRITE)
#define SELF		"SD:/bin/memtest"

typedef unsigned long long u64;

static int s_nFail, s_nPass;

// ---- output -------------------------------------------------------------------------------------

static void put_u64 (u64 v)
{
	char b[24]; int n = 0;
	do { b[n++] = (char) ('0' + v % 10); v /= 10; } while (v != 0);
	char o[24]; int k = 0;
	while (n > 0) o[k++] = b[--n];
	o[k] = 0;
	ax_puts (o);
}

static void put_i (long long v)
{
	if (v < 0) { ax_puts ("-"); v = -v; }
	put_u64 ((u64) v);
}

static void check (const char *pWhat, int bOK)
{
	ax_puts (bOK ? "PASS  " : "FAIL  ");
	ax_putln (pWhat);
	if (bOK) s_nPass++; else s_nFail++;
}

// A check with a number shown (e.g. a time, a count).
static void check_n (const char *pWhat, int bOK, const char *pLabel, long long v)
{
	ax_puts (bOK ? "PASS  " : "FAIL  ");
	ax_puts (pWhat);
	ax_puts (" (");
	ax_puts (pLabel);
	put_i (v);
	ax_putln (")");
	if (bOK) s_nPass++; else s_nFail++;
}

static u64 resident (void)
{
	struct kapi_vm_stats S;
	return kapi_vm_stats (0, &S) == 0 ? S.resident : 0;
}

static u64 faults (void)
{
	struct kapi_vm_stats S;
	return kapi_vm_stats (0, &S) == 0 ? S.faults : 0;
}

static unsigned region_resident (u64 a)
{
	struct kapi_vm_region R;
	return kapi_vm_query (a, &R) == 0 ? R.resident : 0xFFFFFFFFu;
}

static unsigned long get_tp (void)
{
	unsigned long v;
	asm volatile ("mrs %0, tpidr_el0" : "=r" (v));
	return v;
}

static void set_tp (unsigned long v)
{
	asm volatile ("msr tpidr_el0, %0" :: "r" (v) : "memory");
}

// ---- children: killed on purpose -----------------------------------------------------------------

static volatile int s_nSink;
static volatile int s_nDepthMax = 0x7FFFFFFF;	// (never reached: the stack ends first)

static int __attribute__ ((noinline)) recurse (int n)
{
	volatile char Frame[4096];
	if (n >= s_nDepthMax) return 0;
	Frame[0] = (char) n;
	Frame[4095] = (char) n;
	s_nSink += Frame[0];
	return recurse (n + 1) + Frame[4095];
}

static int overflow_thread (void *pArg)
{
	(void) pArg;
	return recurse (0);
}

static int child (const char *pWhat)
{
	if (ax_streq (pWhat, "ro"))			// a write to a READ page
	{
		long long a = kapi_vm_map (0, PAGE, RW, 0);
		if (a < 0) return 2;
		*(volatile unsigned *) a = 1;
		kapi_vm_protect ((u64) a, PAGE, KAPI_PROT_READ);
		(void) *(volatile unsigned *) a;	// (reading is allowed)
		*(volatile unsigned *) a = 2;		// killed here
		return 3;
	}
	if (ax_streq (pWhat, "none"))			// a read of a PROT_NONE reservation
	{
		long long a = kapi_vm_map (0, 16 * PAGE, KAPI_PROT_NONE, KAPI_MAP_NORESERVE);
		if (a < 0) return 2;
		return (int) *(volatile unsigned *) (a + 4 * PAGE);	// killed here
	}
	if (ax_streq (pWhat, "unmapped"))		// a page that was unmapped
	{
		long long a = kapi_vm_map (0, 2 * PAGE, RW, 0);
		if (a < 0) return 2;
		*(volatile unsigned *) a = 1;
		kapi_vm_unmap ((u64) a, 2 * PAGE);
		return (int) *(volatile unsigned *) a;	// killed here
	}
	if (ax_streq (pWhat, "tstack"))			// a thread overflows its 1 MB stack
	{
		struct kapi_thread_attr A;
		kapi_memset (&A, 0, sizeof A);
		A.fn = (unsigned long long) (unsigned long) overflow_thread;
		A.stack_size = 0x100000;
		A.name = "overflow";
		int t = kapi_thread_create_ex (&A);
		if (t == -KAPI_ENOSYS) t = kapi_thread_create (overflow_thread, 0, 0x100000, "overflow");
		if (t < 0) return 2;
		kapi_thread_join (t, 10000, 0);		// (the whole process is killed meanwhile)
		return 3;
	}
	if (ax_streq (pWhat, "mstack"))			// the main thread overflows its stack
	{
		return recurse (0);
	}
	if (pWhat[0] == 't' && pWhat[1] == 'o' && pWhat[2] == 'u' && pWhat[3] == 'c' && pWhat[4] == 'h')
	{						// "touch <MB>": touch that much, exit normally
		u64 nMB = 0;
		for (const char *q = pWhat + 6; *q >= '0' && *q <= '9'; q++) nMB = nMB * 10 + (u64) (*q - '0');
		long long a = kapi_vm_map (0, nMB << 20, RW, 0);
		if (a < 0) return 2;
		for (u64 p = 0; p < (nMB << 20); p += PAGE) *(volatile unsigned *) (a + p) = 1;
		return 0;
	}
	if (ax_streq (pWhat, "oom"))			// touch until the kernel says no more
	{
		long long a = kapi_vm_map (0, 24ULL << 30, RW, KAPI_MAP_NORESERVE);
		if (a < 0) return 2;
		for (u64 p = 0; p < (24ULL << 30); p += PAGE) *(volatile unsigned *) (a + p) = 1;
		return 3;
	}
	return 4;
}

// Run "memtest child <what>": -> its exit status; *pReason its KAPI_PROC_* (-1: unknown, the
// kernel has no proc_wait yet).
static int run_child (const char *pWhat, int *pReason)
{
	char Args[48];
	int n = 0;
	const char *pre = "child ";
	for (int k = 0; pre[k]; k++) Args[n++] = pre[k];
	for (int k = 0; pWhat[k] && n < (int) sizeof Args - 1; k++) Args[n++] = pWhat[k];
	Args[n] = 0;
	*pReason = -1;
	void *pProc = kapi_spawn (SELF, Args, 0, 0);
	if (pProc == 0) return 1000;
	struct kapi_proc_status St;
	int r = kapi_proc_wait (pProc, 0, &St);
	if (r == -KAPI_ENOSYS)
	{
		return kapi_wait (pProc);
	}
	if (r < 0) return 1001;
	*pReason = St.reason;
	return St.code;
}

static unsigned long free_kb (void)
{
	unsigned long t, f, a; unsigned pk;
	kapi_meminfo (&t, &f, &a, &pk);
	return f;
}

// After a child: the free memory back to f0 (within 8 MB), waiting up to 5 s for the reaper.
static void check_back (const char *pWhat, unsigned long f0)
{
	unsigned long f1 = free_kb ();
	for (int w = 0; w < 50 && f1 + 8192 < f0; w++)
	{
		kapi_msleep (100);
		f1 = free_kb ();
	}
	if (f1 + 8192 < f0) { ax_puts ("      (KB free before the child: "); put_i ((long long) f0); ax_putln (")"); }
	check_n (pWhat, f1 + 8192 >= f0, "KB free now ", (long long) f1);
}

static void check_killed (const char *pWhat, const char *pChild, int nStatus, int nReason)
{
	int nWhy;
	int s = run_child (pChild, &nWhy);
	check_n (pWhat, s == nStatus && (nWhy < 0 || nWhy == nReason), "status ", s);
}

// ---- 1. regions ------------------------------------------------------------------------------------

// (v78) Generated code, as a JIT makes it: written, the caches made coherent over it (DC CVAU,
// IC IVAU: allowed at EL0), run; then made read + execute (W^X) and run again.
static void sync_code (u64 a, u64 n)
{
	for (u64 p = a & ~63ULL; p < a + n; p += 64) asm volatile ("dc cvau, %0" :: "r" (p) : "memory");
	asm volatile ("dsb ish" ::: "memory");
	for (u64 p = a & ~63ULL; p < a + n; p += 64) asm volatile ("ic ivau, %0" :: "r" (p) : "memory");
	asm volatile ("dsb ish; isb" ::: "memory");
}

static void test_exec (void)
{
	long long c = kapi_vm_map (0, PAGE, RW | KAPI_PROT_EXEC, 0);
	check ("vm_map RW + EXEC -> mapped", c > 0);
	if (c <= 0) return;
	volatile unsigned *code = (volatile unsigned *) c;
	code[0] = 0x52800540;			// mov w0, #42
	code[1] = 0xD65F03C0;			// ret
	sync_code ((u64) c, 8);
	int (*fn) (void) = (int (*) (void)) c;
	check ("generated code runs -> 42", fn () == 42);
	check ("vm_protect READ + EXEC (W^X)", kapi_vm_protect ((u64) c, PAGE, KAPI_PROT_READ | KAPI_PROT_EXEC) == 0);
	check ("runs again, read + execute", fn () == 42);
	check ("vm_protect RW + EXEC again", kapi_vm_protect ((u64) c, PAGE, RW | KAPI_PROT_EXEC) == 0);
	code[0] = 0x52800E00;			// mov w0, #112
	sync_code ((u64) c, 8);
	check ("rewritten code runs -> 112", fn () == 112);
	kapi_vm_unmap ((u64) c, PAGE);
}

static void test_regions (void)
{
	// lazy vm_map: nothing resident until touched
	u64 f0 = faults ();
	long long a = kapi_vm_map (0, 16 * PAGE, RW, 0);
	check ("vm_map 1 MB RW -> an address in the mmap arena", a >= (long long) (34ULL << 30) && (a & (PAGE - 1)) == 0);
	if (a < 0) return;
	unsigned r0 = region_resident ((u64) a);	// (its region may hold a neighbour's pages too)
	check ("  resident 0 before a touch", r0 != 0xFFFFFFFFu);
	((volatile char *) a)[5 * PAGE + 3] = 7;
	check ("  resident 1 page after one touch", region_resident ((u64) a) == r0 + 1);
	check ("  the page is zero-filled around the write", ((volatile char *) a)[5 * PAGE + 2] == 0
						      && ((volatile unsigned *) a)[5 * PAGE / 4 + 100] == 0);
	check ("  vm_stats counts the fault", faults () >= f0 + 1);
	struct kapi_vm_region R;
	// (a region alike next to it is merged with it, as Linux does: the region holds the range)
	check ("  vm_query: kind ANON, lazy, prot RW", kapi_vm_query ((u64) a, &R) == 0 && R.kind == KAPI_VMK_ANON
						      && (R.flags & KAPI_VMF_LAZY) && R.prot == RW
						      && R.start <= (u64) a && R.end >= (u64) a + 16 * PAGE);

	// partial unmap in the middle: two regions
	check ("vm_unmap of 2 pages in the middle -> 0", kapi_vm_unmap ((u64) a + 4 * PAGE, 2 * PAGE) == 0);
	int r1 = kapi_vm_query ((u64) a, &R);
	u64 e1 = R.end;
	int r2 = kapi_vm_query ((u64) a + 4 * PAGE, &R);
	check ("  vm_query shows 2 regions around the hole", r1 == 0 && e1 == (u64) a + 4 * PAGE
							   && r2 == 1 && R.start == (u64) a + 6 * PAGE);
	check ("  the touched page left with the hole (resident 0 on both sides)",
	       region_resident ((u64) a) == 0 && region_resident ((u64) a + 6 * PAGE) == 0);
	kapi_vm_unmap ((u64) a, 16 * PAGE);
	check ("vm_unmap of the rest (holes allowed) -> the region gone",
	       kapi_vm_query ((u64) a, &R) != 0 || R.start >= (u64) a + 16 * PAGE);

	// a 1 GB reservation, committed 64 KB at a time
	long long b = kapi_vm_map (0, 1ULL << 30, KAPI_PROT_NONE, KAPI_MAP_NORESERVE);
	check ("vm_map 1 GB PROT_NONE NORESERVE", b > 0);
	if (b > 0)
	{
		u64 c = (u64) b + 512 * PAGE;
		check ("  vm_protect 64 KB in the middle RW -> 0", kapi_vm_protect (c, PAGE, RW) == 0);
		((volatile unsigned *) c)[10] = 0x5A5A;
		check ("  written and read back", ((volatile unsigned *) c)[10] == 0x5A5A);
		int q = kapi_vm_query (c, &R);
		check ("  vm_query: that page RW, the reservation split in 3",
		       q == 0 && R.start == c && R.end == c + PAGE && R.prot == RW);
		check ("  vm_protect back to NONE, then RW: the data kept",
		       kapi_vm_protect (c, PAGE, KAPI_PROT_NONE) == 0 && kapi_vm_protect (c, PAGE, RW) == 0
		       && ((volatile unsigned *) c)[10] == 0x5A5A);
		check ("  vm_protect of the whole 1 GB to NONE merges it back", kapi_vm_protect ((u64) b, 1ULL << 30, KAPI_PROT_NONE) == 0
		       && kapi_vm_query ((u64) b, &R) == 0 && R.end == (u64) b + (1ULL << 30));
		check ("  vm_unmap 1 GB -> 0", kapi_vm_unmap ((u64) b, 1ULL << 30) == 0);
	}

	// DONTNEED: zeros on the next touch
	long long d = kapi_vm_map (0, 4 * PAGE, RW, 0);
	if (d > 0)
	{
		for (int i = 0; i < 4; i++) ((volatile unsigned *) (d + i * (long long) PAGE))[1] = 0xAB;
		unsigned nBefore = region_resident ((u64) d);
		check ("MADV_DONTNEED -> 0", kapi_vm_advise ((u64) d, 4 * PAGE, KAPI_MADV_DONTNEED) == 0);
		check ("  the pages left (resident 4 -> 0)", nBefore == 4 && region_resident ((u64) d) == 0);
		check ("  read back as zeros", ((volatile unsigned *) d)[1] == 0);
		check ("MADV_WILLNEED fills them", kapi_vm_advise ((u64) d, 4 * PAGE, KAPI_MADV_WILLNEED) == 0
						     && region_resident ((u64) d) == 4);
		check ("MADV_DONTNEED on the stack -> -EINVAL", kapi_vm_advise (((u64) (unsigned long) &nBefore) & ~(PAGE - 1), PAGE,
										 KAPI_MADV_DONTNEED) == -KAPI_EINVAL);
		kapi_vm_unmap ((u64) d, 4 * PAGE);
	}

	// FIXED, FIXED_NOREPLACE, the errors
	long long f = kapi_vm_map (0, 4 * PAGE, RW, 0);
	if (f > 0)
	{
		kapi_vm_unmap ((u64) f, 4 * PAGE);
		check ("vm_map FIXED at a free address -> that address", kapi_vm_map ((u64) f, 2 * PAGE, RW, KAPI_MAP_FIXED) == f);
		((volatile unsigned *) f)[0] = 99;
		check ("vm_map FIXED_NOREPLACE over it -> -EEXIST",
		       kapi_vm_map ((u64) f, PAGE, RW, KAPI_MAP_FIXED_NOREPLACE) == -KAPI_EEXIST);
		check ("vm_map FIXED over it -> replaced, zero-filled",
		       kapi_vm_map ((u64) f, PAGE, RW, KAPI_MAP_FIXED) == f && ((volatile unsigned *) f)[0] == 0);
		check ("vm_map FIXED unaligned -> -EINVAL", kapi_vm_map ((u64) f + 8, PAGE, RW, KAPI_MAP_FIXED) == -KAPI_EINVAL);
		kapi_vm_unmap ((u64) f, 4 * PAGE);
		long long h = kapi_vm_map ((u64) f, PAGE, RW, 0);
		check ("vm_map with a free hint -> the hint", h == f);
		if (h > 0) kapi_vm_unmap ((u64) h, PAGE);
	}
	check ("vm_map FIXED outside the arena (8 GB) -> -EINVAL",
	       kapi_vm_map (8ULL << 30, PAGE, RW, KAPI_MAP_FIXED) == -KAPI_EINVAL);
	check ("vm_map length 0 -> -EINVAL", kapi_vm_map (0, 0, RW, 0) == -KAPI_EINVAL);
	test_exec ();
	check ("vm_unmap of the heap -> -EINVAL", kapi_vm_unmap (10ULL << 30, PAGE) == -KAPI_EINVAL);
	check ("vm_protect of a hole -> -EINVAL", kapi_vm_protect (59ULL << 30, PAGE, RW) == -KAPI_EINVAL);
	check ("vm_query (bad pointer) -> -EFAULT", kapi_vm_query (8ULL << 30, (struct kapi_vm_region *) 0x80000) == -KAPI_EFAULT);
	check ("vm_stats (no such pid) -> -ESRCH", kapi_vm_stats (999999, 0) == -KAPI_ESRCH);

	// the overcommit heuristic
	check ("vm_map 25 GB RW (more than the RAM) -> -ENOMEM", kapi_vm_map (0, 25ULL << 30, RW, 0) == -KAPI_ENOMEM);
	long long n = kapi_vm_map (0, 25ULL << 30, RW, KAPI_MAP_NORESERVE);
	check ("vm_map 25 GB RW NORESERVE -> mapped (lazy)", n > 0);
	if (n > 0) kapi_vm_unmap ((u64) n, 25ULL << 30);
}

// ---- 2. vm_stats: frames come and go --------------------------------------------------------------

static void test_stats (void)
{
	u64 r0 = resident ();
	long long a = kapi_vm_map (0, 64 * PAGE, RW, 0);
	if (a < 0) { check ("vm_map 4 MB", 0); return; }
	for (int i = 0; i < 64; i++) ((volatile char *) a)[i * PAGE] = 1;
	u64 r1 = resident ();
	check_n ("vm_stats: 4 MB touched -> resident +4 MB", r1 >= r0 + 64 * PAGE, "KB ", (long long) ((r1 - r0) >> 10));
	kapi_vm_unmap ((u64) a, 64 * PAGE);
	u64 r2 = resident ();
	check_n ("vm_unmap returns the frames", r2 <= r0 + 2 * PAGE, "KB above the start ", (long long) (r2 - r0) / 1024);

	// the heap: lazy too
	u64 h0 = resident ();
	char *p = (char *) kapi_sbrk (16 << 20);
	u64 h1 = resident ();
	check_n ("sbrk 16 MB: nothing filled yet", p != (char *) -1 && h1 < h0 + 2 * PAGE, "KB ", (long long) (h1 - h0) / 1024);
	if (p != (char *) -1)
	{
		for (int i = 0; i < 16; i++) p[i * PAGE + 1] = 1;
		u64 h2 = resident ();
		check ("  16 pages touched -> 16 filled", h2 >= h1 + 16 * PAGE && h2 < h1 + 19 * PAGE);
		kapi_sbrk (-(16 << 20));
		u64 h3 = resident ();
		check ("  sbrk back: the pages returned", h3 <= h1 + PAGE);
	}
	struct kapi_vm_stats S;
	check ("vm_stats: lazy and writable bytes, page tables", kapi_vm_stats (0, &S) == 0 && S.lazy >= 0x800000
							       && S.writable > 0 && S.pt_bytes >= PAGE && S.limit == 0);
}

// ---- 3. kapi I/O in place into fresh lazy memory ------------------------------------------------------

#define FILE_SIZE	0x100000

static void test_inplace (void)
{
	long long src = kapi_vm_map (0, FILE_SIZE, RW, 0);
	long long dst = kapi_vm_map (0, FILE_SIZE, RW, 0);
	if (src < 0 || dst < 0) { check ("vm_map 2 x 1 MB", 0); return; }
	unsigned *s = (unsigned *) src;
	for (unsigned i = 0; i < FILE_SIZE / 4; i++) s[i] = i * 2654435761u;
	const char *pPath = "RAM:/memtest.bin";
	if (kapi_save_file (pPath, s, FILE_SIZE) != FILE_SIZE)
	{
		pPath = "SD:/memtest.bin";
		kapi_save_file (pPath, s, FILE_SIZE);
	}
	void *h = kapi_open (pPath);
	unsigned nTotal = 0;
	if (h != 0)
	{
		for (;;)
		{
			int n = kapi_read (h, (char *) dst + nTotal, FILE_SIZE - nTotal);
			if (n <= 0) break;
			nTotal += (unsigned) n;
			if (nTotal >= FILE_SIZE) break;
		}
		kapi_close (h);
	}
	unsigned *d = (unsigned *) dst;
	int bSame = nTotal == FILE_SIZE;
	for (unsigned i = 0; bSame && i < FILE_SIZE / 4; i++) if (d[i] != s[i]) bSame = 0;
	check_n ("kapi read of a 1 MB file into untouched memory", bSame, "bytes ", nTotal);
	kapi_remove (pPath);

	// a string out (get_args: a fault-safe copy) into a fresh page
	long long g = kapi_vm_map (0, PAGE, RW, 0);
	if (g > 0)
	{
		unsigned r0 = region_resident ((u64) g);	// (merged with a neighbour: count the change)
		int n = kapi_get_args ((char *) g, 64);
		check ("get_args into an untouched page", n >= 0 && region_resident ((u64) g) == r0 + 1);
		kapi_vm_unmap ((u64) g, PAGE);
	}
	// a string in (open's path: CUserStr) from a page that was never touched by the app... is
	// zeros: an empty path, which fails cleanly
	long long z = kapi_vm_map (0, PAGE, KAPI_PROT_READ, 0);
	if (z > 0)
	{
		check ("open (a path in an untouched READ page: \"\") fails cleanly", kapi_open ((const char *) z) == 0);
		kapi_vm_unmap ((u64) z, PAGE);
	}
	long long pn = kapi_vm_map (0, PAGE, KAPI_PROT_NONE, 0);
	if (pn > 0)
	{
		check ("random into a PROT_NONE page -> refused", kapi_random ((void *) pn, 16) == 0);
		kapi_vm_unmap ((u64) pn, PAGE);
	}
	kapi_vm_unmap ((u64) src, FILE_SIZE);
	kapi_vm_unmap ((u64) dst, FILE_SIZE);
}

// ---- 4. threads ------------------------------------------------------------------------------------------

#define SAME_THREADS	4
#define SAME_PAGES	32
static volatile long long s_llShared;

static int same_page_thread (void *pArg)
{
	unsigned me = (unsigned) (unsigned long) pArg;
	volatile unsigned *p = (volatile unsigned *) s_llShared;
	for (unsigned pg = 0; pg < SAME_PAGES; pg++)
	{
		p[pg * (PAGE / 4) + me] = 0x1000 + me + pg;
		if ((pg & 3) == 0) kapi_yield ();
	}
	return 0;
}

static volatile unsigned *s_pWord;
static volatile int s_nWaitResult = 99;

static int word_thread (void *pArg)
{
	(void) pArg;
	s_nWaitResult = kapi_wait_word (s_pWord, 0, 5000);	// (the page untouched: 0)
	return 0;
}

static volatile long long s_llReadBuf;
static void *s_pPipe;
static volatile int s_nReadResult = 99;

static int reader_thread (void *pArg)
{
	(void) pArg;
	s_nReadResult = kapi_stream_read (s_pPipe, (void *) s_llReadBuf, 0x20000);
	return 0;		// (the buffer is gone now: not touched)
}

static volatile unsigned long s_ulTlsBad;

static int tls_thread (void *pArg)
{
	unsigned long v = (unsigned long) pArg;
	set_tp (v);
	for (int i = 0; i < 1000; i++)
	{
		if (i % 50 == 0) kapi_msleep (1); else kapi_yield ();
		if (get_tp () != v) s_ulTlsBad++;
	}
	return 0;
}

static volatile unsigned long s_ulExTp;
static volatile int s_nExInfo = 99, s_nExInside, s_nExGuard;

static int ex_thread (void *pArg)
{
	(void) pArg;
	s_ulExTp = get_tp ();
	struct kapi_thread_info I;
	int Local = 1;
	s_nExInfo = kapi_thread_info (0, &I);
	u64 a = (u64) (unsigned long) &Local;
	s_nExInside = a >= I.stack_lo && a < I.stack_hi && I.stack_hi - I.stack_lo == 0x100000 && I.tid >= 2;
	s_nExGuard = I.guard >= PAGE;
	return Local;
}

static int detached_thread (void *pArg)
{
	(void) pArg;
	return 5;
}

static void test_threads (void)
{
	// threads faulting the same pages
	long long a = kapi_vm_map (0, SAME_PAGES * PAGE, RW, 0);
	if (a > 0)
	{
		s_llShared = a;
		int t[SAME_THREADS];
		for (unsigned i = 0; i < SAME_THREADS; i++) t[i] = kapi_thread_create (same_page_thread, (void *) (unsigned long) i, 0, "same");
		for (unsigned i = 0; i < SAME_THREADS; i++) if (t[i] > 0) kapi_thread_join (t[i], 5000, 0);
		int bOK = region_resident ((u64) a) == SAME_PAGES;
		volatile unsigned *p = (volatile unsigned *) a;
		for (unsigned pg = 0; pg < SAME_PAGES; pg++)
			for (unsigned i = 0; i < SAME_THREADS; i++)
				if (p[pg * (PAGE / 4) + i] != 0x1000 + i + pg) bOK = 0;
		check ("4 threads fault the same 32 pages: each filled once, every write kept", bOK);
		kapi_vm_unmap ((u64) a, SAME_PAGES * PAGE);
	}

	// a futex on a lazy page
	long long w = kapi_vm_map (0, PAGE, RW, 0);
	if (w > 0)
	{
		s_pWord = (volatile unsigned *) (w + 128);
		s_nWaitResult = 99;
		int t = kapi_thread_create (word_thread, 0, 0, "word");
		kapi_msleep (50);
		*s_pWord = 1;
		kapi_wake_word (s_pWord);
		kapi_thread_join (t, 5000, 0);
		check ("wait_word on an untouched page, woken", s_nWaitResult == 0);

		// a waiter on a word whose page is unmapped meanwhile: woken (the frame leaves)
		*s_pWord = 0;
		s_nWaitResult = 99;
		t = kapi_thread_create (word_thread, 0, 0, "word");
		kapi_msleep (50);
		unsigned t0 = kapi_get_ticks ();
		kapi_vm_unmap ((u64) w, PAGE);
		kapi_thread_join (t, 5000, 0);
		check_n ("vm_unmap of a page a thread waits on: the waiter woken", s_nWaitResult == 0,
			 "ms ", (long long) (kapi_get_ticks () - t0) * 10);
	}

	// unmap while another thread blocks in a read into that memory (the deferred zap)
	s_pPipe = kapi_pipe ();
	long long rb = kapi_vm_map (0, 0x20000, RW, 0);
	if (s_pPipe != 0 && rb > 0)
	{
		s_llReadBuf = rb;
		s_nReadResult = 99;
		int t = kapi_thread_create (reader_thread, 0, 0, "reader");
		kapi_msleep (50);				// (it blocks in the read, the buffer pinned)
		int u = kapi_vm_unmap ((u64) rb, 0x20000);
		struct kapi_vm_region R;
		int bGone = kapi_vm_query ((u64) rb, &R) != 0 || R.start != (u64) rb;
		u64 r0 = resident ();
		kapi_stream_write (s_pPipe, "0123456789", 10);
		kapi_thread_join (t, 5000, 0);
		u64 r1 = resident ();
		check ("vm_unmap of a buffer a blocked read is filling -> 0, the region gone at once", u == 0 && bGone);
		check_n ("  the read completes into it (no panic), the frames freed after", s_nReadResult == 10 && r1 < r0,
			 "read ", s_nReadResult);
		kapi_stream_close (s_pPipe);
	}

	// TLS: TPIDR_EL0 per thread
	s_ulTlsBad = 0;
	set_tp (0x7777000);
	int t1 = kapi_thread_create (tls_thread, (void *) 0x1111000UL, 0, "tls1");
	int t2 = kapi_thread_create (tls_thread, (void *) 0x2222000UL, 0, "tls2");
	for (int i = 0; i < 300; i++)
	{
		if (i % 30 == 0) kapi_msleep (1); else kapi_yield ();
		if (get_tp () != 0x7777000) s_ulTlsBad++;
	}
	kapi_thread_join (t1, 10000, 0);
	kapi_thread_join (t2, 10000, 0);
	check_n ("TPIDR_EL0 kept per thread over 1000 sleeps / yields", s_ulTlsBad == 0 && t1 > 0 && t2 > 0,
		 "wrong ", (long long) s_ulTlsBad);

	// thread_create_ex: its TLS, its stack, thread_info
	struct kapi_thread_attr A;
	kapi_memset (&A, 0, sizeof A);
	A.fn = (unsigned long long) (unsigned long) ex_thread;
	A.stack_size = 0x100000;
	A.tls = 0x123450000ULL;
	A.name = "ex";
	int te = kapi_thread_create_ex (&A);
	int nCode = -1;
	if (te > 0) kapi_thread_join (te, 5000, &nCode);
	check ("thread_create_ex -> a tid, joined", te >= 2 && nCode == 1);
	check ("  it starts with TPIDR_EL0 = attr.tls", s_ulExTp == 0x123450000ULL);
	check ("  thread_info (0): a local inside its 1 MB stack, a guard below", s_nExInfo == 0 && s_nExInside && s_nExGuard);
	struct kapi_thread_info I;
	int Local = 0;
	u64 l = (u64) (unsigned long) &Local;
	check ("thread_info (1): the main stack holds a local", kapi_thread_info (1, &I) == 0
							       && l >= I.stack_lo && l < I.stack_hi && I.stack_hi == (16ULL << 30));
	check ("thread_info (no such tid) -> -ESRCH", kapi_thread_info (999, &I) == -KAPI_ESRCH);
	A.flags = 1;
	A.reserved[0] = 1;
	check ("thread_create_ex (reserved != 0) -> -EINVAL", kapi_thread_create_ex (&A) == -KAPI_EINVAL);
	A.reserved[0] = 0;
	A.stack_size = 0x1000;
	check ("thread_create_ex (a 4 KB stack) -> -EINVAL", kapi_thread_create_ex (&A) == -KAPI_EINVAL);
	A.stack_size = 0;
	A.fn = (unsigned long long) (unsigned long) detached_thread;
	int td = kapi_thread_create_ex (&A);
	kapi_msleep (30);
	check ("a detached thread: not joinable once ended", td >= 2 && kapi_thread_join (td, 0, 0) == -2);
	check ("thread_create_ex (bad pointer) -> -EFAULT", kapi_thread_create_ex ((const struct kapi_thread_attr *) 0x80000) == -KAPI_EFAULT);
}

// ---- 5. app cores ----------------------------------------------------------------------------------------

static unsigned char s_Stack[64 * 1024] __attribute__ ((aligned (16)));

struct core_job
{
	volatile long long buf;
	volatile unsigned pages;
	volatile unsigned long tp;
	volatile unsigned done;
};
static struct core_job s_Job;

static void core_job (void *pArg)
{
	struct core_job *j = (struct core_job *) pArg;
	j->tp = get_tp ();
	for (unsigned i = 0; i < j->pages; i++) ((volatile unsigned *) (j->buf + (long long) i * PAGE))[3] = 0xC0DE + i;
	j->done = 1;
}

static void test_appcore (void)
{
	int core = kapi_core_acquire ();
	if (core < 0) { ax_putln ("SKIP  app cores: none free"); return; }
	long long a = kapi_vm_map (0, 64 * PAGE, RW, 0);
	if (a > 0)
	{
		s_Job.buf = a; s_Job.pages = 64; s_Job.tp = 0; s_Job.done = 0;
		set_tp (0xABC0000);
		unsigned t0 = kapi_get_ticks ();
		int r = kapi_core_run (core, core_job, &s_Job, s_Stack + sizeof s_Stack);
		while (r == 0 && kapi_core_state (core) == KAPI_CORE_RUNNING && kapi_get_ticks () - t0 < 1000) kapi_msleep (2);
		unsigned ms = (kapi_get_ticks () - t0) * 10;
		int bOK = r == 0 && s_Job.done && kapi_core_state (core) == KAPI_CORE_IDLE;
		for (unsigned i = 0; bOK && i < 64; i++) if (((volatile unsigned *) (a + (long long) i * PAGE))[3] != 0xC0DE + i) bOK = 0;
		check_n ("an app-core job writes 4 MB of unfilled memory (the pager)", bOK, "ms ", ms);
		check ("  the job saw its caller's TPIDR_EL0", s_Job.tp == 0xABC0000);
		kapi_vm_unmap ((u64) a, 64 * PAGE);

		// a job touching an address no region holds: CORE_FAULT, not a hang
		s_Job.buf = (long long) (50ULL << 30); s_Job.pages = 1; s_Job.done = 0;
		t0 = kapi_get_ticks ();
		r = kapi_core_run (core, core_job, &s_Job, s_Stack + sizeof s_Stack);
		while (r == 0 && kapi_core_state (core) == KAPI_CORE_RUNNING && kapi_get_ticks () - t0 < 500) kapi_msleep (2);
		check ("  a job touching no region: CORE_FAULT", kapi_core_state (core) == KAPI_CORE_FAULT && !s_Job.done);
	}
	// the heap is eager now: sbrk'd memory filled at once
	u64 r0 = resident ();
	char *p = (char *) kapi_sbrk (8 * PAGE);
	u64 r1 = resident ();
	check ("with an app core held, sbrk fills the heap at once", p != (char *) -1 && r1 >= r0 + 8 * PAGE);
	if (p != (char *) -1) kapi_sbrk (-(long) (8 * PAGE));
	kapi_core_release (core);
}

// ---- 6. the network (memtest net) --------------------------------------------------------------------------

static volatile int s_nAccepted = -99;
static int s_nListen;

static int accept_thread (void *pArg)
{
	(void) pArg;
	char Ip[32];
	s_nAccepted = kapi_tcp_accept (s_nListen, Ip, sizeof Ip);
	return 0;
}

static void test_net (void)
{
	char Ip[32];
	if (!kapi_net_status (Ip, sizeof Ip)) { check ("memtest net: the network is up", 0); return; }
	s_nListen = kapi_tcp_listen (7519);
	if (s_nListen < 0) { check ("tcp_listen 7519", 0); return; }
	int t = kapi_thread_create (accept_thread, 0, 0, "accept");
	int c = kapi_tcp_connect (Ip, 7519);
	kapi_thread_join (t, 5000, 0);
	if (c < 0 || s_nAccepted < 0) { check ("connect to this Pi's own address", 0); return; }
	static char Msg[] = "hello, lazy memory";
	kapi_tcp_send (c, Msg, sizeof Msg);
	long long b = kapi_vm_map (0, 4 * PAGE, RW, 0);
	int n = 0;
	for (int i = 0; i < 200 && n == 0 && b > 0; i++)
	{
		n = kapi_tcp_recv (s_nAccepted, (void *) b, 4 * PAGE);
		if (n == 0) kapi_msleep (10);
	}
	check ("tcp_recv into untouched memory", n == (int) sizeof Msg && ((volatile char *) b)[7] == 'l');
	kapi_tcp_close (c);
	kapi_tcp_close (s_nAccepted);
	kapi_tcp_close (s_nListen);
	if (b > 0) kapi_vm_unmap ((u64) b, 4 * PAGE);
}

// ---- main -------------------------------------------------------------------------------------------------

int main (void)
{
	char Args[64];
	kapi_get_args (Args, sizeof Args);
	if (Args[0] == 'c' && Args[1] == 'h' && Args[2] == 'i' && Args[3] == 'l' && Args[4] == 'd' && Args[5] == ' ')
	{
		kapi_exit (child (Args + 6));
	}
	if (kapi_abi_version () < 75 || kapi_vm_map (0, PAGE, RW, 0) == -KAPI_ENOSYS)
	{
		ax_putln ("memtest: this kernel has no demand paging (kapi v75 WP-MEM)");
		ax_putln ("memtest: FAIL (1)");
		kapi_exit (1);
	}

	test_regions ();
	test_stats ();
	test_inplace ();
	test_threads ();

	check_killed ("a write to a READ page kills the process (FAULT)", "ro", -11, KAPI_PROC_FAULT);
	check_killed ("a PROT_NONE read kills the process (FAULT)", "none", -11, KAPI_PROC_FAULT);
	check_killed ("an access to an unmapped page kills the process (FAULT)", "unmapped", -11, KAPI_PROC_FAULT);
	check_killed ("a thread's stack overflow kills the process cleanly (FAULT)", "tstack", -11, KAPI_PROC_FAULT);
	check_killed ("the main thread's stack overflow kills the process cleanly (FAULT)", "mstack", -11, KAPI_PROC_FAULT);

	test_appcore ();

	// A child that touches a lot of memory and exits normally gives it all back (the frames, its
	// page tables): 256 MB, or a quarter of the free memory if less (512 MB with "memtest oom").
	{
		unsigned long f0 = free_kb ();
		unsigned long nMB = ax_streq (Args, "oom") ? 512 : 256;
		if (nMB > f0 / 1024 / 4) nMB = f0 / 1024 / 4;
		char What[24] = "touch ";
		char Num[12]; int k = ax_itoa ((int) nMB, Num); Num[k] = 0;
		for (int i = 0; Num[i]; i++) What[6 + i] = Num[i];
		What[6 + k] = 0;
		int nWhy;
		int st = run_child (What, &nWhy);
		check_n ("a child touching memory then exiting normally: status 0", st == 0, "MB ", (long long) nMB);
		check_back ("  its memory is all back", f0);
	}

	if (ax_streq (Args, "net")) test_net ();
	if (ax_streq (Args, "oom"))
	{
		// Twice: the second run must start and end where the first did (a page allocator's
		// counters left short by a kill showed up here as ~1.1 GB "lost" each time).
		unsigned long f00 = free_kb ();
		for (int nRun = 1; nRun <= 2; nRun++)
		{
			unsigned long f0 = free_kb ();
			check_killed (nRun == 1 ? "a child touching pages until none is left: killed (OOM, -9)"
						: "again: killed (OOM, -9)", "oom", -9, KAPI_PROC_OOM);
			check_back ("  the system lives on, the memory is back", f0);
		}
		long long g = kapi_vm_map (0, PAGE, RW, 0);
		check_n ("after two OOM kills: where it started, memory still mapped", g > 0 && free_kb () + 8192 >= f00,
			 "KB free at the start ", (long long) f00);
		if (g > 0) kapi_vm_unmap ((u64) g, PAGE);
	}
	else
	{
		ax_putln ("(the real OOM kill: memtest oom -- run it alone)");
	}

	ax_puts ("memtest: ");
	if (s_nFail == 0)
	{
		ax_puts ("PASS (");
		put_i (s_nPass);
		ax_putln (" checks)");
	}
	else
	{
		ax_puts ("FAIL (");
		put_i (s_nFail);
		ax_putln (")");
	}
	return s_nFail == 0 ? 0 : 1;
}
