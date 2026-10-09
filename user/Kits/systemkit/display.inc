//
// display.inc -- display.h's bodies (SystemKit; on the PC inline).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifdef SK_IMPL
#define SK_FN	extern "C"
#else
#define SK_FN	static inline
#endif
struct dp_mode { short w, h; const char *what; };
static const struct dp_mode DP_MODES[] = {
	{ 1024,  768, "4:3, XGA (Onyx's default)" }, { 1280,  720, "16:9, HD" }, { 1280,  800, "16:10, WXGA" },
	{ 1280, 1024, "5:4, SXGA" }, { 1366,  768, "16:9, laptop screens" }, { 1440,  900, "16:10, WXGA+" },
	{ 1600,  900, "16:9, HD+" }, { 1600, 1200, "4:3, UXGA" }, { 1680, 1050, "16:10, WSXGA+" },
	{ 1920, 1080, "16:9, Full HD" }, { 1920, 1200, "16:10, WUXGA" }, { 2560, 1440, "16:9, QHD" } };
static inline int dp_len (const char *s) { int n = 0; while (s && s[n]) n++; return n; }
static inline char dp_low (char c) { return c >= 'A' && c <= 'Z' ? (char) (c + 32) : c; }
// A whole file read into b -> its length, -1 none.
static inline int dp_read (const char *path, char *b, int cap)
{
	void *f = kapi_open (path);
	if (!f) return -1;
	int n = kapi_read (f, b, (unsigned) (cap - 1));
	kapi_close (f);
	if (n < 0) n = 0;
	b[n] = 0;
	return n;
}
static inline int dp_ncmp (const char *a, const char *b, int n) { for (int i = 0; i < n; i++) if (a[i] != b[i]) return 1; return 0; }
static inline void dp_num (char *b, int *k, int v) { char t[12]; int n = 0; if (v == 0) t[n++] = '0'; while (v > 0) { t[n++] = (char) ('0' + v % 10); v /= 10; } while (n) b[(*k)++] = t[--n]; }

SK_FN int display_modes (void) { return (int) (sizeof DP_MODES / sizeof DP_MODES[0]); }
SK_FN const char *display_mode (int i, int *w, int *h)
{
	if (i < 0 || i >= display_modes ()) return 0;
	if (w) *w = DP_MODES[i].w;
	if (h) *h = DP_MODES[i].h;
	return DP_MODES[i].what;
}
SK_FN int display_parse_size (const char *s, int *w, int *h)
{
	int a = 0, b = 0, k = 0;
	while (s[k] == ' ') k++;
	while (s[k] >= '0' && s[k] <= '9') a = a * 10 + (s[k++] - '0');
	while (s[k] == ' ') k++;
	if (s[k] != 'x' && s[k] != 'X' && s[k] != '*') return 0;
	k++;
	while (s[k] == ' ') k++;
	while (s[k] >= '0' && s[k] <= '9') b = b * 10 + (s[k++] - '0');
	if (a < 640 || b < 480 || a > 2560 || b > 1600) return 0;
	*w = a & ~1; *h = b;
	return 1;
}

SK_FN int display_save_size (int w, int h)
{
	static char in[1024], out[1100];
	int n = dp_read (DISPLAY_CMDLINE, in, sizeof in);
	if (n < 0) { n = 0; in[0] = 0; }
	int o = 0;
	const char *p = "width="; while (*p) out[o++] = *p++;
	dp_num (out, &o, w);
	p = " height="; while (*p) out[o++] = *p++;
	dp_num (out, &o, h);
	for (int i = 0; in[i]; )				// its other options kept
	{
		while (in[i] == ' ' || in[i] == '\t' || in[i] == '\r' || in[i] == '\n') i++;
		int s = i;
		while (in[i] && in[i] != ' ' && in[i] != '\t' && in[i] != '\r' && in[i] != '\n') i++;
		if (i == s) break;
		int size = (i - s > 6 && !dp_ncmp (in + s, "width=", 6)) || (i - s > 7 && !dp_ncmp (in + s, "height=", 7));
		if (size || o + (i - s) + 2 >= (int) sizeof out) continue;
		out[o++] = ' ';
		for (int k = s; k < i; k++) out[o++] = in[k];
	}
	out[o++] = '\n';
	return kapi_save_file (DISPLAY_CMDLINE, out, (unsigned) o) >= 0;
}
SK_FN int display_saved_size (int *w, int *h)
{
	static char b[1024];
	if (dp_read (DISPLAY_CMDLINE, b, sizeof b) <= 0) return 0;
	int cw = 0, ch = 0;
	for (int i = 0; b[i]; i++)
	{
		if (i > 0 && b[i - 1] != ' ' && b[i - 1] != '\n' && b[i - 1] != '\t') continue;
		int *d = !dp_ncmp (b + i, "width=", 6) ? &cw : !dp_ncmp (b + i, "height=", 7) ? &ch : 0;
		if (!d) continue;
		int k = i + (d == &cw ? 6 : 7), v = 0;
		while (b[k] >= '0' && b[k] <= '9') v = v * 10 + (b[k++] - '0');
		*d = v;
	}
	if (cw < 640 || ch < 480) return 0;
	*w = cw; *h = ch;
	return 1;
}

// ---- the games' sizes ------------------------------------------------------------------------------------------
// The value of key in [section] of an .ini's text ("" section: before any) -> 1 found (into out).
static inline int dp_ini (const char *b, const char *section, const char *key, char *out, int cap)
{
	int in = section[0] == 0, kl = dp_len (key);
	for (const char *l = b; *l; )
	{
		const char *e = l; while (*e && *e != '\n') e++;
		const char *p = l; while (p < e && (*p == ' ' || *p == '\t')) p++;
		if (p < e && *p == '[')
		{
			const char *q = p + 1; int k = 0;
			while (q < e && *q != ']' && section[k] && dp_low (*q) == dp_low (section[k])) { q++; k++; }
			in = !section[k] && q < e && *q == ']';
		}
		else if (in && p < e && *p != '#' && *p != ';')
		{
			int i = 0; while (i < kl && p + i < e && dp_low (p[i]) == dp_low (key[i])) i++;
			const char *q = p + i;
			if (i == kl) { while (q < e && (*q == ' ' || *q == '\t')) q++; }
			if (i == kl && q < e && *q == '=')
			{
				q++; while (q < e && (*q == ' ' || *q == '\t')) q++;
				const char *z = e; while (z > q && (z[-1] == ' ' || z[-1] == '\t' || z[-1] == '\r')) z--;
				int n = 0; while (q < z && n < cap - 1) out[n++] = *q++;
				out[n] = 0;
				return 1;
			}
		}
		l = *e ? e + 1 : e;
	}
	return 0;
}
SK_FN int display_game_own (const char *app, int *w, int *h)
{
	static char b[4096];
	char p[96]; int k = 0;
	const char *a = "SD:/apps/"; while (*a) p[k++] = *a++;
	for (int i = 0; app[i] && k < 80; i++) p[k++] = app[i];
	a = ".app/app.txt"; while (*a) p[k++] = *a++;
	p[k] = 0;
	char v[32];
	if (dp_read (p, b, sizeof b) < 0 || !dp_ini (b, "", "resolution", v, sizeof v)) return 0;
	return display_parse_size (v, w, h);
}
SK_FN int display_game_said (const char *app, int *w, int *h)
{
	static char b[4096];
	char v[32];
	if (dp_read (DISPLAY_CONSOLE_INI, b, sizeof b) < 0 || !dp_ini (b, "screen", app, v, sizeof v)) return DISPLAY_OWN;
	if (display_parse_size (v, w, h)) return 1;
	return DISPLAY_SYSTEM;					// ("system", or anything else: the system's)
}
SK_FN int display_game_size (const char *app, int *w, int *h)
{
	int r = display_game_said (app, w, h);
	if (r == 1) return 1;
	if (r == DISPLAY_SYSTEM) return 0;
	return display_game_own (app, w, h);
}
// console.ini written again with [screen]'s line for app replaced (val 0: removed; app 0: every line removed).
static inline int dp_screen_write (const char *app, const char *val)
{
	static char in[4096], out[4400];
	int n = dp_read (DISPLAY_CONSOLE_INI, in, sizeof in);
	if (n < 0) { n = 0; in[0] = 0; }
	int o = 0, sec = 0, seen = 0, done = 0, kl = app ? dp_len (app) : 0;
	for (const char *l = in; *l; )
	{
		const char *e = l; while (*e && *e != '\n') e++;
		const char *p = l; while (p < e && (*p == ' ' || *p == '\t')) p++;
		int drop = 0;
		if (p < e && *p == '[')
		{
			if (sec && !done && app && val) { for (const char *a = app; *a; a++) out[o++] = *a; for (const char *a = " = "; *a; a++) out[o++] = *a; for (const char *a = val; *a; a++) out[o++] = *a; out[o++] = '\n'; done = 1; }
			sec = !dp_ncmp (p, "[screen]", 8); if (sec) seen = 1;
		}
		else if (sec && p < e && *p != '#' && *p != ';')
		{
			int i = 0;
			if (app) { while (i < kl && p + i < e && dp_low (p[i]) == dp_low (app[i])) i++; }
			const char *q = p + i; while (q < e && (*q == ' ' || *q == '\t')) q++;
			int any = 0; for (const char *c = p; c < e; c++) if (*c == '=') any = 1;	// (reset: every key = value line)
			if (app ? (i == kl && q < e && *q == '=') : any)
			{
				drop = 1;
				if (app && val && !done) { for (const char *a = app; *a; a++) out[o++] = *a; for (const char *a = " = "; *a; a++) out[o++] = *a; for (const char *a = val; *a; a++) out[o++] = *a; out[o++] = '\n'; done = 1; }
			}
		}
		if (!drop && o + (e - l) + 2 < (int) sizeof out) { for (const char *c = l; c < e; c++) out[o++] = *c; out[o++] = '\n'; }
		l = *e ? e + 1 : e;
	}
	if (app && val && !done)
	{
		if (!seen) { for (const char *a = "[screen]\n"; *a; a++) out[o++] = *a; }
		for (const char *a = app; *a; a++) out[o++] = *a;
		for (const char *a = " = "; *a; a++) out[o++] = *a;
		for (const char *a = val; *a; a++) out[o++] = *a;
		out[o++] = '\n';
	}
	return kapi_save_file (DISPLAY_CONSOLE_INI, out, (unsigned) o) >= 0;
}
SK_FN int display_game_set (const char *app, int w, int h)
{
	if (!app || !app[0]) return 0;
	if (w == DISPLAY_OWN) return dp_screen_write (app, 0);
	if (w == DISPLAY_SYSTEM) return dp_screen_write (app, "system");
	char v[24]; int k = 0;
	dp_num (v, &k, w); v[k++] = 'x'; dp_num (v, &k, h); v[k] = 0;
	return dp_screen_write (app, v);
}
SK_FN int display_game_reset (void) { return dp_screen_write (0, 0); }
#undef SK_FN
