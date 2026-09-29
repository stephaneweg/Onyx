//
// effects.cpp -- Koton's MeltySynth port: the send effects. Reverb.cs (Freeverb, a public-domain
// reverb by Jezar at Dreampoint: 8 comb + 4 all-pass filters per side) and Chorus.cs (a stereo
// modulated delay, the two sides a quarter period apart).
//
// Buffers are allocated by init () (at the synthesizer's construction); process () never
// allocates. The chorus interpolates in float where MeltySynth used double (same result to
// within float rounding; the delay line is only ~170 samples long).
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

// ---- Reverb.cs -------------------------------------------------------------------------------

namespace {

const float FIXED_GAIN = 0.015f;
const float SCALE_WET = 3.0f;
const float SCALE_DAMP = 0.4f;
const float SCALE_ROOM = 0.28f;
const float OFFSET_ROOM = 0.7f;
const float INITIAL_ROOM = 0.5f;
const float INITIAL_DAMP = 0.5f;
const float INITIAL_WET = 1.0f / SCALE_WET;
const float INITIAL_WIDTH = 1.0f;
const int STEREO_SPREAD = 23;

const int CF_TUNING[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
const int APF_TUNING[4] = { 556, 441, 341, 225 };

// Math.Round (banker's rounding) == rint in the default rounding mode.
int scaleTuning (int sampleRate, int tuning) { return (int) rint ((double) sampleRate / 44100 * tuning); }

} // namespace

void CombFilter::mute ()
{
	memset (buffer, 0, size * sizeof (float));
	filterStore = 0;
}

void CombFilter::process (const float *inputBlock, float *outputBlock, int n)
{
	int blockIndex = 0;
	while (blockIndex < n)
	{
		if (bufferIndex == size) bufferIndex = 0;
		int srcRem = size - bufferIndex;
		int dstRem = n - blockIndex;
		int rem = srcRem < dstRem ? srcRem : dstRem;

		const float *in = inputBlock + blockIndex;
		float *out = outputBlock + blockIndex;
		float *buf = buffer + bufferIndex;
		float fs = filterStore;
		for (int t = 0; t < rem; t++)
		{
			// The ifs flush denormals (a big slowdown otherwise).
			float output = buf[t];
			if (fabsf (output) < 1.0E-6f) output = 0;
			fs = (output * damp2) + (fs * damp1);
			if (fabsf (fs) < 1.0E-6f) fs = 0;
			buf[t] = in[t] + (fs * feedback);
			out[t] += output;
		}
		filterStore = fs;

		bufferIndex += rem;
		blockIndex += rem;
	}
}

void AllPassFilter::mute ()
{
	memset (buffer, 0, size * sizeof (float));
}

void AllPassFilter::process (float *block, int n)
{
	int blockIndex = 0;
	while (blockIndex < n)
	{
		if (bufferIndex == size) bufferIndex = 0;
		int srcRem = size - bufferIndex;
		int dstRem = n - blockIndex;
		int rem = srcRem < dstRem ? srcRem : dstRem;

		float *io = block + blockIndex;
		float *buf = buffer + bufferIndex;
		for (int t = 0; t < rem; t++)
		{
			float input = io[t];
			float bufout = buf[t];
			if (fabsf (bufout) < 1.0E-6f) bufout = 0;
			io[t] = bufout - input;
			buf[t] = input + (bufout * feedback);
		}

		bufferIndex += rem;
		blockIndex += rem;
	}
}

bool Reverb::init (int sampleRate)
{
	memset (this, 0, sizeof *this);
	for (int i = 0; i < 8; i++)
	{
		cfsL[i].size = scaleTuning (sampleRate, CF_TUNING[i]);
		cfsR[i].size = scaleTuning (sampleRate, CF_TUNING[i] + STEREO_SPREAD);
		cfsL[i].buffer = (float *) calloc (cfsL[i].size, sizeof (float));
		cfsR[i].buffer = (float *) calloc (cfsR[i].size, sizeof (float));
		if (!cfsL[i].buffer || !cfsR[i].buffer) return false;
	}
	for (int i = 0; i < 4; i++)
	{
		apfsL[i].size = scaleTuning (sampleRate, APF_TUNING[i]);
		apfsR[i].size = scaleTuning (sampleRate, APF_TUNING[i] + STEREO_SPREAD);
		apfsL[i].buffer = (float *) calloc (apfsL[i].size, sizeof (float));
		apfsR[i].buffer = (float *) calloc (apfsR[i].size, sizeof (float));
		if (!apfsL[i].buffer || !apfsR[i].buffer) return false;
		apfsL[i].feedback = 0.5f;
		apfsR[i].feedback = 0.5f;
	}
	// The property setters of Reverb.cs, in the same order (each one calls Update).
	wet = INITIAL_WET * SCALE_WET;
	roomSize = (INITIAL_ROOM * SCALE_ROOM) + OFFSET_ROOM;
	damp = INITIAL_DAMP * SCALE_DAMP;
	width = INITIAL_WIDTH;
	update ();
	return true;
}

void Reverb::free_ ()
{
	for (int i = 0; i < 8; i++) { free (cfsL[i].buffer); free (cfsR[i].buffer); cfsL[i].buffer = cfsR[i].buffer = nullptr; }
	for (int i = 0; i < 4; i++) { free (apfsL[i].buffer); free (apfsR[i].buffer); apfsL[i].buffer = apfsR[i].buffer = nullptr; }
}

void Reverb::update ()
{
	wet1 = wet * (width / 2.0f + 0.5f);
	wet2 = wet * ((1.0f - width) / 2.0f);

	roomSize1 = roomSize;
	damp1 = damp;
	gain = FIXED_GAIN;

	for (int i = 0; i < 8; i++)
	{
		cfsL[i].feedback = roomSize1;
		cfsL[i].damp1 = damp1;
		cfsL[i].damp2 = 1.0f - damp1;
		cfsR[i].feedback = roomSize1;
		cfsR[i].damp1 = damp1;
		cfsR[i].damp2 = 1.0f - damp1;
	}
}

void Reverb::process (const float *input, float *outputLeft, float *outputRight, int n)
{
	memset (outputLeft, 0, n * sizeof (float));
	memset (outputRight, 0, n * sizeof (float));

	for (int i = 0; i < 8; i++) cfsL[i].process (input, outputLeft, n);
	for (int i = 0; i < 4; i++) apfsL[i].process (outputLeft, n);
	for (int i = 0; i < 8; i++) cfsR[i].process (input, outputRight, n);
	for (int i = 0; i < 4; i++) apfsR[i].process (outputRight, n);

	// With the default settings, this part is skipped.
	if (1.0f - wet1 > 1.0E-3f || wet2 > 1.0E-3f)
	{
		for (int t = 0; t < n; t++)
		{
			float left = outputLeft[t];
			float right = outputRight[t];
			outputLeft[t] = left * wet1 + right * wet2;
			outputRight[t] = right * wet1 + left * wet2;
		}
	}
}

void Reverb::mute ()
{
	for (int i = 0; i < 8; i++) { cfsL[i].mute (); cfsR[i].mute (); }
	for (int i = 0; i < 4; i++) { apfsL[i].mute (); apfsR[i].mute (); }
}

// ---- Chorus.cs -------------------------------------------------------------------------------

bool Chorus::init (int sampleRate, double delay, double depth, double frequency)
{
	memset (this, 0, sizeof *this);
	bufferLength = (int) (sampleRate * (delay + depth)) + 2;
	bufferL = (float *) calloc (bufferLength, sizeof (float));
	bufferR = (float *) calloc (bufferLength, sizeof (float));

	delayTableLength = (int) rint (sampleRate / frequency);
	delayTable = (float *) malloc (delayTableLength * sizeof (float));
	if (!bufferL || !bufferR || !delayTable) return false;
	for (int t = 0; t < delayTableLength; t++)
	{
		double phase = 2 * 3.14159265358979323846 * t / delayTableLength;
		delayTable[t] = (float) (sampleRate * (delay + depth * sin (phase)));
	}

	bufferIndex = 0;
	delayTableIndexL = 0;
	delayTableIndexR = delayTableLength / 4;
	return true;
}

void Chorus::free_ ()
{
	free (bufferL); free (bufferR); free (delayTable);
	bufferL = bufferR = delayTable = nullptr;
}

void Chorus::process (const float *inputLeft, const float *inputRight, float *outputLeft, float *outputRight, int n)
{
	const float len = (float) bufferLength;
	for (int t = 0; t < n; t++)
	{
		{
			float position = bufferIndex - delayTable[delayTableIndexL];
			if (position < 0.0f) position += len;
			int index1 = (int) position;
			if (index1 >= bufferLength) index1 -= bufferLength;	// (float rounding of a tiny negative)
			int index2 = index1 + 1;
			if (index2 == bufferLength) index2 = 0;
			float x1 = bufferL[index1];
			float x2 = bufferL[index2];
			float a = position - index1;
			outputLeft[t] = x1 + a * (x2 - x1);
			if (++delayTableIndexL == delayTableLength) delayTableIndexL = 0;
		}
		{
			float position = bufferIndex - delayTable[delayTableIndexR];
			if (position < 0.0f) position += len;
			int index1 = (int) position;
			if (index1 >= bufferLength) index1 -= bufferLength;
			int index2 = index1 + 1;
			if (index2 == bufferLength) index2 = 0;
			float x1 = bufferR[index1];
			float x2 = bufferR[index2];
			float a = position - index1;
			outputRight[t] = x1 + a * (x2 - x1);
			if (++delayTableIndexR == delayTableLength) delayTableIndexR = 0;
		}

		bufferL[bufferIndex] = inputLeft[t];
		bufferR[bufferIndex] = inputRight[t];
		if (++bufferIndex == bufferLength) bufferIndex = 0;
	}
}

void Chorus::mute ()
{
	memset (bufferL, 0, bufferLength * sizeof (float));
	memset (bufferR, 0, bufferLength * sizeof (float));
}

} // namespace ms
