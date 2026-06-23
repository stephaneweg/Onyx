//
// calendar -- month view + a simple agenda. Left/Right change month, Up/Down change
// year, today is highlighted. Click a day to select it; type a note and press Enter
// to save it (per-day, in SD:/apps/calendar.app/agenda.txt). Backspace edits.
//
#include "kapi.h"
#include "applib.h"

#define W	392
#define H	320
#define OX	8
#define OY	48
#define CW	54
#define CH	34

static unsigned *fb;
static int g_year, g_month;			// displayed month (month 1..12)
static int g_ty, g_tm, g_td;			// today
static int g_sel = 0;				// selected day (0 = none)
static char g_note[128]; static int g_notelen = 0;
static char g_agenda[4096];

static const char *MONTHS[12] = { "January", "February", "March", "April", "May",
	"June", "July", "August", "September", "October", "November", "December" };

static int dow (int y, int m, int d)		// 0=Sun..6=Sat (Zeller)
{
	if (m < 3) { m += 12; y--; }
	int k = y % 100, j = y / 100;
	int h = (d + 13 * (m + 1) / 5 + k + k / 4 + j / 4 + 5 * j) % 7;	// 0=Sat
	return (h + 6) % 7;
}
static int dim (int y, int m)
{
	static const int d[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
	if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
	return d[m - 1];
}

static void day_key (char *k, int day)		// "YYYYMMDD"
{
	k[0] = '0' + (g_year / 1000) % 10; k[1] = '0' + (g_year / 100) % 10;
	k[2] = '0' + (g_year / 10) % 10;   k[3] = '0' + g_year % 10;
	k[4] = '0' + g_month / 10; k[5] = '0' + g_month % 10;
	k[6] = '0' + day / 10;     k[7] = '0' + day % 10; k[8] = '\0';
}

static void load_agenda (void)
{
	g_agenda[0] = '\0';
	void *f = kapi_open ("SD:/apps/calendar.app/agenda.txt");
	if (f == 0) return;
	int n = kapi_read (f, g_agenda, sizeof (g_agenda) - 1);
	kapi_close (f);
	if (n < 0) n = 0;
	g_agenda[n] = '\0';
}

static void load_note (int day)			// selected-day note -> g_note
{
	g_note[0] = '\0'; g_notelen = 0;
	if (day == 0) return;
	char key[12]; day_key (key, day);
	int i = 0;
	while (g_agenda[i])
	{
		int ls = i; while (g_agenda[i] && g_agenda[i] != '\n') i++;
		int le = i; if (g_agenda[i] == '\n') i++;
		if (le - ls > 9 && g_agenda[ls + 8] == '|')
		{
			int match = 1;
			for (int k = 0; k < 8; k++) if (g_agenda[ls + k] != key[k]) { match = 0; break; }
			if (match)
			{
				int p = 0;
				for (int k = ls + 9; k < le && p < (int) sizeof g_note - 1; k++) g_note[p++] = g_agenda[k];
				g_note[p] = '\0'; g_notelen = p; return;
			}
		}
	}
}

static void save_note (void)
{
	if (g_sel == 0) return;
	char key[12]; day_key (key, g_sel);
	static char out[4096]; int p = 0;
	// Copy all lines except this day's.
	int i = 0;
	while (g_agenda[i])
	{
		int ls = i; while (g_agenda[i] && g_agenda[i] != '\n') i++;
		int le = i; if (g_agenda[i] == '\n') i++;
		int skip = 0;
		if (le - ls > 9 && g_agenda[ls + 8] == '|')
		{
			skip = 1; for (int k = 0; k < 8; k++) if (g_agenda[ls + k] != key[k]) { skip = 0; break; }
		}
		if (!skip && le > ls)
		{
			for (int k = ls; k < le && p < (int) sizeof out - 2; k++) out[p++] = g_agenda[k];
			out[p++] = '\n';
		}
	}
	if (g_notelen > 0)				// append the (new) note line
	{
		for (int k = 0; key[k] && p < (int) sizeof out - 2; k++) out[p++] = key[k];
		out[p++] = '|';
		for (int k = 0; k < g_notelen && p < (int) sizeof out - 2; k++) out[p++] = g_note[k];
		out[p++] = '\n';
	}
	out[p] = '\0';
	kapi_save_file ("SD:/apps/calendar.app/agenda.txt", out, (unsigned) p);
	// Reload so g_agenda stays in sync.
	for (int k = 0; k <= p; k++) g_agenda[k] = out[k];
}

static int has_note (int day)
{
	char key[12]; day_key (key, day);
	int i = 0;
	while (g_agenda[i])
	{
		int ls = i; while (g_agenda[i] && g_agenda[i] != '\n') i++;
		if (g_agenda[i] == '\n') i++;
		if (g_agenda[ls + 8] == '|')
		{
			int m = 1; for (int k = 0; k < 8; k++) if (g_agenda[ls + k] != key[k]) { m = 0; break; }
			if (m) return 1;
		}
	}
	return 0;
}

static void on_key (unsigned long s, int ev, long key)
{
	(void) s;
	if (ev != GUI_EVENT_KEY) return;
	switch (key)
	{
	case KEY_LEFT:  if (--g_month < 1) { g_month = 12; g_year--; } g_sel = 0; load_note (0); return;
	case KEY_RIGHT: if (++g_month > 12) { g_month = 1; g_year++; } g_sel = 0; load_note (0); return;
	case KEY_UP:    g_year++; g_sel = 0; load_note (0); return;
	case KEY_DOWN:  g_year--; g_sel = 0; load_note (0); return;
	case KEY_ENTER: save_note (); return;
	case KEY_BACKSPACE: if (g_notelen > 0) g_note[--g_notelen] = '\0'; return;
	}
	if (g_sel != 0 && key >= ' ' && key < 0x7f && g_notelen < (int) sizeof g_note - 1)
	{ g_note[g_notelen++] = (char) key; g_note[g_notelen] = '\0'; }
}

static void on_click (unsigned long s, int ev, long val)
{
	(void) s;
	if (ev != GUI_EVENT_CANVAS_CLICK) return;
	int x = (int) ((val >> 16) & 0xFFFF), y = (int) (val & 0xFFFF);
	int col = (x - OX) / CW, row = (y - OY) / CH;
	if (col < 0 || col >= 7 || row < 0 || row >= 6) return;
	int first = dow (g_year, g_month, 1);
	int day = row * 7 + col - first + 1;
	if (day >= 1 && day <= dim (g_year, g_month)) { g_sel = day; load_note (day); }
}

static void fill_rect (int x, int y, int w, int h, unsigned c)
{
	for (int yy = y; yy < y + h && yy < H; yy++)
		for (int xx = x; xx < x + w && xx < W; xx++)
			if (xx >= 0 && yy >= 0) fb[yy * W + xx] = c;
}

static void redraw (void)
{
	fill_rect (0, 0, W, H, 0x00202830);

	char hdr[40]; int p = 0;
	const char *mn = MONTHS[g_month - 1];
	for (int i = 0; mn[i]; i++) hdr[p++] = mn[i];
	hdr[p++] = ' '; p += ax_itoa (g_year, hdr + p);
	kapi_draw_text (OX, 10, hdr, 0x00ffffff);
	kapi_draw_text (W - 130, 10, "<- -> month  ^v year", 0x00708090);

	static const char *wd[7] = { "Su", "Mo", "Tu", "We", "Th", "Fr", "Sa" };
	for (int c = 0; c < 7; c++) kapi_draw_text (OX + c * CW + 8, 30, wd[c], 0x0090b0d0);

	int first = dow (g_year, g_month, 1), ndays = dim (g_year, g_month);
	for (int cell = 0; cell < 42; cell++)
	{
		int day = cell - first + 1;
		if (day < 1 || day > ndays) continue;
		int col = cell % 7, row = cell / 7;
		int x = OX + col * CW, y = OY + row * CH;
		unsigned bg = 0x00283440;
		if (day == g_sel) bg = 0x00355070;
		else if (day == g_td && g_month == g_tm && g_year == g_ty) bg = 0x00405028;
		fill_rect (x, y, CW - 2, CH - 2, bg);
		char ds[4]; ax_itoa (day, ds);
		kapi_draw_text (x + 4, y + 3, ds, 0x00e0e0e0);
		if (has_note (day)) fill_rect (x + CW - 9, y + 4, 4, 4, 0x0060d0ff);
	}

	// Note editor for the selected day.
	int ny = OY + 6 * CH + 6;
	if (g_sel != 0)
	{
		char lbl[40]; int q = 0;
		const char *t = "note for day "; for (int i = 0; t[i]; i++) lbl[q++] = t[i];
		q += ax_itoa (g_sel, lbl + q); lbl[q++] = ':'; lbl[q] = '\0';
		kapi_draw_text (OX, ny, lbl, 0x0090a0b0);
		fill_rect (OX, ny + 14, W - 16, 18, 0x00101418);
		kapi_draw_text (OX + 4, ny + 15, g_note, 0x00ffffff);
		int cx = OX + 4 + g_notelen * kapi_font_width ();
		fill_rect (cx, ny + 15, 2, kapi_font_height (), 0x0060ff90);
		kapi_draw_text (OX, ny + 36, "type + Enter to save", 0x00607080);
	}
	else kapi_draw_text (OX, ny, "click a day to add a note", 0x00708090);
}

int main (void)
{
	fb = kapi_create_window (W, H, "calendar");
	if (fb == 0) return 1;
	kapi_get_datetime (&g_ty, &g_tm, &g_td, 0, 0, 0);
	if (g_ty < 1970) { g_ty = 2026; g_tm = 1; g_td = 1; }	// uptime clock fallback
	g_year = g_ty; g_month = g_tm;
	load_agenda ();
	kapi_set_key_handler (on_key);
	kapi_set_click_handler (on_click);
	while (!should_exit ()) { pump_events (); redraw (); msleep (16); }
	return 0;
}
