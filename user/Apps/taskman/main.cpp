//
// taskman -- task manager. Lists every task (state + app/kernel), refreshing
// periodically. Up/Down select; k or Del kills the selected app (kernel tasks are
// protected); Enter raises an app's window; r refreshes now. An app's row shows its system
// calls per second (kapi v74 proc_stats; the pid found by name in kapi_list_procs).
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
static wtk::Canvas g_cv;		// the window's canvas (the painter draws on it)
static int g_fw = 8, g_fh = 16, g_rows = 1;

static char g_name[MAXT][NAMEL];
static char g_state[MAXT];		// R/S/B/N
static char g_kernel[MAXT];		// 1 = kernel task (not killable)
static int  g_rate[MAXT];		// system calls per second, -1 unknown
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

	// The system calls per second: each app row's pid from kapi_list_procs ("<pid> <a|k> <state>
	// <pages> <name>" a line), by its name.
	static char procs[4096];
	kapi_list_procs (procs, sizeof (procs));
	for (int t = 0; t < g_count; t++)
	{
		g_rate[t] = -1;
		if (g_kernel[t]) continue;
		for (int j = 0; procs[j]; )
		{
			int ls = j; while (procs[j] && procs[j] != '\n') j++;
			int le = j; if (procs[j] == '\n') j++;
			int k = ls, pid = 0;
			while (k < le && procs[k] >= '0' && procs[k] <= '9') pid = pid * 10 + (procs[k++] - '0');
			for (int f = 0; f < 4 && k < le; f++)		// past pid, kind, state, pages
			{
				while (k < le && procs[k] == ' ') k++;
				if (f < 3) while (k < le && procs[k] != ' ') k++;
			}
			int n = 0; while (g_name[t][n] && k + n < le && procs[k + n] == g_name[t][n]) n++;
			struct kapi_syscall_stats ss;
			if (pid > 0 && g_name[t][n] == 0 && k + n == le && kapi_proc_stats (pid, &ss) == 0)
			{
				g_rate[t] = (int) ss.rate;
				break;
			}
		}
	}
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

// The theme's look (wtk/paint.h): a header strip of the face, the list in a sunken field,
// the selection in the accent. The rows stay at LISTY + i * g_fh (the clicks' mapping).
static void redraw (void)
{
	using namespace wtk;
	g_cv.clear (C_BG);
	wk_rbox (g_cv, 0, 0, W, LISTY - 4, 0, wk_tone (C_FACE, 170), wk_tone (C_FACE, 130));
	wk_etch_h (g_cv, 0, LISTY - 4, W, C_FACE);
	char hdr[40]; int p = ax_itoa (g_count, hdr);
	const char *t = " tasks  k:kill ent:raise"; for (int i = 0; t[i]; i++) hdr[p++] = t[i];
	hdr[p] = '\0';
	wk_text_l (g_cv, 8, 0, LISTY - 4, hdr, C_TEXT);

	wk_sunken (g_cv, 3, LISTY - 2, W - 6, H - LISTY - 1, 4, C_FIELD);
	unsigned dim = wk_mix (C_FIELD, C_FIELD_TEXT, 130), ink = wk_hilite_ink (true);
	for (int i = 0; i < g_count && i < g_rows; i++)
	{
		int y = LISTY + i * g_fh;
		bool sel = i == g_sel;
		if (sel) wk_hilite (g_cv, 6, y, W - 12, g_fh, 4, true);
		char st[2] = { g_state[i], 0 };
		g_cv.text (12, y, st, sel ? ink : wk_tone (C_ACCENT, 84));	// state char
		g_cv.text (30, y, g_name[i], sel ? ink : g_kernel[i] ? dim : C_FIELD_TEXT);
		if (g_kernel[i]) g_cv.text (W - 62, y, "kernel", sel ? ink : dim);
		else if (g_rate[i] >= 0)
		{
			char r[16]; int n = ax_itoa (g_rate[i], r); r[n++] = '/'; r[n++] = 's'; r[n] = '\0';
			g_cv.text (W - 14 - n * g_fw, y, r, sel ? ink : dim);	// system calls per second
		}
	}
}

int main (void)
{
	fb = kapi_create_window (W, H, "taskman");
	if (fb == 0) return 1;
	wtk::wk_decorate_window ();			// (reads the theme: the palette)
	g_cv.adopt (fb, W, H);
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
		present ();				// (the frame drawn into the canvas: shown -- the compositor redraws only what it is told)
		msleep (16);
	}
	return 0;
}
