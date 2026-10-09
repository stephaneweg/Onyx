//
// xmb.h -- consolehome's home in the manner of Lakka's XMB (docs/COMPACT-SHELL-STUDY.md §17, the user's choice of
// 2026-10-09: "v3"; mock-ups docs/compact-shell/mockups/console-v3-*.png). Part of main.cpp (one unit).
//
// LEVEL 1, across the upper third, white icons: ONE PER CONSOLE that has games (GameKit: the installed emulators'
// `games =`, the ROMs of the watched folders; their order =), then ONYX (the native games: the apps of category Games),
// APPS (the apps' categories) and SETTINGS (the console's own settings pages: xset.h). The chosen one stays at a fixed place, big,
// its name under it; the others small and faded. Under it, LEVEL 2, its items as a vertical list -- a console's ROMs
// (alphabetical, the chosen one's title screen big at the right), Onyx's games, the applets, or Apps' categories: the
// chosen category unrolls its apps at its right (A goes into them, Left or B comes back).
// Left / Right: the category (L1 / R1 too); Up / Down: the item (L2 / R2 a page); A: play, open, enter; B: back.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//

// ---- what the home holds --------------------------------------------------------------------------------------------
enum { X_SYS, X_ONYX, X_APPS, X_SET };
struct XCol { int kind, sys, first, n, sel; };		// X_SYS: g_roms[first .. first + n)
#define XMAX		(GAMES_SYSTEMS_MAX + 4)
static XCol g_x[XMAX];
static int g_nx, g_xf;					// the columns, the chosen one
static bool g_inSub;					// Apps: the glow on the chosen category's apps
static int g_subSel;
static struct game_system g_gsys[GAMES_SYSTEMS_MAX];
static int g_ngsys;
#define MAXROMS		512
static struct game g_roms[MAXROMS];
static int g_nroms;
static unsigned g_romsT;				// when they were read
static int g_onyx[MAXAPPS], g_nonyx;			// the native games (g_apps)
static int g_acat[MAXCATS], g_nacat;			// Apps' categories (g_cats)
static int g_set[MAXAPPS], g_nset;			// the applets (g_apps)
static int g_sub[MAXAPPS], g_nsub;			// the chosen category's apps (g_apps)

static bool hidden_cat (const char *c)		// not under Apps: they have their own column, or none
{
	return ieq (c, "Recent") || ieq (c, "Settings") || ieq (c, "Games") || ieq (c, "Shell") || ieq (c, "Emulators");
}
static void sub_read (void)				// the apps of Apps' chosen category
{
	g_nsub = 0;
	int c = g_xf < g_nx && g_x[g_xf].kind == X_APPS && g_x[g_xf].sel < g_nacat ? g_acat[g_x[g_xf].sel] : -1;
	if (c < 0) return;
	for (int a = 0; a < g_napps && g_nsub < MAXAPPS; a++)
		if (!g_apps[a].hidden && !g_apps[a].applet && ieq (g_apps[a].cat, g_cats[c].name)) g_sub[g_nsub++] = a;
	if (g_subSel >= g_nsub) g_subSel = g_nsub ? g_nsub - 1 : 0;
}
static void roms_read (void)				// GameKit: the consoles, the folders, the ROMs (by console, then by name)
{
	static char dirs[GAMES_FOLDERS_MAX][GAMES_PATH];
	g_ngsys = games_systems (g_gsys, GAMES_SYSTEMS_MAX);
	int nd = games_folders (dirs, GAMES_FOLDERS_MAX);
	g_nroms = games_scan (dirs, nd, g_gsys, g_ngsys, g_roms, MAXROMS);
	g_romsT = kapi_get_ticks ();
}
// The columns made again (the apps or the ROMs changed); the chosen one kept by what it is.
static void xmb_build (void)
{
	int wasKind = g_nx ? g_x[g_xf].kind : -1, wasSys = g_nx ? g_x[g_xf].sys : -1;
	int sels[XMAX][2], nsels = 0;
	for (int i = 0; i < g_nx; i++) { sels[nsels][0] = g_x[i].kind * 100 + g_x[i].sys; sels[nsels++][1] = g_x[i].sel; }
	g_nx = 0;
	for (int s = 0; s < g_ngsys && g_nx < XMAX - 3; s++)
	{
		int first = -1, n = 0;
		for (int r = 0; r < g_nroms; r++) if (g_roms[r].sys == s) { if (first < 0) first = r; n++; }
		if (n) g_x[g_nx++] = { X_SYS, s, first, n, 0 };	// (a console with no game: not shown)
	}
	g_nonyx = g_nset = g_nacat = 0;
	for (int a = 0; a < g_napps; a++)
	{
		if (g_apps[a].hidden) continue;
		if (g_apps[a].applet) g_set[g_nset++] = a;
		else if (ieq (g_apps[a].cat, "Games") && !ieq (g_apps[a].name, "gamelib")) g_onyx[g_nonyx++] = a;	// (its ROMs are here)
	}
	for (int c = 0; c < g_ncats; c++)
	{
		if (hidden_cat (g_cats[c].name)) continue;
		bool any = false;
		for (int a = 0; a < g_napps && !any; a++) any = !g_apps[a].hidden && !g_apps[a].applet && ieq (g_apps[a].cat, g_cats[c].name);
		if (any) g_acat[g_nacat++] = c;
	}
	if (g_nonyx) g_x[g_nx++] = { X_ONYX, -1, 0, g_nonyx, 0 };
	if (g_nacat) g_x[g_nx++] = { X_APPS, -1, 0, g_nacat, 0 };
	g_x[g_nx++] = { X_SET, -1, 0, SP_N, 0 };		// (the console's settings: xset.h)
	g_xf = 0;
	for (int i = 0; i < g_nx; i++)
	{
		for (int k = 0; k < nsels; k++) if (sels[k][0] == g_x[i].kind * 100 + g_x[i].sys) g_x[i].sel = sels[k][1] < g_x[i].n ? sels[k][1] : 0;
		if (g_x[i].kind == wasKind && g_x[i].sys == wasSys) g_xf = i;
	}
	sub_read ();
	g_homeDirty = true;
}

// ---- small pieces -----------------------------------------------------------------------------------------------------
static const short SIN_Q[65] = { 0, 101, 201, 301, 401, 501, 601, 700, 799, 897, 995, 1092, 1189, 1285, 1380, 1474, 1567, 1660,
	1751, 1842, 1931, 2019, 2106, 2191, 2276, 2359, 2440, 2520, 2598, 2675, 2751, 2824, 2896, 2967, 3035, 3102, 3166, 3229,
	3290, 3349, 3406, 3461, 3513, 3564, 3612, 3659, 3703, 3745, 3784, 3822, 3857, 3889, 3920, 3948, 3973, 3996, 4017, 4036,
	4052, 4065, 4076, 4085, 4091, 4095, 4096 };
static int isin (int a)					// a: 256 a turn -> sin x 4096
{
	a &= 255;
	if (a < 64) return SIN_Q[a];
	if (a < 128) return SIN_Q[128 - a];
	if (a < 192) return -SIN_Q[a - 128];
	return -SIN_Q[256 - a];
}
static const char *sys_code (int s)			// a console's short name (its icon's word)
{
	static const char *const N[][2] = { { "Game Boy Advance", "GBA" }, { "Game Boy Color", "GBC" }, { "Game Boy", "GB" },
		{ "Super Nintendo", "SNES" }, { "NES", "NES" }, { "Nintendo 64", "N64" }, { "GameCube", "GC" } };
	for (unsigned i = 0; i < sizeof N / sizeof N[0]; i++) if (ieq (g_gsys[s].name, N[i][0])) return N[i][1];
	static char c[8]; int k = 0;
	for (const char *p = g_gsys[s].name; *p && k < 4; p++) if (*p >= 'A' && *p <= 'Z') c[k++] = *p;	// "Master System" -> "MS"
	if (k == 0) { c[0] = g_gsys[s].name[0]; k = 1; }
	c[k] = 0;
	return c;
}
static FtTextFace *Fpx (int px) { int lp = (px * 100 + S / 2) / S; return F (lp < 6 ? 6 : lp > 39 ? 39 : lp); }	// a face near px pixels
static unsigned fade (int a) { return uk_mix (0x285096, 0xFFFFFF, a); }	// a word at opacity a over the blue

// ---- the metrics: logical units (x the scale), a compact set under 560 logical lines (640 x 480) -------------------------
struct XMet { bool c; int tx, ty, title, count, CY, FX, big, small, spr, spl, catlab, FY, fi, oi, flab, olab, sub, gap, dy, above, lx, thr, thy, thw, bar, SX; };
static XMet xm (void)
{
	XMet m;
	m.c = g_sh * 100 / S < 560;
	bool c = m.c;
	int lw = g_sw * 100 / S;				// the logical width
	m.tx = D (c ? 16 : 40); m.ty = D (c ? 10 : 20); m.title = c ? 16 : 24; m.count = c ? 11 : 15;
	m.CY = D (c ? 92 : 176); m.FX = D (c ? 104 : 250);
	m.big = D (c ? 50 : 88); m.small = D (c ? 30 : 52); m.spr = D (c ? 78 : 146); m.spl = D (c ? 72 : 136); m.catlab = c ? 11 : 15;
	m.FY = D (c ? 184 : 322); m.fi = D (c ? 34 : 56); m.oi = D (c ? 24 : 40);
	m.flab = c ? 16 : 25; m.olab = c ? 13 : 19; m.sub = c ? 10 : 15;
	m.gap = D (c ? 50 : 80); m.dy = D (c ? 36 : 58); m.above = D (c ? 60 : 100); m.lx = D (c ? 30 : 50);
	m.thw = D (c ? 220 : 430); m.thr = D (c ? 16 : 58); m.thy = D (c ? 136 : 248);	// the picture: its width, its right margin
	m.bar = D (c ? 28 : 38);
	m.SX = D (c ? (lw < 700 ? 300 : 330) : 600);	// Apps: the apps' icons, at the categories' right
	return m;
}

// ---- the background: Lakka's calm blue gradient and its soft white ribbon (made once for a size) -------------------------
static void xmb_bg_make (int w, int h)
{
	delete [] g_bgPx;
	g_bgPx = new unsigned[w * h]; g_bgW = w; g_bgH = h;
	Canvas cv; cv.adopt (g_bgPx, w, h, w);
	const unsigned A = 0x0A1A4A, B = 0x1A56A8, C = 0x3C8CD6;
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
		{
			int t = (int) ((long) x * 5500 / w + (long) y * 4500 / h);	// 0..10000
			g_bgPx[y * w + x] = t < 6000 ? uk_mix (A, B, t * 256 / 6000) : uk_mix (B, C, (t - 6000) * 256 / 4000);
		}
	// a soft light at the top left
	int lx = w * 15 / 100, ly = -h * 5 / 100, rx = w * 45 / 100, ry = h * 55 / 100;
	for (int y = 0; y < h * 55 / 100; y++)
		for (int x = 0; x < w * 60 / 100; x++)
		{
			long dx = (long) (x - lx) * 256 / rx, dy = (long) (y - ly) * 256 / ry, d = dx * dx + dy * dy;	// 65536: the edge
			if (d < 65536) lk_px (cv, x, y, 0xFFFFFF, (int) (26 * (65536 - d) / 65536));
		}
	// the ribbon: a faint band between two waves, its threads brighter (the waves in 1/16 px)
	int F = D (6) > 1 ? D (6) : 1, F16 = F * 16;
	for (int x = 0; x < w; x++)
	{
		int a1 = (int) ((long) x * 256 * 115 / 100 / w) + 16, a2 = (int) ((long) x * 256 * 124 / 100 / w) + 110;
		int y1 = (h * 60 / 100) + (int) ((long) h * 10 / 100 * isin (a1) / 4096);
		int y2 = (h * 64 / 100) + (int) ((long) h * 8 / 100 * isin (a2) / 4096);
		int t16 = (int) ((long) h * 960 / 100 + (long) h * 160 / 100 * isin (a1) / 4096);	// (the same, in 1/16 px)
		int b16 = (int) ((long) h * 1024 / 100 + (long) h * 128 / 100 * isin (a2) / 4096);
		if (t16 > b16) { int q = t16; t16 = b16; b16 = q; }
		for (int y = (t16 - F16) / 16 - 1; y <= (b16 + F16) / 16 + 1; y++)
		{
			int c16 = y * 16 + 8, in = c16 < t16 ? c16 - (t16 - F16) : c16 > b16 ? b16 + F16 - c16 : F16;	// a soft edge, F px
			if (in > 0 && y >= 0 && y < h) lk_px (cv, x, y, 0xFFFFFF, 34 * (in > F16 ? F16 : in) / F16);
		}
		for (int i = 0; i < 14; i++)				// the threads: between the two waves, a ripple each
		{
			int yy16 = ((y1 * (13 - i) + y2 * i) * 16) / 13 + D (6) * isin ((int) ((long) x * 2304000 / ((long) w * 6283)) + i * 41) * 16 / 4096;
			int yi = yy16 >> 4, fr = yy16 & 15, al = i % 3 ? 46 : 70;
			if (yi >= 0 && yi + 1 < h) { lk_px (cv, x, yi, 0xFFFFFF, al * (16 - fr) / 16); lk_px (cv, x, yi + 1, 0xFFFFFF, al * fr / 16); }
		}
	}
	// the foot's band (the hints' bar), made here once: darker, a faint line on top
	int y0 = h - xm ().bar;
	for (int y = y0; y < h; y++) for (int x = 0; x < w; x++) lk_px (cv, x, y, 0x040A1E, 140);
	for (int x = 0; x < w; x++) lk_px (cv, x, y0, 0xFFFFFF, 40);
}

// ---- the white icons: drawn into a mask (white on black, through look.h's shapes), then laid in white -----------------
enum { XI_CONSOLE, XI_GEM, XI_GRID, XI_GEAR, XI_FOLDER };
struct XIcon { int kind, size; char code[8]; unsigned char *m; };
static XIcon g_xic[32];
static int g_nxic;
static void xi_box (Canvas &c, int s, int x, int y, int w, int h, int r, bool on)	// in 1/64 of the icon (x 4: quarter units)
{
	int X = x * s / 256, Y = y * s / 256, W = (x + w) * s / 256 - X, H = (y + h) * s / 256 - Y, R = r * s / 256;
	if (W < 1) W = 1;
	if (H < 1) H = 1;
	unsigned v = on ? 0xFFFFFF : 0;
	lk_fill (c, X, Y, W, H, R, v, v, 255);
}
static void xi_dot (Canvas &c, int s, int cx, int cy, int r) { xi_box (c, s, cx - r, cy - r, 2 * r, 2 * r, r, false); }
static void xi_word (Canvas &c, int s, int cx, int cy, const char *w, bool on)	// (quarter units) the console's word
{
	UkFaceScope f (Fpx ((s * (strlen (w) < 4 ? 11 : 9) + 32) / 64));
	uk_text (c, cx * s / 256 - tw (w, 2) / 2, cy * s / 256 - uk_fh () / 2, w, on ? 0xFFFFFF : 0, 2);
}
static const unsigned char *xi_mask (int kind, const char *code, int s)
{
	for (int i = 0; i < g_nxic; i++)
		if (g_xic[i].kind == kind && g_xic[i].size == s && (kind != XI_CONSOLE || !strcmp (g_xic[i].code, code))) return g_xic[i].m;
	unsigned *px = new unsigned[s * s];
	for (int i = 0; i < s * s; i++) px[i] = 0;
	Canvas c; c.adopt (px, s, s, s);
	uk_paint_alpha (false);
	if (kind == XI_CONSOLE)				// a console: a generic shape (no maker's design), its short name in it
	{
		if (!strcmp (code, "GB") || !strcmp (code, "GBC"))		// an upright handheld
		{
			xi_box (c, s, 64, 16, 128, 224, 24, true); xi_box (c, s, 80, 36, 96, 88, 12, false); xi_word (c, s, 128, 80, code, true);
			xi_box (c, s, 84, 168, 40, 12, 2, false); xi_box (c, s, 98, 154, 12, 40, 2, false); xi_dot (c, s, 156, 180, 10); xi_dot (c, s, 176, 164, 10);
		}
		else if (!strcmp (code, "GBA"))					// a wide handheld
		{
			xi_box (c, s, 8, 64, 240, 128, 48, true); xi_box (c, s, 76, 80, 104, 96, 12, false); xi_word (c, s, 128, 128, code, true);
			xi_box (c, s, 24, 122, 36, 12, 2, false); xi_box (c, s, 36, 110, 12, 36, 2, false); xi_dot (c, s, 204, 136, 10); xi_dot (c, s, 224, 116, 10);
		}
		else if (!strcmp (code, "GC"))					// a disc
		{
			xi_box (c, s, 16, 16, 224, 224, 112, true); xi_dot (c, s, 128, 128, 24); xi_word (c, s, 128, 192, code, false);
		}
		else if (!strcmp (code, "N64"))					// a cartridge with a label
		{
			xi_box (c, s, 32, 24, 192, 208, 16, true); xi_box (c, s, 56, 48, 144, 112, 12, false); xi_word (c, s, 128, 104, code, true);
			for (int i = 0; i < 5; i++) xi_box (c, s, 72 + i * 24, 184, 8, 32, 2, false);
		}
		else								// a flat controller (NES, Super Nintendo, the others)
		{
			xi_box (c, s, 8, 72, 240, 112, !strcmp (code, "NES") ? 12 : 56, true);
			xi_box (c, s, 36, 122, 48, 12, 2, false); xi_box (c, s, 54, 104, 12, 48, 2, false);
			if (!strcmp (code, "NES")) { xi_dot (c, s, 188, 132, 13); xi_dot (c, s, 220, 132, 13); }
			else { xi_dot (c, s, 200, 104, 10); xi_dot (c, s, 224, 128, 10); xi_dot (c, s, 200, 152, 10); xi_dot (c, s, 176, 128, 10); }
			xi_word (c, s, 128, 160, code, false);
		}
	}
	else if (kind == XI_GEM)					// Onyx: the cut stone
	{
		int r = s * 44 / 100, cx = s / 2, cy = s / 2;
		for (int j = -r; j <= r; j++) for (int i = -r; i <= r; i++)
		{
			int d = (i < 0 ? -i : i) + (j < 0 ? -j : j);
			if (d <= r) c.pixel (cx + i, cy + j, d >= r - 1 ? 0xC0C0C0 : 0xFFFFFF);
		}
		xi_box (c, s, 60, 116, 136, 10, 2, false); xi_box (c, s, 124, 40, 8, 80, 2, false);
	}
	else if (kind == XI_GRID)					// Apps: nine squares
		for (int q = 0; q < 9; q++) xi_box (c, s, 36 + (q % 3) * 64, 36 + (q / 3) * 64, 48, 48, 12, true);
	else if (kind == XI_GEAR)					// Settings: a wheel and its teeth
	{
		xi_box (c, s, 56, 56, 144, 144, 72, true);
		for (int k = 0; k < 8; k++)
		{
			int a = k * 32, cx = 128 + 88 * isin (a + 64) / 4096, cy = 128 + 88 * isin (a) / 4096;
			xi_box (c, s, cx - 20, cy - 20, 40, 40, 8, true);
		}
		xi_dot (c, s, 128, 128, 30);
	}
	else								// a category of apps: a folder
	{
		xi_box (c, s, 24, 52, 92, 40, 12, true); xi_box (c, s, 24, 76, 208, 136, 16, true); xi_box (c, s, 24, 92, 208, 6, 2, false);
	}
	unsigned char *m = new unsigned char[s * s];
	for (int i = 0; i < s * s; i++) m[i] = (unsigned char) (px[i] & 255);
	delete [] px;
	XIcon &e = g_xic[g_nxic < 32 ? g_nxic++ : (g_nxic = 1, 0)];
	if (e.m) delete [] e.m;
	e.kind = kind; e.size = s; e.m = m; e.code[0] = 0;
	if (code) { int k = 0; for (; code[k] && k < 7; k++) e.code[k] = code[k]; e.code[k] = 0; }
	return m;
}
// The icon centred at cx, cy, s pixels, at opacity a (a faint shadow under it).
static void white_icon (Canvas &cv, int kind, const char *code, int cx, int cy, int s, int a)
{
	if (s < 4) return;
	const unsigned char *m = xi_mask (kind, code, s);
	int x0 = cx - s / 2, y0 = cy - s / 2, sh = s / 40 > 0 ? s / 40 : 1;
	if (a > 200)						// (a faint shadow under the chosen one only)
		for (int j = 0; j < s; j++)
			for (int i = 0; i < s; i++)
			{
				int v = m[j * s + i];
				if (v) lk_px (cv, x0 + i, y0 + j + sh, 0x000A28, v * a / 255 * 35 / 100);
			}
	for (int j = 0; j < s; j++)
		for (int i = 0; i < s; i++)
		{
			int v = m[j * s + i];
			if (v) lk_px (cv, x0 + i, y0 + j, 0xFFFFFF, v * a / 255);
		}
}
// An app's icon centred at cx, cy, s pixels, at opacity a.
static void app_icon_at (Canvas &cv, App &ap, int cx, int cy, int s, int a)
{
	app_icon (ap, s);
	if (ap.scaled == 0) return;
	int x0 = cx - s / 2, y0 = cy - s / 2;
	for (int j = 0; j < s; j++)
		for (int i = 0; i < s; i++)
		{
			unsigned c = ap.scaled[j * s + i];
			int t = 255 - (int) (c >> 24);
			if (t > 0) lk_px (cv, x0 + i, y0 + j, c & 0xFFFFFF, t * a / 255);
		}
}

// ---- what a column shows --------------------------------------------------------------------------------------------
static const char *col_name (const XCol &c)
{
	return c.kind == X_SYS ? g_gsys[c.sys].name : c.kind == X_ONYX ? "Onyx" : c.kind == X_APPS ? TR ("Apps") : TR ("Settings");
}
static void col_icon (Canvas &cv, const XCol &c, int cx, int cy, int s, int a)
{
	if (c.kind == X_SYS) white_icon (cv, XI_CONSOLE, sys_code (c.sys), cx, cy, s, a);
	else white_icon (cv, c.kind == X_ONYX ? XI_GEM : c.kind == X_APPS ? XI_GRID : XI_GEAR, 0, cx, cy, s, a);
}
// Item i of column c: its icon, its words (the label; sub: the chosen one's line under it).
static void item_icon (Canvas &cv, const XCol &c, int i, int cx, int cy, int s, int a)
{
	if (c.kind == X_SYS) white_icon (cv, XI_CONSOLE, sys_code (c.sys), cx, cy, s, a);
	else if (c.kind == X_APPS) white_icon (cv, XI_FOLDER, 0, cx, cy, s, a);
	else if (c.kind == X_SET) page_icon (cv, i, cx, cy, s, a);
	else app_icon_at (cv, g_apps[g_onyx[i]], cx, cy, s, a);
}
static const char *item_label (const XCol &c, int i)
{
	if (c.kind == X_SYS) return g_roms[c.first + i].name;
	if (c.kind == X_APPS) return TR (g_cats[g_acat[i]].name);
	if (c.kind == X_SET) return sp_name (i);
	return g_apps[g_onyx[i]].label;
}
static void item_sub (const XCol &c, int i, char *o, int cap)
{
	o[0] = 0;
	int k = 0;
	if (c.kind == X_SYS)
	{
		const char *p = g_roms[c.first + i].path, *f = p;
		for (const char *q = p; *q; q++) if (*q == '/') f = q + 1;
		lx_cat (o, cap, &k, f); lx_cat (o, cap, &k, "  -  "); lx_cat (o, cap, &k, g_gsys[c.sys].name);
	}
	else if (c.kind == X_APPS) { int n = 0; for (int a = 0; a < g_napps; a++) if (!g_apps[a].hidden && !g_apps[a].applet && ieq (g_apps[a].cat, g_cats[g_acat[i]].name)) n++; snprintf (o, (size_t) cap, n == 1 ? TR ("%d app") : TR ("%d apps"), n); }
	else if (c.kind == X_ONYX) lx_cat (o, cap, &k, TR ("Onyx game"));
	else lx_cat (o, cap, &k, sp_help (i));
}

// ---- a ROM's picture (GameKit: the Game Library's title screens) ---------------------------------------------------------
static unsigned g_thumb[GAMES_THUMB_W * GAMES_THUMB_H];
static int g_thumbOf = -1;				// the ROM it is (g_roms), -2 none made yet
static unsigned g_thumbGen;				// (+1 at each picture read: the scaled copy follows)
static bool thumb_for (int r)
{
	if (g_thumbOf == r) return true;
	if (games_thumb_load (&g_roms[r], g_thumb)) { g_thumbOf = r; g_thumbGen++; return true; }
	return false;
}

// ---- the home drawn --------------------------------------------------------------------------------------------------
// A vertical list: item f at FY (big, white, its line under it), the ones after it below, the ones before it above the
// category row (faded); its icons centred at cx, its words from cx + lx, w wide. dim: the whole list fainter.
static void draw_list (Canvas &cv, const XMet &m, const XCol &c, int n, int f, int cx, int w, bool focus, bool dim, bool sub)
{
	int H = g_sh;
	for (int j = 0; j < n; j++)
	{
		int y, s, a, lp;
		if (j == f) { y = m.FY; s = m.fi; a = focus ? 255 : dim ? 130 : 200; lp = m.flab; }
		else if (j > f) { y = m.FY + m.gap + (j - f - 1) * m.dy; s = m.oi; a = dim ? 90 : 150; lp = m.olab; if (y > H - m.bar - D (14)) break; }
		else
		{
			if (sub) { y = m.FY - m.gap - (f - j - 1) * m.dy; a = (dim ? 90 : 150) - 25 * (f - j - 1); }
			else { y = m.CY - m.above - (f - j - 1) * m.dy; a = 90 - 30 * (f - j - 1); }
			s = m.oi; lp = m.olab;
			if (y < m.ty + D (m.title) + D (24) || a <= 0) continue;
		}
		if (sub) app_icon_at (cv, g_apps[g_sub[j]], cx, y, s, a);
		else item_icon (cv, c, j, cx, y, s, a);
		const char *lab = sub ? g_apps[g_sub[j]].label : item_label (c, j);
		char line[160] = "";
		if (j == f && !sub) item_sub (c, j, line, sizeof line);
		if (j == f && sub) { int k = 0; lx_cat (line, sizeof line, &k, g_apps[g_sub[j]].name); }
		{
			UkFaceScope fc (F (lp));
			int ly = line[0] && j == f ? y - D (14) - uk_fh () / 2 + D (2) : y - uk_fh () / 2;
			text_fit (cv, cx + m.lx, ly, w, lab, fade (a), j == f ? 2 : 0);
			if (j == f && line[0]) { UkFaceScope f2 (F (m.sub)); text_fit (cv, cx + m.lx + D (1), y + D (8), w, line, focus ? 0xD2E0F8 : fade (a * 3 / 4)); }
		}
		g_homeHits.add (cx - s / 2, y - m.dy / 2, w + m.lx + s / 2, m.dy, sub ? H_SUB : H_ITEM, j);
	}
}
static void draw_home (void)
{
	unsigned t0 = kapi_clock_us ();
	g_homeDirty = false;
	Canvas &cv = g_hc;
	int W = g_sw, H = g_sh;
	if (g_bgPx == 0 || g_bgW != W || g_bgH != H) xmb_bg_make (W, H);
	for (int y = 0; y < H; y++) memcpy (cv.px + (long) y * cv.stride, g_bgPx + (long) y * W, (size_t) W * 4);
	g_homeHits.clear ();
	uk_paint_alpha (false);
	XMet m = xm ();
	if (g_setOn)						// a settings page (xset.h)
	{
		draw_settings (cv);
		uk_win_select (W_HOME); uk_win_present (); uk_win_select (0);
		return;
	}
	const XCol *c = g_nx ? &g_x[g_xf] : 0;

	// the top: the column's name and its count; the day and the time
	{
		int n = c ? c->n : 0;
		char cnt[32];
		snprintf (cnt, sizeof cnt, c && c->kind == X_SYS ? (n == 1 ? TR ("%d game") : TR ("%d games")) : (n == 1 ? TR ("%d item") : TR ("%d items")), n);
		UkFaceScope f (F (m.title));
		const char *t = c ? col_name (*c) : "Onyx";
		uk_text (cv, m.tx, m.ty, t, 0xFFFFFF, 2);
		int x = m.tx + tw (t, 2) + D (14);
		int hh = 0, mi = 0, dd = 0, mo = 0;
		kapi_get_datetime (0, &mo, &dd, &hh, &mi, 0);
		char tm[8] = { (char) ('0' + hh / 10), (char) ('0' + hh % 10), ':', (char) ('0' + mi / 10), (char) ('0' + mi % 10), 0 };
		uk_text (cv, W - m.tx - tw (tm), m.ty, tm, 0xFFFFFF);
		int tmw = tw (tm);
		UkFaceScope f2 (F (m.count));
		if (c) uk_text (cv, x, m.ty + D (m.title - m.count) * 3 / 4, cnt, 0xC8D6F0);
		if (!m.c && mo >= 1 && mo <= 12)
		{
			static const char *const MON[12] = { TRN ("Jan"), TRN ("Feb"), TRN ("Mar"), TRN ("Apr"), TRN ("May"), TRN ("Jun"), TRN ("Jul"), TRN ("Aug"), TRN ("Sep"), TRN ("Oct"), TRN ("Nov"), TRN ("Dec") };
			char ds[24]; snprintf (ds, sizeof ds, "%d %s", dd, TR (MON[mo - 1]));
			uk_text (cv, W - m.tx - tmw - D (16) - tw (ds), m.ty + D (m.title - m.count) * 3 / 4, ds, 0xC8D6F0);
		}
	}
	if (!c)
	{
		UkFaceScope f (F (m.olab));
		text_cfit (cv, 0, H / 2 - D (10), W, TR ("Nothing here yet"), C_DIM);
		uk_win_select (W_HOME); uk_win_present (); uk_win_select (0);
		return;
	}

	// level 1: the columns across, the chosen one at FX (hidden behind Apps' sub-level? no: always shown)
	for (int i = 0; i < g_nx; i++)
	{
		int x;
		if (i == g_xf) x = m.FX;
		else if (i > g_xf) x = m.FX + m.big / 2 + (m.spr - m.big / 2) + (i - g_xf - 1) * m.spr;
		else x = m.FX - (g_xf - i) * m.spl;
		int s = i == g_xf ? m.big : m.small;
		if (x < -s || x > W + s) continue;
		col_icon (cv, g_x[i], x, m.CY, s, i == g_xf ? 255 : 110);
		g_homeHits.add (x - m.spl / 2, m.CY - m.big / 2, m.spl, m.big, H_CAT, i);
	}
	{
		UkFaceScope f (F (m.catlab));
		text_cfit (cv, m.FX - D (120), m.CY + m.big / 2 + D (6), D (240), col_name (*c), 0xFFFFFF, 2);
	}

	// level 2: the chosen column's items
	int thx = W - m.thr - m.thw;				// the picture's place at the right
	if (c->kind == X_APPS)
	{
		draw_list (cv, m, *c, c->n, c->sel, m.FX, m.SX - m.FX - m.lx - D (40), !g_inSub, g_inSub, false);
		if (g_nsub) draw_list (cv, m, *c, g_nsub, g_subSel, m.SX, W - m.SX - m.lx - m.tx, g_inSub, !g_inSub, true);
	}
	else
	{
		int w = (c->kind == X_SYS ? thx - D (24) : W - m.tx) - (m.FX + m.lx);
		draw_list (cv, m, *c, c->n, c->sel, m.FX, w, true, false, false);
	}
	if (c->kind == X_SYS)					// the ROM's title screen, big at the right
	{
		int r = c->first + c->sel;
		int pw = m.thw, ph = pw * GAMES_THUMB_H / GAMES_THUMB_W;
		if (thx > m.FX + m.lx + D (120))
		{
			if (thumb_for (r))
			{
				static unsigned *s_big, s_gen; static int s_w, s_h;	// (scaled once for a picture, a size)
				if (s_gen != g_thumbGen || s_w != pw || s_h != ph || s_big == 0)
				{
					delete [] s_big;
					s_big = new unsigned[pw * ph]; s_gen = g_thumbGen; s_w = pw; s_h = ph;
					for (int y = 0; y < ph; y++)
					{
						const unsigned *s = g_thumb + (y * GAMES_THUMB_H / ph) * GAMES_THUMB_W;
						for (int x = 0; x < pw; x++) s_big[y * pw + x] = s[x * GAMES_THUMB_W / pw];
					}
				}
				uk_paint_alpha (true);				// (a soft drop under it: two dark offsets, cheap)
				lk_fill (cv, thx + D (3), m.thy + D (8), pw, ph, D (6), 0x000A28, 0x000A28, 60);
				lk_fill (cv, thx + D (1), m.thy + D (4), pw, ph, D (3), 0x000A28, 0x000A28, 70);
				uk_paint_alpha (false);
				for (int y = 0; y < ph && m.thy + y < H; y++)
					memcpy (cv.px + (long) (m.thy + y) * cv.stride + thx, s_big + y * pw, (size_t) (thx + pw <= W ? pw : W - thx) * 4);
			}
			else						// no picture yet: the console, and where the pictures come from
			{
				uk_paint_alpha (true);
				lk_fill (cv, thx, m.thy, pw, ph, D (10), 0x10285C, 0x0C1E48, 150);
				lk_ring (cv, thx, m.thy, pw, ph, D (10), 16, 0x8CB4EC, 120);
				uk_paint_alpha (false);
				white_icon (cv, XI_CONSOLE, sys_code (c->sys), thx + pw / 2, m.thy + ph * 2 / 5, pw * 2 / 5, 170);
				UkFaceScope f (F (m.sub));
				text_cfit (cv, thx, m.thy + ph * 3 / 4, pw, TR ("No picture yet: the Game Library makes them"), 0xC8D6F0);
			}
		}
	}

	// the foot: where we are, the buttons that work (right-aligned)
	{
		int y0 = H - m.bar;					// (its band: in the background, xmb_bg_make)
		const char *ok = c->kind == X_SYS || c->kind == X_ONYX ? TR ("Play") : c->kind == X_APPS && !g_inSub ? TR ("Enter") : TR ("Open");
		struct { const char *b; unsigned ring; const char *w; } hs[] = {
			{ "L1 R1", 0x9AA8C0, TR ("Category") }, { "Home", 0x9AA8C0, TR ("Menu") }, { "B", 0xFF6A6A, TR ("Back") }, { "A", 0x5E9CFF, ok } };
		int n = (int) (sizeof hs / sizeof hs[0]), first = m.c ? 1 : 0, tot = 0;
		for (int i = first; i < n; i++) tot += hint_w (hs[i].b, hs[i].w);
		int hx = W - m.tx - tot + D (22), hy = y0 + (m.bar - (uk_fh () + D (8))) / 2;
		{ UkFaceScope f (F (12)); hy = y0 + (m.bar - (uk_fh () + D (8))) / 2; }
		for (int i = first; i < n; i++) hx += hint (cv, hx, hy, hs[i].b, hs[i].ring, hs[i].w);
		if (!m.c)
		{
			char l[96]; int k = 0;
			lx_cat (l, sizeof l, &k, "Onyx  -  "); lx_cat (l, sizeof l, &k, col_name (*c));
			if (c->kind == X_SYS) { lx_cat (l, sizeof l, &k, " ("); lx_cat (l, sizeof l, &k, g_gsys[c->sys].emu); lx_cat (l, sizeof l, &k, ")"); }
			UkFaceScope f (F (12));
			text_fit (cv, m.tx, y0 + (m.bar - uk_fh ()) / 2, W - m.tx - tot - D (40), l, 0xC8D6F0);
		}
	}
	unsigned t1 = kapi_clock_us ();
	uk_win_select (W_HOME); uk_win_present (); uk_win_select (0);
	unsigned t2 = kapi_clock_us ();
	static unsigned s_said;
	if (t2 - t0 > 40000 && kapi_get_ticks () - s_said > 1000)	// (slow: said, at most every 10 s -- kmsg)
	{
		s_said = kapi_get_ticks ();
		char m[96]; int k = 0;
		lx_cat (m, sizeof m, &k, "consolehome: the home drawn in "); num_cat (m, sizeof m, &k, (int) ((t1 - t0) / 1000));
		lx_cat (m, sizeof m, &k, " ms, shown in "); num_cat (m, sizeof m, &k, (int) ((t2 - t1) / 1000)); lx_cat (m, sizeof m, &k, " ms");
		ax_putln (m);
	}
	if (getenv ("XMB_TIME")) { char m[64]; snprintf (m, sizeof m, "XMBTIME draw %u us present %u us", t1 - t0, t2 - t1); ax_putln (m); }
}

// ---- the home's moves ---------------------------------------------------------------------------------------------------
static void play_rom (int r)				// a ROM: its emulator, at the emulator's own screen size (screen_follow)
{
	const char *emu = g_gsys[g_roms[r].sys].emu;
	int w, h;
	tasks_read ();
	if (task_of (emu) < 0 && screen_of (emu, &w, &h)) { fs_copy (g_launch, emu, sizeof g_launch); g_launchT = kapi_get_ticks (); screen_to (w, h, emu); }
	lx_open (g_roms[r].path, "");
}
static void open_app_at (int a)
{
	const char *name = g_apps[a].name;
	int w, h;
	tasks_read ();
	if (task_of (name) < 0 && screen_of (name, &w, &h)) { fs_copy (g_launch, name, sizeof g_launch); g_launchT = kapi_get_ticks (); screen_to (w, h, name); }
	open_app (name);
}
static void activate (void)
{
	if (!g_nx) return;
	XCol &c = g_x[g_xf];
	if (c.n == 0) return;
	if (c.kind == X_SYS) play_rom (c.first + c.sel);
	else if (c.kind == X_ONYX) open_app_at (g_onyx[c.sel]);
	else if (c.kind == X_SET) set_enter (c.sel);
	else if (!g_inSub) { if (g_nsub) { g_inSub = true; g_subSel = 0; } }
	else if (g_subSel < g_nsub) open_app_at (g_sub[g_subSel]);
}
static void choose_col (int i)
{
	if (g_nx == 0) return;
	if (i < 0) i = 0;
	if (i >= g_nx) i = g_nx - 1;
	if (i != g_xf) { g_xf = i; g_inSub = false; g_subSel = 0; sub_read (); }
	g_homeDirty = true;
}
static void move_item (int d)
{
	if (!g_nx) return;
	XCol &c = g_x[g_xf];
	if (g_inSub) { g_subSel += d; if (g_subSel < 0) g_subSel = 0; if (g_subSel >= g_nsub) g_subSel = g_nsub ? g_nsub - 1 : 0; }
	else
	{
		int was = c.sel;
		c.sel += d;
		if (c.sel < 0) c.sel = 0;
		if (c.sel >= c.n) c.sel = c.n ? c.n - 1 : 0;
		if (c.kind == X_APPS && c.sel != was) { g_subSel = 0; sub_read (); }
	}
	g_homeDirty = true;
}
static void go (int key)
{
	if (!g_nx) return;
	XCol &c = g_x[g_xf];
	switch (key)
	{
	case KEY_UP: move_item (-1); break;
	case KEY_DOWN: move_item (1); break;
	case KEY_PGUP: move_item (-8); break;
	case KEY_PGDN: move_item (8); break;
	case KEY_LEFT: if (g_inSub) { g_inSub = false; g_homeDirty = true; } else choose_col (g_xf - 1); break;
	case KEY_RIGHT: if (!g_inSub) choose_col (g_xf + 1); break;	// (Apps' apps: A goes into them)
	case '\t': choose_col (g_xf + 1); break;
	case -1: choose_col (g_xf - 1); break;
	case KEY_ENTER: activate (); g_homeDirty = true; break;
	case 0x1b: case KEY_BACKSPACE:
		if (g_inSub) g_inSub = false;
		else c.sel = 0;
		g_homeDirty = true;
		break;
	default: return;
	}
}
// The pointer: a move brings the glow (an item, Apps' apps), a click on a column chooses it, on the chosen item opens it;
// the wheel moves the items.
static void home_ptr (int ev, int x, int y, int c, long v)
{
	const Hit *h = g_homeHits.at (x, y);
	if (ev == GUI_EVENT_PTR_DOWN && (c & 1) && h)
	{
		if (h->kind == H_CAT) choose_col (h->i);
		else if (h->kind == H_ITEM && g_nx)
		{
			XCol &col = g_x[g_xf];
			if (h->i == col.sel && !g_inSub) activate ();
			else { g_inSub = false; move_item (h->i - col.sel); }
		}
		else if (h->kind == H_SUB)
		{
			if (g_inSub && h->i == g_subSel) activate ();
			else { g_inSub = true; g_subSel = h->i; }
		}
		g_homeDirty = true;
	}
	if (ev == GUI_EVENT_PTR_WHEEL) move_item (-GUI_PTR_WHEEL (v));
}
