//
// fmsynth.h -- AudioKit's FM synthesizer: 16 voices, each a plain wave (square, sine, triangle, saw,
// noise) or a two-operator FM instrument (OPL2 style: struct kapi_fm_instrument), in integer
// arithmetic. It was the kernel's (kernel/sys/sound.cpp) until 2026-10-05; the kernel only puts
// sound out now, and this is the one source: compiled into audiokit.so (akfm.cpp: ak_fm_*) and, as
// it is, into the PC tools (tools/fmsplayer, tools/tests/sound, tools/tests/fms).
//
//   fmsynth::instrument (voice, &ins);  fmsynth::start (voice, milli_hz, SOUND_FM, 220);
//   fmsynth::render (frames, n);        fmsynth::stop (voice);            (voice -1: all)
//
// One synthesizer per program (its state is static here: include it in ONE source). No lock: the
// caller keeps start / stop / instrument and render apart (akfm.cpp does, with a kapi lock).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. Permission is hereby
// granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions: The above copyright notice and this permission notice shall
// be included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS
// IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
//
#ifndef _audiokit_fmsynth_h
#define _audiokit_fmsynth_h

#include <stdint.h>
#include <kern/kapi_abi.h>		// struct kapi_fm_instrument, FM_SUSTAINED..., SOUND_SQUARE...SOUND_FM

#define FMSYNTH_RATE	44100
#define FMSYNTH_VOICES	16

namespace fmsynth {

typedef int16_t s16; typedef uint16_t u16; typedef uint32_t u32; typedef int32_t s32;
typedef uint64_t u64; typedef int64_t s64;

#include "fm_tables.h"

enum { WAVE_SQUARE = 0, WAVE_SINE, WAVE_TRIANGLE, WAVE_SAW, WAVE_NOISE, WAVE_FM };

// One FM operator (OPL2 style): its envelope works on an ATTENUATION in units of 1/256
// octave (6.02 dB / 256; 4096 units = 96 dB = silent), in 16.16 fixed point.
enum { EG_OFF = 0, EG_ATTACK, EG_DECAY, EG_SUSTAIN, EG_RELEASE };
struct TOperator
{
	u32	nPhase, nMult2;				// phase; frequency multiplier x2
	int	nStage;
	u32	nAtt;					// 16.16 attenuation units
	u32	nAttackK, nDecayInc, nReleaseInc;	// per sample (see RateInit)
	u32	nSustainAtt, nLevelAtt;			// SL / TL, in units
	int	nWave;					// 0 sine, 1 half, 2 abs, 3 quarter pulses
	bool	bSustained, bTremolo, bVibrato;
};

struct TVoice
{
	bool	bOn;					// key down
	u32	nPhase, nInc;				// 32-bit phase accumulator
	int	nWave;
	u32	nGain, nTarget;				// 0..65536 (envelope; FM: the volume)
	u32	nNoise;					// LFSR state
	s32	nNoiseVal;
	// FM (WAVE_FM): 0 = modulator, 1 = carrier
	TOperator Op[2];
	int	nFeedback, nConnection;
	s32	nFb1, nFb2;				// the modulator's last two outputs
	bool	bHasFM;
};

static TVoice   s_Voice[FMSYNTH_VOICES];
static bool  s_bTables = false;
static u32      s_nLfoTrem = 0, s_nLfoVib = 0;	// tremolo 3.7 Hz / vibrato 6.1 Hz phases

static void BuildTables (void)
{
	for (int v = 0; v < FMSYNTH_VOICES; v++)
	{
		TVoice &x = s_Voice[v];
		x.bOn = false; x.nGain = x.nTarget = 0; x.nNoise = 0xACE1u + v; x.nNoiseVal = 0;
		x.bHasFM = false; x.nFb1 = x.nFb2 = 0;
		x.Op[0].nStage = x.Op[1].nStage = EG_OFF;
	}
	s_bTables = true;
}

// ---- FM ------------------------------------------------------------------------------------------
#define ATT_MAX		4096u				// 96 dB
static const u32 s_Mult2[16] = { 1, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 20, 24, 24, 30, 30 };	// OPL x2

// OPL2 timing: attack 0..100 % takes 2826 ms at rate 1 and halves per rate step (15 =
// instant); decay / release over 96 dB take 39280 ms at rate 1, halving too (0 = never).
static u32 AttackK (int r)				// 16.16 exponential factor per sample
{
	if (r <= 0) return 0;
	if (r >= 15) return 0xFFFFFFFFu;			// instant
	u64 k = ((u64) 543949 << (r - 1)) / 124637;		// 65536 * ln(4096) / samples
	return (u32) (k ? k : 1);
}
static u32 LinearInc (int r)				// 16.16 attenuation units per sample
{
	if (r <= 0) return 0;
	return (u32) ((((u64) ATT_MAX << 16) << (r - 1)) * 1000 / (39280ull * FMSYNTH_RATE));
}

static inline s32 OpWave (const TOperator &o, u32 nPhase)
{
	unsigned i = nPhase >> 22;				// 1024 steps
	s32 v = s_Sin1024[i];
	switch (o.nWave)
	{
	case 1: return i < 512 ? v : 0;			// half sine
	case 2: return v < 0 ? -v : v;				// absolute sine
	case 3: return (i & 256) ? 0 : (v < 0 ? -v : v);	// quarter pulses
	default: return v;
	}
}
// Advance the envelope one sample; returns the current attenuation in units.
static inline u32 OpEnvelope (TOperator &o)
{
	switch (o.nStage)
	{
	case EG_ATTACK:
		if (o.nAttackK == 0xFFFFFFFFu) o.nAtt = 0;
		else if (o.nAttackK) o.nAtt -= (u32) (((u64) o.nAtt * o.nAttackK) >> 16) + 1;
		if ((s32) o.nAtt <= (1 << 16)) { o.nAtt = 0; o.nStage = EG_DECAY; }
		break;
	case EG_DECAY:
		o.nAtt += o.nDecayInc;
		if ((o.nAtt >> 16) >= o.nSustainAtt) { o.nAtt = o.nSustainAtt << 16; o.nStage = EG_SUSTAIN; }
		break;
	case EG_SUSTAIN:					// a non-sustained sound keeps fading (release rate)
		if (!o.bSustained) o.nAtt += o.nReleaseInc;
		break;
	case EG_RELEASE:
		o.nAtt += o.nReleaseInc;
		break;
	default:
		return ATT_MAX;
	}
	if ((o.nAtt >> 16) >= ATT_MAX) { o.nAtt = ATT_MAX << 16; if (o.nStage >= EG_SUSTAIN) o.nStage = EG_OFF; return ATT_MAX; }
	return o.nAtt >> 16;
}
static inline s32 AttToAmp (u32 att)			// 0..65535
{
	if (att >= ATT_MAX) return 0;
	return s_Exp256[att & 255] >> (att >> 8);
}

// One FM sample of voice v (tremolo 0..42 units, vibrato -256..256).
static inline s32 FMSample (TVoice &v, u32 nTrem, s32 nVib)
{
	TOperator &m = v.Op[0], &c = v.Op[1];
	u32 inc = v.nInc;
	// modulator (with feedback)
	u32 im = (inc >> 1) * m.nMult2;
	if (m.bVibrato) im += (u32) (((s64) (im >> 8) * nVib) >> 8);
	u32 am = OpEnvelope (m) + m.nLevelAtt + (m.bTremolo ? nTrem : 0);
	u32 ph = m.nPhase;
	if (v.nFeedback) ph += ((u32) ((v.nFb1 + v.nFb2) >> (12 - v.nFeedback))) << 22;
	s32 mo = (OpWave (m, ph) * AttToAmp (am)) >> 16;
	m.nPhase += im;
	v.nFb2 = v.nFb1; v.nFb1 = mo;
	// carrier
	u32 ic = (inc >> 1) * c.nMult2;
	if (c.bVibrato) ic += (u32) (((s64) (ic >> 8) * nVib) >> 8);
	u32 ac = OpEnvelope (c) + c.nLevelAtt + (c.bTremolo ? nTrem : 0);
	u32 pc = c.nPhase + (v.nConnection ? 0 : ((u32) mo << 19));	// FM: +-4 periods at full level
	s32 co = (OpWave (c, pc) * AttToAmp (ac)) >> 16;
	c.nPhase += ic;
	return v.nConnection ? (mo + co) >> 1 : co;
}

static inline s32 WaveSample (TVoice &v)
{
	u32 p = v.nPhase;
	switch (v.nWave)
	{
	case WAVE_SINE:  return s_Sin1024[p >> 22];
	case WAVE_TRIANGLE:
	{
		s32 t = (s32) (p >> 15);			// 0..131071
		return t < 65536 ? t - 32768 : 98303 - t;
	}
	case WAVE_SAW:   return (s32) (p >> 16) - 32768;
	case WAVE_NOISE: return v.nNoiseVal;
	default:         return p < 0x80000000u ? 24000 : -24000;	// square (a bit softer)
	}
}

// The voices' next nFrames stereo frames WRITTEN to pOut (s16 L, R: the same on both sides).
static void render (s16 *pOut, unsigned nFrames)
{
	if (!s_bTables) BuildTables ();
	for (unsigned f = 0; f < nFrames; f++)
	{
		s32 mix = 0;
		// LFOs: tremolo 1 dB (42 units) at 3.7 Hz, vibrato +-7 cents at 6.1 Hz (triangles)
		s_nLfoTrem += 360353u; s_nLfoVib += 594096u;		// 3.7 / 6.1 Hz * 2^32 / 44100
		u32 tt = s_nLfoTrem >> 16; u32 nTrem = ((tt < 32768 ? tt : 65535 - tt) * 42) >> 15;
		u32 vt = s_nLfoVib >> 16; s32 nVib = ((s32) (vt < 32768 ? vt : 65535 - vt) >> 6) - 256;
		nVib = nVib * 7 / 17;					// +-256 = +-0.39 % ~ +-7 cents
		for (int i = 0; i < FMSYNTH_VOICES; i++)
		{
			TVoice &v = s_Voice[i];
			if (v.nWave == WAVE_FM)
			{
				if (v.Op[0].nStage == EG_OFF && v.Op[1].nStage == EG_OFF) continue;
				mix += (FMSample (v, nTrem, nVib) * (s32) (v.nGain >> 1)) >> 15;
				continue;
			}
			if (v.nGain == 0 && !v.bOn) continue;
			// envelope: ~5 ms attack / release (65536 / 220 samples)
			if (v.nGain < v.nTarget) { v.nGain += 300; if (v.nGain > v.nTarget) v.nGain = v.nTarget; }
			else if (v.nGain > v.nTarget) { v.nGain = v.nGain > 300 + v.nTarget ? v.nGain - 300 : v.nTarget; }
			u32 old = v.nPhase;
			v.nPhase += v.nInc;
			if (v.nWave == WAVE_NOISE && v.nPhase < old)	// a new noise value per period
			{
				v.nNoise = (v.nNoise >> 1) ^ (-(v.nNoise & 1u) & 0xB400u);
				v.nNoiseVal = (s32) (v.nNoise & 0xFFFF) - 32768;
			}
			mix += (WaveSample (v) * (s32) (v.nGain >> 1)) >> 15;
		}
		mix >>= 2;						// 4 full voices before clipping
		if (mix > 32767) mix = 32767; else if (mix < -32768) mix = -32768;
		pOut[f * 2] = pOut[f * 2 + 1] = (s16) mix;
	}
}

// 1 while a voice sounds (a key held, or its release not yet over).
__attribute__ ((unused)) static int active (void)
{
	if (!s_bTables) return 0;
	for (int i = 0; i < FMSYNTH_VOICES; i++)
	{
		const TVoice &v = s_Voice[i];
		if (v.nWave == WAVE_FM ? (v.Op[0].nStage != EG_OFF || v.Op[1].nStage != EG_OFF) : (v.bOn || v.nGain != 0)) return 1;
	}
	return 0;
}

// Everything silent at once (no release).
__attribute__ ((unused)) static void silence (void)
{
	for (int i = 0; i < FMSYNTH_VOICES; i++)
	{
		TVoice &v = s_Voice[i];
		v.bOn = false; v.nGain = v.nTarget = 0; v.Op[0].nStage = v.Op[1].nStage = EG_OFF;
	}
}

static void KeyOff (TVoice &v)
{
	v.bOn = false;
	if (v.nWave == WAVE_FM) { for (int k = 0; k < 2; k++) if (v.Op[k].nStage != EG_OFF) v.Op[k].nStage = EG_RELEASE; }
	else v.nTarget = 0;
}

static int start (int nVoice, unsigned nMilliHz, int nWave, int nVolume)
{
	if (nVoice < 0 || nVoice >= FMSYNTH_VOICES) return -1;
	if (nVolume < 0) nVolume = 0;
	if (nVolume > 255) nVolume = 255;
	if (nMilliHz > 20000000) nMilliHz = 20000000;		// 20 kHz
	if (!s_bTables) BuildTables ();
	TVoice &v = s_Voice[nVoice];
	v.nInc = (u32) (((u64) nMilliHz << 32) / (1000ull * FMSYNTH_RATE));
	if (nWave == WAVE_FM && v.bHasFM)			// key on: both envelopes restart
	{
		if (v.nWave != WAVE_FM) { v.Op[0].nAtt = v.Op[1].nAtt = ATT_MAX << 16; }
		v.nWave = WAVE_FM;
		v.nGain = v.nTarget = (u32) nVolume * 257;
		for (int k = 0; k < 2; k++) { v.Op[k].nStage = EG_ATTACK; v.Op[k].nPhase = 0; if (v.Op[k].nAtt > (ATT_MAX << 16)) v.Op[k].nAtt = ATT_MAX << 16; }
		v.nFb1 = v.nFb2 = 0;
		v.bOn = true;
		return 0;
	}
	if (v.nWave == WAVE_FM) { v.Op[0].nStage = v.Op[1].nStage = EG_OFF; v.nGain = 0; }
	v.nWave = nWave >= WAVE_SQUARE && nWave <= WAVE_NOISE ? nWave : WAVE_SQUARE;
	v.nTarget = (u32) nVolume * 257;
	if (!v.bOn && v.nGain == 0) v.nPhase = 0;
	v.bOn = true;
	return 0;
}

static int stop (int nVoice)
{
	if (!s_bTables) BuildTables ();
	for (int i = 0; i < FMSYNTH_VOICES; i++)
		if (nVoice < 0 || nVoice == i) KeyOff (s_Voice[i]);
	return 0;
}

// An FM instrument for a voice (then sound_start (voice, f, SOUND_FM, vol) plays it).
static int instrument (int nVoice, const kapi_fm_instrument *pIns)
{
	if (nVoice < 0 || nVoice >= FMSYNTH_VOICES || pIns == 0) return -1;
	kapi_fm_instrument In = *pIns;
	if (!s_bTables) BuildTables ();
	TVoice &v = s_Voice[nVoice];
	for (int k = 0; k < 2; k++)
	{
		const kapi_fm_op &p = In.op[k];
		TOperator &o = v.Op[k];
		o.nMult2 = s_Mult2[p.mult & 15];
		o.nLevelAtt = (u32) (p.level & 63) * 32;		// 0.75 dB steps
		o.nSustainAtt = (u32) (p.sustain & 15) * 128;		// 3 dB steps
		if ((p.sustain & 15) == 15) o.nSustainAtt = ATT_MAX;
		o.nAttackK = AttackK (p.attack & 15);
		o.nDecayInc = LinearInc (p.decay & 15);
		o.nReleaseInc = LinearInc (p.release & 15);
		o.nWave = p.wave & 3;
		o.bSustained = (p.flags & FM_SUSTAINED) != 0;
		o.bTremolo = (p.flags & FM_TREMOLO) != 0;
		o.bVibrato = (p.flags & FM_VIBRATO) != 0;
		if (v.nWave != WAVE_FM) { o.nStage = EG_OFF; o.nAtt = ATT_MAX << 16; }
	}
	v.nFeedback = In.feedback & 7;
	v.nConnection = In.connection & 1;
	v.bHasFM = true;
	return 0;
}

} // namespace fmsynth

#endif
