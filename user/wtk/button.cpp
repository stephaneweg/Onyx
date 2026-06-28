#include "wtk/button.h"
#include "wtk/skin.h"		// wk_button_skin (9-slice button.bmp)

namespace wtk {

Button::Button (int l, int t, int w, int h, const char *s, Action cb_)
  : Widget (l, t, w, h), cb (cb_)
{ canFocus = true; int i = 0; if (s) for (; s[i] && i < 63; i++) text[i] = s[i]; text[i] = '\0'; }

void Button::onDraw ()
{
	int fh = wk_fh (), fw = wk_fw ();
	int tx = (width - wk_len (text) * fw) / 2; if (tx < 2) tx = 2;
	int ty = (height - fh) / 2;
	Skin &bs = wk_button_skin ();
	if (bs.valid () && !disabled)			// skinned: 9-slice bitmap + black label
	{
		// W = canvas.stride (real row width): the canvas buffer is grow-only, so after a
		// width resize stride > width and passing `width` here would shear the 9-slice
		// (black lines/dots, e.g. when the side-panel collapses). Dest slice stays logical.
		bs.drawOn (canvas.px, canvas.stride, height, pressed ? 2 : (hover ? 1 : 0), 0, 0, width, height);
		if (hasFocus) canvas.frameRect (1, 1, width - 2, height - 2, C_ACCENT);
		canvas.text (tx + (pressed ? 1 : 0), ty + (pressed ? 1 : 0), text, 0x00000000);
		return;
	}
	unsigned face = (disabled || pressed) ? C_FACE_DN : (hover ? C_FACE_HI : C_FACE);
	canvas.clear (face);
	canvas.frameRect (0, 0, width, height, C_BORDER);
	if (hasFocus && !disabled) canvas.frameRect (1, 1, width - 2, height - 2, C_ACCENT);
	canvas.text (tx + (pressed ? 1 : 0), ty + (pressed ? 1 : 0), text, disabled ? C_DIS : C_TEXT);
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
