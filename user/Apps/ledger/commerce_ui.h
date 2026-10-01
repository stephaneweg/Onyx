//
// commerce_ui.h -- the commercial documents' pages (commerce.h): DocsPage lists the quotes, orders, delivery
// notes and purchase orders of the year shown (a kind at a time, or all; their state: draft, sent,
// accepted, refused, done -- a quote past its date: expired); CDocPage types one -- its party, dates,
// reference, description, lines (a description, a quantity, a unit price, a VAT code), its totals -- prints
// it (print.h: a Writer document from its template) and makes the next one: an order from a quote, a
// delivery note from an order, the invoice from any (the invoice's page, filled: Save posts it and marks
// the document done).
//
#ifndef _ledger_commerce_ui_h
#define _ledger_commerce_ui_h

#include "lists.h"
#include "print.h"

namespace lg {

static void open_cdoc (int id);				// (main.cpp) a commercial document in its page
static void new_cdoc (int kind, const CDoc *from = 0);	// a new one (from another: a copy)

// Invoiced: its invoice is in the books.
static bool cdoc_invoiced (const CDoc &d) { return d.invoice && entry_index (g_b, d.invoice) >= 0; }
// A document done: what followed it -- "Invoiced", "Ordered", "Delivered" (else "Done").
static const char *cdoc_done_word (const CDoc &d)
{
	if (cdoc_invoiced (d)) return "Invoiced";
	int k = -1; for (int i = 0; i < g_b.ncd; i++) if (g_b.cd[i].from == d.id) { k = i; break; }
	if (k >= 0) return g_b.cd[k].kind == CD_ORDER ? "Ordered" : g_b.cd[k].kind == CD_DELIVERY ? "Delivered" : "Done";
	return "Done";
}

// ---- the document ------------------------------------------------------------------------------------------------------------
class CDocPage;
static CDocPage *g_cdp;

class CDocPage : public DocPage, public EGModel
{
public:
	CDoc d;
	PickEdit *party; FlatButton *bParty, *bPrint, *bNext;
	DateEdit *date, *until; LineEdit *ref, *text; ChoiceBox *state;
	EditGrid *g;
	money tNet, tTax, tTot;
	CDocPage () : DocPage (E_CDOC), party (0), bParty (0), bPrint (0), bNext (0), date (0), until (0), ref (0), text (0), state (0), g (0), tNet (0), tTax (0), tTot (0)
	{
		cdoc_init (d);
		resizeTo (800, 660);
		buttons (s_save, 0, s_cancel, s_del);
		// Print and Next at the left of the others
		int x = bDelete->left - 8;
		bNext = new FlatButton (TR ("Next step"), s_next, FB_SECONDARY, NI_NEXT); x -= bNext->width; bNext->left = x; bNext->top = bSave->top; bNext->anchor = ANCHOR_RIGHT | ANCHOR_TOP;
		bNext->tip = TR ("An order from a quote, a delivery note from an order, the invoice"); addChild (bNext); x -= 8;
		bPrint = new FlatButton (TR ("Print"), s_print, FB_SECONDARY, NI_PRINT); x -= bPrint->width; bPrint->left = x; bPrint->top = bSave->top; bPrint->anchor = ANCHOR_RIGHT | ANCHOR_TOP;
		bPrint->tip = TR ("The document made by Writer from its template"); addChild (bPrint);
		int W = width, rx = W - 330;
		party = new PickEdit (130, 72, rx - 130 - 60, SK_PARTY, PK_CUSTOMER); party->anchor = ANCHOR_LEFT | ANCHOR_TOP | ANCHOR_RIGHT;
		party->onPick = on_party; party->onChange = mark_dirty; addChild (party);
		bParty = new FlatButton ("", s_newParty, FB_SECONDARY, NI_PLUS); bParty->left = rx - 52; bParty->top = 70; bParty->anchor = ANCHOR_RIGHT | ANCHOR_TOP;
		bParty->resizeTo (34, 30); bParty->tip = TR ("A new party (its card)"); addChild (bParty);
		date = new DateEdit (rx + 110, 72, 150); date->anchor = ANCHOR_RIGHT | ANCHOR_TOP; date->onChange = mark_dirty; addChild (date);
		until = new DateEdit (rx + 110, 104, 150); until->anchor = ANCHOR_RIGHT | ANCHOR_TOP; until->onChange = mark_dirty; addChild (until);
		text = new LineEdit (130, 136, rx - 130 - 18); text->anchor = ANCHOR_LEFT | ANCHOR_TOP | ANCHOR_RIGHT; text->onChange = mark_dirty;
		text->placeholder = TR ("What it is about, its conditions"); addChild (text);
		state = new ChoiceBox (rx + 110, 136, 150); state->anchor = ANCHOR_RIGHT | ANCHOR_TOP; { const char *csn[CS_COUNT]; for (int i = 0; i < CS_COUNT; i++) csn[i] = TR (CS_NAME[i]); state->setOptions (csn, CS_COUNT); } state->onChange = mark_dirty; addChild (state);
		ref = new LineEdit (130, 168, 230); ref->onChange = mark_dirty; ref->placeholder = TR ("Their reference, an order number"); addChild (ref);
		g = new EditGrid (16, 210, W - 32, height - 210 - 120, this);
		g->anchor = ANCHOR_FILL;
		g->addCol (TR ("Description"), 250, EK_TEXT);
		g->addCol (TR ("Quantity"), 86, EK_MONEY, true);
		g->addCol (TR ("Unit price"), 110, EK_MONEY, true);
		g->addCol (TR ("VAT code"), 84, EK_VAT, false, VS_SALES);
		g->addCol (TR ("Total"), 110, EK_READ, true);
		g->flex = 0;
		addChild (g);
	}
	~CDocPage () { cdoc_free (d); }
	EditGrid *grid () override { return g; }
	bool isNew () override { return d.id == 0; }
	bool sale () const { return cd_sale (d.kind); }
	const Party *pty () { return party_of (g_b, d.party); }
	void setup ()
	{
		party->sug.arg = sale () ? PK_CUSTOMER : PK_SUPPLIER;
		party->placeholder = sale () ? TR ("The customer: a name, a code, a VAT number") : TR ("The supplier: a name, a code, a VAT number");
		g->col[3].sugArg = sale () ? VS_SALES : VS_PURCH;
		g->reset ();
	}
	void fields ()
	{
		char pv[16] = ""; if (d.party) { scpy (pv, "P", sizeof pv); scat_num (pv, d.party, sizeof pv); }
		party->setValue (pv);
		char t[16]; date_show (d.date, t); date->setText (t); date_show (d.until, t); until->setText (t);
		text->setText (d.text); ref->setText (d.ref);
		state->sel = iclamp (d.status, 0, CS_COUNT - 1); state->invalidate (true);
		titleFor ();
	}
	void titleFor ()
	{
		if (d.id) { char n[32]; cdoc_number (d, n, sizeof n); scpy (head, TR (CD_NAME[d.kind]), sizeof head); scat (head, " ", sizeof head); scat (head, n, sizeof head); }
		else { static const char *const NEWK[CD_COUNT] = { "New quote", "New order", "New delivery note", "New purchase order" }; scpy (head, TR (NEWK[d.kind]), sizeof head); }
		bDelete->hidden = !d.id;
		// Next and Print at the left of the buttons shown
		int x = (bDelete->hidden ? bCancel->left - 10 : bDelete->left) - 8;
		x -= bNext->width; bNext->left = x; x -= 8;
		x -= bPrint->width; bPrint->left = x;
		invalidate (true);
	}
	void startNew (int kind, const CDoc *from)
	{
		cdoc_free (d);
		if (from) cdoc_follow (g_b, *from, kind, default_date (), d);
		else { cdoc_init (d); d.kind = kind; d.date = default_date (); }
		if (kind == CD_QUOTE && !d.until) d.until = date_add (d.date, 30);
		setup ();
		if (!d.nl) cdoc_add_line (d);
		fields ();
		recalc ();
		dirty = from != 0;
	}
	void load (const CDoc &src)
	{
		cdoc_free (d); cdoc_copy (d, src);
		setup ();
		if (!d.nl) cdoc_add_line (d);
		fields (); recalc ();
		dirty = false;
	}
	void recalc () { cdoc_totals (d, &tNet, &tTax, &tTot); invalidate (true); }
	void subtitle (char *out, int cap) override
	{
		out[0] = '\0';
		if (d.from) { int i = cdoc_index (g_b, d.from); if (i >= 0) { char n[32]; cdoc_number (g_b.cd[i], n, sizeof n); scpy (out, TRC ("origin", "From "), cap); scat (out, TR (CD_NAME[g_b.cd[i].kind]), cap); scat (out, " ", cap); scat (out, n, cap); } }
		if (cdoc_invoiced (d)) { int i = entry_index (g_b, d.invoice); if (i >= 0) { char r[40]; entry_ref (g_b, g_b.e[i], r, sizeof r); if (out[0]) scat (out, "  \xB7  ", cap); scat (out, TR ("Invoiced: "), cap); scat (out, r, cap); } }
		if (!out[0]) scpy (out, TR ("Not posted (its invoice will be)"), cap);
	}
	// ---- the lines (EGModel) ----
	int rows () override { return d.nl; }
	void cell (int r, int c, bool edit, char *out, int cap) override
	{
		out[0] = '\0';
		if (r < 0 || r >= d.nl) return;
		const CLine &l = d.l[r];
		switch (c)
		{
		case 0: scpy (out, l.text, cap); break;
		case 1: qty_show (l.qty, out); break;
		case 2: if (edit) edit_money (l.price, out); else show_money (l.price, out); break;
		case 3: scpy (out, vat_label (l.vat), cap); if (!edit && l.vat >= 0 && VAT_DEFS[l.vat].rate) { char rt[16]; fmt_rate (VAT_DEFS[l.vat].rate, rt); scat (out, "  ", cap); scat (out, rt, cap); } break;
		case 4: show_money (cline_total (l), out); break;
		}
	}
	const char *put (int r, int c, const char *t) override
	{
		CLine &l = d.l[r];
		switch (c)
		{
		case 0: sset (l.text, t); break;
		case 1: { long long q; if (!qty_parse (t, &q)) return TR ("Type a quantity, as 2,5."); l.qty = q; break; }
		case 2: { money m; const char *w = put_money (t, &m); if (w[0]) return w; l.price = m; break; }
		case 3: { int x; const char *w = resolve_vat (t, sale () ? VS_SALES : VS_PURCH, &x); if (w[0]) return w; l.vat = (signed char) x; break; }
		}
		if (l.vat < 0 && (l.price || l.text[0])) defaults (l);
		dirty = true;
		return "";
	}
	void defaults (CLine &l)
	{
		const Party *p = pty ();
		const char *acc = p && p->defAcc[0] ? p->defAcc : sale () ? "700000" : "604000";
		l.vat = (signed char) vat_default (g_b, p, sale (), acc);
	}
	void add () override { CLine &l = cdoc_add_line (d); if (d.nl > 1) l.vat = d.l[d.nl - 2].vat; else defaults (l); }
	void remove (int r) override { cdoc_remove_line (d, r); dirty = true; }
	bool blank (int r) override { const CLine &l = d.l[r]; return !l.text[0] && !l.price; }
	void linesChanged () override { dirty = true; recalc (); }
	// ---- saving ----
	bool save () override
	{
		if (!g->commit ()) return false;
		if (!party->resolve () || party->value ()[0] != 'P') { warn (TR ("Save"), sale () ? TR ("Choose the customer.") : TR ("Choose the supplier.")); party->setFocus (); return false; }
		d.party = cell_int (party->value () + 1);
		int dt = date_parse (date->text ());
		if (!dt) { date->setError (true); warn (TR ("Save"), TR ("Type the date, as 29/09/2026.")); return false; }
		d.date = dt;
		d.until = until->text ()[0] ? date_parse (until->text ()) : 0;
		sset (d.text, text->text ()); scpy (d.ref, ref->text (), sizeof d.ref);
		d.status = iclamp (state->sel, 0, CS_COUNT - 1);
		for (int i = d.nl - 1; i >= 0; i--) if (blank (i)) cdoc_remove_line (d, i);
		if (!d.nl) { cdoc_add_line (d); warn (TR ("Save"), TR ("The document has no line.")); return false; }
		bool wasNew = !d.id;
		CDoc x; cdoc_copy (x, d);
		int i = cdoc_save (g_b, x);
		cdoc_free (d); cdoc_copy (d, g_b.cd[i]);
		if (wasNew && d.from)					// (the one it follows: done)
		{
			int k = cdoc_index (g_b, d.from);
			if (k >= 0 && g_b.cd[k].status != CS_REFUSED) g_b.cd[k].status = CS_DONE;
		}
		char n[32], m[96]; cdoc_number (d, n, sizeof n);
		scpy (m, TR (CD_NAME[d.kind]), sizeof m); scat (m, " ", sizeof m); scat (m, n, sizeof m); scat (m, wasNew ? TR (" made") : TRC ("cdoc", " saved"), sizeof m);
		dirty = false;
		changed ();
		status (m);
		titleFor ();
		return true;
	}
	void del () override
	{
		int i = cdoc_index (g_b, d.id);
		if (i < 0) return;
		if (ask (TR ("Delete"), TR ("Delete this document for good?"), MB_YESNO, 2) != 1) return;
		cdoc_delete (g_b, i);
		changed ();
		status (TR ("Document deleted"));
		close ();
	}
	void print ()
	{
		if ((dirty || !d.id) && !save ()) return;
		print_cdoc (d);
	}
	void next ()
	{
		if ((dirty || !d.id) && !save ()) return;
		int x, y; abs_pos (bNext, &x, &y);
		PopupMenu m (x, y + bNext->height + 2);
		if (d.kind == CD_QUOTE) m.add (TR ("Make the order"), 1);
		if (d.kind == CD_ORDER || d.kind == CD_QUOTE) m.add (TR ("Make a delivery note"), 2);
		m.add (sale () ? TR ("Make the invoice") : TR ("Make the purchase invoice"), 3, !cdoc_invoiced (d));
		if (d.kind == CD_QUOTE) { m.separator (); m.add (TR ("Accepted"), 4, d.status != CS_ACCEPTED); m.add (TR ("Refused"), 5, d.status != CS_REFUSED); }
		int r = m.run ();
		int i = cdoc_index (g_b, d.id);
		if (i < 0) return;
		if (r == 1 || r == 2)
		{
			if (d.kind == CD_QUOTE && r == 1) { g_b.cd[i].status = CS_ACCEPTED; changed (); }
			CDoc src; cdoc_copy (src, g_b.cd[i]);
			new_cdoc (r == 1 ? CD_ORDER : CD_DELIVERY, &src);
			cdoc_free (src);
		}
		else if (r == 3)
		{
			int j = jrn_first (g_b, sale () ? JT_SALES : JT_PURCH);
			if (j < 0) { warn (TR ("Invoice"), TR ("No journal for it (Settings > Journals).")); return; }
			int back = this->back, id = d.id;
			CDoc src; cdoc_copy (src, g_b.cd[i]);
			new_document (j, false, back);
			if (g_inv && g_cur_is_invoice ())
			{
				inv_free (g_inv->v);
				cdoc_to_invoice (g_b, src, j, default_date (), g_inv->v);
				if (src.text[0]) scpy (g_inv->v.text, src.text, sizeof g_inv->v.text);	// (what it is about, else where it comes from: "Devis 2026/0003")
				else cdoc_title (src, doc_lang (src.party), g_inv->v.text, sizeof g_inv->v.text);
				g_inv->fromCdoc = id;
				g_inv->fields (); g_inv->recalc (); g_inv->g->reset (); g_inv->dirty = true;
			}
			cdoc_free (src);
		}
		else if (r == 4 || r == 5) { g_b.cd[i].status = r == 4 ? CS_ACCEPTED : CS_REFUSED; d.status = g_b.cd[i].status; state->sel = d.status; state->invalidate (true); changed (); }
	}
	static bool g_cur_is_invoice ();
	void refresh () override { titleFor (); recalc (); }
	void enter () override { if (!d.party) party->setFocus (); else g->focusIn (); }
	void onDraw () override
	{
		drawHead ();
		int W = width, rx = W - 330;
		lab (20, 72, sale () ? TR ("Customer") : TR ("Supplier"));
		lab (rx, 72, TR ("Date")); lab (rx, 104, d.kind == CD_QUOTE ? TR ("Valid until") : TR ("Delivery")); lab (20, 136, TR ("Description")); lab (rx, 136, TR ("State"));
		lab (20, 168, TR ("Reference"));
		const Party *p = pty ();
		if (p)
		{
			char t[200] = ""; scpy (t, p->street, sizeof t);
			if (p->zip[0] || p->city[0]) { if (t[0]) scat (t, ", ", sizeof t); scat (t, p->zip, sizeof t); scat (t, " ", sizeof t); scat (t, p->city, sizeof t); }
			if (p->vat[0]) { char vv[24]; vat_show (p->vat, vv, sizeof vv); if (t[0]) scat (t, "  \xB7  ", sizeof t); scat (t, vv, sizeof t); }
			text_fit_l (canvas, 132, 102, rx - 150, 26, t, dim_ink (C_BG));
		}
		int fy = wg (g)->top + g->height + 12, fh = height - fy - 8, tx = W - 300;
		wk_rbox (canvas, tx, fy, 284, fh, 8, wk_tone (C_FIELD, 132), wk_tone (C_FIELD, 122));
		wk_rline (canvas, tx, fy, 284, fh, 8, wk_mix (C_BG, 0, 60), 100);
		char a[32];
		wk_text_l (canvas, tx + 14, fy + 8, 20, TR ("Total excl. VAT"), field_dim ()); text_r (canvas, tx + 270, fy + 8, 20, money_s (tNet, a), C_FIELD_TEXT);
		wk_text_l (canvas, tx + 14, fy + 30, 20, TR ("VAT"), field_dim ()); text_r (canvas, tx + 270, fy + 30, 20, money_s (tTax, a), C_FIELD_TEXT);
		canvas.fillRect (tx + 12, fy + fh - 40, 260, 1, wk_tone (C_FIELD, 100));
		wk_text_l (canvas, tx + 14, fy + fh - 36, 28, TR ("Total"), C_FIELD_TEXT, 2);
		text_r (canvas, tx + 270, fy + fh - 36, 28, money_s (tTot, a), C_FIELD_TEXT, 2);
		wk_text_l (canvas, 20, fy, 20, TR ("Printing"), dim_ink (C_BG), 2);
		int k = pk_of (d.kind);
		char t[160]; scpy (t, TR ("Its template: "), sizeof t); scat (t, PK_FILE[k], sizeof t); scat (t, ".rtf", sizeof t); scat (t, TR (" (Settings > Printing)"), sizeof t);
		text_fit_l (canvas, 20, fy + 22, tx - 40, 18, t, dim_ink (C_BG));
		scpy (t, TR ("Made by Writer in "), sizeof t); scat (t, "SD:/docs/", sizeof t); scat (t, PK_FOLDER[k], sizeof t);
		text_fit_l (canvas, 20, fy + 40, tx - 40, 18, t, dim_ink (C_BG));
	}
	static void on_party (Widget &)
	{
		CDocPage *p = g_cdp;
		int id = p->party->value ()[0] == 'P' ? cell_int (p->party->value () + 1) : 0;
		if (id == p->d.party) return;
		p->d.party = id;
		for (int i = 0; i < p->d.nl; i++) if (p->blank (i)) p->defaults (p->d.l[i]);
		p->dirty = true; p->recalc (); p->g->invalidate (true);
	}
	static void s_save () { if (g_cdp->save ()) g_cdp->close (); }
	static void s_cancel () { g_cdp->cancel (); }
	static void s_del () { g_cdp->del (); }
	static void s_print () { g_cdp->print (); }
	static void s_next () { g_cdp->next (); }
	static void s_newParty ()
	{
		char t[NAME_MAX]; trim_copy (t, g_cdp->party->val[0] ? "" : g_cdp->party->text (), sizeof t);
		int id = edit_party (0, g_cdp->sale () ? PK_CUSTOMER : PK_SUPPLIER, t);
		if (!id) return;
		char pv[16] = "P"; scat_num (pv, id, sizeof pv);
		g_cdp->party->setValue (pv); on_party (*g_cdp->party);
	}
};

// ---- the list --------------------------------------------------------------------------------------------------------------------
class DocsPage;
static DocsPage *g_dp;
struct DRow { int i; money total; };

class DocsPage : public Page
{
public:
	DataGrid *g; Segmented *filter; SearchBox *search;
	DRow *rows; int nrows, cap, keepId;
	DocsPage () : Page (P_DOCS), g (0), filter (0), search (0), rows (0), nrows (0), cap (0), keepId (0)
	{
		resizeTo (800, 660);
		HeadRow h (this);
		h.add (TR ("Other..."), s_other, FB_SECONDARY, NI_PLUS, TR ("An order, a delivery note, a purchase order"));
		h.add (TR ("New quote"), s_quote, FB_PRIMARY, NI_PLUS, TR ("A new quote (Ctrl+N)"));
		static const char *const F[5] = { "All", "Quotes", "Orders", "Delivery notes", "Purchase orders" };
		const char *FT[5]; for (int i = 0; i < 5; i++) FT[i] = TR (F[i]);
		filter = new Segmented (16, 70, FT, 5, on_filter); addChild (filter);
		search = new SearchBox (180); search->left = width - 16 - 180; search->top = 70; search->anchor = ANCHOR_RIGHT | ANCHOR_TOP;
		search->placeholder = TR ("Search"); search->onChange = on_search; search->onEnter = on_search_done; addChild (search);
		g = new_grid (this, 16, 108, width - 32, height - 108 - 12, this);
		g->setColumns (6);
		g->setColumn (0, TR ("Document"), 122); g->setColumn (1, TR ("Date"), 102); g->setColumn (2, TR ("Party"), 160);
		g->setColumn (3, TR ("Description"), 150); g->setColumn (4, TR ("Total"), 100, GRID_RIGHT); g->setColumn (5, TR ("State"), 100);
		g->cellText = c_text; g->cellDraw = c_draw; g->onActivate = on_open; g->onContext = on_context; g->sortable = false;
		g->emptyText = TR ("No quote or order yet: New quote makes the first one.");
		fit_columns (g, 2, 90, 3, 5, 4);
	}
	~DocsPage () { delete [] rows; }
	void resizeTo (int w, int h) override { Page::resizeTo (w, h); if (g) fit_columns (g, 2, 90, 3, 5, 4); }
	const char *title () override { return TR ("Quotes and orders"); }
	void subtitle (char *out, int cap_) override
	{
		int open = 0; for (int i = 0; i < nrows; i++) { int s = g_b.cd[rows[i].i].status; if (s == CS_DRAFT || s == CS_SENT || s == CS_ACCEPTED) open++; }
		out[0] = '\0'; scat_num (out, nrows, cap_); scat (out, nrows == 1 ? TR (" document") : TR (" documents"), cap_);
		scat (out, "  \xB7  ", cap_); scat_num (out, open, cap_); scat (out, TR (" to follow up"), cap_);
	}
	void refresh () override
	{
		search->left = width - 16 - search->width;
		int keep = g->sel >= 0 && g->sel < nrows ? g_b.cd[rows[g->sel].i].id : keepId;
		nrows = 0;
		int from = yfrom (), to = yto ();
		const char *words = search->text ();
		for (int i = g_b.ncd - 1; i >= 0; i--)
		{
			const CDoc &d = g_b.cd[i];
			if (d.date < from || d.date > to) continue;
			if (filter->cur && d.kind != filter->cur - 1) continue;
			if (words[0])
			{
				char hay[400], n[32]; cdoc_number (d, n, sizeof n);
				scpy (hay, n, sizeof hay); scat (hay, " ", sizeof hay); scat (hay, TR (CD_NAME[d.kind]), sizeof hay); scat (hay, " ", sizeof hay);
				scat (hay, party_name (g_b, d.party), sizeof hay); scat (hay, " ", sizeof hay); scat (hay, d.text, sizeof hay); scat (hay, " ", sizeof hay); scat (hay, d.ref, sizeof hay);
				if (!words_in (hay, words)) continue;
			}
			if (nrows == cap) { int c = cap ? cap * 2 : 64; DRow *nr = new DRow[c]; for (int q = 0; q < nrows; q++) nr[q] = rows[q]; delete [] rows; rows = nr; cap = c; }
			money n, t, tot; cdoc_totals (d, &n, &t, &tot);
			rows[nrows].i = i; rows[nrows].total = tot; nrows++;
		}
		// newest first (by date, then number)
		for (int a = 1; a < nrows; a++)
			for (int b = a; b > 0; b--)
			{
				const CDoc &x = g_b.cd[rows[b].i], &y = g_b.cd[rows[b - 1].i];
				if (x.date > y.date || (x.date == y.date && x.id > y.id)) { DRow t = rows[b]; rows[b] = rows[b - 1]; rows[b - 1] = t; } else break;
			}
		g->setRows (nrows);
		int sel = -1; for (int i = 0; i < nrows; i++) if (g_b.cd[rows[i].i].id == keep) sel = i;
		g->setSel (sel >= 0 ? sel : nrows ? 0 : -1);
		invalidate (true);
	}
	static const char *c_text (DataGrid &gr, int row, int col, char *buf, int cap_)
	{
		DocsPage *p = (DocsPage *) gr.user;
		if (row >= p->nrows) return "";
		const CDoc &d = g_b.cd[p->rows[row].i];
		switch (col)
		{
		case 0: { char n[32]; cdoc_number (d, n, sizeof n); scpy (buf, TR (CD_SHORT[d.kind]), cap_); scat (buf, " ", cap_); scat (buf, n + 5, cap_); return buf; }
		case 1: date_show (d.date, buf); return buf;
		case 2: return party_name (g_b, d.party);
		case 3: if (d.text[0]) return d.text; return d.nl ? d.l[0].text : "";
		}
		return "";
	}
	static bool c_draw (DataGrid &gr, Canvas &cv, int row, int col, int x, int y, int w, int h, unsigned ink, bool sel)
	{
		DocsPage *p = (DocsPage *) gr.user;
		if (row >= p->nrows) return false;
		const CDoc &d = g_b.cd[p->rows[row].i];
		if (col == 4) { cell_money (cv, x, y, w, h, p->rows[row].total, ink, sel, false, 2); return true; }
		if (col == 5)
		{
			const char *s = TR (CS_NAME[d.status]); unsigned c = field_dim ();
			switch (d.status)
			{
			case CS_SENT: c = C_BLUE; break;
			case CS_ACCEPTED: c = C_GOOD; break;
			case CS_REFUSED: c = C_BAD; break;
			case CS_DONE: c = C_PURPLE; s = TR (cdoc_done_word (d)); break;
			}
			if (d.kind == CD_QUOTE && (d.status == CS_DRAFT || d.status == CS_SENT) && d.until && d.until < today_ymd ()) { s = TR ("Expired"); c = C_WARN; }
			draw_pill (cv, x + 6, y + (h - 18) / 2, 18, s, c, sel);
			return true;
		}
		return false;
	}
	int selId () { return g->sel >= 0 && g->sel < nrows ? g_b.cd[rows[g->sel].i].id : 0; }
	void cmdNew () override { new_cdoc (filter->cur >= 1 ? filter->cur - 1 : CD_QUOTE); }
	void cmdFind () override { search->setFocus (); search->selectAll (); }
	void cmdDelete () override
	{
		int id = selId (); if (!id) return;
		if (ask (TR ("Delete"), TR ("Delete this document for good?"), MB_YESNO, 2) != 1) return;
		cdoc_delete (g_b, cdoc_index (g_b, id));
		changed ();
	}
	void enter () override { g->setFocus (); }
	bool onKey (long k) override
	{
		if (k == KEY_DEL) { cmdDelete (); return true; }
		if (k == KEY_ENTER) { int id = selId (); if (id) { keepId = id; open_cdoc (id); } return true; }
		return false;
	}
	static void on_filter (int) { if (g_dp) g_dp->refresh (); }
	static void on_search (Widget &) { if (g_dp) g_dp->refresh (); }
	static void on_search_done (Widget &) { if (g_dp) g_dp->g->setFocus (); }
	static void on_open (Widget &) { if (!g_dp) return; int id = g_dp->selId (); if (id) { g_dp->keepId = id; open_cdoc (id); } }
	static void on_context (Widget &w)
	{
		DataGrid &d = (DataGrid &) w;
		if (!g_dp || d.ctxRow < 0) return;
		int x, y; abs_pos (&d, &x, &y);
		PopupMenu m (x + d.ctxX, y + d.ctxY);
		m.add (TRC ("verb", "Open"), 1); m.add (TR ("Print"), 2); m.separator (); m.add (TR ("Delete..."), 3);
		int r = m.run ();
		int id = g_dp->selId (); if (!id) return;
		if (r == 1) open_cdoc (id);
		else if (r == 2) { int i = cdoc_index (g_b, id); if (i >= 0) print_cdoc (g_b.cd[i]); }
		else if (r == 3) g_dp->cmdDelete ();
	}
	static void s_quote () { new_cdoc (CD_QUOTE); }
	static void s_other ()
	{
		PopupMenu m (0, 0);
		if (g_dp) { FlatButton *b = 0; for (Widget *c = g_dp->firstChild; c; c = c->nextSib) if (c->top < HEAD_H && c->left > g_dp->width / 2) { b = (FlatButton *) c; break; } if (b) { int x, y; abs_pos (b, &x, &y); m.left = x; m.top = y + b->height + 2; } }
		m.add (TR ("Order"), 1); m.add (TR ("Delivery note"), 2); m.add (TR ("Purchase order"), 3);
		int r = m.run ();
		if (r >= 1) new_cdoc (r == 1 ? CD_ORDER : r == 2 ? CD_DELIVERY : CD_PORDER);
	}
};

} // namespace lg

#endif
