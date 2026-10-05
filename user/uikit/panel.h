//
// uikit/panel.h -- a plain container with a solid background; holds child widgets.
//
#ifndef _uikit_panel_h
#define _uikit_panel_h

#include "uikit/widget.h"

namespace uikit {

class Panel : public Widget
{
public:
	unsigned bg;
	Panel (int l, int t, int w, int h, unsigned bg_ = C_BG);
	unsigned bgColor () override { return bg; }
	void onDraw () override;
};

} // namespace uikit

#endif
