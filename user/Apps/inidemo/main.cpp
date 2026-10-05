//
// inidemo.c -- demonstrates the shared .ini config reader (applib.h). It loads
// config.ini from its OWN app folder (SD:apps/inidemo.app/config.ini via
// kapi_app_dir) and shows the values: strings via app_ini_get, and an int via
// app_ini_get_int (drawn as a bar that long). Edit config.ini + reboot to see it
// change -- no rebuild needed.
//
#include "kapi.h"
#include "uikit/uikit.h"
#include "applib.h"

#define W	380
#define H	250

static unsigned *fb;

static int itoa (int v, char *b)
{
	int neg = 0, p = 0, n = 0;
	char t[12];
	if (v < 0) { neg = 1; v = -v; }
	if (v == 0) t[n++] = '0';
	while (v) { t[n++] = (char) ('0' + v % 10); v /= 10; }
	if (neg) b[p++] = '-';
	while (n) b[p++] = t[--n];
	b[p] = '\0';
	return p;
}

static void draw_kv (int x, int y, const char *label, const char *val, unsigned c)
{
	char line[120];
	int p = 0;
	ax_strcat (line, sizeof (line), &p, label);
	ax_strcat (line, sizeof (line), &p, val);
	uikit::draw_text (fb, W, H, x, y, line, c);
}

int main (void)
{
	fb = kapi_create_window (W, H, "inidemo");
	if (fb == 0) return 1;
	uikit::uk_decorate_window ();			// (reads the theme: the palette)
	using namespace uikit;
	Canvas cv; cv.adopt (fb, W, H);
	cv.clear (C_BG);

	int n = app_ini_load ("config.ini");
	int fh = kapi_font_height ();
	int y = 12, x = 12;

	if (n < 0)
	{
		uikit::draw_text (fb, W, H, x, y, "config.ini not found in app folder", uk_mix (C_TEXT, 0x00E03C3C, 150), 1, 2);
	}
	else
	{
		uikit::draw_text (fb, W, H, x, y, "config.ini values:", C_TEXT, 1, 2); y += fh + 6;

		draw_kv (x, y, "greeting = ", app_ini_get (0, "greeting", "(none)"), C_TEXT); y += fh + 3;
		draw_kv (x, y, "[app] name = ", app_ini_get ("app", "name", "(none)"), C_TEXT); y += fh + 3;
		draw_kv (x, y, "[app] version = ", app_ini_get ("app", "version", "(none)"), C_TEXT); y += fh + 3;
		draw_kv (x, y, "[app] author = ", app_ini_get ("app", "author", "(none)"), C_TEXT); y += fh + 8;

		int bw = app_ini_get_int ("display", "barwidth", 100);
		char num[16]; itoa (bw, num);
		draw_kv (x, y, "[display] barwidth (int) = ", num, C_TEXT); y += fh + 4;
		uk_rbox (cv, x, y, bw, 18, 5, uk_tone (C_ACCENT, 168), uk_tone (C_ACCENT, 104));	// visual proof of the int parse
	}

	while (!should_exit ())
	{
		pump_events ();
		present ();				// (the frame drawn into the canvas: shown -- the compositor redraws only what it is told)
		msleep (50);
	}
	return 0;
}
