//
// wtkdemo -- exercises the recursive widget toolkit (wtk.hpp): labels, buttons,
// a checkbox, a slider driving a progress bar, a textbox, and a NESTED panel with
// its own button. Proves: per-widget canvases, recursive damage (only the touched
// subtree repaints), recursive mouse routing with coordinate conversion (the nested
// button is hit through the panel), focus routing (the textbox edits), and free
// clipping (text stays inside each widget).
//
#include "wtk/wtk.h"

using namespace wtk;

static Label    *g_count;
static Progress *g_prog;
static int       g_n = 0;

static void itoa10 (char *b, const char *prefix, int v)
{
	int i = 0; for (; prefix[i]; i++) b[i] = prefix[i];
	if (v < 0) { b[i++] = '-'; v = -v; }
	int d = 1; while (v / d >= 10) d *= 10;
	while (d > 0) { b[i++] = (char) ('0' + (v / d) % 10); d /= 10; }
	b[i] = '\0';
}

static void onInc   (Widget &) { g_n++;     char b[32]; itoa10 (b, "count: ", g_n); g_count->setText (b); }
static void onReset (Widget &) { g_n = 0;   g_count->setText ("count: 0"); }
static void onSlide (Widget &w) { g_prog->setValue (((Slider &) w).value); }		// slider -> progress
static void onCheck (Widget &w) { g_count->setText (((Checkbox &) w).checked ? "checked" : "unchecked"); }

int main (void)
{
	Root root (460, 350, "wtk demo");

	root.addChild (new Label (12, 10, 436, 20, "Recursive widget toolkit -- all widgets"));

	g_count = new Label (12, 36, 240, 20, "count: 0", C_ACCENT);
	root.addChild (g_count);
	root.addChild (new Button (260, 34, 90, 26, "Increment", onInc));
	root.addChild (new Button (358, 34, 70, 26, "Reset", onReset));

	root.addChild (new Checkbox (12, 70, 200, 22, "a checkbox", false, onCheck));

	root.addChild (new Label (12, 100, 70, 22, "slider:"));
	root.addChild (new Slider (84, 102, 160, 18, 0, 100, 30, onSlide));
	g_prog = new Progress (260, 102, 188, 18, 0, 100, 30);
	root.addChild (g_prog);

	root.addChild (new Label (12, 132, 70, 22, "textbox:"));
	root.addChild (new Textbox (84, 130, 240, 24, "edit me"));

	// Nested container: its children's coords are relative to the panel, so mouse
	// routing must convert through it, and they blit up through the panel's canvas.
	Panel *panel = new Panel (12, 168, 436, 168, 0x002A3640);
	panel->addChild (new Label (10, 10, 200, 18, "nested panel", C_TEXT, 0x002A3640));
	panel->addChild (new Button (10, 36, 150, 28, "Nested +1", onInc));
	panel->addChild (new Textarea (10, 74, 416, 84, 4096));
	root.addChild (panel);

	root.run ();
	return 0;
}
