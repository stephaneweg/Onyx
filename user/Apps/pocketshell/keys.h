//
// keys.h -- the pocket shell's ON-SCREEN KEYBOARD (PocketUI's phase P10; docs/POCKETUI-TECH-STUDY.md sections 6.11
// and 7.4: pocket only -- console has none). A window of the shell's at the screen's bottom edge, opaque and as
// wide as the screen: PocketUI takes such a window of the shell for a BAND (the work area ends above it: the app in
// front is laid out again in what is left, its focused field kept in view), never gives it the keys, and tells the
// shell when the app in front's focused field changes (UK_SHELL_EV_TEXT, the field's type: UIKit's
// uk_set_input_type) -- the keyboard comes with a text field and goes with it. Super+K shows or hides it by hand
// (it then stays until Super+K or its own "hide" key).
//
// A key pressed TYPES for the user: kapi_inject_key -- the server routes the keys to the app in front, as a real
// keyboard's. Characters go as the keyboard's do: ASCII, or one Latin-1 byte (e acute: 0xE9).
//
// The layouts: letters (QWERTY; AZERTY when the system's language is French), figures and punctuation, more signs
// and the accented letters; a field of numbers opens on the figures; an e-mail field has "@", a URL "/" beside the
// space; a terminal gets a row of Esc, Tab, Ctrl (held for the next key) and the arrows.
//
// Included by main.cpp after its helpers (D, F, tw, lk_*, the canvases).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef POCKETSHELL_KEYS_H
#define POCKETSHELL_KEYS_H

enum { W_KEYS = 3 };
enum { KA_TYPE = 0, KA_SHIFT, KA_LAYER, KA_BKSP, KA_ENTER, KA_HIDE, KA_SPACE, KA_CTRL, KA_SEQ };
struct KKey { int x, y, w, h, act; char cap[12]; char out[8]; };
#define KB_MAXKEYS	72
static KKey g_kk[KB_MAXKEYS];
static int g_nkk;
static Canvas g_kc;
static int g_kbStride, g_kbCapW, g_kbCapH;
static bool g_kbDirty, g_kbManual, g_kbShift, g_kbCtrl;	// (g_kbOn: declared by main.cpp, before quick settings)
static int g_kbLayer, g_kbType = -1, g_kbHot = -1, g_kbH;
static unsigned g_kbT;					// when the hot key was pressed, then typed last (its repeat)
static bool g_kbRep;
// With the text fields, or on demand only (quick settings' Keyboard tile; kept in SD:/etc/pocket/keyboard: "auto" /
// "manual"). No file: on demand -- a pocket screen with a real keyboard (a monitor, a remote desktop) is the case
// today; a device without one sets it once.
#define KEYS_FILE	"SD:/etc/pocket/keyboard"
static void kb_auto_load (void)
{
	char b[16]; int n = -1;
	void *h = kapi_open (KEYS_FILE);
	if (h != 0) { n = kapi_read (h, b, sizeof b - 1); kapi_close (h); }
	g_kbAuto = n >= 4 && b[0] == 'a' && b[1] == 'u' && b[2] == 't' && b[3] == 'o';
}
static void kb_auto_save (void)
{
	kapi_mkdir ("SD:/etc/pocket");
	kapi_save_file (KEYS_FILE, g_kbAuto ? "auto\n" : "manual\n", g_kbAuto ? 5 : 7);
}

static bool kb_french (void)	{ const char *l = uk_lang_chosen (); return l && l[0] == 'f' && l[1] == 'r'; }
static int kb_rows (void)	{ return g_kbType == UK_IN_TERMINAL ? 5 : 4; }
static int kb_height (void)
{
	int h = kb_rows () * D (44) + D (8), max = (g_sh - g_ay) * 46 / 100;	// (never half the screen: the work area's rule)
	return h > max ? max : h;
}

// One row's keys from its words (a space between two keys; "{name}" a named key), laid from x over w.
static void kb_row (const char *spec, int x, int y, int w, int h)
{
	int n = 0, wide = 0;
	for (const char *p = spec; *p; ) { while (*p == ' ') p++; if (!*p) break; n++; if (*p == '{') wide++; while (*p && *p != ' ') p++; }
	if (n == 0) return;
	// a named key is half as wide again as a letter; the space takes what is left
	bool space = strstr (spec, "{space}") != 0;
	int unit = space ? w * 2 / (2 * 10 + 1) : w * 2 / (2 * n + wide);
	int used = 0;
	if (space) { for (const char *p = spec; *p; ) { while (*p == ' ') p++; if (!*p) break; if (strncmp (p, "{space}", 7) != 0) used += *p == '{' ? unit * 3 / 2 : unit; while (*p && *p != ' ') p++; } }
	int cx = x + (space ? 0 : (w - (n * unit + wide * unit / 2)) / 2);
	for (const char *p = spec; *p && g_nkk < KB_MAXKEYS; )
	{
		while (*p == ' ') p++;
		if (!*p) break;
		const char *e = p; while (*e && *e != ' ') e++;
		KKey &k = g_kk[g_nkk++];
		memset (&k, 0, sizeof k);
		k.x = cx; k.y = y; k.h = h; k.w = unit; k.act = KA_TYPE;
		int len = (int) (e - p);
		if (*p == '{' && len > 2)
		{
			k.w = unit * 3 / 2;
			struct { const char *name; int act; const char *cap, *out; } static const N[] = {
				{ "{shift}", KA_SHIFT, "\xE2\x87\xA7", "" }, { "{bksp}", KA_BKSP, "\xE2\x8C\xAB", "\x7f" },
				{ "{enter}", KA_ENTER, "\xE2\x8F\x8E", "\n" }, { "{hide}", KA_HIDE, "\xE2\x96\xBE", "" },
				{ "{space}", KA_SPACE, "", " " }, { "{layer}", KA_LAYER, "", "" }, { "{ctrl}", KA_CTRL, "Ctrl", "" },
				{ "{esc}", KA_SEQ, "Esc", "\x1b" }, { "{tab}", KA_SEQ, "Tab", "\t" },
				{ "{left}", KA_SEQ, "\xE2\x86\x90", "\x1b[D" }, { "{right}", KA_SEQ, "\xE2\x86\x92", "\x1b[C" },
				{ "{up}", KA_SEQ, "\xE2\x86\x91", "\x1b[A" }, { "{down}", KA_SEQ, "\xE2\x86\x93", "\x1b[B" } };
			for (unsigned i = 0; i < sizeof N / sizeof N[0]; i++)
				if ((int) strlen (N[i].name) == len && strncmp (p, N[i].name, (size_t) len) == 0)
				{ k.act = N[i].act; fs_copy (k.cap, N[i].cap, sizeof k.cap); fs_copy (k.out, N[i].out, sizeof k.out); }
			if (k.act == KA_SPACE) k.w = w - used;
			if (k.act == KA_LAYER) fs_copy (k.cap, g_kbLayer == 0 ? "123" : g_kbLayer == 1 ? "#+=" : "ABC", sizeof k.cap);
		}
		else
		{
			if (len > (int) sizeof k.cap - 1) len = (int) sizeof k.cap - 1;
			memcpy (k.cap, p, (size_t) len); k.cap[len] = 0;
			// what it types: itself, or its Latin-1 byte (a two-byte UTF-8 letter of U+00A0..U+00FF)
			unsigned char c0 = (unsigned char) k.cap[0], c1 = (unsigned char) k.cap[1];
			if (len == 2 && (c0 == 0xC2 || c0 == 0xC3) && (c1 & 0xC0) == 0x80) { k.out[0] = (char) (((c0 & 3) << 6) | (c1 & 0x3F)); k.out[1] = 0; }
			else fs_copy (k.out, k.cap, sizeof k.out);
		}
		cx += k.w;
		p = e;
	}
}

static void kb_build (void)
{
	g_nkk = 0;
	int w = g_sw, kh = (g_kbH - D (8)) / kb_rows (), y = D (4);
	int max = D (760);					// (a wide screen: the keys stay a hand wide, centred)
	int x = w > max ? (w - max) / 2 : D (2);
	if (w > max) w = max; else w -= D (4);
	if (g_kbType == UK_IN_TERMINAL) { kb_row ("{esc} {tab} {ctrl} {left} {down} {up} {right}", x, y, w, kh); y += kh; }
	bool fr = kb_french ();
	if (g_kbLayer == 0)
	{
		kb_row (fr ? "a z e r t y u i o p" : "q w e r t y u i o p", x, y, w, kh); y += kh;
		kb_row (fr ? "q s d f g h j k l m" : "a s d f g h j k l", x, y, w, kh); y += kh;
		kb_row (fr ? "{shift} w x c v b n ' {bksp}" : "{shift} z x c v b n m {bksp}", x, y, w, kh); y += kh;
	}
	else if (g_kbLayer == 1)
	{
		kb_row ("1 2 3 4 5 6 7 8 9 0", x, y, w, kh); y += kh;
		kb_row ("- / : ; ( ) & @ \" '", x, y, w, kh); y += kh;
		kb_row ("+ = * ? ! _ # % {bksp}", x, y, w, kh); y += kh;
	}
	else
	{
		kb_row ("[ ] { } < > ^ ~ | \\", x, y, w, kh); y += kh;
		kb_row ("\xC3\xA9 \xC3\xA8 \xC3\xAA \xC3\xA0 \xC3\xA2 \xC3\xA7 \xC3\xB9 \xC3\xBB \xC3\xAE \xC3\xB4", x, y, w, kh); y += kh;
		kb_row ("$ \xC2\xA3 \xC2\xB0 ` \xC3\xAB \xC3\xAF \xC3\xBC \xC3\xB6 {bksp}", x, y, w, kh); y += kh;
	}
	kb_row (g_kbType == UK_IN_EMAIL ? "{layer} @ {space} . {enter} {hide}"
	      : g_kbType == UK_IN_URL ? "{layer} / {space} . {enter} {hide}" : "{layer} , {space} . {enter} {hide}", x, y, w, kh);
}

static void kb_draw (void)
{
	g_kbDirty = false;
	Canvas &cv = g_kc;
	unsigned bg = uk_mix (C_BG, 0x000000, 40);		// (the theme's: a field's face for the letters, a control's for the rest)
	cv.fillRect (0, 0, g_sw, g_kbH, bg);
	cv.fillRect (0, 0, g_sw, 1, uk_mix (bg, 0x000000, 60));
	UkFaceScope f (F (17));
	for (int i = 0; i < g_nkk; i++)
	{
		KKey &k = g_kk[i];
		bool named = k.act != KA_TYPE && k.act != KA_SPACE;
		bool lit = i == g_kbHot || (k.act == KA_SHIFT && g_kbShift) || (k.act == KA_CTRL && g_kbCtrl);
		unsigned c1 = lit ? C_ACCENT : named ? C_FACE : C_FIELD, c2 = uk_mix (c1, 0x000000, lit ? 30 : 14);
		int m = D (3);
		lk_fill (cv, k.x + m, k.y + m, k.w - 2 * m, k.h - 2 * m, D (6), c1, c2, 255);
		lk_ring (cv, k.x + m, k.y + m, k.w - 2 * m, k.h - 2 * m, D (6), 16, uk_mix (c2, 0x000000, 50), 255);
		char cap[12];
		fs_copy (cap, k.cap, sizeof cap);
		if (k.act == KA_TYPE && g_kbShift && cap[1] == 0 && cap[0] >= 'a' && cap[0] <= 'z') cap[0] = (char) (cap[0] - 32);
		unsigned ink = lit ? C_SEL_TEXT : named ? C_TEXT : C_FIELD_TEXT;
		if (named && cap[0] > 0 && cap[1] && cap[2] && (unsigned char) cap[0] < 0x80)	// (a word: smaller)
		{ UkFaceScope f2 (F (12)); uk_text (cv, k.x + (k.w - tw (cap)) / 2, k.y + (k.h - uk_fh ()) / 2, cap, ink); }
		else uk_text (cv, k.x + (k.w - tw (cap)) / 2, k.y + (k.h - uk_fh ()) / 2, cap, ink);
	}
	present (W_KEYS);
}

static void kb_place (void)				// on the screen's bottom edge, or parked off it
{
	uk_win_select (W_KEYS);
	if (g_kbOn) { uk_win_resize (g_sw, g_kbH); uk_win_move (0, g_sh - g_kbH); uk_win_raise (0); }
	else uk_win_move (-g_sw - 50, g_sh / 2);
	uk_win_select (0);
}
static void kb_show (bool on)
{
	if (on == g_kbOn && (!on || g_kbH == kb_height ())) { if (on) { kb_build (); g_kbDirty = true; } return; }
	g_kbOn = on; g_kbHot = -1; g_kbShift = false;
	if (g_kbCtrl) { g_kbCtrl = false; kapi_inject_modifiers (0); }
	if (on)
	{
		g_kbH = kb_height ();
		if (g_sw > g_kbCapW || g_kbH > g_kbCapH)	// (a bigger screen, a taller keyboard: its canvas grown)
		{
			int w = g_sw > g_kbCapW ? g_sw : g_kbCapW, h = g_kbH > g_kbCapH ? g_kbH : g_kbCapH, st = w;
			uk_win_select (W_KEYS);
			unsigned *fb = uk_win_resize2 (w, h, &st);
			uk_win_select (0);
			if (fb) { g_kc.adopt (fb, w, h, st); g_kbStride = st; g_kbCapW = w; g_kbCapH = h; }
		}
		g_kc.adopt (g_kc.px, g_sw, g_kbH, g_kbStride);
		kb_build ();
		g_kbDirty = true;
	}
	kb_place ();
}
// The app in front's focused field: its type (UK_IN_*), -1 none (PocketUI's UK_SHELL_EV_TEXT).
static int g_kbField = -1;				// the app in front's focused field's type, -1 none
static void kb_hint (int type)
{
	g_kbField = type;
	if (type >= 0 && !g_kbAuto && !g_kbOn) return;	// (on demand: Super+K, the tile)
	if (type >= 0)
	{
		bool other = type != g_kbType;
		g_kbType = type;
		if (other || !g_kbOn) g_kbLayer = type == UK_IN_NUMBER || type == UK_IN_DECIMAL ? 1 : 0;
		kb_show (true);
	}
	else if (!g_kbManual) kb_show (false);
}
static void kb_toggle (void)				// Super+K
{
	g_kbManual = !g_kbOn;
	if (!g_kbOn) { g_kbType = g_kbField >= 0 ? g_kbField : UK_IN_TEXT; g_kbLayer = g_kbType == UK_IN_NUMBER || g_kbType == UK_IN_DECIMAL ? 1 : 0; }
	kb_show (!g_kbOn);
}
static void kb_screen (void)				// the screen's size or the scale changed
{
	if (!g_kbOn) { kb_place (); return; }
	g_kbOn = false;
	kb_show (true);
}

static void kb_type (int i)
{
	KKey &k = g_kk[i];
	switch (k.act)
	{
	case KA_SHIFT: g_kbShift = !g_kbShift; break;
	case KA_LAYER: g_kbLayer = (g_kbLayer + 1) % 3; g_kbHot = -1; kb_build (); break;
	case KA_HIDE: g_kbManual = false; kb_show (false); return;
	case KA_CTRL: g_kbCtrl = !g_kbCtrl; kapi_inject_modifiers (g_kbCtrl ? MOD_CTRL : 0); break;
	default:
		{
			char o[8];
			fs_copy (o, k.out, sizeof o);
			if (k.act == KA_TYPE && o[1] == 0 && o[0] >= 'a' && o[0] <= 'z')
			{
				if (g_kbCtrl) o[0] = (char) (o[0] - 96);			// (Ctrl+C: 3, as the keyboard's)
				else if (g_kbShift) o[0] = (char) (o[0] - 32);
			}
			if (o[0]) kapi_inject_key (o);
			if (k.act == KA_TYPE || k.act == KA_SPACE) g_kbShift = false;
			if (g_kbCtrl && k.act != KA_SEQ) { g_kbCtrl = false; kapi_inject_modifiers (0); }
		}
	}
	g_kbDirty = true;
}
static void kb_ptr (int ev, int x, int y, int changed)
{
	if (!g_kbOn) return;
	if (ev == GUI_EVENT_PTR_DOWN && (changed & 1))
	{
		g_kbHot = -1;
		for (int i = 0; i < g_nkk; i++) if (x >= g_kk[i].x && x < g_kk[i].x + g_kk[i].w && y >= g_kk[i].y && y < g_kk[i].y + g_kk[i].h) g_kbHot = i;
		if (g_kbHot >= 0) { int h = g_kbHot; g_kbT = kapi_get_ticks (); g_kbRep = false; kb_type (h); }
		g_kbDirty = true;
	}
	else if ((ev == GUI_EVENT_PTR_UP && (changed & 1)) || ev == GUI_EVENT_PTR_LEAVE)
	{
		if (g_kbHot >= 0) { g_kbHot = -1; g_kbDirty = true; }
	}
}
// A key held types again: after 0.4 s, then every 0.06 s (the erase key, the arrows, a letter).
static void kb_tick (void)
{
	if (!g_kbOn || g_kbHot < 0 || g_kbHot >= g_nkk) return;
	int a = g_kk[g_kbHot].act;
	if (a != KA_TYPE && a != KA_BKSP && a != KA_SPACE && a != KA_SEQ) return;
	unsigned now = kapi_get_ticks ();
	if ((int) (now - g_kbT) < (g_kbRep ? 6 : 40)) return;
	g_kbT = now; g_kbRep = true;
	int h = g_kbHot;
	kb_type (h);
}

// Made with the shell's other windows (main): opaque, topmost, parked off the screen. -> false: no window.
static bool kb_make (void)
{
	unsigned *kb = 0;
	g_kbH = kb_height ();
	if (uk_win_new (0, g_ay, g_sw, g_kbH, "pocketshell keyboard", WIN_FLAG_TOPMOST | WIN_FLAG_BORDERLESS | WIN_FLAG_SYSTEM, &kb) != W_KEYS) return false;
	g_kbStride = g_sw; g_kbCapW = g_sw; g_kbCapH = g_kbH;
	g_kc.adopt (kb, g_sw, g_kbH, g_kbStride);
	g_kc.fillRect (0, 0, g_sw, g_kbH, C_BG);
	kb_auto_load ();
	return true;
}

#endif
