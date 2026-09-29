//
// reports.h -- what Ledger shows and exports: a Report is a table (its columns, its rows -- a heading, a
// line, a subtotal, a total -- each row pointing at what it is about: an entry to open, an account or
// a party to show), made from the books by:
//   * the journal: a period's entries, journal by journal, each with its lines; the totals;
//   * the general ledger: accounts, each with its opening balance, its movements and a running
//     balance, its totals;
//   * the trial balance: each account's debits, credits and balance over the period (the opening
//     entries in), the classes' subtotals;
//   * the customers' / suppliers' balance and their ledger; the open items by age (not due, 1-30,
//     31-60, 61-90, more than 90 days late);
//   * the balance sheet and the income statement in the abbreviated Belgian scheme (their rubrics
//     by the PCMN's classes -- a management view, not the filed annual accounts);
//   * the VAT detail: the lines behind each grid of a period's return.
// And written as CSV (';' between the cells, "1234,56": what a Belgian spreadsheet opens).
//
#ifndef _ledger_reports_h
#define _ledger_reports_h

#include "post.h"

namespace lg {

enum { RS_LINE, RS_HEAD, RS_SUB, RS_TOTAL, RS_DIM, RS_BLANK };
enum { RR_NONE, RR_ENTRY, RR_ACCOUNT, RR_PARTY };
enum { RMAXCOL = 10 };
struct RCol { char title[32]; int width, align; bool money, bal; };	// bal: a balance (shown 1.234,56 D / C)
struct RRow { unsigned char style, indent, refKind; int ref; char *cell[RMAXCOL]; };
struct Report
{
	char title[96], sub[160];
	RCol col[RMAXCOL]; int ncol;
	RRow *r; int nr, cap;
};
static void rpt_init (Report &p) { p.title[0] = p.sub[0] = '\0'; p.ncol = 0; p.r = 0; p.nr = p.cap = 0; }
static void rpt_free (Report &p)
{
	for (int i = 0; i < p.nr; i++) for (int c = 0; c < RMAXCOL; c++) sfree (p.r[i].cell[c]);
	delete [] p.r; p.r = 0; p.nr = p.cap = 0; p.ncol = 0;
}
static void rpt_col (Report &p, const char *title, int width, int align = 0, bool money = false, bool bal = false)
{
	if (p.ncol >= RMAXCOL) return;
	RCol &c = p.col[p.ncol++]; scpy (c.title, title, sizeof c.title); c.width = width; c.align = align; c.money = money; c.bal = bal;
}
// A balance as shown: "1.234,56 D" (a debit), "1.234,56 C" (a credit), "0,00".
static void dc_text (const char *s, char *out, int cap)
{
	if (!s[0]) { out[0] = '\0'; return; }
	if (s[0] == '-') { scpy (out, s + 1, cap); scat (out, " C", cap); return; }
	scpy (out, s, cap);
	bool zero = true; for (const char *q = s; *q; q++) if (*q >= '1' && *q <= '9') zero = false;
	if (!zero) scat (out, " D", cap);
}
static void fmt_dc (money v, char *out) { char t[32]; fmt_money (v, t); dc_text (t, out, 40); }
// A line's description: its party's name and its text (the entry's when it has none).
static void line_desc (const Book &b, const Entry &e, const Line &l, char *out, int cap)
{
	const char *t = l.text[0] ? l.text : e.text;
	out[0] = '\0';
	if (l.party)
	{
		const char *n = party_name (b, l.party);
		scpy (out, n, cap);
		if (t[0] && !seq (t, n)) { scat (out, ": ", cap); scat (out, t, cap); }
		return;
	}
	scpy (out, t, cap);
}
static RRow &rpt_row (Report &p, int style = RS_LINE, int refKind = RR_NONE, int ref = 0)
{
	if (p.nr == p.cap) { int c = p.cap ? p.cap * 2 : 256; RRow *nr = new RRow[c]; for (int i = 0; i < p.nr; i++) nr[i] = p.r[i]; delete [] p.r; p.r = nr; p.cap = c; }
	RRow &r = p.r[p.nr++];
	r.style = (unsigned char) style; r.indent = 0; r.refKind = (unsigned char) refKind; r.ref = ref;
	for (int c = 0; c < RMAXCOL; c++) r.cell[c] = s_empty;
	return r;
}
static void rpt_set (RRow &r, int c, const char *s) { if (c >= 0 && c < RMAXCOL) sset (r.cell[c], s); }
static void rpt_money (RRow &r, int c, money v, bool zeroBlank = true) { char t[32]; if (zeroBlank) fmt_money0 (v, t); else fmt_money (v, t); rpt_set (r, c, t); }
static void rpt_date (RRow &r, int c, int d) { char t[16]; date_show (d, t); rpt_set (r, c, t); }
static void period_text (int from, int to, char *out, int cap)
{
	char a[16], b[16]; date_show (from, a); date_show (to, b);
	scpy (out, "From ", cap); scat (out, a, cap); scat (out, " to ", cap); scat (out, b, cap);
}

// ---- the entries in order -------------------------------------------------------------------------------------------
struct EntryOrder { const Book *b; bool byJournal; };
static int entry_cmp (void *ctx, int x, int y)
{
	const EntryOrder &o = *(const EntryOrder *) ctx;
	const Entry &a = o.b->e[x], &c = o.b->e[y];
	if (o.byJournal && a.journal != c.journal) return a.journal - c.journal;
	if (a.date != c.date) return a.date < c.date ? -1 : 1;
	if (a.journal != c.journal) return a.journal - c.journal;
	return a.no - c.no;
}
// The entries of a journal (-1: all) within dates, sorted -> how many (in idx, the caller's delete []).
static int entries_sorted (const Book &b, int journal, int from, int to, bool byJournal, int **idx)
{
	int *v = new int[b.ne + 1], n = 0;
	for (int i = 0; i < b.ne; i++)
	{
		const Entry &e = b.e[i];
		if ((journal >= 0 && e.journal != journal) || (from && e.date < from) || (to && e.date > to)) continue;
		v[n++] = i;
	}
	EntryOrder o = { &b, byJournal };
	idx_sort (v, n, entry_cmp, &o);
	*idx = v;
	return n;
}
// "VEN 2026/0012".
static void entry_ref (const Book &b, const Entry &e, char *out, int cap)
{
	scpy (out, e.journal >= 0 && e.journal < b.njr ? b.jr[e.journal].code : "?", cap); scat (out, " ", cap);
	char n[24]; entry_number (b, e, n, sizeof n); scat (out, n, cap);
}

// ---- the journal ----------------------------------------------------------------------------------------------------------
static void rpt_journal (const Book &b, Report &p, int journal, int from, int to)
{
	rpt_free (p);
	scpy (p.title, journal >= 0 ? b.jr[journal].name : "Journals", sizeof p.title);
	period_text (from, to, p.sub, sizeof p.sub);
	rpt_col (p, "Date", 11); rpt_col (p, "Document", 14); rpt_col (p, "Account", 8); rpt_col (p, "Name", 24);
	rpt_col (p, "Description", 30); rpt_col (p, "Debit", 13, 1, true); rpt_col (p, "Credit", 13, 1, true);
	int *idx, n = entries_sorted (b, journal, from, to, true, &idx);
	money td = 0, tc = 0, jd = 0, jc = 0; int cj = -1;
	auto jtotal = [&] ()
	{
		if (cj < 0) return;
		RRow &r = rpt_row (p, RS_SUB);
		char t[64] = "Total "; scat (t, b.jr[cj].name, sizeof t); rpt_set (r, 4, t); rpt_money (r, 5, jd, false); rpt_money (r, 6, jc, false);
		rpt_row (p, RS_BLANK);
	};
	for (int i = 0; i < n; i++)
	{
		const Entry &e = b.e[idx[i]];
		if (e.journal != cj) { jtotal (); cj = e.journal; jd = jc = 0; RRow &h = rpt_row (p, RS_HEAD); char t[64]; scpy (t, b.jr[cj].code, sizeof t); scat (t, "  ", sizeof t); scat (t, b.jr[cj].name, sizeof t); rpt_set (h, 0, t); }
		char ref[32]; entry_number (b, e, ref, sizeof ref);
		for (int k = 0; k < e.nl; k++)
		{
			const Line &l = e.l[k];
			RRow &r = rpt_row (p, RS_LINE, RR_ENTRY, e.id);
			if (k == 0) { rpt_date (r, 0, e.date); rpt_set (r, 1, ref); }
			rpt_set (r, 2, l.account);
			rpt_set (r, 3, l.party ? party_name (b, l.party) : acc_name (b, l.account));
			rpt_set (r, 4, l.text);
			if (l.amount > 0) { rpt_money (r, 5, l.amount); jd += l.amount; td += l.amount; }
			else { rpt_money (r, 6, -l.amount); jc -= l.amount; tc -= l.amount; }
		}
	}
	jtotal ();
	RRow &t = rpt_row (p, RS_TOTAL); rpt_set (t, 4, "Total"); rpt_money (t, 5, td, false); rpt_money (t, 6, tc, false);
	delete [] idx;
}

// ---- the general ledger -----------------------------------------------------------------------------------------------------
// The start of the fiscal year holding a date (else the date itself).
static int year_start (const Book &b, int date) { int y = year_of (b, date); return y >= 0 ? b.yr[y].start : date; }
// An account's balance before a date: all its lines before it -- only the fiscal year's for the income
// statement's (their year starts from zero).
static money opening_of (const Book &b, const char *code, int from, bool exact = true)
{
	int start = acc_pl (code) ? year_start (b, from) : 0;
	money s = 0;
	for (int i = 0; i < b.ne; i++)
	{
		const Entry &e = b.e[i];
		if (e.date >= from || (start && e.date < start)) continue;
		for (int k = 0; k < e.nl; k++)
		{
			const char *a = e.l[k].account;
			if (exact ? seq (a, code) : !code_cmp (code, a)) s += e.l[k].amount;
		}
	}
	return s;
}
static bool starts_with (const char *s, const char *pre) { while (*pre) if (*s++ != *pre++) return false; return true; }
// The accounts (not the headings) within [lo, hi] -- codes compared as text, hi taken as a prefix too
// ("6" to "6": every 6...); "" no bound.
static int accounts_used (const Book &b, const char *lo, const char *hi, int *out, int cap)
{
	int n = 0;
	for (int i = 0; i < b.nacc && n < cap; i++)
	{
		const char *c = b.acc[i].code;
		if (acc_heading (c)) continue;
		if (lo[0] && code_cmp (c, lo) < 0 && !starts_with (c, lo)) continue;
		if (hi[0] && code_cmp (c, hi) > 0 && !starts_with (c, hi)) continue;
		out[n++] = i;
	}
	return n;
}
static void rpt_ledger (const Book &b, Report &p, const char *lo, const char *hi, int from, int to, bool zeros)
{
	rpt_free (p);
	scpy (p.title, "General ledger", sizeof p.title);
	period_text (from, to, p.sub, sizeof p.sub);
	rpt_col (p, "Date", 11); rpt_col (p, "Document", 14); rpt_col (p, "Description", 44);
	rpt_col (p, "Debit", 13, 1, true); rpt_col (p, "Credit", 13, 1, true); rpt_col (p, "Balance", 15, 1, true, true);
	int *acc = new int[b.nacc + 1], na = accounts_used (b, lo, hi, acc, b.nacc);
	int *idx, n = entries_sorted (b, -1, from, to, false, &idx);
	money gd = 0, gc = 0;
	for (int a = 0; a < na; a++)
	{
		const Account &ac = b.acc[acc[a]];
		money open = opening_of (b, ac.code, from);
		bool any = open != 0;
		for (int i = 0; i < n && !any; i++) for (int k = 0; k < b.e[idx[i]].nl; k++) if (seq (b.e[idx[i]].l[k].account, ac.code)) { any = true; break; }
		if (!any && !zeros) continue;
		RRow &h = rpt_row (p, RS_HEAD, RR_ACCOUNT, acc[a]);
		char t[96]; scpy (t, ac.code, sizeof t); scat (t, "  ", sizeof t); scat (t, ac.name, sizeof t); rpt_set (h, 0, t);
		money bal = open, d = 0, c = 0;
		if (open) { RRow &o = rpt_row (p, RS_DIM); rpt_set (o, 2, "Balance brought forward"); rpt_money (o, 5, bal, false); }
		for (int i = 0; i < n; i++)
		{
			const Entry &e = b.e[idx[i]];
			for (int k = 0; k < e.nl; k++)
			{
				const Line &l = e.l[k];
				if (!seq (l.account, ac.code)) continue;
				RRow &r = rpt_row (p, RS_LINE, RR_ENTRY, e.id);
				rpt_date (r, 0, e.date);
				char ref[32]; entry_ref (b, e, ref, sizeof ref); rpt_set (r, 1, ref);
				char dsc[200]; line_desc (b, e, l, dsc, sizeof dsc); rpt_set (r, 2, dsc);
				if (l.amount > 0) { rpt_money (r, 3, l.amount); d += l.amount; } else { rpt_money (r, 4, -l.amount); c -= l.amount; }
				bal += l.amount; rpt_money (r, 5, bal, false);
			}
		}
		RRow &s = rpt_row (p, RS_SUB, RR_ACCOUNT, acc[a]);
		char tt[48] = "Total "; scat (tt, ac.code, sizeof tt); rpt_set (s, 2, tt); rpt_money (s, 3, d, false); rpt_money (s, 4, c, false); rpt_money (s, 5, bal, false);
		rpt_row (p, RS_BLANK);
		gd += d; gc += c;
	}
	RRow &t = rpt_row (p, RS_TOTAL); rpt_set (t, 2, "Total"); rpt_money (t, 3, gd, false); rpt_money (t, 4, gc, false);
	delete [] idx; delete [] acc;
}

// ---- the trial balance ---------------------------------------------------------------------------------------------------------
static const char *const CLASS_NAME[10] = { "Off-balance sheet rights and commitments", "Equity, provisions and debts over one year",
	"Formation expenses, fixed assets and receivables over one year", "Stocks and contracts in progress",
	"Receivables and debts within one year", "Investments and cash", "Charges", "Income", "", "" };
// Each account's debits and credits of the fiscal year up to `to`: the balance brought forward (a
// balance sheet account's, from the years before) counted as a debit or a credit, as the year's
// opening; the classes' subtotals; the years before's result not yet appropriated (their income and
// charges, which start again from zero) on a line of its own.
static void rpt_trial (const Book &b, Report &p, int to, bool zeros)
{
	rpt_free (p);
	scpy (p.title, "Trial balance", sizeof p.title);
	int ys = year_start (b, to);
	period_text (ys, to, p.sub, sizeof p.sub);
	rpt_col (p, "Account", 9); rpt_col (p, "Name", 34); rpt_col (p, "Debit", 14, 1, true); rpt_col (p, "Credit", 14, 1, true);
	rpt_col (p, "Debit balance", 14, 1, true); rpt_col (p, "Credit balance", 14, 1, true);
	money *dr = new money[b.nacc + 1], *cr = new money[b.nacc + 1], *fw = new money[b.nacc + 1];
	for (int i = 0; i < b.nacc; i++) dr[i] = cr[i] = fw[i] = 0;
	money prev = 0;
	for (int i = 0; i < b.ne; i++)
	{
		const Entry &e = b.e[i];
		if (e.date > to) continue;
		for (int k = 0; k < e.nl; k++)
		{
			const Line &l = e.l[k];
			int a = acc_find (b, l.account);
			if (a < 0) continue;
			if (e.date < ys) { if (!acc_pl (l.account)) fw[a] += l.amount; else prev += l.amount; continue; }
			if (l.amount > 0) dr[a] += l.amount; else cr[a] -= l.amount;
		}
	}
	for (int i = 0; i < b.nacc; i++) { if (fw[i] > 0) dr[i] += fw[i]; else cr[i] -= fw[i]; }
	money cd = 0, cc = 0, cbd = 0, cbc = 0, td = 0, tc = 0, tbd = 0, tbc = 0; int cls = -1;
	auto ctotal = [&] ()
	{
		if (cls < 0) return;
		RRow &r = rpt_row (p, RS_SUB);
		char t[64] = "Total class "; char k[2] = { (char) ('0' + cls), 0 }; scat (t, k, sizeof t); rpt_set (r, 1, t);
		rpt_money (r, 2, cd, false); rpt_money (r, 3, cc, false); rpt_money (r, 4, cbd); rpt_money (r, 5, cbc);
		rpt_row (p, RS_BLANK);
	};
	for (int i = 0; i < b.nacc; i++)
	{
		const Account &a = b.acc[i];
		if (acc_heading (a.code)) continue;
		if (!dr[i] && !cr[i] && !zeros) continue;
		int k = a.code[0] - '0';
		if (k != cls)
		{
			ctotal (); cls = k; cd = cc = cbd = cbc = 0;
			RRow &h = rpt_row (p, RS_HEAD);
			char t[96] = "Class "; char kk[2] = { a.code[0], 0 }; scat (t, kk, sizeof t); scat (t, "  ", sizeof t);
			if (k >= 0 && k <= 9) scat (t, CLASS_NAME[k], sizeof t);
			rpt_set (h, 0, t);
		}
		RRow &r = rpt_row (p, RS_LINE, RR_ACCOUNT, i);
		rpt_set (r, 0, a.code); rpt_set (r, 1, a.name);
		rpt_money (r, 2, dr[i]); rpt_money (r, 3, cr[i]);
		money bal = dr[i] - cr[i];
		if (bal > 0) rpt_money (r, 4, bal); else rpt_money (r, 5, -bal);
		cd += dr[i]; cc += cr[i]; td += dr[i]; tc += cr[i];
		if (bal > 0) { cbd += bal; tbd += bal; } else { cbc -= bal; tbc -= bal; }
	}
	ctotal ();
	if (prev)
	{
		RRow &r = rpt_row (p, RS_DIM);
		rpt_set (r, 1, prev < 0 ? "Profit of the years before, not appropriated" : "Loss of the years before, not appropriated");
		if (prev > 0) { rpt_money (r, 2, prev); rpt_money (r, 4, prev); td += prev; tbd += prev; }
		else { rpt_money (r, 3, -prev); rpt_money (r, 5, -prev); tc -= prev; tbc -= prev; }
		rpt_row (p, RS_BLANK);
	}
	RRow &t = rpt_row (p, RS_TOTAL); rpt_set (t, 1, "Total"); rpt_money (t, 2, td, false); rpt_money (t, 3, tc, false); rpt_money (t, 4, tbd, false); rpt_money (t, 5, tbc, false);
	delete [] dr; delete [] cr; delete [] fw;
}

// ---- parties -----------------------------------------------------------------------------------------------------------------------
// Each matching group's last date (its lines': a group was still open before it) -- new [], by group.
static int *match_last_dates (const Book &b)
{
	int *d = new int[b.nextMatch + 1];
	for (int m = 0; m <= b.nextMatch; m++) d[m] = 0;
	for (int i = 0; i < b.ne; i++) for (int k = 0; k < b.e[i].nl; k++) { int m = b.e[i].l[k].match; if (m > 0 && m <= b.nextMatch && b.e[i].date > d[m]) d[m] = b.e[i].date; }
	return d;
}
// A party's open items by age at a date: [0] not due yet, [1] 1-30 days late, [2] 31-60, [3] 61-90, [4] more.
static void party_ages (const Book &b, int party, int at, money *age, const int *mlast)
{
	for (int i = 0; i < 5; i++) age[i] = 0;
	for (int i = 0; i < b.ne; i++)
	{
		const Entry &e = b.e[i];
		if (e.date > at) continue;
		for (int k = 0; k < e.nl; k++)
		{
			const Line &l = e.l[k];
			if (l.party != party || !acc_party (b, l.account)) continue;
			if (l.match > 0 && l.match <= b.nextMatch && mlast[l.match] <= at) continue;	// (settled by then)
			int due = l.due ? l.due : e.date;
			int late = days_between (due, at);
			int bucket = late <= 0 ? 0 : late <= 30 ? 1 : late <= 60 ? 2 : late <= 90 ? 3 : 4;
			age[bucket] += l.amount;
		}
	}
}
static void rpt_party_balance (const Book &b, Report &p, int kind, int at)
{
	rpt_free (p);
	scpy (p.title, kind == PK_CUSTOMER ? "Customers' balance" : "Suppliers' balance", sizeof p.title);
	char d[16]; date_show (at, d); scpy (p.sub, "At ", sizeof p.sub); scat (p.sub, d, sizeof p.sub);
	rpt_col (p, "Code", 10); rpt_col (p, "Name", 28); rpt_col (p, "VAT number", 16); rpt_col (p, "Debit", 13, 1, true);
	rpt_col (p, "Credit", 13, 1, true); rpt_col (p, "Balance", 14, 1, true); rpt_col (p, "Overdue", 13, 1, true);
	money td = 0, tc = 0, to = 0;
	int *ml = match_last_dates (b);
	for (int i = 0; i < b.npty; i++)
	{
		const Party &pt = b.pty[i];
		if (pt.kind != kind) continue;
		money d = 0, c = 0;
		for (int x = 0; x < b.ne; x++)
		{
			if (b.e[x].date > at) continue;
			for (int k = 0; k < b.e[x].nl; k++) { const Line &l = b.e[x].l[k]; if (l.party == pt.id && acc_party (b, l.account)) { if (l.amount > 0) d += l.amount; else c -= l.amount; } }
		}
		if (!d && !c) continue;
		money age[5]; party_ages (b, pt.id, at, age, ml);
		money over = age[1] + age[2] + age[3] + age[4];
		RRow &r = rpt_row (p, RS_LINE, RR_PARTY, pt.id);
		rpt_set (r, 0, pt.code); rpt_set (r, 1, pt.name); char v[24]; vat_show (pt.vat, v, sizeof v); rpt_set (r, 2, v);
		rpt_money (r, 3, d); rpt_money (r, 4, c); rpt_money (r, 5, d - c, false);
		money ov = kind == PK_CUSTOMER ? over : -over; if (ov > 0) rpt_money (r, 6, ov);
		td += d; tc += c; if (ov > 0) to += ov;
	}
	RRow &t = rpt_row (p, RS_TOTAL); rpt_set (t, 1, "Total"); rpt_money (t, 3, td, false); rpt_money (t, 4, tc, false); rpt_money (t, 5, td - tc, false); rpt_money (t, 6, to);
	delete [] ml;
}
static void rpt_aged (const Book &b, Report &p, int kind, int at)
{
	rpt_free (p);
	scpy (p.title, kind == PK_CUSTOMER ? "Customers' open items by age" : "Suppliers' open items by age", sizeof p.title);
	char d[16]; date_show (at, d); scpy (p.sub, "At ", sizeof p.sub); scat (p.sub, d, sizeof p.sub);
	rpt_col (p, "Name", 28); rpt_col (p, "Not due", 13, 1, true); rpt_col (p, "1-30 days", 13, 1, true); rpt_col (p, "31-60 days", 13, 1, true);
	rpt_col (p, "61-90 days", 13, 1, true); rpt_col (p, "Over 90", 13, 1, true); rpt_col (p, "Total", 14, 1, true);
	money tot[6] = { 0, 0, 0, 0, 0, 0 };
	int sg = kind == PK_CUSTOMER ? 1 : -1;
	int *ml = match_last_dates (b);
	for (int i = 0; i < b.npty; i++)
	{
		const Party &pt = b.pty[i];
		if (pt.kind != kind) continue;
		money age[5]; party_ages (b, pt.id, at, age, ml);
		money s = 0; for (int k = 0; k < 5; k++) s += age[k];
		if (!s && !age[0] && !age[1] && !age[2] && !age[3] && !age[4]) continue;
		RRow &r = rpt_row (p, RS_LINE, RR_PARTY, pt.id);
		rpt_set (r, 0, pt.name);
		for (int k = 0; k < 5; k++) { rpt_money (r, 1 + k, sg * age[k]); tot[k] += sg * age[k]; }
		rpt_money (r, 6, sg * s, false); tot[5] += sg * s;
	}
	RRow &t = rpt_row (p, RS_TOTAL); rpt_set (t, 0, "Total");
	for (int k = 0; k < 6; k++) rpt_money (t, 1 + k, tot[k], false);
	delete [] ml;
}
// A party's ledger: its lines with a running balance, open or matched.
static void rpt_party_ledger (const Book &b, Report &p, int party, int from, int to)
{
	rpt_free (p);
	const Party *pt = party_of (b, party);
	scpy (p.title, pt ? pt->name : "", sizeof p.title);
	period_text (from, to, p.sub, sizeof p.sub);
	rpt_col (p, "Date", 11); rpt_col (p, "Document", 14); rpt_col (p, "Description", 30); rpt_col (p, "Due", 11);
	rpt_col (p, "Debit", 13, 1, true); rpt_col (p, "Credit", 13, 1, true); rpt_col (p, "Balance", 15, 1, true, true); rpt_col (p, "Matched", 8);
	money bal = party_balance (b, party, date_add (from, -1));
	if (bal) { RRow &o = rpt_row (p, RS_DIM); rpt_set (o, 2, "Balance brought forward"); rpt_money (o, 6, bal, false); }
	int *idx, n = entries_sorted (b, -1, from, to, false, &idx);
	money d = 0, c = 0;
	for (int i = 0; i < n; i++)
	{
		const Entry &e = b.e[idx[i]];
		for (int k = 0; k < e.nl; k++)
		{
			const Line &l = e.l[k];
			if (l.party != party || !acc_party (b, l.account)) continue;
			RRow &r = rpt_row (p, RS_LINE, RR_ENTRY, e.id);
			rpt_date (r, 0, e.date); char ref[32]; entry_ref (b, e, ref, sizeof ref); rpt_set (r, 1, ref);
			rpt_set (r, 2, l.text[0] ? l.text : e.text); rpt_date (r, 3, l.due);
			if (l.amount > 0) { rpt_money (r, 4, l.amount); d += l.amount; } else { rpt_money (r, 5, -l.amount); c -= l.amount; }
			bal += l.amount; rpt_money (r, 6, bal, false);
			rpt_set (r, 7, l.match ? "yes" : "");
		}
	}
	RRow &t = rpt_row (p, RS_TOTAL); rpt_set (t, 2, "Total"); rpt_money (t, 4, d, false); rpt_money (t, 5, c, false); rpt_money (t, 6, bal, false);
	delete [] idx;
}

// ---- the balance sheet and the income statement (the abbreviated scheme) -------------------------------------------------------
// A rubric: its name, its accounts (prefixes, '-' before one taken off; a heading's level: indent).
struct Rubric { const char *name; const char *prefixes; unsigned char indent, total; };
static const Rubric ASSETS[] = {
	{ "FIXED ASSETS", "", 0, 2 },
	{ "Formation expenses", "20", 1, 0 },
	{ "Intangible fixed assets", "21", 1, 0 },
	{ "Tangible fixed assets", "22 23 24 25 26 27", 1, 0 },
	{ "Financial fixed assets", "28", 1, 0 },
	{ "CURRENT ASSETS", "", 0, 2 },
	{ "Amounts receivable after more than one year", "29", 1, 0 },
	{ "Stocks and contracts in progress", "3", 1, 0 },
	{ "Amounts receivable within one year", "40 41", 1, 0 },
	{ "Current investments", "50 51 52 53", 1, 0 },
	{ "Cash at bank and in hand", "54 55 56 57 58", 1, 0 },
	{ "Deferred charges and accrued income", "490 491", 1, 0 },
	{ "Suspense accounts", "499", 1, 0 } };
static const Rubric LIABS[] = {
	{ "EQUITY", "", 0, 2 },
	{ "Capital and contributions", "10 11", 1, 0 },
	{ "Revaluation surpluses", "12", 1, 0 },
	{ "Reserves", "13", 1, 0 },
	{ "Accumulated profits (losses)", "14 *", 1, 0 },
	{ "Result of the year (not appropriated)", "6 7", 1, 0 },
	{ "Investment grants", "15", 1, 0 },
	{ "Advance to associates", "19", 1, 0 },
	{ "PROVISIONS AND DEFERRED TAXES", "16", 0, 1 },
	{ "AMOUNTS PAYABLE", "", 0, 2 },
	{ "Amounts payable after more than one year", "17", 1, 0 },
	{ "Amounts payable within one year", "42 43 44 45 46 47 48", 1, 0 },
	{ "Accrued charges and deferred income", "492 493", 1, 0 } };
static const Rubric INCOME[] = {
	{ "Operating income", "70 71 72 74 76", 0, 3 },
	{ "Turnover", "70", 1, 0 },
	{ "Changes in stocks and contracts in progress", "71", 1, 0 },
	{ "Own work capitalised", "72", 1, 0 },
	{ "Other operating income", "74", 1, 0 },
	{ "Non-recurring operating income", "76", 1, 0 },
	{ "Operating charges", "60 61 62 63 64 66", 0, 3 },
	{ "Raw materials, consumables and goods for resale", "60", 1, 0 },
	{ "Services and other goods", "61", 1, 0 },
	{ "Remuneration, social security and pensions", "62", 1, 0 },
	{ "Depreciation, write-downs and provisions", "63", 1, 0 },
	{ "Other operating charges", "64", 1, 0 },
	{ "Non-recurring operating charges", "66", 1, 0 },
	{ "Financial income", "75", 0, 1 },
	{ "Financial charges", "65", 0, 1 },
	{ "Income taxes", "67 77", 0, 1 },
	{ "Transfers to / from deferred taxes and untaxed reserves", "68 78", 0, 1 } };
// The rubric's balance (debit positive) up to a date: its accounts' lines (the income statement's from
// the fiscal year's start); "*": the years before's result (their income and charges' lines).
static money rubric_sum (const Book &b, const char *prefixes, int from, int to)
{
	money s = 0;
	bool prev = false;
	for (const char *q = prefixes; *q; q++) if (*q == '*') prev = true;
	for (int i = 0; i < b.ne; i++)
	{
		const Entry &e = b.e[i];
		if (e.date > to) continue;
		for (int k = 0; k < e.nl; k++)
		{
			const Line &l = e.l[k];
			bool pl = acc_pl (l.account);
			if (pl && e.date < from) { if (prev) s += l.amount; continue; }
			for (const char *q = prefixes; *q; )
			{
				while (*q == ' ') q++;
				if (!*q) break;
				int n = 0; while (q[n] && q[n] != ' ') n++;
				bool in = q[0] != '*';
				for (int j = 0; j < n && in; j++) if (l.account[j] != q[j]) in = false;
				if (in) { s += l.amount; break; }
				q += n;
			}
		}
	}
	return s;
}
static void rpt_rubrics (const Book &b, Report &p, const Rubric *R, int n, int from, int to, int sign, money *total)
{
	money t = 0;
	for (int i = 0; i < n; i++)
	{
		const Rubric &u = R[i];
		if (u.total == 2)				// (a heading whose amount is its rubrics')
		{
			money s = 0;
			for (int j = i + 1; j < n && R[j].indent > 0; j++) s += sign * rubric_sum (b, R[j].prefixes, from, to);
			RRow &r = rpt_row (p, RS_SUB); rpt_set (r, 0, u.name); rpt_money (r, 2, s, false);
			t += s;
			continue;
		}
		money s = sign * rubric_sum (b, u.prefixes, from, to);
		RRow &r = rpt_row (p, u.indent ? RS_LINE : RS_SUB);
		r.indent = u.indent;
		rpt_set (r, 0, u.name); rpt_set (r, 1, u.prefixes); rpt_money (r, 2, s, !u.indent ? false : true);
		if (u.total == 1) t += s;
	}
	*total = t;
}
static void rpt_balance_sheet (const Book &b, Report &p, int at)
{
	rpt_free (p);
	scpy (p.title, "Balance sheet (abbreviated scheme)", sizeof p.title);
	char d[16]; date_show (at, d); scpy (p.sub, "At ", sizeof p.sub); scat (p.sub, d, sizeof p.sub);
	rpt_col (p, "Rubric", 46); rpt_col (p, "Accounts", 20); rpt_col (p, "Amount", 16, 1, true);
	int from = year_start (b, at);
	RRow &h1 = rpt_row (p, RS_HEAD); rpt_set (h1, 0, "ASSETS");
	money ta; rpt_rubrics (b, p, ASSETS, sizeof ASSETS / sizeof ASSETS[0], from, at, 1, &ta);
	RRow &t1 = rpt_row (p, RS_TOTAL); rpt_set (t1, 0, "TOTAL ASSETS"); rpt_money (t1, 2, ta, false);
	rpt_row (p, RS_BLANK);
	RRow &h2 = rpt_row (p, RS_HEAD); rpt_set (h2, 0, "EQUITY AND LIABILITIES");
	money tl; rpt_rubrics (b, p, LIABS, sizeof LIABS / sizeof LIABS[0], from, at, -1, &tl);
	RRow &t2 = rpt_row (p, RS_TOTAL); rpt_set (t2, 0, "TOTAL EQUITY AND LIABILITIES"); rpt_money (t2, 2, tl, false);
	if (ta != tl) { rpt_row (p, RS_BLANK); RRow &w = rpt_row (p, RS_DIM); char t[32]; fmt_money (ta - tl, t); char m[96] = "Difference (entries not balanced?): "; scat (m, t, sizeof m); rpt_set (w, 0, m); }
}
static money income_result (const Book &b, int from, int to) { return -rubric_sum (b, "6 7", from, to); }
static void rpt_income (const Book &b, Report &p, int from, int to)
{
	rpt_free (p);
	scpy (p.title, "Income statement (abbreviated scheme)", sizeof p.title);
	period_text (from, to, p.sub, sizeof p.sub);
	rpt_col (p, "Rubric", 46); rpt_col (p, "Accounts", 20); rpt_col (p, "Amount", 16, 1, true);
	money inc = -rubric_sum (b, "70 71 72 74 76", from, to), chg = rubric_sum (b, "60 61 62 63 64 66", from, to);
	money fin = -rubric_sum (b, "75", from, to), finc = rubric_sum (b, "65", from, to), tax = rubric_sum (b, "67 77", from, to), def = rubric_sum (b, "68 78", from, to);
	for (unsigned i = 0; i < sizeof INCOME / sizeof INCOME[0]; i++)
	{
		const Rubric &u = INCOME[i];
		bool credit = u.prefixes[0] == '7';
		money s = rubric_sum (b, u.prefixes, from, to);
		if (credit) s = -s;
		RRow &r = rpt_row (p, u.indent ? RS_LINE : RS_SUB);
		r.indent = u.indent; rpt_set (r, 0, u.name); rpt_set (r, 1, u.prefixes); rpt_money (r, 2, s, u.indent != 0);
		if (i == 12) { RRow &o = rpt_row (p, RS_SUB); rpt_set (o, 0, "OPERATING PROFIT (LOSS)"); rpt_money (o, 2, inc - chg, false); }
		if (i == 14) { RRow &o = rpt_row (p, RS_SUB); rpt_set (o, 0, "PROFIT (LOSS) BEFORE TAXES"); rpt_money (o, 2, inc - chg + fin - finc, false); }
	}
	RRow &t = rpt_row (p, RS_TOTAL); rpt_set (t, 0, "PROFIT (LOSS) OF THE PERIOD"); rpt_money (t, 2, inc - chg + fin - finc - tax - def, false);
}

// ---- the VAT detail --------------------------------------------------------------------------------------------------------------
static void rpt_vat_detail (const Book &b, Report &p, int from, int to)
{
	rpt_free (p);
	scpy (p.title, "VAT detail by grid", sizeof p.title);
	period_text (from, to, p.sub, sizeof p.sub);
	rpt_col (p, "Grid", 6); rpt_col (p, "Date", 11); rpt_col (p, "Document", 14); rpt_col (p, "Party", 22); rpt_col (p, "Account", 8);
	rpt_col (p, "Code", 7); rpt_col (p, "Amount", 14, 1, true);
	money grid[100]; vat_grids (b, from, to, grid);
	int *idx, n = entries_sorted (b, -1, from, to, false, &idx);
	for (int gi = 0; gi < NGRIDS; gi++)
	{
		int g = GRIDS[gi];
		if (g == 71 || g == 72 || g == 91) continue;
		bool any = false;
		for (int i = 0; i < n; i++)
		{
			const Entry &e = b.e[idx[i]];
			for (int k = 0; k < e.nl; k++)
			{
				int gg[4], m = line_grids (b, e, e.l[k], gg, 4);
				for (int j = 0; j < m; j++) if (gg[j] == g)
				{
					if (!any) { RRow &h = rpt_row (p, RS_HEAD); char t[96]; grid_label (g, t); scat (t, "  ", sizeof t); scat (t, grid_name (g), sizeof t); rpt_set (h, 0, t); any = true; }
					RRow &r = rpt_row (p, RS_LINE, RR_ENTRY, e.id);
					char gl[4]; grid_label (g, gl); rpt_set (r, 0, gl); rpt_date (r, 1, e.date);
					char ref[32]; entry_ref (b, e, ref, sizeof ref); rpt_set (r, 2, ref);
					rpt_set (r, 3, party_name (b, e.party ? e.party : e.l[k].party)); rpt_set (r, 4, e.l[k].account);
					rpt_set (r, 5, vat_code (e.l[k].vat)); rpt_money (r, 6, e.l[k].amount * grid_sign (g), false);
				}
			}
		}
		if (any) { RRow &s = rpt_row (p, RS_SUB); char t[32] = "Grid "; char gl[4]; grid_label (g, gl); scat (t, gl, sizeof t); rpt_set (s, 3, t); rpt_money (s, 6, grid[g], false); rpt_row (p, RS_BLANK); }
	}
	delete [] idx;
}

// ---- CSV ---------------------------------------------------------------------------------------------------------------------------
static void rpt_csv (const Report &p, Out &o)
{
	for (int c = 0; c < p.ncol; c++) { if (c) o.put (';'); put_csv (o, p.col[c].title); }
	o.puts ("\r\n");
	for (int i = 0; i < p.nr; i++)
	{
		const RRow &r = p.r[i];
		if (r.style == RS_BLANK) continue;
		for (int c = 0; c < p.ncol; c++)
		{
			if (c) o.put (';');
			const char *s = r.cell[c];
			if (p.col[c].money && s[0])			// (1.234,56 -> 1234,56: a number to a spreadsheet)
			{
				char t[40]; int n = 0;
				for (const char *q = s; *q && n < 39; q++) if (*q != '.') t[n++] = *q;
				t[n] = '\0';
				o.puts (t);
			}
			else put_csv (o, s);
		}
		o.puts ("\r\n");
	}
}

} // namespace lg

#endif
