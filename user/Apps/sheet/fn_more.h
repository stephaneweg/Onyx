//
// fn_more.h -- the functions (2/2): texts (their characters counted in UTF-8), lookups and references
// (VLOOKUP, MATCH, INDEX, XLOOKUP, OFFSET, INDIRECT...), dates and times, information, finance.
//
#ifndef _sheet_fn_more_h
#define _sheet_fn_more_h

#include "fn_core.h"

namespace ss {

// ---- texts ---------------------------------------------------------------------------------------------
FN (f_len) { ARGS (0, s, l); (void) n; RETN (u8_count (s, l)); }
FN (f_left) { ARGS (0, s, l); OPTN (1, k, 1); if (k < 0) RETE (E_VALUE); int b = u8_offset (s, l, (int) imin ((int) k, 1 << 30)); RET (str_val (cx, s, b)); }
FN (f_right)
{
	ARGS (0, s, l); OPTN (1, k, 1); if (k < 0) RETE (E_VALUE);
	int cnt = u8_count (s, l), from = cnt - (int) imin ((int) k, cnt);
	int b = u8_offset (s, l, from);
	RET (str_val (cx, s + b, l - b));
}
FN (f_mid)
{
	(void) n; ARGS (0, s, l); ARGN (1, st); ARGN (2, k);
	if (st < 1 || k < 0) RETE (E_VALUE);
	int b0 = u8_offset (s, l, (int) imin ((int) (st - 1), 1 << 30));
	int b1 = b0 + u8_offset (s + b0, l - b0, (int) imin ((int) k, 1 << 30));
	RET (str_val (cx, s + b0, b1 - b0));
}
static Val map_case (Ctx &cx, const char *s, int l, int mode)	// 0 lower, 1 upper, 2 proper
{
	Buf b; bool prevLetter = false;
	for (int i = 0; i < l; )
	{
		int k; unsigned c = u8_dec (s + i, l - i, &k); i += k;
		unsigned o = mode == 0 ? uc_lower (c) : mode == 1 ? uc_upper (c) : prevLetter ? uc_lower (c) : uc_upper (c);
		prevLetter = uc_lower (c) != uc_upper (c) || (c >= '0' && c <= '9') || c == '\'';
		if (c == '\'') prevLetter = false;
		b.putu (o);
	}
	return str_val (cx, b);
}
FN (f_lower) { (void) n; ARGS (0, s, l); RET (map_case (cx, s, l, 0)); }
FN (f_upper) { (void) n; ARGS (0, s, l); RET (map_case (cx, s, l, 1)); }
FN (f_proper) { (void) n; ARGS (0, s, l); RET (map_case (cx, s, l, 2)); }
FN (f_trim)
{
	(void) n; ARGS (0, s, l);
	Buf b; bool sp = false;
	for (int i = 0; i < l; i++)
	{
		if (s[i] == ' ') { sp = b.n > 0; continue; }
		if (sp) { b.put (' '); sp = false; }
		b.put (s[i]);
	}
	RET (str_val (cx, b));
}
FN (f_clean) { (void) n; ARGS (0, s, l); Buf b; for (int i = 0; i < l; i++) if ((unsigned char) s[i] >= 32) b.put (s[i]); RET (str_val (cx, b)); }
FN (f_concatenate)
{
	Buf b;
	for (int i = 0; i < n; i++) { ARGS (i, s, l); b.putn (s, l); }
	RET (str_val (cx, b));
}
FN (f_concat)
{
	Buf b; int err = 0;
	for (int i = 0; i < n && !err; i++)
	{
		each (cx, a[i], false, [&] (const Val &v, int, int) -> bool {
			char t[40]; const char *s; int l; int e = 0;
			if (!to_str (cx, v, &s, &l, &e, t)) { err = e; return false; }
			b.putn (s, l); return true; });
	}
	if (err) RETE (err);
	RET (str_val (cx, b));
}
FN (f_textjoin)
{
	ARGS (0, d, dl); ARGB (1, skip);
	Buf b; int err = 0; bool first = true;
	for (int i = 2; i < n && !err; i++)
	{
		each (cx, a[i], true, [&] (const Val &v, int, int) -> bool {
			char t[40]; const char *s; int l; int e = 0;
			if (!to_str (cx, v, &s, &l, &e, t)) { err = e; return false; }
			if (skip && l == 0) return true;
			if (!first) b.putn (d, dl);
			first = false;
			b.putn (s, l); return true; });
	}
	if (err) RETE (err);
	RET (str_val (cx, b));
}
FN (f_rept)
{
	(void) n; ARGS (0, s, l); ARGN (1, k);
	if (k < 0 || l * k > 32767) RETE (E_VALUE);
	Buf b; for (int i = 0; i < (int) k; i++) b.putn (s, l);
	RET (str_val (cx, b));
}
FN (f_substitute)
{
	ARGS (0, s, l); ARGS (1, o, ol); ARGS (2, r, rl); OPTN (3, inst, 0);
	if (n > 3 && a[3].t != V_MISS && inst < 1) RETE (E_VALUE);
	if (ol == 0) RET (str_val (cx, s, l));
	Buf b; int k = 0;
	for (int i = 0; i < l; )
	{
		if (i + ol <= l && !memcmp (s + i, o, ol))
		{
			k++;
			if (inst == 0 || k == (int) inst) { b.putn (r, rl); i += ol; continue; }
		}
		b.put (s[i++]);
	}
	RET (str_val (cx, b));
}
FN (f_replace)
{
	(void) n; ARGS (0, s, l); ARGN (1, st); ARGN (2, k); ARGS (3, r, rl);
	if (st < 1 || k < 0) RETE (E_VALUE);
	int b0 = u8_offset (s, l, (int) st - 1);
	int b1 = b0 + u8_offset (s + b0, l - b0, (int) k);
	Buf b; b.putn (s, b0); b.putn (r, rl); b.putn (s + b1, l - b1);
	RET (str_val (cx, b));
}
static void f_find_ (Ctx &cx, Val *a, int n, Val &out, bool search)
{
	ARGS (0, f, fl); ARGS (1, s, l); OPTN (2, st, 1);
	int cnt = u8_count (s, l);
	if (st < 1 || st > cnt + 1) RETE (E_VALUE);
	int from = u8_offset (s, l, (int) st - 1);
	if (fl == 0) RETN (st);
	for (int i = from, ch = (int) st - 1; i <= l; ch++)
	{
		if (search)
		{
			// (the wildcards: the pattern must match from here, as a prefix)
			for (int e = i; e <= l; )
			{
				if (wild_match (f, fl, s + i, e - i)) RETN (ch + 1);
				if (e == l) break;
				int k; u8_dec (s + e, l - e, &k); e += k;
			}
		}
		else if (i + fl <= l && !memcmp (s + i, f, fl)) RETN (ch + 1);
		if (i == l) break;
		int k; u8_dec (s + i, l - i, &k); i += k;
	}
	RETE (E_VALUE);
}
FN (f_find) { f_find_ (cx, a, n, out, false); }
FN (f_search) { f_find_ (cx, a, n, out, true); }
FN (f_exact) { (void) n; ARGS (0, x, xl); ARGS (1, y, yl); RET (vbool (xl == yl && !memcmp (x, y, xl))); }
static unsigned cp1252 (int c)
{
	static const unsigned short H[32] = { 0x20AC, 0x81, 0x201A, 0x192, 0x201E, 0x2026, 0x2020, 0x2021, 0x2C6, 0x2030, 0x160, 0x2039, 0x152, 0x8D, 0x17D, 0x8F,
		0x90, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x2DC, 0x2122, 0x161, 0x203A, 0x153, 0x9D, 0x17E, 0x178 };
	return c >= 0x80 && c < 0xA0 ? H[c - 0x80] : (unsigned) c;
}
FN (f_char) { (void) n; ARGN (0, x); int c = (int) x; if (c < 1 || c > 255) RETE (E_VALUE); char t[4]; int k = u8_enc (cp1252 (c), t); RET (str_val (cx, t, k)); }
FN (f_code)
{
	(void) n; ARGS (0, s, l); if (!l) RETE (E_VALUE);
	int k; unsigned c = u8_dec (s, l, &k);
	if (c > 255) { for (int i = 0x80; i < 0xA0; i++) if (cp1252 (i) == c) RETN (i); RETN (63); }
	RETN (c);
}
FN (f_unichar) { (void) n; ARGN (0, x); if (x < 1 || x > 0x10FFFF) RETE (E_VALUE); char t[4]; int k = u8_enc ((unsigned) x, t); RET (str_val (cx, t, k)); }
FN (f_unicode) { (void) n; ARGS (0, s, l); if (!l) RETE (E_VALUE); int k; RETN (u8_dec (s, l, &k)); }
FN (f_value)
{
	(void) n;
	Val v = scalar (cx, a[0]);
	if (v.t == V_NUM) RET (v);
	if (v.t == V_ERR) RET (v);
	ARGS (0, s, l);
	double d; if (!input_number (s, l, &d)) RETE (E_VALUE);
	RETN (d);
}
FN (f_numbervalue)
{
	ARGS (0, s, l); OPTS (1, dsep, dl, "."); OPTS (2, gsep, gl, ",");
	Buf b;
	for (int i = 0; i < l; i++)
	{
		if (dl && s[i] == dsep[0]) b.put ('.');
		else if (gl && s[i] == gsep[0]) continue;
		else if (s[i] != ' ') b.put (s[i]);
	}
	double d; int pct = 0;
	while (b.n > 0 && b.b[b.n - 1] == '%') { b.n--; pct++; }
	if (!text_to_num (b.str (), b.n, &d)) RETE (E_VALUE);
	while (pct--) d /= 100;
	RETN (d);
}
FN (f_text)
{
	(void) n; ARGS (1, fmt, fl);
	char code[256]; int k = imin (fl, 255); memcpy (code, fmt, k); code[k] = 0;
	Val v = scalar (cx, a[0]);
	if (v.t == V_ERR) RET (v);
	Buf b;
	double d;
	if (v.t == V_NUM || v.t == V_BOOL || v.t == V_EMPTY) fmt_number (code, v.t == V_EMPTY ? 0 : v.d, b, 0, 11);
	else if (v.t == V_STR && input_number (v.s, v.n, &d)) fmt_number (code, d, b, 0, 11);
	else { char t[256]; int m = imin (v.n, 255); memcpy (t, v.s, m); t[m] = 0; fmt_text (code, t, b); }
	RET (str_val (cx, b));
}
FN (f_fixed)
{
	ARGN (0, x); OPTN (1, d, 2); OPTB (2, nocomma, false);
	int dd = (int) trunc (d);
	char code[40];
	if (dd > 0) { char z[24]; int k = imin (dd, 20); for (int i = 0; i < k; i++) z[i] = '0'; z[k] = 0; snprintf (code, sizeof code, "%s0.%s", nocomma ? "" : "#,##", z); }
	else snprintf (code, sizeof code, "%s0", nocomma ? "" : "#,##");
	if (dd < 0) x = round_dec (x, dd);
	Buf b; fmt_number (code, x, b);
	RET (str_val (cx, b));
}
FN (f_dollar)
{
	ARGN (0, x); OPTN (1, d, 2);
	int dd = (int) trunc (d);
	char code[96];
	if (dd > 0) { char z[24]; int k = imin (dd, 20); for (int i = 0; i < k; i++) z[i] = '0'; z[k] = 0; snprintf (code, sizeof code, "$#,##0.%s;($#,##0.%s)", z, z); }
	else scpy (code, "$#,##0;($#,##0)", sizeof code);
	if (dd < 0) x = round_dec (x, dd);
	Buf b; fmt_number (code, x, b);
	RET (str_val (cx, b));
}
FN (f_t) { (void) n; Val v = scalar (cx, a[0]); if (v.t == V_ERR) RET (v); RET (v.t == V_STR ? v : vstr ("", 0)); }
FN (f_n) { (void) n; Val v = scalar (cx, a[0]); if (v.t == V_ERR) RET (v); RETN (v.t == V_NUM || v.t == V_BOOL ? v.d : 0); }
FN (f_hyperlink) { ARGS (0, u, ul); if (n > 1 && a[1].t != V_MISS) RET (scalar (cx, a[1])); RET (str_val (cx, u, ul)); }

// ---- lookups and references ------------------------------------------------------------------------------
static bool equal_lookup (const Val &x, const Val &v)
{
	if (x.t == V_STR && v.t == V_STR)
		return has_wild (x.s, x.n) ? wild_match (x.s, x.n, v.s, v.n) : ci_cmp (x.s, x.n, v.s, v.n) == 0;
	if (x.t == V_STR || v.t == V_STR) return false;
	if ((x.t == V_BOOL) != (v.t == V_BOOL)) return false;
	if (v.t == V_EMPTY || v.t == V_ERR) return false;
	return x.d == v.d;
}
static bool same_kind (const Val &x, const Val &v) { return (x.t == V_STR) == (v.t == V_STR) && (x.t == V_BOOL) == (v.t == V_BOOL) && v.t != V_EMPTY && v.t != V_ERR; }
// A position in a line of values (a column or a row: get (i)) -- mode 0 exact, 1 the largest <= x
// (ascending), -1 the smallest >= x (descending); -1 when none.
template <class G> static int line_find (Ctx &cx, int cnt, const Val &x, int mode, G get)
{
	(void) cx;
	if (mode == 0) { for (int i = 0; i < cnt; i++) if (equal_lookup (x, get (i))) return i; return -1; }
	// binary search over the values of x's kind (the others skipped as Excel's does)
	int lo = 0, hi = cnt - 1, best = -1;
	while (lo <= hi)
	{
		int mid = (lo + hi) / 2;
		int m = mid;
		Val v = get (m);
		while (!same_kind (x, v) && m < hi) v = get (++m);	// (to the right past the other kinds)
		if (!same_kind (x, v))
		{
			m = mid; v = get (m);
			while (!same_kind (x, v) && m > lo) v = get (--m);
			if (!same_kind (x, v)) break;
			hi = m - 1;
			int c = compare (v, x);
			if (mode > 0 ? c <= 0 : c >= 0) { best = m; if (c == 0) return m; lo = m + 1; hi = mid - 1; }
			continue;
		}
		int c = compare (v, x);
		if (c == 0) { best = m; lo = m + 1; if (mode < 0) return m; continue; }	// (the last of equal ones: ascending)
		if (mode > 0 ? c < 0 : c > 0) { best = m; lo = m + 1; }
		else hi = mid - 1;
	}
	return best;
}
static void f_vhlookup (Ctx &cx, Val *a, int n, Val &out, bool vert)
{
	Val x = scalar (cx, a[0]);
	if (x.t == V_ERR) RET (x);
	const Val &t = a[1];
	if (t.t != V_REF && t.t != V_ARR) RETE (E_NA);
	ARGN (2, colf); OPTB (3, approx, true);
	int R, C; dims (t, &R, &C);
	int col = (int) colf;
	if (col < 1) RETE (E_VALUE);
	if (col > (vert ? C : R)) RETE (E_REF);
	int cnt = vert ? R : C;
	if (t.t == V_REF) { int r1, c1; range_clip (t, &r1, &c1); cnt = imin (cnt, imax (0, (vert ? r1 - t.r0 : c1 - t.c0) + 1)); }
	int i = line_find (cx, cnt, x, approx ? 1 : 0, [&] (int k) { return vert ? elem (cx, t, k, 0) : elem (cx, t, 0, k); });
	if (i < 0) RETE (E_NA);
	Val r = vert ? elem (cx, t, i, col - 1) : elem (cx, t, col - 1, i);
	RET (r.t == V_EMPTY ? vnum (0) : r);
}
FN (f_vlookup) { f_vhlookup (cx, a, n, out, true); }
FN (f_hlookup) { f_vhlookup (cx, a, n, out, false); }
FN (f_match)
{
	Val x = scalar (cx, a[0]);
	if (x.t == V_ERR) RET (x);
	OPTN (2, mt, 1);
	const Val &t = a[1];
	int R, C; dims (t, &R, &C);
	if (R > 1 && C > 1) RETE (E_NA);
	int cnt = R > 1 ? R : C;
	if (t.t == V_REF) { int r1, c1; range_clip (t, &r1, &c1); cnt = imin (cnt, imax (0, (R > 1 ? r1 - t.r0 : c1 - t.c0) + 1)); }
	int mode = mt > 0 ? 1 : mt < 0 ? -1 : 0;
	int i;
	if (mode < 0)						// (the smallest >= x, descending: a scan)
	{
		i = -1;
		for (int k = 0; k < cnt; k++)
		{
			Val v = R > 1 ? elem (cx, t, k, 0) : elem (cx, t, 0, k);
			if (!same_kind (x, v)) continue;
			int c = compare (v, x);
			if (c >= 0) i = k; else break;
			if (c == 0) break;
		}
	}
	else i = line_find (cx, cnt, x, mode, [&] (int k) { return R > 1 ? elem (cx, t, k, 0) : elem (cx, t, 0, k); });
	if (i < 0) RETE (E_NA);
	RETN (i + 1);
}
FN (f_xlookup)
{
	Val x = scalar (cx, a[0]);
	if (x.t == V_ERR) RET (x);
	const Val &la = a[1], &ra = a[2];
	OPTN (4, mm, 0); OPTN (5, sm, 1);
	int R, C; dims (la, &R, &C);
	if (R > 1 && C > 1) RETE (E_VALUE);
	bool vert = R > 1 || C == 1;
	int cnt = vert ? R : C;
	int rr, rc; dims (ra, &rr, &rc);
	if ((vert ? rr : rc) != cnt) RETE (E_VALUE);
	int found = -1;
	int mode = (int) mm;
	bool rev = sm < 0;
	int best = -1;
	for (int s = 0; s < cnt; s++)
	{
		int k = rev ? cnt - 1 - s : s;
		Val v = vert ? elem (cx, la, k, 0) : elem (cx, la, 0, k);
		if (mode == 2 || mode == 0)
		{
			bool eq = mode == 2 ? equal_lookup (x, v) : (x.t == V_STR && v.t == V_STR ? ci_cmp (x.s, x.n, v.s, v.n) == 0 : equal_lookup (x, v));
			if (eq) { found = k; break; }
			continue;
		}
		if (!same_kind (x, v)) continue;
		int c = compare (v, x);
		if (c == 0) { found = k; break; }
		if (mode < 0 && c < 0 && (best < 0 || compare (v, vert ? elem (cx, la, best, 0) : elem (cx, la, 0, best)) > 0)) best = k;
		if (mode > 0 && c > 0 && (best < 0 || compare (v, vert ? elem (cx, la, best, 0) : elem (cx, la, 0, best)) < 0)) best = k;
	}
	if (found < 0) found = best;
	if (found < 0)
	{
		if (n > 3 && a[3].t != V_MISS) RET (scalar (cx, a[3]));
		RETE (E_NA);
	}
	if (ra.t == V_REF)
	{
		if (vert) RET (vref (ra.sh, ra.r0 + found, ra.c0, ra.r0 + found, ra.c1));
		RET (vref (ra.sh, ra.r0, ra.c0 + found, ra.r1, ra.c0 + found));
	}
	RET (vert ? elem (cx, ra, found, 0) : elem (cx, ra, 0, found));
}
FN (f_lookup)
{
	Val x = scalar (cx, a[0]);
	if (x.t == V_ERR) RET (x);
	const Val &lv = a[1];
	int R, C; dims (lv, &R, &C);
	bool vert = R >= C;
	int cnt = vert ? R : C;
	int i = line_find (cx, cnt, x, 1, [&] (int k) { return vert ? elem (cx, lv, k, 0) : elem (cx, lv, 0, k); });
	if (i < 0) RETE (E_NA);
	if (n > 2) { const Val &rv = a[2]; int r2, c2; dims (rv, &r2, &c2); RET (r2 >= c2 ? elem (cx, rv, i, 0) : elem (cx, rv, 0, i)); }
	RET (vert ? elem (cx, lv, i, C - 1) : elem (cx, lv, R - 1, i));
}
FN (f_index)
{
	const Val &t = a[0];
	OPTN (1, rf, 0); OPTN (2, cf, 0);
	int R, C; dims (t, &R, &C);
	int r = (int) rf, c = (int) cf;
	if (n == 2 || (n > 2 && a[2].t == V_MISS)) { if (R == 1 && C > 1) { c = r; r = 1; } else if (C == 1) c = 1; }
	if (r < 0 || c < 0 || r > R || c > C) RETE (E_REF);
	if (t.t == V_REF)
	{
		int r0 = r ? t.r0 + r - 1 : t.r0, r1 = r ? r0 : t.r1;
		int c0 = c ? t.c0 + c - 1 : t.c0, c1 = c ? c0 : t.c1;
		RET (vref (t.sh, r0, c0, r1, c1));
	}
	if (t.t == V_ARR)
	{
		if (r && c) RET (t.a[(r - 1) * t.ac + (c - 1)]);
		if (r) { Val o = arr_new (cx, 1, C); for (int j = 0; j < C; j++) o.a[j] = t.a[(r - 1) * C + j]; RET (o); }
		Val o = arr_new (cx, R, 1); for (int i = 0; i < R; i++) o.a[i] = t.a[i * C + (c ? c - 1 : 0)]; RET (o);
	}
	if (r <= 1 && c <= 1) RET (t);
	RETE (E_REF);
}
FN (f_offset)
{
	const Val &t = a[0];
	if (t.t != V_REF) RETE (E_VALUE);
	ARGN (1, dr); ARGN (2, dc);
	OPTN (3, h, t.r1 - t.r0 + 1); OPTN (4, w, t.c1 - t.c0 + 1);
	int r0 = t.r0 + (int) trunc (dr), c0 = t.c0 + (int) trunc (dc);
	int hh = (int) trunc (h), ww = (int) trunc (w);
	if (hh == 0 || ww == 0) RETE (E_REF);
	int r1 = r0 + hh + (hh > 0 ? -1 : 1), c1 = c0 + ww + (ww > 0 ? -1 : 1);
	if (r1 < r0) { int x = r0; r0 = r1; r1 = x; }
	if (c1 < c0) { int x = c0; c0 = c1; c1 = x; }
	if (r0 < 0 || c0 < 0 || r1 >= MAXR || c1 >= MAXC) RETE (E_REF);
	RET (vref (t.sh, r0, c0, r1, c1));
}
FN (f_indirect)
{
	ARGS (0, s, l); OPTB (1, a1, true);
	if (!a1 || l == 0 || l > 200) RETE (E_REF);
	Formula *f = formula_parse (*cx.b, s, l);
	if (!f) RETE (E_REF);
	int root = f->root;
	while (f->nd[root].k == N_PAREN) root = f->nd[root].a;
	Val r = verr (E_REF);
	if (f->nd[root].k == N_TOK)
	{
		const Tok &k = f->tok[f->nd[root].tok];
		if ((k.t == TK_REF || k.t == TK_AREA) && !(k.fl & TF_BAD))
		{
			Sheet *sh = k.sheet ? book_sheet_by_id (*cx.b, k.sheet) : cx.sh;
			if (sh) r = vref (sh, k.r0, k.c0, k.r1, k.c1);
		}
	}
	formula_free (f);
	RET (r);
}
FN (f_row) { if (n == 0 || a[0].t == V_MISS) RETN (cx.r + 1); if (a[0].t != V_REF) RETE (E_VALUE); RETN (a[0].r0 + 1); }
FN (f_column) { if (n == 0 || a[0].t == V_MISS) RETN (cx.c + 1); if (a[0].t != V_REF) RETE (E_VALUE); RETN (a[0].c0 + 1); }
FN (f_rows) { (void) n; if (a[0].t == V_ERR) RET (a[0]); int R, C; dims (a[0], &R, &C); RETN (R); }
FN (f_columns) { (void) n; if (a[0].t == V_ERR) RET (a[0]); int R, C; dims (a[0], &R, &C); RETN (C); }
FN (f_address)
{
	ARGN (0, r); ARGN (1, c); OPTN (2, absn, 1); OPTB (3, a1, true);
	int ri = (int) r, ci = (int) c, ab = (int) absn;
	if (ri < 1 || ci < 1 || ri > MAXR || ci > MAXC || ab < 1 || ab > 4) RETE (E_VALUE);
	Buf b;
	if (n > 4 && a[4].t != V_MISS) { ARGS (4, sh, shl); char t[80]; int k = imin (shl, 79); memcpy (t, sh, k); t[k] = 0; put_sheet_name (b, t); b.put ('!'); }
	if (a1) { char t[24]; cell_name (ri - 1, ci - 1, t, ab == 1 || ab == 2, ab == 1 || ab == 3); b.puts (t); }
	else
	{
		char t[48];
		if (ab == 1 || ab == 2) snprintf (t, sizeof t, "R%d", ri); else snprintf (t, sizeof t, "R[%d]", ri);
		b.puts (t);
		if (ab == 1 || ab == 3) snprintf (t, sizeof t, "C%d", ci); else snprintf (t, sizeof t, "C[%d]", ci);
		b.puts (t);
	}
	RET (str_val (cx, b));
}
FN (f_transpose)
{
	(void) n;
	int R, C; dims (a[0], &R, &C);
	if ((long long) R * C > 1000000) RETE (E_NUM);
	Val o = arr_new (cx, C, R);
	for (int i = 0; i < R; i++) for (int j = 0; j < C; j++) o.a[j * R + i] = elem (cx, a[0], i, j);
	RET (o);
}
FN (f_formulatext)
{
	(void) n;
	if (a[0].t != V_REF) RETE (E_NA);
	Cell *x = a[0].sh->cells.get (a[0].r0, a[0].c0);
	if (!x || x->kind != K_FORM) RETE (E_NA);
	Buf b; formula_print (*cx.b, x->f, b);
	RET (str_val (cx, b));
}
FN (f_isformula) { (void) n; if (a[0].t != V_REF) RETE (E_VALUE); Cell *x = a[0].sh->cells.get (a[0].r0, a[0].c0); RET (vbool (x && x->kind == K_FORM)); }

// ---- dates and times ---------------------------------------------------------------------------------------
static void today_ymd (int *y, int *m, int *d)
{
	int h, mi, s;
	if (g_now) g_now (y, m, d, &h, &mi, &s); else { *y = 2026; *m = 1; *d = 1; }
}
static double now_serial ()
{
	int y = 2026, m = 1, d = 1, h = 0, mi = 0, s = 0;
	if (g_now) g_now (&y, &m, &d, &h, &mi, &s);
	return date_serial (y, m, d) + (h * 3600 + mi * 60 + s) / 86400.0;
}
#define ARGDATE(i, v) ARGN (i, v); if (v < 0 || v > 2958465.9999999) RETE (E_NUM);
FN (f_date)
{
	(void) n; ARGN (0, y); ARGN (1, m); ARGN (2, d);
	int yi = (int) trunc (y);
	if (yi < 0 || yi > 9999) RETE (E_NUM);
	double s = date_serial (yi, (int) trunc (m), (int) trunc (d));
	if (s < 0 || s > 2958465) RETE (E_NUM);
	RETN (s);
}
FN (f_time)
{
	(void) n; ARGN (0, h); ARGN (1, m); ARGN (2, s);
	double t = trunc (h) * 3600 + trunc (m) * 60 + trunc (s);
	if (t < 0) RETE (E_NUM);
	RETN (fmod (t, 86400) / 86400);
}
FN (f_year) { (void) n; ARGDATE (0, v); int y, m, d; serial_date (v, &y, &m, &d); RETN (y); }
FN (f_month) { (void) n; ARGDATE (0, v); int y, m, d; serial_date (v, &y, &m, &d); RETN (m); }
FN (f_day) { (void) n; ARGDATE (0, v); int y, m, d; serial_date (v, &y, &m, &d); RETN (d); }
FN (f_hour) { (void) n; ARGDATE (0, v); int h, mi, s, ms; serial_time (v, &h, &mi, &s, &ms); RETN (h); }
FN (f_minute) { (void) n; ARGDATE (0, v); int h, mi, s, ms; serial_time (v, &h, &mi, &s, &ms); RETN (mi); }
FN (f_second) { (void) n; ARGDATE (0, v); int h, mi, s, ms; serial_time (v, &h, &mi, &s, &ms); RETN (s + (ms >= 500 ? 1 : 0)); }
FN (f_today) { (void) a; (void) n; RETN (floor (now_serial ())); }
FN (f_now) { (void) a; (void) n; RETN (now_serial ()); }
FN (f_weekday)
{
	ARGDATE (0, v); OPTN (1, t, 1);
	int w = serial_weekday (v), ty = (int) t;		// w: 0 = Sunday
	switch (ty)
	{
	case 1: case 17: RETN (w + 1);
	case 2: case 11: RETN ((w + 6) % 7 + 1);
	case 3: RETN ((w + 6) % 7);
	case 12: case 13: case 14: case 15: case 16: RETN ((w - (ty - 10) + 14) % 7 + 1);
	}
	RETE (E_NUM);
}
static int iso_week (double v)
{
	int y, m, d; serial_date (v, &y, &m, &d);
	int wd = (serial_weekday (v) + 6) % 7;			// 0 = Monday
	double thu = floor (v) - wd + 3;			// the Thursday of this week
	int ty, tm, td; serial_date (thu, &ty, &tm, &td);
	double jan1 = date_serial (ty, 1, 1);
	return (int) ((thu - jan1) / 7) + 1;
}
FN (f_weeknum)
{
	ARGDATE (0, v); OPTN (1, t, 1);
	int ty = (int) t;
	if (ty == 21) RETN (iso_week (v));
	int start;						// the week's first day (0 = Sunday)
	if (ty == 1 || ty == 17) start = 0; else if (ty == 2 || ty == 11) start = 1;
	else if (ty >= 12 && ty <= 16) start = ty - 10; else RETE (E_NUM);
	int y, m, d; serial_date (v, &y, &m, &d);
	double jan1 = date_serial (y, 1, 1);
	int off = (serial_weekday (jan1) - start + 7) % 7;
	RETN (floor ((floor (v) - jan1 + off) / 7) + 1);
}
FN (f_isoweeknum) { (void) n; ARGDATE (0, v); RETN (iso_week (v)); }
static double add_months (double v, int k, bool eom)
{
	int y, m, d; serial_date (v, &y, &m, &d);
	int t = y * 12 + (m - 1) + k;
	int ny = t / 12, nm = t % 12 + 1;
	if (t < 0) { ny = (t - 11) / 12; nm = t - ny * 12 + 1; }
	int md = month_days (ny, nm);
	return date_serial (ny, nm, eom ? md : imin (d, md));
}
FN (f_edate) { (void) n; ARGDATE (0, v); ARGN (1, k); double r = add_months (v, (int) trunc (k), false); if (r < 0) RETE (E_NUM); RETN (r); }
FN (f_eomonth) { (void) n; ARGDATE (0, v); ARGN (1, k); double r = add_months (v, (int) trunc (k), true); if (r < 0) RETE (E_NUM); RETN (r); }
FN (f_days) { (void) n; ARGN (0, e); ARGN (1, s); RETN (floor (e) - floor (s)); }
FN (f_datedif)
{
	(void) n; ARGDATE (0, s); ARGDATE (1, e); ARGS (2, u, ul);
	s = floor (s); e = floor (e);
	if (s > e) RETE (E_NUM);
	int y1, m1, d1, y2, m2, d2; serial_date (s, &y1, &m1, &d1); serial_date (e, &y2, &m2, &d2);
	char unit[4] = ""; for (int i = 0; i < ul && i < 3; i++) { unit[i] = (char) (u[i] & 0xDF); unit[i + 1] = 0; }
	int months = (y2 - y1) * 12 + (m2 - m1) - (d2 < d1 ? 1 : 0);
	if (!strcmp (unit, "Y")) RETN (months / 12);
	if (!strcmp (unit, "M")) RETN (months);
	if (!strcmp (unit, "D")) RETN (e - s);
	if (!strcmp (unit, "YM")) RETN (months % 12);
	if (!strcmp (unit, "MD"))
	{
		if (d2 >= d1) RETN (d2 - d1);
		int pm = m2 - 1, py = y2; if (pm < 1) { pm = 12; py--; }
		RETN (month_days (py, pm) - d1 + d2 < 0 ? 0 : month_days (py, pm) - d1 + d2);
	}
	if (!strcmp (unit, "YD"))
	{
		double s2 = date_serial (y2, m1, d1);
		if (s2 > e) s2 = date_serial (y2 - 1, m1, d1);
		RETN (e - s2);
	}
	RETE (E_NUM);
}
FN (f_days360)
{
	ARGDATE (0, s); ARGDATE (1, e); OPTB (2, eu, false);
	int y1, m1, d1, y2, m2, d2; serial_date (s, &y1, &m1, &d1); serial_date (e, &y2, &m2, &d2);
	if (eu) { if (d1 == 31) d1 = 30; if (d2 == 31) d2 = 30; }
	else
	{
		bool lastFeb1 = m1 == 2 && d1 == month_days (y1, 2);
		if (lastFeb1 || d1 == 31) d1 = 30;
		if (d2 == 31 && d1 >= 30) d2 = 30;
	}
	RETN ((y2 - y1) * 360.0 + (m2 - m1) * 30.0 + (d2 - d1));
}
static bool is_holiday (Ctx &cx, const Val *h, double d)
{
	if (!h) return false;
	bool found = false;
	each (cx, *h, false, [&] (const Val &v, int, int) -> bool { if (v.t == V_NUM && floor (v.d) == d) { found = true; return false; } return true; });
	return found;
}
FN (f_networkdays)
{
	ARGDATE (0, s); ARGDATE (1, e);
	const Val *h = n > 2 && a[2].t != V_MISS ? &a[2] : 0;
	s = floor (s); e = floor (e);
	int sign = 1; if (s > e) { double t = s; s = e; e = t; sign = -1; }
	double k = 0;
	for (double d = s; d <= e; d++) { int w = serial_weekday (d); if (w != 0 && w != 6 && !is_holiday (cx, h, d)) k++; }
	RETN (sign * k);
}
FN (f_workday)
{
	ARGDATE (0, s); ARGN (1, days);
	const Val *h = n > 2 && a[2].t != V_MISS ? &a[2] : 0;
	double d = floor (s);
	int k = (int) trunc (days), step = k < 0 ? -1 : 1;
	while (k != 0)
	{
		d += step;
		int w = serial_weekday (d);
		if (w != 0 && w != 6 && !is_holiday (cx, h, d)) k -= step;
	}
	RETN (d);
}
FN (f_datevalue)
{
	(void) n; ARGS (0, s, l);
	double v; char f[48];
	if (!parse_datetime (s, l, &v, f, sizeof f) || v < 1) RETE (E_VALUE);
	RETN (floor (v));
}
FN (f_timevalue)
{
	(void) n; ARGS (0, s, l);
	double v; char f[48];
	if (!parse_datetime (s, l, &v, f, sizeof f)) RETE (E_VALUE);
	RETN (v - floor (v));
}
FN (f_yearfrac)
{
	ARGDATE (0, s); ARGDATE (1, e); OPTN (2, basis, 0);
	s = floor (s); e = floor (e);
	if (s > e) { double t = s; s = e; e = t; }
	int y1, m1, d1, y2, m2, d2; serial_date (s, &y1, &m1, &d1); serial_date (e, &y2, &m2, &d2);
	switch ((int) basis)
	{
	case 0:
	{
		if (d1 == 31) d1 = 30;
		if (d2 == 31 && d1 >= 30) d2 = 30;
		if (m1 == 2 && d1 == month_days (y1, 2)) { d1 = 30; if (m2 == 2 && d2 == month_days (y2, 2)) d2 = 30; }
		RETN (((y2 - y1) * 360.0 + (m2 - m1) * 30.0 + (d2 - d1)) / 360.0);
	}
	case 1:
	{
		double days = e - s;
		if (y1 == y2) RETN (days / (leap_year (y1) ? 366.0 : 365.0));
		bool withinYear = (y2 == y1 + 1) && (m2 < m1 || (m2 == m1 && d2 <= d1));
		if (withinYear)
		{
			bool leap = (leap_year (y1) && (m1 < 3)) || (leap_year (y2) && (m2 > 2 || (m2 == 2 && d2 == 29)));
			RETN (days / (leap ? 366.0 : 365.0));
		}
		double tot = 0; for (int y = y1; y <= y2; y++) tot += leap_year (y) ? 366 : 365;
		RETN (days / (tot / (y2 - y1 + 1)));
	}
	case 2: RETN ((e - s) / 360.0);
	case 3: RETN ((e - s) / 365.0);
	case 4: { if (d1 == 31) d1 = 30; if (d2 == 31) d2 = 30; RETN (((y2 - y1) * 360.0 + (m2 - m1) * 30.0 + (d2 - d1)) / 360.0); }
	}
	RETE (E_NUM);
}

// ---- information --------------------------------------------------------------------------------------------
FN (f_isblank) { (void) n; Val v = a[0].t == V_REF ? scalar (cx, a[0]) : a[0]; RET (vbool (v.t == V_EMPTY)); }
FN (f_isnumber) { (void) n; Val v = scalar (cx, a[0]); RET (vbool (v.t == V_NUM)); }
FN (f_istext) { (void) n; Val v = scalar (cx, a[0]); RET (vbool (v.t == V_STR)); }
FN (f_isnontext) { (void) n; Val v = scalar (cx, a[0]); RET (vbool (v.t != V_STR)); }
FN (f_islogical) { (void) n; Val v = scalar (cx, a[0]); RET (vbool (v.t == V_BOOL)); }
FN (f_iserror) { (void) n; Val v = scalar (cx, a[0]); RET (vbool (v.t == V_ERR)); }
FN (f_iserr) { (void) n; Val v = scalar (cx, a[0]); RET (vbool (v.t == V_ERR && v.e != E_NA)); }
FN (f_isna) { (void) n; Val v = scalar (cx, a[0]); RET (vbool (v.t == V_ERR && v.e == E_NA)); }
FN (f_isref) { (void) n; RET (vbool (a[0].t == V_REF)); }
FN (f_iseven) { (void) n; ARGN (0, x); RET (vbool (((long long) trunc (x)) % 2 == 0)); }
FN (f_isodd) { (void) n; ARGN (0, x); RET (vbool (((long long) trunc (x)) % 2 != 0)); }
FN (f_na) { (void) a; (void) n; RETE (E_NA); }
FN (f_errortype) { (void) n; Val v = scalar (cx, a[0]); if (v.t != V_ERR || v.e > 7) RETE (E_NA); RETN (v.e); }
FN (f_type)
{
	(void) n;
	if (is_multi (a[0])) RETN (64);
	Val v = scalar (cx, a[0]);
	RETN (v.t == V_NUM || v.t == V_EMPTY ? 1 : v.t == V_STR ? 2 : v.t == V_BOOL ? 4 : v.t == V_ERR ? 16 : 1);
}
FN (f_sheet)
{
	Sheet *s = cx.sh;
	if (n > 0 && a[0].t == V_REF) s = a[0].sh;
	else if (n > 0 && a[0].t == V_STR) { int k = book_sheet_index (*cx.b, a[0].s, a[0].n); if (k < 0) RETE (E_NA); RETN (k + 1); }
	for (int i = 0; i < cx.b->ns; i++) if (cx.b->sh[i] == s) RETN (i + 1);
	RETE (E_NA);
}
FN (f_sheets) { (void) a; (void) n; RETN (cx.b->ns); }

// ---- finance ---------------------------------------------------------------------------------------------
static double fv_ (double r, double np, double pmt, double pv, int type)
{
	if (r == 0) return -(pv + pmt * np);
	double f = pow (1 + r, np);
	return -(pv * f + pmt * (1 + r * type) * (f - 1) / r);
}
static double pmt_ (double r, double np, double pv, double fv, int type)
{
	if (r == 0) return -(pv + fv) / np;
	double f = pow (1 + r, np);
	return -(r * (pv * f + fv)) / ((1 + r * type) * (f - 1));
}
FN (f_pmt) { ARGN (0, r); ARGN (1, np); ARGN (2, pv); OPTN (3, fv, 0); OPTN (4, ty, 0); if (np == 0) RETE (E_NUM); RETN (pmt_ (r, np, pv, fv, ty != 0)); }
FN (f_fv) { ARGN (0, r); ARGN (1, np); ARGN (2, pmt); OPTN (3, pv, 0); OPTN (4, ty, 0); RETN (fv_ (r, np, pmt, pv, ty != 0)); }
FN (f_pv)
{
	ARGN (0, r); ARGN (1, np); ARGN (2, pmt); OPTN (3, fv, 0); OPTN (4, ty, 0);
	if (r == 0) RETN (-(fv + pmt * np));
	double f = pow (1 + r, np);
	RETN (-(fv + pmt * (1 + r * (ty != 0)) * (f - 1) / r) / f);
}
FN (f_nper)
{
	ARGN (0, r); ARGN (1, pmt); ARGN (2, pv); OPTN (3, fv, 0); OPTN (4, ty, 0);
	if (r == 0) { if (pmt == 0) RETE (E_NUM); RETN (-(pv + fv) / pmt); }
	double t = ty != 0 ? 1 : 0;
	double num = pmt * (1 + r * t) - fv * r, den = pmt * (1 + r * t) + pv * r;
	if (num / den <= 0) RETE (E_NUM);
	RETN (log (num / den) / log (1 + r));
}
FN (f_rate)
{
	ARGN (0, np); ARGN (1, pmt); ARGN (2, pv); OPTN (3, fv, 0); OPTN (4, ty, 0); OPTN (5, g, 0.1);
	// the rate r where pv (1 + r)^n + pmt (1 + r type) ((1 + r)^n - 1) / r + fv = 0 (Newton's steps)
	double r = g; int t = ty != 0;
	for (int it = 0; it < 100; it++)
	{
		double h = 1e-7 * (fabs (r) > 1e-3 ? fabs (r) : 1e-3);
		double F = -fv_ (r, np, pmt, pv, t) + fv, F2 = -fv_ (r + h, np, pmt, pv, t) + fv;
		double D = (F2 - F) / h;
		if (D == 0 || isnan (D)) break;
		double step = F / D;
		r -= step;
		if (r <= -1) r = -0.999999;
		if (fabs (step) < 1e-12) RETN (r);
	}
	RETE (E_NUM);
}
FN (f_npv)
{
	ARGN (0, r);
	double s = 0; int i = 0, e = 0;
	if (!each_num (cx, a + 1, n - 1, &e, 0, [&] (double d) { i++; s += d / pow (1 + r, i); })) RETE (e);
	RETN (s);
}
FN (f_irr)
{
	Nums v; int e = 0;
	if (!gather (cx, a, 1, v, &e)) RETE (e);
	OPTN (1, g, 0.1);
	double r = g;
	for (int it = 0; it < 100; it++)
	{
		double f = 0, d = 0;
		for (int i = 0; i < v.n; i++) { f += v.v[i] / pow (1 + r, i); d -= i * v.v[i] / pow (1 + r, i + 1); }
		if (d == 0) break;
		double step = f / d;
		r -= step;
		if (r <= -1) r = -0.99;
		if (fabs (step) < 1e-12) RETN (r);
	}
	RETE (E_NUM);
}
static double ipmt_ (double r, double per, double np, double pv, double fv, int type)
{
	double pmt = pmt_ (r, np, pv, fv, type);
	double ip;
	if (per == 1) ip = type ? 0.0 : -pv;
	else if (type) ip = fv_ (r, per - 2, pmt, pv, 1) - pmt;
	else ip = fv_ (r, per - 1, pmt, pv, 0);
	return ip * r;
}
FN (f_ipmt)
{
	ARGN (0, r); ARGN (1, per); ARGN (2, np); ARGN (3, pv); OPTN (4, fv, 0); OPTN (5, ty, 0);
	if (per < 1 || per > np) RETE (E_NUM);
	RETN (ipmt_ (r, per, np, pv, fv, ty != 0));
}
FN (f_ppmt)
{
	ARGN (0, r); ARGN (1, per); ARGN (2, np); ARGN (3, pv); OPTN (4, fv, 0); OPTN (5, ty, 0);
	if (per < 1 || per > np) RETE (E_NUM);
	RETN (pmt_ (r, np, pv, fv, ty != 0) - ipmt_ (r, per, np, pv, fv, ty != 0));
}
FN (f_sln) { (void) n; ARGN (0, c); ARGN (1, s); ARGN (2, l); if (l == 0) RETE (E_DIV0); RETN ((c - s) / l); }
FN (f_syd) { (void) n; ARGN (0, c); ARGN (1, s); ARGN (2, l); ARGN (3, p); if (l <= 0 || p < 1 || p > l) RETE (E_NUM); RETN ((c - s) * (l - p + 1) * 2 / (l * (l + 1))); }
FN (f_ddb)
{
	ARGN (0, c); ARGN (1, s); ARGN (2, l); ARGN (3, p); OPTN (4, fac, 2);
	if (l <= 0 || p < 1 || p > l || c < 0 || s < 0) RETE (E_NUM);
	double book = c, dep = 0;
	for (int i = 1; i <= (int) ceil (p); i++)
	{
		dep = book * fac / l;
		if (book - dep < s) dep = book - s;
		if (dep < 0) dep = 0;
		book -= dep;
	}
	RETN (dep);
}
FN (f_effect) { (void) n; ARGN (0, r); ARGN (1, k); k = floor (k); if (r <= 0 || k < 1) RETE (E_NUM); RETN (pow (1 + r / k, k) - 1); }
FN (f_nominal) { (void) n; ARGN (0, r); ARGN (1, k); k = floor (k); if (r <= 0 || k < 1) RETE (E_NUM); RETN (k * (pow (1 + r, 1 / k) - 1)); }

} // namespace ss

#endif
