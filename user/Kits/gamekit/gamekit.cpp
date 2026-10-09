//
// gamekit.cpp -- GameKit (SD:/lib/gamekit.so; gamekit/gamekit.h): the consoles of the installed emulators, the watched
// folders, the ROMs found in them, their pictures. Through AppKit only (the files, the folders). Built freestanding for
// Onyx (integer only, no libc); compiled as it is on a PC (the tests, against the stand-in kernel).
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
#include "gamekit/gamekit.h"
#if defined (__aarch64__)
#include "lib.h"
#endif

// ---- small pieces ------------------------------------------------------------------------------------------------
static int g_len (const char *s) { int n = 0; while (s[n]) n++; return n; }
static char g_low (char c) { return c >= 'A' && c <= 'Z' ? (char) (c + 32) : c; }
static void g_cpy (char *d, const char *s, int cap) { int i = 0; for (; s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = 0; }
static void g_cat (char *d, int cap, int *n, const char *s) { while (*s && *n < cap - 1) d[(*n)++] = *s++; d[*n] = 0; }
static int g_eq (const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static int g_less (const char *a, const char *b)		// a before b, ignoring the case
{
	for (;; a++, b++) { char x = g_low (*a), y = g_low (*b); if (x != y || !x) return x < y; }
}
static int g_has (const char *s, const char *w)			// w in s
{
	int n = g_len (w);
	for (; *s; s++) { int i = 0; while (i < n && s[i] == w[i]) i++; if (i == n) return 1; }
	return 0;
}
static int g_atoi (const char *s) { int v = 0, neg = 0; while (*s == ' ') s++; if (*s == '-') { neg = 1; s++; } while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0'); return neg ? -v : v; }

// A file read whole into b (cap - 1 bytes at most, 0-ended) -> its length, -1 not opened.
static int g_read (const char *path, char *b, int cap)
{
	void *f = kapi_open (path);
	if (!f) return -1;
	int n = 0;
	for (;;)
	{
		int r = kapi_read (f, b + n, (unsigned) (cap - 1 - n));
		if (r <= 0) break;
		n += r;
		if (n >= cap - 1) break;
	}
	kapi_close (f);
	b[n] = 0;
	return n;
}
// The value of key in an .ini's text (before any [section]; "key = value", ';' / '#' lines skipped) -> 1 found.
static int g_ini (const char *b, const char *key, char *out, int cap)
{
	int kl = g_len (key);
	for (const char *l = b; *l; )
	{
		const char *e = l; while (*e && *e != '\n') e++;
		const char *p = l; while (p < e && (*p == ' ' || *p == '\t')) p++;
		if (p < e && *p == '[') return 0;
		int i = 0; while (i < kl && p + i < e && g_low (p[i]) == g_low (key[i])) i++;
		if (i == kl)
		{
			const char *q = p + kl; while (q < e && (*q == ' ' || *q == '\t')) q++;
			if (q < e && *q == '=')
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

// ---- the consoles ----------------------------------------------------------------------------------------------------
extern "C" int games_systems (struct game_system *out, int max)
{
	int ns = 0;
	void *d = kapi_opendir ("SD:/apps");
	if (!d) return 0;
	struct kapi_dirent e;
	static char txt[4096];
	while (kapi_readdir (d, &e) && ns < max)
	{
		int nl = g_len (e.name);
		if (!e.is_dir || nl < 5 || e.name[nl - 4] != '.') continue;
		char emu[24]; int k = 0; for (; k < nl - 4 && k < 23; k++) emu[k] = e.name[k]; emu[k] = 0;
		char p[96]; int n = 0; g_cat (p, sizeof p, &n, "SD:/apps/"); g_cat (p, sizeof p, &n, e.name); g_cat (p, sizeof p, &n, "/app.txt");
		if (g_read (p, txt, sizeof txt) < 0) continue;
		static char gbuf[256]; char ord[16];
		const char *games = g_ini (txt, "games", gbuf, sizeof gbuf) ? gbuf : 0;
		if (!g_ini (txt, "order", ord, sizeof ord)) g_cpy (ord, "100", sizeof ord);
		// (an emulator of before the `games` key: its consoles as they were)
		static const char *const older[][3] = { { "gcemu", "GameCube: iso gcm", "10" }, { "n64emu", "Nintendo 64: z64 n64 v64", "20" },
			{ "snesemu", "Super Nintendo: sfc smc", "30" }, { "gbaemu", "Game Boy Advance: gba", "40" },
			{ "gbemu", "Game Boy Color: gbc; Game Boy: gb", "50" }, { "nesemu", "NES: nes", "60" } };
		for (unsigned o = 0; !games && o < sizeof older / sizeof older[0]; o++) if (g_eq (emu, older[o][0])) { games = older[o][1]; g_cpy (ord, older[o][2], sizeof ord); }
		if (!games) continue;
		int order = g_atoi (ord), idx = 0;
		for (const char *g = games; *g && ns < max; idx++)	// "Name: ext ext; Name: ext"
		{
			while (*g == ' ' || *g == ';') g++;
			if (!*g) break;
			struct game_system &y = out[ns];
			int m = 0; while (*g && *g != ':' && *g != ';' && m < 39) y.name[m++] = *g++;
			while (m > 0 && y.name[m - 1] == ' ') m--;
			y.name[m] = 0;
			if (*g != ':') { while (*g && *g != ';') g++; continue; }
			g++;
			int x = 0, t = 0; y.ext[x++] = ' ';
			while (*g && *g != ';')
			{
				while (*g == ' ' || *g == ',' || *g == '.') g++;
				if (!*g || *g == ';') break;
				if (t && t < 60) { y.ext_text[t++] = ' '; y.ext_text[t++] = '/'; y.ext_text[t++] = ' '; }
				if (t < 62) y.ext_text[t++] = '.';
				while (*g && *g != ' ' && *g != ',' && *g != ';') { char c = g_low (*g++); if (x < 62) y.ext[x++] = c; if (t < 62) y.ext_text[t++] = c; }
				if (x < 63) y.ext[x++] = ' ';
			}
			y.ext[x] = 0; y.ext_text[t] = 0;
			g_cpy (y.emu, emu, sizeof y.emu);
			y.order = order * 16 + idx;
			if (y.name[0] && x > 1) ns++;
		}
	}
	kapi_closedir (d);
	for (int i = 1; i < ns; i++)
		for (int j = i; j > 0 && out[j - 1].order > out[j].order; j--) { struct game_system t = out[j]; out[j] = out[j - 1]; out[j - 1] = t; }
	return ns;
}

extern "C" int games_system_of (const char *file, const struct game_system *sys, int nsys)
{
	int n = g_len (file);
	int d = n; while (d > 0 && file[d - 1] != '.' && file[d - 1] != '/') d--;
	if (d <= 0 || file[d - 1] != '.' || n - d > 8) return -1;
	char x[12]; int k = 0; x[k++] = ' '; for (int i = d; i < n; i++) x[k++] = g_low (file[i]); x[k++] = ' '; x[k] = 0;
	for (int i = 0; i < nsys; i++) if (g_has (sys[i].ext, x)) return i;
	return -1;
}

// ---- the folders -------------------------------------------------------------------------------------------------------
extern "C" int games_folders (char (*out)[GAMES_PATH], int max)
{
	static char b[GAMES_FOLDERS_MAX * 220 + 256];
	int nf = 0, any = 0;
	if (g_read (GAMES_CONFIG, b, sizeof b) >= 0)
		for (const char *l = b; *l && nf < max; )		// one `folder = <path>` line each
		{
			const char *e = l; while (*e && *e != '\n') e++;
			char line[GAMES_PATH + 16]; int n = 0; for (const char *p = l; p < e && n < (int) sizeof line - 1; ) line[n++] = *p++;
			line[n] = 0;
			char v[GAMES_PATH];
			if (g_ini (line, "folder", v, sizeof v)) { any = 1; if (v[0]) g_cpy (out[nf++], v, GAMES_PATH); }
			else if (line[0] == '[') break;
			l = *e ? e + 1 : e;
		}
	if (!any && max > 0) { g_cpy (out[0], "SD:/roms", GAMES_PATH); nf = 1; }
	return nf;
}
extern "C" int games_folders_save (const char (*folders)[GAMES_PATH], int n)
{
	static char ini[GAMES_FOLDERS_MAX * (GAMES_PATH + 12) + 80];
	int k = 0;
	g_cat (ini, sizeof ini, &k, "; Game Library settings: the watched folders, one line each\n");
	for (int f = 0; f < n && f < GAMES_FOLDERS_MAX; f++) { g_cat (ini, sizeof ini, &k, "folder = "); g_cat (ini, sizeof ini, &k, folders[f]); g_cat (ini, sizeof ini, &k, "\n"); }
	if (n == 0) g_cat (ini, sizeof ini, &k, "folder =\n");			// (none: not the default)
	return kapi_save_file (GAMES_CONFIG, ini, (unsigned) k) >= 0;
}

// ---- the ROMs ----------------------------------------------------------------------------------------------------------
extern "C" void games_nice_name (const char *file, char *out, int cap)
{
	int n = 0; const char *p = file;
	for (const char *q = file; *q; q++) if (*q == '/' || *q == ':') p = q + 1;
	int e = g_len (p); while (e > 0 && p[e - 1] != '.') e--;
	if (e > 0) e--; else e = g_len (p);
	for (int i = 0; i < e && n < cap - 2; i++)
	{
		char c = p[i];
		if (c == '_' || c == '-') c = ' ';
		if (i > 0 && c >= 'A' && c <= 'Z' && p[i - 1] >= 'a' && p[i - 1] <= 'z') out[n++] = ' ';
		if (n == 0 && c >= 'a' && c <= 'z') c = (char) (c - 32);
		out[n++] = c;
	}
	out[n] = 0;
}
static void g_scan (const char *dir, int depth, const struct game_system *sys, int nsys, struct game *out, int max, int *ng)
{
	if (depth > 6) return;
	void *d = kapi_opendir (dir);
	if (!d) return;
	struct kapi_dirent e;
	while (kapi_readdir (d, &e) && *ng < max)
	{
		if (e.name[0] == '.') continue;
		char p[GAMES_PATH]; int n = 0; g_cat (p, sizeof p, &n, dir); if (n && p[n - 1] != '/') g_cat (p, sizeof p, &n, "/"); g_cat (p, sizeof p, &n, e.name);
		if (e.is_dir) { g_scan (p, depth + 1, sys, nsys, out, max, ng); continue; }
		int s = games_system_of (e.name, sys, nsys);
		if (s < 0) continue;
		struct game &g = out[(*ng)++];
		g_cpy (g.path, p, sizeof g.path);
		games_nice_name (e.name, g.name, sizeof g.name);
		g_cpy (g.key, e.name, sizeof g.key);
		g.sys = s;
	}
	kapi_closedir (d);
}
extern "C" int games_scan (const char (*folders)[GAMES_PATH], int nfolders, const struct game_system *sys, int nsys, struct game *out, int max)
{
	int ng = 0;
	for (int f = 0; f < nfolders; f++) g_scan (folders[f], 0, sys, nsys, out, max, &ng);
	for (int i = 1; i < ng; i++)					// by console, then by name
	{
		struct game t = out[i]; int j = i;
		while (j > 0 && (out[j - 1].sys > t.sys || (out[j - 1].sys == t.sys && g_less (t.name, out[j - 1].name)))) { out[j] = out[j - 1]; j--; }
		out[j] = t;
	}
	return ng;
}

// ---- the pictures ------------------------------------------------------------------------------------------------------
extern "C" void games_thumb_path (const struct game *g, char *out, int cap)
{
	int n = 0; out[0] = 0;
	g_cat (out, cap, &n, GAMES_THUMBS); g_cat (out, cap, &n, g->key); g_cat (out, cap, &n, ".thm");
}
extern "C" int games_thumb_load (const struct game *g, unsigned *px)
{
	char p[GAMES_PATH + 64]; games_thumb_path (g, p, sizeof p);
	void *f = kapi_open (p); if (!f) return 0;
	static unsigned char b[GAMES_THUMB_W * GAMES_THUMB_H * 3];
	int r = kapi_read (f, b, sizeof b); kapi_close (f);
	if (r != (int) sizeof b) return 0;
	for (int i = 0; i < GAMES_THUMB_W * GAMES_THUMB_H; i++) px[i] = ((unsigned) b[i * 3] << 16) | ((unsigned) b[i * 3 + 1] << 8) | b[i * 3 + 2];
	return 1;
}
extern "C" int games_thumb_save (const struct game *g, const unsigned *px)
{
	kapi_mkdir ("SD:/apps/gamelib.app/thumbs");
	static unsigned char b[GAMES_THUMB_W * GAMES_THUMB_H * 3];
	for (int i = 0; i < GAMES_THUMB_W * GAMES_THUMB_H; i++) { b[i * 3] = (unsigned char) (px[i] >> 16); b[i * 3 + 1] = (unsigned char) (px[i] >> 8); b[i * 3 + 2] = (unsigned char) px[i]; }
	char p[GAMES_PATH + 64]; games_thumb_path (g, p, sizeof p);
	return kapi_save_file (p, (const char *) b, sizeof b) >= 0;
}

#if defined (__aarch64__) && defined (ONYX_LIB_BUILD)
// (the library's table: its init -- the library runtime's, Runtime/librt.cpp)
extern "C" int onyx_lib_init (const TLibImports *imp);
extern "C" int gm_lib_init (const TLibImports *imp)
{
	return onyx_lib_init (imp) < 0 ? -1 : 0;
}
#endif
