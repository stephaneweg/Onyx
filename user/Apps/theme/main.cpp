//
// theme -- the Control Panel's Theme applet (applet_proto.h; alone, a window of its own): the
// desktop's look, as Windows 98's Display Properties. On the left a PREVIEW of a desktop (the
// wallpaper, the menu bar, a window behind, a window in front with a button, a text field, a
// selection and a check, the dock) drawn in the colours being edited; click a part of it -- or pick
// it in Item -- and give it a colour: one of the palette's, or any colour (Custom...: red, green,
// blue). The parts (uikit/theme.h): the frame of the window in front and of the ones behind, the
// windows' content, the buttons, the text fields and lists, the selection, the menu bar, the dock,
// the desktop. The buttons, the fields and the menu bar may follow the window's colour
// (Automatic). The scheme: the named colours of the window in front (Peach, Steel, Sage, Brick,
// Slate -- CDE's framed title buttons -- or Milk: soft greys and OS X's coloured beads, a style
// of its own: choosing it, or a CDE scheme again, also takes that style's colours for the window,
// the selection, the frames behind and the dock); the frames' outline. Below, the DESKTOP: its wallpaper (wallpaper.h) -- Voronoi cells, a
// gradient, bubbles, a colour, a picture, or a pattern (one of SD:/wallpapers' grey pictures,
// coloured by the two colours: multiplied) -- its colours, the gradient's direction, the cells'
// number, the picture's file and how it fills the screen, the pattern.
//
// Apply writes SD:/etc/theme.txt and SD:/etc/wallpaper.ini, paints the wallpaper again
// (apps/voronoy), starts the dock again (dock_reload), the menu bar and the agenda the new colours at
// once, and the Control Panel too (AP_THEME: it starts the applet again); the other apps take them
// when they are opened again. Discard reloads what is saved.
//
#include "appkit/appkit.h"
#include "dockconf.h"
#include "wallpaper.h"
#include "applet_proto.h"
#include "img/imgload.hpp"
#include "uikit/uikit.h"
#include "ft/uikitface.h"		// FreeType's text (DejaVu Sans) for every widget

using namespace uikit;

#define W	700
#define H	470
#define PVW	320			// the preview (a 1024 x 768 desktop, small)
#define PVH	240
#define NTHEMES	7			// (uk_themes: Peach .. Slate -- the Classic theme's schemes --, Milk, Dark Coffee: the Modern one's)

enum { IT_ACTIVE, IT_INACTIVE, IT_WINDOW, IT_BUTTON, IT_FIELD, IT_ACCENT, IT_MENUBAR, IT_DOCK, IT_DESKTOP, IT_N };
static const char *const ITEM_NAME[IT_N] = { "Window in front: frame", "Windows behind: frame", "Windows: content",
	"Buttons", "Text fields and lists", "Selection and focus", "Menu bar", "Dock", "Desktop" };
static const char *const OUTLINES[3] = { "None", "Dark", "Black" };
static const char *const MODES[WP_NMODES] = { "Voronoi cells", "Gradient", "Bubbles", "Solid colour", "Picture", "Pattern" };
static const char *const DIRS[2] = { "Top to bottom", "Left to right" };
static const char *const STYLES[2] = { "Fill the screen", "Tile" };

// The palette: the colour themes', CDE's, greys, then deeper tones.
static const unsigned PAL[24] = {
	0x00F0B07A, 0x007A98C0, 0x0080AA76, 0x00C45450, 0x003A4458, 0x00ACACB0, 0x00D0C2BA, 0x00ECE6DE,
	0x00FFFFFF, 0x00C8C8C8, 0x00808080, 0x00303030,
	0x004992A7, 0x00A4BACE, 0x004878B0, 0x001C2C48, 0x00E0A030, 0x00D06A30, 0x0098B040, 0x003E9A6A,
	0x007E62B0, 0x00C0607E, 0x006A4A3A, 0x00000000 };

static UkTheme   g_t, g_saved;			// being edited / the app's own
static Wallpaper g_wp;
static int       g_item = IT_ACTIVE;
static Root     *g_root;

static unsigned item_colour (int it)
{
	switch (it)
	{
	case IT_ACTIVE:   return g_t.active;
	case IT_INACTIVE: return g_t.inactive;
	case IT_WINDOW:   return g_t.window;
	case IT_BUTTON:   return g_t.button;
	case IT_FIELD:    return g_t.field;
	case IT_ACCENT:   return g_t.accent;
	case IT_MENUBAR:  return g_t.menubar;
	case IT_DOCK:     return g_t.dock;
	default:          return g_wp.c1;
	}
}
static bool can_auto (int it) { return it == IT_BUTTON || it == IT_FIELD || it == IT_MENUBAR; }

// What an automatic colour is, the palette of the edited theme applied (a swatch shows it).
static unsigned shown_colour (int it)
{
	unsigned c = item_colour (it);
	if (c != UK_AUTO) return c;
	UkTheme keep; uk_theme_get (keep);
	uk_theme_set (g_t);
	unsigned r = it == IT_BUTTON ? C_BUTTON : it == IT_FIELD ? C_FIELD : C_MENUBAR;
	uk_theme_set (keep);
	return r;
}

// ---- the preview ------------------------------------------------------------------------------------
struct Hot { int x, y, w, h, item; };
static Hot  g_hot[16];
static int  g_nhot;
static unsigned g_wall[PVW * PVH];		// the wallpaper, small (made again when it changes)
static bool g_wallOk;
static unsigned *g_pic; static int g_picW, g_picH; static char g_picPath[200];
// the patterns (SD:/wallpapers: their files, their names), the one shown's grey at the preview's size
#define MAXPAT	16
static char g_patFile[MAXPAT][48], g_patName[MAXPAT][24];
static const char *g_patItems[MAXPAT];
static int g_npat;
static unsigned char g_patGrey[PVW * PVH]; static char g_patPath[200]; static bool g_patOk;

static void scan_patterns (void)
{
	g_npat = 0;
	void *d = kapi_opendir (WALLPAPER_DIR);
	struct kapi_dirent e;
	while (d && g_npat < MAXPAT && kapi_readdir (d, &e))
	{
		if (e.is_dir || e.name[0] == '.' || !img_is_image_name (e.name)) continue;
		int n = 0; for (; e.name[n] && n < 47; n++) g_patFile[g_npat][n] = e.name[n];
		g_patFile[g_npat][n] = 0;
		int k = 0;					// "low-poly.png" -> "Low Poly"
		for (; e.name[k] && e.name[k] != '.' && k < 23; k++)
		{
			char c = e.name[k] == '-' || e.name[k] == '_' ? ' ' : e.name[k];
			if ((k == 0 || g_patName[g_npat][k - 1] == ' ') && c >= 'a' && c <= 'z') c = (char) (c - 32);
			g_patName[g_npat][k] = c;
		}
		g_patName[g_npat][k] = 0;
		g_npat++;
	}
	if (d) kapi_closedir (d);
	for (int i = 1; i < g_npat; i++)				// by name
		for (int j = i; j > 0; j--)
		{
			const char *a = g_patName[j - 1], *b = g_patName[j]; int c = 0;
			while (*a && *a == *b) a++, b++;
			c = (unsigned char) *a - (unsigned char) *b;
			if (c <= 0) break;
			char t[48]; for (int q = 0; q < 48; q++) t[q] = g_patFile[j][q];
			for (int q = 0; q < 48; q++) g_patFile[j][q] = g_patFile[j - 1][q];
			for (int q = 0; q < 48; q++) g_patFile[j - 1][q] = t[q];
			char u[24]; for (int q = 0; q < 24; q++) u[q] = g_patName[j][q];
			for (int q = 0; q < 24; q++) g_patName[j][q] = g_patName[j - 1][q];
			for (int q = 0; q < 24; q++) g_patName[j - 1][q] = u[q];
		}
	for (int i = 0; i < g_npat; i++) g_patItems[i] = g_patName[i];
	if (g_npat == 0) { g_patItems[0] = "(none in SD:/wallpapers)"; }
}
// Which pattern the wallpaper names (its file in SD:/wallpapers), -1 none of them.
static int pattern_index (void)
{
	const char *p = g_wp.pattern, *f = p;
	for (const char *q = p; *q; q++) if (*q == '/') f = q + 1;
	for (int i = 0; i < g_npat; i++)
	{
		const char *a = g_patFile[i], *b = f;
		while (*a && (*a | 32) == (*b | 32)) a++, b++;
		if (!*a && !*b) return i;
	}
	return -1;
}

static void add_hot (int x, int y, int w, int h, int it) { if (g_nhot < 16) g_hot[g_nhot++] = { x, y, w, h, it }; }

static void make_wall (void)
{
	g_wallOk = true;
	if (g_wp.mode == WP_PATTERN)				// the gradient times the pattern's grey
	{
		wp_paint (g_wall, PVW, PVH, PVW, g_wp, 12345, 1, 0);
		bool same = true;
		for (int i = 0; same && (g_patPath[i] || g_wp.pattern[i]); i++) same = g_patPath[i] == g_wp.pattern[i];
		if (!same)
		{
			int n = 0; for (; g_wp.pattern[n] && n < (int) sizeof g_patPath - 1; n++) g_patPath[n] = g_wp.pattern[n];
			g_patPath[n] = 0;
			ImgFrames im;
			g_patOk = g_wp.pattern[0] && img_load (g_wp.pattern, &im) && im.w > 0 && im.h > 0;
			if (g_patOk) { wp_grey_cover (im.px[0], im.w, im.h, g_patGrey, PVW, PVH); img_free (&im); }
		}
		if (g_patOk) wp_multiply (g_wall, PVW, PVH, PVW, g_patGrey);
		return;
	}
	if (g_wp.mode != WP_IMAGE) { wp_paint (g_wall, PVW, PVH, PVW, g_wp, 12345, 1, 0); return; }
	for (int i = 0; i < PVW * PVH; i++) g_wall[i] = g_wp.c1;
	bool same = true;
	for (int i = 0; same && (g_picPath[i] || g_wp.image[i]); i++) same = g_picPath[i] == g_wp.image[i];
	if (!same)
	{
		delete [] g_pic; g_pic = 0; g_picW = g_picH = 0;
		int n = 0; for (; g_wp.image[n] && n < (int) sizeof g_picPath - 1; n++) g_picPath[n] = g_wp.image[n];
		g_picPath[n] = 0;
		ImgFrames im;
		if (g_wp.image[0] && img_load (g_wp.image, &im))
		{
			g_pic = im.px[0]; g_picW = im.w; g_picH = im.h;
			for (int f = 1; f < im.n; f++) delete [] im.px[f];
		}
	}
	if (g_pic == 0 || g_picW <= 0 || g_picH <= 0) return;
	for (int y = 0; y < PVH; y++)
		for (int x = 0; x < PVW; x++)
		{
			int sx, sy;
			if (g_wp.tile) { sx = (x * 1024 / PVW) % g_picW; sy = (y * 768 / PVH) % g_picH; }	// (as the screen's)
			else
			{
				long kx = (long) g_picW * 65536 / PVW, ky = (long) g_picH * 65536 / PVH, k = kx < ky ? kx : ky;
				long ox = ((long) g_picW * 65536 - k * PVW) / 2, oy = ((long) g_picH * 65536 - k * PVH) / 2;
				sx = (int) ((ox + k * x) >> 16); sy = (int) ((oy + k * y) >> 16);
				if (sx >= g_picW) sx = g_picW - 1;
				if (sy >= g_picH) sy = g_picH - 1;
			}
			unsigned c = g_pic[(long) sy * g_picW + sx], a = c >> 24;
			if (a != 255) c = ((((c >> 16) & 255) * a / 255) << 16) | ((((c >> 8) & 255) * a / 255) << 8) | ((c & 255) * a / 255);
			if (g_wp.tint)					// tinted: its grey times the colour
			{
				unsigned k = wp_lum (c) + 1, t = g_wp.c1;
				c = ((((t >> 16) & 255) * k >> 8) << 16) | ((((t >> 8) & 255) * k >> 8) << 8) | ((t & 255) * k >> 8);
			}
			g_wall[y * PVW + x] = c & 0xFFFFFF;
		}
}

// One small window at (x, y): its frame (its corners blended over what is below), its content.
static void mini_frame (Canvas &cv, int x, int y, int w, int h, const char *title, bool active)
{
	static unsigned buf[240 * 150];
	if (w * h > (int) (sizeof buf / sizeof buf[0])) return;
	uk_draw_frame (buf, w, h, KAPI_FRAME_TITLE_H, title, active ? C_FRAME_ACTIVE : C_FRAME_INACTIVE, active);
	for (int j = 0; j < h; j++)
		for (int i = 0; i < w; i++)
		{
			unsigned c = buf[j * w + i], t = c >> 24;
			if (t == 255) continue;
			if (t == 0) cv.pixel (x + i, y + j, c & 0xFFFFFF);
			else uk_blend_px (cv, x + i, y + j, c & 0xFFFFFF, 255 - (int) t);
		}
	cv.fillRect (x + KAPI_FRAME_BORDER, y + KAPI_FRAME_TITLE_H, w - 2 * KAPI_FRAME_BORDER, h - KAPI_FRAME_TITLE_H - KAPI_FRAME_BORDER, C_BG);
}

class Preview : public Widget
{
public:
	Preview (int l, int t) : Widget (l, t, PVW + 2, PVH + 2) {}
	void onDraw () override
	{
		canvas.clear (uk_tone (C_FACE, 70));
		if (!g_wallOk) make_wall ();
		for (int y = 0; y < PVH; y++)
			for (int x = 0; x < PVW; x++) canvas.px[(long) (y + 1) * canvas.stride + x + 1] = g_wall[y * PVW + x];
		g_nhot = 0;
		add_hot (0, 0, PVW, PVH, IT_DESKTOP);
		UkTheme keep; uk_theme_get (keep);
		uk_theme_set (g_t);					// (the edited colours, for this drawing)
		int flags = uk_window_flags ();				// (its windows: every title button)
		uk_window_state (UK_WIN_MENU | UK_WIN_RESIZABLE);
		int fh = uk_fh ();
		// the menu bar
		int bh = fh + 4;
		uk_rbox (canvas, 1, 1, PVW, bh, 0, uk_tone (C_MENUBAR, 150), C_MENUBAR);
		canvas.fillRect (1, 1 + bh, PVW, 1, uk_tone (C_MENUBAR, 90));
		unsigned mink = uk_ink_on (C_MENUBAR);
		uk_text (canvas, 8, 3, "Onyx", mink, 2);
		canvas.text (50, 3, "File  Edit  View", mink);
		canvas.text (PVW - 44, 3, "12:34", mink);
		add_hot (0, 0, PVW, bh + 1, IT_MENUBAR);
		// the window behind
		int ix = 12, iy = 28, iw = 196, ih = 112;
		mini_frame (canvas, 1 + ix, 1 + iy, iw, ih, "Behind", false);
		add_hot (ix, iy, iw, ih, IT_INACTIVE);
		add_hot (ix + KAPI_FRAME_BORDER, iy + KAPI_FRAME_TITLE_H, iw - 2 * KAPI_FRAME_BORDER, ih - KAPI_FRAME_TITLE_H - KAPI_FRAME_BORDER, IT_WINDOW);
		canvas.text (1 + ix + 12, 1 + iy + KAPI_FRAME_TITLE_H + 8, "Some text", C_TEXT);
		canvas.text (1 + ix + 12, 1 + iy + KAPI_FRAME_TITLE_H + 8 + fh + 2, "Dimmed", C_DIS);
		// the window in front: a button, a field with a selection, a check
		int ax = 100, ay = 72, aw = 212, ah = 128;
		mini_frame (canvas, 1 + ax, 1 + ay, aw, ah, "In front", true);
		add_hot (ax, ay, aw, ah, IT_ACTIVE);
		int cx = ax + KAPI_FRAME_BORDER, cy = ay + KAPI_FRAME_TITLE_H, cw = aw - 2 * KAPI_FRAME_BORDER, ch = ah - KAPI_FRAME_TITLE_H - KAPI_FRAME_BORDER;
		add_hot (cx, cy, cw, ch, IT_WINDOW);
		int bx, by, bw, bbh;
		uk_framed (canvas, 1 + cx + 10, 1 + cy + 10, 64, 28, C_BUTTON, UK_NORMAL, &bx, &by, &bw, &bbh);
		uk_text_c (canvas, bx, by, bw, bbh, "OK", C_BUTTON_TEXT);
		add_hot (cx + 10, cy + 10, 64, 28, IT_BUTTON);
		uk_sunken (canvas, 1 + cx + 84, 1 + cy + 12, cw - 94, 24, 4, C_FIELD, true);
		add_hot (cx + 84, cy + 12, cw - 94, 24, IT_FIELD);
		uk_hilite (canvas, 1 + cx + 88, 1 + cy + 16, 36, fh, 3, true);
		canvas.text (1 + cx + 90, 1 + cy + 16, "Text", C_SEL_TEXT);
		canvas.text (1 + cx + 128, 1 + cy + 16, "field", C_FIELD_TEXT);
		add_hot (cx + 88, cy + 16, 36, fh, IT_ACCENT);
		uk_check_mark (canvas, 1 + cx + 10, 1 + cy + 50, 14, true, UK_NORMAL);
		canvas.text (1 + cx + 30, 1 + cy + 49, "Check", C_TEXT);
		add_hot (cx + 10, cy + 50, 14, 14, IT_ACCENT);
		uk_radio_mark (canvas, 1 + cx + 100, 1 + cy + 50, 14, true, UK_NORMAL);
		canvas.text (1 + cx + 120, 1 + cy + 49, "Radio", C_TEXT);
		// the dock
		int dx = 70, dy = PVH - 30, dw = 180, dh = 26;
		uk_rbox (canvas, 1 + dx, 1 + dy, dw, dh, 7, uk_tone (C_DOCK, 172), uk_tone (C_DOCK, 120));
		uk_rline (canvas, 1 + dx, 1 + dy, dw, dh, 7, uk_tone (C_DOCK, 70), 170);
		static const unsigned ICONS[5] = { 0x00F0F0F0, 0x004878B0, 0x00E0A030, 0x0060A060, 0x00C05050 };
		for (int i = 0; i < 5; i++) uk_rbox (canvas, 1 + dx + 8 + i * 20, 1 + dy + 5, 16, 16, 3, uk_tone (ICONS[i], 150), ICONS[i]);
		for (int i = 0; i < 2; i++)
		{
			unsigned f = i == 0 ? uk_mix (C_ACCENT, 0x00FFFFFF, 60) : uk_tone (C_DOCK, 128);
			uk_rbox (canvas, 1 + dx + 112 + i * 16, 1 + dy + 7, 13, 11, 2, f, f);
			uk_rline (canvas, 1 + dx + 112 + i * 16, 1 + dy + 7, 13, 11, 2, i == 0 ? C_ACCENT : uk_tone (C_DOCK, 70), 200);
		}
		uk_rbox (canvas, 1 + dx + dw - 26, 1 + dy + 6, 12, 15, 2, uk_tone (C_DOCK, 76), uk_tone (C_DOCK, 60));
		add_hot (dx, dy, dw, dh, IT_DOCK);
		uk_window_state (flags);
		uk_theme_set (keep);
		// the part chosen: outlined
		for (int i = g_nhot - 1; i >= 0; i--)
			if (g_hot[i].item == g_item)
			{
				const Hot &h = g_hot[i];
				for (int k = 0; k < h.w; k += 2) { canvas.pixel (1 + h.x + k, 1 + h.y, 0x00FFFFFF); canvas.pixel (1 + h.x + k, h.y + h.h, 0x00FFFFFF); }
				for (int k = 0; k < h.h; k += 2) { canvas.pixel (1 + h.x, 1 + h.y + k, 0x00FFFFFF); canvas.pixel (h.x + h.w, 1 + h.y + k, 0x00FFFFFF); }
				for (int k = 1; k < h.w; k += 2) { canvas.pixel (1 + h.x + k, 1 + h.y, 0); canvas.pixel (1 + h.x + k, h.y + h.h, 0); }
				for (int k = 1; k < h.h; k += 2) { canvas.pixel (1 + h.x, 1 + h.y + k, 0); canvas.pixel (h.x + h.w, 1 + h.y + k, 0); }
				break;
			}
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override;
};

// ---- the controls ---------------------------------------------------------------------------------
class Swatch : public Widget
{
public:
	int idx;
	Swatch (int l, int t, int w, int i) : Widget (l, t, w, 48), idx (i) { canFocus = true; }
	void onDraw () override
	{
		canvas.clear (bgColor ());
		unsigned c = uk_themes[idx].frame;
		bool on = g_t.theme == idx, milk = uk_themes[idx].style == UK_STYLE_MILK;
		int x = (width - 42) / 2;
		if (on) { uk_rline (canvas, x - 3, 0, 48, 32, 9, C_ACCENT, 255); uk_rline (canvas, x - 2, 1, 46, 30, 8, C_ACCENT, 160); }
		uk_rbox (canvas, x, 3, 42, 26, 6, !milk ? uk_tone (c, 166) : uk_bright (c) < 110 ? c : uk_tone (c, 230),	// (the Modern ones: down to
			 milk ? uk_theme_palette (idx).face : uk_tone (c, 112));						//  their windows' colour)
		uk_rline (canvas, x, 3, 42, 26, 6, uk_tone (c, 64), 190);
		if (milk)						// (its beads)
		{
			static const unsigned bead[3] = { 0x00F0B43A, 0x004CB653, 0x00E8564E };
			for (int k = 0; k < 3; k++) uk_bead (canvas, x + 9 + k * 9, 12, 7, bead[k]);
		}
		uk_text_c (canvas, 0, 31, width, 17, uk_themes[idx].name, C_TEXT, on ? 2 : 0);
	}
	bool onMouse (int mx, int, int bl, int, int, int) override;
};

class Palette : public Widget
{
public:
	Palette (int l, int t) : Widget (l, t, 12 * 24, 2 * 23) {}
	void onDraw () override
	{
		canvas.clear (bgColor ());
		unsigned cur = item_colour (g_item);
		for (int i = 0; i < 24; i++)
		{
			int x = (i % 12) * 24, y = (i / 12) * 23;
			uk_rbox (canvas, x + 1, y + 1, 20, 19, 4, PAL[i], PAL[i]);
			bool on = PAL[i] == cur;
			uk_rline (canvas, x + 1, y + 1, 20, 19, 4, on ? C_ACCENT : uk_tone (C_FACE, 70), on ? 255 : 150);
			if (on) uk_rline (canvas, x, y, 22, 21, 5, C_ACCENT, 160);
		}
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override;
};

class Current : public Widget				// the chosen part's colour
{
public:
	Current (int l, int t) : Widget (l, t, 58, 30) {}
	void onDraw () override
	{
		canvas.clear (bgColor ());
		unsigned c = shown_colour (g_item);
		uk_rbox (canvas, 0, 0, width, height, 5, c, c);
		uk_rline (canvas, 1, 1, width - 2, height - 2, 4, 0x00FFFFFF, 140);
		uk_rline (canvas, 0, 0, width, height, 5, uk_tone (C_FACE, 60), 230);
		if (item_colour (g_item) == UK_AUTO) uk_text_c (canvas, 0, 0, width, height, "auto", uk_ink_on (c));
	}
};

static Preview   *g_preview;
static Swatch    *g_sw[NTHEMES];
static SegmentedControl *g_segTheme;		// Classic (CDE's framed title buttons) / Modern (the beads)
static Palette   *g_pal;
static Current   *g_curbox;
static Dropdown  *g_ddItem, *g_ddOutline, *g_ddMode, *g_ddDir, *g_ddStyle;
static Checkbox  *g_auto;
static Label     *g_lbPoints, *g_lbC2, *g_lbDir, *g_lbImage, *g_lbStyle, *g_lbC1, *g_lbPattern, *g_status;
static Dropdown  *g_ddPattern;
static Checkbox  *g_cbTint;
static ColorPicker *g_pkC1, *g_pkC2;
static NumericUpDown *g_nuPoints;
static Textbox   *g_tbImage;
static Button    *g_btBrowse;

static void refresh (bool wall = false)
{
	if (wall) g_wallOk = false;
	g_preview->invalidate (true);
	for (int i = 0; i < NTHEMES; i++) { g_sw[i]->hidden = uk_themes[i].style != g_t.style; g_sw[i]->invalidate (true); }	// (the theme's schemes)
	g_segTheme->select (g_t.style == UK_STYLE_MILK ? 1 : 0);
	g_ddOutline->sel = g_t.outline; g_ddOutline->invalidate (true);
	g_pal->invalidate (true);
	g_curbox->invalidate (true);
	bool a = can_auto (g_item);
	g_auto->hidden = !a;
	g_auto->checked = a && item_colour (g_item) == UK_AUTO;
	g_auto->invalidate (true);
	g_pkC1->color = g_wp.c1; g_pkC1->invalidate (true);
	if (g_root) g_root->invalidate (true);
}

static void set_item_colour (unsigned c)
{
	switch (g_item)
	{
	case IT_ACTIVE:
		g_t.active = c; g_t.theme = -1;			// (a scheme of the same style)
		for (int i = 0; uk_themes[i].name; i++) if (uk_themes[i].frame == c && uk_themes[i].style == g_t.style) g_t.theme = i;
		break;
	case IT_INACTIVE: g_t.inactive = c; break;
	case IT_WINDOW:   g_t.window = c; break;
	case IT_BUTTON:   g_t.button = c; break;
	case IT_FIELD:    g_t.field = c; break;
	case IT_ACCENT:   g_t.accent = c; break;
	case IT_MENUBAR:  g_t.menubar = c; break;
	case IT_DOCK:     g_t.dock = c; break;
	default:          g_wp.c1 = c; refresh (true); return;
	}
	refresh ();
}

static void pick_item (int it);
// Theme: Classic / Modern -- the first scheme of the kind chosen (unless a scheme of it is the one in use).
static void on_theme_kind (Widget &w)
{
	int style = ((SegmentedControl &) w).selected == 1 ? UK_STYLE_MILK : UK_STYLE_CDE;
	if (style == g_t.style) return;
	for (int i = 0; uk_themes[i].name; i++) if (uk_themes[i].style == style) { uk_theme_take (g_t, i); break; }
	pick_item (IT_ACTIVE);
}
static void pick_item (int it)
{
	g_item = it;
	g_ddItem->sel = it; g_ddItem->invalidate (true);
	refresh ();
}

bool Preview::onMouse (int mx, int my, int bl, int, int, int)
{
	if (mx < 0) { pressed = false; return false; }
	if (bl && !pressed)
	{
		pressed = true;
		int x = mx - 1, y = my - 1;
		for (int i = g_nhot - 1; i >= 0; i--)
			if (x >= g_hot[i].x && y >= g_hot[i].y && x < g_hot[i].x + g_hot[i].w && y < g_hot[i].y + g_hot[i].h)
			{ pick_item (g_hot[i].item); break; }
	}
	else if (!bl) pressed = false;
	return true;
}

bool Swatch::onMouse (int mx, int, int bl, int, int, int)
{
	if (mx < 0) { pressed = false; return false; }
	if (bl && !pressed)
	{
		pressed = true;
		uk_theme_take (g_t, idx);				// (its frame; its own colours when they differ)
		pick_item (IT_ACTIVE);
	}
	else if (!bl) pressed = false;
	return true;
}

bool Palette::onMouse (int mx, int my, int bl, int, int, int)
{
	if (mx < 0) { pressed = false; return false; }
	if (bl && !pressed)
	{
		pressed = true;
		int i = (my / 23) * 12 + mx / 24;
		if (i >= 0 && i < 24 && mx < 12 * 24) set_item_colour (PAL[i]);
	}
	else if (!bl) pressed = false;
	return true;
}

static void show_desktop_controls (void)
{
	int m = g_wp.mode;
	bool pts = m == WP_VORONOI, two = m == WP_GRADIENT || m == WP_BUBBLES || m == WP_PATTERN, img = m == WP_IMAGE;
	g_lbPoints->hidden = g_nuPoints->hidden = !pts;
	g_lbC2->hidden = g_pkC2->hidden = g_lbDir->hidden = g_ddDir->hidden = !two;
	g_lbImage->hidden = g_tbImage->hidden = g_btBrowse->hidden = g_lbStyle->hidden = g_ddStyle->hidden = !img;
	g_lbPattern->hidden = g_ddPattern->hidden = m != WP_PATTERN;
	g_cbTint->hidden = !img;
	g_lbC1->setText (m == WP_VORONOI ? "Colour" : m == WP_IMAGE ? (g_wp.tint ? "Tint" : "Around") : two ? "Colour 1" : "Colour");
	if (g_root) g_root->invalidate (true);
}

// ---- load / apply ------------------------------------------------------------------------------------
static void load_current (void)
{
	uk_theme_defaults (g_t);
	void *f = kapi_open ("SD:/etc/theme.txt");
	if (f)
	{
		static char buf[4097];
		int n = kapi_read (f, buf, sizeof buf - 1);
		kapi_close (f);
		if (n > 0) { buf[n] = 0; uk_theme_parse (buf, g_t); }
	}
	wp_load (g_wp);
	g_ddOutline->sel = g_t.outline; g_ddOutline->invalidate (true);
	g_ddMode->sel = g_wp.mode; g_ddMode->invalidate (true);
	g_ddDir->sel = g_wp.vertical ? 0 : 1; g_ddDir->invalidate (true);
	g_ddStyle->sel = g_wp.tile ? 1 : 0; g_ddStyle->invalidate (true);
	g_nuPoints->value = g_wp.points; g_nuPoints->invalidate (true);
	g_pkC2->color = g_wp.c2; g_pkC2->invalidate (true);
	g_tbImage->setText (g_wp.image);
	g_cbTint->checked = g_wp.tint != 0; g_cbTint->invalidate (true);
	int pi = pattern_index ();
	g_ddPattern->sel = pi >= 0 ? pi : 0; g_ddPattern->invalidate (true);
	show_desktop_controls ();
	refresh (true);
}

static void apply (void)
{
	static char buf[1400];
	int p = uk_theme_write (g_t, buf, sizeof buf - 40);
	ax_strcat (buf, sizeof buf, &p, "wheelspeed=");			// (the Keyboard & Mouse applet's)
	p += ax_itoa (kapi_get_wheel_speed (), buf + p);
	buf[p++] = '\n'; buf[p] = 0;
	kapi_save_file ("SD:/etc/theme.txt", buf, (unsigned) p);
	int n = 0; while (g_tbImage->text[n] && n < (int) sizeof g_wp.image - 1) { g_wp.image[n] = g_tbImage->text[n]; n++; }
	g_wp.image[n] = 0;
	wp_save (g_wp);
	kapi_exec ("SD:apps/voronoy.app/main", "");				// the wallpaper
	dock_reload ();								// the shell's parts: the new colours
	static const char *const shell[] = { "menubar", "agenda" };
	for (int i = 0; i < 2; i++) { kapi_kill (shell[i]); kapi_launch (shell[i]); }
	if (!uk_applet_send (AP_THEME)) g_status->setText ("Applied: the apps opened from now on take it.");
}

static void on_item (Widget &w) { g_item = ((Dropdown &) w).sel; refresh (); }
static void on_auto (Widget &w)
{
	if (!can_auto (g_item)) return;
	set_item_colour (((Checkbox &) w).checked ? UK_AUTO : shown_colour (g_item));
}
static void on_custom (Widget &)
{
	unsigned c = shown_colour (g_item);
	if (uk_color_dialog (&c, ITEM_NAME[g_item])) set_item_colour (c);
}
static void on_outline (Widget &w) { g_t.outline = ((Dropdown &) w).sel; refresh (); }
static void on_mode (Widget &w) { g_wp.mode = ((Dropdown &) w).sel; show_desktop_controls (); refresh (true); }
static void on_dir (Widget &w) { g_wp.vertical = ((Dropdown &) w).sel == 0; refresh (true); }
static void on_style (Widget &w) { g_wp.tile = ((Dropdown &) w).sel == 1; refresh (true); }
static void on_points (Widget &w) { g_wp.points = ((NumericUpDown &) w).value; refresh (true); }
static void on_c1 (Widget &w) { g_wp.c1 = ((ColorPicker &) w).color; if (g_item == IT_DESKTOP) g_curbox->invalidate (true); refresh (true); }
static void on_c2 (Widget &w) { g_wp.c2 = ((ColorPicker &) w).color; refresh (true); }
static void on_pattern (Widget &w)
{
	int i = ((Dropdown &) w).sel;
	if (i < 0 || i >= g_npat) return;
	int n = 0; ax_strcat (g_wp.pattern, sizeof g_wp.pattern, &n, WALLPAPER_DIR "/"); ax_strcat (g_wp.pattern, sizeof g_wp.pattern, &n, g_patFile[i]);
	refresh (true);
}
static void on_tint (Widget &w) { g_wp.tint = ((Checkbox &) w).checked ? 1 : 0; show_desktop_controls (); refresh (true); }
static void on_image (Widget &)
{
	int n = 0; while (g_tbImage->text[n] && n < (int) sizeof g_wp.image - 1) { g_wp.image[n] = g_tbImage->text[n]; n++; }
	g_wp.image[n] = 0;
	refresh (true);
}
static void on_browse (Widget &)
{
	char p[200];
	if (!uk_file_open (p, sizeof p, g_wp.image[0] ? g_wp.image : WALLPAPER_DIR "/")) return;
	g_tbImage->setText (p);
	const char *d = WALLPAPER_DIR "/"; int k = 0;		// one of the patterns: tinted, as the Pattern mode
	while (d[k] && (p[k] | 32) == (d[k] | 32)) k++;
	if (!d[k]) { g_wp.tint = 1; g_cbTint->checked = true; g_cbTint->invalidate (true); show_desktop_controls (); }
	on_image (*g_tbImage);
}
static void on_apply (Widget &) { apply (); }
static void on_discard (Widget &) { load_current (); g_status->setText (""); }

// The Desktop box's controls are the window's children, placed over the box (a child is clipped to
// its parent: the drop-downs' lists open past the box, upward when they must).
static Root *g_root_; static GroupBox *g_gd;
static void desk_add (Widget *w) { w->left += g_gd->left; w->top += g_gd->top; g_root_->addChild (w); }

int main (void)
{
	ft_uikit_install ("DejaVu Sans", 13);		// (before the widgets; false: the bitmap font)
	Root root (W, H, "Theme");
	scan_patterns ();
	if (root.canvas.px == 0) return 1;
	g_root = &root;
	uk_theme_get (g_saved);
	int X = root.width > W ? (root.width - W) / 2 : 0;		// (an applet's pane may be wider)

	g_preview = new Preview (X + 10, 10);
	root.addChild (g_preview);

	int rx = X + 346;
	// the theme (the frames' kind), then its schemes: only the chosen theme's are shown
	root.addChild (new Label (rx, 7, 60, 20, "Theme", C_TEXT, root.bg));
	static const char *const THEMES[2] = { "Classic", "Modern" };
	g_segTheme = new SegmentedControl (rx + 64, 3, 200, 26, THEMES, 2, g_t.style == UK_STYLE_MILK ? 1 : 0, on_theme_kind);
	root.addChild (g_segTheme);
	root.addChild (new Label (rx, 46, 60, 20, "Scheme", C_TEXT, root.bg));
	for (int i = 0, n[2] = { 0, 0 }; i < NTHEMES; i++)
	{
		int k = uk_themes[i].style == UK_STYLE_MILK ? 1 : 0, w = k ? 96 : 56;
		g_sw[i] = new Swatch (rx + 62 + n[k]++ * w, 33, w, i); root.addChild (g_sw[i]);
	}
	root.addChild (new Label (rx, 86, 60, 24, "Item", C_TEXT, root.bg));
	root.addChild (new Label (rx, 124, 60, 24, "Colour", C_TEXT, root.bg));
	g_curbox = new Current (rx + 64, 122); root.addChild (g_curbox);
	root.addChild (new Button (rx + 132, 121, 100, 32, "Custom...", on_custom));
	g_auto = new Checkbox (rx + 64, 158, 250, 22, "Automatic (the window's)", false, on_auto, root.bg);
	root.addChild (g_auto);
	g_pal = new Palette (rx + 4, 184); root.addChild (g_pal);
	root.addChild (new Label (rx, 240, 70, 24, "Outline", C_TEXT, root.bg));

	GroupBox *gd = new GroupBox (X + 10, 272, W - 20, 150, "Desktop");
	root.addChild (gd);
	g_root_ = &root; g_gd = gd;			// (its controls: the window's, over the box -- desk_add)
	int y0 = gd->contentTop () + 4;
	desk_add (new Label (12, y0 + 4, 84, 20, "Wallpaper", C_TEXT, gd->bg));
	g_lbC1 = new Label (12, y0 + 44, 80, 20, "Colour", C_TEXT, gd->bg); desk_add (g_lbC1);
	g_pkC1 = new ColorPicker (96, y0 + 42, 50, 24, g_wp.c1, on_c1); g_pkC1->tip = "The palette; Item: Desktop, Custom... for any colour";
	g_lbC2 = new Label (170, y0 + 44, 80, 20, "Colour 2", C_TEXT, gd->bg); desk_add (g_lbC2);
	g_pkC2 = new ColorPicker (250, y0 + 42, 50, 24, g_wp.c2, on_c2);
	g_lbDir = new Label (330, y0 + 44, 80, 20, "Direction", C_TEXT, gd->bg); desk_add (g_lbDir);
	g_lbPoints = new Label (170, y0 + 44, 80, 20, "Cells", C_TEXT, gd->bg); desk_add (g_lbPoints);
	g_nuPoints = new NumericUpDown (250, y0 + 40, 70, 28, 4, 64, 28, 1, on_points); desk_add (g_nuPoints);
	g_lbImage = new Label (12, y0 + 84, 80, 20, "Picture", C_TEXT, gd->bg); desk_add (g_lbImage);
	g_tbImage = new Textbox (96, y0 + 80, 360, 28, "", on_image); desk_add (g_tbImage);
	g_btBrowse = new Button (464, y0 + 79, 100, 30, "Browse...", on_browse); desk_add (g_btBrowse);
	g_lbStyle = new Label (170, y0 + 44, 80, 20, "Style", C_TEXT, gd->bg); desk_add (g_lbStyle);
	g_lbPattern = new Label (12, y0 + 84, 80, 20, "Pattern", C_TEXT, gd->bg); desk_add (g_lbPattern);
	g_cbTint = new Checkbox (430, y0 + 42, 140, 24, "Tinted", false, on_tint, gd->bg); desk_add (g_cbTint);
	g_cbTint->tip = "The picture's grey multiplies the colour (a pattern of SD:/wallpapers: coloured)";

	g_status = new Label (X + 12, H - 38, 400, 24, "", C_DIS, root.bg);
	root.addChild (g_status);
	root.addChild (new Button (X + W - 196, H - 42, 90, 32, "Apply", on_apply));
	root.addChild (new Button (X + W - 100, H - 42, 90, 32, "Discard", on_discard));

	// the drop-downs and the pickers last: their lists open over what is below them
	g_ddOutline = new Dropdown (rx + 64, 236, 130, 28, OUTLINES, 3, 1, on_outline); root.addChild (g_ddOutline);
	g_ddItem = new Dropdown (rx + 64, 82, 268, 28, ITEM_NAME, IT_N, 0, on_item); root.addChild (g_ddItem);
	g_ddStyle = new Dropdown (250, y0 + 40, 160, 28, STYLES, 2, 0, on_style); desk_add (g_ddStyle);
	g_ddDir = new Dropdown (410, y0 + 40, 150, 28, DIRS, 2, 0, on_dir); desk_add (g_ddDir);
	g_ddPattern = new Dropdown (96, y0 + 80, 200, 28, g_patItems, g_npat > 0 ? g_npat : 1, 0, on_pattern); desk_add (g_ddPattern);
	desk_add (g_pkC2);
	desk_add (g_pkC1);
	g_ddMode = new Dropdown (96, y0, 200, 28, MODES, WP_NMODES, 0, on_mode); desk_add (g_ddMode);

	load_current ();
	root.run ();
	return 0;
}
