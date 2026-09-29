//
// input.h -- what a typed entry is, as the spreadsheets read it: a number ("1,234.5", "3,5", "-2e3",
// "(42)"), a percentage ("12.5%"), an amount ("$1,200", "12,50 €"), a date ("29/09/2026", "2026-09-29",
// "29 Sep 2026", "Sep 29, 2026", "29/9"), a time ("14:30", "2:30 PM", "1:02:03.5"), both, TRUE / FALSE,
// an error value, else a text (a "'" first: a text, whatever follows). The number format that fits what
// was typed comes along (a percentage shows as one...). Dates are read day first (29/09 = 29 September).
//
#ifndef _sheet_input_h
#define _sheet_input_h

#include "book.h"

namespace ss {

static const char *const MON3[12] = { "jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec" };
static const char *const MONFULL[12] = { "january", "february", "march", "april", "may", "june", "july", "august", "september", "october", "november", "december" };

static int month_word (const char *s, int n)		// "Sep", "september", "sept" -> 9; 0: not a month
{
	if (n < 3) return 0;
	for (int m = 0; m < 12; m++)
	{
		if (n == 3 && ascii_ieq (s, MON3[m], 3)) return m + 1;
		if (n == (int) strlen (MONFULL[m]) && ascii_ieq (s, MONFULL[m], n)) return m + 1;
		if (n == 4 && m == 8 && ascii_ieq (s, "sept", 4)) return 9;
	}
	return 0;
}
static void today_ymd (int *y, int *m, int *d);

struct InTok { char k; int v, n; const char *s; };	// k: 'd' digits (v, n digits), 'w' a word, or the separator
static bool i_is_space (const InTok *t, int nt, int i) { return i < nt && t[i].k == ' '; }

// A date and / or a time -> its serial; the format that shows it like that.
static bool parse_datetime (const char *s, int n, double *out, char *fmt, int fcap)
{
	InTok t[16]; int nt = 0;
	for (int i = 0; i < n; )
	{
		char c = s[i];
		if (nt >= 15) return false;
		if (c >= '0' && c <= '9')
		{
			int v = 0, k = 0; const char *st = s + i;
			while (i < n && s[i] >= '0' && s[i] <= '9') { if (k < 9) v = v * 10 + (s[i] - '0'); k++; i++; }
			t[nt].k = 'd'; t[nt].v = v; t[nt].n = k; t[nt].s = st; nt++;
			continue;
		}
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
		{
			const char *st = s + i; int k = 0;
			while (i < n && ((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z'))) { i++; k++; }
			t[nt].k = 'w'; t[nt].s = st; t[nt].n = k; t[nt].v = 0; nt++;
			continue;
		}
		if (c == ' ') { while (i < n && s[i] == ' ') i++; t[nt].k = ' '; t[nt].n = 1; nt++; continue; }
		if (c == '/' || c == '-' || c == '.' || c == ':' || c == ',') { t[nt].k = c; t[nt].n = 1; nt++; i++; continue; }
		return false;
	}
	int p = 0;
	int y = -1, mo = -1, d = -1;
	bool dateOk = false;
	char dfmt[24] = "";
	auto year = [] (int v, int digits) { return digits <= 2 ? (v < 30 ? 2000 + v : 1900 + v) : v; };
	auto sep = [&] (int i) { return i < nt && (t[i].k == '/' || t[i].k == '-' || t[i].k == '.' || t[i].k == ' '); };
	auto isd = [&] (int i) { return i < nt && t[i].k == 'd'; };
	auto isw = [&] (int i) { return i < nt && t[i].k == 'w'; };
	int cy, cm, cd; today_ymd (&cy, &cm, &cd);
	// the date
	if (isd (0) && t[0].n == 4 && sep (1) && t[1].k != ' ' && isd (2) && sep (3) && t[3].k == t[1].k && isd (4))	// 2026-09-29
	{
		y = t[0].v; mo = t[2].v; d = t[4].v; p = 5; dateOk = true;
		snprintf (dfmt, sizeof dfmt, "yyyy%cmm%cdd", t[1].k, t[1].k);
	}
	else if (isd (0) && t[0].n <= 2 && sep (1) && t[1].k != ' ')
	{
		char sp = t[1].k;
		if (isd (2) && t[2].n <= 2)							// 29/09[/2026]
		{
			d = t[0].v; mo = t[2].v;
			if (sep (3) && t[3].k == sp && isd (4) && (t[4].n == 2 || t[4].n == 4)) { y = year (t[4].v, t[4].n); p = 5; }
			else { y = cy; p = 3; }
			dateOk = true;
			snprintf (dfmt, sizeof dfmt, "dd%cmm%cyyyy", sp == '.' ? '.' : '/', sp == '.' ? '.' : '/');
		}
		else if (isw (2) && month_word (t[2].s, t[2].n))				// 29-Sep[-2026]
		{
			d = t[0].v; mo = month_word (t[2].s, t[2].n);
			if (sep (3) && isd (4) && (t[4].n == 2 || t[4].n == 4)) { y = year (t[4].v, t[4].n); p = 5; }
			else { y = cy; p = 3; }
			dateOk = true;
			scpy (dfmt, sp == '-' ? "dd-mmm-yy" : "d mmm yyyy", sizeof dfmt);
		}
	}
	if (!dateOk && isd (0) && t[0].n <= 2 && i_is_space (t, nt, 1) && isw (2) && month_word (t[2].s, t[2].n))	// 29 Sep 2026
	{
		d = t[0].v; mo = month_word (t[2].s, t[2].n);
		if (i_is_space (t, nt, 3) && isd (4) && (t[4].n == 2 || t[4].n == 4)) { y = year (t[4].v, t[4].n); p = 5; }
		else { y = cy; p = 3; }
		dateOk = true;
		scpy (dfmt, "d mmm yyyy", sizeof dfmt);
	}
	if (!dateOk && isw (0) && month_word (t[0].s, t[0].n))				// Sep 29, 2026 / September 2026
	{
		mo = month_word (t[0].s, t[0].n);
		if (i_is_space (t, nt, 1) && isd (2) && t[2].n <= 2)
		{
			d = t[2].v; p = 3;
			if (p < nt && t[p].k == ',') p++;
			if (i_is_space (t, nt, p)) p++;
			if (isd (p) && (t[p].n == 4 || t[p].n == 2)) { y = year (t[p].v, t[p].n); p++; } else y = cy;
			dateOk = true; scpy (dfmt, "mmm d, yyyy", sizeof dfmt);
		}
		else if (i_is_space (t, nt, 1) && isd (2) && t[2].n == 4) { d = 1; y = t[2].v; p = 3; dateOk = true; scpy (dfmt, "mmmm yyyy", sizeof dfmt); }
	}
	double serial = 0;
	if (dateOk)
	{
		if (mo < 1 || mo > 12 || d < 1 || y < 1900 || y > 9999) return false;
		if (d > month_days (y, mo) && !(y == 1900 && mo == 2 && d == 29)) return false;
		serial = date_serial (y, mo, d);
		if (p < nt && t[p].k == ' ') p++;
		else if (p < nt && t[p].k != ' ') return false;
	}
	// the time
	bool timeOk = false; char tfmt[24] = "";
	if (p < nt)
	{
		if (!isd (p) || !(p + 1 < nt && t[p + 1].k == ':') || !isd (p + 2)) return false;
		int h = t[p].v, mi = t[p + 2].v; double se = 0;
		p += 3;
		bool secs = false;
		if (p < nt && t[p].k == ':' && isd (p + 1))
		{
			se = t[p + 1].v; secs = true; p += 2;
			if (p < nt && t[p].k == '.' && isd (p + 1)) { double f = t[p + 1].v; for (int k = 0; k < t[p + 1].n; k++) f /= 10; se += f; p += 2; }
		}
		bool ampm = false;
		if (p < nt && t[p].k == ' ') p++;
		if (isw (p) && (t[p].n == 2 || t[p].n == 1) && (t[p].s[0] == 'a' || t[p].s[0] == 'A' || t[p].s[0] == 'p' || t[p].s[0] == 'P') && (t[p].n == 1 || t[p].s[1] == 'm' || t[p].s[1] == 'M'))
		{
			if (h < 1 || h > 12) return false;
			bool pm = t[p].s[0] == 'p' || t[p].s[0] == 'P';
			h = h % 12 + (pm ? 12 : 0);
			ampm = true; p++;
		}
		if (p != nt) return false;
		if (mi > 59 || se >= 60 || (h > 23 && dateOk)) return false;
		serial += (h * 3600.0 + mi * 60.0 + se) / 86400.0;
		timeOk = true;
		scpy (tfmt, ampm ? (secs ? "h:mm:ss AM/PM" : "h:mm AM/PM") : secs ? "hh:mm:ss" : "hh:mm", sizeof tfmt);
	}
	if (!dateOk && !timeOk) return false;
	*out = serial;
	if (fmt)
	{
		if (dateOk && timeOk) snprintf (fmt, fcap, "%s %s", dfmt, tfmt);
		else scpy (fmt, dateOk ? dfmt : tfmt, fcap);
	}
	return true;
}

// A number as typed -> its value and the format it asks for ("1,234" -> "#,##0", "5%" -> "0%"...).
static bool parse_typed_number (const char *s0, int n, double *out, char *fmt, int fcap)
{
	if (fmt) fmt[0] = 0;
	const char *s = s0;
	while (n > 0 && (*s == ' ')) { s++; n--; }
	while (n > 0 && s[n - 1] == ' ') n--;
	if (n <= 0 || n > 60) return false;
	bool neg = false, paren = false, pct = false;
	char cur[8] = ""; bool curAfter = false;
	int i = 0;
	auto currency_at = [&] (int at, int *len) -> const char * {
		unsigned char c = (unsigned char) s[at];
		if (c == '$') { *len = 1; return "$"; }
		if (at + 2 < n && c == 0xE2 && (unsigned char) s[at + 1] == 0x82 && (unsigned char) s[at + 2] == 0xAC) { *len = 3; return "\xE2\x82\xAC"; }	// €
		if (at + 1 < n && c == 0xC2 && (unsigned char) s[at + 1] == 0xA3) { *len = 2; return "\xC2\xA3"; }	// £
		if (at + 1 < n && c == 0xC2 && (unsigned char) s[at + 1] == 0xA5) { *len = 2; return "\xC2\xA5"; }	// ¥
		return 0; };
	if (s[i] == '(' && s[n - 1] == ')') { paren = true; i++; n--; }
	if (i < n && (s[i] == '-' || s[i] == '+')) { neg = s[i] == '-'; i++; }
	int cl;
	const char *cs = i < n ? currency_at (i, &cl) : 0;
	if (cs) { scpy (cur, cs, sizeof cur); i += cl; while (i < n && s[i] == ' ') i++; if (!neg && i < n && s[i] == '-') { neg = true; i++; } }
	// the end: %, a currency after
	int e = n;
	if (e > i && s[e - 1] == '%') { pct = true; e--; while (e > i && s[e - 1] == ' ') e--; }
	else
	{
		for (int k = e - 1; k > i && k >= e - 3; k--)
		{
			int l; const char *c2 = currency_at (k, &l);
			if (c2 && k + l == e && !cur[0]) { scpy (cur, c2, sizeof cur); curAfter = true; e = k; while (e > i && s[e - 1] == ' ') e--; break; }
		}
	}
	if (i >= e) return false;
	// digits, the separators, the exponent
	char t[80]; int k = 0;
	int intDigits = 0, groups = 0, decimals = 0; bool point = false, exp = false, grouped = false;
	int lastSep = -1;
	char sepCh = 0;
	for (int j = i; j < e; j++)
	{
		char c = s[j];
		if (c >= '0' && c <= '9') { if (k < 70) t[k++] = c; if (exp) {} else if (point) decimals++; else intDigits++; continue; }
		if (exp) { if ((c == '+' || c == '-') && (s[j - 1] == 'e' || s[j - 1] == 'E')) { t[k++] = c; continue; } return false; }
		if ((c == ',' || c == ' ' || c == '.') && !point)
		{
			// a group separator: exactly three digits follow (then another separator or the end / point)
			int d3 = 0; while (j + 1 + d3 < e && s[j + 1 + d3] >= '0' && s[j + 1 + d3] <= '9') d3++;
			char after = j + 1 + d3 < e ? s[j + 1 + d3] : 0;
			// ('.' groups only when another group or a decimal comma follows: "1.234.567", "1.234,5";
			//  "728.100" is 728.1)
			bool asGroup = d3 == 3 && intDigits > 0 && intDigits <= 3 + groups * 3 && (sepCh == 0 || sepCh == c) &&
				       (c == '.' ? (after == '.' || after == ',' || sepCh == '.') : (after == 0 || after == c || after == '.' || after == 'e' || after == 'E'));
			if (c == '.' && !asGroup) { point = true; t[k++] = '.'; continue; }
			if (c == ',' && !asGroup)
			{
				if ((grouped && sepCh == ',') || d3 == 0) return false;	// ("1,234,5": not a number; "1.234,5" is)
				point = true; t[k++] = '.'; continue;		// (a decimal comma: 3,5)
			}
			if (c == ' ' && !asGroup) return false;
			if (lastSep >= 0 && j - lastSep != 4) return false;
			grouped = true; groups++; sepCh = c; lastSep = j;
			continue;
		}
		if ((c == '.' || c == ',') && point) return false;
		if ((c == 'e' || c == 'E') && k > 0 && j + 1 < e) { exp = true; t[k++] = 'e'; continue; }
		return false;
	}
	if (k == 0 || (k == 1 && t[0] == '.')) return false;
	if (sepCh == '.' && point && grouped) {}		// ("1.234,5": groups by points, a decimal comma)
	t[k] = 0;
	char *endp; double v = strtod (t, &endp);
	if (*endp) return false;
	if (neg || paren) v = -v;
	if (pct) v /= 100;
	*out = v;
	if (fmt)
	{
		char dec[12] = ""; if (decimals > 0) { dec[0] = '.'; int q = imin (decimals, 10); for (int z = 0; z < q; z++) dec[1 + z] = '0'; dec[1 + q] = 0; }
		if (pct) snprintf (fmt, fcap, "0%s%%", dec);
		else if (cur[0])
		{
			const char *d2 = decimals > 0 ? ".00" : "";
			if (curAfter) snprintf (fmt, fcap, "#,##0%s \"%s\"", d2, cur);
			else if (!strcmp (cur, "$")) snprintf (fmt, fcap, "$#,##0%s", d2);
			else snprintf (fmt, fcap, "\"%s\"#,##0%s", cur, d2);
		}
		else if (exp) scpy (fmt, "0.00E+00", fcap);
		else if (grouped) snprintf (fmt, fcap, "#,##0%s", dec);
	}
	return true;
}

// For the formulas' conversions: a number, a percentage, an amount, a date or a time.
static bool input_number (const char *s, int n, double *out)
{
	if (parse_typed_number (s, n, out, 0, 0)) return true;
	return parse_datetime (s, n, out, 0, 0);
}

struct Entry
{
	int kind;						// K_NONE (nothing typed), K_NUM, K_STR, K_BOOL, K_ERR
	double num; int err;
	const char *text; int textLen;				// K_STR (a "'" first taken off)
	char fmt[48];						// the format asked for ("": none)
};

// Typed text (not a formula) -> what it is.
static void input_parse (const char *s, Entry &e)
{
	e.kind = K_NONE; e.num = 0; e.err = 0; e.text = s; e.textLen = (int) strlen (s); e.fmt[0] = 0;
	int n = e.textLen;
	if (n == 0) return;
	if (s[0] == '\'') { e.kind = K_STR; e.text = s + 1; e.textLen = n - 1; return; }
	double v;
	if (parse_typed_number (s, n, &v, e.fmt, sizeof e.fmt)) { e.kind = K_NUM; e.num = v; return; }
	if (parse_datetime (s, n, &v, e.fmt, sizeof e.fmt)) { e.kind = K_NUM; e.num = v; return; }
	int a = 0, z = n; while (a < z && s[a] == ' ') a++; while (z > a && s[z - 1] == ' ') z--;
	if (z - a == 4 && ascii_ieq (s + a, "TRUE", 4)) { e.kind = K_BOOL; e.num = 1; return; }
	if (z - a == 5 && ascii_ieq (s + a, "FALSE", 5)) { e.kind = K_BOOL; e.num = 0; return; }
	for (int k = 1; k <= 7; k++)
	{
		int l = (int) strlen (ERR_NAMES[k]);
		if (z - a == l && ascii_ieq (s + a, ERR_NAMES[k], l)) { e.kind = K_ERR; e.err = k; return; }
	}
	e.kind = K_STR;
}

} // namespace ss

#endif
