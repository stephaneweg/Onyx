//
// fn_core.h -- the functions (1/2): the helpers every function uses (the arguments read as numbers, texts,
// booleans; the numbers of ranges gathered; the criteria of COUNTIF and its kind; the wildcards), then the
// mathematical, statistical and logical functions. As Excel's: the same names, arguments and results.
//
#ifndef _sheet_fn_core_h
#define _sheet_fn_core_h

#include "eval.h"
#include "numfmt.h"
#include "input.h"

namespace ss {

#define FN(name) static void name (Ctx &cx, Val *a, int n, Val &out)
#define LZ(name) static void name (Ctx &cx, const int *a, int n, Val &out)
#define ARGN(i, v) double v; { int e_ = 0; if (!to_num (cx, a[i], &v, &e_)) { out = verr (e_); return; } }
#define OPTN(i, v, def) double v = (def); if (n > (i) && a[i].t != V_MISS) { int e_ = 0; if (!to_num (cx, a[i], &v, &e_)) { out = verr (e_); return; } }
#define ARGS(i, s, l) const char *s; int l; char tb_##s[40]; { int e_ = 0; if (!to_str (cx, a[i], &s, &l, &e_, tb_##s)) { out = verr (e_); return; } }
#define OPTS(i, s, l, def) const char *s = (def); int l = (int) strlen (def); char tb_##s[40]; if (n > (i) && a[i].t != V_MISS) { int e_ = 0; if (!to_str (cx, a[i], &s, &l, &e_, tb_##s)) { out = verr (e_); return; } }
#define ARGB(i, b) bool b; { int e_ = 0; if (!to_bool (cx, a[i], &b, &e_)) { out = verr (e_); return; } }
#define OPTB(i, b, def) bool b = (def); if (n > (i) && a[i].t != V_MISS) { int e_ = 0; if (!to_bool (cx, a[i], &b, &e_)) { out = verr (e_); return; } }
#define RET(x) do { out = (x); return; } while (0)
#define RETN(x) do { double r_ = (x); out = (isnan (r_) || isinf (r_)) ? verr (E_NUM) : vnum (r_); return; } while (0)
#define RETE(e) do { out = verr (e); return; } while (0)

// ---- numbers gathered --------------------------------------------------------------------------------------
struct Nums { double *v; int n, cap; };
static void nums_add (Ctx &cx, Nums &s, double d)
{
	if (s.n == s.cap)
	{
		int nc = s.cap ? s.cap * 2 : 64;
		double *nv = (double *) cx.ar->alloc (nc * (int) sizeof (double));
		if (s.n) memcpy (nv, s.v, s.n * sizeof (double));
		s.v = nv; s.cap = nc;
	}
	s.v[s.n++] = d;
}
enum { NA_PLAIN = 0, NA_A = 1, NA_SKIPERR = 2 };
// The numbers of the arguments, as SUM takes them: in a range or an array, the numbers only; typed
// directly, a number, a boolean or a text that reads as one. NA_A: the texts count as 0 and the
// booleans as 0 / 1 in ranges too (AVERAGEA...). An error stops (false, *err) unless NA_SKIPERR.
template <class F> static bool each_num (Ctx &cx, Val *a, int n, int *err, int mode, F f)
{
	for (int i = 0; i < n; i++)
	{
		const Val &v = a[i];
		if (v.t == V_MISS || v.t == V_EMPTY) continue;
		if (v.t == V_REF || v.t == V_ARR)
		{
			bool ok = true;
			each (cx, v, false, [&] (const Val &e, int, int) -> bool {
				if (e.t == V_ERR) { if (mode & NA_SKIPERR) return true; *err = e.e; ok = false; return false; }
				if (e.t == V_NUM) f (e.d);
				else if (mode & NA_A) { if (e.t == V_BOOL) f (e.d); else if (e.t == V_STR) f (0); }
				return true; });
			if (!ok) return false;
			continue;
		}
		if (v.t == V_ERR) { if (mode & NA_SKIPERR) continue; *err = v.e; return false; }
		if (v.t == V_NUM || v.t == V_BOOL) { f (v.d); continue; }
		if (v.t == V_STR)
		{
			double d;
			if (input_number (v.s, v.n, &d)) f (d);
			else if (mode & NA_SKIPERR) continue;
			else { *err = E_VALUE; return false; }
		}
	}
	return true;
}
static bool gather (Ctx &cx, Val *a, int n, Nums &s, int *err, int mode = 0)
{
	s.v = 0; s.n = s.cap = 0;
	return each_num (cx, a, n, err, mode, [&] (double d) { nums_add (cx, s, d); });
}
static int dcmp (const void *x, const void *y) { double a = *(const double *) x, b = *(const double *) y; return a < b ? -1 : a > b ? 1 : 0; }

// ---- wildcards and criteria ------------------------------------------------------------------------------
// * any run, ? any character, ~ takes the next literally; case ignored.
static bool wild_match (const char *p, int pn, const char *s, int sn)
{
	int pi = 0, si = 0, starP = -1, starS = 0;
	while (si < sn)
	{
		int pl = 0, sl = 0;
		unsigned pc = pi < pn ? u8_dec (p + pi, pn - pi, &pl) : 0;
		unsigned sc = u8_dec (s + si, sn - si, &sl);
		if (pi < pn && pc == '*') { starP = pi + pl; starS = si; pi += pl; continue; }
		bool lit = false;
		if (pi < pn && pc == '~' && pi + pl < pn) { pi += pl; pc = u8_dec (p + pi, pn - pi, &pl); lit = true; }
		if (pi < pn && ((pc == '?' && !lit) || uc_lower (pc) == uc_lower (sc))) { pi += pl; si += sl; continue; }
		if (starP >= 0) { pi = starP; int l; u8_dec (s + starS, sn - starS, &l); starS += l; si = starS; continue; }
		return false;
	}
	while (pi < pn && p[pi] == '*') pi++;
	return pi == pn;
}
static bool has_wild (const char *s, int n) { for (int i = 0; i < n; i++) if (s[i] == '*' || s[i] == '?' || s[i] == '~') return true; return false; }

struct Crit { int op; int kind; double d; const char *s; int n; bool wild; };	// kind: 0 number, 1 text, 2 bool, 3 empty, 4 error
enum { CR_EQ, CR_NE, CR_LT, CR_LE, CR_GT, CR_GE };
static void crit_parse (Ctx &cx, const Val &c0, Crit &c)
{
	Val v = scalar (cx, c0);
	c.op = CR_EQ; c.kind = 0; c.d = 0; c.s = ""; c.n = 0; c.wild = false;
	if (v.t == V_NUM) { c.d = v.d; return; }
	if (v.t == V_BOOL) { c.kind = 2; c.d = v.d; return; }
	if (v.t == V_ERR) { c.kind = 4; c.d = v.e; return; }
	if (v.t != V_STR) { c.kind = 3; return; }
	const char *s = v.s; int n = v.n;
	if (n >= 2 && s[0] == '<' && s[1] == '>') { c.op = CR_NE; s += 2; n -= 2; }
	else if (n >= 2 && s[0] == '<' && s[1] == '=') { c.op = CR_LE; s += 2; n -= 2; }
	else if (n >= 2 && s[0] == '>' && s[1] == '=') { c.op = CR_GE; s += 2; n -= 2; }
	else if (n >= 1 && s[0] == '<') { c.op = CR_LT; s++; n--; }
	else if (n >= 1 && s[0] == '>') { c.op = CR_GT; s++; n--; }
	else if (n >= 1 && s[0] == '=') { c.op = CR_EQ; s++; n--; }
	if (n == 0) { c.kind = 3; return; }
	double d;
	if (input_number (s, n, &d)) { c.d = d; c.kind = 0; return; }
	if (n == 4 && ascii_ieq (s, "TRUE", 4)) { c.kind = 2; c.d = 1; return; }
	if (n == 5 && ascii_ieq (s, "FALSE", 5)) { c.kind = 2; c.d = 0; return; }
	for (int k = 1; k <= 7; k++) { int l = (int) strlen (ERR_NAMES[k]); if (n == l && ascii_ieq (s, ERR_NAMES[k], l)) { c.kind = 4; c.d = k; return; } }
	c.kind = 1; c.s = s; c.n = n;
	c.wild = (c.op == CR_EQ || c.op == CR_NE) && has_wild (s, n);
}
static bool crit_cmp (int op, int r)
{
	switch (op) { case CR_EQ: return r == 0; case CR_NE: return r != 0; case CR_LT: return r < 0; case CR_LE: return r <= 0; case CR_GT: return r > 0; case CR_GE: return r >= 0; }
	return false;
}
static bool crit_match (const Crit &c, const Val &v)
{
	switch (c.kind)
	{
	case 3:							// "": the empty cells ("<>": the others)
	{
		bool empty = v.t == V_EMPTY || (v.t == V_STR && v.n == 0);
		return c.op == CR_NE ? !empty : c.op == CR_EQ ? empty : false;
	}
	case 0:
	{
		double d;
		if (v.t == V_NUM) d = v.d;
		else if (v.t == V_STR && c.op == CR_EQ && text_to_num (v.s, v.n, &d)) {}
		else return c.op == CR_NE;
		return crit_cmp (c.op, d < c.d ? -1 : d > c.d ? 1 : 0);
	}
	case 1:
		if (v.t != V_STR) return c.op == CR_NE;
		if (c.wild) { bool m = wild_match (c.s, c.n, v.s, v.n); return c.op == CR_EQ ? m : !m; }
		return crit_cmp (c.op, ci_cmp (v.s, v.n, c.s, c.n));
	case 2:
		if (v.t != V_BOOL) return c.op == CR_NE;
		return crit_cmp (c.op, v.d < c.d ? -1 : v.d > c.d ? 1 : 0);
	case 4:
		if (v.t != V_ERR) return c.op == CR_NE;
		return crit_cmp (c.op, v.e == (int) c.d ? 0 : 1);
	}
	return false;
}

// ---- mathematics -------------------------------------------------------------------------------------------
FN (f_sum) { double s = 0; int e = 0; if (!each_num (cx, a, n, &e, 0, [&] (double d) { s += d; })) RETE (e); RETN (s); }
FN (f_sumsq) { double s = 0; int e = 0; if (!each_num (cx, a, n, &e, 0, [&] (double d) { s += d * d; })) RETE (e); RETN (s); }
FN (f_product) { double s = 1; int e = 0, k = 0; if (!each_num (cx, a, n, &e, 0, [&] (double d) { s *= d; k++; })) RETE (e); RETN (k ? s : 0); }
FN (f_abs) { ARGN (0, x); RETN (fabs (x)); }
FN (f_sign) { ARGN (0, x); RETN (x > 0 ? 1 : x < 0 ? -1 : 0); }
FN (f_sqrt) { ARGN (0, x); if (x < 0) RETE (E_NUM); RETN (sqrt (x)); }
FN (f_sqrtpi) { ARGN (0, x); if (x < 0) RETE (E_NUM); RETN (sqrt (x * M_PI)); }
FN (f_exp) { ARGN (0, x); RETN (exp (x)); }
FN (f_ln) { ARGN (0, x); if (x <= 0) RETE (E_NUM); RETN (log (x)); }
FN (f_log10) { ARGN (0, x); if (x <= 0) RETE (E_NUM); RETN (log10 (x)); }
FN (f_log) { ARGN (0, x); OPTN (1, b, 10); if (x <= 0 || b <= 0) RETE (E_NUM); if (b == 1) RETE (E_DIV0); RETN (log (x) / log (b)); }
FN (f_power) { ARGN (0, x); ARGN (1, y); RET (op_scalar (cx, OP_POW, vnum (x), vnum (y))); }
FN (f_pi) { (void) a; (void) n; RETN (M_PI); }
FN (f_int) { ARGN (0, x); RETN (floor (x)); }
FN (f_trunc) { ARGN (0, x); OPTN (1, d, 0); double p = pow (10.0, trunc (d)); double y = x * p; char t[40]; snprintf (t, sizeof t, "%.15g", y); y = strtod (t, 0); RETN (trunc (y) / p); }
FN (f_round) { ARGN (0, x); ARGN (1, d); RETN (round_dec (x, (int) trunc (d))); }
static double round_dir (double x, int d, bool up)
{
	double p = pow (10.0, d);
	double y = x * p;
	char t[40]; snprintf (t, sizeof t, "%.15g", y); y = strtod (t, 0);
	double r = up ? (y < 0 ? -ceil (-y) : ceil (y)) : trunc (y);
	return r / p;
}
FN (f_roundup) { ARGN (0, x); ARGN (1, d); RETN (round_dir (x, (int) trunc (d), true)); }
FN (f_rounddown) { ARGN (0, x); ARGN (1, d); RETN (round_dir (x, (int) trunc (d), false)); }
static double clean15 (double y) { char t[40]; snprintf (t, sizeof t, "%.15g", y); return strtod (t, 0); }
FN (f_ceiling)
{
	ARGN (0, x); OPTN (1, s, x < 0 ? -1 : 1);
	if (s == 0) RETN (0);
	if (x > 0 && s < 0) RETE (E_NUM);
	RETN (ceil (clean15 (x / s)) * s);
}
FN (f_floor)
{
	ARGN (0, x); OPTN (1, s, x < 0 ? -1 : 1);
	if (s == 0) { if (x == 0) RETN (0); RETE (E_DIV0); }
	if (x > 0 && s < 0) RETE (E_NUM);
	RETN (floor (clean15 (x / s)) * s);
}
FN (f_ceiling_math)
{
	ARGN (0, x); OPTN (1, s, 1); OPTN (2, mode, 0);
	s = fabs (s); if (s == 0) RETN (0);
	if (x < 0 && mode != 0) RETN (-ceil (clean15 (-x / s)) * s);
	RETN (ceil (clean15 (x / s)) * s);
}
FN (f_floor_math)
{
	ARGN (0, x); OPTN (1, s, 1); OPTN (2, mode, 0);
	s = fabs (s); if (s == 0) RETN (0);
	if (x < 0 && mode != 0) RETN (-floor (clean15 (-x / s)) * s);
	RETN (floor (clean15 (x / s)) * s);
}
FN (f_mround)
{
	ARGN (0, x); ARGN (1, m);
	if (m == 0) RETN (0);
	if ((x > 0 && m < 0) || (x < 0 && m > 0)) RETE (E_NUM);
	double q = clean15 (x / m);
	RETN ((q < 0 ? -floor (-q + 0.5) : floor (q + 0.5)) * m);
}
FN (f_mod) { ARGN (0, x); ARGN (1, y); if (y == 0) RETE (E_DIV0); double r = x - y * floor (x / y); if (fabs (r) < 1e-12 * fabs (y)) r = 0; RETN (r); }
FN (f_quotient) { ARGN (0, x); ARGN (1, y); if (y == 0) RETE (E_DIV0); RETN (trunc (x / y)); }
FN (f_even) { ARGN (0, x); double r = ceil (fabs (x) / 2) * 2; RETN (x < 0 ? -r : r); }
FN (f_odd) { ARGN (0, x); double r = ceil ((fabs (x) - 1) / 2) * 2 + 1; if (fabs (x) <= 1) r = 1; RETN (x < 0 ? -r : r); }
static double fact (double n) { double r = 1; for (int i = 2; i <= (int) n; i++) r *= i; return r; }
FN (f_fact) { ARGN (0, x); if (x < 0) RETE (E_NUM); x = floor (x); if (x > 170) RETE (E_NUM); RETN (fact (x)); }
FN (f_factdouble) { ARGN (0, x); if (x < -1) RETE (E_NUM); x = floor (x); double r = 1; for (double i = x; i > 1; i -= 2) r *= i; RETN (r); }
static double combin (double n, double k) { if (k > n - k) k = n - k; double r = 1; for (int i = 1; i <= (int) k; i++) r = r * (n - k + i) / i; return floor (r + 0.5); }
FN (f_combin) { ARGN (0, x); ARGN (1, k); x = floor (x); k = floor (k); if (x < 0 || k < 0 || k > x) RETE (E_NUM); RETN (combin (x, k)); }
FN (f_combina) { ARGN (0, x); ARGN (1, k); x = floor (x); k = floor (k); if (x < 0 || k < 0) RETE (E_NUM); RETN (combin (x + k - 1, k)); }
FN (f_permut) { ARGN (0, x); ARGN (1, k); x = floor (x); k = floor (k); if (x < 0 || k < 0 || k > x) RETE (E_NUM); double r = 1; for (int i = 0; i < (int) k; i++) r *= x - i; RETN (r); }
static double gcd2 (double a, double b) { while (b > 0.5) { double t = fmod (a, b); a = b; b = t; } return a; }
FN (f_gcd)
{
	double g = 0; int e = 0; bool bad = false;
	if (!each_num (cx, a, n, &e, 0, [&] (double d) { if (d < 0) bad = true; g = gcd2 (g, floor (d)); })) RETE (e);
	if (bad) RETE (E_NUM);
	RETN (g);
}
FN (f_lcm)
{
	double l = 1; int e = 0; bool bad = false, zero = false;
	if (!each_num (cx, a, n, &e, 0, [&] (double d) { d = floor (d); if (d < 0) bad = true; if (d == 0) zero = true; else l = l / gcd2 (l, d) * d; })) RETE (e);
	if (bad) RETE (E_NUM);
	RETN (zero ? 0 : l);
}
static double rnd01 () { g_rand = g_rand * 1103515245u + 12345u; unsigned a = g_rand >> 8; g_rand = g_rand * 1103515245u + 12345u; unsigned b = g_rand >> 8; return ((double) a * 16777216.0 + b) / 281474976710656.0; }
FN (f_rand) { (void) a; (void) n; RETN (rnd01 ()); }
FN (f_randbetween) { ARGN (0, lo); ARGN (1, hi); lo = ceil (lo); hi = floor (hi); if (lo > hi) RETE (E_NUM); RETN (lo + floor (rnd01 () * (hi - lo + 1))); }
FN (f_sin) { ARGN (0, x); RETN (sin (x)); }
FN (f_cos) { ARGN (0, x); RETN (cos (x)); }
FN (f_tan) { ARGN (0, x); RETN (tan (x)); }
FN (f_asin) { ARGN (0, x); if (x < -1 || x > 1) RETE (E_NUM); RETN (asin (x)); }
FN (f_acos) { ARGN (0, x); if (x < -1 || x > 1) RETE (E_NUM); RETN (acos (x)); }
FN (f_atan) { ARGN (0, x); RETN (atan (x)); }
FN (f_atan2) { ARGN (0, x); ARGN (1, y); if (x == 0 && y == 0) RETE (E_DIV0); RETN (atan2 (y, x)); }
FN (f_sinh) { ARGN (0, x); RETN (sinh (x)); }
FN (f_cosh) { ARGN (0, x); RETN (cosh (x)); }
FN (f_tanh) { ARGN (0, x); RETN (tanh (x)); }
FN (f_asinh) { ARGN (0, x); RETN (asinh (x)); }
FN (f_acosh) { ARGN (0, x); if (x < 1) RETE (E_NUM); RETN (acosh (x)); }
FN (f_atanh) { ARGN (0, x); if (x <= -1 || x >= 1) RETE (E_NUM); RETN (atanh (x)); }
FN (f_cot) { ARGN (0, x); if (x == 0) RETE (E_DIV0); RETN (1 / tan (x)); }
FN (f_degrees) { ARGN (0, x); RETN (x * 180 / M_PI); }
FN (f_radians) { ARGN (0, x); RETN (x * M_PI / 180); }

// SUMPRODUCT: arrays of one size multiplied element by element, then added (non-numbers: 0).
FN (f_sumproduct)
{
	int R, C; dims (a[0], &R, &C);
	for (int i = 1; i < n; i++) { int r, c; dims (a[i], &r, &c); if (r != R || c != C) RETE (E_VALUE); }
	double s = 0;
	for (int i = 0; i < R; i++)
		for (int j = 0; j < C; j++)
		{
			double p = 1;
			for (int k = 0; k < n; k++)
			{
				Val e = elem (cx, a[k], i, j);
				if (e.t == V_ERR) RETE (e.e);
				p *= e.t == V_NUM ? e.d : (e.t == V_BOOL && a[k].t == V_ARR) ? e.d : 0;
			}
			s += p;
		}
	RETN (s);
}

// ---- the *IF(S) family ----------------------------------------------------------------------------------
// The cells of `range` meeting `crit`, and those of the same place in `sum` (or range itself): f (value).
template <class F> static bool if_each (Ctx &cx, const Val &range, const Val &crit, const Val *sum, F f)
{
	if (range.t != V_REF && range.t != V_ARR) return false;
	Crit c; crit_parse (cx, crit, c);
	int R, C; dims (range, &R, &C);
	bool empties = crit_match (c, vempty ());
	if (!empties && range.t == V_REF)
	{
		each (cx, range, false, [&] (const Val &v, int i, int j) -> bool {
			if (crit_match (c, v)) f (sum ? elem (cx, *sum, i, j) : v);
			return true; });
		return true;
	}
	int r1 = R, c1 = C;
	if (range.t == V_REF) { int rr, cc; range_clip (range, &rr, &cc); r1 = imin (R, imax (0, rr - range.r0 + 1)); c1 = imin (C, imax (0, cc - range.c0 + 1)); }
	for (int i = 0; i < r1; i++)
		for (int j = 0; j < c1; j++)
		{
			Val v = elem (cx, range, i, j);
			if (crit_match (c, v)) f (sum ? elem (cx, *sum, i, j) : v);
		}
	return true;
}
FN (f_sumif)
{
	double s = 0;
	if (!if_each (cx, a[0], a[1], n > 2 && a[2].t != V_MISS ? &a[2] : 0, [&] (const Val &v) { if (v.t == V_NUM) s += v.d; })) RETE (E_VALUE);
	RETN (s);
}
FN (f_countif) { double k = 0; if (!if_each (cx, a[0], a[1], 0, [&] (const Val &) { k++; })) RETE (E_VALUE); RETN (k); }
FN (f_averageif)
{
	double s = 0, k = 0;
	if (!if_each (cx, a[0], a[1], n > 2 && a[2].t != V_MISS ? &a[2] : 0, [&] (const Val &v) { if (v.t == V_NUM) { s += v.d; k++; } })) RETE (E_VALUE);
	if (k == 0) RETE (E_DIV0);
	RETN (s / k);
}
// The *IFS: (value range first for SUMIFS / AVERAGEIFS / MAXIFS / MINIFS) then pairs range, criteria.
template <class F> static bool ifs_each (Ctx &cx, Val *a, int n, int first, const Val *vals, F f)
{
	if ((n - first) % 2 || n - first < 2) return false;
	int R, C; dims (a[first], &R, &C);
	int np = (n - first) / 2;
	Crit *cr = (Crit *) cx.ar->alloc (np * (int) sizeof (Crit));
	for (int k = 0; k < np; k++)
	{
		int r, c; dims (a[first + 2 * k], &r, &c);
		if (r != R || c != C) return false;
		crit_parse (cx, a[first + 2 * k + 1], cr[k]);
	}
	if (vals) { int r, c; dims (*vals, &r, &c); if (r != R || c != C) return false; }
	int r1 = R, c1 = C;
	const Val &g = a[first];
	if (g.t == V_REF)				// (clip to the used area of the sheets involved)
	{
		int mr = -1, mc = -1;
		for (int k = 0; k < np; k++)
		{
			const Val &x = a[first + 2 * k];
			if (x.t != V_REF) { mr = R - 1; mc = C - 1; break; }
			sheet_bounds (x.sh);
			mr = imax (mr, x.sh->maxR - x.r0); mc = imax (mc, x.sh->maxC - x.c0);
		}
		if (vals && vals->t == V_REF) { sheet_bounds (vals->sh); mr = imax (mr, vals->sh->maxR - vals->r0); mc = imax (mc, vals->sh->maxC - vals->c0); }
		r1 = imin (R, mr + 1); c1 = imin (C, mc + 1);
	}
	for (int i = 0; i < r1; i++)
		for (int j = 0; j < c1; j++)
		{
			bool ok = true;
			for (int k = 0; k < np && ok; k++) ok = crit_match (cr[k], elem (cx, a[first + 2 * k], i, j));
			if (ok) f (vals ? elem (cx, *vals, i, j) : vnum (1));
		}
	return true;
}
FN (f_sumifs) { double s = 0; if (!ifs_each (cx, a, n, 1, &a[0], [&] (const Val &v) { if (v.t == V_NUM) s += v.d; })) RETE (E_VALUE); RETN (s); }
FN (f_countifs) { double k = 0; if (!ifs_each (cx, a, n, 0, 0, [&] (const Val &) { k++; })) RETE (E_VALUE); RETN (k); }
FN (f_averageifs)
{
	double s = 0, k = 0;
	if (!ifs_each (cx, a, n, 1, &a[0], [&] (const Val &v) { if (v.t == V_NUM) { s += v.d; k++; } })) RETE (E_VALUE);
	if (!k) RETE (E_DIV0);
	RETN (s / k);
}
FN (f_maxifs) { double m = -HUGE_VAL; bool any = false; if (!ifs_each (cx, a, n, 1, &a[0], [&] (const Val &v) { if (v.t == V_NUM) { if (v.d > m) m = v.d; any = true; } })) RETE (E_VALUE); RETN (any ? m : 0); }
FN (f_minifs) { double m = HUGE_VAL; bool any = false; if (!ifs_each (cx, a, n, 1, &a[0], [&] (const Val &v) { if (v.t == V_NUM) { if (v.d < m) m = v.d; any = true; } })) RETE (E_VALUE); RETN (any ? m : 0); }

// ---- statistics ------------------------------------------------------------------------------------------
FN (f_average) { double s = 0; int k = 0, e = 0; if (!each_num (cx, a, n, &e, 0, [&] (double d) { s += d; k++; })) RETE (e); if (!k) RETE (E_DIV0); RETN (s / k); }
FN (f_averagea) { double s = 0; int k = 0, e = 0; if (!each_num (cx, a, n, &e, NA_A, [&] (double d) { s += d; k++; })) RETE (e); if (!k) RETE (E_DIV0); RETN (s / k); }
FN (f_count)
{
	double k = 0;
	for (int i = 0; i < n; i++)
	{
		const Val &v = a[i];
		if (v.t == V_REF || v.t == V_ARR) each (cx, v, false, [&] (const Val &e, int, int) -> bool { if (e.t == V_NUM) k++; return true; });
		else if (v.t == V_NUM || v.t == V_BOOL) k++;
		else if (v.t == V_STR) { double d; if (input_number (v.s, v.n, &d)) k++; }
	}
	RETN (k);
}
FN (f_counta)
{
	double k = 0;
	for (int i = 0; i < n; i++)
	{
		const Val &v = a[i];
		if (v.t == V_REF || v.t == V_ARR) each (cx, v, false, [&] (const Val &e, int, int) -> bool { if (e.t != V_EMPTY) k++; return true; });
		else if (v.t != V_EMPTY) k++;
	}
	RETN (k);
}
FN (f_countblank)
{
	(void) n;
	if (a[0].t != V_REF) RETE (E_VALUE);
	double tot = (double) (a[0].r1 - a[0].r0 + 1) * (a[0].c1 - a[0].c0 + 1), k = 0;
	each (cx, a[0], false, [&] (const Val &e, int, int) -> bool { if (!(e.t == V_STR && e.n == 0)) k++; return true; });
	RETN (tot - k);
}
static void f_minmax (Ctx &cx, Val *a, int n, Val &out, bool mx, int mode)
{
	double m = mx ? -HUGE_VAL : HUGE_VAL; int e = 0; bool any = false;
	if (!each_num (cx, a, n, &e, mode, [&] (double d) { any = true; if (mx ? d > m : d < m) m = d; })) RETE (e);
	RETN (any ? m : 0);
}
FN (f_max) { f_minmax (cx, a, n, out, true, 0); }
FN (f_min) { f_minmax (cx, a, n, out, false, 0); }
FN (f_maxa) { f_minmax (cx, a, n, out, true, NA_A); }
FN (f_mina) { f_minmax (cx, a, n, out, false, NA_A); }
FN (f_median)
{
	Nums s; int e = 0;
	if (!gather (cx, a, n, s, &e)) RETE (e);
	if (!s.n) RETE (E_NUM);
	qsort (s.v, s.n, sizeof (double), dcmp);
	RETN (s.n % 2 ? s.v[s.n / 2] : (s.v[s.n / 2 - 1] + s.v[s.n / 2]) / 2);
}
FN (f_mode)
{
	Nums s; int e = 0;
	if (!gather (cx, a, n, s, &e)) RETE (e);
	int best = 0; double bv = 0;
	for (int i = 0; i < s.n; i++)				// (the first value to reach the highest count)
	{
		int k = 0; for (int j = 0; j < s.n; j++) if (s.v[j] == s.v[i]) k++;
		if (k > best) { best = k; bv = s.v[i]; }
	}
	if (best < 2) RETE (E_NA);
	RETN (bv);
}
static void f_largesmall (Ctx &cx, Val *a, Val &out, bool large)
{
	Nums s; int e = 0;
	if (!gather (cx, a, 1, s, &e)) RETE (e);
	double k; if (!to_num (cx, a[1], &k, &e)) RETE (e);
	int ki = (int) ceil (k - 1e-9);
	if (ki < 1 || ki > s.n) RETE (E_NUM);
	qsort (s.v, s.n, sizeof (double), dcmp);
	RETN (large ? s.v[s.n - ki] : s.v[ki - 1]);
}
FN (f_large) { (void) n; f_largesmall (cx, a, out, true); }
FN (f_small) { (void) n; f_largesmall (cx, a, out, false); }
static void f_rank_ (Ctx &cx, Val *a, int n, Val &out, bool avg)
{
	ARGN (0, x); OPTN (2, ord, 0);
	Nums s; int e = 0;
	if (!gather (cx, a + 1, 1, s, &e)) RETE (e);
	int less = 0, eq = 0;
	for (int i = 0; i < s.n; i++) { if (s.v[i] == x) eq++; else if (ord == 0 ? s.v[i] > x : s.v[i] < x) less++; }
	if (!eq) RETE (E_NA);
	RETN (avg ? less + (eq + 1) / 2.0 : less + 1);
}
FN (f_rank) { f_rank_ (cx, a, n, out, false); }
FN (f_rank_avg) { f_rank_ (cx, a, n, out, true); }
static bool pctile (Nums &s, double p, bool exc, double *r)
{
	if (!s.n) return false;
	qsort (s.v, s.n, sizeof (double), dcmp);
	double h = exc ? p * (s.n + 1) - 1 : p * (s.n - 1);
	if (h < 0 || h > s.n - 1) return false;
	int lo = (int) floor (h);
	double f = h - lo;
	*r = lo + 1 < s.n ? s.v[lo] + f * (s.v[lo + 1] - s.v[lo]) : s.v[lo];
	return true;
}
static void f_pct (Ctx &cx, Val *a, Val &out, bool exc, bool quart)
{
	Nums s; int e = 0;
	if (!gather (cx, a, 1, s, &e)) RETE (e);
	double p; if (!to_num (cx, a[1], &p, &e)) RETE (e);
	if (quart) { p = floor (p); if (p < 0 || p > 4) RETE (E_NUM); p /= 4; }
	if (p < 0 || p > 1) RETE (E_NUM);
	double r; if (!pctile (s, p, exc, &r)) RETE (E_NUM);
	RETN (r);
}
FN (f_percentile) { (void) n; f_pct (cx, a, out, false, false); }
FN (f_percentile_exc) { (void) n; f_pct (cx, a, out, true, false); }
FN (f_quartile) { (void) n; f_pct (cx, a, out, false, true); }
FN (f_quartile_exc) { (void) n; f_pct (cx, a, out, true, true); }
FN (f_percentrank)
{
	Nums s; int e = 0;
	if (!gather (cx, a, 1, s, &e)) RETE (e);
	ARGN (1, x); OPTN (2, sig, 3);
	if (!s.n) RETE (E_NUM);
	qsort (s.v, s.n, sizeof (double), dcmp);
	if (x < s.v[0] || x > s.v[s.n - 1]) RETE (E_NA);
	double r = 0;
	for (int i = 0; i < s.n; i++)
	{
		if (s.v[i] == x) { r = (double) i / (s.n - 1); break; }
		if (i + 1 < s.n && s.v[i] < x && s.v[i + 1] > x) { r = (i + (x - s.v[i]) / (s.v[i + 1] - s.v[i])) / (s.n - 1); break; }
	}
	double p = pow (10.0, floor (sig));
	RETN (floor (r * p + 1e-9) / p);
}
static bool variance (Ctx &cx, Val *a, int n, int mode, bool sample, double *r, int *e)
{
	Nums s;
	if (!gather (cx, a, n, s, e, mode)) return false;
	if (s.n < (sample ? 2 : 1)) { *e = E_DIV0; return false; }
	double m = 0; for (int i = 0; i < s.n; i++) m += s.v[i]; m /= s.n;
	double q = 0; for (int i = 0; i < s.n; i++) q += (s.v[i] - m) * (s.v[i] - m);
	*r = q / (sample ? s.n - 1 : s.n);
	return true;
}
FN (f_var) { double r; int e = 0; if (!variance (cx, a, n, 0, true, &r, &e)) RETE (e); RETN (r); }
FN (f_varp) { double r; int e = 0; if (!variance (cx, a, n, 0, false, &r, &e)) RETE (e); RETN (r); }
FN (f_vara) { double r; int e = 0; if (!variance (cx, a, n, NA_A, true, &r, &e)) RETE (e); RETN (r); }
FN (f_stdev) { double r; int e = 0; if (!variance (cx, a, n, 0, true, &r, &e)) RETE (e); RETN (sqrt (r)); }
FN (f_stdevp) { double r; int e = 0; if (!variance (cx, a, n, 0, false, &r, &e)) RETE (e); RETN (sqrt (r)); }
FN (f_stdeva) { double r; int e = 0; if (!variance (cx, a, n, NA_A, true, &r, &e)) RETE (e); RETN (sqrt (r)); }
FN (f_avedev)
{
	Nums s; int e = 0;
	if (!gather (cx, a, n, s, &e)) RETE (e);
	if (!s.n) RETE (E_NUM);
	double m = 0; for (int i = 0; i < s.n; i++) m += s.v[i]; m /= s.n;
	double q = 0; for (int i = 0; i < s.n; i++) q += fabs (s.v[i] - m);
	RETN (q / s.n);
}
FN (f_devsq)
{
	Nums s; int e = 0;
	if (!gather (cx, a, n, s, &e)) RETE (e);
	double m = 0; for (int i = 0; i < s.n; i++) m += s.v[i]; if (s.n) m /= s.n;
	double q = 0; for (int i = 0; i < s.n; i++) q += (s.v[i] - m) * (s.v[i] - m);
	RETN (q);
}
FN (f_geomean)
{
	Nums s; int e = 0;
	if (!gather (cx, a, n, s, &e)) RETE (e);
	if (!s.n) RETE (E_NUM);
	double l = 0; for (int i = 0; i < s.n; i++) { if (s.v[i] <= 0) RETE (E_NUM); l += log (s.v[i]); }
	RETN (exp (l / s.n));
}
FN (f_harmean)
{
	Nums s; int e = 0;
	if (!gather (cx, a, n, s, &e)) RETE (e);
	if (!s.n) RETE (E_NUM);
	double q = 0; for (int i = 0; i < s.n; i++) { if (s.v[i] <= 0) RETE (E_NUM); q += 1 / s.v[i]; }
	RETN (s.n / q);
}
// Pairs of numbers from two ranges of one size (a pair with a non-number left out).
static bool pairs (Ctx &cx, const Val &x, const Val &y, Nums &xs, Nums &ys, int *e)
{
	int r1, c1, r2, c2; dims (x, &r1, &c1); dims (y, &r2, &c2);
	if (r1 * c1 != r2 * c2) { *e = E_NA; return false; }
	xs.v = ys.v = 0; xs.n = ys.n = xs.cap = ys.cap = 0;
	int cnt = r1 * c1;
	for (int k = 0; k < cnt; k++)
	{
		Val a1 = elem (cx, x, k / c1, k % c1), b1 = elem (cx, y, k / c2, k % c2);
		if (a1.t == V_ERR) { *e = a1.e; return false; }
		if (b1.t == V_ERR) { *e = b1.e; return false; }
		if (a1.t == V_NUM && b1.t == V_NUM) { nums_add (cx, xs, a1.d); nums_add (cx, ys, b1.d); }
	}
	return true;
}
struct Reg { double n, mx, my, sxx, syy, sxy; };
static bool regress (Ctx &cx, const Val &ys, const Val &xs, Reg &r, int *e)
{
	Nums X, Y;
	if (!pairs (cx, xs, ys, X, Y, e)) return false;
	r.n = X.n; r.mx = r.my = r.sxx = r.syy = r.sxy = 0;
	if (!X.n) { *e = E_DIV0; return false; }
	for (int i = 0; i < X.n; i++) { r.mx += X.v[i]; r.my += Y.v[i]; }
	r.mx /= X.n; r.my /= X.n;
	for (int i = 0; i < X.n; i++) { double dx = X.v[i] - r.mx, dy = Y.v[i] - r.my; r.sxx += dx * dx; r.syy += dy * dy; r.sxy += dx * dy; }
	return true;
}
FN (f_correl) { (void) n; Reg r; int e = 0; if (!regress (cx, a[1], a[0], r, &e)) RETE (e); if (r.sxx == 0 || r.syy == 0) RETE (E_DIV0); RETN (r.sxy / sqrt (r.sxx * r.syy)); }
FN (f_rsq) { (void) n; Reg r; int e = 0; if (!regress (cx, a[0], a[1], r, &e)) RETE (e); if (r.sxx == 0 || r.syy == 0) RETE (E_DIV0); RETN (r.sxy * r.sxy / (r.sxx * r.syy)); }
FN (f_covar) { (void) n; Reg r; int e = 0; if (!regress (cx, a[1], a[0], r, &e)) RETE (e); RETN (r.sxy / r.n); }
FN (f_covar_s) { (void) n; Reg r; int e = 0; if (!regress (cx, a[1], a[0], r, &e)) RETE (e); if (r.n < 2) RETE (E_DIV0); RETN (r.sxy / (r.n - 1)); }
FN (f_slope) { (void) n; Reg r; int e = 0; if (!regress (cx, a[0], a[1], r, &e)) RETE (e); if (r.sxx == 0) RETE (E_DIV0); RETN (r.sxy / r.sxx); }
FN (f_intercept) { (void) n; Reg r; int e = 0; if (!regress (cx, a[0], a[1], r, &e)) RETE (e); if (r.sxx == 0) RETE (E_DIV0); RETN (r.my - r.sxy / r.sxx * r.mx); }
FN (f_forecast)
{
	(void) n; ARGN (0, x);
	Reg r; int e = 0; if (!regress (cx, a[1], a[2], r, &e)) RETE (e);
	if (r.sxx == 0) RETE (E_DIV0);
	double b = r.sxy / r.sxx;
	RETN (r.my - b * r.mx + b * x);
}
FN (f_steyx)
{
	(void) n; Reg r; int e = 0; if (!regress (cx, a[0], a[1], r, &e)) RETE (e);
	if (r.n < 3 || r.sxx == 0) RETE (E_DIV0);
	RETN (sqrt ((r.syy - r.sxy * r.sxy / r.sxx) / (r.n - 2)));
}
// the normal distribution
static double norm_cdf (double z) { return 0.5 * erfc (-z / M_SQRT2); }
static double norm_inv (double p)				// (Acklam's rational approximation, one Newton step)
{
	static const double A[6] = { -3.969683028665376e+01, 2.209460984245205e+02, -2.759285104469687e+02, 1.383577518672690e+02, -3.066479806614716e+01, 2.506628277459239e+00 };
	static const double B[5] = { -5.447609879822406e+01, 1.615858368580409e+02, -1.556989798598866e+02, 6.680131188771972e+01, -1.328068155288572e+01 };
	static const double C[6] = { -7.784894002430293e-03, -3.223964580411365e-01, -2.400758277161838e+00, -2.549732539343734e+00, 4.374664141464968e+00, 2.938163982698783e+00 };
	static const double D[4] = { 7.784695709041462e-03, 3.224671290700398e-01, 2.445134137142996e+00, 3.754408661907416e+00 };
	double q, x;
	if (p < 0.02425) { q = sqrt (-2 * log (p)); x = (((((C[0] * q + C[1]) * q + C[2]) * q + C[3]) * q + C[4]) * q + C[5]) / ((((D[0] * q + D[1]) * q + D[2]) * q + D[3]) * q + 1); }
	else if (p > 1 - 0.02425) { q = sqrt (-2 * log (1 - p)); x = -(((((C[0] * q + C[1]) * q + C[2]) * q + C[3]) * q + C[4]) * q + C[5]) / ((((D[0] * q + D[1]) * q + D[2]) * q + D[3]) * q + 1); }
	else { q = p - 0.5; double r = q * q; x = (((((A[0] * r + A[1]) * r + A[2]) * r + A[3]) * r + A[4]) * r + A[5]) * q / (((((B[0] * r + B[1]) * r + B[2]) * r + B[3]) * r + B[4]) * r + 1); }
	double e = norm_cdf (x) - p, u = e * sqrt (2 * M_PI) * exp (x * x / 2);
	return x - u / (1 + x * u / 2);
}
FN (f_normdist)
{
	(void) n; ARGN (0, x); ARGN (1, m); ARGN (2, sd); ARGB (3, cum);
	if (sd <= 0) RETE (E_NUM);
	double z = (x - m) / sd;
	RETN (cum ? norm_cdf (z) : exp (-z * z / 2) / (sd * sqrt (2 * M_PI)));
}
FN (f_norminv) { (void) n; ARGN (0, p); ARGN (1, m); ARGN (2, sd); if (p <= 0 || p >= 1 || sd <= 0) RETE (E_NUM); RETN (m + sd * norm_inv (p)); }
FN (f_normsdist) { ARGN (0, z); OPTB (1, cum, true); RETN (cum ? norm_cdf (z) : exp (-z * z / 2) / sqrt (2 * M_PI)); }
FN (f_normsinv) { (void) n; ARGN (0, p); if (p <= 0 || p >= 1) RETE (E_NUM); RETN (norm_inv (p)); }
FN (f_standardize) { (void) n; ARGN (0, x); ARGN (1, m); ARGN (2, sd); if (sd <= 0) RETE (E_NUM); RETN ((x - m) / sd); }
FN (f_binomdist)
{
	(void) n; ARGN (0, k); ARGN (1, t); ARGN (2, p); ARGB (3, cum);
	k = floor (k); t = floor (t);
	if (k < 0 || k > t || p < 0 || p > 1) RETE (E_NUM);
	auto pmf = [&] (double i) { return combin (t, i) * pow (p, i) * pow (1 - p, t - i); };
	if (!cum) RETN (pmf (k));
	double s = 0; for (double i = 0; i <= k; i++) s += pmf (i);
	RETN (s);
}
FN (f_poisson)
{
	(void) n; ARGN (0, k); ARGN (1, m); ARGB (2, cum);
	k = floor (k);
	if (k < 0 || m < 0) RETE (E_NUM);
	auto pmf = [&] (double i) { return exp (-m + i * log (m > 0 ? m : 1e-300) - lgamma (i + 1)); };
	if (!cum) RETN (m == 0 ? (k == 0 ? 1 : 0) : pmf (k));
	double s = 0; for (double i = 0; i <= k; i++) s += m == 0 ? (i == 0 ? 1 : 0) : pmf (i);
	RETN (s);
}
FN (f_expondist) { (void) n; ARGN (0, x); ARGN (1, l); ARGB (2, cum); if (x < 0 || l <= 0) RETE (E_NUM); RETN (cum ? 1 - exp (-l * x) : l * exp (-l * x)); }

// SUBTOTAL: the function by its number (1-11; 101-111: the hidden rows left out); the SUBTOTALs inside
// its ranges are left out.
static int g_fnSubtotal = -1;
static bool is_subtotal_cell (Sheet *s, int r, int c)
{
	Cell *x = s->cells.get (r, c);
	if (!x || x->kind != K_FORM || !x->f) return false;
	const Node &nd = x->f->nd[x->f->root];
	return nd.k == N_FN && x->f->tok[nd.tok].fn == g_fnSubtotal;
}
FN (f_subtotal)
{
	ARGN (0, fnum);
	int k = (int) fnum;
	bool hid = k > 100; if (hid) k -= 100;
	if (k < 1 || k > 11) RETE (E_VALUE);
	Nums s; s.v = 0; s.n = s.cap = 0;
	double cnta = 0;
	for (int i = 1; i < n; i++)
	{
		const Val &v = a[i];
		if (v.t != V_REF) { if (v.t == V_ERR) RETE (v.e); if (v.t == V_NUM) { nums_add (cx, s, v.d); cnta++; } continue; }
		int r1, c1;
		if (!range_clip (v, &r1, &c1)) continue;
		for (int r = v.r0; r <= r1; r++)
		{
			if (hid) { RowInfo *ri = row_info (v.sh, r); if (ri && (ri->fl & RF_HIDDEN)) continue; }
			for (int c = v.c0; c <= c1; c++)
			{
				if (is_subtotal_cell (v.sh, r, c)) continue;
				Val e = cell_value (cx, v.sh, r, c);
				if (e.t == V_ERR) RETE (e.e);
				if (e.t != V_EMPTY) cnta++;
				if (e.t == V_NUM) nums_add (cx, s, e.d);
			}
		}
	}
	double r = 0, m = 0, q = 0;
	switch (k)
	{
	case 1: if (!s.n) RETE (E_DIV0); for (int i = 0; i < s.n; i++) r += s.v[i]; RETN (r / s.n);
	case 2: RETN (s.n);
	case 3: RETN (cnta);
	case 4: if (!s.n) RETN (0); r = s.v[0]; for (int i = 1; i < s.n; i++) if (s.v[i] > r) r = s.v[i]; RETN (r);
	case 5: if (!s.n) RETN (0); r = s.v[0]; for (int i = 1; i < s.n; i++) if (s.v[i] < r) r = s.v[i]; RETN (r);
	case 6: r = 1; for (int i = 0; i < s.n; i++) r *= s.v[i]; RETN (s.n ? r : 0);
	case 9: for (int i = 0; i < s.n; i++) r += s.v[i]; RETN (r);
	default:
		if (s.n < (k == 7 || k == 10 ? 2 : 1)) RETE (E_DIV0);
		for (int i = 0; i < s.n; i++) m += s.v[i];
		m /= s.n;
		for (int i = 0; i < s.n; i++) q += (s.v[i] - m) * (s.v[i] - m);
		q /= (k == 7 || k == 10) ? s.n - 1 : s.n;
		RETN (k == 7 || k == 8 ? sqrt (q) : q);
	}
}

// ---- logic ---------------------------------------------------------------------------------------------
// AND / OR / XOR: the booleans and numbers of the arguments (the texts of ranges left out).
static void f_logic (Ctx &cx, Val *a, int n, Val &out, int op)
{
	int cnt = 0; bool acc = op == 0; int trues = 0;
	for (int i = 0; i < n; i++)
	{
		const Val &v = a[i];
		auto take = [&] (bool b) { cnt++; if (b) trues++; if (op == 0) acc = acc && b; else if (op == 1) acc = acc || b; };
		if (v.t == V_REF || v.t == V_ARR)
		{
			int err = 0;
			each (cx, v, false, [&] (const Val &e, int, int) -> bool {
				if (e.t == V_ERR) { err = e.e; return false; }
				if (e.t == V_NUM || e.t == V_BOOL) take (e.d != 0);
				return true; });
			if (err) RETE (err);
			continue;
		}
		if (v.t == V_MISS || v.t == V_EMPTY) continue;
		bool b; int e = 0;
		if (!to_bool (cx, v, &b, &e)) RETE (e);
		take (b);
	}
	if (!cnt) RETE (E_VALUE);
	RET (vbool (op == 2 ? (trues & 1) : acc));
}
FN (f_and) { f_logic (cx, a, n, out, 0); }
FN (f_or) { f_logic (cx, a, n, out, 1); }
FN (f_xor) { f_logic (cx, a, n, out, 2); }
FN (f_not) { ARGB (0, b); RET (vbool (!b)); }
FN (f_true) { (void) a; (void) n; RET (vbool (true)); }
FN (f_false) { (void) a; (void) n; RET (vbool (false)); }

// IF: only the branch taken is computed (an array as the condition: element by element).
LZ (f_if)
{
	Val c = eval (cx, a[0]);
	if (is_multi (c))
	{
		int R, C; dims (c, &R, &C);
		Val t = n > 1 ? eval (cx, a[1]) : vbool (true), f = n > 2 ? eval (cx, a[2]) : vbool (false);
		if (n > 1 && t.t == V_MISS) t = vnum (0);
		if (n > 2 && f.t == V_MISS) f = vnum (0);
		Val o = arr_new (cx, R, C);
		for (int i = 0; i < R; i++)
			for (int j = 0; j < C; j++)
			{
				bool b; int e = 0;
				Val ce = elem (cx, c, i, j);
				if (!to_bool (cx, ce, &b, &e)) { o.a[i * C + j] = verr (e); continue; }
				Val r = elem (cx, b ? t : f, i, j);
				o.a[i * C + j] = r.t == V_REF ? scalar (cx, r) : r;
			}
		RET (o);
	}
	bool b; int e = 0;
	if (!to_bool (cx, c, &b, &e)) RETE (e);
	if (b) { if (n < 2) RET (vbool (true)); Val v = eval (cx, a[1]); RET (v.t == V_MISS ? vnum (0) : v); }
	if (n < 3) RET (vbool (false));
	Val v = eval (cx, a[2]);
	RET (v.t == V_MISS ? vnum (0) : v);
}
LZ (f_ifs)
{
	if (n % 2) RETE (E_VALUE);
	for (int i = 0; i < n; i += 2)
	{
		bool b; int e = 0;
		if (!to_bool (cx, eval (cx, a[i]), &b, &e)) RETE (e);
		if (b) RET (eval (cx, a[i + 1]));
	}
	RETE (E_NA);
}
LZ (f_iferror)
{
	(void) n;
	Val v = eval (cx, a[0]);
	Val s = v.t == V_REF ? scalar (cx, v) : v;
	if (s.t == V_ERR && s.e != E_DEFER) RET (eval (cx, a[1]));
	RET (v);
}
LZ (f_ifna)
{
	(void) n;
	Val v = eval (cx, a[0]);
	Val s = v.t == V_REF ? scalar (cx, v) : v;
	if (s.t == V_ERR && s.e == E_NA) RET (eval (cx, a[1]));
	RET (v);
}
LZ (f_switch)
{
	Val x = scalar (cx, eval (cx, a[0]));
	if (x.t == V_ERR) RET (x);
	int i = 1;
	for (; i + 1 < n; i += 2)
	{
		Val c = scalar (cx, eval (cx, a[i]));
		if (c.t == V_ERR) RET (c);
		if (compare (x, c) == 0 && ((x.t == V_STR) == (c.t == V_STR))) RET (eval (cx, a[i + 1]));
	}
	if (i < n) RET (eval (cx, a[i]));
	RETE (E_NA);
}
LZ (f_choose)
{
	double k; int e = 0;
	Val iv = eval (cx, a[0]);
	if (!to_num (cx, iv, &k, &e)) RETE (e);
	int i = (int) trunc (k);
	if (i < 1 || i >= n) RETE (E_VALUE);
	RET (eval (cx, a[i]));
}

} // namespace ss

#endif
