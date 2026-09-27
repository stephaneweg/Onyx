//
// snes/snes.cpp -- the cartridge (LoROM / HiROM, its RAM), the bus (4 KB pages: the direct
// reads, the access speeds), the 5A22's registers ($4200-$421F: NMI / IRQ, the H/V timer, the
// multiplier and divider, the joypads), the DMA and HDMA channels and the frame's timing: a
// line is 1364 master clocks, its events (HDMA start, the DRAM refresh, the H/V IRQ, the line
// drawn + HDMA) are run between the CPU's instructions. The CPU is in snes_cpu.cpp, the PPU
// in snes_ppu.cpp, the sound unit in snes_apu.cpp.
//
#include "snes/snes.h"

namespace snes {

static void zero (void *d, u32 n) { u8 *p = (u8 *) d; for (u32 i = 0; i < n; i++) p[i] = 0; }

Machine::Machine () : rom (0), romSize (0), rate (48000)
{
	zero (fb, sizeof fb); zero (sram, sizeof sram); zero (wram, sizeof wram); zero (aram, sizeof aram);
	zero (vram, sizeof vram);
	height = 224; pal = false; hirom = false; title[0] = 0; chip = 0; sramSize = 0; sramDirty = false;
	now = 0; frames = 0;
	for (int b = 0; b < 16; b++)
		for (int c = 0; c < 32; c++) { u32 v = (u32) c * 255 / 31; colorLut[b][c] = v * (u32) b / 15; }
}
Machine::~Machine () {}

void Machine::setSaveRam (const u8 *data, int n)
{
	for (int i = 0; i < n && i < (int) sizeof sram; i++) sram[i] = data[i];
	sramDirty = false;
}

void Machine::setPal (bool on)
{
	if (on == pal) return;
	pal = on;
	spcCycles = now * 1024000ull / (pal ? 21281370ull : 21477272ull);	// (the sound unit's clock goes on from here)
}

// ---- the cartridge ---------------------------------------------------------------------------
// The header at $7FC0 (LoROM) or $FFC0 (HiROM): the one that looks right wins.
static int headerScore (const u8 *d, u32 size, u32 at, bool hi)
{
	if (at + 0x40 > size) return -100;
	const u8 *h = d + at;
	int s = 0;
	u16 sum = (u16) (h[0x1E] | h[0x1F] << 8), cpl = (u16) (h[0x1C] | h[0x1D] << 8);
	if ((u16) (sum + cpl) == 0xFFFF) s += 8;
	u8 map = h[0x15];
	if (hi ? (map & 0xEF) == 0x21 || (map & 0xEF) == 0x25 : (map & 0xEF) == 0x20) s += 4;
	u16 reset = (u16) (h[0x3C] | h[0x3D] << 8);
	if (reset >= 0x8000) s += 3; else s -= 8;
	for (int i = 0; i < 21; i++) if (h[i] < 0x20 || h[i] > 0x7E) { s -= 1; break; }
	if (h[0x19] < 0x14) s += 1;
	if (h[0x17] < 0x0E) s += 1;
	return s;
}

u32 Machine::romMirror (u32 a) const
{
	// a size that is not a power of two: the part above the largest power mirrors (bsnes)
	u32 size = romSize, base = 0, mask = 1u << 23;
	if (a < size) return a;
	while (a >= size)
	{
		while (!(a & mask)) mask >>= 1;
		a -= mask;
		if (size > mask) { size -= mask; base += mask; }
		mask >>= 1;
	}
	return base + a;
}

bool Machine::load (const u8 *d, int size)
{
	if (!d || size < 0x8000) return false;
	if ((size & 0x3FF) == 0x200) { d += 0x200; size -= 0x200; }	// a copier header
	int lo = headerScore (d, (u32) size, 0x7FC0, false), hi = headerScore (d, (u32) size, 0xFFC0, true);
	hirom = hi > lo;
	if ((hirom ? hi : lo) < 0) return false;
	const u8 *h = d + (hirom ? 0xFFC0 : 0x7FC0);
	rom = d; romSize = (u32) size;
	int n = 0;
	for (int i = 0; i < 21; i++) { u8 c = h[i]; title[n++] = c >= 0x20 && c < 0x7F ? (char) c : ' '; }
	while (n > 0 && title[n - 1] == ' ') n--;
	title[n] = 0;
	chip = h[0x16];
	u8 ramCode = h[0x18];
	sramSize = ramCode ? 1024u << (ramCode > 7 ? 7 : ramCode) : 0;
	if (sramSize > SRAM_MAX) sramSize = SRAM_MAX;
	u8 region = h[0x19];
	pal = region >= 2 && region <= 12;
	// the chips we do not have: Super FX (map $20 + chip $13-$1A), SA-1 ($23), DSP-n ($03-$05)...
	if ((chip & 0x0F) >= 3 && chip != 0x00 && chip != 0x01 && chip != 0x02) return false;
	for (u32 i = 0; i < sizeof sram; i++) sram[i] = 0xFF;
	reset ();
	return true;
}

void Machine::mapPages ()
{
	for (int p = 0; p < 4096; p++)
	{
		u32 bank = (u32) p >> 4, addr = ((u32) p & 15) << 12;
		const u8 *r = 0; u8 *w = 0; u8 sp = 8;
		bool sys = (bank & 0x40) == 0;			// banks 00-3F / 80-BF
		if (bank == 0x7E || bank == 0x7F) { r = w = wram + ((bank & 1) << 16) + addr; sp = 8; }
		else if (sys && addr < 0x2000) { r = w = wram + addr; sp = 8; }
		else if (sys && addr < 0x8000)
		{
			sp = 0;						// I/O and the rest: the slow path
			if (hirom && addr >= 0x6000 && (bank & 0x7F) >= 0x20 && sramSize >= 0x1000)
				r = sram + ((((bank & 0x1F) << 13) | (addr - 0x6000)) & (sramSize - 1)), sp = 8;
		}
		else if (!hirom)
		{
			if (addr >= 0x8000) r = rom + romMirror (((bank & 0x7F) << 15) | (addr & 0x7FFF));
			else if ((bank & 0x7F) >= 0x70 && sramSize >= 0x1000)
				r = sram + ((((bank & 0x0F) << 15) | addr) & (sramSize - 1));
			else if ((bank & 0x7F) >= 0x40 && (bank & 0x7F) < 0x70) r = rom + romMirror (((bank & 0x3F) << 15) | (addr & 0x7FFF));
			if (addr >= 0x8000 || !((bank & 0x7F) >= 0x70)) sp = (bank & 0x80) && fastRom ? 6 : 8;
			else sp = 8;
		}
		else
		{
			if (sys) r = rom + romMirror (((bank & 0x3F) << 16) | addr);
			else r = rom + romMirror (((bank & 0x3F) << 16) | addr);
			sp = (bank & 0x80) && fastRom ? 6 : 8;
		}
		if (!r) sp = 0;
		rdPage[p] = r; wrPage[p] = w; pageSpeed[p] = sp;
	}
}

int Machine::ioSpeed (u32 a) const
{
	u32 addr = a & 0xFFFF;
	if (addr >= 0x4000 && addr < 0x4200) return 12;
	if (addr >= 0x2000 && addr < 0x6000) return 6;
	return 8;
}

u8 Machine::read (u32 a)
{
	u32 p = a >> 12;
	const u8 *m = rdPage[p];
	if (m) { now += pageSpeed[p]; return mdr = m[a & 0xFFF]; }
	now += (u32) ioSpeed (a);
	return mdr = ioRead (a);
}

void Machine::write (u32 a, u8 v)
{
	u32 p = a >> 12;
	u8 *m = wrPage[p];
	mdr = v;
	if (m) { now += pageSpeed[p]; m[a & 0xFFF] = v; return; }
	now += (u32) (pageSpeed[p] ? pageSpeed[p] : ioSpeed (a));
	ioWrite (a, v);
}

// The slow path: the I/O registers, the cartridge RAM (writes; small RAMs), open bus.
u8 Machine::ioRead (u32 a)
{
	u32 bank = a >> 16, addr = a & 0xFFFF;
	if ((bank & 0x40) == 0 && addr < 0x8000)
	{
		if (addr >= 0x2100 && addr < 0x2200) return readB ((u8) addr);
		if (addr == 0x4016) { u8 r = (u8) ((mdr & 0xFC) | (padShift >> 15)); padShift = (u16) (padShift << 1 | 1); return r; }
		if (addr == 0x4017) return (u8) ((mdr & 0xE0) | 0x1C);
		if (addr >= 0x4300 && addr < 0x4380)
		{
			Dma &c = dma[(addr >> 4) & 7];
			switch (addr & 0xF)
			{
			case 0: return c.ctrl; case 1: return c.bbad; case 2: return (u8) c.a1t; case 3: return (u8) (c.a1t >> 8);
			case 4: return c.a1b; case 5: return (u8) c.das; case 6: return (u8) (c.das >> 8); case 7: return c.dasb;
			case 8: return (u8) c.a2a; case 9: return (u8) (c.a2a >> 8); case 0xA: return c.ntrl; case 0xB: case 0xF: return c.unused;
			}
			return mdr;
		}
		switch (addr)
		{
		case 0x4210: { u8 r = (u8) ((mdr & 0x70) | (nmiFlag ? 0x80 : 0) | 2); nmiFlag = false; return r; }
		case 0x4211: { u8 r = (u8) ((mdr & 0x7F) | (timeUp ? 0x80 : 0)); timeUp = false; irqLine = false; return r; }
		case 0x4212:
		{
			u16 h = hcounter ();
			u8 r = (u8) (mdr & 0x3E);
			if (line >= vdisp ()) r |= 0x80;
			if (h < 1 || h >= 274) r |= 0x40;
			if (autoJoyBusy && line >= vdisp () && line < vdisp () + 3) r |= 1;
			return r;
		}
		case 0x4213: return wrio;
		case 0x4214: return (u8) rddiv; case 0x4215: return (u8) (rddiv >> 8);
		case 0x4216: return (u8) rdmpy; case 0x4217: return (u8) (rdmpy >> 8);
		case 0x4218: return (u8) joy1; case 0x4219: return (u8) (joy1 >> 8);
		case 0x421A: case 0x421B: case 0x421C: case 0x421D: case 0x421E: case 0x421F: return 0;
		}
		if (hirom && addr >= 0x6000 && (bank & 0x7F) >= 0x20 && sramSize)
			return sram[(((bank & 0x1F) << 13) | (addr - 0x6000)) & (sramSize - 1)];
		return mdr;
	}
	if (!hirom && (bank & 0x7F) >= 0x70 && (bank & 0x7F) < 0x7E && addr < 0x8000 && sramSize)
		return sram[(((bank & 0x0F) << 15) | addr) & (sramSize - 1)];
	return mdr;
}

void Machine::ioWrite (u32 a, u8 v)
{
	u32 bank = a >> 16, addr = a & 0xFFFF;
	if ((bank & 0x40) == 0 && addr < 0x8000)
	{
		if (addr >= 0x2100 && addr < 0x2200) { writeB ((u8) addr, v); return; }
		if (addr == 0x4016)
		{
			bool s = v & 1;
			if (padStrobe && !s) padShift = pad;
			padStrobe = s;
			if (s) padShift = pad;
			return;
		}
		if (addr >= 0x4300 && addr < 0x4380)
		{
			Dma &c = dma[(addr >> 4) & 7];
			switch (addr & 0xF)
			{
			case 0: c.ctrl = v; break; case 1: c.bbad = v; break;
			case 2: c.a1t = (u16) ((c.a1t & 0xFF00) | v); break; case 3: c.a1t = (u16) ((c.a1t & 0xFF) | v << 8); break;
			case 4: c.a1b = v; break;
			case 5: c.das = (u16) ((c.das & 0xFF00) | v); break; case 6: c.das = (u16) ((c.das & 0xFF) | v << 8); break;
			case 7: c.dasb = v; break;
			case 8: c.a2a = (u16) ((c.a2a & 0xFF00) | v); break; case 9: c.a2a = (u16) ((c.a2a & 0xFF) | v << 8); break;
			case 0xA: c.ntrl = v; break; case 0xB: case 0xF: c.unused = v; break;
			}
			return;
		}
		switch (addr)
		{
		case 0x4200:
		{
			bool wasNmi = nmitimen & 0x80;
			nmitimen = v;
			if (!wasNmi && (v & 0x80) && nmiFlag) nmiPending = true;
			if (!(v & 0x30)) { timeUp = false; irqLine = false; }
			computeIrq ();
			return;
		}
		case 0x4201: if ((wrio & 0x80) && !(v & 0x80)) latchCounters (); wrio = v; return;
		case 0x4202: wrmpya = v; return;
		case 0x4203: rdmpy = (u16) (wrmpya * v); rddiv = v; return;
		case 0x4204: wrdivl = v; return;
		case 0x4205: wrdivh = v; return;
		case 0x4206:
		{
			u16 dividend = (u16) (wrdivl | wrdivh << 8);
			if (v) { rddiv = (u16) (dividend / v); rdmpy = (u16) (dividend % v); }
			else { rddiv = 0xFFFF; rdmpy = dividend; }
			return;
		}
		case 0x4207: htime = (u16) ((htime & 0x100) | v); computeIrq (); return;
		case 0x4208: htime = (u16) ((htime & 0xFF) | (v & 1) << 8); computeIrq (); return;
		case 0x4209: vtime = (u16) ((vtime & 0x100) | v); computeIrq (); return;
		case 0x420A: vtime = (u16) ((vtime & 0xFF) | (v & 1) << 8); computeIrq (); return;
		case 0x420B: dmaRun (v); return;
		case 0x420C: hdmaen = v; return;
		case 0x420D: { bool f = v & 1; if (f != fastRom) { fastRom = f; mapPages (); } return; }
		}
		if (hirom && addr >= 0x6000 && (bank & 0x7F) >= 0x20 && sramSize)
		{
			u8 &s = sram[(((bank & 0x1F) << 13) | (addr - 0x6000)) & (sramSize - 1)];
			if (s != v) { s = v; sramDirty = true; }
		}
		return;
	}
	if (!hirom && (bank & 0x7F) >= 0x70 && (bank & 0x7F) < 0x7E && addr < 0x8000 && sramSize)
	{
		u8 &s = sram[(((bank & 0x0F) << 15) | addr) & (sramSize - 1)];
		if (s != v) { s = v; sramDirty = true; }
	}
}

// ---- the B bus: the PPU, the sound unit's ports, the WRAM port -----------------------------------
u8 Machine::readB (u8 a)
{
	if (a >= 0x40 && a < 0x80) { apuSync (); return apuToCpu[a & 3]; }
	if (a == 0x80) { u8 v = wram[wmAddr]; wmAddr = (wmAddr + 1) & 0x1FFFF; return v; }
	return ppuRead (a);
}

void Machine::writeB (u8 a, u8 v)
{
	if (a >= 0x40 && a < 0x80) { apuSync (); cpuToApu[a & 3] = v; return; }
	switch (a)
	{
	case 0x80: wram[wmAddr] = v; wmAddr = (wmAddr + 1) & 0x1FFFF; return;
	case 0x81: wmAddr = (wmAddr & 0x1FF00) | v; return;
	case 0x82: wmAddr = (wmAddr & 0x100FF) | (u32) v << 8; return;
	case 0x83: wmAddr = (wmAddr & 0x0FFFF) | (u32) (v & 1) << 16; return;
	}
	ppuWrite (a, v);
}

u8 Machine::dmaReadA (u32 a)
{
	u32 addr = a & 0xFFFF;
	// the A bus cannot reach the B bus, nor the DMA registers
	if ((a & 0x400000) == 0 && ((addr >= 0x2100 && addr < 0x2200) || (addr >= 0x4000 && addr < 0x4400))) return mdr;
	const u8 *m = rdPage[a >> 12];
	if (m) return mdr = m[a & 0xFFF];
	return mdr = ioRead (a);
}
void Machine::dmaWriteA (u32 a, u8 v)
{
	u32 addr = a & 0xFFFF;
	if ((a & 0x400000) == 0 && ((addr >= 0x2100 && addr < 0x2200) || (addr >= 0x4000 && addr < 0x4400))) return;
	u8 *m = wrPage[a >> 12];
	if (m) { m[a & 0xFFF] = v; return; }
	ioWrite (a, v);
}

// ---- DMA / HDMA -----------------------------------------------------------------------------------
static const u8 DMA_PATTERN[8][4] = { {0,0,0,0}, {0,1,0,1}, {0,0,0,0}, {0,0,1,1}, {0,1,2,3}, {0,1,0,1}, {0,0,0,0}, {0,0,1,1} };
static const u8 DMA_LEN[8] = { 1, 2, 2, 4, 4, 4, 2, 4 };

void Machine::dmaRun (u8 mask)
{
	if (!mask) return;
	now += 8;
	for (int i = 0; i < 8; i++)
	{
		if (!(mask & (1 << i))) continue;
		Dma &c = dma[i];
		now += 8;
		u32 count = c.das ? c.das : 0x10000;
		int k = 0;
		int step = (c.ctrl & 0x08) ? 0 : (c.ctrl & 0x10) ? -1 : 1;
		const u8 *pat = DMA_PATTERN[c.ctrl & 7];
		while (count--)
		{
			u8 b = (u8) (c.bbad + pat[k & 3]);
			u32 aa = (u32) c.a1b << 16 | c.a1t;
			if (c.ctrl & 0x80) dmaWriteA (aa, readB (b));
			else
			{
				u8 v = dmaReadA (aa);
				if (b == 0x80 && (aa & 0xFE0000) == 0x7E0000) {}	// WRAM -> WRAM: nothing
				else writeB (b, v);
			}
			c.a1t = (u16) (c.a1t + step);
			now += 8;
			k++;
		}
		c.das = 0;
	}
}

void Machine::hdmaInit ()
{
	for (int i = 0; i < 8; i++)
	{
		Dma &c = dma[i];
		c.done = !(hdmaen & (1 << i));
		c.doTransfer = false;
		if (c.done) continue;
		c.a2a = c.a1t;
		c.ntrl = dmaReadA ((u32) c.a1b << 16 | c.a2a); c.a2a++;
		now += 8;
		if (c.ctrl & 0x40)
		{
			u8 l = dmaReadA ((u32) c.a1b << 16 | c.a2a); c.a2a++;
			u8 h = dmaReadA ((u32) c.a1b << 16 | c.a2a); c.a2a++;
			c.das = (u16) (l | h << 8);
			now += 16;
		}
		if (!c.ntrl) c.done = true;
		c.doTransfer = true;
	}
}

void Machine::hdmaRun ()
{
	bool any = false;
	for (int i = 0; i < 8; i++)
	{
		Dma &c = dma[i];
		if (c.done || !(hdmaen & (1 << i))) continue;
		any = true;
		now += 8;
		if (c.doTransfer)
		{
			int len = DMA_LEN[c.ctrl & 7];
			const u8 *pat = DMA_PATTERN[c.ctrl & 7];
			for (int k = 0; k < len; k++)
			{
				u32 aa;
				if (c.ctrl & 0x40) { aa = (u32) c.dasb << 16 | c.das; c.das++; }
				else { aa = (u32) c.a1b << 16 | c.a2a; c.a2a++; }
				u8 b = (u8) (c.bbad + pat[k]);
				if (c.ctrl & 0x80) dmaWriteA (aa, readB (b));
				else writeB (b, dmaReadA (aa));
				now += 8;
			}
		}
		c.ntrl--;
		c.doTransfer = (c.ntrl & 0x80) != 0;
		if (!(c.ntrl & 0x7F))
		{
			c.ntrl = dmaReadA ((u32) c.a1b << 16 | c.a2a); c.a2a++;
			now += 8;
			if (c.ctrl & 0x40)
			{
				u8 l = dmaReadA ((u32) c.a1b << 16 | c.a2a); c.a2a++;
				u8 h = dmaReadA ((u32) c.a1b << 16 | c.a2a); c.a2a++;
				c.das = (u16) (l | h << 8);
				now += 16;
			}
			if (!c.ntrl) c.done = true;
			c.doTransfer = true;
		}
	}
	if (any) now += 18;
}

// ---- reset / the frame ------------------------------------------------------------------------------
void Machine::reset ()
{
	fastRom = false;
	mapPages ();
	mdr = 0;
	E = true; P = 0x34; A = X = Y = 0; S = 0x1FF; D = 0; DB = PB = 0;
	waiting = stopped = false;
	nmiPending = irqLine = false;
	nmitimen = 0; wrio = 0xFF; wrmpya = 0xFF; wrdivl = wrdivh = 0xFF; rddiv = rdmpy = 0;
	htime = vtime = 0x1FF; nmiFlag = timeUp = autoJoyBusy = false;
	wmAddr = 0; joy1 = 0; padShift = 0; padStrobe = false; hdmaen = 0;
	for (int i = 0; i < 8; i++)
	{
		Dma &c = dma[i];
		c.ctrl = c.bbad = c.a1b = c.dasb = c.ntrl = c.unused = 0xFF; c.a1t = c.das = c.a2a = 0xFFFF; c.done = true; c.doTransfer = false;
	}
	ppuReset ();
	apuReset ();
	line = 0; lineStart = now; evStep = 0; irqAt = ~0ull; frameDone = false;
	PC = (u16) (read (0xFFFC) | read (0xFFFD) << 8);
	computeIrq ();
}

// The H/V IRQ of the current line: when the H counter reaches HTIME (+ ~3.5 dots), or at the
// start of the line VTIME.
void Machine::computeIrq ()
{
	irqAt = ~0ull;
	u8 en = (nmitimen >> 4) & 3;
	if (!en) return;
	u64 at;
	if (en == 2) { if (line != vtime) return; at = lineStart + 10; }
	else
	{
		if (en == 3 && line != vtime) return;
		if (htime > 339) return;
		at = lineStart + (u64) htime * 4 + 14;
	}
	if (at < now && en != 2 && (now - at) > 8) return;	// already past it in this line
	if (at < now && en == 2 && (now - lineStart) > 40) return;
	irqAt = at;
}

// A line's events, in order: 0 HDMA set up (line 0), 1 DRAM refresh, 2 the line drawn + HDMA,
// 3 the end of the line.
static const u32 EV_POS[4] = { 12, 536, 1096, 1364 };

void Machine::lineEvent ()
{
	switch (evStep)
	{
	case 0:
		if (line == 0) hdmaInit ();
		break;
	case 1:
		now += 40;
		break;
	case 2:
		if (line >= 1 && line < vdisp ()) renderLine (line);
		if (line < vdisp ()) hdmaRun ();
		break;
	case 3:
	{
		line++;
		lineStart += 1364;
		if (line >= lines ())
		{
			line = 0;
			nmiFlag = false;
			overscan = setini & 4;
			frames++;
		}
		if (line == vdisp ())
		{
			height = overscan ? 239 : 224;
			nmiFlag = true;
			if (nmitimen & 0x80) nmiPending = true;
			if (!(inidisp & 0x80)) oamAddr = oamReload;
			if (nmitimen & 1) { joy1 = pad; autoJoyBusy = true; padShift = 0xFFFF; }
			else autoJoyBusy = false;
			frameDone = true;
		}
		if ((line & 15) == 0) apuSync ();
		evStep = 0;
		computeIrq ();
		return;
	}
	}
	evStep++;
}

void Machine::runFrame ()
{
	frameDone = false;
	while (!frameDone)
	{
		u64 ev = lineStart + EV_POS[evStep];
		while (now < ev)
		{
			if (now >= irqAt) { irqAt = ~0ull; timeUp = true; irqLine = true; }
			u64 stop = ev < irqAt ? ev : irqAt;
			if (stopped) { now = ev; break; }
			if (waiting)
			{
				if (nmiPending || irqLine) waiting = false;
				else { now = stop; continue; }
			}
			cpuStep ();
		}
		if (now >= irqAt) { irqAt = ~0ull; timeUp = true; irqLine = true; }
		lineEvent ();
	}
	apuSync ();
}

} // namespace snes
