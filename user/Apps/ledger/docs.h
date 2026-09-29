//
// docs.h -- the documents typed, a page each (they replace the list they were opened from; Save or Cancel go
// back to it):
//   * InvoicePage: a sales or purchase invoice or credit note -- the party (picked as typed; its terms
//     give the due date, its situation the lines' VAT codes), the dates, the references (a sale's
//     structured communication made from its number), the lines (an account, a description, the amount
//     excluding VAT, the VAT code, the VAT -- computed, or as the invoice says it), the totals and the
//     entry it makes, shown as it is typed;
//   * StatementPage: a bank or cash statement -- its old balance (the journal's), the new one, the
//     movements (a party or an account, a description, the amount); a movement's party's open items
//     listed below, those it pays ticked (its amount follows them; an amount typed ticks the one item
//     that is as much; a structured communication typed finds its invoice), matched when saved;
//   * MiscPage: a miscellaneous operation -- its lines' accounts, parties, debits and credits, until they
//     balance (the opening balances: one flagged so).
// Ctrl+S saves (the menu's), Esc cancels (asking when something was typed).
//
#ifndef _ledger_docs_h
#define _ledger_docs_h

#include "editgrid.h"
#include "print.h"

namespace lg {

// ---- what the editors share ------------------------------------------------------------------------------------------------------
class DocPage;
static DocPage *g_doc;				// the document page shown (its fields' changes mark it)
static void mark_dirty (Widget &);

class DocPage : public Page
{
public:
	int back;					// the page it goes back to
	bool dirty;
	char head[96];					// its title
	FlatButton *bSave, *bSaveNew, *bCancel, *bDelete;
	DocPage (int id_) : Page (id_), back (P_OVERVIEW), dirty (false), bSave (0), bSaveNew (0), bCancel (0), bDelete (0) { head[0] = '\0'; }
	const char *title () override { return head; }
	virtual bool save () = 0;			// posted (true), or refused (a message said why)
	virtual bool isNew () = 0;
	virtual void del () {}
	virtual EditGrid *grid () = 0;
	void buttons (void (*onSave) (), void (*onSaveNew) (), void (*onCancel) (), void (*onDel) ())
	{
		HeadRow h (this);
		bSave = h.add ("Save", onSave, FB_PRIMARY, NI_CHECK, "Save the document and go back to the list (Ctrl+S)");
		if (onSaveNew) bSaveNew = h.add ("Save & New", onSaveNew, FB_SECONDARY, -1, "Save it and type the next one");
		bCancel = h.add ("Cancel", onCancel, FB_SECONDARY, -1, "Leave it without saving (Esc)");
		h.gap (10);
		bDelete = h.add ("", onDel, FB_DANGER, NI_TRASH, "Delete the document...");
	}
	bool leave () override
	{
		if (!dirty) return true;
		int r = ask ("Unsaved document", "This document has changes not saved. Save it now?", MB_YESNOCANCEL, 1);
		if (r == 0) return false;
		if (r == 1) return save ();
		dirty = false;
		return true;
	}
	void close () { dirty = false; go (back); }
	void cancel ()
	{
		if (grid ()) grid ()->cancelEditing ();
		if (dirty && ask ("Cancel", "Leave this document without saving its changes?", MB_YESNO, 1) != 1) return;
		close ();
	}
	void tick () override { if (grid ()) grid ()->tick (); }
	// Tab / Shift+Tab: the next / previous field (the grid's first cell among them).
	void focusNext (int dir)
	{
		Widget *list[40]; int n = 0, cur = -1;
		for (Widget *c = firstChild; c && n < 40; c = c->nextSib)
			if (c->canFocus && !c->hidden && !c->disabled) { if (c->hasFocus) cur = n; list[n++] = c; }
		if (!n) return;
		int i = cur < 0 ? 0 : (cur + dir + n) % n;
		if (list[i] == grid ()) grid ()->focusIn (); else list[i]->setFocus ();
		if (grid () && list[i] != grid ()) grid ()->stopEditing ();
		invalidate (true);
	}
	bool onKey (long k) override
	{
		if (k == KEY_TAB) { focusNext ((kapi_get_modifiers () & MOD_SHIFT) ? -1 : 1); return true; }
		if (k == KEY_ENTER) { focusNext (1); return true; }
		if (k == 27) { cancel (); return true; }
		return false;
	}
	// A label at the left of a field.
	void lab (int x, int y, const char *s) { wk_text_l (canvas, x, y, ED_H, s, C_TEXT); }
};
static void mark_dirty (Widget &) { if (g_doc) g_doc->dirty = true; }

// A line typed: a money cell's text -> cents ("" and "0": 0).
static const char *put_money (const char *text, money *out)
{
	money v;
	if (!parse_money (text, &v)) return "Type an amount, as 1.234,56.";
	*out = v;
	return "";
}
static void edit_money (money v, char *out) { if (!v) out[0] = '\0'; else fmt_money (v, out, false); }
static void show_money (money v, char *out) { if (!v) out[0] = '\0'; else fmt_money (v, out); }
// An account typed or picked: its code (false: none such -- why).
static const char *resolve_account (const char *text, int filter, char *code)
{
	char t[64]; trim_copy (t, text, sizeof t);
	code[0] = '\0';
	if (!t[0]) return "";
	char c[CODE_MAX]; int k = 0; while (t[k] && t[k] != ' ' && k < CODE_MAX - 1) { c[k] = t[k]; k++; } c[k] = '\0';
	if (acc_postable (g_b, c)) { scpy (code, c, CODE_MAX); }
	else
	{
		Sug s[2]; int n = sug_accounts (g_b, t, filter, 0, s, 2);
		if (n != 1) return n ? "Several accounts match: choose one from the list." : "No such account: choose one from the list (or add it in the chart).";
		scpy (code, s[0].value, CODE_MAX);
	}
	if ((filter & AF_NOPARTY) && acc_party (g_b, code)) return "That is a customers' or suppliers' account: choose a revenue, expense or other account.";
	return "";
}
// A party typed or picked ("P12", its name, its code): its id (0 none).
static const char *resolve_party (const char *text, int kind, int *id)
{
	char t[80]; trim_copy (t, text, sizeof t);
	*id = 0;
	if (!t[0]) return "";
	if (t[0] == 'P' && all_digits (t + 1)) { int i = cell_int (t + 1); if (party_of (g_b, i)) { *id = i; return ""; } }
	for (int i = 0; i < g_b.npty; i++) if ((kind < 0 || g_b.pty[i].kind == kind) && (ci_eq (g_b.pty[i].name, t) || ci_eq (g_b.pty[i].code, t))) { *id = g_b.pty[i].id; return ""; }
	Sug s[2]; int n = sug_parties (g_b, t, kind, s, 2);
	if (n == 1) { *id = cell_int (s[0].value + 1); return ""; }
	return n ? "Several parties match: choose one from the list." : "No such party: choose one from the list (or add it: the + button).";
}
static const char *vat_label (int v) { return v >= 0 && v < NVAT ? VAT_DEFS[v].code : ""; }
static const char *resolve_vat (const char *text, int side, int *v)
{
	char t[16]; trim_copy (t, text, sizeof t);
	if (!t[0] || seq (t, "-") || ci_eq (t, "(none)")) { *v = -1; return ""; }
	int i = vat_find (t);
	if (i < 0) { Sug s[2]; int n = sug_vat (t, side, s, 2); if (n == 1 && !seq (s[0].value, "-")) i = vat_find (s[0].value); }
	if (i < 0) return "No such VAT code: choose one from the list.";
	if (side >= 0 && VAT_DEFS[i].side != side && VAT_DEFS[i].side != VS_OTHER) return side == VS_SALES ? "That is a purchases' VAT code." : "That is a sales' VAT code.";
	*v = i;
	return "";
}
// "VEN 2026/0012" of an entry by id.
static void doc_ref (int id, char *out, int cap)
{
	int i = entry_index (g_b, id);
	if (i < 0) { scpy (out, "?", cap); return; }
	entry_ref (g_b, g_b.e[i], out, cap);
}
// The journals of a type (their names for a choice box) -> how many; their indexes in idx.
struct JournalChoice
{
	const char *names[MAXJOURNALS]; char buf[MAXJOURNALS][48]; int idx[MAXJOURNALS]; int n;
	void fill (int type, int type2 = -1, bool all = false)
	{
		n = 0;
		for (int i = 0; i < g_b.njr; i++)
		{
			const Journal &j = g_b.jr[i];
			if (!all && ((j.type != type && j.type != type2) || j.hidden)) continue;
			scpy (buf[n], j.code, sizeof buf[n]); scat (buf[n], "  ", sizeof buf[n]); scat (buf[n], j.name, sizeof buf[n]);
			names[n] = buf[n]; idx[n] = i; n++;
		}
	}
	int find (int j) const { for (int i = 0; i < n; i++) if (idx[i] == j) return i; return 0; }
};

// =====================================================================================================================================
// ---- the invoice ------------------------------------------------------------------------------------------------------------------
// =====================================================================================================================================
class InvoicePage;
static InvoicePage *g_inv;

class InvoicePage : public DocPage, public EGModel
{
public:
	Invoice v; bool sale; int prevParty;
	int fromCdoc;					// the quote / order / delivery note it invoices (commerce.h)
	PickEdit *party; FlatButton *bParty, *bPrint;
	DateEdit *date, *due; LineEdit *ref, *comm, *text;
	Segmented *kind; ChoiceBox *jbox; JournalChoice jc;
	EditGrid *g;
	Entry preview; money tNet, tTax, tTot, tRev;
	InvoicePage () : DocPage (E_INVOICE), sale (true), prevParty (0), fromCdoc (0)
	{
		inv_init (v); entry_init (preview); tNet = tTax = tTot = tRev = 0;
		resizeTo (800, 660);
		buttons (s_save, s_saveNew, s_cancel, s_del);
		bPrint = new FlatButton ("Print", s_print, FB_SECONDARY, NI_PRINT); bPrint->left = bDelete->left - 8 - bPrint->width; bPrint->top = bSave->top;
		bPrint->anchor = ANCHOR_RIGHT | ANCHOR_TOP; bPrint->tip = "The invoice made by Writer from its template"; addChild (bPrint);
		int W = width, rx = W - 330;
		party = new PickEdit (130, 72, rx - 130 - 60, SK_PARTY, PK_CUSTOMER); party->anchor = ANCHOR_LEFT | ANCHOR_TOP | ANCHOR_RIGHT;
		party->placeholder = "Type a name, a code, a VAT number"; party->onPick = on_party; party->onChange = mark_dirty; addChild (party);
		bParty = new FlatButton ("", s_newParty, FB_SECONDARY, NI_PLUS); bParty->left = rx - 52; bParty->top = 70; bParty->anchor = ANCHOR_RIGHT | ANCHOR_TOP;
		bParty->resizeTo (34, 30); bParty->tip = "A new party (its card)"; addChild (bParty);
		date = new DateEdit (rx + 110, 72, 150); date->anchor = ANCHOR_RIGHT | ANCHOR_TOP; date->onChange = on_date; addChild (date);
		due = new DateEdit (rx + 110, 104, 150); due->anchor = ANCHOR_RIGHT | ANCHOR_TOP; due->onChange = mark_dirty; addChild (due);
		text = new LineEdit (130, 136, rx - 130 - 18); text->anchor = ANCHOR_LEFT | ANCHOR_TOP | ANCHOR_RIGHT; text->onChange = mark_dirty;
		text->placeholder = "What the invoice is for"; addChild (text);
		static const char *const KINDS[2] = { "Invoice", "Credit note" };
		kind = new Segmented (rx + 110, 136, KINDS, 2, on_kind); kind->anchor = ANCHOR_RIGHT | ANCHOR_TOP; addChild (kind);
		comm = new LineEdit (130, 168, 230); comm->onChange = mark_dirty; addChild (comm);
		ref = new LineEdit (rx + 110, 168, 200); ref->anchor = ANCHOR_RIGHT | ANCHOR_TOP; ref->onChange = mark_dirty; addChild (ref);
		jbox = new ChoiceBox (rx + 110, 200, 200); jbox->anchor = ANCHOR_RIGHT | ANCHOR_TOP; jbox->onChange = on_journal; addChild (jbox);
		g = new EditGrid (16, 238, W - 32, height - 238 - 150, this);
		g->anchor = ANCHOR_FILL;
		g->addCol ("Account", 200, EK_ACCOUNT, false, AF_NOPARTY, "7");
		g->addCol ("Description", 190, EK_TEXT);
		g->addCol ("Excl. VAT", 104, EK_MONEY, true);
		g->addCol ("VAT code", 84, EK_VAT, false, VS_SALES);
		g->addCol ("VAT", 90, EK_MONEY, true);
		g->addCol ("Total", 104, EK_READ, true);
		g->flex = 0;
		addChild (g);
	}
	~InvoicePage () { inv_free (v); entry_free (preview); }
	EditGrid *grid () override { return g; }
	bool isNew () override { return v.id == 0; }
	// ---- loading ----
	void setup (int journal, bool credit)
	{
		sale = g_b.jr[journal].type == JT_SALES;
		party->sug.arg = sale ? PK_CUSTOMER : PK_SUPPLIER;
		g->col[0].prefer = sale ? "70" : "6";
		g->col[3].sugArg = sale ? VS_SALES : VS_PURCH;
		jc.fill (sale ? JT_SALES : JT_PURCH);
		jbox->setOptions (jc.names, jc.n); jbox->sel = jc.find (journal); jbox->hidden = jc.n < 2;
		kind->set (credit ? 1 : 0);
		comm->placeholder = sale ? "Made when saved" : "+++123/4567/89012+++";
		ref->placeholder = sale ? "Order, your reference" : "The supplier's invoice number";
		party->placeholder = sale ? "The customer: a name, a code, a VAT number" : "The supplier: a name, a code, a VAT number";
		g->reset ();
	}
	void fields ()
	{
		party->setValue (v.party ? "" : ""); if (v.party) { char pv[16] = "P"; scat_num (pv, v.party, sizeof pv); party->setValue (pv); }
		char d[16]; date_show (v.date, d); date->setText (d);
		date_show (v.due, d); due->setText (d);
		ref->setText (v.ref); text->setText (v.text);
		char c[24]; if (v.comm[0]) ogm_show (v.comm, c); else c[0] = '\0';
		comm->setText (c[0] ? c : v.comm);
		prevParty = v.party;
		titleFor ();
	}
	void titleFor ()
	{
		char r[40];
		if (v.id) { doc_ref (v.id, r, sizeof r); scpy (head, sale ? (v.credit ? "Sales credit note " : "Sales invoice ") : (v.credit ? "Purchase credit note " : "Purchase invoice "), sizeof head); scat (head, r, sizeof head); }
		else scpy (head, sale ? (v.credit ? "New sales credit note" : "New sales invoice") : (v.credit ? "New purchase credit note" : "New purchase invoice"), sizeof head);
		invalidate (true);
	}
	void startNew (int journal, bool credit)
	{
		inv_free (v); inv_init (v);
		v.journal = journal; v.credit = credit; v.date = default_date ();
		setup (journal, credit);
		inv_add (v);
		fields ();
		recalc ();
		bDelete->hidden = true; bPrint->hidden = true;
		fromCdoc = 0;
		dirty = false;
	}
	bool load (const Entry &e)
	{
		inv_free (v); inv_init (v);
		if (!inv_from_entry (g_b, e, v)) return false;
		setup (v.journal, v.credit);
		if (!v.nl) inv_add (v);
		fields ();
		recalc ();
		bDelete->hidden = false; bPrint->hidden = !sale;
		fromCdoc = 0;
		dirty = false;
		return true;
	}
	// A credit note for an invoice: its lines, its party.
	void creditFor (const Entry &e)
	{
		Invoice src; inv_init (src);
		inv_from_entry (g_b, e, src);
		startNew (e.journal, true);
		inv_free (v); v = src; src.l = 0;
		v.id = 0; v.no = 0; v.credit = true; v.date = default_date (); v.due = v.date; v.comm[0] = '\0';
		char r[40]; entry_ref (g_b, e, r, sizeof r);
		static const char *const FOR[3] = { "Note de cr\xE9" "dit pour ", "Creditnota voor ", "Credit note for " };
		scpy (v.text, FOR[doc_lang (e.party)], sizeof v.text); scat (v.text, r, sizeof v.text);
		setup (v.journal, true);
		fields (); recalc ();
		dirty = true;
	}
	// ---- the totals, the entry it makes ----
	void recalc ()
	{
		inv_totals (v, &tNet, &tTax, &tTot, &tRev);
		entry_free (preview);
		Invoice t = v; t.party = v.party;
		inv_to_entry (g_b, t, preview);
		invalidate (true);
	}
	void subtitle (char *out, int cap) override
	{
		out[0] = '\0';
		if (!v.id) { scpy (out, sale ? "Type the customer, the dates, the lines; Save posts it." : "Type the supplier, the dates, the lines; Save posts it.", cap); return; }
		int i = entry_index (g_b, v.id);
		if (i < 0) return;
		const Entry &e = g_b.e[i];
		const char *lk = entry_locked (g_b, e);
		if (lk[0]) { scpy (out, "Locked: ", cap); scat (out, lk, cap); return; }
		for (int k = 0; k < e.nl; k++)
			if (e.l[k].party && acc_party (g_b, e.l[k].account))
			{
				if (e.l[k].match) { scpy (out, "Paid (matched)", cap); return; }
				char d[16]; date_show (e.l[k].due ? e.l[k].due : e.date, d);
				scpy (out, (e.l[k].due ? e.l[k].due : e.date) < today_ymd () ? "Open, overdue since " : "Open, due on ", cap); scat (out, d, cap);
				return;
			}
	}
	// ---- the lines (EGModel) ----
	int rows () override { return v.nl; }
	void cell (int r, int c, bool edit, char *out, int cap) override
	{
		out[0] = '\0';
		if (r < 0 || r >= v.nl) return;
		const InvLine &l = v.l[r];
		switch (c)
		{
		case 0: scpy (out, l.account, cap); if (!edit && l.account[0]) { scat (out, "  ", cap); scat (out, acc_name (g_b, l.account), cap); } break;
		case 1: scpy (out, l.text, cap); break;
		case 2: if (edit) edit_money (l.net, out); else show_money (l.net, out); break;
		case 3: scpy (out, vat_label (l.vat), cap); if (!edit && l.vat >= 0 && VAT_DEFS[l.vat].rate) { char rt[16]; fmt_rate (VAT_DEFS[l.vat].rate, rt); scat (out, "  ", cap); scat (out, rt, cap); } break;
		case 4: { money t = inv_tax (l); if (edit) edit_money (t, out); else show_money (t, out); if (!edit && vat_reverse (l.vat) && t) scat (out, " *", cap); break; }
		case 5: { money t = inv_tax (l); money tot = l.net + (vat_reverse (l.vat) ? 0 : t); show_money (tot, out); break; }
		}
	}
	unsigned ink (int r, int c) override
	{
		if (c == 4 && r < v.nl && v.l[r].taxSet) return C_BLUE;		// (typed, not computed)
		return 0;
	}
	const Party *pty () { return party_of (g_b, v.party); }
	const char *put (int r, int c, const char *t) override
	{
		InvLine &l = v.l[r];
		switch (c)
		{
		case 0:
		{
			char code[CODE_MAX]; const char *w = resolve_account (t, AF_NOPARTY, code);
			if (w[0]) return w;
			bool had = l.account[0] != 0;
			int oldDef = had ? vat_default (g_b, pty (), sale, l.account) : -2;
			scpy (l.account, code, CODE_MAX);
			if (l.vat < 0 || l.vat == oldDef) l.vat = vat_default (g_b, pty (), sale, l.account);
			break;
		}
		case 1: scpy (l.text, t, sizeof l.text); break;
		case 2:
		{
			money m; const char *w = put_money (t, &m); if (w[0]) return w;
			l.net = m;
			if (!l.account[0]) defaults (l);
			break;
		}
		case 3:
		{
			int x; const char *w = resolve_vat (t, sale ? VS_SALES : VS_PURCH, &x); if (w[0]) return w;
			l.vat = x; l.taxSet = false;
			break;
		}
		case 4:
		{
			money m; const char *w = put_money (t, &m); if (w[0]) return w;
			InvLine tmp = l; tmp.taxSet = false;
			if (m == inv_tax (tmp)) l.taxSet = false; else { l.taxSet = true; l.tax = m; }
			break;
		}
		}
		dirty = true;
		return "";
	}
	bool editable (int r, int c) override { return c != 4 || (r < v.nl && v.l[r].vat >= 0); }
	void defaults (InvLine &l)
	{
		const Party *p = pty ();
		if (!l.account[0])
		{
			if (p && p->defAcc[0] && acc_postable (g_b, p->defAcc)) scpy (l.account, p->defAcc, CODE_MAX);
			else if (v.nl > 1 && v.l[0].account[0] && &l != &v.l[0]) scpy (l.account, v.l[0].account, CODE_MAX);
			else if (sale && acc_postable (g_b, "700000")) scpy (l.account, "700000", CODE_MAX);
		}
		if (l.vat < 0 && l.account[0]) l.vat = vat_default (g_b, p, sale, l.account);
	}
	void add () override
	{
		InvLine &l = inv_add (v);
		const Party *p = pty ();
		if (v.nl > 1) { scpy (l.account, v.l[v.nl - 2].account, CODE_MAX); l.vat = v.l[v.nl - 2].vat; }
		else if (p) defaults (l);
	}
	void remove (int r) override { inv_remove (v, r); dirty = true; }
	bool blank (int r) override { const InvLine &l = v.l[r]; return !l.net && !l.text[0] && !l.taxSet; }
	void linesChanged () override { dirty = true; recalc (); }
	// ---- the header's fields ----
	void partyChosen ()
	{
		int id = 0;
		if (party->value ()[0] == 'P') id = cell_int (party->value () + 1);
		if (id == v.party) return;
		const Party *old = party_of (g_b, v.party), *p = party_of (g_b, id);
		// the lines' codes that were the old party's defaults: the new one's
		for (int i = 0; i < v.nl; i++)
		{
			InvLine &l = v.l[i];
			if (!l.account[0]) continue;
			if (l.vat < 0 || l.vat == vat_default (g_b, old, sale, l.account)) { l.vat = vat_default (g_b, p, sale, l.account); l.taxSet = false; }
		}
		v.party = id;
		if (p)
		{
			int d = date_parse (date->text ());
			if (d) { char t[16]; date_show (date_add (d, p->terms), t); due->setText (t); }
			if (v.nl == 1 && blank (0) && !v.l[0].account[0]) defaults (v.l[0]);
		}
		dirty = true;
		recalc ();
		g->invalidate (true);
	}
	void dateChosen ()
	{
		int d = date_parse (date->text ());
		const Party *p = pty ();
		if (d && (p || !due->text ()[0]))
		{
			char t[16]; date_show (date_add (d, p ? p->terms : 30), t); due->setText (t);
		}
		dirty = true;
	}
	// ---- saving ----
	bool read (const char **why)
	{
		*why = "";
		if (!party->resolve ()) { *why = "Choose the party from the list (or add it: the + button)."; party->setError (true); return false; }
		partyChosen ();
		int d = date_parse (date->text ());
		if (!d) { *why = "Type the date, as 29/09/2026."; date->setError (true); return false; }
		v.date = d;
		v.due = due->text ()[0] ? date_parse (due->text ()) : d;
		if (!v.due) { *why = "Type the due date, as 29/09/2026 (or leave it empty)."; due->setError (true); return false; }
		scpy (v.ref, ref->text (), sizeof v.ref);
		scpy (v.text, text->text (), sizeof v.text);
		char c[16]; char t[40]; trim_copy (t, comm->text (), sizeof t);
		if (!t[0]) v.comm[0] = '\0';
		else if (ogm_parse (t, c)) scpy (v.comm, c, sizeof v.comm);
		else { *why = "The structured communication's check digits are wrong (+++123/4567/89012+++)."; comm->setError (true); return false; }
		v.journal = jc.n ? jc.idx[iclamp (jbox->sel, 0, jc.n - 1)] : v.journal;
		v.credit = kind->cur == 1;
		// the blank lines left out
		for (int i = v.nl - 1; i >= 0; i--) if (!v.l[i].net && !inv_tax (v.l[i]) && (blank (i) || !v.l[i].account[0])) inv_remove (v, i);
		return true;
	}
	bool save () override
	{
		if (!g->commit ()) return false;
		const char *why;
		if (!read (&why)) { if (!v.nl) inv_add (v); status (why); warn ("Save", why); return false; }
		if (v.id)
		{
			int i = entry_index (g_b, v.id);
			if (i >= 0) { const char *lk = entry_locked (g_b, g_b.e[i]); if (lk[0]) { warn ("Save", lk); return false; } }
		}
		const char *w = inv_check (g_b, v);
		if (w[0]) { if (!v.nl) inv_add (v); warn ("Save", w); return false; }
		if (!sale && v.ref[0])					// the same supplier's number twice?
			for (int i = 0; i < g_b.ne; i++)
			{
				const Entry &e = g_b.e[i];
				if (e.id != v.id && e.party == v.party && g_b.jr[e.journal].type == JT_PURCH && ci_eq (e.ref, v.ref) && year_of (g_b, e.date) == year_of (g_b, v.date))
				{
					char r[40], m[200]; entry_ref (g_b, e, r, sizeof r);
					scpy (m, "This supplier's invoice ", sizeof m); scat (m, v.ref, sizeof m); scat (m, " is already posted (", sizeof m); scat (m, r, sizeof m);
					scat (m, "). Post it again?", sizeof m);
					if (ask ("Save", m, MB_YESNO, 2) != 1) return false;
					break;
				}
			}
		Entry e; inv_to_entry (g_b, v, e);
		bool wasNew = !v.id;
		int i = entry_save (g_b, e);
		Entry &s = g_b.e[i];
		if (sale && !s.comm[0])					// (a sale's structured communication: its number's)
		{
			Invoice t; inv_init (t); t.date = s.date; t.no = s.no; t.journal = s.journal;
			inv_make_comm (g_b, t, s.comm);
		}
		v.id = s.id;
		if (fromCdoc)						// (the document it invoices: done)
		{
			int k = cdoc_index (g_b, fromCdoc);
			if (k >= 0) { g_b.cd[k].invoice = s.id; g_b.cd[k].status = CS_DONE; }
			fromCdoc = 0;
		}
		bPrint->hidden = !sale;
		char r[40]; entry_ref (g_b, s, r, sizeof r);
		char m[96]; scpy (m, v.credit ? "Credit note " : "Invoice ", sizeof m); scat (m, r, sizeof m); scat (m, wasNew ? " posted" : " saved", sizeof m);
		dirty = false;
		changed ();
		status (m);
		return true;
	}
	void del () override
	{
		int i = entry_index (g_b, v.id);
		if (i < 0) return;
		const char *w = entry_can_delete (g_b, i);
		if (w[0]) { warn ("Delete", w); return; }
		char r[40], m[160]; entry_ref (g_b, g_b.e[i], r, sizeof r);
		scpy (m, "Delete ", sizeof m); scat (m, r, sizeof m); scat (m, " for good? Its payments' matchings are undone.", sizeof m);
		if (ask ("Delete", m, MB_YESNO, 2) != 1) return;
		entry_delete (g_b, i);
		changed ();
		status ("Document deleted");
		close ();
	}
	void refresh () override
	{
		// (another party's name, a new account: what is shown follows)
		if (v.party) { char pv[16] = "P"; scat_num (pv, v.party, sizeof pv); if (seq (party->value (), pv)) party->setValue (pv); }
		titleFor ();
		recalc ();
	}
	void enter () override { if (!v.party) party->setFocus (); else g->focusIn (); }
	// ---- drawing ----
	void onDraw () override
	{
		drawHead ();
		int W = width, rx = W - 330;
		lab (20, 72, sale ? "Customer" : "Supplier");
		lab (rx, 72, "Date"); lab (rx, 104, "Due date"); lab (20, 136, "Description"); lab (rx, 136, "Kind");
		lab (20, 168, "Communication"); lab (rx, 168, sale ? "Reference" : "Their number");
		if (!jbox->hidden) lab (rx, 200, "Journal");
		const Party *p = pty ();
		if (p)							// the party's address, its VAT number
		{
			char t[200] = ""; scpy (t, p->street, sizeof t);
			if (p->zip[0] || p->city[0]) { if (t[0]) scat (t, ", ", sizeof t); scat (t, p->zip, sizeof t); scat (t, " ", sizeof t); scat (t, p->city, sizeof t); }
			if (p->vat[0]) { char vv[24]; vat_show (p->vat, vv, sizeof vv); if (t[0]) scat (t, "  \xB7  ", sizeof t); scat (t, vv, sizeof t); }
			else { if (t[0]) scat (t, "  \xB7  ", sizeof t); scat (t, REGIME_NAME[p->regime < PR_COUNT ? p->regime : 0], sizeof t); }
			text_fit_l (canvas, 132, 102, rx - 150, 26, t, dim_ink (C_BG));
		}
		{
			int d = date_parse (date->text ()), dd = date_parse (due->text ());
			if (d && dd && dd >= d) { char t[32]; itoa10 (days_between (d, dd), t); scat (t, " days", sizeof t); wk_text_l (canvas, rx + 270, 104, ED_H, t, dim_ink (C_BG)); }
		}
		// the foot: the entry at the left, the totals at the right
		int fy = wg (g)->top + g->height + 12, fh = height - fy - 8;
		int tx = W - 300;
		wk_rbox (canvas, tx, fy, 284, fh, 8, wk_tone (C_FIELD, 132), wk_tone (C_FIELD, 122));
		wk_rline (canvas, tx, fy, 284, fh, 8, wk_mix (C_BG, 0, 60), 100);
		char a[32];
		int y = fy + 8;
		wk_text_l (canvas, tx + 14, y, 20, "Total excl. VAT", field_dim ()); text_r (canvas, tx + 270, y, 20, money_s (tNet, a), C_FIELD_TEXT); y += 20;
		for (int c = 0; c < NVAT && y < fy + fh - 50; c++)		// the VAT by code
		{
			money base = 0, tax = 0; bool any = false;
			for (int i = 0; i < v.nl; i++) if (v.l[i].vat == c) { any = true; base += v.l[i].net; tax += inv_tax (v.l[i]); }
			if (!any || (!tax && !base)) continue;
			char t[48] = "VAT "; scat (t, VAT_DEFS[c].code, sizeof t);
			if (VAT_DEFS[c].rate) { char rt[16]; fmt_rate (VAT_DEFS[c].rate, rt); scat (t, " (", sizeof t); scat (t, rt, sizeof t); scat (t, ")", sizeof t); }
			if (vat_reverse (c)) scat (t, " reverse charge", sizeof t);
			wk_text_l (canvas, tx + 14, y, 20, t, field_dim ()); text_r (canvas, tx + 270, y, 20, money_s (tax, a), vat_reverse (c) ? field_dim () : C_FIELD_TEXT); y += 20;
		}
		canvas.fillRect (tx + 12, fy + fh - 40, 260, 1, wk_tone (C_FIELD, 100));
		wk_text_l (canvas, tx + 14, fy + fh - 36, 28, v.credit || kind->cur == 1 ? "Total to refund" : "Total to pay", C_FIELD_TEXT, 2);
		text_r (canvas, tx + 270, fy + fh - 36, 28, money_s (tTot, a), C_FIELD_TEXT, 2);
		// the entry
		int ex = 16, ew = tx - 16 - 16;
		wk_text_l (canvas, ex + 2, fy, 20, "The entry it makes", dim_ink (C_BG), 2);
		int ly = fy + 22, maxl = (fh - 24) / 18;
		for (int k = 0; k < preview.nl && k < maxl; k++)
		{
			const Line &l = preview.l[k];
			if (k == maxl - 1 && preview.nl > maxl) { wk_text_l (canvas, ex + 2, ly, 18, "...", dim_ink (C_BG)); break; }
			char t[160]; scpy (t, l.account, sizeof t); scat (t, "  ", sizeof t);
			scat (t, l.party ? party_name (g_b, l.party) : acc_name (g_b, l.account), sizeof t);
			unsigned ink = dim_ink (C_BG);
			text_fit_l (canvas, ex + 2 + (l.amount < 0 ? 24 : 0), ly, ew - 200, 18, t, ink);
			fmt_money (l.amount < 0 ? -l.amount : l.amount, a);
			text_r (canvas, ex + ew - (l.amount < 0 ? 0 : 100), ly, 18, a, C_TEXT);
			ly += 18;
		}
		if (preview.nl) { text_r (canvas, ex + ew - 100, fy, 20, "Debit", dim_ink (C_BG)); text_r (canvas, ex + ew, fy, 20, "Credit", dim_ink (C_BG)); }
	}
	// ---- the commands ----
	static void on_party (Widget &) { g_inv->partyChosen (); }
	static void on_date (Widget &) { g_inv->dateChosen (); }
	static void on_kind (int) { g_inv->v.credit = g_inv->kind->cur == 1; g_inv->dirty = true; g_inv->titleFor (); g_inv->recalc (); }
	static void on_journal (Widget &) { g_inv->dirty = true; }
	static void s_save () { if (g_inv->save ()) g_inv->close (); }
	static void s_saveNew () { if (g_inv->save ()) { int j = g_inv->v.journal; bool cr = g_inv->v.credit; g_inv->startNew (j, cr); g_inv->party->setFocus (); g_inv->sync (); } }
	static void s_cancel () { g_inv->cancel (); }
	static void s_del () { g_inv->del (); }
	static void s_print ()
	{
		if ((g_inv->dirty || !g_inv->v.id) && !g_inv->save ()) return;
		int i = entry_index (g_b, g_inv->v.id);
		if (i >= 0) print_invoice (g_b.e[i]);
	}
	static void s_newParty ();
};

// =====================================================================================================================================
// ---- the statement ------------------------------------------------------------------------------------------------------------------
// =====================================================================================================================================
class StatementPage;
static StatementPage *g_st;

class StatementPage : public DocPage, public EGModel
{
public:
	Statement s;
	ChoiceBox *jbox; JournalChoice jc;
	DateEdit *date; LineEdit *text, *newBal;
	EditGrid *g;
	TickGrid *items;				// the open items of the active movement's party
	LineRef open[400]; int nopen; int itemsFor;	// (their lines; the movement they are listed for)
	money oldBal;
	StatementPage () : DocPage (E_STATEMENT), jbox (0), date (0), text (0), newBal (0), g (0), items (0), nopen (0), itemsFor (-1), oldBal (0)
	{
		st_init (s);
		resizeTo (800, 660);
		buttons (s_save, s_saveNew, s_cancel, s_del);
		int W = width, rx = W - 330;
		jbox = new ChoiceBox (130, 72, 240); jbox->onChange = on_journal; addChild (jbox);
		date = new DateEdit (rx + 110, 72, 150); date->anchor = ANCHOR_RIGHT | ANCHOR_TOP; date->onChange = on_date; addChild (date);
		text = new LineEdit (130, 104, rx - 130 - 18); text->anchor = ANCHOR_LEFT | ANCHOR_TOP | ANCHOR_RIGHT; text->onChange = mark_dirty;
		text->placeholder = "Statement 12, the bank's reference..."; addChild (text);
		newBal = new LineEdit (rx + 110, 104, 150); newBal->anchor = ANCHOR_RIGHT | ANCHOR_TOP; newBal->accept = accept_dec; newBal->rightAlign = true;
		newBal->placeholder = "As the bank says"; newBal->onChange = on_newbal; addChild (newBal);
		int gh = (height - 176 - 44) * 55 / 100;
		g = new EditGrid (16, 176, W - 32, gh, this);
		g->anchor = ANCHOR_LEFT | ANCHOR_TOP | ANCHOR_RIGHT;
		g->addCol ("Party or account", 230, EK_TARGET, false, 0, "6");
		g->addCol ("Description", 200, EK_TEXT);
		g->addCol ("Amount", 116, EK_MONEY, true);
		g->addCol ("Pays", 170, EK_READ);
		g->flex = 0;
		g->addText = "Add a movement";
		addChild (g);
		int iy = 176 + gh + 34;
		items = new TickGrid (16, iy, W - 32, height - iy - 44);
		items->anchor = ANCHOR_FILL;
		items->setColumns (6);
		items->setColumn (0, "", 30); items->setColumn (1, "Date", 96); items->setColumn (2, "Document", 130);
		items->setColumn (3, "Description", 250); items->setColumn (4, "Due", 96); items->setColumn (5, "Amount", 110, GRID_RIGHT);
		items->cellText = item_text; items->cellDraw = item_draw; items->onActivate = on_item; items->onTickRow = on_tick;
		items->emptyText = "Choose a movement's party: its open invoices show here.";
		items->user = this;
		addChild (items);
		// (the grid's height follows the page's: the items take what the window grows)
	}
	~StatementPage () { st_free (s); }
	EditGrid *grid () override { return g; }
	bool isNew () override { return s.id == 0; }
	void setup (int journal)
	{
		jc.fill (JT_BANK, JT_CASH);
		jbox->setOptions (jc.names, jc.n); jbox->sel = jc.find (journal);
		g->reset ();
		itemsFor = -1; nopen = 0; items->setRows (0);
	}
	void fields ()
	{
		char d[16]; date_show (s.date, d); date->setText (d);
		text->setText (s.text);
		char b[32]; if (s.nl || s.now != s.old) fmt_money (s.now, b); else b[0] = '\0';
		newBal->setText (s.id ? b : "");
		titleFor ();
	}
	void titleFor ()
	{
		if (s.id) { char r[40]; doc_ref (s.id, r, sizeof r); scpy (head, "Statement ", sizeof head); scat (head, r, sizeof head); }
		else scpy (head, g_b.jr[s.journal].type == JT_CASH ? "New cash statement" : "New bank statement", sizeof head);
		invalidate (true);
	}
	void computeOld () { oldBal = fin_balance_before (g_b, s.journal, s.date ? s.date : default_date (), s.id); s.old = oldBal; }
	void startNew (int journal)
	{
		st_free (s); st_init (s);
		s.journal = journal; s.date = default_date ();
		setup (journal);
		// its description: the next statement's number
		int n = 1; for (int i = 0; i < g_b.ne; i++) if (g_b.e[i].journal == journal && year_of (g_b, g_b.e[i].date) == year_of (g_b, s.date)) n++;
		char t[48] = "Statement "; scat_num (t, n, sizeof t); scpy (s.text, t, sizeof s.text);
		st_add (s);
		computeOld ();
		fields ();
		bDelete->hidden = true;
		dirty = false;
	}
	bool load (const Entry &e)
	{
		st_free (s); st_init (s);
		if (!st_from_entry (g_b, e, s)) return false;
		setup (s.journal);
		if (!s.nl) st_add (s);
		oldBal = s.old;
		fields ();
		bDelete->hidden = false;
		dirty = false;
		return true;
	}
	void subtitle (char *out, int cap) override
	{
		char a[32], b[32];
		scpy (out, "Old balance ", cap); scat (out, money_s (s.old, a), cap);
		scat (out, "  \xB7  movements ", cap); money m = st_sum (s); if (m > 0) scat (out, "+", cap); scat (out, money_s (m, b), cap);
	}
	// ---- the movements (EGModel) ----
	int rows () override { return s.nl; }
	void cell (int r, int c, bool edit, char *out, int cap) override
	{
		out[0] = '\0';
		if (r < 0 || r >= s.nl) return;
		const StLine &l = s.l[r];
		switch (c)
		{
		case 0:
			if (l.party) { const Party *p = party_of (g_b, l.party); if (p) scpy (out, p->name, cap); }
			else if (l.account[0]) { scpy (out, l.account, cap); if (!edit) { scat (out, "  ", cap); scat (out, acc_name (g_b, l.account), cap); } }
			break;
		case 1: scpy (out, l.text, cap); break;
		case 2: if (edit) edit_money (l.amount, out); else if (l.amount) { fmt_money (l.amount, out); if (l.amount > 0) { char t[40] = "+"; scat (t, out, sizeof t); scpy (out, t, cap); } } break;
		case 3:
			if (l.npay)
			{
				doc_ref (l.pay[0].entry, out, cap);
				if (l.npay > 1) { scat (out, " +", cap); scat_num (out, l.npay - 1, cap); }
			}
			else if (l.match) scpy (out, "(matched)", cap);
			break;
		}
	}
	unsigned ink (int r, int c) override
	{
		if (c == 2 && r < s.nl) return s.l[r].amount < 0 ? C_BAD : s.l[r].amount > 0 ? C_GOOD : 0;
		if (c == 0 && r < s.nl && s.l[r].party) return C_FIELD_TEXT;
		return 0;
	}
	const char *put (int r, int c, const char *t) override
	{
		StLine &l = s.l[r];
		switch (c)
		{
		case 0:
		{
			char x[80]; trim_copy (x, t, sizeof x);
			int pid = 0; char code[CODE_MAX] = "";
			if (!x[0]) { l.party = 0; l.account[0] = '\0'; l.npay = 0; break; }
			if (x[0] == 'P' && all_digits (x + 1) && party_of (g_b, cell_int (x + 1))) pid = cell_int (x + 1);
			else if (x[0] == 'A' && all_digits (x + 1)) { if (!acc_postable (g_b, x + 1)) return "No such account."; scpy (code, x + 1, CODE_MAX); }
			else if (digit (x[0])) { const char *w = resolve_account (x, AF_NOPARTY, code); if (w[0]) return w; }
			else
			{
				const char *w = resolve_party (x, -1, &pid);
				if (w[0]) { char c2[CODE_MAX]; if (resolve_account (x, AF_NOPARTY, c2)[0]) return w; scpy (code, c2, CODE_MAX); }
			}
			if (pid && acc_party (g_b, code)) code[0] = '\0';
			if (pid != l.party) l.npay = 0;
			l.party = pid; scpy (l.account, pid ? "" : code, CODE_MAX);
			if (!l.text[0] && pid) scpy (l.text, party_name (g_b, pid), sizeof l.text);
			listItems (r, true);
			break;
		}
		case 1:
		{
			scpy (l.text, t, sizeof l.text);
			// a structured communication in it: its sale found (the party, the item, the amount)
			char d12[16];
			for (const char *q = t; *q; q++)
			{
				if (!digit (*q) && *q != '+') continue;
				char win[40]; int k = 0; for (const char *z = q; *z && k < 39 && (digit (*z) || *z == '+' || *z == '/' || *z == ' '); z++) win[k++] = *z;
				win[k] = '\0';
				if (!ogm_parse (win, d12)) continue;
				for (int i = 0; i < g_b.ne; i++)
				{
					const Entry &e = g_b.e[i];
					if (!seq (e.comm, d12)) continue;
					for (int x = 0; x < e.nl; x++)
						if (e.l[x].party && acc_party (g_b, e.l[x].account) && !e.l[x].match)
						{
							if (!l.party && !l.account[0]) l.party = e.l[x].party;
							if (l.party == e.l[x].party && !payHas (l, e.id, x) && l.npay < MAXPAY)
							{
								l.pay[l.npay].entry = e.id; l.pay[l.npay].line = x; l.npay++;
								if (!l.amount) l.amount = e.l[x].amount;
								status ("Its invoice found by the structured communication");
							}
						}
					break;
				}
				break;
			}
			listItems (r, true);
			break;
		}
		case 2:
		{
			money m; const char *w = put_money (t, &m); if (w[0]) return w;
			l.amount = m;
			if (l.party && !l.npay && m)			// the one open item as much: ticked
			{
				int hit = -1, hits = 0;
				listItems (r, true);
				for (int i = 0; i < nopen; i++) if (g_b.e[open[i].e].l[open[i].l].amount == m && !paidElsewhere (open[i], r)) { hit = i; hits++; }
				if (hits == 1) { l.pay[0].entry = g_b.e[open[hit].e].id; l.pay[0].line = open[hit].l; l.npay = 1; }
			}
			listItems (r, true);
			break;
		}
		}
		dirty = true;
		return "";
	}
	bool payHas (const StLine &l, int entryId, int line) { for (int i = 0; i < l.npay; i++) if (l.pay[i].entry == entryId && l.pay[i].line == line) return true; return false; }
	// Is that item already paid by another movement of this statement?
	bool paidElsewhere (const LineRef &r, int except)
	{
		int id = g_b.e[r.e].id;
		for (int i = 0; i < s.nl; i++) if (i != except && payHas (s.l[i], id, r.l)) return true;
		return false;
	}
	bool editable (int r, int c) override { (void) r; return c != 3; }
	void add () override
	{
		StLine &l = st_add (s);
		(void) l;
	}
	void remove (int r) override { st_remove (s, r); itemsFor = -1; listItems (g->cur >= s.nl ? s.nl - 1 : g->cur, true); dirty = true; }
	bool blank (int r) override { const StLine &l = s.l[r]; return !l.amount && !l.party && !l.account[0] && !l.text[0]; }
	void linesChanged () override { dirty = true; invalidate (true); }
	void rowFocused (int r) override { listItems (r, false); }
	// ---- the open items ----
	void listItems (int r, bool force)
	{
		if (!force && r == itemsFor) return;
		itemsFor = r;
		nopen = 0;
		if (r >= 0 && r < s.nl && s.l[r].party)
		{
			const StLine &l = s.l[r];
			for (int i = 0; i < g_b.ne && nopen < 400; i++)
			{
				const Entry &e = g_b.e[i];
				if (e.id == s.id) continue;
				for (int k = 0; k < e.nl && nopen < 400; k++)
				{
					const Line &x = e.l[k];
					if (x.party != l.party || !acc_party (g_b, x.account) || !x.amount) continue;
					bool mine = payHas (l, e.id, k);
					if (x.match && !mine) continue;
					open[nopen].e = i; open[nopen].l = k; nopen++;
				}
			}
		}
		items->setRows (nopen);
		invalidate (true);
	}
	static const char *item_text (DataGrid &gr, int row, int col, char *buf, int cap)
	{
		StatementPage *p = (StatementPage *) gr.user;
		if (row >= p->nopen) return "";
		const Entry &e = g_b.e[p->open[row].e]; const Line &l = e.l[p->open[row].l];
		switch (col)
		{
		case 1: date_show (e.date, buf); return buf;
		case 2: entry_ref (g_b, e, buf, cap); return buf;
		case 3: scpy (buf, l.text[0] ? l.text : e.text, cap); if (e.ref[0]) { scat (buf, "  (", cap); scat (buf, e.ref, cap); scat (buf, ")", cap); } return buf;
		case 4: date_show (l.due ? l.due : e.date, buf); return buf;
		case 5: fmt_money (l.amount, buf); return buf;
		}
		return "";
	}
	static bool item_draw (DataGrid &gr, Canvas &cv, int row, int col, int x, int y, int w, int h, unsigned ink, bool sel)
	{
		StatementPage *p = (StatementPage *) gr.user;
		if (row >= p->nopen) return false;
		int r = p->itemsFor;
		if (col == 0)
		{
			bool on = r >= 0 && r < p->s.nl && p->payHas (p->s.l[r], g_b.e[p->open[row].e].id, p->open[row].l);
			bool else_ = p->paidElsewhere (p->open[row], r);
			wk_check_mark (cv, x + (w - 16) / 2, y + (h - 16) / 2, 16, on || else_, else_ ? WK_DISABLED : WK_NORMAL);
			return true;
		}
		if (col == 5)
		{
			const Line &l = g_b.e[p->open[row].e].l[p->open[row].l];
			char t[32]; fmt_money (l.amount, t);
			cell_text (cv, x, y, w, h, t, sel ? ink : (l.amount < 0 ? C_BAD : ink), true);
			return true;
		}
		if (col == 4 && !sel)
		{
			const Entry &e = g_b.e[p->open[row].e]; const Line &l = e.l[p->open[row].l];
			int d = l.due ? l.due : e.date;
			char t[16]; date_show (d, t);
			cell_text (cv, x, y, w, h, t, d < p->s.date ? C_BAD : ink, false);
			return true;
		}
		return false;
	}
	// An item ticked or not: the movement's amount follows the items ticked (when it was theirs, or none).
	void toggle (int row)
	{
		int r = itemsFor;
		if (r < 0 || r >= s.nl || row < 0 || row >= nopen) return;
		if (g->editing ()) g->commit ();
		StLine &l = s.l[r];
		if (paidElsewhere (open[row], r)) { status ("Another movement of this statement pays it"); return; }
		money before = 0; for (int i = 0; i < l.npay; i++) { int x = entry_index (g_b, l.pay[i].entry); if (x >= 0) before += g_b.e[x].l[l.pay[i].line].amount; }
		int id = g_b.e[open[row].e].id, ln = open[row].l;
		bool had = false;
		for (int i = 0; i < l.npay; i++) if (l.pay[i].entry == id && l.pay[i].line == ln) { for (int k = i + 1; k < l.npay; k++) l.pay[k - 1] = l.pay[k]; l.npay--; had = true; break; }
		if (!had)
		{
			if (l.npay >= MAXPAY) { status ("A movement pays twelve items at most"); return; }
			l.pay[l.npay].entry = id; l.pay[l.npay].line = ln; l.npay++;
		}
		money after = 0; for (int i = 0; i < l.npay; i++) { int x = entry_index (g_b, l.pay[i].entry); if (x >= 0) after += g_b.e[x].l[l.pay[i].line].amount; }
		if (!l.amount || l.amount == before) l.amount = after;
		dirty = true;
		items->invalidate (true); g->invalidate (true);
		invalidate (true);
	}
	static void on_item (Widget &w) { StatementPage *p = (StatementPage *) ((DataGrid &) w).user; p->toggle (((DataGrid &) w).sel); }
	static void on_tick (Widget &w) { StatementPage *p = (StatementPage *) ((DataGrid &) w).user; p->toggle (((TickGrid &) w).tickRow); }
	// ---- saving ----
	bool save () override
	{
		if (!g->commit ()) return false;
		int d = date_parse (date->text ());
		if (!d) { date->setError (true); warn ("Save", "Type the statement's date, as 29/09/2026."); return false; }
		s.date = d;
		s.journal = jc.n ? jc.idx[iclamp (jbox->sel, 0, jc.n - 1)] : s.journal;
		scpy (s.text, text->text (), sizeof s.text);
		for (int i = s.nl - 1; i >= 0; i--) if (blank (i) || (!s.l[i].amount && !s.l[i].party && !s.l[i].account[0])) st_remove (s, i);
		s.old = fin_balance_before (g_b, s.journal, s.date, s.id);
		money sum = st_sum (s);
		char nb[40]; trim_copy (nb, newBal->text (), sizeof nb);
		if (nb[0])
		{
			money m; if (!parse_money (nb, &m)) { if (!s.nl) st_add (s); newBal->setError (true); warn ("Save", "Type the new balance as an amount, or leave it empty."); return false; }
			if (m != s.old + sum)
			{
				char a[32], b[32], c[32], msg[240];
				fmt_money (s.old, a); fmt_money (s.old + sum, b); fmt_money (m - s.old - sum, c);
				scpy (msg, "The old balance ", sizeof msg); scat (msg, a, sizeof msg); scat (msg, " plus the movements makes ", sizeof msg); scat (msg, b, sizeof msg);
				scat (msg, ", not the new balance typed: ", sizeof msg); scat (msg, c, sizeof msg); scat (msg, " is missing. Check the movements (or empty the new balance).", sizeof msg);
				if (!s.nl) st_add (s);
				warn ("Save", msg); return false;
			}
		}
		s.now = s.old + sum;
		const char *w = st_check (g_b, s);
		if (w[0]) { if (!s.nl) st_add (s); warn ("Save", w); return false; }
		if (s.id)
		{
			int i = entry_index (g_b, s.id);
			if (i >= 0) { const char *lk = entry_locked (g_b, g_b.e[i]); if (lk[0]) { warn ("Save", lk); return false; } }
		}
		Entry e; st_to_entry (g_b, s, e);
		bool wasNew = !s.id;
		int i = entry_save (g_b, e);
		st_apply_matches (g_b, i, s);
		s.id = g_b.e[i].id;
		char r[40], m[96]; entry_ref (g_b, g_b.e[i], r, sizeof r);
		scpy (m, "Statement ", sizeof m); scat (m, r, sizeof m); scat (m, wasNew ? " posted" : " saved", sizeof m);
		dirty = false;
		changed ();
		status (m);
		return true;
	}
	void del () override
	{
		int i = entry_index (g_b, s.id);
		if (i < 0) return;
		const char *w = entry_can_delete (g_b, i);
		if (w[0]) { warn ("Delete", w); return; }
		if (ask ("Delete", "Delete this statement for good? Its matchings are undone.", MB_YESNO, 2) != 1) return;
		entry_delete (g_b, i);
		changed ();
		status ("Statement deleted");
		close ();
	}
	void refresh () override { titleFor (); computeOld (); listItems (itemsFor, true); }
	void enter () override { g->focusIn (); }
	void onDraw () override
	{
		drawHead ();
		int W = width, rx = W - 330;
		lab (20, 72, "Journal"); lab (rx, 72, "Date"); lab (20, 104, "Description"); lab (rx, 104, "New balance");
		money sum = st_sum (s), now = s.old + sum;
		char a[32], b[32], t[160];
		int y = 138;
		scpy (t, "Old balance  ", sizeof t); scat (t, money_s (s.old, a), sizeof t);
		wk_text_l (canvas, 20, y, 26, t, C_TEXT);
		scpy (t, "Movements  ", sizeof t); if (sum > 0) scat (t, "+", sizeof t); scat (t, money_s (sum, a), sizeof t);
		wk_text_l (canvas, 240, y, 26, t, C_TEXT);
		scpy (t, "New balance  ", sizeof t); scat (t, money_s (now, b), sizeof t);
		wk_text_l (canvas, rx, y, 26, t, C_TEXT, 2);
		char nb[40]; trim_copy (nb, newBal->text (), sizeof nb); money m;
		if (nb[0] && parse_money (nb, &m))
		{
			if (m == now) draw_pill (canvas, rx + 250, y + 4, 20, "Balanced", C_GOOD);
			else { char d[48] = "Off by "; scat (d, money_s (m - now, a), sizeof d); draw_pill (canvas, rx + 250 - pill_w (d) + pill_w ("Balanced"), y + 4, 20, d, C_BAD); }
		}
		// the open items' title
		int iy = wg (items)->top - 26;
		int r = itemsFor;
		if (r >= 0 && r < s.nl && s.l[r].party)
		{
			scpy (t, "Open items of ", sizeof t); scat (t, party_name (g_b, s.l[r].party), sizeof t);
			scat (t, "  \xB7  tick those this movement pays", sizeof t);
		}
		else scpy (t, "Open items", sizeof t);
		wk_text_l (canvas, 20, iy, 24, t, C_TEXT, 2);
	}
	void resizeTo (int w, int h) override
	{
		DocPage::resizeTo (w, h);
		if (!g || !items) return;
		int gh = (h - 176 - 44) * 55 / 100;
		g->resizeTo (g->width, gh);
		int iy = 176 + gh + 34;
		wg (items)->top = iy; items->resizeTo (items->width, imax (40, h - iy - 12));
	}
	static void on_journal (Widget &) { StatementPage *p = g_st; p->s.journal = p->jc.n ? p->jc.idx[iclamp (p->jbox->sel, 0, p->jc.n - 1)] : p->s.journal; p->computeOld (); p->titleFor (); p->dirty = true; }
	static void on_date (Widget &) { StatementPage *p = g_st; int d = date_parse (p->date->text ()); if (d) { p->s.date = d; p->computeOld (); } p->dirty = true; p->invalidate (true); }
	static void on_newbal (Widget &) { g_st->dirty = true; g_st->invalidate (true); }
	static void s_save () { if (g_st->save ()) g_st->close (); }
	static void s_saveNew () { if (g_st->save ()) { int j = g_st->s.journal; g_st->startNew (j); g_st->sync (); g_st->g->focusIn (); } }
	static void s_cancel () { g_st->cancel (); }
	static void s_del () { g_st->del (); }
};

// =====================================================================================================================================
// ---- the miscellaneous operation --------------------------------------------------------------------------------------------------
// =====================================================================================================================================
class MiscPage;
static MiscPage *g_misc;

class MiscPage : public DocPage, public EGModel
{
public:
	Entry e;
	ChoiceBox *jbox; JournalChoice jc;
	DateEdit *date; LineEdit *text; Checkbox *opening;
	EditGrid *g; FlatButton *bBalance;
	MiscPage () : DocPage (E_MISC)
	{
		entry_init (e);
		resizeTo (800, 660);
		buttons (s_save, s_saveNew, s_cancel, s_del);
		int W = width, rx = W - 330;
		jbox = new ChoiceBox (130, 72, 240); jbox->onChange = mark_dirty; addChild (jbox);
		date = new DateEdit (rx + 110, 72, 150); date->anchor = ANCHOR_RIGHT | ANCHOR_TOP; date->onChange = mark_dirty; addChild (date);
		text = new LineEdit (130, 104, rx - 130 - 18); text->anchor = ANCHOR_LEFT | ANCHOR_TOP | ANCHOR_RIGHT; text->onChange = mark_dirty;
		text->placeholder = "Salaries of September, depreciation 2026..."; addChild (text);
		opening = new Checkbox (rx + 110, 104, 200, ED_H, "Opening balances", false, mark_dirty, C_BG); opening->anchor = ANCHOR_RIGHT | ANCHOR_TOP; addChild (opening);
		g = new EditGrid (16, 144, W - 32, height - 144 - 64, this);
		g->anchor = ANCHOR_FILL;
		g->addCol ("Account", 200, EK_ACCOUNT, false, AF_ALL, "6");
		g->addCol ("Party", 130, EK_PARTY, false, -1);
		g->addCol ("Description", 150, EK_TEXT);
		g->addCol ("Debit", 100, EK_MONEY, true);
		g->addCol ("Credit", 100, EK_MONEY, true);
		g->addCol ("VAT", 64, EK_VAT, false, -1);
		g->flex = 0;
		addChild (g);
		bBalance = new FlatButton ("Balance it on this line", s_balance, FB_QUIET);
		bBalance->left = W - 16 - bBalance->width; bBalance->top = height - 46; bBalance->anchor = ANCHOR_RIGHT | ANCHOR_BOTTOM;
		bBalance->tip = "The difference put on the active line"; addChild (bBalance);
	}
	~MiscPage () { entry_free (e); }
	EditGrid *grid () override { return g; }
	bool isNew () override { return e.id == 0; }
	void setup (bool all)
	{
		jc.fill (JT_MISC, -1, all);
		jbox->setOptions (jc.names, jc.n); jbox->sel = jc.find (e.journal);
		g->reset ();
	}
	void fields ()
	{
		char d[16]; date_show (e.date, d); date->setText (d);
		text->setText (e.text);
		opening->checked = (e.flags & EF_OPENING) != 0; opening->invalidate (true);
		titleFor ();
	}
	void titleFor ()
	{
		if (e.id) { char r[40]; doc_ref (e.id, r, sizeof r); scpy (head, "Operation ", sizeof head); scat (head, r, sizeof head); }
		else scpy (head, "New miscellaneous operation", sizeof head);
		invalidate (true);
	}
	void startNew (int journal)
	{
		entry_free (e); entry_init (e);
		e.journal = journal; e.date = default_date ();
		setup (false);
		entry_add_line (e); entry_add_line (e);
		fields ();
		bDelete->hidden = true;
		dirty = false;
	}
	void load (const Entry &src)
	{
		entry_free (e); entry_copy (e, src);
		setup (g_b.jr[e.journal].type != JT_MISC);
		if (!e.nl) entry_add_line (e);
		fields ();
		bDelete->hidden = false;
		dirty = false;
	}
	void subtitle (char *out, int cap) override
	{
		money d = 0, c = 0;
		for (int i = 0; i < e.nl; i++) { if (e.l[i].amount > 0) d += e.l[i].amount; else c -= e.l[i].amount; }
		char a[32], b[32];
		scpy (out, "Debit ", cap); scat (out, money_s (d, a), cap); scat (out, "  \xB7  credit ", cap); scat (out, money_s (c, b), cap);
		if (d != c) { scat (out, "  \xB7  difference ", cap); scat (out, money_s (d - c, a), cap); }
		else if (d) scat (out, "  \xB7  balanced", cap);
	}
	// ---- the lines (EGModel) ----
	int rows () override { return e.nl; }
	void cell (int r, int c, bool edit, char *out, int cap) override
	{
		out[0] = '\0';
		if (r < 0 || r >= e.nl) return;
		const Line &l = e.l[r];
		switch (c)
		{
		case 0: scpy (out, l.account, cap); if (!edit && l.account[0]) { scat (out, "  ", cap); scat (out, acc_name (g_b, l.account), cap); } break;
		case 1: if (l.party) scpy (out, party_name (g_b, l.party), cap); break;
		case 2: scpy (out, l.text, cap); break;
		case 3: if (l.amount > 0) { if (edit) edit_money (l.amount, out); else show_money (l.amount, out); } break;
		case 4: if (l.amount < 0) { if (edit) edit_money (-l.amount, out); else show_money (-l.amount, out); } break;
		case 5: scpy (out, vat_label (l.vat), cap); break;
		}
	}
	const char *put (int r, int c, const char *t) override
	{
		Line &l = e.l[r];
		switch (c)
		{
		case 0:
		{
			char code[CODE_MAX]; const char *w = resolve_account (t, AF_ALL, code); if (w[0]) return w;
			scpy (l.account, code, CODE_MAX);
			if (!acc_party (g_b, code)) l.party = 0;
			break;
		}
		case 1:
		{
			int id; const char *w = resolve_party (t, acc_kind (l.account) == AK_PAY ? PK_SUPPLIER : acc_kind (l.account) == AK_RECV ? PK_CUSTOMER : -1, &id);
			if (w[0]) return w;
			l.party = id;
			break;
		}
		case 2: sset (l.text, t); break;
		case 3: { money m; const char *w = put_money (t, &m); if (w[0]) return w; if (m < 0) return "A debit is positive: type the credit in its column."; if (m || l.amount > 0) l.amount = m; break; }
		case 4: { money m; const char *w = put_money (t, &m); if (w[0]) return w; if (m < 0) return "A credit is positive: type the debit in its column."; if (m || l.amount < 0) l.amount = -m; break; }
		case 5: { int x; const char *w = resolve_vat (t, -1, &x); if (w[0]) return w; l.vat = (signed char) x; l.role = LR_NONE; break; }
		}
		dirty = true;
		return "";
	}
	bool editable (int r, int c) override { return c != 1 || (r < e.nl && acc_party (g_b, e.l[r].account)); }
	void add () override
	{
		Line &l = entry_add_line (e);
		money s = entry_sum (e);				// (the difference proposed on the new line)
		(void) l; (void) s;
	}
	void remove (int r) override { line_free (e.l[r]); for (int k = r + 1; k < e.nl; k++) e.l[k - 1] = e.l[k]; e.nl--; dirty = true; }
	bool blank (int r) override { const Line &l = e.l[r]; return !l.amount && !l.account[0] && !l.text[0]; }
	void linesChanged () override { dirty = true; invalidate (true); }
	void balanceHere ()
	{
		int r = g->cur;
		if (g->editing ()) g->commit ();
		if (r < 0 || r >= e.nl) { g->addLine (); r = e.nl - 1; }
		money s = entry_sum (e) - e.l[r].amount;
		e.l[r].amount = -s;
		dirty = true;
		g->invalidate (true); invalidate (true);
		if (g->editing ()) g->activate (r, g->curCol);
	}
	bool save () override
	{
		if (!g->commit ()) return false;
		int d = date_parse (date->text ());
		if (!d) { date->setError (true); warn ("Save", "Type the date, as 29/09/2026."); return false; }
		Entry x; entry_copy (x, e);
		x.date = d; x.due = d;
		x.journal = jc.n ? jc.idx[iclamp (jbox->sel, 0, jc.n - 1)] : x.journal;
		sset (x.text, text->text ());
		if (opening->checked) x.flags |= EF_OPENING; else x.flags &= ~(unsigned) EF_OPENING;
		misc_tidy (x);
		for (int k = 0; k < x.nl; k++) { if (!x.l[k].due) x.l[k].due = d; if (!x.l[k].text[0]) sset (x.l[k].text, x.text); }
		const char *w = misc_check (g_b, x);
		if (w[0]) { entry_free (x); warn ("Save", w); return false; }
		if (e.id)
		{
			int i = entry_index (g_b, e.id);
			if (i >= 0) { const char *lk = entry_locked (g_b, g_b.e[i]); if (lk[0]) { entry_free (x); warn ("Save", lk); return false; } }
		}
		bool wasNew = !e.id;
		int i = entry_save (g_b, x);
		entry_free (e); entry_copy (e, g_b.e[i]);
		char r[40], m[96]; entry_ref (g_b, g_b.e[i], r, sizeof r);
		scpy (m, "Operation ", sizeof m); scat (m, r, sizeof m); scat (m, wasNew ? " posted" : " saved", sizeof m);
		dirty = false;
		changed ();
		status (m);
		return true;
	}
	void del () override
	{
		int i = entry_index (g_b, e.id);
		if (i < 0) return;
		const char *w = entry_can_delete (g_b, i);
		if (w[0]) { warn ("Delete", w); return; }
		if (ask ("Delete", "Delete this operation for good?", MB_YESNO, 2) != 1) return;
		entry_delete (g_b, i);
		changed ();
		status ("Operation deleted");
		close ();
	}
	void refresh () override { titleFor (); }
	void enter () override { g->focusIn (); }
	void onDraw () override
	{
		drawHead ();
		int rx = width - 330;
		lab (20, 72, "Journal"); lab (rx, 72, "Date"); lab (20, 104, "Description");
		money d = 0, c = 0;
		for (int i = 0; i < e.nl; i++) { if (e.l[i].amount > 0) d += e.l[i].amount; else c -= e.l[i].amount; }
		char a[32], t[96];
		int y = height - 46;
		scpy (t, "Debit  ", sizeof t); scat (t, money_s (d, a), sizeof t); wk_text_l (canvas, 20, y, 30, t, C_TEXT, 2);
		scpy (t, "Credit  ", sizeof t); scat (t, money_s (c, a), sizeof t); wk_text_l (canvas, 230, y, 30, t, C_TEXT, 2);
		if (d != c) { scpy (t, "Difference ", sizeof t); scat (t, money_s (d - c, a), sizeof t); draw_pill (canvas, 440, y + 5, 20, t, C_BAD); }
		else if (d) draw_pill (canvas, 440, y + 5, 20, "Balanced", C_GOOD);
	}
	static void s_save () { if (g_misc->save ()) g_misc->close (); }
	static void s_saveNew () { if (g_misc->save ()) { int j = g_misc->e.journal; g_misc->startNew (j); g_misc->sync (); g_misc->g->focusIn (); } }
	static void s_cancel () { g_misc->cancel (); }
	static void s_del () { g_misc->del (); }
	static void s_balance () { g_misc->balanceHere (); }
};

} // namespace lg

#endif
