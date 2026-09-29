//
// view.h -- Writer's page view: the pages laid out on a grey desk, one under the other (the
// document drawn by Writer itself -- FreeType's glyphs, the highlights, the selection, the lists'
// markers, the page numbers), the caret blinking, the scroll bars; the mouse (a click places the
// caret, a drag selects, a double click a word, a triple click the paragraph, Shift+click extends;
// the wheel scrolls; a right click asks the app for its menu) and the keys (typing, the arrows --
// Ctrl: by word / paragraph, Shift: selecting --, Home / End -- Ctrl: the document's --, Page Up /
// Down, Enter -- Shift: a line break --, Tab -- in a list: its level --, Backspace / Delete --
// Ctrl: a word).
//
#ifndef _writer_view_h
#define _writer_view_h

#include "edit.h"

namespace wr {

using namespace wtk;

enum { GAP = 22, SBW = 13 };

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

	// ---- places <-> the screen ----
	// A place's line in the pages (global index), its caret box (view coords, px).
	int globalLine (int p, int l) const
	{
		int g = 0;
		for (int i = 0; i < p; i++) g += L.d->p[i]->nln;
		return g + l;
	}
	void caretBox (Pos c, bool atEnd, int *x, int *y, int *h, int *base = 0)
	{
		const Para *q = L.d->p[c.p];
		int l = line_of (q, c.o, atEnd);
		const Line &ln = q->ln[l];
		int top = (pageTop (ln.page) << 6) + L.mt64 + ln.py64;
		*x = ((originX () << 6) + L.ml64 + x_of (c.p, c.o, atEnd)) >> 6;
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
	// The place under (x, y) of the view (atEnd: the end of a line).
	Pos hit (int x, int y, bool *atEnd)
	{
		*atEnd = false;
		int page = (y + sy - GAP) / (L.pageH + GAP);
		if (y + sy - GAP < 0) page = 0;
		page = wclamp (page, 0, L.npages - 1);
		int g0 = L.pageFirst[page], g1 = page + 1 < L.npages ? L.pageFirst[page + 1] : L.nall;
		if (g0 >= g1) { g0 = wmax (0, g0 - 1); g1 = g0 + 1; }
		int y64 = ((y - pageTop (page)) << 6) - L.mt64;
		int g = g0;
		for (int k = g0; k < g1; k++)
		{
			const Line &ln = L.d->p[L.all[k].p]->ln[L.all[k].l];
			g = k;
			if (y64 < ln.py64 + ln.h64) break;
		}
		int p = L.all[g].p, l = L.all[g].l;
		int x64 = ((x - originX ()) << 6) - L.ml64;
		int o = offset_at_x (p, l, x64, atEnd);
		return mkpos (p, o);
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

	void onDraw () override
	{
		relayout ();
		m_caretDrawn = false;
		int vw = viewW (), vh = viewH ();
		canvas.fillRect (0, 0, width, height, C_DESK ());
		int ox = originX ();
		unsigned shadow = wk_mix (C_DESK (), 0, 60);
		for (int pg = 0; pg < L.npages; pg++)
		{
			int pt = pageTop (pg);
			if (pt > vh) break;
			if (pt + L.pageH < 0) continue;
			fillClip (ox + 3, pt + 3, L.pageW, L.pageH, shadow);
			fillClip (ox - 1, pt - 1, L.pageW + 2, L.pageH + 2, wk_mix (C_DESK (), 0, 90));
			fillClip (ox, pt, L.pageW, L.pageH, 0xFFFFFF);
			cropMarks (ox, pt);
			int g1 = pg + 1 < L.npages ? L.pageFirst[pg + 1] : L.nall;
			for (int g = L.pageFirst[pg]; g < g1; g++) drawLine (L.all[g].p, L.all[g].l, ox, pt, vh);
			if (L.d->page.numbers) pageNumber (pg, ox, pt);
		}
		if (m_drag == 4) { frame (m_rsX, m_rsY, m_rsW, m_rsH, C_ACCENT); frame (m_rsX + 1, m_rsY + 1, m_rsW - 2, m_rsH - 2, 0xFFFFFF); }
		drawBars ();
		if (m_caretOn && hasFocus && !has_sel ()) drawCaret ();
		(void) vw;
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
		}
		if (press)
		{
			setFocus ();
			bool ae; Pos p = hit (mx, my, &ae);
			unsigned t = kapi_get_ticks ();
			bool again = t - m_lastClick < 45 && mx - m_lastX < 5 && m_lastX - mx < 5 && my - m_lastY < 5 && m_lastY - my < 5;
			m_clicks = again ? m_clicks % 3 + 1 : 1;
			m_lastClick = t; m_lastX = mx; m_lastY = my;
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
			if (L.d->p[sel_a ().p]->pf.list != LS_NONE && (sel_a ().o == 0 || sel_a ().p != sel_b ().p)) { ed_para (pf_indent, shift ? -1 : 1); break; }
			if (sel_a ().p != sel_b ().p) { ed_para (pf_indent, shift ? -1 : 1); break; }
			{ unsigned t = '\t'; ed_type (&t, 1, ED_TYPE); }
			break;
		case KEY_BACKSPACE: ed_delete (false, ctrl); break;
		case KEY_DEL: ed_delete (true, ctrl); break;
		case 27: if (has_sel ()) set_caret (g_caret, false); break;
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
	bool m_bl; int m_drag;			// 1 selecting, 2 / 3 a scroll bar's thumb, 4 an image's corner
	int m_clicks; unsigned m_lastClick; int m_lastX, m_lastY, m_mx, m_my;
	Pos m_selA, m_selB;			// a double / triple click's word / paragraph (the drag extends it)
	bool m_caretOn, m_caretDrawn; unsigned m_blinkT;
	int m_cx, m_cy, m_cw, m_ch; unsigned m_under[4 * 512];
	int m_barHot;

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

	void verticalMove (int dir, bool shift)
	{
		relayout ();
		Pos c = g_caret;
		const Para *q = L.d->p[c.p];
		int l = line_of (q, c.o, g_atEnd);
		if (g_goalX < 0) g_goalX = x_of (c.p, c.o, g_atEnd);
		int g = globalLine (c.p, l) + dir;
		if (g < 0) { set_caret (mkpos (0, 0), shift); return; }
		if (g >= L.nall) { set_caret (doc_end (*L.d), shift); return; }
		int gx = g_goalX;
		bool ae;
		int o = offset_at_x (L.all[g].p, L.all[g].l, gx, &ae);
		set_caret (mkpos (L.all[g].p, o), shift, ae);
		g_goalX = gx;
	}
	void pageMove (int dir, bool shift)
	{
		relayout ();
		int x, y, h;
		caretBox (g_caret, g_atEnd, &x, &y, &h);
		int d = viewH () - 40;
		int gx = g_goalX >= 0 ? g_goalX : x_of (g_caret.p, g_caret.o, g_atEnd);
		sy += dir * d; clampScroll ();
		bool ae; Pos p = hit (x, wclamp (y + h / 2, 0, viewH () - 1), &ae);
		(void) gx;
		set_caret (p, shift, ae);
		g_goalX = gx;
	}

	void fillClip (int x, int y, int w, int h, unsigned c)
	{
		int x1 = wmin (x + w, viewW ()), y1 = wmin (y + h, viewH ());
		x = wmax (x, 0); y = wmax (y, 0);
		if (x1 > x && y1 > y) canvas.fillRect (x, y, x1 - x, y1 - y, c);
	}
	void cropMarks (int ox, int pt)
	{
		unsigned c = 0xC8C8C8;
		int x0 = ox + (L.ml64 >> 6), y0 = pt + (L.mt64 >> 6), x1 = ox + L.pageW - (L.mr64 >> 6), y1 = pt + L.pageH - (L.mb64 >> 6);
		int m = wmin (14, wmin (L.ml64 >> 6, L.mt64 >> 6) - 2);
		if (m < 4) return;
		fillClip (x0 - m, y0 - 1, m, 1, c); fillClip (x0 - 1, y0 - m, 1, m, c);
		fillClip (x1, y0 - 1, m, 1, c); fillClip (x1, y0 - m, 1, m, c);
		fillClip (x0 - m, y1, m, 1, c); fillClip (x0 - 1, y1, 1, m, c);
		fillClip (x1, y1, m, 1, c); fillClip (x1, y1, 1, m, c);
	}
	void pageNumber (int pg, int ox, int pt)
	{
		unsigned buf[12]; int n = 0; int v = pg + 1; char t[12]; int j = 0;
		while (v) { t[j++] = (char) ('0' + v % 10); v /= 10; }
		while (j) buf[n++] = (unsigned char) t[--j];
		fnt::Font *f = cf_font (doc_fmt (*L.d, style_fmt (*L.d, ST_NORMAL)));
		if (!f) return;
		int w = 0; for (int i = 0; i < n; i++) w += fnt::advance (f, buf[i]);
		int x64 = (ox << 6) + L.ml64 + (L.textW64 - w) / 2;
		int y = pt + L.pageH - (L.mb64 >> 7) + (f->ascent >> 7);
		for (int i = 0; i < n; i++) { fnt::draw (canvas, f, x64, y, buf[i], 0x404040, 0, 0, viewW (), viewH ()); x64 += fnt::advance (f, buf[i]); }
	}

	// A line: the highlights and the selection behind, the characters, their lines (underline,
	// strike-through), a list's marker.
	void drawLine (int pi, int li, int ox, int pt, int vh)
	{
		const Para *q = L.d->p[pi];
		const Line &ln = q->ln[li];
		int top64 = (pt << 6) + L.mt64 + ln.py64;
		int top = top64 >> 6, bot = (top64 + ln.h64 + 63) >> 6;
		if (bot < 0 || top > vh) return;
		int tx64 = (ox << 6) + L.ml64;
		const int *xs = q->xs;
		auto xAt = [&] (int i) { return i < ln.end ? xs[i] : ln.xEnd64; };
		// highlights
		for (int i = ln.start; i < ln.end; )
		{
			unsigned hl = L.d->fmt[q->cf[i]].hilite;
			int j = i + 1;
			while (j < ln.end && L.d->fmt[q->cf[j]].hilite == hl) j++;
			if (hl != AUTO)				// (the run's font's height, on the baseline)
			{
				int x0 = (tx64 + xAt (i)) >> 6, x1 = (tx64 + xAt (j)) >> 6;
				int raise = 0; fnt::Font *hf = cf_font (q->cf[i], &raise);
				int b64 = top64 + ln.base64 + raise;
				int ht = hf ? (b64 - hf->ascent) >> 6 : top, hb = hf ? (b64 + hf->descent + 63) >> 6 : bot;
				fillClip (x0, ht, x1 - x0, hb - ht, hl);
			}
			i = j;
		}
		// the selection
		if (has_sel ())
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
				Pos a = sel_a (), b = sel_b ();
				if (has_sel () && !(mkpos (pi, i) < a) && mkpos (pi, i) < b)
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
			unsigned col = f.color == AUTO ? 0x000000 : f.color;
			int x64 = tx64 + xs[i];
			if (ch > ' ' && ch != 0xA0) fnt::draw (canvas, font, x64, base, ch, col, 0, 0, viewW (), vh);
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
		// the formatting marks: a dot a space, an arrow a tab, the line breaks, the paragraph's end
		if (g_showMarks)
		{
			unsigned mc = 0x4A7FD0;
			for (int i = ln.start; i <= ln.end; i++)
			{
				bool end = i == ln.end;
				if (end && li != q->nln - 1) break;
				unsigned ch = end ? 0xB6 : q->ch[i];
				unsigned m = ch == ' ' ? 0xB7 : ch == '\t' ? 0x2192 : ch == 0x0B ? 0x21B5 : ch == 0xA0 ? 0xB0 : ch == 0xB6 && end ? 0xB6 : 0;
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
			fnt::Font *font = cf_font (doc_fmt (*L.d, mf));
			if (font)
			{
				int x64 = tx64 + tw2px64 (q->pf.left + q->pf.first);
				int base = (top64 + ln.base64 + 32) >> 6;
				unsigned col = mf.color == AUTO ? 0 : mf.color;
				for (int i = 0; i < n; i++) { fnt::draw (canvas, font, x64, base, mk[i], col, 0, 0, viewW (), vh); x64 += fnt::advance (font, mk[i]); }
			}
		}
	}

	void frame (int x, int y, int w, int h, unsigned c)
	{ fillClip (x, y, w, 1, c); fillClip (x, y + h - 1, w, 1, c); fillClip (x, y, 1, h, c); fillClip (x + w - 1, y, 1, h, c); }
	void blendClip (int x, int y, int w, int h, unsigned c, int a)
	{
		int x1 = wmin (x + w, viewW ()), y1 = wmin (y + h, viewH ());
		for (int j = wmax (y, 0); j < y1; j++) for (int i = wmax (x, 0); i < x1; i++) wk_blend_px (canvas, i, j, c, a);
	}
	// An image at its size on the screen (scaled once, kept until the size changes), blended by its alpha.
	void drawImage (Image &im, int x, int y, int w, int h, int vh)
	{
		if (w <= 0 || h <= 0 || !im.px) return;
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
			int top64 = (pageTop (ln.page) << 6) + L.mt64 + ln.py64;
			int base = (top64 + ln.base64 + 32) >> 6, w = tw2px64 (f.ow) >> 6, h = tw2px64 (f.oh) >> 6;
			int x0 = ((originX () << 6) + L.ml64 + q->xs[o]) >> 6;
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
		if (g_atEnd || x >= 1) x -= 0;
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
