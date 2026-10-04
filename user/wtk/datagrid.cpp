//
// wtk/datagrid.cpp -- DataGrid (see datagrid.h): the header, the rows, the scroll bars. Drawn at
// each change in full (the rows shown only: a grid of any length costs what fits in it).
//
#include "wtk/datagrid.h"

namespace wtk {

#define DG_DBL_TICKS	70			// a double click: two clicks within this (HZ ticks)
enum { DG_SB = WK_SBW + 2 };			// a scroll bar's room (the bar and its margin)
enum { G_NONE, G_VTHUMB, G_HTHUMB, G_EDGE, G_TITLE, G_ROWS };

static void dg_copy (char *d, const char *s, int cap)
{ int i = 0; if (s) for (; s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = '\0'; }

// A text cut to w px ("..." at its end when it did not fit), control characters as spaces, laid
// in (x, y, w, h) by `align`.
static void dg_text (Canvas &cv, int x, int y, int w, int h, const char *s, unsigned ink, int align, int style)
{
	if (wk_textface ())				// a proportional face (wtk/text.h): cut by measure
	{
		if (!s || w <= 0) return;
		char c[256], b[256]; int n = 0;
		for (; s[n] && n < 255; n++) { unsigned char ch = (unsigned char) s[n]; c[n] = ch < 32 ? ' ' : (char) ch; }
		c[n] = '\0';
		int tw = wk_text_fit (c, w, b, sizeof b, style);
		int tx = align == GRID_RIGHT ? x + w - tw : align == GRID_CENTRE ? x + (w - tw) / 2 : x;
		wk_text_l (cv, tx, y, h, b, ink, style);
		return;
	}
	int fw = wk_text_w ("M", style); if (fw < 1) fw = 8;
	int maxc = w / fw; if (maxc <= 0 || !s) return;
	if (maxc > 255) maxc = 255;
	char b[256]; int n = 0;
	while (s[n] && n < maxc) { unsigned char c = (unsigned char) s[n]; b[n] = c < 32 ? ' ' : (char) c; n++; }
	if (s[n])					// (cut: the end made "...")
	{
		if (maxc >= 5) { n = maxc - 3; b[n++] = '.'; b[n++] = '.'; b[n++] = '.'; }
		else n = maxc;
	}
	b[n] = '\0';
	int tw = n * fw, tx = align == GRID_RIGHT ? x + w - tw : align == GRID_CENTRE ? x + (w - tw) / 2 : x;
	wk_text_l (cv, tx, y, h, b, ink, style);
}

DataGrid::DataGrid (int l, int t, int w, int h)
  : Widget (l, t, w, h), nrows (0), sel (-1), top (0), left (0), rowH (wk_fh () + 8), headH (wk_fh () + 10),
    sortCol (-1), sortDesc (false), sortable (true), stripes (true), cellText (0), cellDraw (0),
    onSelect (0), onActivate (0), onSort (0), onContext (0), clickedCol (-1), ctxRow (-1), ctxX (0), ctxY (0),
    emptyText (0), user (0), m_ncol (0), m_hotRow (-1), m_hotHead (-1), m_drag (G_NONE), m_dragCol (-1),
    m_dragX0 (0), m_dragW0 (0), m_lastClick (0), m_lastRow (-1), m_rdown (false)
{ canFocus = true; }

void DataGrid::setColumns (int n)
{
	if (n < 0) n = 0;
	if (n > MAXCOLS) n = MAXCOLS;
	for (int c = m_ncol; c < n; c++) { m_col[c].title[0] = '\0'; m_col[c].width = 100; m_col[c].align = GRID_LEFT; }
	m_ncol = n;
	if (sortCol >= n) sortCol = -1;
	clampScroll ();
	invalidate (true);
}

void DataGrid::setColumn (int c, const char *title, int w, int align)
{
	if (c < 0 || c >= m_ncol) return;
	dg_copy (m_col[c].title, title, sizeof m_col[c].title);
	m_col[c].width = w < 16 ? 16 : w;
	m_col[c].align = align;
	invalidate (true);
}

void DataGrid::autoSize (int c, int minW, int maxW, int sample)
{
	if (c < 0 || c >= m_ncol) return;
	int w = wk_text_w (m_col[c].title, 2) + 16 + (sortable ? 14 : 0);
	char buf[256];
	for (int r = 0; r < nrows && r < sample && cellText; r++)
	{
		const char *s = cellText (*this, r, c, buf, sizeof buf);
		int tw = wk_text_w (s ? s : "") + 16;
		if (tw > w) w = tw;
		if (w >= maxW) break;
	}
	if (w < minW) w = minW;
	if (w > maxW) w = maxW;
	m_col[c].width = w;
	invalidate (true);
}

int DataGrid::totalWidth () const
{ int t = 0; for (int c = 0; c < m_ncol; c++) t += m_col[c].width; return t; }

// Which bars show, and the body's size then (a bar takes room the other one may need).
void DataGrid::bars (bool *vbar, bool *hbar, int *bodyW, int *bodyH) const
{
	int iw = width - 2, ih = height - 2 - headH, tw = totalWidth ();
	bool v = false, h = false;
	for (int pass = 0; pass < 2; pass++)
	{
		v = (long) nrows * rowH > ih - (h ? DG_SB : 0);
		h = tw > iw - (v ? DG_SB : 0);
	}
	*vbar = v; *hbar = h;
	*bodyW = iw - (v ? DG_SB : 0);
	*bodyH = ih - (h ? DG_SB : 0);
	if (*bodyW < 1) *bodyW = 1;
	if (*bodyH < 1) *bodyH = 1;
}

int DataGrid::visibleRows () const
{ bool v, h; int bw, bh; bars (&v, &h, &bw, &bh); int r = bh / (rowH > 0 ? rowH : 1); return r < 1 ? 1 : r; }

int DataGrid::colX (int c) const
{ int x = -left; for (int i = 0; i < c && i < m_ncol; i++) x += m_col[i].width; return x; }

int DataGrid::headAt (int mx) const
{
	int x = 1 - left;
	for (int c = 0; c < m_ncol; c++) { if (mx >= x && mx < x + m_col[c].width) return c; x += m_col[c].width; }
	return -1;
}

int DataGrid::edgeAt (int mx) const
{
	int x = 1 - left;
	for (int c = 0; c < m_ncol; c++) { x += m_col[c].width; if (mx >= x - 4 && mx <= x + 3) return c; }
	return -1;
}

int DataGrid::rowAt (int y) const
{
	bool v, h; int bw, bh; bars (&v, &h, &bw, &bh);
	int by = y - 1 - headH;
	if (by < 0 || by >= bh) return -1;
	int r = top + by / rowH;
	return r < nrows ? r : -1;
}

void DataGrid::clampScroll ()
{
	bool v, h; int bw, bh; bars (&v, &h, &bw, &bh);
	int vis = bh / rowH; if (vis < 1) vis = 1;
	int mt = nrows - vis; if (mt < 0) mt = 0;
	if (top > mt) top = mt;
	if (top < 0) top = 0;
	int ml = totalWidth () - bw; if (ml < 0) ml = 0;
	if (left > ml) left = ml;
	if (left < 0) left = 0;
}

void DataGrid::setRows (int n)
{
	nrows = n < 0 ? 0 : n;
	if (sel >= nrows) sel = nrows - 1;
	if (m_hotRow >= nrows) m_hotRow = -1;
	clampScroll ();
	invalidate (true);
}

void DataGrid::ensureVisible (int r)
{
	if (r < 0 || r >= nrows) return;
	int vis = visibleRows ();
	if (r < top) top = r;
	if (r >= top + vis) top = r - vis + 1;
	clampScroll ();
	invalidate (true);
}

void DataGrid::setSel (int r)
{
	if (r < -1 || r >= nrows) r = -1;
	sel = r;
	ensureVisible (r);
	invalidate (true);
}

void DataGrid::pick (int r, bool fire)
{
	if (nrows <= 0) return;
	if (r < 0) r = 0;
	if (r >= nrows) r = nrows - 1;
	bool changed = r != sel;
	sel = r;
	ensureVisible (r);
	invalidate (true);
	if (changed && fire && onSelect) onSelect (*this);
}

// ---- drawing ------------------------------------------------------------------------------------
void DataGrid::onDraw ()
{
	bool vbar, hbar; int bw, bh;
	bars (&vbar, &hbar, &bw, &bh);
	clampScroll ();
	canvas.clear (bgColor ());
	wk_sunken (canvas, 0, 0, width, height, 4, disabled ? wk_tone (C_FACE, 150) : C_FIELD, hasFocus && !disabled);
	int st = canvas.stride;
	unsigned face = C_FACE, hink = wk_ink_for (wk_tone (face, 160));

	// the header: a raised strip, the titles in bold, the sort's arrow, etched lines between them
	Canvas hc; hc.adopt (canvas.px + st + 1, width - 2, headH, st);
	wk_rbox (hc, 0, 0, hc.w, headH, 3, wk_tone (face, 178), wk_tone (face, 146), 255, WK_TL | WK_TR);
	hc.fillRect (0, headH - 1, hc.w, 1, wk_tone (face, 100));
	for (int c = 0; c < m_ncol; c++)
	{
		int x = colX (c), w = m_col[c].width;
		if (x >= hc.w) break;
		if (x + w <= 0) continue;
		bool hot = c == m_hotHead && sortable, down = m_drag == G_TITLE && m_dragCol == c;
		if (hot || down) wk_rbox (hc, x, 0, w, headH - 1, 0, wk_tone (face, down ? 150 : 192), wk_tone (face, down ? 140 : 160));
		bool arrow = c == sortCol;
		int tl = x + 8, tw = w - 16 - (arrow ? 14 : 0);
		int al = m_col[c].align == GRID_RIGHT && !arrow ? GRID_RIGHT : m_col[c].align == GRID_CENTRE ? GRID_CENTRE : GRID_LEFT;
		dg_text (hc, tl, 0, tw, headH - 1, m_col[c].title, hink, al, 2);
		if (arrow) wk_glyph (hc, sortDesc ? WKG_CHEV_DOWN : WKG_CHEV_UP, x + w - 13, headH / 2, 8, wk_mix (hink, C_ACCENT, 170));
		wk_etch_v (hc, x + w - 2, 5, headH - 11, face);
	}

	// the rows (clipped to the body)
	Canvas bc; bc.adopt (canvas.px + (1 + headH) * st + 1, bw, bh, st);
	int tw = totalWidth (), rowsW = tw - left < bw ? tw - left : bw;
	unsigned stripe = wk_mix (C_FIELD, C_FIELD_TEXT, 9), line = wk_mix (C_FIELD, C_FIELD_TEXT, 22);
	char buf[256];
	for (int r = top; r < nrows; r++)
	{
		int y = (r - top) * rowH;
		if (y >= bh) break;
		bool s = r == sel, hot = r == m_hotRow && !s;
		if (stripes && (r & 1)) bc.fillRect (0, y, rowsW, rowH, stripe);
		for (int c = 0; c < m_ncol; c++)		// the lines between the columns
		{
			int x = colX (c) + m_col[c].width - 1;
			if (x >= 0 && x < bw) bc.fillRect (x, y, 1, rowH, line);
		}
		if (s) wk_hilite (bc, 1, y + 1, rowsW - 2, rowH - 2, 4, hasFocus);
		else if (hot) wk_rbox (bc, 1, y + 1, rowsW - 2, rowH - 2, 4, wk_mix (C_FIELD, C_ACCENT, 34), wk_mix (C_FIELD, C_ACCENT, 44));
		unsigned ink = disabled ? C_DIS : s ? wk_hilite_ink (hasFocus) : C_FIELD_TEXT;
		for (int c = 0; c < m_ncol; c++)
		{
			int x = colX (c), w = m_col[c].width;
			if (x >= bw) break;
			if (x + w <= 0) continue;
			int cx0 = x < 0 ? 0 : x, cx1 = x + w > bw ? bw : x + w;
			int cy0 = y, cy1 = y + rowH > bh ? bh : y + rowH;
			if (cx1 <= cx0 || cy1 <= cy0) continue;
			Canvas cc; cc.adopt (bc.px + cy0 * st + cx0, cx1 - cx0, cy1 - cy0, st);
			if (cellDraw && cellDraw (*this, cc, r, c, x - cx0, 0, w, rowH, ink, s)) continue;
			const char *t = cellText ? cellText (*this, r, c, buf, sizeof buf) : "";
			dg_text (cc, x - cx0 + 8, 0, w - 16, rowH, t, ink, m_col[c].align, 0);
		}
	}
	if (nrows == 0 && emptyText)
		wk_text_c (bc, 0, 0, bw, rowH * 3 < bh ? rowH * 3 : bh, emptyText, wk_mix (C_FIELD, C_FIELD_TEXT, 130));

	// the scroll bars
	if (vbar)
	{
		WkThumb t = wk_thumb (nrows, bh / rowH, top, bh);
		wk_scroll_bar (canvas, 1 + bw + 1, 1 + headH, WK_SBW, bh, true, t.y, t.show ? t.h : 0, C_FIELD,
			       m_drag == G_VTHUMB ? WK_HOT : WK_NORMAL);
	}
	if (hbar)
	{
		WkThumb t = wk_thumb (tw, bw, left, bw);
		wk_scroll_bar (canvas, 1, 1 + headH + bh + 1, bw, WK_SBW, false, t.y, t.show ? t.h : 0, C_FIELD,
			       m_drag == G_HTHUMB ? WK_HOT : WK_NORMAL);
	}
}

// ---- the mouse ----------------------------------------------------------------------------------
bool DataGrid::onMouse (int mx, int my, int bl, int br, int, int wheel)
{
	bool vbar, hbar; int bw, bh;
	bars (&vbar, &hbar, &bw, &bh);
	if (mx < 0 && m_drag == G_NONE)				// the pointer left
	{
		if (m_hotRow >= 0 || m_hotHead >= 0) { m_hotRow = m_hotHead = -1; invalidate (true); }
		m_rdown = false;
		return false;
	}
	if (disabled) return true;
	if (wheel)
	{
		if (kapi_get_modifiers () & MOD_SHIFT) left -= wheel * 40; else top -= wheel;
		clampScroll ();
		invalidate (true);
		return true;
	}
	bool inHead = my >= 1 && my < 1 + headH && mx >= 1 && mx < width - 1;
	int by = my - 1 - headH;
	bool inBody = mx >= 1 && mx < 1 + bw && by >= 0 && by < bh;
	if (m_drag == G_EDGE || (m_drag == G_NONE && inHead && edgeAt (mx) >= 0)) wk_cursor (KAPI_CURSOR_SIZE_H);	// a column's edge
	// a drag going on
	if (m_drag != G_NONE)
	{
		if (!bl)
		{
			int was = m_drag;
			m_drag = G_NONE; catchOutside = false; pressed = false; invalidate (true);
			if (was == G_TITLE && inHead && headAt (mx) == m_dragCol && onSort) { clickedCol = m_dragCol; onSort (*this); }
			return true;
		}
		if (m_drag == G_VTHUMB)
		{
			WkThumb t = wk_thumb (nrows, bh / rowH, top, bh);
			top = (int) wk_thumb_pos (my - 1 - headH, bh, nrows, bh / rowH, t.h);
		}
		else if (m_drag == G_HTHUMB)
		{
			int tw = totalWidth ();
			WkThumb t = wk_thumb (tw, bw, left, bw);
			left = (int) wk_thumb_pos (mx - 1, bw, tw, bw, t.h);
		}
		else if (m_drag == G_EDGE)
		{
			int w = m_dragW0 + mx - m_dragX0;
			m_col[m_dragCol].width = w < 24 ? 24 : w > 2000 ? 2000 : w;
		}
		else if (m_drag == G_ROWS && inBody)
		{
			int r = top + by / rowH;
			if (r < nrows && r != sel) pick (r, true);
		}
		clampScroll ();
		invalidate (true);
		return true;
	}
	// hover
	int hr = inBody ? top + by / rowH : -1; if (hr >= nrows) hr = -1;
	int hh = inHead && edgeAt (mx) < 0 ? headAt (mx) : -1;
	if (hr != m_hotRow || hh != m_hotHead) { m_hotRow = hr; m_hotHead = hh; invalidate (true); }
	// the right button: select, then the menu
	if (br && !m_rdown)
	{
		m_rdown = true;
		setFocus ();
		if (inBody || my >= 1 + headH)
		{
			if (hr >= 0) pick (hr, true);
			ctxRow = hr; ctxX = mx; ctxY = my;
			if (onContext) onContext (*this);
		}
		return true;
	}
	if (!br) m_rdown = false;
	if (!bl || pressed) { if (!bl) pressed = false; return mx >= 0 && my >= 0 && mx < width && my < height; }
	// a press
	pressed = true;
	setFocus ();
	if (vbar && mx >= 1 + bw && my >= 1 + headH && my < 1 + headH + bh)
	{
		m_drag = G_VTHUMB; catchOutside = true;
		WkThumb t = wk_thumb (nrows, bh / rowH, top, bh);
		top = (int) wk_thumb_pos (my - 1 - headH, bh, nrows, bh / rowH, t.h);
		clampScroll (); invalidate (true);
		return true;
	}
	if (hbar && my >= 1 + headH + bh && mx < 1 + bw)
	{
		m_drag = G_HTHUMB; catchOutside = true;
		int tw = totalWidth ();
		WkThumb t = wk_thumb (tw, bw, left, bw);
		left = (int) wk_thumb_pos (mx - 1, bw, tw, bw, t.h);
		clampScroll (); invalidate (true);
		return true;
	}
	if (inHead)
	{
		int e = edgeAt (mx);
		if (e >= 0) { m_drag = G_EDGE; m_dragCol = e; m_dragX0 = mx; m_dragW0 = m_col[e].width; catchOutside = true; }
		else if (sortable && headAt (mx) >= 0) { m_drag = G_TITLE; m_dragCol = headAt (mx); catchOutside = true; invalidate (true); }
		return true;
	}
	if (inBody && hr >= 0)
	{
		unsigned now = kapi_get_ticks ();
		bool dbl = hr == m_lastRow && now - m_lastClick < DG_DBL_TICKS;
		pick (hr, true);
		if (dbl) { m_lastRow = -1; if (onActivate) onActivate (*this); }
		else { m_lastRow = hr; m_lastClick = now; m_drag = G_ROWS; catchOutside = true; }
	}
	return true;
}

// ---- the keys -----------------------------------------------------------------------------------
bool DataGrid::onKey (long k)
{
	if (disabled) return false;
	int page = visibleRows () - 1; if (page < 1) page = 1;
	switch (k)
	{
	case KEY_UP:   pick (sel < 0 ? 0 : sel - 1, true); return true;
	case KEY_DOWN: pick (sel < 0 ? 0 : sel + 1, true); return true;
	case KEY_PGUP: pick (sel - page, true); return true;
	case KEY_PGDN: pick (sel < 0 ? page : sel + page, true); return true;
	case KEY_HOME: pick (0, true); return true;
	case KEY_END:  pick (nrows - 1, true); return true;
	case KEY_LEFT:  left -= 40; clampScroll (); invalidate (true); return true;
	case KEY_RIGHT: left += 40; clampScroll (); invalidate (true); return true;
	case KEY_ENTER: if (sel >= 0 && onActivate) onActivate (*this); return true;
	}
	return false;
}

} // namespace wtk
