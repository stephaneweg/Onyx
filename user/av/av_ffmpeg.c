/*
 * user/av/av_ffmpeg.c -- every codec FFmpeg's libavcodec decodes (AV_WITH_FFMPEG; GPL-2.0+): H.264, H.265,
 * MPEG-4 Part 2 (DivX / Xvid), MPEG-1 / 2, VC-1 / WMV, Theora, MJPEG...; AAC, AC-3 / E-AC-3, DTS, Vorbis,
 * WMA, ALAC, ADPCM...
 *
 * A packet in (its times in microseconds: the context's pkt_timebase), frames out: video as planar 8-bit
 * 4:2:0 (FFmpeg's own when it is that, else brought there by libswscale -- 10-bit, 4:2:2, RGB, paletted),
 * audio as interleaved float (any of FFmpeg's sample formats, planar or not). The track's ff_id says
 * which decoder (AV_C_FFMPEG, or libavformat's tracks); else its codec (H.264, H.265, AAC, Vorbis, MJPEG:
 * Onyx has no decoder of its own for them). Also av__ff_identify: the codecs of Matroska / MP4 tracks
 * Onyx's parsers do not know.
 */
#ifdef AV_WITH_FFMPEG
#include <stdio.h>
#include "av_int.h"
#include "av_os.h"
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>

extern av_lock_t av__ff_lock;
void av__ff_init(void);

#if defined(_NEWLIB_VERSION) && !defined(_WIN32)
/* Onyx's newlib (user/libc/onyx_syscalls.c) has no stat() nor times(), which FFmpeg's libraries reach
 * (a file's facts, clock()): none here -- the media library feeds FFmpeg its bytes itself */
#include <errno.h>
#include <sys/stat.h>
#include <sys/times.h>
int _stat(const char *path, struct stat *st);
int _stat(const char *path, struct stat *st)
{
	(void) path; (void) st;
	errno = ENOENT;
	return -1;
}
clock_t _times(struct tms *t);
clock_t _times(struct tms *t)
{
	clock_t c = (clock_t) (av_os_now_us() / 1000);	/* (newlib: CLOCKS_PER_SEC 1000) */
	if (t != NULL) {
		t->tms_utime = c;
		t->tms_stime = t->tms_cutime = t->tms_cstime = 0;
	}
	return c;
}
#endif

static enum AVCodecID id_of(const struct av_track *t)
{
	if (t->ff_id > 0)
		return (enum AVCodecID) t->ff_id;
	switch (t->codec) {
	case AV_C_H264: return AV_CODEC_ID_H264;
	case AV_C_HEVC: return AV_CODEC_ID_HEVC;
	case AV_C_AAC: return AV_CODEC_ID_AAC;
	case AV_C_VORBIS: return AV_CODEC_ID_VORBIS;
	case AV_C_MJPEG: return AV_CODEC_ID_MJPEG;
	case AV_C_VP8: return AV_CODEC_ID_VP8;
	case AV_C_VP9: return AV_CODEC_ID_VP9;
	case AV_C_AV1: return AV_CODEC_ID_AV1;
	case AV_C_OPUS: return AV_CODEC_ID_OPUS;
	case AV_C_MP3: return AV_CODEC_ID_MP3;
	case AV_C_FLAC: return AV_CODEC_ID_FLAC;
	}
	return AV_CODEC_ID_NONE;
}

int av__ff_decodes(const struct av_track *t)
{
	enum AVCodecID id = id_of(t);
	return id != AV_CODEC_ID_NONE && avcodec_find_decoder(id) != NULL;
}

struct ffdec {
	AVCodecContext *c;
	AVPacket *pk;
	AVFrame *fr;
	int kind, drained, sent_null;
	struct av_packet held;		/* a packet the decoder could not take yet (its output full) */
	int have_held;
	/* the video brought to 4:2:0 8-bit */
	struct SwsContext *sws;
	uint8_t *yuv;
	int yw, yh, yfmt;
	/* the sound interleaved */
	float *pcm;
	size_t pcm_cap;
	av_us next;			/* (a frame without a time: after the last one) */
};

static void *ff_open(const struct av_track *t)
{
	enum AVCodecID id = id_of(t);
	const AVCodec *codec = id != AV_CODEC_ID_NONE ? avcodec_find_decoder(id) : NULL;
	struct ffdec *f;
	int r;

	if (codec == NULL)
		return NULL;
	f = (struct ffdec *) calloc(1, sizeof *f);
	if (f == NULL)
		return NULL;
	f->kind = t->kind;
	f->c = avcodec_alloc_context3(codec);
	f->pk = av_packet_alloc();
	f->fr = av_frame_alloc();
	if (f->c == NULL || f->pk == NULL || f->fr == NULL)
		goto fail;
	f->c->pkt_timebase = (AVRational) { 1, 1000000 };
	f->c->codec_tag = (unsigned) t->ff_tag;
	if (t->extra != NULL && t->extra_len > 0) {
		f->c->extradata = (uint8_t *) av_mallocz(t->extra_len + AV_INPUT_BUFFER_PADDING_SIZE);
		if (f->c->extradata == NULL)
			goto fail;
		memcpy(f->c->extradata, t->extra, t->extra_len);
		f->c->extradata_size = (int) t->extra_len;
	}
	if (t->kind == AV_VIDEO) {
		f->c->width = t->width;
		f->c->height = t->height;
	} else {
		f->c->sample_rate = t->rate;
		if (t->channels > 0)
			av_channel_layout_default(&f->c->ch_layout, t->channels);
		f->c->block_align = t->block_align;
		f->c->bits_per_coded_sample = t->bits_coded ? t->bits_coded : t->bits;
	}
	f->c->bit_rate = t->bit_rate;
	f->c->thread_count = 1;
	av_lock(&av__ff_lock);
	av__ff_init();
	r = avcodec_open2(f->c, codec, NULL);
	av_unlock(&av__ff_lock);
	if (r < 0)
		goto fail;
	f->next = AV_NOTIME;
	return f;
fail:
	avcodec_free_context(&f->c);
	av_packet_free(&f->pk);
	av_frame_free(&f->fr);
	free(f);
	return NULL;
}

static void ff_close(void *c)
{
	struct ffdec *f = (struct ffdec *) c;

	if (f == NULL)
		return;
	av_lock(&av__ff_lock);
	avcodec_free_context(&f->c);
	av_unlock(&av__ff_lock);
	av_packet_free(&f->pk);
	av_frame_free(&f->fr);
	sws_freeContext(f->sws);
	av_pkt_free(&f->held);
	free(f->yuv);
	free(f->pcm);
	free(f);
}

static int give(struct ffdec *f, const struct av_packet *p)
{
	int r;

	if (av_new_packet(f->pk, (int) p->size) < 0)
		return AV_ENOMEM;
	memcpy(f->pk->data, p->data, p->size);
	f->pk->pts = p->pts == AV_NOTIME ? AV_NOPTS_VALUE : p->pts;
	f->pk->dts = p->dts == AV_NOTIME ? AV_NOPTS_VALUE : p->dts;
	f->pk->duration = p->dur;
	if (p->key)
		f->pk->flags |= AV_PKT_FLAG_KEY;
	r = avcodec_send_packet(f->c, f->pk);
	av_packet_unref(f->pk);
	if (r == AVERROR(EAGAIN))
		return AV_AGAIN;
	return r < 0 ? AV_ERR : AV_OK;
}

static int ff_send(void *c, const struct av_packet *p)
{
	struct ffdec *f = (struct ffdec *) c;
	int r;

	if (p == NULL) {
		if (!f->sent_null) {
			avcodec_send_packet(f->c, NULL);
			f->sent_null = 1;
		}
		return AV_OK;
	}
	if (f->sent_null) {
		/* a packet after a drain: the decoder starts again */
		avcodec_flush_buffers(f->c);
		f->sent_null = 0;
	}
	if (f->have_held) {
		/* (the receive loop was not run to its end: what is held goes first) */
		r = give(f, &f->held);
		if (r == AV_AGAIN)
			return AV_ERR;		/* (the caller's packet dropped: never happens with a drained decoder) */
		av_pkt_free(&f->held);
		f->have_held = 0;
	}
	r = give(f, p);
	if (r == AV_AGAIN) {
		/* its output is full: kept, given at the next receive */
		f->held = *p;
		f->held.data = (uint8_t *) malloc(p->size ? p->size : 1);
		if (f->held.data == NULL)
			return AV_ENOMEM;
		memcpy(f->held.data, p->data, p->size);
		f->have_held = 1;
		return AV_OK;
	}
	return r;
}

/* a video frame to 8-bit 4:2:0 planes */
static int video_out(struct ffdec *f, AVFrame *fr, struct av_frame *o)
{
	int w = fr->width, h = fr->height;

	o->kind = AV_VIDEO;
	o->pix = AV_PIX_I420;
	o->width = w;
	o->height = h;
	o->colorspace = fr->colorspace == AVCOL_SPC_BT709 ? AV_CS_BT709 :
		fr->colorspace == AVCOL_SPC_BT2020_NCL || fr->colorspace == AVCOL_SPC_BT2020_CL ? AV_CS_BT2020 : AV_CS_BT601;
	o->full_range = fr->color_range == AVCOL_RANGE_JPEG || fr->format == AV_PIX_FMT_YUVJ420P;
	if (fr->format == AV_PIX_FMT_YUV420P || fr->format == AV_PIX_FMT_YUVJ420P) {
		int k;
		for (k = 0; k < 3; k++) {
			o->plane[k] = fr->data[k];
			o->stride[k] = fr->linesize[k];
		}
		return AV_OK;
	}
	/* the others (10 bits, 4:2:2, 4:4:4, RGB, paletted...): libswscale */
	if (f->yuv == NULL || f->yw != w || f->yh != h || f->yfmt != fr->format) {
		size_t cw = (size_t) (w + 1) / 2, ch = (size_t) (h + 1) / 2;
		free(f->yuv);
		f->yuv = (uint8_t *) malloc((size_t) w * h + 2 * cw * ch + 64);
		if (f->yuv == NULL)
			return AV_ENOMEM;
		f->yw = w; f->yh = h; f->yfmt = fr->format;
	}
	f->sws = sws_getCachedContext(f->sws, w, h, (enum AVPixelFormat) fr->format, w, h, AV_PIX_FMT_YUV420P,
			SWS_FAST_BILINEAR, NULL, NULL, NULL);
	if (f->sws == NULL)
		return AV_EUNSUP;
	{
		int cw = (w + 1) / 2, ch = (h + 1) / 2;
		uint8_t *dst[4] = { f->yuv, f->yuv + (size_t) w * h, f->yuv + (size_t) w * h + (size_t) cw * ch, NULL };
		int ds[4] = { w, cw, cw, 0 };
		sws_scale(f->sws, (const uint8_t * const *) fr->data, fr->linesize, 0, h, dst, ds);
		o->plane[0] = dst[0]; o->plane[1] = dst[1]; o->plane[2] = dst[2];
		o->stride[0] = w; o->stride[1] = o->stride[2] = cw;
	}
	if (o->colorspace == AV_CS_BT601 && fr->format != AV_PIX_FMT_YUV422P && fr->format != AV_PIX_FMT_YUV444P &&
	    fr->format != AV_PIX_FMT_YUV420P10LE)
		o->full_range = 0;
	return AV_OK;
}

/* an audio frame to interleaved float */
static int audio_out(struct ffdec *f, AVFrame *fr, struct av_frame *o)
{
	int ch = fr->ch_layout.nb_channels, n = fr->nb_samples, i, k;
	enum AVSampleFormat fmt = (enum AVSampleFormat) fr->format;
	int planar = av_sample_fmt_is_planar(fmt);
	size_t need = (size_t) ch * (size_t) n;

	if (ch < 1 || ch > 16 || n < 0)
		return AV_ERR;
	if (need > f->pcm_cap) {
		free(f->pcm);
		f->pcm = (float *) malloc((need ? need : 1) * sizeof(float));
		if (f->pcm == NULL) {
			f->pcm_cap = 0;
			return AV_ENOMEM;
		}
		f->pcm_cap = need;
	}
	for (k = 0; k < ch; k++) {
		const uint8_t *src = planar ? fr->extended_data[k] : fr->extended_data[0];
		int step = planar ? 1 : ch, at = planar ? 0 : k;
		float *d = f->pcm + k;
		for (i = 0; i < n; i++, d += ch) {
			int j = at + i * step;
			switch (av_get_packed_sample_fmt(fmt)) {
			case AV_SAMPLE_FMT_U8: *d = ((float) src[j] - 128.0f) / 128.0f; break;
			case AV_SAMPLE_FMT_S16: *d = (float) ((const int16_t *) src)[j] / 32768.0f; break;
			case AV_SAMPLE_FMT_S32: *d = (float) ((double) ((const int32_t *) src)[j] / 2147483648.0); break;
			case AV_SAMPLE_FMT_S64: *d = (float) ((double) ((const int64_t *) src)[j] / 9223372036854775808.0); break;
			case AV_SAMPLE_FMT_FLT: *d = ((const float *) src)[j]; break;
			case AV_SAMPLE_FMT_DBL: *d = (float) ((const double *) src)[j]; break;
			default: *d = 0; break;
			}
		}
	}
	o->kind = AV_AUDIO;
	o->rate = fr->sample_rate;
	o->channels = ch;
	o->samples = n;
	o->pcm = f->pcm;
	return AV_OK;
}

static int ff_receive(void *c, struct av_frame *o)
{
	struct ffdec *f = (struct ffdec *) c;
	int r;

	if (f->have_held) {
		if (give(f, &f->held) != AV_AGAIN) {
			av_pkt_free(&f->held);
			f->have_held = 0;
		}
	}
	av_frame_unref(f->fr);
	r = avcodec_receive_frame(f->c, f->fr);
	if (r < 0)
		return AV_AGAIN;	/* EAGAIN: a packet needed; EOF: drained */
	{
		int64_t t = f->fr->best_effort_timestamp != AV_NOPTS_VALUE ? f->fr->best_effort_timestamp : f->fr->pts;
		o->pts = t != AV_NOPTS_VALUE ? (av_us) t : (f->next != AV_NOTIME ? f->next : 0);
	}
	if (f->kind == AV_VIDEO) {
		o->dur = f->fr->duration > 0 ? (av_us) f->fr->duration : 0;
		r = video_out(f, f->fr, o);
	} else {
		r = audio_out(f, f->fr, o);
		o->dur = f->fr->sample_rate > 0 ? (av_us) f->fr->nb_samples * AV_US / f->fr->sample_rate : 0;
	}
	f->next = o->pts + o->dur;
	return r == AV_OK ? AV_OK : AV_AGAIN;
}

static void ff_flush(void *c)
{
	struct ffdec *f = (struct ffdec *) c;
	avcodec_flush_buffers(f->c);
	f->sent_null = 0;
	if (f->have_held) {
		av_pkt_free(&f->held);
		f->have_held = 0;
	}
	f->next = AV_NOTIME;
}

/* the entries the lists show (av_codec_list); av_ff_any_codec decodes whatever FFmpeg does */
#define FF_IMPL(var, codec, kind, name, w, h, fps) \
	const struct av_codec_impl var = { codec, kind, name, "FFmpeg 7.1", w, h, fps, ff_open, ff_send, ff_receive, ff_flush, ff_close, NULL };
FF_IMPL(av_ff_h264_codec, AV_C_H264, AV_VIDEO, "h264", 1280, 720, 30)
FF_IMPL(av_ff_hevc_codec, AV_C_HEVC, AV_VIDEO, "hevc", 854, 480, 30)
FF_IMPL(av_ff_aac_codec, AV_C_AAC, AV_AUDIO, "aac", 0, 0, 0)
FF_IMPL(av_ff_vorbis_codec, AV_C_VORBIS, AV_AUDIO, "vorbis", 0, 0, 0)
FF_IMPL(av_ff_mjpeg_codec, AV_C_MJPEG, AV_VIDEO, "mjpeg", 1280, 720, 30)
FF_IMPL(av_ff_any_codec, AV_C_FFMPEG, AV_VIDEO, "ffmpeg", 854, 480, 30)

/* ---- the codecs of tracks Onyx's parsers do not know ---------------------------------------- */

static const struct { const char *id; enum AVCodecID ff; } MKV_IDS[] = {
	{ "V_MPEG4/ISO/SP", AV_CODEC_ID_MPEG4 }, { "V_MPEG4/ISO/ASP", AV_CODEC_ID_MPEG4 }, { "V_MPEG4/ISO/AP", AV_CODEC_ID_MPEG4 },
	{ "V_MPEG4/MS/V3", AV_CODEC_ID_MSMPEG4V3 }, { "V_MPEG1", AV_CODEC_ID_MPEG1VIDEO }, { "V_MPEG2", AV_CODEC_ID_MPEG2VIDEO },
	{ "V_THEORA", AV_CODEC_ID_THEORA }, { "V_REAL/RV10", AV_CODEC_ID_RV10 }, { "V_REAL/RV20", AV_CODEC_ID_RV20 },
	{ "V_REAL/RV30", AV_CODEC_ID_RV30 }, { "V_REAL/RV40", AV_CODEC_ID_RV40 }, { "V_PRORES", AV_CODEC_ID_PRORES },
	{ "V_FFV1", AV_CODEC_ID_FFV1 }, { "A_AC3", AV_CODEC_ID_AC3 }, { "A_EAC3", AV_CODEC_ID_EAC3 }, { "A_DTS", AV_CODEC_ID_DTS },
	{ "A_TRUEHD", AV_CODEC_ID_TRUEHD }, { "A_MLP", AV_CODEC_ID_MLP }, { "A_ALAC", AV_CODEC_ID_ALAC }, { "A_TTA1", AV_CODEC_ID_TTA },
	{ "A_WAVPACK4", AV_CODEC_ID_WAVPACK }, { "A_MPEG/L2", AV_CODEC_ID_MP2 }, { "A_MPEG/L1", AV_CODEC_ID_MP1 },
	{ "A_REAL/COOK", AV_CODEC_ID_COOK }, { "A_REAL/ATRC", AV_CODEC_ID_ATRAC3 }, { "A_QUICKTIME/QDM2", AV_CODEC_ID_QDM2 },
};

static void set_ff(struct av_track *t, enum AVCodecID id)
{
	if (id == AV_CODEC_ID_NONE || avcodec_find_decoder(id) == NULL)
		return;
	t->codec = AV_C_FFMPEG;
	t->ff_id = (int) id;
	snprintf(t->codec_str, sizeof t->codec_str, "%s", avcodec_get_name(id));
}

void av__ff_identify(struct av_track *t, int fmt, uint32_t fourcc, int esds_oti)
{
	size_t i;

	if (fmt == AV_FMT_MKV) {
		for (i = 0; i < sizeof MKV_IDS / sizeof MKV_IDS[0]; i++)
			if (!strcmp(t->codec_id, MKV_IDS[i].id)) {
				set_ff(t, MKV_IDS[i].ff);
				return;
			}
		if (!strcmp(t->codec_id, "V_MS/VFW/FOURCC") && t->extra != NULL && t->extra_len >= 40) {
			/* a BITMAPINFOHEADER: its compression (biCompression), the codec's setup after it */
			uint32_t tag = av_rl32(t->extra + 16);
			const struct AVCodecTag *tags[] = { avformat_get_riff_video_tags(), NULL };
			enum AVCodecID id = av_codec_get_id(tags, tag);
			t->ff_tag = (int) tag;
			if (t->extra_len > 40)
				memmove(t->extra, t->extra + 40, t->extra_len - 40);
			t->extra_len -= 40;
			set_ff(t, id);
			return;
		}
		if (!strcmp(t->codec_id, "A_MS/ACM") && t->extra != NULL && t->extra_len >= 18) {
			/* a WAVEFORMATEX: its format tag, block align, bits; the codec's setup after it */
			uint32_t tag = av_rl16(t->extra);
			const struct AVCodecTag *tags[] = { avformat_get_riff_audio_tags(), NULL };
			enum AVCodecID id = av_codec_get_id(tags, tag);
			size_t cb = av_rl16(t->extra + 16);
			t->ff_tag = (int) tag;
			t->block_align = (int) av_rl16(t->extra + 12);
			t->bits_coded = (int) av_rl16(t->extra + 14);
			if (cb > t->extra_len - 18)
				cb = t->extra_len - 18;
			memmove(t->extra, t->extra + 18, cb);
			t->extra_len = cb;
			set_ff(t, id);
			return;
		}
		return;
	}
	if (fmt == AV_FMT_MP4) {
		if (esds_oti > 0) {
			const struct AVCodecTag *tags[] = { avformat_get_mov_video_tags(), avformat_get_mov_audio_tags(), NULL };
			/* MPEG-4 systems' object types (ISO/IEC 14496-1) */
			switch (esds_oti) {
			case 0x20: set_ff(t, AV_CODEC_ID_MPEG4); break;
			case 0x60: case 0x61: case 0x62: case 0x63: case 0x64: case 0x65: set_ff(t, AV_CODEC_ID_MPEG2VIDEO); break;
			case 0x6A: set_ff(t, AV_CODEC_ID_MPEG1VIDEO); break;
			case 0x6C: set_ff(t, AV_CODEC_ID_MJPEG); break;
			case 0xA5: set_ff(t, AV_CODEC_ID_AC3); break;
			case 0xA6: set_ff(t, AV_CODEC_ID_EAC3); break;
			case 0xA9: set_ff(t, AV_CODEC_ID_DTS); break;
			case 0xDD: set_ff(t, AV_CODEC_ID_VORBIS); break;
			default: (void) tags; break;
			}
			return;
		}
		{
			const struct AVCodecTag *tags[] = { avformat_get_mov_video_tags(), avformat_get_mov_audio_tags(), NULL };
			/* (our fourcc is big-endian as read; FFmpeg's tags are little-endian MKTAG) */
			uint32_t le = (fourcc >> 24) | ((fourcc >> 8) & 0xFF00) | ((fourcc << 8) & 0xFF0000) | (fourcc << 24);
			t->ff_tag = (int) le;
			set_ff(t, av_codec_get_id(tags, le));
		}
	}
}

#endif
