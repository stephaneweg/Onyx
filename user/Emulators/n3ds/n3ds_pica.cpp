//
// n3ds/n3ds_pica.cpp -- the PICA200, the 3DS's GPU, in software (phase T2: the reference renderer; the Pi's V3D
// takes the fragment work in T3). A program gives the GPU lists of commands -- each a register's number, a byte
// mask and one or several values -- that set its state and start the draws:
//   vertices: attribute buffers (or vertices given one by one), run through the program's VERTEX SHADER (the
//     GPU's own instruction set: an interpreter here), whose outputs are mapped to position, colour, texture
//     coordinates;
//   triangles: assembled (lists, strips, fans), clipped, culled, put in the viewport and rasterized with
//     perspective-correct interpolation;
//   fragments: up to three textures (8 x 8 tiles in Z order, the 14 formats, ETC1 among them), six texture-combiner
//     stages, the alpha test, the depth test, blending or a logic operation, into the colour and depth buffers
//     (tiled too, rows from the bottom).
// And the display transfer that turns a colour buffer into a screen's framebuffer.
// The procedural texture (unit 3: a colour computed from two coordinates through look-up tables -- what citro2d
// tints its pictures with) is there but for its noise and its filtering.
// Fragment lighting: each fragment has a normal (a quaternion from the shader, turned into a vector) and a view
// vector; up to 8 lights give a "primary" colour (ambient and diffuse) and a "secondary" one (two speculars),
// shaped by look-up tables (D0, D1, the reflection's three, Fresnel, the distance's) -- the combiner's sources 1
// and 2. Not in it: spot lights, bump mapping, shadows, the geometric factors.
// The triangles of a command list are not drawn one by one: they are queued with the state they are drawn with,
// and the queue is rasterized at the list's end (or when it is full), band by band of 8 rows, each band in the
// triangles' order. When the host has other cores (Machine::parallelBegin) they take the bands one after the
// other, and at a list's end this thread does not wait for them: the program goes on, and the list is over
// (picaSync) when it waits for it or at the GPU's next command.
// A texture is decoded once into plain pixels (its rows from the top) and kept while the program's bytes stay the
// same (a checksum of samples, looked at when a draw's state is made): a texel is then one read.
// The fragments of a triangle are shaded by groups of up to CH (the ones that passed the depth test, row after row): each
// input, each combiner stage is worked out for the whole group in one loop over plain rows of numbers (loops the
// compiler turns into vector instructions), the choices a stage makes being made once a group and not once a pixel.
// Not done yet: fog, shadows, stencil, the geometry shader, texture filtering (the nearest texel is taken),
// mipmaps -- noted when a program asks.
//
// Written from the public documentation of the GPU (3dbrew's register and shader pages); no code of another
// emulator.
//
// MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors (see n3ds.h).
//
#include <math.h>
#include <stdio.h>
#include <sys/time.h>
#include <stdlib.h>
#include <string.h>
#include "n3ds/n3ds.h"

namespace n3ds {

enum
{
	// the registers used here
	R_FINALIZE = 0x010, R_CULL = 0x040, R_VIEWPORT_W = 0x041, R_VIEWPORT_H = 0x043, R_DEPTH_SCALE = 0x04D, R_DEPTH_OFFSET = 0x04E,
	R_OUTMAP_TOTAL = 0x04F, R_OUTMAP = 0x050, R_SCISSOR_MODE = 0x065, R_SCISSOR_POS = 0x066, R_SCISSOR_DIM = 0x067, R_VIEWPORT_XY = 0x068,
	R_TEX_CONFIG = 0x080, R_TEX0 = 0x081, R_TEX0_TYPE = 0x08E, R_LIGHTING = 0x08F, R_TEX1 = 0x091, R_TEX1_TYPE = 0x096, R_TEX2 = 0x099, R_TEX2_TYPE = 0x09E,
	R_PROCTEX0 = 0x0A8, R_PROCTEX4 = 0x0AC, R_PROCTEX5 = 0x0AD, R_PROCTEX_LUT = 0x0AF, R_PROCTEX_LUT_DATA = 0x0B0,
	R_TEV_UPDATE = 0x0E0, R_TEV_BUFFER = 0x0FD,
	R_LIGHT0 = 0x140, R_LIGHT_AMBIENT = 0x1C0, R_LIGHT_COUNT = 0x1C2, R_LIGHT_CONFIG0 = 0x1C3, R_LIGHT_CONFIG1 = 0x1C4,
	R_LIGHT_LUT_INDEX = 0x1C5, R_LIGHT_LUT_DATA = 0x1C8, R_LIGHT_LUT_ABS = 0x1D0, R_LIGHT_LUT_SELECT = 0x1D1, R_LIGHT_LUT_SCALE = 0x1D2,
	R_LIGHT_PERMUTATION = 0x1D9,
	R_COLOR_OP = 0x100, R_BLEND_FUNC = 0x101, R_LOGIC_OP = 0x102, R_BLEND_COLOR = 0x103, R_ALPHA_TEST = 0x104, R_STENCIL_TEST = 0x105, R_DEPTH_COLOR_MASK = 0x107,
	R_COLOR_WRITE = 0x113, R_DEPTH_WRITE = 0x115, R_DEPTH_FORMAT = 0x116, R_COLOR_FORMAT = 0x117, R_DEPTH_ADDR = 0x11C, R_COLOR_ADDR = 0x11D, R_FB_DIM = 0x11E,
	R_ATTR_BASE = 0x200, R_ATTR_FORMAT_LO = 0x201, R_ATTR_FORMAT_HI = 0x202, R_ATTR_LOADER = 0x203,
	R_INDEX_CONFIG = 0x227, R_NUM_VERTICES = 0x228, R_GEO_CONFIG = 0x229, R_VERTEX_OFFSET = 0x22A, R_DRAW_ARRAYS = 0x22E, R_DRAW_ELEMENTS = 0x22F,
	R_FIXED_INDEX = 0x232, R_FIXED_DATA = 0x233, R_CMD_SIZE0 = 0x238, R_CMD_ADDR0 = 0x23A, R_CMD_JUMP0 = 0x23C, R_CMD_JUMP1 = 0x23D, R_PRIM_CONFIG = 0x25E, R_PRIM_RESTART = 0x25F,
	R_VSH_BOOL = 0x2B0, R_VSH_INT = 0x2B1, R_VSH_INPUT = 0x2B9, R_VSH_ENTRY = 0x2BA, R_VSH_PERM_LO = 0x2BB, R_VSH_PERM_HI = 0x2BC, R_VSH_OUTMASK = 0x2BD,
	R_VSH_FLOAT_INDEX = 0x2C0, R_VSH_FLOAT_DATA = 0x2C1, R_VSH_CODE_INDEX = 0x2CB, R_VSH_CODE_DATA = 0x2CC, R_VSH_OPDESC_INDEX = 0x2D5, R_VSH_OPDESC_DATA = 0x2D6,
	REG_COUNT = 0x300, CODE_MAX = 4096, OPDESC_MAX = 128,
};
static const int TEV_BASE[6] = { 0x0C0, 0x0C8, 0x0D0, 0x0D8, 0x0F0, 0x0F8 };

// What the vertex shader's outputs mean (the output map's numbers): a vertex is these 24 floats.
enum { A_POS = 0, A_QUAT = 4, A_COLOR = 8, A_TC0 = 12, A_TC1 = 14, A_TC0W = 16, A_VIEW = 18, A_TC2 = 22, A_COUNT = 24 };
struct Vertex { float a[A_COUNT]; };

struct Pica
{
	Machine *m;
	u32 regs[REG_COUNT];
	// the vertex shader: its code, its operand descriptors, its uniforms
	u32 code[CODE_MAX]; u32 opdesc[OPDESC_MAX];
	float fu[96][4]; u8 iu[4][4]; u32 bu;
	u32 codeIndex, opdescIndex;
	struct ShaderIns *dec; u8 *decValid; bool decDirty, decAll;
	struct Batch *batch; u32 badOp;			// a draw's vertices shaded together (on several cores); an instruction the shader does not know	// the code's instructions decoded (as they are met; again when the code or the descriptors change)
	u32 floatIndex; bool float32; u32 floatWords[4]; int floatCount;
	// attributes given by registers: the fixed ones, and the vertices given one by one
	float fixed[12][4]; u32 fixedIndex; u32 fixedWords[3]; int fixedCount;
	float immediate[16][4]; int immediateCount;
	// the procedural texture's tables: the two maps (128 values and their slopes), the colours (256)
	float procMap[2][128], procMapDiff[2][128]; u32 procColor[256];
	u32 procIndex, procTable;
	// the lighting's tables: 0 D0, 1 D1, 3 Fresnel, 4-6 the reflection's blue / green / red, 8-15 the spots', 16-23 the distances'
	float lightLut[24][256], lightLutDiff[24][256];
	u32 lightLutIndex, lightLutType;
	// the triangle being assembled
	Vertex prim[3]; int primCount; bool stripFlip;
	u32 codeTop; int dumped;			// (traces: how much code was loaded; shaders already printed)
	// the textures decoded: where the program's is, its size and format, its bytes' checksum, the pixels (r, g, b, a bytes)
	struct Decoded { const u8 *data; u32 w, h, format; u64 check; u32 *px; u64 used; } decoded[96];
	u64 decodedClock;
	struct Scratch *scratch;			// a worker's rows (8 of them)
	struct Shaded *shaded; u32 drawStamp;		// an indexed draw's vertices already shaded (by their index; this draw's: the stamp)
	bool stateOk, targetOk;				// (the registers' meaning for a draw: worked out at its first triangle)
	struct State *states; u32 stateCount;		// the states of the queued triangles (the last one: the current)
	struct Job *jobs; u32 jobCount;			// the triangles waiting to be rasterized
	u32 bandNext, bandCount;			// the bands of the flush under way: the next one to take, how many
	bool busy; u64 busySince;			// the queue is being rasterized aside (since when, in microseconds)
	int jump;					// a jump asked by the command being run: 1 or 2 (which buffer), 0 none
	// counters
	u64 triangles, pixels, depthFailed, alphaFailed;
	struct { u64 triangles, pixels, depthFailed, alphaFailed; char pad[32]; } stat[8];	// (a worker's own, added at the flush's end)
};

// ---- numbers -----------------------------------------------------------------------------------------------------
// The GPU's 24-bit float: a sign, 7 bits of exponent (63 = 2^0), 16 of mantissa.
static float f24 (u32 v)
{
	v &= 0xFFFFFF;
	const u32 sign = v >> 23, exp = v >> 16 & 0x7F, mant = v & 0xFFFF;
	u32 bits;
	if (!exp && !mant) bits = 0;
	else if (exp == 0x7F) bits = 0xFFu << 23 | mant << 7;
	else bits = (exp + 64) << 23 | mant << 7;
	bits |= sign << 31;
	float f; memcpy (&f, &bits, 4);
	return f;
}
static float bitsFloat (u32 v) { float f; memcpy (&f, &v, 4); return f; }
// Four 24-bit floats in three words: w, z, y, x.
static void unpack24 (const u32 *w, float *out)
{
	out[3] = f24 (w[0] >> 8);
	out[2] = f24 ((w[0] & 0xFF) << 16 | w[1] >> 16);
	out[1] = f24 ((w[1] & 0xFFFF) << 8 | w[2] >> 24);
	out[0] = f24 (w[2] & 0xFFFFFF);
}
static inline int clamp255 (int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

// A pixel's place in a tiled buffer (8 x 8 tiles, Z order inside a tile).
static const u8 TILE_X[8] = { 0, 1, 4, 5, 16, 17, 20, 21 }, TILE_Y[8] = { 0, 2, 8, 10, 32, 34, 40, 42 };
static inline u32 tiled (u32 x, u32 y, u32 width) { return (u32) TILE_X[x & 7] + TILE_Y[y & 7] + (x & ~7u) * 8 + (y & ~7u) * width; }

// ---- the vertex shader ---------------------------------------------------------------------------------------------
// An instruction's four components are worked out at once (the compiler's vectors: NEON): an operand is its
// register's four floats shuffled and negated as the instruction's descriptor says, the result goes to the
// components the descriptor names. What an instruction says is decoded once (ShaderIns), when it is first met.
typedef float V4 __attribute__ ((vector_size (16)));
typedef int VI __attribute__ ((vector_size (16)));
typedef unsigned char VB __attribute__ ((vector_size (16)));

struct Shader
{
	float reg[32][4], out[16][4];					// the inputs (0-15) and the temporaries (16-31); the outputs
	bool cmp[2]; int a[2]; int aL;
};

struct ShaderIns
{
	VB sh[3]; VI neg[3]; VI mask;					// the operands' shuffles (bytes) and signs; the components written
	u8 op, shape;							// shape: 0 not arithmetic, 1 two operands, 2 three (MAD)
	u8 d, r[3], idx, idxOn;						// the destination, the operands' registers, the index register and the operand it moves
	u8 writes;							// (the written components as bits: x = 8)
};

// The GPU's product: nothing times anything is nothing, even infinity (where IEEE says "not a number"): shaders
// count on it.
static inline V4 mulz (V4 a, V4 b)
{
	const V4 zero = { 0.0f, 0.0f, 0.0f, 0.0f };
	const VI z = (a == zero) | (b == zero);
	return (V4) (~z & (VI) (a * b));
}

static inline const float *srcReg (const Pica *p, const Shader &s, u32 r)
{
	if (r < 0x20) return s.reg[r];
	r -= 0x20;
	return p->fu[r < 96 ? r : 95];
}
static inline V4 operand (const float *q, VB sh, VI neg)
{
	V4 v;
	memcpy (&v, q, sizeof v);
	return (V4) ((VI) (V4) __builtin_shuffle ((VB) v, sh) ^ neg);
}

static void decodeIns (const Pica *p, u32 pc, ShaderIns &I)
{
	const u32 ins = p->code[pc], op = ins >> 26;
	I.op = (u8) op; I.shape = 0;
	if (op >= 0x20 && op <= 0x2D) return;				// BREAK, NOP, END, the flow, the geometry shader's
	u32 desc;
	if (op >= 0x30)							// MADI (0x30-0x37), MAD (0x38-0x3F)
	{
		I.shape = 2; I.d = (u8) (ins >> 24 & 0x1F); I.idx = (u8) (ins >> 22 & 3); I.r[0] = (u8) (ins >> 17 & 0x1F); desc = ins & 0x1F;
		if (op >= 0x38) { I.r[1] = (u8) (ins >> 10 & 0x7F); I.r[2] = (u8) (ins >> 5 & 0x1F); I.idxOn = 1; }
		else { I.r[1] = (u8) (ins >> 12 & 0x1F); I.r[2] = (u8) (ins >> 5 & 0x7F); I.idxOn = 2; }
	}
	else
	{
		I.shape = 1; I.d = (u8) (ins >> 21 & 0x1F); I.idx = (u8) (ins >> 19 & 3); desc = ins & 0x7F; I.r[2] = 0;
		if (op >= 0x18 && op <= 0x1B) { I.r[0] = (u8) (ins >> 14 & 0x1F); I.r[1] = (u8) (ins >> 7 & 0x7F); I.idxOn = 1; }	// the "inverted" ones
		else { I.r[0] = (u8) (ins >> 12 & 0x7F); I.r[1] = (u8) (ins >> 7 & 0x1F); I.idxOn = 0; }
	}
	const u32 od = p->opdesc[desc & (OPDESC_MAX - 1)];
	static const int SW[3] = { 5, 14, 23 }, NG[3] = { 4, 13, 22 };
	for (int k = 0; k < 3; k++)
	{
		const u32 sw = od >> SW[k] & 0xFF;
		const int sign = (od >> NG[k] & 1) ? (int) 0x80000000u : 0;
		for (int i = 0; i < 4; i++)
		{
			const u32 c = sw >> (6 - 2 * i) & 3;
			for (int b = 0; b < 4; b++) I.sh[k][4 * i + b] = (unsigned char) (c * 4 + (u32) b);
			I.neg[k][i] = sign;
		}
	}
	I.writes = (u8) (od & 15);
	for (int i = 0; i < 4; i++) I.mask[i] = (od & 15 & (8u >> i)) ? -1 : 0;
}

// The decoded instructions' table is there and up to date (false: no memory). `all`: every instruction decoded now
// (what several cores running the shader at once need: none of them writes the table then).
static bool decReady (Pica *p, bool all)
{
	if (!p->dec)
	{
		p->dec = (ShaderIns *) malloc (sizeof (ShaderIns) * CODE_MAX); p->decValid = (u8 *) malloc (CODE_MAX);
		if (!p->dec || !p->decValid) { free (p->dec); free (p->decValid); p->dec = 0; p->decValid = 0; return false; }
		p->decDirty = true;
	}
	if (p->decDirty) { memset (p->decValid, 0, CODE_MAX); p->decDirty = false; p->decAll = false; }
	if (all && !p->decAll)
	{
		for (u32 pc = 0; pc < CODE_MAX; pc++) if (!p->decValid[pc]) { decodeIns (p, pc, p->dec[pc]); p->decValid[pc] = 1; }
		p->decAll = true;
	}
	return true;
}

// (stepsOut: the instructions run are added there; badOp: an instruction it does not know is left there)
static void runShader (Pica *p, Shader &s, u32 entry, u64 &stepsOut, u32 &badOp)
{
	if (!p->dec || p->decDirty) { if (!decReady (p, false)) return; }
	struct Frame { u32 end, ret, repeat, inc, loop; } stack[16];
	int depth = 0;
	u32 pc = entry;
	s.cmp[0] = s.cmp[1] = false; s.a[0] = s.a[1] = 0; s.aL = 0;
	int steps = 0;
	struct Count { u64 &total; int &n; ~Count () { total += (u64) n; } } count = { stepsOut, steps };
	for (; steps < 65536; steps++)
	{
		while (depth && pc == stack[depth - 1].end)			// the end of a called block, of a loop's body
		{
			Frame &f = stack[depth - 1];
			if (f.repeat) { f.repeat--; s.aL += (int) f.inc; pc = f.loop; }
			else { pc = f.ret; depth--; }
		}
		if (pc >= CODE_MAX) return;
		if (!p->decValid[pc]) { decodeIns (p, pc, p->dec[pc]); p->decValid[pc] = 1; }
		const ShaderIns &I = p->dec[pc];
		const u32 ins = p->code[pc++];
		const u32 op = I.op;
		if (I.shape == 0)
		{
			if (op == 0x22) return;						// END
			if (op == 0x21 || op == 0x20 || op == 0x2A || op == 0x2B || op == 0x23) continue;	// NOP, BREAK, EMIT, SETEMIT (geometry shaders), BREAKC
			// flow: CALL*, IF*, LOOP, JMP*
			const u32 dst = ins >> 10 & 0xFFF, num = ins & 0xFF;
			bool cond = true;
			if (op == 0x25 || op == 0x28 || op == 0x2C)		// ...C: on the comparison's two results
			{
				const bool x = s.cmp[0] == ((ins >> 25 & 1) != 0), y = s.cmp[1] == ((ins >> 24 & 1) != 0);
				switch (ins >> 22 & 3) { case 0: cond = x || y; break; case 1: cond = x && y; break; case 2: cond = x; break; default: cond = y; break; }
			}
			else if (op == 0x26 || op == 0x27 || op == 0x2D) cond = (p->bu >> (ins >> 22 & 15) & 1) != 0;	// ...U: on a boolean uniform
			switch (op)
			{
			case 0x24: case 0x25: case 0x26:			// CALL: num instructions at dst, then back
				if (cond && depth < 16) { stack[depth++] = { dst + num, pc, 0, 0, 0 }; pc = dst; }
				break;
			case 0x27: case 0x28:					// IF: [pc, dst) or else [dst, dst + num)
				if (cond) { if (depth < 16) stack[depth++] = { dst, dst + num, 0, 0, 0 }; }
				else pc = dst;
				break;
			case 0x29:						// LOOP: to dst included, an integer uniform's (count, start, step)
			{
				const u8 *u = p->iu[ins >> 22 & 3];
				s.aL = u[1];
				if (depth < 16) stack[depth++] = { dst + 1, dst + 1, u[0], u[2], pc };
				break;
			}
			case 0x2C: if (cond) pc = dst; break;			// JMPC
			case 0x2D: if (cond == !(num & 1)) pc = dst; break;	// JMPU (num's bit 0: jump when the uniform is false)
			}
			continue;
		}

		// arithmetic
		u32 r1 = I.r[0], r2 = I.r[1], r3 = I.r[2];
		if (I.idx)
		{
			const u32 off = (u32) (I.idx == 1 ? s.a[0] : I.idx == 2 ? s.a[1] : s.aL);
			if (I.idxOn == 0) r1 += off; else if (I.idxOn == 1) r2 += off; else r3 += off;
		}
		const V4 s1 = operand (srcReg (p, s, r1 & 0x7F), I.sh[0], I.neg[0]);
		const V4 s2 = operand (srcReg (p, s, r2 & 0x7F), I.sh[1], I.neg[1]);
		V4 v;
		if (I.shape == 2) v = mulz (s1, s2) + operand (srcReg (p, s, r3 & 0x7F), I.sh[2], I.neg[2]);
		else switch (op)
		{
		case 0x00: v = s1 + s2; break;											// ADD
		case 0x01: { const V4 m = mulz (s1, s2); const float f = m[0] + m[1] + m[2]; v = (V4) { f, f, f, f }; break; }		// DP3
		case 0x02: { const V4 m = mulz (s1, s2); const float f = m[0] + m[1] + m[2] + m[3]; v = (V4) { f, f, f, f }; break; }	// DP4
		case 0x03: case 0x18: { const V4 m = mulz (s1, s2); const float f = m[0] + m[1] + m[2] + s2[3]; v = (V4) { f, f, f, f }; break; }	// DPH
		case 0x04: case 0x19: v = (V4) { 1.0f, s1[1] * s2[1], s1[2], s2[3] }; break;					// DST
		case 0x05: { const float f = exp2f (s1[0]); v = (V4) { f, f, f, f }; break; }					// EX2
		case 0x06: { const float f = log2f (s1[0]); v = (V4) { f, f, f, f }; break; }					// LG2
		case 0x08: v = mulz (s1, s2); break;										// MUL
		case 0x09: case 0x1A: v = (V4) ((s1 >= s2) & (VI) (V4) { 1.0f, 1.0f, 1.0f, 1.0f }); break;			// SGE
		case 0x0A: case 0x1B: v = (V4) ((s1 < s2) & (VI) (V4) { 1.0f, 1.0f, 1.0f, 1.0f }); break;			// SLT
		case 0x0B: v = (V4) { floorf (s1[0]), floorf (s1[1]), floorf (s1[2]), floorf (s1[3]) }; break;			// FLR
		case 0x0C: { const VI c = s1 > s2; v = (V4) (((VI) s1 & c) | ((VI) s2 & ~c)); break; }				// MAX
		case 0x0D: { const VI c = s1 < s2; v = (V4) (((VI) s1 & c) | ((VI) s2 & ~c)); break; }				// MIN
		case 0x0E: { const float f = 1.0f / s1[0]; v = (V4) { f, f, f, f }; break; }					// RCP
		case 0x0F: { const float f = 1.0f / sqrtf (s1[0]); v = (V4) { f, f, f, f }; break; }				// RSQ
		case 0x12: if (I.writes & 8) s.a[0] = (int) s1[0]; if (I.writes & 4) s.a[1] = (int) s1[1]; continue;		// MOVA
		case 0x13: v = s1; break;											// MOV
		case 0x2E: case 0x2F:												// CMP
			for (int i = 0; i < 2; i++)
			{
				const u32 c = i == 0 ? ins >> 24 & 7 : ins >> 21 & 7;
				const float x = s1[i], y = s2[i];
				s.cmp[i] = c == 0 ? x == y : c == 1 ? x != y : c == 2 ? x < y : c == 3 ? x <= y : c == 4 ? x > y : c == 5 ? x >= y : true;
			}
			continue;
		default: badOp = op | 0x100; continue;
		}
		float *dst = I.d < 0x10 ? s.out[I.d] : s.reg[I.d];
		V4 old;
		memcpy (&old, dst, sizeof old);
		v = (V4) (((VI) v & I.mask) | ((VI) old & ~I.mask));
		memcpy (dst, &v, sizeof v);
	}
}

// Runs the shader on a vertex's attributes (the input registers are fed through the permutation) and maps its
// outputs to the vertex's meanings.
static void shadeVertex (Pica *p, float attr[16][4], int count, Vertex &v, u64 &steps, u32 &badOp)
{
	Shader s;
	memset (&s, 0, sizeof s);
	const u64 perm = (u64) p->regs[R_VSH_PERM_HI] << 32 | p->regs[R_VSH_PERM_LO];
	for (int i = 0; i < count && i < 16; i++) memcpy (s.reg[perm >> (4 * i) & 15], attr[i], sizeof (float) * 4);
	if (!(p->m->gpuSkip & 256)) runShader (p, s, p->regs[R_VSH_ENTRY] & 0xFFFF, steps, badOp);
	memset (&v, 0, sizeof v);
	v.a[A_POS + 3] = 1.0f;
	const u32 outMask = p->regs[R_VSH_OUTMASK];
	const int total = (int) (p->regs[R_OUTMAP_TOTAL] & 7);
	int slot = 0;
	for (int o = 0; o < 16 && slot < total && slot < 7; o++)
	{
		if (!(outMask >> o & 1)) continue;
		const u32 map = p->regs[R_OUTMAP + slot++];
		for (int c = 0; c < 4; c++)
		{
			const u32 sem = map >> (8 * c) & 0x1F;
			if (sem < A_COUNT) v.a[sem] = s.out[o][c];
		}
	}
}

// ---- textures ------------------------------------------------------------------------------------------------------
struct Texture { const u8 *data; u32 w, h, format, wrapS, wrapT; bool on; u32 maskW, maskH; /* size - 1 when it repeats and is a power of two, else 0 */
		 u32 bytes; const u32 *px; /* the texture decoded (rows from the top; r, g, b, a bytes), or 0: decoded texel by texel */ };

static void texInfo (const Pica *p, int unit, Texture &t)
{
	static const int BASE[3] = { R_TEX0, R_TEX1, R_TEX2 }, TYPE[3] = { R_TEX0_TYPE, R_TEX1_TYPE, R_TEX2_TYPE };
	const u32 *r = p->regs + BASE[unit];					// border, dim, param, lod, addr
	t.on = (p->regs[R_TEX_CONFIG] >> unit & 1) != 0;
	t.h = r[1] & 0x7FF; t.w = r[1] >> 16 & 0x7FF;
	t.wrapT = r[2] >> 8 & 7; t.wrapS = r[2] >> 12 & 7;
	t.format = p->regs[TYPE[unit]] & 0xF;
	static const u8 BITS[14] = { 32, 24, 16, 16, 16, 16, 16, 8, 8, 8, 4, 4, 4, 8 };
	const u32 bytes = t.format < 14 ? (t.w * t.h * BITS[t.format] + 7) / 8 : 0;
	t.data = t.on && t.w && t.h && bytes ? p->m->physPtr (r[4] << 3, bytes) : 0;
	if (!t.data) t.on = false;
	t.maskW = t.wrapS == 2 && !(t.w & (t.w - 1)) ? t.w - 1 : 0;
	t.maskH = t.wrapT == 2 && !(t.h & (t.h - 1)) ? t.h - 1 : 0;
	t.px = 0;
	t.bytes = bytes;
}

static inline int wrapCoord (int v, int size, u32 mode)
{
	switch (mode)
	{
	case 2: v %= size; return v < 0 ? v + size : v;				// repeat
	case 3: { int m = v % (2 * size); if (m < 0) m += 2 * size; return m < size ? m : 2 * size - 1 - m; }	// mirrored
	default: return v < 0 ? 0 : v >= size ? size - 1 : v;			// clamp to the edge (and to the border: its colour is not kept)
	}
}

// ETC1: blocks of 4 x 4 texels in 8 bytes -- two base colours (each for half the block, split upright or
// lying), and for each texel one of four steps of brightness around its half's colour. An 8 x 8 tile holds four
// blocks; with alpha (ETC1A4) 8 bytes of 4-bit alphas come before each block.
static void etc1Texel (const u8 *block, bool alpha, u32 x, u32 y, int out[4])
{
	static const int STEP[8][2] = { { 2, 8 }, { 5, 17 }, { 9, 29 }, { 13, 42 }, { 18, 60 }, { 24, 80 }, { 33, 106 }, { 47, 183 } };
	u64 a = 0, c = 0;
	if (alpha) { memcpy (&a, block, 8); block += 8; }
	memcpy (&c, block, 8);
	const u32 texel = x * 4 + y;
	const bool flip = (c >> 32 & 1) != 0, diff = (c >> 33 & 1) != 0;
	const bool second = flip ? y >= 2 : x >= 2;
	int r, g, b;
	if (diff)							// 5 bits and a signed 3-bit difference
	{
		r = (int) (c >> 59 & 31); g = (int) (c >> 51 & 31); b = (int) (c >> 43 & 31);
		if (second)
		{
			r += (int) ((s32) ((u32) (c >> 56 & 7) << 29) >> 29); g += (int) ((s32) ((u32) (c >> 48 & 7) << 29) >> 29); b += (int) ((s32) ((u32) (c >> 40 & 7) << 29) >> 29);
		}
		r = (r & 31) << 3 | (r & 31) >> 2; g = (g & 31) << 3 | (g & 31) >> 2; b = (b & 31) << 3 | (b & 31) >> 2;
	}
	else								// 4 bits each
	{
		r = (int) (second ? c >> 56 & 15 : c >> 60 & 15) * 17; g = (int) (second ? c >> 48 & 15 : c >> 52 & 15) * 17; b = (int) (second ? c >> 40 & 15 : c >> 44 & 15) * 17;
	}
	const u32 table = (u32) (second ? c >> 34 & 7 : c >> 37 & 7);
	int step = STEP[table][c >> texel & 1];
	if (c >> (16 + texel) & 1) step = -step;
	out[0] = clamp255 (r + step); out[1] = clamp255 (g + step); out[2] = clamp255 (b + step);
	out[3] = alpha ? (int) (a >> (4 * texel) & 15) * 17 : 255;
}

// The texel at (s, t), t from the bottom: a texture's rows are stored from the top.
static void fetch (const Texture &t, u32 x, u32 y, int out[4]);
static inline void texel (const Texture &t, int s, int tt, int out[4])
{
	const u32 x = t.maskW ? (u32) s & t.maskW : (u32) wrapCoord (s, (int) t.w, t.wrapS);
	const u32 y = t.h - 1 - (t.maskH ? (u32) tt & t.maskH : (u32) wrapCoord (tt, (int) t.h, t.wrapT));
	if (t.px)
	{
		const u32 c = t.px[y * t.w + x];
		out[0] = (int) (c & 255); out[1] = (int) (c >> 8 & 255); out[2] = (int) (c >> 16 & 255); out[3] = (int) (c >> 24);
		return;
	}
	fetch (t, x, y, out);
}
// The texel in column x of row y (from the top), from the program's bytes.
static void fetch (const Texture &t, u32 x, u32 y, int out[4])
{
	if (t.format >= 12)						// ETC1 (12), ETC1A4 (13)
	{
		const u32 size = t.format == 13 ? 16 : 8;
		const u32 tile = (x >> 3) + (y >> 3) * (t.w >> 3), sub = (x >> 2 & 1) + 2 * (y >> 2 & 1);
		etc1Texel (t.data + (tile * 4 + sub) * size, t.format == 13, x & 3, y & 3, out);
		return;
	}
	const u32 n = tiled (x, y, t.w);
	const u8 *d = t.data;
	switch (t.format)
	{
	case 0: { const u8 *q = d + n * 4; out[0] = q[3]; out[1] = q[2]; out[2] = q[1]; out[3] = q[0]; break; }		// RGBA8
	case 1: { const u8 *q = d + n * 3; out[0] = q[2]; out[1] = q[1]; out[2] = q[0]; out[3] = 255; break; }			// RGB8
	case 2: { u32 v = (u32) (d[n * 2] | d[n * 2 + 1] << 8); out[0] = (int) (v >> 11) * 255 / 31; out[1] = (int) (v >> 6 & 31) * 255 / 31; out[2] = (int) (v >> 1 & 31) * 255 / 31; out[3] = (v & 1) ? 255 : 0; break; }	// RGB5A1
	case 3: { u32 v = (u32) (d[n * 2] | d[n * 2 + 1] << 8); out[0] = (int) (v >> 11) * 255 / 31; out[1] = (int) (v >> 5 & 63) * 255 / 63; out[2] = (int) (v & 31) * 255 / 31; out[3] = 255; break; }			// RGB565
	case 4: { u32 v = (u32) (d[n * 2] | d[n * 2 + 1] << 8); out[0] = (int) (v >> 12) * 17; out[1] = (int) (v >> 8 & 15) * 17; out[2] = (int) (v >> 4 & 15) * 17; out[3] = (int) (v & 15) * 17; break; }			// RGBA4
	case 5: out[0] = out[1] = out[2] = d[n * 2 + 1]; out[3] = d[n * 2]; break;						// LA8
	case 6: out[0] = d[n * 2 + 1]; out[1] = d[n * 2]; out[2] = 0; out[3] = 255; break;					// HILO8
	case 7: out[0] = out[1] = out[2] = d[n]; out[3] = 255; break;								// L8
	case 8: out[0] = out[1] = out[2] = 0; out[3] = d[n]; break;								// A8
	case 9: out[0] = out[1] = out[2] = (d[n] >> 4) * 17; out[3] = (d[n] & 15) * 17; break;					// LA4
	case 10: { int v = ((n & 1) ? d[n >> 1] >> 4 : d[n >> 1] & 15) * 17; out[0] = out[1] = out[2] = v; out[3] = 255; break; }	// L4
	case 11: { int v = ((n & 1) ? d[n >> 1] >> 4 : d[n >> 1] & 15) * 17; out[0] = out[1] = out[2] = 0; out[3] = v; break; }	// A4
	default: out[0] = out[1] = out[2] = out[3] = 255; break;								// (ETC1: not yet)
	}
}

// The procedural texture: each coordinate is clamped, the two are combined into one number (u, v, their mean,
// their distance...), which goes through a map, then picks a colour in the table; the alpha may have its own
// combination and map.
struct ProcTex { bool on; int coord; u32 clampU, clampV, funcRgb, funcA; bool separateA; u32 width, offset; };

static inline float procClamp (float c, u32 mode)
{
	c = fabsf (c);
	switch (mode)
	{
	case 0: return c > 1.0f ? 0.0f : c;					// nothing outside
	case 1: return c > 1.0f ? 1.0f : c;					// the edge
	case 2: return c - floorf (c);						// repeated
	case 3: { const int i = (int) c; const float f = c - (float) i; return (i & 1) ? 1.0f - f : f; }	// mirrored
	default: return c > 0.5f ? 1.0f : 0.0f;					// a pulse
	}
}
static inline float procCombine (float u, float v, u32 func)
{
	switch (func)
	{
	case 0: return u; case 1: return u * u; case 2: return v; case 3: return v * v;
	case 4: return (u + v) * 0.5f; case 5: return (u * u + v * v) * 0.5f;
	case 6: { const float d = sqrtf (u * u + v * v); return d > 1.0f ? 1.0f : d; }
	case 7: return u < v ? u : v; case 8: return u > v ? u : v;
	default: { const float a = (u + v) * 0.5f, d = sqrtf (u * u + v * v), x = a > d ? a : d; return x > 1.0f ? 1.0f : x; }
	}
}
static inline float procMapped (const Pica *p, int table, float x)
{
	x *= 128.0f;
	int i = (int) x;
	if (i < 0) i = 0; else if (i > 127) i = 127;
	const float r = p->procMap[table][i] + p->procMapDiff[table][i] * (x - (float) i);
	return r < 0 ? 0 : r > 1 ? 1 : r;
}
static void procTexel (const Pica *p, const ProcTex &pt, float cu, float cv, int out[4])
{
	const float u = procClamp (cu, pt.clampU), v = procClamp (cv, pt.clampV);
	const float x = procMapped (p, 0, procCombine (u, v, pt.funcRgb));
	u32 i = pt.offset + (u32) (x * (float) (pt.width ? pt.width - 1 : 0) + 0.5f);
	if (i > 255) i = 255;
	const u32 c = p->procColor[i];
	out[0] = (int) (c & 255); out[1] = (int) (c >> 8 & 255); out[2] = (int) (c >> 16 & 255); out[3] = (int) (c >> 24);
	if (pt.separateA) out[3] = (int) (procMapped (p, 1, procCombine (u, v, pt.funcA)) * 255.0f + 0.5f);
}

static void flush (Pica *p);

// The decoded pixels of a texture: kept by where the program's bytes are, and made again when they changed.
static void texDecoded (Pica *p, Texture &t)
{
	if (!t.on || t.w * t.h > 1024 * 1024) return;
	u64 check = 1469598103934665603ull ^ t.bytes;
	const u32 step = t.bytes / 97 + 1;
	for (u32 i = 0; i < t.bytes; i += step) { check ^= t.data[i]; check *= 1099511628211ull; }
	Pica::Decoded *slot = 0, *oldest = &p->decoded[0];
	for (auto &d : p->decoded)
	{
		if (d.px && d.data == t.data && d.w == t.w && d.h == t.h && d.format == t.format) { slot = &d; break; }
		if (d.used < oldest->used) oldest = &d;
	}
	if (slot && slot->check == check) { slot->used = ++p->decodedClock; t.px = slot->px; return; }
	if (p->jobCount) flush (p);						// (queued triangles may read what changes here)
	if (!slot) { slot = oldest; free (slot->px); slot->px = 0; }
	if (!slot->px || slot->w * slot->h != t.w * t.h) { free (slot->px); slot->px = (u32 *) malloc ((size_t) t.w * t.h * 4); }
	slot->data = t.data; slot->w = t.w; slot->h = t.h; slot->format = t.format; slot->check = check; slot->used = ++p->decodedClock;
	if (!slot->px) return;
	for (u32 y = 0; y < t.h; y++)
		for (u32 x = 0; x < t.w; x++)
		{
			int c[4];
			fetch (t, x, y, c);
			slot->px[y * t.w + x] = (u32) c[0] | (u32) c[1] << 8 | (u32) c[2] << 16 | (u32) c[3] << 24;
		}
	t.px = slot->px;
}

// ---- lighting ---------------------------------------------------------------------------------------------------------
enum { LUT_D0 = 0, LUT_D1 = 1, LUT_FR = 3, LUT_RB = 4, LUT_RG = 5, LUT_RR = 6, LUT_DA = 16 };

static float f16 (u32 v)						// the GPU's 16-bit float: a sign, 5 bits of exponent, 10 of mantissa
{
	v &= 0xFFFF;
	const u32 sign = v >> 15, exp = v >> 10 & 31, mant = v & 0x3FF;
	u32 bits = !exp && !mant ? 0 : exp == 31 ? 0xFFu << 23 | mant << 13 : (exp + 112) << 23 | mant << 13;
	bits |= sign << 31;
	return bitsFloat (bits);
}
static float f20 (u32 v)						// ... 20-bit: a sign, 7 of exponent, 12 of mantissa
{
	v &= 0xFFFFF;
	const u32 sign = v >> 19, exp = v >> 12 & 0x7F, mant = v & 0xFFF;
	u32 bits = !exp && !mant ? 0 : exp == 0x7F ? 0xFFu << 23 | mant << 11 : (exp + 64) << 23 | mant << 11;
	bits |= sign << 31;
	return bitsFloat (bits);
}
static inline void lightColor (u32 v, float out[3]) { out[0] = (float) (v >> 20 & 255) / 255.0f; out[1] = (float) (v >> 10 & 255) / 255.0f; out[2] = (float) (v & 255) / 255.0f; }

struct Lighting
{
	bool on; int count;
	float ambient[3];
	struct Light { float spec0[3], spec1[3], diff[3], amb[3], pos[3]; bool directional, twoSided, distance; float bias, scale; int id; } light[8];
	struct Lut { bool on, abs; u32 input; float scale; } d0, d1, fr, rr, rg, rb;
	u32 fresnel; bool clampHighlights;
};

static void lightingState (const Pica *p, Lighting &l)
{
	const u32 *r = p->regs;
	l.on = (r[R_LIGHTING] & 1) != 0;
	if (!l.on) return;
	l.count = (int) (r[R_LIGHT_COUNT] & 7) + 1;
	lightColor (r[R_LIGHT_AMBIENT], l.ambient);
	const u32 c0 = r[R_LIGHT_CONFIG0], c1 = r[R_LIGHT_CONFIG1];
	l.fresnel = c0 >> 2 & 3; l.clampHighlights = (c0 >> 27 & 1) != 0;
	for (int i = 0; i < l.count; i++)
	{
		Lighting::Light &g = l.light[i];
		g.id = (int) (r[R_LIGHT_PERMUTATION] >> (i * 4) & 7);
		const u32 *q = r + R_LIGHT0 + g.id * 0x10;
		lightColor (q[0], g.spec0); lightColor (q[1], g.spec1); lightColor (q[2], g.diff); lightColor (q[3], g.amb);
		g.pos[0] = f16 (q[4]); g.pos[1] = f16 (q[4] >> 16); g.pos[2] = f16 (q[5]);
		g.directional = (q[9] & 1) != 0; g.twoSided = (q[9] & 2) != 0;
		g.distance = !(c1 >> (24 + g.id) & 1);
		g.bias = f20 (q[10]); g.scale = f20 (q[11]);
	}
	// which tables the chosen configuration has, and which the program switched off
	static const u8 HAS[9] = { 0x01 | 0x08, 0x04 | 0x08, 0x01 | 0x02 | 0x08, 0x01 | 0x02 | 0x04, 0x01 | 0x02 | 0x08 | 0x30, 0x01 | 0x04 | 0x08 | 0x30, 0x01 | 0x02 | 0x04 | 0x08, 0, 0x3F };
	const u32 cfg = c0 >> 4 & 15, has = cfg < 9 ? HAS[cfg] : 0x3F;	// bits: D0, D1, FR, RR, RG, RB
	static const float SCALE[8] = { 1, 2, 4, 8, 1, 1, 0.25f, 0.5f };
	struct { Lighting::Lut *lut; u32 hasBit, offBit, shift; } T[6] = {
		{ &l.d0, 0x01, 16, 0 }, { &l.d1, 0x02, 17, 4 }, { &l.fr, 0x04, 19, 12 }, { &l.rr, 0x08, 22, 24 }, { &l.rg, 0x10, 21, 20 }, { &l.rb, 0x20, 20, 16 } };
	for (int i = 0; i < 6; i++)
	{
		Lighting::Lut &t = *T[i].lut;
		t.on = (has & T[i].hasBit) && !(c1 >> T[i].offBit & 1);
		t.abs = !(r[R_LIGHT_LUT_ABS] >> (T[i].shift + 1) & 1);
		t.input = r[R_LIGHT_LUT_SELECT] >> T[i].shift & 7;
		t.scale = SCALE[r[R_LIGHT_LUT_SCALE] >> T[i].shift & 7];
	}
}

static inline float lutLook (const Pica *p, int table, float x, bool abs)
{
	int i; float d;
	if (abs) { x = fabsf (x) * 256.0f; i = (int) x; if (i > 255) i = 255; d = x - (float) i; }
	else { x *= 128.0f; float fl = floorf (x); i = (int) fl; if (i < -128) i = -128; else if (i > 127) i = 127; d = x - (float) i; i &= 0xFF; }
	return p->lightLut[table][i] + p->lightLutDiff[table][i] * d;
}
static inline void norm3 (float v[3]) { const float n = sqrtf (v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); if (n > 0) { v[0] /= n; v[1] /= n; v[2] /= n; } }
static inline float dot3 (const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

// ---- the fragment's way to the buffers ---------------------------------------------------------------------------------
struct Target
{
	u8 *color, *depth;
	int w, h;
	u32 colorFormat, colorBytes, depthBytes;
	bool colorWrite, depthWrite;
	int sx0, sy0, sx1, sy1;							// what may be drawn (the buffer, cut by the scissor)
};

static bool target (const Pica *p, Target &t)
{
	const u32 dim = p->regs[R_FB_DIM];
	t.w = (int) (dim & 0x7FF); t.h = (int) (dim >> 12 & 0x3FF) + 1;
	if (t.w <= 0 || t.w > 1024) return false;
	t.colorFormat = p->regs[R_COLOR_FORMAT] >> 16 & 7;
	t.colorBytes = t.colorFormat == 0 ? 4 : t.colorFormat == 1 ? 3 : 2;
	const u32 df = p->regs[R_DEPTH_FORMAT] & 3;
	t.depthBytes = df == 0 ? 2 : df == 2 ? 3 : 4;
	t.color = p->regs[R_COLOR_ADDR] ? p->m->physPtr (p->regs[R_COLOR_ADDR] << 3, (u32) (t.w * t.h) * t.colorBytes) : 0;
	t.depth = p->regs[R_DEPTH_ADDR] ? p->m->physPtr (p->regs[R_DEPTH_ADDR] << 3, (u32) (t.w * t.h) * t.depthBytes) : 0;
	t.colorWrite = t.color && (p->regs[R_COLOR_WRITE] & 0xF) != 0;
	t.depthWrite = t.depth && (p->regs[R_DEPTH_WRITE] & 3) != 0;
	t.sx0 = 0; t.sy0 = 0; t.sx1 = t.w; t.sy1 = t.h;
	if ((p->regs[R_SCISSOR_MODE] & 3) == 3)					// draw inside the rectangle only
	{
		const int x0 = (int) (p->regs[R_SCISSOR_POS] & 0x3FF), y0 = (int) (p->regs[R_SCISSOR_POS] >> 16 & 0x3FF);
		const int x1 = (int) (p->regs[R_SCISSOR_DIM] & 0x3FF) + 1, y1 = (int) (p->regs[R_SCISSOR_DIM] >> 16 & 0x3FF) + 1;
		if (x0 > t.sx0) t.sx0 = x0;
		if (y0 > t.sy0) t.sy0 = y0;
		if (x1 < t.sx1) t.sx1 = x1;
		if (y1 < t.sy1) t.sy1 = y1;
	}
	return t.color != 0;
}

static inline void readColor (const Target &t, const u8 *q, int c[4])
{
	switch (t.colorFormat)
	{
	case 0: c[0] = q[3]; c[1] = q[2]; c[2] = q[1]; c[3] = q[0]; break;
	case 1: c[0] = q[2]; c[1] = q[1]; c[2] = q[0]; c[3] = 255; break;
	case 2: { u32 v = (u32) (q[0] | q[1] << 8); c[0] = (int) (v >> 11) * 255 / 31; c[1] = (int) (v >> 6 & 31) * 255 / 31; c[2] = (int) (v >> 1 & 31) * 255 / 31; c[3] = (v & 1) ? 255 : 0; break; }
	case 3: { u32 v = (u32) (q[0] | q[1] << 8); c[0] = (int) (v >> 11) * 255 / 31; c[1] = (int) (v >> 5 & 63) * 255 / 63; c[2] = (int) (v & 31) * 255 / 31; c[3] = 255; break; }
	default: { u32 v = (u32) (q[0] | q[1] << 8); c[0] = (int) (v >> 12) * 17; c[1] = (int) (v >> 8 & 15) * 17; c[2] = (int) (v >> 4 & 15) * 17; c[3] = (int) (v & 15) * 17; break; }
	}
}
static inline void writeColor (const Target &t, u8 *q, const int c[4])
{
	switch (t.colorFormat)
	{
	case 0: q[3] = (u8) c[0]; q[2] = (u8) c[1]; q[1] = (u8) c[2]; q[0] = (u8) c[3]; break;
	case 1: q[2] = (u8) c[0]; q[1] = (u8) c[1]; q[0] = (u8) c[2]; break;
	case 2: { u32 v = (u32) (c[0] >> 3 << 11 | c[1] >> 3 << 6 | c[2] >> 3 << 1 | c[3] >> 7); q[0] = (u8) v; q[1] = (u8) (v >> 8); break; }
	case 3: { u32 v = (u32) (c[0] >> 3 << 11 | c[1] >> 2 << 5 | c[2] >> 3); q[0] = (u8) v; q[1] = (u8) (v >> 8); break; }
	default: { u32 v = (u32) (c[0] >> 4 << 12 | c[1] >> 4 << 8 | c[2] >> 4 << 4 | c[3] >> 4); q[0] = (u8) v; q[1] = (u8) (v >> 8); break; }
	}
}

static inline bool compare (u32 func, u32 a, u32 b)
{
	switch (func) { case 0: return false; case 1: return true; case 2: return a == b; case 3: return a != b; case 4: return a < b; case 5: return a <= b; case 6: return a > b; default: return a >= b; }
}

// A row of fragments: 255 - a channel; two or three channels combined (false: the result is the first one as it is).
static inline void rowInv (int n, int *__restrict d, const int *s) { for (int i = 0; i < n; i++) d[i] = 255 - s[i]; }
static inline bool rowCombine (u32 mode, int n, int *__restrict d, const int *a, const int *b, const int *c)
{
	switch (mode)
	{
	case 1: for (int i = 0; i < n; i++) d[i] = a[i] * b[i] / 255; return true;
	case 2: for (int i = 0; i < n; i++) d[i] = clamp255 (a[i] + b[i]); return true;
	case 3: for (int i = 0; i < n; i++) d[i] = clamp255 (a[i] + b[i] - 128); return true;
	case 4: for (int i = 0; i < n; i++) d[i] = (a[i] * c[i] + b[i] * (255 - c[i])) / 255; return true;
	case 5: for (int i = 0; i < n; i++) d[i] = clamp255 (a[i] - b[i]); return true;
	case 8: for (int i = 0; i < n; i++) d[i] = clamp255 (a[i] * b[i] / 255 + c[i]); return true;
	case 9: for (int i = 0; i < n; i++) d[i] = clamp255 ((a[i] + b[i]) * c[i] / 255); return true;
	default: return false;
	}
}

// A row blended with what is there (each with its factor), or combined with it bit by bit.
static inline void rowBlend (u32 eq, int n, int *__restrict d, const int *s, const int *sf, const int *t, const int *tf)
{
	switch (eq)
	{
	case 1: for (int i = 0; i < n; i++) d[i] = clamp255 ((s[i] * sf[i] - t[i] * tf[i]) / 255); break;
	case 2: for (int i = 0; i < n; i++) d[i] = clamp255 ((t[i] * tf[i] - s[i] * sf[i]) / 255); break;
	case 3: for (int i = 0; i < n; i++) d[i] = s[i] < t[i] ? s[i] : t[i]; break;
	case 4: for (int i = 0; i < n; i++) d[i] = s[i] > t[i] ? s[i] : t[i]; break;
	default: for (int i = 0; i < n; i++) d[i] = clamp255 ((s[i] * sf[i] + t[i] * tf[i]) / 255); break;
	}
}
static inline void rowLogic (u32 op, int n, int *__restrict o, const int *sr, const int *ds)
{
#define EACH(expr) for (int i = 0; i < n; i++) { const int s = sr[i], d = ds[i]; (void) s; (void) d; o[i] = (expr); } break
	switch (op)
	{
	case 0: EACH (0); case 1: EACH (s & d); case 2: EACH (s & ~d & 255); case 3: EACH (s);
	case 4: EACH (255); case 5: EACH (~s & 255); case 6: EACH (d); case 7: EACH (~d & 255);
	case 8: EACH (~(s & d) & 255); case 9: EACH (s | d); case 10: EACH (~(s | d) & 255); case 11: EACH (s ^ d);
	case 12: EACH (~(s ^ d) & 255); case 13: EACH (~s & d & 255); case 14: EACH ((s | ~d) & 255); default: EACH ((~s | d) & 255);
	}
#undef EACH
}

// One combiner stage's operand: a source's colour seen through an operand's choice.
static inline void tevColor (const int src[4], u32 operand, int out[3])
{
	switch (operand)
	{
	case 0: out[0] = src[0]; out[1] = src[1]; out[2] = src[2]; break;
	case 1: out[0] = 255 - src[0]; out[1] = 255 - src[1]; out[2] = 255 - src[2]; break;
	case 2: out[0] = out[1] = out[2] = src[3]; break;
	case 3: out[0] = out[1] = out[2] = 255 - src[3]; break;
	case 4: out[0] = out[1] = out[2] = src[0]; break;
	case 5: out[0] = out[1] = out[2] = 255 - src[0]; break;
	case 8: out[0] = out[1] = out[2] = src[1]; break;
	case 9: out[0] = out[1] = out[2] = 255 - src[1]; break;
	case 12: out[0] = out[1] = out[2] = src[2]; break;
	case 13: out[0] = out[1] = out[2] = 255 - src[2]; break;
	default: out[0] = src[0]; out[1] = src[1]; out[2] = src[2]; break;
	}
}
static inline int tevAlpha (const int src[4], u32 operand)
{
	switch (operand) { case 0: return src[3]; case 1: return 255 - src[3]; case 2: return src[0]; case 3: return 255 - src[0]; case 4: return src[1]; case 5: return 255 - src[1]; case 6: return src[2]; default: return 255 - src[2]; }
}
static inline int tevCombine (u32 mode, int a, int b, int c)
{
	switch (mode)
	{
	case 0: return a;
	case 1: return a * b / 255;
	case 2: return clamp255 (a + b);
	case 3: return clamp255 (a + b - 128);
	case 4: return (a * c + b * (255 - c)) / 255;
	case 5: return clamp255 (a - b);
	case 8: return clamp255 (a * b / 255 + c);
	case 9: return clamp255 ((a + b) * c / 255);
	default: return a;
	}
}

static inline void blendFactor (u32 f, const int src[4], const int dst[4], const int cst[4], int out[4])
{
	switch (f)
	{
	case 0: out[0] = out[1] = out[2] = out[3] = 0; break;
	case 1: out[0] = out[1] = out[2] = out[3] = 255; break;
	case 2: for (int i = 0; i < 4; i++) out[i] = src[i]; break;
	case 3: for (int i = 0; i < 4; i++) out[i] = 255 - src[i]; break;
	case 4: for (int i = 0; i < 4; i++) out[i] = dst[i]; break;
	case 5: for (int i = 0; i < 4; i++) out[i] = 255 - dst[i]; break;
	case 6: out[0] = out[1] = out[2] = out[3] = src[3]; break;
	case 7: out[0] = out[1] = out[2] = out[3] = 255 - src[3]; break;
	case 8: out[0] = out[1] = out[2] = out[3] = dst[3]; break;
	case 9: out[0] = out[1] = out[2] = out[3] = 255 - dst[3]; break;
	case 10: for (int i = 0; i < 4; i++) out[i] = cst[i]; break;
	case 11: for (int i = 0; i < 4; i++) out[i] = 255 - cst[i]; break;
	case 12: out[0] = out[1] = out[2] = out[3] = cst[3]; break;
	case 13: out[0] = out[1] = out[2] = out[3] = 255 - cst[3]; break;
	default: { int s = src[3] < 255 - dst[3] ? src[3] : 255 - dst[3]; out[0] = out[1] = out[2] = s; out[3] = 255; break; }
	}
}
static inline int blendEq (u32 eq, int s, int sf, int d, int df)
{
	switch (eq)
	{
	case 1: return clamp255 ((s * sf - d * df) / 255);
	case 2: return clamp255 ((d * df - s * sf) / 255);
	case 3: return s < d ? s : d;
	case 4: return s > d ? s : d;
	default: return clamp255 ((s * sf + d * df) / 255);
	}
}

// What a draw's fragments all share, read from the registers once.
enum { CH = 32 };							// the fragments shaded at once
typedef int Row[CH];							// one channel of a group's fragments

struct Fragment
{
	Texture tex[3];
	ProcTex proc;
	Lighting light;
	struct Stage { u32 src[3], srcA[3], op[3], opA[3], mode, modeA; int konst[4]; int scale, scaleA; bool pass; } stage[6];
	int bufferColor[4]; u32 updateRgb, updateA;
	bool alphaTest; u32 alphaFunc, alphaRef;
	bool depthTest; u32 depthFunc; bool depthMask; bool rgbaMask[4];
	bool blend; u32 eqRgb, eqA, srcRgb, dstRgb, srcA, dstA; int blendColor[4]; u32 logicOp;
	// what the stages really read (the rest is not computed), and a fragment that simply replaces what is there
	bool useTex[3], useProc, useLight, replace;
	Row konstRow[6][4], bufferRow[4], zeroRow, fullRow;			// (the stages' constants, the buffer's colour, 0 and 255 as rows)
	bool needPrimary, usesBuffer; int stageEnd;	// the vertex colour is read; a stage reads the buffer; one past the last stage that does something
};

static void fragmentState (const Pica *p, Fragment &f)
{
	for (int i = 0; i < 3; i++) texInfo (p, i, f.tex[i]);
	lightingState (p, f.light);
	{
		const u32 cfg = p->regs[R_TEX_CONFIG], r0 = p->regs[R_PROCTEX0];
		f.proc.on = (cfg >> 10 & 1) != 0; f.proc.coord = (int) (cfg >> 8 & 3);
		f.proc.clampU = r0 & 7; f.proc.clampV = r0 >> 3 & 7; f.proc.funcRgb = r0 >> 6 & 15; f.proc.funcA = r0 >> 10 & 15;
		f.proc.separateA = (r0 >> 14 & 1) != 0;
		f.proc.width = p->regs[R_PROCTEX4] >> 11 & 0xFF; f.proc.offset = p->regs[R_PROCTEX5] & 0xFF;
		if (f.proc.on && (r0 >> 15 & 1)) p->m->note ("procedural texture noise");
	}
	for (int i = 0; i < 6; i++)
	{
		const u32 *r = p->regs + TEV_BASE[i];				// source, operand, combiner, colour, scale
		Fragment::Stage &s = f.stage[i];
		for (int k = 0; k < 3; k++) { s.src[k] = r[0] >> (4 * k) & 15; s.srcA[k] = r[0] >> (16 + 4 * k) & 15; s.op[k] = r[1] >> (4 * k) & 15; s.opA[k] = r[1] >> (12 + 4 * k) & 7; }
		s.mode = r[2] & 15; s.modeA = r[2] >> 16 & 15;
		s.konst[0] = (int) (r[3] & 255); s.konst[1] = (int) (r[3] >> 8 & 255); s.konst[2] = (int) (r[3] >> 16 & 255); s.konst[3] = (int) (r[3] >> 24);
		s.scale = 1 << (r[4] & 3); s.scaleA = 1 << (r[4] >> 16 & 3);
		if (s.scale > 4) s.scale = 4;
		if (s.scaleA > 4) s.scaleA = 4;
		// a stage that hands on what it got (the previous stage's result, as it is)
		s.pass = s.mode == 0 && s.modeA == 0 && s.src[0] == 15 && s.srcA[0] == 15 && s.op[0] == 0 && s.opA[0] == 0 && s.scale == 1 && s.scaleA == 1;
	}
	const u32 bc = p->regs[R_TEV_BUFFER];
	f.bufferColor[0] = (int) (bc & 255); f.bufferColor[1] = (int) (bc >> 8 & 255); f.bufferColor[2] = (int) (bc >> 16 & 255); f.bufferColor[3] = (int) (bc >> 24);
	f.updateRgb = p->regs[R_TEV_UPDATE] >> 8 & 15; f.updateA = p->regs[R_TEV_UPDATE] >> 12 & 15;
	const u32 at = p->regs[R_ALPHA_TEST];
	f.alphaTest = (at & 1) != 0; f.alphaFunc = at >> 4 & 7; f.alphaRef = at >> 8 & 255;
	const u32 dm = p->regs[R_DEPTH_COLOR_MASK];
	f.depthTest = (dm & 1) != 0; f.depthFunc = dm >> 4 & 7;
	for (int i = 0; i < 4; i++) f.rgbaMask[i] = (dm >> (8 + i) & 1) != 0;
	f.depthMask = (dm >> 12 & 1) != 0;
	f.blend = (p->regs[R_COLOR_OP] >> 8 & 1) != 0;
	const u32 bf = p->regs[R_BLEND_FUNC];
	f.eqRgb = bf & 7; f.eqA = bf >> 8 & 7; f.srcRgb = bf >> 16 & 15; f.dstRgb = bf >> 20 & 15; f.srcA = bf >> 24 & 15; f.dstA = bf >> 28 & 15;
	const u32 k = p->regs[R_BLEND_COLOR];
	f.blendColor[0] = (int) (k & 255); f.blendColor[1] = (int) (k >> 8 & 255); f.blendColor[2] = (int) (k >> 16 & 255); f.blendColor[3] = (int) (k >> 24);
	f.logicOp = p->regs[R_LOGIC_OP] & 15;
	u32 used = 0;								// (a bit a source number)
	for (int i = 0; i < 6; i++)
		if (!f.stage[i].pass)
			for (int k = 0; k < 3; k++) used |= 1u << f.stage[i].src[k] | 1u << f.stage[i].srcA[k];
	for (int i = 0; i < 3; i++) f.useTex[i] = f.tex[i].on && (used >> (3 + i) & 1);
	f.useProc = f.proc.on && (used >> 6 & 1);
	f.useLight = f.light.on && (used & 6);
	f.stageEnd = 0;
	for (int i = 0; i < 6; i++) if (!f.stage[i].pass) f.stageEnd = i + 1;
	f.usesBuffer = (used >> 13 & 1) != 0;
	f.needPrimary = !f.stageEnd || (used & 0x8001) || (!f.useLight && (used & 6));
	for (int i = 0; i < CH; i++)
	{
		f.zeroRow[i] = 0; f.fullRow[i] = 255;
		for (int c = 0; c < 4; c++) { f.bufferRow[c][i] = f.bufferColor[c]; for (int st = 0; st < 6; st++) f.konstRow[st][c][i] = f.stage[st].konst[c]; }
	}
	const u32 skip = p->m->gpuSkip;
	if (skip & 1) f.useProc = false;
	if (skip & 8) f.useTex[0] = f.useTex[1] = f.useTex[2] = false;
	if (skip & 16) f.useLight = false;
	if (skip & 4) f.depthTest = false;
	f.replace = (skip & 2) || (f.blend && f.eqRgb == 0 && f.eqA == 0 && f.srcRgb == 1 && f.dstRgb == 0 && f.srcA == 1 && f.dstA == 0
		    && f.rgbaMask[0] && f.rgbaMask[1] && f.rgbaMask[2] && f.rgbaMask[3]);
}

// ---- triangles ---------------------------------------------------------------------------------------------------------
struct Screen { float x, y, z, invw; const Vertex *v; };

// A worker's group of fragments: where they are, their weights, then each input and each stage's result as rows.
struct Scratch
{
	int n, x[CH], y[CH]; u32 at[CH], depth[CH];
	float l0[CH], l1[CH], l2[CH], q0[CH], q1[CH], q2[CH], qs[CH], fa[CH], fb[CH], fq[7][CH];
	Row primary[4], tex[3][4], litP[4], litS[4], proc[4], prev[6][4], c[3][3], a[3], o[4];
	Row dst[4], bo[4], bf[2];					// what is in the buffer, what goes there, two blending factors
	float lf[29][CH];						// the lighting's rows (lightRows)
};

// The lighting of a group of fragments, by rows: from their normals' quaternions (k.fq[0..3]) and their view
// vectors (k.fq[4..6]) to their primary (ambient + diffuse) and secondary (specular) colours. For each light: its
// direction and the half-way vector, the tables read at the dot products the configuration names, the sums.
static void lightRows (const Pica *p, const Lighting &l, int n, Scratch &k, Row *litP, Row *litS)
{
	float *__restrict nx = k.lf[0], *__restrict ny = k.lf[1], *__restrict nz = k.lf[2];		// the normal
	float *__restrict vx = k.lf[3], *__restrict vy = k.lf[4], *__restrict vz = k.lf[5];		// the view vector, of length 1
	float *__restrict lx = k.lf[6], *__restrict ly = k.lf[7], *__restrict lz = k.lf[8];		// the light's direction
	float *__restrict att = k.lf[9], *__restrict nl = k.lf[10], *__restrict lit = k.lf[11];
	float *__restrict in0 = k.lf[12], *__restrict in1 = k.lf[13], *__restrict in2 = k.lf[14];
	float *__restrict d0 = k.lf[15], *__restrict d1 = k.lf[16], *__restrict rr = k.lf[17], *__restrict rg = k.lf[18], *__restrict rb = k.lf[19];
	float *const dif[3] = { k.lf[20], k.lf[21], k.lf[22] }, *const spe[3] = { k.lf[23], k.lf[24], k.lf[25] };
	float *__restrict aP = k.lf[26], *__restrict aS = k.lf[27], *__restrict zero = k.lf[28];
	const float *__restrict q0 = k.fq[0], *__restrict q1 = k.fq[1], *__restrict q2 = k.fq[2], *__restrict q3 = k.fq[3];
	const float *__restrict wx = k.fq[4], *__restrict wy = k.fq[5], *__restrict wz = k.fq[6];
	for (int i = 0; i < n; i++)
	{
		float a = q0[i], b = q1[i], c = q2[i], d = q3[i];
		const float qn = sqrtf (a * a + b * b + c * c + d * d);
		if (qn > 0) { a /= qn; b /= qn; c /= qn; d /= qn; }
		nx[i] = 2 * (a * c + d * b); ny[i] = 2 * (b * c - d * a); nz[i] = 1 - 2 * (a * a + b * b);
		float x = wx[i], y = wy[i], z = wz[i];
		const float vn = sqrtf (x * x + y * y + z * z);
		if (vn > 0) { x /= vn; y /= vn; z /= vn; }
		vx[i] = x; vy[i] = y; vz[i] = z;
		aP[i] = 1.0f; aS[i] = 1.0f; zero[i] = 0.0f;
	}
	for (int c = 0; c < 3; c++) { float *__restrict a = dif[c], *__restrict b = spe[c]; for (int i = 0; i < n; i++) { a[i] = l.ambient[c]; b[i] = 0.0f; } }
	const float *const ins[6] = { in0, in1, in2, nl, zero, zero };
	auto lut = [&] (const Lighting::Lut &t, int table, float *__restrict out)
	{
		if (!t.on) { for (int i = 0; i < n; i++) out[i] = 1.0f; return; }
		const float *x = ins[t.input < 6 ? t.input : 0];
		for (int i = 0; i < n; i++) out[i] = lutLook (p, table, x[i], t.abs) * t.scale;
	};
	for (int li = 0; li < l.count; li++)
	{
		const Lighting::Light &g = l.light[li];
		for (int i = 0; i < n; i++)
		{
			float x = g.pos[0], y = g.pos[1], z = g.pos[2];
			if (!g.directional) { x += wx[i]; y += wy[i]; z += wz[i]; }
			const float len = sqrtf (x * x + y * y + z * z);
			att[i] = len;						// (the distance, for now)
			if (len > 0) { x /= len; y /= len; z /= len; }
			lx[i] = x; ly[i] = y; lz[i] = z;
		}
		if (g.distance)
			for (int i = 0; i < n; i++)
			{
				float x = g.scale * att[i] + g.bias;
				x = x < 0 ? 0 : x > 1 ? 1 : x;
				att[i] = lutLook (p, LUT_DA + g.id, x, true);
			}
		else for (int i = 0; i < n; i++) att[i] = 1.0f;
		for (int i = 0; i < n; i++)
		{
			float hx = vx[i] + lx[i], hy = vy[i] + ly[i], hz = vz[i] + lz[i];	// half-way between the eye and the light
			const float hn = sqrtf (hx * hx + hy * hy + hz * hz);
			if (hn > 0) { hx /= hn; hy /= hn; hz /= hn; }
			const float d = nx[i] * lx[i] + ny[i] * ly[i] + nz[i] * lz[i];
			nl[i] = d;
			lit[i] = g.twoSided ? fabsf (d) : d > 0 ? d : 0;
			in0[i] = nx[i] * hx + ny[i] * hy + nz[i] * hz;
			in1[i] = vx[i] * hx + vy[i] * hy + vz[i] * hz;
			in2[i] = nx[i] * vx[i] + ny[i] * vy[i] + nz[i] * vz[i];
		}
		lut (l.d0, LUT_D0, d0); lut (l.d1, LUT_D1, d1); lut (l.rr, LUT_RR, rr);
		const float *refl[3] = { rr, rr, rr };
		if (l.rg.on) { lut (l.rg, LUT_RG, rg); refl[1] = rg; }
		if (l.rb.on) { lut (l.rb, LUT_RB, rb); refl[2] = rb; }
		for (int c = 0; c < 3; c++)
		{
			float *__restrict a = dif[c], *__restrict b = spe[c];
			const float *r = refl[c];
			const float diff = g.diff[c], amb = g.amb[c], spec0 = g.spec0[c], spec1 = g.spec1[c];
			const bool clampH = l.clampHighlights;
			for (int i = 0; i < n; i++)
			{
				const float high = clampH && nl[i] <= 0 ? 0.0f : 1.0f;
				a[i] += (diff * lit[i] + amb) * att[i];
				b[i] += (d0[i] * spec0 + d1[i] * r[i] * spec1) * high * att[i];
			}
		}
		if (li == l.count - 1 && l.fr.on)
		{
			lut (l.fr, LUT_FR, d0);
			if (l.fresnel & 1) for (int i = 0; i < n; i++) aP[i] = d0[i];
			if (l.fresnel & 2) for (int i = 0; i < n; i++) aS[i] = d0[i];
		}
	}
	for (int c = 0; c < 4; c++)
	{
		const float *a = c < 3 ? dif[c] : aP, *b = c < 3 ? spe[c] : aS;
		int *__restrict o = litP[c], *__restrict q = litS[c];
		for (int i = 0; i < n; i++)
		{
			o[i] = a[i] <= 0 ? 0 : a[i] >= 1 ? 255 : (int) (a[i] * 255.0f + 0.5f);
			q[i] = b[i] <= 0 ? 0 : b[i] >= 1 ? 255 : (int) (b[i] * 255.0f + 0.5f);
		}
	}
}

// A triangle on the screen: its side (the area's sign: the culling) and its box, cut by what may be drawn.
// False: nothing of it is drawn. (One function for the queue and for the rasterizer: the same answer.)
static __attribute__ ((noinline)) bool triBox (const Target &t, u32 cull, const Screen &s0, const Screen &s1, const Screen &s2, float &area, int box[4])
{
	area = (s1.x - s0.x) * (s2.y - s0.y) - (s1.y - s0.y) * (s2.x - s0.x);
	if (area == 0.0f) return false;
	if (cull == 1 && area > 0) return false;				// 1: the counter-clockwise ones (the "front") are removed
	if (cull == 2 && area < 0) return false;				// 2: the clockwise ones (the "back") -- what games use
	float minx = s0.x < s1.x ? s0.x : s1.x, maxx = s0.x > s1.x ? s0.x : s1.x, miny = s0.y < s1.y ? s0.y : s1.y, maxy = s0.y > s1.y ? s0.y : s1.y;
	if (s2.x < minx) minx = s2.x;
	if (s2.x > maxx) maxx = s2.x;
	if (s2.y < miny) miny = s2.y;
	if (s2.y > maxy) maxy = s2.y;
	int x0 = (int) floorf (minx), x1 = (int) ceilf (maxx), y0 = (int) floorf (miny), y1 = (int) ceilf (maxy);
	if (x0 < t.sx0) x0 = t.sx0;
	if (y0 < t.sy0) y0 = t.sy0;
	if (x1 > t.sx1) x1 = t.sx1;
	if (y1 > t.sy1) y1 = t.sy1;
	if (x0 >= x1 || y0 >= y1) return false;
	box[0] = x0; box[1] = y0; box[2] = x1; box[3] = y1;
	return true;
}

// The triangle's fragments in the buffer's rows rowLo..rowHi (a band: counted from the bottom).
static void rasterize (Pica *p, const Target &t, const Fragment &f, u32 cull, Screen s0, Screen s1, Screen s2, int worker, int rowLo, int rowHi)
{
	float area; int box[4];
	if (!triBox (t, cull, s0, s1, s2, area, box)) return;
	if (area < 0) { Screen tmp = s1; s1 = s2; s2 = tmp; area = -area; }	// (turned so that its area is positive)
	const int x0 = box[0], x1 = box[2];
	int y0 = t.h - 1 - rowHi, y1 = t.h - rowLo;
	if (y0 < box[1]) y0 = box[1];
	if (y1 > box[3]) y1 = box[3];
	if (y0 >= y1) return;
	if (p->m->gpuSkip & 32) return;
	u64 nPixels = 0, nDepth = 0, nAlpha = 0;
	const float inv = 1.0f / area;
	const u32 depthMax = t.depthBytes == 2 ? 0xFFFF : 0xFFFFFF;
	// an edge shared by two triangles belongs to one: the left and the lower ones are in
	const float e0x = s2.x - s1.x, e0y = s2.y - s1.y, e1x = s0.x - s2.x, e1y = s0.y - s2.y, e2x = s1.x - s0.x, e2y = s1.y - s0.y;
	const bool in0 = e0y < 0 || (e0y == 0 && e0x > 0), in1 = e1y < 0 || (e1y == 0 && e1x > 0), in2 = e2y < 0 || (e2y == 0 && e2x > 0);
	Scratch &k = p->scratch[worker];
	k.n = 0;
	// where a stage finds each source's channels (an input the draw does not use: its neutral value)
	const int *T[16][4];
	const int *const neutral[4] = { f.zeroRow, f.zeroRow, f.zeroRow, f.fullRow };
	for (int c = 0; c < 4; c++)
	{
		T[0][c] = f.needPrimary ? k.primary[c] : neutral[c];
		T[1][c] = f.useLight ? k.litP[c] : T[0][c];
		T[2][c] = f.useLight ? k.litS[c] : neutral[c];
		for (int u = 0; u < 3; u++) T[3 + u][c] = f.useTex[u] ? k.tex[u][c] : neutral[c];
		T[6][c] = f.useProc ? k.proc[c] : neutral[c];
	}
	const bool useDepth = f.depthTest && t.depth;
	const u32 skipBits = p->m->gpuSkip;
	const bool trace = p->m->traceGpu;
	const Vertex &v0 = *s0.v, &v1 = *s1.v, &v2 = *s2.v;
	const float edgeX[3] = { e0x, e1x, e2x }, edgeY[3] = { e0y, e1y, e2y };
	const float edgeInv[3] = { e0y != 0 ? 1.0f / e0y : 0.0f, e1y != 0 ? 1.0f / e1y : 0.0f, e2y != 0 ? 1.0f / e2y : 0.0f };
	const Screen *const edgeAt[3] = { &s1, &s2, &s0 };			// (the vertex each edge's function is measured from)
	const float iw0 = s0.invw, iw1 = s1.invw, iw2 = s2.invw;

	// The fragments gathered are shaded, then written.
	auto shade = [&] ()
	{
		const int n = k.n;
		k.n = 0;
		if (!n) return;
		if (skipBits & 64) { nPixels += (u64) n; return; }
		// what the vertices carry, perspective-correct
		for (int i = 0; i < n; i++)
		{
			const float q0 = k.l0[i] * iw0, q1 = k.l1[i] * iw1, q2 = k.l2[i] * iw2;
			k.q0[i] = q0; k.q1[i] = q1; k.q2[i] = q2; k.qs[i] = 1.0f / (q0 + q1 + q2);
		}
		auto attr = [&] (int ai, float *__restrict d)
		{
			const float a0 = v0.a[ai], a1 = v1.a[ai], a2 = v2.a[ai];
			for (int i = 0; i < n; i++) d[i] = (k.q0[i] * a0 + k.q1[i] * a1 + k.q2[i] * a2) * k.qs[i];
		};
		if (f.needPrimary)
			for (int c = 0; c < 4; c++)
			{
				attr (A_COLOR + c, k.fa);
				for (int i = 0; i < n; i++) { const float v = k.fa[i]; k.primary[c][i] = v <= 0 ? 0 : v >= 1 ? 255 : (int) (v * 255.0f + 0.5f); }
			}
		for (int u = 0; u < 3; u++)
		{
			if (!f.useTex[u]) continue;
			const Texture &tx = f.tex[u];
			const int ai = u == 0 ? A_TC0 : u == 1 ? A_TC1 : A_TC2;
			attr (ai, k.fa); attr (ai + 1, k.fb);
			const float tw = (float) tx.w, th = (float) tx.h;
			for (int i = 0; i < n; i++)
			{
				int c[4];
				texel (tx, (int) floorf (k.fa[i] * tw), (int) floorf (k.fb[i] * th), c);
				k.tex[u][0][i] = c[0]; k.tex[u][1][i] = c[1]; k.tex[u][2][i] = c[2]; k.tex[u][3][i] = c[3];
			}
		}
		if (f.useLight)
		{
			for (int j = 0; j < 4; j++) attr (A_QUAT + j, k.fq[j]);
			for (int j = 0; j < 3; j++) attr (A_VIEW + j, k.fq[4 + j]);
			lightRows (p, f.light, n, k, k.litP, k.litS);
		}
		if (f.useProc)
		{
			const int ai = f.proc.coord == 0 ? A_TC0 : f.proc.coord == 1 ? A_TC1 : A_TC2;
			attr (ai, k.fa); attr (ai + 1, k.fb);
			for (int i = 0; i < n; i++)
			{
				int c[4] = { 0, 0, 0, 255 };
				procTexel (p, f.proc, k.fa[i], k.fb[i], c);
				k.proc[0][i] = c[0]; k.proc[1][i] = c[1]; k.proc[2][i] = c[2]; k.proc[3][i] = c[3];
			}
		}
		if (skipBits & 128) { nPixels += (u64) n; return; }
		// the combiner's stages: each leaves its result in rows of its own
		// (the combiner's buffer is two stages late: a stage that "updates" it is seen by the stage after the next;
		// the first stage sees nothing, the second the buffer's colour register)
		const int *prev[4] = { T[0][0], T[0][1], T[0][2], T[0][3] };
		const int *buffer[4] = { f.zeroRow, f.zeroRow, f.zeroRow, f.zeroRow }, *next[4] = { f.bufferRow[0], f.bufferRow[1], f.bufferRow[2], f.bufferRow[3] };
		for (int st = 0; st < f.stageEnd; st++)
		{
			const Fragment::Stage &sg = f.stage[st];
			if (!sg.pass)
			{
				for (int c = 0; c < 4; c++)
				{
					T[13][c] = buffer[c]; T[14][c] = f.konstRow[st][c];
					T[7][c] = T[8][c] = T[9][c] = T[10][c] = T[11][c] = T[12][c] = T[15][c] = prev[c];
				}
				const int *cp[3][3], *ap[3];					// the three operands: colour, alpha
				for (int j = 0; j < 3; j++)
				{
					const int *const *src = T[sg.src[j]];
					const u32 op = sg.op[j];
					if (op == 0 || op > 13 || ((op & 3) > 1 && op > 3)) { cp[j][0] = src[0]; cp[j][1] = src[1]; cp[j][2] = src[2]; }
					else if (op == 1) for (int c = 0; c < 3; c++) { rowInv (n, k.c[j][c], src[c]); cp[j][c] = k.c[j][c]; }
					else
					{
						const int *one = src[op < 4 ? 3 : op < 8 ? 0 : op < 12 ? 1 : 2];	// (one channel as the three)
						if (op & 1) { rowInv (n, k.c[j][0], one); one = k.c[j][0]; }
						cp[j][0] = cp[j][1] = cp[j][2] = one;
					}
					src = T[sg.srcA[j]];
					const u32 opA = sg.opA[j];
					const int *one = src[opA < 2 ? 3 : opA < 4 ? 0 : opA < 6 ? 1 : 2];
					if (opA & 1) { rowInv (n, k.a[j], one); one = k.a[j]; }
					ap[j] = one;
				}
				const int *out[4];
				switch (sg.mode)
				{
				case 0: out[0] = cp[0][0]; out[1] = cp[0][1]; out[2] = cp[0][2]; break;
				case 6: case 7:							// the dot product of two colours as vectors
				{
					const int *a0 = cp[0][0], *a1 = cp[0][1], *a2 = cp[0][2], *b0 = cp[1][0], *b1 = cp[1][1], *b2 = cp[1][2];
					int *__restrict d = k.o[0];
					for (int i = 0; i < n; i++)
						d[i] = clamp255 (((a0[i] * 2 - 255) * (b0[i] * 2 - 255) + (a1[i] * 2 - 255) * (b1[i] * 2 - 255) + (a2[i] * 2 - 255) * (b2[i] * 2 - 255)) / 255);
					out[0] = out[1] = out[2] = k.o[0];
					break;
				}
				default:
					if (!rowCombine (sg.mode, n, k.o[0], cp[0][0], cp[1][0], cp[2][0])) { out[0] = cp[0][0]; out[1] = cp[0][1]; out[2] = cp[0][2]; break; }
					rowCombine (sg.mode, n, k.o[1], cp[0][1], cp[1][1], cp[2][1]);
					rowCombine (sg.mode, n, k.o[2], cp[0][2], cp[1][2], cp[2][2]);
					out[0] = k.o[0]; out[1] = k.o[1]; out[2] = k.o[2];
					break;
				}
				if (sg.mode == 7) out[3] = out[0];
				else out[3] = rowCombine (sg.modeA, n, k.o[3], ap[0], ap[1], ap[2]) ? k.o[3] : ap[0];
				for (int c = 0; c < 4; c++)
				{
					const int scale = c < 3 ? sg.scale : sg.scaleA;
					const int *from = out[c];
					int *__restrict d = k.prev[st][c];
					for (int i = 0; i < n; i++) d[i] = clamp255 (from[i] * scale);
					prev[c] = d;
				}
			}
			if (!f.usesBuffer) continue;
			for (int c = 0; c < 4; c++) buffer[c] = next[c];
			if (st < 4)
			{
				if (f.updateRgb >> st & 1) { next[0] = prev[0]; next[1] = prev[1]; next[2] = prev[2]; }
				if (f.updateA >> st & 1) next[3] = prev[3];
			}
		}
		// each fragment into the buffers: the alpha test, the depth, then the colour -- put as it is, or blended with
		// what is there (read for the whole group, blended as rows)
		bool alive[CH];
		for (int i = 0; i < n; i++) alive[i] = !f.alphaTest || compare (f.alphaFunc, (u32) prev[3][i], f.alphaRef);
		for (int i = 0; i < n; i++) if (alive[i]) nPixels++; else nAlpha++;
		if (useDepth && t.depthWrite && f.depthMask)
			for (int i = 0; i < n; i++)
			{
				if (!alive[i]) continue;
				u8 *dq = t.depth + k.at[i] * t.depthBytes;
				const u32 depth = k.depth[i];
				dq[0] = (u8) depth; dq[1] = (u8) (depth >> 8);
				if (t.depthBytes > 2) dq[2] = (u8) (depth >> 16);
			}
		if (!t.colorWrite) return;
		const int *out[4] = { prev[0], prev[1], prev[2], prev[3] };
		if (!f.replace || trace)
		{
			for (int i = 0; i < n; i++)
			{
				int d[4];
				readColor (t, t.color + k.at[i] * t.colorBytes, d);
				k.dst[0][i] = d[0]; k.dst[1][i] = d[1]; k.dst[2][i] = d[2]; k.dst[3][i] = d[3];
			}
			auto factor = [&] (u32 which, int c, int *__restrict tmp) -> const int *
			{
				switch (which)
				{
				case 0: return f.zeroRow;
				case 1: return f.fullRow;
				case 2: return prev[c];
				case 3: rowInv (n, tmp, prev[c]); return tmp;
				case 4: return k.dst[c];
				case 5: rowInv (n, tmp, k.dst[c]); return tmp;
				case 6: return prev[3];
				case 7: rowInv (n, tmp, prev[3]); return tmp;
				case 8: return k.dst[3];
				case 9: rowInv (n, tmp, k.dst[3]); return tmp;
				case 10: for (int i = 0; i < n; i++) tmp[i] = f.blendColor[c]; return tmp;
				case 11: for (int i = 0; i < n; i++) tmp[i] = 255 - f.blendColor[c]; return tmp;
				case 12: for (int i = 0; i < n; i++) tmp[i] = f.blendColor[3]; return tmp;
				case 13: for (int i = 0; i < n; i++) tmp[i] = 255 - f.blendColor[3]; return tmp;
				default:							// the source's alpha, as far as the buffer's leaves room
				{
					if (c == 3) return f.fullRow;
					const int *sa = prev[3], *da = k.dst[3];
					for (int i = 0; i < n; i++) tmp[i] = sa[i] < 255 - da[i] ? sa[i] : 255 - da[i];
					return tmp;
				}
				}
			};
			for (int c = 0; c < 4; c++)
			{
				if (!f.rgbaMask[c]) { out[c] = k.dst[c]; continue; }
				if (f.blend)
				{
					const int *sf = factor (c < 3 ? f.srcRgb : f.srcA, c, k.bf[0]), *df = factor (c < 3 ? f.dstRgb : f.dstA, c, k.bf[1]);
					rowBlend (c < 3 ? f.eqRgb : f.eqA, n, k.bo[c], prev[c], sf, k.dst[c], df);
				}
				else rowLogic (f.logicOp, n, k.bo[c], prev[c], k.dst[c]);
				out[c] = k.bo[c];
			}
		}
		for (int i = 0; i < n; i++)
		{
			if (!alive[i]) continue;
			const int c[4] = { out[0][i], out[1][i], out[2][i], out[3][i] };
			if (trace && k.x[i] == t.w / 2 && k.y[i] == t.h / 2)
				fprintf (stderr, "   centre: lit %d %d %d %d + %d %d %d %d, primary %d %d %d %d, tex0 %d %d %d %d, combined %d %d %d %d, there %d %d %d %d -> %d %d %d %d%c", T[1][0][i], T[1][1][i], T[1][2][i], T[1][3][i], T[2][0][i], T[2][1][i], T[2][2][i], T[2][3][i],
					 T[0][0][i], T[0][1][i], T[0][2][i], T[0][3][i], T[3][0][i], T[3][1][i], T[3][2][i], T[3][3][i], prev[0][i], prev[1][i], prev[2][i], prev[3][i], k.dst[0][i], k.dst[1][i], k.dst[2][i], k.dst[3][i], c[0], c[1], c[2], c[3], 10);
			writeColor (t, t.color + k.at[i] * t.colorBytes, c);
		}
	};

	for (int y = y0; y < y1; y++)
	{
		const float py = (float) y + 0.5f;
		const u32 row = (u32) (t.h - 1 - y);					// (the buffers' rows go from the bottom)
		// where the row can be inside the three edges: a little more than that, the test below stays the judge
		float lo = (float) x0, hi = (float) x1;
		bool none = false;
		for (int e = 0; e < 3; e++)
		{
			const float c = edgeX[e] * (py - edgeAt[e]->y);
			if (edgeY[e] > 0) { const float b = edgeAt[e]->x + c * edgeInv[e]; if (b < hi) hi = b; }
			else if (edgeY[e] < 0) { const float b = edgeAt[e]->x + c * edgeInv[e]; if (b > lo) lo = b; }
			else if (c < 0) none = true;
		}
		if (none) continue;
		int xa = x0, xb = x1;
		if (lo > (float) x0) { const float v = lo - 2.0f; xa = v >= (float) x1 ? x1 : (int) v; if (xa < x0) xa = x0; }
		if (hi < (float) x1) { const float v = hi + 2.0f; xb = v <= (float) x0 ? x0 : (int) v; if (xb > x1) xb = x1; }
		for (int x = xa; x < xb; x++)
		{
			const float px = (float) x + 0.5f;
			const float w0 = e0x * (py - s1.y) - e0y * (px - s1.x);
			const float w1 = e1x * (py - s2.y) - e1y * (px - s2.x);
			const float w2 = e2x * (py - s0.y) - e2y * (px - s0.x);
			if (w0 < 0 || w1 < 0 || w2 < 0) continue;
			if ((w0 == 0 && !in0) || (w1 == 0 && !in1) || (w2 == 0 && !in2)) continue;
			const float l0 = w0 * inv, l1 = w1 * inv, l2 = w2 * inv;
			const u32 at = tiled ((u32) x, row, (u32) t.w);
			u32 depth = 0;
			if (useDepth)							// the depth, linear on the screen
			{
				float z = l0 * s0.z + l1 * s1.z + l2 * s2.z;
				if (z < 0) z = 0; else if (z > 1) z = 1;
				depth = (u32) (z * (float) depthMax);
				const u8 *dq = t.depth + at * t.depthBytes;
				const u32 old = t.depthBytes == 2 ? (u32) (dq[0] | dq[1] << 8) : (u32) (dq[0] | dq[1] << 8 | dq[2] << 16);
				if (!compare (f.depthFunc, depth, old)) { nDepth++; continue; }
			}
			const int i = k.n++;
			k.x[i] = x; k.y[i] = y; k.at[i] = at; k.depth[i] = depth; k.l0[i] = l0; k.l1[i] = l1; k.l2[i] = l2;
			if (k.n == CH) shade ();
		}
	}
	shade ();							// (the last ones: a group is not ended by a row)
	p->stat[worker].pixels += nPixels; p->stat[worker].depthFailed += nDepth; p->stat[worker].alphaFailed += nAlpha;
}

// What is queued: a state (the target, the fragment's rules, the culling) shared by the triangles of a draw, and
// each triangle on the screen with its three vertices.
struct State { Target t; Fragment f; u32 cull; bool ok; };
struct Job { Screen s[3]; Vertex v[3]; u32 state; int rowMin, rowMax; /* the buffer's rows it can touch */ };
enum { STATE_MAX = 256, JOB_MAX = 8192, BAND = 8 };

static u64 microsNow () { struct timeval tv; gettimeofday (&tv, 0); return (u64) tv.tv_sec * 1000000ull + (u64) tv.tv_usec; }

// A worker (this thread, or one of the host's): it takes the next band nobody has, and draws there the queued
// triangles that reach it, in their order -- until no band is left.
static void flushWorker (void *arg, int worker)
{
	Pica *p = (Pica *) arg;
	for (;;)
	{
		const u32 b = __atomic_fetch_add (&p->bandNext, 1u, __ATOMIC_SEQ_CST);
		if (b >= p->bandCount) return;
		const int rowLo = (int) b * BAND, rowHi = rowLo + BAND - 1;
		for (u32 i = 0; i < p->jobCount; i++)
		{
			const Job &j = p->jobs[i];
			if (j.rowMax < rowLo || j.rowMin > rowHi) continue;
			const State &st = p->states[j.state];
			rasterize (p, st.t, st.f, st.cull, j.s[0], j.s[1], j.s[2], worker, rowLo, rowHi);
		}
	}
}

// The queue's bands laid out for the workers.
static void flushStart (Pica *p)
{
	int rows = 0;
	for (u32 i = 0; i < p->jobCount; i++) if (p->jobs[i].rowMax >= rows) rows = p->jobs[i].rowMax + 1;
	p->bandCount = (u32) (rows + BAND - 1) / BAND;
	__atomic_store_n (&p->bandNext, 0u, __ATOMIC_SEQ_CST);
	p->busySince = microsNow ();
}
// The queue is drawn: the workers' counters, the queue emptied; the last state stays (the next triangles may use it).
static void flushDone (Pica *p, bool aside)
{
	Machine *m = p->m;
	for (int w = 0; w < 8; w++)
	{
		p->pixels += p->stat[w].pixels; p->depthFailed += p->stat[w].depthFailed; p->alphaFailed += p->stat[w].alphaFailed;
		m->gsp.pixelsDrawn += p->stat[w].pixels;
		p->stat[w].triangles = p->stat[w].pixels = p->stat[w].depthFailed = p->stat[w].alphaFailed = 0;
	}
	const u64 us = microsNow () - p->busySince;
	m->gsp.usRaster += us;
	if (aside) m->gsp.usRasterAside += us;
	p->jobCount = 0; p->busy = false;
	if (p->stateCount > 1) { p->states[0] = p->states[p->stateCount - 1]; p->stateCount = 1; }
}

// The queued triangles are drawn now (with the host's helpers when it has some).
static void flush (Pica *p)
{
	Machine *m = p->m;
	if (p->busy) picaSync (m);
	if (!p->jobCount) { if (p->stateCount > 1) { p->states[0] = p->states[p->stateCount - 1]; p->stateCount = 1; } return; }
	flushStart (p);
	if (m->parallelBegin && !m->traceGpu && m->parallelBegin (m->parallelUser, flushWorker, p)) m->parallelEnd (m->parallelUser, flushWorker, p);
	else flushWorker (p, 0);
	flushDone (p, false);
}
// ... or by the host's helpers while this thread goes on (a list's end), when there are some.
static void flushAside (Pica *p)
{
	Machine *m = p->m;
	if (!p->jobCount || !m->parallelBegin || m->traceGpu) { flush (p); return; }
	flushStart (p);
	if (!m->parallelBegin (m->parallelUser, flushWorker, p)) { flushWorker (p, 0); flushDone (p, false); return; }
	p->busy = true;
}

bool picaBusy (const Machine *m) { return m->pica && m->pica->busy; }
bool picaDone (Machine *m) { return !m->pica || !m->pica->busy || m->parallelDone (m->parallelUser); }
void picaSync (Machine *m)
{
	Pica *p = m->pica;
	if (!p || !p->busy) return;
	m->parallelEnd (m->parallelUser, flushWorker, p);
	flushDone (p, true);
}

// Do queued triangles still draw into this texture's memory? (They are drawn first then: a picture rendered, then used.)
static bool drawnInto (const Pica *p, const Texture &tx)
{
	if (!p->jobCount || !tx.data) return false;
	for (u32 i = 0; i < p->stateCount; i++)
	{
		const Target &q = p->states[i].t;
		if (!p->states[i].ok || !q.color) continue;
		const u8 *end = q.color + (size_t) q.w * (size_t) q.h * q.colorBytes;
		if (tx.data < end && q.color < tx.data + tx.bytes) return true;
	}
	return false;
}

// A triangle from the shader: clipped to what can be seen (in clip space: -w <= x, y <= w, -w <= z <= 0), each
// piece put on the screen.
static void triangle (Pica *p, const Vertex &a, const Vertex &b, const Vertex &c)
{
	p->m->gsp.trianglesIn++;
	if (p->m->gpuSkip & 512) return;
	if (!p->states)
	{
		p->states = (State *) malloc (sizeof (State) * STATE_MAX);
		p->jobs = (Job *) malloc (sizeof (Job) * JOB_MAX);
		p->scratch = (Scratch *) malloc (sizeof (Scratch) * 8);
		if (!p->states || !p->jobs || !p->scratch) { free (p->states); free (p->jobs); free (p->scratch); p->states = 0; p->jobs = 0; p->scratch = 0; return; }
		p->stateCount = 0; p->jobCount = 0;
	}
	if (!p->stateOk || !p->stateCount)
	{
		static State st;							// (built aside: decoding a texture may flush the queue)
		st.ok = target (p, st.t);
		fragmentState (p, st.f);
		for (int i = 0; i < 3; i++) if (st.f.useTex[i] && drawnInto (p, st.f.tex[i])) { flush (p); break; }
		for (int i = 0; i < 3; i++) if (st.f.useTex[i]) texDecoded (p, st.f.tex[i]);
		st.cull = p->regs[R_CULL] & 3;
		if (p->stateCount >= STATE_MAX) flush (p);
		p->states[p->stateCount++] = st;
		p->stateOk = true;
	}
	const u32 stateIndex = p->stateCount - 1;
	if (!p->states[stateIndex].ok) return;
	Vertex poly[2][12]; int n = 3, cur = 0;
	poly[0][0] = a; poly[0][1] = b; poly[0][2] = c;
	// (a normal's quaternion and its opposite are the same turn: all three on one side, to interpolate them)
	for (int i = 1; i < 3; i++)
	{
		float *q = poly[0][i].a + A_QUAT; const float *q0 = poly[0][0].a + A_QUAT;
		if (q[0] * q0[0] + q[1] * q0[1] + q[2] * q0[2] + q[3] * q0[3] < 0) for (int k = 0; k < 4; k++) q[k] = -q[k];
	}
	// (most triangles are wholly inside: nothing to cut)
	bool inside = true;
	for (int i = 0; i < 3 && inside; i++)
	{
		const float *q = poly[0][i].a;
		inside = q[3] - 1e-6f >= 0 && q[3] + q[0] >= 0 && q[3] - q[0] >= 0 && q[3] + q[1] >= 0 && q[3] - q[1] >= 0 && q[3] + q[2] >= 0 && -q[2] >= 0;
	}
	for (int plane = 0; plane < 7 && n >= 3 && !inside; plane++)
	{
		const Vertex *in = poly[cur]; Vertex *out = poly[cur ^ 1]; int m = 0;
		float d[12];
		for (int i = 0; i < n; i++)
		{
			const float *q = in[i].a;
			switch (plane)
			{
			case 0: d[i] = q[3] - 1e-6f; break;				// in front of the eye
			case 1: d[i] = q[3] + q[0]; break; case 2: d[i] = q[3] - q[0]; break;
			case 3: d[i] = q[3] + q[1]; break; case 4: d[i] = q[3] - q[1]; break;
			case 5: d[i] = q[3] + q[2]; break; default: d[i] = -q[2]; break;
			}
		}
		for (int i = 0; i < n && m < 11; i++)
		{
			const int j = (i + 1) % n;
			if (d[i] >= 0) out[m++] = in[i];
			if ((d[i] >= 0) != (d[j] >= 0))
			{
				const float k = d[i] / (d[i] - d[j]);
				for (int e = 0; e < A_COUNT; e++) out[m].a[e] = in[i].a[e] + (in[j].a[e] - in[i].a[e]) * k;
				m++;
			}
		}
		n = m; cur ^= 1;
	}
	if (n < 3) return;
	const float vw = f24 (p->regs[R_VIEWPORT_W]), vh = f24 (p->regs[R_VIEWPORT_H]);
	const float vx = (float) (int) (p->regs[R_VIEWPORT_XY] & 0x3FF), vy = (float) (int) (p->regs[R_VIEWPORT_XY] >> 16 & 0x3FF);
	const float ds = f24 (p->regs[R_DEPTH_SCALE]), dof = f24 (p->regs[R_DEPTH_OFFSET]);
	Screen s[12];
	for (int i = 0; i < n; i++)
	{
		const float *q = poly[cur][i].a;
		const float iw = 1.0f / q[3];
		s[i].x = (q[0] * iw + 1.0f) * vw + vx;
		s[i].y = (q[1] * iw + 1.0f) * vh + vy;
		s[i].z = q[2] * iw * ds + dof;
		s[i].invw = iw; s[i].v = &poly[cur][i];
	}
	for (int i = 1; i + 1 < n; i++)
	{
		// (what is culled or outside is not queued)
		float area; int box[4];
		{
			const State &st = p->states[p->stateCount - 1];
			if (!triBox (st.t, st.cull, s[0], s[i], s[i + 1], area, box)) continue;
		}
		p->triangles++; p->m->gsp.trianglesDrawn++;
		if (p->jobCount >= JOB_MAX) flush (p);
		Job &j = p->jobs[p->jobCount++];
		const Screen *tri[3] = { &s[0], &s[i], &s[i + 1] };
		for (int k = 0; k < 3; k++) { j.v[k] = *tri[k]->v; j.s[k] = *tri[k]; j.s[k].v = &j.v[k]; }
		j.state = p->stateCount - 1;					// (flush may have moved the state to index 0)
		const int h = p->states[j.state].t.h;
		j.rowMin = h - box[3]; j.rowMax = h - 1 - box[1];
	}
}

// A vertex out of the shader joins the primitive being assembled.
static void assemble (Pica *p, const Vertex &v)
{
	const u32 mode = p->regs[R_PRIM_CONFIG] >> 8 & 3;
	p->prim[p->primCount++] = v;
	if (p->primCount < 3) return;
	if (mode == 1)								// a strip: each new vertex, a triangle (every other one turned)
	{
		if (p->stripFlip) triangle (p, p->prim[1], p->prim[0], p->prim[2]); else triangle (p, p->prim[0], p->prim[1], p->prim[2]);
		p->stripFlip = !p->stripFlip;
		p->prim[0] = p->prim[1]; p->prim[1] = p->prim[2]; p->primCount = 2;
	}
	else if (mode == 2)							// a fan around the first vertex
	{
		triangle (p, p->prim[0], p->prim[1], p->prim[2]);
		p->prim[1] = p->prim[2]; p->primCount = 2;
	}
	else { triangle (p, p->prim[0], p->prim[1], p->prim[2]); p->primCount = 0; }
}

// A vertex an indexed draw names several times (a mesh's triangles share theirs) is shaded once.
struct Shaded { u32 index, stamp, entry; Vertex v; };
enum { SHADED_MAX = 1024 };

// A draw's vertices shaded together: each one's attributes are read first (once a vertex: `order` says which entry
// each of the draw's vertices is), the shader then runs on all of them -- the host's other cores taking their
// share, 32 at a time --, and the triangles are assembled in the draw's order.
enum { BATCH_MAX = 2048, BATCH_SPAN = 8192, BATCH_PIECE = 32 };
struct Batch
{
	float attr[BATCH_MAX][16][4]; Vertex out[BATCH_MAX]; u16 order[BATCH_SPAN];
	u32 count, next; int inputs;
	struct { u64 steps; u32 badOp; char pad[52]; } by[8];		// (a worker's own)
};
static void shadeWorker (void *arg, int worker)
{
	Pica *p = (Pica *) arg;
	Batch &b = *p->batch;
	u64 steps = 0; u32 badOp = 0;
	for (;;)
	{
		const u32 k0 = __atomic_fetch_add (&b.next, (u32) BATCH_PIECE, __ATOMIC_SEQ_CST);
		if (k0 >= b.count) break;
		const u32 k1 = k0 + BATCH_PIECE < b.count ? k0 + BATCH_PIECE : b.count;
		for (u32 k = k0; k < k1; k++) shadeVertex (p, b.attr[k], b.inputs, b.out[k], steps, badOp);
	}
	b.by[worker].steps += steps;
	if (badOp) b.by[worker].badOp = badOp;
}

// ---- draws ---------------------------------------------------------------------------------------------------------------
static void draw (Pica *p, bool indexed)
{
	const u32 base = p->regs[R_ATTR_BASE] << 3;
	const u64 formats = (u64) p->regs[R_ATTR_FORMAT_HI] << 32 | p->regs[R_ATTR_FORMAT_LO];
	const u32 fixedMask = p->regs[R_ATTR_FORMAT_HI] >> 16 & 0xFFF;
	const int inputs = (int) (p->regs[R_VSH_INPUT] & 15) + 1;
	const u32 count = p->regs[R_NUM_VERTICES];
	if (count > 0x100000) return;
	// (the right eye's picture, on a host that shows the left one only: n3ds_gsp.cpp)
	if (p->m->gsp.eyeSkip && (p->regs[R_COLOR_ADDR] << 3) == Machine::virtToPhys (p->m->gsp.eyeSrc)) { p->m->gsp.eyeDraws++; return; }
	if ((p->regs[R_GEO_CONFIG] & 3) == 2) p->m->note ("a geometry shader");
	const u8 *index = 0; bool index16 = false;
	if (indexed)
	{
		index16 = (p->regs[R_INDEX_CONFIG] >> 31) != 0;
		index = p->m->physPtr (base + (p->regs[R_INDEX_CONFIG] & 0x0FFFFFFF), count * (index16 ? 2 : 1));
		if (!index) return;
	}
	if (p->m->traceGpu)
	{
		const u32 *r = p->regs;
		fprintf (stderr, "draw %s %u vertices, mode %u, entry %03x, inputs %d | target %08x %ux%u fmt %u, viewport %g x %g at %u,%u, scissor %u | tex cfg %05x: 0 %ux%u fmt %x at %08x | tev0 %08x %08x %08x k %08x, tev1 %08x %08x %08x, tev2 %08x %08x %08x k %08x, tev3 %08x %08x %08x, tev4 %08x %08x %08x, tev5 %08x %08x %08x, e0 %08x | blend %08x op %08x, alpha %08x, depth %08x, stencil %08x, cull %u, light %u%c",
			 indexed ? "indexed" : "arrays", (unsigned) count, (unsigned) (r[R_PRIM_CONFIG] >> 8 & 3), (unsigned) (r[R_VSH_ENTRY] & 0xFFFF), inputs,
			 (unsigned) (r[R_COLOR_ADDR] << 3), (unsigned) (r[R_FB_DIM] & 0x7FF), (unsigned) ((r[R_FB_DIM] >> 12 & 0x3FF) + 1), (unsigned) (r[R_COLOR_FORMAT] >> 16 & 7),
			 (double) (f24 (r[R_VIEWPORT_W]) * 2), (double) (f24 (r[R_VIEWPORT_H]) * 2), (unsigned) (r[R_VIEWPORT_XY] & 0x3FF), (unsigned) (r[R_VIEWPORT_XY] >> 16 & 0x3FF), (unsigned) (r[R_SCISSOR_MODE] & 3),
			 (unsigned) r[R_TEX_CONFIG], (unsigned) (r[R_TEX0 + 1] >> 16), (unsigned) (r[R_TEX0 + 1] & 0xFFFF), (unsigned) (r[R_TEX0_TYPE] & 15), (unsigned) (r[R_TEX0 + 4] << 3),
			 (unsigned) r[0xC0], (unsigned) r[0xC1], (unsigned) r[0xC2], (unsigned) r[0xC3], (unsigned) r[0xC8], (unsigned) r[0xC9], (unsigned) r[0xCA],
			 (unsigned) r[0xD0], (unsigned) r[0xD1], (unsigned) r[0xD2], (unsigned) r[0xD3], (unsigned) r[0xD8], (unsigned) r[0xD9], (unsigned) r[0xDA],
			 (unsigned) r[0xF0], (unsigned) r[0xF1], (unsigned) r[0xF2], (unsigned) r[0xF8], (unsigned) r[0xF9], (unsigned) r[0xFA], (unsigned) r[0xE0],
			 (unsigned) r[R_BLEND_FUNC], (unsigned) r[R_COLOR_OP], (unsigned) r[R_ALPHA_TEST], (unsigned) r[R_DEPTH_COLOR_MASK], (unsigned) r[R_STENCIL_TEST], (unsigned) (r[R_CULL] & 3), (unsigned) (r[R_LIGHTING] & 1), 10);
	}
	if (p->m->traceGpu && p->dumped < 6)				// (a shader's code, uniforms and state, once: to study it)
	{
		p->dumped++;
		fprintf (stderr, "shader entry %03x bool %04x int %02x%02x%02x %02x%02x%02x outmask %04x outmap %u: %08x %08x %08x %08x %08x %08x %08x%c", (unsigned) (p->regs[R_VSH_ENTRY] & 0xFFFF), (unsigned) p->bu,
			 p->iu[0][0], p->iu[0][1], p->iu[0][2], p->iu[1][0], p->iu[1][1], p->iu[1][2], (unsigned) p->regs[R_VSH_OUTMASK], (unsigned) p->regs[R_OUTMAP_TOTAL],
			 (unsigned) p->regs[R_OUTMAP], (unsigned) p->regs[R_OUTMAP + 1], (unsigned) p->regs[R_OUTMAP + 2], (unsigned) p->regs[R_OUTMAP + 3], (unsigned) p->regs[R_OUTMAP + 4], (unsigned) p->regs[R_OUTMAP + 5], (unsigned) p->regs[R_OUTMAP + 6], 10);
		fprintf (stderr, "code");
		for (u32 i = 0; i < p->codeTop && i < CODE_MAX; i++) fprintf (stderr, " %08x", (unsigned) p->code[i]);
		fprintf (stderr, "%cdesc", 10);
		for (u32 i = 0; i < OPDESC_MAX; i++) fprintf (stderr, " %08x", (unsigned) p->opdesc[i]);
		fprintf (stderr, "%cuniforms", 10);
		for (int i = 0; i < 96; i++) fprintf (stderr, " %d:[%g %g %g %g]", i, (double) p->fu[i][0], (double) p->fu[i][1], (double) p->fu[i][2], (double) p->fu[i][3]);
		fprintf (stderr, "%c", 10);
	}
	if (p->m->traceGpu && (p->regs[R_LIGHTING] & 1))
	{
		Lighting l; lightingState (p, l);
		fprintf (stderr, "   lights %d, ambient %g %g %g, config %08x %08x, luts abs %08x select %08x scale %08x: d0 %d d1 %d fr %d rr %d rg %d rb %d%c", l.count, (double) l.ambient[0], (double) l.ambient[1], (double) l.ambient[2],
			 (unsigned) p->regs[R_LIGHT_CONFIG0], (unsigned) p->regs[R_LIGHT_CONFIG1], (unsigned) p->regs[R_LIGHT_LUT_ABS], (unsigned) p->regs[R_LIGHT_LUT_SELECT], (unsigned) p->regs[R_LIGHT_LUT_SCALE], l.d0.on, l.d1.on, l.fr.on, l.rr.on, l.rg.on, l.rb.on, 10);
		for (int i = 0; i < l.count; i++)
			fprintf (stderr, "     light %d: at %g %g %g%s, diffuse %g %g %g, ambient %g %g %g, specular %g %g %g / %g %g %g, distance %d (bias %g scale %g)%c", l.light[i].id,
				 (double) l.light[i].pos[0], (double) l.light[i].pos[1], (double) l.light[i].pos[2], l.light[i].directional ? " (a direction)" : "",
				 (double) l.light[i].diff[0], (double) l.light[i].diff[1], (double) l.light[i].diff[2], (double) l.light[i].amb[0], (double) l.light[i].amb[1], (double) l.light[i].amb[2],
				 (double) l.light[i].spec0[0], (double) l.light[i].spec0[1], (double) l.light[i].spec0[2], (double) l.light[i].spec1[0], (double) l.light[i].spec1[1], (double) l.light[i].spec1[2],
				 l.light[i].distance, (double) l.light[i].bias, (double) l.light[i].scale, 10);
	}
	const u64 pixelsBefore = p->pixels, trisBefore = p->triangles, depthBefore = p->depthFailed, alphaBefore = p->alphaFailed;
	p->primCount = 0; p->stripFlip = false;
	if (indexed && !p->shaded) p->shaded = (Shaded *) calloc (SHADED_MAX, sizeof (Shaded));
	Shaded *const shaded = indexed && !p->m->traceGpu ? p->shaded : 0;
	if (++p->drawStamp == 0) { p->drawStamp = 1; if (p->shaded) memset (p->shaded, 0, sizeof (Shaded) * SHADED_MAX); }
	// one vertex's attributes, as the loaders and the formats say
	auto readVertex = [&] (u32 vi, float attr[16][4])
	{
		for (int i = 0; i < 16; i++) { attr[i][0] = attr[i][1] = attr[i][2] = 0; attr[i][3] = 1.0f; }
		for (int l = 0; l < 12; l++)
		{
			const u32 *r = p->regs + R_ATTR_LOADER + l * 3;		// offset, the components' attributes, their count and the stride
			const u32 comps = r[2] >> 28, stride = r[2] >> 16 & 0xFF;
			if (!comps) continue;
			const u64 cfg = (u64) (r[2] & 0xFFFF) << 32 | r[1];
			const u8 *src = p->m->physPtr (base + (r[0] & 0x0FFFFFFF) + stride * vi, stride ? stride : 64);
			if (!src) continue;
			u32 pos = 0;
			for (u32 c = 0; c < comps && c < 12; c++)
			{
				const u32 id = (u32) (cfg >> (4 * c) & 15);
				if (id >= 12) { pos = (pos + 3) & ~3u; pos += (id - 11) * 4; continue; }	// padding
				const u32 type = (u32) (formats >> (4 * id) & 3), elems = (u32) (formats >> (4 * id + 2) & 3) + 1;
				const u32 size = type == 3 ? 4 : type == 2 ? 2 : 1;
				pos = (pos + size - 1) & ~(size - 1);
				for (u32 e = 0; e < elems; e++, pos += size)
				{
					const u8 *q = src + pos;
					switch (type)
					{
					case 0: attr[id][e] = (float) (s8) q[0]; break;
					case 1: attr[id][e] = (float) q[0]; break;
					case 2: attr[id][e] = (float) (s16) (q[0] | q[1] << 8); break;
					default: { u32 w = (u32) q[0] | (u32) q[1] << 8 | (u32) q[2] << 16 | (u32) q[3] << 24; attr[id][e] = bitsFloat (w); break; }
					}
				}
			}
		}
		for (int i = 0; i < 12; i++) if (fixedMask >> i & 1) memcpy (attr[i], p->fixed[i], sizeof attr[i]);
	};
	auto vertexIndex = [&] (u32 n) -> u32 { return indexed ? (index16 ? (u32) (index[n * 2] | index[n * 2 + 1] << 8) : index[n]) : n + p->regs[R_VERTEX_OFFSET]; };
	Machine *const m = p->m;
	if (count >= 96 && m->parallelBegin && !m->traceGpu && (!indexed || p->shaded) && decReady (p, true)
	    && (p->batch || (p->batch = (Batch *) malloc (sizeof (Batch))) != 0))
	{
		// (many vertices and other cores: shaded together)
		Batch &b = *p->batch;
		for (u32 n = 0; n < count; )
		{
			b.count = 0; b.inputs = inputs;
			__atomic_store_n (&b.next, 0u, __ATOMIC_SEQ_CST);
			for (int w = 0; w < 8; w++) { b.by[w].steps = 0; b.by[w].badOp = 0; }
			if (++p->drawStamp == 0) { p->drawStamp = 1; if (p->shaded) memset (p->shaded, 0, sizeof (Shaded) * SHADED_MAX); }
			const u32 n0 = n;
			for (; n < count && n - n0 < BATCH_SPAN && b.count < BATCH_MAX; n++)
			{
				const u32 vi = vertexIndex (n);
				Shaded *const slot = shaded ? &shaded[vi & (SHADED_MAX - 1)] : 0;
				if (slot && slot->stamp == p->drawStamp && slot->index == vi) { b.order[n - n0] = (u16) slot->entry; continue; }
				const u32 k = b.count++;
				readVertex (vi, b.attr[k]);
				b.order[n - n0] = (u16) k;
				if (slot) { slot->index = vi; slot->stamp = p->drawStamp; slot->entry = k; }
			}
			m->gsp.vertices += b.count;
			if (b.count >= 2 * BATCH_PIECE && m->parallelBegin (m->parallelUser, shadeWorker, p)) m->parallelEnd (m->parallelUser, shadeWorker, p);
			else shadeWorker (p, 0);
			for (int w = 0; w < 8; w++) { m->gsp.shaderSteps += b.by[w].steps; if (b.by[w].badOp) p->badOp = b.by[w].badOp; }
			for (u32 i = n0; i < n; i++) assemble (p, b.out[b.order[i - n0]]);
		}
		if (++p->drawStamp == 0) p->drawStamp = 1;			// (the entries kept by index are this draw's batches': not vertices)
	}
	else for (u32 n = 0; n < count; n++)
	{
		const u32 vi = vertexIndex (n);
		Shaded *const slot = shaded ? &shaded[vi & (SHADED_MAX - 1)] : 0;
		if (slot && slot->stamp == p->drawStamp && slot->index == vi) { assemble (p, slot->v); continue; }
		float attr[16][4];
		readVertex (vi, attr);
		Vertex v;
		p->m->gsp.vertices++;
		shadeVertex (p, attr, inputs, v, m->gsp.shaderSteps, p->badOp);
		if (slot) { slot->index = vi; slot->stamp = p->drawStamp; slot->v = v; }
		if (p->m->traceGpu && n == 0)
		{
			fprintf (stderr, "   in:");
			for (int i = 0; i < inputs && i < 8; i++) fprintf (stderr, " [%g %g %g %g]", (double) attr[i][0], (double) attr[i][1], (double) attr[i][2], (double) attr[i][3]);
			fprintf (stderr, "  formats %08x %08x perm %08x fixed %03x%c", (unsigned) p->regs[R_ATTR_FORMAT_LO], (unsigned) p->regs[R_ATTR_FORMAT_HI], (unsigned) p->regs[R_VSH_PERM_LO], (unsigned) fixedMask, 10);
		}
		if (p->m->traceGpu && n < 3) fprintf (stderr, "   v%u clip %g %g %g %g  color %g %g %g %g  tc %g %g%c", (unsigned) n, (double) v.a[0], (double) v.a[1], (double) v.a[2], (double) v.a[3], (double) v.a[8], (double) v.a[9], (double) v.a[10], (double) v.a[11], (double) v.a[12], (double) v.a[13], 10);
		assemble (p, v);
	}
	if (p->badOp) { m->note ("vertex shader instruction %02x", (unsigned) (p->badOp & 0xFF)); p->badOp = 0; }
	if (p->m->traceGpu) flush (p);						// (a trace: each draw rasterized at once, to count it)
	if (p->m->traceGpu) fprintf (stderr, "   -> %llu pixels (%llu triangles on the screen; %llu pixels behind, %llu refused by the alpha test)%c", (unsigned long long) (p->pixels - pixelsBefore),
				     (unsigned long long) (p->triangles - trisBefore), (unsigned long long) (p->depthFailed - depthBefore), (unsigned long long) (p->alphaFailed - alphaBefore), 10);
}

// ---- the registers ---------------------------------------------------------------------------------------------------
static void writeReg (Pica *p, u32 id, u32 value, u32 mask)
{
	if (id >= REG_COUNT) return;
	static const u32 BYTES[16] = { 0, 0xFF, 0xFF00, 0xFFFF, 0xFF0000, 0xFF00FF, 0xFFFF00, 0xFFFFFF, 0xFF000000, 0xFF0000FF, 0xFF00FF00, 0xFF00FFFF, 0xFFFF0000, 0xFFFF00FF, 0xFFFFFF00, 0xFFFFFFFF };
	const u32 bits = BYTES[mask & 15];
	p->regs[id] = (p->regs[id] & ~bits) | (value & bits);
	if (id < R_ATTR_BASE) p->stateOk = false;				// (the fragment's registers: before the vertex ones)
	const u32 v = p->regs[id];
	switch (id)
	{
	case R_DRAW_ARRAYS: draw (p, false); break;
	case R_DRAW_ELEMENTS: draw (p, true); break;
	case R_PRIM_RESTART: p->primCount = 0; p->stripFlip = false; break;
	case R_PRIM_CONFIG: p->primCount = 0; p->stripFlip = false; break;
	case R_LIGHT_LUT_INDEX: p->lightLutIndex = v & 0xFF; p->lightLutType = v >> 8 & 31; break;
	case R_CMD_JUMP0: case R_CMD_JUMP1: p->jump = (int) (id - R_CMD_JUMP0) + 1; break;	// (taken by the list's loop)
	case R_FIXED_INDEX: p->fixedIndex = v & 15; p->fixedCount = 0; if (p->fixedIndex == 15) p->immediateCount = 0; break;
	case R_FIXED_DATA: case R_FIXED_DATA + 1: case R_FIXED_DATA + 2:
		p->fixedWords[p->fixedCount++] = value;
		if (p->fixedCount < 3) break;
		p->fixedCount = 0;
		if (p->fixedIndex < 12) { unpack24 (p->fixedWords, p->fixed[p->fixedIndex]); break; }
		if (p->fixedIndex != 15) break;
		// a vertex given attribute by attribute: when the shader's inputs are all there, it is drawn
		unpack24 (p->fixedWords, p->immediate[p->immediateCount++]);
		if (p->immediateCount >= (int) (p->regs[R_VSH_INPUT] & 15) + 1)
		{
			Vertex vtx;
			shadeVertex (p, p->immediate, p->immediateCount, vtx, p->m->gsp.shaderSteps, p->badOp);
			p->immediateCount = 0;
			assemble (p, vtx);
		}
		break;
	case R_PROCTEX_LUT: p->procIndex = v & 0xFF; p->procTable = v >> 8 & 15; break;
	case R_VSH_BOOL: p->bu = v & 0xFFFF; break;
	case R_VSH_INT: case R_VSH_INT + 1: case R_VSH_INT + 2: case R_VSH_INT + 3:
	{
		u8 *u = p->iu[id - R_VSH_INT];
		u[0] = (u8) v; u[1] = (u8) (v >> 8); u[2] = (u8) (v >> 16); u[3] = (u8) (v >> 24);
		break;
	}
	case R_VSH_FLOAT_INDEX: p->floatIndex = v & 0xFF; p->float32 = (v >> 31) != 0; p->floatCount = 0; break;
	case R_VSH_CODE_INDEX: p->codeIndex = v & 0xFFF; break;
	case R_VSH_OPDESC_INDEX: p->opdescIndex = v & 0x7F; break;
	default:
		if (id >= R_LIGHT_LUT_DATA && id < R_LIGHT_LUT_DATA + 8)		// a lighting table's next entry: 12 bits, and a signed 11-bit slope
		{
			const u32 i = p->lightLutIndex++;
			if (p->lightLutType < 24 && i < 256)
			{
				p->lightLut[p->lightLutType][i] = (float) (value & 0xFFF) / 4095.0f;
				const float d = (float) (value >> 12 & 0x7FF) / 2047.0f;
				p->lightLutDiff[p->lightLutType][i] = (value >> 23 & 1) ? -d : d;
			}
			break;
		}
		if (id >= R_PROCTEX_LUT_DATA && id < R_PROCTEX_LUT_DATA + 8)	// a table's next entry: 2 the colour map, 3 the alpha map, 4 the colours
		{
			const u32 i = p->procIndex++;
			if ((p->procTable == 2 || p->procTable == 3) && i < 128)
			{
				const int t = (int) p->procTable - 2;
				p->procMap[t][i] = (float) (value & 0xFFF) / 4095.0f;
				p->procMapDiff[t][i] = (float) ((s32) (value << 8) >> 20) / 4095.0f;
			}
			else if (p->procTable == 4 && i < 256) p->procColor[i] = value;
			break;
		}
		if (id >= R_VSH_FLOAT_DATA && id < R_VSH_FLOAT_DATA + 8)	// a uniform: 4 words (32-bit floats) or 3 (24-bit), w first
		{
			p->floatWords[p->floatCount++] = value;
			if (p->floatCount < (p->float32 ? 4 : 3)) break;
			p->floatCount = 0;
			if (p->floatIndex < 96)
			{
				float *u = p->fu[p->floatIndex];
				if (p->float32) { u[3] = bitsFloat (p->floatWords[0]); u[2] = bitsFloat (p->floatWords[1]); u[1] = bitsFloat (p->floatWords[2]); u[0] = bitsFloat (p->floatWords[3]); }
				else unpack24 (p->floatWords, u);
			}
			p->floatIndex++;
		}
		else if (id >= R_VSH_CODE_DATA && id < R_VSH_CODE_DATA + 8) { if (p->codeIndex < CODE_MAX) { p->code[p->codeIndex++] = value; p->decDirty = true; if (p->codeIndex > p->codeTop) p->codeTop = p->codeIndex; } }
		else if (id >= R_VSH_OPDESC_DATA && id < R_VSH_OPDESC_DATA + 8) { if (p->opdescIndex < OPDESC_MAX) { p->opdesc[p->opdescIndex++] = value; p->decDirty = true; } }
		break;
	}
}

static Pica *pica (Machine *m)
{
	if (!m->pica)
	{
		m->pica = (Pica *) calloc (1, sizeof (Pica));
		if (m->pica) m->pica->m = m;
	}
	return m->pica;
}

void picaFree (Machine *m)
{
	picaSync (m);
	if (m->pica) { free (m->pica->states); free (m->pica->jobs); free (m->pica->scratch); free (m->pica->shaded); free (m->pica->batch); free (m->pica->dec); free (m->pica->decValid); for (auto &d : m->pica->decoded) free (d.px); }
	free (m->pica); m->pica = 0;
}

// A command list: a value, then a header -- the register, a mask of the value's bytes, how many more values
// follow, and whether they go to the following registers or all to this one. Each command fills 8 bytes' multiples.
// A list may go on in another buffer (two registers pairs give an address and a size; writing the jump register
// goes there): games build their frame from such pieces.
void picaCommandList (Machine *m, u32 va, u32 size)
{
	Pica *p = pica (m);
	if (!p || (va & 3) || size > 0x400000) return;
	picaSync (m);
	const u32 *list = (const u32 *) m->physPtr (Machine::virtToPhys (va), size);
	if (!list) { m->note ("a command list outside the linear memory"); return; }
	u32 at = 0, words = size / 4;
	p->jump = 0;
	for (int jumps = 0; at + 2 <= words; )
	{
		const u32 value = list[at], header = list[at + 1];
		at += 2;
		const u32 id = header & 0xFFFF, mask = header >> 16 & 15, extra = header >> 20 & 0xFF;
		const bool consecutive = (header >> 31) != 0;
		if (m->traceGpu && id >= 0x2B0 && id <= 0x2C8)			// (uniforms: how the program sends them)
		{
			fprintf (stderr, "reg %03x mask %x %s +%u: %08x", (unsigned) id, (unsigned) mask, consecutive ? "run" : "same", (unsigned) extra, (unsigned) value);
			for (u32 i = 0; i < extra && i < 20 && at + i < words; i++) fprintf (stderr, " %08x", (unsigned) list[at + i]);
			fprintf (stderr, "%c", 10);
		}
		writeReg (p, id, value, mask);
		for (u32 i = 0; i < extra && at < words; i++, at++) writeReg (p, consecutive ? id + i + 1 : id, list[at], mask);
		if (extra & 1) at++;						// (padded to 8 bytes)
		if (p->jump)
		{
			const int k = p->jump - 1;
			p->jump = 0;
			const u32 bytes = p->regs[R_CMD_SIZE0 + k] << 3;
			const u32 *next = bytes <= 0x400000 ? (const u32 *) m->physPtr (p->regs[R_CMD_ADDR0 + k] << 3, bytes) : 0;
			if (!next || ++jumps > 4096) { m->note ("a command list's jump to nowhere"); break; }
			list = next; words = bytes / 4; at = 0;
		}
	}
	flushAside (p);
}

// ---- transfers -------------------------------------------------------------------------------------------------------
// A display transfer: what was rendered (a tiled buffer) into a framebuffer (linear), or the other way; the
// formats may differ, the picture may be halved, turned upside down.
void picaDisplayTransfer (Machine *m, const u32 *c)
{
	picaSync (m);
	const u32 inW = c[3] & 0xFFFF, inH = c[3] >> 16, outW = c[4] & 0xFFFF, outH = c[4] >> 16, flags = c[5];
	const bool flip = (flags & 1) != 0, inLinear = (flags & 2) != 0, raw = (flags & 8) != 0, sameTiling = (flags & 0x20) != 0;
	const u32 inFmt = flags >> 8 & 7, outFmt = flags >> 12 & 7, scale = flags >> 24 & 3;
	if (m->traceGpu) fprintf (stderr, "transfer %08x %ux%u -> %08x %ux%u flags %08x%c", (unsigned) c[1], (unsigned) inW, (unsigned) inH, (unsigned) c[2], (unsigned) outW, (unsigned) outH, (unsigned) flags, 10);
	static const u32 BYTES[8] = { 4, 3, 2, 2, 2, 4, 4, 4 };
	const u32 ib = BYTES[inFmt], ob = BYTES[outFmt];
	if (raw || !inW || !outW || inW > 2048 || outW > 2048 || inH > 2048 || outH > 2048) { if (raw) m->note ("a raw display transfer"); return; }
	const u32 sx = scale ? 1 : 0, sy = scale == 2 ? 1 : 0;
	Target ti, to;
	memset (&ti, 0, sizeof ti); memset (&to, 0, sizeof to);
	ti.colorFormat = inFmt == 2 ? 3 : inFmt == 3 ? 2 : inFmt;		// (the transfer's numbers swap RGB565 and RGB5A1)
	to.colorFormat = outFmt == 2 ? 3 : outFmt == 3 ? 2 : outFmt;
	// (both buffers are linear memory or VRAM, one piece each in the host: read and written there)
	const u8 *hin = m->physPtr (Machine::virtToPhys (c[1]), inW * inH * ib);
	u8 *hout = m->physPtr (Machine::virtToPhys (c[2]), outW * outH * ob);
	if (hin && hout)
	{
		for (u32 y = 0; y < outH; y++)
		{
			const u32 iy = y << sy, oy = flip ? outH - 1 - y : y;
			if (iy >= inH) continue;
			for (u32 x = 0; x < outW; x++)
			{
				const u32 ix = x << sx;
				if (ix >= inW) continue;
				u32 src, dst;
				if (sameTiling) { src = (ix + iy * inW) * ib; dst = (x + oy * outW) * ob; }
				else if (inLinear) { src = (ix + iy * inW) * ib; dst = tiled (x, oy, outW) * ob; }
				else { src = tiled (ix, iy, inW) * ib; dst = (x + oy * outW) * ob; }
				if (ti.colorFormat == 0 && to.colorFormat == 1) { hout[dst] = hin[src + 1]; hout[dst + 1] = hin[src + 2]; hout[dst + 2] = hin[src + 3]; continue; }	// RGBA8 -> RGB8
				if (ti.colorFormat == to.colorFormat) { for (u32 k = 0; k < ib; k++) hout[dst + k] = hin[src + k]; continue; }
				int col[4];
				readColor (ti, hin + src, col);
				writeColor (to, hout + dst, col);
			}
		}
		return;
	}
	for (u32 y = 0; y < outH; y++)
		for (u32 x = 0; x < outW; x++)
		{
			const u32 ix = x << sx, iy = y << sy;
			if (ix >= inW || iy >= inH) continue;
			const u32 oy = flip ? outH - 1 - y : y;
			u32 src, dst;
			if (sameTiling) { src = (ix + iy * inW) * ib; dst = (x + oy * outW) * ob; }
			else if (inLinear) { src = (ix + iy * inW) * ib; dst = tiled (x, oy, outW) * ob; }
			else { src = tiled (ix, iy, inW) * ib; dst = (x + oy * outW) * ob; }
			u8 in[4], out[4]; int col[4];
			if (!m->mem.read (c[1] + src, in, ib)) return;
			readColor (ti, in, col);
			writeColor (to, out, col);
			if (!m->mem.write (c[2] + dst, out, ob)) return;
		}
}

}
