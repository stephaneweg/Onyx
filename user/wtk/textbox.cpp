#include "wtk/textbox.h"

namespace wtk {

Textbox::Textbox (int l, int t, int w, int h, const char *s, Action cb_)
  : Widget (l, t, w, h), caret (0), password (false), cb (cb_), padR (0), vstart (0)
{ int i = 0; if (s) for (; s[i] && i < 63; i++) text[i] = s[i]; text[i] = '\0'; caret = wk_len (text); }

void Textbox::setText (const char *s)
{ int i = 0; if (s) for (; s[i] && i < 63; i++) text[i] = s[i]; text[i] = '\0'; caret = wk_len (text); invalidate (true); }

// ---- with a proportional face (wtk/text.h): UTF-8, measured -------------------------------------------
// What is shown (a password: a '*' a character) and where byte i of the text is in it.
int Textbox::shown (char *d) const
{
	int n = 0, len = wk_len (text);
	if (!password) { for (; n < len; n++) d[n] = text[n]; d[n] = '\0'; return n; }
	for (int i = 0; i < len; i = wk_u8_next (text, i, len)) d[n++] = '*';
	d[n] = '\0';
	return n;
}
int Textbox::shownAt (int i) const
{
	if (!password) return i;
	int n = 0, len = wk_len (text);
	for (int k = 0; k < i && k < len; k = wk_u8_next (text, k, len)) n++;
	return n;
}
int Textbox::textAt (int d) const			// (the inverse: a place in what is shown)
{
	if (!password) return d;
	int len = wk_len (text), k = 0;
	for (int n = 0; n < d && k < len; n++) k = wk_u8_next (text, k, len);
	return k;
}

void Textbox::drawFace ()
{
	const int pad = 6; int fh = wk_fh ();
	canvas.clear (bgColor ());
	wk_sunken (canvas, 0, 0, width, height, 4, disabled ? wk_tone (C_FACE, 150) : C_FIELD, hasFocus && !disabled);
	char d[64]; int dn = shown (d), len = wk_len (text);
	int avail = width - padR - 2 * pad - 2; if (avail < 1) avail = 1;
	if (caret < 0) caret = 0;
	if (caret > len) caret = len;
	if (vstart > caret) vstart = caret;
	if (vstart < 0 || vstart > len) vstart = 0;
	int dc = shownAt (caret);
	while (vstart < caret && wk_tw_n (d + shownAt (vstart), dc - shownAt (vstart)) > avail)
		vstart = wk_u8_next (text, vstart, len);		// the caret out on the right: scroll
	while (vstart > 0)					// room on the right: show more on the left
	{
		int p = wk_u8_prev (text, vstart), dp = shownAt (p);
		if (wk_tw_n (d + dp, dn - dp) > avail) break;
		vstart = p;
	}
	int ds = shownAt (vstart), ty = (height - fh) / 2;
	wk_text_clip (canvas, pad, ty, d + ds, disabled ? C_DIS : C_FIELD_TEXT, 0, pad, 0, width - padR - 2 * pad, height);
	if (hasFocus && !disabled)
	{
		int cx = pad + wk_tw_n (d + ds, dc - ds);
		if (cx >= pad && cx < width - padR - 2) canvas.fillRect (cx, ty, 2, fh, C_ACCENT);
	}
}

void Textbox::onDraw ()
{
	if (wk_textface ()) { drawFace (); return; }
	const int pad = 6; int fw = wk_fw (), fh = wk_fh ();
	canvas.clear (bgColor ());
	wk_sunken (canvas, 0, 0, width, height, 4, disabled ? wk_tone (C_FACE, 150) : C_FIELD, hasFocus && !disabled);
	int maxvis = (width - padR - 2 * pad) / fw; if (maxvis < 1) maxvis = 1; if (maxvis > 63) maxvis = 63;
	int len = wk_len (text), start = 0;
	if (caret > maxvis - 1) start = caret - (maxvis - 1);
	if (start < 0) start = 0;
	vstart = start;
	char vis[64]; int j = 0;
	for (int c = start; c < len && j < maxvis; c++) vis[j++] = password ? '*' : text[c];
	vis[j] = '\0';
	int ty = (height - fh) / 2;
	canvas.text (pad, ty, vis, disabled ? C_DIS : C_FIELD_TEXT);
	if (hasFocus && !disabled)
	{
		int cx = pad + (caret - start) * fw;
		if (cx >= pad && cx < width - padR - 2) canvas.fillRect (cx, ty, 2, fh, C_ACCENT);
	}
}

bool Textbox::onMouse (int mx, int /*my*/, int bl, int, int, int)
{
	if (mx < 0) { pressed = false; return false; }
	if (disabled) return true;
	if (bl && !pressed)
	{
		pressed = true; setFocus ();
		if (wk_textface ())
		{
			char d[64]; int dn = shown (d), ds = shownAt (vstart);
			caret = textAt (ds + wk_tpos (d + ds, dn - ds, mx - 6));
		}
		else
		{
			int rel = vstart + (mx - 6 + wk_fw () / 2) / wk_fw (), len = wk_len (text);
			caret = rel < 0 ? 0 : (rel > len ? len : rel);
		}
		invalidate (true);
	}
	else if (!bl) pressed = false;
	return true;
}

bool Textbox::keyFace (long k)
{
	int len = wk_len (text);
	if (caret < 0) caret = 0;
	if (caret > len) caret = len;
	char u[4]; int n = wk_u8_key (k, u);
	if (n > 0)
	{
		if (len + n <= 63)
		{
			for (int i = len; i >= caret; i--) text[i + n] = text[i];
			for (int i = 0; i < n; i++) text[caret + i] = u[i];
			caret += n;
		}
	}
	else if (k == KEY_BACKSPACE)
	{
		if (caret > 0)
		{
			int p = wk_u8_prev (text, caret), d = caret - p;
			for (int i = p; i + d <= len; i++) text[i] = text[i + d];
			caret = p;
		}
	}
	else if (k == KEY_DEL)
	{
		if (caret < len)
		{
			int d = wk_u8_next (text, caret, len) - caret;
			for (int i = caret; i + d <= len; i++) text[i] = text[i + d];
		}
	}
	else if (k == KEY_LEFT)  caret = wk_u8_prev (text, caret);
	else if (k == KEY_RIGHT) caret = wk_u8_next (text, caret, len);
	else if (k == KEY_HOME)  caret = 0;
	else if (k == KEY_END)   caret = len;
	else if (k == KEY_ENTER) { if (cb) cb (*this); }
	else return false;
	invalidate (true); return true;
}

bool Textbox::onKey (long k)
{
	if (wk_textface ()) return keyFace (k);
	int len = wk_len (text);
	if ((k >= 32 && k <= 126) || (k >= 0xA0 && k <= 0xFF))	// ASCII + Latin-1 (é è à ç ...)
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
