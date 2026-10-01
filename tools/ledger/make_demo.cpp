// make_demo.cpp -- Ledger's demo company (sdcard/docs/demo-company.ledger), made through Ledger's own engine
// (user/Apps/ledger/): Atelier Lumen SRL, a small Brussels studio of graphic design that also prints books,
// from January 2025 to the end of September 2026 -- its customers (Belgian companies, private persons, an
// EU agency, a Swiss one) and suppliers (the landlord, energy, telecom, paper from Belgium and Germany,
// software from Ireland, the car's lease, the accountant, insurance), its sales invoices (design at 21 %,
// books at 6 %, EU services reverse-charged), purchases, monthly bank statements that pay them (matched;
// some customers late, a few invoices still open), the quarters' VAT returns filed, settled and paid, the
// manager's remuneration, 2025's depreciation, its result appropriated and the year closed; its quotes,
// orders, a delivery note and a purchase order of the summer 2026 (commerce.h: one ordered, then
// delivered -- to be invoiced --, one sent, one in Dutch for a Flemish brewery, one expired, one refused);
// and the bank's next statement, of October 2nd, as a CODA file to import (demo-bank-statement.cod: an
// invoice paid with its structured communication, others by their customers' names, a supplier's direct
// debit found by its IBAN, the bank's charges, a transfer from an unknown party left to complete).
//
//   g++ -std=gnu++17 -O1 -I user -I kernel/include tools/ledger/make_demo.cpp -o /tmp/make_demo
//   /tmp/make_demo sdcard/docs/demo-company.ledger sdcard/docs/demo-bank-statement.cod
//
// (Every name, number and amount is made up; the VAT numbers and IBANs have valid check digits.)
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "Apps/ledger/setup.h"
#include "Apps/ledger/fileio.h"
#include "Apps/ledger/coda.h"

// (Ledger's words: English here -- wtk/lang.cpp is the apps')
const char *wk_tr (const char *s) { return s; }
const char *wk_trc (const char *, const char *s) { return s; }
using namespace lg;

static Book b;
static unsigned rng = 20260929u;
static int rnd (int n) { rng = rng * 1103515245u + 12345u; return (int) ((rng >> 8) % (unsigned) n); }
static money rmoney (money lo, money hi, money step = 100) { return lo + (money) rnd ((int) ((hi - lo) / step) + 1) * step; }

// A Belgian VAT number from eight digits: the check digits added.
static void be_vat (long d8, char *out)
{
	int chk = 97 - (int) (d8 % 97);
	snprintf (out, 20, "BE%08ld%02d", d8, chk);
}
// An IBAN with its check digits (BE + 12 digits: a bank's code, the account, its check modulo 97).
static void be_iban (long long bank10, char *out)
{
	int c2 = (int) (bank10 % 97); if (!c2) c2 = 97;
	char bban[16]; snprintf (bban, sizeof bban, "%010lld%02d", bank10, c2);
	// check digits: 98 - (bban + "BE00" as digits) mod 97
	char t[40]; snprintf (t, sizeof t, "%s111400", bban);
	int r = 0; for (const char *p = t; *p; p++) r = (r * 10 + (*p - '0')) % 97;
	snprintf (out, 20, "BE%02d%s", 98 - r, bban);
}

struct P { int id; int terms; int lateness; };	// lateness: the days it pays after the due date (-1: pays early...)
static int party (int kind, const char *name, const char *street, const char *zip, const char *city, const char *country, const char *vat,
		  int regime, int terms, const char *email, const char *phone, const char *defAcc, const char *defVat)
{
	Party &p = party_add (b);
	p.kind = (unsigned char) kind; scpy (p.name, name, NAME_MAX); scpy (p.street, street, NAME_MAX); scpy (p.zip, zip, 12); scpy (p.city, city, 48);
	scpy (p.country, country, 4); scpy (p.vat, vat, 20); p.regime = (unsigned char) regime; p.terms = (short) terms;
	scpy (p.email, email, 72); scpy (p.phone, phone, 24); scpy (p.defAcc, defAcc, CODE_MAX); scpy (p.defVat, defVat, 8);
	party_make_code (b, kind, name, p.code);
	if (kind == PK_SUPPLIER) { char ib[24]; be_iban (3630000000LL + p.id * 7919LL, ib); scpy (p.iban, ib, 36); }
	return p.id;
}

// A commercial document (its lines: a text, a quantity in thousandths, a unit price) -> its id.
struct CL { const char *text; long long qty; money price; };
static int cdoc_make (int kind, int partyId, int date, int until, int status, const char *text, const char *ref, int from, const CL *lines, int n)
{
	CDoc d; cdoc_init (d);
	d.kind = kind; d.party = partyId; d.date = date; d.until = until; d.status = status; d.from = from;
	sset (d.text, text); scpy (d.ref, ref, sizeof d.ref);
	const Party *p = party_of (b, partyId);
	bool sale = cd_sale (kind);
	for (int i = 0; i < n; i++)
	{
		CLine &l = cdoc_add_line (d);
		sset (l.text, lines[i].text); l.qty = lines[i].qty; l.price = lines[i].price;
		l.vat = (signed char) vat_default (b, p, sale, p && p->defAcc[0] ? p->defAcc : sale ? "700000" : "604000");
	}
	int i = cdoc_save (b, d);
	return b.cd[i].id;
}
// A name in capitals, as banks write them (Latin-1's accented letters too).
static void upper_name (const char *s, char *out)
{
	int u = 0;
	for (const unsigned char *z = (const unsigned char *) s; *z && u < 35; z++)
		out[u++] = (char) ((*z >= 'a' && *z <= 'z') || (*z >= 0xE0 && *z <= 0xFE && *z != 0xF7) ? *z - 32 : *z);
	out[u] = '\0';
}
static void set_lang (int partyId, const char *lang) { int i = party_index (b, partyId); if (i >= 0) scpy (b.pty[i].lang, lang, sizeof b.pty[i].lang); }

// ---- documents --------------------------------------------------------------------------------------------------------------
struct Due { int entry, line, pay; money amount; };	// an invoice's party line, the day it will be paid (0: not)
static Due dues[2000]; static int ndue;
static int JV, JA, JB, JO;

static int post_invoice (int journal, int partyId, int date, bool credit, const char *text, const char *ref,
			 const char *acc1, money net1, const char *vat1, const char *t1,
			 const char *acc2 = 0, money net2 = 0, const char *vat2 = 0, const char *t2 = 0)
{
	Invoice v; inv_init (v);
	v.journal = journal; v.party = partyId; v.date = date; v.due = inv_due_of (b, partyId, date); v.credit = credit;
	if (credit) v.due = date;
	scpy (v.text, text, sizeof v.text); scpy (v.ref, ref ? ref : "", sizeof v.ref);
	{ InvLine &l = inv_add (v); scpy (l.account, acc1, CODE_MAX); l.net = net1; l.vat = vat_find (vat1); scpy (l.text, t1 ? t1 : "", 80); }
	if (acc2) { InvLine &l = inv_add (v); scpy (l.account, acc2, CODE_MAX); l.net = net2; l.vat = vat_find (vat2); scpy (l.text, t2 ? t2 : "", 80); }
	const char *w = inv_check (b, v);
	if (w[0]) { fprintf (stderr, "refused (%d): %s\n", date, w); exit (1); }
	Entry e; inv_to_entry (b, v, e);
	int i = entry_save (b, e);
	Entry &s = b.e[i];
	if (b.jr[journal].type == JT_SALES && !credit) { Invoice t; inv_init (t); t.date = s.date; t.no = s.no; t.journal = s.journal; inv_make_comm (b, t, s.comm); }
	inv_free (v);
	return s.id;
}
// The day an invoice gets paid: its due date and the party's habit (some never, within the demo).
static void plan_payment (int id, int lateness, bool never = false)
{
	int i = entry_index (b, id);
	const Entry &e = b.e[i];
	for (int k = 0; k < e.nl; k++)
		if (e.l[k].party && acc_party (b, e.l[k].account))
		{
			Due &d = dues[ndue++];
			d.entry = id; d.line = k; d.amount = e.l[k].amount;
			d.pay = never ? 0 : date_add (e.l[k].due ? e.l[k].due : e.date, lateness + rnd (7) - 3);
			if (d.pay < e.date) d.pay = date_add (e.date, 2);
			break;
		}
}

// ---- the statements ---------------------------------------------------------------------------------------------------------------
struct Move { int party; const char *account; money amount; char text[80]; int payEntry, payLine, day; };
static Move moves[400]; static int nmoves;
static void move (int day, int partyId, const char *account, money amount, const char *text, int payEntry = 0, int payLine = 0)
{
	Move &m = moves[nmoves++];
	m.party = partyId; m.account = account; m.amount = amount; scpy (m.text, text, sizeof m.text); m.payEntry = payEntry; m.payLine = payLine; m.day = day;
}
static int stmtNo;
static void statement (int date)
{
	if (!nmoves) return;
	Statement s; st_init (s);
	s.journal = JB; s.date = date;
	char t[48]; snprintf (t, sizeof t, "Statement %d", ++stmtNo); scpy (s.text, t, sizeof s.text);
	s.old = fin_balance_before (b, JB, date);
	for (int i = 0; i < nmoves; i++)
	{
		StLine &l = st_add (s);
		l.party = moves[i].party; if (!l.party) scpy (l.account, moves[i].account, CODE_MAX);
		l.amount = moves[i].amount; scpy (l.text, moves[i].text, sizeof l.text);
		if (moves[i].payEntry) { l.pay[0].entry = moves[i].payEntry; l.pay[0].line = moves[i].payLine; l.npay = 1; }
	}
	s.now = s.old + st_sum (s);
	const char *w = st_check (b, s);
	if (w[0]) { fprintf (stderr, "statement refused (%d): %s\n", date, w); exit (1); }
	Entry e; st_to_entry (b, s, e);
	int i = entry_save (b, e);
	st_apply_matches (b, i, s);
	st_free (s);
	nmoves = 0;
}
static void misc (int date, const char *text, const char *a1, money m1, const char *a2, money m2, const char *a3 = 0, money m3 = 0, unsigned flags = 0)
{
	Entry e; entry_init (e);
	e.journal = JO; e.date = date; e.due = date; e.flags = flags; sset (e.text, text);
	Line &x = entry_add_line (e); scpy (x.account, a1, CODE_MAX); x.amount = m1; sset (x.text, text);
	Line &y = entry_add_line (e); scpy (y.account, a2, CODE_MAX); y.amount = m2; sset (y.text, text);
	if (a3) { Line &z = entry_add_line (e); scpy (z.account, a3, CODE_MAX); z.amount = m3; sset (z.text, text); }
	const char *w = misc_check (b, e);
	if (w[0]) { fprintf (stderr, "operation refused (%d %s): %s\n", date, text, w); exit (1); }
	entry_save (b, e);
}
// A quarter's return: filed on the 18th of the next month, settled, paid on the 19th.
struct Filing { int year, q; };
static void file_quarter (int year, int q)
{
	int f, t; period_range (year, q, false, &f, &t);
	Entry st;
	if (vat_settlement (b, year, q, false, st)) entry_save (b, st); else entry_free (st);
	VatReturn &r = return_add (b);
	r.year = year; r.period = q; r.monthly = 0;
	int next = date_add (t, 1);
	r.filed = ymd (y_of (next), m_of (next), 18);
	vat_grids (b, f, t, r.grid);
}

int main (int argc, char **argv)
{
	const char *out = argc > 1 ? argv[1] : "demo-company.ledger";
	book_init (b);
	book_new (b, "fr", 20250101, 20251231);
	year_add_next (b);
	scpy (b.name, "Atelier Lumen SRL", NAME_MAX); scpy (b.legal, "SRL", sizeof b.legal);
	scpy (b.street, "Rue des Tanneurs 58", NAME_MAX); scpy (b.zip, "1000", 12); scpy (b.city, "Bruxelles", 48);
	be_vat (7215830L, b.vat);
	scpy (b.email, "compta@atelier-lumen.be", 72); scpy (b.phone, "+32 2 512 44 90", 24);
	be_iban (3630812345LL, b.iban); scpy (b.bic, "BBRUBEBB", 12);
	scpy (b.reg, "RPM Bruxelles", sizeof b.reg); scpy (b.web, "www.atelier-lumen.be", sizeof b.web);
	scpy (b.jr[2].iban, b.iban, 36);
	JV = jrn_find (b, "VEN"); JA = jrn_find (b, "ACH"); JB = jrn_find (b, "BNK"); JO = jrn_find (b, "OD");

	char v[24];
	// ---- the customers -------------------------------------------------------------------------------------------------------------
	struct C { const char *name, *street, *zip, *city, *country; long vat; int regime, terms, lateness; const char *email, *acc, *code; };
	static const C CUST[] = {
		{ "Brasserie du Sablon SRL", "Place du Grand Sablon 12", "1000", "Bruxelles", "BE", 7453211L, PR_BE, 30, 6, "info@brasseriedusablon.be", "700100", "" },
		{ "Librairie Tropiques SA", "Rue de Flandre 44", "1000", "Bruxelles", "BE", 4281377L, PR_BE, 30, 0, "commandes@tropiques.be", "700000", "" },
		{ "Chocolaterie Van Hove SRL", "Chauss\xE9" "e de Wavre 210", "1050", "Ixelles", "BE", 5620198L, PR_BE, 30, 12, "marketing@vanhove-choco.be", "700100", "" },
		{ "Studio Kaa SRL", "Quai de la Batte 3", "4000", "Li\xE8" "ge", "BE", 8120045L, PR_BE, 45, 20, "hello@studiokaa.be", "700100", "" },
		{ "Brouwerij De Klok NV", "Kaai 7", "2000", "Antwerpen", "BE", 4391276L, PR_BE, 30, 3, "boekhouding@deklok.be", "700100", "" },
		{ "Editions du Pr\xE9" "au SA", "Rue du Pr\xE9" "au 9", "5000", "Namur", "BE", 6690231L, PR_BE, 60, 2, "fabrication@editionsdupreau.be", "700000", "" },
		{ "Th\xE9\xE2" "tre du Canal ASBL", "Rue Dansaert 70", "1000", "Bruxelles", "BE", 0L, PR_PRIVATE, 30, 35, "admin@theatreducanal.be", "700100", "" },
		{ "Mme Claire Dubois", "Avenue Brugmann 301", "1180", "Uccle", "BE", 0L, PR_PRIVATE, 15, 0, "claire.dubois@mail.be", "700100", "" },
		{ "Agence Lumi\xE8" "re Paris SARL", "Rue Oberkampf 88", "75011", "Paris", "FR", -1L, PR_EU, 30, 8, "compta@agence-lumiere.fr", "700610", "" },
		{ "Studio Noord BV", "Prinsengracht 412", "1016", "Amsterdam", "NL", -2L, PR_EU, 30, 1, "finance@studionoord.nl", "700610", "" },
		{ "Design Z\xFC" "rich AG", "Langstrasse 21", "8004", "Z\xFC" "rich", "CH", -3L, PR_WORLD, 30, 10, "office@design-zuerich.ch", "700700", "" },
		{ "Mus\xE9" "e de la Photographie", "Avenue Paul Pastur 11", "6032", "Charleroi", "BE", 4150023L, PR_BE, 45, 25, "mediation@museephoto.be", "700100", "" } };
	enum { NC = sizeof CUST / sizeof CUST[0] };
	int cust[NC]; int custLate[NC];
	for (int i = 0; i < NC; i++)
	{
		const C &c = CUST[i];
		if (c.vat > 0) be_vat (c.vat, v);
		else if (c.vat == -1) scpy (v, "FR40303265045", 24);
		else if (c.vat == -2) scpy (v, "NL859290236B01", 24);
		else if (c.vat == -3) scpy (v, "CHE123456789", 24);
		else v[0] = '\0';
		cust[i] = party (PK_CUSTOMER, c.name, c.street, c.zip, c.city, c.country, v, c.regime, c.terms, c.email, "", c.acc, "");
		custLate[i] = c.lateness;
	}
	// ---- the suppliers --------------------------------------------------------------------------------------------------------------
	be_vat (4427812L, v); int sRent = party (PK_SUPPLIER, "Immo Tanneurs SA", "Rue des Tanneurs 60", "1000", "Bruxelles", "BE", v, PR_BE, 0, "gestion@immotanneurs.be", "", "610000", "A0");
	be_vat (4032213L, v); int sEnergy = party (PK_SUPPLIER, "Lumin\xE9" "a Energie SA", "Boulevard Simon Bolivar 36", "1000", "Bruxelles", "BE", v, PR_BE, 15, "clients@luminea.be", "", "612500", "A21");
	be_vat (2027739L, v); int sTel = party (PK_SUPPLIER, "Telenova SA", "Boulevard du Roi Albert II 27", "1030", "Schaerbeek", "BE", v, PR_BE, 20, "factures@telenova.be", "", "616200", "A21");
	be_vat (4219988L, v); int sPaper = party (PK_SUPPLIER, "Papeteries de Genval SA", "Rue de la Papeterie 5", "1332", "Genval", "BE", v, PR_BE, 30, "ventes@papgenval.be", "", "604000", "A21");
	int sGerman = party (PK_SUPPLIER, "Papierfabrik Ahlen GmbH", "Industriestra\xDF" "e 14", "59229", "Ahlen", "DE", "DE811907980", PR_EU, 30, "export@papierfabrik-ahlen.de", "", "604000", "AEU21");
	int sSoft = party (PK_SUPPLIER, "Pixelworks Software Ltd", "4 Grand Canal Square", "D02", "Dublin", "IE", "IE6388047V", PR_EU, 0, "billing@pixelworks.ie", "", "613310", "AEUS21");
	be_vat (4035610L, v); int sLease = party (PK_SUPPLIER, "Autolease Belgium SA", "Avenue de Tervueren 270", "1150", "Woluwe-Saint-Pierre", "BE", v, PR_BE, 15, "invoices@autolease.be", "", "610220", "A21D50");
	be_vat (6510045L, v); int sAcct = party (PK_SUPPLIER, "Fiduciaire Martin SRL", "Rue Royale 150", "1000", "Bruxelles", "BE", v, PR_BE, 30, "cabinet@fidumartin.be", "", "613200", "A21");
	be_vat (4539004L, v); int sIns = party (PK_SUPPLIER, "Assurances du Midi SA", "Rue du Midi 101", "1000", "Bruxelles", "BE", v, PR_BE, 30, "pro@assurancesdumidi.be", "", "614600", "A0");
	be_vat (4118920L, v); int sPress = party (PK_SUPPLIER, "Offset Benelux SA", "Zoning Nord 8", "1400", "Nivelles", "BE", v, PR_BE, 30, "info@offsetbenelux.be", "", "230000", "A21");
	be_vat (4602345L, v); int sPrint = party (PK_SUPPLIER, "Imprimerie Snel SRL", "Rue de l'Industrie 22", "4400", "Fl\xE9" "malle", "BE", v, PR_BE, 30, "devis@snel.be", "", "603000", "A21");
	(void) sPrint;

	// ---- the opening balances (1 January 2025) --------------------------------------------------------------------------------------
	{
		Entry e; entry_init (e); e.journal = JO; e.date = 20250101; e.due = 20250101; e.flags = EF_OPENING; sset (e.text, "Opening balances");
		struct O { const char *a; money m; } O_[] = { { "240000", 1200000 }, { "240009", -480000 }, { "550000", 2134000 }, { "100000", -1860000 }, { "140000", -994000 } };
		for (auto &o : O_) { Line &l = entry_add_line (e); scpy (l.account, o.a, CODE_MAX); l.amount = o.m; sset (l.text, "Opening balances"); }
		entry_save (b, e);
	}

	// ---- month by month -------------------------------------------------------------------------------------------------------------
	static const char *DESIGN[] = { "Visual identity", "Packaging design", "Brochure layout", "Website design", "Poster series", "Menu design", "Catalogue",
					"Annual report layout", "Exhibition graphics", "Label design", "Photo retouching", "Illustrations" };
	static const char *BOOKS[] = { "Printed books (500 copies)", "Art book reprint", "Exhibition catalogues", "Poetry collection (300 copies)" };
	int nCredit = 0;
	for (int ym = 0; ym < 21; ym++)				// January 2025 .. September 2026
	{
		int y = 2025 + ym / 12, m = ym % 12 + 1, last = month_end (y, m);
		if (y == 2026 && m == 9) last = 20260928;
		// the sales: 5 to 8 invoices
		int ns = 5 + rnd (4);
		int days[16], cis[16];
		for (int k = 0; k < ns; k++)
		{
			cis[k] = rnd (NC); days[k] = 2 + rnd (26);
			if (ymd (y, m, days[k]) > last) days[k] = 1 + rnd (d_of (last));
		}
		for (int i = 1; i < ns; i++) for (int j = i; j > 0 && days[j] < days[j - 1]; j--) { int t = days[j]; days[j] = days[j - 1]; days[j - 1] = t; t = cis[j]; cis[j] = cis[j - 1]; cis[j - 1] = t; }
		for (int k = 0; k < ns; k++)
		{
			int ci = cis[k], day = days[k];
			int d = ymd (y, m, day);
			const C &c = CUST[ci];
			int id;
			if (seq (c.acc, "700000") && rnd (3))			// a bookseller, a publisher: books
			{
				money n1 = rmoney (120000, 400000, 1000);
				id = post_invoice (JV, cust[ci], d, false, BOOKS[rnd (4)], 0, "700000", n1, "V6", 0, rnd (2) ? "700100" : 0, rmoney (40000, 120000), "V21", "Cover design");
			}
			else
			{
				const char *vc = c.regime == PR_EU ? "VEUS" : c.regime == PR_WORLD ? "VEX" : "V21";
				money n1 = rmoney (40000, 280000, 500);
				const char *what = DESIGN[rnd (12)];
				id = post_invoice (JV, cust[ci], d, false, what, 0, c.acc, n1, vc, what, rnd (4) == 0 ? c.acc : 0, rmoney (8000, 45000), vc, "Printing proofs");
			}
			bool never = (y == 2026 && m >= 7 && (ci == 3 || ci == 6 || ci == 11)) || (y == 2026 && m == 9 && rnd (2));
			plan_payment (id, custLate[ci], never);
		}
		// a credit note now and then
		if ((ym == 4 || ym == 10 || ym == 19) && ndue)
		{
			int ci = ym == 19 ? 0 : 4;
			int id = post_invoice (JV, cust[ci], ymd (y, m, 20), true, "Discount granted on a late delivery", 0, CUST[ci].acc, 12000 + 5000 * nCredit++, "V21", "Discount");
			plan_payment (id, 0, ym == 19);
		}
		// the purchases
		int rentId = post_invoice (JA, sRent, ymd (y, m, 1), false, "Rent", 0, "610000", 145000, "A0", "Studio rent", 0, 0, 0, 0);
		plan_payment (rentId, 0);
		int eId = post_invoice (JA, sEnergy, ymd (y, m, 8), false, "Electricity and gas", 0, "612500", rmoney (18000, m >= 11 || m <= 2 ? 42000 : 26000), "A21", 0);
		plan_payment (eId, 0);
		int tId = post_invoice (JA, sTel, ymd (y, m, 5), false, "Internet and phones", 0, "616200", 8990, "A21", 0);
		plan_payment (tId, -2);
		int lId = post_invoice (JA, sLease, ymd (y, m, 3), false, "Car lease", 0, "610220", 56000, "A21D50", 0);
		plan_payment (lId, 0);
		int sId = post_invoice (JA, sSoft, ymd (y, m, 2), false, "Design software subscription", 0, "613310", 11900, "AEUS21", 0);
		plan_payment (sId, 0);
		if (m % 2 == 1) { int pId = post_invoice (JA, sPaper, ymd (y, m, 12 + rnd (8)), false, "Paper", 0, "604000", rmoney (60000, 240000), "A21", 0); plan_payment (pId, 1); }
		if (m % 3 == 2) { int gId = post_invoice (JA, sGerman, ymd (y, m, 16), false, "Art paper, 2 pallets", 0, "604000", rmoney (220000, 380000), "AEU21", 0); plan_payment (gId, 0); }
		if (m % 3 == 0) { int aId = post_invoice (JA, sAcct, ymd (y, m, 25), false, "Accounting, the quarter", 0, "613200", 95000, "A21", 0); plan_payment (aId, 2); }
		if (m == 1) { int iId = post_invoice (JA, sIns, ymd (y, m, 10), false, "Professional insurance, the year", 0, "614600", 186000, "A0", 0); plan_payment (iId, 0); }
		if (y == 2025 && m == 3) { int xId = post_invoice (JA, sPress, 20250314, false, "Digital press Heidel XR", 0, "230000", 950000, "A21", 0); plan_payment (xId, 0); }
		// the purchases' supplier numbers
		for (int i = b.ne - 1; i >= 0 && b.e[i].date >= ymd (y, m, 1); i--)
			if (b.e[i].journal == JA && !b.e[i].ref[0]) snprintf (b.e[i].ref, sizeof b.e[i].ref, "F%d-%04d", y, 1000 + rnd (9000));
		// the quarter's VAT: filed, settled, paid
		if (m == 1 || m == 4 || m == 7 || m == 10)
		{
			int py = m == 1 ? y - 1 : y, pq = m == 1 ? 4 : (m - 1) / 3;
			if (py >= 2025) file_quarter (py, pq);
		}
		// the bank: what falls due this month, the manager's remuneration, the fees, the VAT paid
		for (int i = 0; i < ndue; i++)
		{
			Due &d = dues[i];
			if (!d.pay || d.pay < ymd (y, m, 1) || d.pay > last) continue;
			int ei = entry_index (b, d.entry);
			const Entry &e = b.e[ei];
			char t[80]; char r[32]; entry_number (b, e, r, sizeof r);
			if (b.jr[e.journal].type == JT_SALES) snprintf (t, sizeof t, "Payment %s", e.comm[0] ? r : r);
			else snprintf (t, sizeof t, "%s %s", party_name (b, e.party), e.ref);
			move (d_of (d.pay), e.party, 0, d.amount, t, d.entry, d.line);
		}
		move (28, 0, "618000", -450000, "Manager's remuneration");
		move (28, 0, "657200", -(money) (850 + rnd (400)), "Bank charges");
		if (m == 1 || m == 4 || m == 7 || m == 10)
		{
			// the VAT due (451200's balance) paid on the 19th
			money due = 0;
			for (int i = 0; i < b.ne; i++) for (int k = 0; k < b.e[i].nl; k++) if (seq (b.e[i].l[k].account, "451200")) due += b.e[i].l[k].amount;
			if (due < 0) move (14, 0, "451200", due, "VAT return, payment");
			money rec = 0;
			for (int i = 0; i < b.ne; i++) for (int k = 0; k < b.e[i].nl; k++) if (seq (b.e[i].l[k].account, "411200")) rec += b.e[i].l[k].amount;
			if (rec > 0 && m != 1) move (14, 0, "411200", rec, "VAT refund");
		}
		// the statements: the month's moves, the first half's on the 15th, the rest at its end
		{
			static Move all[400]; int n = nmoves; for (int i = 0; i < n; i++) all[i] = moves[i];
			nmoves = 0;
			for (int i = 0; i < n; i++) if (all[i].day <= 15) moves[nmoves++] = all[i];
			statement (ymd (y, m, 15 < d_of (last) ? 15 : d_of (last)));
			for (int i = 0; i < n; i++) if (all[i].day > 15) moves[nmoves++] = all[i];
			statement (last);
		}
		// the year's end: depreciation, the result appropriated, the year closed
		if (y == 2025 && m == 12)
		{
			misc (20251231, "Depreciation 2025: office furniture and equipment", "630200", 240000, "240009", -240000);
			misc (20251231, "Depreciation 2025: digital press (5 years, 9 months)", "630200", 142500, "230009", -142500);
		}
	}
	// 2025 closed in March 2026 (its VAT returns filed; the accountant's work)
	year_appropriate (b, 0);
	b.yr[0].closed = true;
	// ---- the parties' languages, the commercial documents of the summer 2026 ------------------------------------------------------
	set_lang (cust[4], "nl"); set_lang (cust[9], "nl"); set_lang (cust[10], "en"); set_lang (sGerman, "en"); set_lang (sSoft, "en");
	{
		static const CL q1[] = { { "Cr\xE9" "ation graphique de la nouvelle carte (recto verso)", 2500, 45000 }, { "Impression de 200 cartes, A4, papier 300 g", 200000, 185 } };
		int q = cdoc_make (CD_QUOTE, cust[0], 20260820, 20260919, CS_DONE, "La nouvelle carte du restaurant", "", 0, q1, 2);
		int o = cdoc_make (CD_ORDER, cust[0], 20260825, 20260910, CS_DONE, "La nouvelle carte du restaurant", "Bon 2026-118", q, q1, 2);
		cdoc_make (CD_DELIVERY, cust[0], 20260910, 0, CS_DRAFT, "La nouvelle carte du restaurant", "Bon 2026-118", o, q1, 2);
		static const CL q2[] = { { "\xC9" "tude et maquettes de l'emballage de No\xEB" "l", 1000, 180000 }, { "D\xE9" "clinaison en trois formats", 3000, 35000 },
					  { "Bons \xE0 tirer et suivi de fabrication", 1000, 42000 } };
		cdoc_make (CD_QUOTE, cust[2], 20260915, 20261015, CS_SENT, "Le packaging de No\xEB" "l", "", 0, q2, 3);
		static const CL q3[] = { { "Ontwerp van het etiket voor het winterbier", 1000, 120000 }, { "Drukproeven op etiketpapier", 2000, 8500 },
					  { "Aanpassing voor de flessen van 75 cl", 1000, 30000 } };
		cdoc_make (CD_QUOTE, cust[4], 20260922, 20261022, CS_DRAFT, "Etiketten voor het winterbier", "", 0, q3, 3);
		static const CL q4[] = { { "Maquettes des pages du site vitrine", 6000, 32000 }, { "Int\xE9gration et mise en ligne", 1000, 95000 } };
		cdoc_make (CD_QUOTE, cust[3], 20260701, 20260731, CS_SENT, "La refonte du site vitrine", "", 0, q4, 2);
		static const CL q5[] = { { "Mise en page du catalogue de l'exposition (96 pages)", 96000, 3800 }, { "Couverture et jaquette", 1000, 65000 } };
		cdoc_make (CD_QUOTE, cust[11], 20260610, 20260710, CS_REFUSED, "Le catalogue de l'exposition d'automne", "", 0, q5, 2);
		static const CL o2[] = { { "R\xE9impression du catalogue d'automne, 500 ex., 48 pages", 500000, 320 }, { "Fa\xE7onnage, dos carr\xE9 coll\xE9", 500000, 65 } };
		cdoc_make (CD_ORDER, cust[5], 20260918, 20261005, CS_ACCEPTED, "R\xE9impression du catalogue d'automne", "BC 2026/311", 0, o2, 2);
		static const CL p1[] = { { "Papier couch\xE9 mat 300 g, SRA3 (ramettes de 125)", 20000, 4250 }, { "Papier offset 120 g, A4 (ramettes de 500)", 10000, 1890 } };
		cdoc_make (CD_PORDER, sPaper, 20260924, 20261001, CS_SENT, "Papier pour les commandes d'octobre", "", 0, p1, 2);
	}
	// ---- the bank's next statement, as a CODA file ------------------------------------------------------------------------------
	if (argc > 2)
	{
		CodaStmt c; coda_init (c);
		scpy (c.iban, b.jr[JB].iban, sizeof c.iban); scpy (c.cur, "EUR", 4); scpy (c.holder, "ATELIER LUMEN SRL", sizeof c.holder);
		int nst = 1, last = 0; for (int i = 0; i < b.ne; i++) if (b.e[i].journal == JB) { nst++; if (b.e[i].date > last) last = b.e[i].date; }
		c.paper = nst; c.seq = nst % 1000; c.oldDate = last; c.newDate = 20261002;
		c.oldBal = fin_balance_before (b, JB, 20261002);
		// the open items: a customer's with a structured communication, customers' by name, a supplier's
		struct O { int e, l; } cs[16]; int ncs = 0; O sp = { -1, -1 };
		for (int i = 0; i < b.ne; i++) for (int k = 0; k < b.e[i].nl; k++)
		{
			const Line &x = b.e[i].l[k];
			if (!x.party || x.match || !acc_party (b, x.account)) continue;
			if (x.amount > 0 && ncs < 16) { cs[ncs].e = i; cs[ncs].l = k; ncs++; }
			else if (x.amount < 0 && sp.e < 0) { sp.e = i; sp.l = k; }
		}
		int nm = 0;
		for (int q = 0; q < ncs && nm < 3; q++)
		{
			const Entry &e = b.e[cs[q].e]; const Line &x = e.l[cs[q].l];
			const Party *p = party_of (b, x.party);
			CodaMove &m = coda_add (c);
			m.amount = x.amount; m.date = m.value = 20261001 + nm / 2;
			char r[24]; snprintf (r, sizeof r, "OL%08d%04d", 26100100 + nm, 1000 + nm); scpy (m.ref, r, sizeof m.ref);
			char up_[40]; upper_name (p->name, up_);
			scpy (m.cpName, up_, sizeof m.cpName);
			if (nm == 0 && e.comm[0]) scpy (m.ogm, e.comm, sizeof m.ogm);
			else { char n[32]; entry_number (b, e, n, sizeof n); snprintf (m.comm, sizeof m.comm, "Facture %s", n); }
			scpy (m.code, "00150000", sizeof m.code);
			nm++;
		}
		if (sp.e >= 0)								// a supplier's direct debit
		{
			const Entry &e = b.e[sp.e]; const Line &x = e.l[sp.l];
			const Party *p = party_of (b, x.party);
			CodaMove &m = coda_add (c);
			m.amount = x.amount; m.date = m.value = 20261002;
			scpy (m.ref, "DD26100200017", sizeof m.ref); scpy (m.cpIban, p->iban, sizeof m.cpIban); scpy (m.cpBic, "GKCCBEBB", sizeof m.cpBic);
			char up_[40]; upper_name (p->name, up_);
			scpy (m.cpName, up_, sizeof m.cpName);
			snprintf (m.comm, sizeof m.comm, "Domiciliation europeenne %s", e.ref[0] ? e.ref : "facture");
			scpy (m.code, "00501000", sizeof m.code);
		}
		{ CodaMove &m = coda_add (c); m.amount = -1240; m.date = m.value = 20261002; scpy (m.ref, "FR26100200001", sizeof m.ref);
		  scpy (m.comm, "Frais de gestion du compte, septembre", sizeof m.comm); scpy (m.code, "08037000", sizeof m.code); }
		{ CodaMove &m = coda_add (c); m.amount = 25000; m.date = m.value = 20261002; scpy (m.ref, "OL261002001999", sizeof m.ref);
		  scpy (m.cpName, "JANSSENS PIETER", sizeof m.cpName); scpy (m.cpIban, "BE43068999999501", sizeof m.cpIban);
		  scpy (m.comm, "Acompte stage de reliure", sizeof m.comm); scpy (m.code, "00150000", sizeof m.code); }
		c.newBal = c.oldBal; for (int i = 0; i < c.nm; i++) c.newBal += c.m[i].amount;
		Out co; coda_write (c, "BBRUBEBB", "00721583097", 20261002, true, true, co);
		FILE *cf = fopen (argv[2], "wb"); if (!cf) { perror (argv[2]); return 1; }
		fwrite (co.b, 1, co.n, cf); fclose (cf);
		printf ("%s: statement %d, %d movements\n", argv[2], c.paper, c.nm);
		coda_free (c);
	}
	Out o; book_write (b, o);
	FILE *f = fopen (out, "wb"); if (!f) { perror (out); return 1; }
	fwrite (o.b, 1, o.n, f); fclose (f);
	money g[100]; vat_grids (b, 20260701, 20260930, g);
	int open = 0; for (int i = 0; i < b.ne; i++) for (int k = 0; k < b.e[i].nl; k++) if (b.e[i].l[k].party && !b.e[i].l[k].match && acc_party (b, b.e[i].l[k].account)) open++;
	char a[32]; fmt_money (year_result (b, 0), a);
	printf ("%s: %d entries, %d parties, %d VAT returns, %d open items; 2025's result %s\n", out, b.ne, b.npty, b.nret, open, a);
	return 0;
}
