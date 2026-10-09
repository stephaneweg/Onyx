//
// wifimenu -- the Wi-Fi menu the menu bar opens (a click on its Wi-Fi icon): the networks around,
// strongest first (kapi_wlan_scan), the one we are on marked; click one to join it -- a password
// field for a secured network not known yet, then Connect. Joined WITHOUT a reboot: the network
// goes into SD:/etc/wpa_supplicant.conf (all the known ones kept, the chosen one first by its
// priority), then kapi v60 wlan_reconnect (wpa_supplicant reads the file again, DHCP starts over).
//
// A notification-style box under the icon, a normal borderless window (it gets the keyboard for
// the password): it closes with Esc, when another window takes the keyboard, or once connected.
// "Wi-Fi Settings..." opens wpaconf (the country, the fields by hand).
//
#include "appkit/appkit.h"
#include "uikit/uikit.h"
#include "netkit/netkit.h"

using namespace uikit;

#define W		330
#define HEAD		48
#define ROW		30
#define EXTRA		66			// the open row's password / Connect part
#define FOOT		34
#define MAXNET		12
#define HMAX		(HEAD + MAXNET * ROW + EXTRA + FOOT)	// the window's buffer (shown: its first g_h rows)

// The panel's colours: the theme's, as the menu bar's drop-downs (set in main, once the theme is
// read); the open row (joining) a tint of the accent -- its widgets blend into it.
static unsigned C_BOX, C_LINE, C_TXT, C_DIMT, C_LINK, C_OPEN;

// ---- the networks around -------------------------------------------------------------------------
struct Net { char ssid[33]; int level, security; bool connected, known; };
static Net g_net[MAXNET]; static int g_nnet = 0;
static bool g_scanning = true;
static int g_open = -1, g_hover = -1;			// the row opened (joining), hovered
static char g_status[96] = "Scanning...";
static int g_fw = 8, g_fh = 16;

// ---- the known networks: SD:/etc/wpa_supplicant.conf (NetKit's wifi.h: all kept, the chosen one first) ---------------
static struct wifi_known g_known[WIFI_KNOWN_MAX]; static int g_nknown = 0;

static int slen (const char *s) { int n = 0; while (s[n]) n++; return n; }
static void scpy (char *d, const char *s, int cap) { int i = 0; for (; s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = 0; }
static bool seq (const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static void cat (char *d, int *n, int cap, const char *s) { while (*s && *n < cap - 1) d[(*n)++] = *s++; d[*n] = 0; }

static void load_known (void) { g_nknown = wifi_known_load (g_known, WIFI_KNOWN_MAX, 0, 0); }
static bool known (const char *ssid) { for (int i = 0; i < g_nknown; i++) if (seq (g_known[i].ssid, ssid)) return true; return false; }

static void scan (void)
{
	static struct kapi_wlan_ap ap[40];
	int n = kapi_wlan_scan (ap, 40);
	load_known ();
	g_nnet = 0;
	for (int i = 0; i < n && g_nnet < MAXNET; i++)		// strongest first; one row per name
	{
		if (!ap[i].ssid[0] || ap[i].ssid[0] == ' ') continue;	// (hidden)
		bool dup = false;
		for (int k = 0; k < g_nnet; k++) if (seq (g_net[k].ssid, ap[i].ssid)) { dup = true; if (ap[i].connected) g_net[k].connected = true; }
		if (dup) continue;
		Net &x = g_net[g_nnet++];
		scpy (x.ssid, ap[i].ssid, sizeof x.ssid);
		x.level = ap[i].level; x.security = ap[i].security; x.connected = ap[i].connected; x.known = known (x.ssid);
	}
	for (int i = 0; i < g_nnet; i++)				// the one we are on, first
		if (g_net[i].connected && i > 0) { Net t = g_net[i]; for (int k = i; k > 0; k--) g_net[k] = g_net[k - 1]; g_net[0] = t; break; }
	g_scanning = false;
	if (n < 0 || (n == 0 && !kapi_net_status (0, 0))) scpy (g_status, "No network found (is the Wi-Fi up?)", sizeof g_status);
	else scpy (g_status, g_nnet && g_net[0].connected ? "Connected" : "Not connected", sizeof g_status);
}

// ---- the window --------------------------------------------------------------------------------------
static Textbox *g_pass; static Checkbox *g_show; static Button *g_connect;
static Root *g_root;
static int g_h = HMAX;
static enum { IDLE, JOINING, JOINED, FAILED } g_join = IDLE;
static unsigned g_joinT = 0; static bool g_sawDown = false;
static char g_joinSsid[33];

static int row_y (int i) { return HEAD + i * ROW + (g_open >= 0 && i > g_open ? EXTRA : 0); }
static int list_h (void) { return (g_nnet ? g_nnet : 1) * ROW + (g_open >= 0 ? EXTRA : 0); }
static bool needs_password (const Net &n) { return n.security != WLAN_SEC_OPEN && !n.known; }

static void relayout (void)
{
	int h = HEAD + list_h () + FOOT;
	bool show = g_open >= 0 && needs_password (g_net[g_open]);
	int y = g_open >= 0 ? row_y (g_open) + ROW + 4 : 0;
	g_pass->hidden = !show; g_show->hidden = !show; g_connect->hidden = g_open < 0;
	g_pass->left = 14; g_pass->top = y; g_show->left = 14; g_show->top = y + 32;
	g_connect->left = W - 104; g_connect->top = show ? y + 30 : y + 8;
	if (h != g_h) { g_h = h; g_root->setBounds (W, h); uk_win_resize (W, h); }
	g_root->invalidate (true);
}

static void join (void)
{
	if (g_open < 0 || g_join == JOINING) return;
	const Net &n = g_net[g_open];
	if (n.security == WLAN_SEC_WEP) { scpy (g_status, "WEP networks are not supported", sizeof g_status); g_root->invalidate (true); return; }
	int pl = slen (g_pass->text);
	if (needs_password (n) && (pl < 8 || pl > 63)) { scpy (g_status, "The password is 8 to 63 characters", sizeof g_status); g_root->invalidate (true); return; }
	int r = wifi_join (n.ssid, n.security, needs_password (n) ? g_pass->text : 0);	// (NetKit: kept with the others, first)
	if (r == WIFI_EFULL) { scpy (g_status, "Too many known networks (Wi-Fi Settings)", sizeof g_status); g_root->invalidate (true); return; }
	if (r == WIFI_EWRITE || r < 0) { scpy (g_status, "Cannot write /etc/wpa_supplicant.conf", sizeof g_status); g_root->invalidate (true); return; }
	if (r == WIFI_SAVED) { scpy (g_status, "Saved: reboot to join (no live Wi-Fi)", sizeof g_status); g_root->invalidate (true); return; }
	scpy (g_joinSsid, n.ssid, sizeof g_joinSsid);
	g_join = JOINING; g_joinT = kapi_get_ticks (); g_sawDown = false;
	int m = 0; g_status[0] = 0; cat (g_status, &m, sizeof g_status, "Connecting to "); cat (g_status, &m, sizeof g_status, n.ssid); cat (g_status, &m, sizeof g_status, "...");
	g_root->invalidate (true);
}
static void on_connect (Widget &) { join (); }
static void on_show (Widget &) { g_pass->password = !g_show->checked; g_pass->invalidate (true); }

// signal bars (4) by the level in dBm
static void bars (Canvas &c, int x, int y, int level, unsigned on, unsigned off)
{
	int n = level >= -55 ? 4 : level >= -67 ? 3 : level >= -78 ? 2 : level >= -88 ? 1 : 0;
	for (int i = 0; i < 4; i++) c.fillRect (x + i * 4, y + 12 - (i + 1) * 3, 3, (i + 1) * 3, i < n ? on : off);
}
static void lock (Canvas &c, int x, int y, unsigned col)
{
	c.frameRect (x + 1, y, 6, 5, col);
	c.fillRect (x, y + 4, 8, 6, col);
}

// A floating panel (the menu bar's drop-downs' look): light, rounded, outlined; see-through
// round its corners (WIN_FLAG_ALPHA).
class MenuRoot : public Root
{
public:
	MenuRoot (int x, int y) : Root (x, y, W, HMAX, "Wi-Fi", WIN_FLAG_BORDERLESS | WIN_FLAG_SYSTEM | WIN_FLAG_ALPHA) {}
	void onDraw () override
	{
		int h = height;
		canvas.clear (0xFF000000);
		uk_paint_alpha (true);
		uk_rbox (canvas, 0, 0, W, h, 8, uk_tone (C_BOX, 140), C_BOX);
		uk_rline (canvas, 0, 0, W, h, 8, C_LINE, 200);
		uk_paint_alpha (false);
		uk_text_l (canvas, 14, 8, g_fh, "Wi-Fi", C_TXT, 2);
		canvas.text (14, 8 + g_fh + 2, g_status, g_join == FAILED ? 0x00C03030 : C_DIMT);
		uk_etch_h (canvas, 10, HEAD - 3, W - 20, C_BOX);
		if (g_scanning) canvas.text (14, HEAD + 7, "Looking for networks...", C_DIMT);
		else if (g_nnet == 0) canvas.text (14, HEAD + 7, "No network around", C_DIMT);
		for (int i = 0; i < g_nnet; i++)
		{
			const Net &n = g_net[i];
			int y = row_y (i);
			bool hot = i == g_hover && i != g_open;
			unsigned bg = i == g_open ? C_OPEN : hot ? C_ACCENT : C_BOX;
			if (i == g_open) uk_rbox (canvas, 4, y, W - 8, ROW + EXTRA, 6, C_OPEN, C_OPEN);
			else if (hot) uk_hilite (canvas, 4, y, W - 8, ROW, 6, true);
			unsigned ink = hot ? uk_hilite_ink (true) : C_TXT, dim = hot ? ink : C_DIMT;
			bars (canvas, 14, y + 8, n.level, ink, uk_mix (bg, ink, 64));
			canvas.text (40, y + (ROW - g_fh) / 2, n.ssid, ink);
			int rx = W - 14;
			if (n.connected) { const char *t = "Connected"; rx -= slen (t) * g_fw; canvas.text (rx, y + (ROW - g_fh) / 2, t, hot ? ink : C_LINK); rx -= 8; }
			else if (n.known) { const char *t = "Known"; rx -= slen (t) * g_fw; canvas.text (rx, y + (ROW - g_fh) / 2, t, dim); rx -= 8; }
			if (n.security != WLAN_SEC_OPEN) lock (canvas, rx - 10, y + 10, dim);
		}
		int fy = h - FOOT + 9;
		uk_etch_h (canvas, 10, h - FOOT, W - 20, C_BOX);
		canvas.text (14, fy, "Refresh", C_LINK);
		const char *s = "Wi-Fi Settings...";
		canvas.text (W - 14 - slen (s) * g_fw, fy, s, C_LINK);
	}
	bool onMouse (int mx, int my, int bl, int, int, int) override
	{
		static bool was = false;
		bool press = bl && !was; was = bl != 0;
		int hover = -1;
		for (int i = 0; i < g_nnet; i++) if (my >= row_y (i) && my < row_y (i) + ROW) hover = i;
		if (hover != g_hover) { g_hover = hover; invalidate (true); }
		if (!press) return true;
		if (my >= height - FOOT)					// the footer links
		{
			if (mx < W / 2) { g_scanning = true; scpy (g_status, "Scanning...", sizeof g_status); g_open = -1; relayout (); draw (); uk_win_present (); scan (); relayout (); }
			else { lx_launch ("wpaconf", 0); kapi_exit (0); }
			return true;
		}
		if (hover >= 0 && g_join != JOINING)
		{
			if (g_net[hover].connected) { g_open = -1; relayout (); return true; }
			g_open = hover == g_open ? -1 : hover;
			g_pass->setText ("");
			relayout ();
			if (g_open >= 0 && needs_password (g_net[g_open])) { g_pass->setFocus (); scpy (g_status, "Type the password, then Connect", sizeof g_status); }
		}
		return true;
	}
	bool onKey (long k) override
	{
		if (k == 27) { kapi_exit (0); return true; }
		if (k == KEY_ENTER) { join (); return true; }
		return false;
	}
};

// Still the window with the keyboard? (another one took it: a click elsewhere -> close, as a menu)
static bool have_keys (void)
{
	static struct kapi_win_info wi[32];
	int n = uk_win_list (wi, 32);
	for (int i = 0; i < n; i++) if ((wi[i].state & KAPI_WIN_KEYS) && seq (wi[i].title, "Wi-Fi")) return true;
	return n <= 0;							// (an older kernel: never closed this way)
}

int main (void)
{
	g_fw = uk_fw (); g_fh = uk_fh ();
	int sw = 1024, sh = 768; kapi_screen_size (&sw, &sh);
	MenuRoot root (sw - W - 60, 34);				// under the Wi-Fi icon, near the right
	if (root.canvas.px == 0) return 1;
	g_root = &root;
	C_BOX = C_FIELD; C_LINE = uk_tone (C_FACE, 70); C_TXT = C_FIELD_TEXT;	// (the theme is read)
	C_DIMT = uk_mix (C_FIELD, C_FIELD_TEXT, 130); C_LINK = uk_tone (C_ACCENT, 84);
	C_OPEN = uk_mix (C_FIELD, C_ACCENT, 56);
	root.setBg (C_OPEN);						// (the open row's widgets blend into it)
	g_pass = new Textbox (14, 0, W - 132, g_fh + 10, "", on_connect); g_pass->password = true; root.addChild (g_pass);
	g_show = new Checkbox (14, 0, 150, g_fh + 6, "Show password", false, on_show, C_OPEN); root.addChild (g_show);
	g_connect = new Button (W - 104, 0, 90, 28, "Connect", on_connect); root.addChild (g_connect);
	relayout ();
	root.attach ();
	root.draw (); uk_win_present ();
	scan ();
	relayout ();
	bool hadKeys = false; unsigned lastKeys = 0;
	while (!should_exit ())
	{
		pump_events ();
		unsigned now = kapi_get_ticks ();
		if (now - lastKeys >= 20)					// the keyboard elsewhere: close
		{
			lastKeys = now;
			bool k = have_keys ();
			if (k) hadKeys = true;
			else if (hadKeys && g_join != JOINING) return 0;
		}
		if (g_join == JOINING)						// DHCP starts over: down, then up
		{
			bool up = kapi_net_status (0, 0) != 0;
			if (!up) g_sawDown = true;
			if (up && (g_sawDown || now - g_joinT > 600) && now - g_joinT > 200)
			{
				g_join = JOINED; g_joinT = now;
				int m = 0; g_status[0] = 0; cat (g_status, &m, sizeof g_status, "Connected to "); cat (g_status, &m, sizeof g_status, g_joinSsid);
				root.invalidate (true);
			}
			else if (now - g_joinT > 3000)
			{
				g_join = FAILED;
				scpy (g_status, "Could not connect: check the password", sizeof g_status);
				root.invalidate (true);
			}
		}
		if (g_join == JOINED && now - g_joinT > 150) return 0;		// (1.5 s to read it)
		if (!root.valid) { root.draw (); uk_win_present (); }
		msleep (16);
	}
	return 0;
}
