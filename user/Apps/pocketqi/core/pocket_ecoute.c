/* PocketQI (github.com/stephaneweg/PocketQI, commit 4d2a9ba), unchanged but for these two lines: MIT License,
 * Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md). */
/*
 * pocket_ecoute.c -- l'automate d'écoute permanente (voir pocket_ecoute.h).
 *   Détection de parole à l'énergie : la même que pocketqi_win (plancher suivi, début : 3 blocs à +10 dB,
 *   fin : 22 blocs (0,7 s) sous plancher + 6 dB, 15 s au plus, 8 blocs de parole au moins, 4 en relance).
 */
#include "pocket_ecoute.h"
#include <string.h>

#define START_BLOCKS 3
#define END_BLOCKS 22
#define MIN_BLOCKS 8
/* en relance (on répond à une question de Pocket : « Tu veux la suite ? », la confirmation d'une app) : pas de petit son
 * dont l'écho serait à ignorer, et la réponse est souvent un mot bref ; un « oui » net (~0,2 s) était jeté comme un
 * claquement à 8 blocs, l'app attendait ses 10 s et répondait « pas de réponse » (kmsg du 25/10/2026) */
#define MIN_BLOCKS_RELANCE 4
#define PAUSE_BLOCKS 12                 /* 0,4 s sans parole après « dis Pocket » : petit son, le mot est jeté */

/* 10 log10 (énergie moyenne) en dBFS, sans libm : log2 par l'exposant + mantisse (précision ~0,1 dB) */
static float block_db (const short *b)
{
	double e = 0;
	for (int i = 0; i < GEC_BLOCK; i++) e += (double) b[i] * b[i];
	e /= GEC_BLOCK * 32768.0 * 32768.0;
	if (e < 1e-12) return -120;
	int ex = 0;
	while (e < 0.5) { e *= 2; ex--; }
	while (e >= 1) { e /= 2; ex++; }
	/* e dans [0,5 ; 1) : log2 (e) ~ polynôme */
	double x = e * 2 - 1;                       /* [0 ; 1) : log2 (1 + x) */
	double l2 = x * (1.4425449 + x * (-0.7181452 + x * (0.4575485 + x * (-0.2779042 + x * 0.0959533)))) - 1;
	return (float) (3.0103 * (l2 + ex));        /* 10 log10 (2) = 3,0103 */
}

/* plancher de bruit : descend vite (30 % par bloc) et remonte lentement (2 %), jamais sous FLOOR_MIN. Avant : il
 * prenait tout de suite le niveau d'un bloc plus faible ; un seul bloc de silence numérique (micro-coupure du flux,
 * -95 dB) le faisait plonger, plus rien n'était « silence » ensuite, et la phrase courait jusqu'à 15 s en avalant les
 * suivantes (mal transcrites). */
#define FLOOR_MIN -70.0f
static void track_floor (pocket_ecoute *e, float db)
{
	float d = db < FLOOR_MIN ? FLOOR_MIN : db;
	e->floor_db += (d < e->floor_db ? 0.3f : 0.02f) * (d - e->floor_db);
}

static void append (pocket_ecoute *e, const short *p, size_t k)
{
	if (e->n + k > e->cap) k = e->cap - e->n;
	memcpy (e->pcm + e->n, p, k * sizeof (short));
	e->n += k;
}

/* pas encore de parole : ne garder que le pré-roulement (les derniers GEC_PRE_BLOCKS blocs) */
static void slide (pocket_ecoute *e)
{
	size_t keep = GEC_PRE_BLOCKS * GEC_BLOCK;
	if (e->n > keep) { memmove (e->pcm, e->pcm + e->n - keep, keep * sizeof (short)); e->n = keep; }
}

static void listen_for_phrase (pocket_ecoute *e, int state, float secs)
{
	e->state = e->listen_state = state;
	e->loud = e->quiet = e->nblk = 0;
	e->wait_blocks = (int) (secs * 16000 / GEC_BLOCK);
	e->peak_db = -120;
}

void pocket_ecoute_init (pocket_ecoute *e, short *buf, size_t cap, float thr)
{
	memset (e, 0, sizeof *e);
	e->eveil = 1; e->wait_s = 6; e->relance_s = 8; e->lead_s = 1.1f; e->since_wake = -1;
	pocket_eveil_init (&e->ge, thr);
	e->pcm = buf; e->cap = cap;
	e->floor_db = -60;
	e->state = GEC_VEILLE;
}

void pocket_ecoute_veille (pocket_ecoute *e)
{
	e->n = 0; e->bip_due = 0; e->since_wake = -1;
	if (!e->eveil) { listen_for_phrase (e, GEC_ATTENTE, 0); return; }
	e->state = GEC_VEILLE;
}

void pocket_ecoute_relance (pocket_ecoute *e)
{
	e->n = 0;
	if (!e->eveil) { listen_for_phrase (e, GEC_ATTENTE, 0); return; }
	if (e->relance_s <= 0) { e->state = GEC_VEILLE; return; }
	listen_for_phrase (e, GEC_RELANCE, e->relance_s);
}

void pocket_ecoute_reprise (pocket_ecoute *e)
{
	pocket_eveil_reset (&e->ge);
	if (e->det_reset) e->det_reset (e->det_ctx);
	e->nhist = 0; e->since_wake = -1; e->bip_due = 0;
	if (e->state == GEC_PHRASE) e->state = e->eveil ? GEC_VEILLE : GEC_ATTENTE;
	e->n = 0; e->loud = e->quiet = 0;
}

const char *pocket_ecoute_etat (const pocket_ecoute *e)
{
	static const char *const s[] = { "veille", "attente de la phrase", "phrase", "relance" };
	return s[e->state];
}

int pocket_ecoute_block (pocket_ecoute *e, const short *blk)
{
	float db = block_db (blk);
	int ev = GEC_RIEN;
	if (e->state == GEC_VEILLE) {
		track_floor (e, db);
		int hs = 0; float pr = 0;
		int hit = e->det ? e->det (e->det_ctx, blk, &hs, &pr) : pocket_eveil_process (&e->ge, blk, GEC_BLOCK);
		if (hit && !e->det) { hs = e->ge.hit_sample; pr = e->ge.prob; }
		if (hit) {
			/* réveillé : la phrase commence lead_s avant la détection, dans l'historique ou ce bloc */
			long lead = (long) (e->lead_s * 16000), h = hs - lead;
			long oldest = -(long) e->nhist * GEC_BLOCK;
			if (h < oldest) h = oldest;
			e->n = 0;
			for (int k = e->nhist; k > 0; k--) {             /* les blocs d'historique, du plus ancien */
				long s0 = -(long) k * GEC_BLOCK;
				if (h >= s0 + GEC_BLOCK) continue;
				const short *src = e->hist[(e->ihist - k + GEC_HIST_BLOCKS) % GEC_HIST_BLOCKS];
				long from = h > s0 ? h - s0 : 0;
				append (e, src + from, (size_t) (GEC_BLOCK - from));
			}
			if (h < 0) h = 0;
			append (e, blk + h, (size_t) (GEC_BLOCK - h));
			e->wake_prob = pr; e->wakes++;
			listen_for_phrase (e, GEC_ATTENTE, e->wait_s);
			e->since_wake = 0; e->bip_due = 1;
			/* la parole continue peut-être déjà (« dis Pocket, quel... ») : ce bloc compte pour le début */
			e->loud = db > e->floor_db + 10 && db > -55 ? 1 : 0;
			ev = GEC_REVEIL;
		}
		memcpy (e->hist[e->ihist], blk, sizeof e->hist[0]);
		e->ihist = (e->ihist + 1) % GEC_HIST_BLOCKS;
		if (e->nhist < GEC_HIST_BLOCKS) e->nhist++;
		return ev;
	}
	if (!e->eveil && !e->det && e->state == GEC_ATTENTE)   /* sans mot de réveil : le réseau tourne quand même */
		pocket_eveil_process (&e->ge, blk, GEC_BLOCK);        /* (mesure, et pour rester chaud) */
	e->nhist = 0;                                       /* (hors veille : l'historique repart de zéro) */
	if (e->state == GEC_ATTENTE || e->state == GEC_RELANCE) {
		append (e, blk, GEC_BLOCK);
		e->loud = db > e->floor_db + 10 && db > -55 ? e->loud + 1 : 0;
		if (e->loud >= START_BLOCKS) {
			e->state = GEC_PHRASE; e->nblk = e->loud; e->quiet = 0; e->since_wake = -1;
			e->peak_db = db;
			return GEC_PAROLE;
		}
		track_floor (e, db);
		/* hors de veille, RIEN n'est coupé (décision du client, 23/10) : tout depuis le réveil (ou le début de la
		 * relance) part, silences compris ; après une pause sans parole, un petit son. Sans mot de réveil (eveil = 0),
		 * seul le pré-roulement est gardé avant la parole (on écoute en permanence). */
		if (e->since_wake >= 0 && ++e->since_wake >= PAUSE_BLOCKS) e->since_wake = -1;
		if (e->since_wake < 0 && e->bip_due) { e->bip_due = 0; ev = GEC_BIP; }
		if (!e->eveil) slide (e);
		if (e->wait_blocks > 0 && --e->wait_blocks == 0) {
			e->state = GEC_VEILLE; e->n = 0; e->since_wake = -1;
			return GEC_FIN;
		}
		return ev;
	}
	/* GEC_PHRASE */
	append (e, blk, GEC_BLOCK);
	if (db > e->peak_db) e->peak_db = db;
	e->nblk++;
	e->quiet = db < e->floor_db + 6 ? e->quiet + 1 : 0;
	if (e->quiet >= END_BLOCKS || e->n >= e->cap) {
		int min_blocks = e->listen_state == GEC_RELANCE ? MIN_BLOCKS_RELANCE : MIN_BLOCKS;
		if (e->nblk - e->quiet < min_blocks) {              /* un claquement, l'écho du petit son : on attend encore */
			if (e->eveil && e->wait_blocks <= 0) { e->state = GEC_VEILLE; e->n = 0; return GEC_FIN; }
			e->state = e->listen_state;
			e->loud = 0;
			if (!e->eveil) slide (e);                       /* (hors de veille : on garde tout) */
			return GEC_RIEN;
		}
		e->bip_due = 0;
		/* fin : on retire le silence final au-delà de 0,3 s */
		size_t tail = (size_t) (e->quiet > 9 ? e->quiet - 9 : 0) * GEC_BLOCK;
		if (tail < e->n) e->n -= tail;
		e->state = e->eveil ? GEC_VEILLE : GEC_ATTENTE;    /* l'hôte rouvre la relance après sa réponse */
		if (!e->eveil) {
			/* (la crête de la phrase reste lisible : listen_for_phrase la remettait à -120 avant que l'hôte la lise,
			 *  et sans mot de réveil chaque phrase était refusée comme « trop faible » ; trouvé avec l'émulateur) */
			float pic = e->peak_db;
			listen_for_phrase (e, GEC_ATTENTE, 0);
			e->peak_db = pic;
		}
		return GEC_PHRASE_PRETE;
	}
	return GEC_RIEN;
}

/* ---- texte ---------------------------------------------------------------------------------------------- */
static int lower (int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

/* lettres d'un mot, sans accents ni tirets (« Galô » -> galo, « dis-galo » -> disgalo) ; renvoie la longueur en octets */
static size_t fold_word (const char *p, char *w, size_t n, size_t *k)
{
	size_t i = 0;
	*k = 0;
	for (;;) {
		unsigned char c = (unsigned char) p[i];
		char o = 0;
		if (c == 0xC3 && p[i + 1]) {                    /* à â ä é è ê ë î ï ô ö ù û ü ç (minuscules et majuscules) */
			unsigned char d = (unsigned char) p[i + 1] | 0x20;
			if (d >= 0xA0 && d <= 0xA6) o = 'a';
			else if (d >= 0xA8 && d <= 0xAB) o = 'e';
			else if (d >= 0xAC && d <= 0xAF) o = 'i';
			else if (d >= 0xB2 && d <= 0xB6) o = 'o';
			else if (d >= 0xB9 && d <= 0xBC) o = 'u';
			else if (d == 0xA7) o = 'c';
			else break;
			i += 2;
		} else if ((c | 0x20) >= 'a' && (c | 0x20) <= 'z') { o = (char) (c | 0x20); i++; }
		else if (c == '-' && i > 0) { i++; continue; }
		else break;
		if (*k + 1 < n) w[(*k)++] = o;
	}
	w[*k] = 0;
	return i;
}

static int lev (const char *a, const char *b)
{
	int d[48], la = (int) strlen (a), lb = (int) strlen (b);
	if (la > 46 || lb > 46) return 99;
	for (int j = 0; j <= lb; j++) d[j] = j;
	for (int i = 1; i <= la; i++) {
		int prev = d[0]; d[0] = i;
		for (int j = 1; j <= lb; j++) {
			int cur = d[j], best = prev + (a[i - 1] != b[j - 1]);
			if (d[j] + 1 < best) best = d[j] + 1;
			if (d[j - 1] + 1 < best) best = d[j - 1] + 1;
			d[j] = best; prev = cur;
		}
	}
	return d[lb];
}

static int is_sep (char c) { return c == ' ' || c == ',' || c == '.' || c == '!' || c == '?' || c == ';' || c == ':'; }

/* Le mot de réveil en tête, tel qu'une transcription l'écrit : « Dis Pocket, », « Dit Pocket », « Dispocket », « Dis
 * poquette », « Dis pochette »... : les 1 à 3 premiers mots, lettres mises bout à bout, à 2 fautes au plus de
 * « dispocket » ou « dispoquette » ; ou « Pocket » seul (1 faute). « Dis-moi », « dis, pour quoi » sont à 3 fautes ou
 * plus : gardés. (24/10/2026 : Pocket, après un essai de « dis Nila » ; avant,
 * « dis Pocket », avec aussi « lo, », la fin du mot d'un son coupé ; « la, » serait trop souvent un vrai début.) */
void pocket_ecoute_strip (char *t)
{
	char *p = t;
	while (is_sep (*p)) p++;
	char key[48]; size_t nk = 0;
	const char *end[3] = { 0, 0, 0 };
	int best = 99, bestk = -1;
	const char *q = p;
	for (int k = 0; k < 3; k++) {
		char w[24]; size_t lw;
		size_t l = fold_word (q, w, sizeof w, &lw);
		if (!l || nk + lw + 1 >= sizeof key) break;
		memcpy (key + nk, w, lw + 1); nk += lw;
		q += l;
		end[k] = q;
		int d = nk >= 5 ? lev (key, "dispocket") : 99;
		if (nk >= 5 && lev (key, "dispoquette") < d) d = lev (key, "dispoquette");
		if (k == 0 && nk >= 4 && (lev (key, "pocket") <= 1 || lev (key, "poquette") <= 1) && d > 1) d = 1;	/* « Pocket » seul */
		if (d <= 2 && d <= best) { best = d; bestk = k; }
		while (is_sep (*q)) q++;
	}
	if (bestk >= 0) p = (char *) end[bestk];
	while (is_sep (*p)) p++;
	if (p != t) memmove (t, p, strlen (p) + 1);
	if (t[0] >= 'a' && t[0] <= 'z') t[0] = (char) (t[0] - 32);
}

int pocket_ecoute_fin_dite (const char *t)
{
	static const char *const v[] = { "au revoir", "c'est tout", "c’est tout", "bonne nuit", "à plus", "a plus",
	                                 "à bientôt", "a bientot", "salut pocket", "laisse-moi", "tais-toi", "chut", 0 };
	char low[256]; size_t i;
	for (i = 0; t[i] && i + 1 < sizeof low; i++) low[i] = (char) lower (t[i]);
	low[i] = 0;
	for (int k = 0; v[k]; k++) if (strstr (low, v[k])) return 1;
	return 0;
}
