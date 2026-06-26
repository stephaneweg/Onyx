#include "wtk/scrollbar.h"

namespace wtk {

Scrollbar::Scrollbar (int l, int t, int w, int h, bool vert, int maxv, int val, Action cb_)
  : Widget (l, t, w, h), vertical (vert), value (val), vmax (maxv < 1 ? 1 : maxv), cb (cb_) {}

void Scrollbar::setFromXY (int px, int py)
{
	// Map the cursor over the THUMB's travel range (span - thumb), centring the thumb on
	// the cursor -- same geometry as onDraw(). Mapping over the full span made value==vmax
	// reachable only at the very last pixel, so you could never scroll all the way down.
	int span = vertical ? height : width;
	int thumb = span / 5; if (thumb < 14) thumb = 14; if (thumb > span) thumb = span;
	int range = span - thumb; if (range < 1) range = 1;
	int pos = (vertical ? py : px) - thumb / 2;
	if (pos < 0) pos = 0;
	if (pos > range) pos = range;
	int nv = vmax * pos / range;
	if (nv < 0) nv = 0;
	if (nv > vmax) nv = vmax;
	if (nv != value) { value = nv; if (cb) cb (*this); invalidate (true); }
}

void Scrollbar::scrollBy (int units)			// wheel / programmatic nudge
{
	int nv = value + units;
	if (nv < 0) nv = 0;
	if (nv > vmax) nv = vmax;
	if (nv != value) { value = nv; if (cb) cb (*this); invalidate (true); }
}

void Scrollbar::onDraw ()
{
	canvas.clear (C_FIELD); canvas.frameRect (0, 0, width, height, C_BORDER);
	int span = vertical ? height : width, thumb = span / 5;
	if (thumb < 14) thumb = 14;
	if (thumb > span) thumb = span;
	int pos = (span - thumb) * value / (vmax > 0 ? vmax : 1);
	unsigned face = (hover || pressed) ? C_FACE_HI : C_FACE;
	if (vertical) canvas.fillRect (1, pos, width - 2, thumb, face);
	else          canvas.fillRect (pos, 1, thumb, height - 2, face);
}

bool Scrollbar::onMouse (int mx, int my, int bl, int, int, int wheel)
{
	if (mx < 0) { if (hover) { hover = false; invalidate (true); } pressed = false; return false; }
	if (wheel) { scrollBy (-wheel * 3); return true; }	// a forward notch scrolls up
	bool wh = hover; hover = true;
	if (bl) { pressed = true; setFromXY (mx, my); } else pressed = false;
	if (hover != wh) invalidate (true);
	return true;
}

} // namespace wtk
