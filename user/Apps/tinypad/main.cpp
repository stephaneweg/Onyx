//
// tinypad/main.cpp -- small text editor (wtk port). A toolbar (filename Textbox +
// Open/Save Buttons) over a multi-line Textarea body. The toolkit owns focus: clicking
// the filename box edits the path, clicking the body edits the document -- no custom key
// routing. The Textarea shows its own auto vertical scrollbar. Open/Save use the wtk
// file dialogs.
//
#include "kapi.h"
#include "wtk/wtk.h"		// recursive widget toolkit + wk_file_open / wk_file_save

using namespace wtk;

#define W	560
#define H	430
#define CAP	32768			// max document size (Textarea heap buffer)

static Textbox  *g_fn;		// filename box
static Textarea *g_body;	// the document

static void load_file (void)
{
	const char *path = g_fn->text;
	if (path[0] == '\0') return;
	void *f = kapi_open (path);
	if (f == 0) { g_body->setContent (""); return; }
	static char buf[CAP];
	int n = kapi_read (f, buf, sizeof buf - 1);
	kapi_close (f);
	if (n < 0) n = 0;
	int j = 0;					// drop '\r' (CRLF -> LF)
	for (int i = 0; i < n; i++) if (buf[i] != '\r') buf[j++] = buf[i];
	buf[j] = '\0';
	g_body->setContent (buf);
}
static void save_file (void)
{
	const char *path = g_fn->text;
	if (path[0] != '\0') kapi_save_file (path, g_body->content (), (unsigned) g_body->len);
}

static void on_open (Widget &)
{
	char path[100];
	if (wk_file_open (path, sizeof path, "SD:/")) { g_fn->setText (path); load_file (); }
}
static void on_save (Widget &)
{
	char path[100];
	if (wk_file_save (path, sizeof path, "SD:/", g_fn->text)) { g_fn->setText (path); save_file (); }
}

int main (void)
{
	Root root (W, H, "tinypad");
	if (root.canvas.px == 0) return 1;
	root.setBg (0x00303840);			// toolbar/background tint

	g_fn = new Textbox (4, 4, W - 160, 20, "SD:notes.txt"); root.addChild (g_fn);
	root.addChild (new Button (W - 152, 4, 70, 20, "Open", on_open));
	root.addChild (new Button (W - 78,  4, 70, 20, "Save", on_save));
	g_body = new Textarea (4, 30, W - 8, H - 36, CAP); root.addChild (g_body);

	char args[100];
	int an = kapi_get_args (args, sizeof args);
	if (an > 0 && args[0] != '\0') { g_fn->setText (args); load_file (); }
	g_body->setFocus ();

	root.run ();
	return 0;
}
