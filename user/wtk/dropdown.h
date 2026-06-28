//
// wtk/dropdown.h -- a closed box showing the selected option; clicking expands a list
// below it (the widget grows downward + comes to front while open, and grabs outside
// clicks to close). cb fires when the selection changes.
//
#ifndef _wtk_dropdown_h
#define _wtk_dropdown_h

#include "wtk/widget.h"

namespace wtk {

class Dropdown : public Widget
{
public:
	const char *const *opts; int nopts, sel, rowH; bool open; Action cb;
	Dropdown (int l, int t, int w, int h, const char *const *options, int n, int initial, Action cb_);
	void setOptions (const char *const *options, int n, int initial);
	void onDraw () override;
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override;
private:
	void setOpen (bool o);
};

} // namespace wtk

#endif
