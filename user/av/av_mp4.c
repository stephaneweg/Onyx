/*
 * user/av/av_mp4.c -- ISO BMFF (MP4, M4A, QuickTime's subset) as a push parser: plain files
 * (moov's sample tables, the moov before or after the mdat) and fragmented ones (moof + mdat:
 * MSE's byte stream, DASH / CMAF segments).
 *
 * Top-level boxes are walked at m->p: moov and moof are read whole; an mdat is entered only to
 * take the samples the tables place in it, else skipped. Plain files: once the moov is known,
 * the samples come out in file order (the tracks' cursors merged by offset), wanted where they
 * are (av_demux_want: an mdat before the moov is jumped over, then come back to). Fragments: a
 * moof's runs (trun) give the next samples' offsets, sizes, durations, composition offsets and
 * flags (tfhd's / trex's defaults), their decode times from the tfdt.
 * Times: the track's timescale -> microseconds, the first edit's media time taken off.
 */
#include <stdio.h>
#include "av_int.h"

#define MAX_BOX (64 << 20)	/* the largest moov / moof read whole */

struct mp4_trak {
	uint32_t id;			/* track_ID */
	uint32_t timescale;
	int64_t edit_shift;		/* in timescale units: the first edit's media_time */
	int ti;				/* index in d->tracks, -1 not played */
	/* trex defaults */
	uint32_t def_dur, def_size, def_flags;
	/* plain file: the sample table */
	uint32_t n;
	int64_t *off;
	uint32_t *size;
	int64_t *dts;
	int32_t *cto;
	uint8_t *key;
	uint32_t cur;			/* the next sample to hand out */
	int64_t frag_dts;		/* fragments: the next decode time if no tfdt */
};

struct mp4_sample { int ti; int64_t off; uint32_t size; av_us pts, dts, dur; int key; };

struct sidx_ref { int64_t off; av_us t; };

struct mp4 {
	int64_t p;			/* the next top-level box */
	int have_moov, fragmented;
	struct mp4_trak trak[AV_MAX_TRACKS];
	int ntrak;
	uint32_t mvhd_timescale;
	/* a plain file's samples: on (the tables' cursors), the end of the mdat they are in */
	int table_mode;
	/* the current fragment's samples, in offset order */
	struct mp4_sample *fs;
	int nfs, capfs, ifs;
	int64_t mdat_end;		/* the mdat being read (fragments), 0 none */
	struct sidx_ref *sidx;
	int nsidx;
};

/* ---- boxes in memory -------------------------------------------------------------------- */

/* a child box at p[i]: its type, its payload [*ps, *ps + *pn); -> the next offset, -1 bad */
static int64_t box_at(const uint8_t *p, int64_t n, int64_t i, uint32_t *type, int64_t *ps, int64_t *pn)
{
	uint64_t size;
	int64_t h = 8;

	if (n - i < 8)
		return -1;
	size = av_rb32(p + i);
	*type = av_rb32(p + i + 4);
	if (size == 1) {
		if (n - i < 16)
			return -1;
		size = av_rb64(p + i + 8);
		h = 16;
	} else if (size == 0) {
		size = (uint64_t) (n - i);
	}
	if (size < (uint64_t) h || (int64_t) size > n - i)
		return -1;
	*ps = i + h;
	*pn = (int64_t) size - h;
	return i + (int64_t) size;
}

#define FOURCC(a, b, c, d) ((uint32_t) (a) << 24 | (uint32_t) (b) << 16 | (uint32_t) (c) << 8 | (uint32_t) (d))

/* the first child of a type in a payload; NULL none */
static const uint8_t *find_box(const uint8_t *p, int64_t n, uint32_t want, int64_t *len)
{
	int64_t i = 0, ps, pn;
	uint32_t type;

	while (i < n) {
		int64_t next = box_at(p, n, i, &type, &ps, &pn);
		if (next < 0)
			return NULL;
		if (type == want) {
			*len = pn;
			return p + ps;
		}
		i = next;
	}
	return NULL;
}

static void set_extra(struct av_track *t, const uint8_t *p, int64_t n)
{
	free(t->extra);
	t->extra = NULL;
	t->extra_len = 0;
	if (n <= 0 || n > (1 << 20))
		return;
	t->extra = (uint8_t *) malloc((size_t) n);
	if (t->extra != NULL) {
		memcpy(t->extra, p, (size_t) n);
		t->extra_len = (size_t) n;
	}
}

/* an MPEG-4 descriptor's length (1..4 bytes of 7 bits) */
static int desc_len(const uint8_t *p, int64_t n, int64_t *i, uint32_t *len)
{
	int k;

	*len = 0;
	for (k = 0; k < 4; k++) {
		if (*i >= n)
			return -1;
		*len = *len << 7 | (p[*i] & 0x7F);
		if (!(p[(*i)++] & 0x80))
			return 0;
	}
	return 0;
}

/* esds: the object type and the decoder specific info (AudioSpecificConfig) */
static void parse_esds(struct av_track *t, const uint8_t *p, int64_t n)
{
	int64_t i = 4;		/* version + flags */
	uint32_t len;

	while (i < n) {
		uint8_t tag = p[i++];
		if (desc_len(p, n, &i, &len) < 0)
			return;
		if (tag == 3) {			/* ES_Descriptor */
			uint8_t flags;
			if (i + 3 > n)
				return;
			flags = p[i + 2];
			i += 3;
			if (flags & 0x80) i += 2;
			if (flags & 0x40) { if (i >= n) return; i += 1 + p[i]; }
			if (flags & 0x20) i += 2;
			continue;		/* its children follow */
		}
		if (tag == 4) {			/* DecoderConfigDescriptor */
			uint8_t oti;
			if (i + 13 > n)
				return;
			oti = p[i];
			if (oti == 0x40 || oti == 0x66 || oti == 0x67 || oti == 0x68)
				t->codec = AV_C_AAC;
			else if (oti == 0x69 || oti == 0x6B)
				t->codec = AV_C_MP3;
			else if (oti == 0xAD)
				t->codec = AV_C_OPUS;
			else {
				t->codec = AV_C_NONE;
				av__ff_identify(t, AV_FMT_MP4, 0, oti);	/* (MPEG-4 Part 2, AC-3...: FFmpeg's, when built in) */
			}
			i += 13;
			continue;		/* its DecoderSpecificInfo follows */
		}
		if (tag == 5) {			/* DecoderSpecificInfo */
			if (i + len <= n)
				set_extra(t, p + i, len);
			return;
		}
		i += len;
	}
}

/* a sample entry (stsd's first) into the track */
static void parse_entry(struct av_track *t, uint32_t type, const uint8_t *p, int64_t n)
{
	int64_t clen;
	const uint8_t *c;
	int video = 0, audio = 0;
	int64_t children = 0;

	snprintf(t->codec_id, sizeof t->codec_id, "%c%c%c%c", type >> 24, (type >> 16) & 255,
			(type >> 8) & 255, type & 255);
	switch (type) {
	case FOURCC('a','v','c','1'): case FOURCC('a','v','c','3'): t->codec = AV_C_H264; video = 1; break;
	case FOURCC('h','v','c','1'): case FOURCC('h','e','v','1'): t->codec = AV_C_HEVC; video = 1; break;
	case FOURCC('v','p','0','9'): t->codec = AV_C_VP9; video = 1; break;
	case FOURCC('v','p','0','8'): t->codec = AV_C_VP8; video = 1; break;
	case FOURCC('a','v','0','1'): t->codec = AV_C_AV1; video = 1; break;
	case FOURCC('j','p','e','g'): case FOURCC('m','j','p','a'): t->codec = AV_C_MJPEG; video = 1; break;
	case FOURCC('i','4','2','0'): case FOURCC('I','4','2','0'): t->codec = AV_C_I420; video = 1; break;
	case FOURCC('e','n','c','v'): t->encrypted = 1; video = 1; break;
	case FOURCC('m','p','4','a'): audio = 1; break;
	case FOURCC('O','p','u','s'): t->codec = AV_C_OPUS; audio = 1; break;
	case FOURCC('f','L','a','C'): t->codec = AV_C_FLAC; audio = 1; break;
	case FOURCC('s','o','w','t'): t->codec = AV_C_PCM_S16LE; audio = 1; break;
	case FOURCC('t','w','o','s'): t->codec = AV_C_PCM_S16BE; audio = 1; break;
	case FOURCC('r','a','w',' '): t->codec = AV_C_PCM_U8; audio = 1; break;
	case FOURCC('u','l','a','w'): t->codec = AV_C_ULAW; audio = 1; break;
	case FOURCC('a','l','a','w'): t->codec = AV_C_ALAW; audio = 1; break;
	case FOURCC('i','p','c','m'): case FOURCC('f','p','c','m'): audio = 1; break;
	case FOURCC('.','m','p','3'): t->codec = AV_C_MP3; audio = 1; break;
	case FOURCC('e','n','c','a'): t->encrypted = 1; audio = 1; break;
	/* (decoded by FFmpeg, when built in: av__ff_identify below) */
	case FOURCC('m','p','4','v'): case FOURCC('s','2','6','3'): case FOURCC('h','2','6','3'): case FOURCC('m','j','p','b'):
	case FOURCC('d','v','h','1'): case FOURCC('d','v','h','e'): case FOURCC('a','p','c','n'): case FOURCC('a','p','c','h'):
	case FOURCC('a','p','c','s'): case FOURCC('a','p','c','o'): case FOURCC('a','p','4','h'): case FOURCC('m','x','5','p'):
	case FOURCC('x','d','v','5'): case FOURCC('d','v','c','p'): case FOURCC('d','v','c',' '): case FOURCC('S','V','Q','3'):
		t->codec = AV_C_NONE; video = 1; break;
	case FOURCC('a','c','-','3'): case FOURCC('e','c','-','3'): case FOURCC('a','l','a','c'): case FOURCC('d','t','s','c'):
	case FOURCC('d','t','s','h'): case FOURCC('d','t','s','l'): case FOURCC('s','a','m','r'): case FOURCC('s','a','w','b'):
	case FOURCC('l','p','c','m'): case FOURCC('i','n','2','4'): case FOURCC('f','l','3','2'): case FOURCC('i','m','a','4'):
	case FOURCC('Q','c','l','p'): case FOURCC('m','p','3',' '):
		t->codec = AV_C_NONE; audio = 1; break;
	default:
		t->codec = AV_C_NONE;
		break;
	}
	if (video) {
		t->kind = AV_VIDEO;
		if (n < 78)
			return;
		t->width = (int) av_rb16(p + 24);
		t->height = (int) av_rb16(p + 26);
		children = 78;
	} else if (audio) {
		int version;
		t->kind = AV_AUDIO;
		if (n < 28)
			return;
		version = (int) av_rb16(p + 8);
		t->channels = (int) av_rb16(p + 16);
		t->bits = (int) av_rb16(p + 18);
		t->rate = (int) (av_rb32(p + 24) >> 16);
		children = 28;
		if (version == 1)
			children += 16;
		else if (version == 2) {
			/* QuickTime v2: the rate a double, the channels a u32 */
			if (n < 64)
				return;
			{
				union { uint64_t u; double f; } u;
				u.u = av_rb64(p + 32);
				t->rate = (int) (u.f + 0.5);
			}
			t->channels = (int) av_rb32(p + 40);
			t->bits = (int) av_rb32(p + 48);
			children = 64;
		}
	} else {
		return;
	}
	if (children > n)
		return;
	p += children;
	n -= children;
	if (t->encrypted) {
		/* the original format (sinf/frma) for the messages; not played */
		const uint8_t *s = find_box(p, n, FOURCC('s','i','n','f'), &clen);
		if (s != NULL && (c = find_box(s, clen, FOURCC('f','r','m','a'), &clen)) != NULL && clen >= 4)
			snprintf(t->codec_id, sizeof t->codec_id, "%c%c%c%c", c[0], c[1], c[2], c[3]);
		return;
	}
	switch (t->codec) {
	case AV_C_H264:
		if ((c = find_box(p, n, FOURCC('a','v','c','C'), &clen)) != NULL) set_extra(t, c, clen);
		break;
	case AV_C_HEVC:
		if ((c = find_box(p, n, FOURCC('h','v','c','C'), &clen)) != NULL) set_extra(t, c, clen);
		break;
	case AV_C_VP9: case AV_C_VP8:
		if ((c = find_box(p, n, FOURCC('v','p','c','C'), &clen)) != NULL) set_extra(t, c, clen);
		break;
	case AV_C_AV1:
		if ((c = find_box(p, n, FOURCC('a','v','1','C'), &clen)) != NULL) set_extra(t, c, clen);
		break;
	case AV_C_OPUS:
		if ((c = find_box(p, n, FOURCC('d','O','p','s'), &clen)) != NULL) {
			set_extra(t, c, clen);
			if (clen >= 4)
				t->delay = (av_us) av_rb16(c + 2) * AV_US / 48000;
			t->rate = 48000;
		}
		t->seek_preroll = 80000;
		break;
	case AV_C_FLAC:
		/* dfLa: version / flags, then FLAC metadata blocks (STREAMINFO first) */
		if ((c = find_box(p, n, FOURCC('d','f','L','a'), &clen)) != NULL && clen > 4)
			set_extra(t, c + 4, clen - 4);
		break;
	default:
		break;
	}
	if (type == FOURCC('m','p','4','a') || type == FOURCC('m','p','4','v')) {
		if ((c = find_box(p, n, FOURCC('e','s','d','s'), &clen)) != NULL)
			parse_esds(t, c, clen);
		else
			t->codec = AV_C_NONE;
	} else if (t->codec == AV_C_NONE && !t->encrypted) {
		/* the codec's setup, the box after the sample entry's fields (alac, dac3, dec3, glbl...) */
		const uint8_t *b = NULL;
		if (type == FOURCC('a','l','a','c'))
			b = find_box(p, n, FOURCC('a','l','a','c'), &clen);
		else if (type == FOURCC('a','c','-','3'))
			b = find_box(p, n, FOURCC('d','a','c','3'), &clen);
		else if (type == FOURCC('e','c','-','3'))
			b = find_box(p, n, FOURCC('d','e','c','3'), &clen);
		else
			b = find_box(p, n, FOURCC('g','l','b','l'), &clen);
		if (b != NULL && clen > 0)
			set_extra(t, type == FOURCC('a','l','a','c') ? b - 8 : b, type == FOURCC('a','l','a','c') ? clen + 8 : clen);
		av__ff_identify(t, AV_FMT_MP4, type, 0);
	} else if (type == FOURCC('i','p','c','m') || type == FOURCC('f','p','c','m')) {
		/* pcmC: version / flags, format_flags (bit 0: little endian), sample size */
		if ((c = find_box(p, n, FOURCC('p','c','m','C'), &clen)) != NULL && clen >= 6) {
			int le = c[4] & 1, bits = c[5];
			if (type == FOURCC('f','p','c','m'))
				t->codec = le && bits == 32 ? AV_C_PCM_F32LE : le && bits == 64 ? AV_C_PCM_F64LE : AV_C_NONE;
			else if (bits == 16)
				t->codec = le ? AV_C_PCM_S16LE : AV_C_PCM_S16BE;
			else if (bits == 24)
				t->codec = le ? AV_C_PCM_S24LE : AV_C_PCM_S24BE;
			else if (bits == 32 && le)
				t->codec = AV_C_PCM_S32LE;
			else
				t->codec = AV_C_NONE;
			t->bits = bits;
		}
	}
}

/* ---- the moov --------------------------------------------------------------------------- */

static void free_tables(struct mp4_trak *k)
{
	free(k->off); free(k->size); free(k->dts); free(k->cto); free(k->key);
	k->off = NULL; k->size = NULL; k->dts = NULL; k->cto = NULL; k->key = NULL;
	k->n = k->cur = 0;
}

/* the sample table of a stbl (plain files) */
static void build_tables(struct mp4_trak *k, const uint8_t *stbl, int64_t n)
{
	const uint8_t *stts, *ctts, *stss, *stsz, *stz2, *stsc, *stco, *co64;
	int64_t l_stts = 0, l_ctts = 0, l_stss = 0, l_stsz = 0, l_stz2 = 0, l_stsc = 0, l_stco = 0, l_co64 = 0;
	uint32_t count = 0, i, j;
	uint32_t const_size = 0;
	int fsize = 0;

	stts = find_box(stbl, n, FOURCC('s','t','t','s'), &l_stts);
	ctts = find_box(stbl, n, FOURCC('c','t','t','s'), &l_ctts);
	stss = find_box(stbl, n, FOURCC('s','t','s','s'), &l_stss);
	stsz = find_box(stbl, n, FOURCC('s','t','s','z'), &l_stsz);
	stz2 = find_box(stbl, n, FOURCC('s','t','z','2'), &l_stz2);
	stsc = find_box(stbl, n, FOURCC('s','t','s','c'), &l_stsc);
	stco = find_box(stbl, n, FOURCC('s','t','c','o'), &l_stco);
	co64 = find_box(stbl, n, FOURCC('c','o','6','4'), &l_co64);
	if (stsz != NULL && l_stsz >= 12) {
		const_size = av_rb32(stsz + 4);
		count = av_rb32(stsz + 8);
		if (const_size == 0 && (int64_t) count * 4 + 12 > l_stsz)
			count = (uint32_t) ((l_stsz - 12) / 4);
	} else if (stz2 != NULL && l_stz2 >= 12) {
		fsize = stz2[7];
		count = av_rb32(stz2 + 8);
		if (fsize != 4 && fsize != 8 && fsize != 16)
			count = 0;
		else if ((int64_t) count * fsize / 8 + 12 > l_stz2)
			count = 0;
	}
	if (count == 0 || count > 20000000 || stts == NULL || stsc == NULL || (stco == NULL && co64 == NULL))
		return;
	k->off = (int64_t *) malloc(count * sizeof *k->off);
	k->size = (uint32_t *) malloc(count * sizeof *k->size);
	k->dts = (int64_t *) malloc(count * sizeof *k->dts);
	k->cto = (int32_t *) calloc(count, sizeof *k->cto);
	k->key = (uint8_t *) malloc(count);
	if (!k->off || !k->size || !k->dts || !k->cto || !k->key) {
		free_tables(k);
		return;
	}
	k->n = count;
	/* sizes */
	for (i = 0; i < count; i++) {
		if (stsz != NULL)
			k->size[i] = const_size ? const_size : av_rb32(stsz + 12 + 4 * i);
		else if (fsize == 16)
			k->size[i] = av_rb16(stz2 + 12 + 2 * i);
		else if (fsize == 8)
			k->size[i] = stz2[12 + i];
		else
			k->size[i] = (stz2[12 + i / 2] >> (i & 1 ? 0 : 4)) & 15;
	}
	/* decode times */
	{
		uint32_t entries = l_stts >= 8 ? av_rb32(stts + 4) : 0, s = 0;
		int64_t t = 0;
		for (j = 0; j < entries && 8 + 8 * (int64_t) j + 8 <= l_stts && s < count; j++) {
			uint32_t c = av_rb32(stts + 8 + 8 * j), delta = av_rb32(stts + 12 + 8 * j);
			for (i = 0; i < c && s < count; i++, s++) {
				k->dts[s] = t;
				t += delta;
			}
		}
		for (; s < count; s++)
			k->dts[s] = t;
	}
	/* composition offsets */
	if (ctts != NULL && l_ctts >= 8) {
		uint32_t entries = av_rb32(ctts + 4), s = 0;
		for (j = 0; j < entries && 8 + 8 * (int64_t) j + 8 <= l_ctts && s < count; j++) {
			uint32_t c = av_rb32(ctts + 8 + 8 * j);
			int32_t o = (int32_t) av_rb32(ctts + 12 + 8 * j);
			for (i = 0; i < c && s < count; i++, s++)
				k->cto[s] = o;
		}
	}
	/* sync samples */
	if (stss != NULL && l_stss >= 8) {
		uint32_t entries = av_rb32(stss + 4);
		memset(k->key, 0, count);
		for (j = 0; j < entries && 8 + 4 * (int64_t) j + 4 <= l_stss; j++) {
			uint32_t s = av_rb32(stss + 8 + 4 * j);
			if (s >= 1 && s <= count)
				k->key[s - 1] = 1;
		}
	} else {
		memset(k->key, 1, count);
	}
	/* offsets: chunks and the samples in them */
	{
		uint32_t nchunks = stco != NULL ? (l_stco >= 8 ? av_rb32(stco + 4) : 0) : (l_co64 >= 8 ? av_rb32(co64 + 4) : 0);
		uint32_t entries = l_stsc >= 8 ? av_rb32(stsc + 4) : 0, s = 0, chunk;

		if (stco != NULL && (int64_t) nchunks * 4 + 8 > l_stco)
			nchunks = (uint32_t) ((l_stco - 8) / 4);
		if (co64 != NULL && stco == NULL && (int64_t) nchunks * 8 + 8 > l_co64)
			nchunks = (uint32_t) ((l_co64 - 8) / 8);
		for (j = 0; j < entries && 8 + 12 * (int64_t) j + 12 <= l_stsc; j++) {
			uint32_t first = av_rb32(stsc + 8 + 12 * j), per = av_rb32(stsc + 12 + 12 * j);
			uint32_t last = j + 1 < entries && 8 + 12 * (int64_t) (j + 1) + 12 <= l_stsc ?
				av_rb32(stsc + 8 + 12 * (j + 1)) : nchunks + 1;
			for (chunk = first; chunk < last && chunk <= nchunks && s < count; chunk++) {
				int64_t o = stco != NULL ? av_rb32(stco + 8 + 4 * (chunk - 1)) :
					(int64_t) av_rb64(co64 + 8 + 8 * (chunk - 1));
				for (i = 0; i < per && s < count; i++, s++) {
					k->off[s] = o;
					o += k->size[s];
				}
			}
		}
		if (s < count)
			k->n = s;	/* (the table ends early: what is placed) */
	}
}

static void parse_trak(struct av_demux *d, struct mp4 *m, const uint8_t *p, int64_t n)
{
	const uint8_t *tkhd, *mdia, *mdhd, *hdlr, *minf, *stbl, *stsd, *edts;
	int64_t l_tkhd = 0, l_mdia = 0, l_mdhd = 0, l_hdlr = 0, l_minf = 0, l_stbl = 0, l_stsd = 0, l_edts = 0;
	struct mp4_trak *k;
	struct av_track *t;
	uint32_t handler;

	if (m->ntrak >= AV_MAX_TRACKS)
		return;
	k = &m->trak[m->ntrak];
	memset(k, 0, sizeof *k);
	k->ti = -1;
	tkhd = find_box(p, n, FOURCC('t','k','h','d'), &l_tkhd);
	mdia = find_box(p, n, FOURCC('m','d','i','a'), &l_mdia);
	if (tkhd == NULL || mdia == NULL || l_tkhd < 20)
		return;
	k->id = tkhd[0] == 1 ? av_rb32(tkhd + 20) : av_rb32(tkhd + 12);
	mdhd = find_box(mdia, l_mdia, FOURCC('m','d','h','d'), &l_mdhd);
	hdlr = find_box(mdia, l_mdia, FOURCC('h','d','l','r'), &l_hdlr);
	minf = find_box(mdia, l_mdia, FOURCC('m','i','n','f'), &l_minf);
	if (mdhd == NULL || hdlr == NULL || minf == NULL || l_hdlr < 12 || l_mdhd < 20)
		return;
	if (mdhd[0] == 1) {
		if (l_mdhd < 32)
			return;
		k->timescale = av_rb32(mdhd + 20);
	} else {
		k->timescale = av_rb32(mdhd + 12);
	}
	if (k->timescale == 0)
		k->timescale = 1000;
	handler = av_rb32(hdlr + 8);
	stbl = find_box(minf, l_minf, FOURCC('s','t','b','l'), &l_stbl);
	if (stbl == NULL)
		return;
	stsd = find_box(stbl, l_stbl, FOURCC('s','t','s','d'), &l_stsd);
	edts = find_box(p, n, FOURCC('e','d','t','s'), &l_edts);
	if (edts != NULL) {
		int64_t l_elst;
		const uint8_t *elst = find_box(edts, l_edts, FOURCC('e','l','s','t'), &l_elst);
		if (elst != NULL && l_elst >= 8 && av_rb32(elst + 4) >= 1) {
			/* the first edit (an empty edit -- media_time -1 -- then the next) */
			int v1 = elst[0] == 1, e, cnt = (int) av_rb32(elst + 4);
			for (e = 0; e < cnt && e < 4; e++) {
				int64_t at = 8 + (int64_t) e * (v1 ? 20 : 12);
				int64_t mt;
				if (at + (v1 ? 20 : 12) > l_elst)
					break;
				mt = v1 ? (int64_t) av_rb64(elst + at + 8) : (int32_t) av_rb32(elst + at + 4);
				if (mt >= 0) {
					k->edit_shift = mt;
					break;
				}
			}
		}
	}
	m->ntrak++;
	if ((handler != FOURCC('v','i','d','e') && handler != FOURCC('s','o','u','n')) || stsd == NULL ||
	    l_stsd < 16)
		return;
	t = av__new_track(d);
	if (t == NULL)
		return;
	k->ti = d->ntracks - 1;
	t->number = (int) k->id;
	{
		int64_t ps, pn;
		uint32_t type;
		if (box_at(stsd + 8, l_stsd - 8, 0, &type, &ps, &pn) >= 0)
			parse_entry(t, type, stsd + 8 + ps, pn);
	}
	if (handler == FOURCC('v','i','d','e'))
		t->kind = AV_VIDEO;
	else
		t->kind = AV_AUDIO;
	/* tkhd's display size (16.16) */
	{
		int64_t wo = tkhd[0] == 1 ? 88 : 76;
		if (l_tkhd >= wo + 8 && t->kind == AV_VIDEO) {
			t->dwidth = (int) (av_rb32(tkhd + wo) >> 16);
			t->dheight = (int) (av_rb32(tkhd + wo + 4) >> 16);
		}
	}
	if (t->kind == AV_AUDIO && t->codec == AV_C_OPUS)
		t->rate = 48000;
	build_tables(k, stbl, l_stbl);
	if (k->n > 0) {
		t->duration = (av_us) ((k->dts[k->n - 1] - k->dts[0]) * AV_US / k->timescale);
		if (t->kind == AV_VIDEO && k->n > 1 && t->duration > 0)
			t->fps = (double) (k->n - 1) * AV_US / (double) t->duration;
	}
	av__codec_string(t);
}

static void parse_moov(struct av_demux *d, struct mp4 *m, const uint8_t *p, int64_t n)
{
	int64_t i = 0, ps, pn, next;
	uint32_t type;
	int k;

	for (k = 0; k < m->ntrak; k++)
		free_tables(&m->trak[k]);
	m->ntrak = 0;
	av__clear_tracks(d);
	m->fragmented = 0;
	while (i < n && (next = box_at(p, n, i, &type, &ps, &pn)) >= 0) {
		if (type == FOURCC('m','v','h','d') && pn >= 20) {
			uint64_t dur;
			if (p[ps] == 1 && pn >= 32) {
				m->mvhd_timescale = av_rb32(p + ps + 20);
				dur = av_rb64(p + ps + 24);
			} else {
				m->mvhd_timescale = av_rb32(p + ps + 12);
				dur = av_rb32(p + ps + 16);
			}
			if (m->mvhd_timescale && dur && dur != 0xFFFFFFFFu && dur != ~0ULL)
				d->duration = (av_us) (dur * AV_US / m->mvhd_timescale);
		} else if (type == FOURCC('t','r','a','k')) {
			parse_trak(d, m, p + ps, pn);
		} else if (type == FOURCC('m','v','e','x')) {
			int64_t j = 0, cs, cn, cnext;
			uint32_t ct;
			m->fragmented = 1;
			while (j < pn && (cnext = box_at(p + ps, pn, j, &ct, &cs, &cn)) >= 0) {
				const uint8_t *c = p + ps + cs;
				if (ct == FOURCC('t','r','e','x') && cn >= 24) {
					uint32_t id = av_rb32(c + 4);
					for (k = 0; k < m->ntrak; k++)
						if (m->trak[k].id == id) {
							m->trak[k].def_dur = av_rb32(c + 12);
							m->trak[k].def_size = av_rb32(c + 16);
							m->trak[k].def_flags = av_rb32(c + 20);
						}
				} else if (ct == FOURCC('m','e','h','d') && cn >= 8 && m->mvhd_timescale) {
					uint64_t dur = c[0] == 1 && cn >= 12 ? av_rb64(c + 4) : av_rb32(c + 4);
					d->duration = (av_us) (dur * AV_US / m->mvhd_timescale);
				}
				j = cnext;
			}
		}
		i = next;
	}
	m->have_moov = 1;
	m->table_mode = 0;
	for (k = 0; k < m->ntrak; k++)
		if (m->trak[k].ti >= 0 && m->trak[k].n > 0)
			m->table_mode = 1;
	d->init_new = 1;
}

/* ---- fragments -------------------------------------------------------------------------- */

static struct mp4_trak *trak_of(struct mp4 *m, uint32_t id)
{
	int k;

	for (k = 0; k < m->ntrak; k++)
		if (m->trak[k].id == id)
			return &m->trak[k];
	return NULL;
}

static int add_sample(struct mp4 *m, const struct mp4_sample *s)
{
	if (m->nfs == m->capfs) {
		int cap = m->capfs ? m->capfs * 2 : 256;
		struct mp4_sample *q = (struct mp4_sample *) realloc(m->fs, (size_t) cap * sizeof *q);
		if (q == NULL)
			return -1;
		m->fs = q;
		m->capfs = cap;
	}
	m->fs[m->nfs++] = *s;
	return 0;
}

static int cmp_off(const void *a, const void *b)
{
	const struct mp4_sample *x = (const struct mp4_sample *) a, *y = (const struct mp4_sample *) b;
	return x->off < y->off ? -1 : x->off > y->off;
}

/* a moof (at offset moof_start, payload p / n) -> its samples in m->fs */
static void parse_moof(struct mp4 *m, int64_t moof_start, const uint8_t *p, int64_t n)
{
	int64_t i = 0, ps, pn, next, prev_end = moof_start;
	uint32_t type;
	int first = 1;

	m->nfs = m->ifs = 0;
	while (i < n && (next = box_at(p, n, i, &type, &ps, &pn)) >= 0) {
		if (type == FOURCC('t','r','a','f')) {
			const uint8_t *tf = p + ps, *tfhd;
			int64_t tn = pn, l_tfhd, j = 0, cs, cn, cnext;
			uint32_t flags, ct;
			struct mp4_trak *k;
			int64_t base;
			uint32_t def_dur, def_size, def_flags;
			int64_t dts;

			tfhd = find_box(tf, tn, FOURCC('t','f','h','d'), &l_tfhd);
			if (tfhd == NULL || l_tfhd < 8 || (k = trak_of(m, av_rb32(tfhd + 4))) == NULL) {
				i = next;
				continue;
			}
			flags = av_rb24(tfhd + 1);
			j = 8;
			base = first || (flags & 0x020000) ? moof_start : prev_end;
			if (flags & 0x000001) { if (j + 8 <= l_tfhd) base = (int64_t) av_rb64(tfhd + j); j += 8; }
			if (flags & 0x000002) j += 4;
			def_dur = k->def_dur; def_size = k->def_size; def_flags = k->def_flags;
			if (flags & 0x000008) { if (j + 4 <= l_tfhd) def_dur = av_rb32(tfhd + j); j += 4; }
			if (flags & 0x000010) { if (j + 4 <= l_tfhd) def_size = av_rb32(tfhd + j); j += 4; }
			if (flags & 0x000020) { if (j + 4 <= l_tfhd) def_flags = av_rb32(tfhd + j); j += 4; }
			dts = k->frag_dts;
			j = 0;
			while (j < tn && (cnext = box_at(tf, tn, j, &ct, &cs, &cn)) >= 0) {
				const uint8_t *c = tf + cs;
				if (ct == FOURCC('t','f','d','t') && cn >= 8)
					dts = c[0] == 1 && cn >= 12 ? (int64_t) av_rb64(c + 4) : (int64_t) av_rb32(c + 4);
				j = cnext;
			}
			j = 0;
			while (j < tn && (cnext = box_at(tf, tn, j, &ct, &cs, &cn)) >= 0) {
				const uint8_t *c = tf + cs;
				if (ct == FOURCC('t','r','u','n') && cn >= 8) {
					uint32_t tr = av_rb24(c + 1), count = av_rb32(c + 4), s;
					int v1 = c[0] == 1;
					int64_t q = 8, off = base;
					uint32_t first_flags = def_flags;
					int has_first = 0;
					if (tr & 0x001) { if (q + 4 <= cn) off = base + (int32_t) av_rb32(c + q); q += 4; }
					if (tr & 0x004) { if (q + 4 <= cn) { first_flags = av_rb32(c + q); has_first = 1; } q += 4; }
					for (s = 0; s < count; s++) {
						uint32_t dur = def_dur, size = def_size, sf = s == 0 && has_first ? first_flags : def_flags;
						int64_t cto = 0;
						struct mp4_sample sm;
						if (tr & 0x100) { if (q + 4 > cn) break; dur = av_rb32(c + q); q += 4; }
						if (tr & 0x200) { if (q + 4 > cn) break; size = av_rb32(c + q); q += 4; }
						if (tr & 0x400) { if (q + 4 > cn) break; sf = av_rb32(c + q); if (s == 0 && has_first) sf = first_flags; q += 4; }
						if (tr & 0x800) { if (q + 4 > cn) break; cto = v1 ? (int32_t) av_rb32(c + q) : (int64_t) av_rb32(c + q); q += 4; }
						if (k->ti >= 0) {
							sm.ti = k->ti;
							sm.off = off;
							sm.size = size;
							sm.dts = (dts - k->edit_shift) * AV_US / k->timescale;
							sm.pts = (dts + cto - k->edit_shift) * AV_US / k->timescale;
							sm.dur = (av_us) dur * AV_US / k->timescale;
							/* sample_is_non_sync_sample (bit 16) clear: a sync sample */
							sm.key = !(sf & 0x00010000);
							add_sample(m, &sm);
						}
						off += size;
						dts += dur;
					}
					prev_end = off;
				}
				j = cnext;
			}
			k->frag_dts = dts;
			first = 0;
		}
		i = next;
	}
	if (m->nfs > 1)
		qsort(m->fs, (size_t) m->nfs, sizeof *m->fs, cmp_off);
}

/* ---- the reader ------------------------------------------------------------------------- */

static int emit(struct av_demux *d, const struct mp4_sample *s, struct av_packet *pkt)
{
	pkt->track = s->ti;
	pkt->pts = s->pts;
	pkt->dts = s->dts;
	pkt->dur = s->dur;
	pkt->key = s->key;
	pkt->pos = s->off;
	pkt->size = s->size;
	pkt->data = av__copy(d, s->off, s->size);
	return pkt->data != NULL ? AV_OK : AV_ENOMEM;
}

/* a plain file's next sample: the tracks' cursors merged by offset */
static int table_next(struct mp4 *m, struct mp4_sample *s)
{
	int k, best = -1;
	int64_t bo = 0;

	for (k = 0; k < m->ntrak; k++) {
		struct mp4_trak *t = &m->trak[k];
		if (t->ti < 0 || t->cur >= t->n)
			continue;
		if (best < 0 || t->off[t->cur] < bo) {
			best = k;
			bo = t->off[t->cur];
		}
	}
	if (best < 0)
		return 0;
	{
		struct mp4_trak *t = &m->trak[best];
		uint32_t i = t->cur;
		s->ti = t->ti;
		s->off = t->off[i];
		s->size = t->size[i];
		s->dts = (t->dts[i] - t->edit_shift) * AV_US / t->timescale;
		s->pts = (t->dts[i] + t->cto[i] - t->edit_shift) * AV_US / t->timescale;
		s->dur = i + 1 < t->n ? (t->dts[i + 1] - t->dts[i]) * AV_US / t->timescale : 0;
		s->key = t->key[i];
	}
	return 1;
}

static int mp4_read(struct av_demux *d, void *priv, struct av_packet *pkt)
{
	struct mp4 *m = (struct mp4 *) priv;

	for (;;) {
		const uint8_t *h;
		uint64_t size;
		uint32_t type;
		int64_t hl = 8, start;

		/* a fragment's samples */
		if (m->ifs < m->nfs) {
			struct mp4_sample *s = &m->fs[m->ifs];
			if (av__peek(d, s->off, s->size) == NULL) {
				if (s->off < d->base + (int64_t) d->off)
					m->ifs++;	/* (behind us: lost) */
				else if (d->ended)
					return AV_EOF;
				else
					return AV_AGAIN;
				continue;
			}
			m->ifs++;
			if (m->ifs == m->nfs && m->mdat_end > m->p)
				m->p = m->mdat_end;
			return emit(d, s, pkt);
		}
		/* a plain file's samples */
		if (m->table_mode) {
			struct mp4_sample s;
			int k;
			if (table_next(m, &s)) {
				if (av__peek(d, s.off, s.size) == NULL) {
					if (s.off < d->base + (int64_t) d->off || s.off > av__end(d))
						d->want = s.off;
					return AV_AGAIN;
				}
				for (k = 0; k < m->ntrak; k++)
					if (m->trak[k].ti == s.ti) {
						m->trak[k].cur++;
						break;
					}
				{
					int r = emit(d, &s, pkt);
					av__consume(d, s.off + s.size);
					return r;
				}
			}
			m->table_mode = 0;
			if (!m->fragmented)
				return AV_EOF;
			continue;
		}
		av__consume(d, m->p);
		h = av__peek(d, m->p, 8);
		if (h == NULL) {
			if (m->p < d->base + (int64_t) d->off || m->p > av__end(d))
				d->want = m->p;
			return AV_AGAIN;
		}
		size = av_rb32(h);
		type = av_rb32(h + 4);
		if (size == 1) {
			h = av__peek(d, m->p, 16);
			if (h == NULL)
				return AV_AGAIN;
			size = av_rb64(h + 8);
			hl = 16;
		} else if (size == 0) {
			size = 0;	/* to the end of the stream */
		}
		if (size != 0 && size < (uint64_t) hl)
			return AV_ERR;
		start = m->p;
		switch (type) {
		case FOURCC('m','o','o','v'):
		case FOURCC('m','o','o','f'):
		case FOURCC('s','i','d','x'): {
			const uint8_t *p;
			if (size == 0 || size > MAX_BOX)
				return AV_ERR;
			p = av__peek(d, start, (size_t) size);
			if (p == NULL)
				return AV_AGAIN;
			m->p = start + (int64_t) size;
			if (type == FOURCC('m','o','o','v')) {
				parse_moov(d, m, p + hl, (int64_t) size - hl);
				if (m->table_mode) {
					/* the samples may be before (an mdat skipped): from the first */
					continue;
				}
			} else if (type == FOURCC('m','o','o','f')) {
				if (!m->have_moov)
					return AV_ERR;	/* (MSE: a media segment before the init segment) */
				parse_moof(m, start, p + hl, (int64_t) size - hl);
				m->mdat_end = 0;
			} else if (m->sidx == NULL && size >= hl + 24) {
				/* sidx: the segments' offsets and times (a fragmented file's seeking) */
				const uint8_t *s = p + hl;
				int v1 = s[0] == 1;
				uint32_t ts = av_rb32(s + 8), cnt, r;
				int64_t q = v1 ? 28 : 20, off;
				av_us t;
				if (ts == 0 || (int64_t) size - hl < q + 4)
					continue;
				t = v1 ? (av_us) (av_rb64(s + 12) * AV_US / ts) : (av_us) ((uint64_t) av_rb32(s + 12) * AV_US / ts);
				off = start + (int64_t) size + (v1 ? (int64_t) av_rb64(s + 20) : (int64_t) av_rb32(s + 16));
				cnt = av_rb16(s + q + 2);
				q += 4;
				m->sidx = (struct sidx_ref *) malloc((cnt + 1) * sizeof *m->sidx);
				if (m->sidx == NULL)
					continue;
				for (r = 0; r < cnt && q + 12 <= (int64_t) size - hl; r++, q += 12) {
					m->sidx[r].off = off;
					m->sidx[r].t = t;
					off += av_rb32(s + q) & 0x7FFFFFFF;
					t += (av_us) ((uint64_t) av_rb32(s + q + 4) * AV_US / ts);
				}
				m->nsidx = (int) r;
			}
			continue;
		}
		case FOURCC('m','d','a','t'):
			if (m->nfs > 0 && m->ifs < m->nfs) {
				/* the fragment's samples are in it: taken from the window */
				m->mdat_end = size == 0 ? INT64_MAX : start + (int64_t) size;
				m->p = start + hl;
				continue;
			}
			/* else (a plain file's samples come through the tables; or no moof): skipped */
			if (size == 0) {
				if (!m->have_moov)
					return AV_EUNSUP;	/* (an mdat to the end and no moov before it) */
				return AV_EOF;
			}
			m->p = start + (int64_t) size;
			continue;
		default:
			/* ftyp, styp, free, skip, emsg, prft, uuid, meta...: skipped */
			if (size == 0)
				return AV_EOF;
			m->p = start + (int64_t) size;
			continue;
		}
	}
}

static void *mp4_create(struct av_demux *d)
{
	struct mp4 *m = (struct mp4 *) calloc(1, sizeof *m);

	strcpy(d->name, "mp4");
	return m;
}

static void mp4_destroy(void *priv)
{
	struct mp4 *m = (struct mp4 *) priv;
	int k;

	if (m == NULL)
		return;
	for (k = 0; k < m->ntrak; k++)
		free_tables(&m->trak[k]);
	free(m->fs);
	free(m->sidx);
	free(m);
}

static void mp4_restart(struct av_demux *d, void *priv, int64_t pos)
{
	struct mp4 *m = (struct mp4 *) priv;
	(void) d;
	/* a jump: the bytes there are a box (the caller's seek gave it), or the samples' */
	if (!m->table_mode)
		m->p = pos;
	m->nfs = m->ifs = 0;
}

static void mp4_reset(struct av_demux *d, void *priv)
{
	struct mp4 *m = (struct mp4 *) priv;

	m->p = av__end(d);
	m->nfs = m->ifs = 0;
}

static int64_t mp4_seek(struct av_demux *d, void *priv, av_us t, av_us *at)
{
	struct mp4 *m = (struct mp4 *) priv;
	int k, any = 0;
	int64_t lo = -1;
	av_us vt = AV_NOTIME;
	(void) d;

	if (!m->have_moov)
		return -1;
	if (!m->fragmented) {
		/* the video's sync sample at or before t, then every track from that time */
		for (k = 0; k < m->ntrak; k++) {
			struct mp4_trak *tk = &m->trak[k];
			int64_t want;
			uint32_t i, best = 0;
			if (tk->ti < 0 || tk->n == 0 || d->tracks[tk->ti].kind != AV_VIDEO)
				continue;
			want = t * tk->timescale / AV_US + tk->edit_shift;
			for (i = 0; i < tk->n; i++) {
				if (tk->dts[i] + tk->cto[i] > want)
					break;
				if (tk->key[i])
					best = i;
			}
			tk->cur = best;
			vt = (tk->dts[best] + tk->cto[best] - tk->edit_shift) * AV_US / tk->timescale;
			any = 1;
			break;
		}
		if (vt == AV_NOTIME)
			vt = t;
		for (k = 0; k < m->ntrak; k++) {
			struct mp4_trak *tk = &m->trak[k];
			int64_t want;
			uint32_t i;
			if (tk->ti < 0 || tk->n == 0)
				continue;
			if (d->tracks[tk->ti].kind != AV_VIDEO || !any) {
				want = vt * tk->timescale / AV_US + tk->edit_shift;
				for (i = 0; i + 1 < tk->n && tk->dts[i + 1] <= want; i++)
					;
				tk->cur = i;
			}
			if (tk->cur < tk->n && (lo < 0 || tk->off[tk->cur] < lo))
				lo = tk->off[tk->cur];
		}
		m->table_mode = 1;
		m->nfs = m->ifs = 0;
		if (at != NULL)
			*at = vt;
		return lo;
	}
	/* fragmented: the sidx's segment */
	if (m->nsidx > 0) {
		int r, best = 0;
		for (r = 0; r < m->nsidx; r++)
			if (m->sidx[r].t <= t)
				best = r;
		m->p = m->sidx[best].off;
		m->nfs = m->ifs = 0;
		if (at != NULL)
			*at = m->sidx[best].t;
		return m->p;
	}
	return -1;
}

const struct av_fmt_ops av_mp4_ops = {
	"mp4", mp4_create, mp4_destroy, mp4_read, mp4_restart, mp4_seek, mp4_reset
};
