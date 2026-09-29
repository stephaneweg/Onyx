//
// numfmt.h -- the number formats, as Excel writes them ("#,##0.00", "0%", "0.00E+00", "# ?/?",
// "dd/mm/yyyy hh:mm", "[Red]-#,##0.00", "\"Total: \"@", "$#,##0.00;($#,##0.00)"): up to four sections
// (positive; negative; zero; text), conditions ([>100]), colours ([Red], [Color10]), literal text
// ("...", \x, _x as a space), 0 # ? placeholders with interleaved literals ("000-000"), the thousands'
// separator (and its scaling: "0.0,," in millions), %, exponents, fractions, dates and times ([h]:mm
// elapsed, AM/PM, .000 of a second). A format is compiled once (fmt_get: a small cache by its text).
//
#ifndef _sheet_numfmt_h
#define _sheet_numfmt_h

#include "core.h"

namespace ss {

static const unsigned FMT_NOCOLOR = 0xFF000000u;	// (= AUTO: the format names no colour)

enum { FT_LIT, FT_D0, FT_DH, FT_DQ, FT_POINT, FT_PCT, FT_EXP, FT_SLASH, FT_AT, FT_GEN,
       FT_Y2, FT_Y4, FT_MO, FT_MO2, FT_MO3, FT_MO4, FT_MO5, FT_D, FT_D2, FT_D3, FT_D4,
       FT_H, FT_H2, FT_MI, FT_MI2, FT_S, FT_S2, FT_AMPM, FT_AP, FT_EH, FT_EM, FT_ES, FT_FS };

struct FTok { unsigned char t, n; unsigned short lit, litn; };	// n: FT_EXP: 1 = "E+", 0 = "E-"; FT_FS: digits
struct FSec
{
	int t0, nt;
	unsigned color;					// AUTO: none
	int cond; double condv;				// 0 none, 1 <, 2 <=, 3 >, 4 >=, 5 =, 6 <>
	bool date, text, general, grouping, sci, frac, elapsed, ampm;
	int pct, scale;					// % count, thousands' scaling (trailing commas)
	int ndec;					// the decimals' placeholders
	int fixedDen;					// a fraction's denominator written out ("# ?/8"), else 0
	int denDigits;					// ... or its placeholders
};
struct CFmt
{
	FSec s[4]; int ns;
	FTok tok[160]; int ntok;
	char lit[640]; int nlit;
};

static const char *const MONTHS[12] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };
static const char *const DAYS[7] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };

static unsigned fmt_color_name (const char *s, int n)
{
	static const struct { const char *nm; unsigned c; } C[] = {
		{ "black", 0x000000 }, { "blue", 0x0000FF }, { "cyan", 0x00FFFF }, { "green", 0x00B050 }, { "magenta", 0xFF00FF },
		{ "red", 0xFF0000 }, { "white", 0xFFFFFF }, { "yellow", 0xFFFF00 } };
	for (unsigned i = 0; i < sizeof C / sizeof C[0]; i++) if ((int) strlen (C[i].nm) == n && ascii_ieq (s, C[i].nm, n)) return C[i].c;
	if (n > 5 && ascii_ieq (s, "color", 5))
	{
		static const unsigned P[16] = { 0x000000, 0xFFFFFF, 0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00, 0xFF00FF, 0x00FFFF,
						0x800000, 0x008000, 0x000080, 0x808000, 0x800080, 0x008080, 0xC0C0C0, 0x808080 };
		int k = atoi (s + 5);
		if (k >= 1 && k <= 16) return P[k - 1];
		if (k > 16 && k <= 56) return 0x808080;
	}
	return FMT_NOCOLOR;
}

// ---- compiling ----------------------------------------------------------------------------------------------
static bool fmt_compile (const char *code, CFmt &f)
{
	memset (&f, 0, sizeof f);
	f.ns = 1;
	FSec *sec = &f.s[0];
	sec->t0 = 0; sec->color = FMT_NOCOLOR;
	int n = (int) strlen (code);
	bool afterPoint = false, seenDigit = false, afterH = false;
	auto add = [&] (int t, int k = 0) -> FTok & {
		if (f.ntok >= 159) f.ntok = 158;
		FTok &x = f.tok[f.ntok++]; x.t = (unsigned char) t; x.n = (unsigned char) k; x.lit = x.litn = 0;
		return x; };
	auto lit = [&] (const char *s, int k) {
		if (f.nlit + k >= (int) sizeof f.lit) return;
		if (f.ntok > sec->t0 && f.tok[f.ntok - 1].t == FT_LIT && f.tok[f.ntok - 1].lit + f.tok[f.ntok - 1].litn == f.nlit)
		{ memcpy (f.lit + f.nlit, s, k); f.nlit += k; f.tok[f.ntok - 1].litn += k; return; }
		FTok &x = add (FT_LIT); x.lit = (unsigned short) f.nlit; x.litn = (unsigned short) k;
		memcpy (f.lit + f.nlit, s, k); f.nlit += k; };
	for (int i = 0; i < n; )
	{
		char c = code[i];
		char lc = c >= 'A' && c <= 'Z' ? c + 32 : c;
		if (c == ';')
		{
			sec->nt = f.ntok - sec->t0;
			if (f.ns >= 4) break;
			sec = &f.s[f.ns++];
			sec->t0 = f.ntok; sec->color = FMT_NOCOLOR;
			afterPoint = seenDigit = afterH = false;
			i++; continue;
		}
		if (c == '"') { int j = i + 1; while (j < n && code[j] != '"') j++; lit (code + i + 1, j - i - 1); i = j < n ? j + 1 : j; continue; }
		if (c == '\\' && i + 1 < n) { int l = 1; u8_dec (code + i + 1, n - i - 1, &l); lit (code + i + 1, l); i += 1 + l; continue; }
		if (c == '_' && i + 1 < n) { lit (" ", 1); int l = 1; u8_dec (code + i + 1, n - i - 1, &l); i += 1 + l; continue; }
		if (c == '*' && i + 1 < n) { int l = 1; u8_dec (code + i + 1, n - i - 1, &l); i += 1 + l; continue; }	// (a fill: left out)
		if (c == '[')
		{
			int j = i + 1; while (j < n && code[j] != ']') j++;
			const char *b = code + i + 1; int bl = j - i - 1;
			i = j < n ? j + 1 : j;
			if (bl <= 0) continue;
			if (b[0] == '$')					// [$€-40C]: the symbol
			{
				int k = 1; while (k < bl && b[k] != '-') k++;
				if (k > 1) lit (b + 1, k - 1);
				continue;
			}
			if (b[0] == '<' || b[0] == '>' || b[0] == '=')
			{
				int k = 0, op;
				if (b[0] == '<' && bl > 1 && b[1] == '=') { op = 2; k = 2; }
				else if (b[0] == '<' && bl > 1 && b[1] == '>') { op = 6; k = 2; }
				else if (b[0] == '>' && bl > 1 && b[1] == '=') { op = 4; k = 2; }
				else { op = b[0] == '<' ? 1 : b[0] == '>' ? 3 : 5; k = 1; }
				char t[32]; int tl = imin (bl - k, 31); memcpy (t, b + k, tl); t[tl] = 0;
				sec->cond = op; sec->condv = strtod (t, 0);
				continue;
			}
			char lb = b[0] >= 'A' && b[0] <= 'Z' ? b[0] + 32 : b[0];
			if ((lb == 'h' || lb == 'm' || lb == 's') && bl <= 3)
			{
				add (lb == 'h' ? FT_EH : lb == 'm' ? FT_EM : FT_ES, bl);
				sec->date = sec->elapsed = true;
				if (lb == 'h') afterH = true;
				continue;
			}
			unsigned col = fmt_color_name (b, bl);
			if (col != FMT_NOCOLOR) sec->color = col;
			continue;
		}
		if (n - i >= 7 && ascii_ieq (code + i, "General", 7)) { add (FT_GEN); sec->general = true; i += 7; continue; }
		if (c == '0' || c == '#' || c == '?')
		{
			if (sec->frac)						// the denominator's placeholders
			{
				add (c == '0' ? FT_D0 : c == '#' ? FT_DH : FT_DQ);
				if (!sec->fixedDen) sec->denDigits++;
				i++; continue;
			}
			add (c == '0' ? FT_D0 : c == '#' ? FT_DH : FT_DQ);
			if (afterPoint) sec->ndec++;
			seenDigit = true;
			i++; continue;
		}
		if (c >= '1' && c <= '9' && sec->frac && f.ntok > 0 && f.tok[f.ntok - 1].t == FT_SLASH)	// "?/8"
		{
			int v = 0; while (i < n && code[i] >= '0' && code[i] <= '9') v = v * 10 + (code[i++] - '0');
			sec->fixedDen = v;
			continue;
		}
		if (c == '.')
		{
			// ".0" after seconds: its fractions
			if (f.ntok > sec->t0 && (f.tok[f.ntok - 1].t == FT_S || f.tok[f.ntok - 1].t == FT_S2 || f.tok[f.ntok - 1].t == FT_ES) && i + 1 < n && code[i + 1] == '0')
			{
				int k = 0; i++;
				while (i < n && code[i] == '0' && k < 3) { k++; i++; }
				add (FT_FS, k);
				continue;
			}
			if (!afterPoint && !sec->date) { add (FT_POINT); afterPoint = true; i++; continue; }
			lit (".", 1); i++; continue;
		}
		if (c == ',')
		{
			// between placeholders: the thousands' separator; after the last one: scaling by 1000
			bool nextDigit = i + 1 < n && (code[i + 1] == '0' || code[i + 1] == '#' || code[i + 1] == '?');
			if (seenDigit && !afterPoint && nextDigit) { sec->grouping = true; i++; continue; }
			if (seenDigit && !nextDigit) { int k = 0; while (i < n && code[i] == ',') { k++; i++; } sec->scale += k; continue; }
			lit (",", 1); i++; continue;
		}
		if (c == '%') { add (FT_PCT); sec->pct++; i++; continue; }
		if ((c == 'E' || c == 'e') && i + 1 < n && (code[i + 1] == '+' || code[i + 1] == '-') && seenDigit)
		{
			add (FT_EXP, code[i + 1] == '+' ? 1 : 0);
			sec->sci = true; afterPoint = true;			// (the exponent's digits: not decimals)
			i += 2; continue;
		}
		if (c == '/' && seenDigit && !sec->date) { add (FT_SLASH); sec->frac = true; i++; continue; }
		if (c == '@') { add (FT_AT); sec->text = true; i++; continue; }
		// dates and times
		if (lc == 'y' || lc == 'e')
		{
			int k = 0; while (i < n && (code[i] == c)) { k++; i++; }
			add (k <= 2 && lc == 'y' ? FT_Y2 : FT_Y4);
			sec->date = true; continue;
		}
		if (lc == 'd')
		{
			int k = 0; while (i < n && (code[i] == 'd' || code[i] == 'D')) { k++; i++; }
			add (k == 1 ? FT_D : k == 2 ? FT_D2 : k == 3 ? FT_D3 : FT_D4);
			sec->date = true; continue;
		}
		if (lc == 'h')
		{
			int k = 0; while (i < n && (code[i] == 'h' || code[i] == 'H')) { k++; i++; }
			add (k == 1 ? FT_H : FT_H2);
			sec->date = true; afterH = true; continue;
		}
		if (lc == 's')
		{
			int k = 0; while (i < n && (code[i] == 's' || code[i] == 'S')) { k++; i++; }
			add (k == 1 ? FT_S : FT_S2);
			// "mm" just before seconds: minutes
			for (int j = f.ntok - 2; j >= sec->t0; j--)
			{
				int t = f.tok[j].t;
				if (t == FT_MO) { f.tok[j].t = FT_MI; break; }
				if (t == FT_MO2) { f.tok[j].t = FT_MI2; break; }
				if (t != FT_LIT) break;
			}
			sec->date = true; continue;
		}
		if (lc == 'm')
		{
			int k = 0; while (i < n && (code[i] == 'm' || code[i] == 'M')) { k++; i++; }
			if (afterH && k <= 2) { add (k == 1 ? FT_MI : FT_MI2); afterH = false; }
			else add (k == 1 ? FT_MO : k == 2 ? FT_MO2 : k == 3 ? FT_MO3 : k == 4 ? FT_MO4 : FT_MO5);
			sec->date = true; continue;
		}
		if (lc == 'a' && n - i >= 5 && ascii_ieq (code + i, "AM/PM", 5)) { add (FT_AMPM); sec->ampm = true; sec->date = true; i += 5; continue; }
		if (lc == 'a' && n - i >= 3 && ascii_ieq (code + i, "A/P", 3)) { add (FT_AP, code[i] == 'a'); sec->ampm = true; sec->date = true; i += 3; continue; }
		if (c == 'B' && i + 1 < n && (code[i + 1] == '1' || code[i + 1] == '2')) { i += 2; continue; }	// (calendars)
		int l = 1; u8_dec (code + i, n - i, &l);
		lit (code + i, l); i += l;
	}
	sec->nt = f.ntok - sec->t0;
	return true;
}

// ---- the cache ----------------------------------------------------------------------------------------------
struct FmtCacheEnt { char *code; CFmt *f; };
static FmtCacheEnt g_fcache[128]; static int g_fcacheN, g_fcacheNext;
static CFmt *fmt_get (const char *code)
{
	for (int i = 0; i < g_fcacheN; i++) if (!strcmp (g_fcache[i].code, code)) return g_fcache[i].f;
	int k;
	if (g_fcacheN < 128) k = g_fcacheN++;
	else { k = g_fcacheNext; g_fcacheNext = (g_fcacheNext + 1) % 128; free (g_fcache[k].code); free (g_fcache[k].f); }
	g_fcache[k].code = sdup (code);
	g_fcache[k].f = (CFmt *) malloc (sizeof (CFmt));
	fmt_compile (code, *g_fcache[k].f);
	return g_fcache[k].f;
}

// ---- rounding as the spreadsheets do (half away from zero, on the decimal value) -----------------------------
static double round_dec (double x, int d)
{
	if (x == 0 || isnan (x) || isinf (x)) return x;
	if (d > 15) return x;
	double p = pow (10.0, d > -308 ? d : -308);
	double y = x * p;
	char t[64]; snprintf (t, sizeof t, "%.15g", y);		// (the binary noise off: 2.675 * 100 -> 267.5)
	y = strtod (t, 0);
	y = y < 0 ? -floor (-y + 0.5) : floor (y + 0.5);
	return y / p;
}

// ---- rendering ----------------------------------------------------------------------------------------------
static void put_padded (Buf &o, long long v, int width)
{
	char t[32]; snprintf (t, sizeof t, "%0*lld", width, v); o.puts (t);
}

static void render_date (const CFmt &f, const FSec &s, double v, Buf &o)
{
	int y, mo, d, h, mi, se, ms;
	// the seconds' fractions shown: round there first (23:59:59.999 -> 00:00:00 the next day)
	int fsd = 0;
	for (int i = 0; i < s.nt; i++) if (f.tok[s.t0 + i].t == FT_FS) fsd = f.tok[s.t0 + i].n;
	double q = fsd == 0 ? 1.0 : fsd == 1 ? 10.0 : fsd == 2 ? 100.0 : 1000.0;
	double vv = floor (v * 86400.0 * q + 0.5) / (86400.0 * q);
	serial_date (vv, &y, &mo, &d);
	serial_time (vv, &h, &mi, &se, &ms);
	int wd = serial_weekday (vv);
	for (int i = 0; i < s.nt; i++)
	{
		const FTok &k = f.tok[s.t0 + i];
		switch (k.t)
		{
		case FT_LIT: o.putn (f.lit + k.lit, k.litn); break;
		case FT_Y2: put_padded (o, y % 100, 2); break;
		case FT_Y4: put_padded (o, y, 4); break;
		case FT_MO: put_padded (o, mo, 1); break;
		case FT_MO2: put_padded (o, mo, 2); break;
		case FT_MO3: o.putn (MONTHS[(mo + 11) % 12], 3); break;
		case FT_MO4: o.puts (MONTHS[(mo + 11) % 12]); break;
		case FT_MO5: o.put (MONTHS[(mo + 11) % 12][0]); break;
		case FT_D: put_padded (o, d, 1); break;
		case FT_D2: put_padded (o, d, 2); break;
		case FT_D3: o.putn (DAYS[wd], 3); break;
		case FT_D4: o.puts (DAYS[wd]); break;
		case FT_H: case FT_H2:
		{
			int hh = h;
			if (s.ampm) { hh = h % 12; if (hh == 0) hh = 12; }
			put_padded (o, hh, k.t == FT_H ? 1 : 2);
			break;
		}
		case FT_MI: put_padded (o, mi, 1); break;
		case FT_MI2: put_padded (o, mi, 2); break;
		case FT_S: put_padded (o, se, 1); break;
		case FT_S2: put_padded (o, se, 2); break;
		case FT_FS:
		{
			char t[8]; snprintf (t, sizeof t, "%03d", ms);
			o.put ('.'); o.putn (t, k.n);
			break;
		}
		case FT_AMPM: o.puts (h < 12 ? "AM" : "PM"); break;
		case FT_AP: o.puts (h < 12 ? (k.n ? "a" : "A") : (k.n ? "p" : "P")); break;
		case FT_EH: put_padded (o, (long long) floor (vv * 24 + 1e-9), k.n); break;
		case FT_EM: put_padded (o, (long long) floor (vv * 1440 + 1e-9), k.n); break;
		case FT_ES: put_padded (o, (long long) floor (vv * 86400 + 1e-9), k.n); break;
		case FT_PCT: o.put ('%'); break;
		case FT_POINT: o.put ('.'); break;
		default: break;
		}
	}
}

// The best fraction n / d for x (0 <= x < 1) with d <= maxD.
static void best_frac (double x, int maxD, long long *num, long long *den)
{
	long long bn = 0, bd = 1; double be = x;
	for (long long d = 1; d <= maxD; d++)
	{
		long long nn = (long long) floor (x * d + 0.5);
		double e = fabs (x - (double) nn / d);
		if (e < be - 1e-12) { be = e; bn = nn; bd = d; }
		if (e < 1e-12) break;
	}
	*num = bn; *den = bd;
}

// A number through a section (v >= 0 when the section is the negatives' own).
static void render_number (const CFmt &f, const FSec &s, double v, bool neg, Buf &o, int maxc)
{
	if (s.general)
	{
		char t[64];
		for (int i = 0; i < s.nt; i++)
		{
			const FTok &k = f.tok[s.t0 + i];
			if (k.t == FT_LIT) o.putn (f.lit + k.lit, k.litn);
			else if (k.t == FT_GEN) { num_general (neg ? -v : v, t, sizeof t, maxc); o.puts (t); }
		}
		return;
	}
	for (int i = 0; i < s.pct; i++) v *= 100;
	for (int i = 0; i < s.scale; i++) v /= 1000;
	// the tokens' regions: the first and last placeholders of the number
	int first = -1, last = -1, point = -1, exp = -1, slash = -1;
	for (int i = 0; i < s.nt; i++)
	{
		int t = f.tok[s.t0 + i].t;
		if (t == FT_D0 || t == FT_DH || t == FT_DQ) { if (first < 0) first = i; last = i; }
		if (t == FT_POINT && point < 0) point = i;
		if (t == FT_EXP && exp < 0) exp = i;
		if (t == FT_SLASH && slash < 0) slash = i;
	}
	if (first < 0)						// no placeholder: the literals (and the sign)
	{
		if (neg) o.put ('-');
		for (int i = 0; i < s.nt; i++) { const FTok &k = f.tok[s.t0 + i]; if (k.t == FT_LIT) o.putn (f.lit + k.lit, k.litn); else if (k.t == FT_PCT) o.put ('%'); }
		return;
	}
	// ---- fractions: "# ?/?" (a whole part and the rest), "?/?" (all as a fraction), "# ?/8"
	if (s.frac && slash > 0)
	{
		int numStart = slash - 1;				// (the numerator's placeholders end at the slash)
		while (numStart > 0 && f.tok[s.t0 + numStart - 1].t >= FT_D0 && f.tok[s.t0 + numStart - 1].t <= FT_DQ) numStart--;
		bool whole = false;
		for (int i = first; i < numStart; i++) { int t = f.tok[s.t0 + i].t; if (t >= FT_D0 && t <= FT_DQ) whole = true; }
		long long ip = whole ? (long long) floor (v) : 0;
		double x = whole ? v - (double) ip : v;
		long long nu, de;
		if (s.fixedDen > 0) { de = s.fixedDen; nu = (long long) floor (x * de + 0.5); }
		else
		{
			int maxD = s.denDigits <= 1 ? 9 : s.denDigits == 2 ? 99 : 999;
			double fl = floor (x);
			best_frac (x - fl, maxD, &nu, &de);
			nu += (long long) fl * de;
		}
		if (whole && nu >= de && de > 0) { ip += nu / de; nu %= de; }
		bool showFrac = nu != 0;
		if (neg && (ip || nu)) o.put ('-');
		for (int i = 0; i < first; i++) { const FTok &k = f.tok[s.t0 + i]; if (k.t == FT_LIT) o.putn (f.lit + k.lit, k.litn); }
		if (whole)
		{
			if (ip || !showFrac) o.puti (ip);
			if (showFrac) { if (ip) o.put (' '); o.puti (nu); o.put ('/'); o.puti (de); }
		}
		else { o.puti (nu); o.put ('/'); o.puti (de); }
		for (int i = last + 1; i < s.nt; i++) { const FTok &k = f.tok[s.t0 + i]; if (k.t == FT_LIT) o.putn (f.lit + k.lit, k.litn); else if (k.t == FT_PCT) o.put ('%'); }
		return;
	}
	// ---- the exponent form
	int expo = 0;
	int intEnd = point >= 0 ? point : exp >= 0 ? exp : last + 1;	// the integer placeholders: [first, intEnd)
	int nint = 0, nint0 = 0;
	for (int i = first; i < intEnd; i++) { int t = f.tok[s.t0 + i].t; if (t >= FT_D0 && t <= FT_DQ) { nint++; if (t == FT_D0) nint0++; } }
	if (s.sci && v != 0)
	{
		int e = (int) floor (log10 (v));
		int step = 1, lead = nint0 > 0 ? nint0 : 1;
		if (nint > 1 && nint0 < nint) { step = nint; lead = 1; }	// ("##0.0E+0": engineering)
		if (step > 1) { expo = e >= 0 ? e / step * step : -((-e + step - 1) / step * step); }
		else expo = e - (lead - 1);
		v = v / pow (10.0, expo);
		double r = round_dec (v, s.ndec);				// (9.99 -> 10.0: the exponent up)
		if (step == 1 && r >= pow (10.0, lead)) { expo++; v /= 10; }
	}
	// the digits
	double rv = round_dec (v, s.ndec);
	char t[400];
	if (rv > 1e300) rv = 1e300;
	snprintf (t, sizeof t, "%.*f", s.ndec, rv);
	char *pt = strchr (t, '.');
	char ids[340]; int ni = 0;					// the integer digits (no leading zeros)
	const char *q = t;
	int il = pt ? (int) (pt - t) : (int) strlen (t);
	while (il > 0 && *q == '0') { q++; il--; }
	memcpy (ids, q, il); ni = il; ids[ni] = 0;
	const char *dec = pt ? pt + 1 : "";
	bool allZero = ni == 0;
	for (const char *p = dec; *p; p++) if (*p != '0') allZero = false;
	if (neg && !allZero) o.put ('-');
	// the literals before the number
	for (int i = 0; i < first; i++)
	{
		const FTok &k = f.tok[s.t0 + i];
		if (k.t == FT_LIT) o.putn (f.lit + k.lit, k.litn);
		else if (k.t == FT_PCT) o.put ('%');
	}
	// the integer part: the digits to the placeholders from the right; the first one takes the extra
	{
		Buf ib;							// built reversed
		int di = ni - 1;
		for (int i = intEnd - 1; i >= first; i--)
		{
			const FTok &k = f.tok[s.t0 + i];
			if (k.t == FT_LIT) { if (!s.grouping) for (int j = k.litn - 1; j >= 0; j--) ib.put (f.lit[k.lit + j]); continue; }
			if (k.t == FT_PCT) { ib.put ('%'); continue; }
			if (k.t < FT_D0 || k.t > FT_DQ) continue;
			bool firstPh = true;
			for (int j = i - 1; j >= first; j--) if (f.tok[s.t0 + j].t >= FT_D0 && f.tok[s.t0 + j].t <= FT_DQ) firstPh = false;
			if (firstPh && di >= 0) { while (di >= 0) ib.put (ids[di--]); }
			else if (di >= 0) ib.put (ids[di--]);
			else if (k.t == FT_D0) ib.put ('0');
			else if (k.t == FT_DQ) ib.put (' ');
		}
		// reversed -> in order; the thousands' separators into the digits
		Buf fw;
		for (int i = ib.n - 1; i >= 0; i--) fw.put (ib.b[i]);
		if (s.grouping)
		{
			int nd = 0; for (int i = 0; i < fw.n; i++) if (fw.b[i] >= '0' && fw.b[i] <= '9') nd++;
			int seenD = 0;
			for (int i = 0; i < fw.n; i++)
			{
				char c = fw.b[i];
				o.put (c);
				if (c >= '0' && c <= '9') { seenD++; int left = nd - seenD; if (left > 0 && left % 3 == 0) o.put (','); }
			}
		}
		else o.putn (fw.b ? fw.b : "", fw.n);
	}
	// the decimals: from the left; '#' drops the zeros at the end, '?' makes them spaces
	if (point >= 0)
	{
		int nd = (int) strlen (dec);
		int keep = nd;						// the digits kept: trailing zeros under '#' / '?' go
		{
			int ph = 0; int kinds[400]; for (int i = point + 1; i < (exp >= 0 ? exp : s.nt); i++) { int tt = f.tok[s.t0 + i].t; if (tt >= FT_D0 && tt <= FT_DQ && ph < 400) kinds[ph++] = tt; }
			while (keep > 0 && dec[keep - 1] == '0' && keep - 1 < ph && kinds[keep - 1] != FT_D0) keep--;
		}
		o.put ('.');
		int di = 0;
		for (int i = point + 1; i <= (exp >= 0 ? exp - 1 : last); i++)
		{
			const FTok &k = f.tok[s.t0 + i];
			if (k.t == FT_LIT) { o.putn (f.lit + k.lit, k.litn); continue; }
			if (k.t == FT_PCT) { o.put ('%'); continue; }
			if (k.t < FT_D0 || k.t > FT_DQ) continue;
			if (di < keep) o.put (dec[di]);
			else if (k.t == FT_DQ) o.put (' ');
			else if (k.t == FT_D0) o.put ('0');
			di++;
		}
	}
	// the exponent
	if (exp >= 0)
	{
		const FTok &ek = f.tok[s.t0 + exp];
		o.put ('E');
		if (expo < 0) o.put ('-'); else if (ek.n) o.put ('+');
		int ed = 0;
		for (int i = exp + 1; i < s.nt; i++) { int tt = f.tok[s.t0 + i].t; if (tt >= FT_D0 && tt <= FT_DQ) ed++; }
		put_padded (o, expo < 0 ? -expo : expo, ed > 0 ? ed : 1);
		for (int i = exp + 1; i < s.nt; i++)
		{
			const FTok &k = f.tok[s.t0 + i];
			if (k.t == FT_LIT && i > last) o.putn (f.lit + k.lit, k.litn);
			else if (k.t == FT_PCT) o.put ('%');
		}
		return;
	}
	// the literals after the number
	for (int i = last + 1; i < s.nt; i++)
	{
		const FTok &k = f.tok[s.t0 + i];
		if (k.t == FT_LIT) o.putn (f.lit + k.lit, k.litn);
		else if (k.t == FT_PCT) o.put ('%');
	}
}

static bool cond_ok (const FSec &s, double v)
{
	switch (s.cond)
	{
	case 1: return v < s.condv; case 2: return v <= s.condv; case 3: return v > s.condv;
	case 4: return v >= s.condv; case 5: return v == s.condv; case 6: return v != s.condv;
	}
	return true;
}

// A number shown through a format -> o (its colour in *color when the format names one). maxc: the
// characters the cell holds (General's digits).
static void fmt_number (const char *code, double v, Buf &o, unsigned *color = 0, int maxc = 11)
{
	if (color) *color = FMT_NOCOLOR;
	if (!code || !code[0] || ci_eq (code, "General")) { char t[64]; num_general (v, t, sizeof t, maxc); o.puts (t); return; }
	CFmt *f = fmt_get (code);
	int si = 0; bool own = false;				// own: the section is the negatives' (no sign added)
	int nnum = f->ns == 4 ? 3 : f->ns;
	if (f->s[0].cond)
	{
		if (cond_ok (f->s[0], v)) si = 0;
		else if (nnum > 1 && (!f->s[1].cond || cond_ok (f->s[1], v))) si = 1;
		else si = imin (2, nnum - 1);
	}
	else if (nnum >= 3 && v == 0) si = 2;
	else if (nnum >= 2 && v < 0) { si = 1; own = true; }
	const FSec &s = f->s[si];
	if (color && s.color != FMT_NOCOLOR) *color = s.color;
	double a = own ? fabs (v) : v;
	if (s.date)
	{
		if (a < 0 || a > 2958465.99999) { o.puts ("###"); return; }
		render_date (*f, s, a, o);
		return;
	}
	if (s.nt == 0) return;					// (an empty section: nothing shown)
	render_number (*f, s, fabs (a), !own && v < 0, o, maxc);
}

// A text shown through a format: its fourth section, or a section with @; else as it is.
static void fmt_text (const char *code, const char *s, Buf &o, unsigned *color = 0)
{
	if (color) *color = FMT_NOCOLOR;
	if (!code || !code[0] || ci_eq (code, "General")) { o.puts (s); return; }
	CFmt *f = fmt_get (code);
	const FSec *sec = 0;
	if (f->ns == 4) sec = &f->s[3];
	else for (int i = 0; i < f->ns; i++) if (f->s[i].text) { sec = &f->s[i]; break; }
	if (!sec) { o.puts (s); return; }
	if (color && sec->color != FMT_NOCOLOR) *color = sec->color;
	for (int i = 0; i < sec->nt; i++)
	{
		const FTok &k = f->tok[sec->t0 + i];
		if (k.t == FT_LIT) o.putn (f->lit + k.lit, k.litn);
		else if (k.t == FT_AT) o.puts (s);
	}
}

// ---- what a format is -----------------------------------------------------------------------------------
enum { FK_GENERAL, FK_NUMBER, FK_CURRENCY, FK_PERCENT, FK_SCI, FK_FRACTION, FK_DATE, FK_TIME, FK_DATETIME, FK_TEXT };
static int fmt_kind (const char *code)
{
	if (!code || !code[0] || ci_eq (code, "General")) return FK_GENERAL;
	CFmt *f = fmt_get (code);
	const FSec &s = f->s[0];
	if (s.date)
	{
		bool d = false, t = false;
		for (int i = 0; i < s.nt; i++)
		{
			int k = f->tok[s.t0 + i].t;
			if ((k >= FT_Y2 && k <= FT_D4)) d = true;
			if (k >= FT_H && k <= FT_FS) t = true;
		}
		return d && t ? FK_DATETIME : t ? FK_TIME : FK_DATE;
	}
	if (s.text && f->ns == 1) return FK_TEXT;
	if (s.pct) return FK_PERCENT;
	if (s.sci) return FK_SCI;
	if (s.frac) return FK_FRACTION;
	for (int i = 0; i < s.nt; i++)
	{
		const FTok &k = f->tok[s.t0 + i];
		if (k.t != FT_LIT) continue;
		for (int j = 0; j < k.litn; j++)
		{
			unsigned char c = (unsigned char) f->lit[k.lit + j];
			if (c == '$' || c == 0xE2 || c == 0xC2 || c == 0xA3 || c == 0xA5) return FK_CURRENCY;	// $, €, £, ¥
		}
	}
	return FK_NUMBER;
}
static int fmt_decimals (const char *code) { if (!code || !code[0] || ci_eq (code, "General")) return -1; return fmt_get (code)->s[0].ndec; }

// The format with its decimals changed by d (in every section): "0.00" + 1 -> "0.000"; General -> "0.0...".
static void fmt_add_decimals (const char *code, int d, int generalDecimals, char *out, int cap)
{
	if (!code || !code[0] || ci_eq (code, "General"))
	{
		int k = imax (0, generalDecimals + d);
		Buf b; b.puts ("0"); if (k) { b.put ('.'); for (int i = 0; i < k; i++) b.put ('0'); }
		scpy (out, b.str (), cap);
		return;
	}
	Buf b;
	int n = (int) strlen (code);
	// section by section: after the last placeholder of the number part (before E / %), add or drop
	int i = 0;
	while (i <= n)
	{
		int j = i; bool q = false;
		while (j < n && (q || code[j] != ';')) { if (code[j] == '"') q = !q; if (code[j] == '\\') j++; j++; }
		// [i, j): a section
		int pointAt = -1, lastPh = -1, expAt = -1; q = false;
		for (int k = i; k < j; k++)
		{
			char c = code[k];
			if (c == '"') { q = !q; continue; }
			if (q) continue;
			if (c == '\\') { k++; continue; }
			if (c == '[') { while (k < j && code[k] != ']') k++; continue; }
			if ((c == 'E' || c == 'e') && k + 1 < j && (code[k + 1] == '+' || code[k + 1] == '-')) { if (expAt < 0) expAt = k; continue; }
			if (expAt >= 0) continue;
			if (c == '.' && pointAt < 0) pointAt = k;
			if (c == '0' || c == '#' || c == '?') lastPh = k;
		}
		if (lastPh < 0) { b.putn (code + i, j - i); }
		else if (d > 0)
		{
			b.putn (code + i, lastPh + 1 - i);
			if (pointAt < 0 || pointAt > lastPh) b.put ('.');
			for (int k = 0; k < d; k++) b.put ('0');
			b.putn (code + lastPh + 1, j - lastPh - 1);
		}
		else
		{
			int drop = -d, from = lastPh + 1;
			int k = lastPh;
			while (drop > 0 && pointAt >= 0 && k > pointAt && (code[k] == '0' || code[k] == '#' || code[k] == '?')) { k--; drop--; }
			if (pointAt >= 0 && k == pointAt) k--;			// (no decimal left: the point goes)
			b.putn (code + i, k + 1 - i);
			b.putn (code + from, j - from);
		}
		if (j < n) b.put (';');
		i = j + 1;
		if (j >= n) break;
	}
	scpy (out, b.str (), cap);
}

} // namespace ss

#endif
