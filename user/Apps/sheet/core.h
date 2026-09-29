//
// core.h -- the spreadsheet's basics: a growing buffer, UTF-8 (the cells' text is UTF-8; the keyboard
// and wtk's own font speak Latin-1), case folding, numbers to text and back, Excel's serial dates
// (the 1900 system, its 29 February 1900 included), a scratch arena for the values a formula makes.
// Plain C++ over newlib's libc / libm: the same code builds on the PC for the host tests.
//
#ifndef _sheet_core_h
#define _sheet_core_h

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <ctype.h>

namespace ss {

static inline int imin (int a, int b) { return a < b ? a : b; }
static inline int imax (int a, int b) { return a > b ? a : b; }
static inline int iclamp (int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

// ---- a growing buffer (always NUL-terminated) --------------------------------------------------------------
struct Buf
{
	char *b; int n, cap;
	Buf () : b (0), n (0), cap (0) {}
	~Buf () { free (b); }
	void reserve (int m)
	{
		if (m + 1 <= cap) return;
		int c = cap ? cap : 128;
		while (c < m + 1) c *= 2;
		b = (char *) realloc (b, c); cap = c;
	}
	void put (char c) { reserve (n + 1); b[n++] = c; b[n] = 0; }
	void putn (const char *s, int k) { if (k <= 0) { reserve (n); b[n] = 0; return; } reserve (n + k); memcpy (b + n, s, k); n += k; b[n] = 0; }
	void puts (const char *s) { putn (s, (int) strlen (s)); }
	void putu (unsigned cp);			// a character as UTF-8
	void puti (long long v) { char t[24]; snprintf (t, sizeof t, "%lld", v); puts (t); }
	void clear () { n = 0; if (b) b[0] = 0; }
	const char *str () { reserve (n); b[n] = 0; return b; }
	char *take () { reserve (n); char *r = b; b = 0; n = cap = 0; return r; }	// (the caller frees)
private:
	Buf (const Buf &);
	Buf &operator= (const Buf &);
};

static char *sdup (const char *s, int n = -1)
{
	if (!s) s = "";
	if (n < 0) n = (int) strlen (s);
	char *d = (char *) malloc (n + 1);
	memcpy (d, s, n); d[n] = 0;
	return d;
}
static void scpy (char *d, const char *s, int cap) { int i = 0; if (s) for (; s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = 0; }
static void scat (char *d, const char *s, int cap) { int n = (int) strlen (d); if (n < cap - 1) scpy (d + n, s, cap - n); }

// ---- UTF-8 -------------------------------------------------------------------------------------------------
// A character from s (n bytes at most); *len its bytes. A byte that does not start a valid sequence is
// taken as Latin-1 (a file written by an older app).
static unsigned u8_dec (const char *s, int n, int *len)
{
	const unsigned char *p = (const unsigned char *) s;
	if (n <= 0) { *len = 0; return 0; }
	unsigned c = p[0];
	if (c < 0x80) { *len = 1; return c; }
	int k = c >= 0xF0 && c < 0xF8 ? 4 : c >= 0xE0 ? 3 : c >= 0xC2 && c < 0xE0 ? 2 : 0;
	if (k == 0 || k > n) { *len = 1; return c; }
	unsigned v = c & (0xFF >> (k + 1));
	for (int i = 1; i < k; i++)
	{
		if ((p[i] & 0xC0) != 0x80) { *len = 1; return c; }
		v = v << 6 | (p[i] & 0x3F);
	}
	*len = k;
	return v;
}
static int u8_enc (unsigned cp, char *o)
{
	if (cp < 0x80) { o[0] = (char) cp; return 1; }
	if (cp < 0x800) { o[0] = (char) (0xC0 | cp >> 6); o[1] = (char) (0x80 | (cp & 0x3F)); return 2; }
	if (cp < 0x10000) { o[0] = (char) (0xE0 | cp >> 12); o[1] = (char) (0x80 | (cp >> 6 & 0x3F)); o[2] = (char) (0x80 | (cp & 0x3F)); return 3; }
	o[0] = (char) (0xF0 | cp >> 18); o[1] = (char) (0x80 | (cp >> 12 & 0x3F)); o[2] = (char) (0x80 | (cp >> 6 & 0x3F)); o[3] = (char) (0x80 | (cp & 0x3F));
	return 4;
}
inline void Buf::putu (unsigned cp) { char t[4]; putn (t, u8_enc (cp, t)); }

static int u8_count (const char *s, int n)		// characters in n bytes
{
	int k = 0, i = 0, l;
	while (i < n) { u8_dec (s + i, n - i, &l); i += l; k++; }
	return k;
}
static int u8_offset (const char *s, int n, int ch)	// the byte where character ch starts (n: past the end)
{
	int i = 0, l;
	while (i < n && ch > 0) { u8_dec (s + i, n - i, &l); i += l; ch--; }
	return i;
}
static bool u8_valid (const char *s, int n)
{
	for (int i = 0; i < n; )
	{
		unsigned char c = (unsigned char) s[i];
		if (c < 0x80) { i++; continue; }
		int l; u8_dec (s + i, n - i, &l);
		if (l == 1) return false;
		i += l;
	}
	return true;
}
// Latin-1 (the keyboard's, wtk's text boxes', a CSV's) -> UTF-8; 0x80 is the euro sign, as Windows-1252
// and wtk's font have it (the keymaps' AltGr+E).
static unsigned latin1_cp (unsigned c) { return c == 0x80 ? 0x20AC : c; }
static char *latin1_to_u8 (const char *s, int n)
{
	Buf b;
	for (int i = 0; i < n; i++) b.putu (latin1_cp ((unsigned char) s[i]));
	return b.take ();
}
// UTF-8 -> Latin-1 for wtk's own font ('?' for a character it lacks; the euro sign: 0x80).
static void u8_to_latin1 (const char *s, char *o, int cap)
{
	int n = (int) strlen (s), i = 0, k = 0, l;
	while (i < n && k < cap - 1)
	{
		unsigned c = u8_dec (s + i, n - i, &l);
		i += l;
		o[k++] = c < 256 ? (char) c : c == 0x20AC ? (char) 0x80 : '?';
	}
	o[k] = 0;
}

// ---- case -------------------------------------------------------------------------------------------------
// Upper / lower case: ASCII, Latin-1, Latin Extended-A, Greek, Cyrillic (enough for names and words).
static unsigned uc_lower (unsigned c)
{
	if (c < 128) return c >= 'A' && c <= 'Z' ? c + 32 : c;
	if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 32;
	if (c >= 0x100 && c <= 0x17F)
	{
		if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E)) return (c & 1) ? c + 1 : c;
		if (c == 0x178) return 0xFF;
		return (c & 1) ? c : c + 1;
	}
	if (c >= 0x391 && c <= 0x3AB && c != 0x3A2) return c + 32;
	if (c >= 0x410 && c <= 0x42F) return c + 32;
	if (c >= 0x400 && c <= 0x40F) return c + 80;
	return c;
}
static unsigned uc_upper (unsigned c)
{
	if (c < 128) return c >= 'a' && c <= 'z' ? c - 32 : c;
	if (c >= 0xE0 && c <= 0xFE && c != 0xF7) return c - 32;
	if (c == 0xFF) return 0x178;
	if (c >= 0x100 && c <= 0x17F)
	{
		if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E)) return (c & 1) ? c : c - 1;
		return (c & 1) ? c - 1 : c;
	}
	if (c >= 0x3B1 && c <= 0x3CB && c != 0x3C2) return c - 32;
	if (c >= 0x430 && c <= 0x44F) return c - 32;
	if (c >= 0x450 && c <= 0x45F) return c - 80;
	return c;
}
// Compare as Excel does (case ignored); -1, 0, 1.
static int ci_cmp (const char *a, int na, const char *b, int nb)
{
	int i = 0, j = 0, la, lb;
	while (i < na && j < nb)
	{
		unsigned x = uc_lower (u8_dec (a + i, na - i, &la)), y = uc_lower (u8_dec (b + j, nb - j, &lb));
		if (x != y) return x < y ? -1 : 1;
		i += la; j += lb;
	}
	return i < na ? 1 : j < nb ? -1 : 0;
}
static bool ci_eq (const char *a, const char *b) { return ci_cmp (a, (int) strlen (a), b, (int) strlen (b)) == 0; }
static bool ascii_ieq (const char *a, const char *b, int n)	// the first n bytes, ASCII case ignored
{
	for (int i = 0; i < n; i++)
	{
		char x = a[i], y = b[i];
		if (x >= 'a' && x <= 'z') x -= 32;
		if (y >= 'a' && y <= 'z') y -= 32;
		if (x != y) return false;
		if (!x) return true;
	}
	return true;
}

// ---- numbers as text ---------------------------------------------------------------------------------------
// A number with 15 significant digits at most, no exponent when it is reasonable -- as the formula bar
// shows a cell (and as a number becomes text: ="x"&A1).
static void num_full (double v, char *o, int cap = 32)
{
	if (v == 0) { scpy (o, "0", cap); return; }
	if (isnan (v) || isinf (v)) { scpy (o, "#NUM!", cap); return; }
	double a = fabs (v);
	char t[48];
	if (a >= 1e-9 && a < 1e15)
	{
		snprintf (t, sizeof t, "%.15g", v);
		if (strchr (t, 'e'))					// (%g chose an exponent: write it out)
		{
			int e = (int) floor (log10 (a));
			int dec = 14 - e; if (dec < 0) dec = 0; if (dec > 20) dec = 20;
			snprintf (t, sizeof t, "%.*f", dec, v);
			if (strchr (t, '.')) { int n = (int) strlen (t); while (n > 0 && t[n - 1] == '0') t[--n] = 0; if (n > 0 && t[n - 1] == '.') t[--n] = 0; }
		}
	}
	else
	{
		snprintf (t, sizeof t, "%.14E", v);			// 1.23456789012340E+020 -> 1.2345678901234E+20
		char *e = strchr (t, 'E');
		char mant[32]; int mn = (int) (e - t); memcpy (mant, t, mn); mant[mn] = 0;
		while (mn > 0 && mant[mn - 1] == '0') mant[--mn] = 0;
		if (mn > 0 && mant[mn - 1] == '.') mant[--mn] = 0;
		int ex = atoi (e + 1);
		snprintf (t, sizeof t, "%sE%c%02d", mant, ex < 0 ? '-' : '+', ex < 0 ? -ex : ex);
	}
	scpy (o, t, cap);
}

// The General format in a cell maxc characters wide: the most digits that fit (10 at most), else the
// exponent form, else "###" (as the spreadsheets do).
static void num_general (double v, char *o, int cap, int maxc = 11)
{
	if (v == 0) { scpy (o, "0", cap); return; }
	if (isnan (v) || isinf (v)) { scpy (o, "#NUM!", cap); return; }
	if (maxc < 1) maxc = 1;
	if (maxc > 24) maxc = 24;
	double a = fabs (v);
	char t[64];
	int sig = 10;						// (the General format's significant digits)
	if (a < 1e11 && a >= 1e-5)
	{
		int ip = a < 1 ? 1 : (int) floor (log10 (a)) + 1;	// the integer part's digits
		int room = maxc - ip - (v < 0 ? 1 : 0);		// what is left for "." and decimals
		int dec = room - 1;
		if (dec > sig - (a < 1 ? 0 : ip)) dec = sig - (a < 1 ? 0 : ip);
		if (a < 1)						// leading zeros after the point do not count as digits
		{
			int lz = (int) floor (-log10 (a));
			if (dec > lz + sig) dec = lz + sig;
		}
		if (dec < 0) dec = 0;
		if (room >= 0)
		{
			snprintf (t, sizeof t, "%.*f", dec, v);
			if (strchr (t, '.')) { int n = (int) strlen (t); while (n > 0 && t[n - 1] == '0') t[--n] = 0; if (n > 0 && t[n - 1] == '.') t[--n] = 0; }
			if ((int) strlen (t) <= maxc && strcmp (t, "0") && strcmp (t, "-0")) { scpy (o, t, cap); return; }
		}
	}
	// the exponent form: "1.23E+11"
	for (int d = 5; d >= 0; d--)
	{
		snprintf (t, sizeof t, "%.*E", d, v);
		char *e = strchr (t, 'E');
		char mant[32]; int mn = (int) (e - t); memcpy (mant, t, mn); mant[mn] = 0;
		if (strchr (mant, '.')) { while (mn > 0 && mant[mn - 1] == '0') mant[--mn] = 0; if (mn > 0 && mant[mn - 1] == '.') mant[--mn] = 0; }
		int ex = atoi (e + 1);
		char r[48]; snprintf (r, sizeof r, "%sE%c%02d", mant, ex < 0 ? '-' : '+', ex < 0 ? -ex : ex);
		if ((int) strlen (r) <= maxc || d == 0)
		{
			if ((int) strlen (r) > maxc) { int k = imin (maxc, cap - 1); for (int i = 0; i < k; i++) o[i] = '#'; o[k] = 0; return; }
			scpy (o, r, cap); return;
		}
	}
}

// Text -> a number, the whole text (spaces around allowed): "12", "-3.5", "1e3", ".5". For the formulas'
// conversions ("3" + 1) -- the input line understands more (input.h).
static bool text_to_num (const char *s, int n, double *out)
{
	int i = 0;
	while (i < n && (s[i] == ' ' || s[i] == '\t')) i++;
	while (n > i && (s[n - 1] == ' ' || s[n - 1] == '\t')) n--;
	if (i >= n || n - i > 60) return false;
	char t[64]; int k = 0;
	bool dig = false;
	for (int j = i; j < n; j++)
	{
		char c = s[j];
		if (c >= '0' && c <= '9') dig = true;
		else if (!(c == '.' || c == '+' || c == '-' || c == 'e' || c == 'E')) return false;
		t[k++] = c;
	}
	t[k] = 0;
	if (!dig) return false;
	char *e;
	double v = strtod (t, &e);
	if (*e) return false;
	*out = v;
	return true;
}

// ---- dates (Excel's serial numbers, the 1900 system) --------------------------------------------------------
// Day 1 = 1 January 1900; day 60 = the 29 February 1900 that never was (kept, as Excel keeps it: the
// dates after it count from 30 December 1899). Times are the day's fraction.
static long days_from_civil (int y, int m, int d)	// days since 1970-01-01 (proleptic Gregorian)
{
	y -= m <= 2;
	long era = (y >= 0 ? y : y - 399) / 400;
	unsigned yoe = (unsigned) (y - era * 400);
	unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + (long) doe - 719468;
}
static void civil_from_days (long z, int *y, int *m, int *d)
{
	z += 719468;
	long era = (z >= 0 ? z : z - 146096) / 146097;
	unsigned doe = (unsigned) (z - era * 146097);
	unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	long yy = (long) yoe + era * 400;
	unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	unsigned mp = (5 * doy + 2) / 153;
	*d = (int) (doy - (153 * mp + 2) / 5 + 1);
	*m = (int) (mp < 10 ? mp + 3 : mp - 9);
	*y = (int) (yy + (*m <= 2));
}
static const long EPOCH_1899_12_30 = -25569;		// days_from_civil (1899, 12, 30)
static bool leap_year (int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
static int month_days (int y, int m) { static const int d[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 }; return m == 2 && leap_year (y) ? 29 : d[(m + 11) % 12]; }

// y / m / d -> the serial (months and days out of range roll over, as DATE does).
static double date_serial (int y, int m, int d)
{
	if (y >= 0 && y < 1900) y += 1900;			// (DATE's two-digit years)
	y += (m - 1) >= 0 ? (m - 1) / 12 : -((12 - m) / 12);
	m = ((m - 1) % 12 + 12) % 12 + 1;
	long z = days_from_civil (y, m, 1) + d - 1;
	long s = z - EPOCH_1899_12_30;
	if (s < 61) s--;					// (before March 1900: Excel's count is one lower...)
	if (y == 1900 && m == 2 && d == 29) s = 60;		// (... and its 29 February)
	return (double) s;
}
// The serial -> y / m / d (serial 0: 0 January 1900, 60: 29 February 1900).
static void serial_date (double v, int *y, int *m, int *d)
{
	long s = (long) floor (v);
	if (s == 60) { *y = 1900; *m = 2; *d = 29; return; }
	if (s <= 0) { *y = 1900; *m = 1; *d = 0; return; }
	if (s < 60) s++;
	civil_from_days (s + EPOCH_1899_12_30, y, m, d);
}
static int serial_weekday (double v)			// 0 = Sunday
{
	long s = (long) floor (v);				// (before March 1900, Excel's own weekdays: kept)
	return (int) (((s + 6) % 7 + 7) % 7);
}
// The time of the day (rounded to the millisecond): h, m, s, ms.
static void serial_time (double v, int *h, int *mi, int *s, int *ms)
{
	double f = v - floor (v);
	long t = (long) floor (f * 86400000.0 + 0.5);
	if (t >= 86400000L) t = 86399999L;
	*ms = (int) (t % 1000); t /= 1000;
	*s = (int) (t % 60); t /= 60;
	*mi = (int) (t % 60); *h = (int) (t / 60);
}

// ---- a scratch arena: the strings and arrays a formula makes while it computes ----------------------------
// Released to a mark when the formula's value is kept (the marks nest as the evaluations do).
struct Arena
{
	struct Block { Block *next; int size, used; char data[1]; };
	Block *head;						// the current block (the list goes back to older ones)
	Arena () : head (0) {}
	~Arena () { while (head) { Block *n = head->next; free (head); head = n; } }
	void *alloc (int n)
	{
		n = (n + 15) & ~15;
		if (!head || head->used + n > head->size)
		{
			int sz = n > 60000 ? n : 65536 - (int) sizeof (Block);
			Block *b = (Block *) malloc (sizeof (Block) + sz);
			b->size = sz; b->used = 0; b->next = head; head = b;
		}
		void *p = head->data + head->used;
		head->used += n;
		return p;
	}
	struct Mark { Block *b; int used; };
	Mark mark () { Mark m; m.b = head; m.used = head ? head->used : 0; return m; }
	void release (Mark m)
	{
		while (head && head != m.b) { Block *n = head->next; free (head); head = n; }
		if (head) head->used = m.used;
	}
	char *str (const char *s, int n) { char *d = (char *) alloc (n + 1); if (n) memcpy (d, s, n); d[n] = 0; return d; }
};

} // namespace ss

#endif
