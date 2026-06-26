//
// wtk/scrollbar.h -- draggable thumb (vertical or horizontal), value in [0,vmax].
//
#ifndef _wtk_scrollbar_h
#define _wtk_scrollbar_h

#include "wtk/widget.h"

namespace wtk {

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

} // namespace wtk

#endif
