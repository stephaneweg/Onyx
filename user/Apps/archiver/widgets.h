//
// archiver/widgets.h -- the Archiver's own widgets, drawn as the mock-ups show them: the toolbar of
// large buttons (an icon over its label), the path bar (back, up, the archive and its folders as
// links, the format's badge, the search field), the folder tree (the archive at its root), the list
// (columns sorted by a click on their title, several rows selected with Ctrl / Shift, a ratio bar, a
// drag out of it, a drop into it shown), the archive's summary card, the status bar, the welcome
// page (a drop zone, the formats, the recent archives). They call the app (main.cpp) through the
// app_* functions declared here.
//
#ifndef _archiver_widgets_h
#define _archiver_widgets_h

#include "uikit/uikit.h"
#include "uikit/toolbar.h"
#include "icons.h"
#include "model.h"

namespace ui {

using namespace uikit;

// ---- the app's side (main.cpp) -----------------------------------------------------------------------
void app_tool (int id);
void app_navigate (int node);
void app_activate (int item);
void app_context (int item, int wx, int wy);	// a right click (window coordinates)
void app_selection_changed ();
void app_drag_out ();
void app_back ();
void app_search (const char *q);
void app_open_recent (int i);
void app_welcome_button (int which);		// 0 open, 1 new
bool app_tool_enabled (int id);
extern Model g_model;
extern int   g_folder;				// the folder shown (a node)
extern TextFace *g_small;			// an 11-px face (the details)

static inline unsigned dim_ink () { return uk_mix (C_FIELD_TEXT, C_FIELD, 110); }
static inline unsigned dim_on (unsigned bg) { return uk_mix (uk_ink_for (bg), bg, 100); }
static inline int fh () { return uk_fh (); }
static inline void text_v (Canvas &cv, int x, int y, int h, const char *s, unsigned c, int style = 0)
{ uk_text (cv, x, y + (h - uk_fh ()) / 2, s, c, style); }
static inline void small_v (Canvas &cv, int x, int y, int h, const char *s, unsigned c, int style = 0)
{ UkFaceScope sc (g_small); uk_text (cv, x, y + (h - uk_fh ()) / 2, s, c, style); }
static inline int small_w (const char *s, int style = 0) { UkFaceScope sc (g_small); return uk_tw (s, style); }
static inline void fit (const char *s, int w, char *out, int cap, int style = 0) { uk_text_fit (s, w, out, cap, style); }
static inline bool dbl_click (unsigned &last, int &lastRow, int row)
{
	unsigned now = kapi_get_ticks ();
	bool d = row == lastRow && now - last < 50;
	last = d ? 0 : now; lastRow = row;
	return d;
}
// a dashed rounded outline (a drop target)
static inline void dashed (Canvas &cv, int x, int y, int w, int h, unsigned c)
{
	for (int i = x + 4; i < x + w - 4; i += 8) { cv.fillRect (i, y, 4, 2, c); cv.fillRect (i, y + h - 2, 4, 2, c); }
	for (int i = y + 4; i < y + h - 4; i += 8) { cv.fillRect (x, i, 2, 4, c); cv.fillRect (x + w - 2, i, 2, 4, c); }
}

// ---- the toolbar -----------------------------------------------------------------------------------------
class BigTool : public Widget
{
public:
	int id; char label[24]; bool m_down;
	BigTool (int l, int t, int w, int h, int id_, const char *s) : Widget (l, t, w, h), id (id_), m_down (false)
	{ arc::scopy (label, s, sizeof label); }
	void onDraw () override
	{
		canvas.clear (parent ? parent->bgColor () : C_BG);
		bool off = !app_tool_enabled (id);
		if (!off && (hover || m_down))
			uk_rbox (canvas, 0, 2, width, height - 4, 6, uk_tone (C_BG, m_down ? 150 : 200), uk_tone (C_BG, m_down ? 120 : 160), 200),
			uk_rline (canvas, 0, 2, width, height - 4, 6, uk_tone (C_BG, 90), 160);
		tool_icon (canvas, id, (width - 32) / 2, 6, off);
		UkFaceScope sc (g_small);
		int tw = uk_tw (label);
		uk_text (canvas, (width - tw) / 2, height - uk_fh () - 3, label, off ? C_DIS : C_TEXT);
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		bool in = mx >= 0 && my >= 0 && mx < width && my < height;
		if (in != hover) { hover = in; invalidate (true); }
		if (!in) { if (m_down && !bl) { m_down = false; invalidate (true); } return false; }
		if (!app_tool_enabled (id)) return true;
		if (bl && !m_down) { m_down = true; invalidate (true); }
		else if (!bl && m_down) { m_down = false; invalidate (true); app_tool (id); }
		return true;
	}
};

class ToolStrip : public Widget
{
public:
	int x; int sepX[8], nsep;
	ToolStrip (int l, int t, int w, int h) : Widget (l, t, w, h), x (8), nsep (0) {}
	void add (int id, const char *label)
	{
		int w = small_w (label) + 18; if (w < 58) w = 58;
		BigTool *b = new BigTool (x, 2, w, height - 6, id, label);
		addChild (b); x += w + 2;
	}
	void sep () { if (nsep < 8) sepX[nsep++] = x + 4; x += 10; }
	void refresh () { for (Widget *c = firstChild; c; c = c->nextSib) c->invalidate (true); }
	void onDraw () override
	{
		canvas.clear (C_BG);
		for (int i = 0; i < nsep; i++) uk_etch_v (canvas, sepX[i], 10, height - 22, C_BG);
		uk_etch_h (canvas, 0, height - 2, width, C_BG);
	}
};

// ---- the search field: a placeholder and a magnifier while empty ------------------------------------------
class SearchBox : public Textbox
{
public:
	SearchBox (int l, int t, int w, int h) : Textbox (l, t, w, h, "", 0) { maxLen = 120; padR = 22; }
	void onDraw () override
	{
		Textbox::onDraw ();
		unsigned dm = uk_mix (C_FIELD_TEXT, C_FIELD, 120);
		if (!text[0] && !hasFocus) small_v (canvas, 9, 0, height, TR ("Search in the archive"), dm);
		uk_tool_glyph (canvas, WKT_SEARCH, width - 22, (height - 14) / 2, 14, dm);
	}
};

// ---- the path bar --------------------------------------------------------------------------------------
class PathBar : public Widget
{
public:
	enum { MAXSEG = 16 };
	int segX[MAXSEG], segW[MAXSEG], segNode[MAXSEG], nseg;
	char badge[40];
	int hot, down;
	Textbox *search;
	PathBar (int l, int t, int w, int h) : Widget (l, t, w, h), nseg (0), hot (-1), down (-1)
	{
		badge[0] = 0;
		search = new SearchBox (w - 220, 4, 212, h - 8);
		search->anchor = ANCHOR_TOP | ANCHOR_RIGHT;
		addChild (search);
	}
	unsigned bgColor () override { return C_FIELD; }
	int partAt (int mx, int my)
	{
		if (my < 0 || my >= height) return -9;
		if (mx >= 4 && mx < 28) return -2;		// back
		if (mx >= 32 && mx < 56) return -3;		// up
		for (int i = 0; i < nseg; i++) if (mx >= segX[i] && mx < segX[i] + segW[i]) return i;
		return -9;
	}
	void onDraw () override
	{
		canvas.clear (parent ? parent->bgColor () : C_BG);
		uk_sunken (canvas, 0, 0, width, height, 6, C_FIELD);
		unsigned ink = C_FIELD_TEXT, link = uk_mix (C_ACCENT, 0x000000, 60);
		for (int b = 0; b < 2; b++)
		{
			int bx = 4 + b * 28;
			bool h = hot == -2 - b;
			uk_rbox (canvas, bx, 4, 24, height - 8, 4, uk_mix (C_FIELD, C_BG, h ? 200 : 110), uk_mix (C_FIELD, C_BG, h ? 230 : 150));
			uk_glyph (canvas, b ? WKG_CHEV_UP : WKG_CHEV_LEFT, bx + 12, height / 2, 12, ink);
		}
		int x = 66;
		nseg = 0;
		if (g_model.count == 0) return;
		int chain[MAXSEG], d = 0;
		for (int k = g_folder; k >= 0 && d < MAXSEG; k = g_model.n[k].parent) chain[d++] = k;
		icon_archive (canvas, x, (height - 18) / 2, 18); x += 24;
		int right = width - 230 - (badge[0] ? small_w (badge, 2) + 24 : 0);
		while (d--)
		{
			int k = chain[d]; bool last = d == 0;
			if (k)
			{
				uk_glyph (canvas, WKG_CHEV_RIGHT, x + 5, height / 2, 9, dim_ink ()); x += 14;
			}
			char t[96]; fit (g_model.n[k].name, right - x > 40 ? right - x : 40, t, sizeof t, last ? 2 : 0);
			int tw = uk_tw (t, last ? 2 : 0);
			if (nseg < MAXSEG) { segX[nseg] = x - 2; segW[nseg] = tw + 4; segNode[nseg] = k; nseg++; }
			text_v (canvas, x, 0, height, t, last ? ink : link, last ? 2 : 0);
			if (last) canvas.fillRect (x, height - 7, tw, 2, C_ACCENT);
			else if (hot == nseg - 1) canvas.fillRect (x, height - 8, tw, 1, link);
			x += tw + 6;
		}
		if (badge[0])
		{
			int bw = small_w (badge, 2) + 16, bx = width - 228 - bw;
			uk_rbox (canvas, bx, (height - 18) / 2, bw, 18, 9, uk_tone (C_ACCENT, 120), uk_tone (C_ACCENT, 104));
			small_v (canvas, bx + 8, (height - 18) / 2, 18, badge, C_SEL_TEXT, 2);
		}
	}
	bool wasL = false; int pressPart = -9;
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		int p = mx < 0 ? -9 : partAt (mx, my);
		if (p != hot) { hot = p; invalidate (true); }
		if (bl && !wasL) pressPart = p;
		if (!bl && wasL && pressPart == p && p != -9)
		{
			if (p == -2) app_back ();
			else if (p == -3) { if (g_folder > 0) app_navigate (g_model.n[g_folder].parent); }
			else if (p >= 0) app_navigate (segNode[p]);
		}
		wasL = bl != 0;
		return p != -9;
	}
};

// ---- the folder tree ----------------------------------------------------------------------------------
class FolderTree : public Widget
{
public:
	int rows[4096], nrows, top, rowH, hotRow, dropNode;
	unsigned lastT; int lastRow; bool wasDown;
	FolderTree (int l, int t, int w, int h) : Widget (l, t, w, h), nrows (0), top (0), hotRow (-1), dropNode (-1),
		lastT (0), lastRow (-1), wasDown (false) { rowH = fh () + 10; }
	unsigned bgColor () override { return C_FIELD; }
	void walk (int k)
	{
		if (nrows >= 4096) return;
		rows[nrows++] = k;
		const Node &x = g_model.n[k];
		if (!x.open) return;
		int *v; int m = g_model.list (k, &v);
		for (int i = 0; i < m; i++) if (g_model.n[v[i]].dir) walk (v[i]);
		free (v);
	}
	void rebuild ()
	{
		nrows = 0;
		if (g_model.count) walk (0);
		int vis = (height - 8) / rowH;
		if (top > nrows - vis) top = nrows - vis;
		if (top < 0) top = 0;
		invalidate (true);
	}
	void reveal (int node)			// open its parents, show its row
	{
		for (int k = g_model.n[node].parent; k >= 0; k = g_model.n[k].parent) g_model.n[k].open = true;
		rebuild ();
		int vis = (height - 8) / rowH;
		for (int i = 0; i < nrows; i++)
			if (rows[i] == node) { if (i < top) top = i; if (i >= top + vis) top = i - vis + 1; break; }
		invalidate (true);
	}
	bool hasDirKids (int k) { const Node &x = g_model.n[k]; for (int i = 0; i < x.nkids; i++) if (g_model.n[x.kids[i]].dir) return true; return false; }
	void onDraw () override
	{
		canvas.clear (parent ? parent->bgColor () : C_BG);
		uk_sunken (canvas, 0, 0, width, height, 5, C_FIELD);
		int vis = (height - 8) / rowH;
		for (int i = 0; i < vis && top + i < nrows; i++)
		{
			int k = rows[top + i], y = 4 + i * rowH, d = g_model.depth (k), x = 10 + d * 16;
			const Node &n = g_model.n[k];
			bool sel = k == g_folder;
			if (sel) uk_hilite (canvas, 3, y, width - 6, rowH - 2, 4, false);
			else if (top + i == hotRow) uk_rbox (canvas, 3, y, width - 6, rowH - 2, 4, uk_mix (C_FIELD, C_ACCENT, 30), uk_mix (C_FIELD, C_ACCENT, 30));
			if (k == dropNode) dashed (canvas, 3, y, width - 6, rowH - 2, C_ACCENT);
			if (k == 0 || hasDirKids (k))
				uk_glyph (canvas, n.open ? WKG_CHEV_DOWN : WKG_CHEV_RIGHT, x, y + rowH / 2 - 1, 9, dim_ink ());
			if (k == 0) icon_archive (canvas, x + 10, y + (rowH - 2 - 17) / 2, 17);
			else icon_folder (canvas, x + 10, y + (rowH - 2 - 17) / 2, 17);
			char t[80]; fit (n.name, width - x - 44, t, sizeof t, k == 0 || sel ? 2 : 0);
			text_v (canvas, x + 34, y, rowH - 2, t, C_FIELD_TEXT, k == 0 || sel ? 2 : 0);
		}
		if (nrows > vis)
		{
			UkThumb t = uk_thumb (nrows, vis, top, height - 8);
			uk_draw_vscroll (canvas, width - 11, 4, 8, height - 8, t, C_FIELD);
		}
	}
	int rowAt (int my) { int r = (my - 4) / rowH; return my < 4 || top + r >= nrows ? -1 : top + r; }
	bool onMouse (int mx, int my, int bl, int, int, int wheel) override
	{
		if (mx < 0) { if (hotRow >= 0) { hotRow = -1; invalidate (true); } wasDown = false; return false; }
		if (wheel) { top -= wheel * 3; rebuild (); return true; }
		int r = rowAt (my);
		if (r != hotRow) { hotRow = r; invalidate (true); }
		if (bl && !wasDown && r >= 0)
		{
			int k = rows[r], x = 10 + g_model.depth (k) * 16;
			if (mx >= x - 8 && mx < x + 9 && (k == 0 || hasDirKids (k))) { g_model.n[k].open = !g_model.n[k].open; rebuild (); }
			else if (dbl_click (lastT, lastRow, r)) { g_model.n[k].open = !g_model.n[k].open; rebuild (); }
			else app_navigate (k);
		}
		wasDown = bl != 0;
		return true;
	}
};

// ---- the summary card -----------------------------------------------------------------------------------
class InfoCard : public Widget
{
public:
	enum { MAXR = 9 };
	char k[MAXR][24], v[MAXR][48]; int n;
	InfoCard (int l, int t, int w, int h) : Widget (l, t, w, h), n (0) {}
	void clear () { n = 0; invalidate (true); }
	void row (const char *a, const char *b) { if (n < MAXR) { arc::scopy (k[n], a, 24); arc::scopy (v[n], b, 48); n++; } invalidate (true); }
	void onDraw () override
	{
		canvas.clear (parent ? parent->bgColor () : C_BG);
		if (!n) return;
		unsigned bg = uk_mix (C_BG, C_FIELD, 120);
		uk_rbox (canvas, 0, 0, width, height, 6, bg, bg);
		uk_rline (canvas, 0, 0, width, height, 6, uk_tone (C_BG, 90), 160);
		small_v (canvas, 12, 6, 18, TR ("Archive"), dim_on (bg), 2);
		UkFaceScope sc (g_small);
		for (int i = 0; i < n; i++)
		{
			int y = 28 + i * 19;
			uk_text (canvas, 12, y, k[i], dim_on (bg));
			char t[48]; uk_text_fit (v[i], width - 110, t, sizeof t, 2);
			uk_text (canvas, width - 12 - uk_tw (t, 2), y, t, uk_ink_for (bg), 2);
		}
	}
};

// ---- the list ----------------------------------------------------------------------------------------------
enum { COL_NAME, COL_SIZE, COL_PACKED, COL_RATIO, COL_TIME, COL_METHOD, NCOL };
class EntryList : public Widget
{
public:
	int  *items; int nitems;		// nodes (-1: the parent folder's row)
	char *sel;				// selected, item by item
	int   anchor, cursor, top, rowH, headH, hotRow, hotHead;
	int   colW[NCOL];
	bool  search;				// a search's results: names with their path
	int   dropRow;			// drop target: -1 none, -2 the whole list, else the row
	char  dropText[160];
	const char *emptyText;
	unsigned lastT; int lastRow;
	bool  wasL, wasR, dragging, thumb;
	int   pressX, pressY, pressRow;

	EntryList (int l, int t, int w, int h) : Widget (l, t, w, h), items (0), nitems (0), sel (0), anchor (-1), cursor (-1),
		top (0), hotRow (-1), hotHead (-1), search (false), dropRow (-1), emptyText (0), lastT (0), lastRow (-1),
		wasL (false), wasR (false), dragging (false), thumb (false), pressX (0), pressY (0), pressRow (-1)
	{
		rowH = fh () + 10; headH = fh () + 10; canFocus = true; dropText[0] = 0;
		int w0[NCOL] = { 0, 84, 84, 104, 136, 92 };
		for (int i = 0; i < NCOL; i++) colW[i] = w0[i];
	}
	unsigned bgColor () override { return C_FIELD; }
	void set (int *v, int m, bool isSearch)
	{
		free (items); free (sel);
		bool up = !isSearch && g_folder > 0;
		nitems = m + (up ? 1 : 0);
		items = (int *) malloc (sizeof (int) * (nitems ? nitems : 1));
		if (up) items[0] = -1;
		for (int i = 0; i < m; i++) items[i + (up ? 1 : 0)] = v[i];
		sel = (char *) calloc ((size_t) (nitems ? nitems : 1), 1);
		anchor = cursor = -1; top = 0; search = isSearch;
		invalidate (true);
	}
	int  visRows () { return (height - headH - 6) / rowH; }
	int  nameW () { int w = width - 14 - 10; for (int i = 1; i < NCOL; i++) w -= colW[i]; return w < 120 ? 120 : w; }
	int  colX (int c) { int x = 10; if (c > 0) x += nameW (); for (int i = 1; i < c; i++) x += colW[i]; return x; }
	int  rowAt (int my) { if (my < headH + 2) return -1; int r = top + (my - headH - 2) / rowH; return r < nitems ? r : -1; }
	int  selCount () { int c = 0; for (int i = 0; i < nitems; i++) if (sel[i] && items[i] >= 0) c++; return c; }
	void ensure (int r)
	{
		int v = visRows ();
		if (r < top) top = r;
		if (r >= top + v) top = r - v + 1;
		if (top > nitems - v) top = nitems - v;
		if (top < 0) top = 0;
	}
	void selectOnly (int r) { memset (sel, 0, (size_t) nitems); if (r >= 0 && r < nitems) sel[r] = items[r] >= 0; anchor = cursor = r; }
	void selectAll () { for (int i = 0; i < nitems; i++) sel[i] = items[i] >= 0; invalidate (true); app_selection_changed (); }
	void invert () { for (int i = 0; i < nitems; i++) sel[i] = items[i] >= 0 && !sel[i]; invalidate (true); app_selection_changed (); }
	void selectNode (int node) { for (int i = 0; i < nitems; i++) if (items[i] == node) { selectOnly (i); ensure (i); invalidate (true); return; } }

	void cell (int c, int node, char *out, int cap)
	{
		const Node &n = g_model.n[node];
		out[0] = 0;
		switch (c)
		{
		case COL_SIZE:
			if (n.dir) { char b[16]; arc::u64_str ((arc::u64) n.files, b, sizeof b); arc::scopy (out, b, cap); arc::scat (out, n.files == 1 ? TR (" file") : TR (" files"), cap); }
			else arc::human_size (n.size, out, cap);
			break;
		case COL_PACKED: if (!n.dir) arc::human_size (n.packed, out, cap); break;
		case COL_TIME: arc::dos_str (n.time, out, cap); break;
		case COL_METHOD: if (n.entry >= 0 && !n.dir) arc::scopy (out, g_model.a->methodName (g_model.a->e[n.entry]), cap); break;
		}
	}
	void onDraw () override
	{
		canvas.clear (parent ? parent->bgColor () : C_BG);
		uk_sunken (canvas, 0, 0, width, height, 5, C_FIELD, false);
		// the header
		unsigned hd = uk_mix (C_FIELD, C_BG, 150);
		uk_rbox (canvas, 1, 1, width - 2, headH, 4, uk_mix (C_FIELD, C_BG, 90), hd, 255, UK_TL | UK_TR);
		canvas.fillRect (1, headH + 1, width - 2, 1, uk_tone (C_BG, 110));
		static const char *title[NCOL] = { TR ("Name"), TR ("Size"), TR ("Packed"), TR ("Ratio"), TR ("Modified"), TR ("Method") };
		static const int scol[NCOL] = { SORT_NAME, SORT_SIZE, SORT_PACKED, SORT_RATIO, SORT_TIME, SORT_METHOD };
		for (int c = 0; c < NCOL; c++)
		{
			int x = colX (c), w = c ? colW[c] : nameW ();
			bool right = c == COL_SIZE || c == COL_PACKED;
			unsigned ink = dim_on (hd);
			int tw = small_w (title[c], 2);
			int tx = right ? x + w - 14 - tw : x;
			if (c == hotHead) canvas.fillRect (x - 8, 2, w, headH - 2, uk_mix (hd, 0xFFFFFF, 70));
			small_v (canvas, tx, 1, headH, title[c], ink, 2);
			if (g_model.sortCol == scol[c])
				uk_glyph (canvas, g_model.sortDesc ? WKG_CHEV_UP : WKG_CHEV_DOWN, right ? tx - 9 : tx + tw + 9, headH / 2 + 1, 8, ink);
			if (c) canvas.fillRect (x - 8, 7, 1, headH - 12, uk_tone (hd, 100));
		}
		// the rows
		int v = visRows (), y0 = headH + 3;
		if (!nitems && emptyText) { UkFaceScope sc (g_small); uk_text (canvas, (width - uk_tw (emptyText)) / 2, y0 + 30, emptyText, dim_ink ()); }
		for (int i = 0; i < v && top + i < nitems; i++)
		{
			int r = top + i, y = y0 + i * rowH, node = items[r];
			bool s = sel[r];
			unsigned ink = C_FIELD_TEXT, dm = dim_ink ();
			if (s) { uk_hilite (canvas, 3, y, width - 18, rowH - 2, 4, hasFocus); ink = uk_hilite_ink (hasFocus); dm = hasFocus ? uk_mix (ink, C_ACCENT, 60) : dm; }
			else if (r == hotRow) uk_rbox (canvas, 3, y, width - 18, rowH - 2, 4, uk_mix (C_FIELD, C_ACCENT, 26), uk_mix (C_FIELD, C_ACCENT, 26));
			else if (r & 1) canvas.fillRect (3, y, width - 18, rowH - 2, uk_mix (C_FIELD, 0x000000, 6));
			if (r == dropRow) dashed (canvas, 3, y, width - 18, rowH - 2, C_ACCENT);
			if (r == cursor && hasFocus && !s) uk_rline (canvas, 3, y, width - 18, rowH - 2, 4, C_ACCENT, 120);
			int x = 10;
			if (node < 0)
			{
				uk_glyph (canvas, WKG_UP, x + 9, y + rowH / 2 - 1, 12, dm);
				text_v (canvas, x + 26, y, rowH - 2, TR ("..  (parent folder)"), dm);
				continue;
			}
			const Node &n = g_model.n[node];
			int iy = y + (rowH - 2 - 18) / 2;
			if (n.dir) icon_folder (canvas, x, iy, 18); else icon_file (canvas, x, iy, 18, kind_colour (n.name));
			char nm[300];
			if (search) g_model.pathOf (node, nm, sizeof nm); else arc::scopy (nm, n.name, sizeof nm);
			if (n.dir) arc::scat (nm, "/", sizeof nm);
			char t[160]; fit (nm, nameW () - 36, t, sizeof t, n.dir ? 2 : 0);
			text_v (canvas, x + 26, y, rowH - 2, t, ink, n.dir ? 2 : 0);
			if (n.entry >= 0 && g_model.a->e[n.entry].encrypted) uk_glyph (canvas, WKG_LOCK, x + 30 + uk_tw (t, n.dir ? 2 : 0) + 6, y + rowH / 2 - 1, 11, dm);
			for (int c = 1; c < NCOL; c++)
			{
				int cx = colX (c), w = colW[c];
				if (c == COL_RATIO)
				{
					if (n.dir || !n.size) continue;
					int ra = g_model.ratio (node);
					int bw = 52, by = y + (rowH - 2) / 2 - 3;
					uk_rbox (canvas, cx, by, bw, 7, 3, uk_mix (s ? C_ACCENT : C_FIELD, 0x000000, 30), uk_mix (s ? C_ACCENT : C_FIELD, 0x000000, 30));
					if (ra > 0) uk_rbox (canvas, cx, by, 6 + (bw - 6) * ra / 1000, 7, 3, s && hasFocus ? 0xFFFFFF : 0x5AA07A, s && hasFocus ? 0xE8F0F4 : 0x4A9070);
					char p[12]; arc::u64_str ((arc::u64) ((ra + 5) / 10), p, sizeof p); arc::scat (p, " %", sizeof p);
					small_v (canvas, cx + bw + 6, y, rowH - 2, p, dm);
					continue;
				}
				char b[48]; cell (c, node, b, sizeof b);
				if (!b[0]) continue;
				char t2[48]; fit (b, w - 12, t2, sizeof t2);
				if (c == COL_SIZE || c == COL_PACKED) text_v (canvas, cx + w - 14 - uk_tw (t2), y, rowH - 2, t2, c == COL_SIZE && !n.dir ? ink : dm);
				else small_v (canvas, cx, y, rowH - 2, t2, dm);
			}
		}
		if (nitems > v)
		{
			UkThumb t = uk_thumb (nitems, v, top, height - headH - 8);
			uk_draw_vscroll (canvas, width - 13, headH + 4, 9, height - headH - 8, t, C_FIELD, thumb);
		}
		if (dropRow != -1 && dropText[0])
		{	// the drop's banner
			int bw = width - 80, bx = 40, by = height - 76;
			unsigned bg = uk_mix (C_FIELD, C_BG, 60);
			dashed (canvas, 3, headH + 3, width - 18, height - headH - 7, C_ACCENT);
			uk_rbox (canvas, bx, by, bw, 60, 10, bg, bg);
			uk_rline (canvas, bx, by, bw, 60, 10, C_ACCENT, 255);
			uk_rline (canvas, bx + 1, by + 1, bw - 2, 58, 9, C_ACCENT, 140);
			icon_archive (canvas, bx + 16, by + 13, 34);
			small_v (canvas, bx + 64, by + 8, 20, TR ("Drop to add to"), dim_on (bg));
			char t[160]; fit (dropText, bw - 80, t, sizeof t, 2);
			text_v (canvas, bx + 64, by + 28, 22, t, uk_mix (C_ACCENT, 0x000000, 70), 2);
		}
	}
	void click (int r)
	{
		unsigned m = kapi_get_modifiers ();
		if ((m & MOD_SHIFT) && anchor >= 0)
		{
			if (!(m & MOD_CTRL)) memset (sel, 0, (size_t) nitems);
			int a = anchor < r ? anchor : r, b = anchor < r ? r : anchor;
			for (int i = a; i <= b; i++) sel[i] = items[i] >= 0;
			cursor = r;
		}
		else if (m & MOD_CTRL) { sel[r] = items[r] >= 0 && !sel[r]; anchor = cursor = r; }
		else if (!sel[r]) selectOnly (r);
		else cursor = r;
		invalidate (true); app_selection_changed ();
	}
	bool onMouse (int mx, int my, int bl, int br, int, int wheel) override
	{
		if (mx < 0 && !wasL)
		{
			if (hotRow >= 0 || hotHead >= 0) { hotRow = hotHead = -1; invalidate (true); }
			wasR = false; return false;
		}
		if (wheel) { top -= wheel * 3; ensure (top < 0 ? 0 : top); int v = visRows (); if (top > nitems - v) top = nitems - v; if (top < 0) top = 0; invalidate (true); return true; }
		int v = visRows ();
		bool onBar = nitems > v && mx >= width - 16;
		int r = rowAt (my);
		int hh = my >= 1 && my < headH + 1 ? 0 : -1;
		int hc = -1;
		if (!hh) for (int c = 0; c < NCOL; c++) { int x = colX (c) - 8, w = c ? colW[c] : nameW () + 8; if (mx >= x && mx < x + w) hc = c; }
		if (!wasL && (r != hotRow || hc != hotHead)) { hotRow = r; hotHead = hc; invalidate (true); }
		if (bl && !wasL)
		{
			setFocus ();
			catchOutside = true; pressX = mx; pressY = my; pressRow = -1; dragging = false; thumb = false;
			if (onBar) { thumb = true; }
			else if (hc >= 0)
			{
				static const int scol[NCOL] = { SORT_NAME, SORT_SIZE, SORT_PACKED, SORT_RATIO, SORT_TIME, SORT_METHOD };
				if (g_model.sortCol == scol[hc]) g_model.sortDesc = !g_model.sortDesc; else { g_model.sortCol = scol[hc]; g_model.sortDesc = hc != COL_NAME; }
				app_navigate (-2);			// (the same folder, sorted again)
			}
			else if (r >= 0)
			{
				pressRow = r;
				if (dbl_click (lastT, lastRow, r)) { selectOnly (r); invalidate (true); app_activate (r); wasL = true; return true; }
				click (r);
			}
			else { memset (sel, 0, (size_t) nitems); invalidate (true); app_selection_changed (); }
		}
		else if (bl && wasL)
		{
			if (thumb)
			{
				UkThumb t = uk_thumb (nitems, v, top, height - headH - 8);
				top = (int) uk_thumb_pos (my - headH - 4, height - headH - 8, nitems, v, t.h);
				invalidate (true);
			}
			else if (pressRow >= 0 && !dragging && (mx - pressX) * (mx - pressX) + (my - pressY) * (my - pressY) > 49 && selCount ())
			{ dragging = true; app_drag_out (); }
		}
		else if (!bl && wasL) { catchOutside = false; thumb = false; dragging = false; invalidate (true); }
		if (br && !wasR && r >= 0 && my >= headH)
		{
			setFocus ();
			if (!sel[r]) selectOnly (r);
			invalidate (true); app_selection_changed ();
			app_context (r, mx, my);
		}
		else if (br && !wasR && r < 0 && my >= headH) app_context (-1, mx, my);
		wasL = bl != 0; wasR = br != 0;
		return true;
	}
	bool onKey (long k) override
	{
		if (!nitems) return false;
		int r = cursor < 0 ? 0 : cursor, v = visRows ();
		switch (k)
		{
		case KEY_UP: r--; break; case KEY_DOWN: r++; break;
		case KEY_PGUP: r -= v; break; case KEY_PGDN: r += v; break;
		case KEY_HOME: r = 0; break; case KEY_END: r = nitems - 1; break;
		case KEY_ENTER: if (cursor >= 0) app_activate (cursor); return true;
		case ' ': if (cursor >= 0 && items[cursor] >= 0) { sel[cursor] = !sel[cursor]; invalidate (true); app_selection_changed (); } return true;
		default: return false;
		}
		if (r < 0) r = 0; if (r >= nitems) r = nitems - 1;
		unsigned m = kapi_get_modifiers ();
		if (m & MOD_SHIFT)
		{
			if (anchor < 0) anchor = cursor < 0 ? r : cursor;
			memset (sel, 0, (size_t) nitems);
			int a = anchor < r ? anchor : r, b = anchor < r ? r : anchor;
			for (int i = a; i <= b; i++) sel[i] = items[i] >= 0;
			cursor = r;
		}
		else if (m & MOD_CTRL) cursor = r;
		else selectOnly (r);
		ensure (r); invalidate (true); app_selection_changed ();
		return true;
	}
};

// ---- the status bar ------------------------------------------------------------------------------------
class StatusBar : public Widget
{
public:
	char left[160], right[160]; int busy;		// busy: a job's percent (-1 none)
	unsigned dot;
	StatusBar (int l, int t, int w, int h) : Widget (l, t, w, h), busy (-1), dot (0x4EA05C) { left[0] = right[0] = 0; }
	void set (const char *l, const char *r) { arc::scopy (left, l, sizeof left); arc::scopy (right, r, sizeof right); invalidate (true); }
	void onDraw () override
	{
		canvas.clear (C_BG);
		uk_etch_h (canvas, 0, 0, width, C_BG);
		VPath p; p.circle (V (12), V (height / 2 + 1), V (4)); p.fill (canvas, dot);
		text_v (canvas, 22, 1, height, left, C_TEXT);
		int x = width - 10;
		if (right[0]) { x -= uk_tw (right); text_v (canvas, x, 1, height, right, uk_mix (C_TEXT, C_BG, 100)); x -= 14; }
		if (busy >= 0)
		{
			int w = 120; x -= w;
			uk_progress_bar (canvas, x, height / 2 - 4, w, 9, busy);
		}
	}
};

// ---- the welcome page -----------------------------------------------------------------------------------
class Welcome : public Widget
{
public:
	enum { MAXREC = 6 };
	char rec[MAXREC][300]; arc::u64 recSize[MAXREC]; int nrec, hot, down;
	bool dropHot;
	int bx[2], by, bw, bh, ry0;
	Welcome (int l, int t, int w, int h) : Widget (l, t, w, h), nrec (0), hot (-1), down (-1), dropHot (false) {}
	unsigned bgColor () override { return C_FIELD; }
	void onDraw () override
	{
		canvas.clear (parent ? parent->bgColor () : C_BG);
		uk_sunken (canvas, 0, 0, width, height, 6, C_FIELD);
		int mx = width / 2, zw = 520 < width - 40 ? 520 : width - 40, zx = mx - zw / 2, zy = 30, zh = 196;
		uk_rbox (canvas, zx, zy, zw, zh, 12, uk_mix (C_FIELD, C_ACCENT, dropHot ? 60 : 22), uk_mix (C_FIELD, C_ACCENT, dropHot ? 70 : 30));
		dashed (canvas, zx, zy, zw, zh, C_ACCENT);
		icon_archive (canvas, mx - 30, zy + 18, 60);
		const char *a = dropHot ? TR ("Drop it here") : TR ("Drop an archive here to open it");
		text_v (canvas, mx - uk_tw (a, 2) / 2, zy + 86, 24, a, C_FIELD_TEXT, 2);
		const char *b = TR ("or files and folders to make a new one");
		small_v (canvas, mx - small_w (b) / 2, zy + 110, 20, b, dim_ink ());
		bw = 180; bh = 30; by = zy + 144; bx[0] = mx - bw - 8; bx[1] = mx + 8;
		static const char *lab[2] = { TR ("Open an Archive..."), TR ("New Archive...") };
		for (int i = 0; i < 2; i++)
		{
			int st = (hot == 100 + i) ? (down == 100 + i ? UK_PRESSED : UK_HOT) : UK_NORMAL;
			if (i == 0) { uk_rbox (canvas, bx[i], by, bw, bh, 6, uk_tone (C_ACCENT, st == UK_HOT ? 150 : 136), uk_tone (C_ACCENT, st == UK_PRESSED ? 100 : 112)); uk_rline (canvas, bx[i], by, bw, bh, 6, uk_tone (C_ACCENT, 70), 200); }
			else uk_raised (canvas, bx[i], by, bw, bh, 6, C_BUTTON, st);
			unsigned ink = i == 0 ? C_SEL_TEXT : C_BUTTON_TEXT;
			text_v (canvas, bx[i] + (bw - uk_tw (lab[i], i == 0 ? 2 : 0)) / 2, by, bh, lab[i], ink, i == 0 ? 2 : 0);
		}
		// the formats
		int fy = zy + zh + 28;
		small_v (canvas, mx - small_w (TR ("FORMATS"), 2) / 2, fy, 16, TR ("FORMATS"), dim_ink (), 2);
		// (asked from FileKit: what it reads is listed, what it also writes in green)
		int nf = arc::formats (); if (nf > 6) nf = 6;
		int cw[6], tot = 0;
		for (int i = 0; i < nf; i++) { int w1 = uk_tw (arc::g_formats[i].name, 2), w2 = small_w (arc::g_formats[i].note); cw[i] = (w1 > w2 ? w1 : w2) + 34; tot += cw[i] + 10; }
		int fx = mx - tot / 2;
		for (int i = 0; i < nf; i++)
		{
			const struct fk_format &f = arc::g_formats[i];
			uk_rbox (canvas, fx, fy + 24, cw[i], 50, 8, 0xFFFFFF, uk_mix (0xFFFFFF, C_FIELD, 128));
			uk_rline (canvas, fx, fy + 24, cw[i], 50, 8, uk_tone (C_FIELD, 100), 200);
			VPath p; p.circle (V (fx + 14), V (fy + 38), V (4)); p.fill (canvas, f.can_write ? 0x4EA05C : 0x4E86C8);
			text_v (canvas, fx + 24, fy + 28, 20, f.name, C_FIELD_TEXT, 2);
			small_v (canvas, fx + 14, fy + 50, 18, f.note, dim_ink ());
			fx += cw[i] + 10;
		}
		// the recent archives
		ry0 = fy + 100;
		if (nrec)
		{
			int lx = mx - 330 < 20 ? 20 : mx - 330, lw = width - 2 * lx;
			small_v (canvas, lx + 4, ry0, 16, TR ("RECENT"), dim_ink (), 2);
			for (int i = 0; i < nrec && ry0 + 22 + i * 38 + 34 < height; i++)
			{
				int y = ry0 + 22 + i * 38;
				unsigned bg = hot == i ? uk_mix (C_FIELD, C_ACCENT, 40) : uk_mix (C_FIELD, 0x000000, 8);
				uk_rbox (canvas, lx, y, lw, 34, 6, bg, bg);
				icon_archive (canvas, lx + 12, y + 7, 20);
				char t[120]; fit (arc::base_of (rec[i]), lw / 3, t, sizeof t, 2);
				text_v (canvas, lx + 42, y, 34, t, C_FIELD_TEXT, 2);
				char dir[300]; arc::scopy (dir, rec[i], sizeof dir);
				char *sl = strrchr (dir, '/'); if (sl) *sl = 0;
				fit (dir, lw / 2 - 20, t, sizeof t);
				small_v (canvas, lx + 42 + lw / 3 + 10, y, 34, t, dim_ink ());
				if (recSize[i]) { char s[24]; arc::human_size (recSize[i], s, sizeof s); small_v (canvas, lx + lw - 14 - small_w (s), y, 34, s, dim_ink ()); }
			}
		}
	}
	int partAt (int mx, int my)
	{
		for (int i = 0; i < 2; i++) if (mx >= bx[i] && mx < bx[i] + bw && my >= by && my < by + bh) return 100 + i;
		int lx = width / 2 - 330 < 20 ? 20 : width / 2 - 330;
		for (int i = 0; i < nrec; i++) { int y = ry0 + 22 + i * 38; if (mx >= lx && mx < width - lx && my >= y && my < y + 34) return i; }
		return -1;
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		int p = mx < 0 ? -1 : partAt (mx, my);
		if (p != hot) { hot = p; invalidate (true); }
		if (bl && down == -1) { down = p; invalidate (true); }
		else if (!bl && down != -1)
		{
			int was = down; down = -1; invalidate (true);
			if (was == p) { if (p >= 100) app_welcome_button (p - 100); else if (p >= 0) app_open_recent (p); }
		}
		if (!bl) down = -1;
		return mx >= 0;
	}
};

} // namespace ui

#endif
