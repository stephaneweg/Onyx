//
// uikit/button.h -- a push button (the theme's raised face, as a drop-down's: uikit/paint.h); fires cb on
// release-over.
//
#ifndef _uikit_button_h
#define _uikit_button_h

#include "uikit/widget.h"

namespace uikit {

class Button : public Widget
{
public:
	char	 text[64];
	Action	 cb;
	Button (int l, int t, int w, int h, const char *s, Action cb_ = 0);
	void onDraw () override;
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override;
	bool onKey (long k) override;		// focused (Tab): Space / Enter press it
};

} // namespace uikit

#endif
