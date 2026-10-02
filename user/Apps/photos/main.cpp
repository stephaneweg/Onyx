//
// photos -- Onyx's photo library (docs/photos/README.md): every picture of SD:/Pictures, of each volume's DCIM folder
// and of the folders added by hand, in one place:
//   * the photos by day, the newest first (the day from the camera's EXIF, else the file's name), a ring to select a
//     day, a tick a photo, a heart the favourites; at the right the years, to jump through thousands; the
//     thumbnails' size; the search (a name, a date, a camera, an album, a description);
//   * at the left All photos, Favourites, Recently added; the albums (a list of paths each: a photo is never copied);
//     the folders watched, Add a folder...;
//   * a photo big (viewer.h): the previous and the next, the zoom, the film strip, the details (the camera, the
//     exposure, the folder, its albums, a description);
//   * editing (editor.h): crop, a quarter turn, straighten, the adjustments, the filters; Save or Save as...;
//   * Send by Mail, the Clipboard, the wallpaper, Paint, a PDF contact sheet, the slideshow (share.h).
// The library lives in SD:/etc/photos (library.db, thumbs/, albums/, folders.txt); the folders are walked again at
// each start by a thread of its own (lib.h), the thumbnails made by another (thumbs.h): the window stays live.
// "photos SD:/x.jpg" opens that photo (its folder is shown when it is not in the library).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
//
#include "Apps/photos/share.h"

using namespace photos;

namespace photos {

static void layout_parts ()
{
	int W = g_root->width, H = g_root->height;
	g_tb->resizeTo (W, TB_H); g_tb->place ();
	g_side->top = TB_H; g_side->resizeTo (SIDE_W, H - TB_H);
	g_grid->left = SIDE_W; g_grid->top = TB_H; g_grid->resizeTo (W - SIDE_W, H - TB_H);
	g_view->resizeTo (W, H); g_edit->resizeTo (W, H);
	g_view->disp.free_ (); g_edit->viewDirty = true;
	g_grid->relayout ();
}
static void refresh_all ()
{
	g_grid->relayout ();
	g_tb->invalidate (true); g_side->invalidate (true); g_grid->invalidate (true);
	if (!g_view->hidden) g_view->invalidate (true);
	if (!g_edit->hidden) g_edit->invalidate (true);
}
static void lib_changed ()
{
	counts ();
	if (g_src == SRC_ALBUM && (g_srcArg < 0 || g_srcArg >= g_lib.albums.n)) { g_src = SRC_ALBUMS; }
	if (g_src == SRC_FOLDER && (g_srcArg < 0 || g_srcArg >= g_lib.nroots)) { g_src = SRC_ALL; }
	// the viewer keeps its photo when the list moves
	int viewPi = !g_view->hidden ? g_view->pi : -1;
	build_list ();
	if (viewPi >= 0)
	{
		int at = -1; for (int i = 0; i < g_list.n; i++) if (g_list[i] == viewPi) at = i;
		if (at >= 0) g_view->pos = at;
	}
	refresh_all ();
}
static void show_source (int src, int arg)
{
	if (!g_view->hidden) close_viewer ();
	g_src = src; g_srcArg = arg;
	if (g_selN && src != SRC_ALBUMS) { }
	g_grid->sy = 0;
	lib_changed ();
}
static void open_viewer (int listPos)
{
	if (listPos < 0 || listPos >= g_list.n) return;
	g_view->hidden = false; g_view->bringToFront ();
	g_view->show (listPos);
	g_th.pause = false;
	g_root->invalidate (true);
}
static void close_viewer ()
{
	if (g_view->pos >= 0) g_grid->scroll_to_pos (g_view->pos);
	g_view->hidden = true; g_view->full.free_ (); g_view->disp.free_ ();
	refresh_all (); g_root->invalidate (true);
}
static void open_editor (int pi)
{
	const Pix *have = !g_view->hidden && g_view->pi == pi ? &g_view->full : 0;
	if (!g_edit->open (pi, have)) return;
	g_edit->hidden = false; g_edit->bringToFront ();
	g_edit->invalidate (true); g_root->invalidate (true);
}
static void close_editor ()
{
	g_edit->hidden = true; g_edit->close_ ();
	if (!g_view->hidden) g_view->show (g_view->pos);
	refresh_all (); g_root->invalidate (true);
}
static void act_add_folder ()
{
	char p[300];
	if (!wk_folder_open (p, sizeof p, "SD:/")) return;
	int k = (int) strlen (p); while (k > 1 && p[k - 1] == '/' && p[k - 2] != ':') p[--k] = 0;
	for (int i = 0; i < g_lib.nroots; i++)
		if (ipfx (p, g_lib.roots[i]) && (p[strlen (g_lib.roots[i])] == 0 || p[strlen (g_lib.roots[i])] == '/'))
		{ char q[400]; snprintf (q, sizeof q, "%s is watched already (in %s).", p, g_lib.roots[i]); wk_messagebox ("Photos", q, MB_OK); return; }
	if (g_lib.nroots >= 16) { wk_messagebox ("Photos", "Photos watches 16 folders at most.", MB_OK); return; }
	scpy (g_lib.added[g_lib.nadded++], p, sizeof g_lib.added[0]);
	g_lib.save_added (); g_lib.load_roots ();
	g_lib.start_scan ();
	for (int i = 0; i < g_lib.nroots; i++) if (ieq (g_lib.roots[i], p)) { show_source (SRC_FOLDER, i); return; }
	lib_changed ();
}

static void on_lib_change () { lib_changed (); }
static void on_thumb () { g_grid->invalidate (true); if (!g_view->hidden) g_view->invalidate (true); g_side->invalidate (false); }
static void on_full (FullDone *d) { g_view->full_came (d); }

// ---- the window ---------------------------------------------------------------------------------------------------------------------------
class PhotosRoot : public Root
{
public:
	PhotosRoot (int w, int h) : Root (w, h, "Photos") {}
	void onResized () override { layout_parts (); refresh_all (); }
	void onTick () override
	{
		static char lastQ[200]; static unsigned qT;
		unsigned now = kapi_get_ticks ();
		if (strcmp (lastQ, g_tb->search->text)) { scpy (lastQ, g_tb->search->text, sizeof lastQ); qT = now ? now : 1; }
		if (qT && now - qT > 25) { qT = 0; scpy (g_query, lastQ, sizeof g_query); if (g_src == SRC_ALBUMS && g_query[0]) g_src = SRC_ALL; g_grid->sy = 0; lib_changed (); }
		static bool wasScanning; if (g_lib.scanning != wasScanning) { wasScanning = g_lib.scanning; g_grid->invalidate (true); }
		static unsigned noteT; if (g_note[0] && now - g_noteT > 600 && noteT != g_noteT) { noteT = g_noteT; g_grid->invalidate (true); }
		if (g_lib.dirty && !g_lib.scanning) { static unsigned dT; if (!dT) dT = now; if (now - dT > 300) { dT = 0; g_lib.save (); } }
	}
	bool onKey (long k) override
	{
		if (!g_edit->hidden) return g_edit->key (k);
		if (!g_view->hidden) return g_view->key (k);
		if (k == WK_CTRL ('A')) { for (int i = 0; i < g_list.n; i++) sel_set (g_list[i], true); refresh_all (); return true; }
		if (k == 27 && g_selN) { sel_clear (); refresh_all (); return true; }
		if (k == KEY_DEL && g_selN) { act_delete (); return true; }
		return g_grid->key (k);
	}
	void onDrop (int, int, int type, const char *data, int, unsigned) override
	{
		if (type != DND_FILES) return;
		// a folder dropped: watched; a picture: shown
		char p[300]; int n = 0; while (data[n] && data[n] != '\n' && n < (int) sizeof p - 1) { p[n] = data[n]; n++; } p[n] = 0;
		if (dir_exists (p)) { scpy (g_lib.added[g_lib.nadded < 16 ? g_lib.nadded++ : 15], p, sizeof g_lib.added[0]); g_lib.save_added (); g_lib.load_roots (); g_lib.start_scan (); lib_changed (); }
		else open_path (p);
	}
	static void open_path (const char *p);
};

// "photos SD:/x.jpg": that photo big (in the library, or its folder added for the time being: All photos)
void PhotosRoot::open_path (const char *p)
{
	int k = g_lib.find (p);
	if (k < 0)
	{
		PicInfo pi; void *f = kapi_open (p); unsigned sz = 0; if (f) { sz = kapi_fsize (f); kapi_close (f); }
		if (!f || !pic_info (p, pi)) { wk_messagebox ("Photos", "This picture cannot be read.", MB_OK); return; }
		Photo ph; photo_from (ph, p, sz, pi, now_local ()); ph.alive = true;
		g_lib.ph.push (ph); g_lib.reindex (); k = g_lib.ph.n - 1;	// (not saved unless its folder is watched)
	}
	g_src = SRC_ALL; g_query[0] = 0; lib_changed ();
	for (int i = 0; i < g_list.n; i++) if (g_list[i] == k) { open_viewer (i); return; }
}

static void m_slideshow () { act_slideshow (0); }
static void m_rescan () { g_lib.start_scan (); refresh_all (); }
static void m_find () { g_tb->search->setFocus (); }
static void m_selall () { for (int i = 0; i < g_list.n; i++) sel_set (g_list[i], true); refresh_all (); }
static void m_selnone () { sel_clear (); refresh_all (); }
static void m_fav () { if (!g_view->hidden) act_favourite (g_view->pi); else act_favourite (); }
static void m_album () { if (!g_view->hidden) act_add_to_album (g_view->pi, 300, 60); else act_add_to_album (-1, 300, 60); }
static void m_mail () { if (!g_view->hidden) act_mail (g_view->pi); else act_mail (); }
static void m_delete () { if (!g_view->hidden) act_delete (g_view->pi); else act_delete (); }
static void m_rotate () { if (!g_view->hidden) act_rotate (g_view->pi); else act_rotate (); }
static void m_edit () { if (!g_view->hidden) open_editor (g_view->pi); else { Vec<int> c; if (chosen (c) == 1) open_editor (c[0]); free (c.a); } }
static void m_pdf () { act_pdf (g_src == SRC_ALBUM && !g_selN ? g_srcArg : -1); }
static void m_all () { show_source (SRC_ALL); }
static void m_favs () { show_source (SRC_FAV); }
static void m_recent () { show_source (SRC_RECENT); }
static void m_albums () { show_source (SRC_ALBUMS); }
static void m_bigger () { g_tile = g_tile + 24 > 220 ? 220 : g_tile + 24; refresh_all (); }
static void m_smaller () { g_tile = g_tile - 24 < 72 ? 72 : g_tile - 24; refresh_all (); }

} // namespace photos

int main (void)
{
	ft_wtk_install ("DejaVu Sans", 13);
	faces_open ();

	char args[400]; int na = kapi_get_args (args, sizeof args); args[na > 0 && na < 400 ? na : 0] = 0;
	char *a = args; while (*a == ' ') a++;
	int al = (int) strlen (a); while (al && a[al - 1] == ' ') a[--al] = 0;
	if (a[0] == '"') { a++; char *q = strchr (a, '"'); if (q) *q = 0; }

	g_lib.load ();
	g_lib.onChange = on_lib_change;
	g_th.lib = &g_lib; g_th.onLoaded = on_thumb; g_th.onFull = on_full;

	PhotosRoot root (1000, 640);
	if (root.canvas.px == 0) return 1;
	g_root = &root;
	root.setResizable (true);
	root.setBg (C_BG);
	g_tb = new ToolBar (0, 0, 1000, TB_H); root.addChild (g_tb);
	g_side = new Sidebar (0, TB_H, SIDE_W, 640 - TB_H); root.addChild (g_side);
	g_grid = new Grid (SIDE_W, TB_H, 1000 - SIDE_W, 640 - TB_H); root.addChild (g_grid);
	g_view = new Viewer (0, 0, 1000, 640); root.addChild (g_view);
	g_edit = new Editor (0, 0, 1000, 640); root.addChild (g_edit);

	static Menu menu;
	menu.menu ("File");
	menu.item ("Slideshow", "F5", KEY_F1 + 4, m_slideshow);
	menu.separator ();
	menu.item ("Add a Folder...", "", 0, act_add_folder);
	menu.item ("Look for New Photos", "", 0, m_rescan);
	menu.separator ();
	menu.item ("Send by Mail...", "", 0, m_mail);
	menu.item ("Export as a PDF...", "", 0, m_pdf);
	menu.menu ("Edit");
	menu.item ("Find...", "^F", WK_CTRL ('F'), m_find);
	menu.item ("Select All", "^A", WK_CTRL ('A'), m_selall);
	menu.item ("Select None", "Esc", 0, m_selnone);
	menu.menu ("View");
	menu.item ("All Photos", "", 0, m_all);
	menu.item ("Favourites", "", 0, m_favs);
	menu.item ("Recently Added", "", 0, m_recent);
	menu.item ("Albums", "", 0, m_albums);
	menu.separator ();
	menu.item ("Bigger Thumbnails", "^+", WK_CTRL ('='), m_bigger);
	menu.item ("Smaller Thumbnails", "^-", WK_CTRL ('-'), m_smaller);
	menu.menu ("Photo");
	menu.item ("Edit...", "E", 0, m_edit);
	menu.item ("Rotate to the Left", "R", 0, m_rotate);
	menu.item ("Favourite", "F", 0, m_fav);
	menu.item ("Add to an Album...", "", 0, m_album);
	menu.item ("New Album...", "", 0, act_new_album);
	menu.separator ();
	menu.item ("Move to the Trash", "Del", 0, m_delete);
	menu.publish ();

	counts ();
	build_list ();
	layout_parts ();
	refresh_all ();
	root.fitWorkArea ();
	g_th.start ();
	g_lib.start_scan ();
	if (a[0]) PhotosRoot::open_path (a);

	root.run ();

	g_th.quit ();
	g_lib.quit ();
	if (g_lib.dirty) g_lib.save ();
	return 0;
}
