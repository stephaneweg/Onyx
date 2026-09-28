//
// v3d/qpu.h -- V3D 4.2 QPU instructions built in C++ (no text), for the shaders an app generates
// at run time (the GameCube's TEV, user/gc): an instruction is a Mesa struct v3d_qpu_instr,
// packed by Mesa's qpu_pack.c (tools/qpu/mesa, MIT). The same code runs on the Pi (freestanding)
// and on the PC (the tests: tools/qpu/qpulib.c checks the instruction restrictions, qpusim runs
// the programs).
//
//   qpu::Prog p;
//   p << qpu::I ().a (FADD, rf (3), r1, r5).m (FMUL, r1, r0, rf (0)).ldvary (r0);
//   ...  p.words (), p.count (); p.ok () (false: an operand could not be encoded)
//
// Operands: r0..r5 (accumulators), rf (n) (the register file), small immediates imm (v) (-16..15,
// or the float ones: 1.0 2.0 4.0 ... 0x3f800000 ...), the magic write addresses (tlb, tmus, tmut,
// recip, ...) as destinations; unpacks l (x) / h (x) / abs (x) on sources. Two register-file reads
// an instruction at most (ports A and B; a small immediate takes B), as the hardware has.
//
#ifndef _v3d_qpu_h
#define _v3d_qpu_h

extern "C" {
#include "broadcom/qpu/qpu_instr.h"
}

namespace qpu
{
typedef unsigned long long u64;
typedef unsigned int u32;

struct R
{
	int kind;				// 0 accumulator r0..r5, 1 register file, 2 magic (destination), 3 small immediate, 4 none
	int n;					// the register / the magic address / the immediate's value
	int unpack;				// V3D_QPU_UNPACK_* (sources)
	int pack;				// V3D_QPU_PACK_* (destinations: .l / .h)
};

static const R r0 = { 0, 0, 0, 0 }, r1 = { 0, 1, 0, 0 }, r2 = { 0, 2, 0, 0 }, r3 = { 0, 3, 0, 0 }, r4 = { 0, 4, 0, 0 }, r5 = { 0, 5, 0, 0 };
static const R none = { 4, 0, 0, 0 };
inline R rf (int n) { R r = { 1, n, 0, 0 }; return r; }
inline R magic (int w) { R r = { 2, w, 0, 0 }; return r; }
inline R imm (u32 v) { R r = { 3, (int) v, 0, 0 }; return r; }
inline R l (R x) { x.unpack = V3D_QPU_UNPACK_L; return x; }
inline R h (R x) { x.unpack = V3D_QPU_UNPACK_H; return x; }
inline R abs (R x) { x.unpack = V3D_QPU_UNPACK_ABS; return x; }
inline R dl (R x) { x.pack = V3D_QPU_PACK_L; return x; }
inline R dh (R x) { x.pack = V3D_QPU_PACK_H; return x; }
static const R nop = { 2, V3D_QPU_WADDR_NOP, 0, 0 };
static const R tlb = { 2, V3D_QPU_WADDR_TLB, 0, 0 }, tlbu = { 2, V3D_QPU_WADDR_TLBU, 0, 0 };
static const R tmus = { 2, V3D_QPU_WADDR_TMUS, 0, 0 }, tmut = { 2, V3D_QPU_WADDR_TMUT, 0, 0 };
static const R tmur = { 2, V3D_QPU_WADDR_TMUR, 0, 0 }, tmub = { 2, V3D_QPU_WADDR_TMUB, 0, 0 };
static const R tmua = { 2, V3D_QPU_WADDR_TMUA, 0, 0 }, tmud = { 2, V3D_QPU_WADDR_TMUD, 0, 0 };
static const R recip = { 2, V3D_QPU_WADDR_RECIP, 0, 0 }, rsqrt = { 2, V3D_QPU_WADDR_RSQRT, 0, 0 };
static const R exp2 = { 2, V3D_QPU_WADDR_EXP, 0, 0 }, log2 = { 2, V3D_QPU_WADDR_LOG, 0, 0 };

// one instruction: "add ; mul ; signals"
struct I
{
	v3d_qpu_instr in;
	int nrf, rfregs[2];
	bool ok;
	I ();
	I &a (v3d_qpu_add_op op, R dst = none, R x = none, R y = none);	// the add ALU (as many operands as the op has)
	I &m (v3d_qpu_mul_op op, R dst = none, R x = none, R y = none);	// the mul ALU
	I &ac (v3d_qpu_cond c) { in.flags.ac = c; return *this; }		// conditions / flag pushes / updates
	I &mc (v3d_qpu_cond c) { in.flags.mc = c; return *this; }
	I &apf (v3d_qpu_pf f) { in.flags.apf = f; return *this; }
	I &mpf (v3d_qpu_pf f) { in.flags.mpf = f; return *this; }
	I &auf (v3d_qpu_uf f) { in.flags.auf = f; return *this; }
	I &muf (v3d_qpu_uf f) { in.flags.muf = f; return *this; }
	I &thrsw () { in.sig.thrsw = true; return *this; }
	I &ldunif () { in.sig.ldunif = true; return *this; }			// the uniform -> r5
	I &ldunifrf (R d) { in.sig.ldunifrf = true; sigDst (d); return *this; }
	I &ldvary (R d) { in.sig.ldvary = true; sigDst (d); return *this; }	// (C -> r5 two later)
	I &ldtmu (R d) { in.sig.ldtmu = true; sigDst (d); return *this; }
	I &wrtmuc () { in.sig.wrtmuc = true; return *this; }			// the next uniform -> the TMU config
private:
	int src (R x, v3d_qpu_mux *mux);
	void sigDst (R d);
};

// a program: its words
struct Prog
{
	enum { MAX = 4096 };
	u64 w[MAX];
	int n;
	bool bad;				// an instruction could not be encoded
	int badAt;
	Prog () : n (0), bad (false), badAt (-1) {}
	Prog &operator<< (const I &i);
	void set (int k, const I &i);		// instruction k replaced
	const u64 *words () const { return w; }
	int count () const { return n; }
	bool ok () const { return !bad; }
};

} // namespace qpu

#endif
