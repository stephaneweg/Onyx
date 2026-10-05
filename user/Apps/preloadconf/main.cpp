//
// preloadconf -- the Control Panel's Preload applet (applet_proto.h; alone, a window of its own): the
// programs loaded ahead at boot and kept in memory (SD:/etc/preload.ini, preloadini.h; `preload /boot`,
// the last line of /etc/autostart, reads it). A preloaded program starts without reading the card --
// its code is in memory once, whatever the number of its processes -- and its memory stays taken.
//   * at the left, the list: each program, its size and whether it is in memory now;
//   * at the right, what can be added: the apps (by their names), then the /bin tools.
// A change is written at once, and done at once: a program added is loaded now (kapi image_preload),
// one removed is released (image_unload: its memory is freed when its last process ends).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. Permission is hereby
// granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions: The above copyright notice and this permission notice shall
// be included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS
// IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
//
#include "appkit/appkit.h"
#include "applib.h"
#include "fsutil.h"
#include "launch.h"
#include "BinUtils/imgname.h"
#include "preloadini.h"
#include "uikit/uikit.h"
#include "ft/uikitface.h"		// FreeType's text (DejaVu Sans) for every widget

using namespace uikit;

#define W	700
#define H	470
#define MAXPROGS	260

struct Prog { char name[64]; char label[48]; bool tool; };	// name: what preload.ini holds
static Prog g_progs[MAXPROGS];
static int  g_nprogs;
static PreloadList g_list;

static ListBox *g_lbList, *g_lbAll;
static Label   *g_status, *g_total;

// ---- the programs that can be added: the apps (but the desktop's own parts), then /bin's tools ----
static void sort_progs (int from, int to)
{
	for (int i = from + 1; i < to; i++)
		for (int j = i; j > from && fs_ci_cmp (g_progs[j - 1].label, g_progs[j].label) > 0; j--)
		{ Prog t = g_progs[j]; g_progs[j] = g_progs[j - 1]; g_progs[j - 1] = t; }
}

static void scan_progs (void)
{
	static char list[8192];
	g_nprogs = 0;
	kapi_list_apps (list, sizeof list);
	for (char *p = list; *p && g_nprogs < MAXPROGS; )
	{
		char *e = p; while (*e && *e != '\n') e++;
		char c = *e; *e = 0;
		Prog &a = g_progs[g_nprogs];
		fs_copy (a.name, p, sizeof a.name); fs_copy (a.label, p, sizeof a.label); a.tool = false;
		char cat[24] = "";
		char q[180]; int k = 0;
		lx_cat (q, sizeof q, &k, "SD:/apps/"); lx_cat (q, sizeof q, &k, a.name); lx_cat (q, sizeof q, &k, ".app/app.txt");
		if (app_ini_load_path (q) >= 0)
		{
			const char *nm = app_ini_get (0, "name", 0); if (nm && nm[0]) fs_copy (a.label, nm, sizeof a.label);
			const char *ct = app_ini_get (0, "category", 0); if (ct && ct[0]) fs_copy (cat, ct, sizeof cat);
		}
		// (the desktop's parts run from the boot on, the applets live in this panel: not offered)
		if (fs_ci_cmp (cat, "Shell") != 0 && fs_ci_cmp (cat, "Settings") != 0) g_nprogs++;
		*e = c;
		p = *e ? e + 1 : e;
	}
	int napps = g_nprogs;
	sort_progs (0, napps);
	void *d = kapi_opendir ("SD:/bin");
	if (d)
	{
		struct kapi_dirent ent;
		while (g_nprogs < MAXPROGS && kapi_readdir (d, &ent))
		{
			if (ent.is_dir || fs_skip_dot (ent.name)) continue;
			int l = fs_len (ent.name);
			if (l > 4 && fs_ci_cmp (ent.name + l - 4, ".bax") == 0) continue;	// (a BASIC program: no image)
			Prog &a = g_progs[g_nprogs++];
			fs_copy (a.name, ent.name, sizeof a.name); a.tool = true;
			int k = 0; lx_cat (a.label, sizeof a.label, &k, "/bin/"); lx_cat (a.label, sizeof a.label, &k, ent.name);
		}
		kapi_closedir (d);
	}
	sort_progs (napps, g_nprogs);
}

static const char *label_of (const char *name)
{
	for (int i = 0; i < g_nprogs; i++) if (fs_ci_cmp (g_progs[i].name, name) == 0) return g_progs[i].label;
	return name;
}

// ---- the list ------------------------------------------------------------------------------------
static void put_mb (char *s, int cap, int *n, unsigned long long bytes)
{
	char b[16];
	unsigned kb = (unsigned) ((bytes + 1023) >> 10);
	if (kb >= 1024) { ax_itoa ((int) ((kb + 512) >> 10), b); lx_cat (s, cap, n, b); lx_cat (s, cap, n, " MB"); }
	else { ax_itoa ((int) kb, b); lx_cat (s, cap, n, b); lx_cat (s, cap, n, " KB"); }
}

static void fill_list (int sel, bool force = true)
{
	static char shown[PRELOAD_MAX][64];
	char now[PRELOAD_MAX][64];
	unsigned long long total = 0;
	for (int i = 0; i < g_list.n; i++)
	{
		char path[300]; char *s = now[i]; int n = 0;
		const int cap = 64;
		img_program (g_list.prog[i], path, sizeof path, 0);
		lx_cat (s, cap, &n, label_of (g_list.prog[i]));
		struct kapi_stat st;
		struct kapi_image_info im;
		if (kapi_path_stat (path, &st) < 0) lx_cat (s, cap, &n, "  --  missing");
		else
		{
			total += st.size;
			lx_cat (s, cap, &n, "  --  "); put_mb (s, cap, &n, st.size);
			if (kapi_image_list (path, &im, 1) == 1)
				lx_cat (s, cap, &n, (im.flags & KAPI_IMG_LOADING) ? ", loading" : (im.flags & KAPI_IMG_KEPT) ? ", in memory" : "");
		}
	}
	bool same = !force && g_lbList->count == g_list.n;	// (the tick: the list again only when a line changed)
	for (int i = 0; same && i < g_list.n; i++) if (fs_ci_cmp (shown[i], now[i]) != 0) same = false;
	if (same) return;
	g_lbList->clear ();
	for (int i = 0; i < g_list.n; i++) { fs_copy (shown[i], now[i], 64); g_lbList->add (now[i]); }
	g_lbList->setSel (sel < g_list.n ? sel : g_list.n - 1);
	char t[96]; int n = 0; char b[12];
	ax_itoa (g_list.n, b); lx_cat (t, sizeof t, &n, b);
	lx_cat (t, sizeof t, &n, g_list.n == 1 ? " program, " : " programs, ");
	put_mb (t, sizeof t, &n, total);
	lx_cat (t, sizeof t, &n, " kept in memory from the boot on");
	g_total->setText (g_list.n ? t : "Nothing is preloaded.");
}

static bool save (void)
{
	if (preload_ini_save (&g_list)) return true;
	g_status->setText ("Could not write SD:/etc/preload.ini.");
	return false;
}

// ---- the actions ---------------------------------------------------------------------------------
static void on_add (Widget &)
{
	int a = g_lbAll->sel;
	if (a < 0 || a >= g_nprogs) { g_status->setText ("Pick a program in the list on the right first."); return; }
	for (int i = 0; i < g_list.n; i++)
		if (fs_ci_cmp (g_list.prog[i], g_progs[a].name) == 0) { g_lbList->setSel (i); g_status->setText ("It is in the list already."); return; }
	if (g_list.n >= PRELOAD_MAX) { g_status->setText ("32 programs at most."); return; }
	fs_copy (g_list.prog[g_list.n++], g_progs[a].name, PRELOAD_NAME);
	if (!save ()) { g_list.n--; return; }
	char path[300];
	img_program (g_progs[a].name, path, sizeof path, 0);
	int r = kapi_image_preload (path);
	g_status->setText (r == 0 ? "Added: it is being loaded now, and will be at every boot."
			 : r == -KAPI_ENOMEM ? "Added, but there is no memory to load it now."
			 : "Added: it will be loaded at the next boot.");
	fill_list (g_list.n - 1);
}

static void on_remove (Widget &)
{
	int l = g_lbList->sel;
	if (l < 0 || l >= g_list.n) { g_status->setText ("Pick a program in the list on the left first."); return; }
	char path[300];
	img_program (g_list.prog[l], path, sizeof path, 1);
	for (int i = l; i + 1 < g_list.n; i++) fs_copy (g_list.prog[i], g_list.prog[i + 1], PRELOAD_NAME);
	g_list.n--;
	if (!save ()) { preload_ini_load (&g_list); fill_list (l); return; }
	kapi_image_unload (path);
	g_status->setText ("Removed: its memory is freed when its last window closes.");
	fill_list (l);
}

class PreloadRoot : public Root
{
public:
	unsigned last = 0;
	PreloadRoot () : Root (W, H, "Preload") {}
	void onTick () override				// ("loading" becomes "in memory")
	{
		unsigned now = kapi_get_ticks ();
		if (now - last >= 100) { last = now; fill_list (g_lbList->sel, false); }
	}
};

int main (void)
{
	ft_uikit_install ("DejaVu Sans", 13);		// (before the widgets; false: the bitmap font)
	PreloadRoot root;
	if (root.canvas.px == 0) return 1;
	int X = root.width > W ? (root.width - W) / 2 : 0;

	root.addChild (new Label (X + 12, 8, W - 24, 20, "A program loaded ahead starts without reading the card: its code is read once, at boot,", C_TEXT, root.bg));
	root.addChild (new Label (X + 12, 28, W - 24, 20, "and stays in memory (shared by all its windows). Worth it for the large ones, as Web.", C_TEXT, root.bg));

	GroupBox *gl = new GroupBox (X + 10, 56, 330, 330, "Loaded at boot");
	root.addChild (gl);
	int ct = gl->contentTop () + 4;
	g_lbList = new ListBox (10, ct, 310, 240); gl->addChild (g_lbList);
	gl->addChild (new Button (10, ct + 248, 100, 28, "Remove", on_remove));

	GroupBox *ga = new GroupBox (X + 350, 56, W - 360, 330, "Programs");
	root.addChild (ga);
	g_lbAll = new ListBox (10, ct, W - 380, 240, 0, on_add); ga->addChild (g_lbAll);
	g_lbAll->tip = "The apps, then the /bin tools: pick one, then < Add (or double-click it)";
	ga->addChild (new Button (10, ct + 248, 100, 28, "< Add", on_add));

	g_total = new Label (X + 12, 394, W - 24, 22, "", C_TEXT, root.bg); root.addChild (g_total);
	g_status = new Label (X + 12, 418, W - 24, 22, "", C_DIS, root.bg); root.addChild (g_status);
	root.addChild (new Label (X + 12, H - 26, W - 24, 20, "Kept in SD:/etc/preload.ini; read at boot by the last line of SD:/etc/autostart: preload /boot.", C_DIS, root.bg));

	scan_progs ();
	for (int i = 0; i < g_nprogs; i++) g_lbAll->add (g_progs[i].label);
	preload_ini_load (&g_list);
	fill_list (0);
	root.run ();
	return 0;
}
