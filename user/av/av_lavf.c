/*
 * user/av/av_lavf.c -- the containers Onyx's own parsers do not read (AVI, MPEG-TS / PS, FLV, ASF / WMV,
 * Ogg, RealMedia, 3GP's oddities...), read by FFmpeg's libavformat (AV_WITH_FFMPEG; GPL-2.0+).
 *
 * libavformat pulls its bytes (an AVIOContext's read / seek); the media library pushes them (av_demux_feed,
 * from where av_demux_want says). So libavformat runs on a thread of its own: its read callback waits
 * for the bytes it needs (`need`: what av_demux_want answers), which the parser's read (the feeder's
 * thread) copies from the demuxer's window into a cache of its own; the packets it reads are queued
 * and handed over by that read. A seek is asked of the thread (av_seek_frame on its index) and
 * answered with the offset it then waits for. The parser is "busy" (av_fmt_ops.busy) while packets
 * may still come: the store takes them when it is asked where to feed (av_store_want).
 */
#ifdef AV_WITH_FFMPEG
#include <stdio.h>
#include "av_int.h"
#include "av_os.h"
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>

#define CACHE_KEEP (1 << 20)		/* bytes kept behind libavformat's position (its small back-seeks) */
#define CACHE_AHEAD (8 << 20)		/* bytes wanted ahead of it (more: av_demux_want says -1) */
#define QMAX 512			/* packets queued at most */
#define QBYTES (24 << 20)

/* FFmpeg's state is per context, but its first-time initialisations (tables) are not guarded
 * without threads: the opens are made one at a time (av_ffmpeg.c's decoders too) */
av_lock_t av__ff_lock = AV_LOCK_INIT;
void av__ff_init(void)
{
	static int done;
	if (!done) {
		av_log_set_level(AV_LOG_QUIET);
		done = 1;
	}
}

struct lavf {
	struct av_demux *d;
	av_lock_t lk;
	av_thread_t th;
	volatile int quit;
	/* the bytes: [cbase, cbase + clen) */
	uint8_t *cbuf;
	size_t clen, ccap;
	int64_t cbase;
	int64_t rpos;			/* libavformat's read position */
	int64_t need;			/* the offset its read waits for, -1 none */
	int ended;			/* no bytes past the cache's end will come */
	int64_t size;
	/* the packets */
	struct av_packet q[QMAX];
	int qh, qn;
	size_t qbytes;
	/* the tracks, made by the thread */
	struct av_track tr[AV_MAX_TRACKS];
	int ntr, stream_of[AV_MAX_TRACKS], tracks_ready, tracks_given;
	av_us duration;
	av_us start;			/* the stream's first time (MPEG-TS / PS start at 1.4 s...): made 0 */
	char name[12];
	/* the thread's state */
	int failed, eof, finished;
	av_us seek_t;			/* AV_NOTIME none */
	unsigned seek_req, seek_started, seek_done;
};

/* ---- the AVIOContext's side (the thread) ---------------------------------------------------- */

static int io_read(void *opaque, uint8_t *buf, int size)
{
	struct lavf *l = (struct lavf *) opaque;

	for (;;) {
		av_lock(&l->lk);
		if (l->quit || (l->seek_req != l->seek_started && l->tracks_ready)) {
			/* (a seek asked while reading on: av_read_frame gives up, the loop seeks) */
			av_unlock(&l->lk);
			return AVERROR_EXIT;
		}
		if (l->rpos >= l->cbase && l->rpos < l->cbase + (int64_t) l->clen) {
			size_t off = (size_t) (l->rpos - l->cbase), n = l->clen - off;
			if (n > (size_t) size)
				n = (size_t) size;
			memcpy(buf, l->cbuf + off, n);
			l->rpos += (int64_t) n;
			l->need = -1;
			av_unlock(&l->lk);
			return (int) n;
		}
		if ((l->size >= 0 && l->rpos >= l->size) || (l->ended && l->rpos >= l->cbase + (int64_t) l->clen)) {
			l->need = -1;
			av_unlock(&l->lk);
			return AVERROR_EOF;
		}
		l->need = l->rpos;
		av_unlock(&l->lk);
		av_sleep_ms(2);
	}
}

static int64_t io_seek(void *opaque, int64_t off, int whence)
{
	struct lavf *l = (struct lavf *) opaque;
	int64_t r;

	av_lock(&l->lk);
	switch (whence & ~AVSEEK_FORCE) {
	case AVSEEK_SIZE: r = l->size; break;
	case SEEK_SET: r = l->rpos = off; break;
	case SEEK_CUR: r = l->rpos += off; break;
	case SEEK_END: r = l->size >= 0 ? (l->rpos = l->size + off) : -1; break;
	default: r = -1;
	}
	av_unlock(&l->lk);
	return r < 0 ? AVERROR(EINVAL) : r;
}

/* ---- the thread ---------------------------------------------------------------------------- */

static av_us to_us(int64_t t, AVRational tb)
{
	if (t == AV_NOPTS_VALUE)
		return AV_NOTIME;
	return (av_us) av_rescale_q(t, tb, (AVRational) { 1, 1000000 });
}

/* our codec of FFmpeg's id (a decoder of Onyx's own when there is one; else FFmpeg's) */
static int our_codec(enum AVCodecID id, int bits)
{
	switch (id) {
	case AV_CODEC_ID_VP8: return AV_C_VP8;
	case AV_CODEC_ID_VP9: return AV_C_VP9;
	case AV_CODEC_ID_AV1: return AV_C_AV1;
	case AV_CODEC_ID_H264: return AV_C_H264;
	case AV_CODEC_ID_HEVC: return AV_C_HEVC;
	case AV_CODEC_ID_MJPEG: return AV_C_MJPEG;
	case AV_CODEC_ID_OPUS: return AV_C_OPUS;
	case AV_CODEC_ID_VORBIS: return AV_C_VORBIS;
	case AV_CODEC_ID_AAC: return AV_C_AAC;
	case AV_CODEC_ID_MP3: return AV_C_MP3;
	case AV_CODEC_ID_FLAC: return AV_C_FLAC;
	case AV_CODEC_ID_PCM_S16LE: return AV_C_PCM_S16LE;
	case AV_CODEC_ID_PCM_S16BE: return AV_C_PCM_S16BE;
	case AV_CODEC_ID_PCM_U8: return AV_C_PCM_U8;
	case AV_CODEC_ID_PCM_ALAW: return AV_C_ALAW;
	case AV_CODEC_ID_PCM_MULAW: return AV_C_ULAW;
	default: (void) bits; return AV_C_FFMPEG;
	}
}

static void make_tracks(struct lavf *l, AVFormatContext *fc)
{
	unsigned i;
	int vbest = av_find_best_stream(fc, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
	int abest = av_find_best_stream(fc, AVMEDIA_TYPE_AUDIO, -1, vbest >= 0 ? vbest : -1, NULL, 0);

	for (i = 0; i < fc->nb_streams && l->ntr < AV_MAX_TRACKS; i++) {
		AVStream *st = fc->streams[i];
		AVCodecParameters *cp = st->codecpar;
		struct av_track *t;

		if (cp->codec_type != AVMEDIA_TYPE_VIDEO && cp->codec_type != AVMEDIA_TYPE_AUDIO)
			continue;
		if (st->disposition & AV_DISPOSITION_ATTACHED_PIC)
			continue;			/* (a cover in an audio file) */
		t = &l->tr[l->ntr];
		memset(t, 0, sizeof *t);
		l->stream_of[l->ntr] = (int) i;
		t->number = st->id ? st->id : (int) i + 1;
		t->kind = cp->codec_type == AVMEDIA_TYPE_VIDEO ? AV_VIDEO : AV_AUDIO;
		t->codec = our_codec(cp->codec_id, cp->bits_per_coded_sample);
		t->ff_id = (int) cp->codec_id;
		t->ff_tag = (int) cp->codec_tag;
		t->block_align = cp->block_align;
		t->bits_coded = cp->bits_per_coded_sample;
		t->bit_rate = cp->bit_rate;
		snprintf(t->codec_id, sizeof t->codec_id, "%s", avcodec_get_name(cp->codec_id));
		snprintf(t->codec_str, sizeof t->codec_str, "%s", avcodec_get_name(cp->codec_id));
		/* (Onyx's own decoders want the native layouts: PCM in WAV's) */
		if (t->codec != AV_C_FFMPEG && t->codec >= AV_C_PCM_S16LE && t->codec <= AV_C_ULAW && cp->bits_per_coded_sample == 0)
			t->codec = AV_C_FFMPEG;
		if (t->kind == AV_VIDEO) {
			AVRational sar = st->sample_aspect_ratio.num ? st->sample_aspect_ratio : cp->sample_aspect_ratio;
			t->width = cp->width;
			t->height = cp->height;
			if (sar.num > 0 && sar.den > 0 && sar.num != sar.den) {
				t->dwidth = (int) ((int64_t) cp->width * sar.num / sar.den);
				t->dheight = cp->height;
			}
			if (st->avg_frame_rate.num > 0 && st->avg_frame_rate.den > 0)
				t->fps = av_q2d(st->avg_frame_rate);
			t->lang_default = (int) i == vbest;
		} else {
			t->rate = cp->sample_rate;
			t->channels = cp->ch_layout.nb_channels;
			t->bits = cp->bits_per_coded_sample;
			t->lang_default = (int) i == abest;
		}
		if (cp->extradata_size > 0) {
			t->extra = (uint8_t *) malloc((size_t) cp->extradata_size);
			if (t->extra != NULL) {
				memcpy(t->extra, cp->extradata, (size_t) cp->extradata_size);
				t->extra_len = (size_t) cp->extradata_size;
			}
		}
		if (st->duration != AV_NOPTS_VALUE)
			t->duration = to_us(st->duration, st->time_base);
		l->ntr++;
	}
	/* the best ones first: the player takes the first video and audio tracks */
	{
		int k, j;
		for (k = 0; k < l->ntr; k++)
			if (l->tr[k].lang_default && l->tr[k].kind == AV_VIDEO && k > 0) {
				struct av_track t = l->tr[k]; int s = l->stream_of[k];
				for (j = k; j > 0; j--) { l->tr[j] = l->tr[j - 1]; l->stream_of[j] = l->stream_of[j - 1]; }
				l->tr[0] = t; l->stream_of[0] = s;
				break;
			}
		for (k = 1; k < l->ntr; k++)
			if (l->tr[k].lang_default && l->tr[k].kind == AV_AUDIO) {
				for (j = 0; j < k; j++)
					if (l->tr[j].kind == AV_AUDIO) {
						struct av_track t = l->tr[k]; int s = l->stream_of[k];
						l->tr[k] = l->tr[j]; l->stream_of[k] = l->stream_of[j];
						l->tr[j] = t; l->stream_of[j] = s;
						break;
					}
				break;
			}
	}
	if (fc->duration != AV_NOPTS_VALUE && fc->duration > 0)
		l->duration = (av_us) av_rescale(fc->duration, 1000000, AV_TIME_BASE);
	if (fc->start_time != AV_NOPTS_VALUE && fc->start_time > 0)
		l->start = (av_us) av_rescale(fc->start_time, 1000000, AV_TIME_BASE);
	snprintf(l->name, sizeof l->name, "%s", fc->iformat != NULL ? fc->iformat->name : "lavf");
	{
		char *c = strchr(l->name, ',');	/* ("mov,mp4,m4a...") */
		if (c != NULL)
			*c = 0;
	}
}

static int track_of(struct lavf *l, int stream)
{
	int k;
	for (k = 0; k < l->ntr; k++)
		if (l->stream_of[k] == stream)
			return k;
	return -1;
}

static void queue_drop(struct lavf *l)
{
	while (l->qn > 0) {
		av_pkt_free(&l->q[l->qh]);
		l->qh = (l->qh + 1) % QMAX;
		l->qn--;
	}
	l->qbytes = 0;
}

static int lavf_main(void *arg)
{
	struct lavf *l = (struct lavf *) arg;
	AVFormatContext *fc = avformat_alloc_context();
	uint8_t *iob = (uint8_t *) av_malloc(65536);
	AVIOContext *io = iob != NULL ? avio_alloc_context(iob, 65536, 0, l, io_read, NULL, io_seek) : NULL;
	AVPacket *pk = av_packet_alloc();
	int r;

	if (fc == NULL || io == NULL || pk == NULL) {
		l->failed = AV_ENOMEM;
		goto out;
	}
	io->seekable = AVIO_SEEKABLE_NORMAL;
	fc->pb = io;
	fc->flags |= AVFMT_FLAG_CUSTOM_IO;
	av_lock(&av__ff_lock);
	av__ff_init();
	r = avformat_open_input(&fc, NULL, NULL, NULL);
	if (r >= 0)
		r = avformat_find_stream_info(fc, NULL);
	av_unlock(&av__ff_lock);
	if (r < 0) {
		l->failed = l->quit ? AV_ERR : AV_EUNSUP;
		goto out;
	}
	av_lock(&l->lk);
	make_tracks(l, fc);
	l->tracks_ready = 1;
	av_unlock(&l->lk);
	while (!l->quit) {
		unsigned req;
		int k;

		av_lock(&l->lk);
		req = l->seek_req;
		if (req != l->seek_done) {
			av_us t = l->seek_t;
			l->seek_started = req;
			queue_drop(l);
			av_unlock(&l->lk);
			/* the key frame at or before t (the index); the packets from there */
			t += l->start;
			avformat_seek_file(fc, -1, INT64_MIN, av_rescale(t, AV_TIME_BASE, 1000000), av_rescale(t, AV_TIME_BASE, 1000000), 0);
			av_lock(&l->lk);
			queue_drop(l);
			l->eof = 0;
			l->seek_done = req;
			av_unlock(&l->lk);
			continue;
		}
		if (l->eof || l->qn >= QMAX || l->qbytes >= QBYTES) {
			av_unlock(&l->lk);
			av_sleep_ms(5);
			continue;
		}
		av_unlock(&l->lk);
		r = av_read_frame(fc, pk);
		if (r == AVERROR_EXIT)
			continue;		/* (a seek or the end asked) */
		if (r < 0) {
			av_lock(&l->lk);
			l->eof = 1;
			av_unlock(&l->lk);
			continue;
		}
		k = track_of(l, pk->stream_index);
		if (k >= 0 && pk->size > 0) {
			AVStream *st = fc->streams[pk->stream_index];
			struct av_packet p;
			memset(&p, 0, sizeof p);
			p.track = k;
			p.pts = to_us(pk->pts, st->time_base);
			p.dts = to_us(pk->dts, st->time_base);
			if (p.pts == AV_NOTIME)
				p.pts = p.dts;
			if (p.pts == AV_NOTIME)
				p.pts = 0;
			p.pts -= l->start;
			if (p.dts != AV_NOTIME)
				p.dts -= l->start;
			p.dur = pk->duration > 0 ? to_us(pk->duration, st->time_base) : 0;
			p.key = (pk->flags & AV_PKT_FLAG_KEY) != 0;
			p.pos = pk->pos;
			p.data = (uint8_t *) malloc((size_t) pk->size);
			if (p.data != NULL) {
				memcpy(p.data, pk->data, (size_t) pk->size);
				p.size = (size_t) pk->size;
				av_lock(&l->lk);
				if (l->seek_req == l->seek_done) {
					l->q[(l->qh + l->qn) % QMAX] = p;
					l->qn++;
					l->qbytes += p.size;
				} else {
					free(p.data);	/* (read before a seek) */
				}
				av_unlock(&l->lk);
			}
		}
		av_packet_unref(pk);
	}
out:
	av_packet_free(&pk);
	if (fc != NULL) {
		av_lock(&av__ff_lock);
		avformat_close_input(&fc);
		av_unlock(&av__ff_lock);
	}
	if (io != NULL) {
		av_freep(&io->buffer);
		avio_context_free(&io);
	}
	av_lock(&l->lk);
	l->finished = 1;
	av_unlock(&l->lk);
	return 0;
}

/* ---- the parser's side (the feeder's thread) -------------------------------------------------- */

static void *lavf_create(struct av_demux *d)
{
	struct lavf *l = (struct lavf *) calloc(1, sizeof *l);

	if (l == NULL)
		return NULL;
	l->d = d;
	av_lock_init(&l->lk);
	l->need = 0;
	l->size = d->size;
	l->seek_t = AV_NOTIME;
	if (av_thread_start(&l->th, lavf_main, l, "av lavf") < 0) {
		free(l);
		return NULL;
	}
	return l;
}

static void lavf_destroy(void *priv)
{
	struct lavf *l = (struct lavf *) priv;
	int k;

	if (l == NULL)
		return;
	l->quit = 1;
	av_thread_join(&l->th);
	queue_drop(l);
	for (k = 0; k < l->ntr; k++)
		if (!l->tracks_given)
			free(l->tr[k].extra);
	free(l->cbuf);
	free(l);
}

/* the window's bytes into the cache (the lock held) */
static void take_bytes(struct av_demux *d, struct lavf *l)
{
	int64_t wbase = d->base + (int64_t) d->off;
	size_t n = d->len - d->off;

	if (d->size >= 0)
		l->size = d->size;
	if (n > 0) {
		if (wbase != l->cbase + (int64_t) l->clen) {
			if (wbase > l->cbase && wbase < l->cbase + (int64_t) l->clen) {
				/* overlapping: the new part */
				size_t skip = (size_t) (l->cbase + (int64_t) l->clen - wbase);
				if (skip >= n) {
					av__consume(d, wbase + (int64_t) n);
					return;
				}
				wbase += (int64_t) skip;
				n -= skip;
				av__consume(d, wbase);
			} else {
				/* elsewhere: the cache starts again there */
				l->cbase = wbase;
				l->clen = 0;
			}
		}
		/* the bytes long behind libavformat's position dropped */
		if (l->rpos - l->cbase > CACHE_KEEP * 2 && l->rpos < l->cbase + (int64_t) l->clen) {
			size_t drop = (size_t) (l->rpos - CACHE_KEEP - l->cbase);
			memmove(l->cbuf, l->cbuf + drop, l->clen - drop);
			l->clen -= drop;
			l->cbase += (int64_t) drop;
		}
		if (l->clen + n > l->ccap) {
			size_t cap = l->ccap ? l->ccap : (1 << 20);
			uint8_t *b;
			while (cap < l->clen + n)
				cap *= 2;
			b = (uint8_t *) realloc(l->cbuf, cap);
			if (b == NULL)
				return;
			l->cbuf = b;
			l->ccap = cap;
		}
		memcpy(l->cbuf + l->clen, d->buf + (wbase - d->base), n);
		l->clen += n;
		av__consume(d, wbase + (int64_t) n);
	}
	l->ended = d->ended;
}

/* where bytes are wanted: what the thread waits for, else on after the cache; -1: none for now (far
 * enough ahead); -2: none (the whole stream is read) */
static int64_t wanted(struct lavf *l)
{
	int64_t end = l->cbase + (int64_t) l->clen;

	if (l->need >= 0)
		/* (already here: the thread's turn -- the feeder waits) */
		return l->need >= l->cbase && l->need < end ? -1 : l->need;
	if (l->eof || l->finished || (l->size >= 0 && end >= l->size))
		return -2;
	if (end - l->rpos > CACHE_AHEAD)
		return -1;
	return l->rpos > end ? l->rpos : end;
}

static int lavf_read(struct av_demux *d, void *priv, struct av_packet *pkt)
{
	struct lavf *l = (struct lavf *) priv;
	int r;

	av_lock(&l->lk);
	take_bytes(d, l);
	if (l->tracks_ready && !l->tracks_given) {
		int k;
		av__clear_tracks(d);
		for (k = 0; k < l->ntr; k++) {
			struct av_track *t = av__new_track(d);
			if (t != NULL)
				*t = l->tr[k];
		}
		l->tracks_given = 1;
		d->init_new = 1;
		d->duration = l->duration;
		snprintf(d->name, sizeof d->name, "%s", l->name);
	}
	if (l->failed) {
		r = l->failed;
	} else if (l->tracks_given && l->qn > 0 && l->seek_req == l->seek_done) {
		*pkt = l->q[l->qh];
		memset(&l->q[l->qh], 0, sizeof l->q[l->qh]);
		l->qh = (l->qh + 1) % QMAX;
		l->qn--;
		l->qbytes -= pkt->size;
		r = AV_OK;
	} else if (l->eof && l->qn == 0) {
		r = AV_EOF;
	} else {
		r = AV_AGAIN;
	}
	{
		int64_t w = wanted(l);
		d->want = w >= 0 ? w : w == -1 ? -1 : (l->size >= 0 ? l->size : av__end(d));
	}
	av_unlock(&l->lk);
	return r;
}

static void lavf_restart(struct av_demux *d, void *priv, int64_t pos)
{
	(void) d; (void) priv; (void) pos;	/* (the cache follows the bytes: take_bytes) */
}

static int64_t lavf_seek(struct av_demux *d, void *priv, av_us t, av_us *at)
{
	struct lavf *l = (struct lavf *) priv;
	unsigned req;
	int i;
	int64_t r;

	av_lock(&l->lk);
	if (!l->tracks_ready || l->failed || l->finished) {
		av_unlock(&l->lk);
		return -1;
	}
	l->seek_t = t;
	req = ++l->seek_req;
	l->eof = 0;
	queue_drop(l);
	av_unlock(&l->lk);
	/* the thread seeks (on its index) and waits for the bytes there, or reads on from what it has */
	for (i = 0; i < 400; i++) {
		av_lock(&l->lk);
		if (l->seek_done == req && (l->need >= 0 || l->qn > 0 || l->eof)) {
			av_unlock(&l->lk);
			break;
		}
		av_unlock(&l->lk);
		av_sleep_ms(5);
	}
	av_lock(&l->lk);
	r = l->need >= 0 ? l->need : l->cbase + (int64_t) l->clen;
	av_unlock(&l->lk);
	if (at != NULL)
		*at = t;
	(void) d;
	return r;
}

static int lavf_busy(struct av_demux *d, void *priv)
{
	struct lavf *l = (struct lavf *) priv;
	int b;
	(void) d;
	av_lock(&l->lk);
	b = !l->failed && !l->finished && !(l->eof && l->qn == 0);
	av_unlock(&l->lk);
	return b;
}

const struct av_fmt_ops av_lavf_ops = {
	"lavf", lavf_create, lavf_destroy, lavf_read, lavf_restart, lavf_seek, NULL, lavf_busy
};

/* ---- the probe -------------------------------------------------------------------------------- */

int av__lavf_probe(const uint8_t *p, size_t n)
{
	AVProbeData pd;
	const AVInputFormat *f;
	uint8_t *b;
	int score = 0;

	if (n < 12)
		return 0;
	b = (uint8_t *) calloc(1, n + AVPROBE_PADDING_SIZE);
	if (b == NULL)
		return 0;
	memcpy(b, p, n);
	memset(&pd, 0, sizeof pd);
	pd.filename = "";
	pd.buf = b;
	pd.buf_size = (int) n;
	av_lock(&av__ff_lock);
	av__ff_init();
	f = av_probe_input_format3(&pd, 1, &score);
	av_unlock(&av__ff_lock);
	free(b);
	/* (a guess from a few bytes: a raw MPEG audio or text file is not a container to read) */
	return f != NULL && score >= AVPROBE_SCORE_MAX / 4 && strcmp(f->name, "tty") && strcmp(f->name, "image2");
}

#endif
