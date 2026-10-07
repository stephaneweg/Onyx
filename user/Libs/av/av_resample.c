/*
 * user/Libs/av/av_resample.c -- audio to the output: any rate and channel count -> s16 stereo at
 * the output's rate (Catmull-Rom interpolation; a playback rate other than 1 resamples further,
 * the pitch follows it), with a volume and clipping.
 *
 * Channels beyond two are folded down (the frame's order: L R C LFE BL BR SL SR, as WAVE /
 * SMPTE -- the decoders hand them so).
 */
#include <math.h>
#include "av_int.h"

struct av_resampler {
	int in_rate, in_ch, out_rate;
	double pos;		/* the next output's position, in input frames from hist[0] */
	float hist[3 * 2];	/* the last 3 input frames (stereo) */
	float *tmp;
	int tmp_cap;
};

struct av_resampler *av_resampler_new(int in_rate, int in_ch, int out_rate)
{
	struct av_resampler *r;

	if (in_rate <= 0 || in_ch <= 0 || out_rate <= 0)
		return NULL;
	r = (struct av_resampler *) calloc(1, sizeof *r);
	if (r == NULL)
		return NULL;
	r->in_rate = in_rate;
	r->in_ch = in_ch;
	r->out_rate = out_rate;
	r->pos = 1.0;		/* (the history is silence: start at its last frame + 0) */
	return r;
}

void av_resampler_free(struct av_resampler *r)
{
	if (r != NULL)
		free(r->tmp);
	free(r);
}

double av_resampler_factor(const struct av_resampler *r, double ratio)
{
	return (double) r->out_rate / ((double) r->in_rate * (ratio > 0 ? ratio : 1));
}

/* one input frame folded to stereo */
static void to_stereo(const float *s, int ch, float *l, float *rr)
{
	switch (ch) {
	case 1: *l = *rr = s[0]; break;
	case 2: *l = s[0]; *rr = s[1]; break;
	case 3: *l = 0.7f * (s[0] + 0.707f * s[2]); *rr = 0.7f * (s[1] + 0.707f * s[2]); break;
	case 4: *l = 0.6f * (s[0] + s[2]); *rr = 0.6f * (s[1] + s[3]); break;
	default: {
		/* 5.0 / 5.1 / 7.1: L R C (LFE) BL BR (SL SR) */
		float c = s[2] * 0.707f;
		float bl = ch >= 6 ? s[4] : s[3], br = ch >= 6 ? s[5] : s[4];
		float sl = ch >= 8 ? s[6] : 0, sr = ch >= 8 ? s[7] : 0;
		*l = 0.45f * (s[0] + c + 0.707f * (bl + sl));
		*rr = 0.45f * (s[1] + c + 0.707f * (br + sr));
	}
	}
}

static inline int16_t clip16(float v)
{
	v *= 32767.0f;
	if (v >= 32767.0f) return 32767;
	if (v <= -32768.0f) return -32768;
	return (int16_t) lrintf(v);
}

int av_resample(struct av_resampler *r, const float *in, int in_n, int16_t *out, int out_max,
		double ratio, float volume)
{
	int n = in_n + 3, i, o = 0;
	float *t;
	double step = (double) r->in_rate * (ratio > 0 ? ratio : 1) / (double) r->out_rate;

	if (in_n <= 0)
		return 0;
	if (n * 2 > r->tmp_cap) {
		free(r->tmp);
		r->tmp = (float *) malloc((size_t) n * 2 * sizeof(float));
		if (r->tmp == NULL) {
			r->tmp_cap = 0;
			return 0;
		}
		r->tmp_cap = n * 2;
	}
	t = r->tmp;
	memcpy(t, r->hist, sizeof r->hist);
	for (i = 0; i < in_n; i++)
		to_stereo(in + (size_t) i * r->in_ch, r->in_ch, &t[2 * (i + 3)], &t[2 * (i + 3) + 1]);
	/* outputs while 4 points are around them: p0 = floor(pos) - 1 .. p3 = floor(pos) + 2 */
	while (o < out_max) {
		int k = (int) r->pos;
		float x = (float) (r->pos - k);
		int j;
		const float *p0, *p1, *p2, *p3;

		if (k + 2 >= n)
			break;
		p0 = t + 2 * (k > 0 ? k - 1 : 0);
		p1 = t + 2 * k;
		p2 = t + 2 * (k + 1);
		p3 = t + 2 * (k + 2);
		for (j = 0; j < 2; j++) {
			float y = p1[j] + 0.5f * x * (p2[j] - p0[j] + x * (2 * p0[j] - 5 * p1[j] + 4 * p2[j] - p3[j] +
				x * (3 * (p1[j] - p2[j]) + p3[j] - p0[j])));
			out[2 * o + j] = clip16(y * volume);
		}
		o++;
		r->pos += step;
	}
	/* the last 3 frames are the next call's history */
	memcpy(r->hist, t + 2 * (n - 3), sizeof r->hist);
	r->pos -= (double) (n - 3);
	if (r->pos < 0)
		r->pos = 0;
	return o;
}
