//
// gba/gba.cpp -- the machine: memory map and wait states, I/O registers, the frame loop
// (events: the PPU's line phases and the timers' overflows), timers, DMA, the saves.
// The CPU is in gba_cpu.cpp, the BIOS calls in gba_bios.cpp, the PPU in gba_ppu.cpp and
// the sound in gba_apu.cpp.
//
#include "gba/gba.h"

namespace gba {

enum { CPU_HZ = 16777216 };

void cpuInit ();

static void zero (void *p, unsigned n) { u8 *b = (u8 *) p; while (n--) *b++ = 0; }
static inline u16 ld16 (const u8 *p) { return (u16) (p[0] | (p[1] << 8)); }
static inline u32 ld32 (const u8 *p) { return (u32) p[0] | ((u32) p[1] << 8) | ((u32) p[2] << 16) | ((u32) p[3] << 24); }
static inline void st16 (u8 *p, u16 v) { p[0] = (u8) v; p[1] = (u8) (v >> 8); }
static inline void st32 (u8 *p, u32 v) { p[0] = (u8) v; p[1] = (u8) (v >> 8); p[2] = (u8) (v >> 16); p[3] = (u8) (v >> 24); }
static inline u32 vramOff (u32 a) { a &= 0x1FFFF; return a >= 0x18000 ? a - 0x8000 : a; }

// The BIOS's IRQ entry (the only BIOS code run: the calls are done in C++):
//   0x18: stmfd sp!, {r0-r3, r12, lr}; mov r0, #0x04000000; add lr, pc, #0;
//         ldr pc, [r0, #-4]; ldmfd sp!, {r0-r3, r12, lr}; subs pc, lr, #4
static const u32 BIOS_IRQ[6] = { 0xE92D500F, 0xE3A00301, 0xE28FE000, 0xE510F004, 0xE8BD500F, 0xE25EF004 };

Machine::Machine () : saveType (SAVE_NONE), save (saveBuf), saveSize (0), saveDirty (false),
	rom (0), romSize (0), romMask (0), rate (32768), keys (0)
{
	title[0] = 0; code[0] = 0;
	cpuInit ();
	zero (fb, sizeof fb);
	reset ();
}
Machine::~Machine () {}

bool Machine::load (const u8 *data, int size)
{
	if (!data || size < 0xC0) return false;
	rom = data; romSize = (u32) size;
	for (int i = 0; i < 12; i++) { char c = (char) data[0xA0 + i]; title[i] = c >= 32 && c < 127 ? c : 0; }
	title[12] = 0;
	for (int i = 0; i < 4; i++) { char c = (char) data[0xAC + i]; code[i] = c >= 32 && c < 127 ? c : ' '; }
	code[4] = 0;
	detectSave ();
	reset ();
	return true;
}

void Machine::detectSave ()
{
	// the save library's name in the ROM: "EEPROM_V", "SRAM_V", "FLASH_V", "FLASH512_V", "FLASH1M_V"
	saveType = SAVE_NONE; saveSize = 0;
	static const struct { const char *s; int type, size; } ids[] = {
		{ "EEPROM_V", SAVE_EEPROM, 0x2000 }, { "SRAM_V", SAVE_SRAM, 0x8000 }, { "SRAM_F_V", SAVE_SRAM, 0x8000 },
		{ "FLASH1M_V", SAVE_FLASH128, 0x20000 }, { "FLASH512_V", SAVE_FLASH64, 0x10000 }, { "FLASH_V", SAVE_FLASH64, 0x10000 } };
	for (u32 i = 0; i + 12 < romSize && saveType == SAVE_NONE; i += 4)
		for (unsigned k = 0; k < sizeof ids / sizeof ids[0]; k++)
		{
			const char *s = ids[k].s; u32 j = 0;
			while (s[j] && rom[i + j] == (u8) s[j]) j++;
			if (!s[j]) { saveType = ids[k].type; saveSize = ids[k].size; break; }
		}
	for (int i = 0; i < (int) sizeof saveBuf; i++) saveBuf[i] = 0xFF;
	eeAddrBits = 0;					// EEPROM: 512 B or 8 KB, told by the first DMA
}

void Machine::setSaveRam (const u8 *data, int n)
{
	if (!data || n <= 0) return;
	if (n > (int) sizeof saveBuf) n = sizeof saveBuf;
	for (int i = 0; i < n; i++) saveBuf[i] = data[i];
	if (saveType == SAVE_EEPROM) { eeAddrBits = n <= 512 ? 6 : 14; saveSize = n <= 512 ? 512 : 0x2000; }
	if (saveType == SAVE_NONE)				// (no library name found: guess from the size)
	{
		saveType = n == 0x20000 ? SAVE_FLASH128 : n == 0x10000 ? SAVE_FLASH64 : n <= 0x2000 ? SAVE_EEPROM : SAVE_SRAM;
		saveSize = n;
	}
	saveDirty = false;
}

void Machine::reset ()
{
	zero (bios, sizeof bios); zero (ewram, sizeof ewram); zero (iwram, sizeof iwram);
	zero (pal, sizeof pal); zero (vram, sizeof vram); zero (oam, sizeof oam); zero (io, sizeof io);
	for (int i = 0; i < 6; i++) st32 (bios + 0x18 + i * 4, BIOS_IRQ[i]);
	st32 (bios + 0x34, 0xE55EC002);				// (what a read of the BIOS gives after an IRQ)
	biosLatch = 0xE129F000;
	for (int i = 0; i < 16; i++) r[i] = 0;
	for (int i = 0; i < 6; i++) { bankR13[i] = bankR14[i] = bankSpsr[i] = 0; }
	for (int i = 0; i < 5; i++) bankFiq[i] = bankUsr[i] = 0;
	cpsr = 0x1F; spsr = 0;
	bankR13[3] = 0x03007FE0; bankR13[2] = 0x03007FA0; r[13] = 0x03007F00;
	r[15] = 0x08000000;
	halted = false; branched = false; pipeOk = false; irqLine = false; intrWait = 0; fetchPC = 0x08000000; cyc = 0;
	now = 0; lineStart = 0; inHblank = false; vcount = 0; frameDone = false;
	for (int i = 0; i < 4; i++) { tm[i].reload = tm[i].counter = tm[i].ctl = 0; tm[i].last = 0; tm[i].sub = 0; }
	for (int i = 0; i < 4; i++) { dma[i].src = dma[i].dst = dma[i].cnt = 0; dma[i].ctl = 0; dma[i].on = false; }
	st16 (io + 0x20, 0x100); st16 (io + 0x26, 0x100); st16 (io + 0x30, 0x100); st16 (io + 0x36, 0x100);
	affX[0] = affY[0] = affX[1] = affY[1] = 0;
	st16 (io + 0x88, 0x200);				// SOUNDBIAS
	io[0x300] = 1;						// POSTFLG
	updateWait ();
	// sound
	zero (sq, sizeof sq); zero (&wv, sizeof wv); zero (&ns, sizeof ns); ns.lfsr = 0x7FFF;
	zero (waveRam, sizeof waveRam);
	frameSeq = 0; fsTimer = 0; psgAcc = 0;
	for (int c = 0; c < 2; c++) { fifoLen[c] = fifoRd[c] = fifoWr[c] = 0; dsOut[c] = 0; }
	sampleAcc = 0; ahead = atail = 0; apuLast = 0;
	// saves
	flashState = 0; flashBank = 0; flashId = flashErase = flashWrite = flashBankSel = false;
	eeBits = 0; eeBuf = 0; eeState = 0; eeCmd = 0; eeAddr = 0; eeReadPos = 0; evDirty = false;
}

void Machine::setAudioRate (int hz) { rate = hz > 1000 ? hz : 32768; }

// ---- wait states ----------------------------------------------------------------------------------
void Machine::updateWait ()
{
	static const int NTAB[4] = { 4, 3, 2, 8 };
	u16 w = io16 (0x204);
	for (int i = 0; i < 16; i++) { waitN[i] = waitS[i] = 1; waitN32[i] = waitS32[i] = 1; }
	waitN[2] = waitS[2] = 3; waitN32[2] = waitS32[2] = 6;			// EWRAM
	waitN32[5] = waitS32[5] = waitN32[6] = waitS32[6] = 2;			// palette, VRAM (16-bit bus)
	int ws[3][2] = { { NTAB[(w >> 2) & 3], (w & 0x10) ? 1 : 2 }, { NTAB[(w >> 5) & 3], (w & 0x80) ? 1 : 4 },
			 { NTAB[(w >> 8) & 3], (w & 0x400) ? 1 : 8 } };
	for (int k = 0; k < 3; k++)
		for (int j = 0; j < 2; j++)
		{
			int reg = 8 + k * 2 + j;
			waitN[reg] = 1 + ws[k][0]; waitS[reg] = 1 + ws[k][1];
			waitN32[reg] = waitN[reg] + waitS[reg]; waitS32[reg] = 2 * waitS[reg];
		}
	waitN[14] = waitS[14] = waitN[15] = waitS[15] = 1 + NTAB[w & 3];
	waitN32[14] = waitS32[14] = waitN32[15] = waitS32[15] = waitN[14];
	prefetch = (w & 0x4000) != 0;
}

// ---- memory ---------------------------------------------------------------------------------------
u8 Machine::read8 (u32 a)
{
	switch (a >> 24)
	{
	case 0x0: if (a < 0x4000) return fetchPC < 0x4000 ? bios[a] : (u8) (biosLatch >> ((a & 3) * 8)); return (u8) (openBus () >> ((a & 3) * 8));
	case 0x2: return ewram[a & 0x3FFFF];
	case 0x3: return iwram[a & 0x7FFF];
	case 0x4: if ((a & 0xFFFFFF) < 0x400) return (u8) (ioRead16 (a & 0x3FE) >> ((a & 1) * 8)); return 0;
	case 0x5: return pal[a & 0x3FF];
	case 0x6: return vram[vramOff (a)];
	case 0x7: return oam[a & 0x3FF];
	case 0x8: case 0x9: case 0xA: case 0xB: case 0xC: case 0xD:
	{
		u32 o = a & 0x1FFFFFF;
		if (saveType == SAVE_EEPROM && (a >> 24) == 0xD && (romSize <= 0x1000000 || o >= 0x1FFFF00)) return (u8) eepromRead ();
		if (o < romSize) return rom[o];
		return (u8) ((a >> 1) >> ((a & 1) * 8));
	}
	case 0xE: case 0xF: return saveRead8 (a);
	}
	return (u8) (openBus () >> ((a & 3) * 8));
}

u16 Machine::read16 (u32 a)
{
	if ((a >> 25) == 7) return (u16) (saveRead8 (a) * 0x0101);	// (SRAM: the byte at a, twice)
	a &= ~1u;
	switch (a >> 24)
	{
	case 0x0: if (a < 0x4000) return fetchPC < 0x4000 ? ld16 (bios + a) : (u16) (biosLatch >> ((a & 2) * 8)); return (u16) (openBus () >> ((a & 2) * 8));
	case 0x2: return ld16 (ewram + (a & 0x3FFFF));
	case 0x3: return ld16 (iwram + (a & 0x7FFF));
	case 0x4: if ((a & 0xFFFFFF) < 0x400) return ioRead16 (a & 0x3FE); return 0;
	case 0x5: return ld16 (pal + (a & 0x3FF));
	case 0x6: return ld16 (vram + vramOff (a));
	case 0x7: return ld16 (oam + (a & 0x3FF));
	case 0x8: case 0x9: case 0xA: case 0xB: case 0xC: case 0xD:
	{
		u32 o = a & 0x1FFFFFF;
		if (saveType == SAVE_EEPROM && (a >> 24) == 0xD && (romSize <= 0x1000000 || o >= 0x1FFFF00)) return eepromRead ();
		if (o < romSize) return ld16 (rom + o);
		return (u16) (a >> 1);
	}
	case 0xE: case 0xF: return (u16) (saveRead8 (a) * 0x0101);
	}
	return (u16) (openBus () >> ((a & 2) * 8));
}

u32 Machine::read32 (u32 a)
{
	if ((a >> 25) == 7) return saveRead8 (a) * 0x01010101u;
	a &= ~3u;
	switch (a >> 24)
	{
	case 0x0: if (a < 0x4000) return fetchPC < 0x4000 ? ld32 (bios + a) : biosLatch; return openBus ();
	case 0x2: return ld32 (ewram + (a & 0x3FFFF));
	case 0x3: return ld32 (iwram + (a & 0x7FFF));
	case 0x4: if ((a & 0xFFFFFF) < 0x400) return ioRead16 (a & 0x3FC) | ((u32) ioRead16 ((a & 0x3FC) + 2) << 16); return 0;
	case 0x5: return ld32 (pal + (a & 0x3FF));
	case 0x6: return ld32 (vram + vramOff (a));
	case 0x7: return ld32 (oam + (a & 0x3FF));
	case 0x8: case 0x9: case 0xA: case 0xB: case 0xC: case 0xD:
	{
		u32 o = a & 0x1FFFFFF;
		if (o + 3 < romSize) return ld32 (rom + o);
		return (u32) ((a >> 1) & 0xFFFF) | ((((a + 2) >> 1) & 0xFFFF) << 16);
	}
	case 0xE: case 0xF: return saveRead8 (a) * 0x01010101u;
	}
	return openBus ();
}

#ifdef GBA_DEBUG
u32 g_watchAddr = 0; void (*g_watchHit) (u32 a, u32 v, const u32 *regs, int size) = 0;
#define WATCH(a, v, n) do { if (g_watchHit && ((a) & ~3u) == g_watchAddr) g_watchHit (a, v, r, n); } while (0)
#else
#define WATCH(a, v, n) do {} while (0)
#endif

void Machine::write8 (u32 a, u8 v)
{
	WATCH (a, v, 1);
	switch (a >> 24)
	{
	case 0x2: ewram[a & 0x3FFFF] = v; return;
	case 0x3: iwram[a & 0x7FFF] = v; return;
	case 0x4: if ((a & 0xFFFFFF) < 0x400) ioWrite8 (a & 0x3FF, v); return;
	case 0x5: st16 (pal + (a & 0x3FE), (u16) (v * 0x0101)); return;
	case 0x6:
	{
		u32 o = vramOff (a);
		u32 lim = (io[0] & 7) >= 3 ? 0x14000 : 0x10000;		// byte writes: the BG area only
		if (o < lim) st16 (vram + (o & ~1u), (u16) (v * 0x0101));
		return;
	}
	case 0x7: return;							// (OAM: no byte writes)
	case 0xE: case 0xF: saveWrite8 (a, v); return;
	}
}

void Machine::write16 (u32 a, u16 v)
{
	WATCH (a, v, 2);
	if ((a >> 25) == 7) { saveWrite8 (a, (u8) (v >> ((a & 1) * 8))); return; }	// (SRAM: one byte)
	a &= ~1u;
	switch (a >> 24)
	{
	case 0x2: st16 (ewram + (a & 0x3FFFF), v); return;
	case 0x3: st16 (iwram + (a & 0x7FFF), v); return;
	case 0x4: if ((a & 0xFFFFFF) < 0x400) ioWrite16 (a & 0x3FE, v); return;
	case 0x5: st16 (pal + (a & 0x3FF), v); return;
	case 0x6: st16 (vram + vramOff (a), v); return;
	case 0x7: st16 (oam + (a & 0x3FF), v); return;
	case 0xD: if (saveType == SAVE_EEPROM && (romSize <= 0x1000000 || (a & 0x1FFFFFF) >= 0x1FFFF00)) eepromWrite (v); return;
	case 0xE: case 0xF: saveWrite8 (a, (u8) (v >> ((a & 1) * 8))); return;
	}
}

void Machine::write32 (u32 a, u32 v)
{
	WATCH (a, v, 4);
	if ((a >> 25) == 7) { saveWrite8 (a, (u8) (v >> ((a & 3) * 8))); return; }
	a &= ~3u;
	switch (a >> 24)
	{
	case 0x2: st32 (ewram + (a & 0x3FFFF), v); return;
	case 0x3: st32 (iwram + (a & 0x7FFF), v); return;
	case 0x4: if ((a & 0xFFFFFF) < 0x400) { ioWrite16 (a & 0x3FC, (u16) v); ioWrite16 ((a & 0x3FC) + 2, (u16) (v >> 16)); } return;
	case 0x5: st32 (pal + (a & 0x3FF), v); return;
	case 0x6: st32 (vram + vramOff (a), v); return;
	case 0x7: st32 (oam + (a & 0x3FF), v); return;
	case 0xE: case 0xF: saveWrite8 (a, (u8) (v >> ((a & 3) * 8))); return;
	}
}

// ---- I/O ------------------------------------------------------------------------------------------
u16 Machine::ioRead16 (u32 off)
{
	switch (off)
	{
	case 0x004:
	{
		u16 s = (u16) (io16 (0x004) & 0xFF38);
		if (vcount >= 160 && vcount < 227) s |= 1;
		if (inHblank) s |= 2;
		if (vcount == (io[0x005])) s |= 4;
		return s;
	}
	case 0x006: return (u16) vcount;
	case 0x100: case 0x104: case 0x108: case 0x10C: return timerRead ((int) (off - 0x100) >> 2);
	case 0x102: case 0x106: case 0x10A: case 0x10E: return tm[(off - 0x102) >> 2].ctl;
	case 0x130: return keyInput ();
	case 0x0BA: case 0x0C6: case 0x0D2: case 0x0DE: return dma[(off - 0x0BA) / 12].ctl;
	case 0x0B8: case 0x0C4: case 0x0D0: case 0x0DC: return 0;
	}
	if (off >= 0x060 && off < 0x0A8) return (u16) (apuRead (off) | (apuRead (off + 1) << 8));
	if (off >= 0x0B0 && off < 0x0E0) return 0;				// (DMA addresses: write-only)
	return io16 (off);
}

void Machine::ioWrite8 (u32 off, u8 v)
{
	if (off >= 0x060 && off < 0x0A8) { apuRun (); if (off >= 0xA0) fifoPush (off >= 0xA4 ? 1 : 0, v | 0x100u); else apuWrite (off, v); return; }
	if (off == 0x202 || off == 0x203) { io[off] &= (u8) ~v; checkIrq (); return; }		// IF: 1 clears
	if (off == 0x301) { halted = true; io[0x301] = v; return; }				// HALTCNT
	u32 base = off & ~1u;
	u16 cur = io16 (base);
	u16 nv = (off & 1) ? (u16) ((cur & 0x00FF) | (v << 8)) : (u16) ((cur & 0xFF00) | v);
	ioWrite16 (base, nv);
}

void Machine::ioWrite16 (u32 off, u16 v)
{
	if (off >= 0x060 && off < 0x0A8)
	{
		apuRun ();
		if (off >= 0xA0) { fifoPush (off >= 0xA4 ? 1 : 0, (v & 0xFF) | 0x100u); fifoPush (off >= 0xA4 ? 1 : 0, (v >> 8) | 0x100u); return; }
		apuWrite (off, (u8) v); apuWrite (off + 1, (u8) (v >> 8));
		return;
	}
	switch (off)
	{
	case 0x004: st16 (io + 4, (u16) ((io16 (4) & 7) | (v & 0xFFF8))); return;
	case 0x006: return;
	case 0x028: case 0x02A: st16 (io + off, v); affX[0] = (s32) (ld32 (io + 0x28) << 4) >> 4; return;
	case 0x02C: case 0x02E: st16 (io + off, v); affY[0] = (s32) (ld32 (io + 0x2C) << 4) >> 4; return;
	case 0x038: case 0x03A: st16 (io + off, v); affX[1] = (s32) (ld32 (io + 0x38) << 4) >> 4; return;
	case 0x03C: case 0x03E: st16 (io + off, v); affY[1] = (s32) (ld32 (io + 0x3C) << 4) >> 4; return;
	case 0x100: case 0x104: case 0x108: case 0x10C: timerWrite ((int) (off - 0x100) >> 2, false, v); return;
	case 0x102: case 0x106: case 0x10A: case 0x10E: timerWrite ((int) (off - 0x102) >> 2, true, v); return;
	case 0x0BA: case 0x0C6: case 0x0D2: case 0x0DE: dmaWrite ((int) (off - 0x0BA) / 12, v); return;
	case 0x130: return;
	case 0x200: st16 (io + 0x200, v); checkIrq (); return;
	case 0x202: st16 (io + 0x202, (u16) (io16 (0x202) & ~v)); checkIrq (); return;
	case 0x204: st16 (io + 0x204, v); updateWait (); return;
	case 0x208: st16 (io + 0x208, v); checkIrq (); return;
	case 0x300: io[0x300] = (u8) v; if (v & 0xFF00) { halted = true; io[0x301] = (u8) (v >> 8); } return;
	}
	st16 (io + off, v);
}

void Machine::raise (int bit)
{
	st16 (io + 0x202, (u16) (io16 (0x202) | (1 << bit)));
	checkIrq ();
}

// ---- timers ---------------------------------------------------------------------------------------
static const int PRESCALE[4] = { 1, 64, 256, 1024 };

void Machine::timersUpdate ()
{
	for (int i = 0; i < 4; i++)
	{
		Timer &t = tm[i];
		if (!(t.ctl & 0x80) || (i > 0 && (t.ctl & 4))) continue;		// off, or counting overflows
		int ps = PRESCALE[t.ctl & 3];
		if (now <= t.last) continue;
		u64 ticks = (now - t.last) / (u64) ps;
		t.last += ticks * (u64) ps;
		u32 c = t.counter;
		while (ticks > 0)
		{
			u32 room = 0x10000 - c;
			if (ticks < room) { c += (u32) ticks; ticks = 0; }
			else { ticks -= room; c = t.reload; t.counter = (u16) c; timerOverflow (i); c = t.counter; }
		}
		t.counter = (u16) c;
	}
}

u64 Machine::timersNext ()
{
	u64 best = ~0ull;
	for (int i = 0; i < 4; i++)
	{
		const Timer &t = tm[i];
		if (!(t.ctl & 0x80) || (i > 0 && (t.ctl & 4))) continue;
		// only the timers whose overflow does something: an interrupt, the sound, a cascade
		bool useful = (t.ctl & 0x40) || i < 2 || (i < 3 && (tm[i + 1].ctl & 0x84) == 0x84);
		if (!useful) continue;
		u64 at = t.last + (u64) (0x10000 - t.counter) * (u64) PRESCALE[t.ctl & 3];
		if (at < best) best = at;
	}
	return best;
}

void Machine::timerOverflow (int i)
{
	if (tm[i].ctl & 0x40) raise (3 + i);
	if (i < 2) fifoTimer (i);
	if (i < 3)
	{
		Timer &n = tm[i + 1];
		if ((n.ctl & 0x84) == 0x84)
		{
			if (++n.counter == 0) { n.counter = n.reload; timerOverflow (i + 1); }
		}
	}
}

u16 Machine::timerRead (int i) { timersUpdate (); return tm[i].counter; }

void Machine::timerWrite (int i, bool ctl, u16 v)
{
	timersUpdate ();
	Timer &t = tm[i];
	if (!ctl) { t.reload = v; return; }
	bool was = (t.ctl & 0x80) != 0;
	t.ctl = (u16) (v & 0xC7);
	evDirty = true;
	if (!was && (v & 0x80)) { t.counter = t.reload; t.last = now; }
	else if (v & 0x80) t.last = now - (now - t.last) % (u64) PRESCALE[t.ctl & 3];
}

// ---- DMA ------------------------------------------------------------------------------------------
void Machine::dmaWrite (int ch, u16 v)
{
	Dma &d = dma[ch];
	bool was = (d.ctl & 0x8000) != 0;
	d.ctl = v;
	if (!(v & 0x8000)) { d.on = false; return; }
	if (!was)
	{
		u32 base = 0x0B0 + ch * 12;
		d.src = ld32 (io + base) & (ch == 0 ? 0x07FFFFFF : 0x0FFFFFFF);
		d.dst = ld32 (io + base + 4) & (ch == 3 ? 0x0FFFFFFF : 0x07FFFFFF);
		u32 c = io16 (base + 8);
		d.cnt = c ? c : (ch == 3 ? 0x10000 : 0x4000);
		d.on = true;
		// EEPROM: the transfer's length gives the address size (9 / 73 bits: 512 B, 17 / 81: 8 KB)
		if (ch == 3 && saveType == SAVE_EEPROM && (d.dst >> 24) == 0xD && !eeAddrBits)
		{
			if (d.cnt == 9 || d.cnt == 73) { eeAddrBits = 6; saveSize = 512; }
			else if (d.cnt == 17 || d.cnt == 81) { eeAddrBits = 14; saveSize = 0x2000; }
		}
		if (((v >> 12) & 3) == 0) dmaRun (ch);
	}
}

void Machine::dmaStart (int timing)
{
	for (int ch = 0; ch < 4; ch++)
		if (dma[ch].on && ((dma[ch].ctl >> 12) & 3) == timing && !(timing == 3 && (ch == 1 || ch == 2))) dmaRun (ch);
}

void Machine::dmaRun (int ch)
{
	Dma &d = dma[ch];
	u16 c = d.ctl;
	bool word = (c & 0x400) != 0;
	int dstMode = (c >> 5) & 3, srcMode = (c >> 7) & 3;
	u32 n = d.cnt;
	bool fifo = ((c >> 12) & 3) == 3 && (ch == 1 || ch == 2);
	if (fifo) { n = 4; word = true; dstMode = 2; }
	int step = word ? 4 : 2;
	int sInc = srcMode == 0 ? step : srcMode == 1 ? -step : 0;
	int dInc = (dstMode == 0 || dstMode == 3) ? step : dstMode == 1 ? -step : 0;
	u32 s = d.src, t = d.dst;
	int region = (int) ((s >> 24) & 15), dregion = (int) ((t >> 24) & 15);
	for (u32 i = 0; i < n; i++)
	{
		if (word) write32 (t & ~3u, read32 (s & ~3u));
		else write16 (t & ~1u, read16 (s & ~1u));
		s += (u32) sInc; t += (u32) dInc;
	}
	now += 2 + (u64) n * (u64) ((word ? waitS32[region] + waitS32[dregion] : waitS[region] + waitS[dregion]));
	d.src = s;
	if (dstMode != 3) d.dst = t;
	if (c & 0x4000) raise (8 + ch);
	if ((c & 0x200) && ((c >> 12) & 3) != 0)			// repeat
	{
		u32 cc = io16 (0x0B0 + ch * 12 + 8);
		d.cnt = cc ? cc : (ch == 3 ? 0x10000 : 0x4000);
		if (dstMode == 3) d.dst = ld32 (io + 0x0B0 + ch * 12 + 4) & (ch == 3 ? 0x0FFFFFFF : 0x07FFFFFF);
	}
	else { d.on = false; d.ctl &= 0x7FFF; }
}

// ---- the frame loop ---------------------------------------------------------------------------------
void Machine::ppuEvent ()
{
	u16 stat = io16 (0x004);
	if (!inHblank)
	{
		inHblank = true;
		if (vcount < 160) { renderLine (); dmaStart (2); }
		if (stat & 0x10) raise (1);
	}
	else
	{
		inHblank = false; lineStart += 1232;
		if (vcount < 160)
		{
			affX[0] += (s16) io16 (0x22); affY[0] += (s16) io16 (0x26);
			affX[1] += (s16) io16 (0x32); affY[1] += (s16) io16 (0x36);
		}
		if (++vcount == 228) vcount = 0;
		if (vcount == 160)
		{
			if (stat & 0x08) raise (0);
			dmaStart (1);
			latchAffine ();
			frameDone = true;
		}
		if (vcount == (stat >> 8) && (stat & 0x20)) raise (2);
	}
}

void Machine::runFrame ()
{
	if (!rom) return;
	frameDone = false;
	while (!frameDone)
	{
		u64 target = nextPpu (), t = timersNext ();
		if (t < target) target = t;
		while (now < target)
		{
			if (halted)
			{
				if (ieReg () & ifReg ()) halted = false;
				else { now = target; break; }
			}
			if (irqLine && !(cpsr & 0x80)) irq ();
			now += (u64) ((cpsr & 0x20) ? stepThumb () : stepArm ());
			if (evDirty) { evDirty = false; break; }		// a timer was (re)started
		}
		if (now >= nextPpu ()) ppuEvent ();
		timersUpdate ();
	}
	apuRun ();
}

// ---- saves ------------------------------------------------------------------------------------------
u8 Machine::saveRead8 (u32 a)
{
	if (saveType == SAVE_SRAM) return saveBuf[a & 0x7FFF];
	if (saveType == SAVE_FLASH64 || saveType == SAVE_FLASH128)
	{
		u32 off = a & 0xFFFF;
		if (flashId && off < 2)
			return saveType == SAVE_FLASH128 ? (off ? 0x13 : 0x62) : (off ? 0x1B : 0x32);	// Sanyo / Panasonic
		return saveBuf[(u32) flashBank * 0x10000 + off];
	}
	return 0xFF;
}

void Machine::saveWrite8 (u32 a, u8 v)
{
	if (saveType == SAVE_SRAM) { saveBuf[a & 0x7FFF] = v; saveDirty = true; return; }
	if (saveType != SAVE_FLASH64 && saveType != SAVE_FLASH128) return;
	u32 off = a & 0xFFFF;
	if (flashWrite) { saveBuf[(u32) flashBank * 0x10000 + off] = v; flashWrite = false; saveDirty = true; return; }
	if (flashBankSel) { if (off == 0) flashBank = v & 1; flashBankSel = false; return; }
	switch (flashState)
	{
	case 0: if (off == 0x5555 && v == 0xAA) flashState = 1; break;
	case 1: flashState = (off == 0x2AAA && v == 0x55) ? 2 : 0; break;
	case 2:
		flashState = 0;
		if (flashErase && v == 0x30)					// sector erase (4 KB)
		{
			u32 b = (u32) flashBank * 0x10000 + (off & 0xF000);
			for (u32 i = 0; i < 0x1000; i++) saveBuf[b + i] = 0xFF;
			flashErase = false; saveDirty = true; break;
		}
		if (off != 0x5555) break;
		if (v == 0x90) flashId = true;
		else if (v == 0xF0) flashId = false;
		else if (v == 0x80) flashErase = true;
		else if (v == 0x10 && flashErase)
		{
			for (int i = 0; i < saveSize; i++) saveBuf[i] = 0xFF;
			flashErase = false; saveDirty = true;
		}
		else if (v == 0xA0) flashWrite = true;
		else if (v == 0xB0 && saveType == SAVE_FLASH128) flashBankSel = true;
		break;
	}
}

// EEPROM: a serial protocol, one bit per 16-bit access (always DMA 3). Requests: "11" + address
// + "0" = read (then 4 dummy bits and the 64 data bits come back), "10" + address + 64 bits +
// "0" = write. The address has 6 bits (512 B) or 14 (8 KB).
u16 Machine::eepromRead ()
{
	if (eeState != 2) return 1;						// ready
	int pos = eeReadPos++;
	if (pos >= 67) eeState = 0;
	if (pos < 4) return 0;
	int bit = pos - 4;
	u32 byte = (eeAddr & 0x3FF) * 8 + (u32) (bit >> 3);
	return (u16) ((saveBuf[byte & 0x1FFF] >> (7 - (bit & 7))) & 1);
}

void Machine::eepromWrite (u16 v)
{
	int ab = eeAddrBits ? eeAddrBits : 14;
	u32 mask = (1u << ab) - 1;
	if (eeState == 2) eeState = 0;						// (a new request)
	eeBuf = (eeBuf << 1) | (v & 1);
	eeBits++;
	if (eeBits == 2)
	{
		eeCmd = (int) (eeBuf & 3);
		if (eeCmd != 2 && eeCmd != 3) { eeBits = 0; eeBuf = 0; }
		return;
	}
	if (eeBits == 2 + ab) { eeAddr = (u32) eeBuf & mask; return; }
	if (eeCmd == 3 && eeBits == 2 + ab + 1)				// read request: the data follows
	{
		eeState = 2; eeReadPos = 0; eeBits = 0; eeBuf = 0;
		return;
	}
	if (eeCmd == 2 && eeBits == 2 + ab + 64)				// write: the 64 bits, first byte first
	{
		u32 byte = (eeAddr & 0x3FF) * 8;
		for (int i = 0; i < 8; i++) saveBuf[(byte + (u32) i) & 0x1FFF] = (u8) (eeBuf >> ((7 - i) * 8));
		saveDirty = true;
		return;
	}
	if (eeCmd == 2 && eeBits >= 2 + ab + 65) { eeBits = 0; eeBuf = 0; }	// (the stop bit)
}

} // namespace gba
