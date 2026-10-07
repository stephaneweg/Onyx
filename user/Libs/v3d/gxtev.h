//
// v3d/gxtev.h -- the GameCube's TEV as a V3D fragment shader, generated for one TEV configuration
// (kapi v61 gpu_program): the stages' combiners in integers as the hardware computes them (the
// same arithmetic as pc/NintendoEMU/core/gxgl.cpp's GLSL: the lerp's rounding, the bias / scale,
// the compare modes, the clamps, the swap tables, the konst colours), the texture lookups (one a
// stage that enables its map; the coordinates already divided by the map's size, q for the
// projective ones), the alpha test (the pixels discarded), the EFB's pixel format and the
// destination alpha. Not generated yet: the indirect texturing (the bump alpha reads 0), the fog,
// the z texture (Shader::unsupported says one was asked).
//
// What a draw gives the shader:
//   the varyings -- Shader::vary[]: the rasterized colours (C0 / C1: 0..1 a channel) and each
//     lookup's coordinates (s t, q when projective), in that order;
//   the uniforms -- Shader::uni[]: constants, the TEV registers' initial values, the konst colours,
//     the alpha references, the destination alpha, each lookup's two TMU words (U_TEXP0 then
//     U_TEXP1: for kapi_gpu_batch2 texUni = the index of U_TEXP0).
// Registers: 2 threads a QPU (32), rf0 the W of the varyings; a configuration that needs more
// fails (err).
//
#ifndef _v3d_gxtev_h
#define _v3d_gxtev_h
#include "v3d/qpu.h"

namespace gxtev
{

struct Config
{
	int nStages;				// 1..16
	unsigned cenv[16], aenv[16];		// the stages' colour / alpha combiners (BP 0xC0 + 2 s, 0xC1 + 2 s)
	unsigned tref[16];			// map | coordinate << 3 | enabled << 6 | channel << 7
	unsigned ksel[16];			// konst colour | konst alpha << 5
	unsigned char swap[16];			// the 4 swap tables: swap[t * 4 + ch] = the source channel
	unsigned char alphaFunc[2], alphaLogic;	// the alpha test (0 never ... 7 always; 0 and, 1 or, 2 xor, 3 xnor)
	unsigned char efbFmt;			// 0 RGB8, 1 RGBA6, 2 RGB565 (the others: as 0)
	bool dstAlpha;				// the alpha written is the constant's (U_DSTALPHA)
	unsigned char projMask;			// the coordinates given with q (bit per coordinate)
};

enum UniKind { U_CONST, U_REG, U_KONST, U_ALPHAREF, U_TEXP0, U_TEXP1, U_DSTALPHA };
// U_CONST: value; U_REG: TEV register a (0 PREV, 1 C0, 2 C1, 3 C2) channel b (r g b a), its s11
// value as a 32-bit integer; U_KONST: konst a channel b (0..255); U_ALPHAREF: reference a (0..255);
// U_TEXP0 / U_TEXP1: lookup a's TMU words (the kernel writes them); U_DSTALPHA: the float alpha / 255
struct Uni { unsigned char kind, a, b; unsigned value; };

enum VaryKind { V_C0, V_C1, V_COORD };
// V_C0 / V_C1: colour channel a (0..1); V_COORD: lookup a, component b (0 s, 1 t, 2 q)
struct Vary { unsigned char kind, a, b; };

struct Lookup { unsigned char stage, map, coord; bool proj; };

enum { MAX_UNI = 2048, MAX_LOOKUPS = 8, FLAG_FS_FINAL = 2 /* = KAPI_GPU_P_FS_FINAL */ };

struct Shader
{
	qpu::Prog prog;
	Uni uni[MAX_UNI]; int nUni;
	Vary vary[64]; int nVary;
	Lookup look[MAX_LOOKUPS]; int nLook;
	unsigned flags;				// kapi_gpu_program flags (0: never FS_FINAL, see gxtev.cpp)
	int nRegs;				// register-file registers used
	const char *err;			// 0: ok
	bool unsupported;			// a feature not generated was asked (drawn without it)
	// (the tests': the value ranges the generator assumed -- after instruction ip, register reg:
	// 0..5 r0..r5, 6 + n rf n -- for the simulator to check)
	struct Claim { int ip, reg; long long lo, hi; };
	enum { MAX_CLAIMS = 4096 };
	Claim claim[MAX_CLAIMS]; int nClaim;
};

bool build (const Config &c, Shader &s);

// the configuration's key (for a cache of programs): the same key, the same program
unsigned long long key (const Config &c);

} // namespace gxtev
#endif
