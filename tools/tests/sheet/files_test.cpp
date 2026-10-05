// files_test.cpp -- the spreadsheet's files on the PC: a book written as .xlsx and read back the same;
// with LibreOffice (when installed: soffice), our .xlsx converted to .ods and .xlsx by it, read back.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "imagekit/img/stb_image.h"
#include "Apps/sheet/condfmt.h"
#include <stdio.h>
#include <stdlib.h>
unsigned char *img_inflate (const void *data, unsigned len, bool zlib, unsigned *outLen)
{
	int n = 0;
	char *r = zlib ? stbi_zlib_decode_malloc ((const char *) data, (int) len, &n) : stbi_zlib_decode_noheader_malloc ((const char *) data, (int) len, &n);
	if (!r) return 0;
	unsigned char *o = new unsigned char[n > 0 ? n : 1];
	memcpy (o, r, n); free (r);
	*outLen = (unsigned) n;
	return o;
}
using namespace ss;
static int g_fail, g_checks;
static void now_fn (int *y, int *mo, int *d, int *h, int *mi, int *s) { *y = 2026; *mo = 9; *d = 29; *h = 14; *mi = 30; *s = 0; }
static void set (Book &b, int sh, const char *ref, const char *text) { int r, c; parse_cell_name (ref, &r, &c); if (!cell_input (b, b.sh[sh], r, c, text)) { printf ("cannot set %s\n", text); g_fail++; } }
static Rect R (const char *a, const char *z) { Rect r; parse_cell_name (a, &r.r0, &r.c0); parse_cell_name (z, &r.r1, &r.c1); return r; }
static void bold (Style &s, const void *) { s.bold = 1; }
static void italic_red (Style &s, const void *) { s.italic = 1; s.color = 0xC00000; }
static void fill_y (Style &s, const void *) { s.fill = 0xFFF2CC; }
static void borders (Style &s, const void *) { for (int k = 0; k < 4; k++) { s.bs[k] = BS_THIN; s.bc[k] = 0x444444; } }
static void money (Style &s, const void *a) { s.fmt = (unsigned short) (long) a; }
static void center_wrap (Style &s, const void *) { s.ha = HA_CENTER; s.va = VA_CENTER; s.wrap = 1; }
static void big (Style &s, const void *a) { s.size = 140; s.font = (unsigned short) (long) a; }

static void shown (Book &b, int sh, const char *ref, char *out)
{
	int r, c; parse_cell_name (ref, &r, &c);
	Shown s; cell_shown (b, b.sh[sh], b.sh[sh]->cells.get (r, c), s, 24);
	strcpy (out, s.text);
}
// Two books alike: every cell's shown text and input text, the styles that matter, sizes, merges.
static void compare (Book &a, Book &b, const char *what, bool strict)
{
	g_checks++;
	if (a.ns != b.ns) { printf ("FAIL %s: %d sheets, %d expected\n", what, b.ns, a.ns); g_fail++; return; }
	int bad = 0;
	for (int i = 0; i < a.ns; i++)
	{
		Sheet *s = a.sh[i], *t = b.sh[i];
		if (strcmp (s->name, t->name)) { printf ("FAIL %s: sheet %d named [%s], [%s] expected\n", what, i, t->name, s->name); bad++; }
		for (int k = 0; k < s->cells.cap; k++)
		{
			Cell *x = s->cells.t[k];
			if (!x || x->kind == K_NONE) continue;
			Cell *y = t->cells.get (x->r, x->c);
			char ref[16]; cell_name (x->r, x->c, ref);
			Shown sx, sy; cell_shown (a, s, x, sx, 24); cell_shown (b, t, y, sy, 24);
			if (strcmp (sx.text, sy.text)) { if (bad < 12) printf ("FAIL %s: %s!%s shows [%s], [%s] expected\n", what, s->name, ref, sy.text, sx.text); bad++; continue; }
			Buf ex, ey; cell_edit_text (a, s, x, ex); if (y) cell_edit_text (b, t, y, ey);
			if (strict && strcmp (ex.str (), ey.str ())) { if (bad < 12) printf ("FAIL %s: %s!%s holds [%s], [%s] expected\n", what, s->name, ref, ey.str (), ex.str ()); bad++; continue; }
			const Style &p = a.styles.s[x->style], &q = b.styles.s[y->style];
			if (p.bold != q.bold || p.italic != q.italic || p.size != q.size || p.fill != q.fill || p.ha != q.ha || p.wrap != q.wrap || (p.color == AUTO ? 0 : p.color) != (q.color == AUTO ? 0 : q.color))
			{ if (bad < 12) printf ("FAIL %s: %s!%s style differs (b%d/%d i%d/%d sz%d/%d fill%06X/%06X ha%d/%d col%06X/%06X)\n", what, s->name, ref, q.bold, p.bold, q.italic, p.italic, q.size, p.size, q.fill, p.fill, q.ha, p.ha, q.color, p.color); bad++; }
			for (int sd = 0; sd < 4; sd++) if (p.bs[sd] != q.bs[sd]) { if (bad < 12) printf ("FAIL %s: %s!%s border %d: %d, %d expected\n", what, s->name, ref, sd, q.bs[sd], p.bs[sd]); bad++; break; }
		}
		int tol = strict ? 2 : col_w (s, 0) / 10 + 2;		// (LibreOffice sizes the columns by its own fonts)
		for (int c = 0; c < 10; c++) if (abs (col_w (s, c) - col_w (t, c)) > (strict ? 2 : (col_w (s, c) / 10 + 2))) { printf ("FAIL %s: %s column %d is %d px, %d expected\n", what, s->name, c, col_w (t, c), col_w (s, c)); bad++; }
		for (int r = 0; r < 10; r++) if (abs (row_h (s, r) - row_h (t, r)) > 2) { printf ("FAIL %s: %s row %d is %d px, %d expected\n", what, s->name, r, row_h (t, r), row_h (s, r)); bad++; }
		if (s->nmerge != t->nmerge) { printf ("FAIL %s: %s has %d merges, %d expected\n", what, s->name, t->nmerge, s->nmerge); bad++; }
		(void) tol;
		// (LibreOffice without a window drops the view: the panes checked on our own trips only)
		if (strict && (s->freezeR != t->freezeR || s->freezeC != t->freezeC)) { printf ("FAIL %s: %s frozen %d/%d, %d/%d expected\n", what, s->name, t->freezeR, t->freezeC, s->freezeR, s->freezeC); bad++; }
		if (s->ncharts != t->ncharts) { printf ("FAIL %s: %s has %d charts, %d expected\n", what, s->name, t->ncharts, s->ncharts); bad++; }
		for (int k = 0; k < s->ncharts && k < t->ncharts; k++)
		{
			const Chart &p = *s->charts[k], &q = *t->charts[k];
			if (p.type != q.type || memcmp (&p.src, &q.src, sizeof p.src) || p.head != q.head || p.side != q.side || strcmp (p.title, q.title) || p.legend != q.legend)
			{ printf ("FAIL %s: %s chart %d differs (type %d/%d, title [%s]/[%s], legend %d/%d, head %d/%d side %d/%d)\n", what, s->name, k, q.type, p.type, q.title, p.title, q.legend, p.legend, q.head, p.head, q.side, p.side); bad++; }
			// (LibreOffice sizes the columns by its own font: a chart anchored to cells moves and grows a little)
			else if (abs (p.x - q.x) > 12 + p.x / 10 || abs (p.y - q.y) > 12 + p.y / 10 || abs (p.w - q.w) > 12 + p.w / 10 || abs (p.h - q.h) > 12 + p.h / 10)
			{ printf ("FAIL %s: %s chart %d at %d,%d %dx%d, %d,%d %dx%d expected\n", what, s->name, k, q.x, q.y, q.w, q.h, p.x, p.y, p.w, p.h); bad++; }
		}
	}
	// the defined names
	for (int i = 0; i < a.nnames; i++)
	{
		int k = name_index (b, a.names[i].name, a.names[i].scope ? -1 : 0);
		if (a.names[i].scope) for (int j = 0; j < b.nnames; j++) if (!strcmp (b.names[j].name, a.names[i].name)) k = j;
		if (k < 0) { printf ("FAIL %s: the name %s is lost\n", what, a.names[i].name); bad++; continue; }
		Buf x, y; formula_print (a, a.names[i].f, x); formula_print (b, b.names[k].f, y);
		if (strcmp (x.str (), y.str ())) { printf ("FAIL %s: the name %s is [%s], [%s] expected\n", what, a.names[i].name, y.str (), x.str ()); bad++; }
	}
	if (bad) g_fail++;
}

// ---- conditional formats: the looks the rules give, kept through the files ------------------------------------
static CondFmt cfr (const char *range, int type, int op, const char *a, const char *b, unsigned fill, unsigned color, int bold)
{
	CondFmt c; memset (&c, 0, sizeof c);
	Rect r; parse_sqref (range, &r, 1); c.r = r;
	c.type = type; c.op = op; scpy (c.a, a, sizeof c.a); scpy (c.b, b, sizeof c.b);
	c.fill = fill; c.color = color; c.bold = (signed char) bold; c.italic = -1;
	return c;
}
static const char *cf_look_text (Book &b, Sheet *s, const char *ref, char *out)
{
	int r, c; parse_cell_name (ref, &r, &c);
	CfLook L; cf_cell (b, s, s->cells.get (r, c), r, c, L);
	snprintf (out, 80, "fill %06X color %06X bold %d bar %.2f", L.fill == AUTO ? 0xFFFFFF : L.fill, L.color == AUTO ? 0 : L.color, L.bold, L.bar);
	return out;
}
static void cf_expect (Book &b, Sheet *s, const char *ref, const char *want, const char *what)
{
	char t[80]; cf_look_text (b, s, ref, t);
	g_checks++;
	if (strcmp (t, want)) { printf ("FAIL %s: %s looks [%s], [%s] expected\n", what, ref, t, want); g_fail++; }
}
static void cf_tests (const char *dir)
{
	Book a; book_init (a); book_add_sheet (a, "Rules");
	Sheet *s = a.sh[0];
	char ref[16], v[16];
	for (int i = 1; i <= 10; i++) { snprintf (ref, sizeof ref, "A%d", i); snprintf (v, sizeof v, "%d", i); set (a, 0, ref, v); }
	set (a, 0, "B1", "0"); set (a, 0, "B2", "50"); set (a, 0, "B3", "100");
	set (a, 0, "C1", "5"); set (a, 0, "C2", "10");
	set (a, 0, "D1", "a"); set (a, 0, "D2", "b"); set (a, 0, "D3", "A"); set (a, 0, "D4", "c");
	set (a, 0, "E1", "Apple"); set (a, 0, "E2", "pear"); set (a, 0, "E3", "grape");
	set (a, 0, "G1", "3"); set (a, 0, "G2", "4");
	CondFmt rules[] = {
		cfr ("A1:A10", CF_CELL, CO_GT, "7", "", 0xFFC7CE, 0x9C0006, -1),
		cfr ("A1:A10", CF_TOP, 0, "2", "", AUTO, AUTO, 1),
		cfr ("A1:A10", CF_CELL, CO_BETWEEN, "=$G$1", "=$G$2", 0xFFEB9C, AUTO, -1),
		cfr ("D1:D4", CF_DUP, 0, "", "", 0xC6EFCE, AUTO, -1),
		cfr ("E1:E3", CF_TEXT, CT_CONTAINS, "ap", "", 0xDDEBF7, AUTO, -1),
		cfr ("F1:F4", CF_FORMULA, 0, "=MOD(ROW(),2)=0", "", 0xEEEEEE, AUTO, -1),
	};
	int nr = (int) (sizeof rules / sizeof rules[0]);
	s->cf = (CondFmt *) malloc ((nr + 2) * sizeof (CondFmt));
	for (int i = 0; i < nr; i++) s->cf[s->ncf++] = rules[i];
	CondFmt sc = cfr ("B1:B3", CF_SCALE, 2, "", "", AUTO, AUTO, -1); sc.c0 = 0xFFFFFF; sc.c2 = 0x00FF00; s->cf[s->ncf++] = sc;
	CondFmt br = cfr ("C1:C2", CF_BAR, 0, "", "", AUTO, AUTO, -1); br.c0 = 0x638EC6; s->cf[s->ncf++] = br;
	recalc (a); cf_changed ();
	auto checks = [&] (Book &b, const char *what) {
		Sheet *t = b.sh[0];
		cf_expect (b, t, "A7", "fill FFFFFF color 000000 bold -1 bar -1.00", what);
		cf_expect (b, t, "A8", "fill FFC7CE color 9C0006 bold -1 bar -1.00", what);
		cf_expect (b, t, "A9", "fill FFC7CE color 9C0006 bold 1 bar -1.00", what);
		cf_expect (b, t, "A3", "fill FFEB9C color 000000 bold -1 bar -1.00", what);
		cf_expect (b, t, "A4", "fill FFEB9C color 000000 bold -1 bar -1.00", what);
		cf_expect (b, t, "D1", "fill C6EFCE color 000000 bold -1 bar -1.00", what);
		cf_expect (b, t, "D2", "fill FFFFFF color 000000 bold -1 bar -1.00", what);
		cf_expect (b, t, "D3", "fill C6EFCE color 000000 bold -1 bar -1.00", what);
		cf_expect (b, t, "E1", "fill DDEBF7 color 000000 bold -1 bar -1.00", what);
		cf_expect (b, t, "E2", "fill FFFFFF color 000000 bold -1 bar -1.00", what);
		cf_expect (b, t, "E3", "fill DDEBF7 color 000000 bold -1 bar -1.00", what);
		cf_expect (b, t, "F2", "fill EEEEEE color 000000 bold -1 bar -1.00", what);
		cf_expect (b, t, "F3", "fill FFFFFF color 000000 bold -1 bar -1.00", what);
		cf_expect (b, t, "B1", "fill FFFFFF color 000000 bold -1 bar -1.00", what);
		cf_expect (b, t, "B2", "fill 80FF80 color 000000 bold -1 bar -1.00", what);
		cf_expect (b, t, "B3", "fill 00FF00 color 000000 bold -1 bar -1.00", what);
		cf_expect (b, t, "C1", "fill FFFFFF color 000000 bold -1 bar 0.10", what);
		cf_expect (b, t, "C2", "fill FFFFFF color 000000 bold -1 bar 1.00", what);
	};
	checks (a, "conditional formats");
	const char *why = 0;
	char p1[256]; snprintf (p1, sizeof p1, "%s/cf.xlsx", dir);
	if (!book_save (a, p1, &why)) { printf ("FAIL cf save: %s\n", why); g_fail++; return; }
	Book b; book_init (b);
	if (!book_load (b, p1, &why)) { printf ("FAIL cf load: %s\n", why); g_fail++; return; }
	recalc (b); cf_changed ();
	g_checks++; if (b.sh[0]->ncf != a.sh[0]->ncf) { printf ("FAIL cf: %d rules read, %d written\n", b.sh[0]->ncf, a.sh[0]->ncf); g_fail++; }
	checks (b, "conditional formats, .xlsx");
	if (system ("which soffice > /dev/null 2>&1") == 0)
	{
		char cmd[512]; snprintf (cmd, sizeof cmd, "cd %s && soffice --headless --convert-to xlsx:\"Calc MS Excel 2007 XML\" --outdir lo3 cf.xlsx > /dev/null 2>&1", dir);
		if (system (cmd) == 0)
		{
			char px[256]; snprintf (px, sizeof px, "%s/lo3/cf.xlsx", dir);
			Book c; book_init (c);
			if (!book_load (c, px, &why)) { printf ("FAIL cf LibreOffice load: %s\n", why); g_fail++; }
			else { recalc (c); cf_changed (); checks (c, "conditional formats, LibreOffice's .xlsx"); }
			book_clear (c);
		}
		snprintf (cmd, sizeof cmd, "cd %s && soffice --headless --convert-to ods --outdir lo3 cf.xlsx > /dev/null 2>&1", dir);
		if (system (cmd) == 0)
		{
			char po[256]; snprintf (po, sizeof po, "%s/lo3/cf.ods", dir);
			Book c; book_init (c);
			if (!book_load (c, po, &why)) { printf ("FAIL cf LibreOffice ods load: %s\n", why); g_fail++; }
			else { recalc (c); cf_changed (); checks (c, "conditional formats, LibreOffice's .ods"); }
			book_clear (c);
		}
	}
	book_clear (a); book_clear (b);
}
// Rows / columns inserted and deleted: the rules' ranges and their formulas move as the cells' do.
static void cf_shift_tests ()
{
	Book a; book_init (a); book_add_sheet (a, "Rules"); book_add_sheet (a, "Other");
	Sheet *s = a.sh[0];
	set (a, 0, "B5", "-1"); set (a, 0, "B6", "2"); set (a, 0, "B7", "-3"); set (a, 0, "D1", "0");
	set (a, 0, "C5", "a"); set (a, 0, "C6", "b"); set (a, 0, "C7", "c");
	s->cf = (CondFmt *) malloc (4 * sizeof (CondFmt));
	s->cf[s->ncf++] = cfr ("C5:C7", CF_FORMULA, 0, "=$B5<$D$1", "", 0xFFC7CE, AUTO, -1);
	s->cf[s->ncf++] = cfr ("B5:B7", CF_CELL, CO_BETWEEN, "=$D$1-5", "=Other!A1", 0xC6EFCE, AUTO, -1);
	s->cf[s->ncf++] = cfr ("C5:C7", CF_FORMULA, 0, "B5>0", "", 0xDDEBF7, AUTO, -1);	// (no "=": as .xlsx keeps it)
	recalc (a); cf_changed ();
	auto texts = [&] (const char *w0, const char *w1a, const char *w1b, const char *w2, const char *what) {
		const char *want[4] = { w0, w1a, w1b, w2 }, *got[4] = { s->cf[0].a, s->cf[1].a, s->cf[1].b, s->cf[2].a };
		for (int i = 0; i < 4; i++)
		{
			g_checks++;
			if (strcmp (got[i], want[i])) { printf ("FAIL cf shift, %s: [%s], [%s] expected\n", what, got[i], want[i]); g_fail++; }
		}
	};
	cf_expect (a, s, "C5", "fill FFC7CE color 000000 bold -1 bar -1.00", "cf shift");
	cf_expect (a, s, "C6", "fill DDEBF7 color 000000 bold -1 bar -1.00", "cf shift");
	insert_rows (a, s, 1, 2); recalc (a); cf_changed ();			// (above everything: B5 -> B7, D1 stays)
	texts ("=$B7<$D$1", "=$D$1-5", "=Other!A1", "B7>0", "rows inserted");
	g_checks++; if (s->cf[0].r.r0 != 6 || s->cf[0].r.r1 != 8) { printf ("FAIL cf shift: the range at rows %d..%d\n", s->cf[0].r.r0 + 1, s->cf[0].r.r1 + 1); g_fail++; }
	cf_expect (a, s, "C7", "fill FFC7CE color 000000 bold -1 bar -1.00", "cf shift, rows inserted");
	cf_expect (a, s, "C8", "fill DDEBF7 color 000000 bold -1 bar -1.00", "cf shift, rows inserted");
	cf_expect (a, s, "C9", "fill FFC7CE color 000000 bold -1 bar -1.00", "cf shift, rows inserted");
	insert_cols (a, s, 0, 1); recalc (a); cf_changed ();			// (a column before A: every column moves)
	texts ("=$C7<$E$1", "=$E$1-5", "=Other!A1", "C7>0", "a column inserted");
	cf_expect (a, s, "D8", "fill DDEBF7 color 000000 bold -1 bar -1.00", "cf shift, a column inserted");
	delete_rows (a, s, 0, 1); recalc (a); cf_changed ();			// (the row of $E$1 deleted: #REF!)
	texts ("=$C6<#REF!", "=#REF!-5", "=Other!A1", "C6>0", "a row deleted");
	insert_rows (a, a.sh[1], 0, 1);						// (another sheet's rows: the reference to it moves)
	texts ("=$C6<#REF!", "=#REF!-5", "=Other!A2", "C6>0", "the other sheet's rows");
	book_clear (a);
}

// ---- the AutoFilter: rows hidden by the values chosen, kept through the files --------------------------------
static void af_expect (Book &b, const char *hidden, const char *what)
{
	Sheet *s = b.sh[0];
	char got[64] = "";
	for (int r = 1; r <= 7; r++) { RowInfo *ri = row_info (s, r); if (ri && (ri->fl & RF_HIDDEN)) { char t[16]; snprintf (t, sizeof t, "%d ", r + 1); strcat (got, t); } }
	g_checks++;
	if (strcmp (got, hidden)) { printf ("FAIL %s: rows hidden [%s], [%s] expected\n", what, got, hidden); g_fail++; }
	g_checks++;
	if (!s->af.on || s->af.r.r0 != 0 || s->af.r.c0 != 0 || s->af.r.c1 != 1 || s->af.r.r1 != 6) { printf ("FAIL %s: the filter's range %d %d:%d %d (on %d)\n", what, s->af.r.r0, s->af.r.c0, s->af.r.r1, s->af.r.c1, s->af.on); g_fail++; }
}
static void af_tests (const char *dir)
{
	Book a; book_init (a); book_add_sheet (a, "Fruit");
	const char *F[6] = { "apple", "pear", "Apple", "fig", "", "kiwi" };
	set (a, 0, "A1", "Fruit"); set (a, 0, "B1", "Qty");
	for (int i = 0; i < 6; i++) { char r[16], v[16]; snprintf (r, sizeof r, "A%d", i + 2); if (F[i][0]) set (a, 0, r, F[i]); snprintf (r, sizeof r, "B%d", i + 2); snprintf (v, sizeof v, "%d", i + 1); set (a, 0, r, v); }
	recalc (a);
	Sheet *s = a.sh[0];
	s->af.on = true; s->af.r = Rect { 0, 0, 5, 1 };		// (one row short: grown over the kiwi when applied)
	AfVal *vals; int nv = af_values (a, s, 0, &vals);
	{
		Buf l; for (int i = 0; i < nv; i++) { l.puts (vals[i].t[0] ? vals[i].t : "()"); l.put (' '); free (vals[i].t); }
		free (vals);
		g_checks++; if (strcmp (l.str (), "apple fig kiwi pear () ")) { printf ("FAIL filter values: [%s]\n", l.str ()); g_fail++; }
	}
	af_set (s, 0, "apple\x1F\x1F");					// apples (any case) and the empty cells
	af_set (s, 1, "1\x1F" "3\x1F" "5\x1F" "7\x1F");			// odd quantities
	af_apply (a, s);
	af_expect (a, "3 5 7 ", "filter");
	const char *why = 0;
	char p1[256]; snprintf (p1, sizeof p1, "%s/af.xlsx", dir);
	if (!book_save (a, p1, &why)) { printf ("FAIL af save: %s\n", why); g_fail++; return; }
	Book b; book_init (b);
	if (!book_load (b, p1, &why)) { printf ("FAIL af load: %s\n", why); g_fail++; return; }
	af_expect (b, "3 5 7 ", "filter, .xlsx");
	g_checks++; if (b.sh[0]->af.n != 2) { printf ("FAIL filter, .xlsx: %d columns filtered\n", b.sh[0]->af.n); g_fail++; }
	recalc (b); af_apply (b, b.sh[0]); af_expect (b, "3 5 7 ", "filter, .xlsx, applied again");
	af_clear (b.sh[0]);
	g_checks++; if (b.sh[0]->af.on || b.sh[0]->nrows) { printf ("FAIL filter cleared: on %d, %d rows kept\n", b.sh[0]->af.on, b.sh[0]->nrows); g_fail++; }
	if (system ("which soffice > /dev/null 2>&1") == 0)
	{
		char cmd[512]; snprintf (cmd, sizeof cmd, "cd %s && soffice --headless --convert-to ods --outdir lo4 af.xlsx > /dev/null 2>&1 && soffice --headless --convert-to xlsx:\"Calc MS Excel 2007 XML\" --outdir lo4 lo4/af.ods > /dev/null 2>&1", dir);
		if (system (cmd) == 0)
		{
			char po[256], px[256]; snprintf (po, sizeof po, "%s/lo4/af.ods", dir); snprintf (px, sizeof px, "%s/lo4/af.xlsx", dir);
			Book c; book_init (c);
			if (!book_load (c, po, &why)) { printf ("FAIL af LibreOffice ods load: %s\n", why); g_fail++; }
			else { af_expect (c, "3 5 7 ", "filter, LibreOffice's .ods"); recalc (c); af_apply (c, c.sh[0]); af_expect (c, "3 5 7 ", "filter, LibreOffice's .ods, applied again"); }
			Book d; book_init (d);
			if (!book_load (d, px, &why)) { printf ("FAIL af LibreOffice xlsx load: %s\n", why); g_fail++; }
			else { af_expect (d, "3 5 7 ", "filter, LibreOffice's .xlsx"); recalc (d); af_apply (d, d.sh[0]); af_expect (d, "3 5 7 ", "filter, LibreOffice's .xlsx, applied again"); }
			book_clear (c); book_clear (d);
		}
	}
	book_clear (a); book_clear (b);
}

int main (int argc, char **argv)
{
	const char *dir = argc > 1 ? argv[1] : "/tmp";
	g_now = now_fn; fn_init ();
	Book a; book_init (a);
	book_add_sheet (a, "Budget"); book_add_sheet (a, "Données 2026");
	set (a, 0, "A1", "Item"); set (a, 0, "B1", "Amount"); set (a, 0, "C1", "Share"); set (a, 0, "D1", "Due");
	const char *items[] = { "Rent", "Food & drinks", "Transport", "Leisure <fun>", "Épargne" };
	for (int i = 0; i < 5; i++)
	{
		char r[16], v[32];
		snprintf (r, sizeof r, "A%d", i + 2); set (a, 0, r, items[i]);
		snprintf (r, sizeof r, "B%d", i + 2); snprintf (v, sizeof v, "%d.%02d", 100 * (i + 3) + 7 * i, 25 * i); set (a, 0, r, v);
		snprintf (r, sizeof r, "C%d", i + 2); snprintf (v, sizeof v, "=B%d/$B$7", i + 2); set (a, 0, r, v);
		snprintf (r, sizeof r, "D%d", i + 2); snprintf (v, sizeof v, "%02d/10/2026", i * 5 + 1); set (a, 0, r, v);
	}
	set (a, 0, "A7", "Total"); set (a, 0, "B7", "=SUM(B2:B6)"); set (a, 0, "C7", "=SUM(C2:C6)");
	set (a, 0, "E2", "=IF(B2>400,\"high\",\"low\")"); set (a, 0, "E3", "=TRUE"); set (a, 0, "E4", "=1/0"); set (a, 0, "E5", "=\"a\"\"b\"");
	set (a, 0, "F1", "='Données 2026'!B2*2"); set (a, 0, "F2", "=AVERAGE('Données 2026'!B1:B3)"); set (a, 0, "F3", "12%"); set (a, 0, "F4", "  spaced  ");
	set (a, 0, "G1", "=VLOOKUP(\"Transport\",A2:B6,2,FALSE)"); set (a, 0, "G2", "=TEXT(D2,\"dddd\")"); set (a, 0, "G3", "=ROUND(PMT(0.05/12,120,10000),2)");
	set (a, 1, "A1", "x"); set (a, 1, "B1", "1"); set (a, 1, "B2", "2"); set (a, 1, "B3", "3.5");
	name_set (a, "Total", 0, "=Budget!$B$7"); name_set (a, "Items", 0, "=Budget!$A$2:$A$6");
	set (a, 0, "G4", "=Total*2"); set (a, 0, "G5", "=COUNTA(Items)");
	int fmtMoney = book_fmt (a, "#,##0.00 \"€\"");
	int pct = book_fmt (a, "0.0%");
	int lib = book_font (a, "Liberation Serif");
	style_range (a, a.sh[0], R ("A1", "D1"), bold, 0);
	style_range (a, a.sh[0], R ("A1", "D1"), fill_y, 0);
	style_range (a, a.sh[0], R ("B2", "B7"), money, (const void *) (long) fmtMoney);
	style_range (a, a.sh[0], R ("C2", "C7"), money, (const void *) (long) pct);
	style_range (a, a.sh[0], R ("A2", "D7"), borders, 0);
	style_range (a, a.sh[0], R ("E2", "E2"), italic_red, 0);
	style_range (a, a.sh[0], R ("A9", "D9"), center_wrap, 0);
	style_range (a, a.sh[0], R ("A7", "A7"), big, (const void *) (long) lib);
	set (a, 0, "A9", "A merged title across four columns");
	merge_range (a, a.sh[0], R ("A9", "D9"));
	a.sh[0]->colW[0] = 140; a.sh[0]->colW[1] = 96; cols_changed (a.sh[0]);
	RowInfo *ri = row_add (a.sh[0], 8); ri->fl |= RF_CUSTOM; ri->h = 40; rows_changed (a.sh[0]);
	a.sh[0]->freezeR = 1; a.sh[0]->freezeC = 1;
	Chart *ch = (Chart *) calloc (1, sizeof (Chart));
	ch->type = CH_COLUMN; ch->x = 500; ch->y = 30; ch->w = 360; ch->h = 220; ch->srcSheet = a.sh[0]->id; ch->src = R ("A1", "B6");
	ch->head = true; ch->side = true; ch->legend = LG_RIGHT; ch->grid = true; scpy (ch->title, "Budget", sizeof ch->title);
	a.sh[0]->charts = (Chart **) malloc (sizeof (Chart *)); a.sh[0]->charts[0] = ch; a.sh[0]->ncharts = 1;
	recalc (a);

	char p1[256], p2[256], p3[256], cmd[1024];
	snprintf (p1, sizeof p1, "%s/t1.xlsx", dir);
	const char *why = 0;
	if (!book_save (a, p1, &why)) { printf ("FAIL save: %s\n", why); return 1; }
	Book b; book_init (b);
	if (!book_load (b, p1, &why)) { printf ("FAIL load: %s\n", why); return 1; }
	compare (a, b, "xlsx round trip", true);
	// a second trip gives the same bytes' meaning
	snprintf (p2, sizeof p2, "%s/t2.xlsx", dir);
	book_save (b, p2, &why);
	Book c; book_init (c); book_load (c, p2, &why);
	compare (a, c, "xlsx second trip", true);
	// CSV
	snprintf (p3, sizeof p3, "%s/t3.csv", dir);
	book_save (a, p3, &why);
	Book d; book_init (d); book_load (d, p3, &why);
	{
		char t[256]; shown (d, 0, "B7", t); g_checks++;
		if (strcmp (t, "2,571.60 €")) { printf ("FAIL csv: B7 [%s]\n", t); g_fail++; }
		shown (d, 0, "A3", t); g_checks++; if (strcmp (t, "Food & drinks")) { printf ("FAIL csv: A3 [%s]\n", t); g_fail++; }
	}
	// LibreOffice: our file to .ods and back to .xlsx, both read
	if (system ("which soffice > /dev/null 2>&1") == 0)
	{
		snprintf (cmd, sizeof cmd, "cd %s && soffice --headless --convert-to ods t1.xlsx > /dev/null 2>&1 && soffice --headless --convert-to xlsx:\"Calc MS Excel 2007 XML\" --outdir lo t1.ods > /dev/null 2>&1", dir);
		if (system (cmd) == 0)
		{
			char po[256], px[256]; snprintf (po, sizeof po, "%s/t1.ods", dir); snprintf (px, sizeof px, "%s/lo/t1.xlsx", dir);
			Book e; book_init (e);
			if (!book_load (e, po, &why)) { printf ("FAIL ods load: %s\n", why); g_fail++; }
			else compare (a, e, "LibreOffice .ods", false);
			Book f; book_init (f);
			if (!book_load (f, px, &why)) { printf ("FAIL LibreOffice xlsx load: %s\n", why); g_fail++; }
			else compare (a, f, "LibreOffice .xlsx", false);
			printf ("(LibreOffice: .ods and .xlsx read)\n");
			book_clear (e); book_clear (f);
		}
		else printf ("(LibreOffice conversion failed)\n");
	}
	// the sample workbook (sdcard/docs/cafe-2026.xlsx): read, written, read back the same; LibreOffice's
	// .ods and .xlsx of it read the same (what is shown, the fonts' sizes, the styles)
	if (argc > 2)
	{
		Book s0; book_init (s0);
		if (!book_load (s0, argv[2], &why)) { printf ("FAIL sample load: %s\n", why); g_fail++; }
		else
		{
			char q1[256]; snprintf (q1, sizeof q1, "%s/sample.xlsx", dir);
			book_save (s0, q1, &why);
			Book s1; book_init (s1); book_load (s1, q1, &why);
			compare (s0, s1, "sample round trip", true);
			book_clear (s1);
			if (system ("which soffice > /dev/null 2>&1") == 0)
			{
				snprintf (cmd, sizeof cmd, "cd %s && soffice --headless --convert-to ods sample.xlsx > /dev/null 2>&1 && soffice --headless --convert-to xlsx:\"Calc MS Excel 2007 XML\" --outdir lo2 sample.ods > /dev/null 2>&1", dir);
				if (system (cmd) == 0)
				{
					char po[256], px[256]; snprintf (po, sizeof po, "%s/sample.ods", dir); snprintf (px, sizeof px, "%s/lo2/sample.xlsx", dir);
					Book e; book_init (e);
					if (!book_load (e, po, &why)) { printf ("FAIL sample ods load: %s\n", why); g_fail++; }
					else compare (s0, e, "sample, LibreOffice .ods", false);
					Book f; book_init (f);
					if (!book_load (f, px, &why)) { printf ("FAIL sample LibreOffice xlsx load: %s\n", why); g_fail++; }
					else compare (s0, f, "sample, LibreOffice .xlsx", false);
					book_clear (e); book_clear (f);
				}
			}
		}
		book_clear (s0);
	}
	book_clear (a); book_clear (b); book_clear (c); book_clear (d);
	cf_tests (dir);
	cf_shift_tests ();
	af_tests (dir);
	for (int i = 0; i < CF_SLOTS; i++) cfstat_free (g_cfs[i]);
	for (int i = 0; i < g_fcacheN; i++) { free (g_fcache[i].code); free (g_fcache[i].f); }
	printf ("%d checks, %d failed\n", g_checks, g_fail);
	return g_fail ? 1 : 0;
}
