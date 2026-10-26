//
// consolehome/appkeys.h -- the console's on-screen keyboard for the APPS (the user, 2026-10-14: "how do I type an address
// in the browser with the pad?"). Settings' own keyboard (xset.h, osk_*) edits a field of this shell; this one types into
// the app in front, as a keyboard would (kapi_inject_key: the server gives the keys to the app in front -- this shell
// does not grab them meanwhile).
//
//   * It comes by itself when the app's focused text field changes (PocketUI's UK_SHELL_EV_TEXT, the console's too
//     since this keyboard) while a pad is plugged in, and goes with it -- unless it was asked for: a tap on SELECT
//     (pressed and released alone) shows it / hides it over any app the pad types for (not the games).
//   * The pad on it: the d-pad (or the left stick) chooses a key, A types it, B deletes (Backspace), X a space, Y the
//     next page (Shift -> ?123 -> abc), L1 / R1 the caret (the arrows), Start Enter; Select hides it.
//   * Its window is opaque, as wide as the screen, on its bottom edge: PocketUI makes it a band -- the app is laid out
//     again above it (wm.cpp's rule, the pocket keyboard's).
//   * An app may ask the pad's Y for one of its keys (app.txt `pad_y = f6`: Jet's address bar) instead of Tab.
//
// MIT licence (docs/LICENSING.md).
//
enum { W_KEYS = 3 };
struct AppKeys { bool on, manual; int r, c, page, hint, kind; };
static AppKeys g_ak;
static Canvas g_kc;
static int g_keysStride, g_kH;
static bool g_kDirty;
static const char *const AK_SPECIAL[7] = { TRN ("Shift"), "?123", TRN ("Space"), "\xE2\x80\xB9", "\xE2\x80\xBA", TRN ("Delete"), TRN ("Enter") };
static const char *const AK_BTN[7] = { "Y", "Y", "X", "L1", "R1", "B", "Start" };

static bool ak_pads (void)				// a pad plugged in
{
	struct kapi_pad p;
	for (int i = 0; i < PAD_MAX; i++) if (kapi_pad_state (i, &p)) return true;
	return false;
}
static int ak_key_h (void)
{
	int kh = D (46);
	int most = (g_sh * 44 / 100 - D (16) - D (34)) / 5 - D (6);	// (no more than 44% of the screen: a band, at most half)
	return kh < most ? kh : most;
}
static int ak_height (void) { return D (16) + 5 * ak_key_h () + 4 * D (6) + D (34); }
static const char *ak_label (int r, int c)		// the letter / sign of a key of the grid on the page shown
{
	static char one[2];
	if (g_ak.page == 2) return OSK_SYM[r == 0 ? 3 : r - 1][c];
	one[0] = OSK_ROWS[g_ak.kind][r][c]; one[1] = 0;
	if (g_ak.page == 1 && one[0] >= 'a' && one[0] <= 'z') one[0] -= 32;
	return one;
}
// A key's text typed into the app: ASCII as it is, a Latin-1 letter (two UTF-8 bytes) as its one byte -- as the pocket
// keyboard types; a sign outside Latin-1 (the euro) is not typed.
static void ak_type (const char *s)
{
	unsigned char a = (unsigned char) s[0];
	char out[2] = { 0, 0 };
	if (a < 0x80) out[0] = (char) a;
	else if ((a & 0xE0) == 0xC0 && (unsigned char) s[1] >= 0x80) out[0] = (char) (((a & 0x1F) << 6) | ((unsigned char) s[1] & 0x3F));
	else return;
	kapi_inject_key (out);
}
static void ak_place (void)
{
	uk_win_select (W_KEYS);
	if (g_ak.on)
	{
		g_kH = ak_height ();
		unsigned *fb = uk_win_resize (g_sw, g_kH);
		if (fb) g_kc.adopt (fb, g_sw, g_kH, g_keysStride);
		uk_win_move (0, g_sh - g_kH); uk_win_raise (0);
	}
	else uk_win_move (-g_sw - 50, g_sh / 2);		// (parked: not on the screen's edge, so no band)
	uk_win_select (0);
}
static void ak_show (bool manual, int hint)
{
	bool was = g_ak.on;
	if (!was)
	{
		char km[16] = ""; input_keymap_now (km, sizeof km);
		int k = input_keymap_kind (km);
		g_ak.kind = k == 1 ? 1 : k == 2 ? 2 : 0;
		g_ak.r = 1; g_ak.c = 0;
	}
	g_ak.on = true; g_ak.manual = manual || (was && g_ak.manual);
	g_ak.page = hint == UK_IN_NUMBER || hint == UK_IN_DECIMAL ? 2 : 0;
	if (!was) ak_place ();
	g_kDirty = true;
}
static void ak_hide (void)
{
	if (!g_ak.on) return;
	g_ak.on = false; g_ak.manual = false;
	ak_place ();
}
static void ak_toggle (void) { if (g_ak.on) ak_hide (); else ak_show (true, -1); }
// The app's focused field changed (PocketUI): its type, -1 none.
static void ak_hint (int type)
{
	g_ak.hint = type;
	if (type >= 0) { if (g_padKeys && !g_menu && !g_home && ak_pads ()) ak_show (false, type); }
	else if (!g_ak.manual) ak_hide ();
}
static void ak_draw (void)
{
	g_kDirty = false;
	if (!g_ak.on || g_kc.px == 0) return;
	Canvas &cv = g_kc;
	int W = g_sw, H = g_kH, kh = ak_key_h (), g = D (6);
	lk_fill (cv, 0, 0, W, H, 0, 0x16305C, 0x0A1834, 255);
	lk_fill (cv, 0, 0, W, D (2), 0, 0x6E96D8, 0x6E96D8, 255);
	int kw = (W - D (24) - 9 * g) / 10;
	if (kw > D (84)) kw = D (84);
	int gw = 10 * kw + 9 * g, x0 = (W - gw) / 2, ky = D (12);
	for (int r = 0; r < 5; r++, ky += kh + g)
	{
		int kx = x0, n = r < 4 ? 10 : 7;
		for (int c = 0; c < n; c++)
		{
			int w = r < 4 ? kw : (OSK_SPW[c] * (kw + g)) / 10 - g;
			bool on = g_ak.r == r && g_ak.c == c;
			bool lit = r == 4 && ((c == 0 && g_ak.page == 1) || (c == 1 && g_ak.page == 2));
			if (on) glow (cv, kx, ky, w, kh, D (8), false);
			else lk_fill (cv, kx, ky, w, kh, D (8), lit ? 0x3A64A8 : r == 4 ? 0x1E3666 : 0x26407A, lit ? 0x30589A : r == 4 ? 0x1A305C : 0x1E3870, 255);
			const char *t = r < 4 ? ak_label (r, c) : c == 1 && g_ak.page == 2 ? "abc" : c < 3 || c >= 5 ? TR (AK_SPECIAL[c]) : AK_SPECIAL[c];
			{
				UkFaceScope f (F (r < 4 ? 17 : 12));
				int ty = r < 4 ? ky + (kh - uk_fh ()) / 2 : ky + D (4);
				text_cfit (cv, kx, ty, w, t, on ? 0xFFFFFF : 0xDCE6F8, on ? 2 : 0);
			}
			if (r == 4 && kh >= D (36))
			{
				UkFaceScope f (F (9));
				text_cfit (cv, kx, ky + kh - uk_fh () - D (3), w, AK_BTN[c], 0x8CA4D0);
			}
			kx += w + g;
		}
	}
	// the buttons, at the foot
	int hx = x0, hy = H - D (28);
	hx += hint (cv, hx, hy, "A", 0x5E9CFF, TR ("Type"));
	hx += hint (cv, hx, hy, "B", 0xFF6A6A, TR ("Delete"));
	hx += hint (cv, hx, hy, "X", 0x6AD08A, TR ("Space"));
	hx += hint (cv, hx, hy, "Y", 0xF2C84A, TR ("Shift"));
	hx += hint (cv, hx, hy, "Start", 0x9AA8C0, TR ("Enter"));
	hint (cv, hx, hy, "Select", 0x9AA8C0, TR ("Hide"));
	uk_win_select (W_KEYS); uk_win_present (); uk_win_select (0);
}
static void ak_press (void)				// A on the key chosen
{
	if (g_ak.r < 4)
	{
		ak_type (ak_label (g_ak.r, g_ak.c));
		if (g_ak.page == 1) g_ak.page = 0;		// (a capital: one letter, as the phones)
		return;
	}
	switch (g_ak.c)
	{
	case 0: g_ak.page = g_ak.page == 1 ? 0 : 1; break;
	case 1: g_ak.page = g_ak.page == 2 ? 0 : 2; break;
	case 2: kapi_inject_key (" "); break;
	case 3: kapi_inject_key ("\x1b[D"); break;
	case 4: kapi_inject_key ("\x1b[C"); break;
	case 5: kapi_inject_key ("\x7f"); break;
	case 6: kapi_inject_key ("\n"); break;
	}
}
// The pad while the keyboard is up (pad_poll: an app in front).
static void ak_pad (unsigned now)
{
	static unsigned last, t0;
	unsigned b = pad_any ();
	unsigned press = b & ~last;
	bool rep = (b & (PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT)) && (int) (now - t0) > 16;
	last = b;
	if (!press && !rep) return;
	unsigned d = press ? press : b;
	if (d & (PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT))
	{
		t0 = now + (press ? 22 : 0);
		if (d & (PAD_UP | PAD_DOWN))
		{
			if (g_ak.r == 4) g_ak.c = col_of_sp (g_ak.c);
			g_ak.r = (g_ak.r + ((d & PAD_UP) ? 4 : 1)) % 5;
			if (g_ak.r == 4) g_ak.c = sp_col_of (g_ak.c);
		}
		else { int n = g_ak.r == 4 ? 7 : 10; g_ak.c = (g_ak.c + ((d & PAD_LEFT) ? n - 1 : 1)) % n; }
		g_kDirty = true;
	}
	if (press & PAD_A) ak_press ();
	if (press & PAD_B) kapi_inject_key ("\x7f");
	if (press & PAD_X) kapi_inject_key (" ");
	if (press & PAD_Y) g_ak.page = (g_ak.page + 1) % 3;
	if (press & PAD_L) kapi_inject_key ("\x1b[D");
	if (press & PAD_R) kapi_inject_key ("\x1b[C");
	if (press & PAD_START) kapi_inject_key ("\n");
	if (press) g_kDirty = true;
}
static void ak_make (void)
{
	unsigned *kb = 0;
	g_keysStride = g_sw;
	if (uk_win_new (0, 0, g_sw, g_sh, "consolehome keyboard", WIN_FLAG_TOPMOST | WIN_FLAG_BORDERLESS | WIN_FLAG_SYSTEM, &kb) != W_KEYS) return;
	g_kc.adopt (kb, g_sw, g_sh, g_keysStride);
	uk_win_select (W_KEYS); uk_win_move (-g_sw - 50, g_sh / 2); uk_win_present (); uk_win_select (0);
}
