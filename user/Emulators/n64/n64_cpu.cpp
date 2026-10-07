//
// n64/n64_cpu.cpp -- the R4300i: an interpreter of the MIPS III instructions the N64 uses (the
// 64-bit integer unit, the branches and their delay slots, the "likely" branches, the
// unaligned loads / stores, LL / SC, the traps), COP0 (the exceptions and interrupts, Count /
// Compare, the 32-entry TLB) and COP1 (single / double floats, 32- / 64-bit integers, the 16 or
// 32 registers of Status.FR). One instruction = 2 cycles (Count += 1): the usual N64 figure.
//
#include "n64/n64.h"

namespace n64 {

static inline u64 sx32 (u32 v) { return (u64) (s64) (s32) v; }
static inline u64 sx16 (u32 v) { return (u64) (s64) (s16) (u16) v; }
static inline u64 sx8 (u32 v) { return (u64) (s64) (s8) (u8) v; }

enum { ST_IE = 1, ST_EXL = 2, ST_ERL = 4, ST_FR = 1 << 26, ST_CU1 = 1 << 29, ST_BEV = 1 << 22 };
enum { C0_INDEX = 0, C0_RANDOM, C0_ENTRYLO0, C0_ENTRYLO1, C0_CONTEXT, C0_PAGEMASK, C0_WIRED, C0_7, C0_BADVADDR, C0_COUNT,
       C0_ENTRYHI, C0_COMPARE, C0_STATUS, C0_CAUSE, C0_EPC, C0_PRID, C0_CONFIG, C0_LLADDR, C0_WATCHLO, C0_WATCHHI,
       C0_XCONTEXT, C0_TAGLO = 28, C0_TAGHI, C0_ERROREPC };
enum { EXC_INT = 0, EXC_MOD = 1, EXC_TLBL = 2, EXC_TLBS = 3, EXC_ADEL = 4, EXC_ADES = 5, EXC_SYS = 8, EXC_BP = 9,
       EXC_RI = 10, EXC_CPU = 11, EXC_OV = 12, EXC_TR = 13, EXC_FPE = 15 };

// ---- COP0 ----------------------------------------------------------------------------------------------------------
u32 Machine::count () { return (u32) ((cycles - countBase) >> 1); }

void Machine::scheduleCompare ()
{
	u32 d = (u32) cp0[C0_COMPARE] - count ();
	schedule (EV_COMPARE, (u64) (d ? d : 0x100000000ull) * 2);
}

void Machine::checkInterrupts ()
{
	// the RCP line (IP2), the timer (IP7) are in Cause already
	u32 cause = (u32) cp0[C0_CAUSE] & ~0x400u;
	if (mi[2] & mi[3]) cause |= 0x400;
	cp0[C0_CAUSE] = cause;
	u32 st = (u32) cp0[C0_STATUS];
	if ((st & ST_IE) && !(st & (ST_EXL | ST_ERL)) && (cause & st & 0xFF00)) excPending = true;
}

void Machine::exception (int code, int ce)
{
	excCount[code & 31]++;
	if (code != 0) { lastExcPc = curPc; lastExcCode = code; }
	u32 st = (u32) cp0[C0_STATUS];
	bool refill = (code == EXC_TLBL || code == EXC_TLBS) && tlbMissVector == 0;
	u32 vec = 0x180;
	if (!(st & ST_EXL))
	{
		cp0[C0_EPC] = sx32 (inDelay ? curPc - 4 : curPc);
		if (inDelay) cp0[C0_CAUSE] |= 0x80000000u; else cp0[C0_CAUSE] &= ~0x80000000u;
		if (refill) vec = 0;
	}
	cp0[C0_CAUSE] = (cp0[C0_CAUSE] & ~0x3000007Cull) | (u32) (code << 2) | (u32) (ce << 28);
	cp0[C0_STATUS] = st | ST_EXL;
	u32 base = (st & ST_BEV) ? 0xBFC00200u : 0x80000000u;
	pc = base + vec; npc = pc + 4;
	nextDelay = false;
	tlbMissVector = 0;
}

// ---- the TLB --------------------------------------------------------------------------------------------------------
bool Machine::translate (u32 va, u32 &pa, bool write)
{
	u32 asid = (u32) cp0[C0_ENTRYHI] & 0xFF;
	for (int k = 0; k < 32; k++)
	{
		int i = (int) ((lastTlb + (u32) k) & 31);
		const TlbEntry &e = tlb[i];
		u32 m = e.mask | 0x1FFF;
		if (((va ^ e.hi) & ~m) != 0) continue;
		if (!((e.lo0 & e.lo1 & 1) || (e.hi & 0xFF) == asid)) continue;
		u32 odd = (m + 1) >> 1;
		u32 lo = (va & odd) ? e.lo1 : e.lo0;
		if (!(lo & 2))					// not valid
		{
			tlbMissVector = 1;
			goto miss;
		}
		if (write && !(lo & 4))				// not dirty: TLB modification
		{
			badVa = va;
			cp0[C0_BADVADDR] = sx32 (va);
			cp0[C0_CONTEXT] = (cp0[C0_CONTEXT] & ~0x7FFFF0ull) | ((va >> 9) & 0x7FFFF0);
			cp0[C0_ENTRYHI] = (cp0[C0_ENTRYHI] & 0xFF) | (va & 0xFFFFE000);
			exception (EXC_MOD);
			excPending = false;
			return false;
		}
		lastTlb = (u32) i;
		pa = ((((lo >> 6) & 0xFFFFF) << 12) & ~(odd - 1)) | (va & (odd - 1));
		return true;
	}
	tlbMissVector = 0;
miss:
	badVa = va;
	cp0[C0_BADVADDR] = sx32 (va);
	cp0[C0_CONTEXT] = (cp0[C0_CONTEXT] & ~0x7FFFF0ull) | ((va >> 9) & 0x7FFFF0);
	cp0[C0_ENTRYHI] = (cp0[C0_ENTRYHI] & 0xFF) | (va & 0xFFFFE000);
	exception (write ? EXC_TLBS : EXC_TLBL);
	return false;
}

void Machine::tlbWrite (int i)
{
	TlbEntry &e = tlb[i & 31];
	e.mask = (u32) cp0[C0_PAGEMASK] & 0x01FFE000;
	e.hi = (u32) cp0[C0_ENTRYHI] & ~(e.mask | 0x1F00);
	u32 g = (u32) (cp0[C0_ENTRYLO0] & cp0[C0_ENTRYLO1] & 1);
	e.lo0 = ((u32) cp0[C0_ENTRYLO0] & 0x3FFFFFFE) | g;
	e.lo1 = ((u32) cp0[C0_ENTRYLO1] & 0x3FFFFFFE) | g;
}

void Machine::tlbProbe ()
{
	u32 hi = (u32) cp0[C0_ENTRYHI];
	cp0[C0_INDEX] = 0x80000000u;
	for (int i = 0; i < 32; i++)
	{
		const TlbEntry &e = tlb[i];
		u32 m = e.mask | 0x1FFF;
		if (((hi ^ e.hi) & ~m) != 0) continue;
		if (!((e.lo0 & e.lo1 & 1) || ((hi ^ e.hi) & 0xFF) == 0)) continue;
		cp0[C0_INDEX] = (u64) i;
		return;
	}
}

// ---- memory, virtual ---------------------------------------------------------------------------------------------------
#define XLATE(va, pa, wr) \
	u32 pa; \
	if (((va) >> 30) == 2) pa = (va) & 0x1FFFFFFF; \
	else if (!translate ((va), pa, (wr))) { excPending = false; return false; }

bool Machine::rd8 (u32 va, u64 &v)
{
	XLATE (va, pa, false);
	if (pa < RDRAM_SIZE) v = ((const u8 *) rdram)[pa ^ 3];
	else v = (readSub (pa) >> (24 - 8 * (pa & 3))) & 0xFF;
	return true;
}
bool Machine::rd16 (u32 va, u64 &v)
{
	if (va & 1) { badVa = va; cp0[C0_BADVADDR] = sx32 (va); exception (EXC_ADEL); return false; }
	XLATE (va, pa, false);
	if (pa < RDRAM_SIZE) v = *(const u16 *) ((const u8 *) rdram + (pa ^ 2));
	else v = (readSub (pa) >> (16 - 8 * (pa & 2))) & 0xFFFF;
	return true;
}
bool Machine::rd32 (u32 va, u64 &v)
{
	if (va & 3) { badVa = va; cp0[C0_BADVADDR] = sx32 (va); exception (EXC_ADEL); return false; }
	XLATE (va, pa, false);
	v = pa < RDRAM_SIZE ? rdram[pa >> 2] : readIo (pa);
	return true;
}
bool Machine::rd64 (u32 va, u64 &v)
{
	if (va & 7) { badVa = va; cp0[C0_BADVADDR] = sx32 (va); exception (EXC_ADEL); return false; }
	XLATE (va, pa, false);
	if (pa < RDRAM_SIZE) v = (u64) rdram[pa >> 2] << 32 | rdram[(pa >> 2) + 1];
	else v = (u64) readIo (pa) << 32 | readIo (pa + 4);
	return true;
}
bool Machine::wr8 (u32 va, u32 v)
{
	XLATE (va, pa, true);
	if (pa < RDRAM_SIZE) ((u8 *) rdram)[pa ^ 3] = (u8) v;
	else { int sh = 24 - 8 * (int) (pa & 3); writeIo (pa & ~3u, (v & 0xFF) << sh, 0xFFu << sh); }
	return true;
}
bool Machine::wr16 (u32 va, u32 v)
{
	if (va & 1) { badVa = va; cp0[C0_BADVADDR] = sx32 (va); exception (EXC_ADES); return false; }
	XLATE (va, pa, true);
	if (pa < RDRAM_SIZE) *(u16 *) ((u8 *) rdram + (pa ^ 2)) = (u16) v;
	else { int sh = 16 - 8 * (int) (pa & 2); writeIo (pa & ~3u, (v & 0xFFFF) << sh, 0xFFFFu << sh); }
	return true;
}
bool Machine::wr32 (u32 va, u32 v)
{
	if (va & 3) { badVa = va; cp0[C0_BADVADDR] = sx32 (va); exception (EXC_ADES); return false; }
	XLATE (va, pa, true);
	if (pa < RDRAM_SIZE) rdram[pa >> 2] = v; else writeIo (pa, v, 0xFFFFFFFFu);
	return true;
}
bool Machine::wr64 (u32 va, u64 v)
{
	if (va & 7) { badVa = va; cp0[C0_BADVADDR] = sx32 (va); exception (EXC_ADES); return false; }
	XLATE (va, pa, true);
	if (pa < RDRAM_SIZE) { rdram[pa >> 2] = (u32) (v >> 32); rdram[(pa >> 2) + 1] = (u32) v; }
	else { writeIo (pa, (u32) (v >> 32), 0xFFFFFFFFu); writeIo (pa + 4, (u32) v, 0xFFFFFFFFu); }
	return true;
}
u32 Machine::fetch (u32 va, bool &ok)
{
	ok = true;
	if (va & 3) { badVa = va; cp0[C0_BADVADDR] = sx32 (va); curPc = va; exception (EXC_ADEL); ok = false; return 0; }
	u32 pa;
	if ((va >> 30) == 2) pa = va & 0x1FFFFFFF;
	else if (!translate (va, pa, false)) { ok = false; return 0; }
	if (pa < RDRAM_SIZE) return rdram[pa >> 2];
	return readIo (pa);
}

// ---- COP1 helpers --------------------------------------------------------------------------------------------------
static inline float asF (u32 v) { float f; __builtin_memcpy (&f, &v, 4); return f; }
static inline double asD (u64 v) { double d; __builtin_memcpy (&d, &v, 8); return d; }
static inline u32 fromF (float f) { u32 v; __builtin_memcpy (&v, &f, 4); return v; }
static inline u64 fromD (double d) { u64 v; __builtin_memcpy (&v, &d, 8); return v; }

// ---- one instruction ---------------------------------------------------------------------------------------------
void Machine::step ()
{
	if (excPending)							// an interrupt, before this instruction
	{
		excPending = false;
		u32 st = (u32) cp0[C0_STATUS];
		if ((st & ST_IE) && !(st & (ST_EXL | ST_ERL)) && ((u32) cp0[C0_CAUSE] & st & 0xFF00))
		{
			curPc = pc; inDelay = nextDelay;
			exception (EXC_INT);
		}
	}
	u32 at = pc;
	inDelay = nextDelay; nextDelay = false;
	curPc = at;
	bool ok;
	u32 op = fetch (at, ok);
	cycles += 2;
	if (!ok) return;
	pc = npc; npc += 4;
#ifdef N64_TRACE
	if (traceOn) { traceBuf[traceN % 4096] = at; traceN++; }
#endif
	exec (op);
	r[0] = 0;
}

void Machine::exec (u32 op)
{
	u32 rs = (op >> 21) & 31, rt = (op >> 16) & 31, rd = (op >> 11) & 31, sa = (op >> 6) & 31;
	u64 imm = sx16 (op & 0xFFFF);
	u32 addr;
	u64 v;
#define BRANCH(cond) do { nextDelay = true; if (cond) npc = curPc + 4 + (u32) (imm << 2); } while (0)
#define BRANCHL(cond) do { if (cond) { nextDelay = true; npc = curPc + 4 + (u32) (imm << 2); } else { pc = npc; npc = pc + 4; } } while (0)
#define EA (addr = (u32) (r[rs] + imm))
	switch (op >> 26)
	{
	case 0x00:							// SPECIAL
		switch (op & 63)
		{
		case 0x00: r[rd] = sx32 ((u32) r[rt] << sa); break;			// SLL
		case 0x02: r[rd] = sx32 ((u32) r[rt] >> sa); break;			// SRL
		case 0x03: r[rd] = sx32 ((u32) ((s64) r[rt] >> sa)); break;		// SRA
		case 0x04: r[rd] = sx32 ((u32) r[rt] << (r[rs] & 31)); break;		// SLLV
		case 0x06: r[rd] = sx32 ((u32) r[rt] >> (r[rs] & 31)); break;		// SRLV
		case 0x07: r[rd] = sx32 ((u32) ((s64) r[rt] >> (r[rs] & 31))); break;	// SRAV
		case 0x08: nextDelay = true; npc = (u32) r[rs]; break;			// JR
		case 0x09: { u32 t = (u32) r[rs]; r[rd] = sx32 (curPc + 8); nextDelay = true; npc = t; break; }	// JALR
		case 0x0C: exception (EXC_SYS); break;				// SYSCALL
		case 0x0D: exception (EXC_BP); break;				// BREAK
		case 0x0F: break;						// SYNC
		case 0x10: r[rd] = hi; break;
		case 0x11: hi = r[rs]; break;
		case 0x12: r[rd] = lo; break;
		case 0x13: lo = r[rs]; break;
		case 0x14: r[rd] = r[rt] << (r[rs] & 63); break;			// DSLLV
		case 0x16: r[rd] = r[rt] >> (r[rs] & 63); break;			// DSRLV
		case 0x17: r[rd] = (u64) ((s64) r[rt] >> (r[rs] & 63)); break;		// DSRAV
		case 0x18: { s64 p = (s64) (s32) r[rs] * (s64) (s32) r[rt]; lo = sx32 ((u32) p); hi = sx32 ((u32) (p >> 32)); cycles += 8; break; }
		case 0x19: { u64 p = (u64) (u32) r[rs] * (u64) (u32) r[rt]; lo = sx32 ((u32) p); hi = sx32 ((u32) (p >> 32)); cycles += 8; break; }
		case 0x1A:							// DIV
		{
			s32 a = (s32) r[rs], b = (s32) r[rt];
			if (b == 0) { lo = a < 0 ? 1 : (u64) -1; hi = sx32 ((u32) a); }
			else if (a == (s32) 0x80000000 && b == -1) { lo = sx32 (0x80000000u); hi = 0; }
			else { lo = sx32 ((u32) (a / b)); hi = sx32 ((u32) (a % b)); }
			cycles += 70;
			break;
		}
		case 0x1B:							// DIVU
		{
			u32 a = (u32) r[rs], b = (u32) r[rt];
			if (b == 0) { lo = (u64) -1; hi = sx32 (a); }
			else { lo = sx32 (a / b); hi = sx32 (a % b); }
			cycles += 70;
			break;
		}
		case 0x1C: { __int128 p = (__int128) (s64) r[rs] * (s64) r[rt]; lo = (u64) p; hi = (u64) (p >> 64); cycles += 14; break; }
		case 0x1D: { unsigned __int128 p = (unsigned __int128) r[rs] * r[rt]; lo = (u64) p; hi = (u64) (p >> 64); cycles += 14; break; }
		case 0x1E:							// DDIV
		{
			s64 a = (s64) r[rs], b = (s64) r[rt];
			if (b == 0) { lo = a < 0 ? 1 : (u64) -1; hi = (u64) a; }
			else if (a == (s64) 0x8000000000000000ull && b == -1) { lo = (u64) a; hi = 0; }
			else { lo = (u64) (a / b); hi = (u64) (a % b); }
			cycles += 138;
			break;
		}
		case 0x1F:							// DDIVU
		{
			u64 a = r[rs], b = r[rt];
			if (b == 0) { lo = (u64) -1; hi = a; }
			else { lo = a / b; hi = a % b; }
			cycles += 138;
			break;
		}
		case 0x20:							// ADD
		{
			s32 a = (s32) r[rs], b = (s32) r[rt], s;
			if (__builtin_add_overflow (a, b, &s)) { exception (EXC_OV); break; }
			r[rd] = sx32 ((u32) s); break;
		}
		case 0x21: r[rd] = sx32 ((u32) r[rs] + (u32) r[rt]); break;		// ADDU
		case 0x22:							// SUB
		{
			s32 a = (s32) r[rs], b = (s32) r[rt], s;
			if (__builtin_sub_overflow (a, b, &s)) { exception (EXC_OV); break; }
			r[rd] = sx32 ((u32) s); break;
		}
		case 0x23: r[rd] = sx32 ((u32) r[rs] - (u32) r[rt]); break;		// SUBU
		case 0x24: r[rd] = r[rs] & r[rt]; break;
		case 0x25: r[rd] = r[rs] | r[rt]; break;
		case 0x26: r[rd] = r[rs] ^ r[rt]; break;
		case 0x27: r[rd] = ~(r[rs] | r[rt]); break;
		case 0x2A: r[rd] = (s64) r[rs] < (s64) r[rt]; break;			// SLT
		case 0x2B: r[rd] = r[rs] < r[rt]; break;				// SLTU
		case 0x2C:							// DADD
		{
			s64 s;
			if (__builtin_add_overflow ((s64) r[rs], (s64) r[rt], &s)) { exception (EXC_OV); break; }
			r[rd] = (u64) s; break;
		}
		case 0x2D: r[rd] = r[rs] + r[rt]; break;				// DADDU
		case 0x2E:							// DSUB
		{
			s64 s;
			if (__builtin_sub_overflow ((s64) r[rs], (s64) r[rt], &s)) { exception (EXC_OV); break; }
			r[rd] = (u64) s; break;
		}
		case 0x2F: r[rd] = r[rs] - r[rt]; break;				// DSUBU
		case 0x30: if ((s64) r[rs] >= (s64) r[rt]) exception (EXC_TR); break;	// TGE
		case 0x31: if (r[rs] >= r[rt]) exception (EXC_TR); break;
		case 0x32: if ((s64) r[rs] < (s64) r[rt]) exception (EXC_TR); break;
		case 0x33: if (r[rs] < r[rt]) exception (EXC_TR); break;
		case 0x34: if (r[rs] == r[rt]) exception (EXC_TR); break;
		case 0x36: if (r[rs] != r[rt]) exception (EXC_TR); break;
		case 0x38: r[rd] = r[rt] << sa; break;					// DSLL
		case 0x3A: r[rd] = r[rt] >> sa; break;					// DSRL
		case 0x3B: r[rd] = (u64) ((s64) r[rt] >> sa); break;			// DSRA
		case 0x3C: r[rd] = r[rt] << (sa + 32); break;				// DSLL32
		case 0x3E: r[rd] = r[rt] >> (sa + 32); break;				// DSRL32
		case 0x3F: r[rd] = (u64) ((s64) r[rt] >> (sa + 32)); break;		// DSRA32
		default: exception (EXC_RI); break;
		}
		break;
	case 0x01:							// REGIMM
	{
		s64 a = (s64) r[rs];
		switch (rt)
		{
		case 0x00: BRANCH (a < 0); break;
		case 0x01: BRANCH (a >= 0); break;
		case 0x02: BRANCHL (a < 0); break;
		case 0x03: BRANCHL (a >= 0); break;
		case 0x08: if (a >= (s64) imm) exception (EXC_TR); break;
		case 0x09: if (r[rs] >= imm) exception (EXC_TR); break;
		case 0x0A: if (a < (s64) imm) exception (EXC_TR); break;
		case 0x0B: if (r[rs] < imm) exception (EXC_TR); break;
		case 0x0C: if (r[rs] == imm) exception (EXC_TR); break;
		case 0x0E: if (r[rs] != imm) exception (EXC_TR); break;
		case 0x10: r[31] = sx32 (curPc + 8); BRANCH (a < 0); break;
		case 0x11: r[31] = sx32 (curPc + 8); BRANCH (a >= 0); break;
		case 0x12: r[31] = sx32 (curPc + 8); BRANCHL (a < 0); break;
		case 0x13: r[31] = sx32 (curPc + 8); BRANCHL (a >= 0); break;
		default: exception (EXC_RI); break;
		}
		break;
	}
	case 0x02: nextDelay = true; npc = (curPc & 0xF0000000) | ((op & 0x3FFFFFF) << 2); break;	// J
	case 0x03: r[31] = sx32 (curPc + 8); nextDelay = true; npc = (curPc & 0xF0000000) | ((op & 0x3FFFFFF) << 2); break;
	case 0x04:							// BEQ
		if (rs == 0 && rt == 0 && (u32) imm == 0xFFFFFFFFu && pc == curPc + 4)
		{
			// "b ." with its delay slot: an idle loop -- to the next event (after the slot)
			bool ok; u32 slot = fetch (pc, ok);
			if (ok && slot == 0 && nextEvent > cycles) cycles = nextEvent;
		}
		BRANCH (r[rs] == r[rt]); break;
	case 0x05: BRANCH (r[rs] != r[rt]); break;
	case 0x06: BRANCH ((s64) r[rs] <= 0); break;
	case 0x07: BRANCH ((s64) r[rs] > 0); break;
	case 0x08:							// ADDI
	{
		s32 s;
		if (__builtin_add_overflow ((s32) r[rs], (s32) imm, &s)) { exception (EXC_OV); break; }
		r[rt] = sx32 ((u32) s); break;
	}
	case 0x09: r[rt] = sx32 ((u32) r[rs] + (u32) imm); break;		// ADDIU
	case 0x0A: r[rt] = (s64) r[rs] < (s64) imm; break;
	case 0x0B: r[rt] = r[rs] < imm; break;
	case 0x0C: r[rt] = r[rs] & (op & 0xFFFF); break;
	case 0x0D: r[rt] = r[rs] | (op & 0xFFFF); break;
	case 0x0E: r[rt] = r[rs] ^ (op & 0xFFFF); break;
	case 0x0F: r[rt] = sx32 ((op & 0xFFFF) << 16); break;			// LUI
	case 0x10: cop0 (op); break;
	case 0x11: cop1 (op); break;
	case 0x12: exception (EXC_CPU, 2); break;
	case 0x13: exception (EXC_CPU, 3); break;
	case 0x14: BRANCHL (r[rs] == r[rt]); break;
	case 0x15: BRANCHL (r[rs] != r[rt]); break;
	case 0x16: BRANCHL ((s64) r[rs] <= 0); break;
	case 0x17: BRANCHL ((s64) r[rs] > 0); break;
	case 0x18:							// DADDI
	{
		s64 s;
		if (__builtin_add_overflow ((s64) r[rs], (s64) imm, &s)) { exception (EXC_OV); break; }
		r[rt] = (u64) s; break;
	}
	case 0x19: r[rt] = r[rs] + imm; break;					// DADDIU
	case 0x1A:							// LDL
	{
		EA;
		if (!rd64 (addr & ~7u, v)) break;
		int sh = 8 * (int) (addr & 7);
		u64 mask = ~0ull << sh;
		r[rt] = (r[rt] & ~mask) | (v << sh);
		break;
	}
	case 0x1B:							// LDR
	{
		EA;
		if (!rd64 (addr & ~7u, v)) break;
		int sh = 8 * (int) (7 - (addr & 7));
		u64 mask = ~0ull >> sh;
		r[rt] = (r[rt] & ~mask) | (v >> sh);
		break;
	}
	case 0x20: EA; if (rd8 (addr, v)) r[rt] = sx8 ((u32) v); break;		// LB
	case 0x21: EA; if (rd16 (addr, v)) r[rt] = sx16 ((u32) v); break;	// LH
	case 0x22:							// LWL
	{
		EA;
		if (!rd32 (addr & ~3u, v)) break;
		int sh = 8 * (int) (addr & 3);
		u32 mask = 0xFFFFFFFFu << sh;
		r[rt] = sx32 (((u32) r[rt] & ~mask) | ((u32) v << sh));
		break;
	}
	case 0x23: EA; if (rd32 (addr, v)) r[rt] = sx32 ((u32) v); break;	// LW
	case 0x24: EA; if (rd8 (addr, v)) r[rt] = v; break;			// LBU
	case 0x25: EA; if (rd16 (addr, v)) r[rt] = v; break;			// LHU
	case 0x26:							// LWR
	{
		EA;
		if (!rd32 (addr & ~3u, v)) break;
		int sh = 8 * (int) (3 - (addr & 3));
		u32 mask = 0xFFFFFFFFu >> sh;
		r[rt] = sx32 (((u32) r[rt] & ~mask) | ((u32) v >> sh));
		break;
	}
	case 0x27: EA; if (rd32 (addr, v)) r[rt] = (u32) v; break;		// LWU
	case 0x28: EA; wr8 (addr, (u32) r[rt]); break;				// SB
	case 0x29: EA; wr16 (addr, (u32) r[rt]); break;				// SH
	case 0x2A:							// SWL
	{
		EA;
		if (!rd32 (addr & ~3u, v)) break;
		int sh = 8 * (int) (addr & 3);
		u32 mask = 0xFFFFFFFFu >> sh;
		wr32 (addr & ~3u, ((u32) v & ~mask) | ((u32) r[rt] >> sh));
		break;
	}
	case 0x2B: EA; wr32 (addr, (u32) r[rt]); break;				// SW
	case 0x2C:							// SDL
	{
		EA;
		if (!rd64 (addr & ~7u, v)) break;
		int sh = 8 * (int) (addr & 7);
		u64 mask = ~0ull >> sh;
		wr64 (addr & ~7u, (v & ~mask) | (r[rt] >> sh));
		break;
	}
	case 0x2D:							// SDR
	{
		EA;
		if (!rd64 (addr & ~7u, v)) break;
		int sh = 8 * (int) (7 - (addr & 7));
		u64 mask = ~0ull << sh;
		wr64 (addr & ~7u, (v & ~mask) | (r[rt] << sh));
		break;
	}
	case 0x2E:							// SWR
	{
		EA;
		if (!rd32 (addr & ~3u, v)) break;
		int sh = 8 * (int) (3 - (addr & 3));
		u32 mask = 0xFFFFFFFFu << sh;
		wr32 (addr & ~3u, ((u32) v & ~mask) | ((u32) r[rt] << sh));
		break;
	}
	case 0x2F: break;						// CACHE
	case 0x30:							// LL
		EA;
		if (rd32 (addr, v)) { r[rt] = sx32 ((u32) v); llbit = true; u32 pa = addr & 0x1FFFFFFF; cp0[C0_LLADDR] = pa >> 4; }
		break;
	case 0x31:							// LWC1
		if (!(cp0[C0_STATUS] & ST_CU1)) { exception (EXC_CPU, 1); break; }
		EA;
		if (rd32 (addr, v))
		{
			if (cp0[C0_STATUS] & ST_FR) fpr[rt] = (fpr[rt] & 0xFFFFFFFF00000000ull) | (u32) v;
			else if (rt & 1) fpr[rt & ~1u] = (fpr[rt & ~1u] & 0xFFFFFFFFull) | (v << 32);
			else fpr[rt] = (fpr[rt] & 0xFFFFFFFF00000000ull) | (u32) v;
		}
		break;
	case 0x34:							// LLD
		EA;
		if (rd64 (addr, v)) { r[rt] = v; llbit = true; cp0[C0_LLADDR] = (addr & 0x1FFFFFFF) >> 4; }
		break;
	case 0x35:							// LDC1
		if (!(cp0[C0_STATUS] & ST_CU1)) { exception (EXC_CPU, 1); break; }
		EA;
		if (rd64 (addr, v)) fpr[(cp0[C0_STATUS] & ST_FR) ? rt : (rt & ~1u)] = v;
		break;
	case 0x37: EA; if (rd64 (addr, v)) r[rt] = v; break;			// LD
	case 0x38:							// SC
		EA;
		if (llbit) { if (wr32 (addr, (u32) r[rt])) r[rt] = 1; }
		else r[rt] = 0;
		break;
	case 0x39:							// SWC1
	{
		if (!(cp0[C0_STATUS] & ST_CU1)) { exception (EXC_CPU, 1); break; }
		EA;
		u32 w;
		if (cp0[C0_STATUS] & ST_FR) w = (u32) fpr[rt];
		else w = (rt & 1) ? (u32) (fpr[rt & ~1u] >> 32) : (u32) fpr[rt];
		wr32 (addr, w);
		break;
	}
	case 0x3C:							// SCD
		EA;
		if (llbit) { if (wr64 (addr, r[rt])) r[rt] = 1; }
		else r[rt] = 0;
		break;
	case 0x3D:							// SDC1
		if (!(cp0[C0_STATUS] & ST_CU1)) { exception (EXC_CPU, 1); break; }
		EA;
		wr64 (addr, fpr[(cp0[C0_STATUS] & ST_FR) ? rt : (rt & ~1u)]);
		break;
	case 0x3F: EA; wr64 (addr, r[rt]); break;				// SD
	default: exception (EXC_RI); break;
	}
#undef BRANCH
#undef BRANCHL
#undef EA
}

void Machine::cop0 (u32 op)
{
	u32 rt = (op >> 16) & 31, rd = (op >> 11) & 31;
	switch ((op >> 21) & 31)
	{
	case 0x00:							// MFC0
	case 0x01:							// DMFC0
	{
		u64 v;
		if (rd == C0_COUNT) v = count ();
		else if (rd == C0_RANDOM)
		{
			u32 w = (u32) cp0[C0_WIRED] & 31;
			v = w >= 31 ? 31 : 31 - (u32) ((cycles >> 1) % (32 - w));
		}
		else v = cp0[rd];
		r[rt] = ((op >> 21) & 31) == 0 ? sx32 ((u32) v) : v;
		break;
	}
	case 0x04:							// MTC0
	case 0x05:							// DMTC0
	{
		u64 v = ((op >> 21) & 31) == 4 ? sx32 ((u32) r[rt]) : r[rt];
		switch (rd)
		{
		case C0_INDEX: cp0[rd] = v & 0x8000003F; break;
		case C0_RANDOM: break;
		case C0_ENTRYLO0: case C0_ENTRYLO1: cp0[rd] = v & 0x3FFFFFFF; break;
		case C0_CONTEXT: cp0[rd] = (cp0[rd] & 0x7FFFFF) | (v & ~0x7FFFFFull); break;
		case C0_PAGEMASK: cp0[rd] = v & 0x01FFE000; break;
		case C0_WIRED: cp0[rd] = v & 63; break;
		case C0_COUNT: countBase = cycles - ((u64) (u32) v << 1); scheduleCompare (); break;
		case C0_ENTRYHI: cp0[rd] = v & 0xC00000FFFFFFE0FFull; break;
		case C0_COMPARE:
			cp0[rd] = (u32) v;
			cp0[C0_CAUSE] &= ~0x8000ull;
			scheduleCompare ();
			break;
		case C0_STATUS: cp0[rd] = v & 0xFF57FFFF; checkInterrupts (); break;
		case C0_CAUSE: cp0[rd] = (cp0[rd] & ~0x300ull) | (v & 0x300); checkInterrupts (); break;
		case C0_PRID: break;
		case C0_CONFIG: cp0[rd] = (cp0[rd] & ~0x0F00800Full) | (v & 0x0F00800F); break;
		case C0_BADVADDR: break;
		default: cp0[rd] = v; break;
		}
		break;
	}
	case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15: case 0x16: case 0x17:
	case 0x18: case 0x19: case 0x1A: case 0x1B: case 0x1C: case 0x1D: case 0x1E: case 0x1F:
		switch (op & 63)
		{
		case 0x01:						// TLBR
		{
			const TlbEntry &e = tlb[cp0[C0_INDEX] & 31];
			u32 g = e.lo0 & e.lo1 & 1;
			cp0[C0_PAGEMASK] = e.mask;
			cp0[C0_ENTRYHI] = e.hi & ~e.mask;
			cp0[C0_ENTRYLO0] = (e.lo0 & ~1u) | g;
			cp0[C0_ENTRYLO1] = (e.lo1 & ~1u) | g;
			break;
		}
		case 0x02: tlbWrite ((int) (cp0[C0_INDEX] & 31)); break;	// TLBWI
		case 0x06:						// TLBWR
		{
			u32 w = (u32) cp0[C0_WIRED] & 31;
			tlbWrite ((int) (w >= 31 ? 31 : 31 - (u32) ((cycles >> 1) % (32 - w))));
			break;
		}
		case 0x08: tlbProbe (); break;				// TLBP
		case 0x18:						// ERET
		{
			u32 st = (u32) cp0[C0_STATUS];
			if (st & ST_ERL) { pc = (u32) cp0[C0_ERROREPC]; cp0[C0_STATUS] = st & ~ST_ERL; }
			else { pc = (u32) cp0[C0_EPC]; cp0[C0_STATUS] = st & ~ST_EXL; }
			npc = pc + 4;
			nextDelay = false;
			llbit = false;
			checkInterrupts ();
			break;
		}
		default: break;
		}
		break;
	default: exception (EXC_RI); break;
	}
}

// ---- COP1 ----------------------------------------------------------------------------------------------------------
void Machine::cop1 (u32 op)
{
	if (!(cp0[C0_STATUS] & ST_CU1)) { exception (EXC_CPU, 1); return; }
	bool fr = (cp0[C0_STATUS] & ST_FR) != 0;
	u32 fmt = (op >> 21) & 31, ft = (op >> 16) & 31, fs = (op >> 11) & 31, fd = (op >> 6) & 31;
	auto get32 = [&] (u32 n) -> u32 { return fr ? (u32) fpr[n] : (n & 1) ? (u32) (fpr[n & ~1u] >> 32) : (u32) fpr[n]; };
	auto set32 = [&] (u32 n, u32 v)
	{
		if (fr) fpr[n] = (fpr[n] & 0xFFFFFFFF00000000ull) | v;
		else if (n & 1) fpr[n & ~1u] = (fpr[n & ~1u] & 0xFFFFFFFFull) | ((u64) v << 32);
		else fpr[n] = (fpr[n] & 0xFFFFFFFF00000000ull) | v;
	};
	auto get64 = [&] (u32 n) -> u64 { return fpr[fr ? n : (n & ~1u)]; };
	auto set64 = [&] (u32 n, u64 v) { fpr[fr ? n : (n & ~1u)] = v; };
	switch (fmt)
	{
	case 0x00: r[ft] = sx32 (get32 (fs)); return;				// MFC1
	case 0x01: r[ft] = get64 (fs); return;					// DMFC1
	case 0x02: r[ft] = sx32 (fs == 31 ? fcr31 : fs == 0 ? 0xA00u : 0u); return;	// CFC1
	case 0x04: set32 (fs, (u32) r[ft]); return;				// MTC1
	case 0x05: set64 (fs, r[ft]); return;					// DMTC1
	case 0x06: if (fs == 31) fcr31 = (u32) r[ft] & 0x0183FFFF; return;	// CTC1
	case 0x08:								// BC1
	{
		bool c = (fcr31 >> 23) & 1;
		u64 imm = sx16 (op & 0xFFFF);
		bool cond = (ft & 1) ? c : !c;
		if (ft & 2) { if (cond) { nextDelay = true; npc = curPc + 4 + (u32) (imm << 2); } else { pc = npc; npc = pc + 4; } }
		else { nextDelay = true; if (cond) npc = curPc + 4 + (u32) (imm << 2); }
		return;
	}
	default: break;
	}
	u32 fn = op & 63;
	int rm = (int) (fcr31 & 3);
	// the rounding of the conversions: the mode of the instruction, or FCR31's
	auto rnd = [&] (double x, int mode) -> double
	{
		switch (mode)
		{
		case 0: return __builtin_rint (x);
		case 1: return __builtin_trunc (x);
		case 2: return __builtin_ceil (x);
		default: return __builtin_floor (x);
		}
	};
	auto cmp = [&] (double a, double b)
	{
		bool un = a != a || b != b;
		fcr31 &= ~0x3F000u;						// (the cause bits of this instruction)
		if (un && (fn & 8))						// a signalling compare with a NaN: invalid
		{
			fcr31 |= 0x10040;
			if (fcr31 & 0x800) { exception (EXC_FPE); return; }
		}
		bool res = (un && (fn & 1)) || (!un && ((a < b && (fn & 4)) || (a == b && (fn & 2))));
		if (res) fcr31 |= 1u << 23; else fcr31 &= ~(1u << 23);
	};
	if (fmt == 0x10)							// S
	{
		float a = asF (get32 (fs)), b = asF (get32 (ft));
		switch (fn)
		{
		case 0x00: set32 (fd, fromF (a + b)); break;
		case 0x01: set32 (fd, fromF (a - b)); break;
		case 0x02: set32 (fd, fromF (a * b)); cycles += 4; break;
		case 0x03: set32 (fd, fromF (a / b)); cycles += 56; break;
		case 0x04: set32 (fd, fromF (__builtin_sqrtf (a))); cycles += 56; break;
		case 0x05: set32 (fd, fromF (a < 0 ? -a : a == 0 ? 0.0f : a)); break;
		case 0x06: set32 (fd, get32 (fs)); break;
		case 0x07: set32 (fd, get32 (fs) ^ 0x80000000u); break;
		case 0x08: case 0x09: case 0x0A: case 0x0B: set64 (fd, (u64) (s64) rnd (a, (int) (fn & 3))); break;
		case 0x0C: case 0x0D: case 0x0E: case 0x0F: set32 (fd, (u32) (s32) rnd (a, (int) (fn & 3))); break;
		case 0x21: set64 (fd, fromD ((double) a)); break;
		case 0x24: set32 (fd, (u32) (s32) rnd (a, rm)); break;
		case 0x25: set64 (fd, (u64) (s64) rnd (a, rm)); break;
		default: if (fn >= 0x30) cmp (a, b); break;
		}
	}
	else if (fmt == 0x11)							// D
	{
		double a = asD (get64 (fs)), b = asD (get64 (ft));
		switch (fn)
		{
		case 0x00: set64 (fd, fromD (a + b)); break;
		case 0x01: set64 (fd, fromD (a - b)); break;
		case 0x02: set64 (fd, fromD (a * b)); cycles += 7; break;
		case 0x03: set64 (fd, fromD (a / b)); cycles += 114; break;
		case 0x04: set64 (fd, fromD (__builtin_sqrt (a))); cycles += 114; break;
		case 0x05: set64 (fd, fromD (a < 0 ? -a : a == 0 ? 0.0 : a)); break;
		case 0x06: set64 (fd, get64 (fs)); break;
		case 0x07: set64 (fd, get64 (fs) ^ 0x8000000000000000ull); break;
		case 0x08: case 0x09: case 0x0A: case 0x0B: set64 (fd, (u64) (s64) rnd (a, (int) (fn & 3))); break;
		case 0x0C: case 0x0D: case 0x0E: case 0x0F: set32 (fd, (u32) (s32) rnd (a, (int) (fn & 3))); break;
		case 0x20: set32 (fd, fromF ((float) a)); break;
		case 0x24: set32 (fd, (u32) (s32) rnd (a, rm)); break;
		case 0x25: set64 (fd, (u64) (s64) rnd (a, rm)); break;
		default: if (fn >= 0x30) cmp (a, b); break;
		}
	}
	else if (fmt == 0x14)							// W
	{
		s32 a = (s32) get32 (fs);
		if (fn == 0x20) set32 (fd, fromF ((float) a));
		else if (fn == 0x21) set64 (fd, fromD ((double) a));
	}
	else if (fmt == 0x15)							// L
	{
		s64 a = (s64) get64 (fs);
		if (fn == 0x20) set32 (fd, fromF ((float) a));
		else if (fn == 0x21) set64 (fd, fromD ((double) a));
	}
	else exception (EXC_RI);
}

} // namespace n64
