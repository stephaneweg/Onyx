//
// apps/setup -- the first-run wizard ("Onyx Setup"): started alone by SD:/etc/autostart at the first
// boot (the wallpaper under it; the menu bar, the dock and the agenda held back: "#setup: " lines),
// in a window that cannot be moved and stays centred (WIN_FLAG_FIXED, kapi v69). A page each:
//   1 Region & keyboard: the country (the Wi-Fi's country code; it proposes the layout and the time
//     zone), the layout (taken at once), the time zone (at once: kapi_set_timezone), clock sync;
//   2 Wi-Fi: the networks around, a password, connect (wpa_supplicant.conf, kapi_wlan_reconnect) --
//     or skip;
//   3 Display: the sizes, the monitor's own marked (kapi_screen_native); Try it changes it at once,
//     "Keep this resolution?" goes back by itself after 15 s; kept: SD:/cmdline.txt;
//   4 Appearance: the window in front's colour, the wallpaper and its tint (theme.txt, wallpaper.ini:
//     applied as they are picked, voronoy painting the desktop again);
//   5 Name & privacy: the computer's name (system.ini hostname=, at the next start), the remote
//     services (their autostart lines);
//   6 Ready: a summary; Start Onyx writes system.ini and the autostart (its own line removed, the
//     held-back ones given back and started now), starts or stops the services, and ends.
// FreeType's text (DejaVu Sans) through wtk's face; the theme's widgets. The user guide: docs/04, §4.
// "--demo <page>[b|c]" opens a page (0..6) in a given state, writing nothing (the screenshots':
// 2b the Wi-Fi connecting, 2c connected; 3b "Keep this resolution?").
//
#include "kapi.h"
#include "wtk/wtk.h"
#include "applib.h"
#include "bmp.hpp"
#include "wallpaper.h"
#include "img/imgload.hpp"
#include "ft/wtkface.h"
#include "system.h"

using namespace wtk;

#define W	800		// the window's client area
#define H	600
#define RAIL_W	212		// the steps, on the left
#define FOOT_H	60		// Back / Continue
#define PAD	34		// the page's margins
#define PX	PAD
#define PW	(W - RAIL_W - 2 * PAD)

static const unsigned OK_C = 0x002E9660, WARN_C = 0x00CE7828, ERR_C = 0x00C0392B;

static void scpy (char *d, int cap, const char *s) { int i = 0; for (; s && s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = 0; }
static unsigned dim_ink (unsigned bg) { return wk_mix (wk_ink_for (bg), bg, 100); }
static unsigned line_c (unsigned bg) { return wk_mix (bg, C_TEXT, 36); }
static bool g_demo;				// (the screenshots: nothing written)

// ---- the faces --------------------------------------------------------------------------------------------
static FtTextFace *g_h1, *g_hero, *g_lead, *g_small;
static FtTextFace *face (const char *fam, int px) { FtTextFace *f = new FtTextFace; if (!f->open (fam, px)) { delete f; return 0; } return f; }

// Text in a face (0: the installed one), left-aligned in a line of h px.
static void ftext (Canvas &cv, TextFace *f, int x, int y, int h, const char *s, unsigned c, int style = 0)
{ WkFaceScope sc (f); wk_text_l (cv, x, y, h, s, c, style); }
static int ftw (TextFace *f, const char *s, int style = 0) { WkFaceScope sc (f); return wk_tw (s, style); }
// Word-wrapped into w px from y (lines of lh px) -> the y below it (cv 0: only measured).
static int fwrap (Canvas *cv, TextFace *f, int x, int y, int w, int lh, const char *s, unsigned c, int style = 0)
{
	WkFaceScope sc (f);
	while (*s)
	{
		int n = 0, fit = 0;
		while (s[n] && s[n] != '\n')
		{
			int e = n; while (s[e] == ' ') e++; while (s[e] && s[e] != ' ' && s[e] != '\n') e++;
			if (fit && wk_tw_n (s, e, style) > w) break;
			fit = n = e;
		}
		char b[256]; int k = n < 255 ? n : 255; for (int i = 0; i < k; i++) b[i] = s[i]; b[k] = 0;
		if (cv) wk_text_l (*cv, x, y, lh, b, c, style);
		y += lh; s += n; while (*s == ' ' || *s == '\n') s++;
	}
	return y;
}

// ---- drawing ------------------------------------------------------------------------------------------------
// A filled polygon, anti-aliased (4 x 4 samples a pixel): the brand's gem, a warning's triangle, a disc.
static void aa_poly (Canvas &cv, int ox, int oy, const float *pts, int n, unsigned c)
{
	float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
	for (int i = 0; i < n; i++) { float x = pts[2 * i], y = pts[2 * i + 1]; if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
	for (int py = (int) y0; py <= (int) y1; py++)
		for (int px = (int) x0; px <= (int) x1; px++)
		{
			int in = 0;
			for (int sy = 0; sy < 4; sy++) for (int sx = 0; sx < 4; sx++)
			{
				float x = px + (sx + 0.5f) / 4, y = py + (sy + 0.5f) / 4; bool o = false;
				for (int i = 0, j = n - 1; i < n; j = i++)
				{
					float xi = pts[2 * i], yi = pts[2 * i + 1], xj = pts[2 * j], yj = pts[2 * j + 1];
					if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi) o = !o;
				}
				in += o;
			}
			if (in) wk_blend_px (cv, ox + px, oy + py, c, in * 255 / 16);
		}
}
static float sinf_ (float x) { while (x > 3.14159265f) x -= 6.2831853f; while (x < -3.14159265f) x += 6.2831853f; float x2 = x * x; return x * (1 - x2 / 6 * (1 - x2 / 20 * (1 - x2 / 42 * (1 - x2 / 72)))); }
static float cosf_ (float x) { return sinf_ (x + 1.5707963f); }
// The Onyx gem, s px across.
static void gem (Canvas &cv, int x, int y, int s, unsigned c, unsigned inner)
{
	float o[] = { s * .5f, 0, (float) s, s * .36f, s * .8f, (float) s, s * .2f, (float) s, 0, s * .36f };
	float i[] = { s * .5f, s * .19f, s * .8f, s * .42f, s * .5f, s * .86f, s * .2f, s * .42f };
	aa_poly (cv, x, y, o, 5, c); aa_poly (cv, x, y, i, 4, inner);
}
static void disc (Canvas &cv, int x, int y, int d, unsigned c) { wk_rbox (cv, x, y, d, d, d / 2, c, c); }
static void big_disc (Canvas &cv, int x, int y, int d, unsigned c)		// (any size: wk_rbox's corners stop at 16)
{
	float p[64]; float r = d / 2.0f;
	for (int k = 0; k < 32; k++) { float a = k * 6.2831853f / 32; p[2 * k] = r + r * cosf_ (a); p[2 * k + 1] = r + r * sinf_ (a); }
	aa_poly (cv, x, y, p, 32, c);
}
// A small rounded label (a country's code, a network's security).
static int chip (Canvas &cv, int x, int y, const char *s, unsigned bg, unsigned fg, int h = 18)
{
	int w = wk_tw (s, 2) + 12;
	wk_rbox (cv, x, y, w, h, h / 2, bg, bg);
	wk_text_c (cv, x, y, w, h, s, fg, 2);
	return w;
}

// An app's icon (apps/<name>.app/icon.bmp, 40 x 40, magenta = see-through), scaled down smoothly.
struct Pic { unsigned *px; int w, h; };
static Pic icon (const char *app)
{
	char p[96]; snprintf (p, sizeof p, "SD:/apps/%s.app/icon.bmp", app);
	Pic r; r.px = ui::bmp_decode (p, &r.w, &r.h); return r;
}
static void draw_pic (Canvas &cv, const Pic &p, int x, int y, int s)
{
	if (!p.px || p.w <= 0) return;
	for (int dy = 0; dy < s; dy++) for (int dx = 0; dx < s; dx++)
	{
		int sx0 = dx * p.w / s, sx1 = (dx + 1) * p.w / s, sy0 = dy * p.h / s, sy1 = (dy + 1) * p.h / s;
		if (sx1 <= sx0) sx1 = sx0 + 1;
		if (sy1 <= sy0) sy1 = sy0 + 1;
		unsigned r = 0, g = 0, b = 0, k = 0, t = 0;
		for (int yy = sy0; yy < sy1; yy++) for (int xx = sx0; xx < sx1; xx++)
		{
			unsigned c = p.px[yy * p.w + xx] & 0xFFFFFF; t++;
			if (c == WK_TRANSPARENT_KEY) continue;
			r += (c >> 16) & 255; g += (c >> 8) & 255; b += c & 255; k++;
		}
		if (k) wk_blend_px (cv, x + dx, y + dy, ((r / k) << 16) | ((g / k) << 8) | (b / k), k * 255 / t);
	}
}

// ---- the choices ------------------------------------------------------------------------------------------
// A country: its ISO 3166 code (the Wi-Fi's), its keyboard (SD:/etc/keymaps), its time zones (ZONES,
// the first proposed; -1 ends them) -- the time zone list shows the country's and UTC.
struct Country { const char *name, *code, *keyb; int zones[5]; };
static const Country COUNTRIES[] = {
	{ "Austria", "AT", "DE", { 6, -1 } }, { "Belgium", "BE", "BE", { 0, -1 } }, { "Canada", "CA", "US", { 17, 18, 19, 20, -1 } },
	{ "Denmark", "DK", "US", { 9, -1 } }, { "Finland", "FI", "US", { 14, -1 } }, { "France", "FR", "FR", { 1, -1 } },
	{ "Germany", "DE", "DE", { 4, -1 } }, { "Greece", "GR", "US", { 15, -1 } }, { "Ireland", "IE", "UK", { 12, -1 } },
	{ "Italy", "IT", "IT", { 7, -1 } }, { "Japan", "JP", "US", { 21, -1 } }, { "Luxembourg", "LU", "FR", { 3, -1 } },
	{ "Netherlands", "NL", "US", { 2, -1 } }, { "Poland", "PL", "US", { 10, -1 } }, { "Portugal", "PT", "US", { 13, -1 } },
	{ "Spain", "ES", "ES", { 8, -1 } }, { "Sweden", "SE", "US", { 9, -1 } }, { "Switzerland", "CH", "DE", { 5, -1 } },
	{ "United Kingdom", "GB", "UK", { 11, -1 } }, { "United States", "US", "US", { 16, 18, 19, 20, -1 } } };
#define UTC_ZONE	22
#define NCOUNTRIES	((int) (sizeof COUNTRIES / sizeof COUNTRIES[0]))
static const char *const KEYB_NAMES[] = { "Belgian (AZERTY)", "French (AZERTY)", "German (QWERTZ)", "English, UK", "English, US", "Spanish", "Italian", "Dvorak" };
static const char *const KEYB_CODES[] = { "BE", "FR", "DE", "UK", "US", "ES", "IT", "DV" };
#define NKEYB	8
static char g_zoneText[NZONES][48];
static const char *g_zoneOpts[6]; static int g_zoneMap[6], g_nzoneOpts;	// (the country's zones in the list)

struct Mode { int w, h; };
static const Mode MODES[] = { { 1024, 768 }, { 1280, 720 }, { 1280, 800 }, { 1280, 1024 }, { 1366, 768 }, { 1440, 900 },
	{ 1600, 900 }, { 1680, 1050 }, { 1920, 1080 }, { 1920, 1200 }, { 2560, 1440 } };
static Mode g_modes[16]; static int g_nmodes, g_best = -1, g_curMode = -1;
static int g_scrW = 1024, g_scrH = 768;
static const char *aspect (int w, int h)
{
	int a = w * 100 / h;
	return a == 133 ? "4:3" : a == 125 ? "5:4" : a == 160 ? "16:10" : (a >= 176 && a <= 178) ? "16:9" : "";
}
struct Scheme { const char *name; unsigned c; };
static const Scheme SCHEMES[] = { { "Peach", 0x00F0B07A }, { "Steel", 0x007A98C0 }, { "Sage", 0x0080AA76 }, { "Brick", 0x00C45450 }, { "Slate", 0x003A4458 } };
static const char *const WALLS[] = { 0, "hexagons", "low-poly", "waves", "dunes", "bokeh", "silk", "contours" };
#define NWALLS	8

// The wallpaper's tints: its colour (the gradient's first; the second, a darker shade of it). Two
// rows: these, then the same lighter (tint_of).
static const unsigned TINTS[] = { 0x004878B0, 0x002F5E8C, 0x003E8EA8, 0x002E8A7E, 0x003E8A56, 0x006E8A3A, 0x00B08A38, 0x00C06A34,
	0x00B04A40, 0x00A84A6E, 0x007A4E9A, 0x005A5AA8, 0x004A5668, 0x00707478, 0x00705A48, 0x00303438 };
#define NTINTS	32		// (16 a row)
static int g_country = 1, g_keyb = 0, g_zone = 0, g_mode = 0, g_scheme = 0, g_wall = 0, g_tint = 0;
static unsigned tint_of (int i) { return i < 16 ? TINTS[i] : wk_tone (TINTS[i - 16], 196); }
static unsigned tint1 () { return tint_of (g_tint); }
static unsigned tint2 () { return wk_tone (tint1 (), g_tint < 16 ? 52 : 104); }
static bool g_ntp = true, g_services[4] = { false, true, false, false };
static char g_ip[32];

// ---- the pieces -------------------------------------------------------------------------------------------

// A button filled with the accent: the page's main action.
class AccentButton : public Widget
{
public:
	char text[40]; Action cb;
	AccentButton (int l, int t, int w, int h, const char *s, Action cb_) : Widget (l, t, w, h), cb (cb_) { scpy (text, sizeof text, s); }
	void set (const char *s) { scpy (text, sizeof text, s); invalidate (true); }
	void onDraw () override
	{
		canvas.clear (bgColor ());
		unsigned a = disabled ? wk_mix (C_ACCENT, bgColor (), 130) : pressed ? wk_tone (C_ACCENT, 100) : hover ? wk_tone (C_ACCENT, 150) : C_ACCENT;
		wk_rbox (canvas, 0, 0, width, height, 6, wk_tone (a, 145), a);
		wk_rline (canvas, 0, 0, width, height, 6, wk_tone (a, 96), 150);
		wk_text_c (canvas, 0, 0, width, height, text, wk_ink_on (C_ACCENT), 2);
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		if (mx < 0) { if (hover || pressed) { hover = pressed = false; invalidate (true); } return false; }
		if (disabled) return true;
		if (!hover) { hover = true; invalidate (true); }
		if (bl && !pressed) { pressed = true; invalidate (true); }
		else if (!bl && pressed) { pressed = false; invalidate (true); if (mx < width && my < height && cb) cb (*this); }
		return true;
	}
};

// A text in the accent, underlined under the pointer: a secondary action ("Skip for now").
class Link : public Widget
{
public:
	char text[48]; Action cb;
	Link (int l, int t, int w, int h, const char *s, Action cb_) : Widget (l, t, w, h), cb (cb_) { scpy (text, sizeof text, s); }
	void onDraw () override
	{
		canvas.clear (bgColor ());
		unsigned c = pressed ? wk_tone (C_ACCENT, 90) : C_ACCENT;
		int x = width - wk_tw (text); if (x > 0 && !(tag & 1)) x = 0;
		wk_text_l (canvas, x, 0, height, text, c);
		if (hover) canvas.fillRect (x, height / 2 + wk_fh () / 2, wk_tw (text), 1, c);
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		if (mx < 0) { if (hover) { hover = pressed = false; invalidate (true); } return false; }
		if (!hover) { hover = true; invalidate (true); }
		if (bl) pressed = true;
		else if (pressed) { pressed = false; if (mx < width && my < height && cb) cb (*this); }
		return true;
	}
};

// A text on its parent's background: a face, a style, wrapped or not.
class Text : public Widget
{
public:
	char text[400]; TextFace *f; int style, lh; unsigned c; bool dim;
	Text (int l, int t, int w, int h, const char *s, int style_ = 0, TextFace *f_ = 0, bool dim_ = false)
		: Widget (l, t, w, h), f (f_), style (style_), lh (0), c (0), dim (dim_) { scpy (text, sizeof text, s); }
	void set (const char *s) { scpy (text, sizeof text, s); invalidate (true); }
	void onDraw () override
	{
		canvas.clear (bgColor ());
		unsigned ink = c ? c : dim ? dim_ink (bgColor ()) : wk_ink_for (bgColor ());
		if (lh) fwrap (&canvas, f, 0, 0, width, lh, text, ink, style);
		else ftext (canvas, f, 0, 0, height, text, ink, style);
	}
};

// A white rounded box with a thin outline: the lists' frame.
static void card (Canvas &cv, int x, int y, int w, int h)
{
	wk_rbox (cv, x, y, w, h, 7, 0x00FFFFFF, 0x00FFFFFF);
	wk_rline (cv, x, y, w, h, 7, line_c (C_FIELD));
}
static void sel_row (Canvas &cv, int x, int y, int w, int h)
{
	unsigned f = wk_mix (0x00FFFFFF, C_ACCENT, 38);
	wk_rbox (cv, x, y, w, h, 6, f, f);
	wk_rline (cv, x, y, w, h, 6, wk_mix (0x00FFFFFF, C_ACCENT, 130));
}

// ---- the steps (the rail) -----------------------------------------------------------------------------------
static const char *const STEPS[] = { "Welcome", "Region & keyboard", "Wi-Fi", "Display", "Appearance", "Name & privacy", "Ready" };
#define NSTEPS	7

class Rail : public Widget
{
public:
	int cur;
	Rail (int l, int t, int w, int h) : Widget (l, t, w, h), cur (0) {}
	void onDraw () override
	{
		canvas.clear (C_BG);
		unsigned ink = wk_ink_for (C_BG), dim = dim_ink (C_BG);
		gem (canvas, 22, 24, 30, C_ACCENT, wk_tone (C_ACCENT, 200));
		ftext (canvas, g_h1, 62, 20, 24, "Onyx", ink, 2);
		ftext (canvas, g_small, 63, 42, 16, "Setup", dim);
		int y0 = 96, rh = 44;
		for (int i = 0; i < NSTEPS; i++)
		{
			int y = y0 + i * rh, cx = 34, cy = y + rh / 2;
			if (i < NSTEPS - 1)
				canvas.fillRect (cx - 1, cy + 11, 2, rh - 22, i < cur ? wk_mix (C_BG, OK_C, 160) : line_c (C_BG));
			char n[2] = { (char) ('1' + i), 0 };
			if (i == cur)
			{
				unsigned hl = wk_tone (C_BG, 150);
				wk_rbox (canvas, 10, y + 4, width - 20, rh - 8, 8, hl, hl);
				disc (canvas, cx - 11, cy - 11, 22, C_ACCENT);
				wk_text_c (canvas, cx - 11, cy - 11, 22, 22, n, 0x00FFFFFF, 2);
				wk_text_l (canvas, 56, y, rh, STEPS[i], ink, 2);
			}
			else if (i < cur)
			{
				disc (canvas, cx - 11, cy - 11, 22, OK_C);
				wk_glyph (canvas, WKG_CHECK, cx, cy, 11, 0x00FFFFFF);
				wk_text_l (canvas, 56, y, rh, STEPS[i], ink);
			}
			else
			{
				unsigned in = wk_tone (C_BG, 140);
				wk_rbox (canvas, cx - 11, cy - 11, 22, 22, 11, in, in);
				wk_rline (canvas, cx - 11, cy - 11, 22, 22, 11, wk_mix (C_BG, C_TEXT, 90));
				wk_text_c (canvas, cx - 11, cy - 11, 22, 22, n, dim);
				wk_text_l (canvas, 56, y, rh, STEPS[i], dim);
			}
		}
		canvas.fillRect (width - 1, 0, 1, height, wk_mix (C_BG, C_TEXT, 60));
		ftext (canvas, g_small, 22, height - 40, 18, "Onyx 0.9  \xC2\xB7  Raspberry Pi 4", dim);
	}
};

// ---- a page ----------------------------------------------------------------------------------------------
// A page: the content's light background, its title (large) and what it is for (below, grey).
class Page : public Panel
{
public:
	const char *title; char sub[240]; int bodyY;
	Page (const char *t, const char *s) : Panel (RAIL_W, 0, W - RAIL_W, H - FOOT_H, C_FIELD), title (t), bodyY (0)
	{
		scpy (sub, sizeof sub, s);
		if (title) bodyY = fwrap (0, 0, PX, 66, PW, 20, sub, 0) + 22;	// (below the subtitle)
	}
	void setSub (const char *s) { scpy (sub, sizeof sub, s); invalidate (true); }
	void onDraw () override
	{
		canvas.clear (C_FIELD);
		if (!title) return;
		ftext (canvas, g_h1, PX, 30, 30, title, C_FIELD_TEXT, 2);
		fwrap (&canvas, 0, PX, 66, PW, 20, sub, dim_ink (C_FIELD));
	}
};

// ---- 0: welcome ------------------------------------------------------------------------------------------
class Welcome : public Widget
{
public:
	Pic ic[4];
	Welcome (int l, int t, int w, int h) : Widget (l, t, w, h)
	{ ic[0] = icon ("keyconf"); ic[1] = icon ("wifimenu"); ic[2] = icon ("displayconf"); ic[3] = icon ("theme"); }
	void onDraw () override
	{
		canvas.clear (C_FIELD);
		unsigned ink = C_FIELD_TEXT, dim = dim_ink (C_FIELD);
		gem (canvas, 0, 0, 58, C_ACCENT, wk_tone (C_ACCENT, 200));
		ftext (canvas, g_hero, 0, 72, 44, "Welcome to Onyx", ink, 2);
		int y = fwrap (&canvas, g_lead, 0, 126, width - 30, 23, "Let's get your Raspberry Pi ready. A few choices -- your keyboard, the network, the screen and the look of the desktop -- and you are done. It takes about two minutes; everything can be changed later in the Control Panel.", dim);
		y += 22;
		static const char *const L[] = { "Keyboard, country and time zone", "Wi-Fi network", "Screen resolution", "Colours and wallpaper" };
		for (int i = 0; i < 4; i++, y += 38) { draw_pic (canvas, ic[i], 0, y, 28); wk_text_l (canvas, 42, y, 28, L[i], ink); }
	}
};

// ---- 1: region & keyboard ------------------------------------------------------------------------------
class CountryList : public Widget
{
public:
	int sel, top; Action cb; static const int RH = 30;
	CountryList (int l, int t, int w, int h, Action cb_) : Widget (l, t, w, h), sel (g_country), top (0), cb (cb_) { canFocus = true; }
	int rows () const { return (height - 8) / RH; }
	void show (int i) { if (i < top) top = i; if (i >= top + rows ()) top = i - rows () + 1; }
	void onDraw () override
	{
		canvas.clear (C_FIELD);
		card (canvas, 0, 0, width, height);
		for (int r = 0; r < rows () && top + r < NCOUNTRIES; r++)
		{
			int i = top + r, y = 4 + r * RH; const Country &c = COUNTRIES[i]; bool s = i == sel;
			if (s) sel_row (canvas, 4, y, width - 18, RH);
			chip (canvas, 12, y + 6, c.code, s ? C_ACCENT : wk_mix (0x00FFFFFF, C_BG, 170), s ? 0x00FFFFFF : dim_ink (0x00FFFFFF));
			wk_text_l (canvas, 52, y, RH, c.name, C_FIELD_TEXT, s ? 2 : 0);
			if (s) wk_glyph (canvas, WKG_CHECK, width - 32, y + RH / 2, 11, C_ACCENT);
		}
		int n = NCOUNTRIES, v = rows ();
		if (n > v) { int th = (height - 12) * v / n, ty = (height - 12 - th) * top / (n - v); wk_scroll_bar (canvas, width - 11, 6, 6, height - 12, true, ty, th, 0x00FFFFFF); }
	}
	void pick (int i) { if (i < 0 || i >= NCOUNTRIES) return; sel = i; show (i); invalidate (true); if (cb) cb (*this); }
	bool onMouse (int mx, int my, int bl, int, int, int wheel) override
	{
		if (mx < 0) return false;
		if (wheel) { top -= wheel; int m = NCOUNTRIES - rows (); if (top > m) top = m; if (top < 0) top = 0; invalidate (true); }
		if (bl && !pressed) { setFocus (); pick (top + (my - 4) / RH); }
		pressed = bl;
		return true;
	}
	bool onKey (long k) override
	{
		if (k == KEY_UP) { pick (sel - 1); return true; }
		if (k == KEY_DOWN) { pick (sel + 1); return true; }
		if (k >= 'a' && k <= 'z') k -= 32;
		if (k >= 'A' && k <= 'Z')				// (the first country from that letter)
			for (int i = 0; i < NCOUNTRIES; i++) if (COUNTRIES[i].name[0] == k) { pick (i); return true; }
		return false;
	}
};

// The layout's keys, as a small keyboard.
class KeyboardView : public Widget
{
public:
	const char *rows[3];
	KeyboardView (int l, int t, int w, int h) : Widget (l, t, w, h) { set (0); }
	void set (int k)
	{
		static const char *const AZ[] = { "A Z E R T Y U I O P", "Q S D F G H J K L M", "W X C V B N , ; : =" };
		static const char *const QZ[] = { "Q W E R T Z U I O P", "A S D F G H J K L \xC3\x96", "Y X C V B N M , . -" };
		static const char *const QW[] = { "Q W E R T Y U I O P", "A S D F G H J K L ;", "Z X C V B N M , . /" };
		static const char *const DV[] = { "' , . P Y F G C R L", "A O E U I D H T N S", "; Q J K X B M W V Z" };
		const char *const *r = (k == 0 || k == 1) ? AZ : k == 2 ? QZ : k == 7 ? DV : QW;
		for (int i = 0; i < 3; i++) rows[i] = r[i];
		invalidate (true);
	}
	void onDraw () override
	{
		canvas.clear (C_FIELD);
		card (canvas, 0, 0, width, height);
		int g = 3, kw = (width - 20 - 16) / 10 - g, kh = 21;
		for (int r = 0; r < 3; r++)
		{
			const char *s = rows[r]; int k = 0;
			while (*s)
			{
				char key[5]; int n = 0; while (*s && *s != ' ' && n < 4) key[n++] = *s++; key[n] = 0; while (*s == ' ') s++;
				int x = 10 + r * 8 + k * (kw + g), y = 9 + r * (kh + g);
				wk_rbox (canvas, x, y, kw, kh, 4, 0x00FBFAF9, wk_mix (0x00FFFFFF, C_BG, 150));
				wk_rline (canvas, x, y, kw, kh, 4, wk_mix (0x00FFFFFF, C_TEXT, 60));
				WkFaceScope sc (g_small); wk_text_c (canvas, x, y, kw, kh - 1, key, C_FIELD_TEXT);
				k++;
			}
		}
	}
};

// ---- 2: Wi-Fi ---------------------------------------------------------------------------------------------
struct Net { char ssid[33]; int level, sec; bool con, known; };
static Net g_nets[8]; static int g_nnets;
static bool g_scanned;
static void scan_nets ()
{
	struct kapi_wlan_ap ap[24]; int n = kapi_wlan_scan (ap, 24);
	wpa_load ();
	g_nnets = 0;
	for (int i = 0; i < n && g_nnets < 7; i++)		// (strongest first; a name once; hidden ones: "Other network...")
	{
		if (!ap[i].ssid[0] || ap[i].ssid[0] == ' ') continue;
		bool dup = false;
		for (int k = 0; k < g_nnets; k++) if (!strcmp (g_nets[k].ssid, ap[i].ssid)) { dup = true; if (ap[i].connected) g_nets[k].con = true; }
		if (dup) continue;
		Net &t = g_nets[g_nnets++]; scpy (t.ssid, sizeof t.ssid, ap[i].ssid);
		t.level = ap[i].level; t.sec = ap[i].security; t.con = ap[i].connected; t.known = wpa_known (t.ssid) != 0;
	}
	for (int i = 1; i < g_nnets; i++)			// (the one we are on, first)
		if (g_nets[i].con) { Net t = g_nets[i]; for (int k = i; k > 0; k--) g_nets[k] = g_nets[k - 1]; g_nets[0] = t; break; }
	g_scanned = true;
}
static int bars_of (int dbm) { return dbm > -50 ? 4 : dbm > -60 ? 3 : dbm > -70 ? 2 : 1; }
static void bars (Canvas &cv, int x, int y, int n, unsigned on, unsigned off)
{
	for (int i = 0; i < 4; i++) { int bh = 4 + i * 3; unsigned c = i < n ? on : off; wk_rbox (cv, x + i * 5, y + 13 - bh, 4, bh, 1, c, c); }
}

enum { NET_LIST, NET_PASSWORD, NET_CONNECTING, NET_CONNECTED };
class NetList : public Widget
{
public:
	int sel, state; char err[96]; static const int RH = 42, OPEN_H = 92, OPEN_H0 = 50;
	Textbox *pass; AccentButton *go; Checkbox *autoc;
	NetList (int l, int t, int w, int h, Action onConnect) : Widget (l, t, w, h), sel (-1), state (NET_LIST)
	{
		err[0] = 0;
		pass = new Textbox (54, 0, width - 250, 30, "", onConnect); pass->password = true; pass->maxLen = 63;
		go = new AccentButton (width - 178, 0, 160, 32, "Connect", onConnect);
		autoc = new Checkbox (54, 0, 220, 22, "Connect automatically", true, 0, sel_bg ());
		addChild (pass); addChild (go); addChild (autoc);
		place ();
	}
	static unsigned sel_bg () { return wk_mix (0x00FFFFFF, C_ACCENT, 38); }
	unsigned bgColor () override { return 0x00FFFFFF; }
	bool open () const { return sel >= 0 && sel < g_nnets && state == NET_PASSWORD; }
	bool needPass () const { return open () && g_nets[sel].sec != WLAN_SEC_OPEN; }
	int openH () const { return open () ? (needPass () ? OPEN_H : OPEN_H0) : 0; }
	int rowY (int i) const { return 4 + i * RH + (sel >= 0 && i > sel ? openH () : 0); }
	int total () const { return 8 + (g_nnets ? g_nnets : 1) * RH + openH (); }
	void place ()
	{
		bool o = open (), p = needPass ();
		pass->hidden = autoc->hidden = !p; go->hidden = !o;
		if (o)
		{
			int y = rowY (sel) + RH;
			pass->left = 54; pass->top = y + 16; go->left = width - 178; go->top = p ? y + 15 : y + 2;
			autoc->left = 54; autoc->top = y + 58; autoc->hasFocus = false;
			if (p) pass->setFocus ();
		}
		invalidate (true);
	}
	void onDraw () override
	{
		canvas.clear (C_FIELD);
		card (canvas, 0, 0, width, total ());
		if (!g_nnets)
		{
			wk_text_c (canvas, 0, 4, width, RH, g_scanned ? "No network found. Is the Wi-Fi router on?" : "Looking for networks...", dim_ink (0x00FFFFFF));
			return;
		}
		for (int i = 0; i < g_nnets; i++)
		{
			const Net &n = g_nets[i]; int y = rowY (i); bool s = i == sel;
			if (s && open ()) sel_row (canvas, 4, y, width - 8, RH + openH () - 4);
			else if (i > 0 && !(sel == i - 1 && open ())) canvas.fillRect (14, y, width - 28, 1, line_c (0x00FFFFFF));
			bars (canvas, 18, y + 14, bars_of (n.level), C_FIELD_TEXT, wk_mix (0x00FFFFFF, C_TEXT, 60));
			wk_text_l (canvas, 54, y, RH, n.ssid, C_FIELD_TEXT, s ? 2 : 0);
			int x = width - 18;
			if (n.sec != WLAN_SEC_OPEN) { wk_glyph (canvas, WKG_LOCK, x - 7, y + RH / 2, 13, dim_ink (0x00FFFFFF)); x -= 22; }
			const char *sec = n.sec == WLAN_SEC_OPEN ? "Open" : n.sec == WLAN_SEC_WEP ? "WEP" : n.sec == WLAN_SEC_WPA ? "WPA" : "WPA2";
			x -= wk_tw (sec, 2) + 12;
			chip (canvas, x, y + 12, sec, wk_mix (0x00FFFFFF, C_BG, 150), dim_ink (0x00FFFFFF));
			int cx = 54 + wk_tw (n.ssid, 2) + 12;
			if (n.con && (state == NET_CONNECTED || !s))
			{
				wk_glyph (canvas, WKG_CHECK, cx + 5, y + RH / 2, 11, OK_C);
				wk_text_l (canvas, cx + 16, y, RH, "Connected", OK_C, 2);
			}
			else if (n.known && !s) { WkFaceScope sc (g_small); wk_text_l (canvas, cx, y, RH, "Saved", dim_ink (0x00FFFFFF)); }
			if (s && needPass ())
			{
				ftext (canvas, g_small, 54, y + RH - 4, 18, n.known ? "Password (leave empty: the saved one)" : "Password", dim_ink (0x00FFFFFF));
				if (err[0]) ftext (canvas, g_small, width - 18 - ftw (g_small, err), y + RH + 58, 22, err, ERR_C);
				else ftext (canvas, g_small, width - 18 - ftw (g_small, "Shared with the Wi-Fi menu"), y + RH + 58, 22, "Shared with the Wi-Fi menu", dim_ink (0x00FFFFFF));
			}
			else if (s && open () && err[0]) ftext (canvas, g_small, 54, y + RH + 8, 22, err, ERR_C);
		}
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		if (mx < 0 || !bl || pressed) { pressed = bl && mx >= 0; return false; }
		pressed = true;
		for (int i = 0; i < g_nnets; i++)
			if (my >= rowY (i) && my < rowY (i) + RH)
			{
				if (g_nets[i].con && state == NET_CONNECTED) return true;
				sel = i; state = NET_PASSWORD; err[0] = 0; pass->setText ("");
				place ();
				return true;
			}
		return false;
	}
};

// Connecting: a spinner, what is going on, Cancel.
class Connecting : public Widget
{
public:
	int phase; char what[80];
	Connecting (int l, int t, int w, int h, Action onCancel) : Widget (l, t, w, h), phase (0)
	{
		what[0] = 0;
		addChild (new Button (w / 2 - 55, 164, 110, 36, "Cancel", onCancel));
	}
	unsigned bgColor () override { return 0x00FFFFFF; }
	void onDraw () override
	{
		canvas.clear (C_FIELD);
		card (canvas, 0, 0, width, height);
		int cx = width / 2, cy = 64;
		for (int k = 0; k < 12; k++)		// 12 dots round a circle, the newest darkest
		{
			static const int CS[12] = { 0, 50, 87, 100, 87, 50, 0, -50, -87, -100, -87, -50 };
			int dx = CS[(k + 3) % 12] * 18 / 100, dy = CS[k] * 18 / 100;
			int a = 40 + 215 * ((k - phase + 24) % 12) / 11;
			disc (canvas, cx + dx - 3, cy + dy - 3, 6, wk_mix (0x00FFFFFF, C_ACCENT, a));
		}
		ftext (canvas, g_lead, (width - ftw (g_lead, what, 2)) / 2, 98, 26, what, C_FIELD_TEXT, 2);
		const char *s = "Authenticating  \xC2\xB7  getting an address";
		wk_text_l (canvas, (width - wk_tw (s)) / 2, 126, 22, s, dim_ink (0x00FFFFFF));
	}
};

// A network whose name is not broadcast: its name and password typed.
class HiddenNet : public Widget
{
public:
	Textbox *ssid, *pass;
	HiddenNet (int l, int t, int w, int h, Action onConnect, Action onCancel) : Widget (l, t, w, h)
	{
		addChild (new Text (18, 16, 200, 20, "Network name", 2));
		ssid = new Textbox (18, 40, w - 36, 30, ""); ssid->maxLen = 32; addChild (ssid);
		addChild (new Text (18, 82, 300, 20, "Password (empty: an open network)", 2));
		pass = new Textbox (18, 106, w - 36, 30, "", onConnect); pass->password = true; pass->maxLen = 63; addChild (pass);
		addChild (new Button (w - 18 - 160 - 10 - 104, 150, 104, 38, "Cancel", onCancel));
		addChild (new AccentButton (w - 18 - 160, 153, 160, 32, "Connect", onConnect));
	}
	unsigned bgColor () override { return 0x00FFFFFF; }
	void onDraw () override { canvas.clear (C_FIELD); card (canvas, 0, 0, width, height); }
};

// A coloured note: an icon, a bold line, a small one.
class Banner : public Widget
{
public:
	unsigned c; int kind; char t1[100], t2[140];
	Banner (int l, int t, int w, int h, unsigned c_, int kind_, const char *a, const char *b) : Widget (l, t, w, h), c (c_), kind (kind_)
	{ scpy (t1, sizeof t1, a); scpy (t2, sizeof t2, b); }
	void set (const char *a, const char *b) { scpy (t1, sizeof t1, a); scpy (t2, sizeof t2, b); invalidate (true); }
	void onDraw () override
	{
		canvas.clear (C_FIELD);
		unsigned bg = wk_mix (0x00FFFFFF, c, 30);
		wk_rbox (canvas, 0, 0, width, height, 8, bg, bg);
		wk_rline (canvas, 0, 0, width, height, 8, wk_mix (0x00FFFFFF, c, 120));
		unsigned ink = wk_tone (c, 70);
		if (kind == 0) { disc (canvas, 14, height / 2 - 13, 26, c); wk_glyph (canvas, WKG_CHECK, 27, height / 2, 12, 0x00FFFFFF); }
		else
		{
			float tri[] = { 11, 0, 22, 19, 0, 19 };
			aa_poly (canvas, 14, height / 2 - 10, tri, 3, c);
			canvas.fillRect (24, height / 2 - 4, 2, 7, 0x00FFFFFF); canvas.fillRect (24, height / 2 + 5, 2, 2, 0x00FFFFFF);
		}
		if (t2[0]) { wk_text_l (canvas, 52, 6, 22, t1, ink, 2); ftext (canvas, g_small, 52, 27, 18, t2, ink); }
		else ftext (canvas, g_small, 48, 0, height, t1, ink);
	}
};

// ---- 3: display --------------------------------------------------------------------------------------------
static void mode_text (int i, char *b, int cap) { snprintf (b, cap, "%d \xC3\x97 %d", g_modes[i].w, g_modes[i].h); }
class ModeList : public Widget
{
public:
	Action cb; static const int RH = 32;
	ModeList (int l, int t, int w, int h, Action cb_) : Widget (l, t, w, h), cb (cb_) {}
	void onDraw () override
	{
		canvas.clear (C_FIELD);
		card (canvas, 0, 0, width, height);
		for (int i = 0; i < g_nmodes; i++)
		{
			int y = 4 + i * RH; bool s = i == g_mode;
			if (s) sel_row (canvas, 4, y, width - 8, RH);
			wk_radio_mark (canvas, 14, y + (RH - 16) / 2, 16, s, WK_NORMAL);
			char b[32]; mode_text (i, b, sizeof b);
			wk_text_l (canvas, 40, y, RH, b, C_FIELD_TEXT, s ? 2 : 0);
			if (i == g_best) chip (canvas, width - 14 - wk_tw ("Best", 2) - 12, y + 7, "Best", OK_C, 0x00FFFFFF);
			else if (i == g_curMode) { WkFaceScope sc (g_small); wk_text_l (canvas, width - 14 - wk_tw ("now"), y, RH, "now", C_ACCENT); }
			else { WkFaceScope sc (g_small); const char *a = aspect (g_modes[i].w, g_modes[i].h); wk_text_l (canvas, width - 14 - wk_tw (a), y, RH, a, dim_ink (0x00FFFFFF)); }
		}
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		if (mx < 0) return false;
		if (bl && !pressed) { int i = (my - 4) / RH; if (i >= 0 && i < g_nmodes) { g_mode = i; invalidate (true); if (cb) cb (*this); } }
		pressed = bl; return true;
	}
};

// The wallpaper at a size (the desktop's own painter: wallpaper.h).
static unsigned *wall_pic (int which, int w, int h, unsigned c1, unsigned c2)
{
	unsigned *p = new unsigned[w * h];
	Wallpaper wp; wp_defaults (wp); wp.c1 = c1; wp.c2 = c2;
	if (which == 0) { wp.mode = WP_VORONOI; wp_paint (p, w, h, w, wp, 12345, 1, 0); return p; }
	wp.mode = WP_GRADIENT; wp_paint (p, w, h, w, wp, 0, 1, 0);
	char path[80]; snprintf (path, sizeof path, WALLPAPER_DIR "/%s.png", WALLS[which]);
	ImgFrames im;
	if (img_load (path, &im) && im.w > 0)
	{
		unsigned char *g = new unsigned char[w * h];
		wp_grey_cover (im.px[0], im.w, im.h, g, w, h); wp_multiply (p, w, h, w, g);
		delete [] g; img_free (&im);
	}
	return p;
}
static void blit_round (Canvas &cv, const unsigned *p, int x, int y, int w, int h, int r)
{
	for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) cv.pixel (x + i, y + j, p[j * w + i]);
	wk_rline (cv, x, y, w, h, r, 0, 60);
}

// A monitor showing the desktop at the chosen size: the menu bar, a window and the dock, to scale.
class Monitor : public Widget
{
public:
	unsigned *wall; int ww, wh;
	Monitor (int l, int t, int w, int h) : Widget (l, t, w, h), wall (0), ww (0), wh (0) {}
	void onDraw () override
	{
		canvas.clear (C_FIELD);
		const Mode &m = g_modes[g_mode];
		int sw = width - 16, sh = sw * m.h / m.w; if (sh > height - 60) { sh = height - 60; sw = sh * m.w / m.h; }
		int x = (width - sw - 16) / 2, y = 0;
		wk_rbox (canvas, x, y, sw + 16, sh + 16, 8, 0x003A3E46, 0x00202228);
		if (!wall || ww != sw || wh != sh) { delete [] wall; wall = wall_pic (0, sw, sh, 0x004878B0, 0x001C2C48); ww = sw; wh = sh; }
		for (int j = 0; j < sh; j++) for (int i = 0; i < sw; i++) canvas.pixel (x + 8 + i, y + 8 + j, wall[j * sw + i]);
		int mb = 30 * sw / m.w; if (mb < 3) mb = 3;
		canvas.fillRect (x + 8, y + 8, sw, mb, 0x00ECE6E1);
		int wx = x + 8 + sw * 28 / 100, wy = y + 8 + sh * 22 / 100, wwid = 620 * sw / m.w, whei = 420 * sw / m.w, th = 28 * sw / m.w;
		wk_rbox (canvas, wx, wy, wwid, whei, 3, wk_tone (C_FRAME_ACTIVE, 160), C_FRAME_ACTIVE);
		canvas.fillRect (wx + 2, wy + th, wwid - 4, whei - th - 2, C_BG);
		int dw = 754 * sw / m.w, dh = 80 * sw / m.w; if (dh < 4) dh = 4;
		wk_rbox (canvas, x + 8 + (sw - dw) / 2, y + 8 + sh - dh - 3, dw, dh, 3, wk_tone (C_DOCK, 170), C_DOCK);
		canvas.fillRect (x + sw / 2 - 4, y + sh + 16, 24, 22, 0x0046494F);
		wk_rbox (canvas, x + sw / 2 - 50 + 8, y + sh + 36, 100, 9, 4, 0x0050545C, 0x0032343A);
	}
};

// "Keep this resolution?" -- over the page, a counter running.
class KeepSheet : public Widget
{
public:
	int secs; char msg[160]; Pic ic; Button *revert; AccentButton *keep;
	KeepSheet (int l, int t, int w, int h, Action onKeep, Action onRevert) : Widget (l, t, w, h), secs (15)
	{
		transparent = true; ic = icon ("displayconf"); msg[0] = 0;
		keep = new AccentButton (w - 24 - 110, h - 54, 110, 34, "Keep", onKeep);
		revert = new Button (w - 24 - 110 - 10 - 120, h - 56, 120, 38, "Revert", onRevert);
		addChild (revert); addChild (keep);
	}
	unsigned bgColor () override { return C_FIELD; }
	void onDraw () override
	{
		canvas.clear (WK_TRANSPARENT_KEY);
		wk_popup (canvas, 0, 0, width, height, 12, C_FIELD);
		draw_pic (canvas, ic, 24, 24, 40);
		ftext (canvas, g_lead, 80, 20, 28, "Keep this resolution?", C_FIELD_TEXT, 2);
		fwrap (&canvas, 0, 80, 52, width - 104, 20, msg, dim_ink (C_FIELD));
		wk_progress_bar (canvas, 80, 100, width - 104, 6, (width - 104) * secs / 15);
	}
};

// ---- 4: appearance ---------------------------------------------------------------------------------------------
// The desktop as it will look: the wallpaper, the menu bar, a window behind, the one in front in the
// chosen colour, the dock.
class DesktopPreview : public Widget
{
public:
	unsigned *wall; int shown, shownTint;
	DesktopPreview (int l, int t, int w, int h) : Widget (l, t, w, h), wall (0), shown (-1), shownTint (-1) {}
	void win (int x, int y, int w, int h, unsigned frame, const char *t, bool front)
	{
		wk_rbox (canvas, x, y, w, h, 6, wk_tone (frame, 164), wk_tone (frame, 118));
		wk_rline (canvas, x, y, w, h, 6, wk_tone (frame, 70), 170);
		canvas.fillRect (x + 3, y + 20, w - 6, h - 23, C_BG);
		for (int k = 0; k < 3; k++) wk_rbox (canvas, x + w - 20 - k * 17, y + 4, 14, 12, 3, wk_tone (frame, 175), wk_tone (frame, 125));
		WkFaceScope sc (g_small); wk_text_c (canvas, x, y, w, 20, t, wk_ink_on (frame), 2);
		if (front) wk_text_l (canvas, x + 12, y + 28, 18, "Hello, Onyx.", C_TEXT);
	}
	void onDraw () override
	{
		canvas.clear (C_FIELD);
		if (!wall || shown != g_wall || shownTint != g_tint) { delete [] wall; wall = wall_pic (g_wall, width, height, tint1 (), tint2 ()); shown = g_wall; shownTint = g_tint; }
		blit_round (canvas, wall, 0, 0, width, height, 8);
		for (int y = 1; y < 19; y++) canvas.fillRect (1, y, width - 2, 1, wk_mix (0x00F6F2EE, 0x00DCD4CE, y * 256 / 19));
		{ WkFaceScope sc (g_small); wk_text_l (canvas, 10, 1, 18, "Onyx   File   Edit   View", C_TEXT); wk_text_l (canvas, width - 44, 1, 18, "12:34", C_TEXT, 2); }
		win (40, 28, 200, 84, WK_GREY, "Calendar", false);
		win (180, 40, 250, 86, SCHEMES[g_scheme].c, "Text Editor", true);
		int dw = 240, dx = (width - dw) / 2, dy = height - 30;
		wk_rbox (canvas, dx, dy, dw, 24, 7, wk_tone (C_DOCK, 175), C_DOCK); wk_rline (canvas, dx, dy, dw, 24, 7, wk_tone (C_DOCK, 60), 140);
		static Pic di[6]; static const char *const DI[] = { "tinypad", "netsurf", "paint", "tetris", "terminal", "fileviewer" };
		for (int k = 0; k < 6; k++) { if (!di[k].px) di[k] = icon (DI[k]); draw_pic (canvas, di[k], dx + 12 + k * 38, dy + 2, 20); }
	}
};

class SchemePicker : public Widget
{
public:
	Action cb;
	SchemePicker (int l, int t, int w, int h, Action cb_) : Widget (l, t, w, h), cb (cb_) {}
	void onDraw () override
	{
		canvas.clear (C_FIELD);
		for (int i = 0; i < 5; i++)
		{
			int x = 4 + i * 92, y = 4; bool s = i == g_scheme; unsigned c = SCHEMES[i].c;
			if (s) { wk_rline (canvas, x - 3, y - 3, 86, 42, 10, C_ACCENT); wk_rline (canvas, x - 4, y - 4, 88, 44, 11, C_ACCENT); }
			wk_rbox (canvas, x, y, 80, 36, 8, wk_tone (c, 164), wk_tone (c, 118));
			wk_rline (canvas, x, y, 80, 36, 8, wk_tone (c, 70), 170);
			if (s) wk_glyph (canvas, WKG_CHECK, x + 40, y + 18, 12, wk_ink_on (c));
			wk_text_c (canvas, x, y + 42, 80, 20, SCHEMES[i].name, C_FIELD_TEXT, s ? 2 : 0);
		}
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		if (mx < 0) return false;
		if (bl && !pressed) { int i = (mx - 4) / 92; if (i >= 0 && i < 5 && my < 66) { g_scheme = i; invalidate (true); if (cb) cb (*this); } }
		pressed = bl; return true;
	}
};

class WallPicker : public Widget
{
public:
	Action cb; unsigned *thumb[NWALLS]; int thumbTint; static const int TW = 58, TH = 40, G = 7, SW = 22, SG = 8, SY = TH + 16;
	WallPicker (int l, int t, int w, int h, Action cb_) : Widget (l, t, w, h), cb (cb_), thumbTint (-1) { for (int i = 0; i < NWALLS; i++) thumb[i] = 0; }
	void onDraw () override
	{
		canvas.clear (C_FIELD);
		if (thumbTint != g_tint) { for (int i = 0; i < NWALLS; i++) { delete [] thumb[i]; thumb[i] = 0; } thumbTint = g_tint; }
		for (int i = 0; i < NWALLS; i++)
		{
			int x = 4 + i * (TW + G), y = 4;
			if (!thumb[i]) thumb[i] = wall_pic (i, TW, TH, tint1 (), tint2 ());
			blit_round (canvas, thumb[i], x, y, TW, TH, 6);
			if (i == g_wall) { wk_rline (canvas, x - 3, y - 3, TW + 6, TH + 6, 8, C_ACCENT); wk_rline (canvas, x - 4, y - 4, TW + 8, TH + 8, 9, C_ACCENT); }
		}
		static const char *const NM[] = { "Generated (Voronoi) -- a new one at every start", "Hexagons", "Low poly", "Waves", "Dunes", "Bokeh", "Silk", "Contours" };
		for (int i = 0; i < NTINTS; i++)		// the tints: two rows of swatches (the gradient they give)
		{
			int x = 4 + (i % 16) * (SW + SG), y = SY + (i / 16) * (SW + SG); unsigned c = tint_of (i);
			if (i == g_tint) { wk_rline (canvas, x - 3, y - 3, SW + 6, SW + 6, 9, C_ACCENT); wk_rline (canvas, x - 4, y - 4, SW + 8, SW + 8, 10, C_ACCENT); }
			wk_rbox (canvas, x, y, SW, SW, 6, wk_tone (c, 150), wk_tone (c, 96));
			wk_rline (canvas, x, y, SW, SW, 6, wk_tone (c, 60), 150);
			if (i == g_tint) wk_glyph (canvas, WKG_CHECK, x + SW / 2, y + SW / 2, 10, wk_ink_on (c));
		}
		WkFaceScope sc (g_small); wk_text_l (canvas, 2, SY + 2 * SW + SG + 6, 18, NM[g_wall], dim_ink (C_FIELD));
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		if (mx < 0) return false;
		if (bl && !pressed)
		{
			if (my < TH + 8) { int i = (mx - 4) / (TW + G); if (i >= 0 && i < NWALLS) { g_wall = i; invalidate (true); if (cb) cb (*this); } }
			else if (my >= SY - 4 && my < SY + 2 * SW + SG + 4)
			{
				int c = (mx - 4 + SG / 2) / (SW + SG), r = (my - SY + SG / 2) / (SW + SG), i = r * 16 + c;
				if (c >= 0 && c < 16 && r >= 0 && r < 2) { g_tint = i; invalidate (true); if (cb) cb (*this); }
			}
		}
		pressed = bl; return true;
	}
};

// ---- 5: name & privacy -------------------------------------------------------------------------------------------
class Services : public Widget
{
public:
	Pic ic[4]; static const int RH = 56;
	Services (int l, int t, int w, int h) : Widget (l, t, w, h)
	{ ic[0] = icon ("terminal"); ic[1] = icon ("displayconf"); ic[2] = icon ("taskman"); ic[3] = icon ("fileviewer"); }
	void onDraw () override
	{
		static const char *const T[] = { "Remote shell (telnet)", "Remote desktop (VNC)", "Remote windows (Onyx Remote)", "File sharing (FTP)" };
		static const char *const S[] = { "A command line from another computer, port 23", "See and drive this screen from any VNC viewer, port 5900",
						  "The Onyx windows on a Windows PC, port 3390", "The SD card from another computer, port 21 (user onyx, password onyx)" };
		canvas.clear (C_FIELD);
		card (canvas, 0, 0, width, height);
		for (int i = 0; i < 4; i++)
		{
			int y = i * RH;
			if (i) canvas.fillRect (14, y, width - 28, 1, line_c (0x00FFFFFF));
			draw_pic (canvas, ic[i], 14, y + 13, 30);
			wk_text_l (canvas, 58, y + 8, 20, T[i], C_FIELD_TEXT, 2);
			ftext (canvas, g_small, 58, y + 29, 18, S[i], dim_ink (0x00FFFFFF));
			wk_switch_mark (canvas, width - 58, y + 17, 40, 22, g_services[i], WK_NORMAL);
		}
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		if (mx < 0) return false;
		if (bl && !pressed) { int i = my / RH; if (i >= 0 && i < 4) { g_services[i] = !g_services[i]; invalidate (true); } }
		pressed = bl; return true;
	}
};

// ---- 6: ready ---------------------------------------------------------------------------------------------------
static void go (int p);
class Summary : public Widget
{
public:
	Pic ic[5]; static const int RH = 52; char val[5][120];
	Summary (int l, int t, int w, int h) : Widget (l, t, w, h)
	{ ic[0] = icon ("keyconf"); ic[1] = icon ("wifimenu"); ic[2] = icon ("displayconf"); ic[3] = icon ("theme"); ic[4] = icon ("control"); for (auto &v : val) v[0] = 0; }
	void onDraw () override
	{
		static const char *const T[] = { "Region & keyboard", "Wi-Fi", "Display", "Appearance", "Name & privacy" };
		canvas.clear (C_FIELD);
		card (canvas, 0, 0, width, height);
		for (int i = 0; i < 5; i++)
		{
			int y = 4 + i * RH;
			if (i) canvas.fillRect (14, y, width - 28, 1, line_c (0x00FFFFFF));
			draw_pic (canvas, ic[i], 14, y + 11, 30);
			wk_text_l (canvas, 58, y + 6, 20, T[i], C_FIELD_TEXT, 2);
			char fit[120]; { WkFaceScope sc (g_small); wk_text_fit (val[i], width - 58 - 90, fit, sizeof fit); }
			ftext (canvas, g_small, 58, y + 27, 18, fit, dim_ink (0x00FFFFFF));
			bool hot = hover && hotRow == i;
			wk_text_l (canvas, width - 18 - wk_tw ("Change"), y, RH, "Change", hot ? wk_tone (C_ACCENT, 100) : C_ACCENT);
			if (hot) canvas.fillRect (width - 18 - wk_tw ("Change"), y + RH / 2 + wk_fh () / 2, wk_tw ("Change"), 1, C_ACCENT);
		}
	}
	int hotRow = -1;
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		if (mx < 0) { if (hover) { hover = false; invalidate (true); } return false; }
		int r = (my - 4) / RH, hr = mx > width - 30 - wk_tw ("Change") && r >= 0 && r < 5 ? r : -1;
		if (!hover || hr != hotRow) { hover = true; hotRow = hr; invalidate (true); }
		if (bl && !pressed && hr >= 0) go (hr + 1);
		pressed = bl; return true;
	}
};

// ---- the window ----------------------------------------------------------------------------------------------
static Root *g_root;
static Rail *g_rail;
static Page *g_pages[NSTEPS];
static int g_page;
static Text *g_stepOf;
static Button *g_back;
static Link *g_skip;
static AccentButton *g_next;
static NetList *g_netlist;
static Connecting *g_conn;
static HiddenNet *g_hiddenNet;
static Banner *g_online;
static Link *g_hidden;
static Button *g_rescan;
static KeepSheet *g_keep;
static Dropdown *g_keybDD, *g_zoneDD;
static KeyboardView *g_kbv;
static CountryList *g_countries;
static DesktopPreview *g_prev;
static Summary *g_sum;
static Textbox *g_host;
static Text *g_hostErr, *g_resText, *g_resWhat, *g_note;
static Button *g_try;

// the running state: the Wi-Fi's connection, the resolution tried, the look to apply
static unsigned g_joinT; static bool g_sawDown; static char g_joinSsid[33];
static unsigned g_tryT; static int g_oldW, g_oldH, g_newW, g_newH;
static unsigned g_lookT; static bool g_lookDirty;
static bool g_scanPending; static unsigned g_scanAt;

static void fill_summary ()
{
	const Country &c = COUNTRIES[g_country];
	snprintf (g_sum->val[0], 120, "%s  \xC2\xB7  %s  \xC2\xB7  %s", c.name, KEYB_NAMES[g_keyb], g_zoneText[g_zone]);
	bool on = g_netlist->state == NET_CONNECTED && g_netlist->sel >= 0;
	if (on) snprintf (g_sum->val[1], 120, "%s  \xC2\xB7  connected, %s", g_nets[g_netlist->sel].ssid, g_ip);
	else snprintf (g_sum->val[1], 120, "Not connected  \xC2\xB7  later, from the Wi-Fi icon in the menu bar");
	snprintf (g_sum->val[2], 120, "%d \xC3\x97 %d%s%s", g_scrW, g_scrH, aspect (g_scrW, g_scrH)[0] ? ", " : "", aspect (g_scrW, g_scrH));
	snprintf (g_sum->val[3], 120, "%s  \xC2\xB7  %s", SCHEMES[g_scheme].name, g_wall ? "a pattern wallpaper" : "generated wallpaper");
	int n = 0; for (int k = 0; k < 4; k++) n += g_services[k];
	snprintf (g_sum->val[4], 120, "%s  \xC2\xB7  %s", g_host->text, n == 0 ? "no remote access" : n == 1 ? "1 remote service on" : "remote services on");
	if (n == 1) for (int k = 0; k < 4; k++) if (g_services[k])
	{
		static const char *const NM[] = { "remote shell (telnet) on", "remote desktop (VNC) on", "remote windows on", "file sharing (FTP) on" };
		snprintf (g_sum->val[4], 120, "%s  \xC2\xB7  %s", g_host->text, NM[k]);
	}
	g_sum->invalidate (true);
}

static void update_nav ()
{
	int p = g_page;
	g_back->hidden = p == 0;
	g_skip->hidden = p != 2 || g_netlist->state == NET_CONNECTED;
	g_next->set (p == 0 ? "Get started" : p == NSTEPS - 1 ? "Start Onyx" : "Continue");
	g_next->disabled = (p == 2 && g_netlist->state != NET_CONNECTED) || (p == 3 && !g_keep->hidden);
	g_back->disabled = p == 3 && !g_keep->hidden;
	g_next->invalidate (true); g_back->invalidate (true); g_skip->invalidate (true);
}

static void show_net (int state);
static void go (int p)
{
	if (p < 0 || p >= NSTEPS) return;
	g_page = p;
	for (int i = 0; i < NSTEPS; i++) g_pages[i]->hidden = i != p;
	g_rail->cur = p; g_rail->invalidate (true);
	char b[24]; snprintf (b, sizeof b, "Step %d of %d", p + 1, NSTEPS); g_stepOf->set (b);
	if (p == 2 && !g_scanned && !g_demo) { g_scanPending = true; g_scanAt = kapi_get_ticks (); }	// (scanned in onTick, the page drawn first)
	if (p == 1) g_countries->setFocus ();
	if (p == NSTEPS - 1) fill_summary ();
	update_nav ();
	g_pages[p]->invalidate (true);
	g_root->invalidate (true);
}

// ---- region & keyboard ----
static void apply_keyb () { if (!g_demo) ax_load_keymap (KEYB_CODES[g_keyb]); }
static void apply_zone ()
{
	if (!g_demo) kapi_set_timezone (zone_offset (g_zone));
}
// The time zone list: the country's zones, then UTC -> the item of g_zone in it.
static int zone_opts ()
{
	const Country &c = COUNTRIES[g_country];
	g_nzoneOpts = 0; int at = 0;
	for (int i = 0; i < 5 && c.zones[i] >= 0; i++) g_zoneMap[g_nzoneOpts++] = c.zones[i];
	g_zoneMap[g_nzoneOpts++] = UTC_ZONE;
	for (int i = 0; i < g_nzoneOpts; i++) { g_zoneOpts[i] = g_zoneText[g_zoneMap[i]]; if (g_zoneMap[i] == g_zone) at = i; }
	return at;
}
static void on_country (Widget &w)
{
	g_country = ((CountryList &) w).sel;
	const Country &c = COUNTRIES[g_country];
	for (int k = 0; k < NKEYB; k++) if (!strcmp (KEYB_CODES[k], c.keyb)) { g_keyb = k; break; }
	g_zone = c.zones[0];
	g_keybDD->setOptions (KEYB_NAMES, NKEYB, g_keyb); g_kbv->set (g_keyb);
	int at = zone_opts (); g_zoneDD->setOptions (g_zoneOpts, g_nzoneOpts, at);
	apply_keyb (); apply_zone ();
}
static void on_keyb (Widget &w) { g_keyb = ((Dropdown &) w).sel; g_kbv->set (g_keyb); apply_keyb (); }
static void on_zone (Widget &w) { int i = ((Dropdown &) w).sel; if (i >= 0 && i < g_nzoneOpts) g_zone = g_zoneMap[i]; apply_zone (); }
static void on_ntp (Widget &w) { g_ntp = ((Checkbox &) w).checked; }

// ---- Wi-Fi ----
static void show_net (int state)
{
	g_netlist->state = state;
	g_conn->hidden = state != NET_CONNECTING;
	g_netlist->hidden = state == NET_CONNECTING || !g_hiddenNet->hidden;
	g_online->hidden = state != NET_CONNECTED;
	g_hidden->hidden = state == NET_CONNECTED || !g_hiddenNet->hidden;
	g_rescan->hidden = state == NET_CONNECTING;
	if (state == NET_CONNECTED)
	{
		if (g_netlist->sel >= 0) { for (int i = 0; i < g_nnets; i++) g_nets[i].con = i == g_netlist->sel; }
		if (!kapi_net_status (g_ip, sizeof g_ip)) scpy (g_ip, sizeof g_ip, "--");
		char b[140]; snprintf (b, sizeof b, "Address %s%s", g_ip, g_ntp ? "  \xC2\xB7  the clock is set from the Internet" : "");
		g_online->set ("You are online", b);
		g_online->top = g_netlist->top + g_netlist->total () + 14;
	}
	g_netlist->place ();
	update_nav ();
	g_pages[2]->invalidate (true);
}
static void start_join (const char *ssid, const char *psk, bool open)
{
	scpy (g_joinSsid, sizeof g_joinSsid, ssid);
	snprintf (g_conn->what, sizeof g_conn->what, "Connecting to %s...", ssid);
	if (g_demo) { show_net (NET_CONNECTING); return; }
	if (!wpa_join (ssid, psk, open, COUNTRIES[g_country].code)) { scpy (g_netlist->err, sizeof g_netlist->err, "Cannot write SD:/etc/wpa_supplicant.conf"); g_netlist->invalidate (true); return; }
	if (kapi_wlan_reconnect () < 0) { scpy (g_netlist->err, sizeof g_netlist->err, "Saved: it joins at the next start"); g_netlist->invalidate (true); return; }
	g_joinT = kapi_get_ticks (); g_sawDown = false;
	show_net (NET_CONNECTING);
}
static void on_connect (Widget &)
{
	NetList *l = g_netlist;
	if (l->sel < 0 || l->sel >= g_nnets) return;
	const Net &n = g_nets[l->sel];
	if (n.sec == WLAN_SEC_WEP) { scpy (l->err, sizeof l->err, "WEP networks are not supported"); l->invalidate (true); return; }
	int pl = (int) strlen (l->pass->text);
	bool open = n.sec == WLAN_SEC_OPEN;
	if (!open && !(n.known && pl == 0) && (pl < 8 || pl > 63)) { scpy (l->err, sizeof l->err, "The password is 8 to 63 characters"); l->invalidate (true); return; }
	l->err[0] = 0;
	start_join (n.ssid, l->pass->text, open);
}
static void on_cancel_join (Widget &) { g_joinT = 0; show_net (NET_PASSWORD); }
static void on_rescan (Widget &) { g_nnets = 0; g_scanned = false; g_netlist->sel = -1; show_net (NET_LIST); g_scanPending = true; }
static void on_other (Widget &) { g_hiddenNet->hidden = false; g_hiddenNet->ssid->setFocus (); show_net (NET_LIST); }
static void on_other_cancel (Widget &) { g_hiddenNet->hidden = true; show_net (NET_LIST); }
static void on_other_connect (Widget &)
{
	const char *s = g_hiddenNet->ssid->text, *p = g_hiddenNet->pass->text;
	int pl = (int) strlen (p);
	if (!s[0] || (pl && (pl < 8 || pl > 63))) return;
	g_hiddenNet->hidden = true;
	Net &n = g_nets[g_nnets < 7 ? g_nnets++ : 6];			// (a row for it)
	scpy (n.ssid, sizeof n.ssid, s); n.level = -60; n.sec = pl ? WLAN_SEC_WPA2 : WLAN_SEC_OPEN; n.con = false; n.known = false;
	g_netlist->sel = (int) (&n - g_nets);
	start_join (s, p, pl == 0);
}

// ---- display ----
static void on_mode (Widget &)
{
	char b[32]; mode_text (g_mode, b, sizeof b); g_resText->set (b);
	const Mode &m = g_modes[g_mode];
	char w[80]; const char *a = aspect (m.w, m.h);
	snprintf (w, sizeof w, "%s%s%s", a, a[0] && (g_mode == g_best || g_mode == g_curMode) ? "  \xC2\xB7  " : "",
		  g_mode == g_best ? "the monitor's own" : g_mode == g_curMode ? "the screen now" : "");
	g_resWhat->set (w);
	g_try->disabled = g_mode == g_curMode; g_try->invalidate (true);
	g_pages[3]->invalidate (true);
}
static void keep_msg ()
{
	snprintf (g_keep->msg, sizeof g_keep->msg, "The screen is now %d \xC3\x97 %d. Onyx goes back to %d \xC3\x97 %d in %d second%s.",
		  g_newW, g_newH, g_oldW, g_oldH, g_keep->secs, g_keep->secs == 1 ? "" : "s");
	snprintf (g_keep->revert->text, sizeof g_keep->revert->text, "Revert (%d)", g_keep->secs);
	g_keep->revert->invalidate (true); g_keep->invalidate (true);
}
static void screen_now ()
{
	kapi_screen_size (&g_scrW, &g_scrH);
	g_curMode = -1;
	for (int i = 0; i < g_nmodes; i++) if (g_modes[i].w == g_scrW && g_modes[i].h == g_scrH) g_curMode = i;
}
static void on_try (Widget &)
{
	const Mode &m = g_modes[g_mode];
	g_oldW = g_scrW; g_oldH = g_scrH; g_newW = m.w; g_newH = m.h;
	int r = g_demo ? 0 : kapi_screen_set (m.w, m.h);
	if (r != 0)
	{
		g_note->c = ERR_C;
		g_note->set (r == -2 ? "Not now: a full-screen app owns the display." : r == -3 ? "The monitor or the firmware refused this size." : "This size is not possible here.");
		return;
	}
	if (!g_demo) kapi_exec ("SD:apps/voronoy.app/main", "");	// (the wallpaper at the new size)
	g_keep->secs = 15; g_tryT = kapi_get_ticks ();
	keep_msg ();
	g_keep->hidden = false; g_keep->bringToFront ();
	update_nav ();
	g_root->invalidate (true);
}
static void end_try (bool keep)
{
	g_tryT = 0;
	g_keep->hidden = true;
	if (!keep && !g_demo) { kapi_screen_set (g_oldW, g_oldH); kapi_exec ("SD:apps/voronoy.app/main", ""); }
	if (keep && !g_demo) cmdline_size (g_newW, g_newH);
	if (!g_demo) screen_now ();
	else if (keep) { g_scrW = g_newW; g_scrH = g_newH; g_curMode = g_mode; }
	g_note->c = 0;
	g_note->set (keep ? "Kept: Onyx starts at this size from now on." : "Back to the size before.");
	on_mode (*g_try);
	update_nav ();
	g_root->invalidate (true);
}
static void on_keep (Widget &) { end_try (true); }
static void on_revert (Widget &) { end_try (false); }

// ---- appearance: written and shown as it is picked (a moment later: several clicks, one repaint) ----
static void apply_look ()
{
	g_lookDirty = false;
	WkTheme t; wk_theme_get (t);
	t.theme = g_scheme; t.active = SCHEMES[g_scheme].c;
	wk_theme_set (t);					// (this window's frame in the new colour)
	wk_decorate_window ();
	g_root->invalidate (true);
	if (g_demo) return;
	static char buf[1400];
	int p = wk_theme_write (t, buf, sizeof buf - 40);
	p += snprintf (buf + p, sizeof buf - p, "wheelspeed=%d\n", kapi_get_wheel_speed ());
	kapi_save_file ("SD:/etc/theme.txt", buf, (unsigned) p);
	Wallpaper wp; wp_load (wp);
	wp.c1 = tint1 (); wp.c2 = tint2 ();
	if (g_wall == 0) wp.mode = WP_VORONOI;
	else { wp.mode = WP_PATTERN; wp.vertical = 1; snprintf (wp.pattern, sizeof wp.pattern, WALLPAPER_DIR "/%s.png", WALLS[g_wall]); }
	wp_save (wp);
	kapi_exec ("SD:apps/voronoy.app/main", "");			// (the desktop painted again)
}
static void on_look (Widget &) { g_prev->invalidate (true); g_lookDirty = true; g_lookT = kapi_get_ticks (); }

// ---- name ----
static bool host_ok (const char *s)
{
	int n = (int) strlen (s);
	if (n < 1 || n > 63 || s[0] == '-' || s[n - 1] == '-') return false;
	for (int i = 0; i < n; i++) { char c = s[i]; if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-')) return false; }
	return true;
}

// ---- the end: everything written, the desktop started -------------------------------------------------------
static void finish ()
{
	if (g_demo) { kapi_exit (0); return; }
	if (g_lookDirty) apply_look ();
	char zone[12]; snprintf (zone, sizeof zone, "%d", zone_offset (g_zone));
	ini_set ("SD:/etc/system.ini", "timezone", zone);
	ini_set ("SD:/etc/system.ini", "ntp", g_ntp ? "pool.ntp.org" : "off");
	ini_set ("SD:/etc/system.ini", "hostname", g_host->text);
	if (g_netlist->state != NET_CONNECTED)			// (skipped: the Wi-Fi's country kept anyway)
	{
		wpa_load ();
		scpy (g_wpaCountry, sizeof g_wpaCountry, COUNTRIES[g_country].code);
		wpa_save ();
	}
	bool was[4]; services_read (was);
	static char held[2048];
	autostart_finish (KEYB_CODES[g_keyb], g_services, held, sizeof held);
	for (int k = 0; k < 4; k++)				// the services: started or stopped now
	{
		if (g_services[k] && !running (SERVICES[k])) run_line (SERVICES[k]);
		if (!g_services[k] && was[k]) stop_process (SERVICES[k]);
	}
	for (char *l = strtok (held, "\n"); l; l = strtok (0, "\n")) run_line (l);	// the menu bar, the dock...
	kapi_exit (0);
}

static void on_next (Widget &)
{
	if (g_page == 5 && !host_ok (g_host->text))
	{
		g_hostErr->c = ERR_C; g_hostErr->set ("Letters, digits and '-' only (not at the ends)");
		g_host->setFocus ();
		return;
	}
	if (g_page == NSTEPS - 1) { finish (); return; }
	if (g_page == 4 && g_lookDirty) apply_look ();
	go (g_page + 1);
}
static void on_back (Widget &) { go (g_page - 1); }
static void on_skip (Widget &) { go (3); }

// ---- the loop: the scan, the connection, the counter, the look --------------------------------------------------
class SetupRoot : public Root
{
public:
	SetupRoot (int x, int y) : Root (x, y, W, H, "Onyx Setup", WIN_FLAG_FIXED) {}
	void onTick () override
	{
		unsigned now = kapi_get_ticks ();
		if (g_scanPending && now - g_scanAt > 4)		// (the page drawn "Looking for networks..." first)
		{
			g_scanPending = false;
			scan_nets ();
			if (g_nnets && g_nets[0].con && kapi_net_status (0, 0)) { g_netlist->sel = 0; show_net (NET_CONNECTED); }
			else show_net (NET_LIST);
		}
		if (g_netlist->state == NET_CONNECTING)
		{
			static unsigned spin;
			if (now - spin >= 8) { spin = now; g_conn->phase = (g_conn->phase + 1) % 12; g_conn->invalidate (true); }
			if (!g_demo && g_joinT)
			{
				bool up = kapi_net_status (0, 0) != 0;
				if (!up) g_sawDown = true;
				if (up && (g_sawDown || now - g_joinT > 600) && now - g_joinT > 200) { g_joinT = 0; show_net (NET_CONNECTED); }
				else if (now - g_joinT > 3000)
				{
					g_joinT = 0;
					scpy (g_netlist->err, sizeof g_netlist->err, "Could not connect: check the password");
					show_net (NET_PASSWORD);
				}
			}
		}
		if (g_tryT)
		{
			int left = 15 - (int) ((now - g_tryT) / 100);
			if (left <= 0) end_try (false);
			else if (left != g_keep->secs) { g_keep->secs = left; keep_msg (); }
		}
		if (g_lookDirty && now - g_lookT > 40) apply_look ();
	}
};

int main (void)
{
	ft_wtk_install ("DejaVu Sans", 13);		// (before the widgets)
	g_h1 = face ("DejaVu Sans", 22); g_hero = face ("DejaVu Sans", 32); g_lead = face ("DejaVu Sans", 15); g_small = face ("DejaVu Sans", 11);
	char a[32]; int na = kapi_get_args (a, sizeof a); a[na > 0 && na < 32 ? na : 0] = 0;
	int demo = -1; char variant = 0;
	if (!strncmp (a, "--demo", 6)) { const char *p = a + 6; while (*p == ' ') p++; if (*p >= '0' && *p <= '6') { demo = *p - '0'; variant = p[1]; } g_demo = true; }

	// what is there now: the zones' names, the screen, the monitor, the services, the Wi-Fi's country
	for (int z = 0; z < NZONES; z++) zone_label (z, g_zoneText[z], sizeof g_zoneText[z]);
	g_nmodes = 0;
	for (unsigned i = 0; i < sizeof MODES / sizeof MODES[0]; i++) g_modes[g_nmodes++] = MODES[i];
	int nw = 0, nh = 0;
	if (kapi_screen_native (&nw, &nh) && nw >= 640 && nw <= 2560 && nh >= 480 && nh <= 1600)
	{
		for (int i = 0; i < g_nmodes; i++) if (g_modes[i].w == nw && g_modes[i].h == nh) g_best = i;
		if (g_best < 0 && g_nmodes < 16)			// (not in the list: put in its place)
		{
			int i = g_nmodes++;
			while (i > 0 && (g_modes[i - 1].w > nw || (g_modes[i - 1].w == nw && g_modes[i - 1].h > nh))) { g_modes[i] = g_modes[i - 1]; i--; }
			g_modes[i].w = nw; g_modes[i].h = nh; g_best = i;
		}
	}
	screen_now ();
	g_mode = g_best >= 0 ? g_best : g_curMode >= 0 ? g_curMode : 0;
	if (!g_demo)
	{
		services_read (g_services);
		wpa_load ();
		for (int i = 0; i < NCOUNTRIES; i++) if (!strcmp (COUNTRIES[i].code, g_wpaCountry)) g_country = i;
		for (int k = 0; k < NKEYB; k++) if (!strcmp (KEYB_CODES[k], COUNTRIES[g_country].keyb)) g_keyb = k;
		g_zone = COUNTRIES[g_country].zones[0];
		WkTheme t; wk_theme_get (t); if (t.theme >= 0 && t.theme < 5) g_scheme = t.theme;
		Wallpaper wp; wp_load (wp);
		for (int i = 0; i < 16; i++) if (TINTS[i] == wp.c1) g_tint = i;
	}

	// the window: in the middle of the screen, where it stays (WIN_FLAG_FIXED)
	int sw = 1024, sh = 768; kapi_screen_size (&sw, &sh);
	SetupRoot root ((sw - W - 8) / 2, (sh - H - 32) / 2);
	if (root.canvas.px == 0) return 1;
	g_root = &root;
	{
		struct kapi_win_geom g;
		if (kapi_win_geometry (&g) == 0) kapi_move_window ((sw - g.w) / 2, (sh - g.h) / 2);
	}
	root.setBg (C_FIELD);

	g_rail = new Rail (0, 0, RAIL_W, H); root.addChild (g_rail);

	// the footer
	Panel *foot = new Panel (RAIL_W, H - FOOT_H, W - RAIL_W, FOOT_H, C_BG); root.addChild (foot);
	foot->addChild (new Panel (0, 0, W - RAIL_W, 1, wk_mix (C_BG, C_TEXT, 60)));
	g_stepOf = new Text (PX, 1, 140, FOOT_H - 1, "Step 1 of 7", 0, g_small, true); foot->addChild (g_stepOf);
	g_next = new AccentButton (W - RAIL_W - PAD - 132, 14, 132, 32, "Continue", on_next); foot->addChild (g_next);
	g_back = new Button (W - RAIL_W - PAD - 132 - 10 - 104, 11, 104, 38, "Back", on_back); foot->addChild (g_back);
	g_skip = new Link (W - RAIL_W - PAD - 132 - 10 - 104 - 130, 14, 110, 32, "Skip for now", on_skip); g_skip->tag = 1; foot->addChild (g_skip);

	// 0: welcome
	Page *p = g_pages[0] = new Page (0, "");
	p->addChild (new Welcome (PX, 70, PW, H - FOOT_H - 90));
	// 1: region & keyboard
	p = g_pages[1] = new Page ("Region & keyboard", "Your country sets the time zone and which Wi-Fi channels the radio may use; the keyboard layout is proposed from it.");
	int y = p->bodyY, lw = 244, rx = PX + lw + 26, rw = PW - lw - 26;
	p->addChild (new Text (PX, y, lw, 20, "Country or region", 2));
	g_countries = new CountryList (PX, y + 26, lw, 8 + 9 * CountryList::RH, on_country); p->addChild (g_countries);
	g_countries->show (g_country);
	p->addChild (new Text (rx, y, rw, 20, "Keyboard layout", 2));
	g_keybDD = new Dropdown (rx, y + 24, rw, 30, KEYB_NAMES, NKEYB, g_keyb, on_keyb); p->addChild (g_keybDD);
	p->addChild (new Text (rx, y + 68, rw, 20, "Time zone", 2));
	int zat = zone_opts ();
	g_zoneDD = new Dropdown (rx, y + 92, rw, 30, g_zoneOpts, g_nzoneOpts, zat, on_zone); p->addChild (g_zoneDD);
	p->addChild (new Checkbox (rx, y + 134, rw, 22, "Set the clock from the Internet", g_ntp, on_ntp, C_FIELD));
	p->addChild (new Text (rx, y + 172, rw, 20, "Try it", 2));
	Textbox *tryit = new Textbox (rx, y + 196, rw, 30, g_demo ? "O\xC3\xB9 est la gare ? H\xC3\xA9l\xC3\xA8ne \xC3\xA0 9h" : ""); p->addChild (tryit);
	g_kbv = new KeyboardView (rx, y + 238, rw, 84); p->addChild (g_kbv);
	g_kbv->set (g_keyb);
	// 2: Wi-Fi
	p = g_pages[2] = new Page ("Connect to Wi-Fi", "Pick your network and type its password. You can skip this and connect later from the Wi-Fi icon in the menu bar.");
	y = p->bodyY;
	p->addChild (new Text (PX, y, 200, 24, "Networks nearby", 2));
	g_rescan = new Button (PX + PW - 104, y - 6, 104, 34, "Rescan", on_rescan); p->addChild (g_rescan);
	if (g_demo) scan_nets ();
	g_netlist = new NetList (PX, y + 36, PW, 300, on_connect); p->addChild (g_netlist);
	g_conn = new Connecting (PX, y + 36, PW, 216, on_cancel_join); g_conn->hidden = true; p->addChild (g_conn);
	g_hiddenNet = new HiddenNet (PX, y + 36, PW, 206, on_other_connect, on_other_cancel); g_hiddenNet->hidden = true; p->addChild (g_hiddenNet);
	g_online = new Banner (PX, y + 300, PW, 54, OK_C, 0, "You are online", ""); g_online->hidden = true; p->addChild (g_online);
	g_hidden = new Link (PX, H - FOOT_H - 40, 260, 24, "Other network (hidden name)...", on_other); p->addChild (g_hidden);
	// 3: display
	char dsub[240];
	if (g_best >= 0) snprintf (dsub, sizeof dsub, "Onyx found a monitor able to show %d \xC3\x97 %d. Bigger means more room for windows; smaller means bigger text.", g_modes[g_best].w, g_modes[g_best].h);
	else snprintf (dsub, sizeof dsub, "The monitor did not tell its size: pick the one written in its manual. Bigger means more room for windows; smaller means bigger text.");
	p = g_pages[3] = new Page ("Display", dsub);
	y = p->bodyY; lw = 240; rx = PX + lw + 26; rw = PW - lw - 26;
	p->addChild (new ModeList (PX, y, lw, 8 + g_nmodes * ModeList::RH, on_mode));
	p->addChild (new Monitor (rx, y, rw, 196));
	g_resText = new Text (rx, y + 200, rw, 26, "", 2, g_lead); p->addChild (g_resText);
	g_resWhat = new Text (rx, y + 224, rw, 20, "", 0, 0, true); p->addChild (g_resWhat);
	g_try = new Button (rx, y + 252, 110, 38, "Try it", on_try); p->addChild (g_try);
	g_note = new Text (rx, y + 298, rw, 56, "Applied at once; if the picture does not come back, Onyx returns to the previous size by itself after 15 seconds.", 0, g_small, true);
	g_note->lh = 16; p->addChild (g_note);
	// 4: appearance
	p = g_pages[4] = new Page ("Appearance", "Choose the colour of the window in front and the wallpaper. The preview shows the desktop you will get.");
	y = p->bodyY - 8;
	g_prev = new DesktopPreview (PX, y, PW, 132); p->addChild (g_prev);
	p->addChild (new Text (PX, y + 142, 200, 20, "Colour", 2));
	p->addChild (new SchemePicker (PX - 4, y + 164, PW, 68, on_look));
	p->addChild (new Text (PX, y + 234, 200, 20, "Wallpaper and its tint", 2));
	p->addChild (new WallPicker (PX - 4, y + 256, PW + 8, 140, on_look));
	// 5: name & privacy
	p = g_pages[5] = new Page ("Name & privacy", "How this Raspberry Pi shows itself on your network, and who may reach it from there.");
	y = p->bodyY;
	p->addChild (new Text (PX, y, 200, 20, "Computer name", 2));
	g_host = new Textbox (PX, y + 24, 260, 30, "onyx"); g_host->maxLen = 63; p->addChild (g_host);
	g_hostErr = new Text (PX + 274, y + 24, PW - 274, 30, "Seen by that name on the network (from the next start)", 0, g_small, true); p->addChild (g_hostErr);
	p->addChild (new Text (PX, y + 70, 200, 20, "Remote access", 2));
	p->addChild (new Banner (PX, y + 94, PW, 40, WARN_C, 1, "Only file sharing asks for a password: turn on only what you use, on a network you trust.", ""));
	p->addChild (new Services (PX, y + 144, PW, 4 * Services::RH));
	p->addChild (new Text (PX, y + 144 + 4 * Services::RH + 12, PW, 20, "Each one can be turned on or off later: its line in SD:/etc/autostart.", 0, g_small, true));
	// 6: ready
	p = g_pages[6] = new Page (0, "");
	{
		class Done : public Widget
		{
		public:
			Done (int l, int t, int w, int h) : Widget (l, t, w, h) {}
			void onDraw () override
			{
				canvas.clear (C_FIELD);
				big_disc (canvas, 0, 0, 52, OK_C);
				wk_glyph (canvas, WKG_CHECK, 26, 26, 24, 0x00FFFFFF);
				ftext (canvas, g_hero, 0, 64, 44, "You're all set", C_FIELD_TEXT, 2);
				fwrap (&canvas, g_lead, 0, 114, width, 23, "Here is what Onyx will use. Change any of it now, or later in the Control Panel.", dim_ink (C_FIELD));
			}
		};
		p->addChild (new Done (PX, 34, PW, 170));
	}
	g_sum = new Summary (PX, 204, PW, 8 + 5 * Summary::RH); p->addChild (g_sum);
	p->addChild (new Text (PX, 204 + 8 + 5 * Summary::RH + 16, PW, 20, "Start Onyx: the menu bar and the dock appear; this assistant does not come back.", 0, g_small, true));

	for (int i = 0; i < NSTEPS; i++) root.addChild (g_pages[i]);
	g_keep = new KeepSheet ((W - 420) / 2 + RAIL_W / 2, 150, 420, 180, on_keep, on_revert); g_keep->hidden = true; root.addChild (g_keep);
	on_mode (*g_try);

	// the demo's states (the screenshots)
	if (demo >= 0)
	{
		g_country = 1; g_scheme = demo >= 4 ? 1 : 0; g_host->setText ("onyx-salon");
		if (demo == 4) { g_wall = 3; g_tint = 19; }
		g_countries->sel = 1;
		go (demo);
		if (demo == 2 && !variant) { g_netlist->sel = 0; g_netlist->state = NET_PASSWORD; g_netlist->pass->setText ("correct-horse"); g_netlist->pass->caret = 13; show_net (NET_PASSWORD); }
		else if (demo == 2 && variant == 'b') { g_netlist->sel = 0; snprintf (g_conn->what, sizeof g_conn->what, "Connecting to %s...", g_nets[0].ssid); show_net (NET_CONNECTING); }
		else if (demo >= 2) { g_netlist->sel = 0; show_net (NET_CONNECTED); }
		if (demo == 1) { tryit->setFocus (); tryit->caret = (int) strlen (tryit->text); }
		if (demo == 3 && variant == 'b') on_try (*g_try);
		if (demo == 5) { g_host->setFocus (); g_host->caret = (int) strlen (g_host->text); }
		go (demo);
		if (demo == 3 && variant == 'b') update_nav ();
	}
	else go (0);

	root.run ();
	return 0;
}
