#include "wtk/slider.h"

namespace wtk {

Slider::Slider (int l, int t, int w, int h, int lo, int hi, int val, Action cb_, unsigned bg_)
  : Widget (l, t, w, h), value (val), vmin (lo), vmax (hi), cb (cb_), bg (bg_) { canFocus = true; }

enum { SL_KNOB = 12 };			// the knob's width

void Slider::setFromX (int px)			// the knob's centre under px
{
	int range = (vmax > vmin) ? vmax - vmin : 1, span = width - SL_KNOB > 1 ? width - SL_KNOB : 1;
	int tt = px - SL_KNOB / 2;
	if (tt < 0) tt = 0;
	if (tt > span) tt = span;
	int nv = vmin + (range * tt + span / 2) / span;
	if (nv < vmin) nv = vmin;
	if (nv > vmax) nv = vmax;
	if (nv != value) { value = nv; if (cb) cb (*this); invalidate (true); }
}

void Slider::onDraw ()
{
	canvas.clear (bg);
	const int kw = SL_KNOB;
	int range = (vmax > vmin) ? vmax - vmin : 1, tx = (value - vmin) * (width - kw) / range;
	if (tx < 0) tx = 0;
	if (tx > width - kw) tx = width - kw;
	int st = disabled ? WK_DISABLED : pressed ? WK_PRESSED : hover ? WK_HOT : WK_NORMAL;
	if (hasFocus && !disabled) st |= WK_FOCUS;
	wk_slider_mark (canvas, 0, 0, width, height, tx + kw / 2, tx, kw, st);
}

bool Slider::onMouse (int mx, int /*my*/, int bl, int, int, int)
{
	if (mx < 0) { if (hover || pressed) { hover = false; pressed = false; invalidate (true); } return false; }
	if (disabled) return true;
	bool wh = hover, wp = pressed; hover = true;
	if (bl) { if (!pressed) setFocus (); pressed = true; setFromX (mx); } else pressed = false;
	if (hover != wh || pressed != wp) invalidate (true);
	return true;
}

} // namespace wtk
