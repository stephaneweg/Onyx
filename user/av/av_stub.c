/*
 * user/av/av_stub.c -- stand-in decoders for the tests (the PC bench's NS_MEDIASTUB=1): VP8, VP9,
 * AV1, H.264, Opus, AAC and Vorbis "decoded" into grey frames (a band that moves with the time:
 * playback is visible) and silence, at the packets' times and the tracks' sizes. With them a
 * site's whole media path -- YouTube's player choosing formats, fetching segments, appending
 * them, buffering, playing, seeking -- runs on its real streams where the real decoders are not
 * vendored (docs/06 §44). Never on in a real build unless asked (av_codec_enable_stubs).
 */
#include "av_int.h"

struct stub {
	int kind, w, h;
	uint8_t *buf;
	float *pcm;
	int pcm_cap;
	struct av_frame f;
	int have;
};

static void *stub_open(const struct av_track *t)
{
	struct stub *s = (struct stub *) calloc(1, sizeof *s);

	if (s == NULL)
		return NULL;
	s->kind = t->kind;
	s->w = t->width > 0 ? t->width : 640;
	s->h = t->height > 0 ? t->height : 360;
	if (s->w > 4096 || s->h > 4096) {
		free(s);
		return NULL;
	}
	if (s->kind == AV_VIDEO) {
		int cw = (s->w + 1) / 2, ch = (s->h + 1) / 2;
		s->buf = (uint8_t *) malloc((size_t) s->w * s->h + 2 * (size_t) cw * ch);
		if (s->buf == NULL) {
			free(s);
			return NULL;
		}
		memset(s->buf + (size_t) s->w * s->h, 128, 2 * (size_t) cw * ch);
	}
	return s;
}

static int stub_send(void *c, const struct av_packet *p)
{
	struct stub *s = (struct stub *) c;

	if (p == NULL)
		return AV_OK;
	memset(&s->f, 0, sizeof s->f);
	s->f.kind = s->kind;
	s->f.pts = p->pts;
	if (s->kind == AV_VIDEO) {
		int cw = (s->w + 1) / 2, band = (int) ((p->pts / 40000) % (s->w > 8 ? s->w - 8 : 1)), y;
		for (y = 0; y < s->h; y++) {
			memset(s->buf + (size_t) y * s->w, 110, (size_t) s->w);
			memset(s->buf + (size_t) y * s->w + band, 220, 8);
		}
		s->f.dur = p->dur;
		s->f.pix = AV_PIX_I420;
		s->f.width = s->w;
		s->f.height = s->h;
		s->f.plane[0] = s->buf;
		s->f.plane[1] = s->buf + (size_t) s->w * s->h;
		s->f.plane[2] = s->f.plane[1] + (size_t) cw * ((s->h + 1) / 2);
		s->f.stride[0] = s->w;
		s->f.stride[1] = s->f.stride[2] = cw;
	} else {
		int n = (int) ((p->dur > 0 ? p->dur : 20000) * 48000 / AV_US);
		if (n * 2 > s->pcm_cap) {
			free(s->pcm);
			s->pcm = (float *) calloc((size_t) n * 2, sizeof(float));
			s->pcm_cap = s->pcm ? n * 2 : 0;
			if (s->pcm == NULL)
				return AV_ENOMEM;
		}
		s->f.dur = (av_us) n * AV_US / 48000;
		s->f.rate = 48000;
		s->f.channels = 2;
		s->f.samples = n;
		s->f.pcm = s->pcm;
	}
	s->have = 1;
	return AV_OK;
}

static int stub_receive(void *c, struct av_frame *f)
{
	struct stub *s = (struct stub *) c;

	if (!s->have)
		return AV_AGAIN;
	s->have = 0;
	*f = s->f;
	return AV_OK;
}

static void stub_flush(void *c) { ((struct stub *) c)->have = 0; }

static void stub_close(void *c)
{
	struct stub *s = (struct stub *) c;
	free(s->buf);
	free(s->pcm);
	free(s);
}

#define STUB(id, kind, name) { id, kind, name, "stub (tests)", 854, 480, 30, \
	stub_open, stub_send, stub_receive, stub_flush, stub_close, NULL }
static const struct av_codec_impl stubs[] = {
	STUB(AV_C_VP9, AV_VIDEO, "vp9"), STUB(AV_C_VP8, AV_VIDEO, "vp8"), STUB(AV_C_AV1, AV_VIDEO, "av1"),
	STUB(AV_C_H264, AV_VIDEO, "h264"), STUB(AV_C_OPUS, AV_AUDIO, "opus"), STUB(AV_C_AAC, AV_AUDIO, "aac"),
	STUB(AV_C_VORBIS, AV_AUDIO, "vorbis"),
};

static int stubs_on;

void av_codec_enable_stubs(void)
{
	stubs_on = 1;
}

const struct av_codec_impl *av__stub_of(int codec)
{
	size_t i;

	if (!stubs_on)
		return NULL;
	for (i = 0; i < sizeof stubs / sizeof stubs[0]; i++)
		if (stubs[i].codec == codec)
			return &stubs[i];
	return NULL;
}
