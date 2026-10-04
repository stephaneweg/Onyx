//
// letters -- Onyx's word processor, in the way of AbiWord / Word: the document laid out on pages (A4
// by default) and drawn by Letters itself with FreeType's glyphs from the TrueType fonts of the card
// (user/ft/fonts.h), at any zoom; two toolbars (the file, the edits, a table, the zoom -- the style,
// the font, the size, bold / italic / underline / strike-through, superscript / subscript, the text's
// colour and its highlight, the alignments, the lists, the indents), a ruler (the margins, the
// paragraph's indents, a table's columns dragged), a status bar (the file, the page, the words, the
// zoom). Tables (rows, columns, merged cells, lines, shading, a heading row), headers and footers (the
// first page's own), page numbers and fields, tab stops with leaders, a table of contents, the pages
// kept whole (widows, orphans, headings with their text), a mail merge (a Cardfile's records into a
// letter). File > Print: the pages to the print service (print/print.h).
//
// The pieces: doc.h (the document, its formats, tables, fields, stories, undo), layout.h (lines,
// tables, pages), edit.h (the selection, the edits, the tables' changes, the clipboard, find), view.h
// (the pages drawn, the mouse, the keys), ui.h (the toolbar's buttons, the drop-down lists, the
// palettes, the ruler, the status bar), icons.h, fileio.h (RTF, text, HTML), xml.h, docx.h (Word),
// odt.h (OpenDocument), dialogs.h (Font, Paragraph, Tabs, Page Setup, Page Numbers, Insert Table,
// Table Properties, Field, Find & Replace, Special Character, Date and Time, Word Count), merge.h (the
// mail merge: its data, its fields, its documents).
//
// Files: .rtf (the default), .docx (Word), .odt (OpenDocument): read and written with the formats;
// .txt (plain text); anything else read as text; File > Export writes HTML or text. A file dropped on
// the window is opened, dropped text inserted; "letters SD:/docs/a.odt" opens it; "letters --merge
// JOB" makes a mail merge's documents (Cardfile asks it: merge.h). Closed with unsaved changes, the
// document is kept in SD:/apps/letters.app/recovered.rtf and offered back at the next start.
//
#include "wtk/wtk.h"
#include "docguard.h"
#include "ft/fonts.h"
#include "ui.h"
#include "fileio.h"
#include "docx.h"
#include "odt.h"
#include "dialogs.h"
#include "merge.h"

using namespace wtk;
using namespace wr;

#define W 1000
#define H 700
static const char *RECOVER = "SD:/apps/letters.app/recovered.rtf";
static const char *RECOVER_NAME = "SD:/apps/letters.app/recovered.txt";

static PageView *g_view;
static Ruler *g_ruler;
static StatusBar *g_status;
static ToolButton *g_btn[IC_COUNT];
static PickBox *g_styleBox, *g_fontBox, *g_zoomBox;
static SizeBox *g_sizeBox;
static char g_path[200];			// "" : not saved yet
static unsigned g_saved;			// the document's edit count at its last save
static unsigned g_textColor = 0xC00000, g_hiliteColor = 0xFFFF00;	// the split buttons' colours
static bool g_countDirty = true; static unsigned g_countT; static int g_words;

static bool changed_doc () { return g_doc.changes != g_saved; }
static void refresh ();
static void focus_view () { if (g_view) g_view->setFocus (); }
static void after_edit () { if (g_view) { g_view->ensureVisible (); g_view->wake (); } refresh (); focus_view (); }

// ---- the file's name -------------------------------------------------------------------------------------
static const char *base_name (const char *p)
{
	const char *b = p;
	for (const char *q = p; *q; q++) if (*q == '/' || *q == ':') b = q + 1;
	return b;
}
static bool has_ext (const char *p, const char *ext)
{
	int n = slen (p), e = slen (ext);
	if (n < e) return false;
	for (int i = 0; i < e; i++) if (lower ((unsigned char) p[n - e + i]) != lower ((unsigned char) ext[i])) return false;
	return true;
}
static bool rich_ext (const char *p) { return has_ext (p, ".rtf") || has_ext (p, ".docx") || has_ext (p, ".odt"); }

// ---- files -----------------------------------------------------------------------------------------------
static void doc_loaded ()
{
	L.d = &g_doc;
	layout_reset_fonts ();
	doc_dirty_all (g_doc);
	g_caret = g_anchor = mkpos (0, 0); g_atEnd = false; g_typeCf = -1; g_goalX = -1;
	g_bodyCaret = g_bodyAnchor = mkpos (0, 0); L.hfPage = 0;
	g_relayout = true;
	g_saved = g_doc.changes;
	g_countDirty = true;
	merge_doc_loaded ();
	if (g_view) { g_view->sx = g_view->sy = 0; g_view->invalidate (true); }
	refresh ();
}

static bool read_file (const char *path, char **out, int *len)
{
	void *f = kapi_open (path);
	if (!f) return false;
	unsigned sz = kapi_fsize (f);
	if (sz > 32u << 20) sz = 32u << 20;
	char *b = new char[sz + 1];
	int n = kapi_read (f, b, sz);
	kapi_close (f);
	if (n < 0) n = 0;
	b[n] = 0;
	*out = b; *len = n;
	return true;
}

static bool load_path (const char *path)
{
	char *b; int n;
	if (!read_file (path, &b, &n)) { wk_messagebox ("Open", "The file could not be read.", MB_OK); return false; }
	bool ok = doc_from_bytes (g_doc, b, n);
	delete[] b;
	if (!ok) { doc_new (g_doc); wk_messagebox ("Open", "The document could not be read.", MB_OK); }
	if (g_doc.n == 0) doc_new (g_doc);
	scpy (g_path, ok ? path : "", sizeof g_path);
	doc_loaded ();
	return ok;
}

static bool write_path (const char *path, bool asCopy = false)
{
	char *b = 0; unsigned n = 0;
	bool ok = doc_bytes (g_doc, path, &b, &n) && kapi_save_file (path, b, n) >= 0;
	delete[] b;
	if (!ok) { wk_messagebox ("Save", "The file could not be written.", MB_OK); return false; }
	if (!asCopy) { scpy (g_path, path, sizeof g_path); g_saved = g_doc.changes; }
	refresh ();
	return true;
}

static void cmd_save_as ();
static void cmd_save ()
{
	if (!g_path[0] || !(rich_ext (g_path) || has_ext (g_path, ".txt"))) { cmd_save_as (); return; }
	if (has_ext (g_path, ".txt"))					// (formats would be lost: only when plain)
	{
		bool plain = g_doc.ntbl == 0;
		CharFmt n0 = style_fmt (g_doc, ST_NORMAL);
		for (int i = 0; i < g_doc.nfmt && plain; i++) { const CharFmt &f = g_doc.fmt[i]; if (f.flags || f.color != AUTO || f.hilite != AUTO || (f.font != n0.font && f.size != n0.size)) plain = false; }
		if (!plain && wk_messagebox ("Save", "Plain text keeps no formats (bold, fonts, colours, tables...). Save as text anyway?", MB_YESNO) != 1) { cmd_save_as (); return; }
	}
	write_path (g_path);
	focus_view ();
}
static void cmd_save_as ()
{
	char path[200];
	char def[80];
	scpy (def, g_path[0] ? base_name (g_path) : "Untitled.rtf", sizeof def);
	if (g_path[0] && !rich_ext (def) && !has_ext (def, ".txt"))
	{
		int n = slen (def); while (n > 0 && def[n - 1] != '.') n--;
		if (n > 0) def[n - 1] = 0;
		int k = slen (def); scpy (def + k, ".rtf", (int) sizeof def - k);
	}
	if (wk_file_save (path, sizeof path, "SD:/docs", def))
	{
		if (!rich_ext (path) && !has_ext (path, ".txt")) { int k = slen (path); scpy (path + k, ".rtf", (int) sizeof path - k); }
		write_path (path);
	}
	focus_view ();
}
static void save_for_guard () { cmd_save (); }
static void cmd_new ()
{
	if (!doc_confirm (g_path[0] ? base_name (g_path) : "Untitled", changed_doc (), save_for_guard)) { focus_view (); return; }
	doc_new (g_doc);
	g_path[0] = 0;
	doc_loaded ();
	focus_view ();
}
static void cmd_open ()
{
	if (!doc_confirm (g_path[0] ? base_name (g_path) : "Untitled", changed_doc (), save_for_guard)) { focus_view (); return; }
	char path[200];
	if (wk_file_open (path, sizeof path, "SD:/docs")) load_path (path);
	focus_view ();
}
static void cmd_export (const char *ext)
{
	char path[200], def[80];
	scpy (def, g_path[0] ? base_name (g_path) : "Untitled", sizeof def);
	int n = slen (def); int dot = n; while (dot > 0 && def[dot - 1] != '.') dot--;
	if (dot > 0) def[dot - 1] = 0;
	n = slen (def); scpy (def + n, ext, (int) sizeof def - n);
	if (wk_file_save (path, sizeof path, "SD:/docs", def))
	{
		if (!has_ext (path, ext)) { int k = slen (path); scpy (path + k, ext, (int) sizeof path - k); }
		write_path (path, true);
	}
	focus_view ();
}
static void cmd_export_html () { cmd_export (".html"); }
static void cmd_export_txt () { cmd_export (".txt"); }

// ---- File > Export as PDF (pdf/pdfwrite.h: the pages as they are printed, the fonts embedded as subsets) ------------------
struct PdfOpts { int pages; int from, to; bool marks, jpeg, open; char title[120], author[80]; };
class PdfDialog : public Dialog
{
public:
	PdfOpts &o; Textbox *file, *range, *title, *author; RadioButton *rAll, *rCur, *rRange; Checkbox *cMarks, *cJpeg, *cOpen;
	char dir[200];
	PdfDialog (PdfOpts &o_, const char *def, const char *startDir) : Dialog (520, 470, "Export as PDF"), o (o_)
	{
		scpy (dir, startDir, sizeof dir);
		int y = titleH () + 14;
		file = field (110, y, 280, def); button (400, y - 1, 100, "Browse...", 9); y += 40;
		y += 26;
		rAll = new RadioButton (30, y, 220, 24, "All the pages", 1, true, 0, C_FACE); addChild (rAll);
		char b[48]; snprintf (b, sizeof b, "The current page (%d)", o.from + 1);
		rCur = new RadioButton (30, y + 26, 260, 24, b, 1, false, 0, C_FACE); addChild (rCur);
		rRange = new RadioButton (30, y + 52, 100, 24, "Pages", 1, false, 0, C_FACE); addChild (rRange);
		range = field (130, y + 52, 160, ""); y += 96;
		y += 26;
		cMarks = new Checkbox (30, y, 400, 24, "Bookmarks from the headings", true, 0, C_FACE); addChild (cMarks);
		cJpeg = new Checkbox (30, y + 26, 440, 24, "Photos as JPEG (a smaller file)", false, 0, C_FACE); addChild (cJpeg);
		y += 66;
		title = field (110, y, 390, o.title); author = field (110, y + 32, 390, o.author); y += 72;
		cOpen = new Checkbox (16, height - 42, 280, 24, "Open the PDF when it is saved", true, 0, C_FACE); addChild (cOpen);
		button (width - 210, height - 42, 92, "Export", 1); button (width - 108, height - 42, 92, "Cancel", 0);
	}
	void drawBody () override
	{
		int y = titleH () + 14;
		label (16, y + 5, "Save as"); y += 40;
		label (16, y + 4, "PAGES"); y += 26 + 96;
		label (16, y + 4, "CONTENT"); y += 26 + 66;
		label (16, y + 5, "Title"); label (16, y + 37, "Author");
		canvas.text (16, height - 64, "The fonts are embedded: it looks the same everywhere.", wk_mix (C_FACE, C_TEXT, 150));
	}
	void onButton (int tag) override
	{
		if (tag == 9) { char p[200]; if (wk_file_save (p, sizeof p, dir, file->text)) file->setText (p); return; }
		if (tag == 1)
		{
			o.pages = rAll->checked ? 0 : rCur->checked ? 1 : 2;
			if (o.pages == 2)
			{
				int a = 0, b = 0; const char *t = range->text;
				a = atoi (t); const char *d = strchr (t, '-'); b = d ? atoi (d + 1) : a;
				if (a < 1 || b < a) { wk_messagebox ("Export as PDF", "Type the pages as \"2-5\" (or one page: \"3\").", MB_OK); return; }
				o.from = a - 1; o.to = b - 1;
			}
			o.marks = cMarks->checked; o.jpeg = cJpeg->checked; o.open = cOpen->checked;
			scpy (o.title, title->text, sizeof o.title); scpy (o.author, author->text, sizeof o.author);
		}
		close (tag);
	}
	const char *path () const { return file->text; }
};
static int utf8_put (char *o, unsigned c)
{
	if (c < 0x80) { o[0] = (char) c; return 1; }
	if (c < 0x800) { o[0] = (char) (0xC0 | c >> 6); o[1] = (char) (0x80 | (c & 63)); return 2; }
	if (c < 0x10000) { o[0] = (char) (0xE0 | c >> 12); o[1] = (char) (0x80 | (c >> 6 & 63)); o[2] = (char) (0x80 | (c & 63)); return 3; }
	o[0] = (char) (0xF0 | c >> 18); o[1] = (char) (0x80 | (c >> 12 & 63)); o[2] = (char) (0x80 | (c >> 6 & 63)); o[3] = (char) (0x80 | (c & 63)); return 4;
}
// File > Print: the Print dialog (the library's, the same in every app), then the pages drawn into the job
// by the code that draws them into a PDF (print/pdfprint.h) -- the print service does the rest.
static void cmd_print ()
{
	char name[200]; scpy (name, g_path[0] ? base_name (g_path) : "Untitled", sizeof name);
	PrintJob *j = print_ask (name, L.npages, g_view->pageAt (g_view->viewH () / 2) + 1, L.d->page.w / 20.0f, L.d->page.h / 20.0f);
	if (!j) { focus_view (); return; }
	int zoom = L.zoom;					// (the pages at 100 %, as the PDF's)
	set_zoom (100); g_relayout = true; g_view->relayout ();
	g_pdfJpeg = false;
	{ PrintWriter w (j); g_view->exportPages (w, 0, L.npages - 1); }
	int id = print_end (j);
	set_zoom (zoom); g_relayout = true; g_view->relayout (); g_view->invalidate (true);
	if (id < 0) wk_messagebox ("Print", "The document could not be put in the print queue.", MB_OK);
	focus_view ();
}
static void cmd_export_pdf ()
{
	char def[200];
	scpy (def, g_path[0] ? base_name (g_path) : "Untitled", sizeof def);
	{ int n = slen (def), dot = n; while (dot > 0 && def[dot - 1] != '.') dot--; if (dot > 0) def[dot - 1] = 0; n = slen (def); scpy (def + n, ".pdf", (int) sizeof def - n); }
	char dir[200]; scpy (dir, "SD:/docs", sizeof dir);
	if (g_path[0]) { scpy (dir, g_path, sizeof dir); int k = slen (dir); while (k > 0 && dir[k - 1] != '/') k--; if (k > 0) dir[k - 1] = 0; }
	char full[200]; snprintf (full, sizeof full, "%s/%s", dir, def);
	PdfOpts o; memset (&o, 0, sizeof o);
	o.from = g_view->pageAt (g_view->viewH () / 2); o.to = o.from;
	{ int n = slen (def) - 4; scpy (o.title, def, n + 1 < (int) sizeof o.title ? n + 1 : (int) sizeof o.title); }
	PdfDialog d (o, full, dir);
	if (!d.run ()) { focus_view (); return; }
	char path[200]; scpy (path, d.path (), sizeof path);
	if (!has_ext (path, ".pdf")) { int k = slen (path); scpy (path + k, ".pdf", (int) sizeof path - k); }
	// the pages at 100 % (a px: 0.75 point), drawn into the PDF
	int zoom = L.zoom;
	set_zoom (100); g_relayout = true; g_view->relayout ();
	int from = o.pages == 0 ? 0 : o.from, to = o.pages == 0 ? L.npages - 1 : o.to;
	if (from >= L.npages) from = L.npages - 1; if (to >= L.npages) to = L.npages - 1;
	pdfw::Writer w;
	w.info (o.title, o.author, 0, "Letters (Onyx)");
	g_pdfJpeg = o.jpeg;
	g_view->exportPages (w, from, to);
	if (o.marks)
	{	// the headings: their page, their place
		int n; Para **bp = story_p (g_doc, SY_BODY, &n);
		for (int i = 0; i < n; i++)
		{
			Para *q = bp[i];
			int lv = style_outline (q->pf.style);
			if (!lv || !q->nln || q->ln[0].page < from || q->ln[0].page > to) continue;
			char t[200]; int k = 0;
			for (int c = 0; c < q->len && k < 190; c++) { unsigned ch = q->ch[c]; if (ch < ' ' || ch == OBJ_CHAR || ch == FIELD_CHAR) ch = ' '; k += utf8_put (t + k, ch); }
			t[k] = 0;
			if (k) w.outline (lv - 1, t, q->ln[0].page - from, (q->ln[0].py64 >> 6) * PX2PT);
		}
	}
	unsigned len; unsigned char *pdf = w.finish (&len);
	set_zoom (zoom); g_relayout = true; g_view->relayout (); g_view->invalidate (true);
	int r = pdf ? kapi_save_file (path, pdf, len) : -1;
	delete[] pdf;
	if (r != (int) len) wk_messagebox ("Export as PDF", "The PDF could not be written there.", MB_OK);
	else if (o.open) kapi_exec ("SD:apps/pdf.app/main", path);
	focus_view ();
}

// ---- edits -----------------------------------------------------------------------------------------------
static void cmd_undo () { ed_undo (); after_edit (); }
static void cmd_redo () { ed_redo (); after_edit (); }
static void cmd_cut () { ed_cut (); after_edit (); }
static void cmd_copy () { ed_copy (); focus_view (); }
static void cmd_paste () { ed_paste (false); after_edit (); }
static void cmd_paste_plain () { ed_paste (true); after_edit (); }
static void cmd_select_all () { set_caret (mkpos (0, 0), false); set_caret (doc_end (g_doc), true); g_view->invalidate (true); focus_view (); }
static void cmd_find () { dlg_find (g_view); after_edit (); }

// ---- formats ---------------------------------------------------------------------------------------------
static void toggle (unsigned short f) { ed_toggle (f); after_edit (); }
static void cmd_bold () { toggle (CF_BOLD); }
static void cmd_italic () { toggle (CF_ITALIC); }
static void cmd_under () { toggle (CF_UNDER); }
static void cmd_strike () { toggle (CF_STRIKE); }
static void cmd_super () { toggle (CF_SUPER); }
static void cmd_sub () { toggle (CF_SUB); }
static void align (int a) { ed_para (pf_align, a); after_edit (); }
static void cmd_left () { align (AL_LEFT); }
static void cmd_center () { align (AL_CENTER); }
static void cmd_right () { align (AL_RIGHT); }
static void cmd_justify () { align (AL_JUSTIFY); }
static void cmd_bullets () { ed_para (pf_list, LS_BULLET); after_edit (); }
static void cmd_numbers () { ed_para (pf_list, LS_NUMBER); after_edit (); }
static void cmd_indent () { ed_para (pf_indent, 1); after_edit (); }
static void cmd_outdent () { ed_para (pf_indent, -1); after_edit (); }
static void cmd_clear_format () { ed_clear_format (); after_edit (); }
static void apply_color (unsigned c) { CfChange ch; ch.what = CH_COLOR; ch.color = c; ed_format (ch); after_edit (); }
static void apply_hilite (unsigned c) { CfChange ch; ch.what = CH_HILITE; ch.hilite = c; ed_format (ch); after_edit (); }
static void cmd_color () { apply_color (g_textColor); }
static void cmd_hilite () { apply_hilite (g_hiliteColor); }
static void drop_color (ToolButton &b)
{
	int x = 0, y = 0;
	for (Widget *w = &b; w && w->parent; w = w->parent) { x += w->left; y += w->top; }
	ColorPopup p (x, y + b.height + 2, g_textCols, 60, 10, "Automatic");
	long c = p.pick ();
	if (c != -1) { if (c != (long) AUTO) { g_textColor = (unsigned) c; b.setBar (g_textColor); } apply_color ((unsigned) c); }
	focus_view ();
}
static void drop_hilite (ToolButton &b)
{
	int x = 0, y = 0;
	for (Widget *w = &b; w && w->parent; w = w->parent) { x += w->left; y += w->top; }
	ColorPopup p (x, y + b.height + 2, g_hiliteCols, 15, 5, "No Colour");
	long c = p.pick ();
	if (c != -1) { if (c != (long) AUTO) { g_hiliteColor = (unsigned) c; b.setBar (g_hiliteColor); } apply_hilite ((unsigned) c); }
	focus_view ();
}
static void cmd_font_dialog () { if (dlg_font ()) after_edit (); else focus_view (); }
static void cmd_para_dialog () { if (dlg_paragraph ()) after_edit (); else focus_view (); }
static void cmd_tabs () { if (dlg_tabs ()) after_edit (); else focus_view (); }
static void cmd_page_setup () { if (dlg_page_setup ()) { g_relayout = true; g_view->invalidate (true); g_ruler->invalidate (true); after_edit (); } else focus_view (); }
static void cmd_word_count () { dlg_word_count (); focus_view (); }
static void cmd_symbol () { dlg_symbol (); after_edit (); }
static void cmd_datetime () { dlg_datetime (); after_edit (); }
static void cmd_field () { dlg_field (); after_edit (); }
static void cmd_page_break () { ed_page_break (); after_edit (); }
static void cmd_image ()
{
	char path[200];
	if (wk_file_open (path, sizeof path, "SD:/"))
	{
		if (!ed_insert_image (path, g_doc.page.w - g_doc.page.left - g_doc.page.right)) wk_messagebox ("Insert Image", "That file is not an image Letters can read (PNG, JPEG, BMP, GIF, WebP, PCX).", MB_OK);
	}
	after_edit ();
}
static void cmd_page_numbers () { if (dlg_page_numbers ()) { g_doc.changes++; after_edit (); } else focus_view (); }
static void cmd_marks () { g_showMarks = !g_showMarks; g_btn[IC_PILCROW]->setOn (g_showMarks); g_view->invalidate (true); focus_view (); }
static void cmd_units () { g_inches = !g_inches; g_ruler->invalidate (true); focus_view (); }
static void set_style (int st) { ed_para (pf_style, st); after_edit (); }
// The header / footer of the page at the caret edited (Esc, or a double click on the body: back).
static void cmd_header_footer ()
{
	if (g_doc.cur != SY_BODY) { ed_story (SY_BODY); after_edit (); return; }
	const Para *q = g_doc.p[g_caret.p];
	L.hfPage = q->nln ? q->ln[line_of (q, g_caret.o, g_atEnd)].page : 0;
	ed_story (hf_story (false, L.hfPage));
	after_edit ();
}
static void cmd_toc () { ed_toc (false); after_edit (); }
static void cmd_mail_merge () { dlg_mail_merge (g_view); after_edit (); }
static void cmd_toc_update () { ed_toc (true); after_edit (); }

// ---- tables ------------------------------------------------------------------------------------------------
static bool in_tbl () { return g_doc.cur == SY_BODY && in_table (g_doc.p[sel_a ().p]); }
static void table_cmd (void (*fn) ()) { if (in_tbl ()) fn (); after_edit (); }
static void cmd_table () { if (!in_tbl () && dlg_insert_table ()) after_edit (); else focus_view (); }
static void drop_table (ToolButton &b)
{
	int x = 0, y = 0;
	for (Widget *w = &b; w && w->parent; w = w->parent) { x += w->left; y += w->top; }
	TableGrid g (x, y + b.height + 2);
	int r, c;
	if (g.pick (&r, &c) && !in_tbl ()) { ed_insert_table (r, c); after_edit (); }
	else focus_view ();
}
static void rows_above () { ed_table_rows (false); }
static void rows_below () { ed_table_rows (true); }
static void cols_left () { ed_table_cols (false); }
static void cols_right () { ed_table_cols (true); }
static void cmd_rows_above () { table_cmd (rows_above); }
static void cmd_rows_below () { table_cmd (rows_below); }
static void cmd_cols_left () { table_cmd (cols_left); }
static void cmd_cols_right () { table_cmd (cols_right); }
static void cmd_del_rows () { table_cmd (ed_table_del_rows); }
static void cmd_del_cols () { table_cmd (ed_table_del_cols); }
static void cmd_del_table () { table_cmd (ed_table_delete); }
static void cmd_merge () { table_cmd (ed_table_merge); }
static void cmd_split () { table_cmd (ed_table_split); }
static void cmd_even () { table_cmd (ed_table_even); }
static void sel_table () { ed_table_select (SEL_TABLE); }
static void cmd_sel_table () { table_cmd (sel_table); }
static void cmd_table_props () { if (in_tbl () && dlg_table_props ()) after_edit (); else focus_view (); }

// ---- the zoom --------------------------------------------------------------------------------------------
static const int ZOOMS[] = { 25, 50, 75, 100, 125, 150, 200, 300, 400 };
static void zoom_to (int z)
{
	if (z == L.zoom) return;
	set_zoom (z);
	g_relayout = true;
	g_view->relayout ();
	g_view->ensureVisible ();
	g_view->invalidate (true);
	g_ruler->invalidate (true);
	g_status->invalidate (true);
	g_zoomBox->invalidate (true);
	focus_view ();
}
static void zoom_by (int dir)
{
	int n = (int) (sizeof ZOOMS / sizeof ZOOMS[0]), z = L.zoom;
	if (dir > 0) { for (int i = 0; i < n; i++) if (ZOOMS[i] > z) { zoom_to (ZOOMS[i]); return; } }
	else { for (int i = n - 1; i >= 0; i--) if (ZOOMS[i] < z) { zoom_to (ZOOMS[i]); return; } }
}
static void cmd_zoom_in () { zoom_by (1); }
static void cmd_zoom_out () { zoom_by (-1); }
static void cmd_zoom_100 () { zoom_to (100); }
static void cmd_zoom_width () { int avail = g_view->viewW () - 2 * GAP; zoom_to (wclamp ((int) ((long long) avail * 1500 / g_doc.page.w), 10, 500)); }
static void cmd_zoom_page ()
{
	int aw = g_view->viewW () - 2 * GAP, ah = g_view->viewH () - 2 * GAP;
	int zw = (int) ((long long) aw * 1500 / g_doc.page.w), zh = (int) ((long long) ah * 1500 / g_doc.page.h);
	zoom_to (wclamp (wmin (zw, zh), 10, 500));
}

// ---- the toolbar's boxes ---------------------------------------------------------------------------------
static const char *const STYLE_NAMES[ST_COUNT] = { "Normal", "Heading 1", "Heading 2", "Heading 3", "Title", "Subtitle", "Quote", "Plain Text",
						   "Contents 1", "Contents 2", "Contents 3", "Contents Heading", "Header", "Footer" };

// A text in a font (a family of the card), vertically centred, clipped to w.
static void text_in_font (Canvas &cv, int fam, int style, int size64, int x, int y, int w, int h, const char *s, unsigned ink)
{
	fnt::Font *f = fnt::get (fam, style, size64);
	if (!f) { wk_text_l (cv, x, y, h, s, ink); return; }
	int base = y + (h + ((f->ascent - f->descent) >> 6)) / 2;
	Canvas sub; sub.adopt (cv.px, x + w < cv.w ? x + w : cv.w, cv.h, cv.stride);	// (clipped at w)
	fnt::draw_str (sub, f, x << 6, base, s, ink);
}
static void style_value (Canvas &cv, int x, int y, int w, int h, unsigned ink)
{
	int st = g_doc.p[sel_a ().p]->pf.style;
	wk_text_l (cv, x, y, h, STYLE_NAMES[st], ink);
	(void) w;
}
static void style_row (Canvas &cv, int i, int x, int y, int w, int h, unsigned ink)
{
	const Style &s = STYLES[i];
	int fam = resolve_font (s.font);
	int sz = wclamp ((int) s.size * 64 * 2 / 3, 9 * 64, 17 * 64);
	unsigned c = s.color != AUTO && ink == C_FIELD_TEXT ? s.color : ink;
	text_in_font (cv, fam, (s.flags & CF_BOLD ? fnt::BOLD : 0) | (s.flags & CF_ITALIC ? fnt::ITALIC : 0), sz, x, y, w, h, STYLE_NAMES[i], c);
}
static void pick_style (PickBox &b)
{
	int x, y; b.below (&x, &y);
	ListPopup lp (x, y, 220, ST_COUNT, 30, g_doc.p[sel_a ().p]->pf.style, style_row);
	int r = lp.pick ();
	if (r >= 0) set_style (r); else focus_view ();
}
static void font_value (Canvas &cv, int x, int y, int w, int h, unsigned ink)
{
	CharFmt f = caret_fmt ();
	char b[48]; scpy (b, g_doc.fontName[f.font], sizeof b);
	int maxc = w / wk_fw ();
	if (slen (b) > maxc && maxc > 3) { b[maxc - 2] = '.'; b[maxc - 1] = '.'; b[maxc] = 0; }
	wk_text_l (cv, x, y, h, b, ink);
}
static void font_row (Canvas &cv, int i, int x, int y, int w, int h, unsigned ink)
{
	text_in_font (cv, i, 0, 15 * 64, x, y, w, h, fnt::name (i), ink);
}
static void pick_font (PickBox &b)
{
	int x, y; b.below (&x, &y);
	CharFmt f = caret_fmt ();
	ListPopup lp (x, y, 250, fnt::count (), 28, font_family (f.font), font_row);
	int r = lp.pick ();
	if (r >= 0)
	{
		CfChange c; c.what = CH_FONT; c.font = (short) doc_font (g_doc, fnt::name (r));
		ed_format (c); after_edit ();
	}
	else focus_view ();
}
static void pick_size (int hp) { CfChange c; c.what = CH_SIZE; c.size = (short) hp; ed_format (c); after_edit (); }
static void zoom_value (Canvas &cv, int x, int y, int w, int h, unsigned ink)
{
	char z[8]; int n = 0, v = L.zoom; char t[6]; int j = 0;
	while (v) { t[j++] = (char) ('0' + v % 10); v /= 10; }
	while (j) z[n++] = t[--j];
	z[n++] = '%'; z[n] = 0;
	wk_text_l (cv, x, y, h, z, ink);
	(void) w;
}
static const char *const ZOOM_ROWS[] = { "Page Width", "Whole Page", "25%", "50%", "75%", "100%", "125%", "150%", "200%", "300%", "400%" };
static void zoom_row (Canvas &cv, int i, int x, int y, int, int h, unsigned ink) { wk_text_l (cv, x, y, h, ZOOM_ROWS[i], ink); }
static void pick_zoom (PickBox &b)
{
	int x, y; b.below (&x, &y);
	int sel = -1;
	for (int i = 0; i < 9; i++) if (ZOOMS[i] == L.zoom) sel = i + 2;
	ListPopup lp (x, y, 130, 11, 24, sel, zoom_row, 11);
	int r = lp.pick ();
	if (r == 0) cmd_zoom_width (); else if (r == 1) cmd_zoom_page (); else if (r >= 2) zoom_to (ZOOMS[r - 2]); else focus_view ();
}

// ---- the context menu ------------------------------------------------------------------------------------
static void context_menu (int x, int y)
{
	PopupMenu m (x + g_view->left, y + g_view->top);
	int a, b, head;
	bool tbl = in_tbl (), toc = g_doc.cur == SY_BODY && toc_find (&a, &b, &head) && sel_a ().p >= (head >= 0 ? head : a) && sel_a ().p < b;
	m.add ("Cut", 1, has_sel (), "^X");
	m.add ("Copy", 2, has_sel (), "^C");
	m.add ("Paste", 3, true, "^V");
	m.separator ();
	m.add ("Font...", 5, true, "^D");
	m.add ("Paragraph...", 6, true);
	if (tbl)
	{
		m.separator ();
		m.add ("Insert Row Below", 20, true);
		m.add ("Insert Column Right", 21, true);
		m.add ("Delete Rows", 22, true);
		m.add ("Delete Columns", 23, true);
		m.add ("Merge Cells", 24, has_sel ());
		m.add ("Split Cell", 25, true);
		m.add ("Table Properties...", 26, true);
	}
	else if (toc) { m.separator (); m.add ("Update Table of Contents", 30, true); }
	else if (g_doc.cur != SY_BODY) { m.separator (); m.add ("Close Header and Footer", 31, true, "Esc"); }
	else { m.add ("Bullets", 7, true); m.add ("Numbering", 8, true); }
	m.separator ();
	m.add ("Select All", 9, true, "^A");
	switch (m.run ())
	{
	case 1: cmd_cut (); break;
	case 2: cmd_copy (); break;
	case 3: cmd_paste (); break;
	case 5: cmd_font_dialog (); break;
	case 6: cmd_para_dialog (); break;
	case 7: cmd_bullets (); break;
	case 8: cmd_numbers (); break;
	case 9: cmd_select_all (); break;
	case 20: cmd_rows_below (); break;
	case 21: cmd_cols_right (); break;
	case 22: cmd_del_rows (); break;
	case 23: cmd_del_cols (); break;
	case 24: cmd_merge (); break;
	case 25: cmd_split (); break;
	case 26: cmd_table_props (); break;
	case 30: cmd_toc_update (); break;
	case 31: cmd_header_footer (); break;
	default: focus_view ();
	}
}

// ---- the controls' states --------------------------------------------------------------------------------
static void refresh ()
{
	if (!g_view) return;
	g_view->relayout ();
	CharFmt f = caret_fmt ();
	const Para *q = g_doc.p[sel_a ().p];
	g_btn[IC_BOLD]->setOn (f.flags & CF_BOLD);
	g_btn[IC_ITALIC]->setOn (f.flags & CF_ITALIC);
	g_btn[IC_UNDER]->setOn (f.flags & CF_UNDER);
	g_btn[IC_STRIKE]->setOn (f.flags & CF_STRIKE);
	g_btn[IC_SUPER]->setOn (f.flags & CF_SUPER);
	g_btn[IC_SUB]->setOn (f.flags & CF_SUB);
	g_btn[IC_LEFT]->setOn (q->pf.align == AL_LEFT);
	g_btn[IC_CENTER]->setOn (q->pf.align == AL_CENTER);
	g_btn[IC_RIGHT]->setOn (q->pf.align == AL_RIGHT);
	g_btn[IC_JUSTIFY]->setOn (q->pf.align == AL_JUSTIFY);
	g_btn[IC_BULLETS]->setOn (q->pf.list == LS_BULLET);
	g_btn[IC_NUMBERS]->setOn (q->pf.list == LS_NUMBER);
	g_btn[IC_UNDO]->setDisabled (g_doc.uptr == 0);
	g_btn[IC_REDO]->setDisabled (g_doc.uptr >= g_doc.nundo);
	g_btn[IC_CUT]->setDisabled (!has_sel ());
	g_btn[IC_COPY]->setDisabled (!has_sel ());
	g_btn[IC_TABLE]->setDisabled (in_tbl () || g_doc.cur != SY_BODY);
	g_sizeBox->setValue (f.size);
	g_styleBox->invalidate (true); g_fontBox->invalidate (true); g_zoomBox->invalidate (true);
	g_ruler->invalidate (true);
	// the status bar: the file, the page, the words
	char l[160], m[80];
	int n = 0;
	const char *name = g_path[0] ? base_name (g_path) : "Untitled";
	for (const char *s = name; *s && n < 140; s++) l[n++] = *s;
	if (changed_doc ()) { const char *s = "  (modified)"; while (*s) l[n++] = *s++; }
	l[n] = 0;
	int page = L.hfPage + 1;
	if (g_doc.cur == SY_BODY) { const Para *cq = g_doc.p[g_caret.p]; page = cq->nln ? cq->ln[line_of (cq, g_caret.o, g_atEnd)].page + 1 : 1; }
	auto num = [] (char *b, int &k, int v) { char t[12]; int j = 0; do { t[j++] = (char) ('0' + v % 10); v /= 10; } while (v); while (j) b[k++] = t[--j]; };
	int k = 0;
	if (g_doc.cur != SY_BODY) { for (const char *s = STORY_NAMES[g_doc.cur]; *s && k < 30; s++) m[k++] = *s; for (const char *s = " - "; *s; s++) m[k++] = *s; }
	for (const char *s = "Page "; *s; s++) m[k++] = *s;
	num (m, k, page + g_doc.page.start - 1);
	for (const char *s = " of "; *s; s++) m[k++] = *s;
	num (m, k, L.npages);
	for (const char *s = "     Words: "; *s; s++) m[k++] = *s;
	num (m, k, g_words);
	m[k] = 0;
	g_status->set (l, m);
	g_countDirty = true;
}

// ---- the window ------------------------------------------------------------------------------------------
class WriterRoot : public Root
{
public:
	WriterRoot () : Root (W, H, "Letters") {}
	void onTick () override
	{
		if (g_view) g_view->tick ();
		unsigned t = kapi_get_ticks ();
		if (g_countDirty && t - g_countT > 40)			// (the words counted when typing pauses: the body's)
		{
			g_countDirty = false; g_countT = t;
			int n; Para **p = story_p (g_doc, SY_BODY, &n);
			Counts c = para_count (p, mkpos (0, 0), mkpos (n - 1, p[n - 1]->len));
			if (c.words != g_words) { g_words = c.words; refresh (); g_countDirty = false; }
		}
	}
	void onDrop (int, int, int type, const char *data, int len, unsigned) override
	{
		if (type == DND_TEXT)
		{
			unsigned *u = new unsigned[len + 1];
			int n = decode_text (data, len, u, len + 1);
			ed_type (u, n);
			delete[] u;
			after_edit ();
			return;
		}
		char path[200];
		if (type != DND_FILES || !doc_first_path (data, path, sizeof path)) return;
		void *d = kapi_opendir (path);
		if (d) { kapi_closedir (d); return; }
		if (!doc_confirm (g_path[0] ? base_name (g_path) : "Untitled", changed_doc (), save_for_guard)) return;
		load_path (path);
		focus_view ();
	}
};

static ToolButton *button (ToolBar *tb, int ic, const char *tip, void (*cb) (), int gap = 1, bool split = false)
{
	ToolButton *b = new ToolButton (ic, tip, cb, split);
	tb->add (b, gap);
	g_btn[ic] = b;
	return b;
}

static void now_hook (int *y, int *mo, int *d, int *h, int *mi, int *s) { now_datetime (y, mo, d, h, mi, s); }

int main (void)
{
	char args[200];
	int an = kapi_get_args (args, sizeof args);
	if (an < 0) an = 0;
	args[an < (int) sizeof args ? an : (int) sizeof args - 1] = 0;
	const char *job = 0;					// "--merge JOB": a mail merge's documents
	if (an > 8 && args[0] == '-' && args[1] == '-' && args[2] == 'm') { job = args + 7; while (*job == ' ') job++; }
	g_now = now_hook;
	WriterRoot root;
	root.attach ();				// (a question asked before run (): its clicks and keys)
	wtk::init ();
	if (!fnt::init ())
	{
		wk_messagebox ("Letters", "No TrueType fonts in SD:/res/fonts: Letters cannot draw its pages.", MB_OK);
		return 1;
	}
	make_palettes ();
	doc_init (g_doc); doc_new (g_doc);
	L.d = &g_doc; L.zoom = 100;

	// the toolbars
	ToolBar *tb1 = new ToolBar (0, 0, W), *tb2 = new ToolBar (0, TB_H, W);
	root.addChild (tb1); root.addChild (tb2);
	tb1->anchor = tb2->anchor = ANCHOR_LEFT | ANCHOR_TOP | ANCHOR_RIGHT;
	button (tb1, IC_NEW, "New document (Ctrl+N)", cmd_new);
	button (tb1, IC_OPEN, "Open... (Ctrl+O)", cmd_open);
	button (tb1, IC_SAVE, "Save (Ctrl+S)", cmd_save);
	tb1->sep ();
	button (tb1, IC_UNDO, "Undo (Ctrl+Z)", cmd_undo);
	button (tb1, IC_REDO, "Redo (Ctrl+Y)", cmd_redo);
	tb1->sep ();
	button (tb1, IC_CUT, "Cut (Ctrl+X)", cmd_cut);
	button (tb1, IC_COPY, "Copy (Ctrl+C)", cmd_copy);
	button (tb1, IC_PASTE, "Paste (Ctrl+V)", cmd_paste);
	tb1->sep ();
	button (tb1, IC_FIND, "Find and Replace (Ctrl+F)", cmd_find);
	button (tb1, IC_PILCROW, "Formatting marks", cmd_marks);
	tb1->sep ();
	button (tb1, IC_PAGEBREAK, "Page break", cmd_page_break);
	ToolButton *tt = button (tb1, IC_TABLE, "Insert a table (its arrow: a grid)", cmd_table, 1, true);
	tt->arrow = drop_table;
	button (tb1, IC_SYMBOL, "Special character...", cmd_symbol);
	button (tb1, IC_IMAGE, "Insert an image...", cmd_image);
	tb1->sep ();
	button (tb1, IC_ZOOMOUT, "Zoom out", cmd_zoom_out);
	g_zoomBox = new PickBox (84, "Zoom", zoom_value, pick_zoom);
	tb1->add (g_zoomBox, 2);
	button (tb1, IC_ZOOMIN, "Zoom in", cmd_zoom_in, 2);

	g_styleBox = new PickBox (136, "Paragraph style", style_value, pick_style);
	tb2->add (g_styleBox, 0);
	g_fontBox = new PickBox (176, "Font", font_value, pick_font);
	tb2->add (g_fontBox, 6);
	g_sizeBox = new SizeBox (58, pick_size);
	tb2->add (g_sizeBox, 6);
	tb2->sep ();
	button (tb2, IC_BOLD, "Bold (Ctrl+B)", cmd_bold);
	button (tb2, IC_ITALIC, "Italic (Ctrl+I)", cmd_italic);
	button (tb2, IC_UNDER, "Underline (Ctrl+U)", cmd_under);
	button (tb2, IC_STRIKE, "Strikethrough", cmd_strike);
	button (tb2, IC_SUPER, "Superscript", cmd_super, 4);
	button (tb2, IC_SUB, "Subscript", cmd_sub);
	tb2->sep ();
	ToolButton *tc = button (tb2, IC_COLOR, "Text colour", cmd_color, 1, true);
	tc->arrow = drop_color; tc->setBar (g_textColor);
	ToolButton *th = button (tb2, IC_HILITE, "Highlight", cmd_hilite, 2, true);
	th->arrow = drop_hilite; th->setBar (g_hiliteColor);
	tb2->sep ();
	button (tb2, IC_LEFT, "Align left (Ctrl+L)", cmd_left);
	button (tb2, IC_CENTER, "Centre (Ctrl+E)", cmd_center);
	button (tb2, IC_RIGHT, "Align right (Ctrl+R)", cmd_right);
	button (tb2, IC_JUSTIFY, "Justify (Ctrl+J)", cmd_justify);
	tb2->sep ();
	button (tb2, IC_BULLETS, "Bullets", cmd_bullets);
	button (tb2, IC_NUMBERS, "Numbering", cmd_numbers);
	button (tb2, IC_OUTDENT, "Decrease indent", cmd_outdent, 4);
	button (tb2, IC_INDENT, "Increase indent", cmd_indent);

	// the page, its ruler, the status bar
	int vy = 2 * TB_H + RULER_H;
	g_view = new PageView (0, vy, W, H - vy - STATUS_H);
	g_ruler = new Ruler (0, 2 * TB_H, W, g_view);
	g_status = new StatusBar (0, H - STATUS_H, W);
	g_status->zoomBy = zoom_by;
	root.addChild (g_ruler); root.addChild (g_status);
	root.addChild (g_view);			// (last: on top -- a drag ended over another part still reaches it)
	g_ruler->anchor = ANCHOR_LEFT | ANCHOR_TOP | ANCHOR_RIGHT;
	g_view->anchor = ANCHOR_FILL;
	g_status->anchor = ANCHOR_LEFT | ANCHOR_RIGHT | ANCHOR_BOTTOM;
	root.setResizable (true);
	g_onChange = refresh;
	g_onContext = context_menu;

	static Menu menu;
	menu.menu ("File");
	menu.item ("New", "^N", WK_CTRL ('N'), cmd_new);
	menu.item ("Open...", "^O", WK_CTRL ('O'), cmd_open);
	menu.separator ();
	menu.item ("Save", "^S", WK_CTRL ('S'), cmd_save);
	menu.item ("Save As...", "", 0, cmd_save_as);
	menu.item ("Export as HTML...", "", 0, cmd_export_html);
	menu.item ("Export as Text...", "", 0, cmd_export_txt);
	menu.item ("Export as PDF...", "", 0, cmd_export_pdf);
	menu.separator ();
	menu.item ("Page Setup...", "", 0, cmd_page_setup);
	menu.item ("Print...", "^P", WK_CTRL ('P'), cmd_print);
	menu.menu ("Edit");
	menu.item ("Undo", "^Z", WK_CTRL ('Z'), cmd_undo);
	menu.item ("Redo", "^Y", WK_CTRL ('Y'), cmd_redo);
	menu.separator ();
	menu.item ("Cut", "^X", WK_CTRL ('X'), cmd_cut);
	menu.item ("Copy", "^C", WK_CTRL ('C'), cmd_copy);
	menu.item ("Paste", "^V", WK_CTRL ('V'), cmd_paste);
	menu.item ("Paste Unformatted", "", 0, cmd_paste_plain);
	menu.separator ();
	menu.item ("Select All", "^A", WK_CTRL ('A'), cmd_select_all);
	menu.item ("Find and Replace...", "^F", WK_CTRL ('F'), cmd_find);
	menu.menu ("View");
	menu.item ("Actual Size (100%)", "", 0, cmd_zoom_100);
	menu.item ("Page Width", "", 0, cmd_zoom_width);
	menu.item ("Whole Page", "", 0, cmd_zoom_page);
	menu.separator ();
	menu.item ("Header and Footer", "", 0, cmd_header_footer);
	menu.item ("Formatting Marks", "", 0, cmd_marks);
	menu.item ("Ruler in Inches / Centimetres", "", 0, cmd_units);
	menu.menu ("Insert");
	menu.item ("Page Break", "", 0, cmd_page_break);
	menu.item ("Table...", "", 0, cmd_table);
	menu.item ("Image...", "", 0, cmd_image);
	menu.item ("Special Character...", "", 0, cmd_symbol);
	menu.separator ();
	menu.item ("Page Numbers...", "", 0, cmd_page_numbers);
	menu.item ("Date and Time...", "", 0, cmd_datetime);
	menu.item ("Field...", "", 0, cmd_field);
	menu.item ("Table of Contents", "", 0, cmd_toc);
	menu.menu ("Format");
	menu.item ("Font...", "^D", WK_CTRL ('D'), cmd_font_dialog);
	menu.item ("Paragraph...", "", 0, cmd_para_dialog);
	menu.item ("Tabs...", "", 0, cmd_tabs);
	menu.separator ();
	menu.item ("Bold", "^B", WK_CTRL ('B'), cmd_bold);
	menu.item ("Italic", "^I", 0, cmd_italic);
	menu.item ("Underline", "^U", WK_CTRL ('U'), cmd_under);
	menu.item ("Strikethrough", "", 0, cmd_strike);
	menu.item ("Superscript", "", 0, cmd_super);
	menu.item ("Subscript", "", 0, cmd_sub);
	menu.item ("Clear Formatting", "", 0, cmd_clear_format);
	menu.separator ();
	menu.item ("Align Left", "^L", WK_CTRL ('L'), cmd_left);
	menu.item ("Centre", "^E", WK_CTRL ('E'), cmd_center);
	menu.item ("Align Right", "^R", WK_CTRL ('R'), cmd_right);
	menu.item ("Justify", "^J", WK_CTRL ('J'), cmd_justify);
	menu.separator ();
	menu.item ("Bullets", "", 0, cmd_bullets);
	menu.item ("Numbering", "", 0, cmd_numbers);
	menu.menu ("Table");
	menu.item ("Insert Rows Above", "", 0, cmd_rows_above);
	menu.item ("Insert Rows Below", "", 0, cmd_rows_below);
	menu.item ("Insert Columns Left", "", 0, cmd_cols_left);
	menu.item ("Insert Columns Right", "", 0, cmd_cols_right);
	menu.separator ();
	menu.item ("Delete Rows", "", 0, cmd_del_rows);
	menu.item ("Delete Columns", "", 0, cmd_del_cols);
	menu.item ("Delete Table", "", 0, cmd_del_table);
	menu.separator ();
	menu.item ("Merge Cells", "", 0, cmd_merge);
	menu.item ("Split Cell", "", 0, cmd_split);
	menu.item ("Distribute Columns Evenly", "", 0, cmd_even);
	menu.item ("Select Table", "", 0, cmd_sel_table);
	menu.item ("Table Properties...", "", 0, cmd_table_props);
	menu.menu ("Tools");
	menu.item ("Word Count...", "", 0, cmd_word_count);
	menu.item ("Update Table of Contents", "", 0, cmd_toc_update);
	menu.item ("Mail Merge...", "", 0, cmd_mail_merge);
	menu.publish ();

	// A mail merge's documents, a file named on the command line, else the one kept at the last close.
	if (job)
	{
		int r = merge_job (job, g_path, sizeof g_path);
		if (r == 2) load_path (g_path);
		else { if (r != 1) doc_new (g_doc); g_path[0] = 0; doc_loaded (); if (r == 1) g_saved = g_doc.changes + 1; }
	}
	else if (an > 0 && args[0]) load_path (args);
	else
	{
		bool recovered = false;
		char *b; int n;
		if (read_file (RECOVER, &b, &n))
		{
			if (n > 0 && rtf_is (b, n) && wk_messagebox ("Letters", "Letters was closed with unsaved changes. Open the recovered document?", MB_YESNO) == 1)
			{
				rtf_load (g_doc, b, n);
				if (g_doc.n == 0) doc_new (g_doc);
				char *nb; int nn;
				g_path[0] = 0;
				if (read_file (RECOVER_NAME, &nb, &nn)) { scpy (g_path, nb, sizeof g_path); delete[] nb; }
				doc_loaded ();
				g_saved = g_doc.changes + 1;			// (still unsaved)
				recovered = true;
			}
			delete[] b;
			kapi_remove (RECOVER); kapi_remove (RECOVER_NAME);
		}
		if (!recovered) doc_loaded ();
	}
	refresh ();
	g_view->setFocus ();
	root.run ();

	// Closed with unsaved changes: the document kept for the next start.
	if (g_doc.cur != SY_BODY) doc_story (g_doc, SY_BODY);
	if (changed_doc () && !(g_doc.n == 1 && g_doc.p[0]->len == 0))
	{
		Out o;
		rtf_save (g_doc, o);
		kapi_save_file (RECOVER, o.b, (unsigned) o.n);
		o.free ();
		if (g_path[0]) kapi_save_file (RECOVER_NAME, g_path, (unsigned) slen (g_path));
	}
	return 0;
}
