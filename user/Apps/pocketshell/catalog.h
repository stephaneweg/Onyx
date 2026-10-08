//
// catalog.h -- pocketshell's catalogue: the card's apps (their app.txt: name, category; their icon), the Control
// Panel's applets (SD:/apps/control.app/applets/*.lnk: the Settings tab), the categories in the dock's order
// (SystemKit's dock_layout_load: the launcher's tabs), the recent apps (SD:/etc/pocket/recent), the running ones
// (uk_shell_tasks) and the search (apps, settings, files of SD:/docs, a /bin command). Part of main.cpp (one unit).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//

#define MAXAPPS		160
#define MAXCATS		24
#define RECENT_FILE	"SD:/etc/pocket/recent"
#define RECENT_MAX	16

struct App
{
	char name[32];			// its folder: SD:/apps/<name>.app
	char label[40];			// app.txt's name
	char cat[24];			// app.txt's category ("Other": none)
	char text[64];			// (an applet: its line of help)
	bool applet;			// a Control Panel applet (the Settings tab)
	unsigned *icon; int iw, ih;	// icon.bmp (read the first time it is drawn)
	bool tried;
	unsigned *scaled; int ss;	// ... scaled to ss x ss (0xTTRRGGBB)
};
static App g_apps[MAXAPPS];
static int g_napps;

struct Cat { char name[24]; unsigned dot; };
static Cat g_cats[MAXCATS];		// the launcher's tabs: Recent, the categories, Settings
static int g_ncats;

static char g_recent[RECENT_MAX][32];
static int g_nrecent;

static struct uk_shell_task g_tasks[UK_TASKS_MAX];
static int g_ntasks;
static bool g_home = true;		// no app in front: the launcher shows

static bool ieq (const char *a, const char *b)	{ return fs_ci_cmp (a, b) == 0; }

static App *find_app (const char *name)
{
	for (int i = 0; i < g_napps; i++) if (!g_apps[i].applet && ieq (g_apps[i].name, name)) return &g_apps[i];
	return 0;
}

// The colour of a category's dot (the dock's drawers' identity: Productivity amber, Internet blue...).
static unsigned cat_dot (const char *c)
{
	static const struct { const char *n; unsigned c; } K[] = {
		{ "Recent", 0x8A8F99 }, { "Productivity", 0xE8A33A }, { "Internet", 0x3D86DA }, { "Graphics", 0x4CAF50 },
		{ "Multimedia", 0x9C5BD0 }, { "Games", 0xE0533F }, { "Programming", 0x26A69A }, { "System", 0x6F7B8F },
		{ "Demos", 0xD0679E }, { "BASIC", 0x7E8C3A }, { "Settings", 0x5A6B85 } };
	for (auto &k : K) if (ieq (k.n, c)) return k.c;
	unsigned h = 2166136261u;
	for (; *c; c++) h = (h ^ (unsigned char) *c) * 16777619u;
	return 0x404040 + (h & 0x7F7F7F);
}

static void app_icon (App &a, int s)		// its icon at s x s (read once, scaled again when s changes)
{
	if (!a.tried)
	{
		a.tried = true;
		char q[180]; int k = 0;
		lx_cat (q, sizeof q, &k, "SD:/apps/"); lx_cat (q, sizeof q, &k, a.name); lx_cat (q, sizeof q, &k, ".app/icon.bmp");
		a.icon = ui::icon_load (q, &a.iw, &a.ih);
	}
	if (a.scaled != 0 && a.ss == s) return;
	delete [] a.scaled;
	a.scaled = new unsigned[s * s];
	a.ss = s;
	if (a.icon == 0 || a.iw <= 0 || a.ih <= 0) { for (int i = 0; i < s * s; i++) a.scaled[i] = 0xFF000000u; return; }
	// 4 x 4 samples a pixel: the colour of the opaque ones, the opacity their share (magenta: see-through)
	for (int j = 0; j < s; j++)
		for (int i = 0; i < s; i++)
		{
			unsigned r = 0, g = 0, b = 0, n = 0;
			for (int sy = 0; sy < 4; sy++)
				for (int sx = 0; sx < 4; sx++)
				{
					int x = ((i * 4 + sx) * a.iw + a.iw / 2) / (s * 4), y = ((j * 4 + sy) * a.ih + a.ih / 2) / (s * 4);
					unsigned c = a.icon[y * a.iw + x] & 0xFFFFFF;
					if (c == 0xFF00FF) continue;
					r += (c >> 16) & 255; g += (c >> 8) & 255; b += c & 255; n++;
				}
			a.scaled[j * s + i] = n == 0 ? 0xFF000000u : ((255 - n * 255 / 16) << 24) | ((r / n) << 16) | ((g / n) << 8) | (b / n);
		}
}

static void draw_icon (Canvas &cv, int x, int y, App &a, int s)
{
	app_icon (a, s);
	for (int j = 0; j < s; j++)
		for (int i = 0; i < s; i++)
		{
			unsigned c = a.scaled[j * s + i];
			int t = (int) (c >> 24);
			if (t == 255) continue;
			if (t == 0) cv.pixel (x + i, y + j, c & 0xFFFFFF);
			else uk_blend_px (cv, x + i, y + j, c & 0xFFFFFF, 255 - t);
		}
}

static void scan_apps (void)
{
	for (int i = 0; i < g_napps; i++) { delete [] g_apps[i].icon; delete [] g_apps[i].scaled; }
	g_napps = 0;
	static char list[4096];
	kapi_list_apps (list, sizeof list);
	for (char *p = list; *p && g_napps < MAXAPPS; )
	{
		char *e = p; while (*e && *e != '\n') e++;
		char c = *e; *e = '\0';
		App &a = g_apps[g_napps];
		memset (&a, 0, sizeof a);
		fs_copy (a.name, p, sizeof a.name);
		fs_copy (a.label, p, sizeof a.label);
		fs_copy (a.cat, "Other", sizeof a.cat);
		char q[180]; int k = 0;
		lx_cat (q, sizeof q, &k, "SD:/apps/"); lx_cat (q, sizeof q, &k, a.name); lx_cat (q, sizeof q, &k, ".app/app.txt");
		if (app_ini_load_path (q) >= 0)
		{
			const char *nm = app_ini_get (0, "name", 0);
			if (nm && nm[0]) fs_copy (a.label, nm, sizeof a.label);
			const char *ct = app_ini_get (0, "category", 0);
			if (ct && ct[0]) fs_copy (a.cat, ct, sizeof a.cat);
		}
		// the desktop's parts (Shell), the applets (Settings: their .lnk below) and the emulators (the Game Library's)
		if (!ieq (a.cat, "Shell") && !ieq (a.cat, "Settings") && !ieq (a.cat, "Emulators")) g_napps++;
		*e = c;
		p = *e ? e + 1 : e;
	}
	// the Control Panel's applets: the Settings tab (each opens the Control Panel full screen on it: open_app)
	void *d = kapi_opendir ("SD:/apps/control.app/applets");
	struct kapi_dirent de;
	while (d != 0 && kapi_readdir (d, &de) && g_napps < MAXAPPS)
	{
		int n = lx_len (de.name);
		if (de.is_dir || n < 5 || !ieq (de.name + n - 4, ".lnk")) continue;
		char q[180]; int k = 0;
		lx_cat (q, sizeof q, &k, "SD:/apps/control.app/applets/"); lx_cat (q, sizeof q, &k, de.name);
		if (app_ini_load_path (q) < 0) continue;
		const char *t = app_ini_get (0, "target", 0), *nm = app_ini_get (0, "name", 0), *tx = app_ini_get (0, "text", 0);
		if (t == 0 || !t[0]) continue;
		App &a = g_apps[g_napps++];
		memset (&a, 0, sizeof a);
		fs_copy (a.name, t, sizeof a.name);
		fs_copy (a.label, nm && nm[0] ? nm : t, sizeof a.label);
		fs_copy (a.cat, "Settings", sizeof a.cat);
		fs_copy (a.text, tx ? tx : "", sizeof a.text);
		a.applet = true;
	}
	if (d != 0) kapi_closedir (d);
	// by label (the grid's order)
	for (int i = 1; i < g_napps; i++)
		for (int j = i; j > 0 && fs_ci_cmp (g_apps[j].label, g_apps[j - 1].label) < 0; j--)
		{ App t = g_apps[j]; g_apps[j] = g_apps[j - 1]; g_apps[j - 1] = t; }

	// the tabs: Recent, the dock's categories in its order (those with apps), the others, Settings
	g_ncats = 0;
	auto add_cat = [] (const char *n)
	{
		for (int i = 0; i < g_ncats; i++) if (ieq (g_cats[i].name, n)) return;
		if (g_ncats >= MAXCATS) return;
		fs_copy (g_cats[g_ncats].name, n, sizeof g_cats[g_ncats].name);
		g_cats[g_ncats].dot = cat_dot (n);
		g_ncats++;
	};
	add_cat ("Recent");
	static DockLayout L;
	dock_layout_load (L);
	for (int i = 0; i < L.ncats; i++)
	{
		if (L.cat[i].hidden || !dock_category_docked (L.cat[i].cat)) continue;
		bool any = false;
		for (int a = 0; a < g_napps && !any; a++) any = !g_apps[a].applet && ieq (g_apps[a].cat, L.cat[i].cat);
		if (any) add_cat (L.cat[i].cat);
	}
	for (int a = 0; a < g_napps; a++) if (!g_apps[a].applet) add_cat (g_apps[a].cat);
	add_cat ("Settings");
}

// ---- the recent apps ------------------------------------------------------------------------------------
static void recent_load (void)
{
	g_nrecent = 0;
	static char b[1024];
	int n = -1;
	void *h = kapi_open (RECENT_FILE);
	if (h != 0) { n = kapi_read (h, b, sizeof b - 1); kapi_close (h); }
	if (n > 0)
	{
		b[n] = 0;
		for (char *p = b; *p && g_nrecent < RECENT_MAX; )
		{
			char *e = p; while (*e && *e != '\n' && *e != '\r') e++;
			char c = *e; *e = 0;
			if (p[0] && p[0] != '#' && find_app (p)) fs_copy (g_recent[g_nrecent++], p, 32);
			*e = c;
			p = *e ? e + 1 : e;
			while (*p == '\n' || *p == '\r') p++;
		}
	}
	if (g_nrecent == 0)						// (a new card: the usual ones)
	{
		static const char *const D[] = { "terminal", "fileviewer", "tinypad", "tinycalc", "calendar", "gamelib", 0 };
		for (int i = 0; D[i] && g_nrecent < RECENT_MAX; i++) if (find_app (D[i])) fs_copy (g_recent[g_nrecent++], D[i], 32);
	}
}

static void recent_add (const char *name)			// an app opened: first of the recent ones (kept on the card)
{
	if (find_app (name) == 0) return;
	if (g_nrecent > 0 && ieq (g_recent[0], name)) return;
	int i = 0;
	while (i < g_nrecent && !ieq (g_recent[i], name)) i++;
	if (i == g_nrecent) { if (g_nrecent < RECENT_MAX) g_nrecent++; i = g_nrecent - 1; }
	for (; i > 0; i--) fs_copy (g_recent[i], g_recent[i - 1], 32);
	fs_copy (g_recent[0], name, 32);
	static char b[RECENT_MAX * 34 + 80];
	int n = 0;
	lx_cat (b, sizeof b, &n, "# pocketshell's recent apps (the launcher's Recent tab), the latest first\n");
	for (int k = 0; k < g_nrecent; k++) { lx_cat (b, sizeof b, &n, g_recent[k]); lx_cat (b, sizeof b, &n, "\n"); }
	kapi_mkdir ("SD:/etc/pocket");
	kapi_save_file (RECENT_FILE, b, (unsigned) n);
}

// ---- the running apps -----------------------------------------------------------------------------------
static int task_of (const char *name)				// the task of app name, -1: not running
{
	for (int i = 0; i < g_ntasks; i++) if (ieq (g_tasks[i].name, name)) return i;
	return -1;
}

static void tasks_read (void)
{
	static char s_front[24];
	g_ntasks = uk_shell_tasks (g_tasks, UK_TASKS_MAX);
	g_home = true;
	for (int i = 0; i < g_ntasks; i++)
		if (g_tasks[i].flags & UK_TASK_FRONT)
		{
			g_home = false;
			if (!ieq (s_front, g_tasks[i].name)) { fs_copy (s_front, g_tasks[i].name, sizeof s_front); recent_add (s_front); }
		}
}

// An app opened from the shell: brought to the front when it runs, else started (its name: SD:/apps/<name>.app).
// An applet's name (the Settings tab, a setting found, quick settings' Mode): the Control Panel, full screen, opens it
// -- `control <target>`; a Control Panel already running shows it (AP_OPEN) -- no applet runs alone (P6, the user).
static bool is_applet (const char *name)
{
	for (int i = 0; i < g_napps; i++) if (g_apps[i].applet && ieq (g_apps[i].name, name)) return true;
	return false;
}

static void open_app (const char *name)
{
	if (is_applet (name))
	{
		int c = task_of ("control");
		if (c >= 0) uk_shell_front (g_tasks[c].id, 1);
		lx_launch ("control", name);
		recent_add ("control");
		return;
	}
	int t = task_of (name);
	if (t >= 0) uk_shell_front (g_tasks[t].id, 1);
	else lx_launch (name, 0);
	recent_add (name);
}

// ---- the search: apps, settings, files, a command ---------------------------------------------------------
enum { R_APP, R_SETTING, R_FILE, R_RUN };
struct Result { int kind; int app; char title[64]; char sub[96]; char path[128]; };
#define MAXRESULTS	24
static Result g_res[MAXRESULTS];
static int g_nres;

static bool contains (const char *s, const char *q)		// q in s, ignoring the case
{
	if (!q[0]) return false;
	for (; *s; s++)
	{
		int i = 0;
		while (q[i] && s[i] && lx_low (s[i]) == lx_low (q[i])) i++;
		if (!q[i]) return true;
	}
	return false;
}

// The files the search sees: SD:/docs and the user's folders (two levels), read once a search starts.
#define MAXFILES	200
static char g_files[MAXFILES][96];
static int g_nfiles = -1;
static void files_scan (const char *dir, int depth)
{
	void *d = kapi_opendir (dir);
	struct kapi_dirent de;
	while (d != 0 && kapi_readdir (d, &de) && g_nfiles < MAXFILES)
	{
		if (de.name[0] == '.') continue;
		char p[128]; int k = 0;
		lx_cat (p, sizeof p, &k, dir); if (k > 0 && p[k - 1] != '/') lx_cat (p, sizeof p, &k, "/"); lx_cat (p, sizeof p, &k, de.name);
		if (de.is_dir) { if (depth > 0) files_scan (p, depth - 1); continue; }
		fs_copy (g_files[g_nfiles++], p, sizeof g_files[0]);
	}
	if (d != 0) kapi_closedir (d);
}

static void search (const char *q)
{
	g_nres = 0;
	if (!q[0]) return;
	for (int i = 0; i < g_napps && g_nres < MAXRESULTS; i++)		// the apps
	{
		App &a = g_apps[i];
		if (a.applet || (!contains (a.label, q) && !contains (a.name, q))) continue;
		Result &r = g_res[g_nres++];
		r.kind = R_APP; r.app = i;
		fs_copy (r.title, a.label, sizeof r.title);
		char s[96]; int k = 0; s[0] = 0;
		lx_cat (s, sizeof s, &k, TR (a.cat));
		if (task_of (a.name) >= 0) { lx_cat (s, sizeof s, &k, " -- "); lx_cat (s, sizeof s, &k, TR ("running")); }
		fs_copy (r.sub, s, sizeof r.sub);
	}
	for (int i = 0; i < g_napps && g_nres < MAXRESULTS; i++)		// the settings
	{
		App &a = g_apps[i];
		if (!a.applet || (!contains (TR (a.label), q) && !contains (a.label, q) && !contains (a.text, q))) continue;
		Result &r = g_res[g_nres++];
		r.kind = R_SETTING; r.app = i;
		char t[64]; int k = 0; t[0] = 0;
		lx_cat (t, sizeof t, &k, TR (a.label)); lx_cat (t, sizeof t, &k, ": "); lx_cat (t, sizeof t, &k, a.text);
		fs_copy (r.title, t, sizeof r.title);
		fs_copy (r.sub, TR ("Control Panel"), sizeof r.sub);
	}
	if (g_nfiles < 0) { g_nfiles = 0; files_scan ("SD:/docs", 2); files_scan ("SD:/Notes", 0); files_scan ("SD:/home", 2); }
	for (int i = 0; i < g_nfiles && g_nres < MAXRESULTS - 1; i++)		// the files
	{
		const char *base = g_files[i];
		for (const char *p = g_files[i]; *p; p++) if (*p == '/') base = p + 1;
		if (!contains (base, q)) continue;
		Result &r = g_res[g_nres++];
		r.kind = R_FILE; r.app = -1;
		fs_copy (r.title, base, sizeof r.title);
		fs_copy (r.path, g_files[i], sizeof r.path);
		int n = (int) (base - g_files[i]);
		if (n > (int) sizeof r.sub - 1) n = (int) sizeof r.sub - 1;
		memcpy (r.sub, g_files[i], (size_t) n); r.sub[n] = 0;
		r.app = -1;
	}
	if (g_nres < MAXRESULTS)						// the text as a command
	{
		Result &r = g_res[g_nres++];
		r.kind = R_RUN; r.app = -1;
		fs_copy (r.title, q, sizeof r.title);
		fs_copy (r.sub, TR ("run it in a Terminal (a /bin command)"), sizeof r.sub);
	}
}

static void open_result (const Result &r)
{
	switch (r.kind)
	{
	case R_APP: case R_SETTING: open_app (g_apps[r.app].name); break;
	case R_FILE: if (!fa_open (r.path)) lx_launch ("fileviewer", r.path); break;	// (its app, by its extension)
	case R_RUN: lx_launch ("terminal", r.title); break;				// (the Terminal types it in its first tab)
	}
}
