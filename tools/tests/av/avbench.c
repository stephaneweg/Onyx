/*
 * tools/tests/av/avbench.c -- the decoders' speed: a clip demuxed, every packet of its video track
 * decoded and each frame converted to RGBA (av_yuv_to_rgb), its audio track decoded; the time
 * per frame (the mean, the slowest) and per second of sound. Built by tools/tests/av/bench.sh for
 * the PC and for AArch64 (the Pi's code: libvpx's NEON, dav1d's assembly, run under qemu).
 *
 *   avbench <clip> [repeats]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "av.h"

static double now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

int main(int argc, char **argv)
{
	FILE *fp;
	long n;
	uint8_t *buf, *rgb = NULL;
	int reps = argc > 2 ? atoi(argv[2]) : 1, rep;
	double vtot = 0, vmax = 0, ctot = 0, atot = 0;
	long frames = 0, asamples = 0;
	uint64_t fnv = 1469598103934665603ULL;	/* FNV-1a over every frame's RGB: the same on every build */
	int w = 0, h = 0, rate = 48000;
	const char *vname = "-", *aname = "-";

	if (argc < 2) {
		fprintf(stderr, "usage: avbench <clip> [repeats]\n");
		return 2;
	}
	fp = fopen(argv[1], "rb");
	if (fp == NULL) {
		perror(argv[1]);
		return 1;
	}
	fseek(fp, 0, SEEK_END);
	n = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	buf = malloc((size_t) n);
	if (buf == NULL || fread(buf, 1, (size_t) n, fp) != (size_t) n)
		return 1;
	fclose(fp);
	for (rep = 0; rep < reps; rep++) {
		struct av_demux *d = av_demux_new(AV_FMT_UNKNOWN);
		struct av_decoder *vd = NULL, *ad = NULL;
		int vt = -1, at = -1, r, i;
		struct av_packet pk;
		struct av_frame f;

		av_demux_feed(d, 0, buf, (size_t) n);
		av_demux_end(d);
		while ((r = av_demux_read(d, &pk)) == AV_OK || r == AV_AGAIN) {
			if (r == AV_AGAIN) {
				int64_t want = av_demux_want(d);
				if (want < 0 || want >= n)
					break;
				av_demux_feed(d, want, buf + want, (size_t) (n - want));
				av_demux_end(d);
				continue;
			}
			if (vt < 0 && at < 0)
				for (i = 0; i < av_demux_ntracks(d); i++) {
					const struct av_track *t = av_demux_track(d, i);
					if (t->kind == AV_VIDEO && vt < 0) {
						vt = i;
						vd = av_decoder_new(t);
						vname = vd ? av_decoder_name(vd) : "(none)";
					} else if (t->kind == AV_AUDIO && at < 0) {
						at = i;
						ad = av_decoder_new(t);
						aname = ad ? av_decoder_name(ad) : "(none)";
					}
				}
			if (pk.track == vt && vd != NULL) {
				/* a packet's decoding (shown frames or not: AV1's hidden ones), less the
				 * conversion of the frames it gave */
				double t0 = now_ms(), conv = 0, dt;
				av_decoder_send(vd, &pk);
				while (av_decoder_receive(vd, &f) == AV_OK) {
					double c0;
					if (rgb == NULL || f.width != w || f.height != h) {
						w = f.width;
						h = f.height;
						rgb = realloc(rgb, (size_t) w * h * 4);
					}
					c0 = now_ms();
					av_yuv_to_rgb(&f, rgb, w * 4, AV_PIX_RGBA);
					conv += now_ms() - c0;
					frames++;
					if (rep == 0) {
						size_t k;
						for (k = 0; k < (size_t) w * h * 4; k++)
							fnv = (fnv ^ rgb[k]) * 1099511628211ULL;
					}
				}
				dt = now_ms() - t0 - conv;
				ctot += conv;
				vtot += dt;
				if (dt > vmax)
					vmax = dt;
			} else if (pk.track == at && ad != NULL) {
				double t0 = now_ms();
				av_decoder_send(ad, &pk);
				while (av_decoder_receive(ad, &f) == AV_OK)
					asamples += f.samples, rate = f.rate;
				atot += now_ms() - t0;
			}
			av_packet_free(&pk);
		}
		if (vd != NULL) {
			av_decoder_send(vd, NULL);
			while (av_decoder_receive(vd, &f) == AV_OK)
				frames++;
		}
		av_decoder_free(vd);
		av_decoder_free(ad);
		av_demux_free(d);
	}
	printf("%s: video %s %dx%d: %ld frames, decode %.2f ms/frame (slowest %.1f), to RGB %.2f ms/frame; "
	       "audio %s: %.2f ms per second of sound; the frames' FNV %016llx\n", argv[1], vname, w, h, frames,
	       frames ? vtot / frames : 0, vmax, frames ? ctot / frames : 0, aname,
	       asamples ? atot / ((double) asamples / rate) : 0, (unsigned long long) fnv);
	free(rgb);
	free(buf);
	return 0;
}
