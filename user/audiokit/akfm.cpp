//
// akfm.cpp -- AudioKit (audiokit.h): the FM synthesizer (fmsynth.h: the 16 voices, the two-operator FM
// instruments, the plain waves -- the kernel's until 2026-10-05, the system's only one now): its
// voices played by AudioKit's player thread, mixed with the file and the MIDI notes, or rendered off
// line (ak_fm_render: a WAV export, a program that mixes them itself -- Doom).
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
#include "fmsynth.h"
#include "audiokit.h"

extern "C" void akplayer_start (void);		// (akcore.cpp)

static volatile int s_fmLock;			// start / stop / instrument and render kept apart
static volatile int s_fmLive = 1;		// 0: rendered by the program only (ak_fm_live)

extern "C" int akfm_active (void)			{ return s_fmLive && fmsynth::active (); }
extern "C" void ak_fm_live (int on)			{ s_fmLive = on ? 1 : 0; }

extern "C" int ak_fm_instrument (int voice, const struct kapi_fm_instrument *ins)
{
	kapi_lock (&s_fmLock);
	int r = fmsynth::instrument (voice, ins);
	kapi_unlock (&s_fmLock);
	return r;
}
extern "C" int ak_fm_start (int voice, unsigned milli_hz, int wave, int volume)
{
	kapi_lock (&s_fmLock);
	int r = fmsynth::start (voice, milli_hz, wave, volume);
	kapi_unlock (&s_fmLock);
	if (s_fmLive) akplayer_start ();
	return r;
}
extern "C" void ak_fm_stop (int voice)
{
	kapi_lock (&s_fmLock);
	fmsynth::stop (voice);
	kapi_unlock (&s_fmLock);
}
// Everything silent at once, without the releases (a program that ends, a song stopped dead).
extern "C" void ak_fm_silence (void)
{
	kapi_lock (&s_fmLock);
	fmsynth::silence ();
	kapi_unlock (&s_fmLock);
}
// `frames` frames of the voices WRITTEN to out (the player mixes them; an export writes them to
// its file).
extern "C" void ak_fm_render (short *out, int frames)
{
	while (frames > 0)
	{
		int n = frames > 1024 ? 1024 : frames;
		kapi_lock (&s_fmLock);
		fmsynth::render (out, (unsigned) n);
		kapi_unlock (&s_fmLock);
		out += 2 * n; frames -= n;
	}
}
