//
// menubar -- the system menu bar across the top of the screen (macOS-style).
//
// It shows the ACTIVE app's name and its menus (kapi_get_menu, declared by the app with
// wtk::Menu / kapi_set_menu), opens a drop-down on click and sends the chosen command
// back to the app (kapi_menu_command). The first menu is always the "Onyx" system menu
// (launchers + Shut Down...), then the app's name with "Quit" (MENU_QUIT), then its
// menus. The clock sits on the right, with the Wi-Fi state left of it (arcs = connected,
// a barred circle = not connected; polled about once a second through kapi_net_status).
//
// The window is TOPMOST (always above the others, never active, never gets the keys)
// and TRANSPARENT: it is allocated full-screen but kept BAR_H tall; while a menu is
// open it grows to the whole screen, all magenta (see-through) except the bar and the
// drop-down, so a click anywhere else closes the menu.
//
// Mouse: click a title to open (click it again or anywhere outside to close), slide to
// another title to switch, click an item to run it; press on a title + release on an
// item works too.
//
#include "kapi.h"
#include "wtk/wtk.h"

using namespace wtk;

#define BAR_H		32			// bar height: 27-px face + BEVEL-px 3D edge
#define BEVEL		5			// 3D edge under the face: black, light, normal, dark, black
#define INK_TOP		2			// the 8x16 font's cap ink spans cell rows 2..11:
#define INK_H		10			// centre on the ink, not on the 16-px cell
#define MAXMENUS	12
#define MAXITEMS	24
#define KEYCOL		0x00FF00FFu		// transparent (magenta key)

static const unsigned C_BARBG = 0x00303D4D, C_BARLIGHT = 0x005A6E88, C_BARLINE = 0x00161C24, C_BARBLACK = 0x0005070A, C_BARTXT = 0x00E8ECF0,
		      C_DROP = 0x00262F3B, C_DROPHI = 0x00355070, C_DIM = 0x008A96A8, C_SEP = 0x00404A5A;

struct Item { int id; char label[40]; char key[12]; bool sep; };
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

static int slen (const char *s) { int n = 0; while (s[n]) n++; return n; }
static void scopy (char *d, const char *s, int cap) { int i = 0; for (; s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = '\0'; }

// ---- menus ------------------------------------------------------------------------------
// Onyx system-menu item ids (>= 1000: handled here, never sent to the app).
enum { ONYX_TERMINAL = 1000, ONYX_FILES, ONYX_TASKS, ONYX_APPS, ONYX_SHUTDOWN };

static void add_quit_menu (const char *app)
{
	MenuDef &m = g_menus[g_nmenus++];
	scopy (m.title, app, sizeof m.title); m.count = 0;
	Item &q = m.items[m.count++];
	q.id = MENU_QUIT; scopy (q.label, "Quit", sizeof q.label); scopy (q.key, "^Q", sizeof q.key); q.sep = false;
}

// The system menu, always first: launchers + end of session.
static void add_onyx_menu (void)
{
	MenuDef &m = g_menus[g_nmenus++];
	scopy (m.title, "Onyx", sizeof m.title); m.count = 0;
	static const struct { int id; const char *l; } it[] = {
		{ ONYX_TERMINAL, "Terminal" }, { ONYX_FILES, "File Viewer" },
		{ ONYX_TASKS, "Task Manager" }, { -2, 0 }, { ONYX_APPS, "All Apps..." },
		{ -2, 0 }, { ONYX_SHUTDOWN, "Shut Down..." } };
	for (unsigned i = 0; i < sizeof it / sizeof it[0]; i++)
	{
		Item &x = m.items[m.count++];
		x.id = it[i].id; x.sep = it[i].id == -2; x.key[0] = '\0';
		scopy (x.label, it[i].l ? it[i].l : "", sizeof x.label);
	}
}

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
			Item &s = cur->items[cur->count++]; s.sep = true; s.id = -2; s.label[0] = s.key[0] = '\0';
		}
		else if (*p == 'I' && cur && cur->count < MAXITEMS)
		{
			Item &it = cur->items[cur->count++];
			it.sep = false; it.id = 0;
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
static void layout_titles (void)
{
	int x = 10;
	for (int i = 0; i < g_nmenus; i++)
	{
		g_menus[i].x = x;
		g_menus[i].w = slen (g_menus[i].title) * g_fw + 16;
		x += g_menus[i].w;
	}
}
static int drop_w (const MenuDef &m)
{
	int w = 120;
	for (int i = 0; i < m.count; i++)
	{
		int ww = (slen (m.items[i].label) + slen (m.items[i].key) + 5) * g_fw + 20;
		if (ww > w) w = ww;
	}
	return w;
}
static int item_h (const Item &it) { return it.sep ? 9 : g_fh + 8; }
// y of a text cell whose cap ink is vertically centred in a band [top, top+h)
static int text_y (int top, int h) { return top + (h - INK_H + 1) / 2 - INK_TOP; }
static int drop_h (const MenuDef &m) { int h = 6; for (int i = 0; i < m.count; i++) h += item_h (m.items[i]); return h; }
static int drop_x (const MenuDef &m) { int x = m.x; int w = drop_w (m); if (x + w > g_sw - 2) x = g_sw - 2 - w; return x; }

static int title_at (int x, int y)
{
	if (y < 0 || y >= BAR_H) return -1;
	for (int i = 0; i < g_nmenus; i++) if (x >= g_menus[i].x && x < g_menus[i].x + g_menus[i].w) return i;
	return -1;
}
static int item_at (int x, int y)		// index in the open menu, -1 = none
{
	if (g_open < 0) return -1;
	const MenuDef &m = g_menus[g_open];
	int dx = drop_x (m), dw = drop_w (m), yy = BAR_H + 3;
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
// The bar skin: 2 states of 16 x BAR_H, 9-slice (corners 4 px wide, 1 top / BEVEL bottom rows).
static Skin &bar_skin (void)
{
	static Skin s; static bool tried = false;
	if (!tried) { tried = true; s.load ("SD:/skins/menubar.bmp", 2, 4, 4, 1, BEVEL); }
	return s;
}

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
				if (ring || bar) g_cv.fillRect (cx + dx, cy + dy, 1, 1, C_DIM);
			}
	}
}

static void draw (void)
{
	int h = g_open >= 0 ? g_sh : BAR_H;
	if (g_open >= 0) g_cv.fillRect (0, BAR_H, g_sw, g_sh - BAR_H, KEYCOL);
	// Bar + open title: the skin (SD:/skins/menubar.bmp, state 0 = bar, 1 = open title),
	// or the same look drawn by hand without it (light top row; black/light/normal/dark/black edge).
	Skin &sk = bar_skin ();
	if (sk.valid ()) sk.drawOn (g_cv.px, g_sw, h, 0, 0, 0, g_sw, BAR_H);
	else
	{
		const int e = BAR_H - BEVEL;
		g_cv.fillRect (0, 0, g_sw, BAR_H, C_BARBG);
		g_cv.fillRect (0, 0, g_sw, 1, C_BARLIGHT);
		g_cv.fillRect (0, e,     g_sw, 1, C_BARBLACK);
		g_cv.fillRect (0, e + 1, g_sw, 1, C_BARLIGHT);
		g_cv.fillRect (0, e + 3, g_sw, 1, C_BARLINE);
		g_cv.fillRect (0, e + 4, g_sw, 1, C_BARBLACK);
	}
	const int face = BAR_H - BEVEL;
	int ty = text_y (0, face);
	for (int i = 0; i < g_nmenus; i++)
	{
		const MenuDef &m = g_menus[i];
		if (i == g_open)
		{
			if (sk.valid ()) sk.drawOn (g_cv.px, g_sw, h, 1, m.x, 0, m.w, BAR_H);
			else g_cv.fillRect (m.x, 1, m.w, face - 1, C_DROPHI);
		}
		g_cv.text (m.x + 8, ty, m.title, C_BARTXT);
		if (i == (g_onyx ? 0 : 1)) g_cv.text (m.x + 9, ty, m.title, C_BARTXT);	// app name in bold
	}
	int hh = 0, mm = 0;
	kapi_get_datetime (0, 0, 0, &hh, &mm, 0);
	char clk[6] = { (char) ('0' + hh / 10), (char) ('0' + hh % 10), ':', (char) ('0' + mm / 10), (char) ('0' + mm % 10), 0 };
	int clkX = g_sw - 5 * g_fw - 12;
	g_cv.text (clkX, ty, clk, C_BARTXT);
	g_lastMin = mm;
	if (g_wifi < 0) g_wifi = kapi_net_status (0, 0) ? 1 : 0;
	draw_wifi (clkX - 27, (face - 12) / 2 + 1, g_wifi == 1);

	if (g_open >= 0)
	{
		const MenuDef &m = g_menus[g_open];
		int dx = drop_x (m), dw = drop_w (m), dh = drop_h (m);
		g_cv.fillRect (dx + 3, BAR_H + 3, dw, dh, 0x00101418);		// shadow
		g_cv.fillRect (dx, BAR_H, dw, dh, C_DROP);
		g_cv.frameRect (dx, BAR_H, dw, dh, C_BARLINE);
		int yy = BAR_H + 3;
		for (int i = 0; i < m.count; i++)
		{
			const Item &it = m.items[i];
			int ih = item_h (it);
			if (it.sep) g_cv.fillRect (dx + 6, yy + ih / 2, dw - 12, 1, C_SEP);
			else
			{
				if (i == g_hover) g_cv.fillRect (dx + 2, yy, dw - 4, ih, C_DROPHI);
				int iy = text_y (yy, ih);
				g_cv.text (dx + 12, iy, it.label, C_BARTXT);
				if (it.key[0]) g_cv.text (dx + dw - 12 - slen (it.key) * g_fw, iy, it.key, C_DIM);
			}
			yy += ih;
		}
	}
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
	case ONYX_FILES:    kapi_launch ("fileviewer"); return;
	case ONYX_TASKS:    kapi_launch ("taskman"); return;
	case ONYX_APPS:     kapi_toggle_app ("applist"); return;
	case ONYX_SHUTDOWN: kapi_launch ("shutdown"); return;
	}
	kapi_menu_command (it.id);		// the active app's own item (or MENU_QUIT)
}

static void open_menu (int i) { g_open = i; g_hover = -1; g_dirty = true; }
static void close_menu (void) { g_open = -1; g_hover = -1; g_dirty = true; }

static void ptr (unsigned long, int ev, long v)
{
	int x = GUI_PTR_X (v), y = GUI_PTR_Y (v), c = GUI_PTR_CHANGED (v);
	int t = title_at (x, y);
	switch (ev)
	{
	case GUI_EVENT_PTR_DOWN:
		if (!(c & 1)) break;
		if (t >= 0) { if (t == g_open) close_menu (); else open_menu (t); g_pressedTitle = true; }
		else if (g_open >= 0 && item_at (x, y) < 0) close_menu ();	// click outside
		break;
	case GUI_EVENT_PTR_UP:
		if (!(c & 1)) break;
		if (g_open >= 0)
		{
			int i = item_at (x, y);
			if (i >= 0) { Item it = g_menus[g_open].items[i]; close_menu (); draw (); run_item (it); }
		}
		g_pressedTitle = false;
		break;
	case GUI_EVENT_PTR_MOVE:
		if (g_open >= 0)
		{
			if (t >= 0 && t != g_open) open_menu (t);		// slide across titles
			int i = item_at (x, y);
			if (i != g_hover) { g_hover = i; g_dirty = true; }
		}
		break;
	case GUI_EVENT_PTR_LEAVE:
		if (g_hover != -1) { g_hover = -1; g_dirty = true; }
		break;
	}
}

int main (void)
{
	kapi_screen_size (&g_sw, &g_sh);
	g_fw = kapi_font_width ();  if (g_fw < 1) g_fw = 8;
	g_fh = kapi_font_height (); if (g_fh < 1) g_fh = 16;

	g_fb = kapi_create_window_ex (0, 0, g_sw, g_sh, "menubar",
				      WIN_FLAG_BORDERLESS | WIN_FLAG_TOPMOST | WIN_FLAG_TRANSPARENT | WIN_FLAG_SYSTEM);
	if (g_fb == 0) return 1;
	kapi_resize_window (g_sw, BAR_H);		// reserves the strip (the kernel keeps the minimum)
	g_cv.adopt (g_fb, g_sw, g_sh);
	kapi_set_pointer_handler (ptr);

	static char spec[WIN_MENU_MAX_USER], title[48];
	unsigned serial = ~0u;
	onyx_menu (); layout_titles ();
	for (;;)
	{
		pump_events ();
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
		}
		if (g_dirty) draw ();
		msleep (g_open >= 0 ? 16 : 50);
	}
}
