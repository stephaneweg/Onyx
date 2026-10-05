//
// akmix.cpp -- AudioKit (audiokit.h): the pure part -- mixing and conversion (gain, mix, the soft
// limiter, float to 16 bits, the rate converter), the notes (a key's frequency and name, a note and an
// octave's key), the WAV header. No kernel call, no file: compiled into the library (user/Makefile)
// and, as it is, into the PC builds of the programs that use these (Koton for Windows, the tests).
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
#include <stdio.h>
#include <math.h>
#include "audiokit.h"

// ---- mixing and conversion ----------------------------------------------------------------------------

static inline short sat16 (int v) { return (short) (v > 32767 ? 32767 : v < -32768 ? -32768 : v); }

extern "C" void ak_gain_s16 (short *b, int frames, int gain)
{
	if (gain == 65536) return;
	for (int i = 0; i < frames * 2; i++) b[i] = sat16 ((int) (((long long) b[i] * gain) >> 16));
}
extern "C" void ak_mix_s16 (short *dst, const short *src, int frames, int gain)
{
	for (int i = 0; i < frames * 2; i++) dst[i] = sat16 (dst[i] + (int) (((long long) src[i] * gain) >> 16));
}
extern "C" void ak_mono_to_stereo (short *b, int frames)
{
	for (int i = frames - 1; i >= 0; i--) { b[2 * i + 1] = b[i]; b[2 * i] = b[i]; }
}
extern "C" int ak_volume_gain (int v)
{
	if (v < 0) v = 0;
	if (v > 100) v = 100;
	return (int) ((long long) v * v * 65536 / 10000);		// the square: even steps for the ear
}

// The soft limiter (Koton's: straight up to 0.75, then a tanh knee that never passes 1).
extern "C" float ak_soft_clip (float x)
{
	const float T = 0.75f;
	if (x > T) return T + (1.0f - T) * tanhf ((x - T) / (1.0f - T));
	if (x < -T) return -T + (1.0f - T) * tanhf ((x + T) / (1.0f - T));
	return x;
}
extern "C" void ak_f32_to_s16 (const float *l, const float *r, short *out, int frames, float gain)
{
	for (int i = 0; i < frames; i++)
	{
		int x = (int) (ak_soft_clip (l[i] * gain) * 32767.0f), y = (int) (ak_soft_clip (r[i] * gain) * 32767.0f);
		out[2 * i]     = sat16 (x);
		out[2 * i + 1] = sat16 (y);
	}
}

struct ak_resampler { unsigned in, out, frac; int pl, pr, cl, cr; bool primed; };
extern "C" ak_resampler *ak_resampler_new (int in_rate, int out_rate)
{
	if (in_rate <= 0 || out_rate <= 0) return 0;
	ak_resampler *r = new ak_resampler;
	memset (r, 0, sizeof *r);
	r->in = (unsigned) in_rate; r->out = (unsigned) out_rate;
	return r;
}
extern "C" int ak_resample (ak_resampler *r, const short *in, int nIn, short *out, int cap, int *used)
{
	int i = 0, o = 0;
	if (r == 0) { if (used) *used = 0; return 0; }
	while (o < cap)
	{
		while (r->frac >= r->out || !r->primed)			// the next source frame is needed
		{
			if (i >= nIn) goto done;
			r->pl = r->cl; r->pr = r->cr;
			r->cl = in[2 * i]; r->cr = in[2 * i + 1]; i++;
			if (!r->primed) { r->primed = true; r->pl = r->cl; r->pr = r->cr; }
			else r->frac -= r->out;
		}
		out[2 * o]     = (short) (r->pl + (int) ((long long) (r->cl - r->pl) * (int) r->frac / (int) r->out));
		out[2 * o + 1] = (short) (r->pr + (int) ((long long) (r->cr - r->pr) * (int) r->frac / (int) r->out));
		o++;
		r->frac += r->in;
	}
done:
	if (used) *used = i;
	return o;
}
extern "C" void ak_resampler_free (ak_resampler *r)		{ delete r; }

// ---- notes ----------------------------------------------------------------------------------------------

extern "C" int ak_note_mhz (int key)
{
	if (key < 0) key = 0;
	if (key > 127) key = 127;
	return (int) lrint (440000.0 * pow (2.0, (key - 69) / 12.0));
}
static const char *const s_noteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
extern "C" int ak_note_key (int note, int octave)
{
	int key = (octave + 1) * 12 + note;			// (C4 = 60: octave 4, note 0)
	return key < 0 ? 0 : key > 127 ? 127 : key;
}
extern "C" int ak_note_octave_mhz (int note, int octave)	{ return ak_note_mhz (ak_note_key (note, octave)); }
extern "C" void ak_note_name (int key, char *out8)
{
	if (key < 0) key = 0;
	if (key > 127) key = 127;
	snprintf (out8, 8, "%s%d", s_noteNames[key % 12], key / 12 - 1);
}
extern "C" int ak_note_parse (const char *s)
{
	if (s == 0) return -1;
	while (*s == ' ') s++;
	static const int base[7] = { 9, 11, 0, 2, 4, 5, 7 };		// A B C D E F G
	char c = *s >= 'a' && *s <= 'g' ? (char) (*s - 32) : *s;
	if (c < 'A' || c > 'G') return -1;
	int n = base[c - 'A'];
	s++;
	if (*s == '#') { n++; s++; }
	else if (*s == 'b') { n--; s++; }
	int sign = 1, oct = 4;
	if (*s == '-') { sign = -1; s++; }
	if (*s >= '0' && *s <= '9') { oct = 0; while (*s >= '0' && *s <= '9') oct = oct * 10 + (*s++ - '0'); oct *= sign; }
	int key = (oct + 1) * 12 + n;
	return key < 0 || key > 127 ? -1 : key;
}

// ---- WAV ------------------------------------------------------------------------------------------------

static void le32 (unsigned char *p, unsigned v) { p[0] = (unsigned char) v; p[1] = (unsigned char) (v >> 8); p[2] = (unsigned char) (v >> 16); p[3] = (unsigned char) (v >> 24); }
static void le16 (unsigned char *p, unsigned v) { p[0] = (unsigned char) v; p[1] = (unsigned char) (v >> 8); }
extern "C" int ak_wav_header (unsigned char *h, int rate, int channels, unsigned bytes)
{
	memcpy (h, "RIFF", 4); le32 (h + 4, 36 + bytes); memcpy (h + 8, "WAVEfmt ", 8);
	le32 (h + 16, 16); le16 (h + 20, 1); le16 (h + 22, (unsigned) channels);
	le32 (h + 24, (unsigned) rate); le32 (h + 28, (unsigned) (rate * channels * 2)); le16 (h + 32, (unsigned) (channels * 2)); le16 (h + 34, 16);
	memcpy (h + 36, "data", 4); le32 (h + 40, bytes);
	return 44;
}

