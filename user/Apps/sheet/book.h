//
// book.h -- the workbook: its sheets (up to 16 384 columns x 1 048 576 rows, as Excel's: the cells kept
// in a hash of the used ones), the cells (what was typed -- a number, a text, a formula -- and the value
// shown), the styles (the font, the colours, the alignment, the borders, the number format: interned,
// a cell keeps an index), the columns' widths and the rows' heights (in pixels at 100 %), the merged
// cells, the frozen panes, the charts.
//
#ifndef _sheet_book_h
#define _sheet_book_h

#include "core.h"

namespace ss {

enum { MAXR = 1048576, MAXC = 16384, MAXSHEETS = 256 };
static const unsigned AUTO = 0xFF000000u;		// a colour left to the default (the text's black, no fill)

// ---- values ------------------------------------------------------------------------------------------------
enum { V_EMPTY, V_NUM, V_STR, V_BOOL, V_ERR };
enum { E_NONE, E_NULL, E_DIV0, E_VALUE, E_REF, E_NAME, E_NUM, E_NA, E_CIRC, E_DEFER };
static const char *const ERR_NAMES[] = { "", "#NULL!", "#DIV/0!", "#VALUE!", "#REF!", "#NAME?", "#NUM!", "#N/A", "#CIRC!", "#N/A" };

// ---- the styles ----------------------------------------------------------------------------------------------
enum { HA_GENERAL, HA_LEFT, HA_CENTER, HA_RIGHT, HA_FILL, HA_JUSTIFY };
enum { VA_BOTTOM, VA_CENTER, VA_TOP };
enum { BS_NONE, BS_THIN, BS_MEDIUM, BS_THICK, BS_DASHED, BS_DOTTED, BS_DOUBLE, BS_HAIR };
enum { B_LEFT, B_RIGHT, B_TOP, B_BOTTOM };

struct Style						// (every byte set: compared and hashed whole)
{
	unsigned short font, size;			// the font's family (Book::fonts), its size in 1/10 pt
	unsigned char bold, italic, under, strike;	// under: 1 single, 2 double
	unsigned color, fill;				// the text's colour, the background (AUTO: none)
	unsigned char ha, va, wrap, indent;
	unsigned short fmt, pad0;			// the number format (Book::fmts; 0: General)
	unsigned char bs[4];				// the borders' lines (BS_*), left, right, top, bottom
	unsigned bc[4];					// ... and colours
	void reset () { memset (this, 0, sizeof *this); size = 100; color = fill = AUTO; for (int i = 0; i < 4; i++) bc[i] = 0; }
};

struct Styles
{
	Style *s; int n, cap;
	int *h; int hcap;				// a hash: style -> index (+1)
	Styles () : s (0), n (0), cap (0), h (0), hcap (0) {}
	~Styles () { free (s); free (h); }
	static unsigned hash (const Style &st)
	{
		const unsigned char *p = (const unsigned char *) &st;
		unsigned x = 2166136261u;
		for (unsigned i = 0; i < sizeof st; i++) { x ^= p[i]; x *= 16777619u; }
		return x;
	}
	void rehash ()
	{
		free (h);
		hcap = hcap ? hcap * 2 : 256;
		h = (int *) calloc (hcap, sizeof (int));
		for (int i = 0; i < n; i++)
		{
			unsigned k = hash (s[i]) & (hcap - 1);
			while (h[k]) k = (k + 1) & (hcap - 1);
			h[k] = i + 1;
		}
	}
	int intern (const Style &st)
	{
		if (!hcap || n * 2 >= hcap) rehash ();
		unsigned k = hash (st) & (hcap - 1);
		while (h[k])
		{
			if (!memcmp (&s[h[k] - 1], &st, sizeof st)) return h[k] - 1;
			k = (k + 1) & (hcap - 1);
		}
		if (n >= 65535) return 0;
		if (n == cap) { cap = cap ? cap * 2 : 64; s = (Style *) realloc (s, cap * sizeof (Style)); }
		s[n] = st;
		h[k] = n + 1;
		return n++;
	}
	void release () { free (s); free (h); s = 0; h = 0; n = cap = hcap = 0; }
};

// ---- a cell -----------------------------------------------------------------------------------------------
enum { K_NONE, K_NUM, K_STR, K_BOOL, K_ERR, K_FORM };	// what the cell holds (K_NONE: only a style)
enum { CF_BUSY = 1, CF_STACK = 2, CF_VOLATILE = 4 };

struct Formula;
static void formula_free (Formula *f);

struct Cell
{
	int r, c;
	unsigned short style;
	unsigned char kind;				// K_*
	unsigned char vt;				// the value: V_* (a formula's result)
	unsigned char err;				// its error (V_ERR)
	unsigned char fl;				// CF_* (the computation's marks)
	unsigned epoch;					// the recalculation that computed it
	double num;					// V_NUM, V_BOOL (0 / 1)
	char *str;					// V_STR (malloc'd UTF-8)
	Formula *f;					// K_FORM
};

static Cell *cell_new (int r, int c)
{
	Cell *x = (Cell *) calloc (1, sizeof (Cell));
	x->r = r; x->c = c;
	return x;
}
static void cell_clear_value (Cell *x) { free (x->str); x->str = 0; x->num = 0; x->vt = V_EMPTY; x->err = 0; }
static void cell_clear (Cell *x)		// the content (the style stays)
{
	cell_clear_value (x);
	if (x->f) { formula_free (x->f); x->f = 0; }
	x->kind = K_NONE; x->fl = 0;
}
static void cell_free (Cell *x) { if (x) { cell_clear (x); free (x); } }

// The used cells: open addressing on (row, column).
struct CellMap
{
	Cell **t; int cap, n;
	CellMap () : t (0), cap (0), n (0) {}
	~CellMap () { clear (); free (t); }
	static unsigned hash (int r, int c)
	{
		unsigned long long k = ((unsigned long long) (unsigned) r << 14) | (unsigned) c;
		k ^= k >> 29; k *= 0xBF58476D1CE4E5B9ULL; k ^= k >> 32;
		return (unsigned) k;
	}
	Cell *get (int r, int c) const
	{
		if (!n) return 0;
		unsigned k = hash (r, c) & (cap - 1);
		while (t[k]) { if (t[k]->r == r && t[k]->c == c) return t[k]; k = (k + 1) & (cap - 1); }
		return 0;
	}
	void grow ()
	{
		int oc = cap; Cell **ot = t;
		cap = cap ? cap * 2 : 1024;
		t = (Cell **) calloc (cap, sizeof (Cell *));
		for (int i = 0; i < oc; i++) if (ot[i]) { unsigned k = hash (ot[i]->r, ot[i]->c) & (cap - 1); while (t[k]) k = (k + 1) & (cap - 1); t[k] = ot[i]; }
		free (ot);
	}
	Cell *add (int r, int c)			// the cell, made when there is none
	{
		Cell *x = get (r, c);
		if (x) return x;
		if ((n + 1) * 10 >= cap * 7) grow ();
		x = cell_new (r, c);
		unsigned k = hash (r, c) & (cap - 1);
		while (t[k]) k = (k + 1) & (cap - 1);
		t[k] = x; n++;
		return x;
	}
	void put (Cell *x)				// (a cell made elsewhere, its place free)
	{
		if ((n + 1) * 10 >= cap * 7) grow ();
		unsigned k = hash (x->r, x->c) & (cap - 1);
		while (t[k]) k = (k + 1) & (cap - 1);
		t[k] = x; n++;
	}
	Cell *take (int r, int c)			// out of the map (not freed)
	{
		if (!n) return 0;
		unsigned k = hash (r, c) & (cap - 1);
		while (t[k] && !(t[k]->r == r && t[k]->c == c)) k = (k + 1) & (cap - 1);
		if (!t[k]) return 0;
		Cell *x = t[k];
		t[k] = 0; n--;
		unsigned j = k;					// (backward shift: the chain stays whole)
		for (;;)
		{
			j = (j + 1) & (cap - 1);
			if (!t[j]) break;
			unsigned h = hash (t[j]->r, t[j]->c) & (cap - 1);
			bool move = k <= j ? (h <= k || h > j) : (h <= k && h > j);
			if (move) { t[k] = t[j]; t[j] = 0; k = j; }
		}
		return x;
	}
	void del (int r, int c) { cell_free (take (r, c)); }
	void clear () { for (int i = 0; i < cap; i++) if (t[i]) { cell_free (t[i]); t[i] = 0; } n = 0; }
};

// ---- rows, columns, merged cells, charts -------------------------------------------------------------------
enum { RF_HIDDEN = 1, RF_CUSTOM = 2, RF_AUTO = 4 };
struct RowInfo { int r; unsigned short h, style; unsigned char fl, pad[3]; };
struct Rect { int r0, c0, r1, c1; };
static bool rect_has (const Rect &a, int r, int c) { return r >= a.r0 && r <= a.r1 && c >= a.c0 && c <= a.c1; }
static bool rect_meets (const Rect &a, const Rect &b) { return a.r0 <= b.r1 && b.r0 <= a.r1 && a.c0 <= b.c1 && b.c0 <= a.c1; }

enum { CH_COLUMN, CH_BAR, CH_LINE, CH_AREA, CH_PIE, CH_SCATTER, CH_COUNT };
enum { LG_NONE, LG_RIGHT, LG_BOTTOM, LG_TOP };
struct Chart
{
	int type;
	int x, y, w, h;					// its place on the sheet, px at 100 %
	int srcSheet;					// the data: a sheet's id and a range
	Rect src;
	bool byRows;					// the series in rows (else in columns)
	bool head, side;				// the first row / column: the names (series', categories')
	bool stacked, grid;
	int legend;
	char title[96];
};

// ---- a sheet -------------------------------------------------------------------------------------------
struct Sheet
{
	int id;						// (formulas name a sheet by it: renames, moves keep them)
	char name[64];
	CellMap cells;
	unsigned short *colW, *colSt;			// [MAXC] the width (px at 100 %; 0: the default), a style
	unsigned char *colFl;				// [MAXC] RF_HIDDEN
	int *colX; bool colXok;				// the columns' left edges (prefix sums) [MAXC + 1]
	int defColW, defRowH;
	RowInfo *rows; int nrows, crows;		// the rows that differ, by r
	int *rowD;					// rowD[i]: the height added by rows[0..i-1] (vs the default)
	bool rowDok;
	Rect *merges; int nmerge, cmerge;
	int freezeR, freezeC;				// frozen rows / columns (0: none)
	bool grid;					// the grid's lines shown
	unsigned tab;					// the tab's colour (AUTO: none)
	int curR, curC, ancR, ancC, topR, leftC;	// the view: the cursor, the selection's anchor, the scroll
	Chart **charts; int ncharts;
	int maxR, maxC; bool boundsOk;			// the used area (the cells with content)
	Cell **forms; int nforms, cforms; bool formsOk;	// the formula cells, in order (rebuilt when needed)
};

static Sheet *sheet_new (int id, const char *name)
{
	Sheet *s = (Sheet *) calloc (1, sizeof (Sheet));	// (a CellMap all zero: empty)
	s->id = id;
	scpy (s->name, name, sizeof s->name);
	s->colW = (unsigned short *) calloc (MAXC, sizeof (unsigned short));
	s->colSt = (unsigned short *) calloc (MAXC, sizeof (unsigned short));
	s->colFl = (unsigned char *) calloc (MAXC, 1);
	s->colX = (int *) malloc ((MAXC + 1) * sizeof (int));
	s->defColW = 80; s->defRowH = 17;
	s->grid = true; s->tab = AUTO;
	s->maxR = s->maxC = -1;
	return s;
}
static void chart_free (Chart *c) { free (c); }
static void sheet_free (Sheet *s)
{
	if (!s) return;
	s->cells.clear (); free (s->cells.t);
	free (s->colW); free (s->colSt); free (s->colFl); free (s->colX);
	free (s->rows); free (s->rowD); free (s->merges); free (s->forms);
	for (int i = 0; i < s->ncharts; i++) chart_free (s->charts[i]);
	free (s->charts);
	free (s);
}

// -- the columns
static int col_w (Sheet *s, int c) { return s->colFl[c] & RF_HIDDEN ? 0 : s->colW[c] ? s->colW[c] : s->defColW; }
static void cols_changed (Sheet *s) { s->colXok = false; }
static int col_x (Sheet *s, int c)			// the column's left edge (px at 100 %)
{
	if (!s->colXok)
	{
		int x = 0;
		for (int i = 0; i < MAXC; i++) { s->colX[i] = x; x += col_w (s, i); }
		s->colX[MAXC] = x;
		s->colXok = true;
	}
	return s->colX[iclamp (c, 0, MAXC)];
}
static int col_at (Sheet *s, int x)			// the column under x (px at 100 %)
{
	col_x (s, 0);
	if (x < 0) return 0;
	int lo = 0, hi = MAXC - 1;
	while (lo < hi) { int m = (lo + hi + 1) / 2; if (s->colX[m] <= x) lo = m; else hi = m - 1; }
	return lo;
}

// -- the rows
static int row_find (Sheet *s, int r)			// its index in rows, else -(where it would go) - 1
{
	int lo = 0, hi = s->nrows - 1;
	while (lo <= hi) { int m = (lo + hi) / 2; if (s->rows[m].r == r) return m; if (s->rows[m].r < r) lo = m + 1; else hi = m - 1; }
	return -lo - 1;
}
static RowInfo *row_info (Sheet *s, int r) { int i = row_find (s, r); return i >= 0 ? &s->rows[i] : 0; }
static RowInfo *row_add (Sheet *s, int r)
{
	int i = row_find (s, r);
	if (i >= 0) return &s->rows[i];
	i = -i - 1;
	if (s->nrows == s->crows) { s->crows = s->crows ? s->crows * 2 : 64; s->rows = (RowInfo *) realloc (s->rows, s->crows * sizeof (RowInfo)); }
	memmove (s->rows + i + 1, s->rows + i, (s->nrows - i) * sizeof (RowInfo));
	s->nrows++;
	memset (&s->rows[i], 0, sizeof (RowInfo));
	s->rows[i].r = r;
	s->rowDok = false;
	return &s->rows[i];
}
static void row_drop_if_plain (Sheet *s, int r)	// forget a row that no longer differs
{
	int i = row_find (s, r);
	if (i < 0) return;
	RowInfo &ri = s->rows[i];
	if (ri.style || (ri.fl & (RF_HIDDEN | RF_CUSTOM | RF_AUTO))) return;
	memmove (s->rows + i, s->rows + i + 1, (s->nrows - i - 1) * sizeof (RowInfo));
	s->nrows--;
	s->rowDok = false;
}
static int ri_h (Sheet *s, const RowInfo &ri) { return ri.fl & RF_HIDDEN ? 0 : (ri.fl & (RF_CUSTOM | RF_AUTO)) ? ri.h : s->defRowH; }
static int row_h (Sheet *s, int r) { RowInfo *ri = row_info (s, r); return ri ? ri_h (s, *ri) : s->defRowH; }
static void rows_changed (Sheet *s) { s->rowDok = false; }
static void row_prefix (Sheet *s)
{
	if (s->rowDok) return;
	free (s->rowD);
	s->rowD = (int *) malloc ((s->nrows + 1) * sizeof (int));
	int d = 0;
	for (int i = 0; i < s->nrows; i++) { s->rowD[i] = d; d += ri_h (s, s->rows[i]) - s->defRowH; }
	s->rowD[s->nrows] = d;
	s->rowDok = true;
}
static long long row_y (Sheet *s, int r)		// the row's top (px at 100 %)
{
	row_prefix (s);
	int i = row_find (s, r); if (i < 0) i = -i - 1;
	return (long long) r * s->defRowH + s->rowD[i];
}
static int row_at (Sheet *s, long long y)		// the row under y
{
	if (y < 0) return 0;
	int lo = 0, hi = MAXR - 1;
	while (lo < hi) { int m = (lo + hi + 1) / 2; if (row_y (s, m) <= y) lo = m; else hi = m - 1; }
	return lo;
}

// -- the merged cells
static int merge_at (Sheet *s, int r, int c)		// the merge holding (r, c), -1
{
	for (int i = 0; i < s->nmerge; i++) if (rect_has (s->merges[i], r, c)) return i;
	return -1;
}
static void merge_add (Sheet *s, Rect m)
{
	if (s->nmerge == s->cmerge) { s->cmerge = s->cmerge ? s->cmerge * 2 : 8; s->merges = (Rect *) realloc (s->merges, s->cmerge * sizeof (Rect)); }
	s->merges[s->nmerge++] = m;
}
static void merge_del (Sheet *s, int i) { if (i < 0 || i >= s->nmerge) return; s->merges[i] = s->merges[--s->nmerge]; }

// -- the used area
static void sheet_touched (Sheet *s) { s->boundsOk = false; s->formsOk = false; }
static void sheet_bounds (Sheet *s)
{
	if (s->boundsOk) return;
	int mr = -1, mc = -1;
	for (int i = 0; i < s->cells.cap; i++)
	{
		Cell *x = s->cells.t[i];
		if (x && x->kind != K_NONE) { if (x->r > mr) mr = x->r; if (x->c > mc) mc = x->c; }
	}
	s->maxR = mr; s->maxC = mc;
	s->boundsOk = true;
}

// ---- the workbook -------------------------------------------------------------------------------------
struct Book
{
	Sheet *sh[MAXSHEETS]; int ns;
	int nextId;
	int active;					// the sheet shown
	Styles styles;
	char *fonts[256]; int nfonts;			// the font families named by the styles
	char **fmts; int nfmts, cfmts;			// the number formats' codes (0: "General")
	unsigned epoch;					// the recalculation's count
	bool dateSystem1904;				// (read from a file; the dates' origin then 1 January 1904)
};

static int book_font (Book &b, const char *name)	// a family's index (added)
{
	for (int i = 0; i < b.nfonts; i++) if (ci_eq (b.fonts[i], name)) return i;
	if (b.nfonts >= 256) return 0;
	b.fonts[b.nfonts] = sdup (name);
	return b.nfonts++;
}
static int book_fmt (Book &b, const char *code)	// a format's index (added)
{
	if (!code || !code[0] || ci_eq (code, "General")) return 0;
	for (int i = 1; i < b.nfmts; i++) if (!strcmp (b.fmts[i], code)) return i;
	if (b.nfmts >= 65535) return 0;
	if (b.nfmts == b.cfmts) { b.cfmts = b.cfmts ? b.cfmts * 2 : 32; b.fmts = (char **) realloc (b.fmts, b.cfmts * sizeof (char *)); }
	b.fmts[b.nfmts] = sdup (code);
	return b.nfmts++;
}
static const char *book_fmt_code (Book &b, int i) { return i > 0 && i < b.nfmts ? b.fmts[i] : "General"; }

static const char *DEFAULT_FONT = "Liberation Sans";
static void book_init (Book &b)
{
	memset (&b.sh, 0, sizeof b.sh);
	b.ns = 0; b.nextId = 1; b.active = 0; b.epoch = 1; b.dateSystem1904 = false;
	b.nfonts = 0; b.nfmts = 0; b.cfmts = 32;
	b.fmts = (char **) malloc (32 * sizeof (char *));
	b.fmts[0] = sdup ("General"); b.nfmts = 1;		// (index 0)
	b.styles.release ();
	book_font (b, DEFAULT_FONT);
	Style d; d.reset ();
	b.styles.intern (d);					// (index 0: the default)
}
static void book_clear (Book &b)
{
	for (int i = 0; i < b.ns; i++) sheet_free (b.sh[i]);
	b.ns = 0;
	for (int i = 0; i < b.nfonts; i++) free (b.fonts[i]);
	for (int i = 0; i < b.nfmts; i++) free (b.fmts[i]);
	free (b.fmts); b.fmts = 0; b.nfmts = b.cfmts = 0; b.nfonts = 0;
	b.styles.release ();
}
static Sheet *book_sheet_by_id (Book &b, int id)
{
	for (int i = 0; i < b.ns; i++) if (b.sh[i]->id == id) return b.sh[i];
	return 0;
}
static int book_sheet_index (Book &b, const char *name, int n = -1)
{
	if (n < 0) n = (int) strlen (name);
	for (int i = 0; i < b.ns; i++)
		if ((int) strlen (b.sh[i]->name) == n && ci_cmp (b.sh[i]->name, n, name, n) == 0) return i;
	return -1;
}
static Sheet *book_add_sheet (Book &b, const char *name, int at = -1)
{
	if (b.ns >= MAXSHEETS) return 0;
	if (at < 0 || at > b.ns) at = b.ns;
	Sheet *s = sheet_new (b.nextId++, name);
	memmove (b.sh + at + 1, b.sh + at, (b.ns - at) * sizeof (Sheet *));
	b.sh[at] = s; b.ns++;
	return s;
}
static void book_unique_sheet_name (Book &b, char *out, int cap, const char *base = "Sheet")
{
	for (int k = b.ns + 1; ; k++)
	{
		snprintf (out, cap, "%s%d", base, k);
		if (book_sheet_index (b, out) < 0) return;
	}
}

// ---- addresses ---------------------------------------------------------------------------------------------
static int col_name (int c, char *o)			// 0 -> "A", 26 -> "AA"; its length
{
	char t[8]; int n = 0;
	c++;
	while (c > 0) { int m = (c - 1) % 26; t[n++] = (char) ('A' + m); c = (c - 1) / 26; }
	for (int i = 0; i < n; i++) o[i] = t[n - 1 - i];
	o[n] = 0;
	return n;
}
static void cell_name (int r, int c, char *o, bool absR = false, bool absC = false)
{
	int n = 0;
	if (absC) o[n++] = '$';
	n += col_name (c, o + n);
	if (absR) o[n++] = '$';
	snprintf (o + n, 12, "%d", r + 1);
}
// "B7" -> (6, 1); with $ allowed; false when it is not an address.
static bool parse_cell_name (const char *s, int *r, int *c)
{
	int i = 0, col = 0, row = 0, nl = 0, nd = 0;
	if (s[i] == '$') i++;
	while ((s[i] >= 'A' && s[i] <= 'Z') || (s[i] >= 'a' && s[i] <= 'z')) { col = col * 26 + ((s[i] & 0xDF) - 'A' + 1); i++; if (++nl > 3) return false; }
	if (s[i] == '$') i++;
	while (s[i] >= '0' && s[i] <= '9') { row = row * 10 + (s[i] - '0'); i++; if (++nd > 7) return false; }
	if (s[i] || !nl || !nd || row < 1 || row > MAXR || col < 1 || col > MAXC) return false;
	*r = row - 1; *c = col - 1;
	return true;
}

} // namespace ss

#endif
