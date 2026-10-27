/* PocketQI (github.com/stephaneweg/PocketQI, commit 4d2a9ba), unchanged but for these two lines: MIT License,
 * Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md). */
/* pocket_son.c -- le son envoyé à Voxtral : Ogg Opus (RFC 7845), WAV en secours (pocket_son.h)
 *
 * L'encodeur : libopus 1.6.1 (../libopus, copie non modifiée), réglé comme opusenc --speech (application AUDIO, signal
 * VOICE, VBR) à 16 kb/s, trames de 20 ms. L'Ogg : une page pour OpusHead, une pour OpusTags, puis les paquets, 255
 * segments au plus par page ; la granule de la dernière page coupe le remplissage de fin (pre-skip + n échantillons à
 * 48 kHz), le CRC est celui d'Ogg (polynôme 0x04C11DB7, sans réflexion). Hôtes petit-boutistes (x86, AArch64, Xtensa). */
#include "pocket_son.h"
#include <stdlib.h>
#include <string.h>
#include "opus.h"

#ifndef POCKET_SON_COMPLEXITE
#define POCKET_SON_COMPLEXITE	10
#endif

static int s_opus = 1, s_complexite = POCKET_SON_COMPLEXITE, s_debit = POCKET_SON_DEBIT;

void pocket_son_set_opus (int opus) { s_opus = opus != 0; }
int pocket_son_get_opus (void) { return s_opus; }

void pocket_son_set_reglage (int complexite, int debit)
{
	if (complexite >= 0 && complexite <= 10) s_complexite = complexite;
	if (debit >= 6000 && debit <= 510000) s_debit = debit;
}

static void le16 (unsigned char *p, unsigned v) { p[0] = (unsigned char) v; p[1] = (unsigned char) (v >> 8); }
static void le32 (unsigned char *p, unsigned v) { le16 (p, v & 0xFFFF); le16 (p + 2, v >> 16); }

/* ---- Ogg ---------------------------------------------------------------------------------------------------- */

typedef struct
{
	unsigned char *b;		/* la sortie : avant (réservé) + les pages */
	size_t n, cap;
	int plein;			/* plus de mémoire */
	unsigned seq;
	int nlac;			/* la page en cours : table des segments et corps */
	unsigned char lac[255];
	size_t ncorps;
	unsigned char corps[255 * 255];
} ogg;

static int place (ogg *g, size_t k)
{
	if (g->n + k <= g->cap) return 1;
	size_t c = g->cap * 2;
	while (c < g->n + k) c *= 2;
	unsigned char *p = (unsigned char *) realloc (g->b, c);
	if (!p) { g->plein = 1; return 0; }
	g->b = p;
	g->cap = c;
	return 1;
}

static unsigned crc_ogg (const unsigned char *p, size_t n)
{
	unsigned c = 0;
	while (n--)
	{
		c ^= (unsigned) *p++ << 24;
		for (int k = 0; k < 8; k++) c = c & 0x80000000u ? c << 1 ^ 0x04C11DB7u : c << 1;
	}
	return c & 0xFFFFFFFFu;
}

/* drapeaux : 2 première page, 4 dernière */
static void page (ogg *g, unsigned long long granule, int drapeaux)
{
	size_t k = 27 + (size_t) g->nlac + g->ncorps;
	if (!place (g, k)) return;
	unsigned char *p = g->b + g->n;
	memcpy (p, "OggS", 4);
	p[4] = 0;
	p[5] = (unsigned char) drapeaux;
	for (int i = 0; i < 8; i++) p[6 + i] = (unsigned char) (granule >> 8 * i);
	le32 (p + 14, 0x47414C4Fu);	/* numéro de série : « Pocket » (un seul flux) */
	le32 (p + 18, g->seq++);
	le32 (p + 22, 0);
	p[26] = (unsigned char) g->nlac;
	memcpy (p + 27, g->lac, (size_t) g->nlac);
	memcpy (p + 27 + g->nlac, g->corps, g->ncorps);
	le32 (p + 22, crc_ogg (p, k));
	g->n += k;
	g->nlac = 0;
	g->ncorps = 0;
}

/* 0 : la page en cours est pleine */
static int paquet (ogg *g, const unsigned char *d, size_t len)
{
	int nl = (int) (len / 255) + 1;
	if (g->nlac + nl > 255) return 0;
	for (int i = 0; i < nl - 1; i++) g->lac[g->nlac++] = 255;
	g->lac[g->nlac++] = (unsigned char) (len % 255);
	memcpy (g->corps + g->ncorps, d, len);
	g->ncorps += len;
	return 1;
}

/* 1 : fait ; sinon le code libopus (< 0) */
static int ogg_opus (const short *pcm, size_t n, unsigned rate, size_t avant, size_t apres, pocket_son *s)
{
	if (rate == 0 || 48000 % rate != 0) return OPUS_BAD_ARG;
	int err = OPUS_OK;
	OpusEncoder *e = opus_encoder_create ((opus_int32) rate, 1, OPUS_APPLICATION_AUDIO, &err);
	if (!e) return err < 0 ? err : OPUS_ALLOC_FAIL;
	opus_encoder_ctl (e, OPUS_SET_BITRATE (s_debit));
	opus_encoder_ctl (e, OPUS_SET_COMPLEXITY (s_complexite));
	opus_encoder_ctl (e, OPUS_SET_SIGNAL (OPUS_SIGNAL_VOICE));
	opus_int32 avance = 0;
	opus_encoder_ctl (e, OPUS_GET_LOOKAHEAD (&avance));

	ogg *g = (ogg *) malloc (sizeof *g);
	if (!g) { opus_encoder_destroy (e); return OPUS_ALLOC_FAIL; }
	memset (g, 0, offsetof (ogg, corps));
	g->cap = avant + 1024 + (size_t) s_debit / 8 * (n / rate + 1) * 5 / 4 + apres;	/* (le débit moyen, + 25 %) */
	g->b = (unsigned char *) malloc (g->cap);
	g->n = avant;
	if (!g->b) { free (g); opus_encoder_destroy (e); return OPUS_ALLOC_FAIL; }

	const unsigned f48 = 48000 / rate;		/* les granules et le pre-skip sont à 48 kHz */
	unsigned char h[128];
	memcpy (h, "OpusHead", 8);
	h[8] = 1;					/* version */
	h[9] = 1;					/* mono */
	le16 (h + 10, (unsigned) avance * f48);		/* pre-skip */
	le32 (h + 12, rate);
	le16 (h + 16, 0);				/* gain */
	h[18] = 0;					/* famille de canaux 0 */
	paquet (g, h, 19);
	page (g, 0, 2);
	const char *vendeur = opus_get_version_string ();
	size_t nv = strlen (vendeur);
	if (nv > sizeof h - 16) nv = sizeof h - 16;
	memcpy (h, "OpusTags", 8);
	le32 (h + 8, (unsigned) nv);
	memcpy (h + 12, vendeur, nv);
	le32 (h + 12 + nv, 0);				/* aucun commentaire */
	paquet (g, h, 16 + nv);
	page (g, 0, 0);

	/* les trames de 20 ms ; la fin complétée par du silence, de quoi vider l'avance de l'encodeur */
	const size_t trame = rate / 50, ntrames = (n + (size_t) avance + trame - 1) / trame;
	short z[960];
	unsigned char pk[1500];
	unsigned long long granule = 0;
	for (size_t i = 0; i < ntrames && err >= 0 && !g->plein; i++)
	{
		const short *x = pcm + i * trame;
		if ((i + 1) * trame > n)
		{
			size_t reste = i * trame < n ? n - i * trame : 0;
			memcpy (z, x, reste * sizeof *z);
			memset (z + reste, 0, (trame - reste) * sizeof *z);
			x = z;
		}
		int l = opus_encode (e, x, (int) trame, pk, (opus_int32) sizeof pk);
		if (l < 0) { err = l; break; }
		if (!paquet (g, pk, (size_t) l))
		{
			page (g, granule, 0);
			paquet (g, pk, (size_t) l);
		}
		granule += trame * f48;
	}
	opus_encoder_destroy (e);
	if (err >= 0) page (g, ((unsigned long long) avance + n) * f48, 4);
	if (err >= 0 && !g->plein) place (g, apres);
	if (err >= 0 && g->plein) err = OPUS_ALLOC_FAIL;
	if (err < 0) { free (g->b); free (g); return err; }
	s->tampon = g->b;
	s->data = g->b + avant;
	s->len = g->n - avant;
	s->opus = 1;
	free (g);
	return 1;
}

/* ---- WAV : l'en-tête RIFF de 44 octets, puis le son tel quel ------------------------------------------------- */

static int wav (const short *pcm, size_t n, unsigned rate, size_t avant, size_t apres, pocket_son *s)
{
	size_t d = n * 2;
	unsigned char *b = (unsigned char *) malloc (avant + 44 + d + apres), *w = b + avant;
	if (!b) return 0;
	memcpy (w, "RIFF", 4); le32 (w + 4, (unsigned) (36 + d)); memcpy (w + 8, "WAVEfmt ", 8);
	le32 (w + 16, 16); le16 (w + 20, 1); le16 (w + 22, 1); le32 (w + 24, rate); le32 (w + 28, rate * 2);
	le16 (w + 32, 2); le16 (w + 34, 16); memcpy (w + 36, "data", 4); le32 (w + 40, (unsigned) d);
	memcpy (w + 44, pcm, d);
	s->tampon = b;
	s->data = w;
	s->len = 44 + d;
	s->opus = 0;
	return 1;
}

int pocket_son_prepare (const short *pcm, size_t n, unsigned rate, size_t avant, size_t apres, pocket_son *s)
{
	memset (s, 0, sizeof *s);
	if (s_opus)
	{
		int r = ogg_opus (pcm, n, rate, avant, apres, s);
		if (r == 1) return 1;
		s->erreur = r;
	}
	return wav (pcm, n, rate, avant, apres, s);
}

void pocket_son_libere (pocket_son *s)
{
	free (s->tampon);
	s->tampon = s->data = 0;
	s->len = 0;
}

const char *pocket_son_nom (const pocket_son *s) { return s->opus ? "galo.opus" : "galo.wav"; }
const char *pocket_son_mime (const pocket_son *s) { return s->opus ? "audio/ogg" : "audio/wav"; }
