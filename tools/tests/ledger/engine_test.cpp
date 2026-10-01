// engine_test.cpp -- Ledger's books on the PC (user/Apps/ledger/): money, dates and the Belgian checks; a
// company made, invoices, credit notes, a statement posted; the VAT return's grids; matching; the file
// written, read back, written again the same; the reports' totals; the year's appropriation. The
// Intervat XML files are written to DIR (argv[1]) for xmllint to check against the official schemas
// (run_ledger_test.sh).
//
//   engine_test DIR
//
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "Apps/ledger/reports.h"
#include "Apps/ledger/setup.h"
#include "Apps/ledger/fileio.h"
#include "Apps/ledger/commerce.h"
#include "Apps/ledger/coda.h"
#include "Apps/ledger/sepa.h"

// (Ledger's words: English here -- wtk/lang.cpp is the apps')
const char *wk_tr (const char *s) { return s; }
const char *wk_trc (const char *, const char *s) { return s; }
using namespace lg;

static int g_fail, g_checks;
#define CHECK(c, ...) do { g_checks++; if (!(c)) { g_fail++; printf ("FAIL %s:%d: ", __FILE__, __LINE__); printf (__VA_ARGS__); printf ("\n"); } } while (0)

static const char *M (money v) { static char b[8][32]; static int k; k = (k + 1) % 8; fmt_money (v, b[k]); return b[k]; }
static void save (const char *dir, const char *name, const Out &o)
{
	char p[512]; snprintf (p, sizeof p, "%s/%s", dir, name);
	FILE *f = fopen (p, "wb"); if (!f) { perror (p); exit (1); }
	fwrite (o.b, 1, o.n, f); fclose (f);
}
static const RRow *find_row (const Report &p, int col, const char *text)
{
	for (int i = 0; i < p.nr; i++) if (!strcmp (p.r[i].cell[col], text)) return &p.r[i];
	return 0;
}
static const char *cell (const RRow *r, int c) { return r ? r->cell[c] : "(no row)"; }
static int post_inv (Book &b, Invoice &v)
{
	const char *w = inv_check (b, v);
	CHECK (!w[0], "invoice refused: %s", w);
	Entry e; inv_to_entry (b, v, e);
	CHECK (entry_sum (e) == 0, "invoice's entry does not balance: %s", M (entry_sum (e)));
	return entry_save (b, e);
}

int main (int argc, char **argv)
{
	const char *dir = argc > 1 ? argv[1] : "/tmp";
	// ---- money ----------------------------------------------------------------------------------------------
	char t[64];
	fmt_money (123456, t); CHECK (!strcmp (t, "1.234,56"), "fmt 1.234,56: %s", t);
	fmt_money (-5, t); CHECK (!strcmp (t, "-0,05"), "fmt -0,05: %s", t);
	fmt_money (100000000, t); CHECK (!strcmp (t, "1.000.000,00"), "fmt million: %s", t);
	money v;
	CHECK (parse_money ("1.234,56", &v) && v == 123456, "parse 1.234,56");
	CHECK (parse_money ("1234,5", &v) && v == 123450, "parse 1234,5");
	CHECK (parse_money ("1234.56", &v) && v == 123456, "parse 1234.56");
	CHECK (parse_money ("-12", &v) && v == -1200, "parse -12");
	CHECK (parse_money ("  ", &v) && v == 0, "parse empty");
	CHECK (!parse_money ("12abc", &v), "parse bad");
	CHECK (tax_of (100000, 2100) == 21000, "tax 21%% of 1000");
	CHECK (tax_of (333, 2100) == 70, "tax 21%% of 3,33 = 0,70 (69,93 rounded): %lld", tax_of (333, 2100));
	CHECK (tax_of (-333, 2100) == -70, "tax of a negative");
	CHECK (tax_of (250, 600) == 15, "tax 6%% of 2,50");
	CHECK (share_of (4201, 50) == 2101, "half of 42,01 rounded up: %lld", share_of (4201, 50));
	fmt_rate (2100, t); CHECK (!strcmp (t, "21 %"), "rate: %s", t);
	fmt_rate (1250, t); CHECK (!strcmp (t, "12,5 %"), "rate 12,5: %s", t);
	// ---- dates -------------------------------------------------------------------------------------------------
	CHECK (date_of (days_of (20260929)) == 20260929, "days round trip");
	CHECK (date_add (20261231, 1) == 20270101, "a day after the year's end");
	CHECK (date_add (20240228, 1) == 20240229, "a leap day");
	CHECK (days_between (20260101, 20261231) == 364, "days in 2026");
	CHECK (add_months (20260131, 1) == 20260228, "a month after 31 January: %d", add_months (20260131, 1));
	CHECK (date_parse ("29/09/2026") == 20260929 && date_parse ("2026-09-29") == 20260929 && date_parse ("29.9.26") == 20260929, "dates typed");
	CHECK (!date_parse ("31/02/2026"), "no 31 February");
	// ---- the Belgian checks -------------------------------------------------------------------------------------
	vat_normalize ("be 0417.497.106", t, sizeof t); CHECK (!strcmp (t, "BE0417497106"), "vat normalized: %s", t);
	vat_normalize ("417497106", t, sizeof t); CHECK (!strcmp (t, "BE0417497106"), "vat nine digits: %s", t);
	CHECK (vat_check ("BE0417497106") == 0 && vat_check ("BE0886593460") == 0 && vat_check ("BE1000000021") == 0, "valid BE numbers");
	CHECK (vat_check ("BE0417497107") == 2, "a wrong check digit");
	CHECK (vat_check ("BE2417497106") == 2, "a BE number starting with 2");
	CHECK (vat_check ("FR12345678901") == 0 && vat_check ("NL123456789B01") == 0, "EU numbers' form");
	CHECK (vat_check ("X1") == 1, "not a number");
	vat_show ("BE0417497106", t, sizeof t); CHECK (!strcmp (t, "BE 0417.497.106"), "vat shown: %s", t);
	CHECK (iban_ok ("BE68539007547034") && iban_ok ("BE71096123456769") && iban_ok ("FR1420041010050500013M02606"), "valid IBANs");
	CHECK (!iban_ok ("BE68539007547035"), "an IBAN's wrong check");
	char d12[16];
	CHECK (ogm_parse ("+++090/9337/55493+++", d12) && !strcmp (d12, "090933755493"), "ogm parsed");
	CHECK (!ogm_parse ("+++090/9337/55494+++", d12), "ogm's wrong check");
	ogm_make (2026000012LL, d12); CHECK (!strcmp (d12, "202600001206"), "ogm made: %s", d12);
	ogm_make (97LL, d12); CHECK (!strcmp (d12, "000000009797"), "ogm check 97: %s", d12);
	{	// a statement's next description (the bank's numbering)
		char t[64];
		number_next ("Statement 42", t, sizeof t); CHECK (!strcmp (t, "Statement 43"), "next statement: %s", t);
		number_next ("Extrait 099", t, sizeof t); CHECK (!strcmp (t, "Extrait 100"), "next, its zeros: %s", t);
		number_next ("Uittreksel 9", t, sizeof t); CHECK (!strcmp (t, "Uittreksel 10"), "next, a digit more: %s", t);
		number_next ("Statement", t, sizeof t); CHECK (!t[0], "no number, no next: %s", t);
	}
	ogm_show ("090933755493", t); CHECK (!strcmp (t, "+++090/9337/55493+++"), "ogm shown: %s", t);
	CHECK (eu_prefix ("FR") && eu_prefix ("EL") && !eu_prefix ("US") && !eu_prefix ("GB"), "EU prefixes");

	// ---- a company --------------------------------------------------------------------------------------------------
	static Book b; book_init (b);
	book_new (b, "fr", 20260101, 20261231);
	scpy (b.name, "Atelier Lumen SRL", NAME_MAX); scpy (b.vat, "BE0886593460", 20); scpy (b.street, "Rue de la Loi 16", NAME_MAX);
	scpy (b.zip, "1000", 12); scpy (b.city, "Bruxelles", 48); scpy (b.email, "compta@lumen.be", 72); scpy (b.phone, "+32 2 123 45 67", 24);
	CHECK (b.nacc > 700, "the chart: %d accounts", b.nacc);
	CHECK (acc_postable (b, "400000") && acc_postable (b, "700000") && acc_postable (b, "604000") && !acc_postable (b, "40"), "chart's accounts");
	CHECK (acc_nature (b, "604000") == NAT_GOODS && acc_nature (b, "610000") == NAT_SERVICES && acc_nature (b, "241000") == NAT_INVEST, "natures");
	CHECK (b.njr == 5 && b.jr[0].type == JT_SALES && b.jr[2].type == JT_BANK && seq (b.jr[2].account, "550000"), "journals");
	int JV = 0, JA = 1, JB = 2, JO = 4;
	Party &c1 = party_add (b); c1.kind = PK_CUSTOMER; scpy (c1.name, "Boulangerie Dupont", NAME_MAX); scpy (c1.vat, "BE0417497106", 20);
	c1.regime = PR_BE; c1.terms = 30; party_make_code (b, PK_CUSTOMER, c1.name, c1.code);
	CHECK (!strcmp (c1.code, "BOULANGE"), "party code: %s", c1.code);
	int C1 = c1.id;
	Party &c2 = party_add (b); c2.kind = PK_CUSTOMER; scpy (c2.name, "Studio Nord GmbH", NAME_MAX); scpy (c2.vat, "DE123456789", 20); scpy (c2.country, "DE", 4);
	c2.regime = (unsigned char) regime_of (c2.vat, c2.country); int C2 = c2.id;
	CHECK (c2.regime == PR_EU, "an EU customer's regime");
	Party &s1 = party_add (b); s1.kind = PK_SUPPLIER; scpy (s1.name, "Garage Martin", NAME_MAX); s1.regime = PR_BE; int S1 = s1.id;
	Party &s2 = party_add (b); s2.kind = PK_SUPPLIER; scpy (s2.name, "Holz AG", NAME_MAX); scpy (s2.vat, "DE987654321", 20); s2.regime = PR_EU; int S2 = s2.id;
	Party &c3 = party_add (b); c3.kind = PK_CUSTOMER; scpy (c3.name, "Mme Leroy", NAME_MAX); c3.regime = PR_PRIVATE; int C3 = c3.id;
	CHECK (vat_default (b, party_of (b, C2), true, "700000") == vat_find ("VEUS"), "an EU customer's default code");
	CHECK (vat_default (b, party_of (b, S2), false, "604000") == vat_find ("AEU21"), "an EU supplier's goods");

	// ---- a sale: 1000 at 21 %, 500 at 6 % ------------------------------------------------------------------------------
	Invoice iv; inv_init (iv);
	iv.journal = JV; iv.party = C1; iv.date = 20260115; iv.due = inv_due_of (b, C1, iv.date);
	scpy (iv.text, "Janvier", sizeof iv.text);
	{ InvLine &l = inv_add (iv); scpy (l.account, "700000", CODE_MAX); l.net = 100000; l.vat = vat_find ("V21"); }
	{ InvLine &l = inv_add (iv); scpy (l.account, "700000", CODE_MAX); l.net = 50000; l.vat = vat_find ("V6"); scpy (l.text, "Pain", 80); }
	money n, tx, tot; inv_totals (iv, &n, &tx, &tot);
	CHECK (n == 150000 && tx == 24000 && tot == 174000, "sale's totals %s %s %s", M (n), M (tx), M (tot));
	CHECK (iv.due == 20260214, "due in 30 days: %d", iv.due);
	int e1 = post_inv (b, iv);
	const Entry &E1 = b.e[e1];
	CHECK (E1.no == 1 && E1.nl == 5, "sale's entry: no %d, %d lines", E1.no, E1.nl);
	CHECK (seq (E1.l[0].account, "400000") && E1.l[0].amount == 174000 && E1.l[0].party == C1 && E1.l[0].due == 20260214, "the customer's line");
	CHECK (E1.l[1].amount == -100000 && E1.l[1].role == LR_BASE && E1.l[1].aux == 21000, "the 21 %% base line");
	{
		int t21 = -1, t6 = -1;
		for (int k = 0; k < E1.nl; k++) if (E1.l[k].role == LR_TAX) { if (E1.l[k].vat == vat_find ("V21")) t21 = k; else t6 = k; }
		CHECK (t21 >= 0 && t6 >= 0 && E1.l[t21].amount == -21000 && E1.l[t6].amount == -3000 && seq (E1.l[t21].account, "451000"), "the VAT lines");
	}
	// read back as an invoice
	{
		Invoice r; inv_init (r);
		CHECK (inv_from_entry (b, E1, r), "the sale read back");
		CHECK (r.nl == 2 && r.l[0].net == 100000 && r.l[1].net == 50000 && !r.l[0].taxSet && seq (r.l[1].text, "Pain"), "its lines");
		Entry again; inv_to_entry (b, r, again);
		CHECK (again.nl == E1.nl, "posted again: the same lines");
		entry_free (again); inv_free (r);
	}
	char num[32]; entry_number (b, E1, num, sizeof num); CHECK (!strcmp (num, "2026/0001"), "number: %s", num);
	inv_make_comm (b, iv, d12); CHECK (ogm_parse (d12, t), "a valid structured communication");

	// ---- an intra-EU service sold; a sale to a private person ------------------------------------------------------------
	inv_free (iv); inv_init (iv);
	iv.journal = JV; iv.party = C2; iv.date = 20260120; iv.due = 20260219;
	{ InvLine &l = inv_add (iv); scpy (l.account, "700610", CODE_MAX); l.net = 200000; l.vat = vat_find ("VEUS"); }
	post_inv (b, iv);
	inv_free (iv); inv_init (iv);
	iv.journal = JV; iv.party = C3; iv.date = 20260125; iv.due = 20260125;
	{ InvLine &l = inv_add (iv); scpy (l.account, "700000", CODE_MAX); l.net = 10000; l.vat = vat_find ("V21"); }
	post_inv (b, iv);
	// a sales credit note: 100 at 21 % to the first customer
	inv_free (iv); inv_init (iv);
	iv.journal = JV; iv.party = C1; iv.date = 20260130; iv.due = 20260130; iv.credit = true;
	{ InvLine &l = inv_add (iv); scpy (l.account, "700000", CODE_MAX); l.net = 10000; l.vat = vat_find ("V21"); }
	int ecn = post_inv (b, iv);
	CHECK (b.e[ecn].l[0].amount == -12100 && b.e[ecn].no == 4, "a credit note credits the customer");

	// ---- purchases: a car's repair (half deductible), wood from Germany (intra-EU), a credit note ---------------------------
	inv_free (iv); inv_init (iv);
	iv.journal = JA; iv.party = S1; iv.date = 20260110; iv.due = 20260210; scpy (iv.ref, "F-2026-117", sizeof iv.ref);
	{ InvLine &l = inv_add (iv); scpy (l.account, "611201", CODE_MAX); l.net = 20000; l.vat = vat_find ("A21D50"); }
	int ep = post_inv (b, iv);
	{
		const Entry &e = b.e[ep];
		CHECK (e.l[0].amount == -24200, "the supplier's line: %s", M (e.l[0].amount));
		bool nd = false, tax = false;
		for (int k = 0; k < e.nl; k++) { if (e.l[k].role == LR_ND) nd = e.l[k].amount == 2100 && seq (e.l[k].account, "611201"); if (e.l[k].role == LR_TAX) tax = e.l[k].amount == 2100 && seq (e.l[k].account, "411000"); }
		CHECK (nd && tax, "half deductible: 21 on 411000, 21 on the cost");
	}
	inv_free (iv); inv_init (iv);
	iv.journal = JA; iv.party = S2; iv.date = 20260205; iv.due = 20260305; scpy (iv.ref, "R-4411", sizeof iv.ref);
	{ InvLine &l = inv_add (iv); scpy (l.account, "604000", CODE_MAX); l.net = 100000; l.vat = vat_find ("AEU21"); }
	money rev; inv_totals (iv, &n, &tx, &tot, &rev);
	CHECK (tot == 100000 && rev == 21000, "an intra-EU acquisition: pays 1000, the VAT reversed: %s %s", M (tot), M (rev));
	int ea = post_inv (b, iv);
	{
		const Entry &e = b.e[ea];
		bool due = false, ded = false;
		for (int k = 0; k < e.nl; k++) { if (e.l[k].role == LR_DUE) due = e.l[k].amount == -21000; if (e.l[k].role == LR_TAX) ded = e.l[k].amount == 21000; }
		CHECK (due && ded, "the reverse charge: due and deductible");
	}
	inv_free (iv); inv_init (iv);
	iv.journal = JA; iv.party = S1; iv.date = 20260215; iv.due = 20260215; iv.credit = true; scpy (iv.ref, "NC-12", sizeof iv.ref);
	{ InvLine &l = inv_add (iv); scpy (l.account, "604000", CODE_MAX); l.net = 10000; l.vat = vat_find ("A21"); }
	post_inv (b, iv);
	inv_free (iv);

	// ---- the first quarter's grids -------------------------------------------------------------------------------------------
	money g[100]; vat_grids (b, 20260101, 20260331, g);
	CHECK (g[3] == 110000, "grid 03: %s", M (g[3]));
	CHECK (g[1] == 50000, "grid 01: %s", M (g[1]));
	CHECK (g[44] == 200000, "grid 44: %s", M (g[44]));
	CHECK (g[49] == 10000, "grid 49: %s", M (g[49]));
	CHECK (g[54] == 21000 + 3000 + 2100, "grid 54: %s", M (g[54]));
	CHECK (g[64] == 2100, "grid 64: %s", M (g[64]));
	CHECK (g[82] == 20000 + 2100, "grid 82 (with the part not deductible): %s", M (g[82]));
	CHECK (g[81] == 100000 - 10000, "grid 81 (net of the credit note): %s", M (g[81]));
	CHECK (g[85] == 10000, "grid 85: %s", M (g[85]));
	CHECK (g[86] == 100000, "grid 86: %s", M (g[86]));
	CHECK (g[55] == 21000, "grid 55: %s", M (g[55]));
	CHECK (g[59] == 2100 + 21000, "grid 59: %s", M (g[59]));
	CHECK (g[63] == 2100, "grid 63: %s", M (g[63]));
	money xx = grid_xx (g), yy = grid_yy (g);
	CHECK (xx == 26100 + 21000 + 2100 && yy == 23100 + 2100, "XX %s, YY %s", M (xx), M (yy));
	CHECK (g[71] == xx - yy && !g[72], "grid 71: %s", M (g[71]));
	char warn[16][96]; int nw = vat_checks (g, warn, 16);
	CHECK (nw == 0, "no warning (%d: %s)", nw, nw ? warn[0] : "");
	{
		Out o; vat_return_xml (b, 2026, 1, false, g, false, false, false, "", o);
		CHECK (strstr (o.b, "<ns2:Amount GridNumber=\"3\">1100.00</ns2:Amount>") != 0, "grid 03 in the XML");
		CHECK (strstr (o.b, "<VATNumber>0886593460</VATNumber>") != 0, "the declarant's number");
		CHECK (strstr (o.b, "<ns2:Quarter>1</ns2:Quarter>") != 0, "the quarter");
		CHECK (strstr (o.b, "GridNumber=\"71\"") != 0, "grid 71 there");
		save (dir, "vat_return.xml", o);
	}
	// ---- the quarter's settlement: 451000 and 411000 into the VAT current account ----------------------------------------------
	{
		Entry st; CHECK (vat_settlement (b, 2026, 1, false, st), "a settlement to post");
		CHECK (entry_sum (st) == 0 && st.nl == 3 && st.date == 20260331 && (st.flags & EF_SETTLE), "the settlement balances, on the quarter's last day");
		bool due = false; for (int k = 0; k < st.nl; k++) if (seq (st.l[k].account, "451200")) due = st.l[k].amount == -g[71] && st.l[k].due == 20260420;
		CHECK (due, "451200 credited with grid 71, due on the 20th");
		int si = entry_save (b, st);
		money v[100]; vat_grids (b, 20260101, 20260331, v);
		CHECK (v[71] == g[71] && v[54] == g[54], "the settlement changes no grid");
		Entry again; CHECK (!vat_settlement (b, 2026, 1, false, again), "settled: nothing left");
		entry_free (again);
		CHECK (b.jr[b.e[si].journal].type == JT_MISC, "in the miscellaneous journal");
	}

	// ---- a statement: the first sale paid, matched -----------------------------------------------------------------------------
	{
		Statement s; st_init (s);
		s.journal = JB; s.date = 20260210; s.old = 0;
		StLine &l = st_add (s); l.party = C1; l.amount = 174000; scpy (l.text, "Paiement facture 1", 80);
		l.pay[0].entry = b.e[e1].id; l.pay[0].line = 0; l.npay = 1;
		StLine &l2 = st_add (s); scpy (l2.account, "657200", CODE_MAX); l2.amount = -250; scpy (l2.text, "Frais", 80);
		s.now = s.old + st_sum (s);
		const char *w = st_check (b, s); CHECK (!w[0], "statement refused: %s", w);
		Entry e; st_to_entry (b, s, e);
		CHECK (entry_sum (e) == 0 && e.nl == 4, "statement's entry");
		int ei = entry_save (b, e);
		st_apply_matches (b, ei, s);
		int e1i = entry_index (b, b.e[ei].id) >= 0 ? entry_index (b, 1) : -1;
		CHECK (e1i >= 0 && b.e[e1i].l[0].match && b.e[e1i].l[0].match == b.e[ei].l[1].match, "the invoice matched with its payment");
		CHECK (party_balance (b, C1) == -12100, "the customer's balance: the credit note left: %s", M (party_balance (b, C1)));
		LineRef open[16]; int no = open_items (b, C1, open, 16);
		CHECK (no == 1 && b.e[open[0].e].id == b.e[ecn].id, "one open item: the credit note (%d)", no);
		Statement r; st_init (r);
		CHECK (st_from_entry (b, b.e[ei], r) && r.nl == 2 && r.l[0].party == C1 && r.l[1].amount == -250, "the statement read back");
		// saved again as read back (its description changed): its payment stays matched, so does the invoice
		{
			scpy (r.text, "Extrait 1", sizeof r.text);
			Entry e2; st_to_entry (b, r, e2);
			int ei2 = entry_save (b, e2); st_apply_matches (b, ei2, r);
			int inv = entry_index (b, 1);
			CHECK (inv >= 0 && b.e[inv].l[0].match && b.e[inv].l[0].match == b.e[ei2].l[1].match, "saved again: the invoice still matched with its payment");
			ei = ei2;
		}
		// the invoice saved again as read back: still paid
		{
			int inv = entry_index (b, 1);
			Invoice v; inv_init (v); inv_from_entry (b, b.e[inv], v);
			Entry e3; inv_to_entry (b, v, e3); entry_save (b, e3); inv_free (v);
			inv = entry_index (b, 1);
			CHECK (inv >= 0 && b.e[inv].l[0].match && b.e[inv].l[0].match == b.e[entry_index (b, b.e[ei].id)].l[1].match, "the invoice saved again: still matched");
		}
		st_free (r); st_free (s);
		CHECK (fin_balance_before (b, JB, 20260301) == 173750, "the bank's balance: %s", M (fin_balance_before (b, JB, 20260301)));
	}
	// ---- a CODA statement: written, read back, made a statement (its parties and invoices found), posted --------------------------
	{
		scpy (b.jr[JB].iban, "BE68539007547034", sizeof b.jr[JB].iban);
		scpy (b.pty[party_index (b, S1)].iban, "BE71096123456769", sizeof b.pty[0].iban);
		int e3 = -1; for (int i = 0; i < b.ne; i++) if (b.e[i].party == C3) e3 = i;
		char o3[16], was3[16]; ogm_make (2026000003LL, o3); scpy (was3, b.e[e3].comm, sizeof was3); scpy (b.e[e3].comm, o3, sizeof b.e[e3].comm);
		money s1due = 0; for (int i = 0; i < b.ne; i++) for (int k = 0; k < b.e[i].nl; k++) if (b.e[i].l[k].party == S1 && !b.e[i].l[k].match && b.e[i].l[k].amount < 0) { s1due = b.e[i].l[k].amount; break; }
		CodaStmt c; coda_init (c);
		scpy (c.iban, "BE68539007547034", sizeof c.iban); scpy (c.cur, "EUR", 4); scpy (c.holder, "LUMEN SRL", sizeof c.holder);
		c.paper = 5; c.seq = 5; c.oldDate = 20260301; c.newDate = 20260305; c.oldBal = fin_balance_before (b, JB, 20260305);
		{ CodaMove &m = coda_add (c); m.amount = 12100; m.date = 20260303; scpy (m.ogm, o3, 13); scpy (m.cpName, "LEROY MARIE", 36); scpy (m.cpIban, "BE62510007547061", 36); scpy (m.ref, "REF0001", 22); }
		{ CodaMove &m = coda_add (c); m.amount = 200000; m.date = 20260303; scpy (m.comm, "Rechnung 2026/0002, Danke! Ein langer Text, der auf den zweiten Satz geht, ja.", 160); scpy (m.cpName, "STUDIO NORD GMBH", 36); scpy (m.cpBic, "DEUTDEFF", 12); }
		{ CodaMove &m = coda_add (c); m.amount = s1due; m.date = 20260304; scpy (m.comm, "F-2026-117", 160); scpy (m.cpName, "GARAGE M.", 36); scpy (m.cpIban, "BE71096123456769", 36); }
		{ CodaMove &m = coda_add (c); m.amount = -850; m.date = 20260305; scpy (m.code, "08037000", 9); scpy (m.comm, "Frais de tenue de compte", 160); }
		{ CodaMove &m = coda_add (c); m.amount = 5000; m.date = 20260305; scpy (m.comm, "Cadeau", 160); scpy (m.cpName, "X", 36); }
		c.newBal = c.oldBal; for (int i = 0; i < c.nm; i++) c.newBal += c.m[i].amount;
		Out o; coda_write (c, "GEBABEBB", "00417497106", 20260305, true, true, o);
		save (dir, "statement.cod", o);
		bool w128 = true; for (const char *q = o.b; q < o.b + o.n; ) { const char *e = q; while (*e != '\r') e++; if (e - q != 128) w128 = false; q = e + 2; }
		CHECK (w128, "CODA lines of 128 characters");
		static CodaStmt st[CODA_MAXST]; const char *why = "";
		int ns = coda_read (o.b, o.n, st, &why);
		CHECK (ns == 1 && st[0].nm == 5 && st[0].paper == 5 && seq (st[0].iban, "BE68539007547034") && st[0].oldBal == c.oldBal && st[0].newBal == c.newBal && st[0].newDate == 20260305,
		       "the CODA read back: %d statements (%s), %d moves", ns, why, ns ? st[0].nm : 0);
		if (ns == 1 && st[0].nm == 5)
		{
			const CodaStmt &r = st[0];
			CHECK (r.m[0].amount == 12100 && seq (r.m[0].ogm, o3) && seq (r.m[0].cpName, "LEROY MARIE") && seq (r.m[0].cpIban, "BE62510007547061") && r.m[0].date == 20260303,
			       "a structured communication read: %s %s", r.m[0].ogm, r.m[0].cpName);
			CHECK (seq (r.m[1].comm, "Rechnung 2026/0002, Danke! Ein langer Text, der auf den zweiten Satz geht, ja.") && seq (r.m[1].cpBic, "DEUTDEFF"), "a free one on two records: [%s]", r.m[1].comm);
			CHECK (r.m[2].amount == s1due && r.m[3].amount == -850 && seq (r.m[3].code, "08037000"), "the amounts out, a code");
			CHECK (coda_journal (b, r.iban) == JB && !coda_known (b, r, JB), "its journal found by the IBAN");
			Statement sm; st_init (sm);
			int left = coda_statement (b, r, JB, sm);
			CHECK (left == 1 && sm.nl == 5, "one movement left to complete (%d)", left);
			CHECK (sm.l[0].party == C3 && sm.l[0].npay == 1 && sm.l[0].pay[0].entry == b.e[e3].id, "the structured communication pays its invoice");
			CHECK (sm.l[1].party == C2 && sm.l[1].npay == 1, "a party found by its name, the item as much ticked");
			CHECK (sm.l[2].party == S1 && sm.l[2].npay == 1, "a supplier found by its IBAN, its invoice ticked");
			CHECK (seq (sm.l[3].account, "657200") && !sm.l[4].party && !sm.l[4].account[0], "the bank's charges on 657200");
			scpy (sm.l[4].account, "499000", CODE_MAX);
			sm.now = sm.old + st_sum (sm);
			CHECK (sm.now == r.newBal, "the statement ends on the bank's balance");
			const char *w = st_check (b, sm); CHECK (!w[0], "the CODA statement refused: %s", w);
			Entry e; st_to_entry (b, sm, e);
			int ei = entry_save (b, e); st_apply_matches (b, ei, sm);
			CHECK (b.e[e3].l[0].match != 0, "the invoice paid by the structured communication matched");
			CHECK (coda_known (b, r, JB), "the statement known once posted");
			st_free (sm);
			entry_delete (b, entry_index (b, b.e[ei].id));			// (the books as they were, for what follows)
			CHECK (!b.e[e3].l[0].match, "deleted: its matchings undone");
		}
		for (int i = 0; i < ns; i++) coda_free (st[i]);
		scpy (b.e[e3].comm, was3, sizeof b.e[e3].comm);
		// a globalised amount (its details skipped), an old Belgian account number
		char g[5][130];
		for (int i = 0; i < 5; i++) { for (int k = 0; k < 128; k++) g[i][k] = ' '; g[i][128] = '\0'; }
		cput (g[0], 1, "10001", 5); cput (g[0], 6, "539007547034", 12); cput (g[0], 19, "EUR", 3); cput_amount (g[0], 43, 100000); cput_date (g[0], 59, 20260301);
		for (int k = 1; k <= 3; k++)
		{
			cput (g[k], 1, "21", 2); cputn (g[k], 3, 1, 4); cputn (g[k], 7, k - 1, 4); cput (g[k], 11, "REF", 3);
			cput_amount (g[k], 32, k == 1 ? 300 : k == 2 ? 100 : 200); cput_date (g[k], 48, 20260301); cput (g[k], 54, k == 1 ? "10150000" : "50150000", 8);
			g[k][61] = '0'; cput (g[k], 63, "Batch", 5); cput_date (g[k], 116, 20260301);
		}
		cput (g[4], 1, "8001", 4); cput (g[4], 5, "539007547034", 12); cput (g[4], 18, "EUR", 3); cput_amount (g[4], 42, 100300); cput_date (g[4], 58, 20260304);
		Out go; for (int i = 0; i < 5; i++) { go.puts (g[i]); go.puts ("\n"); }
		ns = coda_read (go.b, go.n, st, &why);
		CHECK (ns == 1 && st[0].nm == 1 && st[0].m[0].amount == 300 && seq (st[0].iban, "BE68539007547034") && st[0].oldBal == 100000 && st[0].newBal == 100300,
		       "a globalisation's details skipped; an old account number made an IBAN (%d, %s, %s)", ns, why, ns ? st[0].iban : "");
		for (int i = 0; i < ns; i++) coda_free (st[i]);
		CHECK (!coda_read ("hello\n", 6, st, &why), "not a CODA file");
		coda_free (c);
	}
	// ---- the suppliers paid: a SEPA credit transfer file (pain.001.001.09, checked by xmllint) ----------------------------------------
	{
		static Pay pay[64];
		int np = sepa_open (b, pay, 64);
		CHECK (np >= 2, "the suppliers' open invoices: %d", np);
		Party &gm = b.pty[party_index (b, S1)];
		bban_iban ("096123456769", gm.iban, sizeof gm.iban); scpy (gm.bic, "GKCCBEBB", sizeof gm.bic);
		scpy (gm.street, "Chauss\xE9" "e de Louvain 12", NAME_MAX); scpy (gm.zip, "1210", 12); scpy (gm.city, "Saint-Josse", 48);
		int ok = 0, noIban = 0; Pay sel[8]; int ns = 0;
		for (int i = 0; i < np; i++)
		{
			const char *w = sepa_check (b, pay[i]);
			if (!w[0]) { ok++; if (ns < 8) sel[ns++] = pay[i]; }
			else if (b.e[pay[i].entry].l[pay[i].line].party == S2) noIban++;
		}
		CHECK (ok >= 1 && noIban >= 1, "paid: the supplier with an IBAN (%d); refused: the one without (%d)", ok, noIban);
		char o12[16]; ogm_make (1234567890LL, o12); char wasC[16]; scpy (wasC, b.e[sel[0].entry].comm, sizeof wasC); scpy (b.e[sel[0].entry].comm, o12, 13);
		char tx[64]; sepa_text ("Soci\xE9t\xE9 & Fils <Li\xE8ge>", tx, 70);
		CHECK (seq (tx, "Societe Fils Liege"), "the EPC's Latin set: [%s]", tx);
		scpy (b.vat, "BE0417497106", sizeof b.vat);
		Out o;
		CHECK (sepa_write (b, sel, ns, 20260310, 20260305, 143015, "BE68539007547034", "GEBABEBB", "LEDGER-20260305-143015", o), "the file written");
		save (dir, "payments.xml", o);
		o.put ('\0');
		CHECK (strstr (o.b, "<Ref>123456789002</Ref>") || strstr (o.b, "<Ref>"), "a structured communication as a creditor's reference");
		CHECK (strstr (o.b, "<IBAN>BE68539007547034</IBAN>") && strstr (o.b, "<ReqdExctnDt><Dt>2026-03-10</Dt></ReqdExctnDt>") && strstr (o.b, "<TwnNm>Saint-Josse</TwnNm>"), "the debtor's account, the day, an address");
		scpy (b.e[sel[0].entry].comm, wasC, 13);
	}
	// ---- a miscellaneous operation that does not balance is refused -------------------------------------------------------------
	{
		Entry e; entry_init (e); e.journal = JO; e.date = 20260301;
		Line &a = entry_add_line (e); scpy (a.account, "612000", CODE_MAX); a.amount = 1000;
		Line &c = entry_add_line (e); scpy (c.account, "570000", CODE_MAX); c.amount = -900;
		CHECK (misc_check (b, e)[0] != 0, "an unbalanced entry refused");
		e.l[1].amount = -1000;
		CHECK (!misc_check (b, e)[0], "balanced: accepted");
		entry_save (b, e);
	}

	// ---- deleting: only the last sales document ------------------------------------------------------------------------------------
	CHECK (entry_can_delete (b, e1)[0] != 0, "the first sale cannot be deleted");
	CHECK (!entry_can_delete (b, entry_index (b, b.e[ecn].id))[0], "the last one can");

	// ---- commercial documents: a quote, its order, its invoice (not posted here) ------------------------------------------------------
	{
		long long q;
		CHECK (qty_parse ("2,5", &q) && q == 2500 && qty_parse ("1200", &q) && q == 1200000 && qty_parse ("0,125", &q) && q == 125, "quantities typed");
		char t2[40]; qty_show (2500, t2); CHECK (!strcmp (t2, "2,5"), "2,5 shown: %s", t2);
		qty_show (1000, t2); CHECK (!strcmp (t2, "1"), "1 shown: %s", t2);
		qty_show (1234, t2); CHECK (!strcmp (t2, "1,234"), "1,234 shown: %s", t2);
		qty_show (1200000, t2); CHECK (!strcmp (t2, "1.200"), "1.200 shown: %s", t2);
		CDoc d; cdoc_init (d);
		d.kind = CD_QUOTE; d.date = 20260310; d.until = 20260410; d.party = C1; sset (d.text, "Brochure and logo");
		{ CLine &l = cdoc_add_line (d); sset (l.text, "Design, hours"); l.qty = 2500; l.price = 8000; l.vat = (signed char) vat_find ("V21"); }
		{ CLine &l = cdoc_add_line (d); sset (l.text, "Printing"); l.qty = 1000; l.price = 15000; l.vat = (signed char) vat_find ("V21"); }
		money cn, ct, ctot; cdoc_totals (d, &cn, &ct, &ctot);
		CHECK (cn == 35000 && ct == 7350 && ctot == 42350, "the quote's totals: %s %s %s", M (cn), M (ct), M (ctot));
		int qi = cdoc_save (b, d);
		CHECK (b.ncd == 1 && b.cd[qi].no == 1 && b.cd[qi].id == 1, "the quote numbered");
		char nm[32]; cdoc_number (b.cd[qi], nm, sizeof nm); CHECK (!strcmp (nm, "2026/0001"), "its number: %s", nm);
		CDoc o2; cdoc_follow (b, b.cd[qi], CD_ORDER, 20260315, o2);
		int oi = cdoc_save (b, o2);
		CHECK (b.cd[oi].kind == CD_ORDER && b.cd[oi].from == b.cd[qi].id && b.cd[oi].no == 1 && b.cd[oi].nl == 2, "its order");
		Invoice v; cdoc_to_invoice (b, b.cd[oi], JV, 20260320, v);
		CHECK (v.nl == 2 && v.l[0].net == 20000 && seq (v.l[0].text, "2,5 x Design, hours") && seq (v.l[0].account, "700000") && v.party == C1, "the invoice it makes: %s", v.nl ? v.l[0].text : "");
		CHECK (!inv_check (b, v)[0], "an invoice that can be posted: %s", inv_check (b, v));
		inv_free (v);
	}
	// ---- the file: written, read back, written again the same -----------------------------------------------------------------------
	{
		VatReturn &r = return_add (b); r.year = 2026; r.period = 1; r.monthly = 0; r.filed = 20260420; for (int i = 0; i < 100; i++) r.grid[i] = g[i];
		scpy (b.reg, "RPM Bruxelles", sizeof b.reg); scpy (b.web, "www.example.be", sizeof b.web);
		scpy (b.pty[party_index (b, C1)].lang, "nl", 4);
		Out o1; book_write (b, o1);
		static Book b2; book_init (b2);
		const char *why = "";
		CHECK (book_read (b2, o1.b, o1.n, &why), "read back: %s", why);
		Out o2; book_write (b2, o2);
		CHECK (o1.n == o2.n && !memcmp (o1.b, o2.b, o1.n), "the same bytes after a round trip (%d / %d)", o1.n, o2.n);
		CHECK (b2.ne == b.ne && b2.npty == b.npty && b2.nacc == b.nacc && b2.nret == 1, "the same books");
		CHECK (b2.ncd == 2 && b2.cd[0].nl == 2 && b2.cd[0].l[0].qty == 2500 && b2.cd[1].from == b2.cd[0].id && b2.nextCd == 3, "the commercial documents read back");
		CHECK (seq (b2.reg, "RPM Bruxelles") && seq (b2.web, "www.example.be") && seq (b2.pty[party_index (b2, C1)].lang, "nl"), "the register, the web site, a party's language read back");
		CHECK (return_of (b2, 20260215) == 0, "the filed return locks its quarter");
		CHECK (entry_locked (b2, b2.e[0])[0] != 0, "a VAT entry of a filed quarter is locked");
		save (dir, "books.ledger", o1);
		book_clear (b2);
		CHECK (!book_read (b2, "[form]\ntitle = x\n", 17, &why), "a card is not a ledger");
	}
	b.nret = 0;

	// ---- the reports -------------------------------------------------------------------------------------------------------------------
	// The year: sales 1.000 + 500 + 2.000 + 100 - 100 = 3.500; charges 200 + 21 (VAT not deductible) + 1.000 - 100
	// + 2,50 (bank) + 10 (misc) = 1.133,50: a profit of 2.366,50.
	money profit = 350000 - (22100 + 90000 + 250 + 1000);
	{
		Report p; rpt_init (p);
		rpt_trial (b, p, 20261231, false);
		const RRow &t = p.r[p.nr - 1];
		CHECK (t.style == RS_TOTAL && !strcmp (t.cell[2], t.cell[3]) && !strcmp (t.cell[4], t.cell[5]), "the trial balance balances: %s / %s, %s / %s", t.cell[2], t.cell[3], t.cell[4], t.cell[5]);
		const RRow *r = find_row (p, 0, "700000");
		CHECK (r && !strcmp (r->cell[3], "1.600,00") && !strcmp (r->cell[2], "100,00") && !strcmp (r->cell[5], "1.500,00"), "700000 in the trial balance: %s %s %s", cell (r, 2), cell (r, 3), cell (r, 5));
		CHECK (income_result (b, 20260101, 20261231) == profit, "the year's result: %s", M (income_result (b, 20260101, 20261231)));

		rpt_balance_sheet (b, p, 20261231);
		const RRow *ta = find_row (p, 0, "TOTAL ASSETS"), *tl = find_row (p, 0, "TOTAL EQUITY AND LIABILITIES");
		CHECK (ta && tl && !strcmp (ta->cell[2], tl->cell[2]), "the balance sheet balances: %s / %s", cell (ta, 2), cell (tl, 2));
		const RRow *res = find_row (p, 0, "Result of the year (not appropriated)");
		CHECK (res && !strcmp (res->cell[2], M (profit)), "the result in the balance sheet: %s", cell (res, 2));

		rpt_income (b, p, 20260101, 20261231);
		const RRow *pr = find_row (p, 0, "PROFIT (LOSS) OF THE PERIOD"), *to = find_row (p, 0, "Turnover");
		CHECK (pr && !strcmp (pr->cell[2], M (profit)), "the income statement's result: %s", cell (pr, 2));
		CHECK (to && !strcmp (to->cell[2], "3.500,00"), "the turnover: %s", cell (to, 2));

		rpt_journal (b, p, -1, 20260101, 20261231);
		const RRow &jt = p.r[p.nr - 1];
		CHECK (!strcmp (jt.cell[5], jt.cell[6]), "the journals' debits and credits: %s / %s", jt.cell[5], jt.cell[6]);

		rpt_ledger (b, p, "4", "4", 20260101, 20261231, false);
		const RRow *h = find_row (p, 0, "400000  Clients");
		CHECK (h != 0, "the customers' account in the general ledger");
		const RRow *s = find_row (p, 2, "Total 400000");
		CHECK (s && !strcmp (s->cell[3], "3.861,00") && !strcmp (s->cell[4], "1.861,00") && !strcmp (s->cell[5], "2.000,00"), "400000's totals: %s %s %s", cell (s, 3), cell (s, 4), cell (s, 5));
		char dc[40]; dc_text ("-1.234,50", dc, sizeof dc); CHECK (!strcmp (dc, "1.234,50 C"), "a credit balance: %s", dc);
		dc_text ("12,00", dc, sizeof dc); CHECK (!strcmp (dc, "12,00 D"), "a debit balance: %s", dc);
		dc_text ("0,00", dc, sizeof dc); CHECK (!strcmp (dc, "0,00"), "no balance: %s", dc);

		rpt_party_balance (b, p, PK_CUSTOMER, 20261231);
		const RRow &pt = p.r[p.nr - 1];
		money cust = party_balance (b, C1) + party_balance (b, C2) + party_balance (b, C3);
		CHECK (!strcmp (pt.cell[5], M (cust)), "the customers' balance: %s (%s)", pt.cell[5], M (cust));

		rpt_aged (b, p, PK_CUSTOMER, 20260315);
		const RRow *st = find_row (p, 0, "Studio Nord GmbH");
		CHECK (st && !strcmp (st->cell[1], "") && !strcmp (st->cell[2], "2.000,00"), "Studio Nord 24 days late: %s / %s", cell (st, 1), cell (st, 2));

		rpt_party_ledger (b, p, C1, 20260101, 20261231);
		const RRow &plt = p.r[p.nr - 1];
		CHECK (!strcmp (plt.cell[6], "-121,00"), "the first customer's ledger ends at -121,00: %s", plt.cell[6]);

		rpt_vat_detail (b, p, 20260101, 20260331);
		const RRow *g3 = find_row (p, 3, "Grid 03");
		CHECK (g3 && !strcmp (g3->cell[6], "1.100,00"), "the VAT detail's grid 03: %s", cell (g3, 6));

		Out o; rpt_csv (p, o);
		CHECK (o.n > 0 && !strncmp (o.b, "Grid;Date;Document;Party;Account;Code;Amount\r\n", 46), "the CSV's header");
		CHECK (strstr (o.b, ";1100,00\r\n") != 0, "the CSV's amounts without the thousands' dots");
		rpt_free (p);
	}

	// ---- the listings -------------------------------------------------------------------------------------------------------------------
	{
		ListRow r[16];
		int n = client_listing (b, 2026, r, 16);
		CHECK (n == 1 && r[0].party == C1 && r[0].base == 140000 && r[0].tax == 21900, "the customer listing: %d rows, %s / %s", n, n ? M (r[0].base) : "", n ? M (r[0].tax) : "");
		Out o; client_listing_xml (b, 2026, r, n, o);
		CHECK (strstr (o.b, "<ns2:CompanyVATNumber issuedBy=\"BE\">0417497106</ns2:CompanyVATNumber>") != 0, "the customer's number in the listing");
		save (dir, "client_listing.xml", o);
		int bad = 0;
		n = intra_listing (b, 20260101, 20260331, r, 16, &bad);
		CHECK (n == 1 && r[0].party == C2 && r[0].kind == 'S' && r[0].base == 200000 && !bad, "the intra-community listing: %d rows", n);
		Out o2; intra_listing_xml (b, 2026, 1, false, r, n, o2);
		CHECK (strstr (o2.b, "issuedBy=\"DE\">123456789<") != 0 && strstr (o2.b, "<ns2:Code>S</ns2:Code>") != 0, "the EU customer in the listing");
		save (dir, "intra_listing.xml", o2);
		// a monthly return, nothing to declare but the one grid
		money z[100]; for (int i = 0; i < 100; i++) z[i] = 0;
		Out o3; vat_return_xml (b, 2026, 12, true, z, true, false, false, "Nothing this month", o3);
		CHECK (strstr (o3.b, "<ns2:Month>12</ns2:Month>") && strstr (o3.b, "GridNumber=\"71\">0.00<") && strstr (o3.b, "<ns2:ClientListingNihil>YES"), "a monthly return with nothing");
		save (dir, "vat_return_month.xml", o3);
	}

	// ---- the year's end: the next year opened, the result appropriated ------------------------------------------------------------------
	{
		int y2 = year_add_next (b);
		CHECK (y2 == 1 && b.yr[1].start == 20270101 && b.yr[1].end == 20271231, "the next year: %d..%d", b.yr[y2 < 0 ? 0 : y2].start, b.yr[y2 < 0 ? 0 : y2].end);
		Report p; rpt_init (p);
		rpt_balance_sheet (b, p, 20270131);
		const RRow *acc = find_row (p, 0, "Accumulated profits (losses)"), *res = find_row (p, 0, "Result of the year (not appropriated)");
		CHECK (acc && !strcmp (acc->cell[2], M (profit)) && res && !strcmp (res->cell[2], ""), "2027: the profit before, not appropriated yet: %s / %s", cell (acc, 2), cell (res, 2));
		const RRow *ta = find_row (p, 0, "TOTAL ASSETS"), *tl = find_row (p, 0, "TOTAL EQUITY AND LIABILITIES");
		CHECK (ta && tl && !strcmp (ta->cell[2], tl->cell[2]), "2027's balance sheet balances: %s / %s", cell (ta, 2), cell (tl, 2));
		rpt_trial (b, p, 20270131, false);
		const RRow &t = p.r[p.nr - 1];
		CHECK (!strcmp (t.cell[2], t.cell[3]), "2027's trial balance balances (the profit before on its line): %s / %s", t.cell[2], t.cell[3]);

		int ei = year_appropriate (b, 0);
		CHECK (ei >= 0 && b.e[ei].nl == 2 && seq (b.e[ei].l[0].account, "693000") && b.e[ei].l[0].amount == profit && seq (b.e[ei].l[1].account, "140000"), "the appropriation: 693000 / 140000");
		CHECK (b.e[ei].date == 20261231 && b.jr[b.e[ei].journal].type == JT_MISC, "on the year's last day, in the miscellaneous journal");
		CHECK (year_appropriate (b, 0) < 0, "nothing left to appropriate");
		rpt_balance_sheet (b, p, 20261231);
		res = find_row (p, 0, "Result of the year (not appropriated)"); acc = find_row (p, 0, "Accumulated profits (losses)");
		CHECK (res && !strcmp (res->cell[2], "") && acc && !strcmp (acc->cell[2], M (profit)), "2026 appropriated: %s / %s", cell (res, 2), cell (acc, 2));
		rpt_trial (b, p, 20270131, false);
		CHECK (!find_row (p, 1, "Profit of the years before, not appropriated"), "2027's trial balance: no line for it now");
		const RRow *k140 = find_row (p, 0, "140000");
		CHECK (k140 && !strcmp (k140->cell[3], M (profit)), "140000 brought forward: %s", cell (k140, 3));
		CHECK (income_result (b, 20270101, 20271231) == 0, "2027 starts from nothing");
		b.yr[0].closed = true;
		CHECK (entry_locked (b, b.e[ei])[0] != 0, "a closed year's entries are locked");
		Invoice v; inv_init (v); v.journal = JV; v.party = C1; v.date = 20261215;
		{ InvLine &l = inv_add (v); scpy (l.account, "700000", CODE_MAX); l.net = 100; l.vat = vat_find ("V21"); }
		CHECK (inv_check (b, v)[0] != 0, "no invoice in a closed year");
		inv_free (v);
		rpt_free (p);
	}
	book_clear (b);
	printf ("%d checks, %d failed\n", g_checks, g_fail);
	return g_fail ? 1 : 0;
}
