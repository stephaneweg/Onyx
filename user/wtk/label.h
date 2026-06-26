//
// wtk/label.h -- non-interactive text on a solid background.
//
#ifndef _wtk_label_h
#define _wtk_label_h

#include "wtk/widget.h"

namespace wtk {

class Label : public Widget
{
public:
	char	 text[128];
	unsigned bg, fg;
	Label (int l, int t, int w, int h, const char *s, unsigned fg_ = C_TEXT, unsigned bg_ = C_BG);
	void setText (const char *s);
	void onDraw () override;
};

} // namespace wtk

#endif
