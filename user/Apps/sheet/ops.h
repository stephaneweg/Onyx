//
// ops.h -- what the window does to the workbook: a cell typed into (a formula read, a number with the
// format it asks for), its value shown (through its number format, a colour from the format), the text
// the input line shows to edit it; styles changed over a range; rows and columns inserted, deleted,
// sized, hidden; cells moved, copied (the relative references following), filled (series: 1, 2, 3...;
// Monday, Tuesday...; dates; "Item 1", "Item 2"...), cleared, sorted, merged; sheets added, deleted,
// renamed, moved. The undo keeps the cells a change touches as they were (the whole sheet, the whole
// book for the structural ones), written in a small binary form.
//
#ifndef _sheet_ops_h
#define _sheet_ops_h

#include "funcs.h"

namespace ss {

// ---- a cell's content -----------------------------------------------------------------------------------
static const Style &cell_style (Book &b, Sheet *s, Cell *x, int r, int c)
{
	if (x && x->style) return b.styles.s[x->style];
	RowInfo *ri = row_info (s, r);
	if (ri && ri->style) return b.styles.s[ri->style];
	if (s->colSt[c]) return b.styles.s[s->colSt[c]];
	return b.styles.s[0];
}
static int cell_style_index (Sheet *s, Cell *x, int r, int c)
{
	if (x && x->style) return x->style;
	RowInfo *ri = row_info (s, r);
	if (ri && ri->style) return ri->style;
	return s->colSt[c];
}

// The cell made (keeping the row's or column's style it had through them).
static Cell *cell_make (Sheet *s, int r, int c)
{
	Cell *x = s->cells.get (r, c);
	if (x) return x;
	int st = cell_style_index (s, 0, r, c);
	x = s->cells.add (r, c);
	x->style = (unsigned short) st;
	return x;
}
static void cell_drop_if_plain (Sheet *s, Cell *x)
{
	if (x && x->kind == K_NONE && x->style == cell_style_index (s, 0, x->r, x->c)) s->cells.del (x->r, x->c);
}

// A typed entry into (r, c): a formula ("=" first; "+" / "-" first when it reads as one), else what
// input_parse makes of it. false (and why) when a formula cannot be read.
static bool cell_input (Book &b, Sheet *s, int r, int c, const char *text, const char **why = 0, int *where = 0)
{
	Cell *x = s->cells.get (r, c);
	const char *t = text;
	bool form = t[0] == '=' && t[1];
	Formula *f = 0;
	if (form)
	{
		f = formula_parse (b, t + 1, -1, why, where);
		if (!f) { if (where) (*where)++; return false; }
	}
	else if ((t[0] == '+' || t[0] == '-') && t[1])
	{
		double v;
		if (!parse_typed_number (t, (int) strlen (t), &v, 0, 0) && !parse_datetime (t, (int) strlen (t), &v, 0, 0))
		{
			f = formula_parse (b, t, -1);				// ("+A1": a formula; "-abc": a text)
			if (f)
			{
				bool anyRef = false;
				for (int i = 0; i < f->nt; i++) if (f->tok[i].t == TK_REF || f->tok[i].t == TK_AREA || f->tok[i].t == TK_FUNC) anyRef = true;
				if (!anyRef) { formula_free (f); f = 0; }
			}
		}
	}
	if (f)
	{
		x = cell_make (s, r, c);
		cell_clear (x);
		x->kind = K_FORM; x->f = f;
		x->epoch = 0;
		sheet_touched (s);
		return true;
	}
	Entry e; input_parse (t, e);
	if (e.kind == K_NONE)
	{
		if (x) { cell_clear (x); cell_drop_if_plain (s, x); }
		sheet_touched (s);
		return true;
	}
	x = cell_make (s, r, c);
	cell_clear (x);
	x->kind = (unsigned char) e.kind;
	switch (e.kind)
	{
	case K_NUM: x->vt = V_NUM; x->num = e.num; break;
	case K_BOOL: x->vt = V_BOOL; x->num = e.num; break;
	case K_ERR: x->vt = V_ERR; x->err = (unsigned char) e.err; break;
	case K_STR: x->vt = V_STR; x->str = sdup (e.text, e.textLen); break;
	}
	x->epoch = b.epoch;
	// the format the entry asks for, if the cell has none of its own
	if (e.fmt[0])
	{
		const Style &st = b.styles.s[x->style];
		int k = fmt_kind (book_fmt_code (b, st.fmt));
		bool date = strchr (e.fmt, 'y') || strchr (e.fmt, 'h') || strchr (e.fmt, 'd');
		bool keep = k != FK_GENERAL && !(date && k != FK_DATE && k != FK_TIME && k != FK_DATETIME && k != FK_NUMBER);
		if (st.fmt == 0 || !keep)
		{
			Style ns = st; ns.fmt = (unsigned short) book_fmt (b, e.fmt);
			x->style = (unsigned short) b.styles.intern (ns);
		}
	}
	sheet_touched (s);
	return true;
}

// A value set directly (from a file, a fill).
static void cell_set_num (Sheet *s, int r, int c, double v) { Cell *x = cell_make (s, r, c); cell_clear (x); x->kind = K_NUM; x->vt = V_NUM; x->num = v; sheet_touched (s); }
static void cell_set_str (Sheet *s, int r, int c, const char *t, int n = -1) { Cell *x = cell_make (s, r, c); cell_clear (x); x->kind = K_STR; x->vt = V_STR; x->str = sdup (t, n); sheet_touched (s); }

// ---- a cell's text: shown, and to edit --------------------------------------------------------------------
struct Shown
{
	char text[256];
	unsigned color;					// the format's colour (AUTO: none)
	bool number;					// a number (aligned right by default)
	bool err, boolean;
};
static void cell_shown (Book &b, Sheet *s, Cell *x, Shown &o, int maxc = 11)
{
	o.text[0] = 0; o.color = AUTO; o.number = o.err = o.boolean = false;
	if (!x || x->kind == K_NONE) return;
	const Style &st = b.styles.s[x->style];
	const char *code = book_fmt_code (b, st.fmt);
	Buf out;
	switch (x->vt)
	{
	case V_NUM: fmt_number (code, x->num, out, &o.color, maxc); o.number = true; break;
	case V_STR: fmt_text (code, x->str, out, &o.color); break;
	case V_BOOL: out.puts (x->num ? "TRUE" : "FALSE"); o.boolean = true; break;
	case V_ERR: out.puts (ERR_NAMES[x->err]); o.err = true; break;
	default: if (x->kind == K_FORM) { out.puts ("0"); o.number = true; } break;
	}
	scpy (o.text, out.str (), sizeof o.text);
	(void) s;
}

// The text the input line shows: the formula, the number in full (a date as a date, a percentage as one).
static void cell_edit_text (Book &b, Sheet *s, Cell *x, Buf &o)
{
	(void) s;
	o.clear ();
	if (!x) return;
	switch (x->kind)
	{
	case K_FORM: formula_print (b, x->f, o); return;
	case K_STR:
	{
		// a text that would read as something else keeps its "'"
		Entry e; input_parse (x->str, e);
		if (e.kind != K_STR || x->str[0] == '=' || x->str[0] == '\'') o.put ('\'');
		o.puts (x->str);
		return;
	}
	case K_BOOL: o.puts (x->num ? "TRUE" : "FALSE"); return;
	case K_ERR: o.puts (ERR_NAMES[x->err]); return;
	case K_NUM:
	{
		const Style &st = b.styles.s[x->style];
		const char *code = book_fmt_code (b, st.fmt);
		int k = fmt_kind (code);
		char t[64];
		if (k == FK_DATE || k == FK_DATETIME || k == FK_TIME)
		{
			bool hasTime = x->num != floor (x->num) || k != FK_DATE;
			if (k == FK_TIME && x->num < 1) fmt_number ("hh:mm:ss", x->num, o);
			else fmt_number (hasTime ? "dd/mm/yyyy hh:mm:ss" : "dd/mm/yyyy", x->num, o);
			return;
		}
		if (k == FK_PERCENT) { num_full (x->num * 100, t, sizeof t); o.puts (t); o.put ('%'); return; }
		num_full (x->num, t, sizeof t);
		o.puts (t);
		return;
	}
	}
}


// ---- styles over a range --------------------------------------------------------------------------------
typedef void (*StyleFn) (Style &st, const void *arg);
static unsigned short restyle (Book &b, int idx, StyleFn fn, const void *arg)
{
	Style st = b.styles.s[idx];
	fn (st, arg);
	return (unsigned short) b.styles.intern (st);
}
static bool rect_whole_cols (const Rect &r) { return r.r0 == 0 && r.r1 >= MAXR - 1; }
static bool rect_whole_rows (const Rect &r) { return r.c0 == 0 && r.c1 >= MAXC - 1; }
// Every cell of the range restyled; whole columns / rows: their own style too, and the cells they hold.
static void style_range (Book &b, Sheet *s, Rect r, StyleFn fn, const void *arg)
{
	if (rect_whole_cols (r) || rect_whole_rows (r))
	{
		bool cols = rect_whole_cols (r), rows = rect_whole_rows (r);
		if (cols) for (int c = r.c0; c <= r.c1; c++) s->colSt[c] = restyle (b, s->colSt[c], fn, arg);
		if (rows && !cols) for (int rr = r.r0; rr <= r.r1; rr++) { RowInfo *ri = row_add (s, rr); ri->style = restyle (b, ri->style, fn, arg); }
		if (cols) for (int i = 0; i < s->nrows; i++) if (s->rows[i].style) s->rows[i].style = restyle (b, s->rows[i].style, fn, arg);
		for (int i = 0; i < s->cells.cap; i++)
		{
			Cell *x = s->cells.t[i];
			if (x && rect_has (r, x->r, x->c)) x->style = restyle (b, x->style, fn, arg);
		}
		return;
	}
	// (cells made where there were none: bounded to what can be shown sensibly)
	long long area = (long long) (r.r1 - r.r0 + 1) * (r.c1 - r.c0 + 1);
	if (area > 4000000) return;
	for (int rr = r.r0; rr <= r.r1; rr++)
		for (int c = r.c0; c <= r.c1; c++)
		{
			Cell *x = cell_make (s, rr, c);
			x->style = restyle (b, x->style, fn, arg);
			cell_drop_if_plain (s, x);
		}
}

// ---- clearing -------------------------------------------------------------------------------------------
enum { CLR_CONTENT = 1, CLR_FORMAT = 2, CLR_ALL = 3 };
static void clear_range (Book &b, Sheet *s, Rect r, int what)
{
	(void) b;
	Cell **list = 0; int n = 0, cap = 0;
	for (int i = 0; i < s->cells.cap; i++)
	{
		Cell *x = s->cells.t[i];
		if (!x || !rect_has (r, x->r, x->c)) continue;
		if (n == cap) { cap = cap ? cap * 2 : 64; list = (Cell **) realloc (list, cap * sizeof (Cell *)); }
		list[n++] = x;
	}
	for (int i = 0; i < n; i++)
	{
		Cell *x = list[i];
		if (what & CLR_CONTENT) cell_clear (x);
		if (what & CLR_FORMAT) x->style = 0;
		if (x->kind == K_NONE && (x->style == 0 || x->style == cell_style_index (s, 0, x->r, x->c))) s->cells.del (x->r, x->c);
	}
	free (list);
	if ((what & CLR_FORMAT) && rect_whole_cols (r)) for (int c = r.c0; c <= r.c1; c++) s->colSt[c] = 0;
	if (what & CLR_FORMAT) for (int i = 0; i < s->nmerge; ) { if (rect_meets (s->merges[i], r)) merge_del (s, i); else i++; }
	sheet_touched (s);
}

// ---- rows and columns inserted / deleted ----------------------------------------------------------------
// Every formula of the book told (their references to the sheet moved), the sheet's cells moved.
static void each_formula (Book &b, void (*f) (Sheet *, Cell *, void *), void *arg)
{
	for (int i = 0; i < b.ns; i++)
	{
		Sheet *s = b.sh[i];
		for (int k = 0; k < s->cells.cap; k++) { Cell *x = s->cells.t[k]; if (x && x->kind == K_FORM) f (s, x, arg); }
	}
}
struct ShiftArg { int target; bool rows; int at, n; };
static void shift_one (Sheet *s, Cell *x, void *a)
{
	ShiftArg *g = (ShiftArg *) a;
	formula_shift (x->f, s->id, g->target, g->rows, g->at, g->n);
}
// A range kept by a chart or a merge: moved as a reference is (false: it was deleted).
static bool shift_rect (Rect &r, bool rows, int at, int n)
{
	int &a = rows ? r.r0 : r.c0, &z = rows ? r.r1 : r.c1;
	int lim = rows ? MAXR : MAXC;
	if (n > 0) { if (a >= at) a += n; if (z >= at) z += n; if (a >= lim) return false; if (z >= lim) z = lim - 1; return true; }
	int d0 = at, d1 = at - n - 1;
	if (a >= d0 && z <= d1) return false;
	if (a > d1) a += n; else if (a >= d0) a = d0;
	if (z > d1) z += n; else if (z >= d0) z = d0 - 1;
	return z >= a;
}
static void shift_cells (Book &b, Sheet *s, bool rows, int at, int n)
{
	// the cells: out of the map, moved (or freed), back in
	int cnt = s->cells.n;
	Cell **all = (Cell **) malloc (imax (1, cnt) * sizeof (Cell *));
	int k = 0;
	for (int i = 0; i < s->cells.cap; i++) if (s->cells.t[i]) { all[k++] = s->cells.t[i]; s->cells.t[i] = 0; }
	s->cells.n = 0;
	for (int i = 0; i < k; i++)
	{
		Cell *x = all[i];
		int &v = rows ? x->r : x->c;
		int lim = rows ? MAXR : MAXC;
		if (n < 0 && v >= at && v < at - n) { cell_free (x); continue; }
		if (v >= at) v += n;
		if (v >= lim || v < 0) { cell_free (x); continue; }
		s->cells.put (x);
	}
	free (all);
	// the formulas that point at the sheet
	ShiftArg g; g.target = s->id; g.rows = rows; g.at = at; g.n = n;
	each_formula (b, shift_one, &g);
	// the rows' or columns' own sizes and styles
	if (rows)
	{
		int w = 0;
		for (int i = 0; i < s->nrows; i++)
		{
			RowInfo ri = s->rows[i];
			if (n < 0 && ri.r >= at && ri.r < at - n) continue;
			if (ri.r >= at) ri.r += n;
			if (ri.r >= MAXR) continue;
			s->rows[w++] = ri;
		}
		s->nrows = w;
		rows_changed (s);
	}
	else
	{
		if (n > 0)
		{
			memmove (s->colW + at + n, s->colW + at, (MAXC - at - n) * sizeof (unsigned short));
			memmove (s->colSt + at + n, s->colSt + at, (MAXC - at - n) * sizeof (unsigned short));
			memmove (s->colFl + at + n, s->colFl + at, (MAXC - at - n));
			for (int c = at; c < at + n; c++) { s->colW[c] = s->colW[at > 0 ? at - 1 : at + n]; s->colSt[c] = at > 0 ? s->colSt[at - 1] : 0; s->colFl[c] = 0; }
		}
		else
		{
			int d = -n;
			memmove (s->colW + at, s->colW + at + d, (MAXC - at - d) * sizeof (unsigned short));
			memmove (s->colSt + at, s->colSt + at + d, (MAXC - at - d) * sizeof (unsigned short));
			memmove (s->colFl + at, s->colFl + at + d, (MAXC - at - d));
			for (int c = MAXC - d; c < MAXC; c++) { s->colW[c] = 0; s->colSt[c] = 0; s->colFl[c] = 0; }
		}
		cols_changed (s);
	}
	// merges, charts' data
	for (int i = 0; i < s->nmerge; ) { if (!shift_rect (s->merges[i], rows, at, n) || (s->merges[i].r0 == s->merges[i].r1 && s->merges[i].c0 == s->merges[i].c1)) merge_del (s, i); else i++; }
	for (int j = 0; j < b.ns; j++)
		for (int i = 0; i < b.sh[j]->ncharts; i++)
		{
			Chart *ch = b.sh[j]->charts[i];
			if (ch->srcSheet == s->id && !shift_rect (ch->src, rows, at, n)) ch->src.r1 = ch->src.r0 - 1;	// (no data left)
		}
	if (rows && n > 0 && s->freezeR > at) s->freezeR += n;
	if (!rows && n > 0 && s->freezeC > at) s->freezeC += n;
	sheet_touched (s);
}
static void insert_rows (Book &b, Sheet *s, int at, int n) { shift_cells (b, s, true, at, n); }
static void delete_rows (Book &b, Sheet *s, int at, int n) { shift_cells (b, s, true, at, -n); }
static void insert_cols (Book &b, Sheet *s, int at, int n) { shift_cells (b, s, false, at, n); }
static void delete_cols (Book &b, Sheet *s, int at, int n) { shift_cells (b, s, false, at, -n); }

// ---- the clipboard ------------------------------------------------------------------------------------------
struct ClipCell
{
	int dr, dc;						// from the block's top-left
	unsigned char kind, vt, err;
	double num; char *str; Formula *f;
	Style st; char font[48]; char fmt[64];			// (the style by value: another book's tables differ)
};
struct Clip
{
	bool full, cut;
	int srcSheet; Rect src;
	int rows, cols;
	ClipCell *c; int n, cap;
	Rect *merges; int nmerge;				// (relative to the block)
	unsigned short *colW;					// the columns' widths (whole columns copied)
	char *text;						// what was put on the system clipboard
};
static Clip g_clip;
static void clip_free ()
{
	for (int i = 0; i < g_clip.n; i++) { free (g_clip.c[i].str); formula_free (g_clip.c[i].f); }
	free (g_clip.c); free (g_clip.merges); free (g_clip.colW); free (g_clip.text);
	memset (&g_clip, 0, sizeof g_clip);
}
// The block copied (the cells with content or a style of their own).
static void copy_range (Book &b, Sheet *s, Rect r, bool cut)
{
	clip_free ();
	g_clip.full = true; g_clip.cut = cut; g_clip.srcSheet = s->id; g_clip.src = r;
	sheet_bounds (s);
	Rect u = r;
	if (u.r1 > s->maxR && rect_whole_cols (r)) u.r1 = imax (s->maxR, r.r0);
	if (u.c1 > s->maxC && rect_whole_rows (r)) u.c1 = imax (s->maxC, r.c0);
	g_clip.rows = u.r1 - u.r0 + 1; g_clip.cols = u.c1 - u.c0 + 1;
	for (int i = 0; i < s->cells.cap; i++)
	{
		Cell *x = s->cells.t[i];
		if (!x || !rect_has (u, x->r, x->c)) continue;
		if (g_clip.n == g_clip.cap) { g_clip.cap = g_clip.cap ? g_clip.cap * 2 : 64; g_clip.c = (ClipCell *) realloc (g_clip.c, g_clip.cap * sizeof (ClipCell)); }
		ClipCell &k = g_clip.c[g_clip.n++];
		memset (&k, 0, sizeof k);
		k.dr = x->r - u.r0; k.dc = x->c - u.c0;
		k.kind = x->kind; k.vt = x->vt; k.err = x->err; k.num = x->num;
		k.str = x->str ? sdup (x->str) : 0;
		k.f = x->f ? formula_dup (x->f) : 0;
		k.st = b.styles.s[x->style];
		scpy (k.font, b.fonts[k.st.font < b.nfonts ? k.st.font : 0], sizeof k.font);
		scpy (k.fmt, book_fmt_code (b, k.st.fmt), sizeof k.fmt);
	}
	for (int i = 0; i < s->nmerge; i++)
		if (s->merges[i].r0 >= u.r0 && s->merges[i].r1 <= u.r1 && s->merges[i].c0 >= u.c0 && s->merges[i].c1 <= u.c1)
		{
			g_clip.merges = (Rect *) realloc (g_clip.merges, (g_clip.nmerge + 1) * sizeof (Rect));
			Rect m = s->merges[i]; m.r0 -= u.r0; m.r1 -= u.r0; m.c0 -= u.c0; m.c1 -= u.c0;
			g_clip.merges[g_clip.nmerge++] = m;
		}
	if (rect_whole_cols (r))
	{
		g_clip.colW = (unsigned short *) malloc (g_clip.cols * sizeof (unsigned short));
		for (int c = 0; c < g_clip.cols; c++) g_clip.colW[c] = s->colW[u.c0 + c];
	}
	g_clip.src = u;
}
// The block as text (tab-separated, as shown): for the other apps.
static char *clip_text (Book &b, Sheet *s, Rect r)
{
	sheet_bounds (s);
	r.r1 = imin (r.r1, imax (r.r0, s->maxR)); r.c1 = imin (r.c1, imax (r.c0, s->maxC));
	Buf o;
	for (int rr = r.r0; rr <= r.r1; rr++)
	{
		for (int c = r.c0; c <= r.c1; c++)
		{
			if (c > r.c0) o.put ('\t');
			Shown sh; cell_shown (b, s, s->cells.get (rr, c), sh, 24);
			o.puts (sh.text);
		}
		o.put ('\n');
	}
	return o.take ();
}
enum { PASTE_ALL, PASTE_VALUES, PASTE_FORMATS, PASTE_FORMULAS };
// The clipboard's block at (r, c) -- repeated over the selection `sel` when it is a multiple of it.
static void paste_range (Book &b, Sheet *s, int r, int c, Rect sel, int what = PASTE_ALL, bool transpose = false)
{
	if (!g_clip.full) return;
	int R = transpose ? g_clip.cols : g_clip.rows, C = transpose ? g_clip.rows : g_clip.cols;
	int tr = 1, tc = 1;
	int sr = sel.r1 - sel.r0 + 1, sc = sel.c1 - sel.c0 + 1;
	if (!g_clip.cut && sr % R == 0 && sc % C == 0 && (sr > R || sc > C) && (long long) sr * sc <= 1000000) { tr = sr / R; tc = sc / C; r = sel.r0; c = sel.c0; }
	Sheet *src = book_sheet_by_id (b, g_clip.srcSheet);
	for (int ti = 0; ti < tr; ti++)
		for (int tj = 0; tj < tc; tj++)
		{
			int r0 = r + ti * R, c0 = c + tj * C;
			if (r0 + R > MAXR || c0 + C > MAXC) continue;
			Rect dst = { r0, c0, r0 + R - 1, c0 + C - 1 };
			if (what == PASTE_ALL || what == PASTE_FORMULAS || what == PASTE_VALUES) clear_range (b, s, dst, what == PASTE_ALL ? CLR_ALL : CLR_CONTENT);
			for (int i = 0; i < g_clip.n; i++)
			{
				ClipCell &k = g_clip.c[i];
				int dr = transpose ? k.dc : k.dr, dc = transpose ? k.dr : k.dc;
				int rr = r0 + dr, cc = c0 + dc;
				Cell *x = cell_make (s, rr, cc);
				if (what != PASTE_FORMATS)
				{
					cell_clear (x);
					if (k.kind == K_FORM && what != PASTE_VALUES)
					{
						x->kind = K_FORM;
						if (g_clip.cut) x->f = formula_dup (k.f);
						else x->f = formula_copy (k.f, rr - (g_clip.src.r0 + k.dr), cc - (g_clip.src.c0 + k.dc));
						// (a formula moved to another sheet keeps pointing at its first sheet)
						if (src && src != s) for (int t = 0; t < x->f->nt; t++) { Tok &tk = x->f->tok[t]; if ((tk.t == TK_REF || tk.t == TK_AREA) && !tk.sheet) { tk.sheet = src->id; tk.fl |= TF_SHEET; } }
						x->epoch = 0;
					}
					else if (k.kind != K_NONE)
					{
						x->kind = k.kind == K_FORM ? (unsigned char) (k.vt == V_NUM ? K_NUM : k.vt == V_STR ? K_STR : k.vt == V_BOOL ? K_BOOL : k.vt == V_ERR ? K_ERR : K_NONE) : k.kind;
						x->vt = k.vt; x->err = k.err; x->num = k.num;
						x->str = k.str ? sdup (k.str) : 0;
						x->epoch = b.epoch;
					}
				}
				if (what == PASTE_ALL || what == PASTE_FORMATS)
				{
					Style st = k.st;
					st.font = (unsigned short) book_font (b, k.font);
					st.fmt = (unsigned short) book_fmt (b, k.fmt);
					x->style = (unsigned short) b.styles.intern (st);
				}
				cell_drop_if_plain (s, x);
			}
			if (what == PASTE_ALL)
			{
				for (int i = 0; i < s->nmerge; ) { if (rect_meets (s->merges[i], dst)) merge_del (s, i); else i++; }
				for (int i = 0; i < g_clip.nmerge; i++)
				{
					Rect m = g_clip.merges[i];
					if (transpose) { int t = m.r0; m.r0 = m.c0; m.c0 = t; t = m.r1; m.r1 = m.c1; m.c1 = t; }
					m.r0 += r0; m.r1 += r0; m.c0 += c0; m.c1 += c0;
					merge_add (s, m);
				}
				if (g_clip.colW && !transpose) { for (int j = 0; j < C && c0 + j < MAXC; j++) s->colW[c0 + j] = g_clip.colW[j]; cols_changed (s); }
			}
		}
	sheet_touched (s);
}
struct MoveArg { int srcId; Rect src; int dr, dc; int dstId; };
static void move_one (Sheet *s, Cell *x, void *a) { MoveArg *g = (MoveArg *) a; formula_move (x->f, s->id, g->srcId, g->src, g->dr, g->dc, g->dstId); }
// A cut pasted: the source emptied, the cells moved, the references to them following.
static void paste_cut (Book &b, Sheet *s, int r, int c)
{
	Sheet *src = book_sheet_by_id (b, g_clip.srcSheet);
	if (!src || !g_clip.cut) return;
	Rect from = g_clip.src;
	clear_range (b, src, from, CLR_ALL);
	for (int i = 0; i < src->nmerge; ) { if (rect_meets (src->merges[i], from)) merge_del (src, i); else i++; }
	Rect sel = { r, c, r + g_clip.rows - 1, c + g_clip.cols - 1 };
	paste_range (b, s, r, c, sel, PASTE_ALL, false);
	MoveArg g; g.srcId = src->id; g.src = from; g.dr = r - from.r0; g.dc = c - from.c0; g.dstId = s->id;
	each_formula (b, move_one, &g);
	for (int j = 0; j < b.ns; j++)
		for (int i = 0; i < b.sh[j]->ncharts; i++)
		{
			Chart *ch = b.sh[j]->charts[i];
			if (ch->srcSheet == src->id && ch->src.r0 >= from.r0 && ch->src.r1 <= from.r1 && ch->src.c0 >= from.c0 && ch->src.c1 <= from.c1)
			{ ch->src.r0 += g.dr; ch->src.r1 += g.dr; ch->src.c0 += g.dc; ch->src.c1 += g.dc; ch->srcSheet = s->id; }
		}
	g_clip.cut = false;
	g_clip.srcSheet = s->id; g_clip.src = sel;			// (a second paste copies)
}
// Text from another app pasted at (r, c): lines of tab-separated values, each read as typed.
static void paste_text (Book &b, Sheet *s, int r, int c, const char *t, int *rows = 0, int *cols = 0)
{
	int rr = r, cc = c, maxc = 0;
	Buf cell;
	const char *p = t;
	int n = (int) strlen (t);
	if (n > 0 && t[n - 1] == '\n') n--;
	if (n > 0 && t[n - 1] == '\r') n--;
	for (int i = 0; i <= n; i++)
	{
		char ch = i < n ? p[i] : '\n';
		if (ch == '\t' || ch == '\n')
		{
			if (cell.n > 0 && cell.b[cell.n - 1] == '\r') cell.n--;
			if (rr < MAXR && cc < MAXC) cell_input (b, s, rr, cc, cell.str ());
			cell.clear ();
			if (ch == '\t') cc++;
			else { maxc = imax (maxc, cc - c + 1); rr++; cc = c; }
			continue;
		}
		cell.put (ch);
	}
	if (rows) *rows = rr - r;
	if (cols) *cols = maxc;
	sheet_touched (s);
}

// ---- filling -------------------------------------------------------------------------------------------
static const char *const DAY3[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
static const char *const MONTH3[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
// A word of a list (Monday, Sep...): the list (0 months, 1 month3, 2 days, 3 day3), its index; the case kept.
static bool list_word (const char *s, int *list, int *idx)
{
	for (int i = 0; i < 12; i++) { if (ci_eq (s, MONTHS[i])) { *list = 0; *idx = i; return true; } if (ci_eq (s, MONTH3[i])) { *list = 1; *idx = i; return true; } }
	for (int i = 0; i < 7; i++) { if (ci_eq (s, DAYS[i])) { *list = 2; *idx = i; return true; } if (ci_eq (s, DAY3[i])) { *list = 3; *idx = i; return true; } }
	return false;
}
static const char *list_item (int list, int i)
{
	switch (list) { case 0: return MONTHS[(i % 12 + 12) % 12]; case 1: return MONTH3[(i % 12 + 12) % 12]; case 2: return DAYS[(i % 7 + 7) % 7]; }
	return DAY3[(i % 7 + 7) % 7];
}
// "Item 12" -> "Item ", 12 (the number at the end).
static bool text_num (const char *s, int *prefix, long long *v)
{
	int n = (int) strlen (s), k = n;
	while (k > 0 && s[k - 1] >= '0' && s[k - 1] <= '9') k--;
	if (k == n || n - k > 15) return false;
	*prefix = k; *v = atoll (s + k);
	return true;
}
// One line of a fill (the source cells src[0..ns), the targets dst[0..nd) in the fill's direction).
struct FillCell { int r, c; };
static void fill_line (Book &b, Sheet *s, const FillCell *src, int ns, const FillCell *dst, int nd)
{
	Cell *x0 = s->cells.get (src[0].r, src[0].c);
	// what kind of series: numbers, dates by months, list words, texts ending in a number
	bool allNum = true, allList = true, allTextNum = true;
	int list = -1; int li0 = 0;
	for (int i = 0; i < ns; i++)
	{
		Cell *x = s->cells.get (src[i].r, src[i].c);
		if (!x || x->kind != K_NUM) allNum = false;
		int l, idx;
		if (!x || x->kind != K_STR || !list_word (x->str, &l, &idx) || (list >= 0 && l != list)) allList = false;
		else { if (list < 0) { list = l; li0 = idx; } }
		int pf; long long v;
		if (!x || x->kind != K_STR || !text_num (x->str, &pf, &v)) allTextNum = false;
	}
	bool single = ns == 1;
	bool isDate = false;
	if (x0 && x0->kind == K_NUM) { int k = fmt_kind (book_fmt_code (b, b.styles.s[x0->style].fmt)); isDate = k == FK_DATE || k == FK_DATETIME; }
	for (int j = 0; j < nd; j++)
	{
		const FillCell &from = src[j % ns];
		Cell *sx = s->cells.get (from.r, from.c);
		Cell *x = cell_make (s, dst[j].r, dst[j].c);
		cell_clear (x);
		x->style = sx ? sx->style : (unsigned short) cell_style_index (s, 0, from.r, from.c);
		int step = j + ns;					// (the position in the series: the source is 0..ns-1)
		if (allNum && !(single && !isDate))
		{
			double v;
			if (single) v = x0->num + (j + 1);			// (a date: day by day)
			else
			{
				// a date series by months (same day, months apart) or a straight line (least squares)
				Cell *x1 = s->cells.get (src[1].r, src[1].c);
				int y0, m0, d0, y1, m1, d1;
				serial_date (x0->num, &y0, &m0, &d0); serial_date (x1->num, &y1, &m1, &d1);
				if (isDate && d0 == d1 && (y1 * 12 + m1) != (y0 * 12 + m0))
				{
					int dm = (y1 * 12 + m1) - (y0 * 12 + m0);
					v = add_months (x0->num, dm * step, false);
				}
				else
				{
					double sx2 = 0, sy = 0, sxy = 0, sxx = 0;
					for (int i = 0; i < ns; i++) { Cell *c = s->cells.get (src[i].r, src[i].c); sx2 += i; sy += c->num; sxy += i * c->num; sxx += (double) i * i; }
					double den = ns * sxx - sx2 * sx2;
					double sl = den != 0 ? (ns * sxy - sx2 * sy) / den : 0, ic = (sy - sl * sx2) / ns;
					v = ic + sl * step;
					v = clean15 (v);
				}
			}
			x->kind = K_NUM; x->vt = V_NUM; x->num = v;
		}
		else if (allList && list >= 0)
		{
			int l2, i1 = li0;
			if (ns > 1) { Cell *x1 = s->cells.get (src[1].r, src[1].c); list_word (x1->str, &l2, &i1); }
			int d = ns > 1 ? i1 - li0 : 1;
			const char *w = list_item (list, li0 + d * step);
			// the case of the source: UPPER, lower, Title
			char t[32]; scpy (t, w, sizeof t);
			bool up = true, low = true;
			for (const char *p = x0->str; *p; p++) { if (*p >= 'a' && *p <= 'z') up = false; if (*p >= 'A' && *p <= 'Z') low = false; }
			for (char *p = t; *p; p++) { if (up && *p >= 'a' && *p <= 'z') *p -= 32; if (low && *p >= 'A' && *p <= 'Z') *p += 32; }
			x->kind = K_STR; x->vt = V_STR; x->str = sdup (t);
		}
		else if (allTextNum)
		{
			int pf; long long v0, v1 = 0;
			text_num (x0->str, &pf, &v0);
			long long d = 1;
			if (ns > 1) { Cell *x1 = s->cells.get (src[1].r, src[1].c); int p1; if (text_num (x1->str, &p1, &v1)) d = v1 - v0; }
			long long v = v0 + d * step;
			if (v < 0) v = -v;
			Buf t; t.putn (x0->str, pf); t.puti (v);
			x->kind = K_STR; x->vt = V_STR; x->str = t.take ();
		}
		else if (sx && sx->kind == K_FORM)
		{
			x->kind = K_FORM; x->f = formula_copy (sx->f, dst[j].r - from.r, dst[j].c - from.c); x->epoch = 0;
		}
		else if (sx && sx->kind != K_NONE)
		{
			x->kind = sx->kind; x->vt = sx->vt; x->err = sx->err; x->num = sx->num; x->str = sx->str ? sdup (sx->str) : 0; x->epoch = b.epoch;
		}
		cell_drop_if_plain (s, x);
	}
}
// The source range `src` extended to `to` (down, right, up or left: whichever `to` goes).
static void fill_range (Book &b, Sheet *s, Rect src, Rect to)
{
	bool down = to.r1 > src.r1, up = to.r0 < src.r0, right = to.c1 > src.c1, left = to.c0 < src.c0;
	int ns, nd;
	if (down || up)
	{
		ns = src.r1 - src.r0 + 1; nd = down ? to.r1 - src.r1 : src.r0 - to.r0;
		FillCell *a = (FillCell *) malloc (ns * sizeof (FillCell)), *d = (FillCell *) malloc (nd * sizeof (FillCell));
		for (int c = src.c0; c <= src.c1; c++)
		{
			for (int i = 0; i < ns; i++) { a[i].r = down ? src.r0 + i : src.r1 - i; a[i].c = c; }
			for (int i = 0; i < nd; i++) { d[i].r = down ? src.r1 + 1 + i : src.r0 - 1 - i; d[i].c = c; }
			fill_line (b, s, a, ns, d, nd);
		}
		free (a); free (d);
	}
	else if (right || left)
	{
		ns = src.c1 - src.c0 + 1; nd = right ? to.c1 - src.c1 : src.c0 - to.c0;
		FillCell *a = (FillCell *) malloc (ns * sizeof (FillCell)), *d = (FillCell *) malloc (nd * sizeof (FillCell));
		for (int r = src.r0; r <= src.r1; r++)
		{
			for (int i = 0; i < ns; i++) { a[i].c = right ? src.c0 + i : src.c1 - i; a[i].r = r; }
			for (int i = 0; i < nd; i++) { d[i].c = right ? src.c1 + 1 + i : src.c0 - 1 - i; d[i].r = r; }
			fill_line (b, s, a, ns, d, nd);
		}
		free (a); free (d);
	}
	sheet_touched (s);
}
// Fill Down / Right (Ctrl+D / Ctrl+R): the first row / column of the selection copied over the rest.
static void fill_copy (Book &b, Sheet *s, Rect sel, bool down)
{
	Rect src = sel;
	if (down) src.r1 = src.r0; else src.c1 = src.c0;
	if ((down && sel.r1 == sel.r0) || (!down && sel.c1 == sel.c0)) return;
	copy_range (b, s, src, false);
	Rect rest = sel;
	if (down) rest.r0++; else rest.c0++;
	if (down) for (int r = rest.r0; r <= rest.r1; r++) paste_range (b, s, r, src.c0, Rect { r, src.c0, r, src.c1 });
	else for (int c = rest.c0; c <= rest.c1; c++) paste_range (b, s, src.r0, c, Rect { src.r0, c, src.r1, c });
	clip_free ();
}

// ---- sorting ------------------------------------------------------------------------------------------
struct SortKey { int col; bool desc; };
struct SortRow { int r; int order; };
static Book *g_sortBook; static Sheet *g_sortSheet; static const SortKey *g_sortKeys; static int g_nkeys;
static bool g_sortCols;					// (sorting columns by a row's values)
static int sort_rank (const Cell *x) { if (!x || x->kind == K_NONE || (x->vt == V_STR && !x->str[0])) return 4; return x->vt == V_NUM ? 0 : x->vt == V_STR ? 1 : x->vt == V_BOOL ? 2 : 3; }
static int sort_cmp (const void *pa, const void *pb)
{
	const SortRow *a = (const SortRow *) pa, *b = (const SortRow *) pb;
	for (int k = 0; k < g_nkeys; k++)
	{
		int kc = g_sortKeys[k].col;
		Cell *x = g_sortCols ? g_sortSheet->cells.get (kc, a->r) : g_sortSheet->cells.get (a->r, kc);
		Cell *y = g_sortCols ? g_sortSheet->cells.get (kc, b->r) : g_sortSheet->cells.get (b->r, kc);
		int rx = sort_rank (x), ry = sort_rank (y);
		if (rx == 4 || ry == 4) { if (rx != ry) return rx == 4 ? 1 : -1; continue; }	// (empty: last, both ways)
		int c;
		if (rx != ry) c = rx < ry ? -1 : 1;
		else if (rx == 1) c = ci_cmp (x->str, (int) strlen (x->str), y->str, (int) strlen (y->str));
		else if (rx == 3) c = x->err < y->err ? -1 : x->err > y->err ? 1 : 0;
		else c = x->num < y->num ? -1 : x->num > y->num ? 1 : 0;
		if (c) return g_sortKeys[k].desc ? -c : c;
	}
	return a->order - b->order;					// (stable)
}
// The rows of r sorted by the keys (their columns: sheet columns); header: the first row stays.
// byCols: the columns sorted by rows' values instead (key.col: a row).
static void sort_range (Book &b, Sheet *s, Rect r, const SortKey *keys, int nkeys, bool header, bool byCols = false)
{
	if (header) { if (byCols) r.c0++; else r.r0++; }
	int n = byCols ? r.c1 - r.c0 + 1 : r.r1 - r.r0 + 1;
	if (n < 2) return;
	SortRow *rows = (SortRow *) malloc (n * sizeof (SortRow));
	for (int i = 0; i < n; i++) { rows[i].r = (byCols ? r.c0 : r.r0) + i; rows[i].order = i; }
	g_sortBook = &b; g_sortSheet = s; g_sortKeys = keys; g_nkeys = nkeys; g_sortCols = byCols;
	qsort (rows, n, sizeof (SortRow), sort_cmp);
	// the cells taken out, then put back at their new rows (formulas moved as copies: relative refs follow)
	int w = byCols ? r.r1 - r.r0 + 1 : r.c1 - r.c0 + 1;
	Cell **tmp = (Cell **) calloc ((size_t) n * w, sizeof (Cell *));
	for (int i = 0; i < n; i++)
		for (int j = 0; j < w; j++)
		{
			int rr = byCols ? r.r0 + j : rows[i].r, cc = byCols ? rows[i].r : r.c0 + j;
			tmp[(size_t) i * w + j] = s->cells.take (rr, cc);
		}
	for (int i = 0; i < n; i++)
		for (int j = 0; j < w; j++)
		{
			Cell *x = tmp[(size_t) i * w + j];
			if (!x) continue;
			int nr = byCols ? x->r : r.r0 + i, nc = byCols ? r.c0 + i : x->c;
			if (x->kind == K_FORM)
			{
				Formula *g = formula_copy (x->f, nr - x->r, nc - x->c);
				formula_free (x->f); x->f = g; x->epoch = 0;
			}
			x->r = nr; x->c = nc;
			s->cells.put (x);
		}
	free (tmp); free (rows);
	sheet_touched (s);
}

// ---- merging --------------------------------------------------------------------------------------------
static void merge_range (Book &b, Sheet *s, Rect r)
{
	(void) b;
	for (int i = 0; i < s->nmerge; ) { if (rect_meets (s->merges[i], r)) merge_del (s, i); else i++; }
	if (r.r0 == r.r1 && r.c0 == r.c1) return;
	for (int rr = r.r0; rr <= r.r1; rr++)				// (the top-left cell's content only)
		for (int c = r.c0; c <= r.c1; c++)
		{
			if (rr == r.r0 && c == r.c0) continue;
			Cell *x = s->cells.get (rr, c);
			if (x) { cell_clear (x); cell_drop_if_plain (s, x); }
		}
	merge_add (s, r);
	sheet_touched (s);
}
static void unmerge_range (Sheet *s, Rect r) { for (int i = 0; i < s->nmerge; ) { if (rect_meets (s->merges[i], r)) merge_del (s, i); else i++; } }

// ---- sheets -------------------------------------------------------------------------------------------
struct DropArg { int id; };
static void drop_one (Sheet *s, Cell *x, void *a) { formula_drop_sheet (x->f, s->id, ((DropArg *) a)->id); x->epoch = 0; }
static void delete_sheet (Book &b, int i)
{
	if (b.ns <= 1 || i < 0 || i >= b.ns) return;
	Sheet *s = b.sh[i];
	memmove (b.sh + i, b.sh + i + 1, (b.ns - i - 1) * sizeof (Sheet *));
	b.ns--;
	DropArg g; g.id = s->id;
	each_formula (b, drop_one, &g);
	for (int j = 0; j < b.ns; j++)
		for (int k = 0; k < b.sh[j]->ncharts; k++) if (b.sh[j]->charts[k]->srcSheet == s->id) b.sh[j]->charts[k]->srcSheet = 0;
	sheet_free (s);
	if (b.active >= b.ns) b.active = b.ns - 1;
	for (int j = 0; j < b.ns; j++) sheet_touched (b.sh[j]);
}
static void move_sheet (Book &b, int from, int to)
{
	if (from < 0 || from >= b.ns || to < 0 || to >= b.ns || from == to) return;
	Sheet *s = b.sh[from];
	if (from < to) memmove (b.sh + from, b.sh + from + 1, (to - from) * sizeof (Sheet *));
	else memmove (b.sh + to + 1, b.sh + to, (from - to) * sizeof (Sheet *));
	b.sh[to] = s;
}
static Chart *chart_dup (const Chart *c) { Chart *d = (Chart *) malloc (sizeof (Chart)); *d = *c; return d; }
static Sheet *duplicate_sheet (Book &b, int i)
{
	Sheet *s = b.sh[i];
	char nm[64]; snprintf (nm, sizeof nm, "%.48s (2)", s->name);
	for (int k = 3; book_sheet_index (b, nm) >= 0; k++) snprintf (nm, sizeof nm, "%.48s (%d)", s->name, k);
	Sheet *d = book_add_sheet (b, nm, i + 1);
	if (!d) return 0;
	for (int k = 0; k < s->cells.cap; k++)
	{
		Cell *x = s->cells.t[k];
		if (!x) continue;
		Cell *y = d->cells.add (x->r, x->c);
		y->style = x->style; y->kind = x->kind; y->vt = x->vt; y->err = x->err; y->num = x->num;
		y->str = x->str ? sdup (x->str) : 0;
		y->f = x->f ? formula_dup (x->f) : 0;
	}
	memcpy (d->colW, s->colW, MAXC * sizeof (unsigned short));
	memcpy (d->colSt, s->colSt, MAXC * sizeof (unsigned short));
	memcpy (d->colFl, s->colFl, MAXC);
	d->defColW = s->defColW; d->defRowH = s->defRowH;
	if (s->nrows) { d->rows = (RowInfo *) malloc (s->nrows * sizeof (RowInfo)); memcpy (d->rows, s->rows, s->nrows * sizeof (RowInfo)); d->nrows = d->crows = s->nrows; }
	for (int k = 0; k < s->nmerge; k++) merge_add (d, s->merges[k]);
	d->freezeR = s->freezeR; d->freezeC = s->freezeC; d->grid = s->grid; d->tab = s->tab;
	for (int k = 0; k < s->ncharts; k++)
	{
		d->charts = (Chart **) realloc (d->charts, (d->ncharts + 1) * sizeof (Chart *));
		Chart *c = chart_dup (s->charts[k]);
		if (c->srcSheet == s->id) c->srcSheet = d->id;
		d->charts[d->ncharts++] = c;
	}
	cols_changed (d); rows_changed (d); sheet_touched (d);
	return d;
}
} // namespace ss

#endif
