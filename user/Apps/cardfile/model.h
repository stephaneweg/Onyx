//
// model.h -- Cardfile's document: one FORM (a title, a description, its fields -- each a display
// name, a column name, a type and the type's options) and its RECORDS (a value per field).
//
// A value is kept as text, in one canonical form per type (what the file holds):
//   text       as typed, on one line          multi-line  as typed ('\n' between the lines)
//   integer    "-1234"                        decimal     "-1234.50" (the field's decimals, 0..6)
//   date       "2026-09-29" (ISO: sorts as text; shown 29/09/2026)
//   colour     "#3366CC"                      yes / no    "yes", or empty for no
//   choice     the option's text
// An empty value is "nothing there" (sorted last). A value read from a file that is not one of its
// type (a file edited by hand) is kept as it is: nothing is lost, the form asks for a valid one.
//
// Here too: the values shown (a date 29/09/2026, Yes / No) and read as typed (leniently: 1/2/26,
// 2026-02-01, "1 234,5"...), the file read and written (the format: main.cpp's header), CSV
// (exported; imported with each column's type guessed), the order the views show (the search, the
// sort). Freestanding: no libc, integers only.
//
#ifndef _cardfile_model_h
#define _cardfile_model_h

#include "kapi.h"

namespace cf {

// ---- strings ----------------------------------------------------------------------------------------
static int  slen (const char *s) { int n = 0; while (s && s[n]) n++; return n; }
static void scpy (char *d, const char *s, int cap) { int i = 0; if (s) for (; s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = '\0'; }
static void scat (char *d, const char *s, int cap) { int n = slen (d); if (n < cap) scpy (d + n, s, cap - n); }
static bool seq (const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static int  imin (int a, int b) { return a < b ? a : b; }
static int  imax (int a, int b) { return a > b ? a : b; }
static int  iclamp (int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static void itoa10 (long long v, char *out)
{
	int n = 0; unsigned long long u = v < 0 ? (unsigned long long) -v : (unsigned long long) v;
	if (v < 0) out[n++] = '-';
	char t[24]; int j = 0;
	do { t[j++] = (char) ('0' + u % 10); u /= 10; } while (u);
	while (j) out[n++] = t[--j];
	out[n] = '\0';
}
static void scat_num (char *d, long long v, int cap) { char t[24]; itoa10 (v, t); scat (d, t, cap); }

// The values: heap strings, the empty one shared.
static char s_empty[1] = { 0 };
static char *sdupn (const char *s, int n)
{
	if (!s || n <= 0) return s_empty;
	char *p = new char[n + 1];
	for (int i = 0; i < n; i++) p[i] = s[i];
	p[n] = '\0';
	return p;
}
static char *sdup (const char *s) { return sdupn (s, slen (s)); }
static void  sfree (char *s) { if (s && s != s_empty) delete [] s; }
static void  sset (char *&slot, const char *s) { char *n = sdup (s); sfree (slot); slot = n; }

// Latin-1 folded for comparisons: lower case, the accents off (e acute -> e, C cedilla -> c,
// sharp s -> s...); the rest as it is.
static unsigned char fold (unsigned char c)
{
	if (c >= 'A' && c <= 'Z') return (unsigned char) (c + 32);
	if (c < 0xC0) return c;
	static const char map[65] = "aaaaaaaceeeeiiiidnooooo*ouuuuyts" "aaaaaaaceeeeiiiidnooooo*ouuuuyty";
	char m = map[c - 0xC0];
	return m == '*' ? c : (unsigned char) m;
}
// Folded comparison, the runs of digits compared as numbers ("Item 9" before "Item 10").
static int ci_cmp (const char *a, const char *b)
{
	for (;;)
	{
		unsigned char x = fold ((unsigned char) *a), y = fold ((unsigned char) *b);
		if (x >= '0' && x <= '9' && y >= '0' && y <= '9')
		{
			while (*a == '0' && a[1] >= '0' && a[1] <= '9') a++;
			while (*b == '0' && b[1] >= '0' && b[1] <= '9') b++;
			int na = 0, nb = 0;
			while (a[na] >= '0' && a[na] <= '9') na++;
			while (b[nb] >= '0' && b[nb] <= '9') nb++;
			if (na != nb) return na - nb;
			for (int i = 0; i < na; i++) if (a[i] != b[i]) return a[i] - b[i];
			a += na; b += nb;
			continue;
		}
		if (x != y || !x) return (int) x - (int) y;
		a++; b++;
	}
}
static bool ci_eq (const char *a, const char *b)
{ for (;; a++, b++) { if (fold ((unsigned char) *a) != fold ((unsigned char) *b)) return false; if (!*a) return true; } }
// Is `nd` in `hay`, case and accents ignored? (nd: n bytes)
static bool ci_has (const char *hay, const char *nd, int n)
{
	if (n <= 0) return true;
	for (const char *h = hay; *h; h++)
	{
		int i = 0;
		while (i < n && h[i] && fold ((unsigned char) h[i]) == fold ((unsigned char) nd[i])) i++;
		if (i == n) return true;
	}
	return false;
}
// A copy without the blanks at both ends.
static void trim_copy (char *d, const char *s, int cap)
{
	while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
	int n = slen (s);
	while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) n--;
	if (n > cap - 1) n = cap - 1;
	for (int i = 0; i < n; i++) d[i] = s[i];
	d[n] = '\0';
}

// A growing buffer of bytes (the file written, a CSV...).
struct Out
{
	char *b; int n, cap;
	Out () : b (0), n (0), cap (0) {}
	~Out () { delete [] b; }
	void grow (int need)
	{
		if (n + need + 1 <= cap) return;
		int nc = cap ? cap * 2 : 4096;
		while (nc < n + need + 1) nc *= 2;
		char *nb = new char[nc];
		for (int i = 0; i < n; i++) nb[i] = b[i];
		delete [] b;
		b = nb; cap = nc;
	}
	void put (char c) { grow (1); b[n++] = c; b[n] = '\0'; }
	void putn (const char *s, int k) { grow (k); for (int i = 0; i < k; i++) b[n++] = s[i]; b[n] = '\0'; }
	void puts (const char *s) { putn (s, slen (s)); }
	char *take () { char *r = b; b = 0; n = cap = 0; return r; }
};

// ---- the types ----------------------------------------------------------------------------------------
enum { FT_TEXT, FT_MEMO, FT_INT, FT_DEC, FT_DATE, FT_COLOR, FT_BOOL, FT_CHOICE, FT_COUNT };
static const char *const TYPE_KEY[FT_COUNT] = { "text", "multiline", "integer", "decimal", "date", "colour", "yesno", "choice" };
static const char *const TYPE_NAME[FT_COUNT] = { "Text", "Multi-line text", "Integer", "Decimal number", "Date", "Colour",
						 "Yes / No", "Choice list" };
enum { MAXF = 64, MAXCH = 64, LABEL_MAX = 64, COL_MAX = 32, MAXDEC = 6, VAL_MAX = 1024, TITLE_MAX = 96, INFO_MAX = 160 };

// ---- numbers: integers, and decimals as integers scaled by 10^decimals --------------------------------
static long long ipow10 (int p) { long long s = 1; while (p-- > 0) s *= 10; return s; }

// A number as typed: a sign, digits, a decimal point -- '.' or ',' (with both, the last one; several
// of one kind: they group the thousands, as spaces and ' do) -> *out, scaled by 10^prec and
// rounded half away from zero (*exact: no digit dropped). False if it is not a number.
static bool parse_num (const char *s, int prec, long long *out, bool *exact)
{
	char b[48]; int n = 0, ndot = 0, ncom = 0; char lastSep = 0;
	while (*s == ' ' || *s == '\t') s++;
	bool neg = false;
	if (*s == '-' || *s == '+') { neg = *s == '-'; s++; }
	for (; *s; s++)
	{
		char c = *s;
		if (c >= '0' && c <= '9') { if (n >= 40) return false; b[n++] = c; }
		else if (c == '.' || c == ',') { if (n >= 40) return false; b[n++] = c; if (c == '.') ndot++; else ncom++; lastSep = c; }
		else if (c == ' ' || c == '\'' || c == '\t' || (unsigned char) c == 0xA0) continue;
		else return false;
	}
	char dp = ndot && ncom ? lastSep : ndot == 1 ? '.' : ncom == 1 ? ',' : 0;
	long long ip = 0, fr = 0, lim = 999999999999999LL / ipow10 (prec);
	int nf = 0, rd = -1; bool after = false, any = false, lost = false;
	for (int i = 0; i < n; i++)
	{
		char c = b[i];
		if (c == '.' || c == ',')
		{
			if (c == dp) { if (after) return false; after = true; }
			else if (after) return false;			// (a group separator after the point)
			continue;
		}
		any = true;
		int d = c - '0';
		if (!after) { ip = ip * 10 + d; if (ip > lim) return false; }
		else if (nf < prec) { fr = fr * 10 + d; nf++; }
		else if (rd < 0) { rd = d; if (d) lost = true; }
		else if (d) lost = true;
	}
	if (!any) return false;
	while (nf < prec) { fr *= 10; nf++; }
	long long v = ip * ipow10 (prec) + fr;
	if (rd >= 5) v++;
	if (exact) *exact = !lost;
	*out = neg ? -v : v;
	return true;
}
// A scaled number written as stored: "-1234.50".
static void fmt_num (long long v, int prec, char *out)
{
	int n = 0;
	if (v < 0) { out[n++] = '-'; v = -v; }
	long long sc = ipow10 (prec), ip = v / sc, fr = v % sc;
	char t[24]; int j = 0;
	do { t[j++] = (char) ('0' + ip % 10); ip /= 10; } while (ip);
	while (j) out[n++] = t[--j];
	if (prec > 0)
	{
		out[n++] = '.';
		for (int i = prec - 1; i >= 0; i--) { out[n + i] = (char) ('0' + fr % 10); fr /= 10; }
		n += prec;
	}
	out[n] = '\0';
}

// ---- dates --------------------------------------------------------------------------------------------
static bool leap (int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
static int  mdays (int y, int m) { static const int d[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 }; return m == 2 && leap (y) ? 29 : d[(m - 1) % 12]; }
static bool valid_date (int y, int m, int d) { return y >= 1 && y <= 9999 && m >= 1 && m <= 12 && d >= 1 && d <= mdays (y, m); }
static void today (int *y, int *m, int *d)
{
	int h, mi, s; *y = 2026; *m = 1; *d = 1;
	kapi_get_datetime (y, m, d, &h, &mi, &s);
	if (!valid_date (*y, *m, *d)) { *y = 2026; *m = 1; *d = 1; }
}
// A date as typed: D/M/Y (or with '.', '-' or spaces; Y in two digits: 70..99 -> 19xx, else 20xx),
// D/M (this year), DDMMYYYY / DDMMYY, or Y-M-D (ISO, as stored).
static bool parse_date (const char *s, int *py, int *pm, int *pd)
{
	int num[3], len[3], k = 0;
	while (*s == ' ') s++;
	while (*s && k < 3)
	{
		if (*s < '0' || *s > '9') return false;
		long v = 0; int n = 0;
		while (*s >= '0' && *s <= '9') { v = v * 10 + (*s - '0'); n++; s++; if (n > 8) return false; }
		num[k] = (int) v; len[k] = n; k++;
		while (*s == ' ') s++;
		if (*s == '/' || *s == '.' || *s == '-') { s++; while (*s == ' ') s++; }
		else if (!(*s >= '0' && *s <= '9')) break;
	}
	if (*s) return false;
	int y, m, d;
	if (k == 1 && (len[0] == 8 || len[0] == 6))
	{
		int v = num[0];
		if (len[0] == 8) { d = v / 1000000; m = v / 10000 % 100; y = v % 10000; }
		else { d = v / 10000; m = v / 100 % 100; y = v % 100; y += y < 70 ? 2000 : 1900; }
	}
	else if (k == 3 && len[0] == 4) { y = num[0]; m = num[1]; d = num[2]; }
	else if (k == 3 && len[0] <= 2 && len[1] <= 2 && (len[2] == 4 || len[2] <= 2))
	{
		d = num[0]; m = num[1]; y = num[2];
		if (len[2] <= 2) y += y < 70 ? 2000 : 1900;
	}
	else if (k == 2 && len[0] <= 2 && len[1] <= 2) { int ty, tm, td; today (&ty, &tm, &td); d = num[0]; m = num[1]; y = ty; }
	else return false;
	if (!valid_date (y, m, d)) return false;
	*py = y; *pm = m; *pd = d;
	return true;
}
static void iso_date (int y, int m, int d, char *o)
{
	o[0] = (char) ('0' + y / 1000 % 10); o[1] = (char) ('0' + y / 100 % 10); o[2] = (char) ('0' + y / 10 % 10);
	o[3] = (char) ('0' + y % 10); o[4] = '-'; o[5] = (char) ('0' + m / 10); o[6] = (char) ('0' + m % 10); o[7] = '-';
	o[8] = (char) ('0' + d / 10); o[9] = (char) ('0' + d % 10); o[10] = '\0';
}
// A stored date "YYYY-MM-DD" -> its numbers (false: not one).
static bool read_iso (const char *s, int *y, int *m, int *d)
{
	for (int i = 0; i < 10; i++) { if (i == 4 || i == 7) { if (s[i] != '-') return false; } else if (s[i] < '0' || s[i] > '9') return false; }
	if (s[10]) return false;
	*y = (s[0] - '0') * 1000 + (s[1] - '0') * 100 + (s[2] - '0') * 10 + (s[3] - '0');
	*m = (s[5] - '0') * 10 + (s[6] - '0'); *d = (s[8] - '0') * 10 + (s[9] - '0');
	return valid_date (*y, *m, *d);
}
static void show_date (int y, int m, int d, char *o)	// "29/09/2026"
{
	o[0] = (char) ('0' + d / 10); o[1] = (char) ('0' + d % 10); o[2] = '/'; o[3] = (char) ('0' + m / 10);
	o[4] = (char) ('0' + m % 10); o[5] = '/'; o[6] = (char) ('0' + y / 1000 % 10); o[7] = (char) ('0' + y / 100 % 10);
	o[8] = (char) ('0' + y / 10 % 10); o[9] = (char) ('0' + y % 10); o[10] = '\0';
}

// ---- colours ------------------------------------------------------------------------------------------
static int hexv (char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }
static const struct { const char *name; unsigned rgb; } COLOR_NAMES[] = {
	{ "black", 0x000000 }, { "white", 0xFFFFFF }, { "red", 0xFF0000 }, { "green", 0x008000 }, { "blue", 0x0000FF },
	{ "yellow", 0xFFFF00 }, { "orange", 0xFFA500 }, { "purple", 0x800080 }, { "pink", 0xFFC0CB }, { "brown", 0xA52A2A },
	{ "grey", 0x808080 }, { "gray", 0x808080 }, { "cyan", 0x00FFFF }, { "magenta", 0xFF00FF }, { "navy", 0x000080 },
	{ "teal", 0x008080 }, { "olive", 0x808000 }, { "maroon", 0x800000 }, { "silver", 0xC0C0C0 }, { "lime", 0x00FF00 },
	{ "gold", 0xFFD700 } };
// "#3366CC", "#36C", "3366CC", "0x3366CC" or a colour's name (red, navy, gold...).
static bool parse_color (const char *s, unsigned *rgb)
{
	char t[40]; trim_copy (t, s, sizeof t);
	const char *h = t;
	if (h[0] == '#') h++;
	else if (h[0] == '0' && (h[1] == 'x' || h[1] == 'X')) h += 2;
	int n = slen (h);
	bool hex = n == 6 || (n == 3 && h != t);
	for (int i = 0; i < n && hex; i++) if (hexv (h[i]) < 0) hex = false;
	if (hex)
	{
		if (n == 3) *rgb = (unsigned) (hexv (h[0]) * 17) << 16 | (unsigned) (hexv (h[1]) * 17) << 8 | (unsigned) (hexv (h[2]) * 17);
		else { unsigned v = 0; for (int i = 0; i < 6; i++) v = v << 4 | (unsigned) hexv (h[i]); *rgb = v; }
		return true;
	}
	for (unsigned i = 0; i < sizeof COLOR_NAMES / sizeof COLOR_NAMES[0]; i++)
		if (ci_eq (t, COLOR_NAMES[i].name)) { *rgb = COLOR_NAMES[i].rgb; return true; }
	return false;
}
static void fmt_color (unsigned rgb, char *o)
{
	static const char *HX = "0123456789ABCDEF";
	o[0] = '#';
	for (int i = 0; i < 6; i++) o[1 + i] = HX[(rgb >> (20 - 4 * i)) & 15];
	o[7] = '\0';
}

// ---- yes / no -----------------------------------------------------------------------------------------
// 1 yes, 0 no (or empty), -1 neither.
static int parse_bool (const char *s)
{
	char t[16]; trim_copy (t, s, sizeof t);
	static const char *const Y[] = { "yes", "y", "true", "1", "on", "oui", "x", "checked" };
	static const char *const N[] = { "", "no", "n", "false", "0", "off", "non" };
	for (unsigned i = 0; i < sizeof Y / sizeof Y[0]; i++) if (ci_eq (t, Y[i])) return 1;
	for (unsigned i = 0; i < sizeof N / sizeof N[0]; i++) if (ci_eq (t, N[i])) return 0;
	return -1;
}

// ---- the fields -----------------------------------------------------------------------------------------
struct Field
{
	char label[LABEL_MAX];			// the display name
	char column[COL_MAX];			// the column name (the file's header)
	int  type, prec;			// FT_*, a decimal's decimals
	int  nch; char *ch[MAXCH];		// a choice list's options
};
static void field_init (Field &f, const char *label, const char *column, int type)
{
	scpy (f.label, label, sizeof f.label); scpy (f.column, column, sizeof f.column);
	f.type = type; f.prec = 2; f.nch = 0;
	for (int i = 0; i < MAXCH; i++) f.ch[i] = s_empty;
}
static void field_free (Field &f) { for (int i = 0; i < f.nch; i++) sset (f.ch[i], ""); f.nch = 0; }
static int  choice_index (const Field &f, const char *v)
{ for (int i = 0; i < f.nch; i++) if (ci_eq (f.ch[i], v)) return i; return -1; }
static bool choice_add (Field &f, const char *v)
{
	if (!*v || choice_index (f, v) >= 0) return true;
	if (f.nch >= MAXCH) return false;
	f.ch[f.nch++] = sdup (v);
	return true;
}

// ---- the values -------------------------------------------------------------------------------------------
// The value v of field f as shown (and exported): a date DD/MM/YYYY; yes / no "Yes" / "No"
// (`boolText` false: "" for no); a multi-line text's lines joined by `joiner` if given; else as stored.
static void value_show (const Field &f, const char *v, char *out, int cap, bool boolText = true, const char *joiner = 0)
{
	if (f.type == FT_DATE)
	{
		int y, m, d;
		if (read_iso (v, &y, &m, &d) && cap > 10) { show_date (y, m, d, out); return; }
	}
	if (f.type == FT_BOOL) { scpy (out, *v ? "Yes" : boolText ? "No" : "", cap); return; }
	if (f.type == FT_MEMO && joiner)
	{
		int n = 0, jl = slen (joiner);
		for (const char *p = v; *p && n < cap - 1; p++)
		{
			if (*p == '\n') { for (int k = 0; k < jl && n < cap - 1; k++) out[n++] = joiner[k]; }
			else out[n++] = *p;
		}
		out[n] = '\0';
		return;
	}
	scpy (out, v, cap);
}

// A text (typed, read, converted) -> the value stored for field f in out; false if it is not a value of
// the type (a choice not in the list, when `anyChoice` is false). *exact: false when it was rounded.
static bool value_parse (const Field &f, const char *in, char *out, int cap, bool *exact = 0, bool anyChoice = false)
{
	if (exact) *exact = true;
	if (f.type == FT_TEXT || f.type == FT_MEMO)
	{
		int n = 0;
		for (const char *p = in; *p && n < cap - 1; p++)
		{
			char c = *p;
			if (c == '\r') continue;
			if (c == '\t' || (c == '\n' && f.type == FT_TEXT)) c = ' ';
			out[n++] = c;
		}
		out[n] = '\0';
		if (f.type == FT_TEXT) { while (n > 0 && out[n - 1] == ' ') out[--n] = '\0'; }
		return true;
	}
	char t[VAL_MAX]; trim_copy (t, in, sizeof t);
	if (!t[0] && f.type != FT_BOOL) { out[0] = '\0'; return true; }
	switch (f.type)
	{
	case FT_INT: case FT_DEC:
	{
		long long v; bool ex;
		int p = f.type == FT_INT ? 0 : f.prec;
		if (!parse_num (t, p, &v, &ex)) return false;
		if (exact) *exact = ex;
		char b[40]; fmt_num (v, p, b);
		scpy (out, b, cap);
		return true;
	}
	case FT_DATE:
	{
		int y, m, d;
		if (!parse_date (t, &y, &m, &d) || cap < 11) return false;
		iso_date (y, m, d, out);
		return true;
	}
	case FT_COLOR:
	{
		unsigned c;
		if (!parse_color (t, &c) || cap < 8) return false;
		fmt_color (c, out);
		return true;
	}
	case FT_BOOL:
	{
		int b = parse_bool (t);
		if (b < 0) return false;
		scpy (out, b ? "yes" : "", cap);
		return true;
	}
	case FT_CHOICE:
	{
		int i = choice_index (f, t);
		if (i >= 0) { scpy (out, f.ch[i], cap); return true; }
		if (!anyChoice) return false;
		scpy (out, t, cap);
		return true;
	}
	}
	scpy (out, t, cap);
	return true;
}

// The same as a new string (sfree it; text of any length), or 0 when it is not a value of the type.
static char *value_new (const Field &f, const char *in, bool *exact = 0, bool anyChoice = false)
{
	if (f.type == FT_TEXT || f.type == FT_MEMO)
	{
		int n = slen (in);
		char *b = new char[n + 1];
		value_parse (f, in, b, n + 1, exact, anyChoice);
		if (!b[0]) { delete [] b; return s_empty; }
		return b;
	}
	char b[VAL_MAX];
	if (!value_parse (f, in, b, sizeof b, exact, anyChoice)) return 0;
	return sdup (b);
}
static void value_set (char *&slot, char *v) { if (!v) return; sfree (slot); slot = v; }

// Two non-empty values of field f compared (their type's order; a value that is not one of its type
// after the others).
static int value_cmp (const Field &f, const char *a, const char *b)
{
	switch (f.type)
	{
	case FT_INT: case FT_DEC:
	{
		long long x = 0, y = 0; bool e;
		int p = f.type == FT_INT ? 0 : f.prec;
		bool pa = parse_num (a, p, &x, &e), pb = parse_num (b, p, &y, &e);
		if (pa && pb) return x < y ? -1 : x > y ? 1 : 0;
		if (pa != pb) return pa ? -1 : 1;
		return ci_cmp (a, b);
	}
	case FT_DATE: case FT_COLOR:
		for (; *a && *a == *b; a++, b++) {}
		return (unsigned char) *a - (unsigned char) *b;
	case FT_BOOL: return (*a ? 1 : 0) - (*b ? 1 : 0);
	case FT_CHOICE:
	{
		int ia = choice_index (f, a), ib = choice_index (f, b);
		if (ia < 0) ia = MAXCH;
		if (ib < 0) ib = MAXCH;
		if (ia != ib) return ia - ib;
		return ci_cmp (a, b);
	}
	}
	return ci_cmp (a, b);
}

// ---- column names ------------------------------------------------------------------------------------------
// A letter, digit or '_' / '-' (a column name's characters).
static bool col_char (char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-'; }
// A column name from a display name: "Date of birth" -> "date_of_birth" (accents off, lower case).
static void make_column (const char *label, char *out, int cap)
{
	int n = 0; bool gap = false;
	for (const char *p = label; *p && n < cap - 1; p++)
	{
		char c = (char) fold ((unsigned char) *p);
		if (col_char (c)) { if (gap && n > 0 && n < cap - 2) out[n++] = '_'; out[n++] = c; gap = false; }
		else gap = true;
	}
	out[n] = '\0';
	if (!n) scpy (out, "field", cap);
}

// ---- the document ---------------------------------------------------------------------------------------
struct Doc
{
	char title[TITLE_MAX], info[INFO_MAX];	// the form's title, its description
	Field f[MAXF]; int nf;
	char ***r; int nr, rcap;		// the records: r[i][k] = field k's value (never 0)
	int  sort; bool sortDesc;		// the views' order: by a field (-1: the file's), descending
};

static char **rec_new (int nf)
{
	char **v = new char *[nf > 0 ? nf : 1];
	for (int k = 0; k < (nf > 0 ? nf : 1); k++) v[k] = s_empty;
	return v;
}
static void rec_free (char **v, int nf) { if (!v) return; for (int k = 0; k < nf; k++) sfree (v[k]); delete [] v; }
static bool rec_empty (const Doc &d, int i) { for (int k = 0; k < d.nf; k++) if (d.r[i][k][0]) return false; return true; }

static void doc_init (Doc &d)
{
	d.title[0] = d.info[0] = '\0'; d.nf = 0; d.r = 0; d.nr = d.rcap = 0; d.sort = -1; d.sortDesc = false;
	for (int i = 0; i < MAXF; i++) field_init (d.f[i], "", "", FT_TEXT);
}
static void doc_clear (Doc &d)
{
	for (int i = 0; i < d.nr; i++) rec_free (d.r[i], d.nf);
	delete [] d.r;
	for (int i = 0; i < d.nf; i++) field_free (d.f[i]);
	doc_init (d);
}
// A new record (empty values) at index `at` (-1: the end) -> its index.
static int doc_add_record (Doc &d, int at)
{
	if (d.nr == d.rcap)
	{
		int nc = d.rcap ? d.rcap * 2 : 64;
		char ***n = new char **[nc];
		for (int i = 0; i < d.nr; i++) n[i] = d.r[i];
		delete [] d.r;
		d.r = n; d.rcap = nc;
	}
	if (at < 0 || at > d.nr) at = d.nr;
	for (int i = d.nr; i > at; i--) d.r[i] = d.r[i - 1];
	d.r[at] = rec_new (d.nf);
	d.nr++;
	return at;
}
static void doc_del_record (Doc &d, int i)
{
	if (i < 0 || i >= d.nr) return;
	rec_free (d.r[i], d.nf);
	for (int k = i; k < d.nr - 1; k++) d.r[k] = d.r[k + 1];
	d.nr--;
}
// The records laid out for new fields: map[k] = the old index of new field k, or -1 (a new, empty one).
static void doc_remap (Doc &d, const int *map, int nf)
{
	for (int i = 0; i < d.nr; i++)
	{
		char **o = d.r[i], **n = rec_new (nf);
		bool used[MAXF] = { false };
		for (int k = 0; k < nf; k++) if (map[k] >= 0 && map[k] < d.nf) { n[k] = o[map[k]]; used[map[k]] = true; }
		for (int k = 0; k < d.nf; k++) if (!used[k]) sfree (o[k]);
		delete [] o;
		d.r[i] = n;
	}
}
static int find_column (const Doc &d, const char *col, int except = -1)
{ for (int k = 0; k < d.nf; k++) if (k != except && ci_eq (d.f[k].column, col)) return k; return -1; }
// Field `self`'s column name made valid and unique: "name" -> "name_2" when another field has it.
static void unique_column (const Doc &d, int self, char *col, int cap)
{
	char base[COL_MAX]; int n = 0;
	for (const char *p = col; *p && n < (int) sizeof base - 1; p++) if (col_char (*p)) base[n++] = *p;
	base[n] = '\0';
	if (!n) scpy (base, "field", sizeof base);
	scpy (col, base, cap);
	for (int k = 2; find_column (d, col, self) >= 0 && k < 1000; k++)
	{
		char num[8]; itoa10 (k, num + 1); num[0] = '_';
		int room = cap - 1 - slen (num);
		scpy (col, base, room + 1 < cap ? room + 1 : cap);
		scat (col, num, cap);
	}
}
static void doc_fix_columns (Doc &d) { for (int k = 0; k < d.nf; k++) unique_column (d, k, d.f[k].column, sizeof d.f[k].column); }

// A field inserted at `at` (its values empty in every record) -> its index.
static int doc_insert_field (Doc &d, int at, const Field &src)
{
	if (d.nf >= MAXF) return -1;
	if (at < 0 || at > d.nf) at = d.nf;
	int map[MAXF];
	for (int k = 0, o = 0; k <= d.nf; k++) map[k] = k == at ? -1 : o++;
	doc_remap (d, map, d.nf + 1);
	for (int k = d.nf; k > at; k--) d.f[k] = d.f[k - 1];
	d.f[at] = src;
	d.nf++;
	if (d.sort >= at) d.sort++;
	unique_column (d, at, d.f[at].column, sizeof d.f[at].column);
	return at;
}
static void doc_remove_field (Doc &d, int at)
{
	if (at < 0 || at >= d.nf) return;
	int map[MAXF];
	for (int k = 0, o = 0; k < d.nf - 1; k++, o++) { if (o == at) o++; map[k] = o; }
	doc_remap (d, map, d.nf - 1);
	field_free (d.f[at]);
	for (int k = at; k < d.nf - 1; k++) d.f[k] = d.f[k + 1];
	d.nf--;
	field_init (d.f[d.nf], "", "", FT_TEXT);
	if (d.sort == at) d.sort = -1; else if (d.sort > at) d.sort--;
}
// Field i and its neighbour (dir -1: the one before, +1: after) swapped.
static bool doc_move_field (Doc &d, int i, int dir)
{
	int j = i + dir;
	if (i < 0 || i >= d.nf || j < 0 || j >= d.nf) return false;
	int map[MAXF];
	for (int k = 0; k < d.nf; k++) map[k] = k;
	map[i] = j; map[j] = i;
	doc_remap (d, map, d.nf);
	Field t = d.f[i]; d.f[i] = d.f[j]; d.f[j] = t;
	if (d.sort == i) d.sort = j; else if (d.sort == j) d.sort = i;
	return true;
}

// A new form: a title and its first fields.
static void doc_new (Doc &d)
{
	doc_clear (d);
	scpy (d.title, "Untitled", sizeof d.title);
	Field f;
	field_init (f, "Name", "name", FT_TEXT); doc_insert_field (d, -1, f);
	field_init (f, "Notes", "notes", FT_MEMO); doc_insert_field (d, -1, f);
}

// ---- changing a field's type ---------------------------------------------------------------------------------
// What converting field k to `type` (with `prec` decimals) would do: how many values would be lost
// (not a value of the new type: emptied) and rounded. A choice list keeps every value (the options
// missing are added).
static void convert_count (const Doc &d, int k, int type, int prec, int *lost, int *rounded)
{
	*lost = *rounded = 0;
	Field nf = d.f[k]; nf.type = type; nf.prec = prec;
	char shown[VAL_MAX], v[VAL_MAX];
	for (int i = 0; i < d.nr; i++)
	{
		const char *o = d.r[i][k];
		if (!o[0]) continue;
		value_show (d.f[k], o, shown, sizeof shown);
		bool ex;
		if (!value_parse (nf, shown, v, sizeof v, &ex, true)) (*lost)++;
		else if (!ex) (*rounded)++;
	}
}
// Field k converted to `type` / `prec`: each value through its text as shown (a date 29/09/2026, a
// yes "Yes"...): what reads as the new type stays, the rest is emptied; to a choice list, the values
// become its options.
static void convert_field (Doc &d, int k, int type, int prec)
{
	Field &f = d.f[k];
	Field old = f;
	int oldType = f.type;
	f.type = type; f.prec = iclamp (prec, 0, MAXDEC);
	char shown[VAL_MAX];
	for (int i = 0; i < d.nr; i++)
	{
		char *&o = d.r[i][k];
		if (!o[0]) continue;
		value_show (old, o, shown, sizeof shown, true, oldType == FT_MEMO && type == FT_TEXT ? " " : 0);
		if (type == FT_CHOICE && oldType != FT_CHOICE) { char t[VAL_MAX]; trim_copy (t, shown, sizeof t); choice_add (f, t); }
		char *nv = value_new (f, shown, 0, true);
		value_set (o, nv ? nv : sdup (""));
	}
	if (type != FT_CHOICE && oldType == FT_CHOICE) {}		// (the options kept: back to a choice list, they return)
}

// ---- the file -------------------------------------------------------------------------------------------------
// (the format: main.cpp's header)
static void put_kv (Out &o, const char *k, const char *v) { o.puts (k); o.puts (" = "); o.puts (v); o.put ('\n'); }
static void put_cell (Out &o, const char *v, bool first)
{
	if (first && *v == '[') o.put ('\\');			// (a record's line never starts a section)
	for (const char *p = v; *p; p++)
	{
		char c = *p;
		if (c == '\\') o.puts ("\\\\");
		else if (c == '\t') o.puts ("\\t");
		else if (c == '\n') o.puts ("\\n");
		else if (c == '\r') o.puts ("\\r");
		else o.put (c);
	}
}
static void doc_write (const Doc &d, Out &o)
{
	o.puts ("# Onyx Cardfile -- a form and its records (open it with Cardfile)\n");
	o.puts ("[form]\n");
	put_kv (o, "version", "1");
	put_kv (o, "title", d.title);
	if (d.info[0]) put_kv (o, "description", d.info);
	if (d.sort >= 0 && d.sort < d.nf) { put_kv (o, "sort", d.f[d.sort].column); if (d.sortDesc) put_kv (o, "order", "descending"); }
	for (int k = 0; k < d.nf; k++)
	{
		const Field &f = d.f[k];
		o.puts ("\n[field]\n");
		put_kv (o, "column", f.column);
		put_kv (o, "label", f.label);
		put_kv (o, "type", TYPE_KEY[f.type]);
		if (f.type == FT_DEC) { char b[4]; itoa10 (f.prec, b); put_kv (o, "decimals", b); }
		if (f.type == FT_CHOICE) for (int i = 0; i < f.nch; i++) put_kv (o, "choice", f.ch[i]);
	}
	o.puts ("\n[records]\n");
	for (int k = 0; k < d.nf; k++) { if (k) o.put ('\t'); o.puts (d.f[k].column); }
	o.put ('\n');
	for (int i = 0; i < d.nr; i++)
	{
		for (int k = 0; k < d.nf; k++) { if (k) o.put ('\t'); put_cell (o, d.r[i][k], k == 0); }
		o.put ('\n');
	}
}

// One tab-separated cell of a record's line from p (up to the tab or the line's end) -> out; returns
// where the next cell starts (0: that was the last).
static const char *read_cell (const char *p, const char *end, char *out, int cap)
{
	int n = 0;
	while (p < end && *p != '\t')
	{
		char c = *p++;
		if (c == '\\' && p < end)
		{
			char e = *p++;
			c = e == 't' ? '\t' : e == 'n' ? '\n' : e == 'r' ? '\r' : e;
		}
		if (c == '\r' && p == end) break;
		if (n < cap - 1) out[n++] = c;
	}
	out[n] = '\0';
	return p < end ? p + 1 : 0;
}

// A document from a file's text; false (and why) when the text is not one.
static bool doc_read (Doc &d, const char *text, int len, const char **why)
{
	doc_clear (d);
	enum { S_NONE, S_FORM, S_FIELD, S_RECORDS, S_OTHER };
	int sec = S_NONE; bool any = false;
	int *rl = 0, nrl = 0, crl = 0;				// the records' lines: start, end
	char sortCol[COL_MAX] = ""; bool desc = false;
	const char *p = text, *end = text + len;
	Field *cur = 0;
	while (p < end)
	{
		const char *ls = p;
		while (p < end && *p != '\n') p++;
		const char *le = p;
		if (p < end) p++;
		const char *e = le;
		if (e > ls && e[-1] == '\r') e--;
		if (*ls == '[')						// a section
		{
			char name[24]; int n = 0;
			for (const char *q = ls + 1; q < e && *q != ']' && n < 23; q++) name[n++] = (char) fold ((unsigned char) *q);
			name[n] = '\0';
			if (seq (name, "form") || seq (name, "cardfile")) sec = S_FORM;
			else if (seq (name, "field"))
			{
				sec = S_FIELD; cur = 0;
				if (d.nf < MAXF) { cur = &d.f[d.nf++]; field_init (*cur, "", "", FT_TEXT); cur->prec = -1; }
			}
			else if (seq (name, "records")) sec = S_RECORDS;
			else sec = S_OTHER;
			if (sec != S_OTHER) any = true;			// (another INI file is not a form)
			continue;
		}
		if (sec == S_RECORDS)
		{
			if (e == ls) continue;				// (a blank line)
			if (nrl + 2 > crl)
			{
				int nc = crl ? crl * 2 : 256;
				int *n = new int[nc];
				for (int i = 0; i < nrl; i++) n[i] = rl[i];
				delete [] rl; rl = n; crl = nc;
			}
			rl[nrl++] = (int) (ls - text); rl[nrl++] = (int) (e - text);
			continue;
		}
		const char *q = ls;
		while (q < e && (*q == ' ' || *q == '\t')) q++;
		if (q == e || *q == '#' || *q == ';') continue;
		// key = value
		char key[24]; int kn = 0;
		while (q < e && *q != '=' && kn < 23) key[kn++] = (char) fold ((unsigned char) *q++);
		while (kn > 0 && (key[kn - 1] == ' ' || key[kn - 1] == '\t')) kn--;
		key[kn] = '\0';
		while (q < e && *q != '=') q++;
		if (q == e) continue;
		q++;
		char val[VAL_MAX]; int vn = 0;
		while (q < e && vn < VAL_MAX - 1) val[vn++] = *q++;
		val[vn] = '\0';
		char v[VAL_MAX]; trim_copy (v, val, sizeof v);
		if (sec == S_FORM)
		{
			if (seq (key, "title")) scpy (d.title, v, sizeof d.title);
			else if (seq (key, "description")) scpy (d.info, v, sizeof d.info);
			else if (seq (key, "sort")) scpy (sortCol, v, sizeof sortCol);
			else if (seq (key, "order")) desc = ci_eq (v, "descending") || ci_eq (v, "desc");
		}
		else if (sec == S_FIELD && cur)
		{
			if (seq (key, "column")) scpy (cur->column, v, sizeof cur->column);
			else if (seq (key, "label") || seq (key, "name")) scpy (cur->label, v, sizeof cur->label);
			else if (seq (key, "type"))
			{
				cur->type = FT_TEXT;
				for (int t = 0; t < FT_COUNT; t++) if (ci_eq (v, TYPE_KEY[t])) cur->type = t;
				if (ci_eq (v, "color")) cur->type = FT_COLOR;
				if (ci_eq (v, "memo") || ci_eq (v, "multi-line")) cur->type = FT_MEMO;
				if (ci_eq (v, "number")) cur->type = FT_DEC;
				if (ci_eq (v, "bool") || ci_eq (v, "boolean") || ci_eq (v, "checkbox")) cur->type = FT_BOOL;
				if (ci_eq (v, "list")) cur->type = FT_CHOICE;
			}
			else if (seq (key, "decimals") || seq (key, "precision"))
			{
				int n = 0; for (const char *c = v; *c >= '0' && *c <= '9'; c++) n = n * 10 + (*c - '0');
				cur->prec = iclamp (n, 0, MAXDEC);
			}
			else if (seq (key, "choice")) choice_add (*cur, v);
			else if (seq (key, "choices"))			// (a convenience by hand: "a | b | c")
			{
				const char *c = v;
				while (*c)
				{
					char one[VAL_MAX]; int n = 0;
					while (*c && *c != '|') { if (n < VAL_MAX - 1) one[n++] = *c; c++; }
					one[n] = '\0';
					if (*c) c++;
					char t[VAL_MAX]; trim_copy (t, one, sizeof t);
					choice_add (*cur, t);
				}
			}
		}
	}
	if (!any)
	{
		delete [] rl;
		doc_clear (d);
		if (why) *why = "This file is not a Cardfile document.";
		return false;
	}
	// the fields: a label from the column or the other way round, the columns valid and unique
	for (int k = 0; k < d.nf; k++)
	{
		Field &f = d.f[k];
		if (f.prec < 0) f.prec = f.type == FT_DEC ? 2 : 2;
		if (!f.column[0]) make_column (f.label[0] ? f.label : "field", f.column, sizeof f.column);
		if (!f.label[0]) scpy (f.label, f.column, sizeof f.label);
	}
	doc_fix_columns (d);
	// the records: the header maps the cells to the fields (by column name); none: in the fields' order
	int first = 0, map[MAXF * 2], nmap = 0;
	if (nrl > 0)
	{
		const char *ls = text + rl[0], *le = text + rl[1];
		int matched = 0, cells = 0;
		bool makeFields = d.nf == 0;
		char cell[VAL_MAX];
		for (const char *c = ls; c && nmap < MAXF * 2; )
		{
			c = read_cell (c, le, cell, sizeof cell);
			char t[COL_MAX]; trim_copy (t, cell, sizeof t);
			int k = find_column (d, t);
			if (k >= 0) matched++;
			else if (makeFields && t[0])			// (no [field]: the header makes text fields)
			{
				Field f; field_init (f, t, t, FT_TEXT);
				unique_column (d, -1, f.column, sizeof f.column);
				k = d.nf < MAXF ? d.nf : -1;
				if (k >= 0) { d.f[d.nf++] = f; matched++; }
			}
			map[nmap++] = k; cells++;
		}
		if (matched) first = 1;
		else { nmap = 0; for (int k = 0; k < d.nf; k++) map[nmap++] = k; }
	}
	int longest = 0;
	for (int i = first; i < nrl / 2; i++) longest = imax (longest, rl[2 * i + 1] - rl[2 * i]);
	char *cell = new char[longest + 1];
	for (int i = first; i < nrl / 2; i++)
	{
		const char *ls = text + rl[2 * i], *le = text + rl[2 * i + 1];
		int r = doc_add_record (d, -1);
		int c = 0;
		for (const char *q = ls; q && c < nmap; c++)
		{
			q = read_cell (q, le, cell, longest + 1);
			int k = map[c];
			if (k < 0 || k >= d.nf) continue;
			// a value that is not one of its type is kept as it is (the form asks for a valid one)
			char *v = value_new (d.f[k], cell, 0, true);
			value_set (d.r[r][k], v ? v : sdup (cell));
		}
	}
	delete [] cell;
	delete [] rl;
	d.sort = sortCol[0] ? find_column (d, sortCol) : -1;
	d.sortDesc = desc && d.sort >= 0;
	if (!d.title[0]) scpy (d.title, "Untitled", sizeof d.title);
	return true;
}

// ---- CSV ----------------------------------------------------------------------------------------------------------
static void csv_cell (Out &o, const char *s)
{
	bool q = false;
	for (const char *p = s; *p; p++) if (*p == ',' || *p == '"' || *p == '\n' || *p == '\r' || *p == ';') q = true;
	if (s[0] == ' ' || (s[0] && s[slen (s) - 1] == ' ')) q = true;
	if (!q) { o.puts (s); return; }
	o.put ('"');
	for (const char *p = s; *p; p++) { if (*p == '"') o.put ('"'); o.put (*p); }
	o.put ('"');
}
// The records ord[0..n) as CSV: the display names, then the values as shown.
static void csv_write (const Doc &d, const int *ord, int n, Out &o)
{
	for (int k = 0; k < d.nf; k++) { if (k) o.put (','); csv_cell (o, d.f[k].label); }
	o.puts ("\r\n");
	char v[VAL_MAX];
	for (int i = 0; i < n; i++)
	{
		for (int k = 0; k < d.nf; k++)
		{
			if (k) o.put (',');
			const char *s = d.r[ord[i]][k];
			if (d.f[k].type != FT_TEXT && d.f[k].type != FT_MEMO) { value_show (d.f[k], s, v, sizeof v); s = v; }
			csv_cell (o, s);
		}
		o.puts ("\r\n");
	}
}

// CSV read: rows of cells (',' ';' or tab between them -- the most frequent in the first line; quoted
// cells may hold them, line breaks and "" for a quote).
struct CsvTable
{
	char **cell; int *rowStart; int ncell, nrow, ccap, rcap;
	CsvTable () : cell (0), rowStart (0), ncell (0), nrow (0), ccap (0), rcap (0) {}
	~CsvTable () { for (int i = 0; i < ncell; i++) sfree (cell[i]); delete [] cell; delete [] rowStart; }
	void addCell (const char *s, int n)
	{
		if (ncell == ccap) { int nc = ccap ? ccap * 2 : 256; char **c = new char *[nc]; for (int i = 0; i < ncell; i++) c[i] = cell[i]; delete [] cell; cell = c; ccap = nc; }
		cell[ncell++] = sdupn (s, n);
	}
	void addRow ()
	{
		if (nrow + 1 >= rcap) { int nc = rcap ? rcap * 2 : 64; int *r = new int[nc]; for (int i = 0; i < nrow; i++) r[i] = rowStart[i]; delete [] rowStart; rowStart = r; rcap = nc; }
		rowStart[nrow++] = ncell;
	}
	int cells (int r) const { return (r + 1 < nrow ? rowStart[r + 1] : ncell) - rowStart[r]; }
	const char *at (int r, int c) const { return c < cells (r) ? cell[rowStart[r] + c] : ""; }
};
static void csv_parse (const char *t, int len, CsvTable &tab)
{
	int nc = 0, ns = 0, nt = 0; bool q = false;
	for (int i = 0; i < len && (q || t[i] != '\n'); i++)
	{
		if (t[i] == '"') q = !q;
		else if (!q) { if (t[i] == ',') nc++; else if (t[i] == ';') ns++; else if (t[i] == '\t') nt++; }
	}
	char sep = nt > nc && nt > ns ? '\t' : ns > nc ? ';' : ',';
	char *buf = new char[len + 1];
	int i = 0;
	while (i < len)
	{
		tab.addRow ();
		for (;;)						// the cells of a line
		{
			int n = 0;
			if (i < len && t[i] == '"')
			{
				i++;
				while (i < len)
				{
					if (t[i] == '"') { if (i + 1 < len && t[i + 1] == '"') { buf[n++] = '"'; i += 2; continue; } i++; break; }
					if (t[i] != '\r') buf[n++] = t[i];
					i++;
				}
				while (i < len && t[i] != sep && t[i] != '\n') i++;	// (after the closing quote)
			}
			else while (i < len && t[i] != sep && t[i] != '\n') { if (t[i] != '\r') buf[n++] = t[i]; i++; }
			tab.addCell (buf, n);
			if (i < len && t[i] == sep) { i++; continue; }
			if (i < len && t[i] == '\n') i++;
			break;
		}
	}
	delete [] buf;
}
// A whole number as a CSV holds one: digits, a '-' before them, no 0 before them ("007", "06 12..."
// -- codes, telephones -- stay text).
static bool csv_int (const char *s)
{
	if (*s == '-') s++;
	if (!*s || (s[0] == '0' && s[1])) return false;
	for (; *s; s++) if (*s < '0' || *s > '9') return false;
	return true;
}
// ... a decimal: digits, one '.' or ',', digits -> how many after it.
static int csv_dec (const char *s)
{
	if (*s == '-') s++;
	int a = 0, b = 0; bool pt = false;
	if (s[0] == '0' && s[1] >= '0' && s[1] <= '9') return -1;
	for (; *s; s++)
	{
		if (*s >= '0' && *s <= '9') { if (pt) b++; else a++; }
		else if ((*s == '.' || *s == ',') && !pt) pt = true;
		else return -1;
	}
	return a && pt && b ? b : -1;
}
// A document from a CSV's text: its first line names the fields; each column's type guessed from its
// values (whole numbers, decimals, dates, yes / no words, colours, several lines, a few values
// repeated: a choice list; else text).
static bool csv_read (Doc &d, const char *text, int len, const char *title)
{
	CsvTable tab;
	csv_parse (text, len, tab);
	if (tab.nrow < 1 || tab.cells (0) < 1) return false;
	doc_clear (d);
	scpy (d.title, title, sizeof d.title);
	int nf = imin (tab.cells (0), MAXF);
	for (int k = 0; k < nf; k++)
	{
		char lab[LABEL_MAX]; trim_copy (lab, tab.at (0, k), sizeof lab);
		if (!lab[0]) { scpy (lab, "Field ", sizeof lab); scat_num (lab, k + 1, sizeof lab); }
		// the type: what every non-empty value of the column reads as
		bool isInt = true, isDec = true, isDate = true, isBool = true, isColor = true, multi = false, any = false;
		int prec = 0, distinct = 0, nonEmpty = 0, maxLen = 0;
		const char *seen[12]; int nseen = 0;
		for (int r = 1; r < tab.nrow; r++)
		{
			char v[VAL_MAX]; trim_copy (v, tab.at (r, k), sizeof v);
			if (!v[0]) continue;
			any = true; nonEmpty++;
			int l = slen (v); if (l > maxLen) maxLen = l;
			for (const char *c = tab.at (r, k); *c; c++) if (*c == '\n') multi = true;
			if (!csv_int (v)) isInt = false;
			int dp = csv_dec (v);
			if (dp < 0 && !csv_int (v)) isDec = false; else if (dp > prec) prec = dp;
			int y, m, dd;
			bool sep = false; for (const char *c = v; *c; c++) if (*c == '/' || *c == '-' || *c == '.') sep = true;
			if (!sep || !parse_date (v, &y, &m, &dd)) isDate = false;
			static const char *const W[] = { "yes", "no", "true", "false", "oui", "non" };
			bool w = false; for (unsigned i = 0; i < 6; i++) if (ci_eq (v, W[i])) w = true;
			if (!w) isBool = false;
			unsigned c; if (v[0] != '#' || !parse_color (v, &c)) isColor = false;
			if (distinct <= 10)
			{
				bool found = false;
				for (int s = 0; s < nseen; s++) if (ci_eq (seen[s], tab.at (r, k))) found = true;
				if (!found) { distinct++; if (nseen < 12) seen[nseen++] = tab.at (r, k); }
			}
		}
		int type = !any ? FT_TEXT : multi ? FT_MEMO : isInt ? FT_INT : isDec ? FT_DEC : isDate ? FT_DATE : isBool ? FT_BOOL
			 : isColor ? FT_COLOR : distinct >= 2 && distinct <= 10 && nonEmpty >= 3 * distinct && maxLen <= 30 ? FT_CHOICE : FT_TEXT;
		Field f; char col[COL_MAX];
		make_column (lab, col, sizeof col);
		field_init (f, lab, col, type);
		f.prec = iclamp (prec, 1, MAXDEC);
		doc_insert_field (d, -1, f);
	}
	for (int r = 1; r < tab.nrow; r++)
	{
		bool blank = true;
		for (int k = 0; k < nf; k++) if (tab.at (r, k)[0]) blank = false;
		if (blank) continue;
		int i = doc_add_record (d, -1);
		for (int k = 0; k < nf; k++)
		{
			Field &f = d.f[k];
			if (f.type == FT_CHOICE) { char t[VAL_MAX]; trim_copy (t, tab.at (r, k), sizeof t); choice_add (f, t); }
			char *nv = value_new (f, tab.at (r, k), 0, true);
			value_set (d.r[i][k], nv ? nv : sdup (tab.at (r, k)));
		}
	}
	return true;
}

// ---- the order shown -------------------------------------------------------------------------------------------
// Does record i hold every word of the search in one of its values as shown (case, accents ignored)?
static bool rec_matches (const Doc &d, int i, const char *search)
{
	char v[VAL_MAX];
	const char *w = search;
	for (;;)
	{
		while (*w == ' ') w++;
		if (!*w) return true;
		int n = 0; while (w[n] && w[n] != ' ') n++;
		bool found = false;
		for (int k = 0; k < d.nf && !found; k++)
		{
			const char *s = d.r[i][k];
			if (d.f[k].type == FT_DATE || d.f[k].type == FT_BOOL) { value_show (d.f[k], s, v, sizeof v); s = v; }
			found = ci_has (s, w, n);
		}
		if (!found) return false;
		w += n;
	}
}
// Records a and b in the order of field `col` (empty values last whichever way; else the file's order).
static int rec_cmp (const Doc &d, int col, bool desc, int a, int b)
{
	const Field &f = d.f[col];
	const char *x = d.r[a][col], *y = d.r[b][col];
	bool ex = !x[0] && f.type != FT_BOOL, ey = !y[0] && f.type != FT_BOOL;
	if (ex || ey) { if (ex && ey) return a - b; return ex ? 1 : -1; }
	int c = value_cmp (f, x, y);
	if (desc) c = -c;
	return c ? c : a - b;
}
static void merge_sort (const Doc &d, int col, bool desc, int *a, int *tmp, int n)
{
	if (n < 2) return;
	int h = n / 2;
	merge_sort (d, col, desc, a, tmp, h);
	merge_sort (d, col, desc, a + h, tmp, n - h);
	int i = 0, j = h, k = 0;
	while (i < h && j < n) tmp[k++] = rec_cmp (d, col, desc, a[i], a[j]) <= 0 ? a[i++] : a[j++];
	while (i < h) tmp[k++] = a[i++];
	while (j < n) tmp[k++] = a[j++];
	for (int q = 0; q < n; q++) a[q] = tmp[q];
}
// The records shown, in order, into ord (room for d.nr): those matching the search, sorted by
// d.sort; `pin` (a new record) is always there. Returns how many.
static int build_order (const Doc &d, const char *search, int pin, int *ord)
{
	int n = 0;
	for (int i = 0; i < d.nr; i++) if (i == pin || !search[0] || rec_matches (d, i, search)) ord[n++] = i;
	if (d.sort >= 0 && d.sort < d.nf && n > 1)
	{
		int *tmp = new int[n];
		merge_sort (d, d.sort, d.sortDesc, ord, tmp, n);
		delete [] tmp;
	}
	return n;
}

} // namespace cf

#endif
