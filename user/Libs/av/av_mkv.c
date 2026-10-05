/*
 * user/Libs/av/av_mkv.c -- Matroska / WebM (EBML) as a push parser.
 *
 * Elements are read one at a time from the window: the masters that hold the stream
 * (Segment, Cluster -- also of unknown size, as live streams have) are entered; the small
 * masters (EBML header, Info, Tracks, SeekHead, Cues, BlockGroup) are read whole; blocks
 * become packets (the lacings: Xiph, fixed, EBML); the rest (Tags, Attachments, Chapters,
 * Void...) is skipped without being kept. MSE's WebM byte stream is this: an initialization
 * segment (EBML header + Segment start + Info + Tracks) then media segments (Clusters), each
 * append continuing the element stream.
 *
 * Times: (Cluster Timecode + the block's) x TimecodeScale ns -> microseconds.
 * Seeking (a file): the Cues (read where the SeekHead says, or met on the way) give the
 * cluster at or before a time.
 */
#include <stdio.h>
#include "av_int.h"

/* element IDs */
#define ID_EBML 0x1A45DFA3
#define ID_DOCTYPE 0x4282
#define ID_SEGMENT 0x18538067
#define ID_SEEKHEAD 0x114D9B74
#define ID_SEEK 0x4DBB
#define ID_SEEKID 0x53AB
#define ID_SEEKPOS 0x53AC
#define ID_INFO 0x1549A966
#define ID_TCSCALE 0x2AD7B1
#define ID_DURATION 0x4489
#define ID_TRACKS 0x1654AE6B
#define ID_TRACKENTRY 0xAE
#define ID_TRACKNUMBER 0xD7
#define ID_TRACKTYPE 0x83
#define ID_FLAGDEFAULT 0x88
#define ID_CODECID 0x86
#define ID_CODECPRIVATE 0x63A2
#define ID_DEFAULTDUR 0x23E383
#define ID_CODECDELAY 0x56AA
#define ID_SEEKPREROLL 0x56BB
#define ID_VIDEO 0xE0
#define ID_PIXELW 0xB0
#define ID_PIXELH 0xBA
#define ID_DISPW 0x54B0
#define ID_DISPH 0x54BA
#define ID_COLOURSPACE 0x2EB524
#define ID_COLOUR 0x55B0
#define ID_MATRIXCOEF 0x55B1
#define ID_RANGE 0x55B9
#define ID_AUDIO 0xE1
#define ID_SAMPLINGFREQ 0xB5
#define ID_CHANNELS 0x9F
#define ID_BITDEPTH 0x6264
#define ID_CONTENTENCODINGS 0x6D80
#define ID_CONTENTENCODING 0x6240
#define ID_CONTENTENCRYPTION 0x5035
#define ID_CONTENTCOMPRESSION 0x5034
#define ID_CLUSTER 0x1F43B675
#define ID_TIMECODE 0xE7
#define ID_SIMPLEBLOCK 0xA3
#define ID_BLOCKGROUP 0xA0
#define ID_BLOCK 0xA1
#define ID_BLOCKDURATION 0x9B
#define ID_REFERENCEBLOCK 0xFB
#define ID_DISCARDPADDING 0x75A2
#define ID_CUES 0x1C53BB6B
#define ID_CUEPOINT 0xBB
#define ID_CUETIME 0xB3
#define ID_CUETRACKPOS 0xB7
#define ID_CUETRACK 0xF7
#define ID_CUECLUSTERPOS 0xF1
#define ID_VOID 0xEC
#define ID_CRC32 0xBF

#define UNKNOWN_SIZE (-1LL)
#define MAX_WHOLE (16 << 20)	/* the largest element read whole (a block, Cues) */

struct cue { av_us t; int64_t pos; };

/* the laced frames of a block, handed out one by one */
struct lace {
	int n, i;
	int64_t pos[64];
	uint32_t size[64];
	int track;
	av_us t, dur;
	int key;
	av_us discard;
};

struct mkv {
	int64_t p;			/* the next element's offset */
	int in_segment, in_cluster;
	int64_t seg_start, seg_end;	/* the Segment's data; seg_end UNKNOWN_SIZE */
	int64_t cluster_end;		/* UNKNOWN_SIZE: until the next cluster / top-level element */
	int64_t cluster_tc;
	uint64_t tcscale;		/* ns per timecode unit */
	int64_t cues_pos;		/* from the SeekHead: relative to seg_start, -1 */
	struct cue *cues;
	int ncues, capcues;
	int cues_read;
	struct lace lace;
	int doc_ok;
	int resync;			/* after a jump: look for a Cluster ID */
	av_us pending;			/* a seek waiting for the Cues (AV_NOTIME: none) */
};

/* ---- EBML numbers ------------------------------------------------------------------------ */

/* an element ID (1..4 bytes, its marker kept) -> length, 0 invalid, -1 more bytes needed */
static int read_id(const uint8_t *p, size_t n, uint32_t *id)
{
	int len;
	uint32_t v;
	int i;

	if (n < 1)
		return -1;
	if (p[0] & 0x80) len = 1;
	else if (p[0] & 0x40) len = 2;
	else if (p[0] & 0x20) len = 3;
	else if (p[0] & 0x10) len = 4;
	else return 0;
	if ((size_t) len > n)
		return -1;
	v = 0;
	for (i = 0; i < len; i++)
		v = v << 8 | p[i];
	*id = v;
	return len;
}

/* a size / vint (1..8 bytes, marker removed) -> length, 0 invalid, -1 more bytes; all ones = unknown */
static int read_vint(const uint8_t *p, size_t n, int64_t *val, int *unknown)
{
	int len = 1, i;
	uint64_t v, mask = 0x80;

	if (n < 1)
		return -1;
	while (len <= 8 && !(p[0] & mask)) {
		len++;
		mask >>= 1;
	}
	if (len > 8)
		return 0;
	if ((size_t) len > n)
		return -1;
	v = p[0] & (mask - 1);
	for (i = 1; i < len; i++)
		v = v << 8 | p[i];
	if (unknown != NULL)
		*unknown = v == ((1ULL << (7 * len)) - 1);
	*val = (int64_t) v;
	return len;
}

static uint64_t rd_uint(const uint8_t *p, int64_t n)
{
	uint64_t v = 0;
	int64_t i;

	for (i = 0; i < n && i < 8; i++)
		v = v << 8 | p[i];
	return v;
}

static double rd_float(const uint8_t *p, int64_t n)
{
	if (n == 4) {
		union { uint32_t u; float f; } u;
		u.u = av_rb32(p);
		return u.f;
	}
	if (n == 8) {
		union { uint64_t u; double f; } u;
		u.u = av_rb64(p);
		return u.f;
	}
	return 0;
}

/* walk the children of a master held in memory: cb (id, data, size) for each */
typedef void (*child_fn)(struct av_demux *d, struct mkv *m, void *ctx, uint32_t id,
		const uint8_t *p, int64_t n);
static int each_child(struct av_demux *d, struct mkv *m, void *ctx, const uint8_t *p, int64_t n,
		child_fn cb)
{
	int64_t i = 0;

	while (i < n) {
		uint32_t id;
		int64_t size;
		int a, b, unk;

		a = read_id(p + i, (size_t) (n - i), &id);
		if (a <= 0)
			return -1;
		b = read_vint(p + i + a, (size_t) (n - i - a), &size, &unk);
		if (b <= 0)
			return -1;
		i += a + b;
		if (unk || size > n - i)
			size = n - i;
		cb(d, m, ctx, id, p + i, size);
		i += size;
	}
	return 0;
}

/* ---- the headers ------------------------------------------------------------------------ */

static void ebml_child(struct av_demux *d, struct mkv *m, void *ctx, uint32_t id,
		const uint8_t *p, int64_t n)
{
	(void) ctx;
	if (id == ID_DOCTYPE) {
		size_t l = (size_t) (n < 11 ? n : 11);
		memcpy(d->name, p, l);
		d->name[l] = 0;
		m->doc_ok = 1;
	}
}

static void info_child(struct av_demux *d, struct mkv *m, void *ctx, uint32_t id,
		const uint8_t *p, int64_t n)
{
	double *dur = (double *) ctx;

	if (id == ID_TCSCALE)
		m->tcscale = rd_uint(p, n);
	else if (id == ID_DURATION)
		*dur = rd_float(p, n);
	(void) d;
}

struct track_ctx { struct av_track *t; int type; uint32_t colourspace; };

static void video_child(struct av_demux *d, struct mkv *m, void *ctx, uint32_t id,
		const uint8_t *p, int64_t n)
{
	struct track_ctx *tc = (struct track_ctx *) ctx;

	switch (id) {
	case ID_PIXELW: tc->t->width = (int) rd_uint(p, n); break;
	case ID_PIXELH: tc->t->height = (int) rd_uint(p, n); break;
	case ID_DISPW: tc->t->dwidth = (int) rd_uint(p, n); break;
	case ID_DISPH: tc->t->dheight = (int) rd_uint(p, n); break;
	case ID_COLOURSPACE: if (n == 4) tc->colourspace = av_rb32(p); break;
	}
}

static void audio_child(struct av_demux *d, struct mkv *m, void *ctx, uint32_t id,
		const uint8_t *p, int64_t n)
{
	struct track_ctx *tc = (struct track_ctx *) ctx;
	(void) d; (void) m;
	switch (id) {
	case ID_SAMPLINGFREQ: tc->t->rate = (int) (rd_float(p, n) + 0.5); break;
	case ID_CHANNELS: tc->t->channels = (int) rd_uint(p, n); break;
	case ID_BITDEPTH: tc->t->bits = (int) rd_uint(p, n); break;
	}
}

static void encoding_child(struct av_demux *d, struct mkv *m, void *ctx, uint32_t id,
		const uint8_t *p, int64_t n);
static void encodings_child(struct av_demux *d, struct mkv *m, void *ctx, uint32_t id,
		const uint8_t *p, int64_t n)
{
	if (id == ID_CONTENTENCODING)
		each_child(d, m, ctx, p, n, encoding_child);
}
static void encoding_child(struct av_demux *d, struct mkv *m, void *ctx, uint32_t id,
		const uint8_t *p, int64_t n)
{
	struct track_ctx *tc = (struct track_ctx *) ctx;
	(void) d; (void) m; (void) p; (void) n;
	/* encrypted (EME) or compressed blocks: not played */
	if (id == ID_CONTENTENCRYPTION || id == ID_CONTENTCOMPRESSION)
		tc->t->encrypted = 1;
}

static void track_child(struct av_demux *d, struct mkv *m, void *ctx, uint32_t id,
		const uint8_t *p, int64_t n)
{
	struct track_ctx *tc = (struct track_ctx *) ctx;
	struct av_track *t = tc->t;

	switch (id) {
	case ID_TRACKNUMBER: t->number = (int) rd_uint(p, n); break;
	case ID_TRACKTYPE: tc->type = (int) rd_uint(p, n); break;
	case ID_FLAGDEFAULT: t->lang_default |= rd_uint(p, n) ? 1 : 0; break;
	case ID_CODECID: {
		size_t l = (size_t) (n < (int64_t) sizeof t->codec_id - 1 ? n : (int64_t) sizeof t->codec_id - 1);
		memcpy(t->codec_id, p, l);
		t->codec_id[l] = 0;
		break;
	}
	case ID_CODECPRIVATE:
		free(t->extra);
		t->extra = (uint8_t *) malloc((size_t) n + 1);
		if (t->extra != NULL) {
			memcpy(t->extra, p, (size_t) n);
			t->extra_len = (size_t) n;
		}
		break;
	case ID_DEFAULTDUR: t->default_dur = (av_us) (rd_uint(p, n) / 1000); break;
	case ID_CODECDELAY: t->delay = (av_us) (rd_uint(p, n) / 1000); break;
	case ID_SEEKPREROLL: t->seek_preroll = (av_us) (rd_uint(p, n) / 1000); break;
	case ID_VIDEO: each_child(d, m, ctx, p, n, video_child); break;
	case ID_AUDIO: each_child(d, m, ctx, p, n, audio_child); break;
	case ID_CONTENTENCODINGS: each_child(d, m, ctx, p, n, encodings_child); break;
	}
}

/* the codec of a Matroska CodecID */
static void mkv_codec(struct av_track *t, uint32_t colourspace)
{
	const char *c = t->codec_id;

	if (!strcmp(c, "V_VP8")) t->codec = AV_C_VP8;
	else if (!strcmp(c, "V_VP9")) t->codec = AV_C_VP9;
	else if (!strcmp(c, "V_AV1")) t->codec = AV_C_AV1;
	else if (!strcmp(c, "V_MPEG4/ISO/AVC")) t->codec = AV_C_H264;
	else if (!strcmp(c, "V_MPEGH/ISO/HEVC")) t->codec = AV_C_HEVC;
	else if (!strcmp(c, "V_MJPEG")) t->codec = AV_C_MJPEG;
	else if (!strcmp(c, "V_UNCOMPRESSED") && (colourspace == 0x49343230 /* I420 */ ||
			colourspace == 0x49595556 /* IYUV */ || colourspace == 0))
		t->codec = AV_C_I420;
	else if (!strcmp(c, "A_OPUS")) t->codec = AV_C_OPUS;
	else if (!strcmp(c, "A_VORBIS")) t->codec = AV_C_VORBIS;
	else if (!strncmp(c, "A_AAC", 5)) t->codec = AV_C_AAC;
	else if (!strcmp(c, "A_MPEG/L3")) t->codec = AV_C_MP3;
	else if (!strcmp(c, "A_FLAC")) t->codec = AV_C_FLAC;
	else if (!strcmp(c, "A_PCM/INT/LIT"))
		t->codec = t->bits == 8 ? AV_C_PCM_U8 : t->bits == 24 ? AV_C_PCM_S24LE :
			t->bits == 32 ? AV_C_PCM_S32LE : AV_C_PCM_S16LE;
	else if (!strcmp(c, "A_PCM/INT/BIG"))
		t->codec = t->bits == 24 ? AV_C_PCM_S24BE : AV_C_PCM_S16BE;
	else if (!strcmp(c, "A_PCM/FLOAT/IEEE"))
		t->codec = t->bits == 64 ? AV_C_PCM_F64LE : AV_C_PCM_F32LE;
	else t->codec = AV_C_NONE;
}

static void tracks_child(struct av_demux *d, struct mkv *m, void *ctx, uint32_t id,
		const uint8_t *p, int64_t n)
{
	struct track_ctx tc;
	struct av_track *t;
	(void) ctx;

	if (id != ID_TRACKENTRY)
		return;
	t = av__new_track(d);
	if (t == NULL)
		return;
	memset(&tc, 0, sizeof tc);
	tc.t = t;
	each_child(d, m, &tc, p, n, track_child);
	if (tc.type == 1) {
		t->kind = AV_VIDEO;
		if (t->default_dur > 0)
			t->fps = 1e6 / (double) t->default_dur;
	} else if (tc.type == 2) {
		t->kind = AV_AUDIO;
		if (t->channels == 0)
			t->channels = 1;
		if (t->rate == 0)
			t->rate = 8000;
	} else {
		t->kind = 0;
	}
	mkv_codec(t, tc.colourspace);
	if (t->codec == AV_C_NONE && t->kind != 0)
		av__ff_identify(t, AV_FMT_MKV, 0, 0);	/* (FFmpeg's, when built in) */
	av__codec_string(t);
}

static void seek_child(struct av_demux *d, struct mkv *m, void *ctx, uint32_t id,
		const uint8_t *p, int64_t n)
{
	uint64_t *e = (uint64_t *) ctx;
	(void) d; (void) m;
	if (id == ID_SEEKID) e[0] = rd_uint(p, n);
	else if (id == ID_SEEKPOS) e[1] = rd_uint(p, n);
}

static void seekhead_child(struct av_demux *d, struct mkv *m, void *ctx, uint32_t id,
		const uint8_t *p, int64_t n)
{
	uint64_t e[2] = { 0, 0 };
	(void) ctx;
	if (id != ID_SEEK)
		return;
	each_child(d, m, e, p, n, seek_child);
	if (e[0] == ID_CUES && m->cues_pos < 0)
		m->cues_pos = (int64_t) e[1];
}

struct cue_ctx { av_us t; int64_t pos; };

static void cuetrack_child(struct av_demux *d, struct mkv *m, void *ctx, uint32_t id,
		const uint8_t *p, int64_t n)
{
	struct cue_ctx *c = (struct cue_ctx *) ctx;
	(void) d; (void) m;
	if (id == ID_CUECLUSTERPOS && c->pos < 0)
		c->pos = (int64_t) rd_uint(p, n);
}

static void cuepoint_child(struct av_demux *d, struct mkv *m, void *ctx, uint32_t id,
		const uint8_t *p, int64_t n)
{
	struct cue_ctx *c = (struct cue_ctx *) ctx;

	if (id == ID_CUETIME)
		c->t = (av_us) (rd_uint(p, n) * m->tcscale / 1000);
	else if (id == ID_CUETRACKPOS)
		each_child(d, m, ctx, p, n, cuetrack_child);
}

static void cues_child(struct av_demux *d, struct mkv *m, void *ctx, uint32_t id,
		const uint8_t *p, int64_t n)
{
	struct cue_ctx c = { 0, -1 };
	(void) ctx;
	if (id != ID_CUEPOINT)
		return;
	each_child(d, m, &c, p, n, cuepoint_child);
	if (c.pos < 0)
		return;
	if (m->ncues == m->capcues) {
		int cap = m->capcues ? m->capcues * 2 : 64;
		struct cue *q = (struct cue *) realloc(m->cues, (size_t) cap * sizeof *q);
		if (q == NULL)
			return;
		m->cues = q;
		m->capcues = cap;
	}
	m->cues[m->ncues].t = c.t;
	m->cues[m->ncues].pos = c.pos;
	m->ncues++;
}

/* ---- blocks ----------------------------------------------------------------------------- */

static int track_index(struct av_demux *d, int number)
{
	int i;

	for (i = 0; i < d->ntracks; i++)
		if (d->tracks[i].number == number)
			return i;
	return -1;
}

/* a block's header and lacing (data at pos, n bytes) into m->lace; 0 ok, -1 skip it */
static int parse_block(struct av_demux *d, struct mkv *m, int64_t pos, int64_t n, int simple,
		av_us dur, int has_ref, av_us discard)
{
	const uint8_t *p = av__peek(d, pos, (size_t) n);
	struct lace *L = &m->lace;
	int64_t tn, i;
	int a, flags, lacing, ti, k;
	int16_t tc;
	const struct av_track *t;

	if (p == NULL)
		return -1;
	a = read_vint(p, (size_t) n, &tn, NULL);
	if (a <= 0 || a + 3 > n)
		return -1;
	ti = track_index(d, (int) tn);
	if (ti < 0)
		return -1;
	t = &d->tracks[ti];
	tc = (int16_t) av_rb16(p + a);
	flags = p[a + 2];
	i = a + 3;
	L->track = ti;
	L->t = (av_us) ((m->cluster_tc + tc) * (int64_t) m->tcscale / 1000);
	L->key = simple ? (flags & 0x80) != 0 : !has_ref;
	if (t->kind == AV_AUDIO)
		L->key = 1;	/* (every audio frame starts decoding) */
	L->discard = discard;
	L->i = 0;
	lacing = (flags >> 1) & 3;
	if (lacing == 0) {
		L->n = 1;
		L->pos[0] = pos + i;
		L->size[0] = (uint32_t) (n - i);
	} else {
		int count;
		int64_t total = 0, sz;

		if (i >= n)
			return -1;
		count = p[i++] + 1;
		if (count > 64)
			return -1;
		if (lacing == 1) {		/* Xiph */
			for (k = 0; k < count - 1; k++) {
				sz = 0;
				do {
					if (i >= n)
						return -1;
					sz += p[i];
				} while (p[i++] == 255);
				L->size[k] = (uint32_t) sz;
				total += sz;
			}
		} else if (lacing == 3) {	/* EBML */
			int64_t prev;
			int b = read_vint(p + i, (size_t) (n - i), &prev, NULL);
			if (b <= 0)
				return -1;
			i += b;
			L->size[0] = (uint32_t) prev;
			total = prev;
			for (k = 1; k < count - 1; k++) {
				int64_t v;
				b = read_vint(p + i, (size_t) (n - i), &v, NULL);
				if (b <= 0)
					return -1;
				i += b;
				v -= (1LL << (7 * b - 1)) - 1;	/* signed */
				prev += v;
				if (prev < 0)
					return -1;
				L->size[k] = (uint32_t) prev;
				total += prev;
			}
		} else {			/* fixed */
			if ((n - i) % count)
				return -1;
			for (k = 0; k < count - 1; k++)
				L->size[k] = (uint32_t) ((n - i) / count);
			total = (n - i) / count * (count - 1);
		}
		if (i + total > n)
			return -1;
		L->size[count - 1] = (uint32_t) (n - i - total);
		L->n = count;
		for (k = 0; k < count; k++) {
			L->pos[k] = pos + i;
			i += L->size[k];
		}
	}
	if (dur <= 0 && t->default_dur > 0)
		dur = t->default_dur * L->n;
	L->dur = dur;
	return 0;
}

/* the next laced frame as a packet */
static int lace_next(struct av_demux *d, struct mkv *m, struct av_packet *pkt)
{
	struct lace *L = &m->lace;
	const struct av_track *t = &d->tracks[L->track];
	av_us each = L->n > 0 && L->dur > 0 ? L->dur / L->n : t->default_dur;

	pkt->track = L->track;
	pkt->pts = pkt->dts = L->t + each * L->i;
	/* (Matroska keeps no decode times: a video stream with B-frames has its frames' times out of order
	 * -- the decode order is the blocks' order; the store makes the decode times) */
	if (t->kind == AV_VIDEO)
		pkt->dts = AV_NOTIME;
	pkt->dur = each;
	pkt->key = L->key;
	pkt->pos = L->pos[L->i];
	pkt->size = L->size[L->i];
	pkt->data = av__copy(d, L->pos[L->i], L->size[L->i]);
	if (L->i == L->n - 1)
		pkt->discard_end = L->discard;
	L->i++;
	if (pkt->data == NULL)
		return AV_ENOMEM;
	return AV_OK;
}

struct group { int64_t bpos, bsize; av_us dur; int ref; av_us discard; };

static void group_child(struct av_demux *d, struct mkv *m, void *ctx, uint32_t id,
		const uint8_t *p, int64_t n)
{
	struct group *g = (struct group *) ctx;
	(void) d; (void) m;
	if (id == ID_BLOCK) {
		g->bpos = (int64_t) (intptr_t) p;	/* (a pointer for now: made an offset by the caller) */
		g->bsize = n;
	} else if (id == ID_BLOCKDURATION) {
		g->dur = (av_us) (rd_uint(p, n) * m->tcscale / 1000);
	} else if (id == ID_REFERENCEBLOCK) {
		g->ref = 1;
	} else if (id == ID_DISCARDPADDING) {
		int64_t v = (int64_t) rd_uint(p, n);
		if (n < 8 && (p[0] & 0x80))
			v -= 1LL << (8 * n);
		g->discard = v > 0 ? v / 1000 : 0;
	}
}

static int64_t mkv_seek(struct av_demux *d, void *priv, av_us t, av_us *at);

/* ---- the element loop ------------------------------------------------------------------- */

static int is_top_level(uint32_t id)
{
	return id == ID_CLUSTER || id == ID_CUES || id == ID_SEEKHEAD || id == ID_INFO ||
		id == ID_TRACKS || id == 0x1254C367 /* Tags */ || id == 0x1941A469 /* Attachments */ ||
		id == 0x1043A770 /* Chapters */ || id == ID_SEGMENT || id == ID_EBML;
}

/* after a jump: find a Cluster's ID (4 bytes 1F 43 B6 75) from the window's start */
static int resync(struct av_demux *d, struct mkv *m)
{
	size_t n = av__avail(d, m->p), i;
	const uint8_t *p = n ? av__peek(d, m->p, n) : NULL;

	if (p == NULL)
		return AV_AGAIN;
	for (i = 0; i + 4 <= n; i++)
		if (p[i] == 0x1F && p[i + 1] == 0x43 && p[i + 2] == 0xB6 && p[i + 3] == 0x75) {
			m->p += (int64_t) i;
			m->resync = 0;
			return AV_OK;
		}
	/* keep the last 3 bytes */
	if (n > 3) {
		m->p += (int64_t) (n - 3);
		av__consume(d, m->p);
	}
	d->want = av__end(d);
	return AV_AGAIN;
}

static int mkv_read(struct av_demux *d, void *priv, struct av_packet *pkt)
{
	struct mkv *m = (struct mkv *) priv;

	for (;;) {
		const uint8_t *h;
		size_t avail;
		uint32_t id;
		int64_t size, start, dstart;
		int a, b, unk;

		if (m->lace.i < m->lace.n)
			return lace_next(d, m, pkt);
		av__consume(d, m->p);
		if (m->resync) {
			int r = resync(d, m);
			if (r != AV_OK)
				return r;
		}
		/* leave the cluster / segment when past their end */
		if (m->in_cluster && m->cluster_end != UNKNOWN_SIZE && m->p >= m->cluster_end)
			m->in_cluster = 0;
		if (m->in_segment && m->seg_end != UNKNOWN_SIZE && m->p >= m->seg_end)
			m->in_segment = 0;

		avail = av__avail(d, m->p);
		h = avail ? av__peek(d, m->p, avail < 12 ? avail : 12) : NULL;
		if (h == NULL) {
			/* (outside the window: wanted there -- a seek's jump back included) */
			d->want = m->p < d->base + (int64_t) d->off || m->p > av__end(d) ? m->p : av__end(d);
			return AV_AGAIN;
		}
		a = read_id(h, avail < 12 ? avail : 12, &id);
		if (a == 0)
			return AV_ERR;
		if (a < 0) {
			d->want = av__end(d);
			return AV_AGAIN;
		}
		b = read_vint(h + a, (avail < 12 ? avail : 12) - (size_t) a, &size, &unk);
		if (b == 0)
			return AV_ERR;
		if (b < 0) {
			d->want = av__end(d);
			return AV_AGAIN;
		}
		start = m->p;
		dstart = m->p + a + b;
		if (unk)
			size = UNKNOWN_SIZE;

		/* an unknown-size cluster ends at the next cluster / top-level element */
		if (m->in_cluster && m->cluster_end == UNKNOWN_SIZE && is_top_level(id))
			m->in_cluster = 0;

		switch (id) {
		case ID_EBML: {
			const uint8_t *p;
			if (size == UNKNOWN_SIZE || size > 4096)
				return AV_ERR;
			p = av__peek(d, dstart, (size_t) size);
			if (p == NULL)
				return AV_AGAIN;
			each_child(d, m, NULL, p, size, ebml_child);
			if (strcmp(d->name, "webm") && strcmp(d->name, "matroska"))
				return AV_EUNSUP;
			m->p = dstart + size;
			/* a new initialization segment: the segment's state starts again */
			m->in_segment = m->in_cluster = 0;
			continue;
		}
		case ID_SEGMENT:
			m->in_segment = 1;
			m->in_cluster = 0;
			m->seg_start = dstart;
			m->seg_end = size == UNKNOWN_SIZE ? UNKNOWN_SIZE : dstart + size;
			m->p = dstart;
			continue;
		case ID_CLUSTER:
			m->in_cluster = 1;
			m->cluster_end = size == UNKNOWN_SIZE ? UNKNOWN_SIZE : dstart + size;
			m->cluster_tc = 0;
			m->p = dstart;
			continue;
		}

		if (size == UNKNOWN_SIZE)
			return AV_ERR;		/* (only Segment and Cluster may be of unknown size) */

		switch (id) {
		case ID_INFO: case ID_TRACKS: case ID_SEEKHEAD: case ID_CUES: case ID_BLOCKGROUP:
		case ID_TIMECODE: case ID_SIMPLEBLOCK: {
			const uint8_t *p;

			if (size > MAX_WHOLE) {
				m->p = dstart + size;	/* too big to hold: skipped */
				continue;
			}
			p = av__peek(d, dstart, (size_t) size);
			if (p == NULL) {
				if (d->ended)
					return AV_EOF;
				return AV_AGAIN;
			}
			m->p = dstart + size;
			if (id == ID_INFO) {
				double dur = 0;
				m->tcscale = 1000000;
				each_child(d, m, &dur, p, size, info_child);
				if (m->tcscale == 0)
					m->tcscale = 1000000;
				if (dur > 0)
					d->duration = (av_us) (dur * (double) m->tcscale / 1000.0);
			} else if (id == ID_TRACKS) {
				av__clear_tracks(d);
				each_child(d, m, NULL, p, size, tracks_child);
				d->init_new = 1;
			} else if (id == ID_SEEKHEAD) {
				each_child(d, m, NULL, p, size, seekhead_child);
			} else if (id == ID_CUES) {
				if (!m->cues_read) {
					m->ncues = 0;
					each_child(d, m, NULL, p, size, cues_child);
					m->cues_read = 1;
				}
				if (m->pending != AV_NOTIME) {
					/* the seek that came for them: on to its cluster */
					av_us t = m->pending;
					m->pending = AV_NOTIME;
					if (mkv_seek(d, m, t, NULL) < 0)
						m->p = dstart + size;
				}
			} else if (id == ID_TIMECODE) {
				m->cluster_tc = (int64_t) rd_uint(p, size);
			} else if (id == ID_SIMPLEBLOCK && m->in_cluster) {
				if (parse_block(d, m, dstart, size, 1, 0, 0, 0) == 0)
					return lace_next(d, m, pkt);
			} else if (id == ID_BLOCKGROUP && m->in_cluster) {
				struct group g;
				memset(&g, 0, sizeof g);
				each_child(d, m, &g, p, size, group_child);
				if (g.bsize > 0) {
					int64_t bpos = dstart + ((const uint8_t *) (intptr_t) g.bpos - p);
					if (parse_block(d, m, bpos, g.bsize, 0, g.dur, g.ref, g.discard) == 0)
						return lace_next(d, m, pkt);
				}
			}
			continue;
		}
		default:
			/* skipped (Void, CRC-32, Tags, Attachments, Chapters, unknown) */
			(void) start;
			m->p = dstart + size;
			continue;
		}
	}
}

static void *mkv_create(struct av_demux *d)
{
	struct mkv *m = (struct mkv *) calloc(1, sizeof *m);
	(void) d;
	if (m == NULL)
		return NULL;
	m->tcscale = 1000000;
	m->cues_pos = -1;
	m->seg_end = UNKNOWN_SIZE;
	m->pending = AV_NOTIME;
	strcpy(d->name, "matroska");
	return m;
}

static void mkv_destroy(void *priv)
{
	struct mkv *m = (struct mkv *) priv;

	if (m != NULL)
		free(m->cues);
	free(m);
}

static void mkv_restart(struct av_demux *d, void *priv, int64_t pos)
{
	struct mkv *m = (struct mkv *) priv;
	(void) d;
	m->p = pos;
	m->lace.n = m->lace.i = 0;
	m->in_cluster = 0;
	/* within the segment: look for a cluster if pos is not a known one */
	m->resync = m->in_segment && pos > m->seg_start;
}

static void mkv_reset(struct av_demux *d, void *priv)
{
	struct mkv *m = (struct mkv *) priv;

	/* MSE abort(): the next append starts with an element */
	m->p = av__end(d);
	m->lace.n = m->lace.i = 0;
	m->in_cluster = 0;
	m->resync = 0;
}

static int64_t mkv_seek(struct av_demux *d, void *priv, av_us t, av_us *at)
{
	struct mkv *m = (struct mkv *) priv;
	int i, best = -1;

	if (!m->in_segment && m->seg_start == 0)
		return -1;
	if (!m->cues_read) {
		if (m->cues_pos < 0)
			return -1;
		/* read the Cues first, then on to the cluster: the caller follows av_demux_want */
		m->pending = t;
		m->p = m->seg_start + m->cues_pos;
		m->lace.n = m->lace.i = 0;
		m->in_cluster = 0;
		m->resync = 0;
		return m->p;
	}
	for (i = 0; i < m->ncues; i++)
		if (m->cues[i].t <= t && (best < 0 || m->cues[i].t >= m->cues[best].t))
			best = i;
	if (best < 0 && m->ncues > 0)
		best = 0;
	if (best < 0)
		return -1;
	if (at != NULL)
		*at = m->cues[best].t;
	m->p = m->seg_start + m->cues[best].pos;
	m->lace.n = m->lace.i = 0;
	m->in_cluster = 0;
	m->in_segment = 1;
	m->resync = 0;
	return m->p;
}

/* (for av_store: whether the Cues were read -- a seek may need two steps) */
int av__mkv_cues_read(void *priv)
{
	return ((struct mkv *) priv)->cues_read;
}

const struct av_fmt_ops av_mkv_ops = {
	"matroska", mkv_create, mkv_destroy, mkv_read, mkv_restart, mkv_seek, mkv_reset
};
