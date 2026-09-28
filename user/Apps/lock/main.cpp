//
// lock -- the screen locked (the dock's lock button): the whole screen (a full-screen app: the
// desktop hidden, every key and click here), the wallpaper darkened, the time large, the date.
// A click or a key unlocks it -- or, when SD:/etc/lock.ini sets a PIN ("pin = 1234"), typing the
// PIN then Enter (Backspace erases; a wrong one shakes the dots away).
//
#include "kapi.h"
#include "applib.h"
#include "wtk/wtk.h"

using namespace wtk;

static unsigned *g_fb, *g_wall;
static int g_w, g_h, g_ww, g_wh;
static char g_pin[16], g_typed[16];
static int g_ntyped;
static bool g_wrong, g_done;

static const char *const DAYS[7] = { "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday" };
static const char *const MONTHS[12] = { "January", "February", "March", "April", "May", "June", "July",
	"August", "September", "October", "November", "December" };

static int day_of_week (int y, int m, int d)			// 0 = Monday (Zeller)
{
	if (m < 3) { m += 12; y--; }
	int k = y % 100, j = y / 100;
	return ((d + 13 * (m + 1) / 5 + k + k / 4 + j / 4 + 5 * j) % 7 + 5) % 7;
}

static int put (char *o, int p, const char *s) { while (*s) o[p++] = *s++; o[p] = '\0'; return p; }
static int put2 (char *o, int p, int v) { o[p++] = (char) ('0' + v / 10 % 10); o[p++] = (char) ('0' + v % 10); o[p] = '\0'; return p; }

static void text_centred (Canvas &cv, int y, const char *s, unsigned c, int scale, int style)
{
	Font &f = font ();
	int w = wk_len (s) * f.width () * scale;
	cv.drawFont ((g_w - w) / 2 + scale, y + scale, s, f, 0x00000000, scale, style);	// a soft shadow
	cv.drawFont ((g_w - w) / 2, y, s, f, c, scale, style);
}

static void draw (void)
{
	Canvas cv; cv.adopt (g_fb, g_w, g_h);
	for (int y = 0; y < g_h; y++)					// the wallpaper, darkened
	{
		unsigned *d = g_fb + (long) y * g_w;
		const unsigned *s = g_wall && y < g_wh ? g_wall + (long) y * g_ww : 0;
		for (int x = 0; x < g_w; x++)
		{
			unsigned c = s && x < g_ww ? s[x] : 0x00203040;
			d[x] = (((c >> 16) & 255) * 100 / 256) << 16 | (((c >> 8) & 255) * 100 / 256) << 8 | ((c & 255) * 110 / 256);
		}
	}
	int yy = 0, mo = 1, dd = 1, hh = 0, mi = 0;
	kapi_get_datetime (&yy, &mo, &dd, &hh, &mi, 0);
	char t[64]; int p = put2 (t, 0, hh); t[p++] = ':'; put2 (t, p, mi);
	int fh = font ().height ();
	int cy = g_h / 2 - 5 * fh;
	text_centred (cv, cy, t, 0x00FFFFFF, 6, 2);
	p = put (t, 0, DAYS[day_of_week (yy, mo, dd)]); t[p++] = ' ';
	if (dd >= 10) t[p++] = (char) ('0' + dd / 10);
	t[p++] = (char) ('0' + dd % 10); t[p++] = ' ';
	p = put (t, p, MONTHS[(mo - 1) % 12]); t[p++] = ' ';
	for (int d = 1000; d; d /= 10) t[p++] = (char) ('0' + yy / d % 10);
	t[p] = '\0';
	text_centred (cv, cy + 6 * fh + 8, t, 0x00E8EEF4, 2, 0);
	int by = cy + 9 * fh + 20;
	if (g_pin[0])
	{
		int n = wk_len (g_pin), dw = 22, x0 = (g_w - n * dw) / 2;	// a dot a digit
		wk_paint_alpha (false);
		for (int i = 0; i < n; i++)
		{
			unsigned c = g_wrong ? 0x00E06058 : 0x00FFFFFF;
			if (i < g_ntyped) wk_rbox (cv, x0 + i * dw + 4, by + 4, 12, 12, 6, c, c);
			else wk_rline (cv, x0 + i * dw + 4, by + 4, 12, 12, 6, c, 220);
		}
		text_centred (cv, by + 34, g_wrong ? "Wrong PIN -- try again" : "Type the PIN, then Enter", 0x00C8D2DC, 1, 0);
	}
	else text_centred (cv, by, "Click or press a key to unlock", 0x00C8D2DC, 1, 0);
	kapi_present_fb ();
}

static void unlock (void) { g_done = true; }

static void on_key (unsigned long, int ev, long k)
{
	if (ev != GUI_EVENT_KEY) return;
	if (!g_pin[0]) { unlock (); return; }
	if (k == KEY_BACKSPACE) { if (g_ntyped > 0) g_ntyped--; g_wrong = false; }
	else if (k == 27) { g_ntyped = 0; g_wrong = false; }
	else if (k == KEY_ENTER)
	{
		g_typed[g_ntyped] = '\0';
		bool ok = g_ntyped == wk_len (g_pin);
		for (int i = 0; ok && i < g_ntyped; i++) if (g_typed[i] != g_pin[i]) ok = false;
		if (ok) { unlock (); return; }
		g_wrong = true; g_ntyped = 0;
	}
	else if (k >= 32 && k < 127 && g_ntyped < 15) { g_typed[g_ntyped++] = (char) k; g_wrong = false; }
	draw ();
}

static void on_pointer (unsigned long, int ev, long)
{
	if (ev == GUI_EVENT_PTR_DOWN && !g_pin[0]) unlock ();
}

int main (void)
{
	wtk::init ();
	if (app_ini_load_path ("SD:/etc/lock.ini") >= 0)
	{
		const char *p = app_ini_get (0, "pin", 0);
		if (p) { int i = 0; for (; p[i] && i < 15; i++) g_pin[i] = p[i]; g_pin[i] = '\0'; }
	}
	g_wall = kapi_wallpaper_buffer (&g_ww, &g_wh);
	if (g_wall && g_ww > 0 && g_wh > 0 && !g_wall[0] && !g_wall[(long) (g_wh / 2) * g_ww + g_ww / 2]
	    && !g_wall[(long) g_wh * g_ww - 1]) g_wall = 0;	// (none drawn: a plain colour)
	g_fb = kapi_fullscreen_begin (&g_w, &g_h);
	if (g_fb == 0) return 1;
	kapi_set_key_handler (on_key);
	kapi_set_pointer_handler (on_pointer);
	draw ();
	int lastMin = -1;
	while (!g_done && !should_exit ())
	{
		pump_events ();
		int mi = 0; kapi_get_datetime (0, 0, 0, 0, &mi, 0);
		if (mi != lastMin) { lastMin = mi; draw (); }
		msleep (100);
	}
	kapi_fullscreen_end ();
	return 0;
}
