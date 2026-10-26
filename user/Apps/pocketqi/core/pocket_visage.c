/* PocketQI (github.com/stephaneweg/PocketQI, commit 060b797), unchanged but for these two lines: MIT License,
 * Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md). */
/*
 * pocket_visage.c -- PocketQI : le visage de Pocket (voir pocket_visage.h)
 *
 * Chaque œil est une forme décrite par sa distance signée (négative dedans) : un rectangle arrondi,
 * moins un demi-plan en haut (la paupière haute, droite ou penchée) et moins un disque en bas (la
 * paupière basse, qui creuse l'œil en arc : « ^^ »). Tous les paramètres varient continûment, donc
 * n'importe quelle pose se fond dans n'importe quelle autre. La distance donne aussi l'anticrénelage
 * (un pixel de fondu au bord) et le halo lumineux autour des yeux.
 */
#include "pocket_visage.h"
#include "pocket_humeur.h"
#include <math.h>
#include <string.h>

#define PI_F		3.14159265f

/* ressorts : la pose (un peu de rebond) et le regard (plus vif) */
#define POSE_OMEGA	16.0f
#define POSE_ZETA	0.55f
#define REGARD_OMEGA	26.0f
#define REGARD_OMEGA_ENNUI	5.0f	/* (l'ennui : le regard glisse en ~0,8 s) */
#define REGARD_ZETA	0.75f
#define PAS_MAX		(1.0f / 240)

/* ---- les poses (intensité 3) ---------------------------------------------------------------- */
/* un œil, côté gauche de l'écran ; x est la distance au milieu (l'œil droit est son miroir) */
typedef struct { float x, y, w, h, r, haut, pente, bas; } oeil;
typedef struct { oeil g, d; float gx, gy, joues, rose, coeur; } pose;

#define SYM(...)	{ __VA_ARGS__ }, { __VA_ARGS__ }
static const pose s_poses[HUMEUR_COUNT] =
{
	/* neutre : deux grands yeux arrondis */
	{ SYM (0.22f, -0.02f, 0.25f, 0.36f, 0.11f, 0.00f,  0.00f, 0.00f), 0, 0, 0, 0, 0 },
	/* joyeux : yeux plissés par le sourire, en arc */
	{ SYM (0.22f, -0.05f, 0.27f, 0.31f, 0.13f, 0.00f,  0.00f, 0.42f), 0, -0.2f, 0, 0, 0 },
	/* rieur : deux « ^ » fins (et ils sautillent : pocket_visage_step) */
	{ SYM (0.22f, -0.06f, 0.28f, 0.27f, 0.135f, 0.00f, 0.00f, 0.72f), 0, -0.2f, 0, 0, 0 },
	/* triste : paupière haute tombante vers l'extérieur, yeux bas */
	{ SYM (0.21f,  0.05f, 0.24f, 0.30f, 0.10f, 0.40f, -0.50f, 0.00f), 0, 0.5f, 0, 0, 0 },
	/* surpris : grands yeux ronds */
	{ SYM (0.23f, -0.05f, 0.29f, 0.44f, 0.145f, 0.00f, 0.00f, 0.00f), 0, -0.3f, 0, 0, 0 },
	/* curieux : un œil grand ouvert, l'autre un peu plissé, regard de côté */
	{ { 0.22f, -0.04f, 0.27f, 0.40f, 0.12f, 0.00f, 0.00f, 0.00f },
	  { 0.22f,  0.00f, 0.23f, 0.31f, 0.10f, 0.26f, 0.15f, 0.06f }, 0.45f, -0.15f, 0, 0, 0 },
	/* fatigué : paupières à moitié tombées */
	{ SYM (0.22f,  0.04f, 0.26f, 0.32f, 0.10f, 0.52f, -0.10f, 0.08f), 0, 0.3f, 0, 0, 0 },
	/* fier : regard satisfait, mi-clos en haut, arc en bas */
	{ SYM (0.22f, -0.05f, 0.27f, 0.31f, 0.13f, 0.24f, -0.08f, 0.46f), 0, -0.6f, 0, 0, 0 },
	/* timide : petits yeux, regard en bas de côté, joues roses */
	{ SYM (0.21f,  0.04f, 0.22f, 0.29f, 0.10f, 0.08f,  0.00f, 0.16f), -0.55f, 0.6f, 1, 0, 0 },
	/* colère : paupière haute penchée vers le milieu */
	{ SYM (0.21f,  0.00f, 0.26f, 0.30f, 0.07f, 0.44f,  0.60f, 0.08f), 0, 0, 0, 0, 0 },
	/* peur : petits yeux écarquillés, sourcils relevés au milieu, regard qui fuit (et ils tremblent) */
	{ SYM (0.24f, -0.04f, 0.20f, 0.38f, 0.10f, 0.40f, -0.65f, 0.00f), 0.45f, 0.2f, 0, 0, 0 },
	/* dégoût : plissés de travers, un œil plus fermé, regard détourné */
	{ { 0.22f,  0.01f, 0.26f, 0.28f, 0.08f, 0.30f,  0.20f, 0.30f },
	  { 0.22f,  0.03f, 0.24f, 0.22f, 0.08f, 0.42f,  0.35f, 0.34f }, -0.5f, 0.2f, 0, 0, 0 },
	/* ennui : paupières plates à moitié tombées, regard vers le bas (26/10 : il regarde lentement autour de lui, d'un
	 * côté puis de l'autre, comme un enfant qui attend : regard_suivant ; avant, en l'air de côté, avec des « z z z » :
	 * un enfant de 6 ans le prenait pour « endormi ») */
	{ SYM (0.22f,  0.01f, 0.27f, 0.32f, 0.09f, 0.48f,  0.00f, 0.12f), 0, 0.55f, 0, 0, 0 },
	/* malicieux : un œil plissé en coin, l'autre à moitié, sourire en bas */
	{ { 0.22f, -0.02f, 0.26f, 0.30f, 0.10f, 0.36f,  0.40f, 0.34f },
	  { 0.22f, -0.03f, 0.26f, 0.32f, 0.11f, 0.18f,  0.10f, 0.30f }, 0.35f, 0, 0, 0, 0 },
	/* perplexe : un œil grand, l'autre petit aux sourcils relevés */
	{ { 0.22f, -0.06f, 0.27f, 0.40f, 0.12f, 0.00f,  0.00f, 0.00f },
	  { 0.21f,  0.03f, 0.21f, 0.24f, 0.09f, 0.28f, -0.55f, 0.08f }, -0.3f, -0.3f, 0, 0, 0 },
	/* méfiant : deux fentes, regard en coin */
	{ SYM (0.22f,  0.01f, 0.28f, 0.30f, 0.07f, 0.50f,  0.12f, 0.32f), 0.8f, 0, 0, 0, 0 },
	/* excité : grands yeux brillants qui sautillent */
	{ SYM (0.23f, -0.06f, 0.30f, 0.42f, 0.15f, 0.00f,  0.00f, 0.14f), 0, -0.3f, 0, 0, 0 },
	/* clin d'œil : un œil ouvert, l'autre fermé en arc */
	{ { 0.22f, -0.04f, 0.27f, 0.36f, 0.12f, 0.00f,  0.00f, 0.10f },
	  { 0.22f, -0.03f, 0.28f, 0.24f, 0.12f, 0.00f,  0.00f, 0.80f }, 0, -0.2f, 0.3f, 0, 0 },
	/* amoureux : les yeux deviennent des cœurs roses, joues roses, ils battent comme un cœur */
	{ SYM (0.22f, -0.04f, 0.27f, 0.31f, 0.13f, 0.00f,  0.00f, 0.00f), 0, -0.3f, 0.8f, 1, 1 },
};

static const float s_intensite[3] = { 0.5f, 0.78f, 1.0f };

/* les petits signes autour des yeux (VP_LARME...), à l'intensité 3 ; comme les joues, l'intensité les rapproche de 0,
 * et ils ne se montrent qu'au-delà de la moitié (prepare_signes) : rien à l'intensité 1, plus petits à l'intensité 2 */
#define NSIGNES		(VP_N - VP_ACC0)
#define SG(vp)		[(vp) - VP_ACC0]
static const float s_signes[HUMEUR_COUNT][NSIGNES] =
{
	[HUMEUR_TRISTE]    = { SG (VP_LARME) = 1 },
	[HUMEUR_SURPRIS]   = { SG (VP_ECLAT) = 1 },
	[HUMEUR_CURIEUX]   = { SG (VP_QUESTION) = 0.8f },
	[HUMEUR_FATIGUE]   = { SG (VP_ZZZ) = 1 },
	[HUMEUR_FIER]      = { SG (VP_ETINCELLE) = 0.85f },
	[HUMEUR_TIMIDE]    = { SG (VP_SUEUR) = 0.85f },
	[HUMEUR_COLERE]    = { SG (VP_VEINE) = 1 },
	[HUMEUR_PEUR]      = { SG (VP_SUEUR) = 1 },
	[HUMEUR_MALICIEUX] = { SG (VP_ETINCELLE) = 1 },
	[HUMEUR_PERPLEXE]  = { SG (VP_QUESTION) = 1 },
	[HUMEUR_EXCITE]    = { SG (VP_ETINCELLE) = 1 },
	[HUMEUR_CLIN]      = { SG (VP_ETINCELLE) = 1 },
	[HUMEUR_AMOUREUX]  = { SG (VP_PCOEUR) = 1 },
};

static void pose_vers (const pose *p, float k, float *dst)
{
	const pose *n = &s_poses[HUMEUR_NEUTRE];
	const oeil *o[2] = { &p->g, &p->d }, *on[2] = { &n->g, &n->d };
	for (int i = 0; i < 2; i++)
	{
		const float *a = &on[i]->x, *b = &o[i]->x;
		float *d = dst + i * VE_N;
		for (int j = 0; j < VE_N; j++) d[j] = a[j] + (b[j] - a[j]) * k;
		d[VE_X] = i == 0 ? -d[VE_X] : d[VE_X];		/* l'œil gauche de l'écran est à x < 0 */
	}
	dst[VP_GX] = p->gx * k;
	dst[VP_GY] = p->gy * k;
	dst[VP_JOUES] = p->joues * k;
	dst[VP_ROSE] = p->rose;
	dst[VP_COEUR] = p->coeur;		/* (des cœurs roses à toutes les intensités : l'intensité joue sur le battement et les joues) */
	const float *sg = s_signes[p - s_poses];
	for (int j = 0; j < NSIGNES; j++) dst[VP_ACC0 + j] = sg[j] * k;
}

/* ---- hasard (xorshift) ---------------------------------------------------------------------- */
static float hasard (pocket_visage *v, float a, float b)
{
	uint32_t x = v->hasard;
	x ^= x << 13; x ^= x >> 17; x ^= x << 5;
	v->hasard = x;
	return a + (b - a) * (float) (x >> 8) / 16777216.0f;
}

/* ---- l'état ----------------------------------------------------------------------------------- */
/* deux axes : le mélange des humeurs a et b (part de b : t), rapproché du neutre (k) */
static void pose_va (int a, int b, float t, float k, float *dst)
{
	float pa[VP_N], pb[VP_N], pn[VP_N];
	pose_vers (&s_poses[a], 1, pa);
	pose_vers (&s_poses[b], 1, pb);
	pose_vers (&s_poses[HUMEUR_NEUTRE], 1, pn);
	for (int i = 0; i < VP_N; i++) dst[i] = pn[i] + (pa[i] + (pb[i] - pa[i]) * t - pn[i]) * k;
}

/* la force d'un effet propre à une humeur (sautillement, tremblement...) : l'intensité si c'est l'humeur montrée,
 * sa part du mélange en deux axes */
static float effet (const pocket_visage *v, int h)
{
	if (!v->va) return v->humeur == h ? s_intensite[v->intensite - 1] : 0;
	return ((v->va_a == h ? 1 - v->va_t : 0) + (v->va_b == h ? v->va_t : 0)) * v->va_k;
}

static void calcule_cible (pocket_visage *v)
{
	if (v->va) pose_va (v->va_a, v->va_b, v->va_t, v->va_k, v->cible);
	else pose_vers (&s_poses[v->humeur], s_intensite[v->intensite - 1], v->cible);
	for (int i = 0; i < 2; i++)
	{
		float *e = v->cible + i * VE_N;
		if (v->etat == VISAGE_ECOUTE) { e[VE_W] *= 1.08f; e[VE_H] *= 1.08f; }
		else if (v->etat == VISAGE_REFLECHIT)
		{
			/* « je travaille » : deux petits points ronds, au milieu (ils sautent : prepare) */
			e[VE_Y] = 0; e[VE_W] = 0.17f; e[VE_H] = 0.17f; e[VE_R] = 0.085f;
			e[VE_HAUT] = 0; e[VE_PENTE] = 0; e[VE_BAS] = 0;
		}
		else if (v->etat == VISAGE_DORT)
		{
			e[VE_Y] = 0.06f; e[VE_H] = 0.035f; e[VE_R] = 0.0175f;
			e[VE_HAUT] = 0; e[VE_PENTE] = 0; e[VE_BAS] = 0;
		}
	}
	if (v->etat == VISAGE_REFLECHIT)	/* (ni regard propre à l'humeur, ni joues, ni cœurs, ni signes : rien que les points) */
	{
		v->cible[VP_GX] = v->cible[VP_GY] = v->cible[VP_JOUES] = v->cible[VP_COEUR] = 0;
		for (int j = VP_ACC0; j < VP_N; j++) v->cible[j] = 0;
	}
	else if (v->etat == VISAGE_DORT)	/* il dort : « z z z », et aucun autre signe */
		for (int j = VP_ACC0; j < VP_N; j++) v->cible[j] = j == VP_ZZZ ? 1 : 0;
}

void pocket_visage_init (pocket_visage *v, uint32_t graine)
{
	memset (v, 0, sizeof *v);
	v->hasard = graine != 0 ? graine : 0x9E3779B9u;
	v->humeur = HUMEUR_NEUTRE;
	v->intensite = 1;
	v->etat = VISAGE_VEILLE;
	v->couleur = 0x3AC6FF;
	v->fond = 0x000000;
	calcule_cible (v);
	memcpy (v->p, v->cible, sizeof v->p);
	v->t_cligne = hasard (v, 1.0f, 3.0f);
	v->t_regard = hasard (v, 1.0f, 2.5f);
}

void pocket_visage_humeur (pocket_visage *v, int humeur, int intensite)
{
	if (humeur < 0 || humeur >= HUMEUR_COUNT) humeur = HUMEUR_NEUTRE;
	if (intensite < 1) intensite = 1;
	if (intensite > 3) intensite = 3;
	v->humeur = humeur;
	v->intensite = intensite;
	v->va = 0;
	calcule_cible (v);
	v->t_regard = v->t + hasard (v, 1.5f, 3.0f);	/* (l'humeur garde son regard un moment) */
	v->gcible[0] = v->gcible[1] = 0;
}

void pocket_visage_humeur_va (pocket_visage *v, float valence, float eveil)
{
	pocket_humeur a, b;
	int n;
	float t, k;
	pocket_humeur_voisines (valence, eveil, &a, &b, &t, &k);
	int change = !v->va || (int) a != v->va_a || (int) b != v->va_b || fabsf (t - v->va_t) > 0.05f || fabsf (k - v->va_k) > 0.05f;
	v->va = 1;
	v->va_a = a; v->va_b = b; v->va_t = t; v->va_k = k;
	v->humeur = pocket_humeur_proche (valence, eveil, &n);
	v->intensite = n;
	calcule_cible (v);
	if (change)		/* (un curseur qu'on déplace : le regard de l'humeur n'est pas relancé à chaque pas) */
	{
		v->t_regard = v->t + hasard (v, 1.5f, 3.0f);
		v->gcible[0] = v->gcible[1] = 0;
	}
}

void pocket_visage_etat (pocket_visage *v, int etat)
{
	if (etat < 0 || etat >= VISAGE_ETAT_COUNT) etat = VISAGE_VEILLE;
	if (etat == v->etat) return;
	v->etat = etat;
	calcule_cible (v);
	v->t_regard = v->t;			/* un regard qui va avec, tout de suite */
	if (etat == VISAGE_ECOUTE && v->cligne == 0) v->cligne = 0.001f;	/* (un clignement d'attention) */
}

void pocket_visage_regard (pocket_visage *v, float x, float y, int actif)
{
	v->regard_ext = actif;
	v->ext[0] = x < -1 ? -1 : x > 1 ? 1 : x;
	v->ext[1] = y < -1 ? -1 : y > 1 ? 1 : y;
}

void pocket_visage_cligne (pocket_visage *v)
{
	if (v->cligne == 0) v->cligne = 0.001f;
}

/* ---- l'animation ------------------------------------------------------------------------------ */
static void regard_suivant (pocket_visage *v)
{
	float x = 0, y = 0, dt = 2;
	switch (v->etat)
	{
	case VISAGE_VEILLE:
		if (effet (v, HUMEUR_ENNUI) > 0.5f)
		{
			/* l'ennui : il regarde autour de lui, en bas (le regard de la pose), d'un côté puis de l'autre, et parfois
			 * un arrêt au milieu ; le regard glisse lentement (pas ()) */
			if (fabsf (v->gcible[0]) > 0.2f && hasard (v, 0, 1) < 0.25f) { x = hasard (v, -0.1f, 0.1f); dt = hasard (v, 0.8f, 1.4f); }
			else
			{
				float cote = v->gcible[0] > 0.2f ? -1.0f : v->gcible[0] < -0.2f ? 1.0f : hasard (v, 0, 1) < 0.5f ? -1.0f : 1.0f;
				x = cote * hasard (v, 0.6f, 0.95f);
				dt = hasard (v, 1.5f, 3.0f);
			}
			y = hasard (v, -0.05f, 0.15f);
			break;
		}
		if (hasard (v, 0, 1) < 0.55f) { x = hasard (v, -0.2f, 0.2f); y = hasard (v, -0.15f, 0.15f); }
		else { x = hasard (v, -0.95f, 0.95f); y = hasard (v, -0.5f, 0.5f); }
		dt = hasard (v, 1.2f, 4.0f);
		break;
	case VISAGE_ECOUTE:
		x = hasard (v, -0.08f, 0.08f); y = hasard (v, -0.05f, 0.05f); dt = hasard (v, 1.5f, 3.0f);
		break;
	case VISAGE_REFLECHIT:
		x = 0; y = 0; dt = 1;		/* (les points restent au milieu) */
		break;
	case VISAGE_PARLE:
		x = hasard (v, -0.3f, 0.3f); y = hasard (v, -0.15f, 0.1f); dt = hasard (v, 0.8f, 2.0f);
		break;
	default:
		x = 0; y = 0.3f; dt = 5;
	}
	float saut = fabsf (x - v->gcible[0]) + fabsf (y - v->gcible[1]);
	v->gcible[0] = x; v->gcible[1] = y;
	v->t_regard = v->t + dt;
	if (saut > 0.8f && v->cligne == 0 && hasard (v, 0, 1) < 0.3f) v->cligne = 0.001f;	/* un grand regard : un clignement */
}

static void ressort (float *p, float *vit, float c, float omega, float zeta, float dt)
{
	float a = omega * omega * (c - *p) - 2 * zeta * omega * *vit;
	*vit += a * dt;
	*p += *vit * dt;
}

static void pas (pocket_visage *v, float dt)
{
	v->t += dt;
	for (int i = 0; i < VP_N; i++) ressort (&v->p[i], &v->v[i], v->cible[i], POSE_OMEGA, POSE_ZETA, dt);
	/* le regard : vif, sauf dans l'ennui, où il glisse lentement d'un côté à l'autre (selon la part d'ennui) */
	float ennui = v->regard_ext ? 0 : effet (v, HUMEUR_ENNUI), om = REGARD_OMEGA + (REGARD_OMEGA_ENNUI - REGARD_OMEGA) * ennui;
	for (int i = 0; i < 2; i++)
		ressort (&v->g[i], &v->gv[i], v->regard_ext ? v->ext[i] : v->gcible[i], om, REGARD_ZETA, dt);

	/* clignements (plus lents et plus fréquents quand Pocket est fatigué, plus lents dans l'ennui ; aucun quand il dort) */
	int las = v->va ? effet (v, HUMEUR_FATIGUE) > 0.5f : v->humeur == HUMEUR_FATIGUE;
	if (v->cligne > 0)
	{
		v->cligne += dt / (las || ennui > 0.5f ? 0.38f : 0.2f);
		if (v->cligne >= 1)
		{
			v->cligne = 0;
			if (v->cligne_double) { v->cligne_double = 0; v->cligne = 0.001f; }
		}
	}
	else if (v->t >= v->t_cligne && v->etat != VISAGE_DORT && v->etat != VISAGE_REFLECHIT)
	{
		v->cligne = 0.001f;
		v->cligne_double = hasard (v, 0, 1) < 0.2f;
	}
	if (v->cligne > 0 && v->t >= v->t_cligne)
		v->t_cligne = v->t + (las ? hasard (v, 1.5f, 3.5f) : hasard (v, 2.0f, 6.0f));
	if (!v->regard_ext && v->t >= v->t_regard) regard_suivant (v);
	/* les sauts de « je travaille » : montent et retombent en douceur */
	float tr = v->etat == VISAGE_REFLECHIT ? 1.0f : 0.0f, k = dt / 0.25f;
	v->travail += (tr - v->travail) * (k > 1 ? 1 : k);
}

void pocket_visage_step (pocket_visage *v, float dt)
{
	if (dt <= 0) return;
	if (dt > 0.5f) dt = 0.5f;		/* (une fenêtre restée cachée : pas de saut) */
	while (dt > PAS_MAX) { pas (v, PAS_MAX); dt -= PAS_MAX; }
	pas (v, dt);
}

/* ---- le dessin -------------------------------------------------------------------------------- */
typedef struct
{
	float cx, cy, sx, sy;		/* centre à l'écran, échelles (sy : clignement compris) */
	float hw, hh, r, haut, pente, bas, cote;
	float coeur;			/* 0..1 : la forme devient un cœur */
	float marge;			/* la fenêtre calculée autour de la boîte (MARGE ; sans halo, MARGE_BORD hors du cœur) */
	float y0, y1;			/* étendue verticale, halo compris */
	float jx, jy;			/* la joue */
} oeil_final;

/* un petit signe à dessiner : sa forme, son centre et son échelle (unités de visage), son opacité, sa couleur, et
 * la boîte où il est calculé (lignes y0..y1, colonnes |x - bx| <= demi, comme la fenêtre d'un œil) */
enum { SF_GOUTTE, SF_Z, SF_QUESTION, SF_VEINE, SF_ETOILE, SF_COEUR, SF_TRAITS };
#define SIGNES_MAX	20
typedef struct
{
	int forme;
	float x, y, s, a;
	float cr, cg, cb;
	float tx[3][4];			/* SF_TRAITS : trois segments x0 y0 x1 y1, et leur demi-épaisseur dans s */
	float bx, demi, y0, y1;
} signe;

typedef struct
{
	oeil_final o[2];
	signe sg[SIGNES_MAX];
	int nsg;
	float joues;
	float ex, ey, unite;
	float rose;			/* 0..1 : les yeux virent au rose */
	float r, g, b, fr, fg, fb;
	float t_fond;			/* pixels : au-delà, le halo ne change plus le fond (ligne ()) */
} cadre;

#define HALO		0.018f		/* largeur du halo, en unités de visage (hauteur de l'écran = 1) */
/* -DPOCKET_VISAGE_HALO_FORCE=0 : sans halo (essai pour l'ESP32, 25/10/2026) ; sans l'option, rendu inchangé au bit près */
#ifdef POCKET_VISAGE_HALO_FORCE
#define HALO_FORCE	(POCKET_VISAGE_HALO_FORCE)
#else
#define HALO_FORCE	0.32f
#endif
/* la fenêtre calculée autour de la boîte de chaque œil (oeil_final.marge) : le halo ; sans halo, juste le bord et son
 * pixel de fondu (MARGE_BORD), sauf pour le cœur, dont les lobes dépassent de la boîte (avec la petite fenêtre, il
 * était coupé net sur les côtés), et quand il y a des joues (leurs lignes sont gardées d'après le bas de la fenêtre :
 * pendant un clignement, le bas des joues était coupé) */
#define MARGE		(HALO * 6)
#define MARGE_BORD	0.04f		/* (vérifié : images sans halo identiques à celles de la grande fenêtre, visage_halo) */

static float lisse (float x) { x = x < 0 ? 0 : x > 1 ? 1 : x; return x * x * (3 - 2 * x); }

/* ---- les petits signes autour des yeux -------------------------------------------------------- */
/* Comme les joues : un paramètre de la pose par signe (VP_LARME...), qui suit l'humeur par son ressort (il apparaît
 * avec un petit rebond, s'efface en douceur) ; dessiné par sa distance signée, à la couleur des yeux. Chaque signe
 * a son petit mouvement à lui (les larmes tombent, les « z » montent, la veine bat...), d'après le temps v->t. */
static float frac (float x) { return x - floorf (x); }

/* l'amplitude a (0..1, rebond compris) -> visible ou non, taille (0,5 .. 1, un peu plus au rebond) et opacité. Le seuil
 * 0,6 : à l'intensité 1 (0,5), même le rebond du ressort (+13 %) ne le passe pas */
static int signe_vu (float a, float *taille, float *alpha)
{
	float s = (a - 0.6f) / 0.4f;
	if (s <= 0) return 0;
	*taille = 0.5f + 0.5f * (s > 1.3f ? 1.3f : s);
	*alpha = lisse ((a - 0.6f) / 0.2f);
	return *alpha > 0.004f;
}

/* l'étendue d'une forme autour de son centre, en unités de la forme : demi-largeur, haut, bas */
static const float s_etendue[][3] =
{
	[SF_GOUTTE] = { 1.05f, -2.35f, 1.05f }, [SF_Z] = { 1.05f, -1.05f, 1.05f }, [SF_QUESTION] = { 0.7f, -1.05f, 0.98f },
	[SF_VEINE] = { 1.2f, -1.2f, 1.2f }, [SF_ETOILE] = { 1.05f, -1.05f, 1.05f }, [SF_COEUR] = { 0.7f, -0.65f, 0.55f },
};

static signe *signe_ajoute (cadre *c, int forme, float x, float y, float s, float a)
{
	if (c->nsg >= SIGNES_MAX || s <= 0.0005f || a <= 0.004f) return 0;
	signe *g = &c->sg[c->nsg++];
	g->forme = forme; g->x = x; g->y = y; g->s = s; g->a = a > 1 ? 1 : a;
	g->cr = c->r; g->cg = c->g; g->cb = c->b;
	/* la boîte : la forme, un pixel et demi de fondu, et le halo s'il y en a un */
	float m = 1.5f / c->unite + (HALO_FORCE > 0 ? HALO * 4 : 0);
	if (forme != SF_TRAITS)
	{
		const float *e = s_etendue[forme];
		g->bx = x; g->demi = e[0] * s + m;
		g->y0 = y + e[1] * s - m; g->y1 = y + e[2] * s + m;
	}
	return g;
}

static void prepare_signes (const pocket_visage *v, cadre *c)
{
	float tl, al, t = v->t;
	const oeil_final *od = &c->o[1];			/* l'œil de droite de l'écran : la plupart des signes s'y posent */
	float dw = od->hw * od->sx, dh = od->hh * od->sx;	/* (sx et non sy : un clignement ne les fait pas bouger) */
	c->nsg = 0;
	if (signe_vu (v->p[VP_LARME], &tl, &al))
	{
		/* les larmes, côté extérieur de chaque œil : elles sortent du bas de l'œil et tombent en accélérant. Le débit
		 * monte avec l'intensité : à 2, une goutte à la fois (les deux yeux en alternance) ; à 3, un filet de
		 * LARMES_FILET gouttes par œil, qui coulent plus loin (jusqu'en bas de l'écran) */
#define LARMES_FILET	5
		float filet = lisse ((v->p[VP_LARME] - 0.8f) / 0.2f), chute = 0.3f + 0.22f * filet;
		for (int i = 0; i < 2; i++)
		{
			const oeil_final *o = &c->o[i];
			float ow = o->hw * o->sx, oh = o->hh * o->sx;
			for (int k = 0; k < LARMES_FILET; k++)
			{
				float u = frac (t / 1.7f + 0.5f * i + (float) k / LARMES_FILET), ak = k == 0 ? 1 : filet;
				signe_ajoute (c, SF_GOUTTE, o->cx - o->cote * ow * 0.55f, o->cy + oh * 0.95f + chute * u * u,
					      0.036f * tl * (1 - 0.15f * filet) * lisse (u / 0.12f) * (1 - 0.25f * u),
					      al * ak * lisse ((1 - u) / 0.2f));
			}
		}
	}
	if (signe_vu (v->p[VP_SUEUR], &tl, &al))
	{
		/* la goutte de sueur : à côté de l'œil, elle glisse un peu, disparaît, revient */
		float u = frac (t / 2.4f);
		signe_ajoute (c, SF_GOUTTE, od->cx + dw + 0.055f, od->cy - dh * 0.5f + 0.07f * lisse (u / 0.8f),
			      0.036f * tl, al * lisse (u / 0.12f) * lisse ((1 - u) / 0.15f));
	}
	if (signe_vu (v->p[VP_VEINE], &tl, &al))	/* la veine de colère, au-dessus de l'œil : elle bat */
		signe_ajoute (c, SF_VEINE, od->cx + dw * 0.85f + 0.04f, od->cy - dh - 0.09f,
			      0.07f * tl * (1 + 0.15f * fabsf (sinf (2 * PI_F * 1.4f * t))), al);
	if (signe_vu (v->p[VP_ZZZ], &tl, &al))
		for (int k = 0; k < 3; k++)
		{
			/* trois « z » qui montent de biais en grossissant */
			float u = frac (t / 2.6f + k / 3.0f);
			signe_ajoute (c, SF_Z, od->cx + dw * 0.6f + 0.2f * u, od->cy - dh * 0.4f - 0.3f * u, (0.016f + 0.032f * u) * tl,
				      al * lisse (u / 0.15f) * lisse ((1 - u) / 0.25f));
		}
	if (signe_vu (v->p[VP_QUESTION], &tl, &al))	/* le « ? », qui flotte doucement */
		signe_ajoute (c, SF_QUESTION, od->cx + dw + 0.09f, od->cy - dh - 0.03f + 0.012f * sinf (2 * PI_F * 0.7f * t),
			      0.085f * tl, al);
	if (signe_vu (v->p[VP_ECLAT], &tl, &al))
		for (int i = 0; i < 2; i++)
		{
			/* trois traits d'éclat au-dessus de chaque œil, côté extérieur */
			const oeil_final *o = &c->o[i];
			float sgn = -o->cote, ow = o->hw * o->sx, oh = o->hh * o->sx;
			float cx = o->cx + sgn * ow * 0.85f, cy = o->cy - oh * 0.9f, p = 1 + 0.12f * sinf (2 * PI_F * 2 * t);
			float r0 = 0.04f * tl, r1 = r0 + 0.065f * tl * p, ep = 0.012f * tl;
			signe *g = signe_ajoute (c, SF_TRAITS, cx, cy, ep, al);
			if (!g) break;
			float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
			for (int k = 0; k < 3; k++)
			{
				float an = (-40.0f + 40.0f * k + sgn * 30.0f) * PI_F / 180, dx = sinf (an), dy = -cosf (an);
				float *s = g->tx[k];
				s[0] = cx + r0 * dx; s[1] = cy + r0 * dy; s[2] = cx + r1 * dx; s[3] = cy + r1 * dy;
				for (int j = 0; j < 4; j += 2)
				{
					if (s[j] < x0) x0 = s[j];
					if (s[j] > x1) x1 = s[j];
					if (s[j + 1] < y0) y0 = s[j + 1];
					if (s[j + 1] > y1) y1 = s[j + 1];
				}
			}
			float m = ep + 1.5f / c->unite + (HALO_FORCE > 0 ? HALO * 4 : 0);
			g->bx = (x0 + x1) * 0.5f; g->demi = (x1 - x0) * 0.5f + m; g->y0 = y0 - m; g->y1 = y1 + m;
		}
	if (signe_vu (v->p[VP_ETINCELLE], &tl, &al))	/* une étincelle à quatre branches, qui scintille */
		signe_ajoute (c, SF_ETOILE, od->cx + dw * 0.95f + 0.03f, od->cy - dh * 0.95f,
			      0.065f * tl * (0.85f + 0.25f * fabsf (sinf (2 * PI_F * 1.3f * t))), al);
	if (signe_vu (v->p[VP_PCOEUR], &tl, &al))
	{
		/* un petit cœur qui monte entre les yeux en ondulant */
		float u = frac (t / 2.2f);
		signe_ajoute (c, SF_COEUR, 0.025f * sinf (2 * PI_F * u), 0.3f - 0.66f * u, 0.1f * tl * lisse (u / 0.2f),
			      al * lisse (u / 0.2f) * lisse ((1 - u) / 0.3f));
	}
}

static void prepare (const pocket_visage *v, int l, int h, cadre *c)
{
	c->unite = (float) h;
	c->ex = l * 0.5f;
	c->ey = h * 0.5f;
	c->r = (float) ((v->couleur >> 16) & 255); c->g = (float) ((v->couleur >> 8) & 255); c->b = (float) (v->couleur & 255);
	c->fr = (float) ((v->fond >> 16) & 255); c->fg = (float) ((v->fond >> 8) & 255); c->fb = (float) (v->fond & 255);
	c->joues = v->p[VP_JOUES] < 0 ? 0 : v->p[VP_JOUES] > 1 ? 1 : v->p[VP_JOUES];
	c->rose = v->p[VP_ROSE] < 0 ? 0 : v->p[VP_ROSE] > 1 ? 1 : v->p[VP_ROSE];
	c->r += (255 - c->r) * c->rose; c->g += (95 - c->g) * c->rose; c->b += (165 - c->b) * c->rose;

	float gx = v->g[0] + v->p[VP_GX], gy = v->g[1] + v->p[VP_GY];
	float t = v->t, k = s_intensite[v->intensite - 1], e, ennui = v->regard_ext ? 0 : effet (v, HUMEUR_ENNUI);
	float dy = 0, dx = 0, ech = 1 + 0.012f * sinf (2 * PI_F * t / 4.0f);	/* respiration */
	if ((e = effet (v, HUMEUR_RIEUR)) > 0) dy -= 0.028f * e * fabsf (sinf (2 * PI_F * 2.6f * t));
	if (v->etat == VISAGE_PARLE) { dy -= 0.014f * fabsf (sinf (2 * PI_F * 2.3f * t)); ech *= 1 + 0.02f * sinf (2 * PI_F * 4.1f * t); }
	if ((e = effet (v, HUMEUR_COLERE)) > 0) dx += 0.003f * e * sinf (2 * PI_F * 17 * t);
	if ((e = effet (v, HUMEUR_PEUR)) > 0) { dx += 0.005f * e * sinf (2 * PI_F * 23 * t); dy += 0.003f * e * sinf (2 * PI_F * 19 * t); }
	if ((e = effet (v, HUMEUR_EXCITE)) > 0) dy -= 0.022f * e * fabsf (sinf (2 * PI_F * 3.4f * t));
	if (!v->va && v->humeur == HUMEUR_AMOUREUX)
	{
		/* un battement de cœur : deux pulsations par seconde, puis repos */
		float ph = fmodf (t, 1.1f);
		ech *= 1 + 0.06f * k * (expf (-ph * ph * 400) + 0.7f * expf (-(ph - 0.22f) * (ph - 0.22f) * 400));
	}
	if (v->etat == VISAGE_DORT) ech *= 1 + 0.03f * sinf (2 * PI_F * t / 5.0f);

	float b = 0;		/* le clignement : vite fermé, rouvert plus doucement */
	if (v->cligne > 0) b = v->cligne < 0.35f ? lisse (v->cligne / 0.35f) : 1 - lisse ((v->cligne - 0.35f) / 0.65f);

	for (int i = 0; i < 2; i++)
	{
		const float *e = v->p + i * VE_N;
		oeil_final *o = &c->o[i];
		float persp = 1 + 0.09f * gx * (i == 0 ? -1 : 1);	/* l'œil du côté regardé grossit un peu */
		if (persp < 0.7f) persp = 0.7f;
		o->sx = ech * persp;
		o->sy = ech * persp * (1 - 0.9f * b);
		o->cx = e[VE_X] * ech + gx * (0.07f + 0.09f * ennui) + dx;	/* (l'ennui : il regarde loin autour de lui) */
		o->cy = e[VE_Y] + gy * (0.05f + 0.05f * ennui) + dy;
		if (v->travail > 0.001f)
		{
			/* « je travaille » : chaque point saute à son tour, 1,6 saut par seconde chacun */
			float s = sinf (2 * PI_F * 1.6f * t + (i ? PI_F : 0));
			o->cy -= 0.07f * (s > 0 ? s : 0) * v->travail;
		}
		o->hw = (e[VE_W] > 0.01f ? e[VE_W] : 0.01f) * 0.5f;
		o->hh = (e[VE_H] > 0.01f ? e[VE_H] : 0.01f) * 0.5f;
		float rm = o->hw < o->hh ? o->hw : o->hh;
		o->r = e[VE_R] < 0 ? 0 : e[VE_R] > rm ? rm : e[VE_R];
		o->haut = e[VE_HAUT];
		o->pente = e[VE_PENTE];
		o->bas = e[VE_BAS];
		o->cote = i == 0 ? 1.0f : -1.0f;
		o->coeur = v->p[VP_COEUR] < 0 ? 0 : v->p[VP_COEUR] > 1 ? 1 : v->p[VP_COEUR];			/* le coin intérieur est vers le milieu */
		o->marge = HALO_FORCE > 0 || o->coeur > 0 || c->joues > 0.01f ? MARGE : MARGE_BORD;
		o->y0 = o->cy - o->hh * o->sy - o->marge;
		o->y1 = o->cy + o->hh * o->sy + o->marge;
		o->jx = o->cx - o->cote * 0.035f;
		o->jy = o->cy + o->hh * o->sx + 0.07f;
	}
	prepare_signes (v, c);
}

/* intersection adoucie : les coins taillés par les paupières sont arrondis */
#define ARRONDI		0.035f
static float smax (float a, float b, float k)
{
	float h = k - fabsf (a - b);
	return (a > b ? a : b) + (h > 0 ? h * h / (4 * k) : 0);
}

/* distance signée à un cœur (d'après I. Quilez) : pointe en (0, 0), y vers le haut, lobes jusqu'à y = 1,1 et
 * x = ±0,6 */
static float coeur_distance (float x, float y)
{
	x = fabsf (x);
	if (y + x > 1)
	{
		float dx = x - 0.25f, dy = y - 0.75f;
		return sqrtf (dx * dx + dy * dy) - 0.35355339f;
	}
	float d1 = x * x + (y - 1) * (y - 1);
	float m = 0.5f * (x + y > 0 ? x + y : 0), bx = x - m, by = y - m, d2 = bx * bx + by * by;
	return sqrtf (d1 < d2 ? d1 : d2) * (x - y > 0 ? 1.0f : -1.0f);
}

/* ---- les formes des signes (distances signées, d'après I. Quilez) ---- */
static float segment_distance (float px, float py, float ax, float ay, float bx, float by)
{
	float dx = bx - ax, dy = by - ay, t = ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy);
	t = t < 0 ? 0 : t > 1 ? 1 : t;
	float ex = px - ax - dx * t, ey = py - ay - dy * t;
	return sqrtf (ex * ex + ey * ey);
}

/* triangle isocèle : pointe en (0, 0), base en y = qy (y vers le bas), demi-base qx */
static float triangle_distance (float px, float py, float qx, float qy)
{
	px = fabsf (px);
	float t = (px * qx + py * qy) / (qx * qx + qy * qy), u = px / qx;
	t = t < 0 ? 0 : t > 1 ? 1 : t;
	u = u < 0 ? 0 : u > 1 ? 1 : u;
	float ax = px - qx * t, ay = py - qy * t, bx = px - qx * u, by = py - qy;
	float d1 = ax * ax + ay * ay, s1 = -(px * qy - py * qx), d2 = bx * bx + by * by, s2 = -(py - qy);
	float d = d1 < d2 ? d1 : d2, s = s1 < s2 ? s1 : s2;
	return s > 0 ? -sqrtf (d) : sqrtf (d);
}

/* losange de demi-diagonales bx, by */
static float losange_distance (float px, float py, float bx, float by)
{
	px = fabsf (px); py = fabsf (py);
	float h = ((bx - 2 * px) * bx - (by - 2 * py) * by) / (bx * bx + by * by);
	h = h < -1 ? -1 : h > 1 ? 1 : h;
	float dx = px - 0.5f * bx * (1 - h), dy = py - 0.5f * by * (1 + h), d = sqrtf (dx * dx + dy * dy);
	return px * by + py * bx - bx * by < 0 ? -d : d;
}

/* le « ? » : un crochet (arc de rayon 0,5 autour de (0, -0,35)), puis la tige ; et le point */
static const float s_question[][2] =
{
	{ -0.5f, -0.35f }, { -0.433f, -0.6f }, { -0.25f, -0.783f }, { 0, -0.85f }, { 0.25f, -0.783f }, { 0.433f, -0.6f },
	{ 0.5f, -0.35f }, { 0.433f, -0.1f }, { 0.25f, 0.083f }, { 0, 0.25f }, { 0, 0.45f },
};

/* distance signée au signe g, en unités de visage */
static float signe_distance (const signe *g, float x, float y)
{
	if (g->forme == SF_TRAITS)
	{
		float d = 1e9f;
		for (int k = 0; k < 3; k++)
		{
			float e = segment_distance (x, y, g->tx[k][0], g->tx[k][1], g->tx[k][2], g->tx[k][3]);
			if (e < d) d = e;
		}
		return d - g->s;
	}
	float u = (x - g->x) / g->s, w = (y - g->y) / g->s, d = 0;
	switch (g->forme)
	{
	case SF_GOUTTE:
	{
		/* le bout rond (rayon 1) en bas, la pointe en haut à 2,3 : un disque et le triangle tangent */
		float d1 = sqrtf (u * u + w * w) - 1, d2 = triangle_distance (u, w + 2.3f, 0.90053f, 1.86522f);
		d = d1 < d2 ? d1 : d2;
		break;
	}
	case SF_Z:
	{
		float a = segment_distance (u, w, -0.8f, -0.8f, 0.8f, -0.8f), b = segment_distance (u, w, 0.8f, -0.8f, -0.8f, 0.8f);
		float e = segment_distance (u, w, -0.8f, 0.8f, 0.8f, 0.8f);
		d = (a < b ? (a < e ? a : e) : (b < e ? b : e)) - 0.2f;
		break;
	}
	case SF_QUESTION:
	{
		float m = 1e9f;
		for (int k = 0; k + 1 < (int) (sizeof s_question / sizeof s_question[0]); k++)
		{
			float e = segment_distance (u, w, s_question[k][0], s_question[k][1], s_question[k + 1][0], s_question[k + 1][1]);
			if (e < m) m = e;
		}
		float p = sqrtf (u * u + (w - 0.78f) * (w - 0.78f)) - 0.17f;
		d = m - 0.15f < p ? m - 0.15f : p;
		break;
	}
	case SF_VEINE:
	{
		/* la veine des mangas : quatre arcs (rayon 0,62, ±50°) aux coins, tournés vers le milieu */
		d = 1e9f;
		for (int k = 0; k < 4; k++)
		{
			float sx = k & 1 ? 1.0f : -1.0f, sy = k & 2 ? 1.0f : -1.0f;
			float qx = u - 0.95f * sx, qy = w - 0.95f * sy, L = sqrtf (qx * qx + qy * qy), e;
			if (-(qx * sx + qy * sy) * 0.70710678f >= L * 0.64278761f) e = fabsf (L - 0.62f);
			else
			{
				/* hors de l'arc : la distance à son bout le plus proche */
				float ax = u - 0.3324f * sx, ay = w - 1.004f * sy, bx = u - 1.004f * sx, by = w - 0.3324f * sy;
				float e1 = ax * ax + ay * ay, e2 = bx * bx + by * by;
				e = sqrtf (e1 < e2 ? e1 : e2);
			}
			if (e < d) d = e;
		}
		d -= 0.15f;
		break;
	}
	case SF_ETOILE:
	{
		float a = losange_distance (u, w, 1, 0.26f), b = losange_distance (u, w, 0.26f, 1);
		d = a < b ? a : b;
		break;
	}
	case SF_COEUR:
		d = coeur_distance (u, 0.5f - w);
		break;
	}
	return d * g->s;
}

/* distance signée au bord de l'œil (négative dedans), en unités de visage, et la hauteur relative
 * 0 (haut) .. 1 (bas) pour le dégradé */
static float distance (const oeil_final *o, float x, float y, float *rel)
{
	float qx = (x - o->cx) / o->sx, qy = (y - o->cy) / o->sy;
	float ax = fabsf (qx) - (o->hw - o->r), ay = fabsf (qy) - (o->hh - o->r);
	float mx = ax > 0 ? ax : 0, my = ay > 0 ? ay : 0;
	float d = sqrtf (mx * mx + my * my) + (ax > ay ? (ax < 0 ? ax : 0) : (ay < 0 ? ay : 0)) - o->r;
	if (o->coeur > 0.001f)
	{
		/* l'œil se fond en cœur (amoureux) : un cœur de la taille de l'œil, centré */
		float S = 1.65f * (o->hw > o->hh ? o->hw : o->hh);
		float dc = coeur_distance (qx / S, (0.52f * S - qy) / S) * S;
		d += (dc - d) * o->coeur;
	}
	/* la paupière haute : un demi-plan (la pente fait descendre le coin intérieur ou extérieur) */
	float yl = -o->hh - ARRONDI + o->haut * (2 * o->hh + ARRONDI) + o->pente * qx * o->cote - fabsf (o->pente) * o->hw;
	float dl = (yl - qy) / sqrtf (1 + o->pente * o->pente);
	d = smax (d, dl, ARRONDI);
	/* la paupière basse : un disque qui remonte du bas et creuse l'œil en arc */
	float R = o->hw * 1.25f, cy = o->hh + ARRONDI - o->bas * (2 * o->hh + ARRONDI) + R;
	float db = R - sqrtf (qx * qx + (qy - cy) * (qy - cy));
	d = smax (d, db, ARRONDI);
	*rel = (qy + o->hh) / (2 * o->hh);
	float s = o->sx < o->sy ? o->sx : o->sy;
	return d * s;
}

static uint32_t pixel (const cadre *c, float x, float y)
{
	float r = c->fr, g = c->fg, b = c->fb;
	/* les joues (timide) : deux taches roses floues sous les yeux */
	if (c->joues > 0.01f)
		for (int i = 0; i < 2; i++)
		{
			float u = (x - c->o[i].jx) / 0.085f, w = (y - c->o[i].jy) / 0.04f;
			float a = 1 - (u * u + w * w);
			if (a > 0)
			{
				a = (1 - (1 - a) * (1 - a)) * c->joues * 0.75f;
				r += (255 - r) * a; g += (110 - g) * a; b += (150 - b) * a;
			}
		}
	float cov = 0, halo = 0, rel = 0;
	for (int i = 0; i < 2; i++)
	{
		const oeil_final *o = &c->o[i];
		if (y < o->y0 || y > o->y1) continue;
		if (fabsf (x - o->cx) > o->hw * o->sx + o->marge) continue;
		float re, d = distance (o, x, y, &re);
		float cv = 0.5f - d * c->unite;			/* un pixel de fondu au bord */
		cv = cv < 0 ? 0 : cv > 1 ? 1 : cv;
		if (cv > cov) { cov = cv; rel = re; }
		if (d > 0)
		{
			float hl = HALO_FORCE * expf (-d / HALO);
			if (hl > halo) halo = hl;
		}
	}
	if (halo > 0)
	{
		r += (c->r - r) * halo; g += (c->g - g) * halo; b += (c->b - b) * halo;
	}
	if (cov > 0)
	{
		/* l'œil : un peu plus clair en haut */
		float l = 0.28f * (1 - lisse (rel));
		float er = c->r + (255 - c->r) * l, eg = c->g + (255 - c->g) * l, eb = c->b + (255 - c->b) * l;
		r += (er - r) * cov; g += (eg - g) * cov; b += (eb - b) * cov;
	}
	/* les petits signes, par-dessus : un pixel de fondu, leur halo, à peine plus clairs que les yeux */
	for (int k = 0; k < c->nsg; k++)
	{
		const signe *sg = &c->sg[k];
		if (y < sg->y0 || y > sg->y1 || fabsf (x - sg->bx) > sg->demi) continue;
		float d = signe_distance (sg, x, y);
		if (d > 0 && HALO_FORCE > 0)
		{
			float hl = HALO_FORCE * expf (-d / HALO) * sg->a;
			r += (sg->cr - r) * hl; g += (sg->cg - g) * hl; b += (sg->cb - b) * hl;
		}
		float cv = 0.5f - d * c->unite;
		cv = (cv < 0 ? 0 : cv > 1 ? 1 : cv) * sg->a;
		if (cv > 0)
		{
			float er = sg->cr + (255 - sg->cr) * 0.14f, eg = sg->cg + (255 - sg->cg) * 0.14f, eb = sg->cb + (255 - sg->cb) * 0.14f;
			r += (er - r) * cv; g += (eg - g) * cv; b += (eb - b) * cv;
		}
	}
	uint32_t R = (uint32_t) (r + 0.5f), G = (uint32_t) (g + 0.5f), B = (uint32_t) (b + 0.5f);
	return 0xFF000000u | (R > 255 ? 255 : R) << 16 | (G > 255 ? 255 : G) << 8 | (B > 255 ? 255 : B);
}

/* ---- le dessin par segments ------------------------------------------------------------------- */
/*
 * Le même résultat que pixel () sur chaque pixel, au bit près, mais la distance n'est calculée qu'au bord
 * (24/10/2026, ../README_rendu.md : 13,8 -> 4,9 ms par image 320 x 170 sur le Pi). Deux propriétés :
 * - la distance est 1-lipschitzienne (rectangle arrondi, cœur et demi-plans exacts ; smax et le mélange vers
 *   le cœur sont des moyennes à poids positifs ; l'échelle min (sx, sy) ne l'agrandit pas) : un pixel à D
 *   pixels du bord garantit que ses k voisins sur la ligne sont à D -+ k. Une distance suffit donc pour sauter
 *   tout un segment de fond (D >= t_fond) ou pour remplir tout un segment plein (D < -0,5 : couverture 1, la
 *   couleur ne dépend que de la ligne) ;
 * - une fois quantifié, le halo ne change plus le fond au-delà de t_fond pixels (12,5 px en RGB565, 22 px en
 *   ARGB, pour le bleu sur noir) : seuil recalculé à chaque image d'après les couleurs.
 * Les zones où plusieurs termes se mêlent (les joues, le milieu où les fenêtres des deux yeux se touchent)
 * sont calculées par pixel () comme avant.
 */

/* pixel () quand seul l'œil o est évalué, hors des joues, sa distance d déjà calculée : les mêmes opérations */
static uint32_t pixel_un (const cadre *c, float d, float re)
{
	float r = c->fr, g = c->fg, b = c->fb;
	float cov = 0, halo = 0, rel = 0;
	float cv = 0.5f - d * c->unite;
	cv = cv < 0 ? 0 : cv > 1 ? 1 : cv;
	if (cv > cov) { cov = cv; rel = re; }
	if (d > 0)
	{
		float hl = HALO_FORCE * expf (-d / HALO);
		if (hl > halo) halo = hl;
	}
	if (halo > 0)
	{
		r += (c->r - r) * halo; g += (c->g - g) * halo; b += (c->b - b) * halo;
	}
	if (cov > 0)
	{
		float l = 0.28f * (1 - lisse (rel));
		float er = c->r + (255 - c->r) * l, eg = c->g + (255 - c->g) * l, eb = c->b + (255 - c->b) * l;
		r += (er - r) * cov; g += (eg - g) * cov; b += (eb - b) * cov;
	}
	uint32_t R = (uint32_t) (r + 0.5f), G = (uint32_t) (g + 0.5f), B = (uint32_t) (b + 0.5f);
	return 0xFF000000u | (R > 255 ? 255 : R) << 16 | (G > 255 ? 255 : G) << 8 | (B > 255 ? 255 : B);
}

/* la distance au bord (en pixels) au-delà de laquelle le halo ne change plus le fond une fois quantifié
 * (8 bits par couleur, ou 5-6-5) */
static float distance_fond (const cadre *c, int rgb565)
{
	static const int dec[3] = { 3, 2, 3 };
	const float col[3] = { c->r, c->g, c->b }, f[3] = { c->fr, c->fg, c->fb };
	float hm = 1;
	for (int k = 0; k < 3; k++)
	{
		int s = rgb565 ? dec[k] : 0, fi = (int) f[k];
		float lim;
		if (col[k] > f[k]) lim = ((float) (((fi >> s) + 1) << s) - 0.5f - f[k]) / (col[k] - f[k]);
		else if (col[k] < f[k]) lim = (f[k] + 0.5f - (float) ((fi >> s) << s)) / (f[k] - col[k]);
		else continue;
		if (lim < hm) hm = lim;
	}
	hm *= 0.99f;			/* (une marge pour les arrondis de expf) */
	float t = 0.5f;
	if (hm < HALO_FORCE)
	{
		float th = HALO * c->unite * logf (HALO_FORCE / hm);
		if (th > t) t = th;
	}
	return t + 0.05f;
}

/* les pixels de la ligne où |x - cx| <= demi, testés exactement comme pixel () le fait : [*a, *b) */
static int dans (const cadre *c, int px, float cx, float demi)
{
	float x = (px + 0.5f - c->ex) / c->unite;
	return !(fabsf (x - cx) > demi);
}

static void bornes (const cadre *c, float cx, float demi, int l, int *a, int *b)
{
	int m = (int) floorf ((cx - demi) * c->unite + c->ex - 0.5f);
	m = m < 0 ? 0 : m > l ? l : m;
	while (m > 0 && dans (c, m - 1, cx, demi)) m--;
	while (m < l && !dans (c, m, cx, demi)) m++;
	int n = (int) ceilf ((cx + demi) * c->unite + c->ex - 0.5f);
	n = n < m ? m : n > l ? l : n;
	while (n < l && dans (c, n, cx, demi)) n++;
	while (n > m && !dans (c, n - 1, cx, demi)) n--;
	*a = m; *b = n;
}

static int loin (const cadre *c, int px)
{
	float x = (px + 0.5f - c->ex) / c->unite;
	for (int i = 0; i < 2; i++)
		if (fabsf (x - c->o[i].cx) <= c->o[i].hw * c->o[i].sx + c->o[i].marge + 0.05f) return 0;
	return 1;
}

/* distance () avec ce qui ne dépend que de la ligne calculé une fois, dans le même ordre (même résultat) */
typedef struct { float qy, ay, my2, t1, t3, nrm, S, hy, R, dby2, re, s; } ligne_oeil;

static void prepare_ligne (const oeil_final *o, float y, ligne_oeil *L)
{
	L->qy = (y - o->cy) / o->sy;
	L->ay = fabsf (L->qy) - (o->hh - o->r);
	float my = L->ay > 0 ? L->ay : 0;
	L->my2 = my * my;
	L->t1 = -o->hh - ARRONDI + o->haut * (2 * o->hh + ARRONDI);
	L->t3 = fabsf (o->pente) * o->hw;
	L->nrm = sqrtf (1 + o->pente * o->pente);
	L->S = 1.65f * (o->hw > o->hh ? o->hw : o->hh);
	L->hy = (0.52f * L->S - L->qy) / L->S;
	L->R = o->hw * 1.25f;
	float cy = o->hh + ARRONDI - o->bas * (2 * o->hh + ARRONDI) + L->R;
	L->dby2 = (L->qy - cy) * (L->qy - cy);
	L->re = (L->qy + o->hh) / (2 * o->hh);
	L->s = o->sx < o->sy ? o->sx : o->sy;
}

static float distance_ligne (const oeil_final *o, const ligne_oeil *L, float x)
{
	float qx = (x - o->cx) / o->sx;
	float ax = fabsf (qx) - (o->hw - o->r);
	float mx = ax > 0 ? ax : 0;
	float d = sqrtf (mx * mx + L->my2) + (ax > L->ay ? (ax < 0 ? ax : 0) : (L->ay < 0 ? L->ay : 0)) - o->r;
	if (o->coeur > 0.001f)
	{
		float dc = coeur_distance (qx / L->S, L->hy) * L->S;
		d += (dc - d) * o->coeur;
	}
	float yl = L->t1 + o->pente * qx * o->cote - L->t3;
	float dl = (yl - L->qy) / L->nrm;
	d = smax (d, dl, ARRONDI);
	float db = L->R - sqrtf (qx * qx + L->dby2);
	d = smax (d, db, ARRONDI);
	return d * L->s;
}

/* les pixels [a, b) de la fenêtre de l'œil i, hors des intervalles [ca, cb) calculés par pixel () */
static void marche (const cadre *c, int i, float y, int a, int b, const int *ca, const int *cb, int nc, uint32_t *out)
{
	const oeil_final *o = &c->o[i];
	/* d'abord une borne : la distance vaut au moins celle à la boîte de l'œil (rectangle, et cœur) */
	float bx = o->hw, qa = -o->hh, qb = o->hh;
	if (o->coeur > 0.001f)
	{
		float S = 1.65f * (o->hw > o->hh ? o->hw : o->hh);
		if (0.61f * S > bx) bx = 0.61f * S;
		if (-0.59f * S < qa) qa = -0.59f * S;
		if (0.53f * S > qb) qb = 0.53f * S;
	}
	float qy = (y - o->cy) / o->sy, dy = qy < qa ? qa - qy : qy > qb ? qy - qb : 0;
	float tq = c->t_fond / ((o->sx < o->sy ? o->sx : o->sy) * c->unite);
	if (dy >= tq) return;
	float dx = bx + sqrtf (tq * tq - dy * dy);
	int xa = (int) floorf ((o->cx - dx * o->sx) * c->unite + c->ex - 0.5f) - 1;
	int xb = (int) ceilf ((o->cx + dx * o->sx) * c->unite + c->ex - 0.5f) + 2;
	if (xa > a) a = xa;
	if (xb < b) b = xb;

	ligne_oeil L;
	prepare_ligne (o, y, &L);
	uint32_t plein = 0;
	int plein_ok = 0, px = a;
	while (px < b)
	{
		int fin = b, saute = 0;
		for (int k = 0; k < nc; k++)
		{
			if (px >= ca[k] && px < cb[k]) { px = cb[k]; saute = 1; break; }
			if (ca[k] > px && ca[k] < fin) fin = ca[k];
		}
		if (saute) continue;
		float d = distance_ligne (o, &L, (px + 0.5f - c->ex) / c->unite), D = d * c->unite;
		if (D >= c->t_fond)
		{
			px += 1 + (int) ((D - c->t_fond) * 0.999f);	/* (le fond est déjà là) */
			continue;
		}
		if (D <= -0.51f)
		{
			int n = 1 + (int) ((-0.51f - D) * 0.999f);
			if (!plein_ok) { plein = pixel_un (c, d, L.re); plein_ok = 1; }
			if (n > fin - px) n = fin - px;
			for (int k = 0; k < n; k++) out[px + k] = plein;
			px += n;
			continue;
		}
		out[px++] = pixel_un (c, d, L.re);
	}
}

static void ligne (const pocket_visage *v, const cadre *c, int py, int l, uint32_t *out)
{
	float y = (py + 0.5f - c->ey) / c->unite;
	uint32_t fond = 0xFF000000u | v->fond;
	for (int x = 0; x < l; x++) out[x] = fond;
	int vide = 1;
	for (int i = 0; i < 2; i++)
		if (y >= c->o[i].y0 && y <= c->o[i].y1 + (c->joues > 0.01f ? 0.1f : 0)) vide = 0;
	for (int k = 0; k < c->nsg; k++)
		if (y >= c->sg[k].y0 && y <= c->sg[k].y1) vide = 0;
	if (vide) return;

	int a[2] = { 0, 0 }, b[2] = { 0, 0 };
	for (int i = 0; i < 2; i++)
		if (!(y < c->o[i].y0 || y > c->o[i].y1)) bornes (c, c->o[i].cx, c->o[i].hw * c->o[i].sx + c->o[i].marge, l, &a[i], &b[i]);
	/* par pixel (), comme avant : là où les deux yeux sont évalués, les joues, et les boîtes des signes (elles,
	 * même loin des yeux : signe vrai) */
	int ca[3 + SIGNES_MAX], cb[3 + SIGNES_MAX], signe_k[3 + SIGNES_MAX], nc = 0;
	int lo = a[0] > a[1] ? a[0] : a[1], hi = b[0] < b[1] ? b[0] : b[1];
	if (lo < hi) { ca[nc] = lo; cb[nc] = hi; signe_k[nc] = 0; nc++; }
	if (c->joues > 0.01f)
		for (int i = 0; i < 2; i++)
			if (fabsf (y - c->o[i].jy) < 0.045f)
			{
				bornes (c, c->o[i].jx, 0.09f, l, &ca[nc], &cb[nc]);
				signe_k[nc] = 0;
				if (ca[nc] < cb[nc]) nc++;
			}
	for (int k = 0; k < c->nsg; k++)
		if (y >= c->sg[k].y0 && y <= c->sg[k].y1)
		{
			int sa, sb, j;
			bornes (c, c->sg[k].bx, c->sg[k].demi, l, &sa, &sb);
			if (sa >= sb) continue;
			/* les boîtes qui se touchent sur la ligne (le filet de larmes) : une seule plage, chaque pixel une fois */
			for (j = 0; j < nc; j++)
				if (signe_k[j] && sa <= cb[j] && sb >= ca[j])
				{
					if (sa < ca[j]) ca[j] = sa;
					if (sb > cb[j]) cb[j] = sb;
					break;
				}
			if (j == nc) { ca[nc] = sa; cb[nc] = sb; signe_k[nc] = 1; nc++; }
		}
	for (int k = 0; k < nc; k++)
		for (int px = ca[k]; px < cb[k]; px++)
			if (signe_k[k] || !loin (c, px)) out[px] = pixel (c, (px + 0.5f - c->ex) / c->unite, y);
	for (int i = 0; i < 2; i++)
		if (a[i] < b[i]) marche (c, i, y, a[i], b[i], ca, cb, nc, out);
}

/* l'ancien dessin, pixel par pixel : la référence des essais (pocket_visage_*_ref) */
static void ligne_ref (const pocket_visage *v, const cadre *c, int py, int l, uint32_t *out)
{
	float y = (py + 0.5f - c->ey) / c->unite;
	int vide = 1;
	for (int i = 0; i < 2; i++)
		if (y >= c->o[i].y0 && y <= c->o[i].y1 + (c->joues > 0.01f ? 0.1f : 0)) vide = 0;
	for (int k = 0; k < c->nsg; k++)
		if (y >= c->sg[k].y0 && y <= c->sg[k].y1) vide = 0;
	uint32_t fond = 0xFF000000u | v->fond;
	if (vide)
	{
		for (int x = 0; x < l; x++) out[x] = fond;
		return;
	}
	for (int px = 0; px < l; px++)
	{
		float x = (px + 0.5f - c->ex) / c->unite;
		int loin = 1;
		for (int i = 0; i < 2 && loin; i++)
			if (fabsf (x - c->o[i].cx) <= c->o[i].hw * c->o[i].sx + c->o[i].marge + 0.05f) loin = 0;
		for (int k = 0; k < c->nsg && loin; k++)
			if (y >= c->sg[k].y0 && y <= c->sg[k].y1 && !(fabsf (x - c->sg[k].bx) > c->sg[k].demi)) loin = 0;
		out[px] = loin ? fond : pixel (c, x, y);
	}
}

typedef void (*fn_ligne) (const pocket_visage *v, const cadre *c, int py, int l, uint32_t *out);

void pocket_visage_ligne_argb (const pocket_visage *v, int y, int l, int h, uint32_t *out)
{
	cadre c;
	prepare (v, l, h, &c);
	c.t_fond = distance_fond (&c, 0);
	ligne (v, &c, y, l, out);
}

/* la ligne en ARGB d'abord, sur la pile (pas d'allocation ; les écrans du robot sont petits) */
static void ligne_rgb565 (fn_ligne f, const pocket_visage *v, const cadre *c, int y, int l, uint16_t *out)
{
	uint32_t tmp[1024];
	if (l > 1024) l = 1024;
	f (v, c, y, l, tmp);
	for (int i = 0; i < l; i++)
	{
		uint32_t p = tmp[i];
		out[i] = (uint16_t) (((p >> 8) & 0xF800) | ((p >> 5) & 0x07E0) | ((p >> 3) & 0x001F));
	}
}

void pocket_visage_ligne_rgb565 (const pocket_visage *v, int y, int l, int h, uint16_t *out)
{
	cadre c;
	prepare (v, l, h, &c);
	c.t_fond = distance_fond (&c, 1);
	ligne_rgb565 (ligne, v, &c, y, l, out);
}

static void lignes_rgb565 (fn_ligne f, const pocket_visage *v, int y0, int n, int l, int h, uint16_t *px, int pas)
{
	cadre c;
	prepare (v, l, h, &c);		/* la pose : une fois pour toutes les lignes */
	c.t_fond = distance_fond (&c, 1);
	for (int i = 0; i < n; i++) ligne_rgb565 (f, v, &c, y0 + i, l, px + (long) i * pas);
}

static void image_argb (fn_ligne f, const pocket_visage *v, uint32_t *px, int l, int h, int pas)
{
	cadre c;
	prepare (v, l, h, &c);
	c.t_fond = distance_fond (&c, 0);
	for (int y = 0; y < h; y++) f (v, &c, y, l, px + (long) y * pas);
}

void pocket_visage_lignes_rgb565 (const pocket_visage *v, int y0, int n, int l, int h, uint16_t *px, int pas)
{
	lignes_rgb565 (ligne, v, y0, n, l, h, px, pas);
}

void pocket_visage_image_argb (const pocket_visage *v, uint32_t *px, int l, int h, int pas)
{
	image_argb (ligne, v, px, l, h, pas);
}

void pocket_visage_lignes_rgb565_ref (const pocket_visage *v, int y0, int n, int l, int h, uint16_t *px, int pas)
{
	lignes_rgb565 (ligne_ref, v, y0, n, l, h, px, pas);
}

void pocket_visage_image_argb_ref (const pocket_visage *v, uint32_t *px, int l, int h, int pas)
{
	image_argb (ligne_ref, v, px, l, h, pas);
}

/* ---- la pose dessinée, pour un autre moteur de rendu (le GPU) ------------------------------------ */
void pocket_visage_parametres (const pocket_visage *v, int l, int h, float *p)
{
	cadre c;
	prepare (v, l, h, &c);
	for (int i = 0; i < 2; i++)
	{
		const oeil_final *o = &c.o[i];
		float *q = p + i * GVP_OEIL_N;
		q[GVP_CX] = o->cx; q[GVP_CY] = o->cy; q[GVP_SX] = o->sx; q[GVP_SY] = o->sy;
		q[GVP_HW] = o->hw; q[GVP_HH] = o->hh; q[GVP_R] = o->r; q[GVP_HAUT] = o->haut;
		q[GVP_PENTE] = o->pente; q[GVP_BAS] = o->bas; q[GVP_COTE] = o->cote; q[GVP_COEUR] = o->coeur;
		q[GVP_Y0] = o->y0; q[GVP_Y1] = o->y1; q[GVP_JX] = o->jx; q[GVP_JY] = o->jy;
	}
	p[GVP_JOUES] = c.joues; p[GVP_EX] = c.ex; p[GVP_EY] = c.ey; p[GVP_UNITE] = c.unite; p[GVP_ROSE] = c.rose;
	p[GVP_CR] = c.r; p[GVP_CG] = c.g; p[GVP_CB] = c.b; p[GVP_FR] = c.fr; p[GVP_FG] = c.fg; p[GVP_FB] = c.fb;
	p[GVP_HALO] = HALO; p[GVP_HALO_FORCE] = HALO_FORCE; p[GVP_MARGE] = MARGE; p[GVP_ARRONDI] = ARRONDI;
	p[GVP_SIGNES] = (float) c.nsg;
}

/* ---- noms ------------------------------------------------------------------------------------- */
static const char *const s_etats[VISAGE_ETAT_COUNT] = { "veille", "ecoute", "reflechit", "parle", "dort" };

const char *pocket_visage_etat_nom (int etat)
{
	return etat >= 0 && etat < VISAGE_ETAT_COUNT ? s_etats[etat] : s_etats[0];
}

int pocket_visage_etat_de_nom (const char *nom)
{
	if (nom == 0) return -1;
	for (int i = 0; i < VISAGE_ETAT_COUNT; i++) if (strcmp (nom, s_etats[i]) == 0) return i;
	return -1;
}
