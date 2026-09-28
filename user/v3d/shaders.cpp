//
// v3d/shaders.cpp -- see shaders.h. The recipes are those of the kernel's hand-written shaders
// (kernel/sys/v3d_shaders.qasm): the VPM read (ldvpmv_in) and written (stvpmv, then vpmwt), 1/W
// on the SFU (r4 three instructions later), a varying = ldvary x W + C (C in r5 two later), the
// TLB written with f16 pairs (vfpack) around the program's last thread switch.
//
#include "v3d/shaders.h"

namespace qpu
{

static void end (Prog &p)					// the program's end: thrsw + 2 delay slots
{
	p << I ().thrsw ();
	p << I ();
	p << I ();
}

void passVS (Prog &p, int n)
{
	p << I ().ldunifrf (rf (5));					// x scale
	p << I ().ldunifrf (rf (6));					// y scale
	p << I ().ldunifrf (rf (7));					// z scale
	p << I ().ldunifrf (rf (8));					// z offset
	p << I ().a (V3D_QPU_A_LDVPMV_IN, rf (4), imm (3));		// w first (read 3 instructions later)
	for (int k = 0; k < 3; k++) p << I ().a (V3D_QPU_A_LDVPMV_IN, rf (1 + k), imm (k));	// x y z
	p << I ().m (V3D_QPU_M_MOV, recip, rf (4));			// 1/w -> r4 (3 later)
	p << I ();
	p << I ();
	p << I ().m (V3D_QPU_M_FMUL, r0, rf (1), r4);			// x/w
	p << I ().m (V3D_QPU_M_FMUL, r1, rf (2), r4);			// y/w
	p << I ().m (V3D_QPU_M_FMUL, r2, rf (3), r4);			// z/w
	p << I ().m (V3D_QPU_M_FMUL, r0, r0, rf (5));
	p << I ().a (V3D_QPU_A_FTOIN, r0, r0).m (V3D_QPU_M_FMUL, r1, r1, rf (6));	// Xs
	p << I ().a (V3D_QPU_A_FTOIN, r1, r1).m (V3D_QPU_M_FMUL, r2, r2, rf (7));	// Ys
	p << I ().a (V3D_QPU_A_FADD, r2, r2, rf (8));			// Zs
	p << I ().a (V3D_QPU_A_STVPMV, imm (0), r0);
	p << I ().a (V3D_QPU_A_STVPMV, imm (1), r1);
	p << I ().a (V3D_QPU_A_STVPMV, imm (2), r2);
	p << I ().a (V3D_QPU_A_STVPMV, imm (3), r4);			// 1/Wc
	if (n > 4)							// the others: input i -> output i, read one ahead
	{								// (r3: the next read, r2: the next write; rf9 / rf10)
		p << I ().m (V3D_QPU_M_MOV, r3, imm (4));
		p << I ().m (V3D_QPU_M_MOV, r2, imm (4));
		for (int i = 4; i < n; i++)
		{
			p << I ().a (V3D_QPU_A_LDVPMV_IN, rf (9 + (i & 1)), r3).m (V3D_QPU_M_ADD, r3, r3, imm (1));
			if (i > 4) p << I ().a (V3D_QPU_A_STVPMV, r2, rf (9 + ((i - 1) & 1))).m (V3D_QPU_M_ADD, r2, r2, imm (1));
		}
		p << I ();							// (a register read not right after its write)
		p << I ().a (V3D_QPU_A_STVPMV, r2, rf (9 + ((n - 1) & 1)));
	}
	p << I ().a (V3D_QPU_A_VPMWT, nop);
	end (p);
}

void passCS (Prog &p)
{
	p << I ().ldunifrf (rf (5));					// x scale
	p << I ().ldunifrf (rf (6));					// y scale
	p << I ().a (V3D_QPU_A_LDVPMV_IN, rf (4), imm (3));		// w first
	for (int k = 0; k < 3; k++) p << I ().a (V3D_QPU_A_LDVPMV_IN, rf (1 + k), imm (k));
	p << I ().m (V3D_QPU_M_MOV, recip, rf (4));			// 1/w -> r4 (3 later)
	p << I ().a (V3D_QPU_A_STVPMV, imm (0), rf (1));		// Xc
	p << I ().a (V3D_QPU_A_STVPMV, imm (1), rf (2));		// Yc
	p << I ().a (V3D_QPU_A_STVPMV, imm (2), rf (3));		// Zc
	p << I ().a (V3D_QPU_A_STVPMV, imm (3), rf (4));		// Wc
	p << I ().m (V3D_QPU_M_FMUL, r0, rf (1), r4);
	p << I ().m (V3D_QPU_M_FMUL, r1, rf (2), r4);
	p << I ().m (V3D_QPU_M_FMUL, r0, r0, rf (5));
	p << I ().a (V3D_QPU_A_FTOIN, r0, r0).m (V3D_QPU_M_FMUL, r1, r1, rf (6));
	p << I ().a (V3D_QPU_A_FTOIN, r1, r1);
	p << I ().a (V3D_QPU_A_STVPMV, imm (4), r0);			// Xs
	p << I ().a (V3D_QPU_A_STVPMV, imm (5), r1);			// Ys
	p << I ().a (V3D_QPU_A_VPMWT, nop);
	end (p);
}

// the colour in rf1..rf4 -> the TLB, around the last thread switch (and the one before when not final)
static void writeColour (Prog &p, bool bFinal)
{
	if (!bFinal)
	{
		p << I ().thrsw ();
		p << I ().thrsw ();						// (two in a row: the last segment)
		p << I ();
		p << I ();
	}
	p << I ().a (V3D_QPU_A_VFPACK, tlb, rf (1), rf (2)).thrsw ();	// (end)
	p << I ().a (V3D_QPU_A_VFPACK, tlb, rf (3), rf (4));
	p << I ();
}

void flatFS (Prog &p, bool bFinal)
{
	for (int k = 0; k < 4; k++) p << I ().ldunifrf (rf (1 + k));
	writeColour (p, bFinal);
}

void varyFS (Prog &p, int n, bool bFinal)
{
	// varying k: ldvary at 2k, x W at 2k + 1, + C (r5) at 2k + 2 (with the next ldvary)
	p << I ().ldvary (r0);
	for (int k = 0; k < n; k++)
	{
		p << I ().m (V3D_QPU_M_FMUL, r1, r0, rf (0));
		I i; i.a (V3D_QPU_A_FADD, k < 4 ? rf (1 + k) : r2, r1, r5);	// (beyond r g b a: read, not used)
		if (k + 1 < n) i.ldvary (r0);
		p << i;
	}
	writeColour (p, bFinal);
}

void texFS (Prog &p)
{
	p << I ().ldvary (r0);						// s
	p << I ().m (V3D_QPU_M_FMUL, r1, r0, rf (0));
	p << I ().a (V3D_QPU_A_FADD, r1, r1, r5).ldvary (r0);		// S ; t
	p << I ().m (V3D_QPU_M_FMUL, r2, r0, rf (0));
	p << I ().a (V3D_QPU_A_FADD, r2, r2, r5);			// T
	p << I ().m (V3D_QPU_M_MOV, tmut, r2);				// T (Mesa's order: T, the configs, S)
	p << I ().wrtmuc ();						// p0
	p << I ().wrtmuc ();						// p1
	p << I ().m (V3D_QPU_M_MOV, tmus, r1);				// S: the lookup starts
	p << I ().thrsw ();						// (wait for the TMU)
	p << I ().thrsw ();						// (the last segment)
	p << I ();
	p << I ();
	p << I ().ldtmu (r0);						// R G (f16)
	p << I ().ldtmu (r2);						// B A
	p << I ().a (V3D_QPU_A_VFPACK, tlb, l (r0), h (r0)).thrsw ();	// (end)
	p << I ().a (V3D_QPU_A_VFPACK, tlb, l (r2), h (r2));
	p << I ();
}

void viewUniforms (int w, int h, unsigned out[4])
{
	union { float f; unsigned u; } c;
	c.f = (float) (w / 2) * 256.0f; out[0] = c.u;
	c.f = (float) (h / 2) * -256.0f; out[1] = c.u;
	c.f = 0.5f; out[2] = c.u; out[3] = c.u;
}

} // namespace qpu
