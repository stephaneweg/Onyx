//
// buttons.h -- pocketshell: a handheld's BUTTONS (the Waveshare PocketTerm35: a D-pad, A / B / X / Y, L / R, Start /
// Select; docs/COMPACT-SHELL-STUDY.md section 20.4, docs/compact-shell/mockups/pocketterm/button-map.png). There is no
// pointer on such a device: every button TYPES A KEY, so that the shell, the menu bar's Menu entry and every app are
// driven the same way by the pad, by a keyboard and by a remote client (Onyx Remote, the PocketTerm Simulator: their
// buttons arrive as these very keys).
//
// SD:/etc/pocketui.ini, section [buttons] -- one line a button, what it types (the defaults):
//
//     up = Up        down = Down      left = Left      right = Right      the D-pad: the arrows
//     a = Enter                       open, confirm, activate the focused control
//     b = Esc                         back, cancel, close a menu or a dialog
//     x = Delete                      the second action: close the chosen app (the switcher, the Running pills), remove
//     y = Shift+F10                   the focused control's context menu
//     l = Ctrl+PageUp                 the category / tab / section before (in the Menu: the menu before)
//     r = Ctrl+PageDown               ... after
//     start = none    select = none   NOT the shell's: Start and Select are the games' (the user, 2026-10-11) -- they
//                                     reach the program in front as pad buttons, untouched; a line may bind them
//
// The two SYSTEM actions are keys of the keyboard under the buttons: the CMD key (Super) pressed and released is the
// MENU -- the top band's column: the app's menus, then Home, Open apps, Quit --, held half a second it is Home; F10 is
// the menu too, Alt+Tab the open apps, Ctrl+Esc Home (PocketUI, the menu bar: not this file).
//
// A key: Up Down Left Right Enter Esc Tab Space Backspace Delete Insert Home End PageUp PageDown F1..F12, a character,
// with Ctrl+ Shift+ Alt+ Super+ before it; "Super" alone is the Super key pressed and released; "none": nothing.
//
// Two ways a device's buttons reach Onyx, both served by this file:
//   * as a GAMEPAD (USB HID, the kernel's kapi_pad_state through gamepad.h and SD:/etc/gamepad.ini): this shell reads
//     the pads and types each button's key (kapi_inject_key / kapi_inject_modifiers), the D-pad repeating while held.
//     NOT while the program in front reads the pad itself -- the rule is the console shell's (consolehome): its
//     app.txt's category is Games or Emulators, or its app.txt says "pad = own" (a program of another category that
//     reads the pad): there every button is the program's, none is typed (no doubled
//     Enter, no stolen Esc);
//   * as KEYS of the keyboard: nothing to do when they are the keys above; when the device sends OTHER keys, a line
//     "<button>.key = <key>" says which key IS that button (e.g. "a.key = F5"): the shell takes that key before the
//     apps (uk_shell_keys) and types the button's key instead.
//
// Part of main.cpp (one unit).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//

enum { BT_UP, BT_DOWN, BT_LEFT, BT_RIGHT, BT_A, BT_B, BT_X, BT_Y, BT_L, BT_R, BT_START, BT_SELECT, BT_N };
struct Chord { unsigned mods; int code; };		// code: a key's (KEY_*, a character), UK_SHELL_KEY_SUPER, 0 none
static const char *const BT_NAME[BT_N] = { "up", "down", "left", "right", "a", "b", "x", "y", "l", "r", "start", "select" };
static Chord g_btDo[BT_N] = {
	{ 0, KEY_UP }, { 0, KEY_DOWN }, { 0, KEY_LEFT }, { 0, KEY_RIGHT }, { 0, KEY_ENTER }, { 0, 0x1b }, { 0, KEY_DEL },
	{ MOD_SHIFT, KEY_F1 + 9 }, { MOD_CTRL, KEY_PGUP }, { MOD_CTRL, KEY_PGDN }, { 0, 0 }, { 0, 0 } };	// (Start, Select: none)
static Chord g_btKey[BT_N];				// "<button>.key": the keyboard's key that IS the button (code 0: none)
static const unsigned BT_PAD[BT_N] = { PAD_UP, PAD_DOWN, PAD_LEFT, PAD_RIGHT, PAD_A, PAD_B, PAD_X, PAD_Y, PAD_L, PAD_R, PAD_START, PAD_SELECT };

static bool bt_ieq (const char *a, const char *b) { for (;; a++, b++) { if (lx_low (*a) != lx_low (*b)) return false; if (!*a) return true; } }

// "Ctrl+PageUp", "Super", "x" -> a chord (false: not understood).
static bool bt_parse (const char *s, Chord *c)
{
	static const struct { const char *n; int code; } K[] = {
		{ "up", KEY_UP }, { "down", KEY_DOWN }, { "left", KEY_LEFT }, { "right", KEY_RIGHT }, { "enter", KEY_ENTER },
		{ "return", KEY_ENTER }, { "esc", 0x1b }, { "escape", 0x1b }, { "tab", '\t' }, { "space", ' ' },
		{ "backspace", KEY_BACKSPACE }, { "delete", KEY_DEL }, { "del", KEY_DEL }, { "home", KEY_HOME },
		{ "end", KEY_END }, { "pageup", KEY_PGUP }, { "pagedown", KEY_PGDN }, { "pgup", KEY_PGUP }, { "pgdn", KEY_PGDN } };
	c->mods = 0; c->code = 0;
	for (;;)
	{
		char w[16]; int n = 0;
		while (*s == ' ' || *s == '\t') s++;
		while (s[n] && s[n] != '+' && s[n] != ' ' && s[n] != '\t' && s[n] != '\r' && n < 15) { w[n] = s[n]; n++; }
		w[n] = 0;
		if (n == 0 && *s == '+') { w[0] = '+'; w[1] = 0; n = 1; }		// (the key "+")
		const char *e = s + n;
		while (*e == ' ' || *e == '\t') e++;
		if (*e == '+' && e[1] != 0 && n > 0)				// a modifier before the key
		{
			if (bt_ieq (w, "ctrl")) c->mods |= MOD_CTRL;
			else if (bt_ieq (w, "shift")) c->mods |= MOD_SHIFT;
			else if (bt_ieq (w, "alt")) c->mods |= MOD_ALT;
			else if (bt_ieq (w, "super") || bt_ieq (w, "cmd")) c->mods |= UK_SHELL_MOD_SUPER;
			else return false;
			s = e + 1;
			continue;
		}
		if (bt_ieq (w, "none") || n == 0) { c->code = 0; return n != 0; }
		if (bt_ieq (w, "super") || bt_ieq (w, "cmd")) { c->code = UK_SHELL_KEY_SUPER; return true; }
		for (unsigned i = 0; i < sizeof K / sizeof K[0]; i++) if (bt_ieq (w, K[i].n)) { c->code = K[i].code; return true; }
		if ((w[0] == 'f' || w[0] == 'F') && w[1] >= '1' && w[1] <= '9' && n <= 3)
		{
			int f = w[1] - '0';
			if (w[2] >= '0' && w[2] <= '9') f = f * 10 + (w[2] - '0');
			if (f >= 1 && f <= 12) { c->code = KEY_F1 + f - 1; return true; }
		}
		if (n == 1) { c->code = (unsigned char) lx_low (w[0]); return true; }
		return false;
	}
}

// SD:/etc/pocketui.ini's [buttons] read (the defaults kept for what it does not say).
static void buttons_load (void)
{
	static char b[4097];
	void *f = kapi_open ("SD:/etc/pocketui.ini");
	if (f == 0) return;
	int n = kapi_read (f, b, sizeof b - 1);
	kapi_close (f);
	if (n <= 0) return;
	b[n] = 0;
	bool in = false;
	for (char *p = b; *p; )
	{
		char *l = p; while (*p && *p != '\n') p++;
		if (*p) *p++ = 0;
		while (*l == ' ' || *l == '\t') l++;
		if (*l == '#' || *l == ';' || *l == 0 || *l == '\r') continue;
		if (*l == '[') { in = strncmp (l, "[buttons]", 9) == 0; continue; }
		if (!in) continue;
		char *eq = strchr (l, '=');
		if (eq == 0) continue;
		char *ke = eq; while (ke > l && (ke[-1] == ' ' || ke[-1] == '\t')) ke--;
		*ke = 0;
		char *v = eq + 1;
		char *h = strchr (v, '#'); if (h != 0 && h > v + 1 && (h[-1] == ' ' || h[-1] == '\t')) *h = 0;	// (a comment after the value)
		bool isKey = false;
		int kl = (int) strlen (l);
		if (kl > 4 && bt_ieq (l + kl - 4, ".key")) { isKey = true; l[kl - 4] = 0; }
		for (int i = 0; i < BT_N; i++)
			if (bt_ieq (l, BT_NAME[i]))
			{
				Chord c;
				if (!bt_parse (v, &c)) { ax_puts ("pocketshell: [buttons] "); ax_puts (BT_NAME[i]); ax_puts (": not a key\n"); break; }
				if (isKey) g_btKey[i] = c; else g_btDo[i] = c;
			}
	}
}

// The key typed: the modifiers set, the key's sequence (as a keyboard's and the remote clients': remotekeys.h), the
// modifiers released. "Super" alone: pressed and released.
static void bt_type (const Chord &c)
{
	if (c.code == 0) return;
	if (c.code == UK_SHELL_KEY_SUPER) { kapi_inject_modifiers (UK_SHELL_MOD_SUPER); kapi_inject_modifiers (0); return; }
	char one[2] = { 0, 0 }, fn[8];
	const char *s = one;
	switch (c.code)
	{
	case KEY_UP: s = "\x1b[A"; break;		case KEY_DOWN: s = "\x1b[B"; break;
	case KEY_RIGHT: s = "\x1b[C"; break;		case KEY_LEFT: s = "\x1b[D"; break;
	case KEY_ENTER: s = "\n"; break;		case KEY_DEL: s = "\x1b[3~"; break;
	case KEY_HOME: s = "\x1b[1~"; break;
	case KEY_END: s = "\x1b[4~"; break;		case KEY_PGUP: s = "\x1b[5~"; break;
	case KEY_PGDN: s = "\x1b[6~"; break;
	default:
		if (c.code >= KEY_F1 && c.code <= KEY_F1 + 11)
		{
			static const char *const F[12] = { "\x1b[[A", "\x1b[[B", "\x1b[[C", "\x1b[[D", "\x1b[[E", "\x1b[17~", "\x1b[18~", "\x1b[19~",
							   "\x1b[20~", "\x1b[21~", "\x1b[23~", "\x1b[24~" };
			strcpy (fn, F[c.code - KEY_F1]); s = fn;
		}
		else one[0] = (char) c.code;
	}
	if (c.mods) kapi_inject_modifiers (c.mods);
	kapi_inject_key (s);
	if (c.mods) kapi_inject_modifiers (0);
}

static void button_do (int b) { if (b >= 0 && b < BT_N) bt_type (g_btDo[b]); }

// The keys that ARE buttons ("<button>.key"), added to the shell's system keys -> how many now.
static int buttons_keys (int *keys, int n, int cap)
{
	for (int i = 0; i < BT_N && n < cap; i++)
	{
		const Chord &k = g_btKey[i];
		if (k.code == 0 || k.code == UK_SHELL_KEY_SUPER) continue;
		if (k.code == g_btDo[i].code && k.mods == g_btDo[i].mods) continue;	// (the key it types already)
		keys[n++] = UK_SHELL_KEY (k.mods, k.code);
	}
	return n;
}
// A system key that is a button -> true: its key typed instead.
static bool buttons_key (unsigned mods, int code)
{
	for (int i = 0; i < BT_N; i++)
	{
		const Chord &k = g_btKey[i];
		if (k.code == 0 || k.code != code || k.mods != (mods & 15)) continue;
		if (k.code == g_btDo[i].code && k.mods == g_btDo[i].mods) return false;
		button_do (i);
		return true;
	}
	return false;
}

// The pads' buttons, all of them (kapi_pad_state whoever has the keys -- as the console's shell reads them).
static unsigned bt_pads (void)
{
	unsigned b = 0;
	for (int i = 0; i < PAD_MAX; i++)
	{
		struct kapi_pad p;
		struct pad_map m;
		if (!kapi_pad_state (i, &p)) continue;
		pad_map_for (&p, &m);
		int lx = 0, ly = 0;
		b |= pad_apply (&p, &m, &lx, &ly, 0, 0);
		if (lx < -500) b |= PAD_LEFT; else if (lx > 500) b |= PAD_RIGHT;	// (a stick as the D-pad)
		if (ly < -500) b |= PAD_UP; else if (ly > 500) b |= PAD_DOWN;
	}
	return b;
}

static bool front_is_game (void);			// (main.cpp: the app in front reads the pad itself)

// Each turn of the shell's loop: the buttons pressed since the last one typed; the D-pad repeats (0.3 s, then 12 a second).
static void buttons_poll (void)
{
	static unsigned last, since, rep;
	unsigned b = bt_pads (), now = kapi_get_ticks (), press = b & ~last;
	last = b;
	static int said = -1;					// (the Pi's kmsg: who has the pad's buttons, when it changes)
	int game = front_is_game () ? 1 : 0;
	if (b != 0 && game != said)
	{
		said = game;
		ax_puts (game ? "pocketshell: the pad is the program's in front (it reads it: no key typed)\n" : "pocketshell: the pad's buttons type keys\n");
	}
	if (game) return;					// a game's pad is its own: nothing typed, nothing taken
	if (press & PAD_HOME)					// (a pad's middle button: Home -- Ctrl+Esc, the shell's key for it)
	{ Chord h = { MOD_CTRL, 0x1b }; bt_type (h); }
	for (int i = 0; i < BT_N; i++) if (press & BT_PAD[i]) button_do (i);
	unsigned dir = b & (PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT);
	if (press & dir) { since = now; rep = now; }
	else if (dir && (int) (now - since) >= 30 && (int) (now - rep) >= 8)
	{
		rep = now;
		for (int i = BT_UP; i <= BT_RIGHT; i++) if (dir & BT_PAD[i]) button_do (i);
	}
}
