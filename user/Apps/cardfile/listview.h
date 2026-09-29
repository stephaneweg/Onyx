//
// listview.h -- the List view: the records shown in a grid (wtk's DataGrid), a column per field, the
// display names on top. A click on a name sorts by it, again the other way, a third time back to the
// file's order; a column's edge dragged widens it. A double click (or Enter) opens the record in the
// form; Delete deletes the selected one (after asking); a letter jumps to the next record whose sorted
// column (else the first) starts with it; a right click: Open, New Record, Duplicate, Delete. The
// cells show the values as the form does: numbers on the right, a date 29/09/2026, a
// colour's swatch and code, a yes / no's check mark, a text of several lines on one.
//
#ifndef _cardfile_listview_h
#define _cardfile_listview_h

#include "app.h"

namespace cf {

static void cmd_dup_record ();

class ListView : public Widget
{
public:
	DataGrid *grid;
	ListView (int l, int t, int w, int h) : Widget (l, t, w, h), m_ver (0)
	{
		grid = new DataGrid (10, 10, w - 20, h - 20);
		grid->anchor = ANCHOR_FILL;
		grid->cellText = cell_text; grid->cellDraw = cell_draw;
		grid->onSelect = on_select; grid->onActivate = on_activate; grid->onSort = on_sort; grid->onContext = on_context;
		addChild (grid);
	}
	unsigned bgColor () override { return C_BG; }
	void onDraw () override { canvas.clear (C_BG); }
	// The columns (the fields changed), the rows, the selection: as the document is.
	void sync ()
	{
		if (m_ver != g_fieldsVer || grid->columns () != g_doc.nf)
		{
			grid->setColumns (g_doc.nf);
			grid->setRows (g_nord);
			columns ();
			m_ver = g_fieldsVer;
		}
		grid->setRows (g_nord);
		grid->sortCol = g_doc.sort; grid->sortDesc = g_doc.sortDesc;
		grid->emptyText = !g_doc.nf ? "This form has no fields: the Design view adds them"
				: g_doc.nr ? "No record matches the search" : "No records yet: New Record (Ctrl+R) adds one";
		grid->setSel (cur_pos ());
		grid->invalidate (true);
	}
	// Each column as wide as its values need (within limits by type); what is left of the grid's width
	// goes to the widest text.
	void columns ()
	{
		int fw = wk_fw (), total = 0, wide = -1;
		for (int k = 0; k < g_doc.nf; k++)
		{
			const Field &f = g_doc.f[k];
			int al = f.type == FT_INT || f.type == FT_DEC ? GRID_RIGHT : f.type == FT_BOOL ? GRID_CENTRE : GRID_LEFT;
			grid->setColumn (k, f.label, 100, al);
			int tw = wk_text_w (f.label, 2) + 32;
			switch (f.type)
			{
			case FT_TEXT: grid->autoSize (k, 90, 240); break;
			case FT_MEMO: grid->autoSize (k, 140, 260); break;
			case FT_INT: case FT_DEC: grid->autoSize (k, 70, 170); break;
			case FT_DATE: grid->column (k).width = imax (tw, 10 * fw + 22); break;
			case FT_COLOR: grid->column (k).width = imax (tw, 7 * fw + 52); break;
			case FT_BOOL: grid->column (k).width = imax (tw, 60); break;
			case FT_CHOICE: grid->autoSize (k, 80, 200); break;
			}
			total += grid->column (k).width;
			if ((f.type == FT_TEXT || f.type == FT_MEMO) && (wide < 0 || grid->column (k).width > grid->column (wide).width)) wide = k;
		}
		int room = grid->width - 2 - WK_SBW - 2 - total;
		if (room > 0 && g_doc.nf) grid->column (wide >= 0 ? wide : g_doc.nf - 1).width += room;
	}
	bool onKey (long k) override
	{
		if (k == KEY_DEL && grid->sel >= 0) { cmd_del_record (); return true; }
		// a letter or a digit: the next record whose sorted column (else the first) starts with it
		if (((k > ' ' && k <= 126) || (k >= 0xC0 && k <= 0xFF)) && g_nord && g_doc.nf)
		{
			int col = g_doc.sort >= 0 ? g_doc.sort : 0;
			unsigned char c = fold ((unsigned char) k);
			char v[VAL_MAX];
			for (int n = 1; n <= g_nord; n++)
			{
				int row = (grid->sel + n) % g_nord;
				value_show (g_doc.f[col], g_doc.r[g_ord[row]][col], v, sizeof v, false);
				if (fold ((unsigned char) v[0]) == c) { grid->setSel (row); select_record (g_ord[row]); break; }
			}
			return true;
		}
		return false;
	}
private:
	unsigned m_ver;
	static const char *cell_text (DataGrid &, int row, int col, char *buf, int cap)
	{
		if (row < 0 || row >= g_nord || col >= g_doc.nf) return "";
		const Field &f = g_doc.f[col];
		value_show (f, g_doc.r[g_ord[row]][col], buf, cap, false, "  \xB7  ");
		return buf;
	}
	static bool cell_draw (DataGrid &, Canvas &cv, int row, int col, int x, int y, int w, int h, unsigned ink, bool selected)
	{
		if (row < 0 || row >= g_nord || col >= g_doc.nf) return false;
		const Field &f = g_doc.f[col];
		const char *v = g_doc.r[g_ord[row]][col];
		if (f.type == FT_BOOL)
		{
			int s = 14;
			if (v[0]) wk_check_mark (cv, x + (w - s) / 2, y + (h - s) / 2, s, true, WK_NORMAL);
			else wk_rline (cv, x + (w - s) / 2, y + (h - s) / 2, s, s, 3, selected ? ink : wk_mix (C_FIELD, C_FIELD_TEXT, 90), 140);
			return true;
		}
		if (f.type == FT_COLOR)
		{
			unsigned c;
			if (!v[0] || !parse_color (v, &c)) return !v[0];
			wk_rbox (cv, x + 8, y + 5, 30, h - 10, 3, c, c);
			wk_rline (cv, x + 8, y + 5, 30, h - 10, 3, wk_mix (c, 0, 90), 200);
			wk_text_l (cv, x + 46, y, h, v, ink);
			return true;
		}
		return false;
	}
	static void on_select (Widget &w)
	{
		DataGrid &g = (DataGrid &) w;
		if (g.sel >= 0 && g.sel < g_nord) select_record (g_ord[g.sel]);
	}
	static void on_activate (Widget &w) { on_select (w); show_view (V_FORM); }
	static void on_sort (Widget &w)
	{
		DataGrid &g = (DataGrid &) w;
		int c = g.clickedCol;
		if (c < 0 || c >= g_doc.nf) return;
		if (g_doc.sort != c) { g_doc.sort = c; g_doc.sortDesc = false; }
		else if (!g_doc.sortDesc) g_doc.sortDesc = true;
		else { g_doc.sort = -1; g_doc.sortDesc = false; }
		g_pin = -1;
		doc_changed (false);
		char m[96] = "Sorted by ";
		if (g_doc.sort < 0) scpy (m, "In the file's order", sizeof m);
		else { scat (m, g_doc.f[c].label, sizeof m); if (g_doc.sortDesc) scat (m, ", descending", sizeof m); }
		status (m);
	}
	static void on_context (Widget &w)
	{
		DataGrid &g = (DataGrid &) w;
		int ax, ay; abs_pos (&g, &ax, &ay);
		PopupMenu m (ax + g.ctxX, ay + g.ctxY);
		bool on = g.ctxRow >= 0;
		m.add ("Open in the Form", 1, on, "Enter");
		m.separator ();
		m.add ("New Record", 2, g_doc.nf > 0, "^R");
		m.add ("Duplicate Record", 3, on, "^D");
		m.add ("Delete Record...", 4, on, "Del");
		switch (m.run ())
		{
		case 1: show_view (V_FORM); break;
		case 2: cmd_new_record (); break;
		case 3: cmd_dup_record (); break;
		case 4: cmd_del_record (); break;
		}
	}
};

} // namespace cf

#endif
