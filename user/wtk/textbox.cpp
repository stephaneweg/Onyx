#include "wtk/textbox.h"

namespace wtk {

Textbox::Textbox (int l, int t, int w, int h, const char *s, Action cb_)
  : Widget (l, t, w, h), caret (0), password (false), cb (cb_)
{ int i = 0; if (s) for (; s[i] && i < 63; i++) text[i] = s[i]; text[i] = '\0'; caret = wk_len (text); }

void Textbox::setText (const char *s)
{ int i = 0; if (s) for (; s[i] && i < 63; i++) text[i] = s[i]; text[i] = '\0'; caret = wk_len (text); invalidate (true); }

void Textbox::onDraw ()
{
	const int pad = 4; int fw = wk_fw (), fh = wk_fh ();
	canvas.clear (disabled ? C_FACE_DN : C_FIELD);
	canvas.frameRect (0, 0, width, height, C_BORDER);
	if (hasFocus && !disabled) canvas.frameRect (1, 1, width - 2, height - 2, C_ACCENT);
	int maxvis = (width - 2 * pad) / fw; if (maxvis < 1) maxvis = 1; if (maxvis > 63) maxvis = 63;
	int len = wk_len (text), start = 0;
	if (caret > maxvis - 1) start = caret - (maxvis - 1);
	if (start < 0) start = 0;
	char vis[64]; int j = 0;
	for (int c = start; c < len && j < maxvis; c++) vis[j++] = password ? '*' : text[c];
	vis[j] = '\0';
	int ty = (height - fh) / 2;
	canvas.text (pad, ty, vis, disabled ? C_DIS : C_TEXT);
	if (hasFocus && !disabled)
	{
		int cx = pad + (caret - start) * fw;
		if (cx >= pad && cx < width - 1) canvas.fillRect (cx, ty, 1, fh, C_ACCENT);
	}
}

bool Textbox::onMouse (int mx, int /*my*/, int bl, int, int, int)
{
	if (mx < 0) { pressed = false; return false; }
	if (disabled) return true;
	if (bl && !pressed)
	{
		pressed = true; setFocus ();
		int rel = (mx - 4) / wk_fw (), len = wk_len (text);
		caret = rel < 0 ? 0 : (rel > len ? len : rel);
		invalidate (true);
	}
	else if (!bl) pressed = false;
	return true;
}

bool Textbox::onKey (long k)
{
	int len = wk_len (text);
	if (k >= 32 && k <= 126)
	{
		if (len < 63)
		{
			if (caret < 0) caret = 0;
			if (caret > len) caret = len;
			for (int i = len; i > caret; i--) text[i] = text[i - 1];
			text[caret] = (char) k; text[len + 1] = '\0'; caret++;
		}
	}
	else if (k == KEY_BACKSPACE) { if (caret > 0) { for (int i = caret - 1; i < len; i++) text[i] = text[i + 1]; caret--; } }
	else if (k == KEY_DEL)       { if (caret < len) for (int i = caret; i < len; i++) text[i] = text[i + 1]; }
	else if (k == KEY_LEFT)      { if (caret > 0) caret--; }
	else if (k == KEY_RIGHT)     { if (caret < len) caret++; }
	else if (k == KEY_HOME)        caret = 0;
	else if (k == KEY_END)         caret = len;
	else if (k == KEY_ENTER)     { if (cb) cb (*this); }
	else return false;
	invalidate (true); return true;
}

} // namespace wtk
