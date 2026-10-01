/*
 * user/av/av_int.h -- the media library's insides: the demuxer's byte window and the
 * containers' parsers (av_mkv.c, av_mp4.c, av_riff.c, av_flac.c), the codecs' table.
 */
#ifndef ONYX_AV_INT_H
#define ONYX_AV_INT_H

#include <stdlib.h>
#include <string.h>
#include "av.h"

#define AV_MAX_TRACKS 8

struct av_demux;

/* a container's parser */
struct av_fmt_ops {
	const char *name;
	void *(*create)(struct av_demux *d);
	void (*destroy)(void *priv);
	/* the next packet from the window: AV_OK, AV_AGAIN (bytes needed: d->want set), AV_EOF, AV_ERR */
	int (*read)(struct av_demux *d, void *priv, struct av_packet *pkt);
	/* the bytes now start at offset pos, which is not where the parser was (a seek, a jump) */
	void (*restart)(struct av_demux *d, void *priv, int64_t pos);
	/* where to go to reach t (-1 unknown); the parser is set to expect it */
	int64_t (*seek)(struct av_demux *d, void *priv, av_us t, av_us *at);
	/* MSE: the parser's partial state dropped (abort()) -- back to "expect a segment" */
	void (*reset)(struct av_demux *d, void *priv);
};

struct av_demux {
	int fmt;
	const struct av_fmt_ops *ops;
	void *priv;
	/* the window: bytes [base, base + len) of the stream */
	uint8_t *buf;
	size_t len, cap, off;		/* off: bytes at the front already consumed (dropped lazily) */
	int64_t base;
	int64_t skip_from;		/* the window was emptied past its end: bytes from here up to base are dropped as they come */
	int64_t want;			/* where the parser needs bytes next */
	int ended;			/* no more bytes will come */
	int error;
	struct av_track tracks[AV_MAX_TRACKS];
	int ntracks;
	int init_new;			/* the tracks were (re)defined */
	av_us duration;
	char name[12];
};

/* the window: a pointer to n bytes at stream offset pos, or NULL (not there: d->want = pos
 * when they are past the end). */
const uint8_t *av__peek(struct av_demux *d, int64_t pos, size_t n);
/* bytes available from pos on (0 if pos is outside the window) */
size_t av__avail(struct av_demux *d, int64_t pos);
/* the bytes before pos are not needed any more */
void av__consume(struct av_demux *d, int64_t pos);
/* end of the window (stream offset) */
static inline int64_t av__end(const struct av_demux *d) { return d->base + (int64_t) d->len; }
/* a packet's data copied from the window (NULL: no memory) */
uint8_t *av__copy(struct av_demux *d, int64_t pos, size_t n);
/* a track slot (the next free one), zeroed; NULL if full */
struct av_track *av__new_track(struct av_demux *d);
void av__clear_tracks(struct av_demux *d);
/* MSE abort(): the window emptied, the parser back to expecting a segment */
void av__demux_reset(struct av_demux *d);

/* big-endian / little-endian readers */
static inline uint32_t av_rb16(const uint8_t *p) { return (uint32_t) p[0] << 8 | p[1]; }
static inline uint32_t av_rb24(const uint8_t *p) { return (uint32_t) p[0] << 16 | (uint32_t) p[1] << 8 | p[2]; }
static inline uint32_t av_rb32(const uint8_t *p) { return (uint32_t) p[0] << 24 | (uint32_t) p[1] << 16 | (uint32_t) p[2] << 8 | p[3]; }
static inline uint64_t av_rb64(const uint8_t *p) { return (uint64_t) av_rb32(p) << 32 | av_rb32(p + 4); }
static inline uint32_t av_rl16(const uint8_t *p) { return (uint32_t) p[1] << 8 | p[0]; }
static inline uint32_t av_rl32(const uint8_t *p) { return (uint32_t) p[3] << 24 | (uint32_t) p[2] << 16 | (uint32_t) p[1] << 8 | p[0]; }

/* the containers */
extern const struct av_fmt_ops av_mkv_ops, av_mp4_ops, av_wav_ops, av_flac_ops, av_mp3_ops;
/* an MPEG audio stream's start (av_mp3.c) */
int av__mp3_probe(const uint8_t *p, size_t n);

/* a codec string ("vp09.00.10.08") for a track from its setup data, when the container gives it */
void av__codec_string(struct av_track *t);

/* the codecs: one implementation */
struct av_codec_impl {
	int codec;
	int kind;
	const char *name, *library;
	int smooth_w, smooth_h, smooth_fps;
	void *(*open)(const struct av_track *t);
	int (*send)(void *c, const struct av_packet *p);	/* p NULL: drain */
	int (*receive)(void *c, struct av_frame *f);
	void (*flush)(void *c);
	void (*close)(void *c);
	/* profile check on an RFC 6381 string (NULL: every profile) -> 1 decodable */
	int (*profile_ok)(const char *s);
};
extern const struct av_codec_impl av_pcm_codec, av_alaw_codec, av_ulaw_codec, av_i420_codec,
	av_flac_codec, av_mp3_codec;
#ifdef AV_WITH_VPX
extern const struct av_codec_impl av_vp8_codec, av_vp9_codec;
#endif
#ifdef AV_WITH_OPUS
extern const struct av_codec_impl av_opus_codec;
#endif
#ifdef AV_WITH_DAV1D
extern const struct av_codec_impl av_av1_codec;
#endif
#ifdef AV_WITH_VORBIS
extern const struct av_codec_impl av_vorbis_codec;
#endif

/* the tests' stand-in decoders (av_stub.c): NULL unless av_codec_enable_stubs () */
const struct av_codec_impl *av__stub_of(int codec);

/* a FLAC frame's length in a buffer of n bytes starting at a frame header (CRC-checked) -> its
 * size, 0 if more bytes are needed (and more are coming: !final), -1 not a frame there */
long av__flac_frame_size(const uint8_t *p, size_t n, int final, int *blocksize, uint64_t *number,
		int *variable);

#endif
