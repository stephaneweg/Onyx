//
// consolehome -- the console mode's shell (PocketUI's console mode: the games, a television, a pad; phase P9 of
// docs/POCKETUI-TECH-STUDY.md; its home Lakka's XMB since 2026-10-09: docs/COMPACT-SHELL-STUDY.md §17, xmb.h).
//
// THE HOME (its backmost window, the whole screen: console mode has no band): across the upper third a white icon per
// CONSOLE that has games (GameKit: the installed emulators, the ROMs of the watched folders), then ONYX (the native
// games), APPS (the apps' categories, each unrolling its apps at its right) and SETTINGS (the console's own settings
// pages, made for a pad: xset.h); under the chosen one its items as a vertical list, a ROM's title screen big at the right. THE MENU (a
// topmost overlay, asked with the pad's Home or Select, F10, the Super key, or the shell's message): the app in front,
// Home, the other running apps (to switch to), Close this app, Settings, Shut Down -- over any app, since an app in
// console mode fills the screen with no chrome. A game (an emulator) gets its own screen size while it is in front.
//
// The pad first (gamepad.h): Left / Right the column, Up / Down the item, A plays / opens / enters, B goes back, L1 / R1
// the previous / next column, L2 / R2 a page, Home or Select the menu. The keyboard the same: the arrows, Enter, Esc or
// Backspace, Page Up / Down, Ctrl+Page Up / Down or Tab, F10 the menu. The mouse: a click chooses, a click on the chosen
// item opens it, the wheel moves the items. The bottom line says the buttons that work.
//
// It is PocketUI's shell (uk_shell_register): its keys come before the front app's (uk_shell_keys), every key while the
// menu is up (uk_shell_grab). Resolution independent: every size in logical units times a scale (SD:/etc/theme.txt
// "scale =", else 1.5 from a 1080-line screen up, 2 from 1800).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "appkit/appkit.h"
#include "systemkit/systemkit.h"
#include "filekit/filekit.h"
#include "uikit/uikit.h"
#include "uikit/bmp.h"
#include "fontkit/uikitface.h"
#include "gamepad.h"
#include "gamekit/gamekit.h"

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

// ---- the home's background: made once for a size (xmb.h) ---------------------------------------------------------------
static unsigned *g_bgPx; static int g_bgW, g_bgH;

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
enum { H_CAT = 1, H_ITEM, H_SUB, H_ROW, H_PAGE, H_DBTN, H_KEY };
static bool screen_of (const char *app, int *w, int *h);
static void screen_to (int w, int h, const char *who);
static char g_launch[32];				// an app just opened: its size before it comes in front (screen_follow)
static unsigned g_launchT;
// A hint's width (hint () below draws it): its button's pill and its word.
static int hint_w (const char *btn, const char *word)
{
	UkFaceScope f (F (12));
	int h = uk_fh () + D (8), bw = tw (btn, 2) + D (12);
	if (bw < h) bw = h;
	return bw + D (7) + tw (word) + D (22);
}
static bool g_setOn;					// the settings' pages are up (xset.h)
static void draw_settings (Canvas &cv);
static void set_enter (int page);
static void set_key (int k);
static void set_ptr (int ev, int x, int y, int c, long v);
#define SP_N	8					// the settings' pages (xset.h's SP)
static const char *sp_name (int i);
static const char *sp_help (int i);
static void page_icon (Canvas &cv, int i, int cx, int cy, int s, int a);
#include "xmb.h"				// the home: Lakka's XMB -- the consoles, Onyx, Apps, Settings across, their items down

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
// The overlays hidden: see-through (their pixels cleared) and parked off the screen -- a screen whose size changes
// (a game's resolution, then the system's again) puts every window back on it: a parked one must show nothing there
// (the user, 2026-10-09: "the app's menu stays when I quit the app").
static void park (int win, Canvas &c, int h)
{
	if (c.px) for (long i = 0; i < (long) c.stride * h; i++) c.px[i] = CLEAR;
	uk_win_select (win); uk_win_present (); uk_win_alpha (0); uk_win_move (-g_sw - 50, 0); uk_win_select (0);
}
static void menu_hide (void)
{
	if (!g_menu) return;
	g_menu = false;
	uk_shell_grab (0);
	park (W_OVER, g_oc, g_oc.h);
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
	if (g_setOn) { set_key (k == '\n' || k == '\r' ? KEY_ENTER : k); return; }
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
	if (g_setOn) set_ptr (ev, x, y, c, v);
	else home_ptr (ev, x, y, c, v);
	if (ev == GUI_EVENT_PTR_DOWN && (c & 2)) menu_show ();		// (a right click: the menu)
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
#include "xset.h"				// the settings' pages: Sound, Gamepad ... Display, in the XMB's style

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
	park (W_TIP, g_tc, g_tipH);
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
	if (mine && !g_menu && set_grabs_pad ()) return;		// (the buttons being learnt: the wizard reads them)
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
	bool set = g_setOn && !g_menu;				// (the settings: every button has its own code)
	unsigned d = press ? press : b;
	int k = (d & PAD_RIGHT) ? KEY_RIGHT : (d & PAD_LEFT) ? KEY_LEFT : (d & PAD_DOWN) ? KEY_DOWN : (d & PAD_UP) ? KEY_UP : 0;
	if (press & PAD_A) k = set ? K_A : KEY_ENTER;
	else if (press & PAD_START) k = set ? K_START : KEY_ENTER;
	else if (press & PAD_B) k = set ? K_B : 0x1b;
	else if (press & PAD_X) k = set ? K_X : 0;
	else if (press & PAD_Y) k = set ? K_Y : 0;
	else if (press & PAD_L3) k = set ? K_L3 : 0;
	else if (press & PAD_L) k = -1;
	else if (press & PAD_R) k = '\t';
	else if (press & PAD_L2) k = KEY_PGUP;
	else if (press & PAD_R2) k = KEY_PGDN;
	if (k == 0) return;
	if (k == KEY_UP || k == KEY_DOWN || k == KEY_LEFT || k == KEY_RIGHT) t0 = now + (press ? 22 : 0);
	if (g_menu) { if (k != -1 && k != '\t' && k != KEY_PGUP && k != KEY_PGDN) menu_key (k); }
	else if (set) set_key (k);
	else go (k);
}

// ---- the games' screen sizes ----------------------------------------------------------------------------------
// An app may want its own resolution on the console (an emulator: the size its picture scales best to, the GPU's
// work for the 3D ones): its app.txt's `resolution = 800x600`, or the
// user's SD:/etc/console.ini, section [screen], `<app> = 640x480` (`system`: the system's). While that app is in
// front the screen has its size (kapi_screen_set, switched before it starts when this shell opens it); at home, the
// game ended or another app in front, the system's size again (the one the screen had before).
static int g_sysW, g_sysH;
static bool screen_of (const char *app, int *w, int *h)	// the app's own size (SystemKit display.h) -> true (cached by name)
{
	static char s_app[32]; static int s_w, s_h; static unsigned s_t;
	if (!ieq (app, s_app) || kapi_get_ticks () - s_t > 500)	// (read again every 5 s: a file changed meanwhile)
	{
		fs_copy (s_app, app, sizeof s_app); s_t = kapi_get_ticks ();
		if (!display_game_size (app, &s_w, &s_h)) s_w = s_h = 0;
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
	if (!g_menu) park (W_OVER, g_oc, g_oc.h);		// (the server put them back on the new screen)
	if (g_tipOn) tip_hide (); else park (W_TIP, g_tc, g_tipH);	// (the tip: placed for the old size)
	if (g_menu) g_overDirty = true;
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
	roms_read ();
	xmb_build ();						// (the console wakes on its first console's games)
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
		set_tick ();
		int mi = 0;
		kapi_get_datetime (0, 0, 0, 0, &mi, 0);
		if (mi != lastMin) { lastMin = mi; g_homeDirty = true; }
		if (kapi_get_ticks () - lastSync >= 10) { lastSync = kapi_get_ticks (); screen_follow (); screen_sync (); }
		{							// back home (a game ended...): the ROMs read again, at most every 30 s
			static bool s_wasHome = true;
			if (g_home && !s_wasHome && kapi_get_ticks () - g_romsT > 3000) { roms_read (); g_thumbOf = -1; xmb_build (); }
			s_wasHome = g_home;
		}
		if (kapi_get_ticks () - lastScan > 1000 && g_home && !g_menu)		// (an app installed meanwhile: every 10 s at home)
		{
			lastScan = kapi_get_ticks ();
			static char list[4096], seen[4096];
			kapi_list_apps (list, sizeof list);
			if (strcmp (list, seen) != 0) { strcpy (seen, list); scan_apps (); xmb_build (); }
		}
		if (g_homeDirty && g_home) draw_home ();
		if (g_overDirty && g_menu) draw_menu ();
		tip_tick ();
		msleep (g_menu ? 16 : 30);
	}
	return 0;
}
