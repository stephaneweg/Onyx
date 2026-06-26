//
// wtk/button.h -- flat themed button; fires cb on release-over.
//
#ifndef _wtk_button_h
#define _wtk_button_h

#include "wtk/widget.h"

namespace wtk {

class Button : public Widget
{
public:
	char	 text[64];
	Action	 cb;
	Button (int l, int t, int w, int h, const char *s, Action cb_ = 0);
	void onDraw () override;
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override;
};

} // namespace wtk

#endif
