//
// gba/gba_ppu.cpp -- the picture, a line at a time (at the start of each line's H-blank):
// the text and affine backgrounds, the bitmap modes, the sprites (normal and affine), the
// windows (0, 1, the sprite window, outside), the priorities, the colour effects (alpha
// blending, brighter, darker; semi-transparent sprites) and the mosaic.
//
#include "gba/gba.h"

namespace gba {

static const u32 TRANSP = 0x80000000u;

static u32 s_rgb[32768];				// BGR555 -> 0x00RRGGBB
static bool s_rgbInit = false;
static void rgbInit ()
{
	for (u32 c = 0; c < 32768; c++)
	{
		u32 r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
		s_rgb[c] = (((r << 3) | (r >> 2)) << 16) | (((g << 3) | (g >> 2)) << 8) | ((b << 3) | (b >> 2));
	}
	s_rgbInit = true;
}

static inline u16 ld16 (const u8 *p) { return (u16) (p[0] | (p[1] << 8)); }

void Machine::latchAffine ()
{
	affX[0] = (s32) ((u32) (io[0x28] | (io[0x29] << 8) | (io[0x2A] << 16) | (io[0x2B] << 24)) << 4) >> 4;
	affY[0] = (s32) ((u32) (io[0x2C] | (io[0x2D] << 8) | (io[0x2E] << 16) | (io[0x2F] << 24)) << 4) >> 4;
	affX[1] = (s32) ((u32) (io[0x38] | (io[0x39] << 8) | (io[0x3A] << 16) | (io[0x3B] << 24)) << 4) >> 4;
	affY[1] = (s32) ((u32) (io[0x3C] | (io[0x3D] << 8) | (io[0x3E] << 16) | (io[0x3F] << 24)) << 4) >> 4;
}

void Machine::renderText (int bg, u32 *line)
{
	u16 cnt = io16 (0x08 + bg * 2);
	int hofs = io16 (0x10 + bg * 4) & 511, vofs = io16 (0x12 + bg * 4) & 511;
	u32 charBase = ((cnt >> 2) & 3) * 0x4000u, screenBase = ((cnt >> 8) & 31) * 0x800u;
	bool bpp8 = cnt & 0x80;
	int size = cnt >> 14;
	int wmask = (size & 1) ? 511 : 255, hmask = (size & 2) ? 511 : 255;
	int ly = vcount;
	int mosH = 1;
	if (cnt & 0x40) { u16 m = io16 (0x4C); int mv = ((m >> 4) & 15) + 1; mosH = (m & 15) + 1; ly -= ly % mv; }
	int y = (ly + vofs) & hmask;
	int sby = (y >> 8) & 1;
	u32 rowBase = screenBase + (u32) ((y >> 3) & 31) * 64;
	int ty0 = y & 7;
	for (int x = 0; x < W; )
	{
		int px = (x + hofs) & wmask;
		int sbx = (px >> 8) & 1;
		int block = size == 0 ? 0 : size == 1 ? sbx : size == 2 ? sby : sbx + sby * 2;
		u16 e = ld16 (vram + ((rowBase + (u32) block * 0x800 + (u32) ((px >> 3) & 31) * 2) & 0xFFFF));
		u32 tile = e & 0x3FF;
		int ty = (e & 0x800) ? 7 - ty0 : ty0;
		bool hf = e & 0x400;
		// the rest of this tile's row
		for (int tx = px & 7; tx < 8 && x < W; tx++, x++)
		{
			int cx = hf ? 7 - tx : tx;
			u32 idx;
			if (bpp8)
			{
				u32 a = charBase + tile * 64 + (u32) ty * 8 + (u32) cx;
				idx = a < 0x10000 ? vram[a] : 0;
				line[x] = idx ? ld16 (pal + idx * 2) : TRANSP;
			}
			else
			{
				u32 a = charBase + tile * 32 + (u32) ty * 4 + (u32) (cx >> 1);
				idx = a < 0x10000 ? (vram[a] >> ((cx & 1) * 4)) & 15 : 0;
				line[x] = idx ? ld16 (pal + ((e >> 12) * 16 + idx) * 2) : TRANSP;
			}
		}
	}
	if (mosH > 1) for (int x = 0; x < W; x++) line[x] = line[x - x % mosH];
}

void Machine::renderAffine (int bg, u32 *line)
{
	u16 cnt = io16 (0x08 + bg * 2);
	int k = bg - 2;
	u32 charBase = ((cnt >> 2) & 3) * 0x4000u, screenBase = ((cnt >> 8) & 31) * 0x800u;
	int size = 128 << (cnt >> 14), tiles = size >> 3;
	bool wrap = cnt & 0x2000;
	s32 pa = (s16) io16 (0x20 + k * 16), pc = (s16) io16 (0x24 + k * 16);
	s32 fx = affX[k], fy = affY[k];
	for (int x = 0; x < W; x++, fx += pa, fy += pc)
	{
		int tx = fx >> 8, ty = fy >> 8;
		if (wrap) { tx &= size - 1; ty &= size - 1; }
		else if (tx < 0 || ty < 0 || tx >= size || ty >= size) { line[x] = TRANSP; continue; }
		u32 tile = vram[(screenBase + (u32) ((ty >> 3) * tiles + (tx >> 3))) & 0xFFFF];
		u32 idx = vram[(charBase + tile * 64 + (u32) ((ty & 7) * 8 + (tx & 7))) & 0xFFFF];
		line[x] = idx ? ld16 (pal + idx * 2) : TRANSP;
	}
	if (cnt & 0x40)
	{
		int mosH = (io16 (0x4C) & 15) + 1;
		if (mosH > 1) for (int x = 0; x < W; x++) line[x] = line[x - x % mosH];
	}
}

void Machine::renderBitmap (int mode, u32 *line)
{
	u16 cnt = io16 (0x0C);
	s32 pa = (s16) io16 (0x20), pc = (s16) io16 (0x24);
	s32 fx = affX[0], fy = affY[0];
	u32 page = (io[0] & 0x10) ? 0xA000 : 0;
	int bw = mode == 5 ? 160 : 240, bh = mode == 5 ? 128 : 160;
	for (int x = 0; x < W; x++, fx += pa, fy += pc)
	{
		int tx = fx >> 8, ty = fy >> 8;
		if (tx < 0 || ty < 0 || tx >= bw || ty >= bh) { line[x] = TRANSP; continue; }
		if (mode == 3) line[x] = ld16 (vram + (u32) (ty * 240 + tx) * 2) & 0x7FFF;
		else if (mode == 5) line[x] = ld16 (vram + page + (u32) (ty * 160 + tx) * 2) & 0x7FFF;
		else { u32 idx = vram[page + (u32) (ty * 240 + tx)]; line[x] = idx ? ld16 (pal + idx * 2) : TRANSP; }
	}
	if (cnt & 0x40)
	{
		int mosH = (io16 (0x4C) & 15) + 1;
		if (mosH > 1) for (int x = 0; x < W; x++) line[x] = line[x - x % mosH];
	}
}

// Sprites: line[] colour (TRANSP if none) with bit 30 = semi-transparent; prio[] 0..3;
// winMask[] 1 where a sprite-window sprite is.
void Machine::renderObjs (u32 *line, u8 *prio, u8 *objWin)
{
	static const u8 SW[3][4] = { { 8, 16, 32, 64 }, { 16, 32, 32, 64 }, { 8, 8, 16, 32 } };
	static const u8 SH[3][4] = { { 8, 16, 32, 64 }, { 8, 8, 16, 32 }, { 16, 32, 32, 64 } };
	u16 disp = io16 (0);
	bool map1D = disp & 0x40;
	bool bitmap = (disp & 7) >= 3;
	int ly = vcount;
	for (int i = 0; i < 128; i++)
	{
		u16 a0 = ld16 (oam + i * 8), a1 = ld16 (oam + i * 8 + 2), a2 = ld16 (oam + i * 8 + 4);
		bool affine = a0 & 0x100;
		if (!affine && (a0 & 0x200)) continue;
		int mode = (a0 >> 10) & 3;
		if (mode == 3) continue;
		int shape = a0 >> 14;
		if (shape == 3) continue;
		int w = SW[shape][a1 >> 14], h = SH[shape][a1 >> 14];
		int y = a0 & 0xFF; if (y >= 160) y -= 256;
		int x = a1 & 0x1FF; if (x >= 240) x -= 512;
		int bw = w, bh = h;
		if (affine && (a0 & 0x200)) { bw *= 2; bh *= 2; }
		int row = ly - y;
		if (row < 0 || row >= bh) { if (y + bh > 256 && ly < ((y + bh) & 255)) row = ly + 256 - y; else continue; }
		if (row < 0 || row >= bh) continue;
		bool bpp8 = a0 & 0x2000;
		u32 tile = a2 & 0x3FF;
		if (bitmap && tile < 512) continue;
		int pr = (a2 >> 10) & 3, palb = a2 >> 12;
		if ((a0 & 0x1000))					// mosaic (vertical)
		{
			int mv = ((io16 (0x4C) >> 12) & 15) + 1;
			row -= row % mv;
		}
		s32 pa = 256, pb = 0, pc = 0, pd = 256;
		if (affine)
		{
			u32 p = ((a1 >> 9) & 31) * 32;
			pa = (s16) ld16 (oam + p + 6); pb = (s16) ld16 (oam + p + 14);
			pc = (s16) ld16 (oam + p + 22); pd = (s16) ld16 (oam + p + 30);
		}
		int rowTiles = map1D ? (bpp8 ? w / 4 : w / 8) : 32;	// (in 32-byte tile units)
		for (int sx = 0; sx < bw; sx++)
		{
			int px = x + sx;
			if (px < 0 || px >= W) continue;
			int tx, ty;
			if (affine)
			{
				int dx = sx - bw / 2, dy = row - bh / 2;
				tx = ((pa * dx + pb * dy) >> 8) + w / 2;
				ty = ((pc * dx + pd * dy) >> 8) + h / 2;
				if (tx < 0 || ty < 0 || tx >= w || ty >= h) continue;
			}
			else
			{
				tx = (a1 & 0x1000) ? w - 1 - sx : sx;
				ty = (a1 & 0x2000) ? h - 1 - row : row;
			}
			u32 color;
			if (bpp8)
			{
				u32 t = tile + (u32) ((ty >> 3) * rowTiles + (tx >> 3) * 2);
				u32 idx = vram[0x10000 + (t & 0x3FF) * 32 + (u32) ((ty & 7) * 8 + (tx & 7))];
				if (!idx) continue;
				color = ld16 (pal + 0x200 + idx * 2);
			}
			else
			{
				u32 t = tile + (u32) ((ty >> 3) * rowTiles + (tx >> 3));
				u32 idx = (vram[0x10000 + (t & 0x3FF) * 32 + (u32) ((ty & 7) * 4 + ((tx & 7) >> 1))] >> ((tx & 1) * 4)) & 15;
				if (!idx) continue;
				color = ld16 (pal + 0x200 + (u32) (palb * 16 + (int) idx) * 2);
			}
			if (mode == 2) { objWin[px] = 1; continue; }
			if (line[px] == TRANSP || pr < prio[px])
			{
				line[px] = (color & 0x7FFF) | (mode == 1 ? 0x40000000u : 0);
				prio[px] = (u8) pr;
			}
		}
	}
}

void Machine::renderLine ()
{
	if (!s_rgbInit) rgbInit ();
	u32 *out = fb + vcount * W;
	u16 disp = io16 (0);
	if (disp & 0x80) { for (int x = 0; x < W; x++) out[x] = 0xFFFFFF; return; }	// forced blank
	int mode = disp & 7;
	static u32 bgl[4][W], objl[W];
	static u8 objp[W], objw[W], win[W];
	bool bgOn[4] = { false, false, false, false };
	for (int b = 0; b < 4; b++)
	{
		if (!(disp & (0x100 << b))) continue;
		if (mode == 0) bgOn[b] = true;
		else if (mode == 1) bgOn[b] = b < 3;
		else if (mode == 2) bgOn[b] = b >= 2;
		else bgOn[b] = b == 2;
	}
	for (int b = 0; b < 4; b++)
	{
		if (!bgOn[b]) continue;
		if (mode == 0 || (mode == 1 && b < 2)) renderText (b, bgl[b]);
		else if (mode <= 2) renderAffine (b, bgl[b]);
		else renderBitmap (mode, bgl[b]);
	}
	bool objOn = disp & 0x1000;
	for (int x = 0; x < W; x++) { objl[x] = TRANSP; objp[x] = 4; objw[x] = 0; }
	if (objOn) renderObjs (objl, objp, objw);
	// windows: which layers (bits 0-4) and effects (bit 5) each pixel shows
	bool w0 = disp & 0x2000, w1 = disp & 0x4000, wo = (disp & 0x8000) && objOn;
	if (w0 || w1 || wo)
	{
		u8 outside = io[0x4A] & 0x3F, objIn = io[0x4B] & 0x3F;
		for (int x = 0; x < W; x++) win[x] = outside;
		if (wo) for (int x = 0; x < W; x++) if (objw[x]) win[x] = objIn;
		for (int k = 1; k >= 0; k--)				// (window 0 over window 1)
		{
			if (!(k ? w1 : w0)) continue;
			int x1 = io[0x41 + k * 2], x2 = io[0x40 + k * 2], y1 = io[0x45 + k * 2], y2 = io[0x44 + k * 2];
			if (x2 > W || x1 > x2) x2 = W;			// (as the hardware reads odd values)
			if (y2 > 228 || y1 > y2) y2 = 228;
			if (vcount < y1 || vcount >= y2) continue;
			u8 m = io[0x48 + k] & 0x3F;
			for (int x = x1; x < x2; x++) win[x] = m;
		}
	}
	else for (int x = 0; x < W; x++) win[x] = 0x3F;
	// the backgrounds by priority (then number)
	int order[4], no = 0, bgPrio[4];
	for (int b = 0; b < 4; b++) bgPrio[b] = io16 (0x08 + b * 2) & 3;
	for (int p = 0; p < 4; p++)
		for (int b = 0; b < 4; b++)
			if (bgOn[b] && bgPrio[b] == p) order[no++] = b;
	u16 bld = io16 (0x50);
	int eva = io[0x52] & 31, evb = io[0x53] & 31, evy = io[0x54] & 31;
	if (eva > 16) eva = 16;
	if (evb > 16) evb = 16;
	if (evy > 16) evy = 16;
	u32 backdrop = ld16 (pal) & 0x7FFF;
	bool effects = ((bld >> 6) & 3) != 0;
	for (int x = 0; x < W; x++)
	{
		u8 m = win[x];
		// the two top backgrounds here, then where the sprite goes among them
		int b1 = -1, b2 = -1;
		for (int k = 0; k < no; k++)
		{
			int b = order[k];
			if (bgl[b][x] == TRANSP || !(m & (1 << b))) continue;
			if (b1 < 0) { b1 = b; if (!effects && objl[x] == TRANSP) break; }
			else { b2 = b; break; }
		}
		bool objHere = objl[x] != TRANSP && (m & 0x10);
		int op = objp[x];
		// the two top layers: colour and layer number (0-3 BG, 4 OBJ, 5 backdrop)
		u32 c1 = backdrop, c2 = backdrop; int l1 = 5, l2 = 5;
		bool semi = false;
		if (objHere && (b1 < 0 || op <= bgPrio[b1]))
		{
			c1 = objl[x] & 0x7FFF; l1 = 4; semi = (objl[x] & 0x40000000) != 0;
			if (b1 >= 0) { c2 = bgl[b1][x]; l2 = b1; }
		}
		else
		{
			if (b1 >= 0) { c1 = bgl[b1][x]; l1 = b1; }
			if (objHere && (b2 < 0 || op <= bgPrio[b2])) { c2 = objl[x] & 0x7FFF; l2 = 4; }
			else if (b2 >= 0) { c2 = bgl[b2][x]; l2 = b2; }
		}
		if (!effects && !semi) { out[x] = s_rgb[c1 & 0x7FFF]; continue; }
		u32 res = c1;
		bool top1 = (bld >> l1) & 1, bot2 = (bld >> (8 + l2)) & 1;
		int e = semi && bot2 ? 1 : ((m & 0x20) && top1 ? ((bld >> 6) & 3) : 0);
		if (e == 1 && bot2)
		{
			u32 r = ((c1 & 31) * eva + (c2 & 31) * evb) >> 4, g = (((c1 >> 5) & 31) * eva + ((c2 >> 5) & 31) * evb) >> 4,
			    b = (((c1 >> 10) & 31) * eva + ((c2 >> 10) & 31) * evb) >> 4;
			res = (r > 31 ? 31 : r) | ((g > 31 ? 31 : g) << 5) | ((b > 31 ? 31 : b) << 10);
		}
		else if (e == 2)
		{
			u32 r = c1 & 31, g = (c1 >> 5) & 31, b = (c1 >> 10) & 31;
			r += ((31 - r) * (u32) evy) >> 4; g += ((31 - g) * (u32) evy) >> 4; b += ((31 - b) * (u32) evy) >> 4;
			res = r | (g << 5) | (b << 10);
		}
		else if (e == 3)
		{
			u32 r = c1 & 31, g = (c1 >> 5) & 31, b = (c1 >> 10) & 31;
			r -= (r * (u32) evy) >> 4; g -= (g * (u32) evy) >> 4; b -= (b * (u32) evy) >> 4;
			res = r | (g << 5) | (b << 10);
		}
		out[x] = s_rgb[res & 0x7FFF];
	}
}

} // namespace gba
