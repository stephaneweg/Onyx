//
// nes/nes.cpp -- the cartridge (iNES / NES 2.0, the mappers), the CPU bus and the 2A03's
// 6502 core; the PPU is in nes_ppu.cpp, the APU in nes_apu.cpp.
//
#include "nes/nes.h"

namespace nes {

#include "nes/nes_optable.inc"

static void zero (void *d, int n) { u8 *p = (u8 *) d; for (int i = 0; i < n; i++) p[i] = 0; }

Machine::Machine () : prg (0), prgSize (0), chrRom (0), chrSize (0), rate (48000)
{
	zero (fb, sizeof fb); zero (sram, sizeof sram);
	pal = false; mapper = 0; battery = sramDirty = false;
}
Machine::~Machine () {}

void Machine::setSaveRam (const u8 *data, int n)
{
	for (int i = 0; i < n && i < (int) sizeof sram; i++) sram[i] = data[i];
	sramDirty = false;
}

void Machine::setPal (bool on)
{
	pal = on;
	cpuHz = pal ? 1662607 : 1789773;
}

// ---- the cartridge ---------------------------------------------------------------------------------
bool Machine::load (const u8 *d, int size)
{
	if (!d || size < 16 + 0x4000 || d[0] != 'N' || d[1] != 'E' || d[2] != 'S' || d[3] != 0x1A) return false;
	bool nes2 = (d[7] & 0x0C) == 0x08;
	u32 prgBanks = d[4], chrBanks = d[5];
	if (nes2) { prgBanks |= (u32) (d[9] & 0x0F) << 8; chrBanks |= (u32) (d[9] & 0xF0) << 4; }
	mapper = (d[6] >> 4) | (d[7] & 0xF0);
	if (nes2) mapper |= (d[8] & 0x0F) << 8;
	if (!nes2 && (d[7] & 0x0C) == 0x04) mapper &= 0x0F;	// ("DiskDude!" junk in the header)
	if (mapper != 0 && mapper != 1 && mapper != 2 && mapper != 3 && mapper != 4 && mapper != 7 && mapper != 66) return false;
	battery = (d[6] & 2) != 0;
	fourScreen = (d[6] & 8) != 0;
	mirror = fourScreen ? 4 : (d[6] & 1) ? 1 : 0;
	int off = 16 + ((d[6] & 4) ? 512 : 0);			// (a trainer: skipped)
	prgSize = prgBanks * 0x4000; chrSize = chrBanks * 0x2000;
	if (prgSize == 0 || off + (int) prgSize > size) return false;
	prg = d + off;
	if (chrSize && off + (int) (prgSize + chrSize) <= size) { chrRom = d + off + prgSize; chrIsRam = false; }
	else { chrRom = 0; chrSize = 0x2000; chrIsRam = true; }
	setPal (nes2 ? (d[12] & 3) == 1 : (d[9] & 1) != 0);
	reset ();
	return true;
}

void Machine::mapperReset ()
{
	mmc1Shift = 0; mmc1Count = 0; mmc1Ctrl = 0x0C; mmc1Chr0 = 0; mmc1Chr1 = 0; mmc1Prg = 0; mmc1LastWrite = 0;
	mmc3Sel = 0; for (int i = 0; i < 8; i++) mmc3Regs[i] = 0;
	mmc3Regs[0] = 0; mmc3Regs[1] = 2; mmc3Regs[2] = 4; mmc3Regs[3] = 5; mmc3Regs[4] = 6; mmc3Regs[5] = 7; mmc3Regs[6] = 0; mmc3Regs[7] = 1;
	mmc3Latch = 0; mmc3Counter = 0; mmc3Reload = false; mmc3IrqOn = false; mmc3Irq = false; mmc3PrgMode = false; mmc3ChrMode = false;
	simpleBank = 0; sramOn = true; a12 = false; a12LowSince = 0;
	updateBanks ();
}

// The mapper's registers -> the 8 KB PRG windows and the 1 KB CHR windows.
void Machine::updateBanks ()
{
	u32 prg8 = prgSize / 0x2000, chr1 = chrSize / 0x400;
	auto P = [&] (int slot, u32 bank8) { prgMap[slot] = (bank8 % prg8) * 0x2000; };
	auto C = [&] (int slot, u32 bank1) { chrMap[slot] = (bank1 % chr1) * 0x400; };
	switch (mapper)
	{
	case 0:
		for (int i = 0; i < 4; i++) P (i, (u32) i);
		for (int i = 0; i < 8; i++) C (i, (u32) i);
		break;
	case 1:
	{
		u32 outer = prgSize > 0x40000 ? (mmc1Chr0 & 0x10) * 2 : 0;	// (SUROM: 512 KB in 256 KB halves)
		u32 bank = (mmc1Prg & 0x0F), mode = (mmc1Ctrl >> 2) & 3;
		if (mode < 2) { P (0, outer + (bank & 0x0E) * 2); P (1, outer + (bank & 0x0E) * 2 + 1); P (2, outer + (bank | 1) * 2); P (3, outer + (bank | 1) * 2 + 1); }
		else if (mode == 2) { P (0, outer); P (1, outer + 1); P (2, outer + bank * 2); P (3, outer + bank * 2 + 1); }
		else { P (0, outer + bank * 2); P (1, outer + bank * 2 + 1); P (2, outer + 30); P (3, outer + 31); }
		if (prgSize <= 0x40000 && mode == 3) { P (2, prg8 - 2); P (3, prg8 - 1); }
		if (prgSize <= 0x40000 && mode == 2) { P (0, 0); P (1, 1); }
		if (mmc1Ctrl & 0x10) { for (int i = 0; i < 4; i++) { C (i, (u32) (mmc1Chr0 * 4 + i)); C (i + 4, (u32) (mmc1Chr1 * 4 + i)); } }
		else for (int i = 0; i < 8; i++) C (i, (u32) ((mmc1Chr0 & 0x1E) * 4 + i));
		static const int M[4] = { 2, 3, 1, 0 };
		mirror = M[mmc1Ctrl & 3];
		sramOn = !(mmc1Prg & 0x10);
		break;
	}
	case 2:
		P (0, simpleBank * 2u); P (1, simpleBank * 2u + 1); P (2, prg8 - 2); P (3, prg8 - 1);
		for (int i = 0; i < 8; i++) C (i, (u32) i);
		break;
	case 3:
		for (int i = 0; i < 4; i++) P (i, (u32) i);
		for (int i = 0; i < 8; i++) C (i, simpleBank * 8u + (u32) i);
		break;
	case 4:
	{
		u32 r6 = mmc3Regs[6] & 0x3F, r7 = mmc3Regs[7] & 0x3F;
		if (!mmc3PrgMode) { P (0, r6); P (1, r7); P (2, prg8 - 2); P (3, prg8 - 1); }
		else { P (0, prg8 - 2); P (1, r7); P (2, r6); P (3, prg8 - 1); }
		int lo = mmc3ChrMode ? 4 : 0, hi = mmc3ChrMode ? 0 : 4;
		C (lo + 0, mmc3Regs[0] & 0xFE); C (lo + 1, mmc3Regs[0] | 1);
		C (lo + 2, mmc3Regs[1] & 0xFE); C (lo + 3, mmc3Regs[1] | 1);
		for (int i = 0; i < 4; i++) C (hi + i, mmc3Regs[2 + i]);
		break;
	}
	case 7:
		for (int i = 0; i < 4; i++) P (i, (simpleBank & 7) * 4u + (u32) i);
		for (int i = 0; i < 8; i++) C (i, (u32) i);
		mirror = (simpleBank & 0x10) ? 3 : 2;
		break;
	case 66:
		for (int i = 0; i < 4; i++) P (i, ((simpleBank >> 4) & 3) * 4u + (u32) i);
		for (int i = 0; i < 8; i++) C (i, (simpleBank & 3) * 8u + (u32) i);
		break;
	}
}

void Machine::mapperWrite (u16 addr, u8 val)
{
	switch (mapper)
	{
	case 1:
		if (val & 0x80) { mmc1Shift = 0; mmc1Count = 0; mmc1Ctrl |= 0x0C; updateBanks (); break; }
		if (cycles == mmc1LastWrite + 1) break;		// (RMW: the 2nd of two writes in a row is ignored)
		mmc1LastWrite = cycles;
		mmc1Shift = (u8) ((mmc1Shift >> 1) | ((val & 1) << 4));
		if (++mmc1Count == 5)
		{
			switch ((addr >> 13) & 3)
			{
			case 0: mmc1Ctrl = mmc1Shift; break;
			case 1: mmc1Chr0 = mmc1Shift; break;
			case 2: mmc1Chr1 = mmc1Shift; break;
			case 3: mmc1Prg = mmc1Shift; break;
			}
			mmc1Shift = 0; mmc1Count = 0;
			updateBanks ();
		}
		break;
	case 2: case 3: case 7: case 66:
		simpleBank = val;
		updateBanks ();
		break;
	case 4:
		switch (addr & 0xE001)
		{
		case 0x8000: mmc3Sel = val & 7; mmc3PrgMode = (val & 0x40) != 0; mmc3ChrMode = (val & 0x80) != 0; updateBanks (); break;
		case 0x8001: mmc3Regs[mmc3Sel] = val; updateBanks (); break;
		case 0xA000: if (!fourScreen) mirror = (val & 1) ? 0 : 1; break;
		case 0xA001: sramOn = (val & 0x80) != 0; break;
		case 0xC000: mmc3Latch = val; break;
		case 0xC001: mmc3Counter = 0; mmc3Reload = true; break;
		case 0xE000: mmc3IrqOn = false; mmc3Irq = false; break;
		case 0xE001: mmc3IrqOn = true; break;
		}
		break;
	}
}

// The CPU moving the PPU's address ($2006, $2007 accesses) also clocks the MMC3 when A12 rises
// after being low for a while (the chip filters short pulses).
void Machine::a12Access (u16 ad)
{
	bool hi = (ad & 0x1000) != 0;
	if (hi && !a12 && cycles - a12LowSince >= 3) mmc3Clock ();
	if (!hi && a12) a12LowSince = cycles;
	a12 = hi;
}

void Machine::mmc3Clock ()
{
	if (mapper != 4) return;
	if (mmc3Counter == 0 || mmc3Reload) { mmc3Counter = mmc3Latch; mmc3Reload = false; }
	else mmc3Counter--;
	if (mmc3Counter == 0 && mmc3IrqOn) mmc3Irq = true;
}

// ---- reset ----------------------------------------------------------------------------------------
void Machine::reset ()
{
	zero (ram, sizeof ram); zero (ciram, sizeof ciram); zero (oam, sizeof oam); zero (palRam, sizeof palRam);
	if (chrIsRam) zero (chrRam, sizeof chrRam);
	mapperReset ();
	a = x = y = 0; s = 0xFD; p = 0x24;
	nmiPending = false; irqLine = false; stall = 0; cycles = 0;
	ctrl = mask = status = oamAddr = 0; v = t = 0; fineX = 0; wLatch = false; readBuf = 0; openBus = 0;
	line = 0; dot = 0; oddFrame = false; frameDone = false; sprite0Dot = -1; dotAcc = 0; nmiOccurred = false;
	zero (pu, sizeof pu); zero (&tr, sizeof tr); zero (&no, sizeof no); zero (&dmc, sizeof dmc);
	no.lfsr = 1; dmc.bits = 8; dmc.silence = true; dmc.rate = 428; dmc.timer = 428;
	frameMode = 0; frameIrqOff = false; frameIrq = false; frameCycle = 0; apuPending = 0; instrCyc = 0; apuAhead = 0;
	sampleAcc = 0; hpAcc = 0; lastOut = 0; ahead = atail = 0;
	pad = 0; padShift = 0; strobe = false;
	pc = read16 (0xFFFC);
}

// ---- the CPU bus -------------------------------------------------------------------------------------
u8 Machine::read (u16 ad)
{
	if (ad < 0x2000) return ram[ad & 0x7FF];
	if (ad >= 0x8000) return prg[prgMap[(ad >> 13) & 3] + (ad & 0x1FFF)];
	if (ad < 0x4000) return regRead (ad & 7);
	if (ad >= 0x6000) return sramOn ? sram[ad & 0x1FFF] : (u8) (ad >> 8);
	if (ad == 0x4015) { apuSync (); return apuStatus (); }
	if (ad == 0x4016)
	{
		u8 bit;
		if (strobe) bit = pad & 1;
		else { bit = padShift & 1; padShift = (u8) ((padShift >> 1) | 0x80); }
		return (u8) (0x40 | bit);
	}
	if (ad == 0x4017) return 0x40;
	return (u8) (ad >> 8);						// (open bus)
}

void Machine::write (u16 ad, u8 val)
{
	if (ad < 0x2000) { ram[ad & 0x7FF] = val; return; }
	if (ad >= 0x8000) { mapperWrite (ad, val); return; }
	if (ad < 0x4000) { regWrite (ad & 7, val); return; }
	if (ad >= 0x6000)
	{
		if (sramOn) { if (sram[ad & 0x1FFF] != val) { sram[ad & 0x1FFF] = val; if (battery) sramDirty = true; } }
		return;
	}
	if (ad == 0x4014)						// OAM DMA: 256 bytes, the CPU stops 513-514 cycles
	{
		u16 base = (u16) (val << 8);
		for (int i = 0; i < 256; i++) oam[(oamAddr + i) & 0xFF] = read ((u16) (base + i));
		stall += 513 + (int) (cycles & 1);
		return;
	}
	if (ad == 0x4016)
	{
		strobe = (val & 1) != 0;
		if (strobe) padShift = pad;
		else padShift = pad;
		return;
	}
	if (ad <= 0x4017) { apuSync (); apuWrite (ad, val); }
}

// ---- the CPU ---------------------------------------------------------------------------------------------
void Machine::interrupt (u16 vector, bool brk)
{
	push ((u8) (pc >> 8)); push ((u8) pc);
	push ((u8) ((p & ~0x10) | 0x20 | (brk ? 0x10 : 0)));
	p |= 0x04;
	pc = read16 (vector);
}

void Machine::adc (u8 m)
{
	int r = a + m + (p & 1);
	p = (u8) ((p & ~0x41) | (r > 0xFF ? 1 : 0) | ((~(a ^ m) & (a ^ r) & 0x80) ? 0x40 : 0));
	a = (u8) r;
	setZN (a);
}

void Machine::cmp (u8 r, u8 m)
{
	int d = r - m;
	p = (u8) ((p & ~1) | (d >= 0 ? 1 : 0));
	setZN ((u8) d);
}

int Machine::step ()
{
	// interrupts: NMI on an edge (set by the PPU), IRQ while the line is low and I is clear
	apuAhead = 0; instrCyc = 7;
	if (nmiPending) { nmiPending = false; interrupt (0xFFFA, false); return 7; }
	irqLine = mmc3Irq || frameIrq || dmc.irq;
	if (irqLine && !(p & 0x04)) { interrupt (0xFFFE, false); return 7; }

	u8 opc = read (pc++);
	const OpInfo &o = OPS[opc];
	int cyc = o.cycles;
	u16 ad = 0;
	switch (o.mode)
	{
	case M_IMP: case M_ACC: break;
	case M_IMM: ad = pc++; break;
	case M_ZP: ad = read (pc++); break;
	case M_ZPX: ad = (u8) (read (pc++) + x); break;
	case M_ZPY: ad = (u8) (read (pc++) + y); break;
	case M_ABS: ad = read16 (pc); pc += 2; break;
	case M_ABX: { u16 b = read16 (pc); pc += 2; ad = (u16) (b + x); if (o.cross && ((b ^ ad) & 0xFF00)) cyc++; break; }
	case M_ABY: { u16 b = read16 (pc); pc += 2; ad = (u16) (b + y); if (o.cross && ((b ^ ad) & 0xFF00)) cyc++; break; }
	case M_IND: { u16 b = read16 (pc); pc += 2; ad = (u16) (read (b) | (read ((u16) ((b & 0xFF00) | ((b + 1) & 0xFF))) << 8)); break; }
	case M_IZX: { u8 z = (u8) (read (pc++) + x); ad = (u16) (read (z) | (read ((u8) (z + 1)) << 8)); break; }
	case M_IZY: { u8 z = read (pc++); u16 b = (u16) (read (z) | (read ((u8) (z + 1)) << 8)); ad = (u16) (b + y); if (o.cross && ((b ^ ad) & 0xFF00)) cyc++; break; }
	case M_REL: ad = pc++; break;
	}
	instrCyc = cyc;

	auto branch = [&] (bool c) {
		if (!c) return;
		u16 target = (u16) (pc + (signed char) read (ad));
		cyc += ((target ^ pc) & 0xFF00) ? 2 : 1;
		pc = target;
	};
	// read-modify-write helper: the value at ad (or A), written back
	auto rmwRead = [&] () -> u8 { return o.mode == M_ACC ? a : read (ad); };
	auto rmwWrite = [&] (u8 val) { if (o.mode == M_ACC) a = val; else write (ad, val); };

	switch (o.op)
	{
	case O_ADC: adc (read (ad)); break;
	case O_SBC: adc ((u8) ~read (ad)); break;
	case O_AND: a &= read (ad); setZN (a); break;
	case O_ORA: a |= read (ad); setZN (a); break;
	case O_EOR: a ^= read (ad); setZN (a); break;
	case O_CMP: cmp (a, read (ad)); break;
	case O_CPX: cmp (x, read (ad)); break;
	case O_CPY: cmp (y, read (ad)); break;
	case O_BIT: { u8 m = read (ad); p = (u8) ((p & 0x3D) | (m & 0xC0) | ((a & m) ? 0 : 2)); break; }
	case O_LDA: a = read (ad); setZN (a); break;
	case O_LDX: x = read (ad); setZN (x); break;
	case O_LDY: y = read (ad); setZN (y); break;
	case O_STA: write (ad, a); break;
	case O_STX: write (ad, x); break;
	case O_STY: write (ad, y); break;
	case O_ASL: { u8 m = rmwRead (); p = (u8) ((p & ~1) | (m >> 7)); m <<= 1; setZN (m); rmwWrite (m); break; }
	case O_LSR: { u8 m = rmwRead (); p = (u8) ((p & ~1) | (m & 1)); m >>= 1; setZN (m); rmwWrite (m); break; }
	case O_ROL: { u8 m = rmwRead (); u8 c = p & 1; p = (u8) ((p & ~1) | (m >> 7)); m = (u8) ((m << 1) | c); setZN (m); rmwWrite (m); break; }
	case O_ROR: { u8 m = rmwRead (); u8 c = p & 1; p = (u8) ((p & ~1) | (m & 1)); m = (u8) ((m >> 1) | (c << 7)); setZN (m); rmwWrite (m); break; }
	case O_INC: { u8 m = (u8) (read (ad) + 1); write (ad, m); setZN (m); break; }
	case O_DEC: { u8 m = (u8) (read (ad) - 1); write (ad, m); setZN (m); break; }
	case O_INX: x++; setZN (x); break;
	case O_INY: y++; setZN (y); break;
	case O_DEX: x--; setZN (x); break;
	case O_DEY: y--; setZN (y); break;
	case O_TAX: x = a; setZN (x); break;
	case O_TAY: y = a; setZN (y); break;
	case O_TXA: a = x; setZN (a); break;
	case O_TYA: a = y; setZN (a); break;
	case O_TSX: x = s; setZN (x); break;
	case O_TXS: s = x; break;
	case O_PHA: push (a); break;
	case O_PHP: push ((u8) (p | 0x30)); break;
	case O_PLA: a = pull (); setZN (a); break;
	case O_PLP: p = (u8) ((pull () & 0xCF) | 0x20); break;
	case O_CLC: p &= ~1; break;
	case O_SEC: p |= 1; break;
	case O_CLI: p &= ~4; break;
	case O_SEI: p |= 4; break;
	case O_CLV: p &= ~0x40; break;
	case O_CLD: p &= ~8; break;
	case O_SED: p |= 8; break;
	case O_BPL: branch (!(p & 0x80)); break;
	case O_BMI: branch ((p & 0x80) != 0); break;
	case O_BVC: branch (!(p & 0x40)); break;
	case O_BVS: branch ((p & 0x40) != 0); break;
	case O_BCC: branch (!(p & 1)); break;
	case O_BCS: branch ((p & 1) != 0); break;
	case O_BNE: branch (!(p & 2)); break;
	case O_BEQ: branch ((p & 2) != 0); break;
	case O_JMP: pc = ad; break;
	case O_JSR: { u16 r = (u16) (pc - 1); push ((u8) (r >> 8)); push ((u8) r); pc = ad; break; }
	case O_RTS: { u8 lo = pull (); u8 hi = pull (); pc = (u16) (((hi << 8) | lo) + 1); break; }
	case O_RTI: { p = (u8) ((pull () & 0xCF) | 0x20); u8 lo = pull (); u8 hi = pull (); pc = (u16) ((hi << 8) | lo); break; }
	case O_BRK: pc++; interrupt (0xFFFE, true); break;
	case O_NOP: if (o.mode != M_IMP) (void) read (ad); break;
	case O_KIL: pc--; break;						// (a jam: stays here)
	// the stable unofficial ones
	case O_LAX: a = x = read (ad); setZN (a); break;
	case O_SAX: write (ad, (u8) (a & x)); break;
	case O_DCP: { u8 m = (u8) (read (ad) - 1); write (ad, m); cmp (a, m); break; }
	case O_ISB: { u8 m = (u8) (read (ad) + 1); write (ad, m); adc ((u8) ~m); break; }
	case O_SLO: { u8 m = read (ad); p = (u8) ((p & ~1) | (m >> 7)); m <<= 1; write (ad, m); a |= m; setZN (a); break; }
	case O_RLA: { u8 m = read (ad); u8 c = p & 1; p = (u8) ((p & ~1) | (m >> 7)); m = (u8) ((m << 1) | c); write (ad, m); a &= m; setZN (a); break; }
	case O_SRE: { u8 m = read (ad); p = (u8) ((p & ~1) | (m & 1)); m >>= 1; write (ad, m); a ^= m; setZN (a); break; }
	case O_RRA: { u8 m = read (ad); u8 c = p & 1; p = (u8) ((p & ~1) | (m & 1)); m = (u8) ((m >> 1) | (c << 7)); write (ad, m); adc (m); break; }
	case O_ANC: a &= read (ad); setZN (a); p = (u8) ((p & ~1) | (a >> 7)); break;
	case O_ALR: a &= read (ad); p = (u8) ((p & ~1) | (a & 1)); a >>= 1; setZN (a); break;
	case O_ARR:
		a &= read (ad);
		a = (u8) ((a >> 1) | ((p & 1) << 7));
		setZN (a);
		p = (u8) ((p & ~0x41) | ((a >> 6) & 1) | (((a >> 6) ^ (a >> 5)) & 1 ? 0x40 : 0));
		break;
	case O_AXS: { int r = (a & x) - read (ad); p = (u8) ((p & ~1) | (r >= 0 ? 1 : 0)); x = (u8) r; setZN (x); break; }
	case O_LXA: a = x = read (ad); setZN (a); break;
	case O_XAA: a = (u8) ((a | 0xEE) & x & read (ad)); setZN (a); break;
	case O_LAS: { u8 m = (u8) (read (ad) & s); a = x = s = m; setZN (m); break; }
	case O_SHY: { u8 hi = (u8) ((ad >> 8) + 1); u8 val = (u8) (y & hi); if (((ad - x) ^ ad) & 0xFF00) ad = (u16) ((val << 8) | (ad & 0xFF)); write (ad, val); break; }
	case O_SHX: { u8 hi = (u8) ((ad >> 8) + 1); u8 val = (u8) (x & hi); if (((ad - y) ^ ad) & 0xFF00) ad = (u16) ((val << 8) | (ad & 0xFF)); write (ad, val); break; }
	case O_SHA: write (ad, (u8) (a & x & ((ad >> 8) + 1))); break;
	case O_TAS: s = (u8) (a & x); write (ad, (u8) (s & ((ad >> 8) + 1))); break;
	}
	return cyc;
}

// An APU register access happens on the instruction's last cycle: the APU catches up to it.
void Machine::apuSync ()
{
	apuFlush ();
	int target = instrCyc - 1;
	if (apuAhead < target) { apuRun (target - apuAhead); apuAhead = target; }
}

// ---- a frame ------------------------------------------------------------------------------------------
void Machine::runFrame ()
{
	if (!prg) return;
	frameDone = false;
	int guard = 0;
	while (!frameDone && guard++ < 200000)
	{
		int c;
		if (stall) { c = stall; stall = 0; }
		else c = step ();
		cycles += (u64) c;
		apuPending += c - apuAhead;				// (the cycles apuSync has not run yet)
		apuAhead = 0;
		apuFlush ();						// (the APU's IRQ and $4015 in step with the CPU)
		ppuRun (c);
	}
	apuFlush ();
}

} // namespace nes
