//
// uikit/datagrid.h -- DataGrid: a read-only table of rows and columns (WPF's DataGrid, a file
// manager's details view, a database's list). Virtual: the app says how many rows there are and
// gives each cell's text (cellText) -- or draws the cell itself (cellDraw: a colour swatch, a
// check mark...). On top, the header: the columns' titles in bold; a click on a title asks the app
// to sort (onSort, clickedCol: the app orders its rows, the grid shows sortCol / sortDesc as an
// arrow); a drag on a title's right edge resizes its column. Below, the rows: every other one a
// shade darker (stripes), a light line between the columns, the one under the pointer tinted, the
// selected one in the accent (dimmer while the grid has not the focus); both scroll bars when the
// rows or the columns do not fit (their thumbs drag, a click on the groove jumps there).
//   Mouse: a click selects (onSelect), a double click activates (onActivate), a right click
//   selects the row under it and asks for a menu (onContext: ctxRow -- -1 below the rows --,
//   ctxX, ctxY: where, in the grid's coordinates); the wheel scrolls (with Shift: sideways).
//   Keys (focused): Up / Down / Page Up / Page Down / Home / End move the selection (onSelect),
//   Left / Right scroll sideways, Enter activates; the others go to the parent (Delete...).
// A cell's text is cut to its column with "..." (the app's text: control characters shown as
// spaces). Integer only, as the rest of uikit.
//
//   DataGrid *g = new DataGrid (0, 0, 600, 400);
//   g->setColumns (2);
//   g->setColumn (0, "Name", 200); g->setColumn (1, "Size", 90, GRID_RIGHT);
//   g->cellText = my_text;         // const char *my_text (DataGrid &g, int row, int col, char *buf, int cap)
//   g->setRows (n);
//
#ifndef _uikit_datagrid_h
#define _uikit_datagrid_h

#include "uikit/widget.h"

namespace uikit {

enum { GRID_LEFT = 0, GRID_RIGHT = 1, GRID_CENTRE = 2 };

class DataGrid : public Widget
{
public:
	enum { MAXCOLS = 64 };
	struct Column { char title[48]; int width, align; };
	// A cell's text: written into buf (cap bytes) and returned, or a constant string returned.
	typedef const char *(*CellText) (DataGrid &g, int row, int col, char *buf, int cap);
	// A cell drawn by the app: cv is clipped to the cell, whose box is (x, y, w, h) there; ink is
	// the text's colour (on the selection or not). Return false to have the text drawn instead.
	typedef bool (*CellDraw) (DataGrid &g, Canvas &cv, int row, int col, int x, int y, int w, int h,
				  unsigned ink, bool selected);

	int  nrows, sel, top, left;		// the rows; the selected one (-1 none); the first shown; px scrolled sideways
	int  rowH, headH;			// (default: the font's height + 8, + 10)
	int  sortCol; bool sortDesc;		// the title showing the sort's arrow (-1 none)
	bool sortable, stripes;			// titles clickable (onSort); every other row shaded
	CellText cellText; CellDraw cellDraw;
	Action onSelect, onActivate, onSort, onContext;
	int  clickedCol;			// (onSort) the title clicked
	int  ctxRow, ctxX, ctxY;		// (onContext) the row right-clicked (-1 none), where
	const char *emptyText;			// written in the body when there is no row (0: nothing)
	void *user;				// the app's (e.g. which table the grid shows)

	DataGrid (int l, int t, int w, int h);
	void setColumns (int n);		// n columns (untitled, 100 px wide, left-aligned)
	int  columns () const { return m_ncol; }
	void setColumn (int c, const char *title, int width, int align = GRID_LEFT);
	Column &column (int c) { return m_col[c < 0 ? 0 : c >= MAXCOLS ? MAXCOLS - 1 : c]; }
	// Column c as wide as its title and the texts of its first `sample` rows need, within [minW, maxW].
	void autoSize (int c, int minW, int maxW, int sample = 300);
	void setRows (int n);			// the number of rows (the selection kept among them)
	void setSel (int r);			// select row r (-1: none) and scroll it into view; no callback
	void ensureVisible (int r);
	int  rowAt (int y) const;		// the row under y (grid coordinates), -1 none
	int  totalWidth () const;		// the columns' widths added
	int  visibleRows () const;		// how many rows the body shows
	void onDraw () override;
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override;
	bool onKey (long k) override;
private:
	Column m_col[MAXCOLS]; int m_ncol;
	int  m_hotRow, m_hotHead;		// under the pointer (-1 none)
	int  m_drag;				// what the left button holds: a G_* below
	int  m_dragCol, m_dragX0, m_dragW0;
	unsigned m_lastClick; int m_lastRow;
	bool m_rdown;
	// The geometry: the body (the rows, below the header) and the scroll bars.
	void bars (bool *vbar, bool *hbar, int *bodyW, int *bodyH) const;
	int  colX (int c) const;		// a column's left edge in the body (scrolled)
	int  headAt (int mx) const;		// the title under x, -1
	int  edgeAt (int mx) const;		// the column whose right edge is under x, -1
	void clampScroll ();
	void pick (int r, bool fire);
};

} // namespace uikit

#endif
