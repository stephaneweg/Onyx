/* PocketQI (github.com/stephaneweg/PocketQI, commit 060b797), unchanged but for these two lines: MIT License,
 * Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md). */
/*
 * pocket_humeur.h -- Pocket alpha : l'humeur du robot (yeux), un seul endroit pour la décider
 *
 * Une humeur = un identifiant d'une liste fixe + une intensité 1..3. Le LLM la propose (champ
 * « humeur » de sa réponse JSON), les règles AIML pourront la poser (<oob>humeur X</oob>). Un nom
 * inconnu donne « neutre », comme une action inconnue est ignorée par la liste blanche.
 * Aujourd'hui l'humeur est seulement affichée / journalisée (pocket_humeur_set appelle le
 * « récepteur » installé par l'hôte) ; plus tard ce récepteur pilotera les LED puis les écrans
 * des yeux (le système dessinera le visage et fera la transition).
 *
 * C pur, commun au noyau Circle et à pocket_ecoute.exe.
 */
#ifndef POCKET_HUMEUR_H
#define POCKET_HUMEUR_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
	HUMEUR_NEUTRE = 0,
	HUMEUR_JOYEUX,
	HUMEUR_RIEUR,
	HUMEUR_TRISTE,
	HUMEUR_SURPRIS,
	HUMEUR_CURIEUX,
	HUMEUR_FATIGUE,
	HUMEUR_FIER,
	HUMEUR_TIMIDE,
	HUMEUR_COLERE,
	/* (23/10/2026 : ajoutées à la fin, les numéros d'avant ne changent pas) */
	HUMEUR_PEUR,
	HUMEUR_DEGOUT,
	HUMEUR_ENNUI,
	HUMEUR_MALICIEUX,
	HUMEUR_PERPLEXE,
	HUMEUR_MEFIANT,
	HUMEUR_EXCITE,
	HUMEUR_CLIN,		/* clin d'œil */
	HUMEUR_AMOUREUX,
	HUMEUR_COUNT
} pocket_humeur;

#define HUMEUR_INTENSITE_MIN	1
#define HUMEUR_INTENSITE_MAX	3

/* « neutre joyeux rieur ... colere » (pour le prompt système) */
const char *pocket_humeur_list (void);

/* nom -> identifiant ; casse, accents et espaces ignorés (« en colère » = colere) ; inconnu ou
 * vide : HUMEUR_NEUTRE et *known = 0 (known peut être nul) */
pocket_humeur pocket_humeur_from_name (const char *name, int *known);
const char *pocket_humeur_name (pocket_humeur h);

/* l'humeur courante ; intensité ramenée dans 1..3 ; appelle le récepteur s'il y en a un */
typedef void pocket_humeur_sink (pocket_humeur h, int intensite, void *ctx);
void pocket_humeur_set_sink (pocket_humeur_sink *sink, void *ctx);
void pocket_humeur_set (pocket_humeur h, int intensite);
pocket_humeur pocket_humeur_get (int *intensite);

/* ---- les deux axes (25/10/2026) : valence (-100 négatif .. 100 positif) x éveil (-100 calme .. 100 excité) ----
 * 16 humeurs sont posées sur un cercle, chacune à son angle (0° : positif, 90° : excité), neutre au centre ;
 * la distance au centre joue le rôle de l'intensité (50 / 78 / 100 : intensités 1 / 2 / 3). Amoureux et clin
 * d'œil (des gestes) restent hors du plan. Le visage mélange les deux humeurs voisines par l'angle, puis
 * rapproche le mélange du neutre par la distance : sur une humeur nommée, c'est exactement sa pose. */
#define HUMEUR_AXE_MAX	100

/* l'angle d'une humeur sur le cercle, en degrés ; -1 si elle n'y est pas (neutre, clin, amoureux) */
int pocket_humeur_angle (pocket_humeur h);
/* les deux humeurs voisines du point, la part de b (0..1, par l'angle) et la distance ramenée à 0..1 */
void pocket_humeur_voisines (float valence, float eveil, pocket_humeur *a, pocket_humeur *b, float *t, float *k);
/* l'humeur nommée la plus proche du point et son intensité (neutre près du centre) */
pocket_humeur pocket_humeur_proche (float valence, float eveil, int *intensite);

/* l'humeur courante par ses deux axes (bornés à -100..100) : le nom le plus proche devient l'humeur courante
 * (pocket_humeur_get, le LLM, la voix), puis le récepteur est appelé ; il lit les axes par pocket_humeur_get_va */
void pocket_humeur_set_va (float valence, float eveil);
/* 1 si l'humeur courante a été posée par ses axes ; sinon 0, et le point de l'humeur nommée (0, 0 pour
 * neutre, clin et amoureux) */
int pocket_humeur_get_va (float *valence, float *eveil);

/* le périphérique MOOD: des apps BASIC, le même sur tous les hôtes :
 *   écrire « 60 40 » (valence éveil, séparés par une espace ou une virgule) ou « joyeux 2 » (un nom, l'intensité
 *   1..3, 3 si absente) : pocket_humeur_commande pose l'humeur, 0 si la ligne n'a rien de valable ;
 *   lire : pocket_humeur_texte donne « 60 40 joyeux 2 » (les axes arrondis, puis le nom le plus proche) */
int pocket_humeur_commande (const char *ligne);
void pocket_humeur_texte (char *out, unsigned cap);

/* retour au neutre, sans le LLM : delai_ms après la dernière humeur (0 : jamais ; 10 s par défaut).
 * L'hôte appelle pocket_humeur_tick avec son horloge (ms) dans sa boucle ; le délai part du premier tick qui
 * voit la nouvelle humeur (donc de la fin de la réponse, si la boucle est occupée pendant qu'elle parle). */
#define HUMEUR_DELAI_NEUTRE_MS	10000
void pocket_humeur_set_delai (unsigned delai_ms);
void pocket_humeur_tick (unsigned long now_ms);

#ifdef __cplusplus
}
#endif

#endif
