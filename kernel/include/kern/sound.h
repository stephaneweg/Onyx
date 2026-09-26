//
// sound.h -- the Onyx sound system (ABI v46): a small synthesizer + a PCM stream, mixed
// and played through Circle's PWM audio (the 3.5 mm jack), started on first use.
//
// One process at a time OWNS the output (SoundAcquire); only the owner can play, and the
// output is silenced and freed when it releases it or exits (SoundOnProcessGone).
//   * Voices (SND_VOICES): SoundStart (voice, milli-Hz, wave, volume) / SoundStop (voice) --
//     a note stays on until stopped (short attack / release ramps, no clicks).
//   * PCM stream: SoundWrite (s16 stereo frames at SND_RATE), non-blocking, into a ring
//     that is mixed with the voices (audio / MIDI players).
// The mixing runs in the PWM DMA interrupt (single core), or -- when Circle is built with
// ARM_ALLOW_MULTI_CORE -- on core 1, which renders chunks ahead into a ring that the
// interrupt only copies (see sys/sound.cpp).
//
#ifndef _kern_sound_h
#define _kern_sound_h

#include <circle/types.h>

#define SND_RATE	44100
#define SND_VOICES	16

int  SoundAcquire (unsigned nPid);			// 1 ok (or already ours), 0 busy, -1 no audio
void SoundRelease (unsigned nPid);
int  SoundStart (unsigned nPid, int nVoice, unsigned nMilliHz, int nWave, int nVolume);
int  SoundStop (unsigned nPid, int nVoice);		// nVoice -1 = all
struct kapi_fm_instrument;
int  SoundInstrument (unsigned nPid, int nVoice, const struct kapi_fm_instrument *pIns);
int  SoundWrite (unsigned nPid, const s16 *pFrames, unsigned nFrames);	// frames taken
int  SoundStatus (unsigned *pRate, unsigned *pFreeFrames, unsigned *pOwnerPid);
void SoundOnProcessGone (unsigned nPid);

#ifdef ARM_ALLOW_MULTI_CORE
void SoundCoreMain (void);				// core 1's loop (kernel.cpp starts it)
#endif

#endif
