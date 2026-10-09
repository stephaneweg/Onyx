//
// Apps/photos/app.h -- what Photos' window parts share: the library and its thumbnails, what is shown (all the photos,
// the favourites, the recent ones, an album, a folder; the search), the list in order (the newest day first) and its
// days, the selection, the parts themselves, the helpers they call one another through (main.cpp defines them).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
//
#ifndef _photos_app_h
#define _photos_app_h

#include "fontkit/fonts.h"
#include "Apps/photos/ui.h"
#include "Apps/photos/thumbs.h"
#include "appkit/appkit.h"
#include "systemkit/systemkit.h"

namespace photos {

static const int TB_H = 50, SIDE_W = 204, ST_H = 26;

static Library g_lib;
static Thumbs g_th;
static Root *g_root;
class ToolBar; class Grid; class Viewer; class Editor;
static ToolBar *g_tb; static SidePanel *g_side; static Grid *g_grid; static Viewer *g_view; static Editor *g_edit;

// ---- what is shown -------------------------------------------------------------------------------------------------------------
enum { SRC_ALL, SRC_FAV, SRC_RECENT, SRC_ALBUM, SRC_FOLDER, SRC_ALBUMS };	// (SRC_ALBUMS: the albums' page)
static int g_src = SRC_ALL, g_srcArg;		// the album, the folder (an index of g_lib.roots)
static char g_query[200];
static Vec<int> g_list;				// photo indices, the newest first
struct Day { long long day; int first, n; };
static Vec<Day> g_days;
static Vec<unsigned char> g_selFlag;		// by photo index
static int g_selN;
static char g_note[300]; static unsigned g_noteT;	// a word in the status line (a few seconds)
static int g_tile = 112;			// the thumbnails' size (the tool bar's slider)

static const char *src_title ()
{
	switch (g_src)
	{
	case SRC_FAV: return TR ("Favourites");
	case SRC_RECENT: return TR ("Recently added");
	case SRC_ALBUM: return g_srcArg >= 0 && g_srcArg < g_lib.albums.n ? g_lib.albums[g_srcArg].name : TR ("Album");
	case SRC_FOLDER: return g_srcArg >= 0 && g_srcArg < g_lib.nroots ? base_name (g_lib.roots[g_srcArg]) : TR ("Folder");
	case SRC_ALBUMS: return TR ("Albums");
	}
	return TR ("All photos");
}

// a folder watched as the sidebar names it: "Pictures", "Camera", "SD1: DCIM"
static void root_label (const char *r, char *out, int cap)
{
	const char *c = strchr (r, ':');
	bool sd = ipfx (r, "SD:/");
	const char *bn = base_name (r);
	if (sd || !c) scpy (out, bn, cap);
	else { int k = (int) (c - r) + 1; char v[16]; scpy (v, r, k + 1 < 16 ? k + 1 : 16); snprintf (out, cap, "%s %s", v, bn); }
}

// the photos of an album that the library has (in the album's order -> by date later)
static bool in_album (int a, const Photo &p) { return a >= 0 && a < g_lib.albums.n && g_lib.album_has (a, p.path); }

// does photo p answer the search? (its name, its day, its camera, its description, its albums)
static bool matches (const Photo &p, const char *q)
{
	if (!q[0]) return true;
	// every word must be found somewhere
	char w[200]; scpy (w, q, sizeof w);
	char day[80] = "", tm[16] = "";
	if (p.when ()) { fmt_day (p.when (), day, sizeof day); fmt_time (p.when (), tm, sizeof tm); }
	for (char *s = strtok (w, " "); s; s = strtok (0, " "))
	{
		if (ifind (base_name (p.path), s) || ifind (day, s) || (p.camera && ifind (p.camera, s)) || (p.desc && ifind (p.desc, s))) continue;
		bool al = false;
		for (int a = 0; a < g_lib.albums.n && !al; a++) if (ifind (g_lib.albums[a].name, s) && g_lib.album_has (a, p.path)) al = true;
		if (!al) return false;
	}
	return true;
}

static void sel_clear () { for (int i = 0; i < g_selFlag.n; i++) g_selFlag[i] = 0; g_selN = 0; }
static bool selected (int pi) { return pi >= 0 && pi < g_selFlag.n && g_selFlag[pi]; }
static void sel_set (int pi, bool on) { if (pi < 0 || pi >= g_selFlag.n) return; if (g_selFlag[pi] != (on ? 1 : 0)) { g_selFlag[pi] = on ? 1 : 0; g_selN += on ? 1 : -1; } }
// the photos chosen (the selection; or the one given)
static int chosen (Vec<int> &out, int one = -1)
{
	out.clear ();
	if (g_selN) { for (int i = 0; i < g_list.n; i++) if (selected (g_list[i])) out.push (g_list[i]); }
	else if (one >= 0) out.push (one);
	return out.n;
}

// the list built again (what is shown, in order, its days)
static void build_list ()
{
	static int seenGen = -1;
	if (seenGen != g_lib.gen || g_selFlag.n != g_lib.ph.n)
	{
		bool same = seenGen == g_lib.gen;
		int old = g_selFlag.n;
		while (g_selFlag.n < g_lib.ph.n) g_selFlag.push (0);
		g_selFlag.n = g_lib.ph.n;
		if (!same) sel_clear ();
		else if (old > g_selFlag.n) { g_selN = 0; for (int i = 0; i < g_selFlag.n; i++) g_selN += g_selFlag[i]; }
		seenGen = g_lib.gen;
	}
	g_list.clear ();
	long long recent = 0;
	if (g_src == SRC_RECENT)
	{	// the last 30 days of additions (or the last 200 added)
		long long newest = 0; for (int i = 0; i < g_lib.ph.n; i++) if (g_lib.ph[i].added > newest) newest = g_lib.ph[i].added;
		recent = newest - 30LL * 86400;
	}
	for (int i = 0; i < g_lib.ph.n; i++)
	{
		const Photo &p = g_lib.ph[i];
		if (p.offline) continue;
		switch (g_src)
		{
		case SRC_FAV: if (!p.fav) continue; break;
		case SRC_RECENT: if (p.added < recent) continue; break;
		case SRC_ALBUM: if (!in_album (g_srcArg, p)) continue; break;
		case SRC_FOLDER: if (g_srcArg < 0 || g_srcArg >= g_lib.nroots || !ipfx (p.path, g_lib.roots[g_srcArg])) continue; break;
		}
		if (!matches (p, g_query)) continue;
		g_list.push (i);
	}
	// the newest first (recently added: by when added); a stable merge sort
	int n = g_list.n;
	int *tmp = (int *) malloc (sizeof (int) * (n ? n : 1));
	bool byAdded = g_src == SRC_RECENT;
	auto key = [&] (int pi) -> long long { const Photo &p = g_lib.ph[pi]; return byAdded ? p.added : p.when (); };
	for (int w = 1; w < n; w *= 2)
		for (int lo = 0; lo < n; lo += 2 * w)
		{
			int mid = lo + w < n ? lo + w : n, hi = lo + 2 * w < n ? lo + 2 * w : n, a = lo, b = mid, k = lo;
			while (a < mid && b < hi) { long long ka = key (g_list[a]), kb = key (g_list[b]); if (ka > kb || (ka == kb && icmp (g_lib.ph[g_list[a]].path, g_lib.ph[g_list[b]].path) >= 0)) tmp[k++] = g_list[a++]; else tmp[k++] = g_list[b++]; }
			while (a < mid) tmp[k++] = g_list[a++];
			while (b < hi) tmp[k++] = g_list[b++];
			for (int i = lo; i < hi; i++) g_list[i] = tmp[i];
		}
	free (tmp);
	// the days
	g_days.clear ();
	for (int i = 0; i < g_list.n; i++)
	{
		long long t = key (g_list[i]); long long d = t ? day_of (t) : -999999;
		if (!g_days.n || g_days[g_days.n - 1].day != d) { Day x = { d, i, 0 }; g_days.push (x); }
		g_days[g_days.n - 1].n++;
	}
}

// main.cpp
static void refresh_all ();
static void layout_parts ();
static void show_source (int src, int arg = 0);
static void open_viewer (int listPos);
static void close_viewer ();
static void open_editor (int pi);
static void close_editor ();
static void act_favourite (int one = -1);
static void act_delete (int one = -1);
static void act_add_to_album (int one = -1, int x = -1, int y = -1);
static void act_share_menu (int one, int x, int y);
static void act_mail (int one = -1);
static void act_wallpaper (int pi);
static void act_copy (int pi);
static void act_pdf (int album = -1);
static void act_slideshow (int fromListPos = 0);
static void act_new_album ();
static void act_add_folder ();
static void act_rotate (int one = -1);
static void photo_menu (int pi, int x, int y);
static void status_note (const char *s);
static void lib_changed ();

} // namespace photos

#endif
