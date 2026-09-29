// model_test.cpp -- host test of Cardfile's document (user/Apps/cardfile/model.h): the values read as
// typed (numbers, dates, colours, yes / no), the comparisons, the file written and read back, a file
// edited by hand, a type changed, CSV read with its types guessed, the order (search, sort).
// Run: sh tools/tests/run_cardfile_test.sh
#include <stdio.h>
#include <string.h>
#define _kapi_h						// (the one kapi call the model makes: today's date)
static int kapi_get_datetime (int *y, int *mo, int *d, int *h, int *mi, int *s)
{ *y = 2026; *mo = 9; *d = 29; if (h) *h = 12; if (mi) *mi = 0; if (s) *s = 0; return 1; }
#include "Apps/cardfile/model.h"

using namespace cf;
static int fails, checks;
static void ok (bool c, const char *what)
{
	checks++;
	if (!c) { fails++; printf ("FAIL %s\n", what); }
}
static void num (const char *s, int prec, bool good, long long want, bool exact = true)
{
	long long v = 0; bool ex = true;
	bool r = parse_num (s, prec, &v, &ex);
	char w[160]; snprintf (w, sizeof w, "parse_num (\"%s\", %d) -> %d %lld exact %d", s, prec, r, v, ex);
	ok (r == good && (!good || (v == want && ex == exact)), w);
}
static void date (const char *s, bool good, const char *iso = "")
{
	int y = 0, m = 0, d = 0;
	bool r = parse_date (s, &y, &m, &d);
	char o[16] = ""; if (r) iso_date (y, m, d, o);
	char w[160]; snprintf (w, sizeof w, "parse_date (\"%s\") -> %d %s", s, r, o);
	ok (r == good && (!good || !strcmp (o, iso)), w);
}
static const char *val (const Doc &d, int r, const char *col) { int k = find_column (d, col); return k >= 0 ? d.r[r][k] : "?"; }

int main ()
{
	// ---- numbers
	num ("12", 0, true, 12); num ("-7", 0, true, -7); num ("+3", 0, true, 3);
	num ("1 234,5", 2, true, 123450); num ("1.234,56", 2, true, 123456); num ("1,234.56", 2, true, 123456);
	num ("1'000'000", 0, true, 1000000); num ("12.345", 2, true, 1235, false); num ("12.344", 2, true, 1234, false);
	num ("0.5", 0, true, 1, false); num ("-2.5", 0, true, -3, false); num ("12.50", 2, true, 1250);
	num ("abc", 0, false, 0); num ("", 0, false, 0); num ("1e5", 0, false, 0); num ("12 kg", 0, false, 0);
	num ("1.2.3", 1, true, 1230); num ("1,5", 1, true, 15);
	char b[40]; fmt_num (-123450, 2, b); ok (!strcmp (b, "-1234.50"), "fmt_num -1234.50");
	fmt_num (5, 3, b); ok (!strcmp (b, "0.005"), "fmt_num 0.005");
	fmt_num (0, 0, b); ok (!strcmp (b, "0"), "fmt_num 0");
	// ---- dates
	date ("29/09/2026", true, "2026-09-29"); date ("29.9.26", true, "2026-09-29"); date ("2026-09-29", true, "2026-09-29");
	date ("1/2/99", true, "1999-02-01"); date ("29/9", true, "2026-09-29"); date ("14032024", true, "2024-03-14");
	date ("29/02/2024", true, "2024-02-29"); date ("29/02/2023", false); date ("31/04/2020", false); date ("12/13/2020", false);
	date ("abc", false); date ("", false); date ("1/2/3/4", false);
	// ---- colours, yes / no
	unsigned c = 0;
	ok (parse_color ("#36C", &c) && c == 0x3366CC, "colour #36C");
	ok (parse_color ("0x123456", &c) && c == 0x123456, "colour 0x123456");
	ok (parse_color (" Navy ", &c) && c == 0x000080, "colour navy");
	ok (!parse_color ("#GGGGGG", &c) && !parse_color ("36C", &c), "colour: not ones");
	fmt_color (0xE67E22, b); ok (!strcmp (b, "#E67E22"), "fmt_color");
	ok (parse_bool ("Yes") == 1 && parse_bool ("oui") == 1 && parse_bool ("x") == 1, "yes words");
	ok (parse_bool ("") == 0 && parse_bool ("No") == 0 && parse_bool ("false") == 0, "no words");
	ok (parse_bool ("maybe") == -1, "neither");
	// ---- comparisons (case, accents, numbers in the text)
	ok (ci_cmp ("Item 9", "Item 10") < 0, "natural order");
	ok (ci_cmp ("\xC9mile", "emile") == 0, "accents folded");
	ok (ci_cmp ("abc", "ABD") < 0, "case folded");
	ok (ci_has ("Les Mis\xE9rables", "miser", 5), "search: accents");
	char col[COL_MAX]; make_column ("Date de naissance (\xE9t\xE9)", col, sizeof col);
	ok (!strcmp (col, "date_de_naissance_ete"), "make_column");

	// ---- a document written, read back, written again: the same bytes
	Doc d; doc_init (d); doc_new (d);
	scpy (d.title, "Test", sizeof d.title);
	Field f;
	field_init (f, "Price", "price", FT_DEC); f.prec = 2; doc_insert_field (d, -1, f);
	field_init (f, "Kind", "kind", FT_CHOICE); choice_add (f, "Small"); choice_add (f, "Large"); doc_insert_field (d, -1, f);
	field_init (f, "Name", "name", FT_TEXT); ok (doc_insert_field (d, -1, f) == 4 && seq (d.f[4].column, "name_2"), "unique column");
	int r = doc_add_record (d, -1);
	value_set (d.r[r][0], sdup ("[draft] a \\ c"));
	value_set (d.r[r][1], sdup ("line 1\nline 2"));
	value_set (d.r[r][2], value_new (d.f[2], "1 234,5"));
	value_set (d.r[r][3], value_new (d.f[3], "large"));
	ok (seq (d.r[r][2], "1234.50") && seq (d.r[r][3], "Large"), "values made");
	r = doc_add_record (d, -1);
	value_set (d.r[r][0], sdup ("second"));
	Out o1; doc_write (d, o1);
	Doc e; doc_init (e);
	const char *why = 0;
	ok (doc_read (e, o1.b, o1.n, &why), "read back");
	Out o2; doc_write (e, o2);
	ok (o1.n == o2.n && !memcmp (o1.b, o2.b, o1.n), "the same bytes after a round trip");
	ok (e.nr == 2 && seq (e.r[0][0], "[draft] a \\ c") && seq (e.r[0][1], "line 1\nline 2"), "escapes");
	ok (strstr (o1.b, "\n\\[draft] a \\\\ c\tline 1\\nline 2\t") != 0, "escaped in the file");
	const char *tab = "[field]\nlabel = A\n[records]\na\nx\\ty\n";
	ok (doc_read (e, tab, (int) strlen (tab), &why) && seq (e.r[0][0], "x y"), "a tab read in a text: a space");

	// ---- a file edited by hand: CRLF, keys in any case, columns in another order, one unknown, one
	// missing, a value not of its type, a choice not in the list, no trailing line break
	const char *hand =
		"; my notes\r\n[Form]\r\nTITLE = Hand\r\nsort = qty\r\norder = descending\r\n"
		"[field]\r\nname = Quantity\r\ncolumn = qty\r\ntype = integer\r\n"
		"[field]\r\nlabel = When\r\ntype = date\r\n"
		"[field]\r\nlabel = Size\r\ntype = choice\r\nchoices = S | M | L\r\n"
		"[records]\r\nwhen\tjunk\tqty\tsize\r\n2026-01-02\tx\t5\tM\r\nnot a date\ty\tlots\tXL\r\n\t\t12";
	ok (doc_read (e, hand, (int) strlen (hand), &why), "hand-made file read");
	ok (e.nf == 3 && seq (e.title, "Hand") && seq (e.f[1].column, "when") && e.f[2].nch == 3, "its fields");
	ok (e.sort == 0 && e.sortDesc, "its sort");
	ok (e.nr == 3 && seq (val (e, 0, "qty"), "5") && seq (val (e, 0, "when"), "2026-01-02") && seq (val (e, 0, "size"), "M"), "record 1");
	ok (seq (val (e, 1, "when"), "not a date") && seq (val (e, 1, "qty"), "lots") && seq (val (e, 1, "size"), "XL"), "values kept as they are");
	ok (seq (val (e, 2, "qty"), "12") && seq (val (e, 2, "size"), ""), "record 3");
	ok (!doc_read (e, "[app]\nname = x\n", 16, &why), "another INI file is not a form");
	ok (doc_read (e, "[records]\na\tb\n1\t2\n", 18, &why) && e.nf == 2 && e.nr == 1 && seq (e.r[0][1], "2"), "no [field]: the header's columns");

	// ---- a type changed: what reads as the new type stays
	doc_clear (e); doc_new (e);
	int lost, rounded;
	const char *vals[4] = { "12", "7.6", "abc", "" };
	for (int i = 0; i < 4; i++) { r = doc_add_record (e, -1); value_set (e.r[r][0], sdup (vals[i])); }
	convert_count (e, 0, FT_INT, 0, &lost, &rounded);
	ok (lost == 1 && rounded == 1, "convert_count to integer");
	convert_field (e, 0, FT_INT, 0);
	ok (seq (e.r[0][0], "12") && seq (e.r[1][0], "8") && seq (e.r[2][0], "") && e.f[0].type == FT_INT, "converted to integer");
	convert_field (e, 0, FT_CHOICE, 0);
	ok (e.f[0].nch == 2 && seq (e.f[0].ch[0], "12") && seq (e.f[0].ch[1], "8"), "to a choice list: the values its options");
	// fields moved, removed: the values follow
	value_set (e.r[0][1], sdup ("note"));
	ok (doc_move_field (e, 0, 1) && seq (e.r[0][0], "note") && seq (e.r[0][1], "12"), "field moved");
	doc_remove_field (e, 0);
	ok (e.nf == 1 && seq (e.r[0][0], "12"), "field removed");

	// ---- CSV: the separator, quotes, the types guessed
	const char *csv = "Name;Height;Price;Planted;Indoor;Code;Notes\r\nBasil;45;3,50;14/04/2025;yes;007;\"a;b\r\nc\"\r\n"
			  "Mint;40;3,1;01/04/2025;no;050;\"say \"\"hi\"\"\"\r\n";
	ok (csv_read (e, csv, (int) strlen (csv), "Plants"), "CSV read");
	ok (e.nf == 7 && e.nr == 2, "CSV size");
	int want[7] = { FT_TEXT, FT_INT, FT_DEC, FT_DATE, FT_BOOL, FT_TEXT, FT_MEMO };
	bool types = true; for (int k = 0; k < 7 && k < e.nf; k++) if (e.f[k].type != want[k]) types = false;
	ok (types, "CSV types guessed");
	ok (e.f[2].prec == 2 && seq (e.r[1][2], "3.10") && seq (e.r[0][3], "2025-04-14") && seq (e.r[0][4], "yes") && seq (e.r[1][4], ""), "CSV values");
	ok (seq (e.r[0][5], "007") && seq (e.r[0][6], "a;b\nc") && seq (e.r[1][6], "say \"hi\""), "CSV quotes, codes kept");
	Out oc; int ord[2] = { 0, 1 }; csv_write (e, ord, 2, oc);
	ok (strstr (oc.b, "Basil,45,3.50,14/04/2025,Yes,007,\"a;b\nc\"") != 0, "CSV written");

	// ---- the order: the search's words, the sort (a choice by its list's order, empty values last)
	doc_clear (e); doc_new (e);
	field_init (f, "Size", "size", FT_CHOICE); choice_add (f, "Small"); choice_add (f, "Medium"); choice_add (f, "Large");
	doc_insert_field (e, -1, f);
	const char *names[5] = { "Zo\xEB", "adam", "Bob", "Claire", "" };
	const char *sizes[5] = { "Large", "Small", "", "Medium", "Small" };
	for (int i = 0; i < 5; i++) { r = doc_add_record (e, -1); value_set (e.r[r][0], sdup (names[i])); value_set (e.r[r][2], sdup (sizes[i])); }
	int o[8], n;
	e.sort = 0; n = build_order (e, "", -1, o);
	ok (n == 5 && o[0] == 1 && o[1] == 2 && o[2] == 3 && o[3] == 0 && o[4] == 4, "sorted by name, empty last");
	e.sortDesc = true; n = build_order (e, "", -1, o);
	ok (o[0] == 0 && o[3] == 1 && o[4] == 4, "descending, empty still last");
	e.sort = 2; e.sortDesc = false; n = build_order (e, "", -1, o);
	ok (o[0] == 1 && o[1] == 4 && o[2] == 3 && o[3] == 0 && o[4] == 2, "a choice sorted by its list");
	e.sort = -1; n = build_order (e, "small ad", -1, o);
	ok (n == 1 && o[0] == 1, "every word of the search");
	n = build_order (e, "zoe", 2, o);
	ok (n == 2 && o[0] == 0 && o[1] == 2, "search: accents; the pinned record shown");

	doc_clear (d); doc_clear (e);
	printf ("%d checks, %d failed\n", checks, fails);
	return fails ? 1 : 0;
}
