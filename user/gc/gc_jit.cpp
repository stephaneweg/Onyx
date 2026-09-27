//
// gc/gc_jit.cpp -- the Gekko's JIT: the PowerPC code translated to AArch64 a block at a time
// (from an address to its branch, at most 64 instructions, within a 4 KB page), run until the
// cycle the machine's events are due. The integer unit, the branches, the loads and stores
// are translated (MEM1 read straight from the host's memory when the address maps it, the rest
// through the memory functions); the other instructions call the interpreter's exec, one each.
//
//   * the registers stay in the Machine (x19 points at it): an instruction loads its operands
//     and stores its result; x20 = MEM1, x21 = the context (the helpers, the fast table),
//     w22 = MEM1's size, w23 = an address kept across a call
//   * a block ends with the next address in pc and its cycles added; the dispatcher (in the
//     code buffer) finds the next block in a direct-mapped table (address + the MSR's IR / DR)
//     and jumps to it while the cycle count is below jitUntil; else back to jitRun (C), which
//     takes the interrupts, translates what is missing
//   * the code is dropped where it is written: icbi, the DVD / ARAM / locked-cache DMAs (by
//     4 KB page: each page knows its blocks), all of it when a BAT or HID0's ICFI changes
//
#include "gc/gc.h"

namespace gc {

void *(*codeAlloc) (u32 size) = 0;

#if defined(__aarch64__)

enum { MSR_EE = 0x8000, MSR_IR = 0x20, MSR_DR = 0x10 };
enum
{
	CODE_SIZE = 32 << 20, MAX_BLOCKS = 1 << 17, HASH_BITS = 16, FAST_BITS = 16, FAST_OFF = 4096,
	MAX_INSNS = 64, NPAGES = MEM1_SIZE >> 12, STUB_WORDS = 256
};
static const u32 NONE = 0xFFFFFFFFu, NOKEY = 0xFFFFFFFFu;
enum { H_INTERP, H_RD8, H_RD16, H_RD32, H_WR8, H_WR16, H_WR32, H_RD64, H_WR64, H_CVTD, H_CVTS, H_FPRF };

static inline u32 rotl (u32 v, int n) { n &= 31; return n ? (v << n) | (v >> (32 - n)) : v; }
static inline u32 mask (int mb, int me)
{
	u32 a = 0xFFFFFFFFu >> mb, b = 0xFFFFFFFFu << (31 - me);
	return mb <= me ? a & b : a | b;
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

// ---- the AArch64 assembler (what the translation uses) -------------------------------------------------------
enum { WZR = 31, SP = 31, XM = 19, XMEM = 20, XCTX = 21, WMSZ = 22, WEA = 23, WONE = 24, XK1 = 25, XK2 = 26 };
enum { EQ = 0, NE, HS, LO, MI, PL, VS, VC, HI, LS, GE, LT, GT, LE };
enum : u32
{
	ADD_W = 0x0B000000, ADDS_W = 0x2B000000, SUB_W = 0x4B000000, SUBS_W = 0x6B000000,
	AND_W = 0x0A000000, BIC_W = 0x0A200000, ORR_W = 0x2A000000, ORN_W = 0x2A200000,
	EOR_W = 0x4A000000, EON_W = 0x4A200000, ANDS_W = 0x6A000000,
	ADC_W = 0x1A000000, ADCS_W = 0x3A000000, SBC_W = 0x5A000000, SBCS_W = 0x7A000000,
	LSLV_W = 0x1AC02000, LSRV_W = 0x1AC02400, ASRV_W = 0x1AC02800, RORV_W = 0x1AC02C00,
	UDIV_W = 0x1AC00800, SDIV_W = 0x1AC00C00, MUL_W = 0x1B007C00, SMULL = 0x9B207C00, UMULL = 0x9BA07C00,
	X64 = 0x80000000,
	ADD_WI = 0x11000000, ADDS_WI = 0x31000000, SUB_WI = 0x51000000, SUBS_WI = 0x71000000,
	UBFM_W = 0x53000000, SBFM_W = 0x13000000, BFM_W = 0x33000000, UBFM_X = 0xD3400000, SBFM_X = 0x93400000,
	CLZ_W = 0x5AC01000, REV_W = 0x5AC00800, REV16_W = 0x5AC00400,
	CSEL_W = 0x1A800000, CSINC_W = 0x1A800400,
	LDR_W = 0xB9400000, STR_W = 0xB9000000, LDR_X = 0xF9400000, STR_X = 0xF9000000,
	LDRB = 0x39400000, STRB = 0x39000000, LDRH = 0x79400000, STRH = 0x79000000,
	LDR_WR = 0xB8604800, STR_WR = 0xB8204800, LDRB_R = 0x38604800, STRB_R = 0x38204800,
	LDRH_R = 0x78604800, STRH_R = 0x78204800, LDR_XR = 0xF8604800, STR_XR = 0xF8204800, REV_X = 0xDAC00C00,
	AND_I = 0x12000000, ORR_I = 0x32000000, EOR_I = 0x52000000,
	// the floating point (double; s <- d / d <- s conversions, moves to / from the integer registers)
	FADD_D = 0x1E602800, FSUB_D = 0x1E603800, FMUL_D = 0x1E600800, FDIV_D = 0x1E601800,
	FMADD_D = 0x1F400000, FMSUB_D = 0x1F408000, FNMADD_D = 0x1F600000, FNMSUB_D = 0x1F608000,
	FNEG_D = 0x1E614000, FABS_D = 0x1E60C000, FMOV_D = 0x1E604000,
	FCVT_SD = 0x1E624000, FCVT_DS = 0x1E22C000, FCVTZS_WD = 0x1E780000,
	FMOV_DX = 0x9E670000, FMOV_XD = 0x9E660000, FMOV_SW = 0x1E270000, FMOV_WS = 0x1E260000,
	FCMP_D = 0x1E602000, FCMP_D0 = 0x1E602008, FCSEL_D = 0x1E600C00, FMOV_D1 = 0x1E6E1000,
	LDR_D = 0xFD400000, STR_D = 0xFD000000
};

// a logical immediate (AND / ORR / EOR #imm): N:immr:imms, false if the value has no encoding
static bool logImm (u64 v, bool x, u32 &enc)
{
	if (!x) v = (v & 0xFFFFFFFFull) | (v << 32);
	if (v == 0 || v == ~0ull) return false;
	int size = 64;
	while (size > 2)
	{
		int h = size / 2; u64 m = (1ull << h) - 1;
		if ((v & m) != ((v >> h) & m)) break;
		size = h;
	}
	u64 mk = size == 64 ? ~0ull : (1ull << size) - 1, e = v & mk;
	int ones = __builtin_popcountll (e);
	u64 run = ones == 64 ? ~0ull : (1ull << ones) - 1;
	for (int r = 0; r < size; r++)
	{
		u64 rot = r ? ((e >> r) | (e << (size - r))) & mk : e;
		if (rot == run)
		{
			u32 immr = (u32) ((size - r) % size), imms = (u32) ((0x3F & ~((size << 1) - 1)) | (ones - 1));
			enc = (size == 64 ? 1u << 22 : 0) | immr << 16 | imms << 10;
			return true;
		}
	}
	return false;
}

struct Asm
{
	u32 *p;
	void put (u32 i) { *p++ = i; }
	void movz (int rd, u32 imm, int hw, bool x = false) { put ((x ? 0xD2800000u : 0x52800000u) | (u32) hw << 21 | (imm & 0xFFFF) << 5 | rd); }
	void movk (int rd, u32 imm, int hw, bool x = false) { put ((x ? 0xF2800000u : 0x72800000u) | (u32) hw << 21 | (imm & 0xFFFF) << 5 | rd); }
	void movn (int rd, u32 imm, int hw) { put (0x12800000u | (u32) hw << 21 | (imm & 0xFFFF) << 5 | rd); }
	void movw (int rd, u32 v)
	{
		if (v <= 0xFFFF) movz (rd, v, 0);
		else if (!(v & 0xFFFF)) movz (rd, v >> 16, 1);
		else if (~v <= 0xFFFF) movn (rd, ~v, 0);
		else if (!(~v & 0xFFFF)) movn (rd, ~v >> 16, 1);
		else { movz (rd, v, 0); movk (rd, v >> 16, 1); }
	}
	void movx (int rd, u64 v)
	{
		movz (rd, (u32) v, 0, true);
		for (int hw = 1; hw < 4; hw++) if ((v >> (hw * 16)) & 0xFFFF) movk (rd, (u32) (v >> (hw * 16)), hw, true);
	}
	void mov (int rd, int rm) { put (ORR_W | (u32) rm << 16 | 31 << 5 | rd); }
	void movX (int rd, int rm) { put (ORR_W | X64 | (u32) rm << 16 | 31 << 5 | rd); }
	// rd = rn op (rm shifted: sh 0 LSL / 1 LSR / 2 ASR / 3 ROR, amt)
	void alu (u32 op, int rd, int rn, int rm, int sh = 0, int amt = 0) { put (op | (u32) sh << 22 | (u32) rm << 16 | (u32) amt << 10 | (u32) rn << 5 | rd); }
	void imm (u32 op, int rd, int rn, u32 imm12, int sh = 0) { put (op | (u32) sh << 22 | (imm12 & 0xFFF) << 10 | (u32) rn << 5 | rd); }
	void bfm (u32 op, int rd, int rn, int immr, int imms) { put (op | (u32) immr << 16 | (u32) imms << 10 | (u32) rn << 5 | rd); }
	void un (u32 op, int rd, int rn) { put (op | (u32) rn << 5 | rd); }
	void csel (u32 op, int rd, int rn, int rm, int cond) { put (op | (u32) rm << 16 | (u32) cond << 12 | (u32) rn << 5 | rd); }
	void cset (int rd, int cond) { csel (CSINC_W, rd, WZR, WZR, cond ^ 1); }
	void cmp (int rn, int rm) { alu (SUBS_W, WZR, rn, rm); }
	void cmpi (int rn, u32 v) { imm (SUBS_WI, WZR, rn, v); }
	void lsl (int rd, int rn, int s) { bfm (UBFM_W, rd, rn, (32 - s) & 31, 31 - s); }
	void lsr (int rd, int rn, int s) { bfm (UBFM_W, rd, rn, s, 31); }
	void asr (int rd, int rn, int s) { bfm (SBFM_W, rd, rn, s, 31); }
	void ubfx (int rd, int rn, int lsb, int w) { bfm (UBFM_W, rd, rn, lsb, lsb + w - 1); }
	void bfi (int rd, int rn, int lsb, int w) { bfm (BFM_W, rd, rn, (32 - lsb) & 31, w - 1); }
	void ror (int rd, int rn, int s) { put (0x13800000u | (u32) rn << 16 | (u32) (s & 31) << 10 | (u32) rn << 5 | rd); }
	// rt <-> [rn + off] (scaled unsigned offset; else through x17)
	void ldst (u32 op, int scale, int rt, int rn, int off)
	{
		if (off >= 0 && !(off & ((1 << scale) - 1)) && (off >> scale) < 4096) { put (op | (u32) (off >> scale) << 10 | (u32) rn << 5 | rt); return; }
		movx (17, (u64) (s64) off);
		alu (ADD_W | X64, 17, rn, 17);
		put (op | 17u << 5 | rt);
	}
	void ldstr (u32 op, int rt, int rn, int rm) { put (op | (u32) rm << 16 | (u32) rn << 5 | rt); }
	// rd = rn op #v (AND / ORR / EOR; through w/x16 when v has no encoding)
	void logi (u32 op, int rd, int rn, u64 v, bool x = false)
	{
		u32 enc;
		if (logImm (v, x, enc)) { put (op | (x ? (u32) X64 : 0u) | enc | (u32) rn << 5 | rd); return; }
		if (x) movx (16, v); else movw (16, (u32) v);
		u32 reg = op == AND_I ? (u32) AND_W : op == ORR_I ? (u32) ORR_W : (u32) EOR_W;
		alu (reg | (x ? (u32) X64 : 0u), rd, rn, 16);
	}
	void fp1 (u32 op, int rd, int rn) { put (op | (u32) rn << 5 | rd); }
	void fp3 (u32 op, int rd, int rn, int rm) { put (op | (u32) rm << 16 | (u32) rn << 5 | rd); }
	void fp4 (u32 op, int rd, int rn, int rm, int ra) { put (op | (u32) rm << 16 | (u32) ra << 10 | (u32) rn << 5 | rd); }
	void fcsel (int rd, int rn, int rm, int cond) { put (FCSEL_D | (u32) rm << 16 | (u32) cond << 12 | (u32) rn << 5 | rd); }
	// branches (to a known place; a forward one is put as 0 and patched)
	static u32 rel (u32 *from, u32 *to, int bits) { return (u32) (to - from) & ((1u << bits) - 1); }
	void b (u32 *t) { put (0x14000000u | rel (p, t, 26)); }
	void bcond (int cond, u32 *t) { put (0x54000000u | rel (p, t, 19) << 5 | (u32) cond); }
	void cbz (int rt, u32 *t, bool nz = false, bool x = false) { put ((nz ? 0x35000000u : 0x34000000u) | (x ? (u32) X64 : 0u) | rel (p, t, 19) << 5 | rt); }
	void tbz (int rt, int bit, u32 *t, bool nz = false) { put ((nz ? 0x37000000u : 0x36000000u) | (u32) (bit >> 5) << 31 | (u32) (bit & 31) << 19 | rel (p, t, 14) << 5 | rt); }
	void br (int rn) { put (0xD61F0000u | (u32) rn << 5); }
	void blr (int rn) { put (0xD63F0000u | (u32) rn << 5); }
	void ret () { put (0xD65F03C0u); }
	static void patch (u32 *at, u32 *to)
	{
		u32 i = *at;
		if ((i & 0x7C000000u) == 0x14000000u) *at = (i & 0xFC000000u) | rel (at, to, 26);
		else if ((i & 0x7E000000u) == 0x36000000u) *at = (i & 0xFFF8001Fu) | rel (at, to, 14) << 5;
		else *at = (i & 0xFF00001Fu) | rel (at, to, 19) << 5;	// b.cond, cbz / cbnz
	}
};

// ---- the JIT ----------------------------------------------------------------------------------------------------
struct Jit
{
	struct Block { u32 key, pa, hashNext, pageNext; void *code; };
	struct Fast { u32 key, pad; void *code; };

	Machine *m;
	Asm a;
	u32 *code, *codeEnd, *codeStart;
	u32 *dispatch, *exitStub;
	void (*enter) (Machine *, void *, u8 *);
	u8 *ctx;					// the helpers' addresses, then the fast table (at FAST_OFF)
	Fast *fast;
	Block *blocks; u32 nBlocks;
	u32 hashHead[1 << HASH_BITS];
	u32 pageHead[NPAGES];
	bool stdMap;					// the BATs map 0x80000000 / 0xC0000000 onto MEM1 as the OS does
	// the Machine's fields
	int oPc, oCycles, oUntil, oCr, oXer, oLr, oCtr, oMsr, oMem1, oScratch, oPs0, oPs1, oFpscr, oFprfVal, oFprfPend, oGqr;
	// the block being translated
	u32 bKey, synced; int dmode; bool fpOk;

	Jit (Machine *mm, void *mem, u32 size);
	void flushAll ();
	void *lookup (u32 key);
	void *compile (u32 pc, u32 key);
	void invalidate (u32 pa, u32 len);
	void unlink (u32 i);

	// the helpers (the interpreter; the memory the fast path does not reach): return with bit
	// 32 set (or 1 for H_INTERP) when an exception / a branch happened -- pc is where to go
	static u64 hInterp (Machine *m, u32 op, u32 pc)
	{
		m->curPc = pc; m->npc = pc + 4; m->memFault = false;
		m->exec (op);
		if (m->npc != pc + 4 || m->halted) { m->pc = m->npc; return 1; }
		return 0;
	}
	static u64 hRd8 (Machine *m, u32 ea, u32 pc) { m->curPc = pc; m->memFault = false; u32 v = m->read8 (ea); return m->memFault ? 1ull << 32 : v; }
	static u64 hRd16 (Machine *m, u32 ea, u32 pc) { m->curPc = pc; m->memFault = false; u32 v = m->read16 (ea); return m->memFault ? 1ull << 32 : v; }
	static u64 hRd32 (Machine *m, u32 ea, u32 pc) { m->curPc = pc; m->memFault = false; u32 v = m->read32 (ea); return m->memFault ? 1ull << 32 : v; }
	static u64 hWr8 (Machine *m, u32 ea, u32 v, u32 pc) { m->curPc = pc; m->memFault = false; m->write8 (ea, (u8) v); return m->memFault ? 1ull << 32 : 0; }
	static u64 hWr16 (Machine *m, u32 ea, u32 v, u32 pc) { m->curPc = pc; m->memFault = false; m->write16 (ea, (u16) v); return m->memFault ? 1ull << 32 : 0; }
	static u64 hWr32 (Machine *m, u32 ea, u32 v, u32 pc) { m->curPc = pc; m->memFault = false; m->write32 (ea, v); return m->memFault ? 1ull << 32 : 0; }
	static u64 hRd64 (Machine *m, u32 ea, u32 pc) { m->curPc = pc; m->memFault = false; m->jitScratch = m->read64 (ea); return m->memFault ? 1ull << 32 : 0; }
	static u64 hWr64 (Machine *m, u32 ea, u64 v, u32 pc) { m->curPc = pc; m->memFault = false; m->write64 (ea, v); return m->memFault ? 1ull << 32 : 0; }
	static u64 hCvtD (u32 v) { return cvtToDouble (v); }
	static u32 hCvtS (u64 v) { return cvtToSingle (v); }
	static void hFprf (Machine *m) { if (m->fprfPending) { m->fprfPending = false; m->setFprf (m->fprfVal); } }

	// ---- emitting ----
	void ldG (int w, int r) { a.ldst (LDR_W, 2, w, XM, r * 4); }
	void stG (int w, int r) { a.ldst (STR_W, 2, w, XM, r * 4); }
	void ldF (int w, int off) { a.ldst (LDR_W, 2, w, XM, off); }
	void stF (int w, int off) { a.ldst (STR_W, 2, w, XM, off); }
	void addConst (int rd, int rn, u32 c)
	{
		if (c == 0) { if (rd != rn) a.mov (rd, rn); }
		else if (c < 4096) a.imm (ADD_WI, rd, rn, c);
		else if (0u - c < 4096) a.imm (SUB_WI, rd, rn, 0u - c);
		else if (!(c & 0xFFF) && c < 0x1000000) a.imm (ADD_WI, rd, rn, c >> 12, 1);
		else { a.movw (4, c); a.alu (ADD_W, rd, rn, 4); }
	}
	void addCycles (int reg, u32 n)			// m->cycles += n (through x<reg>)
	{
		if (!n) return;
		a.ldst (LDR_X, 3, reg, XM, oCycles);
		a.imm (ADD_WI | X64, reg, reg, n);
		a.ldst (STR_X, 3, reg, XM, oCycles);
	}
	void sync (u32 total) { if (total > synced) { addCycles (5, total - synced); synced = total; } }
	void helper (int h) { a.ldst (LDR_X, 3, 16, XCTX, h * 8); a.blr (16); }
	// leave the block: pc = w0; its cycles; to the next block (dispatcher) or to jitRun
	void exitReg (u32 total, bool toC)
	{
		stF (0, oPc);
		a.ldst (LDR_X, 3, 1, XM, oCycles);
		if (total > synced) a.imm (ADD_WI | X64, 1, 1, total - synced);
		a.ldst (STR_X, 3, 1, XM, oCycles);
		if (toC) { a.b (exitStub); return; }
		a.movw (2, bKey & 3);
		a.b (dispatch);
	}
	void exitTo (u32 target, u32 total, bool toC) { a.movw (0, target); exitReg (total, toC); }

	// CR field crf from the flags of a compare (signed / unsigned) + XER's SO
	void crFromFlags (int crf, bool sgn)
	{
		a.movz (3, 4, 0); a.movz (4, 8, 0);
		a.csel (CSEL_W, 3, 4, 3, sgn ? LT : LO);
		a.movz (4, 2, 0);
		a.csel (CSEL_W, 3, 4, 3, EQ);
		ldF (4, oXer);
		a.alu (ORR_W, 3, 3, 4, 1, 31);
		ldF (4, oCr);
		a.bfi (4, 3, 28 - crf * 4, 4);
		stF (4, oCr);
	}
	void setCr0 (int w) { a.cmpi (w, 0); crFromFlags (0, true); }
	void setCaReg (int w) { ldF (4, oXer); a.bfi (4, w, 29, 1); stF (4, oXer); }	// XER.CA = w's bit 0
	void setCaFlag () { a.cset (3, HS); setCaReg (3); }				// XER.CA = the carry
	void caToFlag () { ldF (4, oXer); a.ubfx (3, 4, 29, 1); a.cmpi (3, 1); }	// carry = XER.CA

	// the interpreter for one instruction (toC: then back to jitRun whatever it did)
	void interp (u32 op, u32 pc, u32 idx, bool toC)
	{
		sync (idx * 2);
		a.movX (0, XM); a.movw (1, op); a.movw (2, pc);
		helper (H_INTERP);
		u32 *j = a.p; a.cbz (0, a.p);
		addCycles (1, 2);
		a.b (exitStub);
		Asm::patch (j, a.p);
		if (toC) exitTo (pc + 4, (idx + 1) * 2, true);
	}

	// a load (w1 = the address -> w0 / x0) or a store (w1 = the address, w2 / x2 = the value); size 1 / 2 / 4 / 8
	void memop (bool store, int size, u32 pc, u32 idx)
	{
		u32 *slow1 = 0, *slow2 = 0, *done = 0;
		int ra = 1;
		if (dmode == 0) { a.cmp (1, WMSZ); slow1 = a.p; a.bcond (HS, a.p); }
		else if (dmode == 1)
		{
			slow1 = a.p; a.tbz (1, 31, a.p);
			a.ubfx (3, 1, 0, 30);
			a.cmp (3, WMSZ); slow2 = a.p; a.bcond (HS, a.p);
			ra = 3;
		}
		if (dmode != 2)
		{
			if (!store)
			{
				if (size == 8) { a.ldstr (LDR_XR, 0, XMEM, ra); a.un (REV_X, 0, 0); }
				else if (size == 4) { a.ldstr (LDR_WR, 0, XMEM, ra); a.un (REV_W, 0, 0); }
				else if (size == 2) { a.ldstr (LDRH_R, 0, XMEM, ra); a.un (REV16_W, 0, 0); }
				else a.ldstr (LDRB_R, 0, XMEM, ra);
			}
			else
			{
				if (size == 8) { a.un (REV_X, 4, 2); a.ldstr (STR_XR, 4, XMEM, ra); }
				else if (size == 4) { a.un (REV_W, 4, 2); a.ldstr (STR_WR, 4, XMEM, ra); }
				else if (size == 2) { a.un (REV16_W, 4, 2); a.ldstr (STRH_R, 4, XMEM, ra); }
				else a.ldstr (STRB_R, 2, XMEM, ra);
			}
			done = a.p; a.b (a.p);
		}
		if (slow1) Asm::patch (slow1, a.p);
		if (slow2) Asm::patch (slow2, a.p);
		u32 pend = idx * 2 - synced;
		addCycles (5, pend);
		a.movX (0, XM);
		a.movw (store ? 3 : 2, pc);
		static const int hs[2][9] = { { 0, H_RD8, H_RD16, 0, H_RD32, 0, 0, 0, H_RD64 }, { 0, H_WR8, H_WR16, 0, H_WR32, 0, 0, 0, H_WR64 } };
		helper (hs[store][size]);
		u32 *ok = a.p; a.tbz (0, 32, a.p);
		addCycles (5, 2);
		a.b (exitStub);
		Asm::patch (ok, a.p);
		if (pend)
		{
			a.ldst (LDR_X, 3, 5, XM, oCycles);
			a.imm (SUB_WI | X64, 5, 5, pend);
			a.ldst (STR_X, 3, 5, XM, oCycles);
		}
		if (!store && size == 8) a.ldst (LDR_X, 3, 0, XM, oScratch);
		if (done) Asm::patch (done, a.p);
	}
	// the interpreter for this instruction off the main path (a NaN, the FPU off, a GQR type):
	// then back to the main path (-> the branch to patch to it) or out (0)
	u32 *interpSide (u32 op, u32 pc, u32 idx, bool back)
	{
		u32 pend = idx * 2 - synced;
		addCycles (5, pend);
		a.movX (0, XM); a.movw (1, op); a.movw (2, pc);
		helper (H_INTERP);
		u32 *j = a.p; a.cbz (0, a.p);
		addCycles (1, 2);
		a.b (exitStub);
		Asm::patch (j, a.p);
		if (!back) { a.movw (0, pc + 4); stF (0, oPc); addCycles (1, 2); a.b (exitStub); return 0; }
		if (pend) { a.ldst (LDR_X, 3, 5, XM, oCycles); a.imm (SUB_WI | X64, 5, 5, pend); a.ldst (STR_X, 3, 5, XM, oCycles); }
		u32 *b = a.p; a.b (a.p);
		return b;
	}
	// the FPU's use is checked once a block (MSR.FP: else the interpreter takes the exception)
	void fpCheck (u32 op, u32 pc, u32 idx)
	{
		if (fpOk) return;
		fpOk = true;
		ldF (9, oMsr);
		u32 *ok = a.p; a.tbz (9, 13, a.p, true);
		interpSide (op, pc, idx, false);
		Asm::patch (ok, a.p);
	}
	void ldPs (int dd, int r, int half) { a.ldst (LDR_D, 3, dd, XM, (half ? oPs1 : oPs0) + 8 * r); }
	void stPs (int dd, int r, int half) { a.ldst (STR_D, 3, dd, XM, (half ? oPs1 : oPs0) + 8 * r); }
	void fprf (int dd) { a.ldst (STR_D, 3, dd, XM, oFprfVal); a.ldst (STRB, 0, WONE, XM, oFprfPend); }
	void roundS (int dd) { a.fp1 (FCVT_SD, dd, dd); a.fp1 (FCVT_DS, dd, dd); }
	// the multiplicand of a single-precision multiply: its mantissa rounded to 25 bits (dd in place)
	void force25 (int dd)
	{
		a.fp1 (FMOV_XD, 9, dd);
		a.logi (AND_I, 10, 9, 0x8000000ull, true);
		a.logi (AND_I, 9, 9, 0xFFFFFFFFF8000000ull, true);
		a.alu (ADD_W | X64, 9, 9, 10);
		a.fp1 (FMOV_DX, dd, 9);
	}
	// a single's bits (w ws) -> the double the 750 loads (d dd): FCVT; inf / a signalling NaN: cvtToDouble
	void cvtD (int dd, int ws)
	{
		a.ubfx (10, ws, 22, 9); a.cmpi (10, 0x1FE);
		u32 *slow = a.p; a.bcond (EQ, a.p);
		a.fp1 (FMOV_SW, dd, ws); a.fp1 (FCVT_DS, dd, dd);
		u32 *done = a.p; a.b (a.p);
		Asm::patch (slow, a.p);
		if (ws != 0) a.mov (0, ws);
		helper (H_CVTD);
		a.fp1 (FMOV_DX, dd, 0);
		Asm::patch (done, a.p);
	}
	// a double's bits (x xs) -> the single the 750 stores (w wd): the bits taken as they are; the
	// denormal range: cvtToSingle
	void cvtS (int wd, int xs)
	{
		a.bfm (UBFM_X, 9, xs, 52, 62);
		a.cmpi (9, 896);
		u32 *fast1 = a.p; a.bcond (HI, a.p);
		a.bfm (UBFM_X, 10, xs, 63, 62);				// (x << 1: zero)
		u32 *fast2 = a.p; a.cbz (10, a.p, false, true);
		a.movX (0, xs);
		helper (H_CVTS);
		a.mov (wd, 0);
		u32 *done = a.p; a.b (a.p);
		Asm::patch (fast1, a.p); Asm::patch (fast2, a.p);
		a.bfm (UBFM_X, 10, xs, 32, 63);
		a.logi (AND_I, 10, 10, 0xC0000000u);
		a.bfm (UBFM_X, 11, xs, 29, 58);
		a.alu (ORR_W, wd, 10, 11);
		Asm::patch (done, a.p);
	}
	bool fpInsn (u32 op, u32 pc, u32 idx);
	bool fpLoadStore (u32 op, u32 pc, u32 idx, int kind, bool x, bool upd);
	bool psq (u32 op, u32 pc, u32 idx, bool load, bool upd);
	// the effective address into w1: (rA|0) + d, or (rA|0) + rB
	void eaD (u32 op, bool upd)
	{
		u32 ra = (op >> 16) & 31, d = (u32) (s32) (s16) op;
		if (ra == 0 && !upd) { a.movw (1, d); return; }
		ldG (1, (int) ra); addConst (1, 1, d);
	}
	void eaX (u32 op, bool upd)
	{
		u32 ra = (op >> 16) & 31, rb = (op >> 11) & 31;
		ldG (1, (int) rb);
		if (ra == 0 && !upd) return;
		ldG (4, (int) ra); a.alu (ADD_W, 1, 4, 1);
	}
	// a load / store: size, sign-extended (lha), with update (rA = the address)
	void load (u32 op, bool x, int size, bool sext, bool upd, u32 pc, u32 idx)
	{
		if (x) eaX (op, upd); else eaD (op, upd);
		if (upd) a.mov (WEA, 1);
		memop (false, size, pc, idx);
		if (sext) a.bfm (SBFM_W, 0, 0, 0, 15);
		stG (0, (int) ((op >> 21) & 31));
		if (upd) stG (WEA, (int) ((op >> 16) & 31));
	}
	void store (u32 op, bool x, int size, bool upd, u32 pc, u32 idx)
	{
		if (x) eaX (op, upd); else eaD (op, upd);
		if (upd) a.mov (WEA, 1);
		ldG (2, (int) ((op >> 21) & 31));
		memop (true, size, pc, idx);
		if (upd) stG (WEA, (int) ((op >> 16) & 31));
	}
	// rd = w9 & mask(mb, me)
	void andMask (int rd, int rn, int mb, int me)
	{
		u32 mk = mask (mb, me);
		if (mk == 0xFFFFFFFFu) { a.mov (rd, rn); return; }
		if (mb <= me && me == 31) { a.ubfx (rd, rn, 0, 32 - mb); return; }
		a.movw (10, mk); a.alu (AND_W, rd, rn, 10);
	}
	// the branch's conditions (BO, BI): a jump to the "not taken" place for each, 0..2
	int bcTests (u32 bo, u32 bi, bool ctrToo, u32 **nt)
	{
		int n = 0;
		if (ctrToo && !(bo & 4))
		{
			ldF (5, oCtr); a.imm (SUB_WI, 5, 5, 1); stF (5, oCtr);
			nt[n++] = a.p; a.cbz (5, a.p, (bo & 2) != 0);	// (bo & 2: branch if ctr == 0)
		}
		if (!(bo & 16))
		{
			ldF (5, oCr);
			nt[n++] = a.p; a.tbz (5, (int) (31 - bi), a.p, !(bo & 8));	// (bo & 8: branch if the bit is set)
		}
		return n;
	}

	bool insn (u32 op, u32 pc, u32 idx);
	bool op31 (u32 op, u32 pc, u32 idx);
};

// one instruction -> true: the block ends with it
bool Jit::insn (u32 op, u32 pc, u32 idx)
{
	int d = (int) ((op >> 21) & 31), ra = (int) ((op >> 16) & 31), rb = (int) ((op >> 11) & 31);
	u32 simm = (u32) (s32) (s16) op, uimm = op & 0xFFFF;
	bool rc = op & 1;
	switch (op >> 26)
	{
	case 7: ldG (6, ra); a.movw (7, simm); a.alu (MUL_W, 8, 6, 7); stG (8, d); return false;	// mulli
	case 8: ldG (6, ra); a.movw (7, simm); a.alu (SUBS_W, 8, 7, 6); setCaFlag (); stG (8, d); return false;	// subfic
	case 10:							// cmpli
		ldG (6, ra);
		if (uimm < 4096) a.cmpi (6, uimm); else { a.movw (7, uimm); a.cmp (6, 7); }
		crFromFlags (d >> 2, false);
		return false;
	case 11:							// cmpi
		ldG (6, ra);
		if (simm < 4096) a.cmpi (6, simm); else { a.movw (7, simm); a.cmp (6, 7); }
		crFromFlags (d >> 2, true);
		return false;
	case 12: case 13:						// addic, addic.
		ldG (6, ra); a.movw (7, simm); a.alu (ADDS_W, 8, 6, 7); setCaFlag (); stG (8, d);
		if ((op >> 26) == 13) setCr0 (8);
		return false;
	case 14: case 15:						// addi, addis
	{
		u32 c = (op >> 26) == 15 ? uimm << 16 : simm;
		if (ra == 0) a.movw (8, c); else { ldG (6, ra); addConst (8, 6, c); }
		stG (8, d);
		return false;
	}
	case 16:							// bc
	{
		u32 target = ((op & 2) ? 0 : pc) + (u32) (s32) (s16) (op & 0xFFFC);
		if (op & 1) { a.movw (4, pc + 4); stF (4, oLr); }
		u32 *nt[2]; int n = bcTests ((u32) d, (u32) ra, true, nt);
		exitTo (target, (idx + 1) * 2, false);
		if (n) { for (int i = 0; i < n; i++) Asm::patch (nt[i], a.p); exitTo (pc + 4, (idx + 1) * 2, false); }
		return true;
	}
	case 18:							// b
	{
		u32 off = op & 0x03FFFFFC; if (off & 0x02000000) off |= 0xFC000000;
		u32 target = ((op & 2) ? 0 : pc) + off;
		if (op & 1) { a.movw (4, pc + 4); stF (4, oLr); }
		if (target == pc && idx == 0 && !(op & 1))		// "b .": idle until the next event
		{
			a.movw (0, target); stF (0, oPc);
			a.ldst (LDR_X, 3, 1, XM, oCycles);
			a.imm (ADD_WI | X64, 1, 1, 2);
			a.ldst (LDR_X, 3, 3, XM, oUntil);
			a.alu (SUBS_W | X64, WZR, 1, 3);
			a.csel (CSEL_W | X64, 1, 3, 1, LO);
			a.ldst (STR_X, 3, 1, XM, oCycles);
			a.b (exitStub);
			return true;
		}
		exitTo (target, (idx + 1) * 2, false);
		return true;
	}
	case 19:
		switch ((op >> 1) & 0x3FF)
		{
		case 16: case 528:					// bclr, bcctr
		{
			bool toLr = ((op >> 1) & 0x3FF) == 16;
			ldF (9, toLr ? oLr : oCtr); a.movn (10, 3, 0); a.alu (AND_W, 9, 9, 10);
			if (op & 1) { a.movw (4, pc + 4); stF (4, oLr); }
			u32 *nt[2]; int n = bcTests ((u32) d, (u32) ra, toLr, nt);
			a.mov (0, 9);
			exitReg ((idx + 1) * 2, false);
			if (n) { for (int i = 0; i < n; i++) Asm::patch (nt[i], a.p); exitTo (pc + 4, (idx + 1) * 2, false); }
			return true;
		}
		case 150: exitTo (pc + 4, (idx + 1) * 2, false); return true;	// isync
		case 50: interp (op, pc, idx, true); return true;		// rfi
		case 257: case 129: case 289: case 225: case 33: case 449: case 417: case 193:	// the CR logic
		{
			ldF (3, oCr);
			a.ubfx (4, 3, 31 - ra, 1); a.ubfx (5, 3, 31 - rb, 1);
			switch ((op >> 1) & 0x3FF)
			{
			case 257: a.alu (AND_W, 4, 4, 5); break;
			case 129: a.alu (BIC_W, 4, 4, 5); break;
			case 289: a.alu (EON_W, 4, 4, 5); break;
			case 225: a.alu (AND_W, 4, 4, 5); a.alu (ORN_W, 4, WZR, 4); break;
			case 33: a.alu (ORR_W, 4, 4, 5); a.alu (ORN_W, 4, WZR, 4); break;
			case 449: a.alu (ORR_W, 4, 4, 5); break;
			case 417: a.alu (ORN_W, 4, 4, 5); break;
			default: a.alu (EOR_W, 4, 4, 5); break;
			}
			a.bfi (3, 4, 31 - d, 1);
			stF (3, oCr);
			return false;
		}
		}
		interp (op, pc, idx, false);
		return false;
	case 17: interp (op, pc, idx, true); return true;		// sc
	case 20:							// rlwimi
	{
		int sh = rb, mb = (int) ((op >> 6) & 31), me = (int) ((op >> 1) & 31);
		ldG (6, d); ldG (8, ra);
		if (mb <= me)
		{
			int lsb = 31 - me, w = me - mb + 1;
			a.ror (9, 6, (32 - sh + lsb) & 31);
			a.bfi (8, 9, lsb, w);
		}
		else
		{
			a.ror (9, 6, (32 - sh) & 31);
			a.movw (10, mask (mb, me));
			a.alu (AND_W, 9, 9, 10); a.alu (BIC_W, 8, 8, 10); a.alu (ORR_W, 8, 8, 9);
		}
		stG (8, ra);
		if (rc) setCr0 (8);
		return false;
	}
	case 21:							// rlwinm
	{
		int sh = rb, mb = (int) ((op >> 6) & 31), me = (int) ((op >> 1) & 31);
		ldG (6, d);
		if (sh && mb == 0 && me == 31 - sh) a.lsl (8, 6, sh);		// slwi
		else if (sh && me == 31 && mb == 32 - sh) a.lsr (8, 6, mb);	// srwi
		else
		{
			int src = 6;
			if (sh) { a.ror (9, 6, 32 - sh); src = 9; }
			andMask (8, src, mb, me);
		}
		stG (8, ra);
		if (rc) setCr0 (8);
		return false;
	}
	case 23:							// rlwnm
	{
		ldG (6, d); ldG (7, rb);
		a.alu (SUB_W, 7, WZR, 7);
		a.alu (RORV_W, 9, 6, 7);
		andMask (8, 9, (int) ((op >> 6) & 31), (int) ((op >> 1) & 31));
		stG (8, ra);
		if (rc) setCr0 (8);
		return false;
	}
	case 24: case 25: case 26: case 27: case 28: case 29:		// ori, oris, xori, xoris, andi., andis.
	{
		int o = (int) (op >> 26);
		u32 c = (o & 1) ? uimm << 16 : uimm;
		ldG (6, d);
		if (c == 0 && o <= 27) { stG (6, ra); return false; }	// (nop, mr)
		a.movw (7, c);
		a.alu (o <= 25 ? ORR_W : o <= 27 ? EOR_W : AND_W, 8, 6, 7);
		stG (8, ra);
		if (o >= 28) setCr0 (8);
		return false;
	}
	case 31: return op31 (op, pc, idx);
	case 4: case 59: case 63: return fpInsn (op, pc, idx);
	case 48: return fpLoadStore (op, pc, idx, 0, false, false);	// lfs
	case 49: return fpLoadStore (op, pc, idx, 0, false, true);	// lfsu
	case 50: return fpLoadStore (op, pc, idx, 1, false, false);	// lfd
	case 51: return fpLoadStore (op, pc, idx, 1, false, true);	// lfdu
	case 52: return fpLoadStore (op, pc, idx, 2, false, false);	// stfs
	case 53: return fpLoadStore (op, pc, idx, 2, false, true);	// stfsu
	case 54: return fpLoadStore (op, pc, idx, 3, false, false);	// stfd
	case 55: return fpLoadStore (op, pc, idx, 3, false, true);	// stfdu
	case 56: return psq (op, pc, idx, true, false);			// psq_l
	case 57: return psq (op, pc, idx, true, true);			// psq_lu
	case 60: return psq (op, pc, idx, false, false);		// psq_st
	case 61: return psq (op, pc, idx, false, true);			// psq_stu
	case 32: load (op, false, 4, false, false, pc, idx); return false;	// lwz
	case 33: load (op, false, 4, false, true, pc, idx); return false;	// lwzu
	case 34: load (op, false, 1, false, false, pc, idx); return false;	// lbz
	case 35: load (op, false, 1, false, true, pc, idx); return false;	// lbzu
	case 36: store (op, false, 4, false, pc, idx); return false;		// stw
	case 37: store (op, false, 4, true, pc, idx); return false;		// stwu
	case 38: store (op, false, 1, false, pc, idx); return false;		// stb
	case 39: store (op, false, 1, true, pc, idx); return false;		// stbu
	case 40: load (op, false, 2, false, false, pc, idx); return false;	// lhz
	case 41: load (op, false, 2, false, true, pc, idx); return false;	// lhzu
	case 42: load (op, false, 2, true, false, pc, idx); return false;	// lha
	case 43: load (op, false, 2, true, true, pc, idx); return false;	// lhau
	case 44: store (op, false, 2, false, pc, idx); return false;		// sth
	case 45: store (op, false, 2, true, pc, idx); return false;		// sthu
	}
	interp (op, pc, idx, false);
	return false;
}

bool Jit::op31 (u32 op, u32 pc, u32 idx)
{
	int d = (int) ((op >> 21) & 31), ra = (int) ((op >> 16) & 31), rb = (int) ((op >> 11) & 31);
	bool rc = op & 1;
	u32 xo = (op >> 1) & 0x3FF;
	if (!(op & 0x400))						// the XO-form arithmetic, without OE
	{
		bool done = true;
		switch ((op >> 1) & 0x1FF)
		{
		case 266: ldG (6, ra); ldG (7, rb); a.alu (ADD_W, 8, 6, 7); break;			// add
		case 10: ldG (6, ra); ldG (7, rb); a.alu (ADDS_W, 8, 6, 7); setCaFlag (); break;	// addc
		case 138: ldG (6, ra); ldG (7, rb); caToFlag (); a.alu (ADCS_W, 8, 6, 7); setCaFlag (); break;	// adde
		case 234: ldG (6, ra); a.movn (7, 0, 0); caToFlag (); a.alu (ADCS_W, 8, 6, 7); setCaFlag (); break;	// addme
		case 202: ldG (6, ra); caToFlag (); a.alu (ADCS_W, 8, 6, WZR); setCaFlag (); break;	// addze
		case 40: ldG (6, ra); ldG (7, rb); a.alu (SUB_W, 8, 7, 6); break;			// subf
		case 8: ldG (6, ra); ldG (7, rb); a.alu (SUBS_W, 8, 7, 6); setCaFlag (); break;	// subfc
		case 136: ldG (6, ra); ldG (7, rb); caToFlag (); a.alu (SBCS_W, 8, 7, 6); setCaFlag (); break;	// subfe
		case 232: ldG (6, ra); a.movn (7, 0, 0); caToFlag (); a.alu (SBCS_W, 8, 7, 6); setCaFlag (); break;	// subfme
		case 200: ldG (6, ra); caToFlag (); a.alu (SBCS_W, 8, WZR, 6); setCaFlag (); break;	// subfze
		case 104: ldG (6, ra); a.alu (SUB_W, 8, WZR, 6); break;				// neg
		case 235: ldG (6, ra); ldG (7, rb); a.alu (MUL_W, 8, 6, 7); break;			// mullw
		case 75: ldG (6, ra); ldG (7, rb); a.alu (SMULL, 8, 6, 7); a.bfm (UBFM_X, 8, 8, 32, 63); break;	// mulhw
		case 11: ldG (6, ra); ldG (7, rb); a.alu (UMULL, 8, 6, 7); a.bfm (UBFM_X, 8, 8, 32, 63); break;	// mulhwu
		case 459: ldG (6, ra); ldG (7, rb); a.alu (UDIV_W, 8, 6, 7); break;			// divwu
		default: done = false;
		}
		if (done)
		{
			stG (8, d);
			if (rc) setCr0 (8);
			return false;
		}
	}
	switch (xo)
	{
	case 0: case 32:						// cmp, cmpl
		ldG (6, ra); ldG (7, rb); a.cmp (6, 7);
		crFromFlags (d >> 2, xo == 0);
		return false;
	case 28: case 60: case 444: case 412: case 316: case 476: case 124: case 284:	// and andc or orc xor nand nor eqv
	case 24: case 536: case 792: case 824: case 26: case 954: case 922:	// slw srw sraw srawi cntlzw extsb extsh
	{
		ldG (6, d);
		if (xo != 824 && xo != 26 && xo != 954 && xo != 922) ldG (7, rb);
		switch (xo)
		{
		case 28: a.alu (AND_W, 8, 6, 7); break;
		case 60: a.alu (BIC_W, 8, 6, 7); break;
		case 444: a.alu (ORR_W, 8, 6, 7); break;
		case 412: a.alu (ORN_W, 8, 6, 7); break;
		case 316: a.alu (EOR_W, 8, 6, 7); break;
		case 476: a.alu (AND_W, 8, 6, 7); a.alu (ORN_W, 8, WZR, 8); break;
		case 124: a.alu (ORR_W, 8, 6, 7); a.alu (ORN_W, 8, WZR, 8); break;
		case 284: a.alu (EON_W, 8, 6, 7); break;
		case 24: a.alu (LSLV_W | X64, 8, 6, 7); break;		// (64-bit: a shift of 32..63 gives 0)
		case 536: a.alu (LSRV_W | X64, 8, 6, 7); break;
		case 792:						// sraw
			a.bfm (SBFM_X, 6, 6, 0, 31);
			a.alu (ASRV_W | X64, 8, 6, 7);
			a.alu (LSLV_W | X64, 9, 8, 7);
			a.alu (SUBS_W | X64, WZR, 9, 6);
			a.cset (3, NE);
			a.alu (AND_W, 3, 3, 6, 1, 31);
			setCaReg (3);
			break;
		case 824:						// srawi
			if (rb == 0) { a.mov (8, 6); setCaReg (WZR); break; }
			a.asr (8, 6, rb);
			a.lsl (9, 6, 32 - rb);
			a.cmpi (9, 0);
			a.cset (3, NE);
			a.alu (AND_W, 3, 3, 6, 1, 31);
			setCaReg (3);
			break;
		case 26: a.un (CLZ_W, 8, 6); break;
		case 954: a.bfm (SBFM_W, 8, 6, 0, 7); break;
		case 922: a.bfm (SBFM_W, 8, 6, 0, 15); break;
		}
		stG (8, ra);
		if (rc) setCr0 (8);
		return false;
	}
	case 19: ldF (8, oCr); stG (8, d); return false;		// mfcr
	case 144:							// mtcrf
	{
		u32 crm = (op >> 12) & 0xFF, mk = 0;
		for (int i = 0; i < 8; i++) if (crm & (0x80 >> i)) mk |= 0xF0000000u >> (i * 4);
		ldG (6, d);
		if (mk == 0xFFFFFFFFu) { stF (6, oCr); return false; }
		a.movw (10, mk); ldF (8, oCr);
		a.alu (BIC_W, 8, 8, 10); a.alu (AND_W, 6, 6, 10); a.alu (ORR_W, 8, 8, 6);
		stF (8, oCr);
		return false;
	}
	case 83: ldF (8, oMsr); stG (8, d); return false;		// mfmsr
	case 146: interp (op, pc, idx, true); return true;		// mtmsr
	case 339:							// mfspr
	{
		u32 n = ((op >> 16) & 31) | ((op >> 6) & 0x3E0);
		if (n == 1 || n == 8 || n == 9) { ldF (8, n == 1 ? oXer : n == 8 ? oLr : oCtr); stG (8, d); return false; }
		interp (op, pc, idx, false);
		return false;
	}
	case 467:							// mtspr
	{
		u32 n = ((op >> 16) & 31) | ((op >> 6) & 0x3E0);
		if (n == 8 || n == 9) { ldG (6, d); stF (6, n == 8 ? oLr : oCtr); return false; }
		if (n == 1) { ldG (6, d); a.movw (10, 0xE000FF7F); a.alu (AND_W, 6, 6, 10); stF (6, oXer); return false; }
		interp (op, pc, idx, true);				// (BATs, HID0, the DEC...: back to jitRun)
		return true;
	}
	case 86: case 54: case 278: case 246: case 598: case 854: case 306: case 566: case 470: return false;	// dcbf dcbst dcbt dcbtst sync eieio tlbie tlbsync dcbi
	case 23: load (op, true, 4, false, false, pc, idx); return false;	// lwzx
	case 55: load (op, true, 4, false, true, pc, idx); return false;	// lwzux
	case 87: load (op, true, 1, false, false, pc, idx); return false;	// lbzx
	case 119: load (op, true, 1, false, true, pc, idx); return false;	// lbzux
	case 279: load (op, true, 2, false, false, pc, idx); return false;	// lhzx
	case 311: load (op, true, 2, false, true, pc, idx); return false;	// lhzux
	case 343: load (op, true, 2, true, false, pc, idx); return false;	// lhax
	case 375: load (op, true, 2, true, true, pc, idx); return false;	// lhaux
	case 151: store (op, true, 4, false, pc, idx); return false;		// stwx
	case 183: store (op, true, 4, true, pc, idx); return false;		// stwux
	case 215: store (op, true, 1, false, pc, idx); return false;		// stbx
	case 247: store (op, true, 1, true, pc, idx); return false;		// stbux
	case 407: store (op, true, 2, false, pc, idx); return false;		// sthx
	case 439: store (op, true, 2, true, pc, idx); return false;		// sthux
	case 535: return fpLoadStore (op, pc, idx, 0, true, false);	// lfsx
	case 567: return fpLoadStore (op, pc, idx, 0, true, true);	// lfsux
	case 599: return fpLoadStore (op, pc, idx, 1, true, false);	// lfdx
	case 631: return fpLoadStore (op, pc, idx, 1, true, true);	// lfdux
	case 663: return fpLoadStore (op, pc, idx, 2, true, false);	// stfsx
	case 695: return fpLoadStore (op, pc, idx, 2, true, true);	// stfsux
	case 727: return fpLoadStore (op, pc, idx, 3, true, false);	// stfdx
	case 759: return fpLoadStore (op, pc, idx, 3, true, true);	// stfdux
	case 983: return fpLoadStore (op, pc, idx, 4, true, false);	// stfiwx
	}
	interp (op, pc, idx, false);
	return false;
}


// ---- the floating point, the paired singles ----------------------------------------------------------------------
// A result that is a NaN goes to the interpreter (the 750 keeps the first NaN operand); FPRF is left
// to set (fprfVal / fprfPending: the interpreter sets it when it looks at the FPSCR).
bool Jit::fpInsn (u32 op, u32 pc, u32 idx)
{
	u32 prim = op >> 26;
	int d = (int) ((op >> 21) & 31), ra = (int) ((op >> 16) & 31), rb = (int) ((op >> 11) & 31), rc = (int) ((op >> 6) & 31);
	u32 x5 = (op >> 1) & 0x1F, x10 = (op >> 1) & 0x3FF;
	if (op & 1) { interp (op, pc, idx, false); return false; }	// (Rc: CR1 from the FPSCR)
	u32 *nan[2] = { 0, 0 };
	if (prim == 59 || prim == 63)
	{
		bool single = prim == 59;
		if (x5 == 18 || x5 == 20 || x5 == 21 || x5 == 25 || x5 >= 28)
		{
			fpCheck (op, pc, idx);
			ldPs (0, ra, 0);
			if (x5 != 25) ldPs (1, rb, 0);
			if (x5 == 25 || x5 >= 28) { ldPs (2, rc, 0); if (single) force25 (2); }
			switch (x5)
			{
			case 18: a.fp3 (FDIV_D, 3, 0, 1); break;
			case 20: a.fp3 (FSUB_D, 3, 0, 1); break;
			case 21: a.fp3 (FADD_D, 3, 0, 1); break;
			case 25: a.fp3 (FMUL_D, 3, 0, 2); break;
			case 28: a.fp4 (FNMSUB_D, 3, 0, 2, 1); break;		// a * c - b
			case 29: a.fp4 (FMADD_D, 3, 0, 2, 1); break;		// a * c + b
			case 30: a.fp4 (FNMSUB_D, 3, 0, 2, 1); a.fp1 (FNEG_D, 3, 3); break;
			default: a.fp4 (FMADD_D, 3, 0, 2, 1); a.fp1 (FNEG_D, 3, 3); break;
			}
			a.fp3 (FCMP_D, 0, 3, 3); nan[0] = a.p; a.bcond (VS, a.p);
			if (single) roundS (3);
			stPs (3, d, 0);
			if (single) stPs (3, d, 1);
			fprf (3);
		}
		else if (prim == 63 && x5 == 23)				// fsel
		{
			fpCheck (op, pc, idx);
			ldPs (0, ra, 0); ldPs (1, rb, 0); ldPs (2, rc, 0);
			a.fp1 (FCMP_D0, 0, 0);
			a.fcsel (3, 2, 1, GE);
			stPs (3, d, 0);
			return false;
		}
		else if (prim == 63 && (x10 == 0 || x10 == 32))		// fcmpu, fcmpo
		{
			fpCheck (op, pc, idx);
			a.ldst (LDRB, 0, 9, XM, oFprfPend);
			u32 *j = a.p; a.cbz (9, a.p);
			a.movX (0, XM); helper (H_FPRF);
			Asm::patch (j, a.p);
			ldPs (0, ra, 0); ldPs (1, rb, 0);
			a.fp3 (FCMP_D, 0, 0, 1);
			a.movz (3, 4, 0); a.movz (4, 8, 0); a.csel (CSEL_W, 3, 4, 3, MI);
			a.movz (4, 2, 0); a.csel (CSEL_W, 3, 4, 3, EQ);
			a.movz (4, 1, 0); a.csel (CSEL_W, 3, 4, 3, VS);
			ldF (4, oCr); a.bfi (4, 3, 28 - (d >> 2) * 4, 4); stF (4, oCr);
			ldF (4, oFpscr); a.bfi (4, 3, 12, 4); stF (4, oFpscr);
			return false;
		}
		else if (prim == 63 && (x10 == 12 || x10 == 15))		// frsp, fctiwz
		{
			fpCheck (op, pc, idx);
			ldPs (1, rb, 0);
			a.fp3 (FCMP_D, 0, 1, 1); nan[0] = a.p; a.bcond (VS, a.p);
			if (x10 == 12) { roundS (1); stPs (1, d, 0); fprf (1); }
			else
			{
				a.fp1 (FCVTZS_WD, 9, 1);
				a.movz (10, 0xFFF8, 3, true);
				a.alu (ORR_W | X64, 9, 9, 10);
				a.ldst (STR_X, 3, 9, XM, oPs0 + 8 * d);
			}
		}
		else if (prim == 63 && (x10 == 40 || x10 == 72 || x10 == 136 || x10 == 264))	// fneg, fmr, fnabs, fabs
		{
			fpCheck (op, pc, idx);
			ldPs (1, rb, 0);
			if (x10 == 40) a.fp1 (FNEG_D, 1, 1);
			else if (x10 == 136) { a.fp1 (FABS_D, 1, 1); a.fp1 (FNEG_D, 1, 1); }
			else if (x10 == 264) a.fp1 (FABS_D, 1, 1);
			stPs (1, d, 0);
			return false;
		}
		else { interp (op, pc, idx, false); return false; }
	}
	else								// the paired singles
	{
		bool arith = x5 == 10 || x5 == 11 || (x5 >= 12 && x5 <= 15) || x5 == 18 || x5 == 20 || x5 == 21 || x5 == 23 || x5 == 25 || x5 >= 28;
		bool move = x10 == 40 || x10 == 72 || x10 == 136 || x10 == 264 || x10 == 528 || x10 == 560 || x10 == 592 || x10 == 624;
		if (!arith && !move) { interp (op, pc, idx, false); return false; }
		fpCheck (op, pc, idx);
		ldPs (0, ra, 0); ldPs (1, ra, 1); ldPs (2, rb, 0); ldPs (3, rb, 1); ldPs (4, rc, 0); ldPs (5, rc, 1);
		if (move)
		{
			switch (x10)
			{
			case 40: a.fp1 (FNEG_D, 6, 2); a.fp1 (FNEG_D, 7, 3); break;
			case 72: a.fp1 (FMOV_D, 6, 2); a.fp1 (FMOV_D, 7, 3); break;
			case 136: a.fp1 (FABS_D, 6, 2); a.fp1 (FNEG_D, 6, 6); a.fp1 (FABS_D, 7, 3); a.fp1 (FNEG_D, 7, 7); break;
			case 264: a.fp1 (FABS_D, 6, 2); a.fp1 (FABS_D, 7, 3); break;
			case 528: a.fp1 (FMOV_D, 6, 0); a.fp1 (FMOV_D, 7, 2); break;
			case 560: a.fp1 (FMOV_D, 6, 0); a.fp1 (FMOV_D, 7, 3); break;
			case 592: a.fp1 (FMOV_D, 6, 1); a.fp1 (FMOV_D, 7, 2); break;
			default: a.fp1 (FMOV_D, 6, 1); a.fp1 (FMOV_D, 7, 3); break;
			}
			stPs (6, d, 0); stPs (7, d, 1);
			return false;
		}
		switch (x5)
		{
		case 10: case 11:					// ps_sum0, ps_sum1
		{
			int r = x5 == 10 ? 6 : 7;
			a.fp3 (FADD_D, r, 0, 3);
			a.fp3 (FCMP_D, 0, r, r); nan[0] = a.p; a.bcond (VS, a.p);
			roundS (r);
			if (x5 == 10) { stPs (6, d, 0); stPs (5, d, 1); }
			else { stPs (4, d, 0); stPs (7, d, 1); }
			fprf (r);
			break;
		}
		case 23:						// ps_sel
			a.fp1 (FCMP_D0, 0, 0); a.fcsel (6, 4, 2, GE);
			a.fp1 (FCMP_D0, 0, 1); a.fcsel (7, 5, 3, GE);
			stPs (6, d, 0); stPs (7, d, 1);
			return false;
		default:
		{
			int m0 = 4, m1 = 5;					// the multiplicands of each half
			if (x5 == 12 || x5 == 14) m1 = 4;
			if (x5 == 13 || x5 == 15) m0 = 5;
			if (x5 == 12 || x5 == 14 || x5 == 25 || x5 >= 28) force25 (4);
			if (x5 == 13 || x5 == 15 || x5 == 25 || x5 >= 28) force25 (5);
			for (int h = 0; h < 2; h++)
			{
				int r = 6 + h, fa = h, fb = 2 + h, mc = h ? m1 : m0;
				switch (x5)
				{
				case 12: case 13: case 25: a.fp3 (FMUL_D, r, fa, mc); break;
				case 14: case 15: case 29: a.fp4 (FMADD_D, r, fa, mc, fb); break;
				case 18: a.fp3 (FDIV_D, r, fa, fb); break;
				case 20: a.fp3 (FSUB_D, r, fa, fb); break;
				case 21: a.fp3 (FADD_D, r, fa, fb); break;
				case 28: a.fp4 (FNMSUB_D, r, fa, mc, fb); break;
				case 30: a.fp4 (FNMSUB_D, r, fa, mc, fb); a.fp1 (FNEG_D, r, r); break;
				default: a.fp4 (FMADD_D, r, fa, mc, fb); a.fp1 (FNEG_D, r, r); break;
				}
			}
			a.fp3 (FCMP_D, 0, 6, 6); nan[0] = a.p; a.bcond (VS, a.p);
			a.fp3 (FCMP_D, 0, 7, 7); nan[1] = a.p; a.bcond (VS, a.p);
			roundS (6); roundS (7);
			stPs (6, d, 0); stPs (7, d, 1);
			fprf (6);
		}
		}
	}
	if (nan[0])							// a NaN: the interpreter does it again
	{
		u32 *skip = a.p; a.b (a.p);
		Asm::patch (nan[0], a.p);
		if (nan[1]) Asm::patch (nan[1], a.p);
		u32 *back = interpSide (op, pc, idx, true);
		Asm::patch (skip, a.p); Asm::patch (back, a.p);
	}
	return false;
}

// lfs (kind 0), lfd (1), stfs (2), stfd (3), stfiwx (4): D-form / X-form, with update
bool Jit::fpLoadStore (u32 op, u32 pc, u32 idx, int kind, bool x, bool upd)
{
	int d = (int) ((op >> 21) & 31), ra = (int) ((op >> 16) & 31);
	fpCheck (op, pc, idx);
	if (kind >= 2)
	{
		if (kind == 2) { a.ldst (LDR_X, 3, 12, XM, oPs0 + 8 * d); cvtS (2, 12); }
		else if (kind == 3) a.ldst (LDR_X, 3, 2, XM, oPs0 + 8 * d);
		else a.ldst (LDR_W, 2, 2, XM, oPs0 + 8 * d);
		if (x) eaX (op, upd); else eaD (op, upd);
		if (upd) a.mov (WEA, 1);
		memop (true, kind == 3 ? 8 : 4, pc, idx);
	}
	else
	{
		if (x) eaX (op, upd); else eaD (op, upd);
		if (upd) a.mov (WEA, 1);
		if (kind == 0) { memop (false, 4, pc, idx); cvtD (0, 0); stPs (0, d, 0); stPs (0, d, 1); }
		else { memop (false, 8, pc, idx); a.fp1 (FMOV_DX, 0, 0); stPs (0, d, 0); }
	}
	if (upd) stG (WEA, ra);
	return false;
}

// psq_l / psq_st (+ u): a float GQR type translated (two floats in one 8-byte access, or one with W);
// the other types (the integers scaled) through the interpreter
bool Jit::psq (u32 op, u32 pc, u32 idx, bool load, bool upd)
{
	int d = (int) ((op >> 21) & 31), ra = (int) ((op >> 16) & 31);
	int q = (int) ((op >> 12) & 7); bool w = (op >> 15) & 1;
	u32 off = (u32) ((s32) ((op & 0xFFF) << 20) >> 20);
	fpCheck (op, pc, idx);
	ldF (9, oGqr + 4 * q);
	a.ubfx (10, 9, load ? 16 : 0, 3);
	u32 *other = a.p; a.cbz (10, a.p, true);
	if (load)
	{
		if (ra == 0 && !upd) a.movw (1, off); else { ldG (1, ra); addConst (1, 1, off); }
		if (upd) a.mov (WEA, 1);
		if (w)
		{
			memop (false, 4, pc, idx);
			cvtD (0, 0); stPs (0, d, 0);
			a.put (FMOV_D1 | 1); stPs (1, d, 1);
		}
		else
		{
			memop (false, 8, pc, idx);
			a.movX (XK1, 0);
			a.bfm (UBFM_X, 0, XK1, 32, 63);
			cvtD (0, 0); stPs (0, d, 0);
			a.mov (0, XK1);
			cvtD (0, 0); stPs (0, d, 1);
		}
	}
	else
	{
		if (w) { a.ldst (LDR_X, 3, 12, XM, oPs0 + 8 * d); cvtS (2, 12); }
		else
		{
			a.ldst (LDR_X, 3, 12, XM, oPs0 + 8 * d); cvtS (XK1, 12);
			a.ldst (LDR_X, 3, 12, XM, oPs1 + 8 * d); cvtS (XK2, 12);
			a.alu (ORR_W | X64, 2, XK2, XK1, 0, 32);
		}
		if (ra == 0 && !upd) a.movw (1, off); else { ldG (1, ra); addConst (1, 1, off); }
		if (upd) a.mov (WEA, 1);
		memop (true, w ? 4 : 8, pc, idx);
	}
	if (upd) stG (WEA, ra);
	u32 *skip = a.p; a.b (a.p);
	Asm::patch (other, a.p);
	u32 *back = interpSide (op, pc, idx, true);
	Asm::patch (skip, a.p); Asm::patch (back, a.p);
	return false;
}

#define OFF(f) ((int) ((u8 *) &mm->f - (u8 *) mm))

Jit::Jit (Machine *mm, void *mem, u32 size)
{
	m = mm;
	code = (u32 *) mem; codeEnd = code + size / 4;
	oPc = OFF (pc); oCycles = OFF (cycles); oUntil = OFF (jitUntil); oCr = OFF (cr); oXer = OFF (xer);
	oLr = OFF (lr); oCtr = OFF (ctr); oMsr = OFF (msr); oMem1 = OFF (mem1); oScratch = OFF (jitScratch);
	oPs0 = OFF (ps0); oPs1 = OFF (ps1); oFpscr = OFF (fpscr); oFprfVal = OFF (fprfVal); oFprfPend = OFF (fprfPending); oGqr = OFF (gqr);
	ctx = new u8[FAST_OFF + (sizeof (Fast) << FAST_BITS)];
	fast = (Fast *) (ctx + FAST_OFF);
	u64 *h = (u64 *) ctx;
	h[H_INTERP] = (u64) &hInterp;
	h[H_RD8] = (u64) &hRd8; h[H_RD16] = (u64) &hRd16; h[H_RD32] = (u64) &hRd32;
	h[H_WR8] = (u64) &hWr8; h[H_WR16] = (u64) &hWr16; h[H_WR32] = (u64) &hWr32;
	h[H_RD64] = (u64) &hRd64; h[H_WR64] = (u64) &hWr64; h[H_CVTD] = (u64) &hCvtD; h[H_CVTS] = (u64) &hCvtS; h[H_FPRF] = (u64) &hFprf;
	blocks = new Block[MAX_BLOCKS];

	// enter (x0 = the Machine, x1 = the block, x2 = the context): save the callee-saved registers
	a.p = code;
	enter = (void (*) (Machine *, void *, u8 *)) (void *) a.p;
	a.put (0xA9BA7BFD);						// stp x29, x30, [sp, #-96]!
	a.put (0xA90153F3);						// stp x19, x20, [sp, #16]
	a.put (0xA9025BF5);						// stp x21, x22, [sp, #32]
	a.put (0xA90363F7);						// stp x23, x24, [sp, #48]
	a.put (0xA9046BF9);						// stp x25, x26, [sp, #64]
	a.put (0xA90573FB);						// stp x27, x28, [sp, #80]
	a.movX (XM, 0);
	a.ldst (LDR_X, 3, XMEM, XM, oMem1);
	a.movX (XCTX, 2);
	a.movw (WMSZ, MEM1_SIZE);
	a.movz (WONE, 1, 0);
	a.br (1);
	exitStub = a.p;
	a.put (0xA94153F3);						// ldp x19, x20, [sp, #16]
	a.put (0xA9425BF5);						// ldp x21, x22, [sp, #32]
	a.put (0xA94363F7);						// ldp x23, x24, [sp, #48]
	a.put (0xA9446BF9);						// ldp x25, x26, [sp, #64]
	a.put (0xA94573FB);						// ldp x27, x28, [sp, #80]
	a.put (0xA8C67BFD);						// ldp x29, x30, [sp], #96
	a.ret ();
	// the dispatcher: w0 = pc, x1 = cycles, w2 = the mode (IR, DR) -> the next block, or back
	dispatch = a.p;
	a.ldst (LDR_X, 3, 3, XM, oUntil);
	a.alu (SUBS_W | X64, WZR, 1, 3);
	a.bcond (HS, exitStub);
	a.alu (ORR_W, 4, 0, 2);
	a.ubfx (5, 0, 2, FAST_BITS);
	a.alu (ADD_W | X64, 5, XCTX, 5, 0, 4);
	a.ldst (LDR_W, 2, 6, 5, FAST_OFF);
	a.cmp (6, 4);
	a.bcond (NE, exitStub);
	a.ldst (LDR_X, 3, 7, 5, FAST_OFF + 8);
	a.br (7);
	codeStart = code + STUB_WORDS;
	flushCode (code, codeStart);
	flushAll ();
}

void Jit::flushAll ()
{
	a.p = codeStart;
	nBlocks = 0;
	for (u32 i = 0; i < (1u << HASH_BITS); i++) hashHead[i] = NONE;
	for (u32 i = 0; i < NPAGES; i++) pageHead[i] = NONE;
	for (u32 i = 0; i < (1u << FAST_BITS); i++) { fast[i].key = NOKEY; fast[i].code = 0; }
	stdMap = true;
	for (u32 i = 0; i < (MEM1_SIZE >> 17); i++)
		if (m->dmap[(0x80000000u >> 17) + i] != i + 1 || m->dmap[(0xC0000000u >> 17) + i] != i + 1) stdMap = false;
	m->jitBlocks = 0;
}

static inline u32 hashOf (u32 key) { return (key >> 2) & ((1u << HASH_BITS) - 1); }

void *Jit::lookup (u32 key)
{
	Fast &f = fast[(key >> 2) & ((1u << FAST_BITS) - 1)];
	if (f.key == key) return f.code;
	for (u32 i = hashHead[hashOf (key)]; i != NONE; i = blocks[i].hashNext)
		if (blocks[i].key == key) { f.key = key; f.code = blocks[i].code; return f.code; }
	return 0;
}

void Jit::unlink (u32 i)
{
	Block &b = blocks[i];
	u32 *pp = &hashHead[hashOf (b.key)];
	while (*pp != NONE && *pp != i) pp = &blocks[*pp].hashNext;
	if (*pp == i) *pp = b.hashNext;
	Fast &f = fast[(b.key >> 2) & ((1u << FAST_BITS) - 1)];
	if (f.key == b.key && f.code == b.code) f.key = NOKEY;
	if (m->jitBlocks) m->jitBlocks--;
}

void Jit::invalidate (u32 pa, u32 len)
{
	if (pa >= MEM1_SIZE || !len) return;
	u32 end = pa + len > (u32) MEM1_SIZE ? (u32) MEM1_SIZE : pa + len;
	for (u32 pg = pa >> 12; pg <= (end - 1) >> 12; pg++)
	{
		for (u32 i = pageHead[pg]; i != NONE; i = blocks[i].pageNext) unlink (i);
		pageHead[pg] = NONE;
	}
}

void *Jit::compile (u32 pc, u32 key)
{
	u32 pa = pc;
	if (key & 1)
	{
		u32 b = m->imap[pc >> 17];
		if (!b) return 0;
		pa = ((b - 1) << 17) | (pc & 0x1FFFF);
	}
	if (pa >= MEM1_SIZE || (pc & 3)) return 0;
	if (codeEnd - a.p < 16384 || nBlocks >= MAX_BLOCKS) flushAll ();
	bKey = key; synced = 0; fpOk = false;
	dmode = !(key & 2) ? 0 : stdMap ? 1 : 2;
	u32 *start = a.p;
	for (u32 n = 0;;)
	{
		u32 op = bswap32 (*(const u32 *) (m->mem1 + pa + n * 4));
		bool end = insn (op, pc + n * 4, n);
		n++;
		if (end) break;
		if (n >= MAX_INSNS || !((pa + n * 4) & 0xFFF)) { exitTo (pc + n * 4, n * 2, false); break; }
	}
	flushCode (start, a.p);
	u32 i = nBlocks++;
	Block &b = blocks[i];
	b.key = key; b.pa = pa; b.code = start;
	b.hashNext = hashHead[hashOf (key)]; hashHead[hashOf (key)] = i;
	b.pageNext = pageHead[pa >> 12]; pageHead[pa >> 12] = i;
	Fast &f = fast[(key >> 2) & ((1u << FAST_BITS) - 1)];
	f.key = key; f.code = start;
	m->jitBlocks++; m->jitCompiles++;
	return start;
}

bool Machine::jitEnable ()
{
	if (jit) return true;
	if (!codeAlloc) return false;
	void *mem = codeAlloc (CODE_SIZE);
	if (!mem) return false;
	jit = new Jit (this, mem, CODE_SIZE);
	jitFlush = false;
	return true;
}

void Machine::jitRun (u64 until)
{
	Jit &j = *jit;
	while (cycles < until && !halted)
	{
		if (jitFlush) { j.flushAll (); jitFlush = false; }
		if (cycles >= decAt) { decAt = ~0ull; dec = 0xFFFFFFFF; decPending = true; }
		if ((extIrq || decPending) && (msr & MSR_EE)) checkInterrupts ();
		jitUntil = until < decAt ? until : decAt;
		u32 key = pc | ((msr & MSR_IR) ? 1 : 0) | ((msr & MSR_DR) ? 2 : 0);
		void *c = j.lookup (key);
		if (!c) c = j.compile (pc, key);
		if (!c) { step (); continue; }				// (not in MEM1: the interpreter, its exception)
		j.enter (this, c, j.ctx);
	}
}

void Machine::jitInvalidate (u32 pa, u32 len) { if (jit) jit->invalidate (pa, len); }

#else	// not an AArch64 host: the interpreter only

struct Jit {};
bool Machine::jitEnable () { return false; }
void Machine::jitRun (u64 until) { jit = 0; run (until); }
void Machine::jitInvalidate (u32, u32) {}

#endif

} // namespace gc
