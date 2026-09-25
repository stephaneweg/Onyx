//
// ask -- a small system confirmation dialog, for apps whose own window is too small to
// host a wtk modal (the Shelf). Run it (kapi_spawn) with args "Title|Message|Yes|No";
// it shows a centred window and exits with 1 (Yes / Enter) or 0 (No / Esc / close box).
// The caller polls kapi_proc_done and reads the answer with kapi_wait -- see ask.h.
//
#include "kapi.h"
#include "wtk/wtk.h"

using namespace wtk;

#define W	440
#define H	130

static void on_yes (Widget &) { kapi_exit (1); }
static void on_no (Widget &)  { kapi_exit (0); }

class AskRoot : public Root
{
public:
	AskRoot (int x, int y, const char *title)
	  : Root (x, y, W, H, title, WIN_FLAG_SYSTEM) {}
	bool onKey (long k) override
	{
		if (k == KEY_ENTER) kapi_exit (1);
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
	char *f[4] = { (char *) "Confirm", (char *) "Are you sure?", (char *) "Yes", (char *) "No" };
	char *g[4];
	int n = split (args, g, 4);
	for (int i = 0; i < n; i++) if (g[i][0]) f[i] = g[i];

	int sw = 1024, sh = 768;
	kapi_screen_size (&sw, &sh);
	AskRoot root ((sw - W) / 2, (sh - H) / 2 - 40, f[0]);
	if (root.canvas.px == 0) kapi_exit (0);
	root.addChild (new Label (16, 20, W - 32, 18, f[1], C_TEXT, root.bg));
	root.addChild (new Button (W - 2 * 112, H - 46, 104, 30, f[2], on_yes));
	root.addChild (new Button (W - 1 * 112, H - 46, 104, 30, f[3], on_no));
	root.run ();					// the close box ends it: "No"
	kapi_exit (0);
	return 0;
}
