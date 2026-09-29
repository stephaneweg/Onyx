//
// formview.h -- the Form view: the record shown as an index card -- on top the form's title and its
// description and the record's place ("3 / 12"), then each field: its name, and the editor its type
// takes (a line, several lines, a number, a date, a colour, a check box, a choice). The editors' values
// go into the record when it is left (commit: another record, another view, Save...): a value that is
// not one of its type (a date that does not exist, letters in a number) is said, outlined in red, and
// its editor keeps the keyboard. Tab / Shift+Tab, Enter, Up / Down move between the fields; Page Up /
// Page Down show the previous / next record; Esc puts the record's values back. The card scrolls when
// the form is taller than the window (the wheel, its scroll bar); a click on a field's name gives its
// editor the keyboard (a check box's: ticks it).
//
#ifndef _cardfile_formview_h
#define _cardfile_formview_h

#include "app.h"

namespace cf {

class FormView : public Widget
{
public:
	enum { CARD_Y = 14, HEAD_H = 72, PAD = 24, GAP = 8, MEMO_H = 92, SB_ROOM = 18, MAXW = 820 };
	FormView (int l, int t, int w, int h)
		: Widget (l, t, w, h), m_ne (0), m_ver (0), m_rec (-2), m_cardX (0), m_cardW (0), m_cardH (0), m_labW (0), m_contentH (0),
		  m_lw (-1), m_lh (-1), m_bar (false)
	{
		canFocus = true;			// (redrawn when the keyboard moves: the focused field's name lit)
		m_btn = new Button (0, 0, 160, 30, "", empty_click);
		m_btn->hidden = true;
		addChild (m_btn);
	}
	unsigned bgColor () override { return cardColor (); }	// (the editors lie on the card)
	static unsigned cardColor () { return wk_tone (C_BG, 150); }

	// ---- the editors ----------------------------------------------------------------------------------------
	void build ()
	{
		for (int k = 0; k < m_ne; k++) { removeChild (m_ed[k]); delete m_ed[k]; }
		m_ne = 0;
		for (int k = 0; k < g_doc.nf; k++)
		{
			const Field &f = g_doc.f[k];
			Widget *w;
			switch (f.type)
			{
			case FT_MEMO: w = new MemoEdit (0, 0, 100, MEMO_H); break;
			case FT_INT: case FT_DEC:
			{
				LineEdit *e = new LineEdit (0, 0, 100);
				e->rightAlign = true; e->accept = f.type == FT_INT ? accept_int : accept_dec;
				w = e; break;
			}
			case FT_DATE: w = new DateEdit (0, 0, 100); break;
			case FT_COLOR: w = new ColorEdit (0, 0, 100); break;
			case FT_BOOL: w = new YesNoBox (0, 0, 22); break;
			case FT_CHOICE: { ChoiceBox *c = new ChoiceBox (0, 0, 100); c->setField (f); w = c; break; }
			default: w = new LineEdit (0, 0, 100); break;
			}
			w->tag = k;
			m_ed[m_ne++] = w;
			addChild (w);
		}
		m_ver = g_fieldsVer;
		m_rec = -2;
		place ();
	}
	// The card: as wide as the view lets it (up to MAXW), centred; the names' column as wide as the
	// longest name; each editor as its type needs.
	void place ()
	{
		m_cardW = imin (width - 2 * 16 - SB_ROOM, MAXW);
		if (m_cardW < 280) m_cardW = imax (120, width - 8 - SB_ROOM);
		m_cardX = imax (4, (width - SB_ROOM - m_cardW) / 2);
		m_labW = 60;
		for (int k = 0; k < g_doc.nf; k++) m_labW = imax (m_labW, wk_text_w (g_doc.f[k].label));
		m_labW = imin (m_labW, imax (60, m_cardW / 3));
		int x = m_cardX + PAD + m_labW + 14, full = imax (60, m_cardX + m_cardW - PAD - x);
		int y = CARD_Y + HEAD_H;
		for (int k = 0; k < m_ne; k++)
		{
			const Field &f = g_doc.f[k];
			int w = full, h = ED_H;
			switch (f.type)
			{
			case FT_MEMO: h = MEMO_H; break;
			case FT_INT: w = 170; break;
			case FT_DEC: w = 190; break;
			case FT_DATE: w = 150; break;
			case FT_COLOR: w = 170; break;
			case FT_BOOL: w = 22; break;
			case FT_CHOICE:
				w = 200;
				for (int i = 0; i < f.nch; i++) w = imax (w, wk_text_w (f.ch[i]) + 50);
				break;
			}
			w = imin (w, full);
			m_ed[k]->left = x; m_ed[k]->top = y;
			m_ed[k]->resizeTo (w, h);
			m_y[k] = y; m_h[k] = h;
			y += h + GAP;
		}
		if (!m_ne || m_rec < 0 || g_cur < 0) y = CARD_Y + HEAD_H + 110;
		m_cardH = y - GAP + 20 - CARD_Y;
		m_contentH = CARD_Y + m_cardH + 16;
		m_btn->left = m_cardX + (m_cardW - m_btn->width) / 2; m_btn->top = CARD_Y + HEAD_H + 58;
		clampScroll ();
		invalidate (true);
	}
	void layout () override { if (width != m_lw || height != m_lh) { m_lw = width; m_lh = height; place (); } }
	void clampScroll () { scrollY = iclamp (scrollY, 0, imax (0, m_contentH - height)); }

	// The record's value k into its editor.
	void setEditor (int k, const char *v)
	{
		Widget *w = m_ed[k];
		const Field &f = g_doc.f[k];
		switch (f.type)
		{
		case FT_MEMO: ((MemoEdit *) w)->setText (v); ((MemoEdit *) w)->setError (false); break;
		case FT_COLOR: ((ColorEdit *) w)->setValue (v); break;
		case FT_BOOL: ((YesNoBox *) w)->checked = v[0] != 0; w->invalidate (true); break;
		case FT_CHOICE: ((ChoiceBox *) w)->setValue (v); break;
		default:
		{
			char s[VAL_MAX]; value_show (f, v, s, sizeof s);
			LineEdit *e = (LineEdit *) w;
			e->setText (s); e->setError (false);
		}
		}
	}
	// Editor k's content, as the field's value would be typed (tmp: room for a colour's code).
	const char *editorText (int k, char *tmp)
	{
		Widget *w = m_ed[k];
		switch (g_doc.f[k].type)
		{
		case FT_MEMO: return ((MemoEdit *) w)->text ();
		case FT_COLOR: ((ColorEdit *) w)->value (tmp); return tmp;
		case FT_BOOL: return ((YesNoBox *) w)->checked ? "yes" : "";
		case FT_CHOICE: return ((ChoiceBox *) w)->value ();
		}
		return ((LineEdit *) w)->text ();
	}
	// Does editor k still hold the record's value? (the text shown for it, or the same value typed otherwise)
	bool same (int k)
	{
		const Field &f = g_doc.f[k];
		const char *v = g_doc.r[m_rec][k];
		char tmp[16];
		const char *e = editorText (k, tmp);
		if (f.type == FT_TEXT || f.type == FT_MEMO)
		{
			char *p = value_new (f, e);
			bool s = p && seq (p, v);
			sfree (p);
			return s;
		}
		char shown[VAL_MAX]; value_show (f, v, shown, sizeof shown);
		if (seq (e, shown)) return true;
		char *p = value_new (f, e, 0, true);
		bool s = p && seq (p, v);
		sfree (p);
		return s;
	}
	bool live () const { return m_rec >= 0 && m_rec == g_cur && m_ver == g_fieldsVer && g_cur < g_doc.nr && m_ne == g_doc.nf; }
	bool dirty () { if (!live ()) return false; for (int k = 0; k < m_ne; k++) if (!same (k)) return true; return false; }

	// The shown record's values into its editors (fields changed: the editors made again first). The
	// field that had the keyboard keeps it.
	void load ()
	{
		int focused = focusedField ();
		if (m_ver != g_fieldsVer || m_ne != g_doc.nf) build ();		// (the same place keeps the keyboard)
		bool show = g_cur >= 0 && g_cur < g_doc.nr;
		m_rec = show ? g_cur : -1;
		for (int k = 0; k < m_ne; k++)
		{
			m_ed[k]->hidden = !show;
			if (show) setEditor (k, g_doc.r[g_cur][k]);
		}
		m_btn->hidden = show;
		if (!show) scpy (m_btn->text, g_doc.nr ? "Clear Search" : "New Record", sizeof m_btn->text);
		place ();
		if (show && focused >= 0 && focused < m_ne) m_ed[focused]->setFocus ();
		invalidate (true);
	}
	int focusedField () { for (int k = 0; k < m_ne; k++) if (m_ed[k]->hasFocus && !m_ed[k]->hidden) return k; return -1; }
	void focusField (int k, bool selectAll = true)
	{
		if (k < 0 || k >= m_ne || m_ed[k]->hidden) { if (m_btn->hidden) setFocus (); else m_btn->setFocus (); return; }
		m_ed[k]->setFocus ();
		int t = g_doc.f[k].type;
		if (selectAll && (t == FT_TEXT || t == FT_INT || t == FT_DEC || t == FT_DATE)) ((LineEdit *) m_ed[k])->selectAll ();
		ensureVisible (k);
	}
	void focusFirst () { focusField (0, false); }
	void ensureVisible (int k)
	{
		if (k < 0 || k >= m_ne) return;
		int top = m_y[k] - 12, bot = m_y[k] + m_h[k] + 12;
		if (k == 0) top = 0;
		if (top < scrollY) scrollY = top;
		if (bot > scrollY + height) scrollY = bot - height;
		clampScroll ();
		invalidate (true);
	}

	// The editors' values into the record. False: a value is not one of its type (said unless `quiet`;
	// its editor outlined and given the keyboard) -- the record unchanged.
	bool commit (bool quiet)
	{
		if (!live ()) return true;
		char *nv[MAXF]; bool any = false;
		for (int k = 0; k < m_ne; k++) nv[k] = 0;
		for (int k = 0; k < m_ne; k++)
		{
			if (same (k)) continue;
			const Field &f = g_doc.f[k];
			char tmp[16]; bool exact = true;
			char *v = value_new (f, editorText (k, tmp), &exact, true);
			if (!v || (f.type == FT_INT && !exact))
			{
				sfree (v);
				for (int j = 0; j < k; j++) sfree (nv[j]);
				if (!quiet) invalid (k);
				return false;
			}
			nv[k] = v; any = true;
		}
		if (!any) return true;
		undo_mark (-1);
		for (int k = 0; k < m_ne; k++) if (nv[k]) value_set (g_doc.r[m_rec][k], nv[k]);
		doc_changed (false);
		return true;
	}
	void invalid (int k)
	{
		const Field &f = g_doc.f[k];
		int t = f.type;
		if (t == FT_TEXT || t == FT_INT || t == FT_DEC || t == FT_DATE) ((LineEdit *) m_ed[k])->setError (true);	// (the others cannot hold a wrong value)
		focusField (k);
		char msg[240] = "\"";
		scat (msg, f.label, sizeof msg);
		switch (t)
		{
		case FT_INT: scat (msg, "\" takes a whole number, such as 42 or -7.", sizeof msg); break;
		case FT_DEC: scat (msg, "\" takes a number, such as 12.5 or 1 234,75.", sizeof msg); break;
		case FT_DATE: scat (msg, "\" takes a date that exists, typed as DD/MM/YYYY (29/09/2026, 29.9.26, 2026-09-29) or picked on the calendar.", sizeof msg); break;
		default: scat (msg, "\": this value is not valid.", sizeof msg); break;
		}
		ask ("Not a valid value", msg, MB_OK, 2);
		focusField (k);
	}
	// Esc: the record's values back in the editors.
	void revert () { if (dirty ()) { int k = focusedField (); load (); if (k >= 0) focusField (k, false); status ("The record's changes were undone"); } }

	// ---- drawing ----------------------------------------------------------------------------------------
	void onDraw () override
	{
		canvas.clear (C_BG);
		unsigned card = cardColor (), ink = wk_ink_for (card), dim = wk_mix (card, ink, 150);
		int x = m_cardX, y = CARD_Y - scrollY, w = m_cardW, h = m_cardH;
		wk_rbox (canvas, x + 1, y + 3, w, h, 10, wk_tone (C_BG, 90), wk_tone (C_BG, 90), 70);	// (a shadow)
		wk_rbox (canvas, x, y, w, h, 10, wk_tone (card, 136), card);
		// the head: a band of the accent, the title, the description, the record's place
		unsigned band = wk_mix (card, C_ACCENT, 46);
		wk_rbox (canvas, x + 1, y + 1, w - 2, HEAD_H - 16, 9, wk_mix (card, C_ACCENT, 70), band, 255, WK_TL | WK_TR);
		canvas.fillRect (x + 1, y + HEAD_H - 16, w - 2, 1, wk_mix (card, C_ACCENT, 110));
		unsigned tink = wk_ink_on (band) == wk_ink_on (card) ? wk_tone (C_ACCENT, 60) : wk_ink_on (band);
		if (wk_bright (tink) > 150 && wk_bright (band) > 140) tink = ink;
		char badge[40] = "";
		int pos = cur_pos ();
		if (g_cur >= 0 && pos >= 0)
		{
			if (g_cur == g_pin && rec_empty (g_doc, g_cur)) scpy (badge, "New", sizeof badge);
			else { itoa10 (pos + 1, badge); scat (badge, " / ", sizeof badge); scat_num (badge, g_nord, sizeof badge); }
		}
		int bw = badge[0] ? wk_text_w (badge, 2) + 22 : 0;
		Canvas t; t.adopt (canvas.px + x + PAD, imax (1, w - 2 * PAD - bw - 8), canvas.h, canvas.stride);
		wk_text_l (t, 0, y + 10, 20, g_doc.title, tink, 2);
		if (g_doc.info[0]) wk_text_l (t, 0, y + 32, 20, g_doc.info, wk_mix (band, tink, 170));
		if (bw)
		{
			int bx = x + w - PAD - bw + 8, by = y + 12;
			wk_rbox (canvas, bx, by, bw, 24, 12, wk_tone (C_ACCENT, 150), wk_tone (C_ACCENT, 118));
			wk_text_c (canvas, bx, by, bw, 24, badge, C_SEL_TEXT, 2);
		}
		wk_rline (canvas, x, y, w, h, 10, wk_tone (C_BG, 84), 200);
		// the fields' names (a check box's, and a focused editor's, in the accent)
		if (m_rec >= 0 && g_cur >= 0)
		{
			for (int k = 0; k < m_ne; k++)
			{
				const char *l = g_doc.f[k].label;
				int lw = wk_text_w (l), ly = m_y[k] - scrollY;
				bool f = m_ed[k]->hasFocus;
				Canvas lc; lc.adopt (canvas.px + x + PAD, imax (1, m_labW + 4), canvas.h, canvas.stride);
				wk_text_l (lc, imax (0, m_labW - lw), ly, ED_H, l, f ? wk_tone (C_ACCENT, 70) : ink, f ? 2 : 0);
			}
		}
		else
		{
			const char *a = g_doc.nr ? "No record matches the search." : "This form has no records yet.";
			const char *b = g_doc.nr ? "Clear the search to see them all." : "New Record (Ctrl+R) adds the first one.";
			if (!g_doc.nf) { a = "This form has no fields."; b = "The Design view adds them."; }
			wk_text_c (canvas, x, y + HEAD_H + 6, w, 20, a, ink, 2);
			wk_text_c (canvas, x, y + HEAD_H + 28, w, 20, b, dim);
		}
		// the scroll bar
		if (m_contentH > height)
		{
			WkThumb th = wk_thumb (m_contentH, height, scrollY, height - 8);
			wk_draw_vscroll (canvas, width - WK_SBW - 4, 4, WK_SBW, height - 8, th, C_BG, m_bar);
		}
	}
	// (the Widget draws its children at their place less the scroll; the card itself follows scrollY here)

	bool onMouse (int mx, int my, int bl, int, int, int wheel) override
	{
		if (wheel) { scrollY -= wheel * 40; clampScroll (); invalidate (true); return true; }
		if (mx < 0) { if (m_bar) { m_bar = false; catchOutside = false; invalidate (true); } pressed = false; return false; }
		bool over = m_contentH > height && mx >= width - WK_SBW - 6;
		if (bl && (m_bar || (over && !pressed)))
		{
			if (!m_bar) { m_bar = true; catchOutside = true; }
			WkThumb th = wk_thumb (m_contentH, height, scrollY, height - 8);
			scrollY = (int) wk_thumb_pos (my - 4, height - 8, m_contentH, height, th.h);
			clampScroll (); invalidate (true);
			pressed = true;
			return true;
		}
		if (!bl) { if (m_bar) { m_bar = false; catchOutside = false; invalidate (true); } pressed = false; return true; }
		if (pressed) return true;
		pressed = true;
		// a click on a field's name: its editor
		int cy = my + scrollY;
		if (m_rec >= 0 && mx >= m_cardX && mx < m_cardX + PAD + m_labW + 14)
			for (int k = 0; k < m_ne; k++)
				if (cy >= m_y[k] && cy < m_y[k] + ED_H)
				{
					if (g_doc.f[k].type == FT_BOOL) { YesNoBox *c = (YesNoBox *) m_ed[k]; c->checked = !c->checked; c->invalidate (true); }
					focusField (k);
					invalidate (true);
					return true;
				}
		return true;
	}
	bool onKey (long k) override
	{
		bool shift = (kapi_get_modifiers () & MOD_SHIFT) != 0;
		if ((k == KEY_ENTER || k == ' ') && !m_btn->hidden && m_btn->hasFocus) { empty_click (*m_btn); return true; }
		int f = focusedField ();
		switch (k)
		{
		case KEY_TAB: next (shift ? -1 : 1, true); return true;
		case KEY_ENTER: if (f >= 0) { next (1, true); return true; } return false;
		case KEY_DOWN: if (f >= 0) { next (1, false); return true; } return false;
		case KEY_UP: if (f >= 0) { next (-1, false); return true; } return false;
		case KEY_PGUP: goto_pos (cur_pos () - 1); return true;
		case KEY_PGDN: goto_pos (cur_pos () + 1); return true;
		case KEY_HOME: if (!(kapi_get_modifiers () & MOD_CTRL)) return false; goto_pos (0); return true;
		case KEY_END: if (!(kapi_get_modifiers () & MOD_CTRL)) return false; goto_pos (g_nord - 1); return true;
		case 27: revert (); return true;
		}
		return false;
	}
	void next (int dir, bool wrap)
	{
		if (!m_ne || m_rec < 0) return;
		int f = focusedField ();
		int k = f < 0 ? (dir > 0 ? 0 : m_ne - 1) : f + dir;
		if (k < 0) k = wrap ? m_ne - 1 : 0;
		if (k >= m_ne) k = wrap ? 0 : m_ne - 1;
		focusField (k);
		invalidate (true);
	}
private:
	Widget *m_ed[MAXF]; int m_ne;
	int m_y[MAXF], m_h[MAXF];
	unsigned m_ver; int m_rec;
	int m_cardX, m_cardW, m_cardH, m_labW, m_contentH, m_lw, m_lh;
	bool m_bar;
	Button *m_btn;				// New Record / Clear Search, when no record is shown
	static void empty_click (Widget &) { if (g_doc.nr) cmd_clear_search (); else cmd_new_record (); }
};

} // namespace cf

#endif
