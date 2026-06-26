//
// wtk/panel.h -- a plain container with a solid background; holds child widgets.
//
#ifndef _wtk_panel_h
#define _wtk_panel_h

#include "wtk/widget.h"

namespace wtk {

class Panel : public Widget
{
public:
	unsigned bg;
	Panel (int l, int t, int w, int h, unsigned bg_ = 0x00303D45);
	void onDraw () override;
};

} // namespace wtk

#endif
