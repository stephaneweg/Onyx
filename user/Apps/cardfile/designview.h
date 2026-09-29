//
// designview.h -- the Design view: the form itself. On the left its fields in their order (a grid: the
// number, the display name, the type); Add Field (after the one chosen), Remove, Move Up, Move Down. On
// the right the field chosen -- its display name (the form's and the list's), its column name (the
// file's: letters, digits, _ and -; made from the display name as long as it was not typed), its type,
// and the type's option: a decimal number's decimals, a choice list's choices (one a line) -- then the
// form's title and description. A change applies at once (Undo takes it back). A new type converts the
// values through their text: what reads as the new type stays, the rest is emptied (asked first when
// some would be); to a choice list, the values become its choices.
//
#ifndef _cardfile_designview_h
#define _cardfile_designview_h

#include "app.h"

namespace cf {

static const char *const TYPE_HINT[FT_COUNT] = {
	"One line of text.",
	"Several lines, wrapped at the words.",
	"A whole number: 42, -7, 1 000 000.",
	"",
	"A date: typed as DD/MM/YYYY, or picked on a calendar.",
	"A colour: from a palette or the colour dialog.",
	"A check box: yes or no.",
	"" };

class DesignView;
static DesignView *s_dv;

class DesignView : public Widget
{
public:
	DataGrid *list;
	LineEdit *eLabel, *eColumn, *eTitle, *eInfo;
	ChoiceBox *cType;
	NumericUpDown *nDec;
	MemoEdit *mChoices;
	Label *lOpt, *lHint, *lDecHint, *lChHint, *lStats;
	GroupBox *gFields, *gField, *gForm;
	bool editing;

	DesignView (int l, int t, int w, int h) : Widget (l, t, w, h), editing (false)
	{
		s_dv = this;
		int lw = 420;
		gFields = new GroupBox (12, 6, lw, h - 12, "Fields");
		gFields->anchor = ANCHOR_LEFT | ANCHOR_TOP | ANCHOR_BOTTOM;
		addChild (gFields);
		int ct = gFields->contentTop () + 4;
		list = new DataGrid (12, ct, lw - 24, h - 12 - ct - 50);
		list->anchor = ANCHOR_FILL;
		list->sortable = false;
		list->setColumns (3);
		list->setColumn (0, "#", 36, GRID_RIGHT);
		list->setColumn (1, "Field", lw - 24 - 36 - 172 - 2);
		list->setColumn (2, "Type", 172);
		list->cellText = cell_text;
		list->onSelect = on_pick;
		list->onActivate = on_activate;
		gFields->addChild (list);
		int by = h - 12 - 40, bx = 12;
		static const char *const BT[4] = { "Add Field", "Remove", "Move Up", "Move Down" };
		static const int BW[4] = { 100, 84, 90, 100 };
		for (int i = 0; i < 4; i++)
		{
			Button *b = new Button (bx, by, BW[i], 30, BT[i], on_button);
			b->tag = i; b->anchor = ANCHOR_LEFT | ANCHOR_BOTTOM;
			gFields->addChild (b);
			bx += BW[i] + 6;
		}

		int gx = 12 + lw + 14, gw = w - gx - 12;
		gField = new GroupBox (gx, 6, gw, 306, "Field");
		gField->anchor = ANCHOR_LEFT | ANCHOR_TOP | ANCHOR_RIGHT;
		addChild (gField);
		int cx = 128, cw = gw - cx - 14;
		label (gField, 30, "Display name");
		eLabel = new LineEdit (cx, 28, cw); eLabel->onChange = on_label; eLabel->t.max = LABEL_MAX - 1; eLabel->anchor = ANCHOR_LEFT | ANCHOR_TOP | ANCHOR_RIGHT;
		gField->addChild (eLabel);
		label (gField, 66, "Column name");
		eColumn = new LineEdit (cx, 64, cw); eColumn->onChange = on_column; eColumn->t.max = COL_MAX - 1; eColumn->accept = accept_column;
		eColumn->anchor = ANCHOR_LEFT | ANCHOR_TOP | ANCHOR_RIGHT;
		eColumn->tip = "The column's name in the file: letters, digits, _ and -";
		gField->addChild (eColumn);
		label (gField, 102, "Type");
		cType = new ChoiceBox (cx, 100, 210); cType->setOptions (TYPE_NAME, FT_COUNT); cType->onChange = on_type;
		gField->addChild (cType);
		lOpt = label (gField, 138, "Decimals");
		nDec = new NumericUpDown (cx, 136, 84, ED_H, 0, MAXDEC, 2, 1, on_dec);
		gField->addChild (nDec);
		lDecHint = new Label (cx + 96, 136, cw - 96, ED_H, "");
		lDecHint->anchor = ANCHOR_LEFT | ANCHOR_TOP | ANCHOR_RIGHT;
		gField->addChild (lDecHint);
		mChoices = new MemoEdit (cx, 136, cw, 108, 8000); mChoices->onChange = on_choices;
		mChoices->anchor = ANCHOR_LEFT | ANCHOR_TOP | ANCHOR_RIGHT;
		gField->addChild (mChoices);
		lChHint = new Label (cx, 248, cw, 20, "One choice a line, in their order.", wk_mix (C_BG, C_TEXT, 150));
		lChHint->anchor = ANCHOR_LEFT | ANCHOR_TOP | ANCHOR_RIGHT;
		gField->addChild (lChHint);
		lStats = new Label (16, 276, gw - 30, 20, "", wk_mix (C_BG, C_TEXT, 150));
		lStats->anchor = ANCHOR_LEFT | ANCHOR_TOP | ANCHOR_RIGHT;
		gField->addChild (lStats);
		lHint = new Label (cx, 136, cw, ED_H, "", wk_mix (C_BG, C_TEXT, 150));
		lHint->anchor = ANCHOR_LEFT | ANCHOR_TOP | ANCHOR_RIGHT;
		gField->addChild (lHint);

		gForm = new GroupBox (gx, 322, gw, 108, "Form");
		gForm->anchor = ANCHOR_LEFT | ANCHOR_TOP | ANCHOR_RIGHT;
		addChild (gForm);
		label (gForm, 30, "Title");
		eTitle = new LineEdit (cx, 28, cw); eTitle->onChange = on_title; eTitle->t.max = TITLE_MAX - 1; eTitle->anchor = ANCHOR_LEFT | ANCHOR_TOP | ANCHOR_RIGHT;
		gForm->addChild (eTitle);
		label (gForm, 66, "Description");
		eInfo = new LineEdit (cx, 64, cw); eInfo->onChange = on_info; eInfo->t.max = INFO_MAX - 1; eInfo->anchor = ANCHOR_LEFT | ANCHOR_TOP | ANCHOR_RIGHT;
		eInfo->placeholder = "(none)";
		gForm->addChild (eInfo);
		Label *foot = new Label (gx + 4, 440, gw - 8, 22, "Changes apply at once (Ctrl+Z undoes them).", wk_mix (C_BG, C_TEXT, 150));
		foot->anchor = ANCHOR_LEFT | ANCHOR_TOP | ANCHOR_RIGHT;
		addChild (foot);
	}
	unsigned bgColor () override { return C_BG; }
	void onDraw () override { canvas.clear (C_BG); }
	Label *label (Widget *g, int y, const char *s)
	{
		Label *l = new Label (16, y, 108, 20, s);
		g->addChild (l);
		return l;
	}

	// The list and (not while one of them is being edited) the field's properties, as the document is.
	void sync ()
	{
		list->setRows (g_doc.nf);
		g_fsel = iclamp (g_fsel, 0, imax (0, g_doc.nf - 1));
		list->setSel (g_doc.nf ? g_fsel : -1);
		list->invalidate (true);
		if (!editing) loadField ();
	}
	void loadField ()
	{
		bool has = g_doc.nf > 0;
		Widget *props[] = { eLabel, eColumn, cType, nDec, mChoices };
		for (unsigned i = 0; i < sizeof props / sizeof props[0]; i++) props[i]->disabled = !has;
		if (!has)
		{
			eLabel->setText (""); eColumn->setText (""); lHint->setText ("No field: Add Field makes one.");
			nDec->hidden = mChoices->hidden = lChHint->hidden = lDecHint->hidden = true; lOpt->hidden = true; lHint->hidden = false;
		}
		else
		{
			const Field &f = g_doc.f[g_fsel];
			eLabel->setText (f.label); eLabel->setError (false);
			eColumn->setText (f.column); eColumn->setError (false);
			cType->sel = f.type; cType->invalidate (true);
			bool dec = f.type == FT_DEC, ch = f.type == FT_CHOICE;
			nDec->hidden = lDecHint->hidden = !dec;
			mChoices->hidden = lChHint->hidden = !ch;
			lOpt->hidden = !dec && !ch;
			lHint->hidden = dec || ch;
			lOpt->setText (dec ? "Decimals" : "Choices");
			if (dec) { nDec->value = f.prec; nDec->invalidate (true); decHint (f.prec); }
			if (ch)
			{
				Out o;
				for (int i = 0; i < f.nch; i++) { if (i) o.put ('\n'); o.puts (f.ch[i]); }
				mChoices->setText (o.b ? o.b : "");
			}
			lHint->setText (TYPE_HINT[f.type]);
			// what the records hold in it
			int filled = 0;
			for (int i = 0; i < g_doc.nr; i++) if (g_doc.r[i][g_fsel][0]) filled++;
			char st[96] = "In the records: ";
			scat_num (st, filled, sizeof st);
			if (f.type == FT_BOOL) { scat (st, " yes, ", sizeof st); scat_num (st, g_doc.nr - filled, sizeof st); scat (st, " no", sizeof st); }
			else
			{
				scat (st, filled == 1 ? " value, " : " values, ", sizeof st);
				scat_num (st, g_doc.nr - filled, sizeof st); scat (st, " empty", sizeof st);
			}
			lStats->setText (st);
		}
		lStats->hidden = !has;
		eTitle->setText (g_doc.title);
		eInfo->setText (g_doc.info);
		for (Widget *w = gField->firstChild; w; w = w->nextSib) w->invalidate (true);
		gField->invalidate (true);			// (its face painted again: a control hidden leaves nothing)
		gForm->invalidate (true);
		invalidate (true);
	}
	void decHint (int p) { char b[64] = "Shown as "; char n[24]; fmt_num (123450000LL / ipow10 (6 - p) , p, n); scat (b, n, sizeof b); lDecHint->setText (b); }
	void focusLabel () { eLabel->setFocus (); eLabel->selectAll (); }
	bool onKey (long k) override
	{
		if (k == KEY_DEL && list->hasFocus) { remove (); return true; }
		return false;
	}

	// ---- the commands --------------------------------------------------------------------------------------
	void add ()
	{
		if (g_doc.nf >= MAXF) { ask ("Add Field", "A form holds 64 fields at most.", MB_OK, 2); return; }
		undo_mark (-1);
		Field f; char lab[LABEL_MAX] = "Field "; scat_num (lab, g_doc.nf + 1, sizeof lab);
		char col[COL_MAX]; make_column (lab, col, sizeof col);
		field_init (f, lab, col, FT_TEXT);
		g_fsel = doc_insert_field (g_doc, g_doc.nf ? g_fsel + 1 : 0, f);
		doc_changed (true);
		focusLabel ();
		status ("Field added: name it, then choose its type");
	}
	void remove ()
	{
		if (!g_doc.nf) return;
		const Field &f = g_doc.f[g_fsel];
		int n = 0;
		for (int i = 0; i < g_doc.nr; i++) if (g_doc.r[i][g_fsel][0]) n++;
		char msg[200] = "Remove the field "; scat (msg, f.label, sizeof msg); scat (msg, "?", sizeof msg);
		if (n) { scat (msg, " Its values in ", sizeof msg); scat_num (msg, n, sizeof msg); scat (msg, n == 1 ? " record go with it." : " records go with it.", sizeof msg); }
		if (ask ("Remove Field", msg, MB_YESNO, 1) != 1) return;
		undo_mark (-1);
		doc_remove_field (g_doc, g_fsel);
		g_fsel = imin (g_fsel, imax (0, g_doc.nf - 1));
		doc_changed (true);
		list->setFocus ();
	}
	void move (int dir)
	{
		if (g_fsel + dir < 0 || g_fsel + dir >= g_doc.nf) return;
		undo_mark (-1);
		doc_move_field (g_doc, g_fsel, dir);
		g_fsel += dir;
		doc_changed (true);
		list->setFocus ();
	}
private:
	static const char *cell_text (DataGrid &, int row, int col, char *buf, int cap)
	{
		if (row < 0 || row >= g_doc.nf) return "";
		const Field &f = g_doc.f[row];
		if (col == 0) { itoa10 (row + 1, buf); return buf; }
		if (col == 1) return f.label;
		scpy (buf, TYPE_NAME[f.type], cap);
		if (f.type == FT_DEC) { scat (buf, " (", cap); scat_num (buf, f.prec, cap); scat (buf, ")", cap); }
		if (f.type == FT_CHOICE) { scat (buf, " (", cap); scat_num (buf, f.nch, cap); scat (buf, ")", cap); }
		return buf;
	}
	static void on_pick (Widget &w)
	{
		DataGrid &g = (DataGrid &) w;
		if (g.sel >= 0 && g.sel < g_doc.nf && g.sel != g_fsel) { g_fsel = g.sel; undo_break (); s_dv->loadField (); }
	}
	static void on_activate (Widget &) { s_dv->focusLabel (); }
	static void on_button (Widget &w)
	{
		switch (w.tag)
		{
		case 0: s_dv->add (); break;
		case 1: s_dv->remove (); break;
		case 2: s_dv->move (-1); break;
		case 3: s_dv->move (1); break;
		}
	}
	// (a change made by a control: the document follows, the controls stay as they are)
	static void applied (bool fields) { s_dv->editing = true; doc_changed (fields); s_dv->editing = false; }
	static void on_label (Widget &w)
	{
		if (!g_doc.nf) return;
		Field &f = g_doc.f[g_fsel];
		char was[COL_MAX]; make_column (f.label, was, sizeof was); unique_column (g_doc, g_fsel, was, sizeof was);
		bool follow = seq (was, f.column);			// (the column made from the name: it follows)
		undo_mark (1000 + g_fsel);
		scpy (f.label, ((LineEdit &) w).text (), sizeof f.label);
		if (follow)
		{
			make_column (f.label[0] ? f.label : "field", f.column, sizeof f.column);
			unique_column (g_doc, g_fsel, f.column, sizeof f.column);
			s_dv->eColumn->setText (f.column); s_dv->eColumn->setError (false);
		}
		applied (true);
	}
	static void on_column (Widget &w)
	{
		if (!g_doc.nf) return;
		LineEdit &e = (LineEdit &) w;
		Field &f = g_doc.f[g_fsel];
		if (!e.text ()[0]) { e.setError (true); status ("A column needs a name"); return; }
		int other = find_column (g_doc, e.text (), g_fsel);
		if (other >= 0)
		{
			e.setError (true);
			char m[120] = "The field "; scat (m, g_doc.f[other].label, sizeof m); scat (m, " has this column name", sizeof m);
			status (m);
			return;
		}
		e.setError (false);
		undo_mark (2000 + g_fsel);
		scpy (f.column, e.text (), sizeof f.column);
		applied (true);
	}
	static void on_type (Widget &w)
	{
		if (!g_doc.nf) return;
		ChoiceBox &c = (ChoiceBox &) w;
		Field &f = g_doc.f[g_fsel];
		int nt = c.sel;
		if (nt == f.type) return;
		int lost, rounded;
		convert_count (g_doc, g_fsel, nt, f.prec, &lost, &rounded);
		if (lost)
		{
			char m[240] = "";
			scat_num (m, lost, sizeof m);
			scat (m, lost == 1 ? " value of \"" : " values of \"", sizeof m); scat (m, f.label, sizeof m);
			scat (m, lost == 1 ? "\" is not a " : "\" are not a ", sizeof m);
			static const char *const WHAT[FT_COUNT] = { "text", "text", "whole number", "number", "date", "colour", "yes / no", "choice" };
			scat (m, WHAT[nt], sizeof m);
			scat (m, lost == 1 ? ": it will be emptied. Change the type?" : ": they will be emptied. Change the type?", sizeof m);
			if (ask ("Change the Type", m, MB_YESNO, 2) != 1) { c.sel = f.type; c.invalidate (true); return; }
		}
		undo_mark (-1);
		convert_field (g_doc, g_fsel, nt, f.prec);
		doc_changed (true);					// (the type's rows shown: the controls loaded again)
		if (rounded)
		{
			char m[80] = ""; scat_num (m, rounded, sizeof m); scat (m, rounded == 1 ? " value rounded" : " values rounded", sizeof m);
			status (m);
		}
		else status ("Type changed");
		c.setFocus ();
	}
	static void on_dec (Widget &w)
	{
		if (!g_doc.nf || g_doc.f[g_fsel].type != FT_DEC) return;
		int p = ((NumericUpDown &) w).value;
		Field &f = g_doc.f[g_fsel];
		if (p == f.prec) return;
		int lost, rounded;
		convert_count (g_doc, g_fsel, FT_DEC, p, &lost, &rounded);
		undo_mark (5000 + g_fsel);
		convert_field (g_doc, g_fsel, FT_DEC, p);
		s_dv->decHint (p);
		applied (true);
		if (rounded) { char m[80] = ""; scat_num (m, rounded, sizeof m); scat (m, rounded == 1 ? " value rounded" : " values rounded", sizeof m); status (m); }
	}
	static void on_choices (Widget &w)
	{
		if (!g_doc.nf || g_doc.f[g_fsel].type != FT_CHOICE) return;
		const char *s = ((MemoEdit &) w).text ();
		undo_mark (3000 + g_fsel);
		Field &f = g_doc.f[g_fsel];
		field_free (f);
		bool full = false;
		while (*s)
		{
			char one[VAL_MAX]; int n = 0;
			while (*s && *s != '\n') { if (n < VAL_MAX - 1) one[n++] = *s; s++; }
			one[n] = '\0';
			if (*s) s++;
			char t[VAL_MAX]; trim_copy (t, one, sizeof t);
			if (t[0] && !choice_add (f, t)) full = true;
		}
		applied (true);
		if (full) status ("A choice list holds 64 choices at most");
	}
	static void on_title (Widget &w)
	{
		undo_mark (4000);
		scpy (g_doc.title, ((LineEdit &) w).text (), sizeof g_doc.title);
		applied (false);
	}
	static void on_info (Widget &w)
	{
		undo_mark (4001);
		scpy (g_doc.info, ((LineEdit &) w).text (), sizeof g_doc.info);
		applied (false);
	}
};

} // namespace cf

#endif
