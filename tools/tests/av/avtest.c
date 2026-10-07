/*
 * tools/tests/av/avtest.c -- the media library (user/Libs/av) on its own: containers, decoders,
 * the store (MSE), conversions, the player, against the clips tools/tests/av/mkmedia.py makes.
 * Built with AV_POSIX (pthreads) for the PC, and for AArch64 Linux (run under qemu-aarch64:
 * the NEON paths checked against the C ones). tools/tests/av/run.sh builds and runs it.
 *
 *   avtest <media dir>     -> "ok ..." / "FAIL ..." lines, exit status 0 if all pass
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "av.h"
#include "av_os.h"

static int fails, oks;
#define CHECK(c, ...) do { if (c) { oks++; printf("ok    "); } else { fails++; printf("FAIL  "); } \
	printf(__VA_ARGS__); printf("\n"); } while (0)

static uint8_t *load(const char *dir, const char *name, size_t *n)
{
	char path[512];
	FILE *f;
	uint8_t *b;
	long len;

	snprintf(path, sizeof path, "%s/%s", dir, name);
	f = fopen(path, "rb");
	if (f == NULL) {
		printf("FAIL  cannot open %s\n", path);
		fails++;
		*n = 0;
		return NULL;
	}
	fseek(f, 0, SEEK_END);
	len = ftell(f);
	fseek(f, 0, SEEK_SET);
	b = malloc((size_t) len + 1);
	*n = fread(b, 1, (size_t) len, f);
	fclose(f);
	return b;
}

/* frames.txt: the sum of each frame's I420 bytes */
static long long frame_sum[256];
static int nframes;
static void load_frames(const char *dir)
{
	char path[512], line[128];
	FILE *f;

	snprintf(path, sizeof path, "%s/frames.txt", dir);
	f = fopen(path, "r");
	if (f == NULL)
		return;
	while (fgets(line, sizeof line, f) && nframes < 256) {
		int i;
		long long pts, sum;
		if (sscanf(line, "%d %lld %lld", &i, &pts, &sum) == 3)
			frame_sum[nframes++] = sum;
	}
	fclose(f);
}

static unsigned rnd_state = 12345;
static unsigned rnd(void) { rnd_state = rnd_state * 1103515245 + 12345; return (rnd_state >> 8) & 0xFFFFFF; }

struct dstats { int vpk, apk, vok, keys; av_us last_vpts, first_apts; long long asamples; int order_ok; };

/* demux a whole buffer fed in chunks of random sizes (chunk 0: all at once), following want */
static int demux_all(const uint8_t *b, size_t n, size_t chunk, struct dstats *st, int decode)
{
	struct av_demux *d = av_demux_new(AV_FMT_UNKNOWN);
	struct av_decoder *dec[8] = { 0 };
	int64_t pos = 0;
	int r, i, guard = 0;

	memset(st, 0, sizeof *st);
	st->order_ok = 1;
	st->last_vpts = -1;
	st->first_apts = AV_NOTIME;
	for (;;) {
		struct av_packet p;
		r = av_demux_read(d, &p);
		if (r == AV_OK) {
			const struct av_track *t = av_demux_track(d, p.track);
			if (t->kind == AV_VIDEO) {
				if (p.pts <= st->last_vpts)
					st->order_ok = 0;
				st->last_vpts = p.pts;
				st->keys += p.key;
				if (decode) {
					struct av_frame f;
					if (dec[p.track] == NULL)
						dec[p.track] = av_decoder_new(t);
					if (dec[p.track] && av_decoder_send(dec[p.track], &p) == AV_OK &&
					    av_decoder_receive(dec[p.track], &f) == AV_OK) {
						long long s = 0;
						int y, x, cw = (f.width + 1) / 2, ch = (f.height + 1) / 2;
						for (y = 0; y < f.height; y++)
							for (x = 0; x < f.width; x++)
								s += f.plane[0][y * f.stride[0] + x];
						for (y = 0; y < ch; y++)
							for (x = 0; x < cw; x++)
								s += f.plane[1][y * f.stride[1] + x] + f.plane[2][y * f.stride[2] + x];
						if (st->vpk < nframes && s == frame_sum[st->vpk])
							st->vok++;
					}
				}
				st->vpk++;
			} else if (t->kind == AV_AUDIO) {
				if (st->first_apts == AV_NOTIME)
					st->first_apts = p.pts;
				st->apk++;
				if (decode) {
					struct av_frame f;
					if (dec[p.track] == NULL)
						dec[p.track] = av_decoder_new(t);
					if (dec[p.track] && av_decoder_send(dec[p.track], &p) == AV_OK)
						while (av_decoder_receive(dec[p.track], &f) == AV_OK)
							st->asamples += f.samples;
				}
			}
			av_pkt_free(&p);
			continue;
		}
		if (r == AV_AGAIN) {
			int64_t w = av_demux_want(d);
			size_t len;
			if (w >= (int64_t) n) {
				av_demux_end(d);
				if (++guard > 3)
					break;
				continue;
			}
			if (w != pos)
				pos = w;
			len = chunk ? 1 + rnd() % chunk : n;
			if (pos + (int64_t) len > (int64_t) n)
				len = n - (size_t) pos;
			av_demux_feed(d, pos, b + pos, len);
			pos += len;
			continue;
		}
		break;
	}
	for (i = 0; i < 8; i++)
		av_decoder_free(dec[i]);
	av_demux_free(d);
	return r;
}

static void test_containers(const char *dir)
{
	static const char *files[] = { "clip.webm", "clip.mp4", "clip-moovend.mp4", "frag.mp4", "mse.webm" };
	size_t k;

	for (k = 0; k < sizeof files / sizeof files[0]; k++) {
		size_t n;
		uint8_t *b = load(dir, files[k], &n);
		struct dstats a, c;
		int r;
		if (b == NULL)
			continue;
		r = demux_all(b, n, 0, &a, 1);
		CHECK(r == AV_EOF && a.vpk == 50 && a.vok == 50 && a.order_ok,
			"%s: whole: %d video packets (%d match the frames), %d audio, end %d", files[k], a.vpk, a.vok, a.apk, r);
		CHECK(a.keys == 4, "%s: 4 random access points (%d)", files[k], a.keys);
		CHECK(a.asamples == 96000, "%s: audio 96000 frames decoded (%lld)", files[k], a.asamples);
		r = demux_all(b, n, 3000, &c, 1);
		CHECK(c.vpk == a.vpk && c.apk == a.apk && c.vok == 50 && c.asamples == a.asamples,
			"%s: fed in random pieces (<= 3000 bytes): the same packets (%d %d)", files[k], c.vpk, c.apk);
		r = demux_all(b, n, 7, &c, 0);
		CHECK(c.vpk == a.vpk && c.apk == a.apk, "%s: fed in pieces of <= 7 bytes: the same packets", files[k]);
		free(b);
	}
}

/* seeking in a file: feed from want, seek, check the first video packet */
static void test_seek(const char *dir, const char *name)
{
	size_t n;
	uint8_t *b = load(dir, name, &n);
	struct av_demux *d;
	int64_t pos = 0;
	av_us at = 0;
	int step = 0, got = 0;
	av_us first = AV_NOTIME;

	if (b == NULL)
		return;
	d = av_demux_new(AV_FMT_UNKNOWN);
	/* read the start (the tracks), then seek to 1.3 s */
	for (;;) {
		struct av_packet p;
		int r = av_demux_read(d, &p);
		if (r == AV_OK) {
			av_pkt_free(&p);
			if (++got == 5)
				break;
			continue;
		}
		if (r != AV_AGAIN)
			break;
		pos = av_demux_want(d);
		av_demux_feed(d, pos, b + pos, pos + 4096 > (int64_t) n ? n - (size_t) pos : 4096);
	}
	pos = av_demux_seek(d, 1300000, &at);
	for (step = 0; step < 2000; step++) {
		struct av_packet p;
		int r = av_demux_read(d, &p);
		if (r == AV_OK) {
			const struct av_track *t = av_demux_track(d, p.track);
			if (t->kind == AV_VIDEO) {
				first = p.pts;
				CHECK(p.key, "%s: seek 1.3 s: lands on a random access point", name);
				av_pkt_free(&p);
				break;
			}
			av_pkt_free(&p);
			continue;
		}
		if (r != AV_AGAIN)
			break;
		pos = av_demux_want(d);
		if (pos >= (int64_t) n) {
			av_demux_end(d);
			continue;
		}
		av_demux_feed(d, pos, b + pos, pos + 4096 > (int64_t) n ? n - (size_t) pos : 4096);
	}
	CHECK(first != AV_NOTIME && first <= 1300000 && first >= 900000,
		"%s: after the seek, the first video frame at %.3f s (<= 1.3, the 1 s key)", name, first / 1e6);
	av_demux_free(d);
	free(b);
}

static void test_flac(const char *dir)
{
	size_t n, rn;
	uint8_t *b = load(dir, "tone.flac", &n);
	uint8_t *ref = load(dir, "tone-ref.raw", &rn);
	struct av_demux *d;
	struct av_decoder *dec = NULL;
	long long k = 0, bad = 0, frames = 0;
	av_us t0;

	if (b == NULL || ref == NULL)
		return;
	d = av_demux_new(AV_FMT_UNKNOWN);
	av_demux_feed(d, 0, b, n);
	av_demux_end(d);
	t0 = av_now();
	for (;;) {
		struct av_packet p;
		struct av_frame f;
		int i;
		if (av_demux_read(d, &p) != AV_OK)
			break;
		if (dec == NULL)
			dec = av_decoder_new(av_demux_track(d, 0));
		frames++;
		if (dec == NULL || av_decoder_send(dec, &p) != AV_OK || av_decoder_receive(dec, &f) != AV_OK) {
			bad += 4096;
			av_pkt_free(&p);
			continue;
		}
		for (i = 0; i < f.samples * f.channels; i++, k++) {
			int16_t want = (int16_t) (ref[2 * k] | ref[2 * k + 1] << 8);
			int got = (int) lrintf(f.pcm[i] * 32768.0f);
			if (2 * k + 1 >= (long long) rn || got != want)
				bad++;
		}
		av_pkt_free(&p);
	}
	CHECK(bad == 0 && k * 2 == (long long) rn,
		"tone.flac: %lld frames, %lld samples bit-exact with the reference (%lld wrong) in %.1f ms",
		frames, k, bad, (av_now() - t0) / 1000.0);
	av_decoder_free(dec);
	av_demux_free(d);
	free(b);
	free(ref);
}

static void test_mp3(const char *dir)
{
	size_t n;
	uint8_t *b = load(dir, "silence.mp3", &n);
	struct dstats a, c;
	struct av_demux *d;

	if (b == NULL)
		return;
	demux_all(b, n, 0, &a, 1);
	CHECK(a.apk == 51 && a.asamples >= 50 * 1152, "silence.mp3: 51 frames (the Info frame too), %lld samples decoded (minimp3)", a.asamples);
	demux_all(b, n, 100, &c, 1);
	CHECK(c.apk == a.apk && c.asamples == a.asamples, "silence.mp3: fed in pieces of <= 100 bytes: the same");
	d = av_demux_new(AV_FMT_UNKNOWN);
	av_demux_feed(d, 0, b, n);
	{ struct av_packet p; if (av_demux_read(d, &p) == AV_OK) av_pkt_free(&p); }
	CHECK(!strcmp(av_demux_name(d), "mp3") && av_demux_duration(d) > 1300000 && av_demux_duration(d) < 1310000,
		"silence.mp3: probed as mp3, duration %.3f s from the Info frame", av_demux_duration(d) / 1e6);
	av_demux_free(d);
	CHECK(av_type_supported("audio/mpeg", 0, NULL) == 1 && av_type_supported("audio/mpeg; codecs=\"mp3\"", 1, NULL) == 2,
		"audio/mpeg: canPlayType maybe, MSE probably");
	free(b);
}

static void test_wav(const char *dir)
{
	size_t n;
	uint8_t *b = load(dir, "tone.wav", &n);
	struct dstats a;

	if (b == NULL)
		return;
	demux_all(b, n, 0, &a, 1);
	CHECK(a.asamples == 22050 && a.first_apts == 0, "tone.wav: 22050 frames decoded");
	free(b);
}

/* MSE: segments appended, buffered ranges, remove, out of order */
static int ranges_str(struct av_store *s, int src, char *out, size_t max)
{
	struct av_range r[8];
	int n = av_store_buffered(s, src, r, 8), i;
	size_t o = 0;

	out[0] = 0;
	for (i = 0; i < n; i++)
		o += (size_t) snprintf(out + o, max - o, "%s[%.2f,%.2f]", i ? " " : "", r[i].start / 1e6, r[i].end / 1e6);
	return n;
}

static void test_mse(const char *dir, const char *name, const char *json, const char *mime)
{
	size_t n, jn;
	uint8_t *b = load(dir, name, &n);
	char *j = (char *) load(dir, json, &jn);
	long long rg[16][2];
	int nr = 0, i, src;
	char *q;
	char buf[256];
	struct av_store *s;

	if (b == NULL || j == NULL)
		return;
	j[jn] = 0;
	for (q = j; (q = strchr(q, '[')) != NULL && nr < 16; ) {
		long long a, e;
		double t;
		q++;
		if (*q == '[')
			continue;
		if (sscanf(q, "%lld, %lld, %lf", &a, &e, &t) == 3) {
			rg[nr][0] = a;
			rg[nr][1] = e;
			nr++;
		}
	}
	s = av_store_new();
	src = av_store_add_source(s, mime, 0);
	CHECK(src >= 0, "%s: a source for %s", name, mime);
	/* the init segment in two pieces */
	av_store_append(s, src, b, (size_t) (rg[0][1] / 2));
	av_store_append(s, src, b + rg[0][1] / 2, (size_t) (rg[0][1] - rg[0][1] / 2));
	CHECK(av_store_has_init(s, src) && av_store_ntracks(s, src) == 2, "%s: init segment: 2 tracks", name);
	/* segments 2 and 3 first (out of order), then 0 and 1 */
	for (i = 3; i <= 4 && i < nr; i++)
		av_store_append(s, src, b + rg[i][0], (size_t) (rg[i][1] - rg[i][0]));
	ranges_str(s, src, buf, sizeof buf);
	CHECK(!strcmp(buf, "[1.00,2.00]"), "%s: segments 1-2 s appended: %s", name, buf);
	for (i = 1; i <= 2; i++) {
		/* in odd pieces */
		long long a = rg[i][0];
		while (a < rg[i][1]) {
			long long e = a + 1 + rnd() % 5000;
			if (e > rg[i][1])
				e = rg[i][1];
			av_store_append(s, src, b + a, (size_t) (e - a));
			a = e;
		}
	}
	ranges_str(s, src, buf, sizeof buf);
	CHECK(!strcmp(buf, "[0.00,2.00]"), "%s: then 0-1 s: %s", name, buf);
	av_store_remove(s, src, 500000, 1000000);
	ranges_str(s, src, buf, sizeof buf);
	CHECK(!strcmp(buf, "[0.00,0.50] [1.00,2.00]"), "%s: remove(0.5, 1): %s", name, buf);
	av_store_append(s, src, b + rg[2][0], (size_t) (rg[2][1] - rg[2][0]));
	ranges_str(s, src, buf, sizeof buf);
	CHECK(!strcmp(buf, "[0.00,2.00]"), "%s: 0.5-1 s appended again: %s", name, buf);
	/* the same segment again: overlapping frames replaced, not doubled */
	av_store_append(s, src, b + rg[2][0], (size_t) (rg[2][1] - rg[2][0]));
	ranges_str(s, src, buf, sizeof buf);
	CHECK(!strcmp(buf, "[0.00,2.00]"), "%s: appended twice: %s", name, buf);
	/* timestampOffset +10 s */
	av_store_set_offset(s, src, 10 * AV_US);
	av_store_append(s, src, b + rg[1][0], (size_t) (rg[1][1] - rg[1][0]));
	ranges_str(s, src, buf, sizeof buf);
	CHECK(!strcmp(buf, "[0.00,2.00] [10.00,10.50]"), "%s: timestampOffset 10: %s", name, buf);
	av_store_free(s);
	free(b);
	free(j);
}

static void test_yuv(void)
{
	int w = 854, h = 480, cw = (w + 1) / 2, ch = (h + 1) / 2, i, diff = 0, cs;
	uint8_t *y = malloc((size_t) w * h), *u = malloc((size_t) cw * ch), *v = malloc((size_t) cw * ch);
	uint8_t *o1 = malloc((size_t) w * h * 4), *o2 = malloc((size_t) w * h * 4);
	struct av_frame f;
	av_us t0, t1;
	extern void av__yuv_to_rgb_c(const struct av_frame *f, uint8_t *dst, int dst_stride, int pix);

	for (i = 0; i < w * h; i++) y[i] = (uint8_t) rnd();
	for (i = 0; i < cw * ch; i++) { u[i] = (uint8_t) rnd(); v[i] = (uint8_t) rnd(); }
	memset(&f, 0, sizeof f);
	f.pix = AV_PIX_I420;
	f.width = w;
	f.height = h;
	f.plane[0] = y; f.plane[1] = u; f.plane[2] = v;
	f.stride[0] = w; f.stride[1] = f.stride[2] = cw;
	for (cs = 0; cs < 4; cs++) {
		f.colorspace = cs & 1 ? AV_CS_BT709 : AV_CS_BT601;
		f.full_range = cs >> 1;
		av_yuv_to_rgb(&f, o1, w * 4, cs == 3 ? AV_PIX_BGRA : AV_PIX_RGBA);
		av__yuv_to_rgb_c(&f, o2, w * 4, cs == 3 ? AV_PIX_BGRA : AV_PIX_RGBA);
		for (i = 0; i < w * h * 4; i++)
			diff += o1[i] != o2[i];
	}
	CHECK(diff == 0, "yuv: the fast path and the C path agree (4 colour setups, %d bytes differ)", diff);
	/* white / black / grey */
	y[0] = 235; u[0] = v[0] = 128; f.colorspace = AV_CS_BT601; f.full_range = 0;
	y[1] = 16; y[w] = 126; y[w + 1] = 126;
	av__yuv_to_rgb_c(&f, o2, w * 4, AV_PIX_RGBA);
	CHECK(o2[0] == 255 && o2[1] == 255 && o2[2] == 255 && o2[4] == 0 && o2[5] == 0 && o2[6] == 0,
		"yuv: Y 235 = white (%d %d %d), Y 16 = black (%d %d %d)", o2[0], o2[1], o2[2], o2[4], o2[5], o2[6]);
	t0 = av_now();
	for (i = 0; i < 20; i++)
		av_yuv_to_rgb(&f, o1, w * 4, AV_PIX_RGBA);
	t1 = av_now();
	printf("time  yuv -> rgb 854x480: %.2f ms a frame\n", (t1 - t0) / 20.0 / 1000.0);
	free(y); free(u); free(v); free(o1); free(o2);
}

static void test_types(void)
{
	static const struct { const char *t; int mse; int want; } T[] = {
		{ "video/webm; codecs=\"vp9\"", 1, -1 }, { "video/mp4; codecs=\"avc1.42E01E\"", 1, -1 },
		{ "audio/webm; codecs=\"opus\"", 1, -1 }, { "audio/webm; codecs=\"pcm\"", 1, 2 },
		{ "video/webm; codecs=\"i420, pcm\"", 1, 2 }, { "video/mp4; codecs=\"i420\"", 1, 2 },
		{ "audio/flac", 0, 1 }, { "audio/flac", 1, 0 }, { "audio/wav", 0, 1 },
		{ "audio/mp4; codecs=\"flac\"", 1, 2 }, { "video/x-unknown", 0, 0 },
		{ "audio/webm; codecs=\"vorbis\"", 0, -1 }, { "video/webm; codecs=\"vp09.02.10.10\"", 1, 0 },
		{ "audio/webm; codecs=\"i420\"", 1, 0 },
	};
	size_t i;

	for (i = 0; i < sizeof T / sizeof T[0]; i++) {
		int r = av_type_supported(T[i].t, T[i].mse, NULL);
		if (T[i].want >= 0)
			CHECK(r == T[i].want, "type %s%s -> %d", T[i].mse ? "(mse) " : "", T[i].t, r);
		else
			printf("info  type %s%s -> %d (this build's codecs)\n", T[i].mse ? "(mse) " : "", T[i].t, r);
	}
}

/* ---- the player, with an output consuming in real time --------------------------------- */

struct fake_out { av_us start; long long written; int open; av_lock_t l; };
static struct fake_out fo;
static int fo_open(void *c, int *rate) { (void) c; *rate = 44100; fo.start = av_now(); fo.written = 0; fo.open = 1; return 1; }
static long long fo_played(void) { return (long long) ((av_now() - fo.start) * 44100 / AV_US); }
static unsigned fo_write(void *c, const int16_t *p, unsigned n)
{
	long long q = fo.written - fo_played();
	(void) c; (void) p;
	if (q < 0) { fo.written = fo_played(); q = 0; }	/* (an underrun: silence was played) */
	if (q + n > 22050) n = q >= 22050 ? 0 : (unsigned) (22050 - q);
	fo.written += n;
	return n;
}
static unsigned fo_queued(void *c) { long long q = fo.written - fo_played(); (void) c; return q > 0 ? (unsigned) q + 2048 : 2048; }
static void fo_close(void *c) { (void) c; fo.open = 0; }
static const struct av_audio_out fake = { 0, fo_open, fo_write, fo_queued, fo_close };

static void test_player(const char *dir, const char *name, const char *json, const char *mime)
{
	size_t n, jn;
	uint8_t *b = load(dir, name, &n);
	char *j = (char *) load(dir, json, &jn);
	struct av_player *p;
	struct av_store *s;
	int src, shown = 0, mono = 1, ended = 0, k, seeked = 0;
	av_us last = -1, t0, sync_sum = 0;
	int sync_n = 0;
	struct av_player_status st;
	(void) j;

	if (b == NULL)
		return;
	p = av_player_new(AV_PIX_RGBA, &fake);
	s = av_player_store(p);
	src = av_store_add_source(s, mime, 0);
	av_store_append(s, src, b, n);
	av_store_set_eos(s, 1);
	av_player_set_duration(p, 2 * AV_US);
	/* the first frame shows before play */
	for (k = 0; k < 100; k++) {
		struct av_video_out v;
		if (av_player_poll(p, &st, &v)) {
			CHECK(v.pts == 0 && v.width == 96 && v.height == 64, "%s: player: the first frame before play (%dx%d at %lld)",
				name, v.width, v.height, (long long) v.pts);
			break;
		}
		av_sleep_ms(5);
	}
	CHECK(st.ready >= AV_HAVE_CURRENT_DATA, "%s: player: readyState %d after the first frame", name, st.ready);
	av_player_play(p);
	t0 = av_now();
	while (av_now() - t0 < 3 * AV_US) {
		struct av_video_out v;
		if (av_player_poll(p, &st, &v)) {
			if (v.pts <= last)
				mono = 0;
			last = v.pts;
			shown++;
			if (st.sound && fo.open) {
				/* the audio heard now vs the frame shown now (the output's latency: 2048) */
				av_us heard = (av_us) (fo_played() - 2048) * AV_US / 44100;
				sync_sum += llabs(v.pts - heard);
				sync_n++;
			}
		}
		if (st.ended) {
			ended = 1;
			break;
		}
		av_sleep_ms(4);
	}
	CHECK(ended && shown >= 45 && mono, "%s: player: played to the end in %.2f s: %d frames shown in order (decoded %u, dropped %u)",
		name, (av_now() - t0) / 1e6, shown, st.decoded, st.dropped);
	CHECK(st.time >= 1990000 && st.time <= 2010000, "%s: player: ended at %.3f s", name, st.time / 1e6);
	CHECK(sync_n > 0 && sync_sum / sync_n < 60000, "%s: player: A/V sync, mean |video - audio| %.1f ms (%d frames)",
		name, sync_n ? sync_sum / sync_n / 1000.0 : -1.0, sync_n);
	/* seek back to 1.1 s and play the rest */
	av_player_seek(p, 1100000);
	av_player_play(p);
	t0 = av_now();
	shown = 0;
	last = -1;
	while (av_now() - t0 < 2 * AV_US) {
		struct av_video_out v;
		if (av_player_poll(p, &st, &v)) {
			if (last < 0)
				CHECK(v.pts >= 1040000 && v.pts <= 1120000, "%s: player: seek 1.1 s: first frame at %.3f s", name, v.pts / 1e6);
			last = v.pts;
			shown++;
		}
		if (!st.seeking)
			seeked = 1;
		if (st.ended)
			break;
		av_sleep_ms(4);
	}
	CHECK(seeked && st.ended && shown >= 20, "%s: player: after the seek, played to the end (%d frames)", name, shown);
	av_player_free(p);
	free(b);
	free(j);
}

/* the file mode (the Media Player's): a file opened by path, its reader thread, played to the end, a seek */
static void test_player_file(const char *dir, const char *name)
{
	char path[512];
	struct av_player *p;
	struct av_player_status st;
	int shown = 0, mono = 1, r;
	av_us last = -1, t0, half;

	snprintf(path, sizeof path, "%s/%s", dir, name);
	p = av_player_new(AV_PIX_BGRA, &fake);
	r = av_player_open_file(p, path);
	CHECK(r == AV_OK, "%s: file mode: opened (%d)", name, r);
	if (r != AV_OK) {
		av_player_free(p);
		return;
	}
	av_player_play(p);
	t0 = av_now();
	memset(&st, 0, sizeof st);
	while (av_now() - t0 < 6 * AV_US) {
		struct av_video_out v;
		if (av_player_poll(p, &st, &v)) {
			if (v.pts <= last)
				mono = 0;
			last = v.pts;
			shown++;
		}
		if (st.ended)
			break;
		av_sleep_ms(4);
	}
	CHECK(st.ended && shown >= 20 && mono && st.duration > 0, "%s: file mode: played to the end (%d frames in order, %.2f s long)",
		name, shown, st.duration / 1e6);
	half = st.duration / 2;
	av_player_seek(p, half);
	av_player_play(p);
	t0 = av_now();
	last = -1;
	while (av_now() - t0 < 3 * AV_US && last < 0) {
		struct av_video_out v;
		if (av_player_poll(p, &st, &v))
			last = v.pts;
		av_sleep_ms(4);
	}
	CHECK(last >= half - 200000 && last <= half + 100000, "%s: file mode: seek %.2f s: first frame at %.3f s", name, half / 1e6, last / 1e6);
	av_player_free(p);
	/* not a media file */
	p = av_player_new(AV_PIX_BGRA, &fake);
	snprintf(path, sizeof path, "%s/%s", dir, "mse.json");
	r = av_player_open_file(p, path);
	CHECK(r == AV_EUNSUP, "%s: file mode: a file of another kind refused (%d)", name, r);
	av_player_free(p);
}

/* the queue mode (WebKit's: the host parses, keeps the coded frames, and hands the player those to
 * decode, a little ahead of the playback position): played to the end, then a seek -- the queue
 * flushed and filled again from the start, as the host does from a random access point */
#define QMAX 512
static void test_player_queue(const char *dir, const char *name)
{
	size_t n;
	uint8_t *b = load(dir, name, &n);
	struct av_demux *d;
	static struct av_packet pk[QMAX];
	int npk = 0, i, src, map[8], ntr, next = 0, shown = 0, mono = 1, pass;
	struct av_player *p;
	struct av_store *s;
	struct av_player_status st;
	av_us last = -1, t0, first = -1;

	if (b == NULL)
		return;
	d = av_demux_new(AV_FMT_UNKNOWN);
	av_demux_append(d, b, n);
	av_demux_end(d);
	while (npk < QMAX && av_demux_read(d, &pk[npk]) == AV_OK)
		npk++;
	ntr = av_demux_ntracks(d);
	p = av_player_new(AV_PIX_RGBA, &fake);
	s = av_player_store(p);
	src = av_store_add_queue(s);
	for (i = 0; i < ntr && i < 8; i++)
		map[i] = av_store_queue_track(s, src, av_demux_track(d, i));
	CHECK(src >= 0 && ntr == 2 && map[0] >= 0 && map[1] >= 0 && npk > 60, "%s: queue: %d tracks, %d frames to put", name, ntr, npk);
	av_player_set_duration(p, 2 * AV_US);
	memset(&st, 0, sizeof st);
	for (pass = 0; pass < 2; pass++) {
		if (pass == 1) {
			/* the seek: the queues flushed, filled again from the first frame (a random access point) */
			for (i = 0; i < ntr; i++)
				av_store_queue_flush(s, src, map[i]);
			next = 0;
			av_player_seek(p, 1100000);
			shown = 0;
			last = -1;
		}
		av_player_play(p);
		t0 = av_now();
		while (av_now() - t0 < 4 * AV_US) {
			struct av_video_out v;
			/* frames put while the track holds less than half a second ahead */
			while (next < npk && av_store_queue_level(s, src, map[pk[next].track], st.time, NULL) < 500000) {
				av_store_queue_put(s, src, map[pk[next].track], &pk[next]);
				next++;
			}
			if (next == npk)
				for (i = 0; i < ntr; i++)
					av_store_queue_end(s, src, map[i], 1);
			if (av_player_poll(p, &st, &v)) {
				if (v.pts <= last)
					mono = 0;
				if (last < 0)
					first = v.pts;
				last = v.pts;
				shown++;
			}
			if (st.ended)
				break;
			av_sleep_ms(4);
		}
		if (pass == 0) {
			CHECK(st.ended && shown >= 45 && mono, "%s: queue: played to the end in %.2f s: %d frames shown in order (dropped %u)",
				name, (av_now() - t0) / 1e6, shown, st.dropped);
			CHECK(st.time >= 1990000 && st.time <= 2010000, "%s: queue: ended at %.3f s", name, st.time / 1e6);
		} else {
			CHECK(first >= 1040000 && first <= 1120000, "%s: queue: seek 1.1 s: first frame at %.3f s", name, first / 1e6);
			CHECK(st.ended && shown >= 20, "%s: queue: after the seek, played to the end (%d frames)", name, shown);
		}
	}
	av_player_free(p);
	for (i = 0; i < npk; i++)
		av_pkt_free(&pk[i]);
	av_demux_free(d);
	free(b);
}

int main(int argc, char **argv)
{
	const char *dir = argc > 1 ? argv[1] : ".";
	int n, i;
	const struct av_codec_info *c = av_codec_list(&n);

	printf("info  codecs:");
	for (i = 0; i < n; i++)
		printf(" %s (%s)", c[i].name, c[i].library);
	printf("\n");
	load_frames(dir);
	test_containers(dir);
	test_seek(dir, "clip.webm");
	test_seek(dir, "clip.mp4");
	test_seek(dir, "clip-moovend.mp4");
	test_flac(dir);
	test_wav(dir);
	test_mp3(dir);
	test_mse(dir, "mse.webm", "mse.json", "video/webm; codecs=\"i420, pcm\"");
	test_mse(dir, "frag.mp4", "frag.json", "video/mp4; codecs=\"i420, pcm\"");
	test_yuv();
	test_types();
	test_player(dir, "mse.webm", "mse.json", "video/webm");
	test_player(dir, "frag.mp4", "frag.json", "video/mp4");
	test_player_queue(dir, "mse.webm");
	test_player_queue(dir, "frag.mp4");
	test_player_file(dir, "clip.webm");
	test_player_file(dir, "clip-moovend.mp4");
	printf("%d passed, %d failed\n", oks, fails);
	return fails ? 1 : 0;
}
