//
// locale.inc -- the code of locale.h (SystemKit: compiled into systemkit.so; a PC build has it inline).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. Permission is hereby
// granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions: The above copyright notice and this permission notice shall
// be included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS
// IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
//
#ifdef SK_IMPL
#define SK_FN	extern "C"
#else
#define SK_FN	static inline
#endif

// ---- system.ini's lines ----
// The line [s, e) of b is "key = ..." -> where its value starts (after the '='), -1 otherwise.
static inline int locale_line_key (const char *b, int s, int e, const char *key)
{
	while (s < e && (b[s] == ' ' || b[s] == '\t')) s++;
	int k = 0;
	for (; key[k]; k++) if (s + k >= e || b[s + k] != key[k]) return -1;
	s += k;
	while (s < e && (b[s] == ' ' || b[s] == '\t')) s++;
	if (s >= e || b[s] != '=') return -1;
	return s + 1;
}
static inline int locale_ini_read (char *b, int cap)
{
	int n = 0;
	void *f = kapi_open (LOCALE_INI);
	if (f) { n = kapi_read (f, b, (unsigned) cap - 1); kapi_close (f); if (n < 0) n = 0; }
	b[n] = 0;
	return n;
}
SK_FN int locale_ini_get (const char *key, char *out, int cap)
{
	static char in[8192];
	int n = locale_ini_read (in, sizeof in);
	if (cap > 0) out[0] = 0;
	for (int i = 0; i < n; )
	{
		int s = i; while (i < n && in[i] != '\n') i++;
		int e = i; if (i < n) i++;
		int v = locale_line_key (in, s, e, key);
		if (v < 0) continue;
		while (v < e && (in[v] == ' ' || in[v] == '\t')) v++;
		while (e > v && (in[e - 1] == ' ' || in[e - 1] == '\t' || in[e - 1] == '\r')) e--;
		int o = 0;
		for (; v < e && o < cap - 1; v++) out[o++] = in[v];
		if (cap > 0) out[o] = 0;
		return 1;
	}
	return 0;
}
SK_FN int locale_ini_set (const char *key, const char *value)
{
	static char in[8192], out[8400];
	int n = locale_ini_read (in, sizeof in);
	int o = 0, done = 0;
	for (int i = 0; i < n; )
	{
		int s = i; while (i < n && in[i] != '\n') i++;
		int e = i; if (i < n) i++;
		if (locale_line_key (in, s, e, key) >= 0)
		{
			if (!done)
			{
				for (int k = 0; key[k] && o < (int) sizeof out - 3; k++) out[o++] = key[k];
				out[o++] = '=';
				for (int k = 0; value[k] && o < (int) sizeof out - 2; k++) out[o++] = value[k];
				out[o++] = '\n';
			}
			done = 1;
			continue;
		}
		for (int k = s; k < i && o < (int) sizeof out - 2; k++) out[o++] = in[k];
		if (i == n && e == n && n > 0 && in[n - 1] != '\n') out[o++] = '\n';
	}
	if (!done)
	{
		for (int k = 0; key[k] && o < (int) sizeof out - 3; k++) out[o++] = key[k];
		out[o++] = '=';
		for (int k = 0; value[k] && o < (int) sizeof out - 2; k++) out[o++] = value[k];
		out[o++] = '\n';
	}
	return kapi_save_file (LOCALE_INI, out, (unsigned) o) >= 0 ? 1 : 0;
}

// ---- the languages ----
#define LOCALE_NLANG	2
static inline const char *locale_lang_word (int i, int name)
{
	static const char *const L[LOCALE_NLANG][2] = { { "en", "English" }, { "fr", "Fran\xC3\xA7" "ais" } };
	return i >= 0 && i < LOCALE_NLANG ? L[i][name] : "";
}
SK_FN int locale_language_count (void) { return LOCALE_NLANG; }
SK_FN const char *locale_language_code (int i) { return locale_lang_word (i, 0); }
SK_FN const char *locale_language_name (int i) { return locale_lang_word (i, 1); }
SK_FN int locale_language_index (void)
{
	char v[16];
	if (!locale_ini_get ("language", v, sizeof v)) return 0;
	for (int k = 0; v[k]; k++) if (v[k] >= 'A' && v[k] <= 'Z') v[k] = (char) (v[k] - 'A' + 'a');
	for (int i = 0; i < LOCALE_NLANG; i++) if (ax_streq (v, locale_lang_word (i, 0))) return i;
	return 0;
}
SK_FN const char *locale_language (void) { return locale_lang_word (locale_language_index (), 0); }
SK_FN int locale_set_language (const char *code)
{
	for (int i = 0; i < LOCALE_NLANG; i++)
		if (ax_streq (code, locale_lang_word (i, 0))) return locale_ini_set ("language", code);
	return 0;
}

// ---- the time zones ----
enum { LOCALE_DST_NONE, LOCALE_DST_EU, LOCALE_DST_US };
struct LocaleZone { const char *city; int std, dst; };
#define LOCALE_NZONES	23
static inline const struct LocaleZone *locale_zone_at (int z)
{
	static const struct LocaleZone Z[LOCALE_NZONES] = {
		{ "Brussels", 60, LOCALE_DST_EU }, { "Paris", 60, LOCALE_DST_EU }, { "Amsterdam", 60, LOCALE_DST_EU }, { "Luxembourg", 60, LOCALE_DST_EU },
		{ "Berlin", 60, LOCALE_DST_EU }, { "Zurich", 60, LOCALE_DST_EU }, { "Vienna", 60, LOCALE_DST_EU }, { "Rome", 60, LOCALE_DST_EU },
		{ "Madrid", 60, LOCALE_DST_EU }, { "Stockholm", 60, LOCALE_DST_EU }, { "Warsaw", 60, LOCALE_DST_EU }, { "London", 0, LOCALE_DST_EU },
		{ "Dublin", 0, LOCALE_DST_EU }, { "Lisbon", 0, LOCALE_DST_EU }, { "Helsinki", 120, LOCALE_DST_EU }, { "Athens", 120, LOCALE_DST_EU },
		{ "New York", -300, LOCALE_DST_US }, { "Toronto", -300, LOCALE_DST_US }, { "Chicago", -360, LOCALE_DST_US }, { "Denver", -420, LOCALE_DST_US },
		{ "Los Angeles", -480, LOCALE_DST_US }, { "Tokyo", 540, LOCALE_DST_NONE }, { "UTC", 0, LOCALE_DST_NONE } };
	return z >= 0 && z < LOCALE_NZONES ? &Z[z] : 0;
}
static inline int locale_dow (int y, int m, int d)		// 0 = Sunday (Sakamoto)
{
	static const int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
	if (m < 3) y--;
	return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}
SK_FN int locale_zone_count (void) { return LOCALE_NZONES; }
SK_FN const char *locale_zone_city (int z) { const struct LocaleZone *p = locale_zone_at (z); return p ? p->city : ""; }
// (The day only: the hour of the change is not looked at.)
SK_FN int locale_zone_summer (int z)
{
	const struct LocaleZone *p = locale_zone_at (z);
	if (!p || p->dst == LOCALE_DST_NONE) return 0;
	int y = 0, mo = 0, d = 0, h = 0, mi = 0;
	kapi_get_datetime (&y, &mo, &d, &h, &mi, 0);
	if (y < 2000 || mo < 1 || mo > 12) return 0;		// (no clock yet)
	if (p->dst == LOCALE_DST_EU)
	{
		int mar = 31 - locale_dow (y, 3, 31), oct = 31 - locale_dow (y, 10, 31);	// the last Sundays
		return (mo > 3 && mo < 10) || (mo == 3 && d >= mar) || (mo == 10 && d < oct);
	}
	int mar = 1 + (7 - locale_dow (y, 3, 1)) % 7 + 7, nov = 1 + (7 - locale_dow (y, 11, 1)) % 7;	// 2nd Sunday of March, 1st of November
	return (mo > 3 && mo < 11) || (mo == 3 && d >= mar) || (mo == 11 && d < nov);
}
SK_FN int locale_zone_offset (int z)
{
	const struct LocaleZone *p = locale_zone_at (z);
	return p ? p->std + (locale_zone_summer (z) ? 60 : 0) : 0;
}
SK_FN void locale_zone_utc (int z, char *out, int cap)
{
	int m = locale_zone_offset (z), a = m < 0 ? -m : m, n = 0;
	char num[16];
	ax_strcat (out, cap, &n, "UTC");
	if (!m) return;
	ax_strcat (out, cap, &n, m < 0 ? "-" : "+");
	ax_itoa (a / 60, num); ax_strcat (out, cap, &n, num);
	if (a % 60) { ax_strcat (out, cap, &n, ":"); ax_fmt2 (num, a % 60); num[2] = 0; ax_strcat (out, cap, &n, num); }
}
SK_FN int locale_zone (void)
{
	char v[32];
	if (locale_ini_get ("zone", v, sizeof v))
		for (int z = 0; z < LOCALE_NZONES; z++) if (ax_streq (v, locale_zone_at (z)->city)) return z;
	if (!locale_ini_get ("timezone", v, sizeof v) || !v[0]) return -1;
	int neg = v[0] == '-', m = 0, k = neg;
	for (; v[k] >= '0' && v[k] <= '9'; k++) m = m * 10 + (v[k] - '0');
	if (neg) m = -m;
	for (int z = 0; z < LOCALE_NZONES; z++) if (locale_zone_offset (z) == m) return z;
	return -1;
}
SK_FN int locale_set_zone (int z)
{
	const struct LocaleZone *p = locale_zone_at (z);
	if (!p) return 0;
	int m = locale_zone_offset (z);
	kapi_set_timezone (m);
	char num[16]; int n = 0;
	if (m < 0) { num[n++] = '-'; m = -m; }
	ax_itoa (m, num + n);
	return locale_ini_set ("timezone", num) && locale_ini_set ("zone", p->city);
}

#undef SK_FN
