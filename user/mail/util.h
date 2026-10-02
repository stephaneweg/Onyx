//
// mail/util.h -- Mail's small text tools: a growing buffer, base64, quoted-printable, the charsets met in mail
// (UTF-8, Latin-1, Windows-1252, ASCII) to UTF-8, RFC 2047's encoded words, RFC 5322's dates, case-blind compares.
// Header-only, newlib (malloc / free). Part of Onyx's mail (docs/mail/README.md).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. Permission is hereby granted, free of
// charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished
// to do so, subject to the following conditions: the above copyright notice and this permission notice shall be
// included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF
// ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
#ifndef ONYX_MAIL_UTIL_H
#define ONYX_MAIL_UTIL_H

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>

namespace mail {

// ---- a growing byte buffer (NUL-terminated) -------------------------------------------------------------------------------
struct Buf
{
	char *p; int n, cap;
	Buf () : p (0), n (0), cap (0) {}
	~Buf () { free (p); }
	Buf (const Buf &) = delete;
	Buf &operator= (const Buf &) = delete;
	bool reserve (int k)
	{
		if (n + k + 1 <= cap) return true;
		int c = cap ? cap : 256; while (c < n + k + 1) c *= 2;
		char *q = (char *) realloc (p, c); if (!q) return false;
		p = q; cap = c; return true;
	}
	void add (const void *s, int k) { if (k <= 0 || !reserve (k)) return; memcpy (p + n, s, k); n += k; p[n] = 0; }
	void add (const char *s) { add (s, (int) strlen (s)); }
	void addc (char c) { add (&c, 1); }
	void addf (const char *fmt, ...)
	{
		va_list a; va_start (a, fmt); char t[1024]; int k = vsnprintf (t, sizeof t, fmt, a); va_end (a);
		if (k >= (int) sizeof t) { reserve (k); va_start (a, fmt); vsnprintf (p + n, k + 1, fmt, a); va_end (a); n += k; }
		else add (t, k);
	}
	void clear () { n = 0; if (p) p[0] = 0; }
	const char *c () const { return p ? p : ""; }
	char *take () { char *r = p ? p : (char *) calloc (1, 1); p = 0; n = cap = 0; return r; }
};

static inline char *sdup (const char *s) { if (!s) s = ""; size_t n = strlen (s); char *r = (char *) malloc (n + 1); memcpy (r, s, n + 1); return r; }
static inline char *sndup (const char *s, int n) { char *r = (char *) malloc (n + 1); memcpy (r, s, n); r[n] = 0; return r; }
static inline void scpy (char *d, const char *s, int cap) { if (cap <= 0) return; int i = 0; for (; s && s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = 0; }
static inline int lc (int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static inline bool ieq (const char *a, const char *b) { while (*a && *b) { if (lc (*a) != lc (*b)) return false; a++; b++; } return *a == *b; }
static inline bool ieqn (const char *a, const char *b, int n) { for (int i = 0; i < n; i++) { if (lc (a[i]) != lc (b[i])) return false; if (!a[i]) return true; } return true; }
static inline const char *ifind (const char *s, const char *w) { int n = (int) strlen (w); for (; *s; s++) if (ieqn (s, w, n)) return s; return 0; }
static inline bool istarts (const char *s, const char *w) { return ieqn (s, w, (int) strlen (w)); }

// ---- UTF-8 ------------------------------------------------------------------------------------------------------------------
static inline int u8put (char *o, unsigned c)
{
	if (c < 0x80) { o[0] = (char) c; return 1; }
	if (c < 0x800) { o[0] = (char) (0xC0 | c >> 6); o[1] = (char) (0x80 | (c & 63)); return 2; }
	if (c < 0x10000) { o[0] = (char) (0xE0 | c >> 12); o[1] = (char) (0x80 | (c >> 6 & 63)); o[2] = (char) (0x80 | (c & 63)); return 3; }
	o[0] = (char) (0xF0 | c >> 18); o[1] = (char) (0x80 | (c >> 12 & 63)); o[2] = (char) (0x80 | (c >> 6 & 63)); o[3] = (char) (0x80 | (c & 63)); return 4;
}
static inline unsigned u8get (const char *&s)
{
	unsigned c = (unsigned char) *s++;
	if (c < 0x80) return c;
	int k = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
	c &= 0x3F >> k;
	while (k-- && (*s & 0xC0) == 0x80) c = c << 6 | (*s++ & 0x3F);
	return c;
}
static inline bool valid_utf8 (const unsigned char *s, int n)
{
	for (int i = 0; i < n; )
	{
		unsigned c = s[i];
		int k = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : -1;
		if (k < 0 || i + k >= n + (k ? 0 : 1)) return false;
		for (int j = 1; j <= k; j++) if (i + j >= n || (s[i + j] & 0xC0) != 0x80) return false;
		i += k + 1;
	}
	return true;
}

// ---- charsets -> UTF-8 --------------------------------------------------------------------------------------------------------
static const unsigned short CP1252[32] = { 0x20AC, 0x81, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x8D, 0x017D, 0x8F,
					   0x90, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D, 0x017E, 0x0178 };
static const unsigned short ISO15[8][2] = { { 0xA4, 0x20AC }, { 0xA6, 0x160 }, { 0xA8, 0x161 }, { 0xB4, 0x17D }, { 0xB8, 0x17E }, { 0xBC, 0x152 }, { 0xBD, 0x153 }, { 0xBE, 0x178 } };
// the bytes (in charset cs) appended to o as UTF-8; an unknown charset: UTF-8 if valid, else Windows-1252
static void to_utf8 (Buf &o, const char *s, int n, const char *cs)
{
	bool u8 = !cs || !*cs || ieq (cs, "utf-8") || ieq (cs, "utf8") || ieq (cs, "us-ascii") || ieq (cs, "ascii");
	if (u8 && valid_utf8 ((const unsigned char *) s, n)) { o.add (s, n); return; }
	bool latin1 = cs && (ieq (cs, "iso-8859-1") || ieq (cs, "latin1") || ieq (cs, "l1"));
	bool l9 = cs && ieq (cs, "iso-8859-15");
	if (!o.reserve (n * 3)) return;
	for (int i = 0; i < n; i++)
	{
		unsigned c = (unsigned char) s[i];
		if (c >= 0x80 && c < 0xA0 && !latin1) c = CP1252[c - 0x80];
		else if (l9) for (int k = 0; k < 8; k++) if (ISO15[k][0] == c) c = ISO15[k][1];
		char t[4]; int k = u8put (t, c); o.add (t, k);
	}
}

// ---- base64, quoted-printable ----------------------------------------------------------------------------------------------------
static inline int b64v (int c) { return c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52 : c == '+' || c == '-' ? 62 : c == '/' || c == '_' ? 63 : -1; }
static void b64_decode (Buf &o, const char *s, int n)
{
	unsigned acc = 0; int bits = 0;
	o.reserve (n * 3 / 4 + 4);
	for (int i = 0; i < n; i++)
	{
		int v = b64v ((unsigned char) s[i]);
		if (v < 0) { if (s[i] == '=') break; continue; }
		acc = acc << 6 | (unsigned) v; bits += 6;
		if (bits >= 8) { bits -= 8; o.addc ((char) (acc >> bits & 255)); }
	}
}
static void b64_encode (Buf &o, const void *data, int n, int lineLen = 76)
{
	static const char *T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	const unsigned char *s = (const unsigned char *) data; int col = 0;
	o.reserve (n * 4 / 3 + n / 50 + 8);
	for (int i = 0; i < n; i += 3)
	{
		unsigned v = (unsigned) s[i] << 16 | (i + 1 < n ? (unsigned) s[i + 1] << 8 : 0) | (i + 2 < n ? s[i + 2] : 0);
		char t[4] = { T[v >> 18 & 63], T[v >> 12 & 63], i + 1 < n ? T[v >> 6 & 63] : '=', i + 2 < n ? T[v & 63] : '=' };
		o.add (t, 4); col += 4;
		if (lineLen && col >= lineLen) { o.add ("\r\n", 2); col = 0; }
	}
	if (lineLen && col) o.add ("\r\n", 2);
}
static inline int hexv (int c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; }
// quoted-printable (header: '_' is a space)
static void qp_decode (Buf &o, const char *s, int n, bool header = false)
{
	o.reserve (n);
	for (int i = 0; i < n; i++)
	{
		char c = s[i];
		if (c == '=' && i + 1 < n && (s[i + 1] == '\r' || s[i + 1] == '\n')) { i++; if (s[i] == '\r' && i + 1 < n && s[i + 1] == '\n') i++; continue; }	// soft break
		if (c == '=' && i + 2 < n && hexv (s[i + 1]) >= 0 && hexv (s[i + 2]) >= 0) { o.addc ((char) (hexv (s[i + 1]) * 16 + hexv (s[i + 2]))); i += 2; continue; }
		if (header && c == '_') c = ' ';
		o.addc (c);
	}
}

// ---- RFC 2047: =?charset?B|Q?text?= in headers -> UTF-8 (unfolded) --------------------------------------------------------------
static void decode_header (Buf &o, const char *s, int n = -1)
{
	if (n < 0) n = (int) strlen (s);
	int i = 0; bool prevWord = false;
	int wsFrom = -1, wsTo = -1;					// (the whitespace before the next token)
	while (i < n)
	{
		if (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')
		{
			wsFrom = i; while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n')) i++;
			wsTo = i; continue;
		}
		// an encoded word: =?cs?E?txt?=
		if (s[i] == '=' && i + 1 < n && s[i + 1] == '?')
		{
			int a = i + 2, q1 = a; while (q1 < n && s[q1] != '?') q1++;
			if (q1 + 2 < n && s[q1 + 2] == '?')
			{
				int e = q1 + 3; while (e + 1 < n && !(s[e] == '?' && s[e + 1] == '=')) e++;
				if (e + 1 < n)
				{
					char cs[48]; int cl = q1 - a < 47 ? q1 - a : 47; memcpy (cs, s + a, cl); cs[cl] = 0;
					char *star = strchr (cs, '*'); if (star) *star = 0;	// (RFC 2231's language)
					Buf dec;
					if (lc (s[q1 + 1]) == 'b') b64_decode (dec, s + q1 + 3, e - (q1 + 3)); else qp_decode (dec, s + q1 + 3, e - (q1 + 3), true);
					if (wsFrom >= 0 && !prevWord && o.n) o.addc (' ');	// (between two encoded words: no space)
					to_utf8 (o, dec.c (), dec.n, cs);
					wsFrom = -1; prevWord = true; i = e + 2;
					continue;
				}
			}
		}
		if (wsFrom >= 0 && o.n) o.addc (' ');
		wsFrom = -1; prevWord = false;
		int j = i + 1; while (j < n && s[j] != ' ' && s[j] != '\t' && s[j] != '\r' && s[j] != '\n' && !(s[j] == '=' && j + 1 < n && s[j + 1] == '?')) j++;
		to_utf8 (o, s + i, j - i, 0);
		i = j;
	}
	(void) wsTo;
}
// a header's value encoded for sending: plain if ASCII, else =?UTF-8?B?...?= words
static void encode_header (Buf &o, const char *s)
{
	bool ascii = true; for (const char *p = s; *p; p++) if ((unsigned char) *p >= 0x80) ascii = false;
	if (ascii) { o.add (s); return; }
	int n = (int) strlen (s), i = 0;
	while (i < n)
	{
		int k = n - i > 45 ? 45 : n - i;
		while (k < n - i && k > 0 && ((unsigned char) s[i + k] & 0xC0) == 0x80) k--;	// (whole characters)
		if (i) o.add ("\r\n ");
		o.add ("=?UTF-8?B?"); b64_encode (o, s + i, k, 0); o.add ("?=");
		i += k;
	}
}

// ---- dates (RFC 5322) <-> seconds since 1970 (UTC) -------------------------------------------------------------------------------
static long long days_civil (long long y, int m, int d)
{
	y -= m <= 2; long long era = (y >= 0 ? y : y - 399) / 400; long long yoe = y - era * 400;
	long long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1; long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + doe - 719468;
}
static void civil (long long z, int *y, int *m, int *d)
{
	z += 719468; long long era = (z >= 0 ? z : z - 146096) / 146097; long long doe = z - era * 146097;
	long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; long long yy = yoe + era * 400;
	long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100); long long mp = (5 * doy + 2) / 153;
	*d = (int) (doy - (153 * mp + 2) / 5 + 1); *m = (int) (mp < 10 ? mp + 3 : mp - 9); *y = (int) (yy + (*m <= 2));
}
static const char *MONTHS3[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
// "Tue, 1 Oct 2026 10:05:00 +0200" (or IMAP's "01-Oct-2026 10:05:00 +0200") -> UTC seconds; 0: not understood
static long long parse_date (const char *s)
{
	if (!s) return 0;
	while (*s && !(*s >= '0' && *s <= '9')) s++;			// (the weekday)
	int d = atoi (s); while (*s >= '0' && *s <= '9') s++;
	while (*s == ' ' || *s == '-') s++;
	int m = -1; for (int k = 0; k < 12; k++) if (ieqn (s, MONTHS3[k], 3)) m = k + 1;
	if (m < 0) return 0;
	while (*s && *s != ' ' && *s != '-') s++; while (*s == ' ' || *s == '-') s++;
	int y = atoi (s); if (y < 100) y += y < 50 ? 2000 : 1900;
	while (*s >= '0' && *s <= '9') s++; while (*s == ' ') s++;
	int hh = 0, mm = 0, ss = 0;
	if (*s >= '0' && *s <= '9') { hh = atoi (s); while (*s && *s != ':') s++; if (*s) { mm = atoi (++s); while (*s >= '0' && *s <= '9') s++; if (*s == ':') ss = atoi (++s); } while (*s && *s != ' ') s++; while (*s == ' ') s++; }
	int off = 0;
	if (*s == '+' || *s == '-') { int v = atoi (s + 1); off = (v / 100 * 60 + v % 100) * (*s == '-' ? -1 : 1); }
	else if (ieqn (s, "EDT", 3)) off = -240; else if (ieqn (s, "EST", 3) || ieqn (s, "CDT", 3)) off = -300; else if (ieqn (s, "CST", 3) || ieqn (s, "MDT", 3)) off = -360;
	else if (ieqn (s, "MST", 3) || ieqn (s, "PDT", 3)) off = -420; else if (ieqn (s, "PST", 3)) off = -480;
	return ((days_civil (y, m, d) * 24 + hh) * 60 + mm) * 60 + ss - off * 60LL;
}
// seconds -> "Tue, 01 Oct 2026 10:05:00 +0200" (the offset in minutes)
static void format_date (char *out, int cap, long long t, int offMin)
{
	static const char *WD[7] = { "Thu", "Fri", "Sat", "Sun", "Mon", "Tue", "Wed" };
	long long l = t + offMin * 60LL; long long day = l >= 0 ? l / 86400 : (l - 86399) / 86400; int sec = (int) (l - day * 86400);
	int y, m, d; civil (day, &y, &m, &d);
	int ao = offMin < 0 ? -offMin : offMin;
	snprintf (out, cap, "%s, %02d %s %04d %02d:%02d:%02d %c%02d%02d", WD[((day % 7) + 7) % 7], d, MONTHS3[m - 1], y, sec / 3600, sec / 60 % 60, sec % 60, offMin < 0 ? '-' : '+', ao / 60, ao % 60);
}

// ---- addresses: "Name <a@b>" / a@b / "Name" <a@b>, lists of them --------------------------------------------------------------------
struct Addr { char name[120], email[160]; };
// the header's addresses (its raw value: split first, then each name's encoded words decoded) -> how many
static int parse_addrs (const char *s, Addr *out, int max)
{
	int n = 0;
	const char *p = s ? s : "";
	while (*p && n < max)
	{
		while (*p == ' ' || *p == ',' || *p == ';' || *p == '\t') p++;
		if (!*p) break;
		// one address: up to a comma outside quotes / angle brackets
		const char *a = p; bool q = false; int ang = 0;
		while (*p && (q || ang || (*p != ',' && *p != ';'))) { if (*p == '"') q = !q; else if (!q && *p == '<') ang++; else if (!q && *p == '>' && ang) ang--; p++; }
		char one[400]; int l = (int) (p - a) < 399 ? (int) (p - a) : 399; memcpy (one, a, l); one[l] = 0;
		Addr &r = out[n]; r.name[0] = r.email[0] = 0;
		char *lt = strrchr (one, '<'), *gt = lt ? strchr (lt, '>') : 0;
		if (lt && gt)
		{
			*gt = 0; scpy (r.email, lt + 1, sizeof r.email); *lt = 0;
			char *nm = one; while (*nm == ' ' || *nm == '"') nm++;
			int k = (int) strlen (nm); while (k && (nm[k - 1] == ' ' || nm[k - 1] == '"')) nm[--k] = 0;
			scpy (r.name, nm, sizeof r.name);
		}
		else
		{
			char *e = one; while (*e == ' ') e++;
			char *par = strchr (e, '(');						// a@b (Name)
			if (par) { char *cp = strchr (par, ')'); if (cp) *cp = 0; scpy (r.name, par + 1, sizeof r.name); *par = 0; }
			int k = (int) strlen (e); while (k && e[k - 1] == ' ') e[--k] = 0;
			scpy (r.email, e, sizeof r.email);
		}
		if (strstr (r.name, "=?")) { Buf d; decode_header (d, r.name); scpy (r.name, d.c (), sizeof r.name); }
		if (r.email[0] || r.name[0]) n++;
	}
	return n;
}
// the name to show: the name, else the address's local part
static void addr_show (const Addr &a, char *out, int cap)
{
	if (a.name[0]) { scpy (out, a.name, cap); return; }
	scpy (out, a.email, cap); char *at = strchr (out, '@'); if (at) *at = 0;
}

} // namespace mail

#endif
