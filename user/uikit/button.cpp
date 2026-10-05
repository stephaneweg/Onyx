#include "uikit/button.h"

namespace uikit {

Button::Button (int l, int t, int w, int h, const char *s, Action cb_)
  : Widget (l, t, w, h), cb (cb_)
{ canFocus = true; int i = 0; if (s) for (; s[i] && i < 63; i++) text[i] = s[i]; text[i] = '\0'; }

void Button::onDraw ()
{
	canvas.clear (bgColor ());				// (its rounded corners blend into it)
	int st = disabled ? UK_DISABLED : pressed ? UK_PRESSED : hover ? UK_HOT : UK_NORMAL;
	if (hasFocus && !disabled) st |= UK_FOCUS;
	int bx, by, bw, bh;
	uk_framed (canvas, 0, 0, width, height, C_BUTTON, st, &bx, &by, &bw, &bh);
	uk_text_c (canvas, bx, by, bw, bh, text, disabled ? uk_mix (C_BUTTON, C_BUTTON_TEXT, 110) : C_BUTTON_TEXT);
}

bool Button::onMouse (int mx, int /*my*/, int bl, int, int, int)
{
	if (mx < 0) { if (hover || pressed) { hover = pressed = false; invalidate (true); } return false; }
	bool wasHover = hover, wasPressed = pressed;
	hover = true;
	if (bl && !pressed) pressed = true;
	else if (!bl && pressed) { pressed = false; if (cb) cb (*this); }
	if (hover != wasHover || pressed != wasPressed) invalidate (true);
	return true;
}

bool Button::onKey (long k)
{
	if (disabled || (k != ' ' && k != KEY_ENTER)) return false;
	if (cb) cb (*this);
	return true;
}

} // namespace uikit
