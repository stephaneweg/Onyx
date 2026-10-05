//
// dockconf -- the Control Panel's Panel applet (applet_proto.h; alone, a window of its own): the
// dock's settings (SD:/etc/dock.ini, dockconf.h).
//   * the DRAWERS, left to right: each a group of apps (the "category" of their app.txt) and its
//     main app (the drawer's icon; a click on it starts the app, the strip above it opens the
//     drawer). Add one, remove it, move it; pick its group and its main app in the lists beside;
//   * the LAUNCHERS after the drawers (the Terminal, the File Viewer...): add an app from the list
//     of them all, remove one, move it;
//   * the WORKSPACES (virtual desktops): how many (1 to 6) and their names.
// Apply writes dock.ini and starts the dock again (dock_reload, dockconf.h): it takes them at once, the
// number of workspaces too. Discard reloads what is saved.
//
#include "kapi.h"
#include "applib.h"
#include "fsutil.h"
#include "launch.h"
#include "dockconf.h"
#include "uikit/uikit.h"
#include "ft/uikitface.h"		// FreeType's text (DejaVu Sans) for every widget

using namespace uikit;

#define W	700
#define H	470
#define MAXAPPS	160
#define MAXCATS	16

struct App { char name[32]; char label[40]; char cat[24]; };
static App  g_apps[MAXAPPS];
static int  g_napps;
static char g_cats[MAXCATS][24];
static int  g_ncats;
static DockConf g_c;

static int g_catApps[MAXAPPS], g_ncatApps;	// the apps of the group picked (g_apps indexes)
static int g_allApps[MAXAPPS], g_nallApps;	// every app but the shell's parts, by name

static ListBox *g_lbDrawers, *g_lbGroups, *g_lbMain, *g_lbLaunchers, *g_lbAll;
static NumericUpDown *g_nuDesks;
static Textbox *g_tbDesk[DOCK_MAXDESKS];
static Label   *g_lbDeskN[DOCK_MAXDESKS];
static Label   *g_status;
static Root    *g_root;

static bool hidden_cat (const char *c)
{
	return fs_ci_cmp (c, "Shell") == 0 || fs_ci_cmp (c, "Settings") == 0 || fs_ci_cmp (c, "Emulators") == 0;
}

static App *find_app (const char *name)
{
	for (int i = 0; i < g_napps; i++) if (fs_ci_cmp (g_apps[i].name, name) == 0) return &g_apps[i];
	return 0;
}
static const char *label_of (const char *name) { App *a = find_app (name); return a ? a->label : name; }

static void scan_apps (void)
{
	static char list[4096];
	kapi_list_apps (list, sizeof list);
	g_napps = 0; g_ncats = 0;
	for (char *p = list; *p && g_napps < MAXAPPS; )
	{
		char *e = p; while (*e && *e != '\n') e++;
		char c = *e; *e = 0;
		App &a = g_apps[g_napps++];
		fs_copy (a.name, p, sizeof a.name); fs_copy (a.label, p, sizeof a.label); fs_copy (a.cat, "Other", sizeof a.cat);
		char q[180]; int k = 0;
		lx_cat (q, sizeof q, &k, "SD:/apps/"); lx_cat (q, sizeof q, &k, a.name); lx_cat (q, sizeof q, &k, ".app/app.txt");
		if (app_ini_load_path (q) >= 0)
		{
			const char *nm = app_ini_get (0, "name", 0); if (nm && nm[0]) fs_copy (a.label, nm, sizeof a.label);
			const char *ct = app_ini_get (0, "category", 0); if (ct && ct[0]) fs_copy (a.cat, ct, sizeof a.cat);
		}
		if (!hidden_cat (a.cat))
		{
			bool seen = false;
			for (int i = 0; i < g_ncats; i++) if (fs_ci_cmp (g_cats[i], a.cat) == 0) seen = true;
			if (!seen && g_ncats < MAXCATS) fs_copy (g_cats[g_ncats++], a.cat, 24);
		}
		*e = c;
		p = *e ? e + 1 : e;
	}
	for (int i = 1; i < g_ncats; i++)
		for (int j = i; j > 0 && fs_ci_cmp (g_cats[j - 1], g_cats[j]) > 0; j--)
		{ char t[24]; fs_copy (t, g_cats[j], 24); fs_copy (g_cats[j], g_cats[j - 1], 24); fs_copy (g_cats[j - 1], t, 24); }
	g_nallApps = 0;
	for (int i = 0; i < g_napps; i++) if (fs_ci_cmp (g_apps[i].cat, "Shell") != 0) g_allApps[g_nallApps++] = i;
	for (int i = 1; i < g_nallApps; i++)
		for (int j = i; j > 0 && fs_ci_cmp (g_apps[g_allApps[j - 1]].label, g_apps[g_allApps[j]].label) > 0; j--)
		{ int t = g_allApps[j]; g_allApps[j] = g_allApps[j - 1]; g_allApps[j - 1] = t; }
}

// ---- the lists shown ---------------------------------------------------------------------------
static void fill_drawers (int sel)
{
	g_lbDrawers->clear ();
	for (int i = 0; i < g_c.ndrawers; i++)
	{
		char s[64]; int n = 0;
		lx_cat (s, sizeof s, &n, g_c.drawer[i].cat); lx_cat (s, sizeof s, &n, ": ");
		lx_cat (s, sizeof s, &n, g_c.drawer[i].app[0] ? label_of (g_c.drawer[i].app) : "(none)");
		g_lbDrawers->add (s);
	}
	g_lbDrawers->setSel (sel < g_c.ndrawers ? sel : g_c.ndrawers - 1);
}

static void fill_main (int d)
{
	g_lbMain->clear (); g_ncatApps = 0;
	if (d < 0 || d >= g_c.ndrawers) return;
	int sel = -1;
	for (int i = 0; i < g_napps; i++)
		if (fs_ci_cmp (g_apps[i].cat, g_c.drawer[d].cat) == 0) g_catApps[g_ncatApps++] = i;
	for (int i = 1; i < g_ncatApps; i++)
		for (int j = i; j > 0 && fs_ci_cmp (g_apps[g_catApps[j - 1]].label, g_apps[g_catApps[j]].label) > 0; j--)
		{ int t = g_catApps[j]; g_catApps[j] = g_catApps[j - 1]; g_catApps[j - 1] = t; }
	for (int i = 0; i < g_ncatApps; i++)
	{
		g_lbMain->add (g_apps[g_catApps[i]].label);
		if (fs_ci_cmp (g_apps[g_catApps[i]].name, g_c.drawer[d].app) == 0) sel = i;
	}
	g_lbMain->setSel (sel);
}

static void fill_groups (int d)
{
	int sel = -1;
	if (d >= 0 && d < g_c.ndrawers)
		for (int i = 0; i < g_ncats; i++) if (fs_ci_cmp (g_cats[i], g_c.drawer[d].cat) == 0) sel = i;
	g_lbGroups->setSel (sel);
	fill_main (d);
}

static void fill_launchers (int sel)
{
	g_lbLaunchers->clear ();
	for (int i = 0; i < g_c.nlaunchers; i++) g_lbLaunchers->add (label_of (g_c.launcher[i]));
	g_lbLaunchers->setSel (sel < g_c.nlaunchers ? sel : g_c.nlaunchers - 1);
}

static void show_desks (void)
{
	for (int i = 0; i < DOCK_MAXDESKS; i++)
	{
		bool on = i < g_c.ndesks;
		g_tbDesk[i]->hidden = g_lbDeskN[i]->hidden = !on;
		if (on) g_tbDesk[i]->setText (g_c.desk[i]);
	}
	g_nuDesks->value = g_c.ndesks; g_nuDesks->invalidate (true);
	if (g_root) g_root->invalidate (true);
}

static void fill_all (void)
{
	scan_apps ();
	g_lbGroups->clear ();
	for (int i = 0; i < g_ncats; i++) g_lbGroups->add (g_cats[i]);
	g_lbAll->clear ();
	for (int i = 0; i < g_nallApps; i++) g_lbAll->add (g_apps[g_allApps[i]].label);
	fill_drawers (0);
	fill_groups (0);
	fill_launchers (0);
	show_desks ();
}

// ---- the actions ----------------------------------------------------------------------------------
static void on_drawer (Widget &) { fill_groups (g_lbDrawers->sel); }
static void on_group (Widget &)
{
	int d = g_lbDrawers->sel, g = g_lbGroups->sel;
	if (d < 0 || g < 0 || g >= g_ncats) return;
	if (fs_ci_cmp (g_c.drawer[d].cat, g_cats[g]) != 0)
	{
		fs_copy (g_c.drawer[d].cat, g_cats[g], 24);
		g_c.drawer[d].app[0] = 0;
		fill_main (d);
		if (g_ncatApps > 0) { fs_copy (g_c.drawer[d].app, g_apps[g_catApps[0]].name, 32); g_lbMain->setSel (0); }
		fill_drawers (d);
	}
}
static void on_main (Widget &)
{
	int d = g_lbDrawers->sel, m = g_lbMain->sel;
	if (d < 0 || m < 0 || m >= g_ncatApps) return;
	fs_copy (g_c.drawer[d].app, g_apps[g_catApps[m]].name, 32);
	fill_drawers (d);
}
static void on_add_drawer (Widget &)
{
	if (g_c.ndrawers >= DOCK_MAXDRAWERS) { g_status->setText ("8 drawers at most."); return; }
	int g = g_lbGroups->sel >= 0 ? g_lbGroups->sel : 0;
	DockDrawer &d = g_c.drawer[g_c.ndrawers++];
	fs_copy (d.cat, g_ncats ? g_cats[g] : "Other", 24); d.app[0] = 0;
	for (int i = 0; i < g_napps; i++) if (fs_ci_cmp (g_apps[i].cat, d.cat) == 0) { fs_copy (d.app, g_apps[i].name, 32); break; }
	fill_drawers (g_c.ndrawers - 1);
	fill_groups (g_c.ndrawers - 1);
}
static void on_remove_drawer (Widget &)
{
	int d = g_lbDrawers->sel;
	if (d < 0 || d >= g_c.ndrawers) return;
	for (int i = d; i + 1 < g_c.ndrawers; i++) g_c.drawer[i] = g_c.drawer[i + 1];
	g_c.ndrawers--;
	fill_drawers (d);
	fill_groups (g_lbDrawers->sel);
}
static void move_drawer (int dir)
{
	int d = g_lbDrawers->sel, e = d + dir;
	if (d < 0 || e < 0 || e >= g_c.ndrawers) return;
	DockDrawer t = g_c.drawer[d]; g_c.drawer[d] = g_c.drawer[e]; g_c.drawer[e] = t;
	fill_drawers (e);
}
static void on_up (Widget &) { move_drawer (-1); }
static void on_down (Widget &) { move_drawer (1); }

static void on_add_launcher (Widget &)
{
	int a = g_lbAll->sel;
	if (a < 0 || a >= g_nallApps) { g_status->setText ("Pick an app in the list on the right first."); return; }
	if (g_c.nlaunchers >= DOCK_MAXLAUNCHERS) { g_status->setText ("6 launchers at most."); return; }
	fs_copy (g_c.launcher[g_c.nlaunchers++], g_apps[g_allApps[a]].name, 32);
	fill_launchers (g_c.nlaunchers - 1);
}
static void on_remove_launcher (Widget &)
{
	int l = g_lbLaunchers->sel;
	if (l < 0 || l >= g_c.nlaunchers) return;
	for (int i = l; i + 1 < g_c.nlaunchers; i++) fs_copy (g_c.launcher[i], g_c.launcher[i + 1], 32);
	g_c.nlaunchers--;
	fill_launchers (l);
}
static void move_launcher (int dir)
{
	int l = g_lbLaunchers->sel, e = l + dir;
	if (l < 0 || e < 0 || e >= g_c.nlaunchers) return;
	char t[32]; fs_copy (t, g_c.launcher[l], 32); fs_copy (g_c.launcher[l], g_c.launcher[e], 32); fs_copy (g_c.launcher[e], t, 32);
	fill_launchers (e);
}
static void on_lup (Widget &) { move_launcher (-1); }
static void on_ldown (Widget &) { move_launcher (1); }

static void on_desks (Widget &w)
{
	int n = ((NumericUpDown &) w).value;
	for (int i = g_c.ndesks; i < n && i < DOCK_MAXDESKS; i++) { g_c.desk[i][0] = (char) ('1' + i); g_c.desk[i][1] = 0; }
	for (int i = 0; i < g_c.ndesks && i < DOCK_MAXDESKS; i++) fs_copy (g_c.desk[i], g_tbDesk[i]->text, 24);
	g_c.ndesks = n;
	show_desks ();
}

static void on_apply (Widget &)
{
	for (int i = 0; i < g_c.ndesks; i++)
	{
		fs_copy (g_c.desk[i], g_tbDesk[i]->text, 24);
		if (!g_c.desk[i][0]) { g_c.desk[i][0] = (char) ('1' + i); g_c.desk[i][1] = 0; }
	}
	if (g_c.ndrawers == 0) { g_status->setText ("Keep one drawer at least."); return; }
	if (!dockconf_save (g_c)) { g_status->setText ("Could not write SD:/etc/dock.ini."); return; }
	dock_reload ();
	g_status->setText ("Applied: the dock has it.");
}
static void on_discard (Widget &) { dockconf_load (g_c); fill_all (); g_status->setText (""); }

int main (void)
{
	ft_uikit_install ("DejaVu Sans", 13);		// (before the widgets; false: the bitmap font)
	Root root (W, H, "Panel");
	if (root.canvas.px == 0) return 1;
	g_root = &root;
	int X = root.width > W ? (root.width - W) / 2 : 0;
	dockconf_load (g_c);

	GroupBox *gd = new GroupBox (X + 10, 6, 470, 262, "Drawers");
	root.addChild (gd);
	int ct = gd->contentTop () + 4;
	g_lbDrawers = new ListBox (10, ct, 200, 180, on_drawer); gd->addChild (g_lbDrawers);
	gd->addChild (new Button (10, ct + 188, 58, 28, "Add", on_add_drawer));
	gd->addChild (new Button (72, ct + 188, 76, 28, "Remove", on_remove_drawer));
	gd->addChild (new Button (152, ct + 188, 28, 28, "^", on_up));
	gd->addChild (new Button (182, ct + 188, 28, 28, "v", on_down));
	gd->addChild (new Label (222, ct - 2, 110, 18, "Group", C_TEXT, gd->bg));
	g_lbGroups = new ListBox (222, ct + 18, 112, 198, on_group); gd->addChild (g_lbGroups);
	gd->addChild (new Label (342, ct - 2, 120, 18, "Main app", C_TEXT, gd->bg));
	g_lbMain = new ListBox (342, ct + 18, 118, 198, on_main); gd->addChild (g_lbMain);

	GroupBox *gw = new GroupBox (X + 490, 6, 200, 262, "Workspaces");
	root.addChild (gw);
	gw->addChild (new Label (10, ct + 4, 70, 20, "Number", C_TEXT, gw->bg));
	for (int i = 0; i < DOCK_MAXDESKS; i++)
	{
		char n[2] = { (char) ('1' + i), 0 };
		g_lbDeskN[i] = new Label (10, ct + 44 + i * 32, 20, 20, n, C_TEXT, gw->bg); gw->addChild (g_lbDeskN[i]);
		g_tbDesk[i] = new Textbox (30, ct + 40 + i * 32, 158, 28, ""); g_tbDesk[i]->tip = "Its name (the dock's square shows it)";
		gw->addChild (g_tbDesk[i]);
	}
	g_nuDesks = new NumericUpDown (90, ct, 70, 28, 1, DOCK_MAXDESKS, 4, 1, on_desks); gw->addChild (g_nuDesks);

	GroupBox *gl = new GroupBox (X + 10, 272, W - 20, 150, "Launchers (after the drawers)");
	root.addChild (gl);
	int lt = gl->contentTop () + 4;
	g_lbLaunchers = new ListBox (10, lt, 200, 112); gl->addChild (g_lbLaunchers);
	gl->addChild (new Button (218, lt, 96, 28, "< Add", on_add_launcher));
	gl->addChild (new Button (218, lt + 32, 96, 28, "Remove", on_remove_launcher));
	gl->addChild (new Button (218, lt + 64, 46, 28, "^", on_lup));
	gl->addChild (new Button (268, lt + 64, 46, 28, "v", on_ldown));
	g_lbAll = new ListBox (322, lt, W - 20 - 332, 112, 0, on_add_launcher); gl->addChild (g_lbAll);
	g_lbAll->tip = "Every app: pick one, then < Add (or double-click it)";

	g_status = new Label (X + 12, H - 38, 440, 24, "", C_DIS, root.bg);
	root.addChild (g_status);
	root.addChild (new Button (X + W - 196, H - 42, 90, 32, "Apply", on_apply));
	root.addChild (new Button (X + W - 100, H - 42, 90, 32, "Discard", on_discard));

	fill_all ();
	root.run ();
	return 0;
}
