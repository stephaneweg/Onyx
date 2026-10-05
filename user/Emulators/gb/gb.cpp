//
// gb/gb.cpp -- the Game Boy / Game Boy Color machine (see gb.h). Timing is per instruction
// (the T-cycles of each opcode, then the timer / PPU / APU catch up), the picture is drawn a
// scanline at a time at the end of each line's pixel transfer: what games need.
//
#include "gb/gb.h"

namespace gb {

static void zero (void *p, int n) { unsigned char *d = (unsigned char *) p; for (int i = 0; i < n; i++) d[i] = 0; }

enum { FZ = 0x80, FN = 0x40, FH = 0x20, FC = 0x10 };
enum { CPU_HZ = 4194304 };

Machine::Machine () : cgb (false), sram (0), sramSize (0), battery (false), sramDirty (false), serialLen (0), rom (0), romSize (0), romBanks (0), rate (48000)
{
	title[0] = 0; serial[0] = 0;
	static const unsigned grey[4] = { 0xE0F8D0, 0x88C070, 0x346856, 0x081820 };	// the classic green
	for (int i = 0; i < 4; i++) dmgColors[i] = grey[i];
	zero (fb, sizeof fb);
	ahead = atail = 0;
}
Machine::~Machine () { delete [] sram; }

void Machine::setDmgPalette (const unsigned *four) { for (int i = 0; i < 4; i++) dmgColors[i] = four[i] & 0xFFFFFF; }
void Machine::setAudioRate (int hz) { rate = hz > 8000 ? hz : 48000; }

bool Machine::load (const unsigned char *data, int size)
{
	if (!data || size < 0x8000) return false;
	rom = data; romSize = size; romBanks = size / 0x4000;
	int k = 0;
	for (int i = 0x134; i < 0x144 && k < 16; i++)
	{
		unsigned char ch = data[i];
		if (ch == 0) break;
		if (i >= 0x13F && (data[0x143] & 0x80)) break;		// (CGB headers: manufacturer code)
		title[k++] = ch >= 32 && ch < 127 ? (char) ch : ' ';
	}
	while (k > 0 && title[k - 1] == ' ') k--;
	title[k] = 0;
	cgb = (data[0x143] & 0x80) != 0;
	int t = data[0x147];
	mbc = (t >= 1 && t <= 3) ? 1 : (t == 5 || t == 6) ? 2 : (t >= 0x0F && t <= 0x13) ? 3 : (t >= 0x19 && t <= 0x1E) ? 5 : 0;
	battery = t == 3 || t == 6 || t == 9 || t == 0x0D || t == 0x0F || t == 0x10 || t == 0x13 || t == 0x1B || t == 0x1E;
	static const int ramSizes[6] = { 0, 2048, 8192, 32768, 131072, 65536 };
	int rs = data[0x149] < 6 ? ramSizes[data[0x149]] : 0;
	if (mbc == 2) rs = 512;
	delete [] sram; sram = 0; sramSize = rs;
	if (rs) { sram = new unsigned char[rs]; for (int i = 0; i < rs; i++) sram[i] = 0xFF; }
	reset ();
	return true;
}

void Machine::setSaveRam (const unsigned char *data, int n)
{
	if (!sram) return;
	for (int i = 0; i < n && i < sramSize; i++) sram[i] = data[i];
	sramDirty = false;
}

void Machine::reset ()
{
	zero (vram, sizeof vram); zero (wram, sizeof wram); zero (oam, sizeof oam); zero (hram, sizeof hram); zero (io, sizeof io);
	ie = 0; vbank = 0; wbank = 1;
	romBank = 1; ramBank = 0; mbc1Mode = 0; ramOn = false;
	mapRom ();
	for (int i = 0; i < 5; i++) rtcReg[i] = rtcLatched[i] = 0;
	rtcSel = 0; rtcLatchPrimed = false; rtcCycles = 0;
	// the registers as the boot ROM leaves them
	if (cgb) { a = 0x11; f = 0x80; b = 0x00; c = 0x00; d = 0xFF; e = 0x56; h = 0x00; l = 0x0D; }
	else { a = 0x01; f = 0xB0; b = 0x00; c = 0x13; d = 0x00; e = 0xD8; h = 0x01; l = 0x4D; }
	sp = 0xFFFE; pc = 0x0100;
	ime = false; halted = false; haltBug = false; eiDelay = 0; doubleSpeed = false;
	divCounter = 0xABCC;
	lineDots = 0; ly = 0; winLine = 0; frameDone = false; statLine = false;
	hdmaActive = false; hdmaLen = 0; hdmaSrc = hdmaDst = 0;
	buttons = 0;
	io[0x00] = 0xCF; io[0x40] = 0x91; io[0x41] = 0x85; io[0x47] = 0xFC; io[0x48] = 0xFF; io[0x49] = 0xFF;
	io[0x0F] = 0xE1;
	for (int i = 0; i < 64; i++) { bgPalCgb[i] = 0xFF; obPalCgb[i] = 0xFF; }
	for (int i = 0; i < 32; i++) bgRGB[i] = obRGB[i] = cgbColor (bgPalCgb, i);
	apuPending = 0;
	// APU: on, as after the boot sound
	zero (sq, sizeof sq); zero (&wv, sizeof wv); zero (&ns, sizeof ns);
	ns.lfsr = 0x7FFF; frameSeq = 0; fsTimer = 0; sampleAcc = 0;
	io[0x26] = 0xF1; io[0x24] = 0x77; io[0x25] = 0xF3;
	static const unsigned char waveInit[16] = { 0x84, 0x40, 0x43, 0xAA, 0x2D, 0x78, 0x92, 0x3C, 0x60, 0x59, 0x59, 0xB0, 0x34, 0xB8, 0x2E, 0xDA };
	for (int i = 0; i < 16; i++) io[0x30 + i] = waveInit[i];
	serialLen = 0; serial[0] = 0;
	for (int i = 0; i < W * H; i++) fb[i] = cgb ? 0xFFFFFF : dmgColors[0];
}

// ---- memory ------------------------------------------------------------------------------------------
unsigned char Machine::readSram (unsigned short addr)
{
	if (!ramOn) return 0xFF;
	if (mbc == 3 && rtcSel) return (unsigned char) rtcLatched[rtcSel - 8];
	if (!sram) return 0xFF;
	if (mbc == 2) return (unsigned char) (sram[addr & 0x1FF] | 0xF0);
	int bank = mbc == 1 ? (mbc1Mode ? ramBank & 3 : 0) : ramBank;
	long off = (long) bank * 0x2000 + (addr - 0xA000);
	return sram[off % sramSize];
}
void Machine::writeSram (unsigned short addr, unsigned char v)
{
	if (!ramOn) return;
	if (mbc == 3 && rtcSel) { rtcReg[rtcSel - 8] = v; if (rtcSel == 8) rtcCycles = 0; return; }
	if (!sram) return;
	if (mbc == 2) { sram[addr & 0x1FF] = (unsigned char) (v & 0x0F); sramDirty = true; return; }
	int bank = mbc == 1 ? (mbc1Mode ? ramBank & 3 : 0) : ramBank;
	long off = (long) bank * 0x2000 + (addr - 0xA000);
	sram[off % sramSize] = v; sramDirty = true;
}
// The two ROM windows as pointers, so a read (every opcode fetch) is one load: they only
// change when the game switches banks.
void Machine::mapRom ()
{
	int banks = romBanks ? romBanks : 1;
	long lo = (mbc == 1 && mbc1Mode) ? (long) ((romBank & 0x60) % banks) * 0x4000 : 0;
	romLo = rom + lo;
	romHi = rom + (long) (romBank % banks) * 0x4000;
}

void Machine::writeMBC (unsigned short addr, unsigned char v)
{
	switch (mbc)
	{
	case 1:
		if (addr < 0x2000) ramOn = (v & 0x0F) == 0x0A;
		else if (addr < 0x4000) { romBank = (romBank & 0x60) | (v & 0x1F); if ((v & 0x1F) == 0) romBank |= 1; }
		else if (addr < 0x6000) { ramBank = v & 3; romBank = (romBank & 0x1F) | ((v & 3) << 5); }
		else mbc1Mode = v & 1;
		break;
	case 2:
		if (addr < 0x4000)
		{
			if (addr & 0x100) { romBank = v & 0x0F; if (!romBank) romBank = 1; }
			else ramOn = (v & 0x0F) == 0x0A;
		}
		break;
	case 3:
		if (addr < 0x2000) ramOn = (v & 0x0F) == 0x0A;
		else if (addr < 0x4000) { romBank = v & 0x7F; if (!romBank) romBank = 1; }
		else if (addr < 0x6000) { if (v <= 3) { ramBank = v; rtcSel = 0; } else if (v >= 8 && v <= 12) rtcSel = v; }
		else
		{
			if (v == 0) rtcLatchPrimed = true;
			else if (v == 1 && rtcLatchPrimed) { for (int i = 0; i < 5; i++) rtcLatched[i] = rtcReg[i]; rtcLatchPrimed = false; }
		}
		break;
	case 5:
		if (addr < 0x2000) ramOn = (v & 0x0F) == 0x0A;
		else if (addr < 0x3000) romBank = (romBank & 0x100) | v;
		else if (addr < 0x4000) romBank = (romBank & 0xFF) | ((v & 1) << 8);
		else if (addr < 0x6000) ramBank = v & 0x0F;
		break;
	}
	mapRom ();
}

unsigned char Machine::read8 (unsigned short addr)
{
	if (addr < 0x4000) return romLo[addr];
	if (addr < 0x8000) return romHi[addr - 0x4000];
	if (addr < 0xA000) return vram[vbank][addr - 0x8000];
	if (addr < 0xC000) return readSram (addr);
	if (addr < 0xD000) return wram[0][addr - 0xC000];
	if (addr < 0xE000) return wram[wbank][addr - 0xD000];
	if (addr < 0xFE00) return read8 ((unsigned short) (addr - 0x2000));	// echo RAM
	if (addr < 0xFEA0) return oam[addr - 0xFE00];
	if (addr < 0xFF00) return 0xFF;
	if (addr < 0xFF80) return readIO (addr - 0xFF00);
	if (addr < 0xFFFF) return hram[addr - 0xFF80];
	return ie;
}
void Machine::write8 (unsigned short addr, unsigned char v)
{
	if (addr < 0x8000) { writeMBC (addr, v); return; }
	if (addr < 0xA000) { vram[vbank][addr - 0x8000] = v; return; }
	if (addr < 0xC000) { writeSram (addr, v); return; }
	if (addr < 0xD000) { wram[0][addr - 0xC000] = v; return; }
	if (addr < 0xE000) { wram[wbank][addr - 0xD000] = v; return; }
	if (addr < 0xFE00) { write8 ((unsigned short) (addr - 0x2000), v); return; }
	if (addr < 0xFEA0) { oam[addr - 0xFE00] = v; return; }
	if (addr < 0xFF00) return;
	if (addr < 0xFF80) { writeIO (addr - 0xFF00, v); return; }
	if (addr < 0xFFFF) { hram[addr - 0xFF80] = v; return; }
	ie = v;
}

unsigned char Machine::readIO (int r)
{
	switch (r)
	{
	case 0x00:
	{
		unsigned char sel = io[0x00] & 0x30, lo = 0x0F;
		if (!(sel & 0x10)) lo &= (unsigned char) ~(buttons & 0x0F);			// directions
		if (!(sel & 0x20)) lo &= (unsigned char) ~((buttons >> 4) & 0x0F);	// A B Select Start
		return (unsigned char) (0xC0 | sel | lo);
	}
	case 0x02: return (unsigned char) (io[0x02] | 0x7E);
	case 0x04: return (unsigned char) (divCounter >> 8);
	case 0x07: return (unsigned char) (io[0x07] | 0xF8);
	case 0x0F: return (unsigned char) (io[0x0F] | 0xE0);
	case 0x41: return (unsigned char) (io[0x41] | 0x80);
	case 0x44: return (unsigned char) ly;
	case 0x4D: return cgb ? (unsigned char) ((doubleSpeed ? 0x80 : 0) | (io[0x4D] & 1) | 0x7E) : 0xFF;
	case 0x4F: return cgb ? (unsigned char) (0xFE | vbank) : 0xFF;
	case 0x55: return cgb ? (unsigned char) (hdmaActive ? ((hdmaLen / 16 - 1) & 0x7F) : 0xFF) : 0xFF;
	case 0x68: case 0x6A: return cgb ? (unsigned char) (io[r] | 0x40) : 0xFF;
	case 0x69: return cgb ? bgPalCgb[io[0x68] & 0x3F] : 0xFF;
	case 0x6B: return cgb ? obPalCgb[io[0x6A] & 0x3F] : 0xFF;
	case 0x70: return cgb ? (unsigned char) (0xF8 | wbank) : 0xFF;
	}
	if (r >= 0x10 && r < 0x40) { apuFlush (); return apuRead (r); }
	return io[r];
}

void Machine::writeIO (int r, unsigned char v)
{
	if (r >= 0x10 && r < 0x40) { apuFlush (); apuWrite (r, v); return; }
	switch (r)
	{
	case 0x00: io[0x00] = (unsigned char) (v & 0x30); return;
	case 0x01: io[0x01] = v; return;
	case 0x02:							// serial: an instant transfer (nothing is connected)
		io[0x02] = v;
		if ((v & 0x81) == 0x81)
		{
			if (serialLen < (int) sizeof serial - 1) { serial[serialLen++] = (char) io[0x01]; serial[serialLen] = 0; }
			io[0x01] = 0xFF; io[0x02] &= 0x7F; irq (3);
		}
		return;
	case 0x04: divCounter = 0; return;
	case 0x0F: io[0x0F] = (unsigned char) (v & 0x1F); return;
	case 0x40:
		if ((io[0x40] & 0x80) && !(v & 0x80)) { ly = 0; lineDots = 0; io[0x41] &= 0xFC; winLine = 0; }	// LCD off
		if (!(io[0x40] & 0x80) && (v & 0x80)) { ly = 0; lineDots = 0; setMode (2); }
		io[0x40] = v; return;
	case 0x41: io[0x41] = (unsigned char) ((io[0x41] & 0x07) | (v & 0x78)); checkStat (); return;
	case 0x44: return;
	case 0x45: io[0x45] = v; checkStat (); return;
	case 0x46:							// OAM DMA (at once)
	{
		unsigned short src = (unsigned short) (v << 8);
		for (int i = 0; i < 0xA0; i++) oam[i] = read8 ((unsigned short) (src + i));
		io[0x46] = v; return;
	}
	case 0x4D: if (cgb) io[0x4D] = (unsigned char) (v & 1); return;
	case 0x4F: if (cgb) vbank = v & 1; return;
	case 0x55:							// HDMA: general (now) or during the H-blanks
		if (!cgb) return;
		if (hdmaActive && !(v & 0x80)) { hdmaActive = false; return; }	// stop an H-blank transfer
		hdmaSrc = (unsigned short) (((io[0x51] << 8) | io[0x52]) & 0xFFF0);
		hdmaDst = (unsigned short) (0x8000 | ((((io[0x53] << 8) | io[0x54]) & 0x1FF0)));
		hdmaLen = ((v & 0x7F) + 1) * 16;
		if (v & 0x80) hdmaActive = true;
		else { while (hdmaLen > 0) hdmaBlock (); }
		return;
	case 0x69: if (cgb) { int i = io[0x68] & 0x3F; bgPalCgb[i] = v; bgRGB[i >> 1] = cgbColor (bgPalCgb, i >> 1); if (io[0x68] & 0x80) io[0x68] = (unsigned char) (0x80 | ((i + 1) & 0x3F)); } return;
	case 0x6B: if (cgb) { int i = io[0x6A] & 0x3F; obPalCgb[i] = v; obRGB[i >> 1] = cgbColor (obPalCgb, i >> 1); if (io[0x6A] & 0x80) io[0x6A] = (unsigned char) (0x80 | ((i + 1) & 0x3F)); } return;
	case 0x70: if (cgb) { wbank = v & 7; if (!wbank) wbank = 1; } return;
	}
	io[r] = v;
}

void Machine::hdmaBlock ()						// 16 bytes
{
	for (int i = 0; i < 16; i++)
	{
		vram[vbank][(hdmaDst & 0x1FFF)] = read8 (hdmaSrc);
		hdmaSrc++; hdmaDst = (unsigned short) (0x8000 | ((hdmaDst + 1) & 0x1FFF));
	}
	hdmaLen -= 16;
	if (hdmaLen <= 0) { hdmaLen = 0; hdmaActive = false; }
}

// ---- CPU ------------------------------------------------------------------------------------------------
unsigned char Machine::getR (int i)
{
	switch (i) { case 0: return b; case 1: return c; case 2: return d; case 3: return e; case 4: return h; case 5: return l; case 6: return read8 (hl ()); }
	return a;
}
void Machine::setR (int i, unsigned char v)
{
	switch (i) { case 0: b = v; break; case 1: c = v; break; case 2: d = v; break; case 3: e = v; break; case 4: h = v; break; case 5: l = v; break; case 6: write8 (hl (), v); break; default: a = v; }
}
void Machine::alu (int op, unsigned char v)
{
	int r;
	switch (op)
	{
	case 0: r = a + v; f = (unsigned char) (((r & 0xFF) ? 0 : FZ) | (((a & 0xF) + (v & 0xF)) > 0xF ? FH : 0) | (r > 0xFF ? FC : 0)); a = (unsigned char) r; break;
	case 1: { int cy = (f & FC) ? 1 : 0; r = a + v + cy; f = (unsigned char) (((r & 0xFF) ? 0 : FZ) | (((a & 0xF) + (v & 0xF) + cy) > 0xF ? FH : 0) | (r > 0xFF ? FC : 0)); a = (unsigned char) r; break; }
	case 2: r = a - v; f = (unsigned char) (((r & 0xFF) ? 0 : FZ) | FN | ((a & 0xF) < (v & 0xF) ? FH : 0) | (r < 0 ? FC : 0)); a = (unsigned char) r; break;
	case 3: { int cy = (f & FC) ? 1 : 0; r = a - v - cy; f = (unsigned char) (((r & 0xFF) ? 0 : FZ) | FN | ((a & 0xF) < (v & 0xF) + cy ? FH : 0) | (r < 0 ? FC : 0)); a = (unsigned char) r; break; }
	case 4: a &= v; f = (unsigned char) ((a ? 0 : FZ) | FH); break;
	case 5: a ^= v; f = a ? 0 : FZ; break;
	case 6: a |= v; f = a ? 0 : FZ; break;
	case 7: r = a - v; f = (unsigned char) (((r & 0xFF) ? 0 : FZ) | FN | ((a & 0xF) < (v & 0xF) ? FH : 0) | (r < 0 ? FC : 0)); break;
	}
}
unsigned char Machine::inc (unsigned char v)
{
	unsigned char r = (unsigned char) (v + 1);
	f = (unsigned char) ((f & FC) | (r ? 0 : FZ) | ((v & 0xF) == 0xF ? FH : 0));
	return r;
}
unsigned char Machine::dec (unsigned char v)
{
	unsigned char r = (unsigned char) (v - 1);
	f = (unsigned char) ((f & FC) | (r ? 0 : FZ) | FN | ((v & 0xF) == 0 ? FH : 0));
	return r;
}
void Machine::addHL (unsigned short v)
{
	unsigned hv = hl (); unsigned r = hv + v;
	f = (unsigned char) ((f & FZ) | (((hv & 0xFFF) + (v & 0xFFF)) > 0xFFF ? FH : 0) | (r > 0xFFFF ? FC : 0));
	setHL ((unsigned short) r);
}
unsigned short Machine::addSP (unsigned char e8)
{
	int ev = (signed char) e8;
	f = (unsigned char) ((((sp & 0xF) + (e8 & 0xF)) > 0xF ? FH : 0) | (((sp & 0xFF) + e8) > 0xFF ? FC : 0));
	return (unsigned short) (sp + ev);
}
bool Machine::cond (int cc)
{
	switch (cc) { case 0: return !(f & FZ); case 1: return (f & FZ) != 0; case 2: return !(f & FC); }
	return (f & FC) != 0;
}

int Machine::cbOp ()
{
	int op = fetch ();
	int r = op & 7, x = op >> 6, y = (op >> 3) & 7;
	unsigned char v = getR (r);
	int cyc = r == 6 ? (x == 1 ? 12 : 16) : 8;
	if (x == 0)
	{
		unsigned char res = 0; int cy = 0;
		switch (y)
		{
		case 0: cy = v >> 7; res = (unsigned char) ((v << 1) | cy); break;			// RLC
		case 1: cy = v & 1; res = (unsigned char) ((v >> 1) | (cy << 7)); break;		// RRC
		case 2: cy = v >> 7; res = (unsigned char) ((v << 1) | ((f & FC) ? 1 : 0)); break;	// RL
		case 3: cy = v & 1; res = (unsigned char) ((v >> 1) | ((f & FC) ? 0x80 : 0)); break;	// RR
		case 4: cy = v >> 7; res = (unsigned char) (v << 1); break;				// SLA
		case 5: cy = v & 1; res = (unsigned char) ((v >> 1) | (v & 0x80)); break;		// SRA
		case 6: cy = 0; res = (unsigned char) ((v << 4) | (v >> 4)); break;			// SWAP
		case 7: cy = v & 1; res = (unsigned char) (v >> 1); break;				// SRL
		}
		f = (unsigned char) ((res ? 0 : FZ) | (cy ? FC : 0));
		setR (r, res);
	}
	else if (x == 1) f = (unsigned char) ((f & FC) | FH | ((v >> y) & 1 ? 0 : FZ));	// BIT
	else if (x == 2) setR (r, (unsigned char) (v & ~(1 << y)));				// RES
	else setR (r, (unsigned char) (v | (1 << y)));						// SET
	return cyc;
}

// HALT: nothing happens until an interrupt, so jump to the next moment one can come -- the
// PPU's next mode change or the timer's overflow (the rest ticks as usual).
int Machine::haltSkip ()
{
	int ds = doubleSpeed ? 2 : 1, cyc;
	if (io[0x40] & 0x80)
	{
		int mode = io[0x41] & 3;
		int next = ly >= 144 ? 456 : mode == 2 ? 80 : mode == 3 ? 252 : 456;
		cyc = (next - lineDots) * ds;
	}
	else cyc = (70224 - lineDots) * ds;
	if (io[0x07] & 4)
	{
		static const int bits[4] = { 9, 3, 5, 7 };
		int period = 1 << (bits[io[0x07] & 3] + 1);
		int t = period - (int) (divCounter & (unsigned) (period - 1)) + (255 - io[0x05]) * period;
		if (t < cyc) cyc = t;
	}
	if (cyc > 456 * 2) cyc = 456 * 2;
	cyc &= ~3;
	return cyc < 4 ? 4 : cyc;
}

int Machine::step ()
{
	// interrupts
	unsigned char pend = (unsigned char) (ie & io[0x0F] & 0x1F);
	if (pend)
	{
		halted = false;
		if (ime)
		{
			int bit = 0; while (!(pend & (1 << bit))) bit++;
			io[0x0F] &= (unsigned char) ~(1 << bit);
			ime = false;
			push (pc);
			pc = (unsigned short) (0x40 + bit * 8);
			return 20;
		}
	}
	if (halted) return haltSkip ();
	if (eiDelay) { eiDelay--; if (!eiDelay) ime = true; }

	int op = fetch ();
	// LD r, r' / HALT
	if (op >= 0x40 && op < 0x80)
	{
		if (op == 0x76)
		{
			if (!ime && (ie & io[0x0F] & 0x1F)) haltBug = true; else halted = true;
			return 4;
		}
		int dst = (op >> 3) & 7, src = op & 7;
		setR (dst, getR (src));
		return (dst == 6 || src == 6) ? 8 : 4;
	}
	if (op >= 0x80 && op < 0xC0) { int src = op & 7; alu ((op >> 3) & 7, getR (src)); return src == 6 ? 8 : 4; }

	switch (op)
	{
	case 0x00: return 4;
	case 0x01: setBC (fetch16 ()); return 12;
	case 0x11: setDE (fetch16 ()); return 12;
	case 0x21: setHL (fetch16 ()); return 12;
	case 0x31: sp = fetch16 (); return 12;
	case 0x02: write8 (bc (), a); return 8;
	case 0x12: write8 (de (), a); return 8;
	case 0x22: write8 (hl (), a); setHL ((unsigned short) (hl () + 1)); return 8;
	case 0x32: write8 (hl (), a); setHL ((unsigned short) (hl () - 1)); return 8;
	case 0x0A: a = read8 (bc ()); return 8;
	case 0x1A: a = read8 (de ()); return 8;
	case 0x2A: a = read8 (hl ()); setHL ((unsigned short) (hl () + 1)); return 8;
	case 0x3A: a = read8 (hl ()); setHL ((unsigned short) (hl () - 1)); return 8;
	case 0x03: setBC ((unsigned short) (bc () + 1)); return 8;
	case 0x13: setDE ((unsigned short) (de () + 1)); return 8;
	case 0x23: setHL ((unsigned short) (hl () + 1)); return 8;
	case 0x33: sp++; return 8;
	case 0x0B: setBC ((unsigned short) (bc () - 1)); return 8;
	case 0x1B: setDE ((unsigned short) (de () - 1)); return 8;
	case 0x2B: setHL ((unsigned short) (hl () - 1)); return 8;
	case 0x3B: sp--; return 8;
	case 0x04: case 0x0C: case 0x14: case 0x1C: case 0x24: case 0x2C: case 0x34: case 0x3C:
	{ int r = (op >> 3) & 7; setR (r, inc (getR (r))); return r == 6 ? 12 : 4; }
	case 0x05: case 0x0D: case 0x15: case 0x1D: case 0x25: case 0x2D: case 0x35: case 0x3D:
	{ int r = (op >> 3) & 7; setR (r, dec (getR (r))); return r == 6 ? 12 : 4; }
	case 0x06: case 0x0E: case 0x16: case 0x1E: case 0x26: case 0x2E: case 0x36: case 0x3E:
	{ int r = (op >> 3) & 7; setR (r, fetch ()); return r == 6 ? 12 : 8; }
	case 0x07: { int cy = a >> 7; a = (unsigned char) ((a << 1) | cy); f = cy ? FC : 0; return 4; }
	case 0x0F: { int cy = a & 1; a = (unsigned char) ((a >> 1) | (cy << 7)); f = cy ? FC : 0; return 4; }
	case 0x17: { int cy = a >> 7; a = (unsigned char) ((a << 1) | ((f & FC) ? 1 : 0)); f = cy ? FC : 0; return 4; }
	case 0x1F: { int cy = a & 1; a = (unsigned char) ((a >> 1) | ((f & FC) ? 0x80 : 0)); f = cy ? FC : 0; return 4; }
	case 0x08: { unsigned short ad = fetch16 (); write8 (ad, (unsigned char) sp); write8 ((unsigned short) (ad + 1), (unsigned char) (sp >> 8)); return 20; }
	case 0x09: addHL (bc ()); return 8;
	case 0x19: addHL (de ()); return 8;
	case 0x29: addHL (hl ()); return 8;
	case 0x39: addHL (sp); return 8;
	case 0x10:							// STOP: the CGB speed switch
		fetch ();
		if (cgb && (io[0x4D] & 1)) { doubleSpeed = !doubleSpeed; io[0x4D] = 0; divCounter = 0; }
		return 4;
	case 0x18: { signed char o = (signed char) fetch (); pc = (unsigned short) (pc + o); return 12; }
	case 0x20: case 0x28: case 0x30: case 0x38:
	{ signed char o = (signed char) fetch (); if (cond ((op >> 3) & 3)) { pc = (unsigned short) (pc + o); return 12; } return 8; }
	case 0x27:							// DAA
	{
		int v = a;
		if (f & FN) { if (f & FH) v = (v - 6) & 0xFF; if (f & FC) v -= 0x60; }
		else { if ((f & FH) || (v & 0xF) > 9) v += 6; if ((f & FC) || v > 0x9F) v += 0x60; }
		unsigned char nf = (unsigned char) (f & (FN | FC));
		if (v > 0xFF) nf |= FC;
		a = (unsigned char) v;
		f = (unsigned char) (nf | (a ? 0 : FZ));
		return 4;
	}
	case 0x2F: a = (unsigned char) ~a; f = (unsigned char) (f | FN | FH); return 4;
	case 0x37: f = (unsigned char) ((f & FZ) | FC); return 4;
	case 0x3F: f = (unsigned char) ((f & FZ) | ((f & FC) ? 0 : FC)); return 4;
	case 0xC0: case 0xC8: case 0xD0: case 0xD8: if (cond ((op >> 3) & 3)) { pc = pop (); return 20; } return 8;
	case 0xC9: pc = pop (); return 16;
	case 0xD9: pc = pop (); ime = true; return 16;
	case 0xC2: case 0xCA: case 0xD2: case 0xDA: { unsigned short t = fetch16 (); if (cond ((op >> 3) & 3)) { pc = t; return 16; } return 12; }
	case 0xC3: pc = fetch16 (); return 16;
	case 0xE9: pc = hl (); return 4;
	case 0xC4: case 0xCC: case 0xD4: case 0xDC: { unsigned short t = fetch16 (); if (cond ((op >> 3) & 3)) { push (pc); pc = t; return 24; } return 12; }
	case 0xCD: { unsigned short t = fetch16 (); push (pc); pc = t; return 24; }
	case 0xC7: case 0xCF: case 0xD7: case 0xDF: case 0xE7: case 0xEF: case 0xF7: case 0xFF: push (pc); pc = (unsigned short) (op & 0x38); return 16;
	case 0xC1: setBC (pop ()); return 12;
	case 0xD1: setDE (pop ()); return 12;
	case 0xE1: setHL (pop ()); return 12;
	case 0xF1: { unsigned short v = pop (); a = (unsigned char) (v >> 8); f = (unsigned char) (v & 0xF0); return 12; }
	case 0xC5: push (bc ()); return 16;
	case 0xD5: push (de ()); return 16;
	case 0xE5: push (hl ()); return 16;
	case 0xF5: push ((unsigned short) ((a << 8) | f)); return 16;
	case 0xC6: case 0xCE: case 0xD6: case 0xDE: case 0xE6: case 0xEE: case 0xF6: case 0xFE: alu ((op >> 3) & 7, fetch ()); return 8;
	case 0xCB: return cbOp ();
	case 0xE0: write8 ((unsigned short) (0xFF00 | fetch ()), a); return 12;
	case 0xF0: a = read8 ((unsigned short) (0xFF00 | fetch ())); return 12;
	case 0xE2: write8 ((unsigned short) (0xFF00 | c), a); return 8;
	case 0xF2: a = read8 ((unsigned short) (0xFF00 | c)); return 8;
	case 0xEA: write8 (fetch16 (), a); return 16;
	case 0xFA: a = read8 (fetch16 ()); return 16;
	case 0xE8: sp = addSP (fetch ()); return 16;
	case 0xF8: setHL (addSP (fetch ())); return 12;
	case 0xF9: sp = hl (); return 8;
	case 0xF3: ime = false; eiDelay = 0; return 4;
	case 0xFB: if (!ime && !eiDelay) eiDelay = 2; return 4;
	}
	return 4;								// (the illegal opcodes: nothing)
}

// ---- timer / PPU / APU -----------------------------------------------------------------------------------
void Machine::timerTick (int cycles)
{
	// TIMA counts the falling edges of one DIV counter bit: as many as multiples of
	// 2^(bit+1) crossed
	static const int bits[4] = { 9, 3, 5, 7 };
	unsigned old = divCounter, now = old + (unsigned) cycles;
	divCounter = now & 0xFFFF;
	if (!(io[0x07] & 4)) return;
	int sh = bits[io[0x07] & 3] + 1;
	for (unsigned n = (now >> sh) - (old >> sh); n > 0; n--)
		if (++io[0x05] == 0) { io[0x05] = io[0x06]; irq (2); }
}

void Machine::setMode (int m)
{
	io[0x41] = (unsigned char) ((io[0x41] & 0xFC) | m);
	checkStat ();
}
void Machine::checkStat ()
{
	unsigned char s = io[0x41];
	bool coinc = ly == io[0x45];
	s = (unsigned char) ((s & 0xFB) | (coinc ? 4 : 0));
	io[0x41] = s;
	if (!(io[0x40] & 0x80)) { statLine = false; return; }
	int mode = s & 3;
	bool line = ((s & 0x40) && coinc) || ((s & 0x08) && mode == 0) || ((s & 0x10) && mode == 1) || ((s & 0x20) && mode == 2);
	if (line && !statLine) irq (1);					// the STAT line's rising edge
	statLine = line;
}

void Machine::ppuTick (int dots)
{
	if (!(io[0x40] & 0x80))						// LCD off: a frame's time still passes
	{
		lineDots += dots;
		if (lineDots >= 70224) { lineDots -= 70224; frameDone = true; }
		return;
	}
	while (dots > 0)
	{
		int mode = io[0x41] & 3;
		int next = ly >= 144 ? 456 : mode == 2 ? 80 : mode == 3 ? 252 : 456;
		int n = next - lineDots; if (n > dots) n = dots;
		lineDots += n; dots -= n;
		if (lineDots < next) break;
		if (ly >= 144)
		{
			lineDots = 0; ly++;
			if (ly > 153) { ly = 0; winLine = 0; setMode (2); }
			else checkStat ();
		}
		else if (mode == 2) setMode (3);
		else if (mode == 3)
		{
			renderLine ();
			setMode (0);
			if (hdmaActive) hdmaBlock ();
		}
		else
		{
			lineDots = 0; ly++;
			if (ly == 144) { setMode (1); irq (0); frameDone = true; }
			else setMode (2);
		}
	}
}

unsigned Machine::cgbColor (const unsigned char *pal, int i)
{
	unsigned v = (unsigned) (pal[i * 2] | (pal[i * 2 + 1] << 8));
	unsigned r = v & 31, g = (v >> 5) & 31, bl = (v >> 10) & 31;
	unsigned rr = (r << 3) | (r >> 2), gg = (g << 3) | (g >> 2), bb = (bl << 3) | (bl >> 2);	// 5 -> 8 bits
	return (rr << 16) | (gg << 8) | bb;
}

static inline unsigned rev8 (unsigned b)			// the bits of a byte, reversed
{
	b = ((b & 0xF0) >> 4) | ((b & 0x0F) << 4);
	b = ((b & 0xCC) >> 2) | ((b & 0x33) << 2);
	return ((b & 0xAA) >> 1) | ((b & 0x55) << 1);
}

// Background or window pixels [x0, x1) of the line: px, py = the map position of pixel x0
// (px wraps at 256). Tile by tile: one fetch and one decode per 8 pixels.
void Machine::drawBgSpan (unsigned *out, unsigned char *idx, unsigned char *attrs, int x0, int x1,
			  int mapBase, int px, int py, unsigned char lcdc, const unsigned *dmgPal)
{
	int x = x0;
	int rowBase = mapBase + (py >> 3) * 32;
	while (x < x1)
	{
		px &= 255;
		int mi = rowBase + (px >> 3);
		int tile = vram[0][mi];
		unsigned char attr = cgb ? vram[1][mi] : 0;
		int row = py & 7; if (attr & 0x40) row = 7 - row;
		int addr = ((lcdc & 0x10) ? tile * 16 : 0x1000 + (signed char) tile * 16) + row * 2;
		const unsigned char *bank = vram[(attr & 8) ? 1 : 0];
		unsigned lo = bank[addr], hi = bank[addr + 1];
		if (attr & 0x20)				// X flip: bit 0 first
		{
			lo = rev8 (lo); hi = rev8 (hi);
		}
		const unsigned *pal = cgb ? bgRGB + (attr & 7) * 4 : dmgPal;
		int col = px & 7, n = 8 - col;
		if (n > x1 - x) n = x1 - x;
		for (int k = 0; k < n; k++)
		{
			int bit = 7 - (col + k);
			int ci = ((lo >> bit) & 1) | (((hi >> bit) & 1) << 1);
			idx[x + k] = (unsigned char) ci; attrs[x + k] = attr;
			out[x + k] = pal[ci];
		}
		x += n; px += n;
	}
}

void Machine::renderLine ()
{
	unsigned char lcdc = io[0x40];
	unsigned *out = fb + ly * W;
	unsigned char bgIdx[W], bgAttr[W];
	// ---- background and window
	bool bgOn = cgb || (lcdc & 1);
	bool winOn = (lcdc & 0x20) && io[0x4A] <= ly && io[0x4B] <= 166 && (cgb || (lcdc & 1));
	int wx = io[0x4B] - 7;
	bool winUsed = false;
	if (!bgOn)
		for (int x = 0; x < W; x++) { bgIdx[x] = 0; bgAttr[x] = 0; out[x] = dmgColors[0]; }
	else
	{
		unsigned dmgPal[4];
		for (int k = 0; k < 4; k++) dmgPal[k] = dmgColors[(io[0x47] >> (k * 2)) & 3];
		int split = (winOn && wx < W) ? (wx > 0 ? wx : 0) : W;		// the window from there on
		if (split > 0)
			drawBgSpan (out, bgIdx, bgAttr, 0, split, (lcdc & 0x08) ? 0x1C00 : 0x1800,
				    io[0x43], (ly + io[0x42]) & 255, lcdc, dmgPal);
		if (split < W)
		{
			drawBgSpan (out, bgIdx, bgAttr, split, W, (lcdc & 0x40) ? 0x1C00 : 0x1800,
				    split - wx, winLine, lcdc, dmgPal);
			winUsed = true;
		}
	}
	if (winUsed) winLine++;
	// ---- sprites: up to 10 on the line
	if (!(lcdc & 2)) return;
	int hgt = (lcdc & 4) ? 16 : 8;
	int sel[10], ns = 0;
	for (int i = 0; i < 40 && ns < 10; i++)
	{
		int sy = oam[i * 4] - 16;
		if (ly >= sy && ly < sy + hgt) sel[ns++] = i;
	}
	// drawn so that the winner is drawn last: DMG = lower X then OAM order, CGB = OAM order
	int order[10]; for (int i = 0; i < ns; i++) order[i] = sel[i];
	if (!cgb)
		for (int i = 1; i < ns; i++)
		{
			int k = order[i], j = i;
			while (j > 0 && oam[order[j - 1] * 4 + 1] > oam[k * 4 + 1]) { order[j] = order[j - 1]; j--; }
			order[j] = k;
		}
	for (int n = ns - 1; n >= 0; n--)
	{
		int i = order[n];
		int sy = oam[i * 4] - 16, sx = oam[i * 4 + 1] - 8, tile = oam[i * 4 + 2], at = oam[i * 4 + 3];
		if (hgt == 16) tile &= 0xFE;
		int row = ly - sy; if (at & 0x40) row = hgt - 1 - row;
		const unsigned char *bank = vram[(cgb && (at & 8)) ? 1 : 0];
		int addr = tile * 16 + row * 2;
		for (int col = 0; col < 8; col++)
		{
			int x = sx + col; if (x < 0 || x >= W) continue;
			int bit = (at & 0x20) ? col : 7 - col;
			int ci = ((bank[addr] >> bit) & 1) | (((bank[addr + 1] >> bit) & 1) << 1);
			if (!ci) continue;
			if (cgb)
			{
				bool master = (lcdc & 1) != 0;		// LCDC.0 on CGB: BG / window priority on
				if (master && bgIdx[x] && ((bgAttr[x] & 0x80) || (at & 0x80))) continue;
				out[x] = obRGB[(at & 7) * 4 + ci];
			}
			else
			{
				if ((at & 0x80) && bgIdx[x]) continue;
				unsigned char pal = (at & 0x10) ? io[0x49] : io[0x48];
				out[x] = dmgColors[(pal >> (ci * 2)) & 3];
			}
		}
	}
}

// APU ---------------------------------------------------------------------------------------------------------
static const unsigned char dutyTab[4] = { 0x01, 0x81, 0x87, 0x7E };	// 12.5 %, 25 %, 50 %, 75 %

unsigned char Machine::apuRead (int r)
{
	static const unsigned char mask[0x30] = {
		0x80, 0x3F, 0x00, 0xFF, 0xBF, 0xFF, 0x3F, 0x00, 0xFF, 0xBF, 0x7F, 0xFF, 0x9F, 0xFF, 0xBF, 0xFF,
		0xFF, 0x00, 0x00, 0xBF, 0x00, 0x00, 0x70, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	if (r == 0x26)
		return (unsigned char) ((io[0x26] & 0x80) | 0x70 | (sq[0].on ? 1 : 0) | (sq[1].on ? 2 : 0) | (wv.on ? 4 : 0) | (ns.on ? 8 : 0));
	return (unsigned char) (io[r] | mask[r - 0x10]);
}

void Machine::trigger (int ch)
{
	if (ch < 2)
	{
		Square &s = sq[ch];
		int base = ch ? 0x16 : 0x11;
		s.on = s.dac;
		if (!s.len) s.len = 64;
		s.timer = (2048 - s.freq) * 4;
		s.vol = io[base + 1] >> 4; s.envPeriod = io[base + 1] & 7; s.envDir = (io[base + 1] & 8) ? 1 : -1; s.envTimer = s.envPeriod;
		if (ch == 0)
		{
			s.shadow = s.freq; s.sweepPeriod = (io[0x10] >> 4) & 7; s.sweepDir = (io[0x10] & 8) ? -1 : 1; s.sweepShift = io[0x10] & 7;
			s.sweepTimer = s.sweepPeriod ? s.sweepPeriod : 8; s.sweepOn = s.sweepPeriod || s.sweepShift;
			if (s.sweepShift && s.shadow + s.sweepDir * (s.shadow >> s.sweepShift) > 2047) s.on = false;
		}
	}
	else if (ch == 2) { wv.on = wv.dac; if (!wv.len) wv.len = 256; wv.timer = (2048 - wv.freq) * 2; wv.pos = 0; }
	else
	{
		ns.on = ns.dac; if (!ns.len) ns.len = 64; ns.lfsr = 0x7FFF;
		ns.vol = io[0x21] >> 4; ns.envPeriod = io[0x21] & 7; ns.envDir = (io[0x21] & 8) ? 1 : -1; ns.envTimer = ns.envPeriod;
	}
}

void Machine::apuWrite (int r, unsigned char v)
{
	if (r >= 0x30) { io[r] = v; return; }				// wave RAM
	if (r == 0x26)
	{
		if (!(v & 0x80))					// off: every register cleared
		{
			for (int i = 0x10; i < 0x26; i++) io[i] = 0;
			zero (sq, sizeof sq); int keepLfsr = 0x7FFF; zero (&wv, sizeof wv); zero (&ns, sizeof ns); ns.lfsr = (unsigned) keepLfsr;
		}
		io[0x26] = (unsigned char) (v & 0x80);
		return;
	}
	if (!(io[0x26] & 0x80)) return;				// (the APU is off)
	io[r] = v;
	switch (r)
	{
	case 0x11: sq[0].duty = v >> 6; sq[0].len = 64 - (v & 63); break;
	case 0x16: sq[1].duty = v >> 6; sq[1].len = 64 - (v & 63); break;
	case 0x12: case 0x17: { Square &s = sq[r == 0x12 ? 0 : 1]; s.dac = (v & 0xF8) != 0; if (!s.dac) s.on = false; break; }
	case 0x13: sq[0].freq = (sq[0].freq & 0x700) | v; break;
	case 0x18: sq[1].freq = (sq[1].freq & 0x700) | v; break;
	case 0x14: case 0x19:
	{
		Square &s = sq[r == 0x14 ? 0 : 1];
		s.freq = (s.freq & 0xFF) | ((v & 7) << 8); s.lenOn = (v & 0x40) != 0;
		if (v & 0x80) trigger (r == 0x14 ? 0 : 1);
		break;
	}
	case 0x1A: wv.dac = (v & 0x80) != 0; if (!wv.dac) wv.on = false; break;
	case 0x1B: wv.len = 256 - v; break;
	case 0x1C: wv.vol = (v >> 5) & 3; break;
	case 0x1D: wv.freq = (wv.freq & 0x700) | v; break;
	case 0x1E: wv.freq = (wv.freq & 0xFF) | ((v & 7) << 8); wv.lenOn = (v & 0x40) != 0; if (v & 0x80) trigger (2); break;
	case 0x20: ns.len = 64 - (v & 63); break;
	case 0x21: ns.dac = (v & 0xF8) != 0; if (!ns.dac) ns.on = false; break;
	case 0x22: ns.shift = v >> 4; ns.width = (v >> 3) & 1; ns.div = v & 7; break;
	case 0x23: ns.lenOn = (v & 0x40) != 0; if (v & 0x80) trigger (3); break;
	}
}

void Machine::frameSequencer ()					// 512 Hz: length, sweep, envelope
{
	int st = frameSeq; frameSeq = (frameSeq + 1) & 7;
	if (!(st & 1))
	{
		for (int i = 0; i < 2; i++) if (sq[i].lenOn && sq[i].len > 0 && --sq[i].len == 0) sq[i].on = false;
		if (wv.lenOn && wv.len > 0 && --wv.len == 0) wv.on = false;
		if (ns.lenOn && ns.len > 0 && --ns.len == 0) ns.on = false;
	}
	if (st == 2 || st == 6)
	{
		Square &s = sq[0];
		if (s.sweepOn && --s.sweepTimer <= 0)
		{
			s.sweepTimer = s.sweepPeriod ? s.sweepPeriod : 8;
			if (s.sweepPeriod)
			{
				int nf = s.shadow + s.sweepDir * (s.shadow >> s.sweepShift);
				if (nf > 2047) s.on = false;
				else if (s.sweepShift) { s.shadow = nf; s.freq = nf; if (nf + (nf >> s.sweepShift) > 2047 && s.sweepDir > 0) s.on = false; }
			}
		}
	}
	if (st == 7)
	{
		for (int i = 0; i < 2; i++)
		{
			Square &s = sq[i];
			if (s.envPeriod && --s.envTimer <= 0) { s.envTimer = s.envPeriod; int nv = s.vol + s.envDir; if (nv >= 0 && nv <= 15) s.vol = nv; }
		}
		if (ns.envPeriod && --ns.envTimer <= 0) { ns.envTimer = ns.envPeriod; int nv = ns.vol + ns.envDir; if (nv >= 0 && nv <= 15) ns.vol = nv; }
	}
}

void Machine::mixSample ()
{
	int out[4];
	for (int i = 0; i < 2; i++) out[i] = (sq[i].on && sq[i].dac) ? (((dutyTab[sq[i].duty] >> (7 - sq[i].dutyPos)) & 1) ? sq[i].vol : 0) : 0;
	if (wv.on && wv.dac && wv.vol) out[2] = wv.sample >> (wv.vol - 1); else out[2] = 0;
	out[3] = (ns.on && ns.dac) ? ((ns.lfsr & 1) ? 0 : ns.vol) : 0;
	int left = 0, right = 0;
	unsigned char pan = io[0x25];
	for (int i = 0; i < 4; i++)
	{
		int v = out[i] * 2 - 15;					// the DAC: -15..15
		if (!(i < 2 ? sq[i].dac : i == 2 ? wv.dac : ns.dac)) v = 0;
		if (pan & (0x10 << i)) left += v;
		if (pan & (1 << i)) right += v;
	}
	left *= ((io[0x24] >> 4) & 7) + 1; right *= (io[0x24] & 7) + 1;
	int n = (atail + 1) % ABUF;
	if (n == ahead) return;						// full: drop
	abuf[atail * 2] = (short) (left * 64); abuf[atail * 2 + 1] = (short) (right * 64);
	atail = n;
}

void Machine::apuFlush ()
{
	// in slices that end where an output sample falls, so each sample sees its own moment
	while (apuPending > 0)
	{
		long long need = (CPU_HZ - sampleAcc + rate - 1) / rate;
		int n = need < 1 ? 1 : need < apuPending ? (int) need : apuPending;
		apuPending -= n;
		apuTick (n);
	}
}

void Machine::apuTick (int cycles)				// cycles at the normal-speed rate
{
	if (!(io[0x26] & 0x80)) { sampleAcc += (long long) cycles * rate; while (sampleAcc >= CPU_HZ) { sampleAcc -= CPU_HZ; mixSample (); } return; }
	for (int i = 0; i < 2; i++)
	{
		Square &s = sq[i];
		s.timer -= cycles;
		while (s.timer <= 0) { s.timer += (2048 - s.freq) * 4; s.dutyPos = (s.dutyPos + 1) & 7; }
	}
	wv.timer -= cycles;
	while (wv.timer <= 0)
	{
		wv.timer += (2048 - wv.freq) * 2;
		wv.pos = (wv.pos + 1) & 31;
		unsigned char byte = io[0x30 + wv.pos / 2];
		wv.sample = (wv.pos & 1) ? (byte & 0x0F) : (byte >> 4);
	}
	ns.timer -= cycles;
	while (ns.timer <= 0)
	{
		int div = ns.div ? ns.div * 16 : 8;
		ns.timer += div << ns.shift;
		unsigned bit = (ns.lfsr ^ (ns.lfsr >> 1)) & 1;
		ns.lfsr = (ns.lfsr >> 1) | (bit << 14);
		if (ns.width) ns.lfsr = (ns.lfsr & ~0x40u) | (bit << 6);
	}
	fsTimer += cycles;
	while (fsTimer >= 8192) { fsTimer -= 8192; frameSequencer (); }
	sampleAcc += (long long) cycles * rate;
	while (sampleAcc >= CPU_HZ) { sampleAcc -= CPU_HZ; mixSample (); }
}

int Machine::audioRead (short *lr, int maxFrames)
{
	int n = 0;
	while (ahead != atail && n < maxFrames) { lr[n * 2] = abuf[ahead * 2]; lr[n * 2 + 1] = abuf[ahead * 2 + 1]; ahead = (ahead + 1) % ABUF; n++; }
	return n;
}

void Machine::rtcTick (int cycles)
{
	if (rtcReg[4] & 0x40) return;					// halted
	rtcCycles += cycles;
	while (rtcCycles >= CPU_HZ)
	{
		rtcCycles -= CPU_HZ;
		if (++rtcReg[0] < 60) continue;
		rtcReg[0] = 0; if (++rtcReg[1] < 60) continue;
		rtcReg[1] = 0; if (++rtcReg[2] < 24) continue;
		rtcReg[2] = 0;
		int day = (rtcReg[3] | ((rtcReg[4] & 1) << 8)) + 1;
		if (day > 511) { day = 0; rtcReg[4] |= 0x80; }
		rtcReg[3] = day & 0xFF; rtcReg[4] = (rtcReg[4] & 0xFE) | (day >> 8);
	}
}

void Machine::tick (int cycles)
{
	timerTick (cycles);
	int dots = doubleSpeed ? cycles / 2 : cycles;
	ppuTick (dots);
	apuPending += dots;						// the APU in batches: at most one
	if (apuPending >= 64) apuFlush ();				// output sample in 64 cycles
	if (mbc == 3) rtcTick (dots);
}

void Machine::runFrame ()
{
	if (!rom) return;
	frameDone = false;
	int guard = 0;
	while (!frameDone && guard++ < 200000)
	{
		int cyc = step ();
		tick (cyc);
	}
}

} // namespace gb
