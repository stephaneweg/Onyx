// make_sample.cpp -- the spreadsheet's sample workbook (sdcard/docs/cafe-2026.xlsx): a café's sales by
// month and product (formats, colours, borders, merged titles, a column chart), their summary (lookups,
// shares, a pie chart) and the loan of its espresso machine (PMT, IPMT, PPMT; an area chart; frozen
// panes). Built by the spreadsheet's own engine -- what the screenshots and the user guide show.
//
//   g++ -std=gnu++17 -I user tools/tests/sheet/make_sample.cpp -lm -o /tmp/make_sample
//   /tmp/make_sample sdcard/docs/cafe-2026.xlsx
//
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "img/stb_image.h"
#include "Apps/sheet/ods.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
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

static Book B;
static void now_fn (int *y, int *mo, int *d, int *h, int *mi, int *s) { *y = 2026; *mo = 9; *d = 29; *h = 10; *mi = 0; *s = 0; }
static void set (int sh, const char *ref, const char *text)
{
	int r, c; parse_cell_name (ref, &r, &c);
	const char *why = 0;
	if (!cell_input (B, B.sh[sh], r, c, text, &why)) { printf ("cannot set %s: %s\n", text, why ? why : "?"); exit (1); }
}
static void setf (int sh, const char *ref, const char *fmt, ...) __attribute__ ((format (printf, 3, 4)));
static void setf (int sh, const char *ref, const char *fmt, ...)
{
	char t[256]; va_list a; va_start (a, fmt); vsnprintf (t, sizeof t, fmt, a); va_end (a);
	set (sh, ref, t);
}
static Rect R (const char *a, const char *z) { Rect r; parse_cell_name (a, &r.r0, &r.c0); parse_cell_name (z, &r.r1, &r.c1); return r; }
static Rect R (const char *a) { return R (a, a); }

// the styles, one change each (a range: style_range)
struct Look { int font, size, bold, italic, ha, va, wrap, fmt; unsigned color, fill; int bs[4]; unsigned bc; };
static void look_fn (Style &s, const void *a)
{
	const Look *l = (const Look *) a;
	if (l->font >= 0) s.font = (unsigned short) l->font;
	if (l->size) s.size = (unsigned short) l->size;
	if (l->bold >= 0) s.bold = (unsigned char) l->bold;
	if (l->italic >= 0) s.italic = (unsigned char) l->italic;
	if (l->ha >= 0) s.ha = (unsigned char) l->ha;
	if (l->va >= 0) s.va = (unsigned char) l->va;
	if (l->wrap >= 0) s.wrap = (unsigned char) l->wrap;
	if (l->fmt >= 0) s.fmt = (unsigned short) l->fmt;
	if (l->color != 1) s.color = l->color;
	if (l->fill != 1) s.fill = l->fill;
	for (int k = 0; k < 4; k++) if (l->bs[k] >= 0) { s.bs[k] = (unsigned char) l->bs[k]; s.bc[k] = l->bc; }
}
static Look none () { Look l; l.font = l.bold = l.italic = l.ha = l.va = l.wrap = l.fmt = -1; l.size = 0; l.color = l.fill = 1; for (int k = 0; k < 4; k++) l.bs[k] = -1; l.bc = 0; return l; }
static void apply (int sh, Rect r, const Look &l) { style_range (B, B.sh[sh], r, look_fn, &l); }
static void fmt (int sh, Rect r, const char *code) { Look l = none (); l.fmt = book_fmt (B, code); apply (sh, r, l); }
static void bold (int sh, Rect r) { Look l = none (); l.bold = 1; apply (sh, r, l); }
static void fill (int sh, Rect r, unsigned c) { Look l = none (); l.fill = c; apply (sh, r, l); }
static void border (int sh, Rect r, int side, int bs, unsigned c)
{
	Look l = none (); l.bs[side] = bs; l.bc = c; apply (sh, r, l);
}
static void width (int sh, int c, int px) { B.sh[sh]->colW[c] = (unsigned short) px; cols_changed (B.sh[sh]); }
static void height (int sh, int r, int px) { RowInfo *ri = row_add (B.sh[sh], r); ri->h = (unsigned short) px; ri->fl |= RF_CUSTOM; rows_changed (B.sh[sh]); }
static void title (int sh, Rect r, const char *text, int size, unsigned color)
{
	char a[16]; cell_name (r.r0, r.c0, a); set (sh, a, text);
	Look l = none (); l.size = size; l.bold = 1; l.color = color; l.va = VA_CENTER; apply (sh, r, l);
	if (r.c1 > r.c0) merge_range (B, B.sh[sh], r);
}
static void header (int sh, Rect r)		// white bold on teal, centred, a darker line below
{
	Look l = none (); l.bold = 1; l.color = 0xFFFFFF; l.fill = 0x2F7F8F; l.ha = HA_CENTER; l.va = VA_CENTER;
	l.bs[B_BOTTOM] = BS_MEDIUM; l.bc = 0x1D5560;
	apply (sh, r, l);
	height (sh, r.r0, 24);
}
static Chart *chart (int sh, int type, Rect src, int x, int y, int w, int h, const char *t, int legend)
{
	Sheet *s = B.sh[sh];
	Chart *c = (Chart *) calloc (1, sizeof (Chart));
	c->type = type; c->srcSheet = s->id; c->src = src; c->head = true; c->side = true; c->grid = type != CH_PIE;
	c->legend = legend; c->x = x; c->y = y; c->w = w; c->h = h; scpy (c->title, t, sizeof c->title);
	s->charts = (Chart **) realloc (s->charts, (s->ncharts + 1) * sizeof (Chart *));
	s->charts[s->ncharts++] = c;
	return c;
}

int main (int argc, char **argv)
{
	const char *out = argc > 1 ? argv[1] : "sdcard/docs/cafe-2026.xlsx";
	g_now = now_fn; fn_init ();
	book_init (B);
	book_add_sheet (B, "Sales"); book_add_sheet (B, "Summary"); book_add_sheet (B, "Loan");
	const unsigned INK = 0x1D5560, GREY = 0x6B6B6B, ZEBRA = 0xEEF5F6, LINE = 0x9DBFC6;
	const char *MONEY = "#,##0.00 \xE2\x82\xAC";			// (the euro sign, UTF-8)

	// ---- Sales: the takings of every month by product ----------------------------------------------
	title (0, R ("A1", "F1"), "Onyx Caf\xC3\xA9 \xE2\x80\x94 Sales 2026", 160, INK);
	height (0, 0, 30);
	set (0, "A2", "Takings by product and month, in euros");
	{ Look l = none (); l.italic = 1; l.color = GREY; apply (0, R ("A2", "F2"), l); merge_range (B, B.sh[0], R ("A2", "F2")); }
	const char *H[6] = { "Month", "Coffee", "Tea", "Pastries", "Total", "Change" };
	for (int c = 0; c < 6; c++) { char a[16]; cell_name (3, c, a); set (0, a, H[c]); }
	header (0, R ("A4", "F4"));
	static const double coffee[12] = { 3120.5, 2980.25, 3410, 3562.75, 3890.4, 4210, 4480.6, 3950.2, 3720, 3641.5, 3380.9, 4120.3 };
	static const double tea[12] = { 1240.2, 1310, 1150.5, 980.75, 870, 760.3, 690.1, 720, 910.45, 1120, 1290.8, 1420.6 };
	static const double pastry[12] = { 2050, 1920.4, 2180.7, 2240, 2360.2, 2410.9, 2520.5, 2130, 2290.6, 2310, 2240.35, 2870.25 };
	for (int m = 0; m < 12; m++)
	{
		int r = 5 + m;
		char a[16];
		snprintf (a, sizeof a, "A%d", r); setf (0, a, "=DATE(2026,%d,1)", m + 1);
		snprintf (a, sizeof a, "B%d", r); setf (0, a, "%.2f", coffee[m]);
		snprintf (a, sizeof a, "C%d", r); setf (0, a, "%.2f", tea[m]);
		snprintf (a, sizeof a, "D%d", r); setf (0, a, "%.2f", pastry[m]);
		snprintf (a, sizeof a, "E%d", r); setf (0, a, "=SUM(B%d:D%d)", r, r);
		if (m) { snprintf (a, sizeof a, "F%d", r); setf (0, a, "=E%d/E%d-1", r, r - 1); }
	}
	for (int m = 1; m < 12; m += 2) { char a[16], z[8]; snprintf (a, sizeof a, "A%d", 5 + m); snprintf (z, sizeof z, "F%d", 5 + m); fill (0, R (a, z), ZEBRA); }
	fmt (0, R ("A5", "A16"), "mmmm");
	fmt (0, R ("B5", "E19"), MONEY);
	fmt (0, R ("F5", "F16"), "+0.0%;[Red]-0.0%;0.0%");
	bold (0, R ("E5", "E16"));
	border (0, R ("A16", "F16"), B_BOTTOM, BS_THIN, LINE);
	set (0, "A17", "Total"); set (0, "B17", "=SUM(B5:B16)"); set (0, "C17", "=SUM(C5:C16)"); set (0, "D17", "=SUM(D5:D16)"); set (0, "E17", "=SUM(E5:E16)");
	set (0, "F17", "=E16/E5-1"); fmt (0, R ("F17"), "+0.0%;[Red]-0.0%;0.0%");
	bold (0, R ("A17", "F17"));
	border (0, R ("A17", "F17"), B_BOTTOM, BS_DOUBLE, INK);
	set (0, "A18", "Average"); set (0, "B18", "=AVERAGE(B5:B16)"); set (0, "C18", "=AVERAGE(C5:C16)"); set (0, "D18", "=AVERAGE(D5:D16)"); set (0, "E18", "=AVERAGE(E5:E16)");
	{ Look l = none (); l.italic = 1; l.color = GREY; apply (0, R ("A18", "E18"), l); }
	set (0, "A19", "Best month"); set (0, "B19", "=INDEX(A5:A16,MATCH(MAX(E5:E16),E5:E16,0))"); fmt (0, R ("B19"), "mmmm");
	set (0, "C19", "=MAX(E5:E16)");
	{ Look l = none (); l.ha = HA_LEFT; apply (0, R ("B19"), l); }
	width (0, 0, 98); width (0, 1, 90); width (0, 2, 84); width (0, 3, 90); width (0, 4, 96); width (0, 5, 66);
	chart (0, CH_COLUMN, R ("A4", "D16"), col_x (B.sh[0], 6) + 16, (int) row_y (B.sh[0], 3), 452, 316, "Takings by month", LG_BOTTOM);

	// ---- Summary: the products' shares, the months' figures -----------------------------------------
	title (1, R ("A1", "C1"), "Summary of the year", 140, INK);
	height (1, 0, 28);
	set (1, "A3", "Product"); set (1, "B3", "Takings"); set (1, "C3", "Share");
	header (1, R ("A3", "C3"));
	set (1, "A4", "Coffee"); set (1, "B4", "=Sales!B17");
	set (1, "A5", "Tea"); set (1, "B5", "=Sales!C17");
	set (1, "A6", "Pastries"); set (1, "B6", "=Sales!D17");
	set (1, "A7", "Total"); set (1, "B7", "=SUM(B4:B6)"); set (1, "C7", "=SUM(C4:C6)");
	for (int r = 4; r <= 6; r++) { char a[16]; snprintf (a, sizeof a, "C%d", r); setf (1, a, "=B%d/$B$7", r); }
	fmt (1, R ("B4", "B7"), MONEY); fmt (1, R ("C4", "C7"), "0.0%");
	fill (1, R ("A5", "C5"), ZEBRA);
	bold (1, R ("A7", "C7")); border (1, R ("A7", "C7"), B_TOP, BS_THIN, INK);
	set (1, "A9", "Busiest month"); set (1, "B9", "=INDEX(Sales!A5:A16,MATCH(MAX(Sales!E5:E16),Sales!E5:E16,0))");
	set (1, "A10", "Quietest month"); set (1, "B10", "=INDEX(Sales!A5:A16,MATCH(MIN(Sales!E5:E16),Sales!E5:E16,0))");
	fmt (1, R ("B9", "B10"), "mmmm");
	set (1, "A11", "Months over 7,500 \xE2\x82\xAC"); set (1, "B11", "=COUNTIF(Sales!E5:E16,\">7500\")");
	set (1, "A12", "Summer's share"); set (1, "B12", "=SUMPRODUCT((MONTH(Sales!A5:A16)>=6)*(MONTH(Sales!A5:A16)<=8)*Sales!E5:E16)/B7");
	fmt (1, R ("B12"), "0.0%");
	set (1, "A13", "Coffee, trend a month"); set (1, "B13", "=SLOPE(Sales!B5:B16,MONTH(Sales!A5:A16))");
	fmt (1, R ("B13"), "+#,##0.00 \xE2\x82\xAC;-#,##0.00 \xE2\x82\xAC");
	set (1, "A14", "Tea: best / worst"); set (1, "B14", "=TEXT(MAX(Sales!C5:C16),\"#,##0\")&\" / \"&TEXT(MIN(Sales!C5:C16),\"#,##0\")");
	{ Look l = none (); l.ha = HA_RIGHT; apply (1, R ("B9", "B14"), l); }
	{ Look l = none (); l.color = GREY; apply (1, R ("A9", "A14"), l); }
	width (1, 0, 150); width (1, 1, 110); width (1, 2, 70);
	chart (1, CH_PIE, R ("A3", "B6"), col_x (B.sh[1], 3) + 24, (int) row_y (B.sh[1], 2), 380, 280, "Share of the takings", LG_RIGHT);

	// ---- Loan: the espresso machine, paid back in 36 months -----------------------------------------
	title (2, R ("A1", "E1"), "Espresso machine loan", 140, INK);
	height (2, 0, 28);
	set (2, "A3", "Amount"); set (2, "B3", "12000");
	set (2, "A4", "Yearly rate"); set (2, "B4", "4.5%");
	set (2, "A5", "Months"); set (2, "B5", "36");
	set (2, "A6", "Monthly payment"); set (2, "B6", "=PMT(B4/12,B5,-B3)");
	set (2, "A7", "Total interest"); set (2, "B7", "=B6*B5-B3");
	fmt (2, R ("B3"), MONEY); fmt (2, R ("B4"), "0.00%"); fmt (2, R ("B6", "B7"), MONEY);
	bold (2, R ("A6", "B6"));
	fill (2, R ("B3", "B5"), 0xFFF2CC);				// (the inputs: yellow, as the custom goes)
	{ Look l = none (); l.bs[B_LEFT] = l.bs[B_RIGHT] = l.bs[B_TOP] = l.bs[B_BOTTOM] = BS_THIN; l.bc = 0xBF9000; apply (2, R ("B3", "B5"), l); }
	const char *LH[5] = { "Month", "Payment", "Interest", "Principal", "Balance" };
	for (int c = 0; c < 5; c++) { char a[16]; cell_name (8, c, a); set (2, a, LH[c]); }
	header (2, R ("A9", "E9"));
	for (int k = 1; k <= 36; k++)
	{
		int r = 9 + k; char a[16];
		snprintf (a, sizeof a, "A%d", r); setf (2, a, "%d", k);
		snprintf (a, sizeof a, "B%d", r); set (2, a, "=$B$6");
		snprintf (a, sizeof a, "C%d", r); setf (2, a, "=IPMT($B$4/12,A%d,$B$5,-$B$3)", r);
		snprintf (a, sizeof a, "D%d", r); setf (2, a, "=PPMT($B$4/12,A%d,$B$5,-$B$3)", r);
		snprintf (a, sizeof a, "E%d", r); if (k == 1) setf (2, a, "=$B$3-D%d", r); else setf (2, a, "=E%d-D%d", r - 1, r);
		if (!(k & 1)) { char z[16]; snprintf (a, sizeof a, "A%d", r); snprintf (z, sizeof z, "E%d", r); fill (2, R (a, z), ZEBRA); }
	}
	fmt (2, R ("B10", "E45"), MONEY);
	{ Look l = none (); l.ha = HA_CENTER; apply (2, R ("A10", "A45"), l); }
	width (2, 0, 120); width (2, 1, 96); width (2, 2, 90); width (2, 3, 90); width (2, 4, 100);
	B.sh[2]->freezeR = 9; B.sh[2]->topR = 9;
	{
		Chart *c = chart (2, CH_AREA, R ("E9", "E45"), col_x (B.sh[2], 5) + 20, (int) row_y (B.sh[2], 2), 400, 250, "What is left to pay", LG_NONE);
		c->side = false;
	}

	recalc (B);
	B.active = 0;
	const char *why = 0;
	if (!book_save (B, out, &why)) { printf ("cannot save %s: %s\n", out, why ? why : "?"); return 1; }
	// read back: what the app will show
	Book c; book_init (c);
	if (!book_load (c, out, &why)) { printf ("cannot read %s back: %s\n", out, why ? why : "?"); return 1; }
	const char *probe[][2] = { { "Sales", "E17" }, { "Sales", "B19" }, { "Summary", "C4" }, { "Summary", "B12" }, { "Summary", "B13" }, { "Summary", "B14" }, { "Loan", "B6" }, { "Loan", "E45" } };
	for (auto &p : probe)
	{
		int si = book_sheet_index (c, p[0]); int r, cc; parse_cell_name (p[1], &r, &cc);
		Shown sh; cell_shown (c, c.sh[si], c.sh[si]->cells.get (r, cc), sh, 30);
		printf ("%s!%s = %s\n", p[0], p[1], sh.text);
	}
	printf ("wrote %s\n", out);
	book_clear (c); book_clear (B);
	return 0;
}
