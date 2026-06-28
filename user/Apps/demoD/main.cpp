//
// demoD/main.cpp -- widget gallery (wtk port). Shows the wtk widgets: label, textbox,
// checkbox, button, slider, progress. The slider drives the progress bar.
//
#include "wtk/wtk.h"

using namespace wtk;

#define W 300
#define H 220
#define BG 0x00283848

static Label    *g_label;
static Textbox  *g_text;
static Checkbox *g_check;
static Slider   *g_slider;
static Progress *g_progress;

static unsigned scopy (char *d, const char *s, unsigned n)
{ unsigned i = 0; for (; i + 1 < n && s[i]; i++) d[i] = s[i]; d[i] = '\0'; return i; }

static void on_text (Widget &)			// fires on Enter in the textbox
{
	char msg[80];
	unsigned n = scopy (msg, "text: ", sizeof msg);
	scopy (msg + n, g_text->text, sizeof msg - n);
	g_label->setText (msg);
}
static void on_check (Widget &)
{ g_label->setText (g_check->checked ? "feature: ON" : "feature: OFF"); }
static void on_button (Widget &)
{ g_label->setText ("button clicked!"); }
static void on_slider (Widget &)		// slider drives the bar + a "slider: N%" label
{
	g_progress->setValue (g_slider->value);
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

int main (void)
{
	Root root (W, H, "widget gallery");
	root.bg = BG;

	g_label    = new Label    (10, 10, 280, 16, "type; toggle; slide; click OK", C_TEXT, BG);
	g_text     = new Textbox  (10, 38, 220, 22, "", on_text);	// fires on Enter
	g_check    = new Checkbox (10, 74, 220, 18, "enable feature", false, on_check, BG);
	g_slider   = new Slider   (10, 150, 220, 18, 0, 100, 50, on_slider, BG);
	g_progress = new Progress (10, 178, 220, 16, 0, 100, 50);
	root.addChild (g_label);
	root.addChild (g_text);
	root.addChild (g_check);
	root.addChild (new Button (10, 108, 90, 30, "OK", on_button));
	root.addChild (g_slider);
	root.addChild (g_progress);

	root.run ();
	return 0;
}
