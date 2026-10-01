/*
 * user/av/av.h -- Onyx's media library: demux -> decode -> frames / PCM, and a player engine.
 *
 * Shared by Jet Browser (<video>, <audio>, Media Source Extensions: qjs_media.c) and the Media
 * Player app. Plain C99, no allocation outside malloc, threads through user/av/av_os.h (the kapi
 * on Onyx and in the PC builds that link a kapi; pthreads with AV_POSIX for the standalone tools).
 * docs/03-DEVELOPER-GUIDE.md "The media library" is the guide; this header is the reference.
 *
 * The layers, each usable alone:
 *   1. Containers (av_demux_*): WebM / Matroska, ISO BMFF (MP4, fragmented MP4 -- MSE's), WAV,
 *      native FLAC, MPEG audio (MP3). Bytes are fed in stream order (a push parser: a network stream, MSE appends)
 *      or from where the demuxer asks (av_demux_want: a file, Range requests); packets come out
 *      with their track, times (microseconds) and key flag.
 *   2. Codecs (av_decoder_*): a packet in, frames out. Video frames are planar YUV (I420) or
 *      RGBA; audio frames are interleaved float PCM. The decoders built in are listed by
 *      av_codec_list (); the big ones (VP9, AV1, H.264, Opus, AAC...) are glue to libraries that
 *      are compiled in only when present (AV_WITH_* -- docs/06 §44).
 *   3. Conversion: av_yuv_to_rgb (NEON on AArch64), av_resampler (any rate / channels -> s16
 *      stereo at the output's rate, with a volume).
 *   4. The store (av_store_*): MSE's coded frames by track, buffered ranges, removal, eviction.
 *   5. The player (av_player_*): a store (or a file) played -- decoding threads, the audio output
 *      (the kapi's sound by default), the video frames queued and handed over at their time, the
 *      clock (the audio's when there is sound), seeking, rate, volume, the readyState.
 */
#ifndef ONYX_AV_H
#define ONYX_AV_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int64_t av_us;			/* a time in microseconds */
#define AV_NOTIME INT64_MIN
#define AV_US 1000000LL

/* results */
enum {
	AV_OK = 0,		/* done / a packet or frame was returned */
	AV_AGAIN = 1,		/* more input needed (feed bytes / send a packet) */
	AV_EOF = 2,		/* the end: nothing more will come */
	AV_ERR = -1,		/* malformed data */
	AV_ENOMEM = -2,
	AV_EUNSUP = -3,		/* a format / codec / feature not supported (encrypted...) */
	AV_EFULL = -4		/* a store over its quota (MSE's QuotaExceededError) */
};

enum av_kind { AV_VIDEO = 1, AV_AUDIO = 2 };

enum av_codec_id {
	AV_C_NONE = 0,
	/* video */
	AV_C_VP8, AV_C_VP9, AV_C_AV1, AV_C_H264, AV_C_HEVC, AV_C_MJPEG,
	AV_C_I420,		/* uncompressed planar 4:2:0 (Matroska V_UNCOMPRESSED / MP4 'i420'): tests */
	/* audio */
	AV_C_OPUS = 64, AV_C_VORBIS, AV_C_AAC, AV_C_MP3, AV_C_FLAC,
	AV_C_PCM_S16LE, AV_C_PCM_S16BE, AV_C_PCM_S24LE, AV_C_PCM_S24BE, AV_C_PCM_S32LE,
	AV_C_PCM_F32LE, AV_C_PCM_F64LE, AV_C_PCM_U8, AV_C_ALAW, AV_C_ULAW
};

/* pixel formats of a video frame / of the RGB conversion's output */
enum av_pix { AV_PIX_I420 = 1, AV_PIX_RGBA, AV_PIX_BGRA };
/* colour: the matrix of a YUV frame, and its range */
enum { AV_CS_BT601 = 0, AV_CS_BT709 = 1, AV_CS_BT2020 = 2 };

struct av_track {
	int number;		/* the container's track number / ID */
	int kind;		/* AV_VIDEO / AV_AUDIO; 0: a track the library does not play */
	int codec;		/* enum av_codec_id; AV_C_NONE: unknown codec */
	char codec_id[32];	/* the container's name (Matroska "V_VP9", MP4 "avc1") */
	char codec_str[48];	/* RFC 6381 when known ("vp09.00.21.08", "avc1.4d401e", "opus") */
	int encrypted;		/* 1: the frames are encrypted (EME): not playable */
	/* video */
	int width, height;	/* coded size in pixels */
	int dwidth, dheight;	/* display size (0: = the coded one) */
	double fps;		/* nominal frame rate, 0 unknown */
	/* audio */
	int rate, channels, bits;
	/* both */
	uint8_t *extra;		/* codec setup: Matroska CodecPrivate, avcC, vpcC, av1C, dOps, esds' DSI */
	size_t extra_len;
	av_us delay;		/* codec delay to drop at the start (Opus pre-skip) */
	av_us seek_preroll;	/* audio decoded before a seek point (Opus: 80 ms) */
	av_us default_dur;	/* a frame's duration when the container gives none */
	av_us duration;		/* the track's length, 0 unknown */
	int lang_default;	/* the container's "default track" flag */
};

struct av_packet {
	int track;		/* index into the demuxer's tracks (av_demux_track) */
	av_us pts, dts, dur;	/* dur 0: unknown */
	int key;		/* a random access point */
	int64_t pos;		/* byte offset of its data in the stream, -1 unknown */
	uint8_t *data;		/* malloc'd: the caller's (av_packet_free) */
	size_t size;
	av_us discard_end;	/* Matroska DiscardPadding: audio to drop at its end */
};
void av_packet_free(struct av_packet *p);

/* ---- 1. containers ---------------------------------------------------------------------- */

enum av_format { AV_FMT_UNKNOWN = 0, AV_FMT_MKV, AV_FMT_MP4, AV_FMT_WAV, AV_FMT_FLAC, AV_FMT_MP3 };

/* the container of a stream from its first bytes (at least 12): AV_FMT_* */
int av_probe(const uint8_t *p, size_t n);
/* the container of a MIME type ("video/webm", "audio/mp4; codecs=..."), AV_FMT_UNKNOWN if none */
int av_format_of_mime(const char *mime);

struct av_demux;
/* fmt AV_FMT_UNKNOWN: probed from the first bytes fed */
struct av_demux *av_demux_new(int fmt);
void av_demux_free(struct av_demux *d);
/* Bytes of the stream starting at offset pos. Feeding at the offset av_demux_want gives (the
 * end of the bytes fed, or another place: an MP4's moov after its mdat, a seek) continues;
 * any other offset is a discontinuity (the parser restarts there at a top-level element). */
int av_demux_feed(struct av_demux *d, int64_t pos, const void *p, size_t n);
/* MSE: bytes appended continue the stream whatever their file offset (a segment) */
int av_demux_append(struct av_demux *d, const void *p, size_t n);
/* no more bytes after those fed: the packets left come out, then AV_EOF */
void av_demux_end(struct av_demux *d);
/* the next packet: AV_OK, AV_AGAIN (feed from av_demux_want), AV_EOF, AV_ERR, AV_EUNSUP */
int av_demux_read(struct av_demux *d, struct av_packet *pkt);
/* the stream offset the demuxer wants next (where to feed / read from) */
int64_t av_demux_want(struct av_demux *d);
int av_demux_ntracks(const struct av_demux *d);
const struct av_track *av_demux_track(const struct av_demux *d, int i);
/* 1 once after the tracks were (re)defined: MSE's "initialization segment received" */
int av_demux_take_init(struct av_demux *d);
/* the stream's duration (0 unknown) */
av_us av_demux_duration(const struct av_demux *d);
/* Where to restart to reach time t: the offset to feed from (the parser is reset to expect it),
 * *at the time of the random access point there; -1: unknown (feed from the start / scan). */
int64_t av_demux_seek(struct av_demux *d, av_us t, av_us *at);
/* the container's name for messages ("webm", "matroska", "mp4", "wav", "flac") */
const char *av_demux_name(const struct av_demux *d);

/* ---- 2. codecs -------------------------------------------------------------------------- */

struct av_frame {
	int kind;
	av_us pts, dur;
	/* video */
	int pix;		/* AV_PIX_I420 / AV_PIX_RGBA / AV_PIX_BGRA */
	int width, height;
	uint8_t *plane[3];
	int stride[3];
	int colorspace, full_range;
	/* audio */
	int rate, channels;
	int samples;		/* per channel */
	float *pcm;		/* interleaved, -1..1 */
};

struct av_decoder;
/* NULL: the track's codec is not supported (av_codec_supported says why) */
struct av_decoder *av_decoder_new(const struct av_track *t);
void av_decoder_free(struct av_decoder *d);
/* a packet in (NULL: drain the frames held) -> AV_OK, AV_ERR (a bad packet: skipped) */
int av_decoder_send(struct av_decoder *d, const struct av_packet *p);
/* a frame out -> AV_OK (valid until the next call), AV_AGAIN */
int av_decoder_receive(struct av_decoder *d, struct av_frame *f);
/* drop what is held (a seek) */
void av_decoder_flush(struct av_decoder *d);
const char *av_decoder_name(const struct av_decoder *d);

/* What the build decodes, and how well on the Pi 4 (one A72 core at 1.5 GHz, NEON):
 * the largest frame and rate it decodes smoothly (0: no limit known). */
struct av_codec_info {
	int codec;
	int kind;
	const char *name;	/* "vp9", "opus"... */
	const char *library;	/* "libvpx 1.15", "built in" */
	int smooth_w, smooth_h, smooth_fps;
};
/* the codecs compiled in: n entries */
const struct av_codec_info *av_codec_list(int *n);
/* the codec of an RFC 6381 string / a short name ("vp9", "vp09.00.10.08", "avc1.42E01E",
 * "mp4a.40.2", "opus"), and whether this build decodes that profile: AV_C_* or AV_C_NONE */
int av_codec_of_string(const char *s, int *profile_ok);
/* A MIME type with its parameters ("video/webm; codecs=\"vp9, opus\"; width=1920; height=1080;
 * framerate=30"): 2 "probably", 1 "maybe" (no codecs parameter), 0 no -- canPlayType,
 * MediaSource.isTypeSupported (mse 1: the byte stream formats MSE has, and no "maybe").
 * *smooth: 1 if the size / rate given decode in real time on the Pi (MediaCapabilities). */
int av_type_supported(const char *mime, int mse, int *smooth);

/* the tests: stand-in decoders (grey frames, silence) for the codecs not built in -- a site's
 * media path runs on its real streams (av_stub.c) */
void av_codec_enable_stubs(void);

/* ---- 3. conversion ---------------------------------------------------------------------- */

/* a YUV 4:2:0 frame (f->pix AV_PIX_I420) into 32-bit pixels (AV_PIX_RGBA: bytes R G B A,
 * AV_PIX_BGRA: B G R A), alpha 255, at dst with dst_stride bytes a row (w x h: the frame's) */
void av_yuv_to_rgb(const struct av_frame *f, uint8_t *dst, int dst_stride, int pix);

struct av_resampler;
/* in: in_rate Hz, in_ch channels (float); out: s16 stereo at out_rate */
struct av_resampler *av_resampler_new(int in_rate, int in_ch, int out_rate);
void av_resampler_free(struct av_resampler *r);
/* in_n frames in; up to out_max frames written to out (s16 L R) -> frames written. ratio: the
 * playback rate (1: normal; 2: twice as fast -- the pitch follows) */
int av_resample(struct av_resampler *r, const float *in, int in_n, int16_t *out, int out_max,
		double ratio, float volume);
/* frames of output one frame of input makes, about: to size out */
double av_resampler_factor(const struct av_resampler *r, double ratio);

/* ---- 4. the store (MSE's coded frames) ------------------------------------------------ */

struct av_range { av_us start, end; };

struct av_store;
struct av_store *av_store_new(void);
void av_store_free(struct av_store *s);
/* a source (MSE SourceBuffer / a file): its id >= 0. quota: bytes it may hold (0 default) */
int av_store_add_source(struct av_store *s, const char *mime, size_t quota);
void av_store_remove_source(struct av_store *s, int src);
/* MSE appendBuffer's segment parser loop: the bytes parsed into coded frames.
 * -> AV_OK, AV_ERR (a parse error: MSE's decode error), AV_EUNSUP, AV_EFULL (nothing taken) */
int av_store_append(struct av_store *s, int src, const void *p, size_t n);
/* mode: 0 segments, 1 sequence; offset: timestampOffset; window: appendWindow */
void av_store_set_offset(struct av_store *s, int src, av_us offset);
av_us av_store_get_offset(struct av_store *s, int src);
void av_store_set_mode(struct av_store *s, int src, int sequence);
void av_store_set_window(struct av_store *s, int src, av_us start, av_us end);
/* MSE changeType(): the source's next bytes are of another type (its frames kept) */
int av_store_change_type(struct av_store *s, int src, const char *mime);
/* MSE abort(): the parser reset (a partial segment dropped) */
void av_store_reset_parser(struct av_store *s, int src);
/* MSE remove(start, end) */
void av_store_remove(struct av_store *s, int src, av_us start, av_us end);
/* the source's buffered ranges (the intersection of its tracks'): n written, at most max */
int av_store_buffered(struct av_store *s, int src, struct av_range *r, int max);
/* 1 once the source's init segment was parsed */
int av_store_has_init(struct av_store *s, int src);
/* the source's tracks (its demuxer's) */
int av_store_ntracks(struct av_store *s, int src);
const struct av_track *av_store_track(struct av_store *s, int src, int i);
/* MSE endOfStream (1) / reopened by an append (0) */
void av_store_set_eos(struct av_store *s, int eos);
/* the largest end time buffered by any source */
av_us av_store_end(struct av_store *s);
/* the file mode: bytes of the whole resource at offset pos (a discontinuity restarts) */
int av_store_feed(struct av_store *s, int src, int64_t pos, const void *p, size_t n);
void av_store_feed_end(struct av_store *s, int src);
/* the file mode: where the source's demuxer wants bytes (-1: nowhere: all is there) */
int64_t av_store_want(struct av_store *s, int src);
/* the duration the containers give (0 unknown) */
av_us av_store_duration(struct av_store *s);

/* ---- 5. the player ------------------------------------------------------------------- */

/* readyState, as HTMLMediaElement's */
enum { AV_HAVE_NOTHING = 0, AV_HAVE_METADATA, AV_HAVE_CURRENT_DATA, AV_HAVE_FUTURE_DATA, AV_HAVE_ENOUGH_DATA };

/* the audio output: the kapi's sound when the host gives none */
struct av_audio_out {
	void *ctx;
	/* take the output (rate: its frames a second, stereo s16) -> 1 ok, 0 busy / none */
	int (*open)(void *ctx, int *rate);
	/* frames written now (never waits) */
	unsigned (*write)(void *ctx, const int16_t *frames, unsigned n);
	/* frames written and not yet heard (the queue + the device's latency) */
	unsigned (*queued)(void *ctx);
	void (*close)(void *ctx);
};
/* the kapi's sound output (kapi_sound_acquire / kapi_sound_write / kapi_sound_status) */
const struct av_audio_out *av_audio_kapi(void);

struct av_player_status {
	av_us time;		/* the current playback position */
	av_us duration;		/* 0 unknown; AV_NOTIME infinite (a live stream) */
	int ready;		/* AV_HAVE_* */
	int paused, ended, seeking, waiting;	/* waiting: playing but stalled for data */
	int width, height;	/* the video's display size, 0 none yet */
	int has_audio, has_video;
	int sound;		/* 1: the audio is heard (the output was free) */
	unsigned decoded, dropped;	/* video frames, since the start */
	av_us decode_us;	/* the mean time of a video frame's decoding (us) */
	av_us sync_us;		/* A/V sync: the mean |a frame's time - the audio heard| when shown */
	int error;		/* 0, or AV_E* (MEDIA_ERR_DECODE / SRC_NOT_SUPPORTED) */
	int64_t want;		/* the file mode: an offset to feed from (a seek outside what came), -1 */
};

/* A new video frame for the host: pixels w x h in the format the host asked for */
struct av_video_out {
	const uint8_t *pixels;
	int width, height, stride;
	av_us pts;
	unsigned serial;	/* changes with each new frame */
};

struct av_player;
/* out_pix: the 32-bit layout of the video frames handed over (AV_PIX_RGBA / AV_PIX_BGRA);
 * audio: the output (NULL: the kapi's) */
struct av_player *av_player_new(int out_pix, const struct av_audio_out *audio);
void av_player_free(struct av_player *p);
/* its store: the host appends (MSE) or feeds (a file) into it */
struct av_store *av_player_store(struct av_player *p);
/* the file mode: open a file by path (the player's reader thread reads it): AV_OK / AV_ERR */
int av_player_open_file(struct av_player *p, const char *path);
void av_player_play(struct av_player *p);
void av_player_pause(struct av_player *p);
void av_player_seek(struct av_player *p, av_us t);
void av_player_set_rate(struct av_player *p, double rate);
void av_player_set_volume(struct av_player *p, float volume, int muted);
/* MSE: the media's duration as the page set it (0 unknown) */
void av_player_set_duration(struct av_player *p, av_us d);
/* the store changed (an append, a removal): the threads look again */
void av_player_kick(struct av_player *p);
/* The host's turn (its UI thread, every ~10-30 ms while playing): the status, and the video
 * frame to show now if a new one is due (*v filled, returns 1). */
int av_player_poll(struct av_player *p, struct av_player_status *st, struct av_video_out *v);

/* the time of the platform's clock (us), monotonic */
av_us av_now(void);

#ifdef __cplusplus
}
#endif
#endif
