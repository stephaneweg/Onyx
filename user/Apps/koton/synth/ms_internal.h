//
// ms_internal.h -- the private structures of Koton's MeltySynth port (see meltysynth.h): the
// parsed SoundFont (samples, instruments, presets and their regions, modulators), the generator
// accessors (InstrumentRegion.cs / PresetRegion.cs / RegionPair.cs), SoundFontMath, the MIDI
// channel state (Channel.cs), the voice building blocks (Oscillator, BiQuadFilter, Lfo, the two
// envelopes, Voice), the effects (Reverb, Chorus) and the synthesizer state.
//
// Only included by the synth's own .cpp files. Plain structs with explicit init () functions: no
// constructors that allocate, no exceptions, no STL.
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
#ifndef _koton_ms_internal_h
#define _koton_ms_internal_h

#include <stdint.h>
#include <stddef.h>
#include <math.h>
#include "meltysynth.h"

namespace ms {

// ---- GeneratorType.cs ------------------------------------------------------------------------

enum : int
{
	G_StartAddressOffset = 0,
	G_EndAddressOffset = 1,
	G_StartLoopAddressOffset = 2,
	G_EndLoopAddressOffset = 3,
	G_StartAddressCoarseOffset = 4,
	G_ModulationLfoToPitch = 5,
	G_VibratoLfoToPitch = 6,
	G_ModulationEnvelopeToPitch = 7,
	G_InitialFilterCutoffFrequency = 8,
	G_InitialFilterQ = 9,
	G_ModulationLfoToFilterCutoffFrequency = 10,
	G_ModulationEnvelopeToFilterCutoffFrequency = 11,
	G_EndAddressCoarseOffset = 12,
	G_ModulationLfoToVolume = 13,
	G_ChorusEffectsSend = 15,
	G_ReverbEffectsSend = 16,
	G_Pan = 17,
	G_DelayModulationLfo = 21,
	G_FrequencyModulationLfo = 22,
	G_DelayVibratoLfo = 23,
	G_FrequencyVibratoLfo = 24,
	G_DelayModulationEnvelope = 25,
	G_AttackModulationEnvelope = 26,
	G_HoldModulationEnvelope = 27,
	G_DecayModulationEnvelope = 28,
	G_SustainModulationEnvelope = 29,
	G_ReleaseModulationEnvelope = 30,
	G_KeyNumberToModulationEnvelopeHold = 31,
	G_KeyNumberToModulationEnvelopeDecay = 32,
	G_DelayVolumeEnvelope = 33,
	G_AttackVolumeEnvelope = 34,
	G_HoldVolumeEnvelope = 35,
	G_DecayVolumeEnvelope = 36,
	G_SustainVolumeEnvelope = 37,
	G_ReleaseVolumeEnvelope = 38,
	G_KeyNumberToVolumeEnvelopeHold = 39,
	G_KeyNumberToVolumeEnvelopeDecay = 40,
	G_Instrument = 41,
	G_KeyRange = 43,
	G_VelocityRange = 44,
	G_StartLoopAddressCoarseOffset = 45,
	G_KeyNumber = 46,
	G_Velocity = 47,
	G_InitialAttenuation = 48,
	G_EndLoopAddressCoarseOffset = 50,
	G_CoarseTune = 51,
	G_FineTune = 52,
	G_SampleID = 53,
	G_SampleModes = 54,
	G_ScaleTuning = 56,
	G_ExclusiveClass = 57,
	G_OverridingRootKey = 58,
	G_Count = 61				// the size of a region's generator array
};

enum LoopMode { LOOP_NONE = 0, LOOP_CONTINUOUS = 1, LOOP_UNTIL_NOTE_OFF = 3 };

// ---- SoundFontMath.cs ------------------------------------------------------------------------

const float PI_F = 3.14159265358979323846f;
const float HALF_PI_F = PI_F / 2;
const float NON_AUDIBLE = 1.0E-3f;
const double LOG_NON_AUDIBLE = -6.907755278982137;	// Math.Log (1.0E-3)

inline float timecentsToSeconds (float x) { return powf (2.0f, (1.0f / 1200.0f) * x); }
inline float centsToHertz (float x) { return 8.176f * powf (2.0f, (1.0f / 1200.0f) * x); }
inline float centsToMultiplyingFactor (float x) { return powf (2.0f, (1.0f / 1200.0f) * x); }
inline float decibelsToLinear (float x) { return powf (10.0f, 0.05f * x); }
inline float linearToDecibels (float x) { return 20.0f * log10f (x); }
inline float keyNumberToMultiplyingFactor (int cents, int key) { return timecentsToSeconds ((float) (cents * (60 - key))); }
inline double expCutoff (double x) { return x < LOG_NON_AUDIBLE ? 0.0 : exp (x); }

inline float clampf (float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float maxf (float a, float b) { return a > b ? a : b; }

// ---- The parsed SoundFont --------------------------------------------------------------------

// Modulator.cs: an SF2 modulator (sfModList), source controller -> destination generator.
struct Modulator
{
	uint16_t sourceOper;
	uint16_t destinationOper;		// a generator index (bit 15 set: a link, unsupported)
	int16_t amount;
	uint16_t amountSourceOper;
	uint16_t transformOper;
};

struct SampleHeader
{
	char name[21];
	int start, end, startLoop, endLoop;
	int sampleRate;
	uint8_t originalPitch;
	int8_t pitchCorrection;
};

struct Instrument;

struct InstrumentRegion
{
	int16_t gs[G_Count];
	const SampleHeader *sample;
	const Modulator *mods;			// combined global + local (de-duplicated)
	int modCount;

	int g (int t) const { return gs[t]; }
	bool contains (int key, int vel) const
	{
		return keyRangeStart () <= key && key <= keyRangeEnd () && velRangeStart () <= vel && vel <= velRangeEnd ();
	}
	int keyRangeStart () const { return gs[G_KeyRange] & 0xFF; }
	int keyRangeEnd () const { return (gs[G_KeyRange] >> 8) & 0xFF; }
	int velRangeStart () const { return gs[G_VelocityRange] & 0xFF; }
	int velRangeEnd () const { return (gs[G_VelocityRange] >> 8) & 0xFF; }

	int startAddressOffset () const { return 32768 * gs[G_StartAddressCoarseOffset] + gs[G_StartAddressOffset]; }
	int endAddressOffset () const { return 32768 * gs[G_EndAddressCoarseOffset] + gs[G_EndAddressOffset]; }
	int startLoopAddressOffset () const { return 32768 * gs[G_StartLoopAddressCoarseOffset] + gs[G_StartLoopAddressOffset]; }
	int endLoopAddressOffset () const { return 32768 * gs[G_EndLoopAddressCoarseOffset] + gs[G_EndLoopAddressOffset]; }
	int sampleStart () const { return sample->start + startAddressOffset (); }
	int sampleEnd () const { return sample->end + endAddressOffset (); }
	int sampleStartLoop () const { return sample->startLoop + startLoopAddressOffset (); }
	int sampleEndLoop () const { return sample->endLoop + endLoopAddressOffset (); }
	int sampleModes () const { return gs[G_SampleModes] != 2 ? (int) gs[G_SampleModes] : (int) LOOP_NONE; }
	int exclusiveClass () const { return gs[G_ExclusiveClass]; }
	int rootKey () const { return gs[G_OverridingRootKey] != -1 ? gs[G_OverridingRootKey] : sample->originalPitch; }
};

struct Instrument
{
	char name[21];
	InstrumentRegion *regions;
	int regionCount;
};

struct PresetRegion
{
	int16_t gs[G_Count];
	const Instrument *instrument;
	const Modulator *mods;
	int modCount;

	bool contains (int key, int vel) const
	{
		int k0 = gs[G_KeyRange] & 0xFF, k1 = (gs[G_KeyRange] >> 8) & 0xFF;
		int v0 = gs[G_VelocityRange] & 0xFF, v1 = (gs[G_VelocityRange] >> 8) & 0xFF;
		return k0 <= key && key <= k1 && v0 <= vel && vel <= v1;
	}
};

struct Preset
{
	char name[21];
	int patchNumber;
	int bankNumber;
	PresetRegion *regions;
	int regionCount;
};

struct SoundFont
{
	char bankName[256];
	int16_t *wave;				// the whole smpl chunk (16-bit mono samples)
	int waveLength;
	SampleHeader *samples;
	int sampleCount;
	Instrument *instruments;
	int instrumentCount;
	InstrumentRegion *instrumentRegions;	// the pool behind every Instrument::regions
	Preset *presets;
	int presetCount;
	PresetRegion *presetRegions;		// the pool behind every Preset::regions
	Modulator *instrumentModulators;	// the pool behind every InstrumentRegion::mods
	Modulator *presetModulators;		// the pool behind every PresetRegion::mods
};

// ---- Channel.cs ------------------------------------------------------------------------------

struct Channel
{
	enum { DATA_NONE, DATA_RPN, DATA_NRPN };
	enum { DEFAULT_BREATH_CONTROLLER = 100 };	// CC2 default (mezzo-forte-ish)

	bool isPercussionChannel;
	int bankNumber;
	int patchNumber;

	int16_t modulation;
	int16_t volume;
	int16_t pan;
	int16_t expression;
	bool holdPedal;

	// The raw 7-bit value of every CC, so SF2 modulators with a CC source can read it.
	uint8_t controllers[128];

	uint8_t reverbSend;
	uint8_t chorusSend;

	int16_t rpn;
	int16_t pitchBendRange;
	int16_t coarseTune;
	int16_t fineTune;

	float pitchBendValue;
	int lastDataType;

	void init (bool percussion) { isPercussionChannel = percussion; reset (); }
	void reset ();
	void resetAllControllers ();

	void setController (int index, int value) { controllers[index & 0x7F] = (uint8_t) (value & 0x7F); }
	int getController (int index) const { return controllers[index & 0x7F]; }

	void setBank (int value) { bankNumber = value; if (isPercussionChannel) bankNumber += 128; }
	void setPatch (int value) { patchNumber = value; }
	void setModulationCoarse (int v) { modulation = (int16_t) ((modulation & 0x7F) | (v << 7)); }
	void setModulationFine (int v) { modulation = (int16_t) ((modulation & 0xFF80) | v); }
	void setVolumeCoarse (int v) { volume = (int16_t) ((volume & 0x7F) | (v << 7)); }
	void setVolumeFine (int v) { volume = (int16_t) ((volume & 0xFF80) | v); }
	void setPanCoarse (int v) { pan = (int16_t) ((pan & 0x7F) | (v << 7)); }
	void setPanFine (int v) { pan = (int16_t) ((pan & 0xFF80) | v); }
	void setExpressionCoarse (int v) { expression = (int16_t) ((expression & 0x7F) | (v << 7)); }
	void setExpressionFine (int v) { expression = (int16_t) ((expression & 0xFF80) | v); }
	void setHoldPedal (int v) { holdPedal = v >= 64; }
	void setReverbSend (int v) { reverbSend = (uint8_t) v; }
	void setChorusSend (int v) { chorusSend = (uint8_t) v; }
	void setRpnCoarse (int v) { rpn = (int16_t) ((rpn & 0x7F) | (v << 7)); lastDataType = DATA_RPN; }
	void setRpnFine (int v) { rpn = (int16_t) ((rpn & 0xFF80) | v); lastDataType = DATA_RPN; }
	void setNrpnCoarse (int) { lastDataType = DATA_NRPN; }
	void setNrpnFine (int) { lastDataType = DATA_NRPN; }
	void dataEntryCoarse (int v);
	void dataEntryFine (int v);
	void setPitchBend (int v1, int v2) { pitchBendValue = (1.0f / 8192.0f) * ((v1 | (v2 << 7)) - 8192); }

	float modulationValue () const { return (50.0f / 16383.0f) * modulation; }
	float volumeValue () const { return (1.0f / 16383.0f) * volume; }
	float panValue () const { return (100.0f / 16383.0f) * pan - 50.0f; }
	float expressionValue () const { return (1.0f / 16383.0f) * expression; }
	float reverbSendValue () const { return (1.0f / 127.0f) * reverbSend; }
	float chorusSendValue () const { return (1.0f / 127.0f) * chorusSend; }
	float pitchBendRangeValue () const { return (pitchBendRange >> 7) + 0.01f * (pitchBendRange & 0x7F); }
	float tuneValue () const { return coarseTune + (1.0f / 8192.0f) * (fineTune - 8192); }
	float pitchBend () const { return pitchBendRangeValue () * pitchBendValue; }
};

// ---- Modulator.cs (note-on evaluation) + RegionPair.cs ---------------------------------------

// The contribution (in the destination generator's native units) of one modulator to generator
// dest for a NOTE-ON source (velocity / key / CC). isVelocitySource: the source is the velocity.
float modulatorNoteOnContribution (const Modulator &m, int dest, int velocity, int key, const Channel &ch, bool *isVelocitySource);

// A preset region applied to an instrument region: generators add up (except the sample ones).
struct RegionPair
{
	const PresetRegion *preset;
	const InstrumentRegion *instrument;

	int g (int t) const { return instrument->gs[t] + preset->gs[t]; }

	int modulationLfoToPitch () const { return g (G_ModulationLfoToPitch); }
	int vibratoLfoToPitch () const { return g (G_VibratoLfoToPitch); }
	int modulationEnvelopeToPitch () const { return g (G_ModulationEnvelopeToPitch); }
	float initialFilterQ () const { return 0.1f * g (G_InitialFilterQ); }
	int modulationLfoToFilterCutoffFrequency () const { return g (G_ModulationLfoToFilterCutoffFrequency); }
	float modulationLfoToVolume () const { return 0.1f * g (G_ModulationLfoToVolume); }
	float chorusEffectsSend () const { return 0.1f * g (G_ChorusEffectsSend); }
	float reverbEffectsSend () const { return 0.1f * g (G_ReverbEffectsSend); }
	float pan () const { return 0.1f * g (G_Pan); }

	float delayModulationLfo () const { return timecentsToSeconds ((float) g (G_DelayModulationLfo)); }
	float frequencyModulationLfo () const { return centsToHertz ((float) g (G_FrequencyModulationLfo)); }
	float delayVibratoLfo () const { return timecentsToSeconds ((float) g (G_DelayVibratoLfo)); }
	float frequencyVibratoLfo () const { return centsToHertz ((float) g (G_FrequencyVibratoLfo)); }
	float delayModulationEnvelope () const { return timecentsToSeconds ((float) g (G_DelayModulationEnvelope)); }
	float attackModulationEnvelope () const { return timecentsToSeconds ((float) g (G_AttackModulationEnvelope)); }
	float holdModulationEnvelope () const { return timecentsToSeconds ((float) g (G_HoldModulationEnvelope)); }
	float decayModulationEnvelope () const { return timecentsToSeconds ((float) g (G_DecayModulationEnvelope)); }
	float sustainModulationEnvelope () const { return 0.1f * g (G_SustainModulationEnvelope); }
	float releaseModulationEnvelope () const { return timecentsToSeconds ((float) g (G_ReleaseModulationEnvelope)); }
	int keyNumberToModulationEnvelopeHold () const { return g (G_KeyNumberToModulationEnvelopeHold); }
	int keyNumberToModulationEnvelopeDecay () const { return g (G_KeyNumberToModulationEnvelopeDecay); }
	float delayVolumeEnvelope () const { return timecentsToSeconds ((float) g (G_DelayVolumeEnvelope)); }
	float attackVolumeEnvelope () const { return timecentsToSeconds ((float) g (G_AttackVolumeEnvelope)); }
	float holdVolumeEnvelope () const { return timecentsToSeconds ((float) g (G_HoldVolumeEnvelope)); }
	float decayVolumeEnvelope () const { return timecentsToSeconds ((float) g (G_DecayVolumeEnvelope)); }
	float sustainVolumeEnvelope () const { return 0.1f * g (G_SustainVolumeEnvelope); }
	float releaseVolumeEnvelope () const { return timecentsToSeconds ((float) g (G_ReleaseVolumeEnvelope)); }
	int keyNumberToVolumeEnvelopeHold () const { return g (G_KeyNumberToVolumeEnvelopeHold); }
	int keyNumberToVolumeEnvelopeDecay () const { return g (G_KeyNumberToVolumeEnvelopeDecay); }

	float initialAttenuation () const { return 0.1f * g (G_InitialAttenuation); }

	int coarseTune () const { return g (G_CoarseTune); }
	int fineTune () const { return g (G_FineTune) + instrument->sample->pitchCorrection; }
	int sampleModes () const { return instrument->sampleModes (); }
	int scaleTuning () const { return g (G_ScaleTuning); }
	int exclusiveClass () const { return instrument->exclusiveClass (); }
	int rootKey () const { return instrument->rootKey (); }

	// Note-on (velocity / key / CC) modulators, summed over the preset + instrument lists.
	float noteOnAttenuationFromModulators (int velocity, int key, const Channel &ch, bool *hasVelocitySource) const;
	float noteOnGeneratorModulation (int dest, int velocity, int key, const Channel &ch) const;
	float initialFilterCutoffFrequencyModulated (int velocity, int key, const Channel &ch) const
	{
		return centsToHertz (g (G_InitialFilterCutoffFrequency) + noteOnGeneratorModulation (G_InitialFilterCutoffFrequency, velocity, key, ch));
	}
	float modulationEnvelopeToFilterCutoffFrequencyModulated (int velocity, int key, const Channel &ch) const
	{
		return g (G_ModulationEnvelopeToFilterCutoffFrequency) + noteOnGeneratorModulation (G_ModulationEnvelopeToFilterCutoffFrequency, velocity, key, ch);
	}
};

// ---- The voice building blocks ---------------------------------------------------------------

// What the voices need from their synthesizer (Synthesizer.cs properties).
struct SynthCtx
{
	int sampleRate;
	int blockSize;
	int minimumVoiceDuration;
	Channel *channels;
	const SoundFont *sf;
};

struct Oscillator
{
	const SynthCtx *ctx;
	const int16_t *data;
	int loopMode;
	int sampleRate;
	int start, end, startLoop, endLoop;
	int rootKey;
	float tune;
	float pitchChangeScale;
	float sampleRateRatio;
	bool looping;
	int64_t position_fp;

	void start_ (const int16_t *data, int loopMode, int sampleRate, int start, int end, int startLoop, int endLoop,
		int rootKey, int coarseTune, int fineTune, int scaleTuning);
	void release () { if (loopMode == LOOP_UNTIL_NOTE_OFF) looping = false; }
	bool process (float *block, float pitch);
	bool fillBlockNoLoop (float *block, int64_t pitchRatio_fp);
	bool fillBlockContinuous (float *block, int64_t pitchRatio_fp);
};

struct BiQuadFilter
{
	const SynthCtx *ctx;
	bool active;
	float a0, a1, a2, a3, a4;
	float x1, x2, y1, y2;

	void clearBuffer () { x1 = x2 = y1 = y2 = 0; }
	void setLowPassFilter (float cutoffFrequency, float resonance);
	void process (float *block);
};

struct Lfo
{
	const SynthCtx *ctx;
	bool active;
	double delay;
	double period;
	int processedSampleCount;
	float value;

	void start (float delay, float frequency);
	void process ();
};

enum { STAGE_DELAY, STAGE_ATTACK, STAGE_HOLD, STAGE_DECAY, STAGE_RELEASE };

struct ModulationEnvelope
{
	const SynthCtx *ctx;
	double attackSlope, decaySlope, releaseSlope;
	double attackStartTime, holdStartTime, decayStartTime;
	double decayEndTime, releaseEndTime;
	float sustainLevel, releaseLevel;
	int processedSampleCount;
	int stage;
	float value;

	void start (float delay, float attack, float hold, float decay, float sustain, float release);
	void release ();
	bool process () { return process (ctx->blockSize); }
	bool process (int sampleCount);
};

struct VolumeEnvelope
{
	const SynthCtx *ctx;
	double attackSlope, decaySlope, releaseSlope;
	double attackStartTime, holdStartTime, decayStartTime, releaseStartTime;
	float sustainLevel, releaseLevel;
	int processedSampleCount;
	int stage;
	float value;
	float priority;

	void start (float delay, float attack, float hold, float decay, float sustain, float release);
	void release ();
	bool process () { return process (ctx->blockSize); }
	bool process (int sampleCount);
};

struct Voice
{
	enum { PLAYING, RELEASE_REQUESTED, RELEASED };

	const SynthCtx *ctx;
	VolumeEnvelope volEnv;
	ModulationEnvelope modEnv;
	Lfo vibLfo, modLfo;
	Oscillator oscillator;
	BiQuadFilter filter;
	float *block;				// ctx->blockSize floats (owned by the synthesizer)

	// A sudden change in the mix gain causes pop noise: the previous block's gains are kept and
	// the mixer (Synthesizer::renderBlock) ramps between previous and current.
	float previousMixGainLeft, previousMixGainRight, currentMixGainLeft, currentMixGainRight;
	float previousReverbSend, previousChorusSend, currentReverbSend, currentChorusSend;

	int exclusiveClass;
	int channel;
	int key;
	int velocity;
	float noteGain;
	float cutoff;
	float resonance;
	float vibLfoToPitch, modLfoToPitch, modEnvToPitch;
	int modLfoToCutoff, modEnvToCutoff;
	bool dynamicCutoff;
	float modLfoToVolume;
	bool dynamicVolume;
	float instrumentPan, instrumentReverb, instrumentChorus;
	float smoothedCutoff;			// smooths fast cutoff changes (pop noise)
	int voiceState;
	int voiceLength;

	// Glide: the voice starts glideStartOffsetSemis away from its key and slides linearly to 0.
	float glideStartOffsetSemis;
	int glideDurSamples;
	int glideElapsedSamples;

	void init (const SynthCtx *c, float *blockBuffer);
	void start (const RegionPair &region, int channel, int key, int velocity);
	void startWithGlide (const RegionPair &region, int channel, int key, int velocity, float glideFromKey, float glideDurSec);
	void end () { if (voiceState == PLAYING) voiceState = RELEASE_REQUESTED; }
	void kill () { noteGain = 0; }
	bool process ();
	void releaseIfNecessary (const Channel &ch);
	float priority () const { return noteGain < NON_AUDIBLE ? 0.0f : volEnv.priority; }
};

// ---- Effects ---------------------------------------------------------------------------------

// Reverb.cs: Freeverb (public domain, Jezar at Dreampoint).
struct CombFilter
{
	float *buffer;
	int size;
	int bufferIndex;
	float filterStore;
	float feedback;
	float damp1, damp2;

	void process (const float *input, float *output, int n);
	void mute ();
};

struct AllPassFilter
{
	float *buffer;
	int size;
	int bufferIndex;
	float feedback;

	void process (float *block, int n);
	void mute ();
};

struct Reverb
{
	CombFilter cfsL[8], cfsR[8];
	AllPassFilter apfsL[4], apfsR[4];
	float gain;
	float roomSize, roomSize1;
	float damp, damp1;
	float wet, wet1, wet2;
	float width;

	bool init (int sampleRate);		// false: out of memory (free () is then still safe)
	void free_ ();
	void process (const float *input, float *outputLeft, float *outputRight, int n);
	void mute ();
	void update ();
	float inputGain () const { return gain; }
};

// Chorus.cs
struct Chorus
{
	float *bufferL, *bufferR;
	int bufferLength;
	float *delayTable;
	int delayTableLength;
	int bufferIndex;
	int delayTableIndexL, delayTableIndexR;

	bool init (int sampleRate, double delay, double depth, double frequency);
	void free_ ();
	void process (const float *inL, const float *inR, float *outL, float *outR, int n);
	void mute ();
};

// ---- Synthesizer.cs state --------------------------------------------------------------------

struct PresetId
{
	int id;					// (bank << 16) | patch
	const Preset *preset;
};

struct SynthState
{
	SynthCtx ctx;
	int maximumPolyphony;
	bool enableReverbAndChorus;
	int channelCount;

	PresetId *presetLookup;			// sorted by id, one entry per id (the first preset wins)
	int presetLookupCount;
	const Preset *defaultPreset;

	Channel *channels;

	// VoiceCollection.cs: the voice pool; the first activeVoiceCount entries of voices are live.
	Voice *voicePool;
	Voice **voices;
	int activeVoiceCount;
	float *voiceBlocks;

	float *blockLeft, *blockRight;
	float inverseBlockSize;
	int blockRead;

	Reverb reverb;
	float *reverbInput, *reverbOutputLeft, *reverbOutputRight;
	Chorus chorus;
	float *chorusInputLeft, *chorusInputRight, *chorusOutputLeft, *chorusOutputRight;
};

} // namespace ms

#endif
