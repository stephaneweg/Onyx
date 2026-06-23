//
// demoD.c -- widget gallery: label + textbox + checkbox + button, all skinned and
// driven by the kernel widget toolkit. The kernel draws the widgets over our
// canvas; we just paint a background and react to events.
//
#include "kapi.h"

#define W 300
#define H 220

static unsigned     *fb;
static unsigned long g_Label;		// status label (handle)
static unsigned long g_Text;		// textbox (handle)
static unsigned long g_Progress;	// progress bar (handle)

static void clear (unsigned c)
{
	for (int i = 0; i < W * H; i++)
	{
		fb[i] = c;
	}
}

// Minimal string helpers (no libc in userland).
static unsigned scopy (char *d, const char *s, unsigned n)
{
	unsigned i = 0;
	for (; i + 1 < n && s[i]; i++) d[i] = s[i];
	d[i] = '\0';
	return i;
}

static void on_text (unsigned long sender, int event, long value)
{
	(void) sender; (void) event; (void) value;
	char buf[48], msg[64];
	kapi_widget_get_text (g_Text, buf, sizeof buf);
	unsigned n = scopy (msg, "text: ", sizeof msg);
	scopy (msg + n, buf, sizeof msg - n);
	kapi_widget_set_text (g_Label, msg);
}

static void on_check (unsigned long sender, int event, long value)
{
	(void) sender; (void) event;
	kapi_widget_set_text (g_Label, value ? "feature: ON" : "feature: OFF");
}

static void on_button (unsigned long sender, int event, long value)
{
	(void) sender; (void) event; (void) value;
	kapi_widget_set_text (g_Label, "button clicked!");
}

static void on_slider (unsigned long sender, int event, long value)
{
	(void) sender; (void) event;
	kapi_widget_set_value (g_Progress, (int) value);	// slider drives the bar
	char msg[24];
	unsigned n = scopy (msg, "slider: ", sizeof msg);
	// itoa for 0..100
	int v = (int) value, i = 0; char tmp[4];
	if (v == 0) tmp[i++] = '0';
	while (v > 0) { tmp[i++] = (char) ('0' + v % 10); v /= 10; }
	while (i > 0 && n + 1 < sizeof msg) msg[n++] = tmp[--i];
	if (n + 1 < sizeof msg) msg[n++] = '%';
	msg[n] = '\0';
	kapi_widget_set_text (g_Label, msg);
}

int main (void)
{
	fb = create_window (W, H, "widget gallery");
	if (fb == 0)
	{
		return 1;
	}
	clear (0x00283848);

	g_Label    = kapi_add_label    (10, 10, 280, 16, "type; toggle; slide; click OK");
	g_Text     = kapi_add_textbox  (10, 38, 220, 22, on_text);
	(void)       kapi_add_checkbox (10, 74, 220, 18, "enable feature", on_check);
	(void)       kapi_add_button   (10, 108, 90, 30, "OK", on_button);
	(void)       kapi_add_slider   (10, 150, 220, 18, on_slider);
	g_Progress = kapi_add_progress (10, 178, 220, 16);

	while (!should_exit ())
	{
		pump_events ();		// dispatch widget callbacks in our context
		msleep (16);		// yields -> compositor draws canvas + widgets
	}

	return 0;
}
