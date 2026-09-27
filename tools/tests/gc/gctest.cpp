// gctest.cpp -- host tests of the GameCube core (user/gc).
//   gctest cpu <cputest.elf> <expected.bin>   runs cputest.c's run_tests (linked at 0x80003100)
//       in the interpreter and compares its output with qemu-ppc's (run_gc_test.sh)
//   gctest ps <pstest.elf>                    the paired singles against the manual's results
#include "gc/gc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
using namespace gc;

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
	while (m.pc != 0x80001000 && !m.halted && m.cycles < 400000000ull) m.step ();
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
	while (m.pc != 0x80001000 && !m.halted && m.cycles < 1000000) m.step ();
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

int main (int argc, char **argv)
{
	if (argc >= 3 && !strcmp (argv[1], "ps")) return psTest (argv[2]);
	if (argc >= 4 && !strcmp (argv[1], "cpu")) return cpuTest (argv[2], argv[3]);
	fprintf (stderr, "gctest cpu <cputest.elf> <expected.bin>\n");
	return 2;
}
