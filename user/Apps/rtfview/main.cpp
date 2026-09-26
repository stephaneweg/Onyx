//
// rtfview/main.cpp -- the RTF reader: shows a Rich Text Format document (.rtf) with its
// bold / italic / underline / strikethrough, colours, highlights and sizes, word-wrapped
// (user/rtf.h parses it into a read-only RichTextBox). Plain text files open too.
// File > Open... (^O) or drop a file on the window; Edit > Copy (^C) / Select All (^A);
// File > Edit in Writer hands the document to Writer (which reads and writes .rtf).
// Double-clicking a .rtf file in the File Viewer opens it here (fileassoc.ini).
//
#include "kapi.h"
#include "wtk/wtk.h"
#include "clipboard.h"
#include "docguard.h"
#include "rtf.h"

using namespace wtk;

#define W	640
#define H	500
#define PATH_H	22

static RichTextBox *g_rtb;
static Label       *g_fn;
static char         g_path[128];

static void open_file (const char *path)
{
	void *f = kapi_open (path);
	if (f == 0) { wk_messagebox ("Open", "Cannot open this file.", MB_OK); return; }
	unsigned sz = kapi_fsize (f);
	if (sz > 4u * 1024 * 1024) { kapi_close (f); wk_messagebox ("Open", "This file is too big (4 MB at most).", MB_OK); return; }
	char *b = new char[sz + 1];
	int n = kapi_read (f, b, sz);
	kapi_close (f);
	if (n < 0) n = 0;
	b[n] = 0;
	if (rtf::is_rtf (b, n)) rtf::load (*g_rtb, b, n);
	else
	{
		int j = 0; for (int i = 0; i < n; i++) if (b[i] != '\r') b[j++] = b[i];
		b[j] = 0;
		g_rtb->readonly = false; g_rtb->setContent (b); g_rtb->readonly = true;
	}
	delete[] b;
	int i = 0; for (; path[i] && i < (int) sizeof g_path - 1; i++) g_path[i] = path[i];
	g_path[i] = 0;
	g_fn->setText (g_path);
	g_rtb->setFocus ();
}

static void on_open ()
{
	char p[128];
	if (wk_file_open (p, sizeof p, "SD:/")) open_file (p);
	g_rtb->setFocus ();
}
static void on_writer () { if (g_path[0]) kapi_exec ("SD:/apps/writer.app/main", g_path); }
static void on_copy ()
{
	static char b[65536];
	int n = g_rtb->selectedText (b, sizeof b);
	if (n) clip_set_text_n (b, n);
}
static void on_all () { g_rtb->selectAll (); g_rtb->setFocus (); }

class RtfRoot : public Root
{
public:
	RtfRoot () : Root (W, H, "RTF Reader") {}
	void onDrop (int, int, int type, const char *data, int, unsigned) override
	{
		char p[128];
		if (type == DND_FILES && doc_first_path (data, p, sizeof p)) open_file (p);
	}
};

int main (void)
{
	RtfRoot root;
	if (root.canvas.px == 0) return 1;
	root.setBg (0x00303840);
	g_fn = new Label (8, 3, W - 16, PATH_H - 6, "", 0x00C8D0DA, 0x00303840);
	root.addChild (g_fn);
	g_rtb = new RichTextBox (6, PATH_H, W - 12, H - PATH_H - 6, 65536);
	g_rtb->readonly = true;
	root.addChild (g_rtb);
	static Menu menu;
	menu.menu ("File");
	menu.item ("Open...",        "^O", WK_CTRL ('O'), on_open);
	menu.item ("Edit in Writer", "",   0,             on_writer);
	menu.menu ("Edit");
	menu.item ("Copy",           "^C", WK_CTRL ('C'), on_copy);
	menu.item ("Select All",     "^A", WK_CTRL ('A'), on_all);
	menu.publish ();
	char args[128];
	if (kapi_get_args (args, sizeof args) > 0 && args[0]) open_file (args);
	else
	{
		g_rtb->readonly = false;
		g_rtb->setContent ("RTF Reader\n\nOpen a Rich Text Format document (.rtf) with File > Open... (^O), "
				   "drop one on this window, or double-click it in the File Viewer.");
		g_rtb->readonly = true;
	}
	g_rtb->setFocus ();
	root.run ();
	return 0;
}
