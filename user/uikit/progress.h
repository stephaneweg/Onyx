//
// uikit/progress.h -- non-interactive bar, fills proportionally.
//
#ifndef _uikit_progress_h
#define _uikit_progress_h

#include "uikit/widget.h"

namespace uikit {

class Progress : public Widget
{
public:
	int value, vmin, vmax;
	Progress (int l, int t, int w, int h, int lo, int hi, int val);
	void setValue (int v);
	void onDraw () override;
};

} // namespace uikit

#endif
