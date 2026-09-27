//
// nes/nes_ppu.cpp -- the 2C02: its registers, its memory (the cartridge's pattern tables, the
// nametables with their mirroring, the palette), and the picture. The PPU advances dot by
// dot (341 a line; 262 lines NTSC, 312 PAL) for the events whose timing games depend on --
// vblank and the NMI, the sprite 0 hit, the scroll copies of the "loopy" registers, the
// MMC3's scanline counter -- and draws each visible line at its start from the scroll then
// in force (a split made in the middle of a line shows from the next line).
//
#include "nes/nes.h"

namespace nes {

// the 2C02's 64 colours (the widely used FCEUX palette)
static const u32 NES_RGB[64] = {
	0x747474, 0x24188C, 0x0000A8, 0x44009C, 0x8C0074, 0xA80010, 0xA40000, 0x7C0800,
	0x402C00, 0x004400, 0x005000, 0x003C14, 0x183C5C, 0x000000, 0x000000, 0x000000,
	0xBCBCBC, 0x0070EC, 0x2038EC, 0x8000F0, 0xBC00BC, 0xE40058, 0xD82800, 0xC84C0C,
	0x887000, 0x009400, 0x00A800, 0x009038, 0x008088, 0x000000, 0x000000, 0x000000,
	0xFCFCFC, 0x3CBCFC, 0x5C94FC, 0xCC88FC, 0xF478FC, 0xFC74B4, 0xFC7460, 0xFC9838,
	0xF0BC3C, 0x80D010, 0x4CDC48, 0x58F898, 0x00E8D8, 0x787878, 0x000000, 0x000000,
	0xFCFCFC, 0xA8E4FC, 0xC4D4FC, 0xD4C8FC, 0xFCC4FC, 0xFCC4D8, 0xFCBCB0, 0xFCD8A8,
	0xFCE4A0, 0xE0FCA0, 0xA8F0BC, 0xB0FCCC, 0x9CFCF0, 0xC4C4C4, 0x000000, 0x000000 };

const u8 *Machine::chrPtr (u16 ad) const
{
	const u8 *base = chrIsRam ? chrRam : chrRom;
	return base + chrMap[(ad >> 10) & 7] + (ad & 0x3FF);
}

u8 *Machine::ntPtr (u16 ad)
{
	u32 idx = (u32) (ad - 0x2000) & 0xFFF, table = idx >> 10, m;
	switch (mirror)
	{
	case 0: m = table >> 1; break;				// horizontal
	case 1: m = table & 1; break;				// vertical
	case 2: m = 0; break;					// one screen
	case 3: m = 1; break;
	default: m = table; break;				// four screens
	}
	return &ciram[(m << 10) | (idx & 0x3FF)];
}

u8 Machine::ppuRead (u16 ad)
{
	ad &= 0x3FFF;
	if (ad < 0x2000) return *chrPtr (ad);
	if (ad < 0x3F00) return *ntPtr (ad);
	u32 i = ad & 0x1F; if ((i & 0x13) == 0x10) i &= 0x0F;
	return palRam[i];
}

void Machine::ppuWrite (u16 ad, u8 val)
{
	ad &= 0x3FFF;
	if (ad < 0x2000) { if (chrIsRam) chrRam[chrMap[(ad >> 10) & 7] + (ad & 0x3FF)] = val; return; }
	if (ad < 0x3F00) { *ntPtr (ad) = val; return; }
	u32 i = ad & 0x1F; if ((i & 0x13) == 0x10) i &= 0x0F;
	palRam[i] = val & 0x3F;
}

// ---- the registers ($2000-$2007) -------------------------------------------------------------------
u8 Machine::regRead (int r)
{
	switch (r)
	{
	case 2:
	{
		u8 st = (u8) ((status & 0xE0) | (openBus & 0x1F));
		status &= 0x7F;
		wLatch = false;
		if (line == 241 && dot == 0) nmiOccurred = false;	// (read just before vblank: no NMI this frame)
		openBus = st;
		return st;
	}
	case 4: openBus = oam[oamAddr]; return openBus;
	case 7:
	{
		u8 r7;
		if (!rendering () || line >= 240) a12Access (v);
		if ((v & 0x3FFF) < 0x3F00) { r7 = readBuf; readBuf = ppuRead (v); }
		else { r7 = ppuRead (v); readBuf = ppuRead ((u16) (v - 0x1000)); }
		v = (u16) ((v + ((ctrl & 4) ? 32 : 1)) & 0x7FFF);
		if (!rendering () || line >= 240) a12Access (v);
		openBus = r7;
		return r7;
	}
	}
	return openBus;
}

void Machine::regWrite (int r, u8 val)
{
	openBus = val;
	switch (r)
	{
	case 0:
	{
		bool wasOn = ctrl & 0x80;
		ctrl = val;
		t = (u16) ((t & 0xF3FF) | ((val & 3) << 10));
		if (!wasOn && (val & 0x80) && (status & 0x80)) nmiPending = true;	// (enabled during vblank)
		break;
	}
	case 1: mask = val; break;
	case 3: oamAddr = val; break;
	case 4: oam[oamAddr++] = val; break;
	case 5:
		if (!wLatch) { t = (u16) ((t & 0xFFE0) | (val >> 3)); fineX = val & 7; }
		else t = (u16) ((t & 0x8C1F) | ((val & 7) << 12) | ((val & 0xF8) << 2));
		wLatch = !wLatch;
		break;
	case 6:
		if (!wLatch) t = (u16) ((t & 0x00FF) | ((val & 0x3F) << 8));
		else { t = (u16) ((t & 0xFF00) | val); v = t; if (!rendering () || line >= 240) a12Access (v); }
		wLatch = !wLatch;
		break;
	case 7:
		if (!rendering () || line >= 240) a12Access (v);
		ppuWrite (v, val);
		v = (u16) ((v + ((ctrl & 4) ? 32 : 1)) & 0x7FFF);
		if (!rendering () || line >= 240) a12Access (v);
		break;
	}
}

// ---- timing ----------------------------------------------------------------------------------------
void Machine::ppuRun (int cpuCycles)
{
	if (!pal) { for (int i = cpuCycles * 3; i > 0; i--) ppuDot (); return; }
	dotAcc += (u64) cpuCycles * 16;					// PAL: 3.2 dots a CPU cycle
	while (dotAcc >= 5) { dotAcc -= 5; ppuDot (); }
}

void Machine::ppuDot ()
{
	int lastLine = pal ? 311 : 261;
	bool ren = rendering ();
	if (line < 240)
	{
		if (dot == 1) renderLine ();
		if (dot == sprite0Dot) status |= 0x40;
		if (ren)
		{
			if (dot == 256)					// vertical increment
			{
				if ((v & 0x7000) != 0x7000) v += 0x1000;
				else
				{
					v &= 0x8FFF;
					int cy = (v & 0x03E0) >> 5;
					if (cy == 29) { cy = 0; v ^= 0x0800; }
					else if (cy == 31) cy = 0;
					else cy++;
					v = (u16) ((v & ~0x03E0) | (cy << 5));
				}
			}
			else if (dot == 257) v = (u16) ((v & ~0x041F) | (t & 0x041F));
			else if (dot == 260 && (ctrl & 0x18) != 0x10) mmc3Clock ();	// (A12 rises: sprite fetches at $1000)
			else if (dot == 324 && (ctrl & 0x18) == 0x10) mmc3Clock ();	// (background at $1000)
		}
	}
	else if (line == 241 && dot == 1)
	{
		status |= 0x80;
		nmiOccurred = true;
		if (ctrl & 0x80) nmiPending = true;
		frameDone = true;
	}
	else if (line == lastLine)
	{
		if (dot == 1) { status &= 0x1F; sprite0Dot = -1; }
		if (ren)
		{
			if (dot == 257) v = (u16) ((v & ~0x041F) | (t & 0x041F));
			else if (dot == 280) v = (u16) ((v & ~0x7BE0) | (t & 0x7BE0));
			else if (dot == 260 && (ctrl & 0x18) != 0x10) mmc3Clock ();
			else if (dot == 324 && (ctrl & 0x18) == 0x10) mmc3Clock ();
			if (dot == 339 && oddFrame && !pal) dot++;	// (NTSC: the odd frames are a dot shorter)
		}
	}
	if (++dot > 340)
	{
		dot = 0;
		if (++line > lastLine) { line = 0; oddFrame = !oddFrame; }
		if (line < 240) sprite0Dot = -1;
	}
}

// ---- a line of the picture -----------------------------------------------------------------------------
void Machine::renderLine ()
{
	u32 *out = fb + line * W;
	u8 bg[W + 16];						// background pixel (0-3) and palette, per x
	u8 bgPal[W + 16];
	bool showBg = mask & 0x08, showSpr = mask & 0x10;
	if (showBg)
	{
		u16 vv = v;
		int fy = (vv >> 12) & 7;
		u16 base = (ctrl & 0x10) ? 0x1000 : 0;
		for (int tile = 0; tile < 33; tile++)
		{
			u8 nt = *ntPtr ((u16) (0x2000 | (vv & 0x0FFF)));
			u8 at = *ntPtr ((u16) (0x23C0 | (vv & 0x0C00) | ((vv >> 4) & 0x38) | ((vv >> 2) & 7)));
			int shift = ((vv >> 4) & 4) | (vv & 2);
			u8 pl = (u8) ((at >> shift) & 3);
			const u8 *pat = chrPtr ((u16) (base + nt * 16 + fy));
			u8 lo = pat[0], hi = pat[8];
			for (int b = 0; b < 8; b++)
			{
				int px = tile * 8 + b - fineX;
				if (px < 0 || px >= W) continue;
				bg[px] = (u8) (((lo >> (7 - b)) & 1) | (((hi >> (7 - b)) & 1) << 1));
				bgPal[px] = pl;
			}
			if ((vv & 0x1F) == 31) { vv &= ~0x1F; vv ^= 0x0400; } else vv++;
		}
		if (!(mask & 0x02)) for (int px = 0; px < 8; px++) bg[px] = 0;
	}
	else for (int px = 0; px < W; px++) bg[px] = 0;

	// sprites: the first 8 on this line; the lowest index wins a pixel
	u8 spr[W], sprAttr[W];
	for (int px = 0; px < W; px++) spr[px] = 0;
	if (showSpr && line > 0)
	{
		int h = (ctrl & 0x20) ? 16 : 8, found = 0;
		for (int i = 0; i < 64; i++)
		{
			int sy = oam[i * 4] + 1;
			int row = line - sy;
			if (row < 0 || row >= h) continue;
			if (++found > 8) { status |= 0x20; break; }	// (overflow)
			u8 tileNo = oam[i * 4 + 1], attr = oam[i * 4 + 2];
			int sx = oam[i * 4 + 3];
			if (attr & 0x80) row = h - 1 - row;			// vertical flip
			u16 ad;
			if (h == 16) ad = (u16) (((tileNo & 1) ? 0x1000 : 0) + (tileNo & 0xFE) * 16 + (row >= 8 ? 16 : 0) + (row & 7));
			else ad = (u16) (((ctrl & 0x08) ? 0x1000 : 0) + tileNo * 16 + row);
			const u8 *pat = chrPtr (ad);
			u8 lo = pat[0], hi = pat[8];
			for (int b = 0; b < 8; b++)
			{
				int px = sx + b;
				if (px >= W) break;
				int bit = (attr & 0x40) ? b : 7 - b;
				u8 c = (u8) (((lo >> bit) & 1) | (((hi >> bit) & 1) << 1));
				if (!c) continue;
				if (px < 8 && !(mask & 0x04)) continue;
				// sprite 0 hit: an opaque sprite-0 pixel over an opaque background pixel
				if (i == 0 && showBg && bg[px] && px != 255 && sprite0Dot < 0 && !(status & 0x40)) sprite0Dot = px + 2;
				if (spr[px]) continue;
				spr[px] = (u8) (c | ((attr & 3) << 2) | 0x10);
				sprAttr[px] = attr;
			}
		}
	}

	u8 grey = (mask & 1) ? 0x30 : 0x3F;
	for (int px = 0; px < W; px++)
	{
		u8 ci;
		u8 b = bg[px];
		if (spr[px] && (!b || !(sprAttr[px] & 0x20))) ci = palRam[0x10 | (spr[px] & 0x0F)];
		else if (b) ci = palRam[(bgPal[px] << 2) | b];
		else ci = palRam[0];
		out[px] = NES_RGB[ci & grey];
	}
}

} // namespace nes
