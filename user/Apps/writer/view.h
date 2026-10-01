//
// view.h -- Writer's page view: the pages laid out on a grey desk, one under the other (the
// document drawn by Writer itself -- FreeType's glyphs, the highlights, the selection, the lists'
// markers, the tables' shading and lines, the fields shaded, the tab stops' leaders, each page's
// header and footer), the caret blinking, the scroll bars; the mouse (a click places the caret, a
// drag selects, a double click a word, a triple click the paragraph, Shift+click extends; a double
// click on a header or a footer edits it -- the body greyed --, on the body goes back to it; a
// table's column border dragged; the wheel scrolls; a right click asks the app for its menu) and the
// keys (typing, the arrows -- Ctrl: by word / paragraph, Shift: selecting; in a table, Up / Down from
// cell to cell --, Home / End -- Ctrl: the document's --, Page Up / Down, Enter -- Shift: a line
// break --, Tab -- in a list: its level; in a table: the next cell --, Backspace / Delete -- Ctrl:
// a word --, Esc: out of a header / footer).
//
#ifndef _writer_view_h
#define _writer_view_h

#include "edit.h"
#include "pdf/pdfwrite.h"

namespace wr {

using namespace wtk;

enum { GAP = 22, SBW = 13 };

// File > Export as PDF: the pages drawn again into a PDF (pdf/pdfwrite.h) instead of the screen -- at 100 % (96 px an
// inch: a px is 0.75 point), each page's top left at (0, 0); what is only for the screen (the crop marks, a table's grid
// without lines, the selection, the formatting marks) left out.
static pdfw::Writer *g_pdf;
static bool g_pdfJpeg;				// the images as JPEG (a smaller file)
static const float PX2PT = 0.75f;

static unsigned C_DESK () { return wk_mix (wk_tone (C_BG, 92), 0x7A7E86, 110); }
static unsigned C_SELECT () { return wk_mix (0xFFFFFF, C_ACCENT, 92); }

static void (*g_onContext) (int x, int y);	// the app's: a right click at (x, y) of the view
static bool g_showMarks;			// View > Formatting Marks: the spaces, tabs, line breaks, paragraphs' ends shown

class PageView : public Widget
{
public:
	int sx, sy;				// scrolled (px)
	PageView (int l, int t, int w, int h) : Widget (l, t, w, h), sx (0), sy (0),
		m_bl (false), m_drag (0), m_clicks (0), m_lastClick (0), m_lastX (-99), m_lastY (-99), m_mx (0), m_my (0),
		m_caretOn (true), m_caretDrawn (false), m_blinkT (0), m_cw (0), m_ch (0), m_barHot (0)
	{ canFocus = true; }

	int viewW () const { return width - SBW; }
	int viewH () const { return height - (hbar () ? SBW : 0); }
	bool hbar () const { return L.pageW + 2 * GAP > width - SBW; }
	int docH () const { return L.npages * (L.pageH + GAP) + GAP; }
	int docW () const { return L.pageW + 2 * GAP; }
	int originX () const { int vw = viewW (); return vw > L.pageW + 2 * GAP ? (vw - L.pageW) / 2 : GAP - sx; }
	int pageTop (int i) const { return GAP + i * (L.pageH + GAP) - sy; }
	int pageAt (int y) const { int p = y + sy - GAP < 0 ? 0 : (y + sy - GAP) / (L.pageH + GAP); return wclamp (p, 0, L.npages - 1); }

	void relayout ()
	{
		if (g_relayout) { layout_all (); g_relayout = false; }
		clampScroll ();
	}
	void clampScroll ()
	{
		sy = wclamp (sy, 0, wmax (0, docH () - viewH ()));
		sx = wclamp (sx, 0, wmax (0, docW () - viewW ()));
	}
	// The story edited laid out where it shows (a header / footer: on the page it is edited on).
	void placeStory () { if (L.d->cur != SY_BODY) hf_place (L.d->cur, L.hfPage); }

	// ---- places <-> the screen ----
	void caretBox (Pos c, bool atEnd, int *x, int *y, int *h, int *base = 0)
	{
		placeStory ();
		const Para *q = L.d->p[c.p];
		int l = line_of (q, c.o, atEnd);
		const Line &ln = q->ln[l];
		int top = (pageTop (ln.page) << 6) + ln.py64;
		*x = ((originX () << 6) + q->x64 + x_of (q, c.o, atEnd)) >> 6;
		// the caret's font's height, on the baseline
		int cf = c.o > 0 ? q->cf[c.o - 1] : q->len ? q->cf[0] : q->endCf;
		if (!has_sel () && g_typeCf >= 0) cf = g_typeCf;
		int raise = 0;
		fnt::Font *f = cf_font (cf, &raise);
		int b64 = top + ln.base64 + raise;
		int a = f ? f->ascent : ln.base64, dsc = f ? f->descent : ln.h64 - ln.base64;
		*y = (b64 - a) >> 6; *h = ((b64 + dsc) >> 6) - *y;
		if (*h < 4) *h = 4;
		if (base) *base = b64 >> 6;
	}
	// The place under (x, y) of the view, in the story edited (atEnd: the end of a line).
	Pos hit (int x, int y, bool *atEnd)
	{
		*atEnd = false;
		int page = pageAt (y);
		int x64 = (x - originX ()) << 6, y64 = (y - pageTop (page)) << 6;
		return L.d->cur == SY_BODY ? hitBody (page, x64, y64, atEnd) : hitStory (x64, y64, atEnd);
	}

	// Scroll the caret into view.
	void ensureVisible ()
	{
		relayout ();
		int x, y, h;
		caretBox (g_caret, g_atEnd, &x, &y, &h);
		int vh = viewH (), vw = viewW ();
		if (y < 8) sy += y - 8 - vh / 6;
		else if (y + h > vh - 8) sy += y + h - (vh - 8) + vh / 6;
		if (hbar ())
		{
			if (x < 16) sx += x - 16 - vw / 4;
			else if (x > vw - 16) sx += x - (vw - 16) + vw / 4;
		}
		clampScroll ();
	}

	// The caret shown again at once (after a key, a click).
	void wake () { m_caretOn = true; m_blinkT = kapi_get_ticks (); invalidate (true); }

	// Blinking; dragging past the edge scrolls.
	void tick ()
	{
		unsigned t = kapi_get_ticks ();
		if (m_drag == 1 && (m_my < 0 || m_my >= viewH ()))
		{
			int d = m_my < 0 ? m_my : m_my - viewH ();
			sy += wclamp (d / 2, -40, 40) + (d < 0 ? -2 : 2);
			clampScroll ();
			dragTo (m_mx, wclamp (m_my, 0, viewH () - 1));
			return;
		}
		if (!hasFocus || has_sel ()) return;
		if (t - m_blinkT >= 53)
		{
			m_blinkT = t;
			m_caretOn = !m_caretOn;
			if (m_caretOn) drawCaret (); else eraseCaret ();
			invalidate (false);
		}
	}

	// a page's contents: its header and footer, the body's tables and lines (the screen's, or the PDF's: g_pdf)
	void paintPage (int pg, int ox, int pt, int vh, Para **bp, bool inBody)
	{
		// the header and the footer (greyed while the body is edited)
		for (int f = 0; f < 2; f++)
		{
			int s = hf_story (f != 0, pg);
			if (story_empty (*L.d, s) && L.d->cur != s) continue;
			hf_place (s, pg);
			int sn; Para **sp = story_p (*L.d, s, &sn);
			bool cur = L.d->cur == s;
			for (int i = 0; i < sn; i++) for (int l = 0; l < sp[i]->nln; l++) drawLine (sp[i], i, l, ox, pt, vh, 0, !cur, cur && pg == L.hfPage);
		}
		// the body: the tables' shading, the lines, the tables' lines (greyed while a header is edited)
		for (int k = 0; k < L.nruns; k++) drawTable (L.runs[k], pg, ox, pt, true);
		int g1 = pg + 1 < L.npages ? L.pageFirst[pg + 1] : L.nall;
		for (int g = L.pageFirst[pg]; g < g1; g++) drawLine (bp[L.all[g].p], L.all[g].p, L.all[g].l, ox, pt, vh, 0, !inBody, inBody);
		for (int k = 0; k < L.nruns; k++)
		{
			const TRun &run = L.runs[k];
			int dy;
			if (repeatAt (run.t, pg, &dy))			// (the heading row again on this page)
				for (int i = run.p0; i < run.p1; i++)
					if (bp[i]->pf.row == 0) for (int l = 0; l < bp[i]->nln; l++) drawLine (bp[i], i, l, ox, pt, vh, dy, !inBody, false);
			drawTable (run, pg, ox, pt, false);
		}
	}
	// the pages from..to (from 0) into a PDF: each a page of it
	void exportPages (pdfw::Writer &w, int from, int to)
	{
		g_pdf = &w;
		int n; Para **bp = story_p (*L.d, SY_BODY, &n);
		for (int pg = from; pg <= to && pg < L.npages; pg++)
		{
			w.begin_page (L.d->page.w / 20.0f, L.d->page.h / 20.0f);
			paintPage (pg, 0, 0, 1 << 28, bp, true);
			w.end_page ();
		}
		g_pdf = 0;
	}
	void onDraw () override
	{
		relayout ();
		m_caretDrawn = false;
		int vh = viewH ();
		canvas.fillRect (0, 0, width, height, C_DESK ());
		int ox = originX ();
		unsigned shadow = wk_mix (C_DESK (), 0, 60);
		bool inBody = L.d->cur == SY_BODY;
		int n; Para **bp = story_p (*L.d, SY_BODY, &n);
		for (int pg = 0; pg < L.npages; pg++)
		{
			int pt = pageTop (pg);
			if (pt > vh) break;
			if (pt + L.pageH < 0) continue;
			fillClip (ox + 3, pt + 3, L.pageW, L.pageH, shadow);
			fillClip (ox - 1, pt - 1, L.pageW + 2, L.pageH + 2, wk_mix (C_DESK (), 0, 90));
			fillClip (ox, pt, L.pageW, L.pageH, 0xFFFFFF);
			cropMarks (ox, pt);
			paintPage (pg, ox, pt, vh, bp, inBody);
			if (!inBody) hfFrame (pg, ox, pt);
		}
		placeStory ();
		if (m_drag == 4) { frame (m_rsX, m_rsY, m_rsW, m_rsH, C_ACCENT); frame (m_rsX + 1, m_rsY + 1, m_rsW - 2, m_rsH - 2, 0xFFFFFF); }
		if (m_drag == 5) for (int y = 0; y < vh; y += 4) fillClip (m_colX, y, 1, 2, C_ACCENT);
		drawBars ();
		if (m_caretOn && hasFocus && !has_sel ()) drawCaret ();
		fnt::trim ();					// (a safe moment: no font held)
	}

	bool onMouse (int mx, int my, int bl, int br, int, int wheel) override
	{
		if (mx < 0 && m_drag == 0) { if (m_barHot) { m_barHot = 0; invalidate (true); } m_bl = false; return false; }
		m_mx = mx; m_my = my;
		if (wheel)
		{
			sy -= wheel * 40;
			clampScroll ();
			invalidate (true);
			return true;
		}
		bool press = bl && !m_bl;
		m_bl = bl != 0;
		if (!bl && m_drag == 5)						// a column's border let go
		{
			m_drag = 0; catchOutside = false;
			if (m_colRun < L.nruns)
			{
				const TRun &run = L.runs[m_colRun];
				int xTw = px642tw (((m_colX - originX ()) << 6) - run.t->x64);
				ed_table_col_edge (run.p0, m_colIdx, xTw);
				ensureVisible ();
			}
			invalidate (true);
			return true;
		}
		if (!bl) { if (m_drag) { m_drag = 0; catchOutside = false; } }
		int hot = mx >= viewW () && my < viewH () ? 1 : hbar () && my >= viewH () && mx < viewW () ? 2 : 0;
		if (hot != m_barHot && !m_drag) { m_barHot = hot; invalidate (true); }
		if (m_drag == 2 || m_drag == 3)				// a scroll bar's thumb dragged
		{
			if (m_drag == 2) { int th = thumbV (); sy = (int) wk_thumb_pos (my, viewH (), docH (), viewH (), th); }
			else { int th = thumbH (); sx = (int) wk_thumb_pos (mx, viewW (), docW (), viewW (), th); }
			clampScroll (); invalidate (true);
			return true;
		}
		if (m_drag == 5) { m_colX = wclamp (mx, 0, viewW () - 1); invalidate (true); return true; }
		if (press && hot)
		{
			m_drag = hot == 1 ? 2 : 3; catchOutside = true;
			return onMouse (mx, my, bl, br, 0, 0);
		}
		if (br && !bl && mx < viewW () && my < viewH ())
		{
			setFocus ();
			bool ae; Pos p = hit (mx, my, &ae);
			if (!has_sel () || p < sel_a () || sel_b () < p) { set_caret (p, false, ae); wake (); }
			if (g_onContext) g_onContext (mx, my);
			return true;
		}
		if (m_drag == 4)						// an image's corner dragged
		{
			int w = wmax (12, mx - m_rsX);
			m_rsW = w; m_rsH = wmax (8, (int) ((long long) w * m_rsH0 / wmax (1, m_rsW0)));
			if (!bl)
			{
				m_drag = 0; catchOutside = false;
				CfChange c; c.what = CH_OBJSIZE; c.ow = px642tw (m_rsW << 6); c.oh = px642tw (m_rsH << 6);
				ed_format (c);
				ensureVisible ();
			}
			invalidate (true);
			return true;
		}
		if (press)
		{
			Pos at; int ix, iy, iw, ih;
			if (selectedImage (&at) && imageAt (mx, my, &at, &ix, &iy, &iw, &ih) && mx >= ix + iw - 6 && my >= iy + ih - 6)
			{
				m_drag = 4; catchOutside = true;
				m_rsX = ix; m_rsY = iy; m_rsW = m_rsW0 = iw; m_rsH = m_rsH0 = ih;
				return true;
			}
			if (imageAt (mx, my, &at, &ix, &iy, &iw, &ih))		// a click on an image: it is selected
			{
				setFocus ();
				set_caret (at, false); set_caret (mkpos (at.p, at.o + 1), true);
				m_clicks = 0; m_lastClick = 0;
				wake ();
				return true;
			}
			if (L.d->cur == SY_BODY && colBorderAt (mx, my, &m_colRun, &m_colIdx))	// a table's column border
			{
				setFocus ();
				m_drag = 5; catchOutside = true; m_colX = mx;
				invalidate (true);
				return true;
			}
		}
		if (press)
		{
			setFocus ();
			unsigned t = kapi_get_ticks ();
			bool again = t - m_lastClick < 45 && mx - m_lastX < 5 && m_lastX - mx < 5 && my - m_lastY < 5 && m_lastY - my < 5;
			m_clicks = again ? m_clicks % 3 + 1 : 1;
			m_lastClick = t; m_lastX = mx; m_lastY = my;
			int page = pageAt (my), y64 = (my - pageTop (page)) << 6;
			int zone = y64 < body_top (page) ? 1 : y64 >= body_bot (page) ? 2 : 0;	// (1 the header's, 2 the footer's)
			if (L.d->cur == SY_BODY && zone && m_clicks == 2)		// a double click on a header / footer: edited
			{ L.hfPage = page; ed_story (hf_story (zone == 2, page)); m_clicks = 1; }
			else if (L.d->cur != SY_BODY)
			{
				if (zone)
				{
					int s = hf_story (zone == 2, page);
					L.hfPage = page;
					if (s != L.d->cur) { ed_story (s); m_clicks = 1; }
				}
				else if (m_clicks == 2) { ed_story (SY_BODY); m_clicks = 1; }	// a double click on the body: back to it
				else { wake (); return true; }					// (a click on the body: nothing)
			}
			bool ae; Pos p = hit (mx, my, &ae);
			bool shift = (kapi_get_modifiers () & MOD_SHIFT) != 0;
			if (m_clicks == 2) { Pos a, b; word_at (p, a, b); set_caret (a, false); set_caret (b, true); m_selA = a; m_selB = b; }
			else if (m_clicks == 3) { Pos a = mkpos (p.p, 0), b = mkpos (p.p, L.d->p[p.p]->len); set_caret (a, false); set_caret (b, true); m_selA = a; m_selB = b; }
			else { set_caret (p, shift, ae); m_selA = m_selB = has_sel () ? g_anchor : p; if (shift) m_selA = m_selB = g_anchor; }
			m_drag = 1; catchOutside = true;
			g_goalX = -1;
			wake ();
			return true;
		}
		if (m_drag == 1 && bl) { dragTo (mx, my); return true; }
		return mx >= 0;
	}

	bool onKey (long k) override
	{
		unsigned mods = kapi_get_modifiers ();
		bool shift = (mods & MOD_SHIFT) != 0, ctrl = (mods & MOD_CTRL) != 0;
		Pos c = g_caret;
		switch (k)
		{
		case KEY_LEFT:
			if (has_sel () && !shift) { set_caret (sel_a (), false); break; }
			set_caret (ctrl ? word_left (c) : pos_left (c), shift);
			break;
		case KEY_RIGHT:
			if (has_sel () && !shift) { set_caret (sel_b (), false); break; }
			set_caret (ctrl ? word_right (c) : pos_right (c), shift);
			break;
		case KEY_UP: case KEY_DOWN:
			if (ctrl)
			{
				Pos p = c;
				if (k == KEY_UP) { if (p.o > 0) p.o = 0; else if (p.p > 0) p = mkpos (p.p - 1, 0); }
				else if (p.p < L.d->n - 1) p = mkpos (p.p + 1, 0); else p.o = L.d->p[p.p]->len;
				set_caret (p, shift);
			}
			else verticalMove (k == KEY_UP ? -1 : 1, shift);
			break;
		case KEY_HOME:
			if (ctrl) set_caret (mkpos (0, 0), shift);
			else { const Para *q = L.d->p[c.p]; set_caret (mkpos (c.p, q->ln[line_of (q, c.o, g_atEnd)].start), shift); }
			break;
		case KEY_END:
			if (ctrl) set_caret (doc_end (*L.d), shift);
			else
			{
				const Para *q = L.d->p[c.p]; int l = line_of (q, c.o, g_atEnd); const Line &ln = q->ln[l];
				int e = ln.end;
				bool last = l == q->nln - 1;
				if (!last && e > ln.start && (q->ch[e - 1] == ' ' || q->ch[e - 1] == 0x0B)) { e--; last = true; }
				set_caret (mkpos (c.p, e), shift, !last);
			}
			break;
		case KEY_PGUP: case KEY_PGDN: pageMove (k == KEY_PGUP ? -1 : 1, shift); break;
		case KEY_ENTER:
			if (shift) { unsigned lb = 0x0B; ed_type (&lb, 1); }
			else ed_enter ();
			break;
		case KEY_TAB:
			if (ctrl) { ed_toggle (CF_ITALIC); break; }	// (^I is Tab's code: Ctrl held, italic)
			if (in_table (L.d->p[g_caret.p]) && L.d->cur == SY_BODY) { ed_cell_tab (shift); break; }
			if (L.d->p[sel_a ().p]->pf.list != LS_NONE && (sel_a ().o == 0 || sel_a ().p != sel_b ().p)) { ed_para (pf_indent, shift ? -1 : 1); break; }
			if (sel_a ().p != sel_b ().p) { ed_para (pf_indent, shift ? -1 : 1); break; }
			{ unsigned t = '\t'; ed_type (&t, 1, ED_TYPE); }
			break;
		case KEY_BACKSPACE: ed_delete (false, ctrl); break;
		case KEY_DEL: ed_delete (true, ctrl); break;
		case 27:
			if (has_sel ()) set_caret (g_caret, false);
			else if (L.d->cur != SY_BODY) ed_story (SY_BODY);
			break;
		default:
			if ((k >= 32 && k < 127) || (k >= 0xA0 && k < 0x100) || (k > KEY_F12 && k < 0x110000 && k != 0x7F))
			{ unsigned ch = (unsigned) k; ed_type (&ch, 1, ED_TYPE); }
			else return false;
		}
		ensureVisible ();
		wake ();
		return true;
	}

private:
	bool m_bl; int m_drag;			// 1 selecting, 2 / 3 a scroll bar's thumb, 4 an image's corner, 5 a column's border
	int m_clicks; unsigned m_lastClick; int m_lastX, m_lastY, m_mx, m_my;
	Pos m_selA, m_selB;			// a double / triple click's word / paragraph (the drag extends it)
	bool m_caretOn, m_caretDrawn; unsigned m_blinkT;
	int m_cx, m_cy, m_cw, m_ch; unsigned m_under[4 * 512];
	int m_barHot;
	int m_colRun, m_colIdx, m_colX;		// a column's border dragged: its table (L.runs), the column it ends, where it is

	// ---- hit-testing ----
	static long vdist (int y64, const Line &ln) { return y64 < ln.py64 ? ln.py64 - y64 : y64 >= ln.py64 + ln.h64 ? y64 - ln.py64 - ln.h64 + 1 : 0; }
	// In a header / footer: its nearest line.
	Pos hitStory (int x64, int y64, bool *atEnd)
	{
		placeStory ();
		int bp = 0, bl = 0; long best = -1;
		for (int i = 0; i < L.d->n; i++)
			for (int l = 0; l < L.d->p[i]->nln; l++)
			{
				long dd = vdist (y64, L.d->p[i]->ln[l]);
				if (best < 0 || dd < best) { best = dd; bp = i; bl = l; }
			}
		const Para *q = L.d->p[bp];
		return mkpos (bp, offset_at_x (q, bl, x64 - q->x64, atEnd));
	}
	// The line of cell (row, x) of a table (its first line going down, its last going up).
	bool cellLine (const TRun &run, int row, int x64, int dir, int y64, Pos *np, bool *ae)
	{
		const Table *t = run.t;
		int n; Para **p = story_p (*L.d, SY_BODY, &n);
		int cx = x64 - t->x64, c = 0;
		while (c < t->ncols - 1 && cx >= t->colX64[c + 1]) c++;
		int orow, ocol; cell_owner (t, wclamp (row, 0, t->nrows - 1), c, &orow, &ocol);
		int best = -1, bl = 0; long bd = 0;
		for (int i = run.p0; i < run.p1; i++)
		{
			if (p[i]->pf.row != orow || p[i]->pf.col != ocol) continue;
			for (int l = 0; l < p[i]->nln; l++)
			{
				bool take;					// (down: its first line; up: its last; else the nearest)
				if (dir > 0) take = best < 0;
				else if (dir < 0) take = true;
				else { long dd = vdist (y64, p[i]->ln[l]); take = best < 0 || dd < bd; if (take) bd = dd; }
				if (take) { best = i; bl = l; }
			}
		}
		if (best < 0) return false;
		*np = mkpos (best, offset_at_x (p[best], bl, x64 - p[best]->x64, ae));
		return true;
	}
	int runOf (int pi) const { for (int k = 0; k < L.nruns; k++) if (pi >= L.runs[k].p0 && pi < L.runs[k].p1) return k; return -1; }
	// In the body: in a table's row, the cell under x (the nearest); else the page's nearest line.
	Pos hitBody (int page, int x64, int y64, bool *atEnd)
	{
		int n; Para **p = story_p (*L.d, SY_BODY, &n);
		for (int k = 0; k < L.nruns; k++)
		{
			const Table *t = L.runs[k].t;
			for (int r = 0; r < t->nrows; r++)
			{
				if (t->rowPage[r] != page || y64 < t->rowY64[r] || y64 >= t->rowY64[r] + t->rowH64[r]) continue;
				Pos np;
				if (cellLine (L.runs[k], r, x64, 0, y64, &np, atEnd)) return np;
			}
		}
		int g0 = L.pageFirst[page], g1 = page + 1 < L.npages ? L.pageFirst[page + 1] : L.nall;
		if (g0 >= g1) { g0 = wclamp (g0 - 1, 0, L.nall - 1); g1 = g0 + 1; }
		int best = g0; long bd = -1;
		int padX = tw2px64 (CELL_PAD_X);
		for (int g = g0; g < g1; g++)
		{
			const Para *q = p[L.all[g].p];
			long dd = vdist (y64, q->ln[L.all[g].l]) * 4;
			if (in_table (q))
			{
				int x0 = q->x64 - padX, x1 = q->x64 + q->w64 + padX;
				dd += x64 < x0 ? x0 - x64 : x64 > x1 ? x64 - x1 : 0;
			}
			if (bd < 0 || dd < bd) { bd = dd; best = g; }
		}
		const Para *q = p[L.all[best].p];
		return mkpos (L.all[best].p, offset_at_x (q, L.all[best].l, x64 - q->x64, atEnd));
	}
	// A table's column border under (x, y): its table (L.runs), the column it ends.
	bool colBorderAt (int x, int y, int *run, int *col)
	{
		int page = pageAt (y), ox = originX ();
		int y64 = (y - pageTop (page)) << 6;
		for (int k = 0; k < L.nruns; k++)
		{
			const Table *t = L.runs[k].t;
			for (int r = 0; r < t->nrows; r++)
			{
				if (t->rowPage[r] != page || y64 < t->rowY64[r] || y64 >= t->rowY64[r] + t->rowH64[r]) continue;
				for (int c = 1; c <= t->ncols; c++)
				{
					int bx = ox + ((t->x64 + t->colX64[c]) >> 6);
					if (x < bx - 2 || x > bx + 2) continue;
					if (c < t->ncols) { int orow, ocol; cell_owner (t, r, c, &orow, &ocol); if (ocol < c) continue; }	// (inside a merged cell)
					*run = k; *col = c - 1;
					return true;
				}
			}
		}
		return false;
	}

	void dragTo (int mx, int my)
	{
		bool ae; Pos p = hit (mx, my, &ae);
		if (m_clicks >= 2)				// (by words / paragraphs)
		{
			Pos a, b;
			if (m_clicks == 2) word_at (p, a, b); else { a = mkpos (p.p, 0); b = mkpos (p.p, L.d->p[p.p]->len); }
			if (p < m_selA) { g_anchor = m_selB; set_caret (a, true); }
			else { g_anchor = m_selA; set_caret (b, true); }
		}
		else set_caret (p, true, ae);
		invalidate (true);
	}

	// ---- Up / Down ----
	// The line after / before (p, l) in the story edited.
	bool lineStep (int p, int l, int dir, int *tp, int *tl)
	{
		if (dir > 0) { if (l + 1 < L.d->p[p]->nln) { *tp = p; *tl = l + 1; return true; } if (p + 1 >= L.d->n) return false; *tp = p + 1; *tl = 0; return true; }
		if (l > 0) { *tp = p; *tl = l - 1; return true; }
		if (p == 0) return false;
		*tp = p - 1; *tl = L.d->p[p - 1]->nln - 1;
		return true;
	}
	// From a cell's first / last line: the cell above / below (the column under the goal), else out of
	// the table.
	bool tableStep (int pi, int dir, int gx, Pos *np, bool *ae)
	{
		int k = runOf (pi);
		if (k < 0) return false;
		const TRun &run = L.runs[k];
		const Para *q = L.d->p[pi];
		int r = q->pf.row, nr = dir > 0 ? r + tcell (run.t, r, q->pf.col).rs : r - 1;
		if (nr >= 0 && nr < run.t->nrows) return cellLine (run, nr, gx, dir, 0, np, ae);
		int tp = dir > 0 ? run.p1 : run.p0 - 1;
		if (tp < 0 || tp >= L.d->n) return false;
		return lineIn (tp, dir, gx, np, ae);
	}
	// Paragraph tp's first line (going down) or last (going up) at the goal -- a table's: its cell
	// under the goal in its first / last row.
	bool lineIn (int tp, int dir, int gx, Pos *np, bool *ae)
	{
		int k = runOf (tp);
		if (k >= 0 && L.d->cur == SY_BODY) return cellLine (L.runs[k], dir > 0 ? 0 : L.runs[k].t->nrows - 1, gx, dir, 0, np, ae);
		const Para *q = L.d->p[tp];
		int l = dir > 0 ? 0 : q->nln - 1;
		*np = mkpos (tp, offset_at_x (q, l, gx - q->x64, ae));
		return true;
	}
	void verticalMove (int dir, bool shift)
	{
		relayout ();
		placeStory ();
		Pos c = g_caret;
		const Para *q = L.d->p[c.p];
		int l = line_of (q, c.o, g_atEnd);
		if (g_goalX < 0) g_goalX = q->x64 + x_of (q, c.o, g_atEnd);
		int gx = g_goalX;
		bool ae = false; Pos np;
		if (L.d->cur == SY_BODY && in_table (q))
		{
			int ca, cb; cell_span (*L.d, c.p, &ca, &cb);
			bool edge = dir > 0 ? c.p == cb - 1 && l == q->nln - 1 : c.p == ca && l == 0;
			if (edge)
			{
				if (tableStep (c.p, dir, gx, &np, &ae)) set_caret (np, shift, ae);
				else set_caret (dir < 0 ? mkpos (0, 0) : doc_end (*L.d), shift);
				g_goalX = gx;
				return;
			}
		}
		int tp, tl;
		if (!lineStep (c.p, l, dir, &tp, &tl)) set_caret (dir < 0 ? mkpos (0, 0) : doc_end (*L.d), shift);
		else if (tp != c.p && in_table (L.d->p[tp]) && L.d->cur == SY_BODY && !(in_table (q) && same_cell (q, L.d->p[tp])))
		{ if (lineIn (tp, dir, gx, &np, &ae)) set_caret (np, shift, ae); }
		else { const Para *tq = L.d->p[tp]; int o = offset_at_x (tq, tl, gx - tq->x64, &ae); set_caret (mkpos (tp, o), shift, ae); }
		g_goalX = gx;
	}
	void pageMove (int dir, bool shift)
	{
		relayout ();
		int x, y, h;
		caretBox (g_caret, g_atEnd, &x, &y, &h);
		int d = viewH () - 40;
		const Para *q = L.d->p[g_caret.p];
		int gx = g_goalX >= 0 ? g_goalX : q->x64 + x_of (q, g_caret.o, g_atEnd);
		sy += dir * d; clampScroll ();
		bool ae; Pos p = hit (x, wclamp (y + h / 2, 0, viewH () - 1), &ae);
		set_caret (p, shift, ae);
		g_goalX = gx;
	}

	// ---- drawing ----
	void fillClip (int x, int y, int w, int h, unsigned c)
	{
		if (g_pdf) { g_pdf->fill_rect (x * PX2PT, y * PX2PT, w * PX2PT, h * PX2PT, c); return; }
		int x1 = wmin (x + w, viewW ()), y1 = wmin (y + h, viewH ());
		x = wmax (x, 0); y = wmax (y, 0);
		if (x1 > x && y1 > y) canvas.fillRect (x, y, x1 - x, y1 - y, c);
	}
	void cropMarks (int ox, int pt)
	{
		if (g_pdf) return;
		unsigned c = 0xC8C8C8;
		int x0 = ox + (L.ml64 >> 6), y0 = pt + (L.mt64 >> 6), x1 = ox + L.pageW - (L.mr64 >> 6), y1 = pt + L.pageH - (L.mb64 >> 6);
		int m = wmin (14, wmin (L.ml64 >> 6, L.mt64 >> 6) - 2);
		if (m < 4) return;
		fillClip (x0 - m, y0 - 1, m, 1, c); fillClip (x0 - 1, y0 - m, 1, m, c);
		fillClip (x1, y0 - 1, m, 1, c); fillClip (x1, y0 - m, 1, m, c);
		fillClip (x0 - m, y1, m, 1, c); fillClip (x0 - 1, y1, 1, m, c);
		fillClip (x1, y1, m, 1, c); fillClip (x1, y1, 1, m, c);
	}
	// While a header / footer is edited: the edges of the page's header and footer, their names.
	void hfFrame (int pg, int ox, int pt)
	{
		for (int f = 0; f < 2; f++)
		{
			int s = hf_story (f != 0, pg);
			int y = pt + ((f ? body_bot (pg) : body_top (pg)) >> 6);
			unsigned c = s == L.d->cur ? C_ACCENT : wk_mix (0xFFFFFF, C_ACCENT, 120);
			for (int x = ox + 2; x < ox + L.pageW - 2; x += 6) fillClip (x, y, 3, 1, c);
			const char *nm = STORY_NAMES[s];
			int tw = wk_text_w (nm) + 12, th = 18, tx = ox + (L.ml64 >> 6), ty = f ? y - th : y + 1;
			if (ty < 0 || ty + th > viewH () || tx + tw > viewW ()) continue;
			canvas.fillRect (tx, ty, tw, th, wk_mix (0xFFFFFF, C_ACCENT, 50));
			canvas.text (tx + 6, ty + 2, nm, s == L.d->cur ? C_ACCENT : 0x707070);
		}
	}
	// A table's heading row drawn again on page pg (it runs on from an earlier one): its shift.
	static bool repeatAt (const Table *t, int pg, int *dy)
	{
		if (!t->header || t->nrows < 2 || t->rowPage[0] >= pg) return false;
		for (int r = 1; r < t->nrows; r++)
			if (t->rowPage[r] == pg)
			{
				if (t->rowY64[r] < body_top (pg) + t->rowH64[0]) return false;
				*dy = body_top (pg) - t->rowY64[0];
				return true;
			}
		return false;
	}
	// A table's rows on page pg: their cells' shading (fills), or their lines.
	void drawTable (const TRun &run, int pg, int ox, int pt, bool fills)
	{
		const Table *t = run.t;
		int bw = wmax (1, (t->bw * L.zoom + 300) / 600);		// (eighths of a point, at 96 dpi)
		unsigned bc = t->bcolor == AUTO ? 0 : t->bcolor;
		if (L.d->cur != SY_BODY) bc = wk_mix (bc, 0xFFFFFF, 150);
		int dy = 0;
		bool rep = repeatAt (t, pg, &dy);
		for (int r = rep ? -1 : 0; r < t->nrows; r++)
		{
			int row = r < 0 ? 0 : r, off = r < 0 ? dy : 0;
			if (r >= 0 && t->rowPage[r] != pg) continue;
			bool firstOnPage = r < 0 || r == 0 || t->rowPage[r - 1] != pg;
			for (int c = 0; c < t->ncols; c++)
			{
				const TCell &cl = tcell (t, row, c);
				if (cl.covered) continue;
				int re = wmin (row + (int) cl.rs, t->nrows) - 1, ce = wmin (c + (int) cl.cs, t->ncols);
				if (r < 0) re = 0;
				int x0 = ox + ((t->x64 + t->colX64[c]) >> 6), x1 = ox + ((t->x64 + t->colX64[ce]) >> 6);
				int y0 = pt + ((t->rowY64[row] + off) >> 6), y1 = pt + ((t->rowY64[re] + t->rowH64[re] + off) >> 6);
				if (fills) { if (cl.fill != AUTO) fillClip (x0, y0, x1 - x0, y1 - y0, L.d->cur != SY_BODY ? wk_mix (cl.fill, 0xFFFFFF, 150) : cl.fill); continue; }
				bool lastOnPage = re == t->nrows - 1 || t->rowPage[re + 1] != pg || r < 0;
				bool top = false, bot = false, lft = false, rgt = false;
				switch (t->border)
				{
				case TB_ALL: top = bot = lft = rgt = true; break;
				case TB_OUTER: top = firstOnPage; bot = lastOnPage; lft = c == 0; rgt = ce == t->ncols; break;
				case TB_ROWS: top = bot = true; break;
				}
				if (t->border == TB_NONE)				// (the grid shown faintly: not printed)
				{
					if (g_pdf) continue;
					unsigned g = 0xC9D3E6;
					for (int x = x0; x < x1; x += 3) { fillClip (x, y0, 1, 1, g); fillClip (x, y1, 1, 1, g); }
					for (int y = y0; y < y1; y += 3) { fillClip (x0, y, 1, 1, g); fillClip (x1, y, 1, 1, g); }
					continue;
				}
				if (top) fillClip (x0, y0, x1 - x0 + bw, bw, bc);
				if (bot) fillClip (x0, y1, x1 - x0 + bw, bw, bc);
				if (lft) fillClip (x0, y0, bw, y1 - y0 + bw, bc);
				if (rgt) fillClip (x1, y0, bw, y1 - y0 + bw, bc);
			}
		}
	}
	// A character at x64 (1/64 px) on the baseline: on the screen, or into the PDF (its font's file embedded: the glyph of
	// the face it comes from -- the font's own or DejaVu Sans, the fallback --, a made bold / italic as such)
	void glyph (fnt::Font *font, int x64, int base, unsigned cp, unsigned col, int vh)
	{
		if (!g_pdf) { fnt::draw (canvas, font, x64, base, cp, col, 0, 0, viewW (), vh); return; }
		fnt::Glyph *g = fnt::glyph (font, cp);
		fnt::Font *src = g && g->src ? g->src : font;
		fnt::FaceFile &ff = fnt::g_face[src->face];
		if (!ff.data || !g) return;
		int f = g_pdf->add_font (ff.data, ff.len);
		if (f < 0) return;
		g_pdf->glyph (f, src->size64 / 64.0f * PX2PT, x64 / 64.0f * PX2PT, base * PX2PT, (unsigned) g->gi, cp, col, src->fakeBold, src->fakeItalic);
	}
	// A tab's leader (dots, dashes, a line) from x0 to x1 (1/64 px), on a grid of the page's.
	void leader (int kind, int x0, int x1, int base, fnt::Font *font, unsigned col, int ox, int vh)
	{
		if (kind == TL_LINE) { fillClip ((x0 >> 6) + 2, base + 1, ((x1 - x0) >> 6) - 4, 1, col); return; }
		unsigned g = kind == TL_DOT ? '.' : '-';
		int a = fnt::advance (font, g);
		if (a < 64) return;
		int step = kind == TL_DOT ? a * 3 / 2 : a + a / 3, org = ox << 6;
		for (int x = org + ((x0 - org) / step + 1) * step; x + a <= x1 - step / 3; x += step)
			glyph (font, x, base, g, col, vh);
	}

	// A line: the highlights, the fields' shading and the selection behind, the characters (a field's
	// text, a tab's leader), their lines (underline, strike-through), a list's marker. dy64: moved
	// (a table's heading row again); dim: greyed (another story is edited); sel: its selection shown.
	void drawLine (const Para *q, int pi, int li, int ox, int pt, int vh, int dy64, bool dim, bool sel)
	{
		if (g_pdf) { dim = false; sel = false; }
		const Line &ln = q->ln[li];
		int top64 = (pt << 6) + ln.py64 + dy64;
		int top = top64 >> 6, bot = (top64 + ln.h64 + 63) >> 6;
		if (bot < 0 || top > vh) return;
		int tx64 = (ox << 6) + q->x64;
		const int *xs = q->xs;
		auto xAt = [&] (int i) { return i < ln.end ? xs[i] : ln.xEnd64; };
		auto ink = [&] (unsigned c) { return dim ? wk_mix (c, 0xFFFFFF, 150) : c; };
		// highlights, the fields' shading (their font's height, on the baseline)
		for (int i = ln.start; i < ln.end; )
		{
			const CharFmt &f0 = L.d->fmt[q->cf[i]];
			unsigned hl = f0.hilite;
			bool field = q->ch[i] == FIELD_CHAR && f0.fld;
			int j = i + 1;
			while (j < ln.end && !field && L.d->fmt[q->cf[j]].hilite == hl && !(q->ch[j] == FIELD_CHAR && L.d->fmt[q->cf[j]].fld)) j++;
			if (hl != AUTO || (field && !g_pdf))		// (a field's shading: the screen's only)
			{
				int x0 = (tx64 + xAt (i)) >> 6, x1 = (tx64 + xAt (j)) >> 6;
				int raise = 0; fnt::Font *hf = cf_font (q->cf[i], &raise);
				int b64 = top64 + ln.base64 + raise;
				int ht = hf ? (b64 - hf->ascent) >> 6 : top, hb = hf ? (b64 + hf->descent + 63) >> 6 : bot;
				fillClip (x0, ht, x1 - x0, hb - ht, ink (hl != AUTO ? hl : 0xE3E3E3));
			}
			i = j;
		}
		// the selection
		if (sel && has_sel ())
		{
			Pos a = sel_a (), b = sel_b ();
			if (pi >= a.p && pi <= b.p)
			{
				int s = pi == a.p ? wmax (ln.start, a.o) : ln.start;
				int e = pi == b.p ? wmin (ln.end, b.o) : ln.end;
				bool mark = pi < b.p && li == q->nln - 1;	// (the paragraph's mark selected)
				bool lineEnd = e == ln.end && li < q->nln - 1 && (pi < b.p || b.o > ln.end);
				if (s < e || (mark && s <= e && (pi > a.p || a.o <= ln.end)) || (lineEnd && s <= e))
				{
					int x0 = (tx64 + (s < ln.end ? xs[s] : ln.xEnd64)) >> 6;
					int x1 = (tx64 + xAt (e)) >> 6;
					if (mark) x1 += wmax (4, (L.zoom * 6) / 100);
					if (s >= e && mark) x0 = (tx64 + ln.xEnd64) >> 6;
					fillClip (x0, top, x1 - x0, bot - top, C_SELECT ());
				}
			}
		}
		// the characters
		for (int i = ln.start; i < ln.end; i++)
		{
			unsigned ch = q->ch[i];
			const CharFmt &f = L.d->fmt[q->cf[i]];
			if (ch == OBJ_CHAR && f.obj)				// an image, on the baseline
			{
				int base = (top64 + ln.base64 + 32) >> 6;
				int w = tw2px64 (f.ow) >> 6, h = tw2px64 (f.oh) >> 6;
				drawImage (L.d->img[f.obj - 1], (tx64 + xs[i]) >> 6, base - h, w, h, vh);
				if (dim) blendClip ((tx64 + xs[i]) >> 6, base - h, w, h, 0xFFFFFF, 150);
				Pos a = sel_a (), b = sel_b ();
				if (sel && has_sel () && !(mkpos (pi, i) < a) && mkpos (pi, i) < b)
				{
					if (b.p == a.p && b.o == a.o + 1)			// (the image alone: its frame, a handle)
					{
						int x0 = (tx64 + xs[i]) >> 6;
						frame (x0, base - h, w, h, C_ACCENT);
						fillClip (x0 + w - 4, base - 4, 8, 8, 0xFFFFFF);
						frame (x0 + w - 4, base - 4, 8, 8, C_ACCENT);
					}
					else blendClip ((tx64 + xs[i]) >> 6, base - h, w, h, C_ACCENT, 90);
				}
				continue;
			}
			int raise = 0;
			fnt::Font *font = cf_font (q->cf[i], &raise);
			if (!font) continue;
			int base = (top64 + ln.base64 + raise + 32) >> 6;
			unsigned col = ink (f.color == AUTO ? 0x000000 : f.color);
			int x64 = tx64 + xs[i];
			if (ch == FIELD_CHAR && f.fld)				// a field: its text
			{
				unsigned t[80]; int tn = field_chars (q, i, ln.page, t, 80);
				int x = x64;
				for (int k = 0; k < tn; k++) { if (t[k] > ' ') glyph (font, x, base, t[k], col, vh); x += fnt::advance (font, t[k]); }
			}
			else if (ch == '\t') { if (q->lead[i]) leader (q->lead[i], x64, tx64 + xAt (i + 1), base, font, col, ox, vh); }
			else if (ch > ' ' && ch != 0xA0) glyph (font, x64, base, ch, col, vh);
			if (f.flags & (CF_UNDER | CF_STRIKE))
			{
				bool trailing = true;
				for (int k = i; k < ln.end; k++) if (q->ch[k] != ' ' && q->ch[k] != 0x0B) { trailing = false; break; }
				if (!trailing && ch != 0x0B)
				{
					int x0 = x64 >> 6, x1 = (tx64 + xAt (i + 1)) >> 6;
					int th = wmax (1, (font->ulThick + 32) >> 6);
					if (f.flags & CF_UNDER) fillClip (x0, base + ((font->ulPos + 32) >> 6), x1 - x0, th, col);
					if (f.flags & CF_STRIKE) fillClip (x0, base - ((font->xHeight / 2 + 32) >> 6), x1 - x0, th, col);
				}
			}
		}
		// the formatting marks: a dot a space, an arrow a tab, the line breaks, the paragraph's (a
		// cell's) end
		if (g_showMarks && !dim && !g_pdf)
		{
			unsigned mc = 0x4A7FD0;
			bool cellEnd = false;
			if (in_table (q)) { int bn; Para **bps = story_p (*L.d, SY_BODY, &bn); cellEnd = pi + 1 >= bn || !same_cell (q, bps[pi + 1]); }
			for (int i = ln.start; i <= ln.end; i++)
			{
				bool end = i == ln.end;
				if (end && li != q->nln - 1) break;
				unsigned ch = end ? 0xB6 : q->ch[i];
				unsigned m = ch == ' ' ? 0xB7 : ch == '\t' ? 0x2192 : ch == 0x0B ? 0x21B5 : ch == 0xA0 ? 0xB0 : ch == 0xB6 && end ? (cellEnd ? 0xA4 : 0xB6) : 0;
				if (!m) continue;
				int cf = end ? (q->len ? q->cf[q->len - 1] : q->endCf) : q->cf[i];
				int raise = 0;
				fnt::Font *font = cf_font (cf, &raise);
				if (!font) continue;
				int x64 = tx64 + (end ? ln.xEnd64 : xs[i]);
				int w = end ? 0 : xAt (i + 1) - xs[i];
				int a = fnt::advance (font, m);
				if (ch == ' ' || ch == 0xA0) x64 += (w - a) / 2;
				fnt::draw (canvas, font, x64, (top64 + ln.base64 + raise + 32) >> 6, m, mc, 0, 0, viewW (), vh);
			}
		}
		// a list's marker
		if (li == 0 && q->pf.list != LS_NONE)
		{
			unsigned mk[16]; int n = list_marker (q, mk);
			int cf = marker_font_cf (q);
			CharFmt mf = L.d->fmt[cf];
			mf.flags &= (unsigned short) ~(CF_UNDER | CF_STRIKE | CF_SUPER | CF_SUB);
			mf.obj = 0; mf.ow = mf.oh = 0; mf.fld = 0;
			fnt::Font *font = cf_font (doc_fmt (*L.d, mf));
			if (font)
			{
				int x64 = tx64 + tw2px64 (q->pf.left + q->pf.first);
				int base = (top64 + ln.base64 + 32) >> 6;
				unsigned col = ink (mf.color == AUTO ? 0 : mf.color);
				for (int i = 0; i < n; i++) { glyph (font, x64, base, mk[i], col, vh); x64 += fnt::advance (font, mk[i]); }
			}
		}
	}

	void frame (int x, int y, int w, int h, unsigned c)
	{ fillClip (x, y, w, 1, c); fillClip (x, y + h - 1, w, 1, c); fillClip (x, y, 1, h, c); fillClip (x + w - 1, y, 1, h, c); }
	void blendClip (int x, int y, int w, int h, unsigned c, int a)
	{
		if (g_pdf) return;
		int x1 = wmin (x + w, viewW ()), y1 = wmin (y + h, viewH ());
		for (int j = wmax (y, 0); j < y1; j++) for (int i = wmax (x, 0); i < x1; i++) wk_blend_px (canvas, i, j, c, a);
	}
	// An image at its size on the screen (scaled once, kept until the size changes), blended by its alpha.
	void drawImage (Image &im, int x, int y, int w, int h, int vh)
	{
		if (w <= 0 || h <= 0 || !im.px) return;
		if (g_pdf) { g_pdf->image (im.px, im.w, im.h, x * PX2PT, y * PX2PT, w * PX2PT, h * PX2PT, g_pdfJpeg, 85); return; }
		if (im.cw != w || im.ch != h || !im.cache)
		{
			delete[] im.cache;
			im.cache = new unsigned[w * h];
			im.cw = w; im.ch = h;
			for (int j = 0; j < h; j++)				// (an area average down, bilinear-ish up)
			{
				int sy0 = (int) ((long long) j * im.h / h), sy1 = wmax (sy0 + 1, (int) ((long long) (j + 1) * im.h / h));
				for (int i = 0; i < w; i++)
				{
					int sx0 = (int) ((long long) i * im.w / w), sx1 = wmax (sx0 + 1, (int) ((long long) (i + 1) * im.w / w));
					int sy1c = wmin (sy1, sy0 + 4), sx1c = wmin (sx1, sx0 + 4);	// (at most 4 x 4 samples)
					unsigned a = 0, r = 0, g = 0, b = 0, n = 0;
					for (int yy = sy0; yy < sy1c && yy < im.h; yy++)
						for (int xx = sx0; xx < sx1c && xx < im.w; xx++)
						{
							unsigned p = im.px[yy * im.w + xx];
							a += p >> 24; r += p >> 16 & 255; g += p >> 8 & 255; b += p & 255; n++;
						}
					if (!n) n = 1;
					im.cache[j * w + i] = (a / n) << 24 | (r / n) << 16 | (g / n) << 8 | (b / n);
				}
			}
		}
		int x0 = wmax (x, 0), y0 = wmax (y, 0), x1 = wmin (x + w, viewW ()), y1 = wmin (y + h, vh);
		for (int j = y0; j < y1; j++)
		{
			const unsigned *s = im.cache + (j - y) * w;
			unsigned *d = canvas.px + j * canvas.stride;
			for (int i = x0; i < x1; i++)
			{
				unsigned p = s[i - x], al = p >> 24;
				if (al >= 255) d[i] = p & 0xFFFFFF;
				else if (al) d[i] = wk_mix (d[i], p & 0xFFFFFF, (int) al + 1);
			}
		}
	}
public:
	// The image under (x, y) of the view: its place (false: none).
	bool imageAt (int x, int y, Pos *at, int *ix, int *iy, int *iw, int *ih)
	{
		bool ae; Pos p = hit (x, y, &ae);
		for (int d = -1; d <= 0; d++)
		{
			int o = p.o + d;
			const Para *q = L.d->p[p.p];
			if (o < 0 || o >= q->len || q->ch[o] != OBJ_CHAR || !L.d->fmt[q->cf[o]].obj) continue;
			const CharFmt &f = L.d->fmt[q->cf[o]];
			const Line &ln = q->ln[line_of (q, o)];
			int top64 = (pageTop (ln.page) << 6) + ln.py64;
			int base = (top64 + ln.base64 + 32) >> 6, w = tw2px64 (f.ow) >> 6, h = tw2px64 (f.oh) >> 6;
			int x0 = ((originX () << 6) + q->x64 + q->xs[o]) >> 6;
			if (x >= x0 && x < x0 + w && y >= base - h && y < base)
			{ *at = mkpos (p.p, o); *ix = x0; *iy = base - h; *iw = w; *ih = h; return true; }
		}
		return false;
	}
	// The image selected alone (its place), or false.
	bool selectedImage (Pos *at)
	{
		Pos a = sel_a (), b = sel_b ();
		if (!has_sel () || a.p != b.p || b.o != a.o + 1) return false;
		const Para *q = L.d->p[a.p];
		if (q->ch[a.o] != OBJ_CHAR || !L.d->fmt[q->cf[a.o]].obj) return false;
		*at = a; return true;
	}
private:
	int m_rsX, m_rsY, m_rsW, m_rsH, m_rsW0, m_rsH0;	// an image being resized: its box on the screen, its size at the start

	// The caret: drawn over the canvas, the pixels under it kept (the blink puts them back).
	void drawCaret ()
	{
		if (m_caretDrawn || has_sel () || !hasFocus) return;
		int x, y, h;
		caretBox (g_caret, g_atEnd, &x, &y, &h);
		int w = L.zoom >= 175 ? 2 : 1;
		int x1 = wmin (x + w, viewW ()), y1 = wmin (y + h, viewH ()), x0 = wmax (x, 0), y0 = wmax (y, 0);
		if (x1 <= x0 || y1 <= y0 || (x1 - x0) * (y1 - y0) > (int) (sizeof m_under / sizeof m_under[0])) return;
		m_cx = x0; m_cy = y0; m_cw = x1 - x0; m_ch = y1 - y0;
		for (int j = 0; j < m_ch; j++)
			for (int i = 0; i < m_cw; i++)
			{
				unsigned &px = canvas.px[(m_cy + j) * canvas.stride + m_cx + i];
				m_under[j * m_cw + i] = px;
				px = 0x000000;
			}
		m_caretDrawn = true;
	}
	void eraseCaret ()
	{
		if (!m_caretDrawn) return;
		for (int j = 0; j < m_ch; j++)
			for (int i = 0; i < m_cw; i++) canvas.px[(m_cy + j) * canvas.stride + m_cx + i] = m_under[j * m_cw + i];
		m_caretDrawn = false;
	}

	int thumbV () { WkThumb t = wk_thumb (docH (), viewH (), sy, viewH ()); return t.h; }
	int thumbH () { WkThumb t = wk_thumb (docW (), viewW (), sx, viewW ()); return t.h; }
	void drawBars ()
	{
		unsigned bg = C_BG;
		canvas.fillRect (viewW (), 0, SBW, height, bg);
		WkThumb t = wk_thumb (docH (), viewH (), sy, viewH () - 2);
		wk_scroll_bar (canvas, viewW () + 1, 1, SBW - 2, viewH () - 2, true, t.y, t.show ? t.h : 0, bg, m_barHot == 1 || m_drag == 2 ? WK_HOT : WK_NORMAL);
		if (hbar ())
		{
			canvas.fillRect (0, viewH (), width, SBW, bg);
			WkThumb u = wk_thumb (docW (), viewW (), sx, viewW () - 2);
			wk_scroll_bar (canvas, 1, viewH () + 1, viewW () - 2, SBW - 2, false, u.y, u.show ? u.h : 0, bg, m_barHot == 2 || m_drag == 3 ? WK_HOT : WK_NORMAL);
		}
	}
};

} // namespace wr

#endif
