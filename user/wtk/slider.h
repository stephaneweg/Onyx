//
// wtk/slider.h -- horizontal value in [vmin,vmax]; drag the thumb.
//
#ifndef _wtk_slider_h
#define _wtk_slider_h

#include "wtk/widget.h"

namespace wtk {

class Slider : public Widget
{
public:
	int value, vmin, vmax; Action cb; unsigned bg;
	Slider (int l, int t, int w, int h, int lo, int hi, int val, Action cb_, unsigned bg_ = C_BG);
	void setFromX (int px);
	void onDraw () override;
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override;
};

} // namespace wtk

#endif
