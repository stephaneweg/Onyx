//
// Apps/mail/contactsui.h -- Mail's Contacts, in place of the list and the reading pane: the people A to Z (their
// initials, their address), a search; the one chosen as Cardfile shows a card (name, e-mails, phones, company,
// address, birthday, notes); Write, Edit, Delete, a new one, Open in Cardfile (the same file:
// SD:/Documents/Contacts.card -- contacts.h).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
//
#ifndef _mail_contactsui_h
#define _mail_contactsui_h

#include "Apps/mail/app.h"

namespace mailapp {

// ---- a contact edited ----------------------------------------------------------------------------------------------------------
class ContactBox : public Modal
{
public:
	Contact c; Textbox *t[8]; Textarea *notes;
	static const int W = 520, H = 520, PAD = 20;
	ContactBox (const Contact &from) : Modal (W, H), c (from)
	{
		const char *v[8] = { c.name, c.email, c.email2, c.phone, c.mobile, c.company, c.address, c.birthday };
		int y = titleH () + 14;
		for (int i = 0; i < 8; i++) { t[i] = new Textbox (PAD + 130, y + i * 38, W - 2 * PAD - 130, 30, v[i]); t[i]->maxLen = 290; addChild (t[i]); }
		notes = new Textarea (PAD + 130, y + 8 * 38, W - 2 * PAD - 130, 70, 600); notes->setContent (c.notes); addChild (notes);
		Button *b = new Button (W - PAD - 220, H - 48, 100, 32, "Cancel", [] (Widget &w) { ((Modal *) w.parent)->close (0); }); addChild (b);
		b = new Button (W - PAD - 110, H - 48, 110, 32, "Keep", [] (Widget &w) { ((Modal *) w.parent)->close (1); }); addChild (b);
		t[0]->setFocus ();
	}
	void onDraw () override
	{
		drawBox (c.name[0] ? c.name : "A new contact");
		static const char *const L[9] = { "Name", "E-mail", "Other e-mail", "Phone", "Mobile", "Company", "Address", "Birthday", "Notes" };
		int y = titleH () + 14;
		for (int i = 0; i < 9; i++) text_v (canvas, PAD, y + i * 38, 30, L[i], C_TEXT);
		text (canvas, PAD, y + 7 * 38 + 30, "(dd/mm/yyyy)", wk_mix (C_BG, C_TEXT, 140), F_TINY);
	}
	bool get (Contact &out)
	{
		if (run () != 1) return false;
		char *d[8] = { out.name, out.email, out.email2, out.phone, out.mobile, out.company, out.address, out.birthday };
		int cap[8] = { sizeof out.name, sizeof out.email, sizeof out.email2, sizeof out.phone, sizeof out.mobile, sizeof out.company, sizeof out.address, sizeof out.birthday };
		for (int i = 0; i < 8; i++) scpy (d[i], t[i]->text, cap[i]);
		scpy (out.notes, notes->content (), sizeof out.notes);
		return true;
	}
};

static void contacts_search_changed (Widget &);
class ContactsPane : public Widget
{
public:
	Textbox *search; Button *bNew;
	int order[4096]; int n, sel, sy;
	HitList hits;
	char lastQ[200];
	enum { H_ROW = 1, H_WRITE, H_EDIT, H_DELETE, H_CARDFILE, H_MAIL };
	static const int LW = 300;
	ContactsPane (int l, int t, int w, int h) : Widget (l, t, w, h), n (0), sel (-1), sy (0)
	{
		hidden = true; lastQ[0] = 0;
		search = new Textbox (12, 12, LW - 24 - 40, 30, ""); addChild (search);
		bNew = new Button (LW - 46, 12, 34, 30, "+", [] (Widget &) { ((ContactsPane *) g_contacts)->add_new (); }); addChild (bNew);
		reload ();
	}
	unsigned bgColor () override { return C_FIELD; }
	void reload ()
	{
		int all[4096]; int na = g_m.contacts.count () < 4096 ? g_m.contacts.count () : 4096;
		g_m.contacts.sorted (all);
		n = 0;
		for (int i = 0; i < na; i++)
		{
			if (search->text[0])
			{
				Contact c; g_m.contacts.get (all[i], c);
				if (!ifind (c.name, search->text) && !ifind (c.email, search->text) && !ifind (c.company, search->text)) continue;
			}
			order[n++] = all[i];
		}
		if (sel >= n) sel = n - 1;
		if (sel < 0 && n) sel = 0;
		invalidate (true);
	}
	void tick () { if (hidden) return; if (strcmp (lastQ, search->text)) { scpy (lastQ, search->text, sizeof lastQ); sel = 0; sy = 0; reload (); } }
	void onDraw () override
	{
		canvas.clear (C_FIELD); hits.clear ();
		canvas.fillRect (LW, 0, 1, height, col_line ());
		search->resizeTo (LW - 24 - 40, 30);
		// the list, its letters
		int y = 54 - sy; char letter[8] = "";
		for (int i = 0; i < n; i++)
		{
			Contact c; g_m.contacts.get (order[i], c);
			char L[8]; initials (c.name[0] ? c.name : c.email, L); L[(unsigned char) L[0] >= 0xC0 ? 2 : 1] = 0;
			if (strcmp (L, letter)) { scpy (letter, L, sizeof letter); if (y > -30 && y < height) text (canvas, 16, y + 4, letter, C_ACCENT, F_SMALL, 1); y += 24; }
			if (y > -50 && y < height)
			{
				if (i == sel) wk_fill_round (canvas, 6, y, LW - 12, 46, 6, col_sel ());
				avatar (canvas, 30, y + 23, 16, c.name[0] ? c.name : c.email, c.email);
				text (canvas, 56, y + 5, c.name[0] ? c.name : c.email, C_FIELD_TEXT, F_UI, 1, LW - 70);
				text (canvas, 56, y + 24, c.email, col_dim (), F_SMALL, 0, LW - 70);
			}
			hits.add (0, y, LW, 46, H_ROW, i);
			y += 48;
		}
		if (!n) text_c (canvas, 0, 80, LW, 30, g_m.contacts.count () ? "Nobody by that name." : "No contacts yet.", col_dim ());
		// the card
		int x = LW + 30, W = width - x - 30;
		if (sel < 0 || sel >= n)
		{
			text_c (canvas, LW, height / 2 - 30, width - LW, 24, "Contacts are kept in SD:/Documents/Contacts.card", col_dim ());
			text_c (canvas, LW, height / 2, width - LW, 24, "Cardfile opens the same file.", col_dim (), F_SMALL);
			const char *l = "Open in Cardfile"; int lw = tw (l, F_UI, 1);
			text_c (canvas, LW, height / 2 + 30, width - LW, 24, l, C_ACCENT, F_UI, 1);
			hits.add (LW + (width - LW - lw) / 2, height / 2 + 30, lw, 24, H_CARDFILE);
			return;
		}
		Contact c; g_m.contacts.get (order[sel], c);
		int cy = 30;
		avatar (canvas, x + 36, cy + 36, 36, c.name[0] ? c.name : c.email, c.email);
		text (canvas, x + 90, cy + 8, c.name[0] ? c.name : c.email, C_FIELD_TEXT, F_H1, 1, W - 90);
		if (c.company[0]) text (canvas, x + 90, cy + 44, c.company, col_dim (), F_MID, 0, W - 90);
		cy += 96;
		// the buttons
		static const char *const B[4] = { "Write", "Edit", "Delete", "Open in Cardfile" };
		static const int BK[4] = { H_WRITE, H_EDIT, H_DELETE, H_CARDFILE };
		int bx = x;
		for (int k = 0; k < 4; k++)
		{
			int bw = tw (B[k], F_UI, 1) + 28;
			wk_fill_round (canvas, bx, cy, bw, 32, 16, k == 0 ? C_ACCENT : col_line ());
			text_c (canvas, bx, cy, bw, 32, B[k], k == 0 ? C_SEL_TEXT : C_FIELD_TEXT, F_UI, 1);
			hits.add (bx, cy, bw, 32, BK[k]);
			bx += bw + 8;
		}
		cy += 52;
		// the fields
		const char *L[9] = { "E-mail", "Other e-mail", "Phone", "Mobile", "Company", "Address", "Birthday", "Notes", 0 };
		const char *V[8] = { c.email, c.email2, c.phone, c.mobile, c.company, c.address, c.birthday, c.notes };
		for (int k = 0; k < 8; k++)
		{
			if (!V[k][0]) continue;
			text (canvas, x, cy, L[k], col_dim (), F_SMALL, 1);
			char v[600]; scpy (v, V[k], sizeof v);
			if (k == 6) { int yy, mm, dd; if (sscanf (v, "%d-%d-%d", &yy, &mm, &dd) == 3) snprintf (v, sizeof v, "%d %s %d", dd, MONTHS3[(mm - 1) % 12], yy); }
			int lines = 0;
			for (char *ln = v; ln; lines++)
			{
				char *e = strchr (ln, '\n'); if (e) *e = 0;
				text (canvas, x + 120, cy + lines * 20, ln, k <= 1 ? C_ACCENT : C_FIELD_TEXT, F_UI, 0, W - 120);
				ln = e ? e + 1 : 0;
				if (lines > 5) break;
			}
			if (k <= 1) hits.add (x + 120, cy, tw (V[k]), 20, H_MAIL, k);
			cy += (lines ? lines : 1) * 20 + 12;
			canvas.fillRect (x, cy - 6, W, 1, col_line ());
		}
	}
	void add_new ()
	{
		Contact c; memset (&c, 0, sizeof c);
		ContactBox b (c); Contact out = c;
		if (!b.get (out) || (!out.name[0] && !out.email[0])) return;
		g_m.contacts.add (out); g_m.contacts.save ();
		search->setText (""); lastQ[0] = 0; reload ();
		for (int i = 0; i < n; i++) { Contact x; g_m.contacts.get (order[i], x); if (!strcmp (x.name, out.name) && !strcmp (x.email, out.email)) sel = i; }
		invalidate (true);
	}
	bool onMouse (int mx, int my, int bl, int, int, int wheel) override
	{
		if (wheel && mx < LW) { sy -= wheel * 48; if (sy < 0) sy = 0; invalidate (true); return true; }
		static bool was; bool down = bl && !was; was = bl;
		if (!down) return false;
		const Hit *h = hits.at (mx, my); if (!h) return false;
		Contact c; if (sel >= 0 && sel < n) g_m.contacts.get (order[sel], c);
		switch (h->kind)
		{
		case H_ROW: sel = h->a; invalidate (true); break;
		case H_WRITE: if (sel >= 0) compose_to (c.name, c.email); break;
		case H_MAIL: if (sel >= 0) compose_to (c.name, h->a ? c.email2 : c.email); break;
		case H_EDIT: { ContactBox b (c); Contact out = c; if (b.get (out)) { g_m.contacts.set (order[sel], out); g_m.contacts.save (); reload (); } break; }
		case H_DELETE:
		{
			char q[300]; snprintf (q, sizeof q, "Delete %s from the contacts?", c.name[0] ? c.name : c.email);
			if (wk_messagebox ("Mail", q, MB_YESNO) == 1) { g_m.contacts.remove (order[sel]); g_m.contacts.save (); reload (); }
			break;
		}
		case H_CARDFILE: kapi_exec ("SD:apps/cardfile.app/main", "SD:/Documents/Contacts.card"); break;
		}
		return true;
	}
	bool onKey (long k) override
	{
		if (k == KEY_DOWN && sel < n - 1) { sel++; invalidate (true); return true; }
		if (k == KEY_UP && sel > 0) { sel--; invalidate (true); return true; }
		return false;
	}
};

} // namespace mailapp

#endif
