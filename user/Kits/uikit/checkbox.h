//
// uikit/checkbox.h -- a box + check mark + label; toggles on click.
//
#ifndef _uikit_checkbox_h
#define _uikit_checkbox_h

#include "uikit/widget.h"

namespace uikit {

class Checkbox : public Widget
{
public:
	char	 text[64]; bool checked; Action cb; unsigned bg;
	Checkbox (int l, int t, int w, int h, const char *s, bool chk, Action cb_, unsigned bg_ = C_BG);
	void onDraw () override;
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override;
	bool onKey (long k) override;		// Space / Enter toggle
};

} // namespace uikit

#endif
