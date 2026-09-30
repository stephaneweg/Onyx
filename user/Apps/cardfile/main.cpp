//
// cardfile -- Onyx's card file: a small database in the way of Microsoft Access, without SQL. One file
// (.card) holds one FORM -- a title, a description, its fields: each a display name, a column name and
// a type (one-line text, multi-line text, integer, decimal number with its decimals, date, colour,
// yes / no, a choice list with its choices) -- and its RECORDS. Three views, from the toolbar's switch
// or the View menu (F5, F6, F7):
//   * Form (formview.h): a record at a time, on an index card: the fields' names and their editors; the
//     navigator at the foot (first, previous, "Record 3 of 12", next, last, a new one);
//   * List (listview.h): the records in a grid (wtk's DataGrid), a column per field; a click on a title
//     sorts, again the other way; a double click opens the record in the form;
//   * Design (designview.h): the form itself -- its fields added, removed, moved, named, typed.
// The search box (Ctrl+F) keeps the records holding every word typed (case and accents ignored); the
// sort and the search order both views. Undo / Redo (the whole document at each step: record edits,
// deletions, the form's design), unsaved changes asked about (New, Open, a file dropped), a document
// closed with unsaved changes kept in SD:/apps/cardfile.app/recovered.card and offered at the next start.
// CSV: File > Export as CSV... (the records shown, as shown), Import CSV... (a new form: the first line
// names the fields, each column's type guessed from its values). "cardfile SD:/docs/x.card" opens a file;
// so does a file dropped on the window, or a double click in the File Viewer (fileassoc.ini: card).
// Started without one, Cardfile opens the form it had last (SD:/apps/cardfile.app/last.txt), else a
// new form in the Design view.
//
// THE FILE (.card): text in Latin-1 (as Onyx writes), one form and its records --
//
//   # Onyx Cardfile ...                    '#' or ';' starts a comment (outside [records])
//   [form]
//   version     = 1
//   title       = Contacts
//   description = Friends, family and work
//   sort        = name                     the views' order: a column (order = descending)
//   [field]                                one section a field, in the form's order
//   column      = birthday                 the column's name: letters, digits, _ and -
//   label       = Birthday                 the display name
//   type        = date                     text, multiline, integer, decimal, date, colour, yesno, choice
//   decimals    = 2                        (decimal) 0 to 6
//   choice      = Family                   (choice) an option a line, in the list's order
//   [records]
//   name<TAB>birthday<TAB>...              the header: the columns (in any order)
//   Alice Martin<TAB>1990-03-14<TAB>...    a record a line, a tab between its values
//
// In a value \t, \n, \\ stand for a tab, a line break, a backslash (and "\[" for a '[' starting a line).
// A value as stored: text as typed; integer 42; decimal 12.50 (the field's decimals); date 2026-09-29
// (ISO -- shown 29/09/2026); colour #3366CC; yes / no: "yes", or empty; choice: the option's text. Read
// leniently: keys in any case, the sections in any order, a column missing from the header (empty
// values) or unknown (ignored), no [field] at all (the header's columns become text fields), a value
// that is not one of its type (kept as it is: the form asks for a valid one when it is edited).
//
#include "wtk/wtk.h"
#include "docguard.h"
#include "model.h"
#include "widgets.h"
#include "app.h"
#include "formview.h"
#include "listview.h"
#include "designview.h"

using namespace wtk;

namespace cf {

#define W 900
#define H 600
static const char *RECOVER = "SD:/apps/cardfile.app/recovered.card";
static const char *RECOVER_NAME = "SD:/apps/cardfile.app/recovered.txt";
static const char *LAST = "SD:/apps/cardfile.app/last.txt";		// the form opened or saved last

static Root       *g_root;
static FormView   *g_form;
static ListView   *g_list;
static DesignView *g_design;
static NavBar     *g_nav;
static ViewSwitch *g_switch;
static SearchBox  *g_searchBox;
static ToolButton *g_bUndo, *g_bRedo, *g_bNewRec, *g_bDupRec, *g_bDelRec;
static unsigned    g_state, g_saved, g_seq;		// the document's state (a new one at each change), the one saved
static char        g_msg[128]; static unsigned g_msgT;	// a word in the status bar, since when
static int         g_pinUndo = -1;			// the undo steps when the new record was made

static bool modified () { return g_state != g_saved; }
static void refresh ();

// ---- the dialogs ------------------------------------------------------------------------------------------------
// A question or a message: the text wrapped to the box, an icon (0 information, 1 a question, 2 a warning).
class AskBox : public Modal
{
public:
	enum { MAXL = 10 };
	AskBox (const char *title, const char *text, int buttons, int icon) : Modal (440, 120), m_title (title), m_icon (icon), m_n (0), m_def (1), m_cancel (0)
	{
		int maxc = imin (95, (width - 70 - 22) / wk_fw ());
		const char *p = text;
		while (*p && m_n < MAXL)
		{
			int n = 0, cut = -1;
			while (p[n] && p[n] != '\n' && n < maxc) { if (p[n] == ' ') cut = n; n++; }
			if (p[n] && p[n] != '\n' && p[n] != ' ' && cut > 0) n = cut;
			for (int i = 0; i < n; i++) m_line[m_n][i] = p[i];
			m_line[m_n][n] = '\0';
			m_n++;
			p += n;
			if (*p == '\n' || *p == ' ') p++;
		}
		int h = titleH () + 22 + imax (m_n * 20, 36) + 22 + 44;
		resizeTo (width, h);
		Root *r = Root::current ();
		if (r) { left = (r->width - width) / 2; top = imax (0, (r->height - height) / 2); }
		int by = height - 42;
		if (buttons == MB_YESNOCANCEL) { button (width - 282, by, "Yes", 1); button (width - 192, by, "No", 2); button (width - 102, by, "Cancel", 0); }
		else if (buttons == MB_YESNO) { button (width - 192, by, "Yes", 1); button (width - 102, by, "No", 0); }
		else if (buttons == MB_OKCANCEL) { button (width - 192, by, "OK", 1); button (width - 102, by, "Cancel", 0); }
		else { button (width - 102, by, "OK", 1); m_cancel = 1; }
	}
	void button (int x, int y, const char *s, int tag) { Button *b = new Button (x, y, 88, 30, s, act); b->tag = tag; addChild (b); }
	static void act (Widget &w) { ((Modal *) w.parent)->onButton (w.tag); }
	void onButton (int tag) override { close (tag); }
	bool onKey (long k) override
	{
		if (k == KEY_ENTER) { close (m_def); return true; }
		if (k == 27) { close (m_cancel); return true; }
		return true;
	}
	void onDraw () override
	{
		drawBox (m_title);
		int cx = 20, cy = titleH () + 22;
		unsigned c = m_icon == 2 ? 0x00D8902A : C_ACCENT;
		wk_rbox (canvas, cx, cy, 32, 32, 16, wk_tone (c, 156), wk_tone (c, 112));
		wk_rline (canvas, cx, cy, 32, 32, 16, wk_tone (c, 80), 160);
		wk_text_c (canvas, cx, cy, 32, 32, m_icon == 2 ? "!" : m_icon == 1 ? "?" : "i", 0x00FFFFFF, 2);
		int y = titleH () + 22 + (m_n == 1 ? 8 : 0);
		for (int i = 0; i < m_n; i++) canvas.text (68, y + i * 20, m_line[i], C_TEXT);
	}
private:
	const char *m_title; int m_icon; char m_line[MAXL][96]; int m_n, m_def, m_cancel;
};
static int ask (const char *title, const char *text, int buttons, int icon)
{
	AskBox a (title, text, buttons, icon);
	return a.run ();
}

// Go to Record: its number.
class GotoBox : public Modal
{
public:
	LineEdit *e; int n;
	GotoBox (int cur, int n_) : Modal (320, titleH () + 112), n (n_)
	{
		Root *r = Root::current ();
		if (r) { left = (r->width - width) / 2; top = imax (0, (r->height - height) / 2); }
		e = new LineEdit (96, titleH () + 20, 90);
		e->accept = accept_int; e->rightAlign = true; e->onEnter = ok;
		char b[16]; itoa10 (cur, b); e->setText (b); e->selectAll ();
		addChild (e);
		Button *bt = new Button (width - 192, height - 42, 88, 30, "Go", button); bt->tag = 1; addChild (bt);
		bt = new Button (width - 102, height - 42, 88, 30, "Cancel", button); bt->tag = 0; addChild (bt);
	}
	static void ok (Widget &w) { ((Modal *) w.parent)->close (1); }
	static void button (Widget &w) { ((Modal *) w.parent)->close (w.tag); }
	bool onKey (long k) override { if (k == 27) { close (0); return true; } if (k == KEY_ENTER) { close (1); return true; } return false; }
	void onDraw () override
	{
		drawBox ("Go to Record");
		wk_text_l (canvas, 20, titleH () + 20, ED_H, "Record", C_TEXT);
		char t[32] = "of "; scat_num (t, n, sizeof t);
		wk_text_l (canvas, 198, titleH () + 20, ED_H, t, C_TEXT);
	}
};

// Mail Merge: Writer's letter (its fields the form's) filled with this record, or each record shown --
// one document in Writer (each letter on a new page), or each written in a folder (named after a
// field, or numbered). Writer does it: "writer --merge JOB" (Writer's merge.h), JOB and the records
// written in SD:/apps/cardfile.app/.
static const char *MERGE_DATA = "SD:/apps/cardfile.app/merge.card", *MERGE_JOB = "SD:/apps/cardfile.app/merge.job";
static const char *const MM_FMT[4] = { "As the letter", "RTF (.rtf)", "Word (.docx)", "OpenDocument (.odt)" };
class MergeBox : public Modal
{
public:
	LineEdit *letter, *folder;
	RadioButton *one, *all, *open, *files;
	Dropdown *naming, *format;
	const char *opts[MAXF + 1]; char optBuf[MAXF][LABEL_MAX + 16];
	MergeBox (const char *recName, int shown) : Modal (580, titleH () + 350)
	{
		Root *r = Root::current ();
		if (r) { left = (r->width - width) / 2; top = imax (0, (r->height - height) / 2); }
		int y = titleH () + 18;
		letter = new LineEdit (120, y, 330); letter->setText (g_doc.merge); letter->placeholder = "Writer's letter (.rtf, .docx, .odt)"; addChild (letter);
		button (460, y - 1, 104, "Choose...", 2);
		y += 46;
		char t[64] = "This record: "; scat (t, recName[0] ? recName : "(empty)", sizeof t);
		one = new RadioButton (120, y, 440, 24, t, 1, true, 0, C_FACE); addChild (one);
		char u[64] = "All the records shown ("; scat_num (u, shown, sizeof u); scat (u, ")", sizeof u);
		all = new RadioButton (120, y + 28, 440, 24, u, 1, false, 0, C_FACE); addChild (all);
		y += 74;
		open = new RadioButton (120, y, 440, 24, "One document in Writer, each letter on a new page", 2, true, onKind, C_FACE); addChild (open);
		files = new RadioButton (120, y + 28, 150, 24, "Files, in:", 2, false, onKind, C_FACE); addChild (files);
		folder = new LineEdit (270, y + 28, 294); folder->setText ("SD:/docs/Letters"); addChild (folder);
		y += 66;
		opts[0] = "Numbered (letter-1, letter-2...)";
		int n = 1;
		for (int k = 0; k < g_doc.nf && n <= MAXF; k++) { scpy (optBuf[n - 1], "Named after: ", sizeof optBuf[0]); scat (optBuf[n - 1], g_doc.f[k].label, sizeof optBuf[0]); opts[n] = optBuf[n - 1]; n++; }
		naming = new Dropdown (270, y, 294, 26, opts, n, n > 1 ? 1 : 0, 0); addChild (naming);
		format = new Dropdown (270, y + 34, 294, 26, MM_FMT, 4, 0, 0); addChild (format);
		button (width - 212, height - 44, 104, "Merge", 1);
		button (width - 102, height - 44, 88, "Cancel", 0);
		onKind (*open);
	}
	void button (int x, int y, int w, const char *s, int tag) { Button *b = new Button (x, y, w, 30, s, act); b->tag = tag; addChild (b); }
	static void act (Widget &w) { ((Modal *) w.parent)->onButton (w.tag); }
	static void onKind (Widget &w)
	{
		MergeBox *m = (MergeBox *) w.parent;
		bool f = m->files->checked;
		m->folder->disabled = !f; m->naming->disabled = !f; m->format->disabled = !f;
		m->invalidate (true);
	}
	void onButton (int tag) override
	{
		if (tag == 2)
		{
			char path[200];
			if (wk_file_open (path, sizeof path, "SD:/docs")) letter->setText (path);
			return;
		}
		if (tag == 1 && !letter->text ()[0]) { ask ("Mail Merge", "Choose the letter first: a Writer document whose fields are the form's (Writer: Tools > Mail Merge).", MB_OK, 1); return; }
		close (tag);
	}
	bool onKey (long k) override { if (k == 27) { close (0); return true; } return false; }
	void onDraw () override
	{
		drawBox ("Mail Merge");
		int y = titleH () + 18;
		wk_text_l (canvas, 20, y, ED_H, "Letter:", C_TEXT);
		wk_text_l (canvas, 20, y + 46, 24, "Records:", C_TEXT);
		wk_text_l (canvas, 20, y + 120, 24, "Documents:", C_TEXT);
		wk_text_l (canvas, 150, y + 186, 26, "Their names:", files->checked ? C_TEXT : wk_mix (C_FACE, C_TEXT, 120));
		wk_text_l (canvas, 150, y + 220, 26, "Their format:", files->checked ? C_TEXT : wk_mix (C_FACE, C_TEXT, 120));
		wk_text_l (canvas, 20, height - 78, 20, "Writer fills the letter's fields with the records' values.", wk_mix (C_FACE, C_TEXT, 170));
	}
};

// ---- undo: the whole document kept before each change --------------------------------------------------------------
struct Snap { char *text; int len; unsigned state; int cur, fsel; };
enum { MAXUNDO = 100, UNDO_BYTES = 24 << 20 };
static Snap g_undo[MAXUNDO], g_redo[MAXUNDO];
static int  g_nundo, g_nredo, g_lastKey = -1;

static void snap_take (Snap &s)
{
	Out o; doc_write (g_doc, o);
	s.len = o.n; s.text = o.take (); s.state = g_state; s.cur = g_cur; s.fsel = g_fsel;
	if (!s.text) { s.text = new char[1]; s.text[0] = '\0'; }
}
static void snap_free (Snap &s) { delete [] s.text; s.text = 0; s.len = 0; }
static void drop_oldest (Snap *st, int &n) { if (!n) return; snap_free (st[0]); for (int i = 1; i < n; i++) st[i - 1] = st[i]; n--; }
static long undo_bytes () { long b = 0; for (int i = 0; i < g_nundo; i++) b += g_undo[i].len; for (int i = 0; i < g_nredo; i++) b += g_redo[i].len; return b; }
static void clear_history ()
{
	for (int i = 0; i < g_nundo; i++) snap_free (g_undo[i]);
	for (int i = 0; i < g_nredo; i++) snap_free (g_redo[i]);
	g_nundo = g_nredo = 0; g_lastKey = -1; g_pinUndo = -1;
}
static void undo_mark (int key)
{
	if (!(key >= 0 && key == g_lastKey))
	{
		if (g_nundo == MAXUNDO) drop_oldest (g_undo, g_nundo);
		snap_take (g_undo[g_nundo++]);
		for (int i = 0; i < g_nredo; i++) snap_free (g_redo[i]);
		g_nredo = 0;
		while (undo_bytes () > UNDO_BYTES && g_nundo > 1) { drop_oldest (g_undo, g_nundo); if (g_pinUndo > 0) g_pinUndo--; }
	}
	g_lastKey = key;
	g_state = ++g_seq;
}
static void undo_drop_last () { if (g_nundo) { g_state = g_undo[g_nundo - 1].state; snap_free (g_undo[--g_nundo]); } g_lastKey = -1; }
static void undo_break () { g_lastKey = -1; }

// ---- the order, the record shown ----------------------------------------------------------------------------------
static int cur_pos () { for (int i = 0; i < g_nord; i++) if (g_ord[i] == g_cur) return i; return -1; }

static void rebuild_order ()
{
	if (g_ordCap < g_doc.nr + 1) { delete [] g_ord; g_ordCap = g_doc.nr + 256; g_ord = new int[g_ordCap]; }
	if (g_pin >= g_doc.nr) g_pin = -1;
	if (g_cur >= g_doc.nr) g_cur = -1;
	g_nord = build_order (g_doc, g_search, g_pin, g_ord);
	if (cur_pos () < 0) g_cur = g_nord ? g_ord[0] : -1;
}

static void refresh_views ()
{
	switch (g_view)
	{
	case V_FORM: g_form->load (); break;
	case V_LIST: g_list->sync (); break;
	case V_DESIGN: g_design->sync (); break;
	}
}

static void doc_changed (bool fields)
{
	if (fields) g_fieldsVer++;
	rebuild_order ();
	refresh_views ();
	refresh ();
}

// A new record left without a value (the one New Record made): dropped, as a database does -- and its
// undo step with it (nothing happened).
static bool drop_if_untouched (int rec)
{
	if (rec < 0 || rec != g_pin || rec >= g_doc.nr || !rec_empty (g_doc, rec)) return false;
	doc_del_record (g_doc, rec);
	if (g_cur > rec) g_cur--;
	else if (g_cur == rec) g_cur = -1;
	g_pin = -1;
	if (g_nundo == g_pinUndo && g_pinUndo > 0) undo_drop_last ();
	return true;
}

static void restore (Snap &s)
{
	char sortCol[COL_MAX] = ""; bool desc = g_doc.sortDesc;
	if (g_doc.sort >= 0) scpy (sortCol, g_doc.f[g_doc.sort].column, sizeof sortCol);	// (the order is the view's: kept)
	doc_read (g_doc, s.text, s.len, 0);
	g_doc.sort = sortCol[0] ? find_column (g_doc, sortCol) : -1;
	g_doc.sortDesc = g_doc.sort >= 0 && desc;
	g_state = s.state; g_cur = s.cur; g_fsel = s.fsel; g_pin = -1; g_lastKey = -1; g_pinUndo = -1;
	doc_changed (true);
}
static bool commit_edits () { return g_view != V_FORM || g_form->commit (false); }
static void cmd_undo ()
{
	if (!commit_edits ()) return;
	if (!g_nundo) { status ("Nothing to undo"); return; }
	if (g_nredo == MAXUNDO) drop_oldest (g_redo, g_nredo);
	snap_take (g_redo[g_nredo++]);
	Snap s = g_undo[--g_nundo];
	restore (s);
	snap_free (s);
	status ("Undone");
}
static void cmd_redo ()
{
	if (!commit_edits ()) return;
	if (!g_nredo) { status ("Nothing to redo"); return; }
	if (g_nundo == MAXUNDO) drop_oldest (g_undo, g_nundo);
	snap_take (g_undo[g_nundo++]);
	Snap s = g_redo[--g_nredo];
	restore (s);
	snap_free (s);
	status ("Redone");
}

// ---- the status ---------------------------------------------------------------------------------------------------
static void status (const char *msg) { scpy (g_msg, msg, sizeof g_msg); g_msgT = kapi_get_ticks (); refresh (); }

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
	for (int i = 0; i < e; i++) if (fold ((unsigned char) p[n - e + i]) != fold ((unsigned char) ext[i])) return false;
	return true;
}

static void refresh ()
{
	if (!g_nav) return;
	char pos[48], info[128], file[128];
	int p = cur_pos ();
	if (!g_nord) scpy (pos, g_doc.nr ? "No match" : "No record", sizeof pos);
	else { scpy (pos, "Record ", sizeof pos); scat_num (pos, p + 1, sizeof pos); scat (pos, " of ", sizeof pos); scat_num (pos, g_nord, sizeof pos); }
	info[0] = '\0';
	if (g_msg[0] && kapi_get_ticks () - g_msgT < 400) scpy (info, g_msg, sizeof info);
	else if (g_view == V_DESIGN)
	{
		scat_num (info, g_doc.nf, sizeof info); scat (info, g_doc.nf == 1 ? " field, " : " fields, ", sizeof info);
		scat_num (info, g_doc.nr, sizeof info); scat (info, g_doc.nr == 1 ? " record" : " records", sizeof info);
	}
	else
	{
		if (g_search[0]) { scat_num (info, g_nord, sizeof info); scat (info, " of ", sizeof info); scat_num (info, g_doc.nr, sizeof info); scat (info, " records match", sizeof info); }
		else { scat_num (info, g_doc.nr, sizeof info); scat (info, g_doc.nr == 1 ? " record" : " records", sizeof info); }
		if (g_doc.sort >= 0) { scat (info, ", sorted by ", sizeof info); scat (info, g_doc.f[g_doc.sort].label, sizeof info); if (g_doc.sortDesc) scat (info, " (descending)", sizeof info); }
	}
	scpy (file, g_path[0] ? base_name (g_path) : "Not saved yet", sizeof file);
	g_nav->set (g_view != V_DESIGN, pos, p > 0, p >= 0 && p < g_nord - 1, info, file, modified (), g_view == V_FORM && g_form->dirty ());
	g_bUndo->setDisabled (!g_nundo);
	g_bRedo->setDisabled (!g_nredo);
	g_bNewRec->setDisabled (!g_doc.nf);
	g_bDupRec->setDisabled (g_cur < 0 || g_view == V_DESIGN);
	g_bDelRec->setDisabled (g_cur < 0 || g_view == V_DESIGN);
	g_switch->set (g_view);
}

// ---- the views ------------------------------------------------------------------------------------------------------
static void focus_view ()
{
	if (g_view == V_FORM) { if (g_form->focusedField () < 0) g_form->focusFirst (); }
	else if (g_view == V_LIST) g_list->grid->setFocus ();
	else g_design->list->setFocus ();
}
static void show_view (int v)
{
	if (v == g_view) { focus_view (); return; }
	if (g_view == V_FORM)
	{
		if (!g_form->commit (false)) return;
		drop_if_untouched (g_cur);
	}
	undo_break ();
	g_view = v;
	g_form->hidden = v != V_FORM; g_list->hidden = v != V_LIST; g_design->hidden = v != V_DESIGN;
	rebuild_order ();
	refresh_views ();
	refresh ();
	focus_view ();
	g_root->invalidate (true);
}
static void cmd_view_form () { show_view (V_FORM); }
static void cmd_view_list () { show_view (V_LIST); }
static void cmd_view_design () { show_view (V_DESIGN); }
static void on_switch (int v) { show_view (v); }

// Another record: the record at place `pos` of the order (the edits of the one shown kept first).
static void goto_pos (int pos)
{
	if (!g_nord) return;
	pos = iclamp (pos, 0, g_nord - 1);
	if (pos == cur_pos ()) return;
	int target = g_ord[pos];
	if (g_view == V_FORM && !g_form->commit (false)) return;
	int was = g_cur;
	g_cur = target;
	if (drop_if_untouched (was) && target > was) g_cur = target - 1;
	if (g_pin != g_cur) g_pin = -1;
	undo_break ();
	rebuild_order ();
	refresh_views ();
	refresh ();
}
static void select_record (int rec)
{
	if (rec == g_cur) return;
	int was = g_cur;
	g_cur = rec;
	if (drop_if_untouched (was)) { if (rec > was) g_cur = rec - 1; rebuild_order (); refresh_views (); }
	refresh ();
}
static void cmd_first () { goto_pos (0); }
static void cmd_prev () { goto_pos (cur_pos () - 1); }
static void cmd_next () { goto_pos (cur_pos () + 1); }
static void cmd_last () { goto_pos (g_nord - 1); }
static void cmd_goto ()
{
	if (!g_nord) return;
	if (g_view == V_FORM && !g_form->commit (false)) return;
	GotoBox g (cur_pos () + 1, g_nord);
	g.e->setFocus ();
	if (g.run () == 1)
	{
		long long n = 0; bool ex;
		if (parse_num (g.e->text (), 0, &n, &ex)) goto_pos ((int) (n < 1 ? 1 : n > g_nord ? g_nord : n) - 1);
	}
	focus_view ();
}

// ---- the records ------------------------------------------------------------------------------------------------------
static void cmd_new_record ()
{
	if (!g_doc.nf) { ask ("New Record", "This form has no fields yet: add them in the Design view first.", MB_OK, 0); return; }
	if (g_view == V_FORM && !g_form->commit (false)) return;
	if (g_cur >= 0 && g_cur == g_pin && rec_empty (g_doc, g_cur))		// (a new one is there already)
	{
		if (g_view != V_FORM) show_view (V_FORM);
		g_form->focusFirst ();
		return;
	}
	if (g_view != V_FORM)
	{
		show_view (V_FORM);
		if (g_view != V_FORM) return;
	}
	undo_mark (-1);
	g_pinUndo = g_nundo;
	g_cur = doc_add_record (g_doc, -1);
	g_pin = g_cur;
	doc_changed (false);
	g_form->focusFirst ();
	status ("New record: type its values");
}
static void cmd_dup_record ()
{
	if (g_cur < 0 || g_view == V_DESIGN) return;
	if (g_view == V_FORM && !g_form->commit (false)) return;
	undo_mark (-1);
	int src = g_cur, at = doc_add_record (g_doc, src + 1);
	for (int k = 0; k < g_doc.nf; k++) g_doc.r[at][k] = sdup (g_doc.r[src][k]);
	if (g_pin >= at) g_pin++;
	g_cur = at; g_pin = at;
	doc_changed (false);
	status ("Record duplicated");
}
static void cmd_del_record ()
{
	if (g_cur < 0 || g_view == V_DESIGN) return;
	char msg[200] = "Delete the record";
	for (int k = 0; k < g_doc.nf; k++)
		if ((g_doc.f[k].type == FT_TEXT || g_doc.f[k].type == FT_CHOICE) && g_doc.r[g_cur][k][0])
		{
			char v[48]; scpy (v, g_doc.r[g_cur][k], sizeof v);
			scat (msg, " \"", sizeof msg); scat (msg, v, sizeof msg); scat (msg, "\"", sizeof msg);
			break;
		}
	scat (msg, "?\nEdit > Undo brings it back.", sizeof msg);
	if (ask ("Delete Record", msg, MB_YESNO, 2) != 1) { focus_view (); return; }
	int p = cur_pos (), del = g_cur;
	int next = p + 1 < g_nord ? g_ord[p + 1] : p > 0 ? g_ord[p - 1] : -1;
	undo_mark (-1);
	doc_del_record (g_doc, del);
	if (g_pin == del) g_pin = -1; else if (g_pin > del) g_pin--;
	g_cur = next > del ? next - 1 : next;
	doc_changed (false);
	focus_view ();
	status ("Record deleted");
}
static void cmd_revert_record () { if (g_view == V_FORM) g_form->revert (); }

// ---- the search -------------------------------------------------------------------------------------------------------
static void on_search (Widget &w)
{
	LineEdit &e = (LineEdit &) w;
	if (g_view == V_FORM && !g_form->commit (false)) { e.setText (g_search); return; }
	if (g_view == V_FORM) drop_if_untouched (g_cur);
	scpy (g_search, e.text (), sizeof g_search);
	g_pin = -1;
	rebuild_order ();
	refresh_views ();
	refresh ();
	e.setFocus ();
}
static void on_search_done (Widget &) { focus_view (); }
static void cmd_clear_search () { g_searchBox->clear (); focus_view (); }
static void cmd_find () { g_searchBox->setFocus (); g_searchBox->selectAll (); }
static void cmd_file_order () { g_doc.sort = -1; g_doc.sortDesc = false; rebuild_order (); refresh_views (); status ("In the file's order"); }

// ---- the form's design (the Design view shown for them) ---------------------------------------------------------------
static bool to_design () { show_view (V_DESIGN); return g_view == V_DESIGN; }
static void cmd_add_field () { if (to_design ()) g_design->add (); }
static void cmd_remove_field () { if (to_design ()) g_design->remove (); }
static void cmd_field_up () { if (to_design ()) g_design->move (-1); }
static void cmd_field_down () { if (to_design ()) g_design->move (1); }

// ---- the clipboard: to the editor with the keyboard -------------------------------------------------------------------
static void cmd_cut () { g_root->handleKey (WK_CTRL ('X')); }
static void cmd_copy () { g_root->handleKey (WK_CTRL ('C')); }
static void cmd_paste () { g_root->handleKey (WK_CTRL ('V')); }

// ---- the files ------------------------------------------------------------------------------------------------------------
static bool read_all (const char *path, char **out, int *len)
{
	void *f = kapi_open (path);
	if (!f) return false;
	unsigned sz = kapi_fsize (f);
	if (sz > 64u << 20) sz = 64u << 20;
	char *b = new char[sz + 1];
	int n = kapi_read (f, b, sz);
	kapi_close (f);
	if (n < 0) n = 0;
	b[n] = '\0';
	*out = b; *len = n;
	return true;
}

// A document just read (or made): the state from scratch.
static void doc_fresh (const char *path, bool saved, int view)
{
	scpy (g_path, path, sizeof g_path);
	clear_history ();
	g_state = ++g_seq; g_saved = saved ? g_state : 0;
	g_cur = -1; g_pin = -1; g_fsel = 0;
	g_search[0] = '\0';
	if (g_searchBox) g_searchBox->setText ("");
	g_fieldsVer++;
	g_view = view;
	g_form->hidden = view != V_FORM; g_list->hidden = view != V_LIST; g_design->hidden = view != V_DESIGN;
	rebuild_order ();
	refresh_views ();
	refresh ();
	focus_view ();
	g_root->invalidate (true);
}

static Doc s_tmp;
static void remember (const char *path) { kapi_save_file (LAST, path, (unsigned) slen (path)); }
// A .card file (or a CSV imported) as the document; quiet: no word when it cannot be read.
static bool load_path (const char *path, bool quiet = false)
{
	char *b; int n;
	if (!read_all (path, &b, &n)) { if (!quiet) ask ("Open", "This file could not be read.", MB_OK, 2); return false; }
	bool csv = has_ext (path, ".csv") || has_ext (path, ".tsv");
	const char *why = "This file is not a Cardfile document.";
	bool ok;
	doc_init (s_tmp);
	if (csv)
	{
		char title[TITLE_MAX]; scpy (title, base_name (path), sizeof title);
		int t = slen (title); while (t > 0 && title[t - 1] != '.') t--;
		if (t > 1) title[t - 1] = '\0';
		if (title[0] >= 'a' && title[0] <= 'z') title[0] = (char) (title[0] - 32);
		ok = csv_read (s_tmp, b, n, title);
		why = "This file holds no CSV table.";
	}
	else ok = doc_read (s_tmp, b, n, &why);
	delete [] b;
	if (!ok) { doc_clear (s_tmp); if (!quiet) ask ("Open", why, MB_OK, 2); return false; }
	doc_clear (g_doc);
	g_doc = s_tmp;				// (the fields and the records handed over)
	doc_init (s_tmp);
	int view = g_view == V_DESIGN ? V_FORM : g_view;
	if (csv) view = V_LIST;
	doc_fresh (csv ? "" : path, !csv, view);
	if (!csv) remember (path);
	if (csv)
	{
		char m[96] = "Imported: "; scat_num (m, g_doc.nr, sizeof m); scat (m, " records, ", sizeof m);
		scat_num (m, g_doc.nf, sizeof m); scat (m, " fields", sizeof m);
		status (m);
	}
	return true;
}
static bool write_path (const char *path)
{
	Out o; doc_write (g_doc, o);
	if (kapi_save_file (path, o.b, (unsigned) o.n) < 0) { ask ("Save", "The file could not be written.", MB_OK, 2); return false; }
	scpy (g_path, path, sizeof g_path);
	g_saved = g_state;
	remember (path);
	char m[160] = "Saved "; scat (m, base_name (path), sizeof m);
	status (m);
	return true;
}
// A file's name from the form's title: "My Books" -> "My Books.card"
static void default_name (char *out, int cap, const char *ext)
{
	if (g_path[0])
	{
		scpy (out, base_name (g_path), cap);
		int n = slen (out); while (n > 0 && out[n - 1] != '.') n--;
		if (n > 1) out[n - 1] = '\0';
	}
	else
	{
		int n = 0;
		for (const char *p = g_doc.title; *p && n < cap - 8; p++)
			if (*p != '/' && *p != '\\' && *p != ':' && *p != '*' && *p != '?' && *p != '"' && *p != '<' && *p != '>' && *p != '|') out[n++] = *p;
		out[n] = '\0';
		if (!n) scpy (out, "Untitled", cap);
	}
	scat (out, ext, cap);
}
static void cmd_save_as ()
{
	if (!commit_edits ()) return;
	char path[200], def[96];
	default_name (def, sizeof def, ".card");
	if (wk_file_save (path, sizeof path, "SD:/docs", def))
	{
		if (!has_ext (path, ".card")) scat (path, ".card", sizeof path);
		write_path (path);
	}
	focus_view ();
}
static void cmd_save ()
{
	if (!commit_edits ()) return;
	if (!g_path[0] || !has_ext (g_path, ".card")) { cmd_save_as (); return; }
	write_path (g_path);
	focus_view ();
}
// Unsaved changes: saved (Yes), dropped (No) or kept (Cancel -> false).
static bool confirm_discard ()
{
	if (!commit_edits ()) return false;
	if (!modified ()) return true;
	char m[200] = "Save the changes to ";
	scat (m, g_path[0] ? base_name (g_path) : g_doc.title, sizeof m);
	scat (m, "?", sizeof m);
	int r = ask ("Unsaved changes", m, MB_YESNOCANCEL, 1);
	if (r == 0) return false;
	if (r == 1) { cmd_save (); return !modified (); }
	return true;
}
static void cmd_new ()
{
	if (!confirm_discard ()) { focus_view (); return; }
	doc_new (g_doc);
	doc_fresh ("", true, V_DESIGN);
	g_design->focusLabel ();
	status ("A new form: name its fields and choose their types, then add records");
}
static void cmd_open ()
{
	if (!confirm_discard ()) { focus_view (); return; }
	char path[200];
	if (wk_file_open (path, sizeof path, "SD:/docs")) load_path (path);
	focus_view ();
}
static void cmd_import_csv () { cmd_open (); }
static void cmd_export_csv ()
{
	if (!commit_edits ()) return;
	char path[200], def[96];
	default_name (def, sizeof def, ".csv");
	if (wk_file_save (path, sizeof path, "SD:/docs", def))
	{
		if (!has_ext (path, ".csv")) scat (path, ".csv", sizeof path);
		Out o; csv_write (g_doc, g_ord, g_nord, o);
		if (kapi_save_file (path, o.b ? o.b : "", (unsigned) o.n) < 0) ask ("Export", "The file could not be written.", MB_OK, 2);
		else { char m[96] = "Exported "; scat_num (m, g_nord, sizeof m); scat (m, g_nord == 1 ? " record" : " records", sizeof m); status (m); }
	}
	focus_view ();
}
// Mail Merge (MergeBox): the records to merge written in MERGE_DATA, the job in MERGE_JOB, Writer started.
static void cmd_mail_merge ()
{
	if (!commit_edits ()) return;
	if (g_doc.nf == 0 || g_nord == 0) { ask ("Mail Merge", "The form has no records to merge.", MB_OK, 1); focus_view (); return; }
	char rn[64] = "";
	if (g_cur >= 0) value_show (g_doc.f[0], g_doc.r[g_cur][0], rn, sizeof rn, true, ", ");
	MergeBox m (rn, g_nord);
	if (m.run () != 1) { focus_view (); return; }
	// the letter remembered by the form
	if (!seq (g_doc.merge, m.letter->text ())) { scpy (g_doc.merge, m.letter->text (), sizeof g_doc.merge); g_state = ++g_seq; }
	// the records to merge: this one, or the ones shown (in the views' order)
	bool one = m.one->checked && g_cur >= 0;
	int n = one ? 1 : g_nord;
	char ***keep = g_doc.r; int keepN = g_doc.nr;
	char ***sub = new char **[n];
	if (one) sub[0] = g_doc.r[g_cur]; else for (int i = 0; i < n; i++) sub[i] = g_doc.r[g_ord[i]];
	g_doc.r = sub; g_doc.nr = n;
	Out o; doc_write (g_doc, o);
	g_doc.r = keep; g_doc.nr = keepN;
	delete [] sub;
	bool ok = kapi_save_file (MERGE_DATA, o.b ? o.b : "", (unsigned) o.n) >= 0;
	Out j;
	j.puts ("template = "); j.puts (m.letter->text ()); j.puts ("\ndata = "); j.puts (MERGE_DATA); j.puts ("\nrecords = all\n");
	if (m.files->checked)
	{
		static const char *const F[4] = { "", "rtf", "docx", "odt" };
		j.puts ("output = files\nfolder = "); j.puts (m.folder->text ()[0] ? m.folder->text () : "SD:/docs/Letters");
		j.puts ("\nname = "); if (m.naming->sel > 0) j.puts (g_doc.f[m.naming->sel - 1].column);
		j.puts ("\nformat = "); j.puts (F[m.format->sel & 3]); j.put ('\n');
	}
	else j.puts ("output = open\n");
	if (ok) ok = kapi_save_file (MERGE_JOB, j.b, (unsigned) j.n) >= 0;
	char args[240] = "--merge "; scat (args, MERGE_JOB, sizeof args);
	if (!ok || !kapi_exec ("SD:/apps/writer.app/main", args)) ask ("Mail Merge", "Writer could not be started.", MB_OK, 2);
	else { char s[96] = "Mail merge: "; scat_num (s, n, sizeof s); scat (s, n == 1 ? " record sent to Writer" : " records sent to Writer", sizeof s); status (s); }
	refresh ();
	focus_view ();
}

// ---- the window ---------------------------------------------------------------------------------------------------------
class CardRoot : public Root
{
public:
	CardRoot () : Root (W, H, "Cardfile"), m_tick (0) {}
	void onTick () override
	{
		unsigned t = kapi_get_ticks ();
		if (t - m_tick >= 20)				// (the record's edits, a status word's end)
		{
			m_tick = t;
			if (g_msg[0] && t - g_msgT >= 400) g_msg[0] = '\0';
			refresh ();
		}
	}
	void onDrop (int, int, int type, const char *data, int, unsigned) override
	{
		char path[200];
		if (type != DND_FILES || !doc_first_path (data, path, sizeof path)) return;
		void *d = kapi_opendir (path);
		if (d) { kapi_closedir (d); return; }
		if (!confirm_discard ()) return;
		load_path (path);
	}
	bool onKey (long k) override
	{
		if (k == KEY_PGUP && g_view == V_FORM) { cmd_prev (); return true; }
		if (k == KEY_PGDN && g_view == V_FORM) { cmd_next (); return true; }
		return false;
	}
private:
	unsigned m_tick;
};

static void on_nav (int b)
{
	switch (b)
	{
	case NavBar::B_FIRST: cmd_first (); break;
	case NavBar::B_PREV: cmd_prev (); break;
	case NavBar::B_NEXT: cmd_next (); break;
	case NavBar::B_LAST: cmd_last (); break;
	case NavBar::B_NEW: cmd_new_record (); break;
	case NavBar::B_POS: cmd_goto (); break;
	}
}

static ToolButton *tool (ToolBar *tb, int ic, const char *tip, void (*cb) (), int gap = 2)
{
	ToolButton *b = new ToolButton (ic, tip, cb);
	tb->add (b, gap);
	return b;
}

} // namespace cf

using namespace cf;

int main (void)
{
	CardRoot root;
	if (root.canvas.px == 0) return 1;
	root.attach ();				// (a question asked before run (): its clicks and keys)
	g_root = &root;
	doc_init (g_doc);
	doc_init (s_tmp);

	// the toolbar
	ToolBar *tb = new ToolBar (0, 0, W);
	tb->anchor = ANCHOR_LEFT | ANCHOR_TOP | ANCHOR_RIGHT;
	root.addChild (tb);
	tool (tb, IC_NEW, "New form (Ctrl+N)", cmd_new, 0);
	tool (tb, IC_OPEN, "Open... (Ctrl+O)", cmd_open);
	tool (tb, IC_SAVE, "Save (Ctrl+S)", cmd_save);
	tb->sep ();
	g_bUndo = tool (tb, IC_UNDO, "Undo (Ctrl+Z)", cmd_undo, 0);
	g_bRedo = tool (tb, IC_REDO, "Redo (Ctrl+Y)", cmd_redo);
	tb->sep ();
	g_switch = new ViewSwitch (on_switch);
	g_switch->tip = "The view: Form (F5), List (F6), Design (F7)";
	tb->add (g_switch, 0);
	tb->sep ();
	g_bNewRec = tool (tb, IC_REC_NEW, "New record (Ctrl+R)", cmd_new_record, 0);
	g_bDupRec = tool (tb, IC_REC_DUP, "Duplicate the record (Ctrl+D)", cmd_dup_record);
	g_bDelRec = tool (tb, IC_REC_DEL, "Delete the record...", cmd_del_record);
	g_searchBox = new SearchBox (220);
	g_searchBox->left = W - 10 - g_searchBox->width; g_searchBox->top = (TB_H - g_searchBox->height) / 2;
	g_searchBox->anchor = ANCHOR_RIGHT | ANCHOR_TOP;
	g_searchBox->onChange = on_search; g_searchBox->onEnter = on_search_done;
	tb->addChild (g_searchBox);

	// the views, the navigator
	int vh = H - TB_H - NAV_H;
	g_form = new FormView (0, TB_H, W, vh);
	g_list = new ListView (0, TB_H, W, vh);
	g_design = new DesignView (0, TB_H, W, vh);
	g_form->anchor = g_list->anchor = g_design->anchor = ANCHOR_FILL;
	g_list->hidden = g_design->hidden = true;
	root.addChild (g_form); root.addChild (g_list); root.addChild (g_design);
	g_nav = new NavBar (0, H - NAV_H, W);
	g_nav->anchor = ANCHOR_LEFT | ANCHOR_RIGHT | ANCHOR_BOTTOM;
	g_nav->onNav = on_nav;
	root.addChild (g_nav);
	root.setResizable (true);

	static Menu menu;
	menu.menu ("File");
	menu.item ("New Form", "^N", WK_CTRL ('N'), cmd_new);
	menu.item ("Open...", "^O", WK_CTRL ('O'), cmd_open);
	menu.separator ();
	menu.item ("Save", "^S", WK_CTRL ('S'), cmd_save);
	menu.item ("Save As...", "", 0, cmd_save_as);
	menu.separator ();
	menu.item ("Import CSV...", "", 0, cmd_import_csv);
	menu.item ("Export as CSV...", "", 0, cmd_export_csv);
	menu.menu ("Edit");
	menu.item ("Undo", "^Z", WK_CTRL ('Z'), cmd_undo);
	menu.item ("Redo", "^Y", WK_CTRL ('Y'), cmd_redo);
	menu.separator ();
	menu.item ("Cut", "^X", 0, cmd_cut);
	menu.item ("Copy", "^C", 0, cmd_copy);
	menu.item ("Paste", "^V", 0, cmd_paste);
	menu.separator ();
	menu.item ("Search...", "^F", WK_CTRL ('F'), cmd_find);
	menu.item ("Clear the Search", "", 0, cmd_clear_search);
	menu.menu ("Record");
	menu.item ("New Record", "^R", WK_CTRL ('R'), cmd_new_record);
	menu.item ("Duplicate Record", "^D", WK_CTRL ('D'), cmd_dup_record);
	menu.item ("Delete Record...", "", 0, cmd_del_record);
	menu.separator ();
	menu.item ("First Record", "^Home", 0, cmd_first);
	menu.item ("Previous Record", "PgUp", 0, cmd_prev);
	menu.item ("Next Record", "PgDn", 0, cmd_next);
	menu.item ("Last Record", "^End", 0, cmd_last);
	menu.item ("Go to Record...", "^G", WK_CTRL ('G'), cmd_goto);
	menu.separator ();
	menu.item ("Mail Merge...", "", 0, cmd_mail_merge);
	menu.separator ();
	menu.item ("Undo the Record's Changes", "Esc", 0, cmd_revert_record);
	menu.menu ("View");
	menu.item ("Form", "F5", KEY_F1 + 4, cmd_view_form);
	menu.item ("List", "F6", KEY_F1 + 5, cmd_view_list);
	menu.item ("Design", "F7", KEY_F1 + 6, cmd_view_design);
	menu.separator ();
	menu.item ("In the File's Order", "", 0, cmd_file_order);
	menu.menu ("Design");
	menu.item ("Add Field", "", 0, cmd_add_field);
	menu.item ("Remove Field...", "", 0, cmd_remove_field);
	menu.item ("Move Field Up", "", 0, cmd_field_up);
	menu.item ("Move Field Down", "", 0, cmd_field_down);
	menu.publish ();

	// A file named on the command line; else the form kept at the last close (asked); else the form
	// opened last time; else a new form.
	char args[200];
	int an = kapi_get_args (args, sizeof args);
	bool opened = an > 0 && args[0] && load_path (args);
	if (!opened)
	{
		doc_new (g_doc);
		doc_fresh ("", true, V_DESIGN);
		char *b; int n;
		if (read_all (RECOVER, &b, &n))
		{
			if (n > 0 && ask ("Cardfile", "Cardfile was closed with unsaved changes. Open the form it kept?", MB_YESNO, 1) == 1 && doc_read (s_tmp, b, n, 0))
			{
				doc_clear (g_doc); g_doc = s_tmp; doc_init (s_tmp);
				char *p; int pn; char was[200] = "";
				if (read_all (RECOVER_NAME, &p, &pn)) { scpy (was, p, sizeof was); delete [] p; }
				doc_fresh (was, false, V_FORM);
				opened = true;
			}
			delete [] b;
			kapi_remove (RECOVER); kapi_remove (RECOVER_NAME);
		}
		if (!opened && read_all (LAST, &b, &n))
		{
			char last[200]; trim_copy (last, b, sizeof last);
			delete [] b;
			if (last[0]) opened = load_path (last, true);
		}
		if (!opened) g_design->focusLabel ();
	}
	refresh ();
	root.run ();

	// Closed with unsaved changes: the document kept for the next start.
	g_form->commit (true);
	if (modified ())
	{
		Out o; doc_write (g_doc, o);
		kapi_save_file (RECOVER, o.b, (unsigned) o.n);
		kapi_save_file (RECOVER_NAME, g_path, (unsigned) slen (g_path));
	}
	return 0;
}
