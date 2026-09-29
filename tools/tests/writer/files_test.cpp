// files_test.cpp -- Writer's files on the PC: a document with all Writer knows (styles, formats, tab stops,
// lists, a table with merged cells, fields, headers and footers -- the first page's own --, an image, the
// mail merge's data) written as RTF, .docx and .odt and read back the same (each through the others
// too); with LibreOffice (when installed: soffice), our .docx and .odt converted by it (to .docx, .odt)
// and read back: their text, tables, fields, headers and footers kept. Linked with the desktop
// simulator's wtk (the image codecs) and kapi (fakekapi.o).
//
//   files_test DIR
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Apps/writer/fileio.h"
#include "Apps/writer/docx.h"
#include "Apps/writer/odt.h"
using namespace wr;

static int g_fail, g_checks;
#define CHECK(c, ...) do { g_checks++; if (!(c)) { g_fail++; printf ("FAIL %s:%d: ", what, __LINE__); printf (__VA_ARGS__); printf ("\n"); } } while (0)

static void now_fn (int *y, int *mo, int *d, int *h, int *mi, int *s) { *y = 2026; *mo = 9; *d = 29; *h = 14; *mi = 30; *s = 5; }

// ---- the document made -----------------------------------------------------------------------------------
static unsigned short fmt (Doc &d, const char *font, int size, unsigned flags, unsigned color = AUTO, unsigned hilite = AUTO)
{
	CharFmt f; f.font = (short) doc_font (d, font); f.size = (short) size; f.flags = (unsigned short) flags; f.color = color; f.hilite = hilite;
	return doc_fmt (d, f);
}
static Para *para (Doc &d, int story, int st, const char *text, unsigned short cf)
{
	Para *q = para_styled (d, st);
	unsigned u[512]; int n = decode_text (text, slen (text), u, 512);
	para_insert (q, 0, u, n, cf);
	q->endCf = cf;
	story_append (d, story, q);
	return q;
}
static void add (Para *q, const char *text, unsigned short cf) { unsigned u[512]; int n = decode_text (text, slen (text), u, 512); para_insert (q, q->len, u, n, cf); }
static void field (Doc &d, Para *q, int kind, const char *arg, unsigned short cf)
{
	CharFmt f = d.fmt[cf]; f.fld = doc_field (d, kind, arg);
	unsigned c = FIELD_CHAR; para_insert (q, q->len, &c, 1, doc_fmt (d, f));
}

static void make (Doc &d)
{
	doc_init (d);
	PageSetup &pg = d.page;
	pg.w = A4_W; pg.h = A4_H; pg.left = 1418; pg.right = 1134; pg.top = 1701; pg.bottom = 1417; pg.hdr = 709; pg.ftr = 567; pg.titlePg = true; pg.start = 3;
	scpy (d.mergeSrc, "SD:/docs/contacts.card", sizeof d.mergeSrc);
	unsigned short N = fmt (d, "Liberation Serif", 24, 0);
	unsigned short H1 = fmt (d, "Liberation Sans", 36, CF_BOLD);
	unsigned short B = fmt (d, "Liberation Serif", 24, CF_BOLD), I = fmt (d, "Liberation Serif", 24, CF_ITALIC);
	unsigned short U = fmt (d, "Liberation Serif", 24, CF_UNDER), S = fmt (d, "Liberation Serif", 24, CF_STRIKE);
	unsigned short SUP = fmt (d, "Liberation Serif", 24, CF_SUPER), SUB = fmt (d, "Liberation Serif", 24, CF_SUB);
	unsigned short RED = fmt (d, "Liberation Serif", 24, 0, 0xC00000), HL = fmt (d, "Liberation Serif", 24, 0, AUTO, 0xFFFF00);
	unsigned short MONO = fmt (d, "DejaVu Sans Mono", 20, 0), BIG = fmt (d, "Liberation Sans", 32, CF_BOLD | CF_ITALIC, 0x2F5496);
	unsigned short HF = fmt (d, "Liberation Serif", 20, 0);
	// headers and footers
	Para *h = para (d, SY_HEADER, ST_HEADER, "Onyx Writer ", HF); h->pf.align = AL_RIGHT; add (h, "-- the header", HF);
	Para *f = para (d, SY_FOOTER, ST_FOOTER, "Page ", HF); f->pf.align = AL_CENTER; field (d, f, FK_PAGE, "", HF); add (f, " of ", HF); field (d, f, FK_PAGES, "", HF);
	para (d, SY_FOOTER1, ST_FOOTER, "The first page's footer", HF)->pf.align = AL_CENTER;
	// the body
	para (d, SY_BODY, ST_TITLE, "A Test Document", fmt (d, "Liberation Sans", 56, CF_BOLD));
	para (d, SY_BODY, ST_H1, "Characters", H1);
	Para *q = para (d, SY_BODY, ST_NORMAL, "Plain, ", N);
	add (q, "bold", B); add (q, ", ", N); add (q, "italic", I); add (q, ", ", N); add (q, "underlined", U); add (q, ", ", N); add (q, "struck", S);
	add (q, ", E = mc", N); add (q, "2", SUP); add (q, ", H", N); add (q, "2", SUB); add (q, "O, ", N); add (q, "red", RED); add (q, " and ", N); add (q, "highlighted", HL);
	add (q, "; ", N); add (q, "mono", MONO); add (q, " and ", N); add (q, "big blue", BIG); add (q, ". Accents: é à ü œ € — “quotes”.", N);
	q = para (d, SY_BODY, ST_NORMAL, "Justified, indented, spaced paragraph with a first line indent and more words to fill the line.", N);
	q->pf.align = AL_JUSTIFY; q->pf.left = 567; q->pf.right = 283; q->pf.first = 425; q->pf.before = 240; q->pf.after = 120; q->pf.line = 150;
	q = para (d, SY_BODY, ST_NORMAL, "Hanging indent, kept with the next, lines together, no widow control.", N);
	q->pf.left = 720; q->pf.first = -360; q->pf.keepNext = true; q->pf.keepLines = true; q->pf.widow = false; q->pf.align = AL_CENTER;
	q = para (d, SY_BODY, ST_QUOTE, "A quotation.", fmt (d, "Liberation Serif", 24, CF_ITALIC, 0x404040));
	q = para (d, SY_BODY, ST_CODE, "int main (void) { return 0; }", MONO);
	para (d, SY_BODY, ST_H2, "Tab stops", fmt (d, "Liberation Sans", 30, CF_BOLD));
	q = para (d, SY_BODY, ST_NORMAL, "Chapter\t12", N); pf_add_tab (q->pf, 9354, TA_RIGHT, TL_DOT);
	q = para (d, SY_BODY, ST_NORMAL, "a\tb\tc\t3.14", N); pf_add_tab (q->pf, 2000, TA_LEFT, TL_NONE); pf_add_tab (q->pf, 4677, TA_CENTER, TL_DASH); pf_add_tab (q->pf, 8000, TA_DECIMAL, TL_LINE);
	para (d, SY_BODY, ST_H3, "Lists", fmt (d, "Liberation Sans", 26, CF_BOLD));
	for (int i = 0; i < 4; i++)
	{
		static const char *const T[4] = { "First bullet", "Second bullet", "A sub-item", "Back" };
		q = para (d, SY_BODY, ST_NORMAL, T[i], N); q->pf.list = LS_BULLET; q->pf.level = (unsigned char) (i == 2); q->pf.left = (short) (720 * (q->pf.level + 1)); q->pf.first = -360;
	}
	para (d, SY_BODY, ST_NORMAL, "Between the lists.", N);
	for (int i = 0; i < 3; i++)
	{
		static const char *const T[3] = { "One", "Two", "Three" };
		q = para (d, SY_BODY, ST_NORMAL, T[i], N); q->pf.list = LS_NUMBER; q->pf.left = 720; q->pf.first = -360;
	}
	// a table: a heading row, a cell over two columns, one over two rows, shading, a row's height
	Table *t = table_alloc (4, 3);
	t->colW[0] = 3000; t->colW[1] = 2500; t->colW[2] = 3500;
	t->header = true; t->border = TB_ALL; t->bw = 6; t->bcolor = 0x1F3864; t->align = AL_CENTER; t->rowH[3] = 600;
	tcell (t, 0, 0).fill = tcell (t, 0, 1).fill = tcell (t, 0, 2).fill = 0x4472C4;
	tcell (t, 1, 0).rs = 2; tcell (t, 2, 0).covered = true;
	tcell (t, 3, 0).cs = 2; tcell (t, 3, 1).covered = true;
	tcell (t, 1, 1).fill = 0xD9E2F3;
	int idx = doc_table (d, t);
	static const char *const cells[4][3] = { { "Item", "Qty", "Price" }, { "Coffee (two rows)", "12", "3.50" }, { 0, "4", "1.20" }, { "Over two columns", 0, "4.70" } };
	for (int r = 0; r < 4; r++)
		for (int c = 0; c < 3; c++)
		{
			if (!cells[r][c]) continue;
			q = para (d, SY_BODY, ST_NORMAL, cells[r][c], r == 0 ? B : N);
			q->pf.tbl = (short) idx; q->pf.row = (short) r; q->pf.col = (short) c; q->pf.after = 0; q->pf.line = 100;
			if (c == 2) q->pf.align = AL_RIGHT;
			if (r == 1 && c == 1) { q = para (d, SY_BODY, ST_NORMAL, "second paragraph", I); q->pf.tbl = (short) idx; q->pf.row = 1; q->pf.col = 1; q->pf.after = 0; q->pf.line = 100; }
		}
	para (d, SY_BODY, ST_NORMAL, "", N);
	// fields
	q = para (d, SY_BODY, ST_NORMAL, "Date: ", N); field (d, q, FK_DATE, "dddd d MMMM yyyy", N); add (q, ", time: ", N); field (d, q, FK_TIME, "HH:mm", N);
	add (q, ", dear ", N); field (d, q, FK_MERGE, "Name", B); add (q, ".", N);
	// an image
	unsigned *px = new unsigned[16 * 8];
	for (int i = 0; i < 16 * 8; i++) px[i] = 0xFF000000u | (unsigned) (i * 37 % 255) << 16 | (unsigned) (i * 11 % 255) << 8 | 0x80;
	int im = doc_image (d, px, 16, 8, 0, 0, false);
	q = para (d, SY_BODY, ST_NORMAL, "An image: ", N);
	CharFmt ic = d.fmt[N]; ic.obj = im + 1; ic.ow = 1440; ic.oh = 720;
	unsigned oc = OBJ_CHAR; para_insert (q, q->len, &oc, 1, doc_fmt (d, ic));
	add (q, " (1 x 0.5 in).", N);
	q = para (d, SY_BODY, ST_NORMAL, "A new page.", N); q->pf.pageBreak = true;
	// a table of contents' entries (as the TOC makes them)
	para (d, SY_BODY, ST_TOCHEAD, "Contents", fmt (d, "Liberation Sans", 32, CF_BOLD));
	q = para (d, SY_BODY, ST_TOC1, "Characters\t3", N); pf_add_tab (q->pf, 9354, TA_RIGHT, TL_DOT);
	q = para (d, SY_BODY, ST_TOC2, "Tab stops\t3", N); pf_add_tab (q->pf, 9354, TA_RIGHT, TL_DOT);
	para (d, SY_BODY, ST_NORMAL, "The end.", N);
}

// ---- comparing ----------------------------------------------------------------------------------------------
static bool near (int a, int b, int tol) { return a - b <= tol && b - a <= tol; }
static void text_of (const Para *q, char *out, int cap)
{
	int n = 0;
	for (int k = 0; k < q->len && n < cap - 4; k++)
	{
		unsigned c = q->ch[k];
		if (c == OBJ_CHAR) { out[n++] = '#'; continue; }
		if (c == FIELD_CHAR) { out[n++] = '@'; continue; }
		if (c < 0x80) out[n++] = (char) c; else if (c < 0x800) { out[n++] = (char) (0xC0 | c >> 6); out[n++] = (char) (0x80 | (c & 0x3F)); }
		else { out[n++] = (char) (0xE0 | c >> 12); out[n++] = (char) (0x80 | (c >> 6 & 0x3F)); out[n++] = (char) (0x80 | (c & 0x3F)); }
	}
	out[n] = 0;
}
// strict: every format; else (another program's file) the text, tables' shapes, fields, stories.
static void compare (Doc &a, Doc &b, const char *what, bool strict)
{
	const PageSetup &pa = a.page, &pb = b.page;
	if (strict)
	{
		CHECK (pa.w == pb.w && pa.h == pb.h, "page %dx%d / %dx%d", pa.w, pa.h, pb.w, pb.h);
		CHECK (near (pa.left, pb.left, 2) && near (pa.right, pb.right, 2) && near (pa.top, pb.top, 2) && near (pa.bottom, pb.bottom, 2),
		       "margins %d %d %d %d / %d %d %d %d", pa.left, pa.right, pa.top, pa.bottom, pb.left, pb.right, pb.top, pb.bottom);
		CHECK (near (pa.hdr, pb.hdr, 2) && near (pa.ftr, pb.ftr, 2), "hdr/ftr %d %d / %d %d", pa.hdr, pa.ftr, pb.hdr, pb.ftr);
		CHECK (pa.start == pb.start, "start %d / %d", pa.start, pb.start);
		CHECK (!strcmp (a.mergeSrc, b.mergeSrc), "mergeSrc '%s' / '%s'", a.mergeSrc, b.mergeSrc);
	}
	CHECK (pa.titlePg == pb.titlePg, "titlePg %d / %d", pa.titlePg, pb.titlePg);
	for (int s = 0; s < SY_COUNT; s++)
	{
		int na, nb; Para **A = story_p (a, s, &na), **B = story_p (b, s, &nb);
		if (story_empty (a, s)) { CHECK (story_empty (b, s), "story %d should be empty", s); continue; }
		CHECK (na == nb, "story %d: %d / %d paragraphs", s, na, nb);
		if (na != nb) { for (int i = 0; i < na || i < nb; i++) { char x[200] = "", y[200] = ""; if (i < na) text_of (A[i], x, 200); if (i < nb) text_of (B[i], y, 200); printf ("   %2d: [%s] [%s]\n", i, x, y); } continue; }
		for (int i = 0; i < na; i++)
		{
			const Para *p = A[i], *q = B[i];
			char x[400], y[400]; text_of (p, x, 400); text_of (q, y, 400);
			CHECK (!strcmp (x, y), "story %d para %d: [%s] / [%s]", s, i, x, y);
			CHECK (p->pf.style == q->pf.style, "story %d para %d [%.20s]: style %d / %d", s, i, x, p->pf.style, q->pf.style);
			CHECK (in_table (p) == in_table (q) && p->pf.row == q->pf.row && p->pf.col == q->pf.col, "para %d cell (%d %d) / (%d %d)", i, p->pf.row, p->pf.col, q->pf.row, q->pf.col);
			CHECK (p->pf.list == q->pf.list && p->pf.level == q->pf.level, "para %d [%.20s]: list %d.%d / %d.%d", i, x, p->pf.list, p->pf.level, q->pf.list, q->pf.level);
			CHECK (p->pf.pageBreak == q->pf.pageBreak, "para %d [%.20s]: page break %d / %d", i, x, p->pf.pageBreak, q->pf.pageBreak);
			if (strict)
			{
				const ParaFmt &f = p->pf, &g = q->pf;
				CHECK (f.align == g.align, "para %d [%.20s]: align %d / %d", i, x, f.align, g.align);
				CHECK (near (f.left, g.left, 2) && near (f.right, g.right, 2) && near (f.first, g.first, 2), "para %d [%.20s]: indents %d %d %d / %d %d %d", i, x, f.left, f.right, f.first, g.left, g.right, g.first);
				CHECK (near (f.before, g.before, 2) && near (f.after, g.after, 2) && near (f.line, g.line, 1), "para %d [%.20s]: spacing %d %d %d / %d %d %d", i, x, f.before, f.after, f.line, g.before, g.after, g.line);
				CHECK (f.keepNext == g.keepNext && f.keepLines == g.keepLines && f.widow == g.widow, "para %d [%.20s]: keep %d %d %d / %d %d %d", i, x, f.keepNext, f.keepLines, f.widow, g.keepNext, g.keepLines, g.widow);
				CHECK (f.ntab == g.ntab, "para %d [%.20s]: %d / %d tabs", i, x, f.ntab, g.ntab);
				for (int k = 0; k < f.ntab && k < g.ntab; k++)
					CHECK (near (f.tab[k].pos, g.tab[k].pos, 2) && f.tab[k].align == g.tab[k].align && f.tab[k].leader == g.tab[k].leader,
					       "para %d tab %d: %d %d %d / %d %d %d", i, k, f.tab[k].pos, f.tab[k].align, f.tab[k].leader, g.tab[k].pos, g.tab[k].align, g.tab[k].leader);
			}
			for (int k = 0; k < p->len && k < q->len; k++)
			{
				const CharFmt &f = a.fmt[p->cf[k]], &g = b.fmt[q->cf[k]];
				if (strict)
				{
					bool same = !sicmp (a.fontName[f.font], b.fontName[g.font]) && f.size == g.size && f.flags == g.flags && f.color == g.color && f.hilite == g.hilite;
					CHECK (same, "para %d [%.20s] char %d: %s %d %x %06x %06x / %s %d %x %06x %06x", i, x, k, a.fontName[f.font], f.size, f.flags, f.color, f.hilite,
					       b.fontName[g.font], g.size, g.flags, g.color, g.hilite);
					if (!same) break;
				}
				if (f.obj)
				{
					CHECK (g.obj > 0, "para %d char %d: an image lost", i, k);
					if (g.obj)
					{
						const Image &ia = a.img[f.obj - 1], &ib = b.img[g.obj - 1];
						CHECK (ia.w == ib.w && ia.h == ib.h, "image %dx%d / %dx%d", ia.w, ia.h, ib.w, ib.h);
						CHECK (near (f.ow, g.ow, 3) && near (f.oh, g.oh, 3), "image size %d %d / %d %d", f.ow, f.oh, g.ow, g.oh);
						bool px = ia.w == ib.w && ia.h == ib.h;
						for (int m = 0; px && m < ia.w * ia.h; m++) if (ia.px[m] != ib.px[m]) px = false;
						CHECK (px, "image's pixels");
					}
				}
				if (f.fld)
				{
					CHECK (g.fld > 0, "para %d char %d: a field lost", i, k);
					if (g.fld) CHECK (a.fld[f.fld - 1].kind == b.fld[g.fld - 1].kind && !strcmp (a.fld[f.fld - 1].arg, b.fld[g.fld - 1].arg),
							  "field %d '%s' / %d '%s'", a.fld[f.fld - 1].kind, a.fld[f.fld - 1].arg, b.fld[g.fld - 1].kind, b.fld[g.fld - 1].arg);
				}
			}
			// the tables (at each's first paragraph)
			if (in_table (p) && (i == 0 || !in_table (A[i - 1])) && in_table (q))
			{
				const Table *t = para_table (a, p), *u = para_table (b, q);
				CHECK (t && u && t->nrows == u->nrows && t->ncols == u->ncols, "table %dx%d / %dx%d", t ? t->nrows : 0, t ? t->ncols : 0, u ? u->nrows : 0, u ? u->ncols : 0);
				if (!t || !u || t->nrows != u->nrows || t->ncols != u->ncols) continue;
				for (int k = 0; k < t->nrows * t->ncols; k++)
				{
					const TCell &c = t->cell[k], &e = u->cell[k];
					CHECK (c.cs == e.cs && c.rs == e.rs && c.covered == e.covered, "cell %d: %d %d %d / %d %d %d", k, c.cs, c.rs, c.covered, e.cs, e.rs, e.covered);
					if (strict && !c.covered) CHECK (c.fill == e.fill, "cell %d fill %06x / %06x", k, c.fill, e.fill);
				}
				if (strict)
				{
					for (int c = 0; c < t->ncols; c++) CHECK (near (t->colW[c], u->colW[c], 3), "column %d: %d / %d", c, t->colW[c], u->colW[c]);
					for (int r = 0; r < t->nrows; r++) CHECK (near (t->rowH[r], u->rowH[r], 3), "row %d: %d / %d", r, t->rowH[r], u->rowH[r]);
					CHECK (t->border == u->border && near (t->bw, u->bw, 1) && t->bcolor == u->bcolor, "lines %d %d %06x / %d %d %06x", t->border, t->bw, t->bcolor, u->border, u->bw, u->bcolor);
					CHECK (t->header == u->header && t->align == u->align, "header %d align %d / %d %d", t->header, t->align, u->header, u->align);
				}
				else CHECK (t->header == u->header, "heading row %d / %d", t->header, u->header);
			}
		}
	}
}

// ---- files ------------------------------------------------------------------------------------------------
static bool save (Doc &d, const char *path)
{
	int n = slen (path);
	char *b = 0; unsigned len = 0; bool ok = true;
	if (n > 5 && !strcmp (path + n - 5, ".docx")) { unsigned char *z; ok = docx_save (d, &z, &len); b = (char *) z; }
	else if (n > 4 && !strcmp (path + n - 4, ".odt")) { unsigned char *z; ok = odt_save (d, &z, &len); b = (char *) z; }
	else { Out o; rtf_save (d, o); b = o.b; len = (unsigned) o.n; }
	FILE *f = fopen (path, "wb");
	if (!ok || !f) { delete[] b; return false; }
	fwrite (b, 1, len, f); fclose (f);
	delete[] b;
	return true;
}
static bool load (Doc &d, const char *path)
{
	FILE *f = fopen (path, "rb"); if (!f) return false;
	fseek (f, 0, SEEK_END); long l = ftell (f); fseek (f, 0, SEEK_SET);
	char *b = new char[l + 1]; int n = (int) fread (b, 1, l, f); b[n] = 0; fclose (f);
	doc_init (d);
	bool ok = rtf_is (b, n) ? rtf_load (d, b, n) : docx_is (b, n) ? docx_load (d, b, n) : odt_is (b, n) ? odt_load (d, b, n) : false;
	delete[] b;
	return ok;
}

// RTF tables' lines read as the cells say them: top and bottom only -> the rows' lines; at the sides too
// -> every edge; none (or \brdrnone) -> none.
static void borders_test ()
{
	const char *what = "RTF lines";
	static const struct { const char *cell; int border; } T[] = {
		{ "\\clbrdrt\\brdrs\\brdrw10\\clbrdrb\\brdrs\\brdrw10", TB_ROWS },
		{ "\\clbrdrt\\brdrs\\clbrdrl\\brdrs\\clbrdrb\\brdrs\\clbrdrr\\brdrs", TB_ALL },
		{ "\\clbrdrt\\brdrnone\\clbrdrl\\brdrnone", TB_NONE },
		{ "", TB_NONE } };
	for (unsigned i = 0; i < sizeof T / sizeof T[0]; i++)
	{
		char r[600];
		snprintf (r, sizeof r, "{\\rtf1\\ansi\\trowd%s\\cellx3000%s\\cellx6000\n\\pard\\intbl a\\cell b\\cell\\row\n\\pard after\\par}", T[i].cell, T[i].cell);
		Doc d; doc_init (d);
		bool ok = rtf_load (d, r, slen (r));
		const Table *t = ok && d.n ? para_table (d, d.p[0]) : 0;
		CHECK (t && t->border == T[i].border, "case %u: %d, not %d", i, t ? t->border : -1, T[i].border);
		doc_clear (d);
	}
}

int main (int argc, char **argv)
{
	borders_test ();
	const char *dir = argc > 1 ? argv[1] : "/tmp";
	g_now = now_fn;
	Doc src; make (src);
	char p1[300], p2[300];
	static const char *const EXT[3] = { ".rtf", ".docx", ".odt" };
	for (int i = 0; i < 3; i++)
	{
		snprintf (p1, sizeof p1, "%s/a%s", dir, EXT[i]);
		char what[64]; snprintf (what, sizeof what, "%s", EXT[i]);
		if (!save (src, p1)) { printf ("FAIL %s: not written\n", EXT[i]); g_fail++; continue; }
		Doc d; if (!load (d, p1)) { printf ("FAIL %s: not read\n", EXT[i]); g_fail++; continue; }
		compare (src, d, what, true);
		for (int j = 0; j < 3; j++)				// (then through another format)
		{
			snprintf (p2, sizeof p2, "%s/a%s%s", dir, EXT[i], EXT[j]);
			snprintf (what, sizeof what, "%s -> %s", EXT[i], EXT[j]);
			Doc e;
			if (!save (d, p2) || !load (e, p2)) { printf ("FAIL %s: not written / read\n", what); g_fail++; continue; }
			compare (src, e, what, true);
			doc_clear (e);
		}
		doc_clear (d);
	}
	// LibreOffice: our files converted, read back
	if (!system ("soffice --version > /dev/null 2>&1"))
	{
		static const char *const CONV[][2] = { { ".docx", "docx:\"MS Word 2007 XML\"" }, { ".docx", "odt" }, { ".odt", "docx:\"MS Word 2007 XML\"" }, { ".odt", "odt" } };
		for (int k = 0; k < 4; k++)
		{
			char cmd[900], sub[320];
			snprintf (sub, sizeof sub, "%s/lo%d", dir, k);
			snprintf (cmd, sizeof cmd, "rm -rf %s && mkdir -p %s && cd %s && cp %s/a%s in%s && soffice --headless --convert-to %s --outdir out in%s > log.txt 2>&1",
				  sub, sub, sub, dir, CONV[k][0], CONV[k][0], CONV[k][1], CONV[k][0]);
			char what[64]; snprintf (what, sizeof what, "LibreOffice %s -> %s", CONV[k][0], CONV[k][1][0] == 'd' ? ".docx" : ".odt");
			if (system (cmd)) { printf ("FAIL %s: not converted\n", what); g_fail++; continue; }
			snprintf (p2, sizeof p2, "%s/out/in%s", sub, CONV[k][1][0] == 'd' ? ".docx" : ".odt");
			Doc e;
			if (!load (e, p2)) { printf ("FAIL %s: not read\n", what); g_fail++; continue; }
			compare (src, e, what, false);
			doc_clear (e);
		}
	}
	else printf ("(no LibreOffice: its conversions not tried)\n");
	printf ("%d checks, %d failed\n", g_checks, g_fail);
	return g_fail ? 1 : 0;
}
