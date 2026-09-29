//
// voice.cpp -- Koton's MeltySynth port: one synthesizer voice and its building blocks
// (Oscillator.cs, BiQuadFilter.cs, Lfo.cs, ModulationEnvelope.cs, VolumeEnvelope.cs, Voice.cs,
// RegionEx.cs), plus the note-on evaluation of the SF2 modulators (Modulator.cs, RegionPair.cs)
// that the vendored copy added: velocity / key / CC sources on the initial attenuation and on the
// two filter-cutoff generators.
//
// Everything here runs on the audio core: no allocation, no I/O. The per-sample loops are float
// (+ the oscillator's 64-bit fixed-point phase, as MeltySynth); the envelopes and LFOs keep
// MeltySynth's double-precision timing, but they run once per block, not per sample.
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
#include <string.h>
#include "ms_internal.h"

namespace ms {

// ---- Modulator.cs: note-on sources -----------------------------------------------------------

namespace {

// FluidSynth-compatible concave / convex curves on [0, 1]; coefficient 400/960.
float concave (float x)
{
	if (x <= 0.0f) return 0.0f;
	if (x >= 1.0f) return 1.0f;
	float r = -0.4166667f * (float) log10 ((double) (1.0f - x));
	return r > 1.0f ? 1.0f : r;
}

float convex (float x)
{
	if (x <= 0.0f) return 0.0f;
	if (x >= 1.0f) return 1.0f;
	float r = 1.0f + 0.4166667f * (float) log10 ((double) x);
	return r < 0.0f ? 0.0f : r;
}

// The SF2 source transform (linear / concave / convex / switch), with direction and polarity.
float sourceCurve (uint16_t src, int val)
{
	float x = val / 127.0f;
	if (src & 0x0100) x = 1.0f - x;			// descending
	float c;
	switch ((src >> 10) & 0x3F)
	{
	case 1: c = concave (x); break;
	case 2: c = convex (x); break;
	case 3: c = x >= 0.5f ? 1.0f : 0.0f; break;	// switch
	default: c = x; break;				// linear
	}
	if (src & 0x0200) c = 2.0f * c - 1.0f;		// bipolar
	return c;
}

} // namespace

float modulatorNoteOnContribution (const Modulator &m, int dest, int velocity, int key, const Channel &ch, bool *isVelocitySource)
{
	*isVelocitySource = false;
	if ((m.destinationOper & 0x8000) != 0 || m.destinationOper != dest) return 0.0f;	// links: unsupported
	if (!((m.amountSourceOper & 0x00FF) == 0 && (m.amountSourceOper & 0x0080) == 0)) return 0.0f;
	int val;
	if (m.sourceOper & 0x0080) val = ch.getController (m.sourceOper & 0x007F);	// a MIDI CC (e.g. CC2)
	else
	{
		switch (m.sourceOper & 0x007F)
		{
		case 2: val = velocity; *isVelocitySource = true; break;	// note-on velocity
		case 3: val = key; break;					// note key
		default: return 0.0f;					// other general controllers: ignored
		}
	}
	return m.amount * sourceCurve (m.sourceOper, val);		// the amount source is "none" (== 1)
}

float RegionPair::noteOnAttenuationFromModulators (int velocity, int key, const Channel &ch, bool *hasVelocitySource) const
{
	*hasVelocitySource = false;
	float cb = 0.0f;
	bool v;
	for (int i = 0; i < preset->modCount; i++) { cb += modulatorNoteOnContribution (preset->mods[i], G_InitialAttenuation, velocity, key, ch, &v); *hasVelocitySource |= v; }
	for (int i = 0; i < instrument->modCount; i++) { cb += modulatorNoteOnContribution (instrument->mods[i], G_InitialAttenuation, velocity, key, ch, &v); *hasVelocitySource |= v; }
	return cb;
}

float RegionPair::noteOnGeneratorModulation (int dest, int velocity, int key, const Channel &ch) const
{
	float r = 0.0f;
	bool v;
	for (int i = 0; i < preset->modCount; i++) r += modulatorNoteOnContribution (preset->mods[i], dest, velocity, key, ch, &v);
	for (int i = 0; i < instrument->modCount; i++) r += modulatorNoteOnContribution (instrument->mods[i], dest, velocity, key, ch, &v);
	return r;
}

// ---- Oscillator.cs ---------------------------------------------------------------------------
// Fixed-point phase: an int64 whose low 24 bits are the fraction ("_fp" suffix).

namespace {
const int FRAC_BITS = 24;
const int64_t FRAC_UNIT = (int64_t) 1 << FRAC_BITS;
const float FP_TO_SAMPLE = 1.0f / (32768.0f * (float) FRAC_UNIT);
}

void Oscillator::start_ (const int16_t *data_, int loopMode_, int sampleRate_, int start__, int end_, int startLoop_, int endLoop_,
	int rootKey_, int coarseTune, int fineTune, int scaleTuning)
{
	data = data_;
	loopMode = loopMode_;
	sampleRate = sampleRate_;
	start = start__;
	end = end_;
	startLoop = startLoop_;
	endLoop = endLoop_;
	rootKey = rootKey_;

	tune = coarseTune + 0.01f * fineTune;
	pitchChangeScale = 0.01f * scaleTuning;
	sampleRateRatio = (float) sampleRate / ctx->sampleRate;

	looping = loopMode != LOOP_NONE;
	// Port safety: a loop of no length cannot loop (MeltySynth would run off the sample data).
	if (endLoop - startLoop <= 0) looping = false;

	position_fp = (int64_t) start << FRAC_BITS;
}

bool Oscillator::process (float *block, float pitch)
{
	float pitchChange = pitchChangeScale * (pitch - rootKey) + tune;
	float pitchRatio = sampleRateRatio * powf (2.0f, pitchChange / 12.0f);
	// Port safety: a negative / NaN / absurd ratio (corrupt sample rate or tuning) would move
	// the read position out of the data.
	if (!(pitchRatio >= 0.0f)) pitchRatio = 0.0f;
	if (pitchRatio > 65536.0f) pitchRatio = 65536.0f;
	int64_t pitchRatio_fp = (int64_t) ((float) FRAC_UNIT * pitchRatio);	// (exact: a power-of-two scale)
	return looping ? fillBlockContinuous (block, pitchRatio_fp) : fillBlockNoLoop (block, pitchRatio_fp);
}

bool Oscillator::fillBlockNoLoop (float *block, int64_t pitchRatio_fp)
{
	int n = ctx->blockSize;
	for (int t = 0; t < n; t++)
	{
		int64_t index = position_fp >> FRAC_BITS;
		if (index >= end)
		{
			if (t > 0)
			{
				memset (block + t, 0, (n - t) * sizeof (float));
				return true;
			}
			return false;
		}
		int x1 = data[index];
		int x2 = data[index + 1];
		int64_t a_fp = position_fp & (FRAC_UNIT - 1);
		block[t] = FP_TO_SAMPLE * (float) ((int64_t) x1 * FRAC_UNIT + a_fp * (x2 - x1));
		position_fp += pitchRatio_fp;
	}
	return true;
}

bool Oscillator::fillBlockContinuous (float *block, int64_t pitchRatio_fp)
{
	int n = ctx->blockSize;
	int64_t endLoop_fp = (int64_t) endLoop << FRAC_BITS;
	int64_t loopLength = (int64_t) (endLoop - startLoop);
	int64_t loopLength_fp = loopLength << FRAC_BITS;

	for (int t = 0; t < n; t++)
	{
		if (position_fp >= endLoop_fp)
		{
			position_fp -= loopLength_fp;
			// Port safety: a step longer than the loop would leave it (MeltySynth: out of range).
			if (position_fp >= endLoop_fp) position_fp = endLoop_fp - loopLength_fp + (position_fp - endLoop_fp) % loopLength_fp;
		}
		int64_t index1 = position_fp >> FRAC_BITS;
		int64_t index2 = index1 + 1;
		if (index2 >= endLoop) index2 -= loopLength;

		int x1 = data[index1];
		int x2 = data[index2];
		int64_t a_fp = position_fp & (FRAC_UNIT - 1);
		block[t] = FP_TO_SAMPLE * (float) ((int64_t) x1 * FRAC_UNIT + a_fp * (x2 - x1));
		position_fp += pitchRatio_fp;
	}
	return true;
}

// ---- BiQuadFilter.cs -------------------------------------------------------------------------

namespace {
const float RESONANCE_PEAK_OFFSET = 1.0f - 1.0f / 1.41421356237309505f;	// 1 - 1 / sqrt (2)
}

void BiQuadFilter::setLowPassFilter (float cutoffFrequency, float resonance)
{
	if (cutoffFrequency < 0.499f * ctx->sampleRate)
	{
		active = true;

		// This gives the Q value which makes the desired resonance peak (error < 3%).
		float q = resonance - RESONANCE_PEAK_OFFSET / (1 + 6 * (resonance - 1));
		if (!(q > 1.0E-3f)) q = 1.0E-3f;		// port safety: a negative filter Q (corrupt font)

		float w = 2 * PI_F * cutoffFrequency / ctx->sampleRate;
		float cosw = cosf (w);
		float alpha = sinf (w) / (2 * q);

		float b0 = (1 - cosw) / 2;
		float b1 = 1 - cosw;
		float b2 = (1 - cosw) / 2;
		float c0 = 1 + alpha;
		float c1 = -2 * cosw;
		float c2 = 1 - alpha;

		a0 = b0 / c0;
		a1 = b1 / c0;
		a2 = b2 / c0;
		a3 = c1 / c0;
		a4 = c2 / c0;
	}
	else active = false;
}

void BiQuadFilter::process (float *block)
{
	int n = ctx->blockSize;
	if (active)
	{
		float lx1 = x1, lx2 = x2, ly1 = y1, ly2 = y2;
		for (int t = 0; t < n; t++)
		{
			float input = block[t];
			float output = a0 * input + a1 * lx1 + a2 * lx2 - a3 * ly1 - a4 * ly2;
			lx2 = lx1;
			lx1 = input;
			ly2 = ly1;
			ly1 = output;
			block[t] = output;
		}
		x1 = lx1; x2 = lx2; y1 = ly1; y2 = ly2;
	}
	else
	{
		x2 = block[n - 2];
		x1 = block[n - 1];
		y2 = x2;
		y1 = x1;
	}
}

// ---- Lfo.cs ----------------------------------------------------------------------------------

void Lfo::start (float delay_, float frequency)
{
	if (frequency > 1.0E-3f)
	{
		active = true;
		delay = delay_;
		period = 1.0 / frequency;
		processedSampleCount = 0;
		value = 0;
	}
	else
	{
		active = false;
		value = 0;
	}
}

void Lfo::process ()
{
	if (!active) return;
	processedSampleCount += ctx->blockSize;
	double currentTime = (double) processedSampleCount / ctx->sampleRate;
	if (currentTime < delay) value = 0;
	else
	{
		double phase = fmod (currentTime - delay, period) / period;
		if (phase < 0.25) value = (float) (4 * phase);
		else if (phase < 0.75) value = (float) (4 * (0.5 - phase));
		else value = (float) (4 * (phase - 1.0));
	}
}

// ---- ModulationEnvelope.cs -------------------------------------------------------------------

void ModulationEnvelope::start (float delay, float attack, float hold, float decay, float sustain, float release_)
{
	attackSlope = 1 / (double) attack;
	decaySlope = 1 / (double) decay;
	releaseSlope = 1 / (double) release_;

	attackStartTime = delay;
	holdStartTime = attackStartTime + attack;
	decayStartTime = holdStartTime + hold;

	decayEndTime = decayStartTime + decay;
	releaseEndTime = release_;

	sustainLevel = clampf (sustain, 0.0f, 1.0f);
	releaseLevel = 0;

	processedSampleCount = 0;
	stage = STAGE_DELAY;
	value = 0;

	process (0);
}

void ModulationEnvelope::release ()
{
	stage = STAGE_RELEASE;
	releaseEndTime += (double) processedSampleCount / ctx->sampleRate;
	releaseLevel = value;
}

bool ModulationEnvelope::process (int sampleCount)
{
	processedSampleCount += sampleCount;
	double currentTime = (double) processedSampleCount / ctx->sampleRate;

	while (stage <= STAGE_HOLD)
	{
		double endTime = stage == STAGE_DELAY ? attackStartTime : stage == STAGE_ATTACK ? holdStartTime : decayStartTime;
		if (currentTime < endTime) break;
		stage++;
	}

	switch (stage)
	{
	case STAGE_DELAY:
		value = 0;
		return true;
	case STAGE_ATTACK:
		value = (float) (attackSlope * (currentTime - attackStartTime));
		return true;
	case STAGE_HOLD:
		value = 1;
		return true;
	case STAGE_DECAY:
		value = maxf ((float) (decaySlope * (decayEndTime - currentTime)), sustainLevel);
		return value > NON_AUDIBLE;
	default:	// STAGE_RELEASE
		value = maxf ((float) (releaseLevel * releaseSlope * (releaseEndTime - currentTime)), 0.0f);
		return value > NON_AUDIBLE;
	}
}

// ---- VolumeEnvelope.cs -----------------------------------------------------------------------

void VolumeEnvelope::start (float delay, float attack, float hold, float decay, float sustain, float release_)
{
	attackSlope = 1 / (double) attack;
	decaySlope = -9.226 / decay;
	releaseSlope = -9.226 / release_;

	attackStartTime = delay;
	holdStartTime = attackStartTime + attack;
	decayStartTime = holdStartTime + hold;
	releaseStartTime = 0;

	sustainLevel = clampf (sustain, 0.0f, 1.0f);
	releaseLevel = 0;

	processedSampleCount = 0;
	stage = STAGE_DELAY;
	value = 0;

	process (0);
}

void VolumeEnvelope::release ()
{
	stage = STAGE_RELEASE;
	releaseStartTime = (double) processedSampleCount / ctx->sampleRate;
	releaseLevel = value;
}

bool VolumeEnvelope::process (int sampleCount)
{
	processedSampleCount += sampleCount;
	double currentTime = (double) processedSampleCount / ctx->sampleRate;

	while (stage <= STAGE_HOLD)
	{
		double endTime = stage == STAGE_DELAY ? attackStartTime : stage == STAGE_ATTACK ? holdStartTime : decayStartTime;
		if (currentTime < endTime) break;
		stage++;
	}

	switch (stage)
	{
	case STAGE_DELAY:
		value = 0;
		priority = 4.0f + value;
		return true;
	case STAGE_ATTACK:
		value = (float) (attackSlope * (currentTime - attackStartTime));
		priority = 3.0f + value;
		return true;
	case STAGE_HOLD:
		value = 1;
		priority = 2.0f + value;
		return true;
	case STAGE_DECAY:
		value = maxf ((float) expCutoff (decaySlope * (currentTime - decayStartTime)), sustainLevel);
		priority = 1.0f + value;
		return value > NON_AUDIBLE;
	default:	// STAGE_RELEASE
		value = (float) (releaseLevel * expCutoff (releaseSlope * (currentTime - releaseStartTime)));
		priority = value;
		return value > NON_AUDIBLE;
	}
}

// ---- Voice.cs + RegionEx.cs ------------------------------------------------------------------

void Voice::init (const SynthCtx *c, float *blockBuffer)
{
	memset (this, 0, sizeof *this);
	ctx = c;
	volEnv.ctx = c;
	modEnv.ctx = c;
	vibLfo.ctx = c;
	modLfo.ctx = c;
	oscillator.ctx = c;
	filter.ctx = c;
	block = blockBuffer;
}

void Voice::startWithGlide (const RegionPair &region, int channel_, int key_, int velocity_, float glideFromKey, float glideDurSec)
{
	start (region, channel_, key_, velocity_);
	if (glideDurSec > 0 && fabsf (glideFromKey - key_) > 0.001f)
	{
		glideStartOffsetSemis = glideFromKey - key_;	// < 0 going up, > 0 going down
		glideDurSamples = (int) rint ((double) glideDurSec * ctx->sampleRate);
		glideElapsedSamples = 0;
	}
}

void Voice::start (const RegionPair &region, int channel_, int key_, int velocity_)
{
	glideStartOffsetSemis = 0;
	glideDurSamples = 0;
	glideElapsedSamples = 0;
	exclusiveClass = region.exclusiveClass ();
	channel = channel_;
	key = key_;
	velocity = velocity_;

	const Channel &chan = ctx->channels[channel];

	if (velocity > 0)
	{
		// According to Polyphone's implementation, the initial attenuation is reduced to 40%.
		float sampleAttenuation = 0.4f * region.initialAttenuation ();
		float filterAttenuation = 0.5f * region.initialFilterQ ();

		// The font's note-on modulators on the initial attenuation. A font velocity->attenuation
		// modulator OVERRIDES the default velocity term (2 * dB (vel / 127), == SF2's default
		// velocity->attenuation modulator).
		bool hasFontVelAttenuation;
		float modAttenuationDb = region.noteOnAttenuationFromModulators (velocity, key, chan, &hasFontVelAttenuation) / 10.0f;
		float velocityDb = hasFontVelAttenuation
			? -modAttenuationDb
			: 2 * linearToDecibels (velocity / 127.0f) - modAttenuationDb;

		float decibels = velocityDb - sampleAttenuation - filterAttenuation;
		noteGain = decibelsToLinear (decibels);
	}
	else noteGain = 0;

	cutoff = region.initialFilterCutoffFrequencyModulated (velocity, key, chan);
	resonance = decibelsToLinear (region.initialFilterQ ());

	vibLfoToPitch = 0.01f * region.vibratoLfoToPitch ();
	modLfoToPitch = 0.01f * region.modulationLfoToPitch ();
	modEnvToPitch = 0.01f * region.modulationEnvelopeToPitch ();

	modLfoToCutoff = region.modulationLfoToFilterCutoffFrequency ();
	// The font's velocity / key -> ModEnvToFilterCutoff modulators (as FluidSynth).
	modEnvToCutoff = (int) rintf (region.modulationEnvelopeToFilterCutoffFrequencyModulated (velocity, key, chan));
	dynamicCutoff = modLfoToCutoff != 0 || modEnvToCutoff != 0;

	modLfoToVolume = region.modulationLfoToVolume ();
	dynamicVolume = modLfoToVolume > 0.05f;

	instrumentPan = clampf (region.pan (), -50.0f, 50.0f);
	instrumentReverb = 0.01f * region.reverbEffectsSend ();
	instrumentChorus = 0.01f * region.chorusEffectsSend ();

	// RegionEx: the volume envelope (release clamped to >= 10 ms against pops).
	{
		float delay = region.delayVolumeEnvelope ();
		float attack = region.attackVolumeEnvelope ();
		float hold = region.holdVolumeEnvelope () * keyNumberToMultiplyingFactor (region.keyNumberToVolumeEnvelopeHold (), key);
		float decay = region.decayVolumeEnvelope () * keyNumberToMultiplyingFactor (region.keyNumberToVolumeEnvelopeDecay (), key);
		float sustain = decibelsToLinear (-region.sustainVolumeEnvelope ());
		float release = maxf (region.releaseVolumeEnvelope (), 0.01f);
		volEnv.start (delay, attack, hold, decay, sustain, release);
	}
	// RegionEx: the modulation envelope (attack scaled by the velocity, as TinySoundFont).
	{
		float delay = region.delayModulationEnvelope ();
		float attack = region.attackModulationEnvelope () * ((145 - velocity) / 144.0f);
		float hold = region.holdModulationEnvelope () * keyNumberToMultiplyingFactor (region.keyNumberToModulationEnvelopeHold (), key);
		float decay = region.decayModulationEnvelope () * keyNumberToMultiplyingFactor (region.keyNumberToModulationEnvelopeDecay (), key);
		float sustain = 1.0f - region.sustainModulationEnvelope () / 100.0f;
		float release = region.releaseModulationEnvelope ();
		modEnv.start (delay, attack, hold, decay, sustain, release);
	}
	vibLfo.start (region.delayVibratoLfo (), region.frequencyVibratoLfo ());
	modLfo.start (region.delayModulationLfo (), region.frequencyModulationLfo ());
	{
		const InstrumentRegion *ir = region.instrument;
		oscillator.start_ (ctx->sf->wave, region.sampleModes (), ir->sample->sampleRate,
			ir->sampleStart (), ir->sampleEnd (), ir->sampleStartLoop (), ir->sampleEndLoop (),
			region.rootKey (), region.coarseTune (), region.fineTune (), region.scaleTuning ());
	}
	filter.clearBuffer ();
	filter.setLowPassFilter (cutoff, resonance);

	smoothedCutoff = cutoff;

	voiceState = PLAYING;
	voiceLength = 0;
}

void Voice::releaseIfNecessary (const Channel &ch)
{
	if (voiceLength < ctx->minimumVoiceDuration) return;
	if (voiceState == RELEASE_REQUESTED && !ch.holdPedal)
	{
		volEnv.release ();
		modEnv.release ();
		oscillator.release ();
		voiceState = RELEASED;
	}
}

bool Voice::process ()
{
	if (noteGain < NON_AUDIBLE) return false;

	const Channel &ch = ctx->channels[channel];

	releaseIfNecessary (ch);

	if (!volEnv.process ()) return false;

	modEnv.process ();
	vibLfo.process ();
	modLfo.process ();

	float vibPitchChange = (0.01f * ch.modulationValue () + vibLfoToPitch) * vibLfo.value;
	float modPitchChange = modLfoToPitch * modLfo.value + modEnvToPitch * modEnv.value;
	float channelPitchChange = ch.tuneValue () + ch.pitchBend ();
	// Glide: an offset fading linearly from glideStartOffsetSemis to 0, then spent.
	float glideOffset = 0;
	if (glideDurSamples > 0 && glideElapsedSamples < glideDurSamples)
	{
		float t = (float) glideElapsedSamples / glideDurSamples;
		glideOffset = glideStartOffsetSemis * (1.0f - t);
		glideElapsedSamples += ctx->blockSize;
	}
	float pitch = key + vibPitchChange + modPitchChange + channelPitchChange + glideOffset;
	if (!oscillator.process (block, pitch)) return false;

	if (dynamicCutoff)
	{
		float cents = modLfoToCutoff * modLfo.value + modEnvToCutoff * modEnv.value;
		float factor = centsToMultiplyingFactor (cents);
		float newCutoff = factor * cutoff;

		// The cutoff change is limited within x0.5 and x2 to reduce pop noise.
		float lowerLimit = 0.5f * smoothedCutoff;
		float upperLimit = 2.0f * smoothedCutoff;
		smoothedCutoff = clampf (newCutoff, lowerLimit, upperLimit);

		filter.setLowPassFilter (smoothedCutoff, resonance);
	}
	filter.process (block);

	previousMixGainLeft = currentMixGainLeft;
	previousMixGainRight = currentMixGainRight;
	previousReverbSend = currentReverbSend;
	previousChorusSend = currentChorusSend;

	// According to the GM spec, the following value should be squared.
	float ve = ch.volumeValue () * ch.expressionValue ();
	float channelGain = ve * ve;

	float mixGain = noteGain * channelGain * volEnv.value;
	if (dynamicVolume)
	{
		float decibels = modLfoToVolume * modLfo.value;
		mixGain *= decibelsToLinear (decibels);
	}

	float angle = (PI_F / 200.0f) * (ch.panValue () + instrumentPan + 50.0f);
	if (angle <= 0.0f)
	{
		currentMixGainLeft = mixGain;
		currentMixGainRight = 0;
	}
	else if (angle >= HALF_PI_F)
	{
		currentMixGainLeft = 0;
		currentMixGainRight = mixGain;
	}
	else
	{
		currentMixGainLeft = mixGain * cosf (angle);
		currentMixGainRight = mixGain * sinf (angle);
	}

	currentReverbSend = clampf (ch.reverbSendValue () + instrumentReverb, 0.0f, 1.0f);
	currentChorusSend = clampf (ch.chorusSendValue () + instrumentChorus, 0.0f, 1.0f);

	if (voiceLength == 0)
	{
		previousMixGainLeft = currentMixGainLeft;
		previousMixGainRight = currentMixGainRight;
		previousReverbSend = currentReverbSend;
		previousChorusSend = currentChorusSend;
	}

	voiceLength += ctx->blockSize;
	return true;
}

} // namespace ms
