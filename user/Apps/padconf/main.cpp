//
// padconf -- the Gamepad settings: the USB gamepads plugged in (Circle's drivers, kapi v50),
// what each one sends (its buttons, axes and hats, live) and what the apps see (the PAD_*
// buttons of user/Include/gamepad.h, on a drawn pad), and "Map Buttons..." -- press each button when
// asked -- which writes the pad model's section of SD:/etc/gamepad.ini.
//
//   * Pads 1-4: click the tabs or keys 1-4. Keyboard (the 5th tab, key 5): the keyboard as pad 1 in the emulators --
//     a key for each button (gamepad.h's [keyboard]): a row chosen, Enter or a click, then the key (Esc: cancel). Map Buttons... (M), Forget Mapping (the pad's
//     section removed: back to the built-in / [default] mapping), Reload gamepad.ini: the
//     buttons at the bottom (and the Pad menu when it is a window of its own).
//   * The Control Panel's Gamepad applet (applet_proto.h), or a window of its own when run alone.
//   * While mapping: press the button asked for; Esc = this pad has no such button (skip),
//     Backspace = cancel.
//
#include "appkit/appkit.h"
#include "gamepad.h"
#include "uikit/uikit.h"
#include "fontkit/uikitface.h"		// FreeType's text (DejaVu Sans) for every widget

using namespace uikit;

#define W	600
#define H	440
#define TABW	116			// a pad's tab, from one to the next ("Manette 1 -" fits)

static Root *g_root = 0;
static int g_pad = 0;
static struct kapi_pad g_raw;
static bool g_there = false;

static void cat (char *d, int *n, int cap, const char *s) { while (*s && *n < cap - 1) d[(*n)++] = *s++; d[*n] = 0; }
static void cati (char *d, int *n, int cap, int v)
{
	char t[16]; int k = 0; bool neg = v < 0; unsigned u = neg ? (unsigned) -v : (unsigned) v;
	do { t[k++] = (char) ('0' + u % 10); u /= 10; } while (u);
	if (neg) t[k++] = '-';
	while (k && *n < cap - 1) d[(*n)++] = t[--k];
	d[*n] = 0;
}
static void cathex (char *d, int *n, int cap, unsigned v, int digits)
{
	for (int i = digits - 1; i >= 0; i--) { int x = (v >> (i * 4)) & 15; char c = (char) (x < 10 ? '0' + x : 'a' + x - 10); if (*n < cap - 1) d[(*n)++] = c; }
	d[*n] = 0;
}

// ---- mapping (the wizard) -----------------------------------------------------------------------
// The steps, in PAD_* bit order: up down left right a b x y l r l2 r2 select start l3 r3 home.
static const char *const STEP_TEXT[PAD_NBUTTONS] = {
	TRN ("UP on the d-pad"), TRN ("DOWN on the d-pad"), TRN ("LEFT on the d-pad"), TRN ("RIGHT on the d-pad"),
	TRN ("the BOTTOM face button (Xbox A, PlayStation Cross, Nintendo B)"),
	TRN ("the RIGHT face button (Xbox B, PlayStation Circle, Nintendo A)"),
	TRN ("the LEFT face button (Xbox X, PlayStation Square, Nintendo Y)"),
	TRN ("the TOP face button (Xbox Y, PlayStation Triangle, Nintendo X)"),
	TRN ("the LEFT shoulder button (L / L1 / LB)"), TRN ("the RIGHT shoulder button (R / R1 / RB)"),
	TRN ("the LEFT trigger (L2 / LT / ZL: a button or an analog trigger)"), TRN ("the RIGHT trigger (R2 / RT / ZR)"),
	TRN ("SELECT (Back / Share / -)"), TRN ("START (Options / +)"),
	TRN ("the LEFT stick's click (L3)"), TRN ("the RIGHT stick's click (R3)"), TRN ("HOME (Guide / PS)") };

static bool g_mapping = false;
static int g_step = 0;
static bool g_waitRelease = false;
static struct kapi_pad g_base;				// the rest state of the step
static struct pad_map g_new;
static char g_msg[160] = "";

static int axis_dev (const struct kapi_pad &p, const struct kapi_pad &b, int i)
{
	int range = p.axes[i].maximum - p.axes[i].minimum;
	if (range <= 0) return 0;
	return (p.axes[i].value - b.axes[i].value) * 1000 / range;
}
static bool at_rest (const struct kapi_pad &p)
{
	if (p.buttons != g_base.buttons) return false;
	for (int i = 0; i < p.nhats; i++) if (p.hats[i] >= 0 && p.hats[i] < 8) return false;
	for (int i = 0; i < p.naxes; i++) { int d = axis_dev (p, g_base, i); if (d > 250 || d < -250) return false; }
	return true;
}

static void map_start (void)
{
	if (!g_there) { int n = 0; g_msg[0] = 0; cat (g_msg, &n, sizeof g_msg, TR ("No gamepad here: plug one in.")); return; }
	g_mapping = true; g_step = 0; g_waitRelease = false;
	g_base = g_raw;
	for (int i = 0; i < g_base.nhats; i++) g_base.hats[i] = 8;	// (a rest hat: centred)
	pad_map_default (&g_new, 0, 0);
	for (int i = 0; i < PAD_NBUTTONS; i++) g_new.btn[i] = 0;
	g_new.dpad = PAD_DPAD_NONE; g_new.x_axis = g_new.y_axis = 0; g_new.l2_axis = g_new.r2_axis = 0;
	g_msg[0] = 0;
}

static void next_step (void)
{
	g_step++;
	// a d-pad on a hat or axes is done with UP (hat) or UP + LEFT (axes)
	while (g_step < 4 && (g_new.dpad == PAD_DPAD_HAT || (g_new.dpad == PAD_DPAD_AXES && (g_step == 1 || g_step == 3))))
		g_step++;
	g_waitRelease = true;
}

static void save_mapping (void);

// A press for the current step: a hat, an axis or a button that left its rest state.
static void map_poll (void)
{
	if (!g_mapping || !g_there) return;
	const struct kapi_pad &p = g_raw;
	if (g_waitRelease) { if (at_rest (p)) g_waitRelease = false; return; }
	if (g_step >= PAD_NBUTTONS) { g_mapping = false; save_mapping (); return; }
	if (g_step < 4)
		for (int i = 0; i < p.nhats; i++)
			if (p.hats[i] >= 0 && p.hats[i] < 8) { g_new.dpad = PAD_DPAD_HAT; g_new.hat = i; next_step (); return; }
	if (g_step < 4)
		for (int i = 0; i < p.naxes; i++)
		{
			int d = axis_dev (p, g_base, i);
			if (d > 400 || d < -400)
			{
				g_new.dpad = PAD_DPAD_AXES;
				if (g_step == 0 || g_step == 1) g_new.y_axis = i + 1; else g_new.x_axis = i + 1;
				next_step (); return;
			}
		}
	if (g_step == 10 || g_step == 11)				// L2 / R2: an analog trigger (an axis) too
		for (int i = 0; i < p.naxes; i++)
		{
			int d = axis_dev (p, g_base, i);
			if (d > 400 || d < -400)
			{
				int ax = d > 0 ? i + 1 : -(i + 1);
				if (g_step == 10) g_new.l2_axis = ax; else g_new.r2_axis = ax;
				next_step (); return;
			}
		}
	unsigned nb = p.buttons & ~g_base.buttons;
	if (nb)
	{
		int b = 0; while (!((nb >> b) & 1)) b++;
		g_new.btn[g_step] = b + 1;
		if (g_step < 4) g_new.dpad = PAD_DPAD_BUTTONS;
		next_step ();
	}
}

// Replace (or add) a section of SD:/etc/gamepad.ini: the pad model's ([vvvv:pppp]), or head's ("[keyboard]").
static bool write_section (const char *body, const char *headWanted = 0)
{
	static char buf[8192], out[8192];
	int n = 0;
	void *f = kapi_open ("SD:/etc/gamepad.ini");
	if (f) { n = kapi_read (f, buf, sizeof buf - 1); kapi_close (f); if (n < 0) n = 0; }
	buf[n] = 0;
	char head[16]; int hn = 0; head[0] = 0;
	cat (head, &hn, sizeof head, "["); cathex (head, &hn, sizeof head, g_raw.vid, 4); cat (head, &hn, sizeof head, ":");
	cathex (head, &hn, sizeof head, g_raw.pid, 4); cat (head, &hn, sizeof head, "]");
	if (headWanted) { hn = 0; head[0] = 0; cat (head, &hn, sizeof head, headWanted); }
	int o = 0; bool skip = false;
	for (int i = 0; i < n; )
	{
		int s = i; while (i < n && buf[i] != '\n') i++;
		if (i < n) i++;
		int t = s; while (t < i && (buf[t] == ' ' || buf[t] == '\t')) t++;
		if (buf[t] == '[')
		{
			skip = true;
			for (int k = 0; k < hn; k++) if (pad_lc (buf[t + k]) != head[k]) { skip = false; break; }
		}
		if (!skip) for (int k = s; k < i && o < (int) sizeof out - 1; k++) out[o++] = buf[k];
	}
	if (o > 0 && out[o - 1] != '\n' && o < (int) sizeof out - 1) out[o++] = '\n';
	out[o] = 0;
	cat (out, &o, sizeof out, body);
	return kapi_save_file ("SD:/etc/gamepad.ini", out, (unsigned) o) >= 0;
}

static void save_mapping (void)
{
	static char body[1024]; int n = 0; body[0] = 0;
	cat (body, &n, sizeof body, "["); cathex (body, &n, sizeof body, g_raw.vid, 4); cat (body, &n, sizeof body, ":");
	cathex (body, &n, sizeof body, g_raw.pid, 4); cat (body, &n, sizeof body, "]\t; mapped with the Gamepad app\n");
	cat (body, &n, sizeof body, "dpad = ");
	cat (body, &n, sizeof body, g_new.dpad == PAD_DPAD_HAT ? "hat" : g_new.dpad == PAD_DPAD_AXES ? "axes" : g_new.dpad == PAD_DPAD_BUTTONS ? "buttons" : "none");
	cat (body, &n, sizeof body, "\n");
	if (g_new.dpad == PAD_DPAD_HAT) { cat (body, &n, sizeof body, "hat = "); cati (body, &n, sizeof body, g_new.hat + 1); cat (body, &n, sizeof body, "\n"); }
	int xa = g_new.x_axis ? g_new.x_axis : 1, ya = g_new.y_axis ? g_new.y_axis : 2;
	cat (body, &n, sizeof body, "x_axis = "); cati (body, &n, sizeof body, xa);
	cat (body, &n, sizeof body, "\ny_axis = "); cati (body, &n, sizeof body, ya); cat (body, &n, sizeof body, "\n");
	cat (body, &n, sizeof body, "stick = "); cati (body, &n, sizeof body, g_new.dpad == PAD_DPAD_AXES ? 0 : 1); cat (body, &n, sizeof body, "\n");
	if (g_new.l2_axis) { cat (body, &n, sizeof body, "l2_axis = "); cati (body, &n, sizeof body, g_new.l2_axis); cat (body, &n, sizeof body, "\n"); }
	if (g_new.r2_axis) { cat (body, &n, sizeof body, "r2_axis = "); cati (body, &n, sizeof body, g_new.r2_axis); cat (body, &n, sizeof body, "\n"); }
	for (int i = 0; i < PAD_NBUTTONS; i++)
	{
		if (i < 4 && g_new.dpad != PAD_DPAD_BUTTONS) continue;
		cat (body, &n, sizeof body, pad_names[i]); cat (body, &n, sizeof body, " = ");
		cati (body, &n, sizeof body, g_new.btn[i]); cat (body, &n, sizeof body, "\n");
	}
	int m = 0; g_msg[0] = 0;
	if (write_section (body)) cat (g_msg, &m, sizeof g_msg, TR ("Saved in SD:/etc/gamepad.ini: every app uses it now."));
	else cat (g_msg, &m, sizeof g_msg, TR ("Could not write SD:/etc/gamepad.ini"));
	pad_config_reload ();
}

// ---- the window ------------------------------------------------------------------------------------
// The theme's look (uikit/paint.h): a key cap -- raised, or the accent while it is held -- and its
// label.
static void key_cap (Canvas &c, int x, int y, int w, int h, bool lit, const char *t)
{
	if (lit)
	{
		uk_rbox (c, x, y, w, h, 4, uk_tone (C_ACCENT, 150), uk_tone (C_ACCENT, 112));
		uk_rline (c, x, y, w, h, 4, uk_tone (C_ACCENT, 70), 220);
	}
	else uk_raised (c, x, y, w, h, 4, C_FACE);
	if (t[0]) uk_text_c (c, x, y, w, h, t, lit ? C_SEL_TEXT : C_TEXT);
}

static void pad_shape (Canvas &c, int x, int y, unsigned b)
{
	// a pad seen from above: d-pad left, face buttons right, shoulders on top, sticks' clicks
	uk_rbox (c, x, y + 20, 300, 130, 14, uk_tone (C_FACE, 118), uk_tone (C_FACE, 96));
	uk_rline (c, x, y + 20, 300, 130, 14, uk_tone (C_FACE, 60), 220);
	struct { int bit, dx, dy, w, h; const char *t; } k[] = {
		{ 8, 10, 0, 60, 16, "L" }, { 10, 80, 0, 50, 16, "L2" }, { 11, 170, 0, 50, 16, "R2" }, { 9, 230, 0, 60, 16, "R" },
		{ 0, 44, 40, 22, 22, "" }, { 1, 44, 84, 22, 22, "" }, { 2, 22, 62, 22, 22, "" }, { 3, 66, 62, 22, 22, "" },
		{ 7, 234, 40, 24, 22, "Y" }, { 4, 234, 84, 24, 22, "A" }, { 6, 210, 62, 24, 22, "X" }, { 5, 258, 62, 24, 22, "B" },
		{ 12, 112, 56, 34, 14, "SEL" }, { 13, 154, 56, 34, 14, "STA" }, { 16, 136, 34, 28, 14, "H" },
		{ 14, 104, 104, 34, 18, "L3" }, { 15, 162, 104, 34, 18, "R3" } };
	for (unsigned i = 0; i < sizeof k / sizeof k[0]; i++)
		key_cap (c, x + k[i].dx, y + k[i].dy, k[i].w, k[i].h, (b >> k[i].bit) & 1, k[i].t);
}

// ---- the keyboard as pad 1 (gamepad.h's [keyboard]: the emulators read it) -------------------------------------
// The Keyboard tab (g_pad == PAD_MAX): a key for each button (and the right stick), lit while held; a row chosen
// (Up / Down or a click) and Enter (or a click on it): the next key pressed is its (Esc: cancel). Saved at once.
static const char *const KROW[PAD_KEYS] = {
	TRN ("Up"), TRN ("Down"), TRN ("Left"), TRN ("Right"), TRN ("A (bottom)"), TRN ("B (right)"), TRN ("X (left)"),
	TRN ("Y (top)"), TRN ("L"), TRN ("R"), TRN ("L2"), TRN ("R2"), TRN ("Select"), TRN ("Start"), TRN ("L3"), TRN ("R3"),
	TRN ("Home"), TRN ("Right stick up"), TRN ("Right stick down"), TRN ("Right stick left"), TRN ("Right stick right") };
static int g_ksel = 0;
static bool g_kwait = false;				// waiting for the key of row g_ksel
#define KROWS	11					// rows a column
#define KY0	76
#define KRH	22
static void kbd_save (void)
{
	static char body[1024]; int n = 0; body[0] = 0;
	cat (body, &n, sizeof body, "[keyboard]\t; the keyboard as pad 1 (the emulators), set with the Gamepad app\n");
	for (int i = 0; i < PAD_KEYS; i++)
	{
		cat (body, &n, sizeof body, i < PAD_NBUTTONS ? pad_names[i] : pad_key_names[i - PAD_NBUTTONS]);
		cat (body, &n, sizeof body, " = "); cat (body, &n, sizeof body, g_pad_kbd[i] ? pad_key_word (g_pad_kbd[i]) : "none");
		cat (body, &n, sizeof body, "\n");
	}
	int m = 0; g_msg[0] = 0;
	if (write_section (body, "[keyboard]")) cat (g_msg, &m, sizeof g_msg, TR ("Saved in SD:/etc/gamepad.ini: the emulators use it now."));
	else cat (g_msg, &m, sizeof g_msg, TR ("Could not write SD:/etc/gamepad.ini"));
	pad_config_reload ();
}
static void kbd_draw (Canvas &c, int width, int height)
{
	c.text (14, 46, TR ("The keyboard as pad 1 in the emulators: a key for each button."), C_TEXT);
	for (int i = 0; i < PAD_KEYS; i++)
	{
		int col = i / KROWS, row = i % KROWS, x = 14 + col * 292, y = KY0 + row * KRH;
		bool sel = i == g_ksel, held = g_pad_kbd[i] && kapi_key_held (g_pad_kbd[i]);
		if (sel) uk_hilite (c, x - 4, y - 2, 284, KRH - 2, 5, true);
		c.text (x + 4, y + 1, TR (KROW[i]), sel ? C_SEL_TEXT : C_TEXT);
		const char *w = g_kwait && sel ? "?" : g_pad_kbd[i] ? pad_key_word (g_pad_kbd[i]) : "-";
		key_cap (c, x + 170, y - 1, 96, KRH - 4, held || (g_kwait && sel), w);
	}
	int y = KY0 + KROWS * KRH + 8;
	if (g_kwait) { char s[160]; int n = 0; s[0] = 0; cat (s, &n, sizeof s, TR ("Press the key for ")); cat (s, &n, sizeof s, TR (KROW[g_ksel])); cat (s, &n, sizeof s, TR ("   (Esc: cancel)")); uk_text_l (c, 14, y, 16, s, uk_tone (C_ACCENT, 84), 2); }
	else c.text (14, y, TR ("Up / Down: a button   Enter or a click: its key   Forget: the keys of the start"), C_DIS);
	(void) width; (void) height;
}

class PadRoot : public Root
{
public:
	PadRoot () : Root (W, H, TR ("Gamepad")) {}
	void onDraw () override
	{
		canvas.clear (C_BG);
		unsigned msg = uk_tone (C_ACCENT, 84);				// (a message: the dark accent)
		char s[200]; int n;
		uk_rbox (canvas, 0, 0, W, 38, 0, uk_tone (C_FACE, 170), uk_tone (C_FACE, 130));
		uk_etch_h (canvas, 0, 38, W, C_FACE);
		for (int i = 0; i <= PAD_MAX; i++)				// the tabs (the pads, the keyboard): the current one in the accent
		{
			struct kapi_pad p; bool there = i == PAD_MAX || kapi_pad_state (i, &p) != 0, cur = i == g_pad;
			int tx = 10 + i * TABW;
			if (cur)
			{
				uk_hilite (canvas, tx, 8, TABW - 6, 24, 6, true);
				uk_rline (canvas, tx, 8, TABW - 6, 24, 6, uk_tone (C_ACCENT, 70), 200);
			}
			else uk_raised (canvas, tx, 8, TABW - 6, 24, 6, C_FACE);
			n = 0; s[0] = 0;
			if (i == PAD_MAX) cat (s, &n, sizeof s, TR ("Keyboard"));
			else { cat (s, &n, sizeof s, TR ("Pad ")); cati (s, &n, sizeof s, i + 1); if (!there) cat (s, &n, sizeof s, " -"); }
			uk_text_l (canvas, tx + 12, 8, 24, s, cur ? C_SEL_TEXT : there ? C_TEXT : C_DIS, cur ? 2 : 0);
		}
		int y = 44;
		if (g_pad == PAD_MAX) { kbd_draw (canvas, width, height); if (g_msg[0]) canvas.text (14, height - 70, g_msg, msg); return; }
		if (!g_there)
		{
			canvas.text (14, y, TR ("No gamepad in this slot. Plug a USB gamepad in (Xbox 360 / One,"), C_TEXT);
			canvas.text (14, y + 18, TR ("PlayStation 3 / 4, Switch Pro, or any USB HID gamepad)."), C_TEXT);
			if (g_msg[0]) canvas.text (14, height - 70, g_msg, msg);
			return;
		}
		struct pad_map m; int src = pad_map_for (&g_raw, &m);
		n = 0; s[0] = 0;
		cathex (s, &n, sizeof s, g_raw.vid, 4); cat (s, &n, sizeof s, ":"); cathex (s, &n, sizeof s, g_raw.pid, 4);
		cat (s, &n, sizeof s, (g_raw.props & 1) ? TR ("   known to Circle") : TR ("   generic HID pad"));
		cat (s, &n, sizeof s, TR ("   mapping: "));
		cat (s, &n, sizeof s, src == 2 ? TR ("its own (gamepad.ini)") : src == 1 ? TR ("[default] of gamepad.ini") : TR ("built-in"));
		canvas.text (14, y, s, C_TEXT); y += 26;
		// raw buttons
		canvas.text (14, y, TR ("Buttons"), C_DIS);
		int nb = g_raw.nbuttons > 32 ? 32 : g_raw.nbuttons;
		if (nb < 1) nb = 16;
		for (int i = 0; i < nb; i++)
		{
			bool d = (g_raw.buttons >> i) & 1;
			int bx = 90 + (i % 16) * 31, by = y - 2 + (i / 16) * 22;
			n = 0; s[0] = 0; cati (s, &n, sizeof s, i + 1);
			key_cap (canvas, bx, by, 28, 20, d, s);
		}
		y += nb > 16 ? 50 : 28;
		// axes (a gauge each: a sunken track, a tint of the accent up to the value), hats
		canvas.text (14, y, TR ("Axes"), C_DIS);
		for (int i = 0; i < g_raw.naxes && i < 8; i++)
		{
			int bx = 90 + (i % 4) * 124, by = y + (i / 4) * 22;
			int lo = g_raw.axes[i].minimum, hi = g_raw.axes[i].maximum, v = g_raw.axes[i].value;
			uk_sunken (canvas, bx, by, 110, 18, 4, C_FIELD);
			if (hi > lo)
			{
				int w = (v - lo) * 110 / (hi - lo); if (w < 0) w = 0; if (w > 110) w = 110;
				if (w > 2) uk_rbox (canvas, bx + 1, by + 1, w - 2, 16, 3, uk_mix (C_FIELD, C_ACCENT, 120), uk_mix (C_FIELD, C_ACCENT, 90));
			}
			n = 0; s[0] = 0; cati (s, &n, sizeof s, i + 1); cat (s, &n, sizeof s, ": "); cati (s, &n, sizeof s, v);
			canvas.text (bx + 4, by + 1, s, C_FIELD_TEXT);
		}
		y += g_raw.naxes > 4 ? 48 : 26;
		n = 0; s[0] = 0;
		static const char *const DIR[8] = { TRN ("N"), TRN ("NE"), TRN ("E"), TRN ("SE"), TRN ("S"), TRN ("SW"), TRN ("W"), TRN ("NW") };
		for (int i = 0; i < g_raw.nhats; i++) { cat (s, &n, sizeof s, i ? ", " : "  "); int h = g_raw.hats[i]; cat (s, &n, sizeof s, h >= 0 && h < 8 ? TR (DIR[h]) : TR ("centre")); }
		if (!g_raw.nhats) cat (s, &n, sizeof s, TR ("  none"));
		canvas.text (14, y, TR ("Hats"), C_DIS);
		canvas.text (14 + uk_tw (TR ("Hats")), y, s, C_TEXT); y += 28;
		// what the apps see
		int lx, ly, rx, ry;
		unsigned b = pad_apply (&g_raw, &m, &lx, &ly, &rx, &ry);
		canvas.text (14, y, TR ("What the apps see:"), C_DIS);
		pad_shape (canvas, 150, y, b);
		y += 160;
		if (g_mapping)						// the step: a band of a tint of the accent
		{
			uk_rbox (canvas, 6, y - 5, W - 12, 44, 6, uk_mix (C_FIELD, C_ACCENT, 70), uk_mix (C_FIELD, C_ACCENT, 46));
			uk_rline (canvas, 6, y - 5, W - 12, 44, 6, C_ACCENT, 200);
			n = 0; s[0] = 0;
			if (g_step >= PAD_NBUTTONS) cat (s, &n, sizeof s, TR ("Release every button..."));
			else { cat (s, &n, sizeof s, g_waitRelease ? TR ("Release, then press ") : TR ("Press ")); cat (s, &n, sizeof s, TR (STEP_TEXT[g_step])); }
			uk_text_l (canvas, 14, y, 16, s, C_FIELD_TEXT, 2);
			canvas.text (14, y + 18, TR ("Esc: the pad has none (skip)   Backspace: cancel"), uk_mix (C_FIELD, C_FIELD_TEXT, 150));
		}
		else canvas.text (14, y, TR ("Map Buttons... (M) if the buttons above are not in their places."), C_DIS);
		if (g_msg[0]) canvas.text (14, height - 70, g_msg, msg);
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		static bool down = false;
		if (bl && !down && my >= 8 && my < 32 && mx >= 10 && mx < 10 + (PAD_MAX + 1) * TABW) { g_pad = (mx - 10) / TABW; g_mapping = false; g_kwait = false; invalidate (true); }
		else if (bl && !down && g_pad == PAD_MAX && my >= KY0 - 2 && my < KY0 + KROWS * KRH && mx >= 10 && mx < 10 + 2 * 292)
		{
			int i = ((mx - 10) / 292) * KROWS + (my - KY0 + 2) / KRH;
			if (i >= 0 && i < PAD_KEYS) { g_kwait = i == g_ksel && !g_kwait; g_ksel = i; invalidate (true); }
		}
		down = bl != 0;
		return true;
	}
	bool onKey (long k) override
	{
		if (g_pad == PAD_MAX)					// the keyboard's keys
		{
			if (g_kwait)
			{
				g_kwait = false;
				if (k != 27)
				{
					int c = k >= 'A' && k <= 'Z' ? (int) k + 32 : (int) k;
					if (c == '\n' || c == '\r') c = KEY_ENTER;
					for (int i = 0; i < PAD_KEYS; i++) if (i != g_ksel && g_pad_kbd[i] == c) g_pad_kbd[i] = 0;	// (a key does one thing)
					g_pad_kbd[g_ksel] = c;
					kbd_save ();
				}
				invalidate (true); return true;
			}
			if (k == KEY_UP && g_ksel > 0) { g_ksel--; invalidate (true); return true; }
			if (k == KEY_DOWN && g_ksel < PAD_KEYS - 1) { g_ksel++; invalidate (true); return true; }
			if (k == KEY_LEFT && g_ksel >= KROWS) { g_ksel -= KROWS; invalidate (true); return true; }
			if (k == KEY_RIGHT && g_ksel + KROWS < PAD_KEYS) { g_ksel += KROWS; invalidate (true); return true; }
			if (k == KEY_ENTER || k == '\n') { g_kwait = true; invalidate (true); return true; }
		}
		if (g_mapping)
		{
			if (k == 27) { if (!g_waitRelease && g_step < PAD_NBUTTONS) next_step (); g_waitRelease = false; invalidate (true); return true; }
			if (k == KEY_BACKSPACE) { g_mapping = false; int n = 0; g_msg[0] = 0; cat (g_msg, &n, sizeof g_msg, TR ("Mapping cancelled.")); invalidate (true); return true; }
		}
		if (k >= '1' && k <= '5') { g_pad = (int) (k - '1'); g_mapping = false; g_kwait = false; invalidate (true); return true; }
		if (k == 'm' || k == 'M') { map_start (); invalidate (true); return true; }
		return Root::onKey (k);
	}
};

static void on_map () { if (g_pad == PAD_MAX) g_kwait = true; else map_start (); g_root->invalidate (true); }
static void on_forget ()
{
	if (g_pad == PAD_MAX)					// the keyboard: the keys of the start
	{
		int n = 0; g_msg[0] = 0;
		if (write_section ("", "[keyboard]")) cat (g_msg, &n, sizeof g_msg, TR ("The keyboard's keys are the first ones again."));
		pad_config_reload ();
		g_root->invalidate (true);
		return;
	}
	if (!g_there) return;
	int n = 0; g_msg[0] = 0;
	if (write_section ("")) cat (g_msg, &n, sizeof g_msg, TR ("This pad model's mapping was removed."));
	pad_config_reload ();
	g_root->invalidate (true);
}
static void on_reload () { pad_config_reload (); int n = 0; g_msg[0] = 0; cat (g_msg, &n, sizeof g_msg, TR ("gamepad.ini read again.")); g_root->invalidate (true); }
static void on_quit () { kapi_exit (0); }
static void bt_map (Widget &) { on_map (); }
static void bt_forget (Widget &) { on_forget (); }
static void bt_reload (Widget &) { on_reload (); }

int main (void)
{
	ft_uikit_install ("DejaVu Sans", 13);		// (before the widgets; false: the bitmap font)
	uk_lang_init ();				// the words in the system's language (before the widgets)
	PadRoot root;
	if (root.canvas.px == 0) return 1;
	g_root = &root;
	static Menu menu;
	menu.menu (TRC ("menu", "Pad"));
	menu.item (TR ("Map Buttons..."),      "M",  0, on_map);
	menu.item (TR ("Forget Mapping"),      "",   0, on_forget);
	menu.item (TR ("Reload gamepad.ini"),  "",   0, on_reload);
	menu.separator ();
	menu.item (TR ("Quit"),                "^Q", UK_CTRL ('Q'), on_quit);
	menu.publish ();
	int by = root.height - 40;			// (the commands, also as buttons: an applet has no menu)
	root.addChild (new Button (root.width - 440, by, 150, 30, TR ("Map Buttons..."), bt_map));
	root.addChild (new Button (root.width - 282, by, 150, 30, TR ("Forget Mapping"), bt_forget));
	root.addChild (new Button (root.width - 124, by, 112, 30, TR ("Reload"), bt_reload));
	root.attach ();
	unsigned lastSeq = 0; bool lastThere = false; int lastPad = -1;
	while (!uk_quit ())
	{
		uk_pump ();
		g_there = g_pad < PAD_MAX && kapi_pad_state (g_pad, &g_raw) != 0;
		if (g_pad == PAD_MAX)					// (the keys lit while held)
		{
			static unsigned lastHeld;
			unsigned h = 0;
			for (int i = 0; i < PAD_KEYS; i++) if (g_pad_kbd[i] && kapi_key_held (g_pad_kbd[i])) h |= 1u << i;
			if (h != lastHeld) { lastHeld = h; root.invalidate (true); }
		}
		map_poll ();
		if (g_there != lastThere || g_pad != lastPad || (g_there && g_raw.seq != lastSeq))
		{
			lastThere = g_there; lastPad = g_pad; lastSeq = g_raw.seq;
			root.invalidate (true);
		}
		if (!root.valid) { root.draw (); uk_present (); }
		kapi_msleep (16);
	}
	return 0;
}
