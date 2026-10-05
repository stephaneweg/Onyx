#include "uikit/panel.h"

namespace uikit {

Panel::Panel (int l, int t, int w, int h, unsigned bg_) : Widget (l, t, w, h), bg (bg_) {}
void Panel::onDraw () { canvas.clear (bg); }

} // namespace uikit
