//
// pages2.h -- the pages that look at the books as a whole:
//   * ReportsPage: a report chosen (the journals, the general ledger, the trial balance, the balance sheet,
//     the income statement, the customers' and suppliers' balances and open items by age, a party's
//     account, the VAT detail), its period (a quarter, the year: a click) and its options; shown as a
//     table (a double click on a line opens its document, its account, its party), exported (a Writer
//     document, a workbook, CSV) and opened in Writer or the Spreadsheet;
//   * VatPage: the VAT returns of a year, a period a tile (filed, due, late; what it comes to); the
//     period chosen's return as the form has it (II the operations, III the purchases, IV the VAT due, V
//     the VAT deductible, VI the balance), the checks Intervat makes; the XML file for Intervat, the
//     period marked filed (its VAT entries then locked) or reopened; the annual customer listing and the
//     intra-community listing (their XML files);
//   * SettingsPage: the company (its name, address, VAT number, bank; its VAT situation and returns'
//     period), the fiscal years (the next one added, a year closed: its result appropriated), the
//     journals, the accounts that play a part (customers, suppliers, VAT, the result carried forward).
// And the new company's questions (NewCompanyDialog: main.cpp's File > New Company).
//
#ifndef _ledger_pages2_h
#define _ledger_pages2_h

#include "lists.h"
#include "export.h"

namespace lg {

// =====================================================================================================================================
// ---- the reports -------------------------------------------------------------------------------------------------------------------
// =====================================================================================================================================
enum { R_JOURNAL, R_LEDGER, R_TRIAL, R_BALANCE, R_INCOME, R_CBAL, R_SBAL, R_CAGE, R_SAGE, R_PARTY, R_VAT, R_COUNT };
static const char *const R_NAME[R_COUNT] = { "Journals", "General ledger", "Trial balance", "Balance sheet", "Income statement", "Customers' balances",
					     "Suppliers' balances", "Receivables by age", "Payables by age", "A party's account", "VAT detail" };
class ReportsPage;
static ReportsPage *g_rp;

class ReportsPage : public Page
{
public:
	ChoiceBox *which, *jbox; JournalChoice jc; const char *jnames[MAXJOURNALS + 1];
	DateEdit *from, *to; Segmented *quick;
	PickEdit *accFrom, *accTo, *party; Checkbox *zeros;
	DataGrid *g; FlatButton *bExport, *bWriter, *bSheet;
	Report p; int kind;
	ReportsPage () : Page (P_REPORTS), which (0), jbox (0), from (0), to (0), quick (0), accFrom (0), accTo (0), party (0), zeros (0), g (0), bExport (0), bWriter (0), bSheet (0), kind (R_LEDGER)
	{
		rpt_init (p);
		resizeTo (800, 660);
		HeadRow h (this);
		bExport = h.add ("Save as...", s_export, FB_SECONDARY, NI_EXPORT, "The report written in a file: a Writer document, a workbook, CSV");
		bSheet = h.add ("Spreadsheet", s_sheet, FB_SECONDARY, NI_REPORT, "The report opened in the Spreadsheet (a workbook)");
		bWriter = h.add ("Writer", s_writer, FB_PRIMARY, NI_DOC, "The report opened in Writer (a document to print)");
		which = new ChoiceBox (16, 70, 210); which->setOptions (R_NAME, R_COUNT); which->sel = kind; which->onChange = on_param; addChild (which);
		from = new DateEdit (290, 70, 130); from->onChange = on_date; addChild (from);
		to = new DateEdit (452, 70, 130); to->onChange = on_date; addChild (to);
		static const char *const Q[6] = { "Year", "Q1", "Q2", "Q3", "Q4", "Month" };
		quick = new Segmented (16, 106, Q, 6, on_quick); addChild (quick);
		int x2 = 16 + quick->width + 16;
		jbox = new ChoiceBox (x2 + 70, 106, 220); jbox->onChange = on_param; addChild (jbox);
		accFrom = new PickEdit (x2 + 70, 106, 150, SK_ACCOUNT, AF_HEADINGS); accFrom->onPick = on_param; accFrom->placeholder = "First"; addChild (accFrom);
		accTo = new PickEdit (x2 + 250, 106, 150, SK_ACCOUNT, AF_HEADINGS); accTo->onPick = on_param; accTo->placeholder = "Last"; addChild (accTo);
		party = new PickEdit (x2 + 70, 106, 280, SK_PARTY, -1); party->onPick = on_param; party->placeholder = "A customer or a supplier"; addChild (party);
		zeros = new Checkbox (600, 71, 180, 24, "Zero balances too", false, on_param, C_BG); zeros->tip = "The accounts without a balance or a movement listed too"; addChild (zeros);
		g = new_grid (this, 16, 146, width - 32, height - 146 - 12, this); g->sortable = false; g->stripes = false;
		g->cellText = c_text; g->cellDraw = c_draw; g->onActivate = on_open;
		g->emptyText = "Nothing in this period.";
		quick->set (0);
	}
	~ReportsPage () { rpt_free (p); }
	const char *title () override { return "Reports"; }
	void subtitle (char *out, int cap) override { scpy (out, p.sub[0] ? p.sub : p.title, cap); }	// (its name: in the choice below)
	bool usesFrom () const { return kind == R_JOURNAL || kind == R_LEDGER || kind == R_INCOME || kind == R_PARTY || kind == R_VAT; }
	void layoutParams ()
	{
		int x2 = 16 + quick->width + 16;
		jbox->hidden = kind != R_JOURNAL;
		accFrom->hidden = accTo->hidden = kind != R_LEDGER;
		party->hidden = kind != R_PARTY;
		zeros->hidden = !(kind == R_LEDGER || kind == R_TRIAL);
		from->hidden = !usesFrom ();
		quick->hidden = false;
		(void) x2;
		invalidate (true);
	}
	void setPeriod (int f, int t)
	{
		char a[16]; date_show (f, a); from->setText (a);
		date_show (t, a); to->setText (a);
	}
	void quickPick (int q)
	{
		int ys = yfrom (), ye = yto ();
		if (!ys) { int y = y_of (today_ymd ()); ys = ymd (y, 1, 1); ye = ymd (y, 12, 31); }
		if (q == 0) setPeriod (ys, ye);
		else if (q <= 4) { int y = y_of (ys), f = ymd (y, q * 3 - 2, 1); setPeriod (f, month_end (y_of (f), q * 3)); }
		else { int t = default_date (); setPeriod (ymd (y_of (t), m_of (t), 1), month_end (y_of (t), m_of (t))); }
	}
	void refresh () override
	{
		jc.fill (0, 0, true);
		jnames[0] = "All the journals"; for (int i = 0; i < jc.n; i++) jnames[i + 1] = jc.names[i];
		int keep = jbox->sel; jbox->setOptions (jnames, jc.n + 1); jbox->sel = iclamp (keep, 0, jc.n);
		if (seenYear != 0 && !from->text ()[0]) quickPick (quick->cur);
		if (!from->text ()[0] || m_year != g_yearVer) { m_year = g_yearVer; quickPick (quick->cur); }
		build ();
	}
	unsigned m_year = 0;
	void build ()
	{
		kind = iclamp (which->sel, 0, R_COUNT - 1);
		layoutParams ();
		int f = date_parse (from->text ()), t = date_parse (to->text ());
		if (!t) t = yto ();
		if (!f) f = yfrom ();
		switch (kind)
		{
		case R_JOURNAL: rpt_journal (g_b, p, jbox->sel > 0 ? jc.idx[jbox->sel - 1] : -1, f, t); break;
		case R_LEDGER:
		{
			char lo[CODE_MAX], hi[CODE_MAX];
			accFrom->resolve (); accTo->resolve ();
			scpy (lo, accFrom->value (), CODE_MAX); scpy (hi, accTo->value (), CODE_MAX);
			rpt_ledger (g_b, p, lo, hi, f, t, zeros->checked);
			break;
		}
		case R_TRIAL: rpt_trial (g_b, p, t, zeros->checked); break;
		case R_BALANCE: rpt_balance_sheet (g_b, p, t); break;
		case R_INCOME: rpt_income (g_b, p, f, t); break;
		case R_CBAL: rpt_party_balance (g_b, p, PK_CUSTOMER, t); break;
		case R_SBAL: rpt_party_balance (g_b, p, PK_SUPPLIER, t); break;
		case R_CAGE: rpt_aged (g_b, p, PK_CUSTOMER, t); break;
		case R_SAGE: rpt_aged (g_b, p, PK_SUPPLIER, t); break;
		case R_PARTY:
		{
			party->resolve ();
			int id = party->value ()[0] == 'P' ? cell_int (party->value () + 1) : 0;
			if (id) rpt_party_ledger (g_b, p, id, f, t);
			else { rpt_free (p); scpy (p.title, "A party's account", sizeof p.title); p.sub[0] = '\0'; }
			break;
		}
		case R_VAT: rpt_vat_detail (g_b, p, f, t); break;
		}
		// the columns: dates and short texts their width, the other texts sharing the rest -- at least their
		// title's, a name its first letters --, the amounts 116 pixels (fewer when that leaves the texts too little)
		g->setColumns (p.ncol);
		int avail = g->width - WK_SBW - 6, fixed = 0, weight = 0, nm = 0, textMin = 0;
		int minW[RMAXCOL];
		for (int c = 0; c < p.ncol && c < RMAXCOL; c++)
		{
			minW[c] = imax (imax (70, wk_text_w (p.col[c].title, 2) + 22), imin (160, p.col[c].width * 5));
			if (p.col[c].money) nm++;
			else if (p.col[c].width <= 11) fixed += p.col[c].width * wk_fw () + 16;
			else { weight += p.col[c].width; textMin += minW[c]; }
		}
		int mw = nm ? iclamp ((avail - fixed - textMin) / nm, 92, 116) : 116;
		int rest = imax (100, avail - fixed - nm * mw);
		bool atMin[RMAXCOL] = { false };
		for (int pass = 0; pass < 2; pass++)
			for (int c = 0; c < p.ncol && c < RMAXCOL; c++)
			{
				if (p.col[c].money || p.col[c].width <= 11 || atMin[c]) continue;
				if (minW[c] > (int) ((long long) rest * p.col[c].width / imax (1, weight))) { atMin[c] = true; rest = imax (0, rest - minW[c]); weight -= p.col[c].width; }
			}
		for (int c = 0; c < p.ncol && c < RMAXCOL; c++)
		{
			int w = p.col[c].money ? mw : p.col[c].width <= 11 ? p.col[c].width * wk_fw () + 16
				: atMin[c] ? minW[c] : imax (minW[c], (int) ((long long) rest * p.col[c].width / imax (1, weight)));
			g->setColumn (c, p.col[c].title, w, p.col[c].align == 1 ? GRID_RIGHT : GRID_LEFT);
		}
		g->setRows (p.nr);
		g->top = 0; g->setSel (-1);
		invalidate (true);
	}
	void resizeTo (int w, int h) override { Page::resizeTo (w, h); if (g && p.ncol) build (); }
	static const char *c_text (DataGrid &gr, int row, int col, char *buf, int cap)
	{
		ReportsPage *rp = (ReportsPage *) gr.user;
		(void) buf; (void) cap;
		if (row >= rp->p.nr || col >= rp->p.ncol) return "";
		return rp->p.r[row].cell[col];
	}
	static bool c_draw (DataGrid &gr, Canvas &cv, int row, int col, int x, int y, int w, int h, unsigned ink, bool sel)
	{
		ReportsPage *rp = (ReportsPage *) gr.user;
		if (row >= rp->p.nr || col >= rp->p.ncol) return true;
		const RRow &r = rp->p.r[row];
		const RCol &c = rp->p.col[col];
		const char *s = r.cell[col];
		unsigned in = sel ? ink : C_FIELD_TEXT;
		switch (r.style)
		{
		case RS_BLANK: return true;
		case RS_HEAD:
		{
			if (col) return true;
			// (the heading across the row: a wider canvas from this cell on)
			int span = imin (gr.totalWidth () - gr.left, gr.width - WK_SBW - 6) - (x < 0 ? 0 : x);
			Canvas wide; wide.adopt (cv.px, imax (1, span), cv.h, cv.stride);
			if (!sel) wk_rbox (wide, 1, 2, span - 2, h - 3, 4, wk_mix (C_FIELD, C_ACCENT, 34), wk_mix (C_FIELD, C_ACCENT, 26));
			char t[200]; fit_text (s, span - 16, t, sizeof t, 2);
			wk_text_l (wide, x + 8, y, h, t, in, 2);
			return true;
		}
		}
		char dc[48];
		if (c.bal) { dc_text (s, dc, sizeof dc); s = dc; }
		bool red = !sel && c.money && !c.bal && s[0] == '-';
		switch (r.style)
		{
		case RS_TOTAL:
			if (!sel) cv.fillRect (0, y, cv.w, h, wk_mix (C_FIELD, C_ACCENT, 22));
			if (!sel && c.money && s[0]) { cv.fillRect (x + 8, y + 1, w - 16, 1, wk_mix (C_FIELD, C_FIELD_TEXT, 120)); cv.fillRect (x + 8, y + 3, w - 16, 1, wk_mix (C_FIELD, C_FIELD_TEXT, 120)); }
			cell_text (cv, x, y, w, h, s, red ? C_BAD : in, c.align == 1, 2);
			return true;
		case RS_SUB:
			if (!sel && c.money && s[0]) cv.fillRect (x + 8, y + 1, w - 16, 1, wk_mix (C_FIELD, C_FIELD_TEXT, 90));
			cell_text (cv, x, y, w, h, s, red ? C_BAD : in, c.align == 1, 2);
			return true;
		case RS_DIM:
			cell_text (cv, x, y, w, h, s, sel ? ink : field_dim (), c.align == 1);
			return true;
		}
		// a line: its indent (the rubrics'), the amounts at the right (a balance: D / C)
		if (col == 0 && r.indent) { cell_text (cv, x + r.indent * 16, y, w - r.indent * 16, h, s, in, false); return true; }
		if (c.bal) { cell_text (cv, x, y, w, h, s, in, true); return true; }	// (s: its D / C text already)
		cell_text (cv, x, y, w, h, s, !sel && c.money && s[0] == '-' ? C_BAD : in, c.align == 1);
		return true;
	}
	void openRow ()
	{
		int r = g->sel;
		if (r < 0 || r >= p.nr) return;
		const RRow &x = p.r[r];
		if (x.refKind == RR_ENTRY) open_entry (x.ref);
		else if (x.refKind == RR_ACCOUNT && x.ref >= 0 && x.ref < g_b.nacc) open_account (g_b.acc[x.ref].code);
		else if (x.refKind == RR_PARTY) open_party (x.ref);
	}
	// The VAT detail of a period (the VAT page's grid clicked).
	void showVat (int f, int t) { which->sel = R_VAT; which->invalidate (true); setPeriod (f, t); build (); }
	void enter () override { g->setFocus (); }
	void onDraw () override
	{
		drawHead ();
		if (usesFrom ()) { wk_text_l (canvas, 244, 70, ED_H, "From", C_TEXT); wk_text_l (canvas, 428, 70, ED_H, "to", C_TEXT); }
		else wk_text_l (canvas, 428 - 20, 70, ED_H, "At", C_TEXT);
		int x2 = 16 + quick->width + 16;
		if (kind == R_JOURNAL) wk_text_l (canvas, x2, 106, ED_H, "Journal", C_TEXT);
		else if (kind == R_LEDGER) { wk_text_l (canvas, x2, 106, ED_H, "Accounts", C_TEXT); wk_text_l (canvas, x2 + 226, 106, ED_H, "to", C_TEXT); }
		else if (kind == R_PARTY) wk_text_l (canvas, x2, 106, ED_H, "Party", C_TEXT);
	}
	static void on_param (Widget &) { if (g_rp) g_rp->build (); }
	static void on_date (Widget &w) { if (g_rp && date_parse (((LineEdit &) w).text ())) g_rp->build (); }
	static void on_quick (int q) { if (!g_rp) return; g_rp->quickPick (q); g_rp->build (); }
	static void on_open (Widget &) { if (g_rp) g_rp->openRow (); }
	static void s_writer () { if (g_rp && g_rp->p.nr) open_report (g_rp->p, XF_RTF); }
	static void s_sheet () { if (g_rp && g_rp->p.nr) open_report (g_rp->p, XF_XLSX); }
	static void s_export ()
	{
		if (!g_rp || !g_rp->p.nr) return;
		int x, y; abs_pos (g_rp->bExport, &x, &y);
		PopupMenu m (x, y + g_rp->bExport->height + 2);
		m.add ("Writer document (.rtf)...", 1);
		m.add ("Spreadsheet workbook (.xlsx)...", 2);
		m.add ("CSV file (;)...", 3);
		int r = m.run ();
		if (r >= 1) save_report (g_rp->p, r == 1 ? XF_RTF : r == 2 ? XF_XLSX : XF_CSV);
	}
};

// =====================================================================================================================================
// ---- VAT ---------------------------------------------------------------------------------------------------------------------------
// =====================================================================================================================================
class VatPage;
static VatPage *g_vp;
struct VatSection { const char *title; int grids[12]; };
static const VatSection VAT_FORM[5] = {
	{ "II  Operations (sales)", { 0, 1, 2, 3, 44, 45, 46, 47, 48, 49, -1 } },
	{ "III  Incoming (purchases)", { 81, 82, 83, 84, 85, 86, 87, 88, -1 } },
	{ "IV  VAT due", { 54, 55, 56, 57, 61, 63, -1 } },
	{ "V  VAT deductible", { 59, 62, 64, -1 } },
	{ "VI  Balance", { 71, 72, 91, -1 } } };

class VatPage : public Page
{
public:
	ChoiceBox *ybox; FlatButton *bXml, *bFile, *bDetail, *bLists;
	Checkbox *refund, *payForms; LineEdit *g91;
	const char *ynames[48]; char ybuf[48][8]; int years[48], ny;
	int year, sel; bool monthly;
	money grid[100]; int filed; char warns[16][96]; int nw;
	int hotGrid;
	VatPage () : Page (P_VAT), ybox (0), bXml (0), bFile (0), bDetail (0), bLists (0), refund (0), payForms (0), g91 (0), ny (0), year (0), sel (1), monthly (false), filed (-1), nw (0), hotGrid (-1)
	{
		resizeTo (800, 660);
		HeadRow h (this);
		bLists = h.add ("Listings", s_lists, FB_SECONDARY, NI_CUST, "The annual customer listing, the intra-community listing (XML)");
		bDetail = h.add ("Detail", s_detail, FB_SECONDARY, NI_REPORT, "The lines behind each grid");
		bFile = h.add ("Mark as filed", s_file, FB_SECONDARY, NI_LOCK, "The period marked filed: its VAT entries locked");
		bXml = h.add ("Intervat XML", s_xml, FB_PRIMARY, NI_EXPORT, "The return's file for Intervat (the SPF Finances' site)");
		ybox = new ChoiceBox (16, 70, 100); ybox->onChange = on_year; addChild (ybox);
		refund = new Checkbox (16, 0, 220, 24, "Ask for the refund", false, 0, C_BG); refund->anchor = ANCHOR_LEFT | ANCHOR_BOTTOM; addChild (refund);
		payForms = new Checkbox (250, 0, 220, 24, "Ask for payment forms", false, 0, C_BG); payForms->anchor = ANCHOR_LEFT | ANCHOR_BOTTOM; addChild (payForms);
		g91 = new LineEdit (0, 0, 120); g91->accept = accept_dec; g91->rightAlign = true; g91->hidden = true; g91->onChange = on_g91; addChild (g91);
		for (int i = 0; i < 100; i++) grid[i] = 0;
		placeBottom ();
	}
	void placeBottom () { refund->top = height - 36; payForms->top = height - 36; }
	void resizeTo (int w, int h) override { Page::resizeTo (w, h); if (refund) placeBottom (); }
	const char *title () override { return "VAT returns"; }
	void subtitle (char *out, int cap) override
	{
		if (g_b.vatRegime == VR_FRANCHISE) { scpy (out, "The small business franchise: no VAT return to file (the customer listing still is)", cap); return; }
		if (g_b.vatRegime == VR_NONE) { scpy (out, "Not subject to VAT: no return", cap); return; }
		scpy (out, monthly ? "Monthly returns" : "Quarterly returns", cap);	// (the VAT number: at the top of the side bar)
	}
	int periods () const { return monthly ? 12 : 4; }
	void refresh () override
	{
		monthly = g_b.vatPeriod == VP_MONTH;
		// the calendar years the books span
		ny = 0;
		int y0 = g_b.nyr ? y_of (g_b.yr[0].start) : y_of (today_ymd ()), y1 = g_b.nyr ? y_of (g_b.yr[g_b.nyr - 1].end) : y0;
		for (int y = y0; y <= y1 && ny < 48; y++) { itoa10 (y, ybuf[ny]); ynames[ny] = ybuf[ny]; years[ny] = y; ny++; }
		int want = year ? year : y_of (default_date ());
		if (m_year != g_yearVer) { m_year = g_yearVer; want = y_of (default_date ()); sel = 0; }
		ybox->setOptions (ynames, ny);
		int k = 0; for (int i = 0; i < ny; i++) if (years[i] == want) k = i;
		ybox->sel = k; year = ny ? years[k] : want;
		if (sel < 1 || sel > periods ())				// the period to look at: the first one not filed (whose end is past), else today's
		{
			sel = 1;
			int t = today_ymd ();
			for (int p = 1; p <= periods (); p++)
			{
				int f, e; period_range (year, p, monthly, &f, &e);
				sel = p;
				if (return_find (g_b, year, p, monthly) < 0 && (e < t || (f <= t && t <= e))) break;
			}
		}
		compute ();
	}
	unsigned m_year = 0;
	void compute ()
	{
		int f, t; period_range (year, sel, monthly, &f, &t);
		filed = return_find (g_b, year, sel, monthly);
		if (filed >= 0) for (int i = 0; i < 100; i++) grid[i] = g_b.ret[filed].grid[i];
		else
		{
			money keep91 = grid[91];
			vat_grids (g_b, f, t, grid);
			grid[91] = monthly && sel == 12 ? keep91 : 0;
		}
		nw = vat_checks (grid, warns, 16);
		g91->hidden = !(monthly && sel == 12);
		if (!g91->hidden) { char a[32]; edit_money (grid[91], a); if (!g91->hasFocus) g91->setText (a); }
		bFile->setText (filed >= 0 ? "Reopen" : "Mark as filed");
		bFile->tip = filed >= 0 ? "The period reopened: its entries can be changed again" : "The period marked filed: its VAT entries locked";
		refund->disabled = !grid[72]; if (!grid[72]) refund->checked = false;
		refund->invalidate (true);
		bool vat = g_b.vatRegime == VR_NORMAL;
		bXml->setDisabled (!vat); bFile->setDisabled (!vat);
		// (the header's buttons laid out again: their widths changed)
		int x = width - 16;
		FlatButton *bs[4] = { bLists, bDetail, bFile, bXml };
		for (int i = 0; i < 4; i++) { bs[i]->left = x - bs[i]->width; x = bs[i]->left - 8; }
		invalidate (true);
	}
	// The tiles: the periods of the year.
	void tileBox (int p, int *x, int *y, int *w, int *h)
	{
		int n = periods (), perRow = monthly ? 6 : 4, row = (p - 1) / perRow, col = (p - 1) % perRow;
		int x0 = 130, gap = 8, W = width - x0 - 16;
		*w = (W - (perRow - 1) * gap) / perRow; *h = monthly ? 40 : 56;
		*x = x0 + col * (*w + gap); *y = 66 + row * (*h + 6);
		(void) n;
	}
	int formTop () { return monthly ? 66 + 2 * 46 + 12 : 66 + 56 + 14; }
	void onDraw () override
	{
		drawHead ();
		if (g_b.vatRegime != VR_NORMAL) { wk_text_l (canvas, 20, 110, 24, "This company files no periodic VAT return (Settings > Company: its VAT situation).", C_TEXT); }
		int t = today_ymd ();
		char a[32], s[64];
		for (int p = 1; p <= periods (); p++)
		{
			int x, y, w, h; tileBox (p, &x, &y, &w, &h);
			int f, e; period_range (year, p, monthly, &f, &e);
			int fi = return_find (g_b, year, p, monthly);
			int due = return_deadline (year, p, monthly);
			bool on = p == sel;
			unsigned face = on ? wk_mix (C_FIELD, C_ACCENT, 60) : wk_tone (C_FIELD, 130);
			wk_rbox (canvas, x, y, w, h, 8, face, face);
			wk_rline (canvas, x, y, w, h, 8, on ? C_ACCENT : wk_mix (C_BG, 0, 60), on ? 255 : 110);
			char nm[32];
			if (monthly) scpy (nm, MONTH_SHORT[p - 1], sizeof nm); else { scpy (nm, "Q", sizeof nm); scat_num (nm, p, sizeof nm); }
			wk_text_l (canvas, x + 10, y + 4, 18, nm, C_FIELD_TEXT, 2);
			unsigned c; const char *st;
			if (fi >= 0) { st = "Filed"; c = C_GOOD; }
			else if (f > t) { st = "To come"; c = field_dim (); }
			else if (e >= t) { st = "Running"; c = C_BLUE; }
			else if (due < t) { st = "Late"; c = C_BAD; }
			else { st = "To file"; c = C_WARN; }
			int pw = pill_w (st);
			draw_pill (canvas, x + w - pw - 8, y + 5, 16, st, c);
			if (!monthly)
			{
				char d[16]; date_show (fi >= 0 ? g_b.ret[fi].filed : due, d);
				scpy (s, fi >= 0 ? "on " : "due ", sizeof s); scat (s, d, sizeof s);
				wk_text_l (canvas, x + 10, y + 30, 18, s, field_dim ());
			}
		}
		if (g_b.vatRegime != VR_NORMAL) return;
		// the form
		int y0 = formTop (), W = width - 32, cw = (W - 24) / 3, rh = 22;
		char pn[40]; period_name (year, sel, monthly, pn, sizeof pn);
		{
			int f, e; period_range (year, sel, monthly, &f, &e);
			char d1[16], d2[16]; date_show (f, d1); date_show (e, d2);
			scpy (s, pn, sizeof s);
			wk_text_l (canvas, 20, y0 - 2, 22, s, C_TEXT, 2);
			char r[120]; scpy (r, d1, sizeof r); scat (r, " - ", sizeof r); scat (r, d2, sizeof r);
			if (filed >= 0) { char fd[16]; date_show (g_b.ret[filed].filed, fd); scat (r, "  \xB7  filed on ", sizeof r); scat (r, fd, sizeof r); scat (r, " (the grids as filed)", sizeof r); }
			wk_text_l (canvas, 20 + wk_text_w (pn, 2) + 14, y0 - 2, 22, r, dim_ink (C_BG));
		}
		int fy = y0 + 24;
		int colY[3] = { fy, fy, fy };
		for (int sct = 0; sct < 5; sct++)
		{
			int col = sct < 2 ? sct : 2;
			int x = 16 + col * (cw + 12), y = colY[col];
			wk_text_l (canvas, x + 4, y, rh, VAT_FORM[sct].title, C_TEXT, 2);
			y += rh + 2;
			for (int k = 0; VAT_FORM[sct].grids[k] >= 0; k++)
			{
				int gnum = VAT_FORM[sct].grids[k];
				if (gnum == 91 && !(monthly && sel == 12)) continue;
				bool hot = gnum == hotGrid;
				unsigned bg = hot ? wk_mix (C_FIELD, C_ACCENT, 40) : C_FIELD;
				wk_rbox (canvas, x, y, cw, rh, 5, bg, bg);
				char gl[4]; grid_label (gnum, gl);
				unsigned pc = gnum == 71 ? C_BAD : gnum == 72 ? C_GOOD : C_ACCENT;
				draw_pill (canvas, x + 4, y + 3, rh - 6, gl, pc, grid[gnum] != 0);
				// its name: what the amount leaves of the row
				bool am = grid[gnum] || gnum == 71 || gnum == 72; int ast = gnum >= 71 && gnum <= 72 ? 2 : 0;
				if (am) money_s (grid[gnum], a);
				int aw = am ? wk_text_w (a, ast) + 12 : 8;
				Canvas c; c.adopt (canvas.px + y * canvas.stride + x, cw, rh, canvas.stride);
				text_fit_l (c, 38, 0, cw - 38 - aw, rh, grid_short (gnum), grid[gnum] ? C_FIELD_TEXT : field_dim ());
				if (am) text_r (canvas, x + cw - 8, y, rh, a, grid[gnum] ? C_FIELD_TEXT : field_dim (), ast);
				if (gnum == 91) { g91->left = x + cw - 128; g91->top = y - 2; }
				y += rh + 2;
			}
			if (sct == 2 || sct == 3)			// XX / YY
			{
				money v = sct == 2 ? grid_xx (grid) : grid_yy (grid);
				wk_text_l (canvas, x + 40, y, rh, sct == 2 ? "Total XX" : "Total YY", C_TEXT, 2);
				text_r (canvas, x + cw - 8, y, rh, money_s (v, a), C_TEXT, 2);
				y += rh + 8;
			}
			else y += 8;
			colY[col] = y;
		}
		// the checks
		int wy = imax (colY[0], colY[1]) + 6, wx = 20;
		if (!nw) { wk_glyph (canvas, WKG_CHECK, wx + 8, wy + 11, 12, C_GOOD); wk_text_l (canvas, wx + 20, wy, 22, "The checks Intervat makes find nothing wrong.", C_GOOD); }
		for (int i = 0; i < nw && wy < height - 64; i++)
		{
			wk_text_c (canvas, wx, wy, 16, 22, "!", C_WARN, 2);
			Canvas c; c.adopt (canvas.px + wy * canvas.stride + wx + 20, imax (1, 2 * cw), 22, canvas.stride);
			text_fit_l (c, 0, 0, 2 * cw, 22, warns[i], C_TEXT);
			wy += 22;
		}
		if (!g91->hidden) {}
	}
	int gridAt (int mx, int my)
	{
		if (g_b.vatRegime != VR_NORMAL) return -1;
		int y0 = formTop (), W = width - 32, cw = (W - 24) / 3, rh = 22, fy = y0 + 24;
		int colY[3] = { fy, fy, fy };
		for (int sct = 0; sct < 5; sct++)
		{
			int col = sct < 2 ? sct : 2, x = 16 + col * (cw + 12), y = colY[col] + rh + 2;
			for (int k = 0; VAT_FORM[sct].grids[k] >= 0; k++)
			{
				int gnum = VAT_FORM[sct].grids[k];
				if (gnum == 91 && !(monthly && sel == 12)) continue;
				if (mx >= x && mx < x + cw && my >= y && my < y + rh) return gnum;
				y += rh + 2;
			}
			y += (sct == 2 || sct == 3) ? rh + 8 : 8;
			colY[col] = y;
		}
		return -1;
	}
	bool onMouse (int mx, int my, int bl, int, int, int wheel) override
	{
		if (wheel) return false;
		int hg = gridAt (mx, my);
		if (hg != hotGrid) { hotGrid = hg; invalidate (true); }
		if (bl && !pressed)
		{
			pressed = true;
			for (int p = 1; p <= periods (); p++)
			{
				int x, y, w, h; tileBox (p, &x, &y, &w, &h);
				if (mx >= x && mx < x + w && my >= y && my < y + h) { sel = p; compute (); return true; }
			}
			if (hg >= 0 && hg != 91) s_detail ();
		}
		else if (!bl) pressed = false;
		return true;
	}
	bool onKey (long k) override
	{
		if (k == KEY_LEFT && sel > 1) { sel--; compute (); return true; }
		if (k == KEY_RIGHT && sel < periods ()) { sel++; compute (); return true; }
		return false;
	}
	void xml ()
	{
		if (grid[91] && !(monthly && sel == 12)) grid[91] = 0;
		if (nw && ask ("Intervat", "The checks found something to look at (below the form). Make the file anyway?", MB_YESNO, 2) != 1) return;
		bool last = sel == periods ();
		bool nihil = false;
		if (last)
		{
			ListRow r[4]; int n = client_listing (g_b, year, r, 4);
			nihil = n == 0;
		}
		Out o; vat_return_xml (g_b, year, sel, monthly, grid, nihil, refund->checked && grid[72], payForms->checked, "", o);
		char name[64] = "VAT return "; char ref[16]; period_ref (year, sel, monthly, ref); scat (name, ref, sizeof name); scat (name, ".xml", sizeof name);
		kapi_mkdir ("SD:/docs"); kapi_mkdir ("SD:/docs/VAT");
		char path[220];
		if (!wk_file_save (path, sizeof path, "SD:/docs/VAT", name)) return;
		if (kapi_save_file (path, o.b, (unsigned) o.n) < 0) { warn ("Intervat", "The file could not be written."); return; }
		char m[240] = "Written: "; scat (m, path, sizeof m); status (m);
		if (filed < 0 && ask ("Intervat", "The file is written: send it on Intervat (intervat.minfin.fgov.be). Mark the period as filed now (its VAT entries then locked)?", MB_YESNO, 1) == 1) fileIt ();
	}
	void fileIt ()
	{
		if (filed >= 0)
		{
			if (ask ("VAT", "Reopen this period? Its entries can then be changed again (a return filed again must then be corrected on Intervat).", MB_YESNO, 2) != 1) return;
			for (int k = filed + 1; k < g_b.nret; k++) g_b.ret[k - 1] = g_b.ret[k];
			g_b.nret--;
			changed ();
			status ("Period reopened");
			return;
		}
		VatReturn &r = return_add (g_b);
		r.year = year; r.period = sel; r.monthly = monthly; r.filed = today_ymd ();
		for (int i = 0; i < 100; i++) r.grid[i] = grid[i];
		// its settlement: the VAT due and deductible moved to the VAT current account (vat.h)
		Entry e; bool settled = false;
		if (vat_settlement (g_b, year, sel, monthly, e))
		{
			if (ask ("VAT", "Post the period's settlement too? Its VAT due (451000) and deductible (411000) moved to the VAT current account "
				 "-- 451200 what is paid to the State, 411200 what it refunds -- by a miscellaneous operation on the period's last day.", MB_YESNO, 1) == 1)
			{ entry_save (g_b, e); settled = true; }
			else entry_free (e);
		}
		changed ();
		status (settled ? "Period marked filed, its settlement posted: its VAT entries are locked" : "Period marked filed: its VAT entries are locked");
	}
	void lists ()
	{
		int x, y; abs_pos (bLists, &x, &y);
		PopupMenu m (x, y + bLists->height + 2);
		char a[64] = "Customer listing "; scat_num (a, year, sizeof a); scat (a, " (XML)...", sizeof a);
		char pn[40]; period_name (year, sel, monthly, pn, sizeof pn);
		char b[80] = "Intra-community listing "; scat (b, pn, sizeof b); scat (b, " (XML)...", sizeof b);
		m.add (a, 1); m.add (b, 2);
		int r = m.run ();
		if (r == 1)
		{
			ListRow rows[512]; int n = client_listing (g_b, year, rows, 512);
			if (!n) { warn ("Customer listing", "No Belgian VAT-registered customer reached 250 EUR this year: the listing is nil (say so in the year's last return: its file does it)."); return; }
			Out o; client_listing_xml (g_b, year, rows, n, o);
			char name[64] = "Customer listing "; scat_num (name, year, sizeof name); scat (name, ".xml", sizeof name);
			kapi_mkdir ("SD:/docs/VAT");
			char path[220];
			if (!wk_file_save (path, sizeof path, "SD:/docs/VAT", name)) return;
			if (kapi_save_file (path, o.b, (unsigned) o.n) < 0) { warn ("Customer listing", "The file could not be written."); return; }
			char m2[240] = "Written: "; scat (m2, path, sizeof m2); scat (m2, " (", sizeof m2); scat_num (m2, n, sizeof m2); scat (m2, n == 1 ? " customer)" : " customers)", sizeof m2); status (m2);
		}
		else if (r == 2)
		{
			int f, t; period_range (year, sel, monthly, &f, &t);
			ListRow rows[512]; int bad = 0, n = intra_listing (g_b, f, t, rows, 512, &bad);
			if (!n) { warn ("Intra-community listing", bad ? "Intra-community sales, but their customers' VAT numbers are not valid EU ones: correct their cards." : "No intra-community supply in this period: no listing to file."); return; }
			if (bad && ask ("Intra-community listing", "Some customers have no valid EU VAT number: they are left out. Make the file anyway?", MB_YESNO, 2) != 1) return;
			Out o; intra_listing_xml (g_b, year, sel, monthly, rows, n, o);
			char ref[16]; period_ref (year, sel, monthly, ref);
			char name[64] = "Intra-community listing "; scat (name, ref, sizeof name); scat (name, ".xml", sizeof name);
			kapi_mkdir ("SD:/docs/VAT");
			char path[220];
			if (!wk_file_save (path, sizeof path, "SD:/docs/VAT", name)) return;
			if (kapi_save_file (path, o.b, (unsigned) o.n) < 0) { warn ("Intra-community listing", "The file could not be written."); return; }
			char m2[240] = "Written: "; scat (m2, path, sizeof m2); status (m2);
		}
	}
	void detail ()
	{
		int f, t; period_range (year, sel, monthly, &f, &t);
		go (P_REPORTS);
		if (g_rp) g_rp->showVat (f, t);
	}
	static void on_year (Widget &) { if (!g_vp) return; g_vp->year = g_vp->years[iclamp (g_vp->ybox->sel, 0, g_vp->ny - 1)]; g_vp->sel = 0; g_vp->refresh (); }
	static void on_g91 (Widget &) { if (!g_vp) return; money m; if (parse_money (g_vp->g91->text (), &m)) { g_vp->grid[91] = m; g_vp->invalidate (true); } }
	static void s_xml () { if (g_vp) g_vp->xml (); }
	static void s_file () { if (g_vp) g_vp->fileIt (); }
	static void s_detail () { if (g_vp) g_vp->detail (); }
	static void s_lists () { if (g_vp) g_vp->lists (); }
};

// =====================================================================================================================================
// ---- settings -------------------------------------------------------------------------------------------------------------------
// =====================================================================================================================================
class JournalDialog : public FormBox
{
public:
	LineEdit *code, *name, *iban; ChoiceBox *type; PickEdit *acc; Checkbox *hide; int ji;
	JournalDialog (int j) : FormBox (j < 0 ? "New journal" : "Journal", 560, 0, 110), ji (j)
	{
		const Journal *src = j >= 0 ? &g_b.jr[j] : 0;
		int y = y0;
		code = edit (y, "Code", 80, src ? src->code : ""); code->placeholder = "BNK2"; y += 34;
		name = edit (y, "Name", 330, src ? src->name : ""); y += 34;
		label (y, "Kind"); type = new ChoiceBox (fieldX, y, 200); type->setOptions (JT_NAME, JT_COUNT); type->sel = src ? (int) src->type : (int) JT_BANK; addChild (type);
		if (src) { bool used = false; for (int i = 0; i < g_b.ne && !used; i++) used = g_b.e[i].journal == j; type->disabled = used; }
		y += 34;
		label (y, "Account"); acc = new PickEdit (fieldX, y, 330, SK_ACCOUNT, AF_FIN, "55"); acc->setValue (src ? src->account : "");
		acc->placeholder = "(a bank or cash journal) 550000..."; addChild (acc); y += 34;
		iban = edit (y, "IBAN", 260, ""); if (src) { char ib[48]; iban_show (src->iban, ib, sizeof ib); iban->setText (ib); } y += 34;
		hide = new Checkbox (fieldX, y, 300, 26, "Hidden (no longer offered)", src && src->hidden, 0, C_FACE); addChild (hide); y += 44;
		resizeTo (width, y + 50);
		Root *r = Root::current ();
		if (r) { left = (r->width - width) / 2; top = imax (0, (r->height - height) / 2); }
		buttons (j < 0 ? "Add" : "OK");
		code->setFocus ();
	}
	bool validate () override
	{
		char c[8]; trim_copy (c, code->text (), sizeof c);
		for (char *q = c; *q; q++) *q = up (*q);
		if (!c[0] || slen (c) > 5) { warn ("Journal", "A code of 1 to 5 letters or digits."); code->setFocus (); return false; }
		int o = jrn_find (g_b, c);
		if (o >= 0 && o != ji) { warn ("Journal", "Another journal has this code."); code->setFocus (); return false; }
		char n[40]; trim_copy (n, name->text (), sizeof n);
		if (!n[0]) { warn ("Journal", "Type its name."); name->setFocus (); return false; }
		if (jt_fin (type->sel))
		{
			acc->resolve ();
			if (!acc_postable (g_b, acc->value ())) { warn ("Journal", "A bank or cash journal needs its account (55..., 57...)."); acc->setFocus (); return false; }
		}
		if (ji < 0 && g_b.njr >= MAXJOURNALS) { warn ("Journal", "No more journals can be added."); return false; }
		return true;
	}
};

class NewCompanyDialog : public FormBox
{
public:
	LineEdit *name, *vat, *street, *zip, *city, *email, *phone, *iban; DateEdit *start, *end_; ChoiceBox *lang, *regime, *period;
	NewCompanyDialog () : FormBox ("New company", 640, 0, 140)
	{
		int y = y0;
		name = edit (y, "Company", 440, ""); name->placeholder = "Its name, its legal form (SRL, SA...)"; y += 34;
		vat = edit (y, "VAT number", 180, ""); vat->placeholder = "BE 0123.456.789"; y += 34;
		street = edit (y, "Street", 440, ""); y += 34;
		zip = edit (y, "Postcode", 80, ""); label (y, "City", 250); city = new LineEdit (300, y, 280); addChild (city); y += 34;
		email = edit (y, "E-mail", 230, ""); label (y, "Phone", 402); phone = new LineEdit (450, y, 130); addChild (phone); y += 34;
		iban = edit (y, "Bank account", 260, ""); iban->placeholder = "BE68 5390 0754 7034"; y += 42;
		static const char *const LANG[2] = { "Fran\xE7" "ais (PCMN)", "Nederlands (MAR)" };
		label (y, "Chart of accounts"); lang = new ChoiceBox (fieldX, y, 200); lang->setOptions (LANG, 2); addChild (lang); y += 34;
		static const char *const REG[3] = { "Files VAT returns", "Small business franchise", "Not subject to VAT" };
		label (y, "VAT"); regime = new ChoiceBox (fieldX, y, 220); regime->setOptions (REG, 3); addChild (regime);
		static const char *const PER[2] = { "Quarterly", "Monthly" };
		period = new ChoiceBox (fieldX + 230, y, 130); period->setOptions (PER, 2); addChild (period); y += 34;
		int yy = y_of (today_ymd ());
		label (y, "First fiscal year"); start = new DateEdit (fieldX, y, 140); addChild (start);
		label (y, "to", fieldX + 150); end_ = new DateEdit (fieldX + 176, y, 140); addChild (end_);
		char d[16]; date_show (ymd (yy, 1, 1), d); start->setText (d); date_show (ymd (yy, 12, 31), d); end_->setText (d);
		y += 44;
		label (y, "The chart (PCMN) and the journals (sales, purchases, bank, cash,", -1, true);
		label (y + 20, "miscellaneous operations) are made: all can be changed afterwards.", -1, true);
		y += 56;
		resizeTo (width, y + 50);
		Root *r = Root::current ();
		if (r) { left = (r->width - width) / 2; top = imax (0, (r->height - height) / 2); }
		buttons ("Create");
		name->setFocus ();
	}
	bool validate () override
	{
		char n[NAME_MAX]; trim_copy (n, name->text (), sizeof n);
		if (!n[0]) { warn ("New company", "Type the company's name."); name->setFocus (); return false; }
		char v[24]; vat_normalize (vat->text (), v, sizeof v);
		if (v[0] && vat_check (v) && ask ("New company", "The VAT number is not a valid one. Keep it anyway?", MB_YESNO, 2) != 1) { vat->setFocus (); return false; }
		int s = date_parse (start->text ()), e = date_parse (end_->text ());
		if (!s || !e || e <= s) { warn ("New company", "Type the first fiscal year's first and last days."); start->setFocus (); return false; }
		if (days_between (s, e) > 730) { warn ("New company", "A fiscal year lasts 24 months at most."); end_->setFocus (); return false; }
		return true;
	}
	// The books made from the answers.
	void make (Book &b)
	{
		int s = date_parse (start->text ()), e = date_parse (end_->text ());
		book_new (b, lang->sel == 1 ? "nl" : "fr", s, e);
		trim_copy (b.name, name->text (), NAME_MAX);
		vat_normalize (vat->text (), b.vat, sizeof b.vat);
		scpy (b.street, street->text (), NAME_MAX); scpy (b.zip, zip->text (), sizeof b.zip); scpy (b.city, city->text (), sizeof b.city);
		scpy (b.email, email->text (), sizeof b.email); scpy (b.phone, phone->text (), sizeof b.phone);
		iban_normalize (iban->text (), b.iban, sizeof b.iban);
		if (b.iban[0] && b.njr > 2) scpy (b.jr[2].iban, b.iban, sizeof b.jr[2].iban);
		b.vatRegime = (unsigned char) (regime->sel == 1 ? VR_FRANCHISE : regime->sel == 2 ? VR_NONE : VR_NORMAL);
		b.vatPeriod = (unsigned char) (period->sel == 1 ? VP_MONTH : VP_QUARTER);
	}
};

// A part of a page: its background, what it draws (its labels).
class Pane : public Widget
{
public:
	void (*paint) (Pane &);
	Pane (int l, int t, int w, int h, void (*p) (Pane &)) : Widget (l, t, w, h), paint (p) {}
	unsigned bgColor () override { return C_BG; }
	void onDraw () override { canvas.clear (C_BG); if (paint) paint (*this); }
};

class SettingsPage;
static SettingsPage *g_sp;
class SettingsPage : public Page
{
public:
	Segmented *part;
	Widget *pan[5];
	// the company
	LineEdit *name, *legal, *street, *zip, *city, *country, *vat, *email, *phone, *iban, *bic, *reg, *web; ChoiceBox *regime, *period;
	char vatMsg[48]; unsigned vatCol;
	// the years, the journals
	DataGrid *years, *journals;
	// the accounts by role
	PickEdit *role[7];
	// the printing's templates
	Segmented *tplLang; DataGrid *tpl;
	SettingsPage () : Page (P_SETTINGS), vatCol (0)
	{
		vatMsg[0] = '\0';
		resizeTo (800, 660);
		static const char *const PARTS[5] = { "Company", "Fiscal years", "Journals", "Accounts", "Printing" };
		part = new Segmented (16, 70, PARTS, 5, on_part); addChild (part);
		static void (*const PAINT[5]) (Pane &) = { paint_company, paint_years, 0, paint_roles, paint_printing };
		for (int i = 0; i < 5; i++) { pan[i] = new Pane (0, 108, width, height - 108, PAINT[i]); pan[i]->anchor = ANCHOR_FILL; pan[i]->hidden = i != 0; addChild (pan[i]); }
		// the company
		Widget *c = pan[0];
		int x1 = 140, x2 = 540, y = 12;
		auto ed = [&] (int x, int yy, int w) { LineEdit *e = new LineEdit (x, yy, w); c->addChild (e); return e; };
		name = ed (x1, y, 260); legal = ed (x2, y, 120); legal->placeholder = "SRL, SA, SC..."; y += 34;
		street = ed (x1, y, 260); vat = ed (x2, y, 160); vat->onChange = on_vat; y += 34;
		zip = ed (x1, y, 70); city = ed (x1 + 80, y, 180); email = ed (x2, y, 236); y += 34;
		country = ed (x1, y, 40); phone = ed (x2, y, 150); y += 34;
		iban = ed (x1, y, 260); bic = ed (x2, y, 120); y += 34;
		reg = ed (x1, y, 260); reg->placeholder = "RPM Bruxelles, RPR Gent..."; web = ed (x2, y, 236); web->placeholder = "www.example.be"; y += 46;
		static const char *const REG[3] = { "Files VAT returns", "Small business franchise", "Not subject to VAT (art. 44)" };
		regime = new ChoiceBox (x1, y, 260); regime->setOptions (REG, 3); c->addChild (regime);
		static const char *const PER[2] = { "Quarterly", "Monthly" };
		period = new ChoiceBox (x2, y, 150); period->setOptions (PER, 2); c->addChild (period); y += 50;
		FlatButton *b = new FlatButton ("Save the changes", s_saveCompany, FB_PRIMARY, NI_CHECK); b->left = x1; b->top = y; c->addChild (b);
		// the fiscal years
		Widget *yp = pan[1];
		years = new DataGrid (16, 12, 560, 240); years->sortable = false; yp->addChild (years);
		years->setColumns (5);
		years->setColumn (0, "Year", 110); years->setColumn (1, "From", 100); years->setColumn (2, "To", 100); years->setColumn (3, "State", 100); years->setColumn (4, "Result", 130, GRID_RIGHT);
		years->cellText = y_text; years->cellDraw = y_draw;
		int by = 12;
		auto yb = [&] (const char *s, void (*cb) (), int kind, int icon) { FlatButton *f = new FlatButton (s, cb, kind, icon); f->left = 592; f->top = by; by += 40; yp->addChild (f); return f; };
		yb ("Add the next year", s_addYear, FB_PRIMARY, NI_PLUS);
		yb ("Close the year...", s_closeYear, FB_SECONDARY, NI_LOCK);
		yb ("Reopen the year", s_reopenYear, FB_QUIET, -1);
		// the journals
		Widget *jp = pan[2];
		journals = new DataGrid (16, 12, 760, 260); journals->sortable = false; jp->addChild (journals);
		journals->setColumns (5);
		journals->setColumn (0, "Code", 70); journals->setColumn (1, "Name", 230); journals->setColumn (2, "Kind", 140); journals->setColumn (3, "Account", 90); journals->setColumn (4, "IBAN", 200);
		journals->cellText = j_text; journals->onActivate = on_jedit;
		{	// (the buttons below the list: its columns the room they need)
			FlatButton *f = new FlatButton ("New journal...", s_newJournal, FB_PRIMARY, NI_PLUS); f->left = 16; f->top = 12 + 260 + 12; jp->addChild (f);
			FlatButton *e = new FlatButton ("Edit...", s_editJournal, FB_SECONDARY, NI_EDIT); e->left = f->left + f->width + 8; e->top = f->top; jp->addChild (e);
		}
		// the accounts by role
		Widget *ap = pan[3];
		for (int i = 0; i < 7; i++) { role[i] = new PickEdit (250, 12 + i * 34, 330, SK_ACCOUNT, AF_ALL); ap->addChild (role[i]); }
		FlatButton *sb = new FlatButton ("Save the changes", s_saveRoles, FB_PRIMARY, NI_CHECK); sb->left = 250; sb->top = 12 + 7 * 34 + 14; ap->addChild (sb);
		// the printing's templates: a language's, a kind a row
		Widget *pp = pan[4];
		static const char *const LANGS[3] = { "French", "Dutch", "English" };
		tplLang = new Segmented (16, 12, LANGS, 3, on_tplLang); pp->addChild (tplLang);
		tpl = new DataGrid (16, 52, 560, 7 * 26 + 30); tpl->sortable = false; pp->addChild (tpl);
		tpl->setColumns (3);
		tpl->setColumn (0, "Document", 140); tpl->setColumn (1, "Its template", 290); tpl->setColumn (2, "", 110);
		tpl->cellText = t_text; tpl->cellDraw = t_draw; tpl->onActivate = on_tplEdit; tpl->setRows (PK_KINDS); tpl->setSel (0);
		by = 52;
		{ FlatButton *f = new FlatButton ("Edit in Writer", s_tplEdit, FB_PRIMARY, NI_EDIT); f->left = 592; f->top = by; pp->addChild (f); by += 40; }
		{ FlatButton *f = new FlatButton ("Open the folder", s_tplFolder, FB_SECONDARY, -1); f->left = 592; f->top = by; pp->addChild (f); }
	}
	const char *title () override { return "Settings"; }
	void subtitle (char *out, int cap) override { scpy (out, g_path[0] ? g_path : "(no file)", cap); }
	void fillCompany ()
	{
		name->setText (g_b.name); legal->setText (g_b.legal); street->setText (g_b.street); zip->setText (g_b.zip); city->setText (g_b.city);
		country->setText (g_b.country);
		char v[24]; vat_show (g_b.vat, v, sizeof v); vat->setText (v);
		email->setText (g_b.email); phone->setText (g_b.phone);
		char ib[48]; iban_show (g_b.iban, ib, sizeof ib); iban->setText (ib); bic->setText (g_b.bic);
		reg->setText (g_b.reg); web->setText (g_b.web);
		regime->sel = g_b.vatRegime == VR_FRANCHISE ? 1 : g_b.vatRegime == VR_NONE ? 2 : 0; regime->invalidate (true);
		period->sel = g_b.vatPeriod == VP_MONTH ? 1 : 0; period->invalidate (true);
		checkVat ();
	}
	void fillRoles ()
	{
		const char *v[7] = { g_b.accCustomers, g_b.accSuppliers, g_b.accVatDue, g_b.accVatDeduct, g_b.accProfit, g_b.accLoss, g_b.accSuspense };
		for (int i = 0; i < 7; i++) role[i]->setValue (v[i]);
	}
	void refresh () override
	{
		fillCompany (); fillRoles ();
		years->setRows (g_b.nyr); years->setSel (g_year);
		journals->setRows (g_b.njr);
		invalidate (true);
	}
	void checkVat ()
	{
		char v[24]; vat_normalize (vat->text (), v, sizeof v);
		vatMsg[0] = '\0';
		if (v[0]) { int c = vat_check (v); scpy (vatMsg, !c ? "Valid" : c == 2 ? "Wrong check digits" : "Not a VAT number", sizeof vatMsg); vatCol = !c ? C_GOOD : C_BAD; }
		pan[0]->invalidate (true);
	}
	void saveCompany ()
	{
		char n[NAME_MAX]; trim_copy (n, name->text (), sizeof n);
		if (!n[0]) { warn ("Settings", "Type the company's name."); name->setFocus (); return; }
		char v[24]; vat_normalize (vat->text (), v, sizeof v);
		if (v[0] && vat_check (v) && ask ("Settings", "The VAT number is not a valid one. Keep it anyway?", MB_YESNO, 2) != 1) return;
		char ib[40]; iban_normalize (iban->text (), ib, sizeof ib);
		if (ib[0] && !iban_ok (ib) && ask ("Settings", "The IBAN is not a valid one. Keep it anyway?", MB_YESNO, 2) != 1) return;
		scpy (g_b.name, n, NAME_MAX); scpy (g_b.legal, legal->text (), sizeof g_b.legal); scpy (g_b.street, street->text (), NAME_MAX);
		scpy (g_b.zip, zip->text (), sizeof g_b.zip); scpy (g_b.city, city->text (), sizeof g_b.city);
		char cc[4]; trim_copy (cc, country->text (), sizeof cc); for (char *q = cc; *q; q++) *q = up (*q); scpy (g_b.country, cc[0] ? cc : "BE", sizeof g_b.country);
		scpy (g_b.vat, v, sizeof g_b.vat); scpy (g_b.email, email->text (), sizeof g_b.email); scpy (g_b.phone, phone->text (), sizeof g_b.phone);
		scpy (g_b.iban, ib, sizeof g_b.iban);
		char bc[16]; iban_normalize (bic->text (), bc, sizeof bc); scpy (g_b.bic, bc, sizeof g_b.bic);
		trim_copy (g_b.reg, reg->text (), sizeof g_b.reg); trim_copy (g_b.web, web->text (), sizeof g_b.web);
		g_b.vatRegime = (unsigned char) (regime->sel == 1 ? VR_FRANCHISE : regime->sel == 2 ? VR_NONE : VR_NORMAL);
		g_b.vatPeriod = (unsigned char) (period->sel == 1 ? VP_MONTH : VP_QUARTER);
		changed ();
		status ("The company's details saved");
	}
	void saveRoles ()
	{
		char *d[7] = { g_b.accCustomers, g_b.accSuppliers, g_b.accVatDue, g_b.accVatDeduct, g_b.accProfit, g_b.accLoss, g_b.accSuspense };
		for (int i = 0; i < 7; i++)
		{
			role[i]->resolve ();
			if (!acc_postable (g_b, role[i]->value ())) { warn ("Settings", "Each role needs an account of the chart."); role[i]->setFocus (); return; }
		}
		for (int i = 0; i < 7; i++) scpy (d[i], role[i]->value (), CODE_MAX);
		changed ();
		status ("The accounts by role saved");
	}
	static const char *y_text (DataGrid &, int row, int col, char *buf, int cap)
	{
		if (row >= g_b.nyr) return "";
		const Year &y = g_b.yr[row];
		switch (col)
		{
		case 0: year_label (y, buf, cap); return buf;
		case 1: date_show (y.start, buf); return buf;
		case 2: date_show (y.end, buf); return buf;
		case 3: return y.closed ? "Closed" : "Open";
		}
		return "";
	}
	static bool y_draw (DataGrid &, Canvas &cv, int row, int col, int x, int y, int w, int h, unsigned ink, bool sel)
	{
		if (row >= g_b.nyr) return false;
		if (col == 3) { draw_pill (cv, x + 6, y + (h - 18) / 2, 18, g_b.yr[row].closed ? "Closed" : "Open", g_b.yr[row].closed ? field_dim () : C_GOOD, sel); return true; }
		if (col == 4) { cell_money (cv, x, y, w, h, year_result (g_b, row), ink, sel, false); return true; }
		return false;
	}
	static const char *j_text (DataGrid &, int row, int col, char *buf, int cap)
	{
		if (row >= g_b.njr) return "";
		const Journal &j = g_b.jr[row];
		switch (col)
		{
		case 0: return j.code;
		case 1: if (j.hidden) { scpy (buf, j.name, cap); scat (buf, " (hidden)", cap); return buf; } return j.name;
		case 2: return JT_NAME[j.type < JT_COUNT ? (int) j.type : (int) JT_MISC];
		case 3: return j.account;
		case 4: iban_show (j.iban, buf, cap); return buf;
		}
		return "";
	}
	void editJournal (int j)
	{
		JournalDialog d (j);
		if (d.run () != 1) return;
		char c[8]; trim_copy (c, d.code->text (), sizeof c); for (char *q = c; *q; q++) *q = up (*q);
		char n[40]; trim_copy (n, d.name->text (), sizeof n);
		char ib[40]; iban_normalize (d.iban->text (), ib, sizeof ib);
		int t = iclamp (d.type->sel, 0, JT_COUNT - 1);
		const char *acc = jt_fin (t) ? d.acc->value () : "";
		if (j < 0) { Journal &x = jrn_add (g_b, c, n, t, acc, ib); x.hidden = d.hide->checked; }
		else { Journal &x = g_b.jr[j]; scpy (x.code, c, 6); scpy (x.name, n, 40); x.type = (unsigned char) t; scpy (x.account, acc, CODE_MAX); scpy (x.iban, ib, 36); x.hidden = d.hide->checked; }
		changed ();
		status (j < 0 ? "Journal added" : "Journal saved");
	}
	void addYear ()
	{
		int y = year_add_next (g_b);
		if (y < 0) { warn ("Fiscal years", "No year can be added."); return; }
		char l[16], m[80] = "Fiscal year "; year_label (g_b.yr[y], l, sizeof l); scat (m, l, sizeof m); scat (m, " added", sizeof m);
		changed (); status (m);
	}
	void closeYear ()
	{
		int y = years->sel;
		if (y < 0 || y >= g_b.nyr) return;
		if (g_b.yr[y].closed) { status ("That year is closed already"); return; }
		money r = year_result (g_b, y) - year_appropriated (g_b, y);
		char a[32], l[16], m[400];
		year_label (g_b.yr[y], l, sizeof l);
		scpy (m, "Close the fiscal year ", sizeof m); scat (m, l, sizeof m); scat (m, "?\n", sizeof m);
		if (r > 0) { scat (m, "Its profit of ", sizeof m); scat (m, money_s (r, a), sizeof m); scat (m, " is carried forward (693000 / ", sizeof m); scat (m, g_b.accProfit, sizeof m); scat (m, ") by an entry on its last day.", sizeof m); }
		else if (r < 0) { scat (m, "Its loss of ", sizeof m); scat (m, money_s (-r, a), sizeof m); scat (m, " is carried forward (", sizeof m); scat (m, g_b.accLoss, sizeof m); scat (m, " / 793000) by an entry on its last day.", sizeof m); }
		else scat (m, "Its result is appropriated already.", sizeof m);
		scat (m, " Its entries are then locked (a year can be reopened).", sizeof m);
		if (y + 1 >= g_b.nyr) scat (m, " The next year is added.", sizeof m);
		if (ask ("Close the year", m, MB_YESNO, 1) != 1) return;
		if (y + 1 >= g_b.nyr) year_add_next (g_b);
		year_appropriate (g_b, y);
		g_b.yr[y].closed = true;
		changed ();
		status ("Year closed");
	}
	void reopenYear ()
	{
		int y = years->sel;
		if (y < 0 || y >= g_b.nyr || !g_b.yr[y].closed) return;
		if (ask ("Reopen the year", "Reopen this fiscal year? Its entries can then be changed (its appropriation entry stays: delete it if the result changes).", MB_YESNO, 2) != 1) return;
		g_b.yr[y].closed = false;
		changed ();
		status ("Year reopened");
	}
	static void paint_company (Pane &p)
	{
		int x1 = 20, x2 = 430, y = 12;
		const char *L[6][2] = { { "Name", "Legal form" }, { "Street", "VAT number" }, { "City", "E-mail" }, { "Country", "Phone" }, { "IBAN", "BIC" },
					{ "Register", "Web site" } };
		for (int i = 0; i < 6; i++) { wk_text_l (p.canvas, x1, y + i * 34, ED_H, L[i][0], C_TEXT); wk_text_l (p.canvas, x2, y + i * 34, ED_H, L[i][1], C_TEXT); }
		if (g_sp && g_sp->vatMsg[0]) wk_text_l (p.canvas, 540 + 170, y + 34, ED_H, g_sp->vatMsg, g_sp->vatCol);
		int yv = y + 6 * 34 + 12;
		wk_text_l (p.canvas, x1, yv, ED_H, "VAT situation", C_TEXT); wk_text_l (p.canvas, x2, yv, ED_H, "Returns", C_TEXT);
	}
	static void paint_years (Pane &p)
	{
		int y = 12 + 250;
		wk_text_l (p.canvas, 20, y, 20, "Closing a year carries its result forward (a profit: 693000 / 140000; a loss: 141000 / 793000)", dim_ink (C_BG));
		wk_text_l (p.canvas, 20, y + 20, 20, "by an entry in the miscellaneous journal on its last day, and locks its entries.", dim_ink (C_BG));
		wk_text_l (p.canvas, 20, y + 40, 20, "The balance sheet's accounts go on from year to year: no opening entry is needed.", dim_ink (C_BG));
	}
	static void paint_printing (Pane &p)
	{
		int y = 52 + 7 * 26 + 30 + 14;
		static const char *const L[] = {
			"A document is printed by Writer from its template, a Writer document (.rtf, .docx, .odt)",
			"whose merge fields Ledger fills: \xAB" "Number\xBB, \xAB" "Date\xBB, \xAB" "PartyName\xBB, \xAB" "PartyAddress\xBB, \xAB" "Total\xBB...",
			"A table's row holding \xAB" "LineText\xBB, \xAB" "LineQty\xBB, \xAB" "LinePrice\xBB, \xAB" "LineTotal\xBB is repeated for each line.",
			"In Writer, Tools > Mail Merge lists all the fields (their sample: templates/fields.card).",
			"A party's documents take its language (its card), else the company's (its chart's).",
			"The documents made go to SD:/docs/Quotes, Orders, Delivery notes, Invoices..." };
		for (unsigned i = 0; i < sizeof L / sizeof L[0]; i++) wk_text_l (p.canvas, 20, y + (int) i * 20, 20, L[i], dim_ink (C_BG));
	}
	static void paint_roles (Pane &p)
	{
		static const char *const R[7] = { "Customers", "Suppliers", "VAT due", "VAT deductible", "Profit carried forward", "Loss carried forward", "Suspense account" };
		for (int i = 0; i < 7; i++) wk_text_l (p.canvas, 20, 12 + i * 34, ED_H, R[i], C_TEXT);
	}
	void show (int i) { part->set (i); for (int k = 0; k < 5; k++) pan[k]->hidden = k != i; if (i == 4) tpl->invalidate (true); invalidate (true); }
	// ---- the templates ----
	static const char *t_text (DataGrid &, int row, int col, char *buf, int cap)
	{
		if (row < 0 || row >= PK_KINDS || !g_sp) return "";
		if (col == 0) return PK_TITLE[row];
		if (col == 1)
		{
			char p[200]; if (!template_find (row, g_sp->tplLang->cur, p, sizeof p)) return "(none)";
			int n = slen (TEMPLATES); scpy (buf, "templates", cap); scat (buf, p + n, cap); return buf;
		}
		return "";
	}
	static bool t_draw (DataGrid &, Canvas &cv, int row, int col, int x, int y, int w, int h, unsigned, bool sel)
	{
		if (col != 2 || row < 0 || row >= PK_KINDS || !g_sp) return false;
		int lang = g_sp->tplLang->cur; char p[200];
		bool own = template_find (row, lang, p, sizeof p, true), any = own || template_find (row, lang, p, sizeof p);
		draw_pill (cv, x + 6, y + (h - 18) / 2, 18, !any ? "Missing" : own ? "Its own" : "French one", !any ? C_BAD : own ? C_GOOD : C_WARN, sel);
		(void) w;
		return true;
	}
	void tplEdit ()
	{
		int k = tpl->sel, lang = tplLang->cur;
		if (k < 0 || k >= PK_KINDS) return;
		char p[200];
		if (!template_find (k, lang, p, sizeof p, true))
		{
			char fr[200];
			if (!template_find (k, 0, fr, sizeof fr)) { warn ("Printing", "There is no template for this document: put one (a Writer document) in SD:/apps/ledger.app/templates."); return; }
			if (lang == 0) scpy (p, fr, sizeof p);
			else
			{
				char m[200] = "This document has no "; scat (m, tplLang->seg[lang], sizeof m); scat (m, " template: make one from the French one?", sizeof m);
				if (ask ("Printing", m, MB_YESNO, 1) != 1) return;
				char *b; int n;
				if (!file_read (fr, &b, &n)) { warn ("Printing", "The French template could not be read."); return; }
				char dir[160]; scpy (dir, TEMPLATES, sizeof dir); scat (dir, "/", sizeof dir); scat (dir, LANG_KEY[lang], sizeof dir);
				kapi_mkdir (dir);
				const char *ext = fr + slen (fr); while (ext > fr && *ext != '.') ext--;
				scpy (p, dir, sizeof p); scat (p, "/", sizeof p); scat (p, PK_FILE[k], sizeof p); scat (p, ext, sizeof p);
				bool ok = kapi_save_file (p, b, (unsigned) n) >= 0;
				delete [] b;
				if (!ok) { warn ("Printing", "The template could not be written."); return; }
				tpl->invalidate (true);
			}
		}
		if (!kapi_exec ("SD:/apps/writer.app/main", p)) warn ("Printing", "Writer could not be started.");
		else { char m[240] = "Writer opens "; scat (m, p, sizeof m); status (m); }
	}
	static void on_tplLang (int) { if (g_sp) g_sp->tpl->invalidate (true); }
	static void on_tplEdit (Widget &) { if (g_sp) g_sp->tplEdit (); }
	static void s_tplEdit () { if (g_sp) g_sp->tplEdit (); }
	static void s_tplFolder ()
	{
		if (!g_sp) return;
		char d[160]; scpy (d, TEMPLATES, sizeof d); if (g_sp->tplLang->cur) { scat (d, "/", sizeof d); scat (d, LANG_KEY[g_sp->tplLang->cur], sizeof d); }
		kapi_mkdir (TEMPLATES); kapi_mkdir (d);
		if (!kapi_exec ("SD:/apps/fileviewer.app/main", d)) warn ("Printing", "The File Viewer could not be started.");
	}
	static void on_part (int i) { if (g_sp) g_sp->show (i); }
	static void on_vat (Widget &) { if (g_sp) g_sp->checkVat (); }
	static void on_jedit (Widget &) { if (g_sp) g_sp->editJournal (g_sp->journals->sel); }
	static void s_saveCompany () { if (g_sp) g_sp->saveCompany (); }
	static void s_saveRoles () { if (g_sp) g_sp->saveRoles (); }
	static void s_addYear () { if (g_sp) g_sp->addYear (); }
	static void s_closeYear () { if (g_sp) g_sp->closeYear (); }
	static void s_reopenYear () { if (g_sp) g_sp->reopenYear (); }
	static void s_newJournal () { if (g_sp) g_sp->editJournal (-1); }
	static void s_editJournal () { if (g_sp && g_sp->journals->sel >= 0) g_sp->editJournal (g_sp->journals->sel); }
};

} // namespace lg

#endif
