/*
 * user/av/av_codec.c -- the codecs: the table of those built in, the decoder object, the PCM
 * family (integer, float, A-law, mu-law) and the uncompressed I420 video (the tests' codec),
 * RFC 6381 codec strings, and the answers to canPlayType / MediaSource.isTypeSupported /
 * MediaCapabilities (av_type_supported).
 *
 * The large decoders are glue files compiled with their library (AV_WITH_*: user/av/codecs.mk
 * builds libvpx 1.15.2 for av_vpx.c, dav1d 1.5.1 for av_dav1d.c, libopus 1.5.2 for av_opus.c;
 * av_mp3.c has minimp3 in it): a build answers only for what it really decodes.
 */
#include <stdio.h>
#include <ctype.h>
#include <math.h>
#include "av_int.h"
#include "av_store.h"

/* ---- the table -------------------------------------------------------------------------- */

static const struct av_codec_impl *const impls[] = {
#ifdef AV_WITH_VPX
	&av_vp9_codec, &av_vp8_codec,
#endif
#ifdef AV_WITH_DAV1D
	&av_av1_codec,
#endif
#ifdef AV_WITH_OPUS
	&av_opus_codec,
#endif
#ifdef AV_WITH_VORBIS
	&av_vorbis_codec,
#endif
	&av_flac_codec, &av_mp3_codec, &av_pcm_codec, &av_alaw_codec, &av_ulaw_codec, &av_i420_codec,
#ifdef AV_WITH_FFMPEG
	/* FFmpeg's, after Onyx's own (taken only for what these do not decode) */
	&av_ff_h264_codec, &av_ff_hevc_codec, &av_ff_aac_codec, &av_ff_vorbis_codec, &av_ff_mjpeg_codec, &av_ff_any_codec,
#endif
};
#define NIMPL ((int) (sizeof impls / sizeof impls[0]))

static int is_pcm(int codec)
{
	return codec >= AV_C_PCM_S16LE && codec <= AV_C_PCM_U8;
}

static const struct av_codec_impl *impl_of(int codec)
{
	int i;

	if (is_pcm(codec))
		codec = AV_C_PCM_S16LE;		/* (one implementation for the family) */
	for (i = 0; i < NIMPL; i++)
		if (impls[i]->codec == codec)
			return impls[i];
	return av__stub_of(codec);	/* (the tests' stand-ins, when on: av_stub.c) */
}

const struct av_codec_info *av_codec_list(int *n)
{
	static struct av_codec_info list[32];
	static int made;
	int i;

	if (!made) {
		for (i = 0; i < NIMPL && i < 32; i++) {
			list[i].codec = impls[i]->codec;
			list[i].kind = impls[i]->kind;
			list[i].name = impls[i]->name;
			list[i].library = impls[i]->library;
			list[i].smooth_w = impls[i]->smooth_w;
			list[i].smooth_h = impls[i]->smooth_h;
			list[i].smooth_fps = impls[i]->smooth_fps;
		}
		made = i;
	}
	*n = made;
	return list;
}

/* the decoder of a track: Onyx's own for its codec, else FFmpeg's (by ff_id, AV_C_FFMPEG) */
static const struct av_codec_impl *impl_for(const struct av_track *t)
{
	const struct av_codec_impl *im = t->codec == AV_C_FFMPEG ? NULL : impl_of(t->codec);
#ifdef AV_WITH_FFMPEG
	if ((im == NULL || im->open == av_ff_any_codec.open) && av__ff_decodes(t))
		return &av_ff_any_codec;
	if (im != NULL && im->open == av_ff_any_codec.open)
		return NULL;
#endif
	return im;
}

int av_decoder_supported_track(const struct av_track *t)
{
	return t != NULL && !t->encrypted && impl_for(t) != NULL;
}

/* ---- the decoder object ----------------------------------------------------------------- */

struct av_decoder {
	const struct av_codec_impl *impl;
	void *c;
};

struct av_decoder *av_decoder_new(const struct av_track *t)
{
	const struct av_codec_impl *im;
	struct av_decoder *d;

	if (t == NULL || t->encrypted)
		return NULL;
	im = impl_for(t);
	if (im == NULL)
		return NULL;
	d = (struct av_decoder *) calloc(1, sizeof *d);
	if (d == NULL)
		return NULL;
	d->impl = im;
	d->c = im->open(t);
	if (d->c == NULL) {
		free(d);
		return NULL;
	}
	return d;
}

void av_decoder_free(struct av_decoder *d)
{
	if (d != NULL) {
		d->impl->close(d->c);
		free(d);
	}
}

int av_decoder_send(struct av_decoder *d, const struct av_packet *p)
{
	return d->impl->send(d->c, p);
}

int av_decoder_receive(struct av_decoder *d, struct av_frame *f)
{
	memset(f, 0, sizeof *f);
	return d->impl->receive(d->c, f);
}

void av_decoder_flush(struct av_decoder *d)
{
	d->impl->flush(d->c);
}

const char *av_decoder_name(const struct av_decoder *d)
{
	return d->impl->name;
}

/* ---- PCM -------------------------------------------------------------------------------- */

struct pcm {
	int codec, channels, rate;
	float *out;
	size_t cap;
	struct av_frame f;
	int have;
};

static void *pcm_open(const struct av_track *t)
{
	struct pcm *p;

	if (t->channels < 1 || t->channels > 8 || t->rate <= 0)
		return NULL;
	p = (struct pcm *) calloc(1, sizeof *p);
	if (p == NULL)
		return NULL;
	p->codec = t->codec;
	p->channels = t->channels;
	p->rate = t->rate;
	return p;
}

static void pcm_close(void *c)
{
	struct pcm *p = (struct pcm *) c;
	if (p != NULL)
		free(p->out);
	free(p);
}

static int bytes_of(int codec)
{
	switch (codec) {
	case AV_C_PCM_U8: case AV_C_ALAW: case AV_C_ULAW: return 1;
	case AV_C_PCM_S16LE: case AV_C_PCM_S16BE: return 2;
	case AV_C_PCM_S24LE: case AV_C_PCM_S24BE: return 3;
	case AV_C_PCM_S32LE: case AV_C_PCM_F32LE: return 4;
	case AV_C_PCM_F64LE: return 8;
	}
	return 0;
}

static float alaw(uint8_t v)
{
	int t, seg;
	v ^= 0x55;
	t = (v & 15) << 4;
	seg = (v & 0x70) >> 4;
	if (seg == 0) t += 8;
	else if (seg == 1) t += 0x108;
	else t = (t + 0x108) << (seg - 1);
	return (float) ((v & 0x80) ? t : -t) / 32768.0f;
}

static float ulaw(uint8_t v)
{
	int t;
	v = (uint8_t) ~v;
	t = ((v & 15) << 3) + 0x84;
	t <<= (v & 0x70) >> 4;
	return (float) ((v & 0x80) ? 0x84 - t : t - 0x84) / 32768.0f;
}

static int pcm_send(void *c, const struct av_packet *pk)
{
	struct pcm *p = (struct pcm *) c;
	int bs, n, i;
	size_t total;
	const uint8_t *s;

	if (pk == NULL)
		return AV_OK;
	bs = bytes_of(p->codec);
	if (bs == 0)
		return AV_EUNSUP;
	n = (int) (pk->size / (size_t) (bs * p->channels));
	total = (size_t) n * (size_t) p->channels;
	if (total > p->cap) {
		free(p->out);
		p->out = (float *) malloc(total * sizeof(float));
		if (p->out == NULL) {
			p->cap = 0;
			return AV_ENOMEM;
		}
		p->cap = total;
	}
	s = pk->data;
	for (i = 0; i < (int) total; i++, s += bs) {
		float v;
		switch (p->codec) {
		case AV_C_PCM_U8: v = ((float) s[0] - 128.0f) / 128.0f; break;
		case AV_C_ALAW: v = alaw(s[0]); break;
		case AV_C_ULAW: v = ulaw(s[0]); break;
		case AV_C_PCM_S16LE: v = (float) (int16_t) (s[0] | s[1] << 8) / 32768.0f; break;
		case AV_C_PCM_S16BE: v = (float) (int16_t) (s[1] | s[0] << 8) / 32768.0f; break;
		case AV_C_PCM_S24LE: v = (float) ((int32_t) ((uint32_t) s[0] << 8 | (uint32_t) s[1] << 16 | (uint32_t) s[2] << 24) >> 8) / 8388608.0f; break;
		case AV_C_PCM_S24BE: v = (float) ((int32_t) ((uint32_t) s[2] << 8 | (uint32_t) s[1] << 16 | (uint32_t) s[0] << 24) >> 8) / 8388608.0f; break;
		case AV_C_PCM_S32LE: v = (float) ((double) (int32_t) av_rl32(s) / 2147483648.0); break;
		case AV_C_PCM_F32LE: { union { uint32_t u; float f; } u; u.u = av_rl32(s); v = u.f; break; }
		case AV_C_PCM_F64LE: { union { uint64_t u; double f; } u; u.u = (uint64_t) av_rl32(s + 4) << 32 | av_rl32(s); v = (float) u.f; break; }
		default: v = 0;
		}
		p->out[i] = v;
	}
	memset(&p->f, 0, sizeof p->f);
	p->f.kind = AV_AUDIO;
	p->f.pts = pk->pts;
	p->f.dur = (av_us) n * AV_US / p->rate;
	p->f.rate = p->rate;
	p->f.channels = p->channels;
	p->f.samples = n;
	p->f.pcm = p->out;
	p->have = n > 0;
	return AV_OK;
}

static int pcm_receive(void *c, struct av_frame *f)
{
	struct pcm *p = (struct pcm *) c;

	if (!p->have)
		return AV_AGAIN;
	p->have = 0;
	*f = p->f;
	return AV_OK;
}

static void pcm_flush(void *c) { ((struct pcm *) c)->have = 0; }

const struct av_codec_impl av_pcm_codec = {
	AV_C_PCM_S16LE, AV_AUDIO, "pcm", "built in", 0, 0, 0,
	pcm_open, pcm_send, pcm_receive, pcm_flush, pcm_close, NULL
};
const struct av_codec_impl av_alaw_codec = {
	AV_C_ALAW, AV_AUDIO, "alaw", "built in", 0, 0, 0,
	pcm_open, pcm_send, pcm_receive, pcm_flush, pcm_close, NULL
};
const struct av_codec_impl av_ulaw_codec = {
	AV_C_ULAW, AV_AUDIO, "ulaw", "built in", 0, 0, 0,
	pcm_open, pcm_send, pcm_receive, pcm_flush, pcm_close, NULL
};

/* ---- I420, uncompressed (the tests' video codec) ----------------------------------------- */

struct i420 {
	int w, h;
	struct av_frame f;
	uint8_t *buf;
	int have;
};

static void *i420_open(const struct av_track *t)
{
	struct i420 *v;

	if (t->width <= 0 || t->height <= 0 || t->width > 8192 || t->height > 8192)
		return NULL;
	v = (struct i420 *) calloc(1, sizeof *v);
	if (v == NULL)
		return NULL;
	v->w = t->width;
	v->h = t->height;
	return v;
}

static void i420_close(void *c)
{
	struct i420 *v = (struct i420 *) c;
	if (v != NULL)
		free(v->buf);
	free(v);
}

static int i420_send(void *c, const struct av_packet *p)
{
	struct i420 *v = (struct i420 *) c;
	int cw = (v->w + 1) / 2, ch = (v->h + 1) / 2;
	size_t need = (size_t) v->w * v->h + 2 * (size_t) cw * ch;

	if (p == NULL)
		return AV_OK;
	if (p->size < need)
		return AV_ERR;
	if (v->buf == NULL) {
		v->buf = (uint8_t *) malloc(need);
		if (v->buf == NULL)
			return AV_ENOMEM;
	}
	memcpy(v->buf, p->data, need);
	memset(&v->f, 0, sizeof v->f);
	v->f.kind = AV_VIDEO;
	v->f.pts = p->pts;
	v->f.dur = p->dur;
	v->f.pix = AV_PIX_I420;
	v->f.width = v->w;
	v->f.height = v->h;
	v->f.plane[0] = v->buf;
	v->f.plane[1] = v->buf + (size_t) v->w * v->h;
	v->f.plane[2] = v->f.plane[1] + (size_t) cw * ch;
	v->f.stride[0] = v->w;
	v->f.stride[1] = v->f.stride[2] = cw;
	v->f.colorspace = AV_CS_BT601;
	v->have = 1;
	return AV_OK;
}

static int i420_receive(void *c, struct av_frame *f)
{
	struct i420 *v = (struct i420 *) c;

	if (!v->have)
		return AV_AGAIN;
	v->have = 0;
	*f = v->f;
	return AV_OK;
}

static void i420_flush(void *c) { ((struct i420 *) c)->have = 0; }

const struct av_codec_impl av_i420_codec = {
	AV_C_I420, AV_VIDEO, "i420", "built in", 0, 0, 0,
	i420_open, i420_send, i420_receive, i420_flush, i420_close, NULL
};

#ifndef AV_WITH_FFMPEG
/* (without FFmpeg: the codecs Onyx's decoders do not know stay unknown) */
void av__ff_identify(struct av_track *t, int fmt, uint32_t fourcc, int esds_oti)
{
	(void) t; (void) fmt; (void) fourcc; (void) esds_oti;
}
#endif

/* ---- codec strings ---------------------------------------------------------------------- */

void av__codec_string(struct av_track *t)
{
	const uint8_t *e = t->extra;
	size_t n = t->extra_len;
	char *s = t->codec_str;
	size_t sz = sizeof t->codec_str;

	switch (t->codec) {
	case AV_C_VP8: snprintf(s, sz, "vp8"); break;
	case AV_C_VP9:
		if (e != NULL && !strcmp(t->codec_id, "vp09") && n >= 7) {
			snprintf(s, sz, "vp09.%02d.%02d.%02d", e[4], e[5], e[6] >> 4);
		} else if (e != NULL && n >= 3 && !strcmp(t->codec_id, "V_VP9")) {
			/* Matroska: ID / length / value features (1 profile, 2 level, 3 bit depth) */
			int prof = 0, level = 10, depth = 8;
			size_t i = 0;
			while (i + 2 < n + 1 && i + 2 <= n) {
				int id = e[i], len = e[i + 1];
				if (i + 2 + (size_t) len > n || len < 1)
					break;
				if (id == 1) prof = e[i + 2];
				else if (id == 2) level = e[i + 2];
				else if (id == 3) depth = e[i + 2];
				i += 2 + (size_t) len;
			}
			snprintf(s, sz, "vp09.%02d.%02d.%02d", prof, level, depth);
		} else {
			snprintf(s, sz, "vp9");
		}
		break;
	case AV_C_AV1:
		if (e != NULL && n >= 4 && (e[0] & 0x80)) {
			int prof = e[1] >> 5, level = e[1] & 31, tier = e[2] >> 7;
			int depth = (e[2] & 0x40) ? ((e[2] & 0x20) ? 12 : 10) : 8;
			snprintf(s, sz, "av01.%d.%02d%c.%02d", prof, level, tier ? 'H' : 'M', depth);
		} else {
			snprintf(s, sz, "av01");
		}
		break;
	case AV_C_H264:
		if (e != NULL && n >= 4)
			snprintf(s, sz, "avc1.%02X%02X%02X", e[1], e[2], e[3]);
		else
			snprintf(s, sz, "avc1");
		break;
	case AV_C_HEVC: snprintf(s, sz, "hvc1"); break;
	case AV_C_AAC:
		if (e != NULL && n >= 1)
			snprintf(s, sz, "mp4a.40.%d", e[0] >> 3);
		else
			snprintf(s, sz, "mp4a.40.2");
		break;
	case AV_C_OPUS: snprintf(s, sz, "opus"); break;
	case AV_C_VORBIS: snprintf(s, sz, "vorbis"); break;
	case AV_C_MP3: snprintf(s, sz, "mp3"); break;
	case AV_C_FLAC: snprintf(s, sz, "flac"); break;
	case AV_C_MJPEG: snprintf(s, sz, "mjpeg"); break;
	case AV_C_I420: snprintf(s, sz, "i420"); break;
	case AV_C_FFMPEG:
		if (!s[0])
			snprintf(s, sz, "%s", t->codec_id);
		break;
	default:
		if (is_pcm(t->codec) || t->codec == AV_C_ALAW || t->codec == AV_C_ULAW)
			snprintf(s, sz, "pcm");
		else
			snprintf(s, sz, "%s", t->codec_id);
	}
}

/* a codec string's numbers: "vp09.00.10.08" -> v[0..] = 0, 10, 8 */
static int dotted(const char *s, int *v, int max)
{
	int n = 0;

	while (*s && *s != '.')
		s++;
	while (*s == '.' && n < max) {
		s++;
		v[n++] = (int) strtol(s, NULL, 10);
		while (*s && *s != '.')
			s++;
	}
	return n;
}

int av_codec_of_string(const char *s, int *profile_ok)
{
	char l[48];
	size_t i;
	int codec = AV_C_NONE, ok = 1, v[8];
	const struct av_codec_impl *im;

	for (i = 0; s[i] && i < sizeof l - 1; i++)
		l[i] = (char) tolower((unsigned char) s[i]);
	l[i] = 0;
	if (!strcmp(l, "vp8") || !strcmp(l, "vp8.0") || !strncmp(l, "vp08.", 5))
		codec = AV_C_VP8;
	else if (!strcmp(l, "vp9") || !strcmp(l, "vp9.0")) {
		codec = AV_C_VP9;
	} else if (!strncmp(l, "vp09.", 5)) {
		int n = dotted(l, v, 8);
		codec = AV_C_VP9;
		/* profile 0 (4:2:0, 8 bits) only: 10-bit / 4:4:4 are profiles 1..3 */
		if (n < 3 || v[0] != 0 || v[2] != 8)
			ok = 0;
	} else if (!strncmp(l, "av01", 4)) {
		int n = dotted(l, v, 8);
		codec = AV_C_AV1;
		/* main profile, 8 bits: dav1d as built here (CONFIG_8BPC only -- av_dav1d.c
		 * shows 8-bit 4:2:0 pictures alone) */
		if (n >= 1 && v[0] != 0)
			ok = 0;
		if (n >= 3 && v[2] != 8)
			ok = 0;
	} else if (!strncmp(l, "avc1", 4) || !strncmp(l, "avc3", 4)) {
		codec = AV_C_H264;
	} else if (!strncmp(l, "hvc1", 4) || !strncmp(l, "hev1", 4)) {
		codec = AV_C_HEVC;
	} else if (!strcmp(l, "opus")) {
		codec = AV_C_OPUS;
	} else if (!strcmp(l, "vorbis")) {
		codec = AV_C_VORBIS;
	} else if (!strcmp(l, "flac") || !strcmp(l, "fLaC")) {
		codec = AV_C_FLAC;
	} else if (!strcmp(l, "mp3") || !strcmp(l, "mp4a.69") || !strcmp(l, "mp4a.6b") ||
			!strcmp(l, "mp4a.40.34")) {
		codec = AV_C_MP3;
	} else if (!strncmp(l, "mp4a.40", 7) || !strcmp(l, "mp4a")) {
		codec = AV_C_AAC;
	} else if (!strcmp(l, "pcm") || !strcmp(l, "1") || !strcmp(l, "3") || !strcmp(l, "ipcm") ||
			!strcmp(l, "fpcm") || !strcmp(l, "sowt")) {
		codec = AV_C_PCM_S16LE;
	} else if (!strcmp(l, "alaw") || !strcmp(l, "6")) {
		codec = AV_C_ALAW;
	} else if (!strcmp(l, "ulaw") || !strcmp(l, "7")) {
		codec = AV_C_ULAW;
	} else if (!strcmp(l, "i420")) {
		codec = AV_C_I420;
	}
	im = codec != AV_C_NONE ? impl_of(codec) : NULL;
	if (im == NULL)
		ok = 0;
	else if (ok && im->profile_ok != NULL)
		ok = im->profile_ok(l);
	if (profile_ok != NULL)
		*profile_ok = ok;
	return codec;
}

/* ---- MIME types -------------------------------------------------------------------------- */

/* the value of a parameter (name=value, quoted or not) in a MIME string -> 1 found */
static int mime_param(const char *mime, const char *name, char *out, size_t max)
{
	const char *p = strchr(mime, ';');
	size_t nl = strlen(name);

	while (p != NULL) {
		p++;
		while (*p == ' ' || *p == '\t')
			p++;
		if (!strncasecmp(p, name, nl)) {
			const char *q = p + nl;
			while (*q == ' ')
				q++;
			if (*q == '=') {
				size_t i = 0;
				q++;
				while (*q == ' ')
					q++;
				if (*q == '"') {
					q++;
					while (*q && *q != '"' && i < max - 1)
						out[i++] = *q++;
				} else {
					while (*q && *q != ';' && *q != ' ' && i < max - 1)
						out[i++] = *q++;
				}
				out[i] = 0;
				return 1;
			}
		}
		p = strchr(p, ';');
	}
	return 0;
}

/* may a container hold this codec */
static int fits(int fmt, int codec, int audio_type)
{
	switch (fmt) {
	case AV_FMT_MKV:
		if (audio_type && codec < AV_C_OPUS)
			return 0;
		return codec == AV_C_VP8 || codec == AV_C_VP9 || codec == AV_C_AV1 || codec == AV_C_OPUS ||
			codec == AV_C_VORBIS || codec == AV_C_PCM_S16LE || codec == AV_C_I420 ||
			codec == AV_C_H264 || codec == AV_C_FLAC || codec == AV_C_AAC || codec == AV_C_MP3;
	case AV_FMT_MP4:
		if (audio_type && codec < AV_C_OPUS)
			return 0;
		return codec == AV_C_H264 || codec == AV_C_VP9 || codec == AV_C_AV1 || codec == AV_C_HEVC ||
			codec == AV_C_AAC || codec == AV_C_OPUS || codec == AV_C_FLAC || codec == AV_C_MP3 ||
			codec == AV_C_I420 || codec == AV_C_PCM_S16LE || codec == AV_C_ALAW || codec == AV_C_ULAW;
	case AV_FMT_WAV:
		return codec == AV_C_PCM_S16LE || codec == AV_C_ALAW || codec == AV_C_ULAW;
	case AV_FMT_FLAC:
		return codec == AV_C_FLAC;
	case AV_FMT_MP3:
		return codec == AV_C_MP3;
	}
	return 0;
}

int av_type_supported(const char *mime, int mse, int *smooth)
{
	char type[64], codecs[256];
	int fmt, audio_type, i;
	size_t k;
	int any_codec = 0, all_ok = 1, max_w = 0, max_h = 0, max_fps = 0;

	if (smooth != NULL)
		*smooth = 0;
	for (k = 0; mime[k] && mime[k] != ';' && k < sizeof type - 1; k++)
		type[k] = (char) tolower((unsigned char) mime[k]);
	while (k > 0 && type[k - 1] == ' ')
		k--;
	type[k] = 0;
	fmt = av_format_of_mime(type);
	if (fmt == AV_FMT_UNKNOWN)
		return 0;
	/* MSE's byte stream formats: WebM and ISO BMFF only */
	if (mse && fmt != AV_FMT_MKV && fmt != AV_FMT_MP4 && fmt != AV_FMT_MP3)
		return 0;
	audio_type = !strncmp(type, "audio/", 6);
	if (mime_param(mime, "codecs", codecs, sizeof codecs)) {
		char *c = codecs;
		while (*c) {
			char one[48];
			size_t j = 0;
			int codec, ok;
			const struct av_codec_impl *im;

			while (*c == ' ' || *c == ',')
				c++;
			while (*c && *c != ',' && j < sizeof one - 1)
				one[j++] = *c++;
			while (j > 0 && one[j - 1] == ' ')
				j--;
			one[j] = 0;
			if (!j)
				continue;
			any_codec = 1;
			codec = av_codec_of_string(one, &ok);
			if (codec == AV_C_NONE || !ok || !fits(fmt, codec, audio_type))
				return 0;
			im = impl_of(codec);
			if (im != NULL && im->kind == AV_VIDEO) {
				max_w = im->smooth_w;
				max_h = im->smooth_h;
				max_fps = im->smooth_fps;
			}
		}
	} else {
		/* no codecs: is there a decoder for anything this container holds */
		all_ok = 0;
		for (i = 0; i < NIMPL; i++) {
			/* (a video type: a video codec of the web; the tests' I420 does not count) */
			if (impls[i]->codec == AV_C_I420)
				continue;
			if (!audio_type && !strncmp(type, "video/", 6) && impls[i]->kind != AV_VIDEO)
				continue;
			if (fits(fmt, impls[i]->codec, audio_type))
				all_ok = 1;
		}
		if (!all_ok)
			return 0;
	}
	if (smooth != NULL) {
		char v[32];
		int w = 0, h = 0;
		double fps = 0;
		if (mime_param(mime, "width", v, sizeof v)) w = atoi(v);
		if (mime_param(mime, "height", v, sizeof v)) h = atoi(v);
		if (mime_param(mime, "framerate", v, sizeof v)) fps = atof(v);
		*smooth = 1;
		if (max_w > 0 && (w > max_w || h > max_h))
			*smooth = 0;
		if (max_fps > 0 && fps > max_fps + 0.5)
			*smooth = 0;
	}
	/* (an HDR transfer -- YouTube asks with eotf= -- is not shown right: no; no tunnel mode, no
	 * encrypted blocks; a size well past what decodes smoothly: no -- a player probing the
	 * largest size it may ask for -- YouTube's width= / height= -- stays at what the Pi decodes) */
	{
		char v[32];
		int w = 0, h = 0;
		if (mime_param(mime, "eotf", v, sizeof v) && strcasecmp(v, "bt709") && strcasecmp(v, "sdr"))
			return 0;
		if (mime_param(mime, "tunnelmode", v, sizeof v) && !strcasecmp(v, "true"))
			return 0;
		if (mime_param(mime, "cryptoblockformat", v, sizeof v))
			return 0;
		if (mime_param(mime, "width", v, sizeof v)) w = atoi(v);
		if (mime_param(mime, "height", v, sizeof v)) h = atoi(v);
		if (max_w > 0 && (w > max_w * 3 / 2 || h > max_h * 3 / 2))
			return 0;
	}
	if (!any_codec)
		return mse ? 2 : 1;
	return 2;
}
