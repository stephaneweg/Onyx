// gfx2d.9.c -- the 2D engines' picture test (looked at, tools/tests/nds/README). Engine A (top, mode 5):
// BG0 16-colour tiles (framed squares, top left), BG1 256-colour tiles through the extended palette slot 1
// (a gradient bar), BG2 a direct-colour bitmap (red / green ramps, top right), BG3 a 256-colour bitmap
// rotated 30 degrees (rings, bottom), sprites: four 16x16 (16 colours, 1D), one 32x32 256-colour with the OBJ
// extended palette, one affine double-size, one semi-transparent, one bitmap sprite; window 0 (x 8-248,
// y 4-188) hides BG3 outside; BG0 alpha-blended over BG2 (8/16 + 8/16). Engine B (bottom, mode 0): its palette
// as 16 x 16 blocks, two sprites, the master brightness down by 4/16.
#include "hw.h"

static const s16 SIN30 = 2048, COS30 = 3547;		// 1.12 fixed point

static void fillTile4 (volatile u16 *t, int frame, int fill)	// 8x8, 4 bits: a frame of colour 'frame' around 'fill'
{
	for (int y = 0; y < 8; y++)
		for (int x = 0; x < 8; x += 4)
		{
			u16 v = 0;
			for (int k = 0; k < 4; k++) { int c = (y == 0 || y == 7 || x + k == 0 || x + k == 7) ? frame : fill; v |= (u16) (c << (k * 4)); }
			t[y * 2 + x / 4] = v;
		}
}

int main (void)
{
	POWCNT1 = 0x820F;					// both engines, A on top
	VRAMCNT (0) = 0x81;					// A: engine A BG
	VRAMCNT (1) = 0x82;					// B: engine A OBJ
	VRAMCNT (2) = 0x84;					// C: engine B BG
	VRAMCNT (3) = 0x84;					// D: engine B OBJ
	VRAMCNT (4) = 0x84;					// E: engine A BG extended palettes
	VRAMCNT (5) = 0x85;					// F: engine A OBJ extended palettes
	// palettes
	PAL[0] = RGB (4, 4, 8);					// the backdrop
	PAL[1] = RGB (31, 31, 31); PAL[2] = RGB (31, 0, 0); PAL[3] = RGB (0, 31, 0); PAL[4] = RGB (0, 0, 31);
	for (int i = 1; i < 256; i++) PAL[i + 0] = i < 5 ? PAL[i] : RGB (i & 31, (i >> 3) & 31, 31 - (i & 31));
	for (int i = 0; i < 16; i++) PAL[256 + 16 + i] = RGB (i * 2, 31 - i * 2, 16);	// OBJ palette 1
	for (int i = 0; i < 16; i++) PAL[256 + 32 + i] = RGB (31, i * 2, 0);		// OBJ palette 2
	volatile u16 *bgExt = (volatile u16 *) 0x06880000;	// (E in LCDC to write it first)
	VRAMCNT (4) = 0x80;
	for (int i = 0; i < 256; i++) bgExt[0x1000 + 256 * 3 + i] = RGB (i >> 3, 31 - (i >> 3), 31);	// slot 1, palette 3
	VRAMCNT (4) = 0x84;
	volatile u16 *objExt = (volatile u16 *) 0x06890000;
	VRAMCNT (5) = 0x80;
	for (int i = 0; i < 256; i++) objExt[256 * 2 + i] = RGB (31 - (i >> 3), i >> 3, (i >> 2) & 31);	// palette 2
	VRAMCNT (5) = 0x85;
	// BG0: 16 colours, char base 0, map at 0xF800
	fillTile4 (BG_VRAM + 16, 1, 2);				// tile 1
	fillTile4 (BG_VRAM + 32, 3, 0);				// tile 2: transparent inside
	volatile u16 *map0 = BG_VRAM + 0xF800 / 2;
	for (int i = 0; i < 32 * 32; i++) map0[i] = 0;
	for (int y = 1; y < 6; y++) for (int x = 1; x < 10; x++) map0[y * 32 + x] = (u16) (((x + y) & 1) ? 1 : 2 | 0x400);
	BGCNT (0) = (u16) (0 | (0 << 2) | (31 << 8));
	// BG1: 256 colours, char base 1 (0x4000), map 0xF000, extended palette slot 1 (BG1), palette 3
	volatile u16 *t8 = BG_VRAM + 0x4000 / 2;
	for (int y = 0; y < 8; y++) for (int x = 0; x < 8; x += 2) t8[64 / 2 + y * 4 + x / 2] = (u16) ((x * 32 + y * 4 + 1) | ((x * 32 + 32 + y * 4 + 1) << 8));
	volatile u16 *map1 = BG_VRAM + 0xF000 / 2;
	for (int i = 0; i < 32 * 32; i++) map1[i] = 0;
	for (int x = 1; x < 16; x++) map1[8 * 32 + x] = (u16) (1 | (3 << 12));
	BGCNT (1) = (u16) (1 | (1 << 2) | 0x80 | (30 << 8));
	// BG2: direct colour 128x128 at 0x10000 (base 4), shown at x 128
	volatile u16 *bmp = BG_VRAM + 0x10000 / 2;
	for (int y = 0; y < 128; y++) for (int x = 0; x < 128; x++) bmp[y * 128 + x] = (u16) (0x8000 | RGB (x >> 2, y >> 2, 8));
	BGCNT (2) = (u16) (2 | 0x80 | 4 | (4 << 8));
	R16 (0x04000020) = 256; R16 (0x04000022) = 0; R16 (0x04000024) = 0; R16 (0x04000026) = 256;
	R32 (0x04000028) = (u32) (-128 << 8); R32 (0x0400002C) = 0;
	// BG3: 256-colour bitmap 128x128 at 0x18000 (base 6), rings, rotated 30 degrees about its centre
	volatile u16 *b8 = BG_VRAM + 0x18000 / 2;
	for (int y = 0; y < 128; y++) for (int x = 0; x < 128; x += 2)
	{
		int d0 = (x - 64) * (x - 64) + (y - 64) * (y - 64), d1 = (x + 1 - 64) * (x + 1 - 64) + (y - 64) * (y - 64);
		int c0 = (d0 / 64) & 1 ? 2 : 4, c1 = (d1 / 64) & 1 ? 2 : 4;
		if (d0 > 64 * 64) c0 = 0; if (d1 > 64 * 64) c1 = 0;
		b8[y * 64 + x / 2] = (u16) (c0 | (c1 << 8));
	}
	BGCNT (3) = (u16) (3 | 0x80 | (6 << 8));
	s16 pa = (s16) (COS30 >> 4), pb = (s16) (-SIN30 >> 4), pc = (s16) (SIN30 >> 4), pd = (s16) (COS30 >> 4);
	R16 (0x04000030) = (u16) pa; R16 (0x04000032) = (u16) pb; R16 (0x04000034) = (u16) pc; R16 (0x04000036) = (u16) pd;
	// the screen point (64, 128) shows the bitmap's centre: ref = c - (pa * 64 + pb * 128, pc * 64 + pd * 128)
	R32 (0x04000038) = (u32) ((64 << 8) - (pa * 64 + pb * 128)); R32 (0x0400003C) = (u32) ((64 << 8) - (pc * 64 + pd * 128));
	// sprites (engine A): tiles at 0x06400000, 1D, 32-byte steps
	for (int t = 0; t < 4; t++) fillTile4 (OBJ_VRAM + (1 + t) * 16, 1 + t, 4 + t);
	volatile u8 *o8 = (volatile u8 *) OBJ_VRAM;
	for (int i = 0; i < 32 * 32; i++) ((volatile u16 *) o8)[(5 * 32 + i) / 2] = 0;
	for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x += 2)
	{
		int tile = (y / 8) * 4 + x / 8, off = 5 * 32 + tile * 64 + (y & 7) * 8 + (x & 7);
		int c = ((x / 4) + (y / 4)) & 1 ? 40 + y * 4 : 200 - x * 3;
		if ((x - 16) * (x - 16) + (y - 16) * (y - 16) > 15 * 15) c = 0;
		((volatile u16 *) OBJ_VRAM)[off / 2] = (u16) (c | (c << 8));
	}
	// a bitmap sprite 16x16 (1D, 128-byte steps): at tile 16 -> 16 * 128 bytes
	volatile u16 *bs = OBJ_VRAM + 16 * 128 / 2;
	for (int y = 0; y < 16; y++) for (int x = 0; x < 16; x++) bs[y * 16 + x] = (u16) (0x8000 | RGB (x * 2, 31, y * 2));
	for (int i = 0; i < 128; i++) { OAM[i * 4] = 0x200; OAM[i * 4 + 1] = 0; OAM[i * 4 + 2] = 0; }
	for (int s = 0; s < 4; s++)					// 16x16, 16 colours, palette 1
	{
		OAM[s * 4 + 0] = (u16) (150 | (0 << 14));
		OAM[s * 4 + 1] = (u16) ((8 + s * 20) | (1 << 14));
		OAM[s * 4 + 2] = (u16) ((1 + s) | (1 << 12));
	}
	OAM[4 * 4 + 0] = (u16) (140 | 0x2000);			// 32x32, 256 colours (extended palette 2)
	OAM[4 * 4 + 1] = (u16) (100 | (2 << 14));
	OAM[4 * 4 + 2] = (u16) (5 | (2 << 12));
	OAM[5 * 4 + 0] = (u16) (130 | 0x100 | 0x200);		// affine, double size 16x16 -> 32x32
	OAM[5 * 4 + 1] = (u16) (150 | (1 << 14) | (0 << 9));
	OAM[5 * 4 + 2] = (u16) (2 | (2 << 12));
	OAM[3] = (u16) (COS30 >> 4); OAM[7] = (u16) (-SIN30 >> 4); OAM[11] = (u16) (SIN30 >> 4); OAM[15] = (u16) (COS30 >> 4);
	OAM[6 * 4 + 0] = (u16) (100 | (1 << 10));		// semi-transparent
	OAM[6 * 4 + 1] = (u16) (130 | (1 << 14));
	OAM[6 * 4 + 2] = (u16) (3 | (1 << 12));
	OAM[7 * 4 + 0] = (u16) (100 | (3 << 10));		// bitmap sprite, alpha 15
	OAM[7 * 4 + 1] = (u16) (200 | (1 << 14));
	OAM[7 * 4 + 2] = (u16) (16 | (15 << 12));
	// window 0 and the blending
	R16 (0x04000040) = (u16) ((8 << 8) | 248); R16 (0x04000044) = (u16) ((4 << 8) | 188);
	R16 (0x04000048) = 0x3F;				// inside: everything
	R16 (0x0400004A) = 0x37;				// outside: no BG3
	R16 (0x04000050) = (u16) (1 | (1 << 6) | (4 << 8) | (32 << 8));	// BG0 over BG2 / backdrop: alpha
	R16 (0x04000052) = (u16) (8 | (8 << 8));
	DISPCNT = 5 | 0x0F00 | 0x1000 | 0x2000 | 0x10 | 0x40 | (1 << 16) | (1u << 30) | (1u << 31);
	// engine B: its palette as blocks, two sprites, darker
	volatile u16 *bb = BG_VRAM_B;
	for (int i = 0; i < 256; i++) PAL_B[i] = (u16) (i ? RGB (i & 31, (i * 3) & 31, (i >> 3) & 31) : RGB (2, 2, 2));
	for (int t = 0; t < 16; t++) for (int k = 0; k < 16; k++) bb[16 + t * 16 + k] = (u16) ((t & 15) * 0x1111);
	volatile u16 *mb = BG_VRAM_B + 0x800 / 2;
	for (int y = 0; y < 24; y++) for (int x = 0; x < 32; x++) mb[y * 32 + x] = (u16) ((1 + (x & 15)) | ((y & 15) << 12));
	BGCNT_B (0) = (u16) (0 | (0 << 2) | (1 << 8));
	volatile u16 *ob = (volatile u16 *) 0x06600000;
	fillTile4 (ob + 16, 1, 2);
	for (int i = 0; i < 16; i++) PAL_B[256 + i] = RGB (31, 31 - i, i);
	volatile u16 *oamB = (volatile u16 *) 0x07000400;
	for (int i = 0; i < 128; i++) oamB[i * 4] = 0x200;
	oamB[0] = 80; oamB[1] = 120; oamB[2] = 1;
	oamB[4] = 90; oamB[5] = 140 | 0x1000; oamB[6] = 1;
	DISPCNT_B = 0 | 0x0100 | 0x1000 | 0x10 | (1 << 16);
	R16 (0x0400106C) = (u16) ((2 << 14) | 4);
	RESULTS[0] = DONE;
	for (;;) asm volatile ("mcr p15, 0, r0, c7, c0, 4");
	return 0;
}
