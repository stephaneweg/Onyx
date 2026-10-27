/* PocketQI (github.com/stephaneweg/PocketQI, commit 4d2a9ba), unchanged but for these two lines: MIT License,
 * Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md). */
/*
 * pocket_eveil.c -- mot de réveil « dis Pocket » en flux (voir pocket_eveil.h et scripts/net.py, scripts/feat.py).
 *   Caractéristiques : trame de 30 ms toutes les 10 ms, Hann, FFT 512, 40 mel, log, moins la moyenne glissante.
 *   Réseau : 3 trames -> 32 (ReLU), puis 4 convolutions causales dilatées, puis 1 logit ; un pas toutes les 30 ms.
 */
#include "pocket_eveil.h"
#include "pocket_eveil_tables.h"
#include "pocket_eveil_model.h"
#include <string.h>

/* ---- log et exp sans libm (erreur < 1e-6, bien en dessous de ce qui compte ici) -------------------------- */
static float ge_log (float x)
{
	union { float f; unsigned u; } v = { x };
	int e = (int) ((v.u >> 23) & 255) - 127;
	v.u = (v.u & 0x007fffffu) | 0x3f800000u;        /* mantisse dans [1, 2) */
	float m = v.f;
	if (m > 1.41421356f) { m *= 0.5f; e++; }
	float s = (m - 1) / (m + 1), s2 = s * s;
	float p = s * (2.0f + s2 * (0.666666667f + s2 * (0.4f + s2 * (0.285714286f + s2 * 0.222222222f))));
	return p + (float) e * 0.693147181f;
}

static float ge_exp (float x)
{
	if (x > 30) x = 30;
	if (x < -30) x = -30;
	float t = x * 1.44269504f;
	int n = (int) (t < 0 ? t - 0.5f : t + 0.5f);
	float r = x - (float) n * 0.693147181f;
	float p = 1 + r * (1 + r * (0.5f + r * (0.166666667f + r * (0.0416666667f + r * (0.00833333333f + r * 0.00138888889f)))));
	union { float f; unsigned u; } v;
	v.u = (unsigned) (n + 127) << 23;
	return p * v.f;
}

/* ---- FFT complexe radix 2 en place, 512 points (facteurs de ge_cos / ge_sin) ---------------------------- */
static void ge_fft (float *re, float *im)
{
	const int n = GE_NFFT;
	for (int i = 1, j = 0; i < n; i++) {
		int bit = n >> 1;
		for (; j & bit; bit >>= 1) j ^= bit;
		j ^= bit;
		if (i < j) { float t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
	}
	for (int len = 2; len <= n; len <<= 1) {
		int step = n / len;
		for (int i = 0; i < n; i += len)
			for (int k = 0; k < len / 2; k++) {
				float wr = ge_cos[k * step], wi = -ge_sin[k * step];
				float *ar = re + i + k, *ai = im + i + k, *br = re + i + k + len / 2, *bi = im + i + k + len / 2;
				float tr = *br * wr - *bi * wi, ti = *br * wi + *bi * wr;
				*br = *ar - tr; *bi = *ai - ti; *ar += tr; *ai += ti;
			}
	}
}

/* ---- réseau ----------------------------------------------------------------------------------------------- */
static const float *const ge_w[GE_NL] = { ge_W2, ge_W3, ge_W4, ge_W5 };
static const float *const ge_b[GE_NL] = { ge_b2, ge_b3, ge_b4, ge_b5 };

static int rows (int l) { return (ge_layers[l][0] - 1) * ge_layers[l][1] + 1; }
static int hist_off (int l) { int o = 0; for (int i = 0; i < l; i++) o += rows (i) * ge_layers[i][2]; return o; }

typedef char ge_hist_fits[(5 * 32 + 9 * 48 + 17 * 48 + 17 * 64) <= POCKET_EVEIL_HIST ? 1 : -1];

static float net_step (pocket_eveil *g, const float *in120)
{
	float a[64], y[64];
	for (int o = 0; o < GE_C1; o++) {
		float s = ge_b1[o];
		for (int i = 0; i < 120; i++) s += in120[i] * ge_W1[i * GE_C1 + o];
		a[o] = s > 0 ? s : 0;
	}
	for (int l = 0; l < GE_NL; l++) {
		int k = ge_layers[l][0], d = ge_layers[l][1], ci = ge_layers[l][2], co = ge_layers[l][3], R = rows (l);
		float *h = g->hist + hist_off (l);
		int p = (g->pos[l] + 1) % R;                 /* la nouvelle entrée */
		memcpy (h + p * ci, a, sizeof (float) * ci);
		g->pos[l] = p;
		for (int o = 0; o < co; o++) y[o] = ge_b[l][o];
		for (int j = 0; j < k; j++) {
			const float *x = h + ((p - (k - 1 - j) * d + R * 4) % R) * ci;
			const float *w = ge_w[l] + j * ci * co;
			for (int c = 0; c < ci; c++) {
				float xc = x[c];
				if (xc == 0) continue;               /* ReLU : beaucoup de zéros */
				const float *wc = w + c * co;
				for (int o = 0; o < co; o++) y[o] += xc * wc[o];
			}
		}
		for (int o = 0; o < co; o++) a[o] = y[o] > 0 ? y[o] : 0;
	}
	float s = ge_bo[0];
	for (int i = 0; i < ge_layers[GE_NL - 1][3]; i++) s += a[i] * ge_Wo[i];
	return s;
}

void pocket_eveil_reset (pocket_eveil *g)
{
	memset (g->hist, 0, sizeof g->hist);
	memset (g->pos, 0, sizeof g->pos);
	g->ngrp = 0; g->nbuf = 0;
	g->prob = g->prev_prob = 0;
	g->last_hit = -1000000;
}

void pocket_eveil_init (pocket_eveil *g, float thr)
{
	memset (g, 0, sizeof *g);
	g->thr = thr; g->refractory = 50;
	pocket_eveil_reset (g);
}

/* une trame de 480 échantillons -> 40 valeurs normalisées dans out */
static void frame (pocket_eveil *g, float *out)
{
	static float re[GE_NFFT], im[GE_NFFT];
	for (int i = 0; i < GE_NFFT; i++) { re[i] = i < GE_WIN ? g->buf[i] * (1.0f / 32768) * ge_window[i] : 0; im[i] = 0; }
	ge_fft (re, im);
	float pw[GE_NFFT / 2 + 1];
	for (int i = 0; i <= GE_NFFT / 2; i++) pw[i] = re[i] * re[i] + im[i] * im[i];
	const float *w = ge_mel_w;
	for (int m = 0; m < GE_NMEL; m++) {
		float s = 0;
		for (int i = 0; i < ge_mel_len[m]; i++) s += pw[ge_mel_start[m] + i] * w[i];
		w += ge_mel_len[m];
		float lm = ge_log (s + GE_EPS);
		if (!g->have_mean) g->mean[m] = lm;
		g->mean[m] += GE_ALPHA * (lm - g->mean[m]);
		out[m] = lm - g->mean[m];
	}
	g->have_mean = 1;
}

int pocket_eveil_process (pocket_eveil *g, const short *pcm, int n)
{
	int hit = 0;
	for (int i = 0; i < n; i++) {
		g->buf[g->nbuf++] = pcm[i];
		if (g->nbuf < GE_WIN) continue;
		frame (g, g->grp + 40 * g->ngrp);
		g->frames++;
		memmove (g->buf, g->buf + GE_HOP, sizeof (short) * (GE_WIN - GE_HOP));
		g->nbuf = GE_WIN - GE_HOP;
		if (++g->ngrp < 3) continue;
		g->ngrp = 0;
		float lo = net_step (g, g->grp);
		g->prev_prob = g->prob;
		g->prob = 1 / (1 + ge_exp (-lo));
		float sm = g->steps ? (g->prob + g->prev_prob) / 2 : g->prob;
		if (sm > g->peak) g->peak = sm;
		if (sm >= g->thr && (long) g->steps - g->last_hit >= g->refractory && !hit) {
			g->last_hit = (long) g->steps; g->hit_sample = i + 1; hit = 1;
		}
		g->steps++;
	}
	return hit;
}
