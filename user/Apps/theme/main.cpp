//
// theme -- the desktop's look (the modernised CDE, wtk/theme.h): the colour of the window in
// front's frame (a named theme -- Peach, Steel, Sage, Brick, Slate -- or any colour), the frames
// behind, their 1-px outline (none, dark, black), the apps' face, the accent (focus, selection),
// the dock's face; and the desktop: the wallpaper's colour, the keyboard layout, the wheel's
// speed. A preview shows a window in front and one behind as they will be.
//
// Apply writes SD:/etc/theme.txt (every app reads it when it starts), the wallpaper's colour
// (apps/voronoy's config.ini, then the wallpaper is drawn again), switches the keymap and the
// wheel speed live, and restarts the menu bar, the dock and the agenda so they take the new
// colours at once; the apps already open keep theirs until they are opened again. Discard reloads
// what is saved.
//
#include "kapi.h"
#include "applib.h"
#include "wtk/wtk.h"

using namespace wtk;

#define W	520
#define H	452

static const unsigned DEF_FACE = 0x00D0C2BA, DEF_ACCENT = 0x004992A7, DEF_DOCK = 0x00A4BACE,
		      DEF_WALL = 0x004878B0;
static const char *const OUTLINES[3] = { "None", "Dark", "Black" };

// The values being edited (-1 theme: a custom colour).
static int      g_theme = 0;
static unsigned g_active = 0x00F0B07A, g_inactive = WK_GREY, g_face = DEF_FACE, g_accent = DEF_ACCENT,
		g_dock = DEF_DOCK, g_wall = DEF_WALL;
static int      g_outline = 1, g_wheel = 2;

#define MAXKM	24
static char        g_kmname[MAXKM][12];
static const char *g_kmopt[MAXKM];
static int         g_nkm;

// ---- the widgets ------------------------------------------------------------------------------------
// A named theme: its colour in a rounded swatch, its name below; the chosen one ringed.
class Swatch : public Widget
{
public:
	int idx;
	Swatch (int l, int t, int i) : Widget (l, t, 64, 50), idx (i) { canFocus = true; }
	void onDraw () override;
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override;
};

// The preview: a window behind, a window in front (their frames, the face, a button, a field
// with a selection) in the colours being edited.
class Preview : public Widget
{
public:
	Preview (int l, int t, int w, int h) : Widget (l, t, w, h) {}
	void onDraw () override;
};

static Swatch      *g_sw[5];
static ColorPicker *g_pkActive, *g_pkInactive, *g_pkFace, *g_pkAccent, *g_pkDock, *g_pkWall;
static Dropdown    *g_ddOutline, *g_ddKeymap;
static NumericUpDown *g_nuWheel;
static Preview     *g_preview;
static Label       *g_status;

static void changed (void)
{
	for (int i = 0; i < 5; i++) g_sw[i]->invalidate (true);
	g_preview->invalidate (true);
}

void Swatch::onDraw ()
{
	canvas.clear (bgColor ());
	unsigned c = wk_themes[idx].frame;
	bool on = g_theme == idx;
	if (on) wk_rline (canvas, 8, 0, 48, 32, 9, C_ACCENT, 255);
	if (on) wk_rline (canvas, 9, 1, 46, 30, 8, C_ACCENT, 160);
	wk_rbox (canvas, 11, 3, 42, 26, 6, wk_tone (c, 166), wk_tone (c, 112));
	wk_rline (canvas, 11, 3, 42, 26, 6, wk_tone (c, 64), 190);
	if (hasFocus) wk_rline (canvas, 11, 3, 42, 26, 6, C_ACCENT, 255);
	wk_text_c (canvas, 0, 32, width, 18, wk_themes[idx].name, C_TEXT, on ? 2 : 0);
}

bool Swatch::onMouse (int mx, int, int bl, int, int, int)
{
	if (mx < 0) { pressed = false; return false; }
	if (bl && !pressed)
	{
		pressed = true; setFocus ();
		g_theme = idx; g_active = wk_themes[idx].frame;
		g_pkActive->color = g_active; g_pkActive->invalidate (true);
		changed ();
	}
	else if (!bl) pressed = false;
	return true;
}

// Draw with the edited palette (the app's own is put back after).
struct PaletteSwap
{
	unsigned bg, face, hi, dn, border, text, accent, dis, field, ftext, sel, fa, fi; int ol;
	PaletteSwap ()
	{
		bg = C_BG; face = C_FACE; hi = C_FACE_HI; dn = C_FACE_DN; border = C_BORDER; text = C_TEXT;
		accent = C_ACCENT; dis = C_DIS; field = C_FIELD; ftext = C_FIELD_TEXT; sel = C_SEL_TEXT;
		fa = C_FRAME_ACTIVE; fi = C_FRAME_INACTIVE; ol = WK_OUTLINE;
		C_ACCENT = g_accent; C_FRAME_ACTIVE = g_active; C_FRAME_INACTIVE = g_inactive; WK_OUTLINE = g_outline;
		wk_theme_face (g_face);
	}
	~PaletteSwap ()
	{
		C_BG = bg; C_FACE = face; C_FACE_HI = hi; C_FACE_DN = dn; C_BORDER = border; C_TEXT = text;
		C_ACCENT = accent; C_DIS = dis; C_FIELD = field; C_FIELD_TEXT = ftext; C_SEL_TEXT = sel;
		C_FRAME_ACTIVE = fa; C_FRAME_INACTIVE = fi; WK_OUTLINE = ol;
	}
};

// One small window at (x, y): its frame (the corners blended over the preview's back), its face,
// a framed button, a field with a selection.
static void mini_window (Canvas &cv, int x, int y, int w, int h, const char *title, bool active)
{
	static unsigned buf[260 * 180];
	if (w * h > (int) (sizeof buf / sizeof buf[0])) return;
	wk_draw_frame (buf, w, h, KAPI_FRAME_TITLE_H, title, active ? g_active : g_inactive, active);
	for (int j = 0; j < h; j++)
		for (int i = 0; i < w; i++)
		{
			unsigned c = buf[j * w + i], t = c >> 24;
			if (t == 255) continue;
			if (t == 0) cv.pixel (x + i, y + j, c & 0xFFFFFF);
			else wk_blend_px (cv, x + i, y + j, c & 0xFFFFFF, 255 - (int) t);
		}
	int cx = x + KAPI_FRAME_BORDER, cy = y + KAPI_FRAME_TITLE_H, cw = w - 2 * KAPI_FRAME_BORDER, ch = h - KAPI_FRAME_TITLE_H - KAPI_FRAME_BORDER;
	cv.fillRect (cx, cy, cw, ch, C_BG);
	int bx, by, bw, bh;
	wk_framed (cv, cx + 10, cy + 10, 76, 28, C_FACE, active ? WK_NORMAL : WK_NORMAL, &bx, &by, &bw, &bh);
	wk_text_c (cv, bx, by, bw, bh, "OK", C_TEXT);
	wk_sunken (cv, cx + 96, cy + 12, cw - 106, 24, 4, C_FIELD, active);
	wk_hilite (cv, cx + 100, cy + 16, 36, 16, 3, true);
	cv.text (cx + 102, cy + 16, "Text", C_SEL_TEXT);
	cv.text (cx + 140, cy + 16, "field", C_FIELD_TEXT);
	wk_check_mark (cv, cx + 10, cy + 48, 14, true, WK_NORMAL);
	cv.text (cx + 30, cy + 47, "Check", C_TEXT);
	wk_radio_mark (cv, cx + 96, cy + 48, 14, true, WK_NORMAL);
	cv.text (cx + 116, cy + 47, "Radio", C_TEXT);
}

void Preview::onDraw ()
{
	unsigned back = wk_tone (g_wall, 150);				// (the wallpaper, lighter)
	wk_rbox (canvas, 0, 0, width, height, 0, wk_tone (g_wall, 176), back);
	{
		PaletteSwap swap;
		mini_window (canvas, 10, 8, 250, 110, "Behind", false);
		mini_window (canvas, width - 262, height - 118, 252, 110, "In front", true);
	}
	for (int y = 0; y < height; y++)					// (a frame round the preview)
		for (int x = 0; x < width; x++)
			if (x == 0 || y == 0 || x == width - 1 || y == height - 1) canvas.pixel (x, y, wk_tone (C_FACE, 80));
}

// ---- load / apply --------------------------------------------------------------------------------------
static unsigned parse_color (const char *s, unsigned def)
{
	if (s == 0) return def;
	while (*s == ' ' || *s == '\t') s++;
	if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
	else if (s[0] == '#') s++;
	unsigned v = 0; int any = 0;
	for (; *s; s++)
	{
		char c = *s; int d;
		if (c >= '0' && c <= '9') d = c - '0';
		else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
		else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
		else break;
		v = v * 16 + (unsigned) d; any = 1;
	}
	return any ? v & 0xFFFFFF : def;
}
static int put_color (char *b, unsigned c)
{
	const char *hx = "0123456789ABCDEF";
	b[0] = '0'; b[1] = 'x';
	for (int i = 0; i < 6; i++) b[2 + i] = hx[(c >> ((5 - i) * 4)) & 0xF];
	return 8;
}
static bool ieq (const char *a, const char *b)
{
	for (;; a++, b++)
	{
		char x = *a, y = *b;
		if (x >= 'A' && x <= 'Z') x += 32;
		if (y >= 'A' && y <= 'Z') y += 32;
		if (x != y) return false;
		if (!x) return true;
	}
}

static void scan_keymaps (void)
{
	g_nkm = 0;
	void *dir = kapi_opendir ("SD:/etc/keymaps");
	if (dir == 0) return;
	struct kapi_dirent e;
	while (g_nkm < MAXKM && kapi_readdir (dir, &e))
	{
		if (e.is_dir) continue;
		int n = 0; while (e.name[n]) n++;
		if (n < 6 || !ieq (e.name + n - 5, ".kmap")) continue;
		int b = n - 5; if (b > 11) b = 11;
		for (int i = 0; i < b; i++) g_kmname[g_nkm][i] = e.name[i];
		g_kmname[g_nkm][b] = '\0';
		g_kmopt[g_nkm] = g_kmname[g_nkm];
		g_nkm++;
	}
	kapi_closedir (dir);
}

static void load_current (void)
{
	g_theme = 0; g_active = wk_themes[0].frame; g_inactive = WK_GREY; g_face = DEF_FACE;
	g_accent = DEF_ACCENT; g_dock = DEF_DOCK; g_outline = 1; g_wall = DEF_WALL;
	g_wheel = kapi_get_wheel_speed ();
	if (app_ini_load_path ("SD:/etc/theme.txt") > 0)
	{
		const char *t = app_ini_get (0, "theme", 0);
		if (t) for (int i = 0; wk_themes[i].name; i++) if (ieq (t, wk_themes[i].name)) { g_theme = i; g_active = wk_themes[i].frame; }
		const char *a = app_ini_get (0, "active", 0);
		if (a) { g_active = parse_color (a, g_active); g_theme = -1; for (int i = 0; wk_themes[i].name; i++) if (wk_themes[i].frame == g_active) g_theme = i; }
		g_inactive = parse_color (app_ini_get (0, "inactive", 0), g_inactive);
		g_face = parse_color (app_ini_get (0, "face", 0), g_face);
		g_accent = parse_color (app_ini_get (0, "accent", 0), g_accent);
		g_dock = parse_color (app_ini_get (0, "dock", 0), g_dock);
		const char *o = app_ini_get (0, "outline", 0);
		if (o) g_outline = ieq (o, "none") ? 0 : ieq (o, "black") ? 2 : 1;
	}
	if (app_ini_load_path ("SD:/apps/voronoy.app/config.ini") > 0) g_wall = parse_color (app_ini_get (0, "base", 0), g_wall);
	g_pkActive->color = g_active; g_pkInactive->color = g_inactive; g_pkFace->color = g_face;
	g_pkAccent->color = g_accent; g_pkDock->color = g_dock; g_pkWall->color = g_wall;
	g_ddOutline->sel = g_outline;
	char km[16]; kapi_get_keymap (km, sizeof km);
	g_ddKeymap->sel = 0;
	for (int i = 0; i < g_nkm; i++) if (ieq (km, g_kmopt[i])) g_ddKeymap->sel = i;
	g_nuWheel->value = g_wheel;
	Widget *all[] = { g_pkActive, g_pkInactive, g_pkFace, g_pkAccent, g_pkDock, g_pkWall, g_ddOutline, g_ddKeymap, g_nuWheel };
	for (unsigned i = 0; i < sizeof all / sizeof all[0]; i++) all[i]->invalidate (true);
	changed ();
}

static void apply (void)
{
	static char buf[1024]; int p = 0;
	ax_strcat (buf, sizeof buf, &p,
		"# The desktop's look (the modernised CDE), read by every app when it starts (wtk/theme.h);\n"
		"# written by the Theme app. theme: Peach, Steel, Sage, Brick, Slate (the window in front's frame;\n"
		"# active = a colour instead); inactive: the frames behind; face: the apps'; accent: focus and\n"
		"# selection; outline: none, dark or black; dock: the dock's face. wheelspeed: lines a notch.\n");
	if (g_theme >= 0) { ax_strcat (buf, sizeof buf, &p, "theme    = "); ax_strcat (buf, sizeof buf, &p, wk_themes[g_theme].name); buf[p++] = '\n'; }
	else { ax_strcat (buf, sizeof buf, &p, "active   = "); p += put_color (buf + p, g_active); buf[p++] = '\n'; }
	ax_strcat (buf, sizeof buf, &p, "inactive = "); p += put_color (buf + p, g_inactive); buf[p++] = '\n';
	ax_strcat (buf, sizeof buf, &p, "face     = "); p += put_color (buf + p, g_face); buf[p++] = '\n';
	ax_strcat (buf, sizeof buf, &p, "accent   = "); p += put_color (buf + p, g_accent); buf[p++] = '\n';
	ax_strcat (buf, sizeof buf, &p, "outline  = "); ax_strcat (buf, sizeof buf, &p, g_outline == 0 ? "none" : g_outline == 2 ? "black" : "dark"); buf[p++] = '\n';
	ax_strcat (buf, sizeof buf, &p, "dock     = "); p += put_color (buf + p, g_dock); buf[p++] = '\n';
	ax_strcat (buf, sizeof buf, &p, "wheelspeed="); p += ax_itoa (g_wheel, buf + p); buf[p++] = '\n';
	kapi_save_file ("SD:/etc/theme.txt", buf, (unsigned) p);
	kapi_set_wheel_speed (g_wheel);

	p = 0;								// the wallpaper
	ax_strcat (buf, sizeof buf, &p, "base="); p += put_color (buf + p, g_wall); buf[p++] = '\n';
	ax_strcat (buf, sizeof buf, &p, "points=28\n");
	kapi_save_file ("SD:/apps/voronoy.app/config.ini", buf, (unsigned) p);
	if (g_ddKeymap->sel >= 0 && g_ddKeymap->sel < g_nkm) ax_load_keymap (g_kmopt[g_ddKeymap->sel]);
	kapi_exec ("SD:apps/voronoy.app/main", "");

	static const char *const shell[] = { "menubar", "dock", "agenda" };	// the new colours at once
	for (int i = 0; i < 3; i++) { kapi_kill (shell[i]); kapi_launch (shell[i]); }
	g_status->setText ("Applied: the apps opened from now on take it.");
}

static void on_pick (Widget &)
{
	g_active = g_pkActive->color; g_inactive = g_pkInactive->color; g_face = g_pkFace->color;
	g_accent = g_pkAccent->color; g_dock = g_pkDock->color; g_wall = g_pkWall->color;
	g_theme = -1;
	for (int i = 0; wk_themes[i].name; i++) if (wk_themes[i].frame == g_active) g_theme = i;
	changed ();
}
static void on_outline (Widget &w) { g_outline = ((Dropdown &) w).sel; changed (); }
static void on_wheel (Widget &w) { g_wheel = ((NumericUpDown &) w).value; kapi_set_wheel_speed (g_wheel); }
static void on_apply (Widget &) { apply (); }
static void on_discard (Widget &) { load_current (); g_status->setText (""); }

int main (void)
{
	Root root (W, H, "Theme");
	if (root.canvas.px == 0) return 1;
	scan_keymaps ();

	GroupBox *gw = new GroupBox (10, 6, W - 20, 138, "Windows");
	root.addChild (gw);
	gw->addChild (new Label (12, 26, 120, 20, "In front", C_TEXT, gw->bg));
	for (int i = 0; i < 5; i++) { g_sw[i] = new Swatch (120 + i * 66, 22, i); gw->addChild (g_sw[i]); }
	g_pkActive = new ColorPicker (455, 25, 26, 24, g_active, on_pick); g_pkActive->tip = "Any colour"; gw->addChild (g_pkActive);
	gw->addChild (new Label (12, 84, 110, 20, "Behind", C_TEXT, gw->bg));
	g_pkInactive = new ColorPicker (120, 82, 40, 24, g_inactive, on_pick); gw->addChild (g_pkInactive);
	gw->addChild (new Label (200, 84, 70, 20, "Outline", C_TEXT, gw->bg));
	g_ddOutline = new Dropdown (272, 80, 120, 28, OUTLINES, 3, 1, on_outline); gw->addChild (g_ddOutline);

	GroupBox *ga = new GroupBox (10, 150, 190, 128, "Colours");
	root.addChild (ga);
	ga->addChild (new Label (12, 28, 90, 20, "Apps", C_TEXT, ga->bg));
	g_pkFace = new ColorPicker (110, 26, 60, 24, g_face, on_pick); ga->addChild (g_pkFace);
	ga->addChild (new Label (12, 60, 90, 20, "Accent", C_TEXT, ga->bg));
	g_pkAccent = new ColorPicker (110, 58, 60, 24, g_accent, on_pick); ga->addChild (g_pkAccent);
	ga->addChild (new Label (12, 92, 90, 20, "Dock", C_TEXT, ga->bg));
	g_pkDock = new ColorPicker (110, 90, 60, 24, g_dock, on_pick); ga->addChild (g_pkDock);

	g_preview = new Preview (210, 158, W - 220, 244);
	root.addChild (g_preview);

	GroupBox *gd = new GroupBox (10, 284, 190, 118, "Desktop");
	root.addChild (gd);
	gd->addChild (new Label (12, 28, 90, 20, "Wallpaper", C_TEXT, gd->bg));
	g_pkWall = new ColorPicker (110, 26, 60, 24, g_wall, on_pick); gd->addChild (g_pkWall);
	gd->addChild (new Label (12, 58, 90, 20, "Keyboard", C_TEXT, gd->bg));
	g_ddKeymap = new Dropdown (100, 54, 78, 28, g_kmopt, g_nkm, 0, 0); gd->addChild (g_ddKeymap);
	gd->addChild (new Label (12, 90, 90, 20, "Wheel", C_TEXT, gd->bg));
	g_nuWheel = new NumericUpDown (100, 86, 78, 26, 1, 16, 2, 1, on_wheel); g_nuWheel->tip = "Lines a notch"; gd->addChild (g_nuWheel);

	g_status = new Label (12, H - 38, 250, 24, "", C_DIS, root.bg);
	root.addChild (g_status);
	root.addChild (new Button (W - 196, H - 42, 90, 32, "Apply", on_apply));
	root.addChild (new Button (W - 100, H - 42, 90, 32, "Discard", on_discard));

	load_current ();
	root.run ();
	return 0;
}
