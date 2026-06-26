#include "wtk/checkbox.h"

namespace wtk {

Checkbox::Checkbox (int l, int t, int w, int h, const char *s, bool chk, Action cb_, unsigned bg_)
  : Widget (l, t, w, h), checked (chk), cb (cb_), bg (bg_)
{ canFocus = true; int i = 0; if (s) for (; s[i] && i < 63; i++) text[i] = s[i]; text[i] = '\0'; }

void Checkbox::onDraw ()
{
	canvas.clear (bg);
	int bs = wk_fh (), by = (height - bs) / 2;
	canvas.fillRect (0, by, bs, bs, (!disabled && hover) ? C_FACE_HI : C_FACE);
	canvas.frameRect (0, by, bs, bs, C_BORDER);
	if (checked) canvas.fillRect (3, by + 3, bs - 6, bs - 6, C_ACCENT);
	canvas.text (bs + 6, (height - wk_fh ()) / 2, text, disabled ? C_DIS : C_TEXT);
}

bool Checkbox::onMouse (int mx, int /*my*/, int bl, int, int, int)
{
	if (mx < 0) { if (hover) { hover = false; invalidate (true); } pressed = false; return false; }
	if (disabled) return true;
	bool wh = hover; hover = true;
	if (bl && !pressed) pressed = true;
	else if (!bl && pressed) { pressed = false; checked = !checked; if (cb) cb (*this); invalidate (true); }
	if (hover != wh) invalidate (true);
	return true;
}

} // namespace wtk
