//
// snes/snes_ppu.cpp -- the PPU: its registers ($2100-$213F: VRAM / OAM / CGRAM ports, the
// scroll, Mode 7 matrix and multiplier, windows, colour math, the H/V counter latch) and a
// renderer that makes a whole line when the beam reaches it: the backgrounds of the mode
// (2 / 4 / 8 bpp, 8x8 or 16x16 tiles, 32 / 64-tile maps, offset-per-tile in modes 2 / 4 / 6,
// mosaic, direct colour), Mode 7 (the affine plane, its outside modes, EXTBG), the sprites
// (32 a line, the OAM priority rotation), the main and sub screens with their windows, the
// colour math (add / subtract, half, the fixed colour, clip / prevent regions), brightness.
// Modes 5 / 6 (512 wide) are shown at 256 (every other pixel).
//
#include "snes/snes.h"

namespace snes {

static inline u16 vword (const u8 *vram, u32 w) { w = (w & 0x7FFF) << 1; return (u16) (vram[w] | vram[w + 1] << 8); }

void Machine::ppuReset ()
{
	inidisp = 0x80; obsel = 0; bgmode = 0; mosaic = 0; vmain = 0; m7sel = 0;
	for (int i = 0; i < 4; i++) { bgsc[i] = 0; bghofs[i] = bgvofs[i] = 0; }
	bgnba[0] = bgnba[1] = 0; bgLatch = bgLatchH = 0;
	m7a = m7b = m7c = m7d = m7x = m7y = m7hofs = m7vofs = 0; m7Latch = 0;
	vramAddr = 0; vramBuf = 0;
	oamAddr = oamReload = 0; oamPrioRot = false; oamLatch = 0;
	cgAddr = 0; cgLatch = 0;
	w12sel = w34sel = wobjsel = 0; wh[0] = wh[2] = 1; wh[1] = wh[3] = 0; wbglog = wobjlog = 0;
	tm = ts = tmw = tsw = 0; cgwsel = cgadsub = 0; setini = 0; fixedColor = 0; overscan = false;
	hLatch = vLatch = 0; hFlip = vFlip = counterLatched = false; stat77 = 0; ppu1Bus = ppu2Bus = 0;
	for (int i = 0; i < 544; i++) oam[i] = 0;
	for (int i = 0; i < 512; i++) cgram[i] = 0;
}

u16 Machine::vramRemap () const
{
	u16 a = vramAddr;
	switch ((vmain >> 2) & 3)
	{
	case 1: return (u16) ((a & 0xFF00) | ((a & 0x1F) << 3) | ((a >> 5) & 7));
	case 2: return (u16) ((a & 0xFE00) | ((a & 0x3F) << 3) | ((a >> 6) & 7));
	case 3: return (u16) ((a & 0xFC00) | ((a & 0x7F) << 3) | ((a >> 7) & 7));
	}
	return a;
}

void Machine::vramStep (bool high)
{
	static const u16 STEP[4] = { 1, 32, 128, 128 };
	if (high == ((vmain & 0x80) != 0)) vramAddr = (u16) (vramAddr + STEP[vmain & 3]);
}

void Machine::latchCounters ()
{
	hLatch = hcounter (); if (hLatch > 339) hLatch = 339;
	vLatch = (u16) line;
	counterLatched = true;
}

u8 Machine::ppuRead (u8 r)
{
	switch (r)
	{
	case 0x34: case 0x35: case 0x36:
	{
		s32 p = (s32) m7a * (s8) (m7b >> 8);
		return ppu1Bus = (u8) (p >> ((r - 0x34) * 8));
	}
	case 0x37: if (wrio & 0x80) latchCounters (); return mdr;
	case 0x38:
	{
		u8 v = oamAddr < 0x200 ? oam[oamAddr] : oam[0x200 | (oamAddr & 0x1F)];
		oamAddr = (u16) ((oamAddr + 1) & 0x3FF);
		return ppu1Bus = v;
	}
	case 0x39:
	{
		u8 v = (u8) vramBuf;
		if (!(vmain & 0x80)) { vramBuf = vword (vram, vramRemap ()); vramStep (false); }
		return ppu1Bus = v;
	}
	case 0x3A:
	{
		u8 v = (u8) (vramBuf >> 8);
		if (vmain & 0x80) { vramBuf = vword (vram, vramRemap ()); vramStep (true); }
		return ppu1Bus = v;
	}
	case 0x3B:
	{
		u8 v = cgram[cgAddr];
		if (cgAddr & 1) v = (u8) ((v & 0x7F) | (ppu2Bus & 0x80));
		cgAddr = (u16) ((cgAddr + 1) & 0x1FF);
		return ppu2Bus = v;
	}
	case 0x3C:
	{
		u8 v = hFlip ? (u8) (((hLatch >> 8) & 1) | (ppu2Bus & 0xFE)) : (u8) hLatch;
		hFlip = !hFlip;
		return ppu2Bus = v;
	}
	case 0x3D:
	{
		u8 v = vFlip ? (u8) (((vLatch >> 8) & 1) | (ppu2Bus & 0xFE)) : (u8) vLatch;
		vFlip = !vFlip;
		return ppu2Bus = v;
	}
	case 0x3E: return ppu1Bus = (u8) (stat77 | 1 | (ppu1Bus & 0x10));
	case 0x3F:
	{
		u8 v = (u8) ((counterLatched ? 0x40 : 0) | (pal ? 0x10 : 0) | 3 | (ppu2Bus & 0x20) | ((frames & 1) ? 0x80 : 0));
		hFlip = vFlip = false;
		if (wrio & 0x80) counterLatched = false;
		return ppu2Bus = v;
	}
	}
	return mdr;
}

void Machine::ppuWrite (u8 r, u8 v)
{
	switch (r)
	{
	case 0x00:
		if ((inidisp & 0x80) && !(v & 0x80) && line == vdisp ()) oamAddr = oamReload;
		inidisp = v; break;
	case 0x01: obsel = v; break;
	case 0x02: oamReload = (u16) ((oamReload & 0x200) | v << 1); oamAddr = oamReload; break;
	case 0x03: oamReload = (u16) ((oamReload & 0x1FE) | (v & 1) << 9); oamPrioRot = v & 0x80; oamAddr = oamReload; break;
	case 0x04:
		if (oamAddr < 0x200)
		{
			if (!(oamAddr & 1)) oamLatch = v;
			else { oam[oamAddr - 1] = oamLatch; oam[oamAddr] = v; }
		}
		else oam[0x200 | (oamAddr & 0x1F)] = v;
		oamAddr = (u16) ((oamAddr + 1) & 0x3FF);
		break;
	case 0x05: bgmode = v; break;
	case 0x06: mosaic = v; break;
	case 0x07: case 0x08: case 0x09: case 0x0A: bgsc[r - 7] = v; break;
	case 0x0B: bgnba[0] = v; break;
	case 0x0C: bgnba[1] = v; break;
	case 0x0D:
		m7hofs = (s16) ((s16) ((v << 8 | m7Latch) << 3) >> 3); m7Latch = v;
		[[fallthrough]];
		// fall through: BG1HOFS too
	case 0x0F: case 0x11: case 0x13:
	{
		int n = (r - 0x0D) >> 1;
		bghofs[n] = (u16) (((v << 8) | (bgLatch & ~7) | (bgLatchH & 7)) & 0x3FF);
		bgLatch = v; bgLatchH = v;
		break;
	}
	case 0x0E:
		m7vofs = (s16) ((s16) ((v << 8 | m7Latch) << 3) >> 3); m7Latch = v;
		[[fallthrough]];
		// fall through
	case 0x10: case 0x12: case 0x14:
	{
		int n = (r - 0x0E) >> 1;
		bgvofs[n] = (u16) (((v << 8) | bgLatch) & 0x3FF);
		bgLatch = v;
		break;
	}
	case 0x15: vmain = v; break;
	case 0x16: vramAddr = (u16) ((vramAddr & 0xFF00) | v); vramBuf = vword (vram, vramRemap ()); break;
	case 0x17: vramAddr = (u16) ((vramAddr & 0x00FF) | v << 8); vramBuf = vword (vram, vramRemap ()); break;
	case 0x18: vram[(vramRemap () & 0x7FFF) << 1] = v; vramStep (false); break;
	case 0x19: vram[((vramRemap () & 0x7FFF) << 1) + 1] = v; vramStep (true); break;
	case 0x1A: m7sel = v; break;
	case 0x1B: m7a = (s16) (v << 8 | m7Latch); m7Latch = v; break;
	case 0x1C: m7b = (s16) (v << 8 | m7Latch); m7Latch = v; break;
	case 0x1D: m7c = (s16) (v << 8 | m7Latch); m7Latch = v; break;
	case 0x1E: m7d = (s16) (v << 8 | m7Latch); m7Latch = v; break;
	case 0x1F: m7x = (s16) ((s16) ((v << 8 | m7Latch) << 3) >> 3); m7Latch = v; break;
	case 0x20: m7y = (s16) ((s16) ((v << 8 | m7Latch) << 3) >> 3); m7Latch = v; break;
	case 0x21: cgAddr = (u16) (v << 1); break;
	case 0x22:
		if (!(cgAddr & 1)) cgLatch = v;
		else { cgram[cgAddr - 1] = cgLatch; cgram[cgAddr] = v & 0x7F; }
		cgAddr = (u16) ((cgAddr + 1) & 0x1FF);
		break;
	case 0x23: w12sel = v; break;
	case 0x24: w34sel = v; break;
	case 0x25: wobjsel = v; break;
	case 0x26: case 0x27: case 0x28: case 0x29: wh[r - 0x26] = v; break;
	case 0x2A: wbglog = v; break;
	case 0x2B: wobjlog = v; break;
	case 0x2C: tm = v; break;
	case 0x2D: ts = v; break;
	case 0x2E: tmw = v; break;
	case 0x2F: tsw = v; break;
	case 0x30: cgwsel = v; break;
	case 0x31: cgadsub = v; break;
	case 0x32:
		if (v & 0x20) fixedColor = (u16) ((fixedColor & ~0x001F) | (v & 0x1F));
		if (v & 0x40) fixedColor = (u16) ((fixedColor & ~0x03E0) | (v & 0x1F) << 5);
		if (v & 0x80) fixedColor = (u16) ((fixedColor & ~0x7C00) | (v & 0x1F) << 10);
		break;
	case 0x33: setini = v; break;
	}
}

// ---- the line ------------------------------------------------------------------------------------
static inline u16 cgColor (const u8 *cg, int i) { return (u16) (cg[i << 1] | cg[(i << 1) + 1] << 8); }
static inline u16 directColor (int p, int pal)
{
	return (u16) ((((p & 7) << 2) | ((pal & 1) << 1)) | ((((p >> 3) & 7) << 2 | (pal & 2)) << 5) | ((((p >> 6) & 3) << 3 | (pal & 4)) << 10));
}

bool *Machine::window (int layer)
{
	bool *m = winMask[layer];
	if (winValid[layer]) return m;
	winValid[layer] = true;
	u8 sel, logic;
	if (layer < 4) { sel = (u8) (((layer < 2 ? w12sel : w34sel) >> ((layer & 1) * 4)) & 0xF); logic = (u8) ((wbglog >> (layer * 2)) & 3); }
	else { sel = (u8) ((wobjsel >> ((layer - 4) * 4)) & 0xF); logic = (u8) ((wobjlog >> ((layer - 4) * 2)) & 3); }
	bool e1 = sel & 2, i1 = sel & 1, e2 = sel & 8, i2 = sel & 4;
	if (!e1 && !e2) { for (int x = 0; x < 256; x++) m[x] = false; return m; }
	for (int x = 0; x < 256; x++)
	{
		bool a = (x >= wh[0] && x <= wh[1]) != i1, b = (x >= wh[2] && x <= wh[3]) != i2, r;
		if (!e1 && !e2) r = false;
		else if (!e2) r = a;
		else if (!e1) r = b;
		else r = logic == 0 ? (a || b) : logic == 1 ? (a && b) : logic == 2 ? (a != b) : (a == b);
		m[x] = r;
	}
	return m;
}

void Machine::renderBg (int n, int y, int bpp)
{
	u16 *col = bgCol[n]; u8 *pri = bgPri[n];
	int mode = bgmode & 7;
	bool hires = mode == 5 || mode == 6;
	bool big = bgmode & (0x10 << n);
	int tws = (big || hires) ? 4 : 3, ths = big ? 4 : 3;
	u16 mapBase = (u16) ((bgsc[n] & 0xFC) << 8);
	bool wide = bgsc[n] & 1, tall = bgsc[n] & 2;
	u16 chrBase = (u16) (((bgnba[n >> 1] >> ((n & 1) * 4)) & 0xF) << 12);
	int msize = (mosaic >> 4) + 1;
	bool mos = (mosaic & (1 << n)) && msize > 1;
	int yy = mos ? y - (y - 1) % msize : y;
	int palBase = mode == 0 ? n * 32 : 0;
	bool direct = bpp == 8 && (cgwsel & 1);
	bool opt = (mode == 2 || mode == 4 || mode == 6) && n < 2;
	int wpt = bpp * 4;					// words per 8x8 character
	u16 hofs0 = bghofs[n], vofs0 = bgvofs[n];
	if (!mos && !opt && !hires)
	{
		// the common case: a tile row (8 pixels) at a time
		int vy = (vofs0 + yy) & 0x3FF, hx = hofs0 & 0x3FF;
		int ty = vy >> ths, fy0 = vy & ((1 << ths) - 1);
		u16 rowBase = (u16) (mapBase + ((ty & 31) << 5));
		if ((ty & 32) && tall) rowBase = (u16) (rowBase + (wide ? 0x800 : 0x400));
		int x = 0;
		while (x < 256)
		{
			int tx = hx >> tws;
			u16 a = (u16) (rowBase + (tx & 31));
			if ((tx & 32) && wide) a = (u16) (a + 0x400);
			u16 e = vword (vram, a);
			int fx = hx & ((1 << tws) - 1), fy = fy0;
			bool hf = e & 0x4000;
			if (hf) fx = (1 << tws) - 1 - fx;
			if (e & 0x8000) fy = (1 << ths) - 1 - fy;
			int ch = (e & 0x3FF) + (fx >> 3) + ((fy >> 3) << 4);
			u32 ad = chrBase + (u32) ch * wpt + (fy & 7);
			u32 w0 = vword (vram, ad), w1 = 0, w2 = 0, w3 = 0;
			if (bpp >= 4) w1 = vword (vram, ad + 8);
			if (bpp == 8) { w2 = vword (vram, ad + 16); w3 = vword (vram, ad + 24); }
			int start = hx & 7, cnt = 8 - start;
			if (cnt > 256 - x) cnt = 256 - x;
			u8 cpri = (u8) ((e >> 13) & 1);
			int cpal = (e >> 10) & 7;
			int pbase = palBase + (cpal << bpp);
			for (int k = 0; k < cnt; k++)
			{
				int b = start + k;
				int sh = hf ? b : 7 - b;
				int p = ((w0 >> sh) & 1) | ((w0 >> (sh + 7)) & 2);
				if (bpp >= 4) p |= ((w1 >> sh) & 1) << 2 | ((w1 >> (sh + 8)) & 1) << 3;
				if (bpp == 8) p |= ((w2 >> sh) & 1) << 4 | ((w2 >> (sh + 8)) & 1) << 5 | ((w3 >> sh) & 1) << 6 | ((w3 >> (sh + 8)) & 1) << 7;
				int xx = x + k;
				if (!p) { pri[xx] = 0xFF; continue; }
				pri[xx] = cpri;
				if (direct) col[xx] = directColor (p, cpal);
				else if (bpp == 8) col[xx] = cgColor (cgram, p);
				else col[xx] = cgColor (cgram, pbase + p);
			}
			x += cnt;
			hx = (hx + cnt) & 0x3FF;
		}
		return;
	}
	int lastKey = -1; u8 pix[8]; int cpal = 0, cpri = 0; bool cflip = false;
	for (int x = 0; x < 256; x++)
	{
		int sx = mos ? x - x % msize : x;
		u16 ho = hofs0, vo = vofs0;
		if (opt)
		{
			int c = (sx + (ho & 7)) >> 3;
			if (c >= 1)
			{
				u16 h3 = bghofs[2], v3 = bgvofs[2];
				u16 m3 = (u16) ((bgsc[2] & 0xFC) << 8);
				int tc = ((c - 1) + (h3 >> 3)) & 63, tr = (v3 >> 3) & 63;
				auto ent = [&] (int cx, int cy) -> u16 {
					u16 a = (u16) (m3 + ((cy & 31) << 5) + (cx & 31));
					if ((cx & 32) && (bgsc[2] & 1)) a = (u16) (a + 0x400);
					if ((cy & 32) && (bgsc[2] & 2)) a = (u16) (a + ((bgsc[2] & 1) ? 0x800 : 0x400));
					return vword (vram, a);
				};
				u16 hv = ent (tc, tr), vv;
				u16 bitv = (u16) (0x2000 << n);
				if (mode == 4)
				{
					if (hv & bitv) { if (hv & 0x8000) vo = (u16) (hv & 0x3FF); else ho = (u16) ((hv & 0x3F8) | (ho & 7)); }
				}
				else
				{
					vv = ent (tc, tr + 1);
					if (hv & bitv) ho = (u16) ((hv & 0x3F8) | (ho & 7));
					if (vv & bitv) vo = (u16) (vv & 0x3FF);
				}
			}
		}
		int hx = hires ? ((ho << 1) + sx * 2) : (ho + sx);
		int vy = (vo + yy) & 0x3FF;
		hx &= hires ? 0x7FF : 0x3FF;
		int key = (hx >> 3) | (vy << 11);
		if (key != lastKey)
		{
			lastKey = key;
			int tx = hx >> tws, ty = vy >> ths;
			u16 a = (u16) (mapBase + ((ty & 31) << 5) + (tx & 31));
			if ((tx & 32) && wide) a = (u16) (a + 0x400);
			if ((ty & 32) && tall) a = (u16) (a + (wide ? 0x800 : 0x400));
			u16 e = vword (vram, a);
			int fx = hx & ((1 << tws) - 1), fy = vy & ((1 << ths) - 1);
			cflip = e & 0x4000;
			if (cflip) fx = (1 << tws) - 1 - fx;
			if (e & 0x8000) fy = (1 << ths) - 1 - fy;
			int ch = (e & 0x3FF) + (fx >> 3) + ((fy >> 3) << 4);
			u32 ad = chrBase + (u32) ch * wpt + (fy & 7);
			u16 w0 = vword (vram, ad), w1 = 0, w2 = 0, w3 = 0;
			if (bpp >= 4) w1 = vword (vram, ad + 8);
			if (bpp == 8) { w2 = vword (vram, ad + 16); w3 = vword (vram, ad + 24); }
			for (int b = 0; b < 8; b++)
			{
				int s = 7 - b;
				int p = ((w0 >> s) & 1) | ((w0 >> (s + 7)) & 2);
				if (bpp >= 4) p |= ((w1 >> s) & 1) << 2 | ((w1 >> (s + 8)) & 1) << 3;
				if (bpp == 8) p |= ((w2 >> s) & 1) << 4 | ((w2 >> (s + 8)) & 1) << 5 | ((w3 >> s) & 1) << 6 | ((w3 >> (s + 8)) & 1) << 7;
				pix[b] = (u8) p;
			}
			cpal = (e >> 10) & 7; cpri = (e >> 13) & 1;
		}
		int fb8 = hx & 7;
		int p = pix[cflip ? 7 - fb8 : fb8];
		if (!p) { pri[x] = 0xFF; continue; }
		pri[x] = (u8) cpri;
		if (direct) col[x] = directColor (p, cpal);
		else if (bpp == 8) col[x] = cgColor (cgram, p);
		else col[x] = cgColor (cgram, palBase + (cpal << bpp) + p);
	}
}

void Machine::renderMode7 (int y)
{
	auto clip = [] (int v) -> int { return (v & 0x2000) ? (v | ~1023) : (v & 1023); };
	int msize = (mosaic >> 4) + 1;
	bool mos1 = (mosaic & 1) && msize > 1, mos2 = (mosaic & 2) && msize > 1;
	int yy = (mos1 || mos2) ? y - (y - 1) % msize : y;
	int a = m7a, b = m7b, c = m7c, d = m7d, cx = m7x, cy = m7y, ho = m7hofs, vo = m7vofs;
	int ly = (m7sel & 2) ? 255 - yy : yy;
	int psx = ((a * clip (ho - cx)) & ~63) + ((b * clip (vo - cy)) & ~63) + ((b * ly) & ~63) + (cx << 8);
	int psy = ((c * clip (ho - cx)) & ~63) + ((d * clip (vo - cy)) & ~63) + ((d * ly) & ~63) + (cy << 8);
	bool ext = setini & 0x40, direct = cgwsel & 1;
	int over = m7sel >> 6;
	for (int x = 0; x < 256; x++)
	{
		int sx = (mos1 || mos2) ? x - x % msize : x;
		int lx = (m7sel & 1) ? 255 - sx : sx;
		int px = (psx + a * lx) >> 8, py = (psy + c * lx) >> 8;
		int p;
		if ((px | py) & ~1023)
		{
			if (over == 2) { bgPri[0][x] = bgPri[1][x] = 0xFF; continue; }
			int tile = over == 3 ? 0 : vram[(((py >> 3) & 127) * 128 + ((px >> 3) & 127)) << 1];
			p = vram[((tile * 64 + (py & 7) * 8 + (px & 7)) << 1) + 1];
		}
		else
		{
			int tile = vram[(((py >> 3) & 127) * 128 + ((px >> 3) & 127)) << 1];
			p = vram[((tile * 64 + (py & 7) * 8 + (px & 7)) << 1) + 1];
		}
		if (p) { bgPri[0][x] = 0; bgCol[0][x] = direct ? directColor (p, 0) : cgColor (cgram, p); }
		else bgPri[0][x] = 0xFF;
		if (ext)
		{
			int q = p & 0x7F;
			if (q) { bgPri[1][x] = (u8) (p >> 7); bgCol[1][x] = cgColor (cgram, q); }
			else bgPri[1][x] = 0xFF;
		}
	}
}

void Machine::renderObj (int y)
{
	static const u8 SW[8] = { 8, 8, 8, 16, 16, 32, 16, 16 }, SH[8] = { 8, 8, 8, 16, 16, 32, 32, 32 };
	static const u8 LW[8] = { 16, 32, 64, 32, 64, 64, 32, 32 }, LH[8] = { 16, 32, 64, 32, 64, 64, 64, 32 };
	for (int x = 0; x < 256; x++) objPri[x] = 0xFF;
	int sz = obsel >> 5;
	int first = oamPrioRot ? (oamReload >> 2) & 0x7F : 0;
	u8 list[32]; int cnt = 0;
	for (int i = 0; i < 128; i++)
	{
		int n = (first + i) & 127;
		const u8 *o = oam + n * 4;
		u8 hi = (u8) (oam[0x200 + (n >> 2)] >> ((n & 3) * 2));
		int w = (hi & 2) ? LW[sz] : SW[sz], h = (hi & 2) ? LH[sz] : SH[sz];
		int sx = o[0] | (hi & 1) << 8; if (sx >= 256) sx -= 512;
		int row = (y - 1 - o[1]) & 0xFF;
		if (row >= h) continue;
		if (sx <= -w || sx >= 256) continue;
		if (cnt == 32) { stat77 |= 0x40; break; }
		list[cnt++] = (u8) n;
	}
	u32 nameBase = (u32) (obsel & 7) << 13, gap = (u32) (((obsel >> 3) & 3) + 1) << 12;
	for (int k = cnt - 1; k >= 0; k--)
	{
		int n = list[k];
		const u8 *o = oam + n * 4;
		u8 hi = (u8) (oam[0x200 + (n >> 2)] >> ((n & 3) * 2));
		int w = (hi & 2) ? LW[sz] : SW[sz], h = (hi & 2) ? LH[sz] : SH[sz];
		int sx = o[0] | (hi & 1) << 8; if (sx >= 256) sx -= 512;
		int row = (y - 1 - o[1]) & 0xFF;
		u8 attr = o[3];
		if (attr & 0x80) row = h - 1 - row;
		bool hf = attr & 0x40;
		int prio = (attr >> 4) & 3, pal = (attr >> 1) & 7;
		u32 base = nameBase + ((attr & 1) ? gap : 0);
		int tiles = w >> 3;
		for (int c = 0; c < tiles; c++)
		{
			int x0 = sx + c * 8;
			if (x0 <= -8 || x0 >= 256) continue;
			int tx = hf ? tiles - 1 - c : c;
			int chx = (o[2] + tx) & 0x0F, chy = ((o[2] >> 4) + (row >> 3)) & 0x0F;
			u32 ad = base + (u32) (chy * 16 + chx) * 16 + (row & 7);
			u16 w0 = vword (vram, ad), w1 = vword (vram, ad + 8);
			for (int b = 0; b < 8; b++)
			{
				int xx = x0 + b;
				if (xx < 0 || xx >= 256) continue;
				int s = hf ? b : 7 - b;
				int p = ((w0 >> s) & 1) | ((w0 >> (s + 7)) & 2) | ((w1 >> s) & 1) << 2 | ((w1 >> (s + 8)) & 1) << 3;
				if (!p) continue;
				objCol[xx] = cgColor (cgram, 128 + pal * 16 + p);
				objPri[xx] = (u8) prio;
				objMath[xx] = pal >= 4;
			}
		}
	}
}

// The layers' order, front to back, per mode: 0x10 | p = the sprites of priority p,
// (bg << 1) | p = background bg's tiles of priority p.
static const u8 ORDER0[] = { 0x13, 0x01, 0x03, 0x12, 0x00, 0x02, 0x11, 0x05, 0x07, 0x10, 0x04, 0x06 };
static const u8 ORDER1H[] = { 0x05, 0x13, 0x01, 0x03, 0x12, 0x00, 0x02, 0x11, 0x10, 0x04 };
static const u8 ORDER1[] = { 0x13, 0x01, 0x03, 0x12, 0x00, 0x02, 0x11, 0x05, 0x10, 0x04 };
static const u8 ORDER2[] = { 0x13, 0x01, 0x12, 0x03, 0x11, 0x00, 0x10, 0x02 };
static const u8 ORDER6[] = { 0x13, 0x01, 0x12, 0x11, 0x00, 0x10 };
static const u8 ORDER7[] = { 0x13, 0x12, 0x03, 0x11, 0x00, 0x10, 0x02 };

void Machine::renderLine (int y)
{
	if (y > H) return;
	u32 *out = fb + (y - 1) * W;
	if (inidisp & 0x80) { for (int x = 0; x < W; x++) out[x] = 0; return; }
	for (int i = 0; i < 6; i++) winValid[i] = false;
	int mode = bgmode & 7;
	static const u8 BPP[8][4] = { {2,2,2,2}, {4,4,2,0}, {4,4,0,0}, {8,4,0,0}, {8,2,0,0}, {4,2,0,0}, {4,0,0,0}, {0,0,0,0} };
	u8 used = (u8) (tm | ts);
	u8 have = 0;
	if (mode == 7)
	{
		if (used & 3) { renderMode7 (y); have = (setini & 0x40) ? 3 : 1; }
	}
	else
		for (int n = 0; n < 4; n++)
			if (BPP[mode][n] && (used & (1 << n))) { renderBg (n, y, BPP[mode][n]); have |= (u8) (1 << n); }
	if (used & 0x10) renderObj (y);

	// depth of each (layer, priority): higher = in front
	u8 dBg[4][2] = { {0,0}, {0,0}, {0,0}, {0,0} }, dObj[4] = { 0, 0, 0, 0 };
	const u8 *ord; int nOrd;
	switch (mode)
	{
	case 0: ord = ORDER0; nOrd = sizeof ORDER0; break;
	case 1: if (bgmode & 8) { ord = ORDER1H; nOrd = sizeof ORDER1H; } else { ord = ORDER1; nOrd = sizeof ORDER1; } break;
	case 6: ord = ORDER6; nOrd = sizeof ORDER6; break;
	case 7: ord = ORDER7; nOrd = sizeof ORDER7; break;
	default: ord = ORDER2; nOrd = sizeof ORDER2; break;
	}
	for (int i = 0; i < nOrd; i++)
	{
		u8 e = ord[i], dd = (u8) (nOrd - i);
		if (e & 0x10) dObj[e & 3] = dd; else dBg[e >> 1][e & 1] = dd;
	}
	if (mode == 7) dBg[0][1] = dBg[0][0];

	static u16 mainC[256], subC[256]; static u8 mainZ[256], subZ[256], mainL[256], subL[256];
	u16 back = cgColor (cgram, 0);
	for (int x = 0; x < 256; x++) { mainC[x] = back; mainZ[x] = 0; mainL[x] = 5; subC[x] = fixedColor; subZ[x] = 0; subL[x] = 5; }
	for (int s = 0; s < 2; s++)
	{
		u8 en = s ? ts : tm, wen = s ? tsw : tmw;
		u16 *cc = s ? subC : mainC; u8 *zz = s ? subZ : mainZ, *ll = s ? subL : mainL;
		for (int n = 0; n < 4; n++)
		{
			if (!(en & have & (1 << n))) continue;
			const bool *w = (wen & (1 << n)) ? window (n) : 0;
			const u8 *pp = bgPri[n]; const u16 *pc = bgCol[n];
			u8 d0 = dBg[n][0], d1 = dBg[n][1];
			for (int x = 0; x < 256; x++)
			{
				u8 p = pp[x];
				if (p == 0xFF || (w && w[x])) continue;
				u8 d = p ? d1 : d0;
				if (d > zz[x]) { zz[x] = d; cc[x] = pc[x]; ll[x] = (u8) n; }
			}
		}
		if (en & 0x10)
		{
			const bool *w = (wen & 0x10) ? window (4) : 0;
			for (int x = 0; x < 256; x++)
			{
				u8 p = objPri[x];
				if (p == 0xFF || (w && w[x])) continue;
				u8 d = dObj[p];
				if (d > zz[x]) { zz[x] = d; cc[x] = objCol[x]; ll[x] = objMath[x] ? 4 : 6; }
			}
		}
	}

	const bool *cw = window (5);
	int clipMode = cgwsel >> 6, prevMode = (cgwsel >> 4) & 3;
	bool useSub = cgwsel & 2, subtract = cgadsub & 0x80, halfOn = cgadsub & 0x40;
	const u32 *lut = colorLut[inidisp & 15];
	for (int x = 0; x < 256; x++)
	{
		u16 c = mainC[x];
		bool in = cw[x];
		bool clip = clipMode == 3 || (clipMode == 2 && in) || (clipMode == 1 && !in);
		bool noMath = prevMode == 3 || (prevMode == 2 && in) || (prevMode == 1 && !in);
		if (clip) c = 0;
		u8 l = mainL[x];
		if (!noMath && l != 6 && (cgadsub & (1 << l)))
		{
			u16 o = useSub ? subC[x] : fixedColor;
			bool half = halfOn && !clip && !(useSub && subL[x] == 5);
			int r = c & 31, g = (c >> 5) & 31, b = c >> 10;
			int r2 = o & 31, g2 = (o >> 5) & 31, b2 = o >> 10;
			if (subtract) { r -= r2; g -= g2; b -= b2; if (r < 0) r = 0; if (g < 0) g = 0; if (b < 0) b = 0; if (half) { r >>= 1; g >>= 1; b >>= 1; } }
			else { r += r2; g += g2; b += b2; if (half) { r >>= 1; g >>= 1; b >>= 1; } if (r > 31) r = 31; if (g > 31) g = 31; if (b > 31) b = 31; }
			c = (u16) (r | g << 5 | b << 10);
		}
		out[x] = lut[c & 31] << 16 | lut[(c >> 5) & 31] << 8 | lut[(c >> 10) & 31];
	}
}

} // namespace snes
