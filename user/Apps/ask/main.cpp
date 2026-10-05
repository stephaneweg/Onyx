//
// ask -- a small system confirmation dialog, for apps whose own window is too small to
// host a uikit modal (the dock). Run it (kapi_spawn) with args "Title|Message|Yes|No";
// it shows a centred window and exits with 1 (Yes / Enter) or 0 (No / Esc / close box).
// The caller polls kapi_proc_done and reads the answer with kapi_wait -- see ask.h.
// A fifth field "=text" asks for a line of text (a field holding `text`): on OK the text
// is written to stdout (the caller's pipe: ask_text_begin / ask_text_poll).
//
#include "kapi.h"
#include "uikit/uikit.h"

using namespace uikit;

#define W	440
#define H	130

static Textbox *g_text;				// (asking for a line of text)

// OK: the text (if asked) on stdout, then 1.
static void yes (void)
{
	if (g_text && g_text->text[0]) kapi_stdout_write (g_text->text, (unsigned) uk_len (g_text->text));
	kapi_exit (1);
}
static void on_yes (Widget &) { yes (); }
static void on_no (Widget &)  { kapi_exit (0); }
static void on_enter (Widget &) { yes (); }

class AskRoot : public Root
{
public:
	AskRoot (int x, int y, const char *title)
	  : Root (x, y, W, H, title, WIN_FLAG_SYSTEM) {}
	bool onKey (long k) override
	{
		if (k == KEY_ENTER) yes ();
		if (k == 27) kapi_exit (0);
		return false;
	}
};

// Split args at '|' into up to n fields (in place).
static int split (char *s, char **f, int n)
{
	int k = 0; f[k++] = s;
	for (char *p = s; *p && k < n; p++) if (*p == '|') { *p = '\0'; f[k++] = p + 1; }
	return k;
}

int main (void)
{
	static char args[400];
	kapi_get_args (args, sizeof args);
	char *f[5] = { (char *) "Confirm", (char *) "Are you sure?", (char *) "Yes", (char *) "No", 0 };
	char *g[5];
	int n = split (args, g, 5);
	for (int i = 0; i < n; i++) if (g[i][0] || i == 4) f[i] = g[i];
	bool text = n == 5 && f[4][0] == '=';

	int sw = 1024, sh = 768;
	kapi_screen_size (&sw, &sh);
	AskRoot root ((sw - W) / 2, (sh - H) / 2 - 40, f[0]);
	if (root.canvas.px == 0) kapi_exit (0);
	if (text)
	{
		root.addChild (new Label (16, 14, 110, 26, f[1], C_TEXT, root.bg));
		g_text = new Textbox (120, 14, W - 136, 26, f[4] + 1, on_enter);
		root.addChild (g_text);
		g_text->setFocus ();
	}
	else root.addChild (new Label (16, 20, W - 32, 18, f[1], C_TEXT, root.bg));
	root.addChild (new Button (W - 2 * 112, H - 46, 104, 30, f[2], on_yes));
	root.addChild (new Button (W - 1 * 112, H - 46, 104, 30, f[3], on_no));
	root.run ();					// the close box ends it: "No"
	kapi_exit (0);
	return 0;
}
