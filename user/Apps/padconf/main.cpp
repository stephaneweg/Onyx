//
// padconf -- the Gamepad settings: the USB gamepads plugged in (Circle's drivers, kapi v50),
// what each one sends (its buttons, axes and hats, live) and what the apps see (the PAD_*
// buttons of user/gamepad.h, on a drawn pad), and "Map Buttons..." -- press each button when
// asked -- which writes the pad model's section of SD:/etc/gamepad.ini.
//
//   * Pads 1-4: click the tabs or keys 1-4. Pad > Map Buttons... (M), Forget Mapping (the
//     pad's section removed: back to the built-in / [default] mapping), Reload gamepad.ini.
//   * While mapping: press the button asked for; Esc = this pad has no such button (skip),
//     Backspace = cancel.
//
#include "kapi.h"
#include "gamepad.h"
#include "wtk/wtk.h"

using namespace wtk;

#define W	600
#define H	440

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
	"UP on the d-pad", "DOWN on the d-pad", "LEFT on the d-pad", "RIGHT on the d-pad",
	"the BOTTOM face button (Xbox A, PlayStation Cross, Nintendo B)",
	"the RIGHT face button (Xbox B, PlayStation Circle, Nintendo A)",
	"the LEFT face button (Xbox X, PlayStation Square, Nintendo Y)",
	"the TOP face button (Xbox Y, PlayStation Triangle, Nintendo X)",
	"the LEFT shoulder button (L / L1 / LB)", "the RIGHT shoulder button (R / R1 / RB)",
	"the LEFT trigger (L2 / LT / ZL)", "the RIGHT trigger (R2 / RT / ZR)",
	"SELECT (Back / Share / -)", "START (Options / +)",
	"the LEFT stick's click (L3)", "the RIGHT stick's click (R3)", "HOME (Guide / PS)" };

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
	if (!g_there) { int n = 0; g_msg[0] = 0; cat (g_msg, &n, sizeof g_msg, "No gamepad here: plug one in."); return; }
	g_mapping = true; g_step = 0; g_waitRelease = false;
	g_base = g_raw;
	for (int i = 0; i < g_base.nhats; i++) g_base.hats[i] = 8;	// (a rest hat: centred)
	pad_map_default (&g_new, 0, 0);
	for (int i = 0; i < PAD_NBUTTONS; i++) g_new.btn[i] = 0;
	g_new.dpad = PAD_DPAD_NONE; g_new.x_axis = g_new.y_axis = 0;
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
	unsigned nb = p.buttons & ~g_base.buttons;
	if (nb)
	{
		int b = 0; while (!((nb >> b) & 1)) b++;
		g_new.btn[g_step] = b + 1;
		if (g_step < 4) g_new.dpad = PAD_DPAD_BUTTONS;
		next_step ();
	}
}

// Replace (or add) the pad model's section in SD:/etc/gamepad.ini.
static bool write_section (const char *body)
{
	static char buf[8192], out[8192];
	int n = 0;
	void *f = kapi_open ("SD:/etc/gamepad.ini");
	if (f) { n = kapi_read (f, buf, sizeof buf - 1); kapi_close (f); if (n < 0) n = 0; }
	buf[n] = 0;
	char head[16]; int hn = 0; head[0] = 0;
	cat (head, &hn, sizeof head, "["); cathex (head, &hn, sizeof head, g_raw.vid, 4); cat (head, &hn, sizeof head, ":");
	cathex (head, &hn, sizeof head, g_raw.pid, 4); cat (head, &hn, sizeof head, "]");
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
	for (int i = 0; i < PAD_NBUTTONS; i++)
	{
		if (i < 4 && g_new.dpad != PAD_DPAD_BUTTONS) continue;
		cat (body, &n, sizeof body, pad_names[i]); cat (body, &n, sizeof body, " = ");
		cati (body, &n, sizeof body, g_new.btn[i]); cat (body, &n, sizeof body, "\n");
	}
	int m = 0; g_msg[0] = 0;
	if (write_section (body)) cat (g_msg, &m, sizeof g_msg, "Saved in SD:/etc/gamepad.ini: every app uses it now.");
	else cat (g_msg, &m, sizeof g_msg, "Could not write SD:/etc/gamepad.ini");
	pad_config_reload ();
}

// ---- the window ------------------------------------------------------------------------------------
static void pad_shape (Canvas &c, int x, int y, unsigned b)
{
	// a pad seen from above: d-pad left, face buttons right, shoulders on top, sticks' clicks
	unsigned off = 0x00404650, on = 0x0060D060, txt = 0x00E0E0E0;
	c.fillRect (x, y + 20, 300, 130, 0x00262A30); c.frameRect (x, y + 20, 300, 130, 0x00505860);
	struct { int bit, dx, dy, w, h; const char *t; } k[] = {
		{ 8, 10, 0, 60, 16, "L" }, { 10, 80, 0, 50, 16, "L2" }, { 11, 170, 0, 50, 16, "R2" }, { 9, 230, 0, 60, 16, "R" },
		{ 0, 44, 40, 22, 22, "" }, { 1, 44, 84, 22, 22, "" }, { 2, 22, 62, 22, 22, "" }, { 3, 66, 62, 22, 22, "" },
		{ 7, 234, 40, 24, 22, "Y" }, { 4, 234, 84, 24, 22, "A" }, { 6, 210, 62, 24, 22, "X" }, { 5, 258, 62, 24, 22, "B" },
		{ 12, 112, 56, 34, 14, "SEL" }, { 13, 154, 56, 34, 14, "STA" }, { 16, 136, 34, 28, 14, "H" },
		{ 14, 104, 104, 34, 18, "L3" }, { 15, 162, 104, 34, 18, "R3" } };
	for (unsigned i = 0; i < sizeof k / sizeof k[0]; i++)
	{
		bool lit = (b >> k[i].bit) & 1;
		c.fillRect (x + k[i].dx, y + k[i].dy, k[i].w, k[i].h, lit ? on : off);
		if (k[i].t[0]) c.text (x + k[i].dx + 3, y + k[i].dy + (k[i].h - 16) / 2, k[i].t, txt);
	}
}

class PadRoot : public Root
{
public:
	PadRoot () : Root (W, H, "Gamepad") {}
	void onDraw () override
	{
		canvas.clear (0x001C1F24);
		char s[200]; int n;
		for (int i = 0; i < PAD_MAX; i++)				// the tabs
		{
			struct kapi_pad p; bool there = kapi_pad_state (i, &p) != 0;
			canvas.fillRect (10 + i * 100, 8, 94, 24, i == g_pad ? 0x00405070 : 0x002C3038);
			n = 0; s[0] = 0; cat (s, &n, sizeof s, "Pad "); cati (s, &n, sizeof s, i + 1); if (!there) cat (s, &n, sizeof s, " -");
			canvas.text (22 + i * 100, 12, s, there ? 0x00FFFFFF : 0x00808080);
		}
		int y = 44;
		if (!g_there)
		{
			canvas.text (14, y, "No gamepad in this slot. Plug a USB gamepad in (Xbox 360 / One,", 0x00C8C8C8);
			canvas.text (14, y + 18, "PlayStation 3 / 4, Switch Pro, or any USB HID gamepad).", 0x00C8C8C8);
			if (g_msg[0]) canvas.text (14, H - 26, g_msg, 0x0080E080);
			return;
		}
		struct pad_map m; int src = pad_map_for (&g_raw, &m);
		n = 0; s[0] = 0;
		cathex (s, &n, sizeof s, g_raw.vid, 4); cat (s, &n, sizeof s, ":"); cathex (s, &n, sizeof s, g_raw.pid, 4);
		cat (s, &n, sizeof s, (g_raw.props & 1) ? "   known to Circle" : "   generic HID pad");
		cat (s, &n, sizeof s, "   mapping: ");
		cat (s, &n, sizeof s, src == 2 ? "its own (gamepad.ini)" : src == 1 ? "[default] of gamepad.ini" : "built-in");
		canvas.text (14, y, s, 0x00FFFFFF); y += 26;
		// raw buttons
		canvas.text (14, y, "Buttons", 0x00A0A8B0);
		int nb = g_raw.nbuttons > 32 ? 32 : g_raw.nbuttons;
		if (nb < 1) nb = 16;
		for (int i = 0; i < nb; i++)
		{
			bool d = (g_raw.buttons >> i) & 1;
			int bx = 90 + (i % 16) * 31, by = y - 2 + (i / 16) * 22;
			canvas.fillRect (bx, by, 28, 20, d ? 0x0060D060 : 0x00383C44);
			n = 0; s[0] = 0; cati (s, &n, sizeof s, i + 1); canvas.text (bx + (i < 9 ? 10 : 6), by + 2, s, d ? 0 : 0x00C0C0C0);
		}
		y += nb > 16 ? 50 : 28;
		// axes, hats
		canvas.text (14, y, "Axes", 0x00A0A8B0);
		for (int i = 0; i < g_raw.naxes && i < 8; i++)
		{
			int bx = 90 + (i % 4) * 124, by = y + (i / 4) * 22;
			int lo = g_raw.axes[i].minimum, hi = g_raw.axes[i].maximum, v = g_raw.axes[i].value;
			canvas.fillRect (bx, by, 110, 18, 0x00383C44);
			if (hi > lo) { int w = (v - lo) * 110 / (hi - lo); if (w < 0) w = 0; if (w > 110) w = 110; canvas.fillRect (bx, by, w, 18, 0x00406890); }
			n = 0; s[0] = 0; cati (s, &n, sizeof s, i + 1); cat (s, &n, sizeof s, ": "); cati (s, &n, sizeof s, v);
			canvas.text (bx + 4, by + 1, s, 0x00FFFFFF);
		}
		y += g_raw.naxes > 4 ? 48 : 26;
		n = 0; s[0] = 0; cat (s, &n, sizeof s, "Hats");
		static const char *const DIR[8] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
		for (int i = 0; i < g_raw.nhats; i++) { cat (s, &n, sizeof s, i ? ", " : "  "); int h = g_raw.hats[i]; cat (s, &n, sizeof s, h >= 0 && h < 8 ? DIR[h] : "centre"); }
		if (!g_raw.nhats) cat (s, &n, sizeof s, "  none");
		canvas.text (14, y, s, 0x00A0A8B0); y += 28;
		// what the apps see
		int lx, ly, rx, ry;
		unsigned b = pad_apply (&g_raw, &m, &lx, &ly, &rx, &ry);
		canvas.text (14, y, "What the apps see:", 0x00A0A8B0);
		pad_shape (canvas, 150, y, b);
		y += 160;
		if (g_mapping)
		{
			canvas.fillRect (0, y - 4, W, 44, 0x00303848);
			n = 0; s[0] = 0;
			if (g_step >= PAD_NBUTTONS) cat (s, &n, sizeof s, "Release every button...");
			else { cat (s, &n, sizeof s, g_waitRelease ? "Release, then press " : "Press "); cat (s, &n, sizeof s, STEP_TEXT[g_step]); }
			canvas.text (14, y, s, 0x00FFFF80);
			canvas.text (14, y + 18, "Esc: the pad has none (skip)   Backspace: cancel", 0x00A0A0A0);
		}
		else canvas.text (14, y, "Pad > Map Buttons... (M) if the buttons above are not in their places.", 0x00A0A0A0);
		if (g_msg[0]) canvas.text (14, H - 26, g_msg, 0x0080E080);
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		static bool down = false;
		if (bl && !down && my >= 8 && my < 32 && mx >= 10 && mx < 10 + PAD_MAX * 100) { g_pad = (mx - 10) / 100; g_mapping = false; invalidate (true); }
		down = bl != 0;
		return true;
	}
	bool onKey (long k) override
	{
		if (g_mapping)
		{
			if (k == 27) { if (!g_waitRelease && g_step < PAD_NBUTTONS) next_step (); g_waitRelease = false; invalidate (true); return true; }
			if (k == KEY_BACKSPACE) { g_mapping = false; int n = 0; g_msg[0] = 0; cat (g_msg, &n, sizeof g_msg, "Mapping cancelled."); invalidate (true); return true; }
		}
		if (k >= '1' && k <= '4') { g_pad = (int) (k - '1'); g_mapping = false; invalidate (true); return true; }
		if (k == 'm' || k == 'M') { map_start (); invalidate (true); return true; }
		return Root::onKey (k);
	}
};

static void on_map () { map_start (); g_root->invalidate (true); }
static void on_forget ()
{
	if (!g_there) return;
	int n = 0; g_msg[0] = 0;
	if (write_section ("")) cat (g_msg, &n, sizeof g_msg, "This pad model's mapping was removed.");
	pad_config_reload ();
	g_root->invalidate (true);
}
static void on_reload () { pad_config_reload (); int n = 0; g_msg[0] = 0; cat (g_msg, &n, sizeof g_msg, "gamepad.ini read again."); g_root->invalidate (true); }
static void on_quit () { kapi_exit (0); }

int main (void)
{
	PadRoot root;
	if (root.canvas.px == 0) return 1;
	g_root = &root;
	static Menu menu;
	menu.menu ("Pad");
	menu.item ("Map Buttons...",      "M",  0, on_map);
	menu.item ("Forget Mapping",      "",   0, on_forget);
	menu.item ("Reload gamepad.ini",  "",   0, on_reload);
	menu.separator ();
	menu.item ("Quit",                "^Q", WK_CTRL ('Q'), on_quit);
	menu.publish ();
	root.attach ();
	unsigned lastSeq = 0; bool lastThere = false; int lastPad = -1;
	while (!should_exit ())
	{
		pump_events ();
		g_there = kapi_pad_state (g_pad, &g_raw) != 0;
		map_poll ();
		if (g_there != lastThere || g_pad != lastPad || (g_there && g_raw.seq != lastSeq))
		{
			lastThere = g_there; lastPad = g_pad; lastSeq = g_raw.seq;
			root.invalidate (true);
		}
		if (!root.valid) { root.draw (); kapi_present (); }
		kapi_msleep (16);
	}
	return 0;
}
