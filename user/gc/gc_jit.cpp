//
// gc/gc_jit.cpp -- the Gekko's JIT: the PowerPC code translated to AArch64 a block at a time
// (from an address to its branch, at most 64 instructions, within a 4 KB page), run until the
// cycle the machine's events are due. The integer unit, the branches, the loads and stores
// are translated (MEM1 read straight from the host's memory when the address maps it, the rest
// through the memory functions); the other instructions call the interpreter's exec, one each.
//
//   * the registers live in the Machine (x19 points at it); within a block the ones it uses
//     (the GPRs, CR, XER, LR, CTR) are kept in host registers (x9..x15, x27, x28), loaded
//     when first read and written back at the exits and before the interpreter; x20 = MEM1,
//     x21 = the context (the helpers, the fast table), w22 = MEM1's size, w23 = an address
//     kept across a call, w24 = 1; the FPRs are loaded / stored by each instruction
//   * a block ends with the next address in pc and its cycles added; the dispatcher (in the
//     code buffer) finds the next block in a direct-mapped table (address + the MSR's IR / DR)
//     and jumps to it while the cycle count is below jitUntil; else back to jitRun (C), which
//     takes the interrupts, translates what is missing
//   * a call (bl, bcctrl) is the host's own bl / blr, after pushing {its landing, the guest's
//     return address} on the host's stack; a return (blr) pops them and, LR being that address,
//     is the host's ret (predicted by the core's return stack: no dispatcher); else the stack is
//     emptied (down to its sentinel) and the dispatcher goes on. Emptied too when the JIT leaves
//     and past 8 KB (calls that never return: a return through bctr...). A landing is the
//     caller block's code: it stays in the buffer until everything is dropped (flushAll: only
//     from jitRun, when the stack is empty) and its exit is a link like the others
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
	MAX_INSNS = 64, NPAGES = MEM1_SIZE >> 12, STUB_WORDS = 256,
	CHUNK_WORDS = (1 << 20) / 4, BLOCK_ROOM = (32 << 10) / 4, COLD_SCRATCH = 64 << 10
};
static const u32 NONE = 0xFFFFFFFFu, NOKEY = 0xFFFFFFFFu;
enum { H_INTERP, H_RD8, H_RD16, H_RD32, H_WR8, H_WR16, H_WR32, H_RD64, H_WR64, H_CVTD, H_CVTS, H_FPRF, H_GATHER };

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
enum { WZR = 31, SP = 31, XM = 19, XMEM = 20, XCTX = 21, WMSZ = 22, WEA = 23, XK1 = 25, XDC = 26 };
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
	LDR_D = 0xFD400000, STR_D = 0xFD000000, LDR_Q = 0x3DC00000, STR_Q = 0x3D800000,
	// NEON on two doubles (a paired single: ps0 = lane 0, ps1 = lane 1)
	FADD_2D = 0x4E60D400, FSUB_2D = 0x4EE0D400, FMUL_2D = 0x6E60DC00, FDIV_2D = 0x6E60FC00, FMLA_2D = 0x4E60CC00,
	FNEG_2D = 0x6EE0F800, FABS_2D = 0x4EE0F800, FMUL_2DE = 0x4FC09000, FMLA_2DE = 0x4FC01000,
	FCVTN_2S = 0x0E616800, FCVTL_2D = 0x0E617800, FMAXP_D = 0x7E70F800, FCMGE0_2D = 0x6EE0C800,
	LDR_DR = 0xFC604800, STR_DR = 0xFC204800, LDR_SR = 0xBC604800, STR_SR = 0xBC204800, REV32_8B = 0x2E200800,
	BSL_16B = 0x6E601C00, ORR_16B = 0x4EA01C00, ZIP1_2D = 0x4EC03800, ZIP2_2D = 0x4EC07800, EXT_16B = 0x6E000000
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
	int side; u32 mainWords;			// (the profile: words put off the side paths)
	void put (u32 i) { *p++ = i; if (!side) mainWords++; }
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
	void rorX (int rd, int rn, int s) { put (0x93C00000u | (u32) rn << 16 | (u32) (s & 63) << 10 | (u32) rn << 5 | rd); }
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
	void vmov (int rd, int rn) { if (rd != rn) put (ORR_16B | (u32) rn << 16 | (u32) rn << 5 | rd); }
	void ins (int rd, int di, int rn, int si) { put (0x6E000400u | (u32) ((di << 4) | 8) << 16 | (u32) (si << 3) << 11 | (u32) rn << 5 | rd); }	// Vd.D[di] = Vn.D[si]
	void insX (int rd, int di, int xn) { put (0x4E001C00u | (u32) ((di << 4) | 8) << 16 | (u32) xn << 5 | rd); }	// Vd.D[di] = Xn
	void umovX (int xd, int rn, int si) { put (0x4E003C00u | (u32) ((si << 4) | 8) << 16 | (u32) rn << 5 | xd); }	// Xd = Vn.D[si]
	void byElem (u32 op, int rd, int rn, int rm, int idx) { put (op | (u32) idx << 11 | (u32) rm << 16 | (u32) rn << 5 | rd); }
	void ext8 (int rd, int rn, int rm) { put (EXT_16B | (u32) rm << 16 | 8u << 11 | (u32) rn << 5 | rd); }
	// branches (to a known place; a forward one is put as 0 and patched)
	static u32 rel (u32 *from, u32 *to, int bits) { return (u32) (to - from) & ((1u << bits) - 1); }
	void b (u32 *t) { put (0x14000000u | rel (p, t, 26)); }
	void bcond (int cond, u32 *t) { put (0x54000000u | rel (p, t, 19) << 5 | (u32) cond); }
	void cbz (int rt, u32 *t, bool nz = false, bool x = false) { put ((nz ? 0x35000000u : 0x34000000u) | (x ? (u32) X64 : 0u) | rel (p, t, 19) << 5 | rt); }
	void tbz (int rt, int bit, u32 *t, bool nz = false) { put ((nz ? 0x37000000u : 0x36000000u) | (u32) (bit >> 5) << 31 | (u32) (bit & 31) << 19 | rel (p, t, 14) << 5 | rt); }
	void br (int rn) { put (0xD61F0000u | (u32) rn << 5); }
	void blr (int rn) { put (0xD63F0000u | (u32) rn << 5); }
	void ret () { put (0xD65F03C0u); }
	void retX (int rn) { put (0xD65F0000u | (u32) rn << 5); }
	void bl (u32 *t) { put (0x94000000u | rel (p, t, 26)); }
	void adr (int rd, u32 *t) { u32 o = (u32) ((t - p) * 4); put (0x10000000u | (o & 3) << 29 | ((o >> 2) & 0x7FFFF) << 5 | rd); }
	void stpPre (int rt, int rt2, int off) { put (0xA9800000u | ((u32) (off / 8) & 0x7F) << 15 | (u32) rt2 << 10 | 31u << 5 | rt); }	// stp xt, xt2, [sp, #off]!
	void stpSp (int rt, int rt2, int off) { put (0xA9000000u | ((u32) (off / 8) & 0x7F) << 15 | (u32) rt2 << 10 | 31u << 5 | rt); }	// stp xt, xt2, [sp, #off]
	void ldpPost (int rt, int rt2, int off) { put (0xA8C00000u | ((u32) (off / 8) & 0x7F) << 15 | (u32) rt2 << 10 | 31u << 5 | rt); }	// ldp xt, xt2, [sp], #off
	void cmpSp (int rm) { put (0xEB2063FFu | (u32) rm << 16); }	// cmp sp, xm
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
	struct Block { u32 key, pa, hashNext, pageNext; void *code; u64 runs; u32 words, insns, size, prof, profN; };
	// (the profile) each translated guest instruction: its opcode, its main path's host words (the
	// block's exit when it runs out of instructions: opcode 0); a block's: profN from prof
	struct ProfInsn { u32 op, words; };
	enum { PROF_INSNS = 1 << 20 };
	ProfInsn *profInsns = 0; u32 nProf = 0;
	struct Fast { u32 key, pad; void *code; };

	Machine *m;
	Asm a;
	u32 *code, *codeEnd, *codeStart;
	// The buffer in chunks of CHUNK_WORDS: the blocks' main code from the chunk's start up (a.p), their
	// cold paths and exit stubs from its end down (coldTop): all within a b.cond's reach (1 MB) of the
	// main code, which stays dense (the I-cache). coldScratch: where the cold code is emitted once to
	// learn its length.
	u32 *chunkEnd, *coldTop, *coldScratch;
	void nextChunk ();
	u32 *dispatch, *exitStub;
	void (*enter) (Machine *, void *, u8 *);
	u8 *ctx;					// the helpers' addresses, then the fast table (at FAST_OFF)
	Fast *fast;
	Block *blocks; u32 nBlocks;
	u32 hashHead[1 << HASH_BITS];
	u32 pageHead[NPAGES];
	// the links: a block's exit to a known address jumps straight to that block once it is
	// translated (the branch at site patched; back to its stub when that block is dropped)
	struct Link { u32 key, next; u32 *site, *stub; };
	Link *links; u32 nLinks;
	u32 linkHead[1 << HASH_BITS];
	void addLink (u32 key, u32 *site, u32 *stub);
	void linkTo (u32 key, void *code);
	bool stdMap;					// the BATs map 0x80000000 / 0xC0000000 onto MEM1 as the OS does
	// the Machine's fields
	int oPc, oCycles, oUntil, oEnd, oTb, oCr, oXer, oLr, oCtr, oMsr, oMem1, oScratch, oPs, oFpscr, oFprfVal, oFprfPend, oGqr, oGatherN, oGather, oIdleHit, oSpBase, oSpLimit;
	// the block being translated
	u32 bKey, synced; int dmode; bool fpOk;
	u32 bPa, curIdx;				// (the block's address in MEM1, the instruction being translated)
	// a compare's field left in the host's flags for the conditional branch right after it (crSet):
	// made on the branch's paths where it may still be read -- deadFall / deadTaken: not needed there
	struct { bool on, sgn, deadFall, deadTaken; int crf; } pend;
	bool crDead (u32 pa, int f, int depth);
	bool idle;					// the block is a polling loop (idleLoop)
	u32 gqrOk;					// the GQRs checked in this block for psq's float type (bit q * 2 + load)
	u32 sgl0, sgl1;					// the FPRs (ps0 / ps1) known to hold a single exactly: no force25
	static void setS (u32 &mk, int r, bool v) { if (v) mk |= 1u << r; else mk &= ~(1u << r); }
	static bool isS (u32 mk, int r) { return (mk >> r) & 1; }

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
		if (m->jitInterpOps) m->jitInterpOps[(op >> 26) << 10 | ((op >> 1) & 0x3FF)]++;
		m->curPc = pc; m->npc = pc + 4; m->memFault = false;
		m->exec (op);
		if (m->npc != pc + 4 || m->halted) { m->pc = m->npc; return 1; }
		return 0;
	}
	static void slow (Machine *m, int store, u32 ea) { if (m->jitProfile) m->jitSlowMem[store][ea >> 28]++; }
	static u64 hRd8 (Machine *m, u32 ea, u32 pc) { slow (m, 0, ea); m->curPc = pc; m->memFault = false; u32 v = m->read8 (ea); return m->memFault ? 1ull << 32 : v; }
	static u64 hRd16 (Machine *m, u32 ea, u32 pc) { slow (m, 0, ea); m->curPc = pc; m->memFault = false; u32 v = m->read16 (ea); return m->memFault ? 1ull << 32 : v; }
	static u64 hRd32 (Machine *m, u32 ea, u32 pc) { slow (m, 0, ea); m->curPc = pc; m->memFault = false; u32 v = m->read32 (ea); return m->memFault ? 1ull << 32 : v; }
	static u64 hWr8 (Machine *m, u32 ea, u32 v, u32 pc) { slow (m, 1, ea); m->curPc = pc; m->memFault = false; m->write8 (ea, (u8) v); return m->memFault ? 1ull << 32 : 0; }
	static u64 hWr16 (Machine *m, u32 ea, u32 v, u32 pc) { slow (m, 1, ea); m->curPc = pc; m->memFault = false; m->write16 (ea, (u16) v); return m->memFault ? 1ull << 32 : 0; }
	static u64 hWr32 (Machine *m, u32 ea, u32 v, u32 pc) { slow (m, 1, ea); m->curPc = pc; m->memFault = false; m->write32 (ea, v); return m->memFault ? 1ull << 32 : 0; }
	static u64 hRd64 (Machine *m, u32 ea, u32 pc) { slow (m, 0, ea); m->curPc = pc; m->memFault = false; m->jitScratch = m->read64 (ea); return m->memFault ? 1ull << 32 : 0; }
	static u64 hWr64 (Machine *m, u32 ea, u64 v, u32 pc) { slow (m, 1, ea); m->curPc = pc; m->memFault = false; m->write64 (ea, v); return m->memFault ? 1ull << 32 : 0; }
	static u64 hCvtD (u32 v) { return cvtToDouble (v); }
	static u32 hCvtS (u64 v) { return cvtToSingle (v); }
	static void hFprf (Machine *m) { if (m->fprfPending) { m->fprfPending = false; m->setFprf (m->fprfVal); } }
	static void hGather (Machine *m) { m->gatherFlush (); }

	// ---- the guest registers in host registers (a cache for the block) ----
	// Guest 0..31 = the GPRs, then CR, XER, LR, CTR: loaded when first used, written back (the
	// dirty ones) at the block's exits and before a helper that reads them; a helper that does
	// not touch them (off the main path: the memory, the conversions) saves / reloads the
	// caller-saved ones around its call. The pool: x9..x15, x24, x27, x28; the temporaries are
	// w0..w8 (x16 / x17: the assembler's). A pool register may hold a GPR's pointer into MEM1
	// instead (pHost, hGuest = 64 + the GPR: basePtr) -- forgotten when that GPR is written, made
	// again after a call that clobbers it.
	enum { G_CR = 32, G_XER, G_LR, G_CTR, NG, P_BASE = 64 };
	// the guest registers kept in host registers across the blocks (the most used: LR, CR, r3, r0 --
	// callee-saved ones, which the helpers keep): no load at a block's first use, no store at its
	// exits; the enter stub loads them, the exit stub stores them, and so around the interpreter
	enum { N_SRA = 4 };
	static constexpr int SRA_G[N_SRA] = { G_LR, G_CR, 3, 0 }, SRA_H[N_SRA] = { 24, 27, 28, 29 };
	static bool isSra (int g) { return g == G_LR || g == G_CR || g == 3 || g == 0; }
	void sraMap () { for (int i = 0; i < N_SRA; i++) { gHost[SRA_G[i]] = SRA_H[i]; hGuest[SRA_H[i]] = SRA_G[i]; gDirty[SRA_G[i]] = true; } }
	void sraStore () { for (int i = 0; i < N_SRA; i++) a.ldst (STR_W, 2, SRA_H[i], XM, gOff (SRA_G[i])); }
	void sraLoad () { for (int i = 0; i < N_SRA; i++) a.ldst (LDR_W, 2, SRA_H[i], XM, gOff (SRA_G[i])); }
	int gHost[NG]; bool gDirty[NG]; int hGuest[32]; u32 hStamp[32], stamp, pinned;
	int pHost[32]; u32 bWrit;			// (the GPRs' pointers; the GPRs written so far in the block)
	int gOff (int g) { return g < 32 ? g * 4 : g == G_CR ? oCr : g == G_XER ? oXer : g == G_LR ? oLr : oCtr; }
	static bool calleeSaved (int h) { return h >= 19; }
	void cacheReset ()
	{
		for (int g = 0; g < NG; g++) { gHost[g] = -1; gDirty[g] = false; }
		for (int h = 0; h < 32; h++) { hGuest[h] = -1; hStamp[h] = 0; }
		stamp = 0; pinned = 0;
		for (int r = 0; r < 32; r++) { fHost[r] = -1; fDirty[r] = false; pHost[r] = -1; }
		for (int h = 0; h < 32; h++) { hfGuest[h] = -1; hfStamp[h] = 0; }
		fpinned = 0; fprfR = -1; bWrit = 0;
		sraMap ();
	}
	int alloc ()
	{
		static const int pool[8] = { 9, 10, 11, 12, 13, 14, 15, 18 };
		int best = -1; u32 bs = ~0u;
		for (int i = 0; i < 8; i++)
		{
			int h = pool[i];
			if (pinned & (1u << h)) continue;
			if (hGuest[h] < 0) { best = h; break; }
			if (hStamp[h] < bs) { bs = hStamp[h]; best = h; }
		}
		int g = hGuest[best];
		if (g >= P_BASE) { pHost[g - P_BASE] = -1; hGuest[best] = -1; }	// (a pointer: forgotten)
		else if (g >= 0)					// (the least recently used goes)
		{
			if (gDirty[g]) a.ldst (STR_W, 2, best, XM, gOff (g));
			gHost[g] = -1; gDirty[g] = false; hGuest[best] = -1;
		}
		return best;
	}
	void ptrDrop (int g)						// (GPR g written: its pointer is no more)
	{
		if (g >= 32) return;
		bWrit |= 1u << g;
		int h = pHost[g];
		if (h >= 0) { pHost[g] = -1; hGuest[h] = -1; }
	}
	int G (int g)							// guest g's value (read)
	{
		int h = gHost[g];
		if (h < 0) { h = alloc (); a.ldst (LDR_W, 2, h, XM, gOff (g)); gHost[g] = h; hGuest[h] = g; gDirty[g] = false; }
		hStamp[h] = ++stamp; pinned |= 1u << h;
		return h;
	}
	int W (int g)							// guest g's register, to write whole
	{
		ptrDrop (g);
		int h = gHost[g];
		if (h < 0) { h = alloc (); gHost[g] = h; hGuest[h] = g; }
		gDirty[g] = true; hStamp[h] = ++stamp; pinned |= 1u << h;
		return h;
	}
	int RW (int g) { int h = G (g); gDirty[g] = true; ptrDrop (g); return h; }	// read, then modified
	int Wlater (int g)						// guest g's register, written whole a little later: not
	{								// loaded, not dirty yet (the cold paths before the write
		ptrDrop (g);						// see the old value where it is: memory, or this register
		int h = gHost[g];					// dirty)
		if (h < 0) { h = alloc (); gHost[g] = h; hGuest[h] = g; gDirty[g] = false; }
		hStamp[h] = ++stamp; pinned |= 1u << h;
		return h;
	}
	// the dirty ones to memory (the state stays): all (+ the pending FPRF: before the exits, the
	// interpreter) or those a call clobbers (the caller-saved GPRs, the FPRs)
	void spill (bool all)
	{
		for (int g = 0; g < NG; g++)
			if (gHost[g] >= 0 && gDirty[g] && !isSra (g) && (all || !calleeSaved (gHost[g]))) a.ldst (STR_W, 2, gHost[g], XM, gOff (g));
		for (int r = 0; r < 32; r++)
			if (fHost[r] >= 0 && fDirty[r]) a.ldst (STR_Q, 4, fHost[r], XM, fOff (r));
		if (all) fprfFlush ();
	}
	void reload (bool all)
	{
		for (int g = 0; g < NG; g++)
			if (gHost[g] >= 0 && !isSra (g) && (all || !calleeSaved (gHost[g]))) a.ldst (LDR_W, 2, gHost[g], XM, gOff (g));
		for (int r = 0; r < 32; r++)
			if (fHost[r] >= 0) a.ldst (LDR_Q, 4, fHost[r], XM, fOff (r));
		for (int g = 0; g < 32; g++)				// (the pointers: from their GPRs again)
		{
			int h = pHost[g];
			if (h < 0 || (!all && calleeSaved (h))) continue;
			int v = gHost[g];
			if (v < 0) { a.ldst (LDR_W, 2, 5, XM, gOff (g)); v = 5; }
			a.logi (EOR_I, 5, v, 0x80000000u);
			a.put (0x8B204000u | 5u << 16 | (u32) XMEM << 5 | (u32) h);	// ADD xh, x20, w5, UXTW
		}
	}
	void dropAll ()
	{
		for (int g = 0; g < NG; g++) { gHost[g] = -1; gDirty[g] = false; }
		for (int g = 0; g < 32; g++) pHost[g] = -1;
		bWrit = ~0u;						// (an instruction interpreted: any GPR may have changed)
		for (int h = 0; h < 32; h++) hGuest[h] = -1;
		sraMap ();
		for (int r = 0; r < 32; r++) { fHost[r] = -1; fDirty[r] = false; }
		for (int h = 0; h < 32; h++) hfGuest[h] = -1;
		fprfR = -1;
	}

	// ---- the FPRs in NEON registers (a cache for the block too): a q register holds both
	// halves (ps0 = lane 0, ps1 = lane 1); the pool q8..q31, all treated as caller-saved (a helper
	// may use their upper halves), v0..v7 the temporaries. FPRF is set lazily: fprfR's half
	// fprfLane is its source, written to fprfVal (+ fprfPending) before the FPR changes otherwise,
	// at the exits, before the interpreter.
	int fHost[32]; bool fDirty[32]; int hfGuest[32]; u32 hfStamp[32]; u32 fpinned;
	int fprfR, fprfLane;
	int fOff (int r) { return oPs + 16 * r; }
	int falloc ()
	{
		int best = -1; u32 bs = ~0u;
		for (int h = 8; h < 32; h++)
		{
			if (fpinned & (1u << h)) continue;
			if (hfGuest[h] < 0) { best = h; break; }
			if (hfStamp[h] < bs) { bs = hfStamp[h]; best = h; }
		}
		int g = hfGuest[best];
		if (g >= 0)
		{
			if (fDirty[g]) a.ldst (STR_Q, 4, best, XM, fOff (g));
			fHost[g] = -1; fDirty[g] = false; hfGuest[best] = -1;
		}
		return best;
	}
	// FPR r's register (loaded or not): setsFprf false = it is to be changed otherwise than by an
	// instruction that sets FPRF (a pending FPRF from it is written first). Mark it dirty (fSet)
	// once written: a side path taken before that sees it clean.
	int FRes (int r, bool load, bool setsFprf)
	{
		if (!setsFprf && fprfR == r) { fprfFlush (); fprfR = -1; }
		int h = fHost[r];
		if (h < 0)
		{
			h = falloc (); fHost[r] = h; hfGuest[h] = r; fDirty[r] = false;
			if (load) a.ldst (LDR_Q, 4, h, XM, fOff (r));
		}
		hfStamp[h] = ++stamp; fpinned |= 1u << h;
		return h;
	}
	int FG (int r) { return FRes (r, true, true); }
	// ---- the cold paths: emitted after the block's main code (the slow memory accesses, a NaN,
	// the conversions' rare cases, the interpreter leaving...), each with the cache's state and
	// the cycles as at its branch; they come back to the main code (ret) or leave the block
	struct St { int gH[NG], fH[32], pH[32]; bool gD[NG], fD[32]; int fprfR, fprfLane; u32 synced; };
	enum { D_MEM, D_INTERP, D_IEXIT, D_CVTD, D_CVTS, D_FPRF, D_STUB, D_TOPC, D_RETMISS, D_SPRESET };
	struct Def { int kind; bool store, back; int size, dm, r1, r2; u32 op, pc, idx; u32 *site, *ret, *ret2; St st; };
	enum { MAX_DEFS = 400 };
	Def defs[MAX_DEFS]; int nDefs;
	void saveSt (St &t)
	{
		for (int i = 0; i < NG; i++) { t.gH[i] = gHost[i]; t.gD[i] = gDirty[i]; }
		for (int i = 0; i < 32; i++) { t.fH[i] = fHost[i]; t.fD[i] = fDirty[i]; t.pH[i] = pHost[i]; }
		t.fprfR = fprfR; t.fprfLane = fprfLane; t.synced = synced;
	}
	void loadSt (const St &t)
	{
		for (int i = 0; i < NG; i++) { gHost[i] = t.gH[i]; gDirty[i] = t.gD[i]; }
		for (int i = 0; i < 32; i++) { fHost[i] = t.fH[i]; fDirty[i] = t.fD[i]; pHost[i] = t.pH[i]; }
		fprfR = t.fprfR; fprfLane = t.fprfLane; synced = t.synced;
	}
	Def &defer (int kind)				// (then its branch at d.site, and d.ret)
	{
		Def &d = defs[nDefs++];
		d.kind = kind; d.site = d.ret = d.ret2 = 0; d.back = true;
		saveSt (d.st);
		return d;
	}
	bool defFull () { return nDefs > MAX_DEFS - 8; }
	void emitDefs ();
	void emitDefsHere ();
	void fSet (int r) { fDirty[r] = true; }
	void fprfSet (int r, int lane) { fprfR = r; fprfLane = lane; }
	void fprfFlush ()
	{
		if (fprfR < 0) return;
		int h = fHost[fprfR];
		if (h >= 0 && !fprfLane) a.ldst (STR_D, 3, h, XM, oFprfVal);
		else
		{
			if (h >= 0) a.ins (7, 0, h, 1);
			else a.ldst (LDR_D, 3, 7, XM, fOff (fprfR) + 8 * fprfLane);
			a.ldst (STR_D, 3, 7, XM, oFprfVal);
		}
		a.movz (5, 1, 0); a.ldst (STRB, 0, 5, XM, oFprfPend);
	}
	void ldG (int t, int r) { a.mov (t, G (r)); }
	void stG (int t, int r) { a.mov (W (r), t); }
	void ldF (int w, int off) { a.ldst (LDR_W, 2, w, XM, off); }	// (the fields not cached: MSR, FPSCR, GQRs, pc)
	void stF (int w, int off) { a.ldst (STR_W, 2, w, XM, off); }

	// ---- emitting ----
	void addConst (int rd, int rn, u32 c)
	{
		if (c == 0) { if (rd != rn) a.mov (rd, rn); }
		else if (c < 4096) a.imm (ADD_WI, rd, rn, c);
		else if (0u - c < 4096) a.imm (SUB_WI, rd, rn, 0u - c);
		else if (!(c & 0xFFF) && c < 0x1000000) a.imm (ADD_WI, rd, rn, c >> 12, 1);
		else { a.movw (4, c); a.alu (ADD_W, rd, rn, 4); }
	}
	// The cycles: x26 counts down to jitEnd (= jitUntil when set): the cycles run are jitEnd - x26,
	// written to m->cycles when leaving (exitStub) and before a helper that reads them.
	void dc (u32 n) { if (n) a.imm (SUB_WI | X64, XDC, XDC, n); }		// n cycles run
	void uc (u32 n) { if (n) a.imm (ADD_WI | X64, XDC, XDC, n); }
	void sync (u32 total) { if (total > synced) { dc (total - synced); synced = total; } }
	void cyclesOut () { a.ldst (LDR_X, 3, 5, XM, oEnd); a.alu (SUB_W | X64, 5, 5, XDC); a.ldst (STR_X, 3, 5, XM, oCycles); }
	void resync ()							// (after a helper: jitUntil may have changed)
	{
		a.ldst (LDR_X, 3, 5, XM, oEnd); a.ldst (LDR_X, 3, 6, XM, oUntil);
		a.alu (SUB_W | X64, 5, 5, 6); a.alu (SUB_W | X64, XDC, XDC, 5);
		a.ldst (STR_X, 3, 6, XM, oEnd);
	}
	void helper (int h) { a.ldst (LDR_X, 3, 16, XCTX, h * 8); a.blr (16); }
	void callKeep (int h) { spill (false); helper (h); reload (false); }	// a helper that leaves the guest alone
	// leave the block: pc = w0; the registers written back; its cycles; to the next block
	// (dispatcher) or to jitRun
	void exitReg (u32 total, bool toC)
	{
		spill (true);
		stF (0, oPc);
		if (total > synced) dc (total - synced);
		if (toC) { a.b (exitStub); return; }
		a.movw (2, bKey & 3);
		a.b (dispatch);
	}
	// leave to a known address: while cycles < jitUntil straight to its block (a link), else to jitRun
	void exitTo (u32 target, u32 total, bool toC)
	{
		if (toC) { a.movw (0, target); exitReg (total, true); return; }
		spill (true);
		a.imm (SUBS_WI | X64, XDC, XDC, total > synced ? total - synced : 0);
		u32 *late = a.p; a.bcond (LE, a.p);
		u32 key = target | (bKey & 3);
		u32 *site = a.p; a.b (a.p);
		void *to = lookup (key);
		if (to) Asm::patch (site, (u32 *) to);
		Def &st = defer (D_STUB);				// (its stub -- pc = target, out -- with the cold paths)
		st.site = late; st.ret = site; st.op = target; st.r1 = (int) nLinks; st.back = to != 0;
		addLink (key, site, site);
	}

	// ---- calls and returns on the host's bl / ret (the header): the host's stack holds pairs {the
	// landing after the call's bl, the guest's return address}, above a sentinel {0, 1} at jitSpBase
	void spEmpty ()							// (the stack down to its sentinel -- written again:
	{								// an interrupt of core 0 may have used it)
		a.ldst (LDR_X, 3, 3, XM, oSpBase); a.imm (ADD_WI | X64, SP, 3, 0);
		a.movz (4, 1, 0); a.stpSp (WZR, 4, 0);
	}
	// the pair pushed (x1 = the landing: 3 instructions on), after the room checked
	void callPush ()
	{
		a.ldst (LDR_X, 3, 0, XM, oSpLimit); a.cmpSp (0);
		Def &rs = defer (D_SPRESET); rs.site = a.p; a.bcond (LO, a.p); rs.ret = a.p;
		a.adr (1, a.p + 3);
		a.stpPre (1, SRA_H[0], -16);				// (x24: LR, = the return address)
	}
	// bl target: as exitTo, the target's block called; the landing goes on to the instruction after
	void callTo (u32 target, u32 ret, u32 total)
	{
		spill (true);
		a.imm (SUBS_WI | X64, XDC, XDC, total > synced ? total - synced : 0);
		synced = total;
		u32 *late = a.p; a.bcond (LE, a.p);
		callPush ();
		u32 key = target | (bKey & 3);
		u32 *site = a.p; a.bl (a.p);
		void *to = lookup (key);
		if (to) Asm::patch (site, (u32 *) to);
		Def &st = defer (D_STUB);
		st.site = late; st.ret = site; st.op = target; st.r1 = (int) nLinks; st.back = to != 0;
		addLink (key, site, site);
		dropAll ();						// (the landing: the host registers the callee's, its
		exitTo (ret, total, false);				// cycles counted -- on to the instruction after)
	}
	// bcctrl (w6 = the target): its block from the dispatcher's table, called with blr; not there:
	// out to jitRun (no pair: its return goes through the dispatcher)
	void callReg (u32 ret, u32 total)
	{
		spill (true);
		a.imm (SUBS_WI | X64, XDC, XDC, total > synced ? total - synced : 0);
		synced = total;
		Def &lt = defer (D_TOPC); lt.r1 = 6; lt.site = a.p; a.bcond (LE, a.p);
		a.movw (2, bKey & 3); a.alu (ORR_W, 4, 6, 2);
		a.ubfx (5, 6, 2, FAST_BITS);
		a.alu (ADD_W | X64, 5, XCTX, 5, 0, 4);
		a.ldst (LDR_W, 2, 7, 5, FAST_OFF);
		a.cmp (7, 4);
		Def &ms = defer (D_TOPC); ms.r1 = 6; ms.site = a.p; a.bcond (NE, a.p);
		a.ldst (LDR_X, 3, 7, 5, FAST_OFF + 8);
		callPush ();
		a.blr (7);
		dropAll ();						// (the landing, as callTo's)
		exitTo (ret, total, false);
	}
	// blr / bclr (w6 = the target, LR & ~3): the top pair's -- ret; else the stack emptied, the dispatcher
	void retTo (u32 total)
	{
		spill (true);
		a.imm (SUBS_WI | X64, XDC, XDC, total > synced ? total - synced : 0);
		Def &lt = defer (D_TOPC); lt.r1 = 6; lt.site = a.p; a.bcond (LE, a.p);
		a.ldpPost (1, 2, 16);
		a.cmp (2, 6);
		Def &ms = defer (D_RETMISS); ms.r1 = 6; ms.site = a.p; a.bcond (NE, a.p);
		a.retX (1);
	}

	// CR field crf from the flags of a compare (signed / unsigned) + XER's SO
	void crFromFlags (int crf, bool sgn)
	{
		a.movz (3, 4, 0); a.movz (4, 8, 0);
		a.csel (CSEL_W, 3, 4, 3, sgn ? LT : LO);
		a.movz (4, 2, 0);
		a.csel (CSEL_W, 3, 4, 3, EQ);
		a.alu (ORR_W, 3, 3, G (G_XER), 1, 31);
		a.bfi (RW (G_CR), 3, 28 - crf * 4, 4);
	}
	void setCr0 (int w) { a.cmpi (w, 0); crSet (0, true); }
	// a compare's field (the flags set): into CR now, or -- the next instruction a conditional branch
	// on its LT / GT / EQ -- left in the flags for that branch (bcTests), made there only on the paths
	// that may read it before it is set again (crDead)
	void crSet (int crf, bool sgn)
	{
		u32 npa = bPa + (curIdx + 1) * 4;
		if (curIdx + 1 < MAX_INSNS && (npa & 0xFFF) && npa + 4 <= MEM1_SIZE && !defFull ())
		{
			u32 nop = bswap32 (*(const u32 *) (m->mem1 + npa));
			u32 bo = (nop >> 21) & 31, bi = (nop >> 16) & 31, x = (nop >> 1) & 0x3FF;
			bool isBc = (nop >> 26) == 16 && !(nop & 2), isBr = (nop >> 26) == 19 && (x == 16 || x == 528);
			if ((isBc || isBr) && !(bo & 16) && (int) (bi >> 2) == crf && (bi & 3) != 3)
			{
				pend.on = true; pend.crf = crf; pend.sgn = sgn;
				pend.deadFall = crDead (npa + 4, crf, 3);
				pend.deadTaken = isBc && !(nop & 1) && crDead (npa + (u32) (s32) (s16) (nop & 0xFFFC), crf, 3);
				return;
			}
		}
		crFromFlags (crf, sgn);
	}
	void crPendFlush () { if (pend.on) { pend.on = false; crFromFlags (pend.crf, pend.sgn); } }
	void setCaReg (int w) { a.bfi (RW (G_XER), w, 29, 1); }			// XER.CA = w's bit 0
	void setCaFlag () { a.cset (3, HS); setCaReg (3); }				// XER.CA = the carry
	void caToFlag () { a.ubfx (3, G (G_XER), 29, 1); a.cmpi (3, 1); }		// carry = XER.CA

	// the interpreter for one instruction (toC: then back to jitRun whatever it did)
	void interp (u32 op, u32 pc, u32 idx, bool toC)
	{
		gqrOk = 0;						// (it may be an mtspr)
		sync (idx * 2);
		spill (true); sraStore (); dropAll ();
		sgl0 = sgl1 = 0;
		cyclesOut ();
		a.movX (0, XM); a.movw (1, op); a.movw (2, pc);
		helper (H_INTERP);
		sraLoad ();
		resync ();
		Def &df = defer (D_IEXIT);
		df.site = a.p; a.cbz (0, a.p, true);			// (it left: an exception, a branch)
		if (toC) exitTo (pc + 4, (idx + 1) * 2, true);
	}

	// a load (w1 = the address -> w / x reg) or a store (w1 = the address, w / x reg = the value); size 1 / 2 / 4 / 8.
	// MEM1 read / written here; the rest (the hardware, the pipe, a DSI) on the cold path.
	// (fpv >= 0: the value in V fpv -- a single in lane 0 (size 4), two in lanes 0, 1 (size 8), in
	// the host's byte order -- instead of reg.) A load's reg may be a guest register's: kept as it
	// was until the access (the cold path saves it as it is), then the value.
	void memop (bool store, int size, u32 pc, u32 idx, int fpv = -1, int reg = -1)
	{
		if (reg < 0) reg = store ? 2 : 0;
		Def &df = defer (D_MEM);
		df.store = store; df.size = size; df.pc = pc; df.idx = idx; df.dm = dmode; df.r1 = fpv; df.r2 = reg;
		int ra = 1;
		if (dmode == 0) { a.cmp (1, WMSZ); df.site = a.p; a.bcond (HS, a.p); }
		else if (dmode == 1)					// (0x80000000 + MEM1; 0xC0000000: the cold path first)
		{
			a.logi (EOR_I, 3, 1, 0x80000000u);
			a.cmp (3, WMSZ); df.site = a.p; a.bcond (HS, a.p);
			ra = 3;
		}
		else { df.site = a.p; a.b (a.p); }
		if (dmode != 2) access (store, size, fpv, reg, ra);
		df.ret = a.p;
	}
	// MEM1 at [x20, w ra]: loaded into / stored from reg (or V fpv)
	void access (bool store, int size, int fpv, int reg, int ra)
	{
		if (fpv >= 0)
		{
			if (!store) { a.ldstr (size == 8 ? LDR_DR : LDR_SR, fpv, XMEM, ra); a.fp1 (REV32_8B, fpv, fpv); }
			else { a.fp1 (REV32_8B, 7, fpv); a.ldstr (size == 8 ? STR_DR : STR_SR, 7, XMEM, ra); }
		}
		else if (!store)
		{
			if (size == 8) { a.ldstr (LDR_XR, reg, XMEM, ra); a.un (REV_X, reg, reg); }
			else if (size == 4) { a.ldstr (LDR_WR, reg, XMEM, ra); a.un (REV_W, reg, reg); }
			else if (size == 2) { a.ldstr (LDRH_R, reg, XMEM, ra); a.un (REV16_W, reg, reg); }
			else a.ldstr (LDRB_R, reg, XMEM, ra);
		}
		else
		{
			if (size == 8) { a.un (REV_X, 4, reg); a.ldstr (STR_XR, 4, XMEM, ra); }
			else if (size == 4) { a.un (REV_W, 4, reg); a.ldstr (STR_WR, 4, XMEM, ra); }
			else if (size == 2) { a.un (REV16_W, 4, reg); a.ldstr (STRH_R, 4, XMEM, ra); }
			else a.ldstr (STRB_R, reg, XMEM, ra);
		}
	}
	// Guest ra's pointer into MEM1 (x20 + its offset in the 0x80000000 mirror) for a block's D-form
	// accesses through it, their displacement in the load / store itself: made at the first one when
	// the translation finds ra in MEM1 and not yet written in the block, checked there (not MEM1's
	// then: that instruction by the interpreter, and out of the block); kept while ra is not written.
	// -1: the plain path (ra 0, not MEM1's at translation, written earlier in the block).
	int basePtr (int ra, u32 op, u32 pc, u32 idx)
	{
		if (dmode != 1 || ra == 0) return -1;
		int h = pHost[ra];
		if (h >= 0) { hStamp[h] = ++stamp; pinned |= 1u << h; return h; }
		if (bWrit & (1u << ra)) return -1;
		if ((m->gpr[ra] ^ 0x80000000u) >= (u32) MEM1_SIZE) return -1;	// (the hardware, the uncached mirror...)
		int g = G (ra);
		a.logi (EOR_I, 3, g, 0x80000000u);
		a.cmp (3, WMSZ);
		Def &d = interpSide (op, pc, idx, false); d.site = a.p; a.bcond (HS, a.p);
		h = alloc ();
		a.put (0x8B204000u | 3u << 16 | (u32) XMEM << 5 | (u32) h);		// ADD xh, x20, w3, UXTW
		pHost[ra] = h; hGuest[h] = P_BASE + ra; hStamp[h] = ++stamp; pinned |= 1u << h;
		return h;
	}
	// [xhp + disp] (disp -32768..32767): loaded into / stored from reg (or V fpv), as access () does
	void ptrAccess (bool store, int size, int fpv, int reg, int hp, s32 disp)
	{
		u32 opc;							// (LDUR / STUR; + 0x01000000: the scaled offset form)
		if (fpv >= 0) opc = size == 8 ? (store ? 0xFC000000u : 0xFC400000u) : (store ? 0xBC000000u : 0xBC400000u);
		else if (size == 8) opc = store ? 0xF8000000u : 0xF8400000u;
		else if (size == 4) opc = store ? 0xB8000000u : 0xB8400000u;
		else if (size == 2) opc = store ? 0x78000000u : 0x78400000u;
		else opc = store ? 0x38000000u : 0x38400000u;
		int rt = fpv >= 0 ? (store ? 7 : fpv) : (store && size > 1 ? 4 : reg);
		if (store)							// (the value byte-swapped first)
		{
			if (fpv >= 0) a.fp1 (REV32_8B, 7, fpv);
			else if (size == 8) a.un (REV_X, 4, reg);
			else if (size == 4) a.un (REV_W, 4, reg);
			else if (size == 2) a.un (REV16_W, 4, reg);
		}
		int base = hp;
		if (disp >= 0 && !(disp & (size - 1)) && disp / size < 4096) a.put (opc | 0x01000000u | (u32) (disp / size) << 10 | (u32) base << 5 | (u32) rt);
		else if (disp >= -256 && disp < 256) a.put (opc | ((u32) disp & 0x1FF) << 12 | (u32) base << 5 | (u32) rt);
		else { a.movx (17, (u64) (s64) disp); a.alu (ADD_W | X64, 17, hp, 17); a.put (opc | 0x01000000u | 17u << 5 | (u32) rt); }
		if (!store)
		{
			if (fpv >= 0) a.fp1 (REV32_8B, fpv, fpv);
			else if (size == 8) a.un (REV_X, reg, reg);
			else if (size == 4) a.un (REV_W, reg, reg);
			else if (size == 2) a.un (REV16_W, reg, reg);
		}
	}
	void memCold (const Def &d)
	{
		bool store = d.store; int size = d.size, fpv = d.r1, reg = d.r2;
		if (d.dm == 1)						// the uncached mirror (0xC0000000 + MEM1): as the fast path
		{
			a.logi (EOR_I, 3, 1, 0xC0000000u);
			a.cmp (3, WMSZ);
			u32 *other = a.p; a.bcond (HS, a.p);
			access (store, size, fpv, reg, 3);
			a.b (d.ret);
			Asm::patch (other, a.p);
		}
		if (store && fpv >= 0)					// (a float's bits in w2 / a pair's in x2, as an integer store has them)
		{
			if (size == 8) { a.fp1 (FMOV_XD, 2, fpv); a.rorX (2, 2, 32); }
			else a.fp1 (FMOV_WS, 2, fpv);
		}
		else if (store && reg != 2) a.alu (ORR_W | (size == 8 ? X64 : 0u), 2, WZR, reg);	// (the value in w2 / x2)
		if (store && d.dm != 2)					// the write-gather pipe (the GX FIFO): appended here
		{
			a.movw (5, d.dm == 1 ? 0xCC008000u : 0x0C008000u);
			a.cmp (1, 5);
			u32 *notPipe = a.p; a.bcond (NE, a.p);
			ldF (5, oGatherN);
			a.imm (ADD_WI | X64, 6, XM, (u32) oGather);
			if (size == 8) { a.un (REV_X, 4, 2); a.ldstr (0xF8204800u, 4, 6, 5); }	// (STR x4, [x6, w5, UXTW])
			else if (size == 4) { a.un (REV_W, 4, 2); a.ldstr (0xB8204800u, 4, 6, 5); }
			else if (size == 2) { a.un (REV16_W, 4, 2); a.ldstr (0x78204800u, 4, 6, 5); }
			else a.ldstr (0x38204800u, 2, 6, 5);
			a.imm (ADD_WI, 5, 5, (u32) size); stF (5, oGatherN);
			a.cmpi (5, 32);
			a.bcond (LO, d.ret);
			a.movX (0, XM); callKeep (H_GATHER);		// (a burst: the FIFO's commands run)
			resync ();
			a.b (d.ret);
			Asm::patch (notPipe, a.p);
		}
		u32 pend = d.idx * 2 - synced;
		dc (pend);
		spill (true);						// (an exception leaves from here)
		cyclesOut ();
		a.movX (0, XM);
		a.movw (store ? 3 : 2, d.pc);
		static const int hs[2][9] = { { 0, H_RD8, H_RD16, 0, H_RD32, 0, 0, 0, H_RD64 }, { 0, H_WR8, H_WR16, 0, H_WR32, 0, 0, 0, H_WR64 } };
		helper (hs[store][size]);
		resync ();
		u32 *ok = a.p; a.tbz (0, 32, a.p);
		dc (2);
		a.b (exitStub);
		Asm::patch (ok, a.p);
		uc (pend);
		reload (false);
		if (!store && size == 8) a.ldst (LDR_X, 3, 0, XM, oScratch);
		if (!store && fpv >= 0)					// (into V fpv, as the fast path leaves it)
		{
			if (size == 8) { a.rorX (0, 0, 32); a.fp1 (FMOV_DX, fpv, 0); }
			else a.fp1 (FMOV_SW, fpv, 0);
		}
		else if (!store && reg != 0) a.alu (ORR_W | (size == 8 ? X64 : 0u), reg, WZR, 0);	// (after the reload: the value)
		a.b (d.ret);
	}
	// the interpreter for this instruction on a cold path (a NaN, the FPU off, a GQR type): then
	// back to d.ret (with every register reloaded) or out
	Def &interpSide (u32 op, u32 pc, u32 idx, bool back)
	{
		Def &d = defer (D_INTERP);
		d.op = op; d.pc = pc; d.idx = idx; d.back = back;
		return d;
	}
	void interpCold (const Def &d)
	{
		u32 pend = d.idx * 2 - synced;
		dc (pend);
		spill (true); sraStore ();
		cyclesOut ();
		a.movX (0, XM); a.movw (1, d.op); a.movw (2, d.pc);
		helper (H_INTERP);
		sraLoad ();
		resync ();
		u32 *j = a.p; a.cbz (0, a.p);
		dc (2);
		a.b (exitStub);
		Asm::patch (j, a.p);
		if (!d.back) { a.movw (0, d.pc + 4); stF (0, oPc); dc (2); a.b (exitStub); return; }
		uc (pend);
		reload (true);						// (it may have written any of them)
		a.b (d.ret);
	}
	// d dreg a NaN -> this instruction again by the interpreter (a signalling NaN kept exactly), then
	// on at the instruction's end (its index in defs: its ret set there)
	int nanSide (int dreg, u32 op, u32 pc, u32 idx)
	{
		a.fp3 (FCMP_D, 0, dreg, dreg);
		int k = nDefs;
		Def &nd = interpSide (op, pc, idx, true); nd.site = a.p; a.bcond (VS, a.p);
		return k;
	}
	// the FPU's use is checked once a block (MSR.FP: else the interpreter takes the exception)
	void fpCheck (u32 op, u32 pc, u32 idx)
	{
		if (fpOk) return;
		fpOk = true;
		ldF (5, oMsr); a.ubfx (5, 5, 13, 1);
		Def &d = interpSide (op, pc, idx, false);
		d.site = a.p; a.cbz (5, a.p);
	}
	void roundS (int dd) { a.fp1 (FCVT_SD, dd, dd); a.fp1 (FCVT_DS, dd, dd); }
	void roundS2 (int vd) { a.fp1 (FCVTN_2S, vd, vd); a.fp1 (FCVTL_2D, vd, vd); }	// (both halves)
	// the multiplicand of a single-precision multiply: its mantissa rounded to 25 bits (lane `lane`
	// of vs -> x5)
	void force25x (int vs, int lane)
	{
		a.umovX (5, vs, lane);
		a.logi (AND_I, 6, 5, 0x8000000ull, true);
		a.logi (AND_I, 5, 5, 0xFFFFFFFFF8000000ull, true);
		a.alu (ADD_W | X64, 5, 5, 6);
	}
	// a single's bits (w ws) -> the double the 750 loads (d dd): FCVT; inf / a signalling NaN: cvtToDouble
	void cvtD (int dd, int ws)
	{
		a.ubfx (5, ws, 22, 9); a.cmpi (5, 0x1FE);
		Def &d = defer (D_CVTD); d.r1 = dd; d.r2 = ws;
		d.site = a.p; a.bcond (EQ, a.p);
		a.fp1 (FMOV_SW, dd, ws); a.fp1 (FCVT_DS, dd, dd);
		d.ret = a.p;
	}
	// a double's bits (x xs) -> the single the 750 stores (w wd): the bits taken as they are; the
	// denormal range: cvtToSingle
	void cvtS (int wd, int xs)
	{
		a.bfm (UBFM_X, 5, xs, 52, 62);
		a.cmpi (5, 896);
		Def &d = defer (D_CVTS); d.r1 = wd; d.r2 = xs;
		d.site = a.p; a.bcond (LS, a.p);
		d.ret2 = a.p;
		a.bfm (UBFM_X, 6, xs, 32, 63);
		a.logi (AND_I, 6, 6, 0xC0000000u);
		a.bfm (UBFM_X, 7, xs, 29, 58);
		a.alu (ORR_W, wd, 6, 7);
		d.ret = a.p;
	}
	bool fpInsn (u32 op, u32 pc, u32 idx);
	bool fpLoadStore (u32 op, u32 pc, u32 idx, int kind, bool x, bool upd);
	bool psq (u32 op, u32 pc, u32 idx, bool load, bool upd, bool x = false);
	void eaPsq (int hbase, int hidx, u32 off);
	// the effective address into w1: (rA|0) + d, or (rA|0) + rB
	void eaD (u32 op, bool upd)
	{
		int ra = (int) ((op >> 16) & 31); u32 d = (u32) (s32) (s16) op;
		if (ra == 0 && !upd) { a.movw (1, d); return; }
		addConst (1, G (ra), d);
	}
	void eaX (u32 op, bool upd)
	{
		int ra = (int) ((op >> 16) & 31), rb = (int) ((op >> 11) & 31);
		int b = G (rb);
		if (ra == 0 && !upd) { a.mov (1, b); return; }
		a.alu (ADD_W, 1, G (ra), b);
	}
	// a load / store: size, sign-extended (lha), with update (rA = the address)
	void load (u32 op, bool x, int size, bool sext, bool upd, u32 pc, u32 idx)
	{
		int hp = x ? -1 : basePtr ((int) ((op >> 16) & 31), op, pc, idx);
		if (hp >= 0)							// (through the base's pointer)
		{
			if (upd) { eaD (op, upd); a.mov (WEA, 1); }
			int rd = (int) ((op >> 21) & 31);
			int d = Wlater (rd);
			ptrAccess (false, size, -1, d, hp, (s32) (s16) op);
			gDirty[rd] = true;
			if (sext) a.bfm (SBFM_W, d, d, 0, 15);
			if (upd) stG (WEA, (int) ((op >> 16) & 31));
			return;
		}
		if (x) eaX (op, upd); else eaD (op, upd);
		if (upd) a.mov (WEA, 1);
		int rd = (int) ((op >> 21) & 31);
		int d = Wlater (rd);					// (its register: loaded into; dirty once it is)
		memop (false, size, pc, idx, -1, d);
		gDirty[rd] = true;
		if (sext) a.bfm (SBFM_W, d, d, 0, 15);
		if (upd) stG (WEA, (int) ((op >> 16) & 31));
	}
	void store (u32 op, bool x, int size, bool upd, u32 pc, u32 idx)
	{
		int hp = x ? -1 : basePtr ((int) ((op >> 16) & 31), op, pc, idx);
		if (hp >= 0)							// (through the base's pointer)
		{
			if (upd) { eaD (op, upd); a.mov (WEA, 1); }
			ptrAccess (true, size, -1, G ((int) ((op >> 21) & 31)), hp, (s32) (s16) op);
			if (upd) stG (WEA, (int) ((op >> 16) & 31));
			return;
		}
		if (x) eaX (op, upd); else eaD (op, upd);
		if (upd) a.mov (WEA, 1);
		memop (true, size, pc, idx, -1, G ((int) ((op >> 21) & 31)));
		if (upd) stG (WEA, (int) ((op >> 16) & 31));
	}
	// rd = rn & mask(mb, me)
	void andMask (int rd, int rn, int mb, int me)
	{
		u32 mk = mask (mb, me);
		if (mk == 0xFFFFFFFFu) { if (rd != rn) a.mov (rd, rn); return; }
		if (mb <= me && me == 31) { a.ubfx (rd, rn, 0, 32 - mb); return; }
		a.logi (AND_I, rd, rn, mk);
	}
	// the branch's conditions (BO, BI): a jump to the "not taken" place for each, 0..2
	int bcTests (u32 bo, u32 bi, bool ctrToo, u32 **nt)
	{
		int n = 0;
		bool fl = pend.on && !(bo & 16) && (int) (bi >> 2) == pend.crf;	// (the compare right before: its flags)
		int crh = !(bo & 16) && !fl ? G (G_CR) : -1;		// (all the registers taken before a branch)
		if (fl && (!pend.deadFall || !pend.deadTaken)) { G (G_CR); G (G_XER); }	// (a path makes the field)
		if (ctrToo && !(bo & 4))
		{
			int c = RW (G_CTR); a.imm (SUB_WI, c, c, 1);
			nt[n++] = a.p; a.cbz (c, a.p, (bo & 2) != 0);	// (bo & 2: branch if ctr == 0)
		}
		if (crh >= 0) { nt[n++] = a.p; a.tbz (crh, (int) (31 - bi), a.p, !(bo & 8)); }	// (bo & 8: branch if the bit is set)
		else if (fl)
		{
			int c = (bi & 3) == 0 ? (pend.sgn ? LT : LO) : (bi & 3) == 1 ? (pend.sgn ? GT : HI) : EQ;
			nt[n++] = a.p; a.bcond ((bo & 8) ? c ^ 1 : c, a.p);		// (not taken: the bit as not asked)
		}
		return n;
	}

	// w1 = an address -> x4 = [w1, w1 + len) in the host's MEM1, else (-> the branch) the cold path
	u32 *fastBlock (u32 len)
	{
		if (dmode == 2) { u32 *b = a.p; a.b (a.p); return b; }
		if (dmode == 0) a.mov (3, 1);
		else { a.logi (AND_I, 3, 1, 0xBFFFFFFFu); a.logi (EOR_I, 3, 3, 0x80000000u); }
		a.movw (4, MEM1_SIZE - len);
		a.cmp (3, 4);
		u32 *b = a.p; a.bcond (HI, a.p);
		a.put (0x8B204000u | 3u << 16 | (u32) XMEM << 5 | 4u);	// ADD x4, x20, w3, UXTW
		return b;
	}
	void multi (u32 op, u32 pc, u32 idx, bool load)		// lmw / stmw: MEM1 at once, else the interpreter
	{
		int d = (int) ((op >> 21) & 31), ra = (int) ((op >> 16) & 31);
		u32 disp = (u32) (s32) (s16) op;
		if (ra == 0) a.movw (1, disp); else addConst (1, G (ra), disp);
		if (load)						// (rD..r31 out of the cache: written in memory)
			for (int r = d; r < 32; r++)
			{
				ptrDrop (r);
				if (gHost[r] >= 0 && !isSra (r))
				{
					if (gDirty[r]) a.ldst (STR_W, 2, gHost[r], XM, r * 4);
					hGuest[gHost[r]] = -1; gHost[r] = -1; gDirty[r] = false;
				}
			}
		Def &cd = interpSide (op, pc, idx, true);
		cd.site = fastBlock ((u32) (32 - d) * 4);
		for (int r = d; r < 32; r++)
		{
			int off = (r - d) * 4;
			if (load && isSra (r)) { a.ldst (LDR_W, 2, 5, 4, off); a.un (REV_W, gHost[r], 5); }
			else if (load) { a.ldst (LDR_W, 2, 5, 4, off); a.un (REV_W, 5, 5); a.ldst (STR_W, 2, 5, XM, r * 4); }
			else
			{
				int src = gHost[r];
				if (src < 0) { a.ldst (LDR_W, 2, 5, XM, r * 4); src = 5; }
				a.un (REV_W, 6, src); a.ldst (STR_W, 2, 6, 4, off);
			}
		}
		cd.ret = a.p;
	}
	bool insn (u32 op, u32 pc, u32 idx);
	bool op31 (u32 op, u32 pc, u32 idx);
	bool idleLoop (u32 pa, u32 pc);
	void idleExit (u32 target, u32 total)		// the loop goes on: nothing changes until the next event
	{
		spill (true);
		a.movw (0, target); stF (0, oPc);
		if (total > synced) dc (total - synced);
		a.imm (SUBS_WI | X64, WZR, XDC, 0);
		a.csel (CSEL_W | X64, XDC, WZR, XDC, GT);			// (to the event: nothing left)
		a.movz (5, 1, 0); a.ldst (STRB, 0, 5, XM, oIdleHit);		// (jitRun: the GX's core caught up first)
		a.b (exitStub);
	}
};

// A polling loop: loads, compares, masks, then a conditional branch back to its start (no LK, no
// CTR), every register it reads either set earlier in the same pass or not set by it at all: each
// pass does the same until something outside changes memory -- an interrupt, the hardware, a DMA:
// the next event. Its taken branch skips the time to it.
bool Jit::idleLoop (u32 pa, u32 pc)
{
	u64 written = 0, before = 0;			// (bits: the GPRs; 32 = CR)
	u64 reads[8]; u64 writes[8]; int n = 0; bool found = false;
	for (; n < 8; n++)
	{
		if (pa + (u32) n * 4 + 4 > MEM1_SIZE) return false;
		u32 op = bswap32 (*(const u32 *) (m->mem1 + pa + (u32) n * 4));
		u32 p = op >> 26, d = (op >> 21) & 31, ra = (op >> 16) & 31, rb = (op >> 11) & 31, x = (op >> 1) & 0x3FF;
		u64 r = 0, w = 0;
		auto A0 = [&] () -> u64 { return ra ? 1ull << ra : 0; };
		if (p == 16)
		{
			u32 target = ((op & 2) ? 0 : pc + (u32) n * 4) + (u32) (s32) (s16) (op & 0xFFFC);
			if (target != pc || (op & 1) || !(d & 4) || n == 0) return false;
			reads[n] = (d & 16) ? 0 : 1ull << 32; writes[n] = 0; n++;
			found = true;
			break;
		}
		if (p == 32 || p == 34 || p == 40 || p == 42) { r = A0 (); w = 1ull << d; }		// lwz lbz lhz lha
		else if (p == 31 && (x == 23 || x == 87 || x == 279)) { r = A0 () | 1ull << rb; w = 1ull << d; }	// lwzx lbzx lhzx
		else if (p == 10 || p == 11) { r = 1ull << ra; w = 1ull << 32; }			// cmpli, cmpi
		else if (p == 31 && (x == 0 || x == 32)) { r = 1ull << ra | 1ull << rb; w = 1ull << 32; }	// cmp, cmpl
		else if (p == 21) { r = 1ull << d; w = 1ull << ra | ((op & 1) ? 1ull << 32 : 0); }	// rlwinm(.)
		else if (p == 28 || p == 29) { r = 1ull << d; w = 1ull << ra | 1ull << 32; }		// andi., andis.
		else if (p == 24 || p == 25) { r = 1ull << d; w = 1ull << ra; }			// ori, oris
		else return false;
		reads[n] = r; writes[n] = w;
		written |= w;
	}
	if (!found) return false;
	for (int i = 0; i < n; i++)
	{
		if (reads[i] & written & ~before) return false;		// (a value from the previous pass)
		before |= writes[i];
	}
	return true;
}

// Is CR field f set again before it is read, on every path from the instruction at pa (MEM1) on? The
// paths stay in the block's 4 KB page (the code of another page may change without this block being
// dropped): out of it, a call, a return, an exception, a CR logic op on the field -- read, or maybe.
// (A field not made is still in memory as it was: an interrupt, an exception between, saves and
// restores that value, which nothing reads.)
bool Jit::crDead (u32 pa, int f, int depth)
{
	u32 page = bPa & ~0xFFFu;
	for (int n = 0; n < 32; n++, pa += 4)
	{
		if ((pa & ~0xFFFu) != page || pa + 4 > MEM1_SIZE) return false;
		u32 op = bswap32 (*(const u32 *) (m->mem1 + pa));
		u32 p = op >> 26, d = (op >> 21) & 31, a = (op >> 16) & 31, b = (op >> 11) & 31, x = (op >> 1) & 0x3FF;
		bool rc = op & 1;
		switch (p)
		{
		case 10: case 11: if ((int) (d >> 2) == f) return true; continue;		// cmpli cmpi
		case 16:								// bc
		{
			if (!(d & 16) && (int) (a >> 2) == f) return false;
			if ((op & 3) || depth <= 0) return false;				// (bcl, absolute)
			return crDead (pa + 4, f, depth - 1) && crDead (pa + (u32) (s32) (s16) (op & 0xFFFC), f, depth - 1);
		}
		case 18:								// b
		{
			// a call: CR0 is volatile across it (the ABI) -- the callee may set it, the caller does
			// not read it after without setting it again: set, for CR0 (CR1 carries the varargs'
			// flag in, CR2-4 are kept: read)
			if ((op & 3) == 1) return f == 0;
			if ((op & 3) || depth <= 0) return false;				// (ba)
			u32 off = op & 0x03FFFFFC; if (off & 0x02000000) off |= 0xFC000000;
			return crDead (pa + off, f, depth - 1);
		}
		case 19:
			if (x == 0) { if ((int) (a >> 2) == f) return false; if ((int) (d >> 2) == f) return true; continue; }	// mcrf
			if (x == 150) continue;							// isync
			if (x == 257 || x == 129 || x == 289 || x == 225 || x == 33 || x == 449 || x == 417 || x == 193)	// the CR logic
			{
				if ((int) (a >> 2) == f || (int) (b >> 2) == f || (int) (d >> 2) == f) return false;
				continue;
			}
			return false;								// bclr bcctr rfi ...
		case 31:
			if (x == 19) return false;						// mfcr
			if (x == 144) { if (((op >> 12) & 0xFF) & (0x80u >> f)) return true; continue; }	// mtcrf
			if (x == 0 || x == 32 || x == 512) { if ((int) (d >> 2) == f) return true; continue; }	// cmp cmpl mcrxr
			if (rc && f == 0) return true;						// (a record form, stwcx.: CR0)
			continue;
		case 13: case 28: case 29: if (f == 0) return true; continue;			// addic. andi. andis.
		case 20: case 21: case 23: if (rc && f == 0) return true; continue;		// rlwimi. rlwinm. rlwnm.
		case 63:
			if (x == 0 || x == 32 || x == 64) { if ((int) (d >> 2) == f) return true; continue; }	// fcmpu fcmpo mcrfs
			if (rc && f == 1) return true;
			continue;
		case 59: if (rc && f == 1) return true; continue;
		case 4:
			if (x == 0 || x == 32 || x == 64 || x == 96) { if ((int) (d >> 2) == f) return true; continue; }	// ps_cmpu0 ...
			if (rc && f == 1) return true;
			continue;
		case 7: case 8: case 12: case 14: case 15: case 24: case 25: case 26: case 27:
			continue;
		default:
			if (p >= 32 && p <= 56) continue;						// (the loads and stores, psq_l)
			if (p == 60) continue;								// psq_st
			return false;								// (sc, reserved: unknown)
		}
	}
	return false;
}

// one instruction -> true: the block ends with it
bool Jit::insn (u32 op, u32 pc, u32 idx)
{
	curIdx = idx;
	int d = (int) ((op >> 21) & 31), ra = (int) ((op >> 16) & 31), rb = (int) ((op >> 11) & 31);
	u32 simm = (u32) (s32) (s16) op, uimm = op & 0xFFFF;
	bool rc = op & 1;
	switch (op >> 26)
	{
	case 7: { int x = G (ra); a.movw (7, simm); a.alu (MUL_W, W (d), x, 7); return false; }	// mulli
	case 8: { int x = G (ra); a.movw (7, simm); a.alu (SUBS_W, W (d), 7, x); setCaFlag (); return false; }	// subfic
	case 10:							// cmpli
	{
		int x = G (ra);
		if (uimm < 4096) a.cmpi (x, uimm); else { a.movw (7, uimm); a.cmp (x, 7); }
		crSet (d >> 2, false);
		return false;
	}
	case 11:							// cmpi
	{
		int x = G (ra);
		if (simm < 4096) a.cmpi (x, simm); else { a.movw (7, simm); a.cmp (x, 7); }
		crSet (d >> 2, true);
		return false;
	}
	case 12: case 13:						// addic, addic.
	{
		int x = G (ra); a.movw (7, simm);
		int r = W (d);
		a.alu (ADDS_W, r, x, 7); setCaFlag ();
		if ((op >> 26) == 13) setCr0 (r);
		return false;
	}
	case 14: case 15:						// addi, addis
	{
		u32 c = (op >> 26) == 15 ? uimm << 16 : simm;
		if (ra == 0) a.movw (W (d), c); else { int x = G (ra); addConst (W (d), x, c); }
		return false;
	}
	case 16:							// bc
	{
		u32 target = ((op & 2) ? 0 : pc) + (u32) (s32) (s16) (op & 0xFFFC);
		if (op & 1) a.movw (W (G_LR), pc + 4);
		u32 *nt[2]; int n = bcTests ((u32) d, (u32) ra, true, nt);
		bool fused = pend.on; pend.on = false;
		if (fused && !pend.deadTaken) crFromFlags (pend.crf, pend.sgn);
		if (idle && target == (bKey & ~3u)) idleExit (target, (idx + 1) * 2);	// (a polling loop goes on)
		else exitTo (target, (idx + 1) * 2, false);
		if (n)								// (not taken: the block goes on)
		{
			for (int i = 0; i < n; i++) Asm::patch (nt[i], a.p);
			if (fused && !pend.deadFall) crFromFlags (pend.crf, pend.sgn);
			return false;
		}
		return true;
	}
	case 18:							// b
	{
		u32 off = op & 0x03FFFFFC; if (off & 0x02000000) off |= 0xFC000000;
		u32 target = ((op & 2) ? 0 : pc) + off;
		if (op & 1) a.movw (W (G_LR), pc + 4);
		if (target == pc && idx == 0 && !(op & 1))		// "b .": idle until the next event
		{
			idleExit (target, 2);
			return true;
		}
		if ((op & 1) && target != pc + 4) { callTo (target, pc + 4, (idx + 1) * 2); return true; }	// (a call; "bl $+4": the pc read)
		exitTo (target, (idx + 1) * 2, false);
		return true;
	}
	case 19:
		switch ((op >> 1) & 0x3FF)
		{
		case 16: case 528:					// bclr, bcctr
		{
			bool toLr = ((op >> 1) & 0x3FF) == 16;
			a.movn (7, 3, 0); a.alu (AND_W, 6, G (toLr ? G_LR : G_CTR), 7);
			if (op & 1) a.movw (W (G_LR), pc + 4);
			u32 *nt[2]; int n = bcTests ((u32) d, (u32) ra, toLr, nt);
			bool fused = pend.on; pend.on = false;
			if (fused) crFromFlags (pend.crf, pend.sgn);		// (a return, a call: read, maybe)
			u32 synced0 = synced;
			if (toLr && !(op & 1)) retTo ((idx + 1) * 2);		// (a return)
			else if (!toLr && (op & 1)) callReg (pc + 4, (idx + 1) * 2);	// (an indirect call)
			else { a.mov (0, 6); exitReg ((idx + 1) * 2, false); }
			synced = synced0;					// (not taken: the block goes on as before)
			if (n)							// (not taken: the block goes on)
			{
				for (int i = 0; i < n; i++) Asm::patch (nt[i], a.p);
				if (fused && !pend.deadFall) crFromFlags (pend.crf, pend.sgn);
				return false;
			}
			return true;
		}
		case 150: exitTo (pc + 4, (idx + 1) * 2, false); return true;	// isync
		case 50: interp (op, pc, idx, true); return true;		// rfi
		case 257: case 129: case 289: case 225: case 33: case 449: case 417: case 193:	// the CR logic
		{
			int c = RW (G_CR);
			a.ubfx (4, c, 31 - ra, 1); a.ubfx (5, c, 31 - rb, 1);
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
			a.bfi (c, 4, 31 - d, 1);
			return false;
		}
		}
		interp (op, pc, idx, false);
		return false;
	case 17: interp (op, pc, idx, true); return true;		// sc
	case 20:							// rlwimi
	{
		int sh = rb, mb = (int) ((op >> 6) & 31), me = (int) ((op >> 1) & 31);
		int s = G (d), x = RW (ra);
		if (mb <= me)
		{
			int lsb = 31 - me, w = me - mb + 1;
			a.ror (5, s, (32 - sh + lsb) & 31);
			a.bfi (x, 5, lsb, w);
		}
		else
		{
			a.ror (5, s, (32 - sh) & 31);
			a.movw (4, mask (mb, me));
			a.alu (AND_W, 5, 5, 4); a.alu (BIC_W, x, x, 4); a.alu (ORR_W, x, x, 5);
		}
		if (rc) setCr0 (x);
		return false;
	}
	case 21:							// rlwinm
	{
		int sh = rb, mb = (int) ((op >> 6) & 31), me = (int) ((op >> 1) & 31);
		int s = G (d), x = W (ra);
		if (sh && mb == 0 && me == 31 - sh) a.lsl (x, s, sh);		// slwi
		else if (sh && me == 31 && mb == 32 - sh) a.lsr (x, s, mb);	// srwi
		else
		{
			int src = s;
			if (sh) { a.ror (5, s, 32 - sh); src = 5; }
			andMask (x, src, mb, me);
		}
		if (rc) setCr0 (x);
		return false;
	}
	case 23:							// rlwnm
	{
		int s = G (d), b = G (rb);
		a.alu (SUB_W, 7, WZR, b);
		a.alu (RORV_W, 5, s, 7);
		int x = W (ra);
		andMask (x, 5, (int) ((op >> 6) & 31), (int) ((op >> 1) & 31));
		if (rc) setCr0 (x);
		return false;
	}
	case 24: case 25: case 26: case 27: case 28: case 29:		// ori, oris, xori, xoris, andi., andis.
	{
		int o = (int) (op >> 26);
		u32 c = (o & 1) ? uimm << 16 : uimm;
		int s = G (d);
		if (c == 0 && o <= 27) { if (ra != d) a.mov (W (ra), s); return false; }	// (nop, mr)
		int x = W (ra);
		a.logi (o <= 25 ? ORR_I : o <= 27 ? EOR_I : AND_I, x, s, c);
		if (o >= 28) setCr0 (x);
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
	case 46: multi (op, pc, idx, true); return false;		// lmw
	case 47: multi (op, pc, idx, false); return false;		// stmw
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
	u32 x9 = (op >> 1) & 0x1FF;
	if (!(op & 0x400) && (x9 == 266 || x9 == 10 || x9 == 138 || x9 == 234 || x9 == 202 || x9 == 40 || x9 == 8 || x9 == 136 ||
	                      x9 == 232 || x9 == 200 || x9 == 104 || x9 == 235 || x9 == 75 || x9 == 11 || x9 == 459 || x9 == 491))
	{								// the XO-form arithmetic, without OE
		bool one = x9 == 234 || x9 == 202 || x9 == 232 || x9 == 200 || x9 == 104;	// (rA only)
		int x = G (ra), y = one ? WZR : G (rb);
		if (x9 == 234 || x9 == 232) { a.movn (7, 0, 0); y = 7; }	// (-1)
		if (x9 == 138 || x9 == 234 || x9 == 202 || x9 == 136 || x9 == 232 || x9 == 200) caToFlag ();
		int r = W (d);
		switch (x9)
		{
		case 266: a.alu (ADD_W, r, x, y); break;			// add
		case 10: a.alu (ADDS_W, r, x, y); break;			// addc
		case 138: case 234: case 202: a.alu (ADCS_W, r, x, y); break;	// adde, addme, addze
		case 40: a.alu (SUB_W, r, y, x); break;				// subf
		case 8: a.alu (SUBS_W, r, y, x); break;				// subfc
		case 136: case 232: case 200: a.alu (SBCS_W, r, y, x); break;	// subfe, subfme, subfze (~a + y + CA)
		case 104: a.alu (SUB_W, r, WZR, x); break;			// neg
		case 235: a.alu (MUL_W, r, x, y); break;			// mullw
		case 75: a.alu (SMULL, 5, x, y); a.bfm (UBFM_X, r, 5, 32, 63); break;	// mulhw
		case 11: a.alu (UMULL, 5, x, y); a.bfm (UBFM_X, r, 5, 32, 63); break;	// mulhwu
		case 491:							// divw (/ 0: -1 or 0 by the sign; INT_MIN / -1: -1)
			a.alu (SDIV_W, 5, x, y);
			a.asr (6, x, 31); a.cmpi (y, 0); a.csel (CSEL_W, 5, 6, 5, EQ);
			a.movz (7, 0x8000, 1); a.cmp (x, 7);
			a.put (0x3A400800u | 1u << 16 | (u32) EQ << 12 | (u32) y << 5 | 0u);	// CCMN y, #1, #0, EQ
			a.put (0x5A800000u | 31u << 16 | (u32) NE << 12 | 5u << 5 | (u32) r);	// CSINV r, w5, wzr, NE
			break;
		default: a.alu (UDIV_W, r, x, y); break;			// divwu
		}
		if (x9 != 266 && x9 != 40 && x9 != 104 && x9 != 235 && x9 != 75 && x9 != 11 && x9 != 459 && x9 != 491) setCaFlag ();
		if (rc) setCr0 (r);
		return false;
	}
	switch (xo)
	{
	case 0: case 32:						// cmp, cmpl
	{
		int x = G (ra), y = G (rb); a.cmp (x, y);
		crSet (d >> 2, xo == 0);
		return false;
	}
	case 28: case 60: case 444: case 412: case 316: case 476: case 124: case 284:	// and andc or orc xor nand nor eqv
	case 24: case 536: case 792: case 824: case 26: case 954: case 922:	// slw srw sraw srawi cntlzw extsb extsh
	{
		int s = G (d), b = (xo != 824 && xo != 26 && xo != 954 && xo != 922) ? G (rb) : WZR;
		int x = W (ra);
		switch (xo)
		{
		case 28: a.alu (AND_W, x, s, b); break;
		case 60: a.alu (BIC_W, x, s, b); break;
		case 444: if (s != b || x != s) a.alu (ORR_W, x, s, b); break;
		case 412: a.alu (ORN_W, x, s, b); break;
		case 316: a.alu (EOR_W, x, s, b); break;
		case 476: a.alu (AND_W, x, s, b); a.alu (ORN_W, x, WZR, x); break;
		case 124: a.alu (ORR_W, x, s, b); a.alu (ORN_W, x, WZR, x); break;
		case 284: a.alu (EON_W, x, s, b); break;
		case 24: a.alu (LSLV_W | X64, 5, s, b); a.mov (x, 5); break;	// (64-bit: a shift of 32..63 gives 0)
		case 536: a.alu (LSRV_W | X64, 5, s, b); a.mov (x, 5); break;
		case 792:						// sraw
			a.bfm (SBFM_X, 5, s, 0, 31);
			a.alu (ASRV_W | X64, 6, 5, b);
			a.alu (LSLV_W | X64, 7, 6, b);
			a.alu (SUBS_W | X64, WZR, 7, 5);
			a.cset (3, NE);
			a.alu (AND_W, 3, 3, 5, 1, 31);
			a.mov (x, 6);
			setCaReg (3);
			break;
		case 824:						// srawi
			if (rb == 0) { if (x != s) a.mov (x, s); setCaReg (WZR); break; }
			a.lsl (5, s, 32 - rb);
			a.cmpi (5, 0);
			a.cset (3, NE);
			a.alu (AND_W, 3, 3, s, 1, 31);
			a.asr (x, s, rb);
			setCaReg (3);
			break;
		case 26: a.un (CLZ_W, x, s); break;
		case 954: a.bfm (SBFM_W, x, s, 0, 7); break;
		case 922: a.bfm (SBFM_W, x, s, 0, 15); break;
		}
		if (rc) setCr0 (x);
		return false;
	}
	case 19: { int c = G (G_CR); a.mov (W (d), c); return false; }	// mfcr
	case 144:							// mtcrf
	{
		u32 crm = (op >> 12) & 0xFF, mk = 0;
		for (int i = 0; i < 8; i++) if (crm & (0x80 >> i)) mk |= 0xF0000000u >> (i * 4);
		int s = G (d);
		if (mk == 0xFFFFFFFFu) { a.mov (W (G_CR), s); return false; }
		int c = RW (G_CR);
		a.movw (5, mk);
		a.alu (BIC_W, c, c, 5); a.alu (AND_W, 6, s, 5); a.alu (ORR_W, c, c, 6);
		return false;
	}
	case 83: a.ldst (LDR_W, 2, W (d), XM, oMsr); return false;	// mfmsr
	case 146: interp (op, pc, idx, true); return true;		// mtmsr
	case 339:							// mfspr
	{
		u32 n = ((op >> 16) & 31) | ((op >> 6) & 0x3E0);
		if (n == 1 || n == 8 || n == 9) { int v = G (n == 1 ? G_XER : n == 8 ? G_LR : G_CTR); a.mov (W (d), v); return false; }
		if (n == 268 || n == 269) return op31 ((op & ~(0x3FFu << 1)) | 371u << 1, pc, idx);	// (the timebase)
		interp (op, pc, idx, false);
		return false;
	}
	case 467:							// mtspr
	{
		u32 n = ((op >> 16) & 31) | ((op >> 6) & 0x3E0);
		if (n == 8 || n == 9) { int s = G (d); a.mov (W (n == 8 ? G_LR : G_CTR), s); return false; }
		if (n == 1) { int s = G (d); a.logi (AND_I, W (G_XER), s, 0xE000FF7F); return false; }
		interp (op, pc, idx, true);				// (BATs, HID0, the DEC...: back to jitRun)
		return true;
	}
	case 86: case 54: case 278: case 246: case 598: case 854: case 306: case 566: case 470: return false;	// dcbf dcbst dcbt dcbtst sync eieio tlbie tlbsync dcbi
	case 1014:							// dcbz: 32 bytes of MEM1 at once
	{
		eaX (op, false);
		a.logi (AND_I, 1, 1, ~31u);
		Def &cd = interpSide (op, pc, idx, true);
		cd.site = fastBlock (32);
		a.put (0xA9007C9Fu);					// STP xzr, xzr, [x4]
		a.put (0xA9017C9Fu);					// STP xzr, xzr, [x4, #16]
		cd.ret = a.p;
		return false;
	}
	case 371:							// mftb (TBL / TBU): from the cycles
	{
		u32 n = ((op >> 16) & 31) | ((op >> 6) & 0x3E0);
		if (n != 268 && n != 269) break;
		a.ldst (LDR_X, 3, 5, XM, oEnd); a.alu (SUB_W | X64, 5, 5, XDC);
		if (idx * 2 > synced) a.imm (ADD_WI | X64, 5, 5, idx * 2 - synced);
		a.movz (6, CYC_PER_TB, 0, true); a.alu (UDIV_W | X64, 5, 5, 6);
		a.ldst (LDR_X, 3, 6, XM, oTb); a.alu (ADD_W | X64, 5, 5, 6);
		if (n == 269) a.bfm (UBFM_X, 5, 5, 32, 63);
		a.mov (W (d), 5);
		return false;
	}
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
// The FPRs are cached in q8..q31 (lane 0 = ps0, lane 1 = ps1): the plain FPU works on lane 0 (a
// double result keeps ps1, a single one goes to both), the paired singles on both lanes at once.
// A result that is a NaN goes to the interpreter (the 750 keeps the first NaN operand); FPRF is set
// lazily (fprfSet). Every register an instruction uses is taken before its first branch.
bool Jit::fpInsn (u32 op, u32 pc, u32 idx)
{
	u32 prim = op >> 26;
	int d = (int) ((op >> 21) & 31), ra = (int) ((op >> 16) & 31), rb = (int) ((op >> 11) & 31), rc = (int) ((op >> 6) & 31);
	u32 x5 = (op >> 1) & 0x1F, x10 = (op >> 1) & 0x3FF;
	if (op & 1) { interp (op, pc, idx, false); return false; }	// (Rc: CR1 from the FPSCR)
	int nanDef = -1;						// (its cold path: the interpreter)
	if (prim == 59 || prim == 63)
	{
		bool single = prim == 59;
		if (x5 == 18 || x5 == 20 || x5 == 21 || x5 == 25 || x5 >= 28 || (single && x5 == 24) || (!single && x5 == 26))
		{
			fpCheck (op, pc, idx);
			bool one = x5 == 24 || x5 == 26;			// (fres, frsqrte: frB only)
			int ha = one ? -1 : FG (ra), hb = x5 != 25 ? FG (rb) : -1, hc = (x5 == 25 || x5 >= 28) ? FG (rc) : -1;
			int hd = FRes (d, !single, true);			// (a double result keeps ps1)
			int mc = hc;
			if (hc >= 0 && single && !isS (sgl0, rc)) { force25x (hc, 0); a.fp1 (FMOV_DX, 2, 5); mc = 2; }
			switch (x5)
			{
			case 18: a.fp3 (FDIV_D, 3, ha, hb); break;
			case 20: a.fp3 (FSUB_D, 3, ha, hb); break;
			case 21: a.fp3 (FADD_D, 3, ha, hb); break;
			case 25: a.fp3 (FMUL_D, 3, ha, mc); break;
			case 24: a.put (FMOV_D1 | 4); a.fp3 (FDIV_D, 3, 4, hb); break;	// fres: 1 / b (rounded to single)
			case 26: a.fp1 (0x1E61C000u, 5, hb); a.put (FMOV_D1 | 4); a.fp3 (FDIV_D, 3, 4, 5); break;	// frsqrte: 1 / sqrt (b)
			case 28: a.fp4 (FNMSUB_D, 3, ha, mc, hb); break;		// a * c - b
			case 29: a.fp4 (FMADD_D, 3, ha, mc, hb); break;		// a * c + b
			case 30: a.fp4 (FNMSUB_D, 3, ha, mc, hb); a.fp1 (FNEG_D, 3, 3); break;
			default: a.fp4 (FMADD_D, 3, ha, mc, hb); a.fp1 (FNEG_D, 3, 3); break;
			}
			a.fp3 (FCMP_D, 0, 3, 3); { nanDef = nDefs; Def &nd = interpSide (op, pc, idx, true); nd.site = a.p; a.bcond (VS, a.p); }
			if (single) { roundS (3); a.put (0x4E080400u | 3u << 5 | (u32) hd); }	// (DUP Vd.2D, V3.D[0])
			else a.ins (hd, 0, 3, 0);
			fSet (d); fprfSet (d, 0);
			setS (sgl0, d, single); if (single) setS (sgl1, d, true);
		}
		else if (prim == 63 && x5 == 23)				// fsel
		{
			fpCheck (op, pc, idx);
			int ha = FG (ra), hb = FG (rb), hc = FG (rc), hd = FRes (d, true, false);
			a.fp1 (FCMP_D0, 0, ha);
			a.fcsel (3, hc, hb, GE);
			a.ins (hd, 0, 3, 0); fSet (d);
			setS (sgl0, d, isS (sgl0, rc) && isS (sgl0, rb));
			return false;
		}
		else if (prim == 63 && (x10 == 0 || x10 == 32))		// fcmpu, fcmpo
		{
			fpCheck (op, pc, idx);
			fprfFlush (); fprfR = -1;				// (its FPCC goes over the FPRF to set)
			a.ldst (LDRB, 0, 5, XM, oFprfPend);
			{ Def &fd = defer (D_FPRF); fd.site = a.p; a.cbz (5, a.p, true); fd.ret = a.p; }
			int ha = FG (ra), hb = FG (rb);
			a.fp3 (FCMP_D, 0, ha, hb);
			a.movz (3, 4, 0); a.movz (4, 8, 0); a.csel (CSEL_W, 3, 4, 3, MI);
			a.movz (4, 2, 0); a.csel (CSEL_W, 3, 4, 3, EQ);
			a.movz (4, 1, 0); a.csel (CSEL_W, 3, 4, 3, VS);
			a.bfi (RW (G_CR), 3, 28 - (d >> 2) * 4, 4);
			ldF (4, oFpscr); a.bfi (4, 3, 12, 4); stF (4, oFpscr);
			return false;
		}
		else if (prim == 63 && (x10 == 12 || x10 == 15))		// frsp, fctiwz
		{
			fpCheck (op, pc, idx);
			int hb = FG (rb), hd = FRes (d, true, x10 == 12);
			a.fp3 (FCMP_D, 0, hb, hb); { nanDef = nDefs; Def &nd = interpSide (op, pc, idx, true); nd.site = a.p; a.bcond (VS, a.p); }
			if (x10 == 12)
			{
				a.fp1 (FCVT_SD, 3, hb); a.fp1 (FCVT_DS, 3, 3);
				a.ins (hd, 0, 3, 0); fSet (d); fprfSet (d, 0);
				setS (sgl0, d, true);
			}
			else
			{
				a.fp1 (FCVTZS_WD, 5, hb);
				a.movz (6, 0xFFF8, 3, true);
				a.alu (ORR_W | X64, 5, 5, 6);
				a.insX (hd, 0, 5); fSet (d);
				setS (sgl0, d, false);
			}
		}
		else if (prim == 63 && (x10 == 40 || x10 == 72 || x10 == 136 || x10 == 264))	// fneg, fmr, fnabs, fabs
		{
			fpCheck (op, pc, idx);
			int hb = FG (rb), hd = FRes (d, true, false);
			if (x10 == 72) a.ins (hd, 0, hb, 0);
			else
			{
				if (x10 == 40) a.fp1 (FNEG_D, 3, hb);
				else if (x10 == 136) { a.fp1 (FABS_D, 3, hb); a.fp1 (FNEG_D, 3, 3); }
				else a.fp1 (FABS_D, 3, hb);
				a.ins (hd, 0, 3, 0);
			}
			fSet (d);
			setS (sgl0, d, isS (sgl0, rb));
			return false;
		}
		else { interp (op, pc, idx, false); return false; }
	}
	else if (((op >> 1) & 0x3F) == 6 || ((op >> 1) & 0x3F) == 7 || ((op >> 1) & 0x3F) == 38 || ((op >> 1) & 0x3F) == 39)
	{								// psq_lx, psq_stx, psq_lux, psq_stux
		u32 x6 = (op >> 1) & 0x3F;
		return psq (op, pc, idx, x6 == 6 || x6 == 38, x6 >= 38, true);
	}
	else								// the paired singles
	{
		bool arith = x5 == 10 || x5 == 11 || (x5 >= 12 && x5 <= 15) || x5 == 18 || x5 == 20 || x5 == 21 || x5 == 23 || x5 == 24 || x5 == 25 || x5 == 26 || x5 >= 28;
		bool move = x10 == 40 || x10 == 72 || x10 == 136 || x10 == 264 || x10 == 528 || x10 == 560 || x10 == 592 || x10 == 624;
		if (!arith && !move) { interp (op, pc, idx, false); return false; }
		fpCheck (op, pc, idx);
		if (move)
		{
			bool usesA = x10 >= 528;
			int ha = usesA ? FG (ra) : -1, hb = FG (rb), hd = FRes (d, false, false);
			bool l0 = x10 == 592 || x10 == 624 ? isS (sgl1, ra) : x10 >= 528 ? isS (sgl0, ra) : isS (sgl0, rb);
			bool l1 = x10 == 528 || x10 == 592 ? isS (sgl0, rb) : isS (sgl1, rb);
			switch (x10)
			{
			case 40: a.fp1 (FNEG_2D, hd, hb); break;
			case 72: a.vmov (hd, hb); break;
			case 136: a.fp1 (FABS_2D, hd, hb); a.fp1 (FNEG_2D, hd, hd); break;
			case 264: a.fp1 (FABS_2D, hd, hb); break;
			case 528: a.fp3 (ZIP1_2D, hd, ha, hb); break;		// (a0, b0)
			case 624: a.fp3 (ZIP2_2D, hd, ha, hb); break;		// (a1, b1)
			case 592: a.ext8 (hd, ha, hb); break;			// (a1, b0)
			default: a.vmov (4, hb); a.ins (4, 0, ha, 0); a.vmov (hd, 4); break;	// (a0, b1)
			}
			fSet (d);
			setS (sgl0, d, l0); setS (sgl1, d, l1);
			return false;
		}
		switch (x5)
		{
		case 10: case 11:					// ps_sum0 (a0 + b1, c1), ps_sum1 (c0, a0 + b1)
		{
			int ha = FG (ra), hb = FG (rb), hc = FG (rc), hd = FRes (d, false, true);
			a.ins (4, 0, hb, 1);
			a.fp3 (FADD_D, 5, ha, 4);
			a.fp3 (FCMP_D, 0, 5, 5); { nanDef = nDefs; Def &nd = interpSide (op, pc, idx, true); nd.site = a.p; a.bcond (VS, a.p); }
			roundS (5);
			a.vmov (hd, hc);
			a.ins (hd, x5 == 10 ? 0 : 1, 5, 0);
			fSet (d); fprfSet (d, x5 == 10 ? 0 : 1);
			if (x5 == 10) { setS (sgl1, d, isS (sgl1, rc)); setS (sgl0, d, true); }
			else { setS (sgl0, d, isS (sgl0, rc)); setS (sgl1, d, true); }
			break;
		}
		case 23:						// ps_sel
		{
			int ha = FG (ra), hb = FG (rb), hc = FG (rc), hd = FRes (d, false, false);
			bool l0 = isS (sgl0, rc) && isS (sgl0, rb), l1 = isS (sgl1, rc) && isS (sgl1, rb);
			a.fp1 (FCMGE0_2D, 4, ha);
			a.fp3 (BSL_16B, 4, hc, hb);
			a.vmov (hd, 4); fSet (d);
			setS (sgl0, d, l0); setS (sgl1, d, l1);
			return false;
		}
		default:
		{
			bool usesB = x5 != 12 && x5 != 13 && x5 != 25, usesC = x5 != 18 && x5 != 20 && x5 != 21 && x5 != 24 && x5 != 26;
			bool usesA = x5 != 24 && x5 != 26;
			int ha = usesA ? FG (ra) : -1, hb = usesB ? FG (rb) : -1, hc = usesC ? FG (rc) : -1, hd = FRes (d, false, true);
			// the multiplicand: both halves of c (mul, madd...), or one half for both (muls0/1, madds0/1)
			int mc = hc, lane = -1;
			if (x5 == 12 || x5 == 14) lane = 0;
			if (x5 == 13 || x5 == 15) lane = 1;
			if (usesC)
			{
				if (lane >= 0)
				{
					if (!isS (lane ? sgl1 : sgl0, rc)) { force25x (hc, lane); a.insX (4, 0, 5); mc = 4; lane = 0; }
				}
				else
				{
					bool f0 = !isS (sgl0, rc), f1 = !isS (sgl1, rc);
					if (f0 || f1)
					{
						a.vmov (4, hc);
						if (f0) { force25x (hc, 0); a.insX (4, 0, 5); }
						if (f1) { force25x (hc, 1); a.insX (4, 1, 5); }
						mc = 4;
					}
				}
			}
			switch (x5)
			{
			case 18: a.fp3 (FDIV_2D, 6, ha, hb); break;
			case 24: a.put (FMOV_D1 | 4); a.put (0x4E080400u | 4u << 5 | 4u); a.fp3 (FDIV_2D, 6, 4, hb); break;	// ps_res: 1 / b
			case 26: a.fp1 (0x6EE1F800u, 5, hb); a.put (FMOV_D1 | 4); a.put (0x4E080400u | 4u << 5 | 4u); a.fp3 (FDIV_2D, 6, 4, 5); break;	// ps_rsqrte
			case 20: a.fp3 (FSUB_2D, 6, ha, hb); break;
			case 21: a.fp3 (FADD_2D, 6, ha, hb); break;
			case 25: a.fp3 (FMUL_2D, 6, ha, mc); break;
			case 12: case 13: a.byElem (FMUL_2DE, 6, ha, mc, lane); break;
			case 14: case 15: a.vmov (6, hb); a.byElem (FMLA_2DE, 6, ha, mc, lane); break;
			case 29: a.vmov (6, hb); a.fp3 (FMLA_2D, 6, ha, mc); break;				// a * c + b
			case 28: a.fp1 (FNEG_2D, 6, hb); a.fp3 (FMLA_2D, 6, ha, mc); break;			// a * c - b
			case 30: a.fp1 (FNEG_2D, 6, hb); a.fp3 (FMLA_2D, 6, ha, mc); a.fp1 (FNEG_2D, 6, 6); break;
			default: a.vmov (6, hb); a.fp3 (FMLA_2D, 6, ha, mc); a.fp1 (FNEG_2D, 6, 6); break;
			}
			a.fp1 (FMAXP_D, 5, 6);					// (a NaN in either half)
			a.fp3 (FCMP_D, 0, 5, 5); { nanDef = nDefs; Def &nd = interpSide (op, pc, idx, true); nd.site = a.p; a.bcond (VS, a.p); }
			roundS2 (6);
			a.vmov (hd, 6); fSet (d); fprfSet (d, 0);
			setS (sgl0, d, true); setS (sgl1, d, true);
		}
		}
	}
	if (nanDef >= 0) defs[nanDef].ret = a.p;			// (a NaN: the interpreter did it again)
	return false;
}

// lfs (kind 0), lfd (1), stfs (2), stfd (3), stfiwx (4): D-form / X-form, with update
bool Jit::fpLoadStore (u32 op, u32 pc, u32 idx, int kind, bool x, bool upd)
{
	int d = (int) ((op >> 21) & 31), ra = (int) ((op >> 16) & 31);
	fpCheck (op, pc, idx);
	int hp = x ? -1 : basePtr (ra, op, pc, idx);		// (a base's pointer: made before the branches that come back)
	s32 disp = (s32) (s16) op;
	auto ea = [&] () { if (x) eaX (op, upd); else eaD (op, upd); if (upd) a.mov (WEA, 1); };	// (the address: w1; WEA)
	auto mem = [&] (bool store, int size, int fpv)
	{
		if (hp >= 0) ptrAccess (store, size, fpv, store ? 2 : 0, hp, disp);
		else memop (store, size, pc, idx, fpv);
	};
	int nanK = -1;
	if (kind == 2 && isS (sgl0, d))					// (stfs of a single: FCVT is exact, a NaN aside)
	{
		int hs = FG (d);
		if (hp < 0 || upd) ea ();				// (the registers taken before the NaN's branch)
		nanK = nanSide (hs, op, pc, idx);
		a.fp1 (FCVT_SD, 0, hs);
		mem (true, 4, 0);
	}
	else if (kind >= 2)
	{
		int hs = FG (d);
		a.fp1 (FMOV_XD, kind == 2 ? 8 : 2, hs);
		if (kind == 2) cvtS (2, 8);
		if (hp < 0 || upd) ea ();
		mem (true, kind == 3 ? 8 : 4, -1);
	}
	else
	{
		if (hp < 0 || upd) ea ();
		int hd = FRes (d, kind == 1, false);			// (lfd keeps ps1)
		if (kind == 0)
		{
			mem (false, 4, 0);				// (s0)
			a.fp1 (FCVT_DS, 0, 0);
			nanK = nanSide (0, op, pc, idx);
			a.put (0x4E080400u | (u32) hd);			// DUP Vd.2D, V0.D[0]
			setS (sgl0, d, true); setS (sgl1, d, true);
		}
		else { mem (false, 8, -1); a.insX (hd, 0, 0); setS (sgl0, d, false); }
		fSet (d);
	}
	if (upd) stG (WEA, ra);
	if (nanK >= 0) defs[nanK].ret = a.p;
	return false;
}

// psq_l / psq_st (+ u): translated for the GQR's value when the block is translated (a float type:
// two floats in one 8-byte access, or one with W; an integer type u8 / u16 / s8 / s16 with its
// scale: both elements in one access, converted and scaled); the GQR checked at run time, another
// value through the interpreter
void Jit::eaPsq (int hbase, int hidx, u32 off)
{
	if (hidx >= 0) { if (hbase < 0) a.mov (1, hidx); else a.alu (ADD_W, 1, hbase, hidx); }
	else if (hbase < 0) a.movw (1, off);
	else addConst (1, hbase, off);
}
static double qmul (int s6) { s6 &= 63; if (s6 & 0x20) s6 -= 64; double m = 1.0; while (s6 > 0) { m *= 2.0; s6--; } while (s6 < 0) { m *= 0.5; s6++; } return m; }
static u64 dbits (double v) { u64 u; __builtin_memcpy (&u, &v, 8); return u; }

bool Jit::psq (u32 op, u32 pc, u32 idx, bool load, bool upd, bool x)
{
	int d = (int) ((op >> 21) & 31), ra = (int) ((op >> 16) & 31), rb = (int) ((op >> 11) & 31);
	int q = x ? (int) ((op >> 7) & 7) : (int) ((op >> 12) & 7); bool w = x ? (op >> 10) & 1 : (op >> 15) & 1;
	u32 off = x ? 0 : (u32) ((s32) ((op & 0xFFF) << 20) >> 20);
	u32 g = m->gqr[q];
	int type = (int) (load ? g >> 16 : g) & 7, scale = (int) (load ? g >> 24 : g >> 8) & 63;
	bool isInt = type >= 4;
	fpCheck (op, pc, idx);
	int hp = !x && !upd ? basePtr (ra, op, pc, idx) : -1;		// (a base's pointer: made before the branches)
	int hbase = (ra == 0 && !upd) ? -1 : (upd ? RW (ra) : G (ra));	// (all taken before the branch)
	int hidx = x ? G (rb) : -1;
	auto pmem = [&] (bool store, int size, int fpv)
	{
		if (hp >= 0) ptrAccess (store, size, fpv, store ? 2 : 0, hp, (s32) off);
		else memop (store, size, pc, idx, fpv);
	};
	int hd = load ? FRes (d, false, false) : FG (d);
	int oth = -1;
	u32 gbit = 1u << (q * 2 + (load ? 1 : 0));
	if (!isInt)							// (types 0..3: floats)
	{
		if (!(gqrOk & gbit))
		{
			ldF (5, oGqr + 4 * q);
			a.ubfx (6, 5, load ? 18 : 2, 1);
			Def &od = interpSide (op, pc, idx, false); od.site = a.p; a.cbz (6, a.p, true);
			gqrOk |= gbit;
		}
	}
	else								// (this type and scale)
	{
		ldF (5, oGqr + 4 * q);
		oth = nDefs;
		a.ubfx (6, 5, load ? 16 : 0, 14);
		a.logi (AND_I, 6, 6, 0x3F07u);
		a.movw (7, (u32) (scale << 8 | type));
		a.cmp (6, 7);
		Def &od = interpSide (op, pc, idx, true); od.site = a.p; a.bcond (NE, a.p);
	}
	int es = (type == 4 || type == 6) ? 1 : 2, bits = es * 8;	// (an integer's size)
	bool sgn = type >= 6;
	int nan1 = -1;
	if (load)
	{
		if (hp < 0) eaPsq (hbase, hidx, off);
		if (upd) a.mov (WEA, 1);
		if (isInt)
		{
			pmem (false, w ? es : 2 * es, -1);
			int sh = w ? 0 : bits;
			a.bfm (sgn ? SBFM_W : UBFM_W, 5, 0, sh, sh + bits - 1);
			a.fp1 (sgn ? 0x1E620000u : 0x1E630000u, 0, 5);		// SCVTF / UCVTF d0, w5
			if (w) a.put (FMOV_D1 | 1);
			else { a.bfm (sgn ? SBFM_W : UBFM_W, 6, 0, 0, bits - 1); a.fp1 (sgn ? 0x1E620000u : 0x1E630000u, 1, 6); }
			double mul = qmul (-scale);
			if (mul != 1.0)
			{
				a.movx (5, dbits (mul)); a.fp1 (FMOV_DX, 2, 5);
				a.fp3 (FMUL_D, 0, 0, 2);
				if (!w) a.fp3 (FMUL_D, 1, 1, 2);
			}
			a.fp3 (ZIP1_2D, hd, 0, 1);
		}
		else if (w)
		{
			pmem (false, 4, 0);			// (s0)
			a.fp1 (FCVT_DS, 0, 0);
			nan1 = nanSide (0, op, pc, idx);
			a.put (FMOV_D1 | 1);
			a.ins (hd, 0, 0, 0); a.ins (hd, 1, 1, 0);
		}
		else
		{
			pmem (false, 8, 0);			// (v0.2S: both singles)
			a.fp1 (FCVTL_2D, hd, 0);
			a.fp1 (FMAXP_D, 5, hd);				// (a NaN in either: the interpreter)
			nan1 = nanSide (5, op, pc, idx);
		}
		fSet (d);
	}
	else if (isInt)
	{
		// (double) (float) v * 2^scale, a NaN to the interpreter, truncated, clamped to the type
		a.fp1 (FCVT_SD, 0, hd); a.fp1 (FCVT_DS, 0, 0);
		if (!w) { a.ins (1, 0, hd, 1); a.fp1 (FCVT_SD, 1, 1); a.fp1 (FCVT_DS, 1, 1); }
		a.fp3 (FCMP_D, 0, 0, 0);
		{ Def &nd = interpSide (op, pc, idx, true); nd.site = a.p; a.bcond (VS, a.p); defs[nDefs - 1].ret = 0; }
		int nan1 = nDefs - 1, nan2 = -1;
		if (!w) { a.fp3 (FCMP_D, 0, 1, 1); nan2 = nDefs; Def &nd = interpSide (op, pc, idx, true); nd.site = a.p; a.bcond (VS, a.p); }
		double mul = qmul (scale);
		if (mul != 1.0)
		{
			a.movx (5, dbits (mul)); a.fp1 (FMOV_DX, 2, 5);
			a.fp3 (FMUL_D, 0, 0, 2);
			if (!w) a.fp3 (FMUL_D, 1, 1, 2);
		}
		int lo = type == 6 ? -128 : type == 7 ? -32768 : 0, hi = type == 4 ? 255 : type == 5 ? 65535 : type == 6 ? 127 : 32767;
		a.movw (7, (u32) lo); a.movw (8, (u32) hi);
		for (int k = 0; k < (w ? 1 : 2); k++)
		{
			int r = k ? 6 : 5;
			a.fp1 (FCVTZS_WD, r, k);
			a.cmp (r, 7); a.csel (CSEL_W, r, 7, r, LT);
			a.cmp (r, 8); a.csel (CSEL_W, r, 8, r, GT);
		}
		if (w) a.ubfx (2, 5, 0, bits);
		else { a.ubfx (2, 6, 0, bits); a.bfi (2, 5, bits, bits); }
		if (hp < 0) eaPsq (hbase, hidx, off);
		if (upd) a.mov (WEA, 1);
		pmem (true, w ? es : 2 * es, -1);
		if (upd) a.mov (hbase, WEA);
		if (oth >= 0) defs[oth].ret = a.p;
		defs[nan1].ret = a.p;
		if (nan2 >= 0) defs[nan2].ret = a.p;
		return false;
	}
	else if (w ? isS (sgl0, d) : isS (sgl0, d) && isS (sgl1, d))	// (singles: FCVT(N) is exact, a NaN aside)
	{
		if (w) nan1 = nanSide (hd, op, pc, idx);
		else { a.fp1 (FMAXP_D, 5, hd); nan1 = nanSide (5, op, pc, idx); }
		a.fp1 (w ? FCVT_SD : FCVTN_2S, 0, hd);
		if (hp < 0) eaPsq (hbase, hidx, off);
		if (upd) a.mov (WEA, 1);
		pmem (true, w ? 4 : 8, 0);
	}
	else
	{
		a.fp1 (FMOV_XD, 8, hd);
		if (w) cvtS (2, 8);
		else
		{
			cvtS (XK1, 8);
			a.umovX (8, hd, 1); cvtS (2, 8);
			a.alu (ORR_W | X64, 2, 2, XK1, 0, 32);
		}
		if (hp < 0) eaPsq (hbase, hidx, off);
		if (upd) a.mov (WEA, 1);
		pmem (true, w ? 4 : 8, -1);
	}
	if (upd) a.mov (hbase, WEA);
	if (load) { setS (sgl0, d, true); setS (sgl1, d, true); }	// (the integer types too: small integers x 2^n)
	if (oth >= 0) defs[oth].ret = a.p;
	if (nan1 >= 0) defs[nan1].ret = a.p;
	return false;
}

#define OFF(f) ((int) ((u8 *) &mm->f - (u8 *) mm))

Jit::Jit (Machine *mm, void *mem, u32 size)
{
	m = mm;
	code = (u32 *) mem; codeEnd = code + size / 4;
	oPc = OFF (pc); oCycles = OFF (cycles); oUntil = OFF (jitUntil); oEnd = OFF (jitEnd); oTb = OFF (tbBase); oCr = OFF (cr); oXer = OFF (xer);
	oLr = OFF (lr); oCtr = OFF (ctr); oMsr = OFF (msr); oMem1 = OFF (mem1); oScratch = OFF (jitScratch);
	oPs = OFF (ps); oFpscr = OFF (fpscr); oFprfVal = OFF (fprfVal); oFprfPend = OFF (fprfPending); oGqr = OFF (gqr); oGatherN = OFF (gatherN); oGather = OFF (gather); oIdleHit = OFF (idleHit);
	oSpBase = OFF (jitSpBase); oSpLimit = OFF (jitSpLimit);
	ctx = new u8[FAST_OFF + (sizeof (Fast) << FAST_BITS)];
	fast = (Fast *) (ctx + FAST_OFF);
	u64 *h = (u64 *) ctx;
	h[H_INTERP] = (u64) &hInterp;
	h[H_RD8] = (u64) &hRd8; h[H_RD16] = (u64) &hRd16; h[H_RD32] = (u64) &hRd32;
	h[H_WR8] = (u64) &hWr8; h[H_WR16] = (u64) &hWr16; h[H_WR32] = (u64) &hWr32;
	h[H_RD64] = (u64) &hRd64; h[H_WR64] = (u64) &hWr64; h[H_CVTD] = (u64) &hCvtD; h[H_CVTS] = (u64) &hCvtS; h[H_FPRF] = (u64) &hFprf; h[H_GATHER] = (u64) &hGather;
	blocks = new Block[MAX_BLOCKS];
	links = new Link[2 * MAX_BLOCKS];
	coldScratch = new u32[COLD_SCRATCH];
	if (m->jitProfile) profInsns = new ProfInsn[PROF_INSNS];	// (here: the translations may run where nothing is allocated)

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
	for (int i = 0; i < N_SRA; i++) a.ldst (LDR_W, 2, SRA_H[i], XM, SRA_G[i] == G_LR ? oLr : SRA_G[i] == G_CR ? oCr : SRA_G[i] * 4);	// (the kept guest registers)
	a.ldst (LDR_X, 3, 3, XM, oUntil); a.ldst (LDR_X, 3, 4, XM, oCycles);
	a.alu (SUB_W | X64, XDC, 3, 4); a.ldst (STR_X, 3, 3, XM, oEnd);	// (the countdown)
	a.movz (4, 1, 0); a.stpPre (WZR, 4, -16);			// (the calls' stack: its sentinel -- no LR is 1)
	a.imm (ADD_WI | X64, 4, SP, 0); a.ldst (STR_X, 3, 4, XM, oSpBase);
	a.imm (SUB_WI | X64, 4, 4, 2, 1); a.ldst (STR_X, 3, 4, XM, oSpLimit);	// (8 KB: 512 pairs)
	a.br (1);
	exitStub = a.p;
	a.ldst (LDR_X, 3, 3, XM, oEnd); a.alu (SUB_W | X64, 3, 3, XDC); a.ldst (STR_X, 3, 3, XM, oCycles);
	for (int i = 0; i < N_SRA; i++) a.ldst (STR_W, 2, SRA_H[i], XM, SRA_G[i] == G_LR ? oLr : SRA_G[i] == G_CR ? oCr : SRA_G[i] * 4);
	a.ldst (LDR_X, 3, 3, XM, oSpBase); a.imm (ADD_WI | X64, SP, 3, 16);	// (the calls' stack dropped, its sentinel too)
	a.put (0xA94153F3);						// ldp x19, x20, [sp, #16]
	a.put (0xA9425BF5);						// ldp x21, x22, [sp, #32]
	a.put (0xA94363F7);						// ldp x23, x24, [sp, #48]
	a.put (0xA9446BF9);						// ldp x25, x26, [sp, #64]
	a.put (0xA94573FB);						// ldp x27, x28, [sp, #80]
	a.put (0xA8C67BFD);						// ldp x29, x30, [sp], #96
	a.ret ();
	// the dispatcher: w0 = pc, w2 = the mode (IR, DR) -> the next block, or back
	dispatch = a.p;
	a.imm (SUBS_WI | X64, WZR, XDC, 0);
	a.bcond (LE, exitStub);
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
	a.p = codeStart; chunkEnd = codeStart + CHUNK_WORDS; coldTop = chunkEnd;
	nBlocks = 0; nProf = 0;
	for (u32 i = 0; i < (1u << HASH_BITS); i++) hashHead[i] = linkHead[i] = NONE;
	nLinks = 0;
	for (u32 i = 0; i < NPAGES; i++) pageHead[i] = NONE;
	for (u32 i = 0; i < (1u << FAST_BITS); i++) { fast[i].key = NOKEY; fast[i].code = exitStub; }	// (a pc giving NOKEY lands in jitRun)
	stdMap = true;
	for (u32 i = 0; i < (MEM1_SIZE >> 17); i++)
		if (m->dmap[(0x80000000u >> 17) + i] != i + 1 || m->dmap[(0xC0000000u >> 17) + i] != i + 1) stdMap = false;
	m->jitBlocks = 0;
}

static inline u32 hashOf (u32 key) { return (key >> 2) & ((1u << HASH_BITS) - 1); }

void *Jit::lookup (u32 key)
{
	if (key == NOKEY) return 0;
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
	if (f.key == b.key && f.code == b.code) { f.key = NOKEY; f.code = exitStub; }
	for (u32 k = linkHead[hashOf (b.key)]; k != NONE; k = links[k].next)	// (the exits to it: to their stubs)
		if (links[k].key == b.key)
		{
			Asm::patch (links[k].site, links[k].stub);
			flushCode (links[k].site, links[k].site + 1);
		}
	if (m->jitBlocks) m->jitBlocks--;
}

void Jit::addLink (u32 key, u32 *site, u32 *stub)
{
	Link &l = links[nLinks];
	l.key = key; l.site = site; l.stub = stub;
	l.next = linkHead[hashOf (key)]; linkHead[hashOf (key)] = nLinks++;
}

void Jit::linkTo (u32 key, void *code)
{
	for (u32 k = linkHead[hashOf (key)]; k != NONE; k = links[k].next)
		if (links[k].key == key)
		{
			Asm::patch (links[k].site, (u32 *) code);
			flushCode (links[k].site, links[k].site + 1);
		}
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

// the block's cold paths: emitted once into the scratch space (their length), then for real below the
// chunk's cold code (a.p back to the main code after)
void Jit::emitDefs ()
{
	if (nDefs)
	{
		u32 *hot = a.p;
		a.p = coldScratch; emitDefsHere ();
		u32 n = (u32) (a.p - coldScratch);
		coldTop -= n;
		a.p = coldTop; emitDefsHere ();
		flushCode (coldTop, coldTop + n);
		a.p = hot;
	}
	nDefs = 0;
}

void Jit::emitDefsHere ()
{
	a.side++;
	for (int i = 0; i < nDefs; i++)
	{
		const Def &d = defs[i];
		loadSt (d.st);
		Asm::patch (d.site, a.p);
		switch (d.kind)
		{
		case D_STUB:						// an exit's stub: out with pc = the target
			links[d.r1].stub = a.p;
			if (!d.back) Asm::patch (d.ret, a.p);		// (its target not translated yet)
			a.movw (0, d.op); stF (0, oPc); a.b (exitStub);
			break;
		case D_MEM: memCold (d); break;
		case D_INTERP: interpCold (d); break;
		case D_IEXIT: dc (2); a.b (exitStub); break;	// (the interpreter left: pc is set)
		case D_TOPC: a.mov (0, d.r1); stF (0, oPc); a.b (exitStub); break;	// (out: pc = the register)
		case D_RETMISS:						// (not the call's return: the stack emptied, the dispatcher)
			spEmpty ();
			a.mov (0, d.r1); stF (0, oPc); a.movw (2, bKey & 3); a.b (dispatch);
			break;
		case D_SPRESET: spEmpty (); a.b (d.ret); break;		// (the calls' stack past its limit: emptied)
		case D_CVTD:
			if (d.r2 != 0) a.mov (0, d.r2);
			callKeep (H_CVTD);
			a.fp1 (FMOV_DX, d.r1, 0);
			a.b (d.ret);
			break;
		case D_CVTS:
			a.bfm (UBFM_X, 6, d.r2, 63, 62);			// (x << 1: zero -> the plain way)
			a.cbz (6, d.ret2, false, true);
			a.movX (0, d.r2);
			callKeep (H_CVTS);
			a.mov (d.r1, 0);
			a.b (d.ret);
			break;
		default: a.movX (0, XM); callKeep (H_FPRF); a.b (d.ret); break;	// (D_FPRF)
		}
	}
	a.side--;
}

// the next chunk (its main code from its start, its cold code from its end); none left: everything dropped
void Jit::nextChunk ()
{
	u32 *next = chunkEnd;
	if (next + CHUNK_WORDS > codeEnd) { flushAll (); return; }
	a.p = next; chunkEnd = next + CHUNK_WORDS; coldTop = chunkEnd;
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
	if (nBlocks >= MAX_BLOCKS || nLinks + 2 * MAX_INSNS >= 2 * MAX_BLOCKS) flushAll ();
	if (coldTop - a.p < BLOCK_ROOM) nextChunk ();				// (the block's main and cold code: room)
	bKey = key; synced = 0; fpOk = false; sgl0 = sgl1 = 0; nDefs = 0; gqrOk = 0;
	bPa = pa; pend.on = false;
	idle = idleLoop (pa, pc);
	cacheReset ();
	dmode = !(key & 2) ? 0 : stdMap ? 1 : 2;
	u32 *start = a.p;
	u32 i = nBlocks++;
	Block &b = blocks[i];
	b.runs = 0; b.prof = ~0u; b.profN = 0;
	if (m->jitProfile)						// (the test's profile: the block's runs)
	{
		if (profInsns && nProf + MAX_INSNS + 1 < PROF_INSNS) b.prof = nProf;
		a.side++;
		a.movx (16, (u64) &b.runs); a.ldst (LDR_X, 3, 17, 16, 0); a.imm (ADD_WI | X64, 17, 17, 1); a.ldst (STR_X, 3, 17, 16, 0);
		a.side--;
	}
	a.mainWords = 0; a.side = 0;
	u32 n = 0;
	for (;;)
	{
		u32 op = bswap32 (*(const u32 *) (m->mem1 + pa + n * 4));
		pinned = 0; fpinned = 0;
		u32 w0 = a.mainWords;
		bool end = insn (op, pc + n * 4, n);
		if (b.prof != ~0u) { profInsns[nProf].op = op; profInsns[nProf].words = a.mainWords - w0; nProf++; }
		n++;
		if (end) break;
		if (n >= MAX_INSNS || !((pa + n * 4) & 0xFFF) || defFull ())
		{
			crPendFlush ();					// (a compare last: its branch in the next block)
			w0 = a.mainWords;
			exitTo (pc + n * 4, n * 2, false);
			if (b.prof != ~0u) { profInsns[nProf].op = 0; profInsns[nProf].words = a.mainWords - w0; nProf++; }
			break;
		}
	}
	if (b.prof != ~0u) b.profN = nProf - b.prof;
	emitDefs ();
	flushCode (start, a.p);
	b.words = a.mainWords; b.insns = n; b.size = (u32) (a.p - start);	// (the main code; the cold paths: at the chunk's end)
	b.key = key; b.pa = pa; b.code = start;
	b.hashNext = hashHead[hashOf (key)]; hashHead[hashOf (key)] = i;
	b.pageNext = pageHead[pa >> 12]; pageHead[pa >> 12] = i;
	Fast &f = fast[(key >> 2) & ((1u << FAST_BITS) - 1)];
	f.key = key; f.code = start;
	linkTo (key, start);						// (the exits waiting for it, its own loop)
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
		if (gxIrqBits) gxIrqTake ();				// (the GX's core: its token / finish)
		if (jitFlush) { j.flushAll (); jitFlush = false; }
		if (cycles >= decAt) { decAt = ~0ull; dec = 0xFFFFFFFF; decPending = true; }
		if ((extIrq || decPending) && (msr & MSR_EE)) checkInterrupts ();
		jitUntil = until < decAt ? until : decAt;
		u32 key = pc | ((msr & MSR_IR) ? 1 : 0) | ((msr & MSR_DR) ? 2 : 0);
		void *c = j.lookup (key);
		if (!c) c = j.compile (pc, key);
		if (!c) { step (); continue; }				// (not in MEM1: the interpreter, its exception)
		if (jitProfile) jitEnters++;
		j.enter (this, c, j.ctx);
		// a polling loop skipped to the next event: with the GX on its own core, what it has
		// still to draw is done first (the game waits for it: it must not see a slower GPU)
		if (idleHit) { idleHit = 0; if (gxAsync) gxSync (); }
	}
}

void Machine::jitInvalidate (u32 pa, u32 len) { if (jit) jit->invalidate (pa, len); }

// (the test's profile) the blocks' runs, the host instructions of their main paths, the guest ones
void Machine::jitStats (u64 &runs, u64 &hostInsns, u64 &guestInsns)
{
	runs = hostInsns = guestInsns = 0;
	if (!jit) return;
	for (u32 i = 0; i < jit->nBlocks; i++)
	{
		const Jit::Block &b = jit->blocks[i];
		runs += b.runs; hostInsns += b.runs * b.words; guestInsns += b.runs * b.insns;
	}
}

// (the tests) translate the block at pc now; a block's instructions with their main code's words
bool Machine::jitCompileAt (u32 pc)
{
	if (!jit) return false;
	u32 key = pc | ((msr & MSR_IR) ? 1 : 0) | ((msr & MSR_DR) ? 2 : 0);
	return jit->lookup (key) || jit->compile (pc, key);
}
int Machine::jitBlockInsns (u32 pc, u32 *ops, u32 *words, int max)
{
	if (!jit || !jit->profInsns) return 0;
	for (u32 i = jit->nBlocks; i-- > 0;)
	{
		const Jit::Block &b = jit->blocks[i];
		if ((b.key & ~3u) != pc || b.prof == ~0u) continue;
		int n = 0;
		for (u32 k = b.prof; k < b.prof + b.profN && n < max; k++, n++) { ops[n] = jit->profInsns[k].op; words[n] = jit->profInsns[k].words; }
		return n;
	}
	return 0;
}

// (the tests) the block translated for pc (the last one), its code
bool Machine::jitCode (u32 pc, const u32 *&code, u32 &words)
{
	if (!jit) return false;
	for (u32 i = jit->nBlocks; i-- > 0;)
		if ((jit->blocks[i].key & ~3u) == pc) { code = (const u32 *) jit->blocks[i].code; words = jit->blocks[i].size; return true; }
	return false;
}

// (the test's profile) the n-th most run block: its address, runs, code -> false: none
bool Machine::jitHot (int n, u32 &pc, u64 &runs, const u32 *&code, u32 &words)
{
	if (!jit) return false;
	u64 last = ~0ull; u32 lastI = ~0u;
	for (int k = 0; k <= n; k++)
	{
		u64 best = 0; u32 bi = ~0u;
		for (u32 i = 0; i < jit->nBlocks; i++)
		{
			const Jit::Block &b = jit->blocks[i];
			if ((b.runs < last || (b.runs == last && i > lastI)) && (b.runs > best || bi == ~0u)) { best = b.runs; bi = i; }
		}
		if (bi == ~0u) return false;
		last = best; lastI = bi;
	}
	const Jit::Block &b = jit->blocks[lastI];
	pc = b.key & ~3u; runs = b.runs; code = (const u32 *) b.code; words = b.size;
	return true;
}

// (gcemu --jitprof) the n costliest blocks (runs x host instructions of the main path), best first
static int costliest (const Jit &j, int n, u32 *idx)
{
	int k = 0;
	for (u32 i = 0; i < j.nBlocks; i++)
	{
		u64 c = j.blocks[i].runs * j.blocks[i].words;
		if (!c) continue;
		int at = k < n ? k++ : n;
		if (at == n) { if (c <= j.blocks[idx[n - 1]].runs * j.blocks[idx[n - 1]].words) continue; at = n - 1; }
		while (at > 0 && j.blocks[idx[at - 1]].runs * j.blocks[idx[at - 1]].words < c) { idx[at] = idx[at - 1]; at--; }
		idx[at] = i;
	}
	return k;
}

// The profile as text: the totals, the costliest blocks, the instructions left to the interpreter,
// the accesses off the fast path, the entries from C
int Machine::jitReport (char *out, int cap, int nTop)
{
	static const char HX[] = "0123456789ABCDEF";
	int n = 0;
	auto put = [&] (const char *t) { while (*t && n < cap - 1) out[n++] = *t++; };
	auto hex = [&] (u32 v) { for (int i = 7; i >= 0 && n < cap - 1; i--) out[n++] = HX[(v >> (i * 4)) & 15]; };
	auto dec = [&] (u64 v) { char t[24]; int k = 0; do { t[k++] = (char) ('0' + v % 10); v /= 10; } while (v); while (k && n < cap - 1) out[n++] = t[--k]; };
	auto pct = [&] (u64 a, u64 b) { u64 p = b ? a * 1000 / b : 0; dec (p / 10); put ("."); dec (p % 10); put (" %"); };
	if (!jit) { put ("no JIT\n"); out[n] = 0; return n; }
	Jit &j = *jit;
	u64 runs, host, guest; jitStats (runs, host, guest);
	put ("JIT profile: "); dec (runs); put (" block runs, "); dec (guest); put (" guest instructions, ");
	dec (host); put (" host instructions on the main paths ("); dec (guest ? host * 100 / guest : 0); put (" / 100 guest), ");
	dec (jitEnters); put (" entries from C, "); dec (jitCompiles); put (" translations, "); dec (j.nBlocks); put (" blocks now\n");
	static u32 idx[256];
	if (nTop > 256) nTop = 256;
	int k = costliest (j, nTop, idx);
	put ("the costliest blocks (runs x host instructions): pc, runs, guest / host instructions, share\n");
	u64 cum = 0;
	for (int i = 0; i < k; i++)
	{
		const Jit::Block &b = j.blocks[idx[i]];
		u64 c = b.runs * b.words; cum += c;
		put ("  "); hex (b.key & ~3u); put ("  "); dec (b.runs); put ("  "); dec (b.insns); put (" / "); dec (b.words);
		put ("  "); pct (c, host); put ("  (sum "); pct (cum, host); put (")\n");
	}
	u64 *cls = j.profInsns ? new u64[2 * 65536] : 0;			// the host instructions run, by guest opcode
	if (cls)
	{
		u64 *cnt = cls + 65536;						// (primary << 10 | the extended one's 10 bits)
		__builtin_memset (cls, 0, 2 * 65536 * sizeof (u64));
		for (u32 i = 0; i < j.nBlocks; i++)
		{
			const Jit::Block &b = j.blocks[i];
			if (b.prof == ~0u || !b.runs) continue;
			for (u32 k = b.prof; k < b.prof + b.profN; k++)
			{
				u32 op = j.profInsns[k].op, p = op >> 26;
				u32 c = p == 4 || p == 19 || p == 31 || p == 59 || p == 63 ? (p << 10 | ((op >> 1) & 0x3FF)) : p << 10;
				cls[c] += b.runs * j.profInsns[k].words; cnt[c] += b.runs;
			}
		}
		put ("the host instructions run by guest instruction (primary / extended: guest runs, host instructions, a guest one's, share; 0 / 0: the blocks' fall-through exits):\n");
		for (int t = 0; t < 45; t++)
		{
			u32 best = 65536; u64 bv = 0;
			for (u32 c = 0; c < 65536; c++) if (cls[c] > bv) { bv = cls[c]; best = c; }
			if (best == 65536) break;
			put ("  "); dec (best >> 10); put (" / "); dec (best & 0x3FF); put (":  ");
			dec (cnt[best]); put ("  "); dec (cls[best]); put ("  "); dec (cnt[best] ? cls[best] / cnt[best] : 0);
			put ("."); dec (cnt[best] ? cls[best] * 10 / cnt[best] % 10 : 0); put ("  "); pct (cls[best], host); put ("\n");
			cls[best] = 0;
		}
		delete [] cls;
	}
	if (jitInterpOps)
	{
		put ("left to the interpreter (primary opcode / extended: count):\n");
		static u32 top[32]; int nt = 0;
		for (u32 i = 0; i < 65536; i++)
		{
			if (!jitInterpOps[i]) continue;
			int at = nt < 32 ? nt++ : 32;
			if (at == 32) { if (jitInterpOps[i] <= jitInterpOps[top[31]]) continue; at = 31; }
			while (at > 0 && jitInterpOps[top[at - 1]] < jitInterpOps[i]) { top[at] = top[at - 1]; at--; }
			top[at] = i;
		}
		for (int i = 0; i < nt; i++) { put ("  "); dec (top[i] >> 10); put (" / "); dec (top[i] & 0x3FF); put (": "); dec (jitInterpOps[top[i]]); put ("\n"); }
	}
	put ("off the fast path (by the address's top 4 bits):\n  loads:");
	for (int s = 0; s < 2; s++)
	{
		if (s) put ("\n  stores:");
		for (int i = 0; i < 16; i++) if (jitSlowMem[s][i]) { put (" "); hex ((u32) i << 28); put (" "); dec (jitSlowMem[s][i]); }
	}
	put ("\n");
	out[n] = 0;
	return n;
}

// The costliest blocks' code, for a PC to disassemble: a block = u32 pc, u32 runs (saturated), u32
// guest words, u32 host words, then its guest words (big-endian, as in MEM1) and its host code
int Machine::jitHotCode (u8 *out, int cap, int nTop)
{
	if (!jit) return 0;
	Jit &j = *jit;
	static u32 idx[256];
	if (nTop > 256) nTop = 256;
	int k = costliest (j, nTop, idx), n = 0;
	for (int i = 0; i < k; i++)
	{
		const Jit::Block &b = j.blocks[idx[i]];
		u32 h[4] = { b.key & ~3u, b.runs > 0xFFFFFFFFull ? 0xFFFFFFFFu : (u32) b.runs, b.insns, b.size };
		u32 need = 16 + b.insns * 4 + b.size * 4;
		if (n + (int) need > cap) break;
		__builtin_memcpy (out + n, h, 16); n += 16;
		__builtin_memcpy (out + n, mem1 + b.pa, b.insns * 4); n += (int) b.insns * 4;
		__builtin_memcpy (out + n, b.code, b.size * 4); n += (int) b.size * 4;
	}
	return n;
}

#elif !defined(__x86_64__)	// neither AArch64 nor x86-64 (gc_jit_x64.cpp): the interpreter only

struct Jit {};
bool Machine::jitEnable () { return false; }
void Machine::jitRun (u64 until) { jit = 0; run (until); }
void Machine::jitInvalidate (u32, u32) {}
void Machine::jitStats (u64 &runs, u64 &hostInsns, u64 &guestInsns) { runs = hostInsns = guestInsns = 0; }
bool Machine::jitHot (int, u32 &, u64 &, const u32 *&, u32 &) { return false; }
bool Machine::jitCode (u32, const u32 *&, u32 &) { return false; }
int Machine::jitReport (char *out, int cap, int) { if (cap > 0) out[0] = 0; return 0; }
int Machine::jitHotCode (u8 *, int, int) { return 0; }
bool Machine::jitCompileAt (u32) { return false; }
int Machine::jitBlockInsns (u32, u32 *, u32 *, int) { return 0; }

#endif

} // namespace gc
