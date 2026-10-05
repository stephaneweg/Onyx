//
// config -- the Control Panel's App Settings applet (applet_proto.h; alone, a window of its own):
// an app's own settings, SD:/apps/<name>.app/config.ini, as key = value lines. On the left the
// apps (those with a config.ini first); on the right the chosen app's settings: pick a line, change
// its key or its value in the fields below, Set (Enter) -- a new key adds a line --, Delete removes
// the line. Save writes the file; Reload reads it again. (The apps read their config.ini when they
// start.)
//
#include "kapi.h"
#include "applib.h"
#include "fsutil.h"
#include "uikit/uikit.h"
#include "ft/uikitface.h"		// FreeType's text (DejaVu Sans) for every widget

using namespace uikit;

#define W	700
#define H	470
#define MAXAPPS	96
#define MAXKV	48

static char g_app[MAXAPPS][32];
static bool g_has[MAXAPPS];
static int  g_napps;
static char g_key[MAXKV][32], g_val[MAXKV][64];
static int  g_nkv;
static int  g_cur = -1;
static bool g_dirty;

static ListBox *g_lbApps, *g_lbKv;
static Textbox *g_tbKey, *g_tbVal;
static Label   *g_title, *g_status;

static void ini_path (int a, char *out, int cap)
{
	int p = 0;
	ax_strcat (out, cap, &p, "SD:/apps/"); ax_strcat (out, cap, &p, g_app[a]); ax_strcat (out, cap, &p, ".app/config.ini");
}

static void scan_apps (void)
{
	static char list[4096];
	kapi_list_apps (list, sizeof list);
	g_napps = 0;
	for (char *p = list; *p && g_napps < MAXAPPS; )
	{
		char *e = p; while (*e && *e != '\n') e++;
		char c = *e; *e = 0;
		fs_copy (g_app[g_napps], p, 32);
		char q[160]; ini_path (g_napps, q, sizeof q);
		g_has[g_napps] = fs_exists (q);
		g_napps++;
		*e = c;
		p = *e ? e + 1 : e;
	}
	for (int i = 1; i < g_napps; i++)				// the ones with settings first, by name
		for (int j = i; j > 0; j--)
		{
			bool before = g_has[j] && !g_has[j - 1];
			if (!before && (g_has[j] != g_has[j - 1] || fs_ci_cmp (g_app[j - 1], g_app[j]) <= 0)) break;
			char t[32]; fs_copy (t, g_app[j], 32); fs_copy (g_app[j], g_app[j - 1], 32); fs_copy (g_app[j - 1], t, 32);
			bool h = g_has[j]; g_has[j] = g_has[j - 1]; g_has[j - 1] = h;
		}
}

static void fill_kv (int sel)
{
	g_lbKv->clear ();
	for (int i = 0; i < g_nkv; i++)
	{
		char s[64]; int n = 0;
		ax_strcat (s, sizeof s, &n, g_key[i]); ax_strcat (s, sizeof s, &n, " = "); ax_strcat (s, sizeof s, &n, g_val[i]);
		g_lbKv->add (s);
	}
	g_lbKv->setSel (sel < g_nkv ? sel : g_nkv - 1);
}

static void load (int a)
{
	g_cur = a; g_nkv = 0; g_dirty = false;
	if (a < 0) return;
	char p[160]; ini_path (a, p, sizeof p);
	if (app_ini_load_path (p) > 0)
		for (int i = 0; i < g_ini_n && g_nkv < MAXKV; i++)
		{
			fs_copy (g_key[g_nkv], g_ini_key[i], 32); fs_copy (g_val[g_nkv], g_ini_val[i], 64);
			g_nkv++;
		}
	static char t[64]; int n = 0;
	ax_strcat (t, sizeof t, &n, g_app[a]); ax_strcat (t, sizeof t, &n, g_nkv ? "  (config.ini)" : "  (no settings yet)");
	g_title->setText (t);
	fill_kv (0);
	g_tbKey->setText (g_nkv ? g_key[0] : ""); g_tbVal->setText (g_nkv ? g_val[0] : "");
	g_status->setText ("");
}

static void on_app (Widget &) { load (g_lbApps->sel); }
static void on_kv (Widget &)
{
	int i = g_lbKv->sel;
	if (i < 0 || i >= g_nkv) return;
	g_tbKey->setText (g_key[i]); g_tbVal->setText (g_val[i]);
}
static void on_set (Widget &)
{
	if (g_cur < 0) { g_status->setText ("Pick an app on the left first."); return; }
	if (!g_tbKey->text[0]) { g_status->setText ("A key is needed."); return; }
	int i = 0;
	while (i < g_nkv && fs_ci_cmp (g_key[i], g_tbKey->text) != 0) i++;
	if (i == g_nkv) { if (g_nkv >= MAXKV) return; g_nkv++; }
	fs_copy (g_key[i], g_tbKey->text, 32); fs_copy (g_val[i], g_tbVal->text, 64);
	g_dirty = true;
	fill_kv (i);
	g_status->setText ("Changed: Save writes it.");
}
static void on_delete (Widget &)
{
	int i = g_lbKv->sel;
	if (i < 0 || i >= g_nkv) return;
	for (int k = i; k + 1 < g_nkv; k++) { fs_copy (g_key[k], g_key[k + 1], 32); fs_copy (g_val[k], g_val[k + 1], 64); }
	g_nkv--; g_dirty = true;
	fill_kv (i);
	g_status->setText ("Deleted: Save writes it.");
}
static void on_save (Widget &)
{
	if (g_cur < 0) return;
	static char buf[4096]; int b = 0;
	for (int i = 0; i < g_nkv; i++)
	{
		ax_strcat (buf, sizeof buf, &b, g_key[i]); ax_strcat (buf, sizeof buf, &b, " = ");
		ax_strcat (buf, sizeof buf, &b, g_val[i]); ax_strcat (buf, sizeof buf, &b, "\n");
	}
	char p[160]; ini_path (g_cur, p, sizeof p);
	bool ok = kapi_save_file (p, buf, (unsigned) b) >= 0;
	g_dirty = !ok;
	g_status->setText (ok ? "Saved: the app takes it when it starts again." : "Could not write the file.");
}
static void on_reload (Widget &) { load (g_cur); }

int main (void)
{
	ft_uikit_install ("DejaVu Sans", 13);		// (before the widgets; false: the bitmap font)
	Root root (W, H, "App Settings");
	if (root.canvas.px == 0) return 1;
	int X = root.width > W ? (root.width - W) / 2 : 0;
	scan_apps ();
	root.addChild (new Label (X + 12, 8, 200, 20, "Apps", C_TEXT, root.bg));
	g_lbApps = new ListBox (X + 12, 30, 190, H - 44, on_app); root.addChild (g_lbApps);
	for (int i = 0; i < g_napps; i++)
	{
		char s[40]; int n = 0;
		ax_strcat (s, sizeof s, &n, g_app[i]); if (g_has[i]) ax_strcat (s, sizeof s, &n, " *");
		g_lbApps->add (s);
	}
	g_title = new Label (X + 216, 8, 470, 20, "Pick an app (* : it has settings)", C_TEXT, root.bg); root.addChild (g_title);
	g_lbKv = new ListBox (X + 216, 30, 472, 270, on_kv, 0); root.addChild (g_lbKv);
	root.addChild (new Label (X + 216, 312, 60, 24, "Key", C_TEXT, root.bg));
	g_tbKey = new Textbox (X + 280, 308, 180, 28, "", on_set); root.addChild (g_tbKey);
	root.addChild (new Label (X + 216, 346, 60, 24, "Value", C_TEXT, root.bg));
	g_tbVal = new Textbox (X + 280, 342, 408, 28, "", on_set); root.addChild (g_tbVal);
	root.addChild (new Button (X + 470, 306, 100, 30, "Set", on_set));
	root.addChild (new Button (X + 578, 306, 110, 30, "Delete", on_delete));
	g_status = new Label (X + 216, 380, 472, 22, "", C_DIS, root.bg); root.addChild (g_status);
	root.addChild (new Button (X + 216 + 472 - 196, H - 42, 90, 32, "Save", on_save));
	root.addChild (new Button (X + 216 + 472 - 100, H - 42, 100, 32, "Reload", on_reload));
	root.run ();
	return 0;
}
