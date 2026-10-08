//
// menubar -- the system menu bar across the top of the screen (macOS-style).
//
// It shows the ACTIVE app's name and its menus (uk_win_menu_get, declared by the app with
// uikit::Menu / uk_win_menu_set), opens a drop-down on click and sends the chosen command
// back to the app (uk_win_menu_command). The first menu is always the "Onyx" system menu:
// Terminal / Control Panel / File Viewer / Task Manager, then one entry per app CATEGORY (the
// "category" of each SD:/apps/<name>.app/app.txt; the "Shell" components, the Control Panel's
// "Settings" applets and the "Emulators" -- reached from the Game Library -- are left out) opening a sub-menu
// of its apps, then "Open Windows" (a sub-menu: raise one), then Shut Down... -- it replaces
// the old left panel and app list. Then the app's name with "Quit" (MENU_QUIT), then its
// menus. Apps start through launch.h (their main, or main.bas / main.bax by a runner). The clock sits on the right, with the Wi-Fi state left of it (arcs = connected,
// a barred circle = not connected; polled about once a second through kapi_net_status) and the
// sound's volume left of that. A click on the speaker opens the volume box (a notification-style
// frame: a slider 0..10 and Mute; kapi v60 sound_volume, kept in SD:/etc/sound.ini by volume.h
// and applied at start); a click on the Wi-Fi icon opens the Wi-Fi menu (the wifimenu app: the
// networks around, join one).
//
// A USB stick plugged in (kapi v93: mounted by the kernel as USB1:, USB2:, USB3:, or USB1P1:.. per partition
// when it has several) shows a drive icon
// left of the speaker; a click on it opens the USB box: each stick, its label and size, an Eject
// button (Mount when it was ejected, Format... when it cannot be read), a click on its name opens it in
// the File Viewer; "Disks..." opens the Disks app. The bar also says what happened through notifyd:
// a stick connected, one that can be removed safely, one pulled out without an eject.
//
// A click on the time opens a calendar (the month; "Open Calendar" starts the Calendar app; "Alarms and timers..."
// opens the Clock on its Alarms tab -- when the Clock is on the card). A small bell left of the time says an alarm of
// the Clock rings within 24 hours (SD:/apps/clock.app/alarms.txt read once a minute, through the Clock's own model,
// Apps/clock/alarms.cpp -- AutoDev round 6, S1); a click on it opens the Clock's Alarms too.
//
// Its words are in the system's language (uikit/lang.h: TR, SD:/apps/menubar.app/lang/<code>.txt); the apps' names
// and the windows' titles are theirs.
//
// The look: the modernised CDE (uikit/paint.h) -- a light bar in the theme's menu bar colour, the open title
// in the accent, rounded drop-downs, anti-aliased on what lies below.
//
// The window is TOPMOST (always above the others, never active, never gets the keys)
// and see-through per pixel (WIN_FLAG_ALPHA): it is allocated full-screen but kept BAR_H tall;
// while a menu is open it grows to the whole screen, almost clear (a click anywhere else goes
// to it and closes the menu) except the bar and the drop-down.
//
// Mouse: click a title to open (click it again or anywhere outside to close), slide to
// another title to switch, click an item to run it; press on a title + release on an
// item works too.
//
#include <stdio.h>
#include "appkit/appkit.h"
#include "fontkit/uikitface.h"
#include "uikit/uikit.h"
#include "systemkit/systemkit.h"
#include "Apps/clock/alarms.h"
#include "Apps/clock/clocktime.h"

using namespace uikit;

#define BAR_H		30			// the bar's height (its last row: a dark line)
#define MAXMENUS	12
#define MAXITEMS	24
#define CLEAR		0xFF000000u		// a see-through pixel (the top byte: its transparency)
#define CATCH		0xFE000000u		// ... almost: it still takes the clicks (a menu is open)

// The palette: the theme's (uikit/theme.h), set once the theme is read.
static unsigned C_BARTXT, C_DROP, C_DIM, C_OUT;

struct Item { int id; char label[40]; char key[12]; bool sep; int sub; };	// sub: a sub-menu (g_subs) or -1
struct MenuDef { char title[24]; Item items[MAXITEMS]; int count; int x, w; };

static MenuDef g_menus[MAXMENUS];
static int g_nmenus = 0;
static char g_app[48] = "Onyx";
static bool g_onyx = true;		// no active app: our own Onyx menu

static int g_sw = 1024, g_sh = 768, g_fw = 8, g_fh = 16;
static unsigned *g_fb = 0;
static Canvas g_cv;
static int g_open = -1, g_hover = -1;	// open menu / hovered item index
static int g_titleHot = -1;		// the title under the pointer while no menu is open (a lighter back)
static bool g_dirty = true, g_pressedTitle = false;
static int g_lastMin = -1;
static int g_wifi = -1;			// last drawn Wi-Fi state (1 connected, 0 not)
static int g_vol = 10, g_mute = 0;	// the master volume, as last read from the kernel
static bool g_volOpen = false, g_volDrag = false;	// the volume box (and its slider held)
static bool g_usbOpen = false;		// the USB box
#define VW		236		// the volume box
#define VH		92

static void scopy (char *d, const char *s, int cap) { int i = 0; for (; s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = '\0'; }

// ---- the apps, by category (app.txt), and the open windows: the Onyx menu's sub-menus -------
#define MAXAPPS		128
#define MAXSUBS		12
#define SUBITEMS	40
struct AppInfo { char name[24]; char label[40]; char cat[20]; };
static AppInfo g_apps[MAXAPPS]; static int g_napps = 0;
static unsigned g_scanTick = 0; static bool g_scanned = false;
struct SubItem { char label[40]; char app[24]; };
struct SubDef { SubItem items[SUBITEMS]; int count; bool windows; };
static SubDef g_subs[MAXSUBS]; static int g_nsubs = 0;
static int g_sub = -1, g_subOwner = -1, g_subHover = -1;	// open sub-menu, the item it hangs on, hovered

static bool ci_less (const char *a, const char *b)
{
	for (;; a++, b++)
	{
		char x = (*a >= 'A' && *a <= 'Z') ? (char) (*a + 32) : *a, y = (*b >= 'A' && *b <= 'Z') ? (char) (*b + 32) : *b;
		if (x != y || !x) return x < y;
	}
}
static bool eq (const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }

// Read every app's app.txt: its friendly name and category (at most every 5 s, so an app
// made meanwhile -- QBasic > Make App -- shows up).
static void scan_apps (void)
{
	unsigned now = kapi_get_ticks ();
	if (g_scanned && now - g_scanTick < 500) return;
	g_scanned = true; g_scanTick = now;
	static char list[4096];
	kapi_list_apps (list, sizeof list);
	g_napps = 0;
	for (int i = 0; list[i] && g_napps < MAXAPPS; )
	{
		char name[24]; int n = 0;
		while (list[i] && list[i] != '\n') { if (n < 23) name[n++] = list[i]; i++; }
		if (list[i] == '\n') i++;
		name[n] = '\0';
		if (!n) continue;
		char path[80]; ax_app_path (path, sizeof path, name, ".app/app.txt");
		AppInfo &a = g_apps[g_napps];
		scopy (a.name, name, sizeof a.name);
		scopy (a.label, name, sizeof a.label); scopy (a.cat, "Other", sizeof a.cat);
		if (app_ini_load_path (path) >= 0)
		{
			scopy (a.label, app_ini_get (0, "name", name), sizeof a.label);
			scopy (a.cat, app_ini_get (0, "category", "Other"), sizeof a.cat);
		}
		if (eq (a.cat, "Shell") || eq (a.cat, "Settings") || eq (a.cat, "Emulators")) continue;	// (see above)
		g_napps++;
	}
}
static bool is_shell (const char *name)				// a desktop part (not an "open window")
{
	static const char *const own[] = { "menubar", "notifyd", "desktop", 0 };
	for (int i = 0; own[i]; i++) if (eq (own[i], name)) return true;
	char path[80]; ax_app_path (path, sizeof path, name, ".app/app.txt");
	return app_ini_load_path (path) >= 0 && eq (app_ini_get (0, "category", ""), "Shell");
}
static const char *label_of (const char *name)
{
	for (int i = 0; i < g_napps; i++) if (eq (g_apps[i].name, name)) return g_apps[i].label;
	return name;
}
static void sub_add (SubDef &sd, const char *label, const char *app)	// sorted by label
{
	if (sd.count >= SUBITEMS) return;
	int pos = sd.count;
	while (pos > 0 && ci_less (label, sd.items[pos - 1].label)) { sd.items[pos] = sd.items[pos - 1]; pos--; }
	scopy (sd.items[pos].label, label, sizeof sd.items[pos].label);
	scopy (sd.items[pos].app, app, sizeof sd.items[pos].app);
	sd.count++;
}
static void fill_windows (SubDef &sd)
{
	static char buf[1024];
	uk_win_apps (buf, sizeof buf);
	sd.count = 0;
	for (int i = 0; buf[i]; )
	{
		char name[24]; int n = 0;
		while (buf[i] && buf[i] != '\n') { if (n < 23) name[n++] = buf[i]; i++; }
		if (buf[i] == '\n') i++;
		name[n] = '\0';
		if (n && !is_shell (name)) sub_add (sd, label_of (name), name);
	}
}

// ---- menus ------------------------------------------------------------------------------
// Onyx system-menu item ids (>= 1000: handled here, never sent to the app).
enum { ONYX_TERMINAL = 1000, ONYX_FILES, ONYX_TASKS, ONYX_SHUTDOWN, ONYX_SUB, ONYX_CONTROL };

static void add_quit_menu (const char *app)
{
	MenuDef &m = g_menus[g_nmenus++];
	scopy (m.title, app, sizeof m.title); m.count = 0;
	Item &q = m.items[m.count++];
	q.id = MENU_QUIT; scopy (q.label, TR ("Quit"), sizeof q.label); scopy (q.key, "^Q", sizeof q.key); q.sep = false; q.sub = -1;
}

// The system menu, always first: tools, the apps by category, the open windows, the end.
static void build_onyx_menu (MenuDef &m)
{
	scan_apps ();
	scopy (m.title, "Onyx", sizeof m.title); m.count = 0;
	auto add = [&] (int id, const char *l, int sub)
	{
		if (m.count >= MAXITEMS) return;
		Item &x = m.items[m.count++];
		x.id = id; x.sep = id == -2; x.key[0] = '\0'; x.sub = sub;
		scopy (x.label, l ? l : "", sizeof x.label);
	};
	add (ONYX_TERMINAL, TR ("Terminal"), -1); add (ONYX_CONTROL, TR ("Control Panel"), -1);
	add (ONYX_FILES, TR ("File Viewer"), -1); add (ONYX_TASKS, TR ("Task Manager"), -1);
	add (-2, 0, -1);
	// the categories: the usual ones first, then any other, "Other" last
	g_nsubs = 0;
	static const char *const order[] = { TRN ("Productivity"), TRN ("Internet"), TRN ("Graphics"), TRN ("Programming"), TRN ("Games"), TRN ("BASIC"), TRN ("Demos"), TRN ("System"), 0 };
	// TR: Other
	// TR: Multimedia
	char cats[MAXSUBS][20]; int nc = 0;
	for (int i = 0; order[i]; i++) scopy (cats[nc++], order[i], 20);
	for (int a = 0; a < g_napps && nc < MAXSUBS - 2; a++)
	{
		bool have = eq (g_apps[a].cat, "Other");
		for (int c = 0; c < nc && !have; c++) have = eq (cats[c], g_apps[a].cat);
		if (!have) scopy (cats[nc++], g_apps[a].cat, 20);
	}
	scopy (cats[nc++], "Other", 20);
	for (int c = 0; c < nc && g_nsubs < MAXSUBS - 1; c++)
	{
		SubDef &sd = g_subs[g_nsubs]; sd.count = 0; sd.windows = false;
		for (int a = 0; a < g_napps; a++) if (eq (g_apps[a].cat, cats[c])) sub_add (sd, g_apps[a].label, g_apps[a].name);
		if (sd.count) add (ONYX_SUB, TR (cats[c]), g_nsubs++);	// (the category's name shown in the language; app.txt's kept)
	}
	add (-2, 0, -1);
	SubDef &w = g_subs[g_nsubs]; w.count = 0; w.windows = true;
	add (ONYX_SUB, TR ("Open Windows"), g_nsubs++);
	add (-2, 0, -1);
	add (ONYX_SHUTDOWN, TR ("Shut Down..."), -1);
}
static void add_onyx_menu (void) { build_onyx_menu (g_menus[g_nmenus++]); }

static void onyx_menu (void)			// no active app: just the system menu
{
	g_nmenus = 0; g_onyx = true;
	scopy (g_app, "Onyx", sizeof g_app);
	add_onyx_menu ();
}

static void parse (const char *spec, const char *title)
{
	g_nmenus = 0; g_onyx = false;
	scopy (g_app, title[0] ? title : "App", sizeof g_app);
	if (g_app[0] >= 'a' && g_app[0] <= 'z') g_app[0] = (char) (g_app[0] - 32);	// "tinypad" -> "Tinypad"
	add_onyx_menu ();
	add_quit_menu (g_app);
	const char *p = spec;
	MenuDef *cur = 0;
	while (*p)
	{
		const char *e = p; while (*e && *e != '\n') e++;
		if (*p == 'M' && g_nmenus < MAXMENUS)
		{
			cur = &g_menus[g_nmenus++]; cur->count = 0;
			int n = 0; for (const char *q = p + 1; q < e && n < (int) sizeof cur->title - 1; q++) cur->title[n++] = *q;
			cur->title[n] = '\0';
		}
		else if (*p == '-' && cur && cur->count < MAXITEMS)
		{
			Item &s = cur->items[cur->count++]; s.sep = true; s.id = -2; s.label[0] = s.key[0] = '\0'; s.sub = -1;
		}
		else if (*p == 'I' && cur && cur->count < MAXITEMS)
		{
			Item &it = cur->items[cur->count++];
			it.sep = false; it.id = 0; it.sub = -1;
			const char *q = p + 1;
			while (q < e && *q >= '0' && *q <= '9') it.id = it.id * 10 + (*q++ - '0');
			if (q < e && *q == '\t') q++;
			int n = 0; while (q < e && *q != '\t' && n < (int) sizeof it.label - 1) it.label[n++] = *q++;
			it.label[n] = '\0';
			if (q < e && *q == '\t') q++;
			n = 0; while (q < e && n < (int) sizeof it.key - 1) it.key[n++] = *q++;
			it.key[n] = '\0';
		}
		p = *e ? e + 1 : e;
	}
}

// ---- geometry ------------------------------------------------------------------------------
static int title_style (int i) { return i == (g_onyx ? 0 : 1) ? 2 : 0; }	// the app's name: bold
static void layout_titles (void)
{
	int x = 10;
	for (int i = 0; i < g_nmenus; i++)
	{
		g_menus[i].x = x;
		g_menus[i].w = uk_tw (g_menus[i].title, title_style (i)) + 16;
		x += g_menus[i].w;
	}
}
static int drop_w (const MenuDef &m)
{
	int w = 120;
	for (int i = 0; i < m.count; i++)
	{
		int ww = uk_tw (m.items[i].label) + uk_tw (m.items[i].key) + 5 * g_fw + 20 + (m.items[i].sub >= 0 ? 2 * g_fw : 0);
		if (ww > w) w = ww;
	}
	return w;
}
static int item_h (const Item &it) { return it.sep ? 9 : g_fh + 8; }
static int drop_h (const MenuDef &m) { int h = 10; for (int i = 0; i < m.count; i++) h += item_h (m.items[i]); return h; }
static int drop_x (const MenuDef &m) { int x = m.x; int w = drop_w (m); if (x + w > g_sw - 2) x = g_sw - 2 - w; return x; }

static int item_y (const MenuDef &m, int idx) { int yy = BAR_H + 5; for (int i = 0; i < idx; i++) yy += item_h (m.items[i]); return yy; }
static int sub_w (const SubDef &sd)
{
	int w = 140;
	for (int i = 0; i < sd.count; i++) { int ww = uk_tw (sd.items[i].label) + 28; if (ww > w) w = ww; }
	if (sd.count == 0) w = uk_tw (sd.windows ? TR ("(no open window)") : TR ("(empty)")) + 28;
	return w;
}
static int sub_h (const SubDef &sd) { return 10 + (sd.count ? sd.count : 1) * (g_fh + 8); }
static void sub_rect (int *x, int *y, int *w, int *h)		// where the open sub-menu is
{
	const MenuDef &m = g_menus[g_open]; const SubDef &sd = g_subs[g_sub];
	int dx = drop_x (m), dw = drop_w (m);
	*w = sub_w (sd); *h = sub_h (sd);
	*x = dx + dw - 2; if (*x + *w > g_sw - 2) *x = dx - *w + 2;
	*y = item_y (m, g_subOwner) - 3;
	if (*y + *h > g_sh - 2) *y = g_sh - 2 - *h;
	if (*y < BAR_H) *y = BAR_H;
}
static bool in_sub (int x, int y)
{
	if (g_open < 0 || g_sub < 0) return false;
	int sx, sy, sw, sh; sub_rect (&sx, &sy, &sw, &sh);
	return x >= sx && x < sx + sw && y >= sy && y < sy + sh;
}
static int sub_item_at (int x, int y)
{
	if (!in_sub (x, y)) return -1;
	int sx, sy, sw, sh; sub_rect (&sx, &sy, &sw, &sh);
	int i = (y - sy - 5) / (g_fh + 8);
	return y >= sy + 5 && i >= 0 && i < g_subs[g_sub].count ? i : -1;
}

static int title_at (int x, int y)
{
	if (y < 0 || y >= BAR_H) return -1;
	for (int i = 0; i < g_nmenus; i++) if (x >= g_menus[i].x && x < g_menus[i].x + g_menus[i].w) return i;
	return -1;
}
// the status icons on the right: the clock, the Wi-Fi state left of it, the speaker left of that
static int clk_w (void) { return uk_tw ("00:00", 2); }
static int clk_x (void) { return g_sw - clk_w () - 14; }
static bool on_clock (int x, int y) { return y >= 0 && y < BAR_H && x >= clk_x () - 6 && x < g_sw; }
static int bell_w (void);
static int wifi_x (void) { return clk_x () - 27 - bell_w (); }
static int spk_x (void) { return wifi_x () - 26; }
static bool on_wifi (int x, int y) { return y >= 0 && y < BAR_H && x >= wifi_x () - 3 && x < wifi_x () + 20; }
static bool on_speaker (int x, int y) { return y >= 0 && y < BAR_H && x >= spk_x () - 3 && x < spk_x () + 19; }
static int usb_x (void) { return spk_x () - 26; }
static bool usb_shown (void);
static bool on_usb (int x, int y) { return usb_shown () && y >= 0 && y < BAR_H && x >= usb_x () - 3 && x < usb_x () + 19; }
static void vol_box (int *x, int *y) { *x = spk_x () + 16 - VW; if (*x < 2) *x = 2; *y = BAR_H + 4; }
static bool in_vol_box (int x, int y) { int bx, by; vol_box (&bx, &by); return x >= bx && x < bx + VW && y >= by && y < by + VH; }
static void vol_track (int *x0, int *x1, int *y) { int bx, by; vol_box (&bx, &by); *x0 = bx + 20; *x1 = bx + VW - 20; *y = by + 44; }
static bool on_vol_track (int x, int y) { int x0, x1, ty; vol_track (&x0, &x1, &ty); return x >= x0 - 10 && x <= x1 + 10 && y >= ty - 10 && y <= ty + 14; }
static bool on_mute (int x, int y) { int bx, by; vol_box (&bx, &by); return x >= bx + 14 && x < bx + 100 && y >= by + 60 && y < by + 84; }

static int item_at (int x, int y)		// index in the open menu, -1 = none
{
	if (g_open < 0) return -1;
	const MenuDef &m = g_menus[g_open];
	int dx = drop_x (m), dw = drop_w (m), yy = BAR_H + 5;
	if (x < dx || x >= dx + dw) return -1;
	for (int i = 0; i < m.count; i++)
	{
		int h = item_h (m.items[i]);
		if (y >= yy && y < yy + h) return m.items[i].sep ? -1 : i;
		yy += h;
	}
	return -1;
}

// ---- drawing ----------------------------------------------------------------------------------
static unsigned C_BARDIM;			// the dimmed ink on the bar

// The Wi-Fi state icon, 17 x 12 px, its box's top-left at (x, y). Integer geometry only.
static void draw_wifi (int x, int y, bool up)
{
	if (up)			// a dot + three 90-degree arcs opening upward (centre = bottom middle)
	{
		int cx = x + 8, by = y + 11;
		for (int dy = -11; dy <= 0; dy++)
			for (int dx = -8; dx <= 8; dx++)
			{
				int ax = dx < 0 ? -dx : dx, d2 = dx * dx + dy * dy;
				if (ax > -dy + 1) continue;			// outside the 90-degree wedge
				bool on = d2 <= 2 || (d2 >= 12 && d2 <= 24) || (d2 >= 42 && d2 <= 62) || (d2 >= 90 && d2 <= 120);
				if (on) g_cv.fillRect (cx + dx, by + dy, 1, 1, C_BARTXT);
			}
	}
	else			// an empty circle crossed by a bar (bottom-left to top-right)
	{
		int cx = x + 8, cy = y + 6;
		for (int dy = -6; dy <= 6; dy++)
			for (int dx = -6; dx <= 6; dx++)
			{
				int d2 = dx * dx + dy * dy, s = dx + dy;
				bool ring = d2 >= 17 && d2 <= 30, bar = (s == 0 || s == 1) && d2 <= 30;
				if (ring || bar) g_cv.fillRect (cx + dx, cy + dy, 1, 1, C_BARDIM);
			}
	}
}

// The speaker icon, 16 x 12 px at (x, y): a box and a cone, then 1..3 waves by the volume, or a
// cross when muted (or at 0).
static void draw_speaker (int x, int y)
{
	unsigned c = g_mute || g_vol == 0 ? C_BARDIM : C_BARTXT;
	g_cv.fillRect (x, y + 4, 3, 4, c);
	for (int k = 0; k < 4; k++) g_cv.fillRect (x + 3 + k, y + 3 - k, 1, 6 + 2 * k, c);
	if (g_mute || g_vol == 0)
	{
		for (int k = 0; k < 5; k++) { g_cv.fillRect (x + 10 + k, y + 4 + k, 1, 1, c); g_cv.fillRect (x + 14 - k, y + 4 + k, 1, 1, c); }
		return;
	}
	int waves = g_vol >= 7 ? 3 : g_vol >= 4 ? 2 : 1;
	for (int w = 0; w < waves; w++)
	{
		int r = 3 + 3 * w;						// arcs around (x + 7, y + 6)
		for (int dy = -r; dy <= r; dy++)
			for (int dx = 1; dx <= r; dx++)
			{
				int d2 = dx * dx + dy * dy;
				if (d2 >= r * r - r && d2 <= r * r + r && dx * 3 >= (dy < 0 ? -dy : dy) * 2) g_cv.fillRect (x + 7 + dx, y + 6 + dy, 1, 1, c);
			}
	}
}

// A floating panel (a drop-down, the volume box, the calendar): light, rounded, outlined --
// blended over what lies below (the canvas is see-through there).
static void panel (int x, int y, int w, int h)
{
	uk_rbox (g_cv, x, y, w, h, 8, uk_tone (C_DROP, 140), C_DROP);
	uk_rline (g_cv, x, y, w, h, 8, C_OUT, 200);
}

// The volume box: a panel under the speaker, a slider 0..10 and Mute.
static void draw_volume_box (void)
{
	int bx, by; vol_box (&bx, &by);
	panel (bx, by, VW, VH);
	uk_text_l (g_cv, bx + 14, by + 6, 24, TR ("Volume"), C_FIELD_TEXT, 2);
	char v[40]; int n = 0;
	if (g_mute) scopy (v, TR ("Muted"), sizeof v);
	else { if (g_vol >= 10) { v[n++] = '1'; v[n++] = '0'; } else v[n++] = (char) ('0' + g_vol); v[n] = 0; }
	uk_text_l (g_cv, bx + VW - 14 - uk_tw (v), by + 6, 24, v, C_DIM);
	int x0, x1, ty; vol_track (&x0, &x1, &ty);
	int kx = x0 + (x1 - x0) * g_vol / 10;
	for (int i = 0; i <= 10; i++) g_cv.fillRect (x0 + (x1 - x0) * i / 10, ty + 10, 1, 3, C_DIM);
	uk_slider_mark (g_cv, x0 - 6, ty - 8, x1 - x0 + 12, 20, kx - x0 + 6, kx - 6, 12, g_mute ? UK_DISABLED : g_volDrag ? UK_PRESSED : UK_NORMAL);
	uk_check_mark (g_cv, bx + 18, by + 65, 15, g_mute != 0, UK_NORMAL);
	uk_text_l (g_cv, bx + 42, by + 65, 15, TR ("Mute"), C_FIELD_TEXT);
}

// ---- the calendar (a click on the time) ----------------------------------------------------------
class CalCard : public Calendar				// (its corners blend into the panel)
{
public:
	CalCard (int y, int m, int d) : Calendar (0, 0, y, m, d, 0) {}
	unsigned bgColor () override { return C_DROP; }
};
static CalCard *g_cal;
static bool g_calOpen = false;
static int g_calBtnHot = 0, g_calBtnDown = 0;		// the button pointed at / pressed: 1 Open Calendar, 2 Alarms and timers
static bool g_calClock = false;				// the Clock is on the card: its button shown (read as the calendar opens)
#define CALW	(CAL_W + 20)
static int cal_h (void) { return CAL_H + 20 + 36 + (g_calClock ? 34 : 0); }
static void cal_box (int *x, int *y) { *x = g_sw - CALW - 6; *y = BAR_H + 4; }
static void cal_btn (int i, int *x, int *y, int *w, int *h)	// i: 1 Open Calendar, 2 Alarms and timers (under it)
{
	int bx, by; cal_box (&bx, &by);
	int tw = uk_tw (TR ("Open Calendar"));
	if (g_calClock && uk_tw (TR ("Alarms and timers\xE2\x80\xA6")) > tw) tw = uk_tw (TR ("Alarms and timers\xE2\x80\xA6"));
	*w = tw + 32 < 150 ? 150 : tw + 32 > CAL_W ? CAL_W : tw + 32; *h = 28;
	*x = bx + (CALW - *w) / 2; *y = by + 10 + CAL_H + 6 + (i == 2 ? 34 : 0);
}
static bool in_cal_box (int x, int y) { int bx, by; cal_box (&bx, &by); return x >= bx && x < bx + CALW && y >= by && y < by + cal_h (); }
static int on_cal_btn (int x, int y)			// -> 1, 2, 0 none
{
	for (int i = 1; i <= (g_calClock ? 2 : 1); i++)
	{
		int bx, by, bw, bh; cal_btn (i, &bx, &by, &bw, &bh);
		if (x >= bx && x < bx + bw && y >= by && y < by + bh) return i;
	}
	return 0;
}

static void draw_calendar_box (void)
{
	int bx, by; cal_box (&bx, &by);
	panel (bx, by, CALW, cal_h ());
	g_cal->draw ();
	g_cv.putOther (g_cal->canvas, bx + 10, by + 10, false);
	for (int i = 1; i <= (g_calClock ? 2 : 1); i++)
	{
		int x, y, w, h, lx, ly, lw, lh; cal_btn (i, &x, &y, &w, &h);
		uk_framed (g_cv, x, y, w, h, C_BUTTON, g_calBtnDown == i ? UK_PRESSED : g_calBtnHot == i ? UK_HOT : UK_NORMAL, &lx, &ly, &lw, &lh);
		uk_text_c (g_cv, lx, ly, lw, lh, i == 1 ? TR ("Open Calendar") : TR ("Alarms and timers\xE2\x80\xA6"), C_BUTTON_TEXT);
	}
}
static void open_clock_alarms (void) { lx_launch ("clock", "alarms"); }	// (a Clock running: told, and raised)

// ---- the bell: an alarm of the Clock within 24 hours (S1) ------------------------------------------------------------
static AlarmSet g_alarms;
static bool g_bell = false;				// shown (left of the time)
static void bell_poll (void)				// alarms.txt read again: once a minute, and when the calendar opens
{
	int y = 0, mo = 0, d = 0, h = 0, mi = 0;
	bool real = kapi_get_datetime (&y, &mo, &d, &h, &mi, 0) == 1;
	bool was = g_bell;
	g_bell = false;
	if (real && alarms_load (g_alarms, ALARMS_PATH) > 0)
	{
		long now = clk_minute (y, mo, d, h, mi), next = alarms_next (g_alarms, now, 0);
		g_bell = next >= 0 && next - now <= 1440;
	}
	alarms_free (g_alarms);
	if (g_bell != was) g_dirty = true;
}
static int bell_w (void) { return g_bell ? 20 : 0; }
static int bell_x (void) { return clk_x () - 6 - 16; }
static bool on_bell (int x, int y) { return g_bell && y >= 0 && y < BAR_H && x >= bell_x () - 2 && x < clk_x () - 6; }
// A bell, 14 x 14 px at (x, y): its dome, its rim, its clapper (VPath, 1/16 px)
static void draw_bell (int x, int y, unsigned c)
{
	VPath p;
	p.circle (V (x + 7), V (y + 5), V (4));
	int body[8] = { V (x + 3), V (y + 5), V (x + 11), V (y + 5), V (x + 12) + 8, V (y + 10), V (x + 1) + 8, V (y + 10) };
	p.poly (body, 4);
	p.rrect (V (x), V (y + 9) + 8, V (14), V (2), V (1));
	p.circle (V (x + 7), V (y + 12) + 8, V (1) + 12);
	p.fill (g_cv, c);
}

// ---- the USB sticks (kapi v93) ---------------------------------------------------------------------
#define MAXVOL		16
#define UW		320
#define UROW		46
#define UFOOT		40
static struct kapi_volume g_vols[MAXVOL];
static int g_nvols = 0;
struct UsbSeen { char name[8]; unsigned gen; };
static UsbSeen g_seen[MAXVOL]; static int g_nseen = -1;	// (-1: not read yet: no notification at the start)
static int g_usbHot = -1, g_usbBtnDown = -1;			// the row / button pointed at, pressed
static int g_usbBusy = -1;					// the row whose eject found files open (next: forced)
static bool g_usbFootHot = false;

static bool usb_listed (const struct kapi_volume &v)
{
	return (v.flags & KAPI_VF_REMOVABLE) && (v.state == KAPI_VST_MOUNTED || v.state == KAPI_VST_EJECTED || v.state == KAPI_VST_UNREADABLE);
}
static int usb_count (void) { int n = 0; for (int i = 0; i < g_nvols; i++) if (usb_listed (g_vols[i])) n++; return n; }
static bool usb_shown (void) { return usb_count () > 0; }
static int usb_index (int row)			// the row-th listed stick -> its index in g_vols
{
	for (int i = 0; i < g_nvols; i++) if (usb_listed (g_vols[i]) && row-- == 0) return i;
	return -1;
}
static void usb_box (int *x, int *y, int *h) { *x = usb_x () + 16 - UW; if (*x < 2) *x = 2; *y = BAR_H + 4; *h = 10 + usb_count () * UROW + UFOOT; }
static bool in_usb_box (int x, int y) { int bx, by, bh; usb_box (&bx, &by, &bh); return x >= bx && x < bx + UW && y >= by && y < by + bh; }
static void usb_btn (int row, int *x, int *y, int *w, int *h) { int bx, by, bh; usb_box (&bx, &by, &bh); *w = 84; *h = 26; *x = bx + UW - 12 - *w; *y = by + 5 + row * UROW + (UROW - *h) / 2; }
static int usb_row_at (int x, int y) { int bx, by, bh; usb_box (&bx, &by, &bh); if (x < bx || x >= bx + UW || y < by + 5) return -1; int r = (y - by - 5) / UROW; return r < usb_count () ? r : -1; }
// A device with several partitions has a row each (USB1P1:, USB1P2:) and one button, on its first row:
// Eject / Mount act on the whole device.
static bool usb_has_btn (int row)
{
	if (row <= 0) return row == 0;
	int i = usb_index (row), p = usb_index (row - 1);
	return i < 0 || p < 0 || !eq (g_vols[i].device, g_vols[p].device);
}
static bool on_usb_btn (int row, int x, int y) { if (!usb_has_btn (row)) return false; int bx, by, bw, bh; usb_btn (row, &bx, &by, &bw, &bh); return x >= bx && x < bx + bw && y >= by && y < by + bh; }
static void usb_foot (int *x, int *y, int *w, int *h) { int bx, by, bh; usb_box (&bx, &by, &bh); *w = 120; *h = 26; *x = bx + UW - 12 - *w; *y = by + bh - UFOOT + 6; }
static bool on_usb_foot (int x, int y) { int bx, by, bw, bh; usb_foot (&bx, &by, &bw, &bh); return x >= bx && x < bx + bw && y >= by && y < by + bh; }

static void vname (const struct kapi_volume &v, char *out)	// "USB2:"
{
	int n = 0; while (v.name[n] && n < 7) { out[n] = v.name[n]; n++; }
	out[n++] = ':'; out[n] = 0;
}
static void size_text (unsigned long long b, char *out, int cap)	// "14.9 GB"
{
	const char *unit = " MB"; unsigned long long d = 1ull << 20;
	if (b >= (1ull << 40)) { unit = " TB"; d = 1ull << 40; }
	else if (b >= (1ull << 30)) { unit = " GB"; d = 1ull << 30; }
	unsigned long long w = b / d, t = (b % d) * 10 / d;
	char tmp[24]; int k = 0; do { tmp[k++] = (char) ('0' + w % 10); w /= 10; } while (w);
	int n = 0; while (k && n < cap - 1) out[n++] = tmp[--k];
	if (d > (1ull << 20) && b / d < 100 && n < cap - 3) { out[n++] = '.'; out[n++] = (char) ('0' + t); }
	for (const char *u = unit; *u && n < cap - 1; u++) out[n++] = *u;
	out[n] = 0;
}
static void cat (char *d, int cap, const char *s) { int n = 0; while (d[n]) n++; while (*s && n < cap - 1) d[n++] = *s++; d[n] = 0; }

// The volumes read again (about once a second); what changed told through notifyd.
static void usb_poll (void)
{
	int n = kapi_vol_list (g_vols, MAXVOL, 0);
	g_nvols = n < 0 ? 0 : n > MAXVOL ? MAXVOL : n;
	bool first = g_nseen < 0;
	if (first) g_nseen = 0;
	for (int i = 0; i < g_nvols; i++)
	{
		const struct kapi_volume &v = g_vols[i];
		if (!(v.flags & KAPI_VF_REMOVABLE)) continue;
		int k = 0;
		while (k < g_nseen && !eq (g_seen[k].name, v.name)) k++;
		if (k == g_nseen) { if (g_nseen >= MAXVOL) continue; scopy (g_seen[k].name, v.name, sizeof g_seen[k].name); g_seen[k].gen = 0; g_nseen++; if (first) g_seen[k].gen = v.gen; }
		if (g_seen[k].gen == v.gen) continue;
		g_seen[k].gen = v.gen;
		g_dirty = true;
		char vn[12]; vname (v, vn);
		char msg[320]; msg[0] = 0;
		char act[48]; act[0] = 0;
		switch (v.state)
		{
		case KAPI_VST_MOUNTED:
		{
			char sz[24]; size_text (v.total, sz, sizeof sz);
			snprintf (msg, sizeof msg, TR ("%s is connected as %s (%s, %s). Click to open it."), v.label[0] ? v.label : TR ("A USB stick"), vn, sz, v.type);
			cat (act, sizeof act, "fileviewer "); cat (act, sizeof act, vn); cat (act, sizeof act, "/");
			break;
		}
		case KAPI_VST_EJECTED:
			snprintf (msg, sizeof msg, TR ("%s can be removed safely."), vn);
			break;
		case KAPI_VST_UNREADABLE:
			snprintf (msg, sizeof msg, (v.flags & KAPI_VF_IOERR) ? TR ("%s could not be read.") : TR ("%s has no FAT / exFAT file system. Click to format it."), vn);
			if (!(v.flags & KAPI_VF_IOERR)) { cat (act, sizeof act, "disks "); cat (act, sizeof act, vn); }
			break;
		case KAPI_VST_REMOVED:
			if (v.flags & KAPI_VF_UNSAFE)
			{
				snprintf (msg, sizeof msg, TR ("%s was removed without being ejected: what was being written to it may be lost."), vn);
			}
			break;
		}
		if (msg[0]) notify_action ("USB", msg, act[0] ? act : 0);
	}
	if (g_usbOpen && !usb_shown ()) { g_usbOpen = false; g_dirty = true; }
}

// The drive icon, 16 x 12 px: a stick (its body and its plug)
static void draw_usb (int x, int y)
{
	unsigned c = g_usbOpen ? C_SEL_TEXT : C_BARTXT;
	if (g_usbOpen) uk_hilite (g_cv, x - 4, 3, 24, BAR_H - 7, 5, true);
	g_cv.fillRect (x, y + 3, 11, 7, c);				// the body
	g_cv.fillRect (x + 11, y + 4, 4, 5, c);				// the plug
	g_cv.fillRect (x + 12, y + 5, 2, 1, uk_tone (C_MENUBAR, 176));
	g_cv.fillRect (x + 12, y + 7, 2, 1, uk_tone (C_MENUBAR, 176));
	g_cv.fillRect (x + 2, y + 5, 3, 3, uk_tone (C_MENUBAR, 176));	// a light
}

// ---- the status area: the programs' icons (kapi v95, kapi_tray_*) ---------------------------------
// Each program may put an icon there (UIKit's uk_tray: Telegram's); a double click shows its first window
// again -- even minimised -- (Elegant does it, and tells the program), a right click tells the program.
struct TrayIcon { unsigned pid, gen; char tip[56]; unsigned px[KAPI_TRAY_PX * KAPI_TRAY_PX]; };
static TrayIcon g_tray[KAPI_TRAY_MAX];
static int g_ntray = 0;
static int g_trayHot = -1;				// the icon under the pointer (its tip shown)
static int g_trayClick = -1; static unsigned g_trayClickT;	// (a double click: two presses in 0.4 s)
#define TRAY_STEP	(KAPI_TRAY_PX + 6)

static int tray_right (void) { return (usb_shown () ? usb_x () : spk_x ()) - 8; }
static int tray_x (int i) { return tray_right () - (i + 1) * TRAY_STEP + 3; }
static int tray_at (int x, int y)
{
	if (y < 0 || y >= BAR_H) return -1;
	for (int i = 0; i < g_ntray; i++) if (x >= tray_x (i) - 3 && x < tray_x (i) + KAPI_TRAY_PX + 3) return i;
	return -1;
}

// The icons as Elegant has them now -> true: something changed.
static bool tray_poll (void)
{
	struct kapi_tray_info L[KAPI_TRAY_MAX];
	int n = uk_win_tray_list (L, KAPI_TRAY_MAX);
	if (n < 0) n = 0;
	bool changed = n != g_ntray;
	for (int i = 0; i < n; i++)
	{
		TrayIcon &t = g_tray[i];
		if (!changed && t.pid == L[i].pid && t.gen == L[i].gen) continue;
		changed = true;
		t.pid = L[i].pid; t.gen = L[i].gen;
		scopy (t.tip, L[i].tip, sizeof t.tip);
		if (!uk_win_tray_icon (t.pid, t.px)) for (int k = 0; k < KAPI_TRAY_PX * KAPI_TRAY_PX; k++) t.px[k] = CLEAR;
	}
	g_ntray = n;
	if (changed) { g_trayHot = -1; g_trayClick = -1; }
	return changed;
}

static void draw_tray (void)
{
	int y0 = (BAR_H - 1 - KAPI_TRAY_PX) / 2;
	for (int i = 0; i < g_ntray; i++)
	{
		int x0 = tray_x (i);
		if (i == g_trayHot) uk_rbox (g_cv, x0 - 3, 3, KAPI_TRAY_PX + 6, BAR_H - 7, 5, uk_tone (C_MENUBAR, 232), uk_tone (C_MENUBAR, 208));
		const unsigned *px = g_tray[i].px;
		for (int y = 0; y < KAPI_TRAY_PX; y++)
			for (int x = 0; x < KAPI_TRAY_PX; x++)
			{
				unsigned p = px[y * KAPI_TRAY_PX + x], t = p >> 24;
				if (t < 255) uk_blend_px (g_cv, x0 + x, y0 + y, p & 0x00FFFFFFu, (int) (255 - t));
			}
	}
}

static void draw_usb_box (void)
{
	int bx, by, bh; usb_box (&bx, &by, &bh);
	panel (bx, by, UW, bh);
	for (int r = 0; r < usb_count (); r++)
	{
		const struct kapi_volume &v = g_vols[usb_index (r)];
		int ry = by + 5 + r * UROW;
		if (r == g_usbHot) uk_hilite (g_cv, bx + 4, ry + 2, UW - 8 - 96, UROW - 4, 5, true);
		char vn[12]; vname (v, vn);
		char line[96]; line[0] = 0;
		cat (line, sizeof line, vn); cat (line, sizeof line, "  "); cat (line, sizeof line, v.label[0] ? v.label : TR ("USB stick"));
		unsigned ink = r == g_usbHot ? C_SEL_TEXT : C_FIELD_TEXT, dim = r == g_usbHot ? C_SEL_TEXT : C_DIM;
		uk_text_l (g_cv, bx + 14, ry + 4, g_fh + 2, line, ink, 2);
		char sub[160]; sub[0] = 0;
		if (v.state == KAPI_VST_MOUNTED)
		{
			char sz[24]; size_text (v.total, sz, sizeof sz);
			cat (sub, sizeof sub, sz); cat (sub, sizeof sub, " "); cat (sub, sizeof sub, v.type);
			if (r == g_usbBusy) { cat (sub, sizeof sub, " - "); cat (sub, sizeof sub, TR ("in use: Eject again to force")); }
		}
		else if (v.state == KAPI_VST_EJECTED) cat (sub, sizeof sub, TR ("Ejected: it can be removed"));
		else cat (sub, sizeof sub, (v.flags & KAPI_VF_IOERR) ? TR ("Cannot be read") : TR ("Not formatted"));
		uk_text_l (g_cv, bx + 14, ry + 6 + g_fh, g_fh + 2, sub, dim);
		if (!usb_has_btn (r)) continue;
		int x, y, w, h, lx, ly, lw, lh; usb_btn (r, &x, &y, &w, &h);
		uk_framed (g_cv, x, y, w, h, C_BUTTON, g_usbBtnDown == r ? UK_PRESSED : UK_NORMAL, &lx, &ly, &lw, &lh);
		uk_text_c (g_cv, lx, ly, lw, lh, v.state == KAPI_VST_MOUNTED ? TR ("Eject") : v.state == KAPI_VST_EJECTED ? TR ("Mount") : TR ("Format..."), C_BUTTON_TEXT);
	}
	int x, y, w, h, lx, ly, lw, lh; usb_foot (&x, &y, &w, &h);
	uk_framed (g_cv, x, y, w, h, C_BUTTON, g_usbFootHot ? UK_HOT : UK_NORMAL, &lx, &ly, &lw, &lh);
	uk_text_c (g_cv, lx, ly, lw, lh, TR ("Disks..."), C_BUTTON_TEXT);
}

static void usb_action (int row)		// the row's button
{
	int i = usb_index (row);
	if (i < 0) return;
	const struct kapi_volume &v = g_vols[i];
	char vn[12]; vname (v, vn);
	if (v.state == KAPI_VST_MOUNTED)
	{
		int r = kapi_vol_eject (vn, g_usbBusy == row ? KAPI_EJECT_FORCE : 0);
		if (r == -KAPI_EBUSY) { g_usbBusy = row; g_dirty = true; return; }
		g_usbBusy = -1;
		if (r != 0) notify ("USB", TR ("The stick could not be ejected."));
	}
	else if (v.state == KAPI_VST_EJECTED)
	{
		if (kapi_vol_mount (vn) != 0) notify ("USB", TR ("The stick could not be mounted again."));
	}
	else
	{
		g_usbOpen = false;
		if (uk_win_app_raise ("disks") == 0) lx_launch ("disks", vn);
	}
	usb_poll ();
	g_dirty = true;
}

static void draw (void)
{
	bool full = g_open >= 0 || g_volOpen || g_calOpen || g_usbOpen;
	int tipH = !full && g_trayHot >= 0 && g_trayHot < g_ntray && g_tray[g_trayHot].tip[0] ? g_fh + 16 : 0;	// (an icon's tip)
	int h = full ? g_sh : BAR_H + tipH;
	if (full) g_cv.fillRect (0, BAR_H, g_sw, g_sh - BAR_H, CATCH);	// (catches a click elsewhere)
	else if (tipH) g_cv.fillRect (0, BAR_H, g_sw, tipH, CLEAR);
	uk_paint_alpha (true);
	// the bar: a light gradient of the face, a light line on top, a darker one below
	uk_rbox (g_cv, 0, 0, g_sw, BAR_H - 1, 0, uk_tone (C_MENUBAR, 196), uk_tone (C_MENUBAR, 150));
	for (int x = 0; x < g_sw; x++) { uk_blend_px (g_cv, x, 0, 0x00FFFFFF, 120); g_cv.pixel (x, BAR_H - 1, C_OUT); }
	for (int i = 0; i < g_nmenus; i++)
	{
		const MenuDef &m = g_menus[i];
		if (i == g_open) uk_hilite (g_cv, m.x + 2, 3, m.w - 4, BAR_H - 7, 5, true);
		else if (i == g_titleHot)			// under the pointer: a shade lighter than the bar
			uk_rbox (g_cv, m.x + 2, 3, m.w - 4, BAR_H - 7, 5, uk_tone (C_MENUBAR, 232), uk_tone (C_MENUBAR, 208));
		uk_text_l (g_cv, m.x + 8, 0, BAR_H - 1, m.title, i == g_open ? C_SEL_TEXT : C_BARTXT, title_style (i));
	}
	int hh = 0, mm = 0;
	kapi_get_datetime (0, 0, 0, &hh, &mm, 0);
	char clk[6] = { (char) ('0' + hh / 10), (char) ('0' + hh % 10), ':', (char) ('0' + mm / 10), (char) ('0' + mm % 10), 0 };
	int clkX = clk_x ();
	if (g_calOpen) uk_hilite (g_cv, clkX - 6, 3, clk_w () + 12, BAR_H - 7, 5, true);
	uk_text_l (g_cv, clkX, 0, BAR_H - 1, clk, g_calOpen ? C_SEL_TEXT : C_BARTXT, 2);
	if (g_bell) draw_bell (bell_x (), (BAR_H - 1 - 14) / 2, C_BARTXT);
	g_lastMin = mm;
	if (g_wifi < 0) g_wifi = kapi_net_status (0, 0) ? 1 : 0;
	draw_wifi (wifi_x (), (BAR_H - 1 - 12) / 2, g_wifi == 1);
	draw_speaker (spk_x (), (BAR_H - 1 - 12) / 2);
	if (usb_shown ()) draw_usb (usb_x (), (BAR_H - 1 - 12) / 2);
	draw_tray ();
	if (tipH)
	{
		const char *tip = g_tray[g_trayHot].tip;
		int tw = uk_tw (tip) + 16, tx = tray_x (g_trayHot) + KAPI_TRAY_PX / 2 - tw / 2;
		if (tx + tw > g_sw - 2) tx = g_sw - 2 - tw;
		if (tx < 2) tx = 2;
		panel (tx, BAR_H + 2, tw, tipH - 4);
		uk_text_l (g_cv, tx + 8, BAR_H + 2, tipH - 4, tip, C_FIELD_TEXT);
	}
	if (g_volOpen) draw_volume_box ();
	if (g_usbOpen) draw_usb_box ();
	if (g_calOpen) draw_calendar_box ();

	if (g_open >= 0)
	{
		const MenuDef &m = g_menus[g_open];
		int dx = drop_x (m), dw = drop_w (m), dh = drop_h (m);
		panel (dx, BAR_H + 1, dw, dh);
		int yy = BAR_H + 5;
		for (int i = 0; i < m.count; i++)
		{
			const Item &it = m.items[i];
			int ih = item_h (it);
			if (it.sep) uk_etch_h (g_cv, dx + 10, yy + ih / 2 - 1, dw - 20, C_DROP);
			else
			{
				bool hot = i == g_hover || (g_sub >= 0 && i == g_subOwner);
				if (hot) uk_hilite (g_cv, dx + 4, yy, dw - 8, ih, 5, true);
				uk_text_l (g_cv, dx + 14, yy, ih, it.label, hot ? C_SEL_TEXT : C_FIELD_TEXT);
				if (it.key[0]) uk_text_l (g_cv, dx + dw - 14 - uk_tw (it.key), yy, ih, it.key, hot ? C_SEL_TEXT : C_DIM);
				if (it.sub >= 0) uk_glyph (g_cv, WKG_CHEV_RIGHT, dx + dw - 16, yy + ih / 2, 8, hot ? C_SEL_TEXT : C_FIELD_TEXT);
			}
			yy += ih;
		}
		if (g_sub >= 0)
		{
			const SubDef &sd = g_subs[g_sub];
			int sx, sy, sw, sh; sub_rect (&sx, &sy, &sw, &sh);
			panel (sx, sy, sw, sh);
			int ih = g_fh + 8;
			if (sd.count == 0) uk_text_l (g_cv, sx + 14, sy + 5, ih, sd.windows ? TR ("(no open window)") : TR ("(empty)"), C_DIM);
			for (int i = 0; i < sd.count; i++)
			{
				int iy = sy + 5 + i * ih;
				if (i == g_subHover) uk_hilite (g_cv, sx + 4, iy, sw - 8, ih, 5, true);
				uk_text_l (g_cv, sx + 14, iy, ih, sd.items[i].label, i == g_subHover ? C_SEL_TEXT : C_FIELD_TEXT);
			}
		}
	}
	uk_paint_alpha (false);
	uk_win_resize (g_sw, h);
	uk_win_present ();
	g_dirty = false;
}

// ---- actions -------------------------------------------------------------------------------------
static void run_item (const Item &it)
{
	if (it.sep) return;
	switch (it.id)
	{
	case ONYX_TERMINAL: kapi_launch ("terminal"); return;
	case ONYX_CONTROL:  if (!uk_win_app_raise ("control")) kapi_launch ("control"); return;
	case ONYX_FILES:    kapi_launch ("fileviewer"); return;
	case ONYX_TASKS:    kapi_launch ("taskman"); return;
	case ONYX_SHUTDOWN: kapi_launch ("shutdown"); return;
	case ONYX_SUB:      return;			// (it opens its sub-menu)
	}
	uk_win_menu_command (it.id);		// the active app's own item (or MENU_QUIT)
}

static void open_menu (int i)
{
	g_volOpen = false; g_calOpen = false; g_usbOpen = false;
	if (i == 0) build_onyx_menu (g_menus[0]);		// the apps as they are now
	g_open = i; g_hover = -1; g_sub = -1; g_subOwner = -1; g_subHover = -1; g_dirty = true;
}
static void close_menu (void) { g_open = -1; g_hover = -1; g_sub = -1; g_subOwner = -1; g_subHover = -1; g_dirty = true; }
static void open_sub (int owner)
{
	const Item &it = g_menus[g_open].items[owner];
	if (g_sub == it.sub && g_subOwner == owner) return;
	g_sub = it.sub; g_subOwner = owner; g_subHover = -1;
	if (g_subs[g_sub].windows) fill_windows (g_subs[g_sub]);
	g_dirty = true;
}
static void run_sub_item (int i)
{
	SubItem it = g_subs[g_sub].items[i];
	bool windows = g_subs[g_sub].windows;
	close_menu (); draw ();
	if (windows) uk_win_app_raise (it.app);
	else if (uk_win_app_raise (it.app) == 0) lx_launch (it.app, 0);	// running: to the front
}

// the slider at x -> the volume (moving it unmutes, as on Windows)
static void vol_set_at (int x)
{
	int x0, x1, ty; vol_track (&x0, &x1, &ty);
	int v = ((x - x0) * 10 + (x1 - x0) / 2) / (x1 - x0);
	v = v < 0 ? 0 : v > 10 ? 10 : v;
	int r = kapi_sound_volume (v, 0);
	g_vol = r & 0xFF; g_mute = (r & 0x100) ? 1 : 0; g_dirty = true;
}

static int g_newW = 0, g_newH = 0;		// (GUI_EVENT_DISPLAY_RESIZE: the main loop applies it)

static void ptr (unsigned long, int ev, long v)
{
	if (ev == GUI_EVENT_DISPLAY_RESIZE) { g_newW = GUI_DISPLAY_W (v); g_newH = GUI_DISPLAY_H (v); return; }
	int x = GUI_PTR_X (v), y = GUI_PTR_Y (v), c = GUI_PTR_CHANGED (v);
	int t = title_at (x, y);
	switch (ev)
	{
	case GUI_EVENT_PTR_DOWN:
		if ((c & 2) && tray_at (x, y) >= 0)			// a right click on an icon: its program told
		{
			uk_win_tray_activate (g_tray[tray_at (x, y)].pid, KAPI_TRAY_MENU);
			break;
		}
		if (!(c & 1)) break;
		if (tray_at (x, y) >= 0)				// an icon of the status area: a double click shows its program
		{
			int ti = tray_at (x, y);
			if (g_open >= 0) close_menu ();
			g_volOpen = false; g_calOpen = false; g_usbOpen = false;
			unsigned now = kapi_get_ticks ();
			if (ti == g_trayClick && now - g_trayClickT < 40) { uk_win_tray_activate (g_tray[ti].pid, KAPI_TRAY_OPEN); g_trayClick = -1; }
			else { g_trayClick = ti; g_trayClickT = now; }
			g_trayHot = -1; g_dirty = true;
			break;
		}
		if (g_calOpen && in_cal_box (x, y))			// the calendar: its month, its button
		{
			int bx, by; cal_box (&bx, &by);
			if (on_cal_btn (x, y)) g_calBtnDown = on_cal_btn (x, y);
			else g_cal->handleMouse (x - bx - 10, y - by - 10, 1, 0, 0, 0);
			g_dirty = true;
			break;
		}
		if (on_clock (x, y))					// the calendar, open / closed
		{
			if (g_open >= 0) close_menu ();
			g_volOpen = false;
			g_calOpen = !g_calOpen; g_dirty = true;
			if (g_calOpen)					// (today's month; the Clock's button when it is there)
			{
				int yy = 2026, mo = 1, dd = 1; kapi_get_datetime (&yy, &mo, &dd, 0, 0, 0);
				g_cal->setDate (yy, mo, dd);
				char p[80]; ax_app_path (p, sizeof p, "clock", ".app/app.txt");
				g_calClock = lx_exists (p) != 0;
				bell_poll ();
			}
			break;
		}
		if (on_bell (x, y))					// the bell: the Clock's alarms
		{
			if (g_open >= 0) close_menu ();
			g_volOpen = false; g_calOpen = false; g_usbOpen = false; g_dirty = true;
			open_clock_alarms ();
			break;
		}
		if (g_calOpen) { g_calOpen = false; g_dirty = true; if (t < 0) break; }	// a click elsewhere
		if (g_usbOpen && in_usb_box (x, y))			// the USB box: a button, a stick's name
		{
			int r = usb_row_at (x, y);
			if (r >= 0 && on_usb_btn (r, x, y)) g_usbBtnDown = r;
			else if (on_usb_foot (x, y)) { g_usbOpen = false; if (uk_win_app_raise ("disks") == 0) lx_launch ("disks", 0); }
			else if (r >= 0)
			{
				const struct kapi_volume &v = g_vols[usb_index (r)];
				if (v.state == KAPI_VST_MOUNTED)
				{
					char a[16]; vname (v, a); cat (a, sizeof a, "/");
					g_usbOpen = false;
					lx_launch ("fileviewer", a);
				}
			}
			g_dirty = true;
			break;
		}
		if (on_usb (x, y))					// the USB box, open / closed
		{
			if (g_open >= 0) close_menu ();
			g_volOpen = false;
			g_usbOpen = !g_usbOpen; g_usbBusy = -1; g_dirty = true;
			break;
		}
		if (g_usbOpen) { g_usbOpen = false; g_dirty = true; if (t < 0) break; }
		if (on_speaker (x, y))					// the volume box, open / closed
		{
			if (g_open >= 0) close_menu ();
			g_usbOpen = false;
			g_volOpen = !g_volOpen; g_dirty = true;
			break;
		}
		if (on_wifi (x, y))					// the Wi-Fi menu: an app of its own
		{
			if (g_open >= 0) close_menu ();
			g_volOpen = false; g_dirty = true;
			if (uk_win_app_raise ("wifimenu") == 0) lx_launch ("wifimenu", 0);
			break;
		}
		if (g_volOpen)
		{
			if (in_vol_box (x, y))
			{
				if (on_vol_track (x, y)) { g_volDrag = true; vol_set_at (x); }
				else if (on_mute (x, y)) { int r = kapi_sound_volume (-1, g_mute ? 0 : 1); g_mute = (r & 0x100) ? 1 : 0; volume_save (g_vol, g_mute); g_dirty = true; }
				break;
			}
			g_volOpen = false; g_dirty = true;			// a click outside closes it
			if (t < 0) break;
		}
		if (t >= 0) { if (t == g_open) close_menu (); else open_menu (t); g_pressedTitle = true; }
		else if (g_open >= 0 && item_at (x, y) < 0 && !in_sub (x, y)) close_menu ();	// click outside
		break;
	case GUI_EVENT_PTR_UP:
		if (!(c & 1)) break;
		if (g_calOpen)
		{
			int bx, by; cal_box (&bx, &by);
			if (g_calBtnDown)
			{
				int b = g_calBtnDown;
				g_calBtnDown = 0;
				if (on_cal_btn (x, y) == b)
				{
					g_calOpen = false;
					if (b == 2) open_clock_alarms ();
					else if (uk_win_app_raise ("calendar") == 0) lx_launch ("calendar", 0);
				}
			}
			else if (in_cal_box (x, y)) g_cal->handleMouse (x - bx - 10, y - by - 10, 0, 0, 0, 0);
			g_dirty = true;
			break;
		}
		if (g_volDrag) { g_volDrag = false; volume_save (g_vol, g_mute); break; }
		if (g_usbBtnDown >= 0)
		{
			int r = g_usbBtnDown; g_usbBtnDown = -1; g_dirty = true;
			if (on_usb_btn (r, x, y)) usb_action (r);
			break;
		}
		if (g_open >= 0)
		{
			int si = sub_item_at (x, y);
			if (si >= 0) { run_sub_item (si); break; }
			int i = item_at (x, y);
			if (i >= 0 && g_menus[g_open].items[i].sub >= 0) { open_sub (i); break; }	// (stays open)
			if (i >= 0) { Item it = g_menus[g_open].items[i]; close_menu (); draw (); run_item (it); }
		}
		g_pressedTitle = false;
		break;
	case GUI_EVENT_PTR_MOVE:
		{
			int th = g_open < 0 && !g_volOpen && !g_calOpen && !g_usbOpen ? tray_at (x, y) : -1;
			if (th != g_trayHot) { g_trayHot = th; g_dirty = true; }
		}
		if (g_calOpen)
		{
			int h = on_cal_btn (x, y);
			if (h != g_calBtnHot) { g_calBtnHot = h; g_dirty = true; }
			break;
		}
		if (g_volDrag) { vol_set_at (x); break; }
		if (g_usbOpen)
		{
			int r = usb_row_at (x, y);
			if (r >= 0 && on_usb_btn (r, x, y)) r = -1;		// (the button: not the row)
			bool f = on_usb_foot (x, y);
			if (r != g_usbHot || f != g_usbFootHot) { g_usbHot = r; g_usbFootHot = f; g_dirty = true; }
			break;
		}
		if (g_open >= 0)
		{
			if (t >= 0 && t != g_open) open_menu (t);		// slide across titles
			if (in_sub (x, y))
			{
				int si = sub_item_at (x, y);
				if (si != g_subHover) { g_subHover = si; g_dirty = true; }
				break;
			}
			int i = item_at (x, y);
			if (i != g_hover) { g_hover = i; g_dirty = true; }
			if (i >= 0 && g_menus[g_open].items[i].sub >= 0) open_sub (i);
			else if (i >= 0 && g_sub >= 0) { g_sub = -1; g_subOwner = -1; g_dirty = true; }
		}
		if (t != g_titleHot) { g_titleHot = t; g_dirty = true; }	// the title pointed at
		break;
	case GUI_EVENT_PTR_LEAVE:
		if (g_trayHot != -1) { g_trayHot = -1; g_dirty = true; }
		if (g_hover != -1) { g_hover = -1; g_dirty = true; }
		if (g_titleHot != -1) { g_titleHot = -1; g_dirty = true; }
		break;
	case GUI_EVENT_PTR_WHEEL:				// the wheel over the calendar: the months
		if (g_calOpen && in_cal_box (x, y)) { g_cal->handleMouse (5, 40, 0, 0, 0, GUI_PTR_WHEEL (v)); g_dirty = true; }
		break;
	}
}

int main (void)
{
	kapi_screen_size (&g_sw, &g_sh);
	g_fw = kapi_font_width ();  if (g_fw < 1) g_fw = 8;
	g_fh = kapi_font_height (); if (g_fh < 1) g_fh = 16;

	g_fb = uk_win_create_ex (0, 0, g_sw, g_sh, "menubar",
				      WIN_FLAG_BORDERLESS | WIN_FLAG_TOPMOST | WIN_FLAG_ALPHA | WIN_FLAG_SYSTEM);
	if (g_fb == 0) return 1;
	uk_win_resize (g_sw, BAR_H);		// reserves the strip (the kernel keeps the minimum)
	g_cv.adopt (g_fb, g_sw, g_sh);
	uikit::init ();					// the fonts, the theme: the palette
	if (ft_uikit_install ("DejaVu Sans", 13))		// FreeType's anti-aliased text (else the bitmap font)
		g_fh = uk_fh ();
	uk_lang_init ();					// the words in the system's language (after the text face)
	C_BARTXT = uk_ink_on (uk_tone (C_MENUBAR, 176));
	C_BARDIM = uk_mix (uk_tone (C_MENUBAR, 176), C_BARTXT, 110);
	C_DROP = C_FIELD; C_DIM = uk_mix (C_FIELD, C_FIELD_TEXT, 130); C_OUT = uk_tone (C_MENUBAR, 70);
	{ int yy = 2026, mo = 1, dd = 1; kapi_get_datetime (&yy, &mo, &dd, 0, 0, 0); g_cal = new CalCard (yy, mo, dd); }
	uk_win_on_pointer (ptr);
	alarms_init (g_alarms);
	bell_poll ();							// (the Clock's alarms: the bell)
	volume_restore ();						// the saved volume (SD:/etc/sound.ini)
	{ int r = kapi_sound_volume (-1, -1); g_vol = r & 0xFF; g_mute = (r & 0x100) ? 1 : 0; }

	static char spec[WIN_MENU_MAX_USER], title[48];
	unsigned serial = ~0u;
	onyx_menu (); layout_titles ();
	for (;;)
	{
		pump_events ();
		if (g_newW > 0)					// the screen's new size: the bar across it
		{
			int w = g_newW, h = g_newH, stride = w;
			g_newW = 0;
			g_volOpen = false; g_calOpen = false; g_usbOpen = false; close_menu ();
			unsigned *fb = uk_win_resize2 (w, h, &stride);	// (the canvas: the whole screen, for the drop-downs)
			if (fb != 0) { g_fb = fb; g_sw = w; g_sh = h; g_cv.adopt (fb, w, h, stride); }
			uk_win_resize (g_sw, BAR_H);
			layout_titles ();
			g_dirty = true;
		}
		unsigned s = uk_win_menu_get (spec, sizeof spec, title, sizeof title);
		if (s != serial)
		{
			serial = s;
			if (s == 0) onyx_menu (); else parse (spec, title);
			layout_titles ();
			if (g_open >= 0) close_menu ();		// the active app changed under the menu
			g_dirty = true;
		}
		int hh = 0, mm = 0;
		kapi_get_datetime (0, 0, 0, &hh, &mm, 0);
		if (mm != g_lastMin) { g_dirty = true; bell_poll (); }
		static unsigned lastNet = 0, lastTray = 0;		// Wi-Fi state: about once a second
		unsigned now = kapi_get_ticks ();
		if (now - lastTray >= 25) { lastTray = now; if (tray_poll ()) g_dirty = true; }	// (v95) the programs' icons
		if (now - lastNet >= 100)
		{
			lastNet = now;
			int w = kapi_net_status (0, 0) ? 1 : 0;
			if (w != g_wifi) { g_wifi = w; g_dirty = true; }
			usb_poll ();						// (v93) the USB sticks
			int r = kapi_sound_volume (-1, -1);			// (the volume command may change it)
			if ((r & 0xFF) != g_vol || ((r & 0x100) ? 1 : 0) != g_mute) { g_vol = r & 0xFF; g_mute = (r & 0x100) ? 1 : 0; g_dirty = true; }
		}
		if (g_calOpen && !g_cal->valid) g_dirty = true;		// (the calendar changed its month)
		if (g_dirty) draw ();
		msleep (g_open >= 0 || g_volOpen || g_calOpen || g_usbOpen ? 16 : 50);
	}
}
