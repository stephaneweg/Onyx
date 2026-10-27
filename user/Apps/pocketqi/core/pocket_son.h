/* PocketQI (github.com/stephaneweg/PocketQI, commit 4d2a9ba), unchanged but for these two lines: MIT License,
 * Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md). */
/* pocket_son.h -- le son de la phrase tel qu'il part chez Voxtral (transcription et audio direct), dans les trois
 * builds de PocketQI (pocketqi_win, le Pi sous Circle, l'ESP32-S3) : Ogg Opus (libopus 1.6.1, encodage seul, et un petit
 * écrivain Ogg), le WAV en secours si l'encodage échoue ou si l'Opus est coupé (« son = wav » dans <sd>/etc/ecoute.txt).
 * Entrée : PCM mono 16 bits (16 kHz). Sortie : un tampon malloc, avec de la place réservée avant et après (l'en-tête et
 * la fin du corps multipart), à libérer par pocket_son_libere. Aucune dépendance hors libopus, malloc et memcpy. */
#ifndef POCKET_SON_H
#define POCKET_SON_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define POCKET_SON_DEBIT		16000	/* b/s : même texte que le WAV chez Voxtral (26/10/2026), 18 fois plus petit */

typedef struct
{
	unsigned char *tampon;	/* malloc : avant + len + apres octets */
	unsigned char *data;	/* le fichier (Ogg Opus ou WAV), tampon + avant */
	size_t len;
	int opus;		/* 1 : Ogg Opus ; 0 : WAV */
	int erreur;		/* l'encodage Opus a échoué (code libopus < 0) : WAV en secours ; 0 sinon */
} pocket_son;

/* 1 (défaut) : Ogg Opus ; 0 : WAV, comme avant */
void pocket_son_set_opus (int opus);
int pocket_son_get_opus (void);
/* complexité de l'encodeur, 0 (rapide) à 10 (défaut de libopus) ; débit en b/s (POCKET_SON_DEBIT) */
void pocket_son_set_reglage (int complexite, int debit);

/* le fichier à envoyer ; avant / apres : octets réservés autour (rien n'y est écrit) ; 0 : mémoire */
int pocket_son_prepare (const short *pcm, size_t n, unsigned rate, size_t avant, size_t apres, pocket_son *s);
void pocket_son_libere (pocket_son *s);

/* pour le multipart de la transcription : "galo.opus" / "galo.wav", "audio/ogg" / "audio/wav" */
const char *pocket_son_nom (const pocket_son *s);
const char *pocket_son_mime (const pocket_son *s);

#ifdef __cplusplus
}
#endif

#endif
