//
// dockconf.h -- the dock's settings (SD:/etc/dock.ini), shared by the dock (apps/dock) and the
// Control Panel's Panel applet (apps/dockconf), which writes them and tells the dock to read them
// again (IPC service "dock", DOCK_MSG_RELOAD). The file, one setting a line:
//
//     drawer   = Productivity, tinypad    a drawer: the apps' group (the "category" of their
//                                         app.txt) and its main app (its icon is the drawer's; a
//                                         click on it starts the app, the strip above opens the
//                                         drawer). In this order, left to right.
//     launcher = terminal                 a quick launcher after the drawers (a click starts it,
//                                         or brings it back)
//     desk     = Main                     a workspace (virtual desktop) and its name: 1 to
//                                         DOCK_MAXDESKS of them
//
#ifndef _dockconf_h
#define _dockconf_h

#include "kapi.h"

#define DOCK_INI		"SD:/etc/dock.ini"
#define DOCK_SERVICE		"dock"
#define DOCK_MSG_RELOAD		1		// (no payload) read dock.ini and theme.txt again
#define DOCK_MAXDRAWERS		8
#define DOCK_MAXLAUNCHERS	6
#define DOCK_MAXDESKS		6

struct DockDrawer { char cat[24]; char app[32]; };
struct DockConf
{
	DockDrawer drawer[DOCK_MAXDRAWERS]; int ndrawers;
	char launcher[DOCK_MAXLAUNCHERS][32]; int nlaunchers;
	char desk[DOCK_MAXDESKS][24]; int ndesks;
};

static inline void dc_copy (char *d, const char *s, int cap) { int i = 0; for (; s && s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = 0; }

static inline void dockconf_defaults (DockConf &c)
{
	static const char *const D[5][2] = { { "Productivity", "tinypad" }, { "Internet", "jet" },
		{ "Graphics", "paint" }, { "Games", "gamelib" }, { "Demos", "widgets" } };
	c.ndrawers = 5;
	for (int i = 0; i < 5; i++) { dc_copy (c.drawer[i].cat, D[i][0], 24); dc_copy (c.drawer[i].app, D[i][1], 32); }
	c.nlaunchers = 2;
	dc_copy (c.launcher[0], "terminal", 32); dc_copy (c.launcher[1], "fileviewer", 32);
	c.ndesks = 4;
	for (int i = 0; i < 4; i++) { c.desk[i][0] = (char) ('1' + i); c.desk[i][1] = 0; }
}

// A value's two parts: "Productivity, tinypad" -> "Productivity" and "tinypad".
static inline void dc_split (const char *v, char *a, int acap, char *b, int bcap)
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

// -> false: no file (the defaults)
static inline bool dockconf_load (DockConf &c)
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

static inline int dc_put (char *o, int p, int cap, const char *s) { while (s && *s && p < cap - 1) o[p++] = *s++; o[p] = 0; return p; }

static inline bool dockconf_save (const DockConf &c)
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

// The dock takes its settings (and the theme) again: it is started again -- a dock that only read
// dock.ini again (DOCK_MSG_RELOAD) showed a new drawer or launcher but did not always act on it
// (a click started nothing, a new group was not seen); a new one has them all. Nothing when no
// dock runs (the user may have taken it out of etc/autostart).
static inline int dock_running (void)
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
static inline void dock_reload (void)
{
	if (!kapi_kill ("dock")) return;
	for (int i = 0; i < 30 && dock_running (); i++) kapi_msleep (50);	// (gone, its service too)
	kapi_launch ("dock");
}

#endif
