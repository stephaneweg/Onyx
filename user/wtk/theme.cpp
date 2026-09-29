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
int	 WK_OUTLINE = 1;
unsigned C_DOCK = 0x00A4BACE, C_BUTTON, C_BUTTON_TEXT, C_MENUBAR;

const WkNamedTheme wk_themes[] = {
	{ "Peach", 0x00F0B07A }, { "Steel", 0x007A98C0 }, { "Sage", 0x0080AA76 },
	{ "Brick", 0x00C45450 }, { "Slate", 0x003A4458 }, { 0, 0 }
};

static const unsigned DEF_WINDOW = 0x00D0C2BA, DEF_ACCENT = 0x004992A7, DEF_DOCK = 0x00A4BACE;

// The shades of the window's colour (the face: the apps' background), and what is written on it.
void wk_theme_face (unsigned face)
{
	C_BG = C_FACE = face & 0x00FFFFFFu;
	C_FACE_HI = wk_tone (face, 176);
	C_FACE_DN = wk_tone (face, 108);
	C_BORDER = wk_tone (face, 70);
	C_TEXT = wk_ink_on (face);
	C_DIS = wk_mix (face, C_TEXT, 110);
	C_FIELD = wk_tone (face, 236);				// a field: the face, nearly white
	C_FIELD_TEXT = wk_ink_on (C_FIELD);
	C_SEL_TEXT = wk_ink_on (C_ACCENT);
	C_BUTTON = C_FACE; C_BUTTON_TEXT = C_TEXT;
	C_MENUBAR = C_FACE;
}

void wk_theme_defaults (WkTheme &t)
{
	t.theme = 0; t.active = wk_themes[0].frame; t.inactive = WK_GREY;
	t.window = DEF_WINDOW; t.button = WK_AUTO; t.field = WK_AUTO; t.accent = DEF_ACCENT;
	t.menubar = WK_AUTO; t.dock = DEF_DOCK; t.outline = 1;
}

void wk_theme_set (const WkTheme &t)
{
	C_FRAME_ACTIVE = t.active & 0x00FFFFFFu;
	C_FRAME_INACTIVE = t.inactive & 0x00FFFFFFu;
	C_ACCENT = t.accent & 0x00FFFFFFu;
	WK_OUTLINE = t.outline;
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
				if (eq (v, wk_themes[i].name) && !haveActive) { t.active = wk_themes[i].frame; t.theme = i; }
		}
		else if (eq (k, "active") && colour (v, &c) && c != WK_AUTO)
		{
			t.active = c; haveActive = true; t.theme = -1;
			for (int i = 0; wk_themes[i].name; i++) if (wk_themes[i].frame == c) t.theme = i;
		}
		else if (eq (k, "inactive") && colour (v, &c) && c != WK_AUTO) t.inactive = c;
		else if ((eq (k, "window") || eq (k, "face")) && colour (v, &c) && c != WK_AUTO) t.window = c;
		else if (eq (k, "button") && colour (v, &c)) t.button = c;
		else if (eq (k, "field") && colour (v, &c)) t.field = c;
		else if (eq (k, "accent") && colour (v, &c) && c != WK_AUTO) t.accent = c;
		else if (eq (k, "outline")) t.outline = eq (v, "none") ? 0 : eq (v, "black") ? 2 : 1;
		else if (eq (k, "menubar") && colour (v, &c)) t.menubar = c;
		else if (eq (k, "dock") && colour (v, &c) && c != WK_AUTO) t.dock = c;
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
		"# written by the Control Panel's Theme applet. theme: Peach, Steel, Sage, Brick, Slate (the\n"
		"# window in front's frame; active = a colour instead); inactive: the frames behind; window: the\n"
		"# windows' content; button, field (text boxes, lists), menubar: auto = from the window's;\n"
		"# accent: focus and selection; outline: none, dark or black; dock: the dock's face.\n");
	if (t.theme >= 0) { p = put (o, p, cap, "theme    = "); p = put (o, p, cap, wk_themes[t.theme].name); }
	else { p = put (o, p, cap, "active   = "); p = put_colour (o, p, cap, t.active); }
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
