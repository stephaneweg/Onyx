//
// writer -- Onyx's rich-text editor (word processor body), built on the wtk toolkit's
// RichTextBox. The document's path on a thin bar over a styled, word-wrapping document.
// Styles are synthesised from the monospace bitmap font (bold/italic/underline/strike/
// highlight, 16-colour text, sizes x1..x8 smoothed) and paragraph heading levels
// (Normal / Titre 1-3). Plain-text load/save for now (a style-preserving format is a
// follow-up); .txt opens fine in tinypad too.
//
// Commands are in the system menu bar (wtk::Menu): File (New ^N, Open... ^O, Save ^S,
// Save As...), Edit (Cut ^X / Copy ^C / Paste ^V on the selection via the system
// clipboard, Select All ^A), Format (Bold ^B, Italic, Underline ^U, Strikethrough, Highlight, Smaller,
// Bigger), Color (Black/Red/Green/Blue), Style (Normal, Title 1-3). Styles apply to the
// selection, else to the typing style. The app name menu has Quit (^Q).
//
#include "wtk/wtk.h"
#include "clipboard.h"

using namespace wtk;

#define W	640
#define H	480
#define CAP	32768
#define PATH_H	22

static RichTextBox *g_rtb;
static Label       *g_fn;
static char         g_path[100] = "SD:doc.txt";

// ---- file I/O -----------------------------------------------------------------------

static void set_path (const char *p)
{
	int i = 0; for (; p[i] && i < (int) sizeof g_path - 1; i++) g_path[i] = p[i];
	g_path[i] = '\0';
	g_fn->setText (g_path);
}
static void do_open ()
{
	if (g_path[0] == '\0') return;
	void *f = kapi_open (g_path);
	if (f == 0) { g_rtb->setContent (""); g_rtb->setFocus (); return; }
	static char b[CAP];
	int n = kapi_read (f, b, sizeof b - 1);
	kapi_close (f);
	if (n < 0) n = 0;
	int j = 0;					// drop '\r' (CRLF -> LF)
	for (int i = 0; i < n; i++) if (b[i] != '\r') b[j++] = b[i];
	b[j] = '\0';
	g_rtb->setContent (b);
	g_rtb->setFocus ();
}
static void do_save ()
{
	if (g_path[0] != '\0') kapi_save_file (g_path, g_rtb->content (), (unsigned) g_rtb->length ());
}
static void onNew ()    { g_rtb->setContent (""); set_path ("SD:untitled.txt"); g_rtb->setFocus (); }
static void onOpen ()
{
	char path[100];
	if (wk_file_open (path, sizeof path, "SD:/")) { set_path (path); do_open (); }
	g_rtb->setFocus ();
}
static void onSaveAs ()
{
	char path[100];
	if (wk_file_save (path, sizeof path, "SD:/", g_path)) { set_path (path); do_save (); }
	g_rtb->setFocus ();
}
static void onSave () { if (g_path[0]) do_save (); else onSaveAs (); }

// ---- edit (system clipboard, plain text) --------------------------------------------

static char g_clipbuf[CAP];
static void onCopy ()  { int n = g_rtb->selectedText (g_clipbuf, sizeof g_clipbuf); if (n) clip_set_text_n (g_clipbuf, n); g_rtb->setFocus (); }
static void onCut ()   { onCopy (); g_rtb->cutSelection (); g_rtb->setFocus (); }
static void onPaste () { if (clip_get_text (g_clipbuf, sizeof g_clipbuf)) g_rtb->insertText (g_clipbuf); g_rtb->setFocus (); }
static void onSelectAll () { g_rtb->selectAll (); g_rtb->setFocus (); }

// ---- style commands (apply to the selection, else to the typing style) -------------

static void onBold   () { g_rtb->toggleFlag (RT_BOLD);   g_rtb->setFocus (); }
static void onItalic () { g_rtb->toggleFlag (RT_ITALIC); g_rtb->setFocus (); }
static void onUnder  () { g_rtb->toggleFlag (RT_UNDER);  g_rtb->setFocus (); }
static void onStrike () { g_rtb->toggleFlag (RT_STRIKE); g_rtb->setFocus (); }
static void onHilite () { g_rtb->toggleFlag (RT_HILITE); g_rtb->setFocus (); }

static void onSmaller () { g_rtb->setSize (g_rtb->caretStyle ().size - 1); g_rtb->setFocus (); }
static void onBigger  () { g_rtb->setSize (g_rtb->caretStyle ().size + 1); g_rtb->setFocus (); }

static void onBlack () { g_rtb->setFg (RT_BLACK); g_rtb->setFocus (); }
static void onRed   () { g_rtb->setFg (RT_RED);   g_rtb->setFocus (); }
static void onGreen () { g_rtb->setFg (RT_GREEN); g_rtb->setFocus (); }
static void onBlue  () { g_rtb->setFg (RT_BLUE);  g_rtb->setFocus (); }

static void onNormal () { g_rtb->setLevel (RT_NORMAL); g_rtb->setFocus (); }
static void onT1     () { g_rtb->setLevel (RT_TITLE1); g_rtb->setFocus (); }
static void onT2     () { g_rtb->setLevel (RT_TITLE2); g_rtb->setFocus (); }
static void onT3     () { g_rtb->setLevel (RT_TITLE3); g_rtb->setFocus (); }

int main (void)
{
	Root root (W, H, "Writer");
	root.setBg (0x00303840);

	g_fn = new Label (8, 3, W - 16, PATH_H - 6, g_path, 0x00C8D0DA, 0x00303840);
	root.addChild (g_fn);

	// Body: the rich-text document
	g_rtb = new RichTextBox (6, PATH_H, W - 12, H - PATH_H - 6, CAP);
	g_rtb->setContent (
		"Welcome to Onyx Writer.\n\n"
		"Select text with the mouse, then use the Format menu (Bold ^B, Underline ^U, "
		"Italic, Strikethrough, Highlight, Smaller / Bigger), the Color menu, or the Style "
		"menu for heading levels (Normal / Title 1-3).\n\n"
		"The wheel and the arrow keys scroll. File > Open... / Save / Save As... load and "
		"store plain text.");
	root.addChild (g_rtb);

	static Menu menu;
	menu.menu ("File");
	menu.item ("New",           "^N", WK_CTRL ('N'), onNew);
	menu.item ("Open...",       "^O", WK_CTRL ('O'), onOpen);
	menu.separator ();
	menu.item ("Save",          "^S", WK_CTRL ('S'), onSave);
	menu.item ("Save As...",    "",   0,             onSaveAs);
	menu.menu ("Edit");
	menu.item ("Cut",           "^X", WK_CTRL ('X'), onCut);
	menu.item ("Copy",          "^C", WK_CTRL ('C'), onCopy);
	menu.item ("Paste",         "^V", WK_CTRL ('V'), onPaste);
	menu.separator ();
	menu.item ("Select All",    "^A", WK_CTRL ('A'), onSelectAll);
	menu.menu ("Format");
	menu.item ("Bold",          "^B", WK_CTRL ('B'), onBold);
	menu.item ("Italic",        "",   0,             onItalic);	// (^I is Tab)
	menu.item ("Underline",     "^U", WK_CTRL ('U'), onUnder);
	menu.item ("Strikethrough", "",   0,             onStrike);
	menu.item ("Highlight",     "",   0,             onHilite);
	menu.separator ();
	menu.item ("Smaller",       "",   0,             onSmaller);
	menu.item ("Bigger",        "",   0,             onBigger);
	menu.menu ("Color");
	menu.item ("Black",         "",   0,             onBlack);
	menu.item ("Red",           "",   0,             onRed);
	menu.item ("Green",         "",   0,             onGreen);
	menu.item ("Blue",          "",   0,             onBlue);
	menu.menu ("Style");
	menu.item ("Normal",        "",   0,             onNormal);
	menu.item ("Title 1",       "",   0,             onT1);
	menu.item ("Title 2",       "",   0,             onT2);
	menu.item ("Title 3",       "",   0,             onT3);
	menu.publish ();

	// Open a file named on the command line (autostart "writer SD:notes.txt").
	char args[100];
	int an = kapi_get_args (args, sizeof args);
	if (an > 0 && args[0] != '\0') { set_path (args); do_open (); }

	g_rtb->setFocus ();
	root.run ();
	return 0;
}
