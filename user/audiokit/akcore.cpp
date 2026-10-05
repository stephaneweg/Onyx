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
#define MEDIA_SOUNDFONT_AUDIOKIT			// (midi.h: the SoundFont is aksf.cpp's, one for the process)
#include "Apps/media/decode.h"		// media::Decoder, Stream, decoder_open; midi.h: MidiDecoder, soundfont ()
#include "audiokit/audiokit.h"
#include "Apps/koton/synth/ms_internal.h"	// the reverb and the chorus (ak_reverb_*, ak_chorus_*)

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

// (the mixing and conversion functions, the notes, the WAV header: akmix.cpp)

static inline short sat16 (int v) { return (short) (v > 32767 ? 32767 : v < -32768 ? -32768 : v); }

// (the WAV files written -- ak_wav_save, ak_wav_begin / _write / _end --: akwav.cpp)

// ---- the SoundFont, the synthesizer for C ---------------------------------------------------------------

// (the SoundFont -- ak_soundfont_find / _load / _default / _name --: aksf.cpp)

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
	if (chunk > 0) kapi_sound_config (chunk, ahead);		// (0: the output as it is)
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
#define P_AHEAD_LIVE	1024				// ... when only notes play (a game's effects: ~23 ms)

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

extern "C" int akfm_active (void);			// (akfm.cpp: FM voices sound, or their releases)

static int player_main (void *)
{
	static short buf[2 * P_CHUNK], tmp[2 * P_CHUNK];
	static float L[P_CHUNK], R[P_CHUNK];
	int idle = 0;
	for (;;)
	{
		bool bFile = s_file != 0 && (s_state == AK_PLAYING || s_state == AK_BUSY);
		if (!bFile && !s_liveOn && !akfm_active ())
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
		if (ak_out_queued () > (bFile ? P_AHEAD : P_AHEAD_LIVE)) { kapi_msleep (4); continue; }

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
		if (akfm_active ()) { memset (tmp, 0, sizeof tmp); ak_fm_render (tmp, P_CHUNK); ak_mix_s16 (buf, tmp, P_CHUNK, 65536); }
		ak_out_write (buf, P_CHUNK);
	}
	return 0;
}

extern "C" void akplayer_start (void);		// (for akfm.cpp: the FM voices are played by the same thread)
static void player_start (void);
extern "C" void akplayer_start (void)			{ player_start (); }

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

// ---- the reverb and the chorus (MeltySynth's), as effects of their own -------------------------------------

struct ak_reverb { ms::Reverb r; };
extern "C" ak_reverb *ak_reverb_new (int rate)
{
	ak_reverb *v = new ak_reverb;
	memset ((void *) v, 0, sizeof *v);
	if (!v->r.init (rate)) { v->r.free_ (); delete v; return 0; }
	return v;
}
extern "C" void ak_reverb_set (ak_reverb *v, float room, float damp, float wet, float width)
{
	if (v == 0) return;
	if (room >= 0) v->r.roomSize = room;
	if (damp >= 0) v->r.damp = damp;
	if (wet >= 0) v->r.wet = wet;
	if (width >= 0) v->r.width = width;
	v->r.update ();
}
extern "C" void ak_reverb_process (ak_reverb *v, const float *in, float *left, float *right, int frames)
{
	if (v != 0) v->r.process (in, left, right, frames);
}
extern "C" void ak_reverb_mute (ak_reverb *v)		{ if (v != 0) v->r.mute (); }
extern "C" void ak_reverb_free (ak_reverb *v)		{ if (v != 0) { v->r.free_ (); delete v; } }

struct ak_chorus { ms::Chorus c; };
extern "C" ak_chorus *ak_chorus_new (int rate, float delay_s, float depth_s, float hz)
{
	ak_chorus *v = new ak_chorus;
	memset ((void *) v, 0, sizeof *v);
	if (!v->c.init (rate, delay_s, depth_s, hz)) { v->c.free_ (); delete v; return 0; }
	return v;
}
extern "C" void ak_chorus_process (ak_chorus *v, const float *inL, const float *inR, float *outL, float *outR, int frames)
{
	if (v != 0) v->c.process (inL, inR, outL, outR, frames);
}
extern "C" void ak_chorus_mute (ak_chorus *v)		{ if (v != 0) v->c.mute (); }
extern "C" void ak_chorus_free (ak_chorus *v)		{ if (v != 0) { v->c.free_ (); delete v; } }
