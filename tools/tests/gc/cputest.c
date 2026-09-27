/*
 * cputest.c -- a battery of PowerPC instructions for the Gekko interpreter (user/gc): each is
 * run on pseudo-random operands and its result, CR and XER written out. The same object runs
 * under qemu-ppc (the reference: cputest_main.c prints the buffer) and in the interpreter
 * (tools/tests/gc/gctest.cpp loads it at 0x80003100): the buffers must match.
 * (No libc, no 64-bit arithmetic: nothing the linker would have to bring.)
 */
typedef unsigned int u32;

static u32 seed;
static u32 rnd (void)
{
	seed = seed * 1664525u + 1013904223u;
	u32 r = seed >> 8;
	seed = seed * 1664525u + 1013904223u;
	r ^= seed << 8;
	switch ((seed >> 28) & 7)			/* now and then a special value */
	{
	case 0: return 0;
	case 1: return 0x80000000u;
	case 2: return 0xFFFFFFFFu;
	case 3: return 0x7FFFFFFFu;
	case 4: return r & 0xFF;
	}
	return r;
}

#define N 24

/* rD = insn rA, rB with XER / CR set before; out: rD, CR, XER */
#define T3(insn) *o++ = 0xA5A50000u | __LINE__; for (int i = 0; i < N; i++) { \
	u32 a = rnd (), b = rnd (), xi = rnd () & 0xE0000000u, ci = rnd (), r, c, x; \
	__asm__ volatile ("mtxer %5\n\tmtcrf 255,%6\n\t" insn " %0,%3,%4\n\tmfcr %1\n\tmfxer %2" \
		: "=&r" (r), "=&r" (c), "=&r" (x) : "r" (a), "r" (b), "r" (xi), "r" (ci)); \
	*o++ = r; *o++ = c; *o++ = x; }
/* the same, the operands written in insn */
#define T3X(insn) *o++ = 0xA5A50000u | __LINE__; for (int i = 0; i < N; i++) { \
	u32 a = rnd (), b = rnd (), xi = rnd () & 0xE0000000u, ci = rnd (), r, c, x; \
	__asm__ volatile ("mtxer %5\n\tmtcrf 255,%6\n\t" insn "\n\tmfcr %1\n\tmfxer %2" \
		: "=&r" (r), "=&r" (c), "=&r" (x) : "r" (a), "r" (b), "r" (xi), "r" (ci)); \
	*o++ = r; *o++ = c; *o++ = x; }
/* rD = insn rA */
#define T2(insn) *o++ = 0xA5A50000u | __LINE__; for (int i = 0; i < N; i++) { \
	u32 a = rnd (), xi = rnd () & 0xE0000000u, ci = rnd (), r, c, x; \
	__asm__ volatile ("mtxer %4\n\tmtcrf 255,%5\n\t" insn " %0,%3\n\tmfcr %1\n\tmfxer %2" \
		: "=&r" (r), "=&r" (c), "=&r" (x) : "r" (a), "r" (xi), "r" (ci)); \
	*o++ = r; *o++ = c; *o++ = x; }
/* rD = insn rA, imm (the immediate written in insn) */
#define TI(insn) *o++ = 0xA5A50000u | __LINE__; for (int i = 0; i < N; i++) { \
	u32 a = rnd (), xi = rnd () & 0xE0000000u, ci = rnd (), r, c, x; \
	__asm__ volatile ("mtxer %4\n\tmtcrf 255,%5\n\t" insn "\n\tmfcr %1\n\tmfxer %2" \
		: "=&r" (r), "=&r" (c), "=&r" (x) : "r" (a), "r" (xi), "r" (ci)); \
	*o++ = r; *o++ = c; *o++ = x; }
/* rlwimi: rD is also an input */
#define TM(insn) *o++ = 0xA5A50000u | __LINE__; for (int i = 0; i < N; i++) { \
	u32 a = rnd (), r = rnd (), ci = rnd (), c; \
	__asm__ volatile ("mtcrf 255,%3\n\t" insn "\n\tmfcr %1" : "+r" (r), "=&r" (c) : "r" (a), "r" (ci)); \
	*o++ = r; *o++ = c; }
/* compares into cr field 3 (and cr0 via the second operand pair) */
#define TC(insn) *o++ = 0xA5A50000u | __LINE__; for (int i = 0; i < N; i++) { \
	u32 a = rnd (), b = (i & 3) ? rnd () : a, xi = rnd () & 0x80000000u, ci = rnd (), c; \
	__asm__ volatile ("mtxer %3\n\tmtcrf 255,%4\n\t" insn "\n\tmfcr %0" : "=&r" (c) : "r" (a), "r" (b), "r" (xi), "r" (ci)); \
	*o++ = c; }

/* the doubles the floating point tests use (bits) */
static const u32 fvals[] = {
	0x3FF00000, 0x00000000,  0xBFF00000, 0x00000000,  0x40091EB8, 0x51EB851F,  0xC0240000, 0x00000000,
	0x00000000, 0x00000000,  0x80000000, 0x00000000,  0x7FF00000, 0x00000000,  0xFFF00000, 0x00000000,
	0x3E500000, 0x00000000,  0x47EFFFFF, 0xE0000000,  0x3FD55555, 0x55555555,  0x41DFFFFF, 0xFFC00000,
	0xC1E00000, 0x00000000,  0x3FE00000, 0x00000000,  0x40590000, 0x00000000,  0x3F1A36E2, 0xEB1C432D,
	0x7FEFFFFF, 0xFFFFFFFF,  0x00000000, 0x00000001,  0x3FF80000, 0x00000000,  0x40490FDB, 0x40000000,
	0xC0C38800, 0x00000000,  0x37F00000, 0x00000000,  0x3FEFFFFF, 0xFFFFFFFF,  0x41600000, 0x00000001,
};
#define NF (sizeof fvals / 8)
/* single-representable values (for the single-precision multiplies: no 25-bit rounding) */
static const u32 svals[] = {
	0x3FF00000, 0x00000000,  0xBFF80000, 0x00000000,  0x40091EB8, 0x60000000,  0x3FD55555, 0x40000000,
	0x40590000, 0x00000000,  0xC0240000, 0x00000000,  0x3F1A36E2, 0xE0000000,  0x00000000, 0x00000000,
	0x47EFFFFF, 0xE0000000,  0x3E500000, 0x00000000,
};
#define NS (sizeof svals / 8)

static double fv (const u32 *t, int i) { union { u32 w[2]; double d; } u; u.w[0] = t[i * 2]; u.w[1] = t[i * 2 + 1]; return u.d; }

/* fD = insn fA, fB (/ fC): out the double's bits and FPSCR's FPRF */
#define F3(insn, tab, n) *o++ = 0xA5A50000u | __LINE__; for (int i = 0; i < 40; i++) { \
	double a = fv (tab, (int) (rnd () % n)), b = fv (tab, (int) (rnd () % n)), r; union { double d; u32 w[2]; } u, s; \
	__asm__ volatile (insn " %0,%2,%3\n\tmffs %1" : "=&f" (r), "=&f" (s.d) : "f" (a), "f" (b)); \
	u.d = r; *o++ = u.w[0]; *o++ = u.w[1]; *o++ = s.w[1] & 0x1F000; }
#define F4(insn, tab, n) *o++ = 0xA5A50000u | __LINE__; for (int i = 0; i < 40; i++) { \
	double a = fv (tab, (int) (rnd () % n)), b = fv (tab, (int) (rnd () % n)), c = fv (tab, (int) (rnd () % n)), r; union { double d; u32 w[2]; } u, s; \
	__asm__ volatile (insn " %0,%2,%3,%4\n\tmffs %1" : "=&f" (r), "=&f" (s.d) : "f" (a), "f" (c), "f" (b)); \
	u.d = r; *o++ = u.w[0]; *o++ = u.w[1]; *o++ = s.w[1] & 0x1F000; }
#define F2(insn, tab, n, maskLo) *o++ = 0xA5A50000u | __LINE__; for (int i = 0; i < (int) n; i++) { \
	double b = fv (tab, i), r; union { double d; u32 w[2]; } u; \
	__asm__ volatile (insn " %0,%1" : "=&f" (r) : "f" (b)); \
	u.d = r; *o++ = u.w[0] & (maskLo ? 0 : 0xFFFFFFFF); *o++ = u.w[1]; }

int run_tests (u32 *out)
{
	u32 *o = out;
	seed = 12345;
	/* the integer unit */
	T3 ("add")
	T3 ("add.")
	T3 ("addo")
	T3 ("addo.")
	T3 ("addc")
	T3 ("addc.")
	T3 ("addco")
	T3 ("addco.")
	T3 ("adde")
	T3 ("adde.")
	T3 ("addeo")
	T3 ("addeo.")
	T3 ("subf")
	T3 ("subf.")
	T3 ("subfo")
	T3 ("subfo.")
	T3 ("subfc")
	T3 ("subfc.")
	T3 ("subfco")
	T3 ("subfco.")
	T3 ("subfe")
	T3 ("subfe.")
	T3 ("subfeo")
	T3 ("subfeo.")
	T3 ("mullw")
	T3 ("mullw.")
	T3 ("mullwo")
	T3 ("mullwo.")
	T3 ("mulhw")
	T3 ("mulhw.")
	T3 ("mulhwu")
	T3 ("mulhwu.")
	T3 ("and")
	T3 ("and.")
	T3 ("andc")
	T3 ("andc.")
	T3 ("or")
	T3 ("or.")
	T3 ("orc")
	T3 ("orc.")
	T3 ("xor")
	T3 ("xor.")
	T3 ("nand")
	T3 ("nand.")
	T3 ("nor")
	T3 ("nor.")
	T3 ("eqv")
	T3 ("eqv.")
	T3 ("slw")
	T3 ("slw.")
	T3 ("srw")
	T3 ("srw.")
	T3 ("sraw")
	T3 ("sraw.")
	T2 ("addme")
	T2 ("addme.")
	T2 ("addmeo")
	T2 ("addze")
	T2 ("addze.")
	T2 ("addzeo")
	T2 ("subfme")
	T2 ("subfme.")
	T2 ("subfmeo")
	T2 ("subfze")
	T2 ("subfze.")
	T2 ("subfzeo")
	T2 ("neg")
	T2 ("neg.")
	T2 ("nego")
	T2 ("nego.")
	T2 ("cntlzw")
	T2 ("cntlzw.")
	T2 ("extsb")
	T2 ("extsb.")
	T2 ("extsh")
	T2 ("extsh.")
	TI ("addi %0,%3,-1234")
	TI ("addis %0,%3,0x7FFF")
	TI ("addic %0,%3,-1")
	TI ("addic. %0,%3,1000")
	TI ("subfic %0,%3,77")
	TI ("subfic %0,%3,-1")
	TI ("subfic %0,%3,0")
	TI ("mulli %0,%3,-3001")
	TI ("andi. %0,%3,0x8F0F")
	TI ("andis. %0,%3,0xF00F")
	TI ("ori %0,%3,0x1234")
	TI ("oris %0,%3,0x8001")
	TI ("xori %0,%3,0xFFFF")
	TI ("xoris %0,%3,0x5555")
	TI ("srawi %0,%3,0")
	TI ("srawi %0,%3,1")
	TI ("srawi. %0,%3,5")
	TI ("srawi %0,%3,31")
	TI ("rlwinm %0,%3,0,0,31")
	TI ("rlwinm %0,%3,5,10,20")
	TI ("rlwinm. %0,%3,31,28,3")
	TI ("rlwinm %0,%3,16,16,31")
	TI ("rlwinm %0,%3,3,0,28")
	TI ("rlwinm %0,%3,29,3,31")
	TM ("rlwimi %0,%2,8,4,19")
	TM ("rlwimi. %0,%2,0,24,7")
	TM ("rlwimi %0,%2,31,0,0")
	T3X ("rlwnm %0,%3,%4,4,27")
	T3X ("rlwnm. %0,%3,%4,30,1")
	/* division: no zero divisor, no 0x80000000 / -1 (undefined results) */
	for (int i = 0; i < N * 4; i++)
	{
		u32 a = rnd (), b = rnd (), r1, r2, c;
		if (b == 0 || (a == 0x80000000u && b == 0xFFFFFFFFu)) b = 7;
		__asm__ volatile ("mtxer %4\n\tdivw. %0,%3,%5\n\tdivwu %1,%3,%5\n\tmfcr %2" : "=&r" (r1), "=&r" (r2), "=&r" (c) : "r" (a), "r" (0), "r" (b));
		*o++ = r1; *o++ = r2; *o++ = c;
	}
	TC ("cmpw 3,%1,%2")
	TC ("cmplw 3,%1,%2")
	TC ("cmpwi 5,%1,-5")
	TC ("cmplwi 7,%1,500")
	TC ("cmpw %1,%2")
	/* the CR logic */
	for (int i = 0; i < N; i++)
	{
		u32 ci = rnd (), c;
		__asm__ volatile ("mtcrf 255,%1\n\tcrand 0,5,31\n\tcrandc 1,6,30\n\tcreqv 2,7,29\n\tcrnand 3,8,28\n\t"
			"crnor 4,9,27\n\tcror 5,10,26\n\tcrorc 6,11,25\n\tcrxor 7,12,24\n\tmcrf 5,1\n\tmfcr %0" : "=&r" (c) : "r" (ci));
		*o++ = c;
	}
	/* the branch unit: a counted loop with bdnz, bc on CR bits */
	{
		u32 n = 0, k = 37;
		__asm__ volatile ("mtctr %1\n1:\taddi %0,%0,3\n\tbdnz 1b" : "+r" (n) : "r" (k) : "ctr");
		*o++ = n;
	}
	/* loads / stores: byte-reversed, update forms, multiple */
	{
		u32 buf[8], r1, r2, r3, r4;
		for (int i = 0; i < 8; i++) buf[i] = rnd ();
		u32 *p = buf;
		__asm__ volatile ("lwbrx %0,0,%4\n\tlhbrx %1,%4,%5\n\tlha %2,6(%4)\n\tlbzu %3,9(%4)" : "=&r" (r1), "=&r" (r2), "=&r" (r3), "=&r" (r4), "+b" (p) : "r" (4));
		*o++ = r1; *o++ = r2; *o++ = r3; *o++ = r4; *o++ = (u32) ((char *) p - (char *) buf);
		__asm__ volatile ("stwbrx %1,0,%0\n\tsthbrx %1,%0,%2\n\tstb %1,13(%0)" : : "b" (buf), "r" (0x12345678u), "r" (8) : "memory");
		for (int i = 0; i < 8; i++) *o++ = buf[i];
	}
	/* the floating point (double) */
	F3 ("fadd", fvals, NF)
	F3 ("fsub", fvals, NF)
	F3 ("fmul", fvals, NF)
	F3 ("fdiv", fvals, NF)
	F4 ("fmadd", fvals, NF)
	F4 ("fmsub", fvals, NF)
	F4 ("fnmadd", fvals, NF)
	F4 ("fnmsub", fvals, NF)
	/* single precision (multiplicands single-representable) */
	F3 ("fadds", fvals, NF)
	F3 ("fsubs", fvals, NF)
	F3 ("fdivs", fvals, NF)
	F3 ("fmuls", svals, NS)
	F4 ("fmadds", svals, NS)
	F4 ("fmsubs", svals, NS)
	F4 ("fnmadds", svals, NS)
	F4 ("fnmsubs", svals, NS)
	F2 ("frsp", fvals, NF, 0)
	F2 ("fneg", fvals, NF, 0)
	F2 ("fabs", fvals, NF, 0)
	F2 ("fnabs", fvals, NF, 0)
	F2 ("fmr", fvals, NF, 0)
	/* fctiw(z): the low word only (the high one is undefined) */
	for (int i = 0; i < (int) NF; i++)
	{
		union { double d; u32 w[2]; } u1, u2;
		double b = fv (fvals, i);
		__asm__ volatile ("fctiwz %0,%2\n\tfctiw %1,%2" : "=&f" (u1.d), "=&f" (u2.d) : "f" (b));
		*o++ = u1.w[1]; *o++ = u2.w[1];
	}
	/* fcmpu into cr6, fsel */
	for (int i = 0; i < 60; i++)
	{
		double a = fv (fvals, (int) (rnd () % NF)), b = fv (fvals, (int) (rnd () % NF)), r; u32 c; union { double d; u32 w[2]; } u;
		__asm__ volatile ("fcmpu 6,%2,%3\n\tmfcr %0\n\tfsel %1,%2,%3,%2" : "=&r" (c), "=&f" (r) : "f" (a), "f" (b));
		u.d = r; *o++ = c & 0xF0; *o++ = u.w[0]; *o++ = u.w[1];
	}
	/* single load / store conversions: denormals, NaNs, infinities (lfs -> stfd, lfd -> stfs) */
	{
		static const u32 sbits[] = { 0x00000001, 0x007FFFFF, 0x80400000, 0x7F800000, 0xFF800000, 0x7FC00001, 0x7F800001, 0x3F800000, 0x00800000 };
		for (int i = 0; i < 9; i++)
		{
			union { double d; u32 w[2]; } u; u32 s = sbits[i], back;
			__asm__ volatile ("lfs %0,0(%2)\n\tstfs %0,0(%3)" : "=&f" (u.d), "=m" (back) : "b" (&sbits[i]), "b" (&back) : "memory");
			*o++ = u.w[0]; *o++ = u.w[1]; *o++ = back; (void) s;
		}
		static const u32 dbits[] = { 0x36A00000, 0x00000000,  0x38000000, 0x00000001,  0x7FF00000, 0x00000001,  0x47F00000, 0x00000000,  0x380FFFFF, 0xF0000000 };
		for (int i = 0; i < 5; i++)
		{
			u32 back; double t;
			__asm__ volatile ("lfd %0,0(%2)\n\tstfs %0,0(%3)" : "=&f" (t), "=m" (back) : "b" (&dbits[i * 2]), "b" (&back) : "memory");
			*o++ = back;
		}
	}
	return (int) (o - out);
}
