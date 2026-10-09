/*
 * gpu.c -- the 3DS core's third test program (phase T2): the PICA200. With no library: it builds a command list
 * of its own -- a vertex shader written here as the GPU's machine words, its uniforms, attribute buffers, the
 * registers -- gives it to gsp::Gpu, then reads the colour buffer back (tiled, in VRAM) and checks pixels against
 * what the GPU's rules give: flat and interpolated colours, each shader instruction used (MAD, CMP / IFC, LOOP,
 * MOVA and an indexed uniform, MIN, MAX, RCP, RSQ, FLR, MUL, DP3, SGE, SLT, CALL), textures (RGBA8, A4; replace,
 * modulate, a constant colour), blending, a logic operation, the colour mask, the depth test, culling, the
 * scissor, vertices given one by one, indexed triangles, a fan; and the display transfer to a screen.
 * "<n> checks, 0 failed"; the picture left on the top screen is compared too (n3ds/expect/gpu.crc).
 *
 * MIT License -- Copyright (c) 2026 Stephane Wegener and the Onyx contributors (docs/LICENSING.md).
 */
#include "sys.h"

#define FOREVER		(-1ll)
#define FB_W		240			/* the render target: the top screen, turned a quarter */
#define FB_H		400
#define VRAM		0x1F000000u
#define COLOR_VA	(VRAM)
#define DEPTH_VA	(VRAM + 0x100000)
#define LINEAR		0x14000000u
#define PHYS(va)	((u32) (va) >= VRAM ? (u32) (va) - VRAM + 0x18000000u : (u32) (va) - LINEAR + 0x20000000u)
#define SHARED_AT	0x10002000u
#define CLEAR		0x204060FFu		/* the background: R 0x20, G 0x40, B 0x60, A 0xFF */

/* ---- srv:, gsp::Gpu (as gfx.c) ---- */
static Handle s_srv, s_gsp, s_irqEvent;
static u8 *s_shared;
static int s_seen[8];

static Result getService (Handle *out, const char *name)
{
	u32 *cmd = ipcBuffer ();
	int n = 0; while (name[n]) n++;
	cmd[0] = IPC_HEADER (0x05, 4, 0); cmd[1] = 0; cmd[2] = 0;
	for (int i = 0; i < n && i < 8; i++) ((char *) &cmd[1])[i] = name[i];
	cmd[3] = (u32) n; cmd[4] = 0;
	Result r = svcSendSyncRequest (s_srv);
	if (r) return r;
	if (cmd[1] == 0) *out = cmd[3];
	return cmd[1];
}
static int popInterrupt (void)
{
	u8 *q = s_shared;
	if (!q[1]) return -1;
	int id = q[0xC + q[0]];
	q[0] = (u8) ((q[0] + 1) % 0x34); q[1]--;
	return id;
}
static void waitInterrupt (int want)
{
	for (;;)
	{
		int id;
		while ((id = popInterrupt ()) >= 0) { if (id < 8) s_seen[id]++; if (id == want) return; }
		svcWaitSynchronization (s_irqEvent, FOREVER);
	}
}
/* a command for the GPU through gsp's queue, done at once */
static void gx (u32 c0, u32 c1, u32 c2, u32 c3, u32 c4, u32 c5, u32 c6, u32 c7)
{
	u8 *q = s_shared + 0x800;
	u32 *c = (u32 *) (q + 0x20 + ((q[0] + q[1]) % 15) * 0x20);
	c[0] = c0; c[1] = c1; c[2] = c2; c[3] = c3; c[4] = c4; c[5] = c5; c[6] = c6; c[7] = c7;
	q[1]++;
	u32 *cmd = ipcBuffer ();
	cmd[0] = IPC_HEADER (0x0C, 0, 0);
	svcSendSyncRequest (s_gsp);
}

/* ---- the command list ---- */
static u32 *s_list, *s_cb;
static void W (u32 reg, u32 v) { *s_cb++ = v; *s_cb++ = reg | 0xF0000; }
/* n values: to the registers from reg on (consecutive), or all to reg */
static void WN (u32 reg, const u32 *v, int n, int consecutive)
{
	*s_cb++ = v[0]; *s_cb++ = reg | 0xF0000 | (u32) (n - 1) << 20 | (consecutive ? 0x80000000u : 0);
	for (int i = 1; i < n; i++) *s_cb++ = v[i];
	if ((n - 1) & 1) *s_cb++ = 0;
}

static u32 fbits (float f) { union { float f; u32 u; } x; x.f = f; return x.u; }
/* the GPU's 24-bit float: a sign, 7 bits of exponent, 16 of mantissa */
static u32 f24 (float f)
{
	u32 b = fbits (f);
	if (!(b & 0x7FFFFFFF)) return 0;
	return (b >> 31) << 23 | (((b >> 23 & 0xFF) - 127 + 63) & 0x7F) << 16 | (b >> 7 & 0xFFFF);
}
static void pack24 (float x, float y, float z, float w, u32 out[3])
{
	const u32 fx = f24 (x), fy = f24 (y), fz = f24 (z), fw = f24 (w);
	out[0] = fw << 8 | fz >> 16; out[1] = (fz & 0xFFFF) << 16 | fy >> 8; out[2] = (fy & 0xFF) << 24 | fx;
}
static void uniform (u32 index, float x, float y, float z, float w)	/* as 32-bit floats: w first */
{
	u32 v[4] = { fbits (w), fbits (z), fbits (y), fbits (x) };
	W (0x2C0, 0x80000000u | index);
	WN (0x2C1, v, 4, 0);
}
static void uniform24 (u32 index, float x, float y, float z, float w)	/* as 24-bit floats, packed in 3 words */
{
	u32 v[3];
	pack24 (x, y, z, w, v);
	W (0x2C0, index);
	WN (0x2C1, v, 3, 0);
}

/* ---- the vertex shader, in the GPU's machine words ---- */
#define V(n)		(n)			/* an input register */
#define R(n)		(0x10 + (n))		/* a temporary */
#define C(n)		(0x20 + (n))		/* a float uniform (7-bit operands only) */
#define O(n)		(n)			/* an output (as a destination) */
#define OP(op, dst, idx, s1, s2, desc)	((u32) (op) << 26 | (u32) (dst) << 21 | (u32) (idx) << 19 | (u32) (s1) << 12 | (u32) (s2) << 7 | (u32) (desc))
#define OPI(op, dst, idx, s1, s2, desc)	((u32) (op) << 26 | (u32) (dst) << 21 | (u32) (idx) << 19 | (u32) (s1) << 14 | (u32) (s2) << 7 | (u32) (desc))	/* the 7-bit operand second */
#define MAD(dst, s1, s2, s3, desc)	(0x38u << 26 | (u32) (dst) << 24 | (u32) (s1) << 17 | (u32) (s2) << 10 | (u32) (s3) << 5 | (u32) (desc))
#define CMP(cx, cy, s1, s2, desc)	(0x2Eu << 26 | (u32) (cx) << 24 | (u32) (cy) << 21 | (u32) (s1) << 12 | (u32) (s2) << 7 | (u32) (desc))
#define CALL(dst, num)			(0x24u << 26 | (u32) (dst) << 10 | (u32) (num))
#define IFC_X(dst, num)			(0x28u << 26 | 1u << 25 | 2u << 22 | (u32) (dst) << 10 | (u32) (num))	/* if the comparison's x is true */
#define LOOP(i, dst)			(0x29u << 26 | (u32) (i) << 22 | (u32) (dst) << 10)
#define END				(0x22u << 26)
enum { ADD = 0x00, DP3 = 0x01, DP4 = 0x02, MUL = 0x08, SLT = 0x0A, FLR = 0x0B, MAX = 0x0C, MIN = 0x0D, RCP = 0x0E, RSQ = 0x0F, MOVA = 0x12, MOV = 0x13, SGEI = 0x1A };
enum { LT = 2 };
/* operand descriptors: the destination's components (x = 8 ... w = 1), each source's swizzle */
#define XYZW	0x1Bu
#define ZZZZ	0xAAu
#define DESC(mask, sw1, sw2)	((u32) (mask) | (sw1) << 5 | (sw2) << 14 | XYZW << 23)
static const u32 DESCS[] = {
	DESC (8, XYZW, XYZW),		/* 0: .x */
	DESC (4, XYZW, XYZW),		/* 1: .y */
	DESC (2, XYZW, XYZW),		/* 2: .z */
	DESC (1, XYZW, XYZW),		/* 3: .w */
	DESC (15, XYZW, XYZW),		/* 4: all */
	DESC (15, XYZW, XYZW),		/* 5: (free) */
	DESC (2, ZZZZ, XYZW),		/* 6: .z from the first source's z */
	DESC (1, XYZW, XYZW),		/* 7: (free) */
	DESC (4, ZZZZ, XYZW),		/* 8: .y, the first source's z */
};
enum { E_BASIC = 4, E_MAD = 8, E_CMP = 13, E_LOOP = 19, E_MOVA = 25, E_MISC1 = 29, E_MISC2 = 35 };
static const u32 CODE[] = {
	/*  0 */ OP (DP4, O (0), 0, C (0), V (0), 0),		/* the position: the four rows of c0-c3 by v0 */
	/*  1 */ OP (DP4, O (0), 0, C (1), V (0), 1),
	/*  2 */ OP (DP4, O (0), 0, C (2), V (0), 2),
	/*  3 */ OP (DP4, O (0), 0, C (3), V (0), 3),
	/*  4 */ CALL (0, 4),					/* E_BASIC: colour = v1, texture coordinates = v2 */
	/*  5 */ OP (MOV, O (1), 0, V (1), 0, 4),
	/*  6 */ OP (MOV, O (2), 0, V (2), 0, 4),
	/*  7 */ END,
	/*  8 */ CALL (0, 4),					/* E_MAD: colour = v1 * c4 + c5 */
	/*  9 */ OP (MOV, R (0), 0, C (5), 0, 4),
	/* 10 */ MAD (O (1), V (1), C (4), R (0), 4),
	/* 11 */ OP (MOV, O (2), 0, V (2), 0, 4),
	/* 12 */ END,
	/* 13 */ CALL (0, 4),					/* E_CMP: colour = c6.x < v1.x ? c7 : c8 */
	/* 14 */ CMP (LT, LT, C (6), V (1), 4),
	/* 15 */ IFC_X (17, 1),
	/* 16 */ OP (MOV, O (1), 0, C (7), 0, 4),
	/* 17 */ OP (MOV, O (1), 0, C (8), 0, 4),
	/* 18 */ END,
	/* 19 */ CALL (0, 4),					/* E_LOOP: colour = c9 + c10 + c11 + c12 (the loop's counter as an index) */
	/* 20 */ OP (MOV, R (1), 0, C (9), 0, 4),
	/* 21 */ LOOP (0, 22),
	/* 22 */ OP (ADD, R (1), 3, C (10), R (1), 4),
	/* 23 */ OP (MOV, O (1), 0, R (1), 0, 4),
	/* 24 */ END,
	/* 25 */ CALL (0, 4),					/* E_MOVA: colour = c13[v2.x] */
	/* 26 */ OP (MOVA, 0, 0, V (2), 0, 0),
	/* 27 */ OP (MOV, O (1), 1, C (13), 0, 4),
	/* 28 */ END,
	/* 29 */ CALL (0, 4),					/* E_MISC1: (min (c16, v1).x, max (c16, v1).y, 1 / c16.z, v1.w >= c16.w) */
	/* 30 */ OP (MIN, O (1), 0, C (16), V (1), 0),
	/* 31 */ OP (MAX, O (1), 0, C (16), V (1), 1),
	/* 32 */ OP (RCP, O (1), 0, C (16), 0, 6),
	/* 33 */ OPI (SGEI, O (1), 0, V (1), C (16), 3),
	/* 34 */ END,
	/* 35 */ CALL (0, 4),					/* E_MISC2: (1 / sqrt (c17.x), floor (c17.y) * c17.z, c18.xyz . v1.xyz, c17.w < v1.w) */
	/* 36 */ OP (RSQ, O (1), 0, C (17), 0, 0),
	/* 37 */ OP (FLR, R (2), 0, C (17), 0, 1),
	/* 38 */ OP (MUL, O (1), 0, C (17), R (2), 8),
	/* 39 */ OP (DP3, O (1), 0, C (18), V (1), 2),
	/* 40 */ OP (SLT, O (1), 0, C (17), V (1), 3),
	/* 41 */ END,
};

/* ---- vertices: position (3 floats), colour (4), texture coordinates (2) ---- */
typedef struct { float x, y, z, r, g, b, a, u, v; } Vtx;
static Vtx *s_vb; static u32 s_vn;
static void vtx (float px, float py, float z, float r, float g, float b, float a, float u, float v)
{
	Vtx *t = &s_vb[s_vn++];
	t->x = px / (FB_W / 2) - 1.0f; t->y = py / (FB_H / 2) - 1.0f; t->z = z;	/* pixels -> clip space */
	t->r = r; t->g = g; t->b = b; t->a = a; t->u = u; t->v = v;
}
static void drawArrays (u32 mode, u32 first, u32 count)
{
	W (0x25E, mode << 8 | 2);
	W (0x25F, 1);
	W (0x22A, first);
	W (0x228, count);
	W (0x22E, 1);
}
/* a rectangle of one colour, as a strip; u, v at its corners from (0, 0) to (tu, tv) */
static void quadZ (int x, int y, int w, int h, float z, float r, float g, float b, float a, float tu, float tv)
{
	const u32 first = s_vn;
	vtx ((float) x, (float) y, z, r, g, b, a, 0, 0);
	vtx ((float) (x + w), (float) y, z, r, g, b, a, tu, 0);
	vtx ((float) x, (float) (y + h), z, r, g, b, a, 0, tv);
	vtx ((float) (x + w), (float) (y + h), z, r, g, b, a, tu, tv);
	drawArrays (1, first, 4);
}
static void quad (int x, int y, float r, float g, float b, float a) { quadZ (x, y, 24, 24, -0.5f, r, g, b, a, 1, 1); }
static void entry (u32 e) { W (0x2BA, 0x7FFF0000u | e); }

/* ---- reading the colour buffer back: 8 x 8 tiles in Z order, rows from the bottom; 0xRRGGBBAA ---- */
static const u8 TX[8] = { 0, 1, 4, 5, 16, 17, 20, 21 }, TY[8] = { 0, 2, 8, 10, 32, 34, 40, 42 };
static u32 tiled (u32 x, u32 y, u32 width) { return (u32) TX[x & 7] + TY[y & 7] + (x & ~7u) * 8 + (y & ~7u) * width; }
static u32 pixel (int x, int y)
{
	const u8 *p = (const u8 *) COLOR_VA + tiled ((u32) x, (u32) (FB_H - 1 - y), FB_W) * 4;
	return (u32) p[3] << 24 | (u32) p[2] << 16 | (u32) p[1] << 8 | p[0];
}
static void checkPixel (const char *what, int x, int y, u32 rgb, int tolerance)
{
	const u32 got = pixel (x, y) >> 8;
	int ok = 1;
	for (int i = 0; i < 3; i++)
	{
		int d = (int) (got >> (8 * i) & 255) - (int) (rgb >> (8 * i) & 255);
		if (d < -tolerance || d > tolerance) ok = 0;
	}
	if (ok) { check (what, 1); return; }
	check (what, 0);
	print ("     at "); printDec (x); print (", "); printDec (y); print (": "); printHex (got); print (", expected "); printHex (rgb); print ("\n");
}

int main (void)
{
	u32 a = 0;
	print ("3DS core: GPU test\n");
	section ("setting up");
	checkEq ("srv:", svcConnectToPort (&s_srv, "srv:"), 0);
	checkEq ("gsp::Gpu", getService (&s_gsp, "gsp::Gpu"), 0);
	u32 *cmd = ipcBuffer ();
	svcCreateEvent (&s_irqEvent, 0);
	cmd[0] = IPC_HEADER (0x13, 1, 2); cmd[1] = 1; cmd[2] = 0; cmd[3] = s_irqEvent;
	svcSendSyncRequest (s_gsp);
	checkEq ("the shared page", svcMapMemoryBlock (cmd[4], SHARED_AT, MEMPERM_RW, 0x10000000), 0);
	s_shared = (u8 *) SHARED_AT;
	checkEq ("linear memory", svcControlMemory (&a, 0, 0, 0x100000, MEMOP_ALLOC | MEMOP_LINEAR, MEMPERM_RW), 0);
	s_list = s_cb = (u32 *) a;			/* the command list */
	s_vb = (Vtx *) (a + 0x10000);			/* the vertices */
	u16 *indices = (u16 *) (a + 0x20000);
	u8 *texRgba = (u8 *) (a + 0x30000), *texA4 = (u8 *) (a + 0x31000);
	u8 *screen = (u8 *) (a + 0x40000);		/* the top screen's framebuffer */

	/* the buffers cleared by the GPU's memory fill: the colour, the depth (to 0) */
	gx (2, COLOR_VA, CLEAR, COLOR_VA + FB_W * FB_H * 4, DEPTH_VA, 0, DEPTH_VA + FB_W * FB_H * 4, 0x02010201);
	waitInterrupt (1);
	checkEq ("the colour buffer is cleared", pixel (5, 5), CLEAR);
	checkEq ("... to its last pixel", pixel (FB_W - 1, FB_H - 1), CLEAR);

	/* two textures of 8 x 8: RGBA8 -- texel (s, t), t from the bottom: red = 32 s, green = 32 t, blue = 128 --, and A4 */
	for (u32 y = 0; y < 8; y++) for (u32 x = 0; x < 8; x++)
	{
		u8 *p = texRgba + tiled (x, y, 8) * 4;				/* (the rows are stored from the top) */
		p[3] = (u8) (x * 32); p[2] = (u8) ((7 - y) * 32); p[1] = 128; p[0] = 255;
		const u32 n = tiled (x, y, 8), alpha = (x + y * 2) & 15;
		if (n & 1) texA4[n >> 1] |= (u8) (alpha << 4); else texA4[n >> 1] |= (u8) alpha;
	}

	section ("the command list");
	/* the render target, the viewport, the depth range */
	W (0x11D, PHYS (COLOR_VA) >> 3); W (0x11C, PHYS (DEPTH_VA) >> 3);
	W (0x117, 0x00000002); W (0x116, 3);
	W (0x11E, FB_W | (FB_H - 1) << 12 | 1 << 24);
	W (0x112, 0xF); W (0x113, 0xF); W (0x114, 3); W (0x115, 3);
	W (0x041, f24 (FB_W / 2)); W (0x043, f24 (FB_H / 2)); W (0x068, 0);
	W (0x04D, f24 (-1.0f)); W (0x04E, 0); W (0x06D, 1);
	W (0x040, 0); W (0x065, 0);
	/* the fragment's way: stage 0 gives the vertex's colour, the others hand it on; no test, no blending */
	W (0x0C0, 0); W (0x0C1, 0); W (0x0C2, 0); W (0x0C3, 0); W (0x0C4, 0);
	{
		static const u32 STAGES[5] = { 0x0C8, 0x0D0, 0x0D8, 0x0F0, 0x0F8 };
		for (int i = 0; i < 5; i++) { W (STAGES[i], 0x000F000F); W (STAGES[i] + 1, 0); W (STAGES[i] + 2, 0); W (STAGES[i] + 4, 0); }
	}
	W (0x0E0, 0); W (0x080, 0);
	W (0x100, 0x00E40100); W (0x101, 0x01010000); W (0x104, 0); W (0x107, 0x1F00);
	/* the shader, its descriptors, its uniforms */
	W (0x2CB, 0); WN (0x2CC, CODE, sizeof CODE / 4, 0); W (0x2BF, 1);
	W (0x2D5, 0); WN (0x2D6, DESCS, sizeof DESCS / 4, 0);
	W (0x2B9, 0xA0000002); W (0x242, 2);
	W (0x2BB, 0x76543210); W (0x2BC, 0xFEDCBA98);
	W (0x2BD, 7); W (0x04F, 3);
	W (0x050, 0x03020100); W (0x051, 0x0B0A0908); W (0x052, 0x1F1F0D0C);
	W (0x229, 0); W (0x244, 0);
	uniform (0, 1, 0, 0, 0); uniform (1, 0, 1, 0, 0); uniform (2, 0, 0, 1, 0); uniform (3, 0, 0, 0, 1);
	uniform24 (4, 2, 1, 0.5f, 1);
	uniform (5, 0.25f, 0.25f, 0.125f, 0);
	uniform (6, 0.5f, 0, 0, 0); uniform (7, 1, 1, 0, 1); uniform (8, 0, 0, 1, 1);
	uniform (9, 0, 0, 0, 0); uniform (10, 0.25f, 0, 0, 0.25f); uniform (11, 0, 0.5f, 0, 0.25f); uniform (12, 0, 0, 0.75f, 0.5f);
	uniform (13, 1, 0, 0, 1); uniform (14, 0, 1, 0, 1); uniform (15, 0, 0.5f, 1, 1);
	uniform (16, 0.5f, 0.25f, 4, 1); uniform (17, 16, 1.75f, 0.5f, 0); uniform (18, 1, 0.5f, 0, 0);
	W (0x2B1, 2 | 0 << 8 | 1 << 16);				/* the loop: 3 times, from 0, by 1 */
	/* the attributes: one buffer of position (3 floats), colour (4 floats), texture coordinates (2 floats) */
	W (0x200, PHYS (s_vb) >> 3);
	W (0x201, 0x7FB); W (0x202, 2u << 28);
	W (0x203, 0); W (0x204, 0x210); W (0x205, 3u << 28 | sizeof (Vtx) << 16);

	/* -- the draws -- */
	entry (E_BASIC);
	quad (10, 10, 0.2f, 0.4f, 0.6f, 1);						/* a flat colour */
	entry (E_MAD); quad (40, 10, 0.25f, 0.5f, 0.75f, 1);
	entry (E_CMP); quad (70, 10, 0.25f, 0, 0, 1); quad (100, 10, 0.75f, 0, 0, 1);
	entry (E_LOOP); quad (130, 10, 0, 0, 0, 1);
	entry (E_MOVA); quadZ (160, 10, 24, 24, -0.5f, 0, 0, 0, 1, 0, 0);		/* (u = 0 at every corner: the index 0) */
	{
		const u32 first = s_vn;							/* ... and the index 2 */
		vtx (190, 10, -0.5f, 0, 0, 0, 1, 2, 0); vtx (214, 10, -0.5f, 0, 0, 0, 1, 2, 0);
		vtx (190, 34, -0.5f, 0, 0, 0, 1, 2, 0); vtx (214, 34, -0.5f, 0, 0, 0, 1, 2, 0);
		drawArrays (1, first, 4);
	}
	entry (E_MISC1); quad (10, 40, 0.75f, 0.5f, 0, 1);
	entry (E_MISC2); quad (40, 40, 0.25f, 0.5f, 0.75f, 1);
	entry (E_BASIC);
	W (0x101, 0x76760000); quad (70, 40, 1, 0, 0, 0.5f); W (0x101, 0x01010000);	/* blending: source alpha, 1 - source alpha */
	W (0x100, 0x00E40000); W (0x102, 11); quad (100, 40, 1, 1, 0, 1); W (0x100, 0x00E40100);	/* a logic operation: exclusive or */
	W (0x107, 0x0100); quad (130, 40, 1, 1, 1, 1); W (0x107, 0x1F00);		/* only red is written */
	{										/* a triangle given vertex by vertex */
		static const float T[3][2] = { { 160, 40 }, { 184, 40 }, { 160, 64 } };
		W (0x25E, 0 << 8 | 2); W (0x25F, 1);
		W (0x232, 0xF);
		for (int i = 0; i < 3; i++)
		{
			u32 w[3];
			pack24 (T[i][0] / (FB_W / 2) - 1.0f, T[i][1] / (FB_H / 2) - 1.0f, -0.5f, 1, w); WN (0x233, w, 3, 1);
			pack24 (1, 0.5f, 0, 1, w); WN (0x233, w, 3, 1);
			pack24 (0, 0, 0, 1, w); WN (0x233, w, 3, 1);
		}
	}
	{										/* two triangles by their indices */
		const u32 first = s_vn;
		vtx (190, 40, -0.5f, 0, 1, 1, 1, 0, 0); vtx (214, 40, -0.5f, 0, 1, 1, 1, 0, 0);
		vtx (190, 64, -0.5f, 0, 1, 1, 1, 0, 0); vtx (214, 64, -0.5f, 0, 1, 1, 1, 0, 0);
		static const u8 ORDER[6] = { 0, 1, 2, 2, 1, 3 };
		for (int i = 0; i < 6; i++) indices[i] = (u16) (first + ORDER[i]);
		W (0x25E, 0 << 8 | 2); W (0x25F, 1);
		W (0x227, 0x80000000u | (u32) ((u8 *) indices - (u8 *) s_vb));
		W (0x228, 6); W (0x22F, 1);
	}
	/* the depth test "greater": the depth is -z here -- 0.25, then 0.5 over it, then 0.3 which stays behind */
	W (0x107, 0x1F00 | 6 << 4 | 1);
	quadZ (10, 70, 24, 24, -0.25f, 1, 0, 0, 1, 0, 0);
	quadZ (10, 70, 24, 24, -0.5f, 0, 1, 0, 1, 0, 0);
	quadZ (10, 70, 24, 24, -0.3f, 0, 0, 1, 1, 0, 0);
	W (0x107, 0x1F00);
	/* culling, mode 2 (the back faces removed): the counter-clockwise triangles are kept */
	W (0x040, 2);
	{
		u32 first = s_vn;
		vtx (40, 70, -0.5f, 1, 1, 1, 1, 0, 0); vtx (64, 70, -0.5f, 1, 1, 1, 1, 0, 0); vtx (40, 94, -0.5f, 1, 1, 1, 1, 0, 0);
		drawArrays (0, first, 3);
		first = s_vn;
		vtx (70, 70, -0.5f, 1, 1, 1, 1, 0, 0); vtx (70, 94, -0.5f, 1, 1, 1, 1, 0, 0); vtx (94, 70, -0.5f, 1, 1, 1, 1, 0, 0);
		drawArrays (0, first, 3);
	}
	W (0x040, 0);
	{										/* a fan */
		const u32 first = s_vn;
		vtx (100, 70, -0.5f, 1, 0, 1, 1, 0, 0); vtx (124, 70, -0.5f, 1, 0, 1, 1, 0, 0);
		vtx (124, 94, -0.5f, 1, 0, 1, 1, 0, 0); vtx (100, 94, -0.5f, 1, 0, 1, 1, 0, 0);
		drawArrays (2, first, 4);
	}
	W (0x065, 3); W (0x066, 140 | 75 << 16); W (0x067, 159 | 89 << 16);		/* the scissor: only x 140-159, y 75-89 */
	quadZ (130, 70, 50, 24, -0.5f, 1, 1, 1, 1, 0, 0);
	W (0x065, 0);
	/* a texture alone, then multiplied by the vertex's colour */
	W (0x080, 1); W (0x082, 8 | 8 << 16); W (0x083, 0x2200); W (0x085, PHYS (texRgba) >> 3); W (0x08E, 0);
	W (0x0C0, 0x00030003); W (0x0C2, 0);
	quadZ (20, 110, 80, 80, -0.5f, 1, 1, 1, 1, 1, 1);
	W (0x0C2, 0x00010001);
	quadZ (120, 110, 80, 80, -0.5f, 0.5f, 1, 1, 1, 1, 1);
	/* an A4 texture: a constant yellow whose alpha is the texture's, blended */
	W (0x085, PHYS (texA4) >> 3); W (0x08E, 0xB);
	W (0x0C0, 0x0003000E); W (0x0C2, 0); W (0x0C3, 0xFF00FFFF);
	W (0x101, 0x76760000);
	quadZ (200, 195, 32, 32, -0.5f, 1, 1, 1, 1, 1, 1);
	W (0x101, 0x01010000);
	W (0x080, 0); W (0x0C0, 0); W (0x0C2, 0);
	/* a big triangle whose corners are red, green, blue */
	{
		const u32 first = s_vn;
		vtx (20, 230, -0.5f, 1, 0, 0, 1, 0, 0); vtx (220, 230, -0.5f, 0, 1, 0, 1, 0, 0); vtx (120, 390, -0.5f, 0, 0, 1, 1, 0, 0);
		drawArrays (0, first, 3);
	}
	W (0x111, 1); W (0x110, 1); W (0x010, 0x12345678);
	while (((u32) (s_cb - s_list) * 4) & 15) *s_cb++ = 0;
	check ("it fits its buffer", (u32) (s_cb - s_list) * 4 < 0x10000 && s_vn * sizeof (Vtx) < 0x10000);

	gx (1, (u32) s_list, (u32) (s_cb - s_list) * 4, 0, 0, 0, 0, 0);
	waitInterrupt (5);
	check ("the list is run (its interrupt came)", s_seen[5] == 1);

	section ("the vertex shader");
	/* (a colour of exactly one half may come out as 127 or 128: one unit is allowed where a half is expected) */
	checkPixel ("a flat colour", 15, 15, 0x336699, 0);
	checkPixel ("... to the rectangle's first pixel", 10, 10, 0x336699, 0);
	checkPixel ("... and its last", 33, 33, 0x336699, 0);
	checkPixel ("... not one further", 34, 34, CLEAR >> 8, 0);
	checkPixel ("... nor one before", 9, 9, CLEAR >> 8, 0);
	checkPixel ("multiply and add (a 24-bit uniform)", 45, 15, 0xBFBF80, 1);
	checkPixel ("a comparison: false", 75, 15, 0x0000FF, 0);
	checkPixel ("a comparison: true", 105, 15, 0xFFFF00, 0);
	checkPixel ("a loop over indexed uniforms", 135, 15, 0x4080BF, 1);
	checkPixel ("a uniform chosen by an attribute: 0", 165, 15, 0xFF0000, 0);
	checkPixel ("... 2", 195, 15, 0x0080FF, 1);
	checkPixel ("min, max, reciprocal, greater-or-equal", 15, 45, 0x808040, 1);
	checkEq ("... its alpha", pixel (15, 45) & 255, 255);
	checkPixel ("inverse root, floor, multiply, dot product, less-than", 45, 45, 0x408080, 1);
	checkEq ("... its alpha", pixel (45, 45) & 255, 255);

	section ("fragments");
	checkPixel ("blending: half red over the background", 75, 45, 0x8F1F2F, 2);
	checkPixel ("a logic operation: exclusive or", 105, 45, 0xDFBF60, 0);
	checkPixel ("only red written", 135, 45, 0xFF4060, 0);
	checkPixel ("the depth test: the nearest drawn stays", 15, 75, 0x00FF00, 0);
	checkPixel ("a texture: its texel (0, 0)", 25, 115, 0x000080, 0);
	checkPixel ("... (7, 0)", 95, 115, 0xE00080, 0);
	checkPixel ("... (0, 7)", 25, 185, 0x00E080, 0);
	checkPixel ("... (3, 5)", 55, 165, 0x60A080, 0);
	checkPixel ("a texture times a colour", 195, 185, 0x70E080, 1);
	checkPixel ("an alpha texture, a constant colour, blended", 214, 218, 0x889933, 2);

	section ("triangles");
	checkPixel ("a vertex-by-vertex triangle", 165, 45, 0xFF8000, 1);
	checkPixel ("... its other half is not drawn", 182, 62, CLEAR >> 8, 0);
	checkPixel ("indexed triangles: the first", 195, 45, 0x00FFFF, 0);
	checkPixel ("... the second", 212, 62, 0x00FFFF, 0);
	checkPixel ("culling: a counter-clockwise triangle is drawn", 45, 75, 0xFFFFFF, 0);
	checkPixel ("... a clockwise one is not", 75, 75, CLEAR >> 8, 0);
	checkPixel ("a fan: its first triangle", 120, 75, 0xFF00FF, 0);
	checkPixel ("... its second", 104, 90, 0xFF00FF, 0);
	checkPixel ("the scissor: inside", 150, 80, 0xFFFFFF, 0);
	checkPixel ("... its first pixel", 140, 75, 0xFFFFFF, 0);
	checkPixel ("... its last", 159, 89, 0xFFFFFF, 0);
	checkPixel ("... outside, left", 139, 80, CLEAR >> 8, 0);
	checkPixel ("... outside, above", 150, 90, CLEAR >> 8, 0);
	checkPixel ("interpolation: the middle of red, green, blue", 120, 283, 0x555555, 3);
	checkPixel ("... near the red corner", 26, 232, 0xF30508, 8);
	checkPixel ("... outside it", 20, 300, CLEAR >> 8, 0);

	section ("the display transfer");
	gx (3, COLOR_VA, (u32) screen, FB_W | FB_H << 16, FB_W | FB_H << 16, 0x1000, 0, 0);	/* RGBA8, tiled -> RGB8, in lines */
	waitInterrupt (4);
	{
		const u8 *p = screen + (15 + (FB_H - 1 - 15) * FB_W) * 3;			/* (blue first) */
		check ("a pixel reaches the screen's buffer", p[0] == 0x99 && p[1] == 0x66 && p[2] == 0x33);
		p = screen + (120 + (FB_H - 1 - 283) * FB_W) * 3;
		check ("... and the triangle's middle", p[0] > 0x50 && p[0] < 0x5A && p[1] > 0x50 && p[1] < 0x5A && p[2] > 0x50 && p[2] < 0x5A);
	}
	{
		u8 *u = s_shared + 0x200;
		u32 *info = (u32 *) (u + 4);
		info[0] = 0; info[1] = (u32) screen; info[2] = (u32) screen; info[3] = FB_W * 3; info[4] = 1; info[5] = 0; info[6] = 0;
		u[0] = 0; u[1] = 1;
	}
	waitInterrupt (2);
	summary ();
	return 0;
}
