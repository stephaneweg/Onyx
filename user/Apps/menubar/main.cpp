//
// menubar -- the system menu bar across the top of the screen (macOS-style).
//
// It shows the ACTIVE app's name and its menus (kapi_get_menu, declared by the app with
// uikit::Menu / kapi_set_menu), opens a drop-down on click and sends the chosen command
// back to the app (kapi_menu_command). The first menu is always the "Onyx" system menu:
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
// A click on the time opens a calendar (the month; "Open Calendar" starts the Calendar app).
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
#include "appkit/appkit.h"
#include "ft/uikitface.h"
#include "launch.h"
#include "uikit/uikit.h"
#include "volume.h"

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
static bool g_dirty = true, g_pressedTitle = false;
static int g_lastMin = -1;
static int g_wifi = -1;			// last drawn Wi-Fi state (1 connected, 0 not)
static int g_vol = 10, g_mute = 0;	// the master volume, as last read from the kernel
static bool g_volOpen = false, g_volDrag = false;	// the volume box (and its slider held)
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
	static const char *const own[] = { "menubar", "panel", "shelf", "notifyd", "desktop", "shell", "applist", 0 };
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
	kapi_list_windows (buf, sizeof buf);
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
	q.id = MENU_QUIT; scopy (q.label, "Quit", sizeof q.label); scopy (q.key, "^Q", sizeof q.key); q.sep = false; q.sub = -1;
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
	add (ONYX_TERMINAL, "Terminal", -1); add (ONYX_CONTROL, "Control Panel", -1);
	add (ONYX_FILES, "File Viewer", -1); add (ONYX_TASKS, "Task Manager", -1);
	add (-2, 0, -1);
	// the categories: the usual ones first, then any other, "Other" last
	g_nsubs = 0;
	static const char *const order[] = { "Productivity", "Internet", "Graphics", "Games", "BASIC", "Demos", "System", 0 };
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
		if (sd.count) add (ONYX_SUB, cats[c], g_nsubs++);
	}
	add (-2, 0, -1);
	SubDef &w = g_subs[g_nsubs]; w.count = 0; w.windows = true;
	add (ONYX_SUB, "Open Windows", g_nsubs++);
	add (-2, 0, -1);
	add (ONYX_SHUTDOWN, "Shut Down...", -1);
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
	if (sd.count == 0) w = uk_tw ("(no open window)") + 28;
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
static int wifi_x (void) { return clk_x () - 27; }
static int spk_x (void) { return wifi_x () - 26; }
static bool on_wifi (int x, int y) { return y >= 0 && y < BAR_H && x >= wifi_x () - 3 && x < wifi_x () + 20; }
static bool on_speaker (int x, int y) { return y >= 0 && y < BAR_H && x >= spk_x () - 3 && x < spk_x () + 19; }
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
	uk_text_l (g_cv, bx + 14, by + 6, 24, "Volume", C_FIELD_TEXT, 2);
	char v[8]; int n = 0;
	if (g_mute) { const char *m = "Muted"; while (m[n]) { v[n] = m[n]; n++; } }
	else { if (g_vol >= 10) { v[n++] = '1'; v[n++] = '0'; } else v[n++] = (char) ('0' + g_vol); }
	v[n] = 0;
	uk_text_l (g_cv, bx + VW - 14 - uk_tw (v), by + 6, 24, v, C_DIM);
	int x0, x1, ty; vol_track (&x0, &x1, &ty);
	int kx = x0 + (x1 - x0) * g_vol / 10;
	for (int i = 0; i <= 10; i++) g_cv.fillRect (x0 + (x1 - x0) * i / 10, ty + 10, 1, 3, C_DIM);
	uk_slider_mark (g_cv, x0 - 6, ty - 8, x1 - x0 + 12, 20, kx - x0 + 6, kx - 6, 12, g_mute ? UK_DISABLED : g_volDrag ? UK_PRESSED : UK_NORMAL);
	uk_check_mark (g_cv, bx + 18, by + 65, 15, g_mute != 0, UK_NORMAL);
	uk_text_l (g_cv, bx + 42, by + 65, 15, "Mute", C_FIELD_TEXT);
}

// ---- the calendar (a click on the time) ----------------------------------------------------------
class CalCard : public Calendar				// (its corners blend into the panel)
{
public:
	CalCard (int y, int m, int d) : Calendar (0, 0, y, m, d, 0) {}
	unsigned bgColor () override { return C_DROP; }
};
static CalCard *g_cal;
static bool g_calOpen = false, g_calBtnHot = false, g_calBtnDown = false;
#define CALW	(CAL_W + 20)
#define CALH	(CAL_H + 20 + 36)
static void cal_box (int *x, int *y) { *x = g_sw - CALW - 6; *y = BAR_H + 4; }
static void cal_btn (int *x, int *y, int *w, int *h)
{
	int bx, by; cal_box (&bx, &by);
	*w = 150; *h = 28; *x = bx + (CALW - *w) / 2; *y = by + 10 + CAL_H + 6;
}
static bool in_cal_box (int x, int y) { int bx, by; cal_box (&bx, &by); return x >= bx && x < bx + CALW && y >= by && y < by + CALH; }
static bool on_cal_btn (int x, int y) { int bx, by, bw, bh; cal_btn (&bx, &by, &bw, &bh); return x >= bx && x < bx + bw && y >= by && y < by + bh; }

static void draw_calendar_box (void)
{
	int bx, by; cal_box (&bx, &by);
	panel (bx, by, CALW, CALH);
	g_cal->draw ();
	g_cv.putOther (g_cal->canvas, bx + 10, by + 10, false);
	int x, y, w, h, lx, ly, lw, lh; cal_btn (&x, &y, &w, &h);
	uk_framed (g_cv, x, y, w, h, C_BUTTON, g_calBtnDown ? UK_PRESSED : g_calBtnHot ? UK_HOT : UK_NORMAL, &lx, &ly, &lw, &lh);
	uk_text_c (g_cv, lx, ly, lw, lh, "Open Calendar", C_BUTTON_TEXT);
}

static void draw (void)
{
	bool full = g_open >= 0 || g_volOpen || g_calOpen;
	int h = full ? g_sh : BAR_H;
	if (full) g_cv.fillRect (0, BAR_H, g_sw, g_sh - BAR_H, CATCH);	// (catches a click elsewhere)
	uk_paint_alpha (true);
	// the bar: a light gradient of the face, a light line on top, a darker one below
	uk_rbox (g_cv, 0, 0, g_sw, BAR_H - 1, 0, uk_tone (C_MENUBAR, 196), uk_tone (C_MENUBAR, 150));
	for (int x = 0; x < g_sw; x++) { uk_blend_px (g_cv, x, 0, 0x00FFFFFF, 120); g_cv.pixel (x, BAR_H - 1, C_OUT); }
	for (int i = 0; i < g_nmenus; i++)
	{
		const MenuDef &m = g_menus[i];
		if (i == g_open) uk_hilite (g_cv, m.x + 2, 3, m.w - 4, BAR_H - 7, 5, true);
		uk_text_l (g_cv, m.x + 8, 0, BAR_H - 1, m.title, i == g_open ? C_SEL_TEXT : C_BARTXT, title_style (i));
	}
	int hh = 0, mm = 0;
	kapi_get_datetime (0, 0, 0, &hh, &mm, 0);
	char clk[6] = { (char) ('0' + hh / 10), (char) ('0' + hh % 10), ':', (char) ('0' + mm / 10), (char) ('0' + mm % 10), 0 };
	int clkX = clk_x ();
	if (g_calOpen) uk_hilite (g_cv, clkX - 6, 3, clk_w () + 12, BAR_H - 7, 5, true);
	uk_text_l (g_cv, clkX, 0, BAR_H - 1, clk, g_calOpen ? C_SEL_TEXT : C_BARTXT, 2);
	g_lastMin = mm;
	if (g_wifi < 0) g_wifi = kapi_net_status (0, 0) ? 1 : 0;
	draw_wifi (wifi_x (), (BAR_H - 1 - 12) / 2, g_wifi == 1);
	draw_speaker (spk_x (), (BAR_H - 1 - 12) / 2);
	if (g_volOpen) draw_volume_box ();
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
			if (sd.count == 0) uk_text_l (g_cv, sx + 14, sy + 5, ih, sd.windows ? "(no open window)" : "(empty)", C_DIM);
			for (int i = 0; i < sd.count; i++)
			{
				int iy = sy + 5 + i * ih;
				if (i == g_subHover) uk_hilite (g_cv, sx + 4, iy, sw - 8, ih, 5, true);
				uk_text_l (g_cv, sx + 14, iy, ih, sd.items[i].label, i == g_subHover ? C_SEL_TEXT : C_FIELD_TEXT);
			}
		}
	}
	uk_paint_alpha (false);
	kapi_resize_window (g_sw, h);
	kapi_present ();
	g_dirty = false;
}

// ---- actions -------------------------------------------------------------------------------------
static void run_item (const Item &it)
{
	if (it.sep) return;
	switch (it.id)
	{
	case ONYX_TERMINAL: kapi_launch ("terminal"); return;
	case ONYX_CONTROL:  if (!kapi_raise_app ("control")) kapi_launch ("control"); return;
	case ONYX_FILES:    kapi_launch ("fileviewer"); return;
	case ONYX_TASKS:    kapi_launch ("taskman"); return;
	case ONYX_SHUTDOWN: kapi_launch ("shutdown"); return;
	case ONYX_SUB:      return;			// (it opens its sub-menu)
	}
	kapi_menu_command (it.id);		// the active app's own item (or MENU_QUIT)
}

static void open_menu (int i)
{
	g_volOpen = false; g_calOpen = false;
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
	if (windows) kapi_raise_app (it.app);
	else if (kapi_raise_app (it.app) == 0) lx_launch (it.app, 0);	// running: to the front
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
		if (!(c & 1)) break;
		if (g_calOpen && in_cal_box (x, y))			// the calendar: its month, its button
		{
			int bx, by; cal_box (&bx, &by);
			if (on_cal_btn (x, y)) g_calBtnDown = true;
			else g_cal->handleMouse (x - bx - 10, y - by - 10, 1, 0, 0, 0);
			g_dirty = true;
			break;
		}
		if (on_clock (x, y))					// the calendar, open / closed
		{
			if (g_open >= 0) close_menu ();
			g_volOpen = false;
			g_calOpen = !g_calOpen; g_dirty = true;
			if (g_calOpen)					// (today's month)
			{
				int yy = 2026, mo = 1, dd = 1; kapi_get_datetime (&yy, &mo, &dd, 0, 0, 0);
				g_cal->setDate (yy, mo, dd);
			}
			break;
		}
		if (g_calOpen) { g_calOpen = false; g_dirty = true; if (t < 0) break; }	// a click elsewhere
		if (on_speaker (x, y))					// the volume box, open / closed
		{
			if (g_open >= 0) close_menu ();
			g_volOpen = !g_volOpen; g_dirty = true;
			break;
		}
		if (on_wifi (x, y))					// the Wi-Fi menu: an app of its own
		{
			if (g_open >= 0) close_menu ();
			g_volOpen = false; g_dirty = true;
			if (kapi_raise_app ("wifimenu") == 0) lx_launch ("wifimenu", 0);
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
				g_calBtnDown = false;
				if (on_cal_btn (x, y)) { g_calOpen = false; if (kapi_raise_app ("calendar") == 0) lx_launch ("calendar", 0); }
			}
			else if (in_cal_box (x, y)) g_cal->handleMouse (x - bx - 10, y - by - 10, 0, 0, 0, 0);
			g_dirty = true;
			break;
		}
		if (g_volDrag) { g_volDrag = false; volume_save (g_vol, g_mute); break; }
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
		if (g_calOpen)
		{
			bool h = on_cal_btn (x, y);
			if (h != g_calBtnHot) { g_calBtnHot = h; g_dirty = true; }
			break;
		}
		if (g_volDrag) { vol_set_at (x); break; }
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
		break;
	case GUI_EVENT_PTR_LEAVE:
		if (g_hover != -1) { g_hover = -1; g_dirty = true; }
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

	g_fb = kapi_create_window_ex (0, 0, g_sw, g_sh, "menubar",
				      WIN_FLAG_BORDERLESS | WIN_FLAG_TOPMOST | WIN_FLAG_ALPHA | WIN_FLAG_SYSTEM);
	if (g_fb == 0) return 1;
	kapi_resize_window (g_sw, BAR_H);		// reserves the strip (the kernel keeps the minimum)
	g_cv.adopt (g_fb, g_sw, g_sh);
	uikit::init ();					// the fonts, the theme: the palette
	if (ft_uikit_install ("DejaVu Sans", 13))		// FreeType's anti-aliased text (else the bitmap font)
		g_fh = uk_fh ();
	C_BARTXT = uk_ink_on (uk_tone (C_MENUBAR, 176));
	C_BARDIM = uk_mix (uk_tone (C_MENUBAR, 176), C_BARTXT, 110);
	C_DROP = C_FIELD; C_DIM = uk_mix (C_FIELD, C_FIELD_TEXT, 130); C_OUT = uk_tone (C_MENUBAR, 70);
	{ int yy = 2026, mo = 1, dd = 1; kapi_get_datetime (&yy, &mo, &dd, 0, 0, 0); g_cal = new CalCard (yy, mo, dd); }
	kapi_set_pointer_handler (ptr);
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
			g_volOpen = false; g_calOpen = false; close_menu ();
			unsigned *fb = kapi_resize_window2 (w, h, &stride);	// (the canvas: the whole screen, for the drop-downs)
			if (fb != 0) { g_fb = fb; g_sw = w; g_sh = h; g_cv.adopt (fb, w, h, stride); }
			kapi_resize_window (g_sw, BAR_H);
			layout_titles ();
			g_dirty = true;
		}
		unsigned s = kapi_get_menu (spec, sizeof spec, title, sizeof title);
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
		if (mm != g_lastMin) g_dirty = true;
		static unsigned lastNet = 0;				// Wi-Fi state: about once a second
		unsigned now = kapi_get_ticks ();
		if (now - lastNet >= 100)
		{
			lastNet = now;
			int w = kapi_net_status (0, 0) ? 1 : 0;
			if (w != g_wifi) { g_wifi = w; g_dirty = true; }
			int r = kapi_sound_volume (-1, -1);			// (the volume command may change it)
			if ((r & 0xFF) != g_vol || ((r & 0x100) ? 1 : 0) != g_mute) { g_vol = r & 0xFF; g_mute = (r & 0x100) ? 1 : 0; g_dirty = true; }
		}
		if (g_calOpen && !g_cal->valid) g_dirty = true;		// (the calendar changed its month)
		if (g_dirty) draw ();
		msleep (g_open >= 0 || g_volOpen || g_calOpen ? 16 : 50);
	}
}
