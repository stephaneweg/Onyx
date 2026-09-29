//
// meltysynth.h -- the SoundFont (SF2) synthesizer of Koton, the Onyx DAW: a C++ port of
// MeltySynth (C#, by Nobuaki Tanaka), from the user's vendored copy (which adds SF2 modulator
// de-duplication, note-on modulators on the attenuation and filter generators, per-voice glide
// and a configurable channel count). Same DSP, same maths, same defaults as that copy, so a song
// sounds the same as in the Windows app.
//
//   char err[128];
//   ms::SoundFont *sf = ms::soundfont_load (data, len, err, sizeof err);   (copies what it needs)
//   ms::SynthSettings st; st.sampleRate = 44100;
//   ms::Synthesizer *syn = new ms::Synthesizer (sf, st);                   (allocates everything)
//   if (!syn->ok ()) ...
//   syn->processMidiMessage (0, 0xC0, 48, 0);   syn->noteOn (0, 60, 100);
//   syn->render (left, right, frames);
//   delete syn; ms::soundfont_free (sf);          (the SoundFont must outlive its synthesizers)
//
// Real-time rules: render (), noteOn/Off (), processMidiMessage () and every other method except
// the constructor / destructor never allocate, lock or do I/O. No global mutable state: several
// synthesizers (on one or several SoundFonts) may coexist. One synthesizer is not thread-safe:
// the MIDI calls and render () must not run at the same time.
//
// Port notes: floats in the DSP, doubles only for the per-block envelope / LFO timing (as in
// MeltySynth). MIDI file playback (MidiFile*.cs) and the AudioRendererEx extras are not ported.
//
// ---------------------------------------------------------------------------------------------
// MIT License
//
// Copyright (c) 2021 Nobuaki Tanaka
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
// ---------------------------------------------------------------------------------------------
//
#ifndef _koton_meltysynth_h
#define _koton_meltysynth_h

#include <stddef.h>

namespace ms {

// ---- SoundFont -------------------------------------------------------------------------------

struct SoundFont;

// Parses an SF2 held in memory. Everything needed is copied (the caller may free its buffer as
// soon as this returns). On bad data: nullptr, and a message in err (if err && errcap > 0).
SoundFont *soundfont_load (const void *data, size_t len, char *err, size_t errcap);
void soundfont_free (SoundFont *sf);

const char *soundfont_name (const SoundFont *sf);			// the INAM bank name ("" if none)
int soundfont_preset_count (const SoundFont *sf);
const char *soundfont_preset_name (const SoundFont *sf, int i);	// "" if i is out of range
int soundfont_preset_bank (const SoundFont *sf, int i);		// -1 if i is out of range
int soundfont_preset_patch (const SoundFont *sf, int i);		// -1 if i is out of range

// ---- Synthesizer -----------------------------------------------------------------------------

struct SynthSettings
{
	int sampleRate = 44100;				// 16000 .. 192000
	int blockSize = 64;				// 8 .. 1024 (the internal processing block)
	int maxPolyphony = 64;				// 8 .. 256
	bool enableReverbAndChorus = true;
	int channelCount = 16;				// 16 .. 1024 (MIDI channels; 9 is percussion)
};

struct SynthState;					// private (see ms_internal.h)

class Synthesizer
{
public:
	Synthesizer (const SoundFont *sf, const SynthSettings &s);	// allocates everything here
	~Synthesizer ();
	bool ok () const;				// false: bad settings, no SoundFont or out of memory

	// A MIDI channel message: command = 0x80 note off, 0x90 note on, 0xB0 controller,
	// 0xC0 program change, 0xE0 pitch bend (the low nibble, if any, is ignored).
	void processMidiMessage (int channel, int command, int data1, int data2);
	void noteOn (int channel, int key, int velocity);
	// Glissando: the voice starts at the pitch glideFromKey (fractional keys allowed) and slides
	// linearly (in semitones) to key over glideSeconds. glideSeconds <= 0: a plain noteOn.
	void noteOnGlide (int channel, int key, int velocity, float glideFromKey, float glideSeconds);
	void noteOff (int channel, int key);
	void noteOffAll (bool immediate);			// immediate: no release tail
	void noteOffAllChannel (int channel, bool immediate);
	void resetAllControllers ();
	void resetAllControllersChannel (int channel);
	void reset ();					// voices, channels, reverb/chorus tails

	// Renders frames stereo samples (any count: processed internally in blockSize blocks).
	void render (float *left, float *right, int frames);

	float masterVolume;				// 0.5 by default (as MeltySynth)
	int activeVoiceCount () const;
	int sampleRate () const;
	int blockSize () const;
	int channelCount () const;

private:
	SynthState *st;
	Synthesizer (const Synthesizer &) = delete;
	Synthesizer &operator= (const Synthesizer &) = delete;
};

} // namespace ms

#endif
