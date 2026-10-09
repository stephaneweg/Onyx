//
// nds/nds_cpu.cpp -- the two processors' interpreter: the ARM946E-S (ARMv5TE: BLX, CLZ, the
// saturating and the 16-bit multiplies, LDRD / STRD, the CP15 with its tightly-coupled memories)
// and the ARM7TDMI (ARMv4T). r[15] holds the address of the instruction being run + 8 (ARM) / + 4
// (Thumb) while it runs. The JIT (nds_jit.cpp) calls execArm / execThumb for what it leaves here.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (see nds.h).
//
#include "nds/nds.h"

namespace nds {
#ifdef NDS_DEBUG
u32 g_watch[2][2]; bool g_watchHit;
#endif

static const u32 FN = 0x80000000u, FZ = 0x40000000u, FC = 0x20000000u, FV = 0x10000000u, FQ = 0x08000000u, FI = 0x80u, FT = 0x20u;

static bool s_cond[16][16];					// [cond][NZCV]
static bool s_condInit = false;
static void condInit ()
{
	for (int f = 0; f < 16; f++)
	{
		bool n = f & 8, z = f & 4, c = f & 2, v = f & 1;
		bool t[16] = { z, !z, c, !c, n, !n, v, !v, c && !z, !c || z, n == v, n != v, !z && n == v, z || n != v, true, false };
		for (int k = 0; k < 16; k++) s_cond[k][f] = t[k];
	}
	s_condInit = true;
}

static inline u32 ror (u32 v, int n) { n &= 31; return n ? (v >> n) | (v << (32 - n)) : v; }

#define SETNZ(v)	(cpsr = (cpsr & ~(FN | FZ)) | ((v) & FN) | ((v) ? 0 : FZ))
#define SETC(b)	(cpsr = (b) ? (cpsr | FC) : (cpsr & ~FC))
#define SETV(b)	(cpsr = (b) ? (cpsr | FV) : (cpsr & ~FV))
#define CARRY	((cpsr >> 29) & 1)

bool Arm::cond (u32 c) const { return s_cond[c & 15][cpsr >> 28]; }

void Arm::reset (Machine *mm, int n)
{
	if (!s_condInit) condInit ();
	m = mm; num = n; shift = n ? 1 : 0;
	for (int i = 0; i < 16; i++) r[i] = 0;
	for (int i = 0; i < 6; i++) bankR13[i] = bankR14[i] = bankSpsr[i] = 0;
	for (int i = 0; i < 5; i++) bankFiq[i] = bankUsr[i] = 0;
	cpsr = 0xD3; spsr = 0;
	ts = 0; target = 0;
	halted = false; branched = false; cyc = 0;
	intrWait = false; intrMask = 0;
	cpCtl = 0x00012078; cpDtcm = 0; cpItcm = 0;
	dtcmBase = 0xFFFFFFFF; dtcmSize = 0; itcmSize = 0;
	for (int i = 0; i < 64; i++) cpRegs[i] = 0;
	excBase = n ? 0 : 0xFFFF0000;
	jitOn = false;
}

int Arm::modeBank (u32 md) const
{
	switch (md & 0x1F) { case 0x11: return 1; case 0x12: return 2; case 0x13: return 3; case 0x17: return 4; case 0x1B: return 5; }
	return 0;
}

void Arm::setMode (u32 md)
{
	int ob = modeBank (cpsr), nb = modeBank (md);
	if (ob != nb)
	{
		bankR13[ob] = r[13]; bankR14[ob] = r[14]; bankSpsr[ob] = spsr;
		if (ob == 1) for (int i = 0; i < 5; i++) { bankFiq[i] = r[8 + i]; r[8 + i] = bankUsr[i]; }
		if (nb == 1) for (int i = 0; i < 5; i++) { bankUsr[i] = r[8 + i]; r[8 + i] = bankFiq[i]; }
		r[13] = bankR13[nb]; r[14] = bankR14[nb]; spsr = bankSpsr[nb];
	}
	cpsr = (cpsr & ~0x1Fu) | (md & 0x1F);
}

void Arm::setCpsr (u32 v)
{
	setMode (v | 0x10);
	cpsr = v | 0x10;
}

void Arm::setPC (u32 v)
{
	r[15] = (cpsr & FT) ? v & ~1u : v & ~3u;
	branched = true;
}

void Arm::jumpX (u32 v)
{
	if (v & 1) cpsr |= FT; else cpsr &= ~FT;
	setPC (v);
}

void Arm::irq ()
{
	u32 ret = r[15];					// the next instruction
	u32 old = cpsr;
	setMode (0x12);
	spsr = old;
	cpsr = (cpsr | FI) & ~FT;
	r[14] = ret + 4;
	r[15] = excBase + 0x18;
	halted = false;
}

void Arm::undefined ()
{
	// (no handler: the instruction is skipped)
}

// ---- memory -------------------------------------------------------------------------------------------------
u32 Arm::read32 (u32 a)
{
	u8 *p = m->rdPage[num][a >> Machine::PAGE_BITS];
	if (p) return *(ua32 *) (p + (a & 0x3FFC));
	return num ? m->a7Read32 (a) : m->a9Read32 (a);
}
u32 Arm::read16 (u32 a)
{
	u8 *p = m->rdPage[num][a >> Machine::PAGE_BITS];
	if (p) return *(ua16 *) (p + (a & 0x3FFE));
	return num ? m->a7Read16 (a) : m->a9Read16 (a);
}
u32 Arm::read8 (u32 a)
{
	u8 *p = m->rdPage[num][a >> Machine::PAGE_BITS];
	if (p) return p[a & 0x3FFF];
	return num ? m->a7Read8 (a) : m->a9Read8 (a);
}
void Arm::write32 (u32 a, u32 v)
{
	u8 *p = m->wrPage[num][a >> Machine::PAGE_BITS];
	if (p) { *(ua32 *) (p + (a & 0x3FFC)) = v; return; }
	if (num) m->a7Write32 (a, v); else m->a9Write32 (a, v);
}
void Arm::write16 (u32 a, u32 v)
{
	u8 *p = m->wrPage[num][a >> Machine::PAGE_BITS];
	if (p) { *(ua16 *) (p + (a & 0x3FFE)) = (u16) v; return; }
	if (num) m->a7Write16 (a, v); else m->a9Write16 (a, v);
}
void Arm::write8 (u32 a, u32 v)
{
	u8 *p = m->wrPage[num][a >> Machine::PAGE_BITS];
	if (p) { p[a & 0x3FFF] = (u8) v; return; }
	if (num) m->a7Write8 (a, v); else m->a9Write8 (a, v);
}
u32 Arm::fetch32 (u32 a) { return read32 (a); }
u32 Arm::fetch16 (u32 a) { return read16 (a); }

// ---- the barrel shifter -----------------------------------------------------------------------------------------
u32 Arm::shiftImm (u32 op, bool &c)
{
	u32 v = r[op & 15];
	int amt = (op >> 7) & 31;
	switch ((op >> 5) & 3)
	{
	case 0: if (amt) { c = (v >> (32 - amt)) & 1; v <<= amt; } return v;
	case 1: if (amt) { c = (v >> (amt - 1)) & 1; return v >> amt; } c = v >> 31; return 0;
	case 2: if (amt) { c = ((s32) v >> (amt - 1)) & 1; return (u32) ((s32) v >> amt); } c = v >> 31; return (u32) ((s32) v >> 31);
	default:
		if (amt) { c = (v >> (amt - 1)) & 1; return ror (v, amt); }
		{ u32 res = (v >> 1) | ((u32) CARRY << 31); c = v & 1; return res; }	// RRX
	}
}

u32 Arm::shiftReg (u32 op, bool &c)
{
	int rm = op & 15;
	u32 v = r[rm] + (rm == 15 ? 4 : 0);
	u32 amt = r[(op >> 8) & 15] & 0xFF;
	if (!amt) return v;
	switch ((op >> 5) & 3)
	{
	case 0:
		if (amt < 32) { c = (v >> (32 - amt)) & 1; return v << amt; }
		c = amt == 32 ? (v & 1) : 0; return 0;
	case 1:
		if (amt < 32) { c = (v >> (amt - 1)) & 1; return v >> amt; }
		c = amt == 32 ? (v >> 31) : 0; return 0;
	case 2:
		if (amt < 32) { c = ((s32) v >> (amt - 1)) & 1; return (u32) ((s32) v >> amt); }
		c = v >> 31; return (u32) ((s32) v >> 31);
	default:
		amt &= 31;
		if (!amt) { c = v >> 31; return v; }
		c = (v >> (amt - 1)) & 1; return ror (v, (int) amt);
	}
}

// ---- ARM ------------------------------------------------------------------------------------------------------
void Arm::armDataProc (u32 op)
{
	int opc = (op >> 21) & 15;
	bool S = (op >> 20) & 1;
	int rn = (op >> 16) & 15, rd = (op >> 12) & 15;
	bool c = CARRY;
	u32 b;
	if (op & 0x02000000)
	{
		int rot = ((op >> 8) & 15) * 2;
		b = ror (op & 0xFF, rot);
		if (rot) c = b >> 31;
	}
	else if (op & 0x10) { b = shiftReg (op, c); cyc++; }
	else b = shiftImm (op, c);
	u32 a = r[rn];
	if (rn == 15 && (op & 0x02000010) == 0x10) a += 4;		// (a register shift: pc + 12)
	u32 res = 0;
	bool arith = false, cOut = c, vOut = false;
	switch (opc)
	{
	case 0: case 8: res = a & b; break;
	case 1: case 9: res = a ^ b; break;
	case 2: case 10: res = a - b; arith = true; cOut = a >= b; vOut = ((a ^ b) & (a ^ res)) >> 31; break;
	case 3: res = b - a; arith = true; cOut = b >= a; vOut = ((b ^ a) & (b ^ res)) >> 31; break;
	case 4: case 11: res = a + b; arith = true; cOut = res < a; vOut = (~(a ^ b) & (a ^ res)) >> 31; break;
	case 5: { u64 t = (u64) a + b + CARRY; res = (u32) t; arith = true; cOut = t >> 32; vOut = (~(a ^ b) & (a ^ res)) >> 31; break; }
	case 6: { u32 nc = 1 - CARRY; res = a - b - nc; arith = true; cOut = (u64) a >= (u64) b + nc; vOut = ((a ^ b) & (a ^ res)) >> 31; break; }
	case 7: { u32 nc = 1 - CARRY; res = b - a - nc; arith = true; cOut = (u64) b >= (u64) a + nc; vOut = ((b ^ a) & (b ^ res)) >> 31; break; }
	case 12: res = a | b; break;
	case 13: res = b; break;
	case 14: res = a & ~b; break;
	case 15: res = ~b; break;
	}
	bool test = opc >= 8 && opc <= 11;
	if (rd == 15 && S)
	{
		if (modeBank (cpsr)) setCpsr (spsr);			// (User / System have no SPSR)
		if (!test) setPC (res);
		return;
	}
	if (S)
	{
		SETNZ (res);
		SETC (cOut);
		if (arith) SETV (vOut);
	}
	if (!test)
	{
		if (rd == 15) { setPC (res); cyc += 2; }
		else r[rd] = res;
	}
}

void Arm::armMul (u32 op)
{
	int rd = (op >> 16) & 15, rn = (op >> 12) & 15, rs = (op >> 8) & 15, rm = op & 15;
	u32 res = r[rm] * r[rs];
	if (op & 0x00200000) res += r[rn];
	cyc += num ? 2 : 1;
	r[rd] = res;
	if (op & 0x00100000) SETNZ (res);
}

void Arm::armMulLong (u32 op)
{
	int hi = (op >> 16) & 15, lo = (op >> 12) & 15, rs = (op >> 8) & 15, rm = op & 15;
	u64 res;
	if (op & 0x00400000) res = (u64) ((s64) (s32) r[rm] * (s64) (s32) r[rs]);
	else res = (u64) r[rm] * (u64) r[rs];
	if (op & 0x00200000) res += ((u64) r[hi] << 32) | r[lo];
	cyc += num ? 3 : 2;
	r[lo] = (u32) res; r[hi] = (u32) (res >> 32);
	if (op & 0x00100000) cpsr = (cpsr & ~(FN | FZ)) | ((u32) (res >> 32) & FN) | (res ? 0 : FZ);
}

static inline s32 sat32 (s64 v, bool &q)
{
	if (v > 0x7FFFFFFFll) { q = true; return 0x7FFFFFFF; }
	if (v < -0x80000000ll) { q = true; return (s32) 0x80000000; }
	return (s32) v;
}

// the ARMv5 additions in the multiply / misc space (ARM9 only; the ARM7 sees them as undefined)
void Arm::armMisc (u32 op)
{
	if (num) { undefined (); return; }
	int rd = (op >> 12) & 15, rn = (op >> 16) & 15, rm = op & 15, rs = (op >> 8) & 15;
	if ((op & 0x0FFF0FF0) == 0x016F0F10)			// CLZ
	{
		u32 v = r[rm];
		r[rd] = v ? (u32) __builtin_clz (v) : 32;
		return;
	}
	if ((op & 0x0F900FF0) == 0x01000050)			// QADD / QSUB / QDADD / QDSUB
	{
		bool q = false;
		s32 a = (s32) r[rm], b = (s32) r[rn];
		if (op & 0x00400000) b = sat32 ((s64) b * 2, q);
		s32 res = (op & 0x00200000) ? sat32 ((s64) a - b, q) : sat32 ((s64) a + b, q);
		r[rd] = (u32) res;
		if (q) cpsr |= FQ;
		return;
	}
	if ((op & 0x0F900090) == 0x01000080)			// the signed 16 x 16 multiplies
	{
		int dst = (op >> 16) & 15, acc = (op >> 12) & 15;
		s32 a = (op & 0x20) ? (s32) r[rm] >> 16 : (s32) (s16) r[rm];
		s32 b = (op & 0x40) ? (s32) r[rs] >> 16 : (s32) (s16) r[rs];
		switch ((op >> 21) & 3)
		{
		case 0:						// SMLAxy
		{
			s64 res = (s64) (a * b) + (s32) r[acc];
			r[dst] = (u32) res;
			if (res != (s32) res) cpsr |= FQ;
			break;
		}
		case 1:						// SMLAWy / SMULWy
		{
			s32 w = (op & 0x40) ? (s32) r[rs] >> 16 : (s32) (s16) r[rs];
			s32 res = (s32) (((s64) (s32) r[rm] * w) >> 16);
			if (op & 0x20) r[dst] = (u32) res;		// SMULWy
			else
			{
				s64 t = (s64) res + (s32) r[acc];
				r[dst] = (u32) t;
				if (t != (s32) t) cpsr |= FQ;
			}
			break;
		}
		case 2:						// SMLALxy
		{
			s64 res = (s64) (((u64) r[dst] << 32) | r[acc]) + (s64) (a * b);
			r[acc] = (u32) res; r[dst] = (u32) ((u64) res >> 32);
			cyc++;
			break;
		}
		default: r[dst] = (u32) (a * b); break;	// SMULxy
		}
		return;
	}
	if ((op & 0x0FFFFFF0) == 0x012FFF30)			// BLX register
	{
		u32 t = r[rm];
		r[14] = r[15] - 4;
		jumpX (t);
		return;
	}
	undefined ();
}

void Arm::armHalf (u32 op)
{
	bool P = op & (1 << 24), U = op & (1 << 23), W = op & (1 << 21), L = op & (1 << 20);
	int rn = (op >> 16) & 15, rd = (op >> 12) & 15, sh = (op >> 5) & 3;
	u32 off = (op & (1 << 22)) ? (((op >> 4) & 0xF0) | (op & 0xF)) : r[op & 15];
	u32 base = r[rn];
	u32 eff = U ? base + off : base - off;
	u32 at = P ? eff : base;
	cyc++;
	if (!L && sh >= 2)					// LDRD / STRD (ARMv5)
	{
		if (num) { undefined (); return; }
		if (rd & 1) { undefined (); return; }
		if (sh == 2)
		{
			u32 v0 = read32 (at & ~3u), v1 = read32 ((at & ~3u) + 4);
			if ((W || !P) && rn != rd && rn != rd + 1) r[rn] = eff;
			r[rd] = v0;
			if (rd + 1 == 15) setPC (v1); else r[rd + 1] = v1;
		}
		else
		{
			write32 (at & ~3u, r[rd]);
			write32 ((at & ~3u) + 4, r[rd + 1] + (rd + 1 == 15 ? 4 : 0));
			if (W || !P) r[rn] = eff;
		}
		cyc++;
		return;
	}
	if (L)
	{
		u32 v;
		if (num)
		{
			if (sh == 1) { v = read16 (at & ~1u); if (at & 1) v = ror (v, 8); }
			else if (sh == 2) v = (u32) (s32) (s8) read8 (at);
			else v = (at & 1) ? (u32) (s32) (s8) read8 (at) : (u32) (s32) (s16) read16 (at);
		}
		else
		{
			if (sh == 1) v = read16 (at & ~1u);
			else if (sh == 2) v = (u32) (s32) (s8) read8 (at);
			else v = (u32) (s32) (s16) read16 (at & ~1u);
		}
		if ((W || !P) && rn != rd) r[rn] = eff;
		if (rd == 15) setPC (v); else r[rd] = v;
	}
	else
	{
		if (sh == 1) write16 (at & ~1u, (u16) (r[rd] + (rd == 15 ? 4 : 0)));
		if (W || !P) r[rn] = eff;
	}
}

void Arm::armSingle (u32 op)
{
	bool I = op & (1 << 25), P = op & (1 << 24), U = op & (1 << 23), B = op & (1 << 22), W = op & (1 << 21), L = op & (1 << 20);
	int rn = (op >> 16) & 15, rd = (op >> 12) & 15;
	u32 off;
	if (I) { bool c = CARRY; off = shiftImm (op, c); }
	else off = op & 0xFFF;
	u32 base = r[rn];
	u32 eff = U ? base + off : base - off;
	u32 at = P ? eff : base;
	cyc++;
	if (L)
	{
		u32 v;
		if (B) v = read8 (at);
		else v = ror (read32 (at & ~3u), (int) (at & 3) * 8);
		if ((W || !P) && rn != rd) r[rn] = eff;
		if (rd == 15)
		{
			if (!num && !(cpCtl & 0x8000)) jumpX (v); else setPC (v);
			cyc += 2;
		}
		else r[rd] = v;
	}
	else
	{
		u32 v = r[rd] + (rd == 15 ? 4 : 0);
		if (B) write8 (at, (u8) v);
		else write32 (at & ~3u, v);
		if (W || !P) r[rn] = eff;
	}
}

void Arm::armBlock (u32 op)
{
	bool P = op & (1 << 24), U = op & (1 << 23), S = op & (1 << 22), W = op & (1 << 21), L = op & (1 << 20);
	int rn = (op >> 16) & 15;
	u32 list = op & 0xFFFF;
	int n = __builtin_popcount (list);
	u32 base = r[rn];
	u32 bytes = list ? (u32) n * 4 : 0x40;
	if (!list) { list = 0x8000; n = 1; }			// (empty list: r15, the base moves by 64)
	u32 start = U ? base + (P ? 4 : 0) : base - bytes + (P ? 0 : 4);
	u32 newBase = U ? base + bytes : base - bytes;
	bool userBank = S && (!L || !(list & 0x8000));
	u32 oldMode = cpsr & 0x1F;
	if (userBank) setMode (0x1F);
	u32 a = start & ~3u;
	cyc += n;
	if (L)
	{
		u32 vals[16];
		for (int i = 0; i < 16; i++) if (list & (1u << i)) { vals[i] = read32 (a); a += 4; }
		for (int i = 0; i < 15; i++) if (list & (1u << i)) r[i] = vals[i];
		if (W)
		{
			if (!(list & (1u << rn))) r[rn] = newBase;
			else if (!num && (list & ~((2u << rn) - 1))) r[rn] = newBase;	// (ARM9: the base not the last one)
		}
		if (userBank) { setMode (oldMode); userBank = false; }
		if (list & 0x8000)
		{
			u32 v = vals[15];
			if (S && modeBank (cpsr)) { setCpsr (spsr); setPC (v); }
			else if (!num && !(cpCtl & 0x8000)) jumpX (v);
			else setPC (v);
			cyc += 2;
		}
	}
	else
	{
		bool first = true;
		for (int i = 0; i < 16; i++)
			if (list & (1u << i))
			{
				u32 v = r[i] + (i == 15 ? 4 : 0);
				if (i == rn && !first && num) v = newBase;	// (ARM7: the base not first: the new one)
				write32 (a, v); a += 4;
				first = false;
			}
		if (W) r[rn] = newBase;
	}
	if (userBank) setMode (oldMode);
}

void Arm::armPsr (u32 op)
{
	bool toSpsr = (op >> 22) & 1;
	if (!(op & (1 << 21)))					// MRS
	{
		r[(op >> 12) & 15] = toSpsr ? (modeBank (cpsr) ? spsr : cpsr) : cpsr;
		return;
	}
	u32 v = (op & 0x02000000) ? ror (op & 0xFF, ((op >> 8) & 15) * 2) : r[op & 15];
	u32 mask = 0;
	if (op & (1 << 19)) mask |= 0xFF000000;
	if (op & (1 << 18)) mask |= 0x00FF0000;
	if (op & (1 << 17)) mask |= 0x0000FF00;
	if (op & (1 << 16)) mask |= 0x000000FF;
	if (toSpsr) { if (modeBank (cpsr)) spsr = (spsr & ~mask) | (v & mask); return; }
	if ((cpsr & 0x1F) == 0x10) mask &= 0xFF000000;		// user mode: the flags only
	u32 nv = (cpsr & ~mask) | (v & mask);
	nv = (nv & ~FT) | (cpsr & FT);
	setCpsr (nv);
}

void Arm::armCop (u32 op)
{
	int cp = (op >> 8) & 15;
	if (cp != 15 || num) { if (op & (1 << 20)) { int rd = (op >> 12) & 15; if (rd != 15) r[rd] = 0; } return; }	// (the ARM7's CP14: nothing)
	int cn = (op >> 16) & 15, cm = op & 15, op2 = (op >> 5) & 7, rd = (op >> 12) & 15;
	if (op & (1 << 20))
	{
		u32 v = cp15Read (cn, cm, op2);
		if (rd == 15) cpsr = (cpsr & 0x0FFFFFFF) | (v & 0xF0000000);
		else r[rd] = v;
	}
	else cp15Write (cn, cm, op2, r[rd] + (rd == 15 ? 4 : 0));
}

u32 Arm::cp15Read (int cn, int cm, int op2)
{
	switch (cn)
	{
	case 0:
		if (op2 == 1) return 0x0F0D2112;			// cache type
		if (op2 == 2) return 0x00140180;			// TCM size
		return 0x41059461;					// main ID
	case 1: return cpCtl;
	case 9:
		if (cm == 1) return op2 ? cpItcm : cpDtcm;
		break;
	}
	return cpRegs[(cn * 4 + (cm & 3)) & 63] + 0 * op2;
}

void Arm::cp15Write (int cn, int cm, int op2, u32 v)
{
	switch (cn)
	{
	case 1:
		cpCtl = (cpCtl & ~0x000FF085u) | (v & 0x000FF085u);
		excBase = (cpCtl & 0x2000) ? 0xFFFF0000 : 0;
		updateTcm ();
		return;
	case 7:
		if ((cm == 0 && op2 == 4) || (cm == 8 && op2 == 2)) halted = true;	// wait for an interrupt
		return;
	case 9:
		if (cm == 1)
		{
			if (op2 == 0) cpDtcm = v; else cpItcm = v;
			updateTcm ();
			return;
		}
		break;
	}
	cpRegs[(cn * 4 + (cm & 3)) & 63] = v;
}

void Arm::updateTcm ()
{
	u32 ds = 512u << ((cpDtcm >> 1) & 0x1F); if (ds < 0x1000) ds = 0x1000;
	u32 is = 512u << ((cpItcm >> 1) & 0x1F); if (is < 0x1000) is = 0x1000;
	if (cpCtl & 0x10000) { dtcmBase = cpDtcm & 0xFFFFF000 & ~(ds - 1); dtcmSize = ds; }
	else { dtcmBase = 0xFFFFFFFF; dtcmSize = 0; }
	itcmSize = (cpCtl & 0x40000) ? is : 0;
	m->pagesDirty = true;
}

void Arm::execArm (u32 op)
{
	if ((op >> 28) == 0xF)					// unconditional (ARMv5)
	{
		if (num) { undefined (); return; }
		if ((op & 0x0E000000) == 0x0A000000)		// BLX immediate
		{
			s32 off = ((s32) (op << 8) >> 6) + (s32) ((op >> 23) & 2);
			r[14] = r[15] - 4;
			cpsr |= FT;
			setPC (r[15] + (u32) off);
			cyc += 2;
		}
		return;						// (PLD and the rest: nothing)
	}
	switch ((op >> 25) & 7)
	{
	case 0:
		if ((op & 0x90) == 0x90)
		{
			if ((op & 0x60) == 0)
			{
				if ((op & 0x0FC000F0) == 0x00000090) armMul (op);
				else if ((op & 0x0F8000F0) == 0x00800090) armMulLong (op);
				else if ((op & 0x0FB00FF0) == 0x01000090)	// SWP / SWPB
				{
					int rn = (op >> 16) & 15, rd = (op >> 12) & 15, rm = op & 15;
					u32 a = r[rn];
					if (op & (1 << 22)) { u32 t = read8 (a); write8 (a, (u8) r[rm]); r[rd] = t; }
					else { u32 t = ror (read32 (a & ~3u), (int) (a & 3) * 8); write32 (a & ~3u, r[rm]); r[rd] = t; }
					cyc += 2;
				}
				else undefined ();
			}
			else armHalf (op);
		}
		else if ((op & 0x01900000) == 0x01000000)		// the misc space (opcode 10xx, S = 0)
		{
			if ((op & 0x0FFFFFF0) == 0x012FFF10) { jumpX (r[op & 15]); cyc += 2; }	// BX
			else if ((op & 0x0FB000F0) == 0x01000000 || (op & 0x0FB000F0) == 0x01200000) armPsr (op);	// MRS / MSR
			else if ((op & 0x0FF000F0) == 0x01200070) undefined ();	// BKPT
			else armMisc (op);
		}
		else armDataProc (op);
		break;
	case 1:
		if ((op & 0x01900000) == 0x01000000)
		{
			if (op & (1 << 21)) armPsr (op);		// MSR immediate
			else undefined ();
		}
		else armDataProc (op);
		break;
	case 2: armSingle (op); break;
	case 3: if (!(op & 0x10)) armSingle (op); else undefined (); break;
	case 4: armBlock (op); break;
	case 5:
	{
		s32 off = (s32) (op << 8) >> 6;
		if (op & (1 << 24)) r[14] = r[15] - 4;
		setPC (r[15] + (u32) off);
		cyc += 2;
		break;
	}
	case 6: break;						// LDC / STC: no coprocessor
	default:
		if (op & (1 << 24)) m->swi (*this, (op >> 16) & 0xFF);
		else if (op & 0x10) armCop (op);
		break;
	}
}

int Arm::stepArm ()
{
	u32 pc = r[15];
	u32 op = fetch32 (pc);
	r[15] = pc + 8;
	branched = false;
	cyc = 1;
	if ((op >> 28) == 0xE || s_cond[op >> 28][cpsr >> 28] || (op >> 28) == 0xF) execArm (op);
	if (!branched) r[15] = pc + 4;
	return cyc;
}

// ---- Thumb -----------------------------------------------------------------------------------------------------
void Arm::execThumb (u32 op)
{
	switch (op >> 13)
	{
	case 0:
	{
		int rd = op & 7, rs = (op >> 3) & 7;
		if ((op >> 11) == 3)					// ADD / SUB register or 3-bit immediate
		{
			u32 a = r[rs], b = (op & 0x400) ? (op >> 6) & 7 : r[(op >> 6) & 7], res;
			if (op & 0x200) { res = a - b; SETC (a >= b); SETV (((a ^ b) & (a ^ res)) >> 31); }
			else { res = a + b; SETC (res < a); SETV ((~(a ^ b) & (a ^ res)) >> 31); }
			r[rd] = res; SETNZ (res);
		}
		else
		{
			u32 v = r[rs]; int amt = (op >> 6) & 31;
			bool c = CARRY;
			switch ((op >> 11) & 3)
			{
			case 0: if (amt) { c = (v >> (32 - amt)) & 1; v <<= amt; } break;
			case 1: if (amt) { c = (v >> (amt - 1)) & 1; v >>= amt; } else { c = v >> 31; v = 0; } break;
			default: if (amt) { c = ((s32) v >> (amt - 1)) & 1; v = (u32) ((s32) v >> amt); } else { c = v >> 31; v = (u32) ((s32) v >> 31); } break;
			}
			r[rd] = v; SETNZ (v); SETC (c);
		}
		break;
	}
	case 1:
	{
		int rd = (op >> 8) & 7; u32 imm = op & 0xFF, a = r[rd], res;
		switch ((op >> 11) & 3)
		{
		case 0: r[rd] = imm; SETNZ (imm); break;
		case 1: res = a - imm; SETNZ (res); SETC (a >= imm); SETV (((a ^ imm) & (a ^ res)) >> 31); break;
		case 2: res = a + imm; r[rd] = res; SETNZ (res); SETC (res < a); SETV ((~(a ^ imm) & (a ^ res)) >> 31); break;
		default: res = a - imm; r[rd] = res; SETNZ (res); SETC (a >= imm); SETV (((a ^ imm) & (a ^ res)) >> 31); break;
		}
		break;
	}
	case 2:
		if ((op >> 10) == 0x10)					// ALU
		{
			int rd = op & 7, rs = (op >> 3) & 7;
			u32 a = r[rd], b = r[rs], res;
			switch ((op >> 6) & 15)
			{
			case 0: res = a & b; r[rd] = res; SETNZ (res); break;
			case 1: res = a ^ b; r[rd] = res; SETNZ (res); break;
			case 2: { u32 n = b & 0xFF; bool c = CARRY; if (n) { if (n < 32) { c = (a >> (32 - n)) & 1; a <<= n; } else { c = n == 32 ? (a & 1) : 0; a = 0; } } r[rd] = a; SETNZ (a); SETC (c); cyc++; break; }
			case 3: { u32 n = b & 0xFF; bool c = CARRY; if (n) { if (n < 32) { c = (a >> (n - 1)) & 1; a >>= n; } else { c = n == 32 ? (a >> 31) : 0; a = 0; } } r[rd] = a; SETNZ (a); SETC (c); cyc++; break; }
			case 4: { u32 n = b & 0xFF; bool c = CARRY; if (n) { if (n < 32) { c = ((s32) a >> (n - 1)) & 1; a = (u32) ((s32) a >> n); } else { c = a >> 31; a = (u32) ((s32) a >> 31); } } r[rd] = a; SETNZ (a); SETC (c); cyc++; break; }
			case 5: { u64 t = (u64) a + b + CARRY; res = (u32) t; r[rd] = res; SETNZ (res); SETC (t >> 32); SETV ((~(a ^ b) & (a ^ res)) >> 31); break; }
			case 6: { u32 nc = 1 - CARRY; res = a - b - nc; r[rd] = res; SETNZ (res); SETC ((u64) a >= (u64) b + nc); SETV (((a ^ b) & (a ^ res)) >> 31); break; }
			case 7: { u32 n = b & 0xFF; bool c = CARRY; if (n) { n &= 31; if (n) { c = (a >> (n - 1)) & 1; a = ror (a, (int) n); } else c = a >> 31; } r[rd] = a; SETNZ (a); SETC (c); cyc++; break; }
			case 8: res = a & b; SETNZ (res); break;
			case 9: res = 0 - b; r[rd] = res; SETNZ (res); SETC (b == 0); SETV ((b & res) >> 31); break;
			case 10: res = a - b; SETNZ (res); SETC (a >= b); SETV (((a ^ b) & (a ^ res)) >> 31); break;
			case 11: res = a + b; SETNZ (res); SETC (res < a); SETV ((~(a ^ b) & (a ^ res)) >> 31); break;
			case 12: res = a | b; r[rd] = res; SETNZ (res); break;
			case 13: res = a * b; r[rd] = res; SETNZ (res); cyc += num ? 2 : 1; break;
			case 14: res = a & ~b; r[rd] = res; SETNZ (res); break;
			default: res = ~b; r[rd] = res; SETNZ (res); break;
			}
		}
		else if ((op >> 10) == 0x11)				// hi registers, BX / BLX
		{
			int rd = (op & 7) | ((op >> 4) & 8), rs = (op >> 3) & 15;
			u32 b = r[rs];
			switch ((op >> 8) & 3)
			{
			case 0: if (rd == 15) { setPC (r[15] + b); cyc += 2; } else r[rd] += b; break;
			case 1: { u32 a = r[rd], res = a - b; SETNZ (res); SETC (a >= b); SETV (((a ^ b) & (a ^ res)) >> 31); break; }
			case 2: if (rd == 15) { setPC (b); cyc += 2; } else r[rd] = b; break;
			default:
				if ((op & 0x80) && !num) r[14] = (r[15] - 2) | 1;	// BLX
				jumpX (b);
				cyc += 2;
				break;
			}
		}
		else if ((op >> 11) == 9)					// LDR rd, [pc, #imm]
		{
			u32 a = (r[15] & ~3u) + (op & 0xFF) * 4;
			r[(op >> 8) & 7] = read32 (a);
			cyc++;
		}
		else							// load / store, register offset
		{
			int rd = op & 7;
			u32 a = r[(op >> 3) & 7] + r[(op >> 6) & 7];
			cyc++;
			switch ((op >> 9) & 7)
			{
			case 0: write32 (a & ~3u, r[rd]); break;
			case 1: write16 (a & ~1u, (u16) r[rd]); break;
			case 2: write8 (a, (u8) r[rd]); break;
			case 3: r[rd] = (u32) (s32) (s8) read8 (a); break;
			case 4: r[rd] = ror (read32 (a & ~3u), (int) (a & 3) * 8); break;
			case 5: { u32 v = read16 (a & ~1u); r[rd] = (num && (a & 1)) ? ror (v, 8) : v; break; }
			case 6: r[rd] = read8 (a); break;
			default:
				if (num && (a & 1)) r[rd] = (u32) (s32) (s8) read8 (a);
				else r[rd] = (u32) (s32) (s16) read16 (a & ~1u);
				break;
			}
		}
		break;
	case 3:							// load / store, immediate offset
	{
		int rd = op & 7; u32 imm = (op >> 6) & 31;
		bool byte = op & 0x1000, load = op & 0x800;
		u32 a = r[(op >> 3) & 7] + (byte ? imm : imm * 4);
		cyc++;
		if (load) r[rd] = byte ? read8 (a) : ror (read32 (a & ~3u), (int) (a & 3) * 8);
		else if (byte) write8 (a, (u8) r[rd]);
		else write32 (a & ~3u, r[rd]);
		break;
	}
	case 4:
		cyc++;
		if (!(op & 0x1000))					// LDRH / STRH immediate
		{
			int rd = op & 7;
			u32 a = r[(op >> 3) & 7] + ((op >> 6) & 31) * 2;
			if (op & 0x800) { u32 v = read16 (a & ~1u); r[rd] = (num && (a & 1)) ? ror (v, 8) : v; }
			else write16 (a & ~1u, (u16) r[rd]);
		}
		else							// SP-relative
		{
			int rd = (op >> 8) & 7;
			u32 a = r[13] + (op & 0xFF) * 4;
			if (op & 0x800) r[rd] = ror (read32 (a & ~3u), (int) (a & 3) * 8);
			else write32 (a & ~3u, r[rd]);
		}
		break;
	case 5:
		if (!(op & 0x1000))					// ADD rd, pc / sp, #imm
			r[(op >> 8) & 7] = ((op & 0x800) ? r[13] : (r[15] & ~3u)) + (op & 0xFF) * 4;
		else if ((op & 0x0F00) == 0x0000)			// ADD sp, #+-imm
		{
			u32 imm = (op & 0x7F) * 4;
			r[13] = (op & 0x80) ? r[13] - imm : r[13] + imm;
		}
		else if ((op & 0x0600) == 0x0400)			// PUSH / POP
		{
			u32 list = op & 0xFF;
			bool extra = op & 0x100;
			int n = __builtin_popcount (list) + (extra ? 1 : 0);
			cyc += n;
			if (op & 0x800)
			{
				u32 a = r[13] & ~3u;
				for (int i = 0; i < 8; i++) if (list & (1u << i)) { r[i] = read32 (a); a += 4; }
				r[13] = r[13] + (u32) n * 4;
				if (extra)
				{
					u32 v = read32 (a);
					if (!num) jumpX (v); else setPC (v);
					cyc += 2;
				}
				if (!n) r[13] += 0x40;
			}
			else
			{
				u32 a = r[13] - (u32) n * 4;
				r[13] = a;
				a &= ~3u;
				for (int i = 0; i < 8; i++) if (list & (1u << i)) { write32 (a, r[i]); a += 4; }
				if (extra) write32 (a, r[14]);
			}
		}
		else if ((op & 0xFF00) == 0xBE00) undefined ();		// BKPT
		break;
	case 6:
		if (!(op & 0x1000))					// STMIA / LDMIA
		{
			int rb = (op >> 8) & 7;
			u32 list = op & 0xFF, a = r[rb];
			if (!list)					// (empty: r15, the base moves by 64)
			{
				if (op & 0x800) setPC (read32 (a & ~3u)); else write32 (a & ~3u, r[15] + 2);
				r[rb] = a + 0x40;
				break;
			}
			int n = __builtin_popcount (list);
			cyc += n;
			u32 end = a + (u32) n * 4;
			a &= ~3u;
			if (op & 0x800)
			{
				for (int i = 0; i < 8; i++) if (list & (1u << i)) { r[i] = read32 (a); a += 4; }
				if (!(list & (1u << rb))) r[rb] = end;
				else if (!num && (list & ~((2u << rb) - 1))) r[rb] = end;	// (ARM9: the base not the last one)
			}
			else
			{
				bool first = true;
				for (int i = 0; i < 8; i++)
					if (list & (1u << i))
					{
						u32 v = r[i];
						if (i == rb && !first && num) v = end;
						write32 (a, v); a += 4;
						first = false;
					}
				r[rb] = end;
			}
		}
		else
		{
			int cnd = (op >> 8) & 15;
			if (cnd == 15) m->swi (*this, op & 0xFF);
			else if (cnd != 14 && s_cond[cnd][cpsr >> 28]) { setPC (r[15] + (u32) ((s32) (s8) (op & 0xFF) * 2)); cyc += 2; }
		}
		break;
	default:
		if (!(op & 0x1000))
		{
			if (!(op & 0x800)) { setPC (r[15] + (u32) (((s32) (op << 21)) >> 20)); cyc += 2; }	// B
			else if (!num)					// BLX, second half (ARMv5)
			{
				u32 next = r[15] - 2;
				u32 t = (r[14] + ((op & 0x7FF) << 1)) & ~3u;
				r[14] = next | 1;
				cpsr &= ~FT;
				setPC (t);
				cyc += 2;
			}
		}
		else if (!(op & 0x800)) r[14] = r[15] + (u32) (((s32) ((op & 0x7FF) << 21)) >> 9);	// BL, first half
		else							// BL, second half
		{
			u32 next = r[15] - 2;
			setPC (r[14] + ((op & 0x7FF) << 1));
			r[14] = next | 1;
			cyc += 2;
		}
		break;
	}
}

int Arm::stepThumb ()
{
	u32 pc = r[15];
	u32 op = fetch16 (pc);
	r[15] = pc + 4;
	branched = false;
	cyc = 1;
	execThumb (op);
	if (!branched) r[15] = pc + 2;
	return cyc;
}

void Arm::run ()
{
	while (ts < target)
	{
		if (halted)
		{
			if (!m->wake (num)) { ts = target; break; }
			halted = false;
		}
		if (!(cpsr & FI) && m->irqLine (num)) irq ();
#ifdef NDS_DEBUG
		extern u32 g_watch[2][2]; extern bool g_watchHit;
		if (g_watch[num][1] && (r[15] < g_watch[num][0] || r[15] >= g_watch[num][1]) && !(r[15] >= 0xFFFF0000 || r[15] < 0x4000))
		{ g_watchHit = true; target = ts; break; }
		hist[histN++ & 4095] = r[15] | ((cpsr & FT) ? 1 : 0);
#endif
		int c = (cpsr & FT) ? stepThumb () : stepArm ();
		ts += (s64) c << shift;
	}
}

} // namespace nds
