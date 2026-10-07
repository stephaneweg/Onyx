//
// gallery -- (the desktop simulator only) every uikit control in each of its states, for a look at
// the theme: push buttons (normal, pointed, pressed, disabled, focused), check boxes, radio
// buttons, switches, fields, lists, a drop-down and a combo box (open with SIM's clicks), a
// slider, progress bars, scroll bars, a text area with a selection. SIM_DIALOG=1 opens a message
// box over it.
//
#include "appkit/appkit.h"
#include "uikit/uikit.h"
#include <stdlib.h>

using namespace uikit;

static const char *const OPTS[] = { "Mandelbrot", "Julia", "Burning ship", "Newton" };

static Button *btn (Widget &p, int x, int y, int w, int h, const char *s, int st)
{
	Button *b = new Button (x, y, w, h, s);
	if (st == 1) b->hover = true;
	if (st == 2) b->pressed = b->hover = true;
	if (st == 3) b->disabled = true;
	p.addChild (b);
	if (st == 4) b->setFocus ();
	return b;
}

int main (void)
{
	Root root (640, 440, "Gallery");
	int y = 12;
	btn (root, 12, y, 96, 28, "Normal", 0);
	btn (root, 116, y, 96, 28, "Pointed", 1);
	btn (root, 220, y, 96, 28, "Pressed", 2);
	btn (root, 324, y, 96, 28, "Disabled", 3);
	btn (root, 428, y, 96, 28, "Focused", 4);
	btn (root, 532, y - 2, 96, 34, "Large", 0);
	y += 40;
	btn (root, 12, y, 64, 20, "Small", 0);
	btn (root, 84, y, 64, 20, "Small", 2);
	Checkbox *c1 = new Checkbox (170, y, 110, 20, "Checked", true, 0); root.addChild (c1);
	Checkbox *c2 = new Checkbox (280, y, 110, 20, "Unchecked", false, 0); root.addChild (c2);
	Checkbox *c3 = new Checkbox (400, y, 110, 20, "Disabled", true, 0); c3->disabled = true; root.addChild (c3);
	Checkbox *c4 = new Checkbox (510, y, 110, 20, "Pointed", false, 0); c4->hover = true; root.addChild (c4);
	y += 30;
	RadioButton *r1 = new RadioButton (12, y, 100, 20, "One", 1, true, 0); root.addChild (r1);
	RadioButton *r2 = new RadioButton (112, y, 100, 20, "Two", 1, false, 0); root.addChild (r2);
	ToggleSwitch *t1 = new ToggleSwitch (220, y - 2, 110, 24, "On", true, 0); root.addChild (t1);
	ToggleSwitch *t2 = new ToggleSwitch (330, y - 2, 110, 24, "Off", false, 0); root.addChild (t2);
	NumericUpDown *nu = new NumericUpDown (450, y - 3, 90, 26, 0, 99, 42, 1, 0); root.addChild (nu);
	y += 34;
	Textbox *tb = new Textbox (12, y, 200, 26, "Some text"); root.addChild (tb);
	Textbox *tf = new Textbox (220, y, 200, 26, "Focused field"); root.addChild (tf);
	Combobox *cb = new Combobox (430, y, 198, 26, "Editable"); root.addChild (cb);
	for (int i = 0; i < 4; i++) cb->addOption (OPTS[i]);
	y += 36;
	Dropdown *dd = new Dropdown (12, y, 180, 26, OPTS, 4, 0, 0); root.addChild (dd);
	Slider *sl = new Slider (210, y, 200, 26, 0, 100, 60, 0); root.addChild (sl);
	Progress *pg = new Progress (430, y + 4, 198, 18, 0, 100, 70); root.addChild (pg);
	y += 38;
	ListBox *lb = new ListBox (12, y, 180, 150); root.addChild (lb);
	const char *fr[] = { "Apple", "Banana", "Cherry", "Grape", "Kiwi", "Lemon", "Mango", "Orange", "Peach", "Pear" };
	for (int i = 0; i < 10; i++) lb->add (fr[i]);
	lb->setSel (2);
	Textarea *ta = new Textarea (202, y, 260, 150, 4096); root.addChild (ta);
	ta->setContent ("A text area: the field is light,\nthe selection a shade of the accent,\nthe caret in the accent too.\n\nOne\nTwo\nThree\nFour\nFive\nSix\nSeven\nEight");
	ta->anchor = 36; ta->caret = 70;
	Scrollbar *sb = new Scrollbar (472, y, 14, 150, true, 10, 3, 0); root.addChild (sb);
	GroupBox *gb = new GroupBox (496, y, 132, 150, "Group"); root.addChild (gb);
	Progress *pg2 = new Progress (506, y + 110, 112, 12, 0, 100, 30); root.addChild (pg2);
	Label *lab = new Label (506, y + 26, 110, 20, "A label"); root.addChild (lab);
	Scrollbar *hb = new Scrollbar (506, y + 60, 112, 14, false, 10, 7, 0); root.addChild (hb);
	tf->setFocus ();
	if (getenv ("SIM_DIALOG")) { root.draw (); uk_messagebox ("Save changes?", "The document has changed.\nSave it before closing?", MB_YESNOCANCEL); }
	root.run ();
	return 0;
}
