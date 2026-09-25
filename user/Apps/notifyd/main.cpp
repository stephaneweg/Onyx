//
// notifyd -- the notification service ("notify" IPC service, see notify.h).
//
// Apps call notify (title, text); the message lands in our mailbox and is shown as a
// bubble in the top-right corner, just below the menu bar: it fades in, stays ~4 s and
// fades out (whole-window opacity, kapi_set_window_alpha). Several notifications queue
// and are shown one after the other; a click dismisses the current one. The kernel can
// post too (from pid 0), e.g. "Network up".
//
// The window is BORDERLESS | TOPMOST (above the apps, never the active app). When idle
// it is fully transparent and parked off-screen so it never catches a click.
//
#include "kapi.h"
#include "notify.h"
#include "wtk/wtk.h"

using namespace wtk;

#define NW		330
#define NH		78
#define MARGIN		8
#define TOP		34			// below the menu bar (28 px)
#define QMAX		16
#define FADE_IN_MS	180
#define HOLD_MS		4000
#define FADE_OUT_MS	700
#define MAX_ALPHA	240

struct Note { char title[64]; char text[300]; };
static Note g_q[QMAX];
static int  g_head = 0, g_count = 0;

static int g_sw = 1024, g_fw = 8, g_fh = 16;
static Canvas g_cv;
static enum { IDLE, FADE_IN, HOLD, FADE_OUT } g_state = IDLE;
static unsigned g_t0 = 0;			// state start (ticks, HZ = 100)
static bool g_click = false;

static unsigned now_ms (void) { return kapi_get_ticks () * 10; }

static void draw (const Note &n)
{
	g_cv.clear (0x00262F3B);
	g_cv.frameRect (0, 0, NW, NH, 0x00161C24);
	g_cv.fillRect (0, 0, 4, NH, C_ACCENT);			// accent strip
	g_cv.text (14, 8, n.title, C_TEXT);
	g_cv.text (15, 8, n.title, C_TEXT);			// bold
	// Word-wrap the text over up to 3 lines.
	int maxc = (NW - 28) / g_fw, y = 12 + g_fh, lines = 0;
	const char *p = n.text;
	while (*p && lines < 3)
	{
		int len = 0, cut = -1;
		while (p[len] && p[len] != '\n' && len < maxc) { if (p[len] == ' ') cut = len; len++; }
		if (p[len] && p[len] != '\n' && cut > 0) len = cut;	// break at the last space
		char line[80]; int k = 0;
		for (int i = 0; i < len && k < 79; i++) line[k++] = p[i];
		if (lines == 2 && p[len] && k > 2) { line[k - 2] = '.'; line[k - 1] = '.'; }
		line[k] = '\0';
		g_cv.text (14, y, line, 0x00C8D0DA);
		y += g_fh + 2; lines++;
		p += len;
		while (*p == ' ' || *p == '\n') p++;
	}
	kapi_present ();
}

static void show_next (void)
{
	if (g_count == 0)
	{
		g_state = IDLE;
		kapi_set_window_alpha (0);
		kapi_move_window (-NW - 50, TOP);		// parked: never catches a click
		return;
	}
	draw (g_q[g_head]);
	kapi_move_window (g_sw - NW - MARGIN, TOP);
	kapi_set_window_alpha (0);
	g_state = FADE_IN; g_t0 = now_ms ();
}

static void ptr (unsigned long, int ev, long v)
{
	if (ev == GUI_EVENT_PTR_DOWN && (GUI_PTR_CHANGED (v) & 1)) g_click = true;
}

int main (void)
{
	if (!kapi_ipc_register (NOTIFY_SERVICE)) return 0;	// another notifyd is running
	int sh = 768;
	kapi_screen_size (&g_sw, &sh);
	g_fw = kapi_font_width ();  if (g_fw < 1) g_fw = 8;
	g_fh = kapi_font_height (); if (g_fh < 1) g_fh = 16;

	unsigned *fb = kapi_create_window_ex (-NW - 50, TOP, NW, NH, "notifyd",
					      WIN_FLAG_BORDERLESS | WIN_FLAG_TOPMOST);
	if (fb == 0) return 1;
	g_cv.adopt (fb, NW, NH);
	kapi_set_window_alpha (0);
	kapi_set_pointer_handler (ptr);

	static char buf[520];
	for (;;)
	{
		pump_events ();
		int from = 0, type = 0, n;
		while ((n = kapi_mailbox_recv (&from, &type, buf, sizeof buf - 1, 0)) >= 0)
		{
			if (type != NOTIFY_MSG_SHOW || g_count >= QMAX) continue;
			buf[n] = '\0';
			Note &q = g_q[(g_head + g_count) % QMAX];
			int i = 0, k = 0;
			for (; i < n && buf[i] && k < (int) sizeof q.title - 1; i++) q.title[k++] = buf[i];
			q.title[k] = '\0';
			while (i < n && buf[i]) i++;			// (title overflow)
			i++;
			k = 0;
			for (; i < n && buf[i] && k < (int) sizeof q.text - 1; i++) q.text[k++] = buf[i];
			q.text[k] = '\0';
			g_count++;
			if (g_state == IDLE) show_next ();
		}

		unsigned t = now_ms () - g_t0;
		switch (g_state)
		{
		case IDLE: break;
		case FADE_IN:
			kapi_set_window_alpha (t >= FADE_IN_MS ? MAX_ALPHA : (int) (t * MAX_ALPHA / FADE_IN_MS));
			if (t >= FADE_IN_MS) { g_state = HOLD; g_t0 = now_ms (); }
			break;
		case HOLD:
			if (t >= HOLD_MS || g_click) { g_state = FADE_OUT; g_t0 = now_ms (); }
			break;
		case FADE_OUT:
			kapi_set_window_alpha (t >= FADE_OUT_MS ? 0 : MAX_ALPHA - (int) (t * MAX_ALPHA / FADE_OUT_MS));
			if (t >= FADE_OUT_MS)
			{
				g_head = (g_head + 1) % QMAX; g_count--;
				show_next ();
			}
			break;
		}
		g_click = false;
		msleep (g_state == IDLE ? 50 : 16);
	}
}
