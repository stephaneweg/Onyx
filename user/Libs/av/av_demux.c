/*
 * user/Libs/av/av_demux.c -- the containers' common part: the byte window the parsers read, the
 * probe, the public av_demux_* calls (av.h).
 *
 * The window holds the stream's bytes [base, base + len); the parsers ask for bytes at an
 * offset (av__peek) and say what they no longer need (av__consume). Bytes fed at the window's
 * end extend it; bytes fed elsewhere (a seek, a jump over an mdat) replace it and the parser is
 * told (ops->restart). MSE's appends always extend it (av_demux_append).
 */
#include <stdio.h>
#include <ctype.h>
#include "av_int.h"

void av_pkt_free(struct av_packet *p)
{
	if (p != NULL) {
		free(p->data);
		p->data = NULL;
		p->size = 0;
	}
}

/* ---- the window --------------------------------------------------------------------------- */

const uint8_t *av__peek(struct av_demux *d, int64_t pos, size_t n)
{
	if (pos >= d->base && pos + (int64_t) n <= av__end(d) && pos - d->base >= (int64_t) d->off)
		return d->buf + (pos - d->base);
	if (pos + (int64_t) n > av__end(d))
		d->want = pos >= av__end(d) ? pos : av__end(d);
	return NULL;
}

size_t av__avail(struct av_demux *d, int64_t pos)
{
	if (pos < d->base + (int64_t) d->off || pos >= av__end(d))
		return 0;
	return (size_t) (av__end(d) - pos);
}

void av__consume(struct av_demux *d, int64_t pos)
{
	if (pos <= d->base + (int64_t) d->off)
		return;
	if (pos >= av__end(d)) {
		/* all of it: the window restarts empty at pos (bytes up to pos are skipped as
		 * they come: av_demux_feed drops them) */
		d->skip_from = av__end(d);
		d->base = pos;
		d->len = d->off = 0;
		return;
	}
	d->off = (size_t) (pos - d->base);
	/* compact when the dead front is large */
	if (d->off > 65536 && d->off * 2 > d->len) {
		memmove(d->buf, d->buf + d->off, d->len - d->off);
		d->len -= d->off;
		d->base += (int64_t) d->off;
		d->off = 0;
	}
}

uint8_t *av__copy(struct av_demux *d, int64_t pos, size_t n)
{
	const uint8_t *s = av__peek(d, pos, n);
	uint8_t *p;

	if (s == NULL)
		return NULL;
	p = (uint8_t *) malloc(n ? n : 1);
	if (p != NULL)
		memcpy(p, s, n);
	return p;
}

struct av_track *av__new_track(struct av_demux *d)
{
	struct av_track *t;

	if (d->ntracks >= AV_MAX_TRACKS)
		return NULL;
	t = &d->tracks[d->ntracks++];
	memset(t, 0, sizeof *t);
	return t;
}

void av__clear_tracks(struct av_demux *d)
{
	int i;

	for (i = 0; i < d->ntracks; i++)
		free(d->tracks[i].extra);
	memset(d->tracks, 0, sizeof d->tracks);
	d->ntracks = 0;
}

/* append bytes at the window's end */
static int win_append(struct av_demux *d, const uint8_t *p, size_t n)
{
	if (d->len + n > d->cap) {
		size_t cap = d->cap ? d->cap : 65536;
		uint8_t *b;

		/* first drop the consumed front */
		if (d->off > 0) {
			memmove(d->buf, d->buf + d->off, d->len - d->off);
			d->len -= d->off;
			d->base += (int64_t) d->off;
			d->off = 0;
		}
		while (cap < d->len + n)
			cap *= 2;
		if (cap != d->cap) {
			b = (uint8_t *) realloc(d->buf, cap);
			if (b == NULL)
				return AV_ENOMEM;
			d->buf = b;
			d->cap = cap;
		}
	}
	memcpy(d->buf + d->len, p, n);
	d->len += n;
	return AV_OK;
}

/* ---- the probe ---------------------------------------------------------------------------- */

int av_probe(const uint8_t *p, size_t n)
{
	if (n >= 4 && av_rb32(p) == 0x1A45DFA3)
		return AV_FMT_MKV;
	if (n >= 8 && (!memcmp(p + 4, "ftyp", 4) || !memcmp(p + 4, "moov", 4) ||
			!memcmp(p + 4, "styp", 4) || !memcmp(p + 4, "moof", 4) ||
			!memcmp(p + 4, "sidx", 4) || !memcmp(p + 4, "mdat", 4) ||
			!memcmp(p + 4, "free", 4) || !memcmp(p + 4, "skip", 4) ||
			!memcmp(p + 4, "wide", 4)))
		return AV_FMT_MP4;
	if (n >= 12 && !memcmp(p, "RIFF", 4) && !memcmp(p + 8, "WAVE", 4))
		return AV_FMT_WAV;
	if (n >= 4 && !memcmp(p, "fLaC", 4))
		return AV_FMT_FLAC;
	if (av__mp3_probe(p, n))
		return AV_FMT_MP3;
#ifdef AV_WITH_FFMPEG
	if (av__lavf_probe(p, n))
		return AV_FMT_LAVF;
#endif
	return AV_FMT_UNKNOWN;
}

int av_format_of_mime(const char *mime)
{
	char t[64];
	size_t i;

	for (i = 0; mime[i] && mime[i] != ';' && i < sizeof t - 1; i++)
		t[i] = (char) tolower((unsigned char) mime[i]);
	while (i > 0 && t[i - 1] == ' ')
		i--;
	t[i] = 0;
	if (!strcmp(t, "video/webm") || !strcmp(t, "audio/webm") || !strcmp(t, "video/x-matroska") ||
	    !strcmp(t, "audio/x-matroska") || !strcmp(t, "video/matroska") || !strcmp(t, "audio/matroska"))
		return AV_FMT_MKV;
	if (!strcmp(t, "video/mp4") || !strcmp(t, "audio/mp4") || !strcmp(t, "audio/x-m4a") ||
	    !strcmp(t, "video/quicktime") || !strcmp(t, "audio/m4a"))
		return AV_FMT_MP4;
	if (!strcmp(t, "audio/wav") || !strcmp(t, "audio/wave") || !strcmp(t, "audio/x-wav") ||
	    !strcmp(t, "audio/vnd.wave"))
		return AV_FMT_WAV;
	if (!strcmp(t, "audio/flac") || !strcmp(t, "audio/x-flac"))
		return AV_FMT_FLAC;
	if (!strcmp(t, "audio/mpeg") || !strcmp(t, "audio/mp3") || !strcmp(t, "audio/x-mp3") ||
	    !strcmp(t, "audio/mpeg3"))
		return AV_FMT_MP3;
#ifdef AV_WITH_FFMPEG
	/* the others FFmpeg reads (and "application/x-onyx-lavf": the file mode's, probed by it) */
	if (!strcmp(t, "application/x-onyx-lavf") || !strcmp(t, "video/x-msvideo") || !strcmp(t, "video/avi") ||
	    !strcmp(t, "video/mp2t") || !strcmp(t, "video/mpeg") || !strcmp(t, "video/x-flv") ||
	    !strcmp(t, "video/x-ms-wmv") || !strcmp(t, "video/x-ms-asf") || !strcmp(t, "audio/x-ms-wma") ||
	    !strcmp(t, "video/ogg") || !strcmp(t, "audio/ogg") || !strcmp(t, "video/3gpp") || !strcmp(t, "audio/aac") ||
	    !strcmp(t, "audio/ac3"))
		return AV_FMT_LAVF;
#endif
	return AV_FMT_UNKNOWN;
}

static const struct av_fmt_ops *ops_of(int fmt)
{
	switch (fmt) {
	case AV_FMT_MKV: return &av_mkv_ops;
	case AV_FMT_MP4: return &av_mp4_ops;
	case AV_FMT_WAV: return &av_wav_ops;
	case AV_FMT_FLAC: return &av_flac_ops;
	case AV_FMT_MP3: return &av_mp3_ops;
#ifdef AV_WITH_FFMPEG
	case AV_FMT_LAVF: return &av_lavf_ops;
#endif
	}
	return NULL;
}

/* ---- the public calls --------------------------------------------------------------------- */

struct av_demux *av_demux_new(int fmt)
{
	struct av_demux *d = (struct av_demux *) calloc(1, sizeof *d);

	if (d == NULL)
		return NULL;
	d->fmt = fmt;
	d->size = -1;
	d->ops = ops_of(fmt);
	if (d->ops != NULL) {
		d->priv = d->ops->create(d);
		if (d->priv == NULL) {
			free(d);
			return NULL;
		}
	}
	return d;
}

void av_demux_free(struct av_demux *d)
{
	if (d == NULL)
		return;
	if (d->ops != NULL)
		d->ops->destroy(d->priv);
	av__clear_tracks(d);
	free(d->buf);
	free(d);
}

static int start_probe(struct av_demux *d)
{
	if (d->ops != NULL)
		return AV_OK;
	if (d->len - d->off < 12 && !d->ended)
		return AV_AGAIN;
	d->fmt = av_probe(d->buf + d->off, d->len - d->off);
#ifdef AV_WITH_FFMPEG
	/* (FFmpeg's probe wants more bytes than ours: a few KB) */
	if (d->fmt == AV_FMT_UNKNOWN && d->len - d->off < 4096 && !d->ended)
		return AV_AGAIN;
#endif
	d->ops = ops_of(d->fmt);
	if (d->ops == NULL) {
		d->error = AV_EUNSUP;
		return AV_EUNSUP;
	}
	d->priv = d->ops->create(d);
	if (d->priv == NULL) {
		d->error = AV_ENOMEM;
		return AV_ENOMEM;
	}
	return AV_OK;
}

int av_demux_feed(struct av_demux *d, int64_t pos, const void *p, size_t n)
{
	const uint8_t *b = (const uint8_t *) p;
	int64_t end = av__end(d);

	d->ended = 0;
	if (pos == end)
		return win_append(d, b, n);
	if (pos == d->want && (pos < d->base + (int64_t) d->off || pos > end)) {
		/* where the parser asked to go (a seek, a box after an mdat): a window there */
		d->base = d->skip_from = pos;
		d->len = d->off = 0;
		return win_append(d, b, n);
	}
	if (d->len == d->off && pos >= d->skip_from && pos < d->base) {
		/* on the way to a place the parser skipped to: the bytes before it dropped */
		int64_t drop = d->base - pos;

		if ((int64_t) n <= drop) {
			d->skip_from = pos + (int64_t) n;
			return AV_OK;
		}
		return win_append(d, b + drop, n - (size_t) drop);
	}
	if (pos >= d->base && pos < end) {
		/* overlaps what is there: only the new part */
		if (pos + (int64_t) n <= end)
			return AV_OK;
		return win_append(d, b + (end - pos), n - (size_t) (end - pos));
	}
	/* elsewhere: a new window there, the parser told */
	d->base = d->skip_from = pos;
	d->len = d->off = 0;
	if (d->ops != NULL)
		d->ops->restart(d, d->priv, pos);
	return win_append(d, b, n);
}

int av_demux_append(struct av_demux *d, const void *p, size_t n)
{
	d->ended = 0;
	return win_append(d, (const uint8_t *) p, n);
}

void av_demux_end(struct av_demux *d)
{
	d->ended = 1;
}

int av_demux_read(struct av_demux *d, struct av_packet *pkt)
{
	int r;

	memset(pkt, 0, sizeof *pkt);
	if (d->error)
		return d->error;
	r = start_probe(d);
	if (r == AV_AGAIN) {
		d->want = av__end(d);
		return d->ended ? AV_EOF : AV_AGAIN;
	}
	if (r != AV_OK)
		return r;
	d->want = av__end(d);
	r = d->ops->read(d, d->priv, pkt);
	if (r == AV_AGAIN && d->ended && d->want >= av__end(d) && !av__demux_busy(d))
		r = AV_EOF;
	if (r < 0 && r != AV_EFULL)
		d->error = r;
	return r;
}

int64_t av_demux_want(struct av_demux *d)
{
	return d->want;
}

int av_demux_ntracks(const struct av_demux *d)
{
	return d->ntracks;
}

const struct av_track *av_demux_track(const struct av_demux *d, int i)
{
	return i >= 0 && i < d->ntracks ? &d->tracks[i] : NULL;
}

int av_demux_take_init(struct av_demux *d)
{
	int r = d->init_new;

	d->init_new = 0;
	return r;
}

av_us av_demux_duration(const struct av_demux *d)
{
	return d->duration;
}

int64_t av_demux_seek(struct av_demux *d, av_us t, av_us *at)
{
	int64_t r;

	if (at != NULL)
		*at = 0;
	if (d->ops == NULL || d->ops->seek == NULL)
		return -1;
	r = d->ops->seek(d, d->priv, t, at);
	if (r >= 0) {
		/* the window is kept if the place is in it, else it starts there */
		if (r < d->base + (int64_t) d->off || r > av__end(d)) {
			d->base = d->skip_from = r;
			d->len = d->off = 0;
		} else {
			d->off = (size_t) (r - d->base);
		}
		d->want = r;
		d->error = 0;
	}
	return r;
}

void av_demux_set_size(struct av_demux *d, int64_t size)
{
	d->size = size;
}

int av__demux_busy(struct av_demux *d)
{
	return d->ops != NULL && d->ops->busy != NULL && d->ops->busy(d, d->priv);
}

void av__demux_reset(struct av_demux *d)
{
	d->len = d->off = 0;
	d->error = 0;
	if (d->ops != NULL && d->ops->reset != NULL)
		d->ops->reset(d, d->priv);
}

void av_demux_reset(struct av_demux *d)
{
	av__demux_reset(d);
}

const char *av_demux_name(const struct av_demux *d)
{
	if (d->name[0])
		return d->name;
	return d->ops != NULL ? d->ops->name : "?";
}
