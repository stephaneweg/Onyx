/* PocketQI (github.com/stephaneweg/PocketQI, commit 4d2a9ba), unchanged but for these two lines: MIT License,
 * Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md). */
/*
 * pocket_kws.h -- mot de réveil « dis Pocket » par PocketSphinx en repérage de mot-clé (keyphrase), le moteur retenu
 *   (README : 23/24 phrases réelles du client contre 11 à 15 pour le réseau entraîné sur des voix de synthèse).
 *   Même logique que le banc src/ps_kws.c (mesures) : blocs de 32 ms, détection de parole à l'énergie, UN énoncé
 *   PocketSphinx par segment de parole (relancé au silence : sinon PocketSphinx ne détecte plus rien après quelques
 *   minutes d'un seul énoncé), rien de décodé pendant le silence.
 *   Le même fichier sur le PC (pocketqi_win, PocketSphinx MinGW de ecoute_pc) et sur le robot (Circle, stt_circle/ps).
 */
#ifndef POCKET_KWS_H
#define POCKET_KWS_H

#ifdef __cplusplus
extern "C" {
#endif

/* le mot de réveil (24/10/2026 : l'assistante s'appelle Pocket ; avant, « dis galo ») : les mots doivent être dans
 * le dictionnaire (data/eveil.dict) */
#define POCKET_KWS_PHRASE "dis pocket"
/* son seuil (25/10/2026 : « dis pocket », plus long que « dis galo », n'est presque jamais reconnu à 1e-20 ; 1e-30 :
 * 3 phrases de synthèse sur 5, environ 2 fausses alertes par heure de parole continue sur data/streams) */
#define POCKET_KWS_SEUIL "1e-30"

struct ps_decoder_s;
struct cmd_ln_s;

typedef struct {
	struct ps_decoder_s *ps;
	struct cmd_ln_s *cfg;
	int inutt, started, loud, quiet, npre, ipre;
	float floor_db;
	short pre[10][512];
	int hit_sample;                     /* après une détection : échantillon du bloc où elle tombe */
	unsigned long blocks, decoded;      /* blocs reçus / décodés (charge : décodés / reçus) */
	char err[128];
} pocket_kws;

/* hmm : dossier du modèle (PTM, mdef binaire) ; dict : dictionnaire (dis, pocket) ; kw : POCKET_KWS_PHRASE ;
 * thr : seuil (« 1e-20 ») ; 0 si prêt, -1 sinon (err) */
int pocket_kws_init (pocket_kws *k, const char *hmm, const char *dict, const char *kw, const char *thr);
/* un bloc de 512 échantillons 16 kHz ; 1 : « dis Pocket » vient d'être entendu (hit_sample) */
int pocket_kws_process (pocket_kws *k, const short *blk);
/* le micro était coupé : fin de l'énoncé en cours */
void pocket_kws_reset (pocket_kws *k);
void pocket_kws_free (pocket_kws *k);
/* pour pocket_ecoute (det, det_reset) : ctx = le pocket_kws */
int pocket_kws_detect (void *ctx, const short *blk, int *hit_sample, float *score);
void pocket_kws_reset_cb (void *ctx);

#ifdef __cplusplus
}
#endif
#endif
