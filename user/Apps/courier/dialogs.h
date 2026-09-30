//
// dialogs.h -- Courier's dialogs (wtk modals, drawn as the desktop's): a question, a name asked, Save
// Request (its name, the collection and the folder it goes to), the code of the request (cURL, HTTP,
// Python, JavaScript), Import (a file, or text pasted: a cURL command, a Postman collection /
// environment), the cookie jar.
//
#ifndef _courier_dialogs_h
#define _courier_dialogs_h

#include "rail.h"

namespace cr {

static inline void centre (Widget *d)
{
	Root *r = Root::current ();
	if (r) { d->left = (r->width - d->width) / 2; d->top = imax (0, (r->height - d->height) / 2 - 20); }
}
static void dlg_close (Widget &w) { Widget *p = w.parent; while (p && !p->modal) p = p->parent; if (p) ((Modal *) p)->close (w.tag); }
static Btn *dlg_button (Modal *m, int x, int y, int w, const char *s, int tag, int kind = BTN_SECONDARY)
{
	Btn *b = new Btn (x, y, w, 32, s, kind, dlg_close); b->tag = tag; m->addChild (b); return b;
}

// ---- a question ----------------------------------------------------------------------------------------------------------
class AskDialog : public Modal
{
public:
	const char *m_title, *m_text; bool m_danger;
	AskDialog (const char *title, const char *text, const char *yes, const char *no, bool danger)
		: Modal (460, 200), m_title (title), m_text (text), m_danger (danger)
	{
		// the height the text needs
		Canvas tmp; tmp.alloc (1, 1);
		int h = titleH () + 24 + measure () + 70;
		resizeTo (460, imax (150, h));
		centre (this);
		int bx = width - 16;
		if (no) { bx -= 100; dlg_button (this, bx, height - 48, 100, no, 0); bx -= 8; }
		bx -= 110; dlg_button (this, bx, height - 48, 110, yes, 1, danger ? BTN_DANGER : BTN_PRIMARY);
	}
	int measure ()
	{
		// lines of the wrapped text x the line height
		int lines = 0, w = width - 48; const char *p = m_text;
		while (*p)
		{
			const char *e = p, *cut = 0; Str t;
			while (*e && *e != '\n') { t.set (p, (int) (e - p + 1)); if (wk_tw (t.c ()) > w && cut) break; if (*e == ' ') cut = e; e++; }
			const char *stop = (*e && *e != '\n' && cut) ? cut : e;
			lines++; p = stop; if (*p == ' ' || *p == '\n') p++;
		}
		return lines * (wk_fh () + 4);
	}
	bool onKey (long k) override { if (k == KEY_ENTER) { close (1); return true; } if (k == 27) { close (0); return true; } return false; }
	void onDraw () override { drawBox (m_title); text_wrap (canvas, 24, titleH () + 20, width - 48, m_text, C_TEXT); }
};
static bool ask (const char *title, const char *text, const char *yes = "OK", const char *no = "Cancel", bool danger = false)
{
	AskDialog d (title, text, yes, no, danger);
	return d.run () == 1;
}
static void tell (const char *title, const char *text) { AskDialog d (title, text, "OK", 0, false); d.run (); }

// ---- a name asked -----------------------------------------------------------------------------------------------------------
class NameDialog : public Modal
{
public:
	const char *m_title, *m_label; LineEdit *e;
	NameDialog (const char *title, const char *label, const char *initial, const char *ok) : Modal (440, 0), m_title (title), m_label (label)
	{
		resizeTo (440, titleH () + 130);
		centre (this);
		e = new LineEdit (20, titleH () + 44, width - 40, 32, initial); e->vars = false;
		e->onEnter = [] (Widget &w) { ((Modal *) w.parent)->close (1); };
		addChild (e);
		e->selectAll ();
		dlg_button (this, width - 16 - 100, height - 48, 100, "Cancel", 0);
		dlg_button (this, width - 16 - 100 - 8 - 100, height - 48, 100, ok, 1, BTN_PRIMARY);
	}
	bool onKey (long k) override { if (k == 27) { close (0); return true; } return false; }
	void onDraw () override { drawBox (m_title); text_at (canvas, 20, titleH () + 18, m_label, C_TEXT); }
};
// false: cancelled (or empty)
static bool ask_name (Str &out, const char *title, const char *label, const char *initial, const char *ok = "OK")
{
	NameDialog d (title, label, initial, ok);
	d.e->setFocus ();
	if (d.run () != 1) return false;
	Str t; s_trim (t, d.e->text.c ());
	if (t.empty ()) return false;
	out = t;
	return true;
}

// ---- Save Request: its name, where -----------------------------------------------------------------------------------------
class SaveDialog : public Modal
{
public:
	struct Dest { Item *folder; int depth; Collection *coll; };
	Vec<Dest> dests;
	int sel, top;
	LineEdit *name;
	SaveDialog (const char *initial, Item *preselect) : Modal (520, 470), sel (-1), top (0)
	{
		centre (this);
		name = new LineEdit (20, titleH () + 40, width - 40, 32, initial); name->vars = false;
		addChild (name);
		name->selectAll ();
		Btn *nc = new Btn (20, height - 48, 150, 32, "New Collection", BTN_SECONDARY, [] (Widget &w) { ((SaveDialog *) w.parent)->newColl (); });
		nc->setGlyph (WKG_PLUS); addChild (nc);
		dlg_button (this, width - 16 - 100, height - 48, 100, "Cancel", 0);
		dlg_button (this, width - 16 - 100 - 8 - 100, height - 48, 100, "Save", 1, BTN_PRIMARY);
		refill ();
		for (int i = 0; i < dests.size (); i++) if (dests[i].folder == preselect) sel = i;
		if (sel < 0 && dests.size ()) sel = 0;
	}
	void addFolders (Item *f, int depth, Collection *c)
	{
		for (int i = 0; i < f->kids.size (); i++)
			if (f->kids[i]->folder) { Dest d = { f->kids[i], depth, c }; dests.push (d); addFolders (f->kids[i], depth + 1, c); }
	}
	void refill ()
	{
		dests.clear ();
		for (int i = 0; i < g_store.colls.size (); i++)
		{
			Collection *c = g_store.colls[i];
			Dest d = { &c->root, 0, c }; dests.push (d);
			addFolders (&c->root, 1, c);
		}
	}
	void newColl ()
	{
		app_new_collection ();
		refill ();
		sel = dests.size () ? 0 : -1;
		// (the new one: the last made)
		for (int i = 0; i < dests.size (); i++) if (dests[i].depth == 0 && dests[i].coll == g_store.colls.last ()) sel = i;
		invalidate (true);
	}
	int listY () { return titleH () + 110; }
	int rows () { return (height - 60 - listY ()) / 30; }
	void onDraw () override
	{
		drawBox ("Save Request");
		text_at (canvas, 20, titleH () + 16, "Request name", C_TEXT);
		text_at (canvas, 20, titleH () + 86, "Save to", C_TEXT);
		int ly = listY (), lh = height - 60 - ly;
		wk_sunken (canvas, 20, ly, width - 40, lh, 5, C_FIELD, false);
		if (!dests.size ()) { text_wrap (canvas, 36, ly + 16, width - 72, "There is no collection yet: make one (New Collection, below).", c_dim ()); return; }
		Canvas clip; clip.adopt (canvas.px + (ly + 2) * canvas.stride + 22, width - 44, lh - 4, canvas.stride);
		for (int k = 0; k < rows () + 1; k++)
		{
			int i = top + k; if (i >= dests.size ()) break;
			int y = k * 30;
			Dest &d = dests[i];
			if (i == sel) wk_hilite (clip, 2, y + 1, clip.w - 4, 28, 5, true);
			unsigned ink = i == sel ? wk_hilite_ink (true) : C_FIELD_TEXT;
			int x = 10 + d.depth * 18;
			if (d.depth == 0) icon_collections (clip, x + 10, y + 15, ink);
			else { wk_rline (clip, x + 1, y + 10, 18, 13, 2, ink); wk_rbox (clip, x + 1, y + 7, 8, 4, 2, ink, ink, 255, WK_TL | WK_TR); }
			text_fit (clip, x + 28, y, 30, clip.w - x - 34, d.depth == 0 ? d.coll->name.c () : d.folder->name.c (), ink, d.depth == 0 ? 2 : 0);
		}
	}
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override
	{
		int ly = listY ();
		if (wheel && my > ly) { top = iclamp (top + wheel, 0, imax (0, dests.size () - rows ())); invalidate (true); return true; }
		if (bl && !pressed && mx > 20 && mx < width - 20 && my > ly && my < height - 60)
		{
			pressed = true;
			int i = top + (my - ly - 2) / 30;
			if (i < dests.size ()) { sel = i; invalidate (true); }
			return true;
		}
		if (!bl) pressed = false;
		return Modal::onMouse (mx, my, bl, br, bm, wheel);
	}
	bool onKey (long k) override
	{
		if (k == 27) { close (0); return true; }
		if (k == KEY_ENTER) { close (1); return true; }
		return false;
	}
};

// ---- the request's code -------------------------------------------------------------------------------------------------------
class CodeDialog : public Modal
{
public:
	Choice *lang; CodeEdit *code; const Request *req; const Auth *auth;
	static int s_lang;
	CodeDialog (const Request &r, const Auth &a) : Modal (760, 520), req (&r), auth (&a)
	{
		Root *rt = Root::current ();
		if (rt) resizeTo (imin (760, rt->width - 40), imin (520, rt->height - 40));
		centre (this);
		lang = new Choice (20, titleH () + 14, 220, 30, SNIPPET_NAMES, SN_COUNT, s_lang);
		lang->onChange = [] (Widget &w) { CodeDialog *d = (CodeDialog *) w.parent; s_lang = d->lang->sel; d->fill (); };
		addChild (lang);
		code = new CodeEdit (20, titleH () + 56, width - 40, height - titleH () - 56 - 62);
		code->readonly = true; code->framed_ = true;
		addChild (code);
		Btn *cp = new Btn (width - 16 - 100 - 8 - 150, height - 48, 150, 32, "Copy to Clipboard", BTN_SECONDARY,
				   [] (Widget &w) { CodeDialog *d = (CodeDialog *) w.parent; clip_set_text_n (d->code->text (), d->code->length ()); app_status ("The code is in the clipboard."); });
		cp->setTool (WKT_COPY); addChild (cp);
		dlg_button (this, width - 16 - 100, height - 48, 100, "Close", 0, BTN_PRIMARY);
		fill ();
	}
	void fill ()
	{
		Str s; make_snippet (s, s_lang, *req, g_scope, *auth);
		code->lang = s_lang == SN_PYTHON || s_lang == SN_JS ? LANG_JS : LANG_TEXT;
		code->setText (s.c ());
	}
	bool onKey (long k) override { if (k == 27 || k == KEY_ENTER) { close (0); return true; } return false; }
	void onDraw () override
	{
		drawBox ("Code");
		text_v (canvas, 256, titleH () + 14, 30, "The request as code, its variables resolved.", c_dim ());
	}
};
int CodeDialog::s_lang = SN_CURL;

// ---- Import ------------------------------------------------------------------------------------------------------------------------
class ImportDialog : public Modal
{
public:
	CodeEdit *text; int result;
	Str path;
	ImportDialog () : Modal (700, 480), result (0)
	{
		Root *rt = Root::current ();
		if (rt) resizeTo (imin (700, rt->width - 40), imin (480, rt->height - 40));
		centre (this);
		Btn *f = new Btn (20, titleH () + 16, 190, 34, "Choose a File...", BTN_SECONDARY, [] (Widget &w) { ((ImportDialog *) w.parent)->pickFile (); });
		f->setTool (WKT_OPEN); addChild (f);
		text = new CodeEdit (20, titleH () + 112, width - 40, height - titleH () - 112 - 62);
		text->lineNumbers = false; text->framed_ = true;
		text->placeholder = "curl --location 'https://api.example.com/items' --header 'Accept: application/json'";
		addChild (text);
		dlg_button (this, width - 16 - 100, height - 48, 100, "Cancel", 0);
		dlg_button (this, width - 16 - 100 - 8 - 100, height - 48, 100, "Import", 1, BTN_PRIMARY);
	}
	void pickFile ()
	{
		char p[256];
		if (!wk_file_open (p, sizeof p, "SD:/")) return;
		path = p;
		close (2);
	}
	bool onKey (long k) override { if (k == 27) { close (0); return true; } return false; }
	void onDraw () override
	{
		drawBox ("Import");
		text_v (canvas, 224, titleH () + 16, 34, "A Postman collection / environment (.json), a cURL command (.txt)", c_dim ());
		text_at (canvas, 20, titleH () + 72, "Or paste it here: a cURL command, or a collection's / an environment's JSON", C_TEXT);
	}
};

// ---- the cookie jar ----------------------------------------------------------------------------------------------------------------
class CookiesDialog : public Modal
{
public:
	KVTable *list; KVList rows;
	CookiesDialog () : Modal (720, 460)
	{
		Root *rt = Root::current ();
		if (rt) resizeTo (imin (720, rt->width - 40), imin (460, rt->height - 40));
		centre (this);
		list = new KVTable (20, titleH () + 44, width - 40, height - titleH () - 44 - 62);
		list->readonly = true; list->colName[0] = "Name"; list->colName[1] = "Value"; list->colName[2] = "Domain, path, expiry";
		addChild (list);
		fill ();
		Btn *c = new Btn (20, height - 48, 170, 32, "Delete All Cookies", BTN_DANGER, [] (Widget &w)
		{
			CookiesDialog *d = (CookiesDialog *) w.parent;
			if (!g_jar.list.size ()) return;
			g_jar.list.clear (); g_jar.dirty = true; d->fill (); app_status ("The cookies were deleted.");
		});
		addChild (c);
		dlg_button (this, width - 16 - 100, height - 48, 100, "Close", 0, BTN_PRIMARY);
	}
	void fill ()
	{
		rows.clear ();
		for (int i = 0; i < g_jar.list.size (); i++)
		{
			Cookie &c = g_jar.list[i];
			KV kv (c.name.c (), c.value.c ());
			kv.desc.addf ("%s%s  %s", c.hostOnly ? "" : ".", c.domain.c (), c.path.c ());
			if (c.expires) { Str e; iso_time (e, c.expires); kv.desc.add ("  until "); kv.desc.add (e.c ()); } else kv.desc.add ("  (session)");
			rows.push (kv);
		}
		list->setRows (&rows);
		invalidate (true);
	}
	bool onKey (long k) override { if (k == 27 || k == KEY_ENTER) { close (0); return true; } return false; }
	void onDraw () override
	{
		drawBox ("Cookies");
		char b[96]; snprintf (b, sizeof b, "%d cookie%s kept (sent to their domains; a click on a row copies its value)", g_jar.list.size (), g_jar.list.size () == 1 ? "" : "s");
		text_v (canvas, 20, titleH () + 8, 30, b, c_dim ());
	}
};

} // namespace cr

#endif
