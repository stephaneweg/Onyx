//
// gba/gba_apu.cpp -- the sound: the Game Boy's 4 PSG channels (2 squares, the wave, the noise;
// registers 0x60-0x84, wave RAM in two banks) and Direct Sound A / B (two 32-byte FIFOs of
// 8-bit samples, played at a timer's overflows and refilled by DMA 1 / 2). Run lazily: apuRun
// brings it up to the current cycle (before each register access and each FIFO sample).
//
#include "gba/gba.h"

namespace gba {

enum { CPU_HZ = 16777216 };
static const u8 DUTY[4] = { 0x01, 0x81, 0x87, 0x7E };	// 12.5 %, 25 %, 50 %, 75 %

void Machine::trigger (int ch)
{
	if (ch < 2)
	{
		Square &s = sq[ch];
		u8 env = io[ch ? 0x69 : 0x63];
		s.on = (env & 0xF8) != 0;
		if (!s.len) s.len = 64;
		s.timer = (2048 - s.freq) * 16;
		s.vol = env >> 4; s.envPeriod = env & 7; s.envDir = (env & 8) ? 1 : -1; s.envTimer = s.envPeriod;
		if (ch == 0)
		{
			u8 sw = io[0x60];
			s.shadow = s.freq; s.sweepPeriod = (sw >> 4) & 7; s.sweepDir = (sw & 8) ? -1 : 1; s.sweepShift = sw & 7;
			s.sweepTimer = s.sweepPeriod ? s.sweepPeriod : 8; s.sweepOn = s.sweepPeriod || s.sweepShift;
			if (s.sweepShift && s.shadow + s.sweepDir * (s.shadow >> s.sweepShift) > 2047) s.on = false;
		}
	}
	else if (ch == 2) { wv.on = (io[0x70] & 0x80) != 0; if (!wv.len) wv.len = 256; wv.timer = (2048 - wv.freq) * 8; wv.pos = 0; }
	else
	{
		u8 env = io[0x79];
		ns.on = (env & 0xF8) != 0; if (!ns.len) ns.len = 64; ns.lfsr = 0x7FFF;
		ns.vol = env >> 4; ns.envPeriod = env & 7; ns.envDir = (env & 8) ? 1 : -1; ns.envTimer = ns.envPeriod;
	}
}

u8 Machine::apuRead (u32 off)
{
	if (off >= 0x90 && off < 0xA0) return waveRam[((io[0x70] & 0x40) ? 0 : 16) + (off - 0x90)];	// the other bank
	if (off == 0x84) return (u8) ((io[0x84] & 0x80) | (sq[0].on ? 1 : 0) | (sq[1].on ? 2 : 0) | (wv.on ? 4 : 0) | (ns.on ? 8 : 0));
	if (off == 0x83) return (u8) (io[0x83] & 0x77);
	if (off >= 0xA0) return 0;
	return io[off];
}

void Machine::apuWrite (u32 off, u8 v)
{
	if (off >= 0x90 && off < 0xA0) { waveRam[((io[0x70] & 0x40) ? 0 : 16) + (off - 0x90)] = v; return; }
	if (off == 0x84)
	{
		io[0x84] = (u8) (v & 0x80);
		if (!(v & 0x80))
		{
			for (u32 i = 0x60; i < 0x82; i++) io[i] = 0;
			sq[0].on = sq[1].on = wv.on = ns.on = false;
		}
		return;
	}
	if (off < 0x80 && !(io[0x84] & 0x80)) return;			// (the PSG is off)
	io[off] = v;
	switch (off)
	{
	case 0x62: sq[0].duty = v >> 6; sq[0].len = 64 - (v & 63); break;
	case 0x68: sq[1].duty = v >> 6; sq[1].len = 64 - (v & 63); break;
	case 0x63: case 0x69: if (!(v & 0xF8)) sq[off == 0x63 ? 0 : 1].on = false; break;
	case 0x64: sq[0].freq = (sq[0].freq & 0x700) | v; break;
	case 0x6C: sq[1].freq = (sq[1].freq & 0x700) | v; break;
	case 0x65: case 0x6D:
	{
		Square &s = sq[off == 0x65 ? 0 : 1];
		s.freq = (s.freq & 0xFF) | ((v & 7) << 8); s.lenOn = (v & 0x40) != 0;
		if (v & 0x80) trigger (off == 0x65 ? 0 : 1);
		break;
	}
	case 0x70: if (!(v & 0x80)) wv.on = false; break;
	case 0x72: wv.len = 256 - v; break;
	case 0x73: wv.vol = v; break;
	case 0x74: wv.freq = (wv.freq & 0x700) | v; break;
	case 0x75: wv.freq = (wv.freq & 0xFF) | ((v & 7) << 8); wv.lenOn = (v & 0x40) != 0; if (v & 0x80) trigger (2); break;
	case 0x78: ns.len = 64 - (v & 63); break;
	case 0x79: if (!(v & 0xF8)) ns.on = false; break;
	case 0x7C: ns.shift = v >> 4; ns.width = (v >> 3) & 1; ns.div = v & 7; break;
	case 0x7D: ns.lenOn = (v & 0x40) != 0; if (v & 0x80) trigger (3); break;
	case 0x83:						// FIFO resets
		if (v & 0x08) { fifoLen[0] = fifoRd[0] = fifoWr[0] = 0; }
		if (v & 0x80) { fifoLen[1] = fifoRd[1] = fifoWr[1] = 0; }
		io[0x83] = (u8) (v & 0x77);
		break;
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
	int left = 0, right = 0;
	if (io[0x84] & 0x80)
	{
		int out[4];
		for (int i = 0; i < 2; i++) out[i] = sq[i].on ? (((DUTY[sq[i].duty] >> (7 - sq[i].dutyPos)) & 1) ? sq[i].vol : 0) : 0;
		out[2] = 0;
		if (wv.on)
		{
			static const int SH[4] = { 4, 0, 1, 2 };
			int s = wv.sample;
			out[2] = (wv.vol & 0x80) ? s * 3 / 4 : s >> SH[(wv.vol >> 5) & 3];
		}
		out[3] = ns.on ? ((ns.lfsr & 1) ? 0 : ns.vol) : 0;
		u8 pan = io[0x81];
		int pl = 0, pr = 0;
		for (int i = 0; i < 4; i++)
		{
			int v = out[i] * 2 - 15;
			if (pan & (0x10 << i)) pl += v;
			if (pan & (1 << i)) pr += v;
		}
		pl *= ((io[0x80] >> 4) & 7) + 1; pr *= (io[0x80] & 7) + 1;
		u16 h = io16 (0x82);
		int shift = (h & 3) == 0 ? 2 : (h & 3) == 1 ? 1 : 0;
		left = pl >> shift; right = pr >> shift;
		int a = dsOut[0] * ((h & 4) ? 4 : 2), b = dsOut[1] * ((h & 8) ? 4 : 2);
		if (h & 0x100) right += a;
		if (h & 0x200) left += a;
		if (h & 0x1000) right += b;
		if (h & 0x2000) left += b;
	}
	left *= 32; right *= 32;
	left = left > 32767 ? 32767 : left < -32768 ? -32768 : left;
	right = right > 32767 ? 32767 : right < -32768 ? -32768 : right;
	int n = (atail + 1) % ABUF;
	if (n == ahead) return;						// full: drop
	abuf[atail * 2] = (short) left; abuf[atail * 2 + 1] = (short) right;
	atail = n;
}

void Machine::apuRun ()
{
	while (apuLast < now)
	{
		s64 need = (CPU_HZ - sampleAcc + rate - 1) / rate;
		u64 n = (u64) (need < 1 ? 1 : need);
		if (n > now - apuLast) n = now - apuLast;
		int c = (int) n;
		for (int i = 0; i < 2; i++)
		{
			Square &s = sq[i];
			s.timer -= c;
			int period = (2048 - s.freq) * 16;
			while (s.timer <= 0) { s.timer += period; s.dutyPos = (s.dutyPos + 1) & 7; }
		}
		wv.timer -= c;
		while (wv.timer <= 0)
		{
			wv.timer += (2048 - wv.freq) * 8;
			int len = (io[0x70] & 0x20) ? 64 : 32;
			wv.pos = (wv.pos + 1) % len;
			int bank = (io[0x70] & 0x40) ? 16 : 0;
			u8 byte = waveRam[(bank + wv.pos / 2) & 31];
			wv.sample = (wv.pos & 1) ? (byte & 0x0F) : (byte >> 4);
		}
		ns.timer -= c;
		while (ns.timer <= 0)
		{
			int div = ns.div ? ns.div * 16 : 8;
			ns.timer += (div << ns.shift) * 4;
			u32 bit = (ns.lfsr ^ (ns.lfsr >> 1)) & 1;
			ns.lfsr = (ns.lfsr >> 1) | (bit << 14);
			if (ns.width) ns.lfsr = (ns.lfsr & ~0x40u) | (bit << 6);
		}
		fsTimer += c;
		while (fsTimer >= 32768) { fsTimer -= 32768; frameSequencer (); }
		apuLast += n;
		sampleAcc += (s64) n * rate;
		if (sampleAcc >= CPU_HZ) { sampleAcc -= CPU_HZ; mixSample (); }
	}
}

int Machine::audioRead (short *lr, int maxFrames)
{
	int n = 0;
	while (ahead != atail && n < maxFrames) { lr[n * 2] = abuf[ahead * 2]; lr[n * 2 + 1] = abuf[ahead * 2 + 1]; ahead = (ahead + 1) % ABUF; n++; }
	return n;
}

void Machine::fifoPush (int ch, u32 v)
{
	if (fifoLen[ch] >= 32) return;
	fifo[ch][fifoWr[ch]] = (s8) (u8) v;
	fifoWr[ch] = (fifoWr[ch] + 1) & 31;
	fifoLen[ch]++;
}

// A timer overflowed: the FIFOs driven by it play their next sample (and ask DMA for more).
void Machine::fifoTimer (int t)
{
	u16 h = io16 (0x82);
	for (int ch = 0; ch < 2; ch++)
	{
		if ((int) ((h >> (10 + ch * 4)) & 1) != t) continue;
		if (!(h & (0x300 << (ch * 4)))) continue;			// (not played)
		apuRun ();
		if (fifoLen[ch] > 0)
		{
			dsOut[ch] = fifo[ch][fifoRd[ch]];
			fifoRd[ch] = (fifoRd[ch] + 1) & 31;
			fifoLen[ch]--;
		}
		if (fifoLen[ch] <= 16)
		{
			u32 dst = ch ? 0x040000A4 : 0x040000A0;
			for (int d = 1; d <= 2; d++)
				if (dma[d].on && ((dma[d].ctl >> 12) & 3) == 3 && dma[d].dst == dst) { dmaRun (d); break; }
		}
	}
}

} // namespace gba
