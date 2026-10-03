//
// wtk/theme.cpp -- the palette (wtk/theme.h) and SD:/etc/theme.txt.
//
#include "wtk/theme.h"
#include "wtk/paint.h"
#include "kapi.h"

namespace wtk {

// The modernised CDE's defaults: CDE's beige face, a teal accent, the Peach frame in front.
unsigned C_BG, C_FACE, C_FACE_HI, C_FACE_DN, C_BORDER, C_TEXT, C_ACCENT = 0x004992A7, C_DIS,
	 C_FIELD, C_FIELD_TEXT, C_SEL_TEXT, C_FRAME_ACTIVE = 0x00F0B07A, C_FRAME_INACTIVE = WK_GREY;
int	 WK_OUTLINE = 1, WK_STYLE = WK_STYLE_CDE;
unsigned C_DOCK = 0x00A4BACE, C_BUTTON, C_BUTTON_TEXT, C_MENUBAR;

// Dark Coffee: black coffee's browns under Milk's beads, a caramel accent, a black outline.
static const WkPalette s_coffee = { 0x001F1A17, 0x00C8813F, 0x00322A26, 0x00151210, 0x00352C27, 0x00151210, 2 };

const WkNamedTheme wk_themes[] = {
	{ "Peach", 0x00F0B07A, WK_STYLE_CDE }, { "Steel", 0x007A98C0, WK_STYLE_CDE },
	{ "Sage", 0x0080AA76, WK_STYLE_CDE }, { "Brick", 0x00C45450, WK_STYLE_CDE },
	{ "Slate", 0x003A4458, WK_STYLE_CDE }, { "Milk", 0x00D4D4D6, WK_STYLE_MILK },
	{ "Dark Coffee", 0x004A3E37, WK_STYLE_MILK, &s_coffee }, { 0, 0, 0 }
};

// Each style's own colours: the window's (the apps' face), the accent, the frames behind, the dock.
static const WkPalette s_palettes[2] = {
	{ 0x00D0C2BA, 0x004992A7, WK_GREY, 0x00A4BACE, WK_AUTO, WK_AUTO, 1 },	// CDE's beige, a teal, grey frames behind, a blue dock
	{ 0x00E4E4E4, 0x003D86DA, 0x00E2E2E4, 0x00D9DDE3, WK_AUTO, WK_AUTO, 1 },	// Milk: light greys, Aqua's blue, a silver dock
};
const WkPalette &wk_style_palette (int style) { return s_palettes[style == WK_STYLE_MILK ? 1 : 0]; }
const WkPalette &wk_theme_palette (int theme)
{
	int n = 0; while (wk_themes[n].name) n++;
	if (theme < 0 || theme >= n) return s_palettes[0];
	return wk_themes[theme].pal ? *wk_themes[theme].pal : wk_style_palette (wk_themes[theme].style);
}

// The shades of the window's colour (the face: the apps' background), and what is written on it.
void wk_theme_face (unsigned face)
{
	C_BG = C_FACE = face & 0x00FFFFFFu;
	C_FACE_HI = wk_tone (face, 176);
	C_FACE_DN = wk_tone (face, 108);
	C_BORDER = wk_tone (face, 70);
	C_TEXT = wk_ink_on (face);
	C_DIS = wk_mix (face, C_TEXT, 110);
	C_FIELD = wk_tone (face, wk_bright (face) < 110 ? 90 : 236);	// (a dark face: darker)				// a field: the face, nearly white
	C_FIELD_TEXT = wk_ink_on (C_FIELD);
	C_SEL_TEXT = wk_ink_on (C_ACCENT);
	C_BUTTON = C_FACE; C_BUTTON_TEXT = C_TEXT;
	C_MENUBAR = C_FACE;
}

void wk_theme_defaults (WkTheme &t)
{
	const WkPalette &p = s_palettes[0];
	t.theme = 0; t.active = wk_themes[0].frame; t.inactive = p.inactive;
	t.window = p.face; t.button = WK_AUTO; t.field = WK_AUTO; t.accent = p.accent;
	t.menubar = WK_AUTO; t.dock = p.dock; t.outline = 1; t.style = WK_STYLE_CDE;
}

static void take_palette (WkTheme &t, int style, const WkPalette &p)
{
	t.style = style;
	t.window = p.face; t.accent = p.accent; t.inactive = p.inactive; t.dock = p.dock;
	t.button = p.button; t.field = p.field; t.menubar = WK_AUTO;
	t.outline = p.outline;
}
// (the colours of what t is now: its named theme's, else its style's)
static const WkPalette *palette_of (const WkTheme &t)
{ return t.theme >= 0 ? &wk_theme_palette (t.theme) : &wk_style_palette (t.style); }

void wk_theme_take_style (WkTheme &t, int style)
{
	if (style == t.style) return;
	int outline = t.outline;
	take_palette (t, style, wk_style_palette (style));
	t.outline = outline;
}

void wk_theme_take (WkTheme &t, int theme)
{
	const WkPalette *was = palette_of (t), *now = &wk_theme_palette (theme);
	int style = wk_themes[theme].style;
	if (style != t.style || now != was) take_palette (t, style, *now);
	t.theme = theme; t.active = wk_themes[theme].frame;
}

void wk_theme_set (const WkTheme &t)
{
	C_FRAME_ACTIVE = t.active & 0x00FFFFFFu;
	C_FRAME_INACTIVE = t.inactive & 0x00FFFFFFu;
	C_ACCENT = t.accent & 0x00FFFFFFu;
	WK_OUTLINE = t.outline;
	WK_STYLE = t.style;
	C_DOCK = t.dock & 0x00FFFFFFu;
	wk_theme_face (t.window);
	if (t.button != WK_AUTO) { C_BUTTON = t.button & 0x00FFFFFFu; C_BUTTON_TEXT = wk_ink_on (C_BUTTON); }
	if (t.field != WK_AUTO) { C_FIELD = t.field & 0x00FFFFFFu; C_FIELD_TEXT = wk_ink_on (C_FIELD); }
	if (t.menubar != WK_AUTO) C_MENUBAR = t.menubar & 0x00FFFFFFu;
}

static WkTheme s_cur;
static struct ThemeInit { ThemeInit () { wk_theme_defaults (s_cur); wk_theme_set (s_cur); } } s_themeInit;

void wk_theme_get (WkTheme &t) { t = s_cur; }

static bool eq (const char *a, const char *b)
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

static bool colour (const char *v, unsigned *out)
{
	if (eq (v, "auto")) { *out = WK_AUTO; return true; }
	if (v[0] == '0' && (v[1] == 'x' || v[1] == 'X')) v += 2;
	else if (v[0] == '#') v++;
	unsigned c = 0; int n = 0;
	for (; *v; v++, n++)
	{
		char ch = *v; int d;
		if (ch >= '0' && ch <= '9') d = ch - '0';
		else if (ch >= 'a' && ch <= 'f') d = ch - 'a' + 10;
		else if (ch >= 'A' && ch <= 'F') d = ch - 'A' + 10;
		else break;
		c = c * 16 + (unsigned) d;
	}
	if (n == 0) return false;
	*out = c & 0x00FFFFFFu;
	return true;
}

void wk_theme_parse (const char *text, WkTheme &t)
{
	static char buf[8193];
	int n = 0;
	while (text[n] && n < (int) sizeof buf - 1) { buf[n] = text[n]; n++; }
	buf[n] = 0;
	bool haveActive = false;
	// (a style other than t's: the colours the text leaves out are that style's -- wk_theme_take_style)
	WkTheme was = t; int style = -1, named = -1, namedIdx = -1;
	bool have[8] = {};					// inactive window button field accent menubar dock
	for (char *p = buf; *p; )
	{
		char *line = p;
		while (*p && *p != '\n') p++;
		if (*p) *p++ = 0;
		char *hash = line; while (*hash && *hash != '#') hash++;
		*hash = 0;
		char *eqs = line; while (*eqs && *eqs != '=') eqs++;
		if (!*eqs) continue;
		*eqs = 0;
		char *k = line, *v = eqs + 1;
		while (*k == ' ' || *k == '\t') k++;
		char *ke = k; while (*ke) ke++; while (ke > k && (ke[-1] == ' ' || ke[-1] == '\t' || ke[-1] == '\r')) *--ke = 0;
		while (*v == ' ' || *v == '\t') v++;
		char *ve = v; while (*ve) ve++; while (ve > v && (ve[-1] == ' ' || ve[-1] == '\t' || ve[-1] == '\r')) *--ve = 0;
		unsigned c;
		if (eq (k, "theme"))
		{
			for (int i = 0; wk_themes[i].name; i++)
				if (eq (v, wk_themes[i].name))
				{
					named = wk_themes[i].style; namedIdx = i;
					if (!haveActive) { t.active = wk_themes[i].frame; t.theme = i; }
				}
		}
		else if (eq (k, "style")) style = eq (v, "milk") ? WK_STYLE_MILK : WK_STYLE_CDE;
		else if (eq (k, "active") && colour (v, &c) && c != WK_AUTO)
		{
			t.active = c; haveActive = true; t.theme = -1;
			for (int i = 0; wk_themes[i].name; i++) if (wk_themes[i].frame == c) t.theme = i;
		}
		else if (eq (k, "inactive") && colour (v, &c) && c != WK_AUTO) { t.inactive = c; have[0] = true; }
		else if ((eq (k, "window") || eq (k, "face")) && colour (v, &c) && c != WK_AUTO) { t.window = c; have[1] = true; }
		else if (eq (k, "button") && colour (v, &c)) { t.button = c; have[2] = true; }
		else if (eq (k, "field") && colour (v, &c)) { t.field = c; have[3] = true; }
		else if (eq (k, "accent") && colour (v, &c) && c != WK_AUTO) { t.accent = c; have[4] = true; }
		else if (eq (k, "outline")) { t.outline = eq (v, "none") ? 0 : eq (v, "black") ? 2 : 1; have[7] = true; }
		else if (eq (k, "menubar") && colour (v, &c)) { t.menubar = c; have[5] = true; }
		else if (eq (k, "dock") && colour (v, &c) && c != WK_AUTO) { t.dock = c; have[6] = true; }
	}
	int s = style >= 0 ? style : named >= 0 ? named : was.style;	// (the key, else the theme's)
	const WkPalette *now = namedIdx >= 0 ? &wk_theme_palette (namedIdx) : &wk_style_palette (s);
	if (s != was.style || (namedIdx >= 0 && now != palette_of (was)))	// (another style, or a theme with colours of its own)
	{
		WkTheme got = t;
		take_palette (t, s, *now);
		if (have[7]) t.outline = got.outline;
		if (have[0]) t.inactive = got.inactive;
		if (have[1]) t.window = got.window;
		if (have[2]) t.button = got.button;
		if (have[3]) t.field = got.field;
		if (have[4]) t.accent = got.accent;
		if (have[5]) t.menubar = got.menubar;
		if (have[6]) t.dock = got.dock;
	}
}

static int put (char *o, int p, int cap, const char *s) { while (*s && p < cap - 1) o[p++] = *s++; o[p] = 0; return p; }
static int put_colour (char *o, int p, int cap, unsigned c)
{
	if (c == WK_AUTO) return put (o, p, cap, "auto");
	const char *hx = "0123456789ABCDEF";
	char b[9] = { '0', 'x' };
	for (int i = 0; i < 6; i++) b[2 + i] = hx[(c >> ((5 - i) * 4)) & 0xF];
	b[8] = 0;
	return put (o, p, cap, b);
}

int wk_theme_write (const WkTheme &t, char *o, int cap)
{
	int p = 0;
	o[0] = 0;
	p = put (o, p, cap,
		"# The desktop's look (the modernised CDE), read by every app when it starts (wtk/theme.h);\n"
		"# written by the Control Panel's Theme applet. theme: Peach, Steel, Sage, Brick, Slate, Milk, Dark Coffee\n"
		"# (the window in front's frame; active = a colour instead); style: cde or milk (the title\n"
		"# buttons framed, or OS X's beads); inactive: the frames behind; window: the windows' content;\n"
		"# button, field (text boxes, lists), menubar: auto = from the window's; accent: focus and\n"
		"# selection; outline: none, dark or black; dock: the dock's face.\n");
	if (t.theme >= 0) { p = put (o, p, cap, "theme    = "); p = put (o, p, cap, wk_themes[t.theme].name); }
	else { p = put (o, p, cap, "active   = "); p = put_colour (o, p, cap, t.active); }
	p = put (o, p, cap, t.style == WK_STYLE_MILK ? "\nstyle    = milk" : "\nstyle    = cde");
	struct { const char *k; unsigned c; } kv[] = {
		{ "\ninactive = ", t.inactive }, { "\nwindow   = ", t.window }, { "\nbutton   = ", t.button },
		{ "\nfield    = ", t.field }, { "\naccent   = ", t.accent }, { "\nmenubar  = ", t.menubar },
		{ "\ndock     = ", t.dock } };
	for (unsigned i = 0; i < sizeof kv / sizeof kv[0]; i++) { p = put (o, p, cap, kv[i].k); p = put_colour (o, p, cap, kv[i].c); }
	p = put (o, p, cap, "\noutline  = ");
	p = put (o, p, cap, t.outline == 0 ? "none" : t.outline == 2 ? "black" : "dark");
	p = put (o, p, cap, "\n");
	return p;
}

static bool s_loaded = false;

void wk_theme_reload () { s_loaded = false; wk_theme_load (); }

void wk_theme_load ()
{
	if (s_loaded) return;
	s_loaded = true;
	void *f = kapi_open ("SD:/etc/theme.txt");
	if (f == 0) return;
	unsigned sz = kapi_fsize (f);
	if (sz == 0 || sz > 8192) { kapi_close (f); return; }
	static char buf[8193];
	int n = kapi_read (f, buf, sz);
	kapi_close (f);
	if (n <= 0) return;
	buf[n] = 0;
	wk_theme_defaults (s_cur);
	wk_theme_parse (buf, s_cur);
	wk_theme_set (s_cur);
}

} // namespace wtk
