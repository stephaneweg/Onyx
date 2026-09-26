//
// tinypad/main.cpp -- small text editor (wtk). The file's path on a thin bar over a
// multi-line Textarea body (with its own auto vertical scrollbar). Commands live in the
// system menu bar (wtk::Menu): File > New (^N), Open... (^O), Save (^S), Save As...
// (Open / Save As use the wtk file dialogs); Edit > Copy All, Paste (^V) -- the system
// clipboard. The app name menu has Quit (^Q).
// Drag & drop: a file dropped on the window is opened (after asking to save unsaved
// changes -- docguard.h); dropped text is inserted at the caret.
//
#include "kapi.h"
#include "wtk/wtk.h"		// recursive widget toolkit + wk_file_open / wk_file_save + Menu
#include "clipboard.h"
#include "docguard.h"

using namespace wtk;

#define W	560
#define H	430
#define CAP	32768			// max document size (Textarea heap buffer)
#define PATH_H	22			// path bar

static Label    *g_fn;		// the file's path (read-only)
static Textarea *g_body;	// the document
static char      g_path[100] = "SD:notes.txt";

static unsigned  g_saved;	// doc_hash of the document as last loaded / saved

static void show_path (void) { g_fn->setText (g_path); }
static void mark_saved (void) { g_saved = doc_hash (g_body->content (), (unsigned) g_body->len); }
static bool changed (void)    { return doc_hash (g_body->content (), (unsigned) g_body->len) != g_saved; }

static void load_file (void)
{
	if (g_path[0] == '\0') return;
	void *f = kapi_open (g_path);
	if (f == 0) { g_body->setContent (""); mark_saved (); return; }
	static char buf[CAP];
	int n = kapi_read (f, buf, sizeof buf - 1);
	kapi_close (f);
	if (n < 0) n = 0;
	int j = 0;					// drop '\r' (CRLF -> LF)
	for (int i = 0; i < n; i++) if (buf[i] != '\r') buf[j++] = buf[i];
	buf[j] = '\0';
	g_body->setContent (buf);
	mark_saved ();
}
static void save_file (void)
{
	if (g_path[0] != '\0' && kapi_save_file (g_path, g_body->content (), (unsigned) g_body->len) >= 0) mark_saved ();
}
static void set_path (const char *p)
{
	int i = 0; for (; p[i] && i < (int) sizeof g_path - 1; i++) g_path[i] = p[i];
	g_path[i] = '\0';
	show_path ();
}

static void on_save (void);
static void on_new (void)
{
	if (!doc_confirm (g_path, changed (), on_save)) return;
	g_body->setContent (""); set_path ("SD:untitled.txt"); mark_saved (); g_body->setFocus ();
}
static void on_open (void)
{
	if (!doc_confirm (g_path, changed (), on_save)) return;
	char path[100];
	if (wk_file_open (path, sizeof path, "SD:/")) { set_path (path); load_file (); }
	g_body->setFocus ();
}
static void on_save_as (void)
{
	char path[100];
	if (wk_file_save (path, sizeof path, "SD:/", g_path)) { set_path (path); save_file (); }
	g_body->setFocus ();
}
static void on_save (void) { if (g_path[0]) save_file (); else on_save_as (); }

// Edit: the Textarea's selection (Shift + arrows / Home / End / Page Up / Down, a mouse
// drag, ^A) is cut / copied to the system clipboard; Paste replaces it.
static void on_copy (void) { g_body->copy (); g_body->setFocus (); }
static void on_cut (void) { g_body->cut (); g_body->setFocus (); }
static void on_select_all (void) { g_body->selectAll (); g_body->setFocus (); }
static void on_copy_all (void) { clip_set_text_n (g_body->content (), g_body->len); }
static void on_paste (void)
{
	static char b[CAP];
	if (clip_get_text (b, sizeof b)) g_body->insertText (b);
	g_body->setFocus ();
}

// The window: a dropped file replaces the document, dropped text goes in at the caret.
class PadRoot : public Root
{
public:
	PadRoot () : Root (W, H, "tinypad") {}
	void onDrop (int, int, int type, const char *data, int, unsigned) override
	{
		if (type == DND_TEXT) { g_body->insertText (data); g_body->setFocus (); return; }
		char path[100];
		if (type != DND_FILES || !doc_first_path (data, path, sizeof path)) return;
		void *d = kapi_opendir (path);
		if (d) { kapi_closedir (d); return; }		// a folder: nothing to open
		if (!doc_confirm (g_path, changed (), on_save)) return;
		set_path (path); load_file ();
		g_body->setFocus ();
	}
};

int main (void)
{
	PadRoot root;
	if (root.canvas.px == 0) return 1;
	root.setBg (0x00303840);			// path-bar/background tint

	g_fn = new Label (6, 3, W - 12, PATH_H - 6, g_path, 0x00C8D0DA, 0x00303840); root.addChild (g_fn);
	g_body = new Textarea (4, PATH_H, W - 8, H - PATH_H - 4, CAP); root.addChild (g_body);

	static Menu menu;
	menu.menu ("File");
	menu.item ("New",        "^N", WK_CTRL ('N'), on_new);
	menu.item ("Open...",    "^O", WK_CTRL ('O'), on_open);
	menu.separator ();
	menu.item ("Save",       "^S", WK_CTRL ('S'), on_save);
	menu.item ("Save As...", "",   0,             on_save_as);
	menu.menu ("Edit");
	menu.item ("Cut",        "^X", WK_CTRL ('X'), on_cut);
	menu.item ("Copy",       "^C", WK_CTRL ('C'), on_copy);
	menu.item ("Paste",      "^V", WK_CTRL ('V'), on_paste);
	menu.separator ();
	menu.item ("Select All", "^A", WK_CTRL ('A'), on_select_all);
	menu.item ("Copy All",   "",   0,             on_copy_all);
	menu.publish ();

	char args[100];
	int an = kapi_get_args (args, sizeof args);
	if (an > 0 && args[0] != '\0') { set_path (args); load_file (); }
	else mark_saved ();
	g_body->setFocus ();

	root.run ();
	return 0;
}
