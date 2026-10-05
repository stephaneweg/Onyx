//
// uikit/numeric.h -- NumericUpDown: an integer field with up/down arrows (WPF-style
// spinner). Arrows / the wheel / Up / Down / Page Up / Page Down step it; typing digits
// (and '-') edits it, Enter or leaving the field commits. Clamped to [vmin, vmax];
// cb fires on every change of `value`.
//
#ifndef _uikit_numeric_h
#define _uikit_numeric_h

#include "uikit/widget.h"

namespace uikit {

class NumericUpDown : public Widget
{
public:
	int value, vmin, vmax, step; Action cb;
	NumericUpDown (int l, int t, int w, int h, int lo, int hi, int val, int step_, Action cb_);
	void setValue (int v);			// clamp, repaint, fire cb if it changed
	void onDraw () override;
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override;
	bool onKey (long k) override;
private:
	char m_edit[16]; int m_elen;		// digits being typed (m_elen < 0: not editing)
	int  m_down;				// the arrow held: 1 up, 2 down, 0 none
	void commit ();
};

} // namespace uikit

#endif
