#include "wtk/checkbox.h"

namespace wtk {

Checkbox::Checkbox (int l, int t, int w, int h, const char *s, bool chk, Action cb_, unsigned bg_)
  : Widget (l, t, w, h), checked (chk), cb (cb_), bg (bg_)
{ canFocus = true; int i = 0; if (s) for (; s[i] && i < 63; i++) text[i] = s[i]; text[i] = '\0'; }

void Checkbox::onDraw ()
{
	canvas.clear (bg);
	int fh = wk_fh (), bs = fh - 2 < height ? fh - 2 : height, by = (height - bs) / 2;
	int st = disabled ? WK_DISABLED : pressed ? WK_PRESSED : hover ? WK_HOT : WK_NORMAL;
	if (hasFocus && !disabled) st |= WK_FOCUS;
	wk_check_mark (canvas, 0, by, bs, checked, st);
	unsigned ink = wk_ink_for (bg);			// (readable on the program's own colour too)
	canvas.text (bs + 7, (height - fh) / 2, text, disabled ? wk_mix (bg, ink, 110) : ink);
}

bool Checkbox::onMouse (int mx, int /*my*/, int bl, int, int, int)
{
	if (mx < 0) { if (hover || pressed) { hover = false; pressed = false; invalidate (true); } return false; }
	if (disabled) return true;
	bool wh = hover, wp = pressed; hover = true;
	if (bl && !pressed) { pressed = true; setFocus (); }
	else if (!bl && pressed) { pressed = false; checked = !checked; if (cb) cb (*this); }
	if (hover != wh || pressed != wp) invalidate (true);
	return true;
}

bool Checkbox::onKey (long k)
{
	if (disabled || (k != ' ' && k != KEY_ENTER)) return false;
	checked = !checked;
	if (cb) cb (*this);
	invalidate (true);
	return true;
}

} // namespace wtk
