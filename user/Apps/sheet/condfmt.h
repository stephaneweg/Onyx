//
// condfmt.h -- conditional formatting: a range's rules (the value compared with numbers or formulas, a text
// contained, the top / bottom N, above / below the average, the values met twice or once, a formula true;
// colour scales, data bars) and the look they give a cell. A rule's figures over its range (lowest, highest,
// middle, average, its N-th value, the values met) are computed once a recalculation (the book's epoch) and
// a change of the rules (g_cfGen); the formulas of a rule are written for its top left cell and move with
// the cell looked at, as Excel's do.
//
#ifndef _sheet_condfmt_h
#define _sheet_condfmt_h

#include "ods.h"

namespace ss {

static unsigned g_cfGen = 1;				// bumped at each change of the rules (their figures computed again)
static void cf_changed () { g_cfGen++; }

// What the rules give a cell: a fill, a colour, bold / italic (-1: not set), a data bar (its length 0..1).
struct CfLook { unsigned fill, color; int bold, italic; double bar; unsigned barColor; };
static void cflook_init (CfLook &l) { l.fill = l.color = AUTO; l.bold = l.italic = -1; l.bar = -1; l.barColor = 0; }

// A value as the rules compare them: t 0 empty, 1 number, 2 text, 3 TRUE / FALSE, 4 an error.
struct CfVal { int t; double d; const char *s; };
static CfVal cf_cellval (Cell *x)
{
	CfVal v; v.t = 0; v.d = 0; v.s = "";
	if (!x || x->kind == K_NONE) return v;
	if (x->vt == V_NUM) { v.t = 1; v.d = x->num; }
	else if (x->vt == V_STR) { v.t = 2; v.s = x->str ? x->str : ""; }
	else if (x->vt == V_BOOL) { v.t = 3; v.d = x->num; }
	else if (x->vt == V_ERR) v.t = 4;
	return v;
}
// -1, 0, 1: numbers before texts; texts without case.
static int cf_cmp (const CfVal &a, const CfVal &b)
{
	bool an = a.t == 1 || a.t == 3, bn = b.t == 1 || b.t == 3;
	if (an && bn) return a.d < b.d ? -1 : a.d > b.d ? 1 : 0;
	if (an) return -1;
	if (bn) return 1;
	return ci_cmp (a.s, (int) strlen (a.s), b.s, (int) strlen (b.s));
}
// Does the formula give another value in another cell (relative references)?
static bool formula_moves (const Formula *f)
{
	for (int i = 0; i < f->nt; i++)
	{
		const Tok &k = f->tok[i];
		if (k.t == TK_FUNC && k.fn >= 0 && (!strcmp (fn_name (k.fn), "ROW") || !strcmp (fn_name (k.fn), "COLUMN"))) return true;
		if (k.t != TK_REF && k.t != TK_AREA) continue;
		if (!(k.fl & TF_COLS) && (!(k.fl & TF_AR0) || (k.t == TK_AREA && !(k.fl & TF_AR1)))) return true;
		if (!(k.fl & TF_ROWS) && (!(k.fl & TF_AC0) || (k.t == TK_AREA && !(k.fl & TF_AC1)))) return true;
	}
	return false;
}

// ---- a rule's figures --------------------------------------------------------------------------------------
struct CfKey { bool str; double d; const char *s; };
struct CfStat
{
	const Sheet *s; int idx; unsigned epoch, gen; bool used;
	double lo, hi, mid, avg, thr; int nnum;
	CfKey *keys; int nkeys; char *pool;			// (the values met, sorted: the duplicates)
	Formula *f[2]; bool moves[2];				// the operands written as formulas
	CfVal v[2]; char sv[2][120];				// ... their values (or constants) when they do not move
};
enum { CF_SLOTS = 48 };
static CfStat g_cfs[CF_SLOTS];
static int g_cfNext;
static void cfstat_free (CfStat &st)
{
	free (st.keys); free (st.pool); formula_free (st.f[0]); formula_free (st.f[1]);
	memset (&st, 0, sizeof st);
}
static int cfkey_cmp (const void *pa, const void *pb)
{
	const CfKey *a = (const CfKey *) pa, *b = (const CfKey *) pb;
	if (a->str != b->str) return a->str ? 1 : -1;
	if (!a->str) return a->d < b->d ? -1 : a->d > b->d ? 1 : 0;
	return strcmp (a->s, b->s);
}
static int dbl_cmp (const void *pa, const void *pb) { double a = *(const double *) pa, b = *(const double *) pb; return a < b ? -1 : a > b ? 1 : 0; }
// A value from a formula's result (a range: its first cell).
static CfVal cf_from_val (Ctx &cx, const Val &v0, char *buf, int cap)
{
	Val v = scalar (cx, v0);
	CfVal o; o.t = 0; o.d = 0; o.s = "";
	if (v.t == V_NUM) { o.t = 1; o.d = v.d; }
	else if (v.t == V_BOOL) { o.t = 3; o.d = v.d; }
	else if (v.t == V_STR) { o.t = 2; int n = imin (v.n, cap - 1); memcpy (buf, v.s, n); buf[n] = 0; o.s = buf; }
	else if (v.t == V_ERR) o.t = 4;
	return o;
}
static CfVal cf_eval (Book &b, Sheet *s, Formula *f, int r, int c, char *buf, int cap)
{
	Arena::Mark m = g_arena.mark ();
	Ctx cx; cx.b = &b; cx.sh = s; cx.r = r; cx.c = c; cx.f = f; cx.ar = &g_arena; cx.depth = 0;
	Val v = eval (cx, f->root);
	CfVal o = cf_from_val (cx, v, buf, cap);
	g_arena.release (m);
	return o;
}
static void cf_compute (Book &b, Sheet *s, const CondFmt &cf, CfStat &st)
{
	// the numbers of the range, sorted; the values met
	int cap = 256, n = 0, nk = 0, kcap = 256, pl = 0, pcap = 4096;
	double *num = (double *) malloc (cap * sizeof (double));
	bool dup = cf.type == CF_DUP;
	CfKey *keys = dup ? (CfKey *) malloc (kcap * sizeof (CfKey)) : 0;
	char *pool = dup ? (char *) malloc (pcap) : 0;
	for (int i = 0; i < s->cells.cap; i++)
	{
		Cell *x = s->cells.t[i];
		if (!x || !rect_has (cf.r, x->r, x->c)) continue;
		CfVal v = cf_cellval (x);
		if (v.t == 1) { if (n == cap) { cap *= 2; num = (double *) realloc (num, cap * sizeof (double)); } num[n++] = v.d; }
		if (dup && v.t != 0 && v.t != 4)
		{
			if (nk == kcap) { kcap *= 2; keys = (CfKey *) realloc (keys, kcap * sizeof (CfKey)); }
			CfKey &k = keys[nk++]; k.str = v.t == 2; k.d = v.d; k.s = 0;
			if (k.str)
			{
				int l = (int) strlen (v.s);
				if (pl + l + 1 > pcap) { while (pl + l + 1 > pcap) pcap *= 2; pool = (char *) realloc (pool, pcap); }
				for (int j = 0; j < l; j++) { char ch = v.s[j]; pool[pl + j] = ch >= 'A' && ch <= 'Z' ? (char) (ch + 32) : ch; }
				pool[pl + l] = 0;
				k.d = pl; pl += l + 1;			// (an offset: the pool may move)
			}
		}
	}
	if (dup) { for (int i = 0; i < nk; i++) if (keys[i].str) keys[i].s = pool + (int) keys[i].d; if (nk > 1) qsort (keys, nk, sizeof (CfKey), cfkey_cmp); }
	if (n > 1) qsort (num, n, sizeof (double), dbl_cmp);
	st.nnum = n; st.keys = keys; st.nkeys = nk; st.pool = pool;
	st.lo = n ? num[0] : 0; st.hi = n ? num[n - 1] : 0;
	double sum = 0; for (int i = 0; i < n; i++) sum += num[i];
	st.avg = n ? sum / n : 0;
	st.mid = n ? (n & 1 ? num[n / 2] : (num[n / 2 - 1] + num[n / 2]) / 2) : 0;
	if (cf.type == CF_TOP && n)
	{
		double want = strtod (cf.a, 0);
		int k = cf.pct ? (int) (n * want / 100) : (int) want;
		k = iclamp (k, 1, n);
		st.thr = cf.op == 0 ? num[n - k] : num[k - 1];
	}
	free (num);
	// the operands
	for (int i = 0; i < 2; i++)
	{
		const char *t = i ? cf.b : cf.a;
		st.v[i].t = 0; st.v[i].d = 0; st.v[i].s = "";
		while (*t == ' ') t++;
		if (!*t) continue;
		if (*t == '=' || cf.type == CF_FORMULA)
		{
			if (*t == '=') t++;
			st.f[i] = formula_parse (b, t, (int) strlen (t));
			if (!st.f[i]) { st.v[i].t = 4; continue; }
			st.moves[i] = cf.type == CF_FORMULA || formula_moves (st.f[i]);
			if (!st.moves[i]) st.v[i] = cf_eval (b, s, st.f[i], cf.r.r0, cf.r.c0, st.sv[i], sizeof st.sv[i]);
			continue;
		}
		double d;
		if (input_number (t, (int) strlen (t), &d)) { st.v[i].t = 1; st.v[i].d = d; continue; }
		int l = (int) strlen (t);
		if (l >= 2 && t[0] == '"' && t[l - 1] == '"') { t++; l -= 2; }
		l = imin (l, (int) sizeof st.sv[i] - 1);
		memcpy (st.sv[i], t, l); st.sv[i][l] = 0;
		st.v[i].t = 2; st.v[i].s = st.sv[i];
	}
}
// The rule's figures, computed when the book or the rules changed.
static CfStat &cf_stat (Book &b, Sheet *s, int idx)
{
	for (int i = 0; i < CF_SLOTS; i++)
	{
		CfStat &st = g_cfs[i];
		if (st.used && st.s == s && st.idx == idx)
		{
			if (st.epoch == b.epoch && st.gen == g_cfGen) return st;
			cfstat_free (st);
			st.used = true; st.s = s; st.idx = idx; st.epoch = b.epoch; st.gen = g_cfGen;
			cf_compute (b, s, s->cf[idx], st);
			return st;
		}
	}
	CfStat &st = g_cfs[g_cfNext]; g_cfNext = (g_cfNext + 1) % CF_SLOTS;
	cfstat_free (st);
	st.used = true; st.s = s; st.idx = idx; st.epoch = b.epoch; st.gen = g_cfGen;
	cf_compute (b, s, s->cf[idx], st);
	return st;
}
// An operand's value for the cell (r, c): a formula moved from the rule's top left cell.
static CfVal cf_operand (Book &b, Sheet *s, const CondFmt &cf, CfStat &st, int i, int r, int c, char *buf, int cap)
{
	if (!st.f[i] || !st.moves[i]) return st.v[i];
	Formula *g = formula_copy (st.f[i], r - cf.r.r0, c - cf.r.c0);
	CfVal v = cf_eval (b, s, g, r, c, buf, cap);
	formula_free (g);
	return v;
}

// ---- a cell -------------------------------------------------------------------------------------------------
static bool cf_holds (Book &b, Sheet *s, const CondFmt &cf, CfStat &st, Cell *x, int r, int c)
{
	CfVal v = cf_cellval (x);
	switch (cf.type)
	{
	case CF_CELL:
	{
		if (v.t == 0 || v.t == 4) return false;
		char ba[120], bb[120];
		CfVal a = cf_operand (b, s, cf, st, 0, r, c, ba, sizeof ba);
		if (a.t == 0 || a.t == 4) return false;
		int k = cf_cmp (v, a);
		switch (cf.op)
		{
		case CO_GT: return k > 0;
		case CO_GE: return k >= 0;
		case CO_LT: return k < 0;
		case CO_LE: return k <= 0;
		case CO_EQ: return k == 0;
		case CO_NE: return k != 0;
		default:
		{
			CfVal z = cf_operand (b, s, cf, st, 1, r, c, bb, sizeof bb);
			if (z.t == 0 || z.t == 4) return false;
			CfVal lo = cf_cmp (a, z) <= 0 ? a : z, hi = cf_cmp (a, z) <= 0 ? z : a;
			bool in = cf_cmp (v, lo) >= 0 && cf_cmp (v, hi) <= 0;
			return cf.op == CO_BETWEEN ? in : !in;
		}
		}
	}
	case CF_TEXT:
	{
		if (v.t == 0) return cf.op == CT_NOTCONTAINS;
		Shown sh; cell_shown (b, s, x, sh, 255);
		const char *t = v.t == 2 ? v.s : sh.text, *w = st.v[0].t == 2 ? st.v[0].s : cf.a;
		int tl = (int) strlen (t), wl = (int) strlen (w);
		bool found = false;
		if (cf.op == CT_BEGINS) found = tl >= wl && !ci_cmp (t, wl, w, wl);
		else if (cf.op == CT_ENDS) found = tl >= wl && !ci_cmp (t + tl - wl, wl, w, wl);
		else for (int i = 0; i + wl <= tl && !found; i++) found = !ci_cmp (t + i, wl, w, wl);
		return cf.op == CT_NOTCONTAINS ? !found : found;
	}
	case CF_TOP: return v.t == 1 && st.nnum && (cf.op == 0 ? v.d >= st.thr : v.d <= st.thr);
	case CF_AVERAGE: return v.t == 1 && st.nnum && (cf.op == 0 ? v.d > st.avg : v.d < st.avg);
	case CF_DUP:
	{
		if (v.t == 0 || v.t == 4 || !st.nkeys) return false;
		char low[256]; CfKey k; k.str = v.t == 2; k.d = v.d; k.s = low;
		if (k.str) { int l = imin ((int) strlen (v.s), 255); for (int j = 0; j < l; j++) { char ch = v.s[j]; low[j] = ch >= 'A' && ch <= 'Z' ? (char) (ch + 32) : ch; } low[l] = 0; }
		// how many times it is met: the first and the last place of it in the sorted keys
		int lo = 0, hi = st.nkeys;
		while (lo < hi) { int m = (lo + hi) / 2; if (cfkey_cmp (&st.keys[m], &k) < 0) lo = m + 1; else hi = m; }
		int first = lo; hi = st.nkeys;
		while (lo < hi) { int m = (lo + hi) / 2; if (cfkey_cmp (&st.keys[m], &k) <= 0) lo = m + 1; else hi = m; }
		int count = lo - first;
		return cf.op == 0 ? count > 1 : count == 1;
	}
	case CF_FORMULA:
	{
		char buf[120];
		CfVal a = cf_operand (b, s, cf, st, 0, r, c, buf, sizeof buf);
		return (a.t == 1 || a.t == 3) && a.d != 0;
	}
	}
	return false;
}
static unsigned mix_rgb (unsigned a, unsigned b, double t)
{
	t = t < 0 ? 0 : t > 1 ? 1 : t;
	int r = (int) (((a >> 16) & 255) + (((int) ((b >> 16) & 255) - (int) ((a >> 16) & 255)) * t + 0.5));
	int g = (int) (((a >> 8) & 255) + (((int) ((b >> 8) & 255) - (int) ((a >> 8) & 255)) * t + 0.5));
	int bl = (int) ((a & 255) + (((int) (b & 255) - (int) (a & 255)) * t + 0.5));
	return (unsigned) (iclamp (r, 0, 255) << 16 | iclamp (g, 0, 255) << 8 | iclamp (bl, 0, 255));
}
// The look the sheet's rules give the cell (r, c) (x: its cell, or 0).
static void cf_cell (Book &b, Sheet *s, Cell *x, int r, int c, CfLook &L)
{
	cflook_init (L);
	for (int i = 0; i < s->ncf; i++)
	{
		const CondFmt &cf = s->cf[i];
		if (!rect_has (cf.r, r, c)) continue;
		if (cf.type != CF_FORMULA && (!x || x->kind == K_NONE)) continue;	// (an empty cell: only a formula rule)
		CfStat &st = cf_stat (b, s, i);
		if (cf.type == CF_SCALE || cf.type == CF_BAR)
		{
			CfVal v = cf_cellval (x);
			if (v.t != 1 || !st.nnum) continue;
			if (cf.type == CF_SCALE && L.fill == AUTO)
			{
				if (st.hi <= st.lo) L.fill = cf.op == 3 ? cf.c1 : mix_rgb (cf.c0, cf.c2, 0.5);
				else if (cf.op == 3)
					L.fill = v.d <= st.mid ? mix_rgb (cf.c0, cf.c1, st.mid > st.lo ? (v.d - st.lo) / (st.mid - st.lo) : 1)
							       : mix_rgb (cf.c1, cf.c2, st.hi > st.mid ? (v.d - st.mid) / (st.hi - st.mid) : 0);
				else L.fill = mix_rgb (cf.c0, cf.c2, (v.d - st.lo) / (st.hi - st.lo));
			}
			if (cf.type == CF_BAR && L.bar < 0)			// (Excel's: the lowest a tenth of the cell, the highest nine tenths more)
			{
				L.bar = st.hi > st.lo ? 0.1 + 0.9 * (v.d - st.lo) / (st.hi - st.lo) : 1;
				L.barColor = cf.c0;
			}
			continue;
		}
		if (!cf_holds (b, s, cf, st, x, r, c)) continue;
		if (L.fill == AUTO && cf.fill != AUTO) L.fill = cf.fill;
		if (L.color == AUTO && cf.color != AUTO) L.color = cf.color;
		if (L.bold < 0 && cf.bold >= 0) L.bold = cf.bold;
		if (L.italic < 0 && cf.italic >= 0) L.italic = cf.italic;
	}
}
// The cell's own style under the rules' look (bold, italic, colour).
static bool cf_style (const Style &st, const CfLook &L, Style &out)
{
	if (L.color == AUTO && L.bold < 0 && L.italic < 0) return false;
	out = st;
	if (L.color != AUTO) out.color = L.color;
	if (L.bold >= 0) out.bold = (unsigned char) L.bold;
	if (L.italic >= 0) out.italic = (unsigned char) L.italic;
	return true;
}

// ---- the presets offered (Format > Conditional Formatting) ------------------------------------------------------
struct CfPreset { const char *name; unsigned fill, color; signed char bold; };
static const CfPreset CF_LOOKS[] = {
	{ "Light red fill, dark red text", 0xFFC7CE, 0x9C0006, -1 },
	{ "Yellow fill, dark yellow text", 0xFFEB9C, 0x9C5700, -1 },
	{ "Green fill, dark green text", 0xC6EFCE, 0x006100, -1 },
	{ "Light red fill", 0xFFC7CE, AUTO, -1 },
	{ "Red text", AUTO, 0xC00000, -1 },
	{ "Bold text", AUTO, AUTO, 1 },
	{ "Blue fill, bold", 0xDDEBF7, 0x1F4E79, 1 },
};
enum { NCF_LOOKS = 7 };
struct CfScale { const char *name; int n; unsigned c0, c1, c2; };
static const CfScale CF_SCALES[] = {
	{ "Green - yellow - red", 3, 0x63BE7B, 0xFFEB84, 0xF8696B },
	{ "Red - yellow - green", 3, 0xF8696B, 0xFFEB84, 0x63BE7B },
	{ "Blue - white - red", 3, 0x5A8AC6, 0xFCFCFF, 0xF8696B },
	{ "White - green", 2, 0xFCFCFF, 0, 0x63BE7B },
	{ "White - red", 2, 0xFCFCFF, 0, 0xF8696B },
	{ "Yellow - green", 2, 0xFFEF9C, 0, 0x63BE7B },
};
enum { NCF_SCALES = 6 };
struct CfBar { const char *name; unsigned c; };
static const CfBar CF_BARS[] = { { "Blue bars", 0x638EC6 }, { "Green bars", 0x63C384 }, { "Red bars", 0xFF555A }, { "Orange bars", 0xFFB628 }, { "Purple bars", 0xD6007B } };
enum { NCF_BARS = 5 };

} // namespace ss

#endif
