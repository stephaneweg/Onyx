// gctest.cpp -- host tests of the GameCube core (user/gc).
//   gctest cpu <cputest.elf> <expected.bin>   runs cputest.c's run_tests (linked at 0x80003100)
//       in the interpreter and compares its output with qemu-ppc's (run_gc_test.sh)
//   gctest ps <pstest.elf>                    the paired singles against the manual's results
//   gctest bench <file.elf> [entry]           a function's speed (bench.c: run_bench, run_fbench)
//   gctest fuzz [seed] [count] [length]      the JIT against the interpreter: random sequences
//       (GC_FUZZC=1: their data through the uncached mirror, 0xC0000000; GC_FUZZSTART=n: from the n-th;
//       GC_FUZZDUMP=file: each one's code written there; GC_FUZZPROG=file: that code instead, its first
//       GC_FUZZLEN instructions -- a failing one cut down; GC_FUZZ2=1: each one run again, the JIT's
//       blocks kept, its data through the uncached mirror -- the blocks' base pointers made for the
//       other one meet another value)
//   gctest dol <file.dol> <fields> [out.ppm]   runs a program: its picture, its results at 0x80700000
//   gctest jitsize <jitprof.bin>              the hot blocks of a Pi's profile (gcemu --jitprof)
//       translated again by this JIT: the host instructions of their main code a guest one,
//       weighted by the runs counted there (the GPRs pointing into MEM1, the GQRs 0);
//       GC_JITDUMP=pc: that block's main code into jitdump.bin
// GC_JIT=1: the JIT runs the CPU (an AArch64 host -- run_gc_test.sh builds it for qemu-aarch64 --
// or an x86-64 one)
#include "gc/gc.h"
#include "basic/bas3d.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vector>
#if defined(_WIN32)
#include <windows.h>
#elif defined(__aarch64__) || defined(__x86_64__)
#include <sys/mman.h>
#endif
using namespace gc;

static bool useJit;
static void *hostCode (u32 size)
{
#if defined(_WIN32)
	return VirtualAlloc (0, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
#elif defined(__aarch64__) || defined(__x86_64__)
	void *p = mmap (0, size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	return p == MAP_FAILED ? 0 : p;
#else
	(void) size; return 0;
#endif
}
static void startJit (Machine &m)
{
	if (!useJit) return;
	m.jitProfile = getenv ("GC_PROFILE") != 0;
	if (!m.jitEnable ()) { printf ("FAIL: no JIT on this host\n"); exit (1); }
}
// run a function called with lr = 0x80001000 ("b ." there) until it returns
static void runToReturn (Machine &m, u64 limit)
{
	m.write32 (0x80001000, 0x48000000);
	if (!useJit) { while (m.pc != 0x80001000 && !m.halted && m.cycles < limit) m.step (); return; }
	while (m.pc != 0x80001000 && !m.halted && m.cycles < limit) m.run (m.cycles + 20000);
}

static unsigned char *slurp (const char *p, long *n)
{
	FILE *f = fopen (p, "rb"); if (!f) { perror (p); exit (1); }
	fseek (f, 0, SEEK_END); *n = ftell (f); fseek (f, 0, SEEK_SET);
	unsigned char *b = (unsigned char *) malloc ((size_t) *n + 1);
	if (fread (b, 1, (size_t) *n, f) != (size_t) *n) exit (1);
	fclose (f); return b;
}
static u32 be32 (const unsigned char *p) { return (u32) p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static u32 be16 (const unsigned char *p) { return (u32) p[0] << 8 | p[1]; }

// an ELF32 big-endian executable: its PT_LOAD segments into MEM1 (bss zeroed), -> the entry
static u32 loadElf (Machine &m, const unsigned char *e)
{
	u32 phoff = be32 (e + 28), phn = be16 (e + 44), phsz = be16 (e + 42);
	for (u32 i = 0; i < phn; i++)
	{
		const unsigned char *ph = e + phoff + i * phsz;
		if (be32 (ph) != 1) continue;
		u32 off = be32 (ph + 4), va = be32 (ph + 8), fsz = be32 (ph + 16), msz = be32 (ph + 20);
		for (u32 k = 0; k < msz; k++) m.mem1[(va + k) & 0x01FFFFFF] = k < fsz ? e[off + k] : 0;
	}
	return be32 (e + 24);
}

static int cpuTest (const char *elf, const char *exp)
{
	long n, en;
	unsigned char *e = slurp (elf, &n), *x = slurp (exp, &en);
	static Machine m;
	u32 entry = loadElf (m, e);
	m.pc = entry; m.gpr[1] = 0x80400000; m.gpr[3] = 0x80500000; m.lr = 0x80001000;
	startJit (m);
	runToReturn (m, 400000000ull);
	if (m.halted) { printf ("FAIL halted: %s\n", m.haltMsg); return 1; }
	if (m.pc != 0x80001000) { printf ("FAIL did not return (pc %08X)\n", m.pc); return 1; }
	u32 words = m.gpr[3];
	if ((long) words * 4 != en) printf ("FAIL %u words, qemu wrote %ld\n", words, en / 4);
	int bad = 0; u32 tag = 0;
	for (u32 i = 0; i < words && (long) i * 4 < en; i++)
	{
		u32 got = be32 (m.mem1 + 0x500000 + i * 4), want = be32 (x + i * 4);
		if ((want >> 16) == 0xA5A5) tag = want & 0xFFFF;
		if (got != want)
		{
			if (bad < 40) printf ("  line %u, word %u: got %08X, qemu %08X\n", tag, i, got, want);
			bad++;
		}
	}
	printf ("%s: %u words, %d differ\n", bad ? "FAIL" : "ok  ", words, bad);
	return bad != 0;
}

// the paired singles (pstest.S): the results the manual gives
static int psTest (const char *elf)
{
	long n; unsigned char *e = slurp (elf, &n);
	static Machine m;
	m.pc = loadElf (m, e); m.gpr[1] = 0x80400000; m.gpr[3] = 0x80500000; m.lr = 0x80001000;
	startJit (m);
	runToReturn (m, 1000000);
	if (m.halted) { printf ("FAIL halted: %s\n", m.haltMsg); return 1; }
	static const u32 want[15] = { 0x40600000, 0x40300000, 0x0018FFFA, 0x40D00000, 0x40980000, 0xBE800000, 0x3FC00000, 0x40980000,
		0x40400000, 0x3FC00000, 0x40900000, 0xBF400000, 0xBFC00000, 0x3E800000, 0x00400000 };
	static const char *what[15] = { "ps_add.0", "ps_add.1", "psq_st s16*8", "psq_st W", "ps_sum0.0", "ps_sum0.1", "ps_sum1.0", "ps_sum1.1",
		"ps_merge10.0", "ps_merge10.1", "ps_muls1.0", "ps_muls1.1", "ps_neg.0", "ps_neg.1", "ps_cmpo0 cr2" };
	int bad = 0;
	for (int i = 0; i < 15; i++)
	{
		u32 got = be32 (m.mem1 + 0x500000 + i * 4);
		if (got != want[i]) { printf ("  %s: got %08X, want %08X\n", what[i], got, want[i]); bad++; }
	}
	printf ("%s: paired singles, %d of 15 differ\n", bad ? "FAIL" : "ok  ", bad);
	return bad != 0;
}

// a function's speed: run_bench (r3 = where its result goes) -> the result, the cycles, the time
static int benchTest (const char *elf, const char *entry)
{
	long n; unsigned char *e = slurp (elf, &n);
	static Machine m;
	m.pc = loadElf (m, e);
	if (entry) m.pc = (u32) strtoul (entry, 0, 16);
	m.gpr[1] = 0x80400000; m.gpr[3] = 0x80500000; m.lr = 0x80001000;
	startJit (m);
	struct timespec t0, t1; clock_gettime (CLOCK_MONOTONIC, &t0);
	runToReturn (m, 100000000000ull);
	clock_gettime (CLOCK_MONOTONIC, &t1);
	double s = (double) (t1.tv_sec - t0.tv_sec) + (double) (t1.tv_nsec - t0.tv_nsec) * 1e-9;
	printf ("result %08X, %llu instructions, %.3f s, %.1f M instructions / s%s%s\n", be32 (m.mem1 + 0x500000), m.cycles / 2, s, (double) m.cycles / 2 / s / 1e6,
		m.halted ? " HALTED " : "", m.haltMsg);
	if (useJit) printf ("JIT: %u blocks translated\n", m.jitCompiles);
	if (useJit && m.jitProfile)
	{
		u64 runs, host, guest; m.jitStats (runs, host, guest);
		printf ("profile: %llu block runs (%.1f instructions each), %.2f host instructions / guest instruction (+ ~11 a block: the dispatch)\n",
			runs, (double) guest / (double) runs, (double) host / (double) guest);
		// GC_DUMP=prefix: the 3 most run blocks' code (prefix.N.bin, for objdump -b binary -m aarch64)
		for (int k = 0; k < 3 && getenv ("GC_DUMP"); k++)
		{
			u32 pc, words; u64 r; const u32 *code;
			if (!m.jitHot (k, pc, r, code, words)) break;
			char path[256]; snprintf (path, sizeof path, "%s.%d.bin", getenv ("GC_DUMP"), k);
			FILE *f = fopen (path, "wb"); if (f) { fwrite (code, 4, words, f); fclose (f); }
			printf ("  hot %d: %08X, %llu runs, %u words -> %s\n", k, pc, r, words, path);
		}
	}
	return 0;
}


// ---- the JIT against the interpreter: random instruction sequences (the FPU, the paired singles,
// the loads / stores, the integer unit), the same start, the whole state compared at the end
static u64 rng = 1;
static u32 rnd32 () { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (u32) rng; }
static u32 rn (u32 n) { return rnd32 () % n; }
static u64 fpVal ()
{
	static const u64 special[] = { 0, 0x8000000000000000ull, 0x3FF0000000000000ull, 0xBFF0000000000000ull, 0x3FE0000000000000ull,
		0x7FF0000000000000ull, 0xFFF0000000000000ull, 0x7FF8000000000000ull, 0x7FF4000000000000ull, 0x3810000000000000ull,
		0x36A0000000000000ull, 0x47EFFFFFE0000000ull, 0x4415AF1D78B58C40ull, 0xC0091EB851EB851Full, 0x0000000000000001ull };
	switch (rn (6))
	{
	case 0: return special[rn (sizeof special / 8)];
	case 1: case 2: { float f = (float) ((int) rn (2000) - 1000) / (float) (1 + rn (64)); double d = f; u64 u; memcpy (&u, &d, 8); return u; }	// a single
	case 3: { u32 w = rnd32 (); if ((w & 0x7F800000) == 0x7F800000 && rn (4)) w &= ~0x40000000u; float f; memcpy (&f, &w, 4); double d = f; u64 u; memcpy (&u, &d, 8); return u; }
	default: { double d = (double) ((int) rn (100000) - 50000) / (double) (1 + rn (1000)) * 1.0000001; u64 u; memcpy (&u, &d, 8); return u; }
	}
}
static void fuzzInit (Machine &m, u64 seed, u32 *prog, int n, bool again = false)
{
	if (!again) m.reset ();						// (again: the JIT's blocks kept)
	else { m.halted = false; m.srr0 = 0; m.cycles = 0; m.tbBase = 0; }	// (mftb as in the first run: the JIT's idle loop at the end ran to its time slice's end)
	rng = seed;
	static const u32 edge[] = { 0, 0xFFFFFFFFu, 0x80000000u, 0x7FFFFFFFu, 1, 0x8000u, 0xFFFF8000u };
	for (int i = 0; i < 32; i++) m.gpr[i] = rn (3) ? rnd32 () : rn (2) ? rn (100) : edge[rn (7)];
	m.gpr[1] = getenv ("GC_FUZZC") || again ? 0xC0400000 : 0x80400000;	// (the data -- GC_FUZZC: through the uncached mirror)
	m.gpr[2] = 0xCC008000;					// (the write-gather pipe)
	for (int i = 0; i < 32; i++) for (int k = 0; k < 2; k++) { u64 u = fpVal (); memcpy (&m.ps[i][k], &u, 8); }
	m.cr = rnd32 (); m.xer = rnd32 () & 0xE000007F; m.lr = rnd32 (); m.ctr = rnd32 ();
	m.gqr[0] = 0; m.gqr[1] = 0x03040304; m.gqr[2] = 0x3E073E07; m.gqr[3] = 0x00050005;
	for (u32 i = 0; i < 0x400; i += 4)
	{
		u32 w = rnd32 ();
		if (!rn (3)) { float f = (float) ((int) rn (200) - 100) * 0.125f; memcpy (&w, &f, 4); }
		m.write32 (0x80400000 + i, w);
	}
	for (int i = 0; i < n; i++) m.write32 (0x80010000 + (u32) i * 4, prog[i]);
	m.write32 (0x80010000 + (u32) n * 4, 0x4E800020);		// blr
	m.write32 (0x80001000, 0x48000000);				// b .
	m.pc = 0x80010000; m.lr = 0x80001000;
}
static u32 fuzzInsn ()
{
	auto R = [] () { return 3 + rn (29); };				// (r1: the data's base, r2 kept)
	auto F = [] () { return rn (32); };
	u32 d = F (), a = F (), b = F (), c = F ();
	static const u32 x59[] = { 18, 20, 21, 25, 28, 29, 30, 31, 24 };
	static const u32 psA[] = { 10, 11, 12, 13, 14, 15, 18, 20, 21, 23, 25, 28, 29, 30, 31, 24, 26 };
	static const u32 psX[] = { 40, 72, 136, 264, 528, 560, 592, 624 };
	static const u32 f63x[] = { 12, 15, 40, 72, 136, 264 };
	switch (rn (25))
	{
	case 0: case 1: return 59u << 26 | d << 21 | a << 16 | b << 11 | c << 6 | x59[rn (9)] << 1;	// single (+ fres)
	case 2: return 63u << 26 | d << 21 | a << 16 | b << 11 | c << 6 | (rn (9) ? x59[rn (8)] : 26u) << 1;		// double (+ frsqrte)
	case 3: return 63u << 26 | d << 21 | a << 16 | b << 11 | c << 6 | 23u << 1;			// fsel
	case 4: return 63u << 26 | d << 21 | b << 11 | f63x[rn (6)] << 1;
	case 5: return 63u << 26 | rn (8) << 23 | a << 16 | b << 11 | (rn (2) ? 32u : 0u) << 1;	// fcmpu / fcmpo
	case 6: case 7: case 8: return 4u << 26 | d << 21 | a << 16 | b << 11 | c << 6 | psA[rn (17)] << 1;
	case 9: return 4u << 26 | d << 21 | a << 16 | b << 11 | psX[rn (8)] << 1;
	case 10: return (48u + rn (4) * 2) << 26 | d << 21 | 1u << 16 | (rn (0x80) * 8);		// lfs lfd stfs stfd
	case 11: if (rn (3) == 0) return 4u << 26 | d << 21 | 1u << 16 | 4u << 11 | (u32) rn (2) << 10 | rn (4) << 7 | (rn (2) ? 6u : 7u) << 1;	// psq_lx / psq_stx (r1 + r4)
		return (rn (2) ? 56u : 60u) << 26 | d << 21 | 1u << 16 | (u32) rn (2) << 15 | rn (4) << 12 | (rn (0x40) * 8);	// psq_l / psq_st
	case 12: if (rn (2)) return 31u << 26 | R () << 21 | (12u + rn (2)) << 16 | 8u << 11 | 371u << 1;	// mftb (TBL / TBU)
		return 63u << 26 | d << 21 | 583u << 1;						// mffs (FPRF)
	case 13: return 31u << 26 | d << 21 | 1u << 16 | 4u << 11 | 983u << 1;			// stfiwx (r4 = 0x100 below)
	case 14: { static const u32 xo[] = { 266, 10, 138, 234, 202, 40, 8, 136, 232, 200, 104, 235, 75, 11, 459 };
		return 31u << 26 | R () << 21 | R () << 16 | R () << 11 | xo[rn (15)] << 1 | rn (2); }
	case 15: { static const u32 xo[] = { 28, 60, 444, 412, 316, 476, 124, 284, 24, 536, 792, 824, 26, 954, 922 };
		return 31u << 26 | R () << 21 | R () << 16 | R () << 11 | xo[rn (15)] << 1 | rn (2); }
	case 16: return (20u + rn (2)) << 26 | R () << 21 | R () << 16 | rn (32) << 11 | rn (32) << 6 | rn (32) << 1 | rn (2);	// rlwimi / rlwinm
	case 17: return (10u + rn (6)) << 26 | R () << 21 | R () << 16 | (rn (2) ? rnd32 () & 0xFFFF : rn (8));	// cmpli cmpi addic addic. addi addis
	case 18: return 31u << 26 | rn (8) << 23 | R () << 16 | R () << 11 | (rn (2) ? 32u : 0u) << 1;	// cmp / cmpl
	case 19: return 19u << 26 | rn (32) << 21 | rn (32) << 16 | rn (32) << 11 | (rn (2) ? 193u : 449u) << 1;	// crxor / cror
	case 20: { static const u32 o[] = { 32, 34, 36, 38, 40, 42, 44 }; return o[rn (7)] << 26 | R () << 21 | 1u << 16 | (rn (0x100) * 4); }	// lwz lbz stw stb lhz lha sth
	case 21: return 31u << 26 | R () << 21 | (rn (2) ? 8u : 9u) << 16 | 467u << 1;		// mtlr / mtctr
	case 22: return 31u << 26 | R () << 21 | (rn (2) ? 8u : 9u) << 16 | 339u << 1;		// mflr / mfctr
	case 23:
		switch (rn (14))						// the rest: branches forward, the interpreter's
		{
		case 0: return 16u << 26 | rn (32) << 21 | rn (32) << 16 | (1 + rn (4)) * 4;		// bc +4..16 (any BO)
		case 1: return 18u << 26 | (1 + rn (3)) * 4;						// b +4..12
		case 2: return 31u << 26 | R () << 21 | (912u + rn (8) - 896u) << 16 | 28u << 11 | 467u << 1;	// mtspr GQRn (-> jitRun)
		case 3: return 31u << 26 | R () << 21 | 1u << 16 | (rn (2) ? 467u : 339u) << 1;	// mtxer / mfxer
		case 4: return 31u << 26 | R () << 21 | 1u << 16 | 4u << 11 | 1014u << 1;		// dcbz (r1 + 0x100)
		case 5: return 31u << 26 | R () << 21 | 1u << 16 | 4u << 11 | (rn (2) ? 20u : 150u) << 1 | 1;	// lwarx / stwcx.
		case 6: return 19u << 26 | rn (8) << 23 | rn (8) << 18;					// mcrf
		case 7: return 31u << 26 | R () << 21 | rnd32 () % 256 << 12 | 144u << 1;		// mtcrf
		case 8: return 31u << 26 | R () << 21 | 19u << 1;					// mfcr
		case 9: return 31u << 26 | R () << 21 | R () << 16 | R () << 11 | 491u << 1 | rn (2);	// divw
		case 10: return 23u << 26 | R () << 21 | R () << 16 | R () << 11 | rn (32) << 6 | rn (32) << 1 | rn (2);	// rlwnm
		case 11: return (rn (2) ? 46u : 47u) << 26 | (24u + rn (8)) << 21 | 1u << 16 | (rn (0x40) * 4);	// lmw / stmw (r24..)
		default:							// to the write-gather pipe (r2)
			switch (rn (5))
			{
			case 0: return 36u << 26 | R () << 21 | 2u << 16;		// stw
			case 1: return 38u << 26 | R () << 21 | 2u << 16;		// stb
			case 2: return 44u << 26 | R () << 21 | 2u << 16;		// sth
			case 3: return 52u << 26 | F () << 21 | 2u << 16;		// stfs
			default: return 60u << 26 | F () << 21 | 2u << 16 | (u32) rn (2) << 15;	// psq_st (GQR0)
			}
		}
	default: return (24u + rn (6)) << 26 | R () << 21 | R () << 16 | (rnd32 () & 0xFFFF);	// ori oris xori xoris andi. andis.
	}
}
static int fuzzTest (u64 seed0, int count, int len)
{
	static Machine A, B;
	if (!B.jitEnable ()) { printf ("FAIL: no JIT on this host\n"); return 1; }
	static u32 prog[1024];
	int bad = 0, skipped = 0;
	int t0 = getenv ("GC_FUZZSTART") ? atoi (getenv ("GC_FUZZSTART")) : 0;
	for (int t = t0; t < t0 + count && bad < 3; t++)
	{
		u64 seed = seed0 * 1000003ull + (u64) t + 1;
		rng = seed ^ 0x9E3779B97F4A7C15ull;
		for (int i = 0; i < len; i++) prog[i] = fuzzInsn ();
		if (getenv ("GC_FUZZPROG"))					// (a sequence from a file: its first GC_FUZZLEN)
		{
			FILE *f = fopen (getenv ("GC_FUZZPROG"), "rb");
			int k = 0; u32 w;
			while (f && k < len && fread (&w, 4, 1, f) == 1) prog[k++] = __builtin_bswap32 (w);
			if (f) fclose (f);
			len = k;
			if (getenv ("GC_FUZZLEN") && atoi (getenv ("GC_FUZZLEN")) < len) len = atoi (getenv ("GC_FUZZLEN"));
		}
		if (getenv ("GC_FUZZV")) { printf ("sequence %d\n", t); fflush (stdout); }
		if (getenv ("GC_FUZZDUMP")) { FILE *f = fopen (getenv ("GC_FUZZDUMP"), "wb"); for (int i = 0; i < len; i++) { u32 w = __builtin_bswap32 (prog[i]); fwrite (&w, 4, 1, f); } fclose (f); }
		for (int pass = 0; pass < (getenv ("GC_FUZZ2") ? 2 : 1); pass++) {
		if (pass && (A.halted || B.halted)) break;
		fuzzInit (A, seed, prog, len, pass > 0); fuzzInit (B, seed, prog, len, pass > 0);
		A.srr0 = B.srr0 = 0;						// (set by an exception only)
		A.gpr[4] = B.gpr[4] = 0x100;
		while (A.pc != 0x80001000 && !A.halted && A.cycles < 1000000) A.step ();
		while (B.pc != 0x80001000 && !B.halted && B.cycles < 1000000) B.run (B.cycles + 1000);
		if (A.pc != 0x80001000 && !A.halted) { skipped++; break; }	// (it loops: stopped at another point)
		if (getenv ("GC_FUZZF"))					// (the FPRs listed: both machines)
		{
			const char *q = getenv ("GC_FUZZF");
			while (*q) { int r = (int) strtol (q, (char **) &q, 10); u64 x, y, x1, y1; memcpy (&x, &A.ps[r][0], 8); memcpy (&y, &B.ps[r][0], 8); memcpy (&x1, &A.ps[r][1], 8); memcpy (&y1, &B.ps[r][1], 8);
				printf ("  f%d: %016llX %016llX | JIT %016llX %016llX\n", r, x, x1, y, y1); while (*q == ' ') q++; }
		}
		if (getenv ("GC_FUZZG"))					// (the GPRs listed, CR, XER: both machines)
		{
			const char *q = getenv ("GC_FUZZG");
			while (*q) { int r = (int) strtol (q, (char **) &q, 10); printf ("  r%d: %08X | JIT %08X\n", r, A.gpr[r], B.gpr[r]); while (*q == ' ') q++; }
			printf ("  cr %08X xer %08X | JIT cr %08X xer %08X\n", A.cr, A.xer, B.cr, B.xer);
		}
		if (getenv ("GC_FUZZBLOCK"))					// (a block's code: GC_FUZZBLOCK=pc -> fzblock.bin)
		{
			const u32 *code; u32 words;
			for (u32 bp = (u32) strtoul (getenv ("GC_FUZZBLOCK"), 0, 16); bp >= 0x80010000; bp -= 4)	// (the block starting there, or before)
				if (B.jitCode (bp, code, words)) { FILE *f = fopen ("fzblock.bin", "wb"); fwrite (code, 4, words, f); fclose (f); printf ("  block %08X: %u words\n", bp, words); break; }
		}
		char diff[256] = "";
		for (int i = 0; i < 32 && !diff[0]; i++) if (A.gpr[i] != B.gpr[i]) snprintf (diff, sizeof diff, "r%d: %08X, JIT %08X", i, A.gpr[i], B.gpr[i]);
		for (int i = 0; i < 32 && !diff[0]; i++) for (int k = 0; k < 2 && !diff[0]; k++)
			if (memcmp (&A.ps[i][k], &B.ps[i][k], 8)) { u64 x, y; memcpy (&x, &A.ps[i][k], 8); memcpy (&y, &B.ps[i][k], 8); snprintf (diff, sizeof diff, "f%d.ps%d: %016llX, JIT %016llX", i, k, x, y); }
		// (CR after an exception that did not come back -- the run stopped at its vector: a field the
		// JIT did not make because the code after would set it again first may differ, as it may
		// on the real thing's crash reports; the JIT leaves it so -- crDead)
		if (!diff[0] && A.cr != B.cr && !(A.halted && A.srr0 != 0)) snprintf (diff, sizeof diff, "cr: %08X, JIT %08X", A.cr, B.cr);
		if (!diff[0] && A.xer != B.xer) snprintf (diff, sizeof diff, "xer: %08X, JIT %08X", A.xer, B.xer);
		if (!diff[0] && (A.lr != B.lr || A.ctr != B.ctr)) snprintf (diff, sizeof diff, "lr / ctr differ");
		if (!diff[0] && A.fpscrNow () != B.fpscrNow ()) snprintf (diff, sizeof diff, "fpscr: %08X, JIT %08X", A.fpscr, B.fpscr);
		for (u32 i = 0; i < 0x400 && !diff[0]; i += 4) if (A.read32 (0x80400000 + i) != B.read32 (0x80400000 + i))
			snprintf (diff, sizeof diff, "memory +%X: %08X, JIT %08X", i, A.read32 (0x80400000 + i), B.read32 (0x80400000 + i));
		if (!diff[0] && (A.gatherN != B.gatherN || memcmp (A.gather, B.gather, A.gatherN) || A.piFifoWptr != B.piFifoWptr || memcmp (A.mem1, B.mem1, 0x3000)))
			snprintf (diff, sizeof diff, "the write-gather pipe: %u bytes, JIT %u (FIFO at %X / %X)", A.gatherN, B.gatherN, A.piFifoWptr, B.piFifoWptr);
		if (!diff[0] && (A.pc != B.pc || A.halted != B.halted)) snprintf (diff, sizeof diff, "pc %08X / %08X (%s)", A.pc, B.pc, A.haltMsg);
		if (diff[0])
		{
			printf ("  sequence %d (seed %llu)%s: %s\n", t, (unsigned long long) seed, pass ? ", run again" : "", diff);
			bad++;
			break;
		}
		}
	}
	printf ("%s: the JIT against the interpreter, %d random sequences of %d instructions%s (%d looping, not compared)\n", bad ? "FAIL" : "ok  ", count, len, bad ? "" : ": identical", skipped);
	return bad != 0;
}

static void ppm (const Machine &m, const char *path)
{
	FILE *f = fopen (path, "wb"); if (!f) return;
	fprintf (f, "P6\n%d %d\n255\n", m.fbW, m.fbH);
	for (int i = 0; i < m.fbW * m.fbH; i++) { u32 c = m.fb[i]; fputc ((int) (c >> 16) & 255, f); fputc ((int) (c >> 8) & 255, f); fputc ((int) c & 255, f); }
	fclose (f);
}

// the last GPU frame, rendered by the BASIC 3D's software renderer (as the GPU would draw it)
static void gfxPpm (Machine &m, const char *path)
{
	if (m.gfxReady < 0) { printf ("  (no GX frame)\n"); return; }
	const GFrame &F = m.gfxFrame[m.gfxReady];
	int W = F.width, H = F.height;
	static unsigned px[1024 * 1024]; static float zb[1024 * 1024];
	static_assert (sizeof (GVertex) == sizeof (bas::G3Vertex) && sizeof (GBatch) == sizeof (bas::G3Batch), "layout");
	static bas::G3Batch bt[GFrame::MAXB];
	for (int i = 0; i < F.nb; i++) { __builtin_memcpy (&bt[i], &F.b[i], sizeof bt[i]); bt[i].texture = F.b[i].tex >= 0 ? F.b[i].tex + 1 : 0; }
	Machine *mp = &m;
	bas::swRender (px, W, H, W, zb, (const bas::G3Vertex *) F.v, F.nv, bt, F.nb, F.clear, false, [mp] (int t) -> const bas::G3Texture *
	{
		static bas::G3Texture T;
		T.px = mp->tex[t - 1].px; T.w = mp->tex[t - 1].w; T.h = mp->tex[t - 1].h;
		return &T;
	});
	FILE *f = fopen (path, "wb"); if (!f) return;
	fprintf (f, "P6\n%d %d\n255\n", W, H);
	for (int i = 0; i < W * H; i++) { unsigned c = px[i]; fputc ((int) (c >> 16), f); fputc ((int) (c >> 8) & 255, f); fputc ((int) c & 255, f); }
	fclose (f);
	printf ("  GX frame: %d vertices, %d batches, %dx%d -> %s\n", F.nv, F.nb, W, H, path);
}

// a .dol run for some fields: the picture, the results it leaves at 0x80700000, the interrupts
static int jitSize (const char *path)
{
	FILE *f = fopen (path, "rb"); if (!f) { printf ("FAIL: %s\n", path); return 1; }
	std::vector<u8> d; { u8 buf[65536]; size_t k; while ((k = fread (buf, 1, sizeof buf, f)) > 0) d.insert (d.end (), buf, buf + k); } fclose (f);
	static Machine B; B.jitProfile = true;
	if (!B.jitEnable ()) { printf ("FAIL: no JIT on this host\n"); return 1; }
	B.reset ();
	for (int i = 1; i < 32; i++) B.gpr[i] = 0x80400000;		// (the base pointers made: as for MEM1's)
	size_t n = 0; int blocks = 0;
	static double cls[65536], cnt[65536]; double host = 0, guest = 0;
	while (n + 16 <= d.size ())
	{
		u32 h[4]; memcpy (h, d.data () + n, 16); n += 16;
		u32 pc = h[0], runs = h[1], gw = h[2], hw = h[3];
		if (n + (size_t) gw * 4 + (size_t) hw * 4 > d.size ()) break;
		for (u32 i = 0; i < gw; i++)				// (the guest code where it was)
		{
			u32 w; memcpy (&w, d.data () + n + i * 4, 4);
			u32 pa = (pc + i * 4) & 0x01FFFFFF;
			if (pa + 4 <= MEM1_SIZE) memcpy (B.mem1 + pa, &w, 4);
		}
		n += (size_t) gw * 4 + (size_t) hw * 4;
		if (!B.jitCompileAt (pc)) continue;
		static u32 ops[256], words[256];
		int k = B.jitBlockInsns (pc, ops, words, 256);
		for (int i = 0; i < k; i++)
		{
			u32 op = ops[i], p = op >> 26;
			u32 c = p == 4 || p == 19 || p == 31 || p == 59 || p == 63 ? (p << 10 | ((op >> 1) & 0x3FF)) : p << 10;
			cls[c] += (double) runs * words[i]; cnt[c] += op ? runs : 0; host += (double) runs * words[i]; guest += op ? runs : 0;
		}
		blocks++;
	}
	printf ("%d blocks: %.0f guest instructions, %.0f host (%.2f a guest one)\n", blocks, guest, host, guest ? host / guest : 0);
	if (getenv ("GC_JITDUMP"))					// (a block's main code -> jitdump.bin)
	{
		const u32 *code; u32 words;
		if (B.jitCode ((u32) strtoul (getenv ("GC_JITDUMP"), 0, 16), code, words)) { FILE *o = fopen ("jitdump.bin", "wb"); fwrite (code, 4, words, o); fclose (o); printf ("  jitdump.bin: %u words\n", words); }
	}
	for (int t = 0; t < 30; t++)
	{
		int best = -1;
		for (int c = 0; c < 65536; c++) if (cls[c] > 0 && (best < 0 || cls[c] > cls[best])) best = c;
		if (best < 0) break;
		printf ("  %2d / %3d: %6.1f %%  %5.1f a guest one\n", best >> 10, best & 0x3FF, 100.0 * cls[best] / host, cnt[best] ? cls[best] / cnt[best] : 0.0);
		cls[best] = 0;
	}
	return 0;
}

static int dolTest (const char *dol, int frames, const char *out)
{
	long n; unsigned char *d = slurp (dol, &n);
	static Machine m;
	if (!m.loadDol (d, (u32) n)) { printf ("FAIL: not a .dol\n"); return 1; }
	startJit (m);
	for (int i = 0; i < frames && !m.halted; i++) m.runFrame ();
	if (out) ppm (m, out);
	if (out && getenv ("GC_GX")) gfxPpm (m, getenv ("GC_GX"));
	printf ("%d fields, pc %08X%s%s, %dx%d, gx %u cmds %u prims\n", m.frames, m.pc, m.halted ? " HALTED: " : "", m.haltMsg, m.fbW, m.fbH, m.gxCmds, m.gxPrims);
	printf ("results: %08X %08X %08X %08X %08X\n", m.read32 (0x80700000), m.read32 (0x80700004), m.read32 (0x80700008), m.read32 (0x8070000C), m.read32 (0x80700010));
	printf ("PI irqs:"); for (int i = 0; i < 14; i++) printf (" %u", m.irqCount[i]); printf ("\n");
	if (useJit) printf ("JIT: %u blocks translated, %u live\n", m.jitCompiles, m.jitBlocks);
	return m.halted;
}

int main (int argc, char **argv)
{
	codeAlloc = hostCode;
	useJit = getenv ("GC_JIT") && atoi (getenv ("GC_JIT"));
	if (argc >= 4 && !strcmp (argv[1], "dol")) return dolTest (argv[2], atoi (argv[3]), argc > 4 ? argv[4] : 0);
	if (argc >= 3 && !strcmp (argv[1], "jitsize")) return jitSize (argv[2]);
	if (argc >= 3 && !strcmp (argv[1], "ps")) return psTest (argv[2]);
	if (argc >= 3 && !strcmp (argv[1], "bench")) return benchTest (argv[2], argc > 3 ? argv[3] : 0);
	if (argc >= 2 && !strcmp (argv[1], "fuzz")) return fuzzTest (argc > 2 ? strtoull (argv[2], 0, 10) : 1, argc > 3 ? atoi (argv[3]) : 2000, argc > 4 ? atoi (argv[4]) : 150);
	if (argc >= 4 && !strcmp (argv[1], "cpu")) return cpuTest (argv[2], argv[3]);
	fprintf (stderr, "gctest cpu <cputest.elf> <expected.bin>\n");
	return 2;
}
