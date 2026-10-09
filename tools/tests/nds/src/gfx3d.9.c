// gfx3d.9.c -- the 3D engine's picture test (top screen, BG0 = 3D): a perspective projection; a Gouraud
// triangle (red, green, blue corners, left); a textured quad tilted back (a 32x32 16-colour checker, right);
// a lit quad (one white light from the left: its left edge brighter, bottom left); a translucent white quad
// over the middle (alpha 12/31); a quad strip ribbon (bottom). The clear colour is dark blue; edge marking on.
// The bottom screen shows the 3D layer captured (display capture into bank C, shown by engine B as a bitmap).
#include "hw.h"
#define GX(c)		R32 (0x04000400 + (c) * 4)
#define F(x)		((s32) ((x) * 4096))

static void mtxLoad (const s32 *m) { for (int i = 0; i < 16; i++) GX (0x16) = (u32) m[i]; }
static void vtx (s32 x, s32 y, s32 z) { GX (0x23) = (u32) ((x & 0xFFFF) | (y << 16)); GX (0x23) = (u32) (z & 0xFFFF); }
static void color (int r, int g, int b) { GX (0x20) = RGB (r, g, b); }
static void tex (int s, int t) { GX (0x22) = (u32) ((s * 16) & 0xFFFF) | ((u32) (t * 16) << 16); }

static void scene (void)
{
	// projection
	GX (0x10) = 0;
	static const s32 proj[16] = { 5321, 0, 0, 0, 0, 7094, 0, 0, 0, 0, -4200, -4096, 0, 0, -4148, 0 };
	mtxLoad (proj);
	GX (0x10) = 2; GX (0x15) = 0;
	GX (0x60) = 0 | (0 << 8) | (255 << 16) | (191u << 24);	// the viewport
	// the Gouraud triangle
	GX (0x11) = 0;
	GX (0x1C) = (u32) F (-1.2); GX (0x1C) = (u32) F (0.3); GX (0x1C) = (u32) F (-4);
	GX (0x29) = (31 << 16) | 0xC0 | (1u << 24);		// opaque, both sides, ID 1
	GX (0x2A) = 0;
	GX (0x40) = 0;
	color (31, 0, 0); vtx (F (0), F (0.8), 0);
	color (0, 31, 0); vtx (F (-0.8), F (-0.6), 0);
	color (0, 0, 31); vtx (F (0.8), F (-0.6), 0);
	GX (0x41) = 0;
	GX (0x12) = 1;
	// the textured quad, tilted back 60 degrees about x
	GX (0x11) = 0;
	GX (0x1C) = (u32) F (1.2); GX (0x1C) = (u32) F (0.4); GX (0x1C) = (u32) F (-4);
	static const s32 rot[9] = { 4096, 0, 0, 0, 2048, 3547, 0, -3547, 2048 };
	for (int i = 0; i < 9; i++) GX (0x1A) = (u32) rot[i];
	GX (0x29) = (31 << 16) | 0xC0 | (2u << 24);
	GX (0x2A) = 0 | (2 << 20) | (2 << 23) | (3u << 26) | (1 << 16) | (1 << 17);	// 32x32, 16 colours, repeat
	GX (0x2B) = 0;
	color (31, 31, 31);
	GX (0x40) = 1;
	tex (0, 0); vtx (F (-0.8), F (0.8), 0);
	tex (0, 64); vtx (F (-0.8), F (-0.8), 0);
	tex (64, 64); vtx (F (0.8), F (-0.8), 0);
	tex (64, 0); vtx (F (0.8), F (0.8), 0);
	GX (0x41) = 0;
	GX (0x12) = 1;
	// the lit quad: light 0 pointing right (+x), white; the quad faces the camera rotated 45 about y
	GX (0x11) = 0;
	GX (0x32) = 0x201;					// light 0: (-0.998, 0, 0), by the camera's matrix
	GX (0x1C) = (u32) F (-1.4); GX (0x1C) = (u32) F (-1.1); GX (0x1C) = (u32) F (-4);
	static const s32 roty[9] = { 2896, 0, -2896, 0, 4096, 0, 2896, 0, 2896 };
	for (int i = 0; i < 9; i++) GX (0x1A) = (u32) roty[i];
	GX (0x33) = RGB (31, 31, 31);
	GX (0x30) = RGB (28, 28, 28) | (RGB (4, 4, 4) << 16);	// diffuse, ambient
	GX (0x31) = 0;
	GX (0x29) = 1 | (31 << 16) | 0xC0 | (3u << 24);
	GX (0x2A) = 0;
	GX (0x40) = 1;
	GX (0x21) = (u32) (0 | (0 << 10) | ((511 & 0x3FF) << 20));	// the normal (0, 0, +1)
	vtx (F (-0.5), F (0.5), 0); vtx (F (-0.5), F (-0.5), 0); vtx (F (0.5), F (-0.5), 0); vtx (F (0.5), F (0.5), 0);
	GX (0x41) = 0;
	GX (0x12) = 1;
	// the quad strip ribbon
	GX (0x11) = 0;
	GX (0x1C) = (u32) F (0.6); GX (0x1C) = (u32) F (-1.3); GX (0x1C) = (u32) F (-4);
	GX (0x29) = (31 << 16) | 0xC0 | (4u << 24);
	GX (0x40) = 3;
	for (int i = 0; i <= 6; i++)
	{
		color (i * 5, 31 - i * 5, 16);
		vtx (F (-1.0) + i * F (0.33), F (0.25) + (i & 1) * F (0.1), 0);
		vtx (F (-1.0) + i * F (0.33), F (-0.25) + (i & 1) * F (0.1), 0);
	}
	GX (0x41) = 0;
	GX (0x12) = 1;
	// the translucent quad in front
	GX (0x11) = 0;
	GX (0x1C) = 0; GX (0x1C) = (u32) F (-0.2); GX (0x1C) = (u32) F (-3);
	GX (0x29) = (12 << 16) | 0xC0 | (5u << 24);
	color (31, 31, 31);
	GX (0x40) = 1;
	vtx (F (-0.4), F (0.4), 0); vtx (F (-0.4), F (-0.4), 0); vtx (F (0.4), F (-0.4), 0); vtx (F (0.4), F (0.4), 0);
	GX (0x41) = 0;
	GX (0x12) = 1;
}

int main (void)
{
	POWCNT1 = 0x820F;
	VRAMCNT (0) = 0x83;					// A: texture slot 0
	VRAMCNT (4) = 0x80;					// E: LCDC, to write the palette, then texture palette
	VRAMCNT (2) = 0x80;					// C: LCDC (the capture's destination)
	// the texture: 32x32, 16 colours, a checker of 8x8 cells (colours 1 / 2)
	VRAMCNT (0) = 0x80;
	volatile u16 *t = (volatile u16 *) 0x06800000;
	for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x += 4)
	{
		u16 v = 0;
		for (int k = 0; k < 4; k++) v |= (u16) (((((x + k) >> 3) ^ (y >> 3)) & 1 ? 1 : 2) << (k * 4));
		t[(y * 32 + x) / 4] = v;
	}
	VRAMCNT (0) = 0x83;
	volatile u16 *tp = (volatile u16 *) 0x06880000;
	tp[1] = RGB (31, 31, 0); tp[2] = RGB (31, 0, 31);
	VRAMCNT (4) = 0x83;
	// the engines: A shows BG0 = 3D; B shows bank C (captured) as a 16-bit bitmap, BG2 extended
	DISPCNT = 0 | 8 | 0x0100 | (1 << 16);
	R16 (0x04000008) = 0;					// BG0 priority 0
	R16 (0x04000060) = 1 | 8 | 0x20;			// textures, blending, edge marking
	R16 (0x04000340) = 0;
	R32 (0x04000350) = RGB (2, 2, 10) | (31 << 16) | (63u << 24);	// the clear: colour, alpha 31, polygon ID 63
	R16 (0x04000354) = 0x7FFF;
	for (int i = 0; i < 8; i++) R16 (0x04000330 + i * 2) = RGB (31, 31, 31);
	scene ();
	GX (0x50) = 0;						// SWAP_BUFFERS
	// engine B: bank C as its BG (a 256x192 direct-colour bitmap), filled by the capture
	RESULTS[0] = DONE;
	for (int f = 0; ; f++)
	{
		while (VCOUNT != 192) ;
		while (VCOUNT == 192) ;
		if (f == 3)
		{
			R32 (0x04000064) = (u32) (0x80000000u | (2 << 16) | (3 << 20) | (1u << 24));	// capture the 3D into C
		}
		if (f == 5)
		{
			VRAMCNT (2) = 0x84;					// C: engine B BG
			R16 (0x0400100C) = (u16) (0x80 | 4 | (0 << 8) | (1 << 14));	// BG2: direct bitmap 256x256 at 0
			R16 (0x04001020) = 256; R16 (0x04001026) = 256;
			DISPCNT_B = 5 | 0x0400 | (1 << 16);
		}
		scene ();
		GX (0x50) = 0;
	}
	return 0;
}
