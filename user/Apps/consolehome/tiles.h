//
// tiles.h -- consolehome's home in the manner of the Switch's (docs/COMPACT-SHELL-STUDY.md §19, "v4"; the user's choice
// of 2026-10-10: V1 light and V2 dark, their settings page). Part of main.cpp (one unit), included after xset.h.
//
// TWO ROWS: at the bottom the CATEGORIES as round tiles (one per console that has games -- GameKit --, then Onyx's
// games, Apps, Settings: xmb.h's columns), above them the chosen category's CONTENT as big square tiles scrolling
// sideways -- a console's ROMs (their title screens), Onyx's games, Apps' categories as FOLDER tiles (in one: a BACK
// tile first, then its apps), the settings pages. Up / Down: from a row to the other; Left / Right: in the row; A:
// play, start, open; B: out of a folder, else back to the row's first tile; L1 / R1: the category at once (the focus
// stays above). The settings pages are xset.h's, drawn in the theme (draw_settings_sw). The look: SD:/etc/console.ini
// [look] style = light | dark | xmb (Settings > Display; g_style, g_pal in main.cpp).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//

static int g_trow;					// the focus: 0 the content row, 1 the categories
static int g_tx0 = -100000;				// the content row's left edge as drawn (eased towards its place)

// The ring around the focus: a gap, an accent line, a soft halo of the accent (docs §19.1).
static void tiles_ring (Canvas &cv, int x, int y, int w, int h, int r)
{
	TMet m = tm ();
	int g = m.rg, t = m.rw, x0 = x - g - t, y0 = y - g - t, w0 = w + 2 * (g + t), h0 = h + 2 * (g + t), r0 = r + g + t;
	uk_paint_alpha (true);
	int hl = D (6), ha = g_pal->dark ? 70 : 55;			// the halo: rings outside it, fading
	for (int k = 1; k <= hl; k++) lk_ring (cv, x0 - k, y0 - k, w0 + 2 * k, h0 + 2 * k, r0 + k, 16, g_pal->acc, ha * (hl + 1 - k) / (hl + 1));
	for (int k = 0; k < t; k++) lk_ring (cv, x0 + k, y0 + k, w0 - 2 * k, h0 - 2 * k, r0 - k, 16, g_pal->acc, 255);
	lk_ring (cv, x0 + t, y0 + t, w0 - 2 * t, h0 - 2 * t, r0 - t, 16, lighter (g_pal->acc, 140), 150);
	uk_paint_alpha (false);
}
// A mask (an XMB white icon: a console's shape, a glyph) laid in colour c, centred at cx, cy, s pixels.
static void tint_icon (Canvas &cv, int kind, const char *code, int cx, int cy, int s, unsigned c, int a = 255)
{
	if (s < 4) return;
	const unsigned char *mk = xi_mask (kind, code, s);
	int x0 = cx - s / 2, y0 = cy - s / 2;
	for (int j = 0; j < s; j++)
		for (int i = 0; i < s; i++) { int v = mk[j * s + i]; if (v) lk_px (cv, x0 + i, y0 + j, c, v * a / 255); }
}
// The consoles' colours (the mock-ups'), the folders', the settings pages'
static unsigned sys_colour (int s)
{
	const char *c = sys_code (s);
	static const struct { const char *k; unsigned c; } T[] = { { "GC", 0x7062D6 }, { "N64", 0x2E9668 }, { "SNES", 0x9684C4 }, { "GBA", 0x4270DE },
		{ "GBC", 0xCC5496 }, { "GB", 0x7A962C }, { "NES", 0xCE483E }, { "NDS", 0x5C6E96 } };
	for (unsigned i = 0; i < sizeof T / sizeof T[0]; i++) if (!strcmp (c, T[i].k)) return T[i].c;
	return 0x6A7894;
}
static unsigned cat_colour (const char *cat)
{
	static const struct { const char *k; unsigned c; } T[] = { { "Productivity", 0xF0A03C }, { "Internet", 0x3D86DA }, { "Graphics", 0x4CB653 },
		{ "Multimedia", 0xAA64C8 }, { "Programming", 0x28AAAA }, { "System", 0x828CA0 }, { "Demos", 0xDE5C7C }, { "BASIC", 0x5A78C8 } };
	for (unsigned i = 0; i < sizeof T / sizeof T[0]; i++) if (ieq (cat, T[i].k)) return T[i].c;
	return 0x7882A0;
}
static const unsigned PAGE_COL[SP_N] = { 0xE88030, 0xE24C5C, 0x7860DC, 0x5A6E8C, 0x28A096, 0x3282E2, 0x46AA5A, 0xC864B4, 0x1E96D2 };
static unsigned app_tint (const char *name)		// an app's plate: a calm colour from its name
{
	static const unsigned P[] = { 0x3D86DA, 0x4CB653, 0xF0A03C, 0xAA64C8, 0x28AAAA, 0xDE5C7C, 0x5A78C8, 0x9A7A5A };
	unsigned h = 5381; for (const char *p = name; *p; p++) h = h * 33 + (unsigned char) *p;
	return P[h % (sizeof P / sizeof P[0])];
}
static void vgrad (Canvas &cv, int x, int y, int w, int h, int r, unsigned top, unsigned bot) { lk_fill (cv, x, y, w, h, r, top, bot, 255); }

// ---- a ROM's picture on its tile: the 160 x 144 title screen made square (its first and last rows repeated above and
// below: the title is never cut), scaled to the tile -- the raw pictures cached (the ROM's index; 24 kept) ----------------
struct TThumb { int rom; unsigned gen; unsigned px[GAMES_THUMB_W * GAMES_THUMB_H]; };
static TThumb *g_tth;
static int g_ntth, g_tthGen = 1;
static const unsigned *tile_thumb (int r)
{
	if (!g_tth) g_tth = new TThumb[24];
	for (int i = 0; i < g_ntth; i++) if (g_tth[i].rom == r) { g_tth[i].gen = g_tthGen++; return g_tth[i].px; }
	int k = g_ntth < 24 ? g_ntth++ : 0;
	if (g_ntth == 24 && k == 0) for (int i = 1; i < 24; i++) if (g_tth[i].gen < g_tth[k].gen) k = i;	// (the oldest)
	g_tth[k].rom = -1;
	if (!games_thumb_load (&g_roms[r], g_tth[k].px)) { g_tth[k].rom = -2 - r; return 0; }
	g_tth[k].rom = r; g_tth[k].gen = g_tthGen++;
	return g_tth[k].px;
}
static bool tile_thumb_none (int r) { for (int i = 0; i < g_ntth; i++) if (g_tth[i].rom == -2 - r) return true; return false; }
static void tile_picture (Canvas &cv, int x, int y, int T, int r, const unsigned *pic)
{
	const int PW = GAMES_THUMB_W, PH = GAMES_THUMB_H, pad = (PW - PH) / 2;	// (160 x 144: 8 rows above and below)
	LkBox b = lk_box (x, y, T, T, r);
	for (int j = 0; j < T; j++)
	{
		if ((unsigned) (y + j) >= (unsigned) cv.h) continue;
		int sy = j * PW / T - pad;
		if (sy < 0) sy = 0;
		if (sy >= PH) sy = PH - 1;
		const unsigned *row = pic + sy * PW;
		unsigned *d = cv.px + (long) (y + j) * cv.stride;
		bool ej = j <= r || j >= T - r - 1;
		for (int i = 0; i < T; i++)
		{
			int px = x + i;
			if ((unsigned) px >= (unsigned) cv.w) continue;
			unsigned c = row[i * PW / T] & 0xFFFFFF;
			if (ej || i <= r || i >= T - r - 1) { int a = lk_cov (lk_sd (b, px, y + j)); if (a) lk_px (cv, px, y + j, c, a); }
			else d[px] = c;
		}
	}
}

// ---- what the content row holds -----------------------------------------------------------------------------------------
enum { TK_ROM, TK_ONYX, TK_FOLDER, TK_BACK, TK_APP, TK_PAGE };
static int t_count (const XCol &c)
{
	if (c.kind == X_SYS) return c.n;
	if (c.kind == X_APPS) return g_inSub ? 1 + g_nsub : g_nacat;
	return c.n;
}
static int t_sel (const XCol &c) { return c.kind == X_APPS && g_inSub ? g_subSel : c.sel; }
static void t_setsel (XCol &c, int v) { if (c.kind == X_APPS && g_inSub) g_subSel = v; else { int was = c.sel; c.sel = v; if (c.kind == X_APPS && was != v) sub_read (); } }
static int t_kind (const XCol &c, int i)
{
	if (c.kind == X_SYS) return TK_ROM;
	if (c.kind == X_ONYX) return TK_ONYX;
	if (c.kind == X_SET) return TK_PAGE;
	return g_inSub ? (i == 0 ? TK_BACK : TK_APP) : TK_FOLDER;
}
static const char *t_name (const XCol &c, int i)
{
	switch (t_kind (c, i))
	{
	case TK_ROM: return g_roms[c.first + i].name;
	case TK_ONYX: return g_apps[g_onyx[i]].label;
	case TK_FOLDER: return TR (g_cats[g_acat[i]].name);
	case TK_BACK: return TR ("Back");
	case TK_APP: return g_apps[g_sub[i - 1]].label;
	}
	return sp_name (i);
}
static int folder_apps (int ci, int *out, int max)	// a category's apps (g_apps)
{
	int n = 0;
	for (int a = 0; a < g_napps && n < max; a++)
		if (!g_apps[a].hidden && !g_apps[a].applet && ieq (g_apps[a].cat, g_cats[g_acat[ci]].name)) out[n++] = a;
	return n;
}

// One tile's face, T x T at x, y.
static void draw_tile (Canvas &cv, const TMet &m, const XCol &c, int i, int x, int y)
{
	const Pal &P = *g_pal;
	int T = m.T, r = m.r, k = t_kind (c, i);
	uk_paint_alpha (true);
	lk_shadow (cv, x, y, T, T, r, D (7), P.shadow * 2 / 3, D (5));
	if (k == TK_ROM)
	{
		int ri = c.first + i;
		const unsigned *pic = tile_thumb_none (ri) ? 0 : tile_thumb (ri);
		if (pic) { uk_paint_alpha (false); tile_picture (cv, x, y, T, r, pic); uk_paint_alpha (true); }
		else						// no picture yet: the console's shape, the game's name
		{
			unsigned col = sys_colour (c.sys);
			vgrad (cv, x, y, T, T, r, lighter (col, 60), darker (col, 40));
			tint_icon (cv, XI_CONSOLE, sys_code (c.sys), x + T / 2, y + T * 2 / 5, T * 2 / 5, 0xFFFFFF, 230);
			UkFaceScope f (F (m.c ? 12 : 17));
			text_cfit (cv, x + D (8), y + T * 3 / 4, T - D (16), g_roms[ri].name, 0xFFFFFF, 2);
		}
	}
	else if (k == TK_ONYX || k == TK_APP || k == TK_PAGE)
	{
		App *ap = k == TK_ONYX ? &g_apps[g_onyx[i]] : k == TK_APP ? &g_apps[g_sub[i - 1]] : 0;
		unsigned col = k == TK_PAGE ? PAGE_COL[i] : app_tint (ap->name);
		if (k == TK_PAGE) vgrad (cv, x, y, T, T, r, lighter (col, 26), darker (col, 50));
		else if (!P.dark) vgrad (cv, x, y, T, T, r, mixc (col, 0xFFFFFF, 184), mixc (col, 0xFFFFFF, 140));
		else vgrad (cv, x, y, T, T, r, mixc (col, 0x282C3C, 115), mixc (col, 0x10121C, 180));
		int is = T * 44 / 100;
		if (k == TK_PAGE) page_icon (cv, i, x + T / 2, y + T * 2 / 5, is, 255);
		else app_icon_at (cv, *ap, x + T / 2, y + T * 2 / 5, is, 255);
		UkFaceScope f (F (m.c ? 12 : 19));
		text_cfit (cv, x + D (8), y + T * 70 / 100, T - D (16), t_name (c, i), k == TK_PAGE || P.dark ? 0xF0F2F8 : 0x28282F, 2);
	}
	else if (k == TK_FOLDER)				// a folder of its category's colour: its first apps on a card, its name
	{
		unsigned col = cat_colour (g_cats[g_acat[i]].name);
		vgrad (cv, x, y, T, T, r, lighter (col, 30), darker (col, 56));
		int u = T;					// (in 1/100 of the tile)
		lk_fill (cv, x + 8 * u / 100, y + 9 * u / 100, 32 * u / 100, 11 * u / 100, 3 * u / 100, 0xFFFFFF, 0xFFFFFF, 235);
		lk_fill (cv, x + 8 * u / 100, y + 15 * u / 100, u - 16 * u / 100, 49 * u / 100, 5 * u / 100, 0xFFFFFF, 0xF4F5F8, 240);
		int ids[8], n = folder_apps (i, ids, 4), is = 16 * u / 100, gap = 5 * u / 100, gx = x + (T - 4 * is - 3 * gap) / 2;
		for (int a = 0; a < n; a++) app_icon_at (cv, g_apps[ids[a]], gx + a * (is + gap) + is / 2, y + 40 * u / 100, is, 255);
		{ UkFaceScope f (F (m.c ? 12 : 19)); text_fit (cv, x + 8 * u / 100, y + 68 * u / 100, T - 16 * u / 100, t_name (c, i), 0xFFFFFF, 2); }
		int all[MAXAPPS], na = folder_apps (i, all, MAXAPPS);
		char s[32]; snprintf (s, sizeof s, na == 1 ? TR ("%d app") : TR ("%d apps"), na);
		{ UkFaceScope f (F (m.c ? 10 : 14)); uk_text (cv, x + 8 * u / 100, y + 83 * u / 100, s, 0xFFFFFF); }
	}
	else							// Back: an arrow to the left, "Back", "to Apps"
	{
		lk_fill (cv, x, y, T, T, r, P.neutral, P.neutral, 255);
		int a = T * 28 / 100, cx = x + T / 2, cy = y + T * 34 / 100;
		for (int j = -a / 2; j <= a / 2; j++)		// (the arrow's head: a triangle; its tail: a bar)
		{
			int aj = j < 0 ? -j : j;			// (the tip at the left)
			lk_fill (cv, cx - a / 2 + aj, cy + j, a / 2 - aj + 1, 1, 0, P.sub, P.sub, 255);
		}
		lk_fill (cv, cx - a / 2 + a / 3, cy - a / 7, a * 2 / 3, a * 2 / 7, 0, P.sub, P.sub, 255);
		UkFaceScope f (F (m.c ? 14 : 22));
		text_cfit (cv, x, y + T * 58 / 100, T, TR ("Back"), P.fg, 2);
		UkFaceScope f2 (F (m.c ? 10 : 15));
		text_cfit (cv, x, y + T * 75 / 100, T, TR ("to Apps"), P.sub);
	}
	if (!P.dark && k != TK_ROM) lk_ring (cv, x, y, T, T, r, 16, 0x000000, 22);
	uk_paint_alpha (false);
	g_homeHits.add (x, y, T, T, H_ITEM, i);
}

// ---- the bars ------------------------------------------------------------------------------------------------------------
static void tiles_background (Canvas &cv)
{
	static unsigned *s_bg; static int s_w, s_h; static unsigned s_a, s_b;
	int W = g_sw, H = g_sh;
	if (!s_bg || s_w != W || s_h != H || s_a != g_pal->bg1 || s_b != g_pal->bg2)
	{
		delete [] s_bg; s_bg = new unsigned[W * H]; s_w = W; s_h = H; s_a = g_pal->bg1; s_b = g_pal->bg2;
		for (int y = 0; y < H; y++) { unsigned c = uk_mix (s_a, s_b, y * 256 / H); for (int x = 0; x < W; x++) s_bg[y * W + x] = c; }
	}
	for (int y = 0; y < H; y++) memcpy (cv.px + (long) y * cv.stride, s_bg + (long) y * W, (size_t) W * 4);
}
static void tiles_top (Canvas &cv, const TMet &m, const char *a, const char *b)	// the badge, where we are; the status, the time
{
	const Pal &P = *g_pal;
	int cy = m.top / 2 + D (4), av = m.badge, x = m.mx;
	uk_paint_alpha (true);
	lk_fill (cv, x, cy - av / 2, av, av, av / 2, P.acc, P.acc, 255);
	tint_icon (cv, XI_GEM, 0, x + av / 2, cy + D (1), av * 60 / 100, 0xFFFFFF);
	x += av + D (14);
	{
		UkFaceScope f (F (m.crumb));
		int ty = cy - uk_fh () / 2;
		if (b)
		{
			uk_text (cv, x, ty, a, P.sub); x += tw (a);
			uk_text (cv, x + D (8), ty, "\xE2\x80\xBA", P.sub); x += D (8) + tw ("\xE2\x80\xBA") + D (8);
			text_fit (cv, x, ty, g_sw / 2, b, P.fg, 2);
		}
		else text_fit (cv, x, ty, g_sw / 2, a, P.fg, 2);
	}
	int hh = 0, mi = 0;
	kapi_get_datetime (0, 0, 0, &hh, &mi, 0);
	char tmx[8] = { (char) ('0' + hh / 10), (char) ('0' + hh % 10), ':', (char) ('0' + mi / 10), (char) ('0' + mi % 10), 0 };
	int xr = g_sw - m.mx;
	{ UkFaceScope f (F (m.clock)); xr -= tw (tmx); uk_text (cv, xr, cy - uk_fh () / 2, tmx, P.fg); }
	xr -= D (20);
	int ph = D (m.c ? 10 : 14), pw = ph * 2;		// a pad: when one is plugged in
	struct kapi_pad pd;
	bool pad = false; for (int i = 0; i < PAD_MAX && !pad; i++) pad = kapi_pad_state (i, &pd) != 0;
	if (pad)
	{
		xr -= pw;
		lk_fill (cv, xr, cy - ph / 2, pw, ph, ph / 2, P.fg, P.fg, 255);
		lk_fill (cv, xr + ph / 3, cy - D (1), ph / 2, D (2), 0, P.bg1, P.bg1, 255);
		lk_fill (cv, xr + pw - ph / 2 - D (2), cy - D (2), D (3), D (3), D (1), P.bg1, P.bg1, 255);
		xr -= D (16);
	}
	if (kapi_net_status (0, 0))				// the network up: its bars
	{
		int bh = D (m.c ? 12 : 17);
		xr -= bars_w (bh);
		draw_bars (cv, xr, cy - bh / 2, bh, 4, P.fg);
	}
	uk_paint_alpha (false);
}
// The hints at the bottom: a thin line, the pad at the left, the buttons that work at the right (filled round ones).
static void tiles_hints (Canvas &cv, const char *const *b, const char *const *w, int n)
{
	const Pal &P = *g_pal;
	TMet m = tm ();
	int y0 = m.bary, bh = g_sh - y0, d = m.hb;
	uk_paint_alpha (true);
	lk_fill (cv, m.mx, y0, g_sw - 2 * m.mx, D (1) > 1 ? D (1) : 1, 0, P.line, P.line, 255);
	int yc = y0 + (bh - d) / 2, tot = 0;
	auto bw = [&] (const char *s) -> int		// a button's width: a circle for a letter, a pill each word else
	{
		if (strlen (s) <= 1) return d;
		UkFaceScope f (F (m.hint - 2));
		int t = 0, k = 0; char part[16];
		for (const char *p = s; ; p++)
		{
			if (*p == ' ' || !*p) { part[k] = 0; t += (k ? tw (part, 2) + D (14) : 0) + D (4); k = 0; if (!*p) break; }
			else if (k < 15) part[k++] = *p;
		}
		return t - D (4) > d ? t - D (4) : d;
	};
	{
		UkFaceScope f (F (m.hint));
		for (int i = 0; i < n; i++) tot += bw (b[i]) + D (8) + tw (w[i]) + D (26);
	}
	int x = g_sw - m.mx - tot + D (26);
	for (int i = 0; i < n; i++)
	{
		if (strlen (b[i]) <= 1)
		{
			lk_fill (cv, x, yc, d, d, d / 2, P.fg, P.fg, 255);
			UkFaceScope f (F (m.hint - 2));
			uk_text (cv, x + (d - tw (b[i], 2)) / 2, yc + (d - uk_fh ()) / 2, b[i], P.bg2, 2);
			x += d;
		}
		else
		{
			UkFaceScope f (F (m.hint - 2));
			char part[16]; int k = 0;
			for (const char *p = b[i]; ; p++)
			{
				if (*p == ' ' || !*p)
				{
					part[k] = 0;
					if (k) { int pw = tw (part, 2) + D (14); if (pw < d) pw = d; lk_fill (cv, x, yc + D (1), pw, d - D (2), (d - D (2)) / 2, P.fg, P.fg, 255);
						uk_text (cv, x + (pw - tw (part, 2)) / 2, yc + (d - uk_fh ()) / 2, part, P.bg2, 2); x += pw + D (4); }
					k = 0;
					if (!*p) break;
				}
				else if (k < 15) part[k++] = *p;
			}
			x -= D (4);
		}
		x += D (8);
		UkFaceScope f (F (m.hint));
		uk_text (cv, x, y0 + (bh - uk_fh ()) / 2, w[i], P.fg);
		x += tw (w[i]) + D (26);
	}
	uk_paint_alpha (false);
}

// ---- the home ------------------------------------------------------------------------------------------------------------
static void tiles_home (Canvas &cv)
{
	const Pal &P = *g_pal;
	TMet m = tm ();
	int W = g_sw;
	if (!g_nx)
	{
		tiles_top (cv, m, "Onyx", 0);
		UkFaceScope f (F (m.lab));
		text_cfit (cv, 0, g_sh / 2, W, TR ("Nothing here yet"), P.sub);
		return;
	}
	XCol &c = g_x[g_xf];
	int n = t_count (c), sel = t_sel (c);
	if (sel >= n) sel = n ? n - 1 : 0;
	// the top: where we are
	if (c.kind == X_APPS && g_inSub && c.sel < g_nacat) tiles_top (cv, m, TR ("Apps"), TR (g_cats[g_acat[c.sel]].name));
	else tiles_top (cv, m, col_name (c), 0);
	// the content row: from the left margin, the chosen tile in sight with one after it (eased)
	int step = m.T + m.G, vis = (W - m.mx) / step;
	if (vis < 1) vis = 1;
	int first = sel >= vis - 1 ? sel - (vis - 2) : 0;
	if (first < 0) first = 0;
	int tx0 = m.mx - first * step + (first > 0 ? m.T * 42 / 100 : 0);
	if (g_tx0 < -50000 || g_ayCol != g_xf) g_tx0 = tx0;
	{ int d = tx0 - g_tx0; g_tx0 = d > -3 && d < 3 ? tx0 : g_tx0 + d * 85 / 256 + (d > 0 ? 1 : -1); if (g_tx0 != tx0) g_anim = true; }
	for (int i = 0; i < n; i++)
	{
		int x = g_tx0 + i * step;
		if (x > W || x + m.T < 0) continue;
		draw_tile (cv, m, c, i, x, m.rowy);
	}
	int sx = g_tx0 + sel * step;
	if (n && g_trow == 0)
	{
		tiles_ring (cv, sx, m.rowy, m.T, m.T, m.r);
		UkFaceScope f (F (m.lab));
		const char *nm = t_name (c, sel);
		int lx = sx; if (lx > W - m.mx - tw (nm, 2)) lx = W - m.mx - tw (nm, 2); if (lx < m.mx) lx = m.mx;
		text_fit (cv, lx, m.laby, W - m.mx - lx, nm, P.acc, 2);
	}
	else
	{
		char cnt[32];
		snprintf (cnt, sizeof cnt, c.kind == X_SYS ? (c.n == 1 ? TR ("%d game") : TR ("%d games")) : (n == 1 ? TR ("%d item") : TR ("%d items")), c.kind == X_SYS ? c.n : n);
		UkFaceScope f (F (m.lab));
		uk_text (cv, m.mx, m.laby, col_name (c), P.fg, 2);
		int x = m.mx + tw (col_name (c), 2) + D (14);
		UkFaceScope f2 (F (m.meta));
		uk_text (cv, x, m.laby + D (m.lab - m.meta) * 3 / 4, cnt, P.sub);
	}
	// under the row: the chosen tile's line, its place
	if (n && g_trow == 0)
	{
		char line[200] = "", pos[24] = "";
		int k = t_kind (c, sel);
		if (k == TK_ROM) { const char *p = g_roms[c.first + sel].path, *f = p; for (const char *q = p; *q; q++) if (*q == '/') f = q + 1; ws_cat (line, sizeof line, f, "   \xC2\xB7   ", g_gsys[c.sys].emu); }
		else if (k == TK_FOLDER)
		{
			int ids[6], na = folder_apps (sel, ids, 5);
			for (int a = 0; a < na; a++) ws_cat (line, sizeof line, a ? ", " : "", g_apps[ids[a]].label);
			if (na == 5) ws_cat (line, sizeof line, " ...");
		}
		else if (k == TK_BACK) ws_cat (line, sizeof line, TR ("Back to the Apps' folders (B does the same)"));
		else if (k == TK_APP) ws_cat (line, sizeof line, g_apps[g_sub[sel - 1]].text[0] ? g_apps[g_sub[sel - 1]].text : g_apps[g_sub[sel - 1]].name);
		else if (k == TK_ONYX) ws_cat (line, sizeof line, TR ("Onyx game"));
		else ws_cat (line, sizeof line, sp_help (sel));
		if (k != TK_BACK) snprintf (pos, sizeof pos, "%d / %d", c.kind == X_APPS && g_inSub ? sel : sel + 1, c.kind == X_APPS && g_inSub ? n - 1 : n);
		UkFaceScope f (F (m.meta));
		uk_text (cv, W - m.mx - tw (pos), m.metay, pos, P.sub);
		text_fit (cv, m.mx, m.metay, W - 2 * m.mx - tw (pos) - D (30), line, P.sub);
	}
	// the categories: round tiles across the bottom, centred (scrolled when they do not fit)
	{
		int total = (g_nx - 1) * m.cs + m.cd, avail = W - 2 * m.mx, x0;
		if (total <= avail) x0 = (W - total) / 2 + m.cd / 2;
		else { int off = g_xf * m.cs - (avail - m.cd) / 2; if (off < 0) off = 0; if (off > total - avail) off = total - avail; x0 = m.mx + m.cd / 2 - off; }
		uk_paint_alpha (true);
		for (int i = 0; i < g_nx; i++)
		{
			int cx = x0 + i * m.cs, x = cx - m.cd / 2, y = m.caty - m.cd / 2;
			if (cx < m.mx - m.cd / 2 + D (2) && total > avail) continue;
			if (cx > W - m.mx + m.cd / 2 - D (2) && total > avail) continue;
			lk_shadow (cv, x, y, m.cd, m.cd, m.cd / 2, D (5), P.shadow * 2 / 3, D (4));
			lk_fill (cv, x, y, m.cd, m.cd, m.cd / 2, P.circle, P.circle, 255);
			const XCol &k = g_x[i];
			unsigned col = k.kind == X_SYS ? sys_colour (k.sys) : k.kind == X_ONYX ? 0xE8782C : k.kind == X_APPS ? 0x3282E2 : 0x7C808E;
			if (P.dark) col = lighter (col, 70);
			if (k.kind == X_SYS) tint_icon (cv, XI_CONSOLE, sys_code (k.sys), cx, m.caty, m.cd * 62 / 100, col);
			else tint_icon (cv, k.kind == X_ONYX ? XI_GEM : k.kind == X_APPS ? XI_GRID : XI_GEAR, 0, cx, m.caty, m.cd * 50 / 100, col);
			g_homeHits.add (x, y, m.cd, m.cd, H_CAT, i);
			if (i != g_xf) continue;
			if (g_trow == 1) tiles_ring (cv, x, y, m.cd, m.cd, m.cd / 2);
			else { int dd = D (m.c ? 5 : 7); lk_fill (cv, cx - dd / 2, y + m.cd + D (8), dd, dd, dd / 2, P.acc, P.acc, 255); }
			UkFaceScope f (F (m.catlab));
			text_cfit (cv, cx - D (150), y + m.cd + D (m.c ? 12 : 16), D (300), col_name (k), g_trow == 1 ? P.acc : P.fg, 2);
		}
		if (total > avail)				// more beyond the margins: a chevron each side
		{
			UkFaceScope f (F (m.catlab));
			if (x0 - m.cd / 2 < m.mx) uk_text (cv, (m.mx - tw ("\xE2\x80\xB9")) / 2, m.caty - uk_fh () / 2, "\xE2\x80\xB9", P.sub);
			if (x0 + (g_nx - 1) * m.cs + m.cd / 2 > W - m.mx) uk_text (cv, W - (m.mx + tw ("\xE2\x80\xBA")) / 2, m.caty - uk_fh () / 2, "\xE2\x80\xBA", P.sub);
		}
		uk_paint_alpha (false);
	}
	// the buttons that work
	{
		const char *B[6], *Wd[6]; int nh = 0;
		auto add = [&] (const char *b, const char *w) { B[nh] = b; Wd[nh++] = w; };
		if (!m.c) add ("L1 R1", TR ("Category"));
		if (g_trow == 1) { add ("B", TR ("Back")); add ("A", TR ("Choose")); }
		else
		{
			int k = n ? t_kind (c, sel) : TK_PAGE;
			add ("B", TR ("Back"));
			add ("A", k == TK_ROM || k == TK_ONYX ? TR ("Play") : k == TK_APP ? TR ("Start") : k == TK_BACK ? TR ("Back") : TR ("Open"));
		}
		tiles_hints (cv, B, Wd, nh);
	}
}

// ---- the settings in the theme: the pages listed at the left, the open one's rows at the right (§19.4) --------------------
static void draw_settings_sw (Canvas &cv)
{
	const Pal &P = *g_pal;
	TMet t = tm ();
	SMet m = sm ();
	int W = g_sw;
	rows_build ();
	tiles_top (cv, t, TR ("Settings"), scr_title (scr ()));
	// the pages, at the left (compact: their icons only)
	int lx = t.mx, lw = t.c ? D (48) : D (330), ly = D (t.c ? 56 : 96), lrh = D (t.c ? 36 : 50);
	uk_paint_alpha (true);
	lk_fill (cv, lx + lw + D (t.c ? 10 : 24), D (t.c ? 52 : 90), D (1) > 1 ? D (1) : 1, t.bary - D (t.c ? 62 : 110), 0, P.line, P.line, 255);
	int cur = g_ss[0].kind;
	for (int i = 0; i < SP_N; i++)
	{
		bool on = i == cur;
		int y = ly + i * lrh;
		if (y + lrh > t.bary) break;
		if (on)
		{
			lk_fill (cv, lx, y + D (3), lw, lrh - D (6), D (10), P.dark ? lighter (P.panel, 16) : P.panel, P.dark ? lighter (P.panel, 16) : P.panel, 255);
			lk_fill (cv, lx + D (5), y + D (12), D (4), lrh - D (24), D (2), P.acc, P.acc, 255);
		}
		int cs = D (t.c ? 26 : 30), cx = lx + (t.c ? lw / 2 : D (20) + cs / 2);
		lk_fill (cv, cx - cs / 2, y + (lrh - cs) / 2, cs, cs, D (8), PAGE_COL[i], PAGE_COL[i], 255);
		page_icon (cv, i, cx, y + lrh / 2, cs * 72 / 100, 255);
		if (!t.c)
		{
			UkFaceScope f (F (18));
			text_fit (cv, lx + D (64), y + (lrh - uk_fh ()) / 2, lw - D (70), sp_name (i), on ? P.acc : P.fg, on ? 2 : 0);
		}
		g_homeHits.add (lx, y, lw, lrh, H_PAGE, i);
	}
	uk_paint_alpha (false);
	// the page: its title, its line, its rows (xset.h's, in the theme)
	int px = lx + lw + D (t.c ? 22 : 56), pw = W - px - t.mx;
	{
		UkFaceScope f (F (t.c ? 18 : 30));
		text_fit (cv, px, D (t.c ? 52 : 98), pw, scr_title (scr ()), P.fg, 2);
	}
	if (!t.c && g_nss == 1) { UkFaceScope f (F (16)); text_fit (cv, px, D (142), pw, sp_help (g_ss[0].kind), P.sub); }
	m.rx = px + D (t.c ? 10 : 20); m.vr = px + pw - D (t.c ? 10 : 20);
	m.y0 = D (t.c ? 84 : 190); m.rh = D (t.c ? 34 : 64);
	m.lab = t.c ? 13 : 21; m.val = t.c ? 12 : 19; m.help = t.c ? 10 : 15;
	m.seg = D (t.c ? 10 : 22); m.sgap = D (t.c ? 3 : 5); m.segh = D (t.c ? 8 : 14); m.tw = D (t.c ? 34 : 52); m.th = D (t.c ? 20 : 28);
	if (scr ().kind == SC_WIZ) draw_wizard (cv, m, m.y0, t.bary - D (8));
	else draw_set_rows (cv, m, t.bary - D (8));
	// the flash: a value set, a result (under the time)
	{
		bool vol = kapi_get_ticks () - g_volFlashT < 150 && g_volFlashT;
		if (g_flash[0] || vol)
		{
			char s[140] = "";
			if (vol) { int v = kapi_sound_volume (-1, -1); snprintf (s, sizeof s, (v & 0x100) ? TR ("Volume %d (muted)") : TR ("Volume %d"), v & 0xFF); }
			else fs_copy (s, g_flash, sizeof s);
			UkFaceScope f (F (m.val));
			char b[140]; int w = uk_text_fit (s, W / 2, b, sizeof b) + D (28), h = uk_fh () + D (12);
			int x = W - t.mx - w, y = t.top + D (4);
			uk_paint_alpha (true);
			lk_shadow (cv, x, y, w, h, h / 2, D (5), P.shadow, D (3));
			lk_fill (cv, x, y, w, h, h / 2, P.fg, P.fg, 240);
			uk_paint_alpha (false);
			uk_text (cv, x + D (14), y + D (6), b, P.bg2);
		}
	}
	if (g_osk.on) draw_osk (cv, m);
	if (g_dlg.on) draw_dialog (cv, m);
	draw_set_hints (cv);
}

static void tiles_draw (Canvas &cv)
{
	g_anim = false;
	tiles_background (cv);
	g_homeHits.clear ();
	if (g_setOn) draw_settings_sw (cv);
	else tiles_home (cv);
	g_ayCol = g_xf;
	if (g_anim) g_homeDirty = true;			// (the next frame of the slide)
}

// ---- the moves ------------------------------------------------------------------------------------------------------------
static void tiles_activate (void)
{
	XCol &c = g_x[g_xf];
	int n = t_count (c), sel = t_sel (c);
	if (!n) return;
	switch (t_kind (c, sel))
	{
	case TK_ROM: play_rom (c.first + sel); break;
	case TK_ONYX: open_app_at (g_onyx[sel]); break;
	case TK_PAGE: set_enter (sel); break;
	case TK_FOLDER: sub_read (); g_inSub = true; g_subSel = g_nsub ? 1 : 0; break;
	case TK_BACK: g_inSub = false; break;
	case TK_APP: open_app_at (g_sub[sel - 1]); break;
	}
}
static void tiles_move (int d)
{
	XCol &c = g_x[g_xf];
	int n = t_count (c), v = t_sel (c) + d;
	if (v < 0) v = 0;
	if (v >= n) v = n ? n - 1 : 0;
	t_setsel (c, v);
}
static void tiles_go (int key)
{
	if (!g_nx) return;
	XCol &c = g_x[g_xf];
	if (g_trow == 1)					// the categories
		switch (key)
		{
		case KEY_LEFT: case -1: choose_col (g_xf - 1); break;
		case KEY_RIGHT: case '\t': choose_col (g_xf + 1); break;
		case KEY_UP: case KEY_ENTER: g_trow = 0; break;
		case 0x1b: case KEY_BACKSPACE: g_trow = 0; break;
		default: return;
		}
	else
		switch (key)
		{
		case KEY_LEFT: tiles_move (-1); break;
		case KEY_RIGHT: tiles_move (1); break;
		case KEY_PGUP: tiles_move (-5); break;
		case KEY_PGDN: tiles_move (5); break;
		case KEY_DOWN: g_trow = 1; break;
		case -1: choose_col (g_xf - 1); break;
		case '\t': choose_col (g_xf + 1); break;
		case KEY_ENTER: tiles_activate (); break;
		case 0x1b: case KEY_BACKSPACE:
			if (c.kind == X_APPS && g_inSub) g_inSub = false;
			else t_setsel (c, 0);
			break;
		default: return;
		}
	g_homeDirty = true;
}
// The pointer: a click on a tile chooses it (on the chosen one: opens it), on a category chooses it; the wheel moves.
static void tiles_ptr (int ev, int x, int y, int c, long v)
{
	if (ev == GUI_EVENT_PTR_WHEEL) { g_trow = 0; tiles_move (-GUI_PTR_WHEEL (v)); g_homeDirty = true; return; }
	if (!(ev == GUI_EVENT_PTR_DOWN && (c & 1))) return;
	const Hit *h = g_homeHits.at (x, y);
	if (!h || !g_nx) return;
	if (h->kind == H_CAT) { g_trow = 1; choose_col (h->i); }
	else if (h->kind == H_ITEM)
	{
		XCol &col = g_x[g_xf];
		g_trow = 0;
		if (h->i == t_sel (col)) tiles_activate ();
		else t_setsel (col, h->i);
	}
	g_homeDirty = true;
}

// ---- the menu as a panel at the right (§19.3): over the app in front, dimmed -- its icon and name, the menu's rows
// (Resume, the app's own menus, Home, the other apps, Close, Settings, Shut Down; or an app's menu's commands), the
// chosen one in the ring; at its foot the volume, the network, the time. The same items as XMB's menu (menu_build). --
static void draw_menu_sw (void)
{
	g_overDirty = false;
	const Pal &P = *g_pal;
	TMet t = tm ();
	Canvas &cv = g_oc;
	int W = g_sw, H = g_sh;
	g_overHits.clear ();
	uk_paint_alpha (true);
	for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) cv.px[(long) y * cv.stride + x] = (P.dark ? 0x60000000u : 0x78000000u) | 0x101218;	// (the app dimmed)
	int pw = D (t.c ? 300 : 500), px = W - pw - D (t.c ? 12 : 28), py = D (t.c ? 12 : 28), ph = t.bary - py - D (t.c ? 10 : 20);
	if (px < D (8)) { px = D (8); pw = W - 2 * px; }
	lk_fill (cv, px, py, pw, ph, D (22), P.panel, P.panel, 250);	// (no shadow: over the see-through dim it would blacken)
	if (!P.dark) lk_ring (cv, px, py, pw, ph, D (22), 16, P.line, 255);
	// the header: the app in front (its icon, its name) -- or the menu of the app shown
	App *front = 0; const char *title = "Onyx", *sub = TR ("The menu");
	for (int i = 0; i < g_ntasks; i++)
		if (g_tasks[i].flags & UK_TASK_FRONT) { front = find_app (g_tasks[i].name); title = front ? front->label : g_tasks[i].title[0] ? g_tasks[i].title : g_tasks[i].name; sub = front ? front->name : ""; }
	static char t2[64];
	if (g_level >= 0 && spec_menu (g_level, t2, sizeof t2)) sub = t2;
	int is = D (t.c ? 44 : 84), hx = px + D (t.c ? 14 : 24), hy = py + D (t.c ? 14 : 24);
	if (front) app_icon_at (cv, *front, hx + is / 2, hy + is / 2, is, 255);
	else { lk_fill (cv, hx, hy, is, is, D (12), P.acc, P.acc, 255); tint_icon (cv, XI_GEM, 0, hx + is / 2, hy + is / 2, is * 60 / 100, 0xFFFFFF); }
	{
		UkFaceScope f (F (t.c ? 15 : 26));
		text_fit (cv, hx + is + D (16), hy + D (t.c ? 2 : 10), pw - is - D (t.c ? 40 : 64), title, P.fg, 2);
		UkFaceScope f2 (F (t.c ? 11 : 15));
		text_fit (cv, hx + is + D (16), hy + D (t.c ? 24 : 48), pw - is - D (t.c ? 40 : 64), sub, P.sub);
	}
	// the rows
	int rx = px + D (t.c ? 8 : 12), rw = pw - D (t.c ? 16 : 24), rh = D (t.c ? 30 : 52);
	int foot = D (t.c ? 50 : 90), ry = hy + is + D (t.c ? 12 : 24), avail = py + ph - foot - ry;
	int fit = avail / rh; if (fit < 1) fit = 1;
	int first = g_msel >= fit ? g_msel - fit + 1 : 0;
	for (int i = first; i < g_nmi && ry + rh <= py + ph - foot; i++, ry += rh)
	{
		bool on = i == g_msel;
		if (i > first && !on && i - 1 != g_msel) lk_fill (cv, rx + D (16), ry, rw - D (32), D (1) > 1 ? D (1) : 1, 0, P.line, P.line, 255);
		const MItem &mi = g_mi[i];
		int lx = rx + D (20);
		if (mi.kind == M_TASK && mi.task < g_ntasks)	// another app: its icon
		{
			App *a = find_app (g_tasks[mi.task].name);
			int s = D (t.c ? 18 : 26);
			if (a) { app_icon_at (cv, *a, lx + s / 2, ry + rh / 2, s, 255); lx += s + D (12); }
		}
		UkFaceScope f (F (t.c ? 13 : 19));
		unsigned ink = mi.kind == M_POWER || mi.kind == M_CLOSE ? (P.dark ? 0xFF8A7A : 0xC43C2C) : P.fg;
		text_fit (cv, lx, ry + (rh - uk_fh ()) / 2, rx + rw - D (40) - lx, mi.label, ink, on ? 2 : 0);
		if (mi.kind == M_SETTINGS) uk_text (cv, rx + rw - D (20) - tw ("\xE2\x80\xBA"), ry + (rh - uk_fh ()) / 2, "\xE2\x80\xBA", P.sub);
		if (on) tiles_ring (cv, rx + D (6), ry + D (4), rw - D (12), rh - D (8), D (10));
		g_overHits.add (rx, ry, rw, rh, H_ITEM, i);
	}
	// the foot: the volume, the network, the time
	{
		int fy = py + ph - foot + D (t.c ? 6 : 12), fx = px + D (t.c ? 16 : 28), fw = pw - 2 * D (t.c ? 16 : 28);
		lk_fill (cv, fx, fy - D (t.c ? 4 : 10), fw, D (1) > 1 ? D (1) : 1, 0, P.line, P.line, 255);
		int v = kapi_sound_volume (-1, -1), vol = v < 0 ? 0 : v & 0xFF;
		UkFaceScope f (F (t.c ? 11 : 16));
		char vs[16]; ax_itoa (vol, vs);
		uk_text (cv, fx, fy, TR ("Volume"), P.sub);
		int bx = fx + tw (TR ("Volume")) + D (14), bw = fw - (bx - fx) - D (34), seg = 10, sg = D (3), sw = (bw - (seg - 1) * sg) / seg;
		for (int k = 0; k < seg && sw > 0; k++)
			lk_fill (cv, bx + k * (sw + sg), fy + uk_fh () / 2 - D (5), sw, D (10), D (3), k < vol && !(v & 0x100) ? P.acc : P.line, k < vol && !(v & 0x100) ? P.acc : P.line, 255);
		uk_text (cv, fx + fw - tw (vs), fy, vs, P.fg, 2);
		int y2 = fy + uk_fh () + D (t.c ? 6 : 12);
		char net[40] = "", ip[24] = "";
		if (kapi_net_status (ip, sizeof ip)) { ws_cat (net, sizeof net, TR ("Network"), ": ", ip); }
		else ws_cat (net, sizeof net, TR ("No network"));
		uk_text (cv, fx, y2, net, P.sub);
		int hh = 0, mi2 = 0; kapi_get_datetime (0, 0, 0, &hh, &mi2, 0);
		char tmx[8] = { (char) ('0' + hh / 10), (char) ('0' + hh % 10), ':', (char) ('0' + mi2 / 10), (char) ('0' + mi2 % 10), 0 };
		uk_text (cv, fx + fw - tw (tmx, 2), y2, tmx, P.fg, 2);
	}
	// the buttons, on a band at the bottom
	lk_fill (cv, 0, t.bary, W, H - t.bary, 0, P.bg2, P.bg2, 235);
	{
		const char *B[3] = { "\xE2\x96\xB2\xE2\x96\xBC", "B", "A" }, *Wd[3] = { TR ("Move"), g_level >= 0 ? TR ("Back") : TR ("Resume"), TR ("OK") };
		tiles_hints (cv, B, Wd, 3);
	}
	uk_paint_alpha (false);
	uk_win_select (W_OVER); uk_win_present (); uk_win_select (0);
}
