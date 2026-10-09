//
// gamepad.h -- USB gamepads for apps (header-only, C and C++): the raw pad state from the
// kernel (kapi_pad_state, ABI v50: Circle's drivers) turned into one button mask that is the
// same whatever the pad, through the global mapping SD:/etc/gamepad.ini.
//
//   unsigned b = pad_buttons (-1);          // every pad OR'ed; or 0..3 for one pad
//   if (b & PAD_LEFT) ...; if (b & PAD_A) ...
//   struct pad_input in; if (pad_read (0, &in)) ... in.lx / in.ly (-1000..1000)
//
// The buttons are named after their PLACE on an Xbox-style pad: PAD_A = the bottom face
// button, PAD_B = the right one, PAD_X = the left one, PAD_Y = the top one (on a PlayStation
// pad: Cross, Circle, Square, Triangle; on a Super Nintendo one: B, A, Y, X). The d-pad comes
// from the pad's d-pad (a hat, two axes or four buttons) and from the left stick.
// A pad reads as nothing pressed while the app's window does not have the keyboard.
//
// SD:/etc/gamepad.ini: a [vvvv:pppp] section per pad model (hex USB ids; the Gamepad app in
// Settings writes them: "Map buttons..."), [default] for the other pads Circle does not
// know. Keys: a b x y l r l2 r2 select start l3 r3 home up down left right = a button
// number (1 = the pad's first button; 0 = none); dpad = hat | axes | buttons | none;
// hat = n; x_axis / y_axis = the d-pad / left stick axes; rx_axis / ry_axis = the right
// stick; l2_axis / r2_axis = analog triggers (an axis; negative: its lower half); stick = 1 / 0
// (the left stick moves the d-pad too); deadzone = 0..1000.
// Pads Circle knows (Xbox 360 / One, PS3 / PS4, Switch Pro) need no mapping: their button
// numbers are Circle's TGamePadButton bits + 1 (circle/usb/usbgamepad.h).
//
#ifndef ONYX_GAMEPAD_H
#define ONYX_GAMEPAD_H

#include "appkit/appkit.h"

enum
{
	PAD_UP = 1 << 0, PAD_DOWN = 1 << 1, PAD_LEFT = 1 << 2, PAD_RIGHT = 1 << 3,
	PAD_A = 1 << 4, PAD_B = 1 << 5, PAD_X = 1 << 6, PAD_Y = 1 << 7,
	PAD_L = 1 << 8, PAD_R = 1 << 9, PAD_L2 = 1 << 10, PAD_R2 = 1 << 11,
	PAD_SELECT = 1 << 12, PAD_START = 1 << 13, PAD_L3 = 1 << 14, PAD_R3 = 1 << 15,
	PAD_HOME = 1 << 16
};
#define PAD_NBUTTONS	17
#define PAD_MAX		KAPI_PAD_MAX
// The ini key of each PAD_* bit (bit order).
static const char *const pad_names[PAD_NBUTTONS] = {
	"up", "down", "left", "right", "a", "b", "x", "y", "l", "r", "l2", "r2",
	"select", "start", "l3", "r3", "home" };

enum { PAD_DPAD_AUTO, PAD_DPAD_HAT, PAD_DPAD_AXES, PAD_DPAD_BUTTONS, PAD_DPAD_NONE };

struct pad_map
{
	int btn[PAD_NBUTTONS];		// the raw button number (1-based) of each PAD_* bit, 0 = none
	int dpad;			// PAD_DPAD_*
	int hat;			// 0-based
	int x_axis, y_axis;		// 1-based, 0 = none (the d-pad for dpad = axes; the left stick)
	int rx_axis, ry_axis;		// the right stick
	int l2_axis, r2_axis;		// analog triggers: an axis (1-based; negative = its lower half), 0 = none
	int stick;			// the left stick moves the d-pad too
	int dead;			// per mille
};

struct pad_input
{
	int connected, focus, known;
	unsigned short vid, pid;
	unsigned buttons;		// PAD_* (0 without the keyboard focus)
	int lx, ly, rx, ry;		// sticks, -1000..1000 (0 without the focus)
	unsigned raw;			// the pad's own button bits
};

// ---- the built-in mappings -----------------------------------------------------------------------
static inline void pad_map_default (struct pad_map *m, int known, unsigned props)
{
	// known pads: Circle's TGamePadButton bits (+1); others: the usual generic HID order
	// (1 top, 2 right, 3 bottom, 4 left, 5 L1, 6 R1, 7 L2, 8 R2, 9 select, 10 start, 11 L3,
	// 12 R3, 13 home -- DragonRise PS-style pads, Super Nintendo USB clones)
	static const int KNOWN[PAD_NBUTTONS] = { 16, 18, 19, 17, 10, 9, 11, 8, 6, 7, 4, 5, 12, 15, 13, 14, 1 };
	static const int GENERIC[PAD_NBUTTONS] = { 0, 0, 0, 0, 3, 2, 4, 1, 5, 6, 7, 8, 9, 10, 11, 12, 13 };
	for (int i = 0; i < PAD_NBUTTONS; i++) m->btn[i] = known ? KNOWN[i] : GENERIC[i];
	if (known && (props & (1u << 6)))	// alternative mapping (Switch Pro): + = start, - = select
	{
		m->btn[13] = 20; m->btn[12] = 21;
	}
	m->dpad = known ? PAD_DPAD_BUTTONS : PAD_DPAD_AUTO;
	m->hat = 0;
	m->x_axis = 1; m->y_axis = 2; m->rx_axis = 3; m->ry_axis = 4;
	m->l2_axis = m->r2_axis = 0;
	m->stick = 1; m->dead = 400;
}

// ---- the keyboard as a pad (pad 0), for a program that asks for it: pad_keyboard (1) ----------------------------
// [keyboard] in SD:/etc/gamepad.ini: a key for each PAD_* button (its ini key, as above) and for the right stick
// (rs_up, rs_down, rs_left, rs_right): a letter or a digit, or up, down, left, right, enter, backspace, space, tab,
// esc, home, end, pgup, pgdn, del, f1..f12, none. By default (the buttons by their place, as Nintendo's pads: the
// right one is A): the arrows the d-pad (and the left stick), x the right button (b), z the bottom one (a), s the top
// one (y), a the left one (x), q / w L / R, e / r L2 / R2, Enter Start, Backspace Select; i / k / j / l the right stick.
// Held keys (kapi_key_held: only while the program has the keyboard). The Gamepad applet sets them.
#define PAD_KEYS	(PAD_NBUTTONS + 4)		// the buttons, then the right stick's up, down, left, right
static const char *const pad_key_names[4] = { "rs_up", "rs_down", "rs_left", "rs_right" };
static const int pad_kbd_default[PAD_KEYS] = { KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, 'z', 'x', 'a', 's', 'q', 'w', 'e', 'r',
	KEY_BACKSPACE, KEY_ENTER, 0, 0, 0, 'i', 'k', 'j', 'l' };
static int g_pad_kbd[PAD_KEYS] = { KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, 'z', 'x', 'a', 's', 'q', 'w', 'e', 'r',
	KEY_BACKSPACE, KEY_ENTER, 0, 0, 0, 'i', 'k', 'j', 'l' };
static int g_pad_kbd_on;
static inline void pad_keyboard (int on) { g_pad_kbd_on = on; }
static const struct { const char *name; int code; } pad_key_words[] = {
	{ "up", KEY_UP }, { "down", KEY_DOWN }, { "left", KEY_LEFT }, { "right", KEY_RIGHT }, { "enter", KEY_ENTER },
	{ "backspace", KEY_BACKSPACE }, { "space", ' ' }, { "tab", KEY_TAB }, { "esc", 27 }, { "home", KEY_HOME },
	{ "end", KEY_END }, { "pgup", KEY_PGUP }, { "pgdn", KEY_PGDN }, { "del", KEY_DEL }, { "none", 0 } };
// A key's word in gamepad.ini -> its code (the KEY_* of appkit.h, a lower-case character), -1 not a key.
static inline int pad_key_code (const char *w, int n)
{
	if (n == 1) { int c = w[0] >= 'A' && w[0] <= 'Z' ? w[0] + 32 : w[0]; return c > ' ' && c < 127 ? c : -1; }
	for (unsigned i = 0; i < sizeof pad_key_words / sizeof pad_key_words[0]; i++)
	{
		int k = 0; while (k < n && pad_key_words[i].name[k] && (w[k] >= 'A' && w[k] <= 'Z' ? w[k] + 32 : w[k]) == pad_key_words[i].name[k]) k++;
		if (k == n && !pad_key_words[i].name[k]) return pad_key_words[i].code;
	}
	if ((w[0] == 'f' || w[0] == 'F') && n >= 2 && n <= 3)
	{
		int v = 0; for (int k = 1; k < n; k++) { if (w[k] < '0' || w[k] > '9') return -1; v = v * 10 + w[k] - '0'; }
		if (v >= 1 && v <= 12) return KEY_F1 + v - 1;
	}
	return -1;
}
// A key's code -> its word (the applet shows it, writes it).
static inline const char *pad_key_word (int code)
{
	static char one[2];
	for (unsigned i = 0; i < sizeof pad_key_words / sizeof pad_key_words[0]; i++) if (pad_key_words[i].code == code) return pad_key_words[i].name;
	if (code >= KEY_F1 && code <= KEY_F12) { static char f[4]; int v = code - KEY_F1 + 1; f[0] = 'f'; f[1] = (char) ('0' + (v >= 10 ? 1 : v)); f[2] = v >= 10 ? (char) ('0' + v - 10) : 0; f[3] = 0; return f; }
	one[0] = code > ' ' && code < 127 ? (char) code : '?'; one[1] = 0;
	return one;
}

// ---- SD:/etc/gamepad.ini ---------------------------------------------------------------------------
#define PAD_CFG_MAX	12
struct pad_cfg_entry { int kind; unsigned id; struct pad_map map; unsigned set; };	// kind 1 [default], 2 [vvvv:pppp]; set: keys given
static struct pad_cfg_entry g_pad_cfg[PAD_CFG_MAX];
static int g_pad_ncfg = -1;			// -1: not read yet

static inline int pad_lc (int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static inline int pad_word_eq (const char *a, int n, const char *b)
{
	int i = 0;
	for (; i < n; i++) if (b[i] == 0 || pad_lc (a[i]) != b[i]) return 0;
	return b[i] == 0;
}
static inline int pad_hexval (const char *s, int n, unsigned *out)
{
	unsigned v = 0; int k = 0;
	for (int i = 0; i < n; i++)
	{
		int c = pad_lc (s[i]);
		if (c >= '0' && c <= '9') v = v * 16 + (unsigned) (c - '0');
		else if (c >= 'a' && c <= 'f') v = v * 16 + (unsigned) (c - 'a' + 10);
		else return 0;
		k++;
	}
	*out = v; return k > 0;
}
static inline int pad_atoi (const char *s, int n)
{
	int v = 0, neg = 0, i = 0;
	if (i < n && s[i] == '-') { neg = 1; i++; }
	for (; i < n && s[i] >= '0' && s[i] <= '9'; i++) v = v * 10 + (s[i] - '0');
	return neg ? -v : v;
}

static inline void pad_cfg_set (struct pad_cfg_entry *en, const char *k, int kn, const char *v, int vn)
{
	struct pad_map *m = &en->map;
	for (int i = 0; i < PAD_NBUTTONS; i++)
		if (pad_word_eq (k, kn, pad_names[i])) { m->btn[i] = pad_atoi (v, vn); en->set |= 1u << i; return; }
	if (pad_word_eq (k, kn, "dpad"))
		m->dpad = pad_word_eq (v, vn, "hat") ? PAD_DPAD_HAT : pad_word_eq (v, vn, "axes") ? PAD_DPAD_AXES
			: pad_word_eq (v, vn, "buttons") ? PAD_DPAD_BUTTONS : pad_word_eq (v, vn, "none") ? PAD_DPAD_NONE
			: PAD_DPAD_AUTO;
	else if (pad_word_eq (k, kn, "hat")) m->hat = pad_atoi (v, vn) > 0 ? pad_atoi (v, vn) - 1 : 0;
	else if (pad_word_eq (k, kn, "x_axis")) m->x_axis = pad_atoi (v, vn);
	else if (pad_word_eq (k, kn, "y_axis")) m->y_axis = pad_atoi (v, vn);
	else if (pad_word_eq (k, kn, "rx_axis")) m->rx_axis = pad_atoi (v, vn);
	else if (pad_word_eq (k, kn, "ry_axis")) m->ry_axis = pad_atoi (v, vn);
	else if (pad_word_eq (k, kn, "l2_axis")) m->l2_axis = pad_atoi (v, vn);
	else if (pad_word_eq (k, kn, "r2_axis")) m->r2_axis = pad_atoi (v, vn);
	else if (pad_word_eq (k, kn, "stick")) m->stick = pad_atoi (v, vn) != 0;
	else if (pad_word_eq (k, kn, "deadzone")) m->dead = pad_atoi (v, vn);
}

// (Re-)read SD:/etc/gamepad.ini (read by itself on first use).
static inline void pad_config_reload (void)
{
	static char buf[4096];
	g_pad_ncfg = 0;
	for (int i = 0; i < PAD_KEYS; i++) g_pad_kbd[i] = pad_kbd_default[i];	// ([keyboard] below, else these)
	void *f = kapi_open ("SD:/etc/gamepad.ini");
	if (!f) return;
	int n = kapi_read (f, buf, sizeof buf - 1);
	kapi_close (f);
	if (n <= 0) return;
	buf[n] = 0;
	struct pad_cfg_entry *cur = 0;
	int kbd = 0;					// in [keyboard]
	for (int i = 0; i < n; )
	{
		int s = i; while (i < n && buf[i] != '\n') i++;
		int e = i; if (i < n) i++;
		while (s < e && (buf[s] == ' ' || buf[s] == '\t')) s++;
		while (e > s && (buf[e - 1] == ' ' || buf[e - 1] == '\t' || buf[e - 1] == '\r')) e--;
		if (s >= e || buf[s] == '#' || buf[s] == ';') continue;
		if (buf[s] == '[')
		{
			cur = 0;
			int c = s + 1; while (c < e && buf[c] != ']') c++;
			kbd = pad_word_eq (buf + s + 1, c - s - 1, "keyboard");
			if (kbd) continue;
			if (g_pad_ncfg >= PAD_CFG_MAX) continue;
			struct pad_cfg_entry *en = &g_pad_cfg[g_pad_ncfg];
			unsigned v = 0, p = 0; int colon = s + 1; while (colon < c && buf[colon] != ':') colon++;
			if (pad_word_eq (buf + s + 1, c - s - 1, "default")) { en->kind = 1; en->id = 0; en->set = 0; pad_map_default (&en->map, 0, 0); }
			else if (colon < c && pad_hexval (buf + s + 1, colon - s - 1, &v) && pad_hexval (buf + colon + 1, c - colon - 1, &p))
			{ en->kind = 2; en->id = (v << 16) | (p & 0xFFFF); en->set = 0; pad_map_default (&en->map, 0, 0); }
			else continue;
			cur = en; g_pad_ncfg++;
			continue;
		}
		if (!cur && !kbd) continue;
		int eq = s; while (eq < e && buf[eq] != '=') eq++;
		if (eq >= e) continue;
		int ke = eq; while (ke > s && (buf[ke - 1] == ' ' || buf[ke - 1] == '\t')) ke--;
		int vs = eq + 1; while (vs < e && (buf[vs] == ' ' || buf[vs] == '\t')) vs++;
		int ve = vs; while (ve < e && buf[ve] != ' ' && buf[ve] != '\t' && buf[ve] != ';' && buf[ve] != '#') ve++;
		if (kbd)					// [keyboard]: a button's key
		{
			int code = pad_key_code (buf + vs, ve - vs);
			if (code < 0) continue;
			for (int b = 0; b < PAD_KEYS; b++)
				if (pad_word_eq (buf + s, ke - s, b < PAD_NBUTTONS ? pad_names[b] : pad_key_names[b - PAD_NBUTTONS])) g_pad_kbd[b] = code;
			continue;
		}
		pad_cfg_set (cur, buf + s, ke - s, buf + vs, ve - vs);
	}
	// a button a section gives to one function leaves the functions it had by default
	for (int e = 0; e < g_pad_ncfg; e++)
		for (int i = 0; i < PAD_NBUTTONS; i++)
			if ((g_pad_cfg[e].set >> i) & 1)
				for (int j = 0; j < PAD_NBUTTONS; j++)
					if (!((g_pad_cfg[e].set >> j) & 1) && g_pad_cfg[e].map.btn[j] == g_pad_cfg[e].map.btn[i]) g_pad_cfg[e].map.btn[j] = 0;
}

// The mapping of a pad: its own section, else the built-in one (known pads) or [default].
// Returns 2 (own section), 1 ([default]) or 0 (built-in).
static inline int pad_map_for (const struct kapi_pad *p, struct pad_map *m)
{
	if (g_pad_ncfg < 0) pad_config_reload ();
	int known = (p->props & 1) != 0;
	unsigned id = ((unsigned) p->vid << 16) | p->pid;
	for (int i = 0; i < g_pad_ncfg; i++)
		if (g_pad_cfg[i].kind == 2 && g_pad_cfg[i].id == id) { *m = g_pad_cfg[i].map; return 2; }
	if (!known)
		for (int i = 0; i < g_pad_ncfg; i++)
			if (g_pad_cfg[i].kind == 1) { *m = g_pad_cfg[i].map; return 1; }
	pad_map_default (m, known, p->props);
	return 0;
}

// ---- reading -------------------------------------------------------------------------------------
static inline int pad_axis_norm (const struct kapi_pad *p, int axis1)
{
	if (axis1 <= 0 || axis1 > p->naxes) return 0;
	int lo = p->axes[axis1 - 1].minimum, hi = p->axes[axis1 - 1].maximum, v = p->axes[axis1 - 1].value;
	if (hi <= lo) return 0;
	long r = ((long) (v - lo) * 2000) / (hi - lo) - 1000;
	return r < -1000 ? -1000 : r > 1000 ? 1000 : (int) r;
}

// an axis the d-pad / left stick or a trigger takes
static inline int pad_axis_used (const struct pad_map *m, int ax)
{
	return ax > 0 && (ax == m->x_axis || ax == m->y_axis || ax == m->l2_axis || ax == -m->l2_axis || ax == m->r2_axis || ax == -m->r2_axis);
}

// The PAD_* mask of a raw state through a mapping (the focus is not looked at).
static inline unsigned pad_apply (const struct kapi_pad *p, const struct pad_map *m, int *lx, int *ly, int *rx, int *ry)
{
	unsigned b = 0;
	for (int i = 4; i < PAD_NBUTTONS; i++)
		if (m->btn[i] > 0 && m->btn[i] <= 32 && ((p->buttons >> (m->btn[i] - 1)) & 1)) b |= 1u << i;
	int x = pad_axis_norm (p, m->x_axis), y = pad_axis_norm (p, m->y_axis);
	int dpad = m->dpad;
	if (dpad == PAD_DPAD_AUTO) dpad = p->nhats > 0 ? PAD_DPAD_HAT : PAD_DPAD_AXES;
	if (dpad == PAD_DPAD_HAT && m->hat < p->nhats)
	{
		static const unsigned char H[8] = { 1, 1 | 8, 8, 2 | 8, 2, 2 | 4, 4, 1 | 4 };	// N NE E SE S SW W NW
		int h = p->hats[m->hat];
		if (h >= 0 && h < 8) b |= H[h];
	}
	else if (dpad == PAD_DPAD_BUTTONS)
	{
		for (int i = 0; i < 4; i++)
			if (m->btn[i] > 0 && m->btn[i] <= 32 && ((p->buttons >> (m->btn[i] - 1)) & 1)) b |= 1u << i;
	}
	if (dpad == PAD_DPAD_AXES || m->stick)
	{
		if (x < -m->dead) b |= PAD_LEFT; else if (x > m->dead) b |= PAD_RIGHT;
		if (y < -m->dead) b |= PAD_UP; else if (y > m->dead) b |= PAD_DOWN;
	}
	// analog triggers: pressed past a quarter of the way (from the axis's rest: its end or its middle)
	if (m->l2_axis) { int v = pad_axis_norm (p, m->l2_axis < 0 ? -m->l2_axis : m->l2_axis); if (m->l2_axis > 0 ? v > 250 : v < -250) b |= PAD_L2; }
	if (m->r2_axis) { int v = pad_axis_norm (p, m->r2_axis < 0 ? -m->r2_axis : m->r2_axis); if (m->r2_axis > 0 ? v > 250 : v < -250) b |= PAD_R2; }
	if ((b & PAD_LEFT) && (b & PAD_RIGHT)) b &= ~(unsigned) (PAD_LEFT | PAD_RIGHT);
	if ((b & PAD_UP) && (b & PAD_DOWN)) b &= ~(unsigned) (PAD_UP | PAD_DOWN);
	if (lx) *lx = x;
	if (ly) *ly = y;
	// the right stick: never an axis the d-pad / left stick or a trigger already uses (a pad whose
	// d-pad is on axes 3 / 4, mapped so, would also move the right stick of the defaults)
	if (rx) *rx = pad_axis_used (m, m->rx_axis) ? 0 : pad_axis_norm (p, m->rx_axis);
	if (ry) *ry = pad_axis_used (m, m->ry_axis) ? 0 : pad_axis_norm (p, m->ry_axis);
	return b;
}

// The PAD_* buttons held on the keyboard ([keyboard]), its sticks (-1000..1000; 0 none) -- whatever pad_keyboard says.
static inline unsigned pad_keys (int *lx, int *ly, int *rx, int *ry)
{
	if (g_pad_ncfg < 0) pad_config_reload ();
	unsigned b = 0;
	for (int i = 0; i < PAD_NBUTTONS; i++) if (g_pad_kbd[i] && kapi_key_held (g_pad_kbd[i])) b |= 1u << i;
	if ((b & PAD_LEFT) && (b & PAD_RIGHT)) b &= ~(unsigned) (PAD_LEFT | PAD_RIGHT);
	if ((b & PAD_UP) && (b & PAD_DOWN)) b &= ~(unsigned) (PAD_UP | PAD_DOWN);
	if (lx) *lx = (b & PAD_LEFT) ? -1000 : (b & PAD_RIGHT) ? 1000 : 0;
	if (ly) *ly = (b & PAD_UP) ? -1000 : (b & PAD_DOWN) ? 1000 : 0;
	int h[4];
	for (int i = 0; i < 4; i++) h[i] = g_pad_kbd[PAD_NBUTTONS + i] && kapi_key_held (g_pad_kbd[PAD_NBUTTONS + i]);
	if (rx) *rx = h[2] && !h[3] ? -1000 : h[3] && !h[2] ? 1000 : 0;
	if (ry) *ry = h[0] && !h[1] ? -1000 : h[1] && !h[0] ? 1000 : 0;
	return b;
}

// Pad 0..3: 1 if there (out filled; buttons and sticks 0 while the window has not the keyboard). With pad_keyboard (1),
// pad 0 is there even with no pad plugged in: the keyboard's keys ([keyboard]) added to the first pad's.
static inline int pad_read (int index, struct pad_input *out)
{
	struct kapi_pad p;
	out->connected = out->focus = out->known = 0; out->vid = out->pid = 0;
	out->buttons = 0; out->lx = out->ly = out->rx = out->ry = 0; out->raw = 0;
	int there = kapi_pad_state (index, &p);
	if (there)
	{
		struct pad_map m;
		pad_map_for (&p, &m);
		out->connected = 1; out->focus = p.focus; out->known = (p.props & 1) != 0;
		out->vid = p.vid; out->pid = p.pid; out->raw = p.buttons;
		if (p.focus) out->buttons = pad_apply (&p, &m, &out->lx, &out->ly, &out->rx, &out->ry);
	}
	if (index == 0 && g_pad_kbd_on)
	{
		int lx, ly, rx, ry;
		out->buttons |= pad_keys (&lx, &ly, &rx, &ry);
		if (!out->lx && !out->ly) { out->lx = lx; out->ly = ly; }
		if (!out->rx && !out->ry) { out->rx = rx; out->ry = ry; }
		if (!there) { out->connected = 1; out->focus = 1; }
		return 1;
	}
	return there;
}

// The PAD_* buttons of pad 0..3, or of every pad OR'ed (-1).
static inline unsigned pad_buttons (int index)
{
	struct pad_input in;
	if (index >= 0) return pad_read (index, &in) ? in.buttons : 0;
	unsigned b = 0;
	for (int i = 0; i < PAD_MAX; i++) if (pad_read (i, &in)) b |= in.buttons;
	return b;
}

// ---- learning a pad's buttons (the Gamepad applet's Map Buttons..., the console's Gamepad page) -------------------
// The steps in PAD_* bit order (up, down, left, right, a, b, x, y, l, r, l2, r2, select, start, l3, r3, home): each asks
// for a button and takes the first that leaves its rest state -- a hat or two axes for the d-pad (one press of UP, or
// UP + LEFT), an axis for an analog trigger (L2 / R2), else a button. Then gamepad.ini's section of the pad's model.
//   struct pad_learn L; pad_learn_start (&L, &raw);
//   each turn: int r = pad_learn_poll (&L, &raw);  // 1 a step learnt, 2 every step done (pad_learn_save), 0 waiting
//   pad_learn_skip (&L): this pad has no such button (Esc)
static const char *const pad_learn_text[PAD_NBUTTONS] = {	// (English: the programs translate them -- TRN)
	"UP on the d-pad", "DOWN on the d-pad", "LEFT on the d-pad", "RIGHT on the d-pad",
	"the BOTTOM face button (Xbox A, PlayStation Cross, Nintendo B)",
	"the RIGHT face button (Xbox B, PlayStation Circle, Nintendo A)",
	"the LEFT face button (Xbox X, PlayStation Square, Nintendo Y)",
	"the TOP face button (Xbox Y, PlayStation Triangle, Nintendo X)",
	"the LEFT shoulder button (L / L1 / LB)", "the RIGHT shoulder button (R / R1 / RB)",
	"the LEFT trigger (L2 / LT / ZL: a button or an analog trigger)", "the RIGHT trigger (R2 / RT / ZR)",
	"SELECT (Back / Share / -)", "START (Options / +)",
	"the LEFT stick's click (L3)", "the RIGHT stick's click (R3)", "HOME (Guide / PS)" };
struct pad_learn
{
	int step;			// the button asked (PAD_* bit), PAD_NBUTTONS: done
	int wait_release;		// the last press not let go yet
	struct kapi_pad base;		// the pad at rest
	struct pad_map map;		// what was learnt
	unsigned vid, pid;		// the pad's model
};
static inline int pad_learn_dev (const struct kapi_pad *p, const struct kapi_pad *b, int i)
{
	int range = p->axes[i].maximum - p->axes[i].minimum;
	return range <= 0 ? 0 : (p->axes[i].value - b->axes[i].value) * 1000 / range;
}
static inline int pad_learn_rest (const struct pad_learn *L, const struct kapi_pad *p)
{
	if (p->buttons != L->base.buttons) return 0;
	for (int i = 0; i < p->nhats; i++) if (p->hats[i] >= 0 && p->hats[i] < 8) return 0;
	for (int i = 0; i < p->naxes; i++) { int d = pad_learn_dev (p, &L->base, i); if (d > 250 || d < -250) return 0; }
	return 1;
}
static inline void pad_learn_start (struct pad_learn *L, const struct kapi_pad *raw)
{
	L->step = 0; L->wait_release = 0; L->base = *raw; L->vid = raw->vid; L->pid = raw->pid;
	for (int i = 0; i < L->base.nhats; i++) L->base.hats[i] = 8;	// (a rest hat: centred)
	pad_map_default (&L->map, 0, 0);
	for (int i = 0; i < PAD_NBUTTONS; i++) L->map.btn[i] = 0;
	L->map.dpad = PAD_DPAD_NONE; L->map.x_axis = L->map.y_axis = 0; L->map.l2_axis = L->map.r2_axis = 0;
}
static inline void pad_learn_next (struct pad_learn *L)
{
	L->step++;
	// a d-pad on a hat or axes is done with UP (hat) or UP + LEFT (axes)
	while (L->step < 4 && (L->map.dpad == PAD_DPAD_HAT || (L->map.dpad == PAD_DPAD_AXES && (L->step == 1 || L->step == 3)))) L->step++;
	L->wait_release = 1;
}
static inline void pad_learn_skip (struct pad_learn *L) { if (L->step < PAD_NBUTTONS) pad_learn_next (L); L->wait_release = 0; }
static inline int pad_learn_poll (struct pad_learn *L, const struct kapi_pad *p)
{
	if (L->wait_release) { if (pad_learn_rest (L, p)) L->wait_release = 0; return 0; }
	if (L->step >= PAD_NBUTTONS) return 2;
	if (L->step < 4)
		for (int i = 0; i < p->nhats; i++)
			if (p->hats[i] >= 0 && p->hats[i] < 8) { L->map.dpad = PAD_DPAD_HAT; L->map.hat = i; pad_learn_next (L); return 1; }
	if (L->step < 4)
		for (int i = 0; i < p->naxes; i++)
		{
			int d = pad_learn_dev (p, &L->base, i);
			if (d > 400 || d < -400)
			{
				L->map.dpad = PAD_DPAD_AXES;
				if (L->step == 0 || L->step == 1) L->map.y_axis = i + 1; else L->map.x_axis = i + 1;
				pad_learn_next (L); return 1;
			}
		}
	if (L->step == 10 || L->step == 11)				// L2 / R2: an analog trigger (an axis) too
		for (int i = 0; i < p->naxes; i++)
		{
			int d = pad_learn_dev (p, &L->base, i);
			if (d > 400 || d < -400)
			{
				int ax = d > 0 ? i + 1 : -(i + 1);
				if (L->step == 10) L->map.l2_axis = ax; else L->map.r2_axis = ax;
				pad_learn_next (L); return 1;
			}
		}
	unsigned nb = p->buttons & ~L->base.buttons;
	if (nb)
	{
		int b = 0; while (!((nb >> b) & 1)) b++;
		L->map.btn[L->step] = b + 1;
		if (L->step < 4) L->map.dpad = PAD_DPAD_BUTTONS;
		pad_learn_next (L);
		return 1;
	}
	return 0;
}

// ---- writing SD:/etc/gamepad.ini --------------------------------------------------------------------------------
static inline void pad_cat (char *d, int *n, int cap, const char *s) { while (*s && *n < cap - 1) d[(*n)++] = *s++; d[*n] = 0; }
static inline void pad_cati (char *d, int *n, int cap, int v)
{
	char t[16]; int k = 0; int neg = v < 0; unsigned u = neg ? (unsigned) -v : (unsigned) v;
	do { t[k++] = (char) ('0' + u % 10); u /= 10; } while (u);
	if (neg) t[k++] = '-';
	while (k && *n < cap - 1) d[(*n)++] = t[--k];
	d[*n] = 0;
}
static inline void pad_cathex (char *d, int *n, int cap, unsigned v)
{
	for (int i = 3; i >= 0; i--) { int x = (v >> (i * 4)) & 15; if (*n < cap - 1) d[(*n)++] = (char) (x < 10 ? '0' + x : 'a' + x - 10); }
	d[*n] = 0;
}
// A section of SD:/etc/gamepad.ini replaced by body (head "[045e:028e]", "[keyboard]"; body "" removes it) -> 1 written.
static inline int pad_ini_section (const char *head, const char *body)
{
	static char buf[8192], out[8192];
	int n = 0;
	void *f = kapi_open ("SD:/etc/gamepad.ini");
	if (f) { n = kapi_read (f, buf, sizeof buf - 1); kapi_close (f); if (n < 0) n = 0; }
	buf[n] = 0;
	int hn = 0; while (head[hn]) hn++;
	int o = 0, skip = 0;
	for (int i = 0; i < n; )
	{
		int s = i; while (i < n && buf[i] != '\n') i++;
		if (i < n) i++;
		int t = s; while (t < i && (buf[t] == ' ' || buf[t] == '\t')) t++;
		if (buf[t] == '[')
		{
			skip = 1;
			for (int k = 0; k < hn; k++) if (pad_lc (buf[t + k]) != pad_lc (head[k])) { skip = 0; break; }
		}
		if (!skip) for (int k = s; k < i && o < (int) sizeof out - 1; k++) out[o++] = buf[k];
	}
	if (o > 0 && out[o - 1] != '\n' && o < (int) sizeof out - 1) out[o++] = '\n';
	out[o] = 0;
	pad_cat (out, &o, sizeof out, body);
	int ok = kapi_save_file ("SD:/etc/gamepad.ini", out, (unsigned) o) >= 0;
	pad_config_reload ();
	return ok;
}
// What was learnt written as the pad model's section -> 1 written.
static inline int pad_learn_save (const struct pad_learn *L, const char *comment)
{
	static char body[1024]; int n = 0; body[0] = 0;
	char head[16]; int hn = 0; head[0] = 0;
	pad_cat (head, &hn, sizeof head, "["); pad_cathex (head, &hn, sizeof head, L->vid); pad_cat (head, &hn, sizeof head, ":");
	pad_cathex (head, &hn, sizeof head, L->pid); pad_cat (head, &hn, sizeof head, "]");
	const struct pad_map *g = &L->map;
	pad_cat (body, &n, sizeof body, head); pad_cat (body, &n, sizeof body, "\t; "); pad_cat (body, &n, sizeof body, comment ? comment : "mapped"); pad_cat (body, &n, sizeof body, "\n");
	pad_cat (body, &n, sizeof body, "dpad = ");
	pad_cat (body, &n, sizeof body, g->dpad == PAD_DPAD_HAT ? "hat" : g->dpad == PAD_DPAD_AXES ? "axes" : g->dpad == PAD_DPAD_BUTTONS ? "buttons" : "none");
	pad_cat (body, &n, sizeof body, "\n");
	if (g->dpad == PAD_DPAD_HAT) { pad_cat (body, &n, sizeof body, "hat = "); pad_cati (body, &n, sizeof body, g->hat + 1); pad_cat (body, &n, sizeof body, "\n"); }
	int xa = g->x_axis ? g->x_axis : 1, ya = g->y_axis ? g->y_axis : 2;
	pad_cat (body, &n, sizeof body, "x_axis = "); pad_cati (body, &n, sizeof body, xa);
	pad_cat (body, &n, sizeof body, "\ny_axis = "); pad_cati (body, &n, sizeof body, ya); pad_cat (body, &n, sizeof body, "\n");
	pad_cat (body, &n, sizeof body, "stick = "); pad_cati (body, &n, sizeof body, g->dpad == PAD_DPAD_AXES ? 0 : 1); pad_cat (body, &n, sizeof body, "\n");
	if (g->l2_axis) { pad_cat (body, &n, sizeof body, "l2_axis = "); pad_cati (body, &n, sizeof body, g->l2_axis); pad_cat (body, &n, sizeof body, "\n"); }
	if (g->r2_axis) { pad_cat (body, &n, sizeof body, "r2_axis = "); pad_cati (body, &n, sizeof body, g->r2_axis); pad_cat (body, &n, sizeof body, "\n"); }
	for (int i = 0; i < PAD_NBUTTONS; i++)
	{
		if (i < 4 && g->dpad != PAD_DPAD_BUTTONS) continue;
		pad_cat (body, &n, sizeof body, pad_names[i]); pad_cat (body, &n, sizeof body, " = ");
		pad_cati (body, &n, sizeof body, g->btn[i]); pad_cat (body, &n, sizeof body, "\n");
	}
	return pad_ini_section (head, body);
}
// A pad model's section removed (its mapping forgotten) -> 1 written.
static inline int pad_forget (unsigned vid, unsigned pid)
{
	char head[16]; int hn = 0; head[0] = 0;
	pad_cat (head, &hn, sizeof head, "["); pad_cathex (head, &hn, sizeof head, vid); pad_cat (head, &hn, sizeof head, ":");
	pad_cathex (head, &hn, sizeof head, pid); pad_cat (head, &hn, sizeof head, "]");
	return pad_ini_section (head, "");
}
// The keyboard's keys (g_pad_kbd) written as [keyboard] -> 1 written; pad_keyboard_reset: the section removed.
static inline int pad_keyboard_save (void)
{
	static char body[1024]; int n = 0; body[0] = 0;
	pad_cat (body, &n, sizeof body, "[keyboard]\t; the keyboard as pad 1 (the emulators)\n");
	for (int i = 0; i < PAD_KEYS; i++)
	{
		pad_cat (body, &n, sizeof body, i < PAD_NBUTTONS ? pad_names[i] : pad_key_names[i - PAD_NBUTTONS]);
		pad_cat (body, &n, sizeof body, " = "); pad_cat (body, &n, sizeof body, g_pad_kbd[i] ? pad_key_word (g_pad_kbd[i]) : "none");
		pad_cat (body, &n, sizeof body, "\n");
	}
	return pad_ini_section ("[keyboard]", body);
}
static inline int pad_keyboard_reset (void) { return pad_ini_section ("[keyboard]", ""); }
// A key given to button b (0..PAD_KEYS-1): taken from any other that had it (a key does one thing), then saved.
static inline int pad_keyboard_set (int b, int code)
{
	if (b < 0 || b >= PAD_KEYS) return 0;
	if (g_pad_ncfg < 0) pad_config_reload ();
	if (code >= 'A' && code <= 'Z') code += 32;
	if (code == '\n' || code == '\r') code = KEY_ENTER;
	for (int i = 0; i < PAD_KEYS; i++) if (i != b && g_pad_kbd[i] == code) g_pad_kbd[i] = 0;
	g_pad_kbd[b] = code;
	return pad_keyboard_save ();
}

#endif
