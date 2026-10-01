/*
 * user/av/av_opus.c -- Opus on libopus (BSD; fixed or float, NEON): compiled with AV_WITH_OPUS
 * when third_party/opus-<version> is vendored (docs/06 §44: not vendored yet -- this glue has not
 * been compiled; its API is libopus' stable opus_multistream.h).
 *
 * The setup: Matroska's CodecPrivate is an OpusHead ("OpusHead", version, channels, pre-skip,
 * rate, gain, mapping family [, streams, coupled, mapping]); MP4's dOps is the same fields
 * without the magic (version 0). Output 48 kHz float; the pre-skip dropped at the start and after
 * a flush (a seek: the player decodes 80 ms before the target, trimmed by time). Channels in
 * Vorbis order are put in WAVE order (av_frame's) for 3..8 channels.
 */
#ifdef AV_WITH_OPUS
#include "av_int.h"
#include "opus_multistream.h"

struct opus {
	OpusMSDecoder *dec;
	int channels, preskip, skip;
	float pcm[5760 * 8];
	float tmp[5760 * 8];
	struct av_frame f;
	int have;
};

/* Vorbis -> WAVE channel order (L R C LFE BL BR SL SR) */
static const unsigned char reorder[9][8] = {
	{ 0 }, { 0 }, { 0, 1 }, { 0, 2, 1 }, { 0, 1, 2, 3 }, { 0, 2, 1, 3, 4 },
	{ 0, 2, 1, 5, 3, 4 }, { 0, 2, 1, 6, 5, 3, 4 }, { 0, 2, 1, 7, 5, 6, 3, 4 },
};

static void *opus_open(const struct av_track *t)
{
	struct opus *c = (struct opus *) calloc(1, sizeof *c);
	const uint8_t *e = t->extra;
	size_t n = t->extra_len;
	int ch = t->channels > 0 ? t->channels : 2, family = 0, streams = 1, coupled = ch > 1;
	unsigned char map[8] = { 0, 1 };
	int err = 0, i;

	if (c == NULL)
		return NULL;
	if (e != NULL && n >= 19 && !memcmp(e, "OpusHead", 8)) {
		ch = e[9];
		c->preskip = (int) av_rl16(e + 10);
		family = e[18];
		if (family != 0 && n >= 21 + (size_t) ch) {
			streams = e[19];
			coupled = e[20];
			memcpy(map, e + 21, (size_t) (ch < 8 ? ch : 8));
		}
	} else if (e != NULL && n >= 11) {
		/* dOps: big-endian, no magic */
		ch = e[1];
		c->preskip = (int) av_rb16(e + 2);
		family = e[10];
		if (family != 0 && n >= 13 + (size_t) ch) {
			streams = e[11];
			coupled = e[12];
			memcpy(map, e + 13, (size_t) (ch < 8 ? ch : 8));
		}
	}
	if (ch < 1 || ch > 8) {
		free(c);
		return NULL;
	}
	if (family == 0) {
		streams = 1;
		coupled = ch > 1;
		for (i = 0; i < ch; i++)
			map[i] = (unsigned char) i;
	}
	c->channels = ch;
	c->skip = c->preskip;
	c->dec = opus_multistream_decoder_create(48000, ch, streams, coupled, map, &err);
	if (c->dec == NULL || err != OPUS_OK) {
		free(c);
		return NULL;
	}
	return c;
}

static int opus_send(void *cv, const struct av_packet *p)
{
	struct opus *c = (struct opus *) cv;
	int n, k, i, skip;

	if (p == NULL)
		return AV_OK;
	n = opus_multistream_decode_float(c->dec, p->data, (opus_int32) p->size, c->tmp, 5760, 0);
	if (n <= 0)
		return AV_ERR;
	skip = c->skip < n ? c->skip : n;
	c->skip -= skip;
	/* DiscardPadding: the end of the last packet */
	if (p->discard_end > 0) {
		int drop = (int) (p->discard_end * 48000 / AV_US);
		n = drop < n ? n - drop : 0;
	}
	if (n <= skip)
		return AV_OK;
	for (i = skip; i < n; i++)
		for (k = 0; k < c->channels; k++)
			c->pcm[(i - skip) * c->channels + k] = c->tmp[i * c->channels + reorder[c->channels][k]];
	memset(&c->f, 0, sizeof c->f);
	c->f.kind = AV_AUDIO;
	c->f.pts = p->pts + (av_us) skip * AV_US / 48000;
	c->f.rate = 48000;
	c->f.channels = c->channels;
	c->f.samples = n - skip;
	c->f.dur = (av_us) (n - skip) * AV_US / 48000;
	c->f.pcm = c->pcm;
	c->have = 1;
	return AV_OK;
}

static int opus_receive(void *cv, struct av_frame *f)
{
	struct opus *c = (struct opus *) cv;

	if (!c->have)
		return AV_AGAIN;
	c->have = 0;
	*f = c->f;
	return AV_OK;
}

static void opus_flush(void *cv)
{
	struct opus *c = (struct opus *) cv;
	opus_multistream_decoder_ctl(c->dec, OPUS_RESET_STATE);
	c->have = 0;
}

static void opus_close(void *cv)
{
	struct opus *c = (struct opus *) cv;
	opus_multistream_decoder_destroy(c->dec);
	free(c);
}

const struct av_codec_impl av_opus_codec = {
	AV_C_OPUS, AV_AUDIO, "opus", "libopus", 0, 0, 0,
	opus_open, opus_send, opus_receive, opus_flush, opus_close, NULL
};
#endif
