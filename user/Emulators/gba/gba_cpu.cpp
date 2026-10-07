//
// gba/gba_cpu.cpp -- the ARM7TDMI: ARM (32-bit) and Thumb (16-bit) instructions, the banked
// registers of the modes, interrupts. r[15] holds the address of the next instruction between
// instructions; while one runs it reads as that instruction + 8 (ARM) / + 4 (Thumb).
//
#include "gba/gba.h"

namespace gba {

static const u32 FN = 0x80000000u, FZ = 0x40000000u, FC = 0x20000000u, FV = 0x10000000u, FI = 0x80u, FT = 0x20u;

static bool s_condInit = false;
static bool s_cond[16][16];					// [cond][NZCV]
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

int Machine::modeBank (u32 m)
{
	switch (m & 0x1F) { case 0x11: return 1; case 0x12: return 2; case 0x13: return 3; case 0x17: return 4; case 0x1B: return 5; }
	return 0;
}

void Machine::setMode (u32 m)
{
	int ob = modeBank (cpsr), nb = modeBank (m);
	if (ob != nb)
	{
		bankR13[ob] = r[13]; bankR14[ob] = r[14]; bankSpsr[ob] = spsr;
		if (ob == 1) for (int i = 0; i < 5; i++) { bankFiq[i] = r[8 + i]; r[8 + i] = bankUsr[i]; }
		if (nb == 1) for (int i = 0; i < 5; i++) { bankUsr[i] = r[8 + i]; r[8 + i] = bankFiq[i]; }
		r[13] = bankR13[nb]; r[14] = bankR14[nb]; spsr = bankSpsr[nb];
	}
	cpsr = (cpsr & ~0x1Fu) | (m & 0x1F);
}

void Machine::setCpsr (u32 v)
{
	setMode (v | 0x10);
	if ((cpsr ^ v) & FT) pipeOk = false;
	cpsr = v | 0x10;
}

void Machine::setPC (u32 v)
{
	r[15] = (cpsr & FT) ? v & ~1u : v & ~3u;
	branched = true;
	pipeOk = false;
}

void Machine::irq ()
{
	u32 ret = r[15];					// the next instruction
	u32 old = cpsr;
	setMode (0x12);
	spsr = old;
	cpsr = (cpsr | FI) & ~FT;
	r[14] = ret + 4;
	r[15] = 0x18;
	pipeOk = false;
	halted = false;
}

// ---- the barrel shifter -----------------------------------------------------------------------------
u32 Machine::shiftImm (u32 op, bool &c)
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

u32 Machine::shiftReg (u32 op, bool &c)
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

// ---- ARM ----------------------------------------------------------------------------------------------
void Machine::armDataProc (u32 op)
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
	case 6: { u64 t = (u64) a - b - (1 - CARRY); res = (u32) t; arith = true; cOut = (u64) a >= (u64) b + (1 - CARRY); vOut = ((a ^ b) & (a ^ res)) >> 31; break; }
	case 7: { u64 t = (u64) b - a - (1 - CARRY); res = (u32) t; arith = true; cOut = (u64) b >= (u64) a + (1 - CARRY); vOut = ((b ^ a) & (b ^ res)) >> 31; break; }
	case 12: res = a | b; break;
	case 13: res = b; break;
	case 14: res = a & ~b; break;
	case 15: res = ~b; break;
	}
	bool test = opc >= 8 && opc <= 11;
	if (rd == 15 && S && test) { setCpsr (modeBank (cpsr) ? spsr : cpsr); return; }	// (the old TSTP / CMPP: CPSR = SPSR, no jump)
	if (rd == 15 && S && !test)
	{
		if (modeBank (cpsr)) setCpsr (spsr);			// (User / System have no SPSR)
		setPC (res);
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
		if (rd == 15) setPC (res);
		else r[rd] = res;
	}
}

void Machine::armMul (u32 op)
{
	int rd = (op >> 16) & 15, rn = (op >> 12) & 15, rs = (op >> 8) & 15, rm = op & 15;
	u32 res = r[rm] * r[rs];
	if (op & 0x00200000) { res += r[rn]; cyc++; }
	u32 m = r[rs];
	cyc += (m >> 8) == 0 || (m >> 8) == 0xFFFFFF ? 1 : (m >> 16) == 0 || (m >> 16) == 0xFFFF ? 2 : (m >> 24) == 0 || (m >> 24) == 0xFF ? 3 : 4;
	r[rd] = res;
	if (op & 0x00100000) SETNZ (res);
}

void Machine::armMulLong (u32 op)
{
	int hi = (op >> 16) & 15, lo = (op >> 12) & 15, rs = (op >> 8) & 15, rm = op & 15;
	u64 res;
	if (op & 0x00400000) res = (u64) ((s64) (s32) r[rm] * (s64) (s32) r[rs]);
	else res = (u64) r[rm] * (u64) r[rs];
	if (op & 0x00200000) { res += ((u64) r[hi] << 32) | r[lo]; cyc++; }
	cyc += 2 + ((r[rs] >> 16) ? 2 : 1);
	r[lo] = (u32) res; r[hi] = (u32) (res >> 32);
	if (op & 0x00100000) cpsr = (cpsr & ~(FN | FZ)) | ((u32) (res >> 32) & FN) | (res ? 0 : FZ);
}

void Machine::armHalf (u32 op)
{
	bool P = op & (1 << 24), U = op & (1 << 23), W = op & (1 << 21), L = op & (1 << 20);
	int rn = (op >> 16) & 15, rd = (op >> 12) & 15, sh = (op >> 5) & 3;
	u32 off = (op & (1 << 22)) ? (((op >> 4) & 0xF0) | (op & 0xF)) : r[op & 15];
	u32 base = r[rn];
	u32 eff = U ? base + off : base - off;
	u32 at = P ? eff : base;
	cyc += waitN[(at >> 24) & 15];
	if (L)
	{
		u32 v;
		if (sh == 1) { v = read16 (at); if (at & 1) v = ror (v, 8); }
		else if (sh == 2) v = (u32) (s32) (s8) read8 (at);
		else v = (at & 1) ? (u32) (s32) (s8) read8 (at) : (u32) (s32) (s16) read16 (at);
		if ((W || !P) && rn != rd) r[rn] = eff;
		cyc++;
		if (rd == 15) setPC (v); else r[rd] = v;
	}
	else
	{
		if (sh == 1) write16 (at, (u16) (r[rd] + (rd == 15 ? 4 : 0)));
		if (W || !P) r[rn] = eff;
	}
}

void Machine::armSingle (u32 op)
{
	bool I = op & (1 << 25), P = op & (1 << 24), U = op & (1 << 23), B = op & (1 << 22), W = op & (1 << 21), L = op & (1 << 20);
	int rn = (op >> 16) & 15, rd = (op >> 12) & 15;
	u32 off;
	if (I) { bool c = CARRY; off = shiftImm (op, c); }
	else off = op & 0xFFF;
	u32 base = r[rn];
	u32 eff = U ? base + off : base - off;
	u32 at = P ? eff : base;
	if (L)
	{
		u32 v;
		if (B) { v = read8 (at); cyc += waitN[(at >> 24) & 15]; }
		else { v = ror (read32 (at), (int) (at & 3) * 8); cyc += waitN32[(at >> 24) & 15]; }
		if ((W || !P) && rn != rd) r[rn] = eff;
		cyc++;
		if (rd == 15) setPC (v); else r[rd] = v;
	}
	else
	{
		u32 v = r[rd] + (rd == 15 ? 4 : 0);
		if (B) { write8 (at, (u8) v); cyc += waitN[(at >> 24) & 15]; }
		else { write32 (at, v); cyc += waitN32[(at >> 24) & 15]; }
		if (W || !P) r[rn] = eff;
	}
}

void Machine::armBlock (u32 op)
{
	bool P = op & (1 << 24), U = op & (1 << 23), S = op & (1 << 22), W = op & (1 << 21), L = op & (1 << 20);
	int rn = (op >> 16) & 15;
	u32 list = op & 0xFFFF;
	int n = 0;
	for (int i = 0; i < 16; i++) if (list & (1u << i)) n++;
	u32 base = r[rn];
	u32 bytes = list ? (u32) n * 4 : 0x40;
	if (!list) list = 0x8000;				// (empty list: r15, the base moves by 64)
	u32 start = U ? base + (P ? 4 : 0) : base - bytes + (P ? 0 : 4);
	u32 newBase = U ? base + bytes : base - bytes;
	bool userBank = S && (!L || !(list & 0x8000));
	u32 oldMode = cpsr & 0x1F;
	if (userBank) setMode (0x1F);
	u32 a = start;
	bool first = true;
	cyc += waitN32[(a >> 24) & 15] + (n > 1 ? (n - 1) * waitS32[(a >> 24) & 15] : 0);
	if (L)
	{
		if (W && !(list & (1u << rn))) r[rn] = newBase;
		for (int i = 0; i < 16; i++)
			if (list & (1u << i))
			{
				u32 v = read32 (a); a += 4;
				if (i == 15)
				{
					if (S && modeBank (cpsr)) setCpsr (spsr);
					setPC (v);
				}
				else r[i] = v;
			}
		cyc++;
	}
	else
	{
		for (int i = 0; i < 16; i++)
			if (list & (1u << i))
			{
				write32 (a, r[i] + (i == 15 ? 4 : 0)); a += 4;
				if (first && W) r[rn] = newBase;		// (the base, if first, is stored unchanged)
				first = false;
			}
	}
	if (userBank) setMode (oldMode);
}

void Machine::armPsr (u32 op)
{
	bool toSpsr = (op >> 22) & 1;
	if (!(op & (1 << 21)))					// MRS
	{
		r[(op >> 12) & 15] = toSpsr && modeBank (cpsr) ? spsr : cpsr;
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

int Machine::stepArm ()
{
	u32 pc = r[15];
	fetchPC = pc;
	int region = (int) (pc >> 24) & 15;
	if (!pipeOk) { pipe[0] = read32 (pc); pipe[1] = read32 (pc + 4); pipeOk = true; }
	u32 op = pipe[0];
	pipe[0] = pipe[1]; pipe[1] = read32 (pc + 8);
	if (region == 0) biosLatch = pipe[1];
	cyc = (prefetch && region >= 8 && region <= 13) ? 2 : waitS32[region];
	r[15] = pc + 8;
	branched = false;
	if (s_cond[op >> 28][cpsr >> 28])
	{
		switch ((op >> 25) & 7)
		{
		case 0:
			if ((op & 0x0FFFFFF0) == 0x012FFF10)			// BX
			{
				u32 v = r[op & 15];
				if (v & 1) cpsr |= FT; else cpsr &= ~FT;
				setPC (v);
			}
			else if ((op & 0x0FC000F0) == 0x00000090) armMul (op);
			else if ((op & 0x0F8000F0) == 0x00800090) armMulLong (op);
			else if ((op & 0x0FB00FF0) == 0x01000090)		// SWP / SWPB
			{
				int rn = (op >> 16) & 15, rd = (op >> 12) & 15, rm = op & 15;
				u32 a = r[rn];
				if (op & (1 << 22)) { u32 t = read8 (a); write8 (a, (u8) r[rm]); r[rd] = t; }
				else { u32 t = ror (read32 (a), (int) (a & 3) * 8); write32 (a, r[rm]); r[rd] = t; }
				cyc += 2 * waitN32[(a >> 24) & 15] + 1;
			}
			else if ((op & 0x0E000090) == 0x00000090 && (op & 0x60)) armHalf (op);
			else if ((op & 0x0D900000) == 0x01000000) armPsr (op);
			else armDataProc (op);
			break;
		case 1:
			if ((op & 0x0DB00000) == 0x01200000) armPsr (op);	// MSR immediate
			else if ((op & 0x0D900000) == 0x01000000) {}		// (undefined)
			else armDataProc (op);
			break;
		case 2: armSingle (op); break;
		case 3: if (!(op & 0x10)) armSingle (op); break;
		case 4: armBlock (op); break;
		case 5:
		{
			s32 off = (s32) (op << 8) >> 6;
			if (op & (1 << 24)) r[14] = pc + 4;
			setPC (r[15] + (u32) off);
			break;
		}
		case 7: if (op & (1 << 24)) swi ((op >> 16) & 0xFF); break;
		default: break;
		}
	}
	if (!branched) r[15] = pc + 4;
	else { int nr = (int) (r[15] >> 24) & 15; cyc += waitN32[nr] + waitS32[nr]; }
	return cyc;
}

// ---- Thumb ----------------------------------------------------------------------------------------------
int Machine::stepThumb ()
{
	u32 pc = r[15];
	fetchPC = pc;
	int region = (int) (pc >> 24) & 15;
	if (!pipeOk) { pipe[0] = read16 (pc); pipe[1] = read16 (pc + 2); pipeOk = true; }
	u32 op = pipe[0];
	pipe[0] = pipe[1]; pipe[1] = read16 (pc + 4);
	cyc = (prefetch && region >= 8 && region <= 13) ? 1 : waitS[region];
	r[15] = pc + 4;
	branched = false;
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
			case 13: res = a * b; r[rd] = res; SETNZ (res); cyc += 2; break;
			case 14: res = a & ~b; r[rd] = res; SETNZ (res); break;
			default: res = ~b; r[rd] = res; SETNZ (res); break;
			}
		}
		else if ((op >> 10) == 0x11)				// hi registers, BX
		{
			int rd = (op & 7) | ((op >> 4) & 8), rs = (op >> 3) & 15;
			u32 b = r[rs];
			switch ((op >> 8) & 3)
			{
			case 0: if (rd == 15) setPC (r[15] + b); else r[rd] += b; break;
			case 1: { u32 a = r[rd], res = a - b; SETNZ (res); SETC (a >= b); SETV (((a ^ b) & (a ^ res)) >> 31); break; }
			case 2: if (rd == 15) setPC (b); else r[rd] = b; break;
			default:
				if (b & 1) cpsr |= FT; else cpsr &= ~FT;
				setPC (b);
				break;
			}
		}
		else if ((op >> 11) == 9)					// LDR rd, [pc, #imm]
		{
			u32 a = (r[15] & ~3u) + (op & 0xFF) * 4;
			r[(op >> 8) & 7] = read32 (a);
			cyc += waitN32[(a >> 24) & 15] + 1;
		}
		else							// load / store, register offset
		{
			int rd = op & 7;
			u32 a = r[(op >> 3) & 7] + r[(op >> 6) & 7];
			int reg = (int) (a >> 24) & 15;
			switch ((op >> 9) & 7)
			{
			case 0: write32 (a, r[rd]); cyc += waitN32[reg]; break;
			case 1: write16 (a, (u16) r[rd]); cyc += waitN[reg]; break;
			case 2: write8 (a, (u8) r[rd]); cyc += waitN[reg]; break;
			case 3: r[rd] = (u32) (s32) (s8) read8 (a); cyc += waitN[reg] + 1; break;
			case 4: r[rd] = ror (read32 (a), (int) (a & 3) * 8); cyc += waitN32[reg] + 1; break;
			case 5: { u32 v = read16 (a); r[rd] = (a & 1) ? ror (v, 8) : v; cyc += waitN[reg] + 1; break; }
			case 6: r[rd] = read8 (a); cyc += waitN[reg] + 1; break;
			default: r[rd] = (a & 1) ? (u32) (s32) (s8) read8 (a) : (u32) (s32) (s16) read16 (a); cyc += waitN[reg] + 1; break;
			}
		}
		break;
	case 3:							// load / store, immediate offset
	{
		int rd = op & 7; u32 imm = (op >> 6) & 31;
		bool byte = op & 0x1000, load = op & 0x800;
		u32 a = r[(op >> 3) & 7] + (byte ? imm : imm * 4);
		int reg = (int) (a >> 24) & 15;
		if (load) { r[rd] = byte ? read8 (a) : ror (read32 (a), (int) (a & 3) * 8); cyc += (byte ? waitN[reg] : waitN32[reg]) + 1; }
		else if (byte) { write8 (a, (u8) r[rd]); cyc += waitN[reg]; }
		else { write32 (a, r[rd]); cyc += waitN32[reg]; }
		break;
	}
	case 4:
		if (!(op & 0x1000))					// LDRH / STRH immediate
		{
			int rd = op & 7;
			u32 a = r[(op >> 3) & 7] + ((op >> 6) & 31) * 2;
			if (op & 0x800) { u32 v = read16 (a); r[rd] = (a & 1) ? ror (v, 8) : v; cyc += waitN[(a >> 24) & 15] + 1; }
			else { write16 (a, (u16) r[rd]); cyc += waitN[(a >> 24) & 15]; }
		}
		else							// SP-relative
		{
			int rd = (op >> 8) & 7;
			u32 a = r[13] + (op & 0xFF) * 4;
			if (op & 0x800) { r[rd] = ror (read32 (a), (int) (a & 3) * 8); cyc += waitN32[(a >> 24) & 15] + 1; }
			else { write32 (a, r[rd]); cyc += waitN32[(a >> 24) & 15]; }
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
			int n = extra ? 1 : 0;
			for (int i = 0; i < 8; i++) if (list & (1u << i)) n++;
			if (op & 0x800)
			{
				u32 a = r[13];
				cyc += waitN32[(a >> 24) & 15] + (n > 1 ? (n - 1) * waitS32[(a >> 24) & 15] : 0) + 1;
				for (int i = 0; i < 8; i++) if (list & (1u << i)) { r[i] = read32 (a); a += 4; }
				if (extra) { setPC (read32 (a)); a += 4; }
				r[13] = a;
			}
			else
			{
				u32 a = r[13] - (u32) n * 4;
				r[13] = a;
				cyc += waitN32[(a >> 24) & 15] + (n > 1 ? (n - 1) * waitS32[(a >> 24) & 15] : 0);
				for (int i = 0; i < 8; i++) if (list & (1u << i)) { write32 (a, r[i]); a += 4; }
				if (extra) write32 (a, r[14]);
			}
		}
		break;
	case 6:
		if (!(op & 0x1000))					// STMIA / LDMIA
		{
			int rb = (op >> 8) & 7;
			u32 list = op & 0xFF, a = r[rb];
			if (!list)					// (empty: r15, the base moves by 64)
			{
				if (op & 0x800) setPC (read32 (a)); else write32 (a, r[15] + 2);
				r[rb] = a + 0x40;
				break;
			}
			int n = 0; for (int i = 0; i < 8; i++) if (list & (1u << i)) n++;
			cyc += waitN32[(a >> 24) & 15] + (n > 1 ? (n - 1) * waitS32[(a >> 24) & 15] : 0);
			u32 end = a + (u32) n * 4;
			if (op & 0x800)
			{
				for (int i = 0; i < 8; i++) if (list & (1u << i)) { r[i] = read32 (a); a += 4; }
				if (!(list & (1u << rb))) r[rb] = end;
				cyc++;
			}
			else
			{
				bool first = true;
				for (int i = 0; i < 8; i++)
					if (list & (1u << i))
					{
						write32 (a, r[i]); a += 4;
						if (first) r[rb] = end;
						first = false;
					}
			}
		}
		else
		{
			int cond = (op >> 8) & 15;
			if (cond == 15) swi (op & 0xFF);
			else if (cond != 14 && s_cond[cond][cpsr >> 28]) setPC (r[15] + (u32) ((s32) (s8) (op & 0xFF) * 2));
		}
		break;
	default:
		if (!(op & 0x1000))
		{
			if (!(op & 0x800)) setPC (r[15] + (u32) (((s32) (op << 21)) >> 20));	// B
		}
		else if (!(op & 0x800)) r[14] = r[15] + (u32) (((s32) ((op & 0x7FF) << 21)) >> 9);	// BL, first half
		else							// BL, second half
		{
			u32 next = pc + 2;
			setPC (r[14] + ((op & 0x7FF) << 1));
			r[14] = next | 1;
		}
		break;
	}
	if (!branched) r[15] = pc + 2;
	else { int nr = (int) (r[15] >> 24) & 15; cyc += waitN[nr] + waitS[nr]; }
	return cyc;
}

void cpuInit () { if (!s_condInit) condInit (); }

} // namespace gba
