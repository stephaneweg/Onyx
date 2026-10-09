//
// consolehome -- the console mode's shell (PocketUI's console mode: the games, a television, a pad; phase P9 of
// docs/POCKETUI-TECH-STUDY.md, its look docs/COMPACT-SHELL-STUDY.md section 7: the mood of the PlayStation 2's
// browser -- a deep blue space with soft motes and towers of cubes, big thin words, a glow on the chosen item).
//
// THE HOME (its backmost window, the whole screen: console mode has no band): at the left the CATEGORIES of the card's
// apps (Recent, Games, Productivity... Settings: pocketshell's catalogue -- their app.txt, the dock's order, the
// Control Panel's applets), the chosen one glowing with a line about it; at the right, in a glass panel, the APPS of
// that category as tiles. THE MENU (a topmost overlay, asked with the pad's Home or Select, F10, the Super key, or the
// shell's message): the app in front, Home, the other running apps (to switch to), Close this app, Settings, Shut
// Down -- over any app, since an app in console mode fills the screen with no chrome.
//
// The pad first (gamepad.h): the d-pad moves, A enters, B goes back, L1 / R1 the previous / next category, L2 / R2 a
// page of tiles, Home or Select the menu. The keyboard the same: the arrows, Enter, Esc or Backspace, Page Up / Down
// (a page), Ctrl+Page Up / Down or Tab (a category), F10 the menu. The mouse: a move brings the glow, a click
// enters, the wheel scrolls. The bottom line says the buttons that work.
//
// It is PocketUI's shell (uk_shell_register): its keys come before the front app's (uk_shell_keys), every key while the
// menu is up (uk_shell_grab). Resolution independent: every size in logical units times a scale (SD:/etc/theme.txt
// "scale =", else 1.5 from a 1080-line screen up, 2 from 1800).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#include <string.h>
#include "appkit/appkit.h"
#include "systemkit/systemkit.h"
#include "filekit/filekit.h"
#include "uikit/uikit.h"
#include "uikit/bmp.h"
#include "fontkit/uikitface.h"
#include "gamepad.h"

using namespace uikit;

#include "Apps/pocketshell/catalog.h"		// the apps, the categories, the recent ones, the running ones
#include "Apps/pocketshell/look.h"		// rounded boxes of any radius, rings, shadows

// ---- the screen, the scale ------------------------------------------------------------------------------
static int g_sw = 640, g_sh = 480;
static int S = 100;					// the scale, percent
static int D (int lp)		{ return (lp * S + 50) / 100; }
static FtTextFace *g_faces[40];
static FtTextFace *F (int lp)
{
	if (lp < 6 || lp > 39) return 0;
	if (g_faces[lp] == 0)
	{
		g_faces[lp] = new FtTextFace;
		if (!g_faces[lp]->open ("DejaVu Sans", D (lp))) { delete g_faces[lp]; g_faces[lp] = 0; }
	}
	return g_faces[lp];
}

enum { W_HOME = 0, W_OVER = 1, W_TIP = 2 };
static Canvas g_hc, g_oc;
static int g_homeStride, g_overStride;
static bool g_homeDirty = true, g_overDirty;
static bool g_menu;					// the menu's overlay is up
#define CLEAR		0xFF000000u			// a see-through pixel (WIN_FLAG_ALPHA)

static const unsigned C_GLOW = 0x60C8FF, C_WORD = 0xE4ECF8, C_DIM = 0x7C94B8, C_GLASS1 = 0x1C3158, C_GLASS2 = 0x14244A;

// The words given through tables or files (tools/lang/check.py reads these):
// TR: Recent
// TR: Productivity
// TR: Internet
// TR: Graphics
// TR: Multimedia
// TR: Games
// TR: Programming
// TR: System
// TR: Demos
// TR: BASIC
// TR: Settings
// TR: Other
// (the Control Panel's applets, named by their link files)
// TR: Theme
// TR: Display
// TR: Panel
// TR: Sound
// TR: Preload
// TR: Keyboard & Mouse
// TR: Language & Region
// TR: Printers
// TR: Gamepad
// TR: Wi-Fi
// TR: Packages
// TR: App Settings

// ---- the space behind: drawn once for a size (a gradient, the motes, the towers of cubes) ------------------------
static unsigned *g_bgPx; static int g_bgW, g_bgH;
static unsigned rnd (unsigned &s) { s = s * 1664525u + 1013904223u; return s >> 8; }
static void cube (Canvas &cv, int x, int y, int s, int a)		// a translucent cube's front and top
{
	for (int j = 0; j < s; j++)
		for (int i = 0; i < s; i++)
		{
			bool edge = i == 0 || j == 0 || i == s - 1 || j == s - 1;
			lk_px (cv, x + i, y + j, edge ? 0x6FA8E8 : 0x2C5AA0, edge ? a : a / 3);
		}
	for (int j = 1; j <= s / 3; j++) for (int i = 0; i < s; i++) lk_px (cv, x + i + j, y - j, 0x4F86D0, a / 2);
}
static void space_make (int w, int h)
{
	delete [] g_bgPx;
	g_bgPx = new unsigned[w * h]; g_bgW = w; g_bgH = h;
	Canvas cv; cv.adopt (g_bgPx, w, h, w);
	for (int y = 0; y < h; y++)
	{
		unsigned c = y < h * 2 / 3 ? uk_mix (0x05080F, 0x0A1630, y * 256 / (h * 2 / 3)) : uk_mix (0x0A1630, 0x163268, (y - h * 2 / 3) * 256 / (h / 3 + 1));
		cv.fillRect (0, y, w, 1, c);
	}
	unsigned s = 0x9E3779B9u;
	for (int k = 0; k < w * h / 2600; k++)				// the motes
	{
		int x = (int) (rnd (s) % (unsigned) w), y = (int) (rnd (s) % (unsigned) h), a = 40 + (int) (rnd (s) % 140), r = (int) (rnd (s) % 5) == 0 ? 2 : 1;
		for (int j = -r; j <= r; j++) for (int i = -r; i <= r; i++) if (i * i + j * j <= r * r) lk_px (cv, x + i, y + j, 0xBFD8FF, a / (1 + i * i + j * j));
	}
	int cs = D (20);
	for (int t = 0; t < 7; t++)					// the towers, receding to the left
	{
		int x = D (20) + t * D (54) + (int) (rnd (s) % (unsigned) D (16)), n = 2 + (int) (rnd (s) % 5), a = 26 + t * 3;
		int sz = cs - t;
		for (int k = 0; k < n; k++) cube (cv, x, h - D (70) - (k + 1) * (sz + D (3)) + t * D (4), sz, a - k * 2);
	}
}

// ---- small pieces --------------------------------------------------------------------------------------
static int tw (const char *s, int style = 0)	{ return uk_tw (s, style); }
static void num_cat (char *d, int cap, int *n, int v) { char b[16]; ax_itoa (v, b); lx_cat (d, cap, n, b); }
static void text_fit (Canvas &cv, int x, int y, int w, const char *s, unsigned c, int style = 0)
{
	char b[128];
	uk_text_fit (s, w, b, sizeof b, style);
	uk_text (cv, x, y, b, c, style);
}
static void text_cfit (Canvas &cv, int x, int y, int w, const char *s, unsigned c, int style = 0)
{
	char b[128];
	int n = uk_text_fit (s, w, b, sizeof b, style);
	uk_text (cv, x + (w - n) / 2, y, b, c, style);
}
static void glass (Canvas &cv, int x, int y, int w, int h, int r, int a = 200)
{
	uk_paint_alpha (true);
	lk_fill (cv, x, y, w, h, r, C_GLASS1, C_GLASS2, a);
	lk_ring (cv, x, y, w, h, r, 16, 0x5E86C8, 150);
	uk_paint_alpha (false);
}
// The chosen item: a lit box, its ring and its halo (no halo on the see-through overlay: it would darken the app).
static void glow (Canvas &cv, int x, int y, int w, int h, int r, bool halo = true)
{
	if (halo) lk_shadow (cv, x, y, w, h, r, D (10), 150, 0, C_GLOW);
	lk_fill (cv, x, y, w, h, r, 0x2E5FA8, 0x224A88, 235);
	lk_ring (cv, x, y, w, h, r, 24, 0xBFE4FF, 255);
}
// A pad's button and its word ("A" in a ring, then "Enter") -> the width drawn.
static int hint (Canvas &cv, int x, int y, const char *btn, unsigned ring, const char *word)
{
	UkFaceScope f (F (12));
	int h = uk_fh () + D (8), bw = tw (btn, 2) + D (12);
	if (bw < h) bw = h;
	lk_fill (cv, x, y, bw, h, h / 2, 0x0E1830, 0x0A1226, 230);
	lk_ring (cv, x, y, bw, h, h / 2, 20, ring, 255);
	uk_text (cv, x + (bw - tw (btn, 2)) / 2, y + D (4), btn, 0xF2F6FF, 2);
	uk_text (cv, x + bw + D (7), y + D (4), word, C_WORD);
	return bw + D (7) + tw (word) + D (22);
}

struct Hit { int x, y, w, h, kind, i; };
#define MAXHITS 120
struct Hits { Hit h[MAXHITS]; int n; void clear () { n = 0; } void add (int x, int y, int w, int h_, int k, int i)
	{ if (n < MAXHITS) h[n++] = { x, y, w, h_, k, i }; }
	const Hit *at (int x, int y) const { for (int k = n - 1; k >= 0; k--) if (x >= h[k].x && x < h[k].x + h[k].w && y >= h[k].y && y < h[k].y + h[k].h) return &h[k]; return 0; } };
static Hits g_homeHits, g_overHits;
enum { H_CAT = 1, H_TILE, H_MORE, H_ITEM };

// ---- what the home shows ---------------------------------------------------------------------------------
static int g_cat;					// the category chosen (g_cats)
static int g_firstCat;					// the first one drawn (more than fit)
static bool g_inTiles;					// the glow is on a tile (else on the category)
static int g_tile, g_firstRow;				// the tile chosen, the first row drawn
static int g_items[MAXAPPS], g_nitems;			// the chosen category's apps (g_apps)
static int g_cols = 3, g_rowsShown = 3;

static bool cat_shown (const char *c) { return !ieq (c, "Demos") && !ieq (c, "Other") && !ieq (c, "Shell") && !ieq (c, "Emulators"); }
static int cat_count (int c)
{
	const char *n = g_cats[c].name;
	if (ieq (n, "Recent")) { int k = 0; for (int i = 0; i < g_nrecent; i++) if (find_app (g_recent[i])) k++; return k; }
	int k = 0;
	for (int a = 0; a < g_napps; a++)
		if (!g_apps[a].hidden && (ieq (n, "Settings") ? g_apps[a].applet : !g_apps[a].applet && ieq (g_apps[a].cat, n))) k++;
	return k;
}
static void items_read (void)
{
	g_nitems = 0;
	if (g_cat < 0 || g_cat >= g_ncats) return;
	const char *n = g_cats[g_cat].name;
	if (ieq (n, "Recent"))
	{
		for (int i = 0; i < g_nrecent; i++) { App *a = find_app (g_recent[i]); if (a) g_items[g_nitems++] = (int) (a - g_apps); }
		return;
	}
	for (int a = 0; a < g_napps && g_nitems < MAXAPPS; a++)
		if (!g_apps[a].hidden && (ieq (n, "Settings") ? g_apps[a].applet : !g_apps[a].applet && ieq (g_apps[a].cat, n))) g_items[g_nitems++] = a;
}
static void cats_filter (void)				// the categories console mode shows (not Demos, not Other)
{
	int k = 0;
	for (int i = 0; i < g_ncats; i++) if (cat_shown (g_cats[i].name)) g_cats[k++] = g_cats[i];
	g_ncats = k;
	if (g_cat >= g_ncats) g_cat = 0;
}
static void choose_cat (int c)
{
	if (g_ncats == 0) return;
	g_cat = (c + g_ncats) % g_ncats;
	g_tile = 0; g_firstRow = 0;
	items_read ();
	if (g_nitems == 0) g_inTiles = false;
	g_homeDirty = true;
}
static bool screen_of (const char *app, int *w, int *h);
static void screen_to (int w, int h, const char *who);
static char g_launch[32];				// an app just opened: its size before it comes in front (screen_follow)
static unsigned g_launchT;
static void open_tile (int i)
{
	if (i < 0 || i >= g_nitems) return;
	tasks_read ();
	const char *name = g_apps[g_items[i]].name;
	int w, h;
	if (task_of (name) < 0 && screen_of (name, &w, &h))		// (its own size before it starts: screen_follow)
	{
		fs_copy (g_launch, name, sizeof g_launch); g_launchT = kapi_get_ticks ();
		screen_to (w, h, name);
	}
	open_app (name);
}

// ---- the home drawn ------------------------------------------------------------------------------------------
static void draw_home (void)
{
	g_homeDirty = false;
	Canvas &cv = g_hc;
	int W = g_sw, H = g_sh;
	if (g_bgPx == 0 || g_bgW != W || g_bgH != H) space_make (W, H);
	for (int y = 0; y < H; y++) memcpy (cv.px + (long) y * cv.stride, g_bgPx + (long) y * W, (size_t) W * 4);
	g_homeHits.clear ();
	uk_paint_alpha (false);

	// the top: the gem and "Onyx"; the day and the time
	{
		int gx = D (22), gy = D (20);
		int g = D (6);
		for (int j = -g; j <= g; j++) for (int i = -g; i <= g; i++)
		{
			int d = (i < 0 ? -i : i) + (j < 0 ? -j : j);
			if (d <= g) cv.pixel (gx + g + i, gy + g + j, d == g ? 0x9CCBFF : j < 0 ? 0x78B4F0 : 0x3D86DA);
		}
		UkFaceScope f (F (15));
		uk_text (cv, gx + D (22), gy - D (2), "Onyx", C_WORD);
		int hh = 0, mi = 0; kapi_get_datetime (0, 0, 0, &hh, &mi, 0);
		char t[8] = { (char) ('0' + hh / 10), (char) ('0' + hh % 10), ':', (char) ('0' + mi / 10), (char) ('0' + mi % 10), 0 };
		UkFaceScope f2 (F (18));
		uk_text (cv, W - D (22) - tw (t), gy - D (5), t, 0xFFFFFF);
	}

	// the left: the categories, big thin words; the chosen one glowing with its line
	int lx = D (34), ly = D (72), lw = W * 44 / 100, rowH = D (40), selH = D (62);
	int foot = H - D (52);
	int fit = (foot - ly - (selH - rowH)) / rowH;
	if (fit < 1) fit = 1;
	if (g_cat < g_firstCat) g_firstCat = g_cat;
	if (g_cat >= g_firstCat + fit) g_firstCat = g_cat - fit + 1;
	if (g_firstCat > g_ncats - fit) g_firstCat = g_ncats - fit > 0 ? g_ncats - fit : 0;
	int y = ly;
	for (int c = g_firstCat; c < g_ncats && c < g_firstCat + fit; c++)
	{
		bool on = c == g_cat;
		int h = on ? selH : rowH;
		const char *name = TR (g_cats[c].name);
		if (on)
		{
			uk_paint_alpha (true);
			if (!g_inTiles) glow (cv, lx - D (10), y, lw - D (30), h - D (4), D (10));
			else { lk_fill (cv, lx - D (10), y, lw - D (30), h - D (4), D (10), 0x1E3258, 0x1A2C4E, 200); lk_ring (cv, lx - D (10), y, lw - D (30), h - D (4), D (10), 16, 0x5E86C8, 200); }
			uk_paint_alpha (false);
			{ UkFaceScope f (F (25)); text_fit (cv, lx + D (18), y + D (4), lw - D (70), name, 0xFFFFFF); }
			char sub[64]; int k = 0, n = cat_count (c);
			num_cat (sub, sizeof sub, &k, n); lx_cat (sub, sizeof sub, &k, " "); lx_cat (sub, sizeof sub, &k, ieq (g_cats[c].name, "Settings") ? TR ("settings") : n == 1 ? TR ("app") : TR ("apps"));
			{ UkFaceScope f (F (12)); text_fit (cv, lx + D (20), y + D (36), lw - D (70), sub, 0xD6E4F8); }
		}
		else { UkFaceScope f (F (21)); text_fit (cv, lx + D (18), y + D (6), lw - D (70), name, uk_mix (0x0A1630, C_WORD, 205 - (c > g_cat ? (c - g_cat) * 18 : 0))); }
		lk_fill (cv, lx - D (2), y + h / 2 - D (5), D (7), D (7), D (3), g_cats[c].dot, g_cats[c].dot, on ? 255 : 170);
		g_homeHits.add (lx - D (10), y, lw - D (30), h - D (2), H_CAT, c);
		y += h;
	}

	// the right: the chosen category's apps, tiles in a glass panel
	int px = lx + lw - D (10), py = D (62), pw = W - px - D (22), ph = foot - py - D (8);
	glass (cv, px, py, pw, ph, D (12));
	{
		UkFaceScope f (F (15));
		text_fit (cv, px + D (16), py + D (10), pw - D (80), g_ncats ? TR (g_cats[g_cat].name) : "", C_WORD);
		char n[12]; int k = 0; num_cat (n, sizeof n, &k, g_nitems);
		uk_text (cv, px + pw - D (16) - tw (n), py + D (10), n, C_DIM);
	}
	cv.fillRect (px + D (14), py + D (36), pw - D (28), 1, 0x3E5E96);
	int tileW = D (96), tileH = D (98), ic = D (52);
	g_cols = (pw - D (16)) / tileW; if (g_cols < 1) g_cols = 1;
	g_rowsShown = (ph - D (62)) / tileH; if (g_rowsShown < 1) g_rowsShown = 1;
	int rows = (g_nitems + g_cols - 1) / g_cols;
	if (g_tile >= g_nitems) g_tile = g_nitems ? g_nitems - 1 : 0;
	if (g_tile / g_cols < g_firstRow) g_firstRow = g_tile / g_cols;
	if (g_tile / g_cols >= g_firstRow + g_rowsShown) g_firstRow = g_tile / g_cols - g_rowsShown + 1;
	if (g_firstRow > rows - g_rowsShown) g_firstRow = rows - g_rowsShown > 0 ? rows - g_rowsShown : 0;
	int gx0 = px + (pw - g_cols * tileW) / 2, gy0 = py + D (44);
	for (int i = g_firstRow * g_cols; i < g_nitems && i < (g_firstRow + g_rowsShown) * g_cols; i++)
	{
		int cx = gx0 + (i % g_cols) * tileW, cy = gy0 + (i / g_cols - g_firstRow) * tileH;
		App &a = g_apps[g_items[i]];
		bool on = g_inTiles && i == g_tile;
		uk_paint_alpha (true);
		if (on) glow (cv, cx + D (4), cy + D (2), tileW - D (8), tileH - D (6), D (10));
		int ix = cx + (tileW - ic) / 2 - D (5), iy = cy + D (8);
		lk_fill (cv, ix, iy, ic + D (10), ic + D (10), D (10), 0x3A5C9C, 0x24407A, 235);		// the tile: a glossy card
		lk_ring (cv, ix, iy, ic + D (10), ic + D (10), D (10), 16, 0x8CB4EC, 200);
		uk_paint_alpha (false);
		draw_icon (cv, ix + D (5), iy + D (5), a, ic);
		if (a.icon == 0)
		{
			UkFaceScope f (F (21));
			char c1[2] = { (char) (a.label[0] >= 'a' && a.label[0] <= 'z' ? a.label[0] - 32 : a.label[0]), 0 };
			uk_text (cv, ix + (ic + D (10) - tw (c1, 2)) / 2, iy + (ic + D (10) - uk_fh ()) / 2, c1, 0xFFFFFF, 2);
		}
		{ UkFaceScope f (F (12)); text_cfit (cv, cx + D (2), cy + ic + D (22), tileW - D (4), a.applet ? TR (a.label) : a.label, on ? 0xFFFFFF : C_WORD); }
		g_homeHits.add (cx, cy, tileW, tileH, H_TILE, i);
	}
	if (g_nitems == 0) { UkFaceScope f (F (13)); text_cfit (cv, px, py + ph / 2 - D (8), pw, TR ("Nothing here yet"), C_DIM); }
	int more = g_nitems - (g_firstRow + g_rowsShown) * g_cols;
	if (more > 0)
	{
		UkFaceScope f (F (12));
		char m[40]; int k = 0;
		lx_cat (m, sizeof m, &k, "\xE2\x96\xBE  "); num_cat (m, sizeof m, &k, more); lx_cat (m, sizeof m, &k, " "); lx_cat (m, sizeof m, &k, TR ("more"));
		text_cfit (cv, px, py + ph - D (22), pw, m, C_DIM);
		g_homeHits.add (px, py + ph - D (26), pw, D (24), H_MORE, 0);
	}

	// the foot: the buttons that work here
	cv.fillRect (0, foot, W, 1, 0x24406E);
	int hx = D (120), hy = foot + D (14);
	if (W < D (560)) hx = D (16);
	hx += hint (cv, hx, hy, "A", 0x5E9CFF, g_inTiles ? TR ("Open") : TR ("Enter"));
	if (g_inTiles) hx += hint (cv, hx, hy, "B", 0xFF6A6A, TR ("Back"));
	hx += hint (cv, hx, hy, "L1 R1", 0x9AA8C0, TR ("Category"));
	if (hx + D (140) < W) hint (cv, hx, hy, "Home", 0x9AA8C0, TR ("Menu"));
	uk_win_select (W_HOME); uk_win_present (); uk_win_select (0);
}

// ---- the home's moves -------------------------------------------------------------------------------------------
static void go (int key)
{
	switch (key)
	{
	case KEY_UP:
		if (g_inTiles) { if (g_tile >= g_cols) g_tile -= g_cols; }
		else choose_cat (g_cat - 1);
		break;
	case KEY_DOWN:
		if (g_inTiles) { if (g_tile + g_cols < g_nitems) g_tile += g_cols; else if (g_tile / g_cols < (g_nitems - 1) / g_cols) g_tile = g_nitems - 1; }
		else choose_cat (g_cat + 1);
		break;
	case KEY_RIGHT:
		if (!g_inTiles) { if (g_nitems) g_inTiles = true; }
		else if (g_tile + 1 < g_nitems) g_tile++;
		break;
	case KEY_LEFT:
		if (g_inTiles) { if (g_tile % g_cols == 0) g_inTiles = false; else g_tile--; }
		break;
	case KEY_ENTER:
		if (!g_inTiles) { if (g_nitems) g_inTiles = true; }
		else open_tile (g_tile);
		break;
	case 0x1b: case KEY_BACKSPACE: g_inTiles = false; break;
	case KEY_PGDN: if (g_nitems) { g_inTiles = true; g_tile += g_cols * g_rowsShown; if (g_tile >= g_nitems) g_tile = g_nitems - 1; } break;
	case KEY_PGUP: if (g_nitems) { g_inTiles = true; g_tile -= g_cols * g_rowsShown; if (g_tile < 0) g_tile = 0; } break;
	case '\t': { bool t = g_inTiles; choose_cat (g_cat + 1); g_inTiles = t && g_nitems > 0; } break;
	case -1: { bool t = g_inTiles; choose_cat (g_cat - 1); g_inTiles = t && g_nitems > 0; } break;	// (the previous category)
	default: return;
	}
	g_homeDirty = true;
}

// ---- the menu: an overlay over whatever is in front ----------------------------------------------------------------
enum { M_RESUME, M_HOME, M_TASK, M_CLOSE, M_SETTINGS, M_POWER, M_APPMENU, M_COMMAND, M_BACK };
struct MItem { int kind, task; char label[64]; };
#define MAXMI		(UK_TASKS_MAX + 48)
static MItem g_mi[MAXMI];
static int g_nmi, g_msel;
// The app in front's own menus (console has no menu bar: they are reached here): its spec as it declared it
// (uk_win_menu_set: "M<title>", "I<id>	<label>	<shortcut>", "-"), read when the menu comes up.
static char g_spec[4096], g_specTitle[64];
static int g_level = -1;				// -1 the menu itself, else the app's menu of that number
static const char *spec_menu (int m, char *title, int cap)	// the m-th "M" line -> what follows it (0: none)
{
	const char *p = g_spec;
	while (*p)
	{
		const char *e = p; while (*e && *e != '\n') e++;
		if (*p == 'M' && m-- == 0)
		{
			int n = 0; for (const char *q = p + 1; q < e && n < cap - 1; q++) title[n++] = *q;
			title[n] = 0;
			return *e ? e + 1 : e;
		}
		p = *e ? e + 1 : e;
	}
	return 0;
}

static void menu_build (void)
{
	tasks_read ();
	g_nmi = 0;
	if (g_level >= 0)					// one of the app's menus: its items
	{
		char t[64];
		const char *p = spec_menu (g_level, t, sizeof t);
		for (; p && *p && *p != 'M' && g_nmi < MAXMI - 1; )
		{
			const char *e = p; while (*e && *e != '\n') e++;
			if (*p == 'I')
			{
				MItem &m = g_mi[g_nmi]; m.kind = M_COMMAND; m.task = 0;
				const char *q = p + 1;
				while (q < e && *q >= '0' && *q <= '9') m.task = m.task * 10 + (*q++ - '0');
				if (q < e && *q == '\t') q++;
				int n = 0; while (q < e && *q != '\t' && n < (int) sizeof m.label - 1) m.label[n++] = *q++;
				m.label[n] = 0;
				if (n) g_nmi++;
			}
			p = *e ? e + 1 : e;
		}
		MItem &b = g_mi[g_nmi++]; b.kind = M_BACK; b.task = 0; fs_copy (b.label, TR ("Back"), sizeof b.label);
		if (g_msel >= g_nmi) g_msel = 0;
		return;
	}
	int front = -1;
	for (int i = 0; i < g_ntasks; i++) if (g_tasks[i].flags & UK_TASK_FRONT) front = i;
	auto add = [] (int kind, int task, const char *l) { MItem &m = g_mi[g_nmi++]; m.kind = kind; m.task = task; fs_copy (m.label, l, sizeof m.label); };
	auto name = [] (int t) -> const char * { App *a = find_app (g_tasks[t].name); return a ? a->label : g_tasks[t].title[0] ? g_tasks[t].title : g_tasks[t].name; };
	if (front >= 0)
	{
		char l[64]; int k = 0;
		lx_cat (l, sizeof l, &k, TR ("Resume")); lx_cat (l, sizeof l, &k, "  "); lx_cat (l, sizeof l, &k, name (front));
		add (M_RESUME, front, l);
		char t[64];
		for (int m = 0; spec_menu (m, t, sizeof t) && g_nmi < MAXMI - UK_TASKS_MAX - 6; m++)	// its menus
		{
			char l2[64]; int k2 = 0;
			lx_cat (l2, sizeof l2, &k2, t); lx_cat (l2, sizeof l2, &k2, "  \xE2\x80\xBA");
			add (M_APPMENU, m, l2);
		}
	}
	add (M_HOME, -1, TR ("Home"));
	for (int i = 0; i < g_ntasks; i++) if (i != front) add (M_TASK, i, name (i));
	if (front >= 0)
	{
		char l[64]; int k = 0;
		lx_cat (l, sizeof l, &k, TR ("Close")); lx_cat (l, sizeof l, &k, "  "); lx_cat (l, sizeof l, &k, name (front));
		add (M_CLOSE, front, l);
	}
	add (M_SETTINGS, -1, TR ("Settings"));
	add (M_POWER, -1, TR ("Shut Down..."));
	if (g_msel >= g_nmi) g_msel = 0;
}
static void menu_hide (void)
{
	if (!g_menu) return;
	g_menu = false;
	uk_shell_grab (0);
	uk_win_select (W_OVER); uk_win_alpha (0); uk_win_move (-g_sw - 50, 0); uk_win_select (0);
}
static void menu_show (void)
{
	g_spec[0] = 0; g_level = -1;
	if (!g_home) uk_win_menu_get (g_spec, sizeof g_spec, g_specTitle, sizeof g_specTitle);	// (the app in front's)
	g_menu = true; g_msel = 0;
	menu_build ();
	uk_win_select (W_OVER);
	uk_win_resize (g_sw, g_sh); uk_win_move (0, 0); uk_win_alpha (255); uk_win_raise (0);
	uk_win_select (0);
	g_oc.adopt (g_oc.px, g_sw, g_sh, g_overStride);
	uk_shell_grab (1);
	g_overDirty = true;
}
static void menu_do (int i)
{
	if (i < 0 || i >= g_nmi) return;
	MItem m = g_mi[i];
	if (m.kind == M_APPMENU) { g_level = m.task; g_msel = 0; menu_build (); g_overDirty = true; return; }
	if (m.kind == M_BACK) { g_level = -1; g_msel = 0; menu_build (); g_overDirty = true; return; }
	menu_hide ();
	switch (m.kind)
	{
	case M_RESUME: break;
	case M_HOME: uk_shell_front (0, 0); g_homeDirty = true; break;
	case M_TASK: if (m.task < g_ntasks) uk_shell_front (g_tasks[m.task].id, 1); break;
	case M_CLOSE: if (m.task < g_ntasks) uk_win_close (g_tasks[m.task].id); break;
	case M_SETTINGS: open_app ("control"); break;
	case M_POWER: lx_launch ("shutdown", 0); break;
	case M_COMMAND: uk_win_menu_command (m.task); break;
	}
}
static void draw_menu (void)
{
	g_overDirty = false;
	Canvas &cv = g_oc;
	g_overHits.clear ();
	uk_paint_alpha (true);
	for (int y = 0; y < g_sh; y++) for (int x = 0; x < g_sw; x++) cv.px[(long) y * cv.stride + x] = 0x90000000u | 0x04080F;	// (a dim over the app)
	int w = D (300), rowH = D (40), h = D (64) + g_nmi * rowH + D (16);
	if (w > g_sw - D (40)) w = g_sw - D (40);
	if (h > g_sh - D (70)) h = g_sh - D (70);
	int x = D (24), y = D (24);
	lk_fill (cv, x, y, w, h, D (14), C_GLASS1, C_GLASS2, 240);
	lk_ring (cv, x, y, w, h, D (14), 16, 0x6E96D8, 220);
	{ UkFaceScope f (F (17)); uk_text (cv, x + D (20), y + D (14), "Onyx", 0xFFFFFF); }
	{
		char t[64]; t[0] = 0;
		if (g_level >= 0) spec_menu (g_level, t, sizeof t);
		UkFaceScope f (F (12)); text_fit (cv, x + D (20), y + D (38), w - D (40), g_level >= 0 ? t : TR ("The menu"), C_DIM);
	}
	int ry = y + D (64), fit = (h - D (72)) / rowH, first = 0;
	if (fit < 1) fit = 1;
	if (g_msel >= fit) first = g_msel - fit + 1;
	for (int i = first; i < g_nmi && ry + rowH <= y + h - D (8); i++, ry += rowH)
	{
		bool on = i == g_msel;
		if (on) glow (cv, x + D (10), ry, w - D (20), rowH - D (4), D (9), false);
		UkFaceScope f (F (15));
		text_fit (cv, x + D (26), ry + D (8), w - D (52), g_mi[i].label, on ? 0xFFFFFF : C_WORD);
		g_overHits.add (x + D (10), ry, w - D (20), rowH - D (4), H_ITEM, i);
	}
	lk_fill (cv, 0, g_sh - D (52), g_sw, D (52), 0, 0x0E1C3C, 0x0A1630, 250);		// (the foot: the menu's own buttons)
	int hx = D (40), hy = g_sh - D (38);
	hx += hint (cv, hx, hy, "A", 0x5E9CFF, TR ("Choose"));
	hint (cv, hx, hy, "B", 0xFF6A6A, TR ("Back"));
	uk_paint_alpha (false);
	uk_win_select (W_OVER); uk_win_present (); uk_win_select (0);
}
static void menu_key (int code)
{
	switch (code)
	{
	case KEY_UP: g_msel = (g_msel + g_nmi - 1) % (g_nmi ? g_nmi : 1); break;
	case KEY_DOWN: g_msel = (g_msel + 1) % (g_nmi ? g_nmi : 1); break;
	case KEY_ENTER: case ' ': menu_do (g_msel); return;
	case KEY_RIGHT: if (g_msel < g_nmi && g_mi[g_msel].kind == M_APPMENU) menu_do (g_msel); return;
	case KEY_LEFT: if (g_level < 0) return;		// (falls to: back)
	case 0x1b: case KEY_BACKSPACE:
		if (g_level >= 0) { g_level = -1; g_msel = 0; menu_build (); g_overDirty = true; return; }
		menu_hide (); return;
	case KEY_F1 + 9: menu_hide (); return;
	default: return;
	}
	g_overDirty = true;
}
static void menu_toggle (void) { if (g_menu) menu_hide (); else menu_show (); }

// ---- the events ---------------------------------------------------------------------------------------------------
static void home_key (unsigned long, int ev, long v)
{
	if (ev != GUI_EVENT_KEY) return;
	int k = (int) v;
	unsigned mods = (unsigned) kapi_get_modifiers ();
	if (k == KEY_F1 + 9) { menu_toggle (); return; }
	if ((k == KEY_PGUP || k == KEY_PGDN) && (mods & MOD_CTRL)) { go (k == KEY_PGDN ? '\t' : -1); return; }
	if (k == '\t' && (mods & MOD_SHIFT)) { go (-1); return; }
	if (k == '\n' || k == '\r') k = KEY_ENTER;
	go (k);
}
static void front_look (void);
static void shell_event (unsigned long, int ev, long v)
{
	if (ev == UK_SHELL_EVENT) { tasks_read (); front_look (); g_homeDirty = true; if (g_menu) { menu_build (); g_overDirty = true; } return; }
	if (ev != UK_SHELL_KEYEV) return;
	int code = UK_SHELL_KEY_CODE (v);
	if (g_menu)
	{
		if (code == UK_SHELL_KEY_SUPER) { menu_hide (); return; }
		if (code == '\n' || code == '\r') code = KEY_ENTER;
		menu_key (code);
		return;
	}
	if (code == UK_SHELL_KEY_SUPER || code == KEY_F1 + 9) menu_show ();
}
static void ptr (unsigned long win, int ev, long v)
{
	if (ev == GUI_EVENT_DISPLAY_RESIZE) { g_homeDirty = true; return; }
	int x = GUI_PTR_X (v), y = GUI_PTR_Y (v), c = GUI_PTR_CHANGED (v);
	if (win == W_OVER)
	{
		if (!g_menu) return;
		const Hit *h = g_overHits.at (x, y);
		if (ev == GUI_EVENT_PTR_MOVE && h && h->i != g_msel) { g_msel = h->i; g_overDirty = true; }
		if (ev == GUI_EVENT_PTR_DOWN && (c & 1)) { if (h) menu_do (h->i); else menu_hide (); }
		return;
	}
	const Hit *h = g_homeHits.at (x, y);
	if (ev == GUI_EVENT_PTR_MOVE && h)				// the glow follows the pointer
	{
		if (h->kind == H_TILE && (!g_inTiles || g_tile != h->i)) { g_inTiles = true; g_tile = h->i; g_homeDirty = true; }
		else if (h->kind == H_CAT && g_inTiles && h->i == g_cat) { g_inTiles = false; g_homeDirty = true; }
	}
	if (ev == GUI_EVENT_PTR_DOWN && (c & 1) && h)
	{
		if (h->kind == H_CAT) { choose_cat (h->i); g_inTiles = false; }
		else if (h->kind == H_TILE) { g_inTiles = true; g_tile = h->i; g_homeDirty = true; open_tile (h->i); }
		else if (h->kind == H_MORE) go (KEY_PGDN);
	}
	if (ev == GUI_EVENT_PTR_DOWN && (c & 2)) menu_show ();		// (a right click: the menu)
	if (ev == GUI_EVENT_PTR_WHEEL)
	{
		int wv = GUI_PTR_WHEEL (v);
		if (x < g_sw * 44 / 100) choose_cat (g_cat - wv);
		else { g_inTiles = g_nitems > 0; g_tile -= wv * g_cols; if (g_tile < 0) g_tile = 0; if (g_tile >= g_nitems) g_tile = g_nitems ? g_nitems - 1 : 0; g_homeDirty = true; }
	}
}

// The pad (every pad): the d-pad repeating while held, A enters, B backs, L1 / R1 the categories, L2 / R2 a page; Home
// (the pad's middle button) the menu -- Select too at home. OVER AN APP the pad is the app's (gamepad.h gives the
// buttons to the program that has the keys): this shell reads them all the same (pad_any) for one thing, the menu's
// button -- Home, or Select and Start held together on a pad without one (Select alone is a game's). The menu up, or
// at home, PocketUI names this shell as the one that has the keys: the game under the menu reads nothing.
static unsigned pad_any (void)
{
	unsigned b = 0;
	for (int i = 0; i < PAD_MAX; i++)
	{
		struct kapi_pad p;
		struct pad_map m;
		if (!kapi_pad_state (i, &p)) continue;
		pad_map_for (&p, &m);
		b |= pad_apply (&p, &m, 0, 0, 0, 0);
	}
	return b;
}
// THE PAD AS KEYS: an app that is not a game (app.txt's category: not Games, not Emulators -- those read the pad
// themselves) is moved by the pad as by a keyboard, this shell typing for it (kapi_inject_key: the server routes the
// keys to the app in front): the d-pad the arrows, A Enter, B Esc, X Space, Y Tab, L1 / R1 Ctrl+Page Up / Down (the
// tabs), L2 / R2 Page Up / Down.
static bool g_padKeys;
// THE TIP: an app that comes to the front fills the screen with nothing of the shell's left -- for three seconds a
// small pill at the top right says how to get the menu ("Home  Menu"; a third window, see-through, parked after).
static Canvas g_tc;
static int g_tipW, g_tipH;
static unsigned g_tipT;
static bool g_tipOn;
static void tip_hide (void)
{
	if (!g_tipOn) return;
	g_tipOn = false;
	uk_win_select (W_TIP); uk_win_alpha (0); uk_win_move (-g_sw - 50, 0); uk_win_select (0);
}
static void tip_show (void)
{
	if (g_tc.px == 0 || g_menu) return;
	Canvas &cv = g_tc;
	for (int i = 0; i < cv.stride * g_tipH; i++) cv.px[i] = CLEAR;
	uk_paint_alpha (true);
	lk_fill (cv, 0, 0, g_tipW, g_tipH, g_tipH / 2, 0x101C38, 0x0A1428, 225);
	lk_ring (cv, 0, 0, g_tipW, g_tipH, g_tipH / 2, 16, 0x5E86C8, 220);
	hint (cv, D (10), (g_tipH - (F (12) ? D (12) + D (12) : D (24))) / 2, "Home", 0x9AA8C0, TR ("Menu"));
	uk_paint_alpha (false);
	uk_win_select (W_TIP); uk_win_move (g_sw - g_tipW - D (16), D (14)); uk_win_alpha (255); uk_win_raise (0); uk_win_present (); uk_win_select (0);
	g_tipOn = true; g_tipT = kapi_get_ticks ();
}
static void tip_tick (void) { if (g_tipOn && (kapi_get_ticks () - g_tipT > 300 || g_menu || g_home)) tip_hide (); }

static char g_front[32];				// the app in front ("": none, the home)
static void front_look (void)				// (after tasks_read: who is in front)
{
	static char s_was[32];
	char now[32] = "";
	g_padKeys = false;
	for (int i = 0; i < g_ntasks; i++)
		if (g_tasks[i].flags & UK_TASK_FRONT)
		{
			App *a = find_app (g_tasks[i].name);
			g_padKeys = !(a && (ieq (a->cat, "Games") || ieq (a->cat, "Emulators")));
			fs_copy (now, g_tasks[i].name, sizeof now);
		}
	if (now[0] && !ieq (now, s_was)) tip_show ();		// (another app in front: the tip)
	fs_copy (s_was, now, sizeof s_was);
	fs_copy (g_front, now, sizeof g_front);
}
static void pad_type (unsigned press, unsigned held, bool rep)
{
	unsigned d = press ? press : rep ? held : 0;
	if (d & PAD_UP) kapi_inject_key ("\x1b[A");
	else if (d & PAD_DOWN) kapi_inject_key ("\x1b[B");
	else if (d & PAD_LEFT) kapi_inject_key ("\x1b[D");
	else if (d & PAD_RIGHT) kapi_inject_key ("\x1b[C");
	if (press & PAD_A) kapi_inject_key ("\n");
	if (press & PAD_B) kapi_inject_key ("\x1b");
	if (press & PAD_X) kapi_inject_key (" ");
	if (press & PAD_Y) kapi_inject_key ("\t");
	if (press & PAD_L2) kapi_inject_key ("\x1b[5~");
	if (press & PAD_R2) kapi_inject_key ("\x1b[6~");
	if (press & (PAD_L | PAD_R))
	{
		kapi_inject_modifiers (MOD_CTRL);
		kapi_inject_key (press & PAD_L ? "\x1b[5~" : "\x1b[6~");
		kapi_inject_modifiers (0);
	}
}
static void pad_poll (void)
{
	static unsigned last, t0;
	unsigned b = pad_any (), now = kapi_get_ticks ();
	unsigned press = b & ~last;
	bool rep = (b & (PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT)) && (int) (now - t0) > 16;
	last = b;
	bool mine = g_menu || g_home;
	if ((press & PAD_HOME) || (mine && (press & PAD_SELECT))
	    || (!mine && (press & (PAD_SELECT | PAD_START)) && (b & PAD_SELECT) && (b & PAD_START))) { menu_toggle (); return; }
	if (!press && !rep) return;
	if (!mine)						// (an app in front: the pad is its own, or types for it)
	{
		if (g_padKeys && !(b & PAD_SELECT))
		{
			if (b & (PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT) && (press || rep)) t0 = now + (press ? 22 : 0);
			pad_type (press, b, rep);
		}
		return;
	}
	unsigned d = press ? press : b;
	int k = (d & PAD_RIGHT) ? KEY_RIGHT : (d & PAD_LEFT) ? KEY_LEFT : (d & PAD_DOWN) ? KEY_DOWN : (d & PAD_UP) ? KEY_UP : 0;
	if (press & (PAD_A | PAD_START)) k = KEY_ENTER;
	else if (press & PAD_B) k = 0x1b;
	else if (press & PAD_L) k = -1;
	else if (press & PAD_R) k = '\t';
	else if (press & PAD_L2) k = KEY_PGUP;
	else if (press & PAD_R2) k = KEY_PGDN;
	if (k == 0) return;
	if (k == KEY_UP || k == KEY_DOWN || k == KEY_LEFT || k == KEY_RIGHT) t0 = now + (press ? 22 : 0);
	if (g_menu) { if (k != -1 && k != '\t' && k != KEY_PGUP && k != KEY_PGDN) menu_key (k); }
	else go (k);
}

// ---- the games' screen sizes ----------------------------------------------------------------------------------
// An app may want its own resolution on the console (an emulator: the size its picture scales best to, the GPU's
// work for the 3D ones): its app.txt's `resolution = 800x600`, or the
// user's SD:/etc/console.ini, section [screen], `<app> = 640x480` (`system`: the system's). While that app is in
// front the screen has its size (kapi_screen_set, switched before it starts when this shell opens it); at home, the
// game ended or another app in front, the system's size again (the one the screen had before).
static int g_sysW, g_sysH;
static bool size_parse (const char *s, int *w, int *h)
{
	int a = 0, b = 0, k = 0;
	while (s[k] == ' ') k++;
	while (s[k] >= '0' && s[k] <= '9') a = a * 10 + (s[k++] - '0');
	while (s[k] == ' ') k++;
	if (s[k] != 'x' && s[k] != 'X' && s[k] != '*') return false;
	k++;
	while (s[k] == ' ') k++;
	while (s[k] >= '0' && s[k] <= '9') b = b * 10 + (s[k++] - '0');
	if (a < 640 || b < 480 || a > 2560 || b > 1600) return false;
	*w = a & ~1; *h = b;
	return true;
}
static bool screen_of (const char *app, int *w, int *h)	// the app's own size -> true (cached by name)
{
	static char s_app[32]; static int s_w, s_h; static unsigned s_t;
	if (!ieq (app, s_app) || kapi_get_ticks () - s_t > 500)	// (read again every 5 s: a file changed meanwhile)
	{
		fs_copy (s_app, app, sizeof s_app); s_t = kapi_get_ticks (); s_w = s_h = 0;
		bool said = false;
		if (app_ini_load_path ("SD:/etc/console.ini") >= 0)
		{
			const char *v = app_ini_get ("screen", app, 0);
			if (v) { said = true; if (!size_parse (v, &s_w, &s_h)) s_w = s_h = 0; }
		}
		char p[80]; int n = 0;
		lx_cat (p, sizeof p, &n, "SD:/apps/"); lx_cat (p, sizeof p, &n, app); lx_cat (p, sizeof p, &n, ".app/app.txt");
		if (!said && app_ini_load_path (p) >= 0)
		{
			const char *v = app_ini_get (0, "resolution", 0);
			if (v && !size_parse (v, &s_w, &s_h)) s_w = s_h = 0;
		}
	}
	*w = s_w; *h = s_h;
	return s_w > 0;
}
// The screen at w x h for an app (w 0: the system's size again). The system's size is the screen's whenever no app's
// own size is on (a change made meanwhile -- the Display applet -- followed); only a size set here is undone.
static bool g_ours;					// the screen has an app's size, set here
static void screen_to (int w, int h, const char *who)
{
	int cw = 0, ch = 0;
	kapi_screen_size (&cw, &ch);
	if (w == 0 && !g_ours) { if (cw > 0) { g_sysW = cw; g_sysH = ch; } return; }
	if (w == 0) { w = g_sysW; h = g_sysH; }
	else if (!g_ours && cw > 0) { g_sysW = cw; g_sysH = ch; }
	static unsigned s_fail; static int s_fw, s_fh;
	if (cw == w && ch == h) { g_ours = w != g_sysW || h != g_sysH; return; }
	if (w == s_fw && h == s_fh && kapi_get_ticks () - s_fail < 200) return;	// (refused: again in 2 s)
	int r = kapi_screen_set (w, h);
	char m[96]; int n = 0;
	lx_cat (m, sizeof m, &n, "consolehome: the screen at "); num_cat (m, sizeof m, &n, w); lx_cat (m, sizeof m, &n, " x "); num_cat (m, sizeof m, &n, h);
	lx_cat (m, sizeof m, &n, who[0] ? " for " : " (the system's)"); if (who[0]) lx_cat (m, sizeof m, &n, who);
	if (r != 0) { lx_cat (m, sizeof m, &n, " -- not now: "); num_cat (m, sizeof m, &n, r); s_fail = kapi_get_ticks (); s_fw = w; s_fh = h; }
	else g_ours = w != g_sysW || h != g_sysH;
	static char s_said[96];
	if (strcmp (m, s_said) != 0) { strcpy (s_said, m); ax_putln (m); }	// (a refusal said once)
}
static void screen_follow (void)			// (every 0.1 s: the screen at the size of what is in front)
{
	if (g_sysW == 0) return;
	const char *who = g_home ? "" : g_front;
	if (g_launch[0])
	{
		if (ieq (g_front, g_launch) || kapi_get_ticks () - g_launchT > 1000) g_launch[0] = 0;	// (in front, or 10 s)
		else who = g_launch;
	}
	int w = 0, h = 0;
	if (!(who[0] && screen_of (who, &w, &h))) w = h = 0;
	screen_to (w, h, w ? who : "");
}

static void messages (void)
{
	static char buf[520];
	int from = 0, type = 0, n;
	while ((n = kapi_mailbox_recv (&from, &type, buf, sizeof buf - 1, 0)) >= 0)
	{
		if (type == SHELL_MSG_HOME) { menu_hide (); uk_shell_front (0, 0); g_homeDirty = true; }
		else if (type == SHELL_MSG_SWITCHER || type == SHELL_MSG_QUICK) menu_toggle ();
	}
}

static int scale_of (void)
{
	if (app_ini_load_path ("SD:/etc/theme.txt") >= 0)
	{
		const char *s = app_ini_get (0, "scale", 0);
		if (s && s[0] >= '1' && s[0] <= '3')
		{
			int v = (s[0] - '0') * 100;
			if (s[1] == '.' && s[2] >= '0' && s[2] <= '9') v += (s[2] - '0') * 10;
			return v;
		}
	}
	int m = g_sw < g_sh ? g_sw : g_sh;
	return m >= 1800 ? 200 : m >= 1000 ? 150 : 100;
}

// The screen followed (a mode switch, another resolution): the windows its size, the faces at the scale.
static void screen_sync (void)
{
	static int lw, lh, capW, capH;
	int w = 0, h = 0;
	kapi_screen_size (&w, &h);
	struct uk_win_server_info si;
	memset (&si, 0, sizeof si);
	si.size = sizeof si;
	if (uk_win_server (&si) == 1 && si.screen_w > 0 && si.screen_h > 0) { w = si.screen_w; h = si.screen_h; }
	if (w <= 0 || h <= 0 || (w == lw && h == lh)) return;
	if (capW == 0) { capW = g_sw; capH = g_sh; }
	g_sw = w; g_sh = h; lw = w; lh = h;
	if (w > capW || h > capH)
	{
		int nw = w > capW ? w : capW, nh = h > capH ? h : capH, st = nw;
		uk_win_select (W_HOME);
		unsigned *fb = uk_win_resize2 (nw, nh, &st);
		if (fb) { g_hc.adopt (fb, nw, nh, st); g_homeStride = st; }
		uk_win_select (W_OVER);
		fb = uk_win_resize2 (nw, nh, &st);
		if (fb) { g_oc.adopt (fb, nw, nh, st); g_overStride = st; for (int i = 0; i < st * nh; i++) fb[i] = CLEAR; capW = nw; capH = nh; }
		uk_win_select (0);
	}
	int s = scale_of ();
	if (s != S)
	{
		S = s;
		ft_uikit_install ("DejaVu Sans", D (13));
		for (int i = 0; i < 40; i++) if (g_faces[i]) g_faces[i]->open ("DejaVu Sans", D (i));
		for (int i = 0; i < g_napps; i++) { delete [] g_apps[i].scaled; g_apps[i].scaled = 0; g_apps[i].ss = 0; }
	}
	uk_win_select (W_HOME); uk_win_move (0, 0); uk_win_resize (g_sw, g_sh); uk_win_select (0);
	g_hc.adopt (g_hc.px, g_sw, g_sh, g_homeStride);
	g_homeDirty = true;
}

int main (void)
{
	if (uk_shell_register () != 1)
	{
		ax_puts ("consolehome: the console mode's shell -- PocketUI is not the graphics server here, or another shell runs\n");
		return 0;
	}
	kapi_screen_size (&g_sw, &g_sh);
	g_sysW = g_sw; g_sysH = g_sh;				// (the system's size: the screen's with no game's size on)
	{
		struct uk_win_server_info si;
		memset (&si, 0, sizeof si);
		si.size = sizeof si;
		if (uk_win_server (&si) == 1 && si.screen_w > 0 && si.screen_h > 0) { g_sw = si.screen_w; g_sh = si.screen_h; }
	}
	uikit::init ();
	S = scale_of ();
	ft_uikit_install ("DejaVu Sans", D (13));
	uk_lang_init ();

	unsigned *fb = uk_win_create_ex (0, 0, g_sw, g_sh, "consolehome", WIN_FLAG_BACKMOST | WIN_FLAG_BORDERLESS | WIN_FLAG_SYSTEM);
	if (fb == 0) { ax_puts ("consolehome: no window\n"); return 1; }
	g_homeStride = g_sw;
	g_hc.adopt (fb, g_sw, g_sh, g_homeStride);
	uk_win_on_pointer (ptr);
	uk_win_on_key (home_key);
	unsigned *ob = 0;
	if (uk_win_new (0, 0, g_sw, g_sh, "consolehome menu", WIN_FLAG_TOPMOST | WIN_FLAG_BORDERLESS | WIN_FLAG_ALPHA | WIN_FLAG_SYSTEM, &ob) != W_OVER)
	{ ax_puts ("consolehome: no overlay window\n"); return 1; }
	for (int i = 0; i < g_sw * g_sh; i++) ob[i] = CLEAR;
	g_overStride = g_sw;
	g_oc.adopt (ob, g_sw, g_sh, g_overStride);
	uk_win_select (W_OVER); uk_win_on_pointer (ptr); uk_win_alpha (0); uk_win_move (-g_sw - 50, 0); uk_win_present ();
	uk_win_select (0);
	{	// the tip's window (made on the screen, see-through, then parked off it)
		unsigned *tb = 0;
		g_tipW = D (122); g_tipH = D (34);
		if (uk_win_new (0, 0, g_tipW, g_tipH, "consolehome tip", WIN_FLAG_TOPMOST | WIN_FLAG_BORDERLESS | WIN_FLAG_ALPHA | WIN_FLAG_SYSTEM, &tb) == W_TIP)
		{
			for (int i = 0; i < g_tipW * g_tipH; i++) tb[i] = CLEAR;
			g_tc.adopt (tb, g_tipW, g_tipH, g_tipW);
			uk_win_select (W_TIP); uk_win_alpha (0); uk_win_move (-g_sw - 50, 0); uk_win_present (); uk_win_select (0);
		}
	}

	kapi_ipc_register (SHELL_SERVICE);
	scan_apps ();
	cats_filter ();
	recent_load ();
	for (int c = 0; c < g_ncats; c++) if (ieq (g_cats[c].name, "Games")) g_cat = c;		// (the console wakes on its games...)
	if (g_nrecent > 0) g_cat = 0;								// (... or on what was used last)
	items_read ();
	static const int K[] = { UK_SHELL_KEY (0, UK_SHELL_KEY_SUPER), UK_SHELL_KEY (0, KEY_F1 + 9) };
	uk_shell_events (shell_event);
	uk_shell_keys (K, (int) (sizeof K / sizeof K[0]));
	tasks_read ();
	front_look ();

	int lastMin = -1;
	unsigned lastSync = 0, lastScan = kapi_get_ticks ();
	while (!kapi_should_exit ())
	{
		pump_events ();
		messages ();
		pad_poll ();
		int mi = 0;
		kapi_get_datetime (0, 0, 0, 0, &mi, 0);
		if (mi != lastMin) { lastMin = mi; g_homeDirty = true; }
		if (kapi_get_ticks () - lastSync >= 10) { lastSync = kapi_get_ticks (); screen_follow (); screen_sync (); }
		if (kapi_get_ticks () - lastScan > 1000 && g_home && !g_menu)		// (an app installed meanwhile: every 10 s at home)
		{
			lastScan = kapi_get_ticks ();
			static char list[4096], seen[4096];
			kapi_list_apps (list, sizeof list);
			if (strcmp (list, seen) != 0) { strcpy (seen, list); scan_apps (); cats_filter (); recent_load (); items_read (); g_homeDirty = true; }
		}
		if (g_homeDirty && g_home) draw_home ();
		if (g_overDirty && g_menu) draw_menu ();
		tip_tick ();
		msleep (g_menu ? 16 : 30);
	}
	return 0;
}
