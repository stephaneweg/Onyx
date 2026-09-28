//
// gc/gc_jit_x64.cpp -- the Gekko's JIT for x86-64 hosts (NintendoEMU on Windows; the tests on a PC):
// the PowerPC code translated to x86-64 a block at a time, as gc_jit.cpp does for AArch64 -- the
// same blocks (from an address to its branch, at most 64 instructions, within a 4 KB page), the
// dispatcher and its direct-mapped table, the links between blocks, the cold paths after each
// block, the polling loops skipped to the next event, the code dropped where it is written (see
// there). What differs:
//
//   * r15 = the Machine, r14 = MEM1, r13 = the cycles counting down to jitEnd; the guest registers
//     a block uses (the GPRs, CR, XER, LR, CTR) cached in rbx, rbp, rsi, rdi, r12, r10, r11 (loaded
//     when first read, written back at the exits and before the interpreter); rax, rcx (an
//     address), rdx (a value to store), r8, r9 the temporaries; a few slots on the stack
//   * the FPRs cached in xmm6..xmm15, both halves (ps0 the low double, ps1 the high one), xmm0..xmm4
//     the temporaries, xmm5 FPRF's; the fused multiply-adds with FMA3 when the CPU has it (else the
//     interpreter does them)
//   * the helpers take the Machine only (their other arguments in its jitArg): the same code for
//     the Windows and the System V calling conventions
//
#include "gc/gc.h"
#ifdef GC_TRACE
#include <stdio.h>
#endif

namespace gc {

#if defined(__x86_64__)

enum { MSR_EE = 0x8000, MSR_IR = 0x20, MSR_DR = 0x10 };
enum
{
	CODE_SIZE = 32 << 20, MAX_BLOCKS = 1 << 17, HASH_BITS = 16, FAST_BITS = 16,
	MAX_INSNS = 64, NPAGES = MEM1_SIZE >> 12, CONST_OFF = 4096, STUB_BYTES = 8192
};
static const u32 NONE = 0xFFFFFFFFu, NOKEY = 0xFFFFFFFFu;

static inline u32 mask (int mb, int me)
{
	u32 a = 0xFFFFFFFFu >> mb, b = 0xFFFFFFFFu << (31 - me);
	return mb <= me ? a & b : a | b;
}
// 2^s6 (a GQR's scale: 6 bits, signed)
static double qmul (int s6) { s6 &= 63; if (s6 & 0x20) s6 -= 64; double m = 1.0; while (s6 > 0) { m *= 2.0; s6--; } while (s6 < 0) { m *= 0.5; s6++; } return m; }
static u64 dbits (double v) { u64 u; __builtin_memcpy (&u, &v, 8); return u; }

// ---- the x86-64 assembler (what the translation uses) ------------------------------------------------------
enum { RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI, R8, R9, R10, R11, R12, R13, R14, R15 };
enum { XM = R15, XMEM = R14, XDC = R13 };
#ifdef _WIN32
enum { ARG0 = RCX, ARG1 = RDX };
#else
enum { ARG0 = RDI, ARG1 = RSI };
#endif
// the stack frame (rsp 16-byte aligned in the blocks): the callees' shadow space, xmm6..15 saved, slots
enum { FRAME = 216, SLOT_EA = 192, SLOT_A = 200, SLOT_B = 208 };
enum { CC_O, CC_NO, CC_B, CC_AE, CC_E, CC_NE, CC_BE, CC_A, CC_S, CC_NS, CC_P, CC_NP, CC_L, CC_GE, CC_LE, CC_G };
enum { OP_ADD, OP_OR, OP_ADC, OP_SBB, OP_AND, OP_SUB, OP_XOR, OP_CMP };	// (group 1: /digit, and op r, r/m = 03 + 8 x)
enum { SH_ROL = 0, SH_ROR = 1, SH_SHL = 4, SH_SHR = 5, SH_SAR = 7 };
enum { UN_NOT = 2, UN_NEG = 3, UN_MUL = 4, UN_IMUL = 5, UN_DIV = 6, UN_IDIV = 7 };

struct Asm
{
	u8 *p;
	int side; u32 mainBytes;			// (the profile: bytes put off the side paths)
	void b (u32 v) { *p++ = (u8) v; if (!side) mainBytes++; }
	void d32 (u32 v) { b (v); b (v >> 8); b (v >> 16); b (v >> 24); }
	void d64 (u64 v) { d32 ((u32) v); d32 ((u32) (v >> 32)); }
	// REX: w a 64-bit operation; r, x, rm: the registers in ModRM.reg, SIB.index, ModRM.rm (or the SIB
	// base); force: a byte register spl..dil
	void rex (bool w, int r, int x, int rm, bool force = false)
	{
		u32 v = 0x40 | (w ? 8 : 0) | (r & 8 ? 4 : 0) | (x & 8 ? 2 : 0) | (rm & 8 ? 1 : 0);
		if (v != 0x40 || force) b (v);
	}
	void opc (u32 op) { if (op > 0xFFFF) b (op >> 16); if (op > 0xFF) b (op >> 8); b (op); }
	void modrr (int r, int rm) { b (0xC0 | (u32) (r & 7) << 3 | (u32) (rm & 7)); }
	void modm (int r, int base, s32 disp)			// [base + disp]
	{
		int bs = base & 7, mod = disp == 0 && bs != 5 ? 0 : disp >= -128 && disp < 128 ? 1 : 2;
		b ((u32) (mod << 6 | (r & 7) << 3 | bs));
		if (bs == 4) b (0x24);
		if (mod == 1) b ((u32) disp); else if (mod == 2) d32 ((u32) disp);
	}
	void modx (int r, int base, int index, s32 disp)		// [base + index + disp]
	{
		int bs = base & 7, mod = disp == 0 && bs != 5 ? 0 : disp >= -128 && disp < 128 ? 1 : 2;
		b ((u32) (mod << 6 | (r & 7) << 3 | 4));
		b ((u32) ((index & 7) << 3 | bs));
		if (mod == 1) b ((u32) disp); else if (mod == 2) d32 ((u32) disp);
	}
	// op reg, rm / op reg, [base + disp] / op reg, [base + index + disp]
	void rr (u32 op, int r, int rm, bool w = false, bool force = false) { rex (w, r, 0, rm, force); opc (op); modrr (r, rm); }
	void rm (u32 op, int r, int base, s32 disp, bool w = false, bool force = false) { rex (w, r, 0, base, force); opc (op); modm (r, base, disp); }
	void rx (u32 op, int r, int base, int index, s32 disp = 0, bool w = false, bool force = false) { rex (w, r, index, base, force); opc (op); modx (r, base, index, disp); }

	void mov (int d, int s, bool w = false) { if (d != s) rr (0x8B, d, s, w); }
	void movi (int d, u32 v) { rex (false, 0, 0, d); b (0xB8 + (u32) (d & 7)); d32 (v); }	// (the flags kept)
	void movq (int d, u64 v)
	{
		if (v <= 0xFFFFFFFFu) { movi (d, (u32) v); return; }
		if ((s64) v == (s64) (s32) v) { rex (true, 0, 0, d); b (0xC7); modrr (0, d); d32 ((u32) v); return; }
		rex (true, 0, 0, d); b (0xB8 + (u32) (d & 7)); d64 (v);
	}
	void ld (int d, int base, s32 disp, bool w = false) { rm (0x8B, d, base, disp, w); }
	void st (int base, s32 disp, int s, bool w = false) { rm (0x89, s, base, disp, w); }
	void sti (int base, s32 disp, u32 v) { rex (false, 0, 0, base); b (0xC7); modm (0, base, disp); d32 (v); }	// dword [m] = v
	void stbi (int base, s32 disp, u32 v) { rex (false, 0, 0, base); b (0xC6); modm (0, base, disp); b (v); }	// byte [m] = v
	void alu (int o, int d, int s, bool w = false) { rr (0x03 + (u32) o * 8, d, s, w); }		// d = d o s
	void alui (int o, int d, s32 v, bool w = false)
	{
		rex (w, 0, 0, d);
		if (v >= -128 && v < 128) { b (0x83); modrr (o, d); b ((u32) v); }
		else { b (0x81); modrr (o, d); d32 ((u32) v); }
	}
	void test (int x, int y, bool w = false) { rr (0x85, y, x, w); }
	void testi (int x, u32 v) { rex (false, 0, 0, x); b (0xF7); modrr (0, x); d32 (v); }
	void un (int digit, int r, bool w = false) { rex (w, 0, 0, r); b (0xF7); modrr (digit, r); }
	void imul (int d, int s, bool w = false) { rr (0x0FAF, d, s, w); }
	void imuli (int d, int s, s32 v) { rr (0x69, d, s); d32 ((u32) v); }
	void shi (int digit, int r, int n, bool w = false) { n &= w ? 63 : 31; if (!n) return; rex (w, 0, 0, r); b (0xC1); modrr (digit, r); b ((u32) n); }
	void shcl (int digit, int r, bool w = false) { rex (w, 0, 0, r); b (0xD3); modrr (digit, r); }
	void bswap (int r, bool w = false) { rex (w, 0, 0, r); b (0x0F); b (0xC8 + (u32) (r & 7)); }
	void swap16 (int r) { b (0x66); rex (false, 0, 0, r); b (0xC1); modrr (SH_ROL, r); b (8); }	// (rol r16, 8)
	static bool low8 (int r) { return r >= 4 && r < 8; }		// (spl..dil: a REX)
	void movzx8 (int d, int s) { rr (0x0FB6, d, s, false, low8 (s)); }
	void movsx8 (int d, int s) { rr (0x0FBE, d, s, false, low8 (s)); }
	void movzx16 (int d, int s) { rr (0x0FB7, d, s); }
	void movsx16 (int d, int s) { rr (0x0FBF, d, s); }
	void movsxd (int d, int s) { rr (0x63, d, s, true); }
	void setcc (int cc, int r) { rex (false, 0, 0, r, low8 (r)); b (0x0F); b (0x90 + (u32) cc); modrr (0, r); }
	void cmov (int cc, int d, int s, bool w = false) { rr (0x0F40 + (u32) cc, d, s, w); }
	void bsr (int d, int s) { rr (0x0FBD, d, s); }
	void bti (int r, int bit, bool w = false) { rex (w, 0, 0, r); b (0x0F); b (0xBA); modrr (4, r); b ((u32) bit); }
	void cmc () { b (0xF5); }
	void cdq () { b (0x99); }
	void lea (int d, int base, s32 disp, bool w = false) { rm (0x8D, d, base, disp, w); }
	void leax (int d, int base, int index, bool w = false) { rx (0x8D, d, base, index, 0, w); }
	// branches (to a known place; a forward one: its rel32 patched)
	u8 *jcc (int cc) { b (0x0F); b (0x80 + (u32) cc); u8 *f = p; d32 (0); return f; }
	u8 *jmp () { b (0xE9); u8 *f = p; d32 (0); return f; }
	static void patch (u8 *f, const void *to) { s32 rel = (s32) ((const u8 *) to - (f + 4)); __builtin_memcpy (f, &rel, 4); }
	void jccTo (int cc, const void *to) { patch (jcc (cc), to); }
	void jmpTo (const void *to) { patch (jmp (), to); }
	void jmpr (int r) { rex (false, 0, 0, r); b (0xFF); modrr (4, r); }
	void callr (int r) { rex (false, 0, 0, r); b (0xFF); modrr (2, r); }
	void push (int r) { rex (false, 0, 0, r); b (0x50 + (u32) (r & 7)); }
	void pop (int r) { rex (false, 0, 0, r); b (0x58 + (u32) (r & 7)); }
	void ret () { b (0xC3); }
	// SSE (a prefix 0x66 / 0xF2 / 0xF3 or 0, then 0F op), on xmm0..15
	void sse (u32 pfx, u32 op, int r, int rm_, bool w = false) { if (pfx) b (pfx); rex (w, r, 0, rm_); b (0x0F); opc (op); modrr (r, rm_); }
	void ssem (u32 pfx, u32 op, int r, int base, s32 disp, bool w = false) { if (pfx) b (pfx); rex (w, r, 0, base); b (0x0F); opc (op); modm (r, base, disp); }
	void ssex (u32 pfx, u32 op, int r, int base, int index, s32 disp = 0) { if (pfx) b (pfx); rex (false, r, index, base); b (0x0F); opc (op); modx (r, base, index, disp); }
	// op xmm, [rip + to] (the constants, in the code buffer)
	void ssemRip (u32 pfx, u32 op, int r, const void *to)
	{
		if (pfx) b (pfx);
		rex (false, r, 0, 0); b (0x0F); opc (op);
		b ((u32) ((r & 7) << 3 | 5));
		s32 rel = (s32) ((const u8 *) to - (p + 4)); d32 ((u32) rel);
	}
	void cmpbi (int base, s32 disp, u32 v) { rex (false, 0, 0, base); b (0x80); modm (7, base, disp); b (v); }	// cmp byte [m], v
	// FMA3 (VEX.128.66.0F38.W1, the "231" forms): x1 = x2 * x3 op x1
	void fma (u32 op, int x1, int x2, int x3)
	{
		b (0xC4);
		b ((x1 & 8 ? 0u : 0x80u) | 0x40 | (x3 & 8 ? 0u : 0x20u) | 0x02);
		b (0x80 | (u32) (~x2 & 15) << 3 | 0x01);
		b (op); modrr (x1, x3);
	}
	// the SSE2 used by the floating point
	void movapd (int d, int s) { if (d != s) sse (0x66, 0x28, d, s); }
	void movsd (int d, int s) { if (d != s) sse (0xF2, 0x10, d, s); }	// (the low half; the high one kept)
	void sd (u32 op, int d, int s) { sse (0xF2, op, d, s); }		// a scalar double operation
	void pd (u32 op, int d, int s) { sse (0x66, op, d, s); }		// a packed one (both halves)
	void movqxr (int x, int r) { sse (0x66, 0x6E, x, r, true); }	// xmm = r64
	void movqrx (int r, int x) { sse (0x66, 0x7E, x, r, true); }	// r64 = xmm's low half
	void movd (int x, int r) { sse (0x66, 0x6E, x, r); }		// xmm = r32
	void ucomisd (int x, int y) { sse (0x66, 0x2E, x, y); }
	void pshufd (int d, int s, u32 imm) { sse (0x66, 0x70, d, s); b (imm); }
	void shufpd (int d, int s, u32 imm) { sse (0x66, 0xC6, d, s); b (imm); }
	void cmpsd (int d, int s, u32 pred) { sse (0xF2, 0xC2, d, s); b (pred); }
	void cmppd (int d, int s, u32 pred) { sse (0x66, 0xC2, d, s); b (pred); }
	void movmskpd (int r, int x) { sse (0x66, 0x50, r, x); }
	void cvttsd2si (int r, int x) { sse (0xF2, 0x2C, r, x); }
	void cvtsi2sd (int x, int r) { sse (0xF2, 0x2A, x, r); }
	void cvttpd2dq (int d, int s) { sse (0x66, 0xE6, d, s); }
};

// the SSE operations used (after their prefix: the second opcode byte)
enum : u32
{
	S_MOVUPD_L = 0x10, S_MOVUPD_S = 0x11, S_UNPCKL = 0x14, S_UNPCKH = 0x15, S_ANDPD = 0x54, S_ANDNPD = 0x55,
	S_ORPD = 0x56, S_XORPD = 0x57, S_ADD = 0x58, S_MUL = 0x59, S_CVT = 0x5A, S_SUB = 0x5C, S_DIV = 0x5E,
	S_SQRT = 0x51, S_MOVAPD = 0x28, S_UCOMI = 0x2E, S_CMP = 0xC2, S_SHUF = 0xC6, S_PSHUFD = 0x70,
	S_MOVQ_X = 0x6E, S_MOVQ_R = 0x7E, S_CVTT2SI = 0x2C, S_CVTSI2 = 0x2A, S_MAXSD = 0x5F, S_MINSD = 0x5D
};
// FMA3 (231): a * b + c, a * b - c
enum : u32 { F_MADD_SD = 0xB9, F_MADD_PD = 0xB8, F_MSUB_SD = 0xBB, F_MSUB_PD = 0xBA };

// ---- the JIT ----------------------------------------------------------------------------------------------------
struct Jit
{
	struct Block { u32 key, pa, hashNext, pageNext; u8 *code; u64 runs; u32 bytes, insns, size; };
	struct Fast { u32 key, pad; u8 *code; };

	Machine *m;
	Asm a;
	u8 *code, *codeEnd, *codeStart;
	u8 *dispatch, *exitStub;
	void (*enter) (Machine *, void *);
	Fast *fast;
	Block *blocks; u32 nBlocks;
	u32 hashHead[1 << HASH_BITS];
	u32 pageHead[NPAGES];
	// the links: a block's exit to a known address jumps straight to that block once it is
	// translated (the jmp at site patched; back to its stub when that block is dropped)
	struct Link { u32 key, next; u8 *site, *stub; };
	Link *links; u32 nLinks;
	u32 linkHead[1 << HASH_BITS];
	void addLink (u32 key, u8 *site, u8 *stub);
	void linkTo (u32 key, u8 *to);
	bool stdMap;					// the BATs map 0x80000000 / 0xC0000000 onto MEM1 as the OS does
	bool hasFma;					// (the CPU's FMA3)
	// the Machine's fields
	int oPc, oCycles, oUntil, oEnd, oTb, oCr, oXer, oLr, oCtr, oMsr, oMem1, oScratch, oPs, oFpscr, oFprfVal, oFprfPend, oGqr, oGatherN, oGather, oGpr, oArg;
	// the block being translated
	u32 bKey, synced; int dmode; bool fpOk;
	bool idle;					// the block is a polling loop (idleLoop)
	u32 sgl0, sgl1;					// the FPRs (ps0 / ps1) known to hold a single exactly: no force25
	static void setS (u32 &mk, int r, bool v) { if (v) mk |= 1u << r; else mk &= ~(1u << r); }
	static bool isS (u32 mk, int r) { return (mk >> r) & 1; }

	Jit (Machine *mm, void *mem, u32 size);
	void flushAll ();
	u8 *lookup (u32 key);
	u8 *compile (u32 pc, u32 key);
	void invalidate (u32 pa, u32 len);
	void unlink (u32 i);

	// ---- the helpers (the interpreter; the memory the fast path does not reach): their arguments
	// in m->jitArg; they return with bit 32 set (or 1 for hInterp) when an exception / a branch
	// happened -- pc is where to go
	static u64 hInterp (Machine *m)
	{
		u32 op = (u32) m->jitArg[0], pc = (u32) m->jitArg[1];
		m->curPc = pc; m->npc = pc + 4; m->memFault = false;
		m->exec (op);
		if (m->npc != pc + 4 || m->halted) { m->pc = m->npc; return 1; }
		return 0;
	}
	static void mArgs (Machine *m, u32 &ea) { ea = (u32) m->jitArg[0]; m->curPc = (u32) m->jitArg[1]; m->memFault = false; }
	static u64 hRd8 (Machine *m) { u32 ea; mArgs (m, ea); u32 v = m->read8 (ea); return m->memFault ? 1ull << 32 : v; }
	static u64 hRd16 (Machine *m) { u32 ea; mArgs (m, ea); u32 v = m->read16 (ea); return m->memFault ? 1ull << 32 : v; }
	static u64 hRd32 (Machine *m) { u32 ea; mArgs (m, ea); u32 v = m->read32 (ea); return m->memFault ? 1ull << 32 : v; }
	static u64 hRd64 (Machine *m) { u32 ea; mArgs (m, ea); m->jitScratch = m->read64 (ea); return m->memFault ? 1ull << 32 : 0; }
	static u64 hWr8 (Machine *m) { u32 ea; mArgs (m, ea); m->write8 (ea, (u8) m->jitArg[2]); return m->memFault ? 1ull << 32 : 0; }
	static u64 hWr16 (Machine *m) { u32 ea; mArgs (m, ea); m->write16 (ea, (u16) m->jitArg[2]); return m->memFault ? 1ull << 32 : 0; }
	static u64 hWr32 (Machine *m) { u32 ea; mArgs (m, ea); m->write32 (ea, (u32) m->jitArg[2]); return m->memFault ? 1ull << 32 : 0; }
	static u64 hWr64 (Machine *m) { u32 ea; mArgs (m, ea); m->write64 (ea, m->jitArg[2]); return m->memFault ? 1ull << 32 : 0; }
	static u64 hCvtD (Machine *m) { return cvtToDouble ((u32) m->jitArg[0]); }
	static u64 hCvtS (Machine *m) { return cvtToSingle (m->jitArg[0]); }
	static u64 hFprf (Machine *m) { if (m->fprfPending) { m->fprfPending = false; m->setFprf (m->fprfVal); } return 0; }
	static u64 hGather (Machine *m) { m->gatherFlush (); return 0; }
	typedef u64 (*Helper) (Machine *);
	void call (Helper fn) { a.mov (ARG0, XM, true); a.movq (RAX, (u64) (void *) fn); a.callr (RAX); }

	// ---- the guest registers in host registers (a cache for the block) ----
	// Guest 0..31 = the GPRs, then CR, XER, LR, CTR: loaded when first used, written back (the dirty
	// ones) at the block's exits and before a helper that reads them; a helper that does not touch
	// them (off the main path: the memory, the conversions) saves / reloads the caller-saved ones
	// around its call.
	enum { G_CR = 32, G_XER, G_LR, G_CTR, NG };
	int gHost[NG]; bool gDirty[NG]; int hGuest[16]; u32 hStamp[16], stamp, pinned;
	int gOff (int g) { return g < 32 ? oGpr + g * 4 : g == G_CR ? oCr : g == G_XER ? oXer : g == G_LR ? oLr : oCtr; }
#ifdef _WIN32
	static bool calleeSaved (int h) { return h == RBX || h == RBP || h == RSI || h == RDI || h == R12; }
#else
	static bool calleeSaved (int h) { return h == RBX || h == RBP || h == R12; }
#endif
	void cacheReset ()
	{
		for (int g = 0; g < NG; g++) { gHost[g] = -1; gDirty[g] = false; }
		for (int h = 0; h < 16; h++) { hGuest[h] = -1; hStamp[h] = 0; hfGuest[h] = -1; hfStamp[h] = 0; }
		stamp = 0; pinned = 0;
		for (int r = 0; r < 32; r++) { fHost[r] = -1; fDirty[r] = false; }
		fpinned = 0; fprfR = -1;
	}
	int alloc ()
	{
		static const int pool[7] = { RBX, RBP, RSI, RDI, R12, R10, R11 };
		int best = -1; u32 bs = ~0u;
		for (int i = 0; i < 7; i++)
		{
			int h = pool[i];
			if (pinned & (1u << h)) continue;
			if (hGuest[h] < 0) { best = h; break; }
			if (hStamp[h] < bs) { bs = hStamp[h]; best = h; }
		}
		int g = hGuest[best];
		if (g >= 0)						// (the least recently used goes)
		{
			if (gDirty[g]) a.st (XM, gOff (g), best);
			gHost[g] = -1; gDirty[g] = false; hGuest[best] = -1;
		}
		return best;
	}
	int G (int g)							// guest g's value (read)
	{
		int h = gHost[g];
		if (h < 0) { h = alloc (); a.ld (h, XM, gOff (g)); gHost[g] = h; hGuest[h] = g; gDirty[g] = false; }
		hStamp[h] = ++stamp; pinned |= 1u << h;
		return h;
	}
	int W (int g)							// guest g's register, to write whole
	{
		int h = gHost[g];
		if (h < 0) { h = alloc (); gHost[g] = h; hGuest[h] = g; }
		gDirty[g] = true; hStamp[h] = ++stamp; pinned |= 1u << h;
		return h;
	}
	int RW (int g) { int h = G (g); gDirty[g] = true; return h; }	// read, then modified
	// the dirty ones to memory (the state stays): all (+ the pending FPRF: before the exits, the
	// interpreter) or those a call clobbers (the caller-saved GPRs, the FPRs)
	void spill (bool all)
	{
		for (int g = 0; g < NG; g++)
			if (gHost[g] >= 0 && gDirty[g] && (all || !calleeSaved (gHost[g]))) a.st (XM, gOff (g), gHost[g]);
		for (int r = 0; r < 32; r++)
			if (fHost[r] >= 0 && fDirty[r]) a.ssem (0x66, S_MOVUPD_S, fHost[r], XM, fOff (r));
		if (all) fprfFlush ();
	}
	void reload (bool all)
	{
		for (int g = 0; g < NG; g++)
			if (gHost[g] >= 0 && (all || !calleeSaved (gHost[g]))) a.ld (gHost[g], XM, gOff (g));
		for (int r = 0; r < 32; r++)
			if (fHost[r] >= 0) a.ssem (0x66, S_MOVUPD_L, fHost[r], XM, fOff (r));
	}
	void dropAll ()
	{
		for (int g = 0; g < NG; g++) { gHost[g] = -1; gDirty[g] = false; }
		for (int h = 0; h < 16; h++) { hGuest[h] = -1; hfGuest[h] = -1; }
		for (int r = 0; r < 32; r++) { fHost[r] = -1; fDirty[r] = false; }
		fprfR = -1;
	}

	// ---- the FPRs in xmm registers (a cache for the block too): an xmm holds both halves (ps0 the
	// low double, ps1 the high one); the pool xmm6..15, treated as caller-saved; xmm0..4 the
	// temporaries. FPRF is set lazily: fprfR's half fprfLane is its source, written to fprfVal
	// (+ fprfPending) before the FPR changes otherwise, at the exits, before the interpreter.
	int fHost[32]; bool fDirty[32]; int hfGuest[16]; u32 hfStamp[16]; u32 fpinned;
	int fprfR, fprfLane;
	int fOff (int r) { return oPs + 16 * r; }
	int falloc ()
	{
		int best = -1; u32 bs = ~0u;
		for (int h = 6; h < 16; h++)
		{
			if (fpinned & (1u << h)) continue;
			if (hfGuest[h] < 0) { best = h; break; }
			if (hfStamp[h] < bs) { bs = hfStamp[h]; best = h; }
		}
		int g = hfGuest[best];
		if (g >= 0)
		{
			if (fDirty[g]) a.ssem (0x66, S_MOVUPD_S, best, XM, fOff (g));
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
			if (load) a.ssem (0x66, S_MOVUPD_L, h, XM, fOff (r));
		}
		hfStamp[h] = ++stamp; fpinned |= 1u << h;
		return h;
	}
	int FG (int r) { return FRes (r, true, true); }
	void fSet (int r) { fDirty[r] = true; }
	void fprfSet (int r, int lane) { fprfR = r; fprfLane = lane; }
	void fprfFlush ()
	{
		if (fprfR < 0) return;
		int h = fHost[fprfR];
		if (h >= 0 && !fprfLane) a.ssem (0xF2, 0x11, h, XM, oFprfVal);	// (movsd [fprfVal], the low half)
		else
		{
			if (h >= 0) { a.sse (0x66, S_PSHUFD, 5, h); a.b (0xEE); }	// (the high half down)
			else a.ssem (0xF2, 0x10, 5, XM, fOff (fprfR) + 8 * fprfLane);
			a.ssem (0xF2, 0x11, 5, XM, oFprfVal);
		}
		a.stbi (XM, oFprfPend, 1);
	}

	// ---- the cold paths: emitted after the block's main code (the slow memory accesses, a NaN,
	// the conversions' rare cases, the interpreter leaving...), each with the cache's state and
	// the cycles as at its branch; they come back to the main code (ret) or leave the block
	struct St { int gH[NG], fH[32]; bool gD[NG], fD[32]; int fprfR, fprfLane; u32 synced; };
	enum { D_MEM, D_INTERP, D_IEXIT, D_CVTD, D_CVTS, D_FPRF };
	struct Def { int kind; bool store, back; int size, dm, r1, r2; u32 op, pc, idx; u8 *site, *ret, *ret2; St st; };
	enum { MAX_DEFS = 400 };
	Def defs[MAX_DEFS]; int nDefs;
	void saveSt (St &t)
	{
		for (int i = 0; i < NG; i++) { t.gH[i] = gHost[i]; t.gD[i] = gDirty[i]; }
		for (int i = 0; i < 32; i++) { t.fH[i] = fHost[i]; t.fD[i] = fDirty[i]; }
		t.fprfR = fprfR; t.fprfLane = fprfLane; t.synced = synced;
	}
	void loadSt (const St &t)
	{
		for (int i = 0; i < NG; i++) { gHost[i] = t.gH[i]; gDirty[i] = t.gD[i]; }
		for (int i = 0; i < 32; i++) { fHost[i] = t.fH[i]; fDirty[i] = t.fD[i]; }
		fprfR = t.fprfR; fprfLane = t.fprfLane; synced = t.synced;
	}
	Def &defer (int kind)					// (then its branch at d.site, and d.ret)
	{
		Def &d = defs[nDefs++];
		d.kind = kind; d.site = d.ret = d.ret2 = 0; d.back = true;
		saveSt (d.st);
		return d;
	}
	bool defFull () { return nDefs > MAX_DEFS - 8; }
	void emitDefs ();

	// ---- emitting ----
	void addConst (int rd, int rn, u32 c)			// rd = rn + c (the flags kept)
	{
		if (c == 0) a.mov (rd, rn);
		else a.lea (rd, rn, (s32) c);
	}
	void andImm (int r, u32 mk)
	{
		if (mk == 0xFFFFFFFFu) return;
		if (mk == 0xFFFF) a.movzx16 (r, r);
		else a.alui (OP_AND, r, (s32) mk);
	}
	// The cycles: r13 counts down to jitEnd (= jitUntil when set): the cycles run are jitEnd - r13,
	// written to m->cycles when leaving (exitStub) and before a helper that reads them.
	void dc (u32 n) { if (n) a.alui (OP_SUB, XDC, (s32) n, true); }	// n cycles run
	void uc (u32 n) { if (n) a.alui (OP_ADD, XDC, (s32) n, true); }
	void sync (u32 total) { if (total > synced) { dc (total - synced); synced = total; } }
	void cyclesOut () { a.ld (RAX, XM, oEnd, true); a.alu (OP_SUB, RAX, XDC, true); a.st (XM, oCycles, RAX, true); }
	void resync ()							// (after a helper: jitUntil may have changed; rax kept)
	{
		a.ld (R8, XM, oEnd, true); a.ld (R9, XM, oUntil, true);
		a.alu (OP_SUB, R8, R9, true); a.alu (OP_SUB, XDC, R8, true);
		a.st (XM, oEnd, R9, true);
	}
	void callKeep (Helper fn) { spill (false); call (fn); reload (false); }	// a helper that leaves the guest alone
	// leave the block: pc = eax; the registers written back; its cycles; to the next block
	// (dispatcher) or to jitRun
	void exitReg (u32 total, bool toC)
	{
		spill (true);
		a.st (XM, oPc, RAX);
		if (total > synced) dc (total - synced);
		if (toC) { a.jmpTo (exitStub); return; }
		a.movi (RDX, bKey & 3);
		a.jmpTo (dispatch);
	}
	// leave to a known address: while cycles remain straight to its block (a link), else to jitRun
	void exitTo (u32 target, u32 total, bool toC)
	{
		if (toC) { a.movi (RAX, target); exitReg (total, true); return; }
		spill (true);
		a.alui (OP_SUB, XDC, (s32) (total > synced ? total - synced : 0), true);
		u8 *late = a.jcc (CC_LE);
		u32 key = target | (bKey & 3);
		u8 *site = a.jmp ();
		u8 *stub = a.p;
		a.side++;
		a.sti (XM, oPc, target); a.jmpTo (exitStub);
		a.side--;
		Asm::patch (late, stub);
		u8 *to = lookup (key);
		Asm::patch (site, to ? to : stub);
		addLink (key, site, stub);
	}

	// CR field crf from the flags of a compare (signed / unsigned) + XER's SO
	void crFromFlags (int crf, bool sgn)
	{
		a.movi (RAX, 4); a.movi (R8, 8);
		a.cmov (sgn ? CC_L : CC_B, RAX, R8);
		a.movi (R8, 2);
		a.cmov (CC_E, RAX, R8);
		a.mov (R8, G (G_XER)); a.shi (SH_SHR, R8, 31); a.alu (OP_OR, RAX, R8);
		int sh = 28 - crf * 4, c = RW (G_CR);
		a.alui (OP_AND, c, (s32) ~(0xFu << sh));
		a.shi (SH_SHL, RAX, sh); a.alu (OP_OR, c, RAX);
	}
	void setCr0 (int r) { a.test (r, r); crFromFlags (0, true); }
	void setCaReg (int r)						// XER.CA = r (0 / 1; r is changed)
	{
		int x = RW (G_XER);
		a.alui (OP_AND, x, (s32) ~0x20000000u);
		a.shi (SH_SHL, r, 29); a.alu (OP_OR, x, r);
	}
	void setCa (int cc) { a.setcc (cc, R9); a.movzx8 (R9, R9); setCaReg (R9); }	// XER.CA = the condition (CC_B: a carry, CC_AE: no borrow)
	void caToCF (bool invert) { a.bti (G (G_XER), 29); if (invert) a.cmc (); }	// CF = XER.CA (or not)

	// the interpreter for one instruction (toC: then back to jitRun whatever it did)
	void interp (u32 op, u32 pc, u32 idx, bool toC)
	{
		sync (idx * 2);
		spill (true); dropAll ();
		sgl0 = sgl1 = 0;
		a.sti (XM, oArg, op); a.sti (XM, oArg + 8, pc);
		cyclesOut ();
		call (hInterp);
		resync ();
		Def &df = defer (D_IEXIT);
		a.test (RAX, RAX); df.site = a.jcc (CC_NE);		// (it left: an exception, a branch)
		if (toC) exitTo (pc + 4, (idx + 1) * 2, true);
	}

	// a load (ecx = the address -> eax / rax) or a store (ecx = the address, edx / rdx = the value);
	// size 1 / 2 / 4 / 8. MEM1 read / written here; the rest (the hardware, the pipe, a DSI) on the
	// cold path.
	void memop (bool store, int size, u32 pc, u32 idx)
	{
		Def &df = defer (D_MEM);
		df.store = store; df.size = size; df.pc = pc; df.idx = idx; df.dm = dmode;
		int ix = RCX;
		if (dmode == 0) { a.alui (OP_CMP, RCX, MEM1_SIZE); df.site = a.jcc (CC_AE); }
		else if (dmode == 1)					// (0x80000000 / 0xC0000000 + MEM1)
		{
			a.mov (RAX, RCX);
			a.alui (OP_AND, RAX, (s32) 0xBFFFFFFFu);
			a.alui (OP_XOR, RAX, (s32) 0x80000000u);
			a.alui (OP_CMP, RAX, MEM1_SIZE); df.site = a.jcc (CC_AE);
			ix = RAX;
		}
		else df.site = a.jmp ();
		if (dmode != 2)
		{
			if (!store)
			{
				if (size == 8) { a.rx (0x8B, RAX, XMEM, ix, 0, true); a.bswap (RAX, true); }
				else if (size == 4) { a.rx (0x8B, RAX, XMEM, ix); a.bswap (RAX); }
				else if (size == 2) { a.rx (0x0FB7, RAX, XMEM, ix); a.swap16 (RAX); }
				else a.rx (0x0FB6, RAX, XMEM, ix);
			}
			else
			{
				if (size == 8) { a.mov (R8, RDX, true); a.bswap (R8, true); a.rx (0x89, R8, XMEM, ix, 0, true); }
				else if (size == 4) { a.mov (R8, RDX); a.bswap (R8); a.rx (0x89, R8, XMEM, ix); }
				else if (size == 2) { a.mov (R8, RDX); a.swap16 (R8); a.b (0x66); a.rx (0x89, R8, XMEM, ix); }
				else a.rx (0x88, RDX, XMEM, ix);
			}
		}
		df.ret = a.p;
	}
	void memCold (const Def &d)
	{
		bool store = d.store; int size = d.size;
		if (store && d.dm != 2)					// the write-gather pipe (the GX FIFO): appended here
		{
			a.alui (OP_CMP, RCX, (s32) (d.dm == 1 ? 0xCC008000u : 0x0C008000u));
			u8 *notPipe = a.jcc (CC_NE);
			a.ld (R8, XM, oGatherN);
			if (size == 8) { a.mov (R9, RDX, true); a.bswap (R9, true); a.rx (0x89, R9, XM, R8, oGather, true); }
			else if (size == 4) { a.mov (R9, RDX); a.bswap (R9); a.rx (0x89, R9, XM, R8, oGather); }
			else if (size == 2) { a.mov (R9, RDX); a.swap16 (R9); a.b (0x66); a.rx (0x89, R9, XM, R8, oGather); }
			else a.rx (0x88, RDX, XM, R8, oGather);
			a.alui (OP_ADD, R8, size); a.st (XM, oGatherN, R8);
			a.alui (OP_CMP, R8, 32);
			a.jccTo (CC_B, d.ret);
			callKeep (hGather);				// (a burst: the FIFO's commands run)
			resync ();
			a.jmpTo (d.ret);
			Asm::patch (notPipe, a.p);
		}
		u32 pend = d.idx * 2 - synced;
		dc (pend);
		spill (true);						// (an exception leaves from here)
		a.st (XM, oArg, RCX); a.sti (XM, oArg + 8, d.pc);
		if (store) a.st (XM, oArg + 16, RDX, true);
		cyclesOut ();
		static const Helper hs[2][9] = { { 0, hRd8, hRd16, 0, hRd32, 0, 0, 0, hRd64 }, { 0, hWr8, hWr16, 0, hWr32, 0, 0, 0, hWr64 } };
		call (hs[store][size]);
		resync ();
		a.bti (RAX, 32, true);
		u8 *ok = a.jcc (CC_AE);
		dc (2);
		a.jmpTo (exitStub);
		Asm::patch (ok, a.p);
		uc (pend);
		reload (false);
		if (!store && size == 8) a.ld (RAX, XM, oScratch, true);
		a.jmpTo (d.ret);
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
		spill (true);
		a.sti (XM, oArg, d.op); a.sti (XM, oArg + 8, d.pc);
		cyclesOut ();
		call (hInterp);
		resync ();
		a.test (RAX, RAX); u8 *j = a.jcc (CC_E);
		dc (2);
		a.jmpTo (exitStub);
		Asm::patch (j, a.p);
		if (!d.back) { a.sti (XM, oPc, d.pc + 4); dc (2); a.jmpTo (exitStub); return; }
		uc (pend);
		reload (true);						// (it may have written any of them)
		a.jmpTo (d.ret);
	}
	// the FPU's use is checked once a block (MSR.FP: else the interpreter takes the exception)
	void fpCheck (u32 op, u32 pc, u32 idx)
	{
		if (fpOk) return;
		fpOk = true;
		a.ld (RAX, XM, oMsr); a.testi (RAX, 0x2000);
		Def &d = interpSide (op, pc, idx, false);
		d.site = a.jcc (CC_E);
	}
	bool fpInsn (u32 op, u32 pc, u32 idx);
	bool fpLoadStore (u32 op, u32 pc, u32 idx, int kind, bool x, bool upd);
	bool psq (u32 op, u32 pc, u32 idx, bool load, bool upd, bool x = false);
	void eaPsq (int hbase, int hidx, u32 off);
	// the constants (in the code buffer, 16-byte aligned: rip-relative operands)
	struct Consts { u64 sign2[2], abs2[2], one2[2], q[64][2], lo[8][2], hi[8][2]; };
	Consts *K;
	void loadK (int x, const u64 *k) { a.ssemRip (0x66, S_MOVAPD, x, k); }
	void negK (int x) { a.ssemRip (0x66, S_XORPD, x, K->sign2); }
	void roundS (int x) { a.sd (S_CVT, x, x); a.sse (0xF3, S_CVT, x, x); }	// the low half to single and back
	void roundS2 (int x) { a.pd (S_CVT, x, x); a.sse (0, S_CVT, x, x); }	// both halves
	// the multiplicand of a single-precision multiply: its mantissa rounded to 25 bits (lane `lane`
	// of xmm xs -> the low half of xmm xd; rax, rcx taken)
	void force25 (int xd, int xs, int lane)
	{
		if (lane) { a.pshufd (xd, xs, 0xEE); a.movqrx (RAX, xd); } else a.movqrx (RAX, xs);
		a.mov (RCX, RAX); a.alui (OP_AND, RCX, 0x08000000);
		a.alui (OP_AND, RAX, (s32) 0xF8000000u, true);
		a.alu (OP_ADD, RAX, RCX, true);
		a.movqxr (xd, RAX);
	}
	// a single's bits (gpr ws) -> the double the 750 loads (xmm xd): cvtss2sd; inf / a signalling NaN:
	// cvtToDouble (r8 taken)
	void cvtD (int xd, int ws)
	{
		a.mov (R8, ws); a.shi (SH_SHR, R8, 22); a.alui (OP_AND, R8, 0x1FF); a.alui (OP_CMP, R8, 0x1FE);
		Def &d = defer (D_CVTD); d.r1 = xd; d.r2 = ws;
		d.site = a.jcc (CC_E);
		a.movd (xd, ws); a.sse (0xF3, S_CVT, xd, xd);
		d.ret = a.p;
	}
	// a double's bits (gpr xs) -> the single the 750 stores (gpr wd, may be xs): the bits taken as they
	// are; the denormal range: cvtToSingle (r8, r9 taken)
	void cvtS (int wd, int xs)
	{
		a.mov (R8, xs, true); a.shi (SH_SHR, R8, 52, true); a.alui (OP_AND, R8, 0x7FF); a.alui (OP_CMP, R8, 896);
		Def &d = defer (D_CVTS); d.r1 = wd; d.r2 = xs;
		d.site = a.jcc (CC_BE);
		d.ret2 = a.p;
		a.mov (R8, xs, true); a.shi (SH_SHR, R8, 32, true); a.alui (OP_AND, R8, (s32) 0xC0000000u);
		a.mov (R9, xs, true); a.shi (SH_SHR, R9, 29, true); a.alui (OP_AND, R9, 0x3FFFFFFF);
		a.alu (OP_OR, R8, R9); a.mov (wd, R8);
		d.ret = a.p;
	}
	// the effective address into ecx: (rA|0) + d, or (rA|0) + rB
	void eaD (u32 op, bool upd)
	{
		int ra = (int) ((op >> 16) & 31); u32 d = (u32) (s32) (s16) op;
		if (ra == 0 && !upd) { a.movi (RCX, d); return; }
		addConst (RCX, G (ra), d);
	}
	void eaX (u32 op, bool upd)
	{
		int ra = (int) ((op >> 16) & 31), rb = (int) ((op >> 11) & 31);
		int b = G (rb);
		if (ra == 0 && !upd) { a.mov (RCX, b); return; }
		int x = G (ra);
		a.leax (RCX, x, b);
	}
	// a load / store: size, sign-extended (lha), with update (rA = the address, as the interpreter:
	// kept on the stack across the cold path)
	void load (u32 op, bool x, int size, bool sext, bool upd, u32 pc, u32 idx)
	{
		if (x) eaX (op, upd); else eaD (op, upd);
		if (upd) a.st (RSP, SLOT_EA, RCX);
		memop (false, size, pc, idx);
		int d = W ((int) ((op >> 21) & 31));
		if (sext) a.movsx16 (d, RAX); else a.mov (d, RAX);
		if (upd) a.ld (W ((int) ((op >> 16) & 31)), RSP, SLOT_EA);
	}
	void store (u32 op, bool x, int size, bool upd, u32 pc, u32 idx)
	{
		if (x) eaX (op, upd); else eaD (op, upd);
		if (upd) a.st (RSP, SLOT_EA, RCX);
		int s = G ((int) ((op >> 21) & 31));
		a.mov (RDX, s);
		memop (true, size, pc, idx);
		if (upd) a.ld (W ((int) ((op >> 16) & 31)), RSP, SLOT_EA);
	}
	// the branch's conditions (BO, BI): a jump to the "not taken" place for each, 0..2
	int bcTests (u32 bo, u32 bi, bool ctrToo, u8 **nt)
	{
		int n = 0;
		int crh = !(bo & 16) ? G (G_CR) : -1;			// (all the registers taken before a branch)
		if (ctrToo && !(bo & 4))
		{
			int c = RW (G_CTR); a.alui (OP_SUB, c, 1);
			nt[n++] = a.jcc ((bo & 2) ? CC_NE : CC_E);		// (bo & 2: branch if ctr == 0)
		}
		if (crh >= 0) { a.testi (crh, 1u << (31 - bi)); nt[n++] = a.jcc ((bo & 8) ? CC_E : CC_NE); }	// (bo & 8: branch if the bit is set)
		return n;
	}

	// ecx = an address -> r8 = [ecx, ecx + len) in the host's MEM1, else (-> the branch) the cold path
	u8 *fastBlock (u32 len)
	{
		if (dmode == 2) return a.jmp ();
		a.mov (RAX, RCX);
		if (dmode == 1) { a.alui (OP_AND, RAX, (s32) 0xBFFFFFFFu); a.alui (OP_XOR, RAX, (s32) 0x80000000u); }
		a.alui (OP_CMP, RAX, (s32) (MEM1_SIZE - len));
		u8 *b = a.jcc (CC_A);
		a.leax (R8, XMEM, RAX, true);
		return b;
	}
	void multi (u32 op, u32 pc, u32 idx, bool load)		// lmw / stmw: MEM1 at once, else the interpreter
	{
		int d = (int) ((op >> 21) & 31), ra = (int) ((op >> 16) & 31);
		u32 disp = (u32) (s32) (s16) op;
		if (ra == 0) a.movi (RCX, disp); else addConst (RCX, G (ra), disp);
		if (load)						// (rD..r31 out of the cache: written in memory)
			for (int r = d; r < 32; r++)
				if (gHost[r] >= 0)
				{
					if (gDirty[r]) a.st (XM, gOff (r), gHost[r]);
					hGuest[gHost[r]] = -1; gHost[r] = -1; gDirty[r] = false;
				}
		Def &cd = interpSide (op, pc, idx, true);
		cd.site = fastBlock ((u32) (32 - d) * 4);
		for (int r = d; r < 32; r++)
		{
			int off = (r - d) * 4;
			if (load) { a.ld (RAX, R8, off); a.bswap (RAX); a.st (XM, gOff (r), RAX); }
			else
			{
				int src = gHost[r];
				if (src < 0) { a.ld (RAX, XM, gOff (r)); src = RAX; }
				a.mov (RDX, src); a.bswap (RDX); a.st (R8, off, RDX);
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
		a.sti (XM, oPc, target);
		if (total > synced) dc (total - synced);
		a.movi (RAX, 0);
		a.test (XDC, XDC, true);
		a.cmov (CC_G, XDC, RAX, true);				// (to the event: nothing left)
		a.jmpTo (exitStub);
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

// one instruction -> true: the block ends with it
bool Jit::insn (u32 op, u32 pc, u32 idx)
{
	int d = (int) ((op >> 21) & 31), ra = (int) ((op >> 16) & 31), rb = (int) ((op >> 11) & 31);
	u32 simm = (u32) (s32) (s16) op, uimm = op & 0xFFFF;
	bool rc = op & 1;
	switch (op >> 26)
	{
	case 7: { int x = G (ra); int r = W (d); a.imuli (r, x, (s32) simm); return false; }	// mulli
	case 8:								// subfic
	{
		int x = G (ra);
		a.movi (RDX, simm); a.alu (OP_SUB, RDX, x);
		setCa (CC_AE);
		a.mov (W (d), RDX);
		return false;
	}
	case 10: { int x = G (ra); a.alui (OP_CMP, x, (s32) uimm); crFromFlags (d >> 2, false); return false; }	// cmpli
	case 11: { int x = G (ra); a.alui (OP_CMP, x, (s32) simm); crFromFlags (d >> 2, true); return false; }	// cmpi
	case 12: case 13:						// addic, addic.
	{
		int x = G (ra);
		a.mov (RDX, x); a.alui (OP_ADD, RDX, (s32) simm);
		setCa (CC_B);
		int r = W (d); a.mov (r, RDX);
		if ((op >> 26) == 13) setCr0 (r);
		return false;
	}
	case 14: case 15:						// addi, addis
	{
		u32 c = (op >> 26) == 15 ? uimm << 16 : simm;
		if (ra == 0) { a.movi (W (d), c); return false; }
		int x = G (ra);
		addConst (W (d), x, c);
		return false;
	}
	case 16:							// bc
	{
		u32 target = ((op & 2) ? 0 : pc) + (u32) (s32) (s16) (op & 0xFFFC);
		if (op & 1) a.movi (W (G_LR), pc + 4);
		u8 *nt[2]; int n = bcTests ((u32) d, (u32) ra, true, nt);
		if (idle && target == (bKey & ~3u)) idleExit (target, (idx + 1) * 2);	// (a polling loop goes on)
		else exitTo (target, (idx + 1) * 2, false);
		if (n) { for (int i = 0; i < n; i++) Asm::patch (nt[i], a.p); exitTo (pc + 4, (idx + 1) * 2, false); }
		return true;
	}
	case 18:							// b
	{
		u32 off = op & 0x03FFFFFC; if (off & 0x02000000) off |= 0xFC000000;
		u32 target = ((op & 2) ? 0 : pc) + off;
		if (op & 1) a.movi (W (G_LR), pc + 4);
		if (target == pc && idx == 0 && !(op & 1))		// "b .": idle until the next event
		{
			idleExit (target, 2);
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
			int t = G (toLr ? G_LR : G_CTR);
			a.mov (R9, t); a.alui (OP_AND, R9, ~3);
			if (op & 1) a.movi (W (G_LR), pc + 4);
			u8 *nt[2]; int n = bcTests ((u32) d, (u32) ra, toLr, nt);
			a.mov (RAX, R9);
			exitReg ((idx + 1) * 2, false);
			if (n) { for (int i = 0; i < n; i++) Asm::patch (nt[i], a.p); exitTo (pc + 4, (idx + 1) * 2, false); }
			return true;
		}
		case 150: exitTo (pc + 4, (idx + 1) * 2, false); return true;	// isync
		case 50: interp (op, pc, idx, true); return true;		// rfi
		case 257: case 129: case 289: case 225: case 33: case 449: case 417: case 193:	// the CR logic
		{
			int c = RW (G_CR);
			a.mov (RAX, c); a.shi (SH_SHR, RAX, 31 - ra);
			a.mov (RDX, c); a.shi (SH_SHR, RDX, 31 - rb);
			switch ((op >> 1) & 0x3FF)
			{
			case 257: a.alu (OP_AND, RAX, RDX); break;				// crand
			case 129: a.un (UN_NOT, RDX); a.alu (OP_AND, RAX, RDX); break;		// crandc
			case 289: a.alu (OP_XOR, RAX, RDX); a.un (UN_NOT, RAX); break;		// creqv
			case 225: a.alu (OP_AND, RAX, RDX); a.un (UN_NOT, RAX); break;		// crnand
			case 33: a.alu (OP_OR, RAX, RDX); a.un (UN_NOT, RAX); break;		// crnor
			case 449: a.alu (OP_OR, RAX, RDX); break;				// cror
			case 417: a.un (UN_NOT, RDX); a.alu (OP_OR, RAX, RDX); break;		// crorc
			default: a.alu (OP_XOR, RAX, RDX); break;				// crxor
			}
			a.alui (OP_AND, RAX, 1);
			a.alui (OP_AND, c, (s32) ~(1u << (31 - d)));
			a.shi (SH_SHL, RAX, 31 - d); a.alu (OP_OR, c, RAX);
			return false;
		}
		}
		interp (op, pc, idx, false);
		return false;
	case 17: interp (op, pc, idx, true); return true;		// sc
	case 20:							// rlwimi
	{
		int sh = rb, mb = (int) ((op >> 6) & 31), me = (int) ((op >> 1) & 31);
		u32 mk = mask (mb, me);
		int s = G (d), x = RW (ra);
		a.mov (RAX, s); a.shi (SH_ROL, RAX, sh);
		a.alui (OP_AND, RAX, (s32) mk);
		a.alui (OP_AND, x, (s32) ~mk);
		a.alu (OP_OR, x, RAX);
		if (rc) setCr0 (x);
		return false;
	}
	case 21:							// rlwinm
	{
		int sh = rb, mb = (int) ((op >> 6) & 31), me = (int) ((op >> 1) & 31);
		int s = G (d), x = W (ra);
		a.mov (x, s);
		if (sh && mb == 0 && me == 31 - sh) a.shi (SH_SHL, x, sh);		// slwi
		else if (sh && me == 31 && mb == 32 - sh) a.shi (SH_SHR, x, mb);	// srwi
		else { a.shi (SH_ROL, x, sh); andImm (x, mask (mb, me)); }
		if (rc) setCr0 (x);
		return false;
	}
	case 23:							// rlwnm
	{
		int s = G (d), b = G (rb);
		a.mov (RCX, b); a.mov (RAX, s); a.shcl (SH_ROL, RAX);
		andImm (RAX, mask ((int) ((op >> 6) & 31), (int) ((op >> 1) & 31)));
		int x = W (ra); a.mov (x, RAX);
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
		a.mov (x, s);
		a.alui (o <= 25 ? OP_OR : o <= 27 ? OP_XOR : OP_AND, x, (s32) c);
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
	{								// the XO-form arithmetic, without OE: the result in edx
		bool one = x9 == 234 || x9 == 202 || x9 == 232 || x9 == 200 || x9 == 104;	// (rA only)
		int x = G (ra), y = one ? -1 : G (rb);
		switch (x9)
		{
		case 266: a.mov (RDX, x); a.alu (OP_ADD, RDX, y); break;						// add
		case 10: a.mov (RDX, x); a.alu (OP_ADD, RDX, y); setCa (CC_B); break;				// addc
		case 138: caToCF (false); a.mov (RDX, x); a.alu (OP_ADC, RDX, y); setCa (CC_B); break;		// adde
		case 234: caToCF (false); a.mov (RDX, x); a.alui (OP_ADC, RDX, -1); setCa (CC_B); break;	// addme
		case 202: caToCF (false); a.mov (RDX, x); a.alui (OP_ADC, RDX, 0); setCa (CC_B); break;	// addze
		case 40: a.mov (RDX, y); a.alu (OP_SUB, RDX, x); break;						// subf
		case 8: a.mov (RDX, y); a.alu (OP_SUB, RDX, x); setCa (CC_AE); break;				// subfc
		case 136: caToCF (true); a.mov (RDX, y); a.alu (OP_SBB, RDX, x); setCa (CC_AE); break;	// subfe (y - x - !CA)
		case 232: caToCF (true); a.movi (RDX, 0xFFFFFFFFu); a.alu (OP_SBB, RDX, x); setCa (CC_AE); break;	// subfme
		case 200: caToCF (true); a.movi (RDX, 0); a.alu (OP_SBB, RDX, x); setCa (CC_AE); break;		// subfze
		case 104: a.mov (RDX, x); a.un (UN_NEG, RDX); break;						// neg
		case 235: a.mov (RDX, x); a.imul (RDX, y); break;						// mullw
		case 75: a.movsxd (RAX, x); a.movsxd (RDX, y); a.imul (RAX, RDX, true); a.shi (SH_SAR, RAX, 32, true); a.mov (RDX, RAX); break;	// mulhw
		case 11: a.mov (RAX, x); a.mov (RDX, y); a.imul (RAX, RDX, true); a.shi (SH_SHR, RAX, 32, true); a.mov (RDX, RAX); break;	// mulhwu
		case 491:							// divw (/ 0: -1 or 0 by the sign; INT_MIN / -1: -1)
		{
			a.mov (RAX, x); a.mov (RCX, y);
			a.test (RCX, RCX); u8 *z = a.jcc (CC_E);
			a.alui (OP_CMP, RCX, -1); u8 *fine = a.jcc (CC_NE);
			a.alui (OP_CMP, RAX, (s32) 0x80000000u); u8 *z2 = a.jcc (CC_E);
			Asm::patch (fine, a.p);
			a.cdq (); a.un (UN_IDIV, RCX); u8 *done = a.jmp ();
			Asm::patch (z, a.p); Asm::patch (z2, a.p);
			a.shi (SH_SAR, RAX, 31);
			Asm::patch (done, a.p);
			a.mov (RDX, RAX);
			break;
		}
		default:							// divwu (/ 0: 0)
		{
			a.mov (RAX, x); a.mov (RCX, y);
			a.test (RCX, RCX); u8 *z = a.jcc (CC_E);
			a.movi (RDX, 0); a.un (UN_DIV, RCX); a.mov (RDX, RAX); u8 *done = a.jmp ();
			Asm::patch (z, a.p);
			a.movi (RDX, 0);
			Asm::patch (done, a.p);
			break;
		}
		}
		int r = W (d); a.mov (r, RDX);
		if (rc) setCr0 (r);
		return false;
	}
	switch (xo)
	{
	case 0: case 32:						// cmp, cmpl
	{
		int x = G (ra), y = G (rb); a.alu (OP_CMP, x, y);
		crFromFlags (d >> 2, xo == 0);
		return false;
	}
	case 28: case 60: case 444: case 412: case 316: case 476: case 124: case 284:	// and andc or orc xor nand nor eqv
	case 24: case 536: case 792: case 824: case 26: case 954: case 922:	// slw srw sraw srawi cntlzw extsb extsh
	{
		int s = G (d), b = (xo != 824 && xo != 26 && xo != 954 && xo != 922) ? G (rb) : -1;
		switch (xo)							// (the result in edx)
		{
		case 28: a.mov (RDX, s); a.alu (OP_AND, RDX, b); break;
		case 60: a.mov (RDX, b); a.un (UN_NOT, RDX); a.alu (OP_AND, RDX, s); break;
		case 444: a.mov (RDX, s); if (b != s) a.alu (OP_OR, RDX, b); break;
		case 412: a.mov (RDX, b); a.un (UN_NOT, RDX); a.alu (OP_OR, RDX, s); break;
		case 316: a.mov (RDX, s); a.alu (OP_XOR, RDX, b); break;
		case 476: a.mov (RDX, s); a.alu (OP_AND, RDX, b); a.un (UN_NOT, RDX); break;
		case 124: a.mov (RDX, s); a.alu (OP_OR, RDX, b); a.un (UN_NOT, RDX); break;
		case 284: a.mov (RDX, s); a.alu (OP_XOR, RDX, b); a.un (UN_NOT, RDX); break;
		case 24: a.mov (RAX, s); a.mov (RCX, b); a.shcl (SH_SHL, RAX, true); a.mov (RDX, RAX); break;	// (64-bit: a shift of 32..63 gives 0)
		case 536: a.mov (RAX, s); a.mov (RCX, b); a.shcl (SH_SHR, RAX, true); a.mov (RDX, RAX); break;
		case 792:							// sraw
			a.movsxd (R8, s);
			a.mov (RCX, b); a.alui (OP_AND, RCX, 63);
			a.mov (RAX, R8, true); a.shcl (SH_SAR, RAX, true);	// (the result)
			a.mov (RDX, RAX, true); a.shcl (SH_SHL, RDX, true);	// (back: were bits lost?)
			a.alu (OP_CMP, RDX, R8, true);
			a.setcc (CC_NE, RDX); a.movzx8 (RDX, RDX);
			a.shi (SH_SHR, R8, 63, true); a.alu (OP_AND, RDX, R8);	// (and negative)
			setCaReg (RDX);
			a.mov (RDX, RAX);
			break;
		case 824:							// srawi
			if (rb == 0) { a.mov (RDX, s); int xr = RW (G_XER); a.alui (OP_AND, xr, (s32) ~0x20000000u); break; }
			a.mov (R8, s); a.shi (SH_SHL, R8, 32 - rb);		// (the bits shifted out, at the top)
			a.test (R8, R8); a.setcc (CC_NE, R8); a.movzx8 (R8, R8);
			a.mov (RAX, s); a.shi (SH_SHR, RAX, 31); a.alu (OP_AND, R8, RAX);	// (and negative)
			setCaReg (R8);
			a.mov (RDX, s); a.shi (SH_SAR, RDX, rb);
			break;
		case 26: a.movi (R8, 63); a.bsr (RDX, s); a.cmov (CC_E, RDX, R8); a.alui (OP_XOR, RDX, 31); break;	// cntlzw
		case 954: a.movsx8 (RDX, s); break;
		case 922: a.movsx16 (RDX, s); break;
		}
		int x = W (ra); a.mov (x, RDX);
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
		a.mov (RAX, s); a.alui (OP_AND, RAX, (s32) mk);
		a.alui (OP_AND, c, (s32) ~mk); a.alu (OP_OR, c, RAX);
		return false;
	}
	case 83: a.ld (W (d), XM, oMsr); return false;			// mfmsr
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
		if (n == 1) { int s = G (d); int x = W (G_XER); a.mov (x, s); a.alui (OP_AND, x, (s32) 0xE000FF7Fu); return false; }
		interp (op, pc, idx, true);				// (BATs, HID0, the DEC...: back to jitRun)
		return true;
	}
	case 86: case 54: case 278: case 246: case 598: case 854: case 306: case 566: case 470: return false;	// dcbf dcbst dcbt dcbtst sync eieio tlbie tlbsync dcbi
	case 1014:							// dcbz: 32 bytes of MEM1 at once
	{
		eaX (op, false);
		a.alui (OP_AND, RCX, ~31);
		Def &cd = interpSide (op, pc, idx, true);
		cd.site = fastBlock (32);
		a.sse (0, S_XORPD, 0, 0);				// (xorps xmm0, xmm0)
		a.ssem (0, S_MOVUPD_S, 0, R8, 0); a.ssem (0, S_MOVUPD_S, 0, R8, 16);
		cd.ret = a.p;
		return false;
	}
	case 371:							// mftb (TBL / TBU): from the cycles
	{
		u32 n = ((op >> 16) & 31) | ((op >> 6) & 0x3E0);
		if (n != 268 && n != 269) break;
		a.ld (RAX, XM, oEnd, true); a.alu (OP_SUB, RAX, XDC, true);
		if (idx * 2 > synced) a.alui (OP_ADD, RAX, (s32) (idx * 2 - synced), true);
		a.movi (RCX, CYC_PER_TB); a.movi (RDX, 0); a.un (UN_DIV, RCX, true);
		a.rm (0x03, RAX, XM, oTb, true);
		if (n == 269) a.shi (SH_SHR, RAX, 32, true);
		a.mov (W (d), RAX);
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
// The FPRs are cached in xmm6..15 (the low double = ps0, the high one = ps1): the plain FPU works on
// the low half (a double result keeps ps1, a single one goes to both), the paired singles on both
// at once. A result that is a NaN goes to the interpreter (the 750 keeps the first NaN operand); FPRF
// is set lazily (fprfSet). Every register an instruction uses is taken before its first branch.
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
			if (x5 >= 28 && !hasFma) { interp (op, pc, idx, false); return false; }
			fpCheck (op, pc, idx);
			bool one = x5 == 24 || x5 == 26;			// (fres, frsqrte: frB only)
			int ha = one ? -1 : FG (ra), hb = x5 != 25 ? FG (rb) : -1, hc = (x5 == 25 || x5 >= 28) ? FG (rc) : -1;
			int hd = FRes (d, !single, true);			// (a double result keeps ps1)
			int mc = hc;
			if (hc >= 0 && single && !isS (sgl0, rc)) { force25 (2, hc, 0); mc = 2; }
			switch (x5)
			{
			case 18: a.movapd (0, ha); a.sd (S_DIV, 0, hb); break;
			case 20: a.movapd (0, ha); a.sd (S_SUB, 0, hb); break;
			case 21: a.movapd (0, ha); a.sd (S_ADD, 0, hb); break;
			case 25: a.movapd (0, ha); a.sd (S_MUL, 0, mc); break;
			case 24: loadK (0, K->one2); a.sd (S_DIV, 0, hb); break;			// fres: 1 / b (rounded to single)
			case 26: a.sd (S_SQRT, 1, hb); loadK (0, K->one2); a.sd (S_DIV, 0, 1); break;	// frsqrte: 1 / sqrt (b)
			case 28: a.movapd (0, hb); a.fma (F_MSUB_SD, 0, ha, mc); break;		// a * c - b
			case 29: a.movapd (0, hb); a.fma (F_MADD_SD, 0, ha, mc); break;		// a * c + b
			case 30: a.movapd (0, hb); a.fma (F_MSUB_SD, 0, ha, mc); negK (0); break;
			default: a.movapd (0, hb); a.fma (F_MADD_SD, 0, ha, mc); negK (0); break;
			}
			a.ucomisd (0, 0); { nanDef = nDefs; Def &nd = interpSide (op, pc, idx, true); nd.site = a.jcc (CC_P); }
			if (single) { roundS (0); a.pd (S_UNPCKL, 0, 0); a.movapd (hd, 0); }
			else a.movsd (hd, 0);
			fSet (d); fprfSet (d, 0);
			setS (sgl0, d, single); if (single) setS (sgl1, d, true);
		}
		else if (prim == 63 && x5 == 23)				// fsel
		{
			fpCheck (op, pc, idx);
			int ha = FG (ra), hb = FG (rb), hc = FG (rc), hd = FRes (d, true, false);
			a.pd (S_XORPD, 0, 0);
			a.cmpsd (0, ha, 2);					// (0 <= a: a >= 0 and not a NaN)
			a.movapd (1, 0); a.pd (S_ANDPD, 1, hc);
			a.pd (S_ANDNPD, 0, hb); a.pd (S_ORPD, 0, 1);
			a.movsd (hd, 0); fSet (d);
			setS (sgl0, d, isS (sgl0, rc) && isS (sgl0, rb));
			return false;
		}
		else if (prim == 63 && (x10 == 0 || x10 == 32))		// fcmpu, fcmpo
		{
			fpCheck (op, pc, idx);
			fprfFlush (); fprfR = -1;				// (its FPCC goes over the FPRF to set)
			a.cmpbi (XM, oFprfPend, 0);
			{ Def &fd = defer (D_FPRF); fd.site = a.jcc (CC_NE); fd.ret = a.p; }
			int ha = FG (ra), hb = FG (rb);
			a.ucomisd (ha, hb);
			a.movi (RAX, 4); a.movi (R8, 8); a.cmov (CC_B, RAX, R8);
			a.movi (R8, 2); a.cmov (CC_E, RAX, R8);
			a.movi (R8, 1); a.cmov (CC_P, RAX, R8);
			int sh = 28 - (d >> 2) * 4, c = RW (G_CR);
			a.mov (RDX, RAX); a.shi (SH_SHL, RDX, sh);
			a.alui (OP_AND, c, (s32) ~(0xFu << sh)); a.alu (OP_OR, c, RDX);
			a.ld (RDX, XM, oFpscr); a.alui (OP_AND, RDX, (s32) ~0xF000u);
			a.shi (SH_SHL, RAX, 12); a.alu (OP_OR, RDX, RAX); a.st (XM, oFpscr, RDX);
			return false;
		}
		else if (prim == 63 && (x10 == 12 || x10 == 15))		// frsp, fctiwz
		{
			fpCheck (op, pc, idx);
			int hb = FG (rb), hd = FRes (d, true, x10 == 12);
			a.ucomisd (hb, hb); { nanDef = nDefs; Def &nd = interpSide (op, pc, idx, true); nd.site = a.jcc (CC_P); }
			if (x10 == 12)
			{
				a.movapd (0, hb); roundS (0);
				a.movsd (hd, 0); fSet (d); fprfSet (d, 0);
				setS (sgl0, d, true);
			}
			else							// (x86 gives 0x80000000 out of range: too big -> 0x7FFFFFFF)
			{
				a.cvttsd2si (RAX, hb);
				a.alui (OP_CMP, RAX, (s32) 0x80000000u); u8 *ok = a.jcc (CC_NE);
				a.pd (S_XORPD, 0, 0); a.ucomisd (hb, 0); u8 *ok2 = a.jcc (CC_BE);
				a.movi (RAX, 0x7FFFFFFF);
				Asm::patch (ok, a.p); Asm::patch (ok2, a.p);
				a.movq (RCX, 0xFFF8000000000000ull); a.alu (OP_OR, RAX, RCX, true);
				a.movqxr (0, RAX); a.movsd (hd, 0); fSet (d);
				setS (sgl0, d, false);
			}
		}
		else if (prim == 63 && (x10 == 40 || x10 == 72 || x10 == 136 || x10 == 264))	// fneg, fmr, fnabs, fabs
		{
			fpCheck (op, pc, idx);
			int hb = FG (rb), hd = FRes (d, true, false);
			if (x10 == 72) a.movsd (hd, hb);
			else
			{
				a.movapd (0, hb);
				if (x10 == 40) negK (0);
				else if (x10 == 136) a.ssemRip (0x66, S_ORPD, 0, K->sign2);
				else a.ssemRip (0x66, S_ANDPD, 0, K->abs2);
				a.movsd (hd, 0);
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
		bool fused = x5 == 14 || x5 == 15 || x5 >= 28;
		if ((!arith && !move) || (arith && fused && !hasFma)) { interp (op, pc, idx, false); return false; }
		fpCheck (op, pc, idx);
		if (move)
		{
			bool usesA = x10 >= 528;
			int ha = usesA ? FG (ra) : -1, hb = FG (rb), hd = FRes (d, false, false);
			bool l0 = x10 == 592 || x10 == 624 ? isS (sgl1, ra) : x10 >= 528 ? isS (sgl0, ra) : isS (sgl0, rb);
			bool l1 = x10 == 528 || x10 == 592 ? isS (sgl0, rb) : isS (sgl1, rb);
			switch (x10)
			{
			case 40: a.movapd (0, hb); negK (0); break;
			case 72: a.movapd (0, hb); break;
			case 136: a.movapd (0, hb); a.ssemRip (0x66, S_ORPD, 0, K->sign2); break;
			case 264: a.movapd (0, hb); a.ssemRip (0x66, S_ANDPD, 0, K->abs2); break;
			case 528: a.movapd (0, ha); a.pd (S_UNPCKL, 0, hb); break;		// (a0, b0)
			case 624: a.movapd (0, ha); a.pd (S_UNPCKH, 0, hb); break;		// (a1, b1)
			case 592: a.movapd (0, ha); a.shufpd (0, hb, 1); break;			// (a1, b0)
			default: a.movapd (0, hb); a.movsd (0, ha); break;			// (a0, b1)
			}
			a.movapd (hd, 0);
			fSet (d);
			setS (sgl0, d, l0); setS (sgl1, d, l1);
			return false;
		}
		switch (x5)
		{
		case 10: case 11:					// ps_sum0 (a0 + b1, c1), ps_sum1 (c0, a0 + b1)
		{
			int ha = FG (ra), hb = FG (rb), hc = FG (rc), hd = FRes (d, false, true);
			a.pshufd (1, hb, 0xEE);
			a.movapd (0, ha); a.sd (S_ADD, 0, 1);
			a.ucomisd (0, 0); { nanDef = nDefs; Def &nd = interpSide (op, pc, idx, true); nd.site = a.jcc (CC_P); }
			roundS (0);
			a.movapd (2, hc);
			if (x5 == 10) a.movsd (2, 0); else a.pd (S_UNPCKL, 2, 0);
			a.movapd (hd, 2);
			fSet (d); fprfSet (d, x5 == 10 ? 0 : 1);
			if (x5 == 10) { setS (sgl1, d, isS (sgl1, rc)); setS (sgl0, d, true); }
			else { setS (sgl0, d, isS (sgl0, rc)); setS (sgl1, d, true); }
			break;
		}
		case 23:						// ps_sel
		{
			int ha = FG (ra), hb = FG (rb), hc = FG (rc), hd = FRes (d, false, false);
			bool l0 = isS (sgl0, rc) && isS (sgl0, rb), l1 = isS (sgl1, rc) && isS (sgl1, rb);
			a.pd (S_XORPD, 0, 0); a.cmppd (0, ha, 2);		// (0 <= a, lane by lane)
			a.movapd (1, 0); a.pd (S_ANDPD, 1, hc);
			a.pd (S_ANDNPD, 0, hb); a.pd (S_ORPD, 0, 1);
			a.movapd (hd, 0); fSet (d);
			setS (sgl0, d, l0); setS (sgl1, d, l1);
			return false;
		}
		default:
		{
			bool usesB = x5 != 12 && x5 != 13 && x5 != 25, usesC = x5 != 18 && x5 != 20 && x5 != 21 && x5 != 24 && x5 != 26;
			bool usesA = x5 != 24 && x5 != 26;
			int ha = usesA ? FG (ra) : -1, hb = usesB ? FG (rb) : -1, hc = usesC ? FG (rc) : -1, hd = FRes (d, false, true);
			// the multiplicand: both halves of c (mul, madd...), or one half for both (muls0/1, madds0/1)
			int mc = hc;
			if (usesC)
			{
				int lane = x5 == 12 || x5 == 14 ? 0 : x5 == 13 || x5 == 15 ? 1 : -1;
				if (lane >= 0)
				{
					if (!isS (lane ? sgl1 : sgl0, rc)) force25 (1, hc, lane);
					else if (lane) a.pshufd (1, hc, 0xEE);
					else a.movapd (1, hc);
					a.pd (S_UNPCKL, 1, 1);
					mc = 1;
				}
				else
				{
					bool f0 = !isS (sgl0, rc), f1 = !isS (sgl1, rc);
					if (f0 || f1)
					{
						if (f1) force25 (1, hc, 1); else a.pshufd (1, hc, 0xEE);
						if (f0) force25 (2, hc, 0); else a.movapd (2, hc);
						a.pd (S_UNPCKL, 2, 1);
						mc = 2;
					}
				}
			}
			switch (x5)
			{
			case 18: a.movapd (0, ha); a.pd (S_DIV, 0, hb); break;
			case 24: loadK (0, K->one2); a.pd (S_DIV, 0, hb); break;				// ps_res: 1 / b
			case 26: a.pd (S_SQRT, 3, hb); loadK (0, K->one2); a.pd (S_DIV, 0, 3); break;	// ps_rsqrte
			case 20: a.movapd (0, ha); a.pd (S_SUB, 0, hb); break;
			case 21: a.movapd (0, ha); a.pd (S_ADD, 0, hb); break;
			case 25: case 12: case 13: a.movapd (0, ha); a.pd (S_MUL, 0, mc); break;
			case 14: case 15: case 29: a.movapd (0, hb); a.fma (F_MADD_PD, 0, ha, mc); break;	// a * c + b
			case 28: a.movapd (0, hb); a.fma (F_MSUB_PD, 0, ha, mc); break;			// a * c - b
			case 30: a.movapd (0, hb); a.fma (F_MSUB_PD, 0, ha, mc); negK (0); break;
			default: a.movapd (0, hb); a.fma (F_MADD_PD, 0, ha, mc); negK (0); break;
			}
			a.movapd (3, 0); a.cmppd (3, 3, 3); a.movmskpd (RAX, 3); a.test (RAX, RAX);	// (a NaN in either half)
			{ nanDef = nDefs; Def &nd = interpSide (op, pc, idx, true); nd.site = a.jcc (CC_NE); }
			roundS2 (0);
			a.movapd (hd, 0); fSet (d); fprfSet (d, 0);
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
	if (kind >= 2)
	{
		int hs = FG (d);
		a.movqrx (RDX, hs);
		if (kind == 2) cvtS (RDX, RDX);
		if (x) eaX (op, upd); else eaD (op, upd);
		if (upd) a.st (RSP, SLOT_EA, RCX);
		memop (true, kind == 3 ? 8 : 4, pc, idx);
	}
	else
	{
		if (x) eaX (op, upd); else eaD (op, upd);
		if (upd) a.st (RSP, SLOT_EA, RCX);
		int hd = FRes (d, kind == 1, false);			// (lfd keeps ps1)
		if (kind == 0)
		{
			memop (false, 4, pc, idx); cvtD (0, RAX);
			a.pd (S_UNPCKL, 0, 0); a.movapd (hd, 0);
			setS (sgl0, d, true); setS (sgl1, d, true);
		}
		else { memop (false, 8, pc, idx); a.movqxr (0, RAX); a.movsd (hd, 0); setS (sgl0, d, false); }
		fSet (d);
	}
	if (upd) a.ld (W (ra), RSP, SLOT_EA);
	return false;
}

// psq_l / psq_st (+ u): translated for the GQR's value when the block is translated (a float type:
// two floats in one 8-byte access, or one with W; an integer type u8 / u16 / s8 / s16 with its
// scale: both elements in one access, converted and scaled); the GQR checked at run time, another
// value through the interpreter
void Jit::eaPsq (int hbase, int hidx, u32 off)
{
	if (hidx >= 0) { if (hbase < 0) a.mov (RCX, hidx); else a.leax (RCX, hbase, hidx); }
	else if (hbase < 0) a.movi (RCX, off);
	else addConst (RCX, hbase, off);
}

bool Jit::psq (u32 op, u32 pc, u32 idx, bool load, bool upd, bool x)
{
	int d = (int) ((op >> 21) & 31), ra = (int) ((op >> 16) & 31), rb = (int) ((op >> 11) & 31);
	int q = x ? (int) ((op >> 7) & 7) : (int) ((op >> 12) & 7); bool w = x ? (op >> 10) & 1 : (op >> 15) & 1;
	u32 off = x ? 0 : (u32) ((s32) ((op & 0xFFF) << 20) >> 20);
	u32 g = m->gqr[q];
	int type = (int) (load ? g >> 16 : g) & 7, scale = (int) (load ? g >> 24 : g >> 8) & 63;
	bool isInt = type >= 4;
	fpCheck (op, pc, idx);
	int hbase = (ra == 0 && !upd) ? -1 : (upd ? RW (ra) : G (ra));	// (all taken before the branch)
	int hidx = x ? G (rb) : -1;
	int hd = load ? FRes (d, false, false) : FG (d);
	a.ld (RAX, XM, oGqr + 4 * q);
	int oth = nDefs;
	if (!isInt)							// (types 0..3: floats)
	{
		a.testi (RAX, load ? 0x40000u : 0x4u);
		Def &od = interpSide (op, pc, idx, true); od.site = a.jcc (CC_NE);
	}
	else								// (this type and scale)
	{
		if (load) a.shi (SH_SHR, RAX, 16);
		a.alui (OP_AND, RAX, 0x3F07);
		a.alui (OP_CMP, RAX, scale << 8 | type);
		Def &od = interpSide (op, pc, idx, true); od.site = a.jcc (CC_NE);
	}
	int es = (type == 4 || type == 6) ? 1 : 2, bits = es * 8;	// (an integer's size)
	bool sgn = type >= 6;
	if (load)
	{
		eaPsq (hbase, hidx, off);
		if (upd) a.st (RSP, SLOT_EA, RCX);
		if (isInt)
		{
			memop (false, w ? es : 2 * es, pc, idx);
			a.mov (RCX, RAX);					// (the first element: the high bits, or all of them with W)
			if (!w) a.shi (SH_SHR, RCX, bits);
			if (sgn) { if (bits == 8) a.movsx8 (RCX, RCX); else a.movsx16 (RCX, RCX); }
			a.cvtsi2sd (0, RCX);
			if (w) loadK (1, K->one2);
			else
			{
				if (bits == 8) { if (sgn) a.movsx8 (RDX, RAX); else a.movzx8 (RDX, RAX); }
				else { if (sgn) a.movsx16 (RDX, RAX); else a.movzx16 (RDX, RAX); }
				a.cvtsi2sd (1, RDX);
			}
			if (qmul (-scale) != 1.0)
			{
				a.ssemRip (0xF2, S_MUL, 0, K->q[(-scale) & 63]);
				if (!w) a.ssemRip (0xF2, S_MUL, 1, K->q[(-scale) & 63]);
			}
			a.pd (S_UNPCKL, 0, 1); a.movapd (hd, 0);
		}
		else if (w)
		{
			memop (false, 4, pc, idx);
			cvtD (0, RAX);
			loadK (1, K->one2);
			a.pd (S_UNPCKL, 0, 1); a.movapd (hd, 0);
		}
		else
		{
			memop (false, 8, pc, idx);
			a.st (RSP, SLOT_A, RAX, true);
			a.shi (SH_SHR, RAX, 32, true);
			cvtD (0, RAX); a.ssem (0xF2, 0x11, 0, RSP, SLOT_B);	// (ps0, kept across the next)
			a.ld (RAX, RSP, SLOT_A);
			cvtD (0, RAX);
			a.ssem (0xF2, 0x10, 1, RSP, SLOT_B);
			a.pd (S_UNPCKL, 1, 0); a.movapd (hd, 1);
		}
		fSet (d);
	}
	else if (isInt)
	{
		// (double) (float) v * 2^scale, a NaN to the interpreter, clamped to the type, truncated
		a.movapd (0, hd); roundS2 (0);
		a.ucomisd (0, 0);
		{ Def &nd = interpSide (op, pc, idx, true); nd.site = a.jcc (CC_P); }
		int nan1 = nDefs - 1, nan2 = -1;
		if (!w) { a.pshufd (1, 0, 0xEE); a.ucomisd (1, 1); nan2 = nDefs; Def &nd = interpSide (op, pc, idx, true); nd.site = a.jcc (CC_P); }
		if (qmul (scale) != 1.0) a.ssemRip (0x66, S_MUL, 0, K->q[scale & 63]);
		a.ssemRip (0x66, S_MAXSD, 0, K->lo[type]); a.ssemRip (0x66, S_MINSD, 0, K->hi[type]);	// (maxpd, minpd)
		a.cvttpd2dq (0, 0);
		a.movqrx (RAX, 0);					// (the first element in the low half, the second above)
		if (w) { if (bits == 8) a.movzx8 (RDX, RAX); else a.movzx16 (RDX, RAX); }
		else
		{
			a.mov (RDX, RAX, true); a.shi (SH_SHR, RDX, 32, true);
			if (bits == 8) { a.movzx8 (RDX, RDX); a.movzx8 (RAX, RAX); } else { a.movzx16 (RDX, RDX); a.movzx16 (RAX, RAX); }
			a.shi (SH_SHL, RAX, bits); a.alu (OP_OR, RDX, RAX);
		}
		eaPsq (hbase, hidx, off);
		if (upd) a.st (RSP, SLOT_EA, RCX);
		memop (true, w ? es : 2 * es, pc, idx);
		if (upd) a.ld (hbase, RSP, SLOT_EA);
		defs[oth].ret = defs[nan1].ret = a.p;
		if (nan2 >= 0) defs[nan2].ret = a.p;
		return false;
	}
	else
	{
		a.movqrx (RDX, hd);
		if (w) cvtS (RDX, RDX);
		else
		{
			cvtS (RDX, RDX); a.st (RSP, SLOT_A, RDX);		// (ps0's single, kept across the next)
			a.pshufd (0, hd, 0xEE); a.movqrx (RDX, 0);
			cvtS (RDX, RDX);
			a.ld (RAX, RSP, SLOT_A); a.shi (SH_SHL, RAX, 32, true); a.alu (OP_OR, RDX, RAX, true);
		}
		eaPsq (hbase, hidx, off);
		if (upd) a.st (RSP, SLOT_EA, RCX);
		memop (true, w ? 4 : 8, pc, idx);
	}
	if (upd) a.ld (hbase, RSP, SLOT_EA);
	if (load) { setS (sgl0, d, true); setS (sgl1, d, true); }	// (the integer types too: small integers x 2^n)
	defs[oth].ret = a.p;
	return false;
}

#define OFF(f) ((int) ((u8 *) &mm->f - (u8 *) mm))

Jit::Jit (Machine *mm, void *mem, u32 size)
{
	m = mm;
	code = (u8 *) mem; codeEnd = code + size;
	oPc = OFF (pc); oCycles = OFF (cycles); oUntil = OFF (jitUntil); oEnd = OFF (jitEnd); oTb = OFF (tbBase); oCr = OFF (cr); oXer = OFF (xer);
	oLr = OFF (lr); oCtr = OFF (ctr); oMsr = OFF (msr); oMem1 = OFF (mem1); oScratch = OFF (jitScratch); oGpr = OFF (gpr); oArg = OFF (jitArg);
	oPs = OFF (ps); oFpscr = OFF (fpscr); oFprfVal = OFF (fprfVal); oFprfPend = OFF (fprfPending); oGqr = OFF (gqr); oGatherN = OFF (gatherN); oGather = OFF (gather);
	__builtin_cpu_init ();
	hasFma = __builtin_cpu_supports ("fma");
	fast = new Fast[1u << FAST_BITS];
	blocks = new Block[MAX_BLOCKS];
	links = new Link[2 * MAX_BLOCKS];
	a.side = 0; a.mainBytes = 0;

	// enter (the Machine, the block): save the callee-saved registers, xmm6..15 (Windows), set the
	// fixed registers, the countdown
	static const int saved[8] = { RBX, RBP, RSI, RDI, R12, R13, R14, R15 };
	a.p = code;
	enter = (void (*) (Machine *, void *)) (void *) a.p;
	for (int i = 0; i < 8; i++) a.push (saved[i]);
	a.alui (OP_SUB, RSP, FRAME, true);
	for (int i = 0; i < 10; i++) a.ssem (0, S_MOVUPD_S, 6 + i, RSP, 32 + 16 * i);	// (movups)
	a.mov (XM, ARG0, true);
	a.ld (XMEM, XM, oMem1, true);
	a.ld (RAX, XM, oUntil, true); a.ld (RCX, XM, oCycles, true);
	a.mov (XDC, RAX, true); a.alu (OP_SUB, XDC, RCX, true);	// (the countdown)
	a.st (XM, oEnd, RAX, true);
	a.jmpr (ARG1);
	exitStub = a.p;
	a.ld (RAX, XM, oEnd, true); a.alu (OP_SUB, RAX, XDC, true); a.st (XM, oCycles, RAX, true);
	for (int i = 0; i < 10; i++) a.ssem (0, S_MOVUPD_L, 6 + i, RSP, 32 + 16 * i);
	a.alui (OP_ADD, RSP, FRAME, true);
	for (int i = 7; i >= 0; i--) a.pop (saved[i]);
	a.ret ();
	// the dispatcher: eax = pc, edx = the mode (IR, DR) -> the next block, or back
	dispatch = a.p;
	a.test (XDC, XDC, true); a.jccTo (CC_LE, exitStub);
	a.mov (RCX, RAX); a.alu (OP_OR, RCX, RDX);
	a.shi (SH_SHR, RAX, 2); a.alui (OP_AND, RAX, (1 << FAST_BITS) - 1);
	a.shi (SH_SHL, RAX, 4, true);
	a.movq (R8, (u64) fast);
	a.rx (0x39, RCX, R8, RAX);					// cmp [r8 + rax], ecx
	a.jccTo (CC_NE, exitStub);
	a.rx (0xFF, 4, R8, RAX, 8);					// jmp [r8 + rax + 8]
	// the constants
	K = (Consts *) (void *) (code + CONST_OFF);
	K->sign2[0] = K->sign2[1] = 0x8000000000000000ull;
	K->abs2[0] = K->abs2[1] = 0x7FFFFFFFFFFFFFFFull;
	K->one2[0] = K->one2[1] = dbits (1.0);
	for (int s = 0; s < 64; s++) K->q[s][0] = K->q[s][1] = dbits (qmul (s));
	static const double lo[4] = { 0.0, 0.0, -128.0, -32768.0 }, hi[4] = { 255.0, 65535.0, 127.0, 32767.0 };
	for (int t = 0; t < 8; t++)
	{
		K->lo[t][0] = K->lo[t][1] = dbits (t >= 4 ? lo[t - 4] : 0.0);
		K->hi[t][0] = K->hi[t][1] = dbits (t >= 4 ? hi[t - 4] : 0.0);
	}
	codeStart = code + STUB_BYTES;
	flushAll ();
}

void Jit::flushAll ()
{
	a.p = codeStart;
	nBlocks = 0;
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

u8 *Jit::lookup (u32 key)
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
		if (links[k].key == b.key) Asm::patch (links[k].site, links[k].stub);
	if (m->jitBlocks) m->jitBlocks--;
}

void Jit::addLink (u32 key, u8 *site, u8 *stub)
{
	Link &l = links[nLinks];
	l.key = key; l.site = site; l.stub = stub;
	l.next = linkHead[hashOf (key)]; linkHead[hashOf (key)] = nLinks++;
}

void Jit::linkTo (u32 key, u8 *to)
{
	for (u32 k = linkHead[hashOf (key)]; k != NONE; k = links[k].next)
		if (links[k].key == key) Asm::patch (links[k].site, to);
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

void Jit::emitDefs ()
{
	a.side++;
	for (int i = 0; i < nDefs; i++)
	{
		const Def &d = defs[i];
		loadSt (d.st);
		Asm::patch (d.site, a.p);
		switch (d.kind)
		{
		case D_MEM: memCold (d); break;
		case D_INTERP: interpCold (d); break;
		case D_IEXIT: dc (2); a.jmpTo (exitStub); break;	// (the interpreter left: pc is set)
		case D_CVTD:						// (xmm r1 = the double of the single in r2)
			a.st (XM, oArg, d.r2);
			callKeep (hCvtD);
			a.sse (0x66, S_MOVQ_X, d.r1, RAX, true);
			a.jmpTo (d.ret);
			break;
		case D_CVTS:						// (r1 = the single of the double's bits in r2)
			a.mov (R8, d.r2, true); a.alu (OP_ADD, R8, R8, true);	// (x << 1: zero -> the plain way)
			a.jccTo (CC_E, d.ret2);
			a.st (XM, oArg, d.r2, true);
			callKeep (hCvtS);
			a.mov (d.r1, RAX);
			a.jmpTo (d.ret);
			break;
		default: callKeep (hFprf); a.jmpTo (d.ret); break;	// (D_FPRF)
		}
	}
	a.side--;
	nDefs = 0;
}

u8 *Jit::compile (u32 pc, u32 key)
{
	u32 pa = pc;
	if (key & 1)
	{
		u32 b = m->imap[pc >> 17];
		if (!b) return 0;
		pa = ((b - 1) << 17) | (pc & 0x1FFFF);
	}
	if (pa >= MEM1_SIZE || (pc & 3)) return 0;
	if (codeEnd - a.p < (1 << 18) || nBlocks >= MAX_BLOCKS || nLinks + 2 * MAX_INSNS >= 2 * MAX_BLOCKS) flushAll ();
#ifdef GC_TRACE
	if (m->jitCompiles < 400 || !(m->jitCompiles % 5000)) printf ("JIT: compile %08X key %X (%u blocks, %u in all, %u KB)\n", pc, key & 3, nBlocks, m->jitCompiles, (u32) (a.p - code) >> 10);
#endif
	bKey = key; synced = 0; fpOk = false; sgl0 = sgl1 = 0; nDefs = 0;
	idle = idleLoop (pa, pc);
	cacheReset ();
	dmode = !(key & 2) ? 0 : stdMap ? 1 : 2;
	u8 *start = a.p;
	u32 i = nBlocks++;
	Block &b = blocks[i];
	b.runs = 0;
	if (m->jitProfile)						// (the test's profile: the block's runs)
	{
		a.side++;
		a.movq (RAX, (u64) &b.runs); a.b (0x48); a.b (0xFF); a.b (0x00);	// inc qword [rax]
		a.side--;
	}
	a.mainBytes = 0; a.side = 0;
	u32 n = 0;
	for (;;)
	{
		u32 op = bswap32 (*(const u32 *) (m->mem1 + pa + n * 4));
		pinned = 0; fpinned = 0;
		bool end = insn (op, pc + n * 4, n);
		n++;
		if (end) break;
		if (n >= MAX_INSNS || !((pa + n * 4) & 0xFFF) || defFull ()) { exitTo (pc + n * 4, n * 2, false); break; }
	}
	emitDefs ();
	b.bytes = a.mainBytes; b.insns = n; b.size = (u32) (a.p - start);
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
		if (jitFlush) { j.flushAll (); jitFlush = false; }
		if (cycles >= decAt) { decAt = ~0ull; dec = 0xFFFFFFFF; decPending = true; }
		if ((extIrq || decPending) && (msr & MSR_EE)) checkInterrupts ();
		jitUntil = until < decAt ? until : decAt;
		u32 key = pc | ((msr & MSR_IR) ? 1 : 0) | ((msr & MSR_DR) ? 2 : 0);
		u8 *c = j.lookup (key);
		if (!c) c = j.compile (pc, key);
		if (!c) { step (); continue; }				// (not in MEM1: the interpreter, its exception)
		j.enter (this, c);
	}
}

void Machine::jitInvalidate (u32 pa, u32 len) { if (jit) jit->invalidate (pa, len); }

// (the test's profile) the blocks' runs, the host bytes of their main paths (/ 4), the guest instructions
void Machine::jitStats (u64 &runs, u64 &hostInsns, u64 &guestInsns)
{
	runs = hostInsns = guestInsns = 0;
	if (!jit) return;
	for (u32 i = 0; i < jit->nBlocks; i++)
	{
		const Jit::Block &b = jit->blocks[i];
		runs += b.runs; hostInsns += b.runs * (b.bytes / 4); guestInsns += b.runs * b.insns;
	}
}

// (the tests) the block translated for pc (the last one), its code
bool Machine::jitCode (u32 pc, const u32 *&code, u32 &words)
{
	if (!jit) return false;
	for (u32 i = jit->nBlocks; i-- > 0;)
		if ((jit->blocks[i].key & ~3u) == pc) { code = (const u32 *) (void *) jit->blocks[i].code; words = (jit->blocks[i].size + 3) / 4; return true; }
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
	pc = b.key & ~3u; runs = b.runs; code = (const u32 *) (void *) b.code; words = (b.size + 3) / 4;
	return true;
}

// (gcemu --jitprof: the AArch64 JIT's report; none here)
int Machine::jitReport (char *out, int cap, int) { if (cap > 0) out[0] = 0; return 0; }
int Machine::jitHotCode (u8 *, int, int) { return 0; }

#endif

} // namespace gc
