#include "wtk/label.h"

namespace wtk {

Label::Label (int l, int t, int w, int h, const char *s, unsigned fg_, unsigned bg_)
  : Widget (l, t, w, h), bg (bg_), fg (fg_)
{ int i = 0; if (s) for (; s[i] && i < 127; i++) text[i] = s[i]; text[i] = '\0'; }

void Label::setText (const char *s)
{ int i = 0; if (s) for (; s[i] && i < 127; i++) text[i] = s[i]; text[i] = '\0'; invalidate (true); }

void Label::onDraw ()
{ canvas.clear (bg); canvas.text (2, (height - wk_fh ()) / 2, text, fg); }

} // namespace wtk
