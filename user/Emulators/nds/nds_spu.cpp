//
// nds/nds_spu.cpp -- the sound: 16 channels (PCM 8 / 16 bits, IMA-ADPCM with its loop state, the
// PSG squares on 8-13, the noise on 14-15), their volume / divider / panning, the mixer (the master
// volume, the outputs that take channels 1 / 3 alone), the two capture units (into memory, at
// channel 1's / 3's rate), made at 32728 Hz (1024 bus cycles a sample) and resampled to the host's rate.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (see nds.h).
//
#include "nds/nds.h"

namespace nds {

static const s16 ADPCM_STEP[89] = {
	7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97,
	107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
	876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428,
	4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350,
	22385, 24623, 27086, 29794, 32767 };
static const s8 ADPCM_IDX[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };

enum { SAMPLE_CYCLES = 2048 };				// master cycles a 32728 Hz sample
static const int OUT_HZ = 32728;

void Spu::reset (Machine *mm)
{
	m = mm;
	for (int i = 0; i < 16; i++)
	{
		Chan &c = ch[i];
		c.cnt = 0; c.sad = 0; c.tmr = 0; c.pnt = 0; c.len = 0; c.on = false;
		c.pos = 0; c.timer = 0; c.sample = 0; c.adpcmVal = 0; c.adpcmIdx = 0; c.loopVal = 0; c.loopIdx = 0; c.adpcmLoopSaved = false;
		c.lfsr = 0x7FFF; c.psgPos = 0; c.fifoN = 0;
	}
	for (int i = 0; i < 2; i++) { cap[i].cnt = 0; cap[i].dad = 0; cap[i].len = 0; cap[i].pos = 0; cap[i].timer = 0; }
	soundcnt = 0; bias = 0;
	last = 0; acc = 0;
	prevL = prevR = curL = curR = 0;
	ahead = atail = 0;
}

void Spu::start (int i)
{
	Chan &c = ch[i];
	c.on = true;
	c.pos = 0;
	c.timer = c.tmr;
	c.lfsr = 0x7FFF; c.psgPos = 0;
	c.adpcmLoopSaved = false;
	c.sample = 0;
	if (((c.cnt >> 29) & 3) == 2)
	{
		u32 h = m->a7Read32 (c.sad);
		c.adpcmVal = (s16) (h & 0xFFFF);
		c.adpcmIdx = (s32) ((h >> 16) & 0x7F); if (c.adpcmIdx > 88) c.adpcmIdx = 88;
		c.sample = c.adpcmVal;
	}
}

// the next sample of a channel (one timer overflow): false = it stopped
static bool chanStep (Machine *m, Spu::Chan &c, int idx)
{
	int fmt = (int) (c.cnt >> 29) & 3;
	int rep = (int) (c.cnt >> 27) & 3;
	if (fmt == 3)
	{
		if (idx >= 8 && idx <= 13)
		{
			int duty = (int) (c.cnt >> 24) & 7;
			c.psgPos = (c.psgPos + 1) & 7;
			c.sample = c.psgPos <= duty ? 0x7FFF : -0x7FFF;
		}
		else if (idx >= 14)
		{
			bool carry = c.lfsr & 1;
			c.lfsr >>= 1;
			if (carry) { c.lfsr ^= 0x6000; c.sample = -0x7FFF; } else c.sample = 0x7FFF;
		}
		else c.sample = 0;
		return true;
	}
	u32 total, loopStart;
	if (fmt == 0) { total = (c.pnt + c.len) * 4; loopStart = c.pnt * 4u; }
	else if (fmt == 1) { total = (c.pnt + c.len) * 2; loopStart = c.pnt * 2u; }
	else { total = (c.pnt + c.len) * 8; loopStart = c.pnt * 8u; total = total >= 8 ? total - 8 : 0; loopStart = loopStart >= 8 ? loopStart - 8 : 0; }
	if (c.pos >= total)
	{
		if (rep == 1)
		{
			c.pos = loopStart;
			if (fmt == 2 && c.adpcmLoopSaved) { c.adpcmVal = c.loopVal; c.adpcmIdx = c.loopIdx; }
		}
		else { c.on = false; c.cnt &= ~0x80000000u; c.sample = 0; return false; }
	}
	switch (fmt)
	{
	case 0: c.sample = (s32) (s8) m->a7Read8 (c.sad + c.pos) << 8; break;
	case 1: c.sample = (s32) (s16) m->a7Read16 (c.sad + c.pos * 2); break;
	default:
	{
		if (c.pos == loopStart && !c.adpcmLoopSaved) { c.loopVal = c.adpcmVal; c.loopIdx = c.adpcmIdx; c.adpcmLoopSaved = true; }
		u8 b = (u8) m->a7Read8 (c.sad + 4 + (c.pos >> 1));
		u32 nib = (c.pos & 1) ? b >> 4 : b & 15;
		s32 step = ADPCM_STEP[c.adpcmIdx];
		s32 diff = step >> 3;
		if (nib & 1) diff += step >> 2;
		if (nib & 2) diff += step >> 1;
		if (nib & 4) diff += step;
		if (nib & 8) { c.adpcmVal -= diff; if (c.adpcmVal < -0x7FFF) c.adpcmVal = -0x7FFF; }
		else { c.adpcmVal += diff; if (c.adpcmVal > 0x7FFF) c.adpcmVal = 0x7FFF; }
		c.adpcmIdx += ADPCM_IDX[nib & 7];
		if (c.adpcmIdx < 0) c.adpcmIdx = 0; if (c.adpcmIdx > 88) c.adpcmIdx = 88;
		c.sample = c.adpcmVal;
		break;
	}
	}
	c.pos++;
	return true;
}

static void capWrite (Machine *m, Spu::Cap &k, s32 v)
{
	if (!(k.cnt & 0x80)) return;
	u32 lenBytes = (u32) (k.len ? k.len : 1) * 4;
	if (v > 0x7FFF) v = 0x7FFF; if (v < -0x8000) v = -0x8000;
	if (k.cnt & 8) { m->a7Write8 (k.dad + k.pos, (u32) (u8) (v >> 8)); k.pos += 1; }
	else { m->a7Write16 (k.dad + k.pos, (u32) (u16) v); k.pos += 2; }
	if (k.pos >= lenBytes)
	{
		if (k.cnt & 4) k.cnt &= 0x7F;		// one shot: stops
		k.pos = 0;
	}
}

void Spu::sample ()
{
	s64 L = 0, R = 0;
	s64 out1L = 0, out1R = 0, out3L = 0, out3R = 0;
	s32 chOut[16];
	for (int i = 0; i < 16; i++)
	{
		Chan &c = ch[i];
		chOut[i] = 0;
		if (!c.on) continue;
		c.timer += 512;
		while (c.timer >= 0x10000)
		{
			c.timer = c.timer - 0x10000 + c.tmr;
			if (!chanStep (m, c, i)) break;
			if (i == 1 || i == 3)
			{
				Cap &k = cap[i >> 1];
				if (k.cnt & 0x80)
				{
					s32 src;
					if (k.cnt & 2) src = (s32) ((s64) ch[i - 1].sample * (s32) (ch[i - 1].cnt & 127) >> 7);	// channel 0 / 2
					else src = (s32) ((i == 1 ? curL : curR));					// the mixer
					capWrite (m, k, src);
				}
			}
			if (c.tmr >= 0xFFFF && c.timer < 0x10000) break;
		}
		if (!c.on) continue;
		s32 vol = (s32) (c.cnt & 127);
		static const int SH[4] = { 0, 1, 2, 4 };
		s64 v = ((s64) c.sample * vol) >> 7;
		v >>= SH[(c.cnt >> 8) & 3];
		chOut[i] = (s32) v;
		s32 pan = (s32) ((c.cnt >> 16) & 127);
		s64 l = (v * (128 - pan)) >> 7, r = (v * pan) >> 7;
		if (i == 1) { out1L = l; out1R = r; if (soundcnt & 0x1000) continue; }
		if (i == 3) { out3L = l; out3R = r; if (soundcnt & 0x2000) continue; }
		L += l; R += r;
	}
	(void) chOut;
	if (!(soundcnt & 0x8000)) { L = R = 0; }
	else
	{
		int ls = (soundcnt >> 8) & 3, rs = (soundcnt >> 10) & 3;
		if (ls == 1) L = out1L; else if (ls == 2) L = out3L; else if (ls == 3) L = out1L + out3L;
		if (rs == 1) R = out1R; else if (rs == 2) R = out3R; else if (rs == 3) R = out1R + out3R;
		s64 mv = soundcnt & 127;
		L = (L * mv) >> 7; R = (R * mv) >> 7;
	}
	L >>= 1; R >>= 1;
	if (L > 0x7FFF) L = 0x7FFF; if (L < -0x8000) L = -0x8000;
	if (R > 0x7FFF) R = 0x7FFF; if (R < -0x8000) R = -0x8000;
	prevL = curL; prevR = curR;
	curL = (s32) L; curR = (s32) R;
	// to the output rate (linear between the last two)
	acc += rate;
	while (acc >= OUT_HZ)
	{
		acc -= OUT_HZ;
		s64 f = acc * 256 / rate;				// how far back from the current sample
		s32 l = (s32) (curL + ((prevL - curL) * f >> 8)), r = (s32) (curR + ((prevR - curR) * f >> 8));
		int nx = (ahead + 1) & (ABUF - 1);
		if (nx == atail) break;					// (full: the reader is late)
		abuf[ahead * 2] = (short) l; abuf[ahead * 2 + 1] = (short) r;
		ahead = nx;
	}
}

void Spu::run (s64 now)
{
	if (last > now) { last = now; return; }
	if (now - last > (s64) SAMPLE_CYCLES * 4096) last = now - (s64) SAMPLE_CYCLES * 4096;
	while (last + SAMPLE_CYCLES <= now) { sample (); last += SAMPLE_CYCLES; }
}

u32 Spu::read32 (u32 a)
{
	u32 off = a & 0x1FC;
	if (off < 0x100)
	{
		Chan &c = ch[off >> 4];
		switch (off & 15)
		{
		case 0: return c.cnt;
		case 4: return c.sad;
		case 8: return c.tmr | ((u32) c.pnt << 16);
		default: return c.len;
		}
	}
	switch (off)
	{
	case 0x100: return soundcnt;
	case 0x104: return bias;
	case 0x108: return cap[0].cnt | ((u32) cap[1].cnt << 8);
	case 0x110: return cap[0].dad;
	case 0x118: return cap[1].dad;
	}
	return 0;
}

void Spu::write32 (u32 a, u32 v)
{
	m->spu.run (m->curTime ());
	u32 off = a & 0x1FC;
	if (off < 0x100)
	{
		int i = (int) (off >> 4);
		Chan &c = ch[i];
		switch (off & 15)
		{
		case 0:
		{
			u32 old = c.cnt;
			c.cnt = v & 0xFF7F837F;
			if (!(old & 0x80000000) && (v & 0x80000000)) start (i);
			else if (!(v & 0x80000000)) c.on = false;
			break;
		}
		case 4: c.sad = v & 0x07FFFFFC; break;
		case 8: c.tmr = (u16) v; c.pnt = (u16) (v >> 16); break;
		default: c.len = v & 0x003FFFFF; break;
		}
		return;
	}
	switch (off)
	{
	case 0x100: soundcnt = (u16) (v & 0xBF7F); return;
	case 0x104: bias = (u16) (v & 0x3FF); return;
	case 0x108: write8 (a, (u8) v); write8 (a + 1, (u8) (v >> 8)); return;
	case 0x110: cap[0].dad = v & 0x07FFFFFC; return;
	case 0x114: cap[0].len = (u16) v; return;
	case 0x118: cap[1].dad = v & 0x07FFFFFC; return;
	case 0x11C: cap[1].len = (u16) v; return;
	}
}

void Spu::write16 (u32 a, u16 v)
{
	u32 off = a & 0x1FF;
	if (off == 0x108 || off == 0x109) { write8 (a, (u8) v); write8 (a + 1, (u8) (v >> 8)); return; }
	u32 cur = read32 (a & ~3u);
	u32 sh = (a & 2) * 8;
	write32 (a & ~3u, (cur & ~(0xFFFFu << sh)) | ((u32) v << sh));
}

void Spu::write8 (u32 a, u8 v)
{
	u32 off = a & 0x1FF;
	if (off == 0x108 || off == 0x109)
	{
		m->spu.run (m->curTime ());
		Cap &k = cap[off - 0x108];
		u8 old = k.cnt;
		k.cnt = v & 0x8F;
		if (!(old & 0x80) && (v & 0x80)) { k.pos = 0; }
		return;
	}
	u32 cur = read32 (a & ~3u);
	u32 sh = (a & 3) * 8;
	write32 (a & ~3u, (cur & ~(0xFFu << sh)) | ((u32) v << sh));
}

} // namespace nds
