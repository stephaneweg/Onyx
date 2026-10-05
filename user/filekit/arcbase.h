//
// filekit/arcbase.h -- the archives' small common things, with no engine behind them: strings, a size
// for people, DOS dates, code page 437, an archive's names made clean. Included by the engine (arc.h,
// compiled into SD:/lib/filekit.so) and by the programs that only show archives (the Archiver, over
// FileKit's C interface: Apps/archiver/arcfk.h).
//
#ifndef _filekit_arcbase_h
#define _filekit_arcbase_h

#include <stdlib.h>
#include <string.h>
#include "kapi.h"

namespace arc {

typedef unsigned long long u64;
typedef unsigned u32;
typedef unsigned short u16;
typedef unsigned char u8;

// ---- strings ---------------------------------------------------------------------------------------
static inline char *sdup (const char *s, int n = -1)
{
	if (n < 0) n = (int) strlen (s);
	char *d = (char *) malloc ((size_t) n + 1);
	if (d) { memcpy (d, s, (size_t) n); d[n] = 0; }
	return d;
}
static inline void scopy (char *d, const char *s, int cap)
{
	int i = 0; for (; s && s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = 0;
}
static inline void scat (char *d, const char *s, int cap)
{
	int n = (int) strlen (d); scopy (d + n, s, cap - n);
}
static inline char lower (char c) { return c >= 'A' && c <= 'Z' ? (char) (c + 32) : c; }
static inline int ci_cmp (const char *a, const char *b)
{
	for (;; a++, b++)
	{
		char x = lower (*a), y = lower (*b);
		if (x != y) return (unsigned char) x < (unsigned char) y ? -1 : 1;
		if (!x) return 0;
	}
}
// The extension of a name (after its last '.', lower case, without it) into out.
static inline void ext_of (const char *name, char *out, int cap)
{
	const char *slash = strrchr (name, '/'), *dot = strrchr (name, '.');
	out[0] = 0;
	if (!dot || (slash && dot < slash)) return;
	int i = 0; for (dot++; *dot && i < cap - 1; dot++) out[i++] = lower (*dot); out[i] = 0;
}
static inline const char *base_of (const char *path)
{
	const char *s = strrchr (path, '/');
	if (!s) s = strrchr (path, ':');
	return s ? s + 1 : path;
}

// Human sizes: "512 B", "12.4 KB", "3.1 MB", "1.2 GB" (one decimal under 100).
static inline void human_size (u64 n, char *out, int cap)
{
	static const char *u[] = { "B", "KB", "MB", "GB", "TB" };
	int k = 0; u64 whole = n, tenth = 0;
	while (whole >= 1024 && k < 4) { tenth = (whole % 1024) * 10 / 1024; whole /= 1024; k++; }
	char b[32]; int i = 0; char t[24]; int j = 0; u64 v = whole;
	do { t[j++] = (char) ('0' + v % 10); v /= 10; } while (v);
	while (j) b[i++] = t[--j];
	if (k && whole < 100) { b[i++] = '.'; b[i++] = (char) ('0' + tenth); }
	b[i++] = ' '; for (const char *s = u[k]; *s; s++) b[i++] = *s; b[i] = 0;
	scopy (out, b, cap);
}
static inline void u64_str (u64 v, char *out, int cap)
{
	char t[24]; int j = 0; do { t[j++] = (char) ('0' + v % 10); v /= 10; } while (v);
	int i = 0; while (j && i < cap - 1) out[i++] = t[--j]; out[i] = 0;
}

// ---- DOS dates (what ZIP keeps: 2-second steps from 1980) --------------------------------------------
static inline u32 dos_now ()
{
	int y = 2026, mo = 1, d = 1, h = 0, mi = 0, s = 0;
	kapi_get_datetime (&y, &mo, &d, &h, &mi, &s);
	if (y < 1980) y = 1980;
	return (u32) (((y - 1980) << 25) | (mo << 21) | (d << 16) | (h << 11) | (mi << 5) | (s / 2));
}
// "28/09/2026 14:12"
static inline void dos_str (u32 t, char *out, int cap)
{
	if (!t) { scopy (out, "-", cap); return; }
	int y = (int) (t >> 25) + 1980, mo = (int) (t >> 21) & 15, d = (int) (t >> 16) & 31;
	int h = (int) (t >> 11) & 31, mi = (int) (t >> 5) & 63;
	char b[20] = "00/00/0000 00:00";
	b[0] += (char) (d / 10); b[1] += (char) (d % 10); b[3] += (char) (mo / 10); b[4] += (char) (mo % 10);
	b[6] += (char) (y / 1000); b[7] += (char) (y / 100 % 10); b[8] += (char) (y / 10 % 10); b[9] += (char) (y % 10);
	b[11] += (char) (h / 10); b[12] += (char) (h % 10); b[14] += (char) (mi / 10); b[15] += (char) (mi % 10);
	scopy (out, b, cap);
}

// ---- code page 437 (old ZIP names) -> UTF-8 ------------------------------------------------------------
static const unsigned short CP437[128] = {
	0xC7,0xFC,0xE9,0xE2,0xE4,0xE0,0xE5,0xE7,0xEA,0xEB,0xE8,0xEF,0xEE,0xEC,0xC4,0xC5,
	0xC9,0xE6,0xC6,0xF4,0xF6,0xF2,0xFB,0xF9,0xFF,0xD6,0xDC,0xA2,0xA3,0xA5,0x20A7,0x192,
	0xE1,0xED,0xF3,0xFA,0xF1,0xD1,0xAA,0xBA,0xBF,0x2310,0xAC,0xBD,0xBC,0xA1,0xAB,0xBB,
	0x2591,0x2592,0x2593,0x2502,0x2524,0x2561,0x2562,0x2556,0x2555,0x2563,0x2551,0x2557,0x255D,0x255C,0x255B,0x2510,
	0x2514,0x2534,0x252C,0x251C,0x2500,0x253C,0x255E,0x255F,0x255A,0x2554,0x2569,0x2566,0x2560,0x2550,0x256C,0x2567,
	0x2568,0x2564,0x2565,0x2559,0x2558,0x2552,0x2553,0x256B,0x256A,0x2518,0x250C,0x2588,0x2584,0x258C,0x2590,0x2580,
	0x3B1,0xDF,0x393,0x3C0,0x3A3,0x3C3,0xB5,0x3C4,0x3A6,0x398,0x3A9,0x3B4,0x221E,0x3C6,0x3B5,0x2229,
	0x2261,0xB1,0x2265,0x2264,0x2320,0x2321,0xF7,0x2248,0xB0,0x2219,0xB7,0x221A,0x207F,0xB2,0x25A0,0xA0 };
static inline int u8_put (char *o, unsigned cp)
{
	if (cp < 0x80) { o[0] = (char) cp; return 1; }
	if (cp < 0x800) { o[0] = (char) (0xC0 | cp >> 6); o[1] = (char) (0x80 | (cp & 0x3F)); return 2; }
	o[0] = (char) (0xE0 | cp >> 12); o[1] = (char) (0x80 | ((cp >> 6) & 0x3F)); o[2] = (char) (0x80 | (cp & 0x3F));
	return 3;
}
// A name of n bytes in CP437 (utf8 = false) or UTF-8, made a clean archive path: '\' -> '/', no
// leading '/', no "./", no trailing '/' (the caller knows it was a folder). malloc'ed.
static inline char *clean_name (const char *s, int n, bool utf8)
{
	char *o = (char *) malloc ((size_t) n * 3 + 1);
	if (!o) return 0;
	int k = 0;
	for (int i = 0; i < n; i++)
	{
		unsigned char c = (unsigned char) s[i];
		if (c == '\\') c = '/';
		if (c == 0) break;
		if (c >= 0x80 && !utf8) k += u8_put (o + k, CP437[c - 0x80]);
		else o[k++] = (char) c;
	}
	o[k] = 0;
	// drop "/", "./" at the start, doubled slashes, a trailing slash
	char *r = o, *w = o;
	while (*r == '/' || (r[0] == '.' && r[1] == '/')) r += (*r == '/') ? 1 : 2;
	for (; *r; r++) { if (*r == '/' && (w == o || w[-1] == '/')) continue; *w++ = *r; }
	while (w > o && w[-1] == '/') w--;
	*w = 0;
	return o;
}
static inline bool is_ascii (const char *s) { for (; *s; s++) if ((unsigned char) *s >= 0x80) return false; return true; }
// n bytes that are valid UTF-8 (with at least one character past ASCII: a CP437 name rarely is)
static inline bool valid_utf8 (const char *s, int n)
{
	bool multi = false;
	for (int i = 0; i < n;)
	{
		unsigned char c = (unsigned char) s[i];
		int k = c < 0x80 ? 1 : c >= 0xC2 && c < 0xE0 ? 2 : c >= 0xE0 && c < 0xF0 ? 3 : c >= 0xF0 && c < 0xF5 ? 4 : 0;
		if (!k || i + k > n) return false;
		for (int j = 1; j < k; j++) if (((unsigned char) s[i + j] & 0xC0) != 0x80) return false;
		if (k > 1) multi = true;
		i += k;
	}
	return multi;
}

} // namespace arc

#endif
