//
// demoD/main.cpp -- widget gallery (C++ port). Shows every uikit.hpp widget: label,
// textbox, checkbox, button, slider, progress. The slider drives the progress bar.
// All user-side now -- no kernel widgets; drawn in our canvas, fed by the v22 pointer
// stream + key events.
//
#include "kapi.h"
#include "uikit.hpp"

#define W 300
#define H 220

static ui::Ui       *g_ui;
static ui::Label    *g_label;
static ui::Textbox  *g_text;
static ui::Checkbox *g_check;
static ui::Slider   *g_slider;
static ui::Progress *g_progress;

static unsigned scopy (char *d, const char *s, unsigned n)
{ unsigned i = 0; for (; i + 1 < n && s[i]; i++) d[i] = s[i]; d[i] = '\0'; return i; }

static void on_text (ui::Widget &)		// fires on Enter in the textbox
{
	char msg[80];
	unsigned n = scopy (msg, "text: ", sizeof msg);
	scopy (msg + n, g_text->getText (), sizeof msg - n);
	g_label->setText (msg);
}
static void on_check (ui::Widget &)
{ g_label->setText (g_check->checked ? "feature: ON" : "feature: OFF"); }
static void on_button (ui::Widget &)
{ g_label->setText ("button clicked!"); }
static void on_slider (ui::Widget &)		// slider drives the bar + a "slider: N%" label
{
	g_progress->value = g_slider->value;
	char msg[24];
	unsigned n = scopy (msg, "slider: ", sizeof msg);
	int v = g_slider->value, i = 0; char tmp[4];
	if (v == 0) tmp[i++] = '0';
	while (v > 0) { tmp[i++] = (char) ('0' + v % 10); v /= 10; }
	while (i > 0 && n + 1 < sizeof msg) msg[n++] = tmp[--i];
	if (n + 1 < sizeof msg) msg[n++] = '%';
	msg[n] = '\0';
	g_label->setText (msg);
}
static void on_evt (unsigned long s, int ev, long v) { g_ui->onEvent (s, ev, v); }

int main (void)
{
	unsigned *fb = kapi_create_window (W, H, "widget gallery");
	if (fb == 0) return 1;

	ui::Ui ui (fb, W, H); g_ui = &ui;
	ui.col_bg = 0x00283848;

	g_label    = &ui.label    (10, 10, 280, 16, "type; toggle; slide; click OK");
	g_text     = &ui.textbox  (10, 38, 220, 22, "");
	g_check    = &ui.checkbox (10, 74, 220, 18, "enable feature", false, on_check);
	             ui.button   (10, 108, 90, 30, "OK", on_button);
	g_slider   = &ui.slider   (10, 150, 220, 18, 0, 100, 50, on_slider);
	g_progress = &ui.progress (10, 178, 220, 16, 0, 100, 50);
	g_text->cb = on_text;				// textbox fires its cb on Enter

	kapi_set_pointer_handler (on_evt);
	kapi_set_key_handler (on_evt);

	while (!should_exit ())
	{
		pump_events ();
		if (ui.dirty ()) { ui.background (); ui.drawAll (); present (); }
		msleep (16);
	}
	return 0;
}
