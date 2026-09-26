//
// sound.cpp -- the Onyx sound system (see kern/sound.h): voices + PCM stream, mixed in
// integer arithmetic (no FP in the kernel) and played by a CPWMSoundBaseDevice subclass
// whose GetChunk () is the "producer": zeros while nothing plays, otherwise the waveforms
// of the voices that are on (+ the stream).
//
// Single core (the default Circle build): GetChunk renders the chunk itself, in the DMA
// completion interrupt on core 0 (~1024 frames every 23 ms: cheap).
// ARM_ALLOW_MULTI_CORE: core 1 is the producer -- it sleeps (WFE) until the audio is first
// used, then keeps SND_AHEAD chunks rendered in a ring; GetChunk only copies the next one
// (or zeros if core 1 fell behind).
//
#ifndef SOUND_HOST_TEST				// (tools/tests/sound: the synth on a PC)
#include <kern/sound.h>
#include <circle/sound/pwmsoundbasedevice.h>
#include <circle/interrupt.h>
#include <circle/spinlock.h>
#include <circle/synchronize.h>
#include <circle/logger.h>
#include <circle/new.h>
#endif
#include <kern/kapi_abi.h>				// struct kapi_fm_instrument
#include "sound_tables.h"

#define SND_CHUNK	2048				// words per GetChunk = 1024 stereo frames
#define SND_FRAMES	(SND_CHUNK / 2)
#define SND_STREAM	(SND_RATE / 2)			// PCM ring: 0.5 s of stereo frames
#define SND_AHEAD	4				// (multi-core) chunks rendered ahead

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
	boolean	bSustained, bTremolo, bVibrato;
};

struct TVoice
{
	boolean	bOn;					// key down
	u32	nPhase, nInc;				// 32-bit phase accumulator
	int	nWave;
	u32	nGain, nTarget;				// 0..65536 (envelope; FM: the volume)
	u32	nNoise;					// LFSR state
	s32	nNoiseVal;
	// FM (WAVE_FM): 0 = modulator, 1 = carrier
	TOperator Op[2];
	int	nFeedback, nConnection;
	s32	nFb1, nFb2;				// the modulator's last two outputs
	boolean	bHasFM;
};

static TVoice   s_Voice[SND_VOICES];
static s16      s_Stream[SND_STREAM * 2];		// stereo ring
static unsigned s_nStreamRd = 0, s_nStreamWr = 0;	// frame indices (mod SND_STREAM)
static unsigned s_nOwner = 0;				// owning pid (0 = free)
static CSpinLock s_Lock (IRQ_LEVEL);
static boolean  s_bTables = FALSE;
static u32      s_nLfoTrem = 0, s_nLfoVib = 0;	// tremolo 3.7 Hz / vibrato 6.1 Hz phases
static volatile boolean s_bRunning = FALSE;

static const char From[] = "sound";

// ---- the synthesizer ----------------------------------------------------------------------
static void BuildTables (void)
{
	for (int v = 0; v < SND_VOICES; v++)
	{
		TVoice &x = s_Voice[v];
		x.bOn = FALSE; x.nGain = x.nTarget = 0; x.nNoise = 0xACE1u + v; x.nNoiseVal = 0;
		x.bHasFM = FALSE; x.nFb1 = x.nFb2 = 0;
		x.Op[0].nStage = x.Op[1].nStage = EG_OFF;
	}
	s_bTables = TRUE;
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
	return (u32) ((((u64) ATT_MAX << 16) << (r - 1)) * 1000 / (39280ull * SND_RATE));
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

// Mix nFrames stereo frames into pOut (s16 L, R). Caller holds s_Lock.
static void Render (s16 *pOut, unsigned nFrames)
{
	for (unsigned f = 0; f < nFrames; f++)
	{
		s32 mix = 0;
		// LFOs: tremolo 1 dB (42 units) at 3.7 Hz, vibrato +-7 cents at 6.1 Hz (triangles)
		s_nLfoTrem += 360353u; s_nLfoVib += 594096u;		// 3.7 / 6.1 Hz * 2^32 / 44100
		u32 tt = s_nLfoTrem >> 16; u32 nTrem = ((tt < 32768 ? tt : 65535 - tt) * 42) >> 15;
		u32 vt = s_nLfoVib >> 16; s32 nVib = ((s32) (vt < 32768 ? vt : 65535 - vt) >> 6) - 256;
		nVib = nVib * 7 / 17;					// +-256 = +-0.39 % ~ +-7 cents
		for (int i = 0; i < SND_VOICES; i++)
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
		s32 l = mix, r = mix;
		if (s_nStreamRd != s_nStreamWr)				// + the PCM stream
		{
			l += s_Stream[s_nStreamRd * 2]; r += s_Stream[s_nStreamRd * 2 + 1];
			s_nStreamRd = (s_nStreamRd + 1) % SND_STREAM;
		}
		if (l > 32767) l = 32767; else if (l < -32768) l = -32768;
		if (r > 32767) r = 32767; else if (r < -32768) r = -32768;
		pOut[f * 2] = (s16) l; pOut[f * 2 + 1] = (s16) r;
	}
}

#ifndef SOUND_HOST_TEST
// ---- the device -------------------------------------------------------------------------------
#ifdef ARM_ALLOW_MULTI_CORE
static s16 s_Ahead[SND_AHEAD][SND_FRAMES * 2];
static volatile unsigned s_nAheadRd = 0, s_nAheadWr = 0;	// chunk counters
#endif

class COnyxSoundDevice : public CPWMSoundBaseDevice
{
public:
	COnyxSoundDevice (void) : CPWMSoundBaseDevice (CInterruptSystem::Get (), SND_RATE, SND_CHUNK) {}
protected:
	unsigned GetChunk (u32 *pBuffer, unsigned nChunkSize) override
	{
		static s16 Tmp[SND_FRAMES * 2];
		unsigned nFrames = nChunkSize / 2;
		if (nFrames > SND_FRAMES) nFrames = SND_FRAMES;
		const s16 *pSrc = Tmp;
#ifdef ARM_ALLOW_MULTI_CORE
		if (s_nAheadRd != s_nAheadWr) { pSrc = s_Ahead[s_nAheadRd % SND_AHEAD]; DataMemBarrier (); s_nAheadRd++; }
		else for (unsigned i = 0; i < nFrames * 2; i++) Tmp[i] = 0;	// core 1 is late
		asm volatile ("sev");
#else
		s_Lock.Acquire ();
		Render (Tmp, nFrames);
		s_Lock.Release ();
#endif
		u32 nRange = (u32) GetRangeMax ();
		for (unsigned i = 0; i < nFrames * 2; i++)
			pBuffer[i] = (u32) (((u64) ((s32) pSrc[i] + 32768) * nRange) >> 16);
		for (unsigned i = nFrames * 2; i < nChunkSize; i++) pBuffer[i] = nRange / 2;
		return nChunkSize;
	}
};

static COnyxSoundDevice *s_pDevice = 0;

#ifdef ARM_ALLOW_MULTI_CORE
// Core 1: wait until the audio is started, then keep the ring SND_AHEAD chunks ahead.
void SoundCoreMain (void)
{
	for (;;)
	{
		if (!s_bRunning || s_nAheadWr - s_nAheadRd >= SND_AHEAD) { asm volatile ("wfe"); continue; }
		s_Lock.Acquire ();
		Render (s_Ahead[s_nAheadWr % SND_AHEAD], SND_FRAMES);
		s_Lock.Release ();
		DataMemBarrier ();
		s_nAheadWr++;
	}
}
#endif

// Start the device on first use.
static boolean EnsureDevice (void)
{
	if (s_pDevice != 0) return s_bRunning;
	if (!s_bTables) BuildTables ();
	s_pDevice = new COnyxSoundDevice;
	if (s_pDevice == 0) return FALSE;
#ifdef ARM_ALLOW_MULTI_CORE
	s_bRunning = TRUE;					// core 1 starts rendering
	asm volatile ("sev");
	for (unsigned n = 0; n < 1000000 && s_nAheadWr - s_nAheadRd < 2; n++) {}	// a little ahead
#endif
	if (!s_pDevice->Start ())
	{
		CLogger::Get ()->Write (From, LogError, "cannot start the PWM audio");
		s_bRunning = FALSE;
		return FALSE;
	}
	s_bRunning = TRUE;
	CLogger::Get ()->Write (From, LogNotice, "PWM audio started (%u Hz, %u-frame chunks%s)", SND_RATE, SND_FRAMES,
#ifdef ARM_ALLOW_MULTI_CORE
				" rendered on core 1"
#else
				""
#endif
				);
	return TRUE;
}

#else
static boolean EnsureDevice (void) { if (!s_bTables) BuildTables (); s_bRunning = TRUE; return TRUE; }
#endif

static void KeyOff (TVoice &v)
{
	v.bOn = FALSE;
	if (v.nWave == WAVE_FM) { for (int k = 0; k < 2; k++) if (v.Op[k].nStage != EG_OFF) v.Op[k].nStage = EG_RELEASE; }
	else v.nTarget = 0;
}

static void SilenceLocked (void)
{
	for (int i = 0; i < SND_VOICES; i++) { KeyOff (s_Voice[i]); s_Voice[i].nTarget = 0; s_Voice[i].Op[0].nStage = s_Voice[i].Op[1].nStage = EG_OFF; }
	s_nStreamRd = s_nStreamWr = 0;
}

// ---- the API -----------------------------------------------------------------------------------------
int SoundAcquire (unsigned nPid)
{
	if (nPid == 0) return 0;
	if (!EnsureDevice ()) return -1;
	s_Lock.Acquire ();
	int r = 0;
	if (s_nOwner == 0 || s_nOwner == nPid) { s_nOwner = nPid; r = 1; }
	s_Lock.Release ();
	return r;
}

void SoundRelease (unsigned nPid)
{
	s_Lock.Acquire ();
	if (s_nOwner == nPid && nPid != 0) { SilenceLocked (); s_nOwner = 0; }
	s_Lock.Release ();
}

void SoundOnProcessGone (unsigned nPid) { SoundRelease (nPid); }

int SoundStart (unsigned nPid, int nVoice, unsigned nMilliHz, int nWave, int nVolume)
{
	if (nVoice < 0 || nVoice >= SND_VOICES) return -1;
	if (nVolume < 0) nVolume = 0;
	if (nVolume > 255) nVolume = 255;
	if (nMilliHz > 20000000) nMilliHz = 20000000;		// 20 kHz
	s_Lock.Acquire ();
	if (s_nOwner != nPid || nPid == 0) { s_Lock.Release (); return -1; }
	TVoice &v = s_Voice[nVoice];
	v.nInc = (u32) (((u64) nMilliHz << 32) / (1000ull * SND_RATE));
	if (nWave == WAVE_FM && v.bHasFM)			// key on: both envelopes restart
	{
		if (v.nWave != WAVE_FM) { v.Op[0].nAtt = v.Op[1].nAtt = ATT_MAX << 16; }
		v.nWave = WAVE_FM;
		v.nGain = v.nTarget = (u32) nVolume * 257;
		for (int k = 0; k < 2; k++) { v.Op[k].nStage = EG_ATTACK; v.Op[k].nPhase = 0; if (v.Op[k].nAtt > (ATT_MAX << 16)) v.Op[k].nAtt = ATT_MAX << 16; }
		v.nFb1 = v.nFb2 = 0;
		v.bOn = TRUE;
		s_Lock.Release ();
		return 0;
	}
	if (v.nWave == WAVE_FM) { v.Op[0].nStage = v.Op[1].nStage = EG_OFF; v.nGain = 0; }
	v.nWave = nWave >= WAVE_SQUARE && nWave <= WAVE_NOISE ? nWave : WAVE_SQUARE;
	v.nTarget = (u32) nVolume * 257;
	if (!v.bOn && v.nGain == 0) v.nPhase = 0;
	v.bOn = TRUE;
	s_Lock.Release ();
	return 0;
}

int SoundStop (unsigned nPid, int nVoice)
{
	s_Lock.Acquire ();
	if (s_nOwner != nPid || nPid == 0) { s_Lock.Release (); return -1; }
	for (int i = 0; i < SND_VOICES; i++)
		if (nVoice < 0 || nVoice == i) KeyOff (s_Voice[i]);
	s_Lock.Release ();
	return 0;
}

// An FM instrument for a voice (then sound_start (voice, f, SOUND_FM, vol) plays it).
int SoundInstrument (unsigned nPid, int nVoice, const kapi_fm_instrument *pIns)
{
	if (nVoice < 0 || nVoice >= SND_VOICES || pIns == 0) return -1;
	kapi_fm_instrument In = *pIns;				// (copy from the app first)
	s_Lock.Acquire ();
	if (s_nOwner != nPid || nPid == 0) { s_Lock.Release (); return -1; }
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
	v.bHasFM = TRUE;
	s_Lock.Release ();
	return 0;
}

int SoundWrite (unsigned nPid, const s16 *pFrames, unsigned nFrames)
{
	if (pFrames == 0) return -1;
	s_Lock.Acquire ();
	if (s_nOwner != nPid || nPid == 0) { s_Lock.Release (); return -1; }
	unsigned n = 0;
	while (n < nFrames)
	{
		unsigned next = (s_nStreamWr + 1) % SND_STREAM;
		if (next == s_nStreamRd) break;				// full
		s_Stream[s_nStreamWr * 2] = pFrames[n * 2];
		s_Stream[s_nStreamWr * 2 + 1] = pFrames[n * 2 + 1];
		s_nStreamWr = next; n++;
	}
	s_Lock.Release ();
	return (int) n;
}

int SoundStatus (unsigned *pRate, unsigned *pFree, unsigned *pOwner)
{
	s_Lock.Acquire ();
	unsigned used = (s_nStreamWr + SND_STREAM - s_nStreamRd) % SND_STREAM;
	if (pRate) *pRate = SND_RATE;
	if (pFree) *pFree = SND_STREAM - 1 - used;
	if (pOwner) *pOwner = s_nOwner;
	s_Lock.Release ();
	return s_bRunning ? 1 : 0;
}
