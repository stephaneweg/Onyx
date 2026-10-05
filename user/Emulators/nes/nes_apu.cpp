//
// nes/nes_apu.cpp -- the 2A03's sound: 2 pulse channels (duty, envelope, sweep, length), the
// triangle (linear counter), the noise (15-bit LFSR, 2 modes), the DMC (1-bit delta samples
// read from the cartridge, its IRQ), the frame sequencer (4 / 5 steps, the frame IRQ), NTSC
// and PAL tables; mixed by the hardware's non-linear formulas (integer tables), sampled at the
// output rate with a DC-blocking high-pass.
//
#include "nes/nes.h"

namespace nes {

#include "nes/nes_mix.inc"

static const u8 LEN[32] = { 10, 254, 20, 2, 40, 4, 80, 6, 160, 8, 60, 10, 14, 12, 26, 14,
			    12, 16, 24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30 };
static const u8 DUTY[4][8] = { { 0, 1, 0, 0, 0, 0, 0, 0 }, { 0, 1, 1, 0, 0, 0, 0, 0 },
			       { 0, 1, 1, 1, 1, 0, 0, 0 }, { 1, 0, 0, 1, 1, 1, 1, 1 } };
static const u8 TRI[32] = { 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0,
			    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
static const u16 NOISE_NTSC[16] = { 4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068 };
static const u16 NOISE_PAL[16] = { 4, 8, 14, 30, 60, 88, 118, 148, 188, 236, 354, 472, 708, 944, 1890, 3778 };
static const u16 DMC_NTSC[16] = { 428, 380, 340, 320, 286, 254, 226, 214, 190, 160, 142, 128, 106, 84, 72, 54 };
static const u16 DMC_PAL[16] = { 398, 354, 316, 298, 276, 236, 210, 198, 176, 148, 132, 118, 98, 78, 66, 50 };

void Machine::setAudioRate (int hz) { rate = hz > 8000 ? hz : 48000; }

int Machine::audioRead (short *lr, int maxFrames)
{
	int n = 0;
	while (ahead != atail && n < maxFrames) { lr[n * 2] = abuf[ahead * 2]; lr[n * 2 + 1] = abuf[ahead * 2 + 1]; ahead = (ahead + 1) % ABUF; n++; }
	return n;
}

// ---- the registers ------------------------------------------------------------------------------------
void Machine::apuWrite (u16 ad, u8 val)
{
	if (ad < 0x4008)
	{
		Pulse &q = pu[(ad >> 2) & 1];
		switch (ad & 3)
		{
		case 0: q.duty = val >> 6; q.halt = val & 0x20; q.constVol = val & 0x10; q.vol = q.envPeriod = val & 15; break;
		case 1: q.sweepOn = val & 0x80; q.sweepPeriod = (val >> 4) & 7; q.sweepNeg = val & 8; q.sweepShift = val & 7; q.sweepReload = true; break;
		case 2: q.period = (q.period & 0x700) | val; break;
		case 3: q.period = (q.period & 0xFF) | ((val & 7) << 8); if (q.on) q.len = LEN[val >> 3]; q.seq = 0; q.envStart = true; break;
		}
		return;
	}
	switch (ad)
	{
	case 0x4008: tr.halt = val & 0x80; tr.linPeriod = val & 0x7F; break;
	case 0x400A: tr.period = (tr.period & 0x700) | val; break;
	case 0x400B: tr.period = (tr.period & 0xFF) | ((val & 7) << 8); if (tr.on) tr.len = LEN[val >> 3]; tr.linReload = true; break;
	case 0x400C: no.halt = val & 0x20; no.constVol = val & 0x10; no.vol = no.envPeriod = val & 15; break;
	case 0x400E: no.mode = val & 0x80; no.period = (pal ? NOISE_PAL : NOISE_NTSC)[val & 15]; break;
	case 0x400F: if (no.on) no.len = LEN[val >> 3]; no.envStart = true; break;
	case 0x4010: dmc.irqOn = val & 0x80; dmc.loop = val & 0x40; dmc.rate = (pal ? DMC_PAL : DMC_NTSC)[val & 15]; if (!dmc.irqOn) dmc.irq = false; break;
	case 0x4011: dmc.level = val & 0x7F; break;
	case 0x4012: dmc.start = (u16) (0xC000 + val * 64); break;
	case 0x4013: dmc.len = val * 16 + 1; break;
	case 0x4015:
		pu[0].on = val & 1; pu[1].on = val & 2; tr.on = val & 4; no.on = val & 8;
		if (!pu[0].on) pu[0].len = 0;
		if (!pu[1].on) pu[1].len = 0;
		if (!tr.on) tr.len = 0;
		if (!no.on) no.len = 0;
		dmc.irq = false;
		if (!(val & 0x10)) dmc.remain = 0;
		else if (dmc.remain == 0) { dmc.addr = dmc.start; dmc.remain = dmc.len; }
		break;
	case 0x4017:
		frameMode = val >> 7;
		frameIrqOff = val & 0x40;
		if (frameIrqOff) frameIrq = false;
		frameCycle = ((cycles + (u64) apuAhead) & 1) ? -3 : -4;			// (the reset lands 3-4 cycles later)
		if (frameMode) { quarterFrame (); halfFrame (); }
		break;
	}
}

u8 Machine::apuStatus ()
{
	u8 r = (u8) ((pu[0].len ? 1 : 0) | (pu[1].len ? 2 : 0) | (tr.len ? 4 : 0) | (no.len ? 8 : 0) | (dmc.remain ? 0x10 : 0)
		     | (frameIrq ? 0x40 : 0) | (dmc.irq ? 0x80 : 0));
	frameIrq = false;
	return r;
}

// ---- the frame sequencer: envelopes + linear counter (1/4), lengths + sweeps (1/2) --------------
void Machine::quarterFrame ()
{
	for (int i = 0; i < 2; i++)
	{
		Pulse &q = pu[i];
		if (q.envStart) { q.envStart = false; q.envVol = 15; q.envDiv = q.envPeriod; }
		else if (q.envDiv) q.envDiv--;
		else { q.envDiv = q.envPeriod; if (q.envVol) q.envVol--; else if (q.halt) q.envVol = 15; }
	}
	if (no.envStart) { no.envStart = false; no.envVol = 15; no.envDiv = no.envPeriod; }
	else if (no.envDiv) no.envDiv--;
	else { no.envDiv = no.envPeriod; if (no.envVol) no.envVol--; else if (no.halt) no.envVol = 15; }
	if (tr.linReload) tr.linCounter = tr.linPeriod;
	else if (tr.linCounter) tr.linCounter--;
	if (!tr.halt) tr.linReload = false;
}

static int sweepTarget (int period, int shift, bool neg, bool first)
{
	int ch = period >> shift;
	if (neg) return period - ch - (first ? 1 : 0);		// (pulse 1 negates with one's complement)
	return period + ch;
}

void Machine::halfFrame ()
{
	for (int i = 0; i < 2; i++)
	{
		Pulse &q = pu[i];
		if (q.len && !q.halt) q.len--;
		int target = sweepTarget (q.period, q.sweepShift, q.sweepNeg, i == 0);
		if (q.sweepDiv == 0 && q.sweepOn && q.sweepShift && q.period >= 8 && target <= 0x7FF) q.period = target;
		if (q.sweepDiv == 0 || q.sweepReload) { q.sweepDiv = q.sweepPeriod; q.sweepReload = false; }
		else q.sweepDiv--;
	}
	if (tr.len && !tr.halt) tr.len--;
	if (no.len && !no.halt) no.len--;
}

// ---- the channels, cycle by cycle ----------------------------------------------------------------------
int Machine::mixSample ()
{
	int p1 = 0, p2 = 0;
	for (int i = 0; i < 2; i++)
	{
		const Pulse &q = pu[i];
		int target = sweepTarget (q.period, q.sweepShift, q.sweepNeg, i == 0);
		if (!q.len || q.period < 8 || (!q.sweepNeg && target > 0x7FF) || !DUTY[q.duty][q.seq]) continue;
		(i ? p2 : p1) = q.constVol ? q.vol : q.envVol;
	}
	int tv = TRI[tr.seq];
	int nv = (no.len && !(no.lfsr & 1)) ? (no.constVol ? no.vol : no.envVol) : 0;
	return MIX_PULSE[p1 + p2] + MIX_TND[3 * tv + 2 * nv + dmc.level];
}

void Machine::apuRun (int n)
{
	static const int SEQ_NTSC[5] = { 7457, 14913, 22371, 29829, 37281 };
	static const int SEQ_PAL[5] = { 8313, 16627, 24939, 33253, 41565 };
	const int *seq = pal ? SEQ_PAL : SEQ_NTSC;
	for (; n > 0; n--)
	{
		// the frame sequencer
		frameCycle++;
		if (frameCycle == seq[0] || frameCycle == seq[2]) quarterFrame ();
		else if (frameCycle == seq[1]) { quarterFrame (); halfFrame (); }
		else if (!frameMode && frameCycle == seq[3]) { quarterFrame (); halfFrame (); }
		if (!frameMode && frameCycle >= seq[3] - 1)		// the IRQ flag: set 3 cycles in a row
		{
			if (!frameIrqOff) frameIrq = true;
			if (frameCycle == seq[3] + 1) frameCycle = 0;
		}
		else if (frameMode && frameCycle == seq[4]) { quarterFrame (); halfFrame (); }
		else if (frameMode && frameCycle == seq[4] + 1) frameCycle = 0;
		// the pulses and the noise count in APU cycles (every other CPU cycle), the triangle
		// and the DMC in CPU cycles
		if (frameCycle & 1)
		{
			for (int i = 0; i < 2; i++)
			{
				Pulse &q = pu[i];
				if (q.timer) q.timer--;
				else { q.timer = q.period; q.seq = (q.seq + 1) & 7; }
			}
		}
		if (no.timer) no.timer--;
		else
		{
			no.timer = no.period ? no.period - 1 : 0;
			int fb = (no.lfsr & 1) ^ ((no.lfsr >> (no.mode ? 6 : 1)) & 1);
			no.lfsr = (u16) ((no.lfsr >> 1) | (fb << 14));
		}
		if (tr.timer) tr.timer--;
		else
		{
			tr.timer = tr.period;
			if (tr.len && tr.linCounter && tr.period >= 2) tr.seq = (tr.seq + 1) & 31;
		}
		if (dmc.timer) dmc.timer--;
		else
		{
			dmc.timer = dmc.rate - 1;
			if (!dmc.silence)
			{
				if (dmc.shift & 1) { if (dmc.level <= 125) dmc.level += 2; }
				else if (dmc.level >= 2) dmc.level -= 2;
				dmc.shift >>= 1;
			}
			if (--dmc.bits <= 0)
			{
				dmc.bits = 8;
				if (dmc.bufFull) { dmc.shift = dmc.buf; dmc.bufFull = false; dmc.silence = false; }
				else dmc.silence = true;
			}
		}
		if (!dmc.bufFull && dmc.remain > 0)			// the sample reader (it steals ~4 CPU cycles)
		{
			dmc.buf = read (dmc.addr);
			dmc.bufFull = true;
			dmc.addr = dmc.addr == 0xFFFF ? 0x8000 : (u16) (dmc.addr + 1);
			if (--dmc.remain == 0)
			{
				if (dmc.loop) { dmc.addr = dmc.start; dmc.remain = dmc.len; }
				else if (dmc.irqOn) dmc.irq = true;
			}
		}
		// the output rate
		sampleAcc += (u64) rate;
		if (sampleAcc >= (u64) cpuHz)
		{
			sampleAcc -= (u64) cpuHz;
			int s = mixSample ();					// 0..40000
			hpAcc += ((long long) (s - lastOut) << 16) - (hpAcc >> 9);	// DC blocker (~14 Hz)
			lastOut = s;
			long long o = hpAcc >> 16;
			if (o > 32767) o = 32767; else if (o < -32768) o = -32768;
			int nx = (atail + 1) % ABUF;
			if (nx != ahead) { abuf[atail * 2] = abuf[atail * 2 + 1] = (short) o; atail = nx; }
		}
	}
}

} // namespace nes
