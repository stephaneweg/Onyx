//
// commerce.h -- the commercial documents, which are not posted: a QUOTE (its validity), a customer's ORDER
// (its delivery date), a DELIVERY NOTE, a PURCHASE ORDER to a supplier. Each numbered in its kind and year,
// dated, for a party, with its lines (a description, a quantity -- thousandths: 2,5 hours --, a unit price
// excluding VAT, a VAT code, the account the invoice will take) and its state (draft, sent, accepted,
// refused, done). One becomes the next: a quote an order, an order a delivery note, any an invoice (its
// lines the invoice's: the quantity times the price, the description "2,5 x ..."); the documents made
// remember where they came from. Printed by Writer from a template (print.h).
//
#ifndef _ledger_commerce_h
#define _ledger_commerce_h

#include "post.h"

namespace lg {

static const char *const CD_NAME[CD_COUNT] = { "Quote", "Order", "Delivery note", "Purchase order" };
static const char *const CD_PLURAL[CD_COUNT] = { "Quotes", "Orders", "Delivery notes", "Purchase orders" };
static const char *const CD_SHORT[CD_COUNT] = { "Quote", "Order", "Delivery", "P. order" };
static const char *const CD_KEY[CD_COUNT] = { "quote", "order", "delivery", "porder" };
static const char *const CS_NAME[CS_COUNT] = { "Draft", "Sent", "Accepted", "Refused", "Done" };
static const char *const CS_KEY[CS_COUNT] = { "draft", "sent", "accepted", "refused", "done" };
static bool cd_sale (int kind) { return kind != CD_PORDER; }

// ---- quantities -----------------------------------------------------------------------------------------------------------
// "2,5" / "1.200" / "3" -> thousandths; false when not a number.
static bool qty_parse (const char *s, long long *q)
{
	char t[40]; trim_copy (t, s, sizeof t);
	if (!t[0]) { *q = 0; return true; }
	return parse_num (t, 3, q, 0);
}
// Thousandths -> "2,5" (no useless zeros), "1.200" (grouped), "3".
static void qty_show (long long q, char *out)
{
	char t[40]; fmt_money (q / 10, t);				// (hundredths: then the third decimal if any)
	int n = slen (t);
	if (q % 10) { char d[2] = { (char) ('0' + (q < 0 ? -q : q) % 10), 0 }; scat (t, d, 40); n++; }
	else { while (n > 0 && t[n - 1] == '0') n--; if (n > 0 && t[n - 1] == ',') n--; t[n] = '\0'; }
	scpy (out, t, 40);
}
// A line's amount: its quantity times its price, rounded half away from zero.
static money cline_total (const CLine &l)
{
	long long p = l.qty * l.price, q = labs_ (p) / 1000, r = labs_ (p) % 1000;
	if (r >= 500) q++;
	return p < 0 ? -q : q;
}
static money cline_tax (const CLine &l) { return l.vat >= 0 && l.vat < NVAT ? tax_of (cline_total (l), VAT_DEFS[l.vat].rate) : 0; }
static void cdoc_totals (const CDoc &d, money *net, money *tax, money *total)
{
	money n = 0, t = 0;
	for (int i = 0; i < d.nl; i++) { n += cline_total (d.l[i]); if (!vat_reverse (d.l[i].vat)) t += cline_tax (d.l[i]); }
	*net = n; *tax = t; *total = n + t;
}

// ---- lines ----------------------------------------------------------------------------------------------------------------
static CLine &cdoc_add_line (CDoc &d)
{
	if (d.nl == d.cap) { int c = d.cap ? d.cap * 2 : 8; CLine *nl = new CLine[c]; for (int i = 0; i < d.nl; i++) nl[i] = d.l[i]; delete [] d.l; d.l = nl; d.cap = c; }
	CLine &l = d.l[d.nl++];
	l.text = s_empty; l.qty = 1000; l.price = 0; l.vat = -1; l.account[0] = '\0';
	return l;
}
static void cdoc_remove_line (CDoc &d, int i)
{
	if (i < 0 || i >= d.nl) return;
	cline_free (d.l[i]);
	for (int k = i + 1; k < d.nl; k++) d.l[k - 1] = d.l[k];
	d.nl--;
}
// A deep copy.
static void cdoc_copy (CDoc &dst, const CDoc &src)
{
	dst = src;
	dst.text = sdup (src.text);
	dst.l = src.nl ? new CLine[src.nl] : 0; dst.cap = src.nl;
	for (int i = 0; i < src.nl; i++) { dst.l[i] = src.l[i]; dst.l[i].text = sdup (src.l[i].text); }
}

// ---- the book's documents -------------------------------------------------------------------------------------------------
static int cdoc_index (const Book &b, int id)
{
	int lo = 0, hi = b.ncd - 1;
	while (lo <= hi) { int mid = (lo + hi) / 2; if (b.cd[mid].id == id) return mid; if (b.cd[mid].id < id) lo = mid + 1; else hi = mid - 1; }
	return -1;
}
// The next number of a kind in a calendar year.
static int cdoc_next_number (const Book &b, int kind, int year)
{
	int n = 0;
	for (int i = 0; i < b.ncd; i++) if (b.cd[i].kind == kind && y_of (b.cd[i].date) == year && b.cd[i].no > n) n = b.cd[i].no;
	return n + 1;
}
// "2026/0003".
static void cdoc_number (const CDoc &d, char *out, int cap)
{
	out[0] = '\0'; scat_num (out, y_of (d.date), cap); scat (out, "/", cap);
	char t[16]; itoa10 (d.no, t);
	for (int k = slen (t); k < 4; k++) scat (out, "0", cap);
	scat (out, t, cap);
}
// Put in the book (it takes the lines): a new one numbered; a changed one keeps its number (unless its
// kind or year changed) -> its index.
static int cdoc_save (Book &b, CDoc &d)
{
	int old = d.id ? cdoc_index (b, d.id) : -1;
	if (old >= 0)
	{
		const CDoc &o = b.cd[old];
		if (o.kind != d.kind || y_of (o.date) != y_of (d.date)) d.no = cdoc_next_number (b, d.kind, y_of (d.date));
		else d.no = o.no;
		cdoc_free (b.cd[old]);
		b.cd[old] = d;
		cdoc_init (d);
		b.changes++;
		return old;
	}
	if (b.ncd == b.ccd) { int c = b.ccd ? b.ccd * 2 : 64; CDoc *nc = new CDoc[c]; for (int k = 0; k < b.ncd; k++) nc[k] = b.cd[k]; delete [] b.cd; b.cd = nc; b.ccd = c; }
	if (d.id <= 0) { d.id = b.nextCd++; d.no = cdoc_next_number (b, d.kind, y_of (d.date)); }
	else if (d.id >= b.nextCd) b.nextCd = d.id + 1;
	int at = b.ncd; while (at > 0 && b.cd[at - 1].id > d.id) at--;
	for (int k = b.ncd; k > at; k--) b.cd[k] = b.cd[k - 1];
	b.cd[at] = d; b.ncd++;
	cdoc_init (d);
	b.changes++;
	return at;
}
static void cdoc_delete (Book &b, int i)
{
	if (i < 0 || i >= b.ncd) return;
	cdoc_free (b.cd[i]);
	for (int k = i + 1; k < b.ncd; k++) b.cd[k - 1] = b.cd[k];
	b.ncd--;
	b.changes++;
}
// The document that follows one: a quote's order, an order's delivery note -- a copy, new, dated `date`.
static void cdoc_follow (const Book &b, const CDoc &src, int kind, int date, CDoc &d)
{
	cdoc_copy (d, src);
	d.id = 0; d.no = 0; d.kind = kind; d.date = date; d.status = CS_DRAFT; d.invoice = 0; d.from = src.id;
	d.until = 0;
	(void) b;
}
// The invoice a document makes: its lines (quantity x price; "2,5 x Design" when it is not one), each on
// its account -- the line's, else the party's usual one, else the first sales / purchases account.
static void cdoc_to_invoice (const Book &b, const CDoc &d, int journal, int date, Invoice &v)
{
	inv_init (v);
	v.journal = journal; v.party = d.party; v.date = date; v.due = inv_due_of (b, d.party, date);
	scpy (v.ref, d.ref, sizeof v.ref);
	char n[32]; cdoc_number (d, n, sizeof n);
	scpy (v.text, CD_NAME[d.kind], sizeof v.text); scat (v.text, " ", sizeof v.text); scat (v.text, n, sizeof v.text);
	const Party *p = party_of (b, d.party);
	bool sale = cd_sale (d.kind);
	for (int i = 0; i < d.nl; i++)
	{
		const CLine &l = d.l[i];
		InvLine &il = inv_add (v);
		if (l.account[0] && acc_postable (b, l.account)) scpy (il.account, l.account, CODE_MAX);
		else if (p && p->defAcc[0] && acc_postable (b, p->defAcc)) scpy (il.account, p->defAcc, CODE_MAX);
		else scpy (il.account, sale ? (acc_postable (b, "700000") ? "700000" : "") : (acc_postable (b, "604000") ? "604000" : ""), CODE_MAX);
		il.net = cline_total (l);
		il.vat = l.vat >= 0 ? l.vat : vat_default (b, p, sale, il.account);
		if (l.qty != 1000) { char q[40]; qty_show (l.qty, q); scpy (il.text, q, sizeof il.text); scat (il.text, " x ", sizeof il.text); scat (il.text, l.text, sizeof il.text); }
		else scpy (il.text, l.text, sizeof il.text);
	}
}

} // namespace lg

#endif
