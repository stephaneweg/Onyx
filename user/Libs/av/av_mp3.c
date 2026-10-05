/*
 * user/Libs/av/av_mp3.c -- MPEG audio (MP3, and layers I / II): the raw stream (an ID3v2 tag skipped,
 * frames cut by their headers, the Xing / Info / VBRI frame count for the duration and the
 * Xing table for seeking, else a constant bit rate's estimate), and the decoder on minimp3
 * (third_party/minimp3, CC0 -- the Media Player's copy; its public names renamed here so that a
 * program linking both does not see them twice).
 */
#include <stdio.h>
#include "av_int.h"

#define mp3dec_init av__mp3dec_init
#define mp3dec_decode_frame av__mp3dec_decode_frame
#define mp3dec_f32_to_s16 av__mp3dec_f32_to_s16
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_FLOAT_OUTPUT
#define MINIMP3_NO_STDIO
#include "../../../third_party/minimp3/minimp3.h"

/* ---- the frame header ------------------------------------------------------------------- */

struct mhdr { int version, layer, rate, channels, bytes, samples, bitrate; };

static const short br_v1[3][16] = {
	{ 0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, 0 },	/* L1 */
	{ 0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, 0 },	/* L2 */
	{ 0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0 },	/* L3 */
};
static const short br_v2[2][16] = {
	{ 0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256, 0 },	/* L1 */
	{ 0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0 },		/* L2, L3 */
};

/* 4 bytes -> 1 a frame header (h filled), 0 not one */
static int mhead(const uint8_t *p, struct mhdr *h)
{
	int ver, lay, bri, sri, pad, br, sr;

	if (p[0] != 0xFF || (p[1] & 0xE0) != 0xE0)
		return 0;
	ver = (p[1] >> 3) & 3;		/* 0 MPEG 2.5, 2 MPEG 2, 3 MPEG 1 */
	lay = 4 - ((p[1] >> 1) & 3);	/* 1..3 (4: reserved) */
	bri = p[2] >> 4;
	sri = (p[2] >> 2) & 3;
	pad = (p[2] >> 1) & 1;
	if (ver == 1 || lay == 4 || bri == 0 || bri == 15 || sri == 3)
		return 0;
	sr = sri == 0 ? 44100 : sri == 1 ? 48000 : 32000;
	if (ver == 2) sr /= 2;
	else if (ver == 0) sr /= 4;
	br = ver == 3 ? br_v1[lay - 1][bri] : br_v2[lay == 1 ? 0 : 1][bri];
	h->version = ver;
	h->layer = lay;
	h->rate = sr;
	h->channels = (p[3] >> 6) == 3 ? 1 : 2;
	h->bitrate = br;
	if (lay == 1) {
		h->bytes = (12 * br * 1000 / sr + pad) * 4;
		h->samples = 384;
	} else if (lay == 2 || ver == 3) {
		h->bytes = 144 * br * 1000 / sr + pad;
		h->samples = 1152;
	} else {
		h->bytes = 72 * br * 1000 / sr + pad;
		h->samples = 576;
	}
	return h->bytes > 4;
}

static int same_stream(const struct mhdr *a, const struct mhdr *b)
{
	return a->version == b->version && a->layer == b->layer && a->rate == b->rate;
}

/* ---- the container ---------------------------------------------------------------------- */

struct mp3 {
	int64_t p;
	int64_t data_start;
	int started;			/* the ID3 tag behind us */
	struct mhdr first;
	int have_first;
	uint64_t samples;		/* the samples before p (the next frame's time) */
	uint32_t xing_frames;
	uint8_t toc[100];
	int has_toc;
	uint32_t xing_bytes;
};

static void *mp3_create(struct av_demux *d)
{
	strcpy(d->name, "mp3");
	return calloc(1, sizeof(struct mp3));
}

static void mp3_destroy(void *priv) { free(priv); }

/* the Xing / Info / VBRI header in the first frame (f: its bytes) */
static void mp3_xing(struct av_demux *d, struct mp3 *m, const uint8_t *f, const struct mhdr *h)
{
	int side = h->version == 3 ? (h->channels == 1 ? 17 : 32) : (h->channels == 1 ? 9 : 17);
	const uint8_t *x = f + 4 + side;

	if (h->bytes >= 4 + side + 16 && (!memcmp(x, "Xing", 4) || !memcmp(x, "Info", 4))) {
		uint32_t flags = av_rb32(x + 4);
		const uint8_t *q = x + 8;
		if (flags & 1) { m->xing_frames = av_rb32(q); q += 4; }
		if (flags & 2) { m->xing_bytes = av_rb32(q); q += 4; }
		if ((flags & 4) && q + 100 <= f + h->bytes) {
			memcpy(m->toc, q, 100);
			m->has_toc = 1;
		}
	} else if (h->bytes >= 4 + 32 + 18 && !memcmp(f + 36, "VBRI", 4)) {
		m->xing_bytes = av_rb32(f + 36 + 10);
		m->xing_frames = av_rb32(f + 36 + 14);
	}
	if (m->xing_frames && h->rate)
		d->duration = d->tracks[0].duration = (av_us) ((uint64_t) m->xing_frames * h->samples * AV_US / (uint64_t) h->rate);
}

static int mp3_read(struct av_demux *d, void *priv, struct av_packet *pkt)
{
	struct mp3 *m = (struct mp3 *) priv;

	for (;;) {
		const uint8_t *p;
		struct mhdr h, h2;
		size_t avail;

		if (!m->started) {
			/* an ID3v2 tag: its size is syncsafe (4 x 7 bits) */
			p = av__peek(d, m->p, 10);
			if (p == NULL)
				return AV_AGAIN;
			if (!memcmp(p, "ID3", 3)) {
				uint32_t sz = (uint32_t) (p[6] & 127) << 21 | (uint32_t) (p[7] & 127) << 14 |
					(uint32_t) (p[8] & 127) << 7 | (p[9] & 127);
				m->p += 10 + sz + ((p[5] & 0x10) ? 10 : 0);
				av__consume(d, m->p);
				continue;
			}
			m->started = 1;
			m->data_start = m->p;
		}
		avail = av__avail(d, m->p);
		p = avail >= 4 ? av__peek(d, m->p, avail) : NULL;
		if (p == NULL) {
			if (m->p < d->base + (int64_t) d->off || m->p > av__end(d))
				d->want = m->p;
			return d->ended ? AV_EOF : AV_AGAIN;
		}
		if (!mhead(p, &h) || (m->have_first && !same_stream(&h, &m->first))) {
			/* not a frame here: on to the next sync word */
			size_t i;
			for (i = 1; i + 4 <= avail; i++)
				if (p[i] == 0xFF && (p[i + 1] & 0xE0) == 0xE0 && mhead(p + i, &h) &&
				    (!m->have_first || same_stream(&h, &m->first)))
					break;
			m->p += (int64_t) i;
			av__consume(d, m->p);
			if (i + 4 > avail)
				return d->ended ? AV_EOF : AV_AGAIN;
			continue;
		}
		if ((size_t) h.bytes > avail) {
			if (d->ended)
				return AV_EOF;
			return AV_AGAIN;
		}
		/* the next frame must follow (else this was a false sync) -- unless at the end */
		if ((size_t) h.bytes + 4 <= avail) {
			if (!mhead(p + h.bytes, &h2) || !same_stream(&h, &h2)) {
				if (!m->have_first || (p[h.bytes] != 'T' && p[h.bytes] != 'I')) {	/* (not a TAG / ID3 at the end) */
					m->p++;
					continue;
				}
			}
		} else if (!d->ended && !m->have_first) {
			return AV_AGAIN;
		}
		if (!m->have_first) {
			struct av_track *t;
			m->first = h;
			m->have_first = 1;
			av__clear_tracks(d);
			t = av__new_track(d);
			t->number = 1;
			t->kind = AV_AUDIO;
			t->codec = AV_C_MP3;
			t->rate = h.rate;
			t->channels = h.channels;
			strcpy(t->codec_id, h.layer == 3 ? "mp3" : h.layer == 2 ? "mp2" : "mp1");
			strcpy(t->codec_str, "mp3");
			t->default_dur = (av_us) h.samples * AV_US / h.rate;
			d->init_new = 1;
			mp3_xing(d, m, p, &h);
		}
		pkt->track = 0;
		pkt->pts = pkt->dts = (av_us) (m->samples * AV_US / (uint64_t) h.rate);
		pkt->dur = (av_us) h.samples * AV_US / h.rate;
		pkt->key = 1;
		pkt->pos = m->p;
		pkt->size = (size_t) h.bytes;
		pkt->data = av__copy(d, m->p, (size_t) h.bytes);
		m->p += h.bytes;
		m->samples += (uint64_t) h.samples;
		av__consume(d, m->p);
		return pkt->data != NULL ? AV_OK : AV_ENOMEM;
	}
}

static void mp3_restart(struct av_demux *d, void *priv, int64_t pos)
{
	struct mp3 *m = (struct mp3 *) priv;
	(void) d;
	m->p = pos;
}

static int64_t mp3_seek(struct av_demux *d, void *priv, av_us t, av_us *at)
{
	struct mp3 *m = (struct mp3 *) priv;
	av_us dur = d->duration;
	int64_t off;

	if (!m->have_first || t < 0)
		return -1;
	if (m->has_toc && m->xing_bytes && dur > 0) {
		/* the Xing table: 100 points, each a 256th of the bytes */
		double pc = (double) t * 100.0 / (double) dur;
		int i = pc < 0 ? 0 : pc > 99 ? 99 : (int) pc;
		double a = m->toc[i], b = i < 99 ? m->toc[i + 1] : 256.0;
		double f = a + (b - a) * (pc - i);
		off = m->data_start + (int64_t) (f / 256.0 * m->xing_bytes);
	} else {
		/* a constant bit rate's estimate */
		off = m->data_start + (int64_t) ((double) t / 1e6 * m->first.bitrate * 125.0);
	}
	m->p = off;
	m->samples = (uint64_t) ((double) t / 1e6 * m->first.rate);
	m->samples -= m->samples % (uint64_t) m->first.samples;
	if (at)
		*at = (av_us) (m->samples * AV_US / (uint64_t) m->first.rate);
	return off;
}

static void mp3_reset(struct av_demux *d, void *priv)
{
	struct mp3 *m = (struct mp3 *) priv;
	m->p = av__end(d);
}

const struct av_fmt_ops av_mp3_ops = {
	"mp3", mp3_create, mp3_destroy, mp3_read, mp3_restart, mp3_seek, mp3_reset
};

/* the start of an MPEG audio stream: an ID3 tag, or two frames in a row */
int av__mp3_probe(const uint8_t *p, size_t n)
{
	struct mhdr h, h2;

	if (n >= 3 && !memcmp(p, "ID3", 3))
		return 1;
	if (n >= 4 && mhead(p, &h) && (size_t) h.bytes + 4 <= n)
		return mhead(p + h.bytes, &h2) && same_stream(&h, &h2);
	return 0;
}

/* ---- the decoder ------------------------------------------------------------------------- */

struct mp3dec {
	mp3dec_t dec;
	float pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
	struct av_frame f;
	int have;
};

static void *mp3d_open(const struct av_track *t)
{
	struct mp3dec *c = (struct mp3dec *) calloc(1, sizeof *c);
	(void) t;
	if (c != NULL)
		mp3dec_init(&c->dec);
	return c;
}

static int mp3d_send(void *cv, const struct av_packet *p)
{
	struct mp3dec *c = (struct mp3dec *) cv;
	mp3dec_frame_info_t info;
	int n;

	if (p == NULL)
		return AV_OK;
	n = mp3dec_decode_frame(&c->dec, p->data, (int) p->size, c->pcm, &info);
	if (n <= 0 || info.channels <= 0)
		return AV_OK;		/* (the decoder's first frames, a Xing frame: nothing yet) */
	memset(&c->f, 0, sizeof c->f);
	c->f.kind = AV_AUDIO;
	c->f.pts = p->pts;
	c->f.rate = info.hz;
	c->f.channels = info.channels;
	c->f.samples = n;
	c->f.dur = (av_us) n * AV_US / info.hz;
	c->f.pcm = c->pcm;
	c->have = 1;
	return AV_OK;
}

static int mp3d_receive(void *cv, struct av_frame *f)
{
	struct mp3dec *c = (struct mp3dec *) cv;

	if (!c->have)
		return AV_AGAIN;
	c->have = 0;
	*f = c->f;
	return AV_OK;
}

static void mp3d_flush(void *cv)
{
	struct mp3dec *c = (struct mp3dec *) cv;
	mp3dec_init(&c->dec);
	c->have = 0;
}

static void mp3d_close(void *cv) { free(cv); }

const struct av_codec_impl av_mp3_codec = {
	AV_C_MP3, AV_AUDIO, "mp3", "minimp3", 0, 0, 0,
	mp3d_open, mp3d_send, mp3d_receive, mp3d_flush, mp3d_close, NULL
};
