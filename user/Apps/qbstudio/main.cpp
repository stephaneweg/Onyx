//
// qbstudio -- QBStudio, the IDE for desktop apps in Onyx BASIC (docs/qbstudio/README.md). A project is a folder
// (SD:/projects/<name>/, its project.ini): its window, Main.form (a control a line, the parent by the indentation:
// form.h), drawn and edited in the designer (designer.h: the controls dragged from the toolbox, moved, sized; the
// properties at the right: props.h) and as text, the two kept in step; its code, Main.bas (the event SUBs:
// convert_Click...), in the code editor (codeedit.h: BASIC's colours, the completion after a control's name, the
// problems as you type); the window's code, Main.form.bas, generated (gen.h) and read-only. Run (F5) builds the
// program -- the controls' library, the window's code, yours, then Main_Run -- and starts it with SD:/bin/basic
// (an error, at the start or while it runs, is shown in its file at its line); File > Make App writes it as an app
// (SD:/apps/<name>.app: main.bax or main.bas, app.txt, icon.bmp).
//
// The window: the toolbar (the files, the edits, Run, Make App; Design / Split / Code), the project and the toolbox
// (in the code: the outline) at the left, the open files' tabs, the designer and the form's text (or the code, its
// object and event lists above it), the problems under them, the properties at the right, the status bar.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#include "uikit/uikit.h"
#include "uikit/toolbar.h"
#include "ft/uikitface.h"
#include "basic/bas.h"
#include "codeedit.h"
#include "designer.h"
#include "props.h"
#include "gen.h"
#include "notify.h"

using namespace uikit;
using namespace qs;

#define W0 1000
#define H0 700
enum { TB_H = 36, LEFT_W = 210, RIGHT_W = 244, TABS_H = 28, CODEBAR_H = 34, MSG_H = 112, ST_H = 24, HEAD_H = 22, TREE_H = 190 };
static const char *LAST = "SD:/apps/qbstudio.app/last.txt";
static const char *SAMPLE = "SD:/projects/converter";
static const char *SETTINGS = "SD:/apps/qbstudio.app/settings.ini";

// ---- the project ---------------------------------------------------------------------------------------------------
enum { DOC_FORM, DOC_CODE, DOC_GEN, DOC_TEXT };
struct Doc
{
	char file[64]; int type;
	CodeEdit *ed;
	Form *form;					// DOC_FORM: the last tree read without errors
	int gen;					// DOC_FORM: its generated doc
	bool dirty, open;
	int selPath[32], selN;				// the designer's choice (DOC_FORM)
};
struct Project
{
	char dir[200], name[40], title[64], mainForm[40], category[32]; bool compiled;
	Vec<Doc> docs;
	Vec<int> tabs;					// the open docs, in order
	int cur;
	Project () : compiled (true), cur (-1) { dir[0] = name[0] = title[0] = mainForm[0] = category[0] = 0; }
};
static Project g_p;
struct Problem { int kind; char text[180]; char file[64]; int line; };	// kind 0 error, 1 warning, 2 information
static Vec<Problem> g_probs;
static Vec<Part> g_parts;				// the last build's parts (a runtime error's line found back)
static int g_view = 1;					// a form's view: 0 Design, 1 Split, 2 Text

// ---- the window's parts --------------------------------------------------------------------------------------------
class QsRoot;
static QsRoot *g_root;
static ToolBar *g_tb;
static SegmentedControl *g_seg;
static TreeView *g_tree;
static Designer *g_des;
static Toolbox *g_tools;
static ListBox *g_outline;
static PropGrid *g_props;
static Dropdown *g_objBox, *g_evBox;
static Widget *g_codebar;
class DocTabs; static DocTabs *g_tabs;
class MsgList; static MsgList *g_msgs;
class StatusLine; static StatusLine *g_status;
class PaneTitle; static PaneTitle *g_hProject, *g_hTools, *g_hProps;
static ToolButton *g_runBtn;
static unsigned g_checkAt;				// the code checked again then (ticks; 0 none)
static bool g_reading;					// (a form read: its text's caret does not choose)

static void relayout ();
static void show_doc (int i);
static void check_program (bool quiet);
static void refresh_outline ();
static void refresh_codebar ();
static void status (const char *s);

static const char *base_name (const char *p) { const char *b = p; for (const char *q = p; *q; q++) if (*q == '/' || *q == ':') b = q + 1; return b; }
static bool ends_with (const char *s, const char *e) { int n = (int) strlen (s), k = (int) strlen (e); return n >= k && !strcasecmp (s + n - k, e); }
static void join (char *o, int cap, const char *dir, const char *f) { snprintf (o, cap, "%s%s%s", dir, dir[0] && dir[strlen (dir) - 1] != '/' ? "/" : "", f); }
static char *read_file (const char *path)
{
	void *f = kapi_open (path); if (!f) return 0;
	unsigned sz = kapi_fsize (f);
	char *b = (char *) malloc (sz + 1);
	int n = kapi_read (f, b, sz); kapi_close (f);
	if (n < 0) { free (b); return 0; }
	b[n] = 0;
	// (CRLF -> LF; tabs kept)
	int w = 0; for (int i = 0; i < n; i++) if (b[i] != '\r') b[w++] = b[i];
	b[w] = 0;
	return b;
}
static bool file_exists (const char *p) { void *f = kapi_open (p); if (f) kapi_close (f); return f != 0; }
static bool write_file (const char *path, const char *s) { return kapi_save_file (path, s, (unsigned) strlen (s)) >= 0; }

// ---- small widgets -----------------------------------------------------------------------------------------------------
class PaneTitle : public Widget
{
public:
	char text[48], rtext[48];
	PaneTitle (const char *t) : Widget (0, 0, 10, HEAD_H) { cpy (text, t, sizeof text); rtext[0] = 0; }
	void set (const char *t, const char *r = "") { cpy (text, t, sizeof text); cpy (rtext, r, sizeof rtext); invalidate (true); }
	void onDraw () override
	{
		canvas.clear (C_BG);
		uk_text (canvas, 8, (height - uk_fh ()) / 2, text, C_TEXT, 2);
		if (rtext[0]) uk_text (canvas, width - 8 - uk_tw (rtext), (height - uk_fh ()) / 2, rtext, uk_mix (C_BG, C_TEXT, 140));
		canvas.fillRect (0, height - 1, width, 1, uk_mix (C_BG, C_TEXT, 30));
	}
};
// The open files: a tab each (a dot: changed; x: closed)
class DocTabs : public Widget
{
public:
	DocTabs () : Widget (0, 0, 10, TABS_H) {}
	int tabW (int d) { return uk_tw (g_p.docs[d].file, 2) + 52; }
	void onDraw () override
	{
		unsigned bg = uk_mix (C_BG, C_TEXT, 22);
		canvas.clear (bg);
		int x = 4;
		for (int i = 0; i < g_p.tabs.n; i++)
		{
			int d = g_p.tabs[i]; Doc &D = g_p.docs[d]; int w = tabW (d);
			bool cur = d == g_p.cur;
			if (cur) { uk_rbox (canvas, x, 3, w, height - 3, 5, C_FIELD, C_FIELD); }
			unsigned ink = cur ? C_FIELD_TEXT : D.type == DOC_GEN ? uk_mix (bg, C_TEXT, 120) : C_TEXT;
			unsigned ic = D.type == DOC_FORM ? 0x2E7FA8 : D.type == DOC_GEN ? 0x909090 : 0x3A6FD8;
			canvas.fillRect (x + 8, height / 2 - 4, 10, 11, ic); canvas.fillRect (x + 9, height / 2 - 3, 8, 3, 0xFFFFFF);
			uk_text (canvas, x + 24, (height - uk_fh ()) / 2 + 1, D.file, ink, cur ? 2 : 0);
			int cx = x + w - 16;
			if (D.dirty) uk_bead (canvas, cx - 3, height / 2 - 3, 7, uk_mix (bg, C_TEXT, 150));
			else uk_glyph (canvas, WKG_CLOSE, cx, height / 2 + 1, 7, uk_mix (bg, C_TEXT, 130));
			x += w + 2;
		}
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		if (mx < 0) return false;
		if (!bl || pressed) { if (!bl) pressed = false; return true; }
		pressed = true;
		int x = 4;
		for (int i = 0; i < g_p.tabs.n; i++)
		{
			int d = g_p.tabs[i], w = tabW (d);
			if (mx >= x && mx < x + w)
			{
				if (mx >= x + w - 24 && !g_p.docs[d].dirty && g_p.tabs.n > 1)
				{
					g_p.docs[d].open = false; g_p.tabs.erase (i);
					if (d == g_p.cur) show_doc (g_p.tabs[imin (i, g_p.tabs.n - 1)]);
					invalidate (true); return true;
				}
				show_doc (d); return true;
			}
			x += w + 2;
		}
		(void) my;
		return true;
	}
};
// The problems and the output: a row each; a click shows its place
class MsgList : public Widget
{
public:
	int scroll, sel; char output[160];
	MsgList () : Widget (0, 0, 10, MSG_H), scroll (0), sel (-1) { output[0] = 0; }
	int rowH () { return uk_fh () + 5; }
	void onDraw () override
	{
		canvas.clear (C_FIELD);
		int errs = 0; for (int i = 0; i < g_probs.n; i++) if (g_probs[i].kind == 0) errs++;
		unsigned hb = uk_mix (C_BG, C_FIELD, 100);
		canvas.fillRect (0, 0, width, HEAD_H + 2, hb);
		char t[40]; snprintf (t, sizeof t, "Problems  %d", errs);
		uk_text (canvas, 10, 4, t, C_TEXT, 2);
		if (output[0]) uk_text (canvas, 120, 4, output, uk_mix (hb, C_TEXT, 150));
		canvas.fillRect (0, HEAD_H + 2, width, 1, uk_mix (C_BG, C_TEXT, 30));
		int RH = rowH ();
		for (int i = scroll; i < g_probs.n; i++)
		{
			int y = HEAD_H + 5 + (i - scroll) * RH; if (y > height) break;
			const Problem &p = g_probs[i];
			if (i == sel) canvas.fillRect (0, y, width, RH, uk_mix (C_FIELD, C_ACCENT, 50));
			unsigned c = p.kind == 0 ? 0xD08A1A : p.kind == 1 ? 0x2E8FB8 : 0x3FA45B;
			if (p.kind == 0) c = 0xD03B2B;
			uk_bead (canvas, 10, y + RH / 2 - 4, 9, c);
			char line[260];
			if (p.file[0] && p.line) snprintf (line, sizeof line, "%s %d: %s", p.file, p.line, p.text); else snprintf (line, sizeof line, "%s", p.text);
			uk_text (canvas, 26, y + 2, line, C_FIELD_TEXT);
		}
	}
	bool onMouse (int mx, int my, int bl, int, int, int wheel) override
	{
		if (mx < 0) return false;
		if (wheel) { scroll = imax (0, imin (scroll - wheel, g_probs.n - 1)); invalidate (true); return true; }
		if (!bl || pressed) { if (!bl) pressed = false; return true; }
		pressed = true;
		int i = scroll + (my - HEAD_H - 5) / rowH ();
		if (my < HEAD_H + 5 || i < 0 || i >= g_probs.n) return true;
		sel = i; invalidate (true);
		const Problem &p = g_probs[i];
		for (int d = 0; d < g_p.docs.n; d++)
			if (!strcmp (g_p.docs[d].file, p.file))
			{
				if (g_p.docs[d].type == DOC_FORM && g_view == 0) { g_view = 1; g_seg->select (1); }
				show_doc (d);
				if (p.line > 0) { g_p.docs[d].ed->gotoLine (p.line - 1); g_p.docs[d].ed->setFocus (); }
				break;
			}
		return true;
	}
};
class StatusLine : public Widget
{
public:
	char sLeft[160], sMid[160], sRight[48];
	StatusLine () : Widget (0, 0, 10, ST_H) { sLeft[0] = sMid[0] = sRight[0] = 0; }
	void onDraw () override
	{
		canvas.clear (C_BG);
		canvas.fillRect (0, 0, width, 1, uk_mix (C_BG, C_TEXT, 40));
		int y = (height - uk_fh ()) / 2 + 1;
		uk_text (canvas, 10, y, sLeft, C_TEXT, 2);
		uk_text (canvas, 190, y, sMid, C_TEXT);
		uk_text (canvas, width - 12 - uk_tw (sRight), y, sRight, C_TEXT);
	}
};
static void status (const char *s) { cpy (g_status->sMid, s, sizeof g_status->sMid); g_status->invalidate (true); }

// A line asked for
class AskDialog : public Modal
{
public:
	Textbox *t; const char *m_title, *m_q;
	AskDialog (const char *title, const char *q, const char *def) : Modal (440, 150), m_title (title), m_q (q)
	{
		Root *r = Root::current (); if (r) { left = imax (0, (r->width - width) / 2); top = imax (0, (r->height - height) / 2); }
		t = new Textbox (16, titleH () + 36, 408, 26, def); addChild (t);
		Button *ok = new Button (width - 184, height - 42, 82, 28, "OK", act); ok->tag = 1; addChild (ok);
		Button *no = new Button (width - 94, height - 42, 82, 28, "Cancel", act); no->tag = 0; addChild (no);
	}
	void onDraw () override { drawBox (m_title); canvas.text (16, titleH () + 14, m_q, C_TEXT); }
	bool onKey (long k) override { if (k == 27) { close (0); return true; } if (k == KEY_ENTER) { close (1); return true; } return false; }
	void onButton (int tag) override { close (tag); }
	static void act (Widget &w) { Widget *p = w.parent; while (p && !p->modal) p = p->parent; if (p) ((Modal *) p)->onButton (w.tag); }
};
static bool ask_line (const char *title, const char *q, char *out, int cap)
{
	AskDialog d (title, q, out);
	d.t->setFocus (); d.t->onTabFocus ();
	if (d.run () != 1) return false;
	cpy (out, d.t->text, cap);
	return true;
}

// ---- BASIC's words -------------------------------------------------------------------------------------------------------
static char *g_words; static int g_nwords; static const char **g_word; static unsigned char *g_wlen;
static void load_words ()
{
	g_words = (char *) malloc (16384);
	bas::wordList (g_words, 16384);
	int n = 1; for (char *p = g_words; *p; p++) if (*p == ' ') n++;
	g_word = (const char **) malloc (sizeof (char *) * n); g_wlen = (unsigned char *) malloc (n);
	g_nwords = 0;
	for (char *p = g_words; *p; )
	{
		char *e = p; while (*e && *e != ' ') e++;
		g_word[g_nwords] = p; g_wlen[g_nwords] = (unsigned char) (e - p); g_nwords++;
		p = *e ? e + 1 : e;
	}
}
static bool is_keyword (const char *w, int n)
{
	for (int i = 0; i < g_nwords; i++)
		if (g_wlen[i] == n) { int k = 0; while (k < n && upc (w[k]) == upc (g_word[i][k])) k++; if (k == n) return true; }
	return false;
}
static bool is_word0 (const char *w) { return is_keyword (w, (int) strlen (w)); }

// ---- the documents ---------------------------------------------------------------------------------------------------------
static Doc *cur_doc () { return g_p.cur >= 0 ? &g_p.docs[g_p.cur] : 0; }
static int find_doc (const char *file) { for (int i = 0; i < g_p.docs.n; i++) if (!strcasecmp (g_p.docs[i].file, file)) return i; return -1; }
static Doc *main_form () { for (int i = 0; i < g_p.docs.n; i++) if (g_p.docs[i].type == DOC_FORM) return &g_p.docs[i]; return 0; }
static void form_name_of (const Doc &d, char *o, int cap) { cpy (o, d.file, cap); char *dot = strchr (o, '.'); if (dot) *dot = 0; }
// The code doc a form's events go into: <Form>.bas, else the first .bas
static Doc *code_for_form ()
{
	Doc *f = main_form ();
	if (f) { char n[64]; form_name_of (*f, n, sizeof n); strncat (n, ".bas", sizeof n - strlen (n) - 1); int i = find_doc (n); if (i >= 0) return &g_p.docs[i]; }
	for (int i = 0; i < g_p.docs.n; i++) if (g_p.docs[i].type == DOC_CODE) return &g_p.docs[i];
	return 0;
}
static void all_subs (Vec<char *> &subs) { for (int i = 0; i < g_p.docs.n; i++) if (g_p.docs[i].type == DOC_CODE) scan_subs (g_p.docs[i].ed->text (), subs); }
static void free_subs (Vec<char *> &subs) { for (int i = 0; i < subs.n; i++) free (subs[i]); subs.clear (); }
static bool sub_exists (const char *name) { Vec<char *> s; all_subs (s); bool r = has_sub (s, name); free_subs (s); return r; }

// A form's generated code made again
static void regenerate (Doc &d)
{
	if (d.type != DOC_FORM || !d.form || d.gen < 0) return;
	char fn[64]; form_name_of (d, fn, sizeof fn);
	Vec<char *> subs; all_subs (subs);
	Str g; generate (g, *d.form, fn, subs);
	free_subs (subs);
	CodeEdit *ge = g_p.docs[d.gen].ed;
	if (strcmp (ge->text (), g.str ())) { int c = ge->caret (); ge->setText (g.str ()); ge->setCaret (imin (c, ge->length ())); }
}

static void on_text_changed (Widget &w);
static void on_caret (Widget &w);
static void g_root_add (Widget *w);
static void g_root_remove (Widget *w);
static Doc &add_doc (const char *file, int type, const char *text)
{
	Doc d; memset (&d, 0, sizeof d);
	cpy (d.file, file, sizeof d.file); d.type = type; d.gen = -1;
	d.ed = new CodeEdit (0, 0, 100, 100);
	d.ed->lang = type == DOC_FORM ? LANG_FORM : LANG_BASIC;
	d.ed->readonly = type == DOC_GEN;
	d.ed->setText (text ? text : "");
	d.ed->onChange = on_text_changed; d.ed->onCaret = on_caret;
	d.ed->hidden = true;
	g_root_add (d.ed);
	g_p.docs.push (d);
	return g_p.docs[g_p.docs.n - 1];
}

// ---- the form: its text read, its tree written -------------------------------------------------------------------------
static void set_problems_for (const char *file, Form *f);
// The text of a form read again: the designer and the generated code follow (an error: they keep the last good form)
static void read_form (Doc &d)
{
	Form *f = new Form;
	bool ok = form_read (*f, d.ed->text ());
	d.ed->marks.clear ();
	for (int i = 0; i < f->errors.n; i++) d.ed->marks.push (f->errors[i].line);
	set_problems_for (d.file, f);
	if (!ok) { delete f; d.ed->invalidate (true); return; }
	delete d.form; d.form = f;
	regenerate (d);
	if (&d == cur_doc ())
	{
		g_reading = true;
		g_des->setForm (f);
		El *s = d.selN ? el_by_path (*f, d.selPath, d.selN) : 0;
		if (!s) s = f->root;
		g_des->select (s, false);
		g_props->show (s);
		g_reading = false;
		refresh_codebar ();
	}
}
// The designer (or the properties) changed the tree: its text written again (one step to undo)
static void form_edited ()
{
	Doc *d = cur_doc (); if (!d || d->type != DOC_FORM || !d->form) return;
	El *s = g_des->sel;
	d->selN = s ? path_of (s, d->selPath, 32) : 0;
	Str t; form_write (t, *d->form, d->file);
	// the caret: at the chosen element's line (found by writing the tree up to it)
	int caretLine = 0;
	{
		Form tmp; form_read (tmp, t.str ());
		El *e = d->selN ? el_by_path (tmp, d->selPath, d->selN) : tmp.root;
		if (e) caretLine = e->line - 1;
	}
	int caret = 0; for (int ln = 0, i = 0; t.str ()[i]; i++) { if (ln == caretLine) { caret = i; break; } if (t.str ()[i] == '\n') ln++; }
	g_reading = true;
	d->ed->replaceAll (t.str (), caret);		// (-> on_text_changed: read again)
	g_reading = false;
}
static void on_select (El *e)
{
	Doc *d = cur_doc (); if (!d || d->type != DOC_FORM) return;
	d->selN = e ? path_of (e, d->selPath, 32) : 0;
	g_props->show (e);
	if (e && e->line > 0 && !g_reading) { g_reading = true; d->ed->gotoLine (e->line - 1); g_reading = false; }
	char t[120]; if (e) { describe (e, t, sizeof t); cpy (g_status->sLeft, d->file, sizeof g_status->sLeft); status (t); }
}
static void on_element_open (El *e)
{
	if (!e) return;
	char names[4][64]; const char *labels[4];
	int n = events_of (e, names, labels, 4);
	if (n && g_openSub) g_openSub (names[0]);
}

// ---- the problems ------------------------------------------------------------------------------------------------------
static void add_problem (int kind, const char *file, int line, const char *text)
{
	Problem p; p.kind = kind; cpy (p.file, file ? file : "", sizeof p.file); p.line = line; cpy (p.text, text, sizeof p.text);
	g_probs.push (p);
}
static void drop_problems (const char *file) { for (int i = g_probs.n - 1; i >= 0; i--) if (!strcmp (g_probs[i].file, file)) g_probs.erase (i); }
static void set_problems_for (const char *file, Form *f)
{
	drop_problems (file);
	for (int i = 0; i < f->errors.n; i++) add_problem (0, file, f->errors[i].line, f->errors[i].msg);
	if (!f->errors.n && f->root)
	{
		// what the form holds, its events handled
		int ctl = 0, handled = 0, events = 0;
		Vec<char *> subs; all_subs (subs);
		struct W { static void walk (El *e, int &c, int &h, int &ev, Vec<char *> &s) {
			if (is_control (e->kind) || e->kind == K_MENUITEM) { c++; const char *x = event_of (e->kind); if (x && e->name[0]) { ev++; char n[96]; snprintf (n, sizeof n, "%s_%s", e->name, x); if (has_sub (s, n)) h++; } }
			for (int i = 0; i < e->kids.n; i++) walk (e->kids[i], c, h, ev, s); } };
		W::walk (f->root, ctl, handled, events, subs);
		free_subs (subs);
		char t[160]; snprintf (t, sizeof t, "%d controls, %d of their %d events handled", ctl, handled, events);
		add_problem (2, file, 0, t);
	}
	if (g_msgs) g_msgs->invalidate (true);
}
// The whole program put together: the library, the windows' code, the files, the start
static bool build (Str &prog, bool quiet)
{
	g_parts.clear ();
	add_part (prog, g_parts, "(QBStudio's controls)", LIBRARY);
	Doc *mf = 0;
	for (int i = 0; i < g_p.docs.n; i++)
	{
		Doc &d = g_p.docs[i];
		if (d.type != DOC_FORM) continue;
		if (!d.form) { if (!quiet) status ("The form has errors: see the problems"); return false; }
		regenerate (d);
		add_part (prog, g_parts, g_p.docs[d.gen].file, g_p.docs[d.gen].ed->text ());
		if (!mf) mf = &d;
	}
	for (int i = 0; i < g_p.docs.n; i++) if (g_p.docs[i].type == DOC_CODE) add_part (prog, g_parts, g_p.docs[i].file, g_p.docs[i].ed->text ());
	char start[80] = "\n";
	if (mf) { char fn[64]; form_name_of (*mf, fn, sizeof fn); snprintf (start, sizeof start, "%s_Run\n", fn); }
	add_part (prog, g_parts, "(the start)", start);
	return true;
}
// The program compiled: its first error shown (in its file, at its line); true = none
static bool compile_check (bas::Program **keep, bool quiet)
{
	Str prog;
	if (!build (prog, quiet)) return false;
	for (int i = 0; i < g_p.docs.n; i++) if (g_p.docs[i].type != DOC_FORM) { g_p.docs[i].ed->marks.clear (); g_p.docs[i].ed->invalidate (true); drop_problems (g_p.docs[i].file); }
	for (int i = g_probs.n - 1; i >= 0; i--) if (g_probs[i].file[0] == '(') g_probs.erase (i);
	bas::Error err;
	bas::Program *p = bas::compile (prog.str (), &err);
	if (!p)
	{
		const char *file = "?"; int at = 0;
		part_of (g_parts, err.line, &file, &at);
		add_problem (0, file, at, err.msg);
		int d = find_doc (file);
		if (d >= 0) { g_p.docs[d].ed->marks.push (at); g_p.docs[d].ed->invalidate (true); }
		g_msgs->invalidate (true);
		if (!quiet && d >= 0) { show_doc (d); g_p.docs[d].ed->gotoLine (at - 1); g_p.docs[d].ed->setFocus (); }
		char m[200]; snprintf (m, sizeof m, "%s %d: %s", file, at, err.msg); status (m);
		return false;
	}
	g_msgs->invalidate (true);
	if (keep) *keep = p; else bas::destroy (p);
	return true;
}
static void check_program (bool quiet) { compile_check (0, quiet); }

// ---- the code: SUBs found, written --------------------------------------------------------------------------------------
// The line (0-based) of "SUB name" in a text, else -1
static int sub_line (const char *t, const char *name)
{
	int ln = 0; int L = (int) strlen (name);
	for (const char *p = t; *p; )
	{
		const char *q = p; while (*q == ' ' || *q == '\t') q++;
		int kw = !strncasecmp (q, "SUB ", 4) ? 4 : !strncasecmp (q, "FUNCTION ", 9) ? 9 : 0;
		if (kw) { q += kw; while (*q == ' ') q++; if (!strncasecmp (q, name, L) && !name_ch (q[L]) && q[L] != '.') return ln; }
		while (*p && *p != '\n') p++;
		if (*p) p++;
		ln++;
	}
	return -1;
}
static void open_sub (const char *name)
{
	for (int i = 0; i < g_p.docs.n; i++)
	{
		if (g_p.docs[i].type != DOC_CODE) continue;
		int ln = sub_line (g_p.docs[i].ed->text (), name);
		if (ln >= 0) { show_doc (i); g_p.docs[i].ed->gotoLine (ln + 1); g_p.docs[i].ed->setFocus (); return; }
	}
	Doc *c = code_for_form (); if (!c) return;
	int i = (int) (c - &g_p.docs[0]);
	// written at the end: SUB name / (the caret) / END SUB
	CodeEdit *ed = c->ed;
	Str t; t.puts (ed->text ());
	while (t.n && t.b[t.n - 1] == '\n') t.b[--t.n] = 0;
	t.printf ("\n\nSUB %s\n  ", name);
	int caret = t.n;
	t.puts ("\nEND SUB\n");
	show_doc (i);
	ed->replaceAll (t.str (), caret);
	ed->setFocus ();
	refresh_outline ();
	char m[120]; snprintf (m, sizeof m, "SUB %s written", name); status (m);
}

// The completion: a control's properties and methods, the names
static void complete (const char *obj, Vec<Compl> &out)
{
	auto add = [&] (const char *n, char k, const char *d) { Compl c; cpy (c.name, n, sizeof c.name); c.kind = k; cpy (c.detail, d, sizeof c.detail); out.push (c); };
	Doc *f = main_form (); Form *form = f ? f->form : 0;
	char fn[64] = ""; if (f) form_name_of (*f, fn, sizeof fn);
	if (obj[0])
	{
		if (fn[0] && ieq (obj, fn)) { add ("Width", 'p', "INTEGER"); add ("Height", 'p', "INTEGER"); add ("Close", 'm', ""); return; }
		El *e = form ? form->named (obj) : 0;
		if (!e) return;
		add ("Text", 'p', "STRING");
		if (e->kind == K_SLIDER || e->kind == K_PROGRESS || e->kind == K_LISTBOX || e->kind == K_DROPDOWN) add ("Value", 'p', "DOUBLE");
		if (e->kind == K_CHECKBOX) add ("Checked", 'p', "-1 / 0");
		add ("Enabled", 'p', "-1 / 0"); add ("Visible", 'p', "-1 / 0");
		add ("Focus", 'm', ""); add ("Move", 'm', "x, y, w, h");
		if (e->kind == K_LISTBOX || e->kind == K_DROPDOWN) add ("AddItem", 'm', "s$");
		return;
	}
	if (fn[0]) add (fn, 'c', "Window");
	if (form)
	{
		struct Wk { static void walk (El *e, Vec<Compl> &o) {
			if (e->name[0] && e->kind != K_WINDOW) { Compl c; cpy (c.name, e->name, sizeof c.name); c.kind = 'c'; cpy (c.detail, e->kind == K_MENUITEM ? "Menu item" : KIND_NAMES[e->kind], sizeof c.detail); o.push (c); }
			for (int i = 0; i < e->kids.n; i++) walk (e->kids[i], o); } };
		Wk::walk (form->root, out);
	}
	Vec<char *> subs; all_subs (subs);
	for (int i = 0; i < subs.n; i++) add (subs[i], 's', "SUB");
	free_subs (subs);
	for (int i = 0; i < g_nwords; i++) { char w[40]; int l = imin (g_wlen[i], 39); memcpy (w, g_word[i], l); w[l] = 0; add (w, 'k', ""); }
}

// ---- the views ---------------------------------------------------------------------------------------------------------------
static void refresh_tree ()
{
	g_tree->clear ();
	int root = g_tree->add (-1, g_p.title[0] ? g_p.title : g_p.name);
	int forms = g_tree->add (root, "Forms"), code = g_tree->add (root, "Code"), gen = g_tree->add (root, "Generated"), res = g_tree->add (root, "Resources");
	for (int i = 0; i < g_p.docs.n; i++)
	{
		int par = g_p.docs[i].type == DOC_FORM ? forms : g_p.docs[i].type == DOC_CODE ? code : g_p.docs[i].type == DOC_GEN ? gen : res;
		int n = g_tree->add (par, g_p.docs[i].file); g_tree->setUserData (n, i + 1);
	}
	int a = g_tree->add (res, "project.ini"); g_tree->setUserData (a, 0);
	g_tree->expand (root, true); g_tree->expand (forms, true); g_tree->expand (code, true); g_tree->expand (gen, true); g_tree->expand (res, true);
	g_tree->invalidate (true);
}
static void tree_pick (Widget &)
{
	int id = g_tree->sel; if (id < 0) return;
	int d = g_tree->userData (id) - 1;
	if (d >= 0 && d < g_p.docs.n) show_doc (d);
}
// The outline: the code's SUBs (in the code), a click goes there
static Vec<int> g_outLines;
static void refresh_outline ()
{
	Doc *d = cur_doc ();
	g_outline->clear (); g_outLines.clear ();
	if (!d || d->type == DOC_FORM) return;
	const char *t = d->ed->text (); int ln = 0;
	for (const char *p = t; *p; )
	{
		const char *q = p; while (*q == ' ' || *q == '\t') q++;
		if (!strncasecmp (q, "SUB ", 4) || !strncasecmp (q, "FUNCTION ", 9) || !strncasecmp (q, "PROPERTY ", 9) || !strncasecmp (q, "TYPE ", 5)
		    || !strncasecmp (q, "CLASS ", 6) || !strncasecmp (q, "INTERFACE ", 10) || !strncasecmp (q, "VIRTUAL ", 8) || !strncasecmp (q, "OVERRIDE ", 9) || !strncasecmp (q, "ABSTRACT ", 9))
		{
			char s[64]; int k = 0; while (q[k] && q[k] != '\n' && q[k] != '(' && k < 60) { s[k] = q[k]; k++; }
			s[k] = 0; g_outline->add (s); g_outLines.push (ln);
		}
		while (*p && *p != '\n') p++;
		if (*p) p++;
		ln++;
	}
}
static void outline_pick (Widget &)
{
	Doc *d = cur_doc (); int i = g_outline->sel;
	if (!d || i < 0 || i >= g_outLines.n) return;
	d->ed->gotoLine (g_outLines[i]); d->ed->setFocus ();
}
// The object and event lists above the code
static Vec<char *> g_objNames; static const char **g_objOpts; static const char *g_evOpts[4]; static char g_evNames[4][64];
static void refresh_events ()
{
	int o = g_objBox->sel;
	Doc *f = main_form ();
	int n = 0;
	if (f && f->form && o >= 0 && o < g_objNames.n)
	{
		El *e = o == 0 ? f->form->root : f->form->named (g_objNames[o]);
		const char *labels[4];
		if (e) n = events_of (e, g_evNames, labels, 4);
		for (int i = 0; i < n; i++) g_evOpts[i] = labels[i];
	}
	if (!n) { g_evOpts[0] = "(no events)"; n = 1; }
	g_evBox->setOptions (g_evOpts, n, -1);
	g_evBox->invalidate (true);
}
static void refresh_codebar ()
{
	for (int i = 0; i < g_objNames.n; i++) free (g_objNames[i]);
	g_objNames.clear (); free (g_objOpts); g_objOpts = 0;
	Doc *f = main_form ();
	if (f && f->form)
	{
		char fn[64]; form_name_of (*f, fn, sizeof fn);
		g_objNames.push (strdup (fn));
		struct Wk { static void walk (El *e, Vec<char *> &o) { if (e->name[0] && e->kind != K_WINDOW && event_of (e->kind)) o.push (strdup (e->name)); for (int i = 0; i < e->kids.n; i++) walk (e->kids[i], o); } };
		Wk::walk (f->form->root, g_objNames);
	}
	g_objOpts = (const char **) malloc (sizeof (char *) * (g_objNames.n + 1));
	for (int i = 0; i < g_objNames.n; i++) g_objOpts[i] = g_objNames[i];
	int keep = g_objBox->sel;
	g_objBox->setOptions (g_objOpts, g_objNames.n, keep >= 0 && keep < g_objNames.n ? keep : (g_objNames.n ? 0 : -1));
	refresh_events ();
}
static void obj_picked (Widget &) { refresh_events (); }
static void ev_picked (Widget &)
{
	int i = g_evBox->sel; Doc *f = main_form ();
	if (!f || !f->form || i < 0 || strcmp (g_evOpts[0], "(no events)") == 0) return;
	open_sub (g_evNames[i]);
}

static void show_doc (int i)
{
	if (i < 0 || i >= g_p.docs.n) return;
	g_props->commit ();
	Doc &d = g_p.docs[i];
	if (!d.open) { d.open = true; g_p.tabs.push (i); }
	g_p.cur = i;
	for (int k = 0; k < g_p.docs.n; k++) g_p.docs[k].ed->hidden = k != i || (d.type == DOC_FORM && g_view == 0);
	bool form = d.type == DOC_FORM;
	g_des->hidden = !form || g_view == 2;
	g_tools->hidden = !form; g_outline->hidden = form;
	g_codebar->hidden = form;
	g_hTools->set (form ? "Toolbox" : "Outline", form ? "" : d.file);
	if (form)
	{
		g_reading = true;
		g_des->setForm (d.form);
		El *s = d.form && d.selN ? el_by_path (*d.form, d.selPath, d.selN) : d.form ? d.form->root : 0;
		g_des->select (s, false); g_props->show (s);
		g_reading = false;
	}
	else { g_props->show (0); refresh_codebar (); }
	refresh_outline ();
	relayout ();
	cpy (g_status->sLeft, d.file, sizeof g_status->sLeft);
	g_status->invalidate (true);
	g_tabs->invalidate (true);
	if (!form || g_view == 2) d.ed->setFocus (); else g_des->ov->setFocus ();
}
static void view_changed (Widget &)
{
	g_view = g_seg->selected;
	Doc *d = cur_doc ();
	if (d && d->type != DOC_FORM && g_view != 2) { Doc *f = main_form (); if (f) { show_doc ((int) (f - &g_p.docs[0])); return; } }
	if (d && d->type == DOC_FORM && g_view == 2 && false) {}
	if (d) show_doc (g_p.cur);
}

static void on_text_changed (Widget &w)
{
	for (int i = 0; i < g_p.docs.n; i++)
	{
		Doc &d = g_p.docs[i];
		if (d.ed != &w) continue;
		if (d.type == DOC_GEN) return;
		if (!d.dirty) { d.dirty = true; g_tabs->invalidate (true); }
		if (d.type == DOC_FORM)
		{
			if (!g_reading && g_des->sel) d.selN = path_of (g_des->sel, d.selPath, 32);
			read_form (d);
		}
		else g_checkAt = kapi_get_ticks () + 35;		// (checked when the typing pauses)
		return;
	}
}
static void on_caret (Widget &w)
{
	CodeEdit &ed = (CodeEdit &) w;
	char t[48]; snprintf (t, sizeof t, "Ln %d, Col %d", ed.caretLine () + 1, ed.caretCol () + 1);
	cpy (g_status->sRight, t, sizeof g_status->sRight); g_status->invalidate (true);
	Doc *d = cur_doc ();
	// the form's text: the element of the caret's line chosen in the designer
	if (d && d->ed == &ed && d->type == DOC_FORM && d->form && !g_reading && ed.hasFocus)
	{
		El *e = el_at_line (d->form->root, ed.caretLine () + 1);
		if (e && e != g_des->sel) { g_reading = true; g_des->select (e, false); g_props->show (e); d->selN = path_of (e, d->selPath, 32); g_reading = false; }
	}
}

// ---- the project: read, written ----------------------------------------------------------------------------------------------
static void ini_get (const char *ini, const char *key, char *out, int cap)
{
	int L = (int) strlen (key);
	for (const char *p = ini; *p; )
	{
		while (*p == ' ' || *p == '\t') p++;
		if (!strncasecmp (p, key, L) && (p[L] == ' ' || p[L] == '='))
		{
			const char *v = p + L; while (*v == ' ' || *v == '=') v++;
			int k = 0; while (v[k] && v[k] != '\n' && v[k] != '\r' && k < cap - 1) { out[k] = v[k]; k++; }
			while (k > 0 && out[k - 1] == ' ') k--;
			out[k] = 0; return;
		}
		while (*p && *p != '\n') p++;
		if (*p) p++;
	}
}
static void clear_project ()
{
	for (int i = 0; i < g_p.docs.n; i++) { g_root_remove (g_p.docs[i].ed); delete g_p.docs[i].ed; delete g_p.docs[i].form; }
	g_p.docs.clear (); g_p.tabs.clear (); g_p.cur = -1;
	g_probs.clear ();
	g_des->setForm (0); g_props->show (0);
}
static bool load_project (const char *dir)
{
	char ip[260]; join (ip, sizeof ip, dir, "project.ini");
	char *ini = read_file (ip);
	if (!ini) { char m[300]; snprintf (m, sizeof m, "No project.ini in %s", dir); uk_messagebox ("QBStudio", m, MB_OK); return false; }
	clear_project ();
	cpy (g_p.dir, dir, sizeof g_p.dir);
	{ int n = (int) strlen (g_p.dir); if (n > 1 && g_p.dir[n - 1] == '/') g_p.dir[n - 1] = 0; }
	ini_get (ini, "name", g_p.name, sizeof g_p.name); if (!g_p.name[0]) cpy (g_p.name, base_name (g_p.dir), sizeof g_p.name);
	ini_get (ini, "title", g_p.title, sizeof g_p.title); if (!g_p.title[0]) cpy (g_p.title, g_p.name, sizeof g_p.title);
	ini_get (ini, "main", g_p.mainForm, sizeof g_p.mainForm); if (!g_p.mainForm[0]) cpy (g_p.mainForm, "Main", sizeof g_p.mainForm);
	ini_get (ini, "category", g_p.category, sizeof g_p.category); if (!g_p.category[0]) cpy (g_p.category, "BASIC", sizeof g_p.category);
	char c[16] = "yes"; ini_get (ini, "compiled", c, sizeof c); g_p.compiled = !ieq (c, "no");
	char files[512] = ""; ini_get (ini, "files", files, sizeof files);
	free (ini);
	// the files (a form, its code; then the rest)
	for (char *p = files; *p; )
	{
		while (*p == ' ') p++;
		char f[64]; int k = 0; while (*p && *p != ' ' && k < 63) f[k++] = *p++;
		f[k] = 0; if (!k) break;
		char fp[300]; join (fp, sizeof fp, g_p.dir, f);
		char *t = read_file (fp);
		int type = ends_with (f, ".form") ? DOC_FORM : ends_with (f, ".bas") ? DOC_CODE : DOC_TEXT;
		add_doc (f, type, t ? t : "");
		free (t);
	}
	// each form: its generated code (a read-only doc), its tree
	int n = g_p.docs.n;
	for (int i = 0; i < n; i++)
	{
		if (g_p.docs[i].type != DOC_FORM) continue;
		char gn[64]; snprintf (gn, sizeof gn, "%s.bas", g_p.docs[i].file);
		add_doc (gn, DOC_GEN, "");
		g_p.docs[i].gen = g_p.docs.n - 1;
	}
	for (int i = 0; i < g_p.docs.n; i++) if (g_p.docs[i].type == DOC_FORM) read_form (g_p.docs[i]);
	for (int i = 0; i < g_p.docs.n; i++) g_p.docs[i].dirty = false;
	refresh_tree ();
	Doc *mf = main_form ();
	show_doc (mf ? (int) (mf - &g_p.docs[0]) : 0);
	Doc *cf = code_for_form (); if (cf && !cf->open) { cf->open = true; g_p.tabs.push ((int) (cf - &g_p.docs[0])); }
	write_file (LAST, g_p.dir);
	check_program (true);
	status ("Project opened");
	return true;
}
static void save_project_ini ()
{
	Str s;
	s.puts ("# a QBStudio project: its window, its code, what Make App writes\n[project]\n");
	s.printf ("name = %s\ntitle = %s\nmain = %s\nfiles =", g_p.name, g_p.title, g_p.mainForm);
	for (int i = 0; i < g_p.docs.n; i++) if (g_p.docs[i].type != DOC_GEN) s.printf (" %s", g_p.docs[i].file);
	s.printf ("\ncategory = %s\ncompiled = %s\n", g_p.category, g_p.compiled ? "yes" : "no");
	char p[260]; join (p, sizeof p, g_p.dir, "project.ini");
	write_file (p, s.str ());
}
static bool save_all ()
{
	if (!g_p.dir[0]) return false;
	bool ok = true;
	for (int i = 0; i < g_p.docs.n; i++)
	{
		Doc &d = g_p.docs[i];
		if (d.type == DOC_GEN) for (int k = 0; k < g_p.docs.n; k++) if (g_p.docs[k].gen == i) regenerate (g_p.docs[k]);
		if (!d.dirty && d.type != DOC_GEN) continue;
		char p[300]; join (p, sizeof p, g_p.dir, d.file);
		if (!write_file (p, d.ed->text ())) ok = false; else d.dirty = false;
	}
	save_project_ini ();
	g_tabs->invalidate (true);
	status (ok ? "Saved" : "Some files could not be written");
	return ok;
}
static bool changed () { for (int i = 0; i < g_p.docs.n; i++) if (g_p.docs[i].dirty) return true; return false; }
static bool confirm_close ()
{
	if (!changed ()) return true;
	int r = uk_messagebox ("QBStudio", "Save the project's changes?", MB_YESNO);
	if (r == 1) save_all ();
	return true;
}

// New project: its folder, its window from a template
static const char *const T_WINDOW_FORM =
"# Main.form -- the window (QBStudio's designer writes it; it reads as you see it)\n"
"Window Main \"%s\" size=360x220 resizable\n"
"  Column padding=14 gap=10\n"
"    Label \"Your name:\"\n"
"    TextBox who \"\" fill\n"
"    Label hello \"\" fill\n"
"    Spacer\n"
"    Row align=right\n"
"      Button greet \"Say hello\" default\n";
static const char *const T_WINDOW_CODE =
"' Main.bas -- what the app does (its window: Main.form)\n\n"
"SUB Main_Load\n  who.Focus\nEND SUB\n\n"
"SUB greet_Click\n  hello.Text = \"Hello, \" + who.Text + \"!\"\nEND SUB\n";
static const char *const T_DOC_FORM =
"# Main.form -- the window (QBStudio's designer writes it; it reads as you see it)\n"
"Window Main \"%s\" size=520x360 min=360x240 resizable\n"
"  Menu\n"
"    \"&File\"\n"
"      \"&New\" name=mnuNew key=Ctrl+N\n"
"      \"&Open...\" name=mnuOpen key=Ctrl+O\n"
"      \"&Save\" name=mnuSave key=Ctrl+S\n"
"      -\n"
"      \"&Quit\" name=mnuQuit key=Ctrl+Q\n"
"  Column padding=10 gap=8\n"
"    Row gap=8\n"
"      TextBox line \"\" fill\n"
"      Button add \"Add\" default\n"
"    ListBox items fill grow=1\n"
"  StatusBar status \"Ready\"\n";
static const char *const T_DOC_CODE =
"' Main.bas -- a list kept in a file: New, Open, Save (its window: Main.form)\n\n"
"DIM SHARED path$\n\n"
"SUB Main_Load\n  line.Focus\nEND SUB\n\n"
"SUB add_Click\n  IF line.Text <> \"\" THEN items.AddItem line.Text: line.Text = \"\"\n  line.Focus\nEND SUB\n\n"
"SUB mnuNew_Click\n  r = MSGBOX(\"New\", \"Start a new list?\", 1)\nEND SUB\n\n"
"SUB mnuOpen_Click\n  f$ = OPENFILE$(\"SD:/docs\")\n  IF f$ = \"\" THEN EXIT SUB\n  path$ = f$\n"
"  OPEN f$ FOR INPUT AS #1\n  DO WHILE NOT EOF(1)\n    LINE INPUT #1, a$\n    items.AddItem a$\n  LOOP\n  CLOSE #1\n  status.Text = f$\nEND SUB\n\n"
"SUB mnuSave_Click\n  IF path$ = \"\" THEN path$ = SAVEFILE$(\"SD:/docs\", \"list.txt\")\n  IF path$ = \"\" THEN EXIT SUB\n"
"  status.Text = \"Saved: \" + path$\nEND SUB\n\n"
"SUB mnuQuit_Click\n  Main.Close\nEND SUB\n";
static const char *const T_EMPTY_FORM =
"# Main.form -- the window (QBStudio's designer writes it; it reads as you see it)\n"
"Window Main \"%s\" size=400x280 resizable\n"
"  Column padding=12 gap=8\n";
static const char *const T_EMPTY_CODE = "' Main.bas -- what the app does (its window: Main.form)\n\nSUB Main_Load\nEND SUB\n";

class NewDialog : public Modal
{
public:
	Textbox *name, *title; RadioButton *r[3];
	NewDialog () : Modal (460, 300)
	{
		Root *rt = Root::current (); if (rt) { left = imax (0, (rt->width - width) / 2); top = imax (0, (rt->height - height) / 2); }
		int y = titleH () + 14;
		name = new Textbox (150, y, 290, 26, "myapp"); addChild (name);
		title = new Textbox (150, y + 34, 290, 26, "My App"); addChild (title);
		static const char *const L[3] = { "A window (controls in a column)", "A document app (menu, list, status bar)", "An empty window" };
		for (int i = 0; i < 3; i++) { r[i] = new RadioButton (24, y + 96 + i * 28, 400, 24, L[i], 1, i == 0, 0, C_FACE); addChild (r[i]); }
		Button *ok = new Button (width - 184, height - 42, 82, 28, "Create", act); ok->tag = 1; addChild (ok);
		Button *no = new Button (width - 94, height - 42, 82, 28, "Cancel", act); no->tag = 0; addChild (no);
	}
	void onDraw () override
	{
		drawBox ("New Project");
		int y = titleH () + 14;
		canvas.text (16, y + 5, "Folder name", C_TEXT); canvas.text (16, y + 39, "Title", C_TEXT);
		canvas.text (16, y + 72, "Start from:", C_TEXT);
	}
	int kind () { for (int i = 0; i < 3; i++) if (r[i]->checked) return i; return 0; }
	bool onKey (long k) override { if (k == 27) { close (0); return true; } return false; }
	void onButton (int tag) override { close (tag); }
	static void act (Widget &w) { Widget *p = w.parent; while (p && !p->modal) p = p->parent; if (p) ((Modal *) p)->onButton (w.tag); }
};
static void cmd_new ()
{
	NewDialog d; d.name->setFocus ();
	if (d.run () != 1) return;
	char name[40]; cpy (name, d.name->text, sizeof name);
	for (int i = 0; name[i]; i++) if (!name_ch (name[i])) { uk_messagebox ("New Project", "The folder's name: letters, digits and _ only", MB_OK); return; }
	if (!name[0]) return;
	confirm_close ();
	char dir[200]; snprintf (dir, sizeof dir, "SD:/projects/%s", name);
	char ip[260]; join (ip, sizeof ip, dir, "project.ini");
	if (file_exists (ip)) { uk_messagebox ("New Project", "There is a project of that name already", MB_OK); return; }
	kapi_mkdir ("SD:/projects"); kapi_mkdir (dir);
	int k = d.kind ();
	const char *form = k == 0 ? T_WINDOW_FORM : k == 1 ? T_DOC_FORM : T_EMPTY_FORM;
	const char *code = k == 0 ? T_WINDOW_CODE : k == 1 ? T_DOC_CODE : T_EMPTY_CODE;
	char ft[2048]; snprintf (ft, sizeof ft, form, d.title->text);
	char p[260];
	join (p, sizeof p, dir, "Main.form"); write_file (p, ft);
	join (p, sizeof p, dir, "Main.bas"); write_file (p, code);
	Str ini; ini.printf ("# a QBStudio project: its window, its code, what Make App writes\n[project]\nname = %s\ntitle = %s\nmain = Main\nfiles = Main.form Main.bas\ncategory = BASIC\ncompiled = yes\n", name, d.title->text);
	write_file (ip, ini.str ());
	load_project (dir);
}
static void cmd_open ()
{
	char p[256];
	if (!uk_file_open (p, sizeof p, "SD:/projects")) return;
	confirm_close ();
	// the project: the folder of the file chosen
	char dir[256]; cpy (dir, p, sizeof dir);
	char *b = (char *) base_name (dir); if (b > dir) b[-1] = 0;
	load_project (dir);
}
static void cmd_save () { save_all (); }
static void cmd_add_module ()
{
	if (!g_p.dir[0]) return;
	char n[40] = "Module1";
	if (!ask_line ("Add Module", "The module's name (its file: <name>.bas)", n, sizeof n)) return;
	for (int i = 0; n[i]; i++) if (!name_ch (n[i])) { uk_messagebox ("Add Module", "Letters, digits and _ only", MB_OK); return; }
	char f[64]; snprintf (f, sizeof f, "%s.bas", n);
	if (find_doc (f) >= 0) return;
	char t[200]; snprintf (t, sizeof t, "' %s -- (a module of %s: its SUBs and FUNCTIONs)\n\n", f, g_p.title);
	Doc &d = add_doc (f, DOC_CODE, t); d.dirty = true;
	refresh_tree ();
	show_doc (g_p.docs.n - 1);
	save_all ();
}
static void cmd_project_settings ()
{
	if (!g_p.dir[0]) return;
	char t[64]; cpy (t, g_p.title, sizeof t);
	if (!ask_line ("The project", "The app's title (shown under its icon)", t, sizeof t)) return;
	cpy (g_p.title, t, sizeof g_p.title);
	char c[32]; cpy (c, g_p.category, sizeof c);
	if (ask_line ("The project", "Its category (in the app list)", c, sizeof c)) cpy (g_p.category, c, sizeof g_p.category);
	g_p.compiled = uk_messagebox ("The project", "Make App: compiled (main.bax, it starts at once; the source not in the app)?", MB_YESNO) == 1;
	save_project_ini (); refresh_tree ();
}

// ---- Run, Make App --------------------------------------------------------------------------------------------------------------
static void cmd_run ()
{
	if (!g_p.dir[0]) return;
	g_props->commit ();
	save_all ();
	bas::Program *p = 0;
	if (!compile_check (&p, false)) return;
	bas::destroy (p);
	Str prog; build (prog, false);
	kapi_mkdir ("SD:/tmp"); kapi_mkdir ("SD:/tmp/qbstudio");
	char tmp[200]; snprintf (tmp, sizeof tmp, "SD:/tmp/qbstudio/%s.bas", g_p.name);
	if (!write_file (tmp, prog.str ())) { status ("Cannot write SD:/tmp/qbstudio"); return; }
	char args[600]; snprintf (args, sizeof args, "-s qbstudio -d \"%s\" \"%s\"", g_p.dir, tmp);
	if (!kapi_exec ("SD:/bin/basic", args)) { status ("Cannot start SD:/bin/basic"); return; }
	snprintf (g_msgs->output, sizeof g_msgs->output, "Running %s", g_p.title);
	g_msgs->invalidate (true);
	char m[120]; snprintf (m, sizeof m, "Running %s", g_p.title); status (m);
}
static void cmd_check () { if (compile_check (0, false)) status ("No problems: the program compiles"); }
static void cmd_make_app ()
{
	if (!g_p.dir[0]) return;
	save_all ();
	bas::Program *p = 0;
	if (!compile_check (&p, false)) return;
	char dir[128]; snprintf (dir, sizeof dir, "SD:/apps/%s.app", g_p.name);
	kapi_mkdir (dir);
	char path[200], other[200];
	join (path, sizeof path, dir, g_p.compiled ? "main.bax" : "main.bas");
	join (other, sizeof other, dir, g_p.compiled ? "main.bas" : "main.bax");
	bool ok;
	if (g_p.compiled)
	{
		char *bytes; int n = bas::saveBax (p, &bytes);
		ok = kapi_save_file (path, bytes, (unsigned) n) >= 0;
		delete[] bytes;
	}
	else { Str prog; build (prog, true); ok = write_file (path, prog.str ()); }
	bas::destroy (p);
	if (!ok) { char m[260]; snprintf (m, sizeof m, "Cannot write %s", path); status (m); return; }
	kapi_remove (other);
	Str txt; txt.printf ("# Onyx application metadata (written by QBStudio > Make App)\nname = %s\ncategory = %s\n", g_p.title, g_p.category);
	join (path, sizeof path, dir, "app.txt"); write_file (path, txt.str ());
	// the icon: the project's icon.bmp, else BASIC's
	char ic[260]; join (ic, sizeof ic, g_p.dir, "icon.bmp");
	void *f = kapi_open (ic); if (!f) f = kapi_open ("SD:/apps/qbasic.app/program.bmp");
	if (f)
	{
		unsigned sz = kapi_fsize (f); char *b = (char *) malloc (sz + 1);
		int r = kapi_read (f, b, sz); kapi_close (f);
		join (path, sizeof path, dir, "icon.bmp");
		if (r > 0) kapi_save_file (path, b, (unsigned) r);
		free (b);
	}
	char m[200]; snprintf (m, sizeof m, "App made: %s (in the app list)", dir); status (m);
	snprintf (g_msgs->output, sizeof g_msgs->output, "App made: %s", dir); g_msgs->invalidate (true);
	notify ("QBStudio", "The app is made: it is in the app list.");
}

// ---- the edits: to what has the focus ----------------------------------------------------------------------------------------
static CodeEdit *focused_ed () { Doc *d = cur_doc (); if (d && d->ed->hasFocus) return d->ed; return 0; }
static void cmd_undo () { Doc *d = cur_doc (); if (!d) return; d->ed->undo (); }
static void cmd_redo () { Doc *d = cur_doc (); if (!d) return; d->ed->redo (); }
static void cmd_cut () { CodeEdit *e = focused_ed (); if (e) e->cut (); else if (g_des->ov->hasFocus && g_des->sel && g_des->sel != g_des->form->root) { Str t; el_write (t, g_des->sel, 0); clip_set_text (t.str ()); g_des->removeSelected (); } }
static void cmd_copy () { CodeEdit *e = focused_ed (); if (e) e->copy (); else if (g_des->ov->hasFocus && g_des->sel) { Str t; el_write (t, g_des->sel, 0); clip_set_text (t.str ()); } }
static void cmd_paste ()
{
	CodeEdit *e = focused_ed (); if (e) { e->paste (); return; }
	Doc *d = cur_doc ();
	if (!d || d->type != DOC_FORM || !d->form || !g_des->ov->hasFocus) return;
	// elements copied: read as a form's lines, put after the selection
	char *b = (char *) malloc (65536); if (!b) return;
	int n = clip_get_text (b, 65536); if (n <= 0) { free (b); return; }
	b[n] = 0;
	Str src; src.puts ("Window Clip\n");
	for (char *p = b; *p; ) { src.puts ("  "); while (*p && *p != '\n') src.put (*p++); src.put ('\n'); if (*p) p++; }
	free (b);
	Form tmp; form_read (tmp, src.str ());
	if (!tmp.root || !tmp.root->kids.n) return;
	El *k = tmp.root->kids[0]; tmp.root->kids.erase (0);
	// a name in use: another
	struct Fix { static void names (Form &f, El *e) { if (e->name[0] && f.named (e->name)) { char base[32]; cpy (base, e->name, sizeof base); int l = (int) strlen (base); while (l && base[l - 1] >= '0' && base[l - 1] <= '9') base[--l] = 0; unique_name (f, base, e->name, sizeof e->name); } for (int i = 0; i < e->kids.n; i++) names (f, e->kids[i]); } };
	Fix::names (*d->form, k);
	El *s = g_des->sel ? g_des->sel : d->form->root;
	if (is_container (s->kind)) { g_des->dropPar = s; g_des->dropIdx = s->kids.n; }
	else { g_des->dropPar = s->parent ? s->parent : d->form->root; g_des->dropIdx = s->index () + 1; }
	g_des->dropOk = true;
	g_des->doDrop (k, false);
}
static void cmd_delete () { CodeEdit *e = focused_ed (); if (e) e->onKey (KEY_DEL); else g_des->removeSelected (); }
static void cmd_select_all () { CodeEdit *e = focused_ed (); if (e) e->selectAll (); }
static void cmd_find ()
{
	Doc *d = cur_doc (); if (!d) return;
	static char w[64] = "";
	if (!ask_line ("Find", "Find (in the file shown):", w, sizeof w) || !w[0]) return;
	if (d->type == DOC_FORM && g_view == 0) { g_view = 1; g_seg->select (1); show_doc (g_p.cur); }
	if (!d->ed->find (w)) status ("Not found"); else d->ed->setFocus ();
}
static void cmd_view_code ()
{
	Doc *d = cur_doc ();
	if (d && d->type == DOC_FORM) { Doc *c = code_for_form (); if (c) show_doc ((int) (c - &g_p.docs[0])); }
	else { Doc *f = main_form (); if (f) { if (g_view == 2) { g_view = 1; g_seg->select (1); } show_doc ((int) (f - &g_p.docs[0])); } }
}
static void cmd_design () { g_view = 0; g_seg->select (0); Doc *f = main_form (); if (f) show_doc ((int) (f - &g_p.docs[0])); }
static void cmd_split () { g_view = 1; g_seg->select (1); Doc *f = main_form (); if (f) show_doc ((int) (f - &g_p.docs[0])); }
static void cmd_form_text () { g_view = 2; g_seg->select (2); Doc *f = main_form (); if (f) show_doc ((int) (f - &g_p.docs[0])); }
static void cmd_generated () { Doc *f = main_form (); if (f && f->gen >= 0) show_doc (f->gen); }
// View > Grid, Snap to Grid: kept in settings.ini
static void save_settings () { char t[64]; snprintf (t, sizeof t, "grid = %d\nsnap = %d\n", g_showGrid ? 1 : 0, g_snap ? 1 : 0); write_file (SETTINGS, t); }
static void load_settings ()
{
	char *t = read_file (SETTINGS); if (!t) return;
	char v[8] = "";
	ini_get (t, "grid", v, sizeof v); if (v[0]) g_showGrid = v[0] != '0';
	v[0] = 0; ini_get (t, "snap", v, sizeof v); if (v[0]) g_snap = v[0] != '0';
	free (t);
}
static void cmd_grid () { g_showGrid = !g_showGrid; save_settings (); g_des->rebuild (); status (g_showGrid ? "The grid shown (dots every 5 px)" : "The grid hidden"); }
static void cmd_snap () { g_snap = !g_snap; save_settings (); status (g_snap ? "Snap to the grid: on (Alt while dragging: off)" : "Snap to the grid: off"); }
static void cmd_help () { kapi_exec ("SD:/apps/tinypad.app/main", "SD:/apps/qbstudio.app/help.txt"); }
static void cmd_about () { uk_messagebox ("About QBStudio", "QBStudio: desktop apps in Onyx BASIC -- a window drawn, its code written, run, made an app.", MB_OK); }
static void cmd_quit () { confirm_close (); kapi_exit (0); }
static void cmd_sample () { confirm_close (); load_project (SAMPLE); }

// ---- the window ------------------------------------------------------------------------------------------------------------------
// (a ToolButton's action is Action (Widget &): the commands are void (): a table maps each button to its)
static struct { ToolButton *b; void (*cb) (); } g_cmds[24]; static int g_ncmds;
static void tb_click (Widget &w) { for (int i = 0; i < g_ncmds; i++) if (g_cmds[i].b == &w) { g_cmds[i].cb (); return; } }
static ToolButton *button (int glyph, const char *tip, void (*cb) (), int gap = 1, const char *text = 0)
{
	ToolButton *b = new ToolButton (30, 28, tip, tb_click);
	if (glyph >= 0) b->setGlyph (glyph);
	if (text) { b->setText (text); b->fitWidth (); }
	g_tb->add (b, gap);
	g_cmds[g_ncmds].b = b; g_cmds[g_ncmds].cb = cb; g_ncmds++;
	return b;
}

class QsRoot : public Root
{
public:
	QsRoot () : Root (W0, H0, "QBStudio") {}
	void onResized () override { relayout (); }
	void onTick () override
	{
		g_props->tick ();
		static bool hadErrs = false;
		bool errs = false; for (int i = 0; i < g_probs.n; i++) if (g_probs[i].kind == 0) errs = true;
		if (errs != hadErrs) { hadErrs = errs; relayout (); }
		if (g_checkAt && (int) (kapi_get_ticks () - g_checkAt) >= 0)
		{
			g_checkAt = 0; check_program (true);
			Doc *f = main_form (); if (f) { regenerate (*f); if (f->form) set_problems_for (f->file, f->form); }
			refresh_outline ();
			if (g_props->el) g_props->invalidate (true);
		}
		// a runtime error from the program started: "line\0message\0"
		int from, type; char m[220];
		int n = kapi_mailbox_recv (&from, &type, m, sizeof m - 1, 0);
		if (n > 0)
		{
			m[n] = 0;
			int line = atoi (m); const char *msg = m + strlen (m) + 1;
			const char *file = "?"; int at = 0;
			if (part_of (g_parts, line, &file, &at))
			{
				add_problem (0, file, at, msg);
				int d = find_doc (file);
				if (d >= 0) { show_doc (d); g_p.docs[d].ed->marks.push (at); g_p.docs[d].ed->hiLine = at; g_p.docs[d].ed->gotoLine (at - 1); }
			}
			char t[260]; snprintf (t, sizeof t, "Runtime error: %s %d: %s", file, at, msg); status (t);
			g_msgs->invalidate (true);
		}
	}
	bool onKey (long k) override
	{
		if (k == KEY_F1 + 4) { cmd_run (); return true; }
		if (k == KEY_F1 + 6) { cmd_view_code (); return true; }
		return false;
	}
};

static void g_root_add (Widget *w) { g_root->addChild (w); }
static void g_root_remove (Widget *w) { g_root->removeChild (w); }

// (ListBox, TreeView have a `top` of their own: the scroll -- the place set as Widget's)
static void put (Widget *w, int x, int y, int ww, int hh) { w->Widget::left = x; w->Widget::top = y; w->resizeTo (ww, hh); }
static void relayout ()
{
	int W = g_root->width, H = g_root->height;
	put (g_tb, 0, 0, W, TB_H);
	g_seg->Widget::left = W - g_seg->width - 10;
	int top = TB_H, bottom = H - ST_H;
	put (g_status, 0, bottom, W, ST_H);
	// the left: the project, the toolbox (or the outline)
	put (g_hProject, 0, top, LEFT_W, HEAD_H);
	put (g_tree, 0, top + HEAD_H, LEFT_W, TREE_H);
	int ty = top + HEAD_H + TREE_H;
	put (g_hTools, 0, ty, LEFT_W, HEAD_H);
	put (g_tools, 0, ty + HEAD_H, LEFT_W, imax (10, bottom - ty - HEAD_H));
	put (g_outline, 4, ty + HEAD_H + 4, LEFT_W - 8, imax (10, bottom - ty - HEAD_H - 8));
	// the right: the properties
	put (g_hProps, W - RIGHT_W, top, RIGHT_W, HEAD_H);
	put (g_props, W - RIGHT_W, top + HEAD_H, RIGHT_W, bottom - top - HEAD_H);
	// the middle
	int cx = LEFT_W + 1, cw = W - LEFT_W - RIGHT_W - 2;
	put (g_tabs, cx, top, cw, TABS_H);
	int y = top + TABS_H;
	Doc *d = cur_doc ();
	bool form = d && d->type == DOC_FORM;
	// the problems: under the code; under a form only when there is an error
	int errs = 0; for (int i = 0; i < g_probs.n; i++) if (g_probs[i].kind == 0) errs++;
	int msgH = form && !errs ? 0 : MSG_H;
	g_msgs->hidden = msgH == 0;
	if (!form) { put (g_codebar, cx, y, cw, CODEBAR_H); y += CODEBAR_H; }
	int areaH = bottom - y - msgH;
	put (g_msgs, cx, bottom - msgH, cw, imax (1, msgH));
	if (d)
	{
		if (form)
		{
			int dh = g_view == 0 ? areaH : g_view == 1 ? areaH * 58 / 100 : 0;
			if (g_view != 2) { put (g_des, cx, y, cw, dh); }
			if (g_view != 0) { put (d->ed, cx, y + dh, cw, areaH - dh); }
		}
		else { put (d->ed, cx, y, cw, areaH); }
	}
	// the code bar's lists
	g_objBox->Widget::left = g_codebar->left + 8; g_objBox->Widget::top = g_codebar->top + 4;
	g_evBox->Widget::left = g_objBox->left + g_objBox->width + 8; g_evBox->Widget::top = g_codebar->top + 4;
	g_objBox->hidden = g_evBox->hidden = g_codebar->hidden;
	// (above the code: their lists open over it; the toolbox last, for its drags)
	g_objBox->bringToFront (); g_evBox->bringToFront (); g_tools->bringToFront ();
	g_root->invalidate (true);
}

class CodeBar : public Widget
{
public:
	CodeBar () : Widget (0, 0, 10, CODEBAR_H) {}
	void onDraw () override
	{
		canvas.clear (uk_mix (C_BG, C_TEXT, 10));
		int x = g_evBox->left - left + g_evBox->width + 12;
		const char *h = "the object, its event: its SUB";
		if (x + uk_tw (h, 1) < width - 4) uk_text (canvas, x, (height - uk_fh ()) / 2, h, uk_mix (C_BG, C_TEXT, 130), 1);
	}
};

static int text_w_face (const char *s) { char t[160]; plain (s, t, sizeof t); return uk_tw (t); }

int main (void)
{
	char args[256];
	int an = kapi_get_args (args, sizeof args);
	if (an < 0) an = 0;
	args[an < (int) sizeof args ? an : (int) sizeof args - 1] = 0;
	ft_uikit_install ("DejaVu Sans", 13);
	g_ui = ft_uikit_face ();
	{ FtTextFace *m = new FtTextFace; if (m->open ("DejaVu Sans Mono", 13)) g_mono = m; else delete m; }
	QsRoot root; g_root = &root;
	root.attach ();
	uikit::init ();
	load_words ();
	load_settings ();
	g_isKeyword = is_keyword; g_isWord = is_word0; g_textW = text_w_face;
	g_complete = complete;
	g_onSelect = on_select; g_onFormEdited = form_edited; g_onElementOpen = on_element_open; g_onDesignHint = status;
	g_subExists = sub_exists; g_openSub = open_sub;
	kapi_ipc_register ("qbstudio");

	// the toolbar
	g_tb = new ToolBar (0, 0, W0, TB_H); root.addChild (g_tb);
	button (WKT_NEW, "New project...", cmd_new);
	button (WKT_OPEN, "Open a project... (Ctrl+O)", cmd_open);
	button (WKT_SAVE, "Save all (Ctrl+S)", cmd_save);
	g_tb->sep ();
	button (WKT_UNDO, "Undo (Ctrl+Z)", cmd_undo);
	button (WKT_REDO, "Redo (Ctrl+Y)", cmd_redo);
	g_tb->sep ();
	button (WKT_CUT, "Cut (Ctrl+X)", cmd_cut);
	button (WKT_COPY, "Copy (Ctrl+C)", cmd_copy);
	button (WKT_PASTE, "Paste (Ctrl+V)", cmd_paste);
	g_tb->sep ();
	button (WKT_SEARCH, "Find... (Ctrl+F)", cmd_find);
	g_tb->sep ();
	g_runBtn = button (WKT_PLAY, "Run (F5): build the program and start it", cmd_run, 4, "Run");
	g_runBtn->filled = true; g_runBtn->raised = true; g_runBtn->onColor = 0x2E9E4A; g_runBtn->iconColor = 0x2E9E4A;
	button (WKT_SPARK, "Check: compile without running", cmd_check, 4, "Check");
	button (WKT_GEAR, "Make App: the program as an app (SD:/apps/<name>.app)", cmd_make_app, 4, "Make App");
	static const char *const VIEWS[3] = { "Design", "Split", "Code" };
	g_seg = new SegmentedControl (W0 - 200, (TB_H - 26) / 2, 190, 26, VIEWS, 3, 1, view_changed);
	g_seg->anchor = ANCHOR_TOP | ANCHOR_RIGHT;
	g_tb->addChild (g_seg);

	// the panes
	g_hProject = new PaneTitle ("Project"); root.addChild (g_hProject);
	g_tree = new TreeView (0, 0, LEFT_W, TREE_H, tree_pick, tree_pick); root.addChild (g_tree);
	g_hTools = new PaneTitle ("Toolbox"); root.addChild (g_hTools);
	g_outline = new ListBox (0, 0, LEFT_W, 100, outline_pick, outline_pick); root.addChild (g_outline);
	g_hProps = new PaneTitle ("Properties"); root.addChild (g_hProps);
	g_props = new PropGrid (0, 0, RIGHT_W, 300); root.addChild (g_props);
	g_tabs = new DocTabs (); root.addChild (g_tabs);
	g_codebar = new CodeBar (); root.addChild (g_codebar);
	g_objBox = new Dropdown (8, 4, 200, 26, 0, 0, -1, obj_picked); root.addChild (g_objBox);
	g_evBox = new Dropdown (216, 4, 160, 26, 0, 0, -1, ev_picked); root.addChild (g_evBox);
	g_msgs = new MsgList (); root.addChild (g_msgs);
	g_status = new StatusLine (); root.addChild (g_status);
	g_des = new Designer (0, 0, 400, 300); root.addChild (g_des);
	g_tools = new Toolbox (0, 0, LEFT_W, 200, g_des); g_tools->onPick = [] (int k) { g_des->toolAdd (k); };
	root.addChild (g_tools);		// (last: a control dragged from it over the designer still reaches it)
	root.setResizable (true);

	static Menu menu;
	menu.menu ("File");
	menu.item ("New Project...", "", 0, cmd_new);
	menu.item ("Open Project...", "^O", UK_CTRL ('O'), cmd_open);
	menu.item ("Open the Example", "", 0, cmd_sample);
	menu.separator ();
	menu.item ("Save All", "^S", UK_CTRL ('S'), cmd_save);
	menu.separator ();
	menu.item ("Make App", "", 0, cmd_make_app);
	menu.separator ();
	menu.item ("Quit", "^Q", UK_CTRL ('Q'), cmd_quit);
	menu.menu ("Edit");
	menu.item ("Undo", "^Z", UK_CTRL ('Z'), cmd_undo);
	menu.item ("Redo", "^Y", UK_CTRL ('Y'), cmd_redo);
	menu.separator ();
	menu.item ("Cut", "^X", UK_CTRL ('X'), cmd_cut);
	menu.item ("Copy", "^C", UK_CTRL ('C'), cmd_copy);
	menu.item ("Paste", "^V", UK_CTRL ('V'), cmd_paste);
	menu.item ("Delete", "Del", 0, cmd_delete);
	menu.item ("Select All", "^A", UK_CTRL ('A'), cmd_select_all);
	menu.separator ();
	menu.item ("Find...", "^F", UK_CTRL ('F'), cmd_find);
	menu.menu ("View");
	menu.item ("Design", "", 0, cmd_design);
	menu.item ("Split", "", 0, cmd_split);
	menu.item ("The Form's Text", "", 0, cmd_form_text);
	menu.item ("Code / Form", "F7", 0, cmd_view_code);
	menu.item ("Generated Code", "", 0, cmd_generated);
	menu.separator ();
	menu.item ("Grid", "", 0, cmd_grid);
	menu.item ("Snap to Grid", "", 0, cmd_snap);
	menu.menu ("Project");
	menu.item ("Add Module...", "", 0, cmd_add_module);
	menu.item ("Settings...", "", 0, cmd_project_settings);
	menu.menu ("Run");
	menu.item ("Run", "F5", 0, cmd_run);
	menu.item ("Check", "", 0, cmd_check);
	menu.item ("Make App", "", 0, cmd_make_app);
	menu.menu ("Help");
	menu.item ("QBStudio Help", "F1", 0, cmd_help);
	menu.item ("About QBStudio", "", 0, cmd_about);
	menu.publish ();

	relayout ();
	// the project: named on the command line (its folder, its project.ini, a file of it), else the last one, else the example
	bool ok = false;
	if (an > 0 && args[0])
	{
		char dir[256]; cpy (dir, args, sizeof dir);
		if (ends_with (dir, ".ini") || ends_with (dir, ".form") || ends_with (dir, ".bas")) { char *b = (char *) base_name (dir); if (b > dir) b[-1] = 0; }
		ok = load_project (dir);
	}
	if (!ok) { char *l = read_file (LAST); if (l) { char ip[300]; join (ip, sizeof ip, l, "project.ini"); if (file_exists (ip)) ok = load_project (l); free (l); } }
	if (!ok) ok = load_project (SAMPLE);
	root.run ();
	confirm_close ();
	return 0;
}
