/*
 * user/av/av_store.c -- the coded frames, by source and track: what Media Source Extensions
 * keep (a SourceBuffer's track buffers), and what a file's reader keeps ahead of playback.
 *
 * A source has a demuxer (its byte stream's parser); each packet it gives goes through MSE's
 * coded frame processing (simplified): timestampOffset, the append window, a discontinuity
 * (a decode time going back or jumping) waits for a random access point, frames of an earlier
 * coded frame group that the new ones overlap are removed (with the frames that depend on them,
 * up to the next random access point), and the frame is inserted in decode order.
 * Buffered ranges are the union of [pts, pts + duration) of a track's frames, the source's the
 * intersection of its tracks'. A quota per source: past it, frames well behind the playback
 * position are evicted (whole groups), else the append is refused (QuotaExceededError).
 *
 * The player reads frames by decode time (av__store_next): a cursor that survives insertions
 * and removals. Everything is under one lock (the player's threads read, the host appends).
 */
#include <stdio.h>
#include "av_int.h"
#include "av_os.h"
#include "av_store.h"

#define MAX_SOURCES 4
#define DEFAULT_QUOTA (48u << 20)

struct sframe {
	av_us pts, dts, dur;
	int key;
	unsigned gen;		/* its coded frame group */
	uint8_t *data;
	size_t size;
};

struct strack {
	int kind;
	struct sframe *f;
	int n, cap;
	av_us last_dts, last_dur;	/* AV_NOTIME: unset */
	int need_rap;
	size_t bytes;
};

struct source {
	int used;
	int fmt;
	struct av_demux *dx;
	int init;
	struct strack tr[AV_MAX_TRACKS];
	av_us offset;
	int sequence;
	av_us group_start, group_end;	/* sequence mode */
	av_us win_start, win_end;
	unsigned gen;
	size_t quota, bytes;
	int file_mode, fed_end;
};

struct av_store {
	av_lock_t lock;
	struct source src[MAX_SOURCES];
	int eos;
	av_us now;			/* the playback position (eviction keeps what is after it) */
	unsigned serial;		/* bumped at each change (the player's threads look again) */
};

void av__store_lock(struct av_store *s) { av_lock(&s->lock); }
void av__store_unlock(struct av_store *s) { av_unlock(&s->lock); }
unsigned av__store_serial(struct av_store *s) { return s->serial; }
void av__store_set_now(struct av_store *s, av_us t) { s->now = t; }

struct av_store *av_store_new(void)
{
	struct av_store *s = (struct av_store *) calloc(1, sizeof *s);

	if (s != NULL)
		av_lock_init(&s->lock);
	return s;
}

static void track_clear(struct strack *t)
{
	int i;

	for (i = 0; i < t->n; i++)
		free(t->f[i].data);
	free(t->f);
	memset(t, 0, sizeof *t);
	t->last_dts = AV_NOTIME;
}

static void source_clear(struct source *so)
{
	int k;

	for (k = 0; k < AV_MAX_TRACKS; k++)
		track_clear(&so->tr[k]);
	av_demux_free(so->dx);
	memset(so, 0, sizeof *so);
}

void av_store_free(struct av_store *s)
{
	int i;

	if (s == NULL)
		return;
	for (i = 0; i < MAX_SOURCES; i++)
		if (s->src[i].used)
			source_clear(&s->src[i]);
	free(s);
}

int av_store_add_source(struct av_store *s, const char *mime, size_t quota)
{
	int i, fmt = mime != NULL && *mime ? av_format_of_mime(mime) : AV_FMT_UNKNOWN, k;

	if (mime != NULL && *mime && fmt == AV_FMT_UNKNOWN)
		return AV_EUNSUP;
	av_lock(&s->lock);
	for (i = 0; i < MAX_SOURCES; i++)
		if (!s->src[i].used)
			break;
	if (i == MAX_SOURCES) {
		av_unlock(&s->lock);
		return AV_EFULL;
	}
	memset(&s->src[i], 0, sizeof s->src[i]);
	s->src[i].dx = av_demux_new(fmt);
	if (s->src[i].dx == NULL) {
		av_unlock(&s->lock);
		return AV_ENOMEM;
	}
	s->src[i].used = 1;
	s->src[i].fmt = fmt;
	s->src[i].quota = quota ? quota : DEFAULT_QUOTA;
	s->src[i].win_end = INT64_MAX;
	s->src[i].group_start = AV_NOTIME;
	for (k = 0; k < AV_MAX_TRACKS; k++)
		s->src[i].tr[k].last_dts = AV_NOTIME;
	s->serial++;
	av_unlock(&s->lock);
	return i;
}

static struct source *get(struct av_store *s, int src)
{
	return src >= 0 && src < MAX_SOURCES && s->src[src].used ? &s->src[src] : NULL;
}

void av_store_remove_source(struct av_store *s, int src)
{
	struct source *so;

	av_lock(&s->lock);
	if ((so = get(s, src)) != NULL)
		source_clear(so);
	s->serial++;
	av_unlock(&s->lock);
}

/* ---- frames ------------------------------------------------------------------------------- */

/* the first frame whose dts >= t (binary search) */
static int lower_dts(const struct strack *t, av_us dts)
{
	int lo = 0, hi = t->n;

	while (lo < hi) {
		int mid = (lo + hi) / 2;
		if (t->f[mid].dts < dts)
			lo = mid + 1;
		else
			hi = mid;
	}
	return lo;
}

static void remove_at(struct source *so, struct strack *t, int i)
{
	so->bytes -= t->f[i].size;
	t->bytes -= t->f[i].size;
	free(t->f[i].data);
	memmove(&t->f[i], &t->f[i + 1], (size_t) (t->n - i - 1) * sizeof t->f[0]);
	t->n--;
}

/* frames of other groups that [pts, end) overlaps, and those that depend on them */
static void remove_overlap(struct source *so, struct strack *t, av_us pts, av_us end, unsigned gen)
{
	int i = 0;

	while (i < t->n) {
		struct sframe *f = &t->f[i];
		if (f->gen != gen && f->pts >= pts && f->pts < end) {
			remove_at(so, t, i);
			/* the frames after it up to the next random access point */
			while (i < t->n && !t->f[i].key && t->f[i].gen != gen)
				remove_at(so, t, i);
			continue;
		}
		i++;
	}
}

static int insert(struct source *so, struct strack *t, const struct av_packet *p, av_us pts, av_us dts,
		av_us dur)
{
	int i;

	if (t->n == t->cap) {
		int cap = t->cap ? t->cap * 2 : 256;
		struct sframe *q = (struct sframe *) realloc(t->f, (size_t) cap * sizeof *q);
		if (q == NULL)
			return AV_ENOMEM;
		t->f = q;
		t->cap = cap;
	}
	i = t->n > 0 && t->f[t->n - 1].dts <= dts ? t->n : lower_dts(t, dts + 1);
	memmove(&t->f[i + 1], &t->f[i], (size_t) (t->n - i) * sizeof t->f[0]);
	t->f[i].pts = pts;
	t->f[i].dts = dts;
	t->f[i].dur = dur;
	t->f[i].key = p->key;
	t->f[i].gen = so->gen;
	t->f[i].data = p->data;
	t->f[i].size = p->size;
	t->n++;
	t->bytes += p->size;
	so->bytes += p->size;
	return AV_OK;
}

/* one packet through the coded frame processing; it takes p's data (or frees it) */
static int process(struct av_store *s, struct source *so, struct av_packet *p)
{
	const struct av_track *info = av_demux_track(so->dx, p->track);
	struct strack *t;
	av_us pts, dts, dur;
	int k;

	if (info == NULL || info->kind == 0 || p->track >= AV_MAX_TRACKS) {
		av_packet_free(p);
		return AV_OK;
	}
	t = &so->tr[p->track];
	t->kind = info->kind;
	dur = p->dur > 0 ? p->dur : info->default_dur > 0 ? info->default_dur :
		t->last_dur > 0 ? t->last_dur : (info->kind == AV_VIDEO ? 33333 : 20000);
	if (so->sequence && so->group_start != AV_NOTIME) {
		so->offset = so->group_start - p->pts;
		so->group_start = AV_NOTIME;
	}
	pts = p->pts + so->offset;
	dts = (p->dts != AV_NOTIME ? p->dts : p->pts) + so->offset;
	/* a discontinuity: every track of the source waits for a random access point */
	if (t->last_dts != AV_NOTIME && (dts < t->last_dts || dts - t->last_dts > 2 * (t->last_dur > 0 ? t->last_dur : dur) + 100000)) {
		for (k = 0; k < AV_MAX_TRACKS; k++) {
			so->tr[k].last_dts = AV_NOTIME;
			so->tr[k].need_rap = 1;
		}
		so->gen++;
	}
	/* the append window */
	if (pts < so->win_start || pts + dur > so->win_end) {
		t->need_rap = 1;
		av_packet_free(p);
		return AV_OK;
	}
	if (t->need_rap || t->last_dts == AV_NOTIME) {
		if (!p->key) {
			av_packet_free(p);
			return AV_OK;
		}
		t->need_rap = 0;
	}
	remove_overlap(so, t, pts, pts + dur, so->gen);
	if (insert(so, t, p, pts, dts, dur) != AV_OK) {
		av_packet_free(p);
		return AV_ENOMEM;
	}
	p->data = NULL;
	t->last_dts = dts;
	t->last_dur = dur;
	if (pts + dur > so->group_end)
		so->group_end = pts + dur;
	s->serial++;
	return AV_OK;
}

/* drain the demuxer into the tracks */
static int pump(struct av_store *s, struct source *so)
{
	for (;;) {
		struct av_packet p;
		int r = av_demux_read(so->dx, &p);

		if (av_demux_take_init(so->dx)) {
			/* a (new) initialization segment: a new coded frame group */
			so->init = 1;
			so->gen++;
			s->serial++;
		}
		if (r == AV_OK) {
			r = process(s, so, &p);
			if (r != AV_OK)
				return r;
			continue;
		}
		if (r == AV_AGAIN || r == AV_EOF)
			return AV_OK;
		return r;
	}
}

/* frames before keep_from removed, whole groups (up to a random access point), until the
 * source holds at most `limit` bytes -> 1 if it does */
static int evict(struct source *so, av_us keep_from, size_t limit)
{
	int k;

	for (k = 0; k < AV_MAX_TRACKS && so->bytes > limit; k++) {
		struct strack *t = &so->tr[k];
		int end = 0, i;
		/* the last random access point before keep_from: everything before it goes */
		for (i = 0; i < t->n && t->f[i].pts + t->f[i].dur <= keep_from; i++)
			if (t->f[i].key)
				end = i;
		for (i = 0; i < end && so->bytes > limit; ) {
			remove_at(so, t, 0);
			end--;
		}
	}
	return so->bytes <= limit;
}

int av_store_append(struct av_store *s, int src, const void *p, size_t n)
{
	struct source *so;
	int r;

	av_lock(&s->lock);
	so = get(s, src);
	if (so == NULL) {
		av_unlock(&s->lock);
		return AV_ERR;
	}
	if (so->bytes + n > so->quota && !evict(so, s->now - 10 * AV_US, so->quota - n)) {
		av_unlock(&s->lock);
		return AV_EFULL;
	}
	s->eos = 0;
	r = av_demux_append(so->dx, p, n);
	if (r == AV_OK)
		r = pump(s, so);
	s->serial++;
	av_unlock(&s->lock);
	return r;
}

int av_store_feed(struct av_store *s, int src, int64_t pos, const void *p, size_t n)
{
	struct source *so;
	int r;

	av_lock(&s->lock);
	so = get(s, src);
	if (so == NULL) {
		av_unlock(&s->lock);
		return AV_ERR;
	}
	so->file_mode = 1;
	so->fed_end = 0;
	r = av_demux_feed(so->dx, pos, p, n);
	if (r == AV_OK)
		r = pump(s, so);
	/* the file mode keeps what is around the playback position */
	if (so->bytes > so->quota)
		evict(so, s->now - 5 * AV_US, so->quota * 3 / 4);
	s->serial++;
	av_unlock(&s->lock);
	return r;
}

void av_store_feed_end(struct av_store *s, int src)
{
	struct source *so;

	av_lock(&s->lock);
	if ((so = get(s, src)) != NULL) {
		av_demux_end(so->dx);
		pump(s, so);
		so->fed_end = 1;
	}
	s->serial++;
	av_unlock(&s->lock);
}

int64_t av_store_want(struct av_store *s, int src)
{
	struct source *so;
	int64_t w = -1;

	av_lock(&s->lock);
	if ((so = get(s, src)) != NULL && !so->fed_end)
		w = av_demux_want(so->dx);
	av_unlock(&s->lock);
	return w;
}

int av__store_seek_file(struct av_store *s, int src, av_us t)
{
	struct source *so;
	int64_t r = -1;
	int k;

	av_lock(&s->lock);
	if ((so = get(s, src)) != NULL && so->file_mode) {
		r = av_demux_seek(so->dx, t, NULL);
		if (r >= 0) {
			so->fed_end = 0;
			for (k = 0; k < AV_MAX_TRACKS; k++) {
				so->tr[k].last_dts = AV_NOTIME;
				so->tr[k].need_rap = 1;
			}
			so->gen++;
		}
	}
	s->serial++;
	av_unlock(&s->lock);
	return r >= 0 ? 0 : -1;
}

int av_store_change_type(struct av_store *s, int src, const char *mime)
{
	struct source *so;
	struct av_demux *dx;
	int fmt = av_format_of_mime(mime), k;

	if (fmt == AV_FMT_UNKNOWN)
		return AV_EUNSUP;
	dx = av_demux_new(fmt);
	if (dx == NULL)
		return AV_ENOMEM;
	av_lock(&s->lock);
	so = get(s, src);
	if (so == NULL) {
		av_unlock(&s->lock);
		av_demux_free(dx);
		return AV_ERR;
	}
	/* the frames stay; the next bytes are an initialization segment of the new type */
	av_demux_free(so->dx);
	so->dx = dx;
	so->fmt = fmt;
	for (k = 0; k < AV_MAX_TRACKS; k++) {
		so->tr[k].last_dts = AV_NOTIME;
		so->tr[k].need_rap = 1;
	}
	so->gen++;
	s->serial++;
	av_unlock(&s->lock);
	return AV_OK;
}

void av_store_set_offset(struct av_store *s, int src, av_us offset)
{
	struct source *so;

	av_lock(&s->lock);
	if ((so = get(s, src)) != NULL && so->offset != offset) {
		so->offset = offset;
		so->gen++;
	}
	av_unlock(&s->lock);
}

av_us av_store_get_offset(struct av_store *s, int src)
{
	struct source *so;
	av_us o = 0;

	av_lock(&s->lock);
	if ((so = get(s, src)) != NULL)
		o = so->offset;
	av_unlock(&s->lock);
	return o;
}

void av_store_set_mode(struct av_store *s, int src, int sequence)
{
	struct source *so;

	av_lock(&s->lock);
	if ((so = get(s, src)) != NULL) {
		so->sequence = sequence;
		if (sequence)
			so->group_start = so->group_end;
	}
	av_unlock(&s->lock);
}

void av_store_set_window(struct av_store *s, int src, av_us start, av_us end)
{
	struct source *so;

	av_lock(&s->lock);
	if ((so = get(s, src)) != NULL) {
		so->win_start = start;
		so->win_end = end;
	}
	av_unlock(&s->lock);
}

void av_store_reset_parser(struct av_store *s, int src)
{
	struct source *so;
	int k;

	av_lock(&s->lock);
	if ((so = get(s, src)) != NULL) {
		av__demux_reset(so->dx);
		for (k = 0; k < AV_MAX_TRACKS; k++) {
			so->tr[k].last_dts = AV_NOTIME;
			so->tr[k].need_rap = 1;
		}
		so->gen++;
		if (so->sequence)
			so->group_start = so->group_end;
	}
	av_unlock(&s->lock);
}

void av_store_remove(struct av_store *s, int src, av_us start, av_us end)
{
	struct source *so;
	int k;

	av_lock(&s->lock);
	if ((so = get(s, src)) != NULL) {
		for (k = 0; k < AV_MAX_TRACKS; k++) {
			struct strack *t = &so->tr[k];
			int i = 0;
			while (i < t->n) {
				if (t->f[i].pts >= start && t->f[i].pts < end) {
					remove_at(so, t, i);
					/* and what depends on it, up to the next random access point */
					while (i < t->n && !t->f[i].key && t->f[i].pts >= end)
						remove_at(so, t, i);
					continue;
				}
				i++;
			}
			t->need_rap = 1;
		}
	}
	s->serial++;
	av_unlock(&s->lock);
}

/* ---- ranges ------------------------------------------------------------------------------- */

static int cmp_range(const void *a, const void *b)
{
	const struct av_range *x = (const struct av_range *) a, *y = (const struct av_range *) b;
	return x->start < y->start ? -1 : x->start > y->start;
}

/* a track's ranges (malloc'd) -> count */
static int track_ranges(const struct strack *t, struct av_range **out)
{
	struct av_range *r;
	int i, n = 0;

	*out = NULL;
	if (t->n == 0)
		return 0;
	r = (struct av_range *) malloc((size_t) t->n * sizeof *r);
	if (r == NULL)
		return 0;
	for (i = 0; i < t->n; i++) {
		r[i].start = t->f[i].pts;
		r[i].end = t->f[i].pts + t->f[i].dur;
	}
	qsort(r, (size_t) t->n, sizeof *r, cmp_range);
	for (i = 1; i < t->n; i++) {
		/* gaps up to a frame's length (or 0.1 s) are joined, as browsers do */
		av_us tol = t->f[0].dur > 100000 ? t->f[0].dur : 100000;
		if (r[i].start <= r[n].end + tol) {
			if (r[i].end > r[n].end)
				r[n].end = r[i].end;
		} else {
			r[++n] = r[i];
		}
	}
	*out = r;
	return n + 1;
}

/* a and b intersected into out (max entries) */
static int intersect(const struct av_range *a, int na, const struct av_range *b, int nb,
		struct av_range *out, int max)
{
	int i = 0, j = 0, n = 0;

	while (i < na && j < nb && n < max) {
		av_us s = a[i].start > b[j].start ? a[i].start : b[j].start;
		av_us e = a[i].end < b[j].end ? a[i].end : b[j].end;
		if (s < e)
			out[n++] = (struct av_range) { s, e };
		if (a[i].end < b[j].end)
			i++;
		else
			j++;
	}
	return n;
}

static int source_ranges(struct av_store *s, struct source *so, struct av_range *out, int max)
{
	struct av_range cur[64], tmp[64];
	int n = -1, k, i;
	av_us top = 0;

	for (k = 0; k < AV_MAX_TRACKS; k++) {
		struct av_range *r;
		int m;
		const struct av_track *info = av_demux_track(so->dx, k);
		if (info == NULL || info->kind == 0 || av_decoder_supported_track(info) == 0)
			continue;
		m = track_ranges(&so->tr[k], &r);
		if (m > 0 && r[m - 1].end > top)
			top = r[m - 1].end;
		if (n < 0) {
			n = m < 64 ? m : 64;
			for (i = 0; i < n; i++)
				cur[i] = r[i];
		} else {
			n = intersect(cur, n, r, m, tmp, 64);
			memcpy(cur, tmp, (size_t) n * sizeof cur[0]);
		}
		free(r);
	}
	if (n <= 0)
		return 0;
	/* ended: the last range reaches the highest end of any track (MSE's rule) */
	if ((s->eos || so->fed_end) && cur[n - 1].end < top)
		cur[n - 1].end = top;
	for (i = 0; i < n && i < max; i++)
		out[i] = cur[i];
	return i;
}

int av_store_buffered(struct av_store *s, int src, struct av_range *r, int max)
{
	struct source *so;
	int n = 0;

	av_lock(&s->lock);
	if ((so = get(s, src)) != NULL)
		n = source_ranges(s, so, r, max);
	av_unlock(&s->lock);
	return n;
}

int av_store_has_init(struct av_store *s, int src)
{
	struct source *so;
	int r = 0;

	av_lock(&s->lock);
	if ((so = get(s, src)) != NULL)
		r = so->init;
	av_unlock(&s->lock);
	return r;
}

int av_store_ntracks(struct av_store *s, int src)
{
	struct source *so = get(s, src);
	return so != NULL ? av_demux_ntracks(so->dx) : 0;
}

const struct av_track *av_store_track(struct av_store *s, int src, int i)
{
	struct source *so = get(s, src);
	return so != NULL ? av_demux_track(so->dx, i) : NULL;
}

void av_store_set_eos(struct av_store *s, int eos)
{
	av_lock(&s->lock);
	s->eos = eos;
	s->serial++;
	av_unlock(&s->lock);
}

av_us av_store_end(struct av_store *s)
{
	av_us end = 0;
	int i, k, j;

	av_lock(&s->lock);
	for (i = 0; i < MAX_SOURCES; i++) {
		if (!s->src[i].used)
			continue;
		for (k = 0; k < AV_MAX_TRACKS; k++)
			for (j = 0; j < s->src[i].tr[k].n; j++) {
				const struct sframe *f = &s->src[i].tr[k].f[j];
				if (f->pts + f->dur > end)
					end = f->pts + f->dur;
			}
	}
	av_unlock(&s->lock);
	return end;
}

av_us av_store_duration(struct av_store *s)
{
	av_us d = 0;
	int i;

	av_lock(&s->lock);
	for (i = 0; i < MAX_SOURCES; i++)
		if (s->src[i].used && av_demux_duration(s->src[i].dx) > d)
			d = av_demux_duration(s->src[i].dx);
	av_unlock(&s->lock);
	return d;
}

/* ---- the player's side (av_store.h; the lock held by the caller) ------------------------ */

int av__store_pick(struct av_store *s, int kind, int *src, int *track, struct av_track *info)
{
	int i, k;

	for (i = 0; i < MAX_SOURCES; i++) {
		struct source *so = &s->src[i];
		if (!so->used || !so->init)
			continue;
		for (k = 0; k < av_demux_ntracks(so->dx); k++) {
			const struct av_track *t = av_demux_track(so->dx, k);
			if (t->kind == kind) {
				*src = i;
				*track = k;
				if (info != NULL) {
					*info = *t;
					info->extra = NULL;	/* (the caller copies it if it needs it) */
				}
				return 1;
			}
		}
	}
	return 0;
}

const struct av_track *av__store_track_info(struct av_store *s, int src, int track)
{
	struct source *so = get(s, src);
	return so != NULL ? av_demux_track(so->dx, track) : NULL;
}

int av__store_next(struct av_store *s, int src, int track, av_us after, int inclusive, av_us gap,
		struct av_packet *p)
{
	struct source *so = get(s, src);
	struct strack *t;
	int i;

	memset(p, 0, sizeof *p);
	if (so == NULL)
		return AV_ERR;
	t = &so->tr[track];
	i = after == AV_NOTIME ? 0 : lower_dts(t, inclusive ? after : after + 1);
	if (i >= t->n) {
		if (s->eos || so->fed_end)
			return AV_EOF;
		return AV_AGAIN;
	}
	/* a hole after the last frame read: wait for it to be filled (unless the stream ended) */
	if (after != AV_NOTIME && gap > 0 && t->f[i].dts > after + gap && !(s->eos || so->fed_end))
		return AV_AGAIN;
	p->track = track;
	p->pts = t->f[i].pts;
	p->dts = t->f[i].dts;
	p->dur = t->f[i].dur;
	p->key = t->f[i].key;
	p->size = t->f[i].size;
	p->pos = -1;
	p->data = (uint8_t *) malloc(p->size ? p->size : 1);
	if (p->data == NULL)
		return AV_ENOMEM;
	memcpy(p->data, t->f[i].data, p->size);
	return AV_OK;
}

av_us av__store_rap_before(struct av_store *s, int src, int track, av_us t)
{
	struct source *so = get(s, src);
	struct strack *tr;
	av_us best = AV_NOTIME, first = AV_NOTIME;
	int i;

	if (so == NULL)
		return AV_NOTIME;
	tr = &so->tr[track];
	for (i = 0; i < tr->n; i++) {
		if (!tr->f[i].key)
			continue;
		if (tr->f[i].pts <= t)
			best = tr->f[i].dts;
		else if (first == AV_NOTIME)
			first = tr->f[i].dts;
	}
	if (best == AV_NOTIME && first != AV_NOTIME && tr->n > 0)
		/* nothing at or before t: the first one after it, if it is close (a stream that
		 * starts a little after 0) */
		return first;
	return best;
}

int av__store_ended(struct av_store *s, int src)
{
	struct source *so = get(s, src);
	return s->eos || (so != NULL && so->fed_end);
}

int av__store_is_file(struct av_store *s, int src)
{
	struct source *so = get(s, src);
	return so != NULL && so->file_mode;
}

av_us av__store_ahead(struct av_store *s, av_us t)
{
	struct av_range r[16];
	int i, n, k;
	av_us best = 0;

	for (k = 0; k < MAX_SOURCES; k++) {
		if (!s->src[k].used)
			continue;
		n = source_ranges(s, &s->src[k], r, 16);
		for (i = 0; i < n; i++)
			if (r[i].start <= t + 200000 && r[i].end > t) {
				av_us a = r[i].end - t;
				if (best == 0 || a < best)
					best = a;
			}
	}
	return best;
}

void av__store_trim(struct av_store *s, av_us before)
{
	int k;

	for (k = 0; k < MAX_SOURCES; k++)
		if (s->src[k].used && s->src[k].file_mode)
			evict(&s->src[k], before, 0);
}
