//
// synthesizer.cpp -- Koton's MeltySynth port: the synthesizer (Synthesizer.cs,
// SynthesizerSettings.cs), the voice pool (VoiceCollection.cs) and the MIDI channel state
// (Channel.cs).
//
// The constructor allocates everything (channels, voices and their blocks, the mix / effect
// buffers, the reverb and chorus lines, the preset lookup table); after that, no method
// allocates, locks or does I/O, so render () and the MIDI calls can run on a dedicated audio core.
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
#include <stdlib.h>
#include <string.h>
#include "ms_internal.h"

namespace ms {

// ---- Channel.cs ------------------------------------------------------------------------------

void Channel::reset ()
{
	bankNumber = isPercussionChannel ? 128 : 0;
	patchNumber = 0;

	modulation = 0;
	volume = 100 << 7;
	pan = 64 << 7;
	expression = 127 << 7;
	holdPedal = false;

	reverbSend = 40;
	chorusSend = 0;

	rpn = -1;
	pitchBendRange = 2 << 7;
	coarseTune = 0;
	fineTune = 8192;

	pitchBendValue = 0;
	lastDataType = DATA_NONE;

	memset (controllers, 0, sizeof controllers);
	controllers[2] = DEFAULT_BREATH_CONTROLLER;		// CC2
}

void Channel::resetAllControllers ()
{
	modulation = 0;
	expression = 127 << 7;
	holdPedal = false;
	rpn = -1;
	pitchBendValue = 0;
}

void Channel::dataEntryCoarse (int v)
{
	if (lastDataType != DATA_RPN) return;
	switch (rpn)
	{
	case 0: pitchBendRange = (int16_t) ((pitchBendRange & 0x7F) | (v << 7)); break;
	case 1: fineTune = (int16_t) ((fineTune & 0x7F) | (v << 7)); break;
	case 2: coarseTune = (int16_t) (v - 64); break;
	}
}

void Channel::dataEntryFine (int v)
{
	if (lastDataType != DATA_RPN) return;
	switch (rpn)
	{
	case 0: pitchBendRange = (int16_t) ((pitchBendRange & 0xFF80) | v); break;
	case 1: fineTune = (int16_t) ((fineTune & 0xFF80) | v); break;
	}
}

// ---- Construction ----------------------------------------------------------------------------

namespace {

const int PERCUSSION_CHANNEL = 9;

int comparePresetIds (const void *a, const void *b)
{
	const PresetId *x = (const PresetId *) a, *y = (const PresetId *) b;
	if (x->id != y->id) return x->id < y->id ? -1 : 1;
	// Same id: the first preset in the file wins (Dictionary.Add skipped the later ones).
	return x->preset < y->preset ? -1 : x->preset > y->preset ? 1 : 0;
}

void destroyState (SynthState *s)
{
	if (!s) return;
	free (s->presetLookup);
	free (s->channels);
	free (s->voicePool);
	free (s->voices);
	free (s->voiceBlocks);
	free (s->blockLeft);
	free (s->blockRight);
	s->reverb.free_ ();
	free (s->reverbInput);
	free (s->reverbOutputLeft);
	free (s->reverbOutputRight);
	s->chorus.free_ ();
	free (s->chorusInputLeft);
	free (s->chorusInputRight);
	free (s->chorusOutputLeft);
	free (s->chorusOutputRight);
	free (s);
}

float *allocBlock (int n) { return (float *) calloc (n, sizeof (float)); }

const Preset *findPreset (const SynthState *s, int id)
{
	int lo = 0, hi = s->presetLookupCount - 1;
	while (lo <= hi)
	{
		int mid = (lo + hi) >> 1;
		int v = s->presetLookup[mid].id;
		if (v == id) return s->presetLookup[mid].preset;
		if (v < id) lo = mid + 1; else hi = mid - 1;
	}
	return nullptr;
}

// SynthState construction; false on bad settings or out of memory (the caller destroys).
bool buildState (SynthState *s, const SoundFont *sf, const SynthSettings &set)
{
	if (!(16000 <= set.sampleRate && set.sampleRate <= 192000)) return false;
	if (!(8 <= set.blockSize && set.blockSize <= 1024)) return false;
	if (!(8 <= set.maxPolyphony && set.maxPolyphony <= 256)) return false;
	if (!(16 <= set.channelCount && set.channelCount <= 1024)) return false;
	if (!sf || sf->presetCount <= 0) return false;

	s->ctx.sampleRate = set.sampleRate;
	s->ctx.blockSize = set.blockSize;
	s->ctx.minimumVoiceDuration = set.sampleRate / 500;
	s->ctx.sf = sf;
	s->maximumPolyphony = set.maxPolyphony;
	s->enableReverbAndChorus = set.enableReverbAndChorus;
	s->channelCount = set.channelCount;

	// The preset lookup: (bank << 16) | patch -> preset, sorted for a binary search.
	s->presetLookup = (PresetId *) malloc (sf->presetCount * sizeof (PresetId));
	if (!s->presetLookup) return false;
	int minPresetId = 0x7FFFFFFF;
	for (int i = 0; i < sf->presetCount; i++)
	{
		const Preset *p = &sf->presets[i];
		int id = (p->bankNumber << 16) | p->patchNumber;
		s->presetLookup[i].id = id;
		s->presetLookup[i].preset = p;
		// The preset with the minimum id is the default (the piano, for a GM font).
		if (id < minPresetId) { s->defaultPreset = p; minPresetId = id; }
	}
	qsort (s->presetLookup, sf->presetCount, sizeof (PresetId), comparePresetIds);
	int n = 0;
	for (int i = 0; i < sf->presetCount; i++)
		if (n == 0 || s->presetLookup[n - 1].id != s->presetLookup[i].id) s->presetLookup[n++] = s->presetLookup[i];
	s->presetLookupCount = n;

	s->channels = (Channel *) calloc (s->channelCount, sizeof (Channel));
	if (!s->channels) return false;
	for (int i = 0; i < s->channelCount; i++) s->channels[i].init (i == PERCUSSION_CHANNEL);
	s->ctx.channels = s->channels;

	int bs = set.blockSize;
	s->voicePool = (Voice *) calloc (s->maximumPolyphony, sizeof (Voice));
	s->voices = (Voice **) calloc (s->maximumPolyphony, sizeof (Voice *));
	s->voiceBlocks = allocBlock (s->maximumPolyphony * bs);
	if (!s->voicePool || !s->voices || !s->voiceBlocks) return false;
	for (int i = 0; i < s->maximumPolyphony; i++)
	{
		s->voicePool[i].init (&s->ctx, s->voiceBlocks + i * bs);
		s->voices[i] = &s->voicePool[i];
	}
	s->activeVoiceCount = 0;

	s->blockLeft = allocBlock (bs);
	s->blockRight = allocBlock (bs);
	if (!s->blockLeft || !s->blockRight) return false;
	s->inverseBlockSize = 1.0f / bs;
	s->blockRead = bs;

	if (s->enableReverbAndChorus)
	{
		if (!s->reverb.init (set.sampleRate)) return false;
		s->reverbInput = allocBlock (bs);
		s->reverbOutputLeft = allocBlock (bs);
		s->reverbOutputRight = allocBlock (bs);
		if (!s->chorus.init (set.sampleRate, 0.002, 0.0019, 0.4)) return false;
		s->chorusInputLeft = allocBlock (bs);
		s->chorusInputRight = allocBlock (bs);
		s->chorusOutputLeft = allocBlock (bs);
		s->chorusOutputRight = allocBlock (bs);
		if (!s->reverbInput || !s->reverbOutputLeft || !s->reverbOutputRight || !s->chorusInputLeft
			|| !s->chorusInputRight || !s->chorusOutputLeft || !s->chorusOutputRight) return false;
	}
	return true;
}

// ---- VoiceCollection.cs ----------------------------------------------------------------------

Voice *requestNewVoice (SynthState *s, const InstrumentRegion *region, int channel)
{
	// An exclusive class (e.g. hi-hats): reuse the voice of the same class on this channel.
	int exclusiveClass = region->exclusiveClass ();
	if (exclusiveClass != 0)
	{
		for (int i = 0; i < s->activeVoiceCount; i++)
		{
			Voice *v = s->voices[i];
			if (v->exclusiveClass == exclusiveClass && v->channel == channel) return v;
		}
	}

	// A free one, if any.
	if (s->activeVoiceCount < s->maximumPolyphony) return s->voices[s->activeVoiceCount++];

	// Too many active voices: steal the one with the lowest priority (the older one on a tie).
	Voice *candidate = nullptr;
	float lowestPriority = 3.402823466e+38f;
	for (int i = 0; i < s->activeVoiceCount; i++)
	{
		Voice *v = s->voices[i];
		float priority = v->priority ();
		if (priority < lowestPriority)
		{
			lowestPriority = priority;
			candidate = v;
		}
		else if (priority == lowestPriority && candidate && v->voiceLength > candidate->voiceLength) candidate = v;
	}
	return candidate;
}

void processVoices (SynthState *s)
{
	int i = 0;
	while (i < s->activeVoiceCount)
	{
		if (s->voices[i]->process ()) i++;
		else
		{
			// A finished voice: swap it past the end of the active list.
			s->activeVoiceCount--;
			Voice *tmp = s->voices[i];
			s->voices[i] = s->voices[s->activeVoiceCount];
			s->voices[s->activeVoiceCount] = tmp;
		}
	}
}

// ---- Mixing (ArrayMath.cs + Synthesizer.WriteBlock) ------------------------------------------

inline void multiplyAdd (float a, const float *x, float *dst, int n)
{
	for (int i = 0; i < n; i++) dst[i] += a * x[i];
}

inline void multiplyAddRamp (float a, float step, const float *x, float *dst, int n)
{
	for (int i = 0; i < n; i++) { dst[i] += a * x[i]; a += step; }
}

inline void writeBlock (const SynthState *s, float previousGain, float currentGain, const float *source, float *destination)
{
	if (maxf (previousGain, currentGain) < NON_AUDIBLE) return;
	int n = s->ctx.blockSize;
	if (fabsf (currentGain - previousGain) < 1.0E-3f) multiplyAdd (currentGain, source, destination, n);
	else
	{
		float step = s->inverseBlockSize * (currentGain - previousGain);
		multiplyAddRamp (previousGain, step, source, destination, n);
	}
}

void renderBlock (SynthState *s, float masterVolume)
{
	processVoices (s);

	int n = s->ctx.blockSize;
	memset (s->blockLeft, 0, n * sizeof (float));
	memset (s->blockRight, 0, n * sizeof (float));
	for (int i = 0; i < s->activeVoiceCount; i++)
	{
		const Voice *v = s->voices[i];
		writeBlock (s, masterVolume * v->previousMixGainLeft, masterVolume * v->currentMixGainLeft, v->block, s->blockLeft);
		writeBlock (s, masterVolume * v->previousMixGainRight, masterVolume * v->currentMixGainRight, v->block, s->blockRight);
	}

	if (s->enableReverbAndChorus)
	{
		memset (s->chorusInputLeft, 0, n * sizeof (float));
		memset (s->chorusInputRight, 0, n * sizeof (float));
		for (int i = 0; i < s->activeVoiceCount; i++)
		{
			const Voice *v = s->voices[i];
			writeBlock (s, v->previousChorusSend * v->previousMixGainLeft, v->currentChorusSend * v->currentMixGainLeft, v->block, s->chorusInputLeft);
			writeBlock (s, v->previousChorusSend * v->previousMixGainRight, v->currentChorusSend * v->currentMixGainRight, v->block, s->chorusInputRight);
		}
		s->chorus.process (s->chorusInputLeft, s->chorusInputRight, s->chorusOutputLeft, s->chorusOutputRight, n);
		multiplyAdd (masterVolume, s->chorusOutputLeft, s->blockLeft, n);
		multiplyAdd (masterVolume, s->chorusOutputRight, s->blockRight, n);

		memset (s->reverbInput, 0, n * sizeof (float));
		float g = s->reverb.inputGain ();
		for (int i = 0; i < s->activeVoiceCount; i++)
		{
			const Voice *v = s->voices[i];
			float previousGain = g * v->previousReverbSend * (v->previousMixGainLeft + v->previousMixGainRight);
			float currentGain = g * v->currentReverbSend * (v->currentMixGainLeft + v->currentMixGainRight);
			writeBlock (s, previousGain, currentGain, v->block, s->reverbInput);
		}
		s->reverb.process (s->reverbInput, s->reverbOutputLeft, s->reverbOutputRight, n);
		multiplyAdd (masterVolume, s->reverbOutputLeft, s->blockLeft, n);
		multiplyAdd (masterVolume, s->reverbOutputRight, s->blockRight, n);
	}
}

void noteOnImpl (SynthState *s, int channel, int key, int velocity, float glideFromKey, float glideDurSec)
{
	if (!(0 <= channel && channel < s->channelCount)) return;
	const Channel &ch = s->channels[channel];

	int presetId = (ch.bankNumber << 16) | ch.patchNumber;
	const Preset *preset = findPreset (s, presetId);
	if (!preset)
	{
		// Fall back to the GM sound set: the same patch in bank 0; for drums (bank >= 128), the
		// standard set (128:0).
		int gmPresetId = ch.bankNumber < 128 ? ch.patchNumber : (128 << 16);
		preset = findPreset (s, gmPresetId);
		if (!preset) preset = s->defaultPreset;		// none: the default one
	}

	bool glide = glideDurSec > 0 && fabsf (glideFromKey - key) > 0.001f;
	for (int i = 0; i < preset->regionCount; i++)
	{
		const PresetRegion &pr = preset->regions[i];
		if (!pr.contains (key, velocity)) continue;
		const Instrument *inst = pr.instrument;
		for (int k = 0; k < inst->regionCount; k++)
		{
			const InstrumentRegion &ir = inst->regions[k];
			if (!ir.contains (key, velocity)) continue;
			RegionPair pair = { &pr, &ir };
			Voice *v = requestNewVoice (s, &ir, channel);
			if (!v) continue;
			if (glide) v->startWithGlide (pair, channel, key, velocity, glideFromKey, glideDurSec);
			else v->start (pair, channel, key, velocity);
		}
	}
}

} // namespace

// ---- The public class ------------------------------------------------------------------------

Synthesizer::Synthesizer (const SoundFont *sf, const SynthSettings &set) : masterVolume (0.5f), st (nullptr)
{
	SynthState *s = (SynthState *) calloc (1, sizeof (SynthState));
	if (!s) return;
	if (!buildState (s, sf, set)) { destroyState (s); return; }
	st = s;
}

Synthesizer::~Synthesizer ()
{
	destroyState (st);
}

bool Synthesizer::ok () const { return st != nullptr; }
int Synthesizer::activeVoiceCount () const { return st ? st->activeVoiceCount : 0; }
int Synthesizer::sampleRate () const { return st ? st->ctx.sampleRate : 0; }
int Synthesizer::blockSize () const { return st ? st->ctx.blockSize : 0; }
int Synthesizer::channelCount () const { return st ? st->channelCount : 0; }

void Synthesizer::processMidiMessage (int channel, int command, int data1, int data2)
{
	if (!st || !(0 <= channel && channel < st->channelCount)) return;
	Channel &ch = st->channels[channel];

	switch (command & 0xF0)
	{
	case 0x80:	// Note Off
		noteOff (channel, data1);
		break;

	case 0x90:	// Note On
		noteOn (channel, data1, data2);
		break;

	case 0xB0:	// Controller
		ch.setController (data1, data2);		// the raw value, for the SF2 modulators
		switch (data1)
		{
		case 0x00: ch.setBank (data2); break;			// Bank Select (MSB)
		case 0x01: ch.setModulationCoarse (data2); break;
		case 0x21: ch.setModulationFine (data2); break;
		case 0x06: ch.dataEntryCoarse (data2); break;
		case 0x26: ch.dataEntryFine (data2); break;
		case 0x07: ch.setVolumeCoarse (data2); break;
		case 0x27: ch.setVolumeFine (data2); break;
		case 0x0A: ch.setPanCoarse (data2); break;
		case 0x2A: ch.setPanFine (data2); break;
		case 0x0B: ch.setExpressionCoarse (data2); break;
		case 0x2B: ch.setExpressionFine (data2); break;
		case 0x40: ch.setHoldPedal (data2); break;			// Sustain
		case 0x5B: ch.setReverbSend (data2); break;
		case 0x5D: ch.setChorusSend (data2); break;
		case 0x63: ch.setNrpnCoarse (data2); break;
		case 0x62: ch.setNrpnFine (data2); break;
		case 0x65: ch.setRpnCoarse (data2); break;
		case 0x64: ch.setRpnFine (data2); break;
		case 0x78: noteOffAllChannel (channel, true); break;	// All Sound Off
		case 0x79: resetAllControllersChannel (channel); break;
		case 0x7B: noteOffAllChannel (channel, false); break;	// All Notes Off
		}
		break;

	case 0xC0:	// Program Change
		ch.setPatch (data1);
		break;

	case 0xE0:	// Pitch Bend
		ch.setPitchBend (data1, data2);
		break;
	}
}

void Synthesizer::noteOff (int channel, int key)
{
	if (!st || !(0 <= channel && channel < st->channelCount)) return;
	for (int i = 0; i < st->activeVoiceCount; i++)
	{
		Voice *v = st->voices[i];
		if (v->channel == channel && v->key == key) v->end ();
	}
}

void Synthesizer::noteOn (int channel, int key, int velocity)
{
	noteOnGlide (channel, key, velocity, (float) key, 0.0f);
}

void Synthesizer::noteOnGlide (int channel, int key, int velocity, float glideFromKey, float glideSeconds)
{
	if (!st) return;
	if (velocity == 0) { noteOff (channel, key); return; }
	noteOnImpl (st, channel, key, velocity, glideFromKey, glideSeconds);
}

void Synthesizer::noteOffAll (bool immediate)
{
	if (!st) return;
	if (immediate) st->activeVoiceCount = 0;
	else for (int i = 0; i < st->activeVoiceCount; i++) st->voices[i]->end ();
}

void Synthesizer::noteOffAllChannel (int channel, bool immediate)
{
	if (!st) return;
	for (int i = 0; i < st->activeVoiceCount; i++)
	{
		Voice *v = st->voices[i];
		if (v->channel != channel) continue;
		if (immediate) v->kill ();
		else v->end ();
	}
}

void Synthesizer::resetAllControllers ()
{
	if (!st) return;
	for (int i = 0; i < st->channelCount; i++) st->channels[i].resetAllControllers ();
}

void Synthesizer::resetAllControllersChannel (int channel)
{
	if (!st || !(0 <= channel && channel < st->channelCount)) return;
	st->channels[channel].resetAllControllers ();
}

void Synthesizer::reset ()
{
	if (!st) return;
	st->activeVoiceCount = 0;
	for (int i = 0; i < st->channelCount; i++) st->channels[i].reset ();
	if (st->enableReverbAndChorus)
	{
		st->reverb.mute ();
		st->chorus.mute ();
	}
	st->blockRead = st->ctx.blockSize;
}

void Synthesizer::render (float *left, float *right, int frames)
{
	if (frames <= 0) return;
	if (!st)
	{
		memset (left, 0, frames * sizeof (float));
		memset (right, 0, frames * sizeof (float));
		return;
	}
	int bs = st->ctx.blockSize;
	int wrote = 0;
	while (wrote < frames)
	{
		if (st->blockRead == bs)
		{
			renderBlock (st, masterVolume);
			st->blockRead = 0;
		}
		int srcRem = bs - st->blockRead;
		int dstRem = frames - wrote;
		int rem = srcRem < dstRem ? srcRem : dstRem;
		memcpy (left + wrote, st->blockLeft + st->blockRead, rem * sizeof (float));
		memcpy (right + wrote, st->blockRight + st->blockRead, rem * sizeof (float));
		st->blockRead += rem;
		wrote += rem;
	}
}

} // namespace ms
