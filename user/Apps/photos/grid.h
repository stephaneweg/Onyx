//
// Apps/photos/grid.h -- the library's window parts: the tool bar (Slideshow, what can be done to the selection, the
// thumbnails' size, the search), the left column (All photos, Favourites, Recently added; the albums; the folders
// watched, Add a folder...), the photos by day (a ring selects a whole day; a tick a photo; a heart the favourites;
// at the right the years, to jump through thousands), and the albums' page (their covers, names, counts).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
//
#ifndef _photos_grid_h
#define _photos_grid_h

#include "Apps/photos/app.h"

namespace photos {

// the counts the left column shows (made again when the library changes)
static int g_nAll, g_nFav, g_nRecent; static Vec<int> g_nAlbum; static int g_nRoot[16];
static void counts ()
{
	g_nAll = g_nFav = g_nRecent = 0;
	long long newest = 0; for (int i = 0; i < g_lib.ph.n; i++) if (g_lib.ph[i].added > newest) newest = g_lib.ph[i].added;
	for (int r = 0; r < 16; r++) g_nRoot[r] = 0;
	for (int i = 0; i < g_lib.ph.n; i++)
	{
		const Photo &p = g_lib.ph[i]; if (p.offline) continue;
		g_nAll++; if (p.fav) g_nFav++; if (p.added >= newest - 30LL * 86400) g_nRecent++;
		for (int r = 0; r < g_lib.nroots; r++) if (ipfx (p.path, g_lib.roots[r])) g_nRoot[r]++;
	}
	g_nAlbum.clear ();
	for (int a = 0; a < g_lib.albums.n; a++) { int c = 0; for (int k = 0; k < g_lib.albums[a].paths.n; k++) { int i = g_lib.find (g_lib.albums[a].paths[k]); if (i >= 0 && !g_lib.ph[i].offline) c++; } g_nAlbum.push (c); }
}
// the album's cover: its newest photo the library has
static int album_cover (int a)
{
	int best = -1;
	for (int k = 0; k < g_lib.albums[a].paths.n; k++)
	{
		int i = g_lib.find (g_lib.albums[a].paths[k]); if (i < 0 || g_lib.ph[i].offline) continue;
		if (best < 0 || g_lib.ph[i].when () > g_lib.ph[best].when ()) best = i;
	}
	return best;
}

// ---- the tool bar ------------------------------------------------------------------------------------------------------------
class SearchBox : public HintBox
{
public:
	SearchBox (int l, int t, int w, int h) : HintBox (l, t, w, h, "Search: a name, a date...") { maxLen = 190; }
	bool onKey (long k) override { if (k == 27) { setText (""); return true; } return HintBox::onKey (k); }
};
class ToolBar : public Widget
{
public:
	SearchBox *search; HitList hits; int hot; bool dragSize;
	enum { T_SHOW = 1, T_FAV, T_ALBUM, T_SHARE, T_TRASH, T_CLEAR, T_SIZE };
	int zx;					// the size slider's track
	ToolBar (int l, int t, int w, int h) : Widget (l, t, w, h), hot (0), dragSize (false), zx (0)
	{
		search = new SearchBox (w - 250, 10, 236, 30); addChild (search);
	}
	void place () { int sw = width > 900 ? 236 : 170; search->left = width - sw - 14; search->resizeTo (sw, 30); }
	void onDraw () override
	{
		canvas.clear (C_BG); hits.clear ();
		canvas.fillRect (0, height - 1, width, 1, wk_mix (C_BG, C_TEXT, 40));
		int x = 12;
		{	// Slideshow: the accent pill
			const char *l = "Slideshow"; int w = tw (l, F_UI, 1) + 46;
			bool on = g_list.n > 0 || g_src == SRC_ALBUMS;
			fill_round (canvas, x, 8, w, 34, 6, hot == T_SHOW && on ? wk_mix (C_ACCENT, 0xFFFFFF, 30) : on ? C_ACCENT : wk_mix (C_BG, C_ACCENT, 120));
			icon (canvas, I_PLAY, x + 12, 16, 18, C_SEL_TEXT);
			text_v (canvas, x + 36, 8, 34, l, C_SEL_TEXT, F_UI, 1);
			hits.add (x, 8, w, 34, T_SHOW); x += w + 12;
		}
		canvas.fillRect (x, 12, 1, 26, wk_mix (C_BG, C_TEXT, 50)); x += 12;
		if (g_selN)
		{
			char s[40]; snprintf (s, sizeof s, g_selN == 1 ? "1 selected" : "%d selected", g_selN);
			text_v (canvas, x, 8, 34, s, C_ACCENT, F_UI, 1); x += tw (s, F_UI, 1) + 10;
			// all favourites already? the full heart
			bool allFav = true; for (int i = 0; i < g_list.n; i++) if (selected (g_list[i]) && !g_lib.ph[g_list[i]].fav) { allFav = false; break; }
			struct B { int k, ic; const char *tip; };
			const B BS[] = { { T_FAV, allFav ? I_HEART : I_HEART_O, 0 }, { T_ALBUM, I_ALBUM, 0 }, { T_SHARE, I_SHARE, 0 }, { T_TRASH, I_TRASH, 0 }, { T_CLEAR, I_CLOSE, 0 } };
			for (unsigned i = 0; i < sizeof BS / sizeof BS[0]; i++)
			{
				if (hot == BS[i].k) fill_round (canvas, x, 8, 34, 34, 6, wk_mix (C_BG, C_TEXT, 25));
				icon (canvas, BS[i].ic, x + 8, 16, 18, BS[i].k == T_FAV && allFav ? RED : C_TEXT);
				hits.add (x, 8, 34, 34, BS[i].k); x += 38;
			}
		}
		// the thumbnails' size
		zx = search->left - 150;
		if (zx > x + 10 && g_src != SRC_ALBUMS)
		{
			unsigned dim = wk_mix (C_BG, C_TEXT, 150);
			icon (canvas, I_SMALLPIC, zx, 18, 14, dim);
			int tx = zx + 22, tl = 90;
			canvas.fillRect (tx, 24, tl, 3, wk_mix (C_BG, C_TEXT, 50));
			int k = tx + (g_tile - 72) * tl / (220 - 72);
			canvas.fillRect (tx, 24, k - tx, 3, C_ACCENT);
			disc (canvas, k, 25, 7, C_FIELD); ring (canvas, k, 25, 7, wk_mix (C_BG, C_TEXT, 110), 1);
			icon (canvas, I_BIGPIC, tx + tl + 8, 14, 20, dim);
			hits.add (tx - 8, 10, tl + 16, 30, T_SIZE);
		}
		icon (canvas, I_SEARCH, search->left + search->width - 26, 17, 16, wk_mix (C_FIELD, C_FIELD_TEXT, 120));
	}
	void size_at (int mx)
	{
		int tx = zx + 22, tl = 90;
		int v = 72 + (mx - tx) * (220 - 72) / tl; if (v < 72) v = 72; if (v > 220) v = 220;
		if (v != g_tile) { g_tile = v; refresh_all (); }
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		if (mx < 0 && !dragSize) { if (hot) { hot = 0; invalidate (true); } return false; }
		static bool was; bool down = bl && !was; was = bl;
		if (dragSize) { if (bl) size_at (mx); else { dragSize = false; catchOutside = false; } return true; }
		const Hit *h = hits.at (mx, my);
		int nh = h ? h->kind : 0;
		if (nh != hot) { hot = nh; invalidate (true); }
		if (!down || !h) return h != 0;
		switch (h->kind)
		{
		case T_SHOW: act_slideshow (0); break;
		case T_FAV: act_favourite (); break;
		case T_ALBUM: act_add_to_album (-1, left + h->x, top + h->y + 36); break;
		case T_SHARE: act_share_menu (-1, left + h->x, top + h->y + 36); break;
		case T_TRASH: act_delete (); break;
		case T_CLEAR: sel_clear (); refresh_all (); break;
		case T_SIZE: dragSize = true; catchOutside = true; size_at (mx); break;
		}
		return true;
	}
};

// ---- the left column ---------------------------------------------------------------------------------------------------------------
class Sidebar : public Widget
{
public:
	HitList hits; int sy, hot, contentH;
	enum { S_ALL = 1, S_FAV, S_RECENT, S_ALBUMS, S_ALBUM, S_NEWALBUM, S_FOLDER, S_ADDFOLDER };
	Sidebar (int l, int t, int w, int h) : Widget (l, t, w, h), sy (0), hot (-1), contentH (0) {}
	void onDraw () override
	{
		unsigned bg = col_side ();
		canvas.clear (bg); hits.clear ();
		canvas.fillRect (width - 1, 0, 1, height, wk_mix (C_BG, C_TEXT, 40));
		int y = 12 - sy;
		auto head = [&] (const char *t, int kind) { text (canvas, 18, y + 6, t, wk_mix (bg, C_TEXT, 130), F_TINY, 1); if (kind) hits.add (0, y, width, 22, kind); y += 24; };
		auto item = [&] (const char *label, int ic, unsigned icol, int count, bool on, int kind, int arg) {
			int idx = hits.n;
			if (on) fill_round (canvas, 8, y, width - 16, 28, 6, C_ACCENT);
			else if (hot == idx) fill_round (canvas, 8, y, width - 16, 28, 6, wk_mix (bg, C_TEXT, 22));
			icon (canvas, ic, 18, y + 6, 16, on ? C_SEL_TEXT : icol);
			char n[16] = ""; if (count >= 0) snprintf (n, sizeof n, "%d", count);
			int nw = n[0] ? tw (n, F_SMALL) + 8 : 0;
			text_v (canvas, 44, y, 28, label, on ? C_SEL_TEXT : C_TEXT, F_UI, on ? 1 : 0, width - 44 - 18 - nw);
			if (n[0]) text_r (canvas, width - 18, y, 28, n, on ? C_SEL_TEXT : wk_mix (bg, C_TEXT, 140), F_SMALL);
			hits.add (0, y, width, 28, kind, arg); y += 30;
		};
		auto link = [&] (const char *label, int kind) {
			icon (canvas, I_PLUS, 18, y + 6, 16, C_ACCENT);
			text_v (canvas, 44, y, 28, label, hits.n == hot ? wk_mix (C_ACCENT, C_TEXT, 90) : C_ACCENT);
			hits.add (0, y, width, 28, kind); y += 30;
		};
		unsigned dim = wk_mix (bg, C_TEXT, 150);
		head ("LIBRARY", 0);
		item ("All photos", I_PHOTOS, dim, g_nAll, g_src == SRC_ALL, S_ALL, 0);
		item ("Favourites", I_HEART, RED, g_nFav, g_src == SRC_FAV, S_FAV, 0);
		item ("Recently added", I_CLOCK, dim, g_nRecent, g_src == SRC_RECENT, S_RECENT, 0);
		y += 8; head ("ALBUMS", S_ALBUMS);
		for (int a = 0; a < g_lib.albums.n; a++) item (g_lib.albums[a].name, I_ALBUM, dim, a < g_nAlbum.n ? g_nAlbum[a] : 0, g_src == SRC_ALBUM && g_srcArg == a, S_ALBUM, a);
		link ("New album...", S_NEWALBUM);
		y += 8; head ("FOLDERS", 0);
		for (int r = 0; r < g_lib.nroots; r++)
		{
			char l[200]; root_label (g_lib.roots[r], l, sizeof l);
			item (l, I_FOLDER, 0xD2A550, g_nRoot[r], g_src == SRC_FOLDER && g_srcArg == r, S_FOLDER, r);
		}
		link ("Add a folder...", S_ADDFOLDER);
		contentH = y + sy + 12;
	}
	bool onMouse (int mx, int my, int bl, int br, int, int wheel) override
	{
		if (mx < 0) { if (hot >= 0) { hot = -1; invalidate (true); } return false; }
		if (wheel) { sy -= wheel * 40; int mxs = contentH - height; if (sy > mxs) sy = mxs; if (sy < 0) sy = 0; invalidate (true); return true; }
		const Hit *h = hits.at (mx, my);
		int nh = h ? (int) (h - hits.h) : -1;
		if (nh != hot) { hot = nh; invalidate (true); }
		static bool wasL, wasR; bool down = bl && !wasL, rdown = br && !wasR; wasL = bl; wasR = br;
		if (!h || (!down && !rdown)) return h != 0;
		if (rdown)
		{
			int gx = left + mx, gy = top + my;
			if (h->kind == S_ALBUM)
			{
				int a = h->a;
				PopupMenu m (gx, gy);
				m.add ("Open", 1); m.add ("Slideshow", 2); m.separator ();
				m.add ("Send by Mail...", 3); m.add ("Export as a PDF...", 4); m.separator ();
				m.add ("Rename...", 5); m.add ("Delete the album...", 6);
				int r = m.run ();
				album_action (a, r);
			}
			else if (h->kind == S_FOLDER)
			{
				int rr = h->a; bool byHand = false;
				for (int i = 0; i < g_lib.nadded; i++) if (ieq (g_lib.added[i], g_lib.roots[rr])) byHand = true;
				PopupMenu m (gx, gy);
				m.add ("Open", 1); m.add ("Show in the File Viewer", 2);
				m.separator (); m.add ("Remove from Photos", 3, byHand, byHand ? 0 : "(watched always)");
				int r = m.run ();
				if (r == 1) show_source (SRC_FOLDER, rr);
				else if (r == 2) kapi_exec ("SD:apps/fileviewer.app/main", g_lib.roots[rr]);
				else if (r == 3)
				{
					char q[300]; snprintf (q, sizeof q, "Stop watching %s? Its photos stay on the card; they leave the library and its albums keep their place.", g_lib.roots[rr]);
					if (wk_messagebox ("Photos", q, MB_YESNO) == 1) { char p[200]; scpy (p, g_lib.roots[rr], sizeof p); g_lib.remove_added (p); g_lib.save (); show_source (SRC_ALL); lib_changed (); }
				}
			}
			return true;
		}
		switch (h->kind)
		{
		case S_ALL: show_source (SRC_ALL); break;
		case S_FAV: show_source (SRC_FAV); break;
		case S_RECENT: show_source (SRC_RECENT); break;
		case S_ALBUMS: show_source (SRC_ALBUMS); break;
		case S_ALBUM: show_source (SRC_ALBUM, h->a); break;
		case S_NEWALBUM: act_new_album (); break;
		case S_FOLDER: show_source (SRC_FOLDER, h->a); break;
		case S_ADDFOLDER: act_add_folder (); break;
		}
		return true;
	}
	static void album_action (int a, int r);
};

// ---- a name asked for (a new album, a new name) --------------------------------------------------------------------------------------
class NameBox : public Modal
{
public:
	Textbox *t; const char *m_title, *m_label;
	static const int W = 420, H = 170;
	NameBox (const char *title, const char *label, const char *init) : Modal (W, H), m_title (title), m_label (label)
	{
		t = new Textbox (20, titleH () + 40, W - 40, 30, init); t->maxLen = 100; addChild (t);
		Button *b = new Button (W - 240, H - 48, 100, 32, "Cancel", [] (Widget &w) { ((Modal *) w.parent)->close (0); }); addChild (b);
		b = new Button (W - 130, H - 48, 110, 32, "OK", [] (Widget &w) { ((Modal *) w.parent)->close (1); }); addChild (b);
		t->setFocus ();
	}
	void onDraw () override { drawBox (m_title); text (canvas, 20, titleH () + 14, m_label, C_TEXT); }
	bool onKey (long k) override { if (k == '\n' || k == '\r') { close (1); return true; } if (k == 27) { close (0); return true; } return Modal::onKey (k); }
	bool ask (char *out, int cap)
	{
		if (run () != 1) return false;
		const char *s = t->text; while (*s == ' ') s++;
		scpy (out, s, cap); int k = (int) strlen (out); while (k && out[k - 1] == ' ') out[--k] = 0;
		return out[0] != 0;
	}
};

void Sidebar::album_action (int a, int r)
{
	if (a < 0 || a >= g_lib.albums.n) return;
	switch (r)
	{
	case 1: show_source (SRC_ALBUM, a); break;
	case 2: show_source (SRC_ALBUM, a); act_slideshow (0); break;
	case 3:
	{
		show_source (SRC_ALBUM, a);
		sel_clear (); for (int i = 0; i < g_list.n; i++) sel_set (g_list[i], true);
		act_mail (); sel_clear (); refresh_all (); break;
	}
	case 4: act_pdf (a); break;
	case 5:
	{
		NameBox nb ("Rename the album", "Its new name:", g_lib.albums[a].name); char n[120];
		if (nb.ask (n, sizeof n) && !g_lib.album_rename (a, n)) wk_messagebox ("Photos", "That name cannot be used (one has it already, or it has a / or a :).", MB_OK);
		if (g_src == SRC_ALBUM) { int k = g_lib.album_find (n); if (k >= 0) g_srcArg = k; }
		lib_changed (); break;
	}
	case 6:
	{
		char q[300]; snprintf (q, sizeof q, "Delete the album \"%s\"? Its photos stay in the library.", g_lib.albums[a].name);
		if (wk_messagebox ("Photos", q, MB_YESNO) == 1) { g_lib.album_delete (a); if (g_src == SRC_ALBUM) g_src = SRC_ALBUMS; lib_changed (); }
		break;
	}
	}
}

// ---- the photos by day, the albums' page ---------------------------------------------------------------------------------------------------
class Grid : public Widget
{
public:
	int sy;					// scrolled
	Vec<int> dayY; int totalH;		// each day's top, the whole height
	int cols, tile, gap, x0;
	int hoverPos;				// the list position under the pointer (-1)
	int pressPos, pressDay; bool tlDrag;
	int anchor;				// shift+click: from here
	HitList hits;				// (the albums' page; the days' rings)
	enum { G_DAY = 1, G_ALBUM, G_NEWALBUM, G_ADDFOLDER, G_ALL };
	static const int TL_W = 46, HEAD_H = 36, DAY_GAP = 18;

	Grid (int l, int t, int w, int h) : Widget (l, t, w, h), sy (0), totalH (0), cols (1), tile (112), gap (6), x0 (18), hoverPos (-1), pressPos (-1), pressDay (-1), tlDrag (false), anchor (-1) {}
	unsigned bgColor () override { return C_FIELD; }
	int viewH () const { return height - ST_H; }
	void relayout ()
	{
		tile = g_tile; gap = tile > 150 ? 8 : 6;
		int avail = width - x0 - TL_W - 8;
		cols = avail / (tile + gap); if (cols < 1) cols = 1;
		// the tiles stretch a little to fill the row
		tile = (avail - (cols - 1) * gap) / cols;
		if (tile > g_tile + 24) { cols++; tile = (avail - (cols - 1) * gap) / cols; }
		dayY.clear ();
		int y = 14;
		for (int d = 0; d < g_days.n; d++) { dayY.push (y); y += HEAD_H + ((g_days[d].n + cols - 1) / cols) * (tile + gap) - gap + DAY_GAP; }
		totalH = y;
		clamp ();
	}
	void clamp () { int m = totalH - viewH (); if (sy > m) sy = m; if (sy < 0) sy = 0; }
	// the list position of the tile at (mx, my), -1 none
	int tile_at (int mx, int my, bool *ring = 0) const
	{
		int cy = my + sy;
		int lo = 0, hi = g_days.n - 1, d = -1;
		while (lo <= hi) { int m = (lo + hi) / 2; if (dayY[m] <= cy) { d = m; lo = m + 1; } else hi = m - 1; }
		if (d < 0) return -1;
		int ry = cy - dayY[d] - HEAD_H; if (ry < 0) return -1;
		int row = ry / (tile + gap), cx = mx - x0; if (cx < 0) return -1;
		int col = cx / (tile + gap); if (col >= cols) return -1;
		if (cx - col * (tile + gap) >= tile || ry - row * (tile + gap) >= tile) return -1;
		int k = row * cols + col; if (k >= g_days[d].n) return -1;
		if (ring) *ring = cx - col * (tile + gap) < 30 && ry - row * (tile + gap) < 30;
		return g_days[d].first + k;
	}
	// the tile of a list position: its place on the canvas
	bool tile_rect (int pos, int *x, int *y) const
	{
		for (int d = 0; d < g_days.n; d++)
			if (pos >= g_days[d].first && pos < g_days[d].first + g_days[d].n)
			{
				int k = pos - g_days[d].first;
				*x = x0 + (k % cols) * (tile + gap); *y = dayY[d] + HEAD_H + (k / cols) * (tile + gap) - sy; return true;
			}
		return false;
	}
	void scroll_to_pos (int pos)
	{
		int x, y; if (!tile_rect (pos, &x, &y)) return;
		if (y < 0) sy += y - HEAD_H; else if (y + tile > viewH ()) sy += y + tile - viewH () + 10;
		clamp ();
	}

	void onDraw () override
	{
		unsigned bg = C_FIELD;
		canvas.clear (bg); hits.clear ();
		if (g_src == SRC_ALBUMS) { draw_albums (); draw_status (); return; }
		int vh = viewH ();
		if (!g_list.n) { draw_empty (); draw_status (); return; }
		bool rings = g_selN > 0;
		for (int d = 0; d < g_days.n; d++)
		{
			int y = dayY[d] - sy;
			int rows = (g_days[d].n + cols - 1) / cols;
			if (y + HEAD_H + rows * (tile + gap) < 0) continue;
			if (y > vh) break;
			// the day: its name, its count, its ring
			char t[80];
			if (g_days[d].day == -999999) scpy (t, "No date", sizeof t); else fmt_day (g_days[d].day * 86400, t, sizeof t);
			text (canvas, x0, y + 6, t, C_FIELD_TEXT, F_H2, 1);
			char n[40]; snprintf (n, sizeof n, g_days[d].n == 1 ? "1 photo" : "%d photos", g_days[d].n);
			text (canvas, x0 + tw (t, F_H2, 1) + 12, y + 10, n, col_dim (), F_UI);
			int all = 0; for (int k = 0; k < g_days[d].n; k++) if (selected (g_list[g_days[d].first + k])) all++;
			int rx = x0 + cols * (tile + gap) - gap - 24;
			check_mark (canvas, rx, y + 5, all == g_days[d].n, col_dim ());
			hits.add (rx - 4, y + 2, 30, 30, G_DAY, d);
			// its photos
			for (int k = 0; k < g_days[d].n; k++)
			{
				int tx = x0 + (k % cols) * (tile + gap), ty = y + HEAD_H + (k / cols) * (tile + gap);
				if (ty + tile < 0) continue;
				if (ty > vh) break;
				int pos = g_days[d].first + k, pi = g_list[pos];
				bool sel = selected (pi);
				if (sel)
				{	// a selected photo: smaller in an accent frame
					fill_round (canvas, tx, ty, tile, tile, 6, wk_mix (bg, C_ACCENT, 70));
					g_th.draw (canvas, pi, tx + 8, ty + 8, tile - 16, tile - 16, 4, wk_mix (bg, C_ACCENT, 70));
				}
				else g_th.draw (canvas, pi, tx, ty, tile, tile, 4, bg);
				if (pos == hoverPos && !sel) { for (int j = 0; j < 30; j++) wk_rbox (canvas, tx, ty + j, tile, 1, 0, 0x000000, 0x000000, (30 - j) * 3); }
				if (sel) check_mark (canvas, tx + 4, ty + 4, true, 0xFFFFFF);
				else if (rings || pos == hoverPos) check_mark (canvas, tx + 5, ty + 5, false, 0xFFFFFF);
				if (g_lib.ph[pi].fav) icon (canvas, I_HEART, tx + tile - (sel ? 30 : 24), ty + tile - (sel ? 30 : 24), 17, 0xFFFFFF);
			}
		}
		draw_timeline ();
		draw_status ();
	}
	// the years at the right: where each begins, where the view is (dragged: a jump)
	void draw_timeline ()
	{
		int x = width - TL_W, vh = viewH ();
		int top = 10, th = vh - 20;
		if (totalH <= vh) return;
		canvas.fillRect (x + TL_W - 14, top, 3, th, col_line ());
		// the years: where each begins
		int lastY = -100, lastYear = 0;
		for (int d = 0; d < g_days.n; d++)
		{
			if (g_days[d].day == -999999) continue;
			int yr = year_of (g_days[d].day * 86400);
			if (yr == lastYear) continue;
			lastYear = yr;
			int ly = top + (int) ((long long) dayY[d] * th / totalH);
			if (ly - lastY < 20) continue;
			lastY = ly;
			char s[8]; snprintf (s, sizeof s, "%d", yr);
			text_r (canvas, x + TL_W - 20, ly - 2, 16, s, col_dim (), F_SMALL);
		}
		// where the view is
		int ty = top + (int) ((long long) sy * th / totalH), tl = (int) ((long long) vh * th / totalH); if (tl < 24) tl = 24;
		if (ty + tl > top + th) ty = top + th - tl;
		fill_round (canvas, x + TL_W - 16, ty, 7, tl, 3, C_ACCENT);
	}
	void draw_status ()
	{
		int y = height - ST_H;
		canvas.fillRect (0, y, width, ST_H, col_side ());
		canvas.fillRect (0, y, width, 1, col_line ());
		char s[200]; int k = 0;
		if (g_src == SRC_ALBUMS) k = snprintf (s, sizeof s, g_lib.albums.n == 1 ? "1 album" : "%d albums", g_lib.albums.n);
		else
		{
			k = snprintf (s, sizeof s, g_list.n == 1 ? "1 photo" : "%d photos", g_list.n);
			if (g_selN)
			{
				unsigned long long b = 0; for (int i = 0; i < g_list.n; i++) if (selected (g_list[i])) b += g_lib.ph[g_list[i]].size;
				char sz[40]; fmt_size (b, sz, sizeof sz);
				k += snprintf (s + k, sizeof s - k, " \xC2\xB7 %d selected (%s)", g_selN, sz);
			}
		}
		if (g_lib.scanning) k += snprintf (s + k, sizeof s - k, g_lib.scanCount ? "   \xC2\xB7   Looking for photos... %d new" : "   \xC2\xB7   Looking for photos...", g_lib.scanCount);
		text_v (canvas, 14, y, ST_H, s, col_dim (), F_SMALL);
		if (g_note[0] && kapi_get_ticks () - g_noteT < 600) text_r (canvas, width - 14, y, ST_H, g_note, C_ACCENT, F_SMALL);
	}
	void draw_empty ()
	{
		int vh = viewH (), cx = width / 2;
		icon (canvas, I_PHOTOS, cx - 32, vh / 2 - 110, 64, col_faint ());
		const char *t = "No photos here yet", *s = 0;
		if (g_query[0]) { t = "Nothing found"; s = "No photo has that name, date, camera, album or description."; }
		else if (g_lib.scanning) { t = "Looking for photos..."; s = "Pictures, the cameras' DCIM folders, the folders you add."; }
		else if (g_src == SRC_FAV) s = "A photo's heart puts it here.";
		else if (g_src == SRC_ALBUM) s = "Select photos in All photos, then the album button adds them here.";
		else if (g_src == SRC_RECENT) s = "The photos put on the card lately show here.";
		else s = "Photos shows the pictures of SD:/Pictures, of each volume's DCIM folder and of the folders you add.";
		text_c (canvas, 0, vh / 2 - 30, width, 30, t, C_FIELD_TEXT, F_H2, 1);
		if (s) text_c (canvas, 0, vh / 2 + 4, width, 24, s, col_dim ());
		if (!g_query[0] && (g_src == SRC_ALL || g_src == SRC_FOLDER) && !g_lib.scanning)
		{
			const char *l = "Add a folder..."; int w = tw (l, F_UI, 1) + 36;
			fill_round (canvas, cx - w / 2, vh / 2 + 44, w, 34, 6, C_ACCENT);
			text_c (canvas, cx - w / 2, vh / 2 + 44, w, 34, l, C_SEL_TEXT, F_UI, 1);
			hits.add (cx - w / 2, vh / 2 + 44, w, 34, G_ADDFOLDER);
		}
	}
	void draw_albums ()
	{
		unsigned bg = C_FIELD;
		text (canvas, 20, 14 - sy, "Albums", C_FIELD_TEXT, F_H1, 1);
		const char *nl = "+  New album"; int nw = tw (nl, F_UI, 1) + 30;
		fill_round (canvas, width - nw - 24, 16 - sy, nw, 32, 6, C_ACCENT);
		text_c (canvas, width - nw - 24, 16 - sy, nw, 32, nl, C_SEL_TEXT, F_UI, 1);
		hits.add (width - nw - 24, 16 - sy, nw, 32, G_NEWALBUM);
		int cw = 176, ch = 132, gx = 18, n = (width - 2 * 20 + gx) / (cw + gx); if (n < 1) n = 1;
		cw = (width - 40 - (n - 1) * gx) / n; ch = cw * 3 / 4;
		int vh = viewH ();
		if (!g_lib.albums.n)
		{
			text_c (canvas, 0, vh / 2 - 20, width, 30, "No albums yet", C_FIELD_TEXT, F_H2, 1);
			text_c (canvas, 0, vh / 2 + 12, width, 24, "Select photos, then the album button: a new album, or one made before.", col_dim ());
		}
		for (int a = 0; a < g_lib.albums.n; a++)
		{
			int x = 20 + (a % n) * (cw + gx), y = 74 + (a / n) * (ch + 62) - sy;
			if (y > vh || y + ch + 50 < 0) continue;
			unsigned l1 = wk_mix (bg, C_FIELD_TEXT, 40), l2 = wk_mix (bg, C_FIELD_TEXT, 22);
			fill_round (canvas, x + 10, y - 9, cw - 20, ch, 8, l1); fill_round (canvas, x + 5, y - 5, cw - 10, ch, 8, l2);
			int c = album_cover (a);
			if (c >= 0) g_th.draw (canvas, c, x, y, cw, ch, 8, bg);
			else { fill_round (canvas, x, y, cw, ch, 8, wk_mix (bg, C_FIELD_TEXT, 30)); icon (canvas, I_ALBUM, x + cw / 2 - 20, y + ch / 2 - 20, 40, col_faint ()); }
			text (canvas, x + 2, y + ch + 8, g_lib.albums[a].name, C_FIELD_TEXT, F_UI, 1, cw - 4);
			char s[40]; int k = a < g_nAlbum.n ? g_nAlbum[a] : 0; snprintf (s, sizeof s, k == 1 ? "1 photo" : "%d photos", k);
			text (canvas, x + 2, y + ch + 26, s, col_dim (), F_SMALL);
			hits.add (x, y, cw, ch + 44, G_ALBUM, a);
		}
		totalH = 74 + ((g_lib.albums.n + n - 1) / n) * (ch + 62) + 20;
	}

	bool onMouse (int mx, int my, int bl, int br, int, int wheel) override
	{
		if (mx < 0) { if (hoverPos >= 0) { hoverPos = -1; invalidate (true); } if (!tlDrag) return false; }
		if (wheel) { sy -= wheel * (tile + gap) / 2 * 3 / 2; clamp (); invalidate (true); return true; }
		static bool wasL, wasR; bool down = bl && !wasL, up = !bl && wasL, rdown = br && !wasR; wasL = bl; wasR = br;
		// the years' strip: dragged
		if (tlDrag) { if (!bl) { tlDrag = false; catchOutside = false; } else tl_jump (my); return true; }
		if (g_src != SRC_ALBUMS && down && mx >= width - TL_W && my < viewH () && totalH > viewH ()) { tlDrag = true; catchOutside = true; tl_jump (my); return true; }
		if (my >= viewH ()) return true;
		bool ring = false;
		int pos = g_src == SRC_ALBUMS ? -1 : tile_at (mx, my, &ring);
		if (pos != hoverPos) { hoverPos = pos; invalidate (true); }
		const Hit *h = hits.at (mx, my);
		if (rdown)
		{
			if (h && h->kind == G_ALBUM)
			{
				PopupMenu m (left + mx, top + my);
				m.add ("Open", 1); m.add ("Slideshow", 2); m.separator ();
				m.add ("Send by Mail...", 3); m.add ("Export as a PDF...", 4); m.separator ();
				m.add ("Rename...", 5); m.add ("Delete the album...", 6);
				Sidebar::album_action (h->a, m.run ());
			}
			else if (pos >= 0)
			{
				int pi = g_list[pos];
				if (g_selN && !selected (pi)) { sel_clear (); refresh_all (); }
				photo_menu (pi, left + mx, top + my);
			}
			return true;
		}
		if (down) { pressPos = pos; pressDay = h && h->kind == G_DAY ? h->a : -1; return true; }
		if (!up) return true;
		if (h && h->kind == G_ALBUM) { show_source (SRC_ALBUM, h->a); return true; }
		if (h && h->kind == G_NEWALBUM) { act_new_album (); return true; }
		if (h && h->kind == G_ADDFOLDER) { act_add_folder (); return true; }
		if (h && h->kind == G_DAY && h->a == pressDay)
		{	// the whole day: all on, or all off when they all were
			const Day &d = g_days[h->a]; int on = 0; for (int k = 0; k < d.n; k++) on += selected (g_list[d.first + k]);
			for (int k = 0; k < d.n; k++) sel_set (g_list[d.first + k], on != d.n);
			refresh_all (); return true;
		}
		if (pos < 0 || pos != pressPos) return true;
		unsigned mods = kapi_get_modifiers ();
		int pi = g_list[pos];
		if ((mods & MOD_SHIFT) && anchor >= 0 && anchor < g_list.n)
		{
			int a = anchor < pos ? anchor : pos, b = anchor < pos ? pos : anchor;
			for (int k = a; k <= b; k++) sel_set (g_list[k], true);
			refresh_all (); return true;
		}
		if (ring || g_selN || (mods & MOD_CTRL)) { sel_set (pi, !selected (pi)); anchor = pos; refresh_all (); return true; }
		anchor = pos;
		open_viewer (pos);
		return true;
	}
	void tl_jump (int my)
	{
		int top = 10, th = viewH () - 20;
		long long t = (long long) (my - top) * totalH / th - viewH () / 2;
		sy = (int) t; clamp (); invalidate (true);
	}
	bool key (long k)
	{
		int page = viewH () - 60;
		switch (k)
		{
		case KEY_PGDN: sy += page; break;
		case KEY_PGUP: sy -= page; break;
		case KEY_DOWN: sy += tile + gap; break;
		case KEY_UP: sy -= tile + gap; break;
		case KEY_HOME: sy = 0; break;
		case KEY_END: sy = totalH; break;
		default: return false;
		}
		clamp (); invalidate (true); return true;
	}
};

} // namespace photos

#endif
