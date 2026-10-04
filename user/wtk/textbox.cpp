#include "wtk/textbox.h"
#include "wtk/menu.h"		// WK_CTRL
#include "clipboard.h"

namespace wtk {

Textbox::Textbox (int l, int t, int w, int h, const char *s, Action cb_)
  : Widget (l, t, w, h), caret (0), password (false), cb (cb_), maxLen (63), padR (0), vstart (0), changed (0)
{ canFocus = true; int i = 0; if (s) for (; s[i] && i < maxLen; i++) text[i] = s[i]; text[i] = '\0'; caret = wk_len (text); }

void Textbox::setText (const char *s)
{ int i = 0; if (s) for (; s[i] && i < maxLen; i++) text[i] = s[i]; text[i] = '\0'; caret = wk_len (text); invalidate (true); }

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
	char d[TEXT_CAP]; int dn = shown (d), len = wk_len (text);
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
	int maxvis = (width - padR - 2 * pad) / fw; if (maxvis < 1) maxvis = 1; if (maxvis > TEXT_CAP - 1) maxvis = TEXT_CAP - 1;
	int len = wk_len (text), start = 0;
	if (caret > maxvis - 1) start = caret - (maxvis - 1);
	if (start < 0) start = 0;
	vstart = start;
	char vis[TEXT_CAP]; int j = 0;
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
	wk_cursor (KAPI_CURSOR_TEXT);
	if (bl && !pressed)
	{
		pressed = true; setFocus ();
		if (wk_textface ())
		{
			char d[TEXT_CAP]; int dn = shown (d), ds = shownAt (vstart);
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
		if (len + n <= maxLen)
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
	else if (k == KEY_ENTER) { if (!cb) return false; cb (*this); }
	else return false;
	invalidate (true); return true;
}

// ---- the clipboard: Ctrl+C / Ctrl+X the whole field (not a password's), Ctrl+V at the caret -----------
bool Textbox::clipKey (long k)
{
	if (k == WK_CTRL ('C') || k == WK_CTRL ('X'))
	{
		if (password || !text[0]) return true;
		clip_set_text (text);
		if (k == WK_CTRL ('X')) { text[0] = '\0'; caret = 0; invalidate (true); }
		return true;
	}
	if (k != WK_CTRL ('V')) return false;
	static char b[TEXT_CAP * 4];
	if (!clip_get_text (b, sizeof b)) return true;
	// One line: tabs and line breaks become spaces, those around the text are dropped. Without a
	// face the field is Latin-1: the clipboard's UTF-8 is brought to it (other characters dropped).
	bool face = wk_textface () != 0;
	char in[TEXT_CAP]; int n = 0;
	const unsigned char *p = (const unsigned char *) b;
	while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
	for (; *p && n < TEXT_CAP - 1; p++)
	{
		unsigned c = *p;
		if (c == '\r') continue;
		if (c == '\t' || c == '\n') c = ' ';
		else if (c < 32 || c == 127) continue;
		else if (!face && c >= 0x80)
		{
			if ((c & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80)
			{ c = ((c & 0x1F) << 6) | (p[1] & 0x3F); p++; if (c < 0xA0) continue; }
			else { while ((p[1] & 0xC0) == 0x80) p++; continue; }
		}
		in[n++] = (char) c;
	}
	while (n > 0 && in[n - 1] == ' ') n--;
	int len = wk_len (text);
	if (caret < 0) caret = 0;
	if (caret > len) caret = len;
	if (n > maxLen - len)		// what fits, not cutting a character in two
	{ n = maxLen - len; if (n < 0) n = 0; if (face) while (n > 0 && (in[n] & 0xC0) == 0x80) n--; }
	if (n <= 0) return true;
	for (int i = len; i >= caret; i--) text[i + n] = text[i];
	for (int i = 0; i < n; i++) text[caret + i] = in[i];
	caret += n;
	invalidate (true); return true;
}

void Textbox::onTabFocus () { caret = wk_len (text); vstart = 0; }

// A key: the edit (editKey), then `changed` told if the text is not what it was.
bool Textbox::onKey (long k)
{
	char before[TEXT_CAP];
	int i = 0; for (; text[i]; i++) before[i] = text[i]; before[i] = '\0';
	bool r = editKey (k);
	if (r && changed) { int j = 0; while (before[j] && before[j] == text[j]) j++; if (before[j] != text[j]) changed (*this); }
	return r;
}

bool Textbox::editKey (long k)
{
	if (clipKey (k)) return true;
	if (wk_textface ()) return keyFace (k);
	int len = wk_len (text);
	if ((k >= 32 && k <= 126) || (k >= 0xA0 && k <= 0xFF))	// ASCII + Latin-1 (é è à ç ...)
	{
		if (len < maxLen)
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
	else if (k == KEY_ENTER)     { if (!cb) return false; cb (*this); }
	else return false;
	invalidate (true); return true;
}

} // namespace wtk
