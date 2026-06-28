//
// wtk/colorpick.h -- a colour swatch; clicking expands a fixed 8x5 palette grid below
// it (the widget grows + comes to front while open, grabs outside clicks to close).
// cb fires when a colour is picked; read it from `color`.
//
#ifndef _wtk_colorpick_h
#define _wtk_colorpick_h

#include "wtk/widget.h"

namespace wtk {

class ColorPicker : public Widget
{
public:
	unsigned color; bool open; Action cb;
	ColorPicker (int l, int t, int w, int h, unsigned initial, Action cb_);
	void onDraw () override;
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override;
private:
	int m_boxW, m_boxH;
	void setOpen (bool o);
};

} // namespace wtk

#endif
