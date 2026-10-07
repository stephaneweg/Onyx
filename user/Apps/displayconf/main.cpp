//
// displayconf -- the Control Panel's Display applet (applet_proto.h; alone, a window of its own): the
// screen's resolution. A size picked in the list and Apply: changed at once (kapi v66 screen_set --
// every window kept on the screen and sent GUI_EVENT_DISPLAY_RESIZE: the menu bar, the dock, the
// notifications place themselves again, a maximised window fills the new work area, one too big is
// shrunk into it) and the wallpaper painted again at the new size (apps/voronoy, as the Theme
// applet); kept in SD:/cmdline.txt (width= / height=) for the next start. An older kernel: kept
// only, applied at the next start. The monitor shows any size (the firmware scales the picture to
// its own mode); its native one is the sharpest.
//
#include "appkit/appkit.h"
#include "uikit/uikit.h"
#include "fontkit/uikitface.h"		// FreeType's text (DejaVu Sans) for every widget

using namespace uikit;

#define W	700
#define H	470
#define CMDLINE	"SD:/cmdline.txt"

struct Mode { int w, h; const char *what; };
static const Mode MODES[] =
{
	{ 1024,  768, TRN ("4:3, XGA (Onyx's default)") },
	{ 1280,  720, TRN ("16:9, HD") },
	{ 1280,  800, TRN ("16:10, WXGA") },
	{ 1280, 1024, TRN ("5:4, SXGA") },
	{ 1366,  768, TRN ("16:9, laptop screens") },
	{ 1440,  900, TRN ("16:10, WXGA+") },
	{ 1600,  900, TRN ("16:9, HD+") },
	{ 1600, 1200, TRN ("4:3, UXGA") },
	{ 1680, 1050, TRN ("16:10, WSXGA+") },
	{ 1920, 1080, TRN ("16:9, Full HD") },
	{ 1920, 1200, TRN ("16:10, WUXGA") },
	{ 2560, 1440, TRN ("16:9, QHD") },
};
#define NMODES	((int) (sizeof MODES / sizeof MODES[0]))

static ListBox *g_list;
static Label   *g_now, *g_status;

static int put_str (char *b, int n, const char *s) { while (*s) b[n++] = *s++; return n; }
static int put_int (char *b, int n, int v) { return n + ax_itoa (v, b + n); }

static void show_now (void)
{
	int w = 0, h = 0; kapi_screen_size (&w, &h);
	char s[64]; int n = put_str (s, 0, TR ("The screen now: "));
	n = put_int (s, n, w); n = put_str (s, n, " x "); n = put_int (s, n, h); s[n] = 0;
	g_now->setText (s);
	for (int i = 0; i < NMODES; i++) if (MODES[i].w == w && MODES[i].h == h) g_list->setSel (i);
}

// SD:/cmdline.txt with width= / height= the new size (its other options kept, one line) -> ok
static bool save_cmdline (int w, int h)
{
	static char in[1024], out[1100];
	int n = 0;
	void *f = kapi_open (CMDLINE);
	if (f) { n = kapi_read (f, in, sizeof in - 1); kapi_close (f); }
	if (n < 0) n = 0;
	in[n] = 0;
	int o = put_str (out, 0, "width="); o = put_int (out, o, w);
	o = put_str (out, o, " height="); o = put_int (out, o, h);
	for (int i = 0; in[i]; )
	{
		while (in[i] == ' ' || in[i] == '\t' || in[i] == '\r' || in[i] == '\n') i++;
		int s = i;
		while (in[i] && in[i] != ' ' && in[i] != '\t' && in[i] != '\r' && in[i] != '\n') i++;
		if (i == s) break;
		bool size = (i - s > 6 && in[s] == 'w' && in[s + 1] == 'i' && in[s + 2] == 'd' && in[s + 3] == 't' && in[s + 4] == 'h' && in[s + 5] == '=')
			 || (i - s > 7 && in[s] == 'h' && in[s + 1] == 'e' && in[s + 2] == 'i' && in[s + 3] == 'g' && in[s + 4] == 'h' && in[s + 5] == 't' && in[s + 6] == '=');
		if (size || o + (i - s) + 2 >= (int) sizeof out) continue;
		out[o++] = ' ';
		for (int k = s; k < i; k++) out[o++] = in[k];
	}
	out[o++] = '\n';
	return kapi_save_file (CMDLINE, out, (unsigned) o) >= 0;	// (-1: not written)
}

static void on_apply (Widget &)
{
	int i = g_list->sel;
	if (i < 0 || i >= NMODES) { g_status->setText (TR ("Pick a size in the list first.")); return; }
	int w = MODES[i].w, h = MODES[i].h;
	bool saved = save_cmdline (w, h);
	int r = kapi_screen_set (w, h);
	if (r == 0)
	{
		kapi_exec ("SD:apps/voronoy.app/main", "");	// the wallpaper, at the new size
		g_status->setText (saved ? TR ("Applied, and kept for the next start.") : TR ("Applied -- but SD:/cmdline.txt could not be written."));
	}
	else if (r == -4) g_status->setText (saved ? TR ("Kept: applied at the next start (this kernel cannot change it now).") : TR ("SD:/cmdline.txt could not be written."));
	else if (r == -2) g_status->setText (TR ("Not now: a full-screen app owns the display. Kept for the next start."));
	else if (r == -3) g_status->setText (TR ("The firmware refused this size: the screen kept its own."));
	else g_status->setText (TR ("This size is not possible."));
	show_now ();
}

class DisplayRoot : public Root
{
public:
	DisplayRoot () : Root (W, H, TR ("Display")) {}
	void onDisplayResize (int, int) override { show_now (); }
};

int main (void)
{
	ft_uikit_install ("DejaVu Sans", 13);		// (before the widgets; false: the bitmap font)
	uk_lang_init ();				// the words in the system's language (the face first: UTF-8)
	DisplayRoot root;
	if (root.canvas.px == 0) return 1;
	int X = root.width > W ? (root.width - W) / 2 : 0;
	GroupBox *gs = new GroupBox (X + 10, 8, W - 20, 330, TR ("Resolution"));
	root.addChild (gs);
	int ct = gs->contentTop () + 6;
	g_now = new Label (14, ct, W - 60, 20, "", C_TEXT, gs->bg); gs->addChild (g_now);
	g_list = new ListBox (14, ct + 30, 330, 240, 0, on_apply);
	for (int i = 0; i < NMODES; i++)
	{
		char s[64]; int n = put_int (s, 0, MODES[i].w); n = put_str (s, n, " x "); n = put_int (s, n, MODES[i].h);
		while (n < 13) s[n++] = ' ';
		n = put_str (s, n, TR (MODES[i].what)); s[n] = 0;
		g_list->add (s);
	}
	gs->addChild (g_list);
	gs->addChild (new Button (360, ct + 30, 150, 30, TR ("Apply"), on_apply));
	gs->addChild (new Label (360, ct + 74, 300, 20, TR ("At once: the desktop and the"), C_DIS, gs->bg));
	gs->addChild (new Label (360, ct + 94, 300, 20, TR ("windows follow the new size."), C_DIS, gs->bg));
	gs->addChild (new Label (360, ct + 124, 300, 20, TR ("The monitor's own resolution"), C_DIS, gs->bg));
	gs->addChild (new Label (360, ct + 144, 300, 20, TR ("is the sharpest."), C_DIS, gs->bg));

	g_status = new Label (X + 12, 350, W - 24, 22, "", C_TEXT, root.bg);
	root.addChild (g_status);
	root.addChild (new Label (X + 12, H - 60, W - 24, 20, TR ("Kept in SD:/cmdline.txt (width= / height=), read at the start."), C_DIS, root.bg));
	show_now ();
	root.run ();
	return 0;
}
