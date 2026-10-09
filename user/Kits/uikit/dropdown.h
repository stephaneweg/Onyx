//
// uikit/dropdown.h -- a non-editable drop-down list (the sibling of Combobox, same look): a
// closed box showing the selected option + a drop button; clicking expands a list below it
// (the widget grows downward while open and grabs outside clicks to close) -- above it when it does
// not fit below in the window and fits better there. Open, the list is over everything in the window
// (the widget is the window's child meanwhile -- the dialog's when it is in one --: uikit/lift.h): no
// parent clips it, nothing is drawn over it, wherever the box stands. Keyboard (when focused): Up / Down
// pick the previous / next option, Enter or Space opens / closes the list, Esc closes it. cb
// fires when the selection changes.
// The option strings are NOT copied: they must outlive the widget.
//
#ifndef _uikit_dropdown_h
#define _uikit_dropdown_h

#include "uikit/widget.h"

namespace uikit {

class Dropdown : public Widget
{
public:
	const char *const *opts; int nopts, sel, rowH; bool open; Action cb;
	Dropdown (int l, int t, int w, int h, const char *const *options, int n, int initial, Action cb_);
	void setOptions (const char *const *options, int n, int initial);
	void onDraw () override;
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override;
	bool onKey (long k) override;
private:
	int  m_hot;				// the row under the pointer while open (-1 none)
	bool m_up;				// open upward (the box at the bottom, `top` moved up)
	int  m_top0;				// ... its `top` when closed
	int  listTop () const;			// the open list's first row
	int  boxY () const { return m_up ? height - rowH : 0; }	// the box's row
	int  rowAt (int mx, int my) const;	// the row at (mx, my) while open, or -1
	void setOpen (bool o);
};

// The open list of a drop-down (Dropdown, Combobox): a floating panel at y0, n rows of rowH
// (sel: the chosen one, hot: the one under the pointer, -1 none).
void uk_draw_option_list (Canvas &cv, int y0, int w, int rowH, const char *const *opts, int n,
			  int sel, int hot);

} // namespace uikit

#endif
