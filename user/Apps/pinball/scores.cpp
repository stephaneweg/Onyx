//
// scores.cpp -- Pinball's high scores (scores.h): the top 5 of a table and the settings, over an fk_kv document.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#include "scores.h"
#include <stdio.h>
#include <string.h>

namespace pinball {

static const char *const SETTINGS = "settings";
static const char *const KEYS[TOPN] = { "1", "2", "3", "4", "5" };

// "<digits> <name>" -> true
static bool read_line (const char *s, ScoreLine &l)
{
	if (*s < '0' || *s > '9') return false;
	long v = 0;
	int digits = 0;
	while (*s >= '0' && *s <= '9') { if (++digits > 12) return false; v = v * 10 + (*s++ - '0'); }
	if (*s != ' ') return false;
	while (*s == ' ') s++;
	if (!*s) return false;
	l.score = v;
	scores_clean_name (s, l.name);
	return true;
}

int scores_read (const fk_kv *kv, const char *section, ScoreLine out[TOPN])
{
	int n = 0;
	for (int k = 0; k < TOPN; k++)
	{
		const char *s = fk_kv_get (kv, section, KEYS[k], 0);
		if (!s || !read_line (s, out[n])) continue;
		int i = n++;
		while (i > 0 && out[i - 1].score < out[i].score) { ScoreLine x = out[i]; out[i] = out[i - 1]; out[i - 1] = x; i--; }
	}
	return n;
}

bool scores_qualifies (const fk_kv *kv, const char *section, long score)
{
	if (score <= 0) return false;
	ScoreLine l[TOPN];
	int n = scores_read (kv, section, l);
	return n < TOPN || score > l[TOPN - 1].score;
}

void scores_clean_name (const char *name, char *out)
{
	int o = 0, chars = 0;
	while (*name == ' ' || *name == '=' || *name == '\n' || *name == '\r' || *name == '\t') name++;
	for (; *name && o < NAMEL - 1; name++)
	{
		unsigned char c = (unsigned char) *name;
		if ((c & 0xC0) != 0x80 && ++chars > NAMEC) break;
		out[o++] = c == '=' || c == '\n' || c == '\r' || c == '\t' ? ' ' : (char) c;
	}
	while (o > 0 && out[o - 1] == ' ') o--;
	out[o] = 0;
	if (!o) snprintf (out, NAMEL, "Player");
}

int scores_add (fk_kv *kv, const char *section, long score, const char *name)
{
	if (!scores_qualifies (kv, section, score)) return -1;
	ScoreLine l[TOPN + 1];
	int n = scores_read (kv, section, l);
	int at = 0;
	while (at < n && l[at].score >= score) at++;		// equal scores: the older stays above
	for (int i = n; i > at; i--) l[i] = l[i - 1];
	l[at].score = score;
	scores_clean_name (name, l[at].name);
	if (++n > TOPN) n = TOPN;
	char v[32 + NAMEL];
	for (int k = 0; k < n; k++)
	{
		snprintf (v, sizeof v, "%ld %s", l[k].score, l[k].name);
		fk_kv_set (kv, section, KEYS[k], v);
	}
	for (int k = n; k < TOPN; k++) while (fk_kv_remove (kv, section, KEYS[k])) {}
	return at;
}

void scores_section (const char *path, bool shipped, char *out, int cap)
{
	const char *base = fs_basename (path);
	int n = (int) strlen (base);
	if (n > 6 && !fs_ci_cmp (base + n - 6, ".table")) n -= 6;
	snprintf (out, cap, "%s%.*s", shipped ? "" : "user.", n, base);
}

const char *scores_setting (const fk_kv *kv, const char *key, const char *def) { return fk_kv_get (kv, SETTINGS, key, def); }
void scores_set_setting (fk_kv *kv, const char *key, const char *value) { fk_kv_set (kv, SETTINGS, key, value); }

}
