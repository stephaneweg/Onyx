//
// snes/snes_apu.cpp -- the sound unit: the SPC700 (every opcode, its cycle counts, the 64-byte
// IPL boot ROM that loads the game's sound driver through the four ports), its three timers,
// and the S-DSP a sample at a time (32 kHz): 8 voices decoding BRR, gaussian interpolation,
// ADSR / GAIN envelopes on the global rate counter, noise, pitch modulation, the echo (its
// ring in the sound RAM, the 8-tap FIR, feedback), the main and echo volumes; the output is
// resampled to the host's rate. The SPC700 runs behind the CPU and catches up when the CPU
// touches the ports ($2140-$217F) and every 16 lines (apuSync).
//
#include "snes/snes.h"

namespace snes {

#include "snes/snes_gauss.inc"

static const u8 IPL[64] = {
	0xCD, 0xEF, 0xBD, 0xE8, 0x00, 0xC6, 0x1D, 0xD0, 0xFC, 0x8F, 0xAA, 0xF4, 0x8F, 0xBB, 0xF5, 0x78,
	0xCC, 0xF4, 0xD0, 0xFB, 0x2F, 0x19, 0xEB, 0xF4, 0xD0, 0xFC, 0x7E, 0xF4, 0xD0, 0x0B, 0xE4, 0xF5,
	0xCB, 0xF4, 0xD7, 0x00, 0xFC, 0xD0, 0xF3, 0xAB, 0x01, 0x10, 0xEF, 0x7E, 0xF4, 0x10, 0xEB, 0xBA,
	0xF6, 0xDA, 0x00, 0xBA, 0xF4, 0xC4, 0xF4, 0xDD, 0x5D, 0xD0, 0xDB, 0x1F, 0x00, 0x00, 0xC0, 0xFF };

static const u8 CYC[256] = {
	2,8,4,5,3,4,3,6,2,6,5,4,5,4,6,8,  2,8,4,5,4,5,5,6,5,5,6,5,2,2,4,6,
	2,8,4,5,3,4,3,6,2,6,5,4,5,4,5,4,  2,8,4,5,4,5,5,6,5,5,6,5,2,2,3,8,
	2,8,4,5,3,4,3,6,2,6,4,4,5,4,6,6,  2,8,4,5,4,5,5,6,5,5,4,5,2,2,4,3,
	2,8,4,5,3,4,3,6,2,6,4,4,5,4,5,5,  2,8,4,5,4,5,5,6,5,5,5,5,2,2,3,6,
	2,8,4,5,3,4,3,6,2,6,5,4,5,2,4,5,  2,8,4,5,4,5,5,6,5,5,5,5,2,2,12,5,
	3,8,4,5,3,4,3,6,2,6,4,4,5,2,4,4,  2,8,4,5,4,5,5,6,5,5,5,5,2,2,3,4,
	3,8,4,5,4,5,4,7,2,5,6,4,5,2,4,9,  2,8,4,5,5,6,6,7,4,5,5,5,2,2,6,3,
	2,8,4,5,3,4,3,6,2,4,5,3,4,3,4,3,  2,8,4,5,4,5,5,6,3,4,5,4,2,2,4,3 };

enum { SC = 0x01, SZ = 0x02, SI = 0x04, SH = 0x08, SB = 0x10, SP = 0x20, SV = 0x40, SN = 0x80 };

static inline int clamp16 (int v) { return v < -32768 ? -32768 : v > 32767 ? 32767 : v; }

void Machine::apuReset ()
{
	for (int i = 0; i < 0x10000; i++) aram[i] = 0;
	spcA = spcX = spcY = 0; spcS = 0xEF; spcP = 0; spcPC = 0xFFC0;
	for (int i = 0; i < 4; i++) cpuToApu[i] = apuToCpu[i] = 0;
	spcCtrl = 0x80; dspAddr = 0; spcF8 = spcF9 = 0;
	for (int i = 0; i < 3; i++) { spcTimerTarget[i] = 0; spcTimerOut[i] = 0; spcTimerDiv[i] = 0; spcTimerCnt[i] = 0; }
	spcSleep = false;
	spcCycles = now * 1024000ull / (pal ? 21281370ull : 21477272ull);
	dspClock = 32;
	for (int i = 0; i < 128; i++) dsp[i] = 0;
	dsp[0x6C] = 0xE0;
	for (int v = 0; v < 8; v++)
	{
		Voice &vo = voice[v];
		vo.bufPos = 0; for (int i = 0; i < 24; i++) vo.buf[i] = 0;
		vo.interpPos = 0; vo.brrAddr = 0; vo.brrOffset = 1; vo.konDelay = 0; vo.envMode = 3; vo.env = vo.hiddenEnv = 0; vo.out = 0;
	}
	konPending = 0; endx = 0; dspCounter = 0; noise = 0x4000; echoOffset = 0; echoLength = 0; echoHistPos = 0; everyOther = false;
	for (int i = 0; i < 8; i++) echoHist[i][0] = echoHist[i][1] = 0;
	resAcc = 0; lastL = lastR = 0; ahead = atail = 0;
}

void Machine::setAudioRate (int hz) { rate = hz > 0 ? hz : 48000; }

int Machine::audioRead (short *lr, int maxFrames)
{
	int n = 0;
	while (n < maxFrames && atail != ahead)
	{
		lr[n * 2] = abuf[atail * 2]; lr[n * 2 + 1] = abuf[atail * 2 + 1];
		atail = (atail + 1) & (ABUF - 1);
		n++;
	}
	return n;
}

// 32 kHz -> the host rate, linearly
void Machine::dspOut (int l, int r)
{
	resAcc += (u32) rate;
	while (resAcc >= 32000)
	{
		resAcc -= 32000;
		int f = (int) resAcc, k = rate;
		int ol = l + (lastL - l) * f / k, orr = r + (lastR - r) * f / k;
		int nh = (ahead + 1) & (ABUF - 1);
		if (nh == atail) atail = (atail + 1) & (ABUF - 1);	// full: drop the oldest
		abuf[ahead * 2] = (short) ol; abuf[ahead * 2 + 1] = (short) orr;
		ahead = nh;
	}
	lastL = (s16) l; lastR = (s16) r;
}

void Machine::apuSync ()
{
	u64 target = now * 1024000ull / (pal ? 21281370ull : 21477272ull);
	while (spcCycles < target)
	{
		int c;
		if (spcSleep) { u64 d = target - spcCycles; c = d > 64 ? 64 : (int) d; }
		else c = spcStep ();
		spcCycles += (u64) c;
		spcTimers (c);
		dspClock -= c;
		while (dspClock <= 0) { dspClock += 32; dspSample (); }
	}
}

void Machine::spcTimers (int c)
{
	for (int i = 0; i < 3; i++)
	{
		if (!(spcCtrl & (1 << i))) continue;
		int period = i == 2 ? 16 : 128;
		spcTimerDiv[i] = (u16) (spcTimerDiv[i] + c);
		while (spcTimerDiv[i] >= period)
		{
			spcTimerDiv[i] = (u16) (spcTimerDiv[i] - period);
			spcTimerCnt[i]++;
			if (spcTimerCnt[i] == spcTimerTarget[i]) { spcTimerCnt[i] = 0; spcTimerOut[i] = (u8) ((spcTimerOut[i] + 1) & 15); }
		}
	}
}

u8 Machine::spcRead (u16 a)
{
	if (a >= 0xFFC0) return IPL[a - 0xFFC0];
	switch (a)
	{
	case 0xF2: return dspAddr;
	case 0xF3: { u8 r = dspAddr & 0x7F; return r == 0x7C ? endx : dsp[r]; }
	case 0xF4: case 0xF5: case 0xF6: case 0xF7: return cpuToApu[a - 0xF4];
	case 0xF8: return spcF8;
	case 0xF9: return spcF9;
	case 0xFD: case 0xFE: case 0xFF: { u8 v = spcTimerOut[a - 0xFD]; spcTimerOut[a - 0xFD] = 0; return v; }
	}
	return 0;
}

void Machine::spcWrite (u16 a, u8 v)
{
	aram[a] = v;
	switch (a)
	{
	case 0xF1:
		for (int i = 0; i < 3; i++)
			if (!(spcCtrl & (1 << i)) && (v & (1 << i))) { spcTimerCnt[i] = 0; spcTimerOut[i] = 0; }
		if (v & 0x10) cpuToApu[0] = cpuToApu[1] = 0;
		if (v & 0x20) cpuToApu[2] = cpuToApu[3] = 0;
		spcCtrl = v;
		break;
	case 0xF2: dspAddr = v; break;
	case 0xF3: if (dspAddr < 0x80) dspWrite (dspAddr, v); break;
	case 0xF4: case 0xF5: case 0xF6: case 0xF7: apuToCpu[a - 0xF4] = v; break;
	case 0xF8: spcF8 = v; break;
	case 0xF9: spcF9 = v; break;
	case 0xFA: case 0xFB: case 0xFC: spcTimerTarget[a - 0xFA] = v; break;
	}
}

// ---- the SPC700 ------------------------------------------------------------------------------
int Machine::spcStep ()
{
	u8 op = spcRd (spcPC++);
	int cyc = CYC[op];
	u16 dpb = (spcP & SP) ? 0x100 : 0;
	auto imm = [&] () -> u8 { return spcRd (spcPC++); };
	auto imm16 = [&] () -> u16 { u16 l = imm (); return (u16) (l | imm () << 8); };
	auto dpa = [&] (u8 o) -> u16 { return (u16) (dpb | o); };
	auto setNZ = [&] (u8 v) { spcP = (u8) ((spcP & ~(SN | SZ)) | (v & 0x80) | (v ? 0 : SZ)); };
	auto setNZ16 = [&] (u16 v) { spcP = (u8) ((spcP & ~(SN | SZ)) | ((v >> 8) & 0x80) | (v ? 0 : SZ)); };
	auto rd16dp = [&] (u8 o) -> u16 { return (u16) (spcRd (dpa (o)) | spcRd (dpa ((u8) (o + 1))) << 8); };
	auto push = [&] (u8 v) { spcWr ((u16) (0x100 | spcS), v); spcS--; };
	auto pop = [&] () -> u8 { spcS++; return spcRd ((u16) (0x100 | spcS)); };
	auto branch = [&] (bool t) { s8 r = (s8) imm (); if (t) { spcPC = (u16) (spcPC + r); cyc += 2; } };
	auto adc = [&] (u8 a, u8 b) -> u8 {
		int r = a + b + (spcP & SC);
		spcP = (u8) (spcP & ~(SC | SV | SH));
		if (r > 0xFF) spcP |= SC;
		if ((a ^ b ^ r) & 0x10) spcP |= SH;
		if (~(a ^ b) & (a ^ r) & 0x80) spcP |= SV;
		setNZ ((u8) r); return (u8) r;
	};
	auto cmp = [&] (u8 a, u8 b) { int r = a - b; spcP = (u8) ((spcP & ~SC) | (r >= 0 ? SC : 0)); setNZ ((u8) r); };
	auto alu = [&] (int k, u8 a, u8 b) -> u8 {
		switch (k)
		{
		case 0: a |= b; setNZ (a); return a;
		case 1: a &= b; setNZ (a); return a;
		case 2: a ^= b; setNZ (a); return a;
		case 3: cmp (a, b); return a;
		case 4: return adc (a, b);
		default: return adc (a, (u8) ~b);
		}
	};
	auto shift = [&] (int k, u8 v) -> u8 {		// 0 ASL 1 ROL 2 LSR 3 ROR 4 DEC 5 INC
		u8 c = spcP & SC;
		switch (k)
		{
		case 0: spcP = (u8) ((spcP & ~SC) | (v >> 7)); v = (u8) (v << 1); break;
		case 1: spcP = (u8) ((spcP & ~SC) | (v >> 7)); v = (u8) (v << 1 | c); break;
		case 2: spcP = (u8) ((spcP & ~SC) | (v & 1)); v = (u8) (v >> 1); break;
		case 3: spcP = (u8) ((spcP & ~SC) | (v & 1)); v = (u8) (v >> 1 | c << 7); break;
		case 4: v--; break;
		case 5: v++; break;
		}
		setNZ (v); return v;
	};

	u8 lo = op & 0x0F, hi = op >> 4;
	// the ALU block: OR AND EOR CMP ADC SBC (rows 0-B, columns 4-9)
	if (hi < 0xC && lo >= 4 && lo <= 9)
	{
		int k = hi >> 1; bool odd = hi & 1;
		if (lo == 8 && !odd) { spcA = alu (k, spcA, imm ()); return cyc; }
		if (lo == 8) { u8 v = imm (); u16 d = dpa (imm ()); u8 r = alu (k, spcRd (d), v); if (k != 3) spcWr (d, r); return cyc; }
		if (lo == 9 && !odd) { u8 v = spcRd (dpa (imm ())); u16 d = dpa (imm ()); u8 r = alu (k, spcRd (d), v); if (k != 3) spcWr (d, r); return cyc; }
		if (lo == 9) { u8 v = spcRd (dpa (spcY)); u16 d = dpa (spcX); u8 r = alu (k, spcRd (d), v); if (k != 3) spcWr (d, r); return cyc; }
		u16 a = 0;
		switch (lo | (odd ? 0x10 : 0))
		{
		case 0x04: a = dpa (imm ()); break;
		case 0x14: a = dpa ((u8) (imm () + spcX)); break;
		case 0x05: a = imm16 (); break;
		case 0x15: a = (u16) (imm16 () + spcX); break;
		case 0x06: a = dpa (spcX); break;
		case 0x16: a = (u16) (imm16 () + spcY); break;
		case 0x07: a = rd16dp ((u8) (imm () + spcX)); break;
		case 0x17: a = (u16) (rd16dp (imm ()) + spcY); break;
		}
		spcA = alu (k, spcA, spcRd (a));
		return cyc;
	}
	// SET1 / CLR1 / BBS / BBC / TCALL
	if (lo == 2) { u16 d = dpa (imm ()); u8 v = spcRd (d); u8 b = (u8) (1 << (hi >> 1)); spcWr (d, (hi & 1) ? (u8) (v & ~b) : (u8) (v | b)); return cyc; }
	if (lo == 3) { u8 v = spcRd (dpa (imm ())); bool s = v & (1 << (hi >> 1)); branch ((hi & 1) ? !s : s); return cyc; }
	if (lo == 1)
	{
		push ((u8) (spcPC >> 8)); push ((u8) spcPC);
		u16 va = (u16) (0xFFDE - hi * 2);
		spcPC = (u16) (spcRd (va) | spcRd ((u16) (va + 1)) << 8);
		return cyc;
	}

	switch (op)
	{
	case 0x00: break;
	case 0x10: branch (!(spcP & SN)); break;
	case 0x30: branch (spcP & SN); break;
	case 0x50: branch (!(spcP & SV)); break;
	case 0x70: branch (spcP & SV); break;
	case 0x90: branch (!(spcP & SC)); break;
	case 0xB0: branch (spcP & SC); break;
	case 0xD0: branch (!(spcP & SZ)); break;
	case 0xF0: branch (spcP & SZ); break;
	case 0x2F: branch (true); break;
	case 0x20: spcP &= ~SP; break;
	case 0x40: spcP |= SP; break;
	case 0x60: spcP &= ~SC; break;
	case 0x80: spcP |= SC; break;
	case 0xA0: spcP |= SI; break;
	case 0xC0: spcP &= ~SI; break;
	case 0xE0: spcP &= ~(SV | SH); break;
	case 0xED: spcP ^= SC; break;

	// MOV
	case 0xC4: spcWr (dpa (imm ()), spcA); break;
	case 0xD4: spcWr (dpa ((u8) (imm () + spcX)), spcA); break;
	case 0xC5: spcWr (imm16 (), spcA); break;
	case 0xD5: spcWr ((u16) (imm16 () + spcX), spcA); break;
	case 0xC6: spcWr (dpa (spcX), spcA); break;
	case 0xD6: spcWr ((u16) (imm16 () + spcY), spcA); break;
	case 0xC7: spcWr (rd16dp ((u8) (imm () + spcX)), spcA); break;
	case 0xD7: spcWr ((u16) (rd16dp (imm ()) + spcY), spcA); break;
	case 0xE4: spcA = spcRd (dpa (imm ())); setNZ (spcA); break;
	case 0xF4: spcA = spcRd (dpa ((u8) (imm () + spcX))); setNZ (spcA); break;
	case 0xE5: spcA = spcRd (imm16 ()); setNZ (spcA); break;
	case 0xF5: spcA = spcRd ((u16) (imm16 () + spcX)); setNZ (spcA); break;
	case 0xE6: spcA = spcRd (dpa (spcX)); setNZ (spcA); break;
	case 0xF6: spcA = spcRd ((u16) (imm16 () + spcY)); setNZ (spcA); break;
	case 0xE7: spcA = spcRd (rd16dp ((u8) (imm () + spcX))); setNZ (spcA); break;
	case 0xF7: spcA = spcRd ((u16) (rd16dp (imm ()) + spcY)); setNZ (spcA); break;
	case 0xE8: spcA = imm (); setNZ (spcA); break;
	case 0xCD: spcX = imm (); setNZ (spcX); break;
	case 0x8D: spcY = imm (); setNZ (spcY); break;
	case 0xF8: spcX = spcRd (dpa (imm ())); setNZ (spcX); break;
	case 0xF9: spcX = spcRd (dpa ((u8) (imm () + spcY))); setNZ (spcX); break;
	case 0xE9: spcX = spcRd (imm16 ()); setNZ (spcX); break;
	case 0xEB: spcY = spcRd (dpa (imm ())); setNZ (spcY); break;
	case 0xFB: spcY = spcRd (dpa ((u8) (imm () + spcX))); setNZ (spcY); break;
	case 0xEC: spcY = spcRd (imm16 ()); setNZ (spcY); break;
	case 0xD8: spcWr (dpa (imm ()), spcX); break;
	case 0xD9: spcWr (dpa ((u8) (imm () + spcY)), spcX); break;
	case 0xC9: spcWr (imm16 (), spcX); break;
	case 0xCB: spcWr (dpa (imm ()), spcY); break;
	case 0xDB: spcWr (dpa ((u8) (imm () + spcX)), spcY); break;
	case 0xCC: spcWr (imm16 (), spcY); break;
	case 0x5D: spcX = spcA; setNZ (spcX); break;
	case 0x7D: spcA = spcX; setNZ (spcA); break;
	case 0xFD: spcY = spcA; setNZ (spcY); break;
	case 0xDD: spcA = spcY; setNZ (spcA); break;
	case 0x9D: spcX = spcS; setNZ (spcX); break;
	case 0xBD: spcS = spcX; break;
	case 0xAF: spcWr (dpa (spcX), spcA); spcX++; break;
	case 0xBF: spcA = spcRd (dpa (spcX)); spcX++; setNZ (spcA); break;
	case 0xFA: { u8 v = spcRd (dpa (imm ())); spcWr (dpa (imm ()), v); break; }
	case 0x8F: { u8 v = imm (); spcWr (dpa (imm ()), v); break; }

	// CMP X / Y
	case 0xC8: cmp (spcX, imm ()); break;
	case 0x3E: cmp (spcX, spcRd (dpa (imm ()))); break;
	case 0x1E: cmp (spcX, spcRd (imm16 ())); break;
	case 0xAD: cmp (spcY, imm ()); break;
	case 0x7E: cmp (spcY, spcRd (dpa (imm ()))); break;
	case 0x5E: cmp (spcY, spcRd (imm16 ())); break;

	// shifts, INC / DEC
	case 0x0B: case 0x2B: case 0x4B: case 0x6B: case 0x8B: case 0xAB:
	{
		static const int K[6] = { 0, 1, 2, 3, 4, 5 };
		u16 d = dpa (imm ()); spcWr (d, shift (K[hi >> 1], spcRd (d))); break;
	}
	case 0x1B: case 0x3B: case 0x5B: case 0x7B: case 0x9B: case 0xBB:
	{ u16 d = dpa ((u8) (imm () + spcX)); spcWr (d, shift (hi >> 1, spcRd (d))); break; }
	case 0x0C: case 0x2C: case 0x4C: case 0x6C: case 0x8C: case 0xAC:
	{ u16 d = imm16 (); spcWr (d, shift (hi >> 1, spcRd (d))); break; }
	case 0x1C: spcA = shift (0, spcA); break;
	case 0x3C: spcA = shift (1, spcA); break;
	case 0x5C: spcA = shift (2, spcA); break;
	case 0x7C: spcA = shift (3, spcA); break;
	case 0x9C: spcA = shift (4, spcA); break;
	case 0xBC: spcA = shift (5, spcA); break;
	case 0x1D: spcX = shift (4, spcX); break;
	case 0x3D: spcX = shift (5, spcX); break;
	case 0xDC: spcY = shift (4, spcY); break;
	case 0xFC: spcY = shift (5, spcY); break;

	// 16-bit
	case 0x1A: case 0x3A:
	{
		u8 o = imm ();
		u16 v = (u16) (rd16dp (o) + (op == 0x3A ? 1 : -1));
		spcWr (dpa (o), (u8) v); spcWr (dpa ((u8) (o + 1)), (u8) (v >> 8));
		setNZ16 (v); break;
	}
	case 0x5A: { u16 w = rd16dp (imm ()); u16 ya = (u16) (spcY << 8 | spcA); int r = ya - w;
		spcP = (u8) ((spcP & ~SC) | (r >= 0 ? SC : 0)); setNZ16 ((u16) r); break; }
	case 0x7A: case 0x9A:
	{
		u16 w = rd16dp (imm ()); u16 ya = (u16) (spcY << 8 | spcA);
		if (op == 0x9A) w = (u16) ~w;
		int r = ya + w + (op == 0x9A ? 1 : 0);
		spcP = (u8) (spcP & ~(SC | SV | SH));
		if (r > 0xFFFF) spcP |= SC;
		if (~(ya ^ w) & (ya ^ r) & 0x8000) spcP |= SV;
		if ((ya ^ w ^ r) & 0x1000) spcP |= SH;
		spcA = (u8) r; spcY = (u8) (r >> 8); setNZ16 ((u16) r);
		break;
	}
	case 0xBA: { u16 w = rd16dp (imm ()); spcA = (u8) w; spcY = (u8) (w >> 8); setNZ16 (w); break; }
	case 0xDA: { u8 o = imm (); spcWr (dpa (o), spcA); spcWr (dpa ((u8) (o + 1)), spcY); break; }
	case 0xCF: { u16 r = (u16) (spcY * spcA); spcA = (u8) r; spcY = (u8) (r >> 8); setNZ (spcY); break; }
	case 0x9E:
	{
		u32 ya = (u32) (spcY << 8 | spcA);
		spcP = (u8) (spcP & ~(SV | SH));
		if ((spcY & 15) >= (spcX & 15)) spcP |= SH;
		if (spcY >= spcX) spcP |= SV;
		if (spcY < (spcX << 1)) { if (spcX) { spcA = (u8) (ya / spcX); spcY = (u8) (ya % spcX); } else { spcA = 0xFF; spcY = (u8) ya; } }
		else { spcA = (u8) (255 - (ya - (spcX << 9)) / (256 - spcX)); spcY = (u8) (spcX + (ya - (spcX << 9)) % (256 - spcX)); }
		setNZ (spcA);
		break;
	}
	case 0xDF:
		if ((spcP & SC) || spcA > 0x99) { spcA += 0x60; spcP |= SC; }
		if ((spcP & SH) || (spcA & 15) > 9) spcA += 6;
		setNZ (spcA); break;
	case 0xBE:
		if (!(spcP & SC) || spcA > 0x99) { spcA -= 0x60; spcP &= ~SC; }
		if (!(spcP & SH) || (spcA & 15) > 9) spcA -= 6;
		setNZ (spcA); break;
	case 0x9F: spcA = (u8) (spcA >> 4 | spcA << 4); setNZ (spcA); break;

	// bits
	case 0x0A: case 0x2A: case 0x4A: case 0x6A: case 0x8A: case 0xAA: case 0xCA: case 0xEA:
	{
		u16 w = imm16 (); u16 a = w & 0x1FFF; int b = w >> 13;
		bool m = (spcRd (a) >> b) & 1, c = spcP & SC;
		switch (op)
		{
		case 0x0A: c = c || m; break;
		case 0x2A: c = c || !m; break;
		case 0x4A: c = c && m; break;
		case 0x6A: c = c && !m; break;
		case 0x8A: c = c != m; break;
		case 0xAA: c = m; break;
		case 0xCA: { u8 v = spcRd (a); v = (u8) ((v & ~(1 << b)) | (c ? 1 << b : 0)); spcWr (a, v); break; }
		case 0xEA: { u8 v = spcRd (a); spcWr (a, (u8) (v ^ (1 << b))); break; }
		}
		if (op != 0xCA && op != 0xEA) spcP = (u8) ((spcP & ~SC) | (c ? SC : 0));
		break;
	}
	case 0x0E: case 0x4E:
	{
		u16 a = imm16 (); u8 v = spcRd (a);
		setNZ ((u8) (spcA - v));
		spcWr (a, op == 0x0E ? (u8) (v | spcA) : (u8) (v & ~spcA));
		break;
	}

	// jumps, calls, stack
	case 0x2E: { u8 v = spcRd (dpa (imm ())); branch (spcA != v); break; }
	case 0xDE: { u8 v = spcRd (dpa ((u8) (imm () + spcX))); branch (spcA != v); break; }
	case 0x6E: { u16 d = dpa (imm ()); u8 v = (u8) (spcRd (d) - 1); spcWr (d, v); branch (v != 0); break; }
	case 0xFE: spcY--; branch (spcY != 0); break;
	case 0x5F: spcPC = imm16 (); break;
	case 0x1F: { u16 a = (u16) (imm16 () + spcX); spcPC = (u16) (spcRd (a) | spcRd ((u16) (a + 1)) << 8); break; }
	case 0x3F: { u16 a = imm16 (); push ((u8) (spcPC >> 8)); push ((u8) spcPC); spcPC = a; break; }
	case 0x4F: { u8 u = imm (); push ((u8) (spcPC >> 8)); push ((u8) spcPC); spcPC = (u16) (0xFF00 | u); break; }
	case 0x6F: { u16 l = pop (); spcPC = (u16) (l | pop () << 8); break; }
	case 0x7F: { spcP = pop (); u16 l = pop (); spcPC = (u16) (l | pop () << 8); break; }
	case 0x0F:
		push ((u8) (spcPC >> 8)); push ((u8) spcPC); push (spcP);
		spcP = (u8) ((spcP | SB) & ~SI);
		spcPC = (u16) (spcRd (0xFFDE) | spcRd (0xFFDF) << 8);
		break;
	case 0x0D: push (spcP); break;
	case 0x2D: push (spcA); break;
	case 0x4D: push (spcX); break;
	case 0x6D: push (spcY); break;
	case 0x8E: spcP = pop (); break;
	case 0xAE: spcA = pop (); break;
	case 0xCE: spcX = pop (); break;
	case 0xEE: spcY = pop (); break;
	case 0xEF: case 0xFF: spcSleep = true; break;
	}
	return cyc;
}

// ---- the S-DSP ---------------------------------------------------------------------------------
static const u16 RATES[32] = { 0, 2048, 1536, 1280, 1024, 768, 640, 512, 384, 320, 256, 192, 160, 128, 96, 80,
	64, 48, 40, 32, 24, 20, 16, 12, 10, 8, 6, 5, 4, 3, 2, 1 };
static const u16 OFFSETS[32] = { 1, 0, 1040, 536, 0, 1040, 536, 0, 1040, 536, 0, 1040, 536, 0, 1040, 536,
	0, 1040, 536, 0, 1040, 536, 0, 1040, 536, 0, 1040, 536, 0, 1040, 0, 0 };
enum { ENV_RELEASE, ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN };

bool Machine::counterTick (int r) const
{
	if (!r) return false;
	return ((u32) dspCounter + OFFSETS[r]) % RATES[r] == 0;
}

void Machine::dspWrite (u8 a, u8 v)
{
	dsp[a] = v;
	if (a == 0x4C) konPending = v;
	else if (a == 0x7C) endx = 0;
}

void Machine::decodeBrr (Voice &vo)
{
	u8 header = aram[vo.brrAddr];
	int shift = header >> 4, filter = (header >> 2) & 3;
	for (int k = 0; k < 2; k++)
	{
		u8 byte = aram[(u16) (vo.brrAddr + vo.brrOffset + k)];
		for (int nib = 0; nib < 2; nib++)
		{
			int s = nib ? (byte & 0x0F) : (byte >> 4);
			s = (s ^ 8) - 8;				// sign
			s = (s << shift) >> 1;
			if (shift >= 0xD) s = s < 0 ? -2048 : 0;
			int pos = vo.bufPos;
			int p1 = vo.buf[(pos + 11) % 12], p2 = vo.buf[(pos + 10) % 12] >> 1;
			if (filter >= 2)
			{
				s += p1; s -= p2;
				if (filter == 2) { s += p2 >> 4; s += (p1 * -3) >> 6; }
				else { s += (p1 * -13) >> 7; s += (p2 * 3) >> 4; }
			}
			else if (filter) { s += p1 >> 1; s += (-p1) >> 5; }
			s = clamp16 (s);
			s = (s16) (s * 2);
			vo.buf[pos] = vo.buf[pos + 12] = (s16) s;
			vo.bufPos = (pos + 1) % 12;
		}
	}
}

void Machine::runEnvelope (Voice &vo, int n)
{
	int env = vo.env;
	if (vo.envMode == ENV_RELEASE) { env -= 8; if (env < 0) env = 0; vo.env = env; return; }
	u8 adsr0 = dsp[n * 16 + 5];
	int rateIdx, envData = dsp[n * 16 + 6];
	if (adsr0 & 0x80)
	{
		if (vo.envMode >= ENV_DECAY)
		{
			env--; env -= env >> 8;
			rateIdx = envData & 0x1F;
			if (vo.envMode == ENV_DECAY) rateIdx = ((adsr0 >> 3) & 0x0E) + 0x10;
		}
		else { rateIdx = (adsr0 & 0x0F) * 2 + 1; env += rateIdx < 31 ? 0x20 : 0x400; }
	}
	else
	{
		envData = dsp[n * 16 + 7];
		int mode = envData >> 5;
		if (mode < 4) { env = envData * 0x10; rateIdx = 31; }
		else
		{
			rateIdx = envData & 0x1F;
			if (mode == 4) env -= 0x20;
			else if (mode < 6) { env--; env -= env >> 8; }
			else { env += 0x20; if (mode > 6 && (unsigned) vo.hiddenEnv >= 0x600) env += 0x8 - 0x20; }
		}
	}
	if ((env >> 8) == (envData >> 5) && vo.envMode == ENV_DECAY) vo.envMode = ENV_SUSTAIN;
	vo.hiddenEnv = env;
	if ((unsigned) env > 0x7FF) { env = env < 0 ? 0 : 0x7FF; if (vo.envMode == ENV_ATTACK) vo.envMode = ENV_DECAY; }
	if (counterTick (rateIdx)) vo.env = env;
}

void Machine::dspSample ()
{
	if (--dspCounter < 0) dspCounter = 30720 - 1;
	u8 flg = dsp[0x6C];
	if (counterTick (flg & 0x1F)) { int fb = (noise << 13) ^ (noise << 14); noise = (u16) ((fb & 0x4000) ^ (noise >> 1)); }
	everyOther = !everyOther;
	u8 kon = 0, koff = dsp[0x5C];
	if (everyOther) { kon = konPending; konPending = 0; endx &= (u8) ~kon; }
	int mainL = 0, mainR = 0, echoL = 0, echoR = 0, prevOut = 0;
	u16 dir = (u16) (dsp[0x5D] << 8);
	for (int n = 0; n < 8; n++)
	{
		Voice &vo = voice[n];
		u8 *r = dsp + n * 16;
		u8 bit = (u8) (1 << n);
		int pitch = (r[2] | r[3] << 8) & 0x3FFF;
		if (n && (dsp[0x2D] & bit)) pitch += ((prevOut >> 5) * pitch) >> 10;
		u16 srcAddr = (u16) (dir + r[4] * 4);
		u8 header = aram[vo.brrAddr];
		if (vo.konDelay)
		{
			if (vo.konDelay == 5)
			{
				vo.brrAddr = (u16) (aram[srcAddr] | aram[(u16) (srcAddr + 1)] << 8);
				vo.brrOffset = 1; vo.bufPos = 0; header = 0;
			}
			vo.env = 0; vo.hiddenEnv = 0; vo.interpPos = 0;
			if (--vo.konDelay & 3) vo.interpPos = 0x4000;
			pitch = 0;
		}
		// the sample: gaussian interpolation of 4 BRR samples, or the noise
		int out;
		if (dsp[0x3D] & bit) out = (s16) (noise << 1);
		else
		{
			int off = (vo.interpPos >> 4) & 0xFF;
			const s16 *in = vo.buf + vo.bufPos + (vo.interpPos >> 12);
			out = (GAUSS[255 - off] * in[0]) >> 11;
			out += (GAUSS[511 - off] * in[1]) >> 11;
			out += (GAUSS[256 + off] * in[2]) >> 11;
			out = (s16) out;
			out += (GAUSS[off] * in[3]) >> 11;
			out = clamp16 (out) & ~1;
		}
		out = ((out * vo.env) >> 11) & ~1;
		r[8] = (u8) (vo.env >> 4); r[9] = (u8) (out >> 8);
		if ((flg & 0x80) || (header & 3) == 1) { vo.envMode = ENV_RELEASE; vo.env = 0; }
		if (everyOther)
		{
			if (koff & bit) vo.envMode = ENV_RELEASE;
			if (kon & bit) { vo.konDelay = 5; vo.envMode = ENV_ATTACK; }
		}
		if (!vo.konDelay) runEnvelope (vo, n);
		prevOut = out;
		int l = (out * (s8) r[0]) >> 7, rr = (out * (s8) r[1]) >> 7;
		mainL = clamp16 (mainL + l); mainR = clamp16 (mainR + rr);
		if (dsp[0x4D] & bit) { echoL = clamp16 (echoL + l); echoR = clamp16 (echoR + rr); }
		// the next BRR samples
		if (vo.interpPos >= 0x4000)
		{
			decodeBrr (vo);
			vo.brrOffset += 2;
			if (vo.brrOffset >= 9)
			{
				vo.brrAddr = (u16) (vo.brrAddr + 9);
				if (header & 1)
				{
					vo.brrAddr = (u16) (aram[(u16) (srcAddr + 2)] | aram[(u16) (srcAddr + 3)] << 8);
					endx |= bit;
				}
				vo.brrOffset = 1;
			}
		}
		vo.interpPos = (vo.interpPos & 0x3FFF) + pitch;
		if (vo.interpPos > 0x7FFF) vo.interpPos = 0x7FFF;
	}
	// the echo
	u16 ea = (u16) ((dsp[0x6D] << 8) + echoOffset);
	echoHistPos = (echoHistPos + 1) & 7;
	echoHist[echoHistPos][0] = (s16) ((s16) (aram[ea] | aram[(u16) (ea + 1)] << 8) >> 1);
	echoHist[echoHistPos][1] = (s16) ((s16) (aram[(u16) (ea + 2)] | aram[(u16) (ea + 3)] << 8) >> 1);
	int fir[2];
	for (int c = 0; c < 2; c++)
	{
		int s = 0;
		for (int i = 0; i < 7; i++) s += (echoHist[(echoHistPos + 1 + i) & 7][c] * (s8) dsp[i * 16 + 0x0F]) >> 6;
		s = (s16) s;
		s += (echoHist[echoHistPos][c] * (s8) dsp[0x7F]) >> 6;
		fir[c] = clamp16 (s) & ~1;
	}
	int outL = clamp16 (((mainL * (s8) dsp[0x0C]) >> 7) + ((fir[0] * (s8) dsp[0x2C]) >> 7));
	int outR = clamp16 (((mainR * (s8) dsp[0x1C]) >> 7) + ((fir[1] * (s8) dsp[0x3C]) >> 7));
	if (!(flg & 0x20))
	{
		int wl = clamp16 (echoL + (s16) ((fir[0] * (s8) dsp[0x0D]) >> 7)) & ~1;
		int wr = clamp16 (echoR + (s16) ((fir[1] * (s8) dsp[0x0D]) >> 7)) & ~1;
		aram[ea] = (u8) wl; aram[(u16) (ea + 1)] = (u8) (wl >> 8);
		aram[(u16) (ea + 2)] = (u8) wr; aram[(u16) (ea + 3)] = (u8) (wr >> 8);
	}
	if (!echoOffset) echoLength = (dsp[0x7D] & 15) * 0x800;
	echoOffset += 4;
	if (echoOffset >= echoLength) echoOffset = 0;
	if (flg & 0x40) outL = outR = 0;
	dspOut (outL, outR);
}

} // namespace snes
