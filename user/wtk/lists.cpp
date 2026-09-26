//
// wtk/lists.cpp -- ListBox and TreeView (scrollable item lists with a vertical scrollbar).
//
#include "wtk/listbox.h"
#include "wtk/treeview.h"

namespace wtk {

static const unsigned L_SEL = 0x00355070, L_SELDIM = 0x003A4452, L_TXT = 0x00E0E6EE, L_DIM = 0x008A96A8;
#define DBL_TICKS	70			// double-click window (HZ ticks)

static int row_h () { return wk_fh () + 4; }

// ---- ListBox -----------------------------------------------------------------------------
ListBox::ListBox (int l, int t, int w, int h, Action onSelect_, Action onActivate_)
  : Widget (l, t, w, h), count (0), sel (-1), top (0), onSelect (onSelect_), onActivate (onActivate_),
    m_items (0), m_cap (0), m_lastClick (0), m_lastIdx (-1), m_thumb (false)
{ canFocus = true; }

ListBox::~ListBox () { delete [] m_items; }

void ListBox::add (const char *s)
{
	if (count == m_cap)
	{
		int nc = m_cap ? m_cap * 2 : 32;
		char (*n)[64] = new char[nc][64];
		for (int i = 0; i < count; i++) for (int k = 0; k < 64; k++) n[i][k] = m_items[i][k];
		delete [] m_items; m_items = n; m_cap = nc;
	}
	int i = 0; for (; s && s[i] && i < 63; i++) m_items[count][i] = s[i];
	m_items[count][i] = '\0';
	count++;
	invalidate (true);
}

void ListBox::clear () { count = 0; sel = -1; top = 0; invalidate (true); }
int  ListBox::rows () const { int r = (height - 4) / row_h (); return r < 1 ? 1 : r; }
void ListBox::scrollTo (int t)
{
	int mx = count - rows (); if (mx < 0) mx = 0;
	if (t > mx) t = mx;
	if (t < 0) t = 0;
	if (t != top) { top = t; invalidate (true); }
}
void ListBox::setSel (int i)
{
	if (i < -1 || i >= count) return;
	sel = i;
	if (sel >= 0 && sel < top) scrollTo (sel);
	if (sel >= top + rows ()) scrollTo (sel - rows () + 1);
	invalidate (true);
}
void ListBox::pick (int i, bool fire)
{
	if (i < 0 || i >= count) return;
	bool changed = i != sel;
	setSel (i);
	if (fire && changed && onSelect) onSelect (*this);
}

void ListBox::onDraw ()
{
	int fh = wk_fh (), rh = row_h (), R = rows ();
	WkThumb t = wk_thumb (count, R, top, height - 2);
	int tw = width - (t.show ? WK_SBW : 0);
	canvas.clear (C_FIELD);
	for (int r = 0; r < R && top + r < count; r++)
	{
		int i = top + r, y = 2 + r * rh;
		if (i == sel) canvas.fillRect (2, y, tw - 4, rh, hasFocus ? L_SEL : L_SELDIM);
		canvas.text (6, y + 2, m_items[i], disabled ? C_DIS : L_TXT);
	}
	if (t.show) wk_draw_vscroll (canvas, width - WK_SBW - 1, 1, WK_SBW, height - 2, t, C_FIELD, C_FACE);
	canvas.frameRect (0, 0, width, height, hasFocus ? C_ACCENT : C_BORDER);
	(void) fh;
}

bool ListBox::onMouse (int mx, int my, int bl, int, int, int wheel)
{
	if (mx < 0) { pressed = false; m_thumb = false; return false; }
	if (disabled) return true;
	if (wheel) { scrollTo (top - wheel); return true; }
	WkThumb t = wk_thumb (count, rows (), top, height - 2);
	if (m_thumb)
	{
		if (!bl) m_thumb = false;
		else scrollTo ((int) wk_thumb_pos (my - 1, height - 2, count, rows (), t.h));
		return true;
	}
	if (bl && !pressed)
	{
		pressed = true; setFocus ();
		if (t.show && mx >= width - WK_SBW - 1)
		{ m_thumb = true; scrollTo ((int) wk_thumb_pos (my - 1, height - 2, count, rows (), t.h)); return true; }
		int i = top + (my - 2) / row_h ();
		if (i >= count) return true;
		unsigned now = kapi_get_ticks ();
		bool dbl = i == m_lastIdx && now - m_lastClick < DBL_TICKS;
		pick (i, true);
		if (dbl && onActivate) { onActivate (*this); m_lastIdx = -1; }
		else { m_lastIdx = i; m_lastClick = now; }
	}
	else if (!bl) pressed = false;
	return true;
}

bool ListBox::onKey (long k)
{
	int R = rows ();
	switch (k)
	{
	case KEY_UP:   pick (sel <= 0 ? 0 : sel - 1, true); return true;
	case KEY_DOWN: pick (sel < 0 ? 0 : (sel + 1 < count ? sel + 1 : sel), true); return true;
	case KEY_PGUP: pick (sel - R < 0 ? 0 : sel - R, true); return true;
	case KEY_PGDN: pick (sel + R >= count ? count - 1 : sel + R, true); return true;
	case KEY_HOME: pick (0, true); return true;
	case KEY_END:  pick (count - 1, true); return true;
	case KEY_ENTER: if (sel >= 0 && onActivate) onActivate (*this); return true;
	}
	return false;
}

// ---- TreeView ----------------------------------------------------------------------------
TreeView::TreeView (int l, int t, int w, int h, Action onSelect_, Action onActivate_)
  : Widget (l, t, w, h), sel (-1), top (0), onSelect (onSelect_), onActivate (onActivate_),
    m_nodes (0), m_n (0), m_cap (0), m_vis (0), m_nvis (0), m_lastClick (0), m_lastRow (-1), m_thumb (false)
{ canFocus = true; }

TreeView::~TreeView () { delete [] m_nodes; delete [] m_vis; }

int TreeView::add (int parent, const char *label)
{
	if (m_n == m_cap)
	{
		int nc = m_cap ? m_cap * 2 : 32;
		Node *n = new Node[nc];
		for (int i = 0; i < m_n; i++) n[i] = m_nodes[i];
		delete [] m_nodes; m_nodes = n;
		int *v = new int[nc]; delete [] m_vis; m_vis = v;
		m_cap = nc;
	}
	Node &nd = m_nodes[m_n];
	int i = 0; for (; label && label[i] && i < 47; i++) nd.label[i] = label[i];
	nd.label[i] = '\0';
	nd.parent = (parent >= 0 && parent < m_n) ? parent : -1;
	nd.depth = nd.parent >= 0 ? m_nodes[nd.parent].depth + 1 : 0;
	nd.open = false; nd.data = 0;
	m_n++;
	rebuild ();
	return m_n - 1;
}

void TreeView::clear () { m_n = 0; m_nvis = 0; sel = -1; top = 0; invalidate (true); }

bool TreeView::hasChildren (int id) const
{
	for (int i = 0; i < m_n; i++) if (m_nodes[i].parent == id) return true;
	return false;
}

void TreeView::walk (int parent)			// depth-first, children in insertion order
{
	for (int i = 0; i < m_n; i++)
		if (m_nodes[i].parent == parent)
		{
			m_vis[m_nvis++] = i;
			if (m_nodes[i].open) walk (i);
		}
}

void TreeView::rebuild () { m_nvis = 0; walk (-1); invalidate (true); }

void TreeView::expand (int id, bool open)
{
	if (id < 0 || id >= m_n || m_nodes[id].open == open) return;
	m_nodes[id].open = open;
	if (!open && sel >= 0)				// a hidden selection moves up to the node
		for (int p = m_nodes[sel].parent; p >= 0; p = m_nodes[p].parent)
			if (p == id) { sel = id; break; }
	rebuild ();
}

int TreeView::rows () const { int r = (height - 4) / row_h (); return r < 1 ? 1 : r; }
int TreeView::rowOf (int id) const { for (int r = 0; r < m_nvis; r++) if (m_vis[r] == id) return r; return -1; }
void TreeView::scrollTo (int t)
{
	int mx = m_nvis - rows (); if (mx < 0) mx = 0;
	if (t > mx) t = mx;
	if (t < 0) t = 0;
	if (t != top) { top = t; invalidate (true); }
}
void TreeView::pick (int id, bool fire)
{
	if (id < 0 || id >= m_n) return;
	bool changed = id != sel;
	sel = id;
	int r = rowOf (id);
	if (r >= 0 && r < top) scrollTo (r);
	if (r >= top + rows ()) scrollTo (r - rows () + 1);
	invalidate (true);
	if (fire && changed && onSelect) onSelect (*this);
}

void TreeView::onDraw ()
{
	int fw = wk_fw (), rh = row_h (), R = rows ();
	WkThumb t = wk_thumb (m_nvis, R, top, height - 2);
	int tw = width - (t.show ? WK_SBW : 0);
	canvas.clear (C_FIELD);
	for (int r = 0; r < R && top + r < m_nvis; r++)
	{
		int id = m_vis[top + r], y = 2 + r * rh, x = 4 + m_nodes[id].depth * 16;
		if (id == sel) canvas.fillRect (2, y, tw - 4, rh, hasFocus ? L_SEL : L_SELDIM);
		if (hasChildren (id))				// [+] / [-]
		{
			int bs = 9, by = y + (rh - bs) / 2;
			canvas.frameRect (x, by, bs, bs, L_DIM);
			canvas.fillRect (x + 2, by + 4, 5, 1, L_TXT);
			if (!m_nodes[id].open) canvas.fillRect (x + 4, by + 2, 1, 5, L_TXT);
		}
		canvas.text (x + 14, y + 2, m_nodes[id].label, disabled ? C_DIS : L_TXT);
	}
	if (t.show) wk_draw_vscroll (canvas, width - WK_SBW - 1, 1, WK_SBW, height - 2, t, C_FIELD, C_FACE);
	canvas.frameRect (0, 0, width, height, hasFocus ? C_ACCENT : C_BORDER);
	(void) fw;
}

bool TreeView::onMouse (int mx, int my, int bl, int, int, int wheel)
{
	if (mx < 0) { pressed = false; m_thumb = false; return false; }
	if (disabled) return true;
	if (wheel) { scrollTo (top - wheel); return true; }
	WkThumb t = wk_thumb (m_nvis, rows (), top, height - 2);
	if (m_thumb)
	{
		if (!bl) m_thumb = false;
		else scrollTo ((int) wk_thumb_pos (my - 1, height - 2, m_nvis, rows (), t.h));
		return true;
	}
	if (bl && !pressed)
	{
		pressed = true; setFocus ();
		if (t.show && mx >= width - WK_SBW - 1)
		{ m_thumb = true; scrollTo ((int) wk_thumb_pos (my - 1, height - 2, m_nvis, rows (), t.h)); return true; }
		int r = top + (my - 2) / row_h ();
		if (r >= m_nvis) return true;
		int id = m_vis[r], x = 4 + m_nodes[id].depth * 16;
		if (hasChildren (id) && mx >= x - 2 && mx < x + 12) { expand (id, !m_nodes[id].open); return true; }
		unsigned now = kapi_get_ticks ();
		bool dbl = r == m_lastRow && now - m_lastClick < DBL_TICKS;
		pick (id, true);
		if (dbl)
		{
			if (hasChildren (id)) expand (id, !m_nodes[id].open);
			else if (onActivate) onActivate (*this);
			m_lastRow = -1;
		}
		else { m_lastRow = r; m_lastClick = now; }
	}
	else if (!bl) pressed = false;
	return true;
}

bool TreeView::onKey (long k)
{
	int r = rowOf (sel);
	switch (k)
	{
	case KEY_UP:   if (m_nvis) pick (m_vis[r <= 0 ? 0 : r - 1], true); return true;
	case KEY_DOWN: if (m_nvis) pick (m_vis[r < 0 ? 0 : (r + 1 < m_nvis ? r + 1 : r)], true); return true;
	case KEY_RIGHT: if (sel >= 0 && hasChildren (sel)) expand (sel, true); return true;
	case KEY_LEFT:
		if (sel >= 0 && m_nodes[sel].open) expand (sel, false);
		else if (sel >= 0 && m_nodes[sel].parent >= 0) pick (m_nodes[sel].parent, true);
		return true;
	case KEY_ENTER:
		if (sel >= 0 && hasChildren (sel)) expand (sel, !m_nodes[sel].open);
		else if (sel >= 0 && onActivate) onActivate (*this);
		return true;
	}
	return false;
}

} // namespace wtk
