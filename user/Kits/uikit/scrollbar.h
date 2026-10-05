//
// uikit/scrollbar.h -- draggable thumb (vertical or horizontal), value in [0,vmax].
//
#ifndef _uikit_scrollbar_h
#define _uikit_scrollbar_h

#include "uikit/widget.h"

namespace uikit {

class Scrollbar : public Widget
{
public:
	bool vertical; int value, vmax; Action cb;
	Scrollbar (int l, int t, int w, int h, bool vert, int maxv, int val, Action cb_);
	void setFromXY (int px, int py);
	void scrollBy (int units);		// clamp value+=units, fire cb (wheel / keyboard)
	void onDraw () override;
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override;
};

} // namespace uikit

#endif
