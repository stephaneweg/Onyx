//
// uikit/datagrid.cpp -- DataGrid (see datagrid.h): the header, the rows, the scroll bars. Drawn at
// each change in full (the rows shown only: a grid of any length costs what fits in it).
//
#include "uikit/datagrid.h"
#include "uikit/adapt.h"
#include "uikit/internal/adapt_int.h"

namespace uikit {

#define DG_DBL_TICKS	70			// a double click: two clicks within this (HZ ticks)
enum { DG_SB = UK_SBW + 2 };			// a scroll bar's room (the bar and its margin)
enum { G_NONE, G_VTHUMB, G_HTHUMB, G_EDGE, G_TITLE, G_ROWS };

// ---- (P6) the adaptive part: the columns' roles and priorities, the cards, the detail view, several rows chosen ------
namespace {
struct DgExt : internal::ExtHead
{
	unsigned char role[DataGrid::MAXCOLS] = {}, prio[DataGrid::MAXCOLS] = {};
	bool	 gone[DataGrid::MAXCOLS] = {};	// (pocket: the columns that gave way to the room)
	Widget	*detail = 0; bool detailOn = false;
	bool	 multi = false; unsigned char *bits = 0; int cap = 0; int anchor = -1;
};
DgExt *dg_ext (const DataGrid &g)
{
	internal::ExtHead *h = internal::ext_head ((Widget *) &g);
	return h && h->kind == internal::EXT_DATAGRID ? (DgExt *) h : 0;
}
DgExt *dg_make (DataGrid &g)
{
	DgExt *e = dg_ext (g);
	if (e) return e;
	e = internal::ext_of<DgExt> (&g, internal::EXT_DATAGRID);
	e->destroy = [] (internal::ExtHead *x) { delete [] ((DgExt *) x)->bits; delete (DgExt *) x; };
	return e;
}
bool dg_regular () { return uk_size_class () == UK_SC_REGULAR; }
bool dg_cards (const DataGrid &g)
{
	if (uk_size_class () != UK_SC_NARROW) return false;
	DgExt *e = dg_ext (g);
	if (!e) return false;
	for (int c = 0; c < ((DataGrid &) g).columns (); c++) if (e->role[c] == UK_COL_PRIMARY) return true;
	return false;
}
int dg_w (const DataGrid &g, int c)		// a column's width now (0: it gave way)
{
	int w = ((DataGrid &) g).column (c).width;
	if (dg_regular ()) return w;
	DgExt *e = dg_ext (g);
	return e && e->gone[c] ? 0 : w;
}
int dg_rh (const DataGrid &g) { return dg_cards (g) ? 2 * uk_fh () + 18 : g.rowH; }
int dg_hh (const DataGrid &g) { return dg_cards (g) ? 0 : g.headH; }
int dg_sb () { return dg_regular () ? UK_SBW + 2 : 0; }	// (pocket's overlay bars take no room)
}

static void dg_copy (char *d, const char *s, int cap)
{ int i = 0; if (s) for (; s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = '\0'; }

// A text cut to w px ("..." at its end when it did not fit), control characters as spaces, laid
// in (x, y, w, h) by `align`.
static void dg_text (Canvas &cv, int x, int y, int w, int h, const char *s, unsigned ink, int align, int style)
{
	if (uk_textface ())				// a proportional face (uikit/text.h): cut by measure
	{
		if (!s || w <= 0) return;
		char c[256], b[256]; int n = 0;
		for (; s[n] && n < 255; n++) { unsigned char ch = (unsigned char) s[n]; c[n] = ch < 32 ? ' ' : (char) ch; }
		c[n] = '\0';
		int tw = uk_text_fit (c, w, b, sizeof b, style);
		int tx = align == GRID_RIGHT ? x + w - tw : align == GRID_CENTRE ? x + (w - tw) / 2 : x;
		uk_text_l (cv, tx, y, h, b, ink, style);
		return;
	}
	int fw = uk_text_w ("M", style); if (fw < 1) fw = 8;
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
	uk_text_l (cv, tx, y, h, b, ink, style);
}

DataGrid::DataGrid (int l, int t, int w, int h)
  : Widget (l, t, w, h), nrows (0), sel (-1), top (0), left (0), rowH (uk_size_class () == UK_SC_REGULAR ? uk_fh () + 8 : uk_metrics ().row), headH (uk_fh () + 10),
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
	int w = uk_text_w (m_col[c].title, 2) + 16 + (sortable ? 14 : 0);
	char buf[256];
	for (int r = 0; r < nrows && r < sample && cellText; r++)
	{
		const char *s = cellText (*this, r, c, buf, sizeof buf);
		int tw = uk_text_w (s ? s : "") + 16;
		if (tw > w) w = tw;
		if (w >= maxW) break;
	}
	if (w < minW) w = minW;
	if (w > maxW) w = maxW;
	m_col[c].width = w;
	invalidate (true);
}

int DataGrid::totalWidth () const
{ int t = 0; for (int c = 0; c < m_ncol; c++) t += dg_w (*this, c); return t; }

// Which bars show, and the body's size then (a bar takes room the other one may need).
void DataGrid::bars (bool *vbar, bool *hbar, int *bodyW, int *bodyH) const
{
	int iw = width - 2, ih = height - 2 - dg_hh (*this), tw = totalWidth (), sb = dg_sb (), rh = dg_rh (*this);
	bool v = false, h = false;
	for (int pass = 0; pass < 2; pass++)
	{
		v = (long) nrows * rh > ih - (h ? sb : 0);
		h = tw > iw - (v ? sb : 0);
	}
	*vbar = v; *hbar = h;
	*bodyW = iw - (v ? sb : 0);
	*bodyH = ih - (h ? sb : 0);
	if (*bodyW < 1) *bodyW = 1;
	if (*bodyH < 1) *bodyH = 1;
}

int DataGrid::visibleRows () const
{ bool v, h; int bw, bh; bars (&v, &h, &bw, &bh); int rh = dg_rh (*this); int r = bh / (rh > 0 ? rh : 1); return r < 1 ? 1 : r; }

int DataGrid::colX (int c) const
{ int x = -left; for (int i = 0; i < c && i < m_ncol; i++) x += dg_w (*this, i); return x; }

int DataGrid::headAt (int mx) const
{
	int x = 1 - left;
	for (int c = 0; c < m_ncol; c++) { int w = dg_w (*this, c); if (w > 0 && mx >= x && mx < x + w) return c; x += w; }
	return -1;
}

int DataGrid::edgeAt (int mx) const
{
	int x = 1 - left;
	for (int c = 0; c < m_ncol; c++) { int w = dg_w (*this, c); x += w; if (w > 0 && mx >= x - 4 && mx <= x + 3) return c; }
	return -1;
}

int DataGrid::rowAt (int y) const
{
	bool v, h; int bw, bh; bars (&v, &h, &bw, &bh);
	int by = y - 1 - dg_hh (*this);
	if (by < 0 || by >= bh) return -1;
	int r = top + by / dg_rh (*this);
	return r < nrows ? r : -1;
}

void DataGrid::clampScroll ()
{
	bool v, h; int bw, bh; bars (&v, &h, &bw, &bh);
	int vis = bh / dg_rh (*this); if (vis < 1) vis = 1;
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

// ---- (P6) the adaptive part ---------------------------------------------------------------------------------
void DataGrid::setColumnRole (int c, int role, int priority)
{
	if (c < 0 || c >= MAXCOLS) return;
	DgExt *e = dg_make (*this);
	e->role[c] = (unsigned char) role; e->prio[c] = (unsigned char) (priority < 0 ? 0 : priority > 250 ? 250 : priority);
	invalidate (true);
}
bool DataGrid::columnShown (int c) { return c >= 0 && c < m_ncol && dg_w (*this, c) > 0; }
bool DataGrid::cards () { return dg_cards (*this); }

void DataGrid::setDetailView (Widget *w)
{
	DgExt *e = dg_make (*this);
	if (e->detail) removeChild (e->detail);
	e->detail = w; e->detailOn = false;
	if (w) { w->hidden = true; addChild (w); }
}
void DataGrid::closeDetail ()
{
	DgExt *e = dg_ext (*this);
	if (!e || !e->detailOn) return;
	e->detailOn = false;
	if (e->detail) e->detail->hidden = true;
	setFocus ();
	invalidate (true);
}
static void dg_open_detail (DataGrid &g)
{
	DgExt *e = dg_ext (g);
	if (!e || !e->detail || !dg_cards (g)) return;
	e->detailOn = true;
	e->detail->hidden = false;
	e->detail->left = 0; e->detail->top = 0;
	if (e->detail->width != g.width || e->detail->height != g.height) e->detail->resizeTo (g.width, g.height);
	e->detail->bringToFront ();
	g.invalidate (true);
}

void DataGrid::setMultiSelect (bool on) { dg_make (*this)->multi = on; invalidate (true); }
static bool dg_bit (DgExt *e, int r) { return e && r >= 0 && r < e->cap * 8 && (e->bits[r >> 3] >> (r & 7) & 1); }
static void dg_setbit (DgExt *e, int r, bool on)
{
	if (r < 0) return;
	if (r >= e->cap * 8)
	{
		int nc = (r / 8 + 1) * 2;
		unsigned char *nb = new unsigned char[nc];
		for (int i = 0; i < nc; i++) nb[i] = i < e->cap ? e->bits[i] : 0;
		delete [] e->bits; e->bits = nb; e->cap = nc;
	}
	if (on) e->bits[r >> 3] |= (unsigned char) (1 << (r & 7)); else e->bits[r >> 3] &= (unsigned char) ~(1 << (r & 7));
}
static void dg_clearbits (DgExt *e) { for (int i = 0; i < e->cap; i++) e->bits[i] = 0; }
bool DataGrid::isSelected (int r)
{
	DgExt *e = dg_ext (*this);
	if (!e || !e->multi) return r == sel && r >= 0;
	return dg_bit (e, r) || (r == sel && r >= 0 && selectedCount () == 0);
}
void DataGrid::setSelected (int r, bool on) { DgExt *e = dg_make (*this); if (r >= 0 && r < nrows) { dg_setbit (e, r, on); invalidate (true); } }
int DataGrid::selectedCount ()
{
	DgExt *e = dg_ext (*this);
	if (!e || !e->multi) return sel >= 0 ? 1 : 0;
	int n = 0;
	for (int r = 0; r < nrows && r < e->cap * 8; r++) if (dg_bit (e, r)) n++;
	return n;
}

// The columns that give way (pocket): the highest priorities first, until the rest fits the grid.
static void dg_room (DataGrid &g)
{
	DgExt *e = dg_ext (g);
	if (!e) return;
	for (int c = 0; c < DataGrid::MAXCOLS; c++) e->gone[c] = false;
	if (dg_regular () || dg_cards (g)) return;
	int room = g.width - 2, total = 0;
	for (int c = 0; c < g.columns (); c++) total += g.column (c).width;
	while (total > room)
	{
		int worst = -1;
		for (int c = 0; c < g.columns (); c++) if (!e->gone[c] && e->prio[c] > 0 && (worst < 0 || e->prio[c] >= e->prio[worst])) worst = c;
		if (worst < 0) break;
		e->gone[worst] = true; total -= g.column (worst).width;
	}
}

// Portrait: each row a card -- the primary column in bold, the secondary ones under it, dim.
static void dg_draw_cards (DataGrid &g, Canvas &bc, int bw, int bh)
{
	DgExt *e = dg_ext (g);
	int rh = dg_rh (g), fh = uk_fh ();
	char buf[256], sub[256];
	unsigned line = uk_mix (C_FIELD, C_FIELD_TEXT, 22);
	for (int r = g.top; r < g.nrows; r++)
	{
		int y = (r - g.top) * rh;
		if (y >= bh) break;
		bool s = g.isSelected (r);
		if (s) uk_hilite (bc, 4, y + 2, bw - 8, rh - 4, 6, g.hasFocus);
		unsigned ink = s ? uk_hilite_ink (g.hasFocus) : C_FIELD_TEXT, dim = s ? ink : uk_mix (C_FIELD, C_FIELD_TEXT, 150);
		int n = 0; sub[0] = 0;
		for (int c = 0; c < g.columns (); c++)
		{
			if (!e || e->role[c] == UK_COL_DETAIL || e->role[c] == UK_COL_NONE) continue;
			const char *t = g.cellText ? g.cellText (g, r, c, buf, sizeof buf) : "";
			if (e->role[c] == UK_COL_PRIMARY) { char b[256]; uk_text_fit (t ? t : "", bw - 28, b, sizeof b, 2); uk_text (bc, 14, y + 8, b, ink, 2); continue; }
			if (!t || !t[0]) continue;
			if (n > 0 && n < 250) { const char *dot = "  \xC2\xB7  "; for (int k = 0; dot[k] && n < 250; k++) sub[n++] = dot[k]; }
			for (int k = 0; t[k] && n < 250; k++) sub[n++] = t[k] < 32 ? ' ' : t[k];
			sub[n] = 0;
		}
		char b[256]; uk_text_fit (sub, bw - 28, b, sizeof b);
		uk_text (bc, 14, y + 10 + fh, b, dim);
		if (!s) bc.fillRect (14, y + rh - 1, bw - 28, 1, line);
	}
}

// ---- drawing ------------------------------------------------------------------------------------
void DataGrid::onDraw ()
{
	dg_room (*this);
	bool vbar, hbar; int bw, bh;
	bars (&vbar, &hbar, &bw, &bh);
	clampScroll ();
	if (!dg_regular ())					// (P6) pocket, console: the cards, the glowing rows
	{
		DgExt *e = dg_ext (*this);
		if (e && e->detailOn && !dg_cards (*this)) closeDetail ();
		if (dg_cards (*this))
		{
			canvas.clear (bgColor ());
			uk_sunken (canvas, 0, 0, width, height, 4, disabled ? uk_tone (C_FACE, 150) : C_FIELD, hasFocus && !disabled);
			Canvas bc; bc.adopt (canvas.px + canvas.stride + 1, bw, bh, canvas.stride);
			dg_draw_cards (*this, bc, bw, bh);
			if (nrows == 0 && emptyText) uk_text_c (bc, 0, 0, bw, rowH * 3 < bh ? rowH * 3 : bh, emptyText, uk_mix (C_FIELD, C_FIELD_TEXT, 130));
			if (vbar) { int rh = dg_rh (*this); UkThumb t = uk_thumb (nrows, bh / rh, top, bh); uk_scroll_bar (canvas, 1 + bw - UK_SBW, 1, UK_SBW, bh, true, t.y, t.show ? t.h : 0, C_FIELD, UK_NORMAL); }
			return;
		}
	}
	canvas.clear (bgColor ());
	uk_sunken (canvas, 0, 0, width, height, 4, disabled ? uk_tone (C_FACE, 150) : C_FIELD, hasFocus && !disabled);
	int st = canvas.stride;
	unsigned face = C_FACE, hink = uk_ink_for (uk_tone (face, 160));

	// the header: a raised strip, the titles in bold, the sort's arrow, etched lines between them
	Canvas hc; hc.adopt (canvas.px + st + 1, width - 2, headH, st);
	uk_rbox (hc, 0, 0, hc.w, headH, 3, uk_tone (face, 178), uk_tone (face, 146), 255, UK_TL | UK_TR);
	hc.fillRect (0, headH - 1, hc.w, 1, uk_tone (face, 100));
	for (int c = 0; c < m_ncol; c++)
	{
		int x = colX (c), w = dg_w (*this, c);
		if (x >= hc.w) break;
		if (x + w <= 0 || w <= 0) continue;
		bool hot = c == m_hotHead && sortable, down = m_drag == G_TITLE && m_dragCol == c;
		if (hot || down) uk_rbox (hc, x, 0, w, headH - 1, 0, uk_tone (face, down ? 150 : 192), uk_tone (face, down ? 140 : 160));
		bool arrow = c == sortCol;
		int tl = x + 8, tw = w - 16 - (arrow ? 14 : 0);
		int al = m_col[c].align == GRID_RIGHT && !arrow ? GRID_RIGHT : m_col[c].align == GRID_CENTRE ? GRID_CENTRE : GRID_LEFT;
		dg_text (hc, tl, 0, tw, headH - 1, m_col[c].title, hink, al, 2);
		if (arrow) uk_glyph (hc, sortDesc ? WKG_CHEV_DOWN : WKG_CHEV_UP, x + w - 13, headH / 2, 8, uk_mix (hink, C_ACCENT, 170));
		uk_etch_v (hc, x + w - 2, 5, headH - 11, face);
	}

	// the rows (clipped to the body)
	Canvas bc; bc.adopt (canvas.px + (1 + headH) * st + 1, bw, bh, st);
	int tw = totalWidth (), rowsW = tw - left < bw ? tw - left : bw;
	unsigned stripe = uk_mix (C_FIELD, C_FIELD_TEXT, 9), line = uk_mix (C_FIELD, C_FIELD_TEXT, 22);
	char buf[256];
	for (int r = top; r < nrows; r++)
	{
		int y = (r - top) * rowH;
		if (y >= bh) break;
		bool s = isSelected (r), hot = r == m_hotRow && !s;
		if (stripes && (r & 1)) bc.fillRect (0, y, rowsW, rowH, stripe);
		for (int c = 0; c < m_ncol; c++)		// the lines between the columns
		{
			if (dg_w (*this, c) <= 0) continue;
			int x = colX (c) + dg_w (*this, c) - 1;
			if (x >= 0 && x < bw) bc.fillRect (x, y, 1, rowH, line);
		}
		if (s && uk_size_class () == UK_SC_CONSOLE) { uk_rbox (bc, 1, y + 1, rowsW - 2, rowH - 2, 6, 0x002E5FA8, 0x00224A88); uk_rline (bc, 1, y + 1, rowsW - 2, rowH - 2, 6, 0x0060C8FF, 255); }
		else if (s) uk_hilite (bc, 1, y + 1, rowsW - 2, rowH - 2, 4, hasFocus);
		else if (hot) uk_rbox (bc, 1, y + 1, rowsW - 2, rowH - 2, 4, uk_mix (C_FIELD, C_ACCENT, 34), uk_mix (C_FIELD, C_ACCENT, 44));
		unsigned ink = disabled ? C_DIS : s ? (uk_size_class () == UK_SC_CONSOLE ? 0x00FFFFFF : uk_hilite_ink (hasFocus)) : C_FIELD_TEXT;
		for (int c = 0; c < m_ncol; c++)
		{
			int x = colX (c), w = dg_w (*this, c);
			if (x >= bw) break;
			if (x + w <= 0 || w <= 0) continue;
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
		uk_text_c (bc, 0, 0, bw, rowH * 3 < bh ? rowH * 3 : bh, emptyText, uk_mix (C_FIELD, C_FIELD_TEXT, 130));

	// the scroll bars
	int ov = dg_regular () ? 0 : UK_SBW + 1;		// (P6: pocket's overlay bars over the rows' edge)
	if (vbar)
	{
		UkThumb t = uk_thumb (nrows, bh / rowH, top, bh);
		uk_scroll_bar (canvas, 1 + bw + 1 - ov, 1 + headH, UK_SBW, bh, true, t.y, t.show ? t.h : 0, C_FIELD,
			       m_drag == G_VTHUMB ? UK_HOT : UK_NORMAL);
	}
	if (hbar)
	{
		UkThumb t = uk_thumb (tw, bw, left, bw);
		uk_scroll_bar (canvas, 1, 1 + headH + bh + 1 - ov, bw, UK_SBW, false, t.y, t.show ? t.h : 0, C_FIELD,
			       m_drag == G_HTHUMB ? UK_HOT : UK_NORMAL);
	}
}

// ---- the mouse ----------------------------------------------------------------------------------
bool DataGrid::onMouse (int mx, int my, int bl, int br, int, int wheel)
{
	if (!dg_regular ())					// (P6) the detail view, the cards
	{
		DgExt *e = dg_ext (*this);
		bool in = mx >= 0 && my >= 0 && mx < width && my < height;
		if (e && e->detailOn) return in;
		if (dg_cards (*this))
		{
			if (!in) { pressed = false; return false; }
			int rh = dg_rh (*this);
			if (wheel) { top -= wheel; clampScroll (); invalidate (true); return true; }
			int r = top + (my - 1) / rh;
			if (r >= nrows) r = -1;
			if (br && !m_rdown) { m_rdown = true; if (r >= 0) pick (r, true); ctxRow = r; ctxX = mx; ctxY = my; if (onContext) onContext (*this); return true; }
			if (!br) m_rdown = false;
			if (bl && !pressed) { pressed = true; setFocus (); if (r >= 0) pick (r, true); }
			else if (!bl && pressed)			// (a tap opens it)
			{
				pressed = false;
				if (r >= 0 && r == sel) { if (onActivate) onActivate (*this); dg_open_detail (*this); }
			}
			return true;
		}
		dg_room (*this);
	}
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
	if (m_drag == G_EDGE || (m_drag == G_NONE && inHead && edgeAt (mx) >= 0)) uk_cursor (KAPI_CURSOR_SIZE_H);	// a column's edge
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
			UkThumb t = uk_thumb (nrows, bh / rowH, top, bh);
			top = (int) uk_thumb_pos (my - 1 - headH, bh, nrows, bh / rowH, t.h);
		}
		else if (m_drag == G_HTHUMB)
		{
			int tw = totalWidth ();
			UkThumb t = uk_thumb (tw, bw, left, bw);
			left = (int) uk_thumb_pos (mx - 1, bw, tw, bw, t.h);
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
		UkThumb t = uk_thumb (nrows, bh / rowH, top, bh);
		top = (int) uk_thumb_pos (my - 1 - headH, bh, nrows, bh / rowH, t.h);
		clampScroll (); invalidate (true);
		return true;
	}
	if (hbar && my >= 1 + headH + bh && mx < 1 + bw)
	{
		m_drag = G_HTHUMB; catchOutside = true;
		int tw = totalWidth ();
		UkThumb t = uk_thumb (tw, bw, left, bw);
		left = (int) uk_thumb_pos (mx - 1, bw, tw, bw, t.h);
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
		DgExt *e = dg_ext (*this);
		if (e && e->multi)					// (P6) several rows: Ctrl one more, Shift a range
		{
			unsigned m = kapi_get_modifiers ();
			if (m & MOD_CTRL) { dg_setbit (e, hr, !dg_bit (e, hr)); e->anchor = hr; sel = hr; invalidate (true); if (onSelect) onSelect (*this); m_lastRow = -1; return true; }
			if ((m & MOD_SHIFT) && e->anchor >= 0)
			{
				dg_clearbits (e);
				for (int k = e->anchor < hr ? e->anchor : hr; k <= (e->anchor < hr ? hr : e->anchor); k++) dg_setbit (e, k, true);
				sel = hr; invalidate (true); if (onSelect) onSelect (*this); m_lastRow = -1; return true;
			}
			dg_clearbits (e); dg_setbit (e, hr, true); e->anchor = hr;
		}
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
	if (!dg_regular ())					// (P6) the detail view: Esc / Backspace / Left back to the cards
	{
		DgExt *e = dg_ext (*this);
		if (e && e->detailOn) { if (k == 27 || k == KEY_BACKSPACE || k == KEY_LEFT) { closeDetail (); return true; } return false; }
		if (dg_cards (*this) && k == KEY_ENTER) { if (sel >= 0) { if (onActivate) onActivate (*this); dg_open_detail (*this); } return true; }
	}
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

} // namespace uikit
