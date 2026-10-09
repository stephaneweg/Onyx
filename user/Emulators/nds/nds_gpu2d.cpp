//
// nds/nds_gpu2d.cpp -- the two 2D engines (A: up to 512 KB of BG and 256 KB of OBJ VRAM, the 3D
// layer as BG0, the large bitmap; B: 128 KB each), a line at a time: the text, affine, extended
// (16-bit map, 256-colour and direct-colour bitmaps) and large BGs, the extended palettes, the
// sprites (tiles 1D / 2D, 16 / 256 colours, affine, bitmap sprites), the windows, the blending
// (with the 3D layer's and the bitmap sprites' own alpha), the mosaic, the master brightness; the
// display modes (VRAM display, main memory) and the display capture (into an LCDC bank).
// Colours are composed with 6 bits a channel (the 3D engine's), packed r | g << 6 | b << 12.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (see nds.h).
//
#include "nds/nds.h"

namespace nds {

enum : u32 { TRANSP = 0x80000000u, IS3D = 0x40000000u, OBJ_SEMI = 0x20000000u, OBJ_BMP = 0x10000000u };

static inline u32 c15to18 (u32 c)
{
	u32 r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
	r = (r << 1) | (r >> 4); g = (g << 1) | (g >> 4); b = (b << 1) | (b >> 4);
	return r | (g << 6) | (b << 12);
}
static inline u32 c18to24 (u32 c)
{
	u32 r = c & 63, g = (c >> 6) & 63, b = (c >> 12) & 63;
	r = (r << 2) | (r >> 4); g = (g << 2) | (g >> 4); b = (b << 2) | (b >> 4);
	return (r << 16) | (g << 8) | b;
}
static inline u32 c18to15 (u32 c) { return ((c & 63) >> 1) | (((c >> 7) & 31) << 5) | (((c >> 13) & 31) << 10); }

void Gpu2D::reset (Machine *mm, int n)
{
	m = mm; num = n;
	for (int i = 0; i < 0x70; i++) reg[i] = 0;
	dispcnt = 0;
	affX[0] = affX[1] = affY[0] = affY[1] = 0;
	masterBright = 0;
	mosaicDone = false;
}

void Gpu2D::write8 (u32 off, u8 v)
{
	if (off >= 0x70) return;
	reg[off] = v;
	if (off < 4) dispcnt = (u32) reg[0] | ((u32) reg[1] << 8) | ((u32) reg[2] << 16) | ((u32) reg[3] << 24);
	if (num == 1 && off < 4) dispcnt &= 0xC0B1FFF7;		// (engine B: no 3D, no large BG, no bases, display modes 0 / 1)
	// the affine reference points: reloaded when written
	if (off >= 0x28 && off < 0x30) { s32 v32 = (s32) (rd32 (reg + (off & ~3u)) << 4) >> 4; if (off < 0x2C) affX[0] = v32; else affY[0] = v32; }
	if (off >= 0x38 && off < 0x40) { s32 v32 = (s32) (rd32 (reg + (off & ~3u)) << 4) >> 4; if (off < 0x3C) affX[1] = v32; else affY[1] = v32; }
	if (off == 0x6C || off == 0x6D) masterBright = r16 (0x6C);
}

u8 Gpu2D::read8 (u32 off) const
{
	if (off >= 0x70) return 0;
	if (off < 4) return (u8) (dispcnt >> (off * 8));
	if (off >= 0x08 && off < 0x10) return reg[off];
	if (off >= 0x48 && off < 0x4C) return reg[off];
	if (off >= 0x50 && off < 0x54) return reg[off];
	if (off == 0x6C || off == 0x6D) return reg[off];
	return 0;
}

void Gpu2D::vblank ()
{
	affX[0] = (s32) (rd32 (reg + 0x28) << 4) >> 4; affY[0] = (s32) (rd32 (reg + 0x2C) << 4) >> 4;
	affX[1] = (s32) (rd32 (reg + 0x38) << 4) >> 4; affY[1] = (s32) (rd32 (reg + 0x3C) << 4) >> 4;
}

u8 *Gpu2D::bgVram (u32 off)
{
	u8 *p = num ? m->ptrBBG[(off >> 14) & 7] : m->ptrABG[(off >> 14) & 31];
	return p ? p + (off & 0x3FFF) : m->zero16k + (off & 0x3FFF);
}
u8 *Gpu2D::objVram (u32 off)
{
	u8 *p = num ? m->ptrBOBJ[(off >> 14) & 7] : m->ptrAOBJ[(off >> 14) & 15];
	return p ? p + (off & 0x3FFF) : m->zero16k + (off & 0x3FFF);
}
u16 Gpu2D::bgExtPal (int slot, int idx)
{
	u8 *p = num ? m->ptrBBGExt[slot & 3] : m->ptrABGExt[slot & 3];
	return p ? rd16 (p + (idx & 0xFFF) * 2) : 0;
}
u16 Gpu2D::objExtPal (int idx)
{
	u8 *p = num ? m->ptrBOBJExt : m->ptrAOBJExt;
	return p ? rd16 (p + (idx & 0xFFF) * 2) : 0;
}

// ---- the BGs ----------------------------------------------------------------------------------------------------
void Gpu2D::renderText (int bg, int y)
{
	u32 *out = bgLine[bg];
	u16 cnt = r16 (0x08 + bg * 2);
	u32 hofs = r16 (0x10 + bg * 4) & 0x1FF, vofs = r16 (0x12 + bg * 4) & 0x1FF;
	bool c256 = cnt & 0x80;
	int size = cnt >> 14;
	u32 charBase = ((cnt >> 2) & 15) * 0x4000 + (num ? 0 : ((dispcnt >> 24) & 7) * 0x10000);
	u32 scrBase = ((cnt >> 8) & 31) * 0x800 + (num ? 0 : ((dispcnt >> 27) & 7) * 0x10000);
	bool ext = c256 && (dispcnt & 0x40000000);
	int extSlot = bg < 2 && (cnt & 0x2000) ? bg + 2 : bg;
	const u8 *palBase = m->pal + num * 0x400;
	if ((cnt & 0x40) && (reg[0x4C] & 0xF0)) { int mv = ((reg[0x4C] >> 4) & 15) + 1; y -= y % mv; }
	u32 yy = ((u32) y + vofs) & ((size & 2) ? 511 : 255);
	u32 rowInMap = (yy & 255) >> 3;
	u32 mapY = (size & 2) && yy >= 256 ? ((size & 1) ? 2 : 1) : 0;
	for (int x = 0; x < W; )
	{
		u32 xx = ((u32) x + hofs) & ((size & 1) ? 511 : 255);
		u32 map = mapY + ((size & 1) && xx >= 256 ? 1 : 0);
		u32 ea = scrBase + map * 0x800 + (rowInMap * 32 + ((xx & 255) >> 3)) * 2;
		u16 e = rd16 (bgVram (ea));
		u32 tile = e & 0x3FF;
		u32 ty = (yy & 7) ^ ((e & 0x800) ? 7 : 0);
		int px = (int) (xx & 7);
		for (; px < 8 && x < W; px++, x++)
		{
			int tx = (e & 0x400) ? 7 - px : px;
			u32 ci;
			if (c256) ci = *bgVram (charBase + tile * 64 + ty * 8 + (u32) tx);
			else { u8 b = *bgVram (charBase + tile * 32 + ty * 4 + (u32) (tx >> 1)); ci = (tx & 1) ? b >> 4 : b & 15; }
			if (!ci) { out[x] = TRANSP; continue; }
			u32 col;
			if (!c256) col = rd16 (palBase + ((e >> 12) * 16 + ci) * 2);
			else if (ext) col = bgExtPal (extSlot, (int) ((e >> 12) * 256 + ci));
			else col = rd16 (palBase + ci * 2);
			out[x] = c15to18 (col);
		}
	}
}

// the affine BG walk: calls px (x, tx, ty) for each pixel inside (or wrapped)
void Gpu2D::renderAffine (int bg, int y)
{
	u32 *out = bgLine[bg];
	u16 cnt = r16 (0x08 + bg * 2);
	int k = bg - 2;
	s32 pa = (s16) r16 (0x20 + k * 0x10), pc = (s16) r16 (0x24 + k * 0x10);
	s32 x0 = affX[k], y0 = affY[k];
	int size = 128 << (cnt >> 14);
	bool wrap = cnt & 0x2000;
	u32 charBase = ((cnt >> 2) & 15) * 0x4000 + (num ? 0 : ((dispcnt >> 24) & 7) * 0x10000);
	u32 scrBase = ((cnt >> 8) & 31) * 0x800 + (num ? 0 : ((dispcnt >> 27) & 7) * 0x10000);
	const u8 *palBase = m->pal + num * 0x400;
	(void) y;
	for (int x = 0; x < W; x++, x0 += pa, y0 += pc)
	{
		s32 tx = x0 >> 8, ty = y0 >> 8;
		if (wrap) { tx &= size - 1; ty &= size - 1; }
		else if (tx < 0 || ty < 0 || tx >= size || ty >= size) { out[x] = TRANSP; continue; }
		u32 tile = *bgVram (scrBase + (u32) ((ty >> 3) * (size >> 3) + (tx >> 3)));
		u32 ci = *bgVram (charBase + tile * 64 + (u32) ((ty & 7) * 8 + (tx & 7)));
		out[x] = ci ? c15to18 (rd16 (palBase + ci * 2)) : TRANSP;
	}
}

void Gpu2D::renderExtended (int bg, int y)
{
	u32 *out = bgLine[bg];
	u16 cnt = r16 (0x08 + bg * 2);
	int k = bg - 2;
	s32 pa = (s16) r16 (0x20 + k * 0x10), pc = (s16) r16 (0x24 + k * 0x10);
	s32 x0 = affX[k], y0 = affY[k];
	bool wrap = cnt & 0x2000;
	const u8 *palBase = m->pal + num * 0x400;
	(void) y;
	if (!(cnt & 0x80))					// affine, 16-bit map entries
	{
		int size = 128 << (cnt >> 14);
		u32 charBase = ((cnt >> 2) & 15) * 0x4000 + (num ? 0 : ((dispcnt >> 24) & 7) * 0x10000);
		u32 scrBase = ((cnt >> 8) & 31) * 0x800 + (num ? 0 : ((dispcnt >> 27) & 7) * 0x10000);
		bool ext = dispcnt & 0x40000000;
		for (int x = 0; x < W; x++, x0 += pa, y0 += pc)
		{
			s32 tx = x0 >> 8, ty = y0 >> 8;
			if (wrap) { tx &= size - 1; ty &= size - 1; }
			else if (tx < 0 || ty < 0 || tx >= size || ty >= size) { out[x] = TRANSP; continue; }
			u16 e = rd16 (bgVram (scrBase + (u32) ((ty >> 3) * (size >> 3) + (tx >> 3)) * 2));
			int px = tx & 7, py = ty & 7;
			if (e & 0x400) px = 7 - px;
			if (e & 0x800) py = 7 - py;
			u32 ci = *bgVram (charBase + (e & 0x3FFu) * 64 + (u32) (py * 8 + px));
			if (!ci) { out[x] = TRANSP; continue; }
			out[x] = c15to18 (ext ? bgExtPal (bg, (int) ((e >> 12) * 256 + ci)) : rd16 (palBase + ci * 2));
		}
		return;
	}
	static const int BW[4] = { 128, 256, 512, 512 }, BH[4] = { 128, 256, 256, 512 };
	int w = BW[cnt >> 14], h = BH[cnt >> 14];
	u32 base = ((cnt >> 8) & 31) * 0x4000;
	bool direct = cnt & 4;
	for (int x = 0; x < W; x++, x0 += pa, y0 += pc)
	{
		s32 tx = x0 >> 8, ty = y0 >> 8;
		if (wrap) { tx &= w - 1; ty &= h - 1; }
		else if (tx < 0 || ty < 0 || tx >= w || ty >= h) { out[x] = TRANSP; continue; }
		if (direct)
		{
			u16 c = rd16 (bgVram (base + (u32) (ty * w + tx) * 2));
			out[x] = (c & 0x8000) ? c15to18 (c) : TRANSP;
		}
		else
		{
			u32 ci = *bgVram (base + (u32) (ty * w + tx));
			out[x] = ci ? c15to18 (rd16 (palBase + ci * 2)) : TRANSP;
		}
	}
}

void Gpu2D::renderLarge (int bg, int y)
{
	u32 *out = bgLine[bg];
	u16 cnt = r16 (0x08 + bg * 2);
	s32 pa = (s16) r16 (0x20), pc = (s16) r16 (0x24);
	s32 x0 = affX[0], y0 = affY[0];
	int w = (cnt & 0x4000) ? 1024 : 512, h = (cnt & 0x4000) ? 512 : 1024;
	bool wrap = cnt & 0x2000;
	(void) y;
	for (int x = 0; x < W; x++, x0 += pa, y0 += pc)
	{
		s32 tx = x0 >> 8, ty = y0 >> 8;
		if (wrap) { tx &= w - 1; ty &= h - 1; }
		else if (tx < 0 || ty < 0 || tx >= w || ty >= h) { out[x] = TRANSP; continue; }
		u32 ci = *bgVram ((u32) (ty * w + tx));
		out[x] = ci ? c15to18 (rd16 (m->pal + ci * 2)) : TRANSP;
	}
}

void Gpu2D::render3D (int y)
{
	u32 *out = bgLine[0];
	const u32 *src = m->r3d.color + y * W;
	int hofs = (int) (r16 (0x10) & 0x1FF);
	if (hofs & 0x100) hofs -= 0x200;
	for (int x = 0; x < W; x++)
	{
		int sx = x + hofs;
		if (sx < 0 || sx >= W) { out[x] = TRANSP; continue; }
		u32 p = src[sx];
		u32 a = (p >> 24) & 31;
		if (!a) { out[x] = TRANSP; continue; }
		out[x] = (p & 0x3FFFF) | IS3D | (a << 18);
	}
}

// ---- the sprites -----------------------------------------------------------------------------------------------
void Gpu2D::renderObjs (int y)
{
	for (int x = 0; x < W; x++) { objLine[x] = TRANSP; objWin[x] = 0; }
	if (!(dispcnt & 0x1000)) return;
	const u8 *oamB = m->oam + num * 0x400;
	const u8 *palB = m->pal + num * 0x400 + 0x200;
	static const int SW[4][4] = { { 8, 16, 32, 64 }, { 16, 32, 32, 64 }, { 8, 8, 16, 32 }, { 0, 0, 0, 0 } };
	static const int SH[4][4] = { { 8, 16, 32, 64 }, { 8, 8, 16, 32 }, { 16, 32, 32, 64 }, { 0, 0, 0, 0 } };
	bool map1d = dispcnt & 0x10;
	u32 boundary = 32u << ((dispcnt >> 20) & 3);
	bool extPal = dispcnt & 0x80000000;
	int mosH = (reg[0x4D] & 15) + 1, mosV = ((reg[0x4D] >> 4) & 15) + 1;
	for (int i = 0; i < 128; i++)
	{
		const u8 *o = oamB + i * 8;
		u16 a0 = rd16 (o), a1 = rd16 (o + 2), a2 = rd16 (o + 4);
		bool affine = a0 & 0x100;
		if (!affine && (a0 & 0x200)) continue;			// disabled
		int shape = a0 >> 14, sz = a1 >> 14;
		int w = SW[shape][sz], h = SH[shape][sz];
		if (!w) continue;
		int bw = w, bh = h;
		if (affine && (a0 & 0x200)) { bw *= 2; bh *= 2; }	// double size
		int sy = a0 & 0xFF; if (sy >= 192) sy -= 256;
		int sx = a1 & 0x1FF; if (sx >= 256) sx -= 512;
		int ly = y - sy;
		if (ly < 0 || ly >= bh) continue;
		int mode = (a0 >> 10) & 3;
		bool mosaic = a0 & 0x1000;
		if (mosaic) ly -= ly % mosV;
		bool c256 = a0 & 0x2000;
		u32 tile = a2 & 0x3FF;
		int prio = (a2 >> 10) & 3;
		int palNo = a2 >> 12;
		s32 pa = 256, pb = 0, pc = 0, pd = 256;
		if (affine)
		{
			int k = (a1 >> 9) & 31;
			pa = (s16) rd16 (oamB + k * 32 + 6); pb = (s16) rd16 (oamB + k * 32 + 14);
			pc = (s16) rd16 (oamB + k * 32 + 22); pd = (s16) rd16 (oamB + k * 32 + 30);
		}
		bool hflip = !affine && (a1 & 0x1000), vflip = !affine && (a1 & 0x2000);
		for (int lx = 0; lx < bw; lx++)
		{
			int x = sx + lx;
			if (x < 0 || x >= W) continue;
			int tx, ty;
			if (affine)
			{
				s32 dx = lx - bw / 2, dy = ly - bh / 2;
				tx = ((pa * dx + pb * dy) >> 8) + w / 2;
				ty = ((pc * dx + pd * dy) >> 8) + h / 2;
				if (tx < 0 || ty < 0 || tx >= w || ty >= h) continue;
			}
			else
			{
				tx = hflip ? w - 1 - lx : lx;
				ty = vflip ? h - 1 - ly : ly;
			}
			if (mosaic) { int mx = (lx / mosH) * mosH; if (!affine) tx = hflip ? w - 1 - mx : mx; }
			u32 col;
			bool opaque;
			if (mode == 3)						// a bitmap sprite
			{
				u32 addr;
				if (dispcnt & 0x40) addr = tile * (128u << ((dispcnt >> 22) & 1)) + (u32) (ty * w + tx) * 2;
				else if (dispcnt & 0x20) addr = ((tile & 0x1F) * 0x10 + (tile & 0x3E0) * 0x80) + (u32) (ty * 512 + tx * 2);
				else addr = ((tile & 0x0F) * 0x10 + (tile & 0x3F0) * 0x80) + (u32) (ty * 256 + tx * 2);
				u16 c = rd16 (objVram (addr));
				opaque = (c & 0x8000) && palNo;
				col = c15to18 (c) | OBJ_BMP | ((u32) palNo << 20);
			}
			else
			{
				u32 ci;
				if (c256)
				{
					u32 addr = map1d ? tile * boundary + (u32) ((ty >> 3) * (w >> 3) + (tx >> 3)) * 64
							 : (tile & ~1u) * 32 + (u32) ((ty >> 3) * 0x400 + (tx >> 3) * 64);
					ci = *objVram (addr + (u32) ((ty & 7) * 8 + (tx & 7)));
					opaque = ci != 0;
					col = c15to18 (extPal ? objExtPal (palNo * 256 + (int) ci) : rd16 (palB + ci * 2));
				}
				else
				{
					u32 addr = map1d ? tile * boundary + (u32) ((ty >> 3) * (w >> 3) + (tx >> 3)) * 32
							 : tile * 32 + (u32) ((ty >> 3) * 0x400 + (tx >> 3) * 32);
					u8 b = *objVram (addr + (u32) ((ty & 7) * 4 + ((tx & 7) >> 1)));
					ci = (tx & 1) ? b >> 4 : b & 15;
					opaque = ci != 0;
					col = c15to18 (rd16 (palB + (palNo * 16 + ci) * 2));
				}
				if (mode == 1) col |= OBJ_SEMI;
			}
			if (!opaque) continue;
			if (mode == 2) { objWin[x] = 1; continue; }
			u32 cur = objLine[x];
			if (!(cur & TRANSP) && (int) ((cur >> 26) & 3) <= prio) continue;	// (a lower index, or a better priority, is there)
			objLine[x] = (col & ~(3u << 26)) | ((u32) prio << 26);
		}
	}
}

// the window mask of a pixel: bits 0-3 the BGs, 4 the OBJ, 5 the effects
u32 Gpu2D::windowMask (int x, int y) const
{
	if (!(dispcnt & 0xE000)) return 0x3F;
	for (int w = 0; w < 2; w++)
	{
		if (!(dispcnt & (0x2000u << w))) continue;
		int x2 = reg[0x40 + w * 2], x1 = reg[0x41 + w * 2];
		int y2 = reg[0x44 + w * 2], y1 = reg[0x45 + w * 2];
		bool inX = x1 <= x2 ? (x >= x1 && x < x2) : (x >= x1 || x < x2);
		bool inY = y1 <= y2 ? (y >= y1 && y < y2) : (y >= y1 || y < y2);
		if (inX && inY) return reg[0x48 + w] & 0x3F;
	}
	if ((dispcnt & 0x8000) && objWin[x]) return reg[0x4B] & 0x3F;
	return reg[0x4A] & 0x3F;
}

static inline u32 blendAlpha (u32 a, u32 b, u32 eva, u32 evb)
{
	u32 r = ((a & 63) * eva + (b & 63) * evb) >> 4, g = (((a >> 6) & 63) * eva + ((b >> 6) & 63) * evb) >> 4, bl = (((a >> 12) & 63) * eva + ((b >> 12) & 63) * evb) >> 4;
	if (r > 63) r = 63;
	if (g > 63) g = 63;
	if (bl > 63) bl = 63;
	return r | (g << 6) | (bl << 12);
}
static inline u32 brighten (u32 a, u32 evy)
{
	u32 r = a & 63, g = (a >> 6) & 63, b = (a >> 12) & 63;
	r += ((63 - r) * evy) >> 4; g += ((63 - g) * evy) >> 4; b += ((63 - b) * evy) >> 4;
	return r | (g << 6) | (b << 12);
}
static inline u32 darken (u32 a, u32 evy)
{
	u32 r = a & 63, g = (a >> 6) & 63, b = (a >> 12) & 63;
	r -= (r * evy) >> 4; g -= (g * evy) >> 4; b -= (b * evy) >> 4;
	return r | (g << 6) | (b << 12);
}

void Gpu2D::compose (int y, u32 *out)
{
	u32 bgMode = dispcnt & 7;
	bool en[4];
	for (int i = 0; i < 4; i++) en[i] = (dispcnt & (0x100u << i)) != 0;
	// which BGs and how
	int kind[4] = { 0, 0, 0, 0 };				// 1 text, 2 affine, 3 extended, 4 large, 5 3D
	switch (bgMode)
	{
	case 0: kind[0] = kind[1] = kind[2] = kind[3] = 1; break;
	case 1: kind[0] = kind[1] = kind[2] = 1; kind[3] = 2; break;
	case 2: kind[0] = kind[1] = 1; kind[2] = kind[3] = 2; break;
	case 3: kind[0] = kind[1] = kind[2] = 1; kind[3] = 3; break;
	case 4: kind[0] = kind[1] = 1; kind[2] = 2; kind[3] = 3; break;
	case 5: kind[0] = kind[1] = 1; kind[2] = kind[3] = 3; break;
	case 6: kind[0] = 1; kind[2] = 4; en[1] = en[3] = false; break;
	default: en[0] = en[1] = en[2] = en[3] = false; break;
	}
	if (!num && (dispcnt & 8)) kind[0] = 5;
	for (int i = 0; i < 4; i++)
	{
		if (!en[i]) continue;
		switch (kind[i])
		{
		case 1: renderText (i, y); break;
		case 2: renderAffine (i, y); break;
		case 3: renderExtended (i, y); break;
		case 4: renderLarge (i, y); break;
		case 5: render3D (y); break;
		}
	}
	renderObjs (y);
	// the affine reference points move on
	for (int k = 0; k < 2; k++) { affX[k] += (s16) r16 (0x22 + k * 0x10); affY[k] += (s16) r16 (0x26 + k * 0x10); }
	// the BGs in priority order (then by number)
	int order[4], no = 0;
	for (int p = 0; p < 4; p++) for (int i = 0; i < 4; i++) if (en[i] && (int) (reg[0x08 + i * 2] & 3) == p) order[no++] = i;
	int prio[4]; for (int i = 0; i < 4; i++) prio[i] = reg[0x08 + i * 2] & 3;
	u32 backdrop = c15to18 (rd16 (m->pal + num * 0x400));
	u16 bldcnt = r16 (0x50);
	int effect = (bldcnt >> 6) & 3;
	u32 eva = reg[0x52] & 31, evb = reg[0x53] & 31, evy = reg[0x54] & 31;
	if (eva > 16) eva = 16;
	if (evb > 16) evb = 16;
	if (evy > 16) evy = 16;
	bool anyWin = dispcnt & 0xE000;
	bool objOn = dispcnt & 0x1000;
	for (int x = 0; x < W; x++)
	{
		u32 wm = anyWin ? windowMask (x, y) : 0x3F;
		// the two layers on top: (colour, layer id: 0-3 BG, 4 OBJ, 5 backdrop)
		u32 top = backdrop, second = backdrop; int topL = 5, secL = 5;
		int found = 0;
		u32 obj = objOn && (wm & 0x10) ? objLine[x] : TRANSP;
		int objPrio = (obj & TRANSP) ? 99 : (int) ((obj >> 26) & 3);
		int bi = 0;
		for (int p = 0; p < 4 && found < 2; p++)
		{
			if (objPrio == p && found < 2)
			{
				if (!found) { top = obj; topL = 4; } else { second = obj; secL = 4; }
				found++;
			}
			while (bi < no && prio[order[bi]] == p && found < 2)
			{
				int b = order[bi++];
				if (!(wm & (1u << b))) continue;
				u32 c = bgLine[b][x];
				if (c & TRANSP) continue;
				if (!found) { top = c; topL = b; } else { second = c; secL = b; }
				found++;
			}
			while (bi < no && prio[order[bi]] == p) bi++;
		}
		u32 res = top & 0x3FFFF;
		bool secTarget = (bldcnt >> 8) & (1u << secL);
		bool firstTarget = (bldcnt >> topL) & 1;
		bool fx = (wm & 0x20) != 0;
		if (topL == 0 && (top & IS3D) && secTarget)		// the 3D layer's own alpha
		{
			u32 a = (top >> 18) & 31;
			if (a < 31) res = blendAlpha (top, second, (a + 1) >> 1, (31 - a) >> 1);
			else if (fx && firstTarget && effect >= 2) res = effect == 2 ? brighten (res, evy) : darken (res, evy);
		}
		else if (topL == 4 && (top & OBJ_BMP) && secTarget)
		{
			u32 a = (top >> 20) & 15;
			res = blendAlpha (top, second, a + 1, 15 - a);
		}
		else if (topL == 4 && (top & OBJ_SEMI) && secTarget) res = blendAlpha (top, second, eva, evb);
		else if (fx && firstTarget)
		{
			if (effect == 1 && secTarget) res = blendAlpha (top, second, eva, evb);
			else if (effect == 2) res = brighten (res, evy);
			else if (effect == 3) res = darken (res, evy);
		}
		out[x] = res;
	}
}

void Gpu2D::renderLine (int y, u32 *out)
{
	// out: the line composed (6-bit channels), before the master brightness
	if (dispcnt & 0x80) { for (int x = 0; x < W; x++) out[x] = 0x3FFFF; return; }	// forced blank: white
	compose (y, out);
}

// ---- the displays ------------------------------------------------------------------------------------------------
static void applyBright (u16 mb, u32 *line, u32 *dst)
{
	int mode = mb >> 14; u32 f = mb & 31; if (f > 16) f = 16;
	for (int x = 0; x < W; x++)
	{
		u32 c = line[x];
		if (mode == 1 && f) c = brighten (c, f);
		else if (mode == 2 && f) c = darken (c, f);
		dst[x] = c18to24 (c);
	}
}

void Machine::captureLine (int y, const u32 *lineA)
{
	u32 cnt = dispcapcnt;
	static const int CH[4] = { 128, 64, 128, 192 };
	int size = (int) (cnt >> 20) & 3;
	int width = size ? 256 : 128, height = CH[size];
	if (y >= height) return;
	int bank = (int) (cnt >> 16) & 3;
	if ((vramcnt[bank] & 0x87) != 0x80) return;		// (the bank must be in LCDC mode)
	u8 *dst = vram + bankOff[bank];
	u32 dofs = ((cnt >> 18) & 3) * 0x8000 + (u32) (y * width * 2);
	u32 src = (cnt >> 29) & 3;
	u32 eva = cnt & 31, evb = (cnt >> 8) & 31; if (eva > 16) eva = 16; if (evb > 16) evb = 16;
	// source B: VRAM (the bank DISPCNT names) or the main memory FIFO
	int vbank = (int) (gpuA.dispcnt >> 18) & 3;
	const u8 *vb = vram + bankOff[vbank];
	u32 vofs = ((cnt >> 26) & 3) * 0x8000 + (u32) (y * width * 2);
	for (int x = 0; x < width; x++)
	{
		u32 a15, aA;
		if (cnt & (1u << 24)) { u32 p = r3d.color[y * W + x]; a15 = c18to15 (p & 0x3FFFF); aA = ((p >> 24) & 31) ? 1 : 0; }
		else { a15 = c18to15 (lineA[x]); aA = 1; }
		u32 b15 = rd16 (vb + ((vofs + (u32) x * 2) & 0x1FFFF)), aB = b15 >> 15;
		u32 o;
		if (src == 0) o = a15 | (aA << 15);
		else if (src == 1) o = b15;
		else
		{
			u32 ea = aA ? eva : 0, eb = aB ? evb : 0;
			u32 r = (((a15 & 31) * ea + (b15 & 31) * eb) + 8) >> 4, g = ((((a15 >> 5) & 31) * ea + ((b15 >> 5) & 31) * eb) + 8) >> 4,
			    b = ((((a15 >> 10) & 31) * ea + ((b15 >> 10) & 31) * eb) + 8) >> 4;
			if (r > 31) r = 31;
			if (g > 31) g = 31;
			if (b > 31) b = 31;
			o = r | (g << 5) | (b << 10) | ((ea || eb) ? 0x8000 : 0);
		}
		wr16 (dst + ((dofs + (u32) x * 2) & 0x1FFFF), (u16) o);
	}
	if (y == height - 1) dispcapcnt &= ~0x80000000u;
}

void Machine::displayLine (int y)
{
	static u32 lineA[W], lineB[W];
	bool use3d = (gpuA.dispcnt & 8) || (dispcapcnt & 0x81000000) == 0x81000000;
	if (use3d && render3dWait) render3dWait (render3dCtx, y);
	int modeA = (int) (gpuA.dispcnt >> 16) & 3;
	gpuA.renderLine (y, lineA);
	if (dispcapcnt & 0x80000000) captureLine (y, lineA);
	if (modeA == 2)						// VRAM display
	{
		int bank = (int) (gpuA.dispcnt >> 18) & 3;
		const u8 *p = vram + bankOff[bank] + y * W * 2;
		for (int x = 0; x < W; x++) lineA[x] = c15to18 (rd16 (p + x * 2));
	}
	else if (modeA == 3)					// main memory display (through a DMA)
	{
		for (int ch = 0; ch < 4; ch++)
		{
			Dma &d = dma[0][ch];
			if ((d.cnt & 0x80000000) && dmaTiming (0, ch) == 4)
			{
				for (int x = 0; x < W; x += 2) { u32 v = bus9Read32 (d.src); d.src += 4; lineA[x] = c15to18 (v & 0xFFFF); lineA[x + 1] = c15to18 (v >> 16); }
				break;
			}
		}
	}
	else if (modeA == 0) for (int x = 0; x < W; x++) lineA[x] = 0x3FFFF;
	int modeB = (int) (gpuB.dispcnt >> 16) & 3;
	gpuB.renderLine (y, lineB);
	if (modeB == 0) for (int x = 0; x < W; x++) lineB[x] = 0x3FFFF;
	bool aTop = powcnt1 & 0x8000;
	u32 *dA = screen[aTop ? 0 : 1] + y * W, *dB = screen[aTop ? 1 : 0] + y * W;
	if (!(powcnt1 & 1)) { for (int x = 0; x < W; x++) dA[x] = dB[x] = 0; return; }
	if (powcnt1 & 2) applyBright (gpuA.masterBright, lineA, dA); else for (int x = 0; x < W; x++) dA[x] = 0;
	if (powcnt1 & 0x200) applyBright (gpuB.masterBright, lineB, dB); else for (int x = 0; x < W; x++) dB[x] = 0;
}

} // namespace nds
