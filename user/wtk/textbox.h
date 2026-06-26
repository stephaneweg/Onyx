//
// wtk/textbox.h -- single-line editable field; click to position caret, type to edit.
//
#ifndef _wtk_textbox_h
#define _wtk_textbox_h

#include "wtk/widget.h"

namespace wtk {

class Textbox : public Widget
{
public:
	char	 text[64]; int caret; bool password; Action cb;
	Textbox (int l, int t, int w, int h, const char *s = "", Action cb_ = 0);
	void setText (const char *s);
	void onDraw () override;
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override;
	bool onKey (long k) override;
};

} // namespace wtk

#endif
