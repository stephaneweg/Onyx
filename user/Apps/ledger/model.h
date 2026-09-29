//
// model.h -- Ledger's books: one company's double-entry accounts, in the Belgian way (the PCMN / MAR
// chart of accounts, Belgian VAT). What it holds:
//
//   * the company (its name, address, VAT number, bank account; its VAT situation -- a normal taxable
//     person filing monthly or quarterly returns, the small business franchise, or none) and the
//     accounts it posts to by role: customers 400000, suppliers 440000, VAT due 451000, VAT
//     deductible 411000, the result carried forward 140000 / 141000, the suspense account 499000;
//   * its fiscal YEARS (a start, an end, closed or not);
//   * the chart of ACCOUNTS: a code (the PCMN's digits: "4" a class, "40" and "400" headings, "400000"
//     an account posted to) and a name, kept sorted by code as text -- which is the tree's order;
//     a purchase account's VAT nature (goods, services, investments: grids 81, 82, 83) follows from
//     its code unless it says otherwise;
//   * the PARTIES: customers and suppliers (a code, a name, an address, a VAT number and the VAT
//     situation it implies -- Belgian taxable, private person, EU, outside the EU, co-contractor --,
//     an IBAN, payment terms, the accounts and the VAT code their invoices take by default);
//   * the JOURNALS: sales, purchases, financial (a bank's, the cash's: their account), miscellaneous;
//   * the ENTRIES: a journal's document -- a sales or purchase invoice or credit note, a bank or cash
//     statement, a miscellaneous operation -- numbered in its journal and fiscal year, dated, with its
//     LINES: an account, an amount (cents: a debit positive, a credit negative), the party (on a
//     customers' or suppliers' account: its subsidiary ledger), a VAT code and the part the line plays
//     for it (a taxable base, the tax, the tax a reverse charge makes due, the tax not deductible), a
//     due date, a matching group (lines of a party whose amounts add up to zero: an invoice and its
//     payments -- "lettrage");
//   * the VAT returns filed: their period, their grids (vat.h), the period then locked.
//
// An entry always balances (its lines add up to zero); lines on the customers' and suppliers'
// accounts name their party. Freestanding, integer only.
//
#ifndef _ledger_model_h
#define _ledger_model_h

#include "core.h"

namespace lg {

enum { CODE_MAX = 11, NAME_MAX = 72, ACC_NAME_MAX = 128, PCODE_MAX = 16 };

// ---- VAT (the codes: vat.h) -----------------------------------------------------------------------------------------
enum { VS_SALES, VS_PURCH, VS_OTHER };			// a code's side
enum { LR_NONE, LR_BASE, LR_TAX, LR_DUE, LR_ND };	// a line's part: none, a taxable base, its tax (charged on
							// a sale / deductible on a purchase), the tax a reverse charge
							// makes due, the tax not deductible (a cost)

// ---- accounts -----------------------------------------------------------------------------------------------------
enum { NAT_AUTO = 0, NAT_GOODS = 'G', NAT_SERVICES = 'S', NAT_INVEST = 'I' };
struct Account
{
	char code[CODE_MAX];
	char name[ACC_NAME_MAX];
	char nature;					// NAT_* (a purchase's grid); NAT_AUTO: by the code
	bool hidden;					// (no longer offered)
};
// What an account is, by its code.
enum { AK_OFF, AK_EQUITY, AK_FIXED, AK_STOCK, AK_RECV, AK_PAY, AK_VAT_IN, AK_VAT_OUT, AK_OTHER4, AK_BANK, AK_CASH,
       AK_TREASURY, AK_EXPENSE, AK_INCOME, AK_NONE };
static int acc_kind (const char *c)
{
	switch (c[0])
	{
	case '0': return AK_OFF;
	case '1': return AK_EQUITY;
	case '2': return AK_FIXED;
	case '3': return AK_STOCK;
	case '4':
		if (c[1] == '0') return AK_RECV;
		if (c[1] == '4') return AK_PAY;
		if (c[1] == '1' && c[2] == '1') return AK_VAT_IN;
		if (c[1] == '5' && c[2] == '1') return AK_VAT_OUT;
		return AK_OTHER4;
	case '5':
		if (c[1] == '5' || c[1] == '6') return AK_BANK;
		if (c[1] == '7') return AK_CASH;
		return AK_TREASURY;
	case '6': return AK_EXPENSE;
	case '7': return AK_INCOME;
	}
	return AK_NONE;
}
static bool acc_bs (const char *c) { return c[0] >= '1' && c[0] <= '5'; }	// a balance sheet's (carried forward)
static bool acc_pl (const char *c) { return c[0] == '6' || c[0] == '7'; }	// the income statement's
// A code is a heading (not posted to): the class, the two- and three-digit groups.
static bool acc_heading (const char *c) { return slen (c) <= 3; }

// ---- parties ------------------------------------------------------------------------------------------------------
enum { PK_CUSTOMER, PK_SUPPLIER };
enum { PR_BE, PR_PRIVATE, PR_EU, PR_WORLD, PR_COCONTRACT, PR_COUNT };	// its VAT situation
static const char *const REGIME_NAME[PR_COUNT] = { "Belgian, subject to VAT", "Private person (no VAT number)",
						   "Other EU country, VAT number", "Outside the EU", "Belgian co-contractor (reverse charge)" };
struct Party
{
	int id;
	unsigned char kind, regime; bool hidden;
	short terms;					// days to pay (0: at once)
	char code[PCODE_MAX], name[NAME_MAX], street[NAME_MAX], zip[12], city[48], country[4];
	char vat[20], email[72], phone[24], iban[36], bic[12];
	char account[CODE_MAX];				// its collective account ("": the company's default)
	char defAcc[CODE_MAX];				// its invoices' account by default
	char defVat[8];					// ... VAT code ("": the regime's)
	char *notes;
	char lang[4];					// its documents' language: "fr", "nl", "en" ("": the company's)
};

// ---- journals -----------------------------------------------------------------------------------------------------
enum { JT_SALES, JT_PURCH, JT_BANK, JT_CASH, JT_MISC, JT_COUNT };
static const char *const JT_KEY[JT_COUNT] = { "sales", "purchases", "bank", "cash", "misc" };
static const char *const JT_NAME[JT_COUNT] = { "Sales", "Purchases", "Bank", "Cash", "Miscellaneous" };
struct Journal
{
	char code[6], name[40];
	unsigned char type; bool hidden;
	char account[CODE_MAX];				// (financial) its account: 550000, 570000...
	char iban[36];					// (a bank's) its account's IBAN
};
static bool jt_fin (int t) { return t == JT_BANK || t == JT_CASH; }

// ---- entries --------------------------------------------------------------------------------------------------------
struct Line
{
	char account[CODE_MAX];
	money amount;					// debit > 0, credit < 0
	int party;					// its party's id (0: none)
	signed char vat;				// its VAT code (-1: none)
	unsigned char role;				// LR_*
	money aux;					// a tax's line: the base it was computed on; a base's line: its
						// tax (as the invoice said it, the document's way: positive)
	int due;					// (a party's line) when it is due
	int match;					// its matching group (0: open)
	char *text;
};
enum { EF_CREDIT = 1, EF_OPENING = 2, EF_SETTLE = 4 };	// a credit note / the opening balances / a VAT return's settlement
struct Entry
{
	int id, journal, no, date, due, party;		// (the invoice's party and due date)
	unsigned flags;
	char ref[32];					// (a purchase) the supplier's number; (any) a reference
	char comm[13];					// a structured communication's twelve digits
	char *text;
	money stmtOld, stmtNew;				// (a statement) the balances it says
	Line *l; int nl, cap;
};

// ---- commercial documents (not posted: commerce.h) --------------------------------------------------------------------------
enum { CD_QUOTE, CD_ORDER, CD_DELIVERY, CD_PORDER, CD_COUNT };		// a quote, a customer's order, a delivery note, a
									// purchase order (to a supplier)
enum { CS_DRAFT, CS_SENT, CS_ACCEPTED, CS_REFUSED, CS_DONE, CS_COUNT };	// its state (done: invoiced, delivered)
struct CLine
{
	char *text;
	long long qty;					// thousandths (2,5 hours: 2500)
	money price;					// a unit's price excluding VAT
	signed char vat;				// its VAT code (-1: none)
	char account[CODE_MAX];				// the account its invoice's line takes ("": the party's, the usual)
};
struct CDoc
{
	int id, kind, no, date, until, party, status;	// until: a quote's validity, an order's delivery date
	int invoice, from;				// the invoice it became (an entry's id), the document it came from
	char ref[32];					// the customer's reference, the order's number...
	char *text;					// what it is about, the conditions
	CLine *l; int nl, cap;
};

// ---- the book -------------------------------------------------------------------------------------------------------
enum { VR_NORMAL, VR_FRANCHISE, VR_NONE };		// the company: files VAT returns / the small business
							// franchise (no VAT charged) / not subject (art. 44)
enum { VP_QUARTER, VP_MONTH };
struct Year { int start, end; bool closed; };
struct VatReturn
{
	int year, period;				// a month 1..12, or a quarter 1..4
	unsigned char monthly;
	int filed;					// the day it was filed
	money grid[100];
};
enum { MAXYEARS = 40, MAXJOURNALS = 24 };
struct Book
{
	char name[NAME_MAX], legal[24], street[NAME_MAX], zip[12], city[48], country[4], vat[20], email[72], phone[24];
	char iban[36], bic[12];
	char reg[48], web[64];				// its register ("RPM Bruxelles"), its web site (the documents' letterhead)
	unsigned char vatRegime, vatPeriod;
	char chart[4];					// "fr" / "nl": the chart's language
	char accCustomers[CODE_MAX], accSuppliers[CODE_MAX], accVatDue[CODE_MAX], accVatDeduct[CODE_MAX];
	char accProfit[CODE_MAX], accLoss[CODE_MAX], accSuspense[CODE_MAX];
	Year yr[MAXYEARS]; int nyr;
	Account *acc; int nacc, cacc;
	Party *pty; int npty, cpty, nextParty;
	Journal jr[MAXJOURNALS]; int njr;
	Entry *e; int ne, ce, nextEntry;
	VatReturn *ret; int nret, cret;
	CDoc *cd; int ncd, ccd, nextCd;			// the commercial documents (sorted by id)
	int nextMatch;
	unsigned changes;				// bumped by every change (the views follow)
};

// ---- the book's life ------------------------------------------------------------------------------------------------
static void line_free (Line &l) { sfree (l.text); l.text = s_empty; }
static void entry_init (Entry &e)
{
	e.id = e.journal = e.no = e.date = e.due = e.party = 0; e.flags = 0;
	e.ref[0] = e.comm[0] = '\0'; e.text = s_empty; e.stmtOld = e.stmtNew = 0;
	e.l = 0; e.nl = e.cap = 0;
}
static void entry_free (Entry &e)
{
	for (int i = 0; i < e.nl; i++) line_free (e.l[i]);
	delete [] e.l; e.l = 0; e.nl = e.cap = 0;
	sfree (e.text); e.text = s_empty;
}
static void line_init (Line &l)
{
	l.account[0] = '\0'; l.amount = 0; l.party = 0; l.vat = -1; l.role = LR_NONE; l.aux = 0; l.due = 0; l.match = 0; l.text = s_empty;
}
static Line &entry_add_line (Entry &e)
{
	if (e.nl == e.cap) { int c = e.cap ? e.cap * 2 : 8; Line *nl = new Line[c]; for (int i = 0; i < e.nl; i++) nl[i] = e.l[i]; delete [] e.l; e.l = nl; e.cap = c; }
	Line &l = e.l[e.nl++]; line_init (l);
	return l;
}
// A deep copy (its texts and lines its own).
static void entry_copy (Entry &d, const Entry &s)
{
	d = s;
	d.text = sdup (s.text);
	d.l = s.nl ? new Line[s.nl] : 0; d.cap = s.nl;
	for (int i = 0; i < s.nl; i++) { d.l[i] = s.l[i]; d.l[i].text = sdup (s.l[i].text); }
}
static money entry_sum (const Entry &e) { money s = 0; for (int i = 0; i < e.nl; i++) s += e.l[i].amount; return s; }
// The documents' languages.
static const char *const LANG_KEY[3] = { "fr", "nl", "en" };
static const char *const LANG_NAME[3] = { "French", "Dutch", "English" };
static int lang_index (const char *k) { for (int i = 0; i < 3; i++) if (ci_eq (k, LANG_KEY[i])) return i; return 0; }
static void party_init (Party &p)
{
	p.id = 0; p.kind = PK_CUSTOMER; p.regime = PR_BE; p.hidden = false; p.terms = 30;
	p.code[0] = p.name[0] = p.street[0] = p.zip[0] = p.city[0] = '\0'; scpy (p.country, "BE", 4);
	p.vat[0] = p.email[0] = p.phone[0] = p.iban[0] = p.bic[0] = '\0';
	p.account[0] = p.defAcc[0] = p.defVat[0] = '\0'; p.notes = s_empty; p.lang[0] = '\0';
}

static void book_init (Book &b)
{
	b.name[0] = b.legal[0] = b.street[0] = b.zip[0] = b.city[0] = b.vat[0] = b.email[0] = b.phone[0] = b.iban[0] = b.bic[0] = '\0';
	b.reg[0] = b.web[0] = '\0';
	scpy (b.country, "BE", 4); scpy (b.chart, "fr", 4);
	b.vatRegime = VR_NORMAL; b.vatPeriod = VP_QUARTER;
	scpy (b.accCustomers, "400000", CODE_MAX); scpy (b.accSuppliers, "440000", CODE_MAX);
	scpy (b.accVatDue, "451000", CODE_MAX); scpy (b.accVatDeduct, "411000", CODE_MAX);
	scpy (b.accProfit, "140000", CODE_MAX); scpy (b.accLoss, "141000", CODE_MAX); scpy (b.accSuspense, "499000", CODE_MAX);
	b.nyr = 0; b.acc = 0; b.nacc = b.cacc = 0; b.pty = 0; b.npty = b.cpty = 0; b.nextParty = 1;
	b.njr = 0; b.e = 0; b.ne = b.ce = 0; b.nextEntry = 1; b.ret = 0; b.nret = b.cret = 0; b.nextMatch = 1; b.changes = 0;
	b.cd = 0; b.ncd = b.ccd = 0; b.nextCd = 1;
}
static void cline_free (CLine &l) { sfree (l.text); l.text = s_empty; }
static void cdoc_init (CDoc &d)
{
	d.id = d.kind = d.no = d.date = d.until = d.party = d.status = d.invoice = d.from = 0;
	d.ref[0] = '\0'; d.text = s_empty; d.l = 0; d.nl = d.cap = 0;
}
static void cdoc_free (CDoc &d)
{
	for (int i = 0; i < d.nl; i++) cline_free (d.l[i]);
	delete [] d.l; d.l = 0; d.nl = d.cap = 0;
	sfree (d.text); d.text = s_empty;
}
static void book_clear (Book &b)
{
	for (int i = 0; i < b.ne; i++) entry_free (b.e[i]);
	for (int i = 0; i < b.npty; i++) sfree (b.pty[i].notes);
	for (int i = 0; i < b.ncd; i++) cdoc_free (b.cd[i]);
	delete [] b.acc; delete [] b.pty; delete [] b.e; delete [] b.ret; delete [] b.cd;
	unsigned ch = b.changes;
	book_init (b);
	b.changes = ch + 1;
}

// ---- years ----------------------------------------------------------------------------------------------------------
// The fiscal year holding a date (-1: none).
static int year_of (const Book &b, int date)
{
	for (int i = 0; i < b.nyr; i++) if (date >= b.yr[i].start && date <= b.yr[i].end) return i;
	return -1;
}
static void years_sort (Book &b)
{
	for (int i = 1; i < b.nyr; i++) for (int j = i; j > 0 && b.yr[j].start < b.yr[j - 1].start; j--) { Year t = b.yr[j]; b.yr[j] = b.yr[j - 1]; b.yr[j - 1] = t; }
}
// "2026" (a calendar year), else "2025-2026".
static void year_label (const Year &y, char *out, int cap)
{
	out[0] = '\0'; scat_num (out, y_of (y.start), cap);
	if (y_of (y.end) != y_of (y.start)) { scat (out, "-", cap); scat_num (out, y_of (y.end), cap); }
}

// ---- accounts ---------------------------------------------------------------------------------------------------------
static int code_cmp (const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return (unsigned char) *a - (unsigned char) *b; }
// Its index (-1: none).
static int acc_find (const Book &b, const char *code)
{
	int lo = 0, hi = b.nacc - 1;
	while (lo <= hi)
	{
		int mid = (lo + hi) / 2, c = code_cmp (b.acc[mid].code, code);
		if (!c) return mid;
		if (c < 0) lo = mid + 1; else hi = mid - 1;
	}
	return -1;
}
// Added in its place (or renamed when it exists) -> its index.
static int acc_put (Book &b, const char *code, const char *name, char nature = NAT_AUTO)
{
	int i = acc_find (b, code);
	if (i >= 0) { scpy (b.acc[i].name, name, ACC_NAME_MAX); if (nature) b.acc[i].nature = nature; return i; }
	if (b.nacc == b.cacc) { int c = b.cacc ? b.cacc * 2 : 512; Account *na = new Account[c]; for (int k = 0; k < b.nacc; k++) na[k] = b.acc[k]; delete [] b.acc; b.acc = na; b.cacc = c; }
	int at = 0; while (at < b.nacc && code_cmp (b.acc[at].code, code) < 0) at++;
	for (int k = b.nacc; k > at; k--) b.acc[k] = b.acc[k - 1];
	Account &a = b.acc[at];
	scpy (a.code, code, CODE_MAX); scpy (a.name, name, ACC_NAME_MAX); a.nature = nature; a.hidden = false;
	b.nacc++;
	return at;
}
static void acc_remove (Book &b, int i) { for (int k = i + 1; k < b.nacc; k++) b.acc[k - 1] = b.acc[k]; b.nacc--; }
static const char *acc_name (const Book &b, const char *code) { int i = acc_find (b, code); return i >= 0 ? b.acc[i].name : ""; }
// Its heading's name (the longest code the account's starts with), for an account not in the chart.
static const char *acc_group_name (const Book &b, const char *code)
{
	char t[CODE_MAX]; scpy (t, code, CODE_MAX);
	for (int n = slen (t) - 1; n > 0; n--) { t[n] = '\0'; int i = acc_find (b, t); if (i >= 0) return b.acc[i].name; }
	return "";
}
// A purchase's nature: the account's, else by the code (class 2: investments; 60: goods; others: services).
static char acc_nature (const Book &b, const char *code)
{
	int i = acc_find (b, code);
	if (i >= 0 && b.acc[i].nature) return b.acc[i].nature;
	if (code[0] == '2') return NAT_INVEST;
	if (code[0] == '6' && code[1] == '0') return NAT_GOODS;
	return NAT_SERVICES;
}
// Posted to: in the chart, not a heading.
static bool acc_postable (const Book &b, const char *code) { return !acc_heading (code) && acc_find (b, code) >= 0; }
// Is this a customers' / suppliers' account (its lines name a party)?
static bool acc_party (const Book &b, const char *code)
{
	int k = acc_kind (code);
	if (k == AK_RECV || k == AK_PAY) return code[2] == '0' || code[2] == '1' || seq (code, b.accCustomers) || seq (code, b.accSuppliers);
	return false;
}

// ---- parties -----------------------------------------------------------------------------------------------------------
static int party_index (const Book &b, int id)
{
	if (id <= 0) return -1;
	int lo = 0, hi = b.npty - 1;				// (kept sorted by id: they only grow)
	while (lo <= hi) { int mid = (lo + hi) / 2; if (b.pty[mid].id == id) return mid; if (b.pty[mid].id < id) lo = mid + 1; else hi = mid - 1; }
	return -1;
}
static const Party *party_of (const Book &b, int id) { int i = party_index (b, id); return i >= 0 ? &b.pty[i] : 0; }
static const char *party_name (const Book &b, int id) { const Party *p = party_of (b, id); return p ? p->name : ""; }
static Party &party_add (Book &b)
{
	if (b.npty == b.cpty) { int c = b.cpty ? b.cpty * 2 : 128; Party *np = new Party[c]; for (int k = 0; k < b.npty; k++) np[k] = b.pty[k]; delete [] b.pty; b.pty = np; b.cpty = c; }
	Party &p = b.pty[b.npty++]; party_init (p); p.id = b.nextParty++;
	return p;
}
static int party_by_code (const Book &b, int kind, const char *code)
{
	for (int i = 0; i < b.npty; i++) if (b.pty[i].kind == kind && ci_eq (b.pty[i].code, code)) return i;
	return -1;
}
// A code of its own for a new party: its name's letters (8 at most), numbered when taken.
static void party_make_code (const Book &b, int kind, const char *name, char *out)
{
	char base[PCODE_MAX]; int n = 0;
	for (const char *p = name; *p && n < 8; p++)
	{
		unsigned char c = (unsigned char) *p;
		if (c >= 'a' && c <= 'z') base[n++] = (char) (c - 32);
		else if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) base[n++] = (char) c;
		else if (c >= 0xC0) { unsigned char f = fold (c); if (f >= 'a' && f <= 'z') base[n++] = (char) (f - 32); }
	}
	base[n] = '\0';
	if (!n) scpy (base, kind == PK_CUSTOMER ? "CUST" : "SUPP", PCODE_MAX);
	out[0] = '\0';						// (the party's own code, when out is it: not in the way)
	char t[PCODE_MAX]; scpy (t, base, PCODE_MAX);
	for (int k = 2; party_by_code (b, kind, t) >= 0 && k < 10000; k++) { scpy (t, base, PCODE_MAX); scat_num (t, k, PCODE_MAX); }
	scpy (out, t, PCODE_MAX);
}
// Its collective account, its default VAT code's regime...
static const char *party_account (const Book &b, const Party &p)
{
	if (p.account[0]) return p.account;
	return p.kind == PK_CUSTOMER ? b.accCustomers : b.accSuppliers;
}
// The situation a VAT number and a country imply.
static int regime_of (const char *vat, const char *country)
{
	if (!vat[0]) return country[0] && !(country[0] == 'B' && country[1] == 'E') && !eu_prefix (country) ? PR_WORLD : PR_PRIVATE;
	if (vat[0] == 'B' && vat[1] == 'E') return PR_BE;
	if (eu_prefix (vat)) return PR_EU;
	return PR_WORLD;
}

// ---- journals ------------------------------------------------------------------------------------------------------------
static int jrn_find (const Book &b, const char *code) { for (int i = 0; i < b.njr; i++) if (ci_eq (b.jr[i].code, code)) return i; return -1; }
static int jrn_first (const Book &b, int type) { for (int i = 0; i < b.njr; i++) if (b.jr[i].type == type && !b.jr[i].hidden) return i; return -1; }
static Journal &jrn_add (Book &b, const char *code, const char *name, int type, const char *account = "", const char *iban = "")
{
	Journal &j = b.jr[b.njr < MAXJOURNALS ? b.njr++ : MAXJOURNALS - 1];
	scpy (j.code, code, 6); scpy (j.name, name, 40); j.type = (unsigned char) type; j.hidden = false;
	scpy (j.account, account, CODE_MAX); scpy (j.iban, iban, 36);
	return j;
}

// ---- entries ----------------------------------------------------------------------------------------------------------------
static int entry_index (const Book &b, int id)
{
	int lo = 0, hi = b.ne - 1;				// (kept sorted by id)
	while (lo <= hi) { int mid = (lo + hi) / 2; if (b.e[mid].id == id) return mid; if (b.e[mid].id < id) lo = mid + 1; else hi = mid - 1; }
	return -1;
}
// The next number in a journal's fiscal year.
static int next_number (const Book &b, int journal, int year)
{
	int n = 0;
	for (int i = 0; i < b.ne; i++) if (b.e[i].journal == journal && year_of (b, b.e[i].date) == year && b.e[i].no > n) n = b.e[i].no;
	return n + 1;
}
// The last entry's date in a journal's fiscal year (0: none), the entry `except` left out.
static int last_date (const Book &b, int journal, int year, int except = 0)
{
	int d = 0, no = 0;
	for (int i = 0; i < b.ne; i++)
		if (b.e[i].journal == journal && b.e[i].id != except && year_of (b, b.e[i].date) == year && b.e[i].no > no) { no = b.e[i].no; d = b.e[i].date; }
	return d;
}
// "2026/0012" (the fiscal year's first calendar year, the number).
static void entry_number (const Book &b, const Entry &e, char *out, int cap)
{
	int y = year_of (b, e.date);
	out[0] = '\0';
	scat_num (out, y >= 0 ? y_of (b.yr[y].start) : y_of (e.date), cap);
	scat (out, "/", cap);
	char t[16]; itoa10 (e.no, t);
	for (int k = slen (t); k < 4; k++) scat (out, "0", cap);
	scat (out, t, cap);
}
// Added in its place (by id: a new one gets the next) -> its index.
static int entry_insert (Book &b, Entry &src)
{
	if (b.ne == b.ce) { int c = b.ce ? b.ce * 2 : 256; Entry *ne = new Entry[c]; for (int k = 0; k < b.ne; k++) ne[k] = b.e[k]; delete [] b.e; b.e = ne; b.ce = c; }
	if (src.id <= 0) src.id = b.nextEntry++;
	else if (src.id >= b.nextEntry) b.nextEntry = src.id + 1;
	int at = b.ne; while (at > 0 && b.e[at - 1].id > src.id) at--;
	for (int k = b.ne; k > at; k--) b.e[k] = b.e[k - 1];
	b.e[at] = src; b.ne++;
	entry_init (src);					// (its lines and texts now the book's)
	return at;
}
static void entry_remove (Book &b, int i)
{
	entry_free (b.e[i]);
	for (int k = i + 1; k < b.ne; k++) b.e[k - 1] = b.e[k];
	b.ne--;
}

// ---- locks: a closed year, a filed VAT return's period --------------------------------------------------------------------------
static bool year_closed (const Book &b, int date) { int y = year_of (b, date); return y >= 0 && b.yr[y].closed; }
// The VAT period holding a date: its first and last day.
static void vat_period_of (const Book &b, int date, bool monthly, int *from, int *to)
{
	int y = y_of (date), m = m_of (date);
	if (monthly) { *from = ymd (y, m, 1); *to = month_end (y, m); return; }
	int q = (m - 1) / 3;
	*from = ymd (y, q * 3 + 1, 1); *to = month_end (y, q * 3 + 3);
	(void) b;
}
static void return_range (const VatReturn &r, int *from, int *to)
{
	if (r.monthly) { *from = ymd (r.year, r.period, 1); *to = month_end (r.year, r.period); }
	else { *from = ymd (r.year, r.period * 3 - 2, 1); *to = month_end (r.year, r.period * 3); }
}
// The filed return whose period holds a date (-1: none).
static int return_of (const Book &b, int date)
{
	for (int i = 0; i < b.nret; i++) { int f, t; return_range (b.ret[i], &f, &t); if (date >= f && date <= t) return i; }
	return -1;
}
// An entry's VAT lines: any line with a VAT code.
static bool entry_has_vat (const Entry &e) { for (int i = 0; i < e.nl; i++) if (e.l[i].vat >= 0) return true; return false; }
// Why an entry at that date cannot be changed ("": it can): the year closed, or -- for one with VAT
// lines -- the VAT return of its period filed.
static const char *entry_locked (const Book &b, const Entry &e)
{
	if (year_closed (b, e.date)) return "Its fiscal year is closed.";
	if (entry_has_vat (e) && return_of (b, e.date) >= 0) return "The VAT return of its period has been filed.";
	return "";
}

// ---- matching ("lettrage") -------------------------------------------------------------------------------------------------------
// A line of an entry: where it is.
struct LineRef { int e, l; };
// The open lines of a party (its account's lines not matched), oldest first.
static int open_items (const Book &b, int party, LineRef *out, int cap)
{
	int n = 0;
	for (int i = 0; i < b.ne; i++)
		for (int k = 0; k < b.e[i].nl; k++)
		{
			const Line &l = b.e[i].l[k];
			if (l.party == party && !l.match && l.amount && acc_party (b, l.account) && n < cap) { out[n].e = i; out[n].l = k; n++; }
		}
	return n;
}
// The lines given matched together when they add up to zero (true); their earlier groups undone.
static bool match_lines (Book &b, const LineRef *r, int n)
{
	money s = 0;
	for (int i = 0; i < n; i++) s += b.e[r[i].e].l[r[i].l].amount;
	if (s || n < 2) return false;
	int m = b.nextMatch++;
	for (int i = 0; i < n; i++) b.e[r[i].e].l[r[i].l].match = m;
	b.changes++;
	return true;
}
// A group undone: its lines open again.
static void unmatch (Book &b, int m)
{
	if (!m) return;
	for (int i = 0; i < b.ne; i++) for (int k = 0; k < b.e[i].nl; k++) if (b.e[i].l[k].match == m) b.e[i].l[k].match = 0;
	b.changes++;
}
// A group whose lines no longer add up to zero (an entry changed or deleted): undone.
static void matches_check (Book &b)
{
	// (the groups' sums, by group number)
	int maxm = b.nextMatch;
	money *sum = new money[maxm + 1];
	for (int m = 0; m <= maxm; m++) sum[m] = 0;
	for (int i = 0; i < b.ne; i++) for (int k = 0; k < b.e[i].nl; k++) { int m = b.e[i].l[k].match; if (m > 0 && m <= maxm) sum[m] += b.e[i].l[k].amount; }
	for (int i = 0; i < b.ne; i++) for (int k = 0; k < b.e[i].nl; k++) { int m = b.e[i].l[k].match; if (m > 0 && m <= maxm && sum[m]) b.e[i].l[k].match = 0; }
	delete [] sum;
}

// ---- balances ------------------------------------------------------------------------------------------------------------------
// An account's (or, a prefix: its heading's) debits and credits between two dates (0: no bound).
static void acc_totals (const Book &b, const char *prefix, int from, int to, money *debit, money *credit, bool exact = false)
{
	money d = 0, c = 0; int pn = slen (prefix);
	for (int i = 0; i < b.ne; i++)
	{
		const Entry &e = b.e[i];
		if ((from && e.date < from) || (to && e.date > to)) continue;
		for (int k = 0; k < e.nl; k++)
		{
			const Line &l = e.l[k];
			bool in = true;
			if (exact) in = seq (l.account, prefix);
			else for (int j = 0; j < pn; j++) if (l.account[j] != prefix[j]) { in = false; break; }
			if (!in) continue;
			if (l.amount > 0) d += l.amount; else c -= l.amount;
		}
	}
	*debit = d; *credit = c;
}
static money acc_balance (const Book &b, const char *prefix, int from, int to) { money d, c; acc_totals (b, prefix, from, to, &d, &c); return d - c; }
// A party's balance (its lines on the collective accounts), up to a date (0: all).
static money party_balance (const Book &b, int party, int to = 0, bool openOnly = false)
{
	money s = 0;
	for (int i = 0; i < b.ne; i++)
	{
		if (to && b.e[i].date > to) continue;
		for (int k = 0; k < b.e[i].nl; k++) { const Line &l = b.e[i].l[k]; if (l.party == party && acc_party (b, l.account) && (!openOnly || !l.match)) s += l.amount; }
	}
	return s;
}

} // namespace lg

#endif
