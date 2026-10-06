//
// dockconf.inc -- the code of dockconf.h (SystemKit: compiled into systemkit.so; a PC build has it inline).
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
#ifdef SK_IMPL
#define SK_FN	extern "C"
#else
#define SK_FN	static inline
#endif
SK_FN void dc_copy (char *d, const char *s, int cap) { int i = 0; for (; s && s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = 0; }
SK_FN void dockconf_defaults (DockConf &c)
{
	static const char *const D[6][2] = { { "Productivity", "tinypad" }, { "Internet", "jet" },
		{ "Graphics", "paint" }, { "Programming", "qbstudio" }, { "Games", "gamelib" }, { "Demos", "widgets" } };
	c.ndrawers = 6;
	for (int i = 0; i < 6; i++) { dc_copy (c.drawer[i].cat, D[i][0], 24); dc_copy (c.drawer[i].app, D[i][1], 32); }
	c.nlaunchers = 2;
	dc_copy (c.launcher[0], "terminal", 32); dc_copy (c.launcher[1], "fileviewer", 32);
	c.ndesks = 4;
	for (int i = 0; i < 4; i++) { c.desk[i][0] = (char) ('1' + i); c.desk[i][1] = 0; }
}
SK_FN void dc_split (const char *v, char *a, int acap, char *b, int bcap)
{
	int i = 0, n = 0;
	while (v[i] && v[i] != ',' && n < acap - 1) a[n++] = v[i++];
	while (n > 0 && a[n - 1] == ' ') n--;
	a[n] = 0;
	while (v[i] && v[i] != ',') i++;
	if (v[i] == ',') i++;
	while (v[i] == ' ') i++;
	n = 0;
	while (v[i] && n < bcap - 1) b[n++] = v[i++];
	while (n > 0 && b[n - 1] == ' ') n--;
	b[n] = 0;
}
SK_FN bool dockconf_load (DockConf &c)
{
	dockconf_defaults (c);
	void *f = kapi_open (DOCK_INI);
	if (f == 0) return false;
	static char buf[4096];
	int n = kapi_read (f, buf, sizeof buf - 1);
	kapi_close (f);
	if (n <= 0) return false;
	buf[n] = 0;
	int nd = 0, nl = 0, nk = 0;
	for (char *p = buf; *p; )
	{
		char *l = p; while (*p && *p != '\n') p++;
		if (*p) *p++ = 0;
		while (*l == ' ' || *l == '\t') l++;
		if (*l == ';' || *l == '#' || *l == 0) continue;
		char *eq = l; while (*eq && *eq != '=') eq++;
		if (*eq != '=') continue;
		char *ke = eq; while (ke > l && (ke[-1] == ' ' || ke[-1] == '\t')) ke--;
		*ke = 0;
		char *v = eq + 1; while (*v == ' ' || *v == '\t') v++;
		char *ve = v; while (*ve) ve++;
		while (ve > v && (ve[-1] == ' ' || ve[-1] == '\t' || ve[-1] == '\r')) *--ve = 0;
		bool drawer = l[0] == 'd' && l[1] == 'r', launcher = l[0] == 'l', desk = l[0] == 'd' && l[1] == 'e';
		if (drawer && nd < DOCK_MAXDRAWERS)
		{
			dc_split (v, c.drawer[nd].cat, 24, c.drawer[nd].app, 32);
			if (c.drawer[nd].cat[0]) nd++;
		}
		else if (launcher && nl < DOCK_MAXLAUNCHERS && *v) dc_copy (c.launcher[nl++], v, 32);
		else if (desk && nk < DOCK_MAXDESKS) dc_copy (c.desk[nk++], v, 24);
	}
	if (nd > 0) c.ndrawers = nd;
	c.nlaunchers = nl;
	if (nk > 0) c.ndesks = nk;
	return true;
}
SK_FN int dc_put (char *o, int p, int cap, const char *s) { while (s && *s && p < cap - 1) o[p++] = *s++; o[p] = 0; return p; }
SK_FN bool dockconf_save (const DockConf &c)
{
	static char o[4096];
	int p = 0;
	p = dc_put (o, p, sizeof o,
		"; The dock (apps/dock): written by the Control Panel's Panel applet (dockconf.h).\n"
		"; drawer = group, main app  (the group: the apps' \"category\" in their app.txt)\n"
		"; launcher = app            (after the drawers)\n"
		"; desk = name               (the workspaces, 1 to 6)\n");
	for (int i = 0; i < c.ndrawers; i++)
	{
		p = dc_put (o, p, sizeof o, "drawer = "); p = dc_put (o, p, sizeof o, c.drawer[i].cat);
		p = dc_put (o, p, sizeof o, ", "); p = dc_put (o, p, sizeof o, c.drawer[i].app); p = dc_put (o, p, sizeof o, "\n");
	}
	for (int i = 0; i < c.nlaunchers; i++) { p = dc_put (o, p, sizeof o, "launcher = "); p = dc_put (o, p, sizeof o, c.launcher[i]); p = dc_put (o, p, sizeof o, "\n"); }
	for (int i = 0; i < c.ndesks; i++) { p = dc_put (o, p, sizeof o, "desk = "); p = dc_put (o, p, sizeof o, c.desk[i]); p = dc_put (o, p, sizeof o, "\n"); }
	return kapi_save_file (DOCK_INI, o, (unsigned) p) >= 0;
}
SK_FN bool dock_category_docked (const char *cat)
{
	static const char *const NO[3] = { "Shell", "Settings", "Emulators" };
	for (int i = 0; i < 3; i++)
	{
		int k = 0;
		while (cat[k] && NO[i][k] && (cat[k] | 32) == (NO[i][k] | 32)) k++;
		if (!cat[k] && !NO[i][k]) return false;
	}
	return cat[0] != 0;
}
static inline bool dc_same (const char *a, const char *b)
{
	int k = 0;
	while (a[k] && b[k] && (a[k] | 32) == (b[k] | 32)) k++;
	return !a[k] && !b[k];
}
static inline int dc_find (const DockLayout &l, const char *cat)
{
	for (int i = 0; i < l.ncats; i++) if (dc_same (l.cat[i].cat, cat)) return i;
	return -1;
}
static inline int dc_add (DockLayout &l, const char *cat, const char *app)
{
	int i = dc_find (l, cat);
	if (i >= 0) return i;
	if (l.ncats >= DOCK_MAXCATS) return -1;
	DockCat &c = l.cat[l.ncats];
	dc_copy (c.cat, cat, 24); dc_copy (c.app, app, 32);
	c.hidden = 0; c.napps = 0; c.reserved[0] = c.reserved[1] = 0;
	return l.ncats++;
}
SK_FN bool dock_layout_load (DockLayout &l)
{
	static const char *const D[8][2] = { { "Productivity", "tinypad" }, { "Internet", "jet" }, { "Graphics", "paint" },
		{ "Programming", "qbstudio" }, { "Games", "gamelib" }, { "Multimedia", "media" }, { "Demos", "widgets" }, { "System", "taskman" } };
	l.ncats = 0; l.nlaunchers = 0; l.ndesks = 0;
	for (int i = 0; i < 8; i++) l.reserved[i] = 0;
	// the file: the drawers in their order, the hidden ones, the launchers, the workspaces
	bool file = false;
	void *f = kapi_open (DOCK_INI);
	if (f != 0)
	{
		static char buf[8192];
		int n = kapi_read (f, buf, sizeof buf - 1);
		kapi_close (f);
		if (n > 0)
		{
			file = true;
			buf[n] = 0;
			for (char *p = buf; *p; )
			{
				char *ln = p; while (*p && *p != '\n') p++;
				if (*p) *p++ = 0;
				while (*ln == ' ' || *ln == '\t') ln++;
				if (*ln == ';' || *ln == '#' || *ln == 0) continue;
				char *eq = ln; while (*eq && *eq != '=') eq++;
				if (*eq != '=') continue;
				char *ke = eq; while (ke > ln && (ke[-1] == ' ' || ke[-1] == '\t')) ke--;
				*ke = 0;
				char *v = eq + 1; while (*v == ' ' || *v == '\t') v++;
				char *ve = v; while (*ve) ve++;
				while (ve > v && (ve[-1] == ' ' || ve[-1] == '\t' || ve[-1] == '\r')) *--ve = 0;
				if (dc_same (ln, "drawer"))
				{
					char cat[24], app[32];
					dc_split (v, cat, 24, app, 32);
					if (cat[0] && dock_category_docked (cat)) { int i = dc_add (l, cat, app); if (i >= 0 && !l.cat[i].app[0]) dc_copy (l.cat[i].app, app, 32); }
				}
				else if (dc_same (ln, "hidden")) { if (*v) { int i = dc_add (l, v, ""); if (i >= 0) l.cat[i].hidden = 1; } }
				else if (dc_same (ln, "launcher")) { if (*v && l.nlaunchers < DOCK_MAXLAUNCHERS) dc_copy (l.launcher[l.nlaunchers++], v, 32); }
				else if (dc_same (ln, "desk")) { if (l.ndesks < DOCK_MAXDESKS) dc_copy (l.desk[l.ndesks++], v, 24); }
			}
		}
	}
	if (!file)
	{
		for (int i = 0; i < 8; i++) dc_add (l, D[i][0], D[i][1]);
		dc_copy (l.launcher[0], "terminal", 32); dc_copy (l.launcher[1], "fileviewer", 32); l.nlaunchers = 2;
	}
	if (l.ndesks == 0) { for (int i = 0; i < 4; i++) { l.desk[i][0] = (char) ('1' + i); l.desk[i][1] = 0; } l.ndesks = 4; }
	// the card's apps: their categories counted, the new ones added (by name, after those named)
	static char list[8192];
	kapi_list_apps (list, sizeof list);
	int first = l.ncats;
	for (char *p = list; *p; )
	{
		char *e = p; while (*e && *e != '\n') e++;
		char c = *e; *e = 0;
		char q[180]; int k = 0;
		lx_cat (q, sizeof q, &k, "SD:/apps/"); lx_cat (q, sizeof q, &k, p); lx_cat (q, sizeof q, &k, ".app/app.txt");
		char cat[24] = "Other";
		if (app_ini_load_path (q) >= 0) { const char *ct = app_ini_get (0, "category", 0); if (ct && ct[0]) dc_copy (cat, ct, 24); }
		if (dock_category_docked (cat))
		{
			int i = dc_find (l, cat);
			if (i < 0) i = dc_add (l, cat, "");
			if (i >= 0) l.cat[i].napps++;
		}
		*e = c;
		p = *e ? e + 1 : e;
	}
	for (int i = first + 1; i < l.ncats; i++)		// (the new ones by name)
		for (int j = i; j > first; j--)
		{
			const char *a = l.cat[j - 1].cat, *b = l.cat[j].cat;
			int k = 0; while (a[k] && (a[k] | 32) == (b[k] | 32)) k++;
			if ((a[k] | 32) <= (b[k] | 32)) break;
			DockCat t = l.cat[j]; l.cat[j] = l.cat[j - 1]; l.cat[j - 1] = t;
		}
	// a main app that is not on the card any more: none (the first of its apps)
	for (int i = 0; i < l.ncats; i++)
		if (l.cat[i].app[0])
		{
			char q[180]; int k = 0;
			lx_cat (q, sizeof q, &k, "SD:/apps/"); lx_cat (q, sizeof q, &k, l.cat[i].app); lx_cat (q, sizeof q, &k, ".app/app.txt");
			void *h = kapi_open (q);
			if (h) kapi_close (h); else l.cat[i].app[0] = 0;
		}
	return file;
}
SK_FN bool dock_layout_save (const DockLayout &l)
{
	static char o[8192];
	int p = 0;
	p = dc_put (o, p, sizeof o,
		"; The dock (apps/dock): written by the Control Panel's Panel applet (dockconf.h).\n"
		"; drawer = category, main app  (the categories in the dock's order; one the file does not name is added at the end)\n"
		"; hidden = category            (no drawer for it)\n"
		"; launcher = app               (after the drawers)\n"
		"; desk = name                  (the workspaces, 1 to 6)\n");
	for (int i = 0; i < l.ncats; i++)
	{
		p = dc_put (o, p, sizeof o, "drawer = "); p = dc_put (o, p, sizeof o, l.cat[i].cat);
		p = dc_put (o, p, sizeof o, ", "); p = dc_put (o, p, sizeof o, l.cat[i].app); p = dc_put (o, p, sizeof o, "\n");
	}
	for (int i = 0; i < l.ncats; i++) if (l.cat[i].hidden) { p = dc_put (o, p, sizeof o, "hidden = "); p = dc_put (o, p, sizeof o, l.cat[i].cat); p = dc_put (o, p, sizeof o, "\n"); }
	for (int i = 0; i < l.nlaunchers; i++) { p = dc_put (o, p, sizeof o, "launcher = "); p = dc_put (o, p, sizeof o, l.launcher[i]); p = dc_put (o, p, sizeof o, "\n"); }
	for (int i = 0; i < l.ndesks; i++) { p = dc_put (o, p, sizeof o, "desk = "); p = dc_put (o, p, sizeof o, l.desk[i]); p = dc_put (o, p, sizeof o, "\n"); }
	return kapi_save_file (DOCK_INI, o, (unsigned) p) >= 0;
}
SK_FN int dock_running (void)
{
	static char t[4096];
	kapi_list_tasks (t, sizeof t);
	for (const char *p = t; *p; )
	{
		const char *e = p; while (*e && *e != '\n') e++;
		if (e - p == 7 && p[2] == ' ' && p[3] == 'd' && p[4] == 'o' && p[5] == 'c' && p[6] == 'k') return 1;
		p = *e ? e + 1 : e;
	}
	return 0;
}
SK_FN void dock_reload (void)
{
	if (!kapi_kill ("dock")) return;
	for (int i = 0; i < 30 && dock_running (); i++) kapi_msleep (50);	// (gone, its service too)
	kapi_launch ("dock");
}

#undef SK_FN
