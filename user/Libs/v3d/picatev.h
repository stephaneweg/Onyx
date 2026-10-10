//
// v3d/picatev.h -- the Nintendo 3DS GPU's (PICA200) fragment combiner as a V3D fragment shader, generated for one
// configuration (kapi v61 gpu_program), with the generator's machinery of v3d/gxtev: the six stages in integers
// as user/Emulators/n3ds's software renderer computes them (the operands, the modes, the scales, the exact
// division by 255, the combiner buffer two stages late), the texture lookups (units 0..2), the alpha test (the
// pixels discarded). Only what the output needs is generated: a stage's channel nobody reads is not worked out.
//
// What a draw gives the shader:
//   the varyings -- Shader::vary[]: the vertex colour (0..1 a channel), the lighting's primary and secondary
//     colours when the configuration is lit (worked out by the host, 0..1), each lookup's coordinates s t;
//   the uniforms -- Shader::uni[]: constants, the stages' constant colours, the buffer's colour, the alpha
//     reference, each lookup's two TMU words (U_TEXP0 then U_TEXP1: kapi_gpu_batch2 texUni = U_TEXP0's index).
// Not generated: the procedural texture (its source reads 0 0 0 255: Shader::unsupported), fog.
// Registers: 2 threads a QPU (32), rf0 the W of the varyings; a configuration that needs more fails (err).
//
// MIT License -- Copyright (c) 2026 Stephane Wegener (see docs/LICENSING.md)
//
#ifndef _v3d_picatev_h
#define _v3d_picatev_h
#include "v3d/qpu.h"

namespace picatev
{

struct Config
{
	// a stage: its three colour and alpha sources (0 the vertex colour, 1 the lighting's primary colour, 2 its
	// secondary one, 3..5 the textures, 6 the procedural texture, 13 the buffer, 14 the stage's constant, 15 the
	// stage before), their operands, the modes, the scales (1, 2, 4); pass: the stage changes nothing
	struct Stage { unsigned char src[3], srcA[3], op[3], opA[3], mode, modeA, scale, scaleA; bool pass; } stage[6];
	unsigned char updateRgb, updateA;	// the stages (bits 0..3) whose result goes to the buffer
	bool alphaTest; unsigned char alphaFunc;	// 0 never, 1 always, 2 =, 3 !=, 4 <, 5 <=, 6 >, 7 >= (the alpha against the reference)
	unsigned char texOn;			// the units that have a texture (bit a unit; another one reads 0 0 0 255)
	bool lit;				// the lighting's colours are given (else source 1 is the vertex colour, 2 is 0 0 0 255)
};

enum UniKind { U_CONST, U_KONST, U_BUFFER, U_ALPHAREF, U_TEXP0, U_TEXP1 };
// U_CONST: value; U_KONST: stage a's constant, channel b (0..255); U_BUFFER: the buffer's colour, channel b;
// U_ALPHAREF: the reference (0..255); U_TEXP0 / U_TEXP1: lookup a's TMU words (the kernel writes them)
struct Uni { unsigned char kind, a, b; unsigned value; };

enum VaryKind { V_PRIMARY, V_LITP, V_LITS, V_COORD };
// V_PRIMARY / V_LITP / V_LITS: channel a (0..1); V_COORD: lookup a, component b (0 s, 1 t)
struct Vary { unsigned char kind, a, b; };

struct Lookup { unsigned char unit; };

enum { MAX_UNI = 512, MAX_LOOKUPS = 3 };

struct Shader
{
	qpu::Prog prog;
	Uni uni[MAX_UNI]; int nUni;
	Vary vary[32]; int nVary;
	Lookup look[MAX_LOOKUPS]; int nLook;
	unsigned flags;				// kapi_gpu_program flags (0)
	int nRegs;				// the most register-file registers in use at once
	const char *err;			// 0: ok
	bool unsupported;			// a feature not generated was asked (drawn without it)
	// (the tests': the value ranges the generator assumed -- after instruction ip, register reg: 0..5 r0..r5,
	// 6 + n rf n -- for the simulator to check)
	struct Claim { int ip, reg; long long lo, hi; };
	enum { MAX_CLAIMS = 2048 };
	Claim claim[MAX_CLAIMS]; int nClaim;
};

bool build (const Config &c, Shader &s);

// the configuration's key (for a cache of programs): the same key, the same program
unsigned long long key (const Config &c);

} // namespace picatev
#endif
