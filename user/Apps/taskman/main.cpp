//
// taskman -- task manager. Lists every task (state + app/kernel), refreshing
// periodically. Up/Down select; k or Del kills the selected app (kernel tasks are
// protected); Enter raises an app's window; r refreshes now.
//
#include "kapi.h"
#include "wtk/wtk.h"
#include "applib.h"

#define W	340
#define H	300
#define LISTY	30
#define MAXT	40
#define NAMEL	32

static unsigned *fb;
static int g_fw = 8, g_fh = 16, g_rows = 1;

static char g_name[MAXT][NAMEL];
static char g_state[MAXT];		// R/S/B/N
static char g_kernel[MAXT];		// 1 = kernel task (not killable)
static int  g_count = 0, g_sel = 0;
static int  g_frames = 0;

static void refresh (void)
{
	static char buf[2048];
	kapi_list_tasks (buf, sizeof (buf));
	g_count = 0;
	int i = 0;
	while (buf[i] && g_count < MAXT)
	{
		int ls = i; while (buf[i] && buf[i] != '\n') i++;
		int le = i; if (buf[i] == '\n') i++;
		if (le - ls >= 3)
		{
			g_state[g_count] = buf[ls];
			g_kernel[g_count] = (buf[ls + 1] == 'k');
			int p = 0;
			for (int k = ls + 3; k < le && p < NAMEL - 1; k++) g_name[g_count][k - ls - 3] = buf[k], p++;
			g_name[g_count][p] = '\0';
			g_count++;
		}
	}
	if (g_sel >= g_count) g_sel = g_count - 1;
	if (g_sel < 0) g_sel = 0;
}

static void on_key (unsigned long s, int ev, long key)
{
	(void) s;
	if (ev != GUI_EVENT_KEY) return;
	switch (key)
	{
	case KEY_UP:   if (g_sel > 0) g_sel--; break;
	case KEY_DOWN: if (g_sel < g_count - 1) g_sel++; break;
	case KEY_ENTER:
		if (g_sel < g_count && !g_kernel[g_sel]) kapi_raise_app (g_name[g_sel]);
		break;
	case 'k': case 'K': case KEY_DEL:
		if (g_sel < g_count && !g_kernel[g_sel]) { kapi_kill (g_name[g_sel]); refresh (); }
		break;
	case 'r': case 'R': refresh (); break;
	}
}

static void on_click (unsigned long s, int ev, long val)
{
	(void) s;
	if (ev != GUI_EVENT_CANVAS_CLICK) return;
	int row = ((int) (val & 0xFFFF) - LISTY) / g_fh;
	if (row >= 0 && row < g_count) g_sel = row;
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
	fill_rect (0, 0, W, LISTY - 2, 0x00303d4d);
	char hdr[40]; int p = ax_itoa (g_count, hdr);
	const char *t = " tasks  k:kill ent:raise"; for (int i = 0; t[i]; i++) hdr[p++] = t[i];
	hdr[p] = '\0';
	wtk::draw_text (fb, W, H, 8, 9, hdr, 0x00e0e0e0);

	for (int i = 0; i < g_count && i < g_rows; i++)
	{
		int y = LISTY + i * g_fh;
		if (i == g_sel) fill_rect (0, y, W, g_fh, 0x00355070);
		char st[2] = { g_state[i], 0 };
		wtk::draw_text (fb, W, H, 8, y + 1, st, 0x00ffd070);		// state char
		wtk::draw_text (fb, W, H, 26, y + 1, g_name[i], g_kernel[i] ? 0x00808890 : 0x00e8e8e8);
		if (g_kernel[i]) wtk::draw_text (fb, W, H, W - 60, y + 1, "kernel", 0x00606870);
	}
}

int main (void)
{
	fb = kapi_create_window (W, H, "taskman");
	if (fb == 0) return 1;
	wtk::wk_decorate_window ();
	g_fw = kapi_font_width ();  if (g_fw < 1) g_fw = 8;
	g_fh = kapi_font_height (); if (g_fh < 1) g_fh = 16;
	g_rows = (H - LISTY) / g_fh; if (g_rows < 1) g_rows = 1;

	kapi_set_key_handler (on_key);
	kapi_set_click_handler (on_click);
	refresh ();

	while (!should_exit ())
	{
		pump_events ();
		if (++g_frames >= 25) { refresh (); g_frames = 0; }	// ~every 400 ms
		redraw ();
		msleep (16);
	}
	return 0;
}
