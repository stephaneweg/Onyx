//
// keyconf -- the Control Panel's Keyboard & Mouse applet (applet_proto.h; alone, a window of its
// own). The keyboard's layout: the maps of SD:/etc/keymaps (*.kmap), one taken at once when picked
// (and kept: the "keyb XX" line of SD:/etc/autostart, which sets it at boot), a field to try it.
// The mouse: the wheel's speed (the lines a notch scrolls), at once and kept in SD:/etc/theme.txt
// ("wheelspeed=", the kernel reads it at boot).
//
#include "appkit/appkit.h"
#include "filekit/filekit.h"
#include "uikit/uikit.h"
#include "systemkit/systemkit.h"		// input.h: the layouts, the wheel
#include "fontkit/uikitface.h"		// FreeType's text (DejaVu Sans) for every widget

using namespace uikit;

#define W	700
#define H	470
#define MAXKM	24

static char g_km[MAXKM][12];
static int  g_nkm;
static ListBox *g_lbMaps;
static NumericUpDown *g_nuWheel;
static Label *g_status;

// The layouts' names (SystemKit's input.h gives them in English), translated here:
// TR: Belgian (azerty)
// TR: German (qwertz)
// TR: Dvorak
// TR: Spanish
// TR: French (azerty)
// TR: Italian
// TR: British
// TR: American (qwerty)
static const char *map_name (const char *code) { const char *n = input_keymap_name (code); return n[0] ? TR (n) : ""; }
static void scan_maps (void) { g_nkm = input_keymaps (g_km, MAXKM); }
static void on_map (Widget &)
{
	int i = g_lbMaps->sel;
	if (i < 0 || i >= g_nkm) return;
	int r = input_keymap_set (g_km[i]);			// (taken at once, kept in SD:/etc/autostart)
	if (!r) { g_status->setText (TR ("Could not load this layout.")); return; }
	static char msg[160]; int m = 0;
	ax_strcat (msg, sizeof msg, &m, g_km[i]);
	ax_strcat (msg, sizeof msg, &m, r == INPUT_KEPT ? TR (": taken, and set at every boot (SD:/etc/autostart).") : TR (": taken (autostart not written)."));
	g_status->setText (msg);
}
static void on_wheel (Widget &w)
{
	int v = ((NumericUpDown &) w).value;
	uk_win_wheel_set (v);
	input_wheel_save (v);					// (SD:/etc/theme.txt: the kernel reads it at boot)
}
int main (void)
{
	ft_uikit_install ("DejaVu Sans", 13);		// (before the widgets; false: the bitmap font)
	uk_lang_init ();				// the words in the system's language (before the widgets)
	Root root (W, H, TR ("Keyboard & Mouse"));
	if (root.canvas.px == 0) return 1;
	int X = root.width > W ? (root.width - W) / 2 : 0;
	scan_maps ();

	GroupBox *gk = new GroupBox (X + 10, 8, W - 20, 270, TR ("Keyboard layout"));
	root.addChild (gk);
	int ct = gk->contentTop () + 6;
	g_lbMaps = new ListBox (14, ct, 280, 220, on_map); gk->addChild (g_lbMaps);
	char cur[16] = ""; kapi_get_keymap (cur, sizeof cur);
	for (int i = 0; i < g_nkm; i++)
	{
		char s[64]; int n = 0;
		ax_strcat (s, sizeof s, &n, g_km[i]);
		const char *nm = map_name (g_km[i]);
		if (nm[0]) { ax_strcat (s, sizeof s, &n, "  "); ax_strcat (s, sizeof s, &n, nm); }
		g_lbMaps->add (s);
		if (fs_ci_cmp (g_km[i], cur) == 0) g_lbMaps->setSel (i);
	}
	gk->addChild (new Label (310, ct, 340, 20, TR ("Click a layout: it is taken at once, and"), C_TEXT, gk->bg));
	gk->addChild (new Label (310, ct + 20, 340, 20, TR ("set again at every boot."), C_TEXT, gk->bg));
	gk->addChild (new Label (310, ct + 60, 340, 20, TR ("Try it here:"), C_TEXT, gk->bg));
	gk->addChild (new Textbox (310, ct + 82, 340, 28, ""));

	GroupBox *gm = new GroupBox (X + 10, 288, W - 20, 96, TR ("Mouse"));
	root.addChild (gm);
	int mt = gm->contentTop () + 8;
	gm->addChild (new Label (14, mt + 4, 250, 20, TR ("Wheel: lines a notch scrolls"), C_TEXT, gm->bg));
	g_nuWheel = new NumericUpDown (270, mt, 80, 28, 1, 16, uk_win_wheel_get (), 1, on_wheel); gm->addChild (g_nuWheel);

	g_status = new Label (X + 12, 396, W - 24, 22, "", C_DIS, root.bg);
	root.addChild (g_status);
	root.run ();
	return 0;
}
