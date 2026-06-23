//
// applib.h -- tiny freestanding string helpers for the apps (no libc in EL1 apps).
// Header-only; included by the shell apps that build paths and parse config files.
//
#ifndef _applib_h
#define _applib_h

#include "kapi.h"		// kapi_open/read/close/app_dir (for the INI reader)

// Append src to dst[*pos], advancing *pos, never overflowing cap. NUL-terminates.
static inline void ax_strcat (char *dst, int cap, int *pos, const char *src)
{
	while (*src != '\0' && *pos < cap - 1)
	{
		dst[(*pos)++] = *src++;
	}
	dst[*pos] = '\0';
}

// Build "SD:apps/<name><suffix>" (e.g. suffix ".app/icon.bmp") into dst.
static inline void ax_app_path (char *dst, int cap, const char *name, const char *suffix)
{
	int p = 0;
	ax_strcat (dst, cap, &p, "SD:apps/");
	ax_strcat (dst, cap, &p, name);
	ax_strcat (dst, cap, &p, suffix);
}

static inline int ax_streq (const char *a, const char *b)
{
	while (*a != '\0' && *a == *b) { a++; b++; }
	return *a == *b;
}

static inline int ax_strlen (const char *s)
{
	int n = 0;
	while (s[n] != '\0') n++;
	return n;
}

// Console helpers for /bin tools: write a string / a line to stdout.
static inline void ax_puts (const char *s)  { kapi_stdout_write (s, (unsigned) ax_strlen (s)); }
static inline void ax_putln (const char *s) { ax_puts (s); kapi_stdout_write ("\n", 1); }

// Signed int -> string. Returns length.
static inline int ax_itoa (int v, char *b)
{
	char t[12]; int n = 0, p = 0;
	if (v < 0) { b[p++] = '-'; v = -v; }
	if (v == 0) t[n++] = '0';
	while (v) { t[n++] = (char) ('0' + v % 10); v /= 10; }
	while (n) b[p++] = t[--n];
	b[p] = '\0';
	return p;
}

// Two-digit zero-padded decimal into d[0],d[1].
static inline void ax_fmt2 (char *d, int v)
{
	if (v < 0) v = 0;
	d[0] = (char) ('0' + (v / 10) % 10);
	d[1] = (char) ('0' + v % 10);
}

// ---- .ini config reader -----------------------------------------------------
//
// A minimal "old .ini" reader: [section] headers and key=value lines, ';' or '#'
// comments, whitespace trimmed. Loaded from the app's OWN folder (kapi_app_dir).
// Shared by-source for now (header-only); each app gets its own store.
//
#define INI_MAX		64	// max key/value entries
#define INI_STRLEN	64	// max section/key/value length
#define INI_BUFSZ	2048	// max file size read

static char g_ini_sec[INI_MAX][INI_STRLEN];
static char g_ini_key[INI_MAX][INI_STRLEN];
static char g_ini_val[INI_MAX][INI_STRLEN];
static int  g_ini_n = 0;
static char g_ini_buf[INI_BUFSZ];

// Copy buf[s..e) into dst, trimming surrounding spaces/tabs, capped to INI_STRLEN.
static inline void ax_ini_range (char *dst, const char *buf, int s, int e)
{
	while (s < e && (buf[s] == ' ' || buf[s] == '\t')) s++;
	while (e > s && (buf[e - 1] == ' ' || buf[e - 1] == '\t')) e--;
	int j = 0;
	for (int i = s; i < e && j < INI_STRLEN - 1; i++) dst[j++] = buf[i];
	dst[j] = '\0';
}

// Load <appdir>/filename (e.g. "config.ini"). Returns entry count, or -1 if absent.
static inline int app_ini_load (const char *filename)
{
	g_ini_n = 0;

	char path[96];
	int pn = kapi_app_dir (path, sizeof (path));		// "SD:apps/<self>.app/"
	for (int i = 0; filename[i] != '\0' && pn < (int) sizeof (path) - 1; i++)
		path[pn++] = filename[i];
	path[pn] = '\0';

	void *f = kapi_open (path);
	if (f == 0) return -1;
	int n = kapi_read (f, g_ini_buf, sizeof (g_ini_buf) - 1);
	kapi_close (f);
	if (n < 0) n = 0;
	g_ini_buf[n] = '\0';

	char cur[INI_STRLEN]; cur[0] = '\0';			// current section
	int i = 0;
	while (g_ini_buf[i] != '\0' && g_ini_n < INI_MAX)
	{
		while (g_ini_buf[i] == ' ' || g_ini_buf[i] == '\t') i++;
		int ls = i;
		while (g_ini_buf[i] != '\0' && g_ini_buf[i] != '\n' && g_ini_buf[i] != '\r') i++;
		int le = i;
		while (g_ini_buf[i] == '\n' || g_ini_buf[i] == '\r') i++;
		while (le > ls && (g_ini_buf[le - 1] == ' ' || g_ini_buf[le - 1] == '\t')) le--;
		if (le <= ls) continue;				// blank
		char c0 = g_ini_buf[ls];
		if (c0 == ';' || c0 == '#') continue;		// comment
		if (c0 == '[')					// [section]
		{
			int e = le;
			if (e > ls + 1 && g_ini_buf[e - 1] == ']') e--;
			ax_ini_range (cur, g_ini_buf, ls + 1, e);
			continue;
		}
		int eq = ls;					// key=value
		while (eq < le && g_ini_buf[eq] != '=') eq++;
		if (eq >= le) continue;				// no '='
		int k = g_ini_n;
		int j = 0;					// section: cur is already clean
		while (cur[j] != '\0' && j < INI_STRLEN - 1) { g_ini_sec[k][j] = cur[j]; j++; }
		g_ini_sec[k][j] = '\0';
		ax_ini_range (g_ini_key[k], g_ini_buf, ls, eq);
		ax_ini_range (g_ini_val[k], g_ini_buf, eq + 1, le);
		g_ini_n++;
	}
	return g_ini_n;
}

// Look up a value. section may be 0/"" for keys before any [section]. Returns def
// if not found.
static inline const char *app_ini_get (const char *section, const char *key, const char *def)
{
	const char *sec = section ? section : "";
	for (int i = 0; i < g_ini_n; i++)
		if (ax_streq (g_ini_sec[i], sec) && ax_streq (g_ini_key[i], key))
			return g_ini_val[i];
	return def;
}

// Same, parsed as a signed int32.
static inline int app_ini_get_int (const char *section, const char *key, int def)
{
	const char *v = app_ini_get (section, key, 0);
	if (v == 0) return def;
	int i = 0, neg = 0, any = 0; long r = 0;
	if (v[0] == '-') { neg = 1; i = 1; } else if (v[0] == '+') i = 1;
	while (v[i] >= '0' && v[i] <= '9') { r = r * 10 + (v[i] - '0'); i++; any = 1; }
	if (!any) return def;
	return (int) (neg ? -r : r);
}

#endif
