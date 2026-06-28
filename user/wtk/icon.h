//
// wtk/icon.h -- a magenta-keyed BMP centred above an optional label; hover/press
// highlight + an optional "running" badge.
//
#ifndef _wtk_icon_h
#define _wtk_icon_h

#include "wtk/widget.h"

namespace wtk {

class Icon : public Widget
{
public:
	char	 text[64]; unsigned *pix; int iw, ih; bool badged; Action cb; unsigned bg;
	Icon (int l, int t, int w, int h, const char *bmp, const char *label, Action cb_, unsigned bg_ = C_BG);
	~Icon () override;
	void setBadge (bool b);
	void setIcon (const char *bmp);		// (re)load the image (0/"" clears); repaints
	void onDraw () override;
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override;
};

} // namespace wtk

#endif
