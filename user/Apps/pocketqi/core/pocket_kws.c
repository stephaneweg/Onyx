/* PocketQI (github.com/stephaneweg/PocketQI, commit 4d2a9ba), unchanged but for these two lines: MIT License,
 * Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md). */
/*
 * pocket_kws.c -- « dis Pocket » par PocketSphinx en mot-clé (voir pocket_kws.h ; logique du banc ps_kws.c).
 */
#include "pocket_kws.h"
#include <pocketsphinx.h>
#include <stdio.h>
#include <string.h>

#define BLK 512
#define PRE_BLOCKS 10
#define START_BLOCKS 3
#define END_BLOCKS 22

/* 10 log10 de l'énergie moyenne en dBFS, sans libm (comme pocket_ecoute.c) */
static float block_db (const short *b)
{
	double e = 0;
	for (int i = 0; i < BLK; i++) e += (double) b[i] * b[i];
	e /= BLK * 32768.0 * 32768.0;
	if (e < 1e-12) return -120;
	int ex = 0;
	while (e < 0.5) { e *= 2; ex--; }
	while (e >= 1) { e /= 2; ex++; }
	double x = e * 2 - 1;
	double l2 = x * (1.4425449 + x * (-0.7181452 + x * (0.4575485 + x * (-0.2779042 + x * 0.0959533)))) - 1;
	return (float) (3.0103 * (l2 + ex));
}

int pocket_kws_init (pocket_kws *k, const char *hmm, const char *dict, const char *kw, const char *thr)
{
	memset (k, 0, sizeof *k);
	k->floor_db = -60;
	err_set_loglevel (ERR_WARN);
	ps_config_t *cfg = ps_config_init (NULL);
	if (!cfg) { snprintf (k->err, sizeof k->err, "ps_config_init"); return -1; }
	ps_config_set_str (cfg, "hmm", hmm);
	ps_config_set_str (cfg, "dict", dict);
	ps_config_set_str (cfg, "samprate", "16000");
	ps_config_set_str (cfg, "keyphrase", kw);
	ps_config_set_str (cfg, "kws_threshold", thr);
	ps_config_set_bool (cfg, "mmap", FALSE);           /* (Circle : pas de projection mémoire) */
	k->cfg = cfg;
	k->ps = ps_init (cfg);
	if (!k->ps) { snprintf (k->err, sizeof k->err, "modèle %s ou dictionnaire %s illisible", hmm, dict); return -1; }
	return 0;
}

void pocket_kws_reset (pocket_kws *k)
{
	if (k->inutt) ps_end_utt (k->ps);
	k->inutt = k->started = k->loud = k->quiet = k->npre = 0;
}

int pocket_kws_process (pocket_kws *k, const short *blk)
{
	float db = block_db (blk);
	int feed = 0;
	k->blocks++;
	if (!k->started) {
		float d = db < -70 ? -70 : db;               /* (plancher robuste, comme pocket_ecoute.c) */
		k->floor_db += (d < k->floor_db ? 0.3f : 0.02f) * (d - k->floor_db);
		memcpy (k->pre[k->ipre], blk, sizeof k->pre[0]);
		k->ipre = (k->ipre + 1) % PRE_BLOCKS;
		if (k->npre < PRE_BLOCKS) k->npre++;
		k->loud = db > k->floor_db + 10 && db > -55 ? k->loud + 1 : 0;
		if (k->loud < START_BLOCKS) return 0;
		k->started = 1; k->quiet = 0;
		ps_start_utt (k->ps); k->inutt = 1;
		for (int i = 0; i < k->npre - 1; i++)          /* le pré-roulement (le bloc courant suit) */
			ps_process_raw (k->ps, k->pre[(k->ipre - k->npre + i + PRE_BLOCKS) % PRE_BLOCKS], BLK, FALSE, FALSE);
		k->decoded += k->npre - 1;
		feed = 1;
	} else {
		k->quiet = db < k->floor_db + 6 ? k->quiet + 1 : 0;
		feed = 1;
	}
	int hit = 0;
	if (feed) {
		if (!k->inutt) { ps_start_utt (k->ps); k->inutt = 1; }
		ps_process_raw (k->ps, blk, BLK, FALSE, FALSE);
		k->decoded++;
		int32 sc;
		const char *h = ps_get_hyp (k->ps, &sc);
		if (h && *h) {
			hit = 1; k->hit_sample = BLK;
			ps_end_utt (k->ps); ps_start_utt (k->ps);
		}
		if (k->started && k->quiet >= END_BLOCKS) {    /* fin du segment de parole */
			ps_end_utt (k->ps); k->inutt = 0; k->started = 0; k->loud = 0; k->npre = 0;
		}
	}
	return hit;
}

void pocket_kws_free (pocket_kws *k)
{
	if (k->inutt) ps_end_utt (k->ps);
	if (k->ps) ps_free (k->ps);
	if (k->cfg) ps_config_free (k->cfg);
	memset (k, 0, sizeof *k);
}

int pocket_kws_detect (void *ctx, const short *blk, int *hit_sample, float *score)
{
	pocket_kws *k = (pocket_kws *) ctx;
	int hit = pocket_kws_process (k, blk);
	if (hit) { *hit_sample = k->hit_sample; *score = 1; }
	return hit;
}

void pocket_kws_reset_cb (void *ctx)
{
	pocket_kws_reset ((pocket_kws *) ctx);
}
