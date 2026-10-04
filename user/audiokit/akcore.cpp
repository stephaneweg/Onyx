//
// akcore.cpp -- AudioKit (audiokit.h; SD:/lib/audiokit.so): the files, the background player and its
// live synthesizer, the output helper, the mixing and conversion functions, the notes, WAV.
//
// The files and the MIDI playing are the Media Player's own code -- Apps/media/decode.h, midi.h: the
// decoders' classes, the Standard MIDI File reader, the rate converter, the SoundFont's place --,
// compiled here behind a C interface, so that every program gets them (the Media Player still
// compiles them for itself, over the library's decoders and synthesizer).
// Compiled into the library only (user/Makefile), with the FPU.
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
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "kapi.h"
#include "Apps/media/decode.h"		// media::Decoder, Stream, decoder_open; midi.h: MidiDecoder, soundfont ()
#include "audiokit/audiokit.h"

using namespace media;

// ---- a sound file -------------------------------------------------------------------------------------

struct ak_stream { Stream *s; };

// (a decoder may give fewer frames than asked before its end: the MIDI one gives 1024 at most)
static int read_full (Stream *s, short *out, int n)
{
	int got = 0;
	while (got < n)
	{
		int k = s->read (out + 2 * got, n - got);
		if (k <= 0) break;
		got += k;
	}
	return got;
}

extern "C" ak_stream *ak_open (const char *path, char *err, int cap)
{
	char e[128];
	if (err == 0 || cap <= 0) { err = e; cap = (int) sizeof e; }
	Decoder *d = decoder_open (path, err, cap);
	if (d == 0) return 0;
	ak_stream *a = new ak_stream;
	a->s = new Stream (d);
	return a;
}
extern "C" int ak_read (ak_stream *a, short *out, int frames)	{ return a != 0 && frames > 0 ? read_full (a->s, out, frames) : 0; }
extern "C" int ak_seek_ms (ak_stream *a, long long ms)		{ return a != 0 && a->s->seekMs (ms) ? 1 : 0; }
extern "C" void ak_info_of (ak_stream *a, struct ak_info *o)
{
	memset (o, 0, sizeof *o);
	if (a == 0) return;
	const Decoder *d = a->s->dec;
	o->rate = d->rate; o->channels = d->channels; o->bits = d->bits; o->kbps = d->kbps;
	o->length_ms = a->s->lengthMs ();
	snprintf (o->format, sizeof o->format, "%s", d->format);
}
extern "C" void ak_close (ak_stream *a)				{ if (a != 0) { delete a->s; delete a; } }

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

// A soft limiter: straight up to 0.9, then bent so that it never passes 1.
static inline float soft (float x)
{
	float a = x < 0 ? -x : x;
	if (a <= 0.9f) return x;
	float y = 0.9f + 0.1f * tanhf ((a - 0.9f) * 10.0f);
	return x < 0 ? -y : y;
}
extern "C" void ak_f32_to_s16 (const float *l, const float *r, short *out, int frames, float gain)
{
	for (int i = 0; i < frames; i++)
	{
		out[2 * i]     = (short) (soft (l[i] * gain) * 32767.0f);
		out[2 * i + 1] = (short) (soft (r[i] * gain) * 32767.0f);
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
extern "C" int ak_wav_save (const char *path, const short *frames, int n, int rate)
{
	if (path == 0 || frames == 0 || n < 0) return -1;
	unsigned bytes = (unsigned) n * 4;
	unsigned char *b = (unsigned char *) malloc (44 + bytes);
	if (b == 0) return -1;
	ak_wav_header (b, rate, 2, bytes);
	memcpy (b + 44, frames, bytes);
	int r = kapi_save_file (path, b, 44 + bytes);
	free (b);
	return r == (int) (44 + bytes) ? 0 : -1;
}

// ---- the SoundFont, the synthesizer for C ---------------------------------------------------------------

extern "C" void *ak_soundfont_default (char *err, int cap)
{
	char e[160];
	if (err == 0 || cap <= 0) { err = e; cap = (int) sizeof e; }
	err[0] = 0;
	return soundfont (err, cap);				// (midi.h: found, loaded once, kept)
}
extern "C" const char *ak_soundfont_name (void)			{ return g_sf != 0 ? g_sfName : ""; }

struct ak_synth { ms::Synthesizer *syn; float L[1024], R[1024]; };
extern "C" ak_synth *ak_synth_new (void)
{
	ms::SoundFont *sf = (ms::SoundFont *) ak_soundfont_default (0, 0);
	if (sf == 0) return 0;
	ms::SynthSettings st; st.sampleRate = AUDIOKIT_RATE;
	ak_synth *s = new ak_synth;
	s->syn = new ms::Synthesizer (sf, st);
	if (!s->syn->ok ()) { delete s->syn; delete s; return 0; }
	return s;
}
extern "C" void ak_synth_free (ak_synth *s)			{ if (s != 0) { delete s->syn; delete s; } }
extern "C" void ak_synth_midi (ak_synth *s, int ch, int cmd, int d1, int d2)
{
	if (s != 0) s->syn->processMidiMessage (ch, cmd, d1, d2);
}
extern "C" void ak_synth_render (ak_synth *s, short *out, int frames)
{
	while (s != 0 && frames > 0)
	{
		int n = frames > 1024 ? 1024 : frames;
		s->syn->render (s->L, s->R, n);
		ak_f32_to_s16 (s->L, s->R, out, n, 2.0f);		// (MeltySynth's master volume is 0.5)
		out += 2 * n; frames -= n;
	}
}

// ---- the system's output, for a program's own frames ----------------------------------------------------

static int s_out;					// 1: this process holds the output (ak_out_* or the player)
static unsigned s_cap = 22050;				// the stream's room when it is empty

static int out_acquire (int chunk, int ahead)
{
	int r = kapi_sound_acquire ();
	if (r != 1) return r;
	kapi_sound_config (chunk, ahead);
	unsigned rt = 0, fr = 0, own = 0;
	kapi_sound_status (&rt, &fr, &own);
	s_cap = fr != 0 ? fr : 22050;
	s_out = 1;
	return 1;
}
static unsigned out_free_frames (void)
{
	unsigned rt = 0, fr = 0, own = 0;
	kapi_sound_status (&rt, &fr, &own);
	return fr;
}
extern "C" int ak_out_open (int chunk, int ahead)		{ return s_out == 1 ? 1 : out_acquire (chunk, ahead); }
extern "C" int ak_out_free (void)				{ return s_out == 1 ? (int) out_free_frames () : 0; }
extern "C" int ak_out_queued (void)
{
	if (s_out != 1) return 0;
	unsigned fr = out_free_frames ();
	return s_cap > fr ? (int) (s_cap - fr) : 0;
}
extern "C" int ak_out_write (const short *frames, int n)
{
	int off = 0;
	while (off < n && s_out == 1)
	{
		int w = kapi_sound_write (frames + 2 * off, (unsigned) (n - off));
		if (w <= 0) { kapi_msleep (3); continue; }
		off += w;
	}
	return off;
}
extern "C" void ak_out_close (void)				{ if (s_out == 1) { s_out = 0; kapi_sound_release (); } }

// ---- the player: a thread, a file and the live synthesizer mixed -------------------------------------------

#define P_CHUNK		512				// frames a pass
#define P_AHEAD		4096				// frames kept queued (~93 ms)

static volatile int s_lk;				// the player's state, between its thread and the callers
static Stream *s_file;
static volatile int s_state = AK_STOPPED;
static int s_loop, s_vol = 100;
static long long s_base, s_written;			// the file's place: frames before the last seek, written since
static long long s_lenMs;
static char s_err[160];
static bool s_thread;
static ms::Synthesizer *s_live;
static volatile bool s_liveOn;				// notes were played and may still sound
static volatile bool s_keepOut;				// (ak_play_keep_output)
static int s_liveQuiet;					// passes without a voice

static int player_main (void *)
{
	static short buf[2 * P_CHUNK], tmp[2 * P_CHUNK];
	static float L[P_CHUNK], R[P_CHUNK];
	int idle = 0;
	for (;;)
	{
		bool bFile = s_file != 0 && (s_state == AK_PLAYING || s_state == AK_BUSY);
		if (!bFile && !s_liveOn)
		{
			if (s_out == 1 && !s_keepOut && ++idle > 60 && ak_out_queued () == 0) ak_out_close ();	// (let the others play)
			kapi_msleep (10);
			continue;
		}
		idle = 0;
		if (s_out != 1)
		{
			if (out_acquire (P_CHUNK, 3) != 1)
			{
				if (s_state == AK_PLAYING) s_state = AK_BUSY;
				kapi_msleep (100);
				continue;
			}
			if (s_state == AK_BUSY) s_state = AK_PLAYING;
		}
		if (ak_out_queued () > P_AHEAD) { kapi_msleep (4); continue; }

		memset (buf, 0, sizeof buf);
		kapi_lock (&s_lk);
		int nGain = ak_volume_gain (s_vol);
		if (s_file != 0 && s_state == AK_PLAYING)
		{
			int n = read_full (s_file, tmp, P_CHUNK);
			if (n > 0) { ak_mix_s16 (buf, tmp, n, nGain); s_written += n; }
			if (n < P_CHUNK)				// the end
			{
				if (s_loop && s_file->seekMs (0)) { s_base = 0; s_written = 0; }
				else { delete s_file; s_file = 0; s_state = AK_STOPPED; }
			}
		}
		if (s_live != 0 && s_liveOn)
		{
			s_live->render (L, R, P_CHUNK);
			ak_f32_to_s16 (L, R, tmp, P_CHUNK, 2.0f);
			ak_mix_s16 (buf, tmp, P_CHUNK, nGain);
			if (s_live->activeVoiceCount () == 0) { if (++s_liveQuiet > 300) s_liveOn = false; }	// (3.5 s: the reverb's tail)
			else s_liveQuiet = 0;
		}
		kapi_unlock (&s_lk);
		ak_out_write (buf, P_CHUNK);
	}
	return 0;
}

static void player_start (void)
{
	if (s_thread) return;
	s_thread = true;
	kapi_thread_create (player_main, 0, 256 * 1024, "audiokit");
}

extern "C" int ak_play (const char *path, int loop)
{
	char err[160] = "";
	Decoder *d = decoder_open (path, err, sizeof err);
	if (d == 0) { kapi_lock (&s_lk); snprintf (s_err, sizeof s_err, "%s", err); kapi_unlock (&s_lk); return -1; }
	Stream *s = new Stream (d);
	kapi_lock (&s_lk);
	Stream *old = s_file;
	s_file = s; s_loop = loop; s_base = 0; s_written = 0; s_lenMs = s->lengthMs ();
	s_err[0] = 0;
	s_state = AK_PLAYING;
	kapi_unlock (&s_lk);
	delete old;
	player_start ();
	return 0;
}
extern "C" void ak_play_stop (void)
{
	kapi_lock (&s_lk);
	Stream *old = s_file;
	s_file = 0; s_state = AK_STOPPED;
	kapi_unlock (&s_lk);
	delete old;
}
extern "C" void ak_play_pause (int on)
{
	kapi_lock (&s_lk);
	if (s_file != 0) s_state = on ? AK_PAUSED : AK_PLAYING;
	kapi_unlock (&s_lk);
}
extern "C" int ak_play_state (void)			{ return s_state; }
extern "C" long long ak_play_len_ms (void)		{ return s_file != 0 ? s_lenMs : 0; }
extern "C" long long ak_play_pos_ms (void)
{
	if (s_file == 0) return 0;
	long long f = s_base + s_written - ak_out_queued ();
	return f > 0 ? f * 1000 / AUDIOKIT_RATE : 0;
}
extern "C" int ak_play_seek_ms (long long ms)
{
	int ok = 0;
	kapi_lock (&s_lk);
	if (s_file != 0 && s_file->seekMs (ms)) { s_base = ms * AUDIOKIT_RATE / 1000; s_written = 0; ok = 1; }
	kapi_unlock (&s_lk);
	return ok;
}
extern "C" int ak_play_volume (int v)
{
	if (v >= 0) s_vol = v > 100 ? 100 : v;
	return s_vol;
}
extern "C" const char *ak_play_error (void)		{ return s_err; }
extern "C" void ak_play_keep_output (int on)		{ s_keepOut = on != 0; }
extern "C" int ak_play_wait (int ms)
{
	for (int t = 0; (s_state == AK_PLAYING || s_state == AK_BUSY) && (ms < 0 || t < ms); t += 20) kapi_msleep (20);
	return s_state;
}

// The live synthesizer: made at the first note.
static bool live_ready (void)
{
	if (s_live != 0) return true;
	char err[160] = "";
	ms::SoundFont *sf = (ms::SoundFont *) ak_soundfont_default (err, sizeof err);
	if (sf == 0) { snprintf (s_err, sizeof s_err, "%s", err); return false; }
	ms::SynthSettings st; st.sampleRate = AUDIOKIT_RATE;
	ms::Synthesizer *syn = new ms::Synthesizer (sf, st);
	if (!syn->ok ()) { delete syn; snprintf (s_err, sizeof s_err, "The synthesizer cannot start (memory)."); return false; }
	s_live = syn;
	return true;
}
static void live_midi (int ch, int cmd, int d1, int d2)
{
	if (!live_ready ()) return;
	kapi_lock (&s_lk);
	s_live->processMidiMessage (ch & 15, cmd, d1 & 127, d2 & 127);
	s_liveQuiet = 0; s_liveOn = true;
	kapi_unlock (&s_lk);
	player_start ();
}
extern "C" int ak_note_on (int ch, int key, int vel)
{
	if (!live_ready ()) return -1;
	live_midi (ch, 0x90, key, vel);
	return 0;
}
extern "C" void ak_note_off (int ch, int key)		{ if (s_live != 0) live_midi (ch, 0x80, key, 0); }
extern "C" void ak_program (int ch, int program)	{ live_midi (ch, 0xC0, program, 0); }
extern "C" void ak_control (int ch, int ctl, int value)	{ live_midi (ch, 0xB0, ctl, value); }
extern "C" void ak_pitch_bend (int ch, int value)
{
	int v = value + 8192;
	if (v < 0) v = 0;
	if (v > 16383) v = 16383;
	live_midi (ch, 0xE0, v & 127, v >> 7);
}
extern "C" void ak_notes_off (void)
{
	if (s_live == 0) return;
	kapi_lock (&s_lk);
	s_live->noteOffAll (false);
	kapi_unlock (&s_lk);
}
