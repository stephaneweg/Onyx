//
// uikit/form.h -- FormDialog: a dialog that says what its rows are (a label and its control, a section's title, a
// paragraph, buttons with a role) and lets each UIKit lay them out (docs/POCKETUI-TECH-STUDY.md section 6.7, phase P6;
// docs/03 "The adaptive widgets"):
//   the desktop, pocket in landscape  a box centred on the window: the labels in a column at the left, the controls
//                                     beside them, the buttons at the bottom right (the default first, then Cancel)
//   pocket in portrait                a sheet the window's width: each label over its control, the controls the
//                                     whole width, Cancel at the title's left and the default at its right
//   console                           the PS2's panel; Enter / the pad's cross the default, Esc / the circle Cancel
// (Named FormDialog: QBStudio has a Form of its own, beside `using namespace uikit`.)
//
//   FormDialog f (TR ("New Playlist"));
//   Textbox *name = new Textbox (0, 0, 240, 26, "");
//   f.addRow (TR ("Name:"), name);
//   f.addButton (TR ("Create"), UK_FB_DEFAULT, 1);
//   f.addButton (TR ("Cancel"), UK_FB_CANCEL, 0);
//   if (f.run () == 1) ... name->text ...
//
// A row's control keeps its height; a control 100 px wide or more takes the column's width (a field, a list), a
// narrower one keeps its own (a check box, a spin box). The controls are the dialog's children (freed with it).
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. Permission is hereby
// granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions: The above copyright notice and this permission notice shall
// be included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS
// IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
//
#ifndef _uikit_form_h
#define _uikit_form_h

#include "uikit/dialog.h"
#include "uikit/button.h"

namespace uikit {

enum { UK_FB_DEFAULT = 0, UK_FB_CANCEL = 1, UK_FB_DESTRUCTIVE = 2, UK_FB_OTHER = 3 };	// a button's role

class FormDialog : public Modal
{
public:
	FormDialog (const char *title, int width = 0);	// width: the desktop's box (0: what the rows need, 360 at least)
	~FormDialog () override;
	void addSection (const char *title);		// a section's title over the rows that follow (copied)
	Widget *addRow (const char *label, Widget *w);	// a label (copied; 0: the control alone, the row's width) -> w
	void addText (const char *text);		// a paragraph, wrapped (copied)
	Button *addButton (const char *label, int role, int result);	// UK_FB_*; run () gives `result` when it is pressed
	int  run ();					// laid out, shown -> the result of the button pressed (Enter: the default's,
							// Esc: the Cancel's -- 0 without one)
	void onButton (int tag) override;
	bool onKey (long k) override;
	void onDraw () override;
	void layout () override;
	unsigned long fm_reserved_[2] = { 0, 0 };	// (uikit/abi.h rule 2: room for later; the rows are behind ext)
	virtual void fm_reserved0 () {}			// (rule 3)
	virtual void fm_reserved1 () {}
};

} // namespace uikit

#endif
