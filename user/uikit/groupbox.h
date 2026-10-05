//
// uikit/groupbox.h -- GroupBox: a titled frame around a group of controls (WPF GroupBox).
// Add the controls as its children; their (0,0) is the box's top-left, the frame line
// runs at y = font height / 2 and the content starts below the title (contentTop()).
//
#ifndef _uikit_groupbox_h
#define _uikit_groupbox_h

#include "uikit/widget.h"

namespace uikit {

class GroupBox : public Widget
{
public:
	char title[48]; unsigned bg, frame;	// frame: 0 = etched (the default), else a line's colour
	GroupBox (int l, int t, int w, int h, const char *title_, unsigned bg_ = C_BG);
	int  contentTop () const { return uk_fh () + 4; }
	void onDraw () override;
	unsigned bgColor () override { return bg; }
};

} // namespace uikit

#endif
