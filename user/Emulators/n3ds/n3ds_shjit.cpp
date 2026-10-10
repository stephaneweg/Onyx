//
// n3ds_shjit.cpp -- the PICA200's vertex shaders compiled to AArch64 (NEON), for the host's processor to run a
// vertex's shader as a function instead of interpreting it (n3ds_pica.cpp's runShader, which stays the reference
// and runs whatever is not compiled).
//
// A shader's code from an entry point is compiled as it is structured: an instruction's four components as one
// vector operation (the operands shuffled by a table lookup, negated; the result put into the components the
// descriptor names), a CALL's block compiled in place, an IF as two branches, a LOOP as a loop, a JMP forwards
// inside its block as a branch. The address registers, the loop's counter and the comparison's results live in
// processor registers; the boolean and integer uniforms are read when the code runs (changing them needs no new
// code). The results are the interpreter's to the bit (the same operations in the same order: the product that
// is zero when a factor is, the sums of the dot products left to right).
//
// Not compiled (the entry point then runs in the interpreter): DST, EX2, LG2, a jump backwards or out of its
// block, blocks nested deeper than 8, three loops inside each other.
//
// The function: fn (regs, uniforms, consts, ints, bools) -- regs: the shader's 32 input / temporary registers then
// its 16 outputs (4 floats each); uniforms: the 96 float uniforms; ints: the 4 integer uniforms (4 bytes each);
// bools: the boolean uniforms' bits. No stack, no call: x0-x4 the arguments, w9 w10 the address registers, w11 the
// loop's, w12 w13 the comparison's results, w5-w7 the loops' counts, w14-w17 and v0-v7 scratch.
//
// MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#include <stdlib.h>
#include <string.h>
#include "n3ds/n3ds.h"

extern "C" void *mmap (void *addr, size_t len, int prot, int flags, int fd, long off);

namespace n3ds
{

enum { CODE_WORDS = 4096, ARENA_BYTES = 1 << 20, ENTRIES = 32, CONSTS_SWIZZLE = 0, CONSTS_MASK = 256, CONSTS_ONE = 272, CONSTS_COUNT = 273 };

struct ShaderJit
{
	u32 *arena; u32 used;					// (words)
	struct { u32 entry; ShaderCode fn; bool tried; } entry[ENTRIES]; int entries;
	u8 consts[CONSTS_COUNT][16];
};

namespace
{

struct Asm
{
	u32 *w; u32 n, cap; bool full;
	void put (u32 x) { if (n < cap) w[n++] = x; else full = true; }
	// ---- vectors
	void ldrq (int t, int base, u32 off) { put (0x3DC00000u | (off / 16) << 10 | (u32) base << 5 | (u32) t); }
	void strq (int t, int base, u32 off) { put (0x3D800000u | (off / 16) << 10 | (u32) base << 5 | (u32) t); }
	void op3 (u32 code, int d, int a, int b) { put (code | (u32) b << 16 | (u32) a << 5 | (u32) d); }
	void fadd (int d, int a, int b) { op3 (0x4E20D400u, d, a, b); }
	void fmul (int d, int a, int b) { op3 (0x6E20DC00u, d, a, b); }
	void fcmge (int d, int a, int b) { op3 (0x6E20E400u, d, a, b); }		// a >= b
	void fcmgt (int d, int a, int b) { op3 (0x6EA0E400u, d, a, b); }		// a > b
	void orr (int d, int a, int b) { op3 (0x4EA01C00u, d, a, b); }
	void and_ (int d, int a, int b) { op3 (0x4E201C00u, d, a, b); }
	void bic (int d, int a, int b) { op3 (0x4E601C00u, d, a, b); }			// a & ~b
	void bsl (int d, int a, int b) { op3 (0x6E601C00u, d, a, b); }			// d = d ? a : b
	void bit (int d, int a, int m) { op3 (0x6EA01C00u, d, a, m); }			// d = m ? a : d
	void tbl (int d, int a, int idx) { op3 (0x4E000000u, d, a, idx); }
	void mov (int d, int a) { orr (d, a, a); }
	void fneg (int d, int a) { put (0x6EA0F800u | (u32) a << 5 | (u32) d); }
	void fcmeq0 (int d, int a) { put (0x4EA0D800u | (u32) a << 5 | (u32) d); }
	void frintm (int d, int a) { put (0x4E219800u | (u32) a << 5 | (u32) d); }
	void dup (int d, int a, int lane) { put (0x4E000400u | (u32) (lane << 3 | 4) << 16 | (u32) a << 5 | (u32) d); }	// every lane = a's lane
	void lane (int d, int a, int l) { put (0x5E000400u | (u32) (l << 3 | 4) << 16 | (u32) a << 5 | (u32) d); }	// the scalar d = a's lane
	// ---- scalars
	void fadds (int d, int a, int b) { op3 (0x1E202800u, d, a, b); }
	void fdivs (int d, int a, int b) { op3 (0x1E201800u, d, a, b); }
	void fsqrts (int d, int a) { put (0x1E21C000u | (u32) a << 5 | (u32) d); }
	void fone (int d) { put (0x1E2E1000u | (u32) d); }
	void fcvtzs (int w, int s) { put (0x1E380000u | (u32) s << 5 | (u32) w); }
	void fcmps (int a, int b) { put (0x1E202000u | (u32) b << 16 | (u32) a << 5); }
	// ---- integers
	void movi (int d, u32 imm16) { put (0x52800000u | imm16 << 5 | (u32) d); }
	void movr (int d, int a) { put (0x2A0003E0u | (u32) a << 16 | (u32) d); }
	void addi (int d, int a, u32 imm) { put (0x11000000u | imm << 10 | (u32) a << 5 | (u32) d); }
	void subi (int d, int a, u32 imm) { put (0x51000000u | imm << 10 | (u32) a << 5 | (u32) d); }
	void addr (int d, int a, int b) { put (0x0B000000u | (u32) b << 16 | (u32) a << 5 | (u32) d); }
	void and7f (int d, int a) { put (0x12001800u | (u32) a << 5 | (u32) d); }
	void eor1 (int d, int a) { put (0x52000000u | (u32) a << 5 | (u32) d); }
	void orrw (int d, int a, int b) { put (0x2A000000u | (u32) b << 16 | (u32) a << 5 | (u32) d); }
	void andw (int d, int a, int b) { put (0x0A000000u | (u32) b << 16 | (u32) a << 5 | (u32) d); }
	void cmpi (int a, u32 imm) { put (0x7100001Fu | imm << 10 | (u32) a << 5); }
	void cmpr (int a, int b) { put (0x6B00001Fu | (u32) b << 16 | (u32) a << 5); }
	void csel (int d, int a, int b, u32 cond) { put (0x1A800000u | (u32) b << 16 | cond << 12 | (u32) a << 5 | (u32) d); }
	void cset (int d, u32 cond) { put (0x1A9F07E0u | (cond ^ 1) << 12 | (u32) d); }
	void ldrb (int t, int base, u32 off) { put (0x39400000u | off << 10 | (u32) base << 5 | (u32) t); }
	void addx_uxtw4 (int d, int base, int idx) { put (0x8B205000u | (u32) idx << 16 | (u32) base << 5 | (u32) d); }	// d = base + (u64) idx * 16
	void ret () { put (0xD65F03C0u); }
	// ---- branches: emitted with no target, patched (the word's index is kept)
	u32 b () { put (0x14000000u); return n - 1; }
	u32 bcond (u32 cond) { put (0x54000000u | cond); return n - 1; }
	u32 cbz (int t) { put (0x34000000u | (u32) t); return n - 1; }
	u32 cbnz (int t) { put (0x35000000u | (u32) t); return n - 1; }
	u32 tbz (int t, u32 bitn) { put (0x36000000u | bitn << 19 | (u32) t); return n - 1; }
	u32 tbnz (int t, u32 bitn) { put (0x37000000u | bitn << 19 | (u32) t); return n - 1; }
	void patch (u32 at, u32 target)
	{
		if (at >= cap) return;
		const s32 d = (s32) target - (s32) at;
		const u32 x = w[at];
		if ((x & 0xFC000000u) == 0x14000000u) w[at] = 0x14000000u | ((u32) d & 0x03FFFFFFu);
		else if ((x & 0x7E000000u) == 0x36000000u) { if (d < -8192 || d > 8191) full = true; w[at] = (x & 0xFFF8001Fu) | ((u32) d & 0x3FFFu) << 5; }
		else w[at] = (x & 0xFF00001Fu) | ((u32) d & 0x7FFFFu) << 5;		// b.cond, cbz, cbnz
	}
};
enum { EQ = 0, NE = 1, HS = 2, LO = 3, MI = 4, LS = 9, GE = 10, GT = 12, AL = 14 };
enum { X_REGS = 0, X_UNI = 1, X_CONSTS = 2, X_INTS = 3, W_BOOLS = 4, W_A0 = 9, W_A1 = 10, W_AL = 11, W_CMP0 = 12, W_CMP1 = 13, W_T0 = 14, W_T1 = 15, X_T2 = 16 };

struct Compiler
{
	Asm a;
	const u32 *code, *opdesc;
	bool failed;
	u32 ends[256]; int nEnds;				// the branches to the function's end (END)
	int loops;

	void fail () { failed = true; }
	// an operand's vector into v<t>: register r (the instruction's number, 0x00-0x7F) moved by the index register
	// idx (0: not), shuffled by sw, negated
	void operand (int t, u32 r, u32 idx, u32 sw, bool neg)
	{
		if (!idx)
		{
			r &= 0x7F;
			if (r < 0x20) a.ldrq (t, X_REGS, r * 16);
			else { r -= 0x20; a.ldrq (t, X_UNI, (r < 96 ? r : 95) * 16); }
		}
		else
		{
			const int wi = idx == 1 ? W_A0 : idx == 2 ? W_A1 : W_AL;
			if (r) a.addi (W_T0, wi, r); else a.movr (W_T0, wi);
			a.and7f (W_T0, W_T0);
			a.cmpi (W_T0, 0x20);
			const u32 toUni = a.bcond (HS);
			a.addx_uxtw4 (X_T2, X_REGS, W_T0);
			const u32 done = a.b ();
			a.patch (toUni, a.n);
			a.subi (W_T0, W_T0, 0x20);
			a.movi (W_T1, 95);
			a.cmpr (W_T0, W_T1);
			a.csel (W_T0, W_T0, W_T1, LS);
			a.addx_uxtw4 (X_T2, X_UNI, W_T0);
			a.patch (done, a.n);
			a.ldrq (t, X_T2, 0);
		}
		if (sw != 0x1B) { a.ldrq (7, X_CONSTS, (CONSTS_SWIZZLE + sw) * 16); a.tbl (t, t, 7); }
		if (neg) a.fneg (t, t);
	}
	// v<d> = v<x> * v<y>, zero where a factor is (v6, v7 used)
	void mulz (int d, int x, int y)
	{
		a.fcmeq0 (6, x); a.fcmeq0 (7, y); a.orr (6, 6, 7);
		a.fmul (d, x, y);
		a.bic (d, d, 6);
	}
	// v<v> into the destination register's components `mask` names
	void store (int v, u32 d, u32 mask)
	{
		const int base = X_REGS; const u32 off = d < 0x10 ? 512 + d * 16 : d * 16;
		if (mask == 15) { a.strq (v, base, off); return; }
		if (!mask) return;
		a.ldrq (5, base, off);
		a.ldrq (6, X_CONSTS, (CONSTS_MASK + mask) * 16);
		a.bit (5, v, 6);
		a.strq (5, base, off);
	}
	// the sum of v<m>'s first `n` lanes, left to right, (+ the scalar s<extra> when >= 0) into every lane of v<d>
	void sumLanes (int d, int m, int n, int extra)
	{
		a.lane (4, m, 0);
		for (int i = 1; i < n; i++) { a.lane (5, m, i); a.fadds (4, 4, 5); }
		if (extra >= 0) a.fadds (4, 4, extra);
		a.dup (d, 4, 0);
	}
	bool arithmetic (u32 ins)
	{
		const u32 op = ins >> 26;
		u32 d, idx, r1, r2, r3 = 0, desc; int idxOn;
		const bool mad = op >= 0x30;
		if (mad)
		{
			d = ins >> 24 & 0x1F; idx = ins >> 22 & 3; r1 = ins >> 17 & 0x1F; desc = ins & 0x1F;
			if (op >= 0x38) { r2 = ins >> 10 & 0x7F; r3 = ins >> 5 & 0x1F; idxOn = 1; }
			else { r2 = ins >> 12 & 0x1F; r3 = ins >> 5 & 0x7F; idxOn = 2; }
		}
		else
		{
			d = ins >> 21 & 0x1F; idx = ins >> 19 & 3; desc = ins & 0x7F;
			if (op >= 0x18 && op <= 0x1B) { r1 = ins >> 14 & 0x1F; r2 = ins >> 7 & 0x7F; idxOn = 1; }
			else { r1 = ins >> 12 & 0x7F; r2 = ins >> 7 & 0x1F; idxOn = 0; }
		}
		const u32 od = opdesc[desc & 127];
		const u32 mask = od & 15;
		operand (0, r1, idxOn == 0 ? idx : 0, od >> 5 & 0xFF, (od >> 4 & 1) != 0);
		operand (1, r2, idxOn == 1 ? idx : 0, od >> 14 & 0xFF, (od >> 13 & 1) != 0);
		if (mad)
		{
			operand (2, r3, idxOn == 2 ? idx : 0, od >> 23 & 0xFF, (od >> 22 & 1) != 0);
			mulz (3, 0, 1);
			a.fadd (3, 3, 2);
			store (3, d, mask);
			return true;
		}
		switch (op)
		{
		case 0x00: a.fadd (3, 0, 1); break;								// ADD
		case 0x01: mulz (3, 0, 1); sumLanes (3, 3, 3, -1); break;					// DP3
		case 0x02: mulz (3, 0, 1); sumLanes (3, 3, 4, -1); break;					// DP4
		case 0x03: case 0x18: mulz (3, 0, 1); a.lane (2, 1, 3); sumLanes (3, 3, 3, 2); break;		// DPH
		case 0x08: mulz (3, 0, 1); break;								// MUL
		case 0x09: case 0x1A: a.fcmge (3, 0, 1); a.ldrq (2, X_CONSTS, CONSTS_ONE * 16); a.and_ (3, 3, 2); break;	// SGE
		case 0x0A: case 0x1B: a.fcmgt (3, 1, 0); a.ldrq (2, X_CONSTS, CONSTS_ONE * 16); a.and_ (3, 3, 2); break;	// SLT
		case 0x0B: a.frintm (3, 0); break;								// FLR
		case 0x0C: a.fcmgt (3, 0, 1); a.bsl (3, 0, 1); break;						// MAX
		case 0x0D: a.fcmgt (3, 1, 0); a.bsl (3, 0, 1); break;						// MIN
		case 0x0E: a.fone (4); a.fdivs (4, 4, 0); a.dup (3, 4, 0); break;				// RCP
		case 0x0F: a.fsqrts (5, 0); a.fone (4); a.fdivs (4, 4, 5); a.dup (3, 4, 0); break;		// RSQ
		case 0x12:											// MOVA
			if (mask & 8) a.fcvtzs (W_A0, 0);
			if (mask & 4) { a.lane (4, 0, 1); a.fcvtzs (W_A1, 4); }
			return true;
		case 0x13: a.mov (3, 0); break;									// MOV
		case 0x2E: case 0x2F:										// CMP
			for (int i = 0; i < 2; i++)
			{
				const u32 c = i == 0 ? ins >> 24 & 7 : ins >> 21 & 7;
				const int w = i == 0 ? W_CMP0 : W_CMP1;
				if (c > 5) { a.movi (w, 1); continue; }
				if (i == 0) a.fcmps (0, 1);
				else { a.lane (4, 0, 1); a.lane (5, 1, 1); a.fcmps (4, 5); }
				static const u32 COND[6] = { EQ, NE, MI, LS, GT, GE };
				a.cset (w, COND[c]);
			}
			return true;
		default: return false;										// (DST, EX2, LG2, unknown)
		}
		store (3, d, mask);
		return true;
	}
	// w<W_T0> = the comparison's two results against the instruction's (ins bits 25, 24), combined (bits 23-22)
	void condition (u32 ins)
	{
		if (ins >> 25 & 1) a.movr (W_T0, W_CMP0); else a.eor1 (W_T0, W_CMP0);
		if (ins >> 24 & 1) a.movr (W_T1, W_CMP1); else a.eor1 (W_T1, W_CMP1);
		switch (ins >> 22 & 3)
		{
		case 0: a.orrw (W_T0, W_T0, W_T1); break;
		case 1: a.andw (W_T0, W_T0, W_T1); break;
		case 2: break;
		default: a.movr (W_T0, W_T1); break;
		}
	}
	// the instructions pc .. end - 1
	void block (u32 pc, u32 end, int depth)
	{
		struct Jump { u32 at, target; } jumps[32]; int nJumps = 0;
		if (depth > 8 || end > CODE_WORDS || pc > end) { fail (); return; }
		while (!failed && !a.full)
		{
			for (int i = 0; i < nJumps; i++) if (jumps[i].target == pc) { a.patch (jumps[i].at, a.n); jumps[i] = jumps[--nJumps]; i--; }
			if (pc >= end) break;
			const u32 ins = code[pc], op = ins >> 26;
			const u32 dst = ins >> 10 & 0xFFF, num = ins & 0xFF;
			if (op == 0x22)												// END
			{
				if (depth == 0 && !nJumps) break;				// (the function's own: nothing after it is reached)
				if (nEnds >= 256) { fail (); break; }
				ends[nEnds++] = a.b (); pc++;
				continue;
			}
			if (op == 0x21 || op == 0x20 || op == 0x2A || op == 0x2B || op == 0x23) { pc++; continue; }		// NOP, BREAK, EMIT, SETEMIT, BREAKC
			if (op >= 0x24 && op <= 0x2D)
			{
				// the condition: a branch over what follows when it is false (-1: none)
				s32 skip = -1;
				const bool onCmp = op == 0x25 || op == 0x28 || op == 0x2C, onBool = op == 0x26 || op == 0x27 || op == 0x2D;
				switch (op)
				{
				case 0x24: case 0x25: case 0x26:				// CALL: num instructions at dst
					if (onCmp) { condition (ins); skip = (s32) a.cbz (W_T0); }
					else if (onBool) skip = (s32) a.tbz (W_BOOLS, ins >> 22 & 15);
					block (dst, dst + num, depth + 1);
					if (skip >= 0) a.patch ((u32) skip, a.n);
					pc++;
					break;
				case 0x27: case 0x28:						// IF: [pc + 1, dst) or else [dst, dst + num)
				{
					if (dst < pc + 1) { fail (); break; }
					if (onCmp) { condition (ins); skip = (s32) a.cbz (W_T0); }
					else skip = (s32) a.tbz (W_BOOLS, ins >> 22 & 15);
					block (pc + 1, dst, depth + 1);
					const u32 over = a.b ();
					a.patch ((u32) skip, a.n);
					block (dst, dst + num, depth + 1);
					a.patch (over, a.n);
					pc = dst + num;
					break;
				}
				case 0x29:							// LOOP: to dst included, an integer uniform's (count, start, step)
				{
					if (loops >= 3 || dst < pc) { fail (); break; }
					const int wc = 5 + loops;
					const u32 iu = (ins >> 22 & 3) * 4;
					a.ldrb (wc, X_INTS, iu);
					a.ldrb (W_AL, X_INTS, iu + 1);
					const u32 top = a.n;
					loops++;
					block (pc + 1, dst + 1, depth + 1);
					loops--;
					const u32 out = a.cbz (wc);
					a.subi (wc, wc, 1);
					a.ldrb (W_T0, X_INTS, iu + 2);
					a.addr (W_AL, W_AL, W_T0);
					a.patch (a.b (), top);
					a.patch (out, a.n);
					pc = dst + 1;
					break;
				}
				default:							// JMPC, JMPU: forwards, inside this block
				{
					if (dst <= pc || dst > end || nJumps >= 32) { fail (); break; }
					u32 at;
					if (op == 0x2C) { condition (ins); at = a.cbnz (W_T0); }
					else at = (num & 1) ? a.tbz (W_BOOLS, ins >> 22 & 15) : a.tbnz (W_BOOLS, ins >> 22 & 15);
					jumps[nJumps].at = at; jumps[nJumps].target = dst; nJumps++;
					pc++;
					break;
				}
				}
				continue;
			}
			if (!arithmetic (ins)) { fail (); break; }
			pc++;
		}
		if (nJumps) fail ();
	}
};

} // namespace

// The entry point's function, compiled at its first asking -- 0: it is not compiled (the interpreter's).
ShaderCode shaderCompile (ShaderJit **pj, const u32 *code, const u32 *opdesc, u32 entry)
{
	ShaderJit *j = *pj;
	if (!j)
	{
		j = (ShaderJit *) calloc (1, sizeof (ShaderJit));
		if (!j) return 0;
		void *m = mmap (0, ARENA_BYTES, 7, 0x22, -1, 0);		// (read, write, execute; private, anonymous)
		if (!m || m == (void *) -1) { free (j); return 0; }
		j->arena = (u32 *) m;
		for (int sw = 0; sw < 256; sw++)
			for (int i = 0; i < 4; i++) for (int b = 0; b < 4; b++) j->consts[CONSTS_SWIZZLE + sw][i * 4 + b] = (u8) ((sw >> (6 - 2 * i) & 3) * 4 + b);
		for (int m4 = 0; m4 < 16; m4++)
			for (int i = 0; i < 4; i++) for (int b = 0; b < 4; b++) j->consts[CONSTS_MASK + m4][i * 4 + b] = (m4 & (8 >> i)) ? 0xFF : 0;
		const float one = 1.0f;
		for (int i = 0; i < 4; i++) memcpy (j->consts[CONSTS_ONE] + i * 4, &one, 4);
		*pj = j;
	}
	for (int i = 0; i < j->entries; i++) if (j->entry[i].entry == entry) return j->entry[i].fn;
	if (j->entries >= ENTRIES) return 0;
	const int slot = j->entries++;
	j->entry[slot].entry = entry; j->entry[slot].fn = 0;
	static Compiler c;
	c.a.w = j->arena + j->used; c.a.n = 0; c.a.cap = ARENA_BYTES / 4 - j->used; c.a.full = false;
	c.code = code; c.opdesc = opdesc; c.failed = false; c.nEnds = 0; c.loops = 0;
	if (c.a.cap < 4096) return 0;
	c.a.movi (W_A0, 0); c.a.movi (W_A1, 0); c.a.movi (W_AL, 0); c.a.movi (W_CMP0, 0); c.a.movi (W_CMP1, 0);
	c.block (entry, CODE_WORDS, 0);
	for (int i = 0; i < c.nEnds; i++) c.a.patch (c.ends[i], c.a.n);
	c.a.ret ();
	if (c.failed || c.a.full) return 0;
	__builtin___clear_cache ((char *) c.a.w, (char *) (c.a.w + c.a.n));
	j->entry[slot].fn = (ShaderCode) (void *) c.a.w;
	j->used += c.a.n;
	return j->entry[slot].fn;
}

// The shader's code or its descriptors changed: what was compiled is forgotten.
void shaderJitReset (ShaderJit *j) { if (j) { j->entries = 0; j->used = 0; } }
const void *shaderJitConsts (const ShaderJit *j) { return j->consts; }

} // namespace n3ds
