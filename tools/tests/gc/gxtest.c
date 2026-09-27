/*
 * gxtest.c -- a bare-metal GameCube program for the GX drawing (gctest dol ... -> the GPU frame
 * rendered in software): through the FIFO, the vertex format (position f32 xyz, colour RGBA8,
 * texture coordinate f32 st), an identity matrix, an orthographic projection and the viewport
 * of a 640 x 480 EFB, the TEV (modulate with the texture / the colour alone), an 8 x 8 RGB565
 * checker texture; a textured quad and a shaded triangle; then the copy of the EFB to the XFB.
 */
typedef unsigned int u32; typedef unsigned short u16; typedef unsigned char u8;
#define R32(a) (*(volatile u32 *) (a))
#define R16(a) (*(volatile u16 *) (a))
#define R8(a) (*(volatile u8 *) (a))
#define WGP 0xCC008000

__asm__ (".globl _start\n_start:\n	lis 1, 0x8170\n	bl main\n1:	b 1b\n");

static void u8w (u32 v) { R8 (WGP) = (u8) v; }
static void u32w (u32 v) { R32 (WGP) = v; }
static void f32w (float f) { union { float f; u32 u; } x; x.f = f; R32 (WGP) = x.u; }
static void cp (u32 reg, u32 v) { u8w (0x08); u8w (reg); u32w (v); }
static void bp (u32 v) { u8w (0x61); u32w (v); }
static void xf (u32 addr, int n, const u32 *v) { u8w (0x10); u32w ((u32) (n - 1) << 16 | addr); for (int i = 0; i < n; i++) u32w (v[i]); }
static u32 fu (float f) { union { float f; u32 u; } x; x.f = f; return x.u; }

int main (void)
{
	/* the GX FIFO at 0x00600000, linked */
	R32 (0xCC00300C) = 0x00600000; R32 (0xCC003010) = 0x00680000; R32 (0xCC003014) = 0x00600000;
	R16 (0xCC000020) = 0; R16 (0xCC000022) = 0x0060; R16 (0xCC000024) = 0; R16 (0xCC000026) = 0x0068;
	R16 (0xCC000038) = 0; R16 (0xCC00003A) = 0x0060;
	R16 (0xCC000002) = 0x11;
	/* the texture: 8 x 8 RGB565, 2 x 2 tiles of 4 x 4 (a red / white checker of 2 x 2 squares) */
	u16 *tx = (u16 *) 0x80500000;
	for (int t = 0; t < 4; t++)
		for (int y = 0; y < 4; y++)
			for (int x = 0; x < 4; x++)
			{
				int gx = (t & 1) * 4 + x, gy = (t >> 1) * 4 + y;
				tx[t * 16 + y * 4 + x] = ((gx >> 1) + (gy >> 1)) & 1 ? 0xF800 : 0xFFFF;
			}
	/* the vertex format: position direct, colour 0 direct, texture 0 direct; VAT 0: xyz f32, RGBA8, st f32 */
	cp (0x50, (1 << 9) | (1 << 13));
	cp (0x60, 1);
	cp (0x70, 1 | (4 << 1) | (1 << 13) | (5 << 14) | (1 << 21) | (4 << 22));
	/* the XF: identity position matrix 0, orthographic 640 x 480 (y down), the viewport */
	static const float id[12] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
	u32 m[12]; for (int i = 0; i < 12; i++) m[i] = fu (id[i]);
	xf (0x000, 12, m);
	u32 pr[7] = { fu (2.f / 640), fu (-1.f), fu (-2.f / 480), fu (1.f), fu (0.f), fu (-0.5f), 1 };
	xf (0x1020, 7, pr);
	u32 vp[6] = { fu (320.f), fu (-240.f), fu (16777215.f), fu (320.f + 342), fu (240.f + 342), fu (16777215.f) };
	xf (0x101A, 6, vp);
	u32 one = 1; xf (0x1009, 1, &one);			/* one colour channel */
	u32 cc = 1; xf (0x100E, 1, &cc); xf (0x1010, 1, &cc);	/* its colour, alpha: the vertex's, unlit */
	xf (0x103F, 1, &one);					/* one texgen */
	u32 tg = (5 << 7); xf (0x1040, 1, &tg);			/* texgen 0: the texture coordinate 0, a matrix (0: identity) */
	u32 mi = 0; xf (0x1018, 1, &mi);
	/* the BP: one texgen, one channel, one TEV stage; no depth, no blending, the alpha compare passes */
	bp (0x00000000 | 1 | (1 << 4));
	bp (0x40000000);
	bp (0x41000000);
	bp (0xF3000000 | (7 << 16) | (7 << 19));
	/* the texture map 0: 8 x 8 RGB565 at 0x00500000, repeat */
	bp (0x88000000 | 7 | (7 << 10) | (4 << 20));
	bp (0x94000000 | (0x00500000 >> 5));
	bp (0x80000000 | 1 | (1 << 2));
	/* stage 0: texture 0 x the colour (modulate) */
	bp (0x28000000 | 0x40);
	bp (0xC0000000 | 15 | (10 << 4) | (8 << 8) | (15 << 12) | (1 << 19));
	bp (0xC1000000 | (7 << 4) | (5 << 7) | (4 << 10) | (7 << 13) | (1 << 19));
	/* a quad, 100..300 x 100..300, the checker repeated 4 times */
	u8w (0x80); u8w (0); u8w (4);
	static const float q[4][4] = { { 100, 100, 0, 0 }, { 300, 100, 4, 0 }, { 300, 300, 4, 4 }, { 100, 300, 0, 4 } };
	for (int i = 0; i < 4; i++) { f32w (q[i][0]); f32w (q[i][1]); f32w (0); u32w (0xFFFFFFFF); f32w (q[i][2]); f32w (q[i][3]); }
	/* stage 0 without texture: the colour alone; a triangle red / green / blue */
	bp (0x28000000);
	bp (0xC0000000 | 10 | (15 << 4) | (15 << 8) | (15 << 12) | (1 << 19));
	bp (0xC1000000 | (5 << 4) | (7 << 7) | (7 << 10) | (7 << 13) | (1 << 19));
	u8w (0x90); u8w (0); u8w (3);
	static const float t[3][2] = { { 450, 100 }, { 600, 380 }, { 350, 380 } };
	static const u32 col[3] = { 0xFF0000FF, 0x00FF00FF, 0x0000FFFF };
	for (int i = 0; i < 3; i++) { f32w (t[i][0]); f32w (t[i][1]); f32w (0); u32w (col[i]); f32w (0); f32w (0); }
	/* the copy to the XFB: 640 x 480, cleared after to dark blue */
	bp (0x49000000); bp (0x4A000000 | 639 | (479 << 10));
	bp (0x4B000000 | (0x00400000 >> 5)); bp (0x4D000000 | (1280 >> 5));
	bp (0x4F000000 | 0x20); bp (0x50000000 | 0x0060);
	bp (0x52000000 | (1 << 14) | (1 << 11));
	bp (0x45000002);					/* draw done */
	for (int i = 0; i < 32; i++) u8w (0);
	R32 (0x80700000) = 0x600D600D;
	for (;;) ;
}
