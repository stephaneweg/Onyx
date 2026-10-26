/* PocketQI (github.com/stephaneweg/PocketQI, commit 060b797), unchanged but for these two lines: MIT License,
 * Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md). */
/*
 * pocket_humeur.c -- Pocket alpha : l'humeur du robot (voir pocket_humeur.h)
 */
#include "pocket_humeur.h"
#include <string.h>
#include <math.h>

static const char *const s_names[HUMEUR_COUNT] =
{
	"neutre", "joyeux", "rieur", "triste", "surpris", "curieux", "fatigue", "fier", "timide", "colere",
	"peur", "degout", "ennui", "malicieux", "perplexe", "mefiant", "excite", "clin", "amoureux"
};

/* d'autres mots pour les mêmes humeurs (déjà sans accent ni espace, comme après fold) */
static const struct { const char *mot; pocket_humeur h; } s_alias[] =
{
	{ "content", HUMEUR_JOYEUX }, { "heureux", HUMEUR_JOYEUX }, { "heureuse", HUMEUR_JOYEUX }, { "joyeuse", HUMEUR_JOYEUX },
	{ "rieuse", HUMEUR_RIEUR }, { "surprise", HUMEUR_SURPRIS }, { "curieuse", HUMEUR_CURIEUX }, { "fatiguee", HUMEUR_FATIGUE },
	{ "fiere", HUMEUR_FIER }, { "fache", HUMEUR_COLERE }, { "fachee", HUMEUR_COLERE }, { "effraye", HUMEUR_PEUR },
	{ "effrayee", HUMEUR_PEUR }, { "apeure", HUMEUR_PEUR }, { "degoute", HUMEUR_DEGOUT }, { "degoutee", HUMEUR_DEGOUT },
	{ "ennuye", HUMEUR_ENNUI }, { "ennuyee", HUMEUR_ENNUI }, { "blase", HUMEUR_ENNUI }, { "malicieuse", HUMEUR_MALICIEUX },
	{ "coquin", HUMEUR_MALICIEUX }, { "confus", HUMEUR_PERPLEXE }, { "perdu", HUMEUR_PERPLEXE }, { "mefiante", HUMEUR_MEFIANT },
	{ "suspicieux", HUMEUR_MEFIANT }, { "excitee", HUMEUR_EXCITE }, { "enthousiaste", HUMEUR_EXCITE },
	{ "clindoeil", HUMEUR_CLIN }, { "complice", HUMEUR_CLIN }, { "amoureuse", HUMEUR_AMOUREUX }, { "tendre", HUMEUR_AMOUREUX },
	/* en anglais (l'API des apps BASIC est en anglais) */
	{ "neutral", HUMEUR_NEUTRE }, { "happy", HUMEUR_JOYEUX }, { "laughing", HUMEUR_RIEUR }, { "sad", HUMEUR_TRISTE },
	{ "surprised", HUMEUR_SURPRIS }, { "curious", HUMEUR_CURIEUX }, { "tired", HUMEUR_FATIGUE }, { "proud", HUMEUR_FIER },
	{ "shy", HUMEUR_TIMIDE }, { "angry", HUMEUR_COLERE }, { "afraid", HUMEUR_PEUR }, { "scared", HUMEUR_PEUR },
	{ "fear", HUMEUR_PEUR }, { "disgusted", HUMEUR_DEGOUT }, { "bored", HUMEUR_ENNUI }, { "mischievous", HUMEUR_MALICIEUX },
	{ "puzzled", HUMEUR_PERPLEXE }, { "confused", HUMEUR_PERPLEXE }, { "suspicious", HUMEUR_MEFIANT },
	{ "excited", HUMEUR_EXCITE }, { "wink", HUMEUR_CLIN }, { "love", HUMEUR_AMOUREUX }, { "inlove", HUMEUR_AMOUREUX },
};

/* le cercle des deux axes, par angle croissant (degrés ; 0 : positif, 90 : excité) */
static const struct { short angle; pocket_humeur h; } s_cercle[] =
{
	{ 10, HUMEUR_JOYEUX }, { 30, HUMEUR_RIEUR }, { 45, HUMEUR_MALICIEUX }, { 60, HUMEUR_EXCITE },
	{ 75, HUMEUR_CURIEUX }, { 90, HUMEUR_SURPRIS }, { 105, HUMEUR_PERPLEXE }, { 120, HUMEUR_PEUR },
	{ 140, HUMEUR_COLERE }, { 160, HUMEUR_DEGOUT }, { 175, HUMEUR_MEFIANT }, { 205, HUMEUR_TRISTE },
	{ 235, HUMEUR_ENNUI }, { 260, HUMEUR_FATIGUE }, { 290, HUMEUR_TIMIDE }, { 330, HUMEUR_FIER },
};
#define CERCLE_N	((int) (sizeof s_cercle / sizeof s_cercle[0]))
/* les distances des intensités 1..3 (les facteurs de pocket_visage.c) */
static const float s_rayon[3] = { 0.5f, 0.78f, 1.0f };

static pocket_humeur s_current = HUMEUR_NEUTRE;
static int s_intensite = 1;
static pocket_humeur_sink *s_sink;
static unsigned s_delai = HUMEUR_DELAI_NEUTRE_MS;
static int s_nouvelle;			/* une humeur posée, pas encore vue par pocket_humeur_tick */
static unsigned long s_depuis;		/* l'heure (ms) où tick l'a vue */
static void *s_ctx;
static int s_va;			/* l'humeur courante a été posée par ses deux axes */
static float s_valence, s_eveil;

const char *pocket_humeur_list (void)
{
	return "neutre joyeux rieur triste surpris curieux fatigue fier timide colere peur degout ennui malicieux perplexe "
	       "mefiant excite clin amoureux";
}

const char *pocket_humeur_name (pocket_humeur h)
{
	return (unsigned) h < HUMEUR_COUNT ? s_names[h] : s_names[0];
}

/* minuscules ASCII, lettres accentuées UTF-8 courantes -> sans accent, espaces et tirets retirés */
static void fold (const char *in, char *out, size_t cap)
{
	size_t n = 0;
	const unsigned char *p = (const unsigned char *) in;
	while (*p != 0 && n + 1 < cap)
	{
		unsigned c = *p++;
		if (c == 0xC3 && *p != 0)
		{
			unsigned d = *p++ | 0x20;		/* (majuscule -> minuscule) */
			if (d >= 0xA0 && d <= 0xA5) c = 'a';
			else if (d == 0xA7) c = 'c';
			else if (d >= 0xA8 && d <= 0xAB) c = 'e';
			else if (d >= 0xAC && d <= 0xAF) c = 'i';
			else if (d >= 0xB2 && d <= 0xB6) c = 'o';
			else if (d >= 0xB9 && d <= 0xBC) c = 'u';
			else continue;
		}
		else if (c == 0xC5 && (*p == 0x93 || *p == 0x92) && n + 2 < cap)	/* œ Œ -> oe */
		{
			p++;
			out[n++] = 'o';
			c = 'e';
		}
		else if (c == 0xE2 && p[0] == 0x80 && p[1] == 0x99) { p += 2; continue; }	/* ’ */
		else if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
		else if (c == ' ' || c == '-' || c == '_' || c == '\'') continue;
		out[n++] = (char) c;
	}
	out[n] = 0;
}

pocket_humeur pocket_humeur_from_name (const char *name, int *known)
{
	char f[32];
	fold (name != 0 ? name : "", f, sizeof f);
	const char *p = strncmp (f, "en", 2) == 0 && strcmp (f + 2, "colere") == 0 ? f + 2 : f;	/* « en colère » */
	for (int i = 0; i < HUMEUR_COUNT; i++)
	{
		if (strcmp (p, s_names[i]) == 0)
		{
			if (known != 0) *known = 1;
			return (pocket_humeur) i;
		}
	}
	for (unsigned i = 0; i < sizeof s_alias / sizeof s_alias[0]; i++)
	{
		if (strcmp (p, s_alias[i].mot) == 0)
		{
			if (known != 0) *known = 1;
			return s_alias[i].h;
		}
	}
	if (known != 0) *known = 0;
	return HUMEUR_NEUTRE;
}

void pocket_humeur_set_sink (pocket_humeur_sink *sink, void *ctx)
{
	s_sink = sink;
	s_ctx = ctx;
}

void pocket_humeur_set (pocket_humeur h, int intensite)
{
	if ((unsigned) h >= HUMEUR_COUNT) h = HUMEUR_NEUTRE;
	if (intensite < HUMEUR_INTENSITE_MIN) intensite = HUMEUR_INTENSITE_MIN;
	if (intensite > HUMEUR_INTENSITE_MAX) intensite = HUMEUR_INTENSITE_MAX;
	s_current = h;
	s_intensite = intensite;
	s_va = 0;
	s_nouvelle = 1;
	if (s_sink != 0) s_sink (h, intensite, s_ctx);
}

/* ---- les deux axes ------------------------------------------------------------------------------- */
int pocket_humeur_angle (pocket_humeur h)
{
	for (int i = 0; i < CERCLE_N; i++) if (s_cercle[i].h == h) return s_cercle[i].angle;
	return -1;
}

static float borne (float x)
{
	return x != x ? 0 : x < -HUMEUR_AXE_MAX ? -HUMEUR_AXE_MAX : x > HUMEUR_AXE_MAX ? HUMEUR_AXE_MAX : x;	/* (NaN : 0) */
}

void pocket_humeur_voisines (float valence, float eveil, pocket_humeur *a, pocket_humeur *b, float *t, float *k)
{
	float x = borne (valence) / HUMEUR_AXE_MAX, y = borne (eveil) / HUMEUR_AXE_MAX;
	float r = sqrtf (x * x + y * y);
	float deg = r > 0 ? atan2f (y, x) * (180.0f / 3.14159265f) : 0;
	if (deg < 0) deg += 360;
	/* le premier angle au-delà du point : b ; celui d'avant : a (en faisant le tour) */
	int j = 0;
	while (j < CERCLE_N && s_cercle[j].angle <= deg) j++;
	int i = (j + CERCLE_N - 1) % CERCLE_N;
	j %= CERCLE_N;
	float da = deg - s_cercle[i].angle, ab = (float) (s_cercle[j].angle - s_cercle[i].angle);
	if (da < 0) da += 360;
	if (ab <= 0) ab += 360;
	*a = s_cercle[i].h;
	*b = s_cercle[j].h;
	*t = da / ab;
	*k = r > 1 ? 1 : r;
}

pocket_humeur pocket_humeur_proche (float valence, float eveil, int *intensite)
{
	pocket_humeur a, b;
	float t, k;
	pocket_humeur_voisines (valence, eveil, &a, &b, &t, &k);
	int n = k < 0.64f ? 1 : k < 0.89f ? 2 : 3;	/* (à mi-chemin des distances 0,5 / 0,78 / 1) */
	if (intensite != 0) *intensite = k < 0.25f ? 1 : n;
	return k < 0.25f ? HUMEUR_NEUTRE : t < 0.5f ? a : b;
}

void pocket_humeur_set_va (float valence, float eveil)
{
	int n;
	s_current = pocket_humeur_proche (valence, eveil, &n);
	s_intensite = n;
	s_va = 1;
	s_valence = borne (valence);
	s_eveil = borne (eveil);
	s_nouvelle = 1;
	if (s_sink != 0) s_sink (s_current, n, s_ctx);
}

int pocket_humeur_get_va (float *valence, float *eveil)
{
	float v = s_valence, e = s_eveil;
	if (!s_va)
	{
		int a = pocket_humeur_angle (s_current);
		float r = a < 0 ? 0 : HUMEUR_AXE_MAX * s_rayon[s_intensite - 1];
		v = r * cosf (a * (3.14159265f / 180));
		e = r * sinf (a * (3.14159265f / 180));
	}
	if (valence != 0) *valence = v;
	if (eveil != 0) *eveil = e;
	return s_va;
}

/* ---- MOOD: ------------------------------------------------------------------------------------- */
/* un nombre (signe, chiffres, décimales après . ou ,) ; 0 si *p n'en commence pas un */
static int nombre (const char **p, float *x)
{
	const char *s = *p;
	float sg = 1, v = 0, d = 0.1f;
	int n = 0;
	if (*s == '-' || *s == '+') { if (*s == '-') sg = -1; s++; }
	while (*s >= '0' && *s <= '9') { v = v * 10 + (*s++ - '0'); n++; }
	if (*s == '.' && s[1] >= '0' && s[1] <= '9')		/* (STR$(0.5) donne « .5 ») */
		for (s++; *s >= '0' && *s <= '9'; s++) { v += (*s - '0') * d; d /= 10; n++; }
	if (n == 0) return 0;
	*x = sg * v;
	*p = s;
	return 1;
}

int pocket_humeur_commande (const char *ligne)
{
	const char *p = ligne != 0 ? ligne : "";
	while (*p == ' ' || *p == '\t') p++;
	float v, e;
	if (nombre (&p, &v))
	{
		while (*p == ' ' || *p == '\t' || *p == ',' || *p == ';') p++;
		if (!nombre (&p, &e)) return 0;
		pocket_humeur_set_va (v, e);
		return 1;
	}
	/* un nom (plusieurs mots possibles : « en colère ») puis l'intensité */
	char nom[40];
	unsigned n = 0;
	const char *fin = p;
	for (const char *q = p; *q != 0 && *q != '\r' && *q != '\n'; q++)
		if (!((*q >= '0' && *q <= '9') || *q == ' ' || *q == '\t')) fin = q + 1;
	while (p < fin && n + 1 < sizeof nom) nom[n++] = *p++;
	nom[n] = 0;
	while (*p == ' ' || *p == '\t') p++;
	int known = 0, k = 3;
	if (*p >= '1' && *p <= '9') k = *p - '0';
	pocket_humeur h = pocket_humeur_from_name (nom, &known);
	if (!known) return 0;
	pocket_humeur_set (h, k);
	return 1;
}

static char *entier (char *o, char *fin, float x)
{
	int i = (int) (x < 0 ? x - 0.5f : x + 0.5f);
	char t[8];
	int n = 0;
	unsigned u = (unsigned) (i < 0 ? -i : i);
	do { t[n++] = (char) ('0' + u % 10); u /= 10; } while (u != 0 && n < 7);
	if (i < 0 && o < fin) *o++ = '-';
	while (n > 0 && o < fin) *o++ = t[--n];
	return o;
}

void pocket_humeur_texte (char *out, unsigned cap)
{
	if (cap == 0) return;
	float v, e;
	pocket_humeur_get_va (&v, &e);
	char *o = out, *fin = out + cap - 1;
	o = entier (o, fin, v);
	if (o < fin) *o++ = ' ';
	o = entier (o, fin, e);
	if (o < fin) *o++ = ' ';
	for (const char *s = pocket_humeur_name (s_current); *s != 0 && o < fin; ) *o++ = *s++;
	if (o < fin) *o++ = ' ';
	if (o < fin) *o++ = (char) ('0' + s_intensite);
	*o = 0;
}

void pocket_humeur_set_delai (unsigned delai_ms)
{
	s_delai = delai_ms;
}

void pocket_humeur_tick (unsigned long now_ms)
{
	if (s_nouvelle) { s_nouvelle = 0; s_depuis = now_ms; return; }
	if (s_delai == 0 || (!s_va && s_current == HUMEUR_NEUTRE && s_intensite == 1)) return;
	if (now_ms - s_depuis >= s_delai) pocket_humeur_set (HUMEUR_NEUTRE, 1);
}

pocket_humeur pocket_humeur_get (int *intensite)
{
	if (intensite != 0) *intensite = s_intensite;
	return s_current;
}
