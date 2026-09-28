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

const WkNamedTheme wk_themes[] = {
	{ "Peach", 0x00F0B07A }, { "Steel", 0x007A98C0 }, { "Sage", 0x0080AA76 },
	{ "Brick", 0x00C45450 }, { "Slate", 0x003A4458 }, { 0, 0 }
};

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
}

// (the palette is ready before any app code runs: a global widget, a default argument)
static struct ThemeInit { ThemeInit () { wk_theme_face (0x00D0C2BA); } } s_themeInit;

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

void wk_theme_load ()
{
	static bool done = false;
	if (done) return;
	done = true;
	void *f = kapi_open ("SD:/etc/theme.txt");
	if (f == 0) return;
	unsigned sz = kapi_fsize (f);
	if (sz == 0 || sz > 8192) { kapi_close (f); return; }
	static char buf[8193];
	int n = kapi_read (f, buf, sz);
	kapi_close (f);
	if (n <= 0) return;
	buf[n] = 0;
	unsigned face = C_FACE; bool haveFace = false, haveActive = false;
	unsigned active = C_FRAME_ACTIVE;
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
				if (eq (v, wk_themes[i].name) && !haveActive) active = wk_themes[i].frame;
		}
		else if (eq (k, "active") && colour (v, &c)) { active = c; haveActive = true; }
		else if (eq (k, "inactive") && colour (v, &c)) C_FRAME_INACTIVE = c;
		else if (eq (k, "face") && colour (v, &c)) { face = c; haveFace = true; }
		else if (eq (k, "accent") && colour (v, &c)) C_ACCENT = c;
		else if (eq (k, "outline")) WK_OUTLINE = eq (v, "none") ? 0 : eq (v, "black") ? 2 : 1;
	}
	C_FRAME_ACTIVE = active;
	wk_theme_face (haveFace ? face : C_FACE);
}

} // namespace wtk
