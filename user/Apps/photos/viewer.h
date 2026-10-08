//
// Apps/photos/viewer.h -- a photo seen big, dark around it: the previous and the next (the arrows, the keys, the film
// strip of its neighbours underneath), the zoom (fit, + and -, the wheel; dragged when larger than the window); at the
// top: back, when it was taken, favourite, rotate, edit, add to an album, share, delete, the details. The details: the
// file (its size, its weight), the day and the time, the camera and the exposure, the folder, its albums, a
// description one writes. While the photo is decoded (a thread: thumbs.h) its thumbnail is shown, made big.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
//
#ifndef _photos_viewer_h
#define _photos_viewer_h

#include "Apps/photos/grid.h"

namespace photos {

// a description written
class DescBox : public Modal
{
public:
	Textarea *t; static const int W = 460, H = 260;
	DescBox (const char *init) : Modal (W, H)
	{
		t = new Textarea (20, titleH () + 16, W - 40, H - titleH () - 80, 1000); t->setContent (init); addChild (t);
		Button *b = new Button (W - 240, H - 48, 100, 32, TR ("Cancel"), [] (Widget &w) { ((Modal *) w.parent)->close (0); }); addChild (b);
		b = new Button (W - 130, H - 48, 110, 32, TR ("Keep"), [] (Widget &w) { ((Modal *) w.parent)->close (1); }); addChild (b);
		t->setFocus ();
	}
	void onDraw () override { drawBox (TR ("Description")); }
	bool ask (char *out, int cap) { if (run () != 1) return false; scpy (out, t->content (), cap); return true; }
};

class Viewer : public Widget
{
public:
	int pos, pi;				// the list position shown, its photo
	Pix full; bool loading, failed;
	Pix disp; int dispW, dispH;		// the fitted picture (made once per size)
	bool fit; float zoom;			// zoom: screen pixels per photo pixel
	float panX, panY;			// the photo pixel at the area's centre
	bool info;
	HitList hits; int hot;
	bool dragging; int dragX, dragY; bool moved;
	enum { V_BACK = 1, V_FAV, V_ROTATE, V_EDIT, V_ALBUM, V_SHARE, V_TRASH, V_INFO, V_PREV, V_NEXT, V_ZOOMOUT, V_FIT, V_ZOOMIN, V_STRIP, V_DESC, V_ADDALBUM, V_CHIP };
	static const int BAR_H = 46, STRIP_H = 80, PANEL_W = 300;

	Viewer (int l, int t, int w, int h) : Widget (l, t, w, h), pos (-1), pi (-1), loading (false), failed (false), dispW (0), dispH (0), fit (true), zoom (1), panX (0), panY (0), info (true), hot (0), dragging (false), dragX (0), dragY (0), moved (false) { hidden = true; }
	unsigned bgColor () override { return D_BG; }

	int area_w () const { return width - (info ? PANEL_W : 0); }
	int area_h () const { return height - BAR_H - STRIP_H; }
	float fit_scale () const
	{
		if (!full.px) { const Photo &p = g_lib.ph[pi]; if (p.w > 0 && p.h > 0) { float a = (area_w () - 40.0f) / p.w, b = (area_h () - 20.0f) / p.h; float s = a < b ? a : b; return s > 1 ? 1 : s; } return 1; }
		float a = (area_w () - 40.0f) / full.w, b = (area_h () - 20.0f) / full.h; float s = a < b ? a : b; return s > 1 ? 1 : s;
	}
	float scale () const { return fit ? fit_scale () : zoom; }

	void show (int listPos)
	{
		if (listPos < 0 || listPos >= g_list.n) return;
		pos = listPos; pi = g_list[pos];
		full.free_ (); disp.free_ (); dispW = dispH = 0;
		loading = true; failed = false; fit = true;
		g_th.want_full (pi);
		invalidate (true);
	}
	void full_came (FullDone *d)
	{
		if (d->index != pi) return;
		loading = false;
		if (d->failed) { failed = true; invalidate (true); return; }
		full.take (d->pix);
		Photo &p = g_lib.ph[pi];
		if (p.w != full.w || p.h != full.h) { p.w = (short) full.w; p.h = (short) full.h; g_lib.dirty = true; }
		disp.free_ (); dispW = dispH = 0;
		invalidate (true);
	}
	void step (int d)
	{
		int n = pos + d;
		if (n < 0 || n >= g_list.n) return;
		show (n);
	}

	// ---- drawing
	void bar_button (int &x, int kind, int ic, unsigned col)
	{
		if (hot == kind) fill_round (canvas, x, 6, 36, 34, 6, 0xFFFFFF, 30);
		if (kind == V_INFO && info) fill_round (canvas, x, 6, 36, 34, 6, 0xFFFFFF, 45);
		icon (canvas, ic, x + 9, 14, 18, col);
		hits.add (x, 6, 36, 34, kind); x += 40;
	}
	void onDraw () override
	{
		canvas.clear (D_BG); hits.clear ();
		if (pi < 0 || pi >= g_lib.ph.n) return;
		const Photo &p = g_lib.ph[pi];
		int aw = area_w (), ah = area_h ();
		// the photo
		draw_photo (0, BAR_H, aw, ah);
		// the arrows
		int cy = BAR_H + ah / 2;
		if (pos > 0) { disc (canvas, 30, cy, 20, 0x000000, hot == V_PREV ? 170 : 110); icon (canvas, I_BACK, 18, cy - 12, 24, 0xFFFFFF); hits.add (8, cy - 24, 46, 48, V_PREV); }
		if (pos < g_list.n - 1) { disc (canvas, aw - 30, cy, 20, 0x000000, hot == V_NEXT ? 170 : 110); icon (canvas, I_NEXT, aw - 42, cy - 12, 24, 0xFFFFFF); hits.add (aw - 54, cy - 24, 46, 48, V_NEXT); }
		// the zoom
		{
			char z[16]; if (fit) scpy (z, TR ("Fit"), sizeof z); else snprintf (z, sizeof z, "%d%%", (int) (zoom * 100 + 0.5f));
			int zw = 150, zx = aw / 2 - zw / 2, zy = BAR_H + ah - 40;
			fill_round (canvas, zx, zy, zw, 30, 15, 0x000000, 160);
			icon (canvas, I_MINUS, zx + 14, zy + 7, 16, hot == V_ZOOMOUT ? 0xFFFFFF : D_DIM);
			text_c (canvas, zx + 40, zy, zw - 80, 30, z, 0xFFFFFF, F_UI, 1);
			icon (canvas, I_PLUS, zx + zw - 30, zy + 7, 16, hot == V_ZOOMIN ? 0xFFFFFF : D_DIM);
			hits.add (zx, zy, 40, 30, V_ZOOMOUT); hits.add (zx + 40, zy, zw - 80, 30, V_FIT); hits.add (zx + zw - 40, zy, 40, 30, V_ZOOMIN);
		}
		// the film strip
		{
			int sy = height - STRIP_H + 12, tw_ = 66, th_ = 52, gp = 6;
			int n = (aw - 40) / (tw_ + gp); if (n < 1) n = 1;
			int first = pos - n / 2; if (first + n > g_list.n) first = g_list.n - n; if (first < 0) first = 0;
			int total = (n < g_list.n ? n : g_list.n) * (tw_ + gp) - gp;
			int x = (aw - total) / 2;
			for (int k = first; k < g_list.n && k < first + n; k++, x += tw_ + gp)
			{
				if (k == pos) fill_round (canvas, x - 3, sy - 3, tw_ + 6, th_ + 6, 6, 0xFFFFFF);
				g_th.draw (canvas, g_list[k], x, sy, tw_, th_, 4, k == pos ? 0xFFFFFF : D_BG);
				if (k != pos) uk_rbox (canvas, x, sy, tw_, th_, 4, 0x000000, 0x000000, 70);
				hits.add (x, sy, tw_, th_, V_STRIP, k);
			}
		}
		// the bar
		canvas.fillRect (0, 0, width, BAR_H, D_BAR);
		{
			const char *l = src_title (); int w = tw (l) + 44;
			if (hot == V_BACK) fill_round (canvas, 8, 6, w, 34, 6, 0xFFFFFF, 30);
			icon (canvas, I_BACK, 16, 13, 20, D_TEXT); text_v (canvas, 40, 6, 34, l, D_TEXT, F_UI, 0, 160);
			hits.add (8, 6, w, 34, V_BACK);
			char t[160], d[80], tm[16];
			if (p.when ()) { fmt_day (p.when (), d, sizeof d); fmt_time (p.when (), tm, sizeof tm); snprintf (t, sizeof t, TR ("%s, %s   \xC2\xB7   %d of %d"), d, tm, pos + 1, g_list.n); }
			else snprintf (t, sizeof t, TR ("%s   \xC2\xB7   %d of %d"), base_name (p.path), pos + 1, g_list.n);
			int rx = width - 8 - 7 * 40;
			int cx = w + 20, cw = rx - cx - 10;
			if (tw (t) < cw) text_c (canvas, cx, 0, cw, BAR_H, t, 0xC8CACE);
			int x = rx;
			bar_button (x, V_FAV, p.fav ? I_HEART : I_HEART_O, p.fav ? RED : D_TEXT);
			bar_button (x, V_ROTATE, I_ROTATE_L, D_TEXT);
			bar_button (x, V_EDIT, I_EDIT, D_TEXT);
			bar_button (x, V_ALBUM, I_ALBUM, D_TEXT);
			bar_button (x, V_SHARE, I_SHARE, D_TEXT);
			bar_button (x, V_TRASH, I_TRASH, D_TEXT);
			bar_button (x, V_INFO, I_INFO, D_TEXT);
		}
		if (info) draw_panel (aw);
	}
	void draw_photo (int ax, int ay, int aw, int ah)
	{
		Canvas &cv = canvas;
		if (failed)
		{
			icon (cv, I_PHOTOS, ax + aw / 2 - 30, ay + ah / 2 - 60, 60, D_FAINT);
			text_c (cv, ax, ay + ah / 2 + 6, aw, 24, TR ("This picture cannot be read."), D_DIM);
			return;
		}
		if (!full.px)
		{	// meanwhile its thumbnail, made big
			const Pix *b = g_th.base (pi);
			if (!b) return;
			float s = fit_scale (); const Photo &p = g_lib.ph[pi];
			int w = (int) ((p.w > 0 ? p.w : b->w) * s), h = (int) ((p.h > 0 ? p.h : b->h) * s);
			if (w < 1 || h < 1) return;
			if (w > aw) w = aw; if (h > ah) h = ah;
			int x = ax + (aw - w) / 2, y = ay + (ah - h) / 2;
			scale_into (b->px, b->w, b->h, 0, 0, b->w, b->h, cv.px + (long) y * cv.stride + x, cv.stride, w, h);
			return;
		}
		float s = scale ();
		int w = (int) (full.w * s + 0.5f), h = (int) (full.h * s + 0.5f); if (w < 1) w = 1; if (h < 1) h = 1;
		if (fit)
		{
			if (!disp.px || dispW != w || dispH != h) { disp.alloc (w, h); dispW = w; dispH = h; if (disp.px) scale_into (full.px, full.w, full.h, 0, 0, full.w, full.h, disp.px, w, w, h); }
			if (disp.px) { Canvas sub; int x = ax + (aw - w) / 2, y = ay + (ah - h) / 2; Thumbs::blit (cv, disp.px, x, y, w, h); (void) sub; }
			return;
		}
		// zoomed: the part in sight
		clamp_pan ();
		float vw = aw / s, vh = ah / s;				// photo pixels in sight
		float sx = panX - vw / 2, sy = panY - vh / 2;
		int dx = ax, dy = ay, dw = aw, dh = ah;
		if (w < aw) { dx = ax + (aw - w) / 2; dw = w; sx = 0; vw = (float) full.w; }
		if (h < ah) { dy = ay + (ah - h) / 2; dh = h; sy = 0; vh = (float) full.h; }
		int isx = (int) sx, isy = (int) sy, isw = (int) (vw + 0.5f), ish = (int) (vh + 0.5f);
		if (isw < 1) isw = 1; if (ish < 1) ish = 1;
		if (isx + isw > full.w) isx = full.w - isw; if (isy + ish > full.h) isy = full.h - ish;
		if (isx < 0) isx = 0; if (isy < 0) isy = 0;
		scale_into (full.px, full.w, full.h, isx, isy, isw, ish, cv.px + (long) dy * cv.stride + dx, cv.stride, dw, dh);
	}
	void clamp_pan ()
	{
		if (!full.px) return;
		float s = scale (), vw = area_w () / s / 2, vh = area_h () / s / 2;
		if (panX < vw) panX = vw; if (panX > full.w - vw) panX = full.w - vw;
		if (panY < vh) panY = vh; if (panY > full.h - vh) panY = full.h - vh;
		if (full.w * s <= area_w ()) panX = full.w / 2.0f;
		if (full.h * s <= area_h ()) panY = full.h / 2.0f;
	}
	void set_zoom (float z)
	{
		if (!full.px) return;
		float fs = fit_scale ();
		if (fit) { panX = full.w / 2.0f; panY = full.h / 2.0f; }
		if (z < fs) { fit = true; invalidate (true); return; }
		if (z > 8) z = 8;
		fit = false; zoom = z; clamp_pan (); invalidate (true);
	}
	void draw_panel (int x)
	{
		const Photo &p = g_lib.ph[pi];
		canvas.fillRect (x, BAR_H, PANEL_W, height - BAR_H, D_PANEL);
		canvas.fillRect (x, BAR_H, 1, height - BAR_H, D_LINE);
		int y = BAR_H + 16, X = x + 18, W = PANEL_W - 36;
		text (canvas, X, y, TR ("Details"), D_TEXT, F_H2, 1); y += 36;
		auto row = [&] (int ic, const char *a, const char *b) {
			icon (canvas, ic, X, y + 2, 18, 0xAAAEB4);
			text (canvas, X + 30, y, a, D_TEXT, F_UI, 1, W - 30);
			if (b && b[0]) text (canvas, X + 30, y + 19, b, D_DIM, F_SMALL, 0, W - 30);
			y += b && b[0] ? 46 : 30;
		};
		char a[200], b[200];
		char sz[40]; fmt_size (p.size, sz, sizeof sz);
		if (p.w > 0) snprintf (b, sizeof b, "%d \xC3\x97 %d  \xC2\xB7  %s", p.w, p.h, sz); else scpy (b, sz, sizeof b);
		row (I_PHOTOS, base_name (p.path), b);
		if (p.taken) { fmt_day (p.taken, a, sizeof a); fmt_time (p.taken, b, sizeof b); row (I_CLOCK, a, b); }
		else row (I_CLOCK, TR ("No date"), TR ("(the camera did not write one)"));
		if (p.camera && p.camera[0]) row (I_INFO, p.camera, p.expo);
		{
			char dir[300]; scpy (dir, p.path, sizeof dir); char *s = strrchr (dir, '/'); if (s) *s = 0;
			const char *show = dir; for (int r = 0; r < g_lib.nroots; r++) if (ipfx (dir, g_lib.roots[r])) { const char *c = strchr (dir, ':'); show = c ? c + 2 : dir; }
			char nice[300]; int k = 0; for (const char *q = show; *q && k < 290; q++) { if (*q == '/') { nice[k++] = ' '; nice[k++] = '/'; nice[k++] = ' '; } else nice[k++] = *q; } nice[k] = 0;
			row (I_FOLDER, nice, dir);
		}
		// the albums
		y += 4; text (canvas, X, y, TR ("ALBUMS"), 0x969AA0, F_TINY, 1); y += 18;
		int cx = X;
		for (int al = 0; al < g_lib.albums.n; al++)
		{
			if (!g_lib.album_has (al, p.path)) continue;
			int w = tw (g_lib.albums[al].name, F_SMALL) + 22; if (w > W) w = W;
			if (cx + w > X + W) { cx = X; y += 28; }
			fill_round (canvas, cx, y, w, 22, 11, 0x464A52); text_c (canvas, cx, y, w, 22, g_lib.albums[al].name, D_TEXT, F_SMALL);
			hits.add (cx, y, w, 22, V_CHIP, al); cx += w + 6;
		}
		{
			const char *l = TR ("+ Add"); int w = tw (l, F_SMALL) + 22;
			if (cx + w > X + W) { cx = X; y += 28; }
			fill_round (canvas, cx, y, w, 22, 11, hot == V_ADDALBUM ? 0x50555E : 0x3A3E45); text_c (canvas, cx, y, w, 22, l, 0xC8CCD2, F_SMALL);
			hits.add (cx, y, w, 22, V_ADDALBUM);
		}
		y += 38;
		// the description
		text (canvas, X, y, TR ("DESCRIPTION"), 0x969AA0, F_TINY, 1); y += 18;
		int bh = 86;
		fill_round (canvas, X, y, W, bh, 6, hot == V_DESC ? 0x3A3F47 : 0x34383F); uk_rline (canvas, X, y, W, bh, 6, 0x50545C);
		hits.add (X, y, W, bh, V_DESC);
		const char *d = p.desc && p.desc[0] ? p.desc : 0;
		if (!d) text (canvas, X + 10, y + 9, TR ("Add a description..."), D_FAINT, F_SMALL);
		else
		{	// word-wrapped
			char line[300]; int ly = y + 9; const char *s = d;
			while (*s && ly < y + bh - 16)
			{
				int k = 0, lastSp = -1;
				while (s[k] && s[k] != '\n') { char t[300]; if (k >= 290) break; memcpy (t, s, k + 1); t[k + 1] = 0; if (tw (t, F_SMALL) > W - 20) break; if (s[k] == ' ') lastSp = k; k++; }
				int cut = (s[k] && s[k] != '\n' && lastSp > 0) ? lastSp : k;
				memcpy (line, s, cut); line[cut] = 0;
				text (canvas, X + 10, ly, line, 0xDCDCE0, F_SMALL);
				s += cut; while (*s == ' ' || *s == '\n') s++;
				ly += 17;
			}
		}
	}

	// ---- input
	bool onMouse (int mx, int my, int bl, int br, int, int wheel) override
	{
		if (mx < 0 && !dragging) { if (hot) { hot = 0; invalidate (true); } return false; }
		static bool wasL, wasR; bool down = bl && !wasL, up = !bl && wasL, rdown = br && !wasR; wasL = bl; wasR = br;
		if (dragging)
		{
			if (!bl) { dragging = false; catchOutside = false; if (!moved) click_photo (); return true; }
			if (abs (mx - dragX) + abs (my - dragY) > 2) moved = true;
			if (!fit && full.px) { float s = scale (); panX -= (mx - dragX) / s; panY -= (my - dragY) / s; dragX = mx; dragY = my; clamp_pan (); invalidate (true); }
			return true;
		}
		const Hit *h = hits.at (mx, my);
		int nh = h ? h->kind : 0;
		if (nh != hot) { hot = nh; invalidate (true); }
		bool inPhoto = !h && my > BAR_H && my < height - STRIP_H && mx < area_w ();
		if (wheel && inPhoto) { set_zoom (scale () * (wheel > 0 ? 1.25f : 0.8f)); return true; }
		if (rdown && inPhoto) { photo_menu (pi, left + mx, top + my); return true; }
		if (down && inPhoto) { dragging = true; catchOutside = true; dragX = mx; dragY = my; moved = false; return true; }
		if (!up || !h) return true;
		switch (h->kind)
		{
		case V_BACK: close_viewer (); break;
		case V_PREV: step (-1); break;
		case V_NEXT: step (1); break;
		case V_STRIP: show (h->a); break;
		case V_FAV: act_favourite (pi); break;
		case V_ROTATE: act_rotate (pi); break;
		case V_EDIT: open_editor (pi); break;
		case V_ALBUM: case V_ADDALBUM: act_add_to_album (pi, left + mx - 120, top + my + 16); break;
		case V_SHARE: act_share_menu (pi, left + mx - 160, top + my + 16); break;
		case V_TRASH: act_delete (pi); break;
		case V_INFO: info = !info; disp.free_ (); invalidate (true); break;
		case V_ZOOMIN: set_zoom (scale () * 1.25f); break;
		case V_ZOOMOUT: set_zoom (scale () * 0.8f); break;
		case V_FIT: if (fit) set_zoom (1.0f); else { fit = true; invalidate (true); } break;
		case V_CHIP: show_source (SRC_ALBUM, h->a); break;
		case V_DESC:
		{
			Photo &p = g_lib.ph[pi];
			DescBox b (p.desc ? p.desc : ""); char d[1000];
			if (b.ask (d, sizeof d)) { free (p.desc); p.desc = sdup (d); g_lib.save (); invalidate (true); }
			break;
		}
		}
		return true;
	}
	void click_photo () { }				// (a click on the photo: nothing -- the arrows go on)
	bool key (long k)
	{
		switch (k)
		{
		case KEY_LEFT: case KEY_PGUP: step (-1); return true;
		case KEY_RIGHT: case KEY_PGDN: case ' ': step (1); return true;
		case KEY_HOME: show (0); return true;
		case KEY_END: show (g_list.n - 1); return true;
		case 27: case KEY_BACKSPACE: close_viewer (); return true;
		case '+': case '=': set_zoom (scale () * 1.25f); return true;
		case '-': set_zoom (scale () * 0.8f); return true;
		case '0': fit = true; invalidate (true); return true;
		case '1': set_zoom (1.0f); return true;
		case 'f': case 'F': act_favourite (pi); return true;
		case 'e': case 'E': open_editor (pi); return true;
		case 'i': case 'I': info = !info; invalidate (true); return true;
		case 'r': case 'R': act_rotate (pi); return true;
		case KEY_DEL: act_delete (pi); return true;
		}
		return false;
	}
};

} // namespace photos

#endif
