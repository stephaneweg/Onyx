//
// doc.h -- Writer's document: paragraphs of characters (Unicode code points), each character with
// a character format -- an index into the document's table of them (a font of its font table, a
// size, bold / italic / underline / strike-through / superscript / subscript, a colour, a
// highlight; an image, a field) -- and each paragraph with its paragraph format (a style, the
// alignment, the indents, the spacing, a list, its tab stops; the table cell it is in); the page's
// setup; the header and the footer; the edits, undone and redone.
//
// The STORIES: the body, the header and the footer (of every page -- but the first when it has its
// own: PageSetup::titlePg), the first page's header and footer. The one being edited is in d.p / d.n
// (all the editing works on it); the others wait in d.st[]. doc_story () switches.
//
// A TABLE is a run of paragraphs: each says which table (ParaFmt::tbl, an index into d.tbl + 1) and
// which cell (row, column: the cell's top left in the grid) it is in -- a cell's paragraphs follow
// one another, the cells row by row. A cell always keeps one paragraph at least. The Table itself
// (its columns' widths, the cells' spans and shading, the borders) is a VALUE: a change of the table
// makes a new one and points its paragraphs at it, so undoing the paragraphs undoes the table too.
// The fields (d.fld) and the formats are only ever added as well.
//
// A FIELD is the character FIELD_CHAR whose format names the field (CharFmt::fld): the page's number,
// the number of pages, the date, the time, a mail merge's field (shown «name»). It is laid out and
// drawn as its text of the moment (layout.h).
//
// A place in the text is a Pos: a paragraph and an offset in it (0 .. its length). An edit goes
// through begin () / end (): the paragraphs it touches are copied first, so undo () puts them back
// (and redo () the edited ones); typing a word, deleting a run of characters, are one edit each.
//
#ifndef _writer_doc_h
#define _writer_doc_h

namespace wr {

// ---- small helpers -----------------------------------------------------------------------------------
static inline int slen (const char *s) { int n = 0; while (s && s[n]) n++; return n; }
static inline void scpy (char *d, const char *s, int cap) { int i = 0; for (; s && s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = 0; }
static inline int lower (int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static int sicmp (const char *a, const char *b)
{
	for (;; a++, b++) { int x = lower ((unsigned char) *a), y = lower ((unsigned char) *b); if (x != y || !x) return x - y; }
}
template <class T> static inline T wmin (T a, T b) { return a < b ? a : b; }
template <class T> static inline T wmax (T a, T b) { return a > b ? a : b; }
template <class T> static inline T wclamp (T v, T lo, T hi) { return v < lo ? lo : v > hi ? hi : v; }

// ---- formats -----------------------------------------------------------------------------------------
enum { CF_BOLD = 1, CF_ITALIC = 2, CF_UNDER = 4, CF_STRIKE = 8, CF_SUPER = 16, CF_SUB = 32 };
static const unsigned AUTO = 0xFF000000u;		// the automatic text colour (black) / no highlight

struct CharFmt
{
	short font;			// the document's font table's index
	short size;			// half-points (24: 12 pt)
	unsigned short flags;		// CF_*
	unsigned color, hilite;		// 0xRRGGBB, or AUTO
	int obj;			// an image's (the character U+FFFC): its index in the document's images + 1
	int ow, oh;			// ... its size on the page (twips)
	int fld;			// a field's (the character FIELD_CHAR): its index in the document's fields + 1
	CharFmt () : font (0), size (24), flags (0), color (0xFF000000u), hilite (0xFF000000u), obj (0), ow (0), oh (0), fld (0) {}
	bool same (const CharFmt &o) const
	{
		return font == o.font && size == o.size && flags == o.flags && color == o.color && hilite == o.hilite
		    && obj == o.obj && ow == o.ow && oh == o.oh && fld == o.fld;
	}
};
static const unsigned OBJ_CHAR = 0xFFFC;		// an image's place in the text
static const unsigned FIELD_CHAR = 0xFFF9;		// a field's

// An image of the document: its pixels, and its file (PNG, JPEG: written back as they came).
struct Image
{
	unsigned *px; int w, h;		// 0xAARRGGBB
	unsigned char *data; unsigned len; bool jpeg;	// (0: none -- written as PNG from the pixels)
	unsigned *cache; int cw, ch;	// (the view's: scaled to its size on the screen)
};

// ---- fields ------------------------------------------------------------------------------------------
enum { FK_NONE, FK_PAGE, FK_PAGES, FK_DATE, FK_TIME, FK_MERGE };
struct Field
{
	int kind;			// FK_*
	char arg[64];			// FK_DATE / FK_TIME: the picture ("dd/MM/yyyy", "HH:mm"); FK_MERGE: the data's field
};

// ---- paragraphs ----------------------------------------------------------------------------------------
enum { AL_LEFT, AL_CENTER, AL_RIGHT, AL_JUSTIFY };
enum { LS_NONE, LS_BULLET, LS_NUMBER };
enum { ST_NORMAL, ST_H1, ST_H2, ST_H3, ST_TITLE, ST_SUBTITLE, ST_QUOTE, ST_CODE, ST_TOC1, ST_TOC2, ST_TOC3, ST_TOCHEAD,
       ST_HEADER, ST_FOOTER, ST_COUNT };
enum { TA_LEFT, TA_CENTER, TA_RIGHT, TA_DECIMAL };	// a tab stop's alignment
enum { TL_NONE, TL_DOT, TL_DASH, TL_LINE };		// ... its leader
enum { MAXTABS = 12 };

struct TabStop { int pos; unsigned char align, leader; };	// pos: twips from the text's left edge (the cell's in a table)

struct ParaFmt
{
	unsigned char style, align, list, level;	// ST_*, AL_*, LS_*, the list's level (0..5)
	short left, right, first;	// the indents, twips (first: the first line's, from left; < 0 hanging)
	short before, after;		// the spacing above / below, twips
	short line;			// line spacing, % of single (100, 115, 150, 200)
	bool pageBreak;			// starts a page
	bool keepNext;			// on the page of the next paragraph (the headings)
	bool keepLines;			// all its lines on one page
	bool widow;			// no line of it alone at a page's top or foot
	unsigned char ntab; TabStop tab[MAXTABS];	// its tab stops (by place); past them, one every 1.25 cm
	short tbl, row, col;		// in the document's table tbl - 1 (0: none), its cell's top left
	bool same (const ParaFmt &o) const
	{
		if (!(style == o.style && align == o.align && list == o.list && level == o.level && left == o.left
		    && right == o.right && first == o.first && before == o.before && after == o.after && line == o.line
		    && pageBreak == o.pageBreak && keepNext == o.keepNext && keepLines == o.keepLines && widow == o.widow
		    && ntab == o.ntab && tbl == o.tbl && row == o.row && col == o.col)) return false;
		for (int i = 0; i < ntab; i++) if (tab[i].pos != o.tab[i].pos || tab[i].align != o.tab[i].align || tab[i].leader != o.tab[i].leader) return false;
		return true;
	}
};

// The styles (a paragraph's; its characters take the style's font, size and weight).
struct Style
{
	const char *name, *font;	// its name (as RTF's stylesheet writes it), its font
	short size; unsigned short flags; unsigned color;
	unsigned char align; short before, after, line, left, right; bool keepNext;
};
static const Style STYLES[ST_COUNT] = {
	{ "Normal",      "Liberation Serif", 24, 0,                    AUTO,     AL_LEFT,   0,   120, 115, 0,   0,   false },
	{ "Heading 1",   "Liberation Sans",  36, CF_BOLD,              AUTO,     AL_LEFT,   360, 120, 100, 0,   0,   true },
	{ "Heading 2",   "Liberation Sans",  30, CF_BOLD,              AUTO,     AL_LEFT,   280, 120, 100, 0,   0,   true },
	{ "Heading 3",   "Liberation Sans",  26, CF_BOLD,              AUTO,     AL_LEFT,   240, 80,  100, 0,   0,   true },
	{ "Title",       "Liberation Sans",  56, CF_BOLD,              AUTO,     AL_CENTER, 240, 240, 100, 0,   0,   true },
	{ "Subtitle",    "Liberation Sans",  32, 0,                    0x595959, AL_CENTER, 0,   240, 100, 0,   0,   true },
	{ "Quote",       "Liberation Serif", 24, CF_ITALIC,            0x404040, AL_LEFT,   120, 240, 115, 720, 720, false },
	{ "Plain Text",  "DejaVu Sans Mono", 20, 0,                    AUTO,     AL_LEFT,   0,   0,   100, 0,   0,   false },
	{ "TOC 1",       "Liberation Serif", 24, 0,                    AUTO,     AL_LEFT,   120, 60,  100, 0,   0,   false },
	{ "TOC 2",       "Liberation Serif", 24, 0,                    AUTO,     AL_LEFT,   0,   60,  100, 240, 0,   false },
	{ "TOC 3",       "Liberation Serif", 24, 0,                    AUTO,     AL_LEFT,   0,   60,  100, 480, 0,   false },
	{ "TOC Heading", "Liberation Sans",  32, CF_BOLD,              AUTO,     AL_LEFT,   240, 240, 100, 0,   0,   true },
	{ "Header",      "Liberation Serif", 20, 0,                    AUTO,     AL_LEFT,   0,   0,   100, 0,   0,   false },
	{ "Footer",      "Liberation Serif", 20, 0,                    AUTO,     AL_LEFT,   0,   0,   100, 0,   0,   false },
};
static inline bool style_toc (int st) { return st >= ST_TOC1 && st <= ST_TOC3; }
static inline int style_outline (int st) { return st >= ST_H1 && st <= ST_H3 ? st - ST_H1 + 1 : 0; }	// a heading's level

static ParaFmt style_para (int st)
{
	const Style &s = STYLES[st];
	ParaFmt p;
	p.style = (unsigned char) st; p.align = s.align; p.list = LS_NONE; p.level = 0;
	p.left = s.left; p.right = s.right; p.first = 0; p.before = s.before; p.after = s.after; p.line = s.line;
	p.pageBreak = false; p.keepNext = s.keepNext; p.keepLines = false; p.widow = true;
	p.ntab = 0;
	for (int i = 0; i < MAXTABS; i++) { p.tab[i].pos = 0; p.tab[i].align = TA_LEFT; p.tab[i].leader = TL_NONE; }
	p.tbl = p.row = p.col = 0;
	return p;
}
// A tab stop added (kept in order of place; one at the same place replaced).
static void pf_add_tab (ParaFmt &p, int pos, int align, int leader)
{
	int i = 0;
	while (i < p.ntab && p.tab[i].pos < pos) i++;
	if (i < p.ntab && p.tab[i].pos == pos) { p.tab[i].align = (unsigned char) align; p.tab[i].leader = (unsigned char) leader; return; }
	if (p.ntab >= MAXTABS) return;
	for (int k = p.ntab; k > i; k--) p.tab[k] = p.tab[k - 1];
	p.tab[i].pos = pos; p.tab[i].align = (unsigned char) align; p.tab[i].leader = (unsigned char) leader;
	p.ntab++;
}
static void pf_del_tab (ParaFmt &p, int i)
{
	if (i < 0 || i >= p.ntab) return;
	for (int k = i; k < p.ntab - 1; k++) p.tab[k] = p.tab[k + 1];
	p.ntab--;
}

// ---- the page ----------------------------------------------------------------------------------------
struct PageSetup
{
	int w, h;			// twips (A4: 11906 x 16838)
	int top, bottom, left, right;	// margins, twips
	int hdr, ftr;			// the header's top / the footer's foot, from the page's edge (twips)
	bool titlePg;			// the first page has its own header and footer
	int start;			// the first page's number
};
static const int A4_W = 11906, A4_H = 16838;

// ---- tables ----------------------------------------------------------------------------------------------
enum { MAXCOLS = 63 };
enum { TB_ALL, TB_NONE, TB_OUTER, TB_ROWS };		// the table's lines: every cell's edges, none, the outline, the rows'

struct TCell
{
	unsigned char cs, rs;		// the columns / rows it spans (1, 1)
	bool covered;			// under another cell's span (no paragraph of its own)
	unsigned fill;			// its shading (AUTO: none)
};
struct Table
{
	int nrows, ncols;
	int *colW;			// each column's width (twips)
	short *rowH;			// each row's least height (twips; 0: as its content)
	TCell *cell;			// nrows x ncols, row by row
	unsigned char border, bw;	// TB_*, the lines' width (eighths of a point)
	unsigned bcolor;		// their colour
	bool header;			// the first row repeated at the top of each page the table runs on
	unsigned char align;		// AL_LEFT / AL_CENTER / AL_RIGHT on the page
	int indent;			// AL_LEFT: from the text's left edge (twips)
	// the layout's (layout.h)
	int x64, w64;			// its left edge (from the text's), its width
	int *colX64;			// each column's left edge (ncols + 1)
	int *rowPage, *rowY64, *rowH64;	// each row's page, top (in the page's text area), height
	int lay;			// (the layout pass that placed it)
};
static inline TCell &tcell (Table *t, int r, int c) { return t->cell[r * t->ncols + c]; }
static inline const TCell &tcell (const Table *t, int r, int c) { return t->cell[r * t->ncols + c]; }

static Table *table_alloc (int nr, int nc)
{
	Table *t = new Table;
	t->nrows = nr; t->ncols = nc;
	t->colW = new int[nc]; t->rowH = new short[nr]; t->cell = new TCell[nr * nc];
	for (int c = 0; c < nc; c++) t->colW[c] = 1440;
	for (int r = 0; r < nr; r++) t->rowH[r] = 0;
	for (int i = 0; i < nr * nc; i++) { t->cell[i].cs = t->cell[i].rs = 1; t->cell[i].covered = false; t->cell[i].fill = AUTO; }
	t->border = TB_ALL; t->bw = 4; t->bcolor = 0x000000; t->header = false; t->align = AL_LEFT; t->indent = 0;
	t->x64 = t->w64 = 0; t->colX64 = new int[nc + 1]; t->rowPage = new int[nr]; t->rowY64 = new int[nr]; t->rowH64 = new int[nr];
	t->lay = -1;
	return t;
}
// A table of nr x nc cells, `width` twips wide (the columns alike).
static Table *table_new (int nr, int nc, int width)
{
	Table *t = table_alloc (nr, nc);
	for (int c = 0; c < nc; c++) t->colW[c] = width / nc;
	return t;
}
static Table *table_copy (const Table *s)
{
	Table *t = table_alloc (s->nrows, s->ncols);
	for (int c = 0; c < s->ncols; c++) t->colW[c] = s->colW[c];
	for (int r = 0; r < s->nrows; r++) t->rowH[r] = s->rowH[r];
	for (int i = 0; i < s->nrows * s->ncols; i++) t->cell[i] = s->cell[i];
	t->border = s->border; t->bw = s->bw; t->bcolor = s->bcolor; t->header = s->header; t->align = s->align; t->indent = s->indent;
	return t;
}
static void table_free (Table *t)
{
	if (!t) return;
	delete[] t->colW; delete[] t->rowH; delete[] t->cell; delete[] t->colX64; delete[] t->rowPage; delete[] t->rowY64; delete[] t->rowH64;
	delete t;
}
static int table_width (const Table *t) { int w = 0; for (int c = 0; c < t->ncols; c++) w += t->colW[c]; return w; }

// ---- positions -----------------------------------------------------------------------------------------
struct Pos
{
	int p, o;			// paragraph, offset
	bool operator== (const Pos &b) const { return p == b.p && o == b.o; }
	bool operator!= (const Pos &b) const { return !(*this == b); }
	bool operator< (const Pos &b) const { return p < b.p || (p == b.p && o < b.o); }
	bool operator<= (const Pos &b) const { return !(b < *this); }
};
static inline Pos mkpos (int p, int o) { Pos r; r.p = p; r.o = o; return r; }

// ---- a paragraph ---------------------------------------------------------------------------------------
// A line of a paragraph as the layout made it (layout.h).
struct Line
{
	int start, end;			// the paragraph's characters [start, end)
	int y64, h64, base64;		// top (from the paragraph's first line), height, baseline (from its top)
	int xEnd64;			// the x of its end (from the paragraph's box's left edge)
	int page, py64;			// its page, its top from the page's top edge
};
enum { CELL_PAD_X = 108, CELL_PAD_Y = 29 };		// a table's cell's inner margins (twips)

struct Para
{
	unsigned *ch; unsigned short *cf;	// its characters and their formats
	int len, cap;
	unsigned short endCf;		// its mark's format: what is typed in an empty paragraph / at its end
	ParaFmt pf;
	// (the layout's: layout.h)
	Line *ln; int nln, lncap;	// its lines
	int *xs; int xscap;		// each character's x (and its end's), 1/64 px from its box's left edge
	unsigned char *lead;		// a tab's leader (TL_*: its stop's), for each tab character (xs[]'s size)
	int x64, w64;			// its box: its left edge (from the page's), its width (a cell's inside, or the text's)
	int h64;			// its lines' height (1/64 px, at the zoom)
	int num;			// its number in a numbered list
	int fpage, fpages;		// the page / the count of pages its fields were laid out with (-1: none shown)
	bool dirty;			// to be laid out again
};

static Para *para_new (int cap = 16)
{
	Para *q = new Para;
	q->cap = cap < 4 ? 4 : cap; q->len = 0;
	q->ch = new unsigned[q->cap]; q->cf = new unsigned short[q->cap];
	q->endCf = 0; q->pf = style_para (ST_NORMAL);
	q->ln = 0; q->nln = q->lncap = 0; q->xs = 0; q->xscap = 0; q->lead = 0; q->x64 = 0; q->w64 = -1; q->h64 = 0; q->num = 0;
	q->fpage = q->fpages = -1; q->dirty = true;
	return q;
}
static void para_free (Para *q)
{
	delete[] q->ch; delete[] q->cf; delete[] q->ln; delete[] q->xs; delete[] q->lead;
	delete q;
}
static void para_reserve (Para *q, int n)
{
	if (n <= q->cap) return;
	int c = q->cap * 2; if (c < n) c = n;
	unsigned *ch = new unsigned[c]; unsigned short *cf = new unsigned short[c];
	for (int i = 0; i < q->len; i++) { ch[i] = q->ch[i]; cf[i] = q->cf[i]; }
	delete[] q->ch; delete[] q->cf;
	q->ch = ch; q->cf = cf; q->cap = c;
}
static Para *para_copy (const Para *s)
{
	Para *q = para_new (s->len + 4);
	for (int i = 0; i < s->len; i++) { q->ch[i] = s->ch[i]; q->cf[i] = s->cf[i]; }
	q->len = s->len; q->endCf = s->endCf; q->pf = s->pf;
	return q;
}
// Insert n characters of format f at offset o.
static void para_insert (Para *q, int o, const unsigned *s, int n, unsigned short f)
{
	para_reserve (q, q->len + n);
	for (int i = q->len - 1; i >= o; i--) { q->ch[i + n] = q->ch[i]; q->cf[i + n] = q->cf[i]; }
	for (int i = 0; i < n; i++) { q->ch[o + i] = s[i]; q->cf[o + i] = f; }
	q->len += n; q->dirty = true;
}
static void para_erase (Para *q, int a, int b)
{
	if (b <= a) return;
	for (int i = b; i < q->len; i++) { q->ch[i - (b - a)] = q->ch[i]; q->cf[i - (b - a)] = q->cf[i]; }
	q->len -= b - a; q->dirty = true;
}

static inline bool in_table (const Para *q) { return q->pf.tbl != 0; }
static inline bool same_cell (const Para *a, const Para *b) { return a->pf.tbl == b->pf.tbl && a->pf.row == b->pf.row && a->pf.col == b->pf.col; }

// ---- the document --------------------------------------------------------------------------------------
enum { SY_BODY, SY_HEADER, SY_FOOTER, SY_HEADER1, SY_FOOTER1, SY_COUNT };
static const char *const STORY_NAMES[SY_COUNT] = { "Body", "Header", "Footer", "First Page Header", "First Page Footer" };
struct Story { Para **p; int n, cap; };

struct Undo
{
	int story;			// the story it edited
	int first, nOld, nNew;		// the paragraphs [first, first + nOld) became [first, first + nNew)
	Para **old;			// the former ones
	Pos caret0, anchor0, caret1, anchor1;	// the selection before / after
	int kind;			// ED_* (typing coalesces)
};
enum { ED_OTHER, ED_TYPE, ED_DELETE, ED_BACKSPACE };
enum { MAXUNDO = 200 };

struct Doc
{
	Para **p; int n, cap;		// the paragraphs of the story being edited (st[cur]'s)
	Story st[SY_COUNT]; int cur;	// the other stories (st[cur] is empty while it is in p)
	CharFmt *fmt; int nfmt, fmtcap;
	char (*fontName)[48]; int nfont, fontcap;
	Image *img; int nimg, imgcap;	// the images (only ever added: an undone edit's stay unused)
	Field *fld; int nfld, fldcap;	// the fields (only ever added)
	Table **tbl; int ntbl, tblcap;	// the tables (only ever added: a changed one is a new one)
	PageSetup page;
	char mergeSrc[200];		// the mail merge's data (a .card file), "" none
	Undo *undo; int nundo, undocap, uptr;	// [0, uptr): done ones (undo), [uptr, nundo): undone (redo)
	// (an edit under way)
	int edFirst, edN; Para **edOld; Pos edCaret, edAnchor; int edKind;
	unsigned changes;		// counts the edits (the saved state: its count)
};

static void doc_init (Doc &d)
{
	d.p = 0; d.n = d.cap = 0;
	for (int i = 0; i < SY_COUNT; i++) { d.st[i].p = 0; d.st[i].n = d.st[i].cap = 0; }
	d.cur = SY_BODY;
	d.fmt = 0; d.nfmt = d.fmtcap = 0;
	d.fontName = 0; d.nfont = d.fontcap = 0;
	d.img = 0; d.nimg = d.imgcap = 0;
	d.fld = 0; d.nfld = d.fldcap = 0;
	d.tbl = 0; d.ntbl = d.tblcap = 0;
	d.page.w = A4_W; d.page.h = A4_H; d.page.top = d.page.bottom = 1134; d.page.left = d.page.right = 1134;	// 2 cm
	d.page.hdr = d.page.ftr = 567; d.page.titlePg = false; d.page.start = 1;
	d.mergeSrc[0] = 0;
	d.undo = 0; d.nundo = d.undocap = d.uptr = 0;
	d.edFirst = -1; d.edN = 0; d.edOld = 0; d.edKind = ED_OTHER;
	d.changes = 0;
}

static void undo_free (Undo &u) { for (int i = 0; i < u.nOld; i++) para_free (u.old[i]); delete[] u.old; u.old = 0; u.nOld = 0; }

static void doc_clear (Doc &d)
{
	for (int i = 0; i < d.n; i++) para_free (d.p[i]);
	delete[] d.p;
	for (int s = 0; s < SY_COUNT; s++) { for (int i = 0; i < d.st[s].n; i++) para_free (d.st[s].p[i]); delete[] d.st[s].p; }
	delete[] d.fmt; delete[] d.fontName;
	for (int i = 0; i < d.nimg; i++) { delete[] d.img[i].px; delete[] d.img[i].data; delete[] d.img[i].cache; }
	delete[] d.img;
	delete[] d.fld;
	for (int i = 0; i < d.ntbl; i++) table_free (d.tbl[i]);
	delete[] d.tbl;
	for (int i = 0; i < d.nundo; i++) undo_free (d.undo[i]);
	delete[] d.undo;
	if (d.edOld) { for (int i = 0; i < d.edN; i++) para_free (d.edOld[i]); delete[] d.edOld; }
	doc_init (d);
}

// A story's paragraphs (the one being edited: d.p).
static Para **story_p (const Doc &d, int s, int *n)
{
	if (s == d.cur) { *n = d.n; return d.p; }
	*n = d.st[s].n; return d.st[s].p;
}
static bool story_empty (const Doc &d, int s)
{
	int n; Para **p = story_p (d, s, &n);
	return n == 0 || (n == 1 && p[0]->len == 0);
}

// The font table: a name's index (added if new).
static int doc_font (Doc &d, const char *name)
{
	for (int i = 0; i < d.nfont; i++) if (sicmp (d.fontName[i], name) == 0) return i;
	if (d.nfont == d.fontcap)
	{
		int c = d.fontcap ? d.fontcap * 2 : 8;
		char (*t)[48] = new char[c][48];
		for (int i = 0; i < d.nfont; i++) scpy (t[i], d.fontName[i], 48);
		delete[] d.fontName; d.fontName = t; d.fontcap = c;
	}
	scpy (d.fontName[d.nfont], name, 48);
	return d.nfont++;
}

// A format's index in the table (added if new).
static unsigned short doc_fmt (Doc &d, const CharFmt &f)
{
	for (int i = 0; i < d.nfmt; i++) if (d.fmt[i].same (f)) return (unsigned short) i;
	if (d.nfmt >= 65000) return 0;
	if (d.nfmt == d.fmtcap)
	{
		int c = d.fmtcap ? d.fmtcap * 2 : 32;
		CharFmt *t = new CharFmt[c];
		for (int i = 0; i < d.nfmt; i++) t[i] = d.fmt[i];
		delete[] d.fmt; d.fmt = t; d.fmtcap = c;
	}
	d.fmt[d.nfmt] = f;
	return (unsigned short) d.nfmt++;
}

// An image added (its pixels and file bytes taken over): its index.
static int doc_image (Doc &d, unsigned *px, int w, int h, unsigned char *data, unsigned len, bool jpeg)
{
	if (d.nimg == d.imgcap)
	{
		int c = d.imgcap ? d.imgcap * 2 : 4;
		Image *t = new Image[c];
		for (int i = 0; i < d.nimg; i++) t[i] = d.img[i];
		delete[] d.img; d.img = t; d.imgcap = c;
	}
	Image &im = d.img[d.nimg];
	im.px = px; im.w = w; im.h = h; im.data = data; im.len = len; im.jpeg = jpeg;
	im.cache = 0; im.cw = im.ch = 0;
	return d.nimg++;
}
static int doc_image_copy (Doc &d, const Image &s)
{
	unsigned *px = new unsigned[s.w * s.h];
	for (int i = 0; i < s.w * s.h; i++) px[i] = s.px[i];
	unsigned char *data = 0;
	if (s.data) { data = new unsigned char[s.len]; for (unsigned i = 0; i < s.len; i++) data[i] = s.data[i]; }
	return doc_image (d, px, s.w, s.h, data, s.len, s.jpeg);
}

// A field (its kind and argument): its index + 1 (an existing one alike reused).
static int doc_field (Doc &d, int kind, const char *arg)
{
	for (int i = 0; i < d.nfld; i++) if (d.fld[i].kind == kind && sicmp (d.fld[i].arg, arg ? arg : "") == 0) return i + 1;
	if (d.nfld == d.fldcap)
	{
		int c = d.fldcap ? d.fldcap * 2 : 8;
		Field *t = new Field[c];
		for (int i = 0; i < d.nfld; i++) t[i] = d.fld[i];
		delete[] d.fld; d.fld = t; d.fldcap = c;
	}
	d.fld[d.nfld].kind = kind; scpy (d.fld[d.nfld].arg, arg ? arg : "", sizeof d.fld[d.nfld].arg);
	return ++d.nfld;
}

// A table added (taken over): its number for the paragraphs' tbl (its index + 1).
static int doc_table (Doc &d, Table *t)
{
	if (d.ntbl == d.tblcap)
	{
		int c = d.tblcap ? d.tblcap * 2 : 8;
		Table **n = new Table *[c];
		for (int i = 0; i < d.ntbl; i++) n[i] = d.tbl[i];
		delete[] d.tbl; d.tbl = n; d.tblcap = c;
	}
	d.tbl[d.ntbl++] = t;
	return d.ntbl;
}
static inline Table *para_table (const Doc &d, const Para *q) { return q->pf.tbl > 0 && q->pf.tbl <= d.ntbl ? d.tbl[q->pf.tbl - 1] : 0; }

// A style's character format.
static CharFmt style_fmt (Doc &d, int st)
{
	const Style &s = STYLES[st];
	CharFmt f; f.font = (short) doc_font (d, s.font); f.size = s.size; f.flags = s.flags; f.color = s.color; f.hilite = AUTO;
	return f;
}

static void doc_reserve (Doc &d, int n)
{
	if (n <= d.cap) return;
	int c = d.cap ? d.cap * 2 : 64; if (c < n) c = n;
	Para **t = new Para *[c];
	for (int i = 0; i < d.n; i++) t[i] = d.p[i];
	delete[] d.p; d.p = t; d.cap = c;
}
// Put paragraph q at index i (the ones from i shift down).
static void doc_put (Doc &d, int i, Para *q)
{
	doc_reserve (d, d.n + 1);
	for (int k = d.n; k > i; k--) d.p[k] = d.p[k - 1];
	d.p[i] = q; d.n++;
}
// Take paragraph i out (not freed).
static Para *doc_take (Doc &d, int i)
{
	Para *q = d.p[i];
	for (int k = i; k < d.n - 1; k++) d.p[k] = d.p[k + 1];
	d.n--;
	return q;
}

// A new paragraph of a style (its characters' format the style's).
static Para *para_styled (Doc &d, int st)
{
	Para *q = para_new ();
	q->pf = style_para (st);
	q->endCf = doc_fmt (d, style_fmt (d, st));
	return q;
}

// The story s becomes the one edited (d.p); a header or a footer made when it had no paragraph.
static void doc_story (Doc &d, int s)
{
	if (s == d.cur || s < 0 || s >= SY_COUNT) return;
	d.st[d.cur].p = d.p; d.st[d.cur].n = d.n; d.st[d.cur].cap = d.cap;
	d.p = d.st[s].p; d.n = d.st[s].n; d.cap = d.st[s].cap;
	d.st[s].p = 0; d.st[s].n = d.st[s].cap = 0;
	d.cur = s;
	if (d.n == 0) doc_put (d, 0, para_styled (d, s == SY_BODY ? ST_NORMAL : s == SY_HEADER || s == SY_HEADER1 ? ST_HEADER : ST_FOOTER));
}
// Paragraphs appended to a story (a file's reader: its header, its footer).
static void story_append (Doc &d, int s, Para *q)
{
	int cur = d.cur;
	if (s != cur) { d.st[cur].p = d.p; d.st[cur].n = d.n; d.st[cur].cap = d.cap; d.p = d.st[s].p; d.n = d.st[s].n; d.cap = d.st[s].cap; }
	doc_put (d, d.n, q);
	if (s != cur) { d.st[s].p = d.p; d.st[s].n = d.n; d.st[s].cap = d.cap; d.p = d.st[cur].p; d.n = d.st[cur].n; d.cap = d.st[cur].cap; d.st[cur].p = 0; d.st[cur].n = d.st[cur].cap = 0; }
}
// A story emptied (its paragraphs freed).
static void story_clear (Doc &d, int s)
{
	if (s == d.cur) { for (int i = 0; i < d.n; i++) para_free (d.p[i]); d.n = 0; return; }
	for (int i = 0; i < d.st[s].n; i++) para_free (d.st[s].p[i]);
	d.st[s].n = 0;
}

// An empty document: one Normal paragraph.
static void doc_new (Doc &d)
{
	doc_clear (d);
	doc_put (d, 0, para_styled (d, ST_NORMAL));
}

static inline Pos doc_end (const Doc &d) { return mkpos (d.n - 1, d.p[d.n - 1]->len); }
static inline Pos doc_clamp (const Doc &d, Pos a)
{
	a.p = wclamp (a.p, 0, d.n - 1);
	a.o = wclamp (a.o, 0, d.p[a.p]->len);
	return a;
}

// The format at a place: the character's before it (the one typed there takes it), else the one
// after it, else the paragraph mark's (an image's or a field's: its text's).
static unsigned short doc_cf_at (Doc &d, Pos a)
{
	const Para *q = d.p[a.p];
	unsigned short f = a.o > 0 && a.o <= q->len ? q->cf[a.o - 1] : q->len > 0 ? q->cf[0] : q->endCf;
	if (d.fmt[f].obj || d.fmt[f].fld)
	{
		CharFmt t = d.fmt[f]; t.obj = 0; t.ow = t.oh = 0; t.fld = 0;
		f = doc_fmt (d, t);
	}
	return f;
}

// ---- a table's paragraphs --------------------------------------------------------------------------------
// The run of paragraphs of paragraph p's table: [*a, *b).
static void table_span (const Doc &d, int p, int *a, int *b)
{
	int t = d.p[p]->pf.tbl;
	int i = p, j = p + 1;
	while (i > 0 && d.p[i - 1]->pf.tbl == t) i--;
	while (j < d.n && d.p[j]->pf.tbl == t) j++;
	*a = i; *b = j;
}
// The paragraphs of the cell holding paragraph p: [*a, *b).
static void cell_span (const Doc &d, int p, int *a, int *b)
{
	int i = p, j = p + 1;
	while (i > 0 && same_cell (d.p[i - 1], d.p[p])) i--;
	while (j < d.n && same_cell (d.p[j], d.p[p])) j++;
	*a = i; *b = j;
}
// The first paragraph of cell (r, c) of the table run [a, b), or -1.
static int cell_first (const Doc &d, int a, int b, int r, int c)
{
	for (int i = a; i < b; i++) if (d.p[i]->pf.row == r && d.p[i]->pf.col == c) return i;
	return -1;
}
static inline bool cell_start (const Doc &d, int p) { return p == 0 || !same_cell (d.p[p - 1], d.p[p]) || !in_table (d.p[p]); }

// ---- edits (and their undoing) ----------------------------------------------------------------------------
// begin: the paragraphs [first, first + count) are about to change (copied); end: they are now
// [first, first + nNew). kind: typing coalesces with the edit before it.
static void doc_begin (Doc &d, int first, int count, Pos caret, Pos anchor, int kind)
{
	if (d.edOld) { for (int i = 0; i < d.edN; i++) para_free (d.edOld[i]); delete[] d.edOld; }
	d.edFirst = first; d.edN = count; d.edKind = kind; d.edCaret = caret; d.edAnchor = anchor;
	d.edOld = new Para *[count > 0 ? count : 1];
	for (int i = 0; i < count; i++) d.edOld[i] = para_copy (d.p[first + i]);
}

static void doc_end_edit (Doc &d, int nNew, Pos caret, Pos anchor)
{
	d.changes++;
	for (int i = d.uptr; i < d.nundo; i++) undo_free (d.undo[i]);	// (the redo list is gone)
	d.nundo = d.uptr;
	// Typing: one edit with the one before it (the same single paragraph, just after it).
	if (d.nundo > 0 && (d.edKind == ED_TYPE || d.edKind == ED_DELETE || d.edKind == ED_BACKSPACE))
	{
		Undo &u = d.undo[d.nundo - 1];
		if (u.kind == d.edKind && u.story == d.cur && u.first == d.edFirst && u.nNew == 1 && d.edN == 1 && nNew == 1 && u.caret1 == d.edCaret)
		{
			u.caret1 = caret; u.anchor1 = anchor;
			for (int i = 0; i < d.edN; i++) para_free (d.edOld[i]);
			delete[] d.edOld; d.edOld = 0;
			return;
		}
	}
	if (d.nundo == MAXUNDO)					// (the oldest forgotten)
	{
		undo_free (d.undo[0]);
		for (int i = 1; i < d.nundo; i++) d.undo[i - 1] = d.undo[i];
		d.nundo--;
	}
	if (d.nundo == d.undocap)
	{
		int c = d.undocap ? d.undocap * 2 : 32;
		Undo *t = new Undo[c];
		for (int i = 0; i < d.nundo; i++) t[i] = d.undo[i];
		delete[] d.undo; d.undo = t; d.undocap = c;
	}
	Undo &u = d.undo[d.nundo++];
	u.story = d.cur;
	u.first = d.edFirst; u.nOld = d.edN; u.nNew = nNew; u.old = d.edOld; u.kind = d.edKind;
	u.caret0 = d.edCaret; u.anchor0 = d.edAnchor; u.caret1 = caret; u.anchor1 = anchor;
	d.edOld = 0;
	d.uptr = d.nundo;
}

// Break the typing coalescing (the caret moved).
static void doc_seal (Doc &d) { if (d.uptr > 0) d.undo[d.uptr - 1].kind = ED_OTHER; }

// Swap an edit's paragraphs with the current ones (undo, then redo, the same record) -- in its story.
static void undo_swap (Doc &d, Undo &u)
{
	doc_story (d, u.story);
	Para **cur = new Para *[u.nNew > 0 ? u.nNew : 1];
	for (int i = 0; i < u.nNew; i++) cur[i] = doc_take (d, u.first);
	for (int i = 0; i < u.nOld; i++) doc_put (d, u.first + i, u.old[i]);
	delete[] u.old;
	u.old = cur;
	int t = u.nOld; u.nOld = u.nNew; u.nNew = t;
	Pos c = u.caret0; u.caret0 = u.caret1; u.caret1 = c;
	Pos a = u.anchor0; u.anchor0 = u.anchor1; u.anchor1 = a;
	for (int i = 0; i < u.nNew; i++) d.p[u.first + i]->dirty = true;
	u.kind = ED_OTHER;
	d.changes++;
}
static bool doc_undo (Doc &d, Pos &caret, Pos &anchor)
{
	if (d.uptr == 0) return false;
	Undo &u = d.undo[--d.uptr];
	undo_swap (d, u);
	caret = u.caret1; anchor = u.anchor1;		// (swapped: the "before")
	return true;
}
static bool doc_redo (Doc &d, Pos &caret, Pos &anchor)
{
	if (d.uptr >= d.nundo) return false;
	Undo &u = d.undo[d.uptr++];
	undo_swap (d, u);
	caret = u.caret1; anchor = u.anchor1;
	return true;
}

// ---- text edits (no undo record: the caller brackets them) -------------------------------------------------
// Insert s[0..n) at a with format f ('\n': a new paragraph, the format of the one split -- in the same
// table cell); the place after it returned.
static Pos doc_insert_raw (Doc &d, Pos a, const unsigned *s, int n, unsigned short f)
{
	int i = 0;
	while (i < n)
	{
		int j = i;
		while (j < n && s[j] != '\n') j++;
		if (j > i) { para_insert (d.p[a.p], a.o, s + i, j - i, f); a.o += j - i; }
		if (j < n)					// a paragraph break
		{
			Para *q = d.p[a.p], *r = para_new (q->len - a.o + 4);
			for (int k = a.o; k < q->len; k++) { r->ch[k - a.o] = q->ch[k]; r->cf[k - a.o] = q->cf[k]; }
			r->len = q->len - a.o; r->pf = q->pf; r->pf.pageBreak = false; r->endCf = q->endCf;
			q->len = a.o; q->dirty = true;
			if (a.o > 0) q->endCf = q->cf[a.o - 1];
			doc_put (d, a.p + 1, r);
			a = mkpos (a.p + 1, 0);
			j++;
		}
		i = j;
	}
	return a;
}

// Delete [a, b) (a <= b): the paragraphs joined (the first one's format kept). Within one cell, or
// between two paragraphs outside any table (a table between them goes with the text).
static void doc_erase_raw (Doc &d, Pos a, Pos b)
{
	if (!(a < b)) return;
	if (a.p == b.p) { para_erase (d.p[a.p], a.o, b.o); return; }
	Para *q = d.p[a.p], *r = d.p[b.p];
	q->len = a.o;
	int tail = r->len - b.o;
	para_reserve (q, q->len + tail);
	for (int k = 0; k < tail; k++) { q->ch[q->len + k] = r->ch[b.o + k]; q->cf[q->len + k] = r->cf[b.o + k]; }
	q->len += tail; q->endCf = r->endCf; q->dirty = true;
	for (int k = b.p; k > a.p; k--) para_free (doc_take (d, k));
}

// Delete [a, b) keeping the tables whole: across cells (or from a table to the text beside it) the
// cells are emptied, never joined -- a cell keeps its first paragraph, the paragraph at a stays.
// The paragraphs outside the tables wholly in [a, b) go.
static void doc_erase_safe (Doc &d, Pos a, Pos b)
{
	if (!(a < b)) return;
	Para *qa = d.p[a.p], *qb = d.p[b.p];
	if (a.p == b.p || same_cell (qa, qb)) { doc_erase_raw (d, a, b); return; }
	if (!in_table (qa) && !in_table (qb)) { doc_erase_raw (d, a, b); return; }
	for (int k = b.p; k >= a.p; k--)
	{
		Para *q = d.p[k];
		int o0 = k == a.p ? a.o : 0, o1 = k == b.p ? b.o : q->len;
		para_erase (q, o0, o1);
		bool whole = o0 == 0 && o1 >= q->len + (o1 - o0);	// (all of it was in [a, b))
		if (k == a.p || k == b.p || !whole) continue;
		if (in_table (q) && cell_start (d, k)) continue;	// (a cell's first paragraph stays, empty)
		para_free (doc_take (d, k));
	}
}

// ---- a piece of a document (the clipboard's) -----------------------------------------------------------
// The text [a, b) as a document of its own (its formats, fonts, fields copied; the tables wholly in it
// too -- a part of a table is taken as plain paragraphs), and put back.
static void doc_extract (Doc &d, Pos a, Pos b, Doc &out)
{
	doc_clear (out);
	int *tmap = new int[d.ntbl + 1];			// d's table -> out's (0: flattened)
	for (int i = 0; i <= d.ntbl; i++) tmap[i] = -1;
	for (int p = a.p; p <= b.p; p++)
	{
		const Para *s = d.p[p];
		int o0 = p == a.p ? a.o : 0, o1 = p == b.p ? b.o : s->len;
		Para *q = para_new (o1 - o0 + 4);
		for (int k = o0; k < o1; k++)
		{
			CharFmt f = d.fmt[s->cf[k]];
			f.font = (short) doc_font (out, d.fontName[f.font]);
			if (f.obj) f.obj = doc_image_copy (out, d.img[f.obj - 1]) + 1;
			if (f.fld) f.fld = doc_field (out, d.fld[f.fld - 1].kind, d.fld[f.fld - 1].arg);
			q->ch[k - o0] = s->ch[k]; q->cf[k - o0] = doc_fmt (out, f);
		}
		q->len = o1 - o0; q->pf = s->pf;
		CharFmt e = d.fmt[p == b.p && b.o > 0 ? s->cf[b.o - 1] : s->endCf];
		e.font = (short) doc_font (out, d.fontName[e.font]);
		e.obj = 0; e.ow = e.oh = 0; e.fld = 0;
		q->endCf = doc_fmt (out, e);
		if (s->pf.tbl)
		{
			int t = s->pf.tbl;
			if (tmap[t] < 0)				// (whole in [a, b)? then kept a table)
			{
				int ta, tb; table_span (d, p, &ta, &tb);
				bool whole = ta >= a.p && tb - 1 <= b.p && !(ta == a.p && a.o > 0) && !(tb - 1 == b.p && b.o < d.p[b.p]->len);
				tmap[t] = whole ? doc_table (out, table_copy (d.tbl[t - 1])) : 0;
			}
			q->pf.tbl = (short) tmap[t];
			if (!tmap[t]) q->pf.row = q->pf.col = 0;
		}
		doc_put (out, out.n, q);
	}
	delete[] tmap;
}

// Insert a document's text at a: its first paragraph joins the text before a, its last one the text
// after it (in that paragraph's format); the paragraphs between come whole, its tables too -- in a
// table's cell, all of it goes in the cell (its tables as plain paragraphs). The place after it
// returned.
static Pos doc_insert_doc_raw (Doc &d, Pos a, const Doc &s)
{
	if (s.n == 0) return a;
	unsigned short *map = new unsigned short[s.nfmt > 0 ? s.nfmt : 1];
	for (int i = 0; i < s.nfmt; i++)
	{
		CharFmt f = s.fmt[i];
		f.font = (short) doc_font (d, s.fontName[f.font]);
		if (f.obj) f.obj = doc_image_copy (d, s.img[f.obj - 1]) + 1;
		if (f.fld) f.fld = doc_field (d, s.fld[f.fld - 1].kind, s.fld[f.fld - 1].arg);
		map[i] = doc_fmt (d, f);
	}
	Para *P = d.p[a.p];
	ParaFmt pf0 = P->pf;
	bool cell = in_table (P);
	int *tmap = new int[s.ntbl + 1];
	for (int i = 0; i <= s.ntbl; i++) tmap[i] = cell || i == 0 ? 0 : doc_table (d, table_copy (s.tbl[i - 1]));
	auto plain = [&] (const Para *q) { return cell || !q->pf.tbl; };
	auto put = [&] (Para *t, int o, const Para *q) {		// q's characters at offset o of t
		para_reserve (t, t->len + q->len);
		for (int k = t->len - 1; k >= o; k--) { t->ch[k + q->len] = t->ch[k]; t->cf[k + q->len] = t->cf[k]; }
		for (int k = 0; k < q->len; k++) { t->ch[o + k] = q->ch[k]; t->cf[o + k] = map[q->cf[k]]; }
		t->len += q->len; t->dirty = true;
	};
	auto inCell = [&] (ParaFmt &pf) { if (cell) { pf.tbl = pf0.tbl; pf.row = pf0.row; pf.col = pf0.col; } };
	Pos end;
	if (s.n == 1 && plain (s.p[0]))
	{
		put (P, a.o, s.p[0]);
		if (P->len == s.p[0]->len) P->endCf = map[s.p[0]->endCf];
		end = mkpos (a.p, a.o + s.p[0]->len);
	}
	else
	{
		Para *R = para_new (P->len - a.o + 4);			// (the text after a)
		for (int k = a.o; k < P->len; k++) { R->ch[k - a.o] = P->ch[k]; R->cf[k - a.o] = P->cf[k]; }
		R->len = P->len - a.o; R->pf = pf0; R->pf.pageBreak = false; R->endCf = P->endCf;
		bool empty = a.o == 0;
		P->len = a.o; P->dirty = true;
		if (a.o > 0) P->endCf = P->cf[a.o - 1];
		int at = a.p + 1;					// (R's index: the whole paragraphs go before it)
		doc_put (d, at, R);
		const Para *first = s.p[0], *last = s.p[s.n - 1];
		int k0 = 0, k1 = s.n;
		if (plain (first))
		{
			put (P, a.o, first);
			if (empty) { P->pf = first->pf; inCell (P->pf); P->endCf = map[first->endCf]; }
			k0 = 1;
		}
		else if (empty) { para_free (doc_take (d, a.p)); at--; }	// (a table first: in the place of an empty paragraph)
		bool joins = plain (last) && s.n - 1 >= k0;
		if (joins) { put (R, 0, last); k1 = s.n - 1; }
		for (int k = k0; k < k1; k++)
		{
			const Para *q = s.p[k];
			Para *t = para_new (q->len + 4);
			put (t, 0, q);
			t->pf = q->pf; t->endCf = map[q->endCf];
			if (cell) inCell (t->pf); else t->pf.tbl = (short) tmap[q->pf.tbl];
			if (!t->pf.tbl) t->pf.row = t->pf.col = 0;
			doc_put (d, at++, t);
		}
		end = mkpos (at, joins ? last->len : 0);
	}
	delete[] tmap;
	delete[] map;
	return end;
}

// ---- formats over a range ---------------------------------------------------------------------------------
// A change of character format: what it sets.
enum { CH_FONT = 1, CH_SIZE = 2, CH_FLAGS = 4, CH_COLOR = 8, CH_HILITE = 16, CH_GROW = 32, CH_OBJSIZE = 64 };
struct CfChange
{
	int what;
	int ow, oh;			// CH_OBJSIZE: the images' size (twips)
	short font, size;		// CH_FONT, CH_SIZE (half-points); CH_GROW: size += size (a step)
	unsigned short setFlags, clearFlags;	// CH_FLAGS
	unsigned color, hilite;
};
static const short SIZES[] = { 16, 18, 20, 22, 24, 28, 32, 36, 40, 44, 48, 52, 56, 72, 96, 144 };	// (half-points)
static short grow_size (short s, int dir)
{
	int n = (int) (sizeof SIZES / sizeof SIZES[0]);
	if (dir > 0) { for (int i = 0; i < n; i++) if (SIZES[i] > s) return SIZES[i]; return (short) wmin (s + 24, 3276); }
	for (int i = n - 1; i >= 0; i--) if (SIZES[i] < s) return SIZES[i];
	return (short) wmax (s - 2, 2);
}
static CharFmt cf_apply (const CharFmt &f0, const CfChange &c)
{
	CharFmt f = f0;
	if (c.what & CH_FONT) f.font = c.font;
	if (c.what & CH_SIZE) f.size = c.size;
	if (c.what & CH_GROW) f.size = grow_size (f.size, c.size);
	if (c.what & CH_FLAGS)
	{
		f.flags = (unsigned short) ((f.flags & ~c.clearFlags) | c.setFlags);
		if (c.setFlags & CF_SUPER) f.flags &= (unsigned short) ~CF_SUB;
		if (c.setFlags & CF_SUB) f.flags &= (unsigned short) ~CF_SUPER;
	}
	if (c.what & CH_COLOR) f.color = c.color;
	if (c.what & CH_HILITE) f.hilite = c.hilite;
	if ((c.what & CH_OBJSIZE) && f.obj) { f.ow = c.ow; f.oh = c.oh; }
	return f;
}
// Apply it to [a, b) (a < b), or to the paragraph mark when a == b at a paragraph's end.
static void doc_format_raw (Doc &d, Pos a, Pos b, const CfChange &c)
{
	unsigned short memo[2] = { 0xFFFF, 0 };			// (the last format mapped)
	for (int p = a.p; p <= b.p; p++)
	{
		Para *q = d.p[p];
		int o0 = p == a.p ? a.o : 0, o1 = p == b.p ? b.o : q->len;
		for (int k = o0; k < o1; k++)
		{
			if (q->cf[k] != memo[0]) { memo[0] = q->cf[k]; memo[1] = doc_fmt (d, cf_apply (d.fmt[q->cf[k]], c)); }
			q->cf[k] = memo[1];
		}
		if (p < b.p || o1 == q->len) q->endCf = doc_fmt (d, cf_apply (d.fmt[q->endCf], c));
		q->dirty = true;
	}
}

// A paragraph's style applied: its format's, its characters' font, size, weight and slant (and a
// colour of the former style's taken off); its list, page break, tabs and cell kept.
static void para_set_style (Doc &d, Para *q, int st)
{
	int old = q->pf.style;
	ParaFmt pf = style_para (st);
	pf.list = q->pf.list; pf.level = q->pf.level;
	if (q->pf.list) { pf.left = q->pf.left; pf.first = q->pf.first; }
	pf.pageBreak = q->pf.pageBreak;
	pf.ntab = q->pf.ntab; for (int i = 0; i < q->pf.ntab; i++) pf.tab[i] = q->pf.tab[i];
	pf.tbl = q->pf.tbl; pf.row = q->pf.row; pf.col = q->pf.col;
	if (st == old) { pf.align = q->pf.align; }
	q->pf = pf;
	CharFmt sf = style_fmt (d, st);
	unsigned oldColor = STYLES[old].color;
	unsigned short memo[2] = { 0xFFFF, 0 };
	for (int k = -1; k < q->len; k++)
	{
		unsigned short &cf = k < 0 ? q->endCf : q->cf[k];
		if (cf != memo[0])
		{
			memo[0] = cf;
			CharFmt f = d.fmt[cf];
			f.font = sf.font; f.size = sf.size;
			f.flags = (unsigned short) ((f.flags & ~(CF_BOLD | CF_ITALIC)) | sf.flags);
			if (sf.color != AUTO || f.color == oldColor) f.color = sf.color;
			memo[1] = doc_fmt (d, f);
		}
		cf = memo[1];
	}
	q->dirty = true;
}

// ---- fields' text -------------------------------------------------------------------------------------------
// The date and time of now (the app's clock; the tests' own).
static void (*g_now) (int *y, int *mo, int *d, int *h, int *mi, int *s);
static const char *const MONTH_NAMES[12] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };
static const char *const DAY_NAMES[7] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };

// A date / time as its picture says (Word's: d dd ddd dddd M MM MMM MMMM yy yyyy H HH h hh m mm s ss
// AM/PM; the rest as it is, '...' quoted).
static int date_text (const char *pic, int y, int mo, int d, int h, int mi, int s, unsigned *out, int cap)
{
	int n = 0;
	auto put = [&] (unsigned c) { if (n < cap) out[n++] = c; };
	auto puts = [&] (const char *t, int k = -1) { for (int i = 0; t[i] && (k < 0 || i < k); i++) put ((unsigned char) t[i]); };
	auto num = [&] (int v, int w) { char t[12]; int j = 0; do { t[j++] = (char) ('0' + v % 10); v /= 10; } while (v); while (j < w) t[j++] = '0'; while (j) put ((unsigned char) t[--j]); };
	static const int T[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
	int yy = mo < 3 ? y - 1 : y, dow = (yy + yy / 4 - yy / 100 + yy / 400 + T[(mo - 1 + 12) % 12] + d) % 7;
	for (const char *p = pic; *p; )
	{
		char c = *p; int k = 1;
		while (p[k] == c) k++;
		if (c == '\'') { p++; while (*p && *p != '\'') put ((unsigned char) *p++); if (*p) p++; continue; }
		if (c == 'd') { if (k == 1) num (d, 1); else if (k == 2) num (d, 2); else if (k == 3) puts (DAY_NAMES[dow], 3); else puts (DAY_NAMES[dow]); }
		else if (c == 'M') { if (k == 1) num (mo, 1); else if (k == 2) num (mo, 2); else if (k == 3) puts (MONTH_NAMES[(mo + 11) % 12], 3); else puts (MONTH_NAMES[(mo + 11) % 12]); }
		else if (c == 'y') { if (k <= 2) num (y % 100, 2); else num (y, 4); }
		else if (c == 'H') num (h, k >= 2 ? 2 : 1);
		else if (c == 'h') num (h % 12 ? h % 12 : 12, k >= 2 ? 2 : 1);
		else if (c == 'm') num (mi, k >= 2 ? 2 : 1);
		else if (c == 's') num (s, k >= 2 ? 2 : 1);
		else if ((c == 'A' || c == 'a') && (p[1] == 'M' || p[1] == 'm') && p[2] == '/') { puts (h < 12 ? "AM" : "PM"); p += 5; continue; }
		else { for (int i = 0; i < k; i++) put ((unsigned char) c); }
		p += k;
	}
	return n;
}

// A mail merge's values (the record shown, merge.h): a field's text by its name, or 0.
static const char *(*g_mergeValue) (const char *name);

// A field's text for page `page` (1-based) of `pages`: out, its length.
static int field_text (const Doc &d, int fld, int page, int pages, unsigned *out, int cap)
{
	if (fld < 1 || fld > d.nfld || cap <= 0) return 0;
	const Field &f = d.fld[fld - 1];
	int n = 0;
	auto num = [&] (int v) { char t[12]; int j = 0; if (v < 0) v = 0; do { t[j++] = (char) ('0' + v % 10); v /= 10; } while (v); while (j && n < cap) out[n++] = (unsigned char) t[--j]; };
	switch (f.kind)
	{
	case FK_PAGE: num (page); break;
	case FK_PAGES: num (pages); break;
	case FK_DATE: case FK_TIME:
	{
		int y = 2026, mo = 1, dd = 1, h = 0, mi = 0, s = 0;
		if (g_now) g_now (&y, &mo, &dd, &h, &mi, &s);
		n = date_text (f.arg[0] ? f.arg : f.kind == FK_DATE ? "dd/MM/yyyy" : "HH:mm", y, mo, dd, h, mi, s, out, cap);
		break;
	}
	case FK_MERGE:
	{
		const char *v = g_mergeValue ? g_mergeValue (f.arg) : 0;
		if (v) { for (const char *p = v; *p && n < cap; p++) out[n++] = (unsigned char) *p; }	// (Latin-1 / the .card's text)
		else { out[n++] = 0xAB; for (const char *p = f.arg; *p && n < cap - 1; p++) out[n++] = (unsigned char) *p; if (n < cap) out[n++] = 0xBB; }
		break;
	}
	}
	return n;
}

// ---- text ---------------------------------------------------------------------------------------------
static inline bool is_space (unsigned c) { return c == ' ' || c == '\t' || c == 0xA0 || c == 0x0B; }
static inline bool is_word (unsigned c)
{
	return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'
	    || (c >= 0xC0 && c != 0xD7 && c != 0xF7 && c < 0x2000) || c == '\'' || c == 0x2019;
}

// The words, characters (without / with spaces) and paragraphs of [a, b).
struct Counts { int words, chars, charsSp, paras; };
static Counts para_count (Para *const *pp, Pos a, Pos b)
{
	Counts c = { 0, 0, 0, 0 };
	for (int p = a.p; p <= b.p; p++)
	{
		const Para *q = pp[p];
		int o0 = p == a.p ? a.o : 0, o1 = p == b.p ? b.o : q->len;
		bool in = false, any = false;
		for (int k = o0; k < o1; k++)
		{
			unsigned ch = q->ch[k];
			if (ch == OBJ_CHAR || ch == FIELD_CHAR) { in = false; continue; }
			c.charsSp++;
			if (!is_space (ch)) { c.chars++; any = true; }
			bool w = !is_space (ch) && ch != '-' && ch != 0x2013 && ch != 0x2014;
			if (w && !in) c.words++;
			in = w;
		}
		if (any) c.paras++;
	}
	return c;
}
static Counts doc_count (const Doc &d, Pos a, Pos b) { return para_count (d.p, a, b); }

// UTF-8 / Latin-1 text -> code points (valid UTF-8 with a multi-byte character: UTF-8; else Latin-1).
static int decode_text (const char *s, int n, unsigned *out, int cap)
{
	bool utf8 = true, multi = false;
	for (int i = 0; i < n; )
	{
		unsigned char c = (unsigned char) s[i];
		int k = c < 0x80 ? 0 : (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : (c & 0xF8) == 0xF0 ? 3 : -1;
		if (k < 0 || i + k >= n + (k ? 0 : 1)) { utf8 = false; break; }
		for (int j = 1; j <= k; j++) if (((unsigned char) s[i + j] & 0xC0) != 0x80) { utf8 = false; break; }
		if (!utf8) break;
		if (k) multi = true;
		i += k + 1;
	}
	int m = 0;
	for (int i = 0; i < n && m < cap; )
	{
		unsigned c = (unsigned char) s[i];
		if (utf8 && multi && c >= 0x80)
		{
			int k = (c & 0xE0) == 0xC0 ? 1 : (c & 0xF0) == 0xE0 ? 2 : 3;
			c &= k == 1 ? 0x1F : k == 2 ? 0x0F : 0x07;
			for (int j = 1; j <= k && i + j < n; j++) c = c << 6 | ((unsigned char) s[i + j] & 0x3F);
			i += k + 1;
		}
		else i++;
		if (c == '\r') { if (i < n && s[i] == '\n') continue; c = '\n'; }
		out[m++] = c;
	}
	return m;
}
// Code points -> Latin-1 when they all fit, else UTF-8 (the system's text is Latin-1). Returns the
// length (out: at most cap - 1, NUL-terminated).
static int encode_text (const unsigned *s, int n, char *out, int cap)
{
	bool wide = false;
	for (int i = 0; i < n; i++) if (s[i] > 0xFF && s[i] != OBJ_CHAR && s[i] != FIELD_CHAR) { wide = true; break; }
	int m = 0;
	for (int i = 0; i < n; i++)
	{
		unsigned c = s[i];
		if (c == 0x0B) c = '\n';
		if (c == OBJ_CHAR || c == FIELD_CHAR) continue;	// (an image, a field: not text)
		if (!wide || c < 0x80) { if (m + 1 >= cap) break; out[m++] = (char) c; continue; }
		if (c < 0x800) { if (m + 2 >= cap) break; out[m++] = (char) (0xC0 | c >> 6); out[m++] = (char) (0x80 | (c & 0x3F)); }
		else if (c < 0x10000) { if (m + 3 >= cap) break; out[m++] = (char) (0xE0 | c >> 12); out[m++] = (char) (0x80 | (c >> 6 & 0x3F)); out[m++] = (char) (0x80 | (c & 0x3F)); }
		else { if (m + 4 >= cap) break; out[m++] = (char) (0xF0 | c >> 18); out[m++] = (char) (0x80 | (c >> 12 & 0x3F)); out[m++] = (char) (0x80 | (c >> 6 & 0x3F)); out[m++] = (char) (0x80 | (c & 0x3F)); }
	}
	if (cap > 0) out[m < cap ? m : cap - 1] = 0;
	return m;
}

// The plain text of [a, b) (paragraphs ended by '\n'; a field: its text; the cells of a table's row
// apart by tabs).
static int doc_text (const Doc &d, Pos a, Pos b, unsigned *out, int cap)
{
	int m = 0;
	for (int p = a.p; p <= b.p && m < cap; p++)
	{
		const Para *q = d.p[p];
		int o0 = p == a.p ? a.o : 0, o1 = p == b.p ? b.o : q->len;
		for (int k = o0; k < o1 && m < cap; k++)
		{
			if (q->ch[k] == FIELD_CHAR && d.fmt[q->cf[k]].fld) { unsigned t[80]; int tn = field_text (d, d.fmt[q->cf[k]].fld, 1, 1, t, 80); for (int j = 0; j < tn && m < cap; j++) out[m++] = t[j]; continue; }
			out[m++] = q->ch[k];
		}
		if (p < b.p && m < cap)
		{
			const Para *r = d.p[p + 1];
			out[m++] = in_table (q) && same_cell (q, r) ? '\n' : in_table (q) && in_table (r) && r->pf.tbl == q->pf.tbl && r->pf.row == q->pf.row ? '\t' : '\n';
		}
	}
	return m;
}
static int doc_text_len (const Doc &d, Pos a, Pos b)
{
	int m = 0;
	for (int p = a.p; p <= b.p; p++)
	{
		const Para *q = d.p[p];
		int o0 = p == a.p ? a.o : 0, o1 = p == b.p ? b.o : q->len;
		for (int k = o0; k < o1; k++) m += q->ch[k] == FIELD_CHAR ? 80 : 1;
		m += p < b.p;
	}
	return m;
}

} // namespace wr

#endif
