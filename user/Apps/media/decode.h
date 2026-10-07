//
// Apps/media/decode.h -- Media Player's decoders: a file of any of its formats as s16 stereo frames
// at its own rate (Decoder), and the same at the output's rate, SOUND_RATE (Stream: a linear
// resampler when they differ). MP3 (minimp3), Ogg Vorbis (stb_vorbis), FLAC (dr_flac), WAV
// (dr_wav) -- codecs.h --; MIDI: midi.h (MeltySynth through a SoundFont).
//
//   char err[96]; Decoder *d = decoder_open ("SD:/Music/a.flac", err, sizeof err);
//   Stream s (d); short buf[2 * 1024]; int n = s.read (buf, 1024);   ... s.seekMs (60000);
//
// Files are read through the kapi (kapi_open / kapi_read / kapi_seek): any size, any volume.
//
#ifndef _media_decode_h
#define _media_decode_h

#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>
#include "appkit/appkit.h"
#include "codecs.h"

namespace media {

typedef long long i64;
typedef unsigned long long u64;

// ---- a file read through the kapi --------------------------------------------------------------------------
struct Src
{
	void *h; u64 pos, size;
	Src () : h (0), pos (0), size (0) {}
	~Src () { close (); }
	bool open (const char *path)
	{
		h = kapi_open (path);
		if (!h) return false;
		size = kapi_fsize64 (h); pos = 0;
		return true;
	}
	void close () { if (h) kapi_close (h); h = 0; }
	size_t read (void *buf, size_t n)
	{
		size_t got = 0;
		while (got < n)
		{
			unsigned k = n - got > (1u << 20) ? (1u << 20) : (unsigned) (n - got);
			int r = kapi_read (h, (char *) buf + got, k);
			if (r <= 0) break;
			got += (size_t) r;
			if ((unsigned) r < k) break;
		}
		pos += got;
		return got;
	}
	bool seek (u64 p)
	{
		if (p > size) p = size;
		if (p == pos) return true;
		if (kapi_seek (h, p) != 0) return false;
		pos = p; return true;
	}
};

static inline const char *ext_of (const char *path)
{
	const char *d = 0;
	for (const char *p = path; *p; p++) { if (*p == '.') d = p; else if (*p == '/') d = 0; }
	return d ? d + 1 : "";
}
static inline bool ext_is (const char *path, const char *e) { return !strcasecmp (ext_of (path), e); }

// ---- a decoder: frames at the file's own rate ---------------------------------------------------------------
class Decoder
{
public:
	int rate, channels;				// the source's (channels: what the file has; read gives stereo)
	i64 frames;					// its length in frames at `rate` (0: not known)
	char format[8];					// "MP3", "FLAC"...
	int bits, kbps;					// for its description (0: not known)
	Decoder () : rate (44100), channels (2), frames (0), bits (16), kbps (0) { format[0] = 0; }
	virtual ~Decoder () {}
	virtual int read (short *out, int n) = 0;	// up to n stereo frames -> how many (0: the end)
	virtual bool seek (i64 frame) = 0;
};

// mono to stereo, in place (n frames: the first n shorts are the mono ones)
static inline void mono_to_stereo (short *b, int n) { for (int i = n - 1; i >= 0; i--) { b[2 * i + 1] = b[i]; b[2 * i] = b[i]; } }
// more than two channels: the first two kept
static inline void keep_two (short *b, int n, int ch) { for (int i = 0; i < n; i++) { short l = b[i * ch], r = b[i * ch + 1]; b[2 * i] = l; b[2 * i + 1] = r; } }

// ---- MP3 ------------------------------------------------------------------------------------------------
class Mp3Decoder : public Decoder
{
	Src src; mp3dec_io_t io; mp3dec_ex_t dec; bool ok; short *tmp; int tmpCap;
	static size_t rd (void *buf, size_t size, void *u) { return ((Src *) u)->read (buf, size); }
	static int sk (uint64_t p, void *u) { return ((Src *) u)->seek (p) ? 0 : -1; }
public:
	Mp3Decoder () : ok (false), tmp (0), tmpCap (0) { strcpy (format, "MP3"); }
	~Mp3Decoder () { if (ok) mp3dec_ex_close (&dec); delete[] tmp; }
	bool open (const char *path)
	{
		if (!src.open (path)) return false;
		io.read = rd; io.read_data = &src; io.seek = sk; io.seek_data = &src;
		if (mp3dec_ex_open_cb (&dec, &io, MP3D_SEEK_TO_SAMPLE) != 0 || dec.info.channels <= 0) return false;
		ok = true;
		rate = dec.info.hz; channels = dec.info.channels;
		frames = (i64) (dec.samples / (u64) channels);
		kbps = dec.info.bitrate_kbps;
		if (frames > 0 && rate > 0) kbps = (int) ((i64) src.size * 8 / (frames * 1000 / rate + 1));
		return true;
	}
	int read (short *out, int n) override
	{
		if (!ok) return 0;
		size_t want = (size_t) n * channels;
		short *b = out;
		if (channels > 2) { if (tmpCap < (int) want) { delete[] tmp; tmp = new short[want]; tmpCap = (int) want; } b = tmp; }
		size_t got = mp3dec_ex_read (&dec, b, want);
		int f = (int) (got / channels);
		if (channels == 1) mono_to_stereo (out, f);
		else if (channels > 2) { keep_two (b, f, channels); memcpy (out, b, (size_t) f * 4); }
		return f;
	}
	bool seek (i64 frame) override { return ok && mp3dec_ex_seek (&dec, (u64) frame * channels) == 0; }
};

// ---- FLAC -------------------------------------------------------------------------------------------------
static size_t dr_rd (void *u, void *buf, size_t n) { return ((Src *) u)->read (buf, n); }
static drflac_bool32 fl_sk (void *u, int off, drflac_seek_origin o)
{
	Src *s = (Src *) u;
	i64 p = o == DRFLAC_SEEK_SET ? off : o == DRFLAC_SEEK_CUR ? (i64) s->pos + off : (i64) s->size + off;
	return p >= 0 && s->seek ((u64) p);
}
static drflac_bool32 fl_tell (void *u, drflac_int64 *c) { *c = (drflac_int64) ((Src *) u)->pos; return 1; }
class FlacDecoder : public Decoder
{
	Src src; drflac *f; short *tmp; int tmpCap;
public:
	FlacDecoder () : f (0), tmp (0), tmpCap (0) { strcpy (format, "FLAC"); }
	~FlacDecoder () { if (f) drflac_close (f); delete[] tmp; }
	bool open (const char *path)
	{
		if (!src.open (path)) return false;
		f = drflac_open (dr_rd, fl_sk, fl_tell, &src, 0);
		if (!f || f->channels == 0) return false;
		rate = (int) f->sampleRate; channels = f->channels; frames = (i64) f->totalPCMFrameCount; bits = f->bitsPerSample;
		if (frames > 0) kbps = (int) ((i64) src.size * 8 / (frames * 1000 / rate + 1));
		return true;
	}
	int read (short *out, int n) override
	{
		short *b = channels > 2 ? (tmpCap < n * channels ? (delete[] tmp, tmp = new short[n * channels], tmpCap = n * channels, tmp) : tmp) : out;
		int got = (int) drflac_read_pcm_frames_s16 (f, (drflac_uint64) n, b);
		if (channels == 1) mono_to_stereo (out, got);
		else if (channels > 2) { keep_two (b, got, channels); memcpy (out, b, (size_t) got * 4); }
		return got;
	}
	bool seek (i64 frame) override { return drflac_seek_to_pcm_frame (f, (drflac_uint64) frame); }
};

// ---- WAV ---------------------------------------------------------------------------------------------------
static drwav_bool32 wv_sk (void *u, int off, drwav_seek_origin o)
{
	Src *s = (Src *) u;
	i64 p = o == DRWAV_SEEK_SET ? off : o == DRWAV_SEEK_CUR ? (i64) s->pos + off : (i64) s->size + off;
	return p >= 0 && s->seek ((u64) p);
}
static drwav_bool32 wv_tell (void *u, drwav_int64 *c) { *c = (drwav_int64) ((Src *) u)->pos; return 1; }
class WavDecoder : public Decoder
{
	Src src; drwav w; bool ok; short *tmp; int tmpCap;
public:
	WavDecoder () : ok (false), tmp (0), tmpCap (0) { strcpy (format, "WAV"); }
	~WavDecoder () { if (ok) drwav_uninit (&w); delete[] tmp; }
	bool open (const char *path)
	{
		if (!src.open (path)) return false;
		if (!drwav_init (&w, dr_rd, wv_sk, wv_tell, &src, 0)) return false;
		ok = true;
		rate = (int) w.sampleRate; channels = w.channels; frames = (i64) w.totalPCMFrameCount; bits = w.bitsPerSample;
		kbps = rate * channels * bits / 1000;
		return channels > 0;
	}
	int read (short *out, int n) override
	{
		short *b = channels > 2 ? (tmpCap < n * channels ? (delete[] tmp, tmp = new short[n * channels], tmpCap = n * channels, tmp) : tmp) : out;
		int got = (int) drwav_read_pcm_frames_s16 (&w, (drwav_uint64) n, b);
		if (channels == 1) mono_to_stereo (out, got);
		else if (channels > 2) { keep_two (b, got, channels); memcpy (out, b, (size_t) got * 4); }
		return got;
	}
	bool seek (i64 frame) override { return drwav_seek_to_pcm_frame (&w, (drwav_uint64) frame); }
};

// ---- Ogg Vorbis (the file in memory: stb_vorbis reads it there) ---------------------------------------------------
class OggDecoder : public Decoder
{
	unsigned char *data; stb_vorbis *v;
public:
	OggDecoder () : data (0), v (0) { strcpy (format, "OGG"); }
	~OggDecoder () { if (v) stb_vorbis_close (v); delete[] data; }
	bool open (const char *path)
	{
		Src s; if (!s.open (path) || s.size == 0 || s.size > (256u << 20)) return false;
		data = new unsigned char[(size_t) s.size];
		if (s.read (data, (size_t) s.size) != s.size) return false;
		int err = 0;
		v = stb_vorbis_open_memory (data, (int) s.size, &err, 0);
		if (!v) return false;
		stb_vorbis_info in = stb_vorbis_get_info (v);
		rate = (int) in.sample_rate; channels = in.channels;
		frames = (i64) stb_vorbis_stream_length_in_samples (v);
		kbps = frames > 0 ? (int) ((i64) s.size * 8 / (frames * 1000 / rate + 1)) : 0;
		return channels > 0;
	}
	int read (short *out, int n) override { return stb_vorbis_get_samples_short_interleaved (v, 2, out, n * 2); }	// (mono: duplicated by it)
	bool seek (i64 frame) override { return stb_vorbis_seek (v, (unsigned) frame) != 0; }
};

// ---- the output's rate: a linear resampler over a decoder ------------------------------------------------------
class Stream
{
public:
	Decoder *dec;
	Stream (Decoder *d) : dec (d), inBuf (new short[2 * IN]), inN (0), inPos (0), frac (0), l0 (0), r0 (0), l1 (0), r1 (0), primed (false), done (false), eof (false)
	{ step = dec->rate > 0 ? ((i64) dec->rate << 32) / SOUND_RATE : (1ll << 32); }
	~Stream () { delete[] inBuf; delete dec; }
	i64 lengthMs () const { return dec->rate > 0 ? dec->frames * 1000 / dec->rate : 0; }
	// up to n frames at SOUND_RATE -> how many (0: the end)
	int read (short *out, int n)
	{
		if (dec->rate == SOUND_RATE) return dec->read (out, n);
		if (eof) return 0;
		int k = 0;
		while (k < n)
		{
			if (!primed) { if (!next (&l1, &r1)) { eof = true; break; } primed = true; l0 = l1; r0 = r1; if (!next (&l1, &r1)) { l1 = l0; r1 = r0; } }
			int f = (int) (frac >> 16);			// (frac: 32 bits of fraction; 16 of them used)
			out[2 * k] = (short) (l0 + (int) (((i64) (l1 - l0) * f) >> 16));
			out[2 * k + 1] = (short) (r0 + (int) (((i64) (r1 - r0) * f) >> 16));
			k++;
			frac += step;
			while (frac >= (1ll << 32))
			{
				frac -= 1ll << 32;
				l0 = l1; r0 = r1;
				if (!next (&l1, &r1)) { if (done) { eof = true; return k; } l1 = l0; r1 = r0; }
			}
		}
		return k;
	}
	bool seekMs (i64 ms)
	{
		i64 f = ms * dec->rate / 1000;
		if (f < 0) f = 0;
		if (dec->frames > 0 && f > dec->frames) f = dec->frames;
		inN = inPos = 0; frac = 0; primed = false; done = false; eof = false;
		return dec->seek (f);
	}
private:
	enum { IN = 2048 };
	short *inBuf; int inN, inPos; i64 frac, step; int l0, r0, l1, r1; bool primed, done, eof;
	bool next (int *l, int *r)
	{
		if (inPos >= inN)
		{
			if (done) return false;
			inN = dec->read (inBuf, IN); inPos = 0;
			if (inN <= 0) { done = true; inN = 0; return false; }
		}
		*l = inBuf[2 * inPos]; *r = inBuf[2 * inPos + 1]; inPos++;
		return true;
	}
};

} // namespace media

#include "midi.h"

namespace media {
// The decoder for a file, by its extension (then its first bytes). 0: err says why.
static inline Decoder *decoder_open (const char *path, char *err, int cap)
{
	err[0] = 0;
	unsigned char head[12] = { 0 };
	{ Src s; if (!s.open (path)) { snprintf (err, cap, "The file cannot be opened."); return 0; } s.read (head, sizeof head); }
	Decoder *d = 0; bool ok = false;
	if (ext_is (path, "mid") || ext_is (path, "midi") || ext_is (path, "kar") || !memcmp (head, "MThd", 4))
	{ MidiDecoder *m = new MidiDecoder; ok = m->open (path, err, cap); d = m; }
	else if (ext_is (path, "flac") || !memcmp (head, "fLaC", 4)) { FlacDecoder *f = new FlacDecoder; ok = f->open (path); d = f; }
	else if (ext_is (path, "ogg") || ext_is (path, "oga") || !memcmp (head, "OggS", 4)) { OggDecoder *o = new OggDecoder; ok = o->open (path); d = o; }
	else if (ext_is (path, "wav") || !memcmp (head, "RIFF", 4)) { WavDecoder *w = new WavDecoder; ok = w->open (path); d = w; }
	else { Mp3Decoder *m = new Mp3Decoder; ok = m->open (path); d = m; }
	if (!ok) { if (!err[0]) snprintf (err, cap, "This file cannot be played (%s: damaged, or a kind not known).", d->format[0] ? d->format : "?"); delete d; return 0; }
	return d;
}
} // namespace media

#endif
