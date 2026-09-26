//
// wtk/toggle.h -- ToggleSwitch: an on/off pill switch + label (WPF-style ToggleButton /
// UWP ToggleSwitch). Click (or Space) flips it; cb fires; read `on`.
//
#ifndef _wtk_toggle_h
#define _wtk_toggle_h

#include "wtk/widget.h"

namespace wtk {

class ToggleSwitch : public Widget
{
public:
	char text[64]; bool on; Action cb; unsigned bg;
	ToggleSwitch (int l, int t, int w, int h, const char *s, bool on_, Action cb_, unsigned bg_ = C_BG);
	void setOn (bool v) { if (v != on) { on = v; invalidate (true); } }
	void onDraw () override;
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override;
	bool onKey (long k) override;
};

} // namespace wtk

#endif
