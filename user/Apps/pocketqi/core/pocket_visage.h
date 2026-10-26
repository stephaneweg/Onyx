/* PocketQI (github.com/stephaneweg/PocketQI, commit 060b797), unchanged but for these two lines: MIT License,
 * Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md). */
/*
 * pocket_visage.h -- PocketQI : le visage de Pocket, deux grands yeux qui changent de forme avec l'humeur
 *
 * À la manière d'Eilik, Cozmo ou EMO : deux yeux lumineux sur un écran noir. Chaque humeur de
 * pocket_humeur.h (neutre, joyeux, rieur, triste, surpris, curieux, fatigue, fier, timide, colere, peur,
 * degout, ennui, malicieux, perplexe, mefiant, excite, clin, amoureux) est
 * une « pose » : la position, la taille et l'arrondi de chaque œil, et trois paupières qui en coupent
 * une partie (le haut, droit ou penché : colère, tristesse ; le bas, en arc : sourire « ^^ »).
 * L'intensité 1..3 rapproche plus ou moins la pose du neutre.
 *
 * Tout bouge par ressorts (un peu de rebond, comme les robots du commerce) : passer d'une humeur à
 * l'autre est une transition continue, même interrompue au milieu. Par-dessus : clignements au
 * hasard, regards de côté (les yeux du côté regardé grossissent un peu), respiration, et l'état du
 * shell (veille, écoute, réfléchit, parle, dort).
 *
 * C pur, sans allocation ni dépendance hormis <math.h> et <stdint.h> : le même code pour le robot
 * (écran SPI, plus tard) et pour le PC (pocket_visage.dll, fenêtre « Visage » de PocketQI Studio). Le
 * dessin est fait ligne par ligne, en ARGB 32 bits ou en RGB565, à n'importe quelle taille ; la distance
 * n'est calculée qu'au bord des yeux, le reste par segments (4,9 ms par image 320 x 170 sur le Pi Zero 2 W).
 */
#ifndef POCKET_VISAGE_H
#define POCKET_VISAGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* l'état du shell : ce que Pocket fait, en plus de son humeur */
typedef enum
{
	VISAGE_VEILLE = 0,	/* au repos : regards au hasard, clignements */
	VISAGE_ECOUTE,		/* « dis Pocket » entendu : les yeux grandissent, regardent droit devant */
	VISAGE_REFLECHIT,	/* « je travaille » : la phrase part au LLM, une app tourne, la voix se prépare... Les yeux
				 * deviennent deux points qui sautent chacun leur tour (comme « … » dans une messagerie) */
	VISAGE_PARLE,		/* Pocket parle : les yeux sautillent au rythme de la voix */
	VISAGE_DORT,		/* yeux fermés, respiration lente */
	VISAGE_ETAT_COUNT
} visage_etat;

/* un œil : centre (x, y), largeur, hauteur, arrondi ; paupière haute (part coupée 0..1, pente :
 * > 0 le coin intérieur descend, colère ; < 0 il monte, tristesse) ; paupière basse (0..1, en arc) */
enum { VE_X, VE_Y, VE_W, VE_H, VE_R, VE_HAUT, VE_PENTE, VE_BAS, VE_N };
/* la pose entière : les deux yeux, puis le regard propre à l'humeur (x, y en -1..1), les joues, et la
 * couleur des yeux qui vire au rose, et leur forme qui devient un cœur (amoureux) ; puis les petits signes
 * autour des yeux (26/10, d'après Otto de xiaozhi), 0..1 comme les joues : larmes (triste), goutte de sueur
 * (peur, timide), veine de colère, « z z z » (fatigué, dort), « ? » (perplexe, curieux), traits d'éclat
 * (surpris), étincelle (malicieux, clin d'œil, fier, excité), petit cœur qui monte (amoureux) */
enum { VP_GX = 2 * VE_N, VP_GY, VP_JOUES, VP_ROSE, VP_COEUR,
       VP_LARME, VP_SUEUR, VP_VEINE, VP_ZZZ, VP_QUESTION, VP_ECLAT, VP_ETINCELLE, VP_PCOEUR, VP_N };
#define VP_ACC0		VP_LARME	/* le premier signe ; VP_N - VP_ACC0 signes */

typedef struct
{
	float p[VP_N];		/* la pose montrée */
	float v[VP_N];		/* sa vitesse (ressorts) */
	float cible[VP_N];	/* la pose visée (humeur x intensité) */
	float g[2], gv[2], gcible[2];	/* le regard (ressort plus vif), -1..1 */
	int humeur, intensite, etat;
	float t;			/* secondes depuis l'init */
	float t_cligne, cligne;	/* prochain clignement ; phase en cours (0 : aucun, 0..1) */
	int cligne_double;
	float t_regard;		/* prochain regard au hasard */
	int regard_ext;		/* regard imposé de l'extérieur (une caméra, plus tard) */
	float ext[2];
	float rire;		/* phase du sautillement (rieur, parle) */
	float travail;		/* 0..1 : les sauts de « je travaille » (réfléchit), qui montent et retombent en 0,25 s */
	uint32_t hasard;
	uint32_t couleur;	/* les yeux, 0xRRGGBB */
	uint32_t fond;		/* l'écran, 0xRRGGBB */
	/* l'humeur par ses deux axes (pocket_visage_humeur_va) : le mélange des humeurs va_a et va_b (part de b : va_t),
	 * rapproché du neutre (va_k : 0 neutre .. 1) ; humeur et intensite gardent alors l'humeur la plus proche */
	int va, va_a, va_b;
	float va_t, va_k;
} pocket_visage;

void pocket_visage_init (pocket_visage *v, uint32_t graine);

/* l'humeur (un pocket_humeur) et son intensité 1..3 ; la transition part de la pose montrée */
void pocket_visage_humeur (pocket_visage *v, int humeur, int intensite);
/* l'humeur par ses deux axes, valence (-100 négatif .. 100 positif) et éveil (-100 calme .. 100 excité) :
 * voir pocket_humeur.h ; sur le point d'une humeur nommée, la même pose que pocket_visage_humeur */
void pocket_visage_humeur_va (pocket_visage *v, float valence, float eveil);
void pocket_visage_etat (pocket_visage *v, int etat);
/* regard imposé (x, y en -1..1 ; actif 0 : retour aux regards au hasard) */
void pocket_visage_regard (pocket_visage *v, float x, float y, int actif);
/* un clignement tout de suite */
void pocket_visage_cligne (pocket_visage *v);

/* avance l'animation de dt secondes (n'importe quel pas : découpé en petits pas en interne) */
void pocket_visage_step (pocket_visage *v, float dt);

/* dessin : la ligne y d'une image l x h. Le visage est mis à l'échelle de la hauteur, centré. */
void pocket_visage_ligne_argb (const pocket_visage *v, int y, int l, int h, uint32_t *ligne);
void pocket_visage_ligne_rgb565 (const pocket_visage *v, int y, int l, int h, uint16_t *ligne);
/* les n lignes y0 .. y0 + n - 1 d'une image l x h en RGB565, la ligne i à px + i * pas (pas en pixels) :
 * le même résultat que pocket_visage_ligne_rgb565 ligne par ligne, mais la pose n'est calculée qu'une fois
 * (sur le Pi : 0,8 ms de moins par image 320 x 170). Pour un écran plus large que haut, on peut ne
 * demander que les lignes du milieu d'une image plus haute (320 x 170 pris dans 320 x 240). */
#define POCKET_VISAGE_LIGNES_RGB565 1
void pocket_visage_lignes_rgb565 (const pocket_visage *v, int y0, int n, int l, int h, uint16_t *px, int pas);
/* l'image entière en ARGB (0xAARRGGBB ; en mémoire B G R A, le Bgra32 de Windows) ; pas : en pixels */
void pocket_visage_image_argb (const pocket_visage *v, uint32_t *px, int l, int h, int pas);
/* les mêmes, dessinés pixel par pixel comme avant le 24/10 (la distance calculée partout) : la référence
 * des essais ; le résultat est le même (au bit près), en 2 à 3 fois plus de temps */
void pocket_visage_lignes_rgb565_ref (const pocket_visage *v, int y0, int n, int l, int h, uint16_t *px, int pas);
void pocket_visage_image_argb_ref (const pocket_visage *v, uint32_t *px, int l, int h, int pas);

/* la pose dessinée pour une image l x h, pour un autre moteur de rendu (le GPU) : GVP_N flottants. Par œil
 * (à p + i * GVP_OEIL_N) : centre, échelles, demi-largeur, demi-hauteur, arrondi, paupières, côté, cœur,
 * étendue verticale, joue (en unités de visage, hauteur de l'image = 1) ; puis les joues, l'origine et
 * l'unité (pixels), le rose, la couleur des yeux et du fond (0..255), et les constantes du dessin. En dernier,
 * GVP_SIGNES : le nombre de petits signes visibles (le GPU ne les dessine pas : au-delà de 0, le CPU dessine). */
enum { GVP_CX, GVP_CY, GVP_SX, GVP_SY, GVP_HW, GVP_HH, GVP_R, GVP_HAUT, GVP_PENTE, GVP_BAS, GVP_COTE, GVP_COEUR,
       GVP_Y0, GVP_Y1, GVP_JX, GVP_JY, GVP_OEIL_N };
enum { GVP_JOUES = 2 * GVP_OEIL_N, GVP_EX, GVP_EY, GVP_UNITE, GVP_ROSE, GVP_CR, GVP_CG, GVP_CB, GVP_FR, GVP_FG, GVP_FB,
       GVP_HALO, GVP_HALO_FORCE, GVP_MARGE, GVP_ARRONDI, GVP_SIGNES, GVP_N };
void pocket_visage_parametres (const pocket_visage *v, int l, int h, float *p);

/* « veille ecoute reflechit parle dort » ; nom -> état (-1 si inconnu) */
const char *pocket_visage_etat_nom (int etat);
int pocket_visage_etat_de_nom (const char *nom);

#ifdef __cplusplus
}
#endif

#endif
