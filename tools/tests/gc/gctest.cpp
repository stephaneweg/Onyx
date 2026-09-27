// gctest.cpp -- host tests of the GameCube core (user/gc).
//   gctest cpu <cputest.elf> <expected.bin>   runs cputest.c's run_tests (linked at 0x80003100)
//       in the interpreter and compares its output with qemu-ppc's (run_gc_test.sh)
//   gctest ps <pstest.elf>                    the paired singles against the manual's results
//   gctest bench <file.elf> [entry]           a function's speed (bench.c: run_bench, run_fbench)
//   gctest dol <file.dol> <fields> [out.ppm]   runs a program: its picture, its results at 0x80700000
// GC_JIT=1: the JIT runs the CPU (an AArch64 host: run_gc_test.sh builds it for qemu-aarch64)
#include "gc/gc.h"
#include "basic/bas3d.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(__aarch64__)
#include <sys/mman.h>
#endif
using namespace gc;

static bool useJit;
static void *hostCode (u32 size)
{
#if defined(__aarch64__)
	void *p = mmap (0, size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	return p == MAP_FAILED ? 0 : p;
#else
	(void) size; return 0;
#endif
}
static void startJit (Machine &m)
{
	if (!useJit) return;
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
	return 0;
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
	if (argc >= 3 && !strcmp (argv[1], "ps")) return psTest (argv[2]);
	if (argc >= 3 && !strcmp (argv[1], "bench")) return benchTest (argv[2], argc > 3 ? argv[3] : 0);
	if (argc >= 4 && !strcmp (argv[1], "cpu")) return cpuTest (argv[2], argv[3]);
	fprintf (stderr, "gctest cpu <cputest.elf> <expected.bin>\n");
	return 2;
}
