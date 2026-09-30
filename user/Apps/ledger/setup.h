//
// setup.h -- a new company's books: the chart of accounts (pcmn.h, in French or in Dutch), the journals
// (sales, purchases, a bank's, the cash's, miscellaneous operations), its first fiscal year; and the
// year's end: the result appropriated (69x / 79x against 14x), the year closed.
//
#ifndef _ledger_setup_h
#define _ledger_setup_h

#include "post.h"
#include "pcmn.h"

namespace lg {

static bool chart_nl (const Book &b) { return b.chart[0] == 'n' || b.chart[0] == 'N'; }
// The chart's default accounts, in the book's language.
static void chart_load (Book &b)
{
	bool nl = chart_nl (b);
	for (int i = 0; i < NPCMN; i++) acc_put (b, PCMN[i].code, nl ? PCMN[i].nl : PCMN[i].fr);
}
// A new company's books: its chart, journals and first fiscal year (from start to end).
static void book_new (Book &b, const char *lang, int start, int end)
{
	book_clear (b);
	scpy (b.chart, lang, 4);
	chart_load (b);
	bool nl = chart_nl (b);
	jrn_add (b, nl ? "VKP" : "VEN", nl ? "Verkopen" : "Ventes", JT_SALES);
	jrn_add (b, nl ? "AKP" : "ACH", nl ? "Aankopen" : "Achats", JT_PURCH);
	jrn_add (b, "BNK", nl ? "Bank" : "Banque", JT_BANK, "550000");
	jrn_add (b, nl ? "KAS" : "CAI", nl ? "Kas" : "Caisse", JT_CASH, "570000");
	jrn_add (b, nl ? "DIV" : "OD", nl ? "Diverse verrichtingen" : "Op\xE9rations diverses", JT_MISC);
	b.yr[0].start = start; b.yr[0].end = end; b.yr[0].closed = false; b.nyr = 1;
	b.changes++;
}
// The fiscal year after the last one (as long as it).
static int year_add_next (Book &b)
{
	if (!b.nyr || b.nyr >= MAXYEARS) return -1;
	const Year &l = b.yr[b.nyr - 1];
	int start = date_add (l.end, 1);
	int months = (y_of (l.end) - y_of (l.start)) * 12 + m_of (l.end) - m_of (l.start) + 1;
	int end = date_add (add_months (start, months), -1);
	Year &y = b.yr[b.nyr++]; y.start = start; y.end = end; y.closed = false;
	b.changes++;
	return b.nyr - 1;
}
// A year's result (income less charges: positive a profit), its appropriation's lines not counted.
static money year_result (const Book &b, int y)
{
	money s = 0;
	for (int i = 0; i < b.ne; i++)
	{
		const Entry &e = b.e[i];
		if (e.date < b.yr[y].start || e.date > b.yr[y].end) continue;
		for (int k = 0; k < e.nl; k++)
		{
			const char *a = e.l[k].account;
			if (!acc_pl (a)) continue;
			if ((a[0] == '6' || a[0] == '7') && a[1] == '9') continue;		// (69 / 79: the appropriation)
			s -= e.l[k].amount;
		}
	}
	return s;
}
// What the years' 69x / 79x lines already appropriate.
static money year_appropriated (const Book &b, int y)
{
	money s = 0;
	for (int i = 0; i < b.ne; i++)
	{
		const Entry &e = b.e[i];
		if (e.date < b.yr[y].start || e.date > b.yr[y].end) continue;
		for (int k = 0; k < e.nl; k++) { const char *a = e.l[k].account; if ((a[0] == '6' || a[0] == '7') && a[1] == '9') s += e.l[k].amount; }
	}
	return s;
}
// The appropriation of what is left of a year's result: a profit to carry forward (693 / 140), a loss
// (141 / 793), on the year's last day in the miscellaneous journal -> the entry's index (-1: nothing).
static int year_appropriate (Book &b, int y)
{
	money left = year_result (b, y) - year_appropriated (b, y);
	int j = jrn_first (b, JT_MISC);
	if (!left || j < 0) return -1;
	Entry e; entry_init (e);
	e.journal = j; e.date = b.yr[y].end;
	sset (e.text, chart_nl (b) ? "Resultaatverwerking" : "Affectation du r\xE9sultat");
	bool nl = chart_nl (b);
	if (left > 0)
	{
		Line &a = entry_add_line (e); scpy (a.account, "693000", CODE_MAX); a.amount = left;
		sset (a.text, nl ? "Over te dragen winst" : "B\xE9n\xE9""fice \xE0 reporter");
		Line &c = entry_add_line (e); scpy (c.account, b.accProfit, CODE_MAX); c.amount = -left;
		sset (c.text, nl ? "Overgedragen winst" : "B\xE9n\xE9""fice report\xE9");
	}
	else
	{
		Line &a = entry_add_line (e); scpy (a.account, b.accLoss, CODE_MAX); a.amount = -left;
		sset (a.text, nl ? "Overgedragen verlies" : "Perte report\xE9""e");
		Line &c = entry_add_line (e); scpy (c.account, "793000", CODE_MAX); c.amount = left;
		sset (c.text, nl ? "Over te dragen verlies" : "Perte \xE0 reporter");
	}
	for (int k = 0; k < e.nl; k++)				// (a chart without them: added)
		if (acc_find (b, e.l[k].account) < 0) acc_put (b, e.l[k].account, e.l[k].text);
	return entry_save (b, e);
}

} // namespace lg

#endif
