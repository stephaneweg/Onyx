//
// eval.h -- a formula computed: its tree walked (IF and its kind lazily: only the branch taken), the
// values of the cells it names (a formula cell computed first when it is not yet: the recalculation is
// driven by what each formula needs), the operators on numbers, texts, arrays (element by element, as
// Excel 365: SUM(A1:A5*B1:B5) works), a function applied to arrays when it takes one value (lifted).
// A reference to a cell being computed is a circular one (#CIRC!). Chains longer than the stack allows
// are computed from their far end first (the recalculation's explicit stack: see recalc ()).
//
#ifndef _sheet_eval_h
#define _sheet_eval_h

#include "formula.h"

namespace ss {

enum { V_REF = 5, V_ARR = 6, V_MISS = 7 };

struct Val
{
	unsigned char t, e;				// V_*; the error (V_ERR)
	int n;						// the text's length (V_STR)
	double d;					// V_NUM, V_BOOL (0 / 1)
	const char *s;					// V_STR
	Sheet *sh; int r0, c0, r1, c1;			// V_REF
	Val *a; int ar, ac;				// V_ARR (row by row)
};

static inline Val vnum (double d) { Val v; memset (&v, 0, sizeof v); v.t = V_NUM; v.d = d; return v; }
static inline Val vbool (bool b) { Val v; memset (&v, 0, sizeof v); v.t = V_BOOL; v.d = b ? 1 : 0; return v; }
static inline Val verr (int e) { Val v; memset (&v, 0, sizeof v); v.t = V_ERR; v.e = (unsigned char) e; return v; }
static inline Val vempty () { Val v; memset (&v, 0, sizeof v); v.t = V_EMPTY; return v; }
static inline Val vstr (const char *s, int n) { Val v; memset (&v, 0, sizeof v); v.t = V_STR; v.s = s; v.n = n; return v; }
static inline Val vref (Sheet *sh, int r0, int c0, int r1, int c1) { Val v; memset (&v, 0, sizeof v); v.t = V_REF; v.sh = sh; v.r0 = r0; v.c0 = c0; v.r1 = r1; v.c1 = c1; return v; }
static inline bool is_num_val (const Val &v) { return v.t == V_NUM; }

// ---- the context --------------------------------------------------------------------------------------------
struct Ctx
{
	Book *b;
	Sheet *sh;					// the formula's sheet, cell
	int r, c;
	Formula *f;
	Arena *ar;
	int depth;					// cells being computed under this one
};

static Arena g_arena;					// the values' scratch (the marks nest with the cells)
static Cell *g_defer;					// a cell too deep in a chain to compute now (see recalc)
enum { MAX_DEPTH = 200 };
static void (*g_now) (int *y, int *mo, int *d, int *h, int *mi, int *s);	// the clock (the app's)
static unsigned g_rand = 12345;

struct FnDef;
static const FnDef *fn_def (int i);
static void cell_compute (Book &b, Sheet *s, Cell *x, int depth);

static Val eval (Ctx &cx, int node);

static Val str_val (Ctx &cx, const char *s, int n) { return vstr (cx.ar->str (s, n), n); }
static Val str_val (Ctx &cx, const Buf &b) { return str_val (cx, b.b ? b.b : "", b.n); }
static Val arr_new (Ctx &cx, int rows, int cols)
{
	Val v; memset (&v, 0, sizeof v);
	v.t = V_ARR; v.ar = rows; v.ac = cols;
	v.a = (Val *) cx.ar->alloc (rows * cols * (int) sizeof (Val));
	for (int i = 0; i < rows * cols; i++) v.a[i] = vempty ();
	return v;
}

// ---- the cells' values ------------------------------------------------------------------------------------
// A cell's value (a formula computed first if it is not yet; empty: V_EMPTY).
static Val cell_value (Ctx &cx, Sheet *s, int r, int c)
{
	Cell *x = s->cells.get (r, c);
	if (!x || x->kind == K_NONE) return vempty ();
	if (x->kind == K_FORM && x->epoch != cx.b->epoch)
	{
		if (g_defer) return verr (E_DEFER);
		if (x->fl & CF_BUSY) return verr (E_CIRC);
		if (cx.depth >= MAX_DEPTH) { g_defer = x; return verr (E_DEFER); }
		cell_compute (*cx.b, s, x, cx.depth + 1);
		if (g_defer) return verr (E_DEFER);
	}
	switch (x->vt)
	{
	case V_NUM: return vnum (x->num);
	case V_BOOL: return vbool (x->num != 0);
	case V_STR: return vstr (x->str, (int) strlen (x->str));
	case V_ERR: return verr (x->err);
	}
	return vempty ();
}

// The used part of a range (clipped to the sheet's cells with content).
static bool range_clip (const Val &v, int *r1, int *c1)
{
	sheet_bounds (v.sh);
	*r1 = imin (v.r1, v.sh->maxR); *c1 = imin (v.c1, v.sh->maxC);
	return *r1 >= v.r0 && *c1 >= v.c0;
}

// Every value of a range, an array or a single value: f (value, row, column) -- row by row; a range's
// empty cells skipped unless `empties` (then only within the used area). f returns false to stop.
template <class F> static void each (Ctx &cx, const Val &v, bool empties, F f)
{
	if (v.t == V_REF)
	{
		int r1, c1;
		if (!range_clip (v, &r1, &c1)) return;
		long long area = (long long) (r1 - v.r0 + 1) * (c1 - v.c0 + 1);
		if (!empties && area > (long long) v.sh->cells.n * 3 + 64)	// (sparse: from the cells themselves)
		{
			int cap = v.sh->cells.cap, cnt = 0;
			Cell **list = (Cell **) cx.ar->alloc (imax (1, v.sh->cells.n) * (int) sizeof (Cell *));
			for (int i = 0; i < cap; i++)
			{
				Cell *x = v.sh->cells.t[i];
				if (x && x->kind != K_NONE && x->r >= v.r0 && x->r <= r1 && x->c >= v.c0 && x->c <= c1) list[cnt++] = x;
			}
			qsort (list, cnt, sizeof (Cell *), [] (const void *a, const void *b) -> int {
				const Cell *x = *(Cell *const *) a, *y = *(Cell *const *) b;
				return x->r != y->r ? (x->r < y->r ? -1 : 1) : x->c < y->c ? -1 : x->c > y->c ? 1 : 0; });
			for (int i = 0; i < cnt; i++)
			{
				Val e = cell_value (cx, v.sh, list[i]->r, list[i]->c);
				if (!f (e, list[i]->r - v.r0, list[i]->c - v.c0)) return;
			}
			return;
		}
		for (int r = v.r0; r <= r1; r++)
			for (int c = v.c0; c <= c1; c++)
			{
				Val e = cell_value (cx, v.sh, r, c);
				if (e.t == V_EMPTY && !empties) continue;
				if (!f (e, r - v.r0, c - v.c0)) return;
			}
		return;
	}
	if (v.t == V_ARR)
	{
		for (int i = 0; i < v.ar * v.ac; i++) if (!f (v.a[i], i / v.ac, i % v.ac)) return;
		return;
	}
	f (v, 0, 0);
}

// A range's size (rows, columns); a value's: 1 x 1.
static void dims (const Val &v, int *rows, int *cols)
{
	if (v.t == V_REF) { *rows = v.r1 - v.r0 + 1; *cols = v.c1 - v.c0 + 1; }
	else if (v.t == V_ARR) { *rows = v.ar; *cols = v.ac; }
	else { *rows = 1; *cols = 1; }
}
// The element (i, j) of a range or array (a single value repeats; outside: #N/A).
static Val elem (Ctx &cx, const Val &v, int i, int j)
{
	if (v.t == V_REF)
	{
		int rows = v.r1 - v.r0 + 1, cols = v.c1 - v.c0 + 1;
		if (rows == 1) i = 0;
		if (cols == 1) j = 0;
		if (i >= rows || j >= cols) return verr (E_NA);
		return cell_value (cx, v.sh, v.r0 + i, v.c0 + j);
	}
	if (v.t == V_ARR)
	{
		if (v.ar == 1) i = 0;
		if (v.ac == 1) j = 0;
		if (i >= v.ar || j >= v.ac) return verr (E_NA);
		return v.a[i * v.ac + j];
	}
	return v;
}
static bool is_multi (const Val &v) { return (v.t == V_REF && (v.r0 != v.r1 || v.c0 != v.c1)) || (v.t == V_ARR && v.ar * v.ac > 1); }

// A single value from a range (implicit intersection: the formula's row / column) or an array (its first).
static Val scalar (Ctx &cx, const Val &v)
{
	if (v.t == V_REF)
	{
		if (v.r0 == v.r1 && v.c0 == v.c1) return cell_value (cx, v.sh, v.r0, v.c0);
		if (v.c0 == v.c1 && cx.r >= v.r0 && cx.r <= v.r1) return cell_value (cx, v.sh, cx.r, v.c0);
		if (v.r0 == v.r1 && cx.c >= v.c0 && cx.c <= v.c1) return cell_value (cx, v.sh, v.r0, cx.c);
		return verr (E_VALUE);
	}
	if (v.t == V_ARR) return v.ar * v.ac > 0 ? v.a[0] : verr (E_VALUE);
	if (v.t == V_MISS) return vempty ();
	return v;
}

// ---- conversions --------------------------------------------------------------------------------------------
static bool input_number (const char *s, int n, double *out);	// (input.h: "12%", "3/4/2026", "1,234")

// To a number: false with *err when it cannot be one.
static bool to_num (Ctx &cx, const Val &v0, double *d, int *err)
{
	Val v = scalar (cx, v0);
	switch (v.t)
	{
	case V_NUM: case V_BOOL: *d = v.d; return true;
	case V_EMPTY: case V_MISS: *d = 0; return true;
	case V_STR: if (input_number (v.s, v.n, d)) return true; *err = E_VALUE; return false;
	case V_ERR: *err = v.e; return false;
	}
	*err = E_VALUE; return false;
}
static bool to_int (Ctx &cx, const Val &v, long long *i, int *err)
{
	double d; if (!to_num (cx, v, &d, err)) return false;
	if (d > 9e18 || d < -9e18) { *err = E_NUM; return false; }
	*i = (long long) floor (d);				// (Excel truncates toward... floor for the positive: its INT)
	if (d < 0) *i = (long long) (d > -1e-300 ? 0 : -(long long) floor (-d + 1e-12));
	*i = (long long) (d >= 0 ? floor (d + 1e-9) : -floor (-d + 1e-9));
	return true;
}
static void num_text (double d, char *o) { num_full (d, o, 32); }
// To a text (numbers as the General format writes them, TRUE / FALSE).
static bool to_str (Ctx &cx, const Val &v0, const char **s, int *n, int *err, char *tmp)
{
	Val v = scalar (cx, v0);
	switch (v.t)
	{
	case V_STR: *s = v.s; *n = v.n; return true;
	case V_NUM: num_text (v.d, tmp); *s = tmp; *n = (int) strlen (tmp); return true;
	case V_BOOL: *s = v.d ? "TRUE" : "FALSE"; *n = (int) strlen (*s); return true;
	case V_EMPTY: case V_MISS: *s = ""; *n = 0; return true;
	case V_ERR: *err = v.e; return false;
	}
	*err = E_VALUE; return false;
}
static bool to_bool (Ctx &cx, const Val &v0, bool *b, int *err)
{
	Val v = scalar (cx, v0);
	switch (v.t)
	{
	case V_NUM: case V_BOOL: *b = v.d != 0; return true;
	case V_EMPTY: case V_MISS: *b = false; return true;
	case V_STR:
		if (v.n == 4 && ascii_ieq (v.s, "TRUE", 4)) { *b = true; return true; }
		if (v.n == 5 && ascii_ieq (v.s, "FALSE", 5)) { *b = false; return true; }
		*err = E_VALUE; return false;
	case V_ERR: *err = v.e; return false;
	}
	*err = E_VALUE; return false;
}

// Excel's comparison: numbers < texts < booleans; texts without their case. -1, 0, 1.
static int compare (const Val &a, const Val &b)
{
	Val x = a, y = b;
	if (x.t == V_EMPTY || x.t == V_MISS) { if (y.t == V_STR) x = vstr ("", 0); else if (y.t == V_BOOL) x = vbool (false); else x = vnum (0); }
	if (y.t == V_EMPTY || y.t == V_MISS) { if (x.t == V_STR) y = vstr ("", 0); else if (x.t == V_BOOL) y = vbool (false); else y = vnum (0); }
	int rx = x.t == V_NUM ? 0 : x.t == V_STR ? 1 : 2, ry = y.t == V_NUM ? 0 : y.t == V_STR ? 1 : 2;
	if (rx != ry) return rx < ry ? -1 : 1;
	if (rx == 1) return ci_cmp (x.s, x.n, y.s, y.n);
	return x.d < y.d ? -1 : x.d > y.d ? 1 : 0;
}

// ---- the operators ------------------------------------------------------------------------------------------
static Val op_scalar (Ctx &cx, int op, const Val &a, const Val &b)
{
	if (op == OP_CAT)
	{
		char t1[40], t2[40]; const char *s1, *s2; int n1, n2, e = 0;
		if (!to_str (cx, a, &s1, &n1, &e, t1)) return verr (e);
		if (!to_str (cx, b, &s2, &n2, &e, t2)) return verr (e);
		char *o = (char *) cx.ar->alloc (n1 + n2 + 1);
		memcpy (o, s1, n1); memcpy (o + n1, s2, n2); o[n1 + n2] = 0;
		return vstr (o, n1 + n2);
	}
	if (op >= OP_EQ && op <= OP_GE)
	{
		Val x = scalar (cx, a), y = scalar (cx, b);
		if (x.t == V_ERR) return x;
		if (y.t == V_ERR) return y;
		int c = compare (x, y);
		bool r = op == OP_EQ ? c == 0 : op == OP_NE ? c != 0 : op == OP_LT ? c < 0 : op == OP_GT ? c > 0 : op == OP_LE ? c <= 0 : c >= 0;
		return vbool (r);
	}
	double x, y; int e = 0;
	if (!to_num (cx, a, &x, &e)) return verr (e);
	if (!to_num (cx, b, &y, &e)) return verr (e);
	double r;
	switch (op)
	{
	case OP_ADD: r = x + y; break;
	case OP_SUB: r = x - y; break;
	case OP_MUL: r = x * y; break;
	case OP_DIV: if (y == 0) return verr (E_DIV0); r = x / y; break;
	case OP_POW:
		if (x == 0 && y == 0) return verr (E_NUM);
		if (x == 0 && y < 0) return verr (E_DIV0);
		if (x < 0 && y != floor (y)) return verr (E_NUM);
		r = pow (x, y); break;
	default: return verr (E_VALUE);
	}
	if (isnan (r) || isinf (r)) return verr (E_NUM);
	return vnum (r);
}
static Val op_bin (Ctx &cx, int op, const Val &a, const Val &b)
{
	if (!is_multi (a) && !is_multi (b)) return op_scalar (cx, op, a, b);
	int ra, ca, rb, cb;
	dims (a, &ra, &ca); dims (b, &rb, &cb);
	int R = imax (ra, rb), C = imax (ca, cb);
	if ((long long) R * C > 4000000) return verr (E_NUM);
	Val o = arr_new (cx, R, C);
	for (int i = 0; i < R; i++)
		for (int j = 0; j < C; j++)
			o.a[i * C + j] = op_scalar (cx, op, elem (cx, a, i, j), elem (cx, b, i, j));
	return o;
}
static Val op_un1 (Ctx &cx, int op, const Val &a)
{
	double x; int e = 0;
	if (op == OP_POS) { Val v = scalar (cx, a); return v; }
	if (!to_num (cx, a, &x, &e)) return verr (e);
	if (op == OP_NEG) return vnum (-x);
	return vnum (x / 100);
}
static Val op_un (Ctx &cx, int op, const Val &a)
{
	if (!is_multi (a)) return op_un1 (cx, op, a);
	int R, C; dims (a, &R, &C);
	Val o = arr_new (cx, R, C);
	for (int i = 0; i < R; i++) for (int j = 0; j < C; j++) o.a[i * C + j] = op_un1 (cx, op, elem (cx, a, i, j));
	return o;
}

// ---- the tree -----------------------------------------------------------------------------------------------
static Sheet *tok_sheet (Ctx &cx, const Tok &k)
{
	if (!k.sheet) return cx.sh;
	return book_sheet_by_id (*cx.b, k.sheet);
}
static Val call_fn (Ctx &cx, const Node &n);

static Val eval (Ctx &cx, int node)
{
	const Node &n = cx.f->nd[node];
	switch (n.k)
	{
	case N_TOK:
	{
		const Tok &k = cx.f->tok[n.tok];
		switch (k.t)
		{
		case TK_NUM: return vnum (k.num);
		case TK_STR: return vstr (cx.f->pool + k.s, k.sn);
		case TK_BOOL: return vbool (k.num != 0);
		case TK_ERR: return verr ((int) k.num);
		case TK_NAME:						// a defined name: its formula, computed here
		{
			static int depth;
			const DefName *d = name_find (*cx.b, cx.f->pool + k.s, k.sn, cx.sh ? cx.sh->id : 0);
			if (!d || !d->f) return verr (E_NAME);
			if (depth > 16) return verr (E_REF);			// (a name that names itself)
			Ctx c2 = cx; c2.f = d->f;
			if (d->scope) { Sheet *hs = book_sheet_by_id (*cx.b, d->scope); if (hs) c2.sh = hs; }
			depth++;
			Val v = eval (c2, d->f->root);
			depth--;
			return v;
		}
		case TK_REF: case TK_AREA:
		{
			if (k.fl & TF_BAD) return verr (E_REF);
			Sheet *s = tok_sheet (cx, k);
			if (!s) return verr (E_REF);
			return vref (s, k.r0, k.c0, k.r1, k.c1);
		}
		}
		return verr (E_VALUE);
	}
	case N_PAREN: return eval (cx, n.a);
	case N_MISS: { Val v = vempty (); v.t = V_MISS; return v; }
	case N_UN: { Val a = eval (cx, n.a); return op_un (cx, n.op, a); }
	case N_BIN:
	{
		Val a = eval (cx, n.a), b = eval (cx, n.b);
		if (n.op == OP_RANGE)					// A1:INDEX(...) -> the box around both
		{
			if (a.t == V_ERR) return a;
			if (b.t == V_ERR) return b;
			if (a.t != V_REF || b.t != V_REF || a.sh != b.sh) return verr (E_VALUE);
			return vref (a.sh, imin (a.r0, b.r0), imin (a.c0, b.c0), imax (a.r1, b.r1), imax (a.c1, b.c1));
		}
		return op_bin (cx, n.op, a, b);
	}
	case N_ARR:
	{
		int rows = n.tok, cols = n.b;
		Val o = arr_new (cx, rows, cols);
		for (int i = 0; i < rows * cols; i++)
		{
			const Tok &k = cx.f->tok[cx.f->args[n.a + i]];
			o.a[i] = k.t == TK_NUM ? vnum (k.num) : k.t == TK_STR ? vstr (cx.f->pool + k.s, k.sn) : k.t == TK_BOOL ? vbool (k.num != 0) : verr ((int) k.num);
		}
		return o;
	}
	case N_FN: return call_fn (cx, n);
	}
	return verr (E_VALUE);
}

// ---- the functions' calls -----------------------------------------------------------------------------------
enum { FN_VOL = 1, FN_LAZY = 2, FN_LIFT = 4 };
typedef void (*FnImpl) (Ctx &cx, Val *a, int n, Val &out);
typedef void (*FnLazy) (Ctx &cx, const int *nodes, int n, Val &out);
struct FnDef
{
	const char *name;
	short minA, maxA;
	unsigned char fl, cat;
	FnImpl f; FnLazy lz;
	const char *args, *help;
};

static Val call_fn (Ctx &cx, const Node &n)
{
	const Tok &k = cx.f->tok[n.tok];
	if (k.fn < 0) return verr (E_NAME);
	const FnDef *d = fn_def (k.fn);
	if (n.n < d->minA || n.n > d->maxA) return verr (E_VALUE);
	const int *args = cx.f->args + n.a;
	Val out = vempty ();
	if (d->fl & FN_LAZY) { d->lz (cx, args, n.n, out); return out; }
	Val *a = (Val *) cx.ar->alloc (imax (1, n.n) * (int) sizeof (Val));
	for (int i = 0; i < n.n; i++) a[i] = eval (cx, args[i]);
	if (d->fl & FN_LIFT)					// one value an argument: over arrays, element by element
	{
		int R = 1, C = 1; bool multi = false;
		for (int i = 0; i < n.n; i++) if (is_multi (a[i])) { int r, c; dims (a[i], &r, &c); R = imax (R, r); C = imax (C, c); multi = true; }
		if (multi)
		{
			if ((long long) R * C > 4000000) return verr (E_NUM);
			Val o = arr_new (cx, R, C);
			Val *b = (Val *) cx.ar->alloc (imax (1, n.n) * (int) sizeof (Val));
			for (int i = 0; i < R; i++)
				for (int j = 0; j < C; j++)
				{
					for (int x = 0; x < n.n; x++) b[x] = elem (cx, a[x], i, j);
					Val e = vempty ();
					d->f (cx, b, n.n, e);
					o.a[i * C + j] = e.t == V_REF || e.t == V_ARR ? scalar (cx, e) : e;
				}
			return o;
		}
	}
	d->f (cx, a, n.n, out);
	return out;
}

// ---- a cell's formula computed ------------------------------------------------------------------------------
static void set_result (Cell *x, const Val &v)
{
	cell_clear_value (x);
	switch (v.t)
	{
	case V_NUM:
		if (isnan (v.d) || isinf (v.d)) { x->vt = V_ERR; x->err = E_NUM; }
		else { x->vt = V_NUM; x->num = v.d; }
		break;
	case V_BOOL: x->vt = V_BOOL; x->num = v.d; break;
	case V_STR: x->vt = V_STR; x->str = sdup (v.s, v.n); break;
	case V_ERR: x->vt = V_ERR; x->err = v.e; break;
	default: x->vt = V_NUM; x->num = 0; break;		// (a formula giving an empty cell shows 0)
	}
}
static void cell_compute (Book &b, Sheet *s, Cell *x, int depth)
{
	Ctx cx; cx.b = &b; cx.sh = s; cx.r = x->r; cx.c = x->c; cx.f = x->f; cx.ar = &g_arena; cx.depth = depth;
	x->fl |= CF_BUSY;
	Arena::Mark m = g_arena.mark ();
	Val v = eval (cx, x->f->root);
	if (v.t == V_REF || v.t == V_ARR || v.t == V_MISS) v = scalar (cx, v);
	x->fl &= ~CF_BUSY;
	if (!g_defer)
	{
		set_result (x, v);
		x->epoch = b.epoch;
	}
	g_arena.release (m);
}

// The formula cells of a sheet, row by row.
static void collect_forms (Sheet *s)
{
	if (s->formsOk) return;
	s->nforms = 0;
	for (int i = 0; i < s->cells.cap; i++)
	{
		Cell *x = s->cells.t[i];
		if (!x || x->kind != K_FORM) continue;
		if (s->nforms == s->cforms) { s->cforms = s->cforms ? s->cforms * 2 : 64; s->forms = (Cell **) realloc (s->forms, s->cforms * sizeof (Cell *)); }
		s->forms[s->nforms++] = x;
	}
	if (s->nforms > 1) qsort (s->forms, s->nforms, sizeof (Cell *), [] (const void *a, const void *b) -> int {
		const Cell *x = *(Cell *const *) a, *y = *(Cell *const *) b;
		return x->r != y->r ? (x->r < y->r ? -1 : 1) : x->c < y->c ? -1 : x->c > y->c ? 1 : 0; });
	s->formsOk = true;
}

// Every formula computed again. A chain deeper than MAX_DEPTH: its far cell is computed first (put on an
// explicit stack), then the rest; a cell met again on that stack is in a circle: #CIRC!.
static Sheet *sheet_of_cell (Book &b, Cell *x)
{
	for (int i = 0; i < b.ns; i++) if (b.sh[i]->cells.get (x->r, x->c) == x) return b.sh[i];
	return 0;
}
static void recalc (Book &b)
{
	b.epoch++;
	if (b.epoch == 0) b.epoch = 1;
	g_defer = 0;
	Cell **stk = 0; Sheet **stS = 0; int ns = 0, cs = 0;
	for (int si = 0; si < b.ns; si++)
	{
		Sheet *s = b.sh[si];
		collect_forms (s);
		for (int i = 0; i < s->nforms; i++)
		{
			Cell *x = s->forms[i];
			if (x->epoch == b.epoch) continue;
			// (the explicit stack: a cell, then the deeper ones it waits for)
			if (cs == 0) { cs = 64; stk = (Cell **) malloc (cs * sizeof (Cell *)); stS = (Sheet **) malloc (cs * sizeof (Sheet *)); }
			stk[0] = x; stS[0] = s; ns = 1; x->fl |= CF_STACK;
			while (ns > 0)
			{
				Cell *y = stk[ns - 1]; Sheet *ys = stS[ns - 1];
				if (y->epoch == b.epoch) { y->fl &= ~CF_STACK; ns--; continue; }
				g_defer = 0;
				cell_compute (b, ys, y, 0);
				if (!g_defer) { y->fl &= ~CF_STACK; ns--; continue; }
				Cell *d = g_defer; g_defer = 0;
				if (d->fl & CF_STACK)				// (in a circle)
				{
					cell_clear_value (d); d->vt = V_ERR; d->err = E_CIRC; d->epoch = b.epoch;
					continue;
				}
				Sheet *dsh = sheet_of_cell (b, d);
				if (!dsh) { d->epoch = b.epoch; continue; }
				if (ns == cs) { cs *= 2; stk = (Cell **) realloc (stk, cs * sizeof (Cell *)); stS = (Sheet **) realloc (stS, cs * sizeof (Sheet *)); }
				stk[ns] = d; stS[ns] = dsh; ns++; d->fl |= CF_STACK;
			}
		}
	}
	free (stk); free (stS);
	g_defer = 0;
}

// ---- a formula's value once (the input line's quick results, a chart's series) ------------------------------
static Val eval_text (Book &b, Sheet *s, int r, int c, const char *text, Formula **keep = 0)
{
	Formula *f = formula_parse (b, text, -1);
	if (!f) return verr (E_VALUE);
	Ctx cx; cx.b = &b; cx.sh = s; cx.r = r; cx.c = c; cx.f = f; cx.ar = &g_arena; cx.depth = 0;
	Val v = eval (cx, f->root);
	if (keep) *keep = f; else formula_free (f);
	return v;
}

} // namespace ss

#endif
