//
// n64/n64_bus.cpp -- the N64 outside the CPU: the physical address map, the RCP's interfaces
// (MI interrupts, VI video, AI audio DMA, PI cartridge DMA, SI + the PIF: the controllers and the
// EEPROM, RI, the RSP's SP registers and memories, the RDP's DP registers), the events (the VI
// lines, Count / Compare, the DMAs' ends), the boot (what the PIF and the game's IPL3 leave in
// memory and in the registers), the picture (the framebuffer the VI shows).
//
#include "n64/n64.h"

namespace n64 {

static inline u64 sx32 (u32 v) { return (u64) (s64) (s32) v; }
static void zero (void *p, u32 n) { u8 *d = (u8 *) p; while (n--) *d++ = 0; }

enum { VI_CLOCK_NTSC = 48681812, VI_CLOCK_PAL = 49656530, CPU_HZ = 93750000 };

Machine::Machine () : rdram (0), rom (0), romSize (0)
{
	rdram = new u32[RDRAM_SIZE / 4];
	for (int i = 0; i < 2; i++) { gfxFrame[i].v = 0; gfxFrame[i].b = 0; }
	for (int i = 0; i < MAX_TEX; i++) tex[i].px = 0;
	zero (sram, sizeof sram); zero (eeprom, sizeof eeprom);
	sramDirty = eepromDirty = false; eepromSize = 0; saveType = 0;
	pal = false; cic = 6102; title[0] = 0;
	outRate = 0; aHead = aTail = 0;
	for (int i = 0; i < 4; i++) { padBtn[i] = 0; padX[i] = padY[i] = 0; }
	reset ();
}

Machine::~Machine ()
{
	delete [] rdram; delete [] rom;
	for (int i = 0; i < 2; i++) { delete [] gfxFrame[i].v; delete [] gfxFrame[i].b; }
	for (int i = 0; i < MAX_TEX; i++) delete [] tex[i].px;
}

static u32 crc32 (const u8 *p, u32 n)
{
	u32 c = 0xFFFFFFFF;
	for (u32 i = 0; i < n; i++)
	{
		c ^= p[i];
		for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
	}
	return ~c;
}

bool Machine::load (const u8 *d, u32 size)
{
	if (size < 0x101000) return false;
	u32 magic = (u32) d[0] << 24 | (u32) d[1] << 16 | (u32) d[2] << 8 | d[3];
	int order;							// 0 z64, 1 v64 (16-bit swapped), 2 n64 (32-bit swapped)
	if (magic == 0x80371240) order = 0;
	else if (magic == 0x37804012) order = 1;
	else if (magic == 0x40123780) order = 2;
	else return false;
	delete [] rom;
	romSize = (size + 3) & ~3u;
	rom = new u32[romSize / 4];
	for (u32 i = 0; i < romSize / 4; i++)
	{
		u8 b[4];
		for (int k = 0; k < 4; k++) b[k] = 4 * i + (u32) k < size ? d[4 * i + (u32) k] : 0;
		if (order == 1) { u8 t = b[0]; b[0] = b[1]; b[1] = t; t = b[2]; b[2] = b[3]; b[3] = t; }
		else if (order == 2) { u8 t = b[0]; b[0] = b[3]; b[3] = t; t = b[1]; b[1] = b[2]; b[2] = t; }
		rom[i] = (u32) b[0] << 24 | (u32) b[1] << 16 | (u32) b[2] << 8 | b[3];
	}
	// the header: the name, the region; the boot chip from its IPL3
	for (int i = 0; i < 20; i++) title[i] = (char) ((rom[(0x20 + i) >> 2] >> (24 - 8 * ((0x20 + i) & 3))) & 0xFF);
	title[20] = 0;
	for (int i = 19; i >= 0 && (title[i] == ' ' || title[i] == 0); i--) title[i] = 0;
	u8 region = (u8) (rom[0x3C >> 2] >> 8);
	pal = region == 'D' || region == 'F' || region == 'I' || region == 'P' || region == 'S' || region == 'U'
	   || region == 'X' || region == 'Y';
	u8 ipl[0xFC0];
	for (u32 i = 0; i < 0xFC0; i++) ipl[i] = (u8) (rom[(0x40 + i) >> 2] >> (24 - 8 * ((0x40 + i) & 3)));
	switch (crc32 (ipl, 0xFC0))
	{
	case 0x6170A4A1: cic = 6101; break;
	case 0x0B050EE0: cic = 6103; break;
	case 0x98BC2C86: cic = 6105; break;
	case 0xACC8580A: cic = 6106; break;
	default: cic = 6102; break;
	}
	reset ();
	return true;
}

void Machine::reset ()
{
	zero (r, sizeof r); hi = lo = 0;
	zero (cp0, sizeof cp0); zero (fpr, sizeof fpr); fcr31 = 0; llbit = false;
	zero (tlb, sizeof tlb); lastTlb = 0; tlbMissVector = 0; badVa = 0;
	zero (rdram, RDRAM_SIZE); zero (dmem, sizeof dmem); zero (imem, sizeof imem); zero (pifRam, sizeof pifRam);
	zero (mi, sizeof mi); zero (vi, sizeof vi); zero (ai, sizeof ai); zero (pi, sizeof pi); zero (si, sizeof si);
	zero (ri, sizeof ri); zero (sp, sizeof sp); zero (dp, sizeof dp); zero (rdramReg, sizeof rdramReg);
	mi[1] = 0x02020102;
	sp[4] = 1;							// halted
	spPc = 0;
	ri[0] = 0x0E; ri[1] = 0x40; ri[3] = 0x14; ri[4] = 0x00063634;
	pi[2] = pi[3] = 0x7F;
	cycles = 0; countBase = 0; frames = 0;
	for (int i = 0; i < EV_N; i++) evAt[i] = ~0ull;
	nextEvent = ~0ull;
	excPending = false; inDelay = nextDelay = false; halted = false; haltMsg[0] = 0;
	aiCount = 0;
	for (int i = 0; i < 8; i++) rspTasks[i] = 0;
	for (int i = 0; i < 6; i++) irqCount[i] = 0;
	lastTask = 0;
	audioTasks = audioUnknown = 0;
	resAcc = 0; lastL = lastR = 0; aIn = aOut = aCount = aLoop = 0;
	zero (aDmem, sizeof aDmem); zero (aBook, sizeof aBook); zero (aEnvV, sizeof aEnvV); zero (aEnvS, sizeof aEnvS);
	for (int i = 0; i < 32; i++) excCount[i] = 0;
	viLineNow = 0; viLines = pal ? 313 : 263;
	viCyclesLine = (u64) ((pal ? CPU_HZ / 50 : CPU_HZ / 60) / viLines);
	frameDone = false;
	fbW = 320; fbH = 240;
	zero (fb, sizeof fb);
	cp0[12] = 0x34000000; cp0[16] = 0x7006E463; cp0[15] = 0x00000B22;
	schedule (EV_VI, viCyclesLine);
	scheduleCompare ();
	gfxInit ();
	if (rom) bootHle ();
}

// What the PIF ROM and the cartridge's IPL3 would have done: the first megabyte of the game in
// RDRAM at its entry point, the IPL3 in DMEM, the system variables at 0x80000300, the registers.
void Machine::bootHle ()
{
	u32 entry = rom[2];
	if (cic == 6103) entry -= 0x100000;
	else if (cic == 6106) entry -= 0x200000;
	u32 dst = entry & 0x1FFFFFFF;
	for (u32 i = 0; i < 0x100000; i += 4)
		if (dst + i < RDRAM_SIZE && 0x1000 + i < romSize) rdram[(dst + i) >> 2] = rom[(0x1000 + i) >> 2];
	for (u32 i = 0; i < 0x1000 / 4; i++) dmem[i] = rom[i];
	u32 tv = pal ? 0 : 1;
	u32 seed = cic == 6103 ? 0x78 : cic == 6105 ? 0x91 : cic == 6106 ? 0x85 : 0x3F;
	rdram[0x300 / 4] = tv;						// osTvType
	rdram[0x304 / 4] = 0;						// osRomType (cartridge)
	rdram[0x308 / 4] = 0xB0000000;					// osRomBase
	rdram[0x30C / 4] = 0;						// osResetType (cold)
	rdram[0x310 / 4] = cic == 6105 ? 5 : 2;				// osCicId
	rdram[0x314 / 4] = 0;						// osVersion
	rdram[0x318 / 4] = RDRAM_SIZE;					// osMemSize
	if (cic == 6105) rdram[0x3F0 / 4] = RDRAM_SIZE;
	if (cic == 6105)						// what the 6105's IPL3 leaves in IMEM
	{
		static const u32 I[8] = { 0x3C0DBFC0, 0x8DA807FC, 0x25AD07C0, 0x31080080, 0x5500FFFC, 0x3C0DBFC0, 0x8DA80024, 0x3C0BB000 };
		for (int i = 0; i < 8; i++) imem[i] = I[i];
	}
	r[11] = sx32 (0xA4000040);					// t3
	r[19] = 0;							// s3: the ROM type
	r[20] = tv;							// s4
	r[21] = 0;							// s5: reset type
	r[22] = seed;							// s6
	r[23] = 0;							// s7
	r[29] = sx32 (0xA4001FF0);					// sp
	r[31] = sx32 (0xA4001550);					// ra
	u32 hdr = rom[0];						// the PI timings of domain 1
	pi[5] = hdr & 0xFF; pi[6] = (hdr >> 8) & 0xFF; pi[7] = (hdr >> 16) & 0x0F; pi[8] = (hdr >> 20) & 0x03;
	// the VI as the IPL3 leaves it (blank), the RI / RDRAM as initialised
	cp0[13] = 0xB000007C;						// Cause, as the boot leaves it
	cp0[17] = 0xFFFFFFFF;						// LLAddr (not initialised: what a console shows)
	pi[0] = dst + 0x100000; pi[1] = 0x1000000C;			// (the IPL3's last DMA, its last read)
	pc = entry; npc = pc + 4;
}

// ---- the frame -----------------------------------------------------------------------------------------------------
void Machine::runFrame ()
{
	frameDone = false;
	u64 guard = cycles + (u64) CPU_HZ;				// (a second at most: no VI set up)
	while (!frameDone && !halted && cycles < guard)
	{
		while (cycles < nextEvent) step ();
		events ();
	}
	if (!frameDone) viOutput ();
	frames++;
}

void Machine::events ()
{
	for (int e = 0; e < EV_N; e++)
	{
		if (evAt[e] > cycles) continue;
		evAt[e] = ~0ull;
		switch (e)
		{
		case EV_VI: viLine (); schedule (EV_VI, viCyclesLine); break;
		case EV_COMPARE:
			cp0[13] |= 0x8000;
			checkInterrupts ();
			scheduleCompare ();
			break;
		case EV_PI: pi[4] &= ~3u; raise (MI_PI); break;
		case EV_SI: si[6] &= ~1u; si[6] |= 0x1000; raise (MI_SI); break;
		case EV_AI:
			// the buffer playing is done: the next one starts
			if (aiCount > 0) { aiFifo[0][0] = aiFifo[1][0]; aiFifo[0][1] = aiFifo[1][1]; aiCount--; }
			raise (MI_AI);
			if (aiCount > 0) aiPush ();
			break;
		case EV_SP: raise (MI_SP); break;
		case EV_DP: raise (MI_DP); break;
		}
	}
	nextEvent = ~0ull;
	for (int e = 0; e < EV_N; e++) if (evAt[e] < nextEvent) nextEvent = evAt[e];
}

void Machine::raise (u32 bits) { for (int k = 0; k < 6; k++) if (bits & (1u << k)) irqCount[k]++; mi[2] |= bits; checkInterrupts (); }
void Machine::lower (u32 bits) { mi[2] &= ~bits; checkInterrupts (); }

// ---- the VI -------------------------------------------------------------------------------------------------------------
void Machine::viLine ()
{
	viLineNow += 1;
	u32 vsync = vi[6] & 0x3FF;
	int lines = vsync ? (int) (vsync + 1) / 2 : (pal ? 313 : 263);
	if (lines != viLines && lines > 100)
	{
		viLines = lines;
		viCyclesLine = (u64) ((pal ? CPU_HZ / 50 : CPU_HZ / 60) / viLines);
	}
	if (viLineNow >= viLines) { viLineNow = 0; viOutput (); frameDone = true; }
	if ((u32) (viLineNow * 2) == (vi[3] & 0x3FE)) raise (MI_VI);
}

void Machine::viOutput ()
{
	u32 type = vi[0] & 3;
	u32 origin = vi[1] & 0xFFFFFF, width = vi[2] & 0xFFF;
	u32 hs = (vi[9] >> 16) & 0x3FF, he = vi[9] & 0x3FF, vs = (vi[10] >> 16) & 0x3FF, ve = vi[10] & 0x3FF;
	u32 xs = vi[12] & 0xFFF, ys = vi[13] & 0xFFF;
	int w = he > hs ? (int) ((he - hs) * xs / 1024) : 0;
	int h = ve > vs ? (int) (((ve - vs) / 2) * ys / 1024) : 0;
	h = (h + 7) & ~15;						// (237 -> 240, 474 -> 480: the whole framebuffer)
	if (type < 2 || w <= 0 || h <= 0 || width == 0)
	{
		for (int i = 0; i < fbW * fbH; i++) fb[i] = 0;
		return;
	}
	if (w > FB_MAX_W) w = FB_MAX_W;
	if (h > FB_MAX_H) h = FB_MAX_H;
	fbW = w; fbH = h;
	const u8 *m = (const u8 *) rdram;
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
		{
			u32 c;
			if (type == 2)
			{
				u32 a = origin + (u32) (y * (int) width + x) * 2;
				u32 p = a + 1 < RDRAM_SIZE ? *(const u16 *) (m + (a ^ 2)) : 0;
				u32 R = (p >> 11) & 31, G = (p >> 6) & 31, B = (p >> 1) & 31;
				c = (R << 19 | R << 14) & 0xFF0000; c |= (G << 11 | G << 6) & 0xFF00; c |= (B << 3 | B >> 2) & 0xFF;
			}
			else
			{
				u32 a = origin + (u32) (y * (int) width + x) * 4;
				u32 p = a + 3 < RDRAM_SIZE ? rdram[a >> 2] : 0;
				c = p >> 8;
			}
			fb[y * w + x] = c;
		}
}

// ---- the cartridge ---------------------------------------------------------------------------------------------------
u32 Machine::cartRead (u32 pa)
{
	if (pa >= 0x10000000 && pa < 0x1FC00000)
	{
		u32 o = pa - 0x10000000;
		if (o < romSize) return rom[o >> 2];
		return (o & 0xFFFF) | (o << 16);			// (open bus: the address)
	}
	if (pa >= 0x08000000 && pa < 0x08000000 + SRAM_SIZE)		// SRAM
	{
		u32 o = pa - 0x08000000;
		return (u32) sram[o] << 24 | (u32) sram[o + 1] << 16 | (u32) sram[o + 2] << 8 | sram[o + 3];
	}
	return 0;
}

static inline u8 byteOf (const u32 *words, u32 a) { return (u8) (words[a >> 2] >> (24 - 8 * (a & 3))); }

// The PI DMA as the hardware does it (after ares, ISC, from Rasky's research): the cartridge read
// by 16-bit halves, RDRAM written in blocks of up to 128 bytes that end at 2 KB rows; the first
// block of an unaligned destination loses its misalignment, a block's end is rounded up to 8
// bytes; the lengths read back afterwards (127 or 127 - misalign); the time from the domain's
// BSD registers.
//   Ported from ares (https://github.com/ares-emulator/ares, ares/n64/pi/dma.cpp), under its
//   ISC licence: Copyright (c) 2004-2025 ares team, Near et al. Permission to use, copy, modify,
//   and/or distribute this software for any purpose with or without fee is hereby granted,
//   provided that the above copyright notice and this permission notice appear in all copies.
//   THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS
//   SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL
//   THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY
//   DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF
//   CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE
//   OR PERFORMANCE OF THIS SOFTWARE.
u32 Machine::piHalf (u32 a)
{
	if (a >= 0x10000000 && a < 0x1FC00000)
	{
		u32 o = a - 0x10000000;
		if (o < romSize) return (rom[o >> 2] >> ((o & 2) ? 0 : 16)) & 0xFFFF;
		return o & 0xFFFF;					// (open bus: the address)
	}
	if (a >= 0x08000000 && a < 0x08000000 + SRAM_SIZE)
	{
		u32 o = a - 0x08000000;
		if (!saveType) saveType = 1;
		return (u32) sram[o] << 8 | sram[o + 1];
	}
	return 0;
}

u32 Machine::piDuration (u32 len)
{
	u32 cart = pi[1];
	bool d2 = (cart >= 0x05000000 && cart < 0x06000000) || (cart >= 0x08000000 && cart < 0x10000000);
	u32 lat = pi[d2 ? 9 : 5] & 0xFF, pwd = pi[d2 ? 10 : 6] & 0xFF, pgs = pi[d2 ? 11 : 7] & 15, rls = pi[d2 ? 12 : 8] & 3;
	len = (len | 1) + 1;
	u32 shift = pgs + 2, pageSize = 1u << shift, pageMask = pageSize - 1;
	u32 first = cart, last = cart + len - 2;
	u32 pages = (last >> shift) - (first >> shift) + 1, buffers = 0, partial = 0;
	if ((first >> shift) == (last >> shift)) { if (len == 128) buffers = 1; else partial = len; }
	else
	{
		if ((first & pageMask) == 0) buffers++; else partial += pageSize - (first & pageMask);
		if (((last + 2) & pageMask) == 0) buffers++; else partial += (last & pageMask) + 2;
		if (pages > 2) buffers += (pages - 2) * pageSize / 128;
	}
	u32 c = (14 + lat + 1) * pages + (pwd + 1 + rls + 1) * len / 2 + buffers * 28 + partial;
	return c * 3;
}

void Machine::piDma (bool toRdram)
{
	u8 *m = (u8 *) rdram;
	if (!toRdram)							// RDRAM -> cartridge (SRAM)
	{
		u32 len = (pi[2] | 1) + 1;
		for (u32 i = 0; i < len; i++)
		{
			u32 c = pi[1] + i, d = pi[0] + i;
			if (c >= 0x08000000 && c < 0x08000000 + SRAM_SIZE && d < RDRAM_SIZE)
			{
				sram[c - 0x08000000] = m[d ^ 3];
				sramDirty = true; saveType = 1;
			}
		}
		pi[4] |= 1;
		schedule (EV_PI, piDuration (pi[2]));
		return;
	}
	u8 mem[128];
	s32 length = (s32) (pi[3] & 0xFFFFFF) + 1;
	s32 maxBlock = 128;
	bool firstBlock = true;
	u32 dur = piDuration (pi[3] & 0xFFFFFF);
	while (length > 0)
	{
		s32 misalign = (s32) (pi[0] & 7);
		s32 distEnd = 0x800 - (s32) (pi[0] & 0x7FF);
		s32 blockLen = maxBlock - misalign < distEnd ? maxBlock - misalign : distEnd;
		s32 curLen = length < blockLen ? length : blockLen;
		for (s32 i = 0; i < curLen; i += 2)
		{
			u32 h = piHalf (pi[1]);
			mem[i] = (u8) (h >> 8); mem[i + 1] = (u8) h;
			pi[1] += 2;
			length -= 2;
		}
		s32 n = curLen - misalign;
		if (!(firstBlock && curLen < 127 - misalign) && (n & 1)) n++;	// (by halves)
		for (s32 i = 0; i < n; i++)
		{
			if (pi[0] < RDRAM_SIZE) m[pi[0] ^ 3] = mem[i];
			pi[0] = (pi[0] + 1) & 0xFFFFFF;
		}
		pi[0] = (pi[0] + 7) & ~7u & 0xFFFFFF;
		pi[3] = curLen <= 8 ? (u32) (127 - misalign) : 127u;
		firstBlock = false;
		maxBlock = distEnd < 8 ? 128 - misalign : 128;
	}
	pi[4] |= 1;							// busy
	schedule (EV_PI, dur);
}

// ---- the SI / PIF --------------------------------------------------------------------------------------------------
void Machine::siDma (bool toRdram)
{
	u32 dram = si[0] & 0xFFFFF8;
	u8 *m = (u8 *) rdram;
	if (toRdram)
		for (int i = 0; i < 64; i++) { if (dram + (u32) i < RDRAM_SIZE) m[(dram + (u32) i) ^ 3] = pifRam[i]; }
	else
	{
		for (int i = 0; i < 64; i++) pifRam[i] = dram + (u32) i < RDRAM_SIZE ? m[(dram + (u32) i) ^ 3] : 0;
		pifProcess ();
	}
	si[6] |= 1;
	schedule (EV_SI, 6000);
}

void Machine::pifProcess ()
{
	u8 ctl = pifRam[63];
	if (ctl & 0x08) pifRam[63] &= ~0x08;				// the end of the boot
	if (!(ctl & 1)) return;
	pifRam[63] &= ~1;
	int i = 0, ch = 0;
	while (i < 63 && ch < 5)
	{
		u8 t = pifRam[i];
		if (t == 0x00) { ch++; i++; continue; }
		if (t == 0xFD) { i++; continue; }
		if (t == 0xFE) break;
		if (t == 0xFF) { i++; continue; }
		int tx = t & 0x3F;
		if (i + 1 >= 63) break;
		int rx = pifRam[i + 1] & 0x3F;
		u8 *cmd = &pifRam[i + 2], *res = &pifRam[i + 2 + tx];
		if (i + 2 + tx + rx > 64) break;
		bool present = true;
		if (ch < 4)
		{
			if (ch != 0) present = false;				// one controller
			else switch (cmd[0])
			{
			case 0x00: case 0xFF: res[0] = 0x05; res[1] = 0x00; res[2] = 0x02; break;	// (no pak)
			case 0x01:
				res[0] = (u8) (padBtn[0] >> 8); res[1] = (u8) padBtn[0];
				res[2] = (u8) padX[0]; res[3] = (u8) padY[0];
				break;
			case 0x02: for (int k = 0; k < rx; k++) res[k] = 0; break;	// pak read: nothing
			case 0x03: if (rx) res[0] = 0; break;
			default: break;
			}
		}
		else								// the cartridge: EEPROM
		{
			if (eepromSize == 0) present = false;
			else switch (cmd[0])
			{
			case 0x00: case 0xFF: res[0] = 0x00; res[1] = eepromSize > 512 ? 0xC0 : 0x80; res[2] = 0x00; break;
			case 0x04:
			{
				int b = cmd[1] * 8;
				for (int k = 0; k < 8; k++) res[k] = b + k < eepromSize ? eeprom[b + k] : 0;
				break;
			}
			case 0x05:
			{
				int b = cmd[1] * 8;
				for (int k = 0; k < 8; k++) if (b + k < eepromSize) eeprom[b + k] = cmd[2 + k];
				eepromDirty = true;
				if (rx) res[0] = 0;
				break;
			}
			default: break;
			}
		}
		if (!present) pifRam[i + 1] |= 0x80;
		i += 2 + tx + rx;
		ch++;
	}
}

// ---- the AI ---------------------------------------------------------------------------------------------------------
void Machine::aiPush ()
{
	u32 len = aiFifo[0][1];
	u32 rate = aiRate ();
	aiOutput (aiFifo[0][0], len, rate);
	u64 samples = len / 4;
	schedule (EV_AI, samples * (u64) CPU_HZ / rate + 1);
}

// ---- the SP ---------------------------------------------------------------------------------------------------------
void Machine::spDma (bool toRdram)
{
	u32 v = toRdram ? sp[3] : sp[2];
	u32 len = (v & 0xFF8) + 8, count = ((v >> 12) & 0xFF) + 1, skip = (v >> 20) & 0xFF8;
	u32 mem = sp[0] & 0x1FF8, dram = sp[1] & 0xFFFFF8;
	u32 *spm = (mem & 0x1000) ? imem : dmem;
	for (u32 c = 0; c < count; c++)
	{
		for (u32 i = 0; i < len; i += 4)
		{
			u32 o = ((mem & 0xFFF) + i) & 0xFFC, d = dram + i;
			if (d >= RDRAM_SIZE) break;
			if (toRdram) rdram[d >> 2] = spm[o >> 2]; else spm[o >> 2] = rdram[d >> 2];
		}
		mem = (mem & 0x1000) | ((mem + len) & 0xFFF);
		dram += len + skip;
	}
	sp[0] = mem; sp[1] = dram;
	sp[2] = sp[3] = 0xFF8;
}

void Machine::spStatusWrite (u32 v)
{
	u32 &s = sp[4];
	bool wasHalted = s & 1;
	if (v & 1) s &= ~1u;						// clear halt
	if (v & 2) s |= 1;						// set halt
	if (v & 4) s &= ~2u;						// clear broke
	if (v & 8) lower (MI_SP);
	if (v & 16) raise (MI_SP);
	if (v & 32) s &= ~0x20u;
	if (v & 64) s |= 0x20;
	if (v & 128) s &= ~0x40u;					// interrupt on break
	if (v & 256) s |= 0x40;
	for (int k = 0; k < 8; k++)					// the signals 0..7
	{
		if (v & (1u << (9 + 2 * k))) s &= ~(0x80u << k);
		if (v & (1u << (10 + 2 * k))) s |= 0x80u << k;
	}
	if (wasHalted && !(s & 1)) runRsp ();
}

// The RSP started: its task is done at once, at a high level (n64_gfx.cpp, n64_audio.cpp).
void Machine::runRsp ()
{
	u32 type = dmem[0xFC0 / 4];					// the OSTask: 1 graphics, 2 audio
	lastTask = type;
	rspTasks[type < 8 ? type : 0]++;
	sp[4] |= 1 | 2 | 0x200;						// halted, broke, signal 2 (task done)
	if (sp[4] & 0x40) schedule (EV_SP, 1000);
	if (type == 1) { gfxTask (); schedule (EV_DP, 2000); }		// (the display list's full sync)
	else if (type == 2) audioTask ();
}

// An 8 or 16-bit read: on the PI bus, the word at the address rounded to 2 (the CPU then takes
// its byte lane: the second half of a word reads as the next half); elsewhere the aligned word.
u32 Machine::readSub (u32 pa)
{
	if (pa >= 0x05000000 && pa < 0x1FC00000)
	{
		u32 a = pa & ~1u;
		return piHalf (a) << 16 | piHalf (a + 2);
	}
	return readIo (pa & ~3u);
}

// ---- the physical registers -----------------------------------------------------------------------------------
u32 Machine::readIo (u32 pa)
{
	pa &= ~3u;
	if (pa < 0x03F00000) return 0;
	if (pa < 0x04000000) return rdramReg[(pa >> 2) & 7];
	if (pa < 0x04040000)						// DMEM / IMEM (mirrored)
		return (pa & 0x1000) ? imem[(pa & 0xFFF) >> 2] : dmem[(pa & 0xFFF) >> 2];
	switch (pa >> 20)
	{
	case 0x040:
		if (pa == 0x04080000) return spPc & 0xFFC;
		switch ((pa >> 2) & 7)
		{
		case 7: { u32 v = sp[7]; sp[7] = 1; return v; }	// the semaphore
		case 5: case 6: return 0;			// DMA full / busy
		default: return sp[(pa >> 2) & 7];
		}
	case 0x041: return dp[(pa >> 2) & 7];
	case 0x043: return mi[(pa >> 2) & 3];
	case 0x044:
	{
		u32 i = (pa >> 2) & 15;
		if (i == 4) return (u32) viLineNow * 2 + (vi[0] & 0x40 ? (u32) (frames & 1) : 0);	// V_CURRENT
		return i < 14 ? vi[i] : 0;
	}
	case 0x045:
	{
		u32 i = (pa >> 2) & 7;
		if (i == 3) return (aiCount >= 2 ? 0x80000001u : 0) | (aiCount >= 1 ? 0x40000000u : 0) | 0x01100000u;
		if (i == 1) return aiCount ? aiFifo[0][1] : 0;
		return i < 6 ? ai[i] : 0;
	}
	case 0x046:
	{
		u32 i = (pa >> 2) & 15;
		if (i == 4) return (pi[4] & 7) | ((mi[2] & MI_PI) ? 8u : 0u);
		return i < 13 ? pi[i] : 0;
	}
	case 0x047: return ri[(pa >> 2) & 7];
	case 0x048:
	{
		u32 i = (pa >> 2) & 7;
		if (i == 6) return (si[6] & 1) | ((mi[2] & MI_SI) ? 0x1000u : 0u);
		return i < 7 ? si[i] : 0;
	}
	default: break;
	}
	if (pa >= 0x1FC007C0 && pa < 0x1FC00800)			// PIF RAM
	{
		u32 o = pa - 0x1FC007C0;
		return (u32) pifRam[o] << 24 | (u32) pifRam[o + 1] << 16 | (u32) pifRam[o + 2] << 8 | pifRam[o + 3];
	}
	return cartRead (pa);
}

void Machine::writeIo (u32 pa, u32 v, u32 mask)
{
	pa &= ~3u;
	if (pa < 0x03F00000) return;
	if (pa < 0x04000000) { rdramReg[(pa >> 2) & 7] = v; return; }
	if (pa < 0x04040000)
	{
		u32 *w = (pa & 0x1000) ? &imem[(pa & 0xFFF) >> 2] : &dmem[(pa & 0xFFF) >> 2];
		*w = (*w & ~mask) | (v & mask);
		return;
	}
	switch (pa >> 20)
	{
	case 0x040:
		if (pa == 0x04080000) { spPc = v & 0xFFC; return; }
		switch ((pa >> 2) & 7)
		{
		case 0: sp[0] = v & 0x1FF8; return;
		case 1: sp[1] = v & 0xFFFFF8; return;
		case 2: sp[2] = v; spDma (false); return;
		case 3: sp[3] = v; spDma (true); return;
		case 4: spStatusWrite (v); return;
		case 7: sp[7] = 0; return;
		default: return;
		}
	case 0x041:
	{
		u32 i = (pa >> 2) & 7;
		if (i == 3)						// DP status: the flags (the RDP is not run yet)
		{
			if (v & 1) dp[3] &= ~1u;
			if (v & 2) dp[3] |= 1;
			if (v & 4) dp[3] &= ~2u;
			if (v & 8) dp[3] |= 2;
			if (v & 16) dp[3] &= ~4u;
			if (v & 32) dp[3] |= 4;
			return;
		}
		if (i == 0) { dp[0] = dp[2] = v & 0xFFFFF8; return; }
		if (i == 1) { dp[1] = v & 0xFFFFF8; dp[2] = dp[1]; raise (MI_DP); return; }	// (done at once)
		return;
	}
	case 0x043:
	{
		u32 i = (pa >> 2) & 3;
		if (i == 0)						// MI_MODE
		{
			mi[0] = (mi[0] & ~0x7Fu) | (v & 0x7F);
			if (v & 0x80) mi[0] &= ~0x80u;
			if (v & 0x100) mi[0] |= 0x80;
			if (v & 0x200) mi[0] &= ~0x100u;
			if (v & 0x400) mi[0] |= 0x100;
			if (v & 0x800) lower (MI_DP);
			if (v & 0x1000) mi[0] &= ~0x200u;
			if (v & 0x2000) mi[0] |= 0x200;
		}
		else if (i == 3)					// the mask: clear / set pairs
		{
			for (int k = 0; k < 6; k++)
			{
				if (v & (1u << (2 * k))) mi[3] &= ~(1u << k);
				if (v & (2u << (2 * k))) mi[3] |= 1u << k;
			}
			checkInterrupts ();
		}
		return;
	}
	case 0x044:
	{
		u32 i = (pa >> 2) & 15;
		if (i == 4) { lower (MI_VI); return; }
		if (i < 14) vi[i] = v;
		return;
	}
	case 0x045:
	{
		u32 i = (pa >> 2) & 7;
		if (i == 0) ai[0] = v & 0xFFFFF8;
		else if (i == 1)					// the length: queue a DMA
		{
			u32 len = v & 0x3FFF8;
			if (len && aiCount < 2)
			{
				aiFifo[aiCount][0] = ai[0]; aiFifo[aiCount][1] = len;
				aiCount++;
				if (aiCount == 1) aiPush ();
			}
		}
		else if (i == 3) lower (MI_AI);
		else if (i < 6) ai[i] = v;
		return;
	}
	case 0x046:
	{
		u32 i = (pa >> 2) & 15;
		if (i == 4) { if (v & 2) lower (MI_PI); if (v & 1) pi[4] &= ~4u; return; }
		if (pi[4] & 3) { pi[4] |= 4; return; }			// busy: an error, not written
		if (i == 0) pi[0] = v & 0xFFFFFE;
		else if (i == 1) pi[1] = v & ~1u;
		else if (i == 2) { pi[2] = v & 0xFFFFFF; piDma (false); }
		else if (i == 3) { pi[3] = v & 0xFFFFFF; piDma (true); }
		else if (i < 13) pi[i] = v & 0xFF;
		return;
	}
	case 0x047: ri[(pa >> 2) & 7] = v; return;
	case 0x048:
	{
		u32 i = (pa >> 2) & 7;
		if (i == 0) si[0] = v & 0xFFFFF8;
		else if (i == 1) siDma (true);				// PIF -> RDRAM
		else if (i == 4) siDma (false);				// RDRAM -> PIF
		else if (i == 6) lower (MI_SI);
		return;
	}
	default: break;
	}
	if (pa >= 0x1FC007C0 && pa < 0x1FC00800)
	{
		u32 o = pa - 0x1FC007C0;
		for (int k = 0; k < 4; k++) if (mask & (0xFF000000u >> (8 * k))) pifRam[o + (u32) k] = (u8) (v >> (24 - 8 * k));
		if (o == 0x3C) pifProcess ();
		return;
	}
	if (pa >= 0x08000000 && pa < 0x08000000 + SRAM_SIZE)
	{
		u32 o = pa - 0x08000000;
		for (int k = 0; k < 4; k++) if (mask & (0xFF000000u >> (8 * k))) sram[o + (u32) k] = (u8) (v >> (24 - 8 * k));
		sramDirty = true; saveType = 1;
	}
}

} // namespace n64
