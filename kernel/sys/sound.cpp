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
#include <kern/sound.h>
#include <circle/sound/pwmsoundbasedevice.h>
#include <circle/interrupt.h>
#include <circle/spinlock.h>
#include <circle/synchronize.h>
#include <circle/logger.h>
#include <circle/new.h>

#define SND_CHUNK	2048				// words per GetChunk = 1024 stereo frames
#define SND_FRAMES	(SND_CHUNK / 2)
#define SND_STREAM	(SND_RATE / 2)			// PCM ring: 0.5 s of stereo frames
#define SND_AHEAD	4				// (multi-core) chunks rendered ahead

enum { WAVE_SQUARE = 0, WAVE_SINE, WAVE_TRIANGLE, WAVE_SAW, WAVE_NOISE };

struct TVoice
{
	boolean	bOn;					// key down
	u32	nPhase, nInc;				// 32-bit phase accumulator
	int	nWave;
	u32	nGain, nTarget;				// 0..65536 (envelope)
	u32	nNoise;					// LFSR state
	s32	nNoiseVal;
};

static TVoice   s_Voice[SND_VOICES];
static s16      s_Stream[SND_STREAM * 2];		// stereo ring
static unsigned s_nStreamRd = 0, s_nStreamWr = 0;	// frame indices (mod SND_STREAM)
static unsigned s_nOwner = 0;				// owning pid (0 = free)
static CSpinLock s_Lock (IRQ_LEVEL);
static s16      s_SineTab[256];
static boolean  s_bTables = FALSE;
static volatile boolean s_bRunning = FALSE;

static const char From[] = "sound";

// ---- the synthesizer ----------------------------------------------------------------------
static void BuildTables (void)
{
	// Sine by a 5th-order polynomial on the quarter wave (integer, error < 0.1 %).
	for (int i = 0; i < 256; i++)
	{
		int q = i & 63, quad = i >> 6;			// 64 steps per quarter
		int x = (quad & 1) ? 64 - q : q;		// 0..64 -> 0..pi/2
		// sin (x * pi / 128) * 32767 ~ Bhaskara I: 4x(180-x) / (40500 - x(180-x)), x in degrees
		int deg = x * 90 / 64;
		int num = 4 * deg * (180 - deg), den = 40500 - deg * (180 - deg);
		int v = (int) ((long) num * 32767 / den);
		s_SineTab[i] = (s16) (quad >= 2 ? -v : v);
	}
	for (int v = 0; v < SND_VOICES; v++) { s_Voice[v].bOn = FALSE; s_Voice[v].nGain = s_Voice[v].nTarget = 0; s_Voice[v].nNoise = 0xACE1u + v; s_Voice[v].nNoiseVal = 0; }
	s_bTables = TRUE;
}

static inline s32 WaveSample (TVoice &v)
{
	u32 p = v.nPhase;
	switch (v.nWave)
	{
	case WAVE_SINE:
	{
		int i = p >> 24, f = (p >> 16) & 0xFF;
		int a = s_SineTab[i], b = s_SineTab[(i + 1) & 255];
		return a + (((b - a) * f) >> 8);
	}
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
		for (int i = 0; i < SND_VOICES; i++)
		{
			TVoice &v = s_Voice[i];
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

static void SilenceLocked (void)
{
	for (int i = 0; i < SND_VOICES; i++) { s_Voice[i].bOn = FALSE; s_Voice[i].nTarget = 0; }
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
		if (nVoice < 0 || nVoice == i) { s_Voice[i].bOn = FALSE; s_Voice[i].nTarget = 0; }
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
