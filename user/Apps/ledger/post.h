//
// post.h -- the documents an entry is typed as, and the lines they make:
//
//   * an INVOICE (the sales and purchases journals; a credit note the same, flagged): a party, a
//     date, a due date, a reference, a structured communication, its lines (an account, a
//     description, an amount excluding VAT, a VAT code, the tax -- computed, or as the invoice says
//     it). Posted: the party's line (its collective account, the total to pay, due then), a base
//     line per invoice line (its tax kept in it: aux), the tax's lines grouped by code (451000 on a
//     sale; 411000 its deductible part on a purchase, the rest a cost on the line's account; a
//     reverse charge's tax also due, on 451000). A sale's lines credit and its party debits; a
//     purchase the other way; a credit note turns them round;
//   * a STATEMENT (the financial journals): its old and new balance, its movements (an amount in or
//     out; a party -- then its collective account -- or an account; a description; the open lines
//     it pays, matched with it when saved). Posted as pairs: the journal's account, the counterpart;
//   * a MISCELLANEOUS operation: the lines themselves (misc editor; checked here: balanced).
//
// entry_save puts an entry in the book (a new one numbered in its journal's fiscal year; a changed
// one keeps its number and id; the matchings its change broke undone).
//
#ifndef _ledger_post_h
#define _ledger_post_h

#include "vat.h"

namespace lg {

// ---- invoices ---------------------------------------------------------------------------------------------------------
struct InvLine { char account[CODE_MAX]; char text[80]; money net; int vat; money tax; bool taxSet; };
struct Invoice
{
	int id, journal, no; bool credit;
	int party, date, due;
	char ref[32], comm[13], text[96];
	InvLine *l; int nl, cap;
};
static void inv_init (Invoice &v)
{
	v.id = 0; v.journal = 0; v.no = 0; v.credit = false; v.party = 0; v.date = v.due = 0;
	v.ref[0] = v.comm[0] = v.text[0] = '\0'; v.l = 0; v.nl = v.cap = 0;
}
static void inv_free (Invoice &v) { delete [] v.l; v.l = 0; v.nl = v.cap = 0; }
static InvLine &inv_add (Invoice &v)
{
	if (v.nl == v.cap) { int c = v.cap ? v.cap * 2 : 8; InvLine *nl = new InvLine[c]; for (int i = 0; i < v.nl; i++) nl[i] = v.l[i]; delete [] v.l; v.l = nl; v.cap = c; }
	InvLine &l = v.l[v.nl++];
	l.account[0] = l.text[0] = '\0'; l.net = 0; l.vat = -1; l.tax = 0; l.taxSet = false;
	return l;
}
static void inv_remove (Invoice &v, int i) { if (i < 0 || i >= v.nl) return; for (int k = i + 1; k < v.nl; k++) v.l[k - 1] = v.l[k]; v.nl--; }
static bool inv_sale (const Book &b, const Invoice &v) { return v.journal >= 0 && v.journal < b.njr && b.jr[v.journal].type == JT_SALES; }
// A line's tax: as typed, or its code's rate on its amount.
static money inv_tax (const InvLine &l) { if (l.taxSet) return l.tax; if (l.vat < 0 || l.vat >= NVAT) return 0; return tax_of (l.net, VAT_DEFS[l.vat].rate); }
// The totals: excluding VAT, the VAT charged (a reverse charge's not: the buyer's own), to pay.
static void inv_totals (const Invoice &v, money *net, money *tax, money *total, money *reverse = 0)
{
	money n = 0, t = 0, r = 0;
	for (int i = 0; i < v.nl; i++) { n += v.l[i].net; money x = inv_tax (v.l[i]); if (vat_reverse (v.l[i].vat)) r += x; else t += x; }
	*net = n; *tax = t; *total = n + t; if (reverse) *reverse = r;
}
// The due date the party's terms give.
static int inv_due_of (const Book &b, int party, int date)
{
	const Party *p = party_of (b, party);
	return date_add (date, p ? p->terms : 30);
}
// A sale's structured communication: the year and the number (ten digits), their check.
static void inv_make_comm (const Book &b, const Invoice &v, char *d12)
{
	int y = year_of (b, v.date);
	long long ten = (long long) (y >= 0 ? y_of (b.yr[y].start) : y_of (v.date)) * 1000000LL + v.no % 1000000;
	ogm_make (ten, d12);
}
// What forbids posting it ("": nothing).
static const char *inv_check (const Book &b, const Invoice &v)
{
	if (v.journal < 0 || v.journal >= b.njr || (b.jr[v.journal].type != JT_SALES && b.jr[v.journal].type != JT_PURCH)) return "Choose a sales or purchases journal.";
	bool sale = inv_sale (b, v);
	const Party *p = party_of (b, v.party);
	if (!p) return sale ? "Choose the customer." : "Choose the supplier.";
	if (!date_ok (v.date)) return "Type the invoice's date.";
	int y = year_of (b, v.date);
	if (y < 0) return "The date is in no fiscal year (Settings > Fiscal years).";
	if (b.yr[y].closed) return "The fiscal year of that date is closed.";
	if (v.due && v.due < v.date) return "The due date is before the invoice's date.";
	int nz = 0;
	for (int i = 0; i < v.nl; i++)
	{
		const InvLine &l = v.l[i];
		if (!l.net && !l.account[0]) continue;
		if (!l.account[0]) return "A line has no account.";
		if (!acc_postable (b, l.account)) return "A line's account is not in the chart (or is a heading).";
		if (acc_party (b, l.account)) return "A line posts to a customers' or suppliers' account: choose a revenue or expense account.";
		if (l.vat >= 0 && VAT_DEFS[l.vat].side != (sale ? VS_SALES : VS_PURCH)) return sale ? "A line has a purchases' VAT code." : "A line has a sales' VAT code.";
		if (b.vatRegime == VR_NORMAL && l.vat < 0 && l.net) return "A line has no VAT code.";
		if (l.net) nz++;
	}
	if (!nz) return "The invoice has no amount.";
	money n, t, tot; inv_totals (v, &n, &t, &tot);
	if (!tot && !n) return "The invoice's total is zero.";
	for (int i = 0; i < v.nl; i++) if (v.l[i].vat >= 0 && return_of (b, v.date) >= 0) return "The VAT return of that period has been filed (VAT > the period: reopen it).";
	return "";
}
// The entry an invoice makes.
static void inv_to_entry (const Book &b, const Invoice &v, Entry &e)
{
	entry_init (e);
	bool sale = inv_sale (b, v);
	e.id = v.id; e.journal = v.journal; e.no = v.no; e.date = v.date; e.due = v.due ? v.due : v.date; e.party = v.party;
	e.flags = v.credit ? EF_CREDIT : 0;
	scpy (e.ref, v.ref, sizeof e.ref); scpy (e.comm, v.comm, sizeof e.comm); sset (e.text, v.text);
	int s = sale ? -1 : 1; if (v.credit) s = -s;		// the base's sign
	const Party *p = party_of (b, v.party);
	money net, tax, total; inv_totals (v, &net, &tax, &total);
	{
		Line &l = entry_add_line (e);
		scpy (l.account, p ? party_account (b, *p) : (sale ? b.accCustomers : b.accSuppliers), CODE_MAX);
		l.amount = -s * total; l.party = v.party; l.due = e.due;
		sset (l.text, v.text[0] ? v.text : p ? p->name : "");
	}
	for (int i = 0; i < v.nl; i++)
	{
		const InvLine &il = v.l[i];
		if (!il.net && !inv_tax (il)) continue;
		money t = inv_tax (il);
		Line &l = entry_add_line (e);
		scpy (l.account, il.account, CODE_MAX); l.amount = s * il.net; l.vat = (signed char) il.vat; l.role = LR_BASE; l.aux = t;
		sset (l.text, il.text[0] ? il.text : v.text);
		if (!sale && il.vat >= 0 && t)			// (the part not deductible: a cost on the line's account)
		{
			money nd = t - share_of (t, VAT_DEFS[il.vat].deduct);
			if (nd)
			{
				Line &n2 = entry_add_line (e);
				scpy (n2.account, il.account, CODE_MAX); n2.amount = s * nd; n2.vat = (signed char) il.vat; n2.role = LR_ND;
				sset (n2.text, "VAT not deductible");
			}
		}
	}
	// the taxes, a line a code (and its reverse charge's due tax)
	for (int c = 0; c < NVAT; c++)
	{
		money ded = 0, due = 0, base = 0; bool any = false;
		for (int i = 0; i < v.nl; i++)
		{
			if (v.l[i].vat != c) continue;
			money t = inv_tax (v.l[i]);
			any = true; base += v.l[i].net;
			ded += sale ? t : share_of (t, VAT_DEFS[c].deduct);
			if (VAT_DEFS[c].due) due += t;
		}
		if (!any) continue;
		if (ded)
		{
			Line &l = entry_add_line (e);
			scpy (l.account, sale ? b.accVatDue : b.accVatDeduct, CODE_MAX); l.amount = s * ded; l.vat = (signed char) c; l.role = LR_TAX; l.aux = s * base;
			char t[80] = "VAT "; scat (t, VAT_DEFS[c].code, sizeof t); sset (l.text, t);
		}
		if (due)
		{
			Line &l = entry_add_line (e);
			scpy (l.account, b.accVatDue, CODE_MAX); l.amount = -s * due; l.vat = (signed char) c; l.role = LR_DUE; l.aux = s * base;
			char t[80] = "VAT due "; scat (t, VAT_DEFS[c].code, sizeof t); sset (l.text, t);
		}
	}
}
// The invoice an entry was made from (false: it is no invoice -- a hand-made entry in the journal).
static bool inv_from_entry (const Book &b, const Entry &e, Invoice &v)
{
	inv_free (v); inv_init (v);
	if (e.journal < 0 || e.journal >= b.njr || (b.jr[e.journal].type != JT_SALES && b.jr[e.journal].type != JT_PURCH)) return false;
	v.id = e.id; v.journal = e.journal; v.no = e.no; v.credit = (e.flags & EF_CREDIT) != 0;
	v.party = e.party; v.date = e.date; v.due = e.due;
	scpy (v.ref, e.ref, sizeof v.ref); scpy (v.comm, e.comm, sizeof v.comm); scpy (v.text, e.text, sizeof v.text);
	bool sale = inv_sale (b, v);
	int s = sale ? -1 : 1; if (v.credit) s = -s;
	bool hasParty = false;
	for (int k = 0; k < e.nl; k++)
	{
		const Line &l = e.l[k];
		if (l.role == LR_BASE || (l.role == LR_NONE && !acc_party (b, l.account) && acc_kind (l.account) != AK_VAT_IN && acc_kind (l.account) != AK_VAT_OUT))
		{
			InvLine &il = inv_add (v);
			scpy (il.account, l.account, CODE_MAX); scpy (il.text, l.text, sizeof il.text);
			il.net = s * l.amount; il.vat = l.vat;
			il.tax = l.role == LR_BASE ? l.aux : 0;
			il.taxSet = il.tax != (il.vat >= 0 ? tax_of (il.net, VAT_DEFS[il.vat].rate) : 0);
			if (seq (il.text, v.text)) il.text[0] = '\0';
		}
		else if (acc_party (b, l.account) && l.party) hasParty = true;
	}
	return hasParty && v.nl > 0;
}

// ---- statements -------------------------------------------------------------------------------------------------------
enum { MAXPAY = 12 };
struct PayRef { int entry, line; };			// an open line it pays: its entry's id, the line's index
struct StLine
{
	int party; char account[CODE_MAX]; char text[80];
	money amount;					// in > 0, out < 0
	PayRef pay[MAXPAY]; int npay;			// the open lines it settles (matched when saved)
	int match;					// (read back) its counterpart's matching group
};
struct Statement
{
	int id, journal, no, date;
	money old, now;
	char text[96];
	StLine *l; int nl, cap;
};
static void st_init (Statement &s) { s.id = s.journal = s.no = s.date = 0; s.old = s.now = 0; s.text[0] = '\0'; s.l = 0; s.nl = s.cap = 0; }
static void st_free (Statement &s) { delete [] s.l; s.l = 0; s.nl = s.cap = 0; }
static StLine &st_add (Statement &s)
{
	if (s.nl == s.cap) { int c = s.cap ? s.cap * 2 : 8; StLine *nl = new StLine[c]; for (int i = 0; i < s.nl; i++) nl[i] = s.l[i]; delete [] s.l; s.l = nl; s.cap = c; }
	StLine &l = s.l[s.nl++];
	l.party = 0; l.account[0] = l.text[0] = '\0'; l.amount = 0; l.npay = 0; l.match = 0;
	return l;
}
static void st_remove (Statement &s, int i) { if (i < 0 || i >= s.nl) return; for (int k = i + 1; k < s.nl; k++) s.l[k - 1] = s.l[k]; s.nl--; }
static money st_sum (const Statement &s) { money t = 0; for (int i = 0; i < s.nl; i++) t += s.l[i].amount; return t; }
// A financial journal's balance before a date (its account's lines): a new statement's old balance.
static money fin_balance_before (const Book &b, int journal, int date, int exceptId = 0)
{
	const char *acc = b.jr[journal].account;
	money s = 0;
	for (int i = 0; i < b.ne; i++)
	{
		const Entry &e = b.e[i];
		if (e.id == exceptId || e.date > date) continue;
		if (e.date == date && e.journal == journal && exceptId && e.id > exceptId) continue;	// (the same day's statements made after it)
		for (int k = 0; k < e.nl; k++) if (seq (e.l[k].account, acc)) s += e.l[k].amount;
	}
	return s;
}
static const char *st_check (const Book &b, const Statement &s)
{
	if (s.journal < 0 || s.journal >= b.njr || !jt_fin (b.jr[s.journal].type)) return "Choose a bank or cash journal.";
	if (!acc_postable (b, b.jr[s.journal].account)) return "The journal's account is not in the chart (Settings > Journals).";
	if (!date_ok (s.date)) return "Type the statement's date.";
	int y = year_of (b, s.date);
	if (y < 0) return "The date is in no fiscal year (Settings > Fiscal years).";
	if (b.yr[y].closed) return "The fiscal year of that date is closed.";
	int n = 0;
	for (int i = 0; i < s.nl; i++)
	{
		const StLine &l = s.l[i];
		if (!l.amount && !l.party && !l.account[0]) continue;
		if (!l.amount) return "A movement has no amount.";
		if (!l.party && !l.account[0]) return "A movement has neither a party nor an account.";
		if (l.party && !party_of (b, l.party)) return "A movement's party is unknown.";
		if (!l.party && !acc_postable (b, l.account)) return "A movement's account is not in the chart (or is a heading).";
		if (!l.party && acc_party (b, l.account)) return "A movement on a customers' or suppliers' account needs its party.";
		if (!l.party && seq (l.account, b.jr[s.journal].account)) return "A movement posts to the journal's own account.";
		n++;
	}
	if (!n) return "The statement has no movement.";
	if (s.old + st_sum (s) != s.now) return "The new balance is not the old one plus the movements.";
	return "";
}
static void st_to_entry (const Book &b, const Statement &s, Entry &e)
{
	entry_init (e);
	e.id = s.id; e.journal = s.journal; e.no = s.no; e.date = s.date; e.due = s.date;
	sset (e.text, s.text); e.stmtOld = s.old; e.stmtNew = s.now;
	for (int i = 0; i < s.nl; i++)
	{
		const StLine &sl = s.l[i];
		if (!sl.amount) continue;
		const Party *p = party_of (b, sl.party);
		Line &bl = entry_add_line (e);
		scpy (bl.account, b.jr[s.journal].account, CODE_MAX); bl.amount = sl.amount;
		sset (bl.text, sl.text[0] ? sl.text : p ? p->name : "");
		Line &cl = entry_add_line (e);
		scpy (cl.account, p ? party_account (b, *p) : sl.account, CODE_MAX); cl.amount = -sl.amount;
		cl.party = p ? sl.party : 0; cl.due = s.date;
		sset (cl.text, sl.text[0] ? sl.text : p ? p->name : "");
	}
}
static bool st_from_entry (const Book &b, const Entry &e, Statement &s)
{
	st_free (s); st_init (s);
	if (e.journal < 0 || e.journal >= b.njr || !jt_fin (b.jr[e.journal].type)) return false;
	s.id = e.id; s.journal = e.journal; s.no = e.no; s.date = e.date; s.old = e.stmtOld; s.now = e.stmtNew;
	scpy (s.text, e.text, sizeof s.text);
	const char *acc = b.jr[e.journal].account;
	for (int k = 0; k + 1 < e.nl; k += 2)
	{
		const Line &bl = e.l[k], &cl = e.l[k + 1];
		if (!seq (bl.account, acc) || bl.amount != -cl.amount) return false;	// (not a statement's pairs)
		StLine &sl = st_add (s);
		sl.party = cl.party; sl.amount = bl.amount; sl.match = cl.match;
		if (!cl.party) scpy (sl.account, cl.account, CODE_MAX);
		scpy (sl.text, cl.text, sizeof sl.text);
	}
	if (e.nl % 2) return false;
	if (!s.old && !s.now) { s.old = fin_balance_before (b, e.journal, e.date, e.id); s.now = s.old + st_sum (s); }
	return true;
}

// ---- miscellaneous operations ---------------------------------------------------------------------------------------------
static const char *misc_check (const Book &b, const Entry &e)
{
	if (e.journal < 0 || e.journal >= b.njr) return "Choose a journal.";
	if (!date_ok (e.date)) return "Type the entry's date.";
	int y = year_of (b, e.date);
	if (y < 0) return "The date is in no fiscal year (Settings > Fiscal years).";
	if (b.yr[y].closed) return "The fiscal year of that date is closed.";
	int n = 0;
	for (int k = 0; k < e.nl; k++)
	{
		const Line &l = e.l[k];
		if (!l.amount) continue;
		if (!acc_postable (b, l.account)) return "A line's account is not in the chart (or is a heading).";
		if (acc_party (b, l.account) && !party_of (b, l.party)) return "A line on a customers' or suppliers' account needs its party.";
		n++;
	}
	if (n < 2) return "An entry needs two lines at least.";
	if (entry_sum (e)) return "The entry does not balance: its debits and credits differ.";
	if (entry_has_vat (e) && return_of (b, e.date) >= 0) return "The VAT return of that period has been filed (VAT > the period: reopen it).";
	return "";
}
// The lines without an amount dropped; a VAT code's role set by the account (451 / 411: its tax).
static void misc_tidy (Entry &e)
{
	int k = 0;
	for (int i = 0; i < e.nl; i++)
	{
		if (!e.l[i].amount) { line_free (e.l[i]); continue; }
		Line &l = e.l[i];
		if (l.vat >= 0 && l.role == LR_NONE) { int kd = acc_kind (l.account); l.role = kd == AK_VAT_IN || kd == AK_VAT_OUT ? LR_TAX : LR_BASE; }
		if (l.vat < 0) l.role = LR_NONE;
		e.l[k++] = l;
	}
	e.nl = k;
}

// ---- saving --------------------------------------------------------------------------------------------------------------
// The entry put in the book (it takes its lines): a new one (id 0) numbered; a changed one (its id
// in the book) keeping its number. -> its index.
static int entry_save (Book &b, Entry &e)
{
	int old = e.id ? entry_index (b, e.id) : -1;
	if (old >= 0)
	{
		const Entry &o = b.e[old];
		int yo = year_of (b, o.date), yn = year_of (b, e.date);
		e.no = o.journal == e.journal && yo == yn ? o.no : next_number (b, e.journal, yn);
		entry_remove (b, old);
	}
	else { e.id = 0; e.no = next_number (b, e.journal, year_of (b, e.date)); }
	int i = entry_insert (b, e);
	matches_check (b);
	b.changes++;
	return i;
}
// The open lines a statement's movements pay, matched with them (those adding up to zero).
static void st_apply_matches (Book &b, int ei, const Statement &s)
{
	int k = 0;
	for (int i = 0; i < s.nl; i++)
	{
		const StLine &sl = s.l[i];
		if (!sl.amount) continue;
		int cl = 2 * k + 1; k++;
		if (!sl.npay || cl >= b.e[ei].nl) continue;
		LineRef r[MAXPAY + 1]; int n = 0;
		r[n].e = ei; r[n].l = cl; n++;
		for (int j = 0; j < sl.npay; j++)
		{
			int x = entry_index (b, sl.pay[j].entry);
			if (x < 0 || sl.pay[j].line < 0 || sl.pay[j].line >= b.e[x].nl) continue;
			if (b.e[x].l[sl.pay[j].line].match) unmatch (b, b.e[x].l[sl.pay[j].line].match);
			r[n].e = x; r[n].l = sl.pay[j].line; n++;
		}
		match_lines (b, r, n);
	}
}
static void entry_delete (Book &b, int i)
{
	entry_remove (b, i);
	matches_check (b);
	b.changes++;
}
// May it be deleted? A sales invoice only if it is its journal's last (the numbers follow each other).
static const char *entry_can_delete (const Book &b, int i)
{
	const Entry &e = b.e[i];
	const char *w = entry_locked (b, e);
	if (w[0]) return w;
	if (b.jr[e.journal].type == JT_SALES)
	{
		int y = year_of (b, e.date);
		if (next_number (b, e.journal, y) != e.no + 1) return "Only the journal's last sales document can be deleted: make a credit note instead.";
	}
	return "";
}

} // namespace lg

#endif
