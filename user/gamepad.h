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
// stick; stick = 1 / 0 (the left stick moves the d-pad too); deadzone = 0..1000.
// Pads Circle knows (Xbox 360 / One, PS3 / PS4, Switch Pro) need no mapping: their button
// numbers are Circle's TGamePadButton bits + 1 (circle/usb/usbgamepad.h).
//
#ifndef ONYX_GAMEPAD_H
#define ONYX_GAMEPAD_H

#include "kapi.h"

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
	m->stick = 1; m->dead = 400;
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
	else if (pad_word_eq (k, kn, "stick")) m->stick = pad_atoi (v, vn) != 0;
	else if (pad_word_eq (k, kn, "deadzone")) m->dead = pad_atoi (v, vn);
}

// (Re-)read SD:/etc/gamepad.ini (read by itself on first use).
static inline void pad_config_reload (void)
{
	static char buf[4096];
	g_pad_ncfg = 0;
	void *f = kapi_open ("SD:/etc/gamepad.ini");
	if (!f) return;
	int n = kapi_read (f, buf, sizeof buf - 1);
	kapi_close (f);
	if (n <= 0) return;
	buf[n] = 0;
	struct pad_cfg_entry *cur = 0;
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
		if (!cur) continue;
		int eq = s; while (eq < e && buf[eq] != '=') eq++;
		if (eq >= e) continue;
		int ke = eq; while (ke > s && (buf[ke - 1] == ' ' || buf[ke - 1] == '\t')) ke--;
		int vs = eq + 1; while (vs < e && (buf[vs] == ' ' || buf[vs] == '\t')) vs++;
		int ve = vs; while (ve < e && buf[ve] != ' ' && buf[ve] != '\t' && buf[ve] != ';' && buf[ve] != '#') ve++;
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
	if ((b & PAD_LEFT) && (b & PAD_RIGHT)) b &= ~(unsigned) (PAD_LEFT | PAD_RIGHT);
	if ((b & PAD_UP) && (b & PAD_DOWN)) b &= ~(unsigned) (PAD_UP | PAD_DOWN);
	if (lx) *lx = x;
	if (ly) *ly = y;
	if (rx) *rx = pad_axis_norm (p, m->rx_axis);
	if (ry) *ry = pad_axis_norm (p, m->ry_axis);
	return b;
}

// Pad 0..3: 1 if there (out filled; buttons and sticks 0 while the window has not the keyboard).
static inline int pad_read (int index, struct pad_input *out)
{
	struct kapi_pad p;
	out->connected = out->focus = out->known = 0; out->vid = out->pid = 0;
	out->buttons = 0; out->lx = out->ly = out->rx = out->ry = 0; out->raw = 0;
	if (!kapi_pad_state (index, &p)) return 0;
	struct pad_map m;
	pad_map_for (&p, &m);
	out->connected = 1; out->focus = p.focus; out->known = (p.props & 1) != 0;
	out->vid = p.vid; out->pid = p.pid; out->raw = p.buttons;
	if (p.focus) out->buttons = pad_apply (&p, &m, &out->lx, &out->ly, &out->rx, &out->ry);
	return 1;
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

#endif
