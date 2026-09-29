//
// undo.h -- Undo / Redo: before a change, what it will touch is written down (a range's cells; a whole
// sheet -- rows, columns, merges, charts --; the whole book for the changes of sheets and the moves that
// rewrite formulas elsewhere) in a small binary form; Undo writes the present state the same way (for
// Redo) and puts the old one back. 100 steps, 64 MB at most.
//
#ifndef _sheet_undo_h
#define _sheet_undo_h

#include "ops.h"

namespace ss {

// ---- a binary writer and reader -----------------------------------------------------------------------------
struct Wr
{
	Buf b;
	void raw (const void *p, int n) { b.putn ((const char *) p, n); }
	void i32 (int v) { raw (&v, 4); }
	void u16 (unsigned short v) { raw (&v, 2); }
	void u8 (unsigned char v) { raw (&v, 1); }
	void f64 (double v) { raw (&v, 8); }
	void str (const char *s) { int n = s ? (int) strlen (s) : -1; i32 (n); if (n > 0) raw (s, n); }
};
struct Rd
{
	const char *p, *e; bool ok;
	Rd (const char *d, int n) : p (d), e (d + n), ok (true) {}
	bool raw (void *d, int n) { if (!ok || n < 0 || e - p < n) { ok = false; if (n > 0) memset (d, 0, n); return false; } if (n > 0) memcpy (d, p, n); p += n; return true; }
	int i32 () { int v = 0; raw (&v, 4); return v; }
	unsigned short u16 () { unsigned short v = 0; raw (&v, 2); return v; }
	unsigned char u8 () { unsigned char v = 0; raw (&v, 1); return v; }
	double f64 () { double v = 0; raw (&v, 8); return v; }
	char *str () { int n = i32 (); if (n < 0 || !ok) return 0; if (e - p < n) { ok = false; return 0; } char *s = sdup (p, n); p += n; return s; }
};

static void put_formula (Wr &w, const Formula *f)
{
	w.i32 (f->nt); w.raw (f->tok, f->nt * (int) sizeof (Tok));
	w.i32 (f->nn); w.raw (f->nd, f->nn * (int) sizeof (Node));
	w.i32 (f->na); w.raw (f->args, f->na * (int) sizeof (int));
	w.i32 (f->npool); w.raw (f->pool, f->npool);
	w.i32 (f->root); w.u8 (f->vol);
}
static Formula *get_formula (Rd &r)
{
	Formula *f = (Formula *) calloc (1, sizeof (Formula));
	f->nt = r.i32 (); if (f->nt < 0 || f->nt > 1000000) { r.ok = false; free (f); return 0; }
	f->tok = (Tok *) malloc (imax (1, f->nt) * sizeof (Tok)); r.raw (f->tok, f->nt * (int) sizeof (Tok));
	f->nn = r.i32 (); if (f->nn < 0 || f->nn > 1000000) { r.ok = false; f->nn = 0; }
	f->nd = (Node *) malloc (imax (1, f->nn) * sizeof (Node)); r.raw (f->nd, f->nn * (int) sizeof (Node));
	f->na = r.i32 (); if (f->na < 0 || f->na > 1000000) { r.ok = false; f->na = 0; }
	f->args = (int *) malloc (imax (1, f->na) * sizeof (int)); r.raw (f->args, f->na * (int) sizeof (int));
	f->npool = r.i32 (); if (f->npool < 0 || f->npool > 10000000) { r.ok = false; f->npool = 0; }
	f->pool = (char *) malloc (imax (1, f->npool)); r.raw (f->pool, f->npool);
	f->root = r.i32 (); f->vol = r.u8 ();
	return f;
}
static void put_cell (Wr &w, const Cell *x)
{
	w.i32 (x->r); w.i32 (x->c); w.u16 (x->style); w.u8 (x->kind); w.u8 (x->vt); w.u8 (x->err); w.f64 (x->num);
	w.str (x->str);
	if (x->kind == K_FORM) put_formula (w, x->f);
}
static Cell *get_cell (Rd &r)
{
	int rr = r.i32 (), cc = r.i32 ();
	Cell *x = cell_new (rr, cc);
	x->style = r.u16 (); x->kind = r.u8 (); x->vt = r.u8 (); x->err = r.u8 (); x->num = r.f64 ();
	x->str = r.str ();
	if (x->kind == K_FORM) x->f = get_formula (r);
	if (!r.ok || (x->kind == K_FORM && !x->f)) { cell_free (x); return 0; }
	return x;
}
static void put_sheet (Wr &w, Sheet *s)
{
	w.i32 (s->id); w.str (s->name);
	w.i32 (s->defColW); w.i32 (s->defRowH); w.u8 (s->grid); w.i32 ((int) s->tab);
	w.i32 (s->freezeR); w.i32 (s->freezeC);
	w.i32 (s->curR); w.i32 (s->curC); w.i32 (s->ancR); w.i32 (s->ancC); w.i32 (s->topR); w.i32 (s->leftC);
	int nc = 0; for (int c = 0; c < MAXC; c++) if (s->colW[c] || s->colSt[c] || s->colFl[c]) nc++;
	w.i32 (nc);
	for (int c = 0; c < MAXC; c++) if (s->colW[c] || s->colSt[c] || s->colFl[c]) { w.i32 (c); w.u16 (s->colW[c]); w.u16 (s->colSt[c]); w.u8 (s->colFl[c]); }
	w.i32 (s->nrows); w.raw (s->rows, s->nrows * (int) sizeof (RowInfo));
	w.i32 (s->nmerge); w.raw (s->merges, s->nmerge * (int) sizeof (Rect));
	w.i32 (s->ncharts); for (int i = 0; i < s->ncharts; i++) w.raw (s->charts[i], sizeof (Chart));
	w.i32 (s->ncf); w.raw (s->cf, s->ncf * (int) sizeof (CondFmt));
	w.u8 (s->af.on); w.raw (&s->af.r, sizeof (Rect)); w.i32 (s->af.n);
	for (int i = 0; i < s->af.n; i++) { w.i32 (s->af.col[i]); w.str (s->af.shown[i]); }
	w.i32 (s->cells.n);
	for (int i = 0; i < s->cells.cap; i++) if (s->cells.t[i]) put_cell (w, s->cells.t[i]);
}
// Into an existing sheet (emptied first) -- or a new one.
static Sheet *get_sheet (Rd &r, Sheet *into)
{
	int id = r.i32 (); char *nm = r.str ();
	Sheet *s = into ? into : sheet_new (id, nm ? nm : "Sheet");
	if (into)
	{
		s->cells.clear ();
		memset (s->colW, 0, MAXC * sizeof (unsigned short)); memset (s->colSt, 0, MAXC * sizeof (unsigned short)); memset (s->colFl, 0, MAXC);
		s->nrows = 0; s->nmerge = 0;
		for (int i = 0; i < s->ncharts; i++) chart_free (s->charts[i]);
		s->ncharts = 0;
		free (s->cf); s->cf = 0; s->ncf = 0;
		for (int i = 0; i < s->af.n; i++) free (s->af.shown[i]);
		memset (&s->af, 0, sizeof s->af);
		s->id = id; if (nm) scpy (s->name, nm, sizeof s->name);
	}
	free (nm);
	s->defColW = r.i32 (); s->defRowH = r.i32 (); s->grid = r.u8 (); s->tab = (unsigned) r.i32 ();
	s->freezeR = r.i32 (); s->freezeC = r.i32 ();
	s->curR = r.i32 (); s->curC = r.i32 (); s->ancR = r.i32 (); s->ancC = r.i32 (); s->topR = r.i32 (); s->leftC = r.i32 ();
	int nc = r.i32 ();
	for (int i = 0; i < nc && r.ok; i++) { int c = r.i32 (); unsigned short wv = r.u16 (), st = r.u16 (); unsigned char fl = r.u8 (); if (c >= 0 && c < MAXC) { s->colW[c] = wv; s->colSt[c] = st; s->colFl[c] = fl; } }
	int nr = r.i32 ();
	if (nr < 0 || nr > MAXR) { r.ok = false; nr = 0; }
	if (nr > s->crows) { s->rows = (RowInfo *) realloc (s->rows, nr * sizeof (RowInfo)); s->crows = nr; }
	r.raw (s->rows, nr * (int) sizeof (RowInfo)); s->nrows = r.ok ? nr : 0;
	int nm2 = r.i32 ();
	for (int i = 0; i < nm2 && r.ok; i++) { Rect m; r.raw (&m, sizeof m); merge_add (s, m); }
	int nch = r.i32 ();
	for (int i = 0; i < nch && r.ok; i++)
	{
		Chart *c = (Chart *) malloc (sizeof (Chart)); r.raw (c, sizeof (Chart));
		s->charts = (Chart **) realloc (s->charts, (s->ncharts + 1) * sizeof (Chart *)); s->charts[s->ncharts++] = c;
	}
	int ncf = r.i32 ();
	if (ncf < 0 || ncf > 100000) { r.ok = false; ncf = 0; }
	if (ncf) { s->cf = (CondFmt *) malloc (ncf * sizeof (CondFmt)); if (r.raw (s->cf, ncf * (int) sizeof (CondFmt))) s->ncf = ncf; }
	s->af.on = r.u8 (); r.raw (&s->af.r, sizeof (Rect));
	int naf = r.i32 ();
	if (naf < 0 || naf > AF_MAXCOLS) { r.ok = false; naf = 0; }
	for (int i = 0; i < naf && r.ok; i++) { s->af.col[i] = r.i32 (); s->af.shown[i] = r.str (); if (!s->af.shown[i]) s->af.shown[i] = sdup (""); s->af.n = i + 1; }
	int n = r.i32 ();
	for (int i = 0; i < n && r.ok; i++) { Cell *x = get_cell (r); if (x) { Cell *old = s->cells.take (x->r, x->c); cell_free (old); s->cells.put (x); } }
	cols_changed (s); rows_changed (s); sheet_touched (s);
	return s;
}

// ---- the steps --------------------------------------------------------------------------------------------
enum { U_RECT, U_SHEET, U_BOOK };
struct UStep { int kind, sheet; Rect rect; char *data; int len; int curR, curC; };
struct UStack { UStep *s; int n, cap; long long bytes; };
static UStack g_undo, g_redo;

static void ustack_clear (UStack &u) { for (int i = 0; i < u.n; i++) free (u.s[i].data); u.n = 0; u.bytes = 0; }
static void ustack_push (UStack &u, UStep st)
{
	if (u.n == u.cap) { u.cap = u.cap ? u.cap * 2 : 32; u.s = (UStep *) realloc (u.s, u.cap * sizeof (UStep)); }
	u.s[u.n++] = st; u.bytes += st.len;
	while (u.n > 1 && (u.n > 100 || u.bytes > (64ll << 20)))		// (the oldest dropped)
	{
		u.bytes -= u.s[0].len; free (u.s[0].data);
		memmove (u.s, u.s + 1, (u.n - 1) * sizeof (UStep)); u.n--;
	}
}
static UStep snapshot (Book &b, int kind, Sheet *s, Rect r)
{
	UStep st; memset (&st, 0, sizeof st);
	st.kind = kind; st.sheet = s ? s->id : 0; st.rect = r;
	if (s) { st.curR = s->curR; st.curC = s->curC; }
	Wr w;
	if (kind == U_RECT)
	{
		int cnt = 0;
		for (int i = 0; i < s->cells.cap; i++) if (s->cells.t[i] && rect_has (r, s->cells.t[i]->r, s->cells.t[i]->c)) cnt++;
		w.i32 (cnt);
		for (int i = 0; i < s->cells.cap; i++) if (s->cells.t[i] && rect_has (r, s->cells.t[i]->r, s->cells.t[i]->c)) put_cell (w, s->cells.t[i]);
	}
	else if (kind == U_SHEET) put_sheet (w, s);
	else
	{
		w.i32 (b.ns); w.i32 (b.nextId); w.i32 (b.active);
		for (int i = 0; i < b.ns; i++) put_sheet (w, b.sh[i]);
		w.i32 (b.nnames);
		for (int i = 0; i < b.nnames; i++) { w.str (b.names[i].name); w.i32 (b.names[i].scope); w.u8 (b.names[i].f != 0); if (b.names[i].f) put_formula (w, b.names[i].f); }
	}
	st.len = w.b.n; st.data = w.b.take ();
	return st;
}
// Before a change: what it will touch (a range; its sheet; the book).
static void undo_push (Book &b, int kind, Sheet *s, Rect r)
{
	if (kind == U_RECT && (rect_whole_cols (r) || rect_whole_rows (r) || (long long) (r.r1 - r.r0 + 1) * (r.c1 - r.c0 + 1) > 2000000)) kind = U_SHEET;
	ustack_push (g_undo, snapshot (b, kind, s, r));
	ustack_clear (g_redo);
}
static void undo_rect (Book &b, Sheet *s, Rect r) { undo_push (b, U_RECT, s, r); }
static void undo_sheet (Book &b, Sheet *s) { Rect r = { 0, 0, 0, 0 }; undo_push (b, U_SHEET, s, r); }
static void undo_book (Book &b) { Rect r = { 0, 0, 0, 0 }; undo_push (b, U_BOOK, b.sh[b.active], r); }

static bool restore (Book &b, UStep &st)
{
	Rd r (st.data, st.len);
	if (st.kind == U_BOOK)
	{
		int ns = r.i32 (), next = r.i32 (), act = r.i32 ();
		if (!r.ok || ns < 1 || ns > MAXSHEETS) return false;
		for (int i = 0; i < b.ns; i++) sheet_free (b.sh[i]);
		b.ns = 0;
		for (int i = 0; i < ns && r.ok; i++) { Sheet *s = get_sheet (r, 0); b.sh[b.ns++] = s; }
		b.nextId = next; b.active = iclamp (act, 0, b.ns - 1);
		names_clear (b);
		int nn = r.i32 ();
		if (nn < 0 || nn > 100000) { r.ok = false; nn = 0; }
		for (int i = 0; i < nn && r.ok; i++)
		{
			char *nm = r.str (); int scope = r.i32 (); bool hasF = r.u8 ();
			Formula *f = hasF ? get_formula (r) : 0;
			if (!r.ok || !nm) { free (nm); formula_free (f); break; }
			b.names = (DefName *) realloc (b.names, (b.nnames + 1) * sizeof (DefName));
			DefName &d = b.names[b.nnames++]; memset (&d, 0, sizeof d);
			scpy (d.name, nm, sizeof d.name); d.scope = scope; d.f = f;
			free (nm);
		}
		return r.ok;
	}
	Sheet *s = book_sheet_by_id (b, st.sheet);
	if (!s) return false;
	if (st.kind == U_SHEET) { get_sheet (r, s); return r.ok; }
	// a range: its cells now taken out, the old ones put back
	Cell **list = 0; int n = 0, cap = 0;
	for (int i = 0; i < s->cells.cap; i++)
		if (s->cells.t[i] && rect_has (st.rect, s->cells.t[i]->r, s->cells.t[i]->c))
		{ if (n == cap) { cap = cap ? cap * 2 : 64; list = (Cell **) realloc (list, cap * sizeof (Cell *)); } list[n++] = s->cells.t[i]; }
	for (int i = 0; i < n; i++) s->cells.del (list[i]->r, list[i]->c);
	free (list);
	int cnt = r.i32 ();
	for (int i = 0; i < cnt && r.ok; i++) { Cell *x = get_cell (r); if (x) s->cells.put (x); }
	sheet_touched (s);
	return r.ok;
}
// Undo (redo = false) or Redo: the step's scope written as it is now, the old state put back.
// Returns the sheet the step was on (its cursor and range kept for the view), 0 when nothing to do.
static Sheet *undo_step (Book &b, bool redo, Rect *rect = 0)
{
	UStack &from = redo ? g_redo : g_undo, &to = redo ? g_undo : g_redo;
	if (from.n == 0) return 0;
	UStep st = from.s[--from.n];
	from.bytes -= st.len;
	Sheet *s = book_sheet_by_id (b, st.sheet);
	UStep now = snapshot (b, st.kind, st.kind == U_BOOK ? b.sh[b.active] : s, st.rect);
	if (s) { now.curR = s->curR; now.curC = s->curC; }
	ustack_push (to, now);
	restore (b, st);
	free (st.data);
	s = book_sheet_by_id (b, st.sheet);
	if (rect) *rect = st.rect;
	return s ? s : b.sh[b.active];
}
static void undo_clear () { ustack_clear (g_undo); ustack_clear (g_redo); }
static void undo_drop_last ()				// (a step taken for a change that did not come)
{
	if (!g_undo.n) return;
	g_undo.n--; g_undo.bytes -= g_undo.s[g_undo.n].len; free (g_undo.s[g_undo.n].data);
}

} // namespace ss

#endif
