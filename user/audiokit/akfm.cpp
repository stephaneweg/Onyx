//
// akfm.cpp -- AudioKit (audiokit.h): the FM synthesizer. It IS the kernel's (kernel/sys/sound.cpp: the
// 16 voices, the two-operator FM instruments, the plain waves), compiled here in its host mode -- one
// source, the same sound -- so that a program has it in user space: its voices played by AudioKit's
// player thread, mixed with the file and the MIDI notes, or rendered off line (ak_fm_render: a WAV
// export). The kernel keeps its own copy for the kapi's voices (kapi_sound_start: BASIC's SOUND and
// PLAY, the games, older programs): that interface is append-only and stays.
// Compiled into the library only (user/Makefile). Integer arithmetic.
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
#include <string.h>
#include <stdint.h>
#include "kapi.h"

// ---- the kernel's synthesizer, in user space ----
typedef int16_t s16; typedef uint16_t u16; typedef uint32_t u32; typedef int32_t s32;
typedef uint64_t u64; typedef int64_t s64; typedef uint8_t u8; typedef bool boolean;
#undef TRUE
#undef FALSE
#define TRUE true
#define FALSE false
#define IRQ_LEVEL 0
static volatile int s_fmLock;
struct CSpinLock { CSpinLock (int) {} void Acquire () { kapi_lock (&s_fmLock); } void Release () { kapi_unlock (&s_fmLock); } };
#define SND_RATE	44100
#define SND_VOICES	16
#define SOUND_HOST_TEST
#include "../../kernel/sys/sound.cpp"

#include "audiokit/audiokit.h"

#define FM_PID	1				// (the synthesizer's "owner": this process)
extern "C" void akplayer_start (void);		// (akcore.cpp)

static volatile int s_fmQuiet = 1000;		// passes rendered since the last note (the releases' tail)
static bool s_fmReady;
static volatile int s_fmLive = 1;			// 0: rendered by the program only (ak_fm_live)

static void fm_ready (void)
{
	if (!s_fmReady) { s_fmReady = true; SoundAcquire (FM_PID); }
}

extern "C" int akfm_active (void)			{ return s_fmLive && s_fmQuiet < 400; }
extern "C" void ak_fm_live (int on)			{ s_fmLive = on ? 1 : 0; }	// (~4.6 s after the last note)

extern "C" int ak_fm_instrument (int voice, const struct kapi_fm_instrument *ins)
{
	fm_ready ();
	return SoundInstrument (FM_PID, voice, ins);
}
extern "C" int ak_fm_start (int voice, unsigned milli_hz, int wave, int volume)
{
	fm_ready ();
	int r = SoundStart (FM_PID, voice, milli_hz, wave, volume);
	s_fmQuiet = 0;
	if (s_fmLive) akplayer_start ();
	return r;
}
extern "C" void ak_fm_stop (int voice)
{
	if (s_fmReady) SoundStop (FM_PID, voice);
}
// `frames` frames of the voices ADDED nowhere: written to out (the player mixes them; an export
// writes them to its file).
extern "C" void ak_fm_render (short *out, int frames)
{
	fm_ready ();
	while (frames > 0)
	{
		int n = frames > 1024 ? 1024 : frames;
		s_Lock.Acquire ();
		Render (out, (unsigned) n);
		s_Lock.Release ();
		out += 2 * n; frames -= n;
	}
	if (s_fmQuiet < 1000) s_fmQuiet = s_fmQuiet + 1;
}
