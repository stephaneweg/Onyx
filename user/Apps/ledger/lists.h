//
// lists.h -- the pages that list the books:
//   * OverviewPage: the dashboard -- what the customers owe (overdue), what is owed to the suppliers, the
//     bank and cash, the year's result; the sales and purchases month by month; the next VAT return; the
//     invoices most overdue. Without books open: the welcome (a new company, a file, the demo company);
//   * JournalPage: a journal's documents in the year shown (sales, purchases, bank & cash, miscellaneous
//     operations) -- their number, date, party, amounts, their payment's state (paid, due on, n days
//     late); filtered (open, overdue, paid; words searched), sorted by a title's click; a double click
//     or Enter opens one, Delete deletes it, a right click offers the rest (a credit note for an invoice);
//   * PartyPage: the customers or the suppliers -- their balances, what is overdue; the one chosen's
//     card and its lines (a running balance), where they are matched: lines ticked whose amounts add up
//     to zero are matched together (an invoice and its payment), a match undone;
//   * AccountsPage: the chart of accounts as a tree (classes, groups, accounts; the balances at the
//     year's end, a group's its accounts'), the account chosen's register (its lines in the year, the
//     balance brought forward, a running balance).
//
#ifndef _ledger_lists_h
#define _ledger_lists_h

#include "docs.h"

namespace lg {

// ---- what the lists share -------------------------------------------------------------------------------------------------------
// A list of English words (a filter's, a choice's) translated into dst (ctx: TRC's context, 0: none).
static const char *const *tr_list (const char *const *src, int n, const char **dst, const char *ctx = 0)
{
	for (int i = 0; i < n; i++) dst[i] = ctx ? TRC (ctx, src[i]) : TR (src[i]);
	return dst;
}
// The columns' widths made to fill the grid: flex (and flex2) take what is left, shared w1 : w2.
static void fit_columns (DataGrid *g, int flex, int minW = 90, int flex2 = -1, int w1 = 1, int w2 = 1)
{
	int s = 0;
	for (int c = 0; c < g->columns (); c++) if (c != flex && c != flex2) s += g->column (c).width;
	int rest = g->width - s - WK_SBW - 6;
	if (flex2 < 0) g->column (flex).width = imax (minW, rest);
	else
	{
		int a = imax (minW, rest * w1 / (w1 + w2));
		g->column (flex).width = a; g->column (flex2).width = imax (minW, rest - a);
	}
	g->invalidate (true);
}
static DataGrid *new_grid (Widget *page, int x, int y, int w, int h, void *user)
{
	DataGrid *g = new DataGrid (x, y, w, h);
	g->anchor = ANCHOR_FILL; g->user = user; g->sortable = true;
	page->addChild (g);
	return g;
}
// An amount in a cell: right-aligned, red when negative (unless the row is selected).
static void cell_money (Canvas &cv, int x, int y, int w, int h, money v, unsigned ink, bool sel, bool zeroBlank = true, int style = 0)
{
	if (!v && zeroBlank) return;
	char t[32]; fmt_money (v, t);
	cell_text (cv, x, y, w, h, t, sel || v >= 0 ? ink : C_BAD, true, style);
}
// A document's number in a list of its fiscal year: "0012" (with its journal's code: "VE2 0012").
static void short_number (const Entry &e, bool withJournal, char *out, int cap)
{
	out[0] = '\0';
	if (withJournal) { scpy (out, g_b.jr[e.journal].code, cap); scat (out, " ", cap); }
	char t[16]; itoa10 (e.no, t);
	for (int k = slen (t); k < 4; k++) scat (out, "0", cap);
	scat (out, t, cap);
}
// A document's payment state: its party's lines (on the collective accounts) matched or not, when due.
enum { ST_NONE, ST_PAID, ST_OPEN, ST_OVERDUE, ST_CREDIT, ST_SETTLED, ST_PAYING };
static int doc_state (const Entry &e, money *open, int *due)
{
	*open = 0; *due = 0;
	bool any = false, allMatched = true;
	for (int k = 0; k < e.nl; k++)
	{
		const Line &l = e.l[k];
		if (!l.party || !acc_party (g_b, l.account) || !l.amount) continue;
		any = true;
		if (!l.match) { allMatched = false; *open += l.amount; int d = l.due ? l.due : e.date; if (!*due || d < *due) *due = d; }
	}
	if (!any) return ST_NONE;
	bool credit = (e.flags & EF_CREDIT) != 0;
	if (allMatched) return credit ? ST_SETTLED : ST_PAID;
	if (credit) return ST_CREDIT;
	if (e.flags & EF_PAYING) return ST_PAYING;			// (its transfer in a SEPA file)
	return *due < today_ymd () ? ST_OVERDUE : ST_OPEN;
}
static void state_pill (int st, int due, char *text, unsigned *col)
{
	char d[16];
	switch (st)
	{
	case ST_PAID: scpy (text, TR ("Paid"), 32); *col = C_GOOD; return;
	case ST_SETTLED: scpy (text, TR ("Settled"), 32); *col = C_GOOD; return;
	case ST_CREDIT: scpy (text, TR ("Credit open"), 32); *col = C_PURPLE; return;
	case ST_PAYING: scpy (text, TR ("Transfer sent"), 32); *col = C_PURPLE; return;
	case ST_OPEN: date_show (due, d); d[5] = '\0'; scpy (text, TR ("Due "), 32); scat (text, d, 32); *col = C_BLUE; return;
	case ST_OVERDUE: { int n = days_between (due, today_ymd ()); text[0] = '\0'; scat_num (text, n, 32); scat (text, n == 1 ? TR (" day late") : TR (" days late"), 32); *col = C_BAD; return; }
	}
	text[0] = '\0'; *col = 0;
}

// =====================================================================================================================================
// ---- the overview -----------------------------------------------------------------------------------------------------------------
// =====================================================================================================================================
struct LateItem { int e; money amount; int days; };
class OverviewPage : public Page
{
public:
	money recv, recvLate, pay, payLate, cash, result, turnover;
	int ncash, nlate;
	money sales[12], purch[12]; int months, m0y, m0m;
	LateItem late[6]; int nl;
	char vatName[32]; int vatDue; money vatAmount; bool vatFiled, vatLate;
	FlatButton *bNew, *bOpen, *bDemo, *qSale, *qPurch, *qBank;
	int hotLate;
	OverviewPage () : Page (P_OVERVIEW), bNew (0), bOpen (0), bDemo (0), qSale (0), qPurch (0), qBank (0), hotLate (-1)
	{
		resizeTo (800, 660);
		HeadRow h (this);
		qBank = h.add (TR ("Statement"), s_bank, FB_SECONDARY, NI_BANK, TR ("A new bank statement"));
		qPurch = h.add (TR ("Purchase"), s_purch, FB_SECONDARY, NI_PURCH, TR ("A new purchase invoice"));
		qSale = h.add (TR ("Invoice"), s_sale, FB_PRIMARY, NI_PLUS, TR ("A new sales invoice"));
		bNew = new FlatButton (TR ("Create a company..."), s_newCompany, FB_PRIMARY, NI_PLUS); addChild (bNew);
		bOpen = new FlatButton (TR ("Open a company's books..."), s_open, FB_SECONDARY, NI_MISC); addChild (bOpen);
		bDemo = new FlatButton (TR ("Try the demo company"), s_demo, FB_QUIET, NI_OVERVIEW); addChild (bDemo);
		recv = recvLate = pay = payLate = cash = result = turnover = 0; ncash = nlate = 0; months = 0; nl = 0;
		vatName[0] = '\0'; vatDue = 0; vatAmount = 0; vatFiled = vatLate = false;
	}
	const char *title () override { return g_b.nacc ? TR ("Overview") : TR ("Welcome to Ledger"); }
	void subtitle (char *out, int cap) override
	{
		if (!g_b.nacc) { scpy (out, TR ("The accounts of a Belgian company or self-employed person: invoices, VAT, bank, reports."), cap); return; }
		// (the company's name: at the top of the side bar)
		if (g_year >= 0 && g_year < g_b.nyr)
		{
			char a[16], b[16]; date_show (yfrom (), a); date_show (yto (), b);
			scpy (out, TR ("Fiscal year "), cap); scat (out, a, cap); scat (out, " - ", cap); scat (out, b, cap);
			if (g_b.yr[g_year].closed) scat (out, TR (" (closed)"), cap);
		}
		else scpy (out, g_b.name[0] ? g_b.name : TR ("(the company)"), cap);
	}
	void layoutWelcome ()
	{
		if (!bNew || !qBank) return;
		bool none = !g_b.nacc;
		bNew->hidden = bOpen->hidden = bDemo->hidden = !none;
		qSale->hidden = qPurch->hidden = qBank->hidden = none;
		int cx = width / 2, y = 250;
		bNew->left = cx - bNew->width / 2; bNew->top = y;
		bOpen->left = cx - bOpen->width / 2; bOpen->top = y + 44;
		bDemo->left = cx - bDemo->width / 2; bDemo->top = y + 88;
	}
	void resizeTo (int w, int h) override { Page::resizeTo (w, h); layoutWelcome (); }
	void refresh () override
	{
		layoutWelcome ();
		if (!g_b.nacc) { invalidate (true); return; }
		int t = today_ymd (), to = yto (), from = yfrom ();
		int at = t < to ? t : to;
		recv = recvLate = pay = payLate = cash = 0; ncash = 0; nl = 0;
		for (int i = 0; i < g_b.ne; i++)				// what is owed, what is late
		{
			const Entry &e = g_b.e[i];
			if (e.date > at) continue;
			for (int k = 0; k < e.nl; k++)
			{
				const Line &l = e.l[k];
				if (!l.party || !acc_party (g_b, l.account) || l.match) continue;
				int due = l.due ? l.due : e.date;
				bool isLate = due < t;
				if (acc_kind (l.account) == AK_RECV)
				{
					recv += l.amount;
					if (isLate && l.amount > 0)
					{
						recvLate += l.amount;
						LateItem li; li.e = i; li.amount = l.amount; li.days = days_between (due, t);
						int p = nl < 6 ? nl++ : 5;
						if (p == 5 && nl == 6 && late[5].days >= li.days) continue;
						late[p] = li;
						for (int j = p; j > 0 && late[j].days > late[j - 1].days; j--) { LateItem x = late[j]; late[j] = late[j - 1]; late[j - 1] = x; }
					}
				}
				else { pay -= l.amount; if (isLate && l.amount < 0) payLate -= l.amount; }
			}
		}
		for (int i = 0; i < g_b.nacc; i++)				// the bank and cash accounts
		{
			const char *c = g_b.acc[i].code;
			if (acc_heading (c) || (acc_kind (c) != AK_BANK && acc_kind (c) != AK_CASH)) continue;
			money d, cr; acc_totals (g_b, c, 0, at, &d, &cr, true);
			if (d || cr) { cash += d - cr; ncash++; }
		}
		result = income_result (g_b, from, to);
		turnover = -rubric_sum (g_b, "70", from, to);
		// the months
		months = imin (12, (y_of (to) - y_of (from)) * 12 + m_of (to) - m_of (from) + 1);
		m0y = y_of (from); m0m = m_of (from);
		for (int m = 0; m < 12; m++) sales[m] = purch[m] = 0;
		for (int i = 0; i < g_b.ne; i++)
		{
			const Entry &e = g_b.e[i];
			if (e.date < from || e.date > to) continue;
			int m = (y_of (e.date) - m0y) * 12 + m_of (e.date) - m0m;
			if (m < 0 || m >= months) continue;
			for (int k = 0; k < e.nl; k++)
			{
				const Line &l = e.l[k];
				if (l.account[0] == '7' && l.account[1] == '0') sales[m] -= l.amount;
				else if (l.account[0] == '6' && (l.account[1] == '0' || l.account[1] == '1')) purch[m] += l.amount;
			}
		}
		// the VAT return to come: the first period not filed whose end is past (or the one running)
		vatName[0] = '\0'; vatDue = 0; vatAmount = 0; vatFiled = vatLate = false;
		if (g_b.vatRegime == VR_NORMAL)
		{
			bool monthly = g_b.vatPeriod == VP_MONTH;
			int y = y_of (t), n = monthly ? 12 : 4;
			int best = -1, by = 0;
			for (int yy = y - 1; yy <= y && best < 0; yy++)
				for (int p = 1; p <= n; p++)
				{
					int f, e2; period_range (yy, p, monthly, &f, &e2);
					if (return_find (g_b, yy, p, monthly) >= 0) continue;
					bool used = false; for (int i = 0; i < g_b.ne && !used; i++) if (g_b.e[i].date >= f && g_b.e[i].date <= e2) used = true;
					if (!used && e2 < t) continue;
					if (f > t) break;
					best = p; by = yy; break;
				}
			if (best > 0)
			{
				int f, e2; period_range (by, best, monthly, &f, &e2);
				period_name (by, best, monthly, vatName, sizeof vatName);
				vatDue = return_deadline (by, best, monthly);
				money g[100]; vat_grids (g_b, f, e2, g);
				vatAmount = g[71] ? g[71] : -g[72];
				vatLate = vatDue < t;
			}
		}
		invalidate (true);
	}
	void onDraw () override
	{
		drawHead ();
		if (!g_b.nacc)
		{
			int cx = width / 2;
			draw_ni (canvas, NI_REPORT, cx - 10, 120, C_ACCENT, C_ACCENT);
			wk_text_c (canvas, 0, 150, width, 24, TR ("Your books, the Belgian way"), C_TEXT, 2);
			wk_text_c (canvas, 0, 176, width, 20, TR ("The chart of accounts (PCMN), sales and purchases, bank statements, VAT returns for Intervat,"), dim_ink (C_BG));
			wk_text_c (canvas, 0, 196, width, 20, TR ("the customers' and suppliers' accounts, the general ledger, the balance sheet."), dim_ink (C_BG));
			return;
		}
		int W = width, x0 = 16, gap = 12, tw = (W - 2 * x0 - 3 * gap) / 4, ty = 72, th = 84;
		char a[32], s[96];
		// the tiles
		scpy (s, recvLate ? "" : TR ("Nothing overdue"), sizeof s);
		if (recvLate) { money_s (recvLate, a); scpy (s, a, sizeof s); scat (s, TR (" overdue"), sizeof s); }
		draw_tile (canvas, x0, ty, tw, th, TR ("To receive"), money_s (recv, a), s, recvLate ? C_BAD : C_GOOD, C_ACCENT);
		scpy (s, payLate ? "" : TR ("Nothing overdue"), sizeof s);
		if (payLate) { money_s (payLate, a); scpy (s, a, sizeof s); scat (s, TR (" overdue"), sizeof s); }
		draw_tile (canvas, x0 + tw + gap, ty, tw, th, TR ("To pay"), money_s (pay, a), s, payLate ? C_WARN : C_GOOD, C_WARN);
		scpy (s, "", sizeof s); scat_num (s, ncash, sizeof s); scat (s, ncash == 1 ? TR (" account") : TR (" accounts"), sizeof s);
		draw_tile (canvas, x0 + 2 * (tw + gap), ty, tw, th, TR ("Bank and cash"), money_s (cash, a), s, field_dim (), C_BLUE);
		char cap[40]; scpy (cap, TR ("Result "), sizeof cap); if (g_year >= 0) { char yl[16]; year_label (g_b.yr[g_year], yl, sizeof yl); scat (cap, yl, sizeof cap); }
		char tv[32]; scpy (s, TR ("Turnover "), sizeof s); scat (s, money_s (turnover, tv), sizeof s);
		draw_tile (canvas, x0 + 3 * (tw + gap), ty, tw, th, cap, money_s (result, a), s, result >= 0 ? C_GOOD : C_BAD, result >= 0 ? C_GOOD : C_BAD);
		// the months' chart
		int cy = ty + th + 16, ch = imax (120, height - cy - 170), cw = W - 2 * x0;
		wk_rbox (canvas, x0, cy, cw, ch, 10, wk_tone (C_FIELD, 132), wk_tone (C_FIELD, 124));
		wk_rline (canvas, x0, cy, cw, ch, 10, wk_mix (C_BG, 0, 60), 110);
		wk_text_l (canvas, x0 + 16, cy + 8, 20, TR ("Sales and purchases by month (excl. VAT)"), C_FIELD_TEXT, 2);
		int lx = x0 + cw - 190;
		wk_rbox (canvas, lx, cy + 13, 10, 10, 2, C_ACCENT, C_ACCENT); wk_text_l (canvas, lx + 14, cy + 8, 20, TR ("Sales"), field_dim ());
		wk_rbox (canvas, lx + 80, cy + 13, 10, 10, 2, wk_tone (C_BG, 100), wk_tone (C_BG, 100)); wk_text_l (canvas, lx + 94, cy + 8, 20, TR ("Purchases"), field_dim ());
		money mx = 1; for (int m = 0; m < months; m++) { if (sales[m] > mx) mx = sales[m]; if (purch[m] > mx) mx = purch[m]; }
		// a round top for the scale
		money step = 100; while (step * 10 < mx) step *= 10;
		money top = ((mx + step - 1) / step) * step; if (top < 100) top = 100;
		int px = x0 + 70, pw = cw - 70 - 16, py = cy + 36, ph = ch - 36 - 26;
		for (int k = 0; k <= 4; k++)
		{
			int y = py + ph - ph * k / 4;
			canvas.fillRect (px, y, pw, 1, wk_tone (C_FIELD, k ? 116 : 96));
			money v = top * k / 4; fmt_money0 (v - v % 100, a); int n = slen (a); if (n > 3) a[n - 3] = '\0';
			text_r (canvas, px - 8, y - 9, 18, k ? a : "0", field_dim ());
		}
		if (months > 0)
		{
			int slot = pw / months, bw = imax (4, (slot - 10) / 2);
			for (int m = 0; m < months; m++)
			{
				int sx = px + m * slot + (slot - 2 * bw - 2) / 2;
				int hs = (int) (sales[m] > 0 ? sales[m] * ph / top : 0), hp = (int) (purch[m] > 0 ? purch[m] * ph / top : 0);
				if (hs) wk_rbox (canvas, sx, py + ph - hs, bw, hs, imin (3, bw / 2), wk_tone (C_ACCENT, 150), wk_tone (C_ACCENT, 118), 255, WK_TL | WK_TR);
				if (hp) wk_rbox (canvas, sx + bw + 2, py + ph - hp, bw, hp, imin (3, bw / 2), wk_tone (C_BG, 118), wk_tone (C_BG, 96), 255, WK_TL | WK_TR);
				int mm = (m0m - 1 + m) % 12;
				wk_text_c (canvas, px + m * slot, py + ph + 4, slot, 20, TR (MONTH_SHORT[mm]), field_dim ());
			}
		}
		// the VAT return to come, the invoices most overdue
		int by = cy + ch + 14, bh = height - by - 12, bw1 = (cw - gap) / 2;
		wk_rbox (canvas, x0, by, bw1, bh, 10, wk_tone (C_FIELD, 132), wk_tone (C_FIELD, 124));
		wk_rline (canvas, x0, by, bw1, bh, 10, wk_mix (C_BG, 0, 60), 110);
		wk_text_l (canvas, x0 + 16, by + 8, 20, TR ("The next VAT return"), C_FIELD_TEXT, 2);
		if (g_b.vatRegime != VR_NORMAL) wk_text_l (canvas, x0 + 16, by + 36, 20, g_b.vatRegime == VR_FRANCHISE ? TR ("The small business franchise: no VAT return.") : TR ("Not subject to VAT: no VAT return."), field_dim ());
		else if (!vatName[0]) wk_text_l (canvas, x0 + 16, by + 36, 20, TR ("Every period so far is filed."), C_GOOD);
		else
		{
			wk_text_l (canvas, x0 + 16, by + 34, 22, vatName, C_FIELD_TEXT, 2);
			char d[16]; date_show (vatDue, d); scpy (s, vatLate ? TR ("Late: it was due on ") : TR ("Due on "), sizeof s); scat (s, d, sizeof s);
			wk_text_l (canvas, x0 + 16, by + 56, 20, s, vatLate ? C_BAD : field_dim ());
			scpy (s, vatAmount >= 0 ? TR ("To pay (so far)") : TR ("To recover (so far)"), sizeof s);
			wk_text_l (canvas, x0 + 16, by + 80, 20, s, field_dim ());
			text_r (canvas, x0 + bw1 - 16, by + 76, 24, money_s (vatAmount >= 0 ? vatAmount : -vatAmount, a), vatAmount >= 0 ? C_FIELD_TEXT : C_GOOD, 2);
		}
		int x1 = x0 + bw1 + gap;
		wk_rbox (canvas, x1, by, bw1, bh, 10, wk_tone (C_FIELD, 132), wk_tone (C_FIELD, 124));
		wk_rline (canvas, x1, by, bw1, bh, 10, wk_mix (C_BG, 0, 60), 110);
		wk_text_l (canvas, x1 + 16, by + 8, 20, TR ("Invoices overdue"), C_FIELD_TEXT, 2);
		if (!nl) wk_text_l (canvas, x1 + 16, by + 36, 20, TR ("None: every customer pays on time."), C_GOOD);
		int rows = imin (nl, (bh - 36) / 22);
		for (int i = 0; i < rows; i++)
		{
			const Entry &e = g_b.e[late[i].e];
			int y = by + 32 + i * 22;
			if (i == hotLate) wk_hilite (canvas, x1 + 8, y, bw1 - 16, 22, 5, false);
			unsigned ink = i == hotLate ? wk_hilite_ink (false) : C_FIELD_TEXT;
			text_fit_l (canvas, x1 + 16, y, bw1 - 230, 22, party_name (g_b, e.party), ink);
			char dl[24] = ""; scat_num (dl, late[i].days, sizeof dl); scat (dl, TR (" d"), sizeof dl);
			text_r (canvas, x1 + bw1 - 120, y, 22, dl, i == hotLate ? ink : C_BAD);
			text_r (canvas, x1 + bw1 - 16, y, 22, money_s (late[i].amount, a), ink);
		}
	}
	int lateAt (int mx, int my)
	{
		int W = width, x0 = 16, gap = 12, cw = W - 2 * x0, bw1 = (cw - gap) / 2, x1 = x0 + bw1 + gap;
		int ty = 72, th = 84, cy = ty + th + 16, ch = imax (120, height - cy - 170), by = cy + ch + 14;
		if (mx < x1 || mx >= x1 + bw1 || my < by + 32) return -1;
		int i = (my - by - 32) / 22;
		return i >= 0 && i < nl ? i : -1;
	}
	bool onMouse (int mx, int my, int bl, int, int, int wheel) override
	{
		if (wheel || !g_b.nacc) return false;
		int h = lateAt (mx, my);
		if (h != hotLate) { hotLate = h; invalidate (true); }
		if (bl && !pressed) { pressed = true; if (h >= 0) open_entry (g_b.e[late[h].e].id); }
		else if (!bl) pressed = false;
		return true;
	}
	void cmdNew () override { s_sale (); }
	static void s_sale () { int j = jrn_first (g_b, JT_SALES); if (j >= 0) new_document (j); }
	static void s_purch () { int j = jrn_first (g_b, JT_PURCH); if (j >= 0) new_document (j); }
	static void s_bank () { int j = jrn_first (g_b, JT_BANK); if (j < 0) j = jrn_first (g_b, JT_CASH); if (j >= 0) new_document (j); }
	static void s_newCompany ();
	static void s_open ();
	static void s_demo ();
};

// =====================================================================================================================================
// ---- a journal's documents --------------------------------------------------------------------------------------------------------
// =====================================================================================================================================
struct JRow { int e; money a, b, c; int due, lines; unsigned char st; };
class JournalPage;
static JournalPage *g_jp[4];

class JournalPage : public Page
{
public:
	int t1, t2;					// its journals' types
	DataGrid *g; SearchBox *search; Segmented *filter; ChoiceBox *jbox; JournalChoice jc;
	JRow *rows; int nrows, cap, keepId;
	money tA, tB, tC; int nOpen, nLate;
	FlatButton *bNew, *bCredit;
	JournalPage (int id_) : Page (id_), g (0), search (0), filter (0), jbox (0), rows (0), nrows (0), cap (0), keepId (0), tA (0), tB (0), tC (0), nOpen (0), nLate (0), bNew (0), bCredit (0)
	{
		resizeTo (800, 660);
		t1 = id == P_SALES ? JT_SALES : id == P_PURCH ? JT_PURCH : id == P_FIN ? JT_BANK : JT_MISC;
		t2 = id == P_FIN ? JT_CASH : -1;
		HeadRow h (this);
		if (id == P_SALES || id == P_PURCH)
		{
			bNew = h.add (id == P_SALES ? TR ("New invoice") : TR ("New purchase"), s_new, FB_PRIMARY, NI_PLUS, TR ("A new invoice (Ctrl+N)"));
			bCredit = h.add (TR ("Credit note"), s_credit, FB_SECONDARY, NI_PLUS, TR ("A new credit note"));
			if (id == P_PURCH) h.add (TR ("Pay..."), s_pay, FB_SECONDARY, NI_BANK, TR ("The suppliers' invoices paid: a SEPA transfers file for the bank"));
		}
		else bNew = h.add (id == P_FIN ? TR ("New statement") : TR ("New operation"), s_new, FB_PRIMARY, NI_PLUS, id == P_FIN ? TR ("A new bank or cash statement (Ctrl+N)") : TR ("A new miscellaneous operation (Ctrl+N)"));
		if (id == P_FIN) h.add (TR ("Import CODA"), s_coda, FB_SECONDARY, NI_IMPORT, TR ("The bank's statements file (CODA): its movements, their parties and invoices found"));
		int y = 70;
		if (id == P_SALES || id == P_PURCH)
		{
			static const char *const F[4] = { "All", "Open", "Overdue", "Paid" };
			const char *tf[4]; filter = new Segmented (16, y, tr_list (F, 4, tf, "docs"), 4, on_filter); addChild (filter);
		}
		else filter = 0;
		jbox = new ChoiceBox (filter ? 16 + filter->width + 12 : 16, y, 220); jbox->onChange = on_journal; addChild (jbox);
		search = new SearchBox (220); search->left = width - 16 - 220; search->top = y; search->anchor = ANCHOR_RIGHT | ANCHOR_TOP;
		search->placeholder = TR ("Search"); search->tip = TR ("Words of a party, a description, a number, an amount (Ctrl+F)");
		search->onChange = on_search; search->onEnter = on_search_done; addChild (search);
		g = new_grid (this, 16, 108, width - 32, height - 108 - 36, this);
		if (id == P_SALES || id == P_PURCH)
		{
			g->setColumns (7);
			g->setColumn (0, TR ("No."), 78); g->setColumn (1, TR ("Date"), 98); g->setColumn (2, id == P_SALES ? TR ("Customer") : TR ("Supplier"), 170);
			g->setColumn (3, id == P_SALES ? TR ("Description") : TR ("Their number"), 130); g->setColumn (4, TR ("Excl. VAT"), 0, GRID_RIGHT);
			g->setColumn (5, TR ("Total"), 110, GRID_RIGHT); g->setColumn (6, TR ("Status"), 120);
			g->column (4).width = 0;
		}
		else if (id == P_FIN)
		{
			g->setColumns (7);
			g->setColumn (0, TR ("No."), 78); g->setColumn (1, TR ("Date"), 98); g->setColumn (2, TR ("Description"), 170); g->setColumn (3, TR ("Journal"), 76);
			g->setColumn (4, TR ("In"), 110, GRID_RIGHT); g->setColumn (5, TR ("Out"), 110, GRID_RIGHT); g->setColumn (6, TR ("New balance"), 120, GRID_RIGHT);
		}
		else
		{
			g->setColumns (6);
			g->setColumn (0, TR ("No."), 78); g->setColumn (1, TR ("Date"), 98); g->setColumn (2, TR ("Description"), 200); g->setColumn (3, TR ("Lines"), 60, GRID_RIGHT);
			g->setColumn (4, TR ("Amount"), 120, GRID_RIGHT); g->setColumn (5, TR ("Kind"), 140);
		}
		g->sortCol = 0; g->sortDesc = true;
		g->cellText = cell_text_cb; g->cellDraw = cell_draw_cb; g->onActivate = on_open; g->onSort = on_sort; g->onContext = on_context;
		g->emptyText = TR ("Nothing yet: the button at the top right makes the first one.");
		fit ();
	}
	// The columns fitted: the party and the description share the room (with room to spare: the amount
	// excluding VAT shown too).
	void fit ()
	{
		if (id == P_SALES || id == P_PURCH)
		{
			g->column (4).width = g->width > 900 ? 104 : 0;
			fit_columns (g, 2, 90, 3, 5, 4);
		}
		else fit_columns (g, 2);
	}
	~JournalPage () { delete [] rows; }
	const char *title () override { return id == P_SALES ? TR ("Sales") : id == P_PURCH ? TR ("Purchases") : id == P_FIN ? TR ("Bank and cash") : TR ("Miscellaneous operations"); }
	void subtitle (char *out, int cap_) override
	{
		out[0] = '\0'; scat_num (out, nrows, cap_); scat (out, nrows == 1 ? TR (" document") : TR (" documents"), cap_);
		char a[32];
		if (id == P_SALES || id == P_PURCH)		// (their totals: at the foot)
		{
			if (nOpen) { scat (out, "  \xB7  ", cap_); scat_num (out, nOpen, cap_); scat (out, nOpen == 1 ? TRC ("one", " open") : TR (" open"), cap_); }
			if (nLate) { scat (out, ", ", cap_); scat_num (out, nLate, cap_); scat (out, TR (" overdue"), cap_); }
		}
		else if (id == P_FIN) { scat (out, "  \xB7  ", cap_); scat (out, TR ("in "), cap_); scat (out, money_s (tA, a), cap_); scat (out, TR (", out "), cap_); scat (out, money_s (tB, a), cap_); }
	}
	void resizeTo (int w, int h) override { Page::resizeTo (w, h); if (g) fit (); }
	int journalFilter () { return jc.n > 1 && jbox->sel > 0 ? jc.idx[jbox->sel - 1] : -1; }
	void refresh () override
	{
		// the journals' choice: "All the journals" then each
		jc.fill (t1, t2);
		static const char *names[4][MAXJOURNALS + 1];
		int k = id - P_SALES; if (k < 0 || k > 3) k = 0;
		names[k][0] = TR ("All the journals"); for (int i = 0; i < jc.n; i++) names[k][i + 1] = jc.names[i];
		int keep = jbox->sel;
		jbox->setOptions (names[k], jc.n + 1); jbox->sel = iclamp (keep, 0, jc.n); jbox->hidden = jc.n < 2;
		search->left = width - 16 - search->width;
		int keepSel = g->sel >= 0 && g->sel < nrows ? g_b.e[rows[g->sel].e].id : keepId;
		nrows = 0; tA = tB = tC = 0; nOpen = nLate = 0;
		int jf = journalFilter (), from = yfrom (), to = yto ();
		const char *words = search->text ();
		for (int i = 0; i < g_b.ne; i++)
		{
			const Entry &e = g_b.e[i];
			if (e.date < from || e.date > to) continue;
			int jt = g_b.jr[e.journal].type;
			if (jt != t1 && jt != t2) continue;
			if (jf >= 0 && e.journal != jf) continue;
			JRow r; r.e = i; r.a = r.b = r.c = 0; r.due = 0; r.lines = e.nl; r.st = ST_NONE;
			if (id == P_SALES || id == P_PURCH)
			{
				bool sale = id == P_SALES;
				for (int x = 0; x < e.nl; x++)
				{
					const Line &l = e.l[x];
					if (l.role == LR_BASE || l.role == LR_ND) r.a += sale ? -l.amount : l.amount;
					else if (l.party && acc_party (g_b, l.account)) r.c += sale ? l.amount : -l.amount;
				}
				money open; r.st = (unsigned char) doc_state (e, &open, &r.due);
				if (r.st == ST_OPEN || r.st == ST_OVERDUE || r.st == ST_CREDIT || r.st == ST_PAYING) nOpen++;
				if (r.st == ST_OVERDUE) nLate++;
				int f = filter ? filter->cur : 0;
				if (f == 1 && !(r.st == ST_OPEN || r.st == ST_OVERDUE || r.st == ST_CREDIT || r.st == ST_PAYING)) continue;
				if (f == 2 && r.st != ST_OVERDUE) continue;
				if (f == 3 && !(r.st == ST_PAID || r.st == ST_SETTLED)) continue;
			}
			else if (id == P_FIN)
			{
				const char *acc = g_b.jr[e.journal].account;
				for (int x = 0; x < e.nl; x++) if (seq (e.l[x].account, acc)) { if (e.l[x].amount > 0) r.a += e.l[x].amount; else r.b -= e.l[x].amount; }
				r.c = e.stmtNew; r.lines = e.nl / 2;
			}
			else { for (int x = 0; x < e.nl; x++) if (e.l[x].amount > 0) r.a += e.l[x].amount; }
			if (words[0])
			{
				char hay[400], n[32];
				entry_number (g_b, e, n, sizeof n);
				scpy (hay, n, sizeof hay); scat (hay, " ", sizeof hay); scat (hay, party_name (g_b, e.party), sizeof hay); scat (hay, " ", sizeof hay);
				scat (hay, e.text, sizeof hay); scat (hay, " ", sizeof hay); scat (hay, e.ref, sizeof hay);
				char a[32]; fmt_money (r.c ? r.c : r.a, a); scat (hay, " ", sizeof hay); scat (hay, a, sizeof hay);
				fmt_money (r.c ? r.c : r.a, a, false); scat (hay, " ", sizeof hay); scat (hay, a, sizeof hay);
				for (int x = 0; x < e.nl; x++) if (e.l[x].party) { scat (hay, " ", sizeof hay); scat (hay, party_name (g_b, e.l[x].party), sizeof hay); break; }
				if (!words_in (hay, words)) continue;
			}
			if (nrows == cap) { int c = cap ? cap * 2 : 256; JRow *nr = new JRow[c]; for (int q = 0; q < nrows; q++) nr[q] = rows[q]; delete [] rows; rows = nr; cap = c; }
			rows[nrows++] = r;
			tA += r.a; tB += r.b; tC += r.c;
		}
		sortRows ();
		g->setRows (nrows);
		int sel = -1; for (int i = 0; i < nrows; i++) if (g_b.e[rows[i].e].id == keepSel) sel = i;
		g->setSel (sel >= 0 ? sel : nrows ? 0 : -1);
		invalidate (true);
	}
	static int cmp (void *ctx, int x, int y)
	{
		JournalPage *p = (JournalPage *) ctx;
		const JRow &a = p->rows[x], &b = p->rows[y];
		const Entry &ea = g_b.e[a.e], &eb = g_b.e[b.e];
		int r = 0;
		switch (p->g->sortCol)
		{
		case 0: r = ea.journal != eb.journal ? ea.journal - eb.journal : y_of (ea.date) != y_of (eb.date) ? ea.date - eb.date : ea.no - eb.no; break;
		case 1: r = ea.date - eb.date; break;
		case 2: r = p->id == P_SALES || p->id == P_PURCH ? ci_cmp (party_name (g_b, ea.party), party_name (g_b, eb.party)) : ci_cmp (ea.text, eb.text); break;
		case 3: r = p->id == P_SALES ? ci_cmp (ea.text, eb.text) : p->id == P_PURCH ? ci_cmp (ea.ref, eb.ref) : p->id == P_FIN ? ea.journal - eb.journal : a.lines - b.lines; break;
		case 4: r = a.a < b.a ? -1 : a.a > b.a; break;
		case 5: r = p->id == P_FIN ? (a.b < b.b ? -1 : a.b > b.b) : p->id == P_MISC ? ((int) (ea.flags & 3) - (int) (eb.flags & 3)) : (a.c < b.c ? -1 : a.c > b.c); break;
		case 6: r = p->id == P_FIN ? (a.c < b.c ? -1 : a.c > b.c) : a.st != b.st ? a.st - b.st : a.due - b.due; break;
		}
		if (!r) r = ea.date != eb.date ? ea.date - eb.date : ea.journal != eb.journal ? ea.journal - eb.journal : ea.no - eb.no;
		return p->g->sortDesc ? -r : r;
	}
	void sortRows ()
	{
		int *ix = new int[nrows + 1];
		for (int i = 0; i < nrows; i++) ix[i] = i;
		idx_sort (ix, nrows, cmp, this);
		JRow *t = new JRow[nrows + 1];
		for (int i = 0; i < nrows; i++) t[i] = rows[ix[i]];
		for (int i = 0; i < nrows; i++) rows[i] = t[i];
		delete [] t; delete [] ix;
	}
	static const char *cell_text_cb (DataGrid &gr, int row, int col, char *buf, int cap_)
	{
		JournalPage *p = (JournalPage *) gr.user;
		if (row >= p->nrows) return "";
		const JRow &r = p->rows[row]; const Entry &e = g_b.e[r.e];
		switch (col)
		{
		case 0: short_number (e, p->jc.n > 1 && p->id != P_FIN, buf, cap_); return buf;
		case 1: date_show (e.date, buf); return buf;
		case 2: if (p->id == P_SALES || p->id == P_PURCH) return party_name (g_b, e.party); return e.text;
		case 3:
			if (p->id == P_SALES)			// (no description: its first line's)
			{
				if (e.text[0]) return e.text;
				for (int k = 0; k < e.nl; k++) if (e.l[k].role == LR_BASE && e.l[k].text[0]) return e.l[k].text;
				return "";
			}
			if (p->id == P_PURCH) return e.ref;
			if (p->id == P_FIN) return g_b.jr[e.journal].code;
			buf[0] = '\0'; scat_num (buf, e.nl, cap_); return buf;
		case 5: if (p->id == P_MISC) return e.flags & EF_OPENING ? TR ("Opening") : e.flags & EF_SETTLE ? TR ("VAT settlement") : entry_has_vat (e) ? TR ("With VAT") : ""; break;
		}
		return "";
	}
	static bool cell_draw_cb (DataGrid &gr, Canvas &cv, int row, int col, int x, int y, int w, int h, unsigned ink, bool sel)
	{
		JournalPage *p = (JournalPage *) gr.user;
		if (row >= p->nrows) return false;
		const JRow &r = p->rows[row]; const Entry &e = g_b.e[r.e];
		bool doc = p->id == P_SALES || p->id == P_PURCH;
		if (col == 0 && (e.flags & EF_CREDIT))			// a credit note: its number and a mark
		{
			char n[40]; short_number (e, p->jc.n > 1 && p->id != P_FIN, n, sizeof n);
			wk_text_l (cv, x + 6, y, h, n, ink);
			draw_pill (cv, x + w - 30, y + (h - 16) / 2, 16, TR ("CN"), C_PURPLE, sel);
			return true;
		}
		if (doc && (col == 4 || col == 5)) { cell_money (cv, x, y, w, h, col == 4 ? r.a : r.c, ink, sel, false, col == 5 ? 2 : 0); return true; }
		if (doc && col == 6)
		{
			char t[32]; unsigned c; state_pill (r.st, r.due, t, &c);
			if (t[0]) draw_pill (cv, x + 6, y + (h - 18) / 2, 18, t, c, sel);
			return true;
		}
		if (p->id == P_FIN && col >= 4) { cell_money (cv, x, y, w, h, col == 4 ? r.a : col == 5 ? r.b : r.c, col == 5 && !sel ? C_BAD : ink, sel, col != 6); return true; }
		if (p->id == P_MISC && col == 4) { cell_money (cv, x, y, w, h, r.a, ink, sel); return true; }
		return false;
	}
	void openSel () { if (g->sel >= 0 && g->sel < nrows) { keepId = g_b.e[rows[g->sel].e].id; open_entry (keepId); } }
	void cmdNew () override
	{
		int j = journalFilter (); if (j < 0) j = jc.n ? jc.idx[0] : jrn_first (g_b, t1);
		if (j >= 0) new_document (j, false);
	}
	void cmdFind () override { search->setFocus (); search->selectAll (); }
	void cmdDelete () override
	{
		if (g->sel < 0 || g->sel >= nrows) return;
		int i = rows[g->sel].e;
		const char *w = entry_can_delete (g_b, i);
		if (w[0]) { warn (TR ("Delete"), w); return; }
		char r[40], m[160]; entry_ref (g_b, g_b.e[i], r, sizeof r);
		scpy (m, TR ("Delete "), sizeof m); scat (m, r, sizeof m); scat (m, TR (" for good? Its matchings are undone."), sizeof m);
		if (ask (TR ("Delete"), m, MB_YESNO, 2) != 1) return;
		entry_delete (g_b, i);
		changed ();
		status (TR ("Document deleted"));
	}
	void creditNote ()
	{
		if (g->sel < 0 || g->sel >= nrows) return;
		const Entry &e = g_b.e[rows[g->sel].e];
		if (e.flags & EF_CREDIT) { status (TR ("That is a credit note already")); return; }
		new_document (e.journal, true);
		if (g_inv) g_inv->creditFor (e);
	}
	void enter () override { g->setFocus (); }
	bool onKey (long k) override
	{
		if (k == KEY_DEL) { cmdDelete (); return true; }
		if (k == KEY_ENTER) { openSel (); return true; }
		return false;
	}
	void onDraw () override
	{
		drawHead ();
		// the foot: the totals of the documents shown
		char t[200], a[32];
		int y = height - 32;
		if (id == P_SALES || id == P_PURCH)
		{
			scpy (t, TR ("Excl. VAT "), sizeof t); scat (t, money_s (tA, a), sizeof t);
			scat (t, TR ("     VAT "), sizeof t); scat (t, money_s (tC - tA, a), sizeof t);
			scat (t, TR ("     Total "), sizeof t); scat (t, money_s (tC, a), sizeof t);
		}
		else if (id == P_FIN) { scpy (t, TR ("In "), sizeof t); scat (t, money_s (tA, a), sizeof t); scat (t, TR ("     Out "), sizeof t); scat (t, money_s (tB, a), sizeof t); }
		else { scpy (t, TR ("Amounts "), sizeof t); scat (t, money_s (tA, a), sizeof t); }
		text_r (canvas, width - 20, y, 28, t, dim_ink (C_BG));
		char n[48] = ""; scat_num (n, nrows, sizeof n); scat (n, nrows == 1 ? TR (" document shown") : TR (" documents shown"), sizeof n);
		wk_text_l (canvas, 20, y, 28, n, dim_ink (C_BG));
	}
	static JournalPage *of (Widget &w) { return (JournalPage *) (w.parent); }
	static void on_filter (int) { for (int i = 0; i < 4; i++) if (g_jp[i] && !g_jp[i]->hidden) g_jp[i]->refresh (); }
	static void on_journal (Widget &w) { of (w)->refresh (); }
	static void on_search (Widget &w) { of (w)->refresh (); }
	static void on_search_done (Widget &w) { of (w)->g->setFocus (); }
	static void on_open (Widget &w) { ((JournalPage *) ((DataGrid &) w).user)->openSel (); }
	static void on_sort (Widget &w)
	{
		DataGrid &d = (DataGrid &) w; JournalPage *p = (JournalPage *) d.user;
		if (d.sortCol == d.clickedCol) d.sortDesc = !d.sortDesc; else { d.sortCol = d.clickedCol; d.sortDesc = d.clickedCol >= 4; }
		p->refresh ();
	}
	static void on_context (Widget &w)
	{
		DataGrid &d = (DataGrid &) w; JournalPage *p = (JournalPage *) d.user;
		if (d.ctxRow < 0) return;
		int x, y; abs_pos (&d, &x, &y);
		PopupMenu m (x + d.ctxX, y + d.ctxY);
		m.add (TRC ("verb", "Open"), 1);
		if (p->id == P_SALES) m.add (TR ("Print"), 4);
		if (p->id == P_SALES || p->id == P_PURCH) m.add (TR ("Make a credit note for it"), 2, !(g_b.e[p->rows[d.ctxRow].e].flags & EF_CREDIT));
		m.separator ();
		m.add (TR ("Delete..."), 3);
		int ei = p->rows[d.ctxRow].e;
		switch (m.run ())
		{
		case 1: p->openSel (); break;
		case 2: p->creditNote (); break;
		case 3: p->cmdDelete (); break;
		case 4: if (ei >= 0 && ei < g_b.ne) print_invoice (g_b.e[ei]); break;
		}
	}
	static JournalPage *cur ();
	static void s_new () { JournalPage *p = cur (); if (p) p->cmdNew (); }
	static void s_coda () { cmd_import_coda (); }
	static void s_pay () { cmd_pay (); }
	static void s_credit () { JournalPage *p = cur (); if (!p) return; int j = p->journalFilter (); if (j < 0) j = p->jc.n ? p->jc.idx[0] : jrn_first (g_b, p->t1); if (j >= 0) new_document (j, true); }
};
inline JournalPage *JournalPage::cur () { for (int i = 0; i < 4; i++) if (g_jp[i] && !g_jp[i]->hidden) return g_jp[i]; return 0; }

// =====================================================================================================================================
// ---- the party's card -----------------------------------------------------------------------------------------------------------------
// =====================================================================================================================================
class PartyDialog : public FormBox
{
public:
	Party p;
	LineEdit *name, *code, *vat, *street, *zip, *city, *country, *email, *phone, *iban, *bic, *terms, *notes;
	ChoiceBox *regime, *lang; PickEdit *acc, *vatc;
	bool isNew;
	char vatMsg[64]; unsigned vatCol; char ibanMsg[64]; unsigned ibanCol;
	PartyDialog (const Party &src, bool fresh) : FormBox (fresh ? (src.kind == PK_CUSTOMER ? TR ("New customer") : TR ("New supplier")) : (src.kind == PK_CUSTOMER ? TR ("Customer") : TR ("Supplier")), 660, 0, 120), p (src), isNew (fresh)
	{
		vatMsg[0] = ibanMsg[0] = '\0'; vatCol = ibanCol = 0;
		int y = y0, x2 = 400;
		name = edit (y, TR ("Name"), 500, p.name); name->onChange = on_name; y += 34;
		code = edit (y, TR ("Code"), 140, p.code); code->placeholder = TR ("Made from the name");
		label (y, TR ("Payment terms"), x2 - 110 + 60); terms = new LineEdit (x2 + 110, y, 60); char t[16]; itoa10 (p.terms, t); terms->setText (t); terms->accept = accept_int; terms->rightAlign = true; addChild (terms);
		label (y, TR ("days"), x2 + 178, true); y += 34;
		{ char vs[24]; vat_show (p.vat, vs, sizeof vs); vat = edit (y, TR ("VAT number"), 180, vs); } vat->placeholder = "BE 0123.456.789"; vat->onChange = on_vat; y += 34;
		label (y, TR ("VAT situation")); regime = new ChoiceBox (fieldX, y, 300); { const char *t2[PR_COUNT]; regime->setOptions (tr_list (REGIME_NAME, PR_COUNT, t2), PR_COUNT); } regime->sel = p.regime < PR_COUNT ? p.regime : 0; addChild (regime);
		label (y, TR ("Language"), 454); lang = new ChoiceBox (530, y, 110); { const char *t3[3]; lang->setOptions (tr_list (LANG_NAME, 3, t3), 3); } lang->sel = lang_index (p.lang[0] ? p.lang : g_b.chart);
		lang->tip = TR ("The language of its quotes, orders, invoices (their templates)"); addChild (lang); y += 42;
		street = edit (y, TR ("Street"), 500, p.street); y += 34;
		zip = edit (y, TR ("Postcode"), 80, p.zip);
		label (y, TRC ("short", "City"), 226); city = new LineEdit (270, y, 230); city->setText (p.city); addChild (city);
		label (y, TR ("Country"), 514); country = new LineEdit (580, y, 40); country->setText (p.country); addChild (country); y += 42;
		email = edit (y, TR ("E-mail"), 290, p.email);
		label (y, TRC ("short", "Phone"), 470); phone = new LineEdit (524, y, 116); phone->setText (p.phone); addChild (phone); y += 34;
		iban = edit (y, "IBAN", 260, ""); { char ib[48]; iban_show (p.iban, ib, sizeof ib); iban->setText (ib); } iban->onChange = on_iban;
		label (y, "BIC", 470); bic = new LineEdit (524, y, 116); bic->setText (p.bic); addChild (bic); y += 42;
		label (y, TR ("Account")); acc = new PickEdit (fieldX, y, 330, SK_ACCOUNT, AF_NOPARTY, p.kind == PK_CUSTOMER ? "70" : "6"); acc->setValue (p.defAcc);
		acc->placeholder = p.kind == PK_CUSTOMER ? TR ("The sales' account by default") : TR ("The purchases' account by default"); addChild (acc); y += 34;
		label (y, TR ("VAT code")); vatc = new PickEdit (fieldX, y, 180, SK_VAT, p.kind == PK_CUSTOMER ? VS_SALES : VS_PURCH); vatc->setValue (p.defVat);
		vatc->placeholder = TR ("By its situation"); addChild (vatc); y += 34;
		notes = edit (y, TR ("Notes"), 500, p.notes); y += 44;
		resizeTo (width, y + 50);
		Root *r = Root::current ();
		if (r) { left = (r->width - width) / 2; top = imax (0, (r->height - height) / 2); }
		buttons (fresh ? TR ("Add") : "OK");
		checkVat (); checkIban ();
		name->setFocus ();
	}
	void checkVat ()
	{
		char v[24]; vat_normalize (vat->text (), v, sizeof v);
		vatMsg[0] = '\0';
		if (!v[0]) return;
		int c = vat_check (v);
		if (c == 0) { scpy (vatMsg, v[0] == 'B' && v[1] == 'E' ? TR ("Valid Belgian number") : eu_prefix (v) ? TR ("EU number (its form)") : TR ("Foreign number"), sizeof vatMsg); vatCol = C_GOOD; }
		else if (c == 2) { scpy (vatMsg, TR ("Its check digits are wrong"), sizeof vatMsg); vatCol = C_BAD; }
		else { scpy (vatMsg, TR ("Not a VAT number's form"), sizeof vatMsg); vatCol = C_BAD; }
		invalidate (true);
	}
	void checkIban ()
	{
		char v[40]; iban_normalize (iban->text (), v, sizeof v);
		ibanMsg[0] = '\0';
		if (!v[0]) return;
		if (iban_ok (v)) { scpy (ibanMsg, "Valid", sizeof ibanMsg); ibanCol = C_GOOD; } else { scpy (ibanMsg, "Not a valid IBAN", sizeof ibanMsg); ibanCol = C_BAD; }
		invalidate (true);
	}
	void drawMore () override
	{
		if (vatMsg[0]) wk_text_l (canvas, fieldX + 190, vat->top, ED_H, vatMsg, vatCol);
		if (ibanMsg[0]) wk_text_l (canvas, fieldX + 266, iban->top, ED_H, ibanMsg[0] == 'V' ? TR ("Valid") : TR ("Invalid"), ibanCol);
	}
	bool validate () override
	{
		char t[NAME_MAX]; trim_copy (t, name->text (), sizeof t);
		if (!t[0]) { warn (TR ("Party"), TR ("Type the name.")); name->setFocus (); return false; }
		scpy (p.name, t, NAME_MAX);
		char c[PCODE_MAX]; trim_copy (c, code->text (), sizeof c);
		for (char *q = c; *q; q++) *q = up (*q);
		if (!c[0]) { p.code[0] = '\0'; party_make_code (g_b, p.kind, p.name, c); }
		int other = party_by_code (g_b, p.kind, c);
		if (other >= 0 && g_b.pty[other].id != p.id) { warn (TR ("Party"), TR ("Another party has this code already.")); code->setFocus (); return false; }
		scpy (p.code, c, PCODE_MAX);
		char v[24]; vat_normalize (vat->text (), v, sizeof v);
		if (v[0] && vat_check (v) && ask (TR ("Party"), TR ("The VAT number is not a valid one. Keep it anyway?"), MB_YESNO, 2) != 1) { vat->setFocus (); return false; }
		scpy (p.vat, v, sizeof p.vat);
		char ib[40]; iban_normalize (iban->text (), ib, sizeof ib);
		if (ib[0] && !iban_ok (ib) && ask (TR ("Party"), TR ("The IBAN is not a valid one. Keep it anyway?"), MB_YESNO, 2) != 1) { iban->setFocus (); return false; }
		scpy (p.iban, ib, sizeof p.iban);
		char bc[16]; iban_normalize (bic->text (), bc, sizeof bc); scpy (p.bic, bc, sizeof p.bic);
		p.regime = (unsigned char) iclamp (regime->sel, 0, PR_COUNT - 1);
		scpy (p.street, street->text (), NAME_MAX); scpy (p.zip, zip->text (), sizeof p.zip); scpy (p.city, city->text (), sizeof p.city);
		char cc[4]; trim_copy (cc, country->text (), sizeof cc); for (char *q = cc; *q; q++) *q = up (*q); scpy (p.country, cc[0] ? cc : "BE", sizeof p.country);
		scpy (p.email, email->text (), sizeof p.email); scpy (p.phone, phone->text (), sizeof p.phone);
		p.terms = (short) iclamp (cell_int (terms->text ()), 0, 3650);
		if (!acc->resolve () && acc->text ()[0]) { warn (TR ("Party"), TR ("The account is not one of the chart: choose it from the list.")); acc->setFocus (); return false; }
		scpy (p.defAcc, acc->value (), CODE_MAX);
		if (!vatc->resolve () && vatc->text ()[0]) { warn (TR ("Party"), TR ("Choose the VAT code from the list.")); vatc->setFocus (); return false; }
		scpy (p.defVat, seq (vatc->value (), "-") ? "" : vatc->value (), sizeof p.defVat);
		sset (p.notes, notes->text ());
		scpy (p.lang, LANG_KEY[iclamp (lang->sel, 0, 2)], sizeof p.lang);
		return true;
	}
	static PartyDialog *me (Widget &w) { Widget *q = w.parent; while (q && !q->modal) q = q->parent; return (PartyDialog *) q; }
	static void on_vat (Widget &w)
	{
		PartyDialog *d = me (w);
		d->checkVat ();
		char v[24]; vat_normalize (d->vat->text (), v, sizeof v);
		char cc[4]; trim_copy (cc, d->country->text (), sizeof cc);
		if (v[0] && !vat_check (v))			// its situation and its country follow the number
		{
			int r = regime_of (v, cc);
			if (d->regime->sel != PR_COCONTRACT || r != PR_BE) { d->regime->sel = r; d->regime->invalidate (true); }
			char pre[3] = { v[0], v[1], 0 }; if (pre[0] == 'E' && pre[1] == 'L') { pre[0] = 'G'; pre[1] = 'R'; }
			d->country->setText (pre);
		}
		else if (!v[0] && d->regime->sel == PR_BE) { d->regime->sel = PR_PRIVATE; d->regime->invalidate (true); }
	}
	static void on_iban (Widget &w) { me (w)->checkIban (); }
	static void on_name (Widget &) {}
};
// The card of a party (new: id 0) -> its id (0: cancelled). Its changes saved.
static int edit_party (int id, int kind, const char *name = "")
{
	Party src; party_init (src);
	bool fresh = true;
	if (id) { const Party *p = party_of (g_b, id); if (p) { src = *p; fresh = false; } }
	else { src.kind = (unsigned char) kind; scpy (src.name, name, NAME_MAX); src.regime = PR_BE; }
	PartyDialog d (src, fresh);
	if (d.run () != 1) { if (fresh || d.p.notes != src.notes) sfree (d.p.notes); return 0; }
	if (fresh)
	{
		Party &n = party_add (g_b);
		int nid = n.id;
		n = d.p; n.id = nid;
		changed ();
		char m[96]; scpy (m, TR ("Added: "), sizeof m); scat (m, n.name, sizeof m); status (m);
		return nid;
	}
	int i = party_index (g_b, id);
	if (i >= 0) { char *old = g_b.pty[i].notes; g_b.pty[i] = d.p; if (old != d.p.notes) sfree (old); }
	changed ();
	status (TR ("Card saved"));
	return id;
}
inline void InvoicePage::s_newParty ()
{
	char t[NAME_MAX]; trim_copy (t, g_inv->party->val[0] ? "" : g_inv->party->text (), sizeof t);
	int id = edit_party (0, g_inv->sale ? PK_CUSTOMER : PK_SUPPLIER, t);
	if (!id) return;
	char pv[16] = "P"; scat_num (pv, id, sizeof pv);
	g_inv->party->setValue (pv);
	g_inv->partyChosen ();
	g_inv->due->setFocus ();
}

// =====================================================================================================================================
// ---- the customers, the suppliers -------------------------------------------------------------------------------------------------
// =====================================================================================================================================
struct PRow { int p; money bal, late; };
struct LRow { int e, l; money run; };			// e -1: the balance brought forward
class PartyPage;
static PartyPage *g_pp[2];

class PartyPage : public Page
{
public:
	int kind;
	DataGrid *g; TickGrid *lg_; SearchBox *search; Segmented *filter; Checkbox *openOnly;
	FlatButton *bNew, *bEdit, *bMatch, *bUnmatch;
	PRow *rows; int nrows, cap, keepId;
	LRow *lines; int nlines, lcap;
	bool *tick; int ntick; money tickSum;
	money tBal, tLate;
	PartyPage (int id_) : Page (id_), g (0), lg_ (0), search (0), filter (0), openOnly (0), bNew (0), bEdit (0), bMatch (0), bUnmatch (0), rows (0), nrows (0), cap (0), keepId (0),
		lines (0), nlines (0), lcap (0), tick (0), ntick (0), tickSum (0), tBal (0), tLate (0)
	{
		kind = id == P_CUST ? PK_CUSTOMER : PK_SUPPLIER;
		resizeTo (800, 660);
		HeadRow h (this);
		bNew = h.add (kind == PK_CUSTOMER ? TR ("New customer") : TR ("New supplier"), s_new, FB_PRIMARY, NI_PLUS, TR ("A new card (Ctrl+N)"));
		bEdit = h.add (TR ("Card"), s_edit, FB_SECONDARY, NI_EDIT, TR ("The chosen one's card"));
		static const char *const F[3] = { "All", "With a balance", "Overdue" };
		const char *tf[3]; filter = new Segmented (16, 70, tr_list (F, 3, tf), 3, on_filter); addChild (filter);
		search = new SearchBox (220); search->left = width - 16 - 220; search->top = 70; search->anchor = ANCHOR_RIGHT | ANCHOR_TOP;
		search->placeholder = TR ("Search"); search->onChange = on_search; search->onEnter = on_search_done; addChild (search);
		g = new_grid (this, 16, 108, width - 32, 200, this); g->anchor = ANCHOR_LEFT | ANCHOR_TOP;
		g->setColumns (6);
		g->setColumn (0, TR ("Code"), 92); g->setColumn (1, TR ("Name"), 200); g->setColumn (2, TR ("VAT number"), 142); g->setColumn (3, TR ("City"), 112);
		g->setColumn (4, TR ("Balance"), 112, GRID_RIGHT); g->setColumn (5, TR ("Overdue"), 104, GRID_RIGHT);
		g->sortCol = 1; g->sortDesc = false;
		g->cellText = p_text; g->cellDraw = p_draw; g->onSelect = on_pick; g->onActivate = on_card; g->onSort = on_sort;
		g->emptyText = kind == PK_CUSTOMER ? TR ("No customer yet: New customer adds one.") : TR ("No supplier yet: New supplier adds one.");
		lg_ = new TickGrid (16, 360, width - 32, 200); lg_->user = this; addChild (lg_); lg_->anchor = ANCHOR_LEFT | ANCHOR_TOP; lg_->sortable = false;
		lg_->setColumns (8);
		lg_->setColumn (0, "", 30); lg_->setColumn (1, TR ("Date"), 96); lg_->setColumn (2, TR ("Document"), 86); lg_->setColumn (3, TR ("Description"), 170);
		lg_->setColumn (4, TR ("Due"), 96); lg_->setColumn (5, TR ("Debit"), 98, GRID_RIGHT); lg_->setColumn (6, TR ("Credit"), 98, GRID_RIGHT); lg_->setColumn (7, TR ("Balance"), 112, GRID_RIGHT);
		lg_->cellText = l_text; lg_->cellDraw = l_draw; lg_->onActivate = on_line_open; lg_->onSelect = on_line_sel; lg_->onTickRow = on_line_tick;
		lg_->emptyText = TR ("No line in this fiscal year.");
		bMatch = new FlatButton (TR ("Match"), s_match, FB_SECONDARY, NI_LINK); addChild (bMatch);
		bMatch->tip = TR ("The lines ticked (adding up to zero) matched together: an invoice and its payment");
		bUnmatch = new FlatButton (TR ("Unmatch"), s_unmatch, FB_QUIET); addChild (bUnmatch);
		bUnmatch->tip = TR ("The chosen line's matching undone");
		openOnly = new Checkbox (0, 0, 164, 26, TR ("Open items only"), false, on_open_only, C_BG); addChild (openOnly);
		openOnly->tip = TR ("Only the lines not matched yet, of every year");
		place ();
	}
	~PartyPage () { delete [] rows; delete [] lines; delete [] tick; }
	int bandY () const { return wg (g)->top + g->height + 8; }
	void place ()
	{
		if (!g || !lg_ || !bMatch) return;
		int gh = (height - 108 - 12 - 56) * 42 / 100;
		g->resizeTo (width - 32, gh);
		int by = bandY ();
		int x = width - 16;
		bUnmatch->left = x - bUnmatch->width; bUnmatch->top = by + 6; x = bUnmatch->left - 8;
		bMatch->left = x - bMatch->width; bMatch->top = by + 6; x = bMatch->left - 14;
		openOnly->left = x - openOnly->width; openOnly->top = by + 8;
		wg (lg_)->top = by + 48; lg_->resizeTo (width - 32, imax (60, height - wg (lg_)->top - 12));
		fit_columns (g, 1); fit_columns (lg_, 3);
	}
	void resizeTo (int w, int h) override { Page::resizeTo (w, h); place (); }
	const char *title () override { return kind == PK_CUSTOMER ? TR ("Customers") : TR ("Suppliers"); }
	void subtitle (char *out, int cap_) override
	{
		out[0] = '\0'; scat_num (out, nrows, cap_); scat (out, kind == PK_CUSTOMER ? (nrows == 1 ? TR (" customer") : TR (" customers")) : (nrows == 1 ? TR (" supplier") : TR (" suppliers")), cap_);
		char a[32];
		scat (out, "  \xB7  ", cap_); scat (out, kind == PK_CUSTOMER ? TR ("to receive ") : TR ("to pay "), cap_); scat (out, money_s (tBal, a), cap_);
		if (tLate) { scat (out, "  \xB7  ", cap_); scat (out, TR ("overdue "), cap_); scat (out, money_s (tLate, a), cap_); }
	}
	int selParty () const { return g->sel >= 0 && g->sel < nrows ? g_b.pty[rows[g->sel].p].id : 0; }
	void refresh () override
	{
		search->left = width - 16 - search->width;
		int keep = selParty (); if (!keep) keep = keepId;
		int sg = kind == PK_CUSTOMER ? 1 : -1, t = today_ymd ();
		money *bal = new money[g_b.npty + 1], *late = new money[g_b.npty + 1];
		for (int i = 0; i < g_b.npty; i++) bal[i] = late[i] = 0;
		for (int i = 0; i < g_b.ne; i++)
		{
			const Entry &e = g_b.e[i];
			for (int k = 0; k < e.nl; k++)
			{
				const Line &l = e.l[k];
				if (!l.party || !acc_party (g_b, l.account)) continue;
				int pi = party_index (g_b, l.party);
				if (pi < 0) continue;
				bal[pi] += l.amount;
				if (!l.match && (l.due ? l.due : e.date) < t && sg * l.amount > 0) late[pi] += sg * l.amount;
			}
		}
		nrows = 0; tBal = tLate = 0;
		const char *words = search->text ();
		for (int i = 0; i < g_b.npty; i++)
		{
			const Party &p = g_b.pty[i];
			if (p.kind != kind) continue;
			PRow r; r.p = i; r.bal = sg * bal[i]; r.late = late[i];
			if (filter->cur == 1 && !r.bal) continue;
			if (filter->cur == 2 && !r.late) continue;
			if (words[0])
			{
				char hay[320]; scpy (hay, p.code, sizeof hay); scat (hay, " ", sizeof hay); scat (hay, p.name, sizeof hay); scat (hay, " ", sizeof hay);
				scat (hay, p.city, sizeof hay); scat (hay, " ", sizeof hay); scat (hay, p.vat, sizeof hay); scat (hay, " ", sizeof hay); scat (hay, p.email, sizeof hay);
				if (!words_in (hay, words)) continue;
			}
			if (p.hidden && !r.bal && !words[0]) continue;
			if (nrows == cap) { int c = cap ? cap * 2 : 128; PRow *nr = new PRow[c]; for (int q = 0; q < nrows; q++) nr[q] = rows[q]; delete [] rows; rows = nr; cap = c; }
			rows[nrows++] = r; tBal += r.bal; tLate += r.late;
		}
		delete [] bal; delete [] late;
		int *ix = new int[nrows + 1]; for (int i = 0; i < nrows; i++) ix[i] = i;
		idx_sort (ix, nrows, cmp, this);
		PRow *tmp = new PRow[nrows + 1]; for (int i = 0; i < nrows; i++) tmp[i] = rows[ix[i]]; for (int i = 0; i < nrows; i++) rows[i] = tmp[i];
		delete [] tmp; delete [] ix;
		g->setRows (nrows);
		int sel = -1; for (int i = 0; i < nrows; i++) if (g_b.pty[rows[i].p].id == keep) sel = i;
		g->setSel (sel >= 0 ? sel : nrows ? 0 : -1);
		listLines ();
		invalidate (true);
	}
	static int cmp (void *ctx, int x, int y)
	{
		PartyPage *pp = (PartyPage *) ctx;
		const PRow &a = pp->rows[x], &b = pp->rows[y];
		const Party &pa = g_b.pty[a.p], &pb = g_b.pty[b.p];
		int r = 0;
		switch (pp->g->sortCol)
		{
		case 0: r = ci_cmp (pa.code, pb.code); break;
		case 2: r = ci_cmp (pa.vat, pb.vat); break;
		case 3: r = ci_cmp (pa.city, pb.city); break;
		case 4: r = a.bal < b.bal ? -1 : a.bal > b.bal; break;
		case 5: r = a.late < b.late ? -1 : a.late > b.late; break;
		}
		if (!r) r = ci_cmp (pa.name, pb.name);
		return pp->g->sortDesc ? -r : r;
	}
	void addLine (int e, int l, money run)
	{
		if (nlines == lcap) { int c = lcap ? lcap * 2 : 256; LRow *nr = new LRow[c]; for (int q = 0; q < nlines; q++) nr[q] = lines[q]; delete [] lines; lines = nr; lcap = c; delete [] tick; tick = new bool[c]; }
		lines[nlines].e = e; lines[nlines].l = l; lines[nlines].run = run; nlines++;
	}
	// The chosen party's lines: those of the year shown (after the balance brought forward), or its open ones.
	void listLines ()
	{
		nlines = 0; ntick = 0; tickSum = 0;
		int pid = selParty ();
		if (pid)
		{
			bool open = openOnly->checked;
			int from = open ? 0 : yfrom (), to = open ? 0 : yto ();
			money run = open ? 0 : party_balance (g_b, pid, date_add (from, -1));
			if (!open && run) addLine (-1, 0, run);
			int *idx, n = entries_sorted (g_b, -1, from, to, false, &idx);
			for (int i = 0; i < n; i++)
			{
				const Entry &e = g_b.e[idx[i]];
				for (int k = 0; k < e.nl; k++)
				{
					const Line &l = e.l[k];
					if (l.party != pid || !acc_party (g_b, l.account) || !l.amount) continue;
					if (open && l.match) continue;
					run += l.amount;
					addLine (idx[i], k, run);
				}
			}
			delete [] idx;
		}
		for (int i = 0; i < nlines; i++) tick[i] = false;
		lg_->setRows (nlines);
		lg_->setSel (nlines ? nlines - 1 : -1);
		lg_->ensureVisible (nlines - 1);
		buttonsState ();
	}
	const Line *lineAt (int row) const { return row >= 0 && row < nlines && lines[row].e >= 0 ? &g_b.e[lines[row].e].l[lines[row].l] : 0; }
	void buttonsState ()
	{
		tickSum = 0; ntick = 0;
		for (int i = 0; i < nlines; i++) if (tick[i] && lineAt (i)) { ntick++; tickSum += lineAt (i)->amount; }
		char t[48]; scpy (t, TR ("Match"), sizeof t);
		if (ntick) { char a[32]; scat (t, " (", sizeof t); scat (t, money_s (tickSum, a), sizeof t); scat (t, ")", sizeof t); }
		bMatch->setText (t);
		bMatch->setDisabled (ntick < 2 || tickSum != 0);
		const Line *l = lineAt (lg_->sel);
		bUnmatch->setDisabled (!(l && l->match));
		place ();
		invalidate (true);
	}
	static const char *p_text (DataGrid &gr, int row, int col, char *buf, int cap_)
	{
		PartyPage *pp = (PartyPage *) gr.user;
		if (row >= pp->nrows) return "";
		const Party &p = g_b.pty[pp->rows[row].p];
		switch (col)
		{
		case 0: return p.code;
		case 1: return p.name;
		case 2: vat_show (p.vat, buf, cap_); return buf;
		case 3: return p.city;
		}
		return "";
	}
	static bool p_draw (DataGrid &gr, Canvas &cv, int row, int col, int x, int y, int w, int h, unsigned ink, bool sel)
	{
		PartyPage *pp = (PartyPage *) gr.user;
		if (row >= pp->nrows || col < 4) return false;
		const PRow &r = pp->rows[row];
		if (col == 4) cell_money (cv, x, y, w, h, r.bal, ink, sel, false);
		else if (r.late) { char t[32]; fmt_money (r.late, t); cell_text (cv, x, y, w, h, t, sel ? ink : C_BAD, true); }
		return true;
	}
	static const char *l_text (DataGrid &gr, int row, int col, char *buf, int cap_)
	{
		PartyPage *pp = (PartyPage *) gr.user;
		if (row >= pp->nlines) return "";
		if (pp->lines[row].e < 0) { if (col == 3) return TR ("Balance brought forward"); if (col == 1) { date_show (yfrom (), buf); return buf; } return ""; }
		const Entry &e = g_b.e[pp->lines[row].e]; const Line &l = e.l[pp->lines[row].l];
		switch (col)
		{
		case 1: date_show (e.date, buf); return buf;
		case 2: short_number (e, true, buf, cap_); return buf;
		case 3: scpy (buf, l.text[0] && !seq (l.text, party_name (g_b, l.party)) ? l.text : e.text, cap_); if (e.ref[0] && pp->kind == PK_SUPPLIER) { scat (buf, " (", cap_); scat (buf, e.ref, cap_); scat (buf, ")", cap_); } return buf;
		case 4: if (l.match) return ""; date_show (l.due ? l.due : e.date, buf); return buf;
		}
		return "";
	}
	static bool l_draw (DataGrid &gr, Canvas &cv, int row, int col, int x, int y, int w, int h, unsigned ink, bool sel)
	{
		PartyPage *pp = (PartyPage *) gr.user;
		if (row >= pp->nlines) return false;
		if (pp->lines[row].e < 0)
		{
			if (col == 7) { char t[40]; fmt_dc (pp->lines[row].run, t); cell_text (cv, x, y, w, h, t, ink, true, 2); return true; }
			if (col == 3 || col == 1) { char b[64]; cell_text (cv, x, y, w, h, l_text (gr, row, col, b, sizeof b), sel ? ink : field_dim (), false); return true; }
			return true;
		}
		const Entry &e = g_b.e[pp->lines[row].e]; const Line &l = e.l[pp->lines[row].l];
		unsigned in = l.match && !sel ? wk_mix (C_FIELD, ink, 150) : ink;
		switch (col)
		{
		case 0:
			if (l.match) { draw_ni (cv, NI_LINK, x + (w - 20) / 2, y + (h - 20) / 2, in, sel ? in : C_ACCENT); return true; }
			wk_check_mark (cv, x + (w - 16) / 2, y + (h - 16) / 2, 16, pp->tick[row], WK_NORMAL);
			return true;
		case 4: { if (l.match) return true; int d = l.due ? l.due : e.date; char t[16]; date_show (d, t); cell_text (cv, x, y, w, h, t, sel ? ink : d < today_ymd () ? C_BAD : ink, false); return true; }
		case 5: if (l.amount > 0) cell_money (cv, x, y, w, h, l.amount, in, sel); return true;
		case 6: if (l.amount < 0) cell_money (cv, x, y, w, h, -l.amount, in, sel); return true;
		case 7: { char t[40]; fmt_dc (pp->lines[row].run, t); cell_text (cv, x, y, w, h, t, ink, true); return true; }
		}
		if (l.match && !sel && col >= 1 && col <= 3) { char b[160]; const char *s = l_text (gr, row, col, b, sizeof b); cell_text (cv, x, y, w, h, s, in, false); return true; }
		return false;
	}
	void toggle (int row)
	{
		const Line *l = lineAt (row);
		if (!l || l->match) return;
		tick[row] = !tick[row];
		lg_->invalidate (true);
		buttonsState ();
	}
	void match ()
	{
		if (ntick < 2 || tickSum) return;
		LineRef r[64]; int n = 0;
		for (int i = 0; i < nlines && n < 64; i++) if (tick[i] && lines[i].e >= 0) { r[n].e = lines[i].e; r[n].l = lines[i].l; n++; }
		if (match_lines (g_b, r, n)) { changed (); status (TR ("Lines matched")); }
	}
	void unmatchSel ()
	{
		const Line *l = lineAt (lg_->sel);
		if (!l || !l->match) return;
		unmatch (g_b, l->match);
		changed ();
		status (TR ("Matching undone"));
	}
	void cmdNew () override
	{
		int id2 = edit_party (0, kind);
		if (id2) { keepId = id2; search->setText (""); filter->set (0); refresh (); }
	}
	void editSel () { int pid = selParty (); if (pid) edit_party (pid, kind); }
	void cmdFind () override { search->setFocus (); search->selectAll (); }
	void cmdDelete () override
	{
		int pid = selParty ();
		if (!pid) return;
		for (int i = 0; i < g_b.ne; i++)
		{
			bool has = g_b.e[i].party == pid;
			for (int k = 0; k < g_b.e[i].nl && !has; k++) if (g_b.e[i].l[k].party == pid) has = true;
			if (has) { warn (TR ("Delete"), TR ("This party has documents in the books: its card cannot be deleted.")); return; }
		}
		for (int i = 0; i < g_b.ncd; i++)
			if (g_b.cd[i].party == pid) { warn (TR ("Delete"), TR ("This party has quotes or orders (Quotes and orders): its card cannot be deleted.")); return; }
		if (ask (TR ("Delete"), TR ("Delete this card for good?"), MB_YESNO, 2) != 1) return;
		int i = party_index (g_b, pid);
		sfree (g_b.pty[i].notes);
		for (int k = i + 1; k < g_b.npty; k++) g_b.pty[k - 1] = g_b.pty[k];
		g_b.npty--;
		changed ();
	}
	void show (int pid) { keepId = pid; search->setText (""); filter->set (0); g->setSel (-1); refresh (); }
	void enter () override { g->setFocus (); }
	bool onKey (long k) override
	{
		if (k == KEY_DEL) { cmdDelete (); return true; }
		if (k == ' ' && lg_->hasFocus) { toggle (lg_->sel); return true; }
		return false;
	}
	void onDraw () override
	{
		drawHead ();
		int y = bandY ();
		const Party *p = party_of (g_b, selParty ());
		if (!p) return;
		int tw = openOnly->left - 30;
		text_fit_l (canvas, 20, y + 2, tw, 20, p->name, C_TEXT, 2);
		char t[240] = ""; scpy (t, p->street, sizeof t);
		if (p->zip[0] || p->city[0]) { if (t[0]) scat (t, ", ", sizeof t); scat (t, p->zip, sizeof t); scat (t, " ", sizeof t); scat (t, p->city, sizeof t); }
		if (p->email[0]) { if (t[0]) scat (t, "  \xB7  ", sizeof t); scat (t, p->email, sizeof t); }
		if (p->phone[0]) { if (t[0]) scat (t, "  \xB7  ", sizeof t); scat (t, p->phone, sizeof t); }
		text_fit_l (canvas, 20, y + 22, tw, 18, t, dim_ink (C_BG));
	}
	static PartyPage *of (Widget &w) { return (PartyPage *) w.parent; }
	static PartyPage *cur () { for (int i = 0; i < 2; i++) if (g_pp[i] && !g_pp[i]->hidden) return g_pp[i]; return 0; }
	static void on_filter (int) { PartyPage *p = cur (); if (p) p->refresh (); }
	static void on_search (Widget &w) { of (w)->refresh (); }
	static void on_search_done (Widget &w) { of (w)->g->setFocus (); }
	static void on_pick (Widget &w) { PartyPage *p = (PartyPage *) ((DataGrid &) w).user; p->listLines (); p->invalidate (true); }
	static void on_card (Widget &w) { ((PartyPage *) ((DataGrid &) w).user)->editSel (); }
	static void on_sort (Widget &w)
	{
		DataGrid &d = (DataGrid &) w;
		if (d.sortCol == d.clickedCol) d.sortDesc = !d.sortDesc; else { d.sortCol = d.clickedCol; d.sortDesc = d.clickedCol >= 4; }
		((PartyPage *) d.user)->refresh ();
	}
	static void on_line_open (Widget &w)
	{
		DataGrid &d = (DataGrid &) w; PartyPage *p = (PartyPage *) d.user;
		if (d.sel >= 0 && d.sel < p->nlines && p->lines[d.sel].e >= 0) open_entry (g_b.e[p->lines[d.sel].e].id);
	}
	static void on_line_sel (Widget &w) { PartyPage *p = (PartyPage *) ((DataGrid &) w).user; p->buttonsState (); }
	static void on_line_tick (Widget &w) { PartyPage *p = (PartyPage *) ((DataGrid &) w).user; p->toggle (((TickGrid &) w).tickRow); }
	static void on_open_only (Widget &) { PartyPage *p = cur (); if (p) p->listLines (); }
	static void s_new () { PartyPage *p = cur (); if (p) p->cmdNew (); }
	static void s_edit () { PartyPage *p = cur (); if (p) p->editSel (); }
	static void s_match () { PartyPage *p = cur (); if (p) p->match (); }
	static void s_unmatch () { PartyPage *p = cur (); if (p) p->unmatchSel (); }
};

// =====================================================================================================================================
// ---- the chart of accounts ----------------------------------------------------------------------------------------------------------
// =====================================================================================================================================
class AccountDialog : public FormBox
{
public:
	LineEdit *code, *name; ChoiceBox *nature; Checkbox *hide; bool isNew; char orig[CODE_MAX];
	AccountDialog (const Account *a) : FormBox (a ? TR ("Account") : TR ("New account"), 560, 0, 110), isNew (!a)
	{
		orig[0] = '\0'; if (a) scpy (orig, a->code, CODE_MAX);
		int y = y0;
		code = edit (y, TR ("Number"), 120, a ? a->code : ""); code->accept = accept_int; code->placeholder = TR ("6 digits");
		if (a) { code->disabled = true; }
		y += 34;
		name = edit (y, TR ("Name"), 400, a ? a->name : ""); y += 34;
		static const char *const NAT[4] = { "By its number", "Goods (grid 81)", "Services (grid 82)", "Investments (grid 83)" };
		label (y, TR ("Purchases")); nature = new ChoiceBox (fieldX, y, 220); { const char *tn[4]; nature->setOptions (tr_list (NAT, 4, tn), 4); }
		nature->sel = !a ? 0 : a->nature == NAT_GOODS ? 1 : a->nature == NAT_SERVICES ? 2 : a->nature == NAT_INVEST ? 3 : 0; addChild (nature); y += 34;
		hide = new Checkbox (fieldX, y, 300, 26, TR ("Hidden (no longer offered)"), a && a->hidden, 0, C_FACE); addChild (hide); y += 44;
		resizeTo (width, y + 50);
		Root *r = Root::current ();
		if (r) { left = (r->width - width) / 2; top = imax (0, (r->height - height) / 2); }
		buttons (a ? "OK" : TR ("Add"));
		(a ? name : code)->setFocus ();
	}
	bool validate () override
	{
		char c[CODE_MAX]; trim_copy (c, code->text (), sizeof c);
		if (isNew)
		{
			if (slen (c) < 4 || !all_digits (c)) { warn (TR ("Account"), TR ("An account's number: 4 to 10 digits (the PCMN's: 6).")); code->setFocus (); return false; }
			if (c[0] > '7') { warn (TR ("Account"), TR ("Classes 0 to 7 only (the PCMN's).")); code->setFocus (); return false; }
			if (acc_find (g_b, c) >= 0) { warn (TR ("Account"), TR ("This number is in the chart already.")); code->setFocus (); return false; }
		}
		char n[ACC_NAME_MAX]; trim_copy (n, name->text (), sizeof n);
		if (!n[0]) { warn (TR ("Account"), TR ("Type its name.")); name->setFocus (); return false; }
		return true;
	}
};
// An account's card (0: a new one) -> its code in out ("": cancelled).
static bool edit_account (const char *code, char *out)
{
	int i = code ? acc_find (g_b, code) : -1;
	AccountDialog d (i >= 0 ? &g_b.acc[i] : 0);
	if (d.run () != 1) return false;
	char c[CODE_MAX], n[ACC_NAME_MAX]; trim_copy (c, d.code->text (), sizeof c); trim_copy (n, d.name->text (), sizeof n);
	static const char NATS[4] = { NAT_AUTO, NAT_GOODS, NAT_SERVICES, NAT_INVEST };
	int k = acc_put (g_b, i >= 0 ? g_b.acc[i].code : c, n);
	g_b.acc[k].nature = NATS[iclamp (d.nature->sel, 0, 3)];
	g_b.acc[k].hidden = d.hide->checked;
	scpy (out, g_b.acc[k].code, CODE_MAX);
	changed ();
	status (i >= 0 ? TR ("Account saved") : TR ("Account added"));
	return true;
}

struct ARow { int a; unsigned char depth; bool open, kids; money bal; };
struct RRowL { int e, l; money run; };
class AccountsPage;
static AccountsPage *g_ap;

class AccountsPage : public Page
{
public:
	DataGrid *tree, *reg; SearchBox *search; Segmented *filter;
	FlatButton *bNew, *bEdit;
	ARow *rows; int nrows, cap;
	RRowL *lines; int nlines, lcap; money opening;
	bool *used; money *bal; bool *closed;		// by account index
	int nacc0; char keep[CODE_MAX];
	AccountsPage () : Page (P_ACCOUNTS), tree (0), reg (0), search (0), filter (0), bNew (0), bEdit (0), rows (0), nrows (0), cap (0), lines (0), nlines (0), lcap (0), opening (0), used (0), bal (0), closed (0), nacc0 (0)
	{
		keep[0] = '\0';
		resizeTo (800, 660);
		HeadRow h (this);
		bNew = h.add (TR ("New account"), s_new, FB_PRIMARY, NI_PLUS, TR ("An account added to the chart (Ctrl+N)"));
		bEdit = h.add (TR ("Edit"), s_edit, FB_SECONDARY, NI_EDIT, TR ("The chosen account's name, its purchases' nature"));
		static const char *const F[2] = { "With movements", "All" };
		const char *tf[2]; filter = new Segmented (16, 70, tr_list (F, 2, tf), 2, on_filter); addChild (filter);
		search = new SearchBox (220); search->left = width - 16 - 220; search->top = 70; search->anchor = ANCHOR_RIGHT | ANCHOR_TOP;
		search->placeholder = TR ("Number or name"); search->onChange = on_search; search->onEnter = on_search_done; addChild (search);
		tree = new_grid (this, 16, 108, width - 32, 200, this); tree->anchor = ANCHOR_LEFT | ANCHOR_TOP; tree->sortable = false;
		tree->setColumns (4);
		tree->setColumn (0, TR ("Number"), 126); tree->setColumn (1, TR ("Name"), 300); tree->setColumn (2, TR ("Debit"), 120, GRID_RIGHT); tree->setColumn (3, TR ("Credit"), 120, GRID_RIGHT);
		tree->cellText = t_text; tree->cellDraw = t_draw; tree->onSelect = on_pick; tree->onActivate = on_toggle;
		reg = new_grid (this, 16, 360, width - 32, 200, this); reg->anchor = ANCHOR_LEFT | ANCHOR_TOP; reg->sortable = false;
		reg->setColumns (6);
		reg->setColumn (0, TR ("Date"), 96); reg->setColumn (1, TR ("Document"), 86); reg->setColumn (2, TR ("Description"), 150);
		reg->setColumn (3, TR ("Debit"), 104, GRID_RIGHT); reg->setColumn (4, TR ("Credit"), 104, GRID_RIGHT); reg->setColumn (5, TR ("Balance"), 124, GRID_RIGHT);
		reg->cellText = r_text; reg->cellDraw = r_draw; reg->onActivate = on_open;
		reg->emptyText = TR ("No line in the year.");
		place ();
	}
	~AccountsPage () { delete [] rows; delete [] lines; delete [] used; delete [] bal; delete [] closed; }
	int bandY () const { return wg (tree)->top + tree->height + 8; }
	void place ()
	{
		if (!tree || !reg) return;
		int th = (height - 108 - 12 - 48) * 48 / 100;
		tree->resizeTo (width - 32, th);
		wg (reg)->top = bandY () + 42; reg->resizeTo (width - 32, imax (60, height - wg (reg)->top - 12));
		fit_columns (tree, 1, 120); fit_columns (reg, 2, 100);
	}
	void resizeTo (int w, int h) override { Page::resizeTo (w, h); place (); }
	const char *title () override { return TR ("Chart of accounts"); }
	void subtitle (char *out, int cap_) override
	{
		int n = 0, u = 0; for (int i = 0; i < g_b.nacc; i++) if (!acc_heading (g_b.acc[i].code)) { n++; if (used && i < nacc0 && used[i]) u++; }
		out[0] = '\0'; scat_num (out, n, cap_); scat (out, TR (" accounts"), cap_); scat (out, "  \xB7  ", cap_); scat_num (out, u, cap_); scat (out, TR (" with movements"), cap_);
		if (g_b.chart[0]) { scat (out, "  \xB7  ", cap_); scat (out, TR ("PCMN in "), cap_); scat (out, g_b.chart[0] == 'n' ? TRC ("in", "Dutch") : TRC ("in", "French"), cap_); }
	}
	const char *selCode () { return tree->sel >= 0 && tree->sel < nrows ? g_b.acc[rows[tree->sel].a].code : ""; }
	void refresh () override
	{
		search->left = width - 16 - search->width;
		if (tree->sel >= 0 && tree->sel < nrows) scpy (keep, g_b.acc[rows[tree->sel].a].code, CODE_MAX);
		// each account's balance at the year's end (the income statement's: the year's), used or not
		delete [] used; delete [] bal;
		bool *oldClosed = closed; int oldN = nacc0;
		nacc0 = g_b.nacc;
		used = new bool[nacc0 + 1]; bal = new money[nacc0 + 1]; closed = new bool[nacc0 + 1];
		for (int i = 0; i < nacc0; i++) { used[i] = false; bal[i] = 0; closed[i] = false; }
		(void) oldN;
		delete [] oldClosed;
		int from = yfrom (), to = yto ();
		for (int i = 0; i < g_b.ne; i++)
		{
			const Entry &e = g_b.e[i];
			if (e.date > to) continue;
			for (int k = 0; k < e.nl; k++)
			{
				const Line &l = e.l[k];
				int a = acc_find (g_b, l.account);
				if (a < 0) continue;
				if (acc_pl (l.account) && e.date < from) continue;
				bal[a] += l.amount;
				used[a] = true;
			}
		}
		// the headings' sums (a heading holds the accounts starting with its code)
		for (int i = 0; i < nacc0; i++)
		{
			if (!acc_heading (g_b.acc[i].code)) continue;
			const char *h = g_b.acc[i].code;
			for (int j = i + 1; j < nacc0 && starts_with (g_b.acc[j].code, h); j++)
				if (!acc_heading (g_b.acc[j].code)) { bal[i] += bal[j]; if (used[j]) used[i] = true; }
		}
		build ();
		invalidate (true);
	}
	void build ()
	{
		const char *words = search->text ();
		bool all = filter->cur == 1 || words[0];
		nrows = 0;
		int closedAt = -1; unsigned char closedDepth = 0;
		for (int i = 0; i < nacc0; i++)
		{
			const Account &a = g_b.acc[i];
			int n = slen (a.code);
			unsigned char depth = (unsigned char) (n <= 3 ? n - 1 : 3);
			if (!all && !used[i]) continue;
			if (words[0])
			{
				char hay[200]; scpy (hay, a.code, sizeof hay); scat (hay, " ", sizeof hay); scat (hay, a.name, sizeof hay);
				bool hit = starts_with (a.code, words) || words_in (hay, words);
				bool childHit = false;
				if (!hit && acc_heading (a.code))
					for (int j = i + 1; j < nacc0 && starts_with (g_b.acc[j].code, a.code) && !childHit; j++)
					{ char h2[200]; scpy (h2, g_b.acc[j].code, sizeof h2); scat (h2, " ", sizeof h2); scat (h2, g_b.acc[j].name, sizeof h2); childHit = starts_with (g_b.acc[j].code, words) || words_in (h2, words); }
				if (!hit && !childHit) continue;
			}
			if (closedAt >= 0 && depth > closedDepth && starts_with (a.code, g_b.acc[closedAt].code)) continue;
			closedAt = -1;
			bool kids = acc_heading (a.code) && i + 1 < nacc0 && starts_with (g_b.acc[i + 1].code, a.code);
			if (nrows == cap) { int c = cap ? cap * 2 : 512; ARow *nr = new ARow[c]; for (int q = 0; q < nrows; q++) nr[q] = rows[q]; delete [] rows; rows = nr; cap = c; }
			ARow &r = rows[nrows++];
			r.a = i; r.depth = depth; r.kids = kids; r.open = !closed[i]; r.bal = bal[i];
			if (kids && closed[i]) { closedAt = i; closedDepth = depth; }
		}
		tree->setRows (nrows);
		int sel = -1; for (int i = 0; i < nrows; i++) if (seq (g_b.acc[rows[i].a].code, keep)) sel = i;
		if (sel < 0) for (int i = 0; i < nrows; i++) if (!acc_heading (g_b.acc[rows[i].a].code)) { sel = i; break; }
		tree->setSel (sel);
		listLines ();
	}
	void listLines ()
	{
		nlines = 0; opening = 0;
		const char *c = selCode ();
		if (c[0])
		{
			int from = yfrom (), to = yto ();
			bool head = acc_heading (c);
			opening = opening_of (g_b, c, from, !head);
			money run = opening;
			int *idx, n = entries_sorted (g_b, -1, from, to, false, &idx);
			for (int i = 0; i < n; i++)
			{
				const Entry &e = g_b.e[idx[i]];
				for (int k = 0; k < e.nl; k++)
				{
					const Line &l = e.l[k];
					if (head ? !starts_with (l.account, c) : !seq (l.account, c)) continue;
					run += l.amount;
					if (nlines == lcap) { int cc = lcap ? lcap * 2 : 256; RRowL *nr = new RRowL[cc]; for (int q = 0; q < nlines; q++) nr[q] = lines[q]; delete [] lines; lines = nr; lcap = cc; }
					lines[nlines].e = idx[i]; lines[nlines].l = k; lines[nlines].run = run; nlines++;
				}
			}
			delete [] idx;
		}
		reg->setRows (nlines + (opening ? 1 : 0));
		reg->setSel (-1);
		reg->ensureVisible (nlines);
		invalidate (true);
	}
	void toggleRow (int r)
	{
		if (r < 0 || r >= nrows || !rows[r].kids) return;
		int a = rows[r].a;
		scpy (keep, g_b.acc[a].code, CODE_MAX);
		closed[a] = !closed[a];
		build ();
	}
	static const char *t_text (DataGrid &g, int row, int col, char *buf, int cap_)
	{
		AccountsPage *p = (AccountsPage *) g.user;
		if (row >= p->nrows) return "";
		const Account &a = g_b.acc[p->rows[row].a];
		if (col == 1) return a.name;
		(void) buf; (void) cap_;
		return "";
	}
	static bool t_draw (DataGrid &g, Canvas &cv, int row, int col, int x, int y, int w, int h, unsigned ink, bool sel)
	{
		AccountsPage *p = (AccountsPage *) g.user;
		if (row >= p->nrows) return false;
		const ARow &r = p->rows[row]; const Account &a = g_b.acc[r.a];
		bool head = acc_heading (a.code);
		int style = head && r.depth < 2 ? 2 : 0;
		unsigned in = a.hidden && !sel ? wk_mix (C_FIELD, ink, 120) : ink;
		if (col == 0)
		{
			int ix = x + 4 + r.depth * 12;
			if (r.kids) wk_glyph (cv, r.open ? WKG_CHEV_DOWN : WKG_CHEV_RIGHT, ix + 5, y + h / 2, 8, sel ? ink : field_dim ());
			wk_text_l (cv, ix + 14, y, h, a.code, in, style);
			return true;
		}
		if (col == 1) { cell_text (cv, x, y, w, h, a.name, in, false, style); return true; }
		if (col == 2) { if (r.bal > 0) cell_money (cv, x, y, w, h, r.bal, in, sel, false, style); return true; }	// (its balance: a debit...
		if (col == 3) { if (r.bal < 0) cell_money (cv, x, y, w, h, -r.bal, in, sel, false, style); return true; }	// ... or a credit)
		return false;
	}
	static const char *r_text (DataGrid &g, int row, int col, char *buf, int cap_)
	{
		AccountsPage *p = (AccountsPage *) g.user;
		int k = row - (p->opening ? 1 : 0);
		if (k < 0) return col == 2 ? TR ("Balance brought forward") : "";
		if (k >= p->nlines) return "";
		const Entry &e = g_b.e[p->lines[k].e]; const Line &l = e.l[p->lines[k].l];
		switch (col)
		{
		case 0: date_show (e.date, buf); return buf;
		case 1: short_number (e, true, buf, cap_); return buf;
		case 2:
			if (l.party) { scpy (buf, party_name (g_b, l.party), cap_); if (l.text[0] && !seq (l.text, party_name (g_b, l.party))) { scat (buf, ": ", cap_); scat (buf, l.text, cap_); } return buf; }
			return l.text[0] ? l.text : e.text;
		}
		return "";
	}
	static bool r_draw (DataGrid &g, Canvas &cv, int row, int col, int x, int y, int w, int h, unsigned ink, bool sel)
	{
		AccountsPage *p = (AccountsPage *) g.user;
		int k = row - (p->opening ? 1 : 0);
		if (k < 0) { if (col == 5) { char t[40]; fmt_dc (p->opening, t); cell_text (cv, x, y, w, h, t, ink, true, 2); } else if (col == 2) cell_text (cv, x, y, w, h, TR ("Balance brought forward"), sel ? ink : field_dim (), false); return true; }
		if (k >= p->nlines || col < 3) return false;
		const Line &l = g_b.e[p->lines[k].e].l[p->lines[k].l];
		if (col == 3) { if (l.amount > 0) cell_money (cv, x, y, w, h, l.amount, ink, sel); }
		else if (col == 4) { if (l.amount < 0) cell_money (cv, x, y, w, h, -l.amount, ink, sel); }
		else { char t[40]; fmt_dc (p->lines[k].run, t); cell_text (cv, x, y, w, h, t, ink, true); }
		return true;
	}
	void show (const char *code)
	{
		scpy (keep, code, CODE_MAX);
		search->setText ("");
		int i = acc_find (g_b, code);
		if (i >= 0 && i < nacc0 && !used[i]) filter->set (1);
		for (int j = 0; j < nacc0; j++) if (closed && acc_heading (g_b.acc[j].code) && starts_with (code, g_b.acc[j].code)) closed[j] = false;
		build ();
	}
	void cmdNew () override
	{
		char c[CODE_MAX];
		if (edit_account (0, c)) show (c);
	}
	void editSel ()
	{
		const char *c = selCode ();
		if (!c[0]) return;
		if (acc_heading (c)) { status (TR ("A heading of the PCMN: its accounts are edited")); return; }
		char o[CODE_MAX]; edit_account (c, o);
	}
	void cmdFind () override { search->setFocus (); search->selectAll (); }
	void cmdDelete () override
	{
		const char *c = selCode ();
		if (!c[0] || acc_heading (c)) return;
		int i = acc_find (g_b, c);
		if (i < 0) return;
		if (used[i]) { warn (TR ("Delete"), TR ("This account has lines in the books: hide it instead (Edit > Hidden).")); return; }
		for (int k = 0; k < g_b.npty; k++) if (seq (g_b.pty[k].defAcc, c) || seq (g_b.pty[k].account, c)) { warn (TR ("Delete"), TR ("A party's card names this account.")); return; }
		for (int k = 0; k < g_b.njr; k++) if (seq (g_b.jr[k].account, c)) { warn (TR ("Delete"), TR ("A journal posts to this account.")); return; }
		if (ask (TR ("Delete"), TR ("Remove this account from the chart?"), MB_YESNO, 2) != 1) return;
		acc_remove (g_b, i);
		changed ();
	}
	void enter () override { tree->setFocus (); }
	bool onKey (long k) override
	{
		if (k == KEY_DEL && tree->hasFocus) { cmdDelete (); return true; }
		if ((k == KEY_RIGHT || k == KEY_LEFT) && tree->hasFocus)
		{
			int r = tree->sel;
			if (r >= 0 && r < nrows && rows[r].kids && rows[r].open == (k == KEY_LEFT)) { toggleRow (r); return true; }
		}
		return false;
	}
	void onDraw () override
	{
		drawHead ();
		const char *c = selCode ();
		if (!c[0]) return;
		int y = bandY ();
		char t[200]; scpy (t, c, sizeof t); scat (t, "  ", sizeof t); scat (t, acc_name (g_b, c), sizeof t);
		text_fit_l (canvas, 20, y, width - 40, 20, t, C_TEXT, 2);
		money d = 0, cr = 0; for (int i = 0; i < nlines; i++) { money a = g_b.e[lines[i].e].l[lines[i].l].amount; if (a > 0) d += a; else cr -= a; }
		char a[32], b[32], s[200];
		scpy (s, TR ("In the year: debit "), sizeof s); scat (s, money_s (d, a), sizeof s); scat (s, TR (", credit "), sizeof s); scat (s, money_s (cr, b), sizeof s);
		money end = opening + d - cr;
		char e[40]; fmt_dc (end, e);
		scat (s, "  \xB7  ", sizeof s); scat (s, TR ("balance at the year's end "), sizeof s); scat (s, e, sizeof s);
		text_fit_l (canvas, 20, y + 20, width - 40, 18, s, dim_ink (C_BG));
	}
	static void on_filter (int) { if (g_ap) g_ap->build (); }
	static void on_search (Widget &) { if (g_ap) g_ap->build (); }
	static void on_search_done (Widget &) { if (g_ap) g_ap->tree->setFocus (); }
	static void on_pick (Widget &) { if (g_ap) { if (g_ap->tree->sel >= 0 && g_ap->tree->sel < g_ap->nrows) scpy (g_ap->keep, g_ap->selCode (), CODE_MAX); g_ap->listLines (); } }
	static void on_toggle (Widget &) { if (!g_ap) return; int r = g_ap->tree->sel; if (r >= 0 && r < g_ap->nrows && g_ap->rows[r].kids) g_ap->toggleRow (r); else g_ap->editSel (); }
	static void on_open (Widget &)
	{
		if (!g_ap) return;
		int k = g_ap->reg->sel - (g_ap->opening ? 1 : 0);
		if (k >= 0 && k < g_ap->nlines) open_entry (g_b.e[g_ap->lines[k].e].id);
	}
	static void s_new () { if (g_ap) g_ap->cmdNew (); }
	static void s_edit () { if (g_ap) g_ap->editSel (); }
};

} // namespace lg

#endif
