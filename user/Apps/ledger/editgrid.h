//
// editgrid.h -- the entry grids: an invoice's lines, a statement's movements, a miscellaneous operation's
// lines. A table whose cells are typed in place, as in a spreadsheet: the active cell holds an editor (a
// line: typing replaces what it held, its text all selected); Tab or Enter take what was typed and go to
// the next cell (Shift+Tab: the one before), past the last one a new line; Up / Down the line above /
// below; Esc gives back what the cell held (again: the grid is left); Ctrl+Delete removes the line (so
// does the cross at its end). An account's, a party's, a VAT code's cell drops its suggestions as it is
// typed (pick.h: Enter or Tab take the one lit; Alt+Down or F4 show them all). A click on a cell edits
// it, on "+ Add a line" adds one. The app's side is an EGModel: its lines, a cell's text (as shown, and
// as edited), a cell's text taken ("" or why not: the cell then stays, outlined in red, the reason in
// the status line).
//
#ifndef _ledger_editgrid_h
#define _ledger_editgrid_h

#include "pick.h"

namespace lg {

enum { EK_TEXT, EK_MONEY, EK_ACCOUNT, EK_PARTY, EK_TARGET, EK_VAT, EK_READ };

struct EGModel
{
	virtual int  rows () = 0;
	virtual void cell (int r, int c, bool edit, char *out, int cap) = 0;	// shown (edit: as it is edited)
	virtual const char *put (int r, int c, const char *text) = 0;		// "" taken, else why not
	virtual bool editable (int r, int c) { (void) r; (void) c; return true; }
	virtual void add () = 0;						// a line at the end
	virtual void remove (int r) = 0;
	virtual bool blank (int r) = 0;						// nothing typed in it
	virtual void linesChanged () {}						// after a change: the totals follow
	virtual void rowFocused (int r) { (void) r; }				// the active line changed
	virtual unsigned ink (int r, int c) { (void) r; (void) c; return 0; }	// a cell's colour (0: the field's)
	virtual ~EGModel () {}
};

struct EGCol { char title[32]; int width, kind, sugArg; bool right; const char *prefer; };

class EditGrid;

class CellEditor : public LineEdit, public SugOwner
{
public:
	EditGrid *g; Suggester sug; char before[256];
	CellEditor (EditGrid *grid) : LineEdit (0, 0, 50, ROW_H - 2), g (grid) { before[0] = '\0'; hidden = true; }
	Widget *sugField () override { return this; }
	void sugPicked (int i) override;
	void edited () override;
	bool onKey (long k) override;
	void onDraw () override
	{
		LineEdit::onDraw ();
		if (sug.kind != SK_NONE)				// (the drop button's hint)
			uk_glyph (canvas, WKG_CHEV_DOWN, width - 10, height / 2, 7, field_dim ());
	}
};

class EditGrid : public Widget
{
public:
	enum { MAXC = 10, XW = 26 };
	EGModel *m;
	EGCol col[MAXC]; int ncol, flex;			// flex: the column taking the width left
	int cur, curCol, first, headH, rowH;			// (first: the first line shown)
	CellEditor *ed;
	const char *addText;
	EditGrid (int l, int t, int w, int h, EGModel *model)
		: Widget (l, t, w, h), m (model), ncol (0), flex (-1), cur (-1), curCol (0), first (0), headH (28), rowH (ROW_H + 2), addText (TR ("Add a line")),
		  m_hotRow (-1), m_hotX (false), m_hotAdd (false), m_lost (false), m_thumb (false)
	{
		canFocus = true;
		ed = new CellEditor (this);
		addChild (ed);
	}
	void addCol (const char *title, int width, int kind, bool right = false, int sugArg = 0, const char *prefer = 0)
	{
		if (ncol >= MAXC) return;
		EGCol &c = col[ncol++];
		scpy (c.title, title, sizeof c.title); c.width = width; c.kind = kind; c.right = right; c.sugArg = sugArg; c.prefer = prefer;
	}
	bool editing () const { return !ed->hidden; }
	// ---- geometry ----
	int bodyH () const { return height - headH - 2; }
	int shownRows () const { return imax (1, bodyH () / rowH); }
	bool vbar () const { return (m->rows () + 1) * rowH > bodyH (); }
	int colW (int c) const
	{
		if (c != flex) return col[c].width;
		int s = 0; for (int i = 0; i < ncol; i++) if (i != flex) s += col[i].width;
		return imax (60, width - 2 - XW - (vbar () ? UK_SBW + 2 : 0) - s);
	}
	int colX (int c) const { int x = 1; for (int i = 0; i < c; i++) x += colW (i); return x; }
	int rowY (int r) const { return headH + (r - first) * rowH; }
	void cellBox (int r, int c, int *x, int *y, int *w) const { *x = colX (c); *y = rowY (r); *w = colW (c); }
	void clampTop ()
	{
		int n = m->rows () + 1, s = shownRows ();
		first = iclamp (first, 0, imax (0, n - s));
	}
	void ensureVisible (int r)
	{
		int s = shownRows ();
		if (r < first) first = r;
		if (r >= first + s) first = r - s + 1;
		clampTop ();
	}
	void placeEditor ()
	{
		if (!editing () || cur < 0) return;
		if (cur < first || cur >= first + shownRows ()) { ed->left = -2000; return; }
		int x, y, w; cellBox (cur, curCol, &x, &y, &w);
		ed->left = x + 1; ed->top = y + 1;
		if (ed->width != w - 2 || ed->height != rowH - 2) ed->resizeTo (w - 2, rowH - 2);
		ed->invalidate (true);
	}
	void resizeTo (int w, int h) override { Widget::resizeTo (w, h); clampTop (); placeEditor (); }
	// ---- editing ----
	bool cellEditable (int r, int c) { return c >= 0 && c < ncol && col[c].kind != EK_READ && m->editable (r, c); }
	int firstEditable (int r) { for (int c = 0; c < ncol; c++) if (cellEditable (r, c)) return c; return -1; }
	// The cell (r, c) edited (the one being edited taken first); false when that one refused its text.
	bool activate (int r, int c)
	{
		if (editing () && (r != cur || c != curCol) && !commit ()) return false;
		if (r < 0 || r >= m->rows ()) return false;
		if (!cellEditable (r, c)) { int f = -1; for (int k = c; k < ncol && f < 0; k++) if (cellEditable (r, k)) f = k; if (f < 0) f = firstEditable (r); if (f < 0) return false; c = f; }
		bool rowChanged = r != cur;
		cur = r; curCol = c;
		ensureVisible (r);
		const EGCol &k = col[c];
		char t[256]; m->cell (r, c, true, t, sizeof t);
		ed->accept = k.kind == EK_MONEY ? accept_dec : 0;
		ed->rightAlign = k.right;
		ed->padR = (k.kind == EK_ACCOUNT || k.kind == EK_PARTY || k.kind == EK_TARGET || k.kind == EK_VAT) ? 16 : 0;
		ed->sug.hide ();
		ed->sug.kind = k.kind == EK_ACCOUNT ? SK_ACCOUNT : k.kind == EK_PARTY ? SK_PARTY : k.kind == EK_TARGET ? SK_TARGET : k.kind == EK_VAT ? SK_VAT : SK_NONE;
		ed->sug.arg = k.sugArg; ed->sug.prefer = k.prefer;
		ed->setError (false);
		ed->setText (t); scpy (ed->before, t, sizeof ed->before);
		ed->selectAll ();
		ed->hidden = false;
		placeEditor ();
		ed->setFocus ();
		m_lost = false;
		if (rowChanged) m->rowFocused (r);
		invalidate (true);
		return true;
	}
	// What the editor holds, taken (true), or refused (false: the editor stays, outlined in red).
	bool commit ()
	{
		if (!editing () || cur < 0 || cur >= m->rows ()) return true;
		ed->sug.hide ();
		if (seq (ed->text (), ed->before)) return true;
		const char *w = m->put (cur, curCol, ed->text ());
		if (w && w[0]) { ed->setError (true); status (w); return false; }
		char t[256]; m->cell (cur, curCol, true, t, sizeof t);
		scpy (ed->before, t, sizeof ed->before);
		m->linesChanged ();
		invalidate (true);
		return true;
	}
	void stopEditing ()
	{
		if (!editing ()) return;
		if (!commit ()) return;
		ed->hidden = true; ed->sug.hide ();
		invalidate (true);
	}
	void cancelEditing () { ed->sug.hide (); ed->hidden = true; invalidate (true); }
	// The next (dir 1) / previous (-1) editable cell; past the last line: a new one (unless it is blank).
	void move (int dir)
	{
		if (!commit ()) return;
		int r = cur, c = curCol, n = m->rows ();
		for (int guard = 0; guard < 400; guard++)
		{
			c += dir;
			if (c >= ncol) { c = 0; r++; }
			if (c < 0) { c = ncol - 1; r--; }
			if (r < 0) { r = 0; c = firstEditable (0); break; }
			if (r >= n)
			{
				if (n && m->blank (n - 1)) { r = n - 1; c = firstEditable (r); }
				else { m->add (); m->linesChanged (); r = m->rows () - 1; c = firstEditable (r); }
				break;
			}
			if (cellEditable (r, c)) break;
		}
		if (c >= 0) activate (r, c);
	}
	void moveRow (int d)
	{
		if (!commit ()) return;
		int r = cur + d, n = m->rows ();
		if (r < 0) return;
		if (r >= n)
		{
			if (n && m->blank (n - 1)) return;
			m->add (); m->linesChanged (); r = m->rows () - 1;
		}
		activate (r, curCol);
	}
	void addLine ()
	{
		if (editing () && !commit ()) return;
		int n = m->rows ();
		if (!n || !m->blank (n - 1)) { m->add (); m->linesChanged (); }
		int r = m->rows () - 1;
		activate (r, firstEditable (r));
	}
	void removeRow (int r)
	{
		if (r < 0 || r >= m->rows ()) return;
		bool wasEditing = editing ();
		if (wasEditing) cancelEditing ();
		m->remove (r);
		if (!m->rows ()) m->add ();
		m->linesChanged ();
		cur = imin (cur, m->rows () - 1);
		clampTop ();
		if (wasEditing) activate (cur, curCol);
		invalidate (true);
	}
	// The keyboard comes to the grid: its first cell (or the one it was at).
	void focusIn ()
	{
		if (!m->rows ()) { m->add (); m->linesChanged (); }
		int r = cur >= 0 && cur < m->rows () ? cur : 0;
		activate (r, cur >= 0 ? curCol : firstEditable (r));
	}
	// The window's loop: the editor lost the keyboard (a click elsewhere): its text taken.
	void tick ()
	{
		if (!editing ()) return;
		if (ed->hasFocus) { m_lost = false; return; }
		if (m_lost) return;
		m_lost = true;
		if (commit ()) { ed->hidden = true; ed->sug.hide (); invalidate (true); }
	}
	void reset () { cancelEditing (); cur = -1; curCol = 0; first = 0; invalidate (true); }
	// ---- drawing ----
	void onDraw () override
	{
		unsigned bg = bgColor ();
		canvas.clear (bg);
		uk_sunken (canvas, 0, 0, width, height, 6, C_FIELD, false);
		unsigned hf = uk_tone (C_BG, 150);
		uk_rbox (canvas, 1, 1, width - 2, headH - 1, 5, uk_tone (C_BG, 162), uk_tone (C_BG, 140), 255, UK_TL | UK_TR);
		canvas.fillRect (1, headH - 1, width - 2, 1, uk_tone (C_BG, 110));
		unsigned hink = uk_ink_for (hf);
		for (int c = 0; c < ncol; c++)
		{
			int x = colX (c), w = colW (c);
			Canvas cc; cc.adopt (canvas.px + x, imax (1, w), headH, canvas.stride);
			cell_text (cc, 0, 0, w, headH, col[c].title, hink, col[c].right, 2);
			if (c) canvas.fillRect (x, 6, 1, headH - 12, uk_tone (C_BG, 120));
		}
		int n = m->rows (), s = shownRows ();
		bool vb = vbar ();
		int rw = width - 2 - (vb ? UK_SBW + 2 : 0);
		Canvas body; body.adopt (canvas.px + headH * canvas.stride, width, imax (1, height - headH - 1), canvas.stride);
		char t[256];
		for (int i = 0; i <= s && first + i <= n; i++)
		{
			int r = first + i, y = i * rowH;
			if (r == n)					// "+ Add a line"
			{
				unsigned c = m_hotAdd ? uk_tone (C_ACCENT, 110) : C_ACCENT;
				uk_glyph (body, WKG_PLUS, 16, y + rowH / 2, 9, c);
				uk_text_l (body, 28, y, rowH, addText, c);
				break;
			}
			bool curRow = r == cur;
			unsigned rb = curRow ? uk_mix (C_FIELD, C_ACCENT, 26) : (r & 1) ? uk_tone (C_FIELD, 122) : C_FIELD;
			body.fillRect (1, y, rw, rowH, rb);
			body.fillRect (1, y + rowH - 1, rw, 1, uk_tone (C_FIELD, 112));
			for (int c = 0; c < ncol; c++)
			{
				int x = colX (c), w = colW (c);
				if (c) body.fillRect (x, y, 1, rowH, uk_tone (C_FIELD, 116));
				if (editing () && r == cur && c == curCol) continue;
				m->cell (r, c, false, t, sizeof t);
				unsigned ink = m->ink (r, c);
				if (!ink) ink = col[c].kind == EK_READ ? field_dim () : C_FIELD_TEXT;
				Canvas cc; cc.adopt (body.px + y * body.stride + x, imax (1, w), rowH, body.stride);
				if (col[c].kind == EK_ACCOUNT || col[c].kind == EK_TARGET)		// "700000  Ventes...": the code in bold
				{
					const char *sp = t; while (*sp && *sp != ' ') sp++;
					if (*sp && digit (t[0]))
					{
						char code[16]; int k = 0; for (const char *q = t; q < sp && k < 15; q++) code[k++] = *q; code[k] = '\0';
						int cw = uk_text_w (code, 2);
						uk_text_l (cc, 6, 0, rowH, code, ink, 2);
						while (*sp == ' ') sp++;
						cell_text (cc, cw + 4, 0, w - cw - 4, rowH, sp, field_dim (), false);
						continue;
					}
				}
				cell_text (cc, 0, 0, w, rowH, t, ink, col[c].right);
			}
			if (r == m_hotRow || curRow)			// the line's cross
			{
				int x = width - XW - (vb ? UK_SBW + 2 : 0);
				uk_glyph (body, WKG_CLOSE, x + XW / 2, y + rowH / 2, 8, r == m_hotRow && m_hotX ? C_BAD : field_dim ());
			}
		}
		if (vb) { UkThumb th = uk_thumb (n + 1, s, first, height - headH - 4); uk_draw_vscroll (canvas, width - UK_SBW - 3, headH + 2, UK_SBW, height - headH - 4, th, C_FIELD, m_thumb); }
		if (!editing () && cur >= 0 && cur < n && hasFocus && cur >= first && cur < first + s)
		{
			int x, y, w; cellBox (cur, curCol, &x, &y, &w);
			uk_rline (canvas, x, y, w, rowH, 3, C_ACCENT, 255);
		}
	}
	// ---- the mouse ----
	bool onMouse (int mx, int my, int bl, int, int, int wheel) override
	{
		bool in = mx >= 0 && my >= 0 && mx < width && my < height;
		if (!in && !m_thumb)
		{
			if (m_hotRow >= 0 || m_hotAdd) { m_hotRow = -1; m_hotAdd = false; invalidate (true); }
			if (!bl) pressed = false;
			return false;
		}
		int n = m->rows ();
		bool vb = vbar ();
		if (wheel) { first -= wheel; clampTop (); placeEditor (); invalidate (true); return true; }
		if (m_thumb || (vb && mx >= width - UK_SBW - 4 && my > headH))
		{
			if (bl)
			{
				m_thumb = true;
				UkThumb th = uk_thumb (n + 1, shownRows (), first, height - headH - 4);
				first = (int) uk_thumb_pos (my - headH - 2, height - headH - 4, n + 1, shownRows (), th.h); clampTop (); placeEditor ();
				invalidate (true);
			}
			else m_thumb = false;
			return true;
		}
		int r = my >= headH ? first + (my - headH) / rowH : -1;
		bool onX = r >= 0 && r < n && mx >= width - XW - (vb ? UK_SBW + 2 : 0) - 2;
		bool onAdd = r == n;
		int hr = r >= 0 && r < n ? r : -1;
		if (hr != m_hotRow || onX != m_hotX || onAdd != m_hotAdd) { m_hotRow = hr; m_hotX = onX; m_hotAdd = onAdd; invalidate (true); }
		if (bl && !pressed)
		{
			pressed = true;
			if (onAdd) { addLine (); return true; }
			if (onX) { removeRow (r); return true; }
			if (r >= 0 && r < n)
			{
				int c = ncol - 1;
				for (int k = 0; k < ncol; k++) if (mx < colX (k) + colW (k)) { c = k; break; }
				activate (r, c);
			}
			else if (!hasFocus) setFocus ();
		}
		else if (!bl) pressed = false;
		return true;
	}
	bool onKey (long k) override
	{
		if (k == KEY_ENTER || k == KEY_TAB || k == KEY_DOWN || k == ' ') { if (!editing ()) { focusIn (); return true; } }
		if (k == KEY_DEL && !editing () && cur >= 0) { removeRow (cur); return true; }
		return false;
	}
private:
	int m_hotRow; bool m_hotX, m_hotAdd, m_lost, m_thumb;
};

inline void CellEditor::sugPicked (int i)
{
	if (i < 0 || i >= sug.n) return;
	setText (sug.items[i].value);
	sug.hide ();
	g->move (1);
}
inline void CellEditor::edited ()
{
	LineEdit::edited ();
	if (sug.kind != SK_NONE) sug.update (this, this, text (), 0, 0, width, height);
}
inline bool CellEditor::onKey (long k)
{
	int mods = kapi_get_modifiers ();
	bool shift = (mods & MOD_SHIFT) != 0, ctrl = (mods & MOD_CTRL) != 0, alt = (mods & MOD_ALT) != 0;
	if ((k == KEY_DOWN && alt) || k == KEY_F1 + 3)
	{
		if (sug.kind != SK_NONE) { if (sug.visible ()) sug.hide (); else sug.update (this, this, "", 0, 0, width, height, true); }
		return true;
	}
	if (sug.key (k)) return true;
	if (k == KEY_TAB || k == KEY_ENTER)
	{
		if (sug.visible ()) { const Sug *s = sug.lit (); if (s) setText (s->value); sug.hide (); }
		g->move (shift ? -1 : 1);
		return true;
	}
	if (k == KEY_UP || k == KEY_DOWN) { g->moveRow (k == KEY_UP ? -1 : 1); return true; }
	if (k == 27)
	{
		if (sug.visible ()) { sug.hide (); return true; }
		if (!seq (text (), before)) { setText (before); selectAll (); setError (false); return true; }
		g->cancelEditing ();
		return false;						// (the page's: Esc)
	}
	if (k == KEY_DEL && ctrl) { g->removeRow (g->cur); return true; }
	return LineEdit::onKey (k);
}

} // namespace lg

#endif
