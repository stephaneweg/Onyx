// qpubuild_test -- user/v3d/qpu.h (the run-time QPU builder) against tools/qpu's assembler: the
// kernel's FS_TEX shader built in C++ must give the very words of kernel/sys/v3d_shaders.inc.
#include <stdio.h>
#include <string.h>
#include "v3d/qpu.h"
extern "C" {
#include "qpulib.h"
}
typedef unsigned long long u64;
#include "../../../kernel/sys/v3d_shaders.inc"

using namespace qpu;
static const v3d_qpu_add_op FADD = V3D_QPU_A_FADD, FMIN = V3D_QPU_A_FMIN, VFPACK = V3D_QPU_A_VFPACK, NOPA = V3D_QPU_A_NOP;
static const v3d_qpu_mul_op FMUL = V3D_QPU_M_FMUL, MOV = V3D_QPU_M_MOV;

int main ()
{
	Prog p;
	R one = imm (0x3f800000);
	p << I ().ldvary (r0);
	p << I ().m (FMUL, r1, r0, rf (0));
	p << I ().a (FADD, r1, r1, r5).ldvary (r0);
	p << I ().m (FMUL, r2, r0, rf (0));
	p << I ().a (FADD, r2, r2, r5).ldvary (r0);
	p << I ().m (MOV, tmut, r2);
	p << I ().m (FMUL, r3, r0, rf (0)).wrtmuc ();
	p << I ().a (FADD, rf (3), r3, r5).ldvary (r0).wrtmuc ();
	p << I ().m (MOV, tmus, r1);
	for (int k = 4; k <= 9; k++)
	{
		p << I ().m (FMUL, r3, r0, rf (0));
		p << I ().a (FADD, rf (k), r3, r5).ldvary (r0);
	}
	p << I ().m (FMUL, r3, r0, rf (0)).thrsw ();
	p << I ().a (FADD, rf (10), r3, r5).thrsw ();
	p << I ();
	p << I ().ldtmu (r0);
	p << I ().m (FMUL, r1, l (r0), rf (3)).ldtmu (r2);
	p << I ().a (FADD, r1, r1, rf (7)).m (FMUL, r0, h (r0), rf (4));
	p << I ().a (FADD, r0, r0, rf (8)).m (FMUL, r3, l (r2), rf (5));
	p << I ().a (FADD, r3, r3, rf (9)).m (FMUL, r2, h (r2), rf (6));
	p << I ().a (FADD, r2, r2, rf (10));
	p << I ().a (FMIN, r1, r1, one);
	p << I ().a (FMIN, r0, r0, one);
	p << I ().a (FMIN, r3, r3, one);
	p << I ().a (FMIN, r2, r2, one);
	p << I ().a (VFPACK, tlb, r1, r0).thrsw ();
	p << I ().a (VFPACK, tlb, r3, r2);
	p << I ();
	(void) NOPA;
	int n = (int) (sizeof FS_TEX / sizeof FS_TEX[0]), bad = 0;
	if (!p.ok ()) { printf ("FAIL: instruction %d not encodable\n", p.badAt); return 1; }
	if (p.count () != n) { printf ("FAIL: %d instructions, %d expected\n", p.count (), n); bad = 1; }
	for (int i = 0; i < n && i < p.count (); i++)
		if (p.words ()[i] != FS_TEX[i]) { printf ("FAIL %2d: %016llX %s\n       expected %016llX %s\n", i, p.words ()[i], qpu_disassemble (p.words ()[i]), FS_TEX[i], qpu_disassemble (FS_TEX[i])); bad = 1; }
	char err[256];
	if (qpu_check ((const uint64_t *) p.words (), p.count (), QPU_FRAG, err, sizeof err) < 0) { printf ("FAIL check: %s\n", err); bad = 1; }
	if (!bad) printf ("ok  : FS_TEX built in C++ = the assembled words (%d), restrictions ok\n", n);
	return bad;
}
