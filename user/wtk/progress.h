//
// wtk/progress.h -- non-interactive bar, fills proportionally.
//
#ifndef _wtk_progress_h
#define _wtk_progress_h

#include "wtk/widget.h"

namespace wtk {

class Progress : public Widget
{
public:
	int value, vmin, vmax;
	Progress (int l, int t, int w, int h, int lo, int hi, int val);
	void setValue (int v);
	void onDraw () override;
};

} // namespace wtk

#endif
