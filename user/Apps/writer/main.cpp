//
// writer -- Onyx's rich-text editor (word processor body), built on the wtk toolkit's
// RichTextBox. A two-row toolbar (filename + Open/Save, then style controls) over a
// styled, word-wrapping document. Styles are synthesised from the monospace bitmap
// font (bold/italic/underline/strike/highlight, 16-colour text, sizes x1..x8 smoothed)
// and paragraph heading levels (Normal / Titre 1-3). Plain-text load/save for now
// (a style-preserving format is a follow-up); .txt opens fine in tinypad too.
//
#include "wtk/wtk.h"

using namespace wtk;

#define W	640
#define H	480
#define CAP	32768

static RichTextBox *g_rtb;
static Textbox     *g_fn;

// ---- file I/O (direct kapi; no file dialog -- type the path in the box) -------

static void do_open ()
{
	const char *path = g_fn->text;
	if (path[0] == '\0') return;
	void *f = kapi_open (path);
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
	const char *path = g_fn->text;
	if (path[0] != '\0') kapi_save_file (path, g_rtb->content (), (unsigned) g_rtb->length ());
}
static void onOpen (Widget &) { do_open (); }
static void onSave (Widget &) { do_save (); }

// ---- style controls (apply to the selection, else to the typing style) -------

static void onBold   (Widget &) { g_rtb->toggleFlag (RT_BOLD);   g_rtb->setFocus (); }
static void onItalic (Widget &) { g_rtb->toggleFlag (RT_ITALIC); g_rtb->setFocus (); }
static void onUnder  (Widget &) { g_rtb->toggleFlag (RT_UNDER);  g_rtb->setFocus (); }
static void onStrike (Widget &) { g_rtb->toggleFlag (RT_STRIKE); g_rtb->setFocus (); }
static void onHilite (Widget &) { g_rtb->toggleFlag (RT_HILITE); g_rtb->setFocus (); }

static void onSmaller (Widget &) { g_rtb->setSize (g_rtb->caretStyle ().size - 1); g_rtb->setFocus (); }
static void onBigger  (Widget &) { g_rtb->setSize (g_rtb->caretStyle ().size + 1); g_rtb->setFocus (); }

static void onBlack (Widget &) { g_rtb->setFg (RT_BLACK); g_rtb->setFocus (); }
static void onRed   (Widget &) { g_rtb->setFg (RT_RED);   g_rtb->setFocus (); }
static void onGreen (Widget &) { g_rtb->setFg (RT_GREEN); g_rtb->setFocus (); }
static void onBlue  (Widget &) { g_rtb->setFg (RT_BLUE);  g_rtb->setFocus (); }

static void onNormal (Widget &) { g_rtb->setLevel (RT_NORMAL); g_rtb->setFocus (); }
static void onT1     (Widget &) { g_rtb->setLevel (RT_TITLE1); g_rtb->setFocus (); }
static void onT2     (Widget &) { g_rtb->setLevel (RT_TITLE2); g_rtb->setFocus (); }
static void onT3     (Widget &) { g_rtb->setLevel (RT_TITLE3); g_rtb->setFocus (); }

int main (void)
{
	Root root (W, H, "Writer");

	// Row 1: filename + Open / Save
	g_fn = new Textbox (6, 6, 392, 22, "SD:doc.txt");
	root.addChild (g_fn);
	root.addChild (new Button (402, 5, 58, 24, "Open", onOpen));
	root.addChild (new Button (464, 5, 58, 24, "Save", onSave));

	// Row 2: character styles | size | colours | heading levels
	const int y = 34, h = 24;
	root.addChild (new Button (  6, y, 26, h, "B",    onBold));
	root.addChild (new Button ( 34, y, 26, h, "I",    onItalic));
	root.addChild (new Button ( 62, y, 26, h, "U",    onUnder));
	root.addChild (new Button ( 90, y, 26, h, "S",    onStrike));
	root.addChild (new Button (118, y, 30, h, "Hi",   onHilite));
	root.addChild (new Button (156, y, 30, h, "A-",   onSmaller));
	root.addChild (new Button (188, y, 30, h, "A+",   onBigger));
	root.addChild (new Button (228, y, 34, h, "Blk",  onBlack));
	root.addChild (new Button (264, y, 34, h, "Red",  onRed));
	root.addChild (new Button (300, y, 34, h, "Grn",  onGreen));
	root.addChild (new Button (336, y, 34, h, "Blu",  onBlue));
	root.addChild (new Button (378, y, 46, h, "Norm", onNormal));
	root.addChild (new Button (426, y, 30, h, "T1",   onT1));
	root.addChild (new Button (458, y, 30, h, "T2",   onT2));
	root.addChild (new Button (490, y, 30, h, "T3",   onT3));

	// Body: the rich-text document
	g_rtb = new RichTextBox (6, 62, W - 12, H - 68, CAP);
	g_rtb->setContent (
		"Welcome to Onyx Writer.\n\n"
		"Select text with the mouse, then click B / I / U / S / Hi, pick a colour, "
		"or set a heading level (Norm / T1 / T2 / T3). Use A- / A+ to resize.\n\n"
		"The wheel and the arrow keys scroll. Type the path above and Open / Save.");
	root.addChild (g_rtb);

	// Open a file named on the command line (autostart "writer SD:notes.txt").
	char args[100];
	int an = kapi_get_args (args, sizeof args);
	if (an > 0 && args[0] != '\0') { g_fn->setText (args); do_open (); }

	g_rtb->setFocus ();
	root.run ();
	return 0;
}
