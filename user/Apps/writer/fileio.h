//
// fileio.h -- Writer's files: Rich Text Format read and written (the fonts, the colours, the styles
// -- "Normal", "heading 1", "toc 1"... --, bold / italic / underline / strike-through / superscript /
// subscript, the sizes, the highlights, the paragraphs' alignment, indents, spacing, tab stops, keep
// options, lists (Word's \listtext / \pntext), page breaks; the tables -- their rows, their cells'
// widths, merged cells, shading, lines, heading row --; the headers and footers (the first page's
// own), the fields -- the page, the number of pages, the date, the time, a mail merge's --, the page's
// size, margins and first number), plain text (UTF-8, or Latin-1: the system's own), and an HTML
// export. The .docx and .odt files: docx.h, odt.h (their tables built by TableBuild, here).
//
// Reading RTF: groups and their state, control words, \'hh (Windows-1252), \uN (and its \ucN
// fallback skipped), the destinations Writer has no use for skipped (\info, \*\...).
//
#ifndef _writer_fileio_h
#define _writer_fileio_h

#include "doc.h"
#include "img/imgload.hpp"
#include "img/pngsave.hpp"

namespace wr {

// ---- an output buffer ----------------------------------------------------------------------------------
struct Out
{
	char *b; int n, cap;
	void init () { cap = 1 << 16; b = new char[cap]; n = 0; }
	void grow (int k) { if (n + k <= cap) return; int c = cap * 2; while (c < n + k) c *= 2; char *t = new char[c]; for (int i = 0; i < n; i++) t[i] = b[i]; delete[] b; b = t; cap = c; }
	void put (char c) { grow (1); b[n++] = c; }
	void puts (const char *s) { while (*s) put (*s++); }
	void num (long v) { if (v < 0) { put ('-'); v = -v; } char t[24]; int j = 0; do { t[j++] = (char) ('0' + v % 10); v /= 10; } while (v); while (j) put (t[--j]); }
	void hex2 (unsigned v) { const char *h = "0123456789abcdef"; put (h[(v >> 4) & 15]); put (h[v & 15]); }
	void utf8 (unsigned c)
	{
		if (c < 0x80) put ((char) c);
		else if (c < 0x800) { put ((char) (0xC0 | c >> 6)); put ((char) (0x80 | (c & 0x3F))); }
		else if (c < 0x10000) { put ((char) (0xE0 | c >> 12)); put ((char) (0x80 | (c >> 6 & 0x3F))); put ((char) (0x80 | (c & 0x3F))); }
		else { put ((char) (0xF0 | c >> 18)); put ((char) (0x80 | (c >> 12 & 0x3F))); put ((char) (0x80 | (c >> 6 & 0x3F))); put ((char) (0x80 | (c & 0x3F))); }
	}
	void free () { delete[] b; b = 0; n = cap = 0; }
};

// Windows-1252's 0x80..0x9F -> Unicode.
static unsigned cp1252 (unsigned c)
{
	static const unsigned short T[32] = {
		0x20AC, 0x81, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x8D, 0x017D, 0x8F,
		0x90, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x9D, 0x017E, 0x0178 };
	return c >= 0x80 && c < 0xA0 ? T[c - 0x80] : c;
}

// An image's file bytes (its own PNG / JPEG, else a PNG made): *jpeg says which; delete[] when *made.
static const unsigned char *image_bytes (const Image &im, unsigned *len, bool *jpeg, bool *made)
{
	if (im.data && im.len) { *len = im.len; *jpeg = im.jpeg; *made = false; return im.data; }
	*jpeg = false; *made = true;
	return pngsave::png_encode (im.px, im.w, im.h, true, len);
}

// ---- the formats in use ------------------------------------------------------------------------------------
// Which character formats the document's text uses (every story's -- the tables keep what undone edits
// added), and the tables its paragraphs point at.
static bool *formats_used (const Doc &d, bool *tablesUsed = 0)
{
	bool *used = new bool[d.nfmt + 1];
	for (int i = 0; i <= d.nfmt; i++) used[i] = false;
	if (tablesUsed) for (int i = 0; i < d.ntbl; i++) tablesUsed[i] = false;
	for (int s = 0; s < SY_COUNT; s++)
	{
		int n; Para **p = story_p (d, s, &n);
		for (int i = 0; i < n; i++)
		{
			const Para *q = p[i];
			used[q->endCf] = true;
			for (int k = 0; k < q->len; k++) used[q->cf[k]] = true;
			if (tablesUsed && q->pf.tbl > 0 && q->pf.tbl <= d.ntbl) tablesUsed[q->pf.tbl - 1] = true;
		}
	}
	return used;
}

// ---- fields' instructions (RTF, Word) --------------------------------------------------------------------------
// "PAGE", "NUMPAGES", "DATE \@ "dd/MM/yyyy"", "MERGEFIELD Name": its kind (FK_NONE: not one of Writer's)
// and its argument.
static int field_parse (const char *s, char *arg, int cap)
{
	arg[0] = 0;
	while (*s == ' ' || *s == '\t') s++;
	char w[24]; int n = 0;
	while (*s && *s != ' ' && *s != '\\' && n < 23) { w[n++] = (char) (*s >= 'a' && *s <= 'z' ? *s - 32 : *s); s++; }
	w[n] = 0;
	auto is = [&] (const char *k) { int i = 0; while (k[i] && w[i] == k[i]) i++; return !k[i] && !w[i]; };
	auto word = [&] (const char *p) {				// (the next word, or "quoted words")
		while (*p == ' ') p++;
		int k = 0;
		if (*p == '"') { p++; while (*p && *p != '"' && k < cap - 1) arg[k++] = *p++; }
		else while (*p && *p != ' ' && *p != '\\' && k < cap - 1) arg[k++] = *p++;
		arg[k] = 0;
	};
	if (is ("PAGE")) return FK_PAGE;
	if (is ("NUMPAGES") || is ("SECTIONPAGES")) return FK_PAGES;
	if (is ("MERGEFIELD")) { word (s); return arg[0] ? FK_MERGE : FK_NONE; }
	bool date = is ("DATE") || is ("CREATEDATE") || is ("SAVEDATE") || is ("PRINTDATE"), time = is ("TIME");
	if (!date && !time) return FK_NONE;
	for (const char *p = s; *p; p++) if (p[0] == '\\' && p[1] == '@') { word (p + 2); break; }
	int k = 0;							// ("dd\ MM": a character not a picture's letter needs no \)
	for (int i = 0; arg[i]; i++)
	{
		char c = arg[i + 1];
		if (arg[i] == '\\' && c && !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '\'' || c == '\\')) continue;
		arg[k++] = arg[i];
	}
	arg[k] = 0;
	return date ? FK_DATE : FK_TIME;
}
// A field's instruction, as Word writes it.
static void field_instr (const Field &f, char *out, int cap)
{
	int n = 0;
	auto put = [&] (const char *t) { while (*t && n < cap - 1) out[n++] = *t++; };
	switch (f.kind)
	{
	case FK_PAGE: put ("PAGE"); break;
	case FK_PAGES: put ("NUMPAGES"); break;
	case FK_DATE: case FK_TIME:
		put (f.kind == FK_DATE ? "DATE" : "TIME");
		if (f.arg[0]) { put (" \\@ \""); put (f.arg); put ("\""); }
		break;
	case FK_MERGE:
	{
		bool sp = false; for (const char *t = f.arg; *t; t++) if (*t == ' ') sp = true;
		put ("MERGEFIELD "); if (sp) put ("\""); put (f.arg); if (sp) put ("\"");
		break;
	}
	}
	out[n] = 0;
}

// ---- a table read from a file ----------------------------------------------------------------------------
// Its rows as they come, each cell: its column in the grid, the columns it spans, a vertical merge's
// continuation (the cell above goes on) or the rows it spans, its shading, its paragraphs. finish ()
// makes the Table and puts the paragraphs in the story, cell by cell, row by row.
struct TableBuild
{
	struct Cell { int col, cs, rs; bool vcont; unsigned fill; Para **p; int np, cap; };
	struct Row { Cell *c; int n, cap; int height; };
	Row *rows; int nr, rcap;
	int ncols; int colW[MAXCOLS];
	unsigned char border, bw; unsigned bcolor; bool header; unsigned char align; int indent;

	void init () { rows = 0; nr = rcap = 0; ncols = 0; border = TB_ALL; bw = 4; bcolor = 0; header = false; align = AL_LEFT; indent = 0; }
	void row (int height = 0)
	{
		if (nr == rcap) { int c = rcap * 2 + 8; Row *t = new Row[c]; for (int i = 0; i < nr; i++) t[i] = rows[i]; delete[] rows; rows = t; rcap = c; }
		Row &r = rows[nr++]; r.c = 0; r.n = r.cap = 0; r.height = height;
	}
	Cell *cell (int col, int cs, bool vcont, int rs, unsigned fill)
	{
		if (!nr) row ();
		Row &r = rows[nr - 1];
		if (r.n == r.cap) { int c = r.cap * 2 + 8; Cell *t = new Cell[c]; for (int i = 0; i < r.n; i++) t[i] = r.c[i]; delete[] r.c; r.c = t; r.cap = c; }
		Cell &k = r.c[r.n++];
		k.col = col; k.cs = cs < 1 ? 1 : cs; k.rs = rs < 1 ? 1 : rs; k.vcont = vcont; k.fill = fill; k.p = 0; k.np = k.cap = 0;
		return &k;
	}
	void para (Para *q)				// (into the row's last cell)
	{
		if (!nr || !rows[nr - 1].n) cell (0, 1, false, 1, AUTO);
		Cell &k = rows[nr - 1].c[rows[nr - 1].n - 1];
		if (k.np == k.cap) { int c = k.cap * 2 + 4; Para **t = new Para *[c]; for (int i = 0; i < k.np; i++) t[i] = k.p[i]; delete[] k.p; k.p = t; k.cap = c; }
		k.p[k.np++] = q;
	}
	void widen (int endCol) { if (nr && rows[nr - 1].n) { Cell &k = rows[nr - 1].c[rows[nr - 1].n - 1]; if (endCol > k.col + k.cs) k.cs = endCol - k.col; } }
	static bool blank (const Cell &k) { return k.np == 0 || (k.np == 1 && k.p[0]->len == 0); }
	void finish (Doc &d, int story)
	{
		if (nr == 0 || ncols <= 0) { clear (); return; }
		ncols = wmin (ncols, (int) MAXCOLS);
		Table *t = table_alloc (nr, ncols);
		for (int c = 0; c < ncols; c++) t->colW[c] = wmax (colW[c], 60);
		t->border = border; t->bw = bw; t->bcolor = bcolor; t->header = header; t->align = align; t->indent = indent;
		for (int r = 0; r < nr; r++) t->rowH[r] = (short) wclamp (rows[r].height, 0, 30000);
		// each grid cell's paragraphs (an owner's)
		int N = nr * ncols;
		Cell **own = new Cell *[N];
		bool *taken = new bool[N];
		for (int i = 0; i < N; i++) { own[i] = 0; taken[i] = false; }
		for (int r = 0; r < nr; r++)
			for (int k = 0; k < rows[r].n; k++)
			{
				Cell &c = rows[r].c[k];
				int col = c.col;
				if (col < 0 || col >= ncols) { for (int i = 0; i < c.np; i++) para_free (c.p[i]); c.np = 0; continue; }
				int cs = wmin (c.cs, ncols - col);
				if (c.vcont && r > 0)				// (the cell above goes on)
				{
					int orow, ocol; cell_owner (t, r - 1, col, &orow, &ocol);
					TCell &o = tcell (t, orow, ocol);
					if (!o.covered && own[orow * ncols + ocol] && !taken[r * ncols + col])
					{
						o.rs = (unsigned char) wmax ((int) o.rs, r - orow + 1);
						for (int cc = ocol; cc < ocol + o.cs && cc < ncols; cc++) { tcell (t, r, cc).covered = true; taken[r * ncols + cc] = true; }
						Cell *oc = own[orow * ncols + ocol];
						if (!blank (c)) for (int i = 0; i < c.np; i++) push (*oc, c.p[i]);
						else for (int i = 0; i < c.np; i++) para_free (c.p[i]);
						c.np = 0;
						continue;
					}
				}
				if (taken[r * ncols + col]) { for (int i = 0; i < c.np; i++) para_free (c.p[i]); c.np = 0; continue; }
				TCell &tc = tcell (t, r, col);
				tc.cs = (unsigned char) cs; tc.rs = (unsigned char) wmin (c.rs, nr - r); tc.covered = false; tc.fill = c.fill;
				own[r * ncols + col] = &c;
				for (int rr = r; rr < r + tc.rs; rr++)
					for (int cc = col; cc < col + cs; cc++)
					{
						taken[rr * ncols + cc] = true;
						if (rr != r || cc != col) tcell (t, rr, cc).covered = true;
					}
			}
		int idx = doc_table (d, t);
		for (int r = 0; r < nr; r++)
			for (int c = 0; c < ncols; c++)
			{
				TCell &tc = tcell (t, r, c);
				if (tc.covered && taken[r * ncols + c]) continue;
				tc.covered = false;
				Cell *k = own[r * ncols + c];
				if (!k || k->np == 0)
				{
					Para *e = para_styled (d, ST_NORMAL); e->pf.after = 0; e->pf.line = 100;
					e->pf.tbl = (short) idx; e->pf.row = (short) r; e->pf.col = (short) c;
					story_append (d, story, e);
					continue;
				}
				for (int i = 0; i < k->np; i++)
				{
					Para *q = k->p[i];
					q->pf.tbl = (short) idx; q->pf.row = (short) r; q->pf.col = (short) c;
					q->pf.pageBreak = false;
					story_append (d, story, q);
				}
				k->np = 0;
			}
		delete[] own; delete[] taken;
		clear ();
	}
	static void push (Cell &k, Para *q)
	{
		if (k.np == k.cap) { int c = k.cap * 2 + 4; Para **t = new Para *[c]; for (int i = 0; i < k.np; i++) t[i] = k.p[i]; delete[] k.p; k.p = t; k.cap = c; }
		k.p[k.np++] = q;
	}
	void clear ()
	{
		for (int r = 0; r < nr; r++) { for (int k = 0; k < rows[r].n; k++) { Cell &c = rows[r].c[k]; for (int i = 0; i < c.np; i++) para_free (c.p[i]); delete[] c.p; } delete[] rows[r].c; }
		delete[] rows; rows = 0; nr = rcap = 0; ncols = 0;
	}
};

// After a file is read: a paragraph after a table ending the body; the body never empty.
static void doc_fix (Doc &d)
{
	int n; Para **p = story_p (d, SY_BODY, &n);
	if (n == 0 || in_table (p[n - 1])) story_append (d, SY_BODY, para_styled (d, ST_NORMAL));
}

// ---- RTF: reading --------------------------------------------------------------------------------------
enum { DS_TEXT, DS_SKIP, DS_FONTTBL, DS_COLORTBL, DS_STYLESHEET, DS_LISTTEXT, DS_FLDINST, DS_PICT, DS_DOCVAR };

struct RtfState
{
	CharFmt cf; int cfColor, cfHilite;	// (colour table indices, -1 none)
	ParaFmt pf;
	int dest, uc;
	int styleNo;				// \sN of a stylesheet entry being read
	int fontNo;				// \fN of a font table entry being read
	bool hidden;				// \v: not shown
	int story;				// where its paragraphs go (SY_*)
	bool intbl;				// \intbl: its paragraph in a table's cell
	int tabAlign, tabLead;			// the next \tx's alignment and leader
};

static bool rtf_is (const char *b, int n) { return n >= 5 && b[0] == '{' && b[1] == '\\' && b[2] == 'r' && b[3] == 't' && b[4] == 'f'; }

static int rtf_style_by_name (const char *n)
{
	static const char *const names[ST_COUNT] = { "normal", "heading 1", "heading 2", "heading 3", "title", "subtitle", "quote", "plain text",
						      "toc 1", "toc 2", "toc 3", "toc heading", "header", "footer" };
	for (int i = 0; i < ST_COUNT; i++) if (sicmp (n, names[i]) == 0) return i;
	if (sicmp (n, "block text") == 0 || sicmp (n, "intense quote") == 0) return ST_QUOTE;
	if (sicmp (n, "heading 4") == 0 || sicmp (n, "heading 5") == 0) return ST_H3;
	if (sicmp (n, "contents 1") == 0) return ST_TOC1;
	if (sicmp (n, "contents 2") == 0) return ST_TOC2;
	if (sicmp (n, "contents 3") == 0) return ST_TOC3;
	if (sicmp (n, "contents heading") == 0) return ST_TOCHEAD;
	return -1;
}

static bool rtf_load (Doc &d, const char *b, int n)
{
	doc_clear (d);
	enum { MAXF = 256, MAXC = 256, MAXS = 64, DEPTH = 64 };
	int *fontMap = new int[MAXF];			// RTF \fN -> the document's font
	unsigned *colors = new unsigned[MAXC]; int ncolors = 0;
	int styleMap[MAXS];				// \sN -> ST_*
	for (int i = 0; i < MAXF; i++) fontMap[i] = -1;
	for (int i = 0; i < MAXS; i++) styleMap[i] = i == 0 ? ST_NORMAL : -1;
	RtfState *st = new RtfState[DEPTH]; int sp = 0;
	CharFmt base; base.font = (short) doc_font (d, "Liberation Serif"); base.size = 24; base.flags = 0; base.color = AUTO; base.hilite = AUTO;
	RtfState &s0 = st[0];
	s0.cf = base; s0.cfColor = -1; s0.cfHilite = -1; s0.pf = style_para (ST_NORMAL); s0.pf.after = 0; s0.pf.line = 100;
	s0.dest = DS_TEXT; s0.uc = 1; s0.styleNo = -1; s0.fontNo = -1; s0.hidden = false; s0.story = SY_BODY; s0.intbl = false;
	s0.tabAlign = TA_LEFT; s0.tabLead = TL_NONE;
	int deff = 0;
	char name[64]; int nameLen = 0;			// a font / style name being read
	int red = 0, green = 0, blue = 0; bool anyColor = false;
	unsigned lastCf = 0xFFFF; CharFmt lastFmt = base;
	Para *qs[SY_COUNT];				// each story's paragraph being read
	for (int i = 0; i < SY_COUNT; i++) qs[i] = para_new ();
	char listBuf[16]; int listLen = 0; int listKind = LS_NONE, listLevel = 0;
	int skipChars = 0;				// (a \uN's fallback characters to skip)
	bool pageNext = false;				// a \page: the next paragraph starts a page
	PageSetup &pg = d.page;
	bool landscape = false;
	char inst[256]; int instLen = 0;		// a field's instruction
	char var[400]; int varLen = 0;			// a \docvar's name and value
	// a picture being read: its kind (1 PNG, 2 JPEG), sizes, bytes
	int pkind = 0, picw = 0, pich = 0, goalw = 0, goalh = 0, scalex = 100, scaley = 100;
	unsigned char *pbuf = 0; unsigned plen = 0, pcap = 0; int nib = -1;
	// a table being read: its row's cells' definitions (\cellx), the paragraphs of the row's cells,
	// the rows so far
	struct CellDef { int right; bool vmf, vmr, hmr; int fill; };
	CellDef def[MAXCOLS + 1]; int ndef = 0; CellDef pend = { 0, false, false, false, -1 };
	int rowLeft = 0, rowAlign = AL_LEFT, rowHeight = 0; bool rowHdr = false;
	int brdW = 0, brdColor = -1; bool anyBorder = false;
	struct RawCell { int x0, x1; bool vmr, hmr; int fill; Para **p; int np, cap; };
	struct RawRow { RawCell *c; int n; bool hdr; int align, left, height; };
	RawRow *rows = 0; int nrows = 0, rowcap = 0;
	RawCell cur[MAXCOLS + 1]; int ncur = 0;		// (the row's cells so far: their paragraphs)
	for (int i = 0; i <= MAXCOLS; i++) { cur[i].p = 0; cur[i].np = cur[i].cap = 0; }

	auto fmtIndex = [&] (const RtfState &s) -> unsigned short {
		CharFmt f = s.cf;
		f.color = s.cfColor >= 0 && s.cfColor < ncolors ? colors[s.cfColor] : AUTO;
		f.hilite = s.cfHilite > 0 && s.cfHilite < ncolors ? colors[s.cfHilite] : AUTO;
		if (f.color == 0x000000 && s.cfColor >= 0) f.color = AUTO;	// (black: the automatic one)
		if (lastCf != 0xFFFF && f.same (lastFmt)) return (unsigned short) lastCf;
		lastFmt = f; lastCf = doc_fmt (d, f);
		return (unsigned short) lastCf;
	};
	auto emit = [&] (unsigned c) {
		RtfState &s = st[sp];
		if (skipChars > 0) { skipChars--; return; }
		if (s.dest == DS_LISTTEXT) { if (listLen < 15) listBuf[listLen++] = c < 128 ? (char) c : '*'; return; }
		if (s.dest == DS_FLDINST) { if (instLen < 255) inst[instLen++] = c < 256 ? (char) c : '?'; return; }
		if (s.dest == DS_DOCVAR) { if (varLen < 399) var[varLen++] = c < 256 ? (char) c : '?'; return; }
		if (s.dest == DS_FONTTBL || s.dest == DS_STYLESHEET) { if (c == ';') { name[nameLen] = 0; if (s.dest == DS_FONTTBL && s.fontNo >= 0 && s.fontNo < MAXF) fontMap[s.fontNo] = doc_font (d, name); else if (s.dest == DS_STYLESHEET && s.styleNo >= 0 && s.styleNo < MAXS) { int k = rtf_style_by_name (name); if (k >= 0) styleMap[s.styleNo] = k; } nameLen = 0; } else if (nameLen < 63 && (c >= 32 || nameLen)) name[nameLen++] = c < 256 ? (char) c : '?'; return; }
		if (s.dest == DS_PICT)
		{
			int h = c >= '0' && c <= '9' ? (int) c - '0' : (c | 32) >= 'a' && (c | 32) <= 'f' ? (int) (c | 32) - 'a' + 10 : -1;
			if (h < 0) return;
			if (nib < 0) { nib = h; return; }
			if (plen == pcap) { unsigned nc = pcap ? pcap * 2 : 65536; unsigned char *t = new unsigned char[nc]; for (unsigned k = 0; k < plen; k++) t[k] = pbuf[k]; delete[] pbuf; pbuf = t; pcap = nc; }
			pbuf[plen++] = (unsigned char) (nib << 4 | h); nib = -1;
			return;
		}
		if (s.dest == DS_COLORTBL) { if (c == ';') { if (ncolors < MAXC) colors[ncolors++] = anyColor ? (unsigned) (red << 16 | green << 8 | blue) : 0x000000; red = green = blue = 0; anyColor = false; } return; }
		if (s.dest != DS_TEXT || s.hidden) return;
		unsigned short f = fmtIndex (s);
		para_insert (qs[s.story], qs[s.story]->len, &c, 1, f);
	};
	// The table read so far put in the body.
	auto flushTable = [&] () {
		if (nrows == 0) return;
		int edges[2 * MAXCOLS + 4]; int ne = 0;
		auto addEdge = [&] (int x) { for (int i = 0; i < ne; i++) if (edges[i] == x) return; if (ne < 2 * MAXCOLS + 2) edges[ne++] = x; };
		for (int r = 0; r < nrows; r++) for (int k = 0; k < rows[r].n; k++) { addEdge (rows[r].c[k].x0); addEdge (rows[r].c[k].x1); }
		for (int i = 1; i < ne; i++) for (int j = i; j > 0 && edges[j - 1] > edges[j]; j--) { int t = edges[j]; edges[j] = edges[j - 1]; edges[j - 1] = t; }
		// (edges closer than 1/20 of a point: one)
		int m = 0; for (int i = 0; i < ne; i++) if (m == 0 || edges[i] - edges[m - 1] > 1) edges[m++] = edges[i]; ne = m;
		auto colOf = [&] (int x) { int best = 0; for (int i = 1; i < ne; i++) if ((x - edges[i] < 0 ? edges[i] - x : x - edges[i]) < (x - edges[best] < 0 ? edges[best] - x : x - edges[best])) best = i; return best; };
		TableBuild tb; tb.init ();
		tb.ncols = wmin (ne - 1, (int) MAXCOLS);
		for (int c = 0; c < tb.ncols; c++) tb.colW[c] = edges[c + 1] - edges[c];
		tb.indent = ne ? edges[0] : 0; tb.header = rows[0].hdr; tb.align = (unsigned char) rows[0].align;
		tb.border = anyBorder ? TB_ALL : TB_NONE;
		tb.bw = (unsigned char) wclamp (brdW > 0 ? brdW * 2 / 5 : 4, 1, 48);
		tb.bcolor = brdColor > 0 && brdColor < ncolors ? colors[brdColor] : 0;
		for (int r = 0; r < nrows; r++)
		{
			tb.row (rows[r].height);
			for (int k = 0; k < rows[r].n; k++)
			{
				RawCell &c = rows[r].c[k];
				int c0 = colOf (c.x0), c1 = colOf (c.x1);
				if (c1 <= c0) c1 = c0 + 1;
				if (c.hmr && k > 0)				// (joined to the one before)
				{
					tb.widen (c1);
					for (int i = 0; i < c.np; i++) { if (c.p[i]->len) tb.para (c.p[i]); else para_free (c.p[i]); }
				}
				else
				{
					unsigned fill = c.fill > 0 && c.fill < ncolors ? colors[c.fill] : AUTO;
					tb.cell (c0, c1 - c0, c.vmr, 1, fill);
					for (int i = 0; i < c.np; i++) tb.para (c.p[i]);
				}
				delete[] c.p;
			}
			delete[] rows[r].c;
		}
		nrows = 0;
		tb.finish (d, SY_BODY);
		anyBorder = false; brdW = 0; brdColor = -1;
	};
	auto newPara = [&] (int story) { Para *q = para_new (); qs[story] = q; };
	// A paragraph ends: into its story, or the table's cell being read (cellEnd: \cell, the next one follows).
	auto endPara = [&] (bool last, bool cellEnd) {
		RtfState &s = st[sp];
		Para *q = qs[s.story];
		q->pf = s.pf;
		if (listKind != LS_NONE && q->pf.list == LS_NONE) { q->pf.list = (unsigned char) listKind; q->pf.level = (unsigned char) listLevel; }
		if (q->pf.list != LS_NONE && q->pf.first >= 0) { if (q->pf.left < 360) q->pf.left = 720; q->pf.first = -360; }
		q->endCf = fmtIndex (s);
		listKind = LS_NONE; listLevel = 0;
		if (s.story == SY_BODY && (s.intbl || cellEnd))
		{
			RawCell &c = cur[ncur < MAXCOLS ? ncur : MAXCOLS];
			if (c.np == c.cap) { int k = c.cap * 2 + 4; Para **t = new Para *[k]; for (int i = 0; i < c.np; i++) t[i] = c.p[i]; delete[] c.p; c.p = t; c.cap = k; }
			c.p[c.np++] = q;
			if (cellEnd && ncur < MAXCOLS) ncur++;
			newPara (s.story);
			return;
		}
		if (s.story == SY_BODY) flushTable ();
		if (pageNext && s.story == SY_BODY) { q->pf.pageBreak = true; pageNext = false; }
		int sn; story_p (d, s.story, &sn);
		if (!last || q->len > 0 || sn == 0) { story_append (d, s.story, q); newPara (s.story); }
	};
	// A row ends: its cells (their widths from the definitions) kept.
	auto endRow = [&] () {
		if (qs[SY_BODY]->len > 0) endPara (false, true);		// (a cell's text without \cell)
		int nc = wmax (ncur, 1);
		if (nrows == rowcap) { int c = rowcap * 2 + 8; RawRow *t = new RawRow[c]; for (int i = 0; i < nrows; i++) t[i] = rows[i]; delete[] rows; rows = t; rowcap = c; }
		RawRow &r = rows[nrows++];
		r.c = new RawCell[nc]; r.n = nc; r.hdr = rowHdr; r.align = rowAlign; r.left = rowLeft; r.height = rowHeight;
		int x = rowLeft;
		for (int k = 0; k < nc; k++)
		{
			RawCell &c = r.c[k];
			c = cur[k];
			int right = k < ndef ? def[k].right : x + 1440;
			c.x0 = x; c.x1 = right > x ? right : x + 60; x = c.x1;
			c.vmr = k < ndef && def[k].vmr; c.hmr = k < ndef && def[k].hmr; c.fill = k < ndef ? def[k].fill : -1;
			cur[k].p = 0; cur[k].np = cur[k].cap = 0;
		}
		for (int k = nc; k <= MAXCOLS; k++) { for (int i = 0; i < cur[k].np; i++) para_free (cur[k].p[i]); delete[] cur[k].p; cur[k].p = 0; cur[k].np = cur[k].cap = 0; }
		ncur = 0;
	};

	int i = 0;
	while (i < n)
	{
		char c = b[i];
		if (c == '{')
		{
			if (sp + 1 < DEPTH) { st[sp + 1] = st[sp]; sp++; }
			if (st[sp].dest == DS_DOCVAR && varLen < 399) var[varLen++] = '\x01';
			i++;
			continue;
		}
		if (c == '}')
		{
			RtfState &s = st[sp];
			if (s.dest == DS_LISTTEXT && (sp == 0 || st[sp - 1].dest != DS_LISTTEXT))
			{
				bool digits = false;
				for (int k = 0; k < listLen; k++) if ((listBuf[k] >= '0' && listBuf[k] <= '9') || ((listBuf[k] | 32) >= 'a' && (listBuf[k] | 32) <= 'z')) digits = true;
				listKind = digits ? LS_NUMBER : LS_BULLET;
				listLen = 0;
			}
			if (s.dest == DS_PICT && (sp == 0 || st[sp - 1].dest != DS_PICT))
			{
				ImgFrames im;
				int k = sp - 1;					// (the text it stands in: under any \\*\\shppict)
				while (k >= 0 && st[k].dest != DS_TEXT) k--;
				if (pkind && plen && k >= 0 && !st[k].hidden && img_load_mem (pbuf, plen, &im))
				{
					for (int f2 = 1; f2 < im.n; f2++) delete[] im.px[f2];
					unsigned char *data = new unsigned char[plen];
					for (unsigned j = 0; j < plen; j++) data[j] = pbuf[j];
					int idx = doc_image (d, im.px[0], im.w, im.h, data, plen, pkind == 2);
					unsigned short fi = fmtIndex (st[k]); CharFmt cfm = d.fmt[fi];
					cfm.obj = idx + 1;
					cfm.ow = (goalw > 0 ? goalw : (picw > 0 ? picw : im.w) * 15) * scalex / 100;
					cfm.oh = (goalh > 0 ? goalh : (pich > 0 ? pich : im.h) * 15) * scaley / 100;
					if (cfm.ow <= 0 || cfm.oh <= 0) { cfm.ow = im.w * 15; cfm.oh = im.h * 15; }
					unsigned oc = OBJ_CHAR;
					para_insert (qs[st[k].story], qs[st[k].story]->len, &oc, 1, doc_fmt (d, cfm));
					lastCf = 0xFFFF;
				}
				delete[] pbuf; pbuf = 0; plen = pcap = 0; nib = -1; pkind = 0;
			}
			if (s.dest == DS_DOCVAR && (sp == 0 || st[sp - 1].dest != DS_DOCVAR))
			{
				var[varLen] = 0;
				const char *parts[4] = { 0, 0, 0, 0 }; int np = 0;
				for (int k = 0; k < varLen && np < 4; k++) if (var[k] == '\x01') { var[k] = 0; parts[np++] = var + k + 1; }
				if (np >= 2 && sicmp (parts[0], "OnyxMergeSource") == 0) scpy (d.mergeSrc, parts[1], sizeof d.mergeSrc);
				varLen = 0;
			}
			if (sp > 0 && st[sp - 1].story != s.story && qs[s.story]->len > 0)	// (a header's last paragraph)
			{ endPara (true, false); }
			if (sp > 0) sp--;
			lastCf = 0xFFFF;
			i++;
			continue;
		}
		if (c == '\r' || c == '\n') { i++; continue; }
		if (c != '\\') { emit ((unsigned char) c); i++; continue; }
		// a control symbol / word
		i++;
		if (i >= n) break;
		c = b[i];
		if (c == '\'' && i + 2 < n)
		{
			auto hv = [] (char h) { return h >= '0' && h <= '9' ? h - '0' : (h | 32) >= 'a' && (h | 32) <= 'f' ? (h | 32) - 'a' + 10 : 0; };
			emit (cp1252 ((unsigned) (hv (b[i + 1]) * 16 + hv (b[i + 2]))));
			i += 3;
			continue;
		}
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')))
		{
			i++;
			switch (c)
			{
			case '\\': case '{': case '}': emit ((unsigned char) c); break;
			case '~': emit (0xA0); break;
			case '_': emit (0x2011); break;
			case '*': st[sp].dest = DS_SKIP; break;
			case '\n': case '\r': if (st[sp].dest == DS_TEXT) endPara (false, false); break;
			case '\t': emit ('\t'); break;
			}
			continue;
		}
		char w[32]; int wl = 0;
		while (i < n && ((b[i] >= 'a' && b[i] <= 'z') || (b[i] >= 'A' && b[i] <= 'Z'))) { if (wl < 31) w[wl++] = b[i]; i++; }
		w[wl] = 0;
		long v = 0; bool hasV = false, neg = false;
		if (i < n && b[i] == '-') { neg = true; i++; }
		while (i < n && b[i] >= '0' && b[i] <= '9') { v = v * 10 + (b[i] - '0'); hasV = true; i++; }
		if (neg) v = -v;
		if (i < n && b[i] == ' ') i++;
		RtfState &s = st[sp];
		auto is = [&] (const char *k) { int j = 0; while (k[j] && w[j] == k[j]) j++; return k[j] == 0 && w[j] == 0; };
		int on = hasV ? v != 0 : 1;
		// destinations
		if (is ("pict")) { s.dest = DS_PICT; pkind = 0; picw = pich = goalw = goalh = 0; scalex = scaley = 100; plen = 0; nib = -1; continue; }
		if (s.dest == DS_PICT)
		{
			if (is ("pngblip")) pkind = 1; else if (is ("jpegblip")) pkind = 2;
			else if (is ("picw")) picw = (int) v; else if (is ("pich")) pich = (int) v;
			else if (is ("picwgoal")) goalw = (int) v; else if (is ("pichgoal")) goalh = (int) v;
			else if (is ("picscalex")) scalex = (int) wclamp (v, 1L, 1000L); else if (is ("picscaley")) scaley = (int) wclamp (v, 1L, 1000L);
			else if (is ("bin")) i += (int) wclamp (v, 0L, (long) (n - i));	// (binary bytes: not kept)
			continue;
		}
		if (is ("fonttbl")) { s.dest = DS_FONTTBL; continue; }
		if (is ("colortbl")) { s.dest = DS_COLORTBL; continue; }
		if (is ("stylesheet")) { s.dest = DS_STYLESHEET; continue; }
		if (is ("listtext") || is ("pntext")) { s.dest = DS_LISTTEXT; listLen = 0; continue; }
		if (is ("docvar")) { s.dest = DS_DOCVAR; varLen = 0; continue; }
		if (is ("header") || is ("headerr") || is ("footer") || is ("footerr") || is ("headerf") || is ("footerf"))
		{
			int story = w[0] == 'h' ? (w[6] == 'f' ? SY_HEADER1 : SY_HEADER) : (w[6] == 'f' ? SY_FOOTER1 : SY_FOOTER);
			int sn; story_p (d, story, &sn);
			if (sn > 0 || qs[story]->len > 0) { s.dest = DS_SKIP; continue; }	// (a later section's: the first one's kept)
			s.dest = DS_TEXT; s.story = story; s.intbl = false; s.pf = style_para (ST_NORMAL); s.pf.after = 0; s.pf.line = 100;
			continue;
		}
		if (is ("field")) { instLen = 0; continue; }
		if (is ("fldinst")) { s.dest = DS_FLDINST; instLen = 0; continue; }
		if (is ("fldrslt"))
		{
			inst[instLen] = 0;
			char arg[64]; int kind = field_parse (inst, arg, sizeof arg);
			instLen = 0;
			if (kind != FK_NONE && s.dest == DS_TEXT && !s.hidden)		// (one of Writer's: its result not kept)
			{
				unsigned short fi = fmtIndex (s); CharFmt f = d.fmt[fi];
				f.fld = doc_field (d, kind, arg);
				unsigned fc = FIELD_CHAR;
				para_insert (qs[s.story], qs[s.story]->len, &fc, 1, doc_fmt (d, f));
				s.dest = DS_SKIP;
			}
			continue;
		}
		if (is ("info") || is ("headerl") || is ("footerl") || is ("object")
		    || is ("footnote") || is ("annotation") || is ("xe") || is ("tc") || is ("bkmkstart") || is ("bkmkend") || is ("nonshppict")
		    || is ("themedata") || is ("colorschememapping") || is ("latentstyles") || is ("datastore") || is ("listtable")
		    || is ("listoverridetable") || is ("rsidtbl") || is ("generator") || is ("mmathPr") || is ("xmlnstbl") || is ("pgdsctbl")
		    || is ("nonesttables") || is ("nesttableprops") || is ("formfield") || is ("datafield") || is ("fldtype"))
		{ s.dest = DS_SKIP; continue; }
		if (s.dest == DS_SKIP || s.dest == DS_FLDINST || s.dest == DS_DOCVAR) continue;
		if (s.dest == DS_COLORTBL)
		{
			if (is ("red")) { red = (int) v; anyColor = true; } else if (is ("green")) { green = (int) v; anyColor = true; } else if (is ("blue")) { blue = (int) v; anyColor = true; }
			continue;
		}
		if (s.dest == DS_FONTTBL) { if (is ("f")) { s.fontNo = (int) v; nameLen = 0; } continue; }
		if (s.dest == DS_STYLESHEET) { if (is ("s")) { s.styleNo = (int) v; nameLen = 0; } continue; }
		if (s.dest == DS_LISTTEXT) continue;
		// the document
		if (is ("deff")) deff = (int) v;
		else if (is ("paperw")) pg.w = (int) v;
		else if (is ("paperh")) pg.h = (int) v;
		else if (is ("margl")) pg.left = (int) v;
		else if (is ("margr")) pg.right = (int) v;
		else if (is ("margt")) pg.top = (int) v;
		else if (is ("margb")) pg.bottom = (int) v;
		else if (is ("headery")) pg.hdr = (int) v;
		else if (is ("footery")) pg.ftr = (int) v;
		else if (is ("titlepg")) pg.titlePg = true;
		else if (is ("pgnstarts")) pg.start = (int) wclamp (v, 0L, 9999L);
		else if (is ("landscape")) landscape = true;
		else if (is ("uc")) s.uc = (int) v;
		else if (is ("u")) { long u = v < 0 ? v + 65536 : v; emit ((unsigned) u); skipChars = s.uc; }
		// characters
		else if (is ("par") || is ("sect")) endPara (false, false);
		else if (is ("line")) emit (0x0B);
		else if (is ("tab")) emit ('\t');
		else if (is ("page")) { if (qs[s.story]->len > 0) endPara (false, false); pageNext = true; }
		else if (is ("emdash")) emit (0x2014);
		else if (is ("endash")) emit (0x2013);
		else if (is ("bullet")) emit (0x2022);
		else if (is ("lquote")) emit (0x2018);
		else if (is ("rquote")) emit (0x2019);
		else if (is ("ldblquote")) emit (0x201C);
		else if (is ("rdblquote")) emit (0x201D);
		else if (is ("emspace") || is ("enspace")) emit (' ');
		else if (is ("chpgn") && s.dest == DS_TEXT)		// (a page number, the old way)
		{
			unsigned short fi = fmtIndex (s); CharFmt f = d.fmt[fi]; f.fld = doc_field (d, FK_PAGE, "");
			unsigned fc = FIELD_CHAR; para_insert (qs[s.story], qs[s.story]->len, &fc, 1, doc_fmt (d, f));
		}
		// tables (a nested one: its cells as tabs, its rows as line breaks)
		else if (is ("cell")) { if (s.story == SY_BODY) endPara (false, true); else emit ('\t'); }
		else if (is ("nestcell")) emit ('\t');
		else if (is ("nestrow")) emit (0x0B);
		else if (is ("row")) { if (s.story == SY_BODY) endRow (); else endPara (false, false); }
		else if (is ("intbl")) s.intbl = true;
		else if (is ("trowd")) { ndef = 0; pend.right = 0; pend.vmf = pend.vmr = pend.hmr = false; pend.fill = -1; rowLeft = 0; rowAlign = AL_LEFT; rowHdr = false; rowHeight = 0; }
		else if (is ("trleft")) rowLeft = (int) v;
		else if (is ("trqc")) rowAlign = AL_CENTER;
		else if (is ("trqr")) rowAlign = AL_RIGHT;
		else if (is ("trql")) rowAlign = AL_LEFT;
		else if (is ("trhdr")) rowHdr = true;
		else if (is ("trrh")) rowHeight = (int) (v < 0 ? -v : v);
		else if (is ("clvmgf")) pend.vmf = true;
		else if (is ("clvmrg")) pend.vmr = true;
		else if (is ("clmrg")) pend.hmr = true;
		else if (is ("clcbpat")) pend.fill = (int) v;
		else if (is ("clbrdrt") || is ("clbrdrl") || is ("clbrdrb") || is ("clbrdrr")) anyBorder = true;
		else if (is ("brdrw")) { if (!brdW) brdW = (int) v; }
		else if (is ("brdrcf")) { if (brdColor < 0) brdColor = (int) v; }
		else if (is ("brdrnone") || is ("brdrnil")) {}
		else if (is ("cellx")) { if (ndef < MAXCOLS) { pend.right = (int) v; def[ndef++] = pend; } pend.vmf = pend.vmr = pend.hmr = false; pend.fill = -1; }
		// character formats
		else if (is ("plain")) { s.cf = base; s.cf.font = (short) (fontMap[deff & (MAXF - 1)] >= 0 ? fontMap[deff & (MAXF - 1)] : base.font); s.cfColor = -1; s.cfHilite = -1; s.hidden = false; }
		else if (is ("b")) s.cf.flags = (unsigned short) (on ? s.cf.flags | CF_BOLD : s.cf.flags & ~CF_BOLD);
		else if (is ("i")) s.cf.flags = (unsigned short) (on ? s.cf.flags | CF_ITALIC : s.cf.flags & ~CF_ITALIC);
		else if (is ("ul") || is ("uld") || is ("uldb") || is ("ulw") || is ("ulth") || is ("uldash")) s.cf.flags = (unsigned short) (on ? s.cf.flags | CF_UNDER : s.cf.flags & ~CF_UNDER);
		else if (is ("ulnone")) s.cf.flags &= (unsigned short) ~CF_UNDER;
		else if (is ("strike") || is ("striked")) s.cf.flags = (unsigned short) (on ? s.cf.flags | CF_STRIKE : s.cf.flags & ~CF_STRIKE);
		else if (is ("super")) s.cf.flags = (unsigned short) ((s.cf.flags & ~CF_SUB) | CF_SUPER);
		else if (is ("sub")) s.cf.flags = (unsigned short) ((s.cf.flags & ~CF_SUPER) | CF_SUB);
		else if (is ("nosupersub")) s.cf.flags &= (unsigned short) ~(CF_SUPER | CF_SUB);
		else if (is ("up")) s.cf.flags = (unsigned short) (v ? (s.cf.flags & ~CF_SUB) | CF_SUPER : s.cf.flags & ~CF_SUPER);
		else if (is ("dn")) s.cf.flags = (unsigned short) (v ? (s.cf.flags & ~CF_SUPER) | CF_SUB : s.cf.flags & ~CF_SUB);
		else if (is ("fs")) { if (v > 0) s.cf.size = (short) wclamp ((int) v, 2, 3276); }
		else if (is ("f")) { int f = (int) v & (MAXF - 1); if (fontMap[f] >= 0) s.cf.font = (short) fontMap[f]; }
		else if (is ("cf")) s.cfColor = (int) v;
		else if (is ("highlight") || is ("cb") || is ("chcbpat")) s.cfHilite = (int) v;
		else if (is ("v")) s.hidden = on != 0;
		// paragraph formats
		else if (is ("pard"))
		{
			s.pf = style_para (ST_NORMAL); s.pf.before = 0; s.pf.after = 0; s.pf.line = 100; s.pf.keepNext = false;
			s.intbl = false; s.tabAlign = TA_LEFT; s.tabLead = TL_NONE;
		}
		else if (is ("s"))
		{
			int k = v >= 0 && v < MAXS ? styleMap[v] : -1;
			if (k < 0) k = ST_NORMAL;
			ParaFmt keep = s.pf;
			s.pf = style_para (k);
			s.pf.ntab = keep.ntab; for (int t = 0; t < keep.ntab; t++) s.pf.tab[t] = keep.tab[t];
			if (k == ST_NORMAL) { s.pf.before = keep.before; s.pf.after = keep.after; s.pf.line = keep.line; }
		}
		else if (is ("ql")) s.pf.align = AL_LEFT;
		else if (is ("qc")) s.pf.align = AL_CENTER;
		else if (is ("qr")) s.pf.align = AL_RIGHT;
		else if (is ("qj")) s.pf.align = AL_JUSTIFY;
		else if (is ("li")) s.pf.left = (short) wclamp ((int) v, 0, 30000);
		else if (is ("ri")) s.pf.right = (short) wclamp ((int) v, 0, 30000);
		else if (is ("fi")) s.pf.first = (short) wclamp ((int) v, -30000, 30000);
		else if (is ("sb")) s.pf.before = (short) wclamp ((int) v, 0, 30000);
		else if (is ("sa")) s.pf.after = (short) wclamp ((int) v, 0, 30000);
		else if (is ("sl")) { if (v > 0) s.pf.line = (short) wclamp ((int) (v * 100 / 240), 50, 400); else s.pf.line = 100; }
		else if (is ("pagebb")) s.pf.pageBreak = on != 0;
		else if (is ("keepn")) s.pf.keepNext = on != 0;
		else if (is ("keep")) s.pf.keepLines = on != 0;
		else if (is ("widctlpar")) s.pf.widow = true;
		else if (is ("nowidctlpar")) s.pf.widow = false;
		else if (is ("tqr")) s.tabAlign = TA_RIGHT;
		else if (is ("tqc")) s.tabAlign = TA_CENTER;
		else if (is ("tqdec")) s.tabAlign = TA_DECIMAL;
		else if (is ("tldot") || is ("tlmdot")) s.tabLead = TL_DOT;
		else if (is ("tlhyph")) s.tabLead = TL_DASH;
		else if (is ("tlul") || is ("tlth") || is ("tleq")) s.tabLead = TL_LINE;
		else if (is ("tx")) { if (v > 0) pf_add_tab (s.pf, (int) wclamp (v, 1L, 30000L), s.tabAlign, s.tabLead); s.tabAlign = TA_LEFT; s.tabLead = TL_NONE; }
		else if (is ("tb")) { s.tabAlign = TA_LEFT; s.tabLead = TL_NONE; }
		else if (is ("ilvl")) listLevel = (int) wclamp (v, 0L, 5L);
		else if (is ("pnlvlblt")) s.pf.list = LS_BULLET;
		else if (is ("pnlvlbody") || is ("pndec")) s.pf.list = s.pf.list == LS_BULLET ? LS_BULLET : LS_NUMBER;
		else if (is ("pnlvl")) { s.pf.list = LS_NUMBER; s.pf.level = (unsigned char) wclamp ((int) v - 1, 0, 5); }
	}
	if (ncur > 0 || cur[0].np > 0) endRow ();
	if (qs[SY_BODY]->len > 0) { flushTable (); story_append (d, SY_BODY, qs[SY_BODY]); qs[SY_BODY] = 0; }
	flushTable ();
	for (int k = 0; k < SY_COUNT; k++) if (qs[k]) para_free (qs[k]);
	for (int k = 0; k <= MAXCOLS; k++) { for (int j = 0; j < cur[k].np; j++) para_free (cur[k].p[j]); delete[] cur[k].p; }
	delete[] rows;
	int bn; story_p (d, SY_BODY, &bn);
	if (bn == 0) story_append (d, SY_BODY, para_styled (d, ST_NORMAL));
	doc_fix (d);
	if (landscape && pg.w < pg.h) { int t = pg.w; pg.w = pg.h; pg.h = t; }
	pg.w = wclamp (pg.w, 2880, 40000); pg.h = wclamp (pg.h, 2880, 40000);
	pg.left = wclamp (pg.left, 0, pg.w / 3); pg.right = wclamp (pg.right, 0, pg.w / 3);
	pg.top = wclamp (pg.top, 0, pg.h / 3); pg.bottom = wclamp (pg.bottom, 0, pg.h / 3);
	pg.hdr = wclamp (pg.hdr, 0, pg.h / 3); pg.ftr = wclamp (pg.ftr, 0, pg.h / 3);
	delete[] fontMap; delete[] colors; delete[] st; delete[] pbuf;
	return true;
}

// ---- RTF: writing ----------------------------------------------------------------------------------------
static void rtf_text (Out &o, unsigned c)
{
	if (c == '\\' || c == '{' || c == '}') { o.put ('\\'); o.put ((char) c); }
	else if (c == '\t') o.puts ("\\tab ");
	else if (c == 0x0B) o.puts ("\\line ");
	else if (c == 0xA0) o.puts ("\\~");
	else if (c < 0x80) o.put ((char) c);
	else if (c >= 0xA0 && c < 0x100) { o.puts ("\\'"); o.hex2 (c); }
	else { o.puts ("\\u"); o.num (c < 0x8000 ? (long) c : (long) c - 65536); o.put ('?'); }
}

// (the writer's tables: the fonts' and colours' numbers)
struct RtfOut
{
	Out &o; Doc &d;
	int *fmap; unsigned cols[256]; int ncol;
	int counter[8];
	RtfOut (Out &o_, Doc &d_) : o (o_), d (d_), fmap (0), ncol (0) { for (int i = 0; i < 8; i++) counter[i] = 0; }
	int color (unsigned c)
	{
		if (c == AUTO) return 0;
		for (int i = 0; i < ncol; i++) if (cols[i] == c) return i + 1;
		if (ncol < 255) { cols[ncol++] = c; return ncol; }
		return 0;
	}
	void runFmt (const CharFmt &f)
	{
		o.puts ("\\f"); o.num (fmap[f.font]); o.puts ("\\fs"); o.num (f.size);
		if (f.flags & CF_BOLD) o.puts ("\\b");
		if (f.flags & CF_ITALIC) o.puts ("\\i");
		if (f.flags & CF_UNDER) o.puts ("\\ul");
		if (f.flags & CF_STRIKE) o.puts ("\\strike");
		if (f.flags & CF_SUPER) o.puts ("\\super");
		if (f.flags & CF_SUB) o.puts ("\\sub");
		if (f.color != AUTO) { o.puts ("\\cf"); o.num (color (f.color)); }
		if (f.hilite != AUTO) { o.puts ("\\highlight"); o.num (color (f.hilite)); }
		o.put (' ');
	}
	// A paragraph: its format, its runs, its end (\par, or \cell ending a cell).
	void para (const Para *q, bool intbl, bool cellEnd)
	{
		const ParaFmt &pf = q->pf;
		o.puts ("\\pard\\plain");
		if (intbl) o.puts ("\\intbl");
		o.puts ("\\s"); o.num (pf.style);
		static const char *const al[4] = { "\\ql", "\\qc", "\\qr", "\\qj" };
		o.puts (al[pf.align & 3]);
		if (pf.left) { o.puts ("\\li"); o.num (pf.left); }
		if (pf.right) { o.puts ("\\ri"); o.num (pf.right); }
		if (pf.first) { o.puts ("\\fi"); o.num (pf.first); }
		if (pf.before) { o.puts ("\\sb"); o.num (pf.before); }
		if (pf.after) { o.puts ("\\sa"); o.num (pf.after); }
		if (pf.line != 100) { o.puts ("\\sl"); o.num (pf.line * 240 / 100); o.puts ("\\slmult1"); }
		if (pf.pageBreak && !intbl) o.puts ("\\pagebb");
		if (pf.keepNext) o.puts ("\\keepn");
		if (pf.keepLines) o.puts ("\\keep");
		o.puts (pf.widow ? "\\widctlpar" : "\\nowidctlpar");
		for (int t = 0; t < pf.ntab; t++)
		{
			static const char *const ta[4] = { "", "\\tqc", "\\tqr", "\\tqdec" };
			static const char *const tl[4] = { "", "\\tldot", "\\tlhyph", "\\tlul" };
			o.puts (ta[pf.tab[t].align & 3]); o.puts (tl[pf.tab[t].leader & 3]); o.puts ("\\tx"); o.num (pf.tab[t].pos);
		}
		const CharFmt &f0 = d.fmt[q->len ? q->cf[0] : q->endCf];
		if (pf.list != LS_NONE)
		{
			int lv = pf.level & 7;
			if (pf.list == LS_NUMBER) { counter[lv]++; for (int k = lv + 1; k < 8; k++) counter[k] = 0; }
			o.puts ("{\\listtext\\f"); o.num (fmap[f0.font]); o.puts ("\\fs"); o.num (f0.size); o.put (' ');
			if (pf.list == LS_BULLET) o.puts ("\\'b7");
			else { o.num (counter[lv]); o.put ('.'); }
			o.puts ("\\tab}");
			o.puts (pf.list == LS_BULLET ? "{\\*\\pn\\pnlvlblt\\pnf0{\\pntxtb\\'b7}}" : "{\\*\\pn\\pnlvlbody\\pndec{\\pntxta .}}");
			if (lv) { o.puts ("\\ilvl"); o.num (lv); }
		}
		else for (int k = 0; k < 8; k++) counter[k] = 0;
		for (int i = 0; i < q->len; )
		{
			int j = i; while (j < q->len && q->cf[j] == q->cf[i]) j++;
			const CharFmt &f = d.fmt[q->cf[i]];
			if (f.fld && q->ch[i] == FIELD_CHAR)			// a field: its instruction, its text of now
			{
				for (int k = i; k < j; k++)
				{
					char in[96]; field_instr (d.fld[f.fld - 1], in, sizeof in);
					o.put ('{'); runFmt (f);				// (the field in its format's group: its result's)
					o.puts ("{\\field{\\*\\fldinst {");
					for (const char *t = in; *t; t++) rtf_text (o, (unsigned char) *t);
					o.puts (" }}{\\fldrslt {"); runFmt (f);
					unsigned t[80]; int tn = field_text (d, f.fld, 1, 1, t, 80);
					for (int m = 0; m < tn; m++) rtf_text (o, t[m]);
					o.puts ("}}}}");
				}
				i = j;
				continue;
			}
			o.put ('{'); runFmt (f);
			for (int k = i; k < j; k++)
			{
				if (q->ch[k] != OBJ_CHAR || !f.obj) { rtf_text (o, q->ch[k]); continue; }
				const Image &im = d.img[f.obj - 1];
				unsigned len; bool jpeg, made;
				const unsigned char *bb = image_bytes (im, &len, &jpeg, &made);
				o.puts ("{\\pict"); o.puts (jpeg ? "\\jpegblip" : "\\pngblip");
				o.puts ("\\picw"); o.num (im.w); o.puts ("\\pich"); o.num (im.h);
				o.puts ("\\picwgoal"); o.num (f.ow); o.puts ("\\pichgoal"); o.num (f.oh); o.put ('\n');
				for (unsigned x = 0; x < len; x++) { o.hex2 (bb[x]); if ((x & 63) == 63) o.put ('\n'); }
				o.puts ("}");
				if (made) delete[] bb;
			}
			o.put ('}');
			i = j;
		}
		const CharFmt &e = d.fmt[q->endCf];
		o.puts ("{\\f"); o.num (fmap[e.font]); o.puts ("\\fs"); o.num (e.size); o.puts (cellEnd ? "\\cell}\n" : "\\par}\n");
	}
	// A table's rows (its paragraphs [a, b)).
	void table (Para *const *p, int a, int b)
	{
		const Table *t = para_table (d, p[a]);
		int nc = t->ncols;
		int bw = wclamp (t->bw * 5 / 2, 1, 75);
		for (int r = 0; r < t->nrows; r++)
		{
			o.puts ("\\trowd\\trgaph"); o.num (CELL_PAD_X); o.puts ("\\trleft"); o.num (t->indent);
			if (t->align == AL_CENTER) o.puts ("\\trqc"); else if (t->align == AL_RIGHT) o.puts ("\\trqr");
			if (r == 0 && t->header) o.puts ("\\trhdr");
			if (t->rowH[r]) { o.puts ("\\trrh"); o.num (t->rowH[r]); }
			int x = t->indent;
			for (int c = 0; c < nc; )
			{
				int orow, ocol; cell_owner (t, r, c, &orow, &ocol);
				const TCell &k = tcell (t, orow, ocol);
				if (orow == r && ocol < c) { c++; continue; }	// (inside a cell of this row: its width is the cell's)
				bool cont = orow < r;
				int cs = cont ? k.cs - (c - ocol) : k.cs;
				if (cs < 1) cs = 1;
				if (!cont && k.rs > 1) o.puts ("\\clvmgf");
				if (cont) o.puts ("\\clvmrg");
				bool top = false, bot = false, lft = false, rgt = false;
				switch (t->border)
				{
				case TB_ALL: top = bot = lft = rgt = true; break;
				case TB_OUTER: top = orow == 0; bot = orow + k.rs >= t->nrows; lft = c == 0; rgt = c + cs >= nc; break;
				case TB_ROWS: top = bot = true; break;
				}
				auto brd = [&] (const char *side, bool on) {
					if (!on) return;
					o.puts ("\\clbrdr"); o.puts (side); o.puts ("\\brdrs\\brdrw"); o.num (bw);
					if (t->bcolor != AUTO && t->bcolor) { o.puts ("\\brdrcf"); o.num (color (t->bcolor)); }
				};
				brd ("t", top); brd ("l", lft); brd ("b", bot); brd ("r", rgt);
				if (k.fill != AUTO) { o.puts ("\\clcbpat"); o.num (color (k.fill)); }
				for (int cc = c; cc < c + cs && cc < nc; cc++) x += t->colW[cc];
				o.puts ("\\cellx"); o.num (x);
				c += cs;
			}
			o.put ('\n');
			// the cells' paragraphs, in the same order
			for (int c = 0; c < nc; )
			{
				int orow, ocol; cell_owner (t, r, c, &orow, &ocol);
				const TCell &k = tcell (t, orow, ocol);
				if (orow == r && ocol < c) { c++; continue; }
				int cs = orow < r ? k.cs - (c - ocol) : k.cs;
				if (cs < 1) cs = 1;
				if (orow < r) o.puts ("\\pard\\plain\\intbl\\cell\n");
				else
				{
					int first = -1, last = -1;
					for (int i = a; i < b; i++) if (p[i]->pf.row == r && p[i]->pf.col == c) { if (first < 0) first = i; last = i; }
					if (first < 0) o.puts ("\\pard\\plain\\intbl\\cell\n");
					else for (int i = first; i <= last; i++) para (p[i], true, i == last);
				}
				c += cs;
			}
			o.puts ("\\row\n");
		}
	}
	// A story's paragraphs (the body's tables as tables; a header's as its text).
	void story (Para *const *p, int n, bool body)
	{
		bool toc = false;
		for (int i = 0; i < n; )
		{
			if (body && in_table (p[i]) && para_table (d, p[i]))
			{
				int j = i + 1; while (j < n && p[j]->pf.tbl == p[i]->pf.tbl) j++;
				table (p, i, j);
				i = j;
				continue;
			}
			bool isToc = body && style_toc (p[i]->pf.style);
			if (isToc && !toc) { o.puts ("{\\field{\\*\\fldinst {TOC \\\\o \"1-3\" }}{\\fldrslt\n"); toc = true; }
			para (p[i], false, false);
			if (toc && (i + 1 >= n || !style_toc (p[i + 1]->pf.style))) { o.puts ("}}\n"); toc = false; }
			i++;
		}
	}
};

static int rtf_save (Doc &d, Out &o)
{
	o.init ();
	RtfOut w (o, d);
	bool *tused = new bool[d.ntbl + 1];
	bool *used = formats_used (d, tused);
	int *fmap = new int[d.nfont + 1], nf = 0;
	for (int i = 0; i < d.nfont; i++) fmap[i] = -1;
	for (int i = 0; i < d.nfmt; i++) if (used[i]) { w.color (d.fmt[i].color); w.color (d.fmt[i].hilite); if (fmap[d.fmt[i].font] < 0) fmap[d.fmt[i].font] = nf++; }
	for (int i = 0; i < d.ntbl; i++)
		if (tused[i])
		{
			const Table *t = d.tbl[i];
			if (t->bcolor != AUTO && t->bcolor) w.color (t->bcolor);
			for (int k = 0; k < t->nrows * t->ncols; k++) w.color (t->cell[k].fill);
		}
	w.fmap = fmap;
	o.puts ("{\\rtf1\\ansi\\ansicpg1252\\deff0\\uc1\n{\\fonttbl");
	for (int r = 0; r < nf; r++)
	{
		int i = 0; while (fmap[i] != r) i++;
		o.puts ("{\\f"); o.num (r);
		const char *nm = d.fontName[i];
		bool mono = false, sans = false;
		for (int k = 0; nm[k]; k++) { if (lower (nm[k]) == 'm' && lower (nm[k + 1]) == 'o' && lower (nm[k + 2]) == 'n') mono = true; if (lower (nm[k]) == 's' && lower (nm[k + 1]) == 'a' && lower (nm[k + 2]) == 'n') sans = true; }
		o.puts (mono ? "\\fmodern" : sans ? "\\fswiss" : "\\froman");
		o.puts ("\\fcharset0 ");
		for (int k = 0; nm[k]; k++) rtf_text (o, (unsigned char) nm[k]);
		o.puts (";}");
	}
	o.puts ("}\n{\\colortbl;");
	for (int i = 0; i < w.ncol; i++) { o.puts ("\\red"); o.num (w.cols[i] >> 16 & 255); o.puts ("\\green"); o.num (w.cols[i] >> 8 & 255); o.puts ("\\blue"); o.num (w.cols[i] & 255); o.put (';'); }
	o.puts ("}\n{\\stylesheet");
	static const char *const snames[ST_COUNT] = { "Normal", "heading 1", "heading 2", "heading 3", "Title", "Subtitle", "Quote", "Plain Text",
						       "toc 1", "toc 2", "toc 3", "TOC Heading", "header", "footer" };
	for (int i = 0; i < ST_COUNT; i++) { o.puts ("{\\s"); o.num (i); o.put (' '); o.puts (snames[i]); o.puts (";}"); }
	o.puts ("}\n{\\*\\generator Onyx Writer;}\n");
	if (d.mergeSrc[0])
	{
		o.puts ("{\\*\\docvar {OnyxMergeSource}{");
		for (const char *t = d.mergeSrc; *t; t++) rtf_text (o, (unsigned char) *t);
		o.puts ("}}\n");
	}
	const PageSetup &pg = d.page;
	o.puts ("\\paperw"); o.num (pg.w); o.puts ("\\paperh"); o.num (pg.h);
	o.puts ("\\margl"); o.num (pg.left); o.puts ("\\margr"); o.num (pg.right);
	o.puts ("\\margt"); o.num (pg.top); o.puts ("\\margb"); o.num (pg.bottom);
	if (pg.w > pg.h) o.puts ("\\landscape");
	o.puts ("\\widowctrl\\viewkind1\n\\sectd\\headery"); o.num (pg.hdr); o.puts ("\\footery"); o.num (pg.ftr);
	if (pg.titlePg) o.puts ("\\titlepg");
	if (pg.start != 1) { o.puts ("\\pgnstarts"); o.num (pg.start); o.puts ("\\pgnrestart"); }
	o.put ('\n');
	static const char *const hname[SY_COUNT] = { "", "header", "footer", "headerf", "footerf" };
	for (int s = SY_HEADER; s < SY_COUNT; s++)
	{
		if (story_empty (d, s)) continue;
		int n; Para **p = story_p (d, s, &n);
		o.puts ("{\\"); o.puts (hname[s]); o.put ('\n');
		w.story (p, n, false);
		o.puts ("}\n");
	}
	int n; Para **p = story_p (d, SY_BODY, &n);
	w.story (p, n, true);
	o.puts ("}\n");
	delete[] used; delete[] tused; delete[] fmap;
	return o.n;
}

// ---- plain text ------------------------------------------------------------------------------------------
static void txt_load (Doc &d, const char *b, int n)
{
	doc_new (d);
	unsigned *u = new unsigned[n + 1];
	int m = decode_text (b, n, u, n + 1);
	int k = 0;
	for (int i = 0; i < m; i++) if (u[i] >= 32 || u[i] == '\n' || u[i] == '\t' || u[i] == 0x0C) u[k++] = u[i] == 0x0C ? '\n' : u[i];
	if (k > 0 && u[k - 1] == '\n') k--;			// (the last line's end: no empty paragraph after it)
	unsigned short cf = d.p[0]->endCf;
	doc_insert_raw (d, mkpos (0, 0), u, k, cf);
	delete[] u;
	for (int i = 0; i < d.n; i++) d.p[i]->pf.after = 0, d.p[i]->pf.line = 100;
}
static int txt_save (Doc &d, Out &o)
{
	o.init ();
	int n; Para **p = story_p (d, SY_BODY, &n);
	Doc v = d; v.p = p; v.n = n;				// (the body, whatever story is edited)
	int total = doc_text_len (v, mkpos (0, 0), mkpos (n - 1, p[n - 1]->len));
	unsigned *u = new unsigned[total + 1];
	int m = doc_text (v, mkpos (0, 0), mkpos (n - 1, p[n - 1]->len), u, total);
	u[m++] = '\n';
	o.grow (m * 4 + 1);
	o.n = encode_text (u, m, o.b, o.cap);
	delete[] u;
	return o.n;
}

// ---- HTML export ------------------------------------------------------------------------------------------
static void html_text (Out &o, unsigned c)
{
	if (c == '<') o.puts ("&lt;"); else if (c == '>') o.puts ("&gt;"); else if (c == '&') o.puts ("&amp;");
	else if (c == 0xA0) o.puts ("&nbsp;"); else if (c == 0x0B) o.puts ("<br>"); else if (c == '\t') o.puts ("&emsp;");
	else o.utf8 (c);
}
static void html_color (Out &o, unsigned c) { o.put ('#'); o.hex2 (c >> 16 & 255); o.hex2 (c >> 8 & 255); o.hex2 (c & 255); }

static void html_para (Out &o, Doc &d, const Para *q, int &openList, bool inCell)
{
	static const char *const tag[ST_COUNT] = { "p", "h1", "h2", "h3", "h1", "h2", "blockquote", "pre", "p", "p", "p", "h2", "p", "p" };
	const ParaFmt &pf = q->pf;
	int list = inCell ? LS_NONE : pf.list;
	if (list != openList)
	{
		if (openList != LS_NONE) o.puts (openList == LS_BULLET ? "</ul>\n" : "</ol>\n");
		if (list != LS_NONE) o.puts (list == LS_BULLET ? "<ul>\n" : "<ol>\n");
		openList = list;
	}
	const char *t = list != LS_NONE ? "li" : tag[pf.style];
	o.put ('<'); o.puts (t);
	static const char *const al[4] = { "left", "center", "right", "justify" };
	bool style = pf.align != STYLES[pf.style].align || pf.left != STYLES[pf.style].left || pf.first || pf.pageBreak;
	if (style && list == LS_NONE)
	{
		o.puts (" style=\"text-align:"); o.puts (al[pf.align & 3]);
		if (pf.left) { o.puts ("; margin-left:"); o.num (pf.left / 20); o.puts ("pt"); }
		if (pf.first) { o.puts ("; text-indent:"); o.num (pf.first / 20); o.puts ("pt"); }
		if (pf.pageBreak) o.puts ("; page-break-before:always");
		o.put ('"');
	}
	o.put ('>');
	CharFmt sf = style_fmt (d, pf.style);
	for (int i = 0; i < q->len; )
	{
		int j = i; while (j < q->len && q->cf[j] == q->cf[i]) j++;
		const CharFmt &f = d.fmt[q->cf[i]];
		bool span = f.font != sf.font || f.size != sf.size || f.color != AUTO || f.hilite != AUTO;
		bool b = (f.flags & CF_BOLD) && !(sf.flags & CF_BOLD), it = (f.flags & CF_ITALIC) && !(sf.flags & CF_ITALIC);
		if (span)
		{
			o.puts ("<span style=\"");
			if (f.font != sf.font) { o.puts ("font-family:'"); o.puts (d.fontName[f.font]); o.puts ("'; "); }
			if (f.size != sf.size) { o.puts ("font-size:"); o.num (f.size / 2); if (f.size & 1) o.puts (".5"); o.puts ("pt; "); }
			if (f.color != AUTO) { o.puts ("color:"); html_color (o, f.color); o.puts ("; "); }
			if (f.hilite != AUTO) { o.puts ("background:"); html_color (o, f.hilite); o.puts ("; "); }
			o.puts ("\">");
		}
		if (b) o.puts ("<b>");
		if (it) o.puts ("<i>");
		if (f.flags & CF_UNDER) o.puts ("<u>");
		if (f.flags & CF_STRIKE) o.puts ("<s>");
		if (f.flags & CF_SUPER) o.puts ("<sup>");
		if (f.flags & CF_SUB) o.puts ("<sub>");
		for (int k = i; k < j; k++)
		{
			if (q->ch[k] == FIELD_CHAR && f.fld) { unsigned tt[80]; int tn = field_text (d, f.fld, 1, 1, tt, 80); for (int m = 0; m < tn; m++) html_text (o, tt[m]); continue; }
			if (q->ch[k] != OBJ_CHAR || !f.obj) { html_text (o, q->ch[k]); continue; }
			const Image &im = d.img[f.obj - 1];
			unsigned len; bool jpeg, made;
			const unsigned char *bb = image_bytes (im, &len, &jpeg, &made);
			o.puts ("<img style=\"width:"); o.num (f.ow / 20); o.puts ("pt; height:"); o.num (f.oh / 20);
			o.puts ("pt; vertical-align:baseline\" alt=\"\" src=\"data:image/"); o.puts (jpeg ? "jpeg" : "png"); o.puts (";base64,");
			static const char *B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
			for (unsigned x = 0; x < len; x += 3)
			{
				unsigned v = (unsigned) bb[x] << 16 | (x + 1 < len ? (unsigned) bb[x + 1] << 8 : 0) | (x + 2 < len ? bb[x + 2] : 0);
				o.put (B64[v >> 18 & 63]); o.put (B64[v >> 12 & 63]);
				o.put (x + 1 < len ? B64[v >> 6 & 63] : '='); o.put (x + 2 < len ? B64[v & 63] : '=');
			}
			o.puts ("\">");
			if (made) delete[] bb;
		}
		if (f.flags & CF_SUB) o.puts ("</sub>");
		if (f.flags & CF_SUPER) o.puts ("</sup>");
		if (f.flags & CF_STRIKE) o.puts ("</s>");
		if (f.flags & CF_UNDER) o.puts ("</u>");
		if (it) o.puts ("</i>");
		if (b) o.puts ("</b>");
		if (span) o.puts ("</span>");
		i = j;
	}
	if (q->len == 0 && list == LS_NONE) o.puts ("&nbsp;");
	o.puts ("</"); o.puts (t); o.puts (">\n");
}

static int html_save (Doc &d, Out &o, const char *title)
{
	o.init ();
	o.puts ("<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"utf-8\">\n<title>");
	for (const char *t = title; *t; t++) html_text (o, (unsigned char) *t);
	o.puts ("</title>\n<style>\nbody { max-width: 46em; margin: 2em auto; padding: 0 1em; font-family: 'Liberation Serif', 'Times New Roman', serif; font-size: 12pt; line-height: 1.3; }\n"
		"p, li { margin: 0 0 0.5em 0; } h1, h2, h3 { font-family: 'Liberation Sans', Arial, sans-serif; }\n"
		"blockquote { font-style: italic; color: #404040; } pre { font-family: 'DejaVu Sans Mono', monospace; }\n"
		"table { border-collapse: collapse; margin: 0.5em 0; } td { padding: 2pt 5pt; vertical-align: top; } td p { margin: 0; }\n</style>\n</head>\n<body>\n");
	int openList = LS_NONE;
	int n; Para **p = story_p (d, SY_BODY, &n);
	for (int i = 0; i < n; )
	{
		const Table *t = in_table (p[i]) ? para_table (d, p[i]) : 0;
		if (!t) { html_para (o, d, p[i], openList, false); i++; continue; }
		if (openList != LS_NONE) { o.puts (openList == LS_BULLET ? "</ul>\n" : "</ol>\n"); openList = LS_NONE; }
		int j = i + 1; while (j < n && p[j]->pf.tbl == p[i]->pf.tbl) j++;
		o.puts ("<table>\n");
		for (int r = 0; r < t->nrows; r++)
		{
			o.puts ("<tr>");
			for (int c = 0; c < t->ncols; c++)
			{
				const TCell &k = tcell (t, r, c);
				if (k.covered) continue;
				o.puts (r == 0 && t->header ? "<th" : "<td");
				int w = 0; for (int cc = c; cc < c + k.cs && cc < t->ncols; cc++) w += t->colW[cc];
				o.puts (" style=\"width:"); o.num (w / 20); o.puts ("pt");
				if (t->border != TB_NONE) { o.puts ("; border:"); o.num (wmax (1, t->bw / 8)); o.puts ("pt solid "); html_color (o, t->bcolor == AUTO ? 0 : t->bcolor); }
				if (k.fill != AUTO) { o.puts ("; background:"); html_color (o, k.fill); }
				o.put ('"');
				if (k.cs > 1) { o.puts (" colspan=\""); o.num (k.cs); o.put ('"'); }
				if (k.rs > 1) { o.puts (" rowspan=\""); o.num (k.rs); o.put ('"'); }
				o.put ('>');
				int cl = LS_NONE;
				for (int m = i; m < j; m++) if (p[m]->pf.row == r && p[m]->pf.col == c) html_para (o, d, p[m], cl, true);
				o.puts (r == 0 && t->header ? "</th>" : "</td>");
			}
			o.puts ("</tr>\n");
		}
		o.puts ("</table>\n");
		i = j;
	}
	if (openList != LS_NONE) o.puts (openList == LS_BULLET ? "</ul>\n" : "</ol>\n");
	o.puts ("</body>\n</html>\n");
	return o.n;
}

} // namespace wr

#endif
