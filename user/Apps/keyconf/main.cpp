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

static const char *map_name (const char *code)			// (the ones Onyx ships)
{
	static const char *const K[][2] = { { "BE", TRN ("Belgian (azerty)") }, { "DE", TRN ("German (qwertz)") }, { "DV", TRN ("Dvorak") },
		{ "ES", TRN ("Spanish") }, { "FR", TRN ("French (azerty)") }, { "IT", TRN ("Italian") }, { "UK", TRN ("British") }, { "US", TRN ("American (qwerty)") } };
	for (unsigned i = 0; i < sizeof K / sizeof K[0]; i++) if (fs_ci_cmp (K[i][0], code) == 0) return TR (K[i][1]);
	return "";
}

static void scan_maps (void)
{
	g_nkm = 0;
	void *d = kapi_opendir ("SD:/etc/keymaps");
	if (d == 0) return;
	struct kapi_dirent e;
	while (g_nkm < MAXKM && kapi_readdir (d, &e))
	{
		int n = fs_len (e.name);
		if (e.is_dir || n < 6 || fs_ci_cmp (e.name + n - 5, ".kmap") != 0) continue;
		int b = n - 5; if (b > 11) b = 11;
		for (int i = 0; i < b; i++) g_km[g_nkm][i] = e.name[i];
		g_km[g_nkm][b] = 0;
		g_nkm++;
	}
	kapi_closedir (d);
	for (int i = 1; i < g_nkm; i++)
		for (int j = i; j > 0 && fs_ci_cmp (g_km[j - 1], g_km[j]) > 0; j--)
		{ char t[12]; fs_copy (t, g_km[j], 12); fs_copy (g_km[j], g_km[j - 1], 12); fs_copy (g_km[j - 1], t, 12); }
}

// A text file with its line starting with `key` replaced by `line` (added at its end when there is
// none). -> false: could not write it.
static bool set_line (const char *path, const char *key, const char *line)
{
	static char in[8192], out[8400];
	int n = 0;
	void *f = kapi_open (path);
	if (f) { n = kapi_read (f, in, sizeof in - 1); kapi_close (f); if (n < 0) n = 0; }
	in[n] = 0;
	int o = 0, kl = fs_len (key); bool done = false;
	for (int i = 0; i < n; )
	{
		int s = i; while (i < n && in[i] != '\n') i++;
		int e = i; if (i < n) i++;
		int t = s; while (t < e && (in[t] == ' ' || in[t] == '\t')) t++;
		bool match = e - t >= kl;
		for (int k = 0; match && k < kl; k++) match = in[t + k] == key[k];
		if (match && !done)
		{
			for (int k = 0; line[k] && o < (int) sizeof out - 2; k++) out[o++] = line[k];
			out[o++] = '\n'; done = true;
			continue;
		}
		for (int k = s; k < i && o < (int) sizeof out - 2; k++) out[o++] = in[k];
		if (i == n && e == n && n > 0 && in[n - 1] != '\n') out[o++] = '\n';
	}
	if (!done) { for (int k = 0; line[k] && o < (int) sizeof out - 2; k++) out[o++] = line[k]; out[o++] = '\n'; }
	return kapi_save_file (path, out, (unsigned) o) >= 0;
}

static void on_map (Widget &)
{
	int i = g_lbMaps->sel;
	if (i < 0 || i >= g_nkm) return;
	if (!ax_load_keymap (g_km[i])) { g_status->setText (TR ("Could not load this layout.")); return; }
	char line[24]; int n = 0;
	ax_strcat (line, sizeof line, &n, "keyb "); ax_strcat (line, sizeof line, &n, g_km[i]);
	bool kept = set_line ("SD:/etc/autostart", "keyb ", line);
	static char msg[160]; int m = 0;
	ax_strcat (msg, sizeof msg, &m, g_km[i]);
	ax_strcat (msg, sizeof msg, &m, kept ? TR (": taken, and set at every boot (SD:/etc/autostart).") : TR (": taken (autostart not written)."));
	g_status->setText (msg);
}

static void on_wheel (Widget &w)
{
	int v = ((NumericUpDown &) w).value;
	kapi_set_wheel_speed (v);
	char line[24]; int n = 0;
	ax_strcat (line, sizeof line, &n, "wheelspeed="); n += ax_itoa (v, line + n);
	set_line ("SD:/etc/theme.txt", "wheelspeed", line);
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
	g_nuWheel = new NumericUpDown (270, mt, 80, 28, 1, 16, kapi_get_wheel_speed (), 1, on_wheel); gm->addChild (g_nuWheel);

	g_status = new Label (X + 12, 396, W - 24, 22, "", C_DIS, root.bg);
	root.addChild (g_status);
	root.run ();
	return 0;
}
