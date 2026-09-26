//
// wtk/radio.h -- RadioButton: a round button + label; the buttons of the same `group`
// under the same parent are exclusive (selecting one clears the others). cb fires on
// selection. GroupBox (groupbox.h) is the usual container for a set of them.
//
#ifndef _wtk_radio_h
#define _wtk_radio_h

#include "wtk/widget.h"

namespace wtk {

class RadioButton : public Widget
{
public:
	char	 text[64]; int group; bool checked; Action cb; unsigned bg;
	RadioButton (int l, int t, int w, int h, const char *s, int group_, bool chk, Action cb_, unsigned bg_ = C_BG);
	void select ();				// check me, uncheck my group's siblings, fire cb
	void onDraw () override;
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override;
	bool onKey (long k) override;		// Space / Enter select
	RadioButton *asRadio () override { return this; }
};

// The checked button of `group` among parent's children (0 = none).
RadioButton *wk_radio_checked (Widget *parent, int group);

} // namespace wtk

#endif
