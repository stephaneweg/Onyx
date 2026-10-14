// seams.9.c -- full-screen fades drawn as 3D polygons (as games do for their transitions): the screen cleared
// white, then black polygons covering it -- every pixel must be covered exactly once (no seam left white, no
// pixel blended twice along a shared edge). One variant a frame, in turn (the frame number mod 8):
//   0 two translucent triangles sharing the diagonal from top left to bottom right, corners on the screen's edge
//   1 the same, opaque
//   2 two translucent triangles sharing the other diagonal
//   3 two translucent triangles larger than the screen, wider than high (clipped: the diagonal cut too)
//   4 one translucent quad
//   5 a translucent triangle fan of 4 around the centre
//   6 two translucent triangles, a different polygon ID each
//   7 two translucent triangles larger than the screen, higher than wide (clipped)
//   8 a perspective projection: two translucent triangles larger than the view at a depth (clipped, w != 1)
//   9 the same, w-buffered
//  10 a perspective projection: two translucent triangles tilted (their depth varies), larger than the view
//  11 orthographic, two opaque triangles on the far plane (their depth = the clear's)
#include "hw.h"
#define GX(c)		R32 (0x04000400 + (c) * 4)
#define F(x)		((s32) ((x) * 4096))

static s32 g_z;
static void vtx (s32 x, s32 y) { GX (0x23) = (u32) ((x & 0xFFFF) | ((u32) y << 16)); GX (0x23) = (u32) (g_z & 0xFFFF); }
static void poly (int alpha, int id) { GX (0x29) = ((u32) alpha << 16) | 0xC0 | ((u32) id << 24); }

static void scene (int v)
{
	GX (0x10) = 0; GX (0x15) = 0;				// identity projection
	if (v >= 8 && v <= 10)
	{
		static const s32 proj[16] = { 5321, 0, 0, 0, 0, 7094, 0, 0, 0, 0, -4200, -4096, 0, 0, -4148, 0 };
		for (int i = 0; i < 16; i++) GX (0x16) = (u32) proj[i];
	}
	GX (0x10) = 2; GX (0x15) = 0;				// identity position / vector
	GX (0x60) = 0 | (0 << 8) | (255u << 16) | (191u << 24);
	GX (0x2A) = 0;
	GX (0x20) = 0;						// black
	g_z = v == 11 ? -F (1.0) : 0;
	if (v >= 8 && v <= 10) { GX (0x10) = 2; GX (0x1C) = 0; GX (0x1C) = 0; GX (0x1C) = (u32) F (-3); }
	if (v == 10) { static const s32 rx[9] = { 4096, 0, 0, 0, 3547, 2048, 0, -2048, 3547 }; for (int i = 0; i < 9; i++) GX (0x1A) = (u32) rx[i]; }
	s32 e = F (1.0), ex = v == 3 ? F (1.6) : v == 7 ? F (1.3) : v >= 8 && v <= 10 ? F (3.1) : e, ey = v == 3 ? F (1.2) : v == 7 ? F (1.9) : v >= 8 && v <= 10 ? F (2.7) : e;
	int a = v == 1 || v == 11 ? 31 : 16;
	switch (v)
	{
	case 0: case 1: case 3: case 6: case 7: case 8: case 9: case 10: case 11:
		poly (a, 1); GX (0x40) = 0;
		vtx (-ex, ey); vtx (-ex, -ey); vtx (ex, -ey);	// bottom left half
		GX (0x41) = 0;
		if (v == 6) poly (a, 2);
		GX (0x40) = 0;
		vtx (-ex, ey); vtx (ex, -ey); vtx (ex, ey);	// top right half
		GX (0x41) = 0;
		break;
	case 2:
		poly (a, 1); GX (0x40) = 0;
		vtx (-e, e); vtx (-e, -e); vtx (e, e);
		vtx (-e, -e); vtx (e, -e); vtx (e, e);
		GX (0x41) = 0;
		break;
	case 4:
		poly (a, 1); GX (0x40) = 1;
		vtx (-e, e); vtx (-e, -e); vtx (e, -e); vtx (e, e);
		GX (0x41) = 0;
		break;
	case 5:
		poly (a, 1); GX (0x40) = 0;
		vtx (0, 0); vtx (-e, e); vtx (-e, -e);
		vtx (0, 0); vtx (-e, -e); vtx (e, -e);
		vtx (0, 0); vtx (e, -e); vtx (e, e);
		vtx (0, 0); vtx (e, e); vtx (-e, e);
		GX (0x41) = 0;
		break;
	}
	GX (0x12) = 1;
}

int main (void)
{
	POWCNT1 = 0x820F;
	DISPCNT = 0 | 8 | 0x0100 | (1 << 16);			// engine A: BG0 = 3D
	R16 (0x04000008) = 0;
	R16 (0x04000060) = 8;					// blending
	R16 (0x04000340) = 0;
	R32 (0x04000350) = RGB (31, 31, 31) | (31 << 16) | (63u << 24);	// the clear: white, opaque, ID 63
	R16 (0x04000354) = 0x7FFF;
	RESULTS[0] = DONE;
	for (int f = 0; ; f++)
	{
		scene ((f >> 3) % 12);					// (each variant 8 frames: the picture shown 2 frames later)
		GX (0x50) = (f >> 3) % 12 == 9 ? 2 : 0;
		while (VCOUNT != 192) ;
		while (VCOUNT == 192) ;
	}
	return 0;
}
