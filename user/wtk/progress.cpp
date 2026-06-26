#include "wtk/progress.h"

namespace wtk {

Progress::Progress (int l, int t, int w, int h, int lo, int hi, int val)
  : Widget (l, t, w, h), value (val), vmin (lo), vmax (hi) {}

void Progress::setValue (int v) { if (v != value) { value = v; invalidate (true); } }

void Progress::onDraw ()
{
	canvas.clear (C_FIELD);
	int range = (vmax > vmin) ? vmax - vmin : 1, fw = (value - vmin) * width / range;
	if (fw < 0) fw = 0;
	if (fw > width) fw = width;
	if (fw > 0) canvas.fillRect (0, 0, fw, height, C_ACCENT);
	canvas.frameRect (0, 0, width, height, C_BORDER);
}

} // namespace wtk
