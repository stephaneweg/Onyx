//
// inidemo.c -- demonstrates the shared .ini config reader (applib.h). It loads
// config.ini from its OWN app folder (SD:apps/inidemo.app/config.ini via
// kapi_app_dir) and shows the values: strings via app_ini_get, and an int via
// app_ini_get_int (drawn as a bar that long). Edit config.ini + reboot to see it
// change -- no rebuild needed.
//
#include "kapi.h"
#include "applib.h"

#define W	380
#define H	250

static unsigned *fb;

static void fill_rect (int x, int y, int w, int h, unsigned c)
{
	for (int yy = y; yy < y + h && yy < H; yy++)
		for (int xx = x; xx < x + w && xx < W; xx++)
			if (xx >= 0 && yy >= 0) fb[yy * W + xx] = c;
}

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
	kapi_draw_text (x, y, line, c);
}

int main (void)
{
	fb = kapi_create_window (W, H, "inidemo");
	if (fb == 0) return 1;
	for (int i = 0; i < W * H; i++) fb[i] = 0x00202832;

	int n = app_ini_load ("config.ini");
	int fh = kapi_font_height ();
	int y = 12, x = 12;

	if (n < 0)
	{
		kapi_draw_text (x, y, "config.ini not found in app folder", 0x00ff6060);
	}
	else
	{
		kapi_draw_text (x, y, "config.ini values:", 0x0080d0ff); y += fh + 6;

		draw_kv (x, y, "greeting = ", app_ini_get (0, "greeting", "(none)"), 0x00e0e0e0); y += fh + 3;
		draw_kv (x, y, "[app] name = ", app_ini_get ("app", "name", "(none)"), 0x00e0e0e0); y += fh + 3;
		draw_kv (x, y, "[app] version = ", app_ini_get ("app", "version", "(none)"), 0x00e0e0e0); y += fh + 3;
		draw_kv (x, y, "[app] author = ", app_ini_get ("app", "author", "(none)"), 0x00e0e0e0); y += fh + 8;

		int bw = app_ini_get_int ("display", "barwidth", 100);
		char num[16]; itoa (bw, num);
		draw_kv (x, y, "[display] barwidth (int) = ", num, 0x00ffd070); y += fh + 4;
		fill_rect (x, y, bw, 18, 0x0040a0ff);		// visual proof of the int parse
	}

	while (!should_exit ())
	{
		pump_events ();
		msleep (50);
	}
	return 0;
}
