//
// payui.h -- Pay suppliers: the suppliers' open invoices listed (by due date: those due within a week of
// the day asked ticked, unless a file already holds them), the account paid from (a bank journal with its
// IBAN), the day the bank pays; "Make the file" writes the SEPA credit transfer file (sepa.h) in
// SD:/docs/Payments -- to upload to the bank's site -- and flags its invoices (not offered again until a
// statement pays them).
//
#ifndef _ledger_payui_h
#define _ledger_payui_h

#include "sepa.h"
#include "lists.h"

namespace lg {

class PayDialog : public FormBox
{
public:
	enum { MAXP = 400 };
	Pay p[MAXP]; bool tick[MAXP]; int np;
	TickGrid *g; DateEdit *date; ChoiceBox *acct;
	int jr[MAXJOURNALS]; const char *jn[MAXJOURNALS]; char jbuf[MAXJOURNALS][64]; int nj;
	PayDialog () : FormBox ("Pay suppliers", 880, 560, 130), np (0), g (0), date (0), acct (0), nj (0)
	{
		int y = y0;
		// the accounts: the bank journals that have an IBAN
		for (int j = 0; j < g_b.njr && nj < MAXJOURNALS; j++)
			if (g_b.jr[j].type == JT_BANK && g_b.jr[j].iban[0] && !g_b.jr[j].hidden)
			{
				char ib[48]; iban_show (g_b.jr[j].iban, ib, sizeof ib);
				scpy (jbuf[nj], g_b.jr[j].code, 64); scat (jbuf[nj], "  ", 64); scat (jbuf[nj], ib, 64);
				jn[nj] = jbuf[nj]; jr[nj] = j; nj++;
			}
		if (!nj && g_b.iban[0]) { char ib[48]; iban_show (g_b.iban, ib, sizeof ib); scpy (jbuf[0], ib, 64); jn[0] = jbuf[0]; jr[0] = -1; nj = 1; }
		label (y, "From the account"); acct = new ChoiceBox (fieldX, y, 300); acct->setOptions (jn, nj); addChild (acct);
		label (y, "Paid on", 480); date = new DateEdit (560, y, 150); char d[16]; date_show (default_pay_day (), d); date->setText (d); date->onChange = on_date; addChild (date);
		y += 42;
		g = new TickGrid (20, y, width - 40, height - y - 100);
		g->setColumns (7);
		g->setColumn (0, "", 30); g->setColumn (1, "Supplier", 170); g->setColumn (2, "Document", 132); g->setColumn (3, "Their number", 112);
		g->setColumn (4, "Due", 100); g->setColumn (5, "Amount", 108, GRID_RIGHT); g->setColumn (6, "", 90);
		fit_columns (g, 1, 120);
		g->cellText = c_text; g->cellDraw = c_draw; g->onTickRow = on_tick; g->onActivate = on_act; g->user = this; g->sortable = false;
		g->emptyText = "No supplier's invoice to pay.";
		addChild (g);
		np = sepa_open (g_b, p, MAXP);
		g->setRows (np);
		pick ();
		buttons ("Make the file");
		// (the OK button's width: its text)
		for (Widget *c = firstChild; c; c = c->nextSib) if (c->tag == 1 && c->top > height - 60) { c->left -= 40; c->resizeTo (128, c->height); }
		g->setFocus ();
	}
	// The day the bank pays by default: today (else the year shown's nearest day).
	static int default_pay_day () { return default_date (); }
	int day () { int d = date_parse (date->text ()); return d ? d : default_pay_day (); }
	// Those due within a week of the day, payable, not in a file yet: ticked.
	void pick ()
	{
		int lim = date_add (day (), 7);
		for (int i = 0; i < np; i++)
		{
			const Entry &e = g_b.e[p[i].entry]; const Line &l = e.l[p[i].line];
			int due = l.due ? l.due : e.date;
			tick[i] = due <= lim && !sepa_check (g_b, p[i])[0] && !(e.flags & EF_PAYING);
		}
		g->invalidate (true); invalidate (true);
	}
	void totals (int *n, money *sum) { *n = 0; *sum = 0; for (int i = 0; i < np; i++) if (tick[i]) { (*n)++; *sum += p[i].amount; } }
	void drawMore () override
	{
		int n; money s; totals (&n, &s);
		char t[120], a[32]; itoa10 (n, t); scat (t, n == 1 ? " transfer  \xB7  " : " transfers  \xB7  ", sizeof t); scat (t, money_s (s, a), sizeof t);
		wk_text_l (canvas, 20, height - 90, 24, t, C_TEXT, 2);
		wk_text_l (canvas, 20, height - 64, 20, "The file goes to SD:/docs/Payments: upload it to your bank's site.", dim_ink (C_FACE));
	}
	static const char *c_text (DataGrid &gr, int row, int col, char *buf, int cap)
	{
		PayDialog *d = (PayDialog *) gr.user;
		if (row >= d->np) return "";
		const Entry &e = g_b.e[d->p[row].entry]; const Line &l = e.l[d->p[row].line];
		switch (col)
		{
		case 1: return party_name (g_b, l.party);
		case 2: entry_ref (g_b, e, buf, cap); return buf;
		case 3: return e.ref;
		case 4: date_show (l.due ? l.due : e.date, buf); return buf;
		case 5: fmt_money (d->p[row].amount, buf); return buf;
		}
		return "";
	}
	static bool c_draw (DataGrid &gr, Canvas &cv, int row, int col, int x, int y, int w, int h, unsigned ink, bool sel)
	{
		PayDialog *d = (PayDialog *) gr.user;
		if (row >= d->np) return false;
		const char *why = sepa_check (g_b, d->p[row]);
		if (col == 0) { wk_check_mark (cv, x + (w - 16) / 2, y + (h - 16) / 2, 16, d->tick[row], why[0] ? WK_DISABLED : WK_NORMAL); return true; }
		if (col == 4 && !sel)
		{
			const Entry &e = g_b.e[d->p[row].entry]; const Line &l = e.l[d->p[row].line];
			int due = l.due ? l.due : e.date; char t[16]; date_show (due, t);
			cell_text (cv, x, y, w, h, t, due < today_ymd () ? C_BAD : ink, false);
			return true;
		}
		if (col == 6)
		{
			if (why[0]) draw_pill (cv, x + 6, y + (h - 18) / 2, 18, "No IBAN", C_BAD, sel);
			else if (g_b.e[d->p[row].entry].flags & EF_PAYING) draw_pill (cv, x + 6, y + (h - 18) / 2, 18, "In a file", C_WARN, sel);
			return true;
		}
		return false;
	}
	void toggle (int row)
	{
		if (row < 0 || row >= np) return;
		const char *why = sepa_check (g_b, p[row]);
		if (why[0]) { warn ("Pay suppliers", why); return; }
		tick[row] = !tick[row];
		g->invalidate (true); invalidate (true);
	}
	static void on_tick (Widget &w) { PayDialog *d = (PayDialog *) ((DataGrid &) w).user; d->toggle (((TickGrid &) w).tickRow); }
	static void on_act (Widget &w) { PayDialog *d = (PayDialog *) ((DataGrid &) w).user; d->toggle (((DataGrid &) w).sel); }
	static void on_date (Widget &w) { Widget *q = w.parent; while (q && !q->modal) q = q->parent; if (q) ((PayDialog *) q)->pick (); }
	bool onKey (long k) override
	{
		if (k == ' ' && g->hasFocus) { toggle (g->sel); return true; }
		return FormBox::onKey (k);
	}
	bool validate () override
	{
		int n; money s; totals (&n, &s);
		if (!n) { warn ("Pay suppliers", "Tick the invoices to pay."); return false; }
		if (!nj) { warn ("Pay suppliers", "No account to pay from: type the IBAN of a bank journal (Settings > Journals)."); return false; }
		int d = date_parse (date->text ());
		if (!d) { warn ("Pay suppliers", "Type the day the bank pays, as 29/09/2026."); date->setFocus (); return false; }
		return true;
	}
	// The file written (its path in out) -> false: it could not be.
	bool make (char *out, int cap)
	{
		static Pay sel[MAXP]; int n = 0;
		for (int i = 0; i < np; i++) if (tick[i]) sel[n++] = p[i];
		int j = jr[iclamp (acct->sel, 0, nj - 1)];
		const char *iban = j >= 0 ? g_b.jr[j].iban : g_b.iban;
		int y, mo, dd0, h, mi, se; kapi_get_datetime (&y, &mo, &dd0, &h, &mi, &se);
		int t = today_ymd (), hms = h * 10000 + mi * 100 + se;
		char id[40] = "LEDGER-"; char d8[12]; itoa10 (t, d8); scat (id, d8, sizeof id); scat (id, "-", sizeof id);
		char h6[8]; for (int i = 5, v = hms; i >= 0; i--, v /= 10) h6[i] = (char) ('0' + v % 10); h6[6] = '\0'; scat (id, h6, sizeof id);
		Out o;
		if (!sepa_write (g_b, sel, n, day (), t, hms, iban, g_b.bic, id, o)) return false;
		kapi_mkdir ("SD:/docs"); kapi_mkdir ("SD:/docs/Payments");
		char dd[16]; date_iso (day (), dd);
		scpy (out, "SD:/docs/Payments/SEPA ", cap); scat (out, dd, cap); scat (out, " ", cap); scat (out, h6, cap); scat (out, ".xml", cap);
		if (kapi_save_file (out, o.b, (unsigned) o.n) < 0) return false;
		for (int i = 0; i < n; i++) g_b.e[sel[i].entry].flags |= EF_PAYING;
		return true;
	}
};
static void pay_suppliers ()
{
	PayDialog d;
	if (d.run () != 1) return;
	char path[200];
	if (!d.make (path, sizeof path)) { warn ("Pay suppliers", "The file could not be written (the card full, write-protected?)."); return; }
	changed ();
	char m[240] = "Written: "; scat (m, path, sizeof m); scat (m, " -- upload it to your bank", sizeof m);
	status (m);
}

} // namespace lg

#endif
