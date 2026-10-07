/*
 * user/Libs/av/av_flac.c -- FLAC: the native container ("fLaC", metadata blocks, frames) and the
 * decoder (all of the format: fixed and LPC predictors, Rice partitions with escapes, the
 * stereo decorrelations, wasted bits, 4..32 bits a sample), written from the format's
 * specification (RFC 9639). The decoder also takes FLAC from Matroska (A_FLAC: CodecPrivate =
 * "fLaC" + the metadata) and MP4 (fLaC / dfLa: the metadata blocks).
 *
 * The container cuts frames at the next frame header whose CRC-8 is right and where the bytes
 * before it end with a right CRC-16 (a frame's own check): no length field exists.
 */
#include "av_int.h"

/* ---- checks ----------------------------------------------------------------------------- */

static uint8_t crc8(const uint8_t *p, size_t n)
{
	uint8_t c = 0;
	size_t i;
	int b;

	for (i = 0; i < n; i++) {
		c ^= p[i];
		for (b = 0; b < 8; b++)
			c = (uint8_t) (c & 0x80 ? (c << 1) ^ 0x07 : c << 1);
	}
	return c;
}

static uint16_t crc16_table[256];
static void crc16_init(void)
{
	int i, b;

	if (crc16_table[1])
		return;
	for (i = 0; i < 256; i++) {
		uint16_t c = (uint16_t) (i << 8);
		for (b = 0; b < 8; b++)
			c = (uint16_t) (c & 0x8000 ? (c << 1) ^ 0x8005 : c << 1);
		crc16_table[i] = c;
	}
}
static inline uint16_t crc16_byte(uint16_t c, uint8_t v)
{
	return (uint16_t) ((c << 8) ^ crc16_table[(c >> 8) ^ v]);
}

/* ---- the frame header ------------------------------------------------------------------- */

struct fhdr {
	int blocksize, rate_code, rate, chan, bps_code, variable, hlen;
	uint64_t number;
};

/* a frame header at p (n bytes) -> 1 ok, 0 more bytes, -1 not one */
static int parse_header(const uint8_t *p, size_t n, struct fhdr *h)
{
	size_t i;
	int bs, rc, k, extra;
	uint64_t v;

	if (n < 2)
		return 0;
	if (p[0] != 0xFF || (p[1] & 0xFE) != 0xF8)
		return -1;
	if (n < 5)
		return 0;
	h->variable = p[1] & 1;
	bs = p[2] >> 4;
	rc = p[2] & 15;
	h->chan = p[3] >> 4;
	h->bps_code = (p[3] >> 1) & 7;
	if (bs == 0 || rc == 15 || h->chan > 10 || h->bps_code == 3 || (p[3] & 1))
		return -1;
	/* the coded number (UTF-8 like, up to 7 bytes) */
	i = 4;
	if (!(p[i] & 0x80)) { v = p[i]; extra = 0; }
	else if ((p[i] & 0xE0) == 0xC0) { v = p[i] & 0x1F; extra = 1; }
	else if ((p[i] & 0xF0) == 0xE0) { v = p[i] & 0x0F; extra = 2; }
	else if ((p[i] & 0xF8) == 0xF0) { v = p[i] & 0x07; extra = 3; }
	else if ((p[i] & 0xFC) == 0xF8) { v = p[i] & 0x03; extra = 4; }
	else if ((p[i] & 0xFE) == 0xFC) { v = p[i] & 0x01; extra = 5; }
	else if (p[i] == 0xFE) { v = 0; extra = 6; }
	else return -1;
	i++;
	if (n < i + (size_t) extra + 4)
		return 0;
	for (k = 0; k < extra; k++, i++) {
		if ((p[i] & 0xC0) != 0x80)
			return -1;
		v = v << 6 | (p[i] & 0x3F);
	}
	h->number = v;
	if (bs == 1) h->blocksize = 192;
	else if (bs <= 5) h->blocksize = 576 << (bs - 2);
	else if (bs == 6) h->blocksize = p[i++] + 1;
	else if (bs == 7) { h->blocksize = (int) av_rb16(p + i) + 1; i += 2; }
	else h->blocksize = 256 << (bs - 8);
	h->rate_code = rc;
	h->rate = 0;
	if (n < i + 3)
		return 0;
	if (rc == 12) h->rate = p[i++] * 1000;
	else if (rc == 13) { h->rate = (int) av_rb16(p + i); i += 2; }
	else if (rc == 14) { h->rate = (int) av_rb16(p + i) * 10; i += 2; }
	if (n < i + 1)
		return 0;
	if (crc8(p, i) != p[i])
		return -1;
	h->hlen = (int) i + 1;
	return 1;
}

long av__flac_frame_size(const uint8_t *p, size_t n, int final, int *blocksize, uint64_t *number,
		int *variable)
{
	struct fhdr h, h2;
	uint16_t c = 0;
	size_t q;
	int r = parse_header(p, n, &h);

	if (r <= 0)
		return r == 0 && !final ? 0 : -1;
	crc16_init();
	for (q = 0; q < (size_t) h.hlen; q++)
		c = crc16_byte(c, p[q]);
	for (; q < n; q++) {
		/* a frame ends where its CRC-16 makes the running check 0 and a header follows */
		if (c == 0 && q >= (size_t) h.hlen + 3 && p[q] == 0xFF && q + 1 < n && (p[q + 1] & 0xFE) == 0xF8 &&
		    parse_header(p + q, n - q, &h2) == 1 && h2.variable == h.variable)
			break;
		c = crc16_byte(c, p[q]);
	}
	if (q == n) {
		if (!final)
			return 0;
		if (c != 0)
			return -1;
	}
	if (blocksize) *blocksize = h.blocksize;
	if (number) *number = h.number;
	if (variable) *variable = h.variable;
	return (long) q;
}

/* ---- the container ---------------------------------------------------------------------- */

struct flacd {
	int64_t p;
	int in_frames;
	int64_t first_frame;
	uint64_t total;
	int minbs, maxbs;
	uint32_t maxframe;
	uint64_t *st_sample;	/* SEEKTABLE */
	uint64_t *st_off;
	int nst;
	int resync;
};

/* STREAMINFO (34 bytes) into a track */
static int streaminfo(struct av_track *t, const uint8_t *s, int *minbs, int *maxbs, uint32_t *maxframe,
		uint64_t *total)
{
	t->kind = AV_AUDIO;
	t->codec = AV_C_FLAC;
	if (minbs) *minbs = (int) av_rb16(s);
	if (maxbs) *maxbs = (int) av_rb16(s + 2);
	if (maxframe) *maxframe = av_rb24(s + 7);
	t->rate = (int) (av_rb24(s + 10) >> 4);
	t->channels = ((s[12] >> 1) & 7) + 1;
	t->bits = (((s[12] & 1) << 4) | (s[13] >> 4)) + 1;
	if (total)
		*total = ((uint64_t) (s[13] & 15) << 32) | av_rb32(s + 14);
	strcpy(t->codec_id, "fLaC");
	strcpy(t->codec_str, "flac");
	return t->rate > 0 ? 0 : -1;
}

static void *flacd_create(struct av_demux *d)
{
	struct flacd *f = (struct flacd *) calloc(1, sizeof *f);
	strcpy(d->name, "flac");
	return f;
}

static void flacd_destroy(void *priv)
{
	struct flacd *f = (struct flacd *) priv;

	if (f != NULL) {
		free(f->st_sample);
		free(f->st_off);
	}
	free(f);
}

static int flacd_read(struct av_demux *d, void *priv, struct av_packet *pkt)
{
	struct flacd *f = (struct flacd *) priv;

	for (;;) {
		const uint8_t *h;

		if (f->in_frames) {
			size_t avail = av__avail(d, f->p);
			const uint8_t *p = avail ? av__peek(d, f->p, avail) : NULL;
			long len;
			int bs, var;
			uint64_t num;
			const struct av_track *t = &d->tracks[0];

			if (p == NULL) {
				if (f->p < d->base + (int64_t) d->off || f->p > av__end(d))
					d->want = f->p;
				return d->ended ? AV_EOF : AV_AGAIN;
			}
			if (f->resync || p[0] != 0xFF) {
				/* find a frame header */
				size_t i;
				for (i = 0; i + 1 < avail; i++)
					if (p[i] == 0xFF && (p[i + 1] & 0xFE) == 0xF8) {
						struct fhdr fh;
						if (parse_header(p + i, avail - i, &fh) == 1)
							break;
					}
				f->p += (int64_t) i;
				av__consume(d, f->p);
				if (i + 1 >= avail)
					return d->ended ? AV_EOF : AV_AGAIN;
				f->resync = 0;
				continue;
			}
			len = av__flac_frame_size(p, avail, d->ended, &bs, &num, &var);
			if (len == 0) {
				if (avail > (f->maxframe ? f->maxframe * 2 + 64 : (16u << 20))) {
					f->resync = 1;	/* (garbage: on to the next header) */
					f->p++;
					continue;
				}
				return AV_AGAIN;
			}
			if (len < 0) {
				f->p++;
				f->resync = 1;
				continue;
			}
			pkt->track = 0;
			{
				uint64_t sample = var ? num : num * (uint64_t) (f->minbs && f->minbs == f->maxbs ? f->minbs : bs);
				pkt->pts = pkt->dts = (av_us) (sample * AV_US / (uint64_t) t->rate);
			}
			pkt->dur = (av_us) bs * AV_US / t->rate;
			pkt->key = 1;
			pkt->pos = f->p;
			pkt->size = (size_t) len;
			pkt->data = av__copy(d, f->p, (size_t) len);
			f->p += len;
			av__consume(d, f->p);
			return pkt->data != NULL ? AV_OK : AV_ENOMEM;
		}
		if (f->p == 0) {
			h = av__peek(d, 0, 4);
			if (h == NULL)
				return AV_AGAIN;
			if (memcmp(h, "fLaC", 4))
				return AV_EUNSUP;
			f->p = 4;
		}
		h = av__peek(d, f->p, 4);
		if (h == NULL)
			return AV_AGAIN;
		{
			int last = h[0] & 0x80, type = h[0] & 0x7F;
			uint32_t len = av_rb24(h + 1);
			const uint8_t *b = av__peek(d, f->p + 4, len);

			if (b == NULL)
				return AV_AGAIN;
			if (type == 0 && len >= 34) {
				struct av_track *t;
				av__clear_tracks(d);
				t = av__new_track(d);
				t->number = 1;
				if (streaminfo(t, b, &f->minbs, &f->maxbs, &f->maxframe, &f->total) < 0)
					return AV_ERR;
				/* the decoder's setup: the STREAMINFO */
				t->extra = (uint8_t *) malloc(34);
				if (t->extra != NULL) {
					memcpy(t->extra, b, 34);
					t->extra_len = 34;
				}
				if (f->total)
					d->duration = t->duration = (av_us) (f->total * AV_US / (uint64_t) t->rate);
				d->init_new = 1;
			} else if (type == 3) {
				int n = (int) (len / 18), i, k = 0;
				free(f->st_sample);
				free(f->st_off);
				f->st_sample = (uint64_t *) malloc((size_t) (n + 1) * sizeof *f->st_sample);
				f->st_off = (uint64_t *) malloc((size_t) (n + 1) * sizeof *f->st_off);
				if (f->st_sample != NULL && f->st_off != NULL)
					for (i = 0; i < n; i++) {
						uint64_t s = av_rb64(b + 18 * i);
						if (s == ~0ULL)
							continue;	/* placeholder */
						f->st_sample[k] = s;
						f->st_off[k] = av_rb64(b + 18 * i + 8);
						k++;
					}
				f->nst = k;
			}
			f->p += 4 + len;
			if (last) {
				if (d->ntracks == 0)
					return AV_ERR;
				f->in_frames = 1;
				f->first_frame = f->p;
			}
			av__consume(d, f->p);
		}
	}
}

static void flacd_restart(struct av_demux *d, void *priv, int64_t pos)
{
	struct flacd *f = (struct flacd *) priv;
	(void) d;
	if (f->in_frames) {
		f->p = pos;
		f->resync = 1;
	}
}

static int64_t flacd_seek(struct av_demux *d, void *priv, av_us t, av_us *at)
{
	struct flacd *f = (struct flacd *) priv;
	uint64_t sample;
	int i, best = -1;

	if (!f->in_frames || d->ntracks < 1)
		return -1;
	sample = (uint64_t) (t * d->tracks[0].rate / AV_US);
	for (i = 0; i < f->nst; i++)
		if (f->st_sample[i] <= sample)
			best = i;
	if (best >= 0) {
		f->p = f->first_frame + (int64_t) f->st_off[best];
		if (at) *at = (av_us) (f->st_sample[best] * AV_US / (uint64_t) d->tracks[0].rate);
	} else if (f->total > 0 && d->tracks[0].duration > 0 && t > 0) {
		/* no table: a guess by the bytes is not known here (the stream's length): the start */
		f->p = f->first_frame;
		if (at) *at = 0;
	} else {
		f->p = f->first_frame;
		if (at) *at = 0;
	}
	f->resync = 1;
	return f->p;
}

static void flacd_reset(struct av_demux *d, void *priv) { (void) d; (void) priv; }

const struct av_fmt_ops av_flac_ops = {
	"flac", flacd_create, flacd_destroy, flacd_read, flacd_restart, flacd_seek, flacd_reset
};

/* ---- the decoder ------------------------------------------------------------------------ */

struct bits {
	const uint8_t *p;
	size_t n, i;		/* bytes, the next byte */
	uint64_t acc;
	int nacc;
	int err;
};

static inline void br_fill(struct bits *b)
{
	while (b->nacc <= 56) {
		b->acc |= (uint64_t) (b->i < b->n ? b->p[b->i] : 0) << (56 - b->nacc);
		if (b->i >= b->n)
			b->err = b->i > b->n + 8;
		b->i++;
		b->nacc += 8;
	}
}
static inline uint32_t br_get(struct bits *b, int k)
{
	uint32_t v;

	if (k == 0)
		return 0;
	if (b->nacc < k)
		br_fill(b);
	v = (uint32_t) (b->acc >> (64 - k));
	b->acc <<= k;
	b->nacc -= k;
	return v;
}
static inline int32_t br_sget(struct bits *b, int k)
{
	uint32_t v;

	if (k == 0)
		return 0;
	if (k > 32)
		k = 32;
	v = br_get(b, k);
	if (k < 32 && (v >> (k - 1)))
		v |= ~0u << k;
	return (int32_t) v;
}
static inline uint32_t br_unary(struct bits *b)
{
	uint32_t z = 0;

	for (;;) {
		if (b->nacc == 0)
			br_fill(b);
		if (b->acc == 0) {
			z += (uint32_t) b->nacc;
			b->nacc = 0;
			b->acc = 0;
			if (b->err || z > (1u << 20)) {
				b->err = 1;
				return 0;
			}
			continue;
		}
		{
			int lz = __builtin_clzll(b->acc);
			if (lz >= b->nacc) {
				z += (uint32_t) b->nacc;
				b->nacc = 0;
				b->acc = 0;
				continue;
			}
			z += (uint32_t) lz;
			b->acc <<= lz + 1;
			b->nacc -= lz + 1;
			return z;
		}
	}
}

struct flac {
	int rate, channels, bits;
	int maxbs;
	int32_t *ch[8];
	int cap;
	float *out;
	int out_cap;
	struct av_frame frame;
	int have;
};

static void *flac_open(const struct av_track *t)
{
	struct flac *f = (struct flac *) calloc(1, sizeof *f);
	const uint8_t *e = t->extra;
	size_t n = t->extra_len;

	if (f == NULL)
		return NULL;
	f->rate = t->rate;
	f->channels = t->channels;
	f->bits = t->bits;
	/* the setup: "fLaC" + blocks, the blocks, or the STREAMINFO alone */
	if (e != NULL && n >= 4 && !memcmp(e, "fLaC", 4)) {
		e += 4;
		n -= 4;
	}
	if (e != NULL && n >= 38 && (e[0] & 0x7F) == 0 && av_rb24(e + 1) >= 34) {
		e += 4;
		n -= 4;
	}
	if (e != NULL && n >= 34) {
		struct av_track tt;
		memset(&tt, 0, sizeof tt);
		if (streaminfo(&tt, e, NULL, &f->maxbs, NULL, NULL) == 0) {
			f->rate = tt.rate;
			f->channels = tt.channels;
			f->bits = tt.bits;
		}
	}
	if (f->channels < 1 || f->channels > 8)
		f->channels = 2;
	crc16_init();
	return f;
}

static void flac_close(void *c)
{
	struct flac *f = (struct flac *) c;
	int i;

	if (f == NULL)
		return;
	for (i = 0; i < 8; i++)
		free(f->ch[i]);
	free(f->out);
	free(f);
}

/* a residual into r[order .. bs) */
static int residual(struct bits *b, int32_t *r, int bs, int order)
{
	int method = (int) br_get(b, 2), porder, parts, pb, esc, pi, i = order;

	if (method > 1)
		return -1;
	pb = method == 0 ? 4 : 5;
	esc = method == 0 ? 15 : 31;
	porder = (int) br_get(b, 4);
	parts = 1 << porder;
	if ((bs >> porder) < order || (bs & (parts - 1)))
		return -1;
	for (pi = 0; pi < parts; pi++) {
		int k = (int) br_get(b, pb);
		int end = (pi + 1) * (bs >> porder);
		if (k == esc) {
			int nb = (int) br_get(b, 5);
			for (; i < end; i++)
				r[i] = br_sget(b, nb);
		} else {
			for (; i < end; i++) {
				uint32_t q = br_unary(b);
				uint32_t v = q << k | br_get(b, k);
				r[i] = (int32_t) (v >> 1) ^ -(int32_t) (v & 1);
			}
		}
		if (b->err)
			return -1;
	}
	return 0;
}

static int subframe(struct bits *b, int32_t *s, int bs, int bps)
{
	int type, wasted = 0, i, j;

	if (br_get(b, 1))
		return -1;
	type = (int) br_get(b, 6);
	if (br_get(b, 1)) {
		wasted = (int) br_unary(b) + 1;
		bps -= wasted;
		if (bps <= 0)
			return -1;
	}
	if (type == 0) {
		int32_t v = br_sget(b, bps);
		for (i = 0; i < bs; i++)
			s[i] = v;
	} else if (type == 1) {
		for (i = 0; i < bs; i++)
			s[i] = br_sget(b, bps);
	} else if (type >= 8 && type <= 12) {
		int order = type - 8;
		if (order > bs)
			return -1;
		for (i = 0; i < order; i++)
			s[i] = br_sget(b, bps);
		if (residual(b, s, bs, order) < 0)
			return -1;
		switch (order) {
		case 1: for (i = 1; i < bs; i++) s[i] += s[i - 1]; break;
		case 2: for (i = 2; i < bs; i++) s[i] += 2 * s[i - 1] - s[i - 2]; break;
		case 3: for (i = 3; i < bs; i++) s[i] += 3 * s[i - 1] - 3 * s[i - 2] + s[i - 3]; break;
		case 4: for (i = 4; i < bs; i++) s[i] += 4 * s[i - 1] - 6 * s[i - 2] + 4 * s[i - 3] - s[i - 4]; break;
		}
	} else if (type >= 32) {
		int order = type - 31, prec, shift;
		int32_t coef[32];
		if (order > bs)
			return -1;
		for (i = 0; i < order; i++)
			s[i] = br_sget(b, bps);
		prec = (int) br_get(b, 4) + 1;
		if (prec == 16)
			return -1;
		shift = br_sget(b, 5);
		if (shift < 0)
			return -1;
		for (i = 0; i < order; i++)
			coef[i] = br_sget(b, prec);
		if (residual(b, s, bs, order) < 0)
			return -1;
		if (bps + prec + 5 <= 32) {
			for (i = order; i < bs; i++) {
				int32_t sum = 0;
				for (j = 0; j < order; j++)
					sum += coef[j] * s[i - 1 - j];
				s[i] += sum >> shift;
			}
		} else {
			for (i = order; i < bs; i++) {
				int64_t sum = 0;
				for (j = 0; j < order; j++)
					sum += (int64_t) coef[j] * s[i - 1 - j];
				s[i] += (int32_t) (sum >> shift);
			}
		}
	} else {
		return -1;
	}
	if (b->err)
		return -1;
	if (wasted)
		for (i = 0; i < bs; i++)
			s[i] = (int32_t) ((uint32_t) s[i] << wasted);
	return 0;
}

static int flac_decode_frame(struct flac *f, const uint8_t *p, size_t n)
{
	struct fhdr h;
	struct bits b;
	int bps, nch, c, i, bs;
	static const int bps_of[8] = { 0, 8, 12, 0, 16, 20, 24, 32 };
	uint16_t crc = 0;
	size_t q;

	if (parse_header(p, n, &h) != 1)
		return AV_ERR;
	bs = h.blocksize;
	bps = h.bps_code ? bps_of[h.bps_code] : f->bits;
	nch = h.chan < 8 ? h.chan + 1 : 2;
	if (bps <= 0 || bps > 32 || nch > 8)
		return AV_ERR;
	/* the frame's CRC-16 (its last two bytes) */
	for (q = 0; q < n; q++)
		crc = crc16_byte(crc, p[q]);
	if (crc != 0)
		return AV_ERR;
	if (bs > f->cap) {
		for (c = 0; c < 8; c++) {
			free(f->ch[c]);
			f->ch[c] = (int32_t *) malloc((size_t) bs * sizeof(int32_t));
			if (f->ch[c] == NULL) {
				f->cap = 0;
				return AV_ENOMEM;
			}
		}
		f->cap = bs;
	}
	memset(&b, 0, sizeof b);
	b.p = p + h.hlen;
	b.n = n - (size_t) h.hlen;
	for (c = 0; c < nch; c++) {
		int extra = (h.chan == 8 && c == 1) || (h.chan == 9 && c == 0) || (h.chan == 10 && c == 1);
		if (subframe(&b, f->ch[c], bs, bps + extra) < 0)
			return AV_ERR;
	}
	/* the stereo decorrelations */
	if (h.chan == 8) {		/* left / side */
		for (i = 0; i < bs; i++) f->ch[1][i] = f->ch[0][i] - f->ch[1][i];
	} else if (h.chan == 9) {	/* side / right */
		for (i = 0; i < bs; i++) f->ch[0][i] += f->ch[1][i];
	} else if (h.chan == 10) {	/* mid / side */
		for (i = 0; i < bs; i++) {
			int64_t mid = (int64_t) f->ch[0][i] * 2 | (f->ch[1][i] & 1), side = f->ch[1][i];
			f->ch[0][i] = (int32_t) ((mid + side) >> 1);
			f->ch[1][i] = (int32_t) ((mid - side) >> 1);
		}
	}
	/* to float, interleaved */
	if (bs * nch > f->out_cap) {
		free(f->out);
		f->out = (float *) malloc((size_t) bs * nch * sizeof(float));
		if (f->out == NULL) {
			f->out_cap = 0;
			return AV_ENOMEM;
		}
		f->out_cap = bs * nch;
	}
	{
		float scale = 1.0f / (float) (1u << (bps - 1 > 31 ? 31 : bps - 1));
		for (i = 0; i < bs; i++)
			for (c = 0; c < nch; c++)
				f->out[i * nch + c] = (float) f->ch[c][i] * scale;
	}
	memset(&f->frame, 0, sizeof f->frame);
	f->frame.kind = AV_AUDIO;
	f->frame.rate = h.rate ? h.rate : f->rate;
	if (h.rate_code >= 1 && h.rate_code <= 11) {
		static const int rates[12] = { 0, 88200, 176400, 192000, 8000, 16000, 22050, 24000, 32000, 44100, 48000, 96000 };
		f->frame.rate = rates[h.rate_code];
	}
	f->frame.channels = nch;
	f->frame.samples = bs;
	f->frame.pcm = f->out;
	f->have = 1;
	return AV_OK;
}

static int flac_send(void *c, const struct av_packet *p)
{
	struct flac *f = (struct flac *) c;
	int r;

	if (p == NULL)
		return AV_OK;
	r = flac_decode_frame(f, p->data, p->size);
	if (r == AV_OK) {
		f->frame.pts = p->pts;
		f->frame.dur = p->dur ? p->dur : (av_us) f->frame.samples * AV_US / f->frame.rate;
	}
	return r;
}

static int flac_receive(void *c, struct av_frame *fr)
{
	struct flac *f = (struct flac *) c;

	if (!f->have)
		return AV_AGAIN;
	f->have = 0;
	*fr = f->frame;
	return AV_OK;
}

static void flac_flush(void *c) { ((struct flac *) c)->have = 0; }

const struct av_codec_impl av_flac_codec = {
	AV_C_FLAC, AV_AUDIO, "flac", "built in", 0, 0, 0,
	flac_open, flac_send, flac_receive, flac_flush, flac_close, NULL
};
