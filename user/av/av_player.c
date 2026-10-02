/*
 * user/av/av_player.c -- the player engine: a store's frames played.
 *
 *   - The audio thread decodes the audio track, resamples it to the output (rate, channels,
 *     volume, playback rate) and keeps the output's queue ~150 ms deep; it corrects the clock
 *     from what the output has played (the audio is the master clock when it is heard).
 *   - The video thread decodes the video track into a few 32-bit frames ahead (the host's
 *     layout), dropping frames already late, and frames before a seek's target unconverted.
 *   - The host's poll (its UI thread) reads the clock, takes the frame due now (the latest
 *     ready one whose time has come), works out the readyState, waiting / ended / seeked.
 *   - The file mode (av_player_open_file, the Media Player): a reader thread feeds the store
 *     from the file where its demuxer wants bytes, ~30 s ahead of the playback position.
 * Without a free sound output (another app has it) the clock is the wall clock and the audio is
 * not decoded. Threads poll with naps (2..10 ms): nothing here needs the kapi's events.
 */
#include <stdio.h>
#include <math.h>
#include "av_int.h"
#include "av_os.h"
#include "av_store.h"

#define NSLOT 4
#define AUDIO_AHEAD_US 150000	/* the output's queue kept this deep */
#define PCM_MAX 8192		/* resampled frames held before the output */

enum { SLOT_FREE = 0, SLOT_BUSY, SLOT_READY, SLOT_SHOWN };

struct vslot {
	int state;
	uint8_t *px;
	int w, h;
	av_us pts, dur;
	unsigned serial;
	unsigned seek;			/* the seek it was decoded after (older ones are not shown) */
};

struct av_player {
	struct av_store *store;
	int out_pix;
	const struct av_audio_out *ao;
	av_lock_t lock;
	int quit;

	/* the host's wishes */
	int playing;
	double rate;
	float volume;
	int muted;
	av_us seek_to;			/* AV_NOTIME: none pending */
	unsigned seek_serial;
	av_us duration_set;

	/* the clock */
	int clock_running;
	av_us clock_base, clock_wall;
	int seeking;			/* a seek not yet shown */
	av_us seek_target;
	int waiting, ended;

	/* the audio */
	av_thread_t ath;
	unsigned a_seek;		/* the seek serial the thread has handled */
	int a_src, a_track, has_audio;
	struct av_decoder *adec;
	struct av_resampler *rs;
	int rs_rate, rs_ch;
	av_us a_next;			/* the decode time of the last packet taken */
	int a_first;			/* the next packet: from a random access point */
	av_us a_trim;			/* samples before this dropped (a seek) */
	int a_starved, a_eof;
	int16_t *pcm;			/* resampled, not yet written */
	int pcm_n, pcm_off;
	av_us pcm_pts;			/* media time of pcm[pcm_off] */
	double pcm_step_us;		/* media us an output frame */
	av_us a_end;			/* media time of the end of what was written */
	int out_open, out_rate, sound;

	/* the video */
	av_thread_t vth;
	unsigned v_seek;
	int v_src, v_track, has_video;
	struct av_decoder *vdec;
	av_us v_next;
	int v_first;
	av_us v_skip;			/* frames before this not shown (a seek) */
	int v_starved, v_eof;
	struct vslot slot[NSLOT];
	unsigned serial;
	int width, height;
	unsigned decoded, dropped;
	av_us dec_sum;
	unsigned dec_n;
	int shown_since_seek;
	av_us last_end;			/* end of the last frame shown */
	av_us sync_sum;			/* |frame - audio heard| summed at each frame shown */
	unsigned sync_n;
	int error;

	/* the file mode */
	av_thread_t rth;
	av_file_t file;
	int file_src;
	int64_t file_size;
};

av_us av_now(void)
{
	return av_os_now_us();
}

/* ---- the kapi's sound output --------------------------------------------------------------- */

#ifndef AV_POSIX
static unsigned k_cap, k_lat;
static int k_open(void *c, int *rate)
{
	unsigned r = 0, f = 0, o = 0;
	int lat;
	(void) c;
	if (kapi_sound_acquire() != 1)
		return 0;
	lat = kapi_sound_config(1024, 3);
	k_lat = lat >= 0 ? (unsigned) lat : 4096;
	kapi_sound_status(&r, &f, &o);
	*rate = r ? (int) r : SOUND_RATE;
	k_cap = f;			/* (just acquired: the queue is empty) */
	return 1;
}
static unsigned k_write(void *c, const int16_t *p, unsigned n)
{
	int r;
	(void) c;
	r = kapi_sound_write((const short *) p, n);
	return r > 0 ? (unsigned) r : 0;
}
static unsigned k_queued(void *c)
{
	unsigned r = 0, f = 0, o = 0;
	(void) c;
	kapi_sound_status(&r, &f, &o);
	return (k_cap > f ? k_cap - f : 0) + k_lat;
}
static void k_close(void *c)
{
	(void) c;
	kapi_sound_release();
}
static const struct av_audio_out k_out = { 0, k_open, k_write, k_queued, k_close };
const struct av_audio_out *av_audio_kapi(void) { return &k_out; }
#else
static int n_open(void *c, int *rate) { (void) c; (void) rate; return 0; }
static unsigned n_write(void *c, const int16_t *p, unsigned n) { (void) c; (void) p; return n; }
static unsigned n_queued(void *c) { (void) c; return 0; }
static void n_close(void *c) { (void) c; }
static const struct av_audio_out n_out = { 0, n_open, n_write, n_queued, n_close };
const struct av_audio_out *av_audio_kapi(void) { return &n_out; }
#endif

/* ---- the clock (the player's lock held) ------------------------------------------------- */

static av_us clock_now(struct av_player *p)
{
	if (!p->clock_running)
		return p->clock_base;
	return p->clock_base + (av_us) ((double) (av_now() - p->clock_wall) * p->rate);
}

static void clock_set(struct av_player *p, av_us t, int running)
{
	p->clock_base = t;
	p->clock_wall = av_now();
	p->clock_running = running;
}

/* ---- the audio thread ------------------------------------------------------------------- */

static void audio_reset(struct av_player *p)
{
	if (p->adec != NULL)
		av_decoder_flush(p->adec);
	p->pcm_n = p->pcm_off = 0;
	p->a_starved = p->a_eof = 0;
	if (p->out_open) {
		/* the queue's old audio silenced at once */
		p->ao->close(p->ao->ctx);
		p->out_open = 0;
	}
}

/* (re)open the decoder for the store's audio track -> 1 there is one */
static int audio_setup(struct av_player *p)
{
	struct av_track info;
	const struct av_track *t;
	int src, tr;

	av__store_lock(p->store);
	if (!av__store_pick(p->store, AV_AUDIO, &src, &tr, &info)) {
		av__store_unlock(p->store);
		return 0;
	}
	if (p->adec != NULL && src == p->a_src && tr == p->a_track) {
		av__store_unlock(p->store);
		return 1;
	}
	t = av__store_track_info(p->store, src, tr);
	av_decoder_free(p->adec);
	p->adec = av_decoder_new(t);
	av__store_unlock(p->store);
	p->a_src = src;
	p->a_track = tr;
	av_lock(&p->lock);
	p->has_audio = 1;
	if (p->adec == NULL && !p->error)
		p->error = AV_EUNSUP;
	av_unlock(&p->lock);
	return p->adec != NULL;
}

/* decode the next packet into p->pcm -> AV_OK, AV_AGAIN, AV_EOF */
static int audio_fill(struct av_player *p, double rate, float vol)
{
	struct av_packet pk;
	struct av_frame f;
	int r;

	av__store_lock(p->store);
	if (p->a_first) {
		/* from the seek's time (less a pre-roll: audio frames all start decoding), once
		 * that time is buffered */
		av_us start = p->a_trim - 80000;
		r = av__store_ahead(p->store, p->a_trim) > 0 || av__store_ended(p->store, p->a_src) ?
			av__store_next(p->store, p->a_src, p->a_track, start > 0 ? start : 0, 1, 0, &pk) : AV_AGAIN;
	} else {
		r = av__store_next(p->store, p->a_src, p->a_track, p->a_next, 0, 500000, &pk);
	}
	av__store_unlock(p->store);
	if (r != AV_OK)
		return r;
	p->a_first = 0;
	p->a_next = pk.dts;
	if (av_decoder_send(p->adec, &pk) != AV_OK) {
		av_packet_free(&pk);
		return AV_OK;	/* (a bad packet: skipped) */
	}
	av_packet_free(&pk);
	while (av_decoder_receive(p->adec, &f) == AV_OK) {
		int skip = 0, n, cap;
		if (f.samples <= 0 || f.rate <= 0 || f.channels <= 0)
			continue;
		if (p->rs == NULL || p->rs_rate != f.rate || p->rs_ch != f.channels) {
			av_resampler_free(p->rs);
			p->rs = av_resampler_new(f.rate, f.channels, p->out_rate);
			p->rs_rate = f.rate;
			p->rs_ch = f.channels;
			if (p->rs == NULL)
				return AV_ENOMEM;
		}
		/* before the seek's target: dropped */
		if (f.pts + f.dur <= p->a_trim)
			continue;
		if (f.pts < p->a_trim)
			skip = (int) ((p->a_trim - f.pts) * f.rate / AV_US);
		if (skip >= f.samples)
			continue;
		if (p->pcm_off > 0) {
			memmove(p->pcm, p->pcm + 2 * p->pcm_off, (size_t) (p->pcm_n - p->pcm_off) * 4);
			p->pcm_n -= p->pcm_off;
			p->pcm_off = 0;
		}
		cap = PCM_MAX - p->pcm_n;
		if (p->pcm_n == 0)
			p->pcm_pts = f.pts + (av_us) skip * AV_US / f.rate;
		n = av_resample(p->rs, f.pcm + (size_t) skip * f.channels, f.samples - skip,
				p->pcm + 2 * p->pcm_n, cap, rate, vol);
		p->pcm_n += n;
		p->pcm_step_us = 1e6 / (double) p->out_rate * rate;
	}
	return AV_OK;
}

static int audio_main(void *arg)
{
	struct av_player *p = (struct av_player *) arg;

	p->pcm = (int16_t *) malloc(PCM_MAX * 4);
	if (p->pcm == NULL)
		return 1;
	while (!p->quit) {
		int playing, seeking, muted;
		unsigned seek;
		double rate;
		float vol;
		av_us target;

		av_lock(&p->lock);
		playing = p->playing && !p->ended;
		seek = p->seek_serial;
		target = p->seek_target;
		seeking = p->seeking;
		rate = p->rate;
		vol = p->volume;
		muted = p->muted;
		av_unlock(&p->lock);

		if (seek != p->a_seek) {
			audio_reset(p);
			p->a_first = 1;
			p->a_trim = target;
			p->a_end = target;
			p->a_seek = seek;
		}
		if (!audio_setup(p)) {
			av_sleep_ms(10);
			continue;
		}
		if (!playing || seeking || p->waiting) {
			if (p->out_open) {
				p->ao->close(p->ao->ctx);
				p->out_open = 0;
				/* what was not heard is decoded again from the clock's time at play */
				av_lock(&p->lock);
				p->a_first = 1;
				p->a_trim = clock_now(p);
				av_unlock(&p->lock);
				p->pcm_n = p->pcm_off = 0;
				if (p->adec != NULL)
					av_decoder_flush(p->adec);
			}
			av_sleep_ms(5);
			continue;
		}
		if (!p->out_open) {
			int rr = 0;
			if (p->ao->open(p->ao->ctx, &rr)) {
				p->out_open = 1;
				p->out_rate = rr > 0 ? rr : 44100;
				p->sound = 1;
				if (p->rs != NULL && p->rs_rate > 0) {
					av_resampler_free(p->rs);
					p->rs = NULL;
				}
				av_lock(&p->lock);
				p->a_end = clock_now(p);
				av_unlock(&p->lock);
			} else {
				/* no sound for us: the clock is the wall's */
				p->sound = 0;
				av_sleep_ms(50);
				continue;
			}
		}
		/* keep the output's queue AUDIO_AHEAD_US deep */
		{
			unsigned q = p->ao->queued(p->ao->ctx);
			av_us queued_us = (av_us) q * AV_US / p->out_rate;
			if (queued_us >= AUDIO_AHEAD_US) {
				/* the clock from what is heard: the end of the writes minus the queue */
				av_us heard = p->a_end - (av_us) ((double) queued_us * rate);
				av_lock(&p->lock);
				if (p->clock_running) {
					av_us c = clock_now(p);
					if (heard - c > 20000 || c - heard > 20000)
						clock_set(p, heard, 1);
				}
				av_unlock(&p->lock);
				av_sleep_ms(5);
				continue;
			}
		}
		if (p->pcm_off >= p->pcm_n) {
			int r = audio_fill(p, rate, muted ? 0.0f : vol);
			p->a_starved = r == AV_AGAIN;
			if (r == AV_EOF && !p->a_eof && p->rs != NULL && p->rs_ch > 0 && p->rs_ch <= 8) {
				/* the end: the resampler's last frames pushed out (it holds 3) */
				static const float zero[8 * 4];
				p->pcm_off = p->pcm_n = 0;
				p->pcm_n = av_resample(p->rs, zero, 4, p->pcm, PCM_MAX, rate, muted ? 0.0f : vol);
				p->a_eof = 1;
				if (p->pcm_n > 0)
					continue;
			}
			p->a_eof = r == AV_EOF;
			if (r != AV_OK) {
				av_sleep_ms(5);
				continue;
			}
		}
		if (p->pcm_off < p->pcm_n) {
			unsigned w = p->ao->write(p->ao->ctx, p->pcm + 2 * p->pcm_off, (unsigned) (p->pcm_n - p->pcm_off));
			p->pcm_off += (int) w;
			p->pcm_pts += (av_us) ((double) w * p->pcm_step_us);
			p->a_end = p->pcm_pts;
			if (w == 0)
				av_sleep_ms(3);
		}
	}
	if (p->out_open)
		p->ao->close(p->ao->ctx);
	p->out_open = 0;
	return 0;
}

/* ---- the video thread ------------------------------------------------------------------- */

static int video_setup(struct av_player *p)
{
	struct av_track info;
	const struct av_track *t;
	int src, tr;

	av__store_lock(p->store);
	if (!av__store_pick(p->store, AV_VIDEO, &src, &tr, &info)) {
		av__store_unlock(p->store);
		return 0;
	}
	if (p->vdec != NULL && src == p->v_src && tr == p->v_track) {
		av__store_unlock(p->store);
		return 1;
	}
	t = av__store_track_info(p->store, src, tr);
	av_decoder_free(p->vdec);
	p->vdec = av_decoder_new(t);
	av__store_unlock(p->store);
	p->v_src = src;
	p->v_track = tr;
	av_lock(&p->lock);
	p->has_video = 1;
	if (info.width > 0 && p->width == 0) {
		p->width = info.dwidth > 0 ? info.dwidth : info.width;
		p->height = info.dheight > 0 ? info.dheight : info.height;
	}
	if (p->vdec == NULL && !p->error)
		p->error = AV_EUNSUP;
	av_unlock(&p->lock);
	return p->vdec != NULL;
}

static int free_slot(struct av_player *p)
{
	int i;

	for (i = 0; i < NSLOT; i++)
		if (p->slot[i].state == SLOT_FREE)
			return i;
	return -1;
}

/* a decoded frame: shown later, or dropped -> 0, or -1 when the thread must stop (a seek) */
static int video_frame(struct av_player *p, const struct av_frame *f, unsigned seek)
{
	int s;
	av_us now;
	int first;

	av_lock(&p->lock);
	now = clock_now(p);
	first = !p->shown_since_seek && p->slot[0].state != SLOT_READY && p->slot[1].state != SLOT_READY &&
		p->slot[2].state != SLOT_READY && p->slot[3].state != SLOT_READY;
	if (f->pts + (f->dur > 0 ? f->dur : 1) <= p->v_skip) {
		av_unlock(&p->lock);
		return 0;		/* before the seek's target */
	}
	if (p->clock_running && !first && f->pts + (f->dur > 0 ? f->dur : 33333) < now) {
		p->dropped++;		/* late */
		av_unlock(&p->lock);
		return 0;
	}
	av_unlock(&p->lock);
	/* a free slot, waiting for one */
	for (;;) {
		av_lock(&p->lock);
		s = free_slot(p);
		if (s >= 0)
			p->slot[s].state = SLOT_BUSY;
		av_unlock(&p->lock);
		if (s >= 0)
			break;
		if (p->quit || p->seek_serial != seek)
			return -1;
		av_sleep_ms(2);
	}
	{
		struct vslot *v = &p->slot[s];
		if (v->px == NULL || v->w != f->width || v->h != f->height) {
			free(v->px);
			v->px = (uint8_t *) malloc((size_t) f->width * f->height * 4);
			v->w = f->width;
			v->h = f->height;
		}
		if (v->px == NULL) {
			av_lock(&p->lock);
			v->state = SLOT_FREE;
			av_unlock(&p->lock);
			return 0;
		}
		av_yuv_to_rgb(f, v->px, f->width * 4, p->out_pix);
		av_lock(&p->lock);
		v->pts = f->pts;
		v->dur = f->dur > 0 ? f->dur : 33333;
		v->serial = ++p->serial;
		v->seek = seek;
		v->state = SLOT_READY;
		if (p->width == 0 || p->width != f->width || p->height != f->height) {
			p->width = f->width;
			p->height = f->height;
		}
		av_unlock(&p->lock);
	}
	return 0;
}

static int video_main(void *arg)
{
	struct av_player *p = (struct av_player *) arg;

	while (!p->quit) {
		unsigned seek;
		av_us target;
		struct av_packet pk;
		struct av_frame f;
		int r, i;
		av_us t0;

		av_lock(&p->lock);
		seek = p->seek_serial;
		target = p->seek_target;
		av_unlock(&p->lock);
		if (seek != p->v_seek) {
			if (p->vdec != NULL)
				av_decoder_flush(p->vdec);
			av_lock(&p->lock);
			for (i = 0; i < NSLOT; i++)
				if (p->slot[i].state == SLOT_READY)
					p->slot[i].state = SLOT_FREE;
			p->shown_since_seek = 0;
			av_unlock(&p->lock);
			p->v_first = 1;
			p->v_skip = target;
			p->v_starved = p->v_eof = 0;
			p->v_seek = seek;
		}
		if (!video_setup(p)) {
			av_sleep_ms(10);
			continue;
		}
		av_lock(&p->lock);
		r = free_slot(p) >= 0;
		av_unlock(&p->lock);
		if (!r) {
			av_sleep_ms(3);
			continue;
		}
		av__store_lock(p->store);
		if (p->v_first) {
			/* from the random access point before the target, once the target is buffered */
			av_us rap = av__store_ahead(p->store, p->v_skip) > 0 || av__store_ended(p->store, p->v_src) ?
				av__store_rap_before(p->store, p->v_src, p->v_track, p->v_skip) : AV_NOTIME;
			r = rap == AV_NOTIME ? (av__store_ended(p->store, p->v_src) ? AV_EOF : AV_AGAIN) :
				av__store_next(p->store, p->v_src, p->v_track, rap, 1, 0, &pk);
		} else {
			r = av__store_next(p->store, p->v_src, p->v_track, p->v_next, 0, 500000, &pk);
		}
		av__store_unlock(p->store);
		if (r == AV_EOF && !p->v_eof) {
			/* the decoder's last frames */
			av_decoder_send(p->vdec, NULL);
			while (av_decoder_receive(p->vdec, &f) == AV_OK)
				if (video_frame(p, &f, seek) < 0)
					break;
		}
		p->v_starved = r == AV_AGAIN;
		p->v_eof = r == AV_EOF;
		if (r != AV_OK) {
			av_sleep_ms(5);
			continue;
		}
		p->v_first = 0;
		p->v_next = pk.dts;
		t0 = av_now();
		r = av_decoder_send(p->vdec, &pk);
		av_packet_free(&pk);
		if (r != AV_OK)
			continue;
		while (av_decoder_receive(p->vdec, &f) == AV_OK) {
			av_us dt = av_now() - t0;
			av_lock(&p->lock);
			p->decoded++;
			p->dec_sum += dt;
			p->dec_n++;
			av_unlock(&p->lock);
			if (video_frame(p, &f, seek) < 0)
				break;
			t0 = av_now();
		}
	}
	return 0;
}

/* ---- the file reader (the file mode) -------------------------------------------------- */

static int reader_main(void *arg)
{
	struct av_player *p = (struct av_player *) arg;
	uint8_t *buf = (uint8_t *) malloc(65536);

	if (buf == NULL)
		return 1;
	while (!p->quit) {
		int64_t want = av_store_want(p->store, p->file_src);
		av_us now, ahead;
		size_t n;

		if (want < 0) {
			av_sleep_ms(20);
			continue;
		}
		av_lock(&p->lock);
		now = clock_now(p);
		av_unlock(&p->lock);
		av__store_lock(p->store);
		av__store_set_now(p->store, now);
		ahead = av__store_ahead(p->store, now);
		av__store_unlock(p->store);
		if (ahead > 30 * AV_US) {
			av_sleep_ms(50);
			continue;
		}
		if (want >= p->file_size) {
			av_store_feed_end(p->store, p->file_src);
			continue;
		}
		{
			long got = av_file_read_at(p->file, want, buf, 65536);
			if (got <= 0) {
				av_store_feed_end(p->store, p->file_src);
				continue;
			}
			n = (size_t) got;
		}
		av_store_feed(p->store, p->file_src, want, buf, n);
	}
	free(buf);
	return 0;
}

int av_player_open_file(struct av_player *p, const char *path)
{
	uint8_t head[16];
	size_t n;
	const char *mime;

	p->file = av_file_open(path);
	if (p->file == NULL)
		return AV_ERR;
	p->file_size = av_file_size(p->file);
	{
		long got = av_file_read_at(p->file, 0, head, sizeof head);
		n = got > 0 ? (size_t) got : 0;
	}
	switch (av_probe(head, n)) {
	case AV_FMT_MKV: mime = "video/webm"; break;
	case AV_FMT_MP4: mime = "video/mp4"; break;
	case AV_FMT_WAV: mime = "audio/wav"; break;
	case AV_FMT_FLAC: mime = "audio/flac"; break;
	case AV_FMT_MP3: mime = "audio/mpeg"; break;
	default:
		av_file_close(p->file);
		p->file = NULL;
		return AV_EUNSUP;
	}
	p->file_src = av_store_add_source(p->store, mime, 64u << 20);
	if (p->file_src < 0) {
		av_file_close(p->file);
		p->file = NULL;
		return AV_ERR;
	}
	if (av_thread_start(&p->rth, reader_main, p, "av reader") < 0)
		return AV_ERR;
	return AV_OK;
}

/* ---- the host's calls ------------------------------------------------------------------- */

struct av_player *av_player_new(int out_pix, const struct av_audio_out *audio)
{
	struct av_player *p = (struct av_player *) calloc(1, sizeof *p);

	if (p == NULL)
		return NULL;
	p->store = av_store_new();
	if (p->store == NULL) {
		free(p);
		return NULL;
	}
	av_lock_init(&p->lock);
	p->out_pix = out_pix == AV_PIX_BGRA ? AV_PIX_BGRA : AV_PIX_RGBA;
	p->ao = audio != NULL ? audio : av_audio_kapi();
	p->rate = 1.0;
	p->volume = 1.0f;
	p->out_rate = 44100;
	p->seek_to = AV_NOTIME;
	p->a_first = p->v_first = 1;
	p->a_src = p->v_src = -1;
	p->file_src = -1;
	if (av_thread_start(&p->ath, audio_main, p, "av audio") < 0 ||
	    av_thread_start(&p->vth, video_main, p, "av video") < 0) {
		p->quit = 1;
		av_thread_join(&p->ath);
		av_store_free(p->store);
		free(p);
		return NULL;
	}
	return p;
}

void av_player_free(struct av_player *p)
{
	int i;

	if (p == NULL)
		return;
	p->quit = 1;
	av_thread_join(&p->ath);
	av_thread_join(&p->vth);
	if (p->file != NULL) {
		av_thread_join(&p->rth);
		av_file_close(p->file);
	}
	av_decoder_free(p->adec);
	av_decoder_free(p->vdec);
	av_resampler_free(p->rs);
	free(p->pcm);
	for (i = 0; i < NSLOT; i++)
		free(p->slot[i].px);
	av_store_free(p->store);
	free(p);
}

struct av_store *av_player_store(struct av_player *p)
{
	return p->store;
}

void av_player_play(struct av_player *p)
{
	av_lock(&p->lock);
	if (!p->playing) {
		p->playing = 1;
		if (p->ended) {
			/* play() at the end starts again from the beginning */
			p->ended = 0;
			p->seek_target = 0;
			p->seeking = 1;
			p->seek_serial++;
			clock_set(p, 0, 0);
		} else if (!p->seeking && !p->waiting) {
			clock_set(p, p->clock_base, 1);
		}
	}
	av_unlock(&p->lock);
}

void av_player_pause(struct av_player *p)
{
	av_lock(&p->lock);
	if (p->playing) {
		p->playing = 0;
		clock_set(p, clock_now(p), 0);
	}
	av_unlock(&p->lock);
}

void av_player_seek(struct av_player *p, av_us t)
{
	if (t < 0)
		t = 0;
	av_lock(&p->lock);
	p->seek_target = t;
	p->seeking = 1;
	p->ended = 0;
	p->waiting = 0;
	p->seek_serial++;
	/* what the threads knew before is stale until they see the seek */
	p->shown_since_seek = 0;
	p->v_eof = p->a_eof = 0;
	p->v_starved = p->a_starved = 0;
	clock_set(p, t, 0);
	av_unlock(&p->lock);
	/* the file mode: the demuxer sent there when t is not buffered */
	if (p->file_src >= 0 || (p->store != NULL && av__store_is_file(p->store, 0))) {
		av_us ahead;
		int src = p->file_src >= 0 ? p->file_src : 0;
		av__store_lock(p->store);
		ahead = av__store_ahead(p->store, t);
		av__store_set_now(p->store, t);
		av__store_unlock(p->store);
		if (ahead == 0)
			av__store_seek_file(p->store, src, t);
	}
}

void av_player_set_rate(struct av_player *p, double rate)
{
	if (rate <= 0.0625)
		rate = 0.0625;
	if (rate > 16)
		rate = 16;
	av_lock(&p->lock);
	if (p->clock_running)
		clock_set(p, clock_now(p), 1);
	p->rate = rate;
	av_unlock(&p->lock);
}

void av_player_set_volume(struct av_player *p, float volume, int muted)
{
	av_lock(&p->lock);
	p->volume = volume < 0 ? 0 : volume > 1 ? 1 : volume;
	p->muted = muted;
	av_unlock(&p->lock);
}

void av_player_set_duration(struct av_player *p, av_us d)
{
	av_lock(&p->lock);
	p->duration_set = d;
	av_unlock(&p->lock);
}

void av_player_kick(struct av_player *p)
{
	(void) p;	/* (the threads poll the store; nothing to wake) */
}

int av_player_poll(struct av_player *p, struct av_player_status *st, struct av_video_out *v)
{
	int i, best = -1, shown = -1, got = 0;
	av_us now, ahead, dur, end;
	int has_init, ended_store;

	memset(st, 0, sizeof *st);
	st->want = -1;
	/* the store's view (before the player's lock: never both held) */
	av__store_lock(p->store);
	{
		int s, t;
		struct av_track info;
		has_init = av__store_pick(p->store, AV_VIDEO, &s, &t, &info) ||
			av__store_pick(p->store, AV_AUDIO, &s, &t, &info);
		ended_store = 1;
		if (av__store_pick(p->store, AV_VIDEO, &s, &t, &info) && !av__store_ended(p->store, s))
			ended_store = 0;
		if (av__store_pick(p->store, AV_AUDIO, &s, &t, &info) && !av__store_ended(p->store, s))
			ended_store = 0;
	}
	av_lock(&p->lock);
	now = clock_now(p);
	av_unlock(&p->lock);
	av__store_set_now(p->store, now);
	ahead = av__store_ahead(p->store, now);
	if (p->file_src >= 0 || av__store_is_file(p->store, 0))
		av__store_trim(p->store, now - 10 * AV_US);
	av__store_unlock(p->store);
	dur = p->duration_set > 0 ? p->duration_set : av_store_duration(p->store);
	if (p->file_src < 0 && av__store_is_file(p->store, 0))
		st->want = av_store_want(p->store, 0);
	end = dur > 0 ? dur : (ended_store ? av_store_end(p->store) : 0);

	av_lock(&p->lock);
	now = clock_now(p);
	/* the frame due: the latest ready one whose time has come; when the clock does not run
	 * (paused, a seek, waiting) the first one there */
	for (i = 0; i < NSLOT; i++) {
		struct vslot *s = &p->slot[i];
		if (s->state == SLOT_SHOWN)
			shown = i;
		if (s->state != SLOT_READY)
			continue;
		if (s->seek != p->seek_serial) {
			s->state = SLOT_FREE;	/* (decoded before a seek) */
			continue;
		}
		if (p->clock_running) {
			if (s->pts <= now + 5000 && (best < 0 || s->pts > p->slot[best].pts))
				best = i;
		} else if (!p->shown_since_seek) {
			if (best < 0 || s->pts < p->slot[best].pts)
				best = i;
		}
	}
	if (best >= 0) {
		/* the ready ones before it were never shown: dropped */
		for (i = 0; i < NSLOT; i++)
			if (i != best && p->slot[i].state == SLOT_READY && p->slot[i].pts < p->slot[best].pts) {
				p->slot[i].state = SLOT_FREE;
				p->dropped++;
			}
		if (shown >= 0)
			p->slot[shown].state = SLOT_FREE;
		p->slot[best].state = SLOT_SHOWN;
		p->shown_since_seek = 1;
		p->last_end = p->slot[best].pts + p->slot[best].dur;
		if (p->clock_running && p->sound && p->out_open && p->has_audio) {
			/* the audio heard now (what was written less the output's queue) */
			av_us q = (av_us) p->ao->queued(p->ao->ctx) * AV_US / p->out_rate;
			av_us heard = p->a_end - (av_us) ((double) q * p->rate);
			av_us d = p->slot[best].pts - heard;
			p->sync_sum += d < 0 ? -d : d;
			p->sync_n++;
		}
		v->pixels = p->slot[best].px;
		v->width = p->slot[best].w;
		v->height = p->slot[best].h;
		v->stride = p->slot[best].w * 4;
		v->pts = p->slot[best].pts;
		v->serial = p->slot[best].serial;
		got = 1;
	}
	/* a seek is done when its first frame is there (or the audio's, without video) */
	if (p->seeking) {
		int done = p->has_video ? p->shown_since_seek || p->v_eof : has_init;
		if (done && (ahead > 0 || ended_store || p->v_eof)) {
			p->seeking = 0;
			clock_set(p, p->seek_target, p->playing && !p->ended);
		}
	}
	/* waiting: playing, the clock past what is decoded, and nothing more in the store */
	if (p->playing && !p->seeking && !p->ended) {
		int starved = (p->has_video && p->v_starved && p->last_end + 100000 < now) ||
			(p->has_audio && p->sound && p->a_starved && p->a_end < now);
		if (starved && !p->waiting && ahead < 100000 && !ended_store) {
			p->waiting = 1;
			clock_set(p, now, 0);
		} else if (p->waiting && (ahead >= 500000 || ended_store)) {
			p->waiting = 0;
			clock_set(p, p->clock_base, 1);
		}
	}
	/* the end */
	if (p->playing && !p->seeking && !p->ended && ended_store && end > 0 && now >= end - 20000 &&
	    (!p->has_video || p->v_eof) && (!p->has_audio || !p->sound || p->a_eof)) {
		p->ended = 1;
		p->playing = 0;
		clock_set(p, end, 0);
		now = end;
	}
	st->time = now;
	st->duration = dur > 0 ? dur : (ended_store ? end : 0);
	st->paused = !p->playing;
	st->ended = p->ended;
	st->seeking = p->seeking;
	st->waiting = p->waiting;
	st->width = p->width;
	st->height = p->height;
	st->has_audio = p->has_audio;
	st->has_video = p->has_video;
	st->sound = p->sound;
	st->decoded = p->decoded;
	st->dropped = p->dropped;
	st->decode_us = p->dec_n ? p->dec_sum / p->dec_n : 0;
	st->sync_us = p->sync_n ? p->sync_sum / p->sync_n : 0;
	st->error = p->error;
	if (!has_init)
		st->ready = AV_HAVE_NOTHING;
	else if (p->seeking || (p->has_video && !p->shown_since_seek && !got))
		st->ready = AV_HAVE_METADATA;
	else if (ahead >= 3 * AV_US || ended_store)
		st->ready = AV_HAVE_ENOUGH_DATA;
	else if (ahead >= 100000)
		st->ready = AV_HAVE_FUTURE_DATA;
	else
		st->ready = AV_HAVE_CURRENT_DATA;
	if (p->ended)
		st->ready = AV_HAVE_ENOUGH_DATA;
	av_unlock(&p->lock);
	return got;
}
