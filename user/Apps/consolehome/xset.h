//
// xset.h -- the console's settings in the XMB's own style (docs/COMPACT-SHELL-STUDY.md §18, the user's choice of
// 2026-10-09: "applets specific to the console mode, usable with a pad, a virtual keyboard where text is needed").
// Part of main.cpp (one unit), included after xmb.h.
//
// The XMB's Settings column holds eight pages: Sound, Gamepad, Keyboard & Mouse, Language & Region, Wi-Fi, Packages,
// Mode, Display. A enters one: the pages become the parent column (faded at the left), the page's settings the list --
// a row is a label and its value right-aligned, a help line under the focused one. Left / Right change a value (at
// once, kept at once, as the desktop applets do), A acts or goes deeper, B comes back, Y is the page's second action.
// Text is typed on the virtual keyboard (osk_*: QWERTY, AZERTY or QWERTZ as the keyboard's layout), a question is the
// small dialog (dlg_*: Left / Right, A, B the safe choice; a countdown when one is given). Every setting is the kits':
// SystemKit (volume.h, locale.h, session.h, display.h, input.h), NetKit (wifi.h), gamepad.h, pkg/pkgjob.h.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#include "netkit/netkit.h"
#include "audiokit/audiokit.h"
#include "pkg/pkglib.h"
#include "pkg/pkgjob.h"

// The pad's buttons that have no key (pad_poll gives them while the settings are up)
enum { K_A = 0x7101, K_B, K_X, K_Y, K_START, K_L3 };

// ---- the pages, the screens -------------------------------------------------------------------------------------------
enum { SC_ROMS, SC_SOUND, SC_PAD, SC_KBD, SC_LANG, SC_WIFI, SC_PKG, SC_MODE, SC_DISP,	// (the pages: SP[] order)
	SC_PADINFO, SC_WIZ, SC_KEYS, SC_NET, SC_PKGLIST, SC_PKGONE, SC_GAMES, SC_FOLDER, SC_BROWSE };
struct SPage { const char *name, *app, *help; };
static const SPage SP[] = {
	{ TRN ("Games"), "gamelib", TRN ("The folders the games are looked for in, on the card and the USB sticks") },
	{ TRN ("Sound"), "soundconf", TRN ("The volume, the output, each program's level") },
	{ TRN ("Gamepad"), "padconf", TRN ("The pads, their buttons, the keyboard as a pad") },
	{ TRN ("Keyboard & Mouse"), "keyconf", TRN ("The keyboard's layout, the mouse's wheel") },
	{ TRN ("Language & Region"), "langconf", TRN ("The system's language, the time zone") },
	{ TRN ("Wi-Fi"), "wpaconf", TRN ("The networks around: join one, forget one, the country") },
	{ TRN ("Packages"), "pkgman", TRN ("The updates, the installed programs, the others to install") },
	{ TRN ("Mode"), "modeconf", TRN ("Desktop, pocket or console: the interface") },
	{ TRN ("Display"), "displayconf", TRN ("The screen's resolution, the games' own") } };
static_assert (sizeof SP / sizeof SP[0] == SP_N, "SP_N: the pages");
static const char *sp_name (int i) { return TR (SP[i].name); }
static const char *sp_help (int i) { return TR (SP[i].help); }

struct Scr { int kind, arg, sel, top; };
static Scr g_ss[6];
static int g_nss;					// 0: the settings are not up
static Scr &scr (void) { return g_ss[g_nss - 1]; }

// ---- the rows ----------------------------------------------------------------------------------------------------------
enum { R_HEAD, R_INFO, R_ACTION, R_SUB, R_CHOICE, R_SLIDER, R_TOGGLE, R_NET, R_RADIO };
struct SRow
{
	int kind, id, arg;
	char label[72], value[80], help[160];
	App *icon;
	int v, max;					// a slider's value and its top; a network's bars (v)
	int pct;					// a progress bar on the row (-1 none)
	bool on, warn, good, lock;
};
#define MAXSR	128
static SRow g_sr[MAXSR];
static int g_nsr;
static SRow &row_add (int kind, int id, const char *label, const char *value = "", const char *help = "")
{
	static SRow s_spare;
	SRow &r = g_nsr < MAXSR ? g_sr[g_nsr++] : s_spare;
	memset (&r, 0, sizeof r);
	r.kind = kind; r.id = id; r.pct = -1;
	fs_copy (r.label, label ? label : "", sizeof r.label);
	fs_copy (r.value, value ? value : "", sizeof r.value);
	fs_copy (r.help, help ? help : "", sizeof r.help);
	return r;
}
static bool selectable (const SRow &r) { return r.kind != R_HEAD; }

// ---- the flash: a word at the top right for two seconds (a value set, a result) -------------------------------------
static char g_flash[120];
static unsigned g_flashT;
static void flash (const char *s) { fs_copy (g_flash, s, sizeof g_flash); g_flashT = kapi_get_ticks (); g_homeDirty = true; }

static void ws_cat (char *o, int cap, const char *a, const char *b = 0, const char *c = 0, const char *d = 0)
{
	int k = (int) strlen (o);
	if (a) lx_cat (o, cap, &k, a);
	if (b) lx_cat (o, cap, &k, b);
	if (c) lx_cat (o, cap, &k, c);
	if (d) lx_cat (o, cap, &k, d);
}
static void size_words (char *o, int cap, int w, int h)
{
	o[0] = 0;
	char a[12], b[12]; ax_itoa (w, a); ax_itoa (h, b);
	ws_cat (o, cap, a, " x ", b);
}
static bool contains_ci (const char *s, const char *q)
{
	if (!q[0]) return true;
	for (; *s; s++)
	{
		int k = 0;
		while (q[k] && s[k] && (s[k] | 32) == (q[k] | 32)) k++;
		if (!q[k]) return true;
	}
	return false;
}

// ---- the dialog -------------------------------------------------------------------------------------------------------
enum { D_KEEP = 1, D_FORGET_NET, D_FORGET_PAD, D_MODE, D_WAIT, D_REMOVE, D_RESTART, D_UNWATCH };
struct Dlg { bool on; int purpose, arg; char title[80], text[400]; const char *b[3]; int n, sel, safe; unsigned t0, ticks; };
static Dlg g_dlg;
static void dlg_open (int purpose, int arg, const char *title, const char *text, const char *b0, const char *b1, const char *b2 = 0, int safe = 1, unsigned ticks = 0)
{
	memset (&g_dlg, 0, sizeof g_dlg);
	g_dlg.on = true; g_dlg.purpose = purpose; g_dlg.arg = arg;
	fs_copy (g_dlg.title, title, sizeof g_dlg.title); fs_copy (g_dlg.text, text, sizeof g_dlg.text);
	g_dlg.b[0] = b0; g_dlg.b[1] = b1; g_dlg.b[2] = b2; g_dlg.n = b2 ? 3 : 2;
	g_dlg.safe = safe; g_dlg.sel = 0; g_dlg.ticks = ticks; g_dlg.t0 = kapi_get_ticks ();
	g_homeDirty = true;
}
static void dlg_done (int choice);

// ---- the virtual keyboard ---------------------------------------------------------------------------------------------
enum { O_PASS = 1, O_HIDDEN, O_SEARCH, O_TRY };
struct Osk { bool on; int purpose, min, cap; char label[96], t[80]; int caret, r, c, page; bool secret, show; unsigned lastT; };
static Osk g_osk;
static const char *const OSK_ROWS[3][4] = {			// the letters' pages: QWERTY, AZERTY, QWERTZ
	{ "1234567890", "qwertyuiop", "asdfghjkl-", "zxcvbnm,._" },
	{ "1234567890", "azertyuiop", "qsdfghjklm", "wxcvbn,.-_" },
	{ "1234567890", "qwertzuiop", "asdfghjkl-", "yxcvbnm,._" } };
static const char *const OSK_SYM[4][10] = {
	{ "!", "@", "#", "$", "%", "^", "&", "*", "(", ")" },
	{ "~", "=", "+", "[", "]", "{", "}", ";", ":", "'" },
	{ "\"", "<", ">", "/", "?", "\\", "|", "`", "\xE2\x82\xAC", "\xC2\xA3" },
	{ "1", "2", "3", "4", "5", "6", "7", "8", "9", "0" } };
// The special row: Shift, ?123, Space, <, >, Delete, Done -- their widths in tenths of a key + gap
static const int OSK_SPW[7] = { 15, 15, 20, 10, 10, 15, 15 };
static int g_oskKind;					// OSK_ROWS's: the keyboard's layout
static void osk_open (int purpose, const char *label, const char *text, bool secret, int min = 0, int cap = 63)
{
	memset (&g_osk, 0, sizeof g_osk);
	g_osk.on = true; g_osk.purpose = purpose; g_osk.secret = secret; g_osk.min = min; g_osk.cap = cap < 79 ? cap : 79;
	fs_copy (g_osk.label, label, sizeof g_osk.label); fs_copy (g_osk.t, text ? text : "", sizeof g_osk.t);
	g_osk.caret = (int) strlen (g_osk.t); g_osk.r = 1;
	char km[16] = ""; input_keymap_now (km, sizeof km);
	int k = input_keymap_kind (km);
	g_oskKind = k == 1 ? 1 : k == 2 ? 2 : 0;
	g_homeDirty = true;
}
static const char *osk_key (int r, int c)		// the grid's key (r 0..3) on the page shown
{
	static char one[2];
	if (g_osk.page == 2) return OSK_SYM[r == 0 ? 3 : r - 1][c];
	one[0] = OSK_ROWS[g_oskKind][r][c]; one[1] = 0;
	if (g_osk.page == 1 && one[0] >= 'a' && one[0] <= 'z') one[0] -= 32;
	return one;
}
static void osk_insert (const char *s)
{
	int l = (int) strlen (s), n = (int) strlen (g_osk.t);
	if (n + l > g_osk.cap) return;
	memmove (g_osk.t + g_osk.caret + l, g_osk.t + g_osk.caret, (size_t) (n - g_osk.caret + 1));
	memcpy (g_osk.t + g_osk.caret, s, (size_t) l);
	g_osk.caret += l; g_osk.lastT = kapi_get_ticks ();
	if (g_osk.page == 1) g_osk.page = 0;		// (a capital: one letter, as the phones)
}
static void osk_delete (void)
{
	if (g_osk.caret == 0) return;
	int b = g_osk.caret - 1;
	while (b > 0 && (g_osk.t[b] & 0xC0) == 0x80) b--;	// (a UTF-8 letter: all its bytes)
	memmove (g_osk.t + b, g_osk.t + g_osk.caret, strlen (g_osk.t + g_osk.caret) + 1);
	g_osk.caret = b; g_osk.lastT = 0;
}
static void osk_caret (int d)
{
	int c = g_osk.caret, n = (int) strlen (g_osk.t);
	if (d < 0 && c > 0) { c--; while (c > 0 && (g_osk.t[c] & 0xC0) == 0x80) c--; }
	if (d > 0 && c < n) { c++; while (c < n && (g_osk.t[c] & 0xC0) == 0x80) c++; }
	g_osk.caret = c;
}
static void osk_done (bool ok);
static int sp_col_of (int c)				// a grid column -> the special key under it
{
	int x = c * 10 + 5, a = 0;
	for (int i = 0; i < 7; i++) { if (x < a + OSK_SPW[i]) return i; a += OSK_SPW[i]; }
	return 6;
}
static int col_of_sp (int s)				// a special key -> the grid column under its middle
{
	int a = 0;
	for (int i = 0; i < s; i++) a += OSK_SPW[i];
	int c = (a + OSK_SPW[s] / 2) / 10;
	return c > 9 ? 9 : c;
}
static void osk_press (void)				// A on the key chosen
{
	if (g_osk.r < 4) { osk_insert (osk_key (g_osk.r, g_osk.c)); return; }
	switch (g_osk.c)
	{
	case 0: g_osk.page = g_osk.page == 1 ? 0 : 1; break;
	case 1: g_osk.page = g_osk.page == 2 ? 0 : 2; break;
	case 2: osk_insert (" "); break;
	case 3: osk_caret (-1); break;
	case 4: osk_caret (1); break;
	case 5: osk_delete (); break;
	case 6: osk_done (true); break;
	}
}
static void osk_keypress (int k)
{
	switch (k)
	{
	case KEY_UP: if (g_osk.r == 4) g_osk.c = col_of_sp (g_osk.c); g_osk.r = (g_osk.r + 4) % 5; if (g_osk.r == 4) g_osk.c = sp_col_of (g_osk.c); break;
	case KEY_DOWN: if (g_osk.r == 4) g_osk.c = col_of_sp (g_osk.c); g_osk.r = (g_osk.r + 1) % 5; if (g_osk.r == 4) g_osk.c = sp_col_of (g_osk.c); break;
	case KEY_LEFT: { int n = g_osk.r == 4 ? 7 : 10; g_osk.c = (g_osk.c + n - 1) % n; break; }
	case KEY_RIGHT: { int n = g_osk.r == 4 ? 7 : 10; g_osk.c = (g_osk.c + 1) % n; break; }
	case K_A: osk_press (); break;
	case K_B: if (g_osk.t[0]) osk_delete (); else osk_done (false); break;
	case K_X: osk_insert (" "); break;
	case K_Y: g_osk.page = (g_osk.page + 1) % 3; break;
	case -1: osk_caret (-1); break;
	case '\t': osk_caret (1); break;
	case K_START: case KEY_ENTER: osk_done (true); break;
	case K_L3: g_osk.show = !g_osk.show; break;
	case 0x1b: osk_done (false); break;
	case KEY_BACKSPACE: osk_delete (); break;
	case KEY_HOME: g_osk.caret = 0; break;
	case KEY_END: g_osk.caret = (int) strlen (g_osk.t); break;
	default:						// a keyboard plugged in types too
		if (k >= 32 && k < 127) { char s[2] = { (char) k, 0 }; bool cap = g_osk.page == 1; osk_insert (s); if (cap) g_osk.page = 1; }
		break;
	}
	g_homeDirty = true;
}

// ---- Sound ------------------------------------------------------------------------------------------------------------
static int g_outVal[4], g_outN;
static struct kapi_sound_client g_mix[8];
static int g_nmix;
static unsigned g_volFlashT;
static const char *out_title (int o)
{
	return o == KAPI_SND_OUT_JACK ? TR ("Headphone jack") : o == KAPI_SND_OUT_USB ? TR ("USB headset") : o == KAPI_SND_OUT_HDMI ? TR ("HDMI") : TR ("Automatic");
}
static int test_thread (void *)				// a chime: C E G (soundconf's)
{
	static const unsigned NOTES[3] = { 523251, 659255, 783991 };
	for (int i = 0; i < 3; i++) { ak_fm_start (i, NOTES[i], SOUND_SINE, 170); kapi_msleep (140); }
	kapi_msleep (320);
	ak_fm_stop (-1);
	return 0;
}
static void sound_rows (void)
{
	int o = kapi_sound_output (-1), asked = o < 0 ? KAPI_SND_OUT_JACK : KAPI_SND_OUT_ASKED (o), sel = 0;
	g_outN = 0;
	if (o < 0) g_outVal[g_outN++] = KAPI_SND_OUT_JACK;
	else
		for (int i = KAPI_SND_OUT_AUTO; i <= KAPI_SND_OUT_HDMI; i++)
		{
			if (i != KAPI_SND_OUT_AUTO && i != asked && !KAPI_SND_OUT_HAS (o, i)) continue;
			if (i == asked) sel = g_outN;
			g_outVal[g_outN++] = i;
		}
	char h[160] = "";
	int now = o < 0 ? 0 : KAPI_SND_OUT_NOW (o);
	if (now) ws_cat (h, sizeof h, TR ("Playing on: "), out_title (now));
	else ws_cat (h, sizeof h, TR ("Not playing yet: the sound starts with the first app that plays."));
	SRow &r = row_add (R_CHOICE, 1, TR ("Play on"), out_title (g_outVal[sel]), h); r.v = sel;
	int vm = kapi_sound_volume (-1, -1);
	SRow &v = row_add (R_SLIDER, 2, TR ("Volume"), "", TR ("The whole system's volume (kept in SD:/etc/sound.ini)"));
	v.v = vm < 0 ? 0 : vm & 0xFF; v.max = 10;
	SRow &m = row_add (R_TOGGLE, 3, TR ("Mute"), "", TR ("No sound at all, the volume kept"));
	m.on = vm >= 0 && (vm & 0x100);
	row_add (R_ACTION, 4, TR ("Play a test sound"), "", TR ("A short chime (when no program holds the sound)"));
	g_nmix = kapi_sound_clients (g_mix, 8);
	if (g_nmix > 0)
	{
		row_add (R_HEAD, 0, TR ("Programs playing"));
		for (int i = 0; i < g_nmix; i++)
		{
			App *a = find_app (g_mix[i].name);
			SRow &c = row_add (R_SLIDER, 10, a ? a->label : g_mix[i].name, "", TR ("Its own volume (kept in SD:/etc/mixer.ini); A mutes it"));
			c.arg = i; c.v = g_mix[i].volume; c.max = 100; c.icon = a; c.on = g_mix[i].mute != 0;
			if (c.on) fs_copy (c.value, TR ("muted"), sizeof c.value);
		}
	}
	else if (g_nmix == 0) row_add (R_INFO, 0, TR ("Programs playing"), TR ("none now"), TR ("Each program that plays gets its own volume here"));
}
static void vol_flash (void) { g_volFlashT = kapi_get_ticks (); g_homeDirty = true; }
static void sound_change (SRow &r, int d)
{
	if (r.id == 1 && g_outN)
	{
		int k = (r.v + d + g_outN) % g_outN;
		volume_set_output (g_outVal[k]);
		flash (out_title (g_outVal[k]));
	}
	else if (r.id == 2)
	{
		int v = r.v + d; if (v < 0) v = 0; if (v > 10) v = 10;
		int x = kapi_sound_volume (v, 0);			// (moving it unmutes, as the menu bar's)
		volume_save (x < 0 ? v : x & 0xFF, x >= 0 && (x & 0x100) ? 1 : 0);
		vol_flash ();
	}
	else if (r.id == 3) { int x = kapi_sound_volume (-1, r.on ? 0 : 1); volume_save (x & 0xFF, (x & 0x100) ? 1 : 0); vol_flash (); }
	else if (r.id == 10 && r.arg < g_nmix)
	{
		int v = r.v + d * 10; if (v < 0) v = 0; if (v > 100) v = 100;
		kapi_sound_client_volume (g_mix[r.arg].pid, v, -1);
		g_mix[r.arg].volume = v;
		mixer_set (&g_mix[r.arg], v, -1);
	}
}
static void sound_act (SRow &r)
{
	if (r.id == 3) sound_change (r, 0);
	else if (r.id == 4) { kapi_thread_create (test_thread, 0, 0, "chime"); }
	else if (r.id == 10 && r.arg < g_nmix) { g_mix[r.arg].mute = !g_mix[r.arg].mute; mixer_set (&g_mix[r.arg], -1, g_mix[r.arg].mute); }
}

// ---- Gamepad ----------------------------------------------------------------------------------------------------------
static struct pad_learn g_learn;
static int g_wizPad = -1;				// the pad being learnt
static bool g_wizArmed;					// waiting for the pad at rest before the first step
static unsigned g_wizT;					// the last step's time (8 s: skipped)
static int g_keyWait = -1;				// Keyboard as pad 1: the function waiting for its key
static const char *const PAD_FN[PAD_KEYS] = { TRN ("Up"), TRN ("Down"), TRN ("Left"), TRN ("Right"), "A", "B", "X", "Y", "L", "R", "L2", "R2",
	TRN ("Select"), TRN ("Start"), "L3", "R3", TRN ("Home"), TRN ("Right stick up"), TRN ("Right stick down"), TRN ("Right stick left"), TRN ("Right stick right") };
// (gamepad.h's words of the wizard, and SystemKit's cities with a name of their own in another language)
// TR: UP on the d-pad
// TR: DOWN on the d-pad
// TR: LEFT on the d-pad
// TR: RIGHT on the d-pad
// TR: the BOTTOM face button (Xbox A, PlayStation Cross, Nintendo B)
// TR: the RIGHT face button (Xbox B, PlayStation Circle, Nintendo A)
// TR: the LEFT face button (Xbox X, PlayStation Square, Nintendo Y)
// TR: the TOP face button (Xbox Y, PlayStation Triangle, Nintendo X)
// TR: the LEFT shoulder button (L / L1 / LB)
// TR: the RIGHT shoulder button (R / R1 / RB)
// TR: the LEFT trigger (L2 / LT / ZL: a button or an analog trigger)
// TR: the RIGHT trigger (R2 / RT / ZR)
// TR: SELECT (Back / Share / -)
// TR: START (Options / +)
// TR: the LEFT stick's click (L3)
// TR: the RIGHT stick's click (R3)
// TR: HOME (Guide / PS)
// TR: Brussels
// TR: Vienna
// TR: Warsaw
// TR: London
// TR: Lisbon
// TR: Athens
static void pad_model (const struct kapi_pad &p, char *o, int cap)
{
	o[0] = 0; int k = 0;
	pad_cathex (o, &k, cap, p.vid); pad_cat (o, &k, cap, ":"); pad_cathex (o, &k, cap, p.pid);
}
static void held_words (char *o, int cap)		// the buttons the apps see held now, on every pad
{
	unsigned b = pad_any ();
	o[0] = 0;
	for (int i = 0; i < PAD_NBUTTONS; i++)
		if (b & (1u << i)) { if (o[0]) ws_cat (o, cap, ", "); ws_cat (o, cap, i < 4 || i == 12 || i == 13 || i == 16 ? TR (PAD_FN[i]) : PAD_FN[i]); }
	if (!o[0]) fs_copy (o, TR ("nothing"), cap);
}
static int first_pad (void) { struct kapi_pad p; for (int i = 0; i < PAD_MAX; i++) if (kapi_pad_state (i, &p)) return i; return -1; }
static void wiz_start (int pad)
{
	struct kapi_pad p;
	if (pad < 0 || !kapi_pad_state (pad, &p)) { flash (TR ("Plug in a pad first")); return; }
	g_wizPad = pad; g_wizArmed = true; g_wizT = kapi_get_ticks ();
	g_learn.step = 0;
	g_ss[g_nss++] = { SC_WIZ, pad, 0, 0 };
	g_homeDirty = true;
}
static void pad_rows (int only)				// only: one pad's page (-1: the Gamepad page)
{
	char h[160], v[80];
	if (only < 0)
		for (int i = 0; i < 4 && i < PAD_MAX; i++)
		{
			struct kapi_pad p;
			char l[24]; snprintf (l, sizeof l, TR ("Pad %d"), i + 1);
			if (kapi_pad_state (i, &p))
			{
				pad_model (p, v, sizeof v);
				ws_cat (v, sizeof v, (p.props & 1) ? TR (" (known)") : TR (" (generic)"));
				row_add (R_SUB, 1, l, v, TR ("Its model, its mapping, its buttons; map them")).arg = i;
			}
			else row_add (R_INFO, 0, l, TR ("nothing plugged in"), TR ("A USB or Bluetooth pad plugged in shows here"));
		}
	else
	{
		struct kapi_pad p;
		if (!kapi_pad_state (only, &p)) { row_add (R_INFO, 0, TR ("This pad"), TR ("unplugged")); return; }
		pad_model (p, v, sizeof v);
		row_add (R_INFO, 0, TR ("Model (vendor:product)"), v, TR ("The USB ids its mapping is kept by in SD:/etc/gamepad.ini"));
		row_add (R_INFO, 0, TR ("Kind"), (p.props & 1) ? TR ("known to the system") : TR ("generic USB pad"), TR ("A known pad has its buttons mapped already"));
		struct pad_map m;
		int src = pad_map_for (&p, &m);
		row_add (R_INFO, 0, TR ("Its mapping"), src == 2 ? TR ("its own") : src == 1 ? TR ("[default]") : TR ("built in"),
			 TR ("Its own section in gamepad.ini, the [default] one, or the system's"));
	}
	held_words (v, sizeof v);
	row_add (R_INFO, 2, TR ("Held now"), v, TR ("What the games see: press a button to try it"));
	h[0] = 0; ws_cat (h, sizeof h, TR ("Asks for each button in turn, then keeps them for this pad's model"));
	row_add (R_ACTION, 3, TR ("Map the buttons"), "", h).arg = only;
	if (only < 0)
	{
		pad_config_reload ();
		int n = 0; for (int i = 0; i < PAD_KEYS; i++) if (g_pad_kbd[i]) n++;
		snprintf (v, sizeof v, TR ("%d keys"), n);
		row_add (R_SUB, 4, TR ("Keyboard as pad 1"), v, TR ("The keys that play as pad 1 (a keyboard plugged in, or Remote Desktop's)"));
	}
	row_add (R_ACTION, 5, TR ("Forget this pad's mapping"), "", TR ("Its section removed from gamepad.ini: the built-in mapping again")).arg = only;
	if (only < 0) row_add (R_ACTION, 6, TR ("Read gamepad.ini again"), "", TR ("After the file was changed by hand"));
}
static void keys_rows (void)
{
	for (int i = 0; i < PAD_KEYS; i++)
	{
		const char *fn = (i >= 4 && i <= 11) || i == 14 || i == 15 ? PAD_FN[i] : TR (PAD_FN[i]);
		SRow &r = row_add (R_ACTION, 1, fn, g_keyWait == i ? TR ("press a key...") : g_pad_kbd[i] ? pad_key_word (g_pad_kbd[i]) : TR ("none"),
				   g_keyWait == i ? TR ("Press the key on the keyboard (Esc: none)") : TR ("A: give it another key (a keyboard is needed)"));
		r.arg = i; r.good = g_keyWait == i;
	}
	row_add (R_ACTION, 2, TR ("Back to the defaults"), "", TR ("The arrows, Z X A S, Q W E R..."));
}
static void pad_act (SRow &r)
{
	if (r.id == 1) { g_ss[g_nss++] = { SC_PADINFO, r.arg, 0, 0 }; return; }
	if (r.id == 3) { wiz_start (r.arg >= 0 ? r.arg : first_pad ()); return; }
	if (r.id == 4) { g_keyWait = -1; g_ss[g_nss++] = { SC_KEYS, 0, 0, 0 }; return; }
	if (r.id == 5)
	{
		int pad = r.arg >= 0 ? r.arg : first_pad ();
		if (pad < 0) { flash (TR ("Plug in a pad first")); return; }
		dlg_open (D_FORGET_PAD, pad, TR ("Forget this pad's mapping?"), TR ("Its buttons go back to the built-in mapping."), TR ("Forget"), TR ("Cancel"));
		return;
	}
	if (r.id == 6) { pad_config_reload (); flash (TR ("gamepad.ini read again")); }
}
static void keys_act (SRow &r)
{
	if (r.id == 1) g_keyWait = g_keyWait == r.arg ? -1 : r.arg;
	else if (r.id == 2) { pad_keyboard_reset (); pad_config_reload (); g_keyWait = -1; flash (TR ("The default keys again")); }
}
static void keys_take (int k)				// the key pressed while one function waits
{
	int b = g_keyWait;
	g_keyWait = -1;
	if (k == 0x1b) { g_pad_kbd[b] = 0; pad_keyboard_save (); }
	else if (pad_key_word (k)[0]) pad_keyboard_set (b, k);
	else { flash (TR ("This key cannot be used")); return; }
	pad_config_reload ();
	g_homeDirty = true;
}
static void wiz_tick (void)
{
	struct kapi_pad p;
	if (!kapi_pad_state (g_wizPad, &p)) { g_nss--; flash (TR ("The pad was unplugged")); return; }
	if (g_wizArmed)						// (the A that started it let go first)
	{
		bool rest = p.buttons == 0;
		for (int i = 0; i < p.nhats; i++) if (p.hats[i] >= 0 && p.hats[i] < 8) rest = false;
		if (!rest) return;
		pad_learn_start (&g_learn, &p);
		g_wizArmed = false; g_wizT = kapi_get_ticks (); g_homeDirty = true;
		return;
	}
	int st = g_learn.step, r = pad_learn_poll (&g_learn, &p);
	if (r == 2)
	{
		g_nss--;
		flash (pad_learn_save (&g_learn, "learnt in the console's settings") ? TR ("The buttons are kept for this pad") : TR ("gamepad.ini could not be written"));
		return;
	}
	if (r == 1 || g_learn.step != st) { g_wizT = kapi_get_ticks (); g_homeDirty = true; }
	else if (!g_learn.wait_release && kapi_get_ticks () - g_wizT > 800) { pad_learn_skip (&g_learn); g_wizT = kapi_get_ticks (); g_homeDirty = true; }
}

// ---- Keyboard & Mouse, Language & Region ----------------------------------------------------------------------------
static char g_km[INPUT_KEYMAPS_MAX][12];
static int g_nkm;
// (the layouts' names, given by SystemKit in English)
// TR: American (qwerty)
// TR: Belgian (azerty)
// TR: British
// TR: Dvorak
// TR: French (azerty)
// TR: German (qwertz)
// TR: Italian
// TR: Spanish
// TR: Danish
// TR: Japanese
// TR: Norwegian
// TR: Portuguese
// TR: Swedish / Finnish
static void kbd_rows (void)
{
	g_nkm = input_keymaps (g_km, INPUT_KEYMAPS_MAX);
	char now[16] = ""; input_keymap_now (now, sizeof now);
	int sel = -1;
	for (int i = 0; i < g_nkm; i++) if (ieq (g_km[i], now)) sel = i;
	const char *nm = input_keymap_name (now);
	SRow &r = row_add (R_CHOICE, 1, TR ("Keyboard layout"), nm[0] ? TR (nm) : now, TR ("Taken at once and kept for every start"));
	r.v = sel;
	row_add (R_ACTION, 2, TR ("Try it"), TR ("a physical keyboard"), TR ("A field to type in with the keyboard plugged in"));
	row_add (R_HEAD, 0, TR ("Mouse"));
	SRow &w = row_add (R_SLIDER, 3, TR ("Wheel"), "", TR ("The lines a notch of the wheel scrolls"));
	w.v = input_wheel (); w.max = 16;
}
static void kbd_change (SRow &r, int d)
{
	if (r.id == 1 && g_nkm)
	{
		int k = r.v < 0 ? 0 : (r.v + d + g_nkm) % g_nkm;
		int x = input_keymap_set (g_km[k]);
		const char *nm = input_keymap_name (g_km[k]);
		flash (x ? (nm[0] ? TR (nm) : g_km[k]) : TR ("This layout could not be loaded"));
	}
	else if (r.id == 3)
	{
		int v = r.v + d; if (v < 1) v = 1; if (v > 16) v = 16;
		input_wheel_save (v); uk_win_wheel_set (v);
	}
}
static void lang_rows (void)
{
	int li = locale_language_index ();
	SRow &l = row_add (R_CHOICE, 1, TR ("Language"), locale_language_name (li), TR ("The programs started from now on speak it"));
	l.v = li;
	int z = locale_zone ();
	char v[80] = "", u[16];
	if (z >= 0)
	{
		locale_zone_utc (z, u, sizeof u);
		ws_cat (v, sizeof v, TR (locale_zone_city (z)), " (", u);
		ws_cat (v, sizeof v, locale_zone_summer (z) ? TR (", summer time") : "", ")");
	}
	else fs_copy (v, TR ("none"), sizeof v);
	SRow &t = row_add (R_CHOICE, 2, TR ("Time zone"), v, TR ("The clock follows at once"));
	t.v = z;
	int y = 0, mo = 0, d = 0, hh = 0, mi = 0;
	kapi_get_datetime (&y, &mo, &d, &hh, &mi, 0);
	snprintf (v, sizeof v, "%04d-%02d-%02d  %02d:%02d", y, mo, d, hh, mi);
	row_add (R_INFO, 0, TR ("Now"), v, TR ("The date and the time (the network's clock when there is one)"));
}
static void lang_change (SRow &r, int d)
{
	if (r.id == 1)
	{
		int n = locale_language_count (), k = (r.v + d + n) % n;
		locale_set_language (locale_language_code (k));
		flash (locale_language_name (k));
	}
	else if (r.id == 2)
	{
		int n = locale_zone_count (), k = r.v < 0 ? 0 : (r.v + d + n) % n;
		locale_set_zone (k);
		flash (TR (locale_zone_city (k)));
	}
}

// ---- Wi-Fi ------------------------------------------------------------------------------------------------------------
struct WNet { char ssid[33]; int level, security; bool connected, known, around; };
#define MAXWN	40
static WNet g_wn[MAXWN];
static int g_nwn;
static WNet g_net;					// the network's page shows it
static char g_netPass[64];				// a password typed on its page (not kept until joined)
static bool g_netShow, g_netJoin;			// show the password; join once it is typed
static struct kapi_wlan_ap g_ap[40];
static volatile int g_scanN;
static volatile bool g_scanBusy, g_scanDone;
static int g_scanTid;
static unsigned g_scanT, g_rescanAt;
static char g_cc[8];					// the country
static const char *const CC[] = { "BE", "FR", "NL", "LU", "DE", "CH", "AT", "GB", "IE", "ES", "PT", "IT", "DK", "SE", "NO", "FI", "PL", "US", "CA", "JP", "AU" };
#define NCC	((int) (sizeof CC / sizeof CC[0]))
static int scan_thread (void *) { g_scanN = kapi_wlan_scan (g_ap, 40); g_scanDone = true; return 0; }
static void scan_start (void)
{
	if (g_scanBusy) return;
	g_scanBusy = true; g_scanDone = false;
	g_scanTid = kapi_thread_create (scan_thread, 0, 0, "wlanscan");
	if (g_scanTid < 0) { g_scanBusy = false; flash (TR ("Cannot scan now")); }
	g_homeDirty = true;
}
static void wifi_list (void)				// the scan's networks, then the known ones not around
{
	static struct wifi_known k[WIFI_KNOWN_MAX];
	int nk = wifi_known_load (k, WIFI_KNOWN_MAX, g_cc, sizeof g_cc);
	g_nwn = 0;
	for (int i = 0; i < g_scanN && g_nwn < MAXWN; i++)
	{
		if (!g_ap[i].ssid[0] || g_ap[i].ssid[0] == ' ') continue;
		bool dup = false;
		for (int j = 0; j < g_nwn; j++) if (!strcmp (g_wn[j].ssid, g_ap[i].ssid)) { dup = true; if (g_ap[i].connected) g_wn[j].connected = true; }
		if (dup) continue;
		WNet &w = g_wn[g_nwn++];
		fs_copy (w.ssid, g_ap[i].ssid, sizeof w.ssid);
		w.level = g_ap[i].level; w.security = g_ap[i].security; w.connected = g_ap[i].connected; w.around = true; w.known = false;
	}
	for (int i = 0; i < g_nwn; i++)				// the one we are on, first
		if (g_wn[i].connected && i > 0) { WNet t = g_wn[i]; for (int j = i; j > 0; j--) g_wn[j] = g_wn[j - 1]; g_wn[0] = t; break; }
	for (int i = 0; i < nk; i++)
	{
		bool in = false;
		for (int j = 0; j < g_nwn; j++) if (!strcmp (g_wn[j].ssid, k[i].ssid)) { g_wn[j].known = true; in = true; }
		if (in || g_nwn >= MAXWN) continue;
		WNet &w = g_wn[g_nwn++];
		fs_copy (w.ssid, k[i].ssid, sizeof w.ssid);
		w.level = 0; w.security = ieq (k[i].keymgmt, "NONE") ? WLAN_SEC_OPEN : WLAN_SEC_WPA2; w.connected = false; w.around = false; w.known = true;
	}
}
static int bars_of (int dbm) { return dbm == 0 ? 0 : dbm >= -55 ? 4 : dbm >= -65 ? 3 : dbm >= -75 ? 2 : 1; }
static const char *sec_words (int s) { return s == WLAN_SEC_OPEN ? TR ("open") : s == WLAN_SEC_WEP ? "WEP" : s == WLAN_SEC_WPA ? "WPA" : "WPA2"; }
static void wifi_rows (void)
{
	bool any = false;
	for (int i = 0; i < g_nwn; i++)
	{
		if (!g_wn[i].around) continue;
		any = true;
		WNet &w = g_wn[i];
		char h[160] = "";
		ws_cat (h, sizeof h, sec_words (w.security), w.known ? TR (", known") : "", TR (": A for its page"));
		SRow &r = row_add (R_NET, 1, w.ssid, w.connected ? TR ("Connected") : w.security == WLAN_SEC_OPEN ? TR ("open") : "", h);
		r.arg = i; r.v = bars_of (w.level); r.lock = w.security != WLAN_SEC_OPEN; r.good = w.connected;
	}
	if (!any && !g_scanBusy) row_add (R_INFO, 0, TR ("No network around"), "", TR ("Is the Wi-Fi up? Scan again in a moment"));
	char v[64];
	if (g_scanBusy) fs_copy (v, TR ("Scanning..."), sizeof v);
	else if (g_scanT) { int s = (int) (kapi_get_ticks () - g_scanT) / 100; int n = 0; for (int i = 0; i < g_nwn; i++) n += g_wn[i].around; snprintf (v, sizeof v, TR ("%d found, %d s ago"), n, s); }
	else v[0] = 0;
	row_add (R_ACTION, 2, TR ("Scan again"), v, TR ("Looks for the networks around (3 s); Y does it anywhere on this page"));
	row_add (R_ACTION, 3, TR ("A hidden network..."), "", TR ("Its name typed, then its page"));
	bool head = false;
	for (int i = 0; i < g_nwn; i++)
	{
		if (g_wn[i].around) continue;
		if (!head) { row_add (R_HEAD, 0, TR ("Known, not around")); head = true; }
		row_add (R_SUB, 1, g_wn[i].ssid, TR ("known"), TR ("Kept in SD:/etc/wpa_supplicant.conf: A to forget it")).arg = i;
	}
	int ci = -1; for (int i = 0; i < NCC; i++) if (ieq (CC[i], g_cc)) ci = i;
	SRow &c = row_add (R_CHOICE, 4, TR ("Country"), g_cc[0] ? g_cc : "BE", TR ("The radio's channels follow the country's rules"));
	c.v = ci;
}
static void net_rows (void)
{
	WNet &w = g_net;
	bool need = w.security != WLAN_SEC_OPEN;
	char h[160] = "";
	if (w.connected) ws_cat (h, sizeof h, TR ("Joined now"));
	else ws_cat (h, sizeof h, TR ("Joins it now; it is kept with the others, first"));
	row_add (R_ACTION, 1, w.connected ? TR ("Connected") : TR ("Connect"), "", h).good = w.connected;
	if (need)
	{
		char v[80] = "";
		const char *src = g_netPass;
		static struct wifi_known k[WIFI_KNOWN_MAX];
		if (!src[0] && w.known)
		{
			int nk = wifi_known_load (k, WIFI_KNOWN_MAX, 0, 0);
			for (int i = 0; i < nk; i++) if (!strcmp (k[i].ssid, w.ssid)) src = k[i].psk;
		}
		if (!src[0]) fs_copy (v, TR ("none yet"), sizeof v);
		else if (g_netShow) fs_copy (v, src, sizeof v);
		else { int n = (int) strlen (src); if (n > 20) n = 20; for (int i = 0; i < n; i++) ws_cat (v, sizeof v, "\xE2\x80\xA2"); }
		row_add (R_ACTION, 2, TR ("Password"), v, TR ("8 to 63 characters: A types it on the keyboard"));
		row_add (R_TOGGLE, 3, TR ("Show password"), "", TR ("Its letters on the screen")).on = g_netShow;
	}
	row_add (R_INFO, 0, TR ("Security"), sec_words (w.security), w.security == WLAN_SEC_WEP ? TR ("WEP is not supported") : "");
	char s[32] = "";
	if (w.around) snprintf (s, sizeof s, "%d dBm", w.level); else fs_copy (s, TR ("not around"), sizeof s);
	SRow &sg = row_add (R_INFO, 0, TR ("Signal"), s, ""); sg.v = bars_of (w.level);
	if (w.known) row_add (R_ACTION, 4, TR ("Forget this network"), "", TR ("Removed from SD:/etc/wpa_supplicant.conf"));
}
static void net_connect (void)
{
	WNet &w = g_net;
	if (w.security == WLAN_SEC_WEP) { flash (TR ("WEP networks are not supported")); return; }
	int pl = (int) strlen (g_netPass);
	if (w.security != WLAN_SEC_OPEN && !w.known && pl == 0)
	{
		char l[96] = ""; ws_cat (l, sizeof l, TR ("Password for "), w.ssid);
		g_netJoin = true;
		osk_open (O_PASS, l, "", true, 8, 63);
		return;
	}
	int r = wifi_join (w.ssid, w.security, pl ? g_netPass : 0);
	char m[120] = "";
	if (r == WIFI_OK) { ws_cat (m, sizeof m, TR ("Connecting to "), w.ssid, "..."); g_rescanAt = kapi_get_ticks () + 700; w.known = true; }
	else if (r == WIFI_SAVED) ws_cat (m, sizeof m, TR ("Saved: a restart joins it"));
	else if (r == WIFI_EFULL) ws_cat (m, sizeof m, TR ("Too many known networks: forget one first"));
	else if (r == WIFI_EPASSWORD) ws_cat (m, sizeof m, TR ("The password is 8 to 63 characters"));
	else if (r == WIFI_EWEP) ws_cat (m, sizeof m, TR ("WEP networks are not supported"));
	else ws_cat (m, sizeof m, TR ("Cannot write SD:/etc/wpa_supplicant.conf"));
	flash (m);
	g_netPass[0] = 0;
}
static void net_open (const WNet &w)
{
	g_net = w; g_netPass[0] = 0; g_netShow = false; g_netJoin = false;
	g_ss[g_nss++] = { SC_NET, 0, 0, 0 };
}
static void wifi_act (SRow &r)
{
	if (r.id == 1 && r.arg < g_nwn) net_open (g_wn[r.arg]);
	else if (r.id == 2) scan_start ();
	else if (r.id == 3) osk_open (O_HIDDEN, TR ("The hidden network's name"), "", false, 1, 32);
}
static void net_act (SRow &r)
{
	if (r.id == 1) { if (!g_net.connected) net_connect (); }
	else if (r.id == 2) { char l[96] = ""; ws_cat (l, sizeof l, TR ("Password for "), g_net.ssid); g_netJoin = false; osk_open (O_PASS, l, g_netPass, true, 8, 63); }
	else if (r.id == 3) g_netShow = !g_netShow;
	else if (r.id == 4)
	{
		char t[160] = ""; ws_cat (t, sizeof t, g_net.ssid, TR (": its password is removed from the card."));
		dlg_open (D_FORGET_NET, 0, TR ("Forget this network?"), t, TR ("Forget"), TR ("Cancel"));
	}
}

// ---- Packages ---------------------------------------------------------------------------------------------------------
static pkg::Manager *g_pm;
static bool g_pkgChecked, g_pkgAsked;			// read once on entering; Check now asked (its failure said)
static char g_pkgName[40];				// the package's page
static char g_pkgFind[64];				// the search
static const char *const PKG_MODES[3] = { "manual", "auto", "never" };
static const char *mode_word (const char *m) { return ieq (m, "auto") ? TR ("automatic") : ieq (m, "never") ? TR ("never") : TR ("manual"); }
static void pkg_init (void)
{
	if (!g_pm) { g_pm = new pkg::Manager; g_pm->load_cached_quiet (); pkg::g_jobM = g_pm; }
	if (!g_pkgChecked && !pkg::g_job.running) { g_pkgChecked = true; pkg::job_start (pkg::J_CHECK, 0, 0); }
}
static const pkg::Pkg *pkg_update (const pkg::Inst &in) { return g_pm->staged (in.name) ? 0 : g_pm->update_for (in); }
static void pkg_job_value (SRow &r, const char *name)	// "Installing 42 %" on the package at work
{
	if (pkg::g_job.running && !pkg::g_job.done && !strcmp (pkg::g_job.cur, name))
	{
		snprintf (r.value, sizeof r.value, pkg::g_job.kind == pkg::J_REMOVE ? TR ("Removing...") : TR ("Installing %d %%"), pkg::g_job.pct);
		r.pct = pkg::g_job.kind == pkg::J_REMOVE ? -1 : pkg::g_job.pct;
	}
}
static char g_rowPkg[MAXSR][40];			// each row's package (the rows are rebuilt each time)
static void pkg_row (const char *name, const char *title, const char *value, const char *help, int id)
{
	SRow &r = row_add (R_SUB, id, title, value, help);
	r.icon = find_app (name);
	pkg_job_value (r, name);
}
static void pkg_rows (void)
{
	pkg::Manager &m = *g_pm;
	int nu = 0;
	for (int i = 0; i < m.db.n; i++) { const pkg::Pkg *p = pkg_update (*m.db.v[i]); if (p && !ieq (m.db.v[i]->mode (), "never")) nu++; }
	char v[80], h[160];
	if (nu) { snprintf (v, sizeof v, nu == 1 ? TR ("Install the update") : TR ("Install the %d updates"), nu); row_add (R_ACTION, 1, v, "", TR ("One after the other; the system's waits for a restart")); }
	else row_add (R_INFO, 0, m.haveIndex ? TR ("Everything is up to date") : TR ("No list of packages yet"), "", m.haveIndex ? "" : TR ("Check now reads the repository"));
	bool staged = false;
	for (int i = 0; i < m.db.n; i++) if (m.staged (m.db.v[i]->name)) staged = true;
	if (staged) row_add (R_ACTION, 7, TR ("Restart to finish"), "", TR ("The system's update is staged: it is moved in at the restart")).warn = true;
	bool head = false;
	for (int i = 0; i < m.db.n; i++)
	{
		pkg::Inst &in = *m.db.v[i];
		const pkg::Pkg *p = pkg_update (in);
		if (!p) continue;
		if (!head) { row_add (R_HEAD, 0, TR ("Updates")); head = true; }
		snprintf (v, sizeof v, "%s \xE2\x80\xBA %s", in.version (), p->version);
		snprintf (h, sizeof h, "%s  %s", p->summary, TR ("(X: its updates mode)"));
		fs_copy (g_rowPkg[g_nsr], in.name, 40);
		pkg_row (in.name, p->title, v, h, 2);
		if (p->restart) { g_sr[g_nsr - 1].warn = true; ws_cat (g_sr[g_nsr - 1].value, sizeof g_sr[0].value, TR ("  restart")); }
	}
	row_add (R_HEAD, 0, TR ("The packages"));
	snprintf (v, sizeof v, m.db.n == 1 ? TR ("%d package") : TR ("%d packages"), m.db.n);
	row_add (R_SUB, 3, TR ("Installed"), v, TR ("Remove one, set its updates mode"));
	int na = 0;
	for (int i = 0; i < m.index.n; i++) if (!m.db.find (m.index.p[i].name)) na++;
	snprintf (v, sizeof v, TR ("%d more"), na);
	row_add (R_SUB, 4, TR ("Available"), v, TR ("The others the repository has: install one"));
	row_add (R_ACTION, 5, TR ("Search..."), g_pkgFind, TR ("A name, a word of its summary"));
	if (pkg::g_job.running && pkg::g_job.kind == pkg::J_CHECK) fs_copy (v, TR ("Reading the repository..."), sizeof v);
	else if (m.haveIndex) { v[0] = 0; ws_cat (v, sizeof v, TR ("list of "), m.index.date, m.verified ? TR (", signed") : TR (", not checked")); }
	else fs_copy (v, TR ("never read"), sizeof v);
	row_add (R_ACTION, 6, TR ("Check now"), v, TR ("Reads the repository's list again (its signature checked)"));
}
static void pkglist_rows (int which)			// 0 the installed ones, 1 the available, 2 the search's
{
	pkg::Manager &m = *g_pm;
	char v[80];
	for (int i = 0; i < m.db.n; i++)
	{
		pkg::Inst &in = *m.db.v[i];
		if (which == 1) break;
		const char *t = in.ini.get ("package", "title", in.name), *s = in.ini.get ("package", "summary");
		if (which == 2 && !(contains_ci (in.name, g_pkgFind) || contains_ci (t, g_pkgFind) || contains_ci (s, g_pkgFind))) continue;
		snprintf (v, sizeof v, "%s", in.version ());
		fs_copy (g_rowPkg[g_nsr], in.name, 40);
		pkg_row (in.name, t, v, s, 2);
	}
	for (int i = 0; i < m.index.n && which != 0; i++)
	{
		const pkg::Pkg &p = m.index.p[i];
		if (m.db.find (p.name)) continue;
		if (which == 2 && !(contains_ci (p.name, g_pkgFind) || contains_ci (p.title, g_pkgFind) || contains_ci (p.summary, g_pkgFind))) continue;
		snprintf (v, sizeof v, "%s  (%d KB)", p.version, (int) ((p.size + 1023) / 1024));
		fs_copy (g_rowPkg[g_nsr], p.name, 40);
		pkg_row (p.name, p.title, v, p.summary, 2);
	}
	if (g_nsr == 0) row_add (R_INFO, 0, which == 2 ? TR ("Nothing matches") : TR ("None"));
}
static void pkgone_rows (void)
{
	pkg::Manager &m = *g_pm;
	pkg::Inst *in = m.db.find (g_pkgName);
	const pkg::Pkg *p = m.index.find (g_pkgName);
	char v[80];
	const char *title = in ? in->ini.get ("package", "title", g_pkgName) : p ? p->title : g_pkgName;
	const char *sum = in ? in->ini.get ("package", "summary") : p ? p->summary : "";
	SRow &t = row_add (R_INFO, 0, title, in ? in->version () : TR ("not installed"), sum);
	t.icon = find_app (g_pkgName);
	pkg_job_value (t, g_pkgName);
	const pkg::Pkg *u = in ? pkg_update (*in) : p;
	if (u) { snprintf (v, sizeof v, "%s", u->version); row_add (R_ACTION, 1, in ? TR ("Update") : TR ("Install"), v, u->restart ? TR ("The system's: it waits for a restart") : TR ("Its needs come with it")); }
	if (in && m.staged (in->name)) row_add (R_INFO, 0, TR ("Staged"), TR ("at the restart"), "").warn = true;
	if (in)
	{
		int k = 0; for (int i = 0; i < 3; i++) if (ieq (in->mode (), PKG_MODES[i])) k = i;
		row_add (R_CHOICE, 2, TR ("Its updates"), mode_word (PKG_MODES[k]), TR ("Manual: installed when asked; automatic: by the updates' daemon; never")).v = k;
		if (!in->required ()) row_add (R_ACTION, 3, TR ("Remove"), "", TR ("Its files leave the card (the documents stay)"));
		else row_add (R_INFO, 0, TR ("Remove"), TR ("the system needs it"), "");
	}
	if (p) { snprintf (v, sizeof v, "%d KB", (int) ((p->size + 1023) / 1024)); row_add (R_INFO, 0, TR ("Size"), v, ""); }
}
static void pkg_after (void)				// a job ended: its result said
{
	kapi_thread_join (pkg::g_job.tid, 1000, 0);
	pkg::g_job.running = false;
	g_pm->db.load ();
	int rc = pkg::g_job.rc, kind = pkg::g_job.kind;
	if (kind == pkg::J_CHECK) { if (rc != pkg::OK && g_pkgAsked) flash (pkg::g_job.msg[0] ? pkg::g_job.msg : TR ("The repository could not be read")); g_pkgAsked = false; }
	else if (rc == pkg::OK)
	{
		if (pkg::g_job.nstaged) dlg_open (D_RESTART, 0, TR ("Restart now?"), TR ("The system's update is ready: it is moved in at the restart."), TR ("Restart"), TR ("Later"));
		else flash (kind == pkg::J_REMOVE ? TR ("Removed") : kind == pkg::J_MODE ? TR ("Kept") : TR ("Done: the apps open with their new version"));
	}
	else flash (pkg::g_job.msg[0] ? pkg::g_job.msg : TR ("It did not work"));
	g_homeDirty = true;
}
static void pkg_start (int kind, const char *const *names, int n, const char *mode = 0)
{
	if (pkg::g_job.running) { flash (TR ("A job is running: wait for its end")); return; }
	if (!pkg::job_start (kind, names, n, mode)) flash (TR ("Cannot start the job"));
	g_homeDirty = true;
}
static void pkg_act (SRow &r, int i)
{
	pkg::Manager &m = *g_pm;
	if (r.id == 1)
	{
		static const char *names[64]; int n = 0;
		for (int k = 0; k < m.db.n && n < 64; k++) { const pkg::Pkg *p = pkg_update (*m.db.v[k]); if (p && !ieq (m.db.v[k]->mode (), "never")) names[n++] = m.db.v[k]->name; }
		if (n) pkg_start (pkg::J_INSTALL, names, n);
	}
	else if (r.id == 2) { fs_copy (g_pkgName, g_rowPkg[i], sizeof g_pkgName); g_ss[g_nss++] = { SC_PKGONE, 0, 0, 0 }; }
	else if (r.id == 3) g_ss[g_nss++] = { SC_PKGLIST, 0, 0, 0 };
	else if (r.id == 4) g_ss[g_nss++] = { SC_PKGLIST, 1, 0, 0 };
	else if (r.id == 5) osk_open (O_SEARCH, TR ("Search the packages"), g_pkgFind, false, 0, 40);
	else if (r.id == 6) { g_pkgAsked = true; pkg_start (pkg::J_CHECK, 0, 0); }
	else if (r.id == 7) dlg_open (D_RESTART, 0, TR ("Restart now?"), TR ("The system's update is ready: it is moved in at the restart."), TR ("Restart"), TR ("Later"));
}
static void pkg_x (SRow &r, int i)			// X on an update's row: its mode cycled
{
	if (r.id != 2) return;
	pkg::Inst *in = g_pm->db.find (g_rowPkg[i]);
	if (!in) return;
	int k = 0; for (int q = 0; q < 3; q++) if (ieq (in->mode (), PKG_MODES[q])) k = q;
	const char *nm = in->name;
	pkg_start (pkg::J_MODE, &nm, 1, PKG_MODES[(k + 1) % 3]);
	flash (mode_word (PKG_MODES[(k + 1) % 3]));
}
static void pkgone_act (SRow &r)
{
	const char *nm = g_pkgName;
	if (r.id == 1) pkg_start (pkg::J_INSTALL, &nm, 1);
	else if (r.id == 3)
	{
		char t[160] = ""; ws_cat (t, sizeof t, g_pkgName, TR (": its files leave the card."));
		dlg_open (D_REMOVE, 0, TR ("Remove this package?"), t, TR ("Remove"), TR ("Cancel"));
	}
}

// ---- Mode -------------------------------------------------------------------------------------------------------------
static void *g_modeProc;
static int g_modeTo = -1;
static const char *const MODE_NAME[3] = { TRN ("Desktop"), TRN ("Pocket"), TRN ("Console") };
static const char *const MODE_HELP[3] = { TRN ("Windows, the menu bar and the dock: the mouse and the keyboard"),
	TRN ("One app at a time, big and clear: a small screen, a touch screen"), TRN ("The games on a television, played with a pad") };
static int running_mode (void)
{
	struct uk_win_server_info si;
	memset (&si, 0, sizeof si);
	si.size = sizeof si;
	if (uk_win_server (&si) > 0 && si.mode >= 0 && si.mode < 3) return si.mode;
	return session_mode ();
}
static void mode_rows (void)
{
	int cur = running_mode ();
	for (int i = 0; i < 3; i++)
	{
		SRow &r = row_add (R_RADIO, 1, TR (MODE_NAME[i]), i == cur ? TR ("in use") : g_modeTo == i ? TR ("switching...") : "", TR (MODE_HELP[i]));
		r.arg = i; r.on = i == cur; r.good = i == cur;
	}
}
static void mode_switch (int m, int flags)
{
	g_modeTo = m;
	g_modeProc = session_switch_start (m, flags, 0);
	if (!g_modeProc) { g_modeTo = -1; flash (TR ("The session's tool (SD:/bin/session) could not be started")); return; }
	flash (TR ("Closing the open programs..."));
}
static void mode_act (SRow &r)
{
	if (r.arg == running_mode () || g_modeProc) return;
	char t[300] = "";
	ws_cat (t, sizeof t, TR ("The interface"), ": ", TR (MODE_NAME[r.arg]), ". ");
	ws_cat (t, sizeof t, TR ("The open programs will be closed, then the interface starts again."));
	dlg_open (D_MODE, r.arg, TR ("Switch the interface?"), t, TR ("Switch"), TR ("Cancel"));
}
static void mode_tick (void)
{
	if (!g_modeProc || !kapi_proc_done (g_modeProc)) return;
	int r = kapi_wait (g_modeProc);
	g_modeProc = 0;
	if (r == SESSION_WAITING_APPS)
	{
		static char list[1024];
		int n = session_waiting (list, sizeof list);
		char who[64] = "", t[400] = "";
		if (n > 0) { int k = 0; while (list[k] && list[k] != '\t' && list[k] != '\n' && k < 63) { who[k] = list[k]; k++; } who[k] = 0; }
		ws_cat (t, sizeof t, who[0] ? who : TR ("A program"), " ", TR ("is waiting for an answer"), ". ");
		ws_cat (t, sizeof t, TR ("Answer its question, or force it to end (its unsaved work is lost)."));
		dlg_open (D_WAIT, g_modeTo, TR ("Mode"), t, TR ("Wait"), TR ("Force"), TR ("Cancel"), 2);
		return;
	}
	g_modeTo = -1;
	if (r == SESSION_SWITCHED) flash (TR ("The interface was switched"));
	else if (r == SESSION_FELL_BACK) flash (TR ("That interface could not start: the desktop instead"));
	else if (r == SESSION_REFUSED) flash (TR ("Not now: a full-screen program has the display"));
	else flash (TR ("The interface could not be switched"));
}

// ---- Display ----------------------------------------------------------------------------------------------------------
static int g_dispSel = -1;				// the size chosen (not applied yet)
static int g_keepW, g_keepH;				// the size before the one tried
// (the sizes' words, given by SystemKit in English)
// TR: 4:3, XGA (Onyx's default)
// TR: 16:9, HD
// TR: 16:10, WXGA
// TR: 5:4, SXGA
// TR: 16:9, laptop screens
// TR: 16:10, WXGA+
// TR: 16:9, HD+
// TR: 4:3, UXGA
// TR: 16:10, WSXGA+
// TR: 16:9, Full HD
// TR: 16:10, WUXGA
// TR: 16:9, QHD
static void disp_rows (void)
{
	int cw = 0, ch = 0;
	kapi_screen_size (&cw, &ch);
	int n = display_modes ();
	if (g_dispSel < 0 || g_dispSel >= n)
		for (int i = 0; i < n; i++) { int w, h; display_mode (i, &w, &h); if (w == cw && h == ch) g_dispSel = i; }
	if (g_dispSel < 0) g_dispSel = 0;
	int w = 0, h = 0;
	const char *wd = display_mode (g_dispSel, &w, &h);
	char v[80]; size_words (v, sizeof v, w, h);
	if (wd) ws_cat (v, sizeof v, " (", TR (wd), ")");
	{
		static const char *const LOOK[3] = { TRN ("Lakka (the list)"), TRN ("Tiles, light"), TRN ("Tiles, dark") };
		row_add (R_CHOICE, 3, TR ("Look"), TR (LOOK[g_style]), TR ("The home: the consoles in a list, or as tiles on a light or a dark background")).v = g_style;
	}
	SRow &r = row_add (R_CHOICE, 1, TR ("Resolution"), v, TR ("Left / Right choose a size, A tries it (kept only when you say so)"));
	r.v = g_dispSel;
	size_words (v, sizeof v, cw, ch);
	row_add (R_INFO, 0, TR ("The screen now"), v, "");
	int ne = 0;
	char seen[GAMES_SYSTEMS_MAX][24]; int ns = 0;
	for (int s = 0; s < g_ngsys; s++)
	{
		bool d = false; for (int k = 0; k < ns; k++) if (ieq (seen[k], g_gsys[s].emu)) d = true;
		if (!d && ns < GAMES_SYSTEMS_MAX) { fs_copy (seen[ns++], g_gsys[s].emu, 24); ne++; }
	}
	snprintf (v, sizeof v, ne == 1 ? TR ("%d console") : TR ("%d consoles"), ne);
	row_add (R_SUB, 2, TR ("The games' resolutions"), v, TR ("Each emulator's screen size while it plays (SD:/etc/console.ini)"));
}
static void disp_try (void)
{
	int w = 0, h = 0;
	if (!display_mode (g_dispSel, &w, &h)) return;
	kapi_screen_size (&g_keepW, &g_keepH);
	if (w == g_keepW && h == g_keepH) { display_save_size (w, h); flash (TR ("This is the size in use: kept")); return; }
	int r = kapi_screen_set (w, h);
	if (r == 0) dlg_open (D_KEEP, 0, TR ("Keep this resolution?"), TR ("Nothing pressed: the previous size comes back by itself."), TR ("Keep"), TR ("Go back"), 0, 1, 1500);
	else if (r == -2) { display_save_size (w, h); flash (TR ("Not now: a full-screen app owns the display. Kept for the next start.")); }
	else if (r == -3) flash (TR ("The firmware refused this size: the screen kept its own"));
	else flash (TR ("The screen could not change its size"));
}
// The games' resolutions: one row per emulator (Game Boy and Game Boy Color share theirs)
static char g_emu[GAMES_SYSTEMS_MAX][24];
static int g_nemu;
static void games_rows (void)
{
	g_nemu = 0;
	int n = display_modes ();
	for (int s = 0; s < g_ngsys; s++)
	{
		int e = -1;
		for (int k = 0; k < g_nemu; k++) if (ieq (g_emu[k], g_gsys[s].emu)) e = k;
		if (e >= 0) { ws_cat (g_sr[e].label, sizeof g_sr[0].label, " / ", g_gsys[s].name); continue; }
		if (g_nemu >= GAMES_SYSTEMS_MAX) break;
		fs_copy (g_emu[g_nemu], g_gsys[s].emu, 24);
		int w = 0, h = 0, ow = 0, oh = 0, said = display_game_said (g_gsys[s].emu, &w, &h);
		char v[80] = "", ov[40] = "";
		if (display_game_own (g_gsys[s].emu, &ow, &oh)) size_words (ov, sizeof ov, ow, oh); else fs_copy (ov, TR ("the system's"), sizeof ov);
		if (said == 1) size_words (v, sizeof v, w, h);
		else if (said == DISPLAY_SYSTEM) fs_copy (v, TR ("System"), sizeof v);
		else ws_cat (v, sizeof v, TR ("its own: "), ov);
		char hl[160] = "";
		ws_cat (hl, sizeof hl, TR ("Its own: "), ov, TR ("; Y gives it back"));
		SRow &r = row_add (R_CHOICE, 1, g_gsys[s].name, v, hl);
		r.arg = g_nemu; r.icon = find_app (g_gsys[s].emu);
		r.v = said == 1 ? -3 : said == DISPLAY_SYSTEM ? -1 : -2;	// -2 its own, -1 system, else a mode
		if (said == 1) for (int i = 0; i < n; i++) { int mw, mh; display_mode (i, &mw, &mh); if (mw == w && mh == h) r.v = i; }
		g_nemu++;
	}
	if (g_nemu == 0) row_add (R_INFO, 0, TR ("No emulator installed"), "", "");
	else row_add (R_ACTION, 2, TR ("All back to their own"), "", TR ("Every emulator at the size its app.txt says"));
}
static void games_change (SRow &r, int d)
{
	if (r.id != 1 || r.arg >= g_nemu) return;
	int n = display_modes (), v = r.v == -3 ? -2 : r.v;	// (a size not in the list: from its own)
	v += d;
	if (v < -2) v = n - 1;
	if (v >= n) v = -2;
	const char *emu = g_emu[r.arg];
	if (v == -2) display_game_set (emu, DISPLAY_OWN, 0);
	else if (v == -1) display_game_set (emu, DISPLAY_SYSTEM, 0);
	else { int w, h; display_mode (v, &w, &h); display_game_set (emu, w, h); }
}

// ---- Games: the watched folders (GameKit: GAMES_CONFIG, the Game Library's -- not shown in the console) ----------------
// A folder is chosen on a small browser moved with the pad: the volumes (the card, its partitions, the USB sticks), then
// their folders; "Watch this folder" adds the one shown. The ROMs are read again at once (the home's columns follow).
static char g_fold[GAMES_FOLDERS_MAX][GAMES_PATH];
static int g_nfold;
static char g_brPath[GAMES_PATH];			// the browser's folder ("": the volumes)
#define BR_MAX	96
static char g_brName[BR_MAX][64];
static int g_nbr;
static void folders_read (void) { g_nfold = games_folders (g_fold, GAMES_FOLDERS_MAX); }
static int games_in (const char *folder)		// the ROMs found under a folder
{
	int n = 0, l = (int) strlen (folder);
	for (int r = 0; r < g_nroms; r++)
	{
		const char *p = g_roms[r].path;
		int k = 0;
		while (k < l && p[k] && (p[k] | 32) == (folder[k] | 32)) k++;
		if (k == l && (p[k] == '/' || folder[l - 1] == '/')) n++;
	}
	return n;
}
static void roms_again (void)				// the ROMs read again (the home's columns follow)
{
	roms_read (); g_thumbOf = -1; xmb_build ();
	char m[80]; snprintf (m, sizeof m, g_nroms == 1 ? TR ("%d game found") : TR ("%d games found"), g_nroms);
	flash (m);
}
static void br_read (void)				// the browser's entries: the volumes, or the folder's sub-folders (sorted)
{
	g_nbr = 0;
	if (!g_brPath[0])
	{
		struct kapi_volume v[16];
		int n = kapi_vol_list (v, 16, 0);
		for (int i = 0; i < n && i < 16 && g_nbr < BR_MAX; i++)
			if (v[i].state == KAPI_VST_MOUNTED && strcmp (v[i].name, "RAM")) snprintf (g_brName[g_nbr++], 64, "%s:", v[i].name);
		return;
	}
	void *d = kapi_opendir (g_brPath);
	if (!d) return;
	struct kapi_dirent e;
	while (g_nbr < BR_MAX && kapi_readdir (d, &e))
		if (e.is_dir && e.name[0] != '.') fs_copy (g_brName[g_nbr++], e.name, 64);
	kapi_closedir (d);
	for (int i = 1; i < g_nbr; i++)
		for (int j = i; j > 0; j--)
		{
			const char *a = g_brName[j - 1], *b = g_brName[j];
			int k = 0; while (a[k] && (a[k] | 32) == (b[k] | 32)) k++;
			if ((a[k] | 32) <= (b[k] | 32)) break;
			char t[64]; memcpy (t, g_brName[j], 64); memcpy (g_brName[j], g_brName[j - 1], 64); memcpy (g_brName[j - 1], t, 64);
		}
}
static void br_go (const char *path) { fs_copy (g_brPath, path, sizeof g_brPath); br_read (); if (g_nss) scr ().sel = 0; g_homeDirty = true; }
static void br_up (void)
{
	int l = (int) strlen (g_brPath);
	if (l >= 2 && g_brPath[l - 1] == '/' && g_brPath[l - 2] == ':') { br_go (""); return; }	// "SD:/" -> the volumes
	while (l > 0 && g_brPath[l - 1] != '/') l--;
	if (l > 0 && g_brPath[l - 2] == ':') g_brPath[l] = 0;	// "SD:/roms" -> "SD:/"
	else if (l > 0) g_brPath[l - 1] = 0;
	br_go (g_brPath);
}
static void roms_rows (void)
{
	folders_read ();
	char v[64];
	for (int f = 0; f < g_nfold; f++)
	{
		int n = games_in (g_fold[f]);
		snprintf (v, sizeof v, n == 1 ? TR ("%d game") : TR ("%d games"), n);
		row_add (R_SUB, 1, g_fold[f], v, TR ("Watched with its sub-folders: A to stop watching it")).arg = f;
	}
	if (!g_nfold) row_add (R_INFO, 0, TR ("No folder watched"), "", TR ("Add one: the games found there show in the home"));
	if (g_nfold < GAMES_FOLDERS_MAX) row_add (R_ACTION, 2, TR ("Add a folder..."), "", TR ("The card, its partitions and the USB sticks: choose a folder, then Watch this folder"));
	int nc = 0; for (int i = 0; i < g_nx; i++) if (g_x[i].kind == X_SYS) nc++;
	snprintf (v, sizeof v, TR ("%d games, %d consoles"), g_nroms, nc);
	row_add (R_ACTION, 3, TR ("Look for the games again"), v, TR ("After games were copied (Y does it anywhere on this page)"));
	row_add (R_INFO, 0, TR ("Title screens"), TR ("the Game Library's"), TR ("Made by the Game Library (desktop mode): it plays each game a moment"));
}
static void folder_rows (int f)
{
	if (f >= g_nfold) return;
	row_add (R_INFO, 0, g_fold[f], "", TR ("Watched with its sub-folders (6 deep)"));
	char v[32]; int n = games_in (g_fold[f]);
	snprintf (v, sizeof v, n == 1 ? TR ("%d game") : TR ("%d games"), n);
	row_add (R_INFO, 0, TR ("Games found"), v, "");
	row_add (R_ACTION, 1, TR ("Stop watching this folder"), "", TR ("Its games leave the home; the files stay"));
}
static void browse_rows (void)
{
	if (g_brPath[0])
	{
		row_add (R_ACTION, 1, TR ("Watch this folder"), g_brPath, TR ("Its games, and those of its sub-folders, show in the home"));
		row_add (R_SUB, 2, TR (".. (the folder above)"), "", "");
	}
	for (int i = 0; i < g_nbr; i++) row_add (R_SUB, 3, g_brName[i], "", g_brPath[0] ? "" : TR ("A volume: the card, a partition, a USB stick")).arg = i;
	if (!g_nbr) row_add (R_INFO, 0, g_brPath[0] ? TR ("No sub-folder") : TR ("No volume"), "", "");
}
static void roms_act (SRow &r)
{
	if (r.id == 1) { g_ss[g_nss++] = { SC_FOLDER, r.arg, 0, 0 }; return; }
	if (r.id == 2) { g_ss[g_nss++] = { SC_BROWSE, 0, 0, 0 }; br_go (g_nfold ? "" : "SD:/"); return; }
	if (r.id == 3) roms_again ();
}
static void browse_act (SRow &r)
{
	if (r.id == 2) { br_up (); return; }
	if (r.id == 3 && r.arg < g_nbr)
	{
		char p[GAMES_PATH];
		if (!g_brPath[0]) snprintf (p, sizeof p, "%s/", g_brName[r.arg]);
		else { int l = (int) strlen (g_brPath); snprintf (p, sizeof p, l && g_brPath[l - 1] == '/' ? "%s%s" : "%s/%s", g_brPath, g_brName[r.arg]); }
		br_go (p);
		return;
	}
	if (r.id != 1) return;
	char p[GAMES_PATH]; fs_copy (p, g_brPath, sizeof p);
	int l = (int) strlen (p);
	if (l > 1 && p[l - 1] == '/' && p[l - 2] != ':') p[--l] = 0;	// "SD:/roms/" -> "SD:/roms"
	if (l > 63) { flash (TR ("This folder's path is too long (63 characters at most)")); return; }
	folders_read ();
	for (int f = 0; f < g_nfold; f++) if (ieq (g_fold[f], p)) { flash (TR ("This folder is watched already")); return; }
	if (g_nfold >= GAMES_FOLDERS_MAX) { flash (TR ("Too many folders (8 at most)")); return; }
	fs_copy (g_fold[g_nfold++], p, GAMES_PATH);
	if (!games_folders_save (g_fold, g_nfold)) { flash (TR ("The Game Library's settings could not be written")); return; }
	g_nss--;						// (back to the page: the folder in its list)
	roms_again ();
}

// ---- the rows of the screen shown --------------------------------------------------------------------------------------
static void rows_build (void)
{
	g_nsr = 0;
	switch (scr ().kind)
	{
	case SC_ROMS: roms_rows (); break;
	case SC_FOLDER: folder_rows (scr ().arg); break;
	case SC_BROWSE: browse_rows (); break;
	case SC_SOUND: sound_rows (); break;
	case SC_PAD: pad_rows (-1); break;
	case SC_PADINFO: pad_rows (scr ().arg); break;
	case SC_KEYS: keys_rows (); break;
	case SC_KBD: kbd_rows (); break;
	case SC_LANG: lang_rows (); break;
	case SC_WIFI: wifi_rows (); break;
	case SC_NET: net_rows (); break;
	case SC_PKG: pkg_rows (); break;
	case SC_PKGLIST: pkglist_rows (scr ().arg); break;
	case SC_PKGONE: pkgone_rows (); break;
	case SC_MODE: mode_rows (); break;
	case SC_DISP: disp_rows (); break;
	case SC_GAMES: games_rows (); break;
	}
	Scr &s = scr ();
	if (s.sel >= g_nsr) s.sel = g_nsr - 1;
	if (s.sel < 0) s.sel = 0;
	while (s.sel < g_nsr - 1 && !selectable (g_sr[s.sel])) s.sel++;
}
static const char *scr_title (const Scr &s)
{
	switch (s.kind)
	{
	case SC_PADINFO: { static char t[24]; snprintf (t, sizeof t, TR ("Pad %d"), s.arg + 1); return t; }
	case SC_WIZ: return TR ("Map the buttons");
	case SC_FOLDER: return s.arg < g_nfold ? g_fold[s.arg] : TR ("Games");
	case SC_BROWSE: return g_brPath[0] ? g_brPath : TR ("Add a folder");
	case SC_KEYS: return TR ("Keyboard as pad 1");
	case SC_NET: return g_net.ssid;
	case SC_PKGLIST: return s.arg == 0 ? TR ("Installed") : s.arg == 1 ? TR ("Available") : TR ("Search");
	case SC_PKGONE: return g_pkgName;
	case SC_GAMES: return TR ("The games' resolutions");
	}
	return s.kind < SP_N ? TR (SP[s.kind].name) : "";
}

// ---- entering, leaving ------------------------------------------------------------------------------------------------
static void set_enter (int page)
{
	g_nss = 1; g_ss[0] = { page, 0, 0, 0 }; g_setOn = true;
	if (page == SC_WIFI && !g_scanT) scan_start ();
	if (page == SC_PKG) pkg_init ();
	if (page == SC_DISP) g_dispSel = -1;
	if (page == SC_PAD) pad_config_reload ();
	g_homeDirty = true;
}
static void set_back (void)
{
	g_keyWait = -1;
	if (g_nss > 1) g_nss--;
	else { g_setOn = false; g_nss = 0; }
	g_homeDirty = true;
}

static void dlg_done (int choice)
{
	Dlg d = g_dlg;
	g_dlg.on = false;
	g_homeDirty = true;
	switch (d.purpose)
	{
	case D_KEEP:
		if (choice == 0) { int w, h; kapi_screen_size (&w, &h); display_save_size (w, h); flash (TR ("Kept for every start")); }
		else { kapi_screen_set (g_keepW, g_keepH); flash (TR ("The previous size again")); }
		break;
	case D_FORGET_NET:
		if (choice == 0) { wifi_forget (g_net.ssid); flash (TR ("Forgotten")); if (g_nss > 1) g_nss--; wifi_list (); }
		break;
	case D_FORGET_PAD:
		if (choice == 0) { struct kapi_pad p; if (kapi_pad_state (d.arg, &p)) { pad_forget (p.vid, p.pid); pad_config_reload (); flash (TR ("Forgotten")); } }
		break;
	case D_MODE: if (choice == 0) mode_switch (d.arg, 0); break;
	case D_WAIT:
		if (choice == 0) mode_switch (d.arg, SESSION_NO_ASK);
		else if (choice == 1) mode_switch (d.arg, SESSION_NO_ASK | SESSION_FORCE);
		else { g_modeTo = -1; flash (TR ("Nothing changed: the interface stays as it is")); }
		break;
	case D_REMOVE: if (choice == 0) { const char *nm = g_pkgName; pkg_start (pkg::J_REMOVE, &nm, 1); } break;
	case D_RESTART: if (choice == 0) kapi_reboot (); break;
	case D_UNWATCH:
		if (choice == 0 && d.arg < g_nfold)
		{
			for (int k = d.arg; k < g_nfold - 1; k++) memcpy (g_fold[k], g_fold[k + 1], GAMES_PATH);
			g_nfold--;
			games_folders_save (g_fold, g_nfold);
			if (g_nss > 1) g_nss--;
			roms_again ();
		}
		break;
	}
}
static void osk_done (bool ok)
{
	Osk o = g_osk;
	if (ok && (int) strlen (o.t) < o.min)
	{
		char m[80]; snprintf (m, sizeof m, TR ("At least %d characters"), o.min);
		flash (m);
		return;
	}
	g_osk.on = false;
	g_homeDirty = true;
	if (!ok) return;
	switch (o.purpose)
	{
	case O_PASS: fs_copy (g_netPass, o.t, sizeof g_netPass); if (g_netJoin) { g_netJoin = false; net_connect (); } break;
	case O_HIDDEN:
	{
		WNet w; memset (&w, 0, sizeof w);
		fs_copy (w.ssid, o.t, sizeof w.ssid); w.security = WLAN_SEC_WPA2;
		for (int i = 0; i < g_nwn; i++) if (!strcmp (g_wn[i].ssid, w.ssid)) w = g_wn[i];
		net_open (w);
		break;
	}
	case O_SEARCH: fs_copy (g_pkgFind, o.t, sizeof g_pkgFind); if (g_pkgFind[0]) g_ss[g_nss++] = { SC_PKGLIST, 2, 0, 0 }; break;
	case O_TRY: break;
	}
	memset (g_osk.t, 0, sizeof g_osk.t);			// (a password: not left in memory)
}

// ---- the keys ---------------------------------------------------------------------------------------------------------
static void row_change (int d)
{
	Scr &s = scr ();
	if (s.sel >= g_nsr) return;
	SRow &r = g_sr[s.sel];
	switch (s.kind)
	{
	case SC_SOUND: sound_change (r, d); break;
	case SC_KBD: kbd_change (r, d); break;
	case SC_LANG: lang_change (r, d); break;
	case SC_WIFI: if (r.id == 4) { int k = r.v < 0 ? 0 : (r.v + d + NCC) % NCC; wifi_set_country (CC[k]); fs_copy (g_cc, CC[k], sizeof g_cc); flash (CC[k]); } break;
	case SC_NET: if (r.id == 3) g_netShow = !g_netShow; break;
	case SC_PKGONE:
		if (r.id == 2) { const char *nm = g_pkgName; int k = (r.v + d + 3) % 3; pkg_start (pkg::J_MODE, &nm, 1, PKG_MODES[k]); flash (mode_word (PKG_MODES[k])); }
		break;
	case SC_DISP:
		if (r.id == 1) { int n = display_modes (); g_dispSel = (g_dispSel + d + n) % n; }
		else if (r.id == 3) { int st = (g_style + d + 3) % 3; style_set (st); if (!style_save (st)) flash (TR ("SD:/etc/console.ini could not be written")); }
		break;
	case SC_GAMES: games_change (r, d); break;
	}
	g_homeDirty = true;
}
static void row_act (void)
{
	Scr &s = scr ();
	if (s.sel >= g_nsr) return;
	SRow &r = g_sr[s.sel];
	switch (s.kind)
	{
	case SC_ROMS: roms_act (r); break;
	case SC_FOLDER: if (r.id == 1) dlg_open (D_UNWATCH, s.arg, TR ("Stop watching this folder?"), TR ("Its games leave the home; the files stay on the volume."), TR ("Stop watching"), TR ("Cancel")); break;
	case SC_BROWSE: browse_act (r); break;
	case SC_SOUND: sound_act (r); break;
	case SC_PAD: case SC_PADINFO: pad_act (r); break;
	case SC_KEYS: keys_act (r); break;
	case SC_KBD: if (r.id == 2) osk_open (O_TRY, TR ("Type with the keyboard to try it"), "", false); break;
	case SC_WIFI: wifi_act (r); break;
	case SC_NET: net_act (r); break;
	case SC_PKG: case SC_PKGLIST: pkg_act (r, s.sel); break;
	case SC_PKGONE: pkgone_act (r); break;
	case SC_MODE: mode_act (r); break;
	case SC_DISP: if (r.id == 1) disp_try (); else if (r.id == 2) g_ss[g_nss++] = { SC_GAMES, 0, 0, 0 }; break;
	case SC_GAMES: if (r.id == 2) { display_game_reset (); flash (TR ("Every emulator at its own size")); } break;
	}
	g_homeDirty = true;
}
static void scr_y (void)				// Y: the page's second action
{
	Scr &s = scr ();
	SRow *r = s.sel < g_nsr ? &g_sr[s.sel] : 0;
	if (s.kind == SC_WIFI) scan_start ();
	else if (s.kind == SC_ROMS) roms_again ();
	else if (s.kind == SC_PKG || s.kind == SC_PKGLIST) osk_open (O_SEARCH, TR ("Search the packages"), g_pkgFind, false, 0, 40);
	else if (s.kind == SC_GAMES && r && r->id == 1 && r->arg < g_nemu) display_game_set (g_emu[r->arg], DISPLAY_OWN, 0);
	g_homeDirty = true;
}
static void move_row (int d)
{
	Scr &s = scr ();
	int i = s.sel, step = d > 0 ? 1 : -1, n = d > 0 ? d : -d;
	while (n-- > 0)
	{
		int j = i + step;
		while (j >= 0 && j < g_nsr && !selectable (g_sr[j])) j += step;
		if (j < 0 || j >= g_nsr) break;
		i = j;
	}
	s.sel = i;
	g_homeDirty = true;
}
// A key (the keyboard's, or the pad's through pad_poll) while the settings are up.
static void set_key (int k)
{
	if (g_dlg.on)
	{
		if (k == KEY_LEFT) g_dlg.sel = (g_dlg.sel + g_dlg.n - 1) % g_dlg.n;
		else if (k == KEY_RIGHT) g_dlg.sel = (g_dlg.sel + 1) % g_dlg.n;
		else if (k == KEY_ENTER || k == K_A || k == K_START) dlg_done (g_dlg.sel);
		else if (k == 0x1b || k == K_B || k == KEY_BACKSPACE) dlg_done (g_dlg.safe);
		g_homeDirty = true;
		return;
	}
	if (g_osk.on) { osk_keypress (k); return; }
	if (scr ().kind == SC_WIZ)
	{
		if (k == 0x1b) { pad_learn_skip (&g_learn); g_wizT = kapi_get_ticks (); }
		else if (k == KEY_BACKSPACE) { g_nss--; flash (TR ("Nothing changed")); }
		g_homeDirty = true;
		return;
	}
	if (g_keyWait >= 0 && k != K_A && k != K_B && k != K_X && k != K_Y && k != K_START && k != K_L3) { keys_take (k); return; }
	rows_build ();
	switch (k)
	{
	case KEY_UP: move_row (-1); break;
	case KEY_DOWN: move_row (1); break;
	case KEY_PGUP: move_row (-8); break;
	case KEY_PGDN: move_row (8); break;
	case KEY_LEFT: row_change (-1); break;
	case KEY_RIGHT: row_change (1); break;
	case KEY_ENTER: case K_A: row_act (); break;
	case 0x1b: case K_B: case KEY_BACKSPACE: if (g_keyWait >= 0) g_keyWait = -1; else set_back (); break;
	case K_Y: case 'y': case 'Y': scr_y (); break;
	case K_X: case 'x': case 'X': if (scr ().kind == SC_PKG || scr ().kind == SC_PKGLIST) pkg_x (g_sr[scr ().sel], scr ().sel); break;
	}
	g_homeDirty = true;
}
static bool set_grabs_pad (void) { return g_setOn && g_nss && scr ().kind == SC_WIZ; }

// ---- each turn: the work that runs, the values that move ----------------------------------------------------------------
static void set_tick (void)
{
	if (g_dlg.on && g_dlg.ticks)
	{
		if (kapi_get_ticks () - g_dlg.t0 >= g_dlg.ticks) dlg_done (g_dlg.safe);
		else { static unsigned s_s; unsigned s = (kapi_get_ticks () - g_dlg.t0) / 25; if (s != s_s) { s_s = s; g_homeDirty = true; } }
	}
	if (g_scanBusy && g_scanDone)
	{
		kapi_thread_join (g_scanTid, 1000, 0);
		g_scanBusy = false; g_scanT = kapi_get_ticks ();
		if (g_scanN < 0) g_scanN = 0;
		wifi_list ();
		g_homeDirty = true;
	}
	if (g_rescanAt && (int) (kapi_get_ticks () - g_rescanAt) >= 0) { g_rescanAt = 0; scan_start (); }
	if (pkg::g_job.running)
	{
		static int s_pct = -1; static char s_cur[40];
		if (pkg::g_job.done) { pkg_after (); s_pct = -1; }
		else if (pkg::g_job.pct != s_pct || strcmp (s_cur, pkg::g_job.cur)) { s_pct = pkg::g_job.pct; fs_copy (s_cur, pkg::g_job.cur, sizeof s_cur); g_homeDirty = true; }
	}
	mode_tick ();
	if (!g_setOn || !g_nss) return;
	if (scr ().kind == SC_WIZ) { wiz_tick (); return; }
	static unsigned s_t, s_sig;
	if (kapi_get_ticks () - s_t >= 25)			// (live values: the pads held, the programs playing, the scan's age)
	{
		s_t = kapi_get_ticks ();
		int k = scr ().kind;
		unsigned sig = 0;
		if (k == SC_SOUND)
		{
			struct kapi_sound_client c[8];
			int n = kapi_sound_clients (c, 8);
			sig = (unsigned) n * 977u + (unsigned) kapi_sound_volume (-1, -1) + (unsigned) kapi_sound_output (-1) * 31u;
			for (int i = 0; i < n; i++) sig = sig * 31u + c[i].pid + (unsigned) c[i].volume * 7u + (unsigned) c[i].mute;
		}
		else if (k == SC_PAD || k == SC_PADINFO) { struct kapi_pad p; sig = pad_any (); for (int i = 0; i < 4; i++) sig = sig * 3u + (unsigned) kapi_pad_state (i, &p); }
		else if (k == SC_WIFI) sig = (kapi_get_ticks () - g_scanT) / 100;
		if (sig != s_sig) { s_sig = sig; g_homeDirty = true; }
	}
	if (g_flash[0] && kapi_get_ticks () - g_flashT > 220) { g_flash[0] = 0; g_homeDirty = true; }
}

// ---- drawing ----------------------------------------------------------------------------------------------------------
struct SMet { bool c; int px, pgap, pw, rx, rh, vr, y0, isz, lab, val, help, seg, sgap, segh, tw, th; };
static SMet sm (void)
{
	SMet m;
	bool c = g_sh * 100 / S < 560;
	m.c = c;
	m.px = D (c ? 22 : 110); m.pgap = D (c ? 30 : 46); m.pw = D (c ? 40 : 232);
	m.rx = D (c ? 76 : 420); m.rh = D (c ? 30 : 54); m.vr = g_sw - D (c ? 20 : 60);
	m.y0 = D (c ? 64 : 120); m.isz = D (c ? 18 : 30);
	m.lab = c ? 13 : 19; m.val = c ? 12 : 16; m.help = c ? 10 : 13;
	m.seg = D (c ? 9 : 14); m.sgap = D (c ? 3 : 4); m.segh = D (c ? 9 : 12);
	m.tw = D (c ? 32 : 44); m.th = D (c ? 18 : 24);
	return m;
}
static void page_icon (Canvas &cv, int i, int cx, int cy, int s, int a)
{
	App *ap = find_app (SP[i].app);
	if (ap) app_icon_at (cv, *ap, cx, cy, s, a);
	else white_icon (cv, XI_GEAR, 0, cx, cy, s, a);
}
static void draw_bars (Canvas &cv, int x, int y, int h, int n, unsigned c)	// four rising bars, n lit; x its left
{
	int bw = h / 5 > 1 ? h / 5 : 2, g = bw / 2 + 1;
	for (int i = 0; i < 4; i++)
	{
		int bh = h * (i + 1) / 4;
		lk_fill (cv, x + i * (bw + g), y + h - bh, bw, bh, 1, i < n ? c : 0x6A80A8, i < n ? c : 0x6A80A8, i < n ? 255 : 120);
	}
}
static int bars_w (int h) { int bw = h / 5 > 1 ? h / 5 : 2; return 4 * bw + 3 * (bw / 2 + 1); }
static void draw_lock (Canvas &cv, int x, int y, int h, unsigned c)	// a padlock h high
{
	int w = h * 3 / 4;
	lk_ring (cv, x + w / 6, y, w * 2 / 3, h * 2 / 3, w / 3, 20, c, 230);
	lk_fill (cv, x, y + h * 2 / 5, w, h * 3 / 5, h / 8, c, c, 255);
}
// A row's value at the right edge -> its left x.
static int draw_value (Canvas &cv, const SMet &m, const SRow &r, int y, int h, bool on)
{
	int x = m.vr;
	const Pal &P = *g_pal;
	unsigned ink = r.warn ? 0xF2B24A : r.good ? (P.dark ? 0x7CE08A : 0x2E9E4A) : on ? P.valOn : P.val;
	UkFaceScope f (F (m.val));
	int ty = y + (h - uk_fh ()) / 2;
	auto txt = [&] (const char *s, unsigned c) { int w = tw (s); x -= w; uk_text (cv, x, ty, s, c); x -= D (8); };
	switch (r.kind)
	{
	case R_SUB: txt ("\xE2\x80\xBA", on ? P.on : P.arrow); if (r.value[0]) txt (r.value, ink); break;
	case R_CHOICE:
		if (on) { txt ("\xE2\x80\xBA", P.on); x += D (4); }
		if (r.value[0]) { char b[80]; uk_text_fit (r.value, m.vr - m.rx - D (200), b, sizeof b); txt (b, ink); }
		if (on) { x += D (4); txt ("\xE2\x80\xB9", P.on); }
		break;
	case R_SLIDER:
	{
		char n[16];
		if (r.max == 100) { int k = 0; n[0] = 0; num_cat (n, sizeof n, &k, r.v); lx_cat (n, sizeof n, &k, " %"); } else ax_itoa (r.v, n);
		if (on) txt ("\xE2\x80\xBA", P.on);
		if (r.value[0]) txt (r.value, 0xF2B24A);
		txt (n, on ? P.on : ink);
		int segs = 10, lit = r.max ? (r.v * segs + r.max / 2) / r.max : 0;
		if (r.max == 16) { segs = 16; lit = r.v; }
		int sw = segs == 16 ? m.seg * 2 / 3 : m.seg, wtot = segs * sw + (segs - 1) * m.sgap;
		x -= wtot;
		for (int i = 0; i < segs; i++)
			lk_fill (cv, x + i * (sw + m.sgap), y + (h - m.segh) / 2, sw, m.segh, D (2), i < lit ? P.segOn : P.segOff, i < lit ? P.segOn : P.segOff, i < lit || !P.dark ? 255 : 150);
		x -= D (8);
		if (on) txt ("\xE2\x80\xB9", P.on);
		break;
	}
	case R_TOGGLE:
	{
		int tx = x - m.tw, tyy = y + (h - m.th) / 2;
		lk_fill (cv, tx, tyy, m.tw, m.th, m.th / 2, r.on ? P.togOn : P.togOff, r.on ? P.togOn : P.togOff, 255);
		if (g_style == ST_XMB) lk_ring (cv, tx, tyy, m.tw, m.th, m.th / 2, 16, r.on ? 0xBFE0FF : 0x6A84B8, 200);
		int k = m.th - D (6);
		lk_fill (cv, r.on ? tx + m.tw - k - D (3) : tx + D (3), tyy + D (3), k, k, k / 2, 0xFFFFFF, 0xE0E8F4, 255);
		x = tx - D (8);
		break;
	}
	case R_NET:
	{
		int bh = D (m.c ? 12 : 18);
		x -= bars_w (bh);
		draw_bars (cv, x, y + (h - bh) / 2, bh, r.v, P.on);
		x -= D (10);
		if (r.lock) { x -= bh * 3 / 4; draw_lock (cv, x, y + (h - bh) / 2, bh, P.sub); x -= D (10); }
		if (r.value[0]) txt (r.value, ink);
		break;
	}
	case R_RADIO:
	{
		int d = m.th;
		x -= d;
		lk_ring (cv, x, y + (h - d) / 2, d, d, d / 2, 24, on ? P.on : P.sub, 255);
		if (r.on) lk_fill (cv, x + d / 4, y + (h - d) / 2 + d / 4, d / 2, d / 2, d / 4, 0x7CE08A, 0x7CE08A, 255);
		x -= D (12);
		if (r.value[0]) txt (r.value, ink);
		break;
	}
	default:
		if (r.value[0]) { char b[80]; uk_text_fit (r.value, m.vr - m.rx - D (200), b, sizeof b); txt (b, r.kind == R_INFO && !r.warn && !r.good ? (g_style == ST_XMB ? 0xB4C6E8 : P.sub) : ink); }
		break;
	}
	if (r.pct >= 0)						// a progress bar on its row
	{
		int bw = D (m.c ? 70 : 120), bh = D (m.c ? 5 : 7);
		x -= bw;
		lk_fill (cv, x, y + (h - bh) / 2, bw, bh, bh / 2, P.segOff, P.segOff, 220);
		lk_fill (cv, x, y + (h - bh) / 2, bw * r.pct / 100 > bh ? bw * r.pct / 100 : bh, bh, bh / 2, P.acc, P.acc, 255);
		x -= D (8);
	}
	return x;
}
static void draw_wizard (Canvas &cv, const SMet &m, int y0, int H)
{
	int x = m.rx, w = m.vr - m.rx;
	char t[64];
	if (g_wizArmed) { UkFaceScope f (F (m.lab)); text_fit (cv, x, y0, w, TR ("Let go of every button..."), 0xFFFFFF); return; }
	int st = g_learn.step < PAD_NBUTTONS ? g_learn.step : PAD_NBUTTONS - 1;
	snprintf (t, sizeof t, TR ("Step %d of %d"), st + 1, PAD_NBUTTONS);
	{ UkFaceScope f (F (m.help + 2)); uk_text (cv, x, y0, t, 0xC8D6F0); }
	int y = y0 + D (m.c ? 22 : 36);
	{ UkFaceScope f (F (m.c ? 14 : 22)); text_fit (cv, x, y, w, TR ("Press"), 0xFFFFFF, 2); y += uk_fh () + D (6); }
	{
		UkFaceScope f (F (m.c ? 13 : 19));
		const char *s = TR (pad_learn_text[st]);
		int sl[4], ln[4], n = uk_text_wrap (s, (int) strlen (s), w, 3, sl, ln);
		for (int i = 0; i < n; i++) { char b[160]; int l = ln[i] < 159 ? ln[i] : 159; memcpy (b, s + sl[i], (size_t) l); b[l] = 0; uk_text (cv, x, y, b, 0x8CD0FF, 2); y += uk_fh () + D (4); }
	}
	y += D (m.c ? 10 : 20);
	int dot = D (m.c ? 8 : 12), g = D (m.c ? 5 : 8);			// a dot per step: done, now, to come
	for (int i = 0; i < PAD_NBUTTONS; i++)
	{
		unsigned c = i < st ? 0x7CE08A : i == st ? 0xFFFFFF : 0x4A6498;
		lk_fill (cv, x + i * (dot + g), y, dot, dot, dot / 2, c, c, 255);
	}
	y += dot + D (m.c ? 12 : 24);
	UkFaceScope f (F (m.help));
	int left = 8 - (int) (kapi_get_ticks () - g_wizT) / 100; if (left < 0) left = 0;
	snprintf (t, sizeof t, TR ("No such button on this pad? In %d s it is skipped."), left);
	text_fit (cv, x, y, w, t, 0xC8D6F0);
	y += uk_fh () + D (4);
	text_fit (cv, x, y, w, TR ("A keyboard: Esc skips, Backspace stops (nothing kept)."), 0x9AB0D8);
	(void) H;
}
static void draw_dialog (Canvas &cv, const SMet &m)
{
	int W = g_sw, H = g_sh;
	const Pal &P = *g_pal;
	bool sw = g_style != ST_XMB;			// (the tiles' themes: the panel's colours)
	unsigned cB1 = sw ? P.panel : 0x18305E, cB2 = sw ? P.panel : 0x10244A, cRg = sw ? P.line : 0x6E96D8, cTi = sw ? P.fg : 0xFFFFFF,
		 cTx = sw ? P.sub : 0xD2E0F8, cBo = sw ? P.segOff : 0x2A3E68, cBa = sw ? P.acc : 0x7CC4FF, cSe = sw ? P.sub : 0xC8D6F0;
	uk_paint_alpha (true);
	for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) lk_px (cv, x, y, 0x020814, 120);	// (the page dimmed)
	int w = D (m.c ? 400 : 560); if (w > W - D (20)) w = W - D (20);
	int bw = D (m.c ? 120 : 150), bh = D (m.c ? 34 : 42), pad = D (m.c ? 14 : 24);
	int sl[5], ln[5], n;
	{ UkFaceScope f (F (m.c ? 12 : 15)); n = uk_text_wrap (g_dlg.text, (int) strlen (g_dlg.text), w - 2 * pad, 5, sl, ln); }
	int lh; { UkFaceScope f (F (m.c ? 12 : 15)); lh = uk_fh () + D (4); }
	int th; { UkFaceScope f (F (m.c ? 15 : 20)); th = uk_fh (); }
	int h = pad + th + D (10) + n * lh + (g_dlg.ticks ? D (18) : 0) + D (12) + bh + pad;
	int x = (W - w) / 2, y = (H - h) / 2;
	lk_shadow (cv, x, y, w, h, D (14), D (16), 140, D (6));
	lk_fill (cv, x, y, w, h, D (14), cB1, cB2, 250);
	lk_ring (cv, x, y, w, h, D (14), 16, cRg, 230);
	int yy = y + pad;
	{ UkFaceScope f (F (m.c ? 15 : 20)); text_fit (cv, x + pad, yy, w - 2 * pad, g_dlg.title, cTi, 2); }
	yy += th + D (10);
	{
		UkFaceScope f (F (m.c ? 12 : 15));
		for (int i = 0; i < n; i++) { char b[200]; int l = ln[i] < 199 ? ln[i] : 199; memcpy (b, g_dlg.text + sl[i], (size_t) l); b[l] = 0; uk_text (cv, x + pad, yy, b, cTx); yy += lh; }
	}
	if (g_dlg.ticks)						// the countdown
	{
		unsigned el = kapi_get_ticks () - g_dlg.t0; if (el > g_dlg.ticks) el = g_dlg.ticks;
		char s[32]; snprintf (s, sizeof s, TR ("%d s"), (int) (g_dlg.ticks - el + 99) / 100);
		UkFaceScope f (F (m.help));
		int sw = tw ("00 s") + D (10), bwid = w - 2 * pad - sw, left = (int) ((g_dlg.ticks - el) * (unsigned) bwid / g_dlg.ticks);
		yy += D (6);
		lk_fill (cv, x + pad, yy, bwid, D (5), D (2), cBo, cBo, 255);
		lk_fill (cv, x + pad, yy, left > D (5) ? left : D (5), D (5), D (2), cBa, cBa, 255);
		uk_text (cv, x + w - pad - tw (s), yy + D (2) - uk_fh () / 2, s, cSe);
		yy += D (12);
	}
	yy += D (12);
	int tot = g_dlg.n * bw + (g_dlg.n - 1) * D (14), bx = x + (w - tot) / 2;
	for (int i = 0; i < g_dlg.n; i++, bx += bw + D (14))
	{
		bool on = i == g_dlg.sel;
		if (on && sw) lk_fill (cv, bx, yy, bw, bh, bh / 2, P.acc, P.acc, 255);
		else if (on) glow (cv, bx, yy, bw, bh, bh / 2, false);
		else if (sw) { lk_fill (cv, bx, yy, bw, bh, bh / 2, P.neutral, P.neutral, 255); lk_ring (cv, bx, yy, bw, bh, bh / 2, 16, P.line, 255); }
		else { lk_fill (cv, bx, yy, bw, bh, bh / 2, 0x223A6A, 0x1A3060, 255); lk_ring (cv, bx, yy, bw, bh, bh / 2, 16, 0x5E7EB8, 200); }
		UkFaceScope f (F (m.val));
		text_cfit (cv, bx, yy + (bh - uk_fh ()) / 2, bw, g_dlg.b[i], on ? 0xFFFFFF : sw ? P.fg : 0xC8D6F0, on ? 2 : 0);
		g_homeHits.add (bx, yy, bw, bh, H_DBTN, i);
	}
	uk_paint_alpha (false);
}
static void draw_osk (Canvas &cv, const SMet &m)
{
	int W = g_sw, H = g_sh;
	const Pal &P = *g_pal;
	bool sw = g_style != ST_XMB;			// (the tiles' themes: the panel's colours)
	uk_paint_alpha (true);
	for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) lk_px (cv, x, y, 0x020814, 110);
	int kw = D (m.c ? 40 : 56), kh = D (m.c ? 34 : 48), g = D (m.c ? 5 : 8), pad = D (m.c ? 10 : 16);
	int gw = 10 * kw + 9 * g, pw = gw + 2 * pad;
	if (pw > W - D (8)) { kw = (W - D (8) - 2 * pad - 9 * g) / 10; gw = 10 * kw + 9 * g; pw = gw + 2 * pad; }
	int fh = D (m.c ? 46 : 64);
	int ph = pad + fh + D (10) + 5 * kh + 4 * g + pad;
	int bar = sw ? H - tm ().bary : xm ().bar;
	int x = (W - pw) / 2, y = H - bar - D (8) - ph;
	if (y < D (4)) y = D (4);
	if (g_osk.purpose == O_SEARCH && g_pm)		// the matches, live, above the keyboard
	{
		int n = 0, ly = D (m.c ? 56 : 110);
		UkFaceScope f (F (m.val));
		for (int i = 0; i < g_pm->index.n && ly + uk_fh () < y - D (6); i++)
		{
			const pkg::Pkg &p = g_pm->index.p[i];
			if (!g_osk.t[0] || !(contains_ci (p.name, g_osk.t) || contains_ci (p.title, g_osk.t) || contains_ci (p.summary, g_osk.t))) continue;
			text_fit (cv, x + pad, ly, pw - 2 * pad, p.title, sw ? P.fg : 0xFFFFFF);
			ly += uk_fh () + D (6); n++;
		}
		if (!n && g_osk.t[0]) text_fit (cv, x + pad, ly, pw - 2 * pad, TR ("Nothing matches"), sw ? P.sub : 0xC8D6F0);
	}
	lk_shadow (cv, x, y, pw, ph, D (14), D (16), 140, D (6));
	lk_fill (cv, x, y, pw, ph, D (14), sw ? P.panel : 0x16305C, sw ? P.panel : 0x0E2148, 250);
	lk_ring (cv, x, y, pw, ph, D (14), 16, sw ? P.line : 0x6E96D8, 230);
	// the field: its label, the text and its caret (a secret: dots, the last letter a moment, L3 shows it)
	{
		UkFaceScope f (F (m.help));
		text_fit (cv, x + pad, y + pad, gw, g_osk.label, sw ? P.sub : 0xC8D6F0);
	}
	int fy = y + pad + D (m.c ? 16 : 22), fhh = fh - D (m.c ? 16 : 22);
	{ unsigned fb = sw ? (P.dark ? 0x26262B : 0xFFFFFF) : 0x0A1834; lk_fill (cv, x + pad, fy, gw, fhh, D (8), fb, fb, 255); }
	lk_ring (cv, x + pad, fy, gw, fhh, D (8), 16, sw ? P.acc : 0x8CB4EC, 220);
	{
		UkFaceScope f (F (m.lab));
		char shown[200]; int k = 0, cx = 0; shown[0] = 0;
		int n = (int) strlen (g_osk.t);
		bool recent = kapi_get_ticks () - g_osk.lastT < 100;
		for (int i = 0; i <= n; i++)
		{
			if (i == g_osk.caret) cx = k;
			if (i == n) break;
			if ((g_osk.t[i] & 0xC0) == 0x80) continue;
			int l = 1; while (i + l < n && (g_osk.t[i + l] & 0xC0) == 0x80) l++;
			bool last = i + l == g_osk.caret && recent;
			if (g_osk.secret && !g_osk.show && !last) { if (k + 3 < 199) { memcpy (shown + k, "\xE2\x80\xA2", 3); k += 3; } }
			else if (k + l < 199) { memcpy (shown + k, g_osk.t + i, (size_t) l); k += l; }
		}
		shown[k] = 0;
		char pre[200]; memcpy (pre, shown, (size_t) cx); pre[cx] = 0;
		int tx = x + pad + D (10), ty = fy + (fhh - uk_fh ()) / 2, avail = gw - D (40), pw2 = tw (pre);
		int off = pw2 > avail ? pw2 - avail : 0;			// (a long text: its end in view)
		Canvas sub; sub.adopt (cv.px + (long) fy * cv.stride + x + pad + D (4), gw - D (36), fhh, cv.stride);
		uk_text (sub, tx - (x + pad + D (4)) - off, ty - fy, shown, sw ? P.fg : 0xFFFFFF);
		lk_fill (cv, tx + pw2 - off, ty, D (2), uk_fh (), 0, sw ? P.acc : 0x8CD0FF, sw ? P.acc : 0x8CD0FF, 255);
		if (g_osk.secret)					// the eye: shown or not (L3)
		{
			int ex = x + pad + gw - D (26), ey = fy + fhh / 2;
			lk_ring (cv, ex, ey - D (6), D (18), D (12), D (6), 16, g_osk.show ? 0xFFFFFF : 0x6A84B8, 255);
			lk_fill (cv, ex + D (6), ey - D (3), D (6), D (6), D (3), g_osk.show ? 0xFFFFFF : 0x6A84B8, g_osk.show ? 0xFFFFFF : 0x6A84B8, 255);
		}
	}
	// the keys
	int ky = y + pad + fh + D (10);
	static const char *const SPW_TXT[7] = { TRN ("Shift"), "?123", TRN ("Space"), "\xE2\x80\xB9", "\xE2\x80\xBA", TRN ("Delete"), TRN ("Done") };
	static const char *const SPW_BTN[7] = { "Y", "Y", "X", "L1", "R1", "B", "Start" };
	for (int r = 0; r < 5; r++, ky += kh + g)
	{
		int kx = x + pad;
		int n = r < 4 ? 10 : 7;
		for (int c = 0; c < n; c++)
		{
			int w = r < 4 ? kw : (OSK_SPW[c] * (kw + g)) / 10 - g;
			bool on = g_osk.r == r && g_osk.c == c;
			bool lit = r == 4 && ((c == 0 && g_osk.page == 1) || (c == 1 && g_osk.page == 2));
			if (on && sw) lk_fill (cv, kx, ky, w, kh, D (8), P.acc, P.acc, 255);
			else if (on) glow (cv, kx, ky, w, kh, D (8), false);
			else if (sw) { unsigned kc = lit ? lighter (P.acc, P.dark ? 0 : 150) : r == 4 ? (P.dark ? lighter (P.neutral, 10) : 0xE6E7EC) : P.neutral; lk_fill (cv, kx, ky, w, kh, D (8), kc, kc, 255); if (!P.dark) lk_ring (cv, kx, ky, w, kh, D (8), 16, P.line, 255); }
			else lk_fill (cv, kx, ky, w, kh, D (8), lit ? 0x3A64A8 : r == 4 ? 0x1E3666 : 0x26407A, lit ? 0x30589A : r == 4 ? 0x1A305C : 0x1E3870, 255);
			const char *t = r < 4 ? osk_key (r, c) : c == 1 && g_osk.page == 2 ? "abc" : c < 3 || c >= 5 ? TR (SPW_TXT[c]) : SPW_TXT[c];
			{
				UkFaceScope f (F (r < 4 ? m.lab : m.help + 1));
				int ty = r < 4 ? ky + (kh - uk_fh ()) / 2 : ky + D (m.c ? 3 : 6);
				text_cfit (cv, kx, ty, w, t, on ? 0xFFFFFF : sw ? P.fg : 0xDCE6F8, on ? 2 : 0);
			}
			if (r == 4 && !m.c)
			{
				UkFaceScope f (F (9));
				text_cfit (cv, kx, ky + kh - uk_fh () - D (4), w, SPW_BTN[c], sw ? P.sub : 0x8CA4D0);
			}
			g_homeHits.add (kx, ky, w, kh, H_KEY, r * 16 + c);
			kx += w + g;
		}
	}
	uk_paint_alpha (false);
}
// The foot's hints of the settings: right-aligned, those that work here.
static void tiles_hints (Canvas &cv, const char *const *b, const char *const *w, int n);
static void draw_settings_sw (Canvas &cv);
static void draw_set_hints (Canvas &cv)
{
	XMet xmm = xm ();
	int W = g_sw, H = g_sh, y0 = H - xmm.bar;
	struct Hn { const char *b; unsigned ring; const char *w; } hs[8];
	int n = 0;
	auto add = [&] (const char *b, unsigned ring, const char *w) { if (n < 8) hs[n++] = { b, ring, w }; };
	const unsigned GR = 0x9AA8C0, BL = 0x5E9CFF, RD = 0xFF6A6A, YL = 0xF2C84A, GN = 0x6AD08A;
	if (g_dlg.on) { add ("\xE2\x97\x80\xE2\x96\xB6", GR, TR ("Choose")); add ("B", RD, TR ("Cancel")); add ("A", BL, TR ("OK")); }
	else if (g_osk.on)
	{
		if (!xmm.c) add ("L1 R1", GR, TR ("Cursor"));
		add ("Y", YL, TR ("Shift")); add ("X", GN, TR ("Space")); add ("B", RD, TR ("Delete")); add ("A", BL, TR ("Type")); add ("Start", GR, TR ("Done"));
	}
	else if (scr ().kind == SC_WIZ) { add ("Esc", GR, TR ("Skip")); add ("\xE2\x8C\xAB", GR, TR ("Stop")); }
	else
	{
		const SRow *r = scr ().sel < g_nsr ? &g_sr[scr ().sel] : 0;
		int k = scr ().kind;
		if (k == SC_WIFI || k == SC_PKG || k == SC_PKGLIST || k == SC_GAMES || k == SC_ROMS)
			add ("Y", YL, k == SC_WIFI ? TR ("Scan") : k == SC_GAMES ? TR ("Its own") : k == SC_ROMS ? TR ("Look again") : TR ("Search"));
		if ((k == SC_PKG || k == SC_PKGLIST) && r && r->id == 2) add ("X", GN, TR ("Updates mode"));
		if (r && (r->kind == R_CHOICE || r->kind == R_SLIDER || r->kind == R_TOGGLE)) add ("\xE2\x97\x80\xE2\x96\xB6", GR, TR ("Change"));
		add ("B", RD, TR ("Back"));
		if (r && r->kind != R_INFO && r->kind != R_HEAD && r->kind != R_CHOICE && r->kind != R_SLIDER)
			add ("A", BL, r->kind == R_SUB || r->kind == R_NET ? TR ("Enter") : r->kind == R_TOGGLE ? TR ("Switch") : r->kind == R_RADIO ? TR ("Choose") : TR ("Do"));
	}
	if (g_style != ST_XMB)				// (the tiles' themes: filled round buttons on a thin line)
	{
		const char *B[8], *Wd[8];
		for (int i = 0; i < n; i++) { B[i] = hs[i].b; Wd[i] = hs[i].w; }
		tiles_hints (cv, B, Wd, n);
		return;
	}
	int tot = 0;
	for (int i = 0; i < n; i++) tot += hint_w (hs[i].b, hs[i].w);
	int hx = W - xmm.tx - tot + D (22), hy;
	{ UkFaceScope f (F (12)); hy = y0 + (xmm.bar - (uk_fh () + D (8))) / 2; }
	for (int i = 0; i < n; i++) hx += hint (cv, hx, hy, hs[i].b, hs[i].ring, hs[i].w);
	if (!xmm.c && W - xmm.tx - tot > D (260))
	{
		char l[120] = "Onyx  -  ";
		ws_cat (l, sizeof l, TR ("Settings"), "  \xE2\x80\xBA  ", TR (SP[g_ss[0].kind].name));
		UkFaceScope f (F (12));
		text_fit (cv, xmm.tx, y0 + (xmm.bar - uk_fh ()) / 2, W - xmm.tx - tot - D (40), l, 0xC8D6F0);
	}
}
// The screen's rows from m.y0 to bottom, scrolled to keep the chosen one in sight: its label (an icon before it), its
// value at m.vr, its help line under the chosen one. XMB: the chosen one on a glass; the tiles' themes: thin lines
// between the rows, the chosen one in the accent ring (docs/COMPACT-SHELL-STUDY.md §19.4).
static void tiles_ring (Canvas &cv, int x, int y, int w, int h, int r);
static void draw_set_rows (Canvas &cv, SMet &m, int bottom)
{
	const Pal &P = *g_pal;
	bool sw = g_style != ST_XMB;
	Scr &s = scr ();
	int hh; { UkFaceScope f (F (m.help)); hh = uk_fh () + D (6); }
	int fit = (bottom - m.y0 - hh) / m.rh;
	if (fit < 1) fit = 1;
	if (s.sel < s.top) s.top = s.sel;
	if (s.sel >= s.top + fit) s.top = s.sel - fit + 1;
	if (s.top > 0 && s.top + fit > g_nsr) s.top = g_nsr - fit > 0 ? g_nsr - fit : 0;
	int y = m.y0, pad = sw ? D (m.c ? 10 : 20) : 0;
	auto hline = [&] (int yy) { if (sw) lk_fill (cv, m.rx - pad, yy, m.vr - m.rx + 2 * pad, D (1) > 1 ? D (1) : 1, 0, P.line, P.line, 255); };
	for (int i = s.top; i < g_nsr && y + m.rh <= bottom; i++)
	{
		SRow &r = g_sr[i];
		bool on = i == s.sel && !g_dlg.on && !g_osk.on;
		bool foc = i == s.sel;
		int rh = r.kind == R_HEAD ? m.rh * 3 / 4 : m.rh;
		if (r.kind == R_HEAD)
		{
			UkFaceScope f (F (m.help + 1));
			uk_text (cv, m.rx, y + rh - uk_fh () - D (4), r.label, P.head, 2);
			uk_paint_alpha (true);
			lk_fill (cv, m.rx, y + rh - D (2), m.vr - m.rx, 1 > D (1) ? 1 : D (1), 0, P.head, P.head, 70);
			uk_paint_alpha (false);
			y += rh;
			continue;
		}
		int full = rh + (foc && r.help[0] ? hh : 0);
		uk_paint_alpha (true);
		if (sw) hline (y);
		if (foc && !sw)
		{
			lk_fill (cv, m.rx - D (14), y + D (2), m.vr - m.rx + D (28), full - D (4), D (10), 0xFFFFFF, 0xFFFFFF, on ? 34 : 18);
			lk_ring (cv, m.rx - D (14), y + D (2), m.vr - m.rx + D (28), full - D (4), D (10), 16, 0xBFE4FF, on ? 120 : 50);
		}
		int vx = draw_value (cv, m, r, y, rh, on);
		int lx = m.rx;
		if (r.icon) { app_icon_at (cv, *r.icon, m.rx + m.isz / 2, y + rh / 2, m.isz, foc ? 255 : 200); lx += m.isz + D (12); }
		{
			UkFaceScope f (F (m.lab));
			text_fit (cv, lx, y + (rh - uk_fh ()) / 2, vx - lx - D (8), r.label, foc ? P.on : r.kind == R_INFO && !sw ? 0xC8D6F0 : P.lab, foc ? 2 : 0);
		}
		if (foc && r.help[0])
		{
			UkFaceScope f (F (m.help));
			text_fit (cv, lx, y + rh - D (6), m.vr - lx, r.help, P.help);
		}
		if (foc && on && sw) tiles_ring (cv, m.rx - pad, y + D (3), m.vr - m.rx + 2 * pad, full - D (6), D (12));
		uk_paint_alpha (false);
		g_homeHits.add (m.rx - D (14), y, m.vr - m.rx + D (28), full, H_ROW, i);
		y += full;
	}
	if (sw) { uk_paint_alpha (true); hline (y); uk_paint_alpha (false); }
	if (s.top > 0) { UkFaceScope f (F (m.val)); uk_text (cv, m.vr - D (10), m.y0 - uk_fh () - D (2), "\xE2\x96\xB4", P.sub); }
}
static void draw_settings (Canvas &cv)
{
	if (g_style != ST_XMB) { draw_settings_sw (cv); return; }
	SMet m = sm ();
	XMet xmm = xm ();
	int W = g_sw, H = g_sh;
	rows_build ();
	// the top: the gear faded, the page's name, the path; the time
	{
		int gs = D (m.c ? 22 : 34);
		white_icon (cv, XI_GEAR, 0, xmm.tx + gs / 2, xmm.ty + gs / 2, gs, 150);
		UkFaceScope f (F (xmm.title));
		int x = xmm.tx + gs + D (12);
		const char *t = scr_title (scr ());
		text_fit (cv, x, xmm.ty, W / 2, t, 0xFFFFFF, 2);
		int x2 = x + tw (t, 2) + D (14);
		int hh = 0, mi = 0;
		kapi_get_datetime (0, 0, 0, &hh, &mi, 0);
		char tm[8] = { (char) ('0' + hh / 10), (char) ('0' + hh % 10), ':', (char) ('0' + mi / 10), (char) ('0' + mi % 10), 0 };
		uk_text (cv, W - xmm.tx - tw (tm), xmm.ty, tm, 0xFFFFFF);
		if (g_nss > 1 && !m.c)
		{
			char p[120] = "";
			for (int i = 0; i < g_nss - 1; i++) { if (p[0]) ws_cat (p, sizeof p, "  \xE2\x80\xBA  "); ws_cat (p, sizeof p, scr_title (g_ss[i])); }
			UkFaceScope f2 (F (xmm.count));
			text_fit (cv, x2, xmm.ty + D (xmm.title - xmm.count) * 3 / 4, W / 2 - x2 + W / 4, p, 0xC8D6F0);
		}
	}
	// the flash, under the time: a value set, a result
	{
		bool vol = kapi_get_ticks () - g_volFlashT < 150 && g_volFlashT;
		if (g_flash[0] || vol)
		{
			char s[140] = "";
			if (vol) { int v = kapi_sound_volume (-1, -1); snprintf (s, sizeof s, (v & 0x100) ? TR ("Volume %d (muted)") : TR ("Volume %d"), v & 0xFF); }
			else fs_copy (s, g_flash, sizeof s);
			UkFaceScope f (F (m.val));
			char b[140]; int w = uk_text_fit (s, W / 2, b, sizeof b) + D (28), h = uk_fh () + D (12);
			int x = W - xmm.tx - w, y = xmm.ty + D (m.c ? 24 : 40);
			uk_paint_alpha (true);
			lk_fill (cv, x, y, w, h, h / 2, 0x101C38, 0x0A1428, 220);
			lk_ring (cv, x, y, w, h, h / 2, 16, 0x5E86C8, 220);
			uk_paint_alpha (false);
			uk_text (cv, x + D (14), y + D (6), b, 0xFFFFFF);
		}
	}
	// the parent column: the pages, the current one bigger (compact: a rail of icons)
	{
		int cur = g_ss[0].kind, y = m.y0 + m.rh / 2;
		for (int i = 0; i < SP_N; i++)
		{
			bool on = i == cur;
			int s = on ? D (m.c ? 22 : 34) : D (m.c ? 16 : 24), cy = y + (i - cur) * m.pgap + (i > cur ? D (6) : i < cur ? -D (6) : 0);
			if (cy - s / 2 < xmm.ty + D (xmm.title) + D (14) || cy + s / 2 > H - xmm.bar - D (4)) continue;	// (under the title)
			int a = on ? 230 : 90;
			page_icon (cv, i, m.px + s / 2, cy, s, a);
			if (!m.c)
			{
				UkFaceScope f (F (on ? 17 : 14));
				text_fit (cv, m.px + D (44), cy - uk_fh () / 2, m.pw - D (44), TR (SP[i].name), fade (on ? 220 : 100), on ? 2 : 0);
			}
			g_homeHits.add (m.px - D (6), cy - m.pgap / 2, m.pw, m.pgap, H_PAGE, i);
		}
	}
	// the rows
	int bottom = H - xmm.bar - D (8);
	if (scr ().kind == SC_WIZ) draw_wizard (cv, m, m.y0, bottom);
	else draw_set_rows (cv, m, bottom);
	if (g_osk.on) draw_osk (cv, m);
	if (g_dlg.on) draw_dialog (cv, m);
	draw_set_hints (cv);
}

// The pointer on the settings: a click chooses a row (on the chosen one: A), a page, a dialog's button, a key; the wheel
// moves the rows.
static void set_ptr (int ev, int x, int y, int c, long v)
{
	const Hit *h = g_homeHits.at (x, y);
	if (ev == GUI_EVENT_PTR_WHEEL && !g_dlg.on && !g_osk.on) { rows_build (); move_row (-GUI_PTR_WHEEL (v)); return; }
	if (!(ev == GUI_EVENT_PTR_DOWN && (c & 1))) return;
	if (!h) return;
	if (g_dlg.on) { if (h->kind == H_DBTN) dlg_done (h->i); return; }
	if (g_osk.on) { if (h->kind == H_KEY) { g_osk.r = h->i / 16; g_osk.c = h->i % 16; osk_press (); g_homeDirty = true; } return; }
	if (h->kind == H_PAGE) { if (h->i != g_ss[0].kind) set_enter (h->i); return; }
	if (h->kind == H_ROW)
	{
		rows_build ();
		if (h->i == scr ().sel) row_act ();
		else { scr ().sel = h->i; g_homeDirty = true; }
	}
}
