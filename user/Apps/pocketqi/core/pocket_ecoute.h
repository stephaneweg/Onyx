/* PocketQI (github.com/stephaneweg/PocketQI, commit 4d2a9ba), unchanged but for these two lines: MIT License,
 * Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md). */
/*
 * pocket_ecoute.h -- l'écoute permanente du shell vocal de Pocket : veille -> « dis Pocket » -> la phrase -> relance.
 *   C99 portable (ni malloc ni E/S), le même automate sur le PC (pocketqi_win) et sur le robot (noyau Circle).
 *
 *   VEILLE   le micro écoute, seul le réseau d'éveil (pocket_eveil.c) entend : RIEN ne part.
 *   ATTENTE  « dis Pocket » entendu (GEC_REVEIL : humeur curieux ; GEC_BIP : petit son, si on se tait) : la phrase (la fin de la
 *            même phrase « dis Pocket, quel temps fait-il ? », ou la suivante) pendant wait_s ; rien : GEC_FIN.
 *   PHRASE   parole en cours (détection à l'énergie, celle de pocketqi_win) ; fin : GEC_PHRASE (pcm, n) -> Voxtral.
 *   RELANCE  après la réponse de Pocket (pocket_ecoute_relance) : relance_s pour reparler SANS « dis Pocket » ;
 *            rien : GEC_FIN, retour en veille.
 *   eveil = 0 : l'ancien comportement (chaque phrase entendue part, pas de veille).
 * Ce qui part chez Voxtral : hors de veille, RIEN n'est découpé (décision du client, 23/10). Tout le son depuis le
 * début du mot de réveil (lead_s avant la détection), ou depuis le début de la relance, part en un bloc, silences
 * compris ; la seule décision est la fin : 0,7 s de silence après de la parole (ou le tampon plein), le silence final
 * au-delà de 0,3 s retiré. Sans parole dans wait_s / relance_s : GEC_FIN, rien ne part. Couper au milieu du mot
 * faisait mal transcrire le début de la phrase. L'hôte retire « Dis Pocket » du texte (pocket_ecoute_strip). Le petit
 * son (GEC_BIP) n'est joué qu'après une pause : joué pendant qu'on parle, le micro le captait par-dessus les mots.
 * Tampon conseillé : 25 s (6 s d'attente + 1,3 s de mot + 15 s de phrase).
 */
#ifndef POCKET_ECOUTE_H
#define POCKET_ECOUTE_H

#include "pocket_eveil.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GEC_BLOCK 512                  /* 32 ms à 16 kHz : un bloc du micro */
#define GEC_PRE_BLOCKS 10              /* pré-roulement gardé avant le début de la parole */
#define GEC_HIST_BLOCKS 40             /* historique en veille (1,28 s) : le mot de réveil entier */

enum { GEC_VEILLE, GEC_ATTENTE, GEC_PHRASE, GEC_RELANCE };

/* un détecteur de mot de réveil (PocketSphinx : pocket_kws_detect) ; sans détecteur (det nul), le réseau pocket_eveil.
 * Renvoie 1 si le mot vient d'être entendu : *hit_sample (dans le bloc), *score (0..1, pour l'affichage). */
typedef int pocket_detect_fn (void *ctx, const short *blk, int *hit_sample, float *score);
typedef void pocket_detect_reset_fn (void *ctx);
enum { GEC_RIEN = 0, GEC_REVEIL, GEC_PAROLE, GEC_PHRASE_PRETE, GEC_FIN, GEC_BIP };

typedef struct {
	int eveil;                          /* 1 : mot de réveil exigé (défaut) ; 0 : chaque phrase part */
	float wait_s, relance_s;            /* attente de la phrase après « dis Pocket » ; fenêtre de relance */
	float lead_s;                       /* le son envoyé commence lead_s avant la détection (1,1 : tout le mot) */
	pocket_eveil ge;                      /* le réseau (si det est nul) */
	pocket_detect_fn *det; pocket_detect_reset_fn *det_reset; void *det_ctx;
	int state, listen_state;            /* listen_state : ATTENTE ou RELANCE (où revenir après un claquement) */
	float floor_db;                    /* plancher de bruit (dBFS) */
	int loud, quiet, nblk, wait_blocks, started_at;
	short *pcm; size_t n, cap;          /* la phrase (mémoire fournie par l'hôte) */
	float peak_db;                      /* crête de la phrase */
	float wake_prob;                    /* probabilité au réveil */
	unsigned long wakes;                /* réveils depuis le début */
	short hist[GEC_HIST_BLOCKS][GEC_BLOCK];   /* derniers blocs de la veille (le mot de réveil) */
	int ihist, nhist;
	int since_wake;                     /* blocs depuis le réveil (-1 : pas de réveil en cours) */
	int bip_due;                        /* le petit son reste à jouer (après une pause) */
} pocket_ecoute;

/* buf : de quoi garder une phrase (16 kHz ; 15 s = 240 000 échantillons) */
void pocket_ecoute_init (pocket_ecoute *e, short *buf, size_t cap, float thr);
/* un bloc de GEC_BLOCK échantillons -> un événement (GEC_PHRASE_PRETE : e->pcm, e->n) */
int pocket_ecoute_block (pocket_ecoute *e, const short *blk);
/* après la réponse : fenêtre de relance (relance_s ; 0 : retour en veille) */
void pocket_ecoute_relance (pocket_ecoute *e);
void pocket_ecoute_veille (pocket_ecoute *e);
/* le micro était coupé (Pocket parlait) : oublier l'historique du réseau et la phrase en cours */
void pocket_ecoute_reprise (pocket_ecoute *e);
/* retire en tête d'un texte transcrit ce qui reste du mot (« Dis Pocket, », « Pocket », « lo, »...) ; en place */
void pocket_ecoute_strip (char *text);
/* fin de conversation dite (« au revoir », « merci, c'est tout »...) : pas de relance */
int pocket_ecoute_fin_dite (const char *text);
const char *pocket_ecoute_etat (const pocket_ecoute *e);

#ifdef __cplusplus
}
#endif
#endif
