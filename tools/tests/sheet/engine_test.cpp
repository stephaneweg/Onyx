// engine_test.cpp -- the spreadsheet's engine on the PC: formulas, functions, number formats, typed
// entries (sh tools/tests/run_sheet_test.sh).
#include "Apps/sheet/undo.h"
#include <stdio.h>
using namespace ss;

static Book B;
static int g_fail, g_checks;
static void now_fn (int *y, int *mo, int *d, int *h, int *mi, int *s) { *y = 2026; *mo = 9; *d = 29; *h = 14; *mi = 30; *s = 0; }

static void set (const char *ref, const char *text, int sheet = 0)
{
	int r, c; parse_cell_name (ref, &r, &c);
	const char *why = 0; int where = 0;
	if (!cell_input (B, B.sh[sheet], r, c, text, &why, &where)) { printf ("  cannot read %s: %s (at %d)\n", text, why, where); g_fail++; }
}
static void shown (const char *ref, char *out, int sheet = 0)
{
	int r, c; parse_cell_name (ref, &r, &c);
	Shown s; cell_shown (B, B.sh[sheet], B.sh[sheet]->cells.get (r, c), s, 24);
	strcpy (out, s.text);
}
static void check (const char *ref, const char *expect, int sheet = 0)
{
	recalc (B);
	char t[256]; shown (ref, t, sheet);
	g_checks++;
	if (strcmp (t, expect)) { printf ("FAIL %s: got [%s], expected [%s]\n", ref, t, expect); g_fail++; }
}
static void eq (const char *formula, const char *expect)
{
	set ("Z1000", formula);
	recalc (B);
	char t[256]; shown ("Z1000", t);
	g_checks++;
	if (strcmp (t, expect)) { printf ("FAIL %s: got [%s], expected [%s]\n", formula, t, expect); g_fail++; }
}
static void fmt (const char *code, double v, const char *expect)
{
	Buf o; fmt_number (code, v, o, 0, 11);
	g_checks++;
	if (strcmp (o.str (), expect)) { printf ("FAIL format [%s] %.10g: got [%s], expected [%s]\n", code, v, o.str (), expect); g_fail++; }
}
static void typed (const char *text, const char *kind, double v, const char *wantFmt)
{
	Entry e; input_parse (text, e);
	const char *k = e.kind == K_NUM ? "num" : e.kind == K_STR ? "str" : e.kind == K_BOOL ? "bool" : e.kind == K_ERR ? "err" : "none";
	g_checks++;
	if (strcmp (k, kind) || (e.kind == K_NUM && fabs (e.num - v) > 1e-9) || strcmp (e.fmt, wantFmt))
	{ printf ("FAIL typed [%s]: %s %.10g [%s], expected %s %.10g [%s]\n", text, k, e.num, e.fmt, kind, v, wantFmt); g_fail++; }
}
static void printed (const char *in, const char *expect)
{
	Formula *f = formula_parse (B, in, -1);
	g_checks++;
	if (!f) { printf ("FAIL print [%s]: not read\n", in); g_fail++; return; }
	Buf o; formula_print (B, f, o);
	if (strcmp (o.str (), expect)) { printf ("FAIL print [%s]: got [%s], expected [%s]\n", in, o.str (), expect); g_fail++; }
	formula_free (f);
}

int main ()
{
	g_now = now_fn;
	fn_init ();
	book_init (B);
	book_add_sheet (B, "Sheet1");
	book_add_sheet (B, "Data 2");

	// ---- arithmetic, precedence
	eq ("=1+2*3", "7"); eq ("=(1+2)*3", "9"); eq ("=2^3^2", "64"); eq ("=-2^2", "4"); eq ("=2^-1", "0.5");
	eq ("=10%", "0.1"); eq ("=50%*4", "2"); eq ("=1/0", "#DIV/0!"); eq ("=\"a\"&\"b\"&1", "ab1");
	eq ("=1=1", "TRUE"); eq ("=\"abc\"=\"ABC\"", "TRUE"); eq ("=2<>2", "FALSE"); eq ("=\"b\">\"a\"", "TRUE"); eq ("=1<\"a\"", "TRUE");
	eq ("=\"3\"+4", "7"); eq ("=\"x\"+1", "#VALUE!"); eq ("=TRUE+1", "2"); eq ("=0.1+0.2", "0.3"); eq ("=1/3", "0.3333333333");
	eq ("=10/4", "2.5"); eq ("=123456789*1000", "1.23457E+11"); eq ("=12345678*1000", "12345678000"); eq ("=2^60", "1.15292E+18");
	// ---- references
	set ("A1", "10"); set ("A2", "20"); set ("A3", "30"); set ("A4", "abc"); set ("A5", "TRUE");
	set ("B1", "=A1*2"); check ("B1", "20");
	set ("B2", "=SUM(A1:A5)"); check ("B2", "60");
	set ("B3", "=A1+A4"); check ("B3", "#VALUE!");
	set ("B4", "=COUNT(A1:A5)"); check ("B4", "3");
	set ("B5", "=COUNTA(A1:A5)"); check ("B5", "5");
	set ("B6", "=AVERAGE(A1:A3)"); check ("B6", "20");
	set ("B7", "=SUM(A:A)"); check ("B7", "60");
	set ("B8", "=SUM(A1:A3*2)"); check ("B8", "120");
	set ("B9", "=SUMPRODUCT((A1:A3>15)*A1:A3)"); check ("B9", "50");
	set ("C1", "=C2+1"); set ("C2", "=C1+1"); check ("C1", "#CIRC!"); check ("C2", "#CIRC!");
	set ("C1", "5"); check ("C2", "6");
	// a deep chain (deeper than the recursion allows)
	set ("D1", "1");
	for (int i = 2; i <= 2000; i++) { char r[16], f[32]; snprintf (r, sizeof r, "D%d", i); snprintf (f, sizeof f, "=D%d+1", i - 1); set (r, f); }
	check ("D2000", "2000");
	// ...and its reverse (each cell uses the one below)
	for (int i = 1; i < 2000; i++) { char r[16], f[32]; snprintf (r, sizeof r, "E%d", i); snprintf (f, sizeof f, "=E%d+1", i + 1); set (r, f); }
	set ("E2000", "1"); check ("E1", "2000");
	// other sheets
	set ("A1", "7", 1); set ("F1", "='Data 2'!A1*3"); check ("F1", "21");
	set ("F2", "=SUM('Data 2'!A1:A3)"); check ("F2", "7");
	printed ("SUM('Data 2'!A1:B3)", "=SUM('Data 2'!A1:B3)");
	printed ("sum(a1:b2)+$c$3-D$4", "=SUM(A1:B2)+$C$3-D$4");
	printed ("IF(A1>0 , \"yes\",\"no\")", "=IF(A1>0 , \"yes\",\"no\")");
	printed ("SUM(A:A,1:1)", "=SUM(A:A,1:1)");
	printed ("{1,2;3,4}", "={1,2;3,4}");
	printed ("_xlfn.IFS(A1,1)", "=IFS(A1,1)");
	// ---- math
	eq ("=ROUND(2.675,2)", "2.68"); eq ("=ROUND(-2.5,0)", "-3"); eq ("=ROUND(1234.567,-2)", "1200"); eq ("=ROUNDUP(3.2,0)", "4"); eq ("=ROUNDDOWN(-3.7,0)", "-3");
	eq ("=INT(-3.5)", "-4"); eq ("=TRUNC(-3.5)", "-3"); eq ("=MOD(-3,2)", "1"); eq ("=MOD(3,-2)", "-1"); eq ("=CEILING(2.5,1)", "3"); eq ("=FLOOR(-2.5,-2)", "-2");
	eq ("=CEILING(-2.5,2)", "-2"); eq ("=MROUND(10,3)", "9"); eq ("=SQRT(16)", "4"); eq ("=SQRT(-1)", "#NUM!"); eq ("=POWER(2,10)", "1024"); eq ("=ABS(-5)", "5");
	eq ("=FACT(5)", "120"); eq ("=COMBIN(5,2)", "10"); eq ("=GCD(12,18)", "6"); eq ("=LCM(4,6)", "12"); eq ("=EVEN(3)", "4"); eq ("=ODD(2)", "3");
	eq ("=LOG(100)", "2"); eq ("=LN(EXP(2))", "2"); eq ("=PI()", "3.141592654"); eq ("=DEGREES(PI())", "180"); eq ("=SIGN(-2)", "-1");
	eq ("=PRODUCT(2,3,4)", "24"); eq ("=SUMSQ(3,4)", "25"); eq ("=QUOTIENT(7,2)", "3");
	// ---- statistics
	set ("H1", "3"); set ("H2", "1"); set ("H3", "4"); set ("H4", "1"); set ("H5", "5"); set ("H6", "9");
	eq ("=MEDIAN(H1:H6)", "3.5"); eq ("=MODE(H1:H6)", "1"); eq ("=MAX(H1:H6)", "9"); eq ("=MIN(H1:H6)", "1"); eq ("=LARGE(H1:H6,2)", "5");
	eq ("=SMALL(H1:H6,2)", "1"); eq ("=RANK(4,H1:H6)", "3"); eq ("=STDEV(H1:H6)", "2.994439291"); eq ("=VARP(H1:H6)", "7.472222222");
	eq ("=PERCENTILE(H1:H6,0.25)", "1.5"); eq ("=QUARTILE(H1:H6,3)", "4.75"); eq ("=COUNTIF(H1:H6,\">2\")", "4"); eq ("=COUNTIF(H1:H6,1)", "2");
	eq ("=SUMIF(H1:H6,\">=4\")", "18"); eq ("=AVERAGEIF(H1:H6,\"<4\")", "1.666666667"); eq ("=COUNTBLANK(H1:H8)", "2");
	eq ("=SUMIFS(H1:H6,H1:H6,\">1\",H1:H6,\"<9\")", "12"); eq ("=COUNTIFS(H1:H6,\">1\")", "4"); eq ("=MAXIFS(H1:H6,H1:H6,\"<5\")", "4");
	eq ("=GEOMEAN(2,8)", "4"); eq ("=AVERAGEA(A1:A5)", "12.2");
	set ("I1", "1"); set ("I2", "2"); set ("I3", "3"); set ("J1", "2"); set ("J2", "4"); set ("J3", "6");
	eq ("=CORREL(I1:I3,J1:J3)", "1"); eq ("=SLOPE(J1:J3,I1:I3)", "2"); eq ("=INTERCEPT(J1:J3,I1:I3)", "0"); eq ("=FORECAST(4,J1:J3,I1:I3)", "8");
	eq ("=NORM.S.DIST(0,TRUE)", "0.5"); eq ("=ROUND(NORM.S.INV(0.975),4)", "1.96"); eq ("=ROUND(NORMDIST(1,0,1,TRUE),6)", "0.841345");
	eq ("=SUBTOTAL(9,H1:H6)", "23"); eq ("=SUBTOTAL(1,H1:H6)", "3.833333333");
	// ---- logic
	eq ("=IF(1>2,\"a\",\"b\")", "b"); eq ("=IF(TRUE,5)", "5"); eq ("=IF(FALSE,5)", "FALSE"); eq ("=AND(TRUE,1)", "TRUE"); eq ("=OR(FALSE,0)", "FALSE");
	eq ("=XOR(TRUE,TRUE)", "FALSE"); eq ("=NOT(0)", "TRUE"); eq ("=IFERROR(1/0,\"x\")", "x"); eq ("=IFNA(NA(),2)", "2"); eq ("=IFS(1>2,1,2>1,2)", "2");
	eq ("=SWITCH(2,1,\"one\",2,\"two\")", "two"); eq ("=SWITCH(3,1,\"one\",\"other\")", "other"); eq ("=CHOOSE(2,\"a\",\"b\",\"c\")", "b");
	eq ("=IF(1,,2)", "0");
	eq ("=SUM(IF(H1:H6>2,1,0))", "4");
	// ---- text
	eq ("=LEN(\"héllo\")", "5"); eq ("=LEFT(\"héllo\",2)", "hé"); eq ("=RIGHT(\"hello\",3)", "llo"); eq ("=MID(\"hello\",2,3)", "ell");
	eq ("=UPPER(\"été\")", "ÉTÉ"); eq ("=LOWER(\"ABC\")", "abc"); eq ("=PROPER(\"jean-luc o'neil\")", "Jean-Luc O'Neil"); eq ("=TRIM(\"  a   b  \")", "a b");
	eq ("=SUBSTITUTE(\"a-b-c\",\"-\",\"+\")", "a+b+c"); eq ("=SUBSTITUTE(\"a-b-c\",\"-\",\"+\",2)", "a-b+c"); eq ("=REPLACE(\"abcdef\",2,3,\"X\")", "aXef");
	eq ("=FIND(\"c\",\"abcabc\",4)", "6"); eq ("=SEARCH(\"B?\",\"abcabc\")", "2"); eq ("=SEARCH(\"x\",\"abc\")", "#VALUE!"); eq ("=REPT(\"ab\",3)", "ababab");
	eq ("=CONCATENATE(\"a\",1,TRUE)", "a1TRUE"); eq ("=TEXTJOIN(\", \",TRUE,\"a\",\"\",\"b\")", "a, b"); eq ("=CONCAT(I1:I3)", "123");
	eq ("=EXACT(\"a\",\"A\")", "FALSE"); eq ("=CHAR(65)", "A"); eq ("=CODE(\"A\")", "65"); eq ("=VALUE(\"1,234.5\")", "1234.5"); eq ("=VALUE(\"12%\")", "0.12");
	eq ("=TEXT(1234.5,\"#,##0.00\")", "1,234.50"); eq ("=TEXT(0.256,\"0.0%\")", "25.6%"); eq ("=TEXT(46294,\"dd/mm/yyyy\")", "29/09/2026");
	eq ("=TEXT(46294,\"dddd d mmmm yyyy\")", "Tuesday 29 September 2026"); eq ("=FIXED(1234.567,1)", "1,234.6"); eq ("=DOLLAR(-5)", "($5.00)");
	// ---- lookups
	set ("K1", "apple"); set ("K2", "banana"); set ("K3", "cherry"); set ("L1", "1.5"); set ("L2", "0.5"); set ("L3", "3");
	eq ("=VLOOKUP(\"banana\",K1:L3,2,FALSE)", "0.5"); eq ("=VLOOKUP(\"b*\",K1:L3,2,FALSE)", "0.5"); eq ("=VLOOKUP(\"kiwi\",K1:L3,2,FALSE)", "#N/A");
	eq ("=VLOOKUP(\"blueberry\",K1:L3,2)", "0.5"); eq ("=MATCH(\"cherry\",K1:K3,0)", "3"); eq ("=MATCH(2.5,I1:I3)", "2"); eq ("=INDEX(K1:L3,2,2)", "0.5");
	eq ("=INDEX(K1:K3,3)", "cherry"); eq ("=SUM(INDEX(L1:L3,0,1))", "5"); eq ("=XLOOKUP(\"cherry\",K1:K3,L1:L3)", "3"); eq ("=XLOOKUP(\"x\",K1:K3,L1:L3,\"none\")", "none");
	eq ("=HLOOKUP(2,I1:J3,2,FALSE)", "4"); eq ("=LOOKUP(2.5,I1:I3,J1:J3)", "4"); eq ("=SUM(OFFSET(I1,1,0,2,1))", "5"); eq ("=INDIRECT(\"K\"&2)", "banana");
	eq ("=ROW(K3)", "3"); eq ("=COLUMN(K3)", "11"); eq ("=ROWS(K1:L3)", "3"); eq ("=COLUMNS(K1:L3)", "2"); eq ("=ADDRESS(2,3)", "$C$2"); eq ("=ADDRESS(2,3,4)", "C2");
	// ---- dates
	eq ("=DATE(2026,9,29)", "46294"); eq ("=YEAR(46294)", "2026"); eq ("=MONTH(46294)", "9"); eq ("=DAY(46294)", "29"); eq ("=WEEKDAY(46294)", "3");
	eq ("=WEEKDAY(46294,2)", "2"); eq ("=DATE(2026,13,1)", "46388"); eq ("=DATE(1900,2,29)", "60"); eq ("=DATE(1900,3,1)", "61"); eq ("=DATE(1900,1,1)", "1");
	eq ("=EDATE(DATE(2026,1,31),1)", "46081"); eq ("=EOMONTH(DATE(2026,2,10),0)", "46081"); eq ("=DATEDIF(DATE(2000,5,15),DATE(2026,9,29),\"Y\")", "26");
	eq ("=DATEDIF(DATE(2026,1,31),DATE(2026,3,1),\"M\")", "1"); eq ("=DAYS(DATE(2026,12,25),DATE(2026,9,29))", "87"); eq ("=NETWORKDAYS(DATE(2026,9,28),DATE(2026,10,4))", "5");
	eq ("=WORKDAY(DATE(2026,9,25),1)", "46293"); eq ("=TIME(14,30,0)", "0.6041666667"); eq ("=HOUR(0.604166666666667)", "14"); eq ("=MINUTE(\"14:30\")", "30");
	eq ("=TODAY()", "46294"); eq ("=ISOWEEKNUM(DATE(2026,9,29))", "40"); eq ("=WEEKNUM(DATE(2026,1,1))", "1"); eq ("=DATEVALUE(\"29/09/2026\")", "46294");
	eq ("=YEARFRAC(DATE(2026,1,1),DATE(2026,7,1))", "0.5"); eq ("=DAYS360(DATE(2026,1,31),DATE(2026,3,31))", "60");
	// ---- information
	eq ("=ISBLANK(Z999)", "TRUE"); eq ("=ISNUMBER(A1)", "TRUE"); eq ("=ISTEXT(A4)", "TRUE"); eq ("=ISERROR(1/0)", "TRUE"); eq ("=ISNA(NA())", "TRUE");
	eq ("=ISERR(NA())", "FALSE"); eq ("=TYPE(\"a\")", "2"); eq ("=N(TRUE)", "1"); eq ("=ISEVEN(4)", "TRUE"); eq ("=SHEETS()", "2");
	// ---- finance
	eq ("=ROUND(PMT(0.05/12,360,200000),2)", "-1073.64"); eq ("=ROUND(FV(0.06/12,10,-200,-500,1),2)", "2581.4"); eq ("=ROUND(PV(0.08/12,240,500),2)", "-59777.15");
	eq ("=ROUND(NPER(0.01,-100,1000),4)", "10.5886"); eq ("=ROUND(RATE(12,-100,1000),6)", "0.029229"); eq ("=ROUND(NPV(0.1,-10000,3000,4200,6800),2)", "1188.44");
	set ("M1", "-70000"); set ("M2", "12000"); set ("M3", "15000"); set ("M4", "18000"); set ("M5", "21000"); set ("M6", "26000");
	eq ("=ROUND(IRR(M1:M6),6)", "0.086631"); eq ("=ROUND(IPMT(0.1/12,1,36,8000),2)", "-66.67"); eq ("=ROUND(PPMT(0.1/12,1,24,2000),2)", "-75.62");
	eq ("=SLN(30000,7500,10)", "2250"); eq ("=SYD(30000,7500,10,1)", "4090.909091"); eq ("=DDB(2400,300,10,1)", "480"); eq ("=ROUND(EFFECT(0.0525,4),6)", "0.053543");

	// ---- number formats
	fmt ("0.00", 3.14159, "3.14"); fmt ("#,##0", 1234567.8, "1,234,568"); fmt ("#,##0.00", -1234.5, "-1,234.50"); fmt ("0%", 0.256, "26%");
	fmt ("0.00%", 0.256, "25.60%"); fmt ("0.00E+00", 12345, "1.23E+04"); fmt ("##0.0E+0", 12345, "12.3E+3"); fmt ("# ?/?", 1.25, "1 1/4");
	fmt ("# ?\?/?\?", 3.14159, "3 14/99"); fmt ("?/?", 0.5, "1/2"); fmt ("$#,##0.00;($#,##0.00)", -5, "($5.00)"); fmt ("[Red]0;[Blue]-0", -3, "-3");
	fmt ("0.0,,\"M\"", 12345678, "12.3M"); fmt ("000-000", 12345, "012-345"); fmt ("#.##", 0.5, ".5"); fmt ("0.#", 5, "5."); fmt ("0;-0;\"zero\"", 0, "zero");
	fmt ("dd/mm/yyyy hh:mm", 46294.75, "29/09/2026 18:00"); fmt ("h:mm AM/PM", 0.75, "6:00 PM"); fmt ("[h]:mm", 1.5, "36:00"); fmt ("mmm yy", 46294, "Sep 26");
	fmt ("hh:mm:ss.000", 0.5 + 1.5 / 86400, "12:00:01.500"); fmt ("General", 1234567890123.0, "1.23457E+12"); fmt ("\"Total: \"0", 5, "Total: 5");
	fmt ("#,##0 \"€\"", 1234, "1,234 €"); fmt ("0.00_);(0.00)", 3, "3.00 "); fmt ("[>=100]\"big\";\"small\"", 150, "big"); fmt ("[>=100]\"big\";\"small\"", 5, "small");
	// ---- typed entries
	typed ("42", "num", 42, ""); typed ("-3.5", "num", -3.5, ""); typed ("1,234.5", "num", 1234.5, "#,##0.0"); typed ("3,5", "num", 3.5, "");
	typed ("12%", "num", 0.12, "0%"); typed ("12.5%", "num", 0.125, "0.0%"); typed ("$1,200", "num", 1200, "$#,##0"); typed ("12,50 €", "num", 12.5, "#,##0.00 \"€\"");
	typed ("1e3", "num", 1000, "0.00E+00"); typed ("(42)", "num", -42, ""); typed ("29/09/2026", "num", 46294, "dd/mm/yyyy"); typed ("2026-09-29", "num", 46294, "yyyy-mm-dd");
	typed ("29 Sep 2026", "num", 46294, "d mmm yyyy"); typed ("Sep 29, 2026", "num", 46294, "mmm d, yyyy"); typed ("14:30", "num", 14.5 / 24, "hh:mm");
	typed ("2:30 PM", "num", 14.5 / 24, "h:mm AM/PM"); typed ("29/09/2026 14:30", "num", 46294 + 14.5 / 24, "dd/mm/yyyy hh:mm"); typed ("true", "bool", 1, "");
	typed ("#N/A", "err", 0, ""); typed ("hello", "str", 0, ""); typed ("'123", "str", 0, ""); typed ("1.2.3", "str", 0, ""); typed ("12 34", "str", 0, "");
	typed ("1 234", "num", 1234, "#,##0"); typed ("728.100", "num", 728.1, ""); typed ("1.234.567", "num", 1234567, "#,##0");
	typed ("1.234,5", "num", 1234.5, "#,##0.0"); typed ("1,234,567.25", "num", 1234567.25, "#,##0.00");

	// ---- operations: rows / columns inserted and deleted, copies, fills, sorts, undo
	{
		Sheet *s = B.sh[0];
		auto R = [] (const char *a, const char *z) { Rect r; parse_cell_name (a, &r.r0, &r.c0); parse_cell_name (z, &r.r1, &r.c1); return r; };
		auto form = [&] (const char *ref, const char *want) {
			int r, c; parse_cell_name (ref, &r, &c);
			Cell *x = s->cells.get (r, c); Buf o; if (x) cell_edit_text (B, s, x, o);
			g_checks++;
			if (strcmp (o.str (), want)) { printf ("FAIL %s: formula [%s], expected [%s]\n", ref, o.str (), want); g_fail++; }
		};
		set ("P1", "1"); set ("P2", "2"); set ("P3", "3"); set ("Q1", "=SUM(P1:P3)"); set ("Q2", "=P3*2"); set ("Q3", "='Data 2'!A1+P2");
		undo_sheet (B, s);
		insert_rows (B, s, 1, 2);					// (two rows above row 2)
		form ("Q1", "=SUM(P1:P5)"); form ("Q4", "=P5*2"); form ("Q5", "='Data 2'!A1+P4"); check ("Q1", "6");
		delete_rows (B, s, 1, 2);
		form ("Q1", "=SUM(P1:P3)"); form ("Q2", "=P3*2"); check ("Q2", "6");
		delete_rows (B, s, 2, 1);					// (P3 deleted: Q2 pointed at it)
		form ("Q2", "=#REF!*2"); check ("Q2", "#REF!"); form ("Q1", "=SUM(P1:P2)");
		undo_step (B, false); recalc (B);
		form ("Q2", "=P3*2"); check ("Q1", "6");
		insert_cols (B, s, 15, 1);					// (a column before P)
		form ("R1", "=SUM(Q1:Q3)"); delete_cols (B, s, 15, 1); form ("Q1", "=SUM(P1:P3)");
		// a copy: the relative parts move
		copy_range (B, s, R ("Q2", "Q2"), false);
		paste_range (B, s, 5, 17, R ("R6", "R6"));			// R6
		form ("R6", "=Q7*2");
		set ("S1", "=$P$1+P1"); copy_range (B, s, R ("S1", "S1"), false); paste_range (B, s, 1, 19, R ("T2", "T2")); form ("T2", "=$P$1+Q2");
		// a cut: the references to the cells moved follow
		set ("U1", "5"); set ("U2", "=U1*10"); copy_range (B, s, R ("U1", "U1"), true); paste_cut (B, s, 0, 21); form ("U2", "=V1*10"); check ("U2", "50");
		// fills: numbers, dates, words, "Item 1", formulas
		set ("W1", "1"); set ("W2", "3"); fill_range (B, s, R ("W1", "W2"), R ("W1", "W5")); check ("W5", "9");
		set ("X1", "Monday"); fill_range (B, s, R ("X1", "X1"), R ("X1", "X3")); check ("X3", "Wednesday");
		set ("Y1", "Item 9"); fill_range (B, s, R ("Y1", "Y1"), R ("Y1", "Y3")); check ("Y3", "Item 11");
		set ("AA1", "31/01/2026"); set ("AA2", "28/02/2026"); fill_range (B, s, R ("AA1", "AA1"), R ("AA1", "AA3")); check ("AA3", "02/02/2026");
		set ("AB1", "15/01/2026"); set ("AB2", "15/02/2026"); fill_range (B, s, R ("AB1", "AB2"), R ("AB1", "AB4")); check ("AB4", "15/04/2026");
		set ("AC1", "=W1*2"); fill_range (B, s, R ("AC1", "AC1"), R ("AC1", "AC3")); form ("AC3", "=W3*2"); check ("AC3", "10");
		set ("AD1", "7"); fill_range (B, s, R ("AD1", "AD1"), R ("AD1", "AD3")); check ("AD3", "7");
		// a sort (header kept; texts after numbers; empties last)
		set ("AF1", "Name"); set ("AF2", "pear"); set ("AF3", "Apple"); set ("AF4", "fig"); set ("AF5", "");
		set ("AG1", "Qty"); set ("AG2", "3"); set ("AG3", "=AG4+1"); set ("AG4", "5"); set ("AG5", "1");
		SortKey k = { 31, false };
		sort_range (B, s, R ("AF1", "AG5"), &k, 1, true);
		check ("AF2", "Apple"); check ("AF3", "fig"); check ("AF4", "pear"); check ("AF5", ""); form ("AG2", "=AG3+1"); check ("AG2", "6");
		// undo / redo of typing
		undo_rect (B, s, R ("AH1", "AH1")); set ("AH1", "hello"); check ("AH1", "hello");
		undo_step (B, false); check ("AH1", ""); undo_step (B, true); check ("AH1", "hello");
		// defined names: a range, a value; a sheet's own wins over the book's; rows inserted above: the name's
		// range follows; the book's undo brings the names back
		set ("AJ1", "10"); set ("AJ2", "20"); set ("AJ3", "30");
		g_checks++; if (!name_set (B, "Qty", 0, "=Sheet1!$AJ$1:$AJ$3") || !name_set (B, "Rate", 0, "0.5")) { printf ("FAIL names: not set\n"); g_fail++; }
		set ("AK1", "=SUM(Qty)*Rate"); check ("AK1", "30");
		set ("AK2", "=Nope+1"); check ("AK2", "#NAME?");
		set ("AK3", "=INDEX(Qty,2)"); check ("AK3", "20");
		set ("AK4", "=Qty"); check ("AK4", "#VALUE!");			// (implicit intersection: row 4 is not in the range, as in Excel)
		name_set (B, "Rate", s->id, "2"); check ("AK1", "120");
		name_set (B, "Loop", 0, "=Loop+1"); set ("AK5", "=Loop"); check ("AK5", "#REF!");
		g_checks++; if (name_set (B, "A1", 0, "1") || name_set (B, "TRUE", 0, "1") || name_set (B, "1x", 0, "1") || !name_set (B, "Tax.Rate_2", 0, "=0.2")) { printf ("FAIL names: validity\n"); g_fail++; }
		undo_book (B);
		insert_rows (B, s, 0, 2);
		check ("AK3", "120"); check ("AK5", "20");			// (AK1 -> AK3, AK3 -> AK5; Qty now AJ3:AJ5)
		{ Buf o; formula_print (B, B.names[name_index (B, "Qty", 0)].f, o); g_checks++; if (strcmp (o.str (), "=Sheet1!$AJ$3:$AJ$5")) { printf ("FAIL name shifted: [%s]\n", o.str ()); g_fail++; } }
		undo_step (B, false); recalc (B);
		s = B.sh[0];						// (the book's undo made its sheets anew)
		check ("AK3", "20"); check ("AK1", "120");
		{ Buf o; formula_print (B, B.names[name_index (B, "Qty", 0)].f, o); g_checks++; if (strcmp (o.str (), "=Sheet1!$AJ$1:$AJ$3")) { printf ("FAIL name undone: [%s]\n", o.str ()); g_fail++; } }
		// sheets: renamed (formulas keep them), deleted (their references: #REF!)
		scpy (B.sh[1]->name, "Stock", 64); form ("Q3", "=Stock!A1+P2");
		delete_sheet (B, 1); form ("Q3", "=#REF!+P2"); check ("Q3", "#REF!");
	}

	printf ("%d checks, %d failed\n", g_checks, g_fail);
	return g_fail ? 1 : 0;
}
