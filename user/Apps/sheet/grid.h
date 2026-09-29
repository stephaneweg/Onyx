//
// grid.h -- the grid: the column letters and row numbers (the selection's in the accent; their edges
// dragged to size a column / row, a double click fits it), the cells in up to four panes (the frozen rows
// and columns stay), the selection (a range, whole columns / rows, all), the cursor's cell, the fill
// handle (dragged: a series), the copied range's dashes, the charts over the cells (moved, sized).
// Typing edits a cell in place: a formula's references coloured -- and the same colours framing their
// cells --, cells pointed by the arrows or the mouse while a formula waits for a reference, the function
// being typed offered in a list, its arguments shown as a tip. Enter / Tab put the entry in.
//
#ifndef _sheet_grid_h
#define _sheet_grid_h

#include "chart.h"

namespace ss {

enum { MODE_READY, MODE_ENTER, MODE_EDIT, MODE_POINT };
static const unsigned REFCOL[7] = { 0x1A73E8, 0xD93025, 0x188038, 0x9334E6, 0xE37400, 0x12A4AF, 0xC5221F };

// ---- the text being typed ---------------------------------------------------------------------------------
struct EditLine
{
	Buf t;
	int caret = 0, anchor = 0;
	bool on = false, typed = false;			// editing; typed: started by typing (the arrows leave the cell)
	int r = 0, c = 0, sheetId = 0;
	bool pointing = false;				// a reference being pointed: its text [ps, pe), its cells
	int ps = 0, pe = 0, pr0 = 0, pc0 = 0, pr1 = 0, pc1 = 0;
	int s0 () const { return imin (caret, anchor); }
	int s1 () const { return imax (caret, anchor); }
	void start (const char *s, bool typed_, int r_, int c_, int sid)
	{
		t.clear (); t.puts (s); caret = anchor = t.n; on = true; typed = typed_; r = r_; c = c_; sheetId = sid; pointing = false;
	}
	void stop () { on = false; pointing = false; t.clear (); caret = anchor = 0; }
	void replace (int a, int b, const char *s, int n)
	{
		Buf o; o.putn (t.b ? t.b : "", a); o.putn (s, n); o.putn (t.b ? t.b + b : "", t.n - b);
		t.clear (); t.putn (o.b ? o.b : "", o.n);
		caret = anchor = a + n;
	}
	void insert (const char *s, int n) { replace (s0 (), s1 (), s, n); pointing = false; }
	void insert_char (unsigned cp) { char b[4]; int n = u8_enc (cp, b); insert (b, n); }
	int prev (int i) const { if (i <= 0) return 0; i--; while (i > 0 && ((unsigned char) t.b[i] & 0xC0) == 0x80) i--; return i; }
	int next (int i) const { if (i >= t.n) return t.n; int l; u8_dec (t.b + i, t.n - i, &l); return i + l; }
	int word_left (int i) const { while (i > 0 && t.b[i - 1] == ' ') i = prev (i); while (i > 0 && t.b[i - 1] != ' ' && !strchr ("(),;=+-*/&^<>", t.b[i - 1])) i = prev (i); return i; }
	int word_right (int i) const { while (i < t.n && t.b[i] != ' ' && !strchr ("(),;=+-*/&^<>", t.b[i])) i = next (i); while (i < t.n && t.b[i] == ' ') i = next (i); return i; }
	void backspace () { if (s0 () != s1 ()) insert ("", 0); else if (caret > 0) { int p = prev (caret); replace (p, caret, "", 0); } pointing = false; }
	void del () { if (s0 () != s1 ()) insert ("", 0); else if (caret < t.n) { int q = next (caret); replace (caret, q, "", 0); } pointing = false; }
	bool formula () const { return t.n > 0 && (t.b[0] == '=' || ((t.b[0] == '+' || t.b[0] == '-') && t.n > 1)); }
	// Does the text before the caret wait for a reference (after an operator, "(", ",")?
	bool can_point () const
	{
		if (!formula ()) return false;
		if (pointing && caret == pe) return true;
		int i = caret;
		while (i > 0 && t.b[i - 1] == ' ') i--;
		if (i == 0) return false;
		char ch = t.b[i - 1];
		if (i == 1 && (ch == '=' || ch == '+' || ch == '-')) return true;
		return strchr ("=(,;+-*/^&<>:%", ch) != 0;
	}
};

// The references of a formula being typed: where they are, what they point at, their colours.
struct RefHi { int at, len; int sheetId; Rect r; unsigned col; };
static int edit_refs (Book &b, const EditLine &e, RefHi *out, int max)
{
	if (!e.formula ()) return 0;
	int off = e.t.b[0] == '=' ? 1 : 0;
	Lexer L; L.b = &b; L.s = e.t.b + off; L.n = e.t.n - off; L.i = 0; L.tok = 0; L.nt = L.ct = 0; L.err = 0; L.errPos = 0; L.brace = 0;
	L.run ();						// (what reads up to an error is enough)
	int n = 0;
	for (int i = 0; i < L.nt && n < max; i++)
	{
		const Tok &k = L.tok[i];
		if ((k.t != TK_REF && k.t != TK_AREA) || (k.fl & TF_BAD)) continue;
		RefHi &h = out[n];
		h.at = k.at + off; h.len = k.len; h.sheetId = k.sheet ? k.sheet : e.sheetId;
		h.r.r0 = k.r0; h.r.c0 = k.c0; h.r.r1 = k.r1; h.r.c1 = k.c1;
		// the same reference: the same colour
		int ci = n;
		for (int j = 0; j < n; j++) if (out[j].sheetId == h.sheetId && !memcmp (&out[j].r, &h.r, sizeof (Rect))) { ci = -1 - j; break; }
		h.col = ci >= 0 ? REFCOL[ci % 7] : out[-1 - ci].col;
		n++;
	}
	free (L.tok);
	return n;
}
// The function the caret is in, and which of its arguments (-1: none).
static int edit_function (const EditLine &e, int *argIdx)
{
	if (!e.formula ()) return -1;
	int stack[64], args[64], depth = 0;
	bool str = false;
	for (int i = 0; i < e.caret && i < e.t.n; i++)
	{
		char ch = e.t.b[i];
		if (ch == '"') { str = !str; continue; }
		if (str) continue;
		if (ch == '(')
		{
			int j = i; while (j > 0 && (isalnum ((unsigned char) e.t.b[j - 1]) || e.t.b[j - 1] == '.' || e.t.b[j - 1] == '_')) j--;
			int f = j < i ? fn_lookup (e.t.b + j, i - j) : -1;
			if (depth < 64) { stack[depth] = f; args[depth] = 0; depth++; }
		}
		else if (ch == ')') { if (depth > 0) depth--; }
		else if ((ch == ',' || ch == ';') && depth > 0) args[depth - 1]++;
	}
	while (depth > 0 && stack[depth - 1] < 0) depth--;
	if (depth <= 0) return -1;
	*argIdx = args[depth - 1];
	return stack[depth - 1];
}
// The function name being typed at the caret ("=SU|"): its start; -1 none.
static int edit_word (const EditLine &e, char *word, int cap)
{
	if (!e.formula () || e.caret != e.t.n) return -1;
	int j = e.caret;
	while (j > 0 && (isalpha ((unsigned char) e.t.b[j - 1]) || e.t.b[j - 1] == '.')) j--;
	if (j == e.caret || (j > 0 && (isalnum ((unsigned char) e.t.b[j - 1]) || e.t.b[j - 1] == '$' || e.t.b[j - 1] == '"'))) return -1;
	int n = imin (e.caret - j, cap - 1);
	memcpy (word, e.t.b + j, n); word[n] = 0;
	return j;
}

// ---- the grid ---------------------------------------------------------------------------------------------
enum { D_NONE, D_SELECT, D_COLSEL, D_ROWSEL, D_COLSIZE, D_ROWSIZE, D_FILL, D_POINT, D_VBAR, D_HBAR, D_CHART, D_CHARTSIZE };
static const int HEAD_H = 22, SB = WK_SBW + 4;

class GridView : public Widget
{
public:
	Book *b;
	int z;							// the zoom (%)
	EditLine ed;
	int mode () const { return !ed.on ? MODE_READY : ed.pointing ? MODE_POINT : ed.typed ? MODE_ENTER : MODE_EDIT; }
	bool copyMarquee; Rect copyRect; int copySheet;
	int selChart;						// the chart chosen (-1 none)
	void (*onChange) ();					// the selection / the typing changed
	void (*onEdited) ();					// a cell was put in (the book changed)
	void (*onContext) (int x, int y, int where);		// right click: 0 cells, 1 column headers, 2 row headers, 3 a chart
	void (*onChartOpen) (int i);
	void (*onZoom) (int dir);
	bool (*onCommit) (int r, int c, const char *text);	// the app puts the entry in (undo, format...)
	bool (*onOtherKey) (long k, int mods);			// a key the grid does not take (Ctrl+1, F9...)
	char acWord[40]; int acStart, acSel, acN; int acList[12];	// the functions offered while typing

	GridView (int l, int t, int w, int h) : Widget (l, t, w, h), b (0), z (100), copyMarquee (false), copySheet (0), selChart (-1),
		onChange (0), onEdited (0), onContext (0), onChartOpen (0), onZoom (0), onCommit (0), onOtherKey (0), acStart (-1), acSel (0), acN (0),
		m_drag (D_NONE), m_dragI (0), m_dragV (0), m_lastClick (0), m_lastR (-1), m_lastC (-1), m_blink (true), m_blinkT (0)
	{ canFocus = true; fillTo = Rect { 0, 0, -1, -1 }; }

	Sheet *S () { return b->sh[b->active]; }
	// ---- geometry
	int rowHeadW ()
	{
		Sheet *s = S ();
		char t[12]; snprintf (t, sizeof t, "%d", s->topR + 60);
		int digits = (int) strlen (t);
		return imax (40, digits * 8 + 14);
	}
	int frozenW () { Sheet *s = S (); int w = 0; for (int c = 0; c < s->freezeC; c++) w += col_w (s, c) * z / 100; return w; }
	int frozenH () { Sheet *s = S (); int h = 0; for (int r = 0; r < s->freezeR; r++) h += row_h (s, r) * z / 100; return h; }
	void panes (PaneView pv[4])				// 0: top-left (frozen both), 1: top, 2: left, 3: the main one
	{
		Sheet *s = S ();
		int RW = rowHeadW (), fw = frozenW (), fh = frozenH ();
		int x1 = width - SB, y1 = height - SB;
		int fw2 = imin (fw, x1 - RW), fh2 = imin (fh, y1 - HEAD_H);
		pane_layout (s, z, 0, s->freezeC, 0, s->freezeR, RW, HEAD_H, RW + fw2, HEAD_H + fh2, pv[0]);
		pane_layout (s, z, imax (s->leftC, s->freezeC), MAXC, 0, s->freezeR, RW + fw2, HEAD_H, x1, HEAD_H + fh2, pv[1]);
		pane_layout (s, z, 0, s->freezeC, imax (s->topR, s->freezeR), MAXR, RW, HEAD_H + fh2, RW + fw2, y1, pv[2]);
		pane_layout (s, z, imax (s->leftC, s->freezeC), MAXC, imax (s->topR, s->freezeR), MAXR, RW + fw2, HEAD_H + fh2, x1, y1, pv[3]);
	}
	Rect sel ()
	{
		Sheet *s = S ();
		Rect r = { imin (s->ancR, s->curR), imin (s->ancC, s->curC), imax (s->ancR, s->curR), imax (s->ancC, s->curC) };
		if (m_wholeCols) { r.r0 = 0; r.r1 = MAXR - 1; }
		if (m_wholeRows) { r.c0 = 0; r.c1 = MAXC - 1; }
		// a merge met extends the range (whole rows / columns stay as they are, as in Calc and Excel)
		for (int pass = 0; pass < 4 && !m_wholeCols && !m_wholeRows; pass++)
		{
			bool grew = false;
			for (int i = 0; i < s->nmerge; i++)
			{
				const Rect &m = s->merges[i];
				if (!rect_meets (m, r)) continue;
				if (m.r0 < r.r0) { r.r0 = m.r0; grew = true; }
				if (m.c0 < r.c0) { r.c0 = m.c0; grew = true; }
				if (m.r1 > r.r1) { r.r1 = m.r1; grew = true; }
				if (m.c1 > r.c1) { r.c1 = m.c1; grew = true; }
			}
			if (!grew) break;
		}
		return r;
	}
	bool wholeCols () const { return m_wholeCols; }
	bool wholeRows () const { return m_wholeRows; }
	// The cell under (mx, my); false when outside the cells.
	bool cellAt (int mx, int my, int *r, int *c)
	{
		PaneView pv[4]; panes (pv);
		for (int p = 0; p < 4; p++)
		{
			const PaneView &v = pv[p];
			if (mx < v.x0 || mx >= v.x1 || my < v.y0 || my >= v.y1) continue;
			int cc = -1, rr = -1;
			for (int i = 0; i < v.nc; i++) if (mx >= v.col[i].at && mx < v.col[i].at + v.col[i].len) cc = v.col[i].i;
			for (int i = 0; i < v.nr; i++) if (my >= v.row[i].at && my < v.row[i].at + v.row[i].len) rr = v.row[i].i;
			if (cc < 0) cc = v.nc ? v.col[v.nc - 1].i : 0;
			if (rr < 0) rr = v.nr ? v.row[v.nr - 1].i : 0;
			*r = rr; *c = cc;
			return true;
		}
		return false;
	}
	// A cell's box on the grid (its merge's box), from the pane it is in; false: not shown.
	bool cellBox (int r, int c, int *x, int *y, int *w, int *h)
	{
		Sheet *s = S ();
		int mi = merge_at (s, r, c);
		Rect m = mi >= 0 ? s->merges[mi] : Rect { r, c, r, c };
		PaneView pv[4]; panes (pv);
		int p = (m.r0 < s->freezeR ? 0 : 2) + (m.c0 < s->freezeC ? 0 : 1);
		const PaneView &v = pv[p];
		*x = pane_col_x (s, z, v, m.c0); *w = pane_col_x (s, z, v, m.c1 + 1) - *x;
		*y = pane_row_y (s, z, v, m.r0); *h = pane_row_y (s, z, v, m.r1 + 1) - *y;
		return *x < v.x1 && *x + *w > v.x0 && *y < v.y1 && *y + *h > v.y0;
	}
	// ---- scrolling
	int visRows () { PaneView pv[4]; panes (pv); return imax (1, pv[3].nr - 1); }
	int visCols () { PaneView pv[4]; panes (pv); return imax (1, pv[3].nc - 1); }
	void ensureVisible (int r, int c)
	{
		Sheet *s = S ();
		if (r >= s->freezeR)
		{
			if (r < s->topR) s->topR = r;
			else
			{
				PaneView pv[4]; panes (pv);
				// (the rows below the top that fit whole)
				int y = pv[3].y0, last = s->topR - 1;
				for (int rr = imax (s->topR, s->freezeR); rr < MAXR; rr++) { int h = row_h (s, rr) * z / 100; if (y + h > pv[3].y1) break; y += h; last = rr; }
				while (r > last && s->topR < MAXR - 1)
				{
					s->topR++;
					y = pv[3].y0; last = s->topR - 1;
					for (int rr = s->topR; rr < MAXR; rr++) { int h = row_h (s, rr) * z / 100; if (y + h > pv[3].y1) break; y += h; last = rr; }
				}
			}
		}
		if (c >= s->freezeC)
		{
			if (c < s->leftC) s->leftC = c;
			else
			{
				PaneView pv[4]; panes (pv);
				for (int guard = 0; guard < MAXC; guard++)
				{
					int x = pv[3].x0, last = s->leftC - 1;
					for (int cc = imax (s->leftC, s->freezeC); cc < MAXC; cc++) { int w = col_w (s, cc) * z / 100; if (x + w > pv[3].x1) break; x += w; last = cc; }
					if (c <= last || s->leftC >= MAXC - 1) break;
					s->leftC++;
				}
			}
		}
		s->topR = iclamp (s->topR, s->freezeR, MAXR - 1); s->leftC = iclamp (s->leftC, s->freezeC, MAXC - 1);
		invalidate (true);
	}
	void scrollBy (int dr, int dc)
	{
		Sheet *s = S ();
		s->topR = iclamp (s->topR + dr, s->freezeR, MAXR - 1);
		s->leftC = iclamp (s->leftC + dc, s->freezeC, MAXC - 1);
		invalidate (true);
	}
	// ---- the selection
	void selectCell (int r, int c, bool extend)
	{
		Sheet *s = S ();
		r = iclamp (r, 0, MAXR - 1); c = iclamp (c, 0, MAXC - 1);
		m_tabC = -1;
		if (!extend) { m_wholeCols = m_wholeRows = false; s->ancR = r; s->ancC = c; }
		s->curR = r; s->curC = c;
		if (!extend) { int mi = merge_at (s, r, c); if (mi >= 0) { s->curR = s->ancR = s->merges[mi].r0; s->curC = s->ancC = s->merges[mi].c0; } }
		selChart = -1;
		ensureVisible (r, c);
		changed ();
	}
	void selectRange (Rect r)
	{
		Sheet *s = S ();
		m_wholeCols = r.r0 == 0 && r.r1 >= MAXR - 1; m_wholeRows = r.c0 == 0 && r.c1 >= MAXC - 1;
		s->ancR = r.r0; s->ancC = r.c0; s->curR = r.r1; s->curC = r.c1;
		if (m_wholeCols) { s->ancR = s->curR = imax (s->topR, 0); }
		if (m_wholeRows) { s->ancC = s->curC = s->leftC; }
		if (m_wholeCols) { s->ancR = 0; s->curR = 0; }
		if (m_wholeRows) { s->ancC = 0; s->curC = 0; }
		if (!m_wholeCols && !m_wholeRows) { s->curR = r.r0; s->curC = r.c0; s->ancR = r.r1; s->ancC = r.c1; }
		selChart = -1;
		changed ();
	}
	void selectCols (int c0, int c1) { Sheet *s = S (); m_wholeCols = true; m_wholeRows = false; s->ancC = c0; s->curC = c1; s->ancR = s->curR = imax (0, s->topR); selChart = -1; changed (); }
	void selectRows (int r0, int r1) { Sheet *s = S (); m_wholeRows = true; m_wholeCols = false; s->ancR = r0; s->curR = r1; s->ancC = s->curC = imax (0, s->leftC); selChart = -1; changed (); }
	void selectAll () { Sheet *s = S (); m_wholeCols = m_wholeRows = true; s->ancR = s->ancC = 0; s->curR = s->curC = 0; selChart = -1; changed (); }
	void resetSelKind () { m_wholeCols = m_wholeRows = false; }
	void changed () { invalidate (true); if (onChange) onChange (); }

	// ---- editing
	void beginEdit (bool typed, const char *initial)
	{
		Sheet *s = S ();
		if (ed.on) return;
		if (initial) ed.start (initial, typed, s->curR, s->curC, s->id);
		else
		{
			Buf t; cell_edit_text (*b, s, s->cells.get (s->curR, s->curC), t);
			ed.start (t.str (), typed, s->curR, s->curC, s->id);
		}
		copyMarquee = copyMarquee && typed ? copyMarquee : copyMarquee;
		acStart = -1;
		m_blink = true; m_blinkT = kapi_get_ticks ();
		ensureVisible (s->curR, s->curC);
		changed ();
	}
	void cancelEdit () { if (!ed.on) return; ed.stop (); acStart = -1; changed (); }
	// Put the entry in; then move (dr, dc). false: the formula cannot be read (the editing goes on).
	bool commit (int dr, int dc)
	{
		if (!ed.on) return true;
		Sheet *s = book_sheet_by_id (*b, ed.sheetId);
		if (!s) { ed.stop (); return true; }
		const char *t = ed.t.str ();
		if (onCommit && !onCommit (ed.r, ed.c, t)) return false;
		int r = ed.r, c = ed.c;
		ed.stop (); acStart = -1;
		if (b->sh[b->active] != s) { for (int i = 0; i < b->ns; i++) if (b->sh[i] == s) b->active = i; }
		if (onEdited) onEdited ();
		if (dr || dc)
		{
			Rect sl = sel ();
			bool multi = (sl.r1 > sl.r0 || sl.c1 > sl.c0) && !m_wholeCols && !m_wholeRows;
			if (multi)						// (within the selection, wrapping)
			{
				int nr = r + dr, nc = c + dc;
				if (nr > sl.r1) { nr = sl.r0; nc++; } if (nr < sl.r0) { nr = sl.r1; nc--; }
				if (nc > sl.c1) { nc = sl.c0; nr += dc ? 1 : 0; if (nr > sl.r1) nr = sl.r0; } if (nc < sl.c0) { nc = sl.c1; }
				s->curR = iclamp (nr, sl.r0, sl.r1); s->curC = iclamp (nc, sl.c0, sl.c1);
				ensureVisible (s->curR, s->curC);
			}
			else
			{
				int tabC = m_tabC;
				if (dc && !dr && tabC < 0) tabC = c;		// (Tab: the row's first column kept)
				int mi = merge_at (s, r, c);
				if (mi >= 0) { if (dr > 0) r = s->merges[mi].r1; if (dc > 0) c = s->merges[mi].c1; }
				if (dr > 0 && !dc && tabC >= 0) selectCell (r + dr, tabC, false);
				else selectCell (r + dr, c + dc, false);
				m_tabC = dc && !dr ? tabC : -1;
			}
		}
		changed ();
		return true;
	}
	// A reference pointed (by the arrows or the mouse): put / replaced in the formula at the caret.
	void pointAt (int r0, int c0, int r1, int c1)
	{
		Sheet *s = S ();
		char ref[80]; Buf o;
		if (s->id != ed.sheetId) { put_sheet_name (o, s->name); o.put ('!'); }
		Rect q = { imin (r0, r1), imin (c0, c1), imax (r0, r1), imax (c0, c1) };
		cell_name (q.r0, q.c0, ref); o.puts (ref);
		if (q.r1 != q.r0 || q.c1 != q.c0) { o.put (':'); cell_name (q.r1, q.c1, ref); o.puts (ref); }
		if (!(ed.pointing && ed.caret == ed.pe)) { ed.ps = ed.pe = ed.caret = ed.anchor = ed.s0 (); }
		ed.replace (ed.ps, ed.pe, o.str (), o.n);
		ed.pe = ed.ps + o.n; ed.caret = ed.anchor = ed.pe;
		ed.pointing = true; ed.pr0 = r0; ed.pc0 = c0; ed.pr1 = r1; ed.pc1 = c1;
		changed ();
	}
	// F4: the reference at the caret cycles A1 -> $A$1 -> A$1 -> $A1.
	void cycleAbsolute ()
	{
		if (!ed.formula ()) return;
		RefHi refs[64]; int n = edit_refs (*b, ed, refs, 64);
		for (int i = 0; i < n; i++)
		{
			if (ed.caret < refs[i].at || ed.caret > refs[i].at + refs[i].len) continue;
			// the text of the reference: each cell's part cycled
			Buf o; const char *p = ed.t.b + refs[i].at, *e = p + refs[i].len;
			const char *bang = 0; for (const char *q = p; q < e; q++) if (*q == '!') bang = q;
			if (bang) { o.putn (p, (int) (bang - p + 1)); p = bang + 1; }
			while (p < e)
			{
				const char *q = p; bool ac = false, ar = false;
				if (*q == '$') { ac = true; q++; }
				const char *ls = q; while (q < e && isalpha ((unsigned char) *q)) q++;
				const char *le = q;
				if (q < e && *q == '$') { ar = true; q++; }
				const char *ds = q; while (q < e && isdigit ((unsigned char) *q)) q++;
				const char *de = q;
				int state = ac && ar ? 1 : !ac && ar ? 2 : ac && !ar ? 3 : 0;
				state = (state + 1) % 4;
				bool nac = state == 1 || state == 3, nar = state == 1 || state == 2;
				if (le > ls) { if (nac) o.put ('$'); o.putn (ls, (int) (le - ls)); }
				if (de > ds) { if (nar) o.put ('$'); o.putn (ds, (int) (de - ds)); }
				if (q < e && *q == ':') { o.put (':'); q++; }
				if (q == p) { o.put (*q); q++; }
				p = q;
			}
			ed.replace (refs[i].at, refs[i].at + refs[i].len, o.str (), o.n);
			ed.pointing = false;
			changed ();
			return;
		}
	}
	// The functions whose names start with what is typed (the list under the cell).
	void updateAutocomplete ()
	{
		char w[40];
		int st = edit_word (ed, w, sizeof w);
		acN = 0;
		if (st < 0 || !w[0]) { acStart = -1; return; }
		int wl = (int) strlen (w);
		for (int i = 0; i < NFNS && acN < 12; i++)
			if ((int) strlen (FNS[i].name) > wl && ascii_ieq (FNS[i].name, w, wl)) acList[acN++] = i;
		if (!acN) { acStart = -1; return; }
		if (acStart != st || strcmp (acWord, w)) acSel = 0;
		acStart = st; scpy (acWord, w, sizeof acWord);
		if (acSel >= acN) acSel = 0;
	}
	void acceptAutocomplete ()
	{
		if (acStart < 0 || !acN) return;
		const char *nm = FNS[acList[acSel]].name;
		Buf o; o.puts (nm); o.put ('(');
		ed.replace (acStart, ed.caret, o.str (), o.n);
		acStart = -1;
		changed ();
	}

	// ---- keys
	bool onKey (long k) override;
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override;
	void onDraw () override;
	void tick ()
	{
		if (!ed.on) return;
		unsigned t = kapi_get_ticks ();
		if (t - m_blinkT >= 50) { m_blinkT = t; m_blink = !m_blink; invalidate (true); }
	}
	void blinkOn () { m_blink = true; m_blinkT = kapi_get_ticks (); }
	Rect fillTo;						// (the fill handle's drag: the range to be)
	Rect m_fillRect; int m_fitCol, m_fitRow;		// (what an action asked of the app: see onContext)
private:
	bool m_rdown = false;
	int m_drag, m_dragI, m_dragV;
	int m_dragX0, m_dragY0, m_chartX0, m_chartY0, m_chartW0, m_chartH0, m_corner;
	unsigned m_lastClick; int m_lastR, m_lastC;
	int m_tabC = -1;					// a row typed with Tab: its first column (Enter goes back there)
	bool m_moved = false;					// the chart dragged has moved
	bool m_blink; unsigned m_blinkT;
	bool m_wholeCols = false, m_wholeRows = false;
	void drawHeaders (const PaneView *pv);
	void drawSelection (const PaneView &v, int paneIdx);
	void drawEditor ();
	void drawCharts (const PaneView &v, int pane);
	// A chart's box on the canvas as the pane shows it (frozen panes: the part in each one, as the
	// cells it lies over; pane 1 and 3 scroll across, 2 and 3 down).
	void chartBox (Chart *c, int *x, int *y, int *w, int *h, int pane = 3)
	{
		Sheet *s = S ();
		PaneView pv[4]; panes (pv);
		int c0 = pane & 1 ? imax (s->leftC, s->freezeC) : 0, r0 = pane & 2 ? imax (s->topR, s->freezeR) : 0;
		*x = pv[pane].x0 + (c->x - col_x (s, c0)) * z / 100;
		*y = pv[pane].y0 + (int) ((c->y - row_y (s, r0)) * z / 100);
		*w = c->w * z / 100; *h = c->h * z / 100;
	}
	int paneAt (int mx, int my)
	{
		PaneView pv[4]; panes (pv);
		for (int k = 0; k < 3; k++) if (mx >= pv[k].x0 && mx < pv[k].x1 && my >= pv[k].y0 && my < pv[k].y1) return k;
		return 3;
	}
	int chartAt (int mx, int my)
	{
		Sheet *s = S ();
		int p = paneAt (mx, my);
		for (int i = s->ncharts - 1; i >= 0; i--)
		{
			int x, y, w, h; chartBox (s->charts[i], &x, &y, &w, &h, p);
			if (mx >= x - 4 && mx < x + w + 4 && my >= y - 4 && my < y + h + 4) return i;
		}
		return -1;
	}
	void keyMove (int dr, int dc, bool shift, bool ctrl);
	void dataEdge (int *r, int *c, int dr, int dc);
};

// ---- drawing ----------------------------------------------------------------------------------------------
static fnt::Font *ui_font (int px, bool bold = false) { return fnt::get (g_famUI >= 0 ? g_famUI : g_famSans, bold ? fnt::BOLD : 0, px * 64); }

void GridView::drawHeaders (const PaneView *pv)
{
	Sheet *s = S ();
	Rect sl = sel ();
	int RW = rowHeadW ();
	unsigned face = wk_tone (C_FACE, 166), line = wk_tone (C_FACE, 120);
	unsigned hot = wk_mix (face, C_ACCENT, 70), hotAll = wk_mix (face, C_ACCENT, 140);
	fnt::Font *f = ui_font (12), *fb = ui_font (12, true);
	Rect clipAll = { 0, 0, height - 1, width - 1 };
	// the corner
	canvas.fillRect (0, 0, RW, HEAD_H, face);
	{ VPath p; int tri[6] = { V (RW - 4), V (HEAD_H - 4), V (RW - 4), V (HEAD_H - 14), V (RW - 14), V (HEAD_H - 4) }; p.poly (tri, 3); p.fill (canvas, wk_tone (C_FACE, 110)); }
	// the column letters (panes 0/2 give the frozen columns, 1/3 the others)
	for (int pi = 0; pi < 2; pi++)
	{
		const PaneView &v = pv[pi == 0 ? 2 : 3];
		Rect clip = { 0, v.x0, HEAD_H - 1, v.x1 - 1 };
		canvas.fillRect (v.x0, 0, v.x1 - v.x0, HEAD_H, face);
		for (int i = 0; i < v.nc; i++)
		{
			int c = v.col[i].i, x = v.col[i].at, w = v.col[i].len;
			bool in = c >= sl.c0 && c <= sl.c1;
			bool all = in && m_wholeCols;
			if (in) { int X0 = imax (x, clip.c0), X1 = imin (x + w, clip.c1 + 1); if (X1 > X0) canvas.fillRect (X0, 0, X1 - X0, HEAD_H, all ? hotAll : hot); }
			if (in && !all) hline (canvas, x, x + w - 1, HEAD_H - 2, C_ACCENT, BS_THIN, clip), hline (canvas, x, x + w - 1, HEAD_H - 1, C_ACCENT, BS_THIN, clip);
			vline (canvas, x + w - 1, 3, HEAD_H - 3, line, BS_THIN, clip);
			char t[8]; col_name (c, t);
			unsigned ink = all ? 0xFFFFFF : in ? wk_mix (C_TEXT, C_ACCENT, 120) : C_TEXT;
			text_at (canvas, in ? fb : f, x + w / 2, HEAD_H / 2 + 5, t, ink, 1, clip);
		}
	}
	hline (canvas, 0, width - 1, HEAD_H - 1, line, BS_THIN, clipAll);
	// the row numbers
	for (int pi = 0; pi < 2; pi++)
	{
		const PaneView &v = pv[pi == 0 ? 1 : 3];
		Rect clip = { v.y0, 0, v.y1 - 1, RW - 1 };
		canvas.fillRect (0, v.y0, RW, v.y1 - v.y0, face);
		for (int i = 0; i < v.nr; i++)
		{
			int r = v.row[i].i, y = v.row[i].at, h = v.row[i].len;
			bool in = r >= sl.r0 && r <= sl.r1;
			bool all = in && m_wholeRows;
			if (in) { int Y0 = imax (y, clip.r0), Y1 = imin (y + h, clip.r1 + 1); if (Y1 > Y0) canvas.fillRect (0, Y0, RW, Y1 - Y0, all ? hotAll : hot); }
			if (in && !all) { vline (canvas, RW - 2, y, y + h - 1, C_ACCENT, BS_THIN, clip); vline (canvas, RW - 1, y, y + h - 1, C_ACCENT, BS_THIN, clip); }
			hline (canvas, 3, RW - 4, y + h - 1, line, BS_THIN, clip);
			char t[12]; snprintf (t, sizeof t, "%d", r + 1);
			unsigned ink = all ? 0xFFFFFF : in ? wk_mix (C_TEXT, C_ACCENT, 120) : C_TEXT;
			text_at (canvas, in ? fb : f, RW / 2, y + h / 2 + 5, t, ink, 1, clip);
		}
	}
	vline (canvas, RW - 1, 0, height - SB - 1, line, BS_THIN, clipAll);
	(void) s;
}

void GridView::drawSelection (const PaneView &v, int paneIdx)
{
	(void) paneIdx;
	Sheet *s = S ();
	Rect clip = { v.y0, v.x0, v.y1 - 1, v.x1 - 1 };
	if (v.nc == 0 || v.nr == 0) return;
	// the references of a formula being typed: framed in their colours
	if (ed.on && ed.formula ())
	{
		RefHi refs[64]; int n = edit_refs (*b, ed, refs, 64);
		for (int i = 0; i < n; i++)
		{
			if (refs[i].sheetId != s->id) continue;
			Rect r = refs[i].r;
			if (r.r1 < v.row[0].i || r.r0 > v.row[v.nr - 1].i + 1 || r.c1 < v.col[0].i || r.c0 > v.col[v.nc - 1].i + 1) continue;
			int x0 = pane_col_x (s, z, v, r.c0), x1 = pane_col_x (s, z, v, imin (r.c1, MAXC - 1) + 1);
			int y0 = pane_row_y (s, z, v, r.r0), y1 = pane_row_y (s, z, v, imin (r.r1, MAXR - 1) + 1);
			unsigned col = refs[i].col;
			for (int yy = imax (y0, clip.r0); yy < imin (y1, clip.r1 + 1); yy++)
				for (int xx = imax (x0, clip.c0); xx < imin (x1, clip.c1 + 1); xx++) { unsigned &px = canvas.px[yy * canvas.stride + xx]; px = wk_mix (px, col, 28); }
			for (int t = 0; t < 2; t++)
			{
				hline (canvas, x0 - 1, x1 - 1, y0 - 1 + t, col, BS_THIN, clip); hline (canvas, x0 - 1, x1 - 1, y1 - 2 + t, col, BS_THIN, clip);
				vline (canvas, x0 - 1 + t, y0 - 1, y1 - 1, col, BS_THIN, clip); vline (canvas, x1 - 2 + t, y0 - 1, y1 - 1, col, BS_THIN, clip);
			}
		}
	}
	// the copied range's dashes
	if (copyMarquee && copySheet == s->id)
	{
		Rect r = copyRect;
		int x0 = pane_col_x (s, z, v, r.c0), x1 = pane_col_x (s, z, v, imin (r.c1, MAXC - 1) + 1);
		int y0 = pane_row_y (s, z, v, r.r0), y1 = pane_row_y (s, z, v, imin (r.r1, MAXR - 1) + 1);
		for (int t = 0; t < 2; t++)
		{
			hline (canvas, x0 - 1, x1 - 1, y0 - 1 + t, C_ACCENT, BS_DASHED, clip); hline (canvas, x0 - 1, x1 - 1, y1 - 2 + t, C_ACCENT, BS_DASHED, clip);
			vline (canvas, x0 - 1 + t, y0 - 1, y1 - 1, C_ACCENT, BS_DASHED, clip); vline (canvas, x1 - 2 + t, y0 - 1, y1 - 1, C_ACCENT, BS_DASHED, clip);
		}
	}
	if (ed.on && ed.pointing) return;				// (the pointed range shows as a reference)
	Rect sl = sel ();
	int x0 = pane_col_x (s, z, v, sl.c0), x1 = pane_col_x (s, z, v, imin (sl.c1, MAXC - 1) + 1);
	int y0 = pane_row_y (s, z, v, sl.r0), y1 = pane_row_y (s, z, v, imin (sl.r1, MAXR - 1) + 1);
	if (m_wholeCols) { y0 = v.y0 - 2; y1 = v.y1 + 2; }
	if (m_wholeRows) { x0 = v.x0 - 2; x1 = v.x1 + 2; }
	// the range tinted but the cursor's cell
	int cx, cy, cw, ch;
	bool curShown = cellBox (s->curR, s->curC, &cx, &cy, &cw, &ch);
	bool multi = sl.r1 > sl.r0 || sl.c1 > sl.c0;
	if (multi && !ed.on)
		for (int yy = imax (y0, clip.r0); yy < imin (y1, clip.r1 + 1); yy++)
			for (int xx = imax (x0, clip.c0); xx < imin (x1, clip.c1 + 1); xx++)
			{
				if (curShown && xx >= cx && xx < cx + cw && yy >= cy && yy < cy + ch) continue;
				unsigned &px = canvas.px[yy * canvas.stride + xx]; px = wk_mix (px, C_ACCENT, 46);
			}
	// the frame (2 px) and the fill handle
	unsigned fc = C_ACCENT;
	for (int t = 0; t < 2; t++)
	{
		hline (canvas, x0 - 1, x1 - 1, y0 - 1 + t, fc, BS_THIN, clip); hline (canvas, x0 - 1, x1 - 1, y1 - 2 + t, fc, BS_THIN, clip);
		vline (canvas, x0 - 1 + t, y0 - 1, y1 - 1, fc, BS_THIN, clip); vline (canvas, x1 - 2 + t, y0 - 1, y1 - 1, fc, BS_THIN, clip);
	}
	if (!ed.on && !m_wholeCols && !m_wholeRows)
	{
		int hx = x1 - 4, hy = y1 - 4;
		for (int yy = hy; yy < hy + 7; yy++) for (int xx = hx; xx < hx + 7; xx++)
			if (xx >= clip.c0 && xx <= clip.c1 && yy >= clip.r0 && yy <= clip.r1) canvas.px[yy * canvas.stride + xx] = (yy == hy || yy == hy + 6 || xx == hx || xx == hx + 6) ? 0xFFFFFF : fc;
	}
	// the fill handle dragged: the range it will fill
	if (m_drag == D_FILL && fillTo.r1 >= fillTo.r0)
	{
		Rect r = fillTo;
		int a0 = pane_col_x (s, z, v, r.c0), a1 = pane_col_x (s, z, v, r.c1 + 1), b0 = pane_row_y (s, z, v, r.r0), b1 = pane_row_y (s, z, v, r.r1 + 1);
		hline (canvas, a0 - 1, a1 - 1, b0 - 1, 0x606060, BS_DASHED, clip); hline (canvas, a0 - 1, a1 - 1, b1 - 1, 0x606060, BS_DASHED, clip);
		vline (canvas, a0 - 1, b0 - 1, b1 - 1, 0x606060, BS_DASHED, clip); vline (canvas, a1 - 1, b0 - 1, b1 - 1, 0x606060, BS_DASHED, clip);
	}
}

void GridView::drawCharts (const PaneView &v, int pane)
{
	Sheet *s = S ();
	for (int i = 0; i < s->ncharts; i++)
	{
		Chart *c = s->charts[i];
		int x, y, w, h; chartBox (c, &x, &y, &w, &h, pane);
		int X0 = imax (x, v.x0), Y0 = imax (y, v.y0), X1 = imin (x + w, v.x1), Y1 = imin (y + h, v.y1);
		if (X1 <= X0 || Y1 <= Y0) continue;
		Canvas sub; sub.adopt (canvas.px + Y0 * canvas.stride + X0, X1 - X0, Y1 - Y0, canvas.stride);
		draw_chart (sub, *b, c, x - X0, y - Y0, w, h, z, Rect { 0, 0, sub.h - 1, sub.w - 1 });
		if (i == selChart)
		{
			Rect clip = { v.y0, v.x0, v.y1 - 1, v.x1 - 1 };
			for (int t = 0; t < 2; t++)
			{
				hline (canvas, x - 1 - t, x + w + t, y - 1 - t, C_ACCENT, BS_THIN, clip); hline (canvas, x - 1 - t, x + w + t, y + h + t, C_ACCENT, BS_THIN, clip);
				vline (canvas, x - 1 - t, y - 1 - t, y + h + t, C_ACCENT, BS_THIN, clip); vline (canvas, x + w + t, y - 1 - t, y + h + t, C_ACCENT, BS_THIN, clip);
			}
			int hx[4] = { x - 4, x + w - 3, x - 4, x + w - 3 }, hy[4] = { y - 4, y - 4, y + h - 3, y + h - 3 };
			for (int k = 0; k < 4; k++)
				for (int yy = hy[k]; yy < hy[k] + 8; yy++) for (int xx = hx[k]; xx < hx[k] + 8; xx++)
					if (xx >= clip.c0 && xx <= clip.c1 && yy >= clip.r0 && yy <= clip.r1) canvas.px[yy * canvas.stride + xx] = (yy == hy[k] || yy == hy[k] + 7 || xx == hx[k] || xx == hx[k] + 7) ? C_ACCENT : 0xFFFFFF;
		}
	}
}

void GridView::drawEditor ()
{
	if (!ed.on) return;
	Sheet *s = book_sheet_by_id (*b, ed.sheetId);
	if (!s || s != S ()) return;
	int x, y, w, h;
	if (!cellBox (ed.r, ed.c, &x, &y, &w, &h)) return;
	Cell *cell = s->cells.get (ed.r, ed.c);
	const Style &st = cell_style (*b, s, cell, ed.r, ed.c);
	fnt::Font *f = style_font (*b, st, z);
	const char *t = ed.t.str (); int n = ed.t.n;
	// its lines (line breaks typed), the box grown to them
	int ls[64], le[64], nl = 0;
	for (int i = 0; i <= n && nl < 64; ) { int j = i; while (j < n && t[j] != '\n') j++; ls[nl] = i; le[nl] = j; nl++; if (j >= n) break; i = j + 1; }
	int tw = 0; for (int i = 0; i < nl; i++) tw = imax (tw, u8_width (f, t + ls[i], le[i] - ls[i]) >> 6);
	int lh = imax (f->height >> 6, (f->ascent + f->descent) >> 6);
	int bw = imax (w, tw + 10), bh = imax (h, nl * lh + 6);
	int maxW = width - SB - x - 2;
	if (bw > maxW) bw = imax (w, maxW);
	Rect clip = { imax (y - 1, HEAD_H), imax (x - 1, rowHeadW ()), imin (y + bh + 1, height - SB - 1), imin (x + bw + 1, width - SB - 1) };
	for (int yy = clip.r0; yy <= clip.r1; yy++) for (int xx = clip.c0; xx <= clip.c1; xx++) canvas.px[yy * canvas.stride + xx] = st.fill == AUTO ? 0xFFFFFF : st.fill;
	for (int k = 0; k < 2; k++)
	{
		hline (canvas, x - 1 - k, x + bw + k, y - 1 - k, C_ACCENT, BS_THIN, clip); hline (canvas, x - 1 - k, x + bw + k, y + bh + k, C_ACCENT, BS_THIN, clip);
		vline (canvas, x - 1 - k, y - 1 - k, y + bh + k, C_ACCENT, BS_THIN, clip); vline (canvas, x + bw + k, y - 1 - k, y + bh + k, C_ACCENT, BS_THIN, clip);
	}
	// the text: the references in their colours, the selection, the caret
	RefHi refs[64]; int nr = edit_refs (*b, ed, refs, 64);
	unsigned ink = st.color == AUTO ? 0 : st.color;
	int asc = f->ascent >> 6;
	int base0 = y + 3 + asc;
	if (nl == 1 && bh == h && st.va == VA_BOTTOM) base0 = y + h - 3 - (f->descent >> 6);
	for (int li = 0; li < nl; li++)
	{
		int x64 = (x + 4) * 64, base = base0 + li * lh;
		int i = ls[li], l; unsigned prev = 0;
		while (i < le[li])
		{
			unsigned cp = u8_dec (t + i, n - i, &l);
			if (prev) x64 += fnt::kern (f, prev, cp);
			int adv = fnt::advance (f, cp);
			bool selc = i >= ed.s0 () && i < ed.s1 ();
			unsigned c = ink;
			for (int k = 0; k < nr; k++) if (i >= refs[k].at && i < refs[k].at + refs[k].len) c = refs[k].col;
			if (selc) { int X0 = x64 >> 6, X1 = (x64 + adv) >> 6; for (int yy = base - asc; yy < base - asc + lh; yy++) for (int xx = X0; xx < X1; xx++) if (xx >= clip.c0 && xx <= clip.c1 && yy >= clip.r0 && yy <= clip.r1) canvas.px[yy * canvas.stride + xx] = wk_mix (0xFFFFFF, C_ACCENT, 110); }
			fnt::draw (canvas, f, x64, base, cp, c, clip.c0, clip.r0, clip.c1 + 1, clip.r1 + 1);
			if (i == ed.caret && m_blink) { int cxp = x64 >> 6; vline (canvas, cxp, base - asc, base - asc + lh - 1, 0, BS_THIN, clip); }
			x64 += adv; prev = cp; i += l;
		}
		if (ed.caret == le[li] && m_blink) { int cxp = x64 >> 6; vline (canvas, cxp, base - asc, base - asc + lh - 1, 0, BS_THIN, clip); }
	}
	// the function's arguments (a tip below), or the functions offered
	int ay = y + bh + 4;
	int arg = 0, fi = edit_function (ed, &arg);
	fnt::Font *uf = ui_font (12), *ub = ui_font (12, true);
	if (acStart >= 0 && acN)
	{
		int rowH = 20, lw = 0;
		for (int i = 0; i < acN; i++) lw = imax (lw, (u8_width (ub, FNS[acList[i]].name, (int) strlen (FNS[acList[i]].name)) >> 6));
		int bw2 = imax (lw + 24, 260), bh2 = acN * rowH + 8;
		int bx = x;
		if (ay + bh2 > height - SB) ay = y - bh2 - 4;
		if (bx + bw2 > width - SB) bx = width - SB - bw2;
		panel_in (canvas, bx, ay, bw2, bh2, 6, C_FIELD);
		Rect c2 = { ay, bx, ay + bh2 - 1, bx + bw2 - 1 };
		for (int i = 0; i < acN; i++)
		{
			int yy = ay + 4 + i * rowH;
			bool hot = i == acSel;
			if (hot) wk_hilite (canvas, bx + 4, yy, bw2 - 8, rowH, 4, true);
			text_at (canvas, ub, bx + 10, yy + 15, FNS[acList[i]].name, hot ? C_SEL_TEXT : C_FIELD_TEXT, 0, c2);
		}
		if (acSel < acN)					// (what it does, beside)
		{
			const FnDef &d = FNS[acList[acSel]];
			int tw2 = (u8_width (uf, d.help, (int) strlen (d.help)) >> 6) + 16;
			int tx = bx + bw2 + 4; if (tx + tw2 > width - SB) tx = imax (rowHeadW (), bx - tw2 - 4);
			panel_in (canvas, tx, ay, tw2, 24, 5, 0xFFFFE8);
			text_at (canvas, uf, tx + 8, ay + 16, d.help, 0x333333, 0, Rect { ay, tx, ay + 23, tx + tw2 - 1 });
		}
	}
	else if (fi >= 0)
	{
		const FnDef &d = FNS[fi];
		// "SUM(number1, [number2], ...)" with the current argument in bold
		Buf full; full.puts (d.name); full.put ('(');
		int an = (int) strlen (d.args), k = 0, cur0 = -1, cur1 = -1, ai = 0;
		for (int i = 0; i <= an; i++)
		{
			if (i == an || d.args[i] == ',')
			{
				int s0 = full.n;
				int a0 = k; while (a0 < i && d.args[a0] == ' ') a0++;
				full.putn (d.args + a0, i - a0);
				bool last = i == an || strstr (d.args + i, "...") != 0;
				if (ai == arg || (last && arg > ai && strstr (d.args + a0, "..."))) { cur0 = s0; cur1 = full.n; }
				if (i < an) full.puts (", ");
				k = i + 1; ai++;
			}
		}
		full.put (')');
		int tw3 = (u8_width (uf, full.str (), full.n) >> 6) + 20;
		int tx = x; if (tx + tw3 > width - SB) tx = imax (rowHeadW (), width - SB - tw3);
		panel_in (canvas, tx, ay, tw3, 24, 5, 0xFFFFE8);
		Rect c3 = { ay, tx, ay + 23, tx + tw3 - 1 };
		int x64 = (tx + 10) * 64;
		for (int i = 0; i < full.n; )
		{
			int l; unsigned cp = u8_dec (full.b + i, full.n - i, &l);
			fnt::Font *ff = i >= cur0 && i < cur1 ? ub : uf;
			fnt::draw (canvas, ff, x64, ay + 16, cp, 0x333333, c3.c0, c3.r0, c3.c1 + 1, c3.r1 + 1);
			x64 += fnt::advance (ff, cp); i += l;
		}
	}
}

void GridView::onDraw ()
{
	Sheet *s = S ();
	canvas.clear (0xFFFFFF);
	PaneView pv[4]; panes (pv);
	for (int p = 0; p < 4; p++)
	{
		if (pv[p].x1 <= pv[p].x0 || pv[p].y1 <= pv[p].y0) continue;
		canvas.fillRect (pv[p].x0, pv[p].y0, pv[p].x1 - pv[p].x0, pv[p].y1 - pv[p].y0, 0xFFFFFF);
		paint_pane (canvas, *b, s, z, pv[p], s->grid);
		drawSelection (pv[p], p);
	}
	for (int p = 0; p < 4; p++) if (pv[p].x1 > pv[p].x0 && pv[p].y1 > pv[p].y0) drawCharts (pv[p], p);
	// the frozen panes' edges
	Rect all = { 0, 0, height - 1, width - 1 };
	if (s->freezeC) vline (canvas, pv[1].x0 - 1, HEAD_H, height - SB - 1, 0x9AA0A6, BS_THIN, all);
	if (s->freezeR) hline (canvas, rowHeadW (), width - SB - 1, pv[2].y0 - 1, 0x9AA0A6, BS_THIN, all);
	drawHeaders (pv);
	drawEditor ();
	// the scroll bars (the used area and a bit more: their range grows as one goes)
	sheet_bounds (s);
	int RW = rowHeadW ();
	long totR = imax (s->maxR + 1, s->topR + visRows ()) + 50, totC = imax (s->maxC + 1, s->leftC + visCols ()) + 10;
	canvas.fillRect (width - SB, 0, SB, height, C_BG);
	canvas.fillRect (0, height - SB, width, SB, C_BG);
	WkThumb tv = wk_thumb (totR, visRows (), s->topR, height - SB - HEAD_H - 4);
	wk_scroll_bar (canvas, width - SB + 2, HEAD_H + 2, WK_SBW, height - SB - HEAD_H - 4, true, tv.y, tv.show ? tv.h : 0, C_BG, m_drag == D_VBAR ? WK_HOT : WK_NORMAL);
	WkThumb th = wk_thumb (totC, visCols (), s->leftC, width - SB - RW - 4);
	wk_scroll_bar (canvas, RW + 2, height - SB + 2, width - SB - RW - 4, WK_SBW, false, th.y, th.show ? th.h : 0, C_BG, m_drag == D_HBAR ? WK_HOT : WK_NORMAL);
}

// ---- the keys ---------------------------------------------------------------------------------------------
// Ctrl + an arrow: to the edge of the data (the last filled cell before an empty one, or the next filled).
void GridView::dataEdge (int *r, int *c, int dr, int dc)
{
	Sheet *s = S ();
	sheet_bounds (s);
	auto filled = [&] (int rr, int cc) { Cell *x = s->cells.get (rr, cc); return x && x->kind != K_NONE; };
	int rr = *r, cc = *c;
	int limR = dr > 0 ? imax (s->maxR, rr) : 0, limC = dc > 0 ? imax (s->maxC, cc) : 0;
	bool here = filled (rr, cc), nextF = filled (iclamp (rr + dr, 0, MAXR - 1), iclamp (cc + dc, 0, MAXC - 1));
	if (here && nextF)
	{
		while (true)
		{
			int nr = rr + dr, nc = cc + dc;
			if (nr < 0 || nc < 0 || nr >= MAXR || nc >= MAXC || !filled (nr, nc)) break;
			rr = nr; cc = nc;
		}
	}
	else
	{
		while (true)
		{
			int nr = rr + dr, nc = cc + dc;
			if (nr < 0 || nc < 0 || nr >= MAXR || nc >= MAXC) break;
			rr = nr; cc = nc;
			if (filled (rr, cc)) break;
			if ((dr > 0 && rr > limR) || (dc > 0 && cc > limC)) { rr = dr > 0 ? MAXR - 1 : rr; cc = dc > 0 ? MAXC - 1 : cc; break; }
		}
	}
	*r = rr; *c = cc;
}
void GridView::keyMove (int dr, int dc, bool shift, bool ctrl)
{
	Sheet *s = S ();
	int r = s->curR, c = s->curC;
	if (ctrl) dataEdge (&r, &c, dr, dc);
	else
	{
		// past a merge and the hidden rows / columns
		int mi = merge_at (s, r, c);
		if (mi >= 0 && !shift) { if (dr > 0) r = s->merges[mi].r1; if (dc > 0) c = s->merges[mi].c1; if (dr < 0) r = s->merges[mi].r0; if (dc < 0) c = s->merges[mi].c0; }
		r += dr; c += dc;
		while (dr && r > 0 && r < MAXR - 1 && row_h (s, r) == 0) r += dr;
		while (dc && c > 0 && c < MAXC - 1 && col_w (s, c) == 0) c += dc;
	}
	if (shift) { if (m_wholeCols) c = iclamp (c, 0, MAXC - 1); selectCell (r, c, true); }
	else selectCell (r, c, false);
}
bool GridView::onKey (long k)
{
	Sheet *s = S ();
	int mods = kapi_get_modifiers ();
	bool shift = mods & MOD_SHIFT, ctrl = mods & MOD_CTRL, alt = mods & MOD_ALT;
	if (selChart >= 0 && !ed.on)
	{
		if (k == KEY_DEL || k == KEY_BACKSPACE) { if (onContext) onContext (-1, -1, 4); return true; }	// (the app deletes it)
		if (k == 27) { selChart = -1; changed (); return true; }
		if (k == KEY_ENTER) { if (onChartOpen) onChartOpen (selChart); return true; }
	}
	if (ed.on)
	{
		blinkOn ();
		if (acStart >= 0 && acN)
		{
			if (k == KEY_DOWN) { acSel = (acSel + 1) % acN; invalidate (true); return true; }
			if (k == KEY_UP) { acSel = (acSel + acN - 1) % acN; invalidate (true); return true; }
			if (k == KEY_TAB || (k == KEY_ENTER && !shift)) { acceptAutocomplete (); return true; }
			if (k == 27) { acStart = -1; invalidate (true); return true; }
		}
		switch (k)
		{
		case KEY_ENTER:
			if (alt) { ed.insert ("\n", 1); changed (); return true; }
			commit (shift ? -1 : 1, 0); return true;
		case KEY_TAB: commit (0, shift ? -1 : 1); return true;
		case 27: cancelEdit (); return true;
		case KEY_F1 + 1: ed.typed = !ed.typed; ed.pointing = false; changed (); return true;	// F2
		case KEY_F1 + 3: cycleAbsolute (); return true;					// F4
		case KEY_UP: case KEY_DOWN: case KEY_LEFT: case KEY_RIGHT:
		{
			int dr = k == KEY_UP ? -1 : k == KEY_DOWN ? 1 : 0, dc = k == KEY_LEFT ? -1 : k == KEY_RIGHT ? 1 : 0;
			if (ed.typed && ed.can_point ())
			{
				int r = ed.pointing ? ed.pr1 : ed.r, c = ed.pointing ? ed.pc1 : ed.c;
				int r0 = ed.pointing ? ed.pr0 : ed.r, c0 = ed.pointing ? ed.pc0 : ed.c;
				if (ctrl) dataEdge (&r, &c, dr, dc);
				else { r = iclamp (r + dr, 0, MAXR - 1); c = iclamp (c + dc, 0, MAXC - 1); }
				if (shift && ed.pointing) pointAt (r0, c0, r, c); else pointAt (r, c, r, c);
				ensureVisible (r, c);
				return true;
			}
			if (ed.typed) { commit (dr, dc); return true; }
			if (dc)
			{
				int to = dc < 0 ? (ctrl ? ed.word_left (ed.caret) : ed.prev (ed.caret)) : (ctrl ? ed.word_right (ed.caret) : ed.next (ed.caret));
				if (!shift && ed.s0 () != ed.s1 ()) to = dc < 0 ? ed.s0 () : ed.s1 ();
				ed.caret = to; if (!shift) ed.anchor = to;
			}
			else
			{
				// up / down between the lines of a text with line breaks
				int ls = ed.caret; while (ls > 0 && ed.t.b[ls - 1] != '\n') ls--;
				int col = ed.caret - ls;
				if (dr < 0 && ls > 0) { int ps = ls - 1; while (ps > 0 && ed.t.b[ps - 1] != '\n') ps--; ed.caret = imin (ps + col, ls - 1); }
				else if (dr > 0) { int e = ed.caret; while (e < ed.t.n && ed.t.b[e] != '\n') e++; if (e < ed.t.n) { int ne = e + 1; while (ne < ed.t.n && ed.t.b[ne] != '\n') ne++; ed.caret = imin (e + 1 + col, ne); } }
				if (!shift) ed.anchor = ed.caret;
			}
			ed.pointing = false;
			changed ();
			return true;
		}
		case KEY_HOME: ed.caret = 0; if (!shift) ed.anchor = 0; ed.pointing = false; changed (); return true;
		case KEY_END: ed.caret = ed.t.n; if (!shift) ed.anchor = ed.t.n; ed.pointing = false; changed (); return true;
		case KEY_BACKSPACE: ed.backspace (); updateAutocomplete (); changed (); return true;
		case KEY_DEL: ed.del (); updateAutocomplete (); changed (); return true;
		}
		if (k == WK_CTRL ('A')) { ed.anchor = 0; ed.caret = ed.t.n; changed (); return true; }
		if (k >= 32 && k < 256 && k != 127)
		{
			ed.insert_char ((unsigned) k);
			updateAutocomplete ();
			changed ();
			return true;
		}
		return true;
	}
	// ---- not editing
	switch (k)
	{
	case KEY_UP: keyMove (-1, 0, shift, ctrl); return true;
	case KEY_DOWN: keyMove (1, 0, shift, ctrl); return true;
	case KEY_LEFT: keyMove (0, -1, shift, ctrl); return true;
	case KEY_RIGHT: keyMove (0, 1, shift, ctrl); return true;
	case KEY_HOME:
		if (ctrl) selectCell (s->freezeR ? 0 : 0, 0, shift);
		else selectCell (s->curR, 0, shift);
		if (ctrl) { s->topR = s->freezeR; s->leftC = s->freezeC; invalidate (true); }
		return true;
	case KEY_END:
		if (ctrl) { sheet_bounds (s); selectCell (imax (0, s->maxR), imax (0, s->maxC), shift); }
		else { int r = s->curR, c = s->curC; dataEdge (&r, &c, 0, 1); selectCell (r, c, shift); }
		return true;
	case KEY_PGUP: case KEY_PGDN:
	{
		int d = k == KEY_PGUP ? -1 : 1;
		if (ctrl) { if (onContext) onContext (d, 0, 5); return true; }	// (the previous / next sheet: the app)
		if (alt) { int n = visCols (); scrollBy (0, d * n); selectCell (s->curR, iclamp (s->curC + d * n, 0, MAXC - 1), shift); return true; }
		int n = visRows ();
		scrollBy (d * n, 0);
		selectCell (iclamp (s->curR + d * n, 0, MAXR - 1), s->curC, shift);
		return true;
	}
	case KEY_ENTER: case KEY_TAB:
	{
		Rect sl = sel ();
		bool down = k == KEY_ENTER;
		int dr = down ? (shift ? -1 : 1) : 0, dc = down ? 0 : (shift ? -1 : 1);
		if ((sl.r1 > sl.r0 || sl.c1 > sl.c0) && !m_wholeCols && !m_wholeRows)
		{
			int r = s->curR + dr, c = s->curC + dc;
			if (r > sl.r1) { r = sl.r0; c++; } if (r < sl.r0) { r = sl.r1; c--; }
			if (c > sl.c1) { c = sl.c0; r++; if (r > sl.r1) r = sl.r0; } if (c < sl.c0) { c = sl.c1; r--; if (r < sl.r0) r = sl.r1; }
			s->curR = iclamp (r, sl.r0, sl.r1); s->curC = iclamp (c, sl.c0, sl.c1);
			ensureVisible (s->curR, s->curC); changed ();
		}
		else
		{
			int tabC = m_tabC;
			if (!down && tabC < 0) tabC = s->curC;
			if (down && dr > 0 && tabC >= 0) selectCell (s->curR + 1, tabC, false);
			else keyMove (dr, dc, false, false);
			m_tabC = down ? -1 : tabC;
		}
		return true;
	}
	case KEY_F1 + 1: beginEdit (false, 0); return true;			// F2
	case 27: if (copyMarquee) { copyMarquee = false; changed (); } return true;
	}
	if (k == ' ' && ctrl) { Rect sl = sel (); selectCols (sl.c0, sl.c1); return true; }
	if (k == ' ' && shift) { Rect sl = sel (); selectRows (sl.r0, sl.r1); return true; }
	if (k == KEY_BACKSPACE) { beginEdit (true, ""); return true; }
	if (onOtherKey && (ctrl || alt || (k >= KEY_F1 && k <= KEY_F12) || k == KEY_DEL) && onOtherKey (k, mods)) return true;
	if (k >= 32 && k < 256 && k != 127 && !ctrl)
	{
		char t[4]; int n = u8_enc ((unsigned) k, t); t[n] = 0;
		beginEdit (true, t);
		updateAutocomplete ();
		return true;
	}
	return false;
}

// ---- the mouse ----------------------------------------------------------------------------------------------
bool GridView::onMouse (int mx, int my, int bl, int br, int, int wheel)
{
	Sheet *s = S ();
	bool in = mx >= 0 && my >= 0 && mx < width && my < height;
	int mods = kapi_get_modifiers ();
	int RW = rowHeadW ();
	if (wheel && in)
	{
		if (mods & MOD_CTRL) { if (onZoom) onZoom (wheel > 0 ? 1 : -1); return true; }
		if (mods & MOD_SHIFT) scrollBy (0, -wheel * 2); else scrollBy (-wheel * 3, 0);
		return true;
	}
	// a drag going on
	if (m_drag != D_NONE)
	{
		if (!bl)
		{
			int was = m_drag;
			m_drag = D_NONE; catchOutside = false; pressed = false;
			if (was == D_FILL && fillTo.r1 >= fillTo.r0) { Rect t = fillTo; fillTo.r1 = fillTo.r0 - 1; if (onContext) { m_fillRect = t; onContext (0, 0, 6); } }	// (the app fills)
			if (was == D_COLSIZE || was == D_ROWSIZE || was == D_CHART || was == D_CHARTSIZE) { if (onEdited) onEdited (); }
			invalidate (true);
			changed ();
			return true;
		}
		int r, c;
		switch (m_drag)
		{
		case D_SELECT:
			if (!cellAt (iclamp (mx, RW, width - SB - 1), iclamp (my, HEAD_H, height - SB - 1), &r, &c)) break;
			if (mx >= width - SB) scrollBy (0, 1);
			if (my >= height - SB) scrollBy (1, 0);
			if (mx < RW && s->leftC > s->freezeC) scrollBy (0, -1);
			if (my < HEAD_H && s->topR > s->freezeR) scrollBy (-1, 0);
			if (r != s->curR || c != s->curC) { s->curR = r; s->curC = c; changed (); }
			break;
		case D_POINT:
			if (!cellAt (iclamp (mx, RW, width - SB - 1), iclamp (my, HEAD_H, height - SB - 1), &r, &c)) break;
			if (r != ed.pr1 || c != ed.pc1) pointAt (ed.pr0, ed.pc0, r, c);
			break;
		case D_COLSEL:
			if (cellAt (iclamp (mx, RW, width - SB - 1), iclamp (my, HEAD_H, height - SB - 1), &r, &c)) { s->curC = c; changed (); }
			break;
		case D_ROWSEL:
			if (cellAt (iclamp (mx, RW, width - SB - 1), iclamp (my, HEAD_H, height - SB - 1), &r, &c)) { s->curR = r; changed (); }
			break;
		case D_COLSIZE:
		{
			int w = (m_dragV + (mx - m_dragX0)) * 100 / z;
			s->colW[m_dragI] = (unsigned short) iclamp (w, 4, 2000); s->colFl[m_dragI] &= ~RF_HIDDEN;
			cols_changed (s); invalidate (true);
			break;
		}
		case D_ROWSIZE:
		{
			int h = (m_dragV + (my - m_dragY0)) * 100 / z;
			RowInfo *ri = row_add (s, m_dragI); ri->h = (unsigned short) iclamp (h, 4, 2000); ri->fl |= RF_CUSTOM; ri->fl &= ~RF_HIDDEN;
			rows_changed (s); invalidate (true);
			break;
		}
		case D_FILL:
		{
			if (!cellAt (iclamp (mx, RW, width - SB - 1), iclamp (my, HEAD_H, height - SB - 1), &r, &c)) break;
			Rect sl = sel ();
			Rect t = sl;
			int dR = r > sl.r1 ? r - sl.r1 : r < sl.r0 ? r - sl.r0 : 0, dC = c > sl.c1 ? c - sl.c1 : c < sl.c0 ? c - sl.c0 : 0;
			if (abs (dR) >= abs (dC)) { if (dR > 0) t.r1 = r; else if (dR < 0) t.r0 = r; }
			else { if (dC > 0) t.c1 = c; else if (dC < 0) t.c0 = c; }
			if (dR == 0 && dC == 0) t.r1 = t.r0 - 1;
			fillTo = t;
			if (mx >= width - SB) scrollBy (0, 1);
			if (my >= height - SB) scrollBy (1, 0);
			invalidate (true);
			break;
		}
		case D_VBAR:
		{
			sheet_bounds (s);
			long totR = imax (s->maxR + 1, s->topR + visRows ()) + 50;
			WkThumb t = wk_thumb (totR, visRows (), s->topR, height - SB - HEAD_H - 4);
			s->topR = iclamp ((int) wk_thumb_pos (my - HEAD_H - 2, height - SB - HEAD_H - 4, totR, visRows (), t.h), s->freezeR, MAXR - 1);
			invalidate (true);
			break;
		}
		case D_HBAR:
		{
			sheet_bounds (s);
			long totC = imax (s->maxC + 1, s->leftC + visCols ()) + 10;
			WkThumb t = wk_thumb (totC, visCols (), s->leftC, width - SB - RW - 4);
			s->leftC = iclamp ((int) wk_thumb_pos (mx - RW - 2, width - SB - RW - 4, totC, visCols (), t.h), s->freezeC, MAXC - 1);
			invalidate (true);
			break;
		}
		case D_CHART: case D_CHARTSIZE:
		{
			Chart *ch = selChart >= 0 && selChart < s->ncharts ? s->charts[selChart] : 0;
			if (!ch) break;
			int dx = (mx - m_dragX0) * 100 / z, dy = (my - m_dragY0) * 100 / z;
			if (!m_moved)					// (the first move: the app keeps an undo step)
			{
				if (!dx && !dy) break;
				m_moved = true;
				if (onContext) onContext (0, 0, 8);
			}
			if (m_drag == D_CHART) { ch->x = imax (0, m_chartX0 + dx); ch->y = imax (0, m_chartY0 + dy); chart_anchor (s, ch); }
			else
			{
				int x0 = m_chartX0, y0 = m_chartY0, x1 = m_chartX0 + m_chartW0, y1 = m_chartY0 + m_chartH0;
				if (m_corner & 1) x1 += dx; else x0 += dx;
				if (m_corner & 2) y1 += dy; else y0 += dy;
				if (x1 - x0 < 120) { if (m_corner & 1) x1 = x0 + 120; else x0 = x1 - 120; }
				if (y1 - y0 < 90) { if (m_corner & 2) y1 = y0 + 90; else y0 = y1 - 90; }
				ch->x = imax (0, x0); ch->y = imax (0, y0); ch->w = x1 - x0; ch->h = y1 - y0;
				chart_anchor (s, ch);
			}
			invalidate (true);
			break;
		}
		}
		return true;
	}
	if (!in) return false;
	if (br && !m_rdown)
	{
		m_rdown = true;
		setFocus ();
		if (ed.on) return true;
		int ci = chartAt (mx, my);
		if (ci >= 0) { selChart = ci; changed (); if (onContext) onContext (mx, my, 3); return true; }
		int r, c;
		int where = my < HEAD_H && mx >= RW ? 1 : mx < RW && my >= HEAD_H ? 2 : 0;
		if (cellAt (iclamp (mx, RW, width - SB - 1), iclamp (my, HEAD_H, height - SB - 1), &r, &c))
		{
			Rect sl = sel ();
			if (where == 1 && !(m_wholeCols && c >= sl.c0 && c <= sl.c1)) selectCols (c, c);
			else if (where == 2 && !(m_wholeRows && r >= sl.r0 && r <= sl.r1)) selectRows (r, r);
			else if (where == 0 && !rect_has (sl, r, c)) selectCell (r, c, false);
		}
		if (onContext) onContext (mx, my, where);
		return true;
	}
	if (!br) m_rdown = false;
	if (!bl) { pressed = false; return true; }
	if (pressed) return true;
	pressed = true;
	setFocus ();
	unsigned now = kapi_get_ticks ();
	// the scroll bars
	if (mx >= width - SB && my >= HEAD_H) { m_drag = D_VBAR; catchOutside = true; return onMouse (mx, my, bl, 0, 0, 0); }
	if (my >= height - SB && mx >= RW) { m_drag = D_HBAR; catchOutside = true; return onMouse (mx, my, bl, 0, 0, 0); }
	// the corner: all
	if (mx < RW && my < HEAD_H) { if (ed.on && !commit (0, 0)) return true; selectAll (); return true; }
	// a chart (its handles size it)
	if (!ed.on)
	{
		int ci = chartAt (mx, my);
		if (ci >= 0)
		{
			Chart *ch = s->charts[ci];
			int x, y, w, h; chartBox (ch, &x, &y, &w, &h, paneAt (mx, my));
			bool dbl = ci == selChart && now - m_lastClick < 40;
			m_lastClick = now;
			if (dbl) { if (onChartOpen) onChartOpen (ci); pressed = false; return true; }
			selChart = ci;
			m_dragX0 = mx; m_dragY0 = my; m_chartX0 = ch->x; m_chartY0 = ch->y; m_chartW0 = ch->w; m_chartH0 = ch->h; m_moved = false;
			int cx = mx < x + 8 ? 0 : mx > x + w - 8 ? 1 : -1, cy = my < y + 8 ? 0 : my > y + h - 8 ? 2 : -1;
			if (cx >= 0 && cy >= 0) { m_drag = D_CHARTSIZE; m_corner = cx | cy; }
			else m_drag = D_CHART;
			catchOutside = true;
			changed ();
			return true;
		}
	}
	int r, c;
	// the column letters: select (drag: several), or their edge: size
	if (my < HEAD_H)
	{
		PaneView pv[4]; panes (pv);
		for (int p = 2; p <= 3; p++)
			for (int i = 0; i < pv[p].nc; i++)
			{
				int e = pv[p].col[i].at + pv[p].col[i].len;
				if (abs (mx - e) <= 3)
				{
					int cc = pv[p].col[i].i;
					if (now - m_lastClick < 40 && m_lastC == -2 - cc)			// a double click: fitted
					{
						if (onContext) { m_fitCol = cc; onContext (0, 0, 7); }
						m_lastClick = 0; pressed = false; return true;
					}
					m_lastClick = now; m_lastC = -2 - cc;
					m_drag = D_COLSIZE; m_dragI = cc; m_dragV = pv[p].col[i].len; m_dragX0 = mx; catchOutside = true;
					if (onContext) onContext (0, 0, 8);			// (the app keeps an undo step)
					return true;
				}
			}
		if (ed.on && !commit (0, 0)) return true;
		if (cellAt (iclamp (mx, RW, width - SB - 1), HEAD_H + 1, &r, &c))
		{
			if (mods & MOD_SHIFT) { Rect sl = sel (); selectCols (imin (sl.c0, c) == c ? sl.c1 : sl.c0, c); }
			else selectCols (c, c);
			m_drag = D_COLSEL; catchOutside = true;
		}
		return true;
	}
	if (mx < RW)
	{
		PaneView pv[4]; panes (pv);
		for (int p = 1; p <= 3; p += 2)
			for (int i = 0; i < pv[p].nr; i++)
			{
				int e = pv[p].row[i].at + pv[p].row[i].len;
				if (abs (my - e) <= 2)
				{
					int rr = pv[p].row[i].i;
					if (now - m_lastClick < 40 && m_lastR == -2 - rr)
					{
						if (onContext) { m_fitRow = rr; onContext (0, 0, 9); }
						m_lastClick = 0; pressed = false; return true;
					}
					m_lastClick = now; m_lastR = -2 - rr;
					m_drag = D_ROWSIZE; m_dragI = rr; m_dragV = pv[p].row[i].len; m_dragY0 = my; catchOutside = true;
					if (onContext) onContext (0, 0, 8);
					return true;
				}
			}
		if (ed.on && !commit (0, 0)) return true;
		if (cellAt (RW + 1, my, &r, &c))
		{
			if (mods & MOD_SHIFT) { Rect sl = sel (); selectRows (sl.r0, r); }
			else selectRows (r, r);
			m_drag = D_ROWSEL; catchOutside = true;
		}
		return true;
	}
	if (!cellAt (mx, my, &r, &c)) return true;
	// while a formula waits for a reference: point at the cell (drag: a range)
	if (ed.on && ed.can_point ())
	{
		pointAt (r, c, r, c);
		m_drag = D_POINT; catchOutside = true;
		return true;
	}
	if (ed.on)
	{
		if (ed.r == r && ed.c == c && book_sheet_by_id (*b, ed.sheetId) == s) { ed.typed = false; changed (); return true; }	// (a click in the cell: edit)
		if (!commit (0, 0)) return true;
	}
	// the fill handle
	{
		Rect sl = sel ();
		int x0, y0, w0, h0;
		if (!m_wholeCols && !m_wholeRows && cellBox (sl.r1, sl.c1, &x0, &y0, &w0, &h0))
		{
			int hx = x0 + w0 - 1, hy = y0 + h0 - 1;
			if (abs (mx - hx) <= 4 && abs (my - hy) <= 4) { m_drag = D_FILL; fillTo.r1 = fillTo.r0 - 1; catchOutside = true; return true; }
		}
	}
	bool dbl = now - m_lastClick < 40 && r == m_lastR && c == m_lastC;
	m_lastClick = now; m_lastR = r; m_lastC = c;
	if (dbl) { selectCell (r, c, false); beginEdit (false, 0); pressed = false; return true; }
	selectCell (r, c, (mods & MOD_SHIFT) != 0);
	m_drag = D_SELECT; catchOutside = true;
	return true;
}

} // namespace ss

#endif
