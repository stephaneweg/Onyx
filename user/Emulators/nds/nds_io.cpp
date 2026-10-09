//
// nds/nds_io.cpp -- the I/O registers of the two processors: the interrupts, the timers, the DMA,
// the IPC (the sync register and the FIFOs), the divider and the square root, the keys, the VRAM /
// WRAM controls, the power; the ARM7's SPI (the power manager, the firmware, the touch screen) and
// its real-time clock. The video, the 3D engine, the sound and the cartridge are given their own.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (see nds.h).
//
#include "nds/nds.h"

namespace nds {

// ---- interrupts ---------------------------------------------------------------------------------------------
void Machine::raise (int cpu, int bit)
{
	iflag[cpu] |= 1u << bit;
}

void Machine::checkIrq (int cpu) { (void) cpu; }

s64 Machine::curTime ()
{
	if (running == 0) return arm9.ts;
	if (running == 1) return arm7.ts;
	return now;
}

// ---- timers (bus cycles: master / 2) ---------------------------------------------------------------------------
static const int PS[4] = { 0, 6, 8, 10 };

static void timerAdvance (Machine *m, int cpu, int i, u64 ticks)
{
	Machine::Timer &t = m->tm[cpu][i];
	u64 c = (u64) t.counter + ticks;
	if (c < 0x10000) { t.counter = (u16) c; return; }
	u64 period = 0x10000u - t.reload;
	u64 over = 1 + (c - 0x10000) / period;
	t.counter = (u16) (t.reload + (c - 0x10000) % period);
	if (t.ctl & 0x40) m->raise (cpu, 3 + i);
	if (i < 3 && (m->tm[cpu][i + 1].ctl & 0x84) == 0x84) timerAdvance (m, cpu, i + 1, over);
}

void Machine::timerUpdate (int cpu, int i)
{
	Timer &t = tm[cpu][i];
	if (!(t.ctl & 0x80) || (i > 0 && (t.ctl & 4))) return;
	s64 bus = curTime () >> 1;
	int sh = PS[t.ctl & 3];
	s64 ticks = (bus - t.last) >> sh;
	if (ticks <= 0) return;
	t.last += ticks << sh;
	timerAdvance (this, cpu, i, (u64) ticks);
}

void Machine::timerSchedule (int cpu, int i)
{
	Timer &t = tm[cpu][i];
	int ev = EV_TIMER + cpu * 4 + i;
	cancel (ev);
	if (!(t.ctl & 0x80) || (i > 0 && (t.ctl & 4))) return;
	bool need = (t.ctl & 0x40) != 0;
	for (int k = i + 1; k < 4 && (tm[cpu][k].ctl & 0x84) == 0x84; k++) if (tm[cpu][k].ctl & 0x40) need = true;
	if (!need) return;
	int sh = PS[t.ctl & 3];
	s64 at = (t.last + ((s64) (0x10000 - t.counter) << sh)) * 2;
	schedule (ev, at);
}

void Machine::timerOverflow (int cpu, int i)
{
	timerUpdate (cpu, i);
	timerSchedule (cpu, i);
}

u16 Machine::timerRead (int cpu, int i)
{
	for (int k = 0; k <= i; k++) timerUpdate (cpu, k);
	return tm[cpu][i].counter;
}

void Machine::timerWrite (int cpu, int i, bool ctl, u16 v)
{
	Timer &t = tm[cpu][i];
	if (!ctl) { t.reload = v; return; }
	for (int k = 0; k <= i; k++) timerUpdate (cpu, k);
	u16 old = t.ctl;
	t.ctl = v & 0xC7;
	if (!(old & 0x80) && (v & 0x80)) { t.counter = t.reload; t.last = curTime () >> 1; }
	else if ((old ^ v) & 3) t.last = curTime () >> 1;
	for (int k = 0; k < 4; k++) timerSchedule (cpu, k);
}

// ---- DMA ------------------------------------------------------------------------------------------------------------
int Machine::dmaTiming (int cpu, int ch) const
{
	u32 cnt = dma[cpu][ch].cnt;
	if (!cpu) return (int) (cnt >> 27) & 7;
	static const int T7[4] = { 0, 1, 5, 6 };
	return T7[(cnt >> 28) & 3];
}

void Machine::dmaWrite (int cpu, int ch, u32 cnt)
{
	Dma &d = dma[cpu][ch];
	u32 old = d.cnt;
	d.cnt = cnt;
	if ((old & 0x80000000) || !(cnt & 0x80000000)) return;
	d.src = d.sad; d.dst = d.dad;
	u32 n = cpu ? (cnt & (ch == 3 ? 0xFFFF : 0x3FFF)) : (cnt & 0x1FFFFF);
	if (!n) n = cpu ? (ch == 3 ? 0x10000 : 0x4000) : 0x200000;
	d.count = n;
	int t = dmaTiming (cpu, ch);
	if (t == 0 || t == 7) dmaRun (cpu, ch);
	else if (t == 5 && cart.xferLeft) dmaStart (cpu, 5);
}

void Machine::dmaStart (int cpu, int timing)
{
	for (int ch = 0; ch < 4; ch++)
	{
		Dma &d = dma[cpu][ch];
		if (!(d.cnt & 0x80000000) || dmaTiming (cpu, ch) != timing) continue;
		if (timing == 5)					// the cartridge: a word at a time while there is data
		{
			int guard = 0;
			while ((d.cnt & 0x80000000) && cart.xferLeft && guard++ < 0x100000) dmaRun (cpu, ch);
			continue;
		}
		dmaRun (cpu, ch);
	}
}

void Machine::dmaRun (int cpu, int ch)
{
	Dma &d = dma[cpu][ch];
	u32 cnt = d.cnt;
	bool w32 = cnt & (1u << 26);
	s32 unit = w32 ? 4 : 2;
	int dctl = (int) (cnt >> 21) & 3, sctl = (int) (cnt >> 23) & 3;
	s32 dstep = dctl == 1 ? -unit : dctl == 2 ? 0 : unit;
	s32 sstep = sctl == 1 ? -unit : sctl == 2 ? 0 : unit;
	int timing = dmaTiming (cpu, ch);
	u32 n = d.count;
	if (timing == 5) n = 1;					// (one word each time the cartridge has one)
	int prev = running;
	u32 src = d.src, dst = d.dst;
	if (w32)
	{
		src &= ~3u; dst &= ~3u;
		for (u32 k = 0; k < n; k++) { busWrite32 (cpu, dst, busRead32 (cpu, src)); src += (u32) sstep; dst += (u32) dstep; }
	}
	else
	{
		src &= ~1u; dst &= ~1u;
		for (u32 k = 0; k < n; k++) { busWrite16 (cpu, dst, busRead16 (cpu, src)); src += (u32) sstep; dst += (u32) dstep; }
	}
	d.src = src; d.dst = dst;
	running = prev;
	Arm &c = cpu ? arm7 : arm9;
	if (running == cpu) c.ts += (s64) n * (w32 ? 2 : 1);
	if (timing == 5)
	{
		if (d.count > 1) { d.count--; return; }
	}
	if ((cnt & (1u << 25)) && timing != 0 && timing != 7)
	{
		u32 k = cpu ? (cnt & (ch == 3 ? 0xFFFF : 0x3FFF)) : (cnt & 0x1FFFFF);
		if (!k) k = cpu ? (ch == 3 ? 0x10000 : 0x4000) : 0x200000;
		d.count = k;
		if (dctl == 3) d.dst = d.dad;
	}
	else d.cnt &= ~0x80000000u;
	if (cnt & (1u << 30)) raise (cpu, 8 + ch);
}

// ---- IPC ----------------------------------------------------------------------------------------------------------------
void Machine::ipcSend (int cpu, u32 v)
{
	if (!(fifoCnt[cpu] & 0x8000)) return;
	if (fifoN[cpu] >= 16) { fifoCnt[cpu] |= 0x4000; return; }
	fifo[cpu][(fifoR[cpu] + fifoN[cpu]) & 15] = v;
	fifoN[cpu]++;
	int o = cpu ^ 1;
	if (fifoN[cpu] == 1 && (fifoCnt[o] & 0x400)) raise (o, 18);
}

u32 Machine::ipcRecv (int cpu)
{
	int o = cpu ^ 1;
	if (!fifoN[o]) { fifoCnt[cpu] |= 0x4000; return fifoLast[cpu]; }
	u32 v = fifo[o][fifoR[o]];
	if (!(fifoCnt[cpu] & 0x8000)) return v;			// (disabled: the oldest, kept)
	fifoR[o] = (fifoR[o] + 1) & 15; fifoN[o]--;
	fifoLast[cpu] = v;
	if (!fifoN[o] && (fifoCnt[o] & 4)) raise (o, 17);
	return v;
}

void Machine::ipcFifoCnt (int cpu, u16 v)
{
	u16 old = fifoCnt[cpu];
	if (v & 8) { fifoN[cpu] = 0; fifoR[cpu] = 0; fifoLast[cpu ^ 1] = 0; }
	if (v & 0x4000) old &= ~0x4000;
	fifoCnt[cpu] = (u16) ((old & 0x4000) | (v & 0x8404));
	if ((v & 4) && (!(old & 4) || (v & 8)) && !fifoN[cpu]) raise (cpu, 17);
	if ((v & 0x400) && !(old & 0x400) && fifoN[cpu ^ 1]) raise (cpu, 18);
}

u16 Machine::ipcFifoCntRead (int cpu)
{
	u16 v = fifoCnt[cpu] & 0xC404;
	if (!fifoN[cpu]) v |= 1;
	if (fifoN[cpu] == 16) v |= 2;
	if (!fifoN[cpu ^ 1]) v |= 0x100;
	if (fifoN[cpu ^ 1] == 16) v |= 0x200;
	return v;
}

// ---- divider / square root --------------------------------------------------------------------------------------------------
void Machine::divide ()
{
	int mode = divcnt & 3;
	if (divDen == 0) divcnt |= 0x4000; else divcnt &= ~0x4000;
	switch (mode)
	{
	case 0:
	{
		s32 num = (s32) divNum, den = (s32) divDen;
		if (!den) { divRes = num < 0 ? 1 : -1; divRes = (s64) (u32) divRes | ((s64) (num < 0 ? -1 : 0) << 32); divRem = num; }
		else if (num == (s32) 0x80000000 && den == -1) { divRes = (s64) 0x80000000u; divRem = 0; }
		else { divRes = num / den; divRem = num % den; }
		break;
	}
	case 1: case 3:
	{
		s64 num = divNum; s32 den = (s32) divDen;
		if (!den) { divRes = num < 0 ? 1 : -1; divRem = num; }
		else if (num == (s64) 0x8000000000000000ull && den == -1) { divRes = num; divRem = 0; }
		else { divRes = num / den; divRem = num % den; }
		break;
	}
	default:
	{
		s64 num = divNum, den = divDen;
		if (!den) { divRes = num < 0 ? 1 : -1; divRem = num; }
		else if (num == (s64) 0x8000000000000000ull && den == -1) { divRes = num; divRem = 0; }
		else { divRes = num / den; divRem = num % den; }
		break;
	}
	}
}

void Machine::squareRoot ()
{
	u64 v = (sqrtcnt & 1) ? sqrtParam : (u32) sqrtParam;
	u64 res = 0, bit = 1ull << 62;
	while (bit > v) bit >>= 2;
	while (bit) { if (v >= res + bit) { v -= res + bit; res = (res >> 1) + bit; } else res >>= 1; bit >>= 2; }
	sqrtRes = (u32) res;
}

// ---- SPI (ARM7) -----------------------------------------------------------------------------------------------------------------
void Machine::touchCalibrate (int sx, int sy, u16 &ax, u16 &ay)
{
	ax = (u16) (0x200 + (sx - 1) * 14);
	ay = (u16) (0x200 + (sy - 1) * 14);
}

void Machine::spiWrite (u8 v)
{
	if (!(spicnt & 0x8000)) return;
	int dev = (spicnt >> 8) & 3;
	bool hold = spicnt & 0x800;
	u8 out = 0;
	switch (dev)
	{
	case 0:							// the power manager
		if (!pmGotIndex) { pmIndex = v; pmGotIndex = true; out = 0; }
		else
		{
			int reg = pmIndex & 7;
			if (pmIndex & 0x80) out = pmRegs[reg];
			else { pmRegs[reg] = v; if (reg == 0 && (v & 0x40)) { /* power off: nothing */ } }
			pmGotIndex = false;
		}
		break;
	case 1:							// the firmware (a 256 KB serial flash)
		if (fwState == 0) { fwCmd = v; fwState = 1; fwAddr = 0; fwAddrGot = 0; if (v == 0x06) fwStatus |= 2; if (v == 0x04) fwStatus &= ~2; out = 0xFF; }
		else switch (fwCmd)
		{
		case 0x03: case 0x0B:
			if (fwAddrGot < 3) { fwAddr = (fwAddr << 8) | v; fwAddrGot++; out = 0xFF; }
			else if (fwCmd == 0x0B && fwAddrGot == 3) { fwAddrGot++; out = 0xFF; }
			else { out = firmware[fwAddr & 0x3FFFF]; fwAddr++; }
			break;
		case 0x05: out = fwStatus; break;
		case 0x9F: { static const u8 ID[3] = { 0x20, 0x40, 0x12 }; out = fwAddrGot < 3 ? ID[fwAddrGot] : 0xFF; fwAddrGot++; break; }
		case 0x0A: case 0x02:
			if (fwAddrGot < 3) { fwAddr = (fwAddr << 8) | v; fwAddrGot++; }
			else if (fwStatus & 2) { firmware[fwAddr & 0x3FFFF] = v; fwAddr = (fwAddr & ~0xFFu) | ((fwAddr + 1) & 0xFF); }
			out = 0xFF;
			break;
		default: out = 0xFF; break;
		}
		if (!hold) { if (fwCmd == 0x0A || fwCmd == 0x02) fwStatus &= ~2; fwState = 0; }
		break;
	case 2:							// the touch screen controller (TSC2046)
		if (tscPos == 1) out = (u8) (tscValue >> 5);
		else if (tscPos == 2) out = (u8) (tscValue << 3);
		else out = 0;
		if (v & 0x80)
		{
			int chan = (v >> 4) & 7;
			u16 ax, ay;
			touchCalibrate (touchX + 1, touchY + 1, ax, ay);
			switch (chan)
			{
			case 1: tscValue = touchDown ? ay : 0xFFF; break;
			case 5: tscValue = touchDown ? ax : 0; break;
			case 3: tscValue = touchDown ? 0x100 : 0; break;	// Z1
			case 4: tscValue = touchDown ? 0xE00 : 0xFFF; break;	// Z2
			case 6: tscValue = 0x800; break;		// (the microphone: silence)
			default: tscValue = 0xFFF; break;
			}
			if (v & 8) tscValue &= 0xFF0;
			tscPos = 1;
		}
		else tscPos++;
		break;
	default: out = 0; break;
	}
	spiData = out;
	if (!hold && dev == 0) pmGotIndex = false;
	if (spicnt & 0x4000) raise (1, 23);
}

// ---- RTC (ARM7) --------------------------------------------------------------------------------------------------------------------
static u8 bcd (int v) { return (u8) (((v / 10) << 4) | (v % 10)); }

void Machine::rtcByte (u8 v)
{
	if (rtcPos == 0)
	{
		if ((v & 0xF0) == 0x60) rtcCmd = v;
		else if ((v & 0x0F) == 0x06)				// (the bits the other way)
		{
			u8 r = 0; for (int i = 0; i < 8; i++) if (v & (1 << i)) r |= (u8) (0x80 >> i);
			rtcCmd = r;
		}
		else { rtcCmd = 0; return; }
		rtcOutPos = 0; rtcOutBit = 0;
		for (int i = 0; i < 8; i++) rtcOut[i] = 0;
		if (rtcCmd & 1)						// a read: the answer
		{
			switch ((rtcCmd >> 1) & 7)
			{
			case 0: rtcOut[0] = rtcStat1; break;
			case 1: rtcOut[0] = rtcStat2; break;
			case 2:
				rtcOut[0] = bcd (rtcYear % 100); rtcOut[1] = bcd (rtcMon); rtcOut[2] = bcd (rtcDay);
				rtcOut[3] = (u8) rtcDow;
				rtcOut[4] = bcd ((rtcStat1 & 2) ? rtcHour : rtcHour % 12); if (rtcHour >= 12) rtcOut[4] |= 0x40;
				rtcOut[5] = bcd (rtcMin); rtcOut[6] = bcd (rtcSec);
				break;
			case 3:
				rtcOut[0] = bcd ((rtcStat1 & 2) ? rtcHour : rtcHour % 12); if (rtcHour >= 12) rtcOut[0] |= 0x40;
				rtcOut[1] = bcd (rtcMin); rtcOut[2] = bcd (rtcSec);
				break;
			default: break;
			}
		}
		return;
	}
	if (!(rtcCmd & 1))						// a write
	{
		switch ((rtcCmd >> 1) & 7)
		{
		case 0: if (rtcPos == 1) rtcStat1 = (u8) ((rtcStat1 & 0xF0) | (v & 0x0E)); break;
		case 1: if (rtcPos == 1) rtcStat2 = v; break;
		default: break;							// (the time set: kept ours)
		}
	}
}

void Machine::rtcWrite (u16 v)
{
	u16 old = rtcIo;
	if (!(v & 0x10)) v = (u16) ((v & ~1u) | (old & 1));		// (data in: keep the output bit)
	rtcIo = v;
	if (!(v & 4)) { rtcPos = 0; rtcBitIn = 0; rtcIn = 0; return; }	// chip select off
	if (!(old & 4)) { rtcPos = 0; rtcBitIn = 0; rtcIn = 0; rtcOutPos = 0; rtcOutBit = 0; return; }
	if ((old & 2) && !(v & 2))				// the clock falls: a bit
	{
		if (v & 0x10)
		{
			rtcIn |= (u8) ((v & 1) << rtcBitIn);
			if (++rtcBitIn == 8) { rtcByte (rtcIn); rtcPos++; rtcBitIn = 0; rtcIn = 0; }
		}
		else
		{
			int bit = rtcOutPos < 8 ? (rtcOut[rtcOutPos] >> rtcOutBit) & 1 : 0;
			rtcIo = (u16) ((rtcIo & ~1u) | (u16) bit);
			if (++rtcOutBit == 8) { rtcOutBit = 0; rtcOutPos++; }
		}
	}
}

void Machine::rtcTick ()
{
	static const int DAYS[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
	if (++rtcSec < 60) return;
	rtcSec = 0; if (++rtcMin < 60) return;
	rtcMin = 0; if (++rtcHour < 24) return;
	rtcHour = 0; rtcDow = (rtcDow + 1) % 7;
	int dm = DAYS[rtcMon - 1] + (rtcMon == 2 && rtcYear % 4 == 0 ? 1 : 0);
	if (++rtcDay <= dm) return;
	rtcDay = 1; if (++rtcMon <= 12) return;
	rtcMon = 1; rtcYear++;
}

// ---- the ARM9's registers ---------------------------------------------------------------------------------------------------------
static u16 keyInput (int keys) { return (u16) (~keys & 0x3FF); }

u32 Machine::io9Read16 (u32 a)
{
	u32 off = a & 0xFFFFFF;
	if (off < 0x70)
	{
		switch (off)
		{
		case 0x04: return dispstat[0];
		case 0x06: return (u32) vcount;
		case 0x60: return gpu3d.dispcnt3d & 0xFFFF;
		case 0x62: return 0;
		case 0x64: return dispcapcnt & 0xFFFF;
		case 0x66: return dispcapcnt >> 16;
		}
		return (u32) gpuA.read8 (off) | ((u32) gpuA.read8 (off + 1) << 8);
	}
	if (off >= 0x1000 && off < 0x1070) return (u32) gpuB.read8 (off - 0x1000) | ((u32) gpuB.read8 (off - 0x1000 + 1) << 8);
	if (off >= 0x320 && off < 0x6A4) return off & 2 ? gpu3d.read32 (a & ~3u) >> 16 : gpu3d.read32 (a) & 0xFFFF;
	if (off >= 0xB0 && off < 0xE0)
	{
		int ch = (int) (off - 0xB0) / 12, r = (int) (off - 0xB0) % 12;
		Dma &d = dma[0][ch];
		switch (r) { case 0: return d.sad & 0xFFFF; case 2: return d.sad >> 16; case 4: return d.dad & 0xFFFF; case 6: return d.dad >> 16; case 8: return d.cnt & 0xFFFF; default: return d.cnt >> 16; }
	}
	if (off >= 0xE0 && off < 0xF0) { u32 v = dmaFill[(off - 0xE0) >> 2]; return off & 2 ? v >> 16 : v & 0xFFFF; }
	if (off >= 0x100 && off < 0x110) { int i = (int) (off - 0x100) >> 2; return (off & 2) ? tm[0][i].ctl : timerRead (0, i); }
	switch (off)
	{
	case 0x130: return keyInput (keys);
	case 0x132: return keycnt[0];
	case 0x180: return (u32) (ipcSync[0] & 0x6F00) | ((ipcSync[1] >> 8) & 0xF);
	case 0x184: return ipcFifoCntRead (0);
	case 0x1A0: return cart.spicnt;
	case 0x1A2: return cart.spiOut;
	case 0x1A4: return cart.romctrl & 0xFFFF;
	case 0x1A6: return cart.romctrl >> 16;
	case 0x204: return exmemcnt;
	case 0x208: return ime[0];
	case 0x210: return ie[0] & 0xFFFF;
	case 0x212: return ie[0] >> 16;
	case 0x214: return iflag[0] & 0xFFFF;
	case 0x216: return iflag[0] >> 16;
	case 0x240: return vramcnt[0] | (vramcnt[1] << 8);
	case 0x242: return vramcnt[2] | (vramcnt[3] << 8);
	case 0x244: return vramcnt[4] | (vramcnt[5] << 8);
	case 0x246: return vramcnt[6] | (wramcnt << 8);
	case 0x248: return vramcnt[7] | (vramcnt[8] << 8);
	case 0x280: return divcnt;
	case 0x290: return (u32) divNum & 0xFFFF; case 0x292: return (u32) ((u64) divNum >> 16) & 0xFFFF;
	case 0x294: return (u32) ((u64) divNum >> 32) & 0xFFFF; case 0x296: return (u32) ((u64) divNum >> 48);
	case 0x298: return (u32) divDen & 0xFFFF; case 0x29A: return (u32) ((u64) divDen >> 16) & 0xFFFF;
	case 0x29C: return (u32) ((u64) divDen >> 32) & 0xFFFF; case 0x29E: return (u32) ((u64) divDen >> 48);
	case 0x2A0: return (u32) divRes & 0xFFFF; case 0x2A2: return (u32) ((u64) divRes >> 16) & 0xFFFF;
	case 0x2A4: return (u32) ((u64) divRes >> 32) & 0xFFFF; case 0x2A6: return (u32) ((u64) divRes >> 48);
	case 0x2A8: return (u32) divRem & 0xFFFF; case 0x2AA: return (u32) ((u64) divRem >> 16) & 0xFFFF;
	case 0x2AC: return (u32) ((u64) divRem >> 32) & 0xFFFF; case 0x2AE: return (u32) ((u64) divRem >> 48);
	case 0x2B0: return sqrtcnt;
	case 0x2B4: return sqrtRes & 0xFFFF; case 0x2B6: return sqrtRes >> 16;
	case 0x2B8: return (u32) sqrtParam & 0xFFFF; case 0x2BA: return (u32) (sqrtParam >> 16) & 0xFFFF;
	case 0x2BC: return (u32) (sqrtParam >> 32) & 0xFFFF; case 0x2BE: return (u32) (sqrtParam >> 48);
	case 0x300: return postflg[0];
	case 0x304: return powcnt1;
	}
	return 0;
}

u32 Machine::io9Read32 (u32 a)
{
	u32 off = a & 0xFFFFFF;
	if (off >= 0x320 && off < 0x6A4) return gpu3d.read32 (a);
	switch (off)
	{
	case 0x100000: return ipcRecv (0);
	case 0x100010: return cartReadData (0);
	}
	return io9Read16 (a) | (io9Read16 (a + 2) << 16);
}

u32 Machine::io9Read8 (u32 a)
{
	u32 off = a & 0xFFFFFF;
	if (off >= 0x1A8 && off < 0x1B0) return cart.cmd[off - 0x1A8];
	if (off >= 0x240 && off < 0x24A) { static const int MAP[10] = { 0, 1, 2, 3, 4, 5, 6, -1, 7, 8 }; int k = MAP[off - 0x240]; return k < 0 ? wramcnt : vramcnt[k]; }
	if (off == 0x100010) return cartReadData (0) & 0xFF;
	u32 v = io9Read16 (a & ~1u);
	return (a & 1) ? v >> 8 : v & 0xFF;
}

void Machine::io9Write32 (u32 a, u32 v)
{
	u32 off = a & 0xFFFFFF;
	if (off >= 0x320 && off < 0x6A4) { gpu3d.write32 (a, v); return; }
	if (off >= 0xB0 && off < 0xE0)
	{
		int ch = (int) (off - 0xB0) / 12, r = (int) (off - 0xB0) % 12;
		if (r == 0) dma[0][ch].sad = v & 0x0FFFFFFF;
		else if (r == 4) dma[0][ch].dad = v & 0x0FFFFFFF;
		else dmaWrite (0, ch, v);
		return;
	}
	switch (off)
	{
	case 0x000: case 0x1000: { Gpu2D &g = off ? gpuB : gpuA; for (int i = 0; i < 4; i++) g.write8 (i, (u8) (v >> (i * 8))); return; }
	case 0x188: ipcSend (0, v); return;
	case 0x1A4: cartRomCtrl (0, v); return;
	case 0x208: ime[0] = v & 1; return;
	case 0x210: ie[0] = v; return;
	case 0x214: iflag[0] &= ~v; return;
	case 0x290: divNum = (s64) (((u64) divNum & 0xFFFFFFFF00000000ull) | v); divide (); return;
	case 0x294: divNum = (s64) (((u64) divNum & 0xFFFFFFFFull) | ((u64) v << 32)); divide (); return;
	case 0x298: divDen = (s64) (((u64) divDen & 0xFFFFFFFF00000000ull) | v); divide (); return;
	case 0x29C: divDen = (s64) (((u64) divDen & 0xFFFFFFFFull) | ((u64) v << 32)); divide (); return;
	case 0x2B8: sqrtParam = (sqrtParam & 0xFFFFFFFF00000000ull) | v; squareRoot (); return;
	case 0x2BC: sqrtParam = (sqrtParam & 0xFFFFFFFFull) | ((u64) v << 32); squareRoot (); return;
	case 0x064: dispcapcnt = v; return;
	case 0x060: gpu3d.dispcnt3d = (gpu3d.dispcnt3d & 0x3000 & ~v) | (v & 0x4FFF); return;
	case 0x068: if (dispFifoN < 16) { dispFifo[dispFifoN++] = (u16) v; dispFifo[dispFifoN++ & 15] = (u16) (v >> 16); } return;
	}
	io9Write16 (a, v & 0xFFFF);
	io9Write16 (a + 2, v >> 16);
}

void Machine::io9Write16 (u32 a, u32 v)
{
	u32 off = a & 0xFFFFFF;
	if (off < 0x70)
	{
		switch (off)
		{
		case 0x04: dispstat[0] = (u16) ((dispstat[0] & 7) | (v & 0xFFB8)); return;
		case 0x06: return;
		case 0x60: gpu3d.dispcnt3d = (gpu3d.dispcnt3d & 0x3000 & ~v) | (v & 0x4FFF); return;
		case 0x64: dispcapcnt = (dispcapcnt & 0xFFFF0000) | v; return;
		case 0x66: dispcapcnt = (dispcapcnt & 0xFFFF) | (v << 16); return;
		}
		gpuA.write8 (off, (u8) v); gpuA.write8 (off + 1, (u8) (v >> 8));
		return;
	}
	if (off >= 0x1000 && off < 0x1070) { gpuB.write8 (off - 0x1000, (u8) v); gpuB.write8 (off - 0x1000 + 1, (u8) (v >> 8)); return; }
	if (off >= 0x320 && off < 0x6A4) { gpu3d.write16 (a, (u16) v); return; }
	if (off >= 0xB0 && off < 0xE0)
	{
		int ch = (int) (off - 0xB0) / 12, r = (int) (off - 0xB0) % 12;
		Dma &d = dma[0][ch];
		switch (r)
		{
		case 0: d.sad = (d.sad & 0xFFFF0000) | v; break;
		case 2: d.sad = (d.sad & 0xFFFF) | ((v & 0x0FFF) << 16); break;
		case 4: d.dad = (d.dad & 0xFFFF0000) | v; break;
		case 6: d.dad = (d.dad & 0xFFFF) | ((v & 0x0FFF) << 16); break;
		case 8: d.cnt = (d.cnt & 0xFFFF0000) | v; break;
		default: dmaWrite (0, ch, (d.cnt & 0xFFFF) | (v << 16)); break;
		}
		return;
	}
	if (off >= 0xE0 && off < 0xF0) { u32 &f = dmaFill[(off - 0xE0) >> 2]; f = (off & 2) ? (f & 0xFFFF) | (v << 16) : (f & 0xFFFF0000) | v; return; }
	if (off >= 0x100 && off < 0x110) { timerWrite (0, (int) (off - 0x100) >> 2, (off & 2) != 0, (u16) v); return; }
	if (off >= 0x1A8 && off < 0x1B0) { cart.cmd[off - 0x1A8] = (u8) v; cart.cmd[off - 0x1A8 + 1] = (u8) (v >> 8); return; }
	switch (off)
	{
	case 0x132: keycnt[0] = (u16) v; return;
	case 0x180:
		ipcSync[0] = (u16) ((ipcSync[0] & 0xF) | (v & 0x4F00));
		if ((v & 0x2000) && (ipcSync[1] & 0x4000)) raise (1, 16);
		return;
	case 0x184: ipcFifoCnt (0, (u16) v); return;
	case 0x1A0: cartSpiCnt ((u16) v); return;
	case 0x1A2: cartSpiData ((u8) v); return;
	case 0x1A4: cartRomCtrl (0, (cart.romctrl & 0xFFFF0000) | v); return;
	case 0x1A6: cartRomCtrl (0, (cart.romctrl & 0xFFFF) | (v << 16)); return;
	case 0x204: exmemcnt = (u16) ((exmemcnt & 0x6000) | (v & 0x8880) | 0x6000); return;
	case 0x208: ime[0] = v & 1; return;
	case 0x210: ie[0] = (ie[0] & 0xFFFF0000) | v; return;
	case 0x212: ie[0] = (ie[0] & 0xFFFF) | (v << 16); return;
	case 0x214: iflag[0] &= ~v; return;
	case 0x216: iflag[0] &= ~(v << 16); return;
	case 0x280: divcnt = (u16) (v & 3); divide (); return;
	case 0x290: case 0x292: case 0x294: case 0x296:
	{ int s = (int) (off - 0x290) * 8; divNum = (s64) (((u64) divNum & ~(0xFFFFull << s)) | ((u64) v << s)); divide (); return; }
	case 0x298: case 0x29A: case 0x29C: case 0x29E:
	{ int s = (int) (off - 0x298) * 8; divDen = (s64) (((u64) divDen & ~(0xFFFFull << s)) | ((u64) v << s)); divide (); return; }
	case 0x2B0: sqrtcnt = (u16) (v & 1); squareRoot (); return;
	case 0x2B8: case 0x2BA: case 0x2BC: case 0x2BE:
	{ int s = (int) (off - 0x2B8) * 8; sqrtParam = (sqrtParam & ~(0xFFFFull << s)) | ((u64) v << s); squareRoot (); return; }
	case 0x240: case 0x242: case 0x244: case 0x246: case 0x248:
		io9Write8 (a, v & 0xFF); io9Write8 (a + 1, v >> 8); return;
	case 0x300: postflg[0] = (u8) ((postflg[0] & 1) | (v & 3)); return;
	case 0x304: powcnt1 = (u16) (v & 0x820F); return;
	}
}

void Machine::io9Write8 (u32 a, u32 v)
{
	u32 off = a & 0xFFFFFF;
	v &= 0xFF;
	if (off < 0x70 && (off < 0x4 || off >= 0x8) && (off < 0x60 || off >= 0x6C)) { gpuA.write8 (off, (u8) v); return; }
	if (off >= 0x1000 && off < 0x1070) { gpuB.write8 (off - 0x1000, (u8) v); return; }
	if (off >= 0x320 && off < 0x6A4) { gpu3d.write8 (a, (u8) v); return; }
	if (off >= 0x1A8 && off < 0x1B0) { cart.cmd[off - 0x1A8] = (u8) v; return; }
	if (off >= 0x240 && off < 0x24A)
	{
		static const int MAP[10] = { 0, 1, 2, 3, 4, 5, 6, -1, 7, 8 };
		int k = MAP[off - 0x240];
		if (k < 0) { wramcnt = (u8) (v & 3); pagesRange (0x03000000, 0x04000000); return; }
		static const u8 MASK[9] = { 0x9B, 0x9B, 0x9F, 0x9F, 0x87, 0x9F, 0x9F, 0x83, 0x83 };
		u8 nv = (u8) (v & MASK[k]);
		if (vramcnt[k] != nv) { vramcnt[k] = nv; vramMap (); pagesRange (0x06000000, 0x07000000); }
		return;
	}
	switch (off)
	{
	case 0x208: ime[0] = v & 1; return;
	case 0x210: case 0x211: case 0x212: case 0x213: { int s = (int) (off - 0x210) * 8; ie[0] = (ie[0] & ~(0xFFu << s)) | (v << s); return; }
	case 0x214: case 0x215: case 0x216: case 0x217: iflag[0] &= ~(v << ((off - 0x214) * 8)); return;
	case 0x1A1: cartSpiCnt ((u16) ((cart.spicnt & 0xFF) | (v << 8))); return;
	case 0x1A0: cartSpiCnt ((u16) ((cart.spicnt & 0xFF00) | v)); return;
	case 0x1A2: cartSpiData ((u8) v); return;
	case 0x300: postflg[0] = (u8) ((postflg[0] & 1) | (v & 3)); return;
	case 0x180: ipcSync[0] = (u16) ((ipcSync[0] & 0xFF00) | (ipcSync[0] & 0xF)); return;
	case 0x181:
		ipcSync[0] = (u16) ((ipcSync[0] & 0xF) | ((v << 8) & 0x4F00));
		if ((v & 0x20) && (ipcSync[1] & 0x4000)) raise (1, 16);
		return;
	}
	u32 cur = io9Read16 (a & ~1u);
	if (off >= 0x100 && off < 0x110 && !(off & 2)) cur = tm[0][(off - 0x100) >> 2].reload;
	io9Write16 (a & ~1u, (a & 1) ? (cur & 0xFF) | (v << 8) : (cur & 0xFF00) | v);
}

// ---- the ARM7's registers -----------------------------------------------------------------------------------------------------------------
u32 Machine::io7Read16 (u32 a)
{
	u32 off = a & 0xFFFFFF;
	if (off >= 0x400 && off < 0x520) return off & 2 ? spu.read32 (a & ~3u) >> 16 : spu.read32 (a) & 0xFFFF;
	if (off >= 0xB0 && off < 0xE0)
	{
		int ch = (int) (off - 0xB0) / 12, r = (int) (off - 0xB0) % 12;
		Dma &d = dma[1][ch];
		switch (r) { case 0: return d.sad & 0xFFFF; case 2: return d.sad >> 16; case 4: return d.dad & 0xFFFF; case 6: return d.dad >> 16; case 8: return d.cnt & 0xFFFF; default: return d.cnt >> 16; }
	}
	if (off >= 0x100 && off < 0x110) { int i = (int) (off - 0x100) >> 2; return (off & 2) ? tm[1][i].ctl : timerRead (1, i); }
	switch (off)
	{
	case 0x004: return dispstat[1];
	case 0x006: return (u32) vcount;
	case 0x130: return keyInput (keys);
	case 0x132: return keycnt[1];
	case 0x134: return rcnt;
	case 0x136: return (u32) (0x34 | ((keys & BTN_X) ? 0 : 1) | ((keys & BTN_Y) ? 0 : 2) | 8 | (touchDown ? 0 : 0x40) | ((keys & BTN_LID) ? 0x80 : 0));
	case 0x138: return rtcIo;
	case 0x180: return (u32) (ipcSync[1] & 0x6F00) | ((ipcSync[0] >> 8) & 0xF);
	case 0x184: return ipcFifoCntRead (1);
	case 0x1A0: return cart.spicnt;
	case 0x1A2: return cart.spiOut;
	case 0x1A4: return cart.romctrl & 0xFFFF;
	case 0x1A6: return cart.romctrl >> 16;
	case 0x1C0: return spicnt;
	case 0x1C2: return spiData;
	case 0x204: return exmemcnt;
	case 0x206: return wifiWait[0] | (wifiWait[1] << 8);
	case 0x208: return ime[1];
	case 0x210: return ie[1] & 0xFFFF;
	case 0x212: return ie[1] >> 16;
	case 0x214: return iflag[1] & 0xFFFF;
	case 0x216: return iflag[1] >> 16;
	case 0x240: return vramstat | (wramcnt << 8);
	case 0x300: return postflg[1];
	case 0x304: return powcnt2;
	case 0x308: return biosProt & 0xFFFF;
	case 0x30A: return biosProt >> 16;
	}
	return 0;
}

u32 Machine::io7Read32 (u32 a)
{
	u32 off = a & 0xFFFFFF;
	if (off >= 0x400 && off < 0x520) return spu.read32 (a);
	switch (off)
	{
	case 0x100000: return ipcRecv (1);
	case 0x100010: return cartReadData (1);
	}
	return io7Read16 (a) | (io7Read16 (a + 2) << 16);
}

u32 Machine::io7Read8 (u32 a)
{
	u32 off = a & 0xFFFFFF;
	if (off >= 0x1A8 && off < 0x1B0) return cart.cmd[off - 0x1A8];
	if (off == 0x240) return vramstat;
	if (off == 0x241) return wramcnt;
	if (off >= 0x400 && off < 0x520) return (spu.read32 (a & ~3u) >> ((a & 3) * 8)) & 0xFF;
	u32 v = io7Read16 (a & ~1u);
	return (a & 1) ? v >> 8 : v & 0xFF;
}

void Machine::io7Write32 (u32 a, u32 v)
{
	u32 off = a & 0xFFFFFF;
	if (off >= 0x400 && off < 0x520) { spu.write32 (a, v); return; }
	if (off >= 0xB0 && off < 0xE0)
	{
		int ch = (int) (off - 0xB0) / 12, r = (int) (off - 0xB0) % 12;
		if (r == 0) dma[1][ch].sad = v & 0x07FFFFFF;
		else if (r == 4) dma[1][ch].dad = v & 0x07FFFFFF;
		else dmaWrite (1, ch, v);
		return;
	}
	switch (off)
	{
	case 0x188: ipcSend (1, v); return;
	case 0x1A4: cartRomCtrl (1, v); return;
	case 0x208: ime[1] = v & 1; return;
	case 0x210: ie[1] = v; return;
	case 0x214: iflag[1] &= ~v; return;
	case 0x308: biosProt = v; return;
	}
	io7Write16 (a, v & 0xFFFF);
	io7Write16 (a + 2, v >> 16);
}

void Machine::io7Write16 (u32 a, u32 v)
{
	u32 off = a & 0xFFFFFF;
	if (off >= 0x400 && off < 0x520) { spu.write16 (a, (u16) v); return; }
	if (off >= 0xB0 && off < 0xE0)
	{
		int ch = (int) (off - 0xB0) / 12, r = (int) (off - 0xB0) % 12;
		Dma &d = dma[1][ch];
		switch (r)
		{
		case 0: d.sad = (d.sad & 0xFFFF0000) | v; break;
		case 2: d.sad = (d.sad & 0xFFFF) | ((v & 0x07FF) << 16); break;
		case 4: d.dad = (d.dad & 0xFFFF0000) | v; break;
		case 6: d.dad = (d.dad & 0xFFFF) | ((v & 0x07FF) << 16); break;
		case 8: d.cnt = (d.cnt & 0xFFFF0000) | v; break;
		default: dmaWrite (1, ch, (d.cnt & 0xFFFF) | (v << 16)); break;
		}
		return;
	}
	if (off >= 0x100 && off < 0x110) { timerWrite (1, (int) (off - 0x100) >> 2, (off & 2) != 0, (u16) v); return; }
	if (off >= 0x1A8 && off < 0x1B0) { cart.cmd[off - 0x1A8] = (u8) v; cart.cmd[off - 0x1A8 + 1] = (u8) (v >> 8); return; }
	switch (off)
	{
	case 0x004: dispstat[1] = (u16) ((dispstat[1] & 7) | (v & 0xFFB8)); return;
	case 0x132: keycnt[1] = (u16) v; return;
	case 0x134: rcnt = (u16) v; return;
	case 0x138: rtcWrite ((u16) v); return;
	case 0x180:
		ipcSync[1] = (u16) ((ipcSync[1] & 0xF) | (v & 0x4F00));
		if ((v & 0x2000) && (ipcSync[0] & 0x4000)) raise (0, 16);
		return;
	case 0x184: ipcFifoCnt (1, (u16) v); return;
	case 0x1A0: cartSpiCnt ((u16) v); return;
	case 0x1A2: cartSpiData ((u8) v); return;
	case 0x1A4: cartRomCtrl (1, (cart.romctrl & 0xFFFF0000) | v); return;
	case 0x1A6: cartRomCtrl (1, (cart.romctrl & 0xFFFF) | (v << 16)); return;
	case 0x1C0: spicnt = (u16) ((spicnt & 0x80) | (v & 0xCF03)); return;
	case 0x1C2: spiWrite ((u8) v); return;
	case 0x206: wifiWait[0] = (u8) v; wifiWait[1] = (u8) (v >> 8); return;
	case 0x208: ime[1] = v & 1; return;
	case 0x210: ie[1] = (ie[1] & 0xFFFF0000) | v; return;
	case 0x212: ie[1] = (ie[1] & 0xFFFF) | (v << 16); return;
	case 0x214: iflag[1] &= ~v; return;
	case 0x216: iflag[1] &= ~(v << 16); return;
	case 0x300: io7Write8 (a, v & 0xFF); io7Write8 (a + 1, v >> 8); return;
	case 0x304: powcnt2 = (u16) (v & 3); return;
	case 0x308: biosProt = (biosProt & 0xFFFF0000) | v; return;
	}
}

void Machine::io7Write8 (u32 a, u32 v)
{
	u32 off = a & 0xFFFFFF;
	v &= 0xFF;
	if (off >= 0x400 && off < 0x520) { spu.write8 (a, (u8) v); return; }
	if (off >= 0x1A8 && off < 0x1B0) { cart.cmd[off - 0x1A8] = (u8) v; return; }
	switch (off)
	{
	case 0x138: rtcWrite ((u16) ((rtcIo & 0xFF00) | v)); return;
	case 0x1C2: spiWrite ((u8) v); return;
	case 0x208: ime[1] = v & 1; return;
	case 0x210: case 0x211: case 0x212: case 0x213: { int s = (int) (off - 0x210) * 8; ie[1] = (ie[1] & ~(0xFFu << s)) | (v << s); return; }
	case 0x214: case 0x215: case 0x216: case 0x217: iflag[1] &= ~(v << ((off - 0x214) * 8)); return;
	case 0x1A1: cartSpiCnt ((u16) ((cart.spicnt & 0xFF) | (v << 8))); return;
	case 0x1A0: cartSpiCnt ((u16) ((cart.spicnt & 0xFF00) | v)); return;
	case 0x1A2: cartSpiData ((u8) v); return;
	case 0x300: postflg[1] = (u8) (postflg[1] | (v & 1)); return;
	case 0x301:						// HALTCNT
		if ((v & 0xC0) == 0x80 || (v & 0xC0) == 0xC0) { arm7.halted = true; arm7.jitExit = true; }
		return;
	case 0x180: return;
	case 0x181:
		ipcSync[1] = (u16) ((ipcSync[1] & 0xF) | ((v << 8) & 0x4F00));
		if ((v & 0x20) && (ipcSync[0] & 0x4000)) raise (0, 16);
		return;
	case 0x240: case 0x241: return;
	}
	u32 cur = io7Read16 (a & ~1u);
	if (off >= 0x100 && off < 0x110 && !(off & 2)) cur = tm[1][(off - 0x100) >> 2].reload;
	io7Write16 (a & ~1u, (a & 1) ? (cur & 0xFF) | (v << 8) : (cur & 0xFF00) | v);
}

} // namespace nds
