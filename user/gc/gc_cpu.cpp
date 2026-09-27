//
// gc/gc_cpu.cpp -- the Gekko (PowerPC 750CL): an interpreter. The integer unit (XER carry /
// overflow, CR0), the branch unit (CR, LR, CTR), the floating point (the host's IEEE doubles
// do the 750's arithmetic; the single-precision instructions round their result to single,
// their multiplicand to 25 bits first as the hardware; FPRF and CR1 kept), the paired singles
// (two singles per FPR, the quantized loads / stores with the GQRs), the SPRs (BATs, HIDs, the
// locked-cache DMA, the timebase and the decrementer), the exceptions.
// A single load converts its bits to double exactly (NaNs and denormals kept), as the manual's
// algorithm; a single store the reverse.
//
#include "gc/gc.h"

namespace gc {

enum { MSR_EE = 0x8000, MSR_FP = 0x2000, MSR_IP = 0x40 };
enum { XER_SO = 0x80000000u, XER_OV = 0x40000000u, XER_CA = 0x20000000u };
enum { CYC_INSN = 2 };					// CPU cycles an instruction (on average)

static inline u64 d2u (double d) { u64 u; __builtin_memcpy (&u, &d, 8); return u; }
static inline double u2d (u64 u) { double d; __builtin_memcpy (&d, &u, 8); return d; }
static inline u32 f2u (float f) { u32 u; __builtin_memcpy (&u, &f, 4); return u; }
static inline float u2f (u32 u) { float f; __builtin_memcpy (&f, &u, 4); return f; }

// a single's bits -> the double the 750 loads (exactly, NaNs and denormals kept)
static u64 cvtToDouble (u32 v)
{
	u64 x = v, e = (x >> 23) & 0xFF, frac = x & 0x007FFFFF;
	if (e > 0 && e < 255)
	{
		u64 y = !(e >> 7), z = y << 61 | y << 60 | y << 59;
		return ((x & 0xC0000000) << 32) | z | ((x & 0x3FFFFFFF) << 29);
	}
	if (e == 0 && frac != 0)
	{
		e = 1023 - 126;
		do { frac <<= 1; e--; } while (!(frac & 0x00800000));
		return ((x & 0x80000000) << 32) | (e << 52) | ((frac & 0x007FFFFF) << 29);
	}
	u64 y = e >> 7, z = y << 61 | y << 60 | y << 59;
	return ((x & 0xC0000000) << 32) | z | ((x & 0x3FFFFFFF) << 29);
}

// a double -> the single's bits the 750 stores
static u32 cvtToSingle (u64 x)
{
	u32 e = (u32) (x >> 52) & 0x7FF;
	if (e > 896 || (x & ~0x8000000000000000ull) == 0) return (u32) ((x >> 32) & 0xC0000000) | (u32) ((x >> 29) & 0x3FFFFFFF);
	if (e >= 874)
	{
		u32 t = (u32) (0x80000000 | ((x & 0x000FFFFFFFFFFFFFull) >> 21));
		t >>= 905 - e;
		return t | (u32) ((x >> 32) & 0x80000000);
	}
	return (u32) ((x >> 32) & 0xC0000000) | (u32) ((x >> 29) & 0x3FFFFFFF);
}

static inline double roundSingle (double d) { return (double) (float) d; }
// the multiplicand of a single-precision multiply: rounded to 25 bits of mantissa first
static inline double force25 (double d)
{
	u64 u = d2u (d);
	if ((u & 0x7FF0000000000000ull) == 0x7FF0000000000000ull) return d;	// (inf / NaN)
	return u2d ((u & 0xFFFFFFFFF8000000ull) + (u & 0x8000000ull));
}
static inline bool isNaN (double d) { return d != d; }

static inline u32 rotl (u32 v, int n) { n &= 31; return n ? (v << n) | (v >> (32 - n)) : v; }
static inline u32 mask (int mb, int me)
{
	u32 a = 0xFFFFFFFFu >> mb, b = 0xFFFFFFFFu << (31 - me);
	return mb <= me ? a & b : a | b;
}

// ---- the decrementer and the timebase ---------------------------------------------------------------------------
u32 Machine::decRead ()
{
	if (decAt == ~0ull) return dec;
	return (u32) ((s64) (decAt - cycles) / CYC_PER_TB) - 1;
}
void Machine::decWrite (u32 v)
{
	dec = v;
	// it counts down at the timebase's rate and interrupts when it goes from 0 to -1
	decAt = (s32) v >= 0 ? cycles + ((u64) v + 1) * CYC_PER_TB : ~0ull;
	if ((s32) v < 0) dec = v;
	jitUntil = 0;						// (the JIT's blocks: back to see it)
}

void Machine::exception (u32 vector, u32 ret)
{
	srr0 = ret;
	srr1 = msr & 0x87C0FFFF;
	msr &= ~0x04EF36u;
	npc = pc = vector | ((msr & MSR_IP) ? 0xFFF00000u : 0);
	resv = false;
}

void Machine::checkInterrupts ()
{
	if (!(msr & MSR_EE)) return;
	if (extIrq) { exception (0x500, pc); return; }
	if (decPending) { decPending = false; exception (0x900, pc); }
}

void Machine::run (u64 until)
{
	if (jit) { jitRun (until); return; }
	while (cycles < until && !halted)
	{
		if (cycles >= decAt) { decAt = ~0ull; dec = 0xFFFFFFFF; decPending = true; }
		if ((extIrq || decPending) && (msr & MSR_EE)) checkInterrupts ();
		step ();
	}
}

void Machine::step ()
{
	curPc = pc; npc = pc + 4;
	memFault = false;
	u32 op = fetch (pc);
	if (!memFault) exec (op);
	pc = npc;
	cycles += CYC_INSN;
}

static void hex8 (char *d, u32 v) { for (int i = 7; i >= 0; i--) *d++ = "0123456789ABCDEF"[(v >> (i * 4)) & 15]; }

// ---- the SPRs ---------------------------------------------------------------------------------------------------
u32 Machine::mfspr (u32 n)
{
	switch (n)
	{
	case 1: return xer;
	case 8: return lr;
	case 9: return ctr;
	case 18: return dsisr;
	case 19: return dar;
	case 22: return decRead ();
	case 25: return sdr1;
	case 26: return srr0;
	case 27: return srr1;
	case 268: return (u32) (tbBase + cycles / CYC_PER_TB);
	case 269: return (u32) ((tbBase + cycles / CYC_PER_TB) >> 32);
	case 272: case 273: case 274: case 275: return sprg[n - 272];
	case 282: return ear;
	case 287: return pvr;
	case 912: case 913: case 914: case 915: case 916: case 917: case 918: case 919: return gqr[n - 912];
	case 920: return hid2;
	case 921: return wpar;
	case 922: return dmaU;
	case 923: return dmaL;
	case 936: case 952: return mmcr0;
	case 940: case 956: return mmcr1;
	case 937: case 953: return pmc[0];
	case 938: case 954: return pmc[1];
	case 941: case 957: return pmc[2];
	case 942: case 958: return pmc[3];
	case 1008: return hid0;
	case 1009: return hid1;
	case 1011: return hid4;
	case 1017: return l2cr;
	case 1019: return ictc;
	case 1020: case 1021: case 1022: return thrm[n - 1020];
	}
	if (n >= 528 && n <= 535) return ibat[n - 528];
	if (n >= 536 && n <= 543) return dbat[n - 536];
	return 0;
}

void Machine::mtspr (u32 n, u32 v)
{
	switch (n)
	{
	case 1: xer = v & 0xE000FF7F; return;
	case 8: lr = v; return;
	case 9: ctr = v; return;
	case 18: dsisr = v; return;
	case 19: dar = v; return;
	case 22: decWrite (v); return;
	case 25: sdr1 = v; return;
	case 26: srr0 = v; return;
	case 27: srr1 = v; return;
	case 272: case 273: case 274: case 275: sprg[n - 272] = v; return;
	case 282: ear = v; return;
	case 284: { u64 t = tbBase + cycles / CYC_PER_TB; t = (t & 0xFFFFFFFF00000000ull) | v; tbBase = t - cycles / CYC_PER_TB; return; }
	case 285: { u64 t = tbBase + cycles / CYC_PER_TB; t = (t & 0xFFFFFFFFull) | (u64) v << 32; tbBase = t - cycles / CYC_PER_TB; return; }
	case 912: case 913: case 914: case 915: case 916: case 917: case 918: case 919: gqr[n - 912] = v; return;
	case 920: hid2 = v; return;
	case 921: wpar = v & ~0x1Fu; return;
	case 922: dmaU = v; return;
	case 923:
	{
		dmaL = v;
		if (v & 2)						// T: the locked cache's DMA
		{
			u32 len = ((dmaU & 0x1F) << 2) | ((v >> 2) & 3);
			if (len == 0) len = 128;
			len *= 32;
			u32 mem = dmaU & ~0x1Fu, lc = v & ~0x1Fu;
			u8 *m = ptr (mem & 0x01FFFFFF), *c = ptr (lc);
			if (m && c)
			{
				if (v & 0x10) for (u32 i = 0; i < len; i++) c[i] = m[i];	// LD: memory -> cache
				else { jitInvalidate (mem & 0x01FFFFFF, len); for (u32 i = 0; i < len; i++) m[i] = c[i]; }
			}
			dmaL &= ~3u;
		}
		return;
	}
	case 936: case 952: mmcr0 = v; return;
	case 940: case 956: mmcr1 = v; return;
	case 937: case 953: pmc[0] = v; return;
	case 938: case 954: pmc[1] = v; return;
	case 941: case 957: pmc[2] = v; return;
	case 942: case 958: pmc[3] = v; return;
	case 1008: hid0 = v; if (v & 0x800) jitFlush = true; return;	// (ICFI: the instruction cache flash-invalidated)
	case 1009: hid1 = v; return;
	case 1011: hid4 = v; return;
	case 1017: l2cr = v & ~1u; return;			// (the invalidate bit reads back clear)
	case 1019: ictc = v; return;
	case 1020: case 1021: case 1022: thrm[n - 1020] = v; return;
	}
	if (n >= 528 && n <= 535) { ibat[n - 528] = v; batRebuild (); return; }
	if (n >= 536 && n <= 543) { dbat[n - 536] = v; batRebuild (); return; }
}

// ---- the floating point status ---------------------------------------------------------------------------------
void Machine::setFprf (double d)
{
	u32 c;
	u64 u = d2u (d);
	bool neg = u >> 63;
	u64 e = (u >> 52) & 0x7FF, f = u & 0xFFFFFFFFFFFFFull;
	if (e == 0x7FF) c = f ? 0x11 : (neg ? 0x09 : 0x05);
	else if (e == 0) c = f ? (neg ? 0x18 : 0x14) : (neg ? 0x12 : 0x02);
	else c = neg ? 0x08 : 0x04;
	fpscr = (fpscr & ~0x1F000u) | (c << 12);
}

// ---- the quantized loads / stores (GQR: LD_TYPE 16-18, LD_SCALE 24-29, ST_TYPE 0-2, ST_SCALE 8-13) ----------
static double qscale (int scale) { scale = (scale & 0x20) ? scale - 64 : scale; double m = 1.0; if (scale > 0) while (scale--) m *= 2.0; else while (scale++ < 0) m *= 0.5; return m; }

double Machine::quantLoad (u32 ea, int type, int scale, int &size)
{
	switch (type)
	{
	case 4: size = 1; return (double) read8 (ea) * qscale (-scale & 63);
	case 5: size = 2; return (double) read16 (ea) * qscale (-scale & 63);
	case 6: size = 1; return (double) (s8) read8 (ea) * qscale (-scale & 63);
	case 7: size = 2; return (double) (s16) read16 (ea) * qscale (-scale & 63);
	default: size = 4; return u2d (cvtToDouble (read32 (ea)));
	}
}

void Machine::quantStore (u32 ea, double v, int type, int scale, int &size)
{
	if (type >= 4 && type <= 7)
	{
		double x = (double) (float) v * qscale (scale & 63);
		double lo = type == 4 || type == 5 ? 0.0 : type == 6 ? -128.0 : -32768.0;
		double hi = type == 4 ? 255.0 : type == 5 ? 65535.0 : type == 6 ? 127.0 : 32767.0;
		if (!(x >= lo)) x = lo;					// (NaN -> the minimum)
		if (x > hi) x = hi;
		s32 i = (s32) x;					// (toward zero)
		if (type == 4 || type == 6) { size = 1; write8 (ea, (u8) i); }
		else { size = 2; write16 (ea, (u16) i); }
		return;
	}
	size = 4;
	write32 (ea, cvtToSingle (d2u (v)));
}

// ---- the instructions -----------------------------------------------------------------------------------------
#define RD ((op >> 21) & 31)
#define RA ((op >> 16) & 31)
#define RB ((op >> 11) & 31)
#define RC ((op >> 6) & 31)
#define SIMM ((s32) (s16) op)
#define UIMM (op & 0xFFFF)
#define RA0 (RA ? gpr[RA] : 0)
#define RCBIT (op & 1)
#define LOADG(r, expr) do { u32 v_ = (expr); if (memFault) return; gpr[r] = v_; } while (0)
#define FPCHECK if (!(msr & MSR_FP)) { exception (0x800, curPc); return; }

void Machine::exec (u32 op)
{
	u32 d = RD, a = RA, ea;
	switch (op >> 26)
	{
	case 3:								// twi
	{
		s32 x = (s32) gpr[a], y = SIMM; u32 to = d;
		if (((to & 16) && x < y) || ((to & 8) && x > y) || ((to & 4) && x == y) || ((to & 2) && (u32) x < (u32) y) || ((to & 1) && (u32) x > (u32) y))
		{ srr1 = 0; exception (0x700, curPc); srr1 |= 0x20000; }
		return;
	}
	case 4: op4 (op); return;
	case 7: gpr[d] = (u32) ((s32) gpr[a] * SIMM); return;		// mulli
	case 8:								// subfic
	{
		u32 b = (u32) SIMM;
		u64 s = (u64) b + (u64) (u32) ~gpr[a] + 1;		// (carry = no borrow)
		xer = (s >> 32) ? xer | XER_CA : xer & ~XER_CA;
		gpr[d] = (u32) s;
		return;
	}
	case 10:							// cmpli
	{
		u32 x = gpr[a], y = UIMM, f = x < y ? 8 : x > y ? 4 : 2;
		int crf = (int) (d >> 2);
		cr = (cr & ~(0xF0000000u >> (crf * 4))) | ((f | (xer >> 31)) << (28 - crf * 4));
		return;
	}
	case 11:							// cmpi
	{
		s32 x = (s32) gpr[a], y = SIMM; u32 f = x < y ? 8 : x > y ? 4 : 2;
		int crf = (int) (d >> 2);
		cr = (cr & ~(0xF0000000u >> (crf * 4))) | ((f | (xer >> 31)) << (28 - crf * 4));
		return;
	}
	case 12: case 13:						// addic, addic.
	{
		u32 x = gpr[a], y = (u32) SIMM, r = x + y;
		xer = r < x ? xer | XER_CA : xer & ~XER_CA;
		gpr[d] = r;
		if ((op >> 26) == 13) setCr0 (r);
		return;
	}
	case 14: gpr[d] = RA0 + (u32) SIMM; return;			// addi
	case 15: gpr[d] = RA0 + (UIMM << 16); return;			// addis
	case 16:							// bc
	{
		u32 bo = d, bi = a;
		if (!(bo & 4)) ctr--;
		bool ctrOk = (bo & 4) || ((ctr != 0) ^ ((bo >> 1) & 1));
		bool condOk = (bo & 16) || (((cr >> (31 - bi)) & 1) == ((bo >> 3) & 1));
		if (ctrOk && condOk) npc = ((op & 2) ? 0 : curPc) + (u32) (s32) (s16) (op & 0xFFFC);
		if (op & 1) lr = curPc + 4;
		return;
	}
	case 17: exception (0xC00, curPc + 4); return;			// sc
	case 18:							// b
	{
		u32 off = op & 0x03FFFFFC; if (off & 0x02000000) off |= 0xFC000000;
		npc = ((op & 2) ? 0 : curPc) + off;
		if (op & 1) lr = curPc + 4;
		// an idle loop ("b ." with nothing else to do): wait for the next event
		if (npc == curPc && !(op & 1)) idleSkips++;
		return;
	}
	case 19: op19 (op); return;
	case 20:							// rlwimi
	{
		u32 m = mask ((op >> 6) & 31, (op >> 1) & 31), r = rotl (gpr[d], (int) RB);
		gpr[a] = (r & m) | (gpr[a] & ~m);
		if (RCBIT) setCr0 (gpr[a]);
		return;
	}
	case 21:							// rlwinm
		gpr[a] = rotl (gpr[d], (int) RB) & mask ((op >> 6) & 31, (op >> 1) & 31);
		if (RCBIT) setCr0 (gpr[a]);
		return;
	case 23:							// rlwnm
		gpr[a] = rotl (gpr[d], (int) (gpr[RB] & 31)) & mask ((op >> 6) & 31, (op >> 1) & 31);
		if (RCBIT) setCr0 (gpr[a]);
		return;
	case 24: gpr[a] = gpr[d] | UIMM; return;			// ori
	case 25: gpr[a] = gpr[d] | (UIMM << 16); return;		// oris
	case 26: gpr[a] = gpr[d] ^ UIMM; return;			// xori
	case 27: gpr[a] = gpr[d] ^ (UIMM << 16); return;		// xoris
	case 28: gpr[a] = gpr[d] & UIMM; setCr0 (gpr[a]); return;	// andi.
	case 29: gpr[a] = gpr[d] & (UIMM << 16); setCr0 (gpr[a]); return;	// andis.
	case 31: op31 (op); return;
	case 32: LOADG (d, read32 (RA0 + (u32) SIMM)); return;		// lwz
	case 33: ea = gpr[a] + (u32) SIMM; LOADG (d, read32 (ea)); gpr[a] = ea; return;	// lwzu
	case 34: LOADG (d, read8 (RA0 + (u32) SIMM)); return;		// lbz
	case 35: ea = gpr[a] + (u32) SIMM; LOADG (d, read8 (ea)); gpr[a] = ea; return;
	case 36: write32 (RA0 + (u32) SIMM, gpr[d]); return;		// stw
	case 37: ea = gpr[a] + (u32) SIMM; write32 (ea, gpr[d]); if (!memFault) gpr[a] = ea; return;
	case 38: write8 (RA0 + (u32) SIMM, (u8) gpr[d]); return;	// stb
	case 39: ea = gpr[a] + (u32) SIMM; write8 (ea, (u8) gpr[d]); if (!memFault) gpr[a] = ea; return;
	case 40: LOADG (d, read16 (RA0 + (u32) SIMM)); return;		// lhz
	case 41: ea = gpr[a] + (u32) SIMM; LOADG (d, read16 (ea)); gpr[a] = ea; return;
	case 42: LOADG (d, (u32) (s32) (s16) read16 (RA0 + (u32) SIMM)); return;	// lha
	case 43: ea = gpr[a] + (u32) SIMM; LOADG (d, (u32) (s32) (s16) read16 (ea)); gpr[a] = ea; return;
	case 44: write16 (RA0 + (u32) SIMM, (u16) gpr[d]); return;	// sth
	case 45: ea = gpr[a] + (u32) SIMM; write16 (ea, (u16) gpr[d]); if (!memFault) gpr[a] = ea; return;
	case 46:							// lmw
		ea = RA0 + (u32) SIMM;
		for (u32 r = d; r < 32; r++, ea += 4) { u32 v = read32 (ea); if (memFault) return; gpr[r] = v; }
		return;
	case 47:							// stmw
		ea = RA0 + (u32) SIMM;
		for (u32 r = d; r < 32; r++, ea += 4) { write32 (ea, gpr[r]); if (memFault) return; }
		return;
	case 48: case 49:						// lfs, lfsu
	{
		FPCHECK
		ea = ((op >> 26) == 49 ? gpr[a] : RA0) + (u32) SIMM;
		u32 v = read32 (ea); if (memFault) return;
		ps0[d] = ps1[d] = u2d (cvtToDouble (v));
		if ((op >> 26) == 49) gpr[a] = ea;
		return;
	}
	case 50: case 51:						// lfd, lfdu
	{
		FPCHECK
		ea = ((op >> 26) == 51 ? gpr[a] : RA0) + (u32) SIMM;
		u64 v = read64 (ea); if (memFault) return;
		ps0[d] = u2d (v);
		if ((op >> 26) == 51) gpr[a] = ea;
		return;
	}
	case 52: case 53:						// stfs, stfsu
		FPCHECK
		ea = ((op >> 26) == 53 ? gpr[a] : RA0) + (u32) SIMM;
		write32 (ea, cvtToSingle (d2u (ps0[d])));
		if ((op >> 26) == 53 && !memFault) gpr[a] = ea;
		return;
	case 54: case 55:						// stfd, stfdu
		FPCHECK
		ea = ((op >> 26) == 55 ? gpr[a] : RA0) + (u32) SIMM;
		write64 (ea, d2u (ps0[d]));
		if ((op >> 26) == 55 && !memFault) gpr[a] = ea;
		return;
	case 56: case 57:						// psq_l, psq_lu
	{
		FPCHECK
		s32 off = (s32) ((op & 0xFFF) << 20) >> 20;
		ea = ((op >> 26) == 57 ? gpr[a] : RA0) + (u32) off;
		u32 g = gqr[(op >> 12) & 7]; bool w = (op >> 15) & 1;
		int type = (int) (g >> 16) & 7, scale = (int) (g >> 24) & 63, size;
		double x = quantLoad (ea, type, scale, size);
		double y = w ? 1.0 : quantLoad (ea + (u32) size, type, scale, size);
		if (memFault) return;
		ps0[d] = x; ps1[d] = y;
		if ((op >> 26) == 57) gpr[a] = ea;
		return;
	}
	case 59: op59 (op); return;
	case 60: case 61:						// psq_st, psq_stu
	{
		FPCHECK
		s32 off = (s32) ((op & 0xFFF) << 20) >> 20;
		ea = ((op >> 26) == 61 ? gpr[a] : RA0) + (u32) off;
		u32 g = gqr[(op >> 12) & 7]; bool w = (op >> 15) & 1;
		int type = (int) g & 7, scale = (int) (g >> 8) & 63, size;
		quantStore (ea, ps0[d], type, scale, size);
		if (!w && !memFault) quantStore (ea + (u32) size, ps1[d], type, scale, size);
		if ((op >> 26) == 61 && !memFault) gpr[a] = ea;
		return;
	}
	case 63: op63 (op); return;
	}
	halted = true;
	const char *m = "unknown instruction "; int n = 0;
	while (m[n]) { haltMsg[n] = m[n]; n++; }
	hex8 (haltMsg + n, op); n += 8; haltMsg[n++] = ' '; haltMsg[n++] = '@'; hex8 (haltMsg + n, curPc); haltMsg[n + 8] = 0;
}

void Machine::op19 (u32 op)
{
	u32 d = RD, a = RA, b = RB;
	switch ((op >> 1) & 0x3FF)
	{
	case 0:								// mcrf
	{
		u32 f = (cr >> (28 - (a >> 2) * 4)) & 15;
		cr = (cr & ~(0xF0000000u >> ((d >> 2) * 4))) | (f << (28 - (d >> 2) * 4));
		return;
	}
	case 16:							// bclr
	{
		u32 bo = d, bi = a, target = lr & ~3u;
		if (!(bo & 4)) ctr--;
		bool ctrOk = (bo & 4) || ((ctr != 0) ^ ((bo >> 1) & 1));
		bool condOk = (bo & 16) || (((cr >> (31 - bi)) & 1) == ((bo >> 3) & 1));
		if (ctrOk && condOk) npc = target;
		if (op & 1) lr = curPc + 4;
		return;
	}
	case 528:							// bcctr
	{
		u32 bo = d, bi = a;
		bool condOk = (bo & 16) || (((cr >> (31 - bi)) & 1) == ((bo >> 3) & 1));
		if (condOk) npc = ctr & ~3u;
		if (op & 1) lr = curPc + 4;
		return;
	}
	case 50:							// rfi
		msr = (msr & ~0x87C0FFFFu) | (srr1 & 0x87C0FFFF);
		msr &= ~0x40000u;
		npc = srr0 & ~3u;
		return;
	case 150: return;						// isync
	}
	// the CR logic: crD = crA op crB
	u32 x = (cr >> (31 - a)) & 1, y = (cr >> (31 - b)) & 1, r;
	switch ((op >> 1) & 0x3FF)
	{
	case 257: r = x & y; break;					// crand
	case 129: r = x & !y; break;					// crandc
	case 289: r = !(x ^ y); break;					// creqv
	case 225: r = !(x & y); break;					// crnand
	case 33: r = !(x | y); break;					// crnor
	case 449: r = x | y; break;					// cror
	case 417: r = x | !y; break;					// crorc
	case 193: r = x ^ y; break;					// crxor
	default:
		halted = true;
		{ const char *m = "unknown op19 "; int n = 0; while (m[n]) { haltMsg[n] = m[n]; n++; } hex8 (haltMsg + n, op); haltMsg[n + 8] = 0; }
		return;
	}
	cr = (cr & ~(0x80000000u >> d)) | (r << (31 - d));
}

void Machine::op31 (u32 op)
{
	u32 d = RD, a = RA, b = RB, ea;
	u32 xo = (op >> 1) & 0x3FF;
	bool oe = op & 0x400;
	// the XO-form arithmetic (9-bit opcode + OE)
	switch ((op >> 1) & 0x1FF)
	{
	case 266: case 10: case 138: case 234: case 202:		// add, addc, adde, addme, addze
	case 40: case 8: case 136: case 232: case 200:			// subf, subfc, subfe, subfme, subfze
	{
		u32 x9 = (op >> 1) & 0x1FF;
		u32 ra = gpr[a], rb = gpr[b], ca = (xer >> 29) & 1;
		u64 s;
		bool sub = x9 == 40 || x9 == 8 || x9 == 136 || x9 == 232 || x9 == 200;
		u32 x = sub ? ~ra : ra;
		u32 y = (x9 == 234 || x9 == 232) ? 0xFFFFFFFFu : (x9 == 202 || x9 == 200) ? 0 : rb;
		u32 cin = (x9 == 266 || x9 == 10) ? 0 : (x9 == 40 || x9 == 8) ? 1 : ca;
		s = (u64) x + y + cin;
		u32 r = (u32) s;
		if (x9 != 266 && x9 != 40) xer = (s >> 32) ? xer | XER_CA : xer & ~XER_CA;
		if (oe)
		{
			bool ov = (~(x ^ y) & (x ^ r)) >> 31;
			xer = ov ? xer | XER_OV | XER_SO : xer & ~XER_OV;
		}
		gpr[d] = r;
		if (RCBIT) setCr0 (r);
		return;
	}
	case 104:							// neg
	{
		u32 r = 0u - gpr[a];
		if (oe) xer = gpr[a] == 0x80000000u ? xer | XER_OV | XER_SO : xer & ~XER_OV;
		gpr[d] = r;
		if (RCBIT) setCr0 (r);
		return;
	}
	case 235:							// mullw
	{
		s64 p = (s64) (s32) gpr[a] * (s32) gpr[b];
		if (oe) xer = (p < -2147483648LL || p > 2147483647LL) ? xer | XER_OV | XER_SO : xer & ~XER_OV;
		gpr[d] = (u32) p;
		if (RCBIT) setCr0 (gpr[d]);
		return;
	}
	case 75: gpr[d] = (u32) (((s64) (s32) gpr[a] * (s32) gpr[b]) >> 32); if (RCBIT) setCr0 (gpr[d]); return;	// mulhw
	case 11: gpr[d] = (u32) (((u64) gpr[a] * gpr[b]) >> 32); if (RCBIT) setCr0 (gpr[d]); return;			// mulhwu
	case 491:							// divw
	{
		s32 x = (s32) gpr[a], y = (s32) gpr[b];
		bool bad = y == 0 || (x == (s32) 0x80000000 && y == -1);
		if (bad) gpr[d] = (x < 0 && y == 0) ? 0xFFFFFFFFu : (y == 0 ? 0 : 0xFFFFFFFFu);
		else gpr[d] = (u32) (x / y);
		if (oe) xer = bad ? xer | XER_OV | XER_SO : xer & ~XER_OV;
		if (RCBIT) setCr0 (gpr[d]);
		return;
	}
	case 459:							// divwu
	{
		bool bad = gpr[b] == 0;
		gpr[d] = bad ? 0 : gpr[a] / gpr[b];
		if (oe) xer = bad ? xer | XER_OV | XER_SO : xer & ~XER_OV;
		if (RCBIT) setCr0 (gpr[d]);
		return;
	}
	}
	switch (xo)
	{
	case 0: case 32:						// cmp, cmpl
	{
		u32 f;
		if (xo == 0) { s32 x = (s32) gpr[a], y = (s32) gpr[b]; f = x < y ? 8 : x > y ? 4 : 2; }
		else { u32 x = gpr[a], y = gpr[b]; f = x < y ? 8 : x > y ? 4 : 2; }
		int crf = (int) (d >> 2);
		cr = (cr & ~(0xF0000000u >> (crf * 4))) | ((f | (xer >> 31)) << (28 - crf * 4));
		return;
	}
	case 4:								// tw
	{
		s32 x = (s32) gpr[a], y = (s32) gpr[b]; u32 to = d;
		if (((to & 16) && x < y) || ((to & 8) && x > y) || ((to & 4) && x == y) || ((to & 2) && (u32) x < (u32) y) || ((to & 1) && (u32) x > (u32) y))
		{ exception (0x700, curPc); srr1 |= 0x20000; }
		return;
	}
	case 28: gpr[a] = gpr[d] & gpr[b]; break;			// and
	case 60: gpr[a] = gpr[d] & ~gpr[b]; break;			// andc
	case 444: gpr[a] = gpr[d] | gpr[b]; break;			// or
	case 412: gpr[a] = gpr[d] | ~gpr[b]; break;			// orc
	case 316: gpr[a] = gpr[d] ^ gpr[b]; break;			// xor
	case 476: gpr[a] = ~(gpr[d] & gpr[b]); break;			// nand
	case 124: gpr[a] = ~(gpr[d] | gpr[b]); break;			// nor
	case 284: gpr[a] = ~(gpr[d] ^ gpr[b]); break;			// eqv
	case 24: { u32 n = gpr[b] & 63; gpr[a] = n > 31 ? 0 : gpr[d] << n; break; }	// slw
	case 536: { u32 n = gpr[b] & 63; gpr[a] = n > 31 ? 0 : gpr[d] >> n; break; }	// srw
	case 792:							// sraw
	{
		u32 n = gpr[b] & 63; s32 v = (s32) gpr[d];
		if (n > 31) { gpr[a] = (u32) (v >> 31); xer = v < 0 ? xer | XER_CA : xer & ~XER_CA; }
		else
		{
			gpr[a] = (u32) (v >> n);
			xer = (v < 0 && n && (gpr[d] << (32 - n))) ? xer | XER_CA : xer & ~XER_CA;
		}
		break;
	}
	case 824:							// srawi
	{
		u32 n = b; s32 v = (s32) gpr[d];
		gpr[a] = (u32) (v >> n);
		xer = (v < 0 && n && (gpr[d] << (32 - n))) ? xer | XER_CA : xer & ~XER_CA;
		break;
	}
	case 26: gpr[a] = gpr[d] ? (u32) __builtin_clz (gpr[d]) : 32; break;	// cntlzw
	case 954: gpr[a] = (u32) (s32) (s8) gpr[d]; break;		// extsb
	case 922: gpr[a] = (u32) (s32) (s16) gpr[d]; break;		// extsh
	case 19: gpr[d] = cr; return;					// mfcr
	case 144:							// mtcrf
	{
		u32 crm = (op >> 12) & 0xFF, m = 0;
		for (int i = 0; i < 8; i++) if (crm & (0x80 >> i)) m |= 0xF0000000u >> (i * 4);
		cr = (cr & ~m) | (gpr[d] & m);
		return;
	}
	case 512:							// mcrxr
	{
		int crf = (int) (d >> 2);
		cr = (cr & ~(0xF0000000u >> (crf * 4))) | ((xer >> 28) << (28 - crf * 4));
		xer &= 0x0FFFFFFF;
		return;
	}
	case 83: gpr[d] = msr; return;					// mfmsr
	case 146: msr = gpr[d]; return;					// mtmsr (interrupts: looked at before the next one)
	case 339: gpr[d] = mfspr (((op >> 16) & 31) | ((op >> 6) & 0x3E0)); return;	// mfspr
	case 467: mtspr (((op >> 16) & 31) | ((op >> 6) & 0x3E0), gpr[d]); return;	// mtspr
	case 371: gpr[d] = mfspr (((op >> 16) & 31) | ((op >> 6) & 0x3E0)); return;	// mftb
	case 595: gpr[d] = sr[a & 15]; return;				// mfsr
	case 659: gpr[d] = sr[gpr[b] >> 28]; return;			// mfsrin
	case 210: sr[a & 15] = gpr[d]; return;				// mtsr
	case 242: sr[gpr[b] >> 28] = gpr[d]; return;			// mtsrin
	case 86: case 54: case 278: case 246: case 598: case 854: case 306: case 566: return;	// dcbf dcbst dcbt dcbtst sync eieio tlbie tlbsync
	case 982:							// icbi: the JIT's code there is stale
	{
		u32 pa;
		if (translate (RA0 + gpr[b], pa, false, false)) jitInvalidate (pa & ~31u, 32);
		return;
	}
	case 470: return;						// dcbi (the data is kept)
	case 1014:							// dcbz
	{
		ea = (RA0 + gpr[b]) & ~31u;
		for (u32 i = 0; i < 32; i += 4) { write32 (ea + i, 0); if (memFault) return; }
		return;
	}
	case 20:							// lwarx
		ea = RA0 + gpr[b];
		LOADG (d, read32 (ea));
		resv = true; resvAddr = ea;
		return;
	case 150:							// stwcx.
		ea = RA0 + gpr[b];
		if (resv) { write32 (ea, gpr[d]); if (memFault) return; resv = false; cr = (cr & 0x0FFFFFFF) | ((2 | (xer >> 31)) << 28); }
		else cr = (cr & 0x0FFFFFFF) | ((xer >> 31) << 28);
		return;
	case 23: LOADG (d, read32 (RA0 + gpr[b])); return;		// lwzx
	case 55: ea = gpr[a] + gpr[b]; LOADG (d, read32 (ea)); gpr[a] = ea; return;
	case 87: LOADG (d, read8 (RA0 + gpr[b])); return;		// lbzx
	case 119: ea = gpr[a] + gpr[b]; LOADG (d, read8 (ea)); gpr[a] = ea; return;
	case 279: LOADG (d, read16 (RA0 + gpr[b])); return;		// lhzx
	case 311: ea = gpr[a] + gpr[b]; LOADG (d, read16 (ea)); gpr[a] = ea; return;
	case 343: LOADG (d, (u32) (s32) (s16) read16 (RA0 + gpr[b])); return;	// lhax
	case 375: ea = gpr[a] + gpr[b]; LOADG (d, (u32) (s32) (s16) read16 (ea)); gpr[a] = ea; return;
	case 534: LOADG (d, bswap32 (read32 (RA0 + gpr[b]))); return;	// lwbrx
	case 790: LOADG (d, bswap16 (read16 (RA0 + gpr[b]))); return;	// lhbrx
	case 151: write32 (RA0 + gpr[b], gpr[d]); return;		// stwx
	case 183: ea = gpr[a] + gpr[b]; write32 (ea, gpr[d]); if (!memFault) gpr[a] = ea; return;
	case 215: write8 (RA0 + gpr[b], (u8) gpr[d]); return;		// stbx
	case 247: ea = gpr[a] + gpr[b]; write8 (ea, (u8) gpr[d]); if (!memFault) gpr[a] = ea; return;
	case 407: write16 (RA0 + gpr[b], (u16) gpr[d]); return;		// sthx
	case 439: ea = gpr[a] + gpr[b]; write16 (ea, (u16) gpr[d]); if (!memFault) gpr[a] = ea; return;
	case 662: write32 (RA0 + gpr[b], bswap32 (gpr[d])); return;	// stwbrx
	case 918: write16 (RA0 + gpr[b], bswap16 ((u16) gpr[d])); return;	// sthbrx
	case 597: case 533:						// lswi, lswx
	{
		u32 n = xo == 597 ? (b ? b : 32) : (xer & 0x7F);
		ea = xo == 597 ? RA0 : RA0 + gpr[b];
		u32 r = d - 1, shift = 0;
		for (u32 i = 0; i < n; i++)
		{
			if (shift == 0) { r = (r + 1) & 31; gpr[r] = 0; }
			u32 v = read8 (ea + i); if (memFault) return;
			gpr[r] |= v << (24 - shift);
			shift = (shift + 8) & 31;
		}
		return;
	}
	case 725: case 661:						// stswi, stswx
	{
		u32 n = xo == 725 ? (b ? b : 32) : (xer & 0x7F);
		ea = xo == 725 ? RA0 : RA0 + gpr[b];
		u32 r = d - 1, shift = 0;
		for (u32 i = 0; i < n; i++)
		{
			if (shift == 0) r = (r + 1) & 31;
			write8 (ea + i, (u8) (gpr[r] >> (24 - shift))); if (memFault) return;
			shift = (shift + 8) & 31;
		}
		return;
	}
	case 535: case 567:						// lfsx, lfsux
	{
		FPCHECK
		ea = (xo == 567 ? gpr[a] : RA0) + gpr[b];
		u32 v = read32 (ea); if (memFault) return;
		ps0[d] = ps1[d] = u2d (cvtToDouble (v));
		if (xo == 567) gpr[a] = ea;
		return;
	}
	case 599: case 631:						// lfdx, lfdux
	{
		FPCHECK
		ea = (xo == 631 ? gpr[a] : RA0) + gpr[b];
		u64 v = read64 (ea); if (memFault) return;
		ps0[d] = u2d (v);
		if (xo == 631) gpr[a] = ea;
		return;
	}
	case 663: case 695:						// stfsx, stfsux
		FPCHECK
		ea = (xo == 695 ? gpr[a] : RA0) + gpr[b];
		write32 (ea, cvtToSingle (d2u (ps0[d])));
		if (xo == 695 && !memFault) gpr[a] = ea;
		return;
	case 727: case 759:						// stfdx, stfdux
		FPCHECK
		ea = (xo == 759 ? gpr[a] : RA0) + gpr[b];
		write64 (ea, d2u (ps0[d]));
		if (xo == 759 && !memFault) gpr[a] = ea;
		return;
	case 983:							// stfiwx
		FPCHECK
		write32 (RA0 + gpr[b], (u32) d2u (ps0[d]));
		return;
	case 310: LOADG (d, read32 (RA0 + gpr[b])); return;		// eciwx
	case 438: write32 (RA0 + gpr[b], gpr[d]); return;		// ecowx
	default:
		halted = true;
		{ const char *m = "unknown op31 "; int n = 0; while (m[n]) { haltMsg[n] = m[n]; n++; } hex8 (haltMsg + n, op); n += 8; haltMsg[n++] = ' '; haltMsg[n++] = '@'; hex8 (haltMsg + n, curPc); haltMsg[n + 8] = 0; }
		return;
	}
	if (RCBIT) setCr0 (gpr[a]);					// (the logical ops, shifts, extends)
}

// ---- the floating point ----------------------------------------------------------------------------------------
static inline double fnegd (double x) { return u2d (d2u (x) ^ 0x8000000000000000ull); }
static inline double fabsd (double x) { return u2d (d2u (x) & 0x7FFFFFFFFFFFFFFFull); }
// a NaN result keeps the first NaN operand (quieted), as the 750
static inline double pickNaN (double r, double a, double b, double c)
{
	if (!isNaN (r)) return r;
	if (isNaN (a)) return u2d (d2u (a) | 0x0008000000000000ull);
	if (isNaN (b)) return u2d (d2u (b) | 0x0008000000000000ull);
	if (isNaN (c)) return u2d (d2u (c) | 0x0008000000000000ull);
	return u2d (0x7FF8000000000000ull);
}
static inline double fres (double x) { return roundSingle (1.0 / x); }
static inline double frsqrte (double x)
{
	if (x < 0.0 && !isNaN (x)) return u2d (0x7FF8000000000000ull);
	return 1.0 / __builtin_sqrt (x);
}

void Machine::op59 (u32 op)
{
	FPCHECK
	u32 d = RD, a = RA, b = RB, c = RC;
	double fa = ps0[a], fb = ps0[b], fc = ps0[c], r;
	switch ((op >> 1) & 0x1F)
	{
	case 18: r = fa / fb; break;					// fdivs
	case 20: r = fa - fb; break;					// fsubs
	case 21: r = fa + fb; break;					// fadds
	case 24: r = fres (fb); break;					// fres
	case 25: r = fa * force25 (fc); break;				// fmuls
	case 28: r = __builtin_fma (fa, force25 (fc), -fb); break;	// fmsubs
	case 29: r = __builtin_fma (fa, force25 (fc), fb); break;	// fmadds
	case 30: r = fnegd (__builtin_fma (fa, force25 (fc), -fb)); break;	// fnmsubs
	case 31: r = fnegd (__builtin_fma (fa, force25 (fc), fb)); break;	// fnmadds
	default:
		halted = true;
		{ const char *m = "unknown op59 "; int n = 0; while (m[n]) { haltMsg[n] = m[n]; n++; } hex8 (haltMsg + n, op); haltMsg[n + 8] = 0; }
		return;
	}
	r = roundSingle (pickNaN (r, fa, fb, fc));
	ps0[d] = ps1[d] = r;
	setFprf (r);
	if (RCBIT) setCr1 ();
}

void Machine::op63 (u32 op)
{
	FPCHECK
	u32 d = RD, a = RA, b = RB, c = RC;
	double fa = ps0[a], fb = ps0[b], fc = ps0[c], r;
	switch ((op >> 1) & 0x1F)
	{
	case 18: r = fa / fb; goto arith;				// fdiv
	case 20: r = fa - fb; goto arith;				// fsub
	case 21: r = fa + fb; goto arith;				// fadd
	case 25: r = fa * fc; goto arith;				// fmul
	case 28: r = __builtin_fma (fa, fc, -fb); goto arith;		// fmsub
	case 29: r = __builtin_fma (fa, fc, fb); goto arith;		// fmadd
	case 30: r = fnegd (__builtin_fma (fa, fc, -fb)); goto arith;		// fnmsub
	case 31: r = fnegd (__builtin_fma (fa, fc, fb)); goto arith;		// fnmadd
	case 26: r = frsqrte (fb); goto arith;				// frsqrte
	case 23: ps0[d] = (fa >= 0.0) ? fc : fb; if (RCBIT) setCr1 (); return;	// fsel (NaN -> fb)
	}
	switch ((op >> 1) & 0x3FF)
	{
	case 0: case 32:						// fcmpu, fcmpo
	{
		u32 f = isNaN (fa) || isNaN (fb) ? 1 : fa < fb ? 8 : fa > fb ? 4 : 2;
		int crf = (int) (d >> 2);
		cr = (cr & ~(0xF0000000u >> (crf * 4))) | (f << (28 - crf * 4));
		fpscr = (fpscr & ~0xF000u) | (f << 12);
		return;
	}
	case 12:							// frsp
		r = roundSingle (fb);
		ps0[d] = r; setFprf (r);
		if (RCBIT) setCr1 ();
		return;
	case 14: case 15:						// fctiw, fctiwz
	{
		double x = fb; s32 i;
		int rn = ((op >> 1) & 0x3FF) == 15 ? 1 : (int) (fpscr & 3);
		if (isNaN (x)) i = (s32) 0x80000000;
		else
		{
			double t = rn == 0 ? __builtin_rint (x) : rn == 1 ? __builtin_trunc (x) : rn == 2 ? __builtin_ceil (x) : __builtin_floor (x);
			if (t >= 2147483648.0) i = 0x7FFFFFFF;
			else if (t < -2147483648.0) i = (s32) 0x80000000;
			else i = (s32) t;
		}
		ps0[d] = u2d (0xFFF8000000000000ull | (u32) i);
		if (RCBIT) setCr1 ();
		return;
	}
	case 40: ps0[d] = fnegd (fb); break;				// fneg
	case 72: ps0[d] = fb; break;					// fmr
	case 136: ps0[d] = fnegd (fabsd (fb)); break;			// fnabs
	case 264: ps0[d] = fabsd (fb); break;				// fabs
	case 583: ps0[d] = u2d ((u64) fpscr); break;			// mffs
	case 711:							// mtfsf
	{
		u32 fm = (op >> 17) & 0xFF, m = 0;
		for (int i = 0; i < 8; i++) if (fm & (0x80 >> i)) m |= 0xF0000000u >> (i * 4);
		fpscr = (fpscr & ~m) | ((u32) d2u (fb) & m);
		break;
	}
	case 134:							// mtfsfi
	{
		int crf = (int) (d >> 2); u32 imm = (op >> 12) & 15;
		fpscr = (fpscr & ~(0xF0000000u >> (crf * 4))) | (imm << (28 - crf * 4));
		break;
	}
	case 38: fpscr |= 0x80000000u >> d; break;			// mtfsb1
	case 70: fpscr &= ~(0x80000000u >> d); break;			// mtfsb0
	case 64:							// mcrfs
	{
		u32 f = (fpscr >> (28 - (a >> 2) * 4)) & 15;
		cr = (cr & ~(0xF0000000u >> ((d >> 2) * 4))) | (f << (28 - (d >> 2) * 4));
		fpscr &= ~((0xF0000000u >> ((a >> 2) * 4)) & 0x9FF80700u);	// (the exception bits it reads are cleared)
		return;
	}
	default:
		halted = true;
		{ const char *m = "unknown op63 "; int n = 0; while (m[n]) { haltMsg[n] = m[n]; n++; } hex8 (haltMsg + n, op); haltMsg[n + 8] = 0; }
		return;
	}
	if (RCBIT) setCr1 ();
	return;
arith:
	r = pickNaN (r, fa, fb, fc);
	ps0[d] = r;
	setFprf (r);
	if (RCBIT) setCr1 ();
}

// ---- the paired singles ----------------------------------------------------------------------------------------
void Machine::op4 (u32 op)
{
	FPCHECK
	u32 d = RD, a = RA, b = RB, c = RC;
	double a0 = ps0[a], a1 = ps1[a], b0 = ps0[b], b1 = ps1[b], c0 = ps0[c], c1 = ps1[c], r0, r1;
	switch ((op >> 1) & 0x1F)
	{
	case 10: r0 = a0 + b1; r1 = c1; goto single0;			// ps_sum0
	case 11: r0 = c0; r1 = a0 + b1; goto single1;			// ps_sum1
	case 12: r0 = a0 * force25 (c0); r1 = a1 * force25 (c0); goto both;	// ps_muls0
	case 13: r0 = a0 * force25 (c1); r1 = a1 * force25 (c1); goto both;	// ps_muls1
	case 14: r0 = __builtin_fma (a0, force25 (c0), b0); r1 = __builtin_fma (a1, force25 (c0), b1); goto both;	// ps_madds0
	case 15: r0 = __builtin_fma (a0, force25 (c1), b0); r1 = __builtin_fma (a1, force25 (c1), b1); goto both;	// ps_madds1
	case 18: r0 = a0 / b0; r1 = a1 / b1; goto both;			// ps_div
	case 20: r0 = a0 - b0; r1 = a1 - b1; goto both;			// ps_sub
	case 21: r0 = a0 + b0; r1 = a1 + b1; goto both;			// ps_add
	case 23: ps0[d] = a0 >= 0.0 ? c0 : b0; ps1[d] = a1 >= 0.0 ? c1 : b1; if (RCBIT) setCr1 (); return;	// ps_sel
	case 24: r0 = fres (b0); r1 = fres (b1); goto both;		// ps_res
	case 25: r0 = a0 * force25 (c0); r1 = a1 * force25 (c1); goto both;	// ps_mul
	case 26: r0 = frsqrte (b0); r1 = frsqrte (b1); goto both;	// ps_rsqrte
	case 28: r0 = __builtin_fma (a0, force25 (c0), -b0); r1 = __builtin_fma (a1, force25 (c1), -b1); goto both;	// ps_msub
	case 29: r0 = __builtin_fma (a0, force25 (c0), b0); r1 = __builtin_fma (a1, force25 (c1), b1); goto both;	// ps_madd
	case 30: r0 = fnegd (__builtin_fma (a0, force25 (c0), -b0)); r1 = fnegd (__builtin_fma (a1, force25 (c1), -b1)); goto both;	// ps_nmsub
	case 31: r0 = fnegd (__builtin_fma (a0, force25 (c0), b0)); r1 = fnegd (__builtin_fma (a1, force25 (c1), b1)); goto both;	// ps_nmadd
	}
	switch ((op >> 1) & 0x3F)					// the indexed quantized loads / stores
	{
	case 6: case 38: case 7: case 39:
	{
		u32 x6 = (op >> 1) & 0x3F;
		u32 ea = ((x6 == 38 || x6 == 39) ? gpr[a] : RA0) + gpr[b];
		u32 g = gqr[(op >> 7) & 7]; bool w = (op >> 10) & 1;
		int size;
		if (x6 == 6 || x6 == 38)
		{
			int type = (int) (g >> 16) & 7, scale = (int) (g >> 24) & 63;
			double x = quantLoad (ea, type, scale, size);
			double y = w ? 1.0 : quantLoad (ea + (u32) size, type, scale, size);
			if (memFault) return;
			ps0[d] = x; ps1[d] = y;
		}
		else
		{
			int type = (int) g & 7, scale = (int) (g >> 8) & 63;
			quantStore (ea, ps0[d], type, scale, size);
			if (!w && !memFault) quantStore (ea + (u32) size, ps1[d], type, scale, size);
			if (memFault) return;
		}
		if (x6 == 38 || x6 == 39) gpr[a] = ea;
		return;
	}
	}
	switch ((op >> 1) & 0x3FF)
	{
	case 0: case 32: case 64: case 96:				// ps_cmpu0, ps_cmpo0, ps_cmpu1, ps_cmpo1
	{
		bool hi = ((op >> 1) & 0x3FF) >= 64;
		double x = hi ? a1 : a0, y = hi ? b1 : b0;
		u32 f = isNaN (x) || isNaN (y) ? 1 : x < y ? 8 : x > y ? 4 : 2;
		int crf = (int) (d >> 2);
		cr = (cr & ~(0xF0000000u >> (crf * 4))) | (f << (28 - crf * 4));
		fpscr = (fpscr & ~0xF000u) | (f << 12);
		return;
	}
	case 40: ps0[d] = fnegd (b0); ps1[d] = fnegd (b1); break;	// ps_neg
	case 72: ps0[d] = b0; ps1[d] = b1; break;			// ps_mr
	case 136: ps0[d] = fnegd (fabsd (b0)); ps1[d] = fnegd (fabsd (b1)); break;	// ps_nabs
	case 264: ps0[d] = fabsd (b0); ps1[d] = fabsd (b1); break;	// ps_abs
	case 528: ps0[d] = a0; ps1[d] = b0; break;			// ps_merge00
	case 560: ps0[d] = a0; ps1[d] = b1; break;			// ps_merge01
	case 592: ps0[d] = a1; ps1[d] = b0; break;			// ps_merge10
	case 624: ps0[d] = a1; ps1[d] = b1; break;			// ps_merge11
	case 1014:							// dcbz_l
	{
		u32 ea = (RA0 + gpr[b]) & ~31u;
		for (u32 i = 0; i < 32; i += 4) { write32 (ea + i, 0); if (memFault) return; }
		return;
	}
	default:
		halted = true;
		{ const char *m = "unknown op4 "; int n = 0; while (m[n]) { haltMsg[n] = m[n]; n++; } hex8 (haltMsg + n, op); haltMsg[n + 8] = 0; }
		return;
	}
	if (RCBIT) setCr1 ();
	return;
single0:
	ps0[d] = roundSingle (pickNaN (r0, a0, b1, 0.0)); ps1[d] = r1;
	setFprf (ps0[d]);
	if (RCBIT) setCr1 ();
	return;
single1:
	ps0[d] = r0; ps1[d] = roundSingle (pickNaN (r1, a0, b1, 0.0));
	setFprf (ps1[d]);
	if (RCBIT) setCr1 ();
	return;
both:
	ps0[d] = roundSingle (pickNaN (r0, a0, b0, c0));
	ps1[d] = roundSingle (pickNaN (r1, a1, b1, c1));
	setFprf (ps0[d]);
	if (RCBIT) setCr1 ();
}

} // namespace gc
