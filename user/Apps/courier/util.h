//
// util.h -- Courier's small toolbox: a heap string (Str), a growable array (Vec), text helpers,
// ids, dates. A newlib app (malloc, snprintf), no exceptions, no STL.
//
#ifndef _courier_util_h
#define _courier_util_h

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "kapi.h"
#include "onyxpp.hpp"		// placement new

namespace cr {

static inline int imin (int a, int b) { return a < b ? a : b; }
static inline int imax (int a, int b) { return a > b ? a : b; }
static inline int iclamp (int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

// ---- Str: a NUL-terminated heap string ----------------------------------------------------------------
class Str
{
	char *m_p; int m_n, m_cap;
	void grow (int need)
	{
		if (need + 1 <= m_cap) return;
		int c = m_cap ? m_cap : 16;
		while (c < need + 1) c *= 2;
		char *p = (char *) malloc (c);
		if (!p) return;
		if (m_p) { memcpy (p, m_p, m_n + 1); free (m_p); } else p[0] = 0;
		m_p = p; m_cap = c;
	}
public:
	Str () : m_p (0), m_n (0), m_cap (0) {}
	Str (const char *s) : m_p (0), m_n (0), m_cap (0) { set (s); }
	Str (const Str &o) : m_p (0), m_n (0), m_cap (0) { set (o.c (), o.m_n); }
	~Str () { free (m_p); }
	Str &operator= (const Str &o) { if (this != &o) set (o.c (), o.m_n); return *this; }
	Str &operator= (const char *s) { set (s); return *this; }
	const char *c () const { return m_p ? m_p : ""; }
	char *data () { grow (m_n); return m_p; }
	int len () const { return m_n; }
	bool empty () const { return m_n == 0; }
	void clear () { m_n = 0; if (m_p) m_p[0] = 0; }
	void set (const char *s, int n = -1)
	{
		if (!s) s = "";
		if (n < 0) n = (int) strlen (s);
		if (m_p && s >= m_p && s < m_p + m_cap) { Str t; t.set (s, n); set (t.c (), t.len ()); return; }
		grow (n); if (!m_p) return;
		memcpy (m_p, s, n); m_p[n] = 0; m_n = n;
	}
	void add (const char *s, int n = -1)
	{
		if (!s) return;
		if (n < 0) n = (int) strlen (s);
		if (m_p && s >= m_p && s < m_p + m_cap) { Str t; t.set (s, n); add (t.c (), t.len ()); return; }
		grow (m_n + n); if (!m_p) return;
		memcpy (m_p + m_n, s, n); m_n += n; m_p[m_n] = 0;
	}
	void add (char ch) { grow (m_n + 1); if (!m_p) return; m_p[m_n++] = ch; m_p[m_n] = 0; }
	void addf (const char *fmt, ...) __attribute__ ((format (printf, 2, 3)));
	void addInt (long long v) { char b[24]; snprintf (b, sizeof b, "%lld", v); add (b); }
	void insert (int at, const char *s, int n = -1)
	{
		if (n < 0) n = (int) strlen (s);
		at = iclamp (at, 0, m_n);
		Str t; t.set (s, n);
		grow (m_n + n); if (!m_p) return;
		memmove (m_p + at + n, m_p + at, m_n - at + 1);
		memcpy (m_p + at, t.c (), n); m_n += n;
	}
	void erase (int at, int n)
	{
		at = iclamp (at, 0, m_n); n = iclamp (n, 0, m_n - at);
		if (!n) return;
		memmove (m_p + at, m_p + at + n, m_n - at - n + 1); m_n -= n;
	}
	void truncate (int n) { if (n < m_n && m_p) { m_n = imax (0, n); m_p[m_n] = 0; } }
	bool eq (const char *s) const { return strcmp (c (), s ? s : "") == 0; }
	// hand the buffer over (the caller frees it)
	char *release () { char *p = m_p; m_p = 0; m_n = m_cap = 0; return p; }
};

inline void Str::addf (const char *fmt, ...)
{
	char b[512];
	__builtin_va_list ap; __builtin_va_start (ap, fmt);
	int n = vsnprintf (b, sizeof b, fmt, ap);
	__builtin_va_end (ap);
	if (n < (int) sizeof b) { add (b, n < 0 ? 0 : n); return; }
	char *p = (char *) malloc (n + 1);
	if (!p) return;
	__builtin_va_start (ap, fmt);
	vsnprintf (p, n + 1, fmt, ap);
	__builtin_va_end (ap);
	add (p, n); free (p);
}

// ---- Vec<T>: a growable array of objects (copied with their own copy constructor) ---------------------
template <class T> class Vec
{
	T *m_p; int m_n, m_cap;
	void grow (int need)
	{
		if (need <= m_cap) return;
		int c = m_cap ? m_cap * 2 : 8;
		while (c < need) c *= 2;
		T *p = (T *) malloc (sizeof (T) * c);
		if (!p) return;
		for (int i = 0; i < m_n; i++) { new (&p[i]) T (m_p[i]); m_p[i].~T (); }
		free (m_p); m_p = p; m_cap = c;
	}
public:
	Vec () : m_p (0), m_n (0), m_cap (0) {}
	Vec (const Vec &o) : m_p (0), m_n (0), m_cap (0) { *this = o; }
	~Vec () { clear (); free (m_p); }
	Vec &operator= (const Vec &o)
	{
		if (this == &o) return *this;
		clear (); grow (o.m_n);
		for (int i = 0; i < o.m_n; i++) new (&m_p[i]) T (o.m_p[i]);
		m_n = o.m_n;
		return *this;
	}
	int size () const { return m_n; }
	T &operator[] (int i) { return m_p[i]; }
	const T &operator[] (int i) const { return m_p[i]; }
	T &last () { return m_p[m_n - 1]; }
	T &push (const T &v) { grow (m_n + 1); new (&m_p[m_n]) T (v); return m_p[m_n++]; }
	T &push () { grow (m_n + 1); new (&m_p[m_n]) T (); return m_p[m_n++]; }
	T &insert (int at, const T &v)
	{
		at = iclamp (at, 0, m_n);
		push (v);
		for (int i = m_n - 1; i > at; i--) { T t (m_p[i]); m_p[i] = m_p[i - 1]; m_p[i - 1] = t; }
		return m_p[at];
	}
	void remove (int at)
	{
		if (at < 0 || at >= m_n) return;
		for (int i = at; i < m_n - 1; i++) m_p[i] = m_p[i + 1];
		m_p[--m_n].~T ();
	}
	void move (int from, int to)
	{
		if (from < 0 || from >= m_n || to < 0 || to >= m_n || from == to) return;
		T t (m_p[from]);
		if (from < to) for (int i = from; i < to; i++) m_p[i] = m_p[i + 1];
		else for (int i = from; i > to; i--) m_p[i] = m_p[i - 1];
		m_p[to] = t;
	}
	void clear () { for (int i = 0; i < m_n; i++) m_p[i].~T (); m_n = 0; }
};

// ---- text ---------------------------------------------------------------------------------------------
static inline bool s_eq (const char *a, const char *b) { return strcmp (a ? a : "", b ? b : "") == 0; }
static inline char lc (char c) { return (c >= 'A' && c <= 'Z') ? (char) (c + 32) : c; }
static inline bool s_eqi (const char *a, const char *b)
{
	if (!a) a = "";
	if (!b) b = "";
	while (*a && lc (*a) == lc (*b)) { a++; b++; }
	return lc (*a) == lc (*b);
}
static inline bool s_eqin (const char *a, const char *b, int n)	// the first n bytes, no case
{
	for (int i = 0; i < n; i++) { if (lc (a[i]) != lc (b[i])) return false; if (!a[i]) return true; }
	return true;
}
static inline bool s_starts (const char *s, const char *p) { return strncmp (s, p, strlen (p)) == 0; }
static inline bool s_startsi (const char *s, const char *p) { return s_eqin (s, p, (int) strlen (p)); }
// the first place of w in s[0..n), no case; -1
static inline int s_findi (const char *s, int n, const char *w)
{
	int wl = (int) strlen (w);
	if (wl == 0) return 0;
	for (int i = 0; i + wl <= n; i++) if (s_eqin (s + i, w, wl)) return i;
	return -1;
}
static inline void s_copy (char *d, const char *s, int cap)
{
	if (cap <= 0) return;
	int i = 0; for (; s && s[i] && i < cap - 1; i++) d[i] = s[i];
	d[i] = 0;
}
static inline bool is_space (char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
// s trimmed of spaces at both ends, into out
static inline void s_trim (Str &out, const char *s, int n = -1)
{
	if (n < 0) n = (int) strlen (s);
	int a = 0, b = n;
	while (a < b && is_space (s[a])) a++;
	while (b > a && is_space (s[b - 1])) b--;
	out.set (s + a, b - a);
}
static inline int hexval (char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

// ---- URL encoding (RFC 3986's unreserved set kept) ------------------------------------------------------
static inline void url_encode (Str &out, const char *s, bool spacePlus = false)
{
	static const char H[] = "0123456789ABCDEF";
	for (const unsigned char *p = (const unsigned char *) s; *p; p++)
	{
		unsigned char c = *p;
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~')
			out.add ((char) c);
		else if (c == ' ' && spacePlus) out.add ('+');
		else { out.add ('%'); out.add (H[c >> 4]); out.add (H[c & 15]); }
	}
}
// a URL's query part: only what would break it is encoded (spaces, '#', non-ASCII), as Postman
static inline void url_encode_soft (Str &out, const char *s)
{
	static const char H[] = "0123456789ABCDEF";
	for (const unsigned char *p = (const unsigned char *) s; *p; p++)
	{
		unsigned char c = *p;
		if (c <= 0x20 || c >= 0x7F || c == '"' || c == '#' || c == '<' || c == '>')
		{ out.add ('%'); out.add (H[c >> 4]); out.add (H[c & 15]); }
		else out.add ((char) c);
	}
}
static inline void url_decode (Str &out, const char *s, int n = -1, bool plusSpace = true)
{
	if (n < 0) n = (int) strlen (s);
	for (int i = 0; i < n; i++)
	{
		if (s[i] == '%' && i + 2 < n + 0 && hexval (s[i + 1]) >= 0 && hexval (s[i + 2]) >= 0)
		{ out.add ((char) (hexval (s[i + 1]) * 16 + hexval (s[i + 2]))); i += 2; }
		else if (s[i] == '+' && plusSpace) out.add (' ');
		else out.add (s[i]);
	}
}

// ---- base64 ----------------------------------------------------------------------------------------------
static inline void base64 (Str &out, const unsigned char *d, int n)
{
	static const char B[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	for (int i = 0; i < n; i += 3)
	{
		unsigned v = (unsigned) d[i] << 16 | (i + 1 < n ? (unsigned) d[i + 1] << 8 : 0) | (i + 2 < n ? d[i + 2] : 0);
		out.add (B[(v >> 18) & 63]); out.add (B[(v >> 12) & 63]);
		out.add (i + 1 < n ? B[(v >> 6) & 63] : '=');
		out.add (i + 2 < n ? B[v & 63] : '=');
	}
}

// ---- ids, time -------------------------------------------------------------------------------------------
static inline unsigned rnd32 ()
{
	unsigned v = 0;
	kapi_random (&v, sizeof v);
	static unsigned x = 0x9E3779B9u;
	x ^= x << 13; x ^= x >> 17; x ^= x << 5;
	return v ^ x ^ kapi_get_ticks ();
}
// a version-4 UUID (Postman's ids)
static inline void uuid (Str &out)
{
	unsigned char b[16];
	for (int i = 0; i < 16; i += 4) { unsigned r = rnd32 (); memcpy (b + i, &r, 4); }
	b[6] = (unsigned char) ((b[6] & 0x0F) | 0x40);
	b[8] = (unsigned char) ((b[8] & 0x3F) | 0x80);
	static const char H[] = "0123456789abcdef";
	out.clear ();
	for (int i = 0; i < 16; i++)
	{
		if (i == 4 || i == 6 || i == 8 || i == 10) out.add ('-');
		out.add (H[b[i] >> 4]); out.add (H[b[i] & 15]);
	}
}
// days since 1970-01-01 of a civil date (Howard Hinnant's algorithm)
static inline long days_from_civil (int y, int m, int d)
{
	y -= m <= 2;
	long era = (y >= 0 ? y : y - 399) / 400;
	unsigned yoe = (unsigned) (y - era * 400);
	unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + (long) doe - 719468;
}
static inline long long now_unix ()
{
	int y = 1970, mo = 1, d = 1, h = 0, mi = 0, s = 0;
	kapi_get_datetime (&y, &mo, &d, &h, &mi, &s);
	return (long long) days_from_civil (y, mo, d) * 86400 + h * 3600 + mi * 60 + s;
}
static inline void civil_from_days (long z, int *y, int *m, int *d)
{
	z += 719468;
	long era = (z >= 0 ? z : z - 146096) / 146097;
	unsigned doe = (unsigned) (z - era * 146097);
	unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	int yy = (int) yoe + (int) era * 400;
	unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	unsigned mp = (5 * doy + 2) / 153;
	*d = (int) (doy - (153 * mp + 2) / 5 + 1);
	*m = (int) (mp < 10 ? mp + 3 : mp - 9);
	*y = yy + (*m <= 2);
}
// "2026-09-30T14:03:22.000Z"
static inline void iso_time (Str &out, long long t)
{
	int y, m, d; civil_from_days ((long) (t / 86400), &y, &m, &d);
	int s = (int) (t % 86400);
	out.addf ("%04d-%02d-%02dT%02d:%02d:%02d.000Z", y, m, d, s / 3600, s / 60 % 60, s % 60);
}

// ---- files -----------------------------------------------------------------------------------------------
// the whole file (malloc'd, NUL-terminated) or 0
static inline char *read_file (const char *path, int *len = 0, int maxLen = 64 << 20)
{
	void *f = kapi_open (path);
	if (!f) return 0;
	unsigned n = kapi_fsize (f);
	if ((int) n > maxLen) { kapi_close (f); return 0; }
	char *b = (char *) malloc (n + 1);
	int got = b ? kapi_read (f, b, n) : -1;
	kapi_close (f);
	if (!b) return 0;
	if (got < 0) got = 0;
	b[got] = 0;
	if (len) *len = got;
	return b;
}
static inline const char *base_name (const char *p)
{
	const char *s = strrchr (p, '/');
	return s ? s + 1 : p;
}
// a size for people: "512 B", "1.4 KB", "2.03 MB"
static inline void human_size (char *out, int cap, long long n)
{
	if (n < 1024) snprintf (out, cap, "%lld B", n);
	else if (n < 1024 * 1024) snprintf (out, cap, "%lld.%lld KB", n / 1024, (n % 1024) * 10 / 1024);
	else snprintf (out, cap, "%lld.%02lld MB", n / (1024 * 1024), (n % (1024 * 1024)) * 100 / (1024 * 1024));
}
// a file name from a title: letters, digits, '-', '_' (spaces -> '_')
static inline void safe_name (Str &out, const char *s)
{
	for (; *s; s++)
	{
		char c = *s;
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_') out.add (c);
		else if (c == ' ' || c == '.') out.add ('_');
	}
	if (out.empty ()) out.set ("untitled");
}

} // namespace cr

#endif
