#include "wtk/button.h"

namespace wtk {

Button::Button (int l, int t, int w, int h, const char *s, Action cb_)
  : Widget (l, t, w, h), cb (cb_)
{ canFocus = true; int i = 0; if (s) for (; s[i] && i < 63; i++) text[i] = s[i]; text[i] = '\0'; }

void Button::onDraw ()
{
	unsigned face = pressed ? C_FACE_DN : (hover ? C_FACE_HI : C_FACE);
	canvas.clear (face);
	canvas.frameRect (0, 0, width, height, C_BORDER);
	int fh = wk_fh (), fw = wk_fw ();
	int tx = (width - wk_len (text) * fw) / 2; if (tx < 2) tx = 2;
	canvas.text (tx + (pressed ? 1 : 0), (height - fh) / 2 + (pressed ? 1 : 0), text, C_TEXT);
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

} // namespace wtk
