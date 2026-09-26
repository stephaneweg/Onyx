//
// wtk/groupbox.h -- GroupBox: a titled frame around a group of controls (WPF GroupBox).
// Add the controls as its children; their (0,0) is the box's top-left, the frame line
// runs at y = font height / 2 and the content starts below the title (contentTop()).
//
#ifndef _wtk_groupbox_h
#define _wtk_groupbox_h

#include "wtk/widget.h"

namespace wtk {

class GroupBox : public Widget
{
public:
	char title[48]; unsigned bg, frame;
	GroupBox (int l, int t, int w, int h, const char *title_, unsigned bg_ = C_BG);
	int  contentTop () const { return wk_fh () + 4; }
	void onDraw () override;
};

} // namespace wtk

#endif
