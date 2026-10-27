/* PocketQI (github.com/stephaneweg/PocketQI, commit 4d2a9ba), unchanged but for these two lines: MIT License,
 * Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md). */
/*
 * pocket_eveil.h -- mot de réveil « dis Pocket » : petit réseau en flux (C99 portable : ni libm, ni malloc, ni E/S).
 *   Le même fichier sert à pocketqi_win (PC) et au noyau Circle du robot. Les poids sont dans pocket_eveil_model.h
 *   (scripts/net.py export), les tables des caractéristiques dans pocket_eveil_tables.h (scripts/feat.py tables).
 *
 *   static pocket_eveil ge;  pocket_eveil_init (&ge, 0.5f);
 *   pour chaque bloc du micro (16 kHz mono) :  if (pocket_eveil_process (&ge, pcm, n) > 0) -> réveillé ;
 *   ge.hit_sample : position (dans le bloc) de la fin du mot : ce qui suit est la phrase pour Voxtral.
 *   Coût : ~0,4 MFLOP/s de caractéristiques + ~1,7 MMAC/s de réseau ; ~11 Ko d'état, 200 Ko de poids.
 */
#ifndef POCKET_EVEIL_H
#define POCKET_EVEIL_H

#ifdef __cplusplus
extern "C" {
#endif

#define POCKET_EVEIL_HIST 2560           /* historiques des couches dilatées (floats) : 5x32 + 9x48 + 17x48 + 17x64 */

typedef struct {
	float thr;                          /* seuil sur la probabilité lissée (0..1) */
	int refractory;                     /* pas de 30 ms après une détection (50 : 1,5 s) */
	/* caractéristiques */
	short buf[480]; int nbuf;           /* derniers échantillons (trame de 30 ms) */
	int frames;                         /* trames produites */
	float mean[40]; int have_mean;      /* moyenne glissante par bande */
	float grp[120]; int ngrp;           /* 3 trames regroupées pour la première couche */
	/* réseau */
	float hist[POCKET_EVEIL_HIST];        /* historique des entrées des couches dilatées */
	int pos[4];                         /* position courante dans chaque historique circulaire */
	unsigned long steps;                /* pas de 30 ms calculés */
	float prob, prev_prob;              /* sortie (probabilité) et la précédente */
	long last_hit;                      /* pas de la dernière détection */
	int hit_sample;                     /* après une détection : échantillon du bloc où elle tombe */
	float peak;                         /* plus forte probabilité lissée depuis le dernier pocket_eveil_peak_reset */
} pocket_eveil;

void pocket_eveil_init (pocket_eveil *g, float thr);
/* oublie l'historique du réseau (après que Pocket a parlé : le micro était coupé) ; garde la moyenne du micro */
void pocket_eveil_reset (pocket_eveil *g);
/* n échantillons ; renvoie 1 si « dis Pocket » vient d'être entendu (hit_sample : où dans le bloc), sinon 0 */
int pocket_eveil_process (pocket_eveil *g, const short *pcm, int n);

#ifdef __cplusplus
}
#endif
#endif
