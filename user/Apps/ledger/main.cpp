//
// ledger -- Onyx's accounting: the double-entry books of a Belgian company or self-employed person, the
// way BOB 50 keeps them and as simply as GnuCash shows them. One file (.ledger: fileio.h) holds a
// company's books: its chart of accounts (the PCMN, in French or in Dutch: pcmn.h), its customers and
// suppliers, its journals and their documents, its fiscal years and VAT returns. Every document saved is
// written to the file at once (the one before kept as .bak).
//
// The window: at the left, the side bar -- the company, the fiscal year shown, the pages; at the right
// the page shown (ui.h Page):
//   Overview        the dashboard (lists.h)                 Customers, Suppliers   their cards, balances,
//   Sales           the sales invoices and credit notes                            accounts, matching
//   Purchases       the purchase invoices and credit notes   Chart of accounts     the tree, the registers
//   Bank and cash   the statements                           Reports               journals, ledgers,
//   Misc. operations                                                               balances, balance sheet...
//                                                            VAT                   returns, Intervat, listings
//                                                            Settings              company, years, journals
// A document opens in its page (docs.h: the invoice, the statement, the operation), Save goes back to
// its list. Ctrl+N makes a new one of what the page lists, Ctrl+S saves the document, Ctrl+F searches,
// Esc leaves a document.
//
// "ledger SD:/docs/x.ledger" opens a file; so does a file dropped on the window or a double click in the
// File Viewer (fileassoc.ini: ledger). Started without one, Ledger opens the books it had last
// (SD:/apps/ledger.app/last.txt), else it welcomes: a new company, a file, the demo company.
//
#include "wtk/wtk.h"
#include "docguard.h"
#include "ui.h"
#include "pick.h"
#include "editgrid.h"
#include "docs.h"
#include "lists.h"
#include "export.h"
#include "pages2.h"
#include "commerce_ui.h"

using namespace wtk;

namespace lg {

#define W 1000
#define H 700
static const char *LAST = "SD:/apps/ledger.app/last.txt";
static const char *DEMO = "SD:/docs/demo-company.ledger";

static Root *g_root;
static Page *g_page[NPAGES];
static int   g_cur = -1;

// ---- the side bar ---------------------------------------------------------------------------------------------------------------
struct NavItem { int page; const char *label; };
static const NavItem NAV[] = {
	{ P_OVERVIEW, "Overview" },
	{ -1, "JOURNALS" }, { P_SALES, "Sales" }, { P_PURCH, "Purchases" }, { P_FIN, "Bank and cash" }, { P_MISC, "Misc. operations" },
	{ -1, "COMMERCIAL" }, { P_DOCS, "Quotes and orders" },
	{ -1, "PARTIES" }, { P_CUST, "Customers" }, { P_SUPP, "Suppliers" },
	{ -1, "ACCOUNTING" }, { P_ACCOUNTS, "Chart of accounts" }, { P_REPORTS, "Reports" }, { P_VAT, "VAT" },
	{ -1, "" }, { P_SETTINGS, "Settings" } };
enum { NNAV = sizeof NAV / sizeof NAV[0], ITEM_H = 30, CAP_H = 24, NAV_Y = 104 };

class SideBar : public Widget
{
public:
	ChoiceBox *years; const char *ynames[MAXYEARS]; char ybuf[MAXYEARS][24];
	int cur, hot; int badge[P_COUNT]; unsigned badgeCol[P_COUNT];
	SideBar () : Widget (0, 0, SIDE_W, H), cur (P_OVERVIEW), hot (-1)
	{
		anchor = ANCHOR_LEFT | ANCHOR_TOP | ANCHOR_BOTTOM;
		years = new ChoiceBox (12, 62, SIDE_W - 24); years->onChange = on_year; years->tip = "The fiscal year the pages show";
		addChild (years);
		for (int i = 0; i < P_COUNT; i++) { badge[i] = 0; badgeCol[i] = 0; }
	}
	unsigned face () const { return wk_tone (C_BG, 116); }
	unsigned bgColor () override { return face (); }
	int itemY (int i) const { int y = NAV_Y; for (int k = 0; k < i; k++) y += NAV[k].page < 0 ? CAP_H : ITEM_H; return y; }
	int itemAt (int my) const
	{
		for (int i = 0; i < NNAV; i++) { int y = itemY (i), h = NAV[i].page < 0 ? CAP_H : ITEM_H; if (my >= y && my < y + h) return NAV[i].page < 0 ? -1 : i; }
		return -1;
	}
	void refresh ()
	{
		for (int i = 0; i < g_b.nyr && i < MAXYEARS; i++)
		{
			year_label (g_b.yr[i], ybuf[i], sizeof ybuf[i]);
			char t[24] = "Fiscal year "; scat (t, ybuf[i], sizeof t); if (g_b.yr[i].closed) scat (t, " (closed)", sizeof t);
			scpy (ybuf[i], t, sizeof ybuf[i]); ynames[i] = ybuf[i];
		}
		years->setOptions (ynames, g_b.nyr);
		years->sel = iclamp (g_year, 0, imax (0, g_b.nyr - 1));
		years->hidden = !g_b.nyr;
		// the badges: overdue documents, a VAT return late
		for (int i = 0; i < P_COUNT; i++) badge[i] = 0;
		int t = today_ymd ();
		for (int i = 0; i < g_b.ne; i++)
		{
			const Entry &e = g_b.e[i];
			int jt = g_b.jr[e.journal].type;
			if (jt != JT_SALES && jt != JT_PURCH) continue;
			for (int k = 0; k < e.nl; k++)
			{
				const Line &l = e.l[k];
				if (!l.party || l.match || !acc_party (g_b, l.account)) continue;
				if ((l.due ? l.due : e.date) < t && (jt == JT_SALES ? l.amount > 0 : l.amount < 0)) { badge[jt == JT_SALES ? P_SALES : P_PURCH]++; break; }
			}
		}
		badgeCol[P_SALES] = C_BAD; badgeCol[P_PURCH] = C_WARN;
		if (g_b.vatRegime == VR_NORMAL && g_b.ne)
		{
			bool monthly = g_b.vatPeriod == VP_MONTH;
			int y = y_of (t), late = 0;
			for (int yy = y - 1; yy <= y; yy++)
				for (int p = 1; p <= (monthly ? 12 : 4); p++)
				{
					int f, e; period_range (yy, p, monthly, &f, &e);
					if (return_deadline (yy, p, monthly) >= t || return_find (g_b, yy, p, monthly) >= 0) continue;
					bool used = false; for (int i = 0; i < g_b.ne && !used; i++) if (g_b.e[i].date >= f && g_b.e[i].date <= e) used = true;
					if (used) late++;
				}
			badge[P_VAT] = late; badgeCol[P_VAT] = C_BAD;
		}
		invalidate (true);
	}
	void onDraw () override
	{
		unsigned bg = face (), ink = wk_ink_for (bg), dim = wk_mix (bg, ink, 140);
		canvas.clear (bg);
		canvas.fillRect (width - 1, 0, 1, height, wk_tone (C_BG, 92));
		bool open = g_b.nacc != 0;
		// the company
		text_fit_l (canvas, 14, 10, width - 28, 24, open ? (g_b.name[0] ? g_b.name : "(the company)") : "Ledger", ink, 2);
		char v[40] = ""; if (open && g_b.vat[0]) vat_show (g_b.vat, v, sizeof v); else if (open) scpy (v, g_b.vatRegime == VR_NORMAL ? "(no VAT number)" : "Not subject to VAT", sizeof v);
		else scpy (v, "Belgian accounting", sizeof v);
		text_fit_l (canvas, 14, 32, width - 28, 20, v, dim);
		for (int i = 0; i < NNAV; i++)
		{
			int y = itemY (i);
			const NavItem &n = NAV[i];
			if (n.page < 0)
			{
				if (n.label[0]) wk_text_l (canvas, 16, y + 4, CAP_H - 4, n.label, wk_mix (bg, ink, 110));
				else wk_etch_h (canvas, 12, y + CAP_H / 2, width - 24, bg);
				continue;
			}
			bool on = n.page == cur, h = i == hot && open;
			if (on) wk_hilite (canvas, 8, y + 1, width - 16, ITEM_H - 2, 6, true);
			else if (h) wk_rbox (canvas, 8, y + 1, width - 16, ITEM_H - 2, 6, wk_tone (bg, 150), wk_tone (bg, 140));
			unsigned in = on ? wk_hilite_ink (true) : open || n.page == P_OVERVIEW ? ink : wk_mix (bg, ink, 90);
			draw_ni (canvas, PAGE_ICON[n.page], 18, y + (ITEM_H - 20) / 2, in, on ? in : C_ACCENT);
			wk_text_l (canvas, 48, y, ITEM_H, n.label, in, on ? 2 : 0);
			if (open && badge[n.page])
			{
				char b[8]; itoa10 (badge[n.page], b);
				int pw = pill_w (b);
				draw_pill (canvas, width - 16 - pw, y + (ITEM_H - 18) / 2, 18, b, badgeCol[n.page], true);
			}
		}
		// the file, at the foot
		if (g_path[0])
		{
			const char *f = g_path; for (const char *q = g_path; *q; q++) if (*q == '/' || *q == ':') f = q + 1;
			int y = height - 30;
			wk_etch_h (canvas, 12, y - 8, width - 24, bg);
			unsigned c = g_saveFailed ? C_BAD : C_GOOD;
			wk_rbox (canvas, 16, y + 7, 8, 8, 4, c, c);
			text_fit_l (canvas, 30, y, width - 44, 22, f, dim);
		}
	}
	bool onMouse (int mx, int my, int bl, int, int, int wheel) override
	{
		if (wheel) return false;
		bool in = mx >= 0 && my >= 0 && mx < width && my < height;
		int h = in ? itemAt (my) : -1;
		if (h != hot) { hot = h; invalidate (true); }
		if (bl && !pressed) { pressed = true; if (h >= 0 && (g_b.nacc || NAV[h].page == P_OVERVIEW)) go (NAV[h].page); }
		else if (!bl) pressed = false;
		return in;
	}
	void set (int page) { if (page != cur) { cur = page; invalidate (true); } }
	static void on_year (Widget &w);
};
static SideBar *g_side;

// ---- the status: a word at the foot of the page, for a few seconds -------------------------------------------------------------
class Toast : public Widget
{
public:
	char msg[160]; unsigned t0;
	Toast () : Widget (0, 0, 300, 34), t0 (0) { msg[0] = '\0'; hidden = true; transparent = true; }
	void show (const char *m)
	{
		scpy (msg, m, sizeof msg);
		int w = imin (wk_text_w (msg) + 40, W - SIDE_W - 40);
		Widget *p = parent;
		if (p) { left = SIDE_W + (p->width - SIDE_W - w) / 2; top = p->height - height - 18; bringToFront (); }
		if (w != width) resizeTo (w, height);
		hidden = false; t0 = kapi_get_ticks ();
		invalidate (true);
	}
	void tick () { if (!hidden && kapi_get_ticks () - t0 > 350) { hidden = true; if (parent) parent->invalidate (true); } }
	void onDraw () override
	{
		canvas.clear (WK_TRANSPARENT_KEY);
		unsigned c = 0x00303438;
		wk_rbox (canvas, 0, 0, width, height, height / 2, c, c);
		wk_corner_key (canvas, 0, 0, width, height, height / 2);
		char t[160]; fit_text (msg, width - 32, t, sizeof t);
		wk_text_c (canvas, 0, 0, width, height, t, 0x00FFFFFF);
	}
};
static Toast *g_toast;
static void status (const char *msg) { if (g_toast) g_toast->show (msg); }

// ---- the pages ---------------------------------------------------------------------------------------------------------------------
static Page *make_page (int id)
{
	Page *p = 0;
	switch (id)
	{
	case P_OVERVIEW: p = new OverviewPage (); break;
	case P_SALES: case P_PURCH: case P_FIN: case P_MISC: { JournalPage *j = new JournalPage (id); g_jp[id - P_SALES] = j; p = j; break; }
	case P_CUST: case P_SUPP: { PartyPage *q = new PartyPage (id); g_pp[id - P_CUST] = q; p = q; break; }
	case P_ACCOUNTS: p = g_ap = new AccountsPage (); break;
	case P_REPORTS: p = g_rp = new ReportsPage (); break;
	case P_VAT: p = g_vp = new VatPage (); break;
	case P_SETTINGS: p = g_sp = new SettingsPage (); break;
	case E_INVOICE: p = g_inv = new InvoicePage (); break;
	case E_STATEMENT: p = g_st = new StatementPage (); break;
	case E_MISC: p = g_misc = new MiscPage (); break;
	case P_DOCS: p = g_dp = new DocsPage (); break;
	case E_CDOC: p = g_cdp = new CDocPage (); break;
	}
	p->left = SIDE_W; p->top = 0; p->hidden = true;
	p->resizeTo (g_root->width - SIDE_W, g_root->height);
	g_root->addChild (p);
	if (g_toast) g_toast->bringToFront ();
	return p;
}
static Page *page (int id) { if (!g_page[id]) g_page[id] = make_page (id); return g_page[id]; }
static void title_bar () { if (g_side) g_side->invalidate (true); }	// (the company's name: the side bar's top)
// Page id shown (the one before asked to leave first unless ask is false).
static bool show_page (int id, bool askLeave = true)
{
	if (g_cur == id) { g_page[id]->sync (); return true; }
	if (askLeave && g_cur >= 0 && !g_page[g_cur]->leave ()) return false;
	Page *p = page (id);
	if (g_cur >= 0) g_page[g_cur]->hidden = true;
	g_cur = id;
	g_doc = id >= E_INVOICE ? (DocPage *) p : 0;
	p->hidden = false;
	p->sync ();
	p->invalidate (true);
	g_side->set (id < P_COUNT ? id : ((DocPage *) p)->back);
	p->enter ();
	g_root->invalidate (true);
	return true;
}
static void go (int id) { show_page (id); }
static int list_of (int journal)
{
	int t = journal >= 0 && journal < g_b.njr ? (int) g_b.jr[journal].type : (int) JT_MISC;
	return t == JT_SALES ? P_SALES : t == JT_PURCH ? P_PURCH : jt_fin (t) ? P_FIN : P_MISC;
}
static int back_page (int journal) { return g_cur >= 0 && g_cur < P_COUNT ? g_cur : list_of (journal); }
static void open_entry (int id)
{
	int i = entry_index (g_b, id);
	if (i < 0) return;
	if (g_cur >= 0 && g_cur >= E_INVOICE && !g_page[g_cur]->leave ()) return;
	if (g_cur >= 0 && g_cur < E_INVOICE && !g_page[g_cur]->leave ()) return;
	const Entry &e = g_b.e[i];
	int t = g_b.jr[e.journal].type, back = back_page (e.journal);
	DocPage *d;
	if ((t == JT_SALES || t == JT_PURCH) && ((InvoicePage *) page (E_INVOICE))->load (e)) d = g_inv;
	else if (jt_fin (t) && ((StatementPage *) page (E_STATEMENT))->load (e)) d = g_st;
	else { ((MiscPage *) page (E_MISC))->load (e); d = g_misc; }
	d->back = back;
	if (g_cur == d->id) { d->sync (); d->invalidate (true); d->enter (); return; }
	show_page (d->id, false);
}
static void new_document (int journal, bool credit, int backTo)
{
	if (journal < 0 || journal >= g_b.njr) return;
	if (g_cur >= 0 && !g_page[g_cur]->leave ()) return;
	int t = g_b.jr[journal].type, back = backTo >= 0 ? backTo : back_page (journal);
	if (g_year >= 0 && g_b.yr[g_year].closed) { warn ("New document", "The fiscal year shown is closed: choose another one (at the top of the side bar)."); return; }
	DocPage *d;
	if (t == JT_SALES || t == JT_PURCH) { ((InvoicePage *) page (E_INVOICE))->startNew (journal, credit); d = g_inv; }
	else if (jt_fin (t)) { ((StatementPage *) page (E_STATEMENT))->startNew (journal); d = g_st; }
	else { ((MiscPage *) page (E_MISC))->startNew (journal); d = g_misc; }
	d->back = back;
	d->seen = g_b.changes; d->seenYear = g_yearVer;
	if (g_cur == d->id) { d->invalidate (true); d->enter (); return; }
	show_page (d->id, false);
}
// A commercial document in its page; a new one (from another: its copy -- an order from a quote).
static void open_cdoc (int id)
{
	int i = cdoc_index (g_b, id);
	if (i < 0) return;
	if (g_cur >= 0 && !g_page[g_cur]->leave ()) return;
	i = cdoc_index (g_b, id);
	if (i < 0) return;
	int back = g_cur >= 0 && g_cur < P_COUNT ? g_cur : P_DOCS;
	CDocPage *d = (CDocPage *) page (E_CDOC);
	d->load (g_b.cd[i]);
	d->back = back;
	if (g_cur == E_CDOC) { d->sync (); d->invalidate (true); d->enter (); return; }
	show_page (E_CDOC, false);
}
static void new_cdoc (int kind, const CDoc *from)
{
	if (g_cur >= 0 && !g_page[g_cur]->leave ()) return;
	int back = g_cur >= 0 && g_cur < P_COUNT ? g_cur : g_cur >= E_INVOICE ? ((DocPage *) g_page[g_cur])->back : P_DOCS;
	CDocPage *d = (CDocPage *) page (E_CDOC);
	d->startNew (kind, from);
	d->back = back;
	d->seen = g_b.changes; d->seenYear = g_yearVer;
	if (g_cur == E_CDOC) { d->invalidate (true); d->enter (); return; }
	show_page (E_CDOC, false);
}
bool CDocPage::g_cur_is_invoice () { return g_cur == E_INVOICE; }
static void open_party (int id)
{
	const Party *p = party_of (g_b, id);
	if (!p) return;
	int pg = p->kind == PK_CUSTOMER ? P_CUST : P_SUPP;
	if (!show_page (pg)) return;
	g_pp[pg - P_CUST]->show (id);
}
static void open_account (const char *code)
{
	if (!show_page (P_ACCOUNTS)) return;
	g_ap->show (code);
}
void SideBar::on_year (Widget &w)
{
	ChoiceBox &c = (ChoiceBox &) w;
	if (c.sel == g_year) return;
	g_year = c.sel; g_yearVer++;
	if (g_cur >= 0) { g_page[g_cur]->sync (); g_page[g_cur]->invalidate (true); }
	g_side->refresh ();
}

// ---- the books' file ----------------------------------------------------------------------------------------------------------------
static void changed ()
{
	g_b.changes++;
	if (g_path[0])
	{
		bool ok = book_save (g_b, g_path);
		if (!ok && !g_saveFailed) warn ("Save", "The books could not be written to their file (the card full, write-protected?). They are kept in memory: try File > Save a Copy As.");
		g_saveFailed = !ok;
	}
	if (g_cur >= 0) g_page[g_cur]->sync ();
	g_side->refresh ();
	title_bar ();
}
static void remember (const char *path) { kapi_mkdir ("SD:/apps/ledger.app"); kapi_save_file (LAST, path, (unsigned) slen (path)); }
// The fiscal year to show first: today's, else the last one.
static int year_for_today ()
{
	int y = year_of (g_b, today_ymd ());
	return y >= 0 ? y : g_b.nyr - 1;
}
static void books_opened ()
{
	g_year = year_for_today ();
	g_yearVer++;
	g_saveFailed = false;
	for (int i = 0; i < NPAGES; i++) if (g_page[i]) { g_page[i]->seen = 0; if (i >= E_INVOICE) ((DocPage *) g_page[i])->dirty = false; }
	g_side->refresh ();
	title_bar ();
	g_cur = g_cur >= 0 && g_cur < P_COUNT ? g_cur : -1;
	if (g_cur >= 0) g_page[g_cur]->hidden = true;
	g_cur = -1;
	show_page (P_OVERVIEW, false);
}
static bool load_path (const char *path, bool quiet = false, bool toast = true)
{
	char *b; int n;
	if (!file_read (path, &b, &n)) { if (!quiet) warn ("Open", "The file could not be read."); return false; }
	static Book tmp; static bool init = false;
	if (!init) { book_init (tmp); init = true; }
	const char *why = "";
	bool ok = book_read (tmp, b, n, &why);
	delete [] b;
	if (!ok) { if (!quiet) warn ("Open", why); return false; }
	book_clear (g_b);
	g_b = tmp;
	unsigned ch = tmp.changes;
	book_init (tmp); tmp.changes = ch;			// (its arrays now g_b's)
	scpy (g_path, path, sizeof g_path);
	remember (path);
	books_opened ();
	if (!quiet && toast) { char m[240] = "Opened: "; scat (m, path, sizeof m); status (m); }
	return true;
}
static bool leave_current () { return g_cur < 0 || g_page[g_cur]->leave (); }
static void cmd_open ()
{
	if (!leave_current ()) return;
	char path[200];
	if (!wk_file_open (path, sizeof path, "SD:/docs")) return;
	load_path (path);
}
static void cmd_new_company ()
{
	if (!leave_current ()) return;
	NewCompanyDialog d;
	if (d.run () != 1) return;
	static Book nb; static bool init = false;
	if (!init) { book_init (nb); init = true; }
	d.make (nb);
	char name[96]; int k = 0;
	for (const char *q = nb.name; *q && k < 80; q++) { unsigned char c = (unsigned char) *q; if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c >= 0xC0 || c == ' ' || c == '-') name[k++] = (char) c; }
	name[k] = '\0'; if (!k) scpy (name, "company", sizeof name);
	scat (name, ".ledger", sizeof name);
	char path[200];
	if (!wk_file_save (path, sizeof path, "SD:/docs", name)) { book_clear (nb); return; }
	int n = slen (path); if (n < 7 || !ci_eq (path + n - 7, ".ledger")) scat (path, ".ledger", sizeof path);
	Out o; book_write (nb, o);
	book_clear (nb);
	if (kapi_save_file (path, o.b, (unsigned) o.n) < 0) { warn ("New company", "The file could not be written."); return; }
	load_path (path);
	status ("The company's books are ready: its chart, its journals, its first fiscal year");
}
static void cmd_save_copy ()
{
	if (!g_b.nacc) return;
	char path[200];
	if (!wk_file_save (path, sizeof path, "SD:/docs", "copy.ledger")) return;
	Out o; book_write (g_b, o);
	if (kapi_save_file (path, o.b, (unsigned) o.n) < 0) warn ("Save a Copy", "The file could not be written.");
	else { char m[240] = "Copy written: "; scat (m, path, sizeof m); status (m); }
}
void OverviewPage::s_newCompany () { cmd_new_company (); }
void OverviewPage::s_open () { cmd_open (); }
void OverviewPage::s_demo ()
{
	if (!load_path (DEMO)) warn ("Demo", "The demo company's file (SD:/docs/demo-company.ledger) is not on the card.");
}

// ---- the commands -----------------------------------------------------------------------------------------------------------------
static bool books () { if (g_b.nacc) return true; status ("Open a company's books first (File > Open, or New Company)"); return false; }
static Page *cur_page () { return g_cur >= 0 ? g_page[g_cur] : 0; }
static void cmd_new () { if (books () && cur_page ()) cur_page ()->cmdNew (); }
static void cmd_save () { if (g_doc && g_cur >= E_INVOICE && g_doc->save ()) g_doc->close (); }
static void cmd_find () { if (books () && cur_page ()) cur_page ()->cmdFind (); }
static void cmd_delete () { if (books () && cur_page ()) { if (g_cur >= E_INVOICE) g_doc->del (); else cur_page ()->cmdDelete (); } }
static void doc_of (int type, bool credit) { if (!books ()) return; int j = jrn_first (g_b, type); if (j < 0) { warn ("New document", "No journal of that kind (Settings > Journals)."); return; } new_document (j, credit); }
static void cmd_sale () { doc_of (JT_SALES, false); }
static void cmd_sale_cn () { doc_of (JT_SALES, true); }
static void cmd_purch () { doc_of (JT_PURCH, false); }
static void cmd_purch_cn () { doc_of (JT_PURCH, true); }
static void cmd_bank () { doc_of (JT_BANK, false); }
static void cmd_cash () { doc_of (JT_CASH, false); }
static void cmd_misc () { doc_of (JT_MISC, false); }
static void go_if (int p) { if (p == P_OVERVIEW || books ()) go (p); }
static void cmd_go_overview () { go_if (P_OVERVIEW); }
static void cmd_go_sales () { go_if (P_SALES); }
static void cmd_go_purch () { go_if (P_PURCH); }
static void cmd_go_fin () { go_if (P_FIN); }
static void cmd_go_misc () { go_if (P_MISC); }
static void cmd_go_cust () { go_if (P_CUST); }
static void cmd_go_supp () { go_if (P_SUPP); }
static void cmd_go_accounts () { go_if (P_ACCOUNTS); }
static void cmd_go_reports () { go_if (P_REPORTS); }
static void cmd_go_vat () { go_if (P_VAT); }
static void cmd_go_settings () { go_if (P_SETTINGS); }
static void cmd_go_docs () { go_if (P_DOCS); }
static void cmd_quote () { if (books ()) new_cdoc (CD_QUOTE); }
static void cmd_order () { if (books ()) new_cdoc (CD_ORDER); }
static void cmd_delivery () { if (books ()) new_cdoc (CD_DELIVERY); }
static void cmd_porder () { if (books ()) new_cdoc (CD_PORDER); }
static void cmd_close_year () { if (!books ()) return; go (P_SETTINGS); if (g_sp) { g_sp->show (1); g_sp->years->setSel (g_year); g_sp->closeYear (); } }
static void cmd_listings () { if (!books ()) return; go (P_VAT); if (g_vp) g_vp->lists (); }

// ---- the window --------------------------------------------------------------------------------------------------------------------
class LedgerRoot : public Root
{
public:
	LedgerRoot () : Root (W, H, "Ledger") {}
	void onTick () override
	{
		if (g_toast) g_toast->tick ();
		if (g_cur >= 0) g_page[g_cur]->tick ();
	}
	void onResized () override
	{
		for (int i = 0; i < NPAGES; i++) if (g_page[i]) g_page[i]->resizeTo (width - SIDE_W, height);
		if (g_side) g_side->resizeTo (SIDE_W, height);
	}
	void onDrop (int, int, int type, const char *data, int, unsigned) override
	{
		char path[200];
		if (type != DND_FILES || !doc_first_path (data, path, sizeof path)) return;
		void *d = kapi_opendir (path);
		if (d) { kapi_closedir (d); return; }
		if (!leave_current ()) return;
		load_path (path);
	}
};

} // namespace lg

using namespace lg;

int main (void)
{
	LedgerRoot root;
	if (root.canvas.px == 0) return 1;
	root.attach ();				// (a question asked before run (): its clicks and keys)
	g_root = &root;
	book_init (g_b);
	root.setBg (C_BG);
	g_side = new SideBar ();
	root.addChild (g_side);
	g_toast = new Toast ();
	root.addChild (g_toast);
	root.setResizable (true);

	static Menu menu;
	menu.menu ("File");
	menu.item ("New Company...", "", 0, cmd_new_company);
	menu.item ("Open...", "^O", WK_CTRL ('O'), cmd_open);
	menu.separator ();
	menu.item ("Save a Copy As...", "", 0, cmd_save_copy);
	menu.menu ("Edit");
	menu.item ("New", "^N", WK_CTRL ('N'), cmd_new);
	menu.item ("Save the Document", "^S", WK_CTRL ('S'), cmd_save);
	menu.item ("Delete...", "", 0, cmd_delete);
	menu.separator ();
	menu.item ("Search", "^F", WK_CTRL ('F'), cmd_find);
	menu.menu ("Documents");
	menu.item ("Sales Invoice", "", 0, cmd_sale);
	menu.item ("Sales Credit Note", "", 0, cmd_sale_cn);
	menu.item ("Purchase Invoice", "", 0, cmd_purch);
	menu.item ("Purchase Credit Note", "", 0, cmd_purch_cn);
	menu.separator ();
	menu.item ("Bank Statement", "", 0, cmd_bank);
	menu.item ("Cash Statement", "", 0, cmd_cash);
	menu.item ("Miscellaneous Operation", "", 0, cmd_misc);
	menu.separator ();
	menu.item ("Quote", "", 0, cmd_quote);
	menu.item ("Order", "", 0, cmd_order);
	menu.item ("Delivery Note", "", 0, cmd_delivery);
	menu.item ("Purchase Order", "", 0, cmd_porder);
	menu.menu ("Go");
	menu.item ("Overview", "", 0, cmd_go_overview);
	menu.item ("Sales", "", 0, cmd_go_sales);
	menu.item ("Purchases", "", 0, cmd_go_purch);
	menu.item ("Bank and Cash", "", 0, cmd_go_fin);
	menu.item ("Miscellaneous Operations", "", 0, cmd_go_misc);
	menu.item ("Quotes and Orders", "", 0, cmd_go_docs);
	menu.separator ();
	menu.item ("Customers", "", 0, cmd_go_cust);
	menu.item ("Suppliers", "", 0, cmd_go_supp);
	menu.separator ();
	menu.item ("Chart of Accounts", "", 0, cmd_go_accounts);
	menu.item ("Reports", "", 0, cmd_go_reports);
	menu.item ("VAT", "", 0, cmd_go_vat);
	menu.item ("Settings", "", 0, cmd_go_settings);
	menu.menu ("Tools");
	menu.item ("VAT Listings...", "", 0, cmd_listings);
	menu.item ("Close the Fiscal Year...", "", 0, cmd_close_year);
	menu.publish ();

	// A file named on the command line; else the books opened last; else the welcome.
	char args[200];
	int an = kapi_get_args (args, sizeof args);
	if (an < 0) an = 0;
	args[an < (int) sizeof args ? an : (int) sizeof args - 1] = '\0';
	bool opened = an > 0 && args[0] && load_path (args, false, false);
	if (!opened)
	{
		char *b; int n;
		if (file_read (LAST, &b, &n))
		{
			char last[200]; trim_copy (last, b, sizeof last);
			delete [] b;
			if (last[0]) opened = load_path (last, true);
		}
	}
	if (!opened) { g_side->refresh (); show_page (P_OVERVIEW, false); title_bar (); }
	root.run ();
	return 0;
}
