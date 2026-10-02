/*
 * tools/tests/av/fftest.c -- the media library with FFmpeg (AV_WITH_FFMPEG: av_ffmpeg.c's decoders,
 * av_lavf.c's demuxers) on clips made by run.sh with the ffmpeg command: each container read to its
 * end through the push model (the bytes fed from where av_demux_want says, in 64 KB pieces), every
 * packet decoded -- the frames and the seconds of sound counted --, then the file mode (av_player_open_file):
 * played a second, a seek half way, its first frame there.
 *
 *   fftest DIR file:frames:seconds ...
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "av.h"
#include "av_os.h"

static int fails, oks;
#define CHECK(c, ...) do { if (c) { oks++; printf("ok    "); } else { fails++; printf("FAIL  "); } printf(__VA_ARGS__); printf("\n"); } while (0)

static int fo_open(void *c, int *rate) { (void) c; *rate = 44100; return 1; }
static unsigned fo_write(void *c, const int16_t *p, unsigned n) { (void) c; (void) p; return n; }
static unsigned fo_queued(void *c) { (void) c; return 2048; }
static void fo_close(void *c) { (void) c; }
static const struct av_audio_out fake = { 0, fo_open, fo_write, fo_queued, fo_close };

static void decode_all(const char *path, const char *name, int want_frames, double want_sec)
{
	FILE *f = fopen(path, "rb");
	long n, frames = 0, idle = 0;
	double sec = 0;
	unsigned char *b;
	struct av_demux *d;
	struct av_decoder *dec[8] = { 0 };
	int r, k, vcodec = -1;

	if (f == NULL) { CHECK(0, "%s: missing", name); return; }
	fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
	b = (unsigned char *) malloc((size_t) n);
	if (fread(b, 1, (size_t) n, f) != (size_t) n) n = 0;
	fclose(f);
	d = av_demux_new(AV_FMT_UNKNOWN);
	av_demux_set_size(d, n);
	for (;;) {
		struct av_packet p;
		r = av_demux_read(d, &p);
		if (r == AV_AGAIN) {
			int64_t w = av_demux_want(d);
			if (w == -1 || w >= n) {
				if (w >= n) av_demux_end(d);
				av_sleep_ms(1);
				if (++idle > 20000) break;
				continue;
			}
			idle = 0;
			av_demux_feed(d, w, b + w, (size_t) (n - w > 65536 ? 65536 : n - w));
			continue;
		}
		if (r != AV_OK)
			break;
		idle = 0;
		if (p.track >= 0 && p.track < 8) {
			const struct av_track *t = av_demux_track(d, p.track);
			if (dec[p.track] == NULL)
				dec[p.track] = av_decoder_new(t);
			if (dec[p.track] != NULL) {
				struct av_frame fr;
				av_decoder_send(dec[p.track], &p);
				while (av_decoder_receive(dec[p.track], &fr) == AV_OK) {
					if (fr.kind == AV_VIDEO) { frames++; vcodec = t->codec; }
					else if (fr.rate > 0) sec += (double) fr.samples / fr.rate;
				}
			}
		}
		av_pkt_free(&p);
	}
	for (k = 0; k < 8; k++) if (dec[k] != NULL) {
		struct av_frame fr;
		av_decoder_send(dec[k], NULL);
		while (av_decoder_receive(dec[k], &fr) == AV_OK)
			if (fr.kind == AV_VIDEO) frames++;
		av_decoder_free(dec[k]);
	}
	CHECK(r == AV_EOF && frames >= want_frames * 9 / 10 && frames <= want_frames + 2 && sec > want_sec - 0.4 && sec < want_sec + 0.4,
		"%s: %s, %ld frames (%d made), %.2f s of sound (%.1f), codec %d", name, av_demux_name(d), frames, want_frames, sec, want_sec, vcodec);
	av_demux_free(d);
	free(b);
}

static void file_mode(const char *path, const char *name, double len)
{
	struct av_player *p = av_player_new(AV_PIX_BGRA, &fake);
	struct av_player_status st;
	av_us t0, first = -1, half = (av_us) (len * 1e6 / 2);
	int r = av_player_open_file(p, path), shown = 0;

	CHECK(r == AV_OK, "%s: file mode: opened (%d)", name, r);
	if (r != AV_OK) { av_player_free(p); return; }
	av_player_play(p);
	t0 = av_now();
	memset(&st, 0, sizeof st);
	while (av_now() - t0 < 1500000) {
		struct av_video_out v;
		if (av_player_poll(p, &st, &v)) shown++;
		av_sleep_ms(4);
	}
	CHECK(shown >= 15 && !st.error, "%s: file mode: %d frames shown in 1.5 s (time %.2f s, error %d)", name, shown, st.time / 1e6, st.error);
	av_player_seek(p, half);
	t0 = av_now();
	while (av_now() - t0 < 4000000 && first < 0) {
		struct av_video_out v;
		if (av_player_poll(p, &st, &v)) first = v.pts;
		av_sleep_ms(4);
	}
	CHECK(first >= half - 600000 && first <= half + 200000, "%s: file mode: seek %.2f s, first frame at %.3f s", name, half / 1e6, first / 1e6);
	av_player_free(p);
}

int main(int argc, char **argv)
{
	int i;
	for (i = 2; i < argc; i++) {
		char path[512], name[128];
		int frames = 0;
		double sec = 0;
		if (sscanf(argv[i], "%127[^:]:%d:%lf", name, &frames, &sec) != 3)
			continue;
		snprintf(path, sizeof path, "%s/%s", argv[1], name);
		decode_all(path, name, frames, sec);
		file_mode(path, name, sec);
	}
	printf("%d passed, %d failed\n", oks, fails);
	return fails ? 1 : 0;
}
