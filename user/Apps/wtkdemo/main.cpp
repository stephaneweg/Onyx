//
// wtkdemo -- exercises the recursive widget toolkit (uikit.hpp): labels, buttons,
// a checkbox, a slider driving a progress bar, a textbox, and a NESTED panel with
// its own button. Proves: per-widget canvases, recursive damage (only the touched
// subtree repaints), recursive mouse routing with coordinate conversion (the nested
// button is hit through the panel), focus routing (the textbox edits), and free
// clipping (text stays inside each widget).
//
#include "uikit/uikit.h"

using namespace uikit;

static Label    *g_count;
static Progress *g_prog;
static int       g_n = 0;

// The nested container: a panel of the theme -- a lighter shade of the face, rounded, an outline.
class Card : public Panel
{
public:
	Card (int l, int t, int w, int h) : Panel (l, t, w, h, uk_tone (C_FACE, 150)) {}
	void onDraw () override
	{
		canvas.clear (parent ? parent->bgColor () : C_BG);
		uk_rbox (canvas, 0, 0, width, height, 7, bg, bg);
		uk_rline (canvas, 0, 0, width, height, 7, uk_tone (C_FACE, 76), 200);
	}
};

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

// Infra demos: modal MessageBox / FileDialog, Dropdown, ColorPicker.
static const char *const g_dropOpts[] = { "Red", "Green", "Blue", "Yellow" };
static void onMsg  (Widget &) { int r = uk_messagebox ("Confirm", "Modal dialog from uikit.\nProceed?", MB_YESNO); g_count->setText (r ? "yes" : "no"); }
static void onOpen (Widget &) { char path[256]; if (uk_file_open (path, sizeof path, "SD:/")) g_count->setText (path); }
static void onDrop (Widget &w) { g_count->setText (g_dropOpts[((Dropdown &) w).sel]); }
static void onColor(Widget &) { g_count->setText ("colour picked"); }

int main (void)
{
	Root root (460, 430, "uikit demo");

	root.addChild (new Label (12, 10, 436, 20, "Recursive widget toolkit -- all widgets"));

	g_count = new Label (12, 36, 240, 20, "count: 0", uk_mix (C_ACCENT, C_TEXT, 140));
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
	Panel *panel = new Card (12, 168, 436, 168);
	panel->addChild (new Label (10, 10, 200, 18, "nested panel", C_TEXT, panel->bg));
	panel->addChild (new Button (10, 36, 150, 28, "Nested +1", onInc));
	panel->addChild (new Textarea (10, 74, 416, 84, 4096));
	root.addChild (panel);

	// Infra row: modal dialogs (skinned title bar + buttons), a dropdown, a colour picker.
	root.addChild (new Button (12, 346, 90, 26, "MsgBox", onMsg));
	root.addChild (new Button (108, 346, 90, 26, "Open...", onOpen));
	root.addChild (new Dropdown (210, 348, 110, 22, g_dropOpts, 4, 0, onDrop));
	root.addChild (new ColorPicker (332, 346, 40, 24, 0x0060FF90, onColor));

	root.run ();
	return 0;
}
