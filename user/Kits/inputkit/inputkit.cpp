//
// inputkit.cpp -- InputKit (inputkit.h): the actions, their bindings (SD:/etc/input.ini), the pads' events and the
// state, built on gamepad.h (the pads' layouts: SD:/etc/gamepad.ini). Through AppKit only; freestanding.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#include "inputkit/inputkit.h"
#include "gamepad.h"
#if defined (__aarch64__) && defined (ONYX_LIB_BUILD)
#include "lib.h"
#endif

#define IN_SOURCES	4			// the keys an action may have
#define IN_INI		"SD:/etc/input.ini"

namespace {

struct KeySrc { int code; unsigned mods; };
KeySrc s_key[IN_ACTIONS][IN_SOURCES];
int s_nkey[IN_ACTIONS];
unsigned s_pad[IN_ACTIONS];			// the PAD_* bits of each action
bool s_loaded, s_claimed;

const char *const NAME[IN_ACTIONS] = { "up", "down", "left", "right", "accept", "back", "alt1", "alt2", "prev", "next",
				       "menu", "home", "start", "select", "l2", "r2" };

char low (char c) { return c >= 'A' && c <= 'Z' ? (char) (c + 32) : c; }
bool weq (const char *a, int n, const char *b)		// the n characters at a are the word b (case ignored)
{
	int i = 0;
	for (; i < n && b[i]; i++) if (low (a[i]) != b[i]) return false;
	return i == n && b[i] == 0;
}

const struct { const char *w; int code; } KEYS[] = {
	{ "up", KEY_UP }, { "down", KEY_DOWN }, { "left", KEY_LEFT }, { "right", KEY_RIGHT }, { "enter", KEY_ENTER },
	{ "return", KEY_ENTER }, { "esc", 27 }, { "escape", 27 }, { "tab", KEY_TAB }, { "space", ' ' },
	{ "backspace", KEY_BACKSPACE }, { "delete", KEY_DEL }, { "del", KEY_DEL }, { "home", KEY_HOME }, { "end", KEY_END },
	{ "pageup", KEY_PGUP }, { "pagedown", KEY_PGDN }, { "pgup", KEY_PGUP }, { "pgdn", KEY_PGDN } };

// "Ctrl+PageUp" (n characters) -> a key source; false: not a key.
bool parse_key (const char *s, int n, KeySrc *k)
{
	k->code = 0; k->mods = 0;
	for (;;)
	{
		while (n > 0 && (*s == ' ' || *s == '\t')) { s++; n--; }
		while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r')) n--;
		int w = 0;
		while (w < n && s[w] != '+') w++;
		if (w == 0 && n > 0) w = 1;				// (the key "+")
		if (w < n && n - w > 1)					// a modifier, then more
		{
			if (weq (s, w, "ctrl")) k->mods |= MOD_CTRL;
			else if (weq (s, w, "shift")) k->mods |= MOD_SHIFT;
			else if (weq (s, w, "alt")) k->mods |= MOD_ALT;
			else return false;
			s += w + 1; n -= w + 1;
			continue;
		}
		if (n <= 0) return false;
		for (unsigned i = 0; i < sizeof KEYS / sizeof KEYS[0]; i++) if (weq (s, n, KEYS[i].w)) { k->code = KEYS[i].code; return true; }
		if (low (s[0]) == 'f' && n >= 2 && n <= 3 && s[1] >= '1' && s[1] <= '9')
		{
			int f = s[1] - '0';
			if (n == 3) { if (s[2] < '0' || s[2] > '9') return false; f = f * 10 + (s[2] - '0'); }
			if (f >= 1 && f <= 12) { k->code = KEY_F1 + f - 1; return true; }
		}
		if (n == 1 && s[0] > ' ' && (unsigned char) s[0] < 127) { k->code = low (s[0]); return true; }
		return false;
	}
}

void defaults ()
{
	static const struct { int a; int code; unsigned mods; unsigned pad; } D[IN_ACTIONS] = {
		{ IN_UP, KEY_UP, 0, PAD_UP }, { IN_DOWN, KEY_DOWN, 0, PAD_DOWN }, { IN_LEFT, KEY_LEFT, 0, PAD_LEFT },
		{ IN_RIGHT, KEY_RIGHT, 0, PAD_RIGHT }, { IN_ACCEPT, KEY_ENTER, 0, PAD_A }, { IN_BACK, 27, 0, PAD_B },
		{ IN_ALT1, KEY_DEL, 0, PAD_X }, { IN_ALT2, KEY_F1 + 9, MOD_SHIFT, PAD_Y },
		{ IN_PREV, KEY_PGUP, MOD_CTRL, PAD_L }, { IN_NEXT, KEY_PGDN, MOD_CTRL, PAD_R },	// (+ Ctrl+Shift+Tab / Ctrl+Tab below)
		{ IN_MENU, KEY_F1 + 9, 0, 0 }, { IN_HOME, 27, MOD_CTRL, PAD_HOME },
		{ IN_START, 0, 0, PAD_START }, { IN_SELECT, 0, 0, PAD_SELECT }, { IN_L2, 0, 0, PAD_L2 }, { IN_R2, 0, 0, PAD_R2 } };
	for (int i = 0; i < IN_ACTIONS; i++)
	{
		int a = D[i].a;
		s_nkey[a] = D[i].code ? 1 : 0; s_key[a][0].code = D[i].code; s_key[a][0].mods = D[i].mods;
		s_pad[a] = D[i].pad;
	}
	s_key[IN_PREV][1].code = KEY_TAB; s_key[IN_PREV][1].mods = MOD_CTRL | MOD_SHIFT; s_nkey[IN_PREV] = 2;
	s_key[IN_NEXT][1].code = KEY_TAB; s_key[IN_NEXT][1].mods = MOD_CTRL; s_nkey[IN_NEXT] = 2;
}

// The file's text: [keys] and [pad], "action = source, source".
void parse (const char *t)
{
	int sec = 0;						// 1 keys, 2 pad
	for (const char *p = t; *p; )
	{
		const char *l = p; while (*p && *p != '\n') p++;
		const char *e = p; if (*p) p++;
		while (l < e && (*l == ' ' || *l == '\t')) l++;
		const char *c = l; while (c < e && *c != '#' && *c != ';') c++;		// (a comment)
		e = c;
		if (l >= e) continue;
		if (*l == '[') { sec = weq (l + 1, 4, "keys") ? 1 : weq (l + 1, 3, "pad") ? 2 : 0; continue; }
		const char *eq = l; while (eq < e && *eq != '=') eq++;
		if (eq >= e || sec == 0) continue;
		int kn = (int) (eq - l); while (kn > 0 && (l[kn - 1] == ' ' || l[kn - 1] == '\t')) kn--;
		int a = -1;
		for (int i = 0; i < IN_ACTIONS; i++) if (weq (l, kn, NAME[i])) a = i;
		if (a < 0) continue;
		if (sec == 1) s_nkey[a] = 0; else s_pad[a] = 0;
		for (const char *v = eq + 1; v < e; )
		{
			const char *w = v; while (v < e && *v != ',') v++;
			const char *we = v; if (v < e) v++;
			while (w < we && (*w == ' ' || *w == '\t')) w++;
			while (we > w && (we[-1] == ' ' || we[-1] == '\t' || we[-1] == '\r')) we--;
			int n = (int) (we - w);
			if (n <= 0 || weq (w, n, "none")) continue;
			if (sec == 1)
			{
				KeySrc k;
				if (s_nkey[a] < IN_SOURCES && parse_key (w, n, &k)) s_key[a][s_nkey[a]++] = k;
			}
			else for (int b = 0; b < PAD_NBUTTONS; b++) if (weq (w, n, pad_names[b])) s_pad[a] |= 1u << b;
		}
	}
}

void ready () { if (!s_loaded) in_load (); }

// The pads' buttons as events: what each pad held at the last call, the directions' repeat.
unsigned s_last[PAD_MAX];
unsigned s_since[PAD_MAX][4], s_rep[PAD_MAX][4];

unsigned pad_now (int p, struct pad_input *in)		// its PAD_* buttons, the left stick as the D-pad too
{
	if (!pad_read (p, in) || !in->focus) return 0;
	unsigned b = in->buttons;
	if (in->lx < -500) b |= PAD_LEFT; else if (in->lx > 500) b |= PAD_RIGHT;
	if (in->ly < -500) b |= PAD_UP; else if (in->ly > 500) b |= PAD_DOWN;
	return b;
}

int cat (char *o, int k, int cap, const char *s) { while (*s && k < cap - 1) o[k++] = *s++; o[k] = 0; return k; }

} // namespace

extern "C" int in_load_text (const char *text)
{
	s_loaded = true;
	defaults ();
	if (text != 0) parse (text);
	return 1;
}

extern "C" int in_load (void)
{
	s_loaded = true;
	defaults ();
	pad_config_reload ();					// (gamepad.h: the pads' layouts, SD:/etc/gamepad.ini)
	static char b[4097];
	void *f = kapi_open (IN_INI);
	if (f == 0) return 0;
	int n = kapi_read (f, b, sizeof b - 1);
	kapi_close (f);
	if (n <= 0) return 0;
	b[n] = 0;
	parse (b);
	return 1;
}

extern "C" int in_key_action (int key, unsigned mods)
{
	ready ();
	mods &= MOD_CTRL | MOD_SHIFT | MOD_ALT;
	if (key >= 'A' && key <= 'Z') key += 32;
	for (int a = 0; a < IN_ACTIONS; a++)
		for (int i = 0; i < s_nkey[a]; i++) if (s_key[a][i].code == key && s_key[a][i].mods == mods) return a;
	return -1;
}

extern "C" int in_key_event (int key, unsigned mods, struct in_event *out)
{
	int a = in_key_action (key, mods);
	if (a < 0 || out == 0) return 0;
	out->action = a; out->kind = IN_PRESS; out->source = IN_SRC_KEY; out->code = key; out->mods = (int) (mods & 7); out->player = 0;
	return 1;
}

extern "C" int in_poll (struct in_event *out, int max)
{
	ready ();
	int n = 0;
	unsigned now = kapi_get_ticks ();
	for (int p = 0; p < PAD_MAX; p++)
	{
		struct pad_input in;
		unsigned b = s_claimed ? 0 : pad_now (p, &in), was = s_last[p];
		s_last[p] = b;
		if (b == 0 && was == 0) continue;
		for (int a = 0; a < IN_ACTIONS; a++)
		{
			unsigned m = s_pad[a];
			if (m == 0) continue;
			bool on = (b & m) != 0, old = (was & m) != 0;
			int kind = 0;
			if (on && !old) { kind = IN_PRESS; if (a <= IN_RIGHT) { s_since[p][a] = now; s_rep[p][a] = now; } }
			else if (!on && old) kind = IN_RELEASE;
			else if (on && a <= IN_RIGHT && (int) (now - s_since[p][a]) >= 30 && (int) (now - s_rep[p][a]) >= 8) { kind = IN_REPEAT; s_rep[p][a] = now; }
			if (kind == 0 || out == 0 || n >= max) continue;
			struct in_event &e = out[n++];
			e.action = a; e.kind = kind; e.source = IN_SRC_PAD; e.code = (int) (m & (on ? b : was)); e.mods = 0; e.player = p;
		}
	}
	return n;
}

extern "C" void in_claim (int on) { s_claimed = on != 0; }
extern "C" int in_claimed (void) { return s_claimed ? 1 : 0; }

extern "C" int in_state (int player, struct in_state *out)
{
	ready ();
	s_claimed = true;
	if (out == 0 || player < 0 || player >= PAD_MAX) return 0;
	out->held = 0; out->lx = out->ly = out->rx = out->ry = 0; out->connected = 0; out->source = IN_SRC_NONE;
	struct pad_input in;
	if (pad_read (player, &in))
	{
		out->connected = 1;
		out->lx = in.lx; out->ly = in.ly; out->rx = in.rx; out->ry = in.ry;
		for (int a = 0; a < IN_ACTIONS; a++) if (in.buttons & s_pad[a]) { out->held |= IN_BIT (a); out->source = IN_SRC_PAD; }
	}
	if (player == 0)					// the keyboard: the keys held (without modifiers)
	{
		out->connected = 1;
		for (int a = 0; a < IN_ACTIONS; a++)
			for (int i = 0; i < s_nkey[a]; i++)
				if (s_key[a][i].mods == 0 && kapi_key_held (s_key[a][i].code)) { out->held |= IN_BIT (a); if (out->source == IN_SRC_NONE) out->source = IN_SRC_KEY; }
	}
	return out->connected;
}

extern "C" const char *in_action_name (int action) { return action >= 0 && action < IN_ACTIONS ? NAME[action] : ""; }

extern "C" unsigned in_pad_mask (int action) { ready (); return action >= 0 && action < IN_ACTIONS ? s_pad[action] : 0; }

extern "C" int in_binding (int action, int source, char *out, int cap)
{
	ready ();
	if (out == 0 || cap < 2 || action < 0 || action >= IN_ACTIONS) return 0;
	out[0] = 0;
	int k = 0;
	if (source == IN_SRC_KEY)
	{
		if (s_nkey[action] == 0) return 0;
		const KeySrc &s = s_key[action][0];
		if (s.mods & MOD_CTRL) k = cat (out, k, cap, "Ctrl+");
		if (s.mods & MOD_SHIFT) k = cat (out, k, cap, "Shift+");
		if (s.mods & MOD_ALT) k = cat (out, k, cap, "Alt+");
		static const struct { int code; const char *w; } W[] = { { KEY_UP, "Up" }, { KEY_DOWN, "Down" }, { KEY_LEFT, "Left" },
			{ KEY_RIGHT, "Right" }, { KEY_ENTER, "Enter" }, { 27, "Esc" }, { KEY_TAB, "Tab" }, { ' ', "Space" },
			{ KEY_BACKSPACE, "Backspace" }, { KEY_DEL, "Delete" }, { KEY_HOME, "Home" }, { KEY_END, "End" },
			{ KEY_PGUP, "PageUp" }, { KEY_PGDN, "PageDown" } };
		for (unsigned i = 0; i < sizeof W / sizeof W[0]; i++) if (W[i].code == s.code) { cat (out, k, cap, W[i].w); return 1; }
		if (s.code >= KEY_F1 && s.code <= KEY_F1 + 11)
		{
			int f = s.code - KEY_F1 + 1;
			char w[4] = { 'F', (char) (f >= 10 ? '1' : '0' + f), (char) (f >= 10 ? '0' + f - 10 : 0), 0 };
			cat (out, k, cap, w); return 1;
		}
		char one[2] = { (char) s.code, 0 };
		cat (out, k, cap, one);
		return 1;
	}
	if (source == IN_SRC_PAD)
	{
		static const char *const P[PAD_NBUTTONS] = { "D-pad up", "D-pad down", "D-pad left", "D-pad right", "A", "B", "X", "Y", "L", "R",
							     "L2", "R2", "Select", "Start", "L3", "R3", "Home" };
		for (int b = 0; b < PAD_NBUTTONS; b++) if (s_pad[action] & (1u << b)) { cat (out, 0, cap, P[b]); return 1; }
	}
	return 0;
}

extern "C" int in_app_owns_pad (const char *app)
{
	char path[96]; int k = 0;
	if (app != 0 && app[0]) { k = cat (path, k, sizeof path, "SD:/apps/"); k = cat (path, k, sizeof path, app); k = cat (path, k, sizeof path, ".app/"); }
	else { k = kapi_app_dir (path, sizeof path - 10); if (k <= 0) return 0; path[k] = 0; }
	cat (path, k, sizeof path, "app.txt");
	static char b[2049];
	void *f = kapi_open (path);
	if (f == 0) return 0;
	int n = kapi_read (f, b, sizeof b - 1);
	kapi_close (f);
	if (n <= 0) return 0;
	b[n] = 0;
	for (const char *p = b; *p; )
	{
		const char *l = p; while (*p && *p != '\n') p++;
		const char *e = p; if (*p) p++;
		while (l < e && (*l == ' ' || *l == '\t')) l++;
		const char *eq = l; while (eq < e && *eq != '=') eq++;
		if (eq >= e) continue;
		int kn = (int) (eq - l); while (kn > 0 && (l[kn - 1] == ' ' || l[kn - 1] == '\t')) kn--;
		const char *v = eq + 1; while (v < e && (*v == ' ' || *v == '\t')) v++;
		while (e > v && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) e--;
		int vn = (int) (e - v);
		if (weq (l, kn, "category") && (weq (v, vn, "games") || weq (v, vn, "emulators"))) return 1;
		if (weq (l, kn, "pad") && weq (v, vn, "own")) return 1;
	}
	return 0;
}

#if defined (__aarch64__) && defined (ONYX_LIB_BUILD)
// (the library's table: its init -- the library runtime's, Runtime/librt.cpp)
extern "C" int onyx_lib_init (const TLibImports *imp);
extern "C" int in_lib_init (const TLibImports *imp)
{
	return onyx_lib_init (imp) < 0 ? -1 : 0;
}
#endif
