/*
 * user/Libs/av/av_riff.c -- WAV (RIFF / WAVE): PCM (8 / 16 / 24 / 32-bit integer, 32 / 64-bit
 * float, A-law, mu-law; WAVE_FORMAT_EXTENSIBLE), cut into packets of 4096 frames.
 */
#include <stdio.h>
#include "av_int.h"

#define WAV_PACKET 4096

struct wav {
	int64_t p;			/* the next chunk / the next packet */
	int64_t data_start, data_end;	/* the data chunk */
	int block;			/* bytes a frame */
	int in_data;
	int have_fmt;
};

static void *wav_create(struct av_demux *d)
{
	struct wav *w = (struct wav *) calloc(1, sizeof *w);
	if (w != NULL)
		w->p = 12;
	strcpy(d->name, "wav");
	return w;
}

static void wav_destroy(void *priv) { free(priv); }

static int wav_fmt(struct av_demux *d, struct wav *w, const uint8_t *f, uint32_t n)
{
	struct av_track *t;
	int tag, bits;

	if (n < 16)
		return AV_ERR;
	av__clear_tracks(d);
	t = av__new_track(d);
	tag = (int) av_rl16(f);
	t->kind = AV_AUDIO;
	t->number = 1;
	t->channels = (int) av_rl16(f + 2);
	t->rate = (int) av_rl32(f + 4);
	w->block = (int) av_rl16(f + 12);
	bits = (int) av_rl16(f + 14);
	t->bits = bits;
	if (tag == 0xFFFE && n >= 40)
		tag = (int) av_rl16(f + 24);	/* the sub-format GUID's first word */
	if (tag == 1)
		t->codec = bits == 8 ? AV_C_PCM_U8 : bits == 16 ? AV_C_PCM_S16LE : bits == 24 ? AV_C_PCM_S24LE :
			bits == 32 ? AV_C_PCM_S32LE : AV_C_NONE;
	else if (tag == 3)
		t->codec = bits == 32 ? AV_C_PCM_F32LE : bits == 64 ? AV_C_PCM_F64LE : AV_C_NONE;
	else if (tag == 6)
		t->codec = AV_C_ALAW;
	else if (tag == 7)
		t->codec = AV_C_ULAW;
	else
		t->codec = AV_C_NONE;
	snprintf(t->codec_id, sizeof t->codec_id, "wav %d", tag);
	snprintf(t->codec_str, sizeof t->codec_str, "%d", tag);
	if (t->channels <= 0 || t->rate <= 0 || w->block <= 0)
		return AV_ERR;
	w->have_fmt = 1;
	d->init_new = 1;
	return AV_OK;
}

static int wav_read(struct av_demux *d, void *priv, struct av_packet *pkt)
{
	struct wav *w = (struct wav *) priv;

	for (;;) {
		const uint8_t *h;
		uint32_t size;

		if (w->in_data) {
			int64_t left = w->data_end - w->p, n;
			const struct av_track *t = &d->tracks[0];
			size_t avail;

			if (left < w->block)
				return AV_EOF;
			n = (int64_t) WAV_PACKET * w->block;
			if (n > left)
				n = left - left % w->block;
			avail = av__avail(d, w->p);
			if ((int64_t) avail < n) {
				if (d->ended && avail >= (size_t) w->block)
					n = (int64_t) (avail - avail % (size_t) w->block);
				else {
					if (avail == 0 && (w->p < d->base + (int64_t) d->off || w->p > av__end(d)))
						d->want = w->p;
					return AV_AGAIN;
				}
			}
			pkt->track = 0;
			pkt->pts = pkt->dts = (w->p - w->data_start) / w->block * AV_US / t->rate;
			pkt->dur = n / w->block * AV_US / t->rate;
			pkt->key = 1;
			pkt->pos = w->p;
			pkt->size = (size_t) n;
			pkt->data = av__copy(d, w->p, (size_t) n);
			w->p += n;
			av__consume(d, w->p);
			return pkt->data != NULL ? AV_OK : AV_ENOMEM;
		}
		if (av__peek(d, 0, 12) != NULL) {
			h = av__peek(d, 0, 12);
			if (memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4))
				return AV_EUNSUP;
		}
		h = av__peek(d, w->p, 8);
		if (h == NULL)
			return AV_AGAIN;
		size = av_rl32(h + 4);
		if (!memcmp(h, "fmt ", 4)) {
			const uint8_t *f = av__peek(d, w->p + 8, size);
			int r;
			if (f == NULL)
				return AV_AGAIN;
			r = wav_fmt(d, w, f, size);
			if (r != AV_OK)
				return r;
		} else if (!memcmp(h, "data", 4)) {
			if (!w->have_fmt)
				return AV_ERR;
			w->data_start = w->p + 8;
			w->data_end = size == 0xFFFFFFFFu || size == 0 ? INT64_MAX : w->data_start + size;
			if (d->tracks[0].rate > 0 && w->data_end != INT64_MAX)
				d->duration = d->tracks[0].duration =
					(size / w->block) * AV_US / d->tracks[0].rate;
			w->p = w->data_start;
			w->in_data = 1;
			continue;
		}
		w->p += 8 + size + (size & 1);
		av__consume(d, w->p);
	}
}

static void wav_restart(struct av_demux *d, void *priv, int64_t pos)
{
	struct wav *w = (struct wav *) priv;
	(void) d;
	if (w->in_data && pos >= w->data_start)
		w->p = pos - (pos - w->data_start) % w->block;
}

static int64_t wav_seek(struct av_demux *d, void *priv, av_us t, av_us *at)
{
	struct wav *w = (struct wav *) priv;
	int64_t frame;

	if (!w->in_data || d->ntracks < 1)
		return -1;
	frame = t * d->tracks[0].rate / AV_US;
	w->p = w->data_start + frame * w->block;
	if (w->p > w->data_end)
		w->p = w->data_end - (w->data_end - w->data_start) % w->block;
	if (at != NULL)
		*at = (w->p - w->data_start) / w->block * AV_US / d->tracks[0].rate;
	return w->p;
}

static void wav_reset(struct av_demux *d, void *priv) { (void) d; (void) priv; }

const struct av_fmt_ops av_wav_ops = {
	"wav", wav_create, wav_destroy, wav_read, wav_restart, wav_seek, wav_reset
};
