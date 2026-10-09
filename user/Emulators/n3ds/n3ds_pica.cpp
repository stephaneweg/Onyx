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
// Not done yet: lighting, fog, shadows, stencil, the geometry shader, texture filtering (the nearest texel is
// taken), mipmaps -- noted when a program asks.
//
// Written from the public documentation of the GPU (3dbrew's register and shader pages); no code of another
// emulator.
//
// MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors (see n3ds.h).
//
#include <math.h>
#include <stdio.h>
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
	u32 floatIndex; bool float32; u32 floatWords[4]; int floatCount;
	// attributes given by registers: the fixed ones, and the vertices given one by one
	float fixed[12][4]; u32 fixedIndex; u32 fixedWords[3]; int fixedCount;
	float immediate[16][4]; int immediateCount;
	// the procedural texture's tables: the two maps (128 values and their slopes), the colours (256)
	float procMap[2][128], procMapDiff[2][128]; u32 procColor[256];
	u32 procIndex, procTable;
	// the triangle being assembled
	Vertex prim[3]; int primCount; bool stripFlip;
	u32 codeTop; int dumped;			// (traces: how much code was loaded; shaders already printed)
	int jump;					// a jump asked by the command being run: 1 or 2 (which buffer), 0 none
	// counters
	u64 triangles, pixels, depthFailed, alphaFailed;
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
struct Shader
{
	float in[16][4], tmp[16][4], out[16][4];
	bool cmp[2]; int a[2]; int aL;
};

// The GPU's product: nothing times anything is nothing, even infinity (where IEEE says "not a number"): shaders
// count on it.
static inline float mulz (float a, float b) { return a == 0.0f || b == 0.0f ? 0.0f : a * b; }

static inline const float *srcReg (const Pica *p, const Shader &s, u32 r)
{
	if (r < 0x10) return s.in[r];
	if (r < 0x20) return s.tmp[r - 0x10];
	r -= 0x20;
	return p->fu[r < 96 ? r : 95];
}

static void runShader (Pica *p, Shader &s, u32 entry)
{
	struct Frame { u32 end, ret, repeat, inc, loop; } stack[16];
	int depth = 0;
	u32 pc = entry;
	s.cmp[0] = s.cmp[1] = false; s.a[0] = s.a[1] = 0; s.aL = 0;
	for (int steps = 0; steps < 65536; steps++)
	{
		while (depth && pc == stack[depth - 1].end)			// the end of a called block, of a loop's body
		{
			Frame &f = stack[depth - 1];
			if (f.repeat) { f.repeat--; s.aL += (int) f.inc; pc = f.loop; }
			else { pc = f.ret; depth--; }
		}
		if (pc >= CODE_MAX) return;
		const u32 ins = p->code[pc++];
		const u32 op = ins >> 26;
		if (op == 0x22) return;						// END
		if (op == 0x21 || op == 0x20) continue;				// NOP, BREAK
		if (op >= 0x24 && op <= 0x2D && op != 0x2A && op != 0x2B)	// flow: CALL*, IF*, LOOP, JMP*
		{
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
		if (op == 0x2A || op == 0x2B || op == 0x23) continue;		// EMIT, SETEMIT (geometry shaders), BREAKC

		// arithmetic: where the operands and the descriptor are depends on the instruction's shape
		u32 d, r1, r2, r3 = 0, desc, idx;
		bool mad = false;
		if (op >= 0x30)							// MADI (0x30-0x37), MAD (0x38-0x3F)
		{
			mad = true;
			d = ins >> 24 & 0x1F; idx = ins >> 22 & 3; r1 = ins >> 17 & 0x1F; desc = ins & 0x1F;
			if (op >= 0x38) { r2 = ins >> 10 & 0x7F; r3 = ins >> 5 & 0x1F; if (idx) r2 += (u32) (idx == 1 ? s.a[0] : idx == 2 ? s.a[1] : s.aL); }
			else { r2 = ins >> 12 & 0x1F; r3 = ins >> 5 & 0x7F; if (idx) r3 += (u32) (idx == 1 ? s.a[0] : idx == 2 ? s.a[1] : s.aL); }
		}
		else
		{
			d = ins >> 21 & 0x1F; idx = ins >> 19 & 3; desc = ins & 0x7F;
			const int off = idx == 0 ? 0 : idx == 1 ? s.a[0] : idx == 2 ? s.a[1] : s.aL;
			if (op >= 0x18 && op <= 0x1B) { r1 = ins >> 14 & 0x1F; r2 = (ins >> 7 & 0x7F) + (u32) off; }	// the "inverted" ones
			else { r1 = (ins >> 12 & 0x7F) + (u32) off; r2 = ins >> 7 & 0x1F; }
		}
		const u32 od = p->opdesc[desc & (OPDESC_MAX - 1)];
		float s1[4], s2[4], s3[4];
		{
			const float *q = srcReg (p, s, r1 & 0x7F); const u32 sw = od >> 5 & 0xFF; const bool neg = (od >> 4 & 1) != 0;
			for (int i = 0; i < 4; i++) { float v = q[sw >> (6 - 2 * i) & 3]; s1[i] = neg ? -v : v; }
			q = srcReg (p, s, r2 & 0x7F); const u32 sw2 = od >> 14 & 0xFF; const bool neg2 = (od >> 13 & 1) != 0;
			for (int i = 0; i < 4; i++) { float v = q[sw2 >> (6 - 2 * i) & 3]; s2[i] = neg2 ? -v : v; }
			if (mad)
			{
				q = srcReg (p, s, r3 & 0x7F); const u32 sw3 = od >> 23 & 0xFF; const bool neg3 = (od >> 22 & 1) != 0;
				for (int i = 0; i < 4; i++) { float v = q[sw3 >> (6 - 2 * i) & 3]; s3[i] = neg3 ? -v : v; }
			}
		}
		float *dst = d < 0x10 ? s.out[d] : s.tmp[d - 0x10];
		const u32 mask = od & 15;
#define EACH(expr) for (int i = 0; i < 4; i++) if (mask & (8u >> i)) dst[i] = (expr)
		if (mad) { EACH (mulz (s1[i], s2[i]) + s3[i]); continue; }
		switch (op)
		{
		case 0x00: EACH (s1[i] + s2[i]); break;									// ADD
		case 0x01: { float v = mulz (s1[0], s2[0]) + mulz (s1[1], s2[1]) + mulz (s1[2], s2[2]); EACH (v); break; }			// DP3
		case 0x02: { float v = mulz (s1[0], s2[0]) + mulz (s1[1], s2[1]) + mulz (s1[2], s2[2]) + mulz (s1[3], s2[3]); EACH (v); break; }	// DP4
		case 0x03: case 0x18: { float v = mulz (s1[0], s2[0]) + mulz (s1[1], s2[1]) + mulz (s1[2], s2[2]) + s2[3]; EACH (v); break; }	// DPH
		case 0x04: case 0x19: { float t[4] = { 1.0f, s1[1] * s2[1], s1[2], s2[3] }; EACH (t[i]); break; }		// DST
		case 0x05: { float v = exp2f (s1[0]); EACH (v); break; }							// EX2
		case 0x06: { float v = log2f (s1[0]); EACH (v); break; }							// LG2
		case 0x08: EACH (mulz (s1[i], s2[i])); break;									// MUL
		case 0x09: case 0x1A: EACH (s1[i] >= s2[i] ? 1.0f : 0.0f); break;						// SGE
		case 0x0A: case 0x1B: EACH (s1[i] < s2[i] ? 1.0f : 0.0f); break;						// SLT
		case 0x0B: EACH (floorf (s1[i])); break;									// FLR
		case 0x0C: EACH (s1[i] > s2[i] ? s1[i] : s2[i]); break;							// MAX
		case 0x0D: EACH (s1[i] < s2[i] ? s1[i] : s2[i]); break;							// MIN
		case 0x0E: { float v = 1.0f / s1[0]; EACH (v); break; }							// RCP
		case 0x0F: { float v = 1.0f / sqrtf (s1[0]); EACH (v); break; }						// RSQ
		case 0x12: if (mask & 8) s.a[0] = (int) s1[0]; if (mask & 4) s.a[1] = (int) s1[1]; break;			// MOVA
		case 0x13: EACH (s1[i]); break;										// MOV
		case 0x2E: case 0x2F:											// CMP
			for (int i = 0; i < 2; i++)
			{
				const u32 c = i == 0 ? ins >> 24 & 7 : ins >> 21 & 7;
				const float x = s1[i], y = s2[i];
				s.cmp[i] = c == 0 ? x == y : c == 1 ? x != y : c == 2 ? x < y : c == 3 ? x <= y : c == 4 ? x > y : c == 5 ? x >= y : true;
			}
			break;
		default: p->m->note ("vertex shader instruction %02x", (unsigned) op); break;
		}
#undef EACH
	}
}

// Runs the shader on a vertex's attributes (the input registers are fed through the permutation) and maps its
// outputs to the vertex's meanings.
static void shadeVertex (Pica *p, float attr[16][4], int count, Vertex &v)
{
	Shader s;
	memset (&s, 0, sizeof s);
	const u64 perm = (u64) p->regs[R_VSH_PERM_HI] << 32 | p->regs[R_VSH_PERM_LO];
	for (int i = 0; i < count && i < 16; i++) memcpy (s.in[perm >> (4 * i) & 15], attr[i], sizeof (float) * 4);
	runShader (p, s, p->regs[R_VSH_ENTRY] & 0xFFFF);
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
struct Texture { const u8 *data; u32 w, h, format, wrapS, wrapT; bool on; };

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
static void texel (const Texture &t, int s, int tt, int out[4])
{
	const u32 x = (u32) wrapCoord (s, (int) t.w, t.wrapS), y = t.h - 1 - (u32) wrapCoord (tt, (int) t.h, t.wrapT);
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
struct Fragment
{
	Texture tex[3];
	ProcTex proc;
	struct Stage { u32 src[3], srcA[3], op[3], opA[3], mode, modeA; int konst[4]; int scale, scaleA; bool pass; } stage[6];
	int bufferColor[4]; u32 updateRgb, updateA;
	bool alphaTest; u32 alphaFunc, alphaRef;
	bool depthTest; u32 depthFunc; bool depthMask; bool rgbaMask[4];
	bool blend; u32 eqRgb, eqA, srcRgb, dstRgb, srcA, dstA; int blendColor[4]; u32 logicOp;
};

static void fragmentState (const Pica *p, Fragment &f)
{
	for (int i = 0; i < 3; i++) texInfo (p, i, f.tex[i]);
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
}

// ---- triangles ---------------------------------------------------------------------------------------------------------
struct Screen { float x, y, z, invw; const Vertex *v; };

static void rasterize (Pica *p, const Target &t, const Fragment &f, Screen s0, Screen s1, Screen s2)
{
	// the triangle's side: culled, or turned so that its area is positive
	float area = (s1.x - s0.x) * (s2.y - s0.y) - (s1.y - s0.y) * (s2.x - s0.x);
	const u32 cull = p->regs[R_CULL] & 3;
	if (area == 0.0f) return;
	if (cull == 1 && area > 0) return;					// 1: the counter-clockwise ones (the "front") are removed
	if (cull == 2 && area < 0) return;					// 2: the clockwise ones (the "back") -- what games use
	if (area < 0) { Screen tmp = s1; s1 = s2; s2 = tmp; area = -area; }
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
	if (x0 >= x1 || y0 >= y1) return;
	p->triangles++;
	const float inv = 1.0f / area;
	const u32 depthMax = t.depthBytes == 2 ? 0xFFFF : 0xFFFFFF;
	// an edge shared by two triangles belongs to one: the left and the lower ones are in
	const float e0x = s2.x - s1.x, e0y = s2.y - s1.y, e1x = s0.x - s2.x, e1y = s0.y - s2.y, e2x = s1.x - s0.x, e2y = s1.y - s0.y;
	const bool in0 = e0y < 0 || (e0y == 0 && e0x > 0), in1 = e1y < 0 || (e1y == 0 && e1x > 0), in2 = e2y < 0 || (e2y == 0 && e2x > 0);
	for (int y = y0; y < y1; y++)
	{
		const float py = (float) y + 0.5f;
		for (int x = x0; x < x1; x++)
		{
			const float px = (float) x + 0.5f;
			const float w0 = e0x * (py - s1.y) - e0y * (px - s1.x);
			const float w1 = e1x * (py - s2.y) - e1y * (px - s2.x);
			const float w2 = e2x * (py - s0.y) - e2y * (px - s0.x);
			if (w0 < 0 || w1 < 0 || w2 < 0) continue;
			if ((w0 == 0 && !in0) || (w1 == 0 && !in1) || (w2 == 0 && !in2)) continue;
			const float l0 = w0 * inv, l1 = w1 * inv, l2 = w2 * inv;
			// the depth, linear on the screen
			float z = l0 * s0.z + l1 * s1.z + l2 * s2.z;
			if (z < 0) z = 0; else if (z > 1) z = 1;
			const u32 depth = (u32) (z * (float) depthMax);
			const u32 row = (u32) (t.h - 1 - y);				// (the buffers' rows go from the bottom)
			const u32 at = tiled ((u32) x, row, (u32) t.w);
			u8 *dq = t.depth ? t.depth + at * t.depthBytes : 0;
			if (f.depthTest && dq)
			{
				const u32 old = t.depthBytes == 2 ? (u32) (dq[0] | dq[1] << 8) : (u32) (dq[0] | dq[1] << 8 | dq[2] << 16);
				if (!compare (f.depthFunc, depth, old)) { p->depthFailed++; continue; }
			}
			// what the vertices carry, perspective-correct
			const float q0 = l0 * s0.invw, q1 = l1 * s1.invw, q2 = l2 * s2.invw, qs = 1.0f / (q0 + q1 + q2);
#define ATTR(i) ((q0 * s0.v->a[i] + q1 * s1.v->a[i] + q2 * s2.v->a[i]) * qs)
			int primary[4];
			for (int i = 0; i < 4; i++) { float c = ATTR (A_COLOR + i); primary[i] = c <= 0 ? 0 : c >= 1 ? 255 : (int) (c * 255.0f + 0.5f); }
			int texc[3][4];
			for (int u = 0; u < 3; u++)
			{
				const Texture &tx = f.tex[u];
				if (!tx.on) { texc[u][0] = texc[u][1] = texc[u][2] = 0; texc[u][3] = 255; continue; }
				const int ai = u == 0 ? A_TC0 : u == 1 ? A_TC1 : A_TC2;
				texel (tx, (int) floorf (ATTR (ai) * (float) tx.w), (int) floorf (ATTR (ai + 1) * (float) tx.h), texc[u]);
			}
			int proc[4] = { 0, 0, 0, 255 };
			if (f.proc.on)
			{
				const int ai = f.proc.coord == 0 ? A_TC0 : f.proc.coord == 1 ? A_TC1 : A_TC2;
				procTexel (p, f.proc, ATTR (ai), ATTR (ai + 1), proc);
			}
#undef ATTR
			// the combiner's stages
			int prev[4] = { primary[0], primary[1], primary[2], primary[3] };
			int buffer[4] = { f.bufferColor[0], f.bufferColor[1], f.bufferColor[2], f.bufferColor[3] }, nextBuffer[4] = { buffer[0], buffer[1], buffer[2], buffer[3] };
			for (int st = 0; st < 6; st++)
			{
				const Fragment::Stage &sg = f.stage[st];
				for (int i = 0; i < 4; i++) buffer[i] = nextBuffer[i];
				if (!sg.pass)
				{
					int c[3][3], a[3];
					for (int k = 0; k < 3; k++)
					{
						const int *src;
						switch (sg.src[k]) { case 0: case 1: case 2: src = primary; break; case 3: src = texc[0]; break; case 4: src = texc[1]; break; case 5: src = texc[2]; break; case 6: src = proc; break; case 13: src = buffer; break; case 14: src = sg.konst; break; default: src = prev; break; }
						tevColor (src, sg.op[k], c[k]);
						switch (sg.srcA[k]) { case 0: case 1: case 2: src = primary; break; case 3: src = texc[0]; break; case 4: src = texc[1]; break; case 5: src = texc[2]; break; case 6: src = proc; break; case 13: src = buffer; break; case 14: src = sg.konst; break; default: src = prev; break; }
						a[k] = tevAlpha (src, sg.opA[k]);
					}
					int out[4];
					if (sg.mode == 6 || sg.mode == 7)			// the dot product of two colours as vectors
					{
						int d = ((c[0][0] * 2 - 255) * (c[1][0] * 2 - 255) + (c[0][1] * 2 - 255) * (c[1][1] * 2 - 255) + (c[0][2] * 2 - 255) * (c[1][2] * 2 - 255)) / 255;
						out[0] = out[1] = out[2] = clamp255 (d);
					}
					else for (int i = 0; i < 3; i++) out[i] = tevCombine (sg.mode, c[0][i], c[1][i], c[2][i]);
					out[3] = sg.mode == 7 ? out[0] : tevCombine (sg.modeA, a[0], a[1], a[2]);
					for (int i = 0; i < 3; i++) prev[i] = clamp255 (out[i] * sg.scale);
					prev[3] = clamp255 (out[3] * sg.scaleA);
				}
				if (st < 4)
				{
					if (f.updateRgb >> st & 1) { nextBuffer[0] = prev[0]; nextBuffer[1] = prev[1]; nextBuffer[2] = prev[2]; }
					if (f.updateA >> st & 1) nextBuffer[3] = prev[3];
				}
			}
			if (f.alphaTest && !compare (f.alphaFunc, (u32) prev[3], f.alphaRef)) { p->alphaFailed++; continue; }
			p->pixels++;
			if (dq && t.depthWrite && f.depthMask && f.depthTest)
			{
				dq[0] = (u8) depth; dq[1] = (u8) (depth >> 8);
				if (t.depthBytes > 2) dq[2] = (u8) (depth >> 16);
			}
			if (!t.colorWrite) continue;
			u8 *cq = t.color + at * t.colorBytes;
			int dst[4], out[4];
			readColor (t, cq, dst);
			if (f.blend)
			{
				int sf[4], df[4], sfa[4], dfa[4];
				blendFactor (f.srcRgb, prev, dst, f.blendColor, sf); blendFactor (f.dstRgb, prev, dst, f.blendColor, df);
				blendFactor (f.srcA, prev, dst, f.blendColor, sfa); blendFactor (f.dstA, prev, dst, f.blendColor, dfa);
				for (int i = 0; i < 3; i++) out[i] = blendEq (f.eqRgb, prev[i], sf[i], dst[i], df[i]);
				out[3] = blendEq (f.eqA, prev[3], sfa[3], dst[3], dfa[3]);
			}
			else for (int i = 0; i < 4; i++)
			{
				const int s = prev[i], d = dst[i];
				switch (f.logicOp)
				{
				case 0: out[i] = 0; break; case 1: out[i] = s & d; break; case 2: out[i] = s & ~d & 255; break; case 3: out[i] = s; break;
				case 4: out[i] = 255; break; case 5: out[i] = ~s & 255; break; case 6: out[i] = d; break; case 7: out[i] = ~d & 255; break;
				case 8: out[i] = ~(s & d) & 255; break; case 9: out[i] = s | d; break; case 10: out[i] = ~(s | d) & 255; break; case 11: out[i] = s ^ d; break;
				case 12: out[i] = ~(s ^ d) & 255; break; case 13: out[i] = ~s & d & 255; break; case 14: out[i] = (s | ~d) & 255; break; default: out[i] = (~s | d) & 255; break;
				}
			}
			for (int i = 0; i < 4; i++) if (!f.rgbaMask[i]) out[i] = dst[i];
			if (p->m->traceGpu && x == t.w / 2 && y == t.h / 2)
				fprintf (stderr, "   centre: primary %d %d %d %d, tex0 %d %d %d %d, combined %d %d %d %d, there %d %d %d %d -> %d %d %d %d%c", primary[0], primary[1], primary[2], primary[3],
					 texc[0][0], texc[0][1], texc[0][2], texc[0][3], prev[0], prev[1], prev[2], prev[3], dst[0], dst[1], dst[2], dst[3], out[0], out[1], out[2], out[3], 10);
			writeColor (t, cq, out);
		}
	}
}

// A triangle from the shader: clipped to what can be seen (in clip space: -w <= x, y <= w, -w <= z <= 0), each
// piece put on the screen.
static void triangle (Pica *p, const Vertex &a, const Vertex &b, const Vertex &c)
{
	Target t;
	if (!target (p, t)) return;
	Fragment f;
	fragmentState (p, f);
	Vertex poly[2][12]; int n = 3, cur = 0;
	poly[0][0] = a; poly[0][1] = b; poly[0][2] = c;
	for (int plane = 0; plane < 7 && n >= 3; plane++)
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
	for (int i = 1; i + 1 < n; i++) rasterize (p, t, f, s[0], s[i], s[i + 1]);
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

// ---- draws ---------------------------------------------------------------------------------------------------------------
static void draw (Pica *p, bool indexed)
{
	const u32 base = p->regs[R_ATTR_BASE] << 3;
	const u64 formats = (u64) p->regs[R_ATTR_FORMAT_HI] << 32 | p->regs[R_ATTR_FORMAT_LO];
	const u32 fixedMask = p->regs[R_ATTR_FORMAT_HI] >> 16 & 0xFFF;
	const int inputs = (int) (p->regs[R_VSH_INPUT] & 15) + 1;
	const u32 count = p->regs[R_NUM_VERTICES];
	if (count > 0x100000) return;
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
	const u64 pixelsBefore = p->pixels, trisBefore = p->triangles, depthBefore = p->depthFailed, alphaBefore = p->alphaFailed;
	p->primCount = 0; p->stripFlip = false;
	for (u32 n = 0; n < count; n++)
	{
		const u32 vi = indexed ? (index16 ? (u32) (index[n * 2] | index[n * 2 + 1] << 8) : index[n]) : n + p->regs[R_VERTEX_OFFSET];
		float attr[16][4];
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
		Vertex v;
		shadeVertex (p, attr, inputs, v);
		if (p->m->traceGpu && n == 0)
		{
			fprintf (stderr, "   in:");
			for (int i = 0; i < inputs && i < 8; i++) fprintf (stderr, " [%g %g %g %g]", (double) attr[i][0], (double) attr[i][1], (double) attr[i][2], (double) attr[i][3]);
			fprintf (stderr, "  formats %08x %08x perm %08x fixed %03x%c", (unsigned) p->regs[R_ATTR_FORMAT_LO], (unsigned) p->regs[R_ATTR_FORMAT_HI], (unsigned) p->regs[R_VSH_PERM_LO], (unsigned) fixedMask, 10);
		}
		if (p->m->traceGpu && n < 3) fprintf (stderr, "   v%u clip %g %g %g %g  color %g %g %g %g  tc %g %g%c", (unsigned) n, (double) v.a[0], (double) v.a[1], (double) v.a[2], (double) v.a[3], (double) v.a[8], (double) v.a[9], (double) v.a[10], (double) v.a[11], (double) v.a[12], (double) v.a[13], 10);
		assemble (p, v);
	}
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
	const u32 v = p->regs[id];
	switch (id)
	{
	case R_DRAW_ARRAYS: draw (p, false); break;
	case R_DRAW_ELEMENTS: draw (p, true); break;
	case R_PRIM_RESTART: p->primCount = 0; p->stripFlip = false; break;
	case R_PRIM_CONFIG: p->primCount = 0; p->stripFlip = false; break;
	case R_LIGHTING: if (v & 1) p->m->note ("lighting"); break;
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
			shadeVertex (p, p->immediate, p->immediateCount, vtx);
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
		else if (id >= R_VSH_CODE_DATA && id < R_VSH_CODE_DATA + 8) { if (p->codeIndex < CODE_MAX) { p->code[p->codeIndex++] = value; if (p->codeIndex > p->codeTop) p->codeTop = p->codeIndex; } }
		else if (id >= R_VSH_OPDESC_DATA && id < R_VSH_OPDESC_DATA + 8) { if (p->opdescIndex < OPDESC_MAX) p->opdesc[p->opdescIndex++] = value; }
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

void picaFree (Machine *m) { free (m->pica); m->pica = 0; }

// A command list: a value, then a header -- the register, a mask of the value's bytes, how many more values
// follow, and whether they go to the following registers or all to this one. Each command fills 8 bytes' multiples.
// A list may go on in another buffer (two registers pairs give an address and a size; writing the jump register
// goes there): games build their frame from such pieces.
void picaCommandList (Machine *m, u32 va, u32 size)
{
	Pica *p = pica (m);
	if (!p || (va & 3) || size > 0x400000) return;
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
			if (!next || ++jumps > 4096) { m->note ("a command list's jump to nowhere"); return; }
			list = next; words = bytes / 4; at = 0;
		}
	}
}

// ---- transfers -------------------------------------------------------------------------------------------------------
// A display transfer: what was rendered (a tiled buffer) into a framebuffer (linear), or the other way; the
// formats may differ, the picture may be halved, turned upside down.
void picaDisplayTransfer (Machine *m, const u32 *c)
{
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
