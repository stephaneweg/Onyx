//
// vat.h -- Belgian VAT: the codes a line takes, the periodic return's grids, the Intervat XML files.
//
// THE CODES. Each says which grids of the return a line fills -- the model of Odoo's l10n_be (its
// taxes' "tags"), a reference Belgian setup: a grid's amount is the lines' balances (a debit
// positive) times the grid's sign (sales' grids and the taxes due take the credits: 00-47, 54-57,
// 61, 63, 84, 85; the others the debits: 48, 49, 59, 62, 64, 81-83, 86-88). A code gives, for an
// operation and for a credit note, the grids of its base, of its tax (charged on a sale, deductible
// on a purchase; the part not deductible is a cost and counts with the base) and of the tax a reverse
// charge makes due. "N" stands for the purchase's nature: 81 (goods), 82 (services, the default), 83
// (investments), by the account (model.h: acc_nature). Whether a line is an operation or a credit
// note follows from its sign: a sale's base credited, a purchase's base debited is an operation.
//   Sales: 21 / 12 / 6 % (03 / 02 / 01, tax 54; credit notes 49, tax 64), 0 % (00), the Belgian co-
// contractor's (45), intra-EU goods (46, listing L), services (44, S), triangular (46, T; credit notes
// 48), exports (47), exempt art. 44 (none). Purchases: 21 / 12 / 6 / 0 % (N, tax 59; credit notes
// N less and 85, tax 63), 21 % half deductible (cars), not deductible (receptions); reverse charge:
// intra-EU goods (N + 86, tax due 55 and deductible 59; credit notes 84), intra-EU services (N + 88),
// the Belgian co-contractor's (N + 87, due 56), services from outside the EU (N + 87, 56), imports
// deferred with an ET 14000 licence (N + 87, 57). Regularisations: 61 (for the State), 62 (for the
// declarant).
//
// THE RETURN (vat_grids): a period's lines, grid by grid; then XX = 54 + 55 + 56 + 57 + 61 + 63,
// YY = 59 + 62 + 64, 71 = XX - YY (due) or 72 = YY - XX (to recover). THE FILES (Intervat's XML,
// schemas NewTVA-in, NewLK-in, NewICO-in v0.9 -- SPF Finances): the periodic return
// (VATConsignment), the annual customer listing (ClientListingConsignment: the Belgian taxable
// customers, 250 EUR and more), the intra-community listing (IntraConsignment: the EU customers, by
// kind L / S / T). Amounts "1234.56", never negative in the return; grid numbers as integers ("3"
// for 03); the VAT numbers' ten digits (the Belgian ones) or their number without its prefix.
//
#ifndef _ledger_vat_h
#define _ledger_vat_h

#include "model.h"

namespace lg {

// ---- the codes ----------------------------------------------------------------------------------------------------
// base / tax / due: "operation|credit note" -- grids by spaces, N the nature's.
struct VatDef
{
	const char *code, *name;
	unsigned char side; int rate; unsigned char deduct, due;		// due: the reverse charge's grid (55, 56, 57; 0: none)
	const char *base, *tax, *dueg;
	char listing;							// 'L', 'S', 'T' (intra-community), 'C' (customer listing), 0
};
static const VatDef VAT_DEFS[] = {
	// sales
	{ "V21",    "Sales 21 %",                                  VS_SALES, 2100, 0, 0,  "03|49", "54|64", "", 'C' },
	{ "V12",    "Sales 12 %",                                  VS_SALES, 1200, 0, 0,  "02|49", "54|64", "", 'C' },
	{ "V6",     "Sales 6 %",                                   VS_SALES,  600, 0, 0,  "01|49", "54|64", "", 'C' },
	{ "V0",     "Sales 0 %",                                   VS_SALES,    0, 0, 0,  "00|49", "",      "", 'C' },
	{ "VCC",    "Sales, VAT due by the Belgian co-contractor", VS_SALES,    0, 0, 0,  "45|49", "",      "", 'C' },
	{ "VEUG",   "Intra-EU supplies of goods",                  VS_SALES,    0, 0, 0,  "46|48", "",      "", 'L' },
	{ "VEUS",   "Intra-EU services",                           VS_SALES,    0, 0, 0,  "44|48", "",      "", 'S' },
	{ "VEUT",   "Intra-EU triangular sales (ABC)",             VS_SALES,    0, 0, 0,  "46|48", "",      "", 'T' },
	{ "VEX",    "Exports outside the EU",                      VS_SALES,    0, 0, 0,  "47|49", "",      "", 0 },
	{ "VX",     "Exempt (art. 44), not in the return",         VS_SALES,    0, 0, 0,  "|",     "",      "", 0 },
	// purchases
	{ "A21",    "Purchases 21 %",                              VS_PURCH, 2100, 100, 0, "N|N 85", "59|63", "", 0 },
	{ "A12",    "Purchases 12 %",                              VS_PURCH, 1200, 100, 0, "N|N 85", "59|63", "", 0 },
	{ "A6",     "Purchases 6 %",                               VS_PURCH,  600, 100, 0, "N|N 85", "59|63", "", 0 },
	{ "A0",     "Purchases 0 % or exempt",                     VS_PURCH,    0, 100, 0, "N|N 85", "",      "", 0 },
	{ "A21D50", "Purchases 21 %, half deductible (cars)",      VS_PURCH, 2100,  50, 0, "N|N 85", "59|63", "", 0 },
	{ "A21ND",  "Purchases 21 %, not deductible",              VS_PURCH, 2100,   0, 0, "N|N 85", "",      "", 0 },
	{ "A12ND",  "Purchases 12 %, not deductible",              VS_PURCH, 1200,   0, 0, "N|N 85", "",      "", 0 },
	{ "AEU21",  "Intra-EU acquisitions of goods 21 %",         VS_PURCH, 2100, 100, 55, "N 86|N 86 84", "59|", "55|", 0 },
	{ "AEU12",  "Intra-EU acquisitions of goods 12 %",         VS_PURCH, 1200, 100, 55, "N 86|N 86 84", "59|", "55|", 0 },
	{ "AEU6",   "Intra-EU acquisitions of goods 6 %",          VS_PURCH,  600, 100, 55, "N 86|N 86 84", "59|", "55|", 0 },
	{ "AEUS21", "Intra-EU services received 21 %",             VS_PURCH, 2100, 100, 55, "N 88|N 88 84", "59|", "55|", 0 },
	{ "ACC21",  "Belgian co-contractor (reverse charge) 21 %", VS_PURCH, 2100, 100, 56, "N 87|N 87 85", "59|", "56|", 0 },
	{ "ACC12",  "Belgian co-contractor (reverse charge) 12 %", VS_PURCH, 1200, 100, 56, "N 87|N 87 85", "59|", "56|", 0 },
	{ "ACC6",   "Belgian co-contractor (reverse charge) 6 %",  VS_PURCH,  600, 100, 56, "N 87|N 87 85", "59|", "56|", 0 },
	{ "AWS21",  "Services from outside the EU 21 % (rev. charge)", VS_PURCH, 2100, 100, 56, "N 87|N 87 85", "59|", "56|", 0 },
	{ "AIM21",  "Imports, VAT deferred (ET 14000) 21 %",       VS_PURCH, 2100, 100, 57, "N 87|N 87 85", "59|", "57|", 0 },
	// regularisations (a miscellaneous operation's line on the VAT accounts)
	{ "R61",    "Regularisation for the State (grid 61)",      VS_OTHER,    0, 0, 0,  "|",     "61|61", "", 0 },
	{ "R62",    "Regularisation for the declarant (grid 62)",  VS_OTHER,    0, 0, 0,  "|",     "62|62", "", 0 },
};
enum { NVAT = sizeof VAT_DEFS / sizeof VAT_DEFS[0] };

static int vat_find (const char *code)
{
	for (int i = 0; i < NVAT; i++) if (ci_eq (VAT_DEFS[i].code, code)) return i;
	return -1;
}
static const char *vat_code (int v) { return v >= 0 && v < NVAT ? VAT_DEFS[v].code : ""; }
static bool vat_reverse (int v) { return v >= 0 && v < NVAT && VAT_DEFS[v].due; }
static bool vat_side (int v, int side) { return v >= 0 && v < NVAT && VAT_DEFS[v].side == side; }

// A party's code by default for a line on this account.
static int vat_default (const Book &b, const Party *p, bool sale, const char *account)
{
	if (b.vatRegime == VR_FRANCHISE && sale) return -1;
	if (b.vatRegime == VR_NONE) return sale ? vat_find ("VX") : vat_find ("A21ND");
	if (p && p->defVat[0]) { int v = vat_find (p->defVat); if (v >= 0) return v; }
	int r = p ? (int) p->regime : (int) PR_BE;
	if (sale)
	{
		if (r == PR_EU) return vat_find ("VEUS");
		if (r == PR_WORLD) return vat_find ("VEX");
		if (r == PR_COCONTRACT) return vat_find ("VCC");
		return vat_find ("V21");
	}
	char nat = acc_nature (b, account);
	if (r == PR_EU) return vat_find (nat == NAT_SERVICES ? "AEUS21" : "AEU21");
	if (r == PR_WORLD) return vat_find (nat == NAT_SERVICES ? "AWS21" : "A0");
	if (r == PR_COCONTRACT) return vat_find ("ACC21");
	return vat_find ("A21");
}

// ---- grids --------------------------------------------------------------------------------------------------------
static const int GRIDS[] = { 0, 1, 2, 3, 44, 45, 46, 47, 48, 49, 81, 82, 83, 84, 85, 86, 87, 88, 54, 55, 56, 57, 61, 63, 59, 62, 64, 71, 72, 91 };
enum { NGRIDS = sizeof GRIDS / sizeof GRIDS[0] };
static int grid_sign (int g)
{
	switch (g)
	{
	case 0: case 1: case 2: case 3: case 44: case 45: case 46: case 47:
	case 54: case 55: case 56: case 57: case 61: case 63: case 84: case 85: return -1;
	}
	return 1;
}
// "03" / "91".
static void grid_label (int g, char *out) { out[0] = (char) ('0' + g / 10); out[1] = (char) ('0' + g % 10); out[2] = '\0'; }
static const char *grid_name (int g)
{
	switch (g)
	{
	case 0: return "Operations subject to a special scheme";
	case 1: return "Operations subject to 6 % VAT";
	case 2: return "Operations subject to 12 % VAT";
	case 3: return "Operations subject to 21 % VAT";
	case 44: return "Intra-EU services (the customer's VAT)";
	case 45: return "Operations with VAT due by the co-contractor";
	case 46: return "Exempt intra-EU supplies and ABC sales";
	case 47: return "Other exempt operations, operations abroad";
	case 48: return "Credit notes issued: grids 44 and 46";
	case 49: return "Credit notes issued: other operations";
	case 81: return "Goods, raw and auxiliary materials";
	case 82: return "Services and miscellaneous goods";
	case 83: return "Investment goods";
	case 84: return "Credit notes received: grids 86 and 88";
	case 85: return "Credit notes received: other operations";
	case 86: return "Intra-EU acquisitions and ABC sales";
	case 87: return "Other incoming operations, VAT due by you";
	case 88: return "Intra-EU services with reverse charge";
	case 54: return "VAT on grids 01, 02 and 03";
	case 55: return "VAT on grids 86 and 88";
	case 56: return "VAT on grid 87 (except deferred imports)";
	case 57: return "VAT on imports with deferral";
	case 61: return "Regularisations in favour of the State";
	case 63: return "VAT to pay back on credit notes received";
	case 59: return "Deductible VAT";
	case 62: return "Regularisations in favour of the declarant";
	case 64: return "VAT to recover on credit notes issued";
	case 71: return "VAT due to the State";
	case 72: return "VAT due by the State";
	case 91: return "Advance paid for December (monthly filers)";
	}
	return "";
}
// Its short name (the return's form, in a narrow box).
static const char *grid_short (int g)
{
	switch (g)
	{
	case 0: return "Special scheme"; case 1: return "Sales at 6 %"; case 2: return "Sales at 12 %"; case 3: return "Sales at 21 %";
	case 44: return "EU services"; case 45: return "Co-contractor"; case 46: return "EU supplies"; case 47: return "Exempt, abroad";
	case 48: return "Credit notes 44/46"; case 49: return "Other credit notes";
	case 81: return "Goods, materials"; case 82: return "Services"; case 83: return "Investments"; case 84: return "Credit notes 86/88";
	case 85: return "Other credit notes"; case 86: return "EU acquisitions"; case 87: return "VAT due by you"; case 88: return "EU services";
	case 54: return "VAT on 01-03"; case 55: return "VAT on 86, 88"; case 56: return "VAT on 87"; case 57: return "Import VAT";
	case 61: return "For the State"; case 63: return "On credit notes"; case 59: return "Deductible VAT"; case 62: return "For you";
	case 64: return "On credit notes"; case 71: return "Due to State"; case 72: return "Due by State"; case 91: return "December advance";
	}
	return grid_name (g);
}
// Is a VAT line a credit note's? An invoice's lines (the sales and purchases journals) say it by their
// entry (a discount line, negative, stays an operation's); elsewhere by their sign: a sale's base or
// tax credited, a purchase's debited is an operation (the tax a reverse charge makes due: the other way).
static bool line_credit (const Book &b, const Entry &e, const Line &l)
{
	int jt = e.journal >= 0 && e.journal < b.njr ? (int) b.jr[e.journal].type : (int) JT_MISC;
	if (jt == JT_SALES || jt == JT_PURCH) return (e.flags & EF_CREDIT) != 0;
	const VatDef &d = VAT_DEFS[l.vat];
	if (d.side == VS_SALES) return l.amount > 0;
	if (d.side == VS_PURCH) return l.role == LR_DUE ? l.amount > 0 : l.amount < 0;
	return false;
}
// A line's grids: its code's spec for its part and the entry's direction, the nature's grid put in for N.
static int line_grids (const Book &b, const Entry &e, const Line &l, int *g, int cap)
{
	if (l.vat < 0 || l.vat >= NVAT || l.role == LR_NONE || !l.amount) return 0;
	const VatDef &d = VAT_DEFS[l.vat];
	const char *spec = l.role == LR_TAX ? d.tax : l.role == LR_DUE ? d.dueg : d.base;	// (LR_ND: the base's)
	const char *p = spec;
	if (line_credit (b, e, l)) { while (*p && *p != '|') p++; if (*p) p++; }
	int n = 0;
	while (*p && *p != '|' && n < cap)
	{
		while (*p == ' ') p++;
		if (!*p || *p == '|') break;
		if (*p == 'N') { char nat = acc_nature (b, l.account); g[n++] = nat == NAT_GOODS ? 81 : nat == NAT_INVEST ? 83 : 82; p++; continue; }
		int v = 0; while (digit (*p)) v = v * 10 + (*p++ - '0');
		g[n++] = v;
	}
	return n;
}
// A period's grids (grid[0..99], its lines' amounts; then XX, YY, 71 / 72 made). ref: the lines
// counted (their entries' indexes, when wanted for a grid: grid >= 0).
static void vat_grids (const Book &b, int from, int to, money *grid)
{
	for (int i = 0; i < 100; i++) grid[i] = 0;
	for (int i = 0; i < b.ne; i++)
	{
		const Entry &e = b.e[i];
		if (e.date < from || e.date > to) continue;
		for (int k = 0; k < e.nl; k++)
		{
			int g[4], n = line_grids (b, e, e.l[k], g, 4);
			for (int j = 0; j < n; j++) grid[g[j]] += e.l[k].amount * grid_sign (g[j]);
		}
	}
	money xx = grid[54] + grid[55] + grid[56] + grid[57] + grid[61] + grid[63];
	money yy = grid[59] + grid[62] + grid[64];
	grid[71] = xx > yy ? xx - yy : 0;
	grid[72] = yy > xx ? yy - xx : 0;
}
static money grid_xx (const money *g) { return g[54] + g[55] + g[56] + g[57] + g[61] + g[63]; }
static money grid_yy (const money *g) { return g[59] + g[62] + g[64]; }

// The warnings a return's grids call for (Intervat's own checks, some of them): into out (lines).
static int vat_checks (const money *g, char out[][96], int cap)
{
	int n = 0;
	auto add = [&] (const char *s) { if (n < cap) scpy (out[n++], s, 96); };
	for (int i = 0; i < NGRIDS; i++) if (g[GRIDS[i]] < 0)
	{
		char t[96] = "Grid "; char l[4]; grid_label (GRIDS[i], l); scat (t, l, 96);
		scat (t, " is negative: Intervat takes no negative amount -- check its credit notes.", 96); add (t);
	}
	money due = tax_of (g[1], 600) + tax_of (g[2], 1200) + tax_of (g[3], 2100);
	if (labs_ (g[54] - due) > 100 + due / 100) add ("Grid 54 differs from the VAT on grids 01, 02 and 03 (their rates).");
	if ((g[86] || g[88]) && !g[55]) add ("Grids 86 / 88 hold acquisitions but grid 55 holds no VAT due.");
	if (g[55] && !g[86] && !g[88]) add ("Grid 55 holds VAT but grids 86 and 88 are empty.");
	if ((g[56] || g[57]) && !g[87]) add ("Grids 56 / 57 hold VAT but grid 87 is empty.");
	if (g[63] > tax_of (g[85] + g[84], 2100) + 100) add ("Grid 63 is more than 21 % of grids 84 and 85.");
	if (g[64] > tax_of (g[49], 2100) + 100) add ("Grid 64 is more than 21 % of grid 49.");
	return n;
}

// ---- the periods --------------------------------------------------------------------------------------------------
// A period's name: "Q3 2026" / "September 2026".
static void period_name (int year, int period, bool monthly, char *out, int cap)
{
	out[0] = '\0';
	if (monthly) scpy (out, MONTH_NAME[(period - 1) % 12], cap);
	else { scpy (out, "Q", cap); scat_num (out, period, cap); }
	scat (out, " ", cap); scat_num (out, year, cap);
}
static void period_range (int year, int period, bool monthly, int *from, int *to)
{
	if (monthly) { *from = ymd (year, period, 1); *to = month_end (year, period); }
	else { *from = ymd (year, period * 3 - 2, 1); *to = month_end (year, period * 3); }
}
static int return_find (const Book &b, int year, int period, bool monthly)
{
	for (int i = 0; i < b.nret; i++) if (b.ret[i].year == year && b.ret[i].period == period && (bool) b.ret[i].monthly == monthly) return i;
	return -1;
}
static VatReturn &return_add (Book &b)
{
	if (b.nret == b.cret) { int c = b.cret ? b.cret * 2 : 32; VatReturn *nr = new VatReturn[c]; for (int i = 0; i < b.nret; i++) nr[i] = b.ret[i]; delete [] b.ret; b.ret = nr; b.cret = c; }
	VatReturn &r = b.ret[b.nret++];
	r.year = r.period = 0; r.monthly = 0; r.filed = 0; for (int i = 0; i < 100; i++) r.grid[i] = 0;
	return r;
}
// The day a period's return is due: the 20th of the month after the period (the usual deadline).
static int return_deadline (int year, int period, bool monthly)
{
	int from, to; period_range (year, period, monthly, &from, &to);
	int next = date_add (to, 1);
	return ymd (y_of (next), m_of (next), 20);
}

// ---- the Intervat XML files ---------------------------------------------------------------------------------------
static void put_amount (Out &o, money v) { char t[32]; fmt_plain (v < 0 ? -v : v, t); o.puts (t); }
static void put_signed (Out &o, money v) { char t[32]; fmt_plain (v, t); o.puts (t); }
// The declarant: the company (its VAT number's ten digits; a phone and an e-mail only in the forms the
// schema takes).
static void put_declarant (Out &o, const Book &b, const char *ns)
{
	o.puts ("\t\t<"); o.puts (ns); o.puts (":Declarant>\n");
	o.puts ("\t\t\t<VATNumber>"); o.puts (b.vat[0] == 'B' && b.vat[1] == 'E' ? b.vat + 2 : b.vat); o.puts ("</VATNumber>\n");
	o.puts ("\t\t\t<Name>"); put_xml (o, b.name); o.puts ("</Name>\n");
	if (b.street[0]) { o.puts ("\t\t\t<Street>"); put_xml (o, b.street); o.puts ("</Street>\n"); }
	if (b.zip[0]) { o.puts ("\t\t\t<PostCode>"); put_xml (o, b.zip); o.puts ("</PostCode>\n"); }
	if (b.city[0]) { o.puts ("\t\t\t<City>"); put_xml (o, b.city); o.puts ("</City>\n"); }
	o.puts ("\t\t\t<CountryCode>BE</CountryCode>\n");
	bool mailOk = false;
	{
		const char *at = 0; bool bad = false;
		for (const char *p = b.email; *p; p++) { if (*p == '@') { if (at) bad = true; at = p; } else if (!(alnum (*p) || *p == '_' || *p == '-' || *p == '.')) bad = true; }
		if (at && !bad && at > b.email) { const char *dot = 0; for (const char *p = at; *p; p++) if (*p == '.') dot = p; mailOk = dot && dot > at + 1 && slen (dot + 1) >= 2 && slen (dot + 1) <= 5; }
	}
	if (mailOk) { o.puts ("\t\t\t<EmailAddress>"); o.puts (b.email); o.puts ("</EmailAddress>\n"); }
	char ph[24]; int n = 0;
	for (const char *p = b.phone; *p && n < 20; p++) if (digit (*p) || *p == ' ' || *p == '-' || (*p == '+' && n == 0)) ph[n++] = *p;
	ph[n] = '\0';
	if (n) { o.puts ("\t\t\t<Phone>"); o.puts (ph); o.puts ("</Phone>\n"); }
	o.puts ("\t\t</"); o.puts (ns); o.puts (":Declarant>\n");
}
static void put_period (Out &o, int year, int period, bool monthly, const char *ns)
{
	o.puts ("\t\t<"); o.puts (ns); o.puts (":Period>\n\t\t\t<"); o.puts (ns); o.puts (monthly ? ":Month>" : ":Quarter>");
	char t[16]; itoa10 (period, t); o.puts (t);
	o.puts ("</"); o.puts (ns); o.puts (monthly ? ":Month>\n" : ":Quarter>\n");
	o.puts ("\t\t\t<"); o.puts (ns); o.puts (":Year>"); itoa10 (year, t); o.puts (t); o.puts ("</"); o.puts (ns); o.puts (":Year>\n");
	o.puts ("\t\t</"); o.puts (ns); o.puts (":Period>\n");
}
// "2026-Q3" / "2026-09" (the reference the declarant gives the file).
static void period_ref (int year, int period, bool monthly, char *out)
{
	out[0] = '\0'; scat_num (out, year, 16); scat (out, monthly ? "-" : "-Q", 16);
	if (monthly && period < 10) scat (out, "0", 16);
	scat_num (out, period, 16);
}
// The periodic return (the grids given: vat_grids'; 91 as typed). nihil: no customer listing to file
// (the year's last return only); restitution / payment: the asks.
static void vat_return_xml (const Book &b, int year, int period, bool monthly, const money *grid, bool nihil, bool restitution,
			    bool payment, const char *comment, Out &o)
{
	o.puts ("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
	o.puts ("<ns2:VATConsignment xmlns=\"http://www.minfin.fgov.be/InputCommon\" xmlns:ns2=\"http://www.minfin.fgov.be/VATConsignment\" VATDeclarationsNbr=\"1\">\n");
	char ref[16]; period_ref (year, period, monthly, ref);
	o.puts ("\t<ns2:VATDeclaration SequenceNumber=\"1\" DeclarantReference=\""); o.puts (ref); o.puts ("\">\n");
	put_declarant (o, b, "ns2");
	put_period (o, year, period, monthly, "ns2");
	o.puts ("\t\t<ns2:Data>\n");
	for (int i = 0; i < NGRIDS; i++)
	{
		int g = GRIDS[i];
		if (g == 71 || g == 72) continue;
		if (grid[g] <= 0) continue;
		char t[8]; itoa10 (g, t);
		o.puts ("\t\t\t<ns2:Amount GridNumber=\""); o.puts (t); o.puts ("\">"); put_amount (o, grid[g]); o.puts ("</ns2:Amount>\n");
	}
	// (71 or 72: the one is always there)
	bool due = grid[71] > 0 || grid[72] <= 0;
	o.puts ("\t\t\t<ns2:Amount GridNumber=\""); o.puts (due ? "71" : "72"); o.puts ("\">"); put_amount (o, due ? grid[71] : grid[72]); o.puts ("</ns2:Amount>\n");
	o.puts ("\t\t</ns2:Data>\n");
	o.puts ("\t\t<ns2:ClientListingNihil>"); o.puts (nihil ? "YES" : "NO"); o.puts ("</ns2:ClientListingNihil>\n");
	o.puts ("\t\t<ns2:Ask Restitution=\""); o.puts (restitution ? "YES" : "NO"); o.puts ("\" Payment=\""); o.puts (payment ? "YES" : "NO"); o.puts ("\"/>\n");
	if (comment && comment[0]) { o.puts ("\t\t<ns2:Comment>"); put_xml (o, comment); o.puts ("</ns2:Comment>\n"); }
	o.puts ("\t</ns2:VATDeclaration>\n</ns2:VATConsignment>\n");
}

// ---- the settlement ------------------------------------------------------------------------------------------------------
// A return's settlement (the "liquidation"): the period's VAT due (451000) and deductible (411000) moved
// to the VAT current account -- what the State is owed on 451200, what it owes on 411200 -- by an entry
// of the miscellaneous journal on the period's last day (flagged so; a settlement made again moves only
// what is left). The payment of what is due then debits 451200 (a bank statement's line). false: nothing
// to settle.
static bool vat_settlement (const Book &b, int year, int period, bool monthly, Entry &e)
{
	int from, to; period_range (year, period, monthly, &from, &to);
	money due = 0, ded = 0;
	for (int i = 0; i < b.ne; i++)
	{
		const Entry &x = b.e[i];
		if (x.date < from || x.date > to) continue;
		for (int k = 0; k < x.nl; k++)
		{
			if (seq (x.l[k].account, b.accVatDue)) due += x.l[k].amount;
			else if (seq (x.l[k].account, b.accVatDeduct)) ded += x.l[k].amount;
		}
	}
	entry_init (e);
	if (!due && !ded) return false;
	int j = 0; while (j < b.njr && b.jr[j].type != JT_MISC) j++;
	e.journal = j < b.njr ? j : 0; e.date = to; e.due = to; e.flags = EF_SETTLE;
	char pn[32]; period_name (year, period, monthly, pn, sizeof pn);
	char t[64] = "VAT settlement "; scat (t, pn, sizeof t); sset (e.text, t);
	if (due) { Line &l = entry_add_line (e); scpy (l.account, b.accVatDue, CODE_MAX); l.amount = -due; sset (l.text, t); }
	if (ded) { Line &l = entry_add_line (e); scpy (l.account, b.accVatDeduct, CODE_MAX); l.amount = -ded; sset (l.text, t); }
	money bal = due + ded;					// (negative: the State is owed it)
	if (bal) { Line &l = entry_add_line (e); scpy (l.account, bal < 0 ? "451200" : "411200", CODE_MAX); l.amount = bal; l.due = return_deadline (year, period, monthly); sset (l.text, t); }
	return true;
}

// ---- the listings -------------------------------------------------------------------------------------------------------
// A customer's amounts in a period: the bases (excluding VAT) of its sales lines whose code lists it
// (kind 'C': the customer listing's; 'L', 'S', 'T': the intra-community's), and their VAT.
struct ListRow { int party; char kind; money base, tax; };
static int listing_rows (const Book &b, int from, int to, bool intra, ListRow *out, int cap)
{
	int n = 0;
	for (int i = 0; i < b.ne; i++)
	{
		const Entry &e = b.e[i];
		if (e.date < from || e.date > to || !e.party) continue;
		const Party *p = party_of (b, e.party);
		if (!p || p->kind != PK_CUSTOMER) continue;
		for (int k = 0; k < e.nl; k++)
		{
			const Line &l = e.l[k];
			if (l.vat < 0 || l.vat >= NVAT || VAT_DEFS[l.vat].side != VS_SALES) continue;
			char kind = VAT_DEFS[l.vat].listing;
			if (!kind || (intra ? kind == 'C' : kind != 'C')) continue;
			int j = 0; while (j < n && !(out[j].party == e.party && out[j].kind == kind)) j++;
			if (j == n) { if (n == cap) continue; out[n].party = e.party; out[n].kind = kind; out[n].base = out[n].tax = 0; n++; }
			if (l.role == LR_BASE) out[j].base -= l.amount;		// (a sale's base: a credit)
			else if (l.role == LR_TAX) out[j].tax -= l.amount;
		}
	}
	return n;
}
// The customer listing of a calendar year: the Belgian VAT-registered customers whose sales reached
// 250 EUR (excluding VAT) -- rows kept in out, their count returned.
static int client_listing (const Book &b, int year, ListRow *out, int cap)
{
	ListRow *all = new ListRow[cap > 0 ? cap : 1];
	int n = listing_rows (b, ymd (year, 1, 1), ymd (year, 12, 31), false, all, cap), k = 0;
	for (int i = 0; i < n; i++)
	{
		const Party *p = party_of (b, all[i].party);
		if (!p || !(p->vat[0] == 'B' && p->vat[1] == 'E') || vat_check (p->vat)) continue;
		// (a customer's rows of several codes: one line)
		int j = 0; while (j < k && out[j].party != all[i].party) j++;
		if (j == k) { out[k] = all[i]; out[k].kind = 'C'; k++; }
		else { out[j].base += all[i].base; out[j].tax += all[i].tax; }
	}
	int m = 0;
	for (int i = 0; i < k; i++) if (out[i].base >= 25000) out[m++] = out[i];
	delete [] all;
	return m;
}
static void client_listing_xml (const Book &b, int year, const ListRow *r, int n, Out &o)
{
	money sumB = 0, sumT = 0;
	for (int i = 0; i < n; i++) { sumB += r[i].base; sumT += r[i].tax; }
	o.puts ("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
	o.puts ("<ns2:ClientListingConsignment xmlns=\"http://www.minfin.fgov.be/InputCommon\" xmlns:ns2=\"http://www.minfin.fgov.be/ClientListingConsignment\" ClientListingsNbr=\"1\">\n");
	char t[32]; itoa10 (n, t);
	o.puts ("\t<ns2:ClientListing SequenceNumber=\"1\" ClientsNbr=\""); o.puts (t); o.puts ("\" DeclarantReference=\"");
	itoa10 (year, t); o.puts (t); o.puts ("-LK\" TurnOverSum=\""); put_signed (o, sumB); o.puts ("\" VATAmountSum=\""); put_signed (o, sumT); o.puts ("\">\n");
	put_declarant (o, b, "ns2");
	o.puts ("\t\t<ns2:Period>"); itoa10 (year, t); o.puts (t); o.puts ("</ns2:Period>\n");
	for (int i = 0; i < n; i++)
	{
		const Party *p = party_of (b, r[i].party);
		itoa10 (i + 1, t);
		o.puts ("\t\t<ns2:Client SequenceNumber=\""); o.puts (t); o.puts ("\">\n");
		o.puts ("\t\t\t<ns2:CompanyVATNumber issuedBy=\"BE\">"); o.puts (p ? p->vat + 2 : ""); o.puts ("</ns2:CompanyVATNumber>\n");
		o.puts ("\t\t\t<ns2:TurnOver>"); put_signed (o, r[i].base); o.puts ("</ns2:TurnOver>\n");
		o.puts ("\t\t\t<ns2:VATAmount>"); put_signed (o, r[i].tax); o.puts ("</ns2:VATAmount>\n");
		o.puts ("\t\t</ns2:Client>\n");
	}
	o.puts ("\t</ns2:ClientListing>\n</ns2:ClientListingConsignment>\n");
}
// The intra-community listing of a period: its rows (customer and kind), those without a valid EU
// VAT number left out (and counted in *bad).
static int intra_listing (const Book &b, int from, int to, ListRow *out, int cap, int *bad)
{
	int n = listing_rows (b, from, to, true, out, cap), m = 0;
	*bad = 0;
	for (int i = 0; i < n; i++)
	{
		const Party *p = party_of (b, out[i].party);
		if (!out[i].base) continue;
		if (!p || vat_check (p->vat) || !eu_prefix (p->vat) || (p->vat[0] == 'B' && p->vat[1] == 'E')) { (*bad)++; continue; }
		out[m++] = out[i];
	}
	return m;
}
static void intra_listing_xml (const Book &b, int year, int period, bool monthly, const ListRow *r, int n, Out &o)
{
	money sum = 0;
	for (int i = 0; i < n; i++) sum += r[i].base;
	o.puts ("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
	o.puts ("<ns2:IntraConsignment xmlns=\"http://www.minfin.fgov.be/InputCommon\" xmlns:ns2=\"http://www.minfin.fgov.be/IntraConsignment\" IntraListingsNbr=\"1\">\n");
	char t[32], ref[16]; itoa10 (n, t); period_ref (year, period, monthly, ref);
	o.puts ("\t<ns2:IntraListing SequenceNumber=\"1\" ClientsNbr=\""); o.puts (t); o.puts ("\" DeclarantReference=\""); o.puts (ref);
	o.puts ("-IC\" AmountSum=\""); put_signed (o, sum); o.puts ("\">\n");
	put_declarant (o, b, "ns2");
	put_period (o, year, period, monthly, "ns2");
	for (int i = 0; i < n; i++)
	{
		const Party *p = party_of (b, r[i].party);
		char pre[3] = { p->vat[0], p->vat[1], 0 };
		if (pre[0] == 'G' && pre[1] == 'R') { pre[0] = 'E'; pre[1] = 'L'; }
		itoa10 (i + 1, t);
		o.puts ("\t\t<ns2:IntraClient SequenceNumber=\""); o.puts (t); o.puts ("\">\n");
		o.puts ("\t\t\t<ns2:CompanyVATNumber issuedBy=\""); o.puts (pre); o.puts ("\">"); o.puts (p->vat + 2); o.puts ("</ns2:CompanyVATNumber>\n");
		char k[2] = { r[i].kind, 0 };
		o.puts ("\t\t\t<ns2:Code>"); o.puts (k); o.puts ("</ns2:Code>\n");
		o.puts ("\t\t\t<ns2:Amount>"); put_signed (o, r[i].base); o.puts ("</ns2:Amount>\n");
		o.puts ("\t\t</ns2:IntraClient>\n");
	}
	o.puts ("\t</ns2:IntraListing>\n</ns2:IntraConsignment>\n");
}

} // namespace lg

#endif
