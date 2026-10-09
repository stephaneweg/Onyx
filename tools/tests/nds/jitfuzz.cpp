//
// jitfuzz.cpp -- the JIT against the interpreter (on an AArch64 host, or under qemu-aarch64): random
// sequences of ARM / Thumb instructions (data processing with every operand form and condition, the
// multiplies, CLZ; loads and stores into a scratch area; the Thumb formats), run from the same random
// registers and flags by both; the registers, the flags and the scratch memory must agree.
//
//   jitfuzz [iterations] [seed]
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "nds/nds.h"

using namespace nds;
namespace nds { extern void (*g_jitDumpHook) (const u32 *code, int words, u32 pc); }
static FILE *g_dump;
static void dumpHook (const u32 *code, int words, u32 pc) { if (g_dump) { fprintf (g_dump, "block %08x %d\n", pc, words); fwrite (code, 4, (size_t) words, g_dump); } }

static void *hostCode (unsigned size)
{
	void *p = mmap (0, size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	return p == MAP_FAILED ? 0 : p;
}

static u32 seed = 1;
static u32 rnd () { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; }
static u32 rv ()						// interesting values
{
	switch (rnd () % 8)
	{
	case 0: return 0; case 1: return 0xFFFFFFFF; case 2: return 0x80000000; case 3: return 0x7FFFFFFF;
	case 4: return rnd () & 0xFF; case 5: return 1u << (rnd () & 31);
	default: return rnd ();
	}
}

static const u32 CODE = 0x02000000, DATA = 0x02100000;

static u32 armOp (bool v5)
{
	u32 cond = (rnd () % 4) ? 0xE : rnd () % 15;
	int k = (int) (rnd () % 20);
	u32 rd = rnd () % 15, rn = rnd () % 16, rm = rnd () % 16, rs = rnd () % 15;
	if (rnd () % 4) { if (rn == 15) rn = 3; if (rm == 15) rm = 4; }
	if (k < 12)							// data processing
	{
		u32 opc = rnd () % 16, S = rnd () & 1;
		if (opc >= 8 && opc <= 11) S = 1;
		u32 op = (cond << 28) | (opc << 21) | (S << 20) | (rn << 16) | (rd << 12);
		switch (rnd () % 4)
		{
		case 0: return op | 0x02000000 | ((rnd () % 16) << 8) | (rnd () & 0xFF);
		case 1: return op | ((rnd () % 32) << 7) | ((rnd () % 4) << 5) | rm;
		case 2: return op | ((rnd () % 4) << 5) | rm;
		default: if (rm == 15 || rn == 15) rm = 5, rn = 6; return op | (rs << 8) | ((rnd () % 4) << 5) | 0x10 | rm;
		}
	}
	if (rm == 15) rm = 1; if (rn == 15) rn = 2;
	if (k < 14)							// MUL / MLA
	{
		u32 a = rnd () & 1, S = rnd () & 1;
		if (rd == rm) rd = (rd + 1) % 15;
		return (cond << 28) | (a << 21) | (S << 20) | (rd << 16) | (rn << 12) | (rs << 8) | 0x90 | rm;
	}
	if (k < 16)							// the long multiplies
	{
		u32 hi = rd, lo = rn % 15; if (hi == lo) lo = (lo + 1) % 15;
		return (cond << 28) | 0x00800090 | ((rnd () % 4) << 21) | ((rnd () & 1) << 20) | (hi << 16) | (lo << 12) | (rs << 8) | rm;
	}
	if (k < 17 && v5) return (cond << 28) | 0x016F0F10 | (rd << 12) | rm;	// CLZ
	// a load / store into DATA (r12 holds it, kept)
	{
		u32 base = 12;
		if (rd == 12) rd = 0;
		u32 off = rnd () & 0xFC;
		switch (rnd () % 4)
		{
		case 0: return (cond << 28) | 0x05800000 | ((rnd () & 1) << 20) | ((rnd () & 1) << 22) | (base << 16) | (rd << 12) | off | (rnd () & 3);
		case 1: return (cond << 28) | 0x01C000B0 | ((rnd () & 1) << 20) | (base << 16) | (rd << 12) | ((off & 0xF0) << 4) | (off & 0xE);
		case 2: return (cond << 28) | 0x01D000D0 | (rnd () & 0x20) | (base << 16) | (rd << 12) | (off & 0xF);
		default:
		{
			u32 list = rnd () & 0x0FFF & ~(1u << base);
			if (!list) list = 1;
			return (cond << 28) | 0x08800000 | ((rnd () & 1) << 20) | (base << 16) | list;	// STMIA / LDMIA r12 (no write-back)
		}
		}
	}
}

static u16 thumbOp ()
{
	u32 rd = rnd () & 7, rs = rnd () & 7, rn = rnd () & 7;
	switch (rnd () % 9)
	{
	case 0: return (u16) ((rnd () % 3) << 11 | (rnd () % 32) << 6 | rs << 3 | rd);
	case 1: return (u16) (0x1800 | (rnd () & 3) << 9 | rn << 6 | rs << 3 | rd);
	case 2: return (u16) (0x2000 | (rnd () & 3) << 11 | rd << 8 | (rnd () & 0xFF));
	case 3: case 4: return (u16) (0x4000 | (rnd () & 15) << 6 | rs << 3 | rd);
	case 5:
	{
		u32 h = rnd () % 3;					// ADD / CMP / MOV high (not the PC)
		u32 d = rnd () % 15, s = rnd () % 15;
		if (d == 12 || (h != 1 && d == 13)) d = 0;
		return (u16) (0x4400 | h << 8 | (d & 8) << 4 | s << 3 | (d & 7));
	}
	case 6: return (u16) (0xB000 | (rnd () & 0xFF)) & 0xB0FF;	// ADD / SUB sp (kept even)
	default:							// loads / stores, register offset from r7 = DATA (kept)
	{
		if (rd == 7) rd = 0;
		u32 imm = rnd () & 31;
		switch (rnd () % 3)
		{
		case 0: return (u16) (0x6000 | (rnd () & 1) << 11 | imm << 6 | 7 << 3 | rd);
		case 1: return (u16) (0x8000 | (rnd () & 1) << 11 | imm << 6 | 7 << 3 | rd);
		default: return (u16) (0x7000 | (rnd () & 1) << 11 | imm << 6 | 7 << 3 | rd);
		}
	}
	}
}

int main (int argc, char **argv)
{
	int iters = argc > 1 ? atoi (argv[1]) : 2000;
	seed = argc > 2 ? (u32) atoi (argv[2]) : 12345;
	static u8 rom[0x400];
	memset (rom, 0, sizeof rom);
	wr32 (rom + 0x20, 0x200); wr32 (rom + 0x24, CODE); wr32 (rom + 0x28, CODE); wr32 (rom + 0x2C, 4);
	wr32 (rom + 0x30, 0x200); wr32 (rom + 0x34, 0x02200000); wr32 (rom + 0x38, 0x02200000); wr32 (rom + 0x3C, 4);
	wr32 (rom + 0x200, 0xEAFFFFFE);
	Machine *a = new Machine, *b = new Machine;
	a->load (rom, sizeof rom); b->load (rom, sizeof rom);
	if (!b->jitEnable (hostCode)) { printf ("no JIT here\n"); return 1; }
	a->useJit = false;
	int bad = 0;
	struct Case { int cpu; bool thumb; int n; u32 ops[16]; u32 regs[16]; u32 flags; u32 data[64]; };
	auto run = [&] (const Case &t, bool print) -> bool
	{
		Machine *ms[2] = { a, b };
		for (int k = 0; k < 2; k++)
		{
			Machine *m = ms[k];
			Arm &c = t.cpu ? m->arm7 : m->arm9;
			for (int i = 0; i < t.n; i++) { if (t.thumb) wr16 (m->mainRam + i * 2, (u16) t.ops[i]); else wr32 (m->mainRam + i * 4, t.ops[i]); }
			u32 end = CODE + (u32) t.n * (t.thumb ? 2 : 4);
			if (t.thumb) { wr16 (m->mainRam + t.n * 2, 0xE7FE); wr16 (m->mainRam + t.n * 2 + 2, 0xE7FE); }
			else wr32 (m->mainRam + t.n * 4, 0xEAFFFFFE);
			for (int i = 0; i < 64; i++) wr32 (m->mainRam + 0x100000 + i * 4, t.data[i]);
			if (m->jit) jitFlushAll (m);
			if (m->pagesDirty) m->pagesUpdate ();
			for (int i = 0; i < 15; i++) c.r[i] = t.regs[i];
			c.cpsr = 0x1F | t.flags | 0xC0 | (t.thumb ? 0x20 : 0);
			c.r[15] = CODE;
			c.halted = false;
			m->ie[t.cpu] = 0;
			if (k == 0) { for (int g = 0; g < 200 && c.r[15] != end; g++) { if (t.thumb) c.stepThumb (); else c.stepArm (); } }
			else { m->running = t.cpu; for (int g = 0; g < 50 && c.r[15] != end; g++) { c.ts = 0; c.target = 1; jitRun (m, c); } m->running = -1; }
		}
		Arm &x = t.cpu ? a->arm7 : a->arm9, &y = t.cpu ? b->arm7 : b->arm9;
		bool same = (x.cpsr & 0xF00000FF) == (y.cpsr & 0xF00000FF) && x.r[15] == y.r[15];
		for (int i = 0; i < 15; i++) if (x.r[i] != y.r[i]) same = false;
		for (int i = 0; i < 64; i++) if (rd32 (a->mainRam + 0x100000 + i * 4) != rd32 (b->mainRam + 0x100000 + i * 4)) same = false;
		if (!same && print)
		{
			printf ("MISMATCH (%s, %s, %d instructions), flags in %x:\n", t.cpu ? "ARM7" : "ARM9", t.thumb ? "Thumb" : "ARM", t.n, t.flags >> 28);
			for (int i = 0; i < t.n; i++) printf ("  %0*x\n", t.thumb ? 4 : 8, t.ops[i]);
			printf ("  in:  "); for (int i = 0; i < 15; i++) printf ("%08x ", t.regs[i]); printf ("\n");
			printf ("  int: "); for (int i = 0; i < 16; i++) printf ("%08x ", x.r[i]); printf ("cpsr %08x\n", x.cpsr);
			printf ("  jit: "); for (int i = 0; i < 16; i++) printf ("%08x ", y.r[i]); printf ("cpsr %08x\n", y.cpsr);
			for (int i = 0; i < 64; i++) { u32 p = rd32 (a->mainRam + 0x100000 + i * 4), q = rd32 (b->mainRam + 0x100000 + i * 4); if (p != q) printf ("  mem +%02x: int %08x jit %08x\n", i * 4, p, q); }
		}
		return same;
	};
	for (int it = 0; it < iters && bad < 6; it++)
	{
		Case t;
		t.cpu = (int) (rnd () & 1);
		t.thumb = rnd () & 1;
		t.n = 1 + (int) (rnd () % 12);
		for (int i = 0; i < t.n; i++) t.ops[i] = t.thumb ? thumbOp () : armOp (t.cpu == 0);
		t.flags = (rnd () & 0xF) << 28;
		for (int i = 0; i < 15; i++) t.regs[i] = rv ();
		t.regs[t.thumb ? 7 : 12] = DATA;
		t.regs[13] = 0x02180000 + (rnd () & 0xFC);
		for (int i = 0; i < 64; i++) t.data[i] = rnd ();
		if (run (t, false)) continue;
		bad++;
		// smaller: without each instruction in turn, while it still differs
		for (bool again = true; again; )
		{
			again = false;
			for (int i = 0; i < t.n && t.n > 1; i++)
			{
				Case u = t; for (int k = i; k < t.n - 1; k++) u.ops[k] = t.ops[k + 1]; u.n--;
				if (!run (u, false)) { t = u; again = true; break; }
			}
		}
		printf ("#%d ", it);
		if (getenv ("JITDUMP")) { g_dump = fopen (getenv ("JITDUMP"), "wb"); g_jitDumpHook = dumpHook; }
		run (t, true);
		if (g_dump) { fclose (g_dump); g_dump = 0; g_jitDumpHook = 0; return 1; }
	}
	printf ("%s\n", bad ? "FAILED" : "all the same");
	return bad ? 1 : 0;
}
