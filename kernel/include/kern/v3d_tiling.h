//
// kern/v3d_tiling.h -- where a texel of a V3D 4.2 texture (32 bits a texel, level 0) lives in
// memory: the layout the texture unit expects for that size (Mesa's v3d_setup_slices and
// v3d_tiling.c, MIT, rewritten): LT, UBLINEAR (1 / 2 columns) or UIF (+ padding, XOR).
// Pure code (sys/v3d.cpp and the host test tools/tests/v3d/tiling_test.cpp).
//
#ifndef _kern_v3d_tiling_h
#define _kern_v3d_tiling_h

enum { TL_LT, TL_UB1, TL_UB2, TL_UIF };

static u32 UbPad (u32 nHeightUb)		// (Mesa v3d_get_ub_pad, 32 bpp)
{
	const u32 PC_ROWS = 32, ROWS_1_5 = 6, PC_MINUS_1_5 = PC_ROWS - ROWS_1_5;
	u32 nOff = nHeightUb % PC_ROWS;
	if (nOff == 0) return 0;
	if (nOff < ROWS_1_5) return nHeightUb < PC_ROWS ? 0 : ROWS_1_5 - nOff;
	if (nOff > PC_MINUS_1_5) return PC_ROWS - nOff;
	return 0;
}

struct TLayout { int nKind; u32 nPadW, nPadH, nUbPad; bool bXor; };

static void Layout (u32 w, u32 h, TLayout &L)
{
	L.nUbPad = 0; L.bXor = false;
	if (w <= 4 || h <= 4) { L.nKind = TL_LT; L.nPadW = (w + 3) & ~3u; L.nPadH = (h + 3) & ~3u; }
	else if (w <= 8) { L.nKind = TL_UB1; L.nPadW = 8; L.nPadH = (h + 7) & ~7u; }
	else if (w <= 16) { L.nKind = TL_UB2; L.nPadW = 16; L.nPadH = (h + 7) & ~7u; }
	else
	{
		L.nKind = TL_UIF; L.nPadW = (w + 31) & ~31u; L.nPadH = (h + 7) & ~7u;
		L.nUbPad = UbPad (L.nPadH / 8);
		L.nPadH += L.nUbPad * 8;
		L.bXor = (L.nPadH / 8) % 32 == 0;
	}
}

static inline u32 TexelOffset (const TLayout &L, u32 x, u32 y)	// bytes
{
	u32 nIn = (x & 3) * 4 + (y & 3) * 16;			// inside the 4 x 4 utile (64 bytes)
	switch (L.nKind)
	{
	case TL_LT:
		return 64 * (x / 4 + y / 4) + nIn;
	case TL_UB1:
	case TL_UB2:
		return 256 * ((y / 8) * (L.nKind == TL_UB1 ? 1 : 2) + x / 8) + ((x & 4) ? 64 : 0) + ((y & 4) ? 128 : 0) + nIn;
	default:
	{
		u32 mbX = x / 8, mbY = y / 8, mbH = L.nPadH / 8;
		if (L.bXor && ((mbX / 4) & 1)) mbY ^= 0x10;
		u32 nId = (mbX / 4) * (mbH * 4) + (mbX & 3) + mbY * 4;
		return nId * 256 + ((y & 4) ? 128 : 0) + ((x & 4) ? 64 : 0) + nIn;
	}
	}
}

// (v70 gpu_texture_rect) The pixels (0xAARRGGBB, nStride pixels a row) of the rectangle x0, y0,
// w x h stored into the texels pT of a texture of layout L (R G B A in memory): a utile row -- 4
// texels, 16 contiguous bytes in every layout -- per offset computed. *pLo / *pHi: the lowest byte
// written and the highest + 1 (the span to clean from the CPU caches).
static void StoreRect (const TLayout &L, unsigned char *pT, u32 x0, u32 y0, u32 w, u32 h,
		       const unsigned *pPx, long nStride, u32 *pLo, u32 *pHi)
{
	u32 nLo = ~0u, nHi = 0;
	for (u32 y = 0; y < h; y++)
	{
		const unsigned *src = pPx + (long) y * nStride;
		for (u32 x = 0; x < w; )
		{
			u32 tx = x0 + x;
			u32 nOff = TexelOffset (L, tx & ~3u, y0 + y);		// (the utile row's first texel)
			u32 *d = (u32 *) (pT + nOff);
			if (nOff < nLo) nLo = nOff;
			if (nOff + 16 > nHi) nHi = nOff + 16;
			for (u32 k = tx & 3; k < 4 && x < w; k++, x++)
			{
				u32 c = src[x];
				d[k] = (c & 0xFF00FF00) | ((c >> 16) & 0xFF) | ((c & 0xFF) << 16);
			}
		}
	}
	*pLo = nLo; *pHi = nHi;
}

#endif
