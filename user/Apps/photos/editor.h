//
// Apps/photos/editor.h -- a photo edited: Crop (free or a ratio -- the original's, 1:1, 4:3, 3:2, 16:9 --, the
// corners and the sides dragged, the thirds' grid; a quarter turn; straighten by a few degrees), Adjust (Enhance --
// automatic --, exposure, contrast, highlights, shadows, saturation, warmth, sharpness), Filters (black and white,
// warm, cool, vintage, vivid: each shown on the photo). Before / after; Save (over the original: a JPEG keeps its
// camera facts -- EXIF --, a PNG stays a PNG) or Save as... (a new file, JPEG or PNG). The work is shown on a smaller
// copy (fast); the whole photo is done when it is saved.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors.
//
#ifndef _photos_editor_h
#define _photos_editor_h

#include "Apps/photos/viewer.h"

namespace photos {

static const char *const RATIO_NAME[6] = { "Free", "Original", "1:1", "4:3", "3:2", "16:9" };

class Editor : public Widget
{
public:
	int pi; char path[400]; PicInfo info;
	Pix src;				// the whole photo, turned the right way
	Pix base;				// a smaller copy (the work shown)
	int rot; float straight;		// quarter turns; degrees
	float c0x, c0y, c1x, c1y;		// the crop, 0..1 of the turned and straightened picture
	int ratio; Adjust adj; bool enhanced;
	int tab; bool before;
	Pix geo; int geoRot; float geoStraight;	// base turned + straightened (made again when they change)
	Pix view; bool viewDirty; int ix, iy, iw, ih;	// the picture shown, where
	Pix fthumb[FL_N]; bool fthumbDirty;
	HitList hits; int hot;
	int drag; int dragX, dragY; float d0x, d0y, d1x, d1y;	// the crop dragged (which part), from where
	int slider; 				// the slider dragged (A_*, or 100: straighten), -1
	bool changed;
	enum { E_CANCEL = 1, E_BEFORE, E_SAVEAS, E_SAVE, E_TAB, E_ROTL, E_ROTR, E_RATIO, E_SLIDER, E_STRAIGHT, E_ENHANCE, E_FILTER, E_RESET };
	enum { D_NONE, D_MOVE, D_TL, D_TR, D_BL, D_BR, D_L, D_R, D_T, D_B };
	static const int BAR_H = 46, PANEL_W = 290;

	Editor (int l, int t, int w, int h) : Widget (l, t, w, h), pi (-1), rot (0), straight (0), c0x (0), c0y (0), c1x (1), c1y (1), ratio (0), enhanced (false), tab (1), before (false),
		geoRot (-1), geoStraight (0), viewDirty (true), ix (0), iy (0), iw (0), ih (0), fthumbDirty (true), hot (0), drag (D_NONE), dragX (0), dragY (0), d0x (0), d0y (0), d1x (0), d1y (0), slider (-1), changed (false)
	{ hidden = true; memset (&adj, 0, sizeof adj); path[0] = 0; }
	unsigned bgColor () override { return D_BG; }

	// ---- opened, closed
	bool open (int photo, const Pix *have)
	{
		pi = photo; scpy (path, g_lib.ph[pi].path, sizeof path);
		if (!pic_info (path, info)) memset (&info, 0, sizeof info);
		src.free_ ();
		if (have && have->px) src.copy_of (*have);
		else if (!Thumbs::load_full (path, g_lib.ph[pi].orient, src)) { uk_messagebox ("Photos", "This picture cannot be read.", MB_OK); return false; }
		fit (src, base, 1600, 1600);
		rot = 0; straight = 0; c0x = c0y = 0; c1x = c1y = 1; ratio = 0; memset (&adj, 0, sizeof adj); enhanced = false;
		tab = 1; before = false; geoRot = -1; viewDirty = true; fthumbDirty = true; changed = false; drag = D_NONE; slider = -1;
		return true;
	}
	void close_ () { src.free_ (); base.free_ (); geo.free_ (); view.free_ (); for (int i = 0; i < FL_N; i++) fthumb[i].free_ (); }

	// ---- the work
	void make_geo ()
	{
		if (geoRot == rot && geoStraight == straight && geo.px) return;
		geo.copy_of (base); rotate90 (geo, rot); straighten (geo, straight);
		geoRot = rot; geoStraight = straight; viewDirty = true; fthumbDirty = true;
	}
	void crop_px (int W, int H, int *x, int *y, int *w, int *h) const
	{
		*x = (int) (c0x * W + 0.5f); *y = (int) (c0y * H + 0.5f); *w = (int) ((c1x - c0x) * W + 0.5f); *h = (int) ((c1y - c0y) * H + 0.5f);
		if (*w < 1) *w = 1; if (*h < 1) *h = 1;
	}
	void make_view ()
	{
		make_geo ();
		if (!viewDirty && view.px) return;
		viewDirty = false;
		int aw = width - PANEL_W - 60, ah = height - BAR_H - (tab == 0 ? 90 : 50);
		if (tab == 0)
		{	// the whole picture, the crop drawn over it
			fit (geo, view, aw, ah, true);
		}
		else
		{
			int x, y, w, h; crop_px (geo.w, geo.h, &x, &y, &w, &h);
			Pix c; c.alloc (w, h);
			if (!c.px) return;
			for (int j = 0; j < h; j++) memcpy (c.px + (size_t) j * w, geo.px + (size_t) (y + j) * geo.w + x, (size_t) w * 4);
			fit (c, view, aw, ah, true);
			if (!before) adjust (view, adj);
		}
		iw = view.w; ih = view.h; ix = (width - PANEL_W - iw) / 2; iy = BAR_H + (height - BAR_H - (tab == 0 ? 50 : 0) - ih) / 2;
	}
	void make_fthumbs ()
	{
		if (!fthumbDirty) return;
		fthumbDirty = false;
		make_geo ();
		int x, y, w, h; crop_px (geo.w, geo.h, &x, &y, &w, &h);
		Pix c; c.alloc (w, h); if (!c.px) return;
		for (int j = 0; j < h; j++) memcpy (c.px + (size_t) j * w, geo.px + (size_t) (y + j) * geo.w + x, (size_t) w * 4);
		Pix small; fit (c, small, 240, 240);
		for (int f = 0; f < FL_N; f++)
		{
			fthumb[f].alloc (118, 84); if (!fthumb[f].px) continue;
			cover (small, fthumb[f].px, 118, 84);
			Adjust a = adj; a.filter = f; adjust (fthumb[f], a);
		}
	}
	void dirty () { viewDirty = true; changed = true; invalidate (true); }
	// the crop made to the ratio (the largest of that shape around its centre)
	void apply_ratio ()
	{
		if (ratio == 0) return;
		float W = (float) geo.w, H = (float) geo.h;
		float r = ratio == 1 ? (float) src.w / src.h : ratio == 2 ? 1.0f : ratio == 3 ? 4.0f / 3 : ratio == 4 ? 1.5f : 16.0f / 9;
		if (ratio >= 2 && (c1y - c0y) * H > (c1x - c0x) * W) r = 1 / r;			// (portrait: the shape stood up)
		if (ratio == 1 && (rot & 1)) r = 1 / r;
		float cx = (c0x + c1x) / 2 * W, cy = (c0y + c1y) / 2 * H;
		float w = W, h = W / r; if (h > H) { h = H; w = H * r; }
		float x = cx - w / 2, y = cy - h / 2;
		if (x < 0) x = 0; if (y < 0) y = 0; if (x + w > W) x = W - w; if (y + h > H) y = H - h;
		c0x = x / W; c0y = y / H; c1x = (x + w) / W; c1y = (y + h) / H;
	}
	float ratio_value () const
	{
		if (ratio == 0) return 0;
		float r = ratio == 1 ? (float) src.w / src.h : ratio == 2 ? 1.0f : ratio == 3 ? 4.0f / 3 : ratio == 4 ? 1.5f : 16.0f / 9;
		if (ratio == 1 && (rot & 1)) r = 1 / r;
		if (ratio >= 2 && (d1y - d0y) * geo.h > (d1x - d0x) * geo.w) r = 1 / r;
		return r;
	}

	// ---- drawing
	void button (int x, int y, int w, const char *l, int kind, bool accent)
	{
		unsigned c = accent ? (hot == kind ? uk_mix (C_ACCENT, 0xFFFFFF, 30) : C_ACCENT) : (hot == kind || (kind == E_BEFORE && before) ? 0x565A63 : D_BTN);
		fill_round (canvas, x, y, w, 32, 6, c);
		text_c (canvas, x, y, w, 32, l, accent ? C_SEL_TEXT : D_TEXT, F_UI, accent ? 1 : 0);
		hits.add (x, y, w, 32, kind);
	}
	void onDraw () override
	{
		canvas.clear (D_BG); hits.clear ();
		if (!src.px) return;
		make_view ();
		// the picture
		if (view.px) Thumbs::blit (canvas, view.px, ix, iy, iw, ih);
		if (tab == 0) draw_crop ();
		// the bar
		canvas.fillRect (0, 0, width, BAR_H, D_BAR);
		{
			int w = tw ("Cancel") + 44;
			if (hot == E_CANCEL) fill_round (canvas, 8, 6, w, 34, 6, 0xFFFFFF, 30);
			icon (canvas, I_BACK, 16, 13, 20, D_TEXT); text_v (canvas, 40, 6, 34, "Cancel", D_TEXT);
			hits.add (8, 6, w, 34, E_CANCEL);
			int rx = width - 8;
			button (rx -= 84, 7, 84, "Save", E_SAVE, true);
			button (rx -= 104, 7, 96, "Save as...", E_SAVEAS, false);
			button (rx -= 128, 7, 120, "Before / after", E_BEFORE, false);
			text_c (canvas, w + 10, 0, rx - w - 20, BAR_H, base_name (path), 0xDCDEE2, F_UI, 1);
		}
		draw_panel ();
	}
	void draw_crop ()
	{
		int x0 = ix + (int) (c0x * iw), y0 = iy + (int) (c0y * ih), x1 = ix + (int) (c1x * iw), y1 = iy + (int) (c1y * ih);
		// dimmed outside
		uk_rbox (canvas, ix, iy, iw, y0 - iy, 0, 0, 0, 150);
		uk_rbox (canvas, ix, y1, iw, iy + ih - y1, 0, 0, 0, 150);
		uk_rbox (canvas, ix, y0, x0 - ix, y1 - y0, 0, 0, 0, 150);
		uk_rbox (canvas, x1, y0, ix + iw - x1, y1 - y0, 0, 0, 0, 150);
		// the frame, the thirds
		uk_rline (canvas, x0, y0, x1 - x0, y1 - y0, 0, 0xFFFFFF);
		uk_rline (canvas, x0 + 1, y0 + 1, x1 - x0 - 2, y1 - y0 - 2, 0, 0xFFFFFF, 160);
		for (int k = 1; k < 3; k++)
		{
			uk_rbox (canvas, x0 + (x1 - x0) * k / 3, y0, 1, y1 - y0, 0, 0xFFFFFF, 0xFFFFFF, 110);
			uk_rbox (canvas, x0, y0 + (y1 - y0) * k / 3, x1 - x0, 1, 0, 0xFFFFFF, 0xFFFFFF, 110);
		}
		int hx[4] = { x0, x1, x0, x1 }, hy[4] = { y0, y0, y1, y1 };
		for (int k = 0; k < 4; k++) fill_round (canvas, hx[k] - 6, hy[k] - 6, 12, 12, 3, 0xFFFFFF);
		// the ratios, a quarter turn
		int cx = ix, cy = height - 42;
		for (int r = 0; r < 6; r++)
		{
			int w = tw (RATIO_NAME[r], F_SMALL, r == ratio) + 24;
			fill_round (canvas, cx, cy, w, 28, 14, r == ratio ? C_ACCENT : (hot == E_RATIO + 100 * (r + 1) ? 0x50555E : 0x3C4048));
			text_c (canvas, cx, cy, w, 28, RATIO_NAME[r], r == ratio ? C_SEL_TEXT : D_TEXT, F_SMALL, r == ratio);
			hits.add (cx, cy, w, 28, E_RATIO, r); cx += w + 6;
		}
		cx += 8;
		if (hot == E_ROTL) fill_round (canvas, cx, cy - 2, 32, 32, 6, 0xFFFFFF, 30);
		icon (canvas, I_ROTATE_L, cx + 6, cy + 4, 20, D_TEXT); hits.add (cx, cy - 2, 32, 32, E_ROTL);
	}
	void draw_slider (int x, int y, int w, const char *label, int val, int lo, int hi, int kind, int arg, const char *show = 0)
	{
		text (canvas, x, y, label, D_TEXT);
		char v[16]; if (show) scpy (v, show, sizeof v); else snprintf (v, sizeof v, lo < 0 && val > 0 ? "+%d" : "%d", val);
		text_r (canvas, x + w, y, fh (), v, val ? D_TEXT : D_DIM, F_SMALL);
		int ty = y + 26;
		fill_round (canvas, x, ty, w, 4, 2, 0x50545C);
		float t = (float) (val - lo) / (hi - lo); int kx = x + (int) (w * t);
		int mid = lo < 0 ? x + (int) (w * (float) (0 - lo) / (hi - lo)) : x;
		if (kx != mid) fill_round (canvas, kx < mid ? kx : mid, ty, abs (kx - mid), 4, 2, C_ACCENT);
		disc (canvas, kx, ty + 2, 8, 0xFFFFFF); ring (canvas, kx, ty + 2, 8, C_ACCENT, 2);
		hits.add (x - 8, ty - 10, w + 16, 24, kind, arg);
	}
	void draw_panel ()
	{
		int px = width - PANEL_W;
		canvas.fillRect (px, BAR_H, PANEL_W, height - BAR_H, D_PANEL);
		canvas.fillRect (px, BAR_H, 1, height - BAR_H, D_LINE);
		static const char *const TN[3] = { "Crop", "Adjust", "Filters" }; static const int TI[3] = { I_CROP, I_SUN, I_FILTERS };
		int tx = px + 14, y = BAR_H + 12;
		for (int i = 0; i < 3; i++)
		{
			int w = 84;
			if (i == tab) fill_round (canvas, tx, y, w, 56, 8, 0x464A52);
			else if (hot == E_TAB + 100 * (i + 1)) fill_round (canvas, tx, y, w, 56, 8, 0x3A3E45);
			icon (canvas, TI[i], tx + w / 2 - 10, y + 7, 20, i == tab ? 0xFFFFFF : 0xAAAEB4);
			text_c (canvas, tx, y + 32, w, 20, TN[i], i == tab ? 0xFFFFFF : 0xAAAEB4, F_SMALL, i == tab);
			hits.add (tx, y, w, 56, E_TAB, i); tx += w + 6;
		}
		y += 74;
		int X = px + 18, W = PANEL_W - 36;
		if (tab == 0)
		{
			text (canvas, X, y, "ROTATE", 0x969AA0, F_TINY, 1); y += 18;
			for (int k = 0; k < 2; k++)
			{
				int bx = X + k * (W / 2 + 4), bw = W / 2 - 4, kind = k ? E_ROTR : E_ROTL;
				fill_round (canvas, bx, y, bw, 34, 6, hot == kind ? 0x50555E : 0x3C4048);
				icon (canvas, k ? I_ROTATE_R : I_ROTATE_L, bx + 12, y + 7, 20, D_TEXT);
				text_v (canvas, bx + 40, y, 34, k ? "Right" : "Left", D_TEXT);
				hits.add (bx, y, bw, 34, kind);
			}
			y += 52;
			char s[16]; snprintf (s, sizeof s, "%+.1f\xC2\xB0", straight); if (straight == 0) scpy (s, "0\xC2\xB0", sizeof s);
			draw_slider (X, y, W, "Straighten", (int) (straight * 10), -150, 150, E_STRAIGHT, 0, s); y += 56;
			text (canvas, X, y, "SHAPE", 0x969AA0, F_TINY, 1); y += 18;
			int cx = X;
			for (int r = 0; r < 6; r++)
			{
				int w = tw (RATIO_NAME[r], F_SMALL, r == ratio) + 22;
				if (cx + w > X + W) { cx = X; y += 34; }
				fill_round (canvas, cx, y, w, 28, 14, r == ratio ? C_ACCENT : 0x3C4048);
				text_c (canvas, cx, y, w, 28, RATIO_NAME[r], r == ratio ? C_SEL_TEXT : D_TEXT, F_SMALL, r == ratio);
				hits.add (cx, y, w, 28, E_RATIO, r); cx += w + 6;
			}
			y += 48;
			int x, yy, w, h; crop_px (src.w, src.h, &x, &yy, &w, &h);
			if (rot & 1) { int t = w; w = h; h = t; }
			char d[80]; snprintf (d, sizeof d, "The result: %d \xC3\x97 %d", (int) ((c1x - c0x) * ((rot & 1) ? src.h : src.w)), (int) ((c1y - c0y) * ((rot & 1) ? src.w : src.h)));
			text (canvas, X, y, d, D_DIM, F_SMALL);
			y += 30;
			text (canvas, X, y, "Reset", hot == E_RESET ? 0xFFFFFF : uk_mix (C_ACCENT, 0xFFFFFF, 90), F_UI, 1); hits.add (X, y - 4, tw ("Reset", F_UI, 1), 24, E_RESET);
		}
		else if (tab == 1)
		{
			fill_round (canvas, X - 4, y, W + 8, 36, 6, enhanced ? uk_mix (0x3C4048, C_ACCENT, 100) : (hot == E_ENHANCE ? 0x484C55 : 0x3C4048));
			icon (canvas, I_MAGIC, X + 6, y + 8, 20, AMBER); text_v (canvas, X + 36, y, 36, "Enhance (automatic)", D_TEXT, F_UI, 1);
			if (enhanced) icon (canvas, I_CHECK, X + W - 20, y + 9, 18, 0xFFFFFF);
			hits.add (X - 4, y, W + 8, 36, E_ENHANCE);
			y += 50;
			static const char *const SEC[3] = { "LIGHT", "COLOUR", "DETAIL" }; static const int FROM[3] = { 0, 4, 6 }, TO[3] = { 4, 6, 7 };
			for (int s = 0; s < 3; s++)
			{
				text (canvas, X, y, SEC[s], 0x969AA0, F_TINY, 1); y += 18;
				for (int a = FROM[s]; a < TO[s]; a++) { draw_slider (X, y, W, ADJ_NAME[a], adj.v[a], a == A_SHARPNESS ? 0 : -100, 100, E_SLIDER, a); y += 44; }
				y += 4;
			}
			text (canvas, X, y, "Reset", hot == E_RESET ? 0xFFFFFF : uk_mix (C_ACCENT, 0xFFFFFF, 90), F_UI, 1); hits.add (X, y - 4, tw ("Reset", F_UI, 1), 24, E_RESET);
		}
		else
		{
			make_fthumbs ();
			for (int f = 0; f < FL_N; f++)
			{
				int fx = X + (f % 2) * 130, fy = y + (f / 2) * 116;
				if (f == adj.filter) fill_round (canvas, fx - 4, fy - 4, 126, 92, 8, C_ACCENT);
				if (fthumb[f].px) Thumbs::blit (canvas, fthumb[f].px, fx, fy, 118, 84);
				round_corners (canvas, fx, fy, 118, 84, 5, f == adj.filter ? C_ACCENT : D_PANEL);
				text_c (canvas, fx - 4, fy + 88, 126, 20, FILTER_NAME[f], f == adj.filter ? 0xFFFFFF : D_DIM, F_SMALL, f == adj.filter);
				hits.add (fx, fy, 118, 108, E_FILTER, f);
			}
		}
	}

	// ---- input
	int crop_part (int mx, int my) const
	{
		int x0 = ix + (int) (c0x * iw), y0 = iy + (int) (c0y * ih), x1 = ix + (int) (c1x * iw), y1 = iy + (int) (c1y * ih);
		const int R = 14;
		bool L = abs (mx - x0) < R, Rr = abs (mx - x1) < R, T = abs (my - y0) < R, B = abs (my - y1) < R;
		bool inX = mx > x0 - R && mx < x1 + R, inY = my > y0 - R && my < y1 + R;
		if (L && T) return D_TL; if (Rr && T) return D_TR; if (L && B) return D_BL; if (Rr && B) return D_BR;
		if (L && inY) return D_L; if (Rr && inY) return D_R; if (T && inX) return D_T; if (B && inX) return D_B;
		if (mx > x0 && mx < x1 && my > y0 && my < y1) return D_MOVE;
		return D_NONE;
	}
	void crop_drag (int mx, int my)
	{
		float dx = (float) (mx - dragX) / iw, dy = (float) (my - dragY) / ih;
		float a = d0x, b = d0y, c = d1x, d = d1y; const float MIN = 0.05f;
		switch (drag)
		{
		case D_MOVE: { float w = c - a, h = d - b; a += dx; b += dy; if (a < 0) a = 0; if (b < 0) b = 0; if (a + w > 1) a = 1 - w; if (b + h > 1) b = 1 - h; c = a + w; d = b + h; break; }
		case D_TL: a += dx; b += dy; break; case D_TR: c += dx; b += dy; break;
		case D_BL: a += dx; d += dy; break; case D_BR: c += dx; d += dy; break;
		case D_L: a += dx; break; case D_R: c += dx; break; case D_T: b += dy; break; case D_B: d += dy; break;
		}
		if (a < 0) a = 0; if (b < 0) b = 0; if (c > 1) c = 1; if (d > 1) d = 1;
		if (c - a < MIN) { if (drag == D_TL || drag == D_BL || drag == D_L) a = c - MIN; else c = a + MIN; }
		if (d - b < MIN) { if (drag == D_TL || drag == D_TR || drag == D_T) b = d - MIN; else d = b + MIN; }
		float r = ratio_value ();
		if (r > 0 && drag != D_MOVE)
		{	// the shape kept: the height follows the width (a side: around its middle)
			float W = (float) geo.w, H = (float) geo.h;
			float w = (c - a) * W, h = w / r;
			if (drag == D_T || drag == D_B) { h = (d - b) * H; w = h * r; }
			if (h > H) { h = H; w = h * r; } if (w > W) { w = W; h = w / r; }
			float nh = h / H, nw = w / W;
			if (drag == D_TL || drag == D_TR) b = d - nh; else if (drag == D_BL || drag == D_BR) d = b + nh;
			else if (drag == D_L || drag == D_R) { float m = (b + d) / 2; b = m - nh / 2; d = m + nh / 2; }
			else { float m = (a + c) / 2; a = m - nw / 2; c = m + nw / 2; }
			if (drag == D_TL || drag == D_BL) a = c - nw; else if (drag == D_TR || drag == D_BR) c = a + nw;
			if (b < 0) { d -= b; b = 0; } if (d > 1) { b -= d - 1; d = 1; } if (a < 0) { c -= a; a = 0; } if (c > 1) { a -= c - 1; c = 1; }
			if (a < 0) a = 0; if (b < 0) b = 0;
		}
		c0x = a; c0y = b; c1x = c; c1y = d;
		changed = true; fthumbDirty = true; invalidate (true);
	}
	void slider_at (int mx)
	{
		const Hit *h = 0; for (int i = 0; i < hits.n; i++) if ((hits.h[i].kind == E_SLIDER && hits.h[i].a == slider) || (slider == 100 && hits.h[i].kind == E_STRAIGHT)) h = &hits.h[i];
		if (!h) return;
		float t = (float) (mx - (h->x + 8)) / (h->w - 16); if (t < 0) t = 0; if (t > 1) t = 1;
		if (slider == 100) { float v = -15.0f + 30.0f * t; v = (int) (v * 2 + (v >= 0 ? 0.5f : -0.5f)) / 2.0f; if (v > -0.3f && v < 0.3f) v = 0; if (v != straight) { straight = v; make_geo (); dirty (); } return; }
		int lo = slider == A_SHARPNESS ? 0 : -100, v = lo + (int) ((100 - lo) * t + 0.5f);
		if (lo < 0 && v > -4 && v < 4) v = 0;
		if (v != adj.v[slider]) { adj.v[slider] = v; fthumbDirty = true; dirty (); }
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		if (mx < 0 && drag == D_NONE && slider < 0) { if (hot) { hot = 0; invalidate (true); } return false; }
		static bool wasL; bool down = bl && !wasL, up = !bl && wasL; wasL = bl;
		if (drag != D_NONE) { if (!bl) { drag = D_NONE; catchOutside = false; fthumbDirty = true; } else crop_drag (mx, my); return true; }
		if (slider >= 0) { if (!bl) { slider = -1; catchOutside = false; } else slider_at (mx); return true; }
		const Hit *h = hits.at (mx, my);
		int nh = h ? (h->kind == E_RATIO || h->kind == E_TAB ? h->kind + 100 * (h->a + 1) : h->kind) : 0;
		if (nh != hot) { hot = nh; invalidate (true); }
		if (down && tab == 0 && !h)
		{
			int part = crop_part (mx, my);
			if (part != D_NONE) { drag = part; dragX = mx; dragY = my; d0x = c0x; d0y = c0y; d1x = c1x; d1y = c1y; catchOutside = true; return true; }
		}
		if (down && h && (h->kind == E_SLIDER || h->kind == E_STRAIGHT)) { slider = h->kind == E_STRAIGHT ? 100 : h->a; catchOutside = true; slider_at (mx); return true; }
		if (down && h && h->kind == E_BEFORE) { before = true; viewDirty = true; invalidate (true); return true; }
		if (up && before) { before = false; viewDirty = true; invalidate (true); }
		if (!up || !h) return true;
		switch (h->kind)
		{
		case E_CANCEL: if (!changed || uk_messagebox ("Photos", "Leave without saving the changes?", MB_YESNO) == 1) close_editor (); break;
		case E_SAVE: save (false); break;
		case E_SAVEAS: save (true); break;
		case E_TAB: tab = h->a; viewDirty = true; invalidate (true); break;
		case E_ROTL: rot = (rot + 3) & 3; make_geo (); apply_ratio (); dirty (); break;
		case E_ROTR: rot = (rot + 1) & 3; make_geo (); apply_ratio (); dirty (); break;
		case E_RATIO: ratio = h->a; if (ratio == 0) { } else apply_ratio (); fthumbDirty = true; dirty (); break;
		case E_ENHANCE:
			if (enhanced) { for (int i = 0; i < A_N; i++) adj.v[i] = 0; enhanced = false; }
			else { make_geo (); auto_enhance (geo, adj); enhanced = true; }
			fthumbDirty = true; dirty (); break;
		case E_FILTER: adj.filter = h->a; dirty (); break;
		case E_RESET:
			if (tab == 0) { rot = 0; straight = 0; c0x = c0y = 0; c1x = c1y = 1; ratio = 0; make_geo (); }
			else { for (int i = 0; i < A_N; i++) adj.v[i] = 0; enhanced = false; }
			fthumbDirty = true; dirty (); break;
		}
		return true;
	}
	bool key (long k)
	{
		if (k == 27) { if (!changed || uk_messagebox ("Photos", "Leave without saving the changes?", MB_YESNO) == 1) close_editor (); return true; }
		if (k == UK_CTRL ('S')) { save (false); return true; }
		return false;
	}

	// ---- saved
	bool render (Pix &out)
	{
		out.copy_of (src); if (!out.px) return false;
		rotate90 (out, rot); straighten (out, straight);
		int x, y, w, h; crop_px (out.w, out.h, &x, &y, &w, &h); crop (out, x, y, w, h);
		adjust (out, adj);
		return out.px != 0;
	}
	static bool ends (const char *s, const char *e) { int a = (int) strlen (s), b = (int) strlen (e); return a >= b && ieq (s + a - b, e); }
	void save (bool as)
	{
		bool png = ends (path, ".png"), jpg = ends (path, ".jpg") || ends (path, ".jpeg") || ends (path, ".jpe");
		char out[400]; scpy (out, path, sizeof out);
		if (!as && !png && !jpg)
		{
			uk_messagebox ("Photos", "Photos writes JPEG and PNG pictures: choose a name for the edited one.", MB_OK);
			as = true;
		}
		if (as)
		{
			char dir[400], name[200]; scpy (dir, path, sizeof dir); char *s = strrchr (dir, '/'); if (s) *s = 0;
			scpy (name, base_name (path), sizeof name); char *dot = strrchr (name, '.'); if (dot) *dot = 0;
			char def[240]; snprintf (def, sizeof def, "%s (edited).%s", name, png ? "png" : "jpg");
			if (!uk_file_save (out, sizeof out, dir, def)) return;
			if (!ends (out, ".png") && !ends (out, ".jpg") && !ends (out, ".jpeg")) { int k = (int) strlen (out); if (k < 390) strcpy (out + k, png ? ".png" : ".jpg"); }
			png = ends (out, ".png");
		}
		status_note ("Saving...");
		Pix r; if (!render (r)) { uk_messagebox ("Photos", "Not enough memory to save this picture.", MB_OK); return; }
		opaque (r);
		unsigned n = 0; unsigned char *data = png ? pngsave::png_encode (r.px, r.w, r.h, false, &n) : pngsave::jpeg_encode (r.px, r.w, r.h, 92, &n);
		if (data && !png && info.exifOff) { unsigned m = 0; unsigned char *x = jpeg_with_exif (data, n, path, info, &m); if (x) { delete[] data; data = x; n = m; } }
		int ok = data ? kapi_save_file (out, data, n) : -1;
		delete[] data;
		if (ok < 0) { uk_messagebox ("Photos", "The picture could not be written.", MB_OK); return; }
		// the library follows
		PicInfo ni; pic_info (out, ni);
		int k = g_lib.find (out);
		if (k >= 0)
		{
			Photo &p = g_lib.ph[k];
			g_th.forget (p.key ());
			p.size = n; p.w = (short) r.w; p.h = (short) r.h; p.orient = 1;
			if (ni.taken) p.taken = ni.taken;
		}
		else
		{
			bool watched = false; for (int i = 0; i < g_lib.nroots; i++) if (ipfx (out, g_lib.roots[i])) watched = true;
			if (watched) { Photo p; photo_from (p, out, n, ni, now_local ()); if (!p.taken) p.taken = g_lib.ph[pi].taken; g_lib.ph.push (p); g_lib.reindex (); }
		}
		g_lib.save ();
		changed = false;
		char s[300]; snprintf (s, sizeof s, "Saved: %s", base_name (out)); status_note (s);
		int show = g_lib.find (out);
		close_editor ();
		lib_changed ();
		// the viewer on the photo saved
		for (int i = 0; i < g_list.n; i++) if (g_list[i] == show) { open_viewer (i); break; }
	}
};

} // namespace photos

#endif
