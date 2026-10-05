//
// sheet -- Onyx's spreadsheet, in the way of LibreOffice Calc and Gnumeric: workbooks of sheets up to
// 16 384 columns x 1 048 576 rows; formulas as Excel writes them (237 functions: mathematics,
// statistics, logic, text, lookups, dates, finance; references relative and absolute, to other sheets,
// ranges and whole columns; arrays), recomputed at each change; number formats (Excel's codes), fonts,
// colours, borders, alignment, wrapping, merged cells; rows and columns inserted, deleted, sized,
// hidden; frozen panes; copy / cut / paste (the references following), the fill handle's series, sort,
// find and replace; defined names; conditional formatting (rules, colour scales, data bars); the
// AutoFilter; charts (column, bar, line, area, pie, scatter); undo. Files: Excel's .xlsx (its own format:
// read and written), LibreOffice's .ods (read), CSV (read and written).
//
//   the engine: core.h book.h formula.h eval.h numfmt.h input.h fn_core.h fn_more.h funcs.h ops.h undo.h
//   the files:  xml.h xlsx.h ods.h (+ CSV)
//   the window: condfmt.h ui_base.h render.h chart.h grid.h bars.h dialogs.h main.cpp
//
#define SHEET_APP 1
#include "dialogs.h"
#include "docguard.h"
#include "clipboard.h"

using namespace ss;
using namespace uikit;

#define W 1000						// (fits a 1024 x 768 screen: as Letters)
#define H 700

static Book g_b;
static GridView *g_grid;
static FormulaBar *g_fbar;
static SheetTabs *g_tabs;
static StatusBar *g_status;
static ToolButton *g_btn[140];
static PickBox *g_fontBox, *g_sizeBox, *g_zoomBox;
static char g_path[256];
static unsigned g_gen = 1, g_saved = 1;
static unsigned g_textColor = 0xC00000, g_fillColor = 0xFFF2CC;
static int g_borderPreset = 1;					// the Borders button's last one
static const char *RECOVER = "SD:/apps/sheet.app/recovered.xlsx";
static const char *RECOVER_NAME = "SD:/apps/sheet.app/recovered.txt";
static const int ZOOMS[] = { 25, 50, 75, 90, 100, 110, 125, 150, 175, 200, 250, 300, 400 };
enum { NZOOMS = 13 };

static Sheet *S () { return g_b.sh[g_b.active]; }
static bool changed_doc () { return g_gen != g_saved; }
static void touched () { g_gen++; }
static void refresh ();

static void now_clock (int *y, int *mo, int *d, int *h, int *mi, int *s) { kapi_get_datetime (y, mo, d, h, mi, s); }
static const char *base_name (const char *p) { const char *s = strrchr (p, '/'); return s ? s + 1 : p; }
static void message (const char *text) { note ("Spreadsheet", text); }

// ---- the book's state -------------------------------------------------------------------------------------
static void book_fresh ()
{
	book_clear (g_b); book_init (g_b);
	book_add_sheet (g_b, "Sheet1");
	fonts_reset (); undo_clear (); clip_free ();
	g_grid->copyMarquee = false; g_grid->selChart = -1; g_grid->resetSelKind ();
	g_path[0] = 0;
	g_gen = g_saved = 1;
}
static void loaded ()
{
	fonts_reset (); undo_clear ();
	g_grid->copyMarquee = false; g_grid->selChart = -1; g_grid->resetSelKind ();
	for (int i = 0; i < g_b.ns; i++) { Sheet *s = g_b.sh[i]; s->topR = imax (s->topR, s->freezeR); s->leftC = imax (s->leftC, s->freezeC); }
	g_tabs->first = 0; g_tabs->showActive ();
	g_gen = g_saved = 1;
	refresh ();
}
static bool load_path (const char *p)
{
	const char *why = 0;
	if (!book_load (g_b, p, &why)) { message (why ? why : "The file cannot be opened."); book_fresh (); refresh (); return false; }
	scpy (g_path, p, sizeof g_path);
	loaded ();
	return true;
}
static bool save_to (const char *p)
{
	if (ends_with (p, ".csv") || ends_with (p, ".txt") || ends_with (p, ".tsv"))
	{
		if (note ("Save as CSV", "A CSV file keeps only the values of the sheet shown -- no formats, no formulas, no other sheet. Save it anyway?", MB_OKCANCEL) != 1) return false;
	}
	const char *why = 0;
	if (!book_save (g_b, p, &why)) { message (why ? why : "The file cannot be written."); return false; }
	return true;
}
static void cmd_save_as ();
static void cmd_save ()
{
	if (g_grid->ed.on && !g_grid->commit (0, 0)) return;
	if (!g_path[0] || ends_with (g_path, ".ods")) { cmd_save_as (); return; }
	if (save_to (g_path)) { g_saved = g_gen; refresh (); }
}
static void cmd_save_as ()
{
	if (g_grid->ed.on && !g_grid->commit (0, 0)) return;
	char out[256], def[80];
	scpy (def, g_path[0] ? base_name (g_path) : "Book1.xlsx", sizeof def);
	char *dot = strrchr (def, '.'); if (dot && (ends_with (def, ".ods"))) scpy (dot, ".xlsx", (int) (sizeof def - (dot - def)));
	if (!uk_file_save (out, sizeof out, "SD:/docs", def)) return;
	if (!strrchr (base_name (out), '.')) { int n = (int) strlen (out); scpy (out + n, ".xlsx", (int) sizeof out - n); }
	if (!save_to (out)) return;
	if (!(ends_with (out, ".csv") || ends_with (out, ".txt") || ends_with (out, ".tsv"))) { scpy (g_path, out, sizeof g_path); g_saved = g_gen; }
	refresh ();
}
static void save_for_guard () { cmd_save (); }
static void cmd_new ()
{
	if (g_grid->ed.on) g_grid->cancelEdit ();
	if (!doc_confirm (g_path[0] ? base_name (g_path) : "Untitled", changed_doc (), save_for_guard)) return;
	book_fresh ();
	g_tabs->first = 0;
	refresh ();
}
static void cmd_open ()
{
	if (g_grid->ed.on && !g_grid->commit (0, 0)) return;
	if (!doc_confirm (g_path[0] ? base_name (g_path) : "Untitled", changed_doc (), save_for_guard)) return;
	char p[256];
	if (!uk_file_open (p, sizeof p, g_path[0] ? g_path : "SD:/docs")) return;
	load_path (p);
}
static void cmd_export_csv ()
{
	if (g_grid->ed.on && !g_grid->commit (0, 0)) return;
	char out[256], def[80];
	scpy (def, g_path[0] ? base_name (g_path) : "Book1", sizeof def);
	char *dot = strrchr (def, '.'); if (dot) *dot = 0;
	int n = (int) strlen (def); scpy (def + n, ".csv", (int) sizeof def - n);
	if (!uk_file_save (out, sizeof out, "SD:/docs", def)) return;
	int len; char *d = csv_write (g_b, S (), ',', &len);
	if (!write_file (out, d, len)) message ("The file cannot be written.");
	free (d);
}

// ---- File > Export as PDF: the used cells of a sheet (or all), cut into pages (pdf/pdfwrite.h) ---------------------------
struct PdfOpts { bool all, landscape, fit, grid, jpeg, open; float pw, ph, margin; };	// (pw, ph, margin: a printer's paper; 0: A4, 36 points)
class PdfDialog : public Dialog
{
public:
	PdfOpts &o; Textbox *file; RadioButton *rThis, *rAll, *rPort, *rLand; Checkbox *cFit, *cGrid, *cOpen; char dir[256];
	PdfDialog (PdfOpts &o_, const char *def, const char *startDir) : Dialog (500, 400, "Export as PDF"), o (o_)
	{
		snprintf (dir, sizeof dir, "%s", startDir);
		int y = titleH () + 14;
		file = field (100, y, 270, def); button (380, y - 1, 100, "Browse...", 9); y += 66;
		rThis = new RadioButton (30, y, 200, 24, "This sheet", 1, true, 0, C_FACE); addChild (rThis);
		rAll = new RadioButton (240, y, 220, 24, "All the sheets", 1, false, 0, C_FACE); addChild (rAll); y += 58;
		rPort = new RadioButton (30, y, 200, 24, "Portrait", 2, !o.landscape, 0, C_FACE); addChild (rPort);
		rLand = new RadioButton (240, y, 200, 24, "Landscape", 2, o.landscape, 0, C_FACE); addChild (rLand); y += 58;
		cFit = new Checkbox (30, y, 440, 24, "Fit the columns to the page's width", true, 0, C_FACE); addChild (cFit);
		cGrid = new Checkbox (30, y + 26, 440, 24, "Print the grid's lines", false, 0, C_FACE); addChild (cGrid);
		cOpen = new Checkbox (16, height - 42, 260, 24, "Open the PDF when it is saved", true, 0, C_FACE); addChild (cOpen);
		button (width - 210, height - 42, 92, "Export", 1); button (width - 108, height - 42, 92, "Cancel", 0);
	}
	void drawBody () override
	{
		int y = titleH () + 14;
		label (16, y + 5, "Save as"); y += 40;
		label (16, y + 4, "WHAT"); y += 58;
		label (16, y + 4, "THE PAGE (A4)"); y += 58;
		label (16, y + 4, "LAYOUT");
	}
	void onButton (int tag) override
	{
		if (tag == 9) { char p[256]; if (uk_file_save (p, sizeof p, dir, file->text)) file->setText (p); return; }
		if (tag == 1) { o.all = rAll->checked; o.landscape = rLand->checked; o.fit = cFit->checked; o.grid = cGrid->checked; o.open = cOpen->checked; }
		close (tag);
	}
	const char *path () const { return file->text; }
};
// a sheet's used cells as pages: the columns, then the rows, in bands that fit; the footer: its name and the page
static void pdf_sheet (pdfw::Writer &w, Sheet *s, const PdfOpts &o, int &pageNo, Canvas &dummy)
{
	sheet_bounds (s);
	int maxR = s->maxR, maxC = s->maxC;
	for (int i = 0; i < s->nmerge; i++) { if (s->merges[i].r1 > maxR) maxR = s->merges[i].r1; if (s->merges[i].c1 > maxC) maxC = s->merges[i].c1; }
	for (int i = 0; i < s->ncharts; i++)		// (the charts: in the pages too)
	{
		Chart *c = s->charts[i];
		if (maxC < 0) maxC = 0; if (maxR < 0) maxR = 0;
		while (maxC < MAXC - 1 && col_x (s, maxC + 1) < c->x + c->w) maxC++;
		while (maxR < 100000 && row_y (s, maxR + 1) < c->y + c->h) maxR++;
	}
	if (maxR < 0 || maxC < 0) return;
	float PW = o.landscape ? 841.89f : 595.28f, PH = o.landscape ? 595.28f : 841.89f, M = 36, FOOT = 18;
	if (o.pw > 0 && o.ph > 0) { PW = o.pw; PH = o.ph; }
	if (o.margin > 0) M = o.margin;
	float scale = 0.75f;
	long long usedW = col_x (s, maxC + 1);
	if (o.fit && usedW * scale > PW - 2 * M) scale = (PW - 2 * M) / usedW;
	int bandW = (int) ((PW - 2 * M) / scale), bandH = (int) ((PH - 2 * M - FOOT) / scale);
	// the column bands, the row bands
	int cb[256], ncb = 0, rb[2048], nrb = 0;
	for (int c = 0; c <= maxC && ncb < 255; )
	{
		cb[ncb++] = c; int w = 0;
		while (c <= maxC && (w + col_w (s, c) <= bandW || w == 0)) { w += col_w (s, c); c++; }
	}
	cb[ncb] = maxC + 1;
	for (int r = 0; r <= maxR && nrb < 2047; )
	{
		rb[nrb++] = r; int h = 0;
		while (r <= maxR && (h + row_h (s, r) <= bandH || h == 0)) { h += row_h (s, r); r++; }
	}
	rb[nrb] = maxR + 1;
	for (int i = 0; i < ncb; i++)
		for (int j = 0; j < nrb; j++)
		{
			w.begin_page (PW, PH);
			g_pdfS = scale; g_pdfX = M; g_pdfY = M;
			PaneView pv;
			int pw = (int) (col_x (s, cb[i + 1]) - col_x (s, cb[i])), ph = (int) (row_y (s, rb[j + 1]) - row_y (s, rb[j]));
			pane_layout (s, 100, cb[i], cb[i + 1], rb[j], rb[j + 1], 0, 0, pw, ph, pv);
			paint_pane (dummy, g_b, s, 100, pv, o.grid);
			// the charts whose top left is on this page: drawn at twice the size, as images
			int bx0 = (int) col_x (s, cb[i]), by0 = (int) row_y (s, rb[j]);
			for (int k = 0; k < s->ncharts; k++)
			{
				Chart *c = s->charts[k];
				if (c->x < bx0 || c->x >= bx0 + pw || c->y < by0 || c->y >= by0 + ph || c->w <= 0 || c->h <= 0) continue;
				Canvas cc;
				if (!cc.alloc (c->w * 2, c->h * 2)) continue;
				cc.clear (0xFFFFFF);
				pdfw::Writer *keep = g_pdf; g_pdf = 0;
				draw_chart (cc, g_b, c, 0, 0, c->w * 2, c->h * 2, 200, Rect { 0, 0, c->h * 2 - 1, c->w * 2 - 1 });
				g_pdf = keep;
				for (int q = 0; q < c->w * 2 * c->h * 2; q++) cc.px[q] |= 0xFF000000u;
				w.image (cc.px, c->w * 2, c->h * 2, M + (c->x - bx0) * scale, M + (c->y - by0) * scale, c->w * scale, c->h * scale);
			}
			// the footer: the sheet, the page
			char foot[120]; snprintf (foot, sizeof foot, "%s  \xE2\x80\x94  %d", s->name, ++pageNo);
			fnt::Font *f = fnt::get (g_famSans >= 0 ? g_famSans : 0, 0, 12 * 64);
			if (f)
			{
				g_pdfS = 0.75f; g_pdfX = 0; g_pdfY = 0;
				int tw = u8_width (f, foot, (int) strlen (foot));
				u8_draw (dummy, f, (int) ((PW / 0.75f) * 64 - tw) / 2, (int) ((PH - M + 6) / 0.75f), foot, (int) strlen (foot), 0x707070, 0, 0, 1 << 20, 1 << 20);
			}
			w.end_page ();
		}
}
static void cmd_export_pdf ()
{
	if (g_grid->ed.on && !g_grid->commit (0, 0)) return;
	char def[256], dir[256] = "SD:/docs";
	snprintf (def, sizeof def, "%s", g_path[0] ? base_name (g_path) : "Book1");
	char *dot = strrchr (def, '.'); if (dot) *dot = 0;
	if (g_path[0]) { snprintf (dir, sizeof dir, "%s", g_path); char *e = strrchr (dir, '/'); if (e) *e = 0; }
	char full[300]; snprintf (full, sizeof full, "%s/%s.pdf", dir, def);
	Sheet *s0 = S (); sheet_bounds (s0);
	PdfOpts o; memset (&o, 0, sizeof o);
	long long uw = col_x (s0, s0->maxC + 1), uh = row_y (s0, s0->maxR + 1);	// (the used part's size, the charts with it)
	for (int i = 0; i < s0->ncharts; i++) { Chart *c = s0->charts[i]; if (c->x + c->w > uw) uw = c->x + c->w; if (c->y + c->h > uh) uh = c->y + c->h; }
	o.landscape = uw > uh && uw > 700;
	PdfDialog d (o, full, dir);
	if (!d.run ()) return;
	char path[256]; snprintf (path, sizeof path, "%s", d.path ());
	if (!ends_with (path, ".pdf")) { int n = (int) strlen (path); scpy (path + n, ".pdf", (int) sizeof path - n); }
	pdfw::Writer w;
	w.info (def, 0, 0, "Spreadsheet (Onyx)");
	Canvas dummy;
	g_pdf = &w;
	int pageNo = 0;
	for (int i = 0; i < g_b.ns; i++)
	{
		if (!o.all && g_b.sh[i] != s0) continue;
		int before = pageNo;
		pdf_sheet (w, g_b.sh[i], o, pageNo, dummy);
		if (o.all && pageNo > before) w.outline (0, g_b.sh[i]->name, before, 0);
	}
	g_pdf = 0; g_pdfS = 0.75f; g_pdfX = g_pdfY = 0;
	if (!pageNo) { message ("There is nothing in the sheet to export."); return; }
	unsigned len; unsigned char *pdf = w.finish (&len);
	bool ok = pdf && write_file (path, (const char *) pdf, (int) len);
	delete[] pdf;
	if (!ok) message ("The file cannot be written.");
	else if (o.open) kapi_exec ("SD:apps/pdf.app/main", path);
}

// File > Print: the Print dialog (the library's: the printer, its paper, the pages...), then the sheet's
// pages drawn into the job by the PDF export's code (print/pdfprint.h).
static void cmd_print ()
{
	if (g_grid->ed.on && !g_grid->commit (0, 0)) return;
	char name[256]; snprintf (name, sizeof name, "%s", g_path[0] ? base_name (g_path) : "Book1");
	Sheet *s0 = S (); sheet_bounds (s0);
	long long uw = col_x (s0, s0->maxC + 1), uh = row_y (s0, s0->maxR + 1);
	for (int i = 0; i < s0->ncharts; i++) { Chart *c = s0->charts[i]; if (c->x + c->w > uw) uw = c->x + c->w; if (c->y + c->h > uh) uh = c->y + c->h; }
	PrintSetup ps; print_setup_default (&ps);
	if (uw > uh && uw > 700) print_setup_paper (&ps, 0, PRINT_LANDSCAPE);	// (a wide sheet: the paper lying, first)
	PrintDialogInfo di = { sizeof di, name, 0, 0, 0, 0, 0 };
	if (!print_dialog (&ps, &di)) return;
	PrintJob *j = print_begin (&ps, name);
	if (!j) { message ("The print queue cannot be written."); return; }
	PdfOpts o; memset (&o, 0, sizeof o);
	o.pw = ps.paper_w; o.ph = ps.paper_h;
	// the margin: half an inch, and clear of what the printer cannot print on (the footer is under it)
	float m = ps.margin_l; if (ps.margin_t > m) m = ps.margin_t; if (ps.margin_r > m) m = ps.margin_r; if (ps.margin_b > m) m = ps.margin_b;
	o.margin = m + 12 > 36 ? m + 12 : 36;
	int pageNo = 0;
	{
		PrintWriter w (j); Canvas dummy;
		g_pdf = &w;
		pdf_sheet (w, s0, o, pageNo, dummy);
		g_pdf = 0; g_pdfS = 0.75f; g_pdfX = g_pdfY = 0;
	}
	if (!pageNo) { print_abort (j); message ("There is nothing in the sheet to print."); return; }
	if (print_end (j) < 0) message ("The sheet could not be put in the print queue.");
}

// ---- after a change ----------------------------------------------------------------------------------------
static void after_change (bool rows = false)
{
	recalc (g_b);
	touched ();
	if (rows) { Rect r = g_grid->sel (); Sheet *s = S (); for (int rr = r.r0; rr <= imin (r.r1, r.r0 + 500); rr++) autofit_row (g_b, s, rr); }
	g_grid->invalidate (true);
	refresh ();
}
static Rect used_sel ()						// the selection, clipped to the used area for whole rows / columns
{
	Rect r = g_grid->sel ();
	Sheet *s = S ();
	sheet_bounds (s);
	if (g_grid->wholeCols ()) r.r1 = imax (r.r0, s->maxR);
	if (g_grid->wholeRows ()) r.c1 = imax (r.c0, s->maxC);
	return r;
}

// ---- the grid's requests ----------------------------------------------------------------------------------------
static bool grid_commit (int r, int c, const char *text)
{
	Sheet *s = book_sheet_by_id (g_b, g_grid->ed.sheetId);
	if (!s) return true;
	// (Latin-1 typed keys arrive as UTF-8 already)
	Rect one = { r, c, r, c };
	undo_rect (g_b, s, one);
	const char *why = 0; int where = 0;
	if (!cell_input (g_b, s, r, c, text, &why, &where))
	{
		undo_step (g_b, false); ustack_clear (g_redo);		// (nothing changed: the step taken back)
		char m[200]; snprintf (m, sizeof m, "This formula cannot be read: %s", why ? why : "?");
		message (m);
		g_grid->ed.caret = g_grid->ed.anchor = iclamp (where, 0, g_grid->ed.t.n);
		return false;
	}
	recalc (g_b);
	autofit_row (g_b, s, r);
	touched ();
	return true;
}
static void grid_edited () { refresh (); }
static void cmd_cut (); static void cmd_copy (); static void cmd_paste (); static void cmd_paste_special ();
static void cmd_ins_rows (); static void cmd_ins_cols (); static void cmd_del_rows (); static void cmd_del_cols ();
static void cmd_clear_contents (); static void cmd_format_cells (); static void cmd_chart (); static void cmd_col_width ();
static void cmd_cond_format ()
{
	if (g_grid->ed.on && !g_grid->commit (0, 0)) return;
	Sheet *s = S (); Rect r = used_sel ();
	char a1[24], a2[24], t[64]; cell_name (r.r0, r.c0, a1); cell_name (r.r1, r.c1, a2);
	if (r.r0 == r.r1 && r.c0 == r.c1) scpy (t, a1, sizeof t); else snprintf (t, sizeof t, "%s:%s", a1, a2);
	undo_sheet (g_b, s);
	CondDialog d (&g_b, s, t);
	d.run ();
	if (!d.changed) undo_drop_last (); else touched ();
	cf_changed ();
	g_grid->invalidate (true); g_grid->setFocus ();
	refresh ();
}
static void cmd_row_height (); static void cmd_opt_width (); static void cmd_hide_rows (); static void cmd_hide_cols (); static void cmd_show_all ();
static void cmd_sort_asc (); static void cmd_sort_desc ();
static void chart_open (int i);
static void pick_sheet (int i);

static void grid_context (int x, int y, int where);
enum { M_CUT = 1, M_COPY, M_PASTE, M_PASTESP, M_INSROW, M_INSCOL, M_DELROW, M_DELCOL, M_CLEAR, M_FORMAT, M_CHART, M_COLW, M_ROWH, M_OPTW,
       M_HIDER, M_HIDEC, M_SHOW, M_SORTA, M_SORTD, M_CHPROP, M_CHDEL, M_CHFRONT };
static void context_menu (int x, int y, int where)
{
	int ax = x + g_grid->left, ay = y + g_grid->top;
	PopupMenu m (ax, ay);
	if (where == 3)
	{
		m.add ("Chart Properties...", M_CHPROP); m.add ("Delete Chart", M_CHDEL, true, "Del"); m.add ("Bring to Front", M_CHFRONT);
	}
	else
	{
		m.add ("Cut", M_CUT, true, "^X"); m.add ("Copy", M_COPY, true, "^C"); m.add ("Paste", M_PASTE, true, "^V"); m.add ("Paste Special...", M_PASTESP);
		m.separator ();
		if (where != 2) { m.add ("Insert Columns Before", M_INSCOL); m.add ("Delete Columns", M_DELCOL); }
		if (where != 1) { m.add ("Insert Rows Above", M_INSROW); m.add ("Delete Rows", M_DELROW); }
		m.add ("Clear Contents", M_CLEAR, true, "Del");
		m.separator ();
		if (where == 1) { m.add ("Column Width...", M_COLW); m.add ("Optimal Width", M_OPTW); m.add ("Hide Columns", M_HIDEC); m.add ("Show Columns", M_SHOW); }
		else if (where == 2) { m.add ("Row Height...", M_ROWH); m.add ("Hide Rows", M_HIDER); m.add ("Show Rows", M_SHOW); }
		else { m.add ("Sort Ascending", M_SORTA); m.add ("Insert Chart...", M_CHART); }
		m.add ("Format Cells...", M_FORMAT, true, "^1");
	}
	switch (m.run ())
	{
	case M_CUT: cmd_cut (); break; case M_COPY: cmd_copy (); break; case M_PASTE: cmd_paste (); break; case M_PASTESP: cmd_paste_special (); break;
	case M_INSROW: cmd_ins_rows (); break; case M_INSCOL: cmd_ins_cols (); break; case M_DELROW: cmd_del_rows (); break; case M_DELCOL: cmd_del_cols (); break;
	case M_CLEAR: cmd_clear_contents (); break; case M_FORMAT: cmd_format_cells (); break; case M_CHART: cmd_chart (); break;
	case M_COLW: cmd_col_width (); break; case M_ROWH: cmd_row_height (); break; case M_OPTW: cmd_opt_width (); break;
	case M_HIDER: cmd_hide_rows (); break; case M_HIDEC: cmd_hide_cols (); break; case M_SHOW: cmd_show_all (); break;
	case M_SORTA: cmd_sort_asc (); break;
	case M_CHPROP: chart_open (g_grid->selChart); break;
	case M_CHDEL: grid_context (0, 0, 4); break;
	case M_CHFRONT:
	{
		Sheet *s = S (); int i = g_grid->selChart;
		if (i >= 0 && i < s->ncharts - 1) { Chart *c = s->charts[i]; memmove (s->charts + i, s->charts + i + 1, (s->ncharts - i - 1) * sizeof (Chart *)); s->charts[s->ncharts - 1] = c; g_grid->selChart = s->ncharts - 1; touched (); }
		break;
	}
	}
	g_grid->invalidate (true);
}
static void grid_context (int x, int y, int where)
{
	Sheet *s = S ();
	switch (where)
	{
	case 4:							// delete the chart chosen
		if (g_grid->selChart >= 0 && g_grid->selChart < s->ncharts)
		{
			undo_sheet (g_b, s);
			chart_free (s->charts[g_grid->selChart]);
			memmove (s->charts + g_grid->selChart, s->charts + g_grid->selChart + 1, (s->ncharts - g_grid->selChart - 1) * sizeof (Chart *));
			s->ncharts--; g_grid->selChart = -1;
			touched (); g_grid->invalidate (true);
		}
		return;
	case 5: pick_sheet (iclamp (g_b.active + x, 0, g_b.ns - 1)); return;
	case 6:							// the fill handle released
	{
		Rect src = g_grid->sel (), to = g_grid->m_fillRect;
		undo_rect (g_b, s, to);
		// (a smaller range than the source: the rest cleared, as the spreadsheets do)
		if (to.r1 < src.r1 || to.c1 < src.c1)
		{
			Rect clr = src;
			if (to.r1 < src.r1) clr.r0 = to.r1 + 1; else clr.c0 = to.c1 + 1;
			clear_range (g_b, s, clr, CLR_CONTENT);
		}
		else fill_range (g_b, s, src, to);
		g_grid->selectRange (to.r1 < src.r1 || to.c1 < src.c1 ? Rect { src.r0, src.c0, to.r1, to.c1 } : to);
		after_change (true);
		return;
	}
	case 7:							// a column fitted (double click on its edge)
	{
		undo_sheet (g_b, s);
		Rect r = g_grid->sel ();
		int c0 = g_grid->m_fitCol, c1 = c0;
		if (g_grid->wholeCols () && c0 >= r.c0 && c0 <= r.c1) { c0 = r.c0; c1 = r.c1; }
		for (int c = c0; c <= c1; c++) { s->colW[c] = (unsigned short) fit_col_width (g_b, s, c); s->colFl[c] &= ~RF_HIDDEN; }
		cols_changed (s); touched (); g_grid->invalidate (true);
		return;
	}
	case 8: undo_sheet (g_b, s); touched (); return;			// (a column / row is sized: its undo step)
	case 9:
	{
		undo_sheet (g_b, s);
		RowInfo *ri = row_info (s, g_grid->m_fitRow);
		if (ri) ri->fl &= ~(RF_CUSTOM | RF_HIDDEN);
		autofit_row (g_b, s, g_grid->m_fitRow);
		touched (); g_grid->invalidate (true);
		return;
	}
	}
	context_menu (x, y, where);
}

// ---- the edit menu ------------------------------------------------------------------------------------------
static void editor_copy (bool cut)
{
	EditLine &e = g_grid->ed;
	if (e.s0 () == e.s1 ()) return;
	clip_set_text_n (e.t.b + e.s0 (), e.s1 () - e.s0 ());
	if (cut) { e.insert ("", 0); g_grid->changed (); }
}
static void cmd_copy ()
{
	if (g_grid->ed.on) { editor_copy (false); return; }
	Sheet *s = S ();
	Rect r = used_sel ();
	if (g_grid->wholeCols () || g_grid->wholeRows ()) copy_range (g_b, s, g_grid->sel (), false); else copy_range (g_b, s, r, false);
	char *t = clip_text (g_b, s, r);
	clip_set_text (t);
	free (g_clip.text); g_clip.text = t;
	g_grid->copyMarquee = true; g_grid->copyRect = r; g_grid->copySheet = s->id;
	g_grid->invalidate (true);
}
static void cmd_cut ()
{
	if (g_grid->ed.on) { editor_copy (true); return; }
	cmd_copy ();
	g_clip.cut = true;
}
static void cmd_paste ()
{
	Sheet *s = S ();
	static char text[65536];
	int n = clip_get_text (text, sizeof text);
	if (g_grid->ed.on)
	{
		int k = 0; while (k < n && text[k] != '\n' && text[k] != '\r') k++;
		char *u = u8_valid (text, k) ? sdup (text, k) : latin1_to_u8 (text, k);
		g_grid->ed.insert (u, (int) strlen (u)); free (u);
		g_grid->changed ();
		return;
	}
	bool ours = g_clip.full && g_clip.text && n > 0 && !strcmp (text, g_clip.text);
	if (ours || (g_clip.full && n == 0))
	{
		if (g_clip.cut)
		{
			undo_book (g_b);
			paste_cut (g_b, s, s->curR, s->curC);
			g_grid->copyMarquee = false;
			Rect r = { s->curR, s->curC, s->curR + g_clip.rows - 1, s->curC + g_clip.cols - 1 };
			g_grid->selectRange (r);
		}
		else
		{
			Rect sl = g_grid->sel ();
			int R = g_clip.rows, C = g_clip.cols;
			Rect dst = { s->curR, s->curC, s->curR + R - 1, s->curC + C - 1 };
			if ((sl.r1 - sl.r0 + 1) % R == 0 && (sl.c1 - sl.c0 + 1) % C == 0 && !g_grid->wholeCols () && !g_grid->wholeRows ()) dst = sl;
			if (g_clip.nmerge || g_clip.colW) undo_sheet (g_b, s); else undo_rect (g_b, s, dst);
			paste_range (g_b, s, s->curR, s->curC, sl);
			if (dst.r1 - dst.r0 + 1 == R && dst.c1 - dst.c0 + 1 == C) dst = Rect { s->curR, s->curC, s->curR + R - 1, s->curC + C - 1 };
			g_grid->selectRange (dst);
		}
		after_change (true);
		return;
	}
	if (n <= 0) return;
	// text from another app: tab-separated lines
	char *u = u8_valid (text, n) ? sdup (text, n) : latin1_to_u8 (text, n);
	int rows = 1, cols = 1;
	for (const char *p = u; *p; p++) { if (*p == '\n' && p[1]) rows++; }
	undo_rect (g_b, s, Rect { s->curR, s->curC, imin (MAXR - 1, s->curR + rows), imin (MAXC - 1, s->curC + 200) });
	paste_text (g_b, s, s->curR, s->curC, u, &rows, &cols);
	free (u);
	g_grid->selectRange (Rect { s->curR, s->curC, s->curR + imax (0, rows - 1), s->curC + imax (0, cols - 1) });
	after_change (true);
}
static void cmd_paste_special ()
{
	if (g_grid->ed.on || !g_clip.full) { if (!g_clip.full) message ("Copy some cells first (Paste Special pastes what was copied here)."); return; }
	PasteDialog d;
	if (d.run () != 1) return;
	Sheet *s = S ();
	int what = d.what (); bool tr = d.tr->checked;
	int R = tr ? g_clip.cols : g_clip.rows, C = tr ? g_clip.rows : g_clip.cols;
	Rect dst = { s->curR, s->curC, s->curR + R - 1, s->curC + C - 1 };
	undo_rect (g_b, s, dst);
	bool cut = g_clip.cut; g_clip.cut = false;
	paste_range (g_b, s, s->curR, s->curC, dst, what, tr);
	g_clip.cut = cut;
	g_grid->selectRange (dst);
	after_change (true);
}
static void cmd_clear_contents ()
{
	if (g_grid->ed.on) { g_grid->ed.del (); g_grid->changed (); return; }
	Sheet *s = S ();
	Rect r = used_sel ();
	undo_rect (g_b, s, r);
	clear_range (g_b, s, r, CLR_CONTENT);
	after_change ();
}
static void cmd_undo ()
{
	if (g_grid->ed.on) { g_grid->cancelEdit (); return; }
	Rect r;
	Sheet *s = undo_step (g_b, false, &r);
	if (!s) return;
	for (int i = 0; i < g_b.ns; i++) if (g_b.sh[i] == s) g_b.active = i;
	fonts_reset (); recalc (g_b); touched ();
	g_grid->selChart = -1; g_grid->copyMarquee = false;
	g_tabs->showActive ();
	g_grid->ensureVisible (s->curR, s->curC);
	refresh ();
}
static void cmd_redo ()
{
	if (g_grid->ed.on) return;
	Rect r;
	Sheet *s = undo_step (g_b, true, &r);
	if (!s) return;
	for (int i = 0; i < g_b.ns; i++) if (g_b.sh[i] == s) g_b.active = i;
	fonts_reset (); recalc (g_b); touched ();
	g_grid->selChart = -1;
	g_tabs->showActive ();
	g_grid->ensureVisible (s->curR, s->curC);
	refresh ();
}
static void cmd_fill_down ()
{
	if (g_grid->ed.on) return;
	Sheet *s = S (); Rect r = used_sel ();
	undo_rect (g_b, s, r); fill_copy (g_b, s, r, true); after_change ();
}
static void cmd_fill_right ()
{
	if (g_grid->ed.on) return;
	Sheet *s = S (); Rect r = used_sel ();
	undo_rect (g_b, s, r); fill_copy (g_b, s, r, false); after_change ();
}
static void cmd_select_all () { if (g_grid->ed.on) { g_grid->ed.anchor = 0; g_grid->ed.caret = g_grid->ed.t.n; g_grid->changed (); return; } g_grid->selectAll (); }

// Find and Replace: a cell's text (its formula, or its value as shown) against what is looked for.
static bool cell_matches (Sheet *s, Cell *x, Buf &txt)
{
	txt.clear ();
	if (!x || x->kind == K_NONE) return false;
	if (g_find.formulas) cell_edit_text (g_b, s, x, txt);
	else { Shown sh; cell_shown (g_b, s, x, sh, 64); txt.puts (x->vt == V_STR ? x->str : sh.text); }
	char *f = latin1_to_u8 (g_find.find, (int) strlen (g_find.find));
	int fl = (int) strlen (f);
	bool ok = false;
	if (g_find.whole) ok = g_find.matchCase ? !strcmp (txt.str (), f) : ci_cmp (txt.str (), txt.n, f, fl) == 0;
	else
		for (int i = 0; i + fl <= txt.n && !ok; i++)
			ok = g_find.matchCase ? !memcmp (txt.b + i, f, fl) : ci_cmp (txt.b + i, fl, f, fl) == 0;
	free (f);
	return ok;
}
static bool find_next (bool fromNext)
{
	for (int pass = 0; pass < (g_find.allSheets ? g_b.ns + 1 : 2); pass++)
	{
		Sheet *s = S ();
		sheet_bounds (s);
		int r = s->curR, c = s->curC + (fromNext ? 1 : 0);
		if (pass > 0) { r = 0; c = 0; }
		for (; r <= s->maxR; r++, c = 0)
			for (; c <= s->maxC; c++)
			{
				Buf t;
				if (cell_matches (s, s->cells.get (r, c), t)) { g_grid->selectCell (r, c, false); return true; }
			}
		if (g_find.allSheets && pass < g_b.ns) { pick_sheet ((g_b.active + 1) % g_b.ns); }
	}
	return false;
}
static int replace_in (Sheet *s, int r, int c)			// the cell's matches replaced; how many
{
	Cell *x = s->cells.get (r, c);
	Buf t;
	if (!cell_matches (s, x, t)) return 0;
	char *f = latin1_to_u8 (g_find.find, (int) strlen (g_find.find)), *rp = latin1_to_u8 (g_find.repl, (int) strlen (g_find.repl));
	int fl = (int) strlen (f), k = 0;
	Buf o;
	if (g_find.whole) { o.puts (rp); k = 1; }
	else
		for (int i = 0; i < t.n; )
		{
			bool m = i + fl <= t.n && (g_find.matchCase ? !memcmp (t.b + i, f, fl) : ci_cmp (t.b + i, fl, f, fl) == 0);
			if (m && fl) { o.puts (rp); i += fl; k++; } else o.put (t.b[i++]);
		}
	free (f); free (rp);
	if (!k) return 0;
	if (x->kind == K_STR && !g_find.formulas) cell_set_str (s, r, c, o.str ());
	else cell_input (g_b, s, r, c, o.str ());
	return k;
}
static void cmd_find ()
{
	if (g_grid->ed.on && !g_grid->commit (0, 0)) return;
	for (;;)
	{
		FindDialog d;
		int r = d.run ();
		if (r == 0 || !g_find.find[0]) return;
		Sheet *s = S ();
		if (r == 2) { if (!find_next (true)) { message ("Nothing more was found."); return; } continue; }
		if (r == 3)
		{
			undo_rect (g_b, s, Rect { s->curR, s->curC, s->curR, s->curC });
			if (replace_in (s, s->curR, s->curC)) after_change ();
			if (!find_next (true)) { message ("Nothing more was found."); return; }
			continue;
		}
		if (r == 4)
		{
			int total = 0;
			if (g_find.allSheets) undo_book (g_b); else undo_sheet (g_b, s);
			for (int i = 0; i < g_b.ns; i++)
			{
				Sheet *t = g_b.sh[i];
				if (!g_find.allSheets && t != s) continue;
				sheet_bounds (t);
				for (int rr = 0; rr <= t->maxR; rr++) for (int cc = 0; cc <= t->maxC; cc++) total += replace_in (t, rr, cc);
			}
			after_change ();
			char m[80]; snprintf (m, sizeof m, "%d replacement%s made.", total, total == 1 ? "" : "s");
			message (m);
			return;
		}
	}
}
// The selection as an absolute reference: "Sales!$B$5:$D$16".
static void sel_abs_ref (Buf &o)
{
	Sheet *s = S (); Rect r = g_grid->sel ();
	o.put ('='); put_sheet_name (o, s->name); o.put ('!');
	char a[24];
	col_name (r.c0, a); o.put ('$'); o.puts (a); o.put ('$'); o.puti (r.r0 + 1);
	if (r.r1 != r.r0 || r.c1 != r.c0) { col_name (r.c1, a); o.puts (":$"); o.puts (a); o.put ('$'); o.puti (r.r1 + 1); }
}
static void cmd_names ()
{
	if (g_grid->ed.on && !g_grid->commit (0, 0)) return;
	Buf ref; sel_abs_ref (ref);
	undo_book (g_b);
	NamesDialog d (&g_b, ref.str ());
	d.run ();
	if (!d.changed) undo_drop_last ();
	else { recalc (g_b); touched (); g_grid->invalidate (true); }
	g_grid->setFocus ();
	refresh ();
}
static void cmd_goto () { g_fbar->name->setFocus (); g_fbar->name->selectAll (); }
static void name_leave () { g_grid->setFocus (); refresh (); }
static void go_to (const char *t)
{
	// "B7", "C2:F9", "Sheet2!A1", "A:A", "3:3"
	Formula *f = formula_parse (g_b, t, -1);
	bool ok = false;
	if (f && f->nd[f->root].k == N_TOK)
	{
		const Tok &k = f->tok[f->nd[f->root].tok];
		if ((k.t == TK_REF || k.t == TK_AREA) && !(k.fl & TF_BAD))
		{
			if (k.sheet) for (int i = 0; i < g_b.ns; i++) if (g_b.sh[i]->id == k.sheet) pick_sheet (i);
			if (k.t == TK_REF) g_grid->selectCell (k.r0, k.c0, false);
			else { g_grid->selectRange (Rect { k.r0, k.c0, k.r1, k.c1 }); g_grid->ensureVisible (k.r0, k.c0); }
			ok = true;
		}
		else if (k.t == TK_NAME)
		{
			char nm[64]; scpy (nm, f->pool + k.s, imin (k.sn + 1, (int) sizeof nm));
			Sheet *s = S ();
			const DefName *d = name_find (g_b, nm, (int) strlen (nm), s->id);
			if (d && d->f)						// a name: its range shown
			{
				Buf e; e.put ('='); e.puts (nm);
				Val v = eval_text (g_b, s, s->curR, s->curC, e.str ());
				if (v.t == V_REF && v.sh)
				{
					for (int i = 0; i < g_b.ns; i++) if (g_b.sh[i] == v.sh) pick_sheet (i);
					if (v.r0 == v.r1 && v.c0 == v.c1) g_grid->selectCell (v.r0, v.c0, false);
					else { g_grid->selectRange (Rect { v.r0, v.c0, v.r1, v.c1 }); g_grid->ensureVisible (v.r0, v.c0); }
					ok = true;
				}
				else { message ("This name holds a value, not a range."); ok = true; }
			}
			else if (name_valid (nm))				// a new name: the selection's (as Excel does)
			{
				Buf ref; sel_abs_ref (ref);
				undo_book (g_b);
				if (name_set (g_b, nm, 0, ref.str ())) { recalc (g_b); touched (); g_grid->invalidate (true); }
				else undo_drop_last ();
				ok = true;
			}
		}
	}
	formula_free (f);
	if (!ok) message ("Type a cell or a range to go there (B7, C2:F9, Sheet2!A1), or a name for the selection.");
	g_grid->setFocus ();
	refresh ();
}

// ---- view ------------------------------------------------------------------------------------------------
static void set_zoom (int z) { g_grid->z = iclamp (z, 25, 400); g_grid->invalidate (true); refresh (); }
static void zoom_step (int d)
{
	if (d == 0) { set_zoom (100); return; }
	int z = g_grid->z, i = 0;
	while (i < NZOOMS && ZOOMS[i] < z) i++;
	if (d > 0) { if (i < NZOOMS && ZOOMS[i] == z) i++; set_zoom (ZOOMS[imin (i, NZOOMS - 1)]); }
	else set_zoom (ZOOMS[imax (0, i - 1)]);
}
static void cmd_zoom_in () { zoom_step (1); }
static void cmd_zoom_out () { zoom_step (-1); }
static void cmd_zoom_100 () { set_zoom (100); }
static void cmd_freeze ()
{
	Sheet *s = S ();
	if (s->freezeR || s->freezeC) { s->freezeR = s->freezeC = 0; }
	else
	{
		int r = s->curR - (s->topR > s->curR ? 0 : 0), c = s->curC;
		if (r == 0 && c == 0) r = 1;				// (at A1: the first row)
		// the rows / columns frozen: those above / left of the cursor, from what is shown
		s->freezeR = r; s->freezeC = c;
		s->topR = imax (s->topR, r); s->leftC = imax (s->leftC, c);
	}
	touched ();
	g_grid->invalidate (true); refresh ();
}
static void cmd_gridlines () { Sheet *s = S (); s->grid = !s->grid; touched (); g_grid->invalidate (true); }
static void cmd_formulas () { g_showFormulas = !g_showFormulas; g_grid->invalidate (true); refresh (); }

// ---- insert -----------------------------------------------------------------------------------------------
static void cmd_ins_rows ()
{
	if (g_grid->ed.on) return;
	Rect r = g_grid->sel ();
	int n = g_grid->wholeCols () ? 1 : r.r1 - r.r0 + 1;
	undo_book (g_b);
	insert_rows (g_b, S (), r.r0, n);
	after_change ();
}
static void cmd_ins_cols ()
{
	if (g_grid->ed.on) return;
	Rect r = g_grid->sel ();
	int n = g_grid->wholeRows () ? 1 : r.c1 - r.c0 + 1;
	undo_book (g_b);
	insert_cols (g_b, S (), r.c0, n);
	after_change ();
}
static void cmd_del_rows ()
{
	if (g_grid->ed.on) return;
	Rect r = g_grid->sel ();
	if (g_grid->wholeCols ()) { message ("Choose rows (or cells in them) to delete."); return; }
	undo_book (g_b);
	delete_rows (g_b, S (), r.r0, r.r1 - r.r0 + 1);
	g_grid->selectCell (r.r0, S ()->curC, false);
	after_change ();
}
static void cmd_del_cols ()
{
	if (g_grid->ed.on) return;
	Rect r = g_grid->sel ();
	if (g_grid->wholeRows ()) { message ("Choose columns (or cells in them) to delete."); return; }
	undo_book (g_b);
	delete_cols (g_b, S (), r.c0, r.c1 - r.c0 + 1);
	g_grid->selectCell (S ()->curR, r.c0, false);
	after_change ();
}
static void cmd_function ()
{
	FuncDialog d;
	if (d.run () != 1 || d.chosen < 0) { g_grid->setFocus (); return; }
	Buf o; o.puts (FNS[d.chosen].name); o.put ('(');
	if (!g_grid->ed.on) { Buf t; t.put ('='); t.puts (o.str ()); g_grid->beginEdit (true, t.str ()); }
	else g_grid->ed.insert (o.str (), o.n);
	g_grid->ed.typed = true;
	g_grid->setFocus ();
	g_grid->changed ();
}
// AutoSum: the numbers above the cell (else at its left) summed -- the formula proposed, being typed.
static void cmd_autosum ()
{
	Sheet *s = S ();
	if (g_grid->ed.on) { g_grid->ed.insert ("SUM(", 4); g_grid->changed (); return; }
	Rect sl = used_sel ();
	auto isnum = [&] (int r, int c) { Cell *x = s->cells.get (r, c); return x && (x->vt == V_NUM) && x->kind != K_NONE; };
	if (sl.r1 > sl.r0 || sl.c1 > sl.c0)				// a range: a total under each column
	{
		undo_rect (g_b, s, Rect { sl.r0, sl.c0, sl.r1 + 1, sl.c1 });
		for (int c = sl.c0; c <= sl.c1; c++)
		{
			char a[24], z[24], f[64]; cell_name (sl.r0, c, a); cell_name (sl.r1, c, z);
			snprintf (f, sizeof f, "=SUM(%s:%s)", a, z);
			cell_input (g_b, s, sl.r1 + 1, c, f);
		}
		after_change ();
		return;
	}
	int r = s->curR, c = s->curC;
	int r0 = r, c0 = c;
	while (r0 > 0 && isnum (r0 - 1, c)) r0--;
	char f[64];
	if (r0 < r) { char a[24], z[24]; cell_name (r0, c, a); cell_name (r - 1, c, z); snprintf (f, sizeof f, "=SUM(%s:%s)", a, z); }
	else
	{
		while (c0 > 0 && isnum (r, c0 - 1)) c0--;
		if (c0 < c) { char a[24], z[24]; cell_name (r, c0, a); cell_name (r, c - 1, z); snprintf (f, sizeof f, "=SUM(%s:%s)", a, z); }
		else scpy (f, "=SUM()", sizeof f);
	}
	g_grid->beginEdit (true, f);
	if (!strcmp (f, "=SUM()")) { g_grid->ed.caret = g_grid->ed.anchor = 5; }
	g_grid->setFocus ();
	g_grid->changed ();
}
// The block of data around the cursor (the current region: until empty rows and columns).
static Rect current_region (Sheet *s, int r, int c)
{
	Rect q = { r, c, r, c };
	auto filled = [&] (int rr, int cc) { if (rr < 0 || cc < 0 || rr >= MAXR || cc >= MAXC) return false; Cell *x = s->cells.get (rr, cc); return x && x->kind != K_NONE; };
	for (bool grew = true; grew; )
	{
		grew = false;
		for (int cc = q.c0 - 1; cc <= q.c1 + 1; cc++) { if (filled (q.r0 - 1, cc)) { q.r0--; grew = true; break; } }
		for (int cc = q.c0 - 1; cc <= q.c1 + 1; cc++) { if (filled (q.r1 + 1, cc)) { q.r1++; grew = true; break; } }
		for (int rr = q.r0 - 1; rr <= q.r1 + 1; rr++) { if (filled (rr, q.c0 - 1)) { q.c0--; grew = true; break; } }
		for (int rr = q.r0 - 1; rr <= q.r1 + 1; rr++) { if (filled (rr, q.c1 + 1)) { q.c1++; grew = true; break; } }
		if (q.r1 - q.r0 > 100000 || q.c1 - q.c0 > 1000) break;
	}
	return q;
}
static bool is_text_cell (Sheet *s, int r, int c) { Cell *x = s->cells.get (r, c); return x && x->vt == V_STR; }
static void cmd_chart ()
{
	if (g_grid->ed.on && !g_grid->commit (0, 0)) return;
	Sheet *s = S ();
	Rect r = used_sel ();
	if (r.r0 == r.r1 && r.c0 == r.c1) r = current_region (s, r.r0, r.c0);
	Chart c; memset (&c, 0, sizeof c);
	c.type = CH_COLUMN; c.srcSheet = s->id; c.src = r;
	c.head = false; for (int cc = r.c0; cc <= r.c1; cc++) if (is_text_cell (s, r.r0, cc)) c.head = true;
	c.side = false; for (int rr = r.r0 + (c.head ? 1 : 0); rr <= r.r1; rr++) if (is_text_cell (s, rr, r.c0)) c.side = true;
	c.byRows = (r.c1 - r.c0) > (r.r1 - r.r0) && (r.r1 - r.r0) < 6;
	c.legend = LG_RIGHT; c.grid = true;
	c.w = 460; c.h = 290;
	c.x = col_x (s, r.c1 + 1) + 24; c.y = (int) row_y (s, r.r0);
	ChartDialog d (&g_b, c, true);
	if (d.run () != 1) return;
	undo_sheet (g_b, s);
	Chart *nc = (Chart *) malloc (sizeof (Chart)); *nc = d.c;
	chart_anchor (s, nc);
	s->charts = (Chart **) realloc (s->charts, (s->ncharts + 1) * sizeof (Chart *));
	s->charts[s->ncharts++] = nc;
	g_grid->selChart = s->ncharts - 1;
	touched ();
	g_grid->invalidate (true);
}
static void chart_open (int i)
{
	Sheet *s = S ();
	if (i < 0 || i >= s->ncharts) return;
	ChartDialog d (&g_b, *s->charts[i], false);
	if (d.run () != 1) return;
	undo_sheet (g_b, s);
	*s->charts[i] = d.c;
	touched ();
	g_grid->invalidate (true);
}
static void cmd_insert_sheet ()
{
	if (g_grid->ed.on && !g_grid->commit (0, 0)) return;
	undo_book (g_b);
	char nm[64]; book_unique_sheet_name (g_b, nm, sizeof nm);
	book_add_sheet (g_b, nm, g_b.active + 1);
	g_b.active++;
	touched ();
	g_tabs->showActive ();
	refresh ();
}
static void cmd_date_time (bool time)
{
	int y, mo, d, h, mi, se; kapi_get_datetime (&y, &mo, &d, &h, &mi, &se);
	char t[40];
	if (time) snprintf (t, sizeof t, "%02d:%02d:%02d", h, mi, se); else snprintf (t, sizeof t, "%02d/%02d/%04d", d, mo, y);
	if (g_grid->ed.on) { g_grid->ed.insert (t, (int) strlen (t)); g_grid->changed (); return; }
	Sheet *s = S ();
	undo_rect (g_b, s, Rect { s->curR, s->curC, s->curR, s->curC });
	cell_input (g_b, s, s->curR, s->curC, t);
	after_change ();
}
static void cmd_date () { cmd_date_time (false); }
static void cmd_time () { cmd_date_time (true); }

// ---- format ---------------------------------------------------------------------------------------------
// A change applied to every cell of the selection (whole rows / columns: theirs too), one undo step.
static void apply_style (StyleFn fn, const void *arg)
{
	if (g_grid->ed.on && !g_grid->commit (0, 0)) return;
	Sheet *s = S ();
	Rect r = g_grid->sel ();
	if (g_grid->wholeCols () || g_grid->wholeRows ()) undo_sheet (g_b, s); else undo_rect (g_b, s, r);
	style_range (g_b, s, r, fn, arg);
	after_change (true);
}
static const Style &cur_style () { Sheet *s = S (); return cell_style (g_b, s, s->cells.get (s->curR, s->curC), s->curR, s->curC); }
static void f_bold (Style &st, const void *a) { st.bold = a != 0; }
static void f_italic (Style &st, const void *a) { st.italic = a != 0; }
static void f_under (Style &st, const void *a) { st.under = (unsigned char) (long) a; }
static void f_strike (Style &st, const void *a) { st.strike = a != 0; }
static void f_ha (Style &st, const void *a) { st.ha = (unsigned char) (long) a; }
static void f_va (Style &st, const void *a) { st.va = (unsigned char) (long) a; }
static void f_wrap (Style &st, const void *a) { st.wrap = a != 0; }
static void f_color (Style &st, const void *a) { st.color = (unsigned) (unsigned long) a; }
static void f_fill (Style &st, const void *a) { st.fill = (unsigned) (unsigned long) a; }
static void f_fmt (Style &st, const void *a) { st.fmt = (unsigned short) (long) a; }
static void f_font (Style &st, const void *a) { st.font = (unsigned short) (long) a; }
static void f_size (Style &st, const void *a) { st.size = (unsigned short) (long) a; }
static void f_indent (Style &st, const void *a) { st.indent = (unsigned char) iclamp (st.indent + (int) (long) a, 0, 15); }
static void f_clear (Style &st, const void *) { st.reset (); }
static void cmd_bold () { apply_style (f_bold, (const void *) (long) !cur_style ().bold); }
static void cmd_italic () { apply_style (f_italic, (const void *) (long) !cur_style ().italic); }
static void cmd_under () { apply_style (f_under, (const void *) (long) (cur_style ().under ? 0 : 1)); }
static void cmd_strike () { apply_style (f_strike, (const void *) (long) !cur_style ().strike); }
static void cmd_left () { apply_style (f_ha, (const void *) (long) (cur_style ().ha == HA_LEFT ? HA_GENERAL : HA_LEFT)); }
static void cmd_center () { apply_style (f_ha, (const void *) (long) (cur_style ().ha == HA_CENTER ? HA_GENERAL : HA_CENTER)); }
static void cmd_right () { apply_style (f_ha, (const void *) (long) (cur_style ().ha == HA_RIGHT ? HA_GENERAL : HA_RIGHT)); }
static void cmd_vtop () { apply_style (f_va, (const void *) (long) VA_TOP); }
static void cmd_vmid () { apply_style (f_va, (const void *) (long) VA_CENTER); }
static void cmd_vbot () { apply_style (f_va, (const void *) (long) VA_BOTTOM); }
static void cmd_wrap () { apply_style (f_wrap, (const void *) (long) !cur_style ().wrap); }
static void cmd_indent () { apply_style (f_indent, (const void *) (long) 1); }
static void cmd_outdent () { apply_style (f_indent, (const void *) (long) -1); }
static void cmd_text_color () { apply_style (f_color, (const void *) (unsigned long) g_textColor); }
static void cmd_fill_color () { apply_style (f_fill, (const void *) (unsigned long) g_fillColor); }
static void cmd_clear_format ()
{
	if (g_grid->ed.on) return;
	Sheet *s = S (); Rect r = g_grid->sel ();
	if (g_grid->wholeCols () || g_grid->wholeRows ()) undo_sheet (g_b, s); else undo_rect (g_b, s, r);
	style_range (g_b, s, r, f_clear, 0);
	after_change (true);
}
static void cmd_merge ()
{
	if (g_grid->ed.on && !g_grid->commit (0, 0)) return;
	Sheet *s = S ();
	Rect r = g_grid->sel ();
	if (g_grid->wholeCols () || g_grid->wholeRows ()) { message ("Merge a range of cells, not whole rows or columns."); return; }
	undo_sheet (g_b, s);
	int mi = merge_at (s, r.r0, r.c0);
	if (mi >= 0 && s->merges[mi].r0 == r.r0 && s->merges[mi].c0 == r.c0 && s->merges[mi].r1 == r.r1 && s->merges[mi].c1 == r.c1) unmerge_range (s, r);
	else
	{
		// (the other cells' content goes: asked first when there is some)
		bool more = false;
		for (int rr = r.r0; rr <= r.r1 && !more; rr++) for (int c = r.c0; c <= r.c1; c++) { if (rr == r.r0 && c == r.c0) continue; Cell *x = s->cells.get (rr, c); if (x && x->kind != K_NONE) { more = true; break; } }
		if (more && note ("Merge Cells", "Only the top left cell's content is kept. Merge anyway?", MB_OKCANCEL) != 1) { undo_step (g_b, false); ustack_clear (g_redo); return; }
		merge_range (g_b, s, r);
		Rect one = { r.r0, r.c0, r.r0, r.c0 };
		style_range (g_b, s, one, f_ha, (const void *) (long) HA_CENTER);
		style_range (g_b, s, one, f_va, (const void *) (long) VA_CENTER);
	}
	g_grid->selectRange (r);
	after_change ();
}
// Number formats from the toolbar.
static void set_format (const char *code) { apply_style (f_fmt, (const void *) (long) book_fmt (g_b, code)); }
static const char *g_curSym = "\xE2\x82\xAC";
static void cmd_currency ()
{
	char c[64];
	if (!strcmp (g_curSym, "$")) scpy (c, "$#,##0.00;[Red]-$#,##0.00", sizeof c);
	else snprintf (c, sizeof c, "#,##0.00 \"%s\";[Red]-#,##0.00 \"%s\"", g_curSym, g_curSym);
	set_format (c);
}
static void drop_currency (ToolButton &b)
{
	int x, y; b.below (&x, &y);
	PopupMenu m (x, y);
	for (int i = 0; i < 5; i++) m.add (CURRENCY_NAMES[i], i + 1);
	int r = m.run ();
	if (r > 0) { g_curSym = CURRENCIES[r - 1]; cmd_currency (); }
}
static void cmd_percent () { set_format ("0%"); }
static void cmd_thousands () { set_format ("#,##0.00"); }
static void change_decimals (int d)
{
	Sheet *s = S ();
	Cell *x = s->cells.get (s->curR, s->curC);
	const char *code = book_fmt_code (g_b, cur_style ().fmt);
	int gen = 0;
	if (x && x->vt == V_NUM)				// (General: as many decimals as are shown now)
	{
		char t[40]; num_general (x->num, t, sizeof t, 11);
		char *dot = strchr (t, '.'); if (dot && !strchr (t, 'E')) gen = (int) strlen (dot + 1);
	}
	char out[160]; fmt_add_decimals (code, d, gen, out, sizeof out);
	set_format (out);
}
static void cmd_dec_plus () { change_decimals (1); }
static void cmd_dec_minus () { change_decimals (-1); }
// Borders from the toolbar: presets over the selection.
enum { BP_NONE, BP_ALL, BP_OUTLINE, BP_THICKOUT, BP_BOTTOM, BP_TOP, BP_LEFT, BP_RIGHT, BP_DBOTTOM, BP_THICKBOTTOM, BP_INSIDE };
static const char *const BP_NAMES[] = { "No Borders", "All Borders", "Outside Borders", "Thick Outside Borders", "Bottom Border", "Top Border", "Left Border", "Right Border", "Double Bottom Border", "Thick Bottom Border", "Inside Borders" };
static void apply_borders (int preset, int lineStyle, unsigned color, const BorderSet *custom = 0)
{
	if (g_grid->ed.on && !g_grid->commit (0, 0)) return;
	Sheet *s = S ();
	Rect r = used_sel ();
	if ((long long) (r.r1 - r.r0 + 1) * (r.c1 - r.c0 + 1) > 1000000) return;
	undo_rect (g_b, s, Rect { imax (0, r.r0 - 1), imax (0, r.c0 - 1), imin (MAXR - 1, r.r1 + 1), imin (MAXC - 1, r.c1 + 1) });
	for (int rr = r.r0; rr <= r.r1; rr++)
		for (int c = r.c0; c <= r.c1; c++)
		{
			Cell *x = cell_make (s, rr, c);
			Style st = g_b.styles.s[x->style];
			bool top = rr == r.r0, bot = rr == r.r1, lef = c == r.c0, rig = c == r.c1;
			auto set = [&] (int side, bool on, int bs) { st.bs[side] = (unsigned char) (on ? bs : BS_NONE); st.bc[side] = on ? color : 0; };
			switch (preset)
			{
			case BP_NONE: for (int k = 0; k < 4; k++) set (k, false, 0); break;
			case BP_ALL: for (int k = 0; k < 4; k++) set (k, true, lineStyle); break;
			case BP_OUTLINE: case BP_THICKOUT:
			{
				int bs = preset == BP_THICKOUT ? BS_THICK : lineStyle;
				if (top) set (B_TOP, true, bs);
				if (bot) set (B_BOTTOM, true, bs);
				if (lef) set (B_LEFT, true, bs);
				if (rig) set (B_RIGHT, true, bs);
				break;
			}
			case BP_INSIDE:
				if (!top) set (B_TOP, true, lineStyle);
				if (!bot) set (B_BOTTOM, true, lineStyle);
				if (!lef) set (B_LEFT, true, lineStyle);
				if (!rig) set (B_RIGHT, true, lineStyle);
				break;
			case BP_BOTTOM: if (bot) set (B_BOTTOM, true, lineStyle); break;
			case BP_TOP: if (top) set (B_TOP, true, lineStyle); break;
			case BP_LEFT: if (lef) set (B_LEFT, true, lineStyle); break;
			case BP_RIGHT: if (rig) set (B_RIGHT, true, lineStyle); break;
			case BP_DBOTTOM: if (bot) set (B_BOTTOM, true, BS_DOUBLE); break;
			case BP_THICKBOTTOM: if (bot) set (B_BOTTOM, true, BS_THICK); break;
			case -1:
				if (custom)
				{
					const int *e = custom->edge;
					if (lef && e[0] >= 0) set (B_LEFT, e[0] == 1, lineStyle);
					if (rig && e[1] >= 0) set (B_RIGHT, e[1] == 1, lineStyle);
					if (top && e[2] >= 0) set (B_TOP, e[2] == 1, lineStyle);
					if (bot && e[3] >= 0) set (B_BOTTOM, e[3] == 1, lineStyle);
					if (!bot && e[4] >= 0) set (B_BOTTOM, e[4] == 1, lineStyle);	// (inside: horizontal)
					if (!top && e[4] >= 0) set (B_TOP, e[4] == 1, lineStyle);
					if (!rig && e[5] >= 0) set (B_RIGHT, e[5] == 1, lineStyle);	// (inside: vertical)
					if (!lef && e[5] >= 0) set (B_LEFT, e[5] == 1, lineStyle);
				}
				break;
			}
			x->style = (unsigned short) g_b.styles.intern (st);
			cell_drop_if_plain (s, x);
		}
	after_change ();
}
static void cmd_borders () { apply_borders (g_borderPreset, BS_THIN, 0); }
static void drop_borders (ToolButton &b)
{
	int x, y; b.below (&x, &y);
	PopupMenu m (x, y);
	for (int i = 0; i < 11; i++) m.add (BP_NAMES[i], i + 1);
	int r = m.run ();
	if (r > 0) { g_borderPreset = r - 1; apply_borders (g_borderPreset, BS_THIN, 0); }
}
static void drop_text_color (ToolButton &b)
{
	int x, y; b.below (&x, &y);
	ColorPopup p (x, y, g_cols, 60, 10, "Automatic");
	long c = p.pick ();
	if (c == -1) return;
	g_textColor = (unsigned) c; b.setBar (g_textColor == AUTO ? 0 : g_textColor);
	cmd_text_color ();
}
static void drop_fill_color (ToolButton &b)
{
	int x, y; b.below (&x, &y);
	ColorPopup p (x, y, g_cols, 60, 10, "No Fill");
	long c = p.pick ();
	if (c == -1) return;
	g_fillColor = (unsigned) c; b.setBar (g_fillColor == AUTO ? 0xFE000000u : g_fillColor);
	cmd_fill_color ();
}
// Format Cells: what was changed set over the selection.
struct Diff { Style from, to; };
static void f_diff (Style &st, const void *a)
{
	const Diff *d = (const Diff *) a;
	const Style &f = d->from, &t = d->to;
	if (f.font != t.font) st.font = t.font;
	if (f.size != t.size) st.size = t.size;
	if (f.bold != t.bold) st.bold = t.bold;
	if (f.italic != t.italic) st.italic = t.italic;
	if (f.under != t.under) st.under = t.under;
	if (f.strike != t.strike) st.strike = t.strike;
	if (f.color != t.color) st.color = t.color;
	if (f.fill != t.fill) st.fill = t.fill;
	if (f.ha != t.ha) st.ha = t.ha;
	if (f.va != t.va) st.va = t.va;
	if (f.wrap != t.wrap) st.wrap = t.wrap;
	if (f.indent != t.indent) st.indent = t.indent;
	if (f.fmt != t.fmt) st.fmt = t.fmt;
}
static void cmd_format_cells ()
{
	if (g_grid->ed.on && !g_grid->commit (0, 0)) return;
	Sheet *s = S ();
	Cell *x = s->cells.get (s->curR, s->curC);
	Shown sh; cell_shown (g_b, s, x, sh, 30);
	Rect r = g_grid->sel ();
	int mi = merge_at (s, r.r0, r.c0);
	bool merged = mi >= 0 && s->merges[mi].r1 == r.r1 && s->merges[mi].c1 == r.c1 && (r.r1 > r.r0 || r.c1 > r.c0);
	FormatDialog d (&g_b, cur_style (), x && x->vt == V_NUM ? x->num : 1234.5678, !x || x->vt == V_NUM || x->kind == K_NONE, x && x->vt == V_STR ? x->str : "", merged);
	if (d.run () != 1) { g_grid->setFocus (); return; }
	if (g_grid->wholeCols () || g_grid->wholeRows ()) undo_sheet (g_b, s); else undo_sheet (g_b, s);
	Diff df; df.from = d.orig; df.to = d.st;
	style_range (g_b, s, r, f_diff, &df);
	bool anyEdge = false; for (int k = 0; k < 6; k++) if (d.bset.edge[k] >= 0) anyEdge = true;
	if (anyEdge)
	{
		ustack_clear (g_redo);
		BorderSet bs = d.bset;
		// (in the same undo step: the sheet was written down already)
		int n = g_undo.n;
		apply_borders (-1, BS_VALS[d.bstyle->sel], d.bcol->color == AUTO ? 0 : d.bcol->color, &bs);
		while (g_undo.n > n) { g_undo.n--; free (g_undo.s[g_undo.n].data); }
	}
	if (d.mergeOn != d.mergeOrig && !g_grid->wholeCols () && !g_grid->wholeRows ())
	{
		if (d.mergeOn) merge_range (g_b, s, r); else unmerge_range (s, r);
	}
	after_change (true);
	g_grid->setFocus ();
}
static void cmd_row_height ()
{
	Sheet *s = S (); Rect r = g_grid->sel ();
	char t[32]; snprintf (t, sizeof t, "%g", row_h (s, s->curR) * 0.75);
	AskDialog d ("Row Height", "Height (points):", t);
	if (d.run () != 1) return;
	double pt = strtod (d.value, 0);
	if (pt <= 0 || pt > 1000) return;
	undo_sheet (g_b, s);
	for (int rr = r.r0; rr <= imin (r.r1, r.r0 + 100000); rr++) { RowInfo *ri = row_add (s, rr); ri->h = (unsigned short) (pt * 4 / 3 + 0.5); ri->fl |= RF_CUSTOM; ri->fl &= ~RF_HIDDEN; }
	rows_changed (s); touched (); g_grid->invalidate (true);
}
static void cmd_col_width ()
{
	Sheet *s = S (); Rect r = g_grid->sel ();
	char t[32]; snprintf (t, sizeof t, "%.2f", (col_w (s, s->curC) - 5) / 7.0);
	AskDialog d ("Column Width", "Width (characters of the default font):", t);
	if (d.run () != 1) return;
	double ch = strtod (d.value, 0);
	if (ch < 0 || ch > 255) return;
	undo_sheet (g_b, s);
	for (int c = r.c0; c <= r.c1; c++) { s->colW[c] = (unsigned short) (ch * 7 + 5 + 0.5); s->colFl[c] &= ~RF_HIDDEN; }
	cols_changed (s); touched (); g_grid->invalidate (true);
}
static void cmd_opt_width ()
{
	Sheet *s = S (); Rect r = g_grid->sel ();
	undo_sheet (g_b, s);
	for (int c = r.c0; c <= imin (r.c1, r.c0 + 500); c++) { s->colW[c] = (unsigned short) fit_col_width (g_b, s, c); s->colFl[c] &= ~RF_HIDDEN; }
	cols_changed (s); touched (); g_grid->invalidate (true);
}
static void cmd_hide_rows ()
{
	Sheet *s = S (); Rect r = g_grid->sel ();
	undo_sheet (g_b, s);
	for (int rr = r.r0; rr <= imin (r.r1, r.r0 + 100000); rr++) row_add (s, rr)->fl |= RF_HIDDEN;
	rows_changed (s); touched ();
	g_grid->selectCell (imin (MAXR - 1, r.r1 + 1), s->curC, false);
}
static void cmd_hide_cols ()
{
	Sheet *s = S (); Rect r = g_grid->sel ();
	undo_sheet (g_b, s);
	for (int c = r.c0; c <= r.c1; c++) s->colFl[c] |= RF_HIDDEN;
	cols_changed (s); touched ();
	g_grid->selectCell (s->curR, imin (MAXC - 1, r.c1 + 1), false);
}
static void cmd_show_all ()
{
	Sheet *s = S (); Rect r = g_grid->sel ();
	if (r.r0 == r.r1 && r.c0 == r.c1) { r.r0 = 0; r.r1 = MAXR - 1; r.c0 = 0; r.c1 = MAXC - 1; }
	undo_sheet (g_b, s);
	for (int c = imax (0, r.c0 - 1); c <= imin (MAXC - 1, r.c1 + 1); c++) s->colFl[c] &= ~RF_HIDDEN;
	for (int i = 0; i < s->nrows; i++) if (s->rows[i].r >= r.r0 - 1 && s->rows[i].r <= r.r1 + 1) s->rows[i].fl &= ~RF_HIDDEN;
	cols_changed (s); rows_changed (s); touched (); g_grid->invalidate (true);
}

// ---- sheets -----------------------------------------------------------------------------------------------
static void pick_sheet (int i)
{
	if (i < 0 || i >= g_b.ns || i == g_b.active) return;
	if (g_grid->ed.on && !g_grid->ed.pointing && !g_grid->commit (0, 0)) return;
	g_b.active = i;
	g_grid->resetSelKind (); g_grid->selChart = -1;
	g_tabs->showActive ();
	g_grid->invalidate (true);
	refresh ();
}
static void cmd_delete_sheet ()
{
	if (g_b.ns <= 1) { message ("A workbook keeps one sheet at least."); return; }
	char m[160]; snprintf (m, sizeof m, "Delete the sheet \"%s\"? (Undo brings it back.)", S ()->name);
	char l1[160]; u8_to_latin1 (m, l1, sizeof l1);
	if (note ("Delete Sheet", l1, MB_OKCANCEL) != 1) return;
	undo_book (g_b);
	delete_sheet (g_b, g_b.active);
	recalc (g_b); touched ();
	g_tabs->showActive (); g_grid->invalidate (true); refresh ();
}
static void rename_sheet (int i)
{
	char cur[64]; u8_to_latin1 (g_b.sh[i]->name, cur, sizeof cur);
	AskDialog d ("Rename Sheet", "The sheet's name:", cur);
	if (d.run () != 1 || !d.value[0]) return;
	char *u = latin1_to_u8 (d.value, (int) strlen (d.value));
	for (char *p = u; *p; p++) if (strchr ("[]:*?/\\", *p)) *p = '_';
	int k = book_sheet_index (g_b, u);
	if (k >= 0 && k != i) { message ("Another sheet has this name."); free (u); return; }
	undo_book (g_b);
	scpy (g_b.sh[i]->name, u, sizeof g_b.sh[i]->name);
	free (u);
	touched (); g_tabs->invalidate (true); g_grid->invalidate (true); refresh ();
}
static void cmd_rename_sheet () { rename_sheet (g_b.active); }
static void cmd_duplicate_sheet () { undo_book (g_b); if (duplicate_sheet (g_b, g_b.active)) { g_b.active++; recalc (g_b); touched (); g_tabs->showActive (); refresh (); } }
static void cmd_sheet_left () { if (g_b.active > 0) { undo_book (g_b); move_sheet (g_b, g_b.active, g_b.active - 1); g_b.active--; touched (); g_tabs->showActive (); } }
static void cmd_sheet_right () { if (g_b.active < g_b.ns - 1) { undo_book (g_b); move_sheet (g_b, g_b.active, g_b.active + 1); g_b.active++; touched (); g_tabs->showActive (); } }
static void tab_menu (int i, int x, int y)
{
	(void) i;
	PopupMenu m (x + g_tabs->left, y + g_tabs->top - 200);
	m.add ("Insert Sheet", 1); m.add ("Delete Sheet", 2); m.add ("Rename...", 3); m.add ("Duplicate", 4);
	m.separator (); m.add ("Move Left", 5); m.add ("Move Right", 6); m.separator (); m.add ("Tab Colour...", 7);
	switch (m.run ())
	{
	case 1: cmd_insert_sheet (); break; case 2: cmd_delete_sheet (); break; case 3: cmd_rename_sheet (); break; case 4: cmd_duplicate_sheet (); break;
	case 5: cmd_sheet_left (); break; case 6: cmd_sheet_right (); break;
	case 7:
	{
		ColorPopup p (x + g_tabs->left, y + g_tabs->top - 240, g_cols, 60, 10, "No Colour");
		long c = p.pick ();
		if (c != -1) { undo_book (g_b); S ()->tab = (unsigned) c; touched (); g_tabs->invalidate (true); }
		break;
	}
	}
}

// ---- data ------------------------------------------------------------------------------------------------
static bool looks_header (Sheet *s, Rect r)			// the first row texts, the next one numbers somewhere
{
	if (r.r1 <= r.r0) return false;
	bool text = true, below = false;
	for (int c = r.c0; c <= r.c1; c++)
	{
		Cell *x = s->cells.get (r.r0, c);
		if (x && x->kind != K_NONE && x->vt != V_STR) text = false;
		Cell *y = s->cells.get (r.r0 + 1, c);
		if (y && y->vt != V_STR && y->kind != K_NONE) below = true;
	}
	return text && below;
}
static void sort_by (bool desc)
{
	if (g_grid->ed.on && !g_grid->commit (0, 0)) return;
	Sheet *s = S ();
	Rect r = used_sel ();
	int keyCol = s->curC;
	if (r.r0 == r.r1 && r.c0 == r.c1) r = current_region (s, r.r0, r.c0);
	if (r.r1 <= r.r0) return;
	bool hdr = looks_header (s, r);
	undo_rect (g_b, s, r);
	SortKey k = { keyCol, desc };
	sort_range (g_b, s, r, &k, 1, hdr);
	after_change ();
}
static void cmd_sort_asc () { sort_by (false); }
static void cmd_sort_desc () { sort_by (true); }
static void cmd_sort ()
{
	if (g_grid->ed.on && !g_grid->commit (0, 0)) return;
	Sheet *s = S ();
	Rect r = used_sel ();
	if (r.r0 == r.r1 && r.c0 == r.c1) r = current_region (s, r.r0, r.c0);
	if (r.c1 - r.c0 >= 64) r.c1 = r.c0 + 63;
	SortDialog d (&g_b, s, r, looks_header (s, r));
	if (d.run () != 1) return;
	SortKey keys[3]; int n = 0;
	for (int k = 0; k < 3; k++) if (d.key[k]->sel < d.ncols) { keys[n].col = r.c0 + d.key[k]->sel; keys[n].desc = d.ord[k]->sel == 1; n++; }
	if (!n) return;
	g_grid->selectRange (r);
	undo_rect (g_b, s, r);
	sort_range (g_b, s, r, keys, n, d.hdr->checked);
	after_change ();
}
static void cmd_recalc () { recalc (g_b); g_grid->invalidate (true); refresh (); }
// Data > AutoFilter: the buttons put on the table's headers (the selection, else the table around the
// cursor), or taken off with the filters.
static void cmd_autofilter ()
{
	if (g_grid->ed.on && !g_grid->commit (0, 0)) return;
	Sheet *s = S ();
	undo_sheet (g_b, s);
	if (s->af.on) af_clear (s);
	else
	{
		Rect r = used_sel ();
		if (r.r0 == r.r1 && r.c0 == r.c1) r = current_region (s, r.r0, r.c0);
		if (r.r1 <= r.r0) { undo_drop_last (); message ("Choose a table first: its first row holds the headers, the rows under it the data."); return; }
		s->af.on = true; s->af.r = r;
	}
	touched (); g_grid->invalidate (true); refresh ();
}
// A filter button clicked: its drop-down (sort; the values shown).
static void filter_drop (int col, int x, int y)
{
	if (g_grid->ed.on && !g_grid->commit (0, 0)) return;
	Sheet *s = S ();
	AfVal *vals = 0; int n = af_values (g_b, s, col, &vals);
	int k = af_index (s, col);
	FilterPopup p (x, y, vals, n, k >= 0 ? s->af.shown[k] : 0);
	int r = p.run ();
	if (r == 2 || r == 3)
	{
		undo_sheet (g_b, s);
		SortKey key = { col, r == 3 };
		sort_range (g_b, s, s->af.r, &key, 1, true);
		recalc (g_b);
		af_apply (g_b, s);
		touched (); g_grid->invalidate (true); refresh ();
	}
	else if (r == 1)
	{
		undo_sheet (g_b, s);
		if (p.allChecked ()) af_set (s, col, 0);
		else { Buf l; for (int i = 0; i < n; i++) if (p.checked (i)) { l.puts (vals[i].t); l.put ('\x1F'); } af_set (s, col, l.str ()); }
		af_apply (g_b, s);
		touched (); g_grid->invalidate (true); refresh ();
	}
	for (int i = 0; i < n; i++) free (vals[i].t);
	free (vals);
	g_grid->setFocus ();
}

static bool other_key (long k, int mods)
{
	bool ctrl = mods & MOD_CTRL, shift = mods & MOD_SHIFT;
	if (k == KEY_DEL) { cmd_clear_contents (); return true; }
	if (k == KEY_F1 + 8) { cmd_recalc (); return true; }			// F9
	if (ctrl && k == '1') { cmd_format_cells (); return true; }
	if (ctrl && (k == ';' || k == ',')) { cmd_date (); return true; }
	if (ctrl && (k == ':' || (k == ';' && shift) || k == '.')) { cmd_time (); return true; }
	if (ctrl && (k == '+' || k == '=')) { Rect r = g_grid->sel (); if (g_grid->wholeCols ()) cmd_ins_cols (); else if (g_grid->wholeRows () || r.c1 == r.c0) cmd_ins_rows (); else cmd_ins_rows (); return true; }
	if (ctrl && k == '-') { if (g_grid->wholeCols ()) cmd_del_cols (); else cmd_del_rows (); return true; }
	if (ctrl && k == '`') { cmd_formulas (); return true; }
	return false;
}

// ---- the toolbars' pick boxes ----------------------------------------------------------------------------
static void font_value (Canvas &cv, int x, int y, int w, int h, unsigned ink)
{
	const Style &st = cur_style ();
	int fam = book_family (g_b, st.font);
	fnt::Font *f = fnt::get (fam, 0, 14 * 64);
	char l1[64]; u8_to_latin1 (g_b.fonts[st.font < g_b.nfonts ? st.font : 0], l1, sizeof l1);
	text_at (cv, f, x, y + h / 2 + 5, g_b.fonts[st.font < g_b.nfonts ? st.font : 0], ink, 0, Rect { y, x, y + h - 1, x + w - 1 });
	(void) l1;
}
static void font_row (Canvas &cv, int i, int x, int y, int w, int h, unsigned ink)
{
	fnt::Font *f = fnt::get (i, 0, 15 * 64);
	text_at (cv, f, x, y + h / 2 + 5, fnt::name (i), ink, 0, Rect { y, x, y + h - 1, x + w - 1 });
}
static void pick_font (PickBox &p)
{
	int x, y; p.below (&x, &y);
	ListPopup lp (x, y, 240, fnt::count (), 26, book_family (g_b, cur_style ().font), font_row, 14);
	int r = lp.pick ();
	if (r >= 0) apply_style (f_font, (const void *) (long) book_font (g_b, fnt::name (r)));
	g_grid->setFocus ();
}
static void size_value (Canvas &cv, int x, int y, int w, int h, unsigned ink)
{
	char t[16]; snprintf (t, sizeof t, "%g", cur_style ().size / 10.0);
	uk_text_l (cv, x, y, h, t, ink); (void) w;
}
static void size_row (Canvas &cv, int i, int x, int y, int, int h, unsigned ink) { uk_text_l (cv, x, y, h, SIZE_NAMES[i], ink); }
static void pick_size (PickBox &p)
{
	int x, y; p.below (&x, &y);
	int sel = 4; for (int i = 0; i < 23; i++) if (SIZE_VALS[i] == cur_style ().size) sel = i;
	ListPopup lp (x, y, 80, 23, 22, sel, size_row, 14);
	int r = lp.pick ();
	if (r >= 0) apply_style (f_size, (const void *) (long) SIZE_VALS[r]);
	g_grid->setFocus ();
}
static void zoom_value (Canvas &cv, int x, int y, int w, int h, unsigned ink) { char t[16]; snprintf (t, sizeof t, "%d%%", g_grid->z); uk_text_l (cv, x, y, h, t, ink); (void) w; }
static void zoom_row (Canvas &cv, int i, int x, int y, int, int h, unsigned ink) { char t[16]; snprintf (t, sizeof t, "%d%%", ZOOMS[i]); uk_text_l (cv, x, y, h, t, ink); }
static void pick_zoom (PickBox &p)
{
	int x, y; p.below (&x, &y);
	int sel = 4; for (int i = 0; i < NZOOMS; i++) if (ZOOMS[i] == g_grid->z) sel = i;
	ListPopup lp (x, y, 90, NZOOMS, 22, sel, zoom_row, 13);
	int r = lp.pick ();
	if (r >= 0) set_zoom (ZOOMS[r]);
	g_grid->setFocus ();
}

// ---- the state shown -----------------------------------------------------------------------------------------
static void refresh ()
{
	if (!g_grid) return;
	Sheet *s = S ();
	const Style &st = cur_style ();
	auto on = [] (int ic, bool v) { if (g_btn[ic]) g_btn[ic]->setOn (v); };
	on (wr::IC_BOLD, st.bold); on (wr::IC_ITALIC, st.italic); on (wr::IC_UNDER, st.under != 0); on (wr::IC_STRIKE, st.strike);
	on (wr::IC_LEFT, st.ha == HA_LEFT); on (wr::IC_CENTER, st.ha == HA_CENTER); on (wr::IC_RIGHT, st.ha == HA_RIGHT);
	on (SI_VTOP, st.va == VA_TOP); on (SI_VMID, st.va == VA_CENTER); on (SI_VBOT, st.va == VA_BOTTOM);
	on (SI_WRAP, st.wrap); on (SI_MERGE, merge_at (s, s->curR, s->curC) >= 0); on (SI_FREEZE, s->freezeR || s->freezeC);
	on (SI_FILTER, s->af.on);
	if (g_btn[wr::IC_UNDO]) g_btn[wr::IC_UNDO]->setDisabled (g_undo.n == 0 && !g_grid->ed.on);
	if (g_btn[wr::IC_REDO]) g_btn[wr::IC_REDO]->setDisabled (g_redo.n == 0);
	g_fontBox->invalidate (true); g_sizeBox->invalidate (true); g_zoomBox->invalidate (true);
	g_fbar->sync ();
	g_tabs->invalidate (true);
	// the status bar: the sheet, the mode, the selection's figures
	char l[64], m[160];
	static const char *const MODES[4] = { "Ready", "Enter", "Edit", "Point" };
	snprintf (l, sizeof l, "Sheet %d of %d    %s%s", g_b.active + 1, g_b.ns, MODES[g_grid->mode ()], changed_doc () ? "    (modified)" : "");
	m[0] = 0;
	Rect r = used_sel ();
	int mi = merge_at (s, r.r0, r.c0);
	bool oneCell = mi >= 0 && s->merges[mi].r1 == r.r1 && s->merges[mi].c1 == r.c1;	// (a merged cell)
	if (!g_grid->ed.on && !oneCell && (r.r1 > r.r0 || r.c1 > r.c0))
	{
		double sum = 0; int cnt = 0, cnta = 0;
		int fmt = -1; bool mixed = false;				// (the numbers' format, when they share one)
		long long area = (long long) (r.r1 - r.r0 + 1) * (r.c1 - r.c0 + 1);
		if (area <= 2000000)
			for (int k = 0; k < s->cells.cap; k++)
			{
				Cell *x = s->cells.t[k];
				if (!x || !rect_has (r, x->r, x->c) || x->kind == K_NONE) continue;
				cnta++;
				if (x->vt != V_NUM) continue;
				sum += x->num; cnt++;
				int f = g_b.styles.s[x->style].fmt;
				if (fmt < 0) fmt = f; else if (f != fmt) mixed = true;
			}
		if (cnta)
		{
			// (the figures in the numbers' format when they all have the same one: 1,234.50 €)
			const char *code = mixed || fmt < 0 ? "General" : book_fmt_code (g_b, fmt);
			int fk = fmt_kind (code);
			if (fk == FK_TEXT || fk == FK_DATE || fk == FK_TIME || fk == FK_DATETIME) code = "General";
			Buf a, a2;
			fmt_number (code, sum, a, 0, 16); fmt_number (code, cnt ? sum / cnt : 0, a2, 0, 16);
			if (cnt) snprintf (m, sizeof m, "Sum: %s    Average: %s    Count: %d", a.str (), a2.str (), cnta);
			else snprintf (m, sizeof m, "Count: %d", cnta);
		}
	}
	if (!m[0] && g_path[0]) { char l1[100]; u8_to_latin1 (base_name (g_path), l1, sizeof l1); snprintf (m, sizeof m, "%s", l1); }
	g_status->set (l, m, g_grid->z);
}

// ---- the window ------------------------------------------------------------------------------------------
class SheetRoot : public Root
{
public:
	SheetRoot () : Root (W, H, "Spreadsheet") {}
	void onTick () override { if (g_grid) g_grid->tick (); }
	void onDrop (int, int, int type, const char *data, int len, unsigned) override
	{
		if (type == DND_TEXT && len > 0)
		{
			Sheet *s = S ();
			char *u = u8_valid (data, len) ? sdup (data, len) : latin1_to_u8 (data, len);
			undo_rect (g_b, s, Rect { s->curR, s->curC, imin (MAXR - 1, s->curR + 1000), imin (MAXC - 1, s->curC + 100) });
			paste_text (g_b, s, s->curR, s->curC, u);
			free (u);
			after_change ();
			return;
		}
		char path[200];
		if (type != DND_FILES || !doc_first_path (data, path, sizeof path)) return;
		void *d = kapi_opendir (path);
		if (d) { kapi_closedir (d); return; }
		if (!doc_confirm (g_path[0] ? base_name (g_path) : "Untitled", changed_doc (), save_for_guard)) return;
		load_path (path);
	}
};

static ToolButton *button (ToolBar *tb, int ic, const char *tip, void (*cb) (), int gap = 1, bool split = false)
{
	ToolButton *b = new ToolButton (ic, tip, cb, split);
	tb->add (b, gap);
	if (ic >= 0 && ic < 140) g_btn[ic] = b;
	return b;
}
static void fbar_cancel () { g_grid->cancelEdit (); g_grid->setFocus (); refresh (); }
static void fbar_accept () { g_grid->commit (0, 0); g_grid->setFocus (); refresh (); }
static void fbar_focus () { refresh (); }
static void tabs_add () { cmd_insert_sheet (); }

int main (void)
{
	SheetRoot root;
	root.attach ();				// (a question asked before run (): its clicks and keys)
	uikit::init ();
	if (!fnt::init ())
	{
		note ("Spreadsheet", "No TrueType fonts in SD:/res/fonts: the spreadsheet cannot draw its cells.");
		return 1;
	}
	g_famSans = fnt::find ("Liberation Sans"); if (g_famSans < 0) g_famSans = 0;
	g_famUI = fnt::find ("DejaVu Sans"); if (g_famUI < 0) g_famUI = g_famSans;
	g_now = now_clock;
	fn_init ();
	make_palettes ();
	fonts_reset ();
	book_init (g_b);
	book_add_sheet (g_b, "Sheet1");

	// the toolbars
	ToolBar *tb1 = new ToolBar (0, 0, W), *tb2 = new ToolBar (0, TB_H, W);
	root.addChild (tb1); root.addChild (tb2);
	tb1->anchor = tb2->anchor = ANCHOR_LEFT | ANCHOR_TOP | ANCHOR_RIGHT;
	button (tb1, wr::IC_NEW, "New workbook (Ctrl+N)", cmd_new);
	button (tb1, wr::IC_OPEN, "Open... (Ctrl+O)", cmd_open);
	button (tb1, wr::IC_SAVE, "Save (Ctrl+S)", cmd_save);
	tb1->sep ();
	button (tb1, wr::IC_UNDO, "Undo (Ctrl+Z)", cmd_undo);
	button (tb1, wr::IC_REDO, "Redo (Ctrl+Y)", cmd_redo);
	tb1->sep ();
	button (tb1, wr::IC_CUT, "Cut (Ctrl+X)", cmd_cut);
	button (tb1, wr::IC_COPY, "Copy (Ctrl+C)", cmd_copy);
	button (tb1, wr::IC_PASTE, "Paste (Ctrl+V)", cmd_paste);
	tb1->sep ();
	button (tb1, wr::IC_FIND, "Find and Replace (Ctrl+F)", cmd_find);
	button (tb1, SI_SORTASC, "Sort ascending (by the cursor's column)", cmd_sort_asc);
	button (tb1, SI_SORTDESC, "Sort descending", cmd_sort_desc);
	button (tb1, SI_FILTER, "AutoFilter: buttons on the table's headers to show some rows only", cmd_autofilter);
	tb1->sep ();
	button (tb1, SI_INSROW, "Insert rows above", cmd_ins_rows);
	button (tb1, SI_INSCOL, "Insert columns before", cmd_ins_cols);
	button (tb1, SI_DELROW, "Delete rows", cmd_del_rows);
	button (tb1, SI_DELCOL, "Delete columns", cmd_del_cols);
	tb1->sep ();
	button (tb1, SI_CHART, "Insert a chart of the selection", cmd_chart);
	button (tb1, SI_FREEZE, "Freeze the rows above and the columns left of the cursor", cmd_freeze);
	tb1->sep ();
	button (tb1, wr::IC_ZOOMOUT, "Zoom out", cmd_zoom_out);
	g_zoomBox = new PickBox (84, "Zoom", zoom_value, pick_zoom);
	tb1->add (g_zoomBox, 2);
	button (tb1, wr::IC_ZOOMIN, "Zoom in", cmd_zoom_in, 2);

	g_fontBox = new PickBox (170, "Font", font_value, pick_font);
	tb2->add (g_fontBox, 0);
	g_sizeBox = new PickBox (62, "Font size", size_value, pick_size);
	tb2->add (g_sizeBox, 6);
	tb2->sep ();
	button (tb2, wr::IC_BOLD, "Bold (Ctrl+B)", cmd_bold);
	button (tb2, wr::IC_ITALIC, "Italic (Ctrl+I)", cmd_italic);
	button (tb2, wr::IC_UNDER, "Underline (Ctrl+U)", cmd_under);
	button (tb2, wr::IC_STRIKE, "Strikethrough", cmd_strike);
	tb2->sep ();
	ToolButton *tc = button (tb2, wr::IC_COLOR, "Font colour", cmd_text_color, 1, true);
	tc->arrow = drop_text_color; tc->setBar (g_textColor);
	ToolButton *tf = button (tb2, SI_FILL, "Fill colour", cmd_fill_color, 2, true);
	tf->arrow = drop_fill_color; tf->setBar (g_fillColor);
	tb2->sep ();
	button (tb2, wr::IC_LEFT, "Align left", cmd_left);
	button (tb2, wr::IC_CENTER, "Centre", cmd_center);
	button (tb2, wr::IC_RIGHT, "Align right", cmd_right);
	button (tb2, SI_VTOP, "Top", cmd_vtop, 4);
	button (tb2, SI_VMID, "Middle", cmd_vmid);
	button (tb2, SI_VBOT, "Bottom", cmd_vbot);
	tb2->sep ();
	button (tb2, SI_WRAP, "Wrap text", cmd_wrap);
	button (tb2, SI_MERGE, "Merge and centre", cmd_merge);
	tb2->sep ();
	ToolButton *cu = button (tb2, SI_CURRENCY, "Currency format", cmd_currency, 1, true);
	cu->arrow = drop_currency;
	button (tb2, SI_PERCENT, "Percent format", cmd_percent, 2);
	button (tb2, SI_THOUSANDS, "Thousands' separator, two decimals", cmd_thousands);
	button (tb2, SI_DECPLUS, "Add a decimal", cmd_dec_plus);
	button (tb2, SI_DECMINUS, "Take a decimal off", cmd_dec_minus);
	tb2->sep ();
	ToolButton *bo = button (tb2, SI_BORDERS, "Borders", cmd_borders, 1, true);
	bo->arrow = drop_borders;
	button (tb2, SI_CLEAR, "Clear the formatting", cmd_clear_format, 2);

	// the formula bar, the grid, the tabs, the status bar
	int gy = 2 * TB_H + FBAR_H;
	g_grid = new GridView (0, gy, W, H - gy - TABS_H - STATUS_H);
	g_grid->b = &g_b;
	g_fbar = new FormulaBar (0, 2 * TB_H, W, g_grid, cmd_function, cmd_autosum, fbar_cancel, fbar_accept);
	g_fbar->name->onGo = go_to; g_fbar->name->onLeave = name_leave;
	g_fbar->line->onFocusEdit = fbar_focus;
	g_tabs = new SheetTabs (0, H - TABS_H - STATUS_H, W, &g_b);
	g_tabs->onPick = pick_sheet; g_tabs->onAdd = tabs_add; g_tabs->onRename = rename_sheet; g_tabs->onMenu = tab_menu;
	g_status = new StatusBar (0, H - STATUS_H, W);
	g_status->onZoom = zoom_step;
	root.addChild (g_fbar); root.addChild (g_tabs); root.addChild (g_status);
	root.addChild (g_grid);			// (last: on top -- a drag ended over another part still reaches it)
	g_fbar->anchor = ANCHOR_LEFT | ANCHOR_TOP | ANCHOR_RIGHT;
	g_grid->anchor = ANCHOR_FILL;
	g_tabs->anchor = g_status->anchor = ANCHOR_LEFT | ANCHOR_RIGHT | ANCHOR_BOTTOM;
	root.setResizable (true);
	root.fitWorkArea ();			// (between the menu bar and the dock: not under the dock)
	g_grid->onChange = refresh; g_grid->onEdited = grid_edited; g_grid->onContext = grid_context; g_grid->onChartOpen = chart_open;
	g_grid->onZoom = zoom_step; g_grid->onCommit = grid_commit; g_grid->onOtherKey = other_key; g_grid->onFilter = filter_drop;

	static Menu menu;
	menu.menu ("File");
	menu.item ("New", "^N", UK_CTRL ('N'), cmd_new);
	menu.item ("Open...", "^O", UK_CTRL ('O'), cmd_open);
	menu.item ("Save", "^S", UK_CTRL ('S'), cmd_save);
	menu.item ("Save As...", "", 0, cmd_save_as);
	menu.item ("Export as CSV...", "", 0, cmd_export_csv);
	menu.item ("Export as PDF...", "", 0, cmd_export_pdf);
	menu.separator ();
	menu.item ("Print...", "^P", UK_CTRL ('P'), cmd_print);
	menu.menu ("Edit");
	menu.item ("Undo", "^Z", UK_CTRL ('Z'), cmd_undo);
	menu.item ("Redo", "^Y", UK_CTRL ('Y'), cmd_redo);
	menu.separator ();
	menu.item ("Cut", "^X", UK_CTRL ('X'), cmd_cut);
	menu.item ("Copy", "^C", UK_CTRL ('C'), cmd_copy);
	menu.item ("Paste", "^V", UK_CTRL ('V'), cmd_paste);
	menu.item ("Paste Special...", "", 0, cmd_paste_special);
	menu.item ("Delete Contents", "Del", 0, cmd_clear_contents);
	menu.separator ();
	menu.item ("Fill Down", "^D", UK_CTRL ('D'), cmd_fill_down);
	menu.item ("Fill Right", "^R", UK_CTRL ('R'), cmd_fill_right);
	menu.item ("Select All", "^A", UK_CTRL ('A'), cmd_select_all);
	menu.item ("Find and Replace...", "^F", UK_CTRL ('F'), cmd_find);
	menu.item ("Go To...", "^G", UK_CTRL ('G'), cmd_goto);
	menu.menu ("View");
	menu.item ("Zoom In", "", 0, cmd_zoom_in);
	menu.item ("Zoom Out", "", 0, cmd_zoom_out);
	menu.item ("Actual Size (100%)", "", 0, cmd_zoom_100);
	menu.separator ();
	menu.item ("Freeze Panes", "", 0, cmd_freeze);
	menu.item ("Gridlines", "", 0, cmd_gridlines);
	menu.item ("Formulas", "^`", 0, cmd_formulas);
	menu.menu ("Insert");
	menu.item ("Rows Above", "", 0, cmd_ins_rows);
	menu.item ("Columns Before", "", 0, cmd_ins_cols);
	menu.separator ();
	menu.item ("Function...", "", 0, cmd_function);
	menu.item ("Names...", "", 0, cmd_names);
	menu.item ("AutoSum", "", 0, cmd_autosum);
	menu.item ("Chart...", "", 0, cmd_chart);
	menu.item ("Sheet", "", 0, cmd_insert_sheet);
	menu.separator ();
	menu.item ("Current Date", "^;", 0, cmd_date);
	menu.item ("Current Time", "", 0, cmd_time);
	menu.menu ("Format");
	menu.item ("Cells...", "^1", 0, cmd_format_cells);
	menu.item ("Conditional Formatting...", "", 0, cmd_cond_format);
	menu.separator ();
	menu.item ("Bold", "^B", UK_CTRL ('B'), cmd_bold);
	menu.item ("Italic", "^I", 0, cmd_italic);
	menu.item ("Underline", "^U", UK_CTRL ('U'), cmd_under);
	menu.item ("Merge and Centre", "", 0, cmd_merge);
	menu.item ("Wrap Text", "", 0, cmd_wrap);
	menu.separator ();
	menu.item ("Row Height...", "", 0, cmd_row_height);
	menu.item ("Column Width...", "", 0, cmd_col_width);
	menu.item ("Optimal Column Width", "", 0, cmd_opt_width);
	menu.item ("Hide Rows", "", 0, cmd_hide_rows);
	menu.item ("Hide Columns", "", 0, cmd_hide_cols);
	menu.item ("Show Rows and Columns", "", 0, cmd_show_all);
	menu.separator ();
	menu.item ("Clear Formatting", "", 0, cmd_clear_format);
	menu.menu ("Sheet");
	menu.item ("Delete Rows", "^-", 0, cmd_del_rows);
	menu.item ("Delete Columns", "", 0, cmd_del_cols);
	menu.separator ();
	menu.item ("Insert Sheet", "", 0, cmd_insert_sheet);
	menu.item ("Delete Sheet", "", 0, cmd_delete_sheet);
	menu.item ("Rename Sheet...", "", 0, cmd_rename_sheet);
	menu.item ("Duplicate Sheet", "", 0, cmd_duplicate_sheet);
	menu.item ("Move Sheet Left", "", 0, cmd_sheet_left);
	menu.item ("Move Sheet Right", "", 0, cmd_sheet_right);
	menu.menu ("Data");
	menu.item ("Sort Ascending", "", 0, cmd_sort_asc);
	menu.item ("Sort Descending", "", 0, cmd_sort_desc);
	menu.item ("Sort...", "", 0, cmd_sort);
	menu.item ("AutoFilter", "", 0, cmd_autofilter);
	menu.item ("Recalculate", "F9", 0, cmd_recalc);
	menu.publish ();

	// a file named on the command line, else the one kept at the last close
	char args[240];
	int an = kapi_get_args (args, sizeof args);
	if (an > 0 && args[0]) load_path (args);
	else
	{
		int n; char *b = read_file (RECOVER, &n);
		if (b)
		{
			free (b);
			if (note ("Spreadsheet", "The spreadsheet was closed with unsaved changes. Open the recovered workbook?", MB_YESNO) == 1)
			{
				const char *why = 0;
				if (book_load (g_b, RECOVER, &why))
				{
					int nl; char *nm = read_file (RECOVER_NAME, &nl);
					g_path[0] = 0;
					if (nm) { scpy (g_path, nm, sizeof g_path); free (nm); }
					loaded ();
					g_saved = g_gen + 1;				// (still unsaved)
				}
				else { book_fresh (); }
			}
			kapi_remove (RECOVER); kapi_remove (RECOVER_NAME);
		}
	}
	refresh ();
	g_grid->setFocus ();
	root.run ();

	// closed with unsaved changes: the workbook kept for the next start
	if (changed_doc ())
	{
		if (g_grid->ed.on) g_grid->commit (0, 0);
		int n; char *d = xlsx_write (g_b, &n);
		if (d) { write_file (RECOVER, d, n); free (d); }
		if (g_path[0]) write_file (RECOVER_NAME, g_path, (int) strlen (g_path));
	}
	return 0;
}
