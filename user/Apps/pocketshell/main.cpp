//
// pocketshell -- the pocket mode's shell (docs/POCKETUI-TECH-STUDY.md section 7.2, phase P5; the look and the rules:
// docs/COMPACT-SHELL-STUDY.md section 6). PocketUI (SD:/bin/pocketui) shows one app at a time; the global menu bar
// (the desktop's menubar, kept as pocket's top band: the user's decision) shows its menus, the tray and the time; this
// program is the rest -- a client of PocketUI through the pocket UIKit's shell calls (uikit/win.h uk_shell_*):
//
//   * the LAUNCHER (Home): its backmost window, at the work area, behind every app -- a search field, the category
//     tabs (Recent, the categories of the apps' app.txt in the dock's order -- SystemKit's dock_layout_load --,
//     Settings: the Control Panel's applets), the apps' grid (a dot under a running one), the Running strip (the
//     open apps); typing searches the apps, the settings, the files of SD:/docs and offers the text as a command;
//   * the TASK SWITCHER (Alt+Tab, the menu bar's Onyx > Open Apps): the open apps as cards, the most recent first,
//     their pictures from PocketUI (uk_shell_thumb: no app redraws); release Alt (or Enter, a click) to switch, Del
//     closes the chosen app, Esc stays;
//   * QUICK SETTINGS (Super+N, a click on the menu bar's time): Wi-Fi, do not disturb, the sound, the mode, the
//     volume, the NOTIFICATIONS -- this program serves the IPC service "notify" (SystemKit's notify.h) in pocket: a
//     notification shows as a toast under the bar, then waits in the panel -- the Control Panel, Lock, Power.
//
// Its keys come from PocketUI before the front app (uk_shell_keys): Super (alone) / Alt+F1 / Ctrl+Esc Home, Alt+Tab
// and Alt+Shift+Tab the switcher, Super+N quick settings, Super+Space the search; while an overlay is up every key is
// its (uk_shell_grab). The menu bar asks it through SystemKit's shell.h (the IPC service "shell"). Resolution
// independent: every size in logical units times a scale (SD:/etc/theme.txt "scale =", else 1.5 from a 1080-line
// screen up, 2 from 1800), the layout reflows by the logical width and height (the grid's columns, the hints, the
// Running strip, the switcher's row or column, the panel's width).
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

using namespace uikit;

#include "catalog.h"

// ---- the screen, the scale ------------------------------------------------------------------------------
static int g_sw = 800, g_sh = 480;			// the screen
static int g_ax, g_ay = 30, g_aw = 800, g_ah = 450;	// the work area (under the menu bar)
static int S = 100;					// the scale, percent
static int D (int lp)		{ return (lp * S + 50) / 100; }
static int LW (void)		{ return g_aw * 100 / S; }	// the logical width, height
static int LH (void)		{ return g_ah * 100 / S; }
static bool portrait (void)	{ return LH () * 100 > LW () * 105; }

static FtTextFace *g_big, *g_small;			// (the regular face: ft_uikit_install's)
static int g_fh = 16;

// ---- the windows ----------------------------------------------------------------------------------------
enum { W_HOME = 0, W_OVER = 1, W_TOAST = 2 };
static Canvas g_hc, g_oc, g_tc;				// their canvases
static int g_overStride, g_homeStride;
enum { OV_NONE, OV_SWITCHER, OV_QUICK };
static int g_over = OV_NONE;
static bool g_homeDirty = true, g_overDirty;

static void present (int w)	{ uk_win_select (w); uk_win_present (); uk_win_select (0); }

#define CLEAR		0xFF000000u			// a see-through pixel (WIN_FLAG_ALPHA)
#define CATCH		0xFE000000u			// ... almost: it still takes the clicks

// ---- colours: Milk's, from the theme ---------------------------------------------------------------------
static unsigned C_PANEL, C_PANEL2, C_SILVER, C_INK, C_INKDIM, C_NAVY1, C_NAVY2, C_OUTLINE;
static void colours (void)
{
	C_PANEL = uk_tone (C_FIELD, 132); C_PANEL2 = uk_tone (C_BG, 140);
	C_SILVER = C_DOCK; C_INK = C_FIELD_TEXT; C_INKDIM = uk_mix (C_FIELD, C_FIELD_TEXT, 150);
	C_NAVY1 = 0x1C2A44; C_NAVY2 = 0x2B3D5E; C_OUTLINE = uk_tone (C_BG, 92);
}

// ---- small pieces --------------------------------------------------------------------------------------
static int tw (const char *s, int style = 0)	{ return uk_tw (s, style); }
static void text (Canvas &cv, int x, int y, const char *s, unsigned c, int style = 0) { uk_text (cv, x, y, s, c, style); }
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

// A key's cap ("Alt", "Tab"): a small raised box with its word -> its width.
static int keycap (Canvas &cv, int x, int y, const char *k, bool dark = false)
{
	UkFaceScope f (g_small);
	int w = tw (k, 2) + D (10), h = uk_fh () + D (4);
	uk_rbox (cv, x, y, w, h, D (4), dark ? 0x5A6270 : 0xFAFAFA, dark ? 0x434A56 : 0xD8DADF);
	uk_rline (cv, x, y, w, h, D (4), dark ? 0x22262E : 0x9097A2);
	text (cv, x + D (5), y + D (2), k, dark ? 0xF0F2F5 : 0x30343A, 2);
	return w;
}
// Hints: caps then a word, "Tab|category|Arrows|choose" -> the width drawn.
static int hints (Canvas &cv, int x, int y, const char *const *h, int n, unsigned ink, bool dark = false, bool measure = false)
{
	int x0 = x;
	UkFaceScope f (g_small);
	for (int i = 0; i + 1 < n; i += 2)
	{
		for (const char *p = h[i]; *p; )			// ("Alt+Tab": two caps)
		{
			char k[24]; int j = 0;
			while (*p && *p != '+' && j < 23) k[j++] = *p++;
			k[j] = 0;
			if (*p == '+') p++;
			const char *kt = TR (k);				// (the keys' names: Entrée, Échap, Maj, Suppr...)
			if (kt != k) { int m = 0; while (kt[m] && m < 23) { k[m] = kt[m]; m++; } k[m] = 0; }
			if (measure) x += tw (k, 2) + D (10) + D (3);
			else x += keycap (cv, x, y, k, dark) + D (3);
		}
		x += D (3);
		if (!measure) text (cv, x, y + D (2), TR (h[i + 1]), ink);
		x += tw (TR (h[i + 1])) + D (14);
	}
	return x - x0;
}

// Hit boxes, made as each surface is drawn.
struct Hit { int x, y, w, h, kind, i; };
#define MAXHITS 200
struct Hits { Hit h[MAXHITS]; int n; void clear () { n = 0; } void add (int x, int y, int w, int h_, int k, int i)
	{ if (n < MAXHITS) h[n++] = { x, y, w, h_, k, i }; }
	const Hit *at (int x, int y) const { for (int k = n - 1; k >= 0; k--) if (x >= h[k].x && x < h[k].x + h[k].w && y >= h[k].y && y < h[k].y + h[k].h) return &h[k]; return 0; } };
static Hits g_homeHits, g_overHits;

// An app's icon, or its initial on a round tile when it has none.
static void icon_of (Canvas &cv, int x, int y, const char *name, const char *label, int s)
{
	App *a = find_app (name);
	if (a != 0 && (a->tried == false || a->icon != 0)) { draw_icon (cv, x, y, *a, s); if (a->icon) return; }
	uk_rbox (cv, x, y, s, s, s / 4, uk_tone (C_ACCENT, 150), uk_tone (C_ACCENT, 110));
	char c[2] = { (char) (label && label[0] ? label[0] : '?'), 0 };
	if (c[0] >= 'a' && c[0] <= 'z') c[0] = (char) (c[0] - 32);
	text (cv, x + (s - tw (c, 2)) / 2, y + (s - uk_fh ()) / 2, c, 0xFFFFFF, 2);
}

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
// TR: Up
// TR: Down
// TR: Shift
static const char *task_label (const struct uk_shell_task &t)
{
	App *a = find_app (t.name);
	return a != 0 ? a->label : t.title[0] ? t.title : t.name;
}

// =========================================================================================================
// THE LAUNCHER (the home window)
// =========================================================================================================
static int g_tab = 0;					// the tab shown (g_cats)
static int g_tabFirst = 0;				// the first tab drawn (they scroll)
static int g_focus = 0;					// the grid's focused tile
static int g_gridTop = 0;				// the grid's first row shown
static char g_query[64];				// the search's text ("": the tabs)
static int g_resSel = 0, g_resTop = 0;
enum { H_TAB = 1, H_CHEV, H_TILE, H_CHIP, H_CHIPX, H_RESULT, H_FIELD };

static int g_items[MAXAPPS], g_nitems;			// the apps of the tab shown
static void tab_items (void)
{
	g_nitems = 0;
	if (g_tab < 0 || g_tab >= g_ncats) return;
	const char *c = g_cats[g_tab].name;
	if (ieq (c, "Recent"))
	{
		for (int r = 0; r < g_nrecent; r++)
			for (int i = 0; i < g_napps; i++)
				if (!g_apps[i].applet && ieq (g_apps[i].name, g_recent[r])) { g_items[g_nitems++] = i; break; }
		return;
	}
	bool set = ieq (c, "Settings");
	for (int i = 0; i < g_napps; i++)
		if (set ? g_apps[i].applet : (!g_apps[i].applet && ieq (g_apps[i].cat, c))) g_items[g_nitems++] = i;
}

struct Grid { int x, y, w, h, tw, th, icon, cols, rows; };
static bool strip_shown (void)		{ return !portrait () && LH () >= 440; }
static bool hints_shown (void)		{ return LW () >= 700; }
static int field_w (void)		{ return hints_shown () ? D (300) : g_aw - 2 * D (12); }
static int tabs_y (void)		{ return D (10) + D (30) + D (8); }
static int tabs_h (void)		{ return D (LH () < 420 ? 28 : 32); }
static int strip_h (void)		{ return strip_shown () ? D (66) : 0; }

static Grid grid (void)
{
	Grid g;
	int M = D (12);
	g.x = M; g.y = tabs_y () + tabs_h (); g.w = g_aw - 2 * M; g.h = g_ah - g.y - M - strip_h ();
	bool big = LW () >= 600;
	g.tw = D (big ? 94 : 80); g.th = D (big ? 84 : 76); g.icon = D (big ? 48 : 40);
	g.cols = (g_aw - D (24)) / g.tw;
	if (!big && g.cols < 3) g.cols = 3;
	if (g.cols < 1) g.cols = 1;
	if (g.cols * g.tw > g.w) g.tw = g.w / g.cols;
	g.rows = (g.h - D (10)) / g.th;
	if (g.rows < 1) g.rows = 1;
	return g;
}

static void draw_search (Canvas &cv)
{
	int M = D (12), y = D (10), h = D (30), w = field_w ();
	uk_rbox (cv, M, y, w, h, h / 2, 0xFFFFFF, 0xF4F5F7);
	uk_rline (cv, M, y, w, h, h / 2, g_query[0] ? C_ACCENT : 0x9AA3B2);
	// the magnifier: a ring and its handle
	int r = D (5), cx = M + D (15), cy = y + h / 2 - D (2);
	uk_rline (cv, cx - r, cy - r, 2 * r + 1, 2 * r + 1, r, 0x505660);
	uk_rline (cv, cx - r + 1, cy - r + 1, 2 * r - 1, 2 * r - 1, r - 1, 0x505660);
	for (int k = 0; k < D (5); k++)
		for (int t = 0; t < 2; t++) cv.pixel (cx + r - 1 + k + t, cy + r - 1 + k, 0x505660);
	int tx = M + D (30), ty = y + (h - uk_fh ()) / 2;
	if (g_query[0])
	{
		text_fit (cv, tx, ty, w - D (40), g_query, 0x202428);
		int cxp = tx + tw (g_query);
		if (cxp < M + w - D (10)) cv.fillRect (cxp + 1, ty + D (1), D (2) > 1 ? 2 : 1, uk_fh () - D (2), 0x202428);	// (the caret)
	}
	else text_fit (cv, tx, ty, w - D (40), TR ("Type to find an app, a file, a setting"), 0x8A909A);
	g_homeHits.add (M, y, w, h, H_FIELD, 0);
	if (hints_shown ())
	{
		static const char *const H1[] = { TRN ("Tab"), TRN ("category"), TRN ("Arrows"), TRN ("choose"), TRN ("Enter"), TRN ("open") };
		static const char *const H2[] = { TRN ("Up+Down"), TRN ("choose"), TRN ("Enter"), TRN ("open"), TRN ("Esc"), TRN ("clear") };
		UkFaceScope f (g_small);
		hints (cv, M + w + D (18), y + (h - uk_fh () - D (4)) / 2, g_query[0] ? H2 : H1, 6, 0xE8ECF2, false);
	}
}

static void draw_tabs (Canvas &cv)
{
	int M = D (12), y = tabs_y (), h = tabs_h (), x = M, right = g_aw - M - D (30);
	if (g_tab < g_tabFirst) g_tabFirst = g_tab;
	for (;;)							// (the tab shown kept in view)
	{
		int xx = x;
		for (int i = g_tabFirst; i <= g_tab; i++) xx += tw (TR (g_cats[i].name), 2) + D (40) + D (4);
		if (xx <= right || g_tabFirst >= g_tab) break;
		g_tabFirst++;
	}
	bool more = false;
	for (int i = g_tabFirst; i < g_ncats; i++)
	{
		const char *n = TR (g_cats[i].name);
		bool on = i == g_tab;
		int w = tw (n, 2) + D (40);
		if (x + w > right) { more = true; break; }
		if (on) uk_rbox (cv, x, y, w, h + D (2), D (6), 0xFFFFFF, C_PANEL, 255, UK_TL | UK_TR);
		else
		{
			uk_rbox (cv, x, y + D (3), w, h - D (3), D (6), uk_tone (C_SILVER, 150), uk_tone (C_SILVER, 118), 255, UK_TL | UK_TR);
			uk_rline (cv, x, y + D (3), w, h - D (3), D (6), uk_tone (C_SILVER, 80), 255, UK_TL | UK_TR);
		}
		int ty = y + (on ? 0 : D (2)) + (h - uk_fh ()) / 2;
		int d = D (8);
		uk_rbox (cv, x + D (12), ty + (uk_fh () - d) / 2, d, d, d / 2, g_cats[i].dot, uk_tone (g_cats[i].dot, 110));
		text (cv, x + D (26), ty, n, on ? 0x14181E : 0x30343C, 2);
		g_homeHits.add (x, y, w, h, H_TAB, i);
		x += w + D (4);
	}
	if (more || g_tabFirst > 0)					// the chevron: the next tabs
	{
		int cx = g_aw - M - D (24), cy = y + D (4), s = D (24);
		uk_rbox (cv, cx, cy, s, s, D (5), uk_tone (C_SILVER, 140), uk_tone (C_SILVER, 112));
		uk_glyph (cv, more ? WKG_CHEV_RIGHT : WKG_CHEV_LEFT, cx + s / 2, cy + s / 2, D (10), 0x30343C);
		g_homeHits.add (cx, cy, s, s, H_CHEV, more ? 1 : -1);
	}
}

static void draw_grid (Canvas &cv)
{
	Grid g = grid ();
	uk_rbox (cv, g.x, g.y, g.w, g.h, D (8), C_PANEL, uk_tone (C_PANEL, 122), 255, UK_BL | UK_BR | (g_tab == g_tabFirst ? 0 : UK_TL) | UK_TR);
	tab_items ();
	if (g_focus >= g_nitems) g_focus = g_nitems > 0 ? g_nitems - 1 : 0;
	int frow = g_focus / g.cols;
	if (frow < g_gridTop) g_gridTop = frow;
	if (frow >= g_gridTop + g.rows) g_gridTop = frow - g.rows + 1;
	int gap = (g.w - g.cols * g.tw) / g.cols;
	if (g_nitems == 0)
		text_cfit (cv, g.x, g.y + g.h / 2 - uk_fh (), g.w, ieq (g_cats[g_tab].name, "Recent") ? TR ("The apps you open come here.") : TR ("No apps here."), C_INKDIM);
	for (int k = g_gridTop * g.cols; k < g_nitems && k < (g_gridTop + g.rows) * g.cols; k++)
	{
		int r = k / g.cols - g_gridTop, c = k % g.cols;
		int x = g.x + gap / 2 + c * (g.tw + gap), y = g.y + D (10) + r * g.th;
		App &a = g_apps[g_items[k]];
		bool on = k == g_focus;
		if (on) uk_hilite (cv, x + D (2), y, g.tw - D (4), g.th - D (4), D (8), true);
		draw_icon (cv, x + (g.tw - g.icon) / 2, y + D (6), a, g.icon);
		const char *l = a.applet ? TR (a.label) : a.label;
		text_cfit (cv, x + D (3), y + D (6) + g.icon + D (4), g.tw - D (6), l, on ? uk_hilite_ink (true) : C_INK, on ? 2 : 0);
		if (!a.applet && task_of (a.name) >= 0)				// running: a dot under its name
		{
			int d = D (5);
			uk_rbox (cv, x + (g.tw - d) / 2, y + g.th - D (8), d, d, d / 2, on ? 0xFFFFFF : C_ACCENT, on ? 0xFFFFFF : C_ACCENT);
		}
		g_homeHits.add (x, y, g.tw, g.th, H_TILE, k);
	}
	int total = (g_nitems + g.cols - 1) / g.cols;
	if (total > g.rows)						// (more rows: a thin bar on the right)
	{
		int th = g.h * g.rows / total, ty = g.y + (g.h - th) * g_gridTop / (total - g.rows);
		uk_rbox (cv, g.x + g.w - D (6), ty + D (4), D (3), th - D (8), D (1), 0xA0A6B0, 0xA0A6B0);
	}
}

static void draw_strip (Canvas &cv)
{
	if (!strip_shown ()) return;
	int M = D (12), y = g_ah - strip_h () + D (2);
	text (cv, M + D (4), y + D (2), TR ("Running"), 0xE8ECF2, 2);
	static const char *const H[] = { TRN ("Alt+Tab"), TRN ("switch") };
	{ UkFaceScope f (g_small); hints (cv, M + D (4) + tw (TR ("Running"), 2) + D (14), y + D (1), H, 2, 0xE8ECF2); }
	int x = M, cy = y + D (24), ch = D (34);
	if (g_ntasks == 0) { UkFaceScope f (g_small); text (cv, x + D (4), cy + D (8), TR ("No open apps."), 0xB8C0CC); }
	for (int i = 0; i < g_ntasks; i++)
	{
		const char *l = task_label (g_tasks[i]);
		int w = tw (l) + D (68);
		if (w > D (180)) w = D (180);
		if (x + w > g_aw - M) break;
		bool front = (g_tasks[i].flags & UK_TASK_FRONT) != 0;
		uk_rbox (cv, x, cy, w, ch, D (7), uk_tone (C_SILVER, 156), uk_tone (C_SILVER, 128));
		uk_rline (cv, x, cy, w, ch, D (7), front ? C_ACCENT : uk_tone (C_SILVER, 70));
		icon_of (cv, x + D (8), cy + (ch - D (20)) / 2, g_tasks[i].name, l, D (20));
		text_fit (cv, x + D (34), cy + (ch - uk_fh ()) / 2, w - D (58), l, 0x202428);
		uk_glyph (cv, WKG_CLOSE, x + w - D (14), cy + ch / 2, D (8), 0x60666E);
		g_homeHits.add (x, cy, w - D (26), ch, H_CHIP, i);
		g_homeHits.add (x + w - D (26), cy, D (26), ch, H_CHIPX, i);
		x += w + D (8);
	}
}

static void draw_results (Canvas &cv)
{
	int M = D (12), y0 = tabs_y () - D (4), x = M, w = g_aw - 2 * M, h = g_ah - y0 - M;
	uk_rbox (cv, x, y0, w, h, D (8), C_PANEL, uk_tone (C_PANEL, 122));
	int row = D (40), head = D (24);
	if (g_resSel >= g_nres) g_resSel = g_nres - 1;
	if (g_resSel < 0) g_resSel = 0;
	// the rows' places (the group heads between them), the selected one kept in view
	int Y[MAXRESULTS], yy = 0;
	for (int i = 0; i < g_nres; i++)
	{
		if (i == 0 || g_res[i].kind != g_res[i - 1].kind) yy += head;
		Y[i] = yy; yy += row;
	}
	if (g_resTop > g_resSel) g_resTop = g_resSel;
	while (g_resTop < g_resSel && Y[g_resSel] + row - Y[g_resTop] + head > h - D (12)) g_resTop++;
	static const char *const G[] = { TRN ("APPS"), TRN ("SETTINGS"), TRN ("FILES"), TRN ("RUN") };
	int base = (g_nres > 0 ? Y[g_resTop] : 0) - head - D (2);	// (the top row's group head shown above it)
	for (int i = g_resTop; i < g_nres; i++)
	{
		int ry = y0 + Y[i] - base;
		if (ry + row > y0 + h - D (4)) break;
		const Result &r = g_res[i];
		if (i == g_resTop || r.kind != g_res[i - 1].kind)
		{
			UkFaceScope f (g_small);
			text (cv, x + D (16), ry - head + D (6), TR (G[r.kind]), 0x7A808A, 2);
		}
		bool on = i == g_resSel;
		if (on) uk_hilite (cv, x + D (6), ry, w - D (12), row - D (2), D (6), true);
		unsigned ink = on ? uk_hilite_ink (true) : 0x14181E, dim = on ? uk_mix (uk_hilite_ink (true), C_ACCENT, 80) : 0x6A707A;
		int is = D (24), ix = x + D (16), iy = ry + (row - is) / 2;
		if (r.kind == R_APP || r.kind == R_SETTING) { App &a = g_apps[r.app]; draw_icon (cv, ix, iy, a, is); }
		else if (r.kind == R_RUN) icon_of (cv, ix, iy, "terminal", "T", is);
		else
		{
			char app[32];
			if (fa_app_for (r.path, app, sizeof app)) icon_of (cv, ix, iy, app, r.title, is);
			else icon_of (cv, ix, iy, "fileviewer", r.title, is);
		}
		text_fit (cv, ix + is + D (12), ry + D (3), w - is - D (110), r.title, ink, 2);
		{ UkFaceScope f (g_small); text_fit (cv, ix + is + D (12), ry + D (3) + g_fh, w - is - D (110), r.sub, dim); }
		if (on) text (cv, x + w - D (16) - tw (TR ("Enter"), 2), ry + (row - uk_fh ()) / 2, TR ("Enter"), ink, 2);
		g_homeHits.add (x, ry, w, row, H_RESULT, i);
	}
}

static void draw_home (void)
{
	Canvas &cv = g_hc;
	g_homeHits.clear ();
	for (int y = 0; y < g_ah; y++)					// the navy of the desktop, a gradient
		cv.fillRect (0, y, g_aw, 1, uk_mix (C_NAVY2, C_NAVY1, y * 256 / (g_ah > 1 ? g_ah : 1)));
	draw_search (cv);
	if (g_query[0]) draw_results (cv);
	else { draw_tabs (cv); draw_grid (cv); draw_strip (cv); }
	present (W_HOME);
	g_homeDirty = false;
}

// ---- the launcher's keys and clicks -------------------------------------------------------------------------
static void query_changed (void)
{
	g_resSel = 0; g_resTop = 0;
	search (g_query);
	g_homeDirty = true;
}

static void open_focused (void)
{
	if (g_query[0]) { if (g_resSel < g_nres) { Result r = g_res[g_resSel]; g_query[0] = 0; query_changed (); open_result (r); } return; }
	tab_items ();
	if (g_focus < g_nitems) open_app (g_apps[g_items[g_focus]].name);
}

static void set_tab (int t)
{
	if (g_ncats == 0) return;
	g_tab = (t + g_ncats) % g_ncats;
	g_focus = 0; g_gridTop = 0;
	g_homeDirty = true;
}

static void home_key (unsigned long, int ev, long v)
{
	if (ev != GUI_EVENT_KEY) return;
	static bool said;					// (the Pi's kmsg: the keys reach the launcher)
	if (!said) { said = true; ax_puts ("pocketshell: the launcher gets the keys\n"); }
	int k = (int) v;
	unsigned mods = (unsigned) kapi_get_modifiers ();
	Grid g = grid ();
	int ql = (int) strlen (g_query);
	switch (k)
	{
	case KEY_ENTER: open_focused (); return;
	case 0x1b:
		if (ql) { g_query[0] = 0; query_changed (); }
		else if (g_ntasks > 0) uk_shell_front (g_tasks[0].id, 1);	// (Home again: back to the app)
		return;
	case KEY_BACKSPACE:
		if (ql) { do ql--; while (ql > 0 && ((unsigned char) g_query[ql] & 0xC0) == 0x80); g_query[ql] = 0; query_changed (); }
		return;
	case '\t':
		if (!ql) set_tab (g_tab + ((mods & MOD_SHIFT) ? -1 : 1));
		return;
	case KEY_UP:	if (ql) g_resSel--; else if (g_focus >= g.cols) g_focus -= g.cols; g_homeDirty = true; return;
	case KEY_DOWN:	if (ql) g_resSel++; else if (g_focus + g.cols < g_nitems) g_focus += g.cols; g_homeDirty = true; return;
	case KEY_LEFT:	if (!ql) { if (g_focus > 0) g_focus--; else set_tab (g_tab - 1); g_homeDirty = true; } return;
	case KEY_RIGHT:	if (!ql) { if (g_focus + 1 < g_nitems) g_focus++; else set_tab (g_tab + 1); g_homeDirty = true; } return;
	case KEY_PGDN:	if (!ql) { g_focus += g.cols * g.rows; if (g_focus >= g_nitems) g_focus = g_nitems - 1; g_homeDirty = true; } return;
	case KEY_PGUP:	if (!ql) { g_focus -= g.cols * g.rows; if (g_focus < 0) g_focus = 0; g_homeDirty = true; } return;
	case KEY_HOME:	if (!ql) { g_focus = 0; g_homeDirty = true; } return;
	case KEY_END:	if (!ql) { g_focus = g_nitems - 1; g_homeDirty = true; } return;
	}
	if (k >= 32 && k < 256 && k != 127 && !(mods & MOD_CTRL) && ql < (int) sizeof g_query - 3)	// typing: the search
	{
		if (k < 128) g_query[ql++] = (char) k;
		else { g_query[ql++] = (char) (0xC0 | (k >> 6)); g_query[ql++] = (char) (0x80 | (k & 0x3F)); }	// (Latin-1 -> UTF-8)
		g_query[ql] = 0;
		query_changed ();
	}
}

static void home_click (int x, int y, bool right)
{
	const Hit *h = g_homeHits.at (x, y);
	if (h == 0) return;
	switch (h->kind)
	{
	case H_TAB: set_tab (h->i); break;
	case H_CHEV:
		if (h->i > 0) { if (g_tabFirst + 1 < g_ncats) g_tabFirst++; if (g_tab < g_tabFirst) set_tab (g_tabFirst); }
		else { g_tabFirst = 0; }
		g_homeDirty = true;
		break;
	case H_TILE:
		g_focus = h->i; g_homeDirty = true;
		if (!right) open_focused ();
		break;
	case H_CHIP: uk_shell_front (g_tasks[h->i].id, 1); break;
	case H_CHIPX: uk_win_close (g_tasks[h->i].id); break;
	case H_RESULT: g_resSel = h->i; open_focused (); break;
	}
}

// =========================================================================================================
// THE OVERLAYS: the switcher, quick settings (one topmost window over the work area, see-through)
// =========================================================================================================
static void over_show (int kind);
static void over_hide (void);

// ---- the switcher ------------------------------------------------------------------------------------------
static int g_sel;					// the card chosen
static bool g_held;					// opened by Alt+Tab, Alt still held: its release switches
static int g_cardFirst;					// the first card shown (more than fit)
struct Thumb { unsigned id; int w, h; unsigned *px; };
static Thumb g_thumbs[UK_TASKS_MAX];
enum { H_CARD = 20, H_CARDX, H_BACK };

static void thumbs_take (int tw_, int th)
{
	for (int i = 0; i < g_ntasks && i < UK_TASKS_MAX; i++)
	{
		Thumb &t = g_thumbs[i];
		if (t.px == 0 || t.w * t.h < tw_ * th) { delete [] t.px; t.px = new unsigned[tw_ * th]; }
		t.id = g_tasks[i].id; t.w = tw_; t.h = th;
		if (uk_shell_thumb (g_tasks[i].id, t.px, tw_, th) != 1)
			for (int k = 0; k < tw_ * th; k++) t.px[k] = 0x303844;
	}
}

static void blit_thumb (Canvas &cv, int x, int y, const Thumb &t, int w, int h)
{
	for (int j = 0; j < h; j++)
		for (int i = 0; i < w; i++)
			cv.pixel (x + i, y + j, t.px[(j * t.h / h) * t.w + i * t.w / w] & 0xFFFFFF);
}

static void switch_to (int i)
{
	if (i >= 0 && i < g_ntasks) uk_shell_front (g_tasks[i].id, 1);
	over_hide ();
}

static void draw_switcher (void)
{
	Canvas &cv = g_oc;
	g_overHits.clear ();
	uk_paint_alpha (true);
	cv.clear (0x30000000u | 0x0A0F18);				// the dim (a fifth of what is below shows)
	g_overHits.add (0, 0, g_aw, g_ah, H_BACK, 0);
	int M = D (16);
	{
		UkFaceScope f (g_big);
		const char *t = TR ("Open apps -- the most recent first");
		uk_text_over (cv, (g_aw - tw (t, 2)) / 2, D (14), t, 0xFFFFFF, 2, 1);
	}
	if (g_ntasks == 0)
	{
		const char *t = TR ("No open apps.");
		uk_text_over (cv, (g_aw - tw (t)) / 2, g_ah / 2 - uk_fh (), t, 0xD8DEE8, 0, 1);
	}
	if (g_sel >= g_ntasks) g_sel = g_ntasks - 1;
	if (g_sel < 0) g_sel = 0;
	if (!portrait ())						// landscape: a row of cards
	{
		int nfit = (LW () - 40) / 150;
		if (nfit < 1) nfit = 1;
		int slot = (g_aw - 2 * M) / (nfit < g_ntasks ? nfit : (g_ntasks > 0 ? g_ntasks : 1));
		if (slot > D (210)) slot = D (210);
		int n = g_ntasks < nfit ? g_ntasks : nfit;
		if (g_sel < g_cardFirst) g_cardFirst = g_sel;
		if (g_sel >= g_cardFirst + n) g_cardFirst = g_sel - n + 1;
		int tw_ = slot - D (20), th = tw_ * g_ah / (g_aw > 0 ? g_aw : 1);
		if (th > g_ah / 2) { th = g_ah / 2; tw_ = th * g_aw / g_ah; }
		int x0 = (g_aw - n * slot) / 2, cy = (g_ah - th) / 2 - D (10);
		thumbs_take (tw_, th);
		for (int k = 0; k < n; k++)
		{
			int i = g_cardFirst + k;
			const uk_shell_task &t = g_tasks[i];
			int x = x0 + k * slot + (slot - tw_) / 2;
			bool on = i == g_sel;
			int ix = x, iy = cy, iw = tw_, ih = th;
			if (on)							// the chosen one bigger, on the accent
			{
				int g = D (10);
				ix -= g; iy -= g; iw += 2 * g; ih += 2 * g;
				uk_rbox (cv, ix - D (6), iy - D (6), iw + D (12), ih + D (12) + g_fh + D (16), D (10), uk_tone (C_ACCENT, 150), uk_tone (C_ACCENT, 120));
			}
			blit_thumb (cv, ix, iy, g_thumbs[i], iw, ih);
			uk_rline (cv, ix, iy, iw, ih, 0, on ? 0xFFFFFF : 0x50586A);
			const char *l = task_label (t);
			int ny = iy + ih + D (8);
			icon_of (cv, ix, ny, t.name, l, D (20));
			if (on) text_fit (cv, ix + D (26), ny + (D (20) - uk_fh ()) / 2, iw - D (26), l, 0xFFFFFF, 2);
			else uk_text_over (cv, ix + D (26), ny + (D (20) - uk_fh ()) / 2, l, 0xF0F2F5, 0, 1);
			if (i == 0 && (t.flags & UK_TASK_FRONT)) { UkFaceScope f (g_small); uk_text_over (cv, ix, iy - g_fh - D (2), TR ("now"), 0xD8DEE8, 2, 1); }
			if (on)							// its close bead
			{
				int d = D (22), bx = ix + iw - d / 2, by = iy - d / 2;
				uk_rbox (cv, bx, by, d, d, d / 2, 0x3A3F48, 0x24282F);
				uk_rline (cv, bx, by, d, d, d / 2, 0xE0E4EA);
				uk_glyph (cv, WKG_CLOSE, bx + d / 2, by + d / 2, D (8), 0xFFFFFF);
				g_overHits.add (ix - D (6), iy - D (6), iw + D (12), ih + D (12) + g_fh + D (16), H_CARD, i);
				g_overHits.add (bx, by, d, d, H_CARDX, i);
			}
			else g_overHits.add (ix, iy, iw, ih + D (32), H_CARD, i);
		}
	}
	else								// portrait: a column of rows
	{
		int row = D (64), tw_ = D (80), th = D (48);
		int top = D (14) + D (30), n = (g_ah - top - D (60)) / row;
		if (n < 1) n = 1;
		if (g_sel < g_cardFirst) g_cardFirst = g_sel;
		if (g_sel >= g_cardFirst + n) g_cardFirst = g_sel - n + 1;
		thumbs_take (tw_, th);
		for (int k = 0; k < n && g_cardFirst + k < g_ntasks; k++)
		{
			int i = g_cardFirst + k, y = top + k * row, x = M;
			const uk_shell_task &t = g_tasks[i];
			bool on = i == g_sel;
			uk_rbox (cv, x, y, g_aw - 2 * M, row - D (6), D (8), on ? uk_tone (C_ACCENT, 150) : 0x343B48, on ? uk_tone (C_ACCENT, 120) : 0x2A303B);
			blit_thumb (cv, x + D (6), y + (row - D (6) - th) / 2, g_thumbs[i], tw_, th);
			const char *l = task_label (t);
			text_fit (cv, x + tw_ + D (16), y + D (8), g_aw - 2 * M - tw_ - D (60), l, 0xFFFFFF, 2);
			{ UkFaceScope f (g_small); text_fit (cv, x + tw_ + D (16), y + D (10) + g_fh, g_aw - 2 * M - tw_ - D (60), t.title, 0xD8DEE8); }
			uk_glyph (cv, WKG_CLOSE, g_aw - M - D (18), y + (row - D (6)) / 2, D (9), 0xFFFFFF);
			g_overHits.add (x, y, g_aw - 2 * M - D (36), row - D (6), H_CARD, i);
			g_overHits.add (g_aw - M - D (36), y, D (36), row - D (6), H_CARDX, i);
		}
	}
	static const char *const H[] = { TRN ("Alt+Tab"), TRN ("next"), TRN ("Shift+Tab"), TRN ("back"), TRN ("Del"), TRN ("close the app"), TRN ("Esc"), TRN ("stay") };
	static const char *const H2[] = { TRN ("Tab"), TRN ("next"), TRN ("Del"), TRN ("close"), TRN ("Esc"), TRN ("stay") };
	{
		UkFaceScope f (g_small);
		bool wide = LW () >= 560;
		int w = hints (cv, 0, 0, wide ? H : H2, wide ? 8 : 6, 0, true, true);
		hints (cv, (g_aw - w) / 2, g_ah - D (34), wide ? H : H2, wide ? 8 : 6, 0xE8ECF2, true);
	}
	uk_paint_alpha (false);
	present (W_OVER);
}

static void switcher_key (int code, unsigned mods)
{
	switch (code)
	{
	case '\t': g_sel += (mods & MOD_SHIFT) ? -1 : 1; break;
	case KEY_RIGHT: case KEY_DOWN: g_sel++; break;
	case KEY_LEFT: case KEY_UP: g_sel--; break;
	case KEY_ENTER: case ' ': switch_to (g_sel); return;
	case 0x1b: over_hide (); return;
	case KEY_DEL: if (g_sel < g_ntasks) uk_win_close (g_tasks[g_sel].id); return;
	case UK_SHELL_KEY_HELD: if (g_held && !(mods & MOD_ALT)) switch_to (g_sel); return;
	default: return;
	}
	if (g_ntasks > 0) g_sel = (g_sel + g_ntasks) % g_ntasks;
	g_overDirty = true;
}

// ---- quick settings and the notifications --------------------------------------------------------------------
struct Note { char title[64]; char text[200]; char action[124]; unsigned at; };
#define MAXNOTES 20
static Note g_notes[MAXNOTES];				// the newest first
static int g_nnotes;
static bool g_dnd;					// do not disturb: no toast
static int g_vol = 7, g_mute;
static bool g_volDrag;
enum { H_WIFI = 40, H_DND, H_SOUND, H_MODE, H_VOL, H_CLEAR, H_NOTE, H_CONTROL, H_LOCK, H_POWER, H_PANEL };
static int g_volX0, g_volX1;

static const char *const WD[] = { TRN ("Sunday"), TRN ("Monday"), TRN ("Tuesday"), TRN ("Wednesday"), TRN ("Thursday"), TRN ("Friday"), TRN ("Saturday") };
static const char *const MO[] = { TRN ("January"), TRN ("February"), TRN ("March"), TRN ("April"), TRN ("May"), TRN ("June"), TRN ("July"),
				  TRN ("August"), TRN ("September"), TRN ("October"), TRN ("November"), TRN ("December") };

static void num2 (char *o, int v) { o[0] = (char) ('0' + v / 10 % 10); o[1] = (char) ('0' + v % 10); o[2] = 0; }

static void panel_rect (int *x, int *y, int *w)
{
	*w = LW () < 420 ? g_aw - 2 * D (8) : D (340);
	*x = g_aw - *w - D (8); *y = D (6);
}

static void tile (Canvas &cv, int x, int y, int w, int h, bool on, const char *title, const char *sub, int kind)
{
	if (on) uk_rbox (cv, x, y, w, h, D (8), uk_tone (C_ACCENT, 150), uk_tone (C_ACCENT, 118));
	else { uk_rbox (cv, x, y, w, h, D (8), 0xFAFAFB, 0xE6E8EC); uk_rline (cv, x, y, w, h, D (8), 0xB8BEC8); }
	unsigned ink = on ? 0xFFFFFF : 0x1A1E24;
	text_fit (cv, x + D (12), y + D (5), w - D (16), title, ink, 2);
	{ UkFaceScope f (g_small); text_fit (cv, x + D (12), y + D (7) + g_fh, w - D (16), sub, on ? 0xE8F0FC : 0x5A606A); }
	g_overHits.add (x, y, w, h, kind, 0);
}

static void ago (unsigned at, char *o)				// "now", "2 min", "1 h"
{
	unsigned s = (kapi_get_ticks () - at) / 100;
	if (s < 60) { strcpy (o, TR ("now")); return; }
	unsigned m = s / 60;
	int k = 0;
	char d[12];
	if (m < 60) { int i = 0; do { d[i++] = (char) ('0' + m % 10); m /= 10; } while (m); while (i) o[k++] = d[--i]; strcpy (o + k, " min"); return; }
	unsigned h = m / 60;
	int i = 0; do { d[i++] = (char) ('0' + h % 10); h /= 10; } while (h); while (i) o[k++] = d[--i]; strcpy (o + k, " h");
}

static void draw_quick (void)
{
	Canvas &cv = g_oc;
	g_overHits.clear ();
	uk_paint_alpha (true);
	cv.clear (CATCH);
	g_overHits.add (0, 0, g_aw, g_ah, H_BACK, 0);
	int px, py, pw;
	panel_rect (&px, &py, &pw);
	int pad = D (12), x = px + pad, w = pw - 2 * pad;
	int notesH = g_nnotes == 0 ? D (40) : 0;
	for (int i = 0; i < g_nnotes; i++) notesH += D (50) + D (6);
	int fixed = D (40) + 2 * (D (48) + D (8)) + D (34) + D (12) + D (28) + D (46) + 2 * pad;
	int ph = fixed + notesH;
	if (ph > g_ah - D (12)) ph = g_ah - D (12);
	uk_rbox (cv, px, py, pw, ph, D (10), 0xF6F6F7, 0xE9EAED);
	uk_rline (cv, px, py, pw, ph, D (10), 0x8A909A);
	g_overHits.add (px, py, pw, ph, H_PANEL, 0);
	int y = py + pad;
	// the date, the time
	int yy = 2026, mo = 1, dd = 1, hh = 0, mi = 0;
	kapi_get_datetime (&yy, &mo, &dd, &hh, &mi, 0);
	{
		int a = (14 - mo) / 12, yr = yy - a, m = mo + 12 * a - 2;
		int wd = (dd + yr + yr / 4 - yr / 100 + yr / 400 + 31 * m / 12) % 7;
		char date[64]; int k = 0; char d[4]; num2 (d, dd);
		lx_cat (date, sizeof date, &k, TR (WD[wd])); lx_cat (date, sizeof date, &k, " ");
		lx_cat (date, sizeof date, &k, d[0] == '0' ? d + 1 : d); lx_cat (date, sizeof date, &k, " ");
		lx_cat (date, sizeof date, &k, TR (MO[(mo + 11) % 12]));
		text_fit (cv, x, y + D (6), w - D (80), date, 0x14181E, 2);
		char t[8]; num2 (t, hh); t[2] = ':'; num2 (t + 3, mi);
		UkFaceScope f (g_big);
		text (cv, x + w - tw (t, 2), y + D (4), t, 0x14181E, 2);
	}
	y += D (40);
	// the tiles
	int tw2 = (w - D (8)) / 2, th = D (48);
	char ip[40] = "";
	bool net = kapi_net_status (ip, sizeof ip) != 0;
	tile (cv, x, y, tw2, th, net, TR ("Wi-Fi"), net ? (ip[0] ? ip : TR ("connected")) : TR ("not connected"), H_WIFI);
	tile (cv, x + tw2 + D (8), y, tw2, th, g_dnd, TR ("Do not disturb"), g_dnd ? TR ("on") : TR ("off"), H_DND);
	y += th + D (8);
	tile (cv, x, y, tw2, th, !g_mute, TR ("Sound"), g_mute ? TR ("muted") : TR ("on"), H_SOUND);
	tile (cv, x + tw2 + D (8), y, tw2, th, false, TR ("Mode"), TR ("Pocket"), H_MODE);
	y += th + D (8);
	// the volume
	{
		int sx = x + D (2), sy = y + D (8), s = D (14);			// a speaker
		cv.fillRect (sx, sy + s / 3, s / 3, s / 3 + 1, 0x30343A);
		for (int k = 0; k < s / 2; k++) cv.fillRect (sx + s / 3 + k, sy + s / 3 - k * 2 / 3, 1, s / 3 + 1 + k * 4 / 3, 0x30343A);
		g_volX0 = x + D (30); g_volX1 = x + w - D (8);
		int tw_ = g_volX1 - g_volX0, fill = g_mute ? 0 : tw_ * g_vol / 10, kw = D (18);
		uk_slider_mark (cv, g_volX0, y + D (4), tw_, D (22), fill, g_volX0 + fill - kw / 2, kw, g_volDrag ? UK_HOT : UK_NORMAL);
		g_overHits.add (g_volX0 - D (10), y, tw_ + D (20), D (30), H_VOL, 0);
	}
	y += D (34) + D (6);
	cv.fillRect (x, y, w, 1, 0xC8CCD4);
	y += D (6);
	// the notifications
	text (cv, x, y + D (4), TR ("Notifications"), 0x14181E, 2);
	if (g_nnotes > 0)
	{
		const char *c = TR ("Clear all");
		text (cv, x + w - tw (c), y + D (4), c, C_ACCENT);
		g_overHits.add (x + w - tw (c) - D (6), y, tw (c) + D (12), D (26), H_CLEAR, 0);
	}
	y += D (28);
	int bottom = py + ph - pad - D (34) - D (8);
	if (g_nnotes == 0) { UkFaceScope f (g_small); text (cv, x + D (4), y + D (8), TR ("No notifications."), 0x6A707A); }
	for (int i = 0; i < g_nnotes && y + D (50) <= bottom; i++)
	{
		Note &n = g_notes[i];
		uk_rbox (cv, x, y, w, D (50), D (8), 0xFFFFFF, 0xF8F8FA);
		uk_rline (cv, x, y, w, D (50), D (8), 0xC4C9D2);
		const char *app = n.action[0] ? n.action : n.title;
		char an[32]; int k = 0;
		while (app[k] && app[k] != ' ' && k < 31) { an[k] = app[k]; k++; }
		an[k] = 0;
		App *a = 0;
		for (int j = 0; j < g_napps && a == 0; j++) if (ieq (g_apps[j].label, n.title) || ieq (g_apps[j].name, an)) a = &g_apps[j];
		icon_of (cv, x + D (8), y + D (11), a ? a->name : "", n.title, D (28));
		char t[16]; ago (n.at, t);
		{ UkFaceScope f (g_small); text (cv, x + w - D (10) - tw (t), y + D (6), t, 0x6A707A); }
		text_fit (cv, x + D (46), y + D (5), w - D (100), n.title, 0x14181E, 2);
		{ UkFaceScope f (g_small); text_fit (cv, x + D (46), y + D (8) + g_fh, w - D (56), n.text, 0x40464E); }
		g_overHits.add (x, y, w, D (50), H_NOTE, i);
		y += D (50) + D (6);
	}
	// the Control Panel, Lock, Power
	static const char *const B[] = { TRN ("Control Panel"), TRN ("Lock"), TRN ("Power") };
	static const int G[] = { WKG_GEAR, WKG_LOCK, WKG_POWER };
	// each button its glyph and its word -- the words left out, the small ones' first, when they do not fit
	int by = py + ph - pad - D (34), need[3], gap = D (8);
	for (int i = 0; i < 3; i++) need[i] = D (36) + tw (TR (B[i]), 2);
	bool words[3] = { true, true, true };
	if (need[0] + need[1] + need[2] + 2 * gap > w) { words[1] = words[2] = false; need[1] = need[2] = D (44); }
	if (w - 2 * gap - need[1] - need[2] < D (100)) { words[0] = false; need[0] = (w - 2 * gap) / 3; need[1] = need[2] = need[0]; }	// (else its word cut)
	int BW[3] = { w - 2 * gap - need[1] - need[2], need[1], need[2] }, bx = x;	// (the Control Panel the widest)
	for (int i = 0; i < 3; i++)
	{
		int bw = BW[i];
		if (i > 0) bx += BW[i - 1] + gap;
		uk_framed (cv, bx, by, bw, D (34), C_BUTTON, UK_NORMAL);
		const char *l = TR (B[i]);
		unsigned gc = i == 2 ? 0xD8402E : 0x30343A;
		if (!words[i]) uk_glyph (cv, G[i], bx + bw / 2, by + D (17), D (14), gc);
		else
		{
			int lw = D (20) + tw (l, 2);
			if (lw > bw - D (8)) lw = bw - D (8);
			int lx = bx + (bw - lw) / 2;
			uk_glyph (cv, G[i], lx + D (7), by + D (17), D (14), gc);
			text_fit (cv, lx + D (20), by + (D (34) - uk_fh ()) / 2, bw - D (28), l, C_BUTTON_TEXT, 2);
		}
		g_overHits.add (bx, by, bw, D (34), H_CONTROL + i, 0);
	}
	uk_paint_alpha (false);
	present (W_OVER);
}

static void vol_at (int x)
{
	int v = ((x - g_volX0) * 10 + (g_volX1 - g_volX0) / 2) / (g_volX1 - g_volX0 > 0 ? g_volX1 - g_volX0 : 1);
	v = v < 0 ? 0 : v > 10 ? 10 : v;
	int r = kapi_sound_volume (v, 0);
	g_vol = r & 0xFF; g_mute = (r & 0x100) ? 1 : 0;
	g_overDirty = true;
}

static void quick_click (const Hit *h, int x)
{
	switch (h->kind)
	{
	case H_BACK: over_hide (); break;
	case H_WIFI: over_hide (); if (uk_win_app_raise ("wifimenu") == 0) lx_launch ("wifimenu", 0); break;
	case H_DND: g_dnd = !g_dnd; g_overDirty = true; break;
	case H_SOUND: { int r = kapi_sound_volume (-1, g_mute ? 0 : 1); g_mute = (r & 0x100) ? 1 : 0; volume_save (g_vol, g_mute); g_overDirty = true; break; }
	case H_MODE: over_hide (); open_app ("modeconf"); break;
	case H_VOL: g_volDrag = true; vol_at (x); break;
	case H_CLEAR: g_nnotes = 0; g_overDirty = true; break;
	case H_NOTE:
		{
			Note n = g_notes[h->i];
			for (int i = h->i; i + 1 < g_nnotes; i++) g_notes[i] = g_notes[i + 1];
			g_nnotes--;
			g_overDirty = true;
			if (n.action[0])
			{
				char app[48]; int k = 0; const char *a = n.action;
				while (*a && *a != ' ' && k < 47) app[k++] = *a++;
				app[k] = 0;
				while (*a == ' ') a++;
				over_hide ();
				lx_launch (app, a);
			}
			break;
		}
	case H_CONTROL: over_hide (); open_app ("control"); break;
	case H_LOCK: over_hide (); lx_launch ("lock", 0); break;
	case H_POWER: over_hide (); lx_launch ("shutdown", 0); break;
	}
}

// ---- the overlay shown, hidden ----------------------------------------------------------------------------
static void over_show (int kind)
{
	if (g_over != kind) g_cardFirst = 0;
	g_over = kind;
	uk_win_select (W_OVER);
	uk_win_resize (g_aw, g_ah);
	uk_win_move (g_ax, g_ay);
	uk_win_alpha (255);
	uk_win_raise (0);
	uk_win_select (0);
	g_oc.adopt (g_oc.px, g_aw, g_ah, g_overStride);
	uk_shell_grab (1);
	tasks_read ();
	if (kind == OV_QUICK) { int r = kapi_sound_volume (-1, -1); g_vol = r & 0xFF; g_mute = (r & 0x100) ? 1 : 0; }
	g_overDirty = true;
}

static void over_hide (void)
{
	if (g_over == OV_NONE) return;
	g_over = OV_NONE; g_held = false; g_volDrag = false;
	uk_shell_grab (0);
	uk_win_select (W_OVER);
	uk_win_alpha (0);
	uk_win_move (-g_sw - 50, g_ay);					// parked: never catches a click
	uk_win_select (0);
}

// ---- the toast --------------------------------------------------------------------------------------------
static int g_toastState;				// 0 none, 1 in, 2 held, 3 out
static unsigned g_toastT;
static int g_toastNote = -1;
static int toast_w (void) { int w = D (330); return w > g_aw - D (16) ? g_aw - D (16) : w; }

static void toast_show (void)
{
	if (g_nnotes == 0) return;
	Canvas &cv = g_tc;
	int w = toast_w (), h = D (74);
	uk_paint_alpha (true);
	cv.clear (CLEAR);
	uk_rbox (cv, 0, 0, w, h, D (8), uk_tone (C_FIELD, 140), C_FIELD);
	uk_rline (cv, 0, 0, w, h, D (8), uk_tone (C_FACE, 70), 200);
	uk_paint_alpha (false);
	uk_rbox (cv, D (7), D (9), D (4), h - D (18), D (2), uk_tone (C_ACCENT, 150), uk_tone (C_ACCENT, 110));
	Note &n = g_notes[0];
	text_fit (cv, D (18), D (8), w - D (30), n.title, C_FIELD_TEXT, 2);
	int st[3], ln[3];
	int k = uk_text_wrap (n.text, (int) strlen (n.text), w - D (30), 2, st, ln);
	for (int i = 0; i < k; i++)
	{
		char l[200]; int m = ln[i] < 199 ? ln[i] : 199;
		memcpy (l, n.text + st[i], (size_t) m); l[m] = 0;
		text (cv, D (18), D (12) + g_fh * (i + 1), l, uk_mix (C_FIELD, C_FIELD_TEXT, 190));
	}
	uk_win_select (W_TOAST);
	uk_win_move (g_ax + g_aw - w - D (8), g_ay + D (8));
	uk_win_alpha (0);
	uk_win_raise (0);
	uk_win_present ();
	uk_win_select (0);
	g_toastState = 1; g_toastT = kapi_get_ticks (); g_toastNote = 0;
}

static void toast_tick (void)
{
	if (g_toastState == 0) return;
	unsigned t = (kapi_get_ticks () - g_toastT) * 10;		// ms
	uk_win_select (W_TOAST);
	if (g_toastState == 1) { uk_win_alpha (t >= 180 ? 240 : (int) (t * 240 / 180)); if (t >= 180) { g_toastState = 2; g_toastT = kapi_get_ticks (); } }
	else if (g_toastState == 2) { if (t >= 4000) { g_toastState = 3; g_toastT = kapi_get_ticks (); } }
	else
	{
		uk_win_alpha (t >= 600 ? 0 : 240 - (int) (t * 240 / 600));
		if (t >= 600) { g_toastState = 0; uk_win_move (-g_sw - 50, g_ay); }
	}
	uk_win_select (0);
}

static void note_add (const char *buf, int n)
{
	Note q;
	memset (&q, 0, sizeof q);
	int i = 0, k = 0;
	for (; i < n && buf[i] && k < (int) sizeof q.title - 1; i++) q.title[k++] = buf[i];
	while (i < n && buf[i]) i++;
	i++; k = 0;
	for (; i < n && buf[i] && k < (int) sizeof q.text - 1; i++) q.text[k++] = buf[i];
	while (i < n && buf[i]) i++;
	i++; k = 0;
	for (; i < n && buf[i] && k < (int) sizeof q.action - 1; i++) q.action[k++] = buf[i];
	q.at = kapi_get_ticks ();
	if (g_nnotes < MAXNOTES) g_nnotes++;
	for (int j = g_nnotes - 1; j > 0; j--) g_notes[j] = g_notes[j - 1];
	g_notes[0] = q;
	if (g_over == OV_QUICK) g_overDirty = true;
	else if (!g_dnd) toast_show ();
}

// =========================================================================================================
// THE SHELL'S EVENTS, KEYS, MESSAGES
// =========================================================================================================
static void area_read (void)
{
	struct uk_win_server_info si;
	memset (&si, 0, sizeof si);
	si.size = sizeof si;
	uk_win_server (&si);
	if (si.screen_w > 0) { g_sw = si.screen_w; g_sh = si.screen_h; }
	if (si.work_w > 0 && si.work_h > 0) { g_ax = si.work_x; g_ay = si.work_y; g_aw = si.work_w; g_ah = si.work_h; }
	if (g_aw > g_sw) g_aw = g_sw;
	if (g_ah > g_sh) g_ah = g_sh;
}

static void relayout (void)
{
	area_read ();
	uk_win_select (W_HOME);
	uk_win_resize (g_aw, g_ah);
	uk_win_select (0);
	g_hc.adopt (g_hc.px, g_aw, g_ah, g_homeStride);
	if (g_over != OV_NONE) over_show (g_over);
	g_homeDirty = true;
}

static void go_home (bool search_)
{
	if (search_) { g_query[0] = 0; query_changed (); }
	uk_shell_front (0, 0);
	g_homeDirty = true;
}

static void toggle_home (void)
{
	if (g_over != OV_NONE) over_hide ();
	tasks_read ();
	if (g_home && g_ntasks > 0) uk_shell_front (g_tasks[0].id, 1);	// (Home again: back to the app)
	else go_home (false);
}

static void open_switcher (bool held)
{
	tasks_read ();
	g_held = held;
	int front = -1;
	for (int i = 0; i < g_ntasks; i++) if (g_tasks[i].flags & UK_TASK_FRONT) front = i;
	g_sel = front == 0 && g_ntasks > 1 ? 1 : 0;
	over_show (OV_SWITCHER);
}

static void shell_event (unsigned long, int ev, long v)
{
	if (ev == UK_SHELL_EVENT)
	{
		if (v == UK_SHELL_EV_AREA) relayout ();
		else { tasks_read (); g_homeDirty = true; if (g_over == OV_SWITCHER) g_overDirty = true; }
		return;
	}
	if (ev != UK_SHELL_KEYEV) return;
	unsigned mods = UK_SHELL_KEY_MODS (v);
	int code = UK_SHELL_KEY_CODE (v);
	if (g_over == OV_SWITCHER)
	{
		if (code == UK_SHELL_KEY_SUPER) { over_hide (); go_home (false); return; }
		switcher_key (code, mods);
		return;
	}
	if (g_over == OV_QUICK)
	{
		if (code == 0x1b || (code == 'n' && (mods & UK_SHELL_MOD_SUPER))) over_hide ();
		else if (code == UK_SHELL_KEY_SUPER) { over_hide (); go_home (false); }
		else if (code == '\t' && (mods & MOD_ALT)) { over_hide (); open_switcher (true); }
		return;
	}
	if (code == '\t' && (mods & MOD_ALT)) { open_switcher (true); if (mods & MOD_SHIFT) { g_sel = g_ntasks - 1; g_overDirty = true; } return; }
	if (code == UK_SHELL_KEY_SUPER || (code == KEY_F1 && (mods & MOD_ALT)) || (code == 0x1b && (mods & MOD_CTRL))) { toggle_home (); return; }
	if (code == ' ' && (mods & UK_SHELL_MOD_SUPER)) { go_home (true); return; }
	if (code == 'n' && (mods & UK_SHELL_MOD_SUPER)) { over_show (OV_QUICK); return; }
}

static void ptr (unsigned long win, int ev, long v)
{
	if (ev == GUI_EVENT_DISPLAY_RESIZE)
	{
		int w = GUI_DISPLAY_W (v), h = GUI_DISPLAY_H (v), st = w;
		g_sw = w; g_sh = h;
		uk_win_select (W_HOME);
		unsigned *fb = uk_win_resize2 (w, h, &st);
		if (fb) { g_hc.adopt (fb, w, h, st); g_homeStride = st; }
		uk_win_select (W_OVER);
		fb = uk_win_resize2 (w, h, &st);
		if (fb) { g_oc.adopt (fb, w, h, st); g_overStride = st; }
		uk_win_select (0);
		relayout ();
		return;
	}
	int x = GUI_PTR_X (v), y = GUI_PTR_Y (v), c = GUI_PTR_CHANGED (v);
	if (win == W_HOME)
	{
		if (ev == GUI_EVENT_PTR_DOWN && (c & 3)) home_click (x, y, (c & 2) != 0);
		if (ev == GUI_EVENT_PTR_WHEEL && !g_query[0])
		{
			Grid g = grid ();
			g_focus -= GUI_PTR_WHEEL (v) * g.cols;
			if (g_focus >= g_nitems) g_focus = g_nitems - 1;
			if (g_focus < 0) g_focus = 0;
			g_homeDirty = true;
		}
		return;
	}
	if (win == W_TOAST)
	{
		if (ev == GUI_EVENT_PTR_DOWN && g_toastState != 0 && g_nnotes > 0)
		{
			g_toastState = 3; g_toastT = kapi_get_ticks ();
			if (g_notes[0].action[0]) { Hit h = { 0, 0, 0, 0, H_NOTE, 0 }; quick_click (&h, 0); }
		}
		return;
	}
	if (win != W_OVER || g_over == OV_NONE) return;
	if (g_over == OV_QUICK)
	{
		if (ev == GUI_EVENT_PTR_MOVE && g_volDrag) vol_at (x);
		if (ev == GUI_EVENT_PTR_UP && g_volDrag) { g_volDrag = false; volume_save (g_vol, g_mute); }
		if (ev == GUI_EVENT_PTR_DOWN && (c & 1)) { const Hit *h = g_overHits.at (x, y); if (h) quick_click (h, x); }
		return;
	}
	if (ev == GUI_EVENT_PTR_DOWN && (c & 1))
	{
		const Hit *h = g_overHits.at (x, y);
		if (h == 0 || h->kind == H_BACK) over_hide ();
		else if (h->kind == H_CARD) switch_to (h->i);
		else if (h->kind == H_CARDX) uk_win_close (g_tasks[h->i].id);
	}
}

static void messages (void)
{
	static char buf[520];
	int from = 0, type = 0, n;
	while ((n = kapi_mailbox_recv (&from, &type, buf, sizeof buf - 1, 0)) >= 0)
	{
		buf[n] = 0;
		switch (type)
		{
		case NOTIFY_MSG_SHOW: note_add (buf, n); break;
		case SHELL_MSG_HOME: toggle_home (); break;
		case SHELL_MSG_SEARCH: over_hide (); go_home (true); break;
		case SHELL_MSG_SWITCHER: if (g_over == OV_SWITCHER) over_hide (); else open_switcher (false); break;
		case SHELL_MSG_QUICK: if (g_over == OV_QUICK) over_hide (); else over_show (OV_QUICK); break;
		}
	}
}

// The scale: SD:/etc/theme.txt "scale = 1 | 1.5 | 2", else from the screen (1.5 from 1000 lines, 2 from 1800).
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

int main (void)
{
	if (uk_shell_register () != 1)
	{
		ax_puts ("pocketshell: the pocket mode's shell -- PocketUI is not the graphics server here, or another shell runs\n");
		return 0;
	}
	kapi_screen_size (&g_sw, &g_sh);
	area_read ();
	uikit::init ();
	S = scale_of ();
	if (ft_uikit_install ("DejaVu Sans", D (13))) g_fh = uk_fh ();
	g_big = new FtTextFace; if (!g_big->open ("DejaVu Sans", D (16))) { delete g_big; g_big = 0; }
	g_small = new FtTextFace; if (!g_small->open ("DejaVu Sans", D (11))) { delete g_small; g_small = 0; }
	uk_lang_init ();
	colours ();

	// the home: backmost, at the work area (its canvas the screen's size: the area may grow)
	unsigned *fb = uk_win_create_ex (g_ax, g_ay, g_sw, g_sh, "pocketshell", WIN_FLAG_BACKMOST | WIN_FLAG_BORDERLESS | WIN_FLAG_SYSTEM);
	if (fb == 0) { ax_puts ("pocketshell: no window\n"); return 1; }
	g_homeStride = g_sw;
	uk_win_resize (g_aw, g_ah);
	g_hc.adopt (fb, g_aw, g_ah, g_homeStride);
	uk_win_on_pointer (ptr);
	uk_win_on_key (home_key);
	// the overlay (the switcher, quick settings) and the toast: topmost, see-through, parked off the screen
	unsigned *ob = 0, *tb = 0;
	// (made on the screen -- a place off it is "placed by the server" --, see-through, then parked off it)
	if (uk_win_new (0, g_ay, g_sw, g_sh, "pocketshell overlay", WIN_FLAG_TOPMOST | WIN_FLAG_BORDERLESS | WIN_FLAG_ALPHA | WIN_FLAG_SYSTEM, &ob) != W_OVER
	    || uk_win_new (0, g_ay, D (340), D (80), "pocketshell toast", WIN_FLAG_TOPMOST | WIN_FLAG_BORDERLESS | WIN_FLAG_ALPHA | WIN_FLAG_SYSTEM, &tb) != W_TOAST)
	{ ax_puts ("pocketshell: no overlay window\n"); return 1; }
	for (int i = 0; i < g_sw * g_sh; i++) ob[i] = CLEAR;
	for (int i = 0; i < D (340) * D (80); i++) tb[i] = CLEAR;
	g_overStride = g_sw;
	g_oc.adopt (ob, g_aw, g_ah, g_overStride);
	g_tc.adopt (tb, D (340), D (80));
	uk_win_select (W_OVER); uk_win_on_pointer (ptr); uk_win_alpha (0); uk_win_move (-g_sw - 50, g_ay); uk_win_present ();
	uk_win_select (W_TOAST); uk_win_on_pointer (ptr); uk_win_alpha (0); uk_win_move (-g_sw - 50, g_ay); uk_win_present ();
	uk_win_select (0);

	kapi_ipc_register (SHELL_SERVICE);
	if (!kapi_ipc_register (NOTIFY_SERVICE)) ax_puts ("pocketshell: another program serves the notifications (notifyd)\n");
	scan_apps ();
	recent_load ();
	tab_items ();
	static const int K[] = {
		UK_SHELL_KEY (MOD_ALT, '\t'), UK_SHELL_KEY (MOD_ALT | MOD_SHIFT, '\t'), UK_SHELL_KEY (0, UK_SHELL_KEY_SUPER),
		UK_SHELL_KEY (MOD_ALT, KEY_F1), UK_SHELL_KEY (MOD_CTRL, 0x1b), UK_SHELL_KEY (UK_SHELL_MOD_SUPER, 'n'),
		UK_SHELL_KEY (UK_SHELL_MOD_SUPER, ' ') };
	uk_shell_events (shell_event);
	uk_shell_keys (K, (int) (sizeof K / sizeof K[0]));
	tasks_read ();
	{ int r = kapi_sound_volume (-1, -1); g_vol = r & 0xFF; g_mute = (r & 0x100) ? 1 : 0; }

	int lastMin = -1;
	unsigned lastScan = kapi_get_ticks ();
	while (!kapi_should_exit ())
	{
		pump_events ();
		messages ();
		toast_tick ();
		int mi = 0;
		kapi_get_datetime (0, 0, 0, 0, &mi, 0);
		if (mi != lastMin) { lastMin = mi; if (g_over == OV_QUICK) g_overDirty = true; }
		if (kapi_get_ticks () - lastScan > 1000 && g_home && g_over == OV_NONE)	// (an app installed meanwhile: every 10 s at home)
		{
			lastScan = kapi_get_ticks ();
			static char list[4096], seen[4096];
			kapi_list_apps (list, sizeof list);
			if (strcmp (list, seen) != 0) { strcpy (seen, list); scan_apps (); recent_load (); g_homeDirty = true; }
		}
		if (g_homeDirty) draw_home ();
		if (g_overDirty)
		{
			g_overDirty = false;
			if (g_over == OV_SWITCHER) draw_switcher ();
			else if (g_over == OV_QUICK) draw_quick ();
		}
		msleep (g_over != OV_NONE || g_toastState != 0 ? 16 : 40);
	}
	return 0;
}
