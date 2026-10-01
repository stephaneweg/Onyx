//
// core.h -- Ledger's basics, freestanding (no libc) and integer only, as Cardfile's (whose string, number
// and date helpers this builds on: Apps/cardfile/model.h, namespace cf):
//   * money: a long long of CENTS, never a floating point; shown the Belgian way "1.234,56", written
//     in the file and in the XML as "1234.56"; read as typed ("1234,56", "1.234,56", "1234.56", "-12");
//     a VAT rate in hundredths of a percent (2100 = 21 %), a tax rounded half away from zero;
//   * dates: an int yyyymmdd (20260929; 0 = none) -- compares as a number; shown 29/09/2026, written
//     2026-09-29; days added through a day count (Howard Hinnant's civil-from-days);
//   * the Belgian checks: a VAT number (BE + 10 digits, the last two 97 minus the first eight modulo
//     97), an IBAN (ISO 13616, modulo 97), a structured communication (+++123/4567/89002+++: ten
//     digits and their modulo 97, 97 for 0); the EU's countries (their VAT prefixes);
//   * text for other programs: Latin-1 (Onyx's) to UTF-8, XML's escapes, CSV's quotes;
//   * a merge sort of indexes with a comparison and its context.
//
#ifndef _ledger_core_h
#define _ledger_core_h

#include "Apps/cardfile/model.h"
#include "wtk/lang.h"		// TR (): the words in the language chosen

namespace lg {

using namespace cf;

typedef long long money;			// cents

static long long lmin (long long a, long long b) { return a < b ? a : b; }
static long long lmax (long long a, long long b) { return a > b ? a : b; }
static long long labs_ (long long a) { return a < 0 ? -a : a; }

// ---- money ---------------------------------------------------------------------------------------------------
// "1.234,56" (grouped by three with '.', ',' before the cents); "-1.234,56".
static void fmt_money (money v, char *out, bool group = true)
{
	int n = 0;
	unsigned long long u = v < 0 ? (unsigned long long) -v : (unsigned long long) v;
	if (v < 0) out[n++] = '-';
	unsigned long long ip = u / 100; int fr = (int) (u % 100);
	char t[32]; int j = 0, k = 0;
	do { if (group && k && k % 3 == 0) t[j++] = '.'; t[j++] = (char) ('0' + ip % 10); ip /= 10; k++; } while (ip);
	while (j) out[n++] = t[--j];
	out[n++] = ','; out[n++] = (char) ('0' + fr / 10); out[n++] = (char) ('0' + fr % 10);
	out[n] = '\0';
}
// "" for zero, else as fmt_money (a ledger's empty debit or credit).
static void fmt_money0 (money v, char *out) { if (!v) out[0] = '\0'; else fmt_money (v, out); }
// "1234.56": the file's, the XML's.
static void fmt_plain (money v, char *out) { fmt_num (v, 2, out); }
// As typed -> cents; false when it is not an amount.
static bool parse_money (const char *s, money *out)
{
	char t[48]; trim_copy (t, s, sizeof t);
	int n = slen (t);
	if (n > 0 && ((unsigned char) t[n - 1] == 0x80 || t[n - 1] == 'E' || t[n - 1] == 'e')) t[--n] = '\0';	// (a euro sign)
	if (!t[0]) { *out = 0; return true; }
	return parse_num (t, 2, out, 0);
}
// The tax on a base at a rate (hundredths of a percent), rounded half away from zero.
static money tax_of (money base, int rate)
{
	long long p = base * rate;				// cents x 1/10000
	long long q = labs_ (p) / 10000, r = labs_ (p) % 10000;
	if (r >= 5000) q++;
	return p < 0 ? -q : q;
}
// A share of an amount (percent, 0..100), rounded half away from zero.
static money share_of (money v, int pct)
{
	long long p = v * pct, q = labs_ (p) / 100, r = labs_ (p) % 100;
	if (r >= 50) q++;
	return p < 0 ? -q : q;
}
// A rate "21 %" / "6 %" / "12,5 %".
static void fmt_rate (int rate, char *out)
{
	char t[16]; fmt_num (rate, 2, t);
	int n = slen (t);
	while (n > 0 && t[n - 1] == '0') n--;
	if (n > 0 && t[n - 1] == '.') n--;
	t[n] = '\0';
	for (int i = 0; t[i]; i++) if (t[i] == '.') t[i] = ',';
	scpy (out, t, 16); scat (out, " %", 16);
}

// ---- dates -------------------------------------------------------------------------------------------------------
static inline int ymd (int y, int m, int d) { return y * 10000 + m * 100 + d; }
static inline int y_of (int v) { return v / 10000; }
static inline int m_of (int v) { return v / 100 % 100; }
static inline int d_of (int v) { return v % 100; }
static bool date_ok (int v) { return v > 0 && valid_date (y_of (v), m_of (v), d_of (v)); }
// The days since 1970-01-01 (negative before) and back.
static long days_of (int v)
{
	int y = y_of (v), m = m_of (v), d = d_of (v);
	y -= m <= 2;
	long era = (y >= 0 ? y : y - 399) / 400;
	int yoe = (int) (y - era * 400);
	int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	return era * 146097 + doe - 719468;
}
static int date_of (long z)
{
	z += 719468;
	long era = (z >= 0 ? z : z - 146096) / 146097;
	int doe = (int) (z - era * 146097);
	int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	int y = (int) (yoe + era * 400);
	int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	int mp = (5 * doy + 2) / 153;
	int d = doy - (153 * mp + 2) / 5 + 1;
	int m = mp < 10 ? mp + 3 : mp - 9;
	return ymd (y + (m <= 2), m, d);
}
static int date_add (int v, int days) { return date_of (days_of (v) + days); }
static int days_between (int a, int b) { return (int) (days_of (b) - days_of (a)); }
static int month_end (int y, int m) { return ymd (y, m, mdays (y, m)); }
static int add_months (int v, int k)				// (the day kept, or the month's last)
{
	int y = y_of (v), m = m_of (v) - 1 + k;
	y += m >= 0 ? m / 12 : -((11 - m) / 12);
	m = ((m % 12) + 12) % 12 + 1;
	return ymd (y, m, imin (d_of (v), mdays (y, m)));
}
static int today_ymd () { int y, m, d; today (&y, &m, &d); return ymd (y, m, d); }
// "29/09/2026" ("" for none); "2026-09-29".
static void date_show (int v, char *o) { if (!date_ok (v)) { o[0] = '\0'; return; } show_date (y_of (v), m_of (v), d_of (v), o); }
static void date_iso (int v, char *o) { if (!date_ok (v)) { o[0] = '\0'; return; } iso_date (y_of (v), m_of (v), d_of (v), o); }
// As typed (29/9/26, 29.09.2026, 290926, 2026-09-29...) or as stored -> yyyymmdd, 0 when not one.
static int date_parse (const char *s)
{
	int y, m, d;
	char t[24]; trim_copy (t, s, sizeof t);
	if (!t[0]) return 0;
	if (!parse_date (t, &y, &m, &d)) return 0;
	return ymd (y, m, d);
}
static const char *const MONTH_NAME[12] = { "January", "February", "March", "April", "May", "June", "July", "August",
					   "September", "October", "November", "December" };
static const char *const MONTH_SHORT[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

// ---- Belgian and European checks -------------------------------------------------------------------------------
// The EU's member states by their VAT prefix (Greece: EL; Northern Ireland's goods: XI).
static const char *const EU_PREFIX[] = { "AT", "BE", "BG", "CY", "CZ", "DE", "DK", "EE", "EL", "ES", "FI", "FR", "HR", "HU",
					 "IE", "IT", "LT", "LU", "LV", "MT", "NL", "PL", "PT", "RO", "SE", "SI", "SK", "XI", 0 };
static bool eu_prefix (const char *cc)
{
	for (int i = 0; EU_PREFIX[i]; i++) if (cc[0] == EU_PREFIX[i][0] && cc[1] == EU_PREFIX[i][1]) return true;
	return false;
}
// A country's code (ISO 3166) as its VAT prefix (GR -> EL).
static void vat_prefix_of (const char *country, char *out) { if (country[0] == 'G' && country[1] == 'R') scpy (out, "EL", 3); else scpy (out, country, 3); }
static char up (char c) { return c >= 'a' && c <= 'z' ? (char) (c - 32) : c; }
static bool digit (char c) { return c >= '0' && c <= '9'; }
static bool alnum (char c) { return digit (c) || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
// A text ending in a number, that number one more ("Extrait 042" -> "Extrait 043") -> out ("" when it has none).
static void number_next (const char *s, char *out, int cap)
{
	out[0] = '\0';
	int n = slen (s), k = n;
	while (k > 0 && digit (s[k - 1])) k--;
	if (k == n || n - k > 9) return;
	long long v = 0; for (int i = k; i < n; i++) v = v * 10 + (s[i] - '0');
	char d[24]; itoa10 (v + 1, d);
	int w = n - k, dl = slen (d);
	if (k + (w > dl ? w : dl) >= cap) return;
	int o = 0; for (int i = 0; i < k; i++) out[o++] = s[i];
	for (int i = dl; i < w; i++) out[o++] = '0';				// (its leading zeros kept)
	for (int i = 0; i < dl; i++) out[o++] = d[i];
	out[o] = '\0';
}
// A VAT number as written by anyone ("be 0123.456.789", "0123 456 789") -> "BE0123456789": capitals,
// no separators; ten digits alone taken as Belgian (nine: an old one, a 0 before).
static void vat_normalize (const char *in, char *out, int cap)
{
	char t[32]; int n = 0;
	for (const char *p = in; *p && n < 30; p++) if (alnum (*p)) t[n++] = up (*p);
	t[n] = '\0';
	bool allDigits = n > 0; for (int i = 0; i < n; i++) if (!digit (t[i])) allDigits = false;
	if (allDigits && (n == 9 || n == 10)) { scpy (out, "BE", cap); if (n == 9) scat (out, "0", cap); scat (out, t, cap); return; }
	if (n >= 3 && t[0] == 'B' && t[1] == 'E' && n == 11) { scpy (out, "BE0", cap); scat (out, t + 2, cap); return; }
	scpy (out, t, cap);
}
// A Belgian VAT / enterprise number's ten digits: the first 0 or 1, the last two 97 - (the first eight % 97).
static bool be_number_ok (const char *d10)
{
	for (int i = 0; i < 10; i++) if (!digit (d10[i])) return false;
	if (d10[10]) return false;
	if (d10[0] != '0' && d10[0] != '1') return false;
	long v = 0; for (int i = 0; i < 8; i++) v = v * 10 + (d10[i] - '0');
	int chk = (d10[8] - '0') * 10 + (d10[9] - '0');
	return 97 - (int) (v % 97) == chk;
}
// A normalized VAT number: 0 fine, 1 its form is wrong (for its country), 2 a Belgian one's check digits are.
static int vat_check (const char *v)
{
	int n = slen (v);
	if (n < 4 || !(v[0] >= 'A' && v[0] <= 'Z') || !(v[1] >= 'A' && v[1] <= 'Z')) return 1;
	for (int i = 2; i < n; i++) if (!alnum (v[i])) return 1;
	if (v[0] == 'B' && v[1] == 'E') { if (n != 12) return 1; return be_number_ok (v + 2) ? 0 : 2; }
	return n >= 4 && n <= 14 ? 0 : 1;
}
// "BE0123456789" -> "BE 0123.456.789" (others as they are).
static void vat_show (const char *v, char *out, int cap)
{
	if (slen (v) == 12 && v[0] == 'B' && v[1] == 'E')
	{
		char t[20] = "BE ";
		int n = 3;
		for (int i = 2; i < 12; i++) { if (i == 6 || i == 9) t[n++] = '.'; t[n++] = v[i]; }
		t[n] = '\0';
		scpy (out, t, cap);
	}
	else scpy (out, v, cap);
}
// An IBAN -> capitals without spaces; its check (modulo 97 of the rearranged digits == 1).
static void iban_normalize (const char *in, char *out, int cap)
{
	int n = 0;
	for (const char *p = in; *p && n < cap - 1; p++) if (alnum (*p)) out[n++] = up (*p);
	out[n] = '\0';
}
static bool iban_ok (const char *v)
{
	int n = slen (v);
	if (n < 15 || n > 34 || !(v[0] >= 'A' && v[0] <= 'Z') || !(v[1] >= 'A' && v[1] <= 'Z') || !digit (v[2]) || !digit (v[3])) return false;
	if (v[0] == 'B' && v[1] == 'E' && n != 16) return false;
	int r = 0;
	for (int k = 0; k < n; k++)
	{
		char c = v[(k + 4) % n];
		if (digit (c)) r = (r * 10 + (c - '0')) % 97;
		else if (c >= 'A' && c <= 'Z') { int x = c - 'A' + 10; r = (r * 100 + x) % 97; }
		else return false;
	}
	return r == 1;
}
// "BE68539007547034" -> "BE68 5390 0754 7034".
static void iban_show (const char *v, char *out, int cap)
{
	int n = 0;
	for (int i = 0; v[i] && n < cap - 2; i++) { if (i && i % 4 == 0) out[n++] = ' '; out[n++] = v[i]; }
	out[n] = '\0';
}
static bool bic_ok (const char *v)
{
	int n = slen (v);
	if (n != 8 && n != 11) return false;
	for (int i = 0; i < n; i++) if (!alnum (v[i])) return false;
	return true;
}
// A structured communication ("+++090/9337/55493+++", or its twelve digits typed anyhow) -> its twelve
// digits (false: not twelve, or their check wrong).
static bool ogm_parse (const char *s, char *d12)
{
	int n = 0;
	for (const char *p = s; *p; p++) { if (digit (*p)) { if (n >= 12) return false; d12[n++] = *p; } else if (*p != '+' && *p != '/' && *p != ' ' && *p != '*') return false; }
	d12[n] = '\0';
	if (n != 12) return false;
	long long v = 0; for (int i = 0; i < 10; i++) v = v * 10 + (d12[i] - '0');
	int chk = (int) (v % 97); if (!chk) chk = 97;
	return chk == (d12[10] - '0') * 10 + (d12[11] - '0');
}
// Ten digits -> the structured communication's twelve (their modulo 97 after them).
static void ogm_make (long long ten, char *d12)
{
	ten %= 10000000000LL; if (ten < 0) ten = -ten;
	int chk = (int) (ten % 97); if (!chk) chk = 97;
	for (int i = 9; i >= 0; i--) { d12[i] = (char) ('0' + ten % 10); ten /= 10; }
	d12[10] = (char) ('0' + chk / 10); d12[11] = (char) ('0' + chk % 10); d12[12] = '\0';
}
// Twelve digits -> "+++123/4567/89002+++".
static void ogm_show (const char *d12, char *out)
{
	if (slen (d12) != 12) { out[0] = '\0'; return; }
	int n = 0;
	for (int i = 0; i < 3; i++) out[n++] = '+';
	for (int i = 0; i < 12; i++) { if (i == 3 || i == 7) out[n++] = '/'; out[n++] = d12[i]; }
	for (int i = 0; i < 3; i++) out[n++] = '+';
	out[n] = '\0';
}

// ---- text for other programs -----------------------------------------------------------------------------------
// One Latin-1 character (Onyx's; 0x80: the euro, as wtk's font has it) as UTF-8.
static void put_utf8 (Out &o, unsigned char c)
{
	if (c < 0x80) { o.put ((char) c); return; }
	if (c == 0x80) { o.put ((char) 0xE2); o.put ((char) 0x82); o.put ((char) 0xAC); return; }
	if (c < 0xA0) { o.put ('?'); return; }
	o.put ((char) (0xC0 | c >> 6)); o.put ((char) (0x80 | (c & 0x3F)));
}
// Text in an XML element or attribute: escaped, UTF-8.
static void put_xml (Out &o, const char *s)
{
	for (const unsigned char *p = (const unsigned char *) s; *p; p++)
	{
		if (*p == '&') o.puts ("&amp;");
		else if (*p == '<') o.puts ("&lt;");
		else if (*p == '>') o.puts ("&gt;");
		else if (*p == '"') o.puts ("&quot;");
		else if (*p == '\'') o.puts ("&apos;");
		else if (*p < 32 && *p != '\t') o.put (' ');
		else put_utf8 (o, *p);
	}
}
// UTF-8 read back into Latin-1 (what Onyx shows): a character outside it becomes '?', the euro 0x80.
static int utf8_to_latin1 (const char *s, int n, char *out, int cap)
{
	int k = 0;
	for (int i = 0; i < n && k < cap - 1; )
	{
		unsigned char c = (unsigned char) s[i];
		unsigned cp; int len;
		if (c < 0x80) { cp = c; len = 1; }
		else if ((c & 0xE0) == 0xC0 && i + 1 < n) { cp = (c & 0x1F) << 6 | ((unsigned char) s[i + 1] & 0x3F); len = 2; }
		else if ((c & 0xF0) == 0xE0 && i + 2 < n) { cp = (c & 0x0F) << 12 | ((unsigned char) s[i + 1] & 0x3F) << 6 | ((unsigned char) s[i + 2] & 0x3F); len = 3; }
		else if ((c & 0xF8) == 0xF0 && i + 3 < n) { cp = 0xFFFD; len = 4; }
		else { cp = c; len = 1; }				// (not UTF-8: taken as Latin-1)
		i += len;
		out[k++] = cp == 0x20AC ? (char) 0x80 : cp < 0x100 ? (char) cp : cp == 0x2019 || cp == 0x2018 ? '\'' : cp == 0x201C || cp == 0x201D ? '"' : cp == 0x2013 || cp == 0x2014 ? '-' : '?';
	}
	out[k] = '\0';
	return k;
}
// A CSV cell (';' between them, as Belgian spreadsheets expect): quoted when it must be.
static void put_csv (Out &o, const char *s)
{
	bool q = false;
	for (const char *p = s; *p; p++) if (*p == ';' || *p == '"' || *p == '\n' || *p == '\r' || *p == '\t') q = true;
	if (!q) { o.puts (s); return; }
	o.put ('"');
	for (const char *p = s; *p; p++) { if (*p == '"') o.put ('"'); o.put (*p); }
	o.put ('"');
}

// ---- sorting -----------------------------------------------------------------------------------------------------
// A stable merge sort of n indexes by cmp (negative: a first).
typedef int (*IdxCmp) (void *ctx, int a, int b);
static void idx_sort_rec (int *a, int *t, int n, IdxCmp cmp, void *ctx)
{
	if (n < 2) return;
	int h = n / 2;
	idx_sort_rec (a, t, h, cmp, ctx); idx_sort_rec (a + h, t, n - h, cmp, ctx);
	int i = 0, j = h, k = 0;
	while (i < h && j < n) t[k++] = cmp (ctx, a[j], a[i]) < 0 ? a[j++] : a[i++];
	while (i < h) t[k++] = a[i++];
	while (j < n) t[k++] = a[j++];
	for (int x = 0; x < n; x++) a[x] = t[x];
}
static void idx_sort (int *a, int n, IdxCmp cmp, void *ctx)
{
	if (n < 2) return;
	int *t = new int[n];
	idx_sort_rec (a, t, n, cmp, ctx);
	delete [] t;
}

// A growing array of ints.
struct IntList
{
	int *v; int n, cap;
	IntList () : v (0), n (0), cap (0) {}
	~IntList () { delete [] v; }
	void clear () { n = 0; }
	void add (int x) { if (n == cap) { int c = cap ? cap * 2 : 64; int *nv = new int[c]; for (int i = 0; i < n; i++) nv[i] = v[i]; delete [] v; v = nv; cap = c; } v[n++] = x; }
	bool has (int x) const { for (int i = 0; i < n; i++) if (v[i] == x) return true; return false; }
private:
	IntList (const IntList &); IntList &operator= (const IntList &);
};

} // namespace lg

#endif
