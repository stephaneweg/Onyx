//
// nds/nds_jit.cpp -- the JIT: the two processors' ARM and Thumb code translated to AArch64, a block at a
// time (from an address to its branch out, at most 48 instructions, within 1 KB), run by a small dispatcher
// (jitRun) that takes the interrupts and the halts between blocks.
//
//   * Thumb instructions are first rewritten as the ARM instructions that do the same (their PC read as
//     the Thumb one), so one translator serves both; BL pairs, the branches and the PC-relative loads are
//     handled on their own.
//   * x19 points at the processor (Arm); up to 9 of the guest registers a block uses most live in w20-w28
//     (loaded at its start, stored back at its exits); the others are read and written in Arm::r.
//   * the guest's NZCV is the host's (the ARM and AArch64 flags mean the same, carry included); a flag
//     liveness pass skips the flags nothing reads; they go back into the CPSR at the exits and before the
//     interpreter.
//   * loads and stores read the processor's page table (16 KB pages: Machine::rdPage / wrPage) inline; a
//     missing page goes to the memory functions through a stub out of line (which keeps the host flags);
//     the pages holding translated code have no write pointer, so writes there go through the functions,
//     which drop the code (by 1 KB chunk) and ask the block to stop (Arm::jitExit)
//   * what is not translated (MSR / MRS, the coprocessor, SWI, the ARMv5 DSP ops, LDM with ^...) runs in
//     the interpreter (execArm / execThumb) and ends the block.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (see nds.h).
//
#include "nds/nds.h"

namespace nds {

#if defined(__aarch64__)
extern unsigned g_jitNoKinds, g_jitNoDP;
extern void (*g_jitDumpHook) (const u32 *code, int words, u32 pc);


#define OFF(f) ((u32) __builtin_offsetof (Arm, f))

enum { FAST_BITS = 16 };
struct FastEntry { u32 key, pad; u32 *code; };
enum { CODE_SIZE = 16 << 20, MAX_BLOCKS = 1 << 16, HASH = 1 << 15, MAX_INS = 48, NCHUNKS = 4096 + 32 + 64 + 32 + 656 };
enum { FN = 8, FZ = 4, FC = 2, FV = 1, FALL = 15 };

struct Block
{
	u32 pc; u8 cpu, thumb, valid, pad;
	u32 *code;
	Block *hnext, *cnext;
	int chunk;
	const u8 *hostLo, *hostHi;		// its instructions' bytes
};

class Jit
{
public:
	u32 *buf; u32 size, pos;
	void (*enter) (Arm *c, u32 *code);
	u32 *exitStub;
	Block *blocks; int nBlocks;
	Block *hash[2][HASH];
	FastEntry *fast[2];
	Block *chunkList[NCHUNKS];
	u16 pageCount[NCHUNKS / 16 + 1];
	u32 helpers;
};
u64 g_jitStats[8];				// compiled, flushes, invalidations, slow reads, slow writes, dispatches, interp, block xfer slow

// ---- the helpers the code calls ------------------------------------------------------------------------------
extern u64 g_jitStats[8];
static u32 hRead32 (Arm *c, u32 a) { g_jitStats[3]++; return c->read32 (a & ~3u); }
static u32 hRead32r (Arm *c, u32 a) { g_jitStats[3]++; u32 v = c->read32 (a & ~3u); int n = (int) (a & 3) * 8; return n ? (v >> n) | (v << (32 - n)) : v; }
static u32 hRead16 (Arm *c, u32 a) { g_jitStats[3]++; return c->read16 (a & ~1u); }
static u32 hRead8 (Arm *c, u32 a) { g_jitStats[3]++; return c->read8 (a); }
static u32 hReadS8 (Arm *c, u32 a) { return (u32) (s32) (s8) c->read8 (a); }
static u32 hReadS16 (Arm *c, u32 a) { return (u32) (s32) (s16) c->read16 (a & ~1u); }
static u32 hRead16r (Arm *c, u32 a) { u32 v = c->read16 (a & ~1u); return (a & 1) ? (v >> 8) | (v << 24) : v; }	// (ARM7: rotated)
static u32 hReadS16x (Arm *c, u32 a) { return (a & 1) ? (u32) (s32) (s8) c->read8 (a) : (u32) (s32) (s16) c->read16 (a); }	// (ARM7 LDRSH)
static u32 hWrite32 (Arm *c, u32 a, u32 v) { g_jitStats[4]++; c->write32 (a & ~3u, v); return c->jitExit; }
static u32 hWrite16 (Arm *c, u32 a, u32 v) { g_jitStats[4]++; c->write16 (a & ~1u, v); return c->jitExit; }
static u32 hWrite8 (Arm *c, u32 a, u32 v) { g_jitStats[4]++; c->write8 (a, v); return c->jitExit; }
// one instruction in the interpreter (r[15] = its address + 8 / + 4 on entry): it ends the block
u32 g_jitInterpOps[256][2];			// (tests: the instructions left to the interpreter, sampled)
static void hInterp (Arm *c, u32 op, u32 addr)
{
	g_jitStats[6]++;
	{ u32 h = (op * 2654435761u) >> 24; g_jitInterpOps[h][0] = op | (c->cpsr & 0x20 ? 0x80000000u : 0); g_jitInterpOps[h][1]++; }
	bool thumb = c->cpsr & 0x20;
	c->r[15] = addr + (thumb ? 4 : 8);
	c->branched = false; c->cyc = 1;
	if (thumb) c->execThumb (op); else c->execArm (op);
	if (!c->branched) c->r[15] = addr + (thumb ? 2 : 4);
	c->ts += (s64) c->cyc << c->shift;
}
// a block transfer that left the fast path (another page, a missing one): the interpreter's, on Arm::r
static u32 hBlockXfer (Arm *c, u32 op, u32 addr)
{
	g_jitStats[7]++;
	c->r[15] = addr + 8;
	c->branched = false;
	c->armBlock (op);
	if (!c->branched) c->r[15] = addr + 4;
	return c->branched ? 2u : (u32) c->jitExit;
}

// ---- the assembler ------------------------------------------------------------------------------------------------
enum { XC = 19, WZR = 31, SP = 31, T0 = 9, T1 = 10, T2 = 11, T3 = 12, T4 = 13, T5 = 14, T6 = 15, IP0 = 16, IP1 = 17, WTGT = 8 };
enum { EQ = 0, NE, HS, LO, MI, PL, VS, VC, HI, LS, GE, LT, GT, LE, AL };

struct Asm
{
	u32 *p;
	void e (u32 v) { *p++ = v; }
	void movw (int rd, u32 v)
	{
		u32 lo = v & 0xFFFF, hi = v >> 16;
		if (!hi) { e (0x52800000 | (lo << 5) | (u32) rd); return; }
		if (!lo) { e (0x52A00000 | (hi << 5) | (u32) rd); return; }
		if (hi == 0xFFFF) { e (0x12800000 | ((~lo & 0xFFFF) << 5) | (u32) rd); return; }
		e (0x52800000 | (lo << 5) | (u32) rd);
		e (0x72A00000 | (hi << 5) | (u32) rd);
	}
	void movx (int rd, u64 v)
	{
		e (0xD2800000 | ((u32) (v & 0xFFFF) << 5) | (u32) rd);
		for (int k = 1; k < 4; k++) { u32 h = (u32) (v >> (k * 16)) & 0xFFFF; if (h) e (0xF2800000 | ((u32) k << 21) | (h << 5) | (u32) rd); }
	}
	void mov (int rd, int rm) { if (rd != rm) e (0x2A0003E0 | ((u32) rm << 16) | (u32) rd); }
	void movx64 (int rd, int rm) { e (0xAA0003E0 | ((u32) rm << 16) | (u32) rd); }
	// data processing, shifted register: base | shift<<22 | rm<<16 | imm6<<10 | rn<<5 | rd
	void dp (u32 base, int rd, int rn, int rm, int sh = 0, int amt = 0) { e (base | ((u32) sh << 22) | ((u32) rm << 16) | ((u32) amt << 10) | ((u32) rn << 5) | (u32) rd); }
	void addi (int rd, int rn, u32 imm, bool s = false) { e ((s ? 0x31000000u : 0x11000000u) | (imm << 10) | ((u32) rn << 5) | (u32) rd); }
	void subi (int rd, int rn, u32 imm, bool s = false) { e ((s ? 0x71000000u : 0x51000000u) | (imm << 10) | ((u32) rn << 5) | (u32) rd); }
	void addxi (int rd, int rn, u32 imm) { e (0x91000000 | (imm << 10) | ((u32) rn << 5) | (u32) rd); }
	void ubfm (int rd, int rn, int immr, int imms) { e (0x53000000 | ((u32) immr << 16) | ((u32) imms << 10) | ((u32) rn << 5) | (u32) rd); }
	void sbfm (int rd, int rn, int immr, int imms) { e (0x13000000 | ((u32) immr << 16) | ((u32) imms << 10) | ((u32) rn << 5) | (u32) rd); }
	void lsli (int rd, int rn, int n) { ubfm (rd, rn, (32 - n) & 31, 31 - n); }
	void lsri (int rd, int rn, int n) { ubfm (rd, rn, n, 31); }
	void asri (int rd, int rn, int n) { sbfm (rd, rn, n, 31); }
	void rori (int rd, int rn, int n) { e (0x13800000 | ((u32) rn << 16) | ((u32) n << 10) | ((u32) rn << 5) | (u32) rd); }
	void ubfx (int rd, int rn, int lsb, int w) { ubfm (rd, rn, lsb, lsb + w - 1); }
	void var (u32 base, int rd, int rn, int rm) { e (base | ((u32) rm << 16) | ((u32) rn << 5) | (u32) rd); }	// LSLV / LSRV / ASRV / RORV
	void csel (int rd, int rn, int rm, int cond) { e (0x1A800000 | ((u32) rm << 16) | ((u32) cond << 12) | ((u32) rn << 5) | (u32) rd); }
	void cmpi (int rn, u32 imm) { subi (WZR, rn, imm, true); }
	void ldrw (int rt, int rn, u32 off) { e (0xB9400000 | ((off >> 2) << 10) | ((u32) rn << 5) | (u32) rt); }
	void strw (int rt, int rn, u32 off) { e (0xB9000000 | ((off >> 2) << 10) | ((u32) rn << 5) | (u32) rt); }
	void ldrx (int rt, int rn, u32 off) { e (0xF9400000 | ((off >> 3) << 10) | ((u32) rn << 5) | (u32) rt); }
	void strx (int rt, int rn, u32 off) { e (0xF9000000 | ((off >> 3) << 10) | ((u32) rn << 5) | (u32) rt); }
	void ldrbi (int rt, int rn, u32 off) { e (0x39400000 | (off << 10) | ((u32) rn << 5) | (u32) rt); }
	void strbi (int rt, int rn, u32 off) { e (0x39000000 | (off << 10) | ((u32) rn << 5) | (u32) rt); }
	void mrs (int rt) { e (0xD53B4200 | (u32) rt); }
	void msr (int rt) { e (0xD51B4200 | (u32) rt); }
	void blr (int rn) { e (0xD63F0000 | ((u32) rn << 5)); }
	void br (int rn) { e (0xD61F0000 | ((u32) rn << 5)); }
	u32 *b () { u32 *at = p; e (0x14000000); return at; }
	u32 *bcond (int cond) { u32 *at = p; e (0x54000000 | (u32) cond); return at; }
	u32 *cbz (int rt, bool x64 = false) { u32 *at = p; e ((x64 ? 0xB4000000u : 0x34000000u) | (u32) rt); return at; }
	u32 *cbnz (int rt, bool x64 = false) { u32 *at = p; e ((x64 ? 0xB5000000u : 0x35000000u) | (u32) rt); return at; }
	u32 *tbz (int rt, int bit) { u32 *at = p; e (0x36000000 | ((u32) (bit & 31) << 19) | (u32) rt); return at; }
	u32 *tbnz (int rt, int bit) { u32 *at = p; e (0x37000000 | ((u32) (bit & 31) << 19) | (u32) rt); return at; }
	static void patch (u32 *at, u32 *to)
	{
		s32 d = (s32) (to - at);
		u32 op = *at;
		if ((op & 0xFC000000) == 0x14000000) *at = 0x14000000 | ((u32) d & 0x3FFFFFF);
		else if ((op & 0xFF000010) == 0x54000000 || (op & 0x7E000000) == 0x34000000) *at = (op & 0xFF00001F) | (((u32) d & 0x7FFFF) << 5);
		else *at = (op & 0xFFF8001F) | (((u32) d & 0x3FFF) << 5);	// TBZ / TBNZ
	}
	void bTo (u32 *to) { u32 *at = b (); patch (at, to); }
	void call (const void *fn) { movx (IP0, (u64) fn); blr (IP0); }
};

// ---- a block's instructions, decoded --------------------------------------------------------------------------------
enum Kind { K_DP, K_MUL, K_MULL, K_LDST, K_LDSTH, K_BLOCK, K_B, K_BX, K_CLZ, K_THUMB_BL, K_INTERP, K_TBRANCH };

struct Ins
{
	u32 addr, op;			// op: the ARM form (a Thumb one rewritten)
	u32 raw;			// the original (for the interpreter)
	u32 pcv;			// what PC reads as
	int kind;
	int cond;
	u8 flagsUse, flagsDef;		// NZCV bits
	u8 flagsNeed;			// of its definitions, the ones read later
	u8 liveAfter;			// the flags read after it (before being set again)
	bool ends;			// the block ends after it
	u32 target;			// a constant branch's target
	bool link, toThumb, toArm;	// BL / BLX
	int cyc;
};

static const u8 COND_USE[16] = { FZ, FZ, FC, FC, FN, FN, FV, FV, FC | FZ, FC | FZ, FN | FV, FN | FV, FN | FZ | FV, FN | FZ | FV, 0, 0 };

static inline u32 ror32 (u32 v, int n) { n &= 31; return n ? (v >> n) | (v << (32 - n)) : v; }

// a Thumb instruction as ARM: 0 = no equivalent (special or the interpreter's)
static u32 thumbToArm (u32 t)
{
	u32 rd = t & 7, rs = (t >> 3) & 7;
	switch (t >> 13)
	{
	case 0:
		if ((t >> 11) == 3)
		{
			u32 rn = (t >> 6) & 7;
			if (t & 0x400) return ((t & 0x200) ? 0xE2500000u : 0xE2900000u) | (rs << 16) | (rd << 12) | rn;
			return ((t & 0x200) ? 0xE0500000u : 0xE0900000u) | (rs << 16) | (rd << 12) | rn;
		}
		return 0xE1B00000u | (rd << 12) | (((t >> 6) & 31) << 7) | (((t >> 11) & 3) << 5) | rs;
	case 1:
	{
		u32 r = (t >> 8) & 7, imm = t & 0xFF;
		switch ((t >> 11) & 3)
		{
		case 0: return 0xE3B00000u | (r << 12) | imm;
		case 1: return 0xE3500000u | (r << 16) | imm;
		case 2: return 0xE2900000u | (r << 16) | (r << 12) | imm;
		default: return 0xE2500000u | (r << 16) | (r << 12) | imm;
		}
	}
	case 2:
		if ((t >> 10) == 0x10)
		{
			switch ((t >> 6) & 15)
			{
			case 0: return 0xE0100000u | (rd << 16) | (rd << 12) | rs;
			case 1: return 0xE0300000u | (rd << 16) | (rd << 12) | rs;
			case 2: return 0xE1B00010u | (rd << 12) | (rs << 8) | (0 << 5) | rd;
			case 3: return 0xE1B00010u | (rd << 12) | (rs << 8) | (1 << 5) | rd;
			case 4: return 0xE1B00010u | (rd << 12) | (rs << 8) | (2 << 5) | rd;
			case 5: return 0xE0B00000u | (rd << 16) | (rd << 12) | rs;
			case 6: return 0xE0D00000u | (rd << 16) | (rd << 12) | rs;
			case 7: return 0xE1B00010u | (rd << 12) | (rs << 8) | (3 << 5) | rd;
			case 8: return 0xE1100000u | (rd << 16) | rs;
			case 9: return 0xE2700000u | (rs << 16) | (rd << 12);
			case 10: return 0xE1500000u | (rd << 16) | rs;
			case 11: return 0xE1700000u | (rd << 16) | rs;
			case 12: return 0xE1900000u | (rd << 16) | (rd << 12) | rs;
			case 13: return 0xE0100090u | (rd << 16) | (rd << 8) | rs;
			case 14: return 0xE1D00000u | (rd << 16) | (rd << 12) | rs;
			default: return 0xE1F00000u | (rd << 12) | rs;
			}
		}
		if ((t >> 10) == 0x11)
		{
			u32 hd = (t & 7) | ((t >> 4) & 8), hs = (t >> 3) & 15;
			switch ((t >> 8) & 3)
			{
			case 0: return 0xE0800000u | (hd << 16) | (hd << 12) | hs;
			case 1: return 0xE1500000u | (hd << 16) | hs;
			case 2: return 0xE1A00000u | (hd << 12) | hs;
			default: return ((t & 0x80) ? 0xE12FFF30u : 0xE12FFF10u) | hs;
			}
		}
		if ((t >> 11) == 9) return 0xE59F0000u | (((t >> 8) & 7) << 12) | ((t & 0xFF) * 4);	// (PC: (addr + 4) & ~3)
		{
			u32 ro = (t >> 6) & 7, base = (rs << 16) | (rd << 12) | ro;
			switch ((t >> 9) & 7)
			{
			case 0: return 0xE7800000u | base;
			case 1: return 0xE18000B0u | base;
			case 2: return 0xE7C00000u | base;
			case 3: return 0xE19000D0u | base;
			case 4: return 0xE7900000u | base;
			case 5: return 0xE19000B0u | base;
			case 6: return 0xE7D00000u | base;
			default: return 0xE19000F0u | base;
			}
		}
	case 3:
	{
		u32 imm = (t >> 6) & 31;
		bool byte = t & 0x1000, load = t & 0x800;
		u32 base = (rs << 16) | (rd << 12);
		if (byte) return (load ? 0xE5D00000u : 0xE5C00000u) | base | imm;
		return (load ? 0xE5900000u : 0xE5800000u) | base | (imm * 4);
	}
	case 4:
		if (!(t & 0x1000))
		{
			u32 off = ((t >> 6) & 31) * 2;
			return ((t & 0x800) ? 0xE1D000B0u : 0xE1C000B0u) | (rs << 16) | (rd << 12) | ((off & 0xF0) << 4) | (off & 0xF);
		}
		return ((t & 0x800) ? 0xE59D0000u : 0xE58D0000u) | (((t >> 8) & 7) << 12) | ((t & 0xFF) * 4);
	case 5:
		if (!(t & 0x1000)) return ((t & 0x800) ? 0xE28D0F00u : 0xE28F0F00u) | (((t >> 8) & 7) << 12) | (t & 0xFF);
		if ((t & 0x0F00) == 0x0000) return ((t & 0x80) ? 0xE24DDF00u : 0xE28DDF00u) | (t & 0x7F);
		if ((t & 0x0600) == 0x0400)
		{
			u32 list = t & 0xFF;
			if (t & 0x800) return 0xE8BD0000u | list | ((t & 0x100) ? 0x8000u : 0);
			return 0xE92D0000u | list | ((t & 0x100) ? 0x4000u : 0);
		}
		return 0;
	case 6:
		if (!(t & 0x1000))
		{
			if (!(t & 0xFF)) return 0;
			return ((t & 0x800) ? 0xE8B00000u : 0xE8A00000u) | (((t >> 8) & 7) << 16) | (t & 0xFF);
		}
		return 0;
	default: return 0;
	}
}

// the ARM instruction's kind, its flags, whether it ends the block
static void classify (Ins &in, int cpu, bool thumb)
{
	u32 op = in.op;
	in.cond = (int) (op >> 28);
	in.flagsUse = in.cond < 14 ? COND_USE[in.cond] : 0;
	in.flagsDef = 0; in.ends = false; in.cyc = 1;
	in.link = in.toThumb = in.toArm = false;
	if (in.cond == 15) { in.kind = K_INTERP; in.ends = true; in.flagsUse = FALL; return; }
	switch ((op >> 25) & 7)
	{
	case 0: case 1:
		if ((op & 0x0E000090) == 0x00000090 && !(op & 0x02000000))
		{
			if ((op & 0x60) == 0)
			{
				if ((op & 0x0FC000F0) == 0x00000090) { in.kind = K_MUL; if (op & 0x00100000) in.flagsDef = FN | FZ; in.cyc = 2; return; }
				if ((op & 0x0F8000F0) == 0x00800090) { in.kind = K_MULL; if (op & 0x00100000) in.flagsDef = FN | FZ; in.cyc = 3; return; }
				in.kind = K_INTERP; in.ends = true; in.flagsUse = FALL; return;	// SWP
			}
			u32 sh = (op >> 5) & 3;
			bool L = op & 0x00100000;
			if (!L && sh >= 2) { in.kind = K_INTERP; in.ends = true; in.flagsUse = FALL; return; }	// LDRD / STRD
			in.kind = K_LDSTH; in.cyc = 2;
			if (L && ((op >> 12) & 15) == 15) { in.ends = true; in.flagsUse = FALL; }
			return;
		}
		if ((op & 0x01900000) == 0x01000000)		// the misc space
		{
			if ((op & 0x0FFFFFD0) == 0x012FFF10)		// BX / BLX register
			{
				if ((op & 0x20) && cpu) { in.kind = K_INTERP; in.ends = true; in.flagsUse = FALL; return; }
				in.kind = K_BX; in.ends = true; in.flagsUse |= FALL; in.link = op & 0x20; in.cyc = 3;
				return;
			}
			if ((op & 0x0FFF0FF0) == 0x016F0F10 && !cpu) { in.kind = K_CLZ; return; }
			in.kind = K_INTERP; in.ends = true; in.flagsUse = FALL; return;
		}
		{
			in.kind = K_DP;
			int opc = (int) (op >> 21) & 15;
			bool S = op & 0x00100000;
			int rd = (int) (op >> 12) & 15;
			bool regShift = !(op & 0x02000000) && (op & 0x10);
			if (regShift) in.cyc = 2;
			bool rrx = !(op & 0x02000000) && !(op & 0x10) && ((op >> 5) & 3) == 3 && !((op >> 7) & 31);
			if (rrx) in.flagsUse |= FC;
			if (opc == 5 || opc == 6 || opc == 7) in.flagsUse |= FC;
			if (rd == 15 && (opc < 8 || opc > 11))
			{
				if (S) { in.kind = K_INTERP; in.ends = true; in.flagsUse = FALL; return; }
				in.ends = true; in.flagsUse |= FALL; in.cyc = 3;
			}
			if (S)
			{
				bool logical = opc < 2 || opc == 8 || opc == 9 || opc >= 12;
				if (!logical) in.flagsDef = FALL;
				else
				{
					in.flagsDef = FN | FZ;
					// the shifter's carry: a rotated immediate, a non-zero immediate shift, a register shift (may keep it)
					if (op & 0x02000000) { if ((op >> 8) & 15) in.flagsDef |= FC; }
					else if (regShift) { in.flagsUse |= FC; in.flagsDef |= FC; }
					else if (((op >> 7) & 31) || ((op >> 5) & 3)) in.flagsDef |= FC;
				}
			}
			return;
		}
	case 2: case 3:
		if ((op & 0x02000010) == 0x02000010) { in.kind = K_INTERP; in.ends = true; in.flagsUse = FALL; return; }
		in.kind = K_LDST; in.cyc = 2;
		if (((op >> 12) & 15) == 15 && (op & 0x00100000)) { in.ends = true; in.flagsUse = FALL; in.cyc = 4; }
		{
			bool I = op & 0x02000000;
			if (I && ((op >> 5) & 3) == 3 && !((op >> 7) & 31)) in.flagsUse |= FC;	// RRX offset
		}
		return;
	case 4:
		if (op & 0x00400000) { in.kind = K_INTERP; in.ends = true; in.flagsUse = FALL; return; }	// ^
		in.kind = K_BLOCK; in.cyc = 1 + __builtin_popcount (op & 0xFFFF);
		if (!(op & 0xFFFF)) { in.kind = K_INTERP; in.ends = true; in.flagsUse = FALL; return; }
		if ((op & 0x00108000) == 0x00108000) { in.ends = true; in.flagsUse = FALL; in.cyc += 2; }
		in.flagsUse |= 0;
		return;
	case 5:
		in.kind = K_B; in.cyc = 3;
		in.target = in.pcv + (u32) ((s32) (op << 8) >> 6);
		in.link = op & 0x01000000;
		in.flagsUse |= FALL;
		if (in.cond == 14) in.ends = true;
		return;
	default:
		in.kind = K_INTERP; in.ends = true; in.flagsUse = FALL;
		return;
	}
	(void) thumb;
}

// ---- the translation -----------------------------------------------------------------------------------------------------
struct Gen
{
	Machine *m; Jit *j; Arm *c;
	Asm a;
	Ins ins[MAX_INS]; int n;
	int cpu; bool thumb;
	s8 hostOf[16];			// guest register -> host w20..w28, -1 in memory
	u16 written;			// guest registers written in the block (to store back)
	bool flagsHost, flagsDirty;
	int cycSoFar;
	// the out-of-line stubs
	struct Stub { int kind; u32 *from; u32 *back; int addrReg, valReg, dst, fn; int insIndex; u32 op; bool fh, fd; };
	Stub stubs[MAX_INS * 4]; int nStubs;

	u32 roff (int g) const { return OFF (r) + (u32) g * 4; }
	// the host register holding guest g (loading it into tmp if it lives in memory); pc reads as v
	int rd (int g, int tmp, u32 pcv)
	{
		if (g == 15) { a.movw (tmp, pcv); return tmp; }
		if (hostOf[g] >= 0) return hostOf[g];
		a.ldrw (tmp, XC, roff (g));
		return tmp;
	}
	int dst (int g, int tmp) { return (g < 15 && hostOf[g] >= 0) ? hostOf[g] : tmp; }
	void commit (int g, int reg)
	{
		written |= (u16) (1u << g);
		if (hostOf[g] >= 0) { a.mov (hostOf[g], reg); return; }
		a.strw (reg, XC, roff (g));
	}
	void loadFlags ()
	{
		if (flagsHost) return;
		a.ldrw (IP0, XC, OFF (cpsr));
		a.msr (IP0);
		flagsHost = true;
	}
	void spillFlags ()
	{
		if (!flagsHost || !flagsDirty) return;
		a.mrs (T5);
		a.ldrw (T6, XC, OFF (cpsr));
		a.ubfm (T6, T6, 0, 27);					// (and 0x0FFFFFFF)
		a.dp (0x2A000000, T6, T6, T5);				// orr
		a.strw (T6, XC, OFF (cpsr));
		flagsDirty = false;
	}
	void storeRegs ()
	{
		for (int g = 0; g < 15; g++) if (hostOf[g] >= 0 && (written & (1u << g))) a.strw (hostOf[g], XC, roff (g));
	}
	void addCycles (int cyc)
	{
		if (cyc <= 0) return;
		a.ldrx (T6, XC, OFF (ts));
		a.addxi (T6, T6, (u32) (cyc << c->shift));
		a.strx (T6, XC, OFF (ts));
	}
	// to the next block straight from here (key: its address | Thumb) while there is time and nothing to look at,
	// else back to jitRun
	void chain (int key)
	{
		a.ldrx (T1, XC, OFF (ts)); a.ldrx (T2, XC, OFF (target));
		a.dp (0xEB000000, WZR, T1, T2);				// cmp x10, x11
		Asm::patch (a.bcond (GE), j->exitStub);
		a.ldrbi (T1, XC, OFF (jitExit));
		Asm::patch (a.cbnz (T1), j->exitStub);
		a.ldrx (T2, XC, OFF (jitFast));
		a.ubfx (T1, key, 1, FAST_BITS);
		a.e (0x8B000000 | ((u32) T1 << 16) | (4u << 10) | ((u32) T2 << 5) | (u32) T2);	// add x11, x11, x10, lsl 4
		a.ldrw (T1, T2, 0);
		a.dp (0x6B000000, WZR, T1, key);			// cmp w10, wkey
		Asm::patch (a.bcond (NE), j->exitStub);
		a.ldrx (T2, T2, 8);
		a.br (T2);
	}
	// an exit to a constant address (in the current state, or switching)
	void exitTo (u32 pc, int cyc, int setThumb = -1)
	{
		storeRegs ();
		spillFlags ();
		addCycles (cyc);
		if (setThumb >= 0)
		{
			a.ldrw (T6, XC, OFF (cpsr));
			a.movw (T5, 0x20);
			a.dp (setThumb ? 0x2A000000u : 0x0A200000u, T6, T6, T5);	// orr / bic
			a.strw (T6, XC, OFF (cpsr));
		}
		a.movw (T5, pc);
		a.strw (T5, XC, OFF (r) + 60);
		a.movw (T0, pc | (u32) (setThumb >= 0 ? setThumb : thumb ? 1 : 0));
		chain (T0);
	}
	// an exit to the address in WTGT: interworking (bit 0 = Thumb) or in the current state
	void exitReg (int cyc, bool interwork)
	{
		storeRegs ();
		spillFlags ();
		addCycles (cyc);
		if (interwork)
		{
			a.ldrw (T6, XC, OFF (cpsr));
			a.e (0x33000000 | (27u << 16) | (0u << 10) | ((u32) WTGT << 5) | (u32) T6);	// bfi w15 (T), w8 bit 0 -> bit 5
			a.strw (T6, XC, OFF (cpsr));
			// pc = target & (thumb ? ~1 : ~3)
			a.ubfx (T5, WTGT, 0, 1);
			a.dp (0x2A000000, T5, WZR, T5, 0, 1);		// w14 = bit0 << 1 (lsl 1)
			a.movw (T4, 0xFFFFFFFC);
			a.dp (0x2A000000, T4, T4, T5);			// ~3 | (bit0 << 1)
			a.dp (0x0A000000, T5, WTGT, T4);
			a.strw (T5, XC, OFF (r) + 60);
			a.ubfx (T4, WTGT, 0, 1);
			a.dp (0x2A000000, T0, T5, T4);			// the key: pc | T
		}
		else
		{
			a.movw (T4, thumb ? 0xFFFFFFFE : 0xFFFFFFFC);
			a.dp (0x0A000000, T5, WTGT, T4);
			a.strw (T5, XC, OFF (r) + 60);
			if (thumb) { a.movw (T4, 1); a.dp (0x2A000000, T0, T5, T4); } else a.mov (T0, T5);
		}
		chain (T0);
	}

	// ---- loads and stores -----------------------------------------------------------------------------------------
	// value -> dstReg from the address in addrReg (size 4 / 2 / 1; signed; rotate an LDR's unaligned word)
	void load (int addrReg, int dstReg, int size, bool sgn, bool rotate, int insIndex)
	{
		// the helper that does the whole thing when the page is missing (0 hRead32, 1 hRead16, 2 hRead8,
		// 3 hReadS8, 4 hReadS16, 5 hReadS16x, 6 hRead16r, 7 hRead32r)
		int fn = size == 4 ? (rotate ? 7 : 0) : size == 1 ? (sgn ? 3 : 2) : sgn ? (cpu ? 5 : 4) : (cpu ? 6 : 1);
		a.ldrx (IP1, XC, OFF (rdTab));
		a.lsri (IP0, addrReg, 14);
		a.e (0xF8607800 | ((u32) IP0 << 16) | ((u32) IP1 << 5) | (u32) IP1);	// ldr x17, [x17, x16, lsl 3]
		u32 *miss = a.cbz (IP1, true);
		if (size == 4)
		{
			a.ubfm (IP0, addrReg, 2, 13); a.lsli (IP0, IP0, 2);				// & 0x3FFC
			a.e (0xB8606800 | ((u32) IP0 << 16) | ((u32) IP1 << 5) | (u32) dstReg);
			if (rotate) { a.lsli (IP0, addrReg, 3); a.var (0x1AC02C00, dstReg, dstReg, IP0); }
		}
		else if (size == 2)
		{
			if (sgn && cpu)						// ARM7 LDRSH: an odd address reads a signed byte
			{
				u32 *odd = a.tbnz (addrReg, 0);
				a.ubfm (IP0, addrReg, 1, 13); a.lsli (IP0, IP0, 1);
				a.e (0x78E06800 | ((u32) IP0 << 16) | ((u32) IP1 << 5) | (u32) dstReg);
				u32 *done = a.b ();
				Asm::patch (odd, a.p);
				a.ubfm (IP0, addrReg, 0, 13);
				a.e (0x38E06800 | ((u32) IP0 << 16) | ((u32) IP1 << 5) | (u32) dstReg);
				Asm::patch (done, a.p);
			}
			else
			{
				a.ubfm (IP0, addrReg, 1, 13); a.lsli (IP0, IP0, 1);			// & 0x3FFE
				a.e ((sgn ? 0x78E06800u : 0x78606800u) | ((u32) IP0 << 16) | ((u32) IP1 << 5) | (u32) dstReg);
				if (!sgn && cpu) { a.ubfm (IP0, addrReg, 0, 0); a.lsli (IP0, IP0, 3); a.var (0x1AC02C00, dstReg, dstReg, IP0); }	// (ARM7: rotated when odd)
			}
		}
		else
		{
			a.ubfm (IP0, addrReg, 0, 13);
			a.e ((sgn ? 0x38E06800u : 0x38606800u) | ((u32) IP0 << 16) | ((u32) IP1 << 5) | (u32) dstReg);
		}
		Stub &s = stubs[nStubs++];
		s.kind = 0; s.from = miss; s.back = a.p; s.addrReg = addrReg; s.dst = dstReg; s.fn = fn; s.insIndex = insIndex;
		s.fh = flagsHost; s.fd = flagsDirty;
	}
	void store (int addrReg, int valReg, int size, int insIndex)
	{
		a.ldrx (IP1, XC, OFF (wrTab));
		a.lsri (IP0, addrReg, 14);
		a.e (0xF8607800 | ((u32) IP0 << 16) | ((u32) IP1 << 5) | (u32) IP1);
		u32 *miss = a.cbz (IP1, true);
		if (size == 4) { a.ubfm (IP0, addrReg, 2, 13); a.lsli (IP0, IP0, 2); a.e (0xB8206800 | ((u32) IP0 << 16) | ((u32) IP1 << 5) | (u32) valReg); }
		else if (size == 2) { a.ubfm (IP0, addrReg, 1, 13); a.lsli (IP0, IP0, 1); a.e (0x78206800 | ((u32) IP0 << 16) | ((u32) IP1 << 5) | (u32) valReg); }
		else { a.ubfm (IP0, addrReg, 0, 13); a.e (0x38206800 | ((u32) IP0 << 16) | ((u32) IP1 << 5) | (u32) valReg); }
		Stub &s = stubs[nStubs++];
		s.kind = 1; s.from = miss; s.back = a.p; s.addrReg = addrReg; s.valReg = valReg; s.fn = size; s.insIndex = insIndex;
		s.fh = flagsHost; s.fd = flagsDirty;
	}
	void emitStubs ()
	{
		static const void *RD[8] = { (const void *) hRead32, (const void *) hRead16, (const void *) hRead8, (const void *) hReadS8, (const void *) hReadS16, (const void *) hReadS16x, (const void *) hRead16r, (const void *) hRead32r };
		for (int k = 0; k < nStubs; k++)
		{
			Stub &s = stubs[k];
			Asm::patch (s.from, a.p);
			a.mrs (IP0); a.strx (IP0, SP, 96);		// the host flags kept
			bool fh = flagsHost, fd = flagsDirty;
			flagsHost = s.fh; flagsDirty = s.fd;
			if (s.kind == 0)
			{
				a.mov (1, s.addrReg);
				a.movx64 (0, XC);
				a.call (RD[s.fn]);
				a.mov (s.dst, 0);
				a.ldrx (IP0, SP, 96); a.msr (IP0);
				a.bTo (s.back);
			}
			else if (s.kind == 1)
			{
				a.mov (1, s.addrReg); a.mov (2, s.valReg);
				a.movx64 (0, XC);
				a.call (s.fn == 4 ? (const void *) hWrite32 : s.fn == 2 ? (const void *) hWrite16 : (const void *) hWrite8);
				a.ldrx (IP0, SP, 96); a.msr (IP0);
				u32 *ok = a.cbz (0);
				// the code was changed (or the processor stops): out, at the next instruction
				Ins &in = ins[s.insIndex];
				exitTo (in.addr + (thumb ? 2 : 4), cycAt (s.insIndex));
				Asm::patch (ok, s.back);
			}
			else							// a block transfer's slow path: the interpreter's on Arm::r
			{
				Ins &in = ins[s.insIndex];
				for (int g = 0; g < 15; g++) if (hostOf[g] >= 0) a.strw (hostOf[g], XC, roff (g));
				if (flagsHost && flagsDirty) { spillFlags (); flagsDirty = true; }
				a.movx64 (0, XC); a.movw (1, s.op); a.movw (2, in.addr);
				a.call ((const void *) hBlockXfer);
				a.mov (T0, 0);
				for (int g = 0; g < 15; g++) if (hostOf[g] >= 0) a.ldrw (hostOf[g], XC, roff (g));
				a.ldrx (IP0, SP, 96); a.msr (IP0);
				u32 *ok = a.cbz (T0);
				u32 *jumped = a.tbnz (T0, 1);
				exitTo (in.addr + (thumb ? 2 : 4), cycAt (s.insIndex));	// (the code changed)
				Asm::patch (jumped, a.p);
				addCycles (cycAt (s.insIndex)); a.bTo (j->exitStub);	// (it jumped: Arm::r, the CPSR are right)
				Asm::patch (ok, s.back);
			}
			flagsHost = fh; flagsDirty = fd;
		}
	}
	int cycAt (int k) { int s = 0; for (int i = 0; i <= k; i++) s += ins[i].cyc; return s; }

	// ---- operand 2 of a data-processing instruction -> a register; the shifter's carry -> T5 if asked
	int operand2 (const Ins &in, int tmp, bool wantCarry, bool &carryKnown, int &carryConst)
	{
		u32 op = in.op;
		carryKnown = false; carryConst = -1;
		if (op & 0x02000000)
		{
			int rot = (int) ((op >> 8) & 15) * 2;
			u32 v = ror32 (op & 0xFF, rot);
			a.movw (tmp, v);
			if (rot) carryConst = (int) (v >> 31);
			return tmp;
		}
		int rm = (int) op & 15;
		int type = (int) (op >> 5) & 3;
		if (op & 0x10)						// shift by a register
		{
			int rs = (int) (op >> 8) & 15;
			int vm = rd (rm, T0, in.pcv + 4);
			int vs = rd (rs, T1, in.pcv + 4);
			a.ubfx (T2, vs, 0, 8);					// the amount: Rs & 0xFF
			// (no compare: the flags may be the host's) the amount clamped to K: (a & m) | (K & ~m), m = a < K ? -1 : 0
			auto clamp = [&] (int K) { a.subi (T3, T2, (u32) K); a.asri (T3, T3, 31); a.dp (0x0A000000, T2, T2, T3); a.movw (T4, (u32) K); a.dp (0x0A200000, T4, T4, T3); a.dp (0x2A000000, T2, T2, T4); };
			if (wantCarry)						// the old C kept when the amount is 0 -> T6 (0 / 1), zm -> T4
			{
				loadFlags ();
				a.mrs (T6); a.ubfx (T6, T6, 29, 1);
			}
			switch (type)
			{
			case 0:							// x = Rm << min (a, 33) in 64 bits: C bit 32
				if (wantCarry) { a.subi (T4, T2, 1); a.lsri (T4, T4, 31); a.strw (T4, SP, 104); }
				clamp (33);
				a.ubfm (IP0, vm, 0, 31);				// (zero-extended)
				a.e (0x9AC02000 | ((u32) T2 << 16) | ((u32) IP0 << 5) | (u32) IP0);	// lslv x16, x16, x11
				if (wantCarry) a.e (0xD3608000 | ((u32) IP0 << 5) | (u32) T5);	// ubfx x14, x16, 32, 1
				a.mov (tmp, IP0);
				break;
			case 1:							// x = (Rm << 1) >> min (a, 33): C bit 0
			case 2:							// (signed, min (a, 32))
				if (wantCarry) { a.subi (T4, T2, 1); a.lsri (T4, T4, 31); a.strw (T4, SP, 104); }
				clamp (type == 1 ? 33 : 32);
				if (type == 1) a.ubfm (IP0, vm, 0, 31); else a.e (0x93407C00 | ((u32) vm << 5) | (u32) IP0);	// sxtw x16, w(vm)
				a.e (0xD37FF800 | ((u32) IP0 << 5) | (u32) IP0);	// lsl x16, x16, 1
				a.e ((type == 1 ? 0x9AC02400u : 0x9AC02800u) | ((u32) T2 << 16) | ((u32) IP0 << 5) | (u32) IP0);	// lsrv / asrv x16, x16, x11
				if (wantCarry) a.ubfx (T5, IP0, 0, 1);
				a.e (0xD341FC00 | ((u32) IP0 << 5) | (u32) tmp);	// lsr x(tmp), x16, 1 (w: the low half)
				break;
			default:
				if (wantCarry) { a.subi (T4, T2, 1); a.lsri (T4, T4, 31); a.strw (T4, SP, 104); }
				a.var (0x1AC02C00, tmp, vm, T2);
				if (wantCarry) a.ubfx (T5, tmp, 31, 1);
				break;
			}
			if (wantCarry)						// C = zm ? old : new  ->  new ^ ((new ^ old) & zm)
			{
				a.ldrw (T4, SP, 104);
				a.dp (0x4A000000, T3, T5, T6);
				a.dp (0x0A000000, T3, T3, T4);
				a.dp (0x4A000000, T5, T5, T3);
				carryKnown = true;
			}
			return tmp;
		}
		int amt = (int) (op >> 7) & 31;
		int vm = rd (rm, T0, in.pcv);
		switch (type)
		{
		case 0:
			if (!amt) return vm;
			if (wantCarry) { a.ubfx (T5, vm, 32 - amt, 1); carryKnown = true; }
			a.lsli (tmp, vm, amt); return tmp;
		case 1:
			if (!amt) { if (wantCarry) { a.ubfx (T5, vm, 31, 1); carryKnown = true; } a.movw (tmp, 0); return tmp; }
			if (wantCarry) { a.ubfx (T5, vm, amt - 1, 1); carryKnown = true; }
			a.lsri (tmp, vm, amt); return tmp;
		case 2:
			if (!amt) amt = 32;
			if (wantCarry) { a.ubfx (T5, vm, amt - 1, 1); carryKnown = true; }
			a.asri (tmp, vm, amt == 32 ? 31 : amt); return tmp;
		default:
			if (!amt)						// RRX: (C << 31) | (v >> 1)
			{
				loadFlags ();
				a.mrs (T6);
				a.ubfx (T6, T6, 29, 1);
				if (wantCarry) { a.ubfx (T5, vm, 0, 1); carryKnown = true; }
				a.lsri (tmp, vm, 1);
				a.dp (0x2A000000, tmp, tmp, T6, 0, 31);	// orr tmp, tmp, w15, lsl 31
				return tmp;
			}
			if (wantCarry) { a.ubfx (T5, vm, amt - 1, 1); carryKnown = true; }
			a.rori (tmp, vm, amt); return tmp;
		}
	}

	// N and Z from res; C from T5 (carryKnown) / the constant / kept; V kept
	void setNZ (int res, u8 live, bool carryKnown, int carryConst, u8 defs)
	{
		bool keepC = (live & FC) && !(defs & FC);
		bool newC = (live & FC) && (defs & FC);
		bool keepV = live & FV;
		if (!keepC && !newC && !keepV) { a.dp (0x6A000000, WZR, res, res); flagsHost = true; flagsDirty = true; return; }	// tst
		if (keepC || keepV) { loadFlags (); a.mrs (IP1); }
		a.dp (0x6A000000, WZR, res, res);
		a.mrs (IP0);
		if (newC)
		{
			if (carryKnown) a.dp (0xAA000000, IP0, IP0, T5, 0, 29);			// orr x16, x16, x14, lsl 29
			else if (carryConst == 1) { a.movw (T6, 0x20000000); a.dp (0xAA000000, IP0, IP0, T6); }
		}
		else if (keepC) { a.ubfm (T6, IP1, 29, 29); a.dp (0xAA000000, IP0, IP0, T6, 0, 29); }
		if (keepV) { a.ubfm (T6, IP1, 28, 28); a.dp (0xAA000000, IP0, IP0, T6, 0, 28); }
		a.msr (IP0);
		flagsHost = true; flagsDirty = true;
	}

	bool genDP (int k)
	{
		Ins &in = ins[k];
		u32 op = in.op;
		int opc = (int) (op >> 21) & 15;
		bool S = op & 0x00100000;
		int rn = (int) (op >> 16) & 15, rdg = (int) (op >> 12) & 15;
		u8 need = S ? in.flagsNeed : 0;
		bool logical = opc < 2 || opc == 8 || opc == 9 || opc >= 12;
		bool regShift = !(op & 0x02000000) && (op & 0x10);
		bool carryKnown; int carryConst;
		bool wantC = logical && S && (need & FC);
		int b = operand2 (in, T1, wantC, carryKnown, carryConst);
		int av = (opc == 13 || opc == 15) ? -1 : rd (rn, T2, in.pcv + (regShift ? 4 : 0));
		int d = (opc >= 8 && opc <= 11) ? T3 : (rdg == 15 ? WTGT : dst (rdg, T3));
		bool setF = S && need;
		if (opc == 5 || opc == 6 || opc == 7) loadFlags ();
		switch (opc)
		{
		case 0: case 8: a.dp (0x0A000000, d, av, b); break;
		case 1: case 9: a.dp (0x4A000000, d, av, b); break;
		case 2: case 10: a.dp (setF ? 0x6B000000u : 0x4B000000u, opc == 10 ? WZR : d, av, b); break;
		case 3: a.dp (setF ? 0x6B000000u : 0x4B000000u, d, b, av); break;
		case 4: case 11: a.dp (setF ? 0x2B000000u : 0x0B000000u, opc == 11 ? WZR : d, av, b); break;
		case 5: a.dp (setF ? 0x3A000000u : 0x1A000000u, d, av, b); break;
		case 6: a.dp (setF ? 0x7A000000u : 0x5A000000u, d, av, b); break;
		case 7: a.dp (setF ? 0x7A000000u : 0x5A000000u, d, b, av); break;
		case 12: a.dp (0x2A000000, d, av, b); break;
		case 13: a.mov (d, b); break;
		case 14: a.dp (0x0A200000, d, av, b); break;
		default: a.dp (0x2A200000, d, WZR, b); break;
		}
		if (setF)
		{
			if (logical) setNZ ((opc >= 8 && opc <= 11) ? T3 : d, in.liveAfter, carryKnown, carryConst, in.flagsDef);
			else { flagsHost = true; flagsDirty = true; }
		}
		if (opc >= 8 && opc <= 11) return true;
		if (rdg == 15) { exitReg (cycAt (k), false); return true; }
		if (d != (rdg < 15 && hostOf[rdg] >= 0 ? hostOf[rdg] : -2)) commit (rdg, d);
		else written |= (u16) (1u << rdg);
		return true;
	}

	bool genMul (int k)
	{
		Ins &in = ins[k];
		u32 op = in.op;
		int rdg = (int) (op >> 16) & 15, rn = (int) (op >> 12) & 15, rs = (int) (op >> 8) & 15, rm = (int) op & 15;
		if (rdg == 15) return false;
		int vm = rd (rm, T0, in.pcv), vs = rd (rs, T1, in.pcv);
		int d = dst (rdg, T3);
		int acc = (op & 0x00200000) ? rd (rn, T2, in.pcv) : WZR;
		a.e (0x1B000000 | ((u32) vs << 16) | ((u32) acc << 10) | ((u32) vm << 5) | (u32) d);
		if ((op & 0x00100000) && in.flagsNeed) setNZ (d, in.liveAfter, false, -1, FN | FZ);
		if (hostOf[rdg] < 0 || d != hostOf[rdg]) commit (rdg, d); else written |= (u16) (1u << rdg);
		return true;
	}

	bool genMull (int k)
	{
		Ins &in = ins[k];
		u32 op = in.op;
		int hi = (int) (op >> 16) & 15, lo = (int) (op >> 12) & 15, rs = (int) (op >> 8) & 15, rm = (int) op & 15;
		if (hi == 15 || lo == 15 || hi == lo) return false;
		int vm = rd (rm, T0, in.pcv), vs = rd (rs, T1, in.pcv);
		bool sgn = op & 0x00400000;
		if (op & 0x00200000)
		{
			int vl = rd (lo, T2, in.pcv), vh = rd (hi, T3, in.pcv);
			a.ubfm (T4, vl, 0, 31);					// (w -> x: the upper half cleared)
			a.e (0xAA000000 | ((u32) vh << 16) | (32u << 10) | ((u32) T4 << 5) | (u32) T4);	// orr x13, x13, x(vh), lsl 32
			a.e ((sgn ? 0x9B200000u : 0x9BA00000u) | ((u32) vs << 16) | ((u32) T4 << 10) | ((u32) vm << 5) | (u32) T4);
		}
		else a.e ((sgn ? 0x9B207C00u : 0x9BA07C00u) | ((u32) vs << 16) | ((u32) vm << 5) | (u32) T4);
		if ((op & 0x00100000) && in.flagsNeed)
		{
			// N from bit 63, Z from the 64 bits; C, V kept
			bool keep = in.liveAfter & (FC | FV);
			if (keep) { loadFlags (); a.mrs (T5); }
			a.e (0xEA00001F | ((u32) T4 << 16) | ((u32) T4 << 5));	// tst x13, x13
			if (keep)
			{
				a.mrs (T3);
				a.ubfm (T5, T5, 28, 29);			// w14 = C V bits >> 28
				a.dp (0xAA000000, T3, T3, T5, 0, 28);
				a.msr (T3);
			}
			flagsHost = true; flagsDirty = true;
		}
		int dl = dst (lo, T2);
		a.mov (dl, T4);
		commit (lo, dl);
		a.e (0xD360FC00 | ((u32) T4 << 5) | (u32) T4);		// lsr x13, x13, 32
		int dh = dst (hi, T3);
		a.mov (dh, T4);
		commit (hi, dh);
		return true;
	}

	bool genLdst (int k)
	{
		Ins &in = ins[k];
		u32 op = in.op;
		bool I = op & 0x02000000, P = op & 0x01000000, U = op & 0x00800000, B = op & 0x00400000, W = op & 0x00200000, L = op & 0x00100000;
		int rn = (int) (op >> 16) & 15, rdg = (int) (op >> 12) & 15;
		bool wb = W || !P;
		if (wb && rn == 15) return false;
		// the offset
		int offReg = -1; u32 offImm = 0;
		if (I)
		{
			bool ck; int cc;
			Ins tmp = in; tmp.op = (op & ~0x02000010u) | 0;		// a register operand with an immediate shift
			offReg = operand2 (tmp, T1, false, ck, cc);
		}
		else offImm = op & 0xFFF;
		int base = rd (rn, T2, rn == 15 ? (in.pcv & ~3u) : in.pcv);
		if (rn == 15) { base = T2; }
		// eff = base +- off -> T3
		if (offReg >= 0) a.dp (U ? 0x0B000000u : 0x4B000000u, T3, base, offReg);
		else if (offImm) { if (U) a.addi (T3, base, offImm); else a.subi (T3, base, offImm); }
		else a.mov (T3, base);
		int addr = P ? T3 : base;
		if (L)
		{
			if (addr != T3 && addr != T4) { a.mov (T4, addr); addr = T4; }
			if (wb && rn != rdg) commit (rn, T3);
			int d = rdg == 15 ? WTGT : dst (rdg, T0);
			load (addr, d, B ? 1 : 4, false, !B, k);
			if (rdg == 15) { exitReg (cycAt (k), !cpu && !(c->cpCtl & 0x8000)); return true; }
			if (hostOf[rdg] < 0 || d != hostOf[rdg]) commit (rdg, d); else written |= (u16) (1u << rdg);
		}
		else
		{
			int v = rd (rdg, T0, in.pcv + 4);
			if (v != T0) { a.mov (T0, v); v = T0; }
			if (addr != T3) { a.mov (T4, addr); addr = T4; }
			if (wb) commit (rn, T3);
			store (addr, v, B ? 1 : 4, k);
		}
		return true;
	}

	bool genLdstH (int k)
	{
		Ins &in = ins[k];
		u32 op = in.op;
		bool P = op & 0x01000000, U = op & 0x00800000, W = op & 0x00200000, L = op & 0x00100000;
		int rn = (int) (op >> 16) & 15, rdg = (int) (op >> 12) & 15, sh = (int) (op >> 5) & 3;
		bool wb = W || !P;
		if (wb && rn == 15) return false;
		int offReg = -1; u32 offImm = 0;
		if (op & 0x00400000) offImm = ((op >> 4) & 0xF0) | (op & 0xF);
		else offReg = rd ((int) op & 15, T1, in.pcv);
		int base = rd (rn, T2, in.pcv);
		if (offReg >= 0) a.dp (U ? 0x0B000000u : 0x4B000000u, T3, base, offReg);
		else if (offImm) { if (U) a.addi (T3, base, offImm); else a.subi (T3, base, offImm); }
		else a.mov (T3, base);
		int addr = P ? T3 : base;
		if (L)
		{
			if (addr != T3) { a.mov (T4, addr); addr = T4; }
			if (wb && rn != rdg) commit (rn, T3);
			int d = rdg == 15 ? WTGT : dst (rdg, T0);
			if (sh == 1) load (addr, d, 2, false, false, k);
			else if (sh == 2) load (addr, d, 1, true, false, k);
			else load (addr, d, 2, true, false, k);
			if (rdg == 15) { exitReg (cycAt (k), false); return true; }
			if (hostOf[rdg] < 0 || d != hostOf[rdg]) commit (rdg, d); else written |= (u16) (1u << rdg);
		}
		else
		{
			if (sh != 1) return false;
			int v = rd (rdg, T0, in.pcv + 4);
			if (v != T0) { a.mov (T0, v); v = T0; }
			if (addr != T3) { a.mov (T4, addr); addr = T4; }
			if (wb) commit (rn, T3);
			store (addr, v, 2, k);
		}
		return true;
	}

	bool genBlock (int k)
	{
		Ins &in = ins[k];
		u32 op = in.op;
		bool P = op & 0x01000000, U = op & 0x00800000, W = op & 0x00200000, L = op & 0x00100000;
		int rn = (int) (op >> 16) & 15;
		u32 list = op & 0xFFFF;
		int nr = __builtin_popcount (list);
		if (rn == 15) return false;
		int base = rd (rn, T2, in.pcv);
		a.mov (T3, base);					// the old base, kept
		// the start address -> T4
		u32 bytes = (u32) nr * 4;
		if (U) { if (P) a.addi (T4, T3, 4); else a.mov (T4, T3); }
		else { a.subi (T4, T3, bytes - (P ? 0 : 4)); }
		// the page: one for the whole transfer, else the slow path
		a.ldrx (IP1, XC, L ? OFF (rdTab) : OFF (wrTab));
		a.lsri (IP0, T4, 14);
		a.e (0xF8607800 | ((u32) IP0 << 16) | ((u32) IP1 << 5) | (u32) IP1);
		u32 *miss = a.cbz (IP1, true);
		a.ubfm (IP0, T4, 0, 13); a.ubfm (IP0, IP0, 2, 31); a.lsli (IP0, IP0, 2);		// & 0x3FFC
		a.addi (T5, IP0, bytes - 1);
		a.lsri (T5, T5, 14);
		u32 *miss2 = a.cbnz (T5);
		a.e (0x8B204000 | ((u32) IP0 << 16) | ((u32) IP1 << 5) | (u32) IP1);	// add x17, x17, w16, uxtw
		u32 newBaseOff = U ? bytes : (u32) -(s32) bytes;
		int i = 0;
		bool first = true;
		for (int g = 0; g < 16; g++)
		{
			if (!(list & (1u << g))) continue;
			u32 off = (u32) i * 4;
			if (L)
			{
				int d = g == 15 ? (int) WTGT : (hostOf[g] >= 0 ? (int) hostOf[g] : (int) T0);
				a.ldrw (d, IP1, off);
				if (g != 15 && hostOf[g] < 0) a.strw (d, XC, roff (g));
				if (g != 15) written |= (u16) (1u << g);
			}
			else
			{
				int v;
				if (g == 15) { a.movw (T0, in.pcv + 4); v = T0; }
				else if (g == rn) v = T3;
				else v = rd (g, T0, 0);
				if (g == rn && !first && cpu == 1)		// (ARM7: a base stored after the first: the new one)
				{
					if (U) a.addi (T0, T3, bytes); else a.subi (T0, T3, bytes);
					v = T0;
				}
				a.strw (v, IP1, off);
			}
			first = false;
			i++;
		}
		(void) newBaseOff;
		// the write-back
		if (W)
		{
			bool doWb = L ? (!(list & (1u << rn)) || (!cpu && (list & ~((2u << rn) - 1)))) : true;
			if (doWb)
			{
				int d = dst (rn, T0);
				if (U) a.addi (d, T3, bytes); else a.subi (d, T3, bytes);
				if (hostOf[rn] < 0 || d != hostOf[rn]) commit (rn, d); else written |= (u16) (1u << rn);
			}
		}
		u32 *back = a.p;
		Stub &s = stubs[nStubs++];
		s.kind = 2; s.from = miss; s.back = back; s.op = op; s.insIndex = k; s.fh = flagsHost; s.fd = flagsDirty;
		Stub &s2 = stubs[nStubs++];
		s2 = s; s2.from = miss2;
		if (L && (list & 0x8000)) { exitReg (cycAt (k), !cpu && !(c->cpCtl & 0x8000)); return true; }
		return true;
	}

	bool genB (int k)
	{
		Ins &in = ins[k];
		if (in.link) { a.movw (T0, (in.addr + (thumb ? 2 : 4)) | (thumb ? 1 : 0)); commit (14, T0); }
		exitTo (in.target, cycAt (k));
		return true;
	}

	bool genBX (int k)
	{
		Ins &in = ins[k];
		int rm = (int) in.op & 15;
		int v = rd (rm, T0, in.pcv);
		a.mov (WTGT, v);
		if (in.link) { a.movw (T1, thumb ? ((in.addr + 2) | 1) : in.addr + 4); commit (14, T1); }
		exitReg (cycAt (k), true);
		return true;
	}

	bool genClz (int k)
	{
		Ins &in = ins[k];
		int rm = (int) in.op & 15, rdg = (int) (in.op >> 12) & 15;
		if (rdg == 15) return false;
		int v = rd (rm, T0, in.pcv);
		int d = dst (rdg, T1);
		a.e (0x5AC01000 | ((u32) v << 5) | (u32) d);
		if (hostOf[rdg] < 0 || d != hostOf[rdg]) commit (rdg, d); else written |= (u16) (1u << rdg);
		return true;
	}

	void genInterp (int k)
	{
		Ins &in = ins[k];
		// everything back into Arm, the instruction run, out (Arm::r[15] set by it)
		storeRegs ();
		spillFlags ();
		addCycles (cycAt (k) - in.cyc);
		a.movx64 (0, XC); a.movw (1, in.raw); a.movw (2, in.addr);
		a.call ((const void *) hInterp);
		a.bTo (j->exitStub);
	}

	bool genOne (int k)
	{
		Ins &in = ins[k];
		switch (in.kind)
		{
		case K_DP: return genDP (k);
		case K_MUL: return genMul (k);
		case K_MULL: return genMull (k);
		case K_LDST: return genLdst (k);
		case K_LDSTH: return genLdstH (k);
		case K_BLOCK: return genBlock (k);
		case K_B: case K_TBRANCH: case K_THUMB_BL: return genB (k);
		case K_BX: return genBX (k);
		case K_CLZ: return genClz (k);
		default: return false;
		}
	}
};

// ---- the cache ---------------------------------------------------------------------------------------------------------------
static int chunkOf (Machine *m, const u8 *p)
{
	if (p >= m->mainRam && p < m->mainRam + 0x400000) return (int) ((p - m->mainRam) >> 10);
	if (p >= m->wram && p < m->wram + sizeof m->wram) return 4096 + (int) ((p - m->wram) >> 10);
	if (p >= m->wram7 && p < m->wram7 + sizeof m->wram7) return 4096 + 32 + (int) ((p - m->wram7) >> 10);
	if (p >= m->itcm && p < m->itcm + sizeof m->itcm) return 4096 + 96 + (int) ((p - m->itcm) >> 10);
	if (p >= m->vram && p < m->vram + 0xA4000) return 4096 + 128 + (int) ((p - m->vram) >> 10);
	return -1;
}

bool jitPageHasCode (Machine *m, const u8 *page)
{
	int c = chunkOf (m, page);
	return c >= 0 && m->jit->pageCount[c >> 4] != 0;
}

static void flushCode (void *from, void *to)
{
	u64 ctr; asm volatile ("mrs %0, ctr_el0" : "=r" (ctr));
	u64 dl = 4u << ((ctr >> 16) & 15), il = 4u << (ctr & 15);
	u64 b = (u64) from, e = (u64) to;
	for (u64 x = b & ~(dl - 1); x < e; x += dl) asm volatile ("dc cvau, %0" :: "r" (x) : "memory");
	asm volatile ("dsb ish" ::: "memory");
	for (u64 x = b & ~(il - 1); x < e; x += il) asm volatile ("ic ivau, %0" :: "r" (x) : "memory");
	asm volatile ("dsb ish\n\tisb" ::: "memory");
}

static void protectPage (Machine *m, const u8 *hostPage)
{
	for (int cpu = 0; cpu < 2; cpu++)
	{
		u8 **wp = m->wrPage[cpu];
		for (int i = 0; i < Machine::NPAGES; i++) if (wp[i] == hostPage) wp[i] = 0;
	}
}

void jitFlushAll (Machine *m)
{
	Jit *j = m->jit;
	if (!j) return;
	g_jitStats[1]++;
	for (int c = 0; c < 2; c++) for (int i = 0; i < HASH; i++) j->hash[c][i] = 0;
	for (int c = 0; c < 2; c++) for (int i = 0; i < (1 << FAST_BITS); i++) { j->fast[c][i].key = 0xFFFFFFFF; j->fast[c][i].code = 0; }
	for (int i = 0; i < NCHUNKS; i++) j->chunkList[i] = 0;
	for (int i = 0; i < NCHUNKS / 16 + 1; i++) j->pageCount[i] = 0;
	j->nBlocks = 0;
	j->pos = j->helpers;
	m->pagesDirty = true;
}

void jitInvalidateHost (Machine *m, const u8 *p)
{
	Jit *j = m->jit;
	int ch = chunkOf (m, p);
	if (ch < 0 || !j->chunkList[ch]) return;
	// only the blocks whose bytes these are (data beside code is written often)
	bool any = false;
	for (Block **pb = &j->chunkList[ch]; *pb; )
	{
		Block *b = *pb;
		if (p >= b->hostLo && p < b->hostHi)
		{
			b->valid = 0; *pb = b->cnext; any = true;
			FastEntry &f = j->fast[b->cpu][(b->pc >> 1) & ((1 << FAST_BITS) - 1)];
			if (f.code == b->code) f.key = 0xFFFFFFFF;
		}
		else pb = &b->cnext;
	}
	if (!any) return;
	g_jitStats[2]++;
	m->arm9.jitExit = true; m->arm7.jitExit = true;
}

static Block *lookup (Jit *j, int cpu, u32 pc, bool thumb)
{
	Block **slot = &j->hash[cpu][(pc >> 1) & (HASH - 1)];
	while (*slot)
	{
		Block *b = *slot;
		if (!b->valid) { *slot = b->hnext; continue; }
		if (b->pc == pc && b->thumb == thumb) return b;
		slot = &b->hnext;
	}
	return 0;
}

// the guest's instructions from pc: decoded, their flags' liveness
static int decode (Machine *m, Arm &c, u32 pc, bool thumb, Ins *ins)
{
	int n = 0;
	u32 a = pc;
	u32 chunkEnd = (pc & ~0x3FFu) + 0x400;
	while (n < MAX_INS && a < chunkEnd)
	{
		Ins &in = ins[n];
		in.addr = a;
		if (thumb)
		{
			u32 t = c.read16 (a);
			in.raw = t;
			in.pcv = a + 4;
			in.target = 0;
			if ((t >> 11) == 0x1E && a + 2 < chunkEnd)			// BL / BLX pair
			{
				u32 t2 = c.read16 (a + 2);
				if ((t2 >> 11) == 0x1F || ((t2 >> 11) == 0x1D && !c.num))
				{
					s32 hi = (s32) ((t & 0x7FF) << 21) >> 9;
					u32 lr = a + 4 + (u32) hi;
					in.op = 0; in.kind = K_THUMB_BL; in.cond = 14; in.flagsUse = FALL; in.flagsDef = 0; in.ends = true; in.cyc = 4;
					in.link = true;
					if ((t2 >> 11) == 0x1F) { in.target = (lr + ((t2 & 0x7FF) << 1)) & ~1u; in.toArm = false; }
					else { in.target = (lr + ((t2 & 0x7FF) << 1)) & ~3u; in.toArm = true; }
					in.addr = a; in.raw = t;
					// (the return address: after both halves)
					in.pcv = a + 2;
					n++;
					break;
				}
			}
			if ((t >> 12) == 0xD && ((t >> 8) & 15) < 14)			// conditional branch
			{
				in.op = 0; in.kind = K_TBRANCH; in.cond = (int) (t >> 8) & 15;
				in.flagsUse = FALL; in.flagsDef = 0; in.ends = false; in.cyc = 3; in.link = false;
				in.target = a + 4 + (u32) ((s32) (s8) (t & 0xFF) * 2);
				n++; a += 2; continue;
			}
			if ((t >> 11) == 0x1C)						// B
			{
				in.op = 0; in.kind = K_TBRANCH; in.cond = 14; in.flagsUse = FALL; in.flagsDef = 0; in.ends = true; in.cyc = 3; in.link = false;
				in.target = a + 4 + (u32) (((s32) (t << 21)) >> 20);
				n++; break;
			}
			u32 arm = thumbToArm (t);
			if (!arm) { in.op = 0; in.kind = K_INTERP; in.cond = 14; in.flagsUse = FALL; in.flagsDef = 0; in.ends = true; in.cyc = 1; n++; break; }
			in.op = arm;
			if ((t >> 11) == 9 || ((t >> 12) == 0xA && !(t & 0x800))) in.pcv = (a + 4) & ~3u;	// (PC-relative: aligned)
			classify (in, c.num, true);
			n++; a += 2;
			if (in.ends) break;
		}
		else
		{
			u32 op = c.read32 (a);
			in.raw = op; in.op = op; in.pcv = a + 8;
			classify (in, c.num, false);
			if (in.kind == K_B && in.cond == 15) in.kind = K_INTERP;
			if ((op >> 28) == 0xF && !c.num && (op & 0x0E000000) == 0x0A000000)	// BLX immediate
			{
				in.kind = K_B; in.cond = 14; in.ends = true; in.link = true; in.toThumb = true; in.flagsUse = FALL; in.cyc = 3;
				in.target = a + 8 + (u32) (((s32) (op << 8) >> 6) + (s32) ((op >> 23) & 2));
			}
			n++; a += 4;
			if (in.ends) break;
		}
	}
	(void) m;
	for (int k = 0; k < n; k++)
		if (ins[k].kind == K_DP && g_jitNoDP)
		{
			u32 op = ins[k].op; unsigned f = 1u << ((op >> 21) & 15);
			if (op & 0x00100000) f |= 1u << 16;
			if (!(op & 0x02000000) && (op & 0x10)) f |= 1u << 17;
			if (op & 0x02000000) f |= 1u << 18;
			if (!(op & 0x02000000) && !(op & 0x10) && (op & 0xFE0)) f |= 1u << 19;
			if (thumb) f |= 1u << 20;
			unsigned want = g_jitNoDP;
			if ((want & 0xFFFF) && !(f & want & 0xFFFF)) continue;
			if ((want & 0x1F0000) && (f & want & 0x1F0000) != (want & 0x1F0000)) continue;
			ins[k].kind = K_INTERP; ins[k].flagsUse = FALL; ins[k].ends = true; n = k + 1; break;
		}
	for (int k = 0; k < n; k++) if (g_jitNoKinds & (1u << ins[k].kind)) { ins[k].kind = K_INTERP; ins[k].flagsUse = FALL; ins[k].ends = true; n = k + 1; break; }
	// the flags' liveness (all live at the end)
	u8 live = FALL;
	for (int k = n - 1; k >= 0; k--)
	{
		Ins &in = ins[k];
		in.flagsNeed = in.flagsDef & live;
		in.liveAfter = live;
		if (in.cond < 14) live = (u8) (in.flagsUse | live);
		else live = (u8) (in.flagsUse | (live & ~in.flagsDef));
	}
	return n;
}

static Block *compile (Machine *m, Arm &c, u32 pc, bool thumb)
{
	Jit *j = m->jit;
	g_jitStats[0]++;
	if (j->nBlocks >= MAX_BLOCKS || j->size - j->pos < 16384) jitFlushAll (m);
	static Gen g;
	g.m = m; g.j = j; g.c = &c; g.cpu = c.num; g.thumb = thumb;
	g.n = decode (m, c, pc, thumb, g.ins);
	if (!g.n) return 0;
	// the registers: the most used ones in w20..w28
	int uses[16] = { 0 };
	for (int k = 0; k < g.n; k++)
	{
		u32 op = g.ins[k].op;
		if (!op) { if (g.ins[k].kind == K_THUMB_BL || g.ins[k].link) uses[14] += 1; continue; }
		uses[(op >> 16) & 15]++; uses[(op >> 12) & 15]++; uses[op & 15]++;
		if (g.ins[k].kind == K_BLOCK) { for (int r = 0; r < 16; r++) if (op & (1u << r)) uses[r]++; }
		else uses[(op >> 8) & 15]++;
	}
	for (int r = 0; r < 16; r++) g.hostOf[r] = -1;
	for (int h = 0; h < 9; h++)
	{
		int best = -1, bu = 0;
		for (int r = 0; r < 15; r++) if (g.hostOf[r] < 0 && uses[r] > bu) { bu = uses[r]; best = r; }
		if (best < 0 || bu < 2) break;
		g.hostOf[best] = (s8) (20 + h);
	}
	g.written = 0; g.flagsHost = false; g.flagsDirty = false; g.nStubs = 0;
	u32 *start = j->buf + j->pos;
	g.a.p = start;
	for (int r = 0; r < 15; r++) if (g.hostOf[r] >= 0) g.a.ldrw (g.hostOf[r], XC, g.roff (r));
	c.jitExit = false;
	bool ended = false;
	for (int k = 0; k < g.n && !ended; k++)
	{
		Ins &in = g.ins[k];
		u32 *skip = 0;
		bool exits = false;					// (the body always leaves)
		if (in.cond < 14)
		{
			g.loadFlags ();
			skip = g.a.bcond (in.cond ^ 1);
		}
		bool fh = g.flagsHost, fd = g.flagsDirty;
		if (in.kind == K_INTERP) { g.genInterp (k); exits = true; }
		else if (in.kind == K_THUMB_BL)
		{
			g.a.movw (T0, (in.addr + 4) | 1); g.commit (14, T0);
			g.exitTo (in.target, g.cycAt (k), in.toArm ? 0 : -1);
			exits = true;
		}
		else if (in.kind == K_B && in.toThumb)
		{
			g.a.movw (T0, in.addr + 4); g.commit (14, T0);
			g.exitTo (in.target, g.cycAt (k), 1);
			exits = true;
		}
		else if (!g.genOne (k)) { g.genInterp (k); exits = true; }
		else exits = in.ends;
		if (skip)
		{
			Asm::patch (skip, g.a.p);
			// the state after: the skipped path's, joined with the body's if it can come back
			if (exits) { g.flagsHost = fh; g.flagsDirty = fd; }
			else { g.flagsHost = fh && g.flagsHost; g.flagsDirty = fd || g.flagsDirty; }
		}
		else if (exits) ended = true;
	}
	if (!ended)
	{
		Ins &last = g.ins[g.n - 1];
		g.exitTo (last.addr + (thumb ? 2 : 4), g.cycAt (g.n - 1));
	}
	g.emitStubs ();
	flushCode (start, g.a.p);
	if (g_jitDumpHook) g_jitDumpHook (start, (int) (g.a.p - start), pc);
	j->pos = (u32) (g.a.p - j->buf);
	Block *b = &j->blocks[j->nBlocks++];
	b->pc = pc; b->cpu = (u8) c.num; b->thumb = thumb; b->valid = 1; b->code = start;
	Block **slot = &j->hash[c.num][(pc >> 1) & (HASH - 1)];
	b->hnext = *slot; *slot = b;
	FastEntry &fe = j->fast[c.num][(pc >> 1) & ((1 << FAST_BITS) - 1)];
	fe.key = pc | (thumb ? 1u : 0u); fe.code = start;
	u8 *host = c.num ? m->host7 (pc) : m->host9 (pc);
	b->chunk = host ? chunkOf (m, host) : -1;
	{
		const Ins &last = g.ins[g.n - 1];
		u32 bytes = last.addr - pc + (thumb ? (last.kind == K_THUMB_BL ? 4u : 2u) : 4u);
		b->hostLo = host - 3; b->hostHi = host + bytes;		// (a byte write just before an instruction's word: -3)
	}
	if (b->chunk >= 0)
	{
		b->cnext = j->chunkList[b->chunk]; j->chunkList[b->chunk] = b;
		if (!j->pageCount[b->chunk >> 4]++) protectPage (m, c.num ? m->host7 (pc & ~0x3FFFu) : m->host9 (pc & ~0x3FFFu));
	}
	else b->cnext = 0;
	return b;
}

bool jitAvailable () { return true; }
unsigned g_jitNoKinds;				// (tests: the kinds left to the interpreter, 1 << Kind)
unsigned g_jitNoDP;				// (tests: the data-processing ones: 1 << opcode, 16 S, 17 register shift, 18 immediate, 19 shifted, 20 thumb)
void (*g_jitDumpHook) (const u32 *code, int words, u32 pc);	// (tests: each block's code)

bool Machine::jitEnable (void *(*codeAlloc) (u32 size))
{
	if (jit) return true;
	if (!codeAlloc) return false;
	void *mem = codeAlloc (CODE_SIZE);
	if (!mem) return false;
	Jit *j = new Jit;
	j->buf = (u32 *) mem; j->size = CODE_SIZE / 4; j->pos = 0;
	j->blocks = new Block[MAX_BLOCKS];
	for (int c = 0; c < 2; c++) j->fast[c] = new FastEntry[1 << FAST_BITS];
	arm9.jitFast = j->fast[0]; arm7.jitFast = j->fast[1];
	// the entry and the exit
	Asm a; a.p = j->buf;
	u32 *enter = a.p;
	a.e (0xA9B97BFD);						// stp x29, x30, [sp, #-112]!
	a.e (0xA90153F3);						// stp x19, x20, [sp, #16]
	a.e (0xA9025BF5);						// stp x21, x22, [sp, #32]
	a.e (0xA90363F7);						// stp x23, x24, [sp, #48]
	a.e (0xA9046BF9);						// stp x25, x26, [sp, #64]
	a.e (0xA90573FB);						// stp x27, x28, [sp, #80]
	a.movx64 (XC, 0);
	a.br (1);
	u32 *exitStub = a.p;
	a.e (0xA94153F3);						// ldp x19, x20, [sp, #16]
	a.e (0xA9425BF5);
	a.e (0xA94363F7);
	a.e (0xA9446BF9);
	a.e (0xA94573FB);
	a.e (0xA8C77BFD);						// ldp x29, x30, [sp], #112
	a.e (0xD65F03C0);						// ret
	flushCode (j->buf, a.p);
	j->enter = (void (*) (Arm *, u32 *)) (void *) enter;
	j->exitStub = exitStub;
	j->helpers = (u32) (a.p - j->buf);
	j->pos = j->helpers;
	jit = j;
	jitFlushAll (this);
	arm9.jitOn = arm7.jitOn = true;
	useJit = true;
	return true;
}

void jitRun (Machine *m, Arm &c)
{
	Jit *j = m->jit;
	while (c.ts < c.target)
	{
		if (c.halted)
		{
			if (!m->wake (c.num)) { c.ts = c.target; break; }
			c.halted = false;
		}
		if (!(c.cpsr & 0x80) && m->irqLine (c.num)) c.irq ();
		if (m->pagesDirty) m->pagesUpdate ();
		bool thumb = c.cpsr & 0x20;
		u32 pc = c.r[15];
		Block *b = lookup (j, c.num, pc, thumb);
		if (!b) b = compile (m, c, pc, thumb);
		if (!b) { int k = thumb ? c.stepThumb () : c.stepArm (); c.ts += (s64) k << c.shift; continue; }
		c.jitExit = false;
		g_jitStats[5]++;
		j->enter (&c, b->code);
	}
}

#else	// (not an AArch64 host: no JIT)

bool jitAvailable () { return false; }
bool jitPageHasCode (Machine *, const u8 *) { return false; }
void jitRun (Machine *m, Arm &c) { (void) m; c.run (); }
void jitInvalidateHost (Machine *, const u8 *) {}
void jitFlushAll (Machine *) {}
bool Machine::jitEnable (void *(*codeAlloc) (u32 size)) { (void) codeAlloc; return false; }

#endif

} // namespace nds
