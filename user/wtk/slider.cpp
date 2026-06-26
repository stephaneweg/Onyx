#include "wtk/slider.h"

namespace wtk {

Slider::Slider (int l, int t, int w, int h, int lo, int hi, int val, Action cb_, unsigned bg_)
  : Widget (l, t, w, h), value (val), vmin (lo), vmax (hi), cb (cb_), bg (bg_) { canFocus = true; }

void Slider::setFromX (int px)
{
	int range = (vmax > vmin) ? vmax - vmin : 1, tt = px;
	if (tt < 0) tt = 0;
	if (tt > width) tt = width;
	int nv = vmin + range * tt / (width > 0 ? width : 1);
	if (nv < vmin) nv = vmin;
	if (nv > vmax) nv = vmax;
	if (nv != value) { value = nv; if (cb) cb (*this); invalidate (true); }
}

void Slider::onDraw ()
{
	canvas.clear (bg);
	int midy = height / 2; canvas.fillRect (0, midy - 1, width, 3, C_BORDER);
	int range = (vmax > vmin) ? vmax - vmin : 1, tx = (value - vmin) * (width - 8) / range;
	if (tx < 0) tx = 0;
	if (tx > width - 8) tx = width - 8;
	canvas.fillRect (tx, 0, 8, height, (hover || pressed) ? C_FACE_HI : C_FACE);
	canvas.frameRect (tx, 0, 8, height, C_BORDER);
}

bool Slider::onMouse (int mx, int /*my*/, int bl, int, int, int)
{
	if (mx < 0) { if (hover) { hover = false; invalidate (true); } pressed = false; return false; }
	bool wh = hover; hover = true;
	if (bl) { pressed = true; setFromX (mx); } else pressed = false;
	if (hover != wh) invalidate (true);
	return true;
}

} // namespace wtk
