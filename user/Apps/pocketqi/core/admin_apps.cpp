/* PocketQI (github.com/stephaneweg/PocketQI, commit 3eda2e5), unchanged but for these two lines: MIT License,
 * Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md). */
//
// admin_apps.cpp -- Pocket alpha admin_web : installer, lister et supprimer les apps vocales (API /api/apps)
//
// Pour PocketQI Studio (bouton « Envoyer au robot »), sans passer par la carte SD :
//
//   GET    /api/apps        les apps installées : [{"id", "name", "description", "actions": [...]}]
//   POST   /api/apps/<id>   installe ou remplace SD:/apps/<id>/ ; le corps est un paquet (ci-dessous)
//   DELETE /api/apps/<id>   supprime SD:/apps/<id>/
//   GET    /api/apps/<id>   l'app installée, en paquet (le même format) : PocketQI Studio la récupère
//   POST   /api/apps/<id>/run  la lancer sans le LLM (F5 du studio) : corps = l'action puis un argument par ligne ;
//                           seulement si le shell a installé son lanceur (AdminSetAppRunner), 501 sinon
//
// Réponses en JSON (UTF-8) : {"ok": true, ...} ou {"ok": false, "error": "...", "file": "main.bas", "line": 12}.
//
// Le paquet (Content-Type: application/x-galo-app), du texte simple à écrire et à relire :
//
//   POCKETAPP 1\n
//   F app.txt 123\n <les 123 octets du fichier> \n
//   F main.bas 4567\n <les 4567 octets> \n
//   END\n
//
// Contrôles, tous faits AVANT d'écrire quoi que ce soit :
//   - id : a-z 0-9 _ (1 à 23 caractères, la taille de TPocketApp::Id) ;
//   - corps de ADMIN_APP_MAX_BODY octets au plus, ADMIN_APP_MAX_FILES fichiers, chacun ADMIN_APP_MAX_FILE ;
//   - noms de fichiers simples (A-Z a-z 0-9 _ - .), sans dossier ni « .. » : rien ne sort de SD:/apps/<id>/ ;
//   - fichiers texte (ni octet nul ni caractère de contrôle autre que tabulation et fins de ligne) ;
//   - app.txt valide : au moins une ligne llm.<action> = <types> : <description>, types connus, tailles des
//     tableaux de pocketapps.h (le noyau tronquerait sans rien dire) ;
//   - main.bas se compile avec SD:/lib/galo.bi ajouté après lui (la VM BASIC d'Onyx, comme à l'exécution).
// En cas d'échec, rien n'est touché et l'erreur est renvoyée (fichier et ligne pour la compilation).
//
// Écriture : SD:/tmp/apps/<id>.new/ d'abord (les fichiers du paquet, puis les DONNÉES de l'ancienne version que le
// paquet n'apporte pas : agenda.txt... sont gardées), puis deux renommages : l'ancienne version de côté
// (SD:/tmp/apps/<id>.old), la nouvelle à sa place ; l'ancienne est remise si le second échoue. Une app n'est donc
// jamais à moitié écrite dans SD:/apps. Puis AdminAppsChanged () : les manifestes sont relus (comme
// « skills reload »), le LLM connaît l'app à la phrase suivante.
//
// Accès : comme les autres pages, sans mot de passe tant que SD:/etc/admin.pass n'existe pas (décision du client,
// README § 3). S'il existe (« sha256:<sel hex>:<SHA-256 hex du sel puis du mot de passe> »), l'API exige
// « Authorization: Basic » avec ce mot de passe (l'utilisateur est ignoré) : 401 sinon. POST et DELETE avec un
// en-tête Origin sont refusés (403) : une page web ne peut pas installer d'app ; PocketQI Studio n'en envoie pas.
//
#include "admin_core.h"
#include "basic/bas.h"
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>

#define APPS_DIR	"SD:/apps"
#define APPS_TMP0	"SD:/tmp"
#define APPS_TMP	"SD:/tmp/apps"
#define APPS_LIB	"SD:/lib/galo.bi"
#define ADMIN_PASS	ADMIN_ETC "/admin.pass"

#define MAX_ID		23		// TPocketApp::Id[24]
#define MAX_NAME	31
#define MAX_KEEP	(256 * 1024)	// une donnée gardée de l'ancienne version
#define MAX_LIB		(128 * 1024)	// galo.bi
#define MANIFEST_MAX	4000		// pocketapps.cpp lit 4095 octets d'app.txt
#define MAX_ACTIONS	8		// POCKETAPPS_MAX_ACTIONS
#define MAX_TYPES	6		// POCKETAPPS_MAX_TYPES
#define MAX_ACTION	23		// TPocketAppAction::Name[24]
#define MAX_ADESC	199		// TPocketAppAction::Description[200]
#define MAX_DESC	119		// TPocketApp::Description[120]

// ---- petits outils -----------------------------------------------------------------------------

static bool IsLowerAlnum (char c)
{
	return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}

bool AdminAppIdValid (const char *pId)
{
	unsigned n = strlen (pId);
	if (n == 0 || n > MAX_ID)
	{
		return false;
	}
	for (unsigned i = 0; i < n; i++)
	{
		if (!IsLowerAlnum (pId[i]))
		{
			return false;
		}
	}
	return true;
}

// un nom de fichier sans dossier : A-Z a-z 0-9 _ - ., pas de « . » au début (ni « .. »)
static bool FileNameValid (const char *pName)
{
	unsigned n = strlen (pName);
	if (n == 0 || n > MAX_NAME || pName[0] == '.')
	{
		return false;
	}
	for (unsigned i = 0; i < n; i++)
	{
		char c = pName[i];
		if (!(   (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
		      || c == '_' || c == '-' || c == '.'))
		{
			return false;
		}
	}
	return true;
}

// du texte (UTF-8 ou Latin-1) : pas d'octet nul ni de contrôle sauf \t \r \n ; 0, ou la ligne fautive
static unsigned TextBadLine (const char *p, unsigned n)
{
	unsigned nLine = 1;
	for (unsigned i = 0; i < n; i++)
	{
		unsigned char c = (unsigned char) p[i];
		if (c == '\n')
		{
			nLine++;
		}
		else if (c < 0x20 && c != '\t' && c != '\r')
		{
			return nLine;
		}
	}
	return 0;
}

// JSON : la chaîne entre guillemets (UTF-8 gardé, " \ et contrôles échappés)
static void JsonString (CAdminText *pOut, const char *p, unsigned n)
{
	pOut->Append ("\"");
	for (unsigned i = 0; i < n; i++)
	{
		unsigned char c = (unsigned char) p[i];
		if (c == '"' || c == '\\')
		{
			char e[3] = { '\\', (char) c, 0 };
			pOut->Append (e);
		}
		else if (c < 0x20)
		{
			pOut->Printf ("\\u%04x", c);
		}
		else
		{
			pOut->Append ((const char *) &p[i], 1);
		}
	}
	pOut->Append ("\"");
}

static void JsonString (CAdminText *pOut, const char *p)
{
	JsonString (pOut, p, strlen (p));
}

// UTF-8 -> Latin-1 (les sources de la VM), comme versLatin1 de pocketqi_win/src/pocket_apps.cpp
static void ToLatin1 (const char *s, unsigned n, CAdminText *pOut)
{
	for (unsigned i = 0; i < n; i++)
	{
		unsigned char c = (unsigned char) s[i];
		if (c < 0x80)
		{
			pOut->Append ((const char *) &s[i], 1);
			continue;
		}
		unsigned v = 0, k = 0;
		if ((c & 0xE0) == 0xC0) { v = c & 0x1F; k = 1; }
		else if ((c & 0xF0) == 0xE0) { v = c & 0x0F; k = 2; }
		else if ((c & 0xF8) == 0xF0) { v = c & 0x07; k = 3; }
		else { pOut->Append ((const char *) &s[i], 1); continue; }	// (déjà du Latin-1)
		bool bUtf8 = i + k < n;
		for (unsigned j = 1; bUtf8 && j <= k; j++)
		{
			bUtf8 = ((unsigned char) s[i + j] & 0xC0) == 0x80;
		}
		if (!bUtf8)
		{
			pOut->Append ((const char *) &s[i], 1);			// (Latin-1)
			continue;
		}
		for (unsigned j = 0; j < k; j++)
		{
			v = (v << 6) | ((unsigned char) s[++i] & 0x3F);
		}
		char b = '?';
		if (v < 256) b = (char) v;
		else if (v == 0x153) { pOut->Append ("oe"); continue; }
		else if (v == 0x152) { pOut->Append ("OE"); continue; }
		else if (v == 0x2018 || v == 0x2019) b = '\'';
		else if (v == 0x201C || v == 0x201D) b = '"';
		else if (v == 0x2026) { pOut->Append ("..."); continue; }
		else if (v == 0x2013 || v == 0x2014) b = '-';
		else if (v == 0x20AC) { pOut->Append ("euros"); continue; }
		pOut->Append (&b, 1);
	}
}

static void Latin1ToUtf8 (const char *s, CAdminText *pOut)
{
	for (; *s != '\0'; s++)
	{
		unsigned char c = (unsigned char) *s;
		if (c < 0x80)
		{
			pOut->Append (s, 1);
		}
		else
		{
			char b[2] = { (char) (0xC0 | (c >> 6)), (char) (0x80 | (c & 0x3F)) };
			pOut->Append (b, 2);
		}
	}
}

static bool StartsWithNoCase (const char *p, const char *pEnd, const char *pWord)
{
	unsigned n = strlen (pWord);
	return (unsigned) (pEnd - p) >= n && strncasecmp (p, pWord, n) == 0;
}

// WITH HEADERS ... END WITH -> appels WebHeader de galo.bi, une ligne pour une ligne (comme avecEntetes de
// pocketqi_win et WithHeaders de basic_circle/src/basichost.cpp) : sans cela, une app web ne compilerait pas
static void WithHeaders (const char *pSrc, unsigned nSrc, CAdminText *pOut)
{
	bool bInside = false;
	const char *pEnd = pSrc + nSrc;
	for (const char *l = pSrc; l < pEnd; )
	{
		const char *e = l;
		while (e < pEnd && *e != '\n') e++;
		const char *le = e;
		bool bCR = le > l && le[-1] == '\r';
		if (bCR) le--;
		const char *t = l;
		while (t < le && (*t == ' ' || *t == '\t')) t++;
		const char *te = le;
		while (te > t && (te[-1] == ' ' || te[-1] == '\t')) te--;
		unsigned nt = (unsigned) (te - t);

		if (!bInside && nt == 12 && strncasecmp (t, "with headers", 12) == 0)
		{
			pOut->Append (l, (unsigned) (t - l));
			pOut->Append ("WebHeader \"\"");
			bInside = true;
		}
		else if (bInside && nt == 8 && strncasecmp (t, "end with", 8) == 0)
		{
			pOut->Append (l, (unsigned) (t - l));
			pOut->Append ("REM (END WITH)");
			bInside = false;
		}
		else if (bInside && nt > 0 && *t != '\'' && !StartsWithNoCase (t, te, "rem"))
		{
			const char *q = t;
			while (q < te && *q != '=') q++;
			if (q < te)
			{
				const char *ne = q, *v = q + 1;
				while (ne > t && (ne[-1] == ' ' || ne[-1] == '\t')) ne--;
				while (v < te && (*v == ' ' || *v == '\t')) v++;
				pOut->Append (l, (unsigned) (t - l));
				pOut->Append ("WebHeader \"");
				pOut->Append (t, (unsigned) (ne - t));
				pOut->Append (": \" + (");
				pOut->Append (v, (unsigned) (te - v));
				pOut->Append (")");
			}
			else
			{
				pOut->Append (l, (unsigned) (le - l));
			}
		}
		else
		{
			pOut->Append (l, (unsigned) (le - l));
		}
		if (e < pEnd)
		{
			pOut->Append (bCR ? "\r\n" : "\n");
		}
		l = e < pEnd ? e + 1 : e;
	}
}

// ---- SHA-256 et base64 (le mot de passe facultatif de l'API) -------------------------------------

struct TSha256 { unsigned h[8]; unsigned char Block[64]; unsigned nBlock; unsigned long long nTotal; };

static const unsigned K256[64] = {
	0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
	0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
	0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
	0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
	0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
	0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
	0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
	0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2 };

static unsigned Ror (unsigned x, unsigned n) { return (x >> n) | (x << (32 - n)); }

static void ShaBlock (TSha256 *s)
{
	unsigned w[64];
	for (unsigned i = 0; i < 16; i++)
	{
		w[i] = (unsigned) s->Block[4 * i] << 24 | (unsigned) s->Block[4 * i + 1] << 16
		     | (unsigned) s->Block[4 * i + 2] << 8 | s->Block[4 * i + 3];
	}
	for (unsigned i = 16; i < 64; i++)
	{
		unsigned s0 = Ror (w[i - 15], 7) ^ Ror (w[i - 15], 18) ^ (w[i - 15] >> 3);
		unsigned s1 = Ror (w[i - 2], 17) ^ Ror (w[i - 2], 19) ^ (w[i - 2] >> 10);
		w[i] = w[i - 16] + s0 + w[i - 7] + s1;
	}
	unsigned a = s->h[0], b = s->h[1], c = s->h[2], d = s->h[3], e = s->h[4], f = s->h[5], g = s->h[6], h = s->h[7];
	for (unsigned i = 0; i < 64; i++)
	{
		unsigned t1 = h + (Ror (e, 6) ^ Ror (e, 11) ^ Ror (e, 25)) + ((e & f) ^ (~e & g)) + K256[i] + w[i];
		unsigned t2 = (Ror (a, 2) ^ Ror (a, 13) ^ Ror (a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
		h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
	}
	s->h[0] += a; s->h[1] += b; s->h[2] += c; s->h[3] += d; s->h[4] += e; s->h[5] += f; s->h[6] += g; s->h[7] += h;
}

static void ShaInit (TSha256 *s)
{
	static const unsigned H0[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
					0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
	memcpy (s->h, H0, sizeof H0);
	s->nBlock = 0;
	s->nTotal = 0;
}

static void ShaUpdate (TSha256 *s, const void *pData, unsigned n)
{
	const unsigned char *p = (const unsigned char *) pData;
	s->nTotal += n;
	while (n-- > 0)
	{
		s->Block[s->nBlock++] = *p++;
		if (s->nBlock == 64)
		{
			ShaBlock (s);
			s->nBlock = 0;
		}
	}
}

static void ShaFinal (TSha256 *s, unsigned char Out[32])
{
	unsigned long long nBits = s->nTotal * 8;
	unsigned char Pad = 0x80;
	ShaUpdate (s, &Pad, 1);
	Pad = 0;
	while (s->nBlock != 56)
	{
		ShaUpdate (s, &Pad, 1);
	}
	unsigned char Len[8];
	for (unsigned i = 0; i < 8; i++)
	{
		Len[i] = (unsigned char) (nBits >> (56 - 8 * i));
	}
	ShaUpdate (s, Len, 8);
	for (unsigned i = 0; i < 8; i++)
	{
		Out[4 * i] = (unsigned char) (s->h[i] >> 24); Out[4 * i + 1] = (unsigned char) (s->h[i] >> 16);
		Out[4 * i + 2] = (unsigned char) (s->h[i] >> 8); Out[4 * i + 3] = (unsigned char) s->h[i];
	}
	AdminWipe (s, sizeof *s);
}

static int HexDigit (char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

// "<hex>" -> octets ; nombre d'octets, -1 si mal formé
static int FromHex (const char *p, unsigned n, unsigned char *pOut, unsigned nMax)
{
	if (n % 2 != 0 || n / 2 > nMax)
	{
		return -1;
	}
	for (unsigned i = 0; i < n / 2; i++)
	{
		int a = HexDigit (p[2 * i]), b = HexDigit (p[2 * i + 1]);
		if (a < 0 || b < 0)
		{
			return -1;
		}
		pOut[i] = (unsigned char) (a << 4 | b);
	}
	return (int) (n / 2);
}

static int Base64Value (char c)
{
	if (c >= 'A' && c <= 'Z') return c - 'A';
	if (c >= 'a' && c <= 'z') return c - 'a' + 26;
	if (c >= '0' && c <= '9') return c - '0' + 52;
	if (c == '+') return 62;
	if (c == '/') return 63;
	return -1;
}

// base64 -> texte (NUL ajouté) ; longueur, -1 si mal formé
static int FromBase64 (const char *p, char *pOut, unsigned nMax)
{
	unsigned n = 0, nBits = 0, Acc = 0;
	for (; *p != '\0' && *p != '='; p++)
	{
		int v = Base64Value (*p);
		if (v < 0)
		{
			return -1;
		}
		Acc = (Acc << 6) | (unsigned) v;
		nBits += 6;
		if (nBits >= 8)
		{
			nBits -= 8;
			if (n + 1 >= nMax)
			{
				return -1;
			}
			pOut[n++] = (char) ((Acc >> nBits) & 0xFF);
		}
	}
	pOut[n] = '\0';
	return (int) n;
}

// le mot de passe d'administration (facultatif) : true si l'API est permise à cette requête
static bool AuthOK (const THttpRequest &rRequest)
{
	char Line[200];
	int n = AdminFileRead (ADMIN_PASS, Line, sizeof Line);
	if (n < 0)
	{
		return true;					// (pas de mot de passe : décision du client)
	}
	while (n > 0 && (Line[n - 1] == '\n' || Line[n - 1] == '\r' || Line[n - 1] == ' '))
	{
		Line[--n] = '\0';
	}
	// sha256:<sel hex>:<empreinte hex>
	const char *pSalt = strncmp (Line, "sha256:", 7) == 0 ? Line + 7 : 0;
	const char *pColon = pSalt != 0 ? strchr (pSalt, ':') : 0;
	unsigned char Salt[32], Hash[32], Mine[32];
	int nSalt = pColon != 0 ? FromHex (pSalt, (unsigned) (pColon - pSalt), Salt, sizeof Salt) : -1;
	if (nSalt < 0 || FromHex (pColon + 1, strlen (pColon + 1), Hash, sizeof Hash) != 32)
	{
		AdminLog ("apps: %s malformed, API refused", ADMIN_PASS);
		return false;
	}
	if (strncasecmp (rRequest.Authorization, "Basic ", 6) != 0)
	{
		return false;
	}
	char User[160];
	int nUser = FromBase64 (rRequest.Authorization + 6, User, sizeof User);
	const char *pPass = nUser > 0 ? strchr (User, ':') : 0;
	bool bOK = false;
	if (pPass != 0)
	{
		pPass++;
		TSha256 s;
		ShaInit (&s);
		ShaUpdate (&s, Salt, (unsigned) nSalt);
		ShaUpdate (&s, pPass, strlen (pPass));
		ShaFinal (&s, Mine);
		unsigned char d = 0;
		for (unsigned i = 0; i < 32; i++)
		{
			d |= (unsigned char) (Mine[i] ^ Hash[i]);	// (temps constant)
		}
		bOK = d == 0;
	}
	AdminWipe (User, sizeof User);
	AdminWipe (Mine, sizeof Mine);
	return bOK;
}

// ---- le manifeste --------------------------------------------------------------------------------

struct TAppAction
{
	const char *pName;	unsigned nName;
	const char *pTypes;	unsigned nTypes;		// « date [texte] »
	const char *pDesc;	unsigned nDesc;
};

struct TAppManifest
{
	const char *pName;	unsigned nName;
	const char *pDesc;	unsigned nDesc;
	TAppAction Actions[MAX_ACTIONS];
	unsigned nActions;
};

static void Trim (const char **pp, const char **ppEnd)
{
	while (*pp < *ppEnd && (**pp == ' ' || **pp == '\t' || **pp == '\r')) (*pp)++;
	while (*ppEnd > *pp && ((*ppEnd)[-1] == ' ' || (*ppEnd)[-1] == '\t' || (*ppEnd)[-1] == '\r')) (*ppEnd)--;
}

// un type d'argument connu du shell : date heure nombre texte, [facultatif]
static bool TypeKnown (const char *p, unsigned n, bool *pOptional)
{
	*pOptional = n >= 2 && p[0] == '[' && p[n - 1] == ']';
	if (*pOptional)
	{
		p++;
		n -= 2;
	}
	static const char *const Types[] = { "date", "heure", "nombre", "texte" };
	for (unsigned i = 0; i < sizeof Types / sizeof Types[0]; i++)
	{
		if (n == strlen (Types[i]) && strncasecmp (p, Types[i], n) == 0)
		{
			return true;
		}
	}
	return false;
}

// les lignes de app.txt (comme ParseManifest de pocketapps.cpp et PocketAppsScan de pocketqi_win) ; bStrict : les
// erreurs (pError, *pLine) ; sinon le mieux possible (liste des apps)
static bool ParseManifest (const char *pText, unsigned nText, TAppManifest *pM, bool bStrict,
			   char *pError, unsigned nErrorSize, unsigned *pLine)
{
	memset (pM, 0, sizeof *pM);
	*pLine = 0;
	pError[0] = '\0';
	const char *pEnd = pText + nText;
	unsigned nLine = 0;
	for (const char *l = pText; l < pEnd; )
	{
		const char *e = l;
		while (e < pEnd && *e != '\n') e++;
		nLine++;
		const char *t = l, *te = e;
		l = e < pEnd ? e + 1 : e;
		Trim (&t, &te);
		if (t == te || *t == '#')
		{
			continue;
		}
		const char *pEq = t;
		while (pEq < te && *pEq != '=') pEq++;
		if (pEq == te)
		{
			continue;
		}
		const char *k = t, *ke = pEq, *v = pEq + 1, *ve = te;
		Trim (&k, &ke);
		Trim (&v, &ve);
		unsigned nk = (unsigned) (ke - k);
		if (nk == 4 && strncasecmp (k, "name", 4) == 0)
		{
			pM->pName = v; pM->nName = (unsigned) (ve - v);
		}
		else if (nk == 11 && strncasecmp (k, "description", 11) == 0)
		{
			pM->pDesc = v; pM->nDesc = (unsigned) (ve - v);
			if (bStrict && pM->nDesc > MAX_DESC)
			{
				snprintf (pError, nErrorSize, "description trop longue (%u octets, %u au plus)", pM->nDesc, MAX_DESC);
				*pLine = nLine;
				return false;
			}
		}
		else if (nk > 4 && strncasecmp (k, "llm.", 4) == 0)
		{
			const char *a = k + 4;
			unsigned na = nk - 4;
			bool bName = na <= MAX_ACTION;
			for (unsigned i = 0; bName && i < na; i++)
			{
				char c = a[i];
				bName = IsLowerAlnum (c) || (c >= 'A' && c <= 'Z');
			}
			const char *c = v;
			while (c < ve && *c != ':') c++;
			const char *ty = v, *tye = c, *d = c < ve ? c + 1 : c, *de = ve;
			Trim (&ty, &tye);
			Trim (&d, &de);
			if (bStrict)
			{
				if (!bName)
				{
					snprintf (pError, nErrorSize, "nom d'action invalide (lettres, chiffres et _, %u au plus)", MAX_ACTION);
					*pLine = nLine;
					return false;
				}
				if (pM->nActions == MAX_ACTIONS)
				{
					snprintf (pError, nErrorSize, "trop d'actions llm. (%u au plus)", MAX_ACTIONS);
					*pLine = nLine;
					return false;
				}
				if (c == ve || d == de)
				{
					snprintf (pError, nErrorSize, "il manque « : description » (llm.%.*s = types : ce que fait l'action)",
						  (int) na, a);
					*pLine = nLine;
					return false;
				}
				if ((unsigned) (de - d) > MAX_ADESC)
				{
					snprintf (pError, nErrorSize, "description de llm.%.*s trop longue (%u octets, %u au plus)",
						  (int) na, a, (unsigned) (de - d), MAX_ADESC);
					*pLine = nLine;
					return false;
				}
				unsigned nTypes = 0;
				bool bOptionalSeen = false;
				for (const char *p = ty; p < tye; )
				{
					while (p < tye && (*p == ' ' || *p == '\t')) p++;
					const char *q = p;
					while (q < tye && *q != ' ' && *q != '\t') q++;
					if (q == p)
					{
						break;
					}
					bool bOptional;
					if (!TypeKnown (p, (unsigned) (q - p), &bOptional))
					{
						snprintf (pError, nErrorSize, "type inconnu « %.*s » (date, heure, nombre, texte ; [type] facultatif)",
							  (int) (q - p), p);
						*pLine = nLine;
						return false;
					}
					if (bOptionalSeen && !bOptional)
					{
						snprintf (pError, nErrorSize, "un argument facultatif [..] doit être à la fin");
						*pLine = nLine;
						return false;
					}
					bOptionalSeen = bOptionalSeen || bOptional;
					if (++nTypes > MAX_TYPES)
					{
						snprintf (pError, nErrorSize, "trop d'arguments (%u au plus)", MAX_TYPES);
						*pLine = nLine;
						return false;
					}
					p = q;
				}
			}
			if (pM->nActions < MAX_ACTIONS)
			{
				TAppAction *pA = &pM->Actions[pM->nActions++];
				pA->pName = a; pA->nName = na;
				pA->pTypes = ty; pA->nTypes = (unsigned) (tye - ty);
				pA->pDesc = d; pA->nDesc = (unsigned) (de - d);
			}
		}
	}
	if (pM->nActions == 0)
	{
		snprintf (pError, nErrorSize, "aucune action : il faut au moins une ligne llm.<action> = <types> : <description>");
		return false;
	}
	return true;
}

static void JsonApp (CAdminText *pOut, const char *pId, const TAppManifest &m)
{
	pOut->Append ("{\"id\":");
	JsonString (pOut, pId);
	pOut->Append (",\"name\":");
	JsonString (pOut, m.pName != 0 ? m.pName : pId, m.pName != 0 ? m.nName : strlen (pId));
	pOut->Append (",\"description\":");
	JsonString (pOut, m.pDesc != 0 ? m.pDesc : "", m.nDesc);
	pOut->Append (",\"actions\":[");
	for (unsigned i = 0; i < m.nActions; i++)
	{
		const TAppAction &a = m.Actions[i];
		pOut->Append (i > 0 ? ",{\"name\":" : "{\"name\":");
		CAdminText Lower;
		for (unsigned k = 0; k < a.nName; k++)
		{
			char c = a.pName[k] >= 'A' && a.pName[k] <= 'Z' ? (char) (a.pName[k] - 'A' + 'a') : a.pName[k];
			Lower.Append (&c, 1);
		}
		JsonString (pOut, Lower.Get (), Lower.GetLength ());
		pOut->Append (",\"types\":");
		JsonString (pOut, a.pTypes, a.nTypes);
		pOut->Append (",\"description\":");
		JsonString (pOut, a.pDesc, a.nDesc);
		pOut->Append ("}");
	}
	pOut->Append ("]}");
}

// ---- la compilation (VM BASIC d'Onyx, galo.bi ajouté après le programme) -------------------------

// true si main.bas se compile ; sinon pFile (« main.bas » ou « galo.bi »), *pLine et pError (UTF-8)
bool AdminAppCompile (const char *pMain, unsigned nMain, char *pFile, unsigned nFileSize, unsigned *pLine,
		      char *pError, unsigned nErrorSize)
{
	*pLine = 0;
	snprintf (pFile, nFileSize, "main.bas");
	char *pBi = (char *) malloc (MAX_LIB);
	int nBi = pBi != 0 ? AdminFileRead (APPS_LIB, pBi, MAX_LIB) : -1;
	if (nBi < 0)
	{
		free (pBi);
		snprintf (pError, nErrorSize, "%s introuvable sur le robot", APPS_LIB);
		return false;
	}

	CAdminText Headers, Src;
	WithHeaders (pMain, nMain, &Headers);
	ToLatin1 (Headers.Get (), Headers.GetLength (), &Src);
	if (Src.GetLength () > 0 && Src.Get ()[Src.GetLength () - 1] != '\n')
	{
		Src.Append ("\n");
	}
	unsigned nMainLines = 0;
	for (const char *p = Src.Get (); *p != '\0'; p++)
	{
		nMainLines += *p == '\n';
	}
	ToLatin1 (pBi, (unsigned) nBi, &Src);
	free (pBi);
	if (Src.Failed ())
	{
		snprintf (pError, nErrorSize, "mémoire insuffisante pour compiler");
		return false;
	}

	bas::setDialect (0);
	bas::Error e;
	e.line = 0;
	e.msg[0] = '\0';
	bas::Program *pProgram = bas::compile (Src.Get (), &e);
	if (pProgram != 0)
	{
		bas::destroy (pProgram);
		return true;
	}
	unsigned nLine = e.line > 0 ? (unsigned) e.line : 0;
	if (nLine > nMainLines)
	{
		snprintf (pFile, nFileSize, "galo.bi");
		nLine -= nMainLines;
	}
	*pLine = nLine;
	char French[200];
	bas::frenchMessage (e.msg, French, sizeof French);
	CAdminText Utf8;
	Latin1ToUtf8 (French, &Utf8);
	snprintf (pError, nErrorSize, "%s", Utf8.Get ());
	return false;
}

// ---- les dossiers --------------------------------------------------------------------------------

struct TNameList
{
	char Names[64][MAX_NAME + 1];
	bool bDir[64];
	unsigned nCount;
	bool bOverflow;
};

static void AddName (const char *pName, bool bDir, void *pParam)
{
	TNameList *p = (TNameList *) pParam;
	if (p->nCount == sizeof p->Names / sizeof p->Names[0] || strlen (pName) > MAX_NAME)
	{
		p->bOverflow = true;
		return;
	}
	strcpy (p->Names[p->nCount], pName);
	p->bDir[p->nCount++] = bDir;
}

// supprime un dossier et son contenu (deux niveaux au plus : les apps n'en ont pas davantage)
static bool RemoveTree (const char *pPath, unsigned nDepth = 0)
{
	TNameList *pList = (TNameList *) malloc (sizeof (TNameList));
	if (pList == 0)
	{
		return false;
	}
	pList->nCount = 0;
	pList->bOverflow = false;
	if (AdminListDir (pPath, AddName, pList) < 0)
	{
		free (pList);
		return !AdminFileExists (pPath);
	}
	bool bOK = !pList->bOverflow;
	for (unsigned i = 0; i < pList->nCount; i++)
	{
		CAdminText Child;
		Child.Printf ("%s/%s", pPath, pList->Names[i]);
		if (pList->bDir[i])
		{
			bOK = nDepth < 3 && RemoveTree (Child.Get (), nDepth + 1) && bOK;
		}
		else
		{
			bOK = AdminFileDelete (Child.Get ()) && bOK;
		}
	}
	free (pList);
	return AdminRemoveDir (pPath) && bOK;
}

// ---- le paquet -----------------------------------------------------------------------------------

struct TAppFile
{
	char Name[MAX_NAME + 1];
	const char *pData;
	unsigned nLength;
};

struct TAppPackage
{
	TAppFile Files[ADMIN_APP_MAX_FILES];
	unsigned nFiles;
};

static const TAppFile *FindFile (const TAppPackage &r, const char *pName)
{
	for (unsigned i = 0; i < r.nFiles; i++)
	{
		if (strcasecmp (r.Files[i].Name, pName) == 0)
		{
			return &r.Files[i];
		}
	}
	return 0;
}

static bool ParsePackage (const char *p, unsigned n, TAppPackage *pPkg, char *pError, unsigned nErrorSize)
{
	pPkg->nFiles = 0;
	const char *pEnd = p + n;
	static const char Magic[] = "POCKETAPP 1\n";
	if (n < sizeof Magic - 1 || memcmp (p, Magic, sizeof Magic - 1) != 0)
	{
		snprintf (pError, nErrorSize, "paquet invalide : « POCKETAPP 1 » attendu au début");
		return false;
	}
	p += sizeof Magic - 1;
	for (;;)
	{
		const char *e = p;
		while (e < pEnd && *e != '\n') e++;
		if (e == pEnd)
		{
			snprintf (pError, nErrorSize, "paquet tronqué (« END » attendu)");
			return false;
		}
		if (e - p == 3 && memcmp (p, "END", 3) == 0)
		{
			break;
		}
		// F <nom> <longueur>
		if (e - p < 5 || p[0] != 'F' || p[1] != ' ')
		{
			snprintf (pError, nErrorSize, "paquet invalide : « F <nom> <longueur> » attendu");
			return false;
		}
		const char *pName = p + 2, *pSpace = pName;
		while (pSpace < e && *pSpace != ' ') pSpace++;
		char Name[MAX_NAME + 2];
		unsigned nName = (unsigned) (pSpace - pName);
		if (pSpace == e || nName > MAX_NAME)
		{
			snprintf (pError, nErrorSize, "paquet invalide : nom de fichier trop long ou sans longueur");
			return false;
		}
		memcpy (Name, pName, nName);
		Name[nName] = '\0';
		if (!FileNameValid (Name))
		{
			snprintf (pError, nErrorSize, "nom de fichier refusé « %s » : lettres, chiffres, _ - . seulement, sans dossier",
				  Name);
			return false;
		}
		unsigned long nLength = 0;
		const char *d = pSpace + 1;
		if (d == e)
		{
			snprintf (pError, nErrorSize, "paquet invalide : longueur de %s absente", Name);
			return false;
		}
		for (; d < e; d++)
		{
			if (*d < '0' || *d > '9' || nLength > ADMIN_APP_MAX_BODY)
			{
				snprintf (pError, nErrorSize, "paquet invalide : longueur de %s", Name);
				return false;
			}
			nLength = nLength * 10 + (unsigned) (*d - '0');
		}
		if (nLength > ADMIN_APP_MAX_FILE)
		{
			snprintf (pError, nErrorSize, "%s trop gros (%lu octets, %u au plus)", Name, nLength, ADMIN_APP_MAX_FILE);
			return false;
		}
		const char *pData = e + 1;
		if ((unsigned long) (pEnd - pData) < nLength + 1 || pData[nLength] != '\n')
		{
			snprintf (pError, nErrorSize, "paquet tronqué dans %s", Name);
			return false;
		}
		if (pPkg->nFiles == ADMIN_APP_MAX_FILES)
		{
			snprintf (pError, nErrorSize, "trop de fichiers (%u au plus)", ADMIN_APP_MAX_FILES);
			return false;
		}
		if (FindFile (*pPkg, Name) != 0)
		{
			snprintf (pError, nErrorSize, "%s est deux fois dans le paquet", Name);
			return false;
		}
		unsigned nBad = TextBadLine (pData, (unsigned) nLength);
		if (nBad != 0)
		{
			snprintf (pError, nErrorSize, "%s n'est pas un fichier texte (ligne %u)", Name, nBad);
			return false;
		}
		TAppFile *f = &pPkg->Files[pPkg->nFiles++];
		strcpy (f->Name, Name);
		f->pData = pData;
		f->nLength = (unsigned) nLength;
		p = pData + nLength + 1;
	}
	return true;
}

// ---- les réponses --------------------------------------------------------------------------------

static void SendJson (CAdminText *pResponse, unsigned nStatus, CAdminText *pBody, const char *pExtra = 0)
{
	HttpResponse (pResponse, nStatus, "application/json; charset=utf-8", pBody->Get (), pBody->GetLength (), pExtra);
}

static void SendError (CAdminText *pResponse, unsigned nStatus, const char *pError, const char *pFile = 0,
		       unsigned nLine = 0, const char *pExtra = 0)
{
	CAdminText Body;
	Body.Append ("{\"ok\":false,\"error\":");
	JsonString (&Body, pError);
	if (pFile != 0)
	{
		Body.Append (",\"file\":");
		JsonString (&Body, pFile);
		Body.Printf (",\"line\":%u", nLine);
	}
	Body.Append ("}\n");
	AdminLog ("apps: %u %s", nStatus, pFile != 0 ? "refused (see the response)" : "refused");
	SendJson (pResponse, nStatus, &Body, pExtra);
}

// ---- GET /api/apps -------------------------------------------------------------------------------

static void ListApps (CAdminText *pResponse)
{
	TNameList *pList = (TNameList *) malloc (sizeof (TNameList));
	char *pText = (char *) malloc (MANIFEST_MAX + 100);
	if (pList == 0 || pText == 0)
	{
		free (pList);
		free (pText);
		SendError (pResponse, 500, "mémoire insuffisante");
		return;
	}
	pList->nCount = 0;
	pList->bOverflow = false;
	AdminListDir (APPS_DIR, AddName, pList);
	CAdminText Body;
	Body.Append ("{\"ok\":true,\"apps\":[");
	unsigned nApps = 0;
	for (unsigned i = 0; i < pList->nCount; i++)
	{
		char Id[MAX_NAME + 1];
		unsigned k = 0;
		for (; pList->Names[i][k] != '\0'; k++)
		{
			char c = pList->Names[i][k];
			Id[k] = c >= 'A' && c <= 'Z' ? (char) (c - 'A' + 'a') : c;
		}
		Id[k] = '\0';
		CAdminText Main, Manifest;
		Main.Printf ("%s/%s/main.bas", APPS_DIR, pList->Names[i]);
		Manifest.Printf ("%s/%s/app.txt", APPS_DIR, pList->Names[i]);
		int n;
		if (!pList->bDir[i] || !AdminFileExists (Main.Get ())
		    || (n = AdminFileRead (Manifest.Get (), pText, MANIFEST_MAX + 100)) < 0)
		{
			continue;
		}
		TAppManifest m;
		char Error[8];
		unsigned nLine;
		if (!ParseManifest (pText, (unsigned) n, &m, false, Error, sizeof Error, &nLine))
		{
			continue;
		}
		if (nApps++ > 0)
		{
			Body.Append (",");
		}
		JsonApp (&Body, Id, m);
	}
	Body.Append ("]}\n");
	free (pList);
	free (pText);
	SendJson (pResponse, 200, &Body);
}

static unsigned CountApps (const char *pExcept)
{
	TNameList *pList = (TNameList *) malloc (sizeof (TNameList));
	if (pList == 0)
	{
		return 0;
	}
	pList->nCount = 0;
	pList->bOverflow = false;
	AdminListDir (APPS_DIR, AddName, pList);
	unsigned n = 0;
	for (unsigned i = 0; i < pList->nCount; i++)
	{
		n += pList->bDir[i] && strcasecmp (pList->Names[i], pExcept) != 0;
	}
	free (pList);
	return n;
}

// ---- POST /api/apps/<id> -------------------------------------------------------------------------

// les données de l'ancienne version que le paquet n'apporte pas : copiées (fichiers) ou déplacées (dossiers) dans
// le nouveau dossier ; *pMoved : des dossiers ont été déplacés (à remettre si l'installation échoue)
static bool KeepOldData (const char *pOld, const char *pNew, const TAppPackage &rPkg, CAdminText *pKept,
			 char *pError, unsigned nErrorSize)
{
	TNameList *pList = (TNameList *) malloc (sizeof (TNameList));
	char *pBuffer = (char *) malloc (MAX_KEEP + 1);
	bool bOK = pList != 0 && pBuffer != 0;
	if (!bOK)
	{
		snprintf (pError, nErrorSize, "mémoire insuffisante");
	}
	else
	{
		pList->nCount = 0;
		pList->bOverflow = false;
		AdminListDir (pOld, AddName, pList);
		if (pList->bOverflow)
		{
			snprintf (pError, nErrorSize, "l'app installée a trop de fichiers pour garder ses données");
			bOK = false;
		}
	}
	for (unsigned i = 0; bOK && i < pList->nCount; i++)
	{
		const char *pName = pList->Names[i];
		if (pList->bDir[i] || FindFile (rPkg, pName) != 0)
		{
			continue;			// (les dossiers : déplacés après, par MoveOldDirs)
		}
		CAdminText From, To;
		From.Printf ("%s/%s", pOld, pName);
		To.Printf ("%s/%s", pNew, pName);
		int n = AdminFileRead (From.Get (), pBuffer, MAX_KEEP + 1);
		if (n < 0 || n >= MAX_KEEP)
		{
			snprintf (pError, nErrorSize, "donnée %s de l'app installée illisible ou trop grosse pour être gardée", pName);
			bOK = false;
		}
		else if (!AdminFileWrite (To.Get (), pBuffer, (unsigned) n))
		{
			snprintf (pError, nErrorSize, "écriture de %s impossible (carte pleine ?)", pName);
			bOK = false;
		}
		else
		{
			pKept->Append (pKept->GetLength () > 0 ? ",\"" : "\"");
			pKept->Append (pName);
			pKept->Append ("\"");
		}
	}
	free (pList);
	free (pBuffer);
	return bOK;
}

// les sous-dossiers de pFrom (pas dans le paquet : il n'a que des fichiers) déplacés dans pTo ; false au premier échec
static bool MoveDirs (const char *pFrom, const char *pTo)
{
	TNameList *pList = (TNameList *) malloc (sizeof (TNameList));
	if (pList == 0)
	{
		return false;
	}
	pList->nCount = 0;
	pList->bOverflow = false;
	AdminListDir (pFrom, AddName, pList);
	bool bOK = !pList->bOverflow;
	for (unsigned i = 0; bOK && i < pList->nCount; i++)
	{
		if (!pList->bDir[i])
		{
			continue;
		}
		CAdminText From, To;
		From.Printf ("%s/%s", pFrom, pList->Names[i]);
		To.Printf ("%s/%s", pTo, pList->Names[i]);
		bOK = AdminRename (From.Get (), To.Get ());
	}
	free (pList);
	return bOK;
}

static bool s_bBusy;

static void InstallApp (const char *pId, const THttpRequest &rRequest, CAdminText *pResponse)
{
	char Error[300];
	TAppPackage *pPkg = (TAppPackage *) malloc (sizeof (TAppPackage));
	if (pPkg == 0)
	{
		SendError (pResponse, 500, "mémoire insuffisante");
		return;
	}
	if (!ParsePackage (rRequest.pBody, rRequest.nBodyLength, pPkg, Error, sizeof Error))
	{
		free (pPkg);
		SendError (pResponse, 400, Error);
		return;
	}

	// le manifeste
	const TAppFile *pManifest = FindFile (*pPkg, "app.txt");
	const TAppFile *pMain = FindFile (*pPkg, "main.bas");
	if (pManifest == 0 || pMain == 0)
	{
		free (pPkg);
		SendError (pResponse, 422, pManifest == 0 ? "app.txt manque dans le paquet" : "main.bas manque dans le paquet");
		return;
	}
	unsigned nLine;
	TAppManifest m;
	if (pManifest->nLength > MANIFEST_MAX)
	{
		snprintf (Error, sizeof Error, "app.txt trop long (%u octets, %u au plus)", pManifest->nLength, MANIFEST_MAX);
		free (pPkg);
		SendError (pResponse, 422, Error, "app.txt", 0);
		return;
	}
	if (!ParseManifest (pManifest->pData, pManifest->nLength, &m, true, Error, sizeof Error, &nLine))
	{
		free (pPkg);
		SendError (pResponse, 422, Error, "app.txt", nLine);
		return;
	}

	// la compilation
	char File[16];
	if (!AdminAppCompile (pMain->pData, pMain->nLength, File, sizeof File, &nLine, Error, sizeof Error))
	{
		free (pPkg);
		SendError (pResponse, 422, Error, File, nLine);
		return;
	}

	CAdminText Final, New, Old;
	Final.Printf ("%s/%s", APPS_DIR, pId);
	New.Printf ("%s/%s.new", APPS_TMP, pId);
	Old.Printf ("%s/%s.old", APPS_TMP, pId);
	bool bReplace = AdminFileExists (Final.Get ());
	if (!bReplace && CountApps (pId) >= ADMIN_APPS_MAX)
	{
		free (pPkg);
		snprintf (Error, sizeof Error, "déjà %u apps sur le robot : en supprimer une d'abord", ADMIN_APPS_MAX);
		SendError (pResponse, 409, Error);
		return;
	}

	// SD:/tmp/apps/<id>.new : les fichiers du paquet, puis les données gardées
	AdminMakeDir (APPS_DIR);
	AdminMakeDir (APPS_TMP0);
	AdminMakeDir (APPS_TMP);
	RemoveTree (New.Get ());
	RemoveTree (Old.Get ());
	bool bOK = AdminMakeDir (New.Get ());
	if (!bOK)
	{
		snprintf (Error, sizeof Error, "dossier %s impossible à créer", New.Get ());
	}
	for (unsigned i = 0; bOK && i < pPkg->nFiles; i++)
	{
		CAdminText Path;
		Path.Printf ("%s/%s", New.Get (), pPkg->Files[i].Name);
		bOK = AdminFileWrite (Path.Get (), pPkg->Files[i].pData, pPkg->Files[i].nLength);
		if (!bOK)
		{
			snprintf (Error, sizeof Error, "écriture de %s impossible (carte pleine ?)", pPkg->Files[i].Name);
		}
	}
	CAdminText Kept;
	if (bOK && bReplace)
	{
		bOK = KeepOldData (Final.Get (), New.Get (), *pPkg, &Kept, Error, sizeof Error);
	}
	if (!bOK)
	{
		RemoveTree (New.Get ());
		free (pPkg);
		SendError (pResponse, 500, Error);
		return;
	}

	// les deux renommages (et les sous-dossiers de données, déplacés de l'ancienne à la nouvelle)
	if (bReplace)
	{
		if (!MoveDirs (Final.Get (), New.Get ()))
		{
			MoveDirs (New.Get (), Final.Get ());
			RemoveTree (New.Get ());
			free (pPkg);
			SendError (pResponse, 500, "les dossiers de données de l'app n'ont pas pu être déplacés : rien n'a changé");
			return;
		}
		if (!AdminRename (Final.Get (), Old.Get ()))
		{
			MoveDirs (New.Get (), Final.Get ());
			RemoveTree (New.Get ());
			free (pPkg);
			SendError (pResponse, 500, "l'app installée est occupée (en cours d'exécution ?) : rien n'a changé, réessayer");
			return;
		}
	}
	if (!AdminRename (New.Get (), Final.Get ()))
	{
		if (bReplace)
		{
			AdminRename (Old.Get (), Final.Get ());
			MoveDirs (New.Get (), Final.Get ());
		}
		RemoveTree (New.Get ());
		free (pPkg);
		SendError (pResponse, 500, "renommage impossible : l'ancienne version est gardée");
		return;
	}
	if (bReplace)
	{
		RemoveTree (Old.Get ());
	}
	AdminLog ("apps: %s %s (%u files)", pId, bReplace ? "replaced" : "installed", pPkg->nFiles);
	AdminAppsChanged (pId);

	CAdminText Body;
	Body.Printf ("{\"ok\":true,\"replaced\":%s,\"files\":%u,\"kept\":[", bReplace ? "true" : "false", pPkg->nFiles);
	Body.Append (Kept.Get ());
	Body.Append ("],\"app\":");
	free (pPkg);
	// (le manifeste relu depuis la carte : c'est bien ce qui est installé)
	char *pText = (char *) malloc (MANIFEST_MAX + 100);
	CAdminText Manifest;
	Manifest.Printf ("%s/app.txt", Final.Get ());
	int n = pText != 0 ? AdminFileRead (Manifest.Get (), pText, MANIFEST_MAX + 100) : -1;
	if (n >= 0 && ParseManifest (pText, (unsigned) n, &m, false, Error, sizeof Error, &nLine))
	{
		JsonApp (&Body, pId, m);
	}
	else
	{
		Body.Append ("null");
	}
	free (pText);
	Body.Append ("}\n");
	SendJson (pResponse, 200, &Body);
}

// ---- DELETE /api/apps/<id> -----------------------------------------------------------------------

static void DeleteApp (const char *pId, CAdminText *pResponse)
{
	CAdminText Final, Del;
	Final.Printf ("%s/%s", APPS_DIR, pId);
	Del.Printf ("%s/%s.del", APPS_TMP, pId);
	if (!AdminFileExists (Final.Get ()))
	{
		SendError (pResponse, 404, "cette app n'est pas sur le robot");
		return;
	}
	AdminMakeDir (APPS_TMP0);
	AdminMakeDir (APPS_TMP);
	RemoveTree (Del.Get ());
	// d'abord hors de SD:/apps (un seul renommage : l'app disparaît d'un coup), puis effacée
	if (!AdminRename (Final.Get (), Del.Get ()))
	{
		SendError (pResponse, 500, "l'app est occupée (en cours d'exécution ?) : réessayer");
		return;
	}
	if (!RemoveTree (Del.Get ()))
	{
		AdminLog ("apps: %s not entirely erased", Del.Get ());
	}
	AdminLog ("apps: %s deleted", pId);
	AdminAppsChanged (pId);
	CAdminText Body;
	Body.Append ("{\"ok\":true,\"deleted\":");
	JsonString (&Body, pId);
	Body.Append ("}\n");
	SendJson (pResponse, 200, &Body);
}

// ---- GET /api/apps/<id> --------------------------------------------------------------------------

// l'app installée, en paquet (le même format que POST) : app.txt, main.bas puis ses autres fichiers texte, pour que
// PocketQI Studio la récupère. Ce qui ne pourrait pas être réinstallé tel quel (sous-dossier, fichier binaire, trop gros,
// nom refusé, au-delà de 16 fichiers ou 64 Ko) n'est pas mis : l'en-tête X-Pocket-Skipped en donne la liste.
static void GetApp (const char *pId, CAdminText *pResponse)
{
	CAdminText Dir;
	Dir.Printf ("%s/%s", APPS_DIR, pId);
	TNameList *pList = (TNameList *) malloc (sizeof (TNameList));
	char *pBuffer = (char *) malloc (ADMIN_APP_MAX_FILE + 2);
	if (pList == 0 || pBuffer == 0)
	{
		free (pList);
		free (pBuffer);
		SendError (pResponse, 500, "mémoire insuffisante");
		return;
	}
	pList->nCount = 0;
	pList->bOverflow = false;
	if (AdminListDir (Dir.Get (), AddName, pList) < 0)
	{
		free (pList);
		free (pBuffer);
		SendError (pResponse, 404, "cette app n'est pas sur le robot");
		return;
	}

	CAdminText Body, Skipped;
	Body.Append ("POCKETAPP 1\n");
	unsigned nFiles = 0;
	// app.txt, main.bas, puis le reste dans l'ordre du répertoire
	for (unsigned nPass = 0; nPass < 3; nPass++)
	{
		for (unsigned i = 0; i < pList->nCount; i++)
		{
			const char *pName = pList->Names[i];
			unsigned nWant = strcasecmp (pName, "app.txt") == 0 ? 0 : strcasecmp (pName, "main.bas") == 0 ? 1 : 2;
			if (nWant != nPass)
			{
				continue;
			}
			const char *pWhy = 0;
			int n = -1;
			if (pList->bDir[i])
			{
				pWhy = "/";
			}
			else if (!FileNameValid (pName))
			{
				pWhy = "";
			}
			else
			{
				CAdminText Path;
				Path.Printf ("%s/%s", Dir.Get (), pName);
				n = AdminFileRead (Path.Get (), pBuffer, ADMIN_APP_MAX_FILE + 2);
				if (n < 0 || n > ADMIN_APP_MAX_FILE || TextBadLine (pBuffer, (unsigned) n) != 0
				    || nFiles == ADMIN_APP_MAX_FILES || Body.GetLength () + n + 48 > ADMIN_APP_MAX_BODY)
				{
					pWhy = "";
				}
			}
			if (pWhy != 0)
			{
				if (FileNameValid (pName))
				{
					Skipped.Append (Skipped.GetLength () > 0 ? ", " : "");
					Skipped.Append (pName);
					Skipped.Append (pWhy);
				}
				continue;
			}
			Body.Printf ("F %s %d\n", pName, n);
			Body.Append (pBuffer, (unsigned) n);
			Body.Append ("\n");
			nFiles++;
		}
	}
	Body.Append ("END\n");
	free (pList);
	free (pBuffer);
	if (Body.Failed ())
	{
		SendError (pResponse, 500, "mémoire insuffisante");
		return;
	}
	CAdminText Extra;
	if (Skipped.GetLength () > 0)
	{
		Extra.Printf ("X-Pocket-Skipped: %s\r\n", Skipped.Get ());
	}
	AdminLog ("apps: %s sent (%u files)", pId, nFiles);
	HttpResponse (pResponse, 200, "application/x-galo-app", Body.Get (), Body.GetLength (),
		      Extra.GetLength () > 0 ? Extra.Get () : 0);
}

// ---- POST /api/apps/<id>/run ---------------------------------------------------------------------

// lancer l'app sans le LLM (F5 de PocketQI Studio quand la cible est un robot) : le corps est l'action, puis un argument
// par ligne. L'app doit être installée et l'action dans son manifeste ; le shell vérifie le reste (types) comme pour
// « /run ». Seul le shell sait lancer une app : il installe son lanceur (AdminSetAppRunner), sinon 501.
#define RUN_MAX_ARGS	5		// GROQ_MAX_ARGS (6) moins l'action
#define RUN_MAX_ARG	1000		// groq_outcome::args : 1200 octets

static TAdminAppRunner *s_pRunner;

void AdminSetAppRunner (TAdminAppRunner *pRunner)
{
	s_pRunner = pRunner;
}

static void RunApp (const char *pId, const THttpRequest &rRequest, CAdminText *pResponse)
{
	if (s_pRunner == 0)
	{
		SendError (pResponse, 501, "ce robot ne lance pas les apps à distance");
		return;
	}
	// le corps : l'action, puis les arguments, une ligne chacun
	static char Lines[1 + RUN_MAX_ARGS][RUN_MAX_ARG + 1];
	unsigned nLines = 0;
	const char *p = rRequest.pBody, *pEnd = rRequest.pBody + rRequest.nBodyLength;
	if (TextBadLine (p, rRequest.nBodyLength) != 0)
	{
		SendError (pResponse, 400, "corps : du texte, l'action puis un argument par ligne");
		return;
	}
	while (p < pEnd)
	{
		const char *e = p;
		while (e < pEnd && *e != '\n') e++;
		const char *t = e;
		if (t > p && t[-1] == '\r') t--;
		if (nLines == 1 + RUN_MAX_ARGS || t - p > RUN_MAX_ARG)
		{
			SendError (pResponse, 400, nLines == 1 + RUN_MAX_ARGS ? "trop d'arguments (5 au plus)" : "argument trop long");
			return;
		}
		memcpy (Lines[nLines], p, (size_t) (t - p));
		Lines[nLines++][t - p] = '\0';
		p = e + 1;
	}
	if (nLines == 0 || Lines[0][0] == '\0')
	{
		SendError (pResponse, 400, "l'action manque (première ligne du corps)");
		return;
	}

	// l'app installée, et l'action dans son manifeste
	CAdminText Main, Manifest;
	Main.Printf ("%s/%s/main.bas", APPS_DIR, pId);
	Manifest.Printf ("%s/%s/app.txt", APPS_DIR, pId);
	char *pText = (char *) malloc (MANIFEST_MAX + 100);
	int n = pText != 0 && AdminFileExists (Main.Get ()) ? AdminFileRead (Manifest.Get (), pText, MANIFEST_MAX + 100) : -1;
	TAppManifest m;
	char Error[8];
	unsigned nLine;
	if (n < 0 || !ParseManifest (pText, (unsigned) n, &m, false, Error, sizeof Error, &nLine))
	{
		free (pText);
		SendError (pResponse, 404, "app absente du robot : l'envoyer d'abord");
		return;
	}
	bool bKnown = false;
	for (unsigned i = 0; i < m.nActions && !bKnown; i++)
	{
		bKnown = m.Actions[i].nName == strlen (Lines[0]) && strncasecmp (m.Actions[i].pName, Lines[0], m.Actions[i].nName) == 0;
	}
	free (pText);
	if (!bKnown)
	{
		CAdminText Why;
		Why.Printf ("action inconnue de %s : %s", pId, Lines[0]);
		SendError (pResponse, 400, Why.Get ());
		return;
	}

	const char *Args[RUN_MAX_ARGS];
	for (unsigned i = 1; i < nLines; i++)
	{
		Args[i - 1] = Lines[i];
	}
	char Refused[160] = "";
	if (!(*s_pRunner) (pId, Lines[0], Args, nLines - 1, Refused, sizeof Refused))
	{
		SendError (pResponse, 409, Refused[0] != '\0' ? Refused : "le robot ne peut pas lancer l'app maintenant");
		return;
	}
	AdminLog ("apps: %s %s launched", pId, Lines[0]);
	CAdminText Body;
	Body.Append ("{\"ok\":true}\n");
	SendJson (pResponse, 200, &Body);
}

// ---- l'aiguillage ----------------------------------------------------------------------------------

bool AdminIsAppsPath (const char *pPath)
{
	return strcmp (pPath, "/api/apps") == 0 || strncmp (pPath, "/api/apps/", 10) == 0;
}

void AdminAppsRequest (const THttpRequest &rRequest, CAdminText *pResponse)
{
	bool bGet = strcmp (rRequest.Method, "GET") == 0;
	bool bPost = strcmp (rRequest.Method, "POST") == 0;
	bool bDelete = strcmp (rRequest.Method, "DELETE") == 0;
	const char *pPath = rRequest.Path;

	if ((bPost || bDelete) && rRequest.Origin[0] != '\0')
	{
		SendError (pResponse, 403, "requête venue d'une page web : refusée (seul PocketQI Studio installe des apps)");
		return;
	}
	if (!AuthOK (rRequest))
	{
		SendError (pResponse, 401, "mot de passe d'administration demandé", 0, 0,
			   "WWW-Authenticate: Basic realm=\"Pocket\", charset=\"UTF-8\"\r\n");
		return;
	}

	if (strcmp (pPath, "/api/apps") == 0)
	{
		if (bGet)
		{
			ListApps (pResponse);
		}
		else
		{
			SendError (pResponse, 405, "GET seulement");
		}
		return;
	}

	const char *pId = pPath + 10;
	const char *pRun = strchr (pId, '/');
	if (pRun != 0 && strcmp (pRun, "/run") == 0 && pRun - pId <= MAX_ID)
	{
		char Id[MAX_ID + 1];
		memcpy (Id, pId, (size_t) (pRun - pId));
		Id[pRun - pId] = '\0';
		if (!AdminAppIdValid (Id))
		{
			SendError (pResponse, 400, "nom d'app invalide : a-z, 0-9 et _ seulement (23 au plus)");
		}
		else if (!bPost)
		{
			SendError (pResponse, 405, "POST seulement");
		}
		else
		{
			RunApp (Id, rRequest, pResponse);
		}
		return;
	}
	if (!AdminAppIdValid (pId))
	{
		SendError (pResponse, 400, "nom d'app invalide : a-z, 0-9 et _ seulement (23 au plus)");
		return;
	}
	if (bGet)
	{
		GetApp (pId, pResponse);
		return;
	}
	if (!bPost && !bDelete)
	{
		SendError (pResponse, 405, "GET (récupérer), POST (installer) ou DELETE (supprimer)");
		return;
	}
	if (s_bBusy)
	{
		SendError (pResponse, 409, "une autre installation est en cours : réessayer");
		return;
	}
	s_bBusy = true;
	if (bPost)
	{
		InstallApp (pId, rRequest, pResponse);
	}
	else
	{
		DeleteApp (pId, pResponse);
	}
	s_bBusy = false;
}
