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
	char text[128];			// (an applet: its line of help, in the language)
	bool applet;			// a Control Panel applet (the Settings tab)
	bool hidden;			// found by the search only (the Control Panel: its applets have the Settings tab)
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
	if (s > a.iw)						// enlarged: bilinear, the see-through pixels (magenta) left out
	{
		for (int j = 0; j < s; j++)
			for (int i = 0; i < s; i++)
			{
				int fx = ((2 * i + 1) * a.iw * 128) / s - 128, fy = ((2 * j + 1) * a.ih * 128) / s - 128;	// (1/256 px)
				if (fx < 0) fx = 0;
				if (fy < 0) fy = 0;
				int x0 = fx >> 8, y0 = fy >> 8, wx = fx & 255, wy = fy & 255;
				unsigned r = 0, g = 0, b = 0, wsum = 0;
				for (int k = 0; k < 4; k++)
				{
					int x = x0 + (k & 1), y = y0 + (k >> 1);
					if (x >= a.iw) x = a.iw - 1;
					if (y >= a.ih) y = a.ih - 1;
					unsigned w = (unsigned) (((k & 1) ? wx : 256 - wx) * ((k >> 1) ? wy : 256 - wy));
					unsigned c = a.icon[y * a.iw + x] & 0xFFFFFF;
					if (c == 0xFF00FF || w == 0) continue;
					r += ((c >> 16) & 255) * w; g += ((c >> 8) & 255) * w; b += (c & 255) * w; wsum += w;
				}
				unsigned op = wsum * 255 / 65536;
				a.scaled[j * s + i] = wsum == 0 ? 0xFF000000u : ((255 - op) << 24) | ((r / wsum) << 16) | ((g / wsum) << 8) | (b / wsum);
			}
		return;
	}
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
		// the desktop's parts (Shell), the applets (Settings: their .lnk below) and the emulators (the Game Library's);
		// the Control Panel itself: found by the search (its applets: the Settings tab)
		if (ieq (a.name, "control")) { a.hidden = true; g_napps++; }
		else if (!ieq (a.cat, "Shell") && !ieq (a.cat, "Settings") && !ieq (a.cat, "Emulators")) g_napps++;
		*e = c;
		p = *e ? e + 1 : e;
	}
	// the Control Panel's applets: the Settings tab (each opens the Control Panel full screen on it: open_app); their
	// help lines in the language from the Control Panel's own catalogue (SD:/apps/control.app/lang/<code>.txt)
	static char cat_[16384];
	int ncat = 0;
	if (uk_lang ()[0] && !ieq (uk_lang (), "en"))
	{
		char q[80]; int k = 0;
		lx_cat (q, sizeof q, &k, "SD:/apps/control.app/lang/"); lx_cat (q, sizeof q, &k, uk_lang ()); lx_cat (q, sizeof q, &k, ".txt");
		void *h = kapi_open (q);
		if (h != 0) { ncat = kapi_read (h, cat_, sizeof cat_ - 1); kapi_close (h); }
		if (ncat < 0) ncat = 0;
	}
	cat_[ncat] = 0;
	auto ctl_tr = [&] (const char *en, char *o, int cap)	// en's line "en<TAB>translation" -> o (else en)
	{
		int L = (int) strlen (en);
		for (int i = 0; L > 0 && i + L < ncat; )
		{
			if ((i == 0 || cat_[i - 1] == '\n') && memcmp (cat_ + i, en, (size_t) L) == 0 && cat_[i + L] == '\t')
			{
				int j = i + L + 1, m = 0;
				while (j < ncat && cat_[j] != '\n' && cat_[j] != '\r' && m < cap - 1) o[m++] = cat_[j++];
				o[m] = 0;
				return;
			}
			while (i < ncat && cat_[i] != '\n') i++;
			i++;
		}
		fs_copy (o, en, cap);
	};
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
		ctl_tr (tx ? tx : "", a.text, sizeof a.text);
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
	for (int a = 0; a < g_napps; a++) if (!g_apps[a].applet && !g_apps[a].hidden) add_cat (g_apps[a].cat);
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
// The results: the best match first, then the apps, the settings (the applets, by their names and help lines), the
// files, and the text as a command last. kind (0 all, 1 apps, 2 settings, 3 files) keeps one kind; counts[] says how
// many of each kind match (counts[0] all of them).
enum { R_APP, R_SETTING, R_FILE, R_RUN };
struct Result { int kind; int app; int score; char title[64]; char sub[128]; char path[128]; };
#define MAXRESULTS	40
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
static int match_score (const char *s, const char *q)		// 3 s begins with q, 2 one of its words does, 1 q in it, 0 not
{
	for (int i = 0; s[i]; i++)
	{
		if (i > 0 && s[i - 1] != ' ' && s[i - 1] != '-' && s[i - 1] != '_' && s[i - 1] != '.') continue;
		int k = 0;
		while (q[k] && s[i + k] && lx_low (s[i + k]) == lx_low (q[k])) k++;
		if (!q[k]) return i == 0 ? 3 : 2;
	}
	return contains (s, q) ? 1 : 0;
}

// The files the search sees: SD:/docs and the user's folders (two levels), read once a search starts.
#define MAXFILES	200
static char g_files[MAXFILES][96];
static unsigned g_fsize[MAXFILES];
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
		g_fsize[g_nfiles] = (unsigned) de.size;
		fs_copy (g_files[g_nfiles++], p, sizeof g_files[0]);
	}
	if (d != 0) kapi_closedir (d);
}
static void size_cat (char *o, int cap, int *k, unsigned b)	// "1.2 KB", "152 KB", "3.4 MB"
{
	const char *u = " B"; unsigned d = 1;
	if (b >= 1024u * 1024u) { u = " MB"; d = 1024u * 1024u; }
	else if (b >= 1024u) { u = " KB"; d = 1024u; }
	unsigned w = b / d, t = (b % d) * 10 / d;
	char n[16]; int i = 0;
	do { n[i++] = (char) ('0' + w % 10); w /= 10; } while (w);
	while (i) { char c[2] = { n[--i], 0 }; lx_cat (o, cap, k, c); }
	if (d > 1 && b / d < 10) { char c[3] = { '.', (char) ('0' + t), 0 }; lx_cat (o, cap, k, c); }
	lx_cat (o, cap, k, u);
}

static void search (const char *q, int kind, int *counts)
{
	g_nres = 0;
	for (int i = 0; i < 4; i++) counts[i] = 0;
	if (!q[0]) return;
	static Result all[MAXRESULTS * 3];
	int n = 0;
	for (int i = 0; i < g_napps && n < MAXRESULTS * 3; i++)		// the apps
	{
		App &a = g_apps[i];
		if (a.applet) continue;
		int sc = match_score (a.label, q);
		if (sc == 0 && contains (a.name, q)) sc = 1;
		if (sc == 0) continue;
		Result &r = all[n++];
		memset (&r, 0, sizeof r);
		int rk = 0;							// (as good: the one opened last first)
		for (int k = 0; k < g_nrecent; k++) if (ieq (g_recent[k], a.name)) { rk = RECENT_MAX - k; break; }
		r.kind = R_APP; r.app = i; r.score = (sc + 1) * 100 + rk;	// (an app before a setting or a file as good)
		fs_copy (r.title, a.label, sizeof r.title);
		char s[96]; int k = 0; s[0] = 0;
		lx_cat (s, sizeof s, &k, TR (a.cat));
		if (task_of (a.name) >= 0) { lx_cat (s, sizeof s, &k, "  -  "); lx_cat (s, sizeof s, &k, TR ("running")); }
		fs_copy (r.sub, s, sizeof r.sub);
	}
	for (int i = 0; i < g_napps && n < MAXRESULTS * 3; i++)		// the settings: by their names, their help lines
	{
		App &a = g_apps[i];
		if (!a.applet) continue;
		int sc = match_score (TR (a.label), q);
		if (sc == 0) sc = match_score (a.label, q);
		if (sc == 0 && (contains (TR (a.text), q) || contains (a.text, q))) sc = 1;
		if (sc == 0) continue;
		Result &r = all[n++];
		memset (&r, 0, sizeof r);
		r.kind = R_SETTING; r.app = i; r.score = sc * 100;
		fs_copy (r.title, TR (a.label), sizeof r.title);
		fs_copy (r.sub, TR (a.text), sizeof r.sub);
	}
	if (g_nfiles < 0) { g_nfiles = 0; files_scan ("SD:/docs", 2); files_scan ("SD:/Notes", 0); files_scan ("SD:/home", 2); }
	for (int i = 0; i < g_nfiles && n < MAXRESULTS * 3; i++)		// the files
	{
		const char *base = g_files[i];
		for (const char *p = g_files[i]; *p; p++) if (*p == '/') base = p + 1;
		int sc = match_score (base, q);
		if (sc == 0) continue;
		Result &r = all[n++];
		memset (&r, 0, sizeof r);
		r.kind = R_FILE; r.app = -1; r.score = sc * 100;
		fs_copy (r.title, base, sizeof r.title);
		fs_copy (r.path, g_files[i], sizeof r.path);
		char app[32], s[128]; int k = 0; s[0] = 0;
		if (fa_app_for (g_files[i], app, sizeof app)) { App *a = find_app (app); lx_cat (s, sizeof s, &k, a ? a->label : app); lx_cat (s, sizeof s, &k, "  -  "); }
		int dn = (int) (base - g_files[i]) - 1;
		char dir[96]; if (dn > 95) dn = 95; if (dn < 0) dn = 0;
		memcpy (dir, g_files[i], (size_t) dn); dir[dn] = 0;
		lx_cat (s, sizeof s, &k, dir); lx_cat (s, sizeof s, &k, "  -  "); size_cat (s, sizeof s, &k, g_fsize[i]);
		fs_copy (r.sub, s, sizeof r.sub);
	}
	for (int i = 0; i < n; i++) { counts[0]++; counts[all[i].kind + 1]++; }
	// each kind by its score (the best first), then its name
	for (int i = 1; i < n; i++)
		for (int j = i; j > 0; j--)
		{
			Result &a = all[j], &b = all[j - 1];
			bool before = a.kind < b.kind || (a.kind == b.kind && (a.score > b.score || (a.score == b.score && fs_ci_cmp (a.title, b.title) < 0)));
			if (!before) break;
			Result t = a; a = b; b = t;
		}
	int best = -1;							// the best match: the best score of the kinds shown
	for (int i = 0; i < n; i++)
		if ((kind == 0 || all[i].kind == kind - 1) && (best < 0 || all[i].score > all[best].score)) best = i;
	if (best >= 0) g_res[g_nres++] = all[best];
	for (int i = 0; i < n && g_nres < MAXRESULTS - 1; i++)
		if (kind == 0 || all[i].kind == kind - 1) g_res[g_nres++] = all[i];
	Result &r = g_res[g_nres++];						// the text as a command
	memset (&r, 0, sizeof r);
	r.kind = R_RUN; r.app = -1;
	fs_copy (r.title, q, sizeof r.title);
	fs_copy (r.sub, TR ("run it in a Terminal (a /bin command)"), sizeof r.sub);
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
