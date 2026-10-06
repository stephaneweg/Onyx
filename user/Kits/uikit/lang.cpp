//
// uikit/lang.cpp -- see lang.h. The catalogues are read whole; a word is found by its English text's hash
// (open addressing), each translation unescaped in place: TR's pointers stay valid for the app's life.
//
#include "uikit/lang.h"
#include "appkit/appkit.h"
#include "systemkit/locale.h"	// the system's language (SD:/etc/system.ini)
#include "uikit/sysclip.h"	// uk_clip_ready: SystemKit opened for the library
#include "uikit/text.h"		// uk_textface, uk_u8_get

static char  s_code[8] = "en";
static char  s_chosen[8];
static char **s_key, **s_val;			// the table (s_cap slots, a power of 2)
static unsigned s_cap, s_n;

static unsigned lg_hash (const char *a, const char *b)	// FNV-1a over a then b (b may be 0)
{
	unsigned h = 2166136261u;
	for (; *a; a++) { h ^= (unsigned char) *a; h *= 16777619u; }
	if (b) { h ^= '|'; h *= 16777619u; for (; *b; b++) { h ^= (unsigned char) *b; h *= 16777619u; } }
	return h;
}
static bool lg_eq (const char *k, const char *a, const char *b)	// k == a (or a "|" b)
{
	while (*a && *k == *a) { k++; a++; }
	if (*a) return false;
	if (!b) return *k == '\0';
	if (*k++ != '|') return false;
	while (*b && *k == *b) { k++; b++; }
	return !*b && *k == '\0';
}
static const char *lg_find (const char *a, const char *b)
{
	if (!s_n) return 0;
	for (unsigned i = lg_hash (a, b) & (s_cap - 1);; i = (i + 1) & (s_cap - 1))
	{
		if (!s_key[i]) return 0;
		if (lg_eq (s_key[i], a, b)) return s_val[i];
	}
}
const char *uk_tr (const char *en)
{
	if (!en || !s_n || !en[0]) return en;
	const char *t = lg_find (en, 0);
	return t ? t : en;
}
const char *uk_trc (const char *ctx, const char *en)
{
	if (!en || !s_n || !en[0]) return en;
	const char *t = lg_find (ctx, en);
	if (!t) t = lg_find (en, 0);
	return t ? t : en;
}

static void lg_put (char *k, char *v)
{
	if ((s_n + 1) * 2 > s_cap)			// (grown: half full at most)
	{
		unsigned nc = s_cap ? s_cap * 2 : 1024;
		char **nk = new char *[nc], **nv = new char *[nc];
		for (unsigned i = 0; i < nc; i++) { nk[i] = 0; nv[i] = 0; }
		for (unsigned i = 0; i < s_cap; i++)
			if (s_key[i]) { unsigned j = lg_hash (s_key[i], 0) & (nc - 1); while (nk[j]) j = (j + 1) & (nc - 1); nk[j] = s_key[i]; nv[j] = s_val[i]; }
		delete [] s_key; delete [] s_val;
		s_key = nk; s_val = nv; s_cap = nc;
	}
	unsigned i = lg_hash (k, 0) & (s_cap - 1);
	while (s_key[i]) { if (lg_eq (s_key[i], k, 0)) { s_val[i] = v; return; } i = (i + 1) & (s_cap - 1); }
	s_key[i] = k; s_val[i] = v; s_n++;
}
static char *lg_unescape (char *s)		// in place
{
	char *o = s, *r = s;
	while (*s)
	{
		if (*s == '\\' && s[1]) { s++; *o++ = *s == 't' ? '\t' : *s == 'n' ? '\n' : *s; s++; }
		else *o++ = *s++;
	}
	*o = '\0';
	return r;
}
// UTF-8 -> Latin-1, in place, for an app drawing with uikit's bitmap fonts (no face installed: one byte a glyph,
// as its books' text): the euro is 0x80 (as the keymaps send it), a few typographic marks plain ones.
static char *lg_latin1 (char *s)
{
	int n = 0; while (s[n]) n++;
	char *o = s;
	for (int i = 0; i < n; )
	{
		int k; unsigned c = uikit::uk_u8_get (s + i, n - i, &k);
		i += k;
		if (c < 0x100) *o++ = (char) c;
		else if (c == 0x20AC) *o++ = (char) 0x80;
		else if (c == 0x2018 || c == 0x2019) *o++ = '\'';
		else if (c == 0x201C || c == 0x201D) *o++ = '"';
		else if (c == 0x2013 || c == 0x2014) *o++ = '-';
		else if (c == 0x2026) { *o++ = '.'; *o++ = '.'; *o++ = '.'; }
		else if (c == 0x0153) { *o++ = 'o'; *o++ = 'e'; }
		else if (c == 0x0152) { *o++ = 'O'; *o++ = 'E'; }
		else *o++ = '?';
	}
	*o = '\0';
	return s;
}
static bool lg_read (const char *path)
{
	void *f = kapi_open (path);
	if (!f) return false;
	unsigned n = kapi_fsize (f);
	char *b = new char[n + 1];			// (kept: the words point into it)
	int got = n ? kapi_read (f, b, n) : 0;
	kapi_close (f);
	if (got < 0) got = 0;
	b[got] = '\0';
	char *p = b;
	if ((unsigned char) p[0] == 0xEF && (unsigned char) p[1] == 0xBB && (unsigned char) p[2] == 0xBF) p += 3;	// (a BOM)
	while (*p)
	{
		char *line = p;
		while (*p && *p != '\n') p++;
		if (*p) *p++ = '\0';
		int k = 0; while (line[k]) k++;
		if (k && line[k - 1] == '\r') line[k - 1] = '\0';
		if (line[0] == '#' || !line[0]) continue;
		char *tab = line; while (*tab && *tab != '\t') tab++;
		if (!*tab) continue;
		*tab = '\0';
		char *v = tab + 1;
		if (!*v) continue;				// (not translated yet: the English stays)
		char *k8 = lg_unescape (line), *v8 = lg_unescape (v);
		if (!uikit::uk_textface ()) { lg_latin1 (k8); lg_latin1 (v8); }	// (a face's text is UTF-8: kept)
		lg_put (k8, v8);
	}
	return true;
}
static void lg_app_path (char *out, int cap, const char *tail)	// "SD:/apps/<app>.app/" + tail
{
	char d[128]; d[0] = '\0';
	if (kapi_app_dir (d, sizeof d) <= 0) d[0] = '\0';
	int n = 0; while (n < (int) sizeof d - 1 && d[n]) n++;		// (its length: some hosts answer 1)
	int o = 0;
	for (int i = 0; i < n && o < cap - 1; i++) { out[o++] = d[i]; if (d[i] == ':' && d[i + 1] != '/' && o < cap - 1) out[o++] = '/'; }
	if (o && out[o - 1] != '/' && o < cap - 1) out[o++] = '/';
	for (int i = 0; tail[i] && o < cap - 1; i++) out[o++] = tail[i];
	out[o] = '\0';
}
static void lg_copy (char *d, const char *s, int cap) { int i = 0; for (; s && s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = '\0'; }

bool uk_lang_load (const char *code)
{
	s_n = 0;
	for (unsigned i = 0; i < s_cap; i++) { s_key[i] = 0; s_val[i] = 0; }	// (the old words' text is kept: a pointer handed out stays valid)
	lg_copy (s_code, "en", sizeof s_code);
	if (!code || !code[0] || (code[0] == 'e' && code[1] == 'n' && !code[2])) return true;
	char p[160] = "SD:/res/lang/"; int k = 0; while (p[k]) k++;
	for (int i = 0; code[i] && k < 150; i++) p[k++] = code[i];
	p[k++] = '.'; p[k++] = 't'; p[k++] = 'x'; p[k++] = 't'; p[k] = '\0';
	bool any = lg_read (p);
	char tail[40] = "lang/"; k = 5;
	for (int i = 0; code[i] && k < 30; i++) tail[k++] = code[i];
	tail[k++] = '.'; tail[k++] = 't'; tail[k++] = 'x'; tail[k++] = 't'; tail[k] = '\0';
	lg_app_path (p, sizeof p, tail);
	any = lg_read (p) || any;
	if (any) lg_copy (s_code, code, sizeof s_code);
	return any;
}
const char *uk_lang () { return s_code; }
// The language chosen. On Onyx: the system's (SD:/etc/system.ini, systemkit/locale.h). An app ported to another
// system (the Mac's Ledger, pc/macOS: no Control Panel there) keeps its own, SD:/apps/<app>.app/lang.txt.
#ifdef __APPLE__
const char *uk_lang_chosen ()
{
	char p[160]; lg_app_path (p, sizeof p, "lang.txt");
	s_chosen[0] = '\0';
	void *f = kapi_open (p);
	if (!f) return s_chosen;
	char b[16]; int n = kapi_read (f, b, sizeof b - 1); kapi_close (f);
	int o = 0;
	for (int i = 0; i < n && o < (int) sizeof s_chosen - 1; i++)
	{
		char c = b[i];
		if (c >= 'A' && c <= 'Z') c = (char) (c - 'A' + 'a');
		if (c >= 'a' && c <= 'z') s_chosen[o++] = c;
		else if (o) break;
	}
	s_chosen[o] = '\0';
	return s_chosen;
}
bool uk_lang_choose (const char *code)
{
	char p[160]; lg_app_path (p, sizeof p, "lang.txt");
	int n = 0; while (code && code[n]) n++;
	return kapi_save_file (p, code ? code : "", (unsigned) n) >= 0;
}
#else
const char *uk_lang_chosen ()
{
	// (SystemKit is opened here when the program did not: without it, English)
	lg_copy (s_chosen, uikit::uk_clip_ready () ? locale_language () : "en", sizeof s_chosen);
	return s_chosen;
}
bool uk_lang_choose (const char *code) { return uikit::uk_clip_ready () && locale_set_language (code ? code : "en") != 0; }
#endif
void uk_lang_init () { const char *c = uk_lang_chosen (); if (c[0]) uk_lang_load (c); }
