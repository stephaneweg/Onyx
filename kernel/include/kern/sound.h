//
// sound.h -- the Onyx sound system (ABI v46): a small synthesizer + a PCM stream, mixed
// and played through Circle's PWM audio (the 3.5 mm jack), started on first use.
//
// One process at a time OWNS the output (SoundAcquire); only the owner can play, and the
// output is silenced and freed when it releases it or exits (SoundOnProcessGone).
//   * (The voices -- SoundStart / SoundStop / SoundInstrument -- left the kernel on 2026-10-05: the
//     synthesizer is AudioKit's, in user space: user/audiokit/fmsynth.h, ak_fm_*.)
//   * PCM stream: SoundWrite (s16 stereo frames at SND_RATE), non-blocking, into a ring
//     that is mixed with the mapped ring (audio / MIDI players, AudioKit).
// The mixing runs in the PWM DMA interrupt (single core), or -- when Circle is built with
// ARM_ALLOW_MULTI_CORE -- on core 1, which renders chunks ahead into a ring that the
// interrupt only copies (see sys/sound.cpp).
//   * Low latency (ABI v68): SoundConfig (chunk frames, chunks ahead) for the owner, and
//     SoundRing: a PCM ring in a page the owner maps and fills from anywhere (an app core);
//     both back to the defaults / off when the owner releases the output or dies.
//
#ifndef _kern_sound_h
#define _kern_sound_h

#include <circle/types.h>

#define SND_RATE	44100
#define SND_VOICES	16

int  SoundAcquire (unsigned nPid);			// 1 ok (or already ours), 0 busy, -1 no audio
void SoundRelease (unsigned nPid);
int  SoundWrite (unsigned nPid, const s16 *pFrames, unsigned nFrames);	// frames taken
int  SoundStatus (unsigned *pRate, unsigned *pFreeFrames, unsigned *pOwnerPid);
void SoundOnProcessGone (unsigned nPid);
int  SoundVolume (int nVolume, int nMute);		// 0..10, mute 0 / 1 (-1: keep) -> volume | 0x100 if muted
int  SoundOutput (int nOut);				// (v84) KAPI_SND_OUT_* (-1: ask) -> running | asked << 8 | present << 16
void SoundPoll (void);					// every 100 ms (the kernel's input task): the USB output follows its device
int  SoundConfig (unsigned nPid, int nChunkFrames, int nAhead);	// -> latency in frames, -1 not the owner
#define SND_RING_PAGE	0x10000				// the mapped ring's page (one 64 KB app page)
struct kapi_sound_ring;
struct kapi_sound_ring *SoundRing (unsigned nPid);	// the ring (kernel address), 0 not the owner

#ifdef ARM_ALLOW_MULTI_CORE
void SoundCoreMain (void);				// core 1's loop (kernel.cpp starts it)
#endif

#endif
