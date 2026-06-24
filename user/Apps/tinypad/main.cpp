//
// tinypad/main.cpp -- small text editor (C++ port, fully on uikit.hpp). A toolbar
// (filename Textbox + Open/Save Buttons) over a multi-line Textarea body. The toolkit
// owns focus: clicking the filename box edits the path, clicking the body edits the
// document -- no custom key routing. Open/Save use the kernel file dialogs.
//
#include "kapi.h"
#include "uikit.hpp"
#include "uidialog.hpp"		// ui::file_open / ui::file_save (user-side modal)

#define W	560
#define H	430
#define CAP	32768			// max document size (Textarea heap buffer)

static ui::Ui        *g_ui;
static ui::Textbox   *g_fn;	// filename box
static ui::Textarea  *g_body;	// the document
static ui::Scrollbar *g_vsb, *g_hsb;	// scroll the body with the mouse

static void load_file (void)
{
	const char *path = g_fn->getText ();
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
	const char *path = g_fn->getText ();
	if (path[0] != '\0') kapi_save_file (path, g_body->content (), (unsigned) g_body->length ());
}

static void on_open (ui::Widget &)
{
	char path[100];
	ui::FileDialog fd (*g_ui, "SD:/", 0, false);
	if (fd.run (g_ui)) { fd.getResult (path, sizeof path); g_fn->setText (path); load_file (); g_ui->markDirty (); }
}
static void on_save (ui::Widget &)
{
	char path[100];
	ui::FileDialog fd (*g_ui, "SD:/", g_fn->getText (), true);
	if (fd.run (g_ui)) { fd.getResult (path, sizeof path); g_fn->setText (path); save_file (); }
}
static void on_vscroll (ui::Widget &w) { g_body->setTop  (((ui::Scrollbar &) w).value); g_ui->markDirty (); }
static void on_hscroll (ui::Widget &w) { g_body->setLeft (((ui::Scrollbar &) w).value); g_ui->markDirty (); }
static void on_evt (unsigned long s, int ev, long v) { g_ui->onEvent (s, ev, v); }

int main (void)
{
	unsigned *fb = kapi_create_window (W, H, "tinypad");
	if (fb == 0) return 1;

	ui::Ui ui (fb, W, H); g_ui = &ui;
	ui.col_bg = 0x00303840;				// toolbar/background tint

	g_fn = &ui.textbox (4, 4, W - 160, 20, "SD:notes.txt");
	       ui.button  (W - 152, 4, 70, 20, "Open", on_open);
	       ui.button  (W - 78,  4, 70, 20, "Save", on_save);
	g_body = &ui.textarea  (4, 30, W - 20, H - 46, CAP);		// leaves room for scrollbars
	g_vsb  = &ui.scrollbar (W - 14, 30, 12, H - 46, true,  1, 0, on_vscroll);
	g_hsb  = &ui.scrollbar (4, H - 14, W - 20, 12, false, 1, 0, on_hscroll);

	char args[100];
	int an = kapi_get_args (args, sizeof args);
	if (an > 0 && args[0] != '\0') { g_fn->setText (args); load_file (); }
	ui.setFocus (*g_body);

	kapi_set_pointer_handler (on_evt);
	kapi_set_key_handler (on_evt);

	while (!should_exit ())
	{
		pump_events ();
		// reflect the body's scroll position in the scrollbars (the caret scrolls it too)
		int mt = g_body->maxTop  (); g_vsb->vmax = mt > 0 ? mt : 1; g_vsb->value = g_body->topLine ();
		int ml = g_body->maxLeft (); g_hsb->vmax = ml > 0 ? ml : 1; g_hsb->value = g_body->leftCol ();
		if (ui.dirty ()) { ui.background (); ui.drawAll (); present (); }
		msleep (16);
	}
	return 0;
}
