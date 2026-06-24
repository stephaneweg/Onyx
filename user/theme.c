//
// theme -- a desktop theme editor. Pick the window-chrome colours (active/inactive
// skin tint + title text), the wallpaper colour, and the keyboard layout. Apply
// writes the config files (SD:skins/theme.txt, voronoy/config.ini), re-tints the
// window chrome live (kapi_set_window_theme), switches the keymap live, and re-runs
// voronoy to repaint the wallpaper. Discard reloads the saved values.
//
// Fully app-drawn (canvas + canvas-click): the colour pickers / dropdown / buttons
// are drawn into our own canvas via the applib widgets and hit-tested by on_click.
//
#include "kapi.h"
#include "applib.h"

#define W	380
#define H	340
#define BG	0x00202833

// Colour pickers: 0 active tint, 1 inactive tint, 2 title text, 3 wallpaper base.
static unsigned *fb;
static ax_colorpick pk[4];
static const char *const PK_LABEL[4] = { "Active tint", "Inactive tint", "Title text", "Wallpaper" };

static const char *const KEYMAPS[] = { "US", "UK", "DE", "FR", "ES", "IT", "DV" };
static ax_dropdown kmdd = { 130, 36, 90, 18, KEYMAPS, 7, 3, 0 };	// at top; list expands down

// Apply / Discard buttons (app-drawn).
#define BTN_Y	300
static int in_rect (int x, int y, int rx, int ry, int rw, int rh)
{
	return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

// "0xRRGGBB" parser (hex), and 8-char writer.
static unsigned parse_color (const char *s, unsigned def)
{
	if (s == 0) return def;
	while (*s == ' ' || *s == '\t') s++;
	if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
	unsigned v = 0; int any = 0;
	for (; *s; s++)
	{
		char c = *s; int d;
		if (c >= '0' && c <= '9') d = c - '0';
		else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
		else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
		else break;
		v = v * 16 + d; any = 1;
	}
	return any ? v : def;
}
static int put_color (char *b, unsigned c)
{
	const char *hx = "0123456789ABCDEF";
	b[0] = '0'; b[1] = 'x';
	for (int i = 0; i < 6; i++) b[2 + i] = hx[(c >> ((5 - i) * 4)) & 0xF];
	return 8;
}

// Load the saved values into the widgets (also the Discard action).
static void load_current (void)
{
	unsigned act = 0x00FFC878, inact = 0x008090A0, text = 0x00FFFFFF, wall = 0x004878B0;
	if (app_ini_load_path ("SD:etc/theme.txt") > 0)
	{
		act   = parse_color (app_ini_get (0, "active", 0),   act);
		inact = parse_color (app_ini_get (0, "inactive", 0), inact);
		text  = parse_color (app_ini_get (0, "text", 0),     text);
	}
	if (app_ini_load_path ("SD:apps/voronoy.app/config.ini") > 0)
		wall = parse_color (app_ini_get (0, "base", 0), wall);

	pk[0].color = act; pk[1].color = inact; pk[2].color = text; pk[3].color = wall;
	for (int i = 0; i < 4; i++) pk[i].open = 0;

	char km[16]; kmdd.sel = 3;		// default FR
	kapi_get_keymap (km, sizeof (km));
	for (int i = 0; i < 7; i++) if (ax_streq (km, KEYMAPS[i])) kmdd.sel = i;
	kmdd.open = 0;
}

static void apply (void)
{
	char buf[160]; int p = 0;
	// SD:etc/theme.txt
	ax_strcat (buf, sizeof buf, &p, "active=");   p += put_color (buf + p, pk[0].color); buf[p++] = '\n';
	ax_strcat (buf, sizeof buf, &p, "inactive="); p += put_color (buf + p, pk[1].color); buf[p++] = '\n';
	ax_strcat (buf, sizeof buf, &p, "text=");     p += put_color (buf + p, pk[2].color); buf[p++] = '\n';
	kapi_save_file ("SD:etc/theme.txt", buf, p);

	// voronoy wallpaper colour
	p = 0;
	ax_strcat (buf, sizeof buf, &p, "base=");     p += put_color (buf + p, pk[3].color); buf[p++] = '\n';
	ax_strcat (buf, sizeof buf, &p, "points=28\n");
	kapi_save_file ("SD:apps/voronoy.app/config.ini", buf, p);

	// Apply live: re-tint window chrome, switch keymap, repaint the wallpaper.
	kapi_set_window_theme (pk[0].color, pk[1].color, pk[2].color);
	kapi_set_keymap (KEYMAPS[kmdd.sel]);
	kapi_exec ("SD:apps/voronoy.app/main.elf", "");
}

static void draw_button (int x, const char *label, unsigned bg)
{
	ax_fill (fb, W, H, x, BTN_Y, 80, 24, bg);
	ax_frame (fb, W, H, x, BTN_Y, 80, 24, 0x00c0c0c0);
	kapi_draw_text (x + 14, BTN_Y + 4, label, 0x00ffffff);
}

static void redraw (void)
{
	for (int i = 0; i < W * H; i++) fb[i] = BG;
	kapi_draw_text (12, 8, "Theme editor", 0x00ffd070);

	for (int i = 0; i < 4; i++)
		kapi_draw_text (12, pk[i].y + 1, PK_LABEL[i], 0x00d0d8e0);
	kapi_draw_text (12, kmdd.y + 1, "Keymap", 0x00d0d8e0);

	// Closed widgets + buttons first, then the single open overlay on top.
	for (int i = 0; i < 4; i++) if (!pk[i].open) ax_colorpick_draw (&pk[i], fb, W, H);
	if (!kmdd.open) ax_dropdown_draw (&kmdd, fb, W, H);
	draw_button (130, "Apply", 0x00306030);
	draw_button (220, "Discard", 0x00603030);
	for (int i = 0; i < 4; i++) if (pk[i].open) ax_colorpick_draw (&pk[i], fb, W, H);
	if (kmdd.open) ax_dropdown_draw (&kmdd, fb, W, H);
}

static void close_all_but (int keep)	// enforce a single open overlay
{
	for (int i = 0; i < 4; i++) if (i != keep) pk[i].open = 0;
	if (keep != 4) kmdd.open = 0;
}

static void on_click (unsigned long s, int ev, long val)
{
	(void) s;
	if (ev != GUI_EVENT_CANVAS_CLICK) return;
	int x = (int) ((val >> 16) & 0xFFFF), y = (int) (val & 0xFFFF);

	for (int i = 0; i < 4; i++)
		if (ax_colorpick_click (&pk[i], x, y)) { close_all_but (i); return; }
	if (ax_dropdown_click (&kmdd, x, y)) { close_all_but (4); return; }

	if (in_rect (x, y, 130, BTN_Y, 80, 24)) { apply (); return; }
	if (in_rect (x, y, 220, BTN_Y, 80, 24)) { load_current (); return; }

	close_all_but (-1);			// click elsewhere: close everything
}

int main (void)
{
	fb = kapi_create_window (W, H, "theme");
	if (fb == 0) return 1;

	for (int i = 0; i < 4; i++) { pk[i].x = 130; pk[i].y = 70 + i * 30; pk[i].w = 70; pk[i].h = 20; }
	load_current ();

	kapi_set_click_handler (on_click);
	while (!should_exit ())
	{
		pump_events ();
		redraw ();
		msleep (30);
	}
	return 0;
}
