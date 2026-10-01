//
// pick.h -- choosing what a field names as it is typed: an account (its code's digits, or words of its name),
// a party (its code, words of its name, its VAT number), a VAT code, a journal. A list drops under the
// field (SugList: a floating panel on top of the window -- or of the dialog --, the first match lit); Up /
// Down move in it, Enter or Tab takes the one lit, a click takes one, Esc or a click elsewhere closes it.
// Suggester is that list's logic, shared by a field (PickEdit: the invoice's customer, a dialog's
// account) and by the cells of the entry grids (editgrid.h). A suggestion's value is what the field then
// holds: an account's code, "P12" (the party 12), "A700000" (an account, where parties are offered too),
// a VAT code ("-": none), a journal's index.
//
#ifndef _ledger_pick_h
#define _ledger_pick_h

#include "ui.h"

namespace lg {

struct Sug { char text[128]; char value[24]; };
enum { MAXSUG = 80 };
enum { SK_NONE, SK_ACCOUNT, SK_PARTY, SK_TARGET, SK_VAT, SK_JOURNAL };
enum { AF_ALL = 0, AF_NOPARTY = 1, AF_PARTY = 2, AF_FIN = 4, AF_HEADINGS = 8 };	// SK_ACCOUNT's filter (arg)

// ---- the sources -------------------------------------------------------------------------------------------------------------
// Every word typed is in the text (case and accents ignored).
static bool words_in (const char *hay, const char *typed)
{
	const char *p = typed;
	for (;;)
	{
		while (*p == ' ') p++;
		if (!*p) return true;
		int n = 0; while (p[n] && p[n] != ' ') n++;
		if (!ci_has (hay, p, n)) return false;
		p += n;
	}
}
static bool all_digits (const char *s) { if (!*s) return false; for (; *s; s++) if (!digit (*s)) return false; return true; }
static void sug_set (Sug &s, const char *a, const char *b, const char *value)
{
	scpy (s.text, a, sizeof s.text);
	if (b && b[0]) { scat (s.text, "\t", sizeof s.text); scat (s.text, b, sizeof s.text); }
	scpy (s.value, value, sizeof s.value);
}
static bool acc_ok (const Book &b, const Account &a, int filter)
{
	if (a.hidden) return false;
	if (!(filter & AF_HEADINGS) && acc_heading (a.code)) return false;
	if ((filter & AF_NOPARTY) && acc_party (b, a.code)) return false;
	if ((filter & AF_PARTY) && !acc_party (b, a.code)) return false;
	if ((filter & AF_FIN) && acc_kind (a.code) != AK_BANK && acc_kind (a.code) != AK_CASH) return false;
	return true;
}
// The accounts: those whose code starts with the digits typed, then those whose name holds the words;
// nothing typed: the preferred class's first (prefer: "70", "6"...), then the others.
static int sug_accounts (const Book &b, const char *typed, int filter, const char *prefer, Sug *out, int cap)
{
	int n = 0;
	bool dig = all_digits (typed);
	for (int pass = 0; pass < 2 && n < cap; pass++)
		for (int i = 0; i < b.nacc && n < cap; i++)
		{
			const Account &a = b.acc[i];
			if (!acc_ok (b, a, filter)) continue;
			bool in;
			if (!typed[0]) in = pass == 0 ? (prefer && prefer[0] && starts_with (a.code, prefer)) : !(prefer && prefer[0] && starts_with (a.code, prefer));
			else if (pass == 0) in = dig && starts_with (a.code, typed);
			else in = !dig && words_in (a.name, typed);
			if (!in) continue;
			sug_set (out[n++], a.code, a.name, a.code);
		}
	return n;
}
// The parties of a kind (-1: both): code, name, city, VAT number.
static int sug_parties (const Book &b, const char *typed, int kind, Sug *out, int cap, const char *prefix = "P")
{
	int n = 0;
	for (int i = 0; i < b.npty && n < cap; i++)
	{
		const Party &p = b.pty[i];
		if (p.hidden || (kind >= 0 && p.kind != kind)) continue;
		if (typed[0])
		{
			char all[256]; scpy (all, p.code, sizeof all); scat (all, " ", sizeof all); scat (all, p.name, sizeof all);
			scat (all, " ", sizeof all); scat (all, p.city, sizeof all); scat (all, " ", sizeof all); scat (all, p.vat, sizeof all);
			if (!words_in (all, typed)) continue;
		}
		char sub[128]; scpy (sub, p.code, sizeof sub);
		if (p.city[0]) { scat (sub, " \xB7 ", sizeof sub); scat (sub, p.city, sizeof sub); }
		if (kind < 0) scat (sub, p.kind == PK_CUSTOMER ? TR ("  (customer)") : TR ("  (supplier)"), sizeof sub);
		char v[24]; scpy (v, prefix, sizeof v); scat_num (v, p.id, sizeof v);
		sug_set (out[n++], p.name, sub, v);
	}
	// (by name)
	for (int i = 1; i < n; i++) for (int j = i; j > 0 && ci_cmp (out[j].text, out[j - 1].text) < 0; j--) { Sug t = out[j]; out[j] = out[j - 1]; out[j - 1] = t; }
	return n;
}
static int sug_vat (const char *typed, int side, Sug *out, int cap)
{
	int n = 0;
	if (!typed[0] && n < cap) sug_set (out[n++], TR ("(none)"), TR ("No VAT on this line"), "-");
	for (int i = 0; i < NVAT && n < cap; i++)
	{
		const VatDef &d = VAT_DEFS[i];
		if (side >= 0 && d.side != side && d.side != VS_OTHER) continue;
		if (typed[0] && !starts_with (d.code, typed) && !ci_eq (d.code, typed) && !words_in (d.name, typed) && !words_in (TR (d.name), typed))
		{
			char up_[16]; int k = 0; for (const char *q = typed; *q && k < 15; q++) up_[k++] = up (*q);
			up_[k] = '\0';
			if (!starts_with (d.code, up_)) continue;
		}
		sug_set (out[n++], d.code, TR (d.name), d.code);
	}
	return n;
}
static int sug_journals (const Book &b, const char *typed, int type, Sug *out, int cap)
{
	int n = 0;
	for (int i = 0; i < b.njr && n < cap; i++)
	{
		const Journal &j = b.jr[i];
		if (j.hidden || (type >= 0 && j.type != type)) continue;
		if (typed[0] && !words_in (j.code, typed) && !words_in (j.name, typed)) continue;
		char v[8]; itoa10 (i, v);
		sug_set (out[n++], j.code, j.name, v);
	}
	return n;
}
static int suggest (int kind, int arg, const char *prefer, const char *typed, Sug *out, int cap)
{
	char t[64]; trim_copy (t, typed, sizeof t);
	switch (kind)
	{
	case SK_ACCOUNT: return sug_accounts (g_b, t, arg, prefer, out, cap);
	case SK_PARTY: return sug_parties (g_b, t, arg, out, cap);
	case SK_TARGET:
	{
		int n = t[0] ? sug_parties (g_b, t, -1, out, cap) : 0;
		if (!t[0])					// (nothing typed: the customers and suppliers first, then the accounts)
		{
			n = sug_parties (g_b, t, PK_CUSTOMER, out, cap / 2);
			n += sug_parties (g_b, t, PK_SUPPLIER, out + n, cap / 2 - n);
		}
		int m = sug_accounts (g_b, t, AF_NOPARTY, prefer, out + n, cap - n);
		for (int i = n; i < n + m; i++) { char v[24] = "A"; scat (v, out[i].value, sizeof v); scpy (out[i].value, v, sizeof out[i].value); }
		return n + m;
	}
	case SK_VAT: return sug_vat (t, arg, out, cap);
	case SK_JOURNAL: return sug_journals (g_b, t, arg, out, cap);
	}
	return 0;
}

// ---- the list ---------------------------------------------------------------------------------------------------------------------
class SugList;
struct SugOwner { virtual void sugPicked (int i) = 0; virtual Widget *sugField () = 0; virtual ~SugOwner () {} };

class SugList : public Widget
{
public:
	enum { ROWS = 10 };
	const Sug *items; int n, hot, first, rowH;			// (first: the first row shown)
	SugOwner *owner;
	SugList () : Widget (0, 0, 300, 100), items (0), n (0), hot (0), first (0), rowH (wk_fh () + 8), owner (0), m_down (false)
	{ transparent = true; hidden = true; catchOutside = true; }
	int rows () const { return imin (n, ROWS); }
	void ensure () { if (hot < first) first = hot; if (hot >= first + ROWS) first = hot - ROWS + 1; first = iclamp (first, 0, imax (0, n - ROWS)); }
	void onDraw () override
	{
		canvas.clear (WK_TRANSPARENT_KEY);
		wk_popup (canvas, 0, 0, width, height, 7, C_FIELD);
		int sbw = n > ROWS ? WK_SBW + 2 : 0;
		int codeW = 0;
		for (int i = 0; i < n; i++)
		{
			const char *tab = items[i].text; while (*tab && *tab != '\t') tab++;
			if (*tab) { char c[64]; int k = 0; for (const char *q = items[i].text; q < tab && k < 63; q++) c[k++] = *q; c[k] = '\0'; codeW = imax (codeW, wk_text_w (c, 2)); }
		}
		codeW = imin (codeW, width / 2);
		for (int r = 0; r < ROWS && first + r < n; r++)
		{
			int i = first + r, y = 4 + r * rowH;
			bool h = i == hot;
			if (h) wk_hilite (canvas, 4, y, width - 8 - sbw, rowH, 5, true);
			unsigned ink = h ? C_SEL_TEXT : C_FIELD_TEXT, dim = h ? C_SEL_TEXT : field_dim ();
			const char *s = items[i].text, *tab = s; while (*tab && *tab != '\t') tab++;
			char a[128]; int k = 0; for (const char *q = s; q < tab && k < 127; q++) a[k++] = *q; a[k] = '\0';
			Canvas c; c.adopt (canvas.px + 12, imax (1, width - 24 - sbw), height, canvas.stride);
			if (*tab)
			{
				text_fit_l (c, 0, y, codeW + 4, rowH, a, ink, 2);
				text_fit_l (c, codeW + 16, y, c.w - codeW - 16, rowH, tab + 1, dim);
			}
			else text_fit_l (c, 0, y, c.w, rowH, a, ink, a[0] == '(' ? 1 : 0);
		}
		if (sbw) { WkThumb t = wk_thumb (n, ROWS, first, height - 8); wk_draw_vscroll (canvas, width - WK_SBW - 4, 4, WK_SBW, height - 8, t, C_FIELD); }
	}
	int rowAt (int mx, int my) const
	{
		if (mx < 0 || my < 4 || mx >= width || my >= height - 4) return -1;
		int i = first + (my - 4) / rowH;
		return i < n ? i : -1;
	}
	bool onMouse (int mx, int my, int bl, int, int, int wheel) override
	{
		if (hidden) return false;
		bool in = mx >= 0 && my >= 0 && mx < width && my < height;
		if (!in)
		{
			// a press elsewhere (but on the field): closed, the press going on to what is there
			if (bl && !m_down && owner)
			{
				Widget *f = owner->sugField ();
				int fx, fy, lx, ly; abs_pos (f, &fx, &fy); abs_pos (this, &lx, &ly);
				int ax = lx + mx, ay = ly + my;
				bool onField = ax >= fx && ay >= fy && ax < fx + f->width && ay < fy + f->height;
				if (!onField) close ();
			}
			if (!bl) m_down = false;
			return false;
		}
		if (wheel) { first = iclamp (first - wheel, 0, imax (0, n - ROWS)); invalidate (true); return true; }
		int r = rowAt (mx, my);
		if (r >= 0 && r != hot) { hot = r; invalidate (true); }
		if (bl && !m_down) m_down = true;
		else if (!bl && m_down) { m_down = false; if (r >= 0 && owner) owner->sugPicked (r); }
		return true;
	}
	void close () { if (!hidden) { hidden = true; if (parent) parent->invalidate (true); } }
private:
	bool m_down;
};

// Where a widget is within an ancestor (its parents' scroll counted).
static void pos_in (Widget *w, Widget *anc, int *x, int *y)
{
	int ax = 0, ay = 0;
	for (Widget *p = w; p && p != anc && p->parent; p = p->parent) { ax += p->left - p->parent->scrollX; ay += p->top - p->parent->scrollY; }
	*x = ax; *y = ay;
}
// The floating panels' host: the dialog a widget is in, else the window.
static Widget *host_of (Widget *w)
{
	Widget *top = w;
	for (Widget *p = w; p; p = p->parent) { if (p->modal) return p; top = p; }
	return top;
}

// The list's logic for a field: what it offers, where it drops, the one lit.
struct Suggester
{
	SugList *list;
	Sug items[MAXSUG]; int n;
	int kind, arg; const char *prefer;
	Suggester () : list (0), n (0), kind (SK_NONE), arg (0), prefer (0) {}
	~Suggester () { if (list) { if (list->parent) list->parent->removeChild (list); delete list; } }
	bool visible () const { return list && !list->hidden; }
	void hide () { if (list) list->close (); }
	// The suggestions for the text typed, shown under the box (bx, by, bw, bh: in field's coordinates).
	void update (SugOwner *owner, Widget *field, const char *typed, int bx, int by, int bw, int bh, bool always = false)
	{
		n = kind == SK_NONE ? 0 : suggest (kind, arg, prefer, typed, items, MAXSUG);
		if (!n || (!typed[0] && !always)) { hide (); return; }
		if (!list) list = new SugList ();
		list->owner = owner; list->items = items; list->n = n; list->hot = 0; list->first = 0;
		// a value typed in full: that one lit
		char t[64]; trim_copy (t, typed, sizeof t);
		for (int i = 0; i < n; i++) if (ci_eq (items[i].value, t)) { list->hot = i; break; }
		list->ensure ();
		Widget *h = host_of (field);
		if (list->parent != h) { if (list->parent) list->parent->removeChild (list); h->addChild (list); }
		else list->bringToFront ();
		int fx, fy; pos_in (field, h, &fx, &fy);
		int w = imax (bw, 440), hh = 8 + list->rows () * list->rowH;
		int x = fx + bx, y = fy + by + bh + 2;
		if (y + hh > h->height && fy + by - hh - 2 >= 0) y = fy + by - hh - 2;
		if (x + w > h->width) x = imax (0, h->width - w);
		list->left = x; list->top = y;
		if (list->width != w || list->height != hh) list->resizeTo (w, hh);
		list->hidden = false;
		list->invalidate (true);
	}
	// Up / Down / Page Up / Page Down in the list (true: taken).
	bool key (long k)
	{
		if (!visible ()) return false;
		int d = k == KEY_UP ? -1 : k == KEY_DOWN ? 1 : k == KEY_PGUP ? -SugList::ROWS : k == KEY_PGDN ? SugList::ROWS : 0;
		if (!d) return false;
		list->hot = iclamp (list->hot + d, 0, n - 1);
		list->ensure ();
		list->invalidate (true);
		return true;
	}
	const Sug *lit () const { return visible () && list->hot >= 0 && list->hot < n ? &items[list->hot] : 0; }
};

// ---- a field that picks -------------------------------------------------------------------------------------------------------------
// A line to type a party's or an account's name or code in; the list drops as it is typed (and from its
// button: all of them). The value chosen (value ()): the suggestion's; typed and not chosen: resolved
// when it names one alone (resolve ()).
class PickEdit : public LineEdit, public SugOwner
{
public:
	Suggester sug;
	char val[24];				// the value chosen ("": none)
	Action onPick;				// a value chosen
	PickEdit (int l, int t, int w, int kind, int arg = 0, const char *prefer = 0) : LineEdit (l, t, w), onPick (0), m_btnHot (false)
	{ sug.kind = kind; sug.arg = arg; sug.prefer = prefer; val[0] = '\0'; padR = 22; }
	Widget *sugField () override { return this; }
	// The value and the text shown for it.
	void setValue (const char *v)
	{
		scpy (val, v, sizeof val);
		char t[160]; shown (v, t, sizeof t); setText (t);
	}
	void shown (const char *v, char *out, int cap)
	{
		out[0] = '\0';
		if (!v[0]) return;
		if (sug.kind == SK_PARTY || (sug.kind == SK_TARGET && v[0] == 'P'))
		{
			const Party *p = party_of (g_b, cell_int (v + 1));
			if (p) scpy (out, p->name, cap);
		}
		else if (sug.kind == SK_ACCOUNT || sug.kind == SK_TARGET)
		{
			const char *c = sug.kind == SK_TARGET ? v + 1 : v;
			scpy (out, c, cap); const char *nm = acc_name (g_b, c);
			if (nm[0]) { scat (out, "  ", cap); scat (out, nm, cap); }
		}
		else if (sug.kind == SK_JOURNAL) { int j = cell_int (v); if (j >= 0 && j < g_b.njr) { scpy (out, g_b.jr[j].code, cap); scat (out, "  ", cap); scat (out, g_b.jr[j].name, cap); } }
		else scpy (out, v, cap);
	}
	void sugPicked (int i) override
	{
		if (i < 0 || i >= sug.n) return;
		sug.hide ();
		setValue (sug.items[i].value);
		setFocus ();
		selectAll ();
		if (onPick) onPick (*this);
	}
	// The text typed, when no suggestion was taken: the one it names alone (an exact code, a single match).
	bool resolve ()
	{
		char t[64]; trim_copy (t, text (), sizeof t);
		if (!t[0]) { bool had = val[0] != 0; val[0] = '\0'; if (had && onPick) onPick (*this); return true; }
		char cur[160]; shown (val, cur, sizeof cur);
		if (val[0] && seq (cur, text ())) return true;		// (unchanged)
		Sug s[4]; int n = suggest (sug.kind, sug.arg, sug.prefer, t, s, 4);
		for (int i = 0; i < n; i++) if (ci_eq (s[i].value, t) || (sug.kind == SK_ACCOUNT && seq (s[i].value, t))) { setValue (s[i].value); if (onPick) onPick (*this); return true; }
		if (n == 1) { setValue (s[0].value); if (onPick) onPick (*this); return true; }
		return false;
	}
	const char *value () const { return val; }
	void drawExtra () override
	{
		int bw = 18, bx = width - bw - 3, bh = height - 6;
		wk_raised (canvas, bx, 3, bw, bh, 3, C_BUTTON, disabled ? WK_DISABLED : m_btnHot ? WK_HOT : WK_NORMAL);
		wk_glyph (canvas, WKG_CHEV_DOWN, bx + bw / 2, height / 2, 8, C_BUTTON_TEXT);
	}
	void edited () override
	{
		LineEdit::edited ();
		val[0] = '\0';
		sug.update (this, this, text (), 0, 0, width, height);
	}
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override
	{
		bool onBtn = mx >= width - 22 && mx < width && my >= 0 && my < height;
		if (onBtn != m_btnHot) { m_btnHot = onBtn; invalidate (true); }
		if (onBtn && !wheel && !disabled)
		{
			if (bl && !pressed) { pressed = true; if (!hasFocus) setFocus (); return true; }
			if (!bl && pressed) { pressed = false; if (sug.visible ()) sug.hide (); else sug.update (this, this, "", 0, 0, width, height, true); return true; }
			return true;
		}
		return LineEdit::onMouse (mx, my, bl, br, bm, wheel);
	}
	bool onKey (long k) override
	{
		if (disabled) return false;
		if (sug.key (k)) return true;
		if (sug.visible () && (k == KEY_ENTER || k == KEY_TAB)) { const Sug *s = sug.lit (); if (s) { int i = (int) (s - sug.items); sugPicked (i); } if (k == KEY_ENTER) return true; return false; }
		if (k == 27 && sug.visible ()) { sug.hide (); return true; }
		if ((k == KEY_DOWN && (kapi_get_modifiers () & MOD_ALT)) || k == KEY_F1 + 3) { sug.update (this, this, "", 0, 0, width, height, true); return true; }
		if (k == KEY_TAB || k == KEY_ENTER) { sug.hide (); resolve (); return false; }
		return LineEdit::onKey (k);
	}
private:
	bool m_btnHot;
};

} // namespace lg

#endif
