//
// slides -- Onyx's presentation program, in the way of PowerPoint / Impress (the office suite's third, after
// Letters and Sheet: their toolbars, their icons). The window: two toolbars (the file, the slides -- new with
// a layout, the layout, duplicate, delete --, the objects to insert -- a text box, a picture, a table, a chart,
// a shape from the gallery, a line, an arrow --, the zoom, Start show; then the text: the font, the size, bold
// / italic / underline / strike / superscript / subscript, the colour, the alignment, the anchor, the lists,
// the levels, the order), the slides at the left (sections, thumbnails), the slide in the middle (its layers
// composited by the GPU: render.h), the speaker's notes under it, the sidebar at the right (Slide, Shape,
// Text, Animate), the status bar; the slide sorter; the slide show and the presenter's console (show.h).
//
// The pieces: model.h (the deck), text.h (the text's layout and drawing), render.h (the layers, the GPU),
// editor.h (the state, the commands, undo), view.h (the slide edited), panes.h (thumbnails, sorter, notes,
// status bar), sidebar.h (the four tabs), show.h (the show), odp.h (OpenDocument: read and written).
//
// Files: .odp (OpenDocument presentation: Slides' own; Impress reads it); File > Export as PDF (a page a
// slide) and Export Slide as PNG. "slides SD:/docs/a.odp" opens it. Closed with unsaved changes, the deck is
// kept in SD:/apps/slides.app/recovered.odp and offered back at the next start.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#include "wtk/wtk.h"
#include "docguard.h"
#include "sidebar.h"
#include "show.h"
#include "odp.h"
#include "pdf/pdfwrite.h"

using namespace wtk;
using namespace sl;

#define W 1000
#define H 700
static const char *RECOVER = "SD:/apps/slides.app/recovered.odp";
static const char *RECOVER_NAME = "SD:/apps/slides.app/recovered.txt";
enum { TH_W = 172, SB_W = 236, NOTES_H = 84, ST_H = 24 };

static Root *g_root;
static SlideView *g_view;
static SlideList *g_list;
static Sorter *g_sorter;
static NotesPane *g_notes;
static StatusBar *g_status;
static ToolBar *g_tb1, *g_tb2;
static PickBox *g_fontBox, *g_sizeBox, *g_zoomBox;
static ToolButton *g_btn[400];
static unsigned g_textColor = 0xC00000;

static void refresh ();
static void focus_view () { if (g_viewMode == VIEW_SORTER) g_sorter->setFocus (); else g_view->setFocus (); }
static void after () { g_view->wake (); refresh (); }

// ---- files ---------------------------------------------------------------------------------------------------------
static const char *base_name (const char *p) { const char *b = p; for (const char *q = p; *q; q++) if (*q == '/' || *q == ':') b = q + 1; return b; }
static bool has_ext (const char *p, const char *e)
{
	int n = (int) strlen (p), k = (int) strlen (e);
	if (n < k) return false;
	for (int i = 0; i < k; i++) { int a = p[n - k + i], b = e[i]; if (a >= 'A' && a <= 'Z') a += 32; if (a != b) return false; }
	return true;
}
static unsigned char *read_all (const char *path, unsigned *len)
{
	void *f = kapi_open (path);
	if (!f) return 0;
	unsigned sz = kapi_fsize (f);
	if (sz > (64u << 20)) { kapi_close (f); return 0; }
	unsigned char *b = (unsigned char *) malloc (sz + 1);
	int n = kapi_read (f, b, sz);
	kapi_close (f);
	if (n < 0) { free (b); return 0; }
	*len = (unsigned) n;
	return b;
}
static void deck_loaded ()
{
	g_cur = 0; g_sel.clear (); end_edit (); undo_clear ();
	g_saved = g_deck.changes;
	g_thumbs.clear (); g_notes->reset ();
	g_view->C.drop_all ();
	g_sorterSel.clear ();
	g_famCache[0] = 0; g_nfamCache = 0;
	refresh ();
	g_list->m_top = 0;
	g_view->wake ();
}
static bool load_path (const char *path)
{
	unsigned n = 0;
	unsigned char *b = read_all (path, &n);
	if (!b) { wk_messagebox ("Open", "The file could not be read.", MB_OK); return false; }
	bool ok = odp_load (g_deck, b, n);
	free (b);
	if (!ok) { deck_new (g_deck); wk_messagebox ("Open", "That is not a presentation Slides can read (.odp).", MB_OK); }
	scpy (g_path, ok ? path : "", sizeof g_path);
	deck_loaded ();
	return ok;
}
static bool write_path (const char *path, bool asCopy = false)
{
	unsigned n = 0;
	unsigned char *b = odp_save (g_deck, &n);
	bool ok = b && kapi_save_file (path, b, n) >= 0;
	delete[] b;
	if (!ok) { wk_messagebox ("Save", "The file could not be written.", MB_OK); return false; }
	if (!asCopy) { scpy (g_path, path, sizeof g_path); g_saved = g_deck.changes; }
	refresh ();
	return true;
}
static void cmd_save_as ()
{
	char path[200], def[80];
	scpy (def, g_path[0] ? base_name (g_path) : "Untitled.odp", sizeof def);
	if (wk_file_save (path, sizeof path, "SD:/docs", def))
	{
		if (!has_ext (path, ".odp")) { int k = (int) strlen (path); scpy (path + k, ".odp", (int) sizeof path - k); }
		write_path (path);
	}
	focus_view ();
}
static void cmd_save () { if (!g_path[0] || !has_ext (g_path, ".odp")) { cmd_save_as (); return; } write_path (g_path); focus_view (); }
static void save_for_guard () { cmd_save (); }
static void cmd_new ()
{
	if (!doc_confirm (g_path[0] ? base_name (g_path) : "Untitled", changed_doc (), save_for_guard)) { focus_view (); return; }
	deck_new (g_deck, theme_find (g_deck.theme.name) >= 0 ? theme_find (g_deck.theme.name) : 0);
	g_path[0] = 0;
	deck_loaded ();
	focus_view ();
}
static void cmd_open ()
{
	if (!doc_confirm (g_path[0] ? base_name (g_path) : "Untitled", changed_doc (), save_for_guard)) { focus_view (); return; }
	char path[200];
	if (wk_file_open (path, sizeof path, "SD:/docs")) load_path (path);
	focus_view ();
}
// File > Export as PDF: a page a slide (the slide flattened at 1600 px wide, as a JPEG at quality 92), the
// title from the file's name.
static void cmd_export_pdf ()
{
	char def[120], path[200];
	scpy (def, g_path[0] ? base_name (g_path) : "Untitled", sizeof def);
	{ int n = (int) strlen (def), dot = n; while (dot > 0 && def[dot - 1] != '.') dot--; if (dot > 0) def[dot - 1] = 0; n = (int) strlen (def); scpy (def + n, ".pdf", (int) sizeof def - n); }
	if (!wk_file_save (path, sizeof path, "SD:/docs", def)) { focus_view (); return; }
	if (!has_ext (path, ".pdf")) { int k = (int) strlen (path); scpy (path + k, ".pdf", (int) sizeof path - k); }
	pdfw::Writer w;
	char title[120]; scpy (title, def, sizeof title); title[strlen (title) - 4] = 0;
	w.info (title, "", 0, "Slides (Onyx)");
	int pw = 1600, ph = pw * g_deck.sh / g_deck.sw;
	unsigned *px = (unsigned *) malloc ((size_t) pw * ph * 4);
	Compositor C; C.init (true);
	float ptW = g_deck.sw / 35.277778f, ptH = g_deck.sh / 35.277778f;
	for (int i = 0; i < g_deck.slides.n; i++)
	{
		if (g_deck.slides[i]->hidden) continue;
		C.frame++;
		flatten_slide (C, g_deck, *g_deck.slides[i], i, px, pw, ph, pw);
		C.sweep (0);
		for (int k = 0; k < pw * ph; k++) px[k] |= 0xFF000000u;
		w.begin_page (ptW, ptH);
		w.image (px, pw, ph, 0, 0, ptW, ptH, true, 92);
		w.end_page ();
	}
	C.drop_all ();
	free (px);
	unsigned len = 0; unsigned char *pdf = w.finish (&len);
	int r = pdf ? kapi_save_file (path, pdf, len) : -1;
	delete[] pdf;
	if (r != (int) len) wk_messagebox ("Export as PDF", "The PDF could not be written there.", MB_OK);
	else kapi_exec ("SD:apps/pdf.app/main", path);
	focus_view ();
}
static void cmd_export_png ()
{
	char def[120], path[200];
	snprintf (def, sizeof def, "Slide %d.png", g_cur + 1);
	if (!wk_file_save (path, sizeof path, "SD:/docs", def)) { focus_view (); return; }
	if (!has_ext (path, ".png")) { int k = (int) strlen (path); scpy (path + k, ".png", (int) sizeof path - k); }
	int pw = 1920, ph = pw * g_deck.sh / g_deck.sw;
	unsigned *px = (unsigned *) malloc ((size_t) pw * ph * 4);
	Compositor C; C.init (true); C.frame++;
	flatten_slide (C, g_deck, *cur_slide (), g_cur, px, pw, ph, pw);
	C.drop_all ();
	for (int k = 0; k < pw * ph; k++) px[k] |= 0xFF000000u;
	unsigned n = 0; unsigned char *png = pngsave::png_encode (px, pw, ph, false, &n);
	free (px);
	if (!png || kapi_save_file (path, png, n) < 0) wk_messagebox ("Export", "The picture could not be written there.", MB_OK);
	delete[] png;
	focus_view ();
}

// ---- a few dialogs ---------------------------------------------------------------------------------------------------
class Dialog : public Modal
{
public:
	Dialog (int w, int h, const char *title) : Modal (w, h), m_title (title)
	{
		Root *r = Root::current ();
		int RW = r ? r->width : w, RH = r ? r->height : h;
		left = imax (0, (RW - w) / 2); top = imax (0, (RH - h) / 2);
	}
	void onDraw () override { drawBox (m_title); drawBody (); }
	virtual void drawBody () {}
	bool onKey (long k) override { if (k == 27) { close (0); return true; } if (k == KEY_ENTER) { onButton (1); return true; } return false; }
	void onButton (int tag) override { close (tag); }
	Button *button (int x, int y, int w, const char *label, int tag) { Button *b = new Button (x, y, w, 28, label, act); b->tag = tag; addChild (b); return b; }
	void okCancel () { button (width - 184, height - 42, 82, "OK", 1); button (width - 94, height - 42, 82, "Cancel", 0); }
	void label (int x, int y, const char *s) { canvas.text (x, y, s, C_TEXT); }
	Textbox *field (int x, int y, int w, const char *s) { Textbox *t = new Textbox (x, y, w, 26, s); addChild (t); return t; }
	static void act (Widget &w) { Widget *p = w.parent; while (p && !p->modal) p = p->parent; if (p) ((Modal *) p)->onButton (w.tag); }
private:
	const char *m_title;
};
// One line asked for (a section's name, the footer...)
class AskDialog : public Dialog
{
public:
	Textbox *t; const char *m_q;
	AskDialog (const char *title, const char *q, const char *def) : Dialog (420, 150, title), m_q (q)
	{ t = field (16, titleH () + 36, 388, def); okCancel (); }
	void drawBody () override { label (16, titleH () + 14, m_q); }
};
static bool ask_line (const char *title, const char *q, char *out, int cap)
{
	AskDialog d (title, q, out);
	d.t->setFocus ();
	if (d.run () != 1) return false;
	scpy (out, d.t->text, cap);
	return true;
}
// Insert Table: its rows and columns
class TableDialog : public Dialog
{
public:
	Textbox *rows, *cols;
	TableDialog () : Dialog (300, 170, "Insert Table") { rows = field (130, titleH () + 16, 60, "4"); cols = field (130, titleH () + 52, 60, "3"); okCancel (); }
	void drawBody () override { label (16, titleH () + 22, "Rows"); label (16, titleH () + 58, "Columns"); }
};
// A chart's data: its title, its categories (up to 8 shown) and series (up to 4)
class ChartDialog : public Dialog
{
public:
	Chart &c; Textbox *title, *cat[8], *ser[4], *val[4][8];
	ChartDialog (Chart &c_) : Dialog (720, 380, "Chart Data"), c (c_)
	{
		int y = titleH () + 14;
		title = field (100, y, 300, c.title);
		y += 44;
		for (int i = 0; i < 8; i++) cat[i] = field (150 + i * 70, y, 66, i < c.ncat ? c.cat[i] : "");
		for (int k = 0; k < 4; k++)
		{
			ser[k] = field (16, y + 34 + k * 34, 128, k < c.nser ? c.ser[k] : "");
			for (int i = 0; i < 8; i++) { char t[32] = ""; if (k < c.nser && i < c.ncat) snprintf (t, sizeof t, "%g", c.val[k][i]); val[k][i] = field (150 + i * 70, y + 34 + k * 34, 66, t); }
		}
		okCancel ();
	}
	void drawBody () override
	{
		label (16, titleH () + 20, "Title");
		label (16, titleH () + 64, "Categories:");
		canvas.text (16, height - 64, "A series a row, a category a column (empty: left out).", wk_mix (C_FACE, C_TEXT, 150));
	}
	void onButton (int tag) override
	{
		if (tag == 1)
		{
			scpy (c.title, title->text, sizeof c.title);
			int nc = 0; for (int i = 0; i < 8; i++) if (cat[i]->text[0]) nc = i + 1;
			int ns = 0; for (int k = 0; k < 4; k++) if (ser[k]->text[0]) ns = k + 1;
			if (nc == 0 || ns == 0) { wk_messagebox ("Chart Data", "Name at least one category and one series.", MB_OK); return; }
			c.ncat = nc; c.nser = c.type == CH_PIE ? 1 : ns;
			for (int i = 0; i < nc; i++) scpy (c.cat[i], cat[i]->text, 24);
			for (int k = 0; k < ns; k++) { scpy (c.ser[k], ser[k]->text, 32); for (int i = 0; i < nc; i++) c.val[k][i] = strtod (val[k][i]->text, 0); }
		}
		close (tag);
	}
};

// ---- the shapes' gallery, the layouts' gallery -----------------------------------------------------------------------
class ShapePopup : public Modal
{
public:
	enum { CS = 30, PER = 7, PAD = 10 };
	ShapePopup (int x, int y) : Modal (PAD * 2 + PER * CS, 0), m_hot (-1)
	{
		resizeTo (width, PAD * 2 + 22 + ((SH_COUNT + PER - 1) / PER) * CS + 40);
		left = x; top = y;
		Root *r = Root::current (); if (r && left + width > r->width) left = r->width - width;
	}
	int pick () { int r = run (); return r - 1; }
	void onDraw () override
	{
		canvas.clear (WK_TRANSPARENT_KEY);
		wk_popup (canvas, 0, 0, width, height, 7, C_FIELD);
		wk_text_l (canvas, PAD, PAD - 2, 20, "Shapes", wk_mix (C_FIELD, C_FIELD_TEXT, 150), 1);
		for (int i = 0; i < SH_COUNT; i++)
		{
			int x = PAD + (i % PER) * CS, y = PAD + 22 + (i / PER) * CS;
			if (i == m_hot) wk_hilite (canvas, x, y, CS - 2, CS - 2, 5, false);
			VPath p; shape_path (p, i, (float) x + 5, (float) y + 6, CS - 12.0f, CS - 14.0f, 160, 0, true); p.fill (canvas, 0x6EA0D2);
			p.clear (); shape_path (p, i, (float) x + 5, (float) y + 6, CS - 12.0f, CS - 14.0f, 160, 1.2f, false); p.fill (canvas, 0x3C64A0);
		}
		int ly = height - PAD - 34;
		canvas.fillRect (PAD, ly, width - 2 * PAD, 1, wk_mix (C_FIELD, C_FIELD_TEXT, 40));
		const char *names[2] = { "Line", "Arrow" };
		for (int k = 0; k < 2; k++)
		{
			int x = PAD + k * (width - 2 * PAD) / 2, y = ly + 4;
			if (m_hot == 100 + k) wk_hilite (canvas, x, y, (width - 2 * PAD) / 2 - 4, 28, 5, false);
			VPath p; p.line (V (x + 8), V (y + 22), V (x + 30), V (y + 6), 26); if (k) p.arrowHead (V (x + 32), V (y + 5), 36, V (7), V (4)); p.fill (canvas, 0x3C64A0);
			wk_text_l (canvas, x + 40, y, 28, names[k], C_FIELD_TEXT);
		}
	}
	int hitAt (int mx, int my)
	{
		int ly = height - PAD - 34;
		if (my >= ly + 4 && my < ly + 32 && mx >= PAD && mx < width - PAD) return 100 + ((mx - PAD) * 2 / (width - 2 * PAD));
		int c = (mx - PAD) / CS, r = (my - PAD - 22) / CS;
		if (mx < PAD || my < PAD + 22 || c >= PER) return -1;
		int i = r * PER + c; return i < SH_COUNT ? i : -1;
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		int h = hitAt (mx, my); if (h != m_hot) { m_hot = h; invalidate (true); }
		bool in = mx >= 0 && my >= 0 && mx < width && my < height;
		if (bl && !pressed) { pressed = true; if (!in) close (0); }
		else if (!bl && pressed) { pressed = false; if (h >= 0) close (h + 1); }
		return true;
	}
	bool onKey (long k) override { if (k == 27) close (0); return true; }
	int m_hot;
};
static void layout_thumb (Canvas &cv, int x, int y, int w, int h, int k)
{
	cv.fillRect (x, y, w, h, 0xFFFFFF); cv.frameRect (x, y, w, h, 0xB4A8A0);
	unsigned bar = 0x4E8696, g = 0xD2CEC8;
	auto lines = [&] (int lx, int ly, int lw, int n) { for (int i = 0; i < n; i++) cv.fillRect (lx, ly + i * 7, lw * (i % 2 ? 9 : 10) / 10, 3, g); };
	switch (k)
	{
	case LY_TITLE: cv.fillRect (x + w * 15 / 100, y + h * 36 / 100, w * 70 / 100, 7, bar); cv.fillRect (x + w * 28 / 100, y + h * 56 / 100, w * 44 / 100, 4, g); break;
	case LY_SECTION: cv.fillRect (x + 1, y + 1, w - 2, h - 2, 0xD8E6EA); cv.fillRect (x + w * 12 / 100, y + h * 48 / 100, w * 60 / 100, 7, bar); break;
	case LY_BLANK: break;
	default:
		cv.fillRect (x + 6, y + 6, w * 55 / 100, 6, bar);
		if (k == LY_CONTENT) lines (x + 8, y + 20, w - 16, 4);
		else if (k == LY_TWO) { lines (x + 8, y + 20, w / 2 - 12, 4); lines (x + w / 2 + 4, y + 20, w / 2 - 12, 4); }
		else if (k == LY_COMPARE) { for (int j = 0; j < 2; j++) { int lx = x + 8 + j * (w / 2 - 2); cv.fillRect (lx, y + 19, w / 2 - 12, 4, 0xF4C8A0); lines (lx, y + 27, w / 2 - 12, 3); } }
		else if (k == LY_PICTURE) { cv.fillRect (x + 1, y + 1, w * 48 / 100, h - 2, 0x96BAC8); cv.fillRect (x + w * 55 / 100, y + 8, w * 38 / 100, 5, bar); lines (x + w * 55 / 100, y + 20, w * 38 / 100, 3); }
	}
}
class LayoutPopup : public Modal
{
public:
	enum { IW = 104, IH = 59, PAD = 12 };
	LayoutPopup (int x, int y, int cur) : Modal (PAD + 4 * (IW + 12) + 2, 0), m_hot (-1), m_cur (cur)
	{ resizeTo (width, 30 + 2 * (IH + 34) + 46); left = x; top = y; Root *r = Root::current (); if (r && left + width > r->width) left = r->width - width; }
	int pick () { int r = run (); return r - 1; }
	void onDraw () override
	{
		canvas.clear (WK_TRANSPARENT_KEY);
		wk_popup (canvas, 0, 0, width, height, 7, C_FIELD);
		char t[48]; snprintf (t, sizeof t, "Layout of slide %d", g_cur + 1);
		wk_text_l (canvas, PAD, 4, 24, t, wk_mix (C_FIELD, C_FIELD_TEXT, 150), 1);
		for (int k = 0; k < LY_COUNT; k++)
		{
			int x = PAD + (k % 4) * (IW + 12), y = 30 + (k / 4) * (IH + 34);
			if (k == m_hot) wk_hilite (canvas, x - 5, y - 5, IW + 10, IH + 30, 5, false);
			else if (k == m_cur) wk_rline (canvas, x - 5, y - 5, IW + 10, IH + 30, 5, C_ACCENT);
			layout_thumb (canvas, x, y, IW, IH, k);
			wk_text_c (canvas, x - 6, y + IH + 2, IW + 12, 18, L1 (g_deck.layout[k].name), k == m_hot ? C_SEL_TEXT : C_FIELD_TEXT);
		}
		int ly = height - 40;
		canvas.fillRect (8, ly, width - 16, 1, wk_mix (C_FIELD, C_FIELD_TEXT, 40));
		if (m_hot == 100) wk_hilite (canvas, 8, ly + 4, width - 16, 30, 5, false);
		wk_text_l (canvas, 16, ly + 4, 30, "Reset the slide to its layout", m_hot == 100 ? C_SEL_TEXT : C_FIELD_TEXT);
	}
	int hitAt (int mx, int my)
	{
		if (my >= height - 36 && my < height - 6) return 100;
		for (int k = 0; k < LY_COUNT; k++) { int x = PAD + (k % 4) * (IW + 12), y = 30 + (k / 4) * (IH + 34); if (mx >= x - 5 && mx < x + IW + 5 && my >= y - 5 && my < y + IH + 25) return k; }
		return -1;
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		int h = hitAt (mx, my); if (h != m_hot) { m_hot = h; invalidate (true); }
		bool in = mx >= 0 && my >= 0 && mx < width && my < height;
		if (bl && !pressed) { pressed = true; if (!in) close (0); }
		else if (!bl && pressed) { pressed = false; if (h >= 0) close (h + 1); }
		return true;
	}
	bool onKey (long k) override { if (k == 27) close (0); return true; }
	int m_hot, m_cur;
};

// ---- the commands ----------------------------------------------------------------------------------------------------
static void ui_undo () { if (g_notes->ta->hasFocus) { focus_view (); } sl::cmd_undo (); g_thumbs.prune (); after (); }
static void ui_redo () { sl::cmd_redo (); g_thumbs.prune (); after (); }
static Buf g_clipText;
static void cmd_copy ()
{
	if (g_notes->ta->hasFocus) { g_notes->ta->copy (); return; }
	if (g_edit) { Buf t; text_selected_utf8 (t); if (t.n) clip_set_text (t.str ()); return; }
	if (g_viewMode == VIEW_SORTER || g_list->hasFocus)
	{
		clip_clear_own ();
		if (g_sorterSel.n > 1) for (int i = 0; i < g_deck.slides.n; i++) { if (g_sorterSel.find (i) >= 0) g_clipSlides.push (slide_copy (g_deck.slides[i])); }
		else if (cur_slide ()) g_clipSlides.push (slide_copy (cur_slide ()));
		g_clipKind = 2;
		g_clipText.clear (); g_clipText.puts ("[Slides: slides]"); clip_set_text (g_clipText.str ());
		return;
	}
	if (!g_sel.n) return;
	copy_objects ();
	g_clipText.clear ();
	char b[256]; clip_get_text (b, sizeof b); g_clipText.puts (b);
}
static void cmd_cut ()
{
	if (g_notes->ta->hasFocus) { g_notes->ta->cut (); return; }
	cmd_copy ();
	if (g_edit) { TextBody *tb = edit_body (); if (tb && has_tsel ()) { begin_change (); TPos a = tsel_a (); tb_delete (*tb, a, tsel_b ()); g_caret = g_anchor = a; done_change (); } after (); return; }
	if (g_viewMode == VIEW_SORTER || g_list->hasFocus) { cmd_delete_slide (); after (); return; }
	cmd_delete_objects (); after ();
}
static bool own_clip ()
{
	char b[256]; int n = clip_get_text (b, sizeof b);
	return n > 0 && g_clipText.n && !strcmp (b, g_clipText.str ());
}
static void cmd_paste ()
{
	if (g_notes->ta->hasFocus) { g_notes->ta->paste (); return; }
	if (own_clip () && g_clipKind == 2 && g_clipSlides.n && (g_viewMode == VIEW_SORTER || g_list->hasFocus || !g_edit))
	{
		begin_change ();
		for (int i = 0; i < g_clipSlides.n; i++)
		{
			Slide *s = slide_copy (g_clipSlides[i]);
			for (int k = 0; k < s->obj.n; k++) { int old = s->obj[k]->id, nid = g_deck.nextId++; s->obj[k]->id = nid; for (int a = 0; a < s->anim.n; a++) if (s->anim[a].obj == old) s->anim[a].obj = nid; }
			g_deck.slides.insert (g_cur + 1 + i, s);
		}
		g_cur++; end_edit (); g_sel.clear ();
		done_change (); notify_slides (); after ();
		return;
	}
	if (own_clip () && g_clipKind == 1 && g_clipObj.n && !g_edit)
	{
		Slide *s = cur_slide (); if (!s) return;
		begin_change ();
		g_sel.clear ();
		for (int i = 0; i < g_clipObj.n; i++)
		{
			Object *o = obj_copy (g_clipObj[i]); o->id = g_deck.nextId++;
			if (s->by_id (g_clipObj[i]->id) || true) { o->x += 400; o->y += 400; g_clipObj[i]->x += 400; g_clipObj[i]->y += 400; }
			if (o->ph != PH_NONE && o->kind == OB_TEXT) { bool has = false; for (int k = 0; k < s->obj.n; k++) if (s->obj[k]->ph == o->ph) has = true; if (has) o->ph = PH_NONE; }
			s->obj.push (o); g_sel.push (o->id);
		}
		done_change (); if (g_onSlides) g_onSlides (); after ();
		return;
	}
	// a picture from the clipboard
	int iw, ih; unsigned *img = !g_edit ? clip_get_image (&iw, &ih) : 0;
	if (img)
	{
		unsigned n = 0; unsigned char *png = pngsave::png_encode (img, iw, ih, true, &n); delete[] img;
		if (png) { Object *o = make_picture ("pasted.png", png, n); delete[] png; if (o) add_object (o); after (); }
		return;
	}
	// text: into the text edited, or a new text box
	char *t = (char *) malloc (65536); int n = clip_get_text (t, 65536);
	if (n > 0)
	{
		unsigned *u = (unsigned *) malloc (sizeof (unsigned) * (n + 1)); int m = 0;
		for (int i = 0; i < n; ) { int l; u[m++] = ss::u8_dec (t + i, n - i, &l); i += l > 0 ? l : 1; }
		if (g_edit) g_view->type (u, m);
		else
		{
			Object *o = make_text_box (g_deck.sw / 10, g_deck.sh / 3, g_deck.sw * 8 / 10, 1500);
			o->tb.clear (); o->tb.ensure (); tb_insert (o->tb, tpos (0, 0), u, m, cf_inherit ());
			add_object (o);
		}
		free (u);
	}
	free (t);
	after ();
}
static void cmd_delete ()
{
	if (g_edit) { long k = KEY_DEL; g_view->onKey (k); return; }
	if (g_viewMode == VIEW_SORTER || g_list->hasFocus) { cmd_delete_slide (); after (); return; }
	cmd_delete_objects (); after ();
}
static void cmd_select_all ()
{
	if (g_notes->ta->hasFocus) { g_notes->ta->selectAll (); return; }
	if (g_edit) { TextBody *tb = edit_body (); int lp = tb->p.n - 1; g_anchor = tpos (0, 0); g_caret = tpos (lp, tb->p[lp]->len); after (); return; }
	Slide *s = cur_slide (); if (!s) return;
	g_sel.clear (); for (int i = 0; i < s->obj.n; i++) g_sel.push (s->obj[i]->id);
	after (); focus_view ();
}
static void cmd_duplicate ()
{
	if (g_viewMode == VIEW_SORTER || g_list->hasFocus || !g_sel.n) { cmd_duplicate_slide (); after (); return; }
	Slide *s = cur_slide (); if (!s) return;
	begin_change ();
	Vec<int> nsel;
	for (int k = 0; k < g_sel.n; k++) { Object *o = obj_of (g_sel[k]); if (!o) continue; Object *c = obj_copy (o); c->id = g_deck.nextId++; c->x += 400; c->y += 400; if (c->ph != PH_NONE) c->ph = PH_NONE; s->obj.push (c); nsel.push (c->id); }
	g_sel.clear (); for (int i = 0; i < nsel.n; i++) g_sel.push (nsel[i]);
	done_change (); if (g_onSlides) g_onSlides (); after ();
}
static void cmd_new_slide_btn () { cmd_new_slide (); after (); focus_view (); }
static void drop_new_slide (ToolButton &b)
{
	int x, y; b.below (&x, &y);
	LayoutPopup p (x, y, -1);
	int k = p.pick ();
	if (k >= 0 && k < LY_COUNT) { cmd_new_slide (k); after (); }
	focus_view ();
}
static void cmd_layout ()
{
	ToolButton &b = *g_btn[SL_LAYOUT];
	int x, y; b.below (&x, &y);
	Slide *s = cur_slide (); if (!s) return;
	LayoutPopup p (x, y, s->layout);
	int k = p.pick ();
	if (k == 100) set_layout (s->layout);
	else if (k >= 0 && k < LY_COUNT) set_layout (k);
	after (); focus_view ();
}
static void cmd_dup_slide () { cmd_duplicate_slide (); after (); }
static void cmd_del_slide () { cmd_delete_slide (); g_thumbs.prune (); after (); }
static void set_tool (int t) { g_tool = t; end_edit (); g_sel.clear (); after (); focus_view (); }
static void cmd_textbox () { set_tool (TOOL_TEXT); }
static void cmd_line () { set_tool (TOOL_LINE); }
static void cmd_arrow () { set_tool (TOOL_ARROW); }
static void cmd_shape () { set_tool (TOOL_SHAPE); }
static void drop_shapes (ToolButton &b)
{
	int x, y; b.below (&x, &y);
	ShapePopup p (x, y);
	int k = p.pick ();
	if (k == 100) set_tool (TOOL_LINE);
	else if (k == 101) set_tool (TOOL_ARROW);
	else if (k >= 0) { g_toolShape = k; set_tool (TOOL_SHAPE); }
	else focus_view ();
}
static void picture_into (Object *ph)
{
	char path[200];
	if (!wk_file_open (path, sizeof path, "SD:/docs/pictures")) { focus_view (); return; }
	unsigned n = 0; unsigned char *b = read_all (path, &n);
	if (!b) { focus_view (); return; }
	Object *o = make_picture (base_name (path), b, n, ph);
	free (b);
	if (!o) { wk_messagebox ("Insert Picture", "That file is not a picture Slides can read (PNG, JPEG, BMP, GIF, WebP, PCX).", MB_OK); focus_view (); return; }
	if (ph)
	{
		// the placeholder replaced by the picture (in its place in the order)
		Slide *s = cur_slide ();
		begin_change ();
		int i = s->index_of (ph->id);
		o->ph = PH_PICTURE;
		if (i >= 0) { delete s->obj[i]; s->obj[i] = o; } else s->obj.push (o);
		g_sel.clear (); g_sel.push (o->id);
		done_change (); if (g_onSlides) g_onSlides ();
	}
	else add_object (o);
	after (); focus_view ();
}
static void cmd_picture ()
{
	// an empty picture placeholder on the slide: filled
	Slide *s = cur_slide ();
	for (int i = 0; s && i < s->obj.n; i++) if (s->obj[i]->ph == PH_PICTURE && s->obj[i]->kind != OB_PICTURE) { picture_into (s->obj[i]); return; }
	picture_into (0);
}
static void cmd_table ()
{
	TableDialog d;
	if (d.run () == 1)
	{
		int r = iclamp (atoi (d.rows->text), 1, 30), c = iclamp (atoi (d.cols->text), 1, 12);
		Object *o = make_table (r, c);
		// the body placeholder's place, when the slide has an empty one
		Slide *s = cur_slide ();
		for (int i = 0; s && i < s->obj.n; i++)
			if ((s->obj[i]->ph == PH_BODY || s->obj[i]->ph == PH_BODY2) && s->obj[i]->tb.empty ())
			{
				Object *p = s->obj[i];
				o->x = p->x; o->y = p->y; for (int k = 0; k < c; k++) o->tbl->colW[k] = p->w / c; o->w = p->w;
				begin_change (); delete p; s->obj.erase (i); s->obj.push (o); g_sel.clear (); g_sel.push (o->id); done_change (); if (g_onSlides) g_onSlides (); after (); focus_view ();
				return;
			}
		add_object (o);
	}
	after (); focus_view ();
}
static void cmd_chart_type (int type)
{
	Object *o = make_chart (type);
	ChartDialog d (*o->chart);
	if (d.run () != 1) { delete o; focus_view (); return; }
	add_object (o);
	after (); focus_view ();
}
static void cmd_chart () { cmd_chart_type (CH_COLUMN); }
static void drop_chart (ToolButton &b)
{
	int x, y; b.below (&x, &y);
	Sidebar::g_listItems = CHART_NAMES; Sidebar::g_listN = CH_COUNT;
	ListPopup lp (x, y, 160, CH_COUNT, 26, 0, Sidebar::list_row);
	int k = lp.pick ();
	if (k >= 0) cmd_chart_type (k); else focus_view ();
}
static void chart_data (Object *o)
{
	if (!o || !o->chart) return;
	Chart c = *o->chart;
	ChartDialog d (c);
	if (d.run () == 1) { begin_change (); *o->chart = c; done_change (); after (); }
	focus_view ();
}
static void cmd_arr (int d) { cmd_arrange (d); after (); }
static void cmd_front () { cmd_arr (2); }
static void cmd_back () { cmd_arr (-2); }
static void cmd_forward () { cmd_arr (1); }
static void cmd_backward () { cmd_arr (-1); }
// the text's formats
static void flag (unsigned short f) { CharFmt c = shown_format (); apply_flag (f, !(c.flags & f)); after (); if (g_edit) focus_view (); }
static void cmd_bold () { flag (CF_BOLD); }
static void cmd_italic () { flag (CF_ITALIC); }
static void cmd_under () { flag (CF_UNDER); }
static void cmd_strike () { flag (CF_STRIKE); }
static void cmd_super () { flag (CF_SUPER); }
static void cmd_sub () { flag (CF_SUB); }
static void para_cmd (ParaFn fn, int a) { apply_para (fn, a); after (); }
static void cmd_left () { para_cmd (pf_align, AL_LEFT); }
static void cmd_center () { para_cmd (pf_align, AL_CENTER); }
static void cmd_right () { para_cmd (pf_align, AL_RIGHT); }
static void cmd_justify () { para_cmd (pf_align, AL_JUSTIFY); }
static void cmd_bullets () { ParaFmt p = shown_para (); para_cmd (pf_bullet, p.bullet == BU_BULLET ? BU_NONE : BU_BULLET); }
static void cmd_numbers () { ParaFmt p = shown_para (); para_cmd (pf_bullet, p.bullet == BU_NUMBER ? BU_NONE : BU_NUMBER); }
static void cmd_indent () { para_cmd (pf_level, 1); }
static void cmd_outdent () { para_cmd (pf_level, -1); }
static void set_anchor (int a)
{
	if (!g_sel.n && !g_edit) return;
	begin_change ();
	for (int k = 0; k < g_sel.n; k++) { Object *o = obj_of (g_sel[k]); if (o) o->tb.anchor = (signed char) a; }
	done_change (); after ();
}
static void cmd_anchor_t () { set_anchor (AN_TOP); }
static void cmd_anchor_m () { set_anchor (AN_MIDDLE); }
static void cmd_anchor_b () { set_anchor (AN_BOTTOM); }
static void apply_color (unsigned c) { FmtChange ch; ch.what = FC_COLOR; ch.color = c; apply_format (ch); after (); }
static void cmd_color () { apply_color (g_textColor); }
static void drop_color (ToolButton &b)
{
	int x, y; b.below (&x, &y);
	palette_make ();
	ColorPopup p (x, y, g_palette, 70, 10, "Automatic");
	long c = p.pick ();
	if (c != -1) { unsigned code = palette_code (c); if (code != AUTO) { g_textColor = code; b.setBar (g_deck.rgb (code)); } apply_color (code); }
	focus_view ();
}
static void cmd_spacing ()
{
	ParaFmt p = shown_para ();
	static const int sp[] = { 90, 100, 115, 150, 200 };
	int i = 0; for (int k = 0; k < 5; k++) if (p.spacing >= sp[k]) i = k;
	para_cmd (pf_spacing, sp[(i + 1) % 5]);
}
// the zoom
static const int ZOOMS[] = { 25, 33, 50, 66, 75, 100, 125, 150, 200, 300 };
static void zoom_to (int z) { g_view->set_zoom (z); after (); }
static void cmd_zoom_in () { int z = g_view->zoom_pct (); for (int i = 0; i < 10; i++) if (ZOOMS[i] > z + 1) { zoom_to (ZOOMS[i]); return; } }
static void cmd_zoom_out () { int z = g_view->zoom_pct (); for (int i = 9; i >= 0; i--) if (ZOOMS[i] < z - 1) { zoom_to (ZOOMS[i]); return; } }
static void cmd_zoom_fit () { zoom_to (0); }
static void zoom_value (Canvas &cv, int x, int y, int, int h, unsigned ink) { char t[16]; if (g_view->zoom) snprintf (t, sizeof t, "%d%%", g_view->zoom_pct ()); else snprintf (t, sizeof t, "Fit (%d%%)", g_view->zoom_pct ()); wk_text_l (cv, x, y, h, t, ink); }
static const char *const ZOOM_ROWS[] = { "Fit the window", "25%", "33%", "50%", "66%", "75%", "100%", "125%", "150%", "200%", "300%" };
static void pick_zoom (PickBox &b)
{
	int x, y; b.below (&x, &y);
	Sidebar::g_listItems = ZOOM_ROWS; Sidebar::g_listN = 11;
	ListPopup lp (x, y, 140, 11, 24, -1, Sidebar::list_row, 11);
	int r = lp.pick ();
	if (r == 0) zoom_to (0); else if (r > 0) zoom_to (ZOOMS[r - 1]);
	focus_view ();
}
// the font, the size
static void font_value (Canvas &cv, int x, int y, int w, int h, unsigned ink)
{
	CharFmt f = shown_format ();
	char b[48]; scpy (b, g_deck.font_name (f.font), sizeof b);
	while (wk_text_w (b) > w && strlen (b) > 3) { int n = (int) strlen (b); b[n - 4] = '.'; b[n - 3] = '.'; b[n - 2] = 0; }
	wk_text_l (cv, x, y, h, b, ink);
}
static void pick_font (PickBox &b)
{
	int x, y; b.below (&x, &y);
	CharFmt f = shown_format ();
	ListPopup lp (x, y, 250, fnt::count (), 28, fnt::find (g_deck.font_name (f.font)), Sidebar::font_row);
	int r = lp.pick ();
	if (r >= 0) { FmtChange c; c.what = FC_FONT; c.font = (short) g_deck.font_index (fnt::name (r)); apply_format (c); after (); }
	focus_view ();
}
static void size_value (Canvas &cv, int x, int y, int, int h, unsigned ink) { CharFmt f = shown_format (); char t[16]; if (f.size % 10) snprintf (t, sizeof t, "%.1f", f.size / 10.0); else snprintf (t, sizeof t, "%d", f.size / 10); wk_text_l (cv, x, y, h, t, ink); }
static const char *const SIZE_ROWS[] = { "8", "10", "12", "14", "16", "18", "20", "24", "28", "32", "36", "40", "44", "48", "54", "60", "72", "88" };
static void pick_size (PickBox &b)
{
	int x, y; b.below (&x, &y);
	Sidebar::g_listItems = SIZE_ROWS; Sidebar::g_listN = 18;
	CharFmt f = shown_format (); int sel = -1; for (int i = 0; i < 18; i++) if (atoi (SIZE_ROWS[i]) * 10 == f.size) sel = i;
	ListPopup lp (x, y, 70, 18, 24, sel, Sidebar::list_row);
	int r = lp.pick ();
	if (r >= 0) { FmtChange c; c.what = FC_SIZE; c.size = (short) (atoi (SIZE_ROWS[r]) * 10); apply_format (c); after (); }
	focus_view ();
}

// ---- the views, the show -------------------------------------------------------------------------------------------
static void relayout ();
static void set_view (int v) { g_viewMode = v; end_edit (); relayout (); after (); focus_view (); }
static void cmd_normal () { set_view (VIEW_NORMAL); }
static void cmd_sorter () { g_sorterSel.clear (); set_view (VIEW_SORTER); }
static void cmd_notes () { g_showNotes = !g_showNotes; relayout (); after (); }
static void open_slide (int i) { go_slide (i); set_view (VIEW_NORMAL); }
static void show (int from, bool presenter)
{
	end_edit (); g_notes->sync ();
	Show s;
	s.run (from, presenter);
	g_root->attach ();
	g_root->invalidate (true);
	g_view->C.drop_all ();
	after ();
}
static void cmd_show_start () { show (0, false); }
static void cmd_show_here () { show (g_cur, false); }
static void cmd_presenter () { show (g_cur, true); }
static void play_effects () { show (g_cur, false); }

// ---- the sections, the slide's menu ----------------------------------------------------------------------------------
static void cmd_add_section ()
{
	Slide *s = cur_slide (); if (!s) return;
	char n[48]; scpy (n, s->section[0] ? s->section : "New section", sizeof n);
	if (ask_line ("Section", "The section starting at this slide:", n, sizeof n)) { begin_change (); scpy (s->section, n[0] ? n : "Section", sizeof s->section); done_change (); notify_slides (); }
	after (); focus_view ();
}
static void cmd_remove_section () { Slide *s = cur_slide (); if (!s || !s->section[0]) return; begin_change (); s->section[0] = 0; done_change (); notify_slides (); after (); }
static void cmd_hide () { cmd_hide_slide (); after (); }
static void cmd_footer ()
{
	char t[96]; scpy (t, g_deck.footerText, sizeof t);
	if (ask_line ("Header and Footer", "The footer on every slide (empty: none):", t, sizeof t)) { begin_change (); scpy (g_deck.footerText, t, sizeof g_deck.footerText); g_deck.footer = t[0] != 0; done_change (); notify_slides (); }
	after (); focus_view ();
}
static void slide_menu (int x, int y)
{
	PopupMenu m (x, y);
	Slide *s = cur_slide ();
	m.add ("New Slide", 1, true, "^M");
	m.add ("Duplicate Slide", 2, true);
	m.add ("Delete Slide", 3, g_deck.slides.n > 0);
	m.separator ();
	m.add (s && s->hidden ? "Show Slide" : "Hide Slide", 4, true);
	m.add ("Layout...", 5, true);
	m.separator ();
	m.add (s && s->section[0] ? "Rename Section..." : "Add Section Here...", 6, true);
	if (s && s->section[0]) m.add ("Remove Section", 7, true);
	m.separator ();
	m.add ("Copy", 8, true, "^C"); m.add ("Paste", 9, true, "^V");
	switch (m.run ())
	{
	case 1: cmd_new_slide_btn (); break;
	case 2: cmd_dup_slide (); break;
	case 3: cmd_del_slide (); break;
	case 4: cmd_hide (); break;
	case 5: cmd_layout (); break;
	case 6: cmd_add_section (); break;
	case 7: cmd_remove_section (); break;
	case 8: g_list->setFocus (); cmd_copy (); break;
	case 9: g_list->setFocus (); cmd_paste (); break;
	}
}
static void view_menu (int x, int y)
{
	PopupMenu m (x, y);
	bool any = g_sel.n > 0;
	if (any)
	{
		m.add ("Cut", 1, true, "^X"); m.add ("Copy", 2, true, "^C"); m.add ("Paste", 3, true, "^V"); m.add ("Delete", 4, true, "Del");
		m.separator ();
		Object *o = sel_one ();
		if (o && (o->kind == OB_TEXT || o->kind == OB_SHAPE)) m.add ("Edit Text", 5, true, "F2");
		if (o && o->kind == OB_CHART) m.add ("Chart Data...", 12, true);
		m.add ("Bring to Front", 6, true); m.add ("Send to Back", 7, true);
		m.separator ();
		m.add ("Format...", 8, true); m.add ("Animate...", 9, true);
	}
	else
	{
		m.add ("Paste", 3, true, "^V");
		m.separator ();
		m.add ("New Slide", 10, true); m.add ("Layout...", 11, true); m.add ("Background...", 13, true);
	}
	switch (m.run ())
	{
	case 1: cmd_cut (); break; case 2: cmd_copy (); break; case 3: cmd_paste (); break; case 4: cmd_delete (); break;
	case 5: { Object *o = sel_one (); if (o) { begin_edit (o->id); after (); } break; }
	case 6: cmd_front (); break; case 7: cmd_back (); break;
	case 8: g_sbTab = SB_SHAPE; after (); break; case 9: g_sbTab = SB_ANIM; after (); break;
	case 10: cmd_new_slide_btn (); break; case 11: cmd_layout (); break; case 12: chart_data (sel_one ()); break;
	case 13: g_sbTab = SB_SLIDE; after (); break;
	}
	focus_view ();
}

// ---- the sidebar's actions --------------------------------------------------------------------------------------------
static void each_sel (void (*fn) (Object &, double, unsigned), double v, unsigned c)
{
	begin_change ();
	for (int k = 0; k < g_sel.n; k++) { Object *o = obj_of (g_sel[k]); if (o) fn (*o, v, c); }
	done_change (); if (g_onSlides) g_onSlides ();
}
void Sidebar::pick_list (SRow &r, int x, int y, int w)
{
	Slide *s = cur_slide (); if (!s) return;
	Object *o = g_sel.n ? obj_of (g_sel[0]) : 0;
	int k = -1;
	switch (r.id)
	{
	case I_LAYOUT: { LayoutPopup p (x, y, s->layout); k = p.pick (); if (k == 100) set_layout (s->layout); else if (k >= 0 && k < LY_COUNT) set_layout (k); break; }
	case I_FONT_MAJOR: case I_FONT_MINOR: case I_FONT:
	{
		ListPopup lp (x, y, imax (w, 230), fnt::count (), 28, fnt::find (r.value), font_row);
		k = lp.pick ();
		if (k < 0) break;
		if (r.id == I_FONT) { FmtChange c; c.what = FC_FONT; c.font = (short) g_deck.font_index (fnt::name (k)); apply_format (c); }
		else { begin_change (); scpy (r.id == I_FONT_MAJOR ? g_deck.theme.major : g_deck.theme.minor, fnt::name (k), 48); done_change (); notify_slides (); }
		break;
	}
	case I_BG_TYPE:
	{
		static const char *T[3] = { "The theme's", "Solid colour", "Gradient" };
		k = list (x, y, w, T, 3, s->bg.type == FILL_INHERIT ? 0 : s->bg.type == FILL_GRADIENT ? 2 : 1);
		if (k < 0) break;
		begin_change ();
		Fill cur = s->bg.type == FILL_INHERIT ? g_deck.masterBg : s->bg;
		if (k == 0) s->bg.type = FILL_INHERIT;
		else { s->bg = cur; s->bg.type = k == 2 ? FILL_GRADIENT : FILL_SOLID; if (k == 2 && s->bg.c2 == s->bg.c1) s->bg.c2 = THEME | TC_LT2; }
		done_change (); notify_slides ();
		break;
	}
	case I_SIZE:
		k = list (x, y, w, SIZE_NAMES, 4, -1);
		if (k >= 0)
		{
			begin_change ();
			double fx = (double) SIZE_W[k] / g_deck.sw, fy = (double) SIZE_H[k] / g_deck.sh;
			auto scale = [&] (Object *ob) { ob->x = (int) (ob->x * fx); ob->y = (int) (ob->y * fy); ob->w = (int) (ob->w * fx); ob->h = (int) (ob->h * fy); };
			for (int i = 0; i < g_deck.slides.n; i++) for (int j = 0; j < g_deck.slides[i]->obj.n; j++) scale (g_deck.slides[i]->obj[j]);
			for (int i = 0; i < g_deck.decor.n; i++) scale (g_deck.decor[i]);
			for (int l = 0; l < LY_COUNT; l++) for (int j = 0; j < g_deck.layout[l].ph.n; j++) scale (g_deck.layout[l].ph[j]);
			g_deck.sw = SIZE_W[k]; g_deck.sh = SIZE_H[k];
			g_thumbs.clear ();
			done_change (); notify_slides ();
		}
		break;
	case I_FILL_TYPE:
		k = list (x, y, w, FILL_NAMES, 3, -1);
		if (k >= 0 && o) { begin_change (); for (int i = 0; i < g_sel.n; i++) { Object *q = obj_of (g_sel[i]); if (!q) continue; q->fill.type = (signed char) (k == 0 ? FILL_NONE : k == 1 ? FILL_SOLID : FILL_GRADIENT); if (k == 2 && q->fill.c2 == q->fill.c1) q->fill.c2 = THEME | TC_LT1; } done_change (); notify_slides (); }
		break;
	case I_LINE_TYPE:
		k = list (x, y, w, LINE_NAMES, 4, -1);
		if (k >= 0 && o) { begin_change (); for (int i = 0; i < g_sel.n; i++) { Object *q = obj_of (g_sel[i]); if (q) q->line.type = (signed char) k; } done_change (); notify_slides (); }
		break;
	case I_HEAD0: case I_HEAD1:
		k = list (x, y, w, HEAD_NAMES, 4, -1);
		if (k >= 0 && o) { begin_change (); for (int i = 0; i < g_sel.n; i++) { Object *q = obj_of (g_sel[i]); if (q) { if (r.id == I_HEAD0) q->line.head0 = (signed char) k; else q->line.head1 = (signed char) k; } } done_change (); notify_slides (); }
		break;
	case I_SHAPE:
		k = list (x, y, w, SHAPE_NAMES, SH_COUNT, o ? o->shape : -1);
		if (k >= 0 && o) { begin_change (); for (int i = 0; i < g_sel.n; i++) { Object *q = obj_of (g_sel[i]); if (q && (q->kind == OB_SHAPE || q->kind == OB_PICTURE)) q->shape = (signed char) k; } done_change (); notify_slides (); }
		break;
	case I_TBL_ROWS:
	{
		static const char *T[6] = { "Insert a row below", "Insert a column at the right", "Delete the last row", "Delete the last column", "Columns evenly", "Edit the first cell" };
		k = list (x, y, w, T, 6, -1);
		if (k < 0 || !o || !o->tbl) break;
		begin_change ();
		Table *t = o->tbl;
		int nr = t->rows + (k == 0) - (k == 2 && t->rows > 1), nc = t->cols + (k == 1) - (k == 3 && t->cols > 1);
		if (k <= 3 && (nr != t->rows || nc != t->cols))
		{
			Table *n = new Table (nr, nc);
			for (int c = 0; c < nc; c++) n->colW[c] = c < t->cols ? t->colW[c] : t->colW[t->cols - 1];
			for (int rr = 0; rr < nr; rr++) n->rowH[rr] = rr < t->rows ? t->rowH[rr] : t->rowH[t->rows - 1];
			for (int rr = 0; rr < nr; rr++) for (int c = 0; c < nc; c++) { if (rr < t->rows && c < t->cols) { n->at (rr, c).copy_from (t->at (rr, c)); n->cfill[rr * nc + c] = t->cfill[rr * t->cols + c]; } else { n->at (rr, c).inset[0] = n->at (rr, c).inset[2] = 200; n->at (rr, c).inset[1] = n->at (rr, c).inset[3] = 100; } }
			n->header = t->header; n->banded = t->banded;
			if (k == 1) { int sum = 0; for (int c = 0; c < nc; c++) sum += n->colW[c]; for (int c = 0; c < nc; c++) n->colW[c] = n->colW[c] * o->w / sum; }
			delete t; o->tbl = n;
		}
		else if (k == 4) { int sum = 0; for (int c = 0; c < t->cols; c++) sum += t->colW[c]; for (int c = 0; c < t->cols; c++) t->colW[c] = sum / t->cols; }
		done_change (); notify_slides ();
		if (k == 5) begin_edit (o->id, 0, 0);
		break;
	}
	case I_CHART_TYPE:
		k = list (x, y, w, CHART_NAMES, CH_COUNT, o && o->chart ? o->chart->type : -1);
		if (k >= 0 && o && o->chart) { begin_change (); o->chart->type = k; if (k == CH_PIE) o->chart->nser = imin (o->chart->nser, 1); done_change (); notify_slides (); }
		break;
	case I_BULLET:
		k = list (x, y, w, BULLET_NAMES, 3, -1);
		if (k >= 0) apply_para (pf_bullet, k);
		break;
	case I_FIT:
		k = list (x, y, w, FIT_NAMES, 3, -1);
		if (k >= 0) { begin_change (); for (int i = 0; i < g_sel.n; i++) { Object *q = obj_of (g_sel[i]); if (q) q->tb.fit = (signed char) k; } done_change (); notify_slides (); }
		break;
	case I_TR_TYPE:
		k = list (x, y, w, TR_NAMES, TR_COUNT, s->tr.type);
		if (k >= 0) { begin_change (); s->tr.type = (signed char) k; done_change (); notify_slides (); }
		break;
	case I_TR_DIR:
		k = list (x, y, w, DIR_NAMES + 1, 4, s->tr.dir - 1);
		if (k >= 0) { begin_change (); s->tr.dir = (signed char) (k + 1); done_change (); }
		break;
	case I_TR_ADV:
		k = list (x, y, w, ADV_NAMES, 2, s->tr.after >= 0);
		if (k >= 0) { begin_change (); s->tr.after = k ? (s->tr.after >= 0 ? s->tr.after : 5000) : -1; done_change (); notify_slides (); }
		break;
	case I_FX_ADD:
	{
		if (!g_sel.n) break;
		// the class, then the effect
		static const char *A[12] = { "Entrance: Appear", "Entrance: Fade", "Entrance: Fly in", "Entrance: Wipe", "Entrance: Zoom", "Entrance: Float in",
					     "Emphasis: Pulse", "Emphasis: Grow", "Emphasis: Spin", "Exit: Fade", "Exit: Fly out", "Exit: Zoom" };
		static const signed char CL[12] = { AC_ENTRANCE, AC_ENTRANCE, AC_ENTRANCE, AC_ENTRANCE, AC_ENTRANCE, AC_ENTRANCE, AC_EMPHASIS, AC_EMPHASIS, AC_EMPHASIS, AC_EXIT, AC_EXIT, AC_EXIT };
		static const signed char FX[12] = { FX_APPEAR, FX_FADE, FX_FLY, FX_WIPE, FX_ZOOM, FX_FLOAT, FX_PULSE, FX_GROW, FX_SPIN, FX_FADE, FX_FLY, FX_ZOOM };
		k = list (x, y, w, A, 12, -1);
		if (k < 0) break;
		begin_change ();
		for (int i = 0; i < g_sel.n; i++)
		{
			Anim a; a.obj = g_sel[i]; a.cls = CL[k]; a.fx = FX[k]; a.start = (signed char) (i == 0 ? ST_CLICK : ST_WITH); a.dir = DIR_DOWN; a.byPara = false; a.delay = 0; a.dur = FX[k] == FX_APPEAR ? 0 : 500;
			s->anim.push (a);
		}
		g_animSel = s->anim.n - 1;
		done_change (); notify_slides ();
		break;
	}
	case I_FX_EFFECT: case I_FX_START: case I_FX_DIR:
	{
		if (g_animSel < 0 || g_animSel >= s->anim.n) break;
		Anim &a = s->anim[g_animSel];
		if (r.id == I_FX_EFFECT) { k = list (x, y, w, FX_NAMES, FX_COUNT, a.fx); if (k >= 0) { begin_change (); s->anim[g_animSel].fx = (signed char) k; done_change (); } }
		else if (r.id == I_FX_START) { k = list (x, y, w, START_NAMES, 3, a.start); if (k >= 0) { begin_change (); s->anim[g_animSel].start = (signed char) k; done_change (); } }
		else { k = list (x, y, w, DIR_NAMES + 1, 4, a.dir - 1); if (k >= 0) { begin_change (); s->anim[g_animSel].dir = (signed char) (k + 1); done_change (); } }
		notify_slides ();
		break;
	}
	}
	build (); invalidate (true); after ();
}
static void set_fill_c1 (Object &o, double, unsigned c) { o.fill.c1 = c; if (o.fill.type == FILL_NONE) o.fill.type = FILL_SOLID; }
static void set_fill_c2 (Object &o, double, unsigned c) { o.fill.c2 = c; }
static void set_fill_angle (Object &o, double v, unsigned) { o.fill.angle = (short) v; }
static void set_fill_alpha (Object &o, double v, unsigned) { o.fill.alpha = (unsigned char) (255 - v * 255 / 100); }
static void set_line_c (Object &o, double, unsigned c) { o.line.color = c; if (o.line.type == LN_NONE) o.line.type = LN_SOLID; }
static void set_line_w (Object &o, double v, unsigned) { o.line.width = (short) (v * 35.278); }
static void set_radius (Object &o, double v, unsigned) { o.radius = (short) (v * 10); }
static void set_shadow (Object &o, double v, unsigned) { o.shadow = v != 0; }
static void set_x (Object &o, double v, unsigned) { o.x = (int) (v * 1000); }
static void set_y (Object &o, double v, unsigned) { o.y = (int) (v * 1000); }
static void set_w (Object &o, double v, unsigned) { o.w = imax (100, (int) (v * 1000)); }
static void set_h (Object &o, double v, unsigned) { o.h = imax (100, (int) (v * 1000)); }
static void set_rot (Object &o, double v, unsigned) { o.rot = (short) (((int) v % 360 + 360) % 360); }
static void set_flip (Object &o, double v, unsigned) { o.flipH = v != 0; }
static void set_crop (Object &o, double v, unsigned) { o.crop[0] = o.crop[2] = (short) (v * 10); }
static void set_wrap (Object &o, double v, unsigned) { o.tb.wrap = v != 0; }
void Sidebar::act (const SRow &r, int sub, double v)
{
	Slide *s = cur_slide (); if (!s) return;
	Object *o = g_sel.n ? obj_of (g_sel[0]) : 0;
	switch (r.id)
	{
	case I_RESET: set_layout (s->layout); break;
	case I_THEME:
	{
		begin_change ();
		char major[48], minor[48]; scpy (major, g_deck.theme.major, 48); scpy (minor, g_deck.theme.minor, 48);
		theme_set (g_deck.theme, sub);
		scpy (g_deck.theme.major, major, 48); scpy (g_deck.theme.minor, minor, 48);
		done_change (); notify_slides ();
		break;
	}
	case I_BG_C1: case I_BG_C2:
		begin_change ();
		if (s->bg.type == FILL_INHERIT) { s->bg = g_deck.masterBg; s->bg.type = FILL_SOLID; }
		if (sub == 1) s->bg.type = FILL_INHERIT;
		else if (r.id == I_BG_C1) s->bg.c1 = r.color; else s->bg.c2 = r.color;
		done_change (); notify_slides ();
		break;
	case I_BG_ANGLE: begin_change (); s->bg.angle = (short) v; done_change (); notify_slides (); break;
	case I_MASTER_OBJ: begin_change (); s->masterObjects = v != 0; done_change (); notify_slides (); break;
	case I_NUMBER: begin_change (); g_deck.number = v != 0; done_change (); notify_slides (); break;
	case I_FOOTER: if (v != 0 && !g_deck.footerText[0]) cmd_footer (); else { begin_change (); g_deck.footer = v != 0; done_change (); notify_slides (); } break;
	case I_HIDDEN: begin_change (); s->hidden = v != 0; done_change (); notify_slides (); break;
	case I_FILL_C1: if (sub == 1) { begin_change (); for (int i = 0; i < g_sel.n; i++) { Object *q = obj_of (g_sel[i]); if (q) q->fill.type = FILL_NONE; } done_change (); } else each_sel (set_fill_c1, 0, r.color); break;
	case I_FILL_C2: each_sel (set_fill_c2, 0, r.color); break;
	case I_FILL_ANGLE: each_sel (set_fill_angle, v, 0); break;
	case I_FILL_ALPHA: each_sel (set_fill_alpha, v, 0); break;
	case I_LINE_C: if (sub == 1) { begin_change (); for (int i = 0; i < g_sel.n; i++) { Object *q = obj_of (g_sel[i]); if (q) q->line.type = LN_NONE; } done_change (); } else each_sel (set_line_c, 0, r.color); break;
	case I_LINE_W: each_sel (set_line_w, v, 0); break;
	case I_RADIUS: each_sel (set_radius, v, 0); break;
	case I_SHADOW: each_sel (set_shadow, v, 0); break;
	case I_CROP: each_sel (set_crop, v, 0); break;
	case I_X: each_sel (set_x, v, 0); break;
	case I_Y: each_sel (set_y, v, 0); break;
	case I_W: each_sel (set_w, v, 0); break;
	case I_H: each_sel (set_h, v, 0); break;
	case I_ROT: each_sel (set_rot, v, 0); break;
	case I_FLIP: each_sel (set_flip, v, 0); break;
	case I_ORDER: cmd_arrange (sub == 0 ? 2 : sub == 1 ? 1 : sub == 2 ? -1 : -2); break;
	case I_ALIGN: if (sub < 6) cmd_align (sub); else cmd_distribute (sub == 7); break;
	case I_TBL_HEAD: if (o && o->tbl) { begin_change (); o->tbl->header = v != 0; done_change (); notify_slides (); } break;
	case I_TBL_BAND: if (o && o->tbl) { begin_change (); o->tbl->banded = v != 0; done_change (); notify_slides (); } break;
	case I_CHART_LEGEND: if (o && o->chart) { begin_change (); o->chart->legend = v != 0; done_change (); notify_slides (); } break;
	case I_CHART_LABELS: if (o && o->chart) { begin_change (); o->chart->labels = v != 0; done_change (); notify_slides (); } break;
	case I_CHART_DATA: chart_data (o); break;
	case I_SIZE_T: { FmtChange c; c.what = FC_SIZE; c.size = (short) (v * 10); apply_format (c); break; }
	case I_COLOR: { FmtChange c; c.what = FC_COLOR; c.color = sub == 1 ? AUTO : r.color; apply_format (c); break; }
	case I_STYLE: { static const unsigned short F[6] = { CF_BOLD, CF_ITALIC, CF_UNDER, CF_STRIKE, CF_SUPER, CF_SUB }; CharFmt c = shown_format (); apply_flag (F[sub], !(c.flags & F[sub])); break; }
	case I_ALIGN_T: apply_para (pf_align, sub); break;
	case I_LEVEL: { ParaFmt p = shown_para (); apply_para (pf_level, (int) v - 1 - p.level); break; }
	case I_SPACING: apply_para (pf_spacing, (int) (v * 100 + 0.5)); break;
	case I_BEFORE: { static int sv; sv = (int) (v * 10); apply_para ([] (ParaFmt &p, int a) { p.before = (short) a; }, sv); break; }
	case I_AFTER: { static int sv; sv = (int) (v * 10); apply_para ([] (ParaFmt &p, int a) { p.after = (short) a; }, sv); break; }
	case I_ANCHOR: set_anchor (sub); break;
	case I_WRAP: each_sel (set_wrap, v, 0); break;
	case I_TR_DUR: begin_change (); s->tr.dur = (short) (v * 1000); done_change (); break;
	case I_TR_AFTER: begin_change (); s->tr.after = (int) (v * 1000); done_change (); notify_slides (); break;
	case I_TR_ALL: begin_change (); for (int i = 0; i < g_deck.slides.n; i++) g_deck.slides[i]->tr = s->tr; done_change (); notify_slides (); break;
	case I_FX_TOOLS:
		if (g_animSel < 0 || g_animSel >= s->anim.n) break;
		begin_change ();
		if (sub == 0) { s->anim.erase (g_animSel); if (g_animSel >= s->anim.n) g_animSel = s->anim.n - 1; }
		else
		{
			int to = g_animSel + (sub == 1 ? -1 : 1);
			if (to >= 0 && to < s->anim.n) { Anim t = s->anim[to]; s->anim[to] = s->anim[g_animSel]; s->anim[g_animSel] = t; g_animSel = to; }
		}
		done_change (); notify_slides ();
		break;
	case I_FX_DELAY: if (g_animSel >= 0 && g_animSel < s->anim.n) { begin_change (); s->anim[g_animSel].delay = (short) (v * 1000); done_change (); } break;
	case I_FX_DUR: if (g_animSel >= 0 && g_animSel < s->anim.n) { begin_change (); s->anim[g_animSel].dur = (short) (v * 1000); done_change (); } break;
	case I_FX_PARA: if (g_animSel >= 0 && g_animSel < s->anim.n) { begin_change (); s->anim[g_animSel].byPara = v != 0; done_change (); } break;
	case I_FX_PLAY: play_effects (); break;
	}
	build (); invalidate (true); after ();
}

// ---- the toolbars' state, the status bar ------------------------------------------------------------------------------
static void refresh ()
{
	if (!g_view) return;
	g_notes->sync ();
	Slide *s = cur_slide ();
	CharFmt f = shown_format ();
	ParaFmt p = shown_para ();
	bool text = g_edit || (g_sel.n && obj_of (g_sel[0]) && obj_of (g_sel[0])->kind != OB_PICTURE && obj_of (g_sel[0])->kind != OB_LINE);
	g_btn[wr::IC_BOLD]->setOn (text && (f.flags & CF_BOLD)); g_btn[wr::IC_ITALIC]->setOn (text && (f.flags & CF_ITALIC));
	g_btn[wr::IC_UNDER]->setOn (text && (f.flags & CF_UNDER)); g_btn[wr::IC_STRIKE]->setOn (text && (f.flags & CF_STRIKE));
	g_btn[wr::IC_SUPER]->setOn (text && (f.flags & CF_SUPER)); g_btn[wr::IC_SUB]->setOn (text && (f.flags & CF_SUB));
	g_btn[wr::IC_LEFT]->setOn (text && p.align == AL_LEFT); g_btn[wr::IC_CENTER]->setOn (text && p.align == AL_CENTER);
	g_btn[wr::IC_RIGHT]->setOn (text && p.align == AL_RIGHT); g_btn[wr::IC_JUSTIFY]->setOn (text && p.align == AL_JUSTIFY);
	g_btn[wr::IC_BULLETS]->setOn (text && p.bullet == BU_BULLET); g_btn[wr::IC_NUMBERS]->setOn (text && p.bullet == BU_NUMBER);
	Object *o = g_sel.n ? obj_of (g_sel[0]) : 0;
	g_btn[SL_ANCHOR_T]->setOn (o && o->tb.anchor == AN_TOP && text); g_btn[SL_ANCHOR_M]->setOn (o && o->tb.anchor == AN_MIDDLE && text); g_btn[SL_ANCHOR_B]->setOn (o && o->tb.anchor == AN_BOTTOM && text);
	g_btn[wr::IC_UNDO]->setDisabled (!g_undo.n); g_btn[wr::IC_REDO]->setDisabled (!g_redo.n);
	g_btn[wr::IC_CUT]->setDisabled (!g_sel.n && !has_tsel ()); g_btn[wr::IC_COPY]->setDisabled (!g_sel.n && !has_tsel ());
	g_btn[SL_TEXTBOX]->setOn (g_tool == TOOL_TEXT); g_btn[SL_SHAPES]->setOn (g_tool == TOOL_SHAPE || g_tool == TOOL_LINE || g_tool == TOOL_ARROW);
	g_fontBox->invalidate (true); g_sizeBox->invalidate (true); g_zoomBox->invalidate (true);
	// the status bar
	char l[200];
	const char *sec = s ? section_of (g_cur) : "";
	snprintf (l, sizeof l, "Slide %d of %d%s%s%s     Theme: %s%s", g_cur + 1, g_deck.slides.n, sec[0] ? "     " : "", sec, s && s->hidden ? "  (hidden)" : "", g_deck.theme.name, changed_doc () ? "     (modified)" : "");
	scpy (g_status->text, L1 (l), sizeof g_status->text);
	g_status->zoom = g_view->zoom_pct ();
	g_status->invalidate (true);
	g_sidebar->build (); g_sidebar->invalidate (true);
	g_list->invalidate (true); g_sorter->invalidate (true);
	g_view->invalidate (true);
	// the window's title: the file's name
	static char title[120];
	snprintf (title, sizeof title, "Slides \xE2\x80\x94 %s%s", g_path[0] ? base_name (g_path) : "Untitled", changed_doc () ? " *" : "");
	(void) title;
}
static void on_slides () { g_thumbs.prune (); g_list->invalidate (true); g_sorter->invalidate (true); }
static void on_status (int what)
{
	switch (what)
	{
	case 1: cmd_notes (); break; case 2: cmd_normal (); break; case 3: cmd_sorter (); break; case 4: cmd_show_here (); break;
	case 5: cmd_zoom_out (); break; case 6: cmd_zoom_in (); break; case 7: cmd_zoom_fit (); break;
	}
}
static void on_wheel_slide (int dir) { go_slide (g_cur + dir); g_list->show_cur (); after (); }
static void on_zoom () { refresh (); }

// The parts placed: the slides' column, the slide (or the sorter), the notes, the sidebar
static void relayout ()
{
	int RW = g_root->width, RH = g_root->height;
	int top = 2 * ss::TB_H + 2, bot = RH - ST_H;
	bool normal = g_viewMode == VIEW_NORMAL;
	int listW = normal ? TH_W : 0, sideW = normal ? SB_W : 0;
	g_list->hidden = !normal; g_sidebar->hidden = !normal; g_view->hidden = !normal; g_notes->hidden = !normal || !g_showNotes;
	g_sorter->hidden = normal;
	g_list->left = 0; g_list->top = top; g_list->resizeTo (TH_W, bot - top);
	g_sidebar->left = RW - SB_W; g_sidebar->top = top; g_sidebar->resizeTo (SB_W, bot - top);
	int nh = normal && g_showNotes ? NOTES_H : 0;
	g_view->left = listW; g_view->top = top; g_view->resizeTo (RW - listW - sideW, bot - top - nh);
	g_notes->left = listW; g_notes->top = bot - nh; g_notes->resizeTo (RW - listW - sideW, imax (nh, 20));
	g_sorter->left = 0; g_sorter->top = top; g_sorter->resizeTo (RW, bot - top);
	g_status->left = 0; g_status->top = bot; g_status->resizeTo (RW, ST_H);
	g_tb1->resizeTo (RW, ss::TB_H); g_tb2->resizeTo (RW, ss::TB_H);
	g_sidebar->build ();
	g_root->invalidate (true);
}

// ---- the window ----------------------------------------------------------------------------------------------------
class ShowButton : public Widget		// "Start show" with its arrow (the start: the first slide, this one, the console)
{
public:
	ShowButton () : Widget (0, 0, 118, 28), m_hot (-1) { tip = "Start the show (F5; Shift+F5: from this slide)"; }
	unsigned bgColor () override { return parent ? parent->bgColor () : C_BG; }
	void onDraw () override
	{
		canvas.clear (bgColor ());
		unsigned a = 0x3F8DA4, b = 0x2E6E80;
		if (m_hot >= 0) { a = wk_mix (a, 0xFFFFFF, 30); }
		wk_rbox (canvas, 0, 1, width, height - 2, 5, a, b);
		wk_rline (canvas, 0, 1, width, height - 2, 5, 0x1E4E5C);
		slides_icon (canvas, SL_SHOW, 4, 4, 0xFFFFFF, false);
		wk_text_l (canvas, 26, 1, height - 2, "Start show", 0xFFFFFF, 1);
		canvas.fillRect (width - 18, 6, 1, height - 12, 0x8FC0CC);
		wk_glyph (canvas, WKG_CHEV_DOWN, width - 9, height / 2, 7, 0xFFFFFF);
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		int part = mx < 0 || my < 0 || mx >= width || my >= height ? -1 : mx >= width - 18 ? 1 : 0;
		if (part != m_hot) { m_hot = part; invalidate (true); }
		if (bl && part >= 0 && !pressed) pressed = true;
		else if (!bl && pressed)
		{
			pressed = false;
			if (part == 0) cmd_show_start ();
			else if (part == 1)
			{
				int x = 0, y = 0; for (Widget *w = this; w && w->parent; w = w->parent) { x += w->left; y += w->top; }
				PopupMenu m (x, y + height + 2);
				m.add ("From the Beginning", 1, true, "F5"); m.add ("From This Slide", 2, true, "Sh+F5"); m.add ("Presenter View", 3, true);
				int r = m.run ();
				if (r == 1) cmd_show_start (); else if (r == 2) cmd_show_here (); else if (r == 3) cmd_presenter ();
			}
		}
		return part >= 0;
	}
	int m_hot;
};

class SlidesRoot : public Root
{
public:
	SlidesRoot () : Root (W, H, "Slides") {}
	void onTick () override
	{
		if (g_view) g_view->tick ();
		if (g_notes && !g_notes->hidden) g_notes->sync ();
		static unsigned last; unsigned t = kapi_get_ticks ();
		if (t - last > 25 && g_notes) { last = t; }
	}
	void onResized () override { relayout (); }
	bool onKey (long k) override
	{
		unsigned m = kapi_get_modifiers ();
		if (k == KEY_F1 + 4) { if (m & MOD_SHIFT) cmd_show_here (); else cmd_show_start (); return true; }
		return false;
	}
	void onDrop (int, int, int type, const char *data, int len, unsigned) override
	{
		if (type == DND_TEXT) { unsigned *u = (unsigned *) malloc (sizeof (unsigned) * (len + 1)); int m = 0; for (int i = 0; i < len; ) { int l; u[m++] = ss::u8_dec (data + i, len - i, &l); i += l > 0 ? l : 1; } if (g_edit) g_view->type (u, m); free (u); after (); return; }
		char path[200];
		if (type != DND_FILES || !doc_first_path (data, path, sizeof path)) return;
		if (has_ext (path, ".odp"))
		{
			if (!doc_confirm (g_path[0] ? base_name (g_path) : "Untitled", changed_doc (), save_for_guard)) return;
			load_path (path); return;
		}
		if (img_is_image_name (path))
		{
			unsigned n = 0; unsigned char *b = read_all (path, &n);
			if (b) { Object *o = make_picture (base_name (path), b, n); free (b); if (o) add_object (o); after (); }
		}
	}
};

static ToolButton *button (ToolBar *tb, int ic, const char *tip, void (*cb) (), int gap = 1, bool split = false)
{
	ToolButton *b = new ToolButton (ic, tip, cb, split);
	tb->add (b, gap);
	if (ic >= 0 && ic < 400) g_btn[ic] = b;
	return b;
}
static void now_hook (int *y, int *mo, int *d, int *h, int *mi, int *s)
{
	*y = 2026; *mo = 1; *d = 1; *h = *mi = *s = 0;
	kapi_get_datetime (y, mo, d, h, mi, s);
}

int main (void)
{
	char args[200];
	int an = kapi_get_args (args, sizeof args);
	if (an < 0) an = 0;
	args[an < (int) sizeof args ? an : (int) sizeof args - 1] = 0;
	SlidesRoot root; g_root = &root;
	root.attach ();
	wtk::init ();
	if (!fnt::init ()) { wk_messagebox ("Slides", "No TrueType fonts in SD:/res/fonts: Slides cannot draw its slides.", MB_OK); return 1; }
	ss::g_appIcon = slides_icon;
	ss::make_palettes ();
	Show::g_now = now_hook;
	deck_new (g_deck);

	// the toolbars
	g_tb1 = new ToolBar (0, 0, W); g_tb2 = new ToolBar (0, ss::TB_H, W);
	root.addChild (g_tb1); root.addChild (g_tb2);
	g_tb1->anchor = g_tb2->anchor = ANCHOR_LEFT | ANCHOR_TOP | ANCHOR_RIGHT;
	button (g_tb1, wr::IC_NEW, "New presentation (Ctrl+N)", cmd_new);
	button (g_tb1, wr::IC_OPEN, "Open... (Ctrl+O)", cmd_open);
	button (g_tb1, wr::IC_SAVE, "Save (Ctrl+S)", cmd_save);
	g_tb1->sep ();
	button (g_tb1, wr::IC_UNDO, "Undo (Ctrl+Z)", ui_undo);
	button (g_tb1, wr::IC_REDO, "Redo (Ctrl+Y)", ui_redo);
	g_tb1->sep ();
	button (g_tb1, wr::IC_CUT, "Cut (Ctrl+X)", cmd_cut);
	button (g_tb1, wr::IC_COPY, "Copy (Ctrl+C)", cmd_copy);
	button (g_tb1, wr::IC_PASTE, "Paste (Ctrl+V)", cmd_paste);
	g_tb1->sep ();
	ToolButton *ns = button (g_tb1, SL_NEWSLIDE, "New slide (Ctrl+M; its arrow: with a layout)", cmd_new_slide_btn, 1, true); ns->arrow = drop_new_slide;
	button (g_tb1, SL_LAYOUT, "The slide's layout", cmd_layout);
	button (g_tb1, SL_DUPSLIDE, "Duplicate the slide", cmd_dup_slide);
	button (g_tb1, SL_DELSLIDE, "Delete the slide", cmd_del_slide);
	g_tb1->sep ();
	button (g_tb1, SL_TEXTBOX, "Text box (draw it on the slide)", cmd_textbox);
	button (g_tb1, SL_PICTURE, "Insert a picture...", cmd_picture);
	button (g_tb1, SL_TABLE, "Insert a table...", cmd_table);
	ToolButton *ch = button (g_tb1, SL_CHART, "Insert a chart (its arrow: its type)", cmd_chart, 1, true); ch->arrow = drop_chart;
	ToolButton *sh = button (g_tb1, SL_SHAPES, "Shapes (its arrow: the gallery)", cmd_shape, 1, true); sh->arrow = drop_shapes;
	button (g_tb1, SL_ARROW, "Arrow (draw it)", cmd_arrow);
	g_tb1->sep ();
	button (g_tb1, wr::IC_ZOOMOUT, "Zoom out", cmd_zoom_out);
	g_zoomBox = new PickBox (96, "Zoom", zoom_value, pick_zoom);
	g_tb1->add (g_zoomBox, 2);
	button (g_tb1, wr::IC_ZOOMIN, "Zoom in", cmd_zoom_in, 2);
	ShowButton *sb = new ShowButton ();
	sb->left = W - sb->width - 8; sb->top = (ss::TB_H - sb->height) / 2; sb->anchor = ANCHOR_TOP | ANCHOR_RIGHT;
	g_tb1->addChild (sb);

	g_fontBox = new PickBox (160, "Font", font_value, pick_font);
	g_tb2->add (g_fontBox, 0);
	g_sizeBox = new PickBox (58, "Size", size_value, pick_size);
	g_tb2->add (g_sizeBox, 6);
	g_tb2->sep ();
	button (g_tb2, wr::IC_BOLD, "Bold (Ctrl+B)", cmd_bold);
	button (g_tb2, wr::IC_ITALIC, "Italic (Ctrl+I)", cmd_italic);
	button (g_tb2, wr::IC_UNDER, "Underline (Ctrl+U)", cmd_under);
	button (g_tb2, wr::IC_STRIKE, "Strikethrough", cmd_strike);
	button (g_tb2, wr::IC_SUPER, "Superscript", cmd_super, 4);
	button (g_tb2, wr::IC_SUB, "Subscript", cmd_sub);
	g_tb2->sep ();
	ToolButton *tc = button (g_tb2, wr::IC_COLOR, "Text colour", cmd_color, 1, true); tc->arrow = drop_color; tc->setBar (g_textColor);
	g_tb2->sep ();
	button (g_tb2, wr::IC_LEFT, "Align left (Ctrl+L)", cmd_left);
	button (g_tb2, wr::IC_CENTER, "Centre (Ctrl+E)", cmd_center);
	button (g_tb2, wr::IC_RIGHT, "Align right (Ctrl+R)", cmd_right);
	button (g_tb2, wr::IC_JUSTIFY, "Justify (Ctrl+J)", cmd_justify);
	button (g_tb2, SL_ANCHOR_T, "Text at the top", cmd_anchor_t, 4);
	button (g_tb2, SL_ANCHOR_M, "Text in the middle", cmd_anchor_m);
	button (g_tb2, SL_ANCHOR_B, "Text at the bottom", cmd_anchor_b);
	g_tb2->sep ();
	button (g_tb2, wr::IC_BULLETS, "Bullets", cmd_bullets);
	button (g_tb2, wr::IC_NUMBERS, "Numbering", cmd_numbers);
	button (g_tb2, wr::IC_OUTDENT, "Decrease the level (Shift+Tab)", cmd_outdent, 4);
	button (g_tb2, wr::IC_INDENT, "Increase the level (Tab)", cmd_indent);
	button (g_tb2, SL_SPACING, "Line spacing", cmd_spacing);
	g_tb2->sep ();
	button (g_tb2, SL_FRONT, "Bring to front", cmd_front);
	button (g_tb2, SL_BACK, "Send to back", cmd_back);

	// the parts
	int top = 2 * ss::TB_H + 2;
	g_list = new SlideList (0, top, TH_W, H - top - ST_H);
	g_view = new SlideView (TH_W, top, W - TH_W - SB_W, H - top - ST_H - NOTES_H);
	g_notes = new NotesPane (TH_W, H - ST_H - NOTES_H, W - TH_W - SB_W, NOTES_H);
	g_sidebar = new Sidebar (W - SB_W, top, SB_W, H - top - ST_H);
	g_sorter = new Sorter (0, top, W, H - top - ST_H);
	g_status = new StatusBar (0, H - ST_H, W);
	root.addChild (g_list); root.addChild (g_notes); root.addChild (g_sidebar); root.addChild (g_sorter); root.addChild (g_status);
	root.addChild (g_view);			// (last: a drag ended over another part still reaches it)
	g_sorter->hidden = true;
	root.setResizable (true);
	root.setBg (C_BG);

	g_onChange = refresh; g_onSlides = on_slides;
	g_onContext = view_menu; g_onSlideMenu = slide_menu; g_onPictureWanted = picture_into;
	g_onStatus = on_status; g_onOpenSlide = open_slide; g_onTool = refresh;
	SlideView::g_onWheelSlide = on_wheel_slide; SlideView::g_onZoom = on_zoom;
	g_onPlayEffects = play_effects; g_onChartData = chart_data;

	static Menu menu;
	menu.menu ("File");
	menu.item ("New", "^N", WK_CTRL ('N'), cmd_new);
	menu.item ("Open...", "^O", WK_CTRL ('O'), cmd_open);
	menu.separator ();
	menu.item ("Save", "^S", WK_CTRL ('S'), cmd_save);
	menu.item ("Save As...", "", 0, cmd_save_as);
	menu.item ("Export as PDF...", "", 0, cmd_export_pdf);
	menu.item ("Export Slide as PNG...", "", 0, cmd_export_png);
	menu.menu ("Edit");
	menu.item ("Undo", "^Z", WK_CTRL ('Z'), ui_undo);
	menu.item ("Redo", "^Y", WK_CTRL ('Y'), ui_redo);
	menu.separator ();
	menu.item ("Cut", "^X", WK_CTRL ('X'), cmd_cut);
	menu.item ("Copy", "^C", WK_CTRL ('C'), cmd_copy);
	menu.item ("Paste", "^V", WK_CTRL ('V'), cmd_paste);
	menu.item ("Duplicate", "^D", WK_CTRL ('D'), cmd_duplicate);
	menu.item ("Delete", "Del", 0, cmd_delete);
	menu.item ("Select All", "^A", WK_CTRL ('A'), cmd_select_all);
	menu.menu ("View");
	menu.item ("Normal", "", 0, cmd_normal);
	menu.item ("Slide Sorter", "", 0, cmd_sorter);
	menu.item ("Notes", "", 0, cmd_notes);
	menu.separator ();
	menu.item ("Fit the Window", "", 0, cmd_zoom_fit);
	menu.item ("Zoom In", "", 0, cmd_zoom_in);
	menu.item ("Zoom Out", "", 0, cmd_zoom_out);
	menu.menu ("Insert");
	menu.item ("New Slide", "^M", WK_CTRL ('M'), cmd_new_slide_btn);
	menu.item ("Duplicate Slide", "", 0, cmd_dup_slide);
	menu.separator ();
	menu.item ("Text Box", "", 0, cmd_textbox);
	menu.item ("Picture...", "", 0, cmd_picture);
	menu.item ("Table...", "", 0, cmd_table);
	menu.item ("Chart...", "", 0, cmd_chart);
	menu.item ("Arrow", "", 0, cmd_arrow);
	menu.item ("Line", "", 0, cmd_line);
	menu.separator ();
	menu.item ("Header and Footer...", "", 0, cmd_footer);
	menu.menu ("Format");
	menu.item ("Bold", "^B", WK_CTRL ('B'), cmd_bold);
	menu.item ("Italic", "^I", 0, cmd_italic);
	menu.item ("Underline", "^U", WK_CTRL ('U'), cmd_under);
	menu.separator ();
	menu.item ("Bullets", "", 0, cmd_bullets);
	menu.item ("Numbering", "", 0, cmd_numbers);
	menu.separator ();
	menu.item ("Bring to Front", "", 0, cmd_front);
	menu.item ("Bring Forward", "", 0, cmd_forward);
	menu.item ("Send Backward", "", 0, cmd_backward);
	menu.item ("Send to Back", "", 0, cmd_back);
	menu.menu ("Slide");
	menu.item ("Layout...", "", 0, cmd_layout);
	menu.item ("Hide / Show Slide", "", 0, cmd_hide);
	menu.item ("Add Section...", "", 0, cmd_add_section);
	menu.item ("Remove Section", "", 0, cmd_remove_section);
	menu.menu ("Show");
	menu.item ("From the Beginning", "F5", 0, cmd_show_start);
	menu.item ("From This Slide", "Sh+F5", 0, cmd_show_here);
	menu.item ("Presenter View", "", 0, cmd_presenter);
	menu.publish ();

	relayout ();
	// a file named on the command line, else the one kept at the last close
	if (an > 0 && args[0]) load_path (args);
	else
	{
		unsigned n = 0; unsigned char *b = read_all (RECOVER, &n);
		bool recovered = false;
		if (b)
		{
			if (n > 0 && wk_messagebox ("Slides", "Slides was closed with unsaved changes. Open the recovered presentation?", MB_YESNO) == 1 && odp_load (g_deck, b, n))
			{
				unsigned nn = 0; unsigned char *nb = read_all (RECOVER_NAME, &nn);
				g_path[0] = 0;
				if (nb) { int k = imin ((int) nn, 199); memcpy (g_path, nb, k); g_path[k] = 0; free (nb); }
				deck_loaded ();
				g_saved = g_deck.changes + 1;
				recovered = true;
			}
			free (b);
			kapi_remove (RECOVER); kapi_remove (RECOVER_NAME);
		}
		if (!recovered) deck_loaded ();
	}
	refresh ();
	g_view->setFocus ();
	root.run ();

	if (changed_doc ())
	{
		unsigned n = 0; unsigned char *b = odp_save (g_deck, &n);
		if (b) { kapi_save_file (RECOVER, b, n); delete[] b; }
		if (g_path[0]) kapi_save_file (RECOVER_NAME, g_path, (unsigned) strlen (g_path));
	}
	return 0;
}
