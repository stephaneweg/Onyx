#include "wtk/textarea.h"
// operator new[]/delete[] resolve at link from the app's onyxpp.hpp (see canvas.cpp).

namespace wtk {

Textarea::Textarea (int l, int t, int w, int h, int capacity)
  : Widget (l, t, w, h), cap (capacity < 16 ? 16 : capacity),
    len (0), caret (0), top (0), left (0), rows (1), cols (1), readonly (false)
{ canFocus = true; buf = new char[cap]; buf[0] = '\0'; }

Textarea::~Textarea () { delete [] buf; }

void Textarea::setContent (const char *s)
{ len = 0; caret = 0; top = 0; left = 0; if (s) while (s[len] && len < cap - 1) { buf[len] = s[len]; len++; } buf[len] = '\0'; invalidate (true); }

void Textarea::insertAt (int ch)
{ if (len >= cap - 1) return; for (int i = len; i > caret; i--) buf[i] = buf[i - 1]; buf[caret] = (char) ch; len++; caret++; buf[len] = '\0'; }

void Textarea::deleteAt (int i)
{ if (i < 0 || i >= len) return; for (int j = i; j < len; j++) buf[j] = buf[j + 1]; len--; }

void Textarea::ensureVisible (int vr, int vc)
{
	int cl = 0; for (int i = 0; i < caret; i++) if (buf[i] == '\n') cl++;
	int cc = caret - lineStart (caret);
	if (cl < top) top = cl;
	if (cl >= top + vr) top = cl - vr + 1;
	if (cc < left) left = cc;
	if (cc >= left + vc) left = cc - vc + 1;
	if (top < 0) top = 0;
	if (left < 0) left = 0;
}

void Textarea::onDraw ()
{
	const int pad = 4; int fw = wk_fw (), fh = wk_fh ();
	rows = (height - 4) / fh; if (rows < 1) rows = 1;
	cols = (width - 2 * pad) / fw; if (cols < 1) cols = 1; if (cols > 159) cols = 159;
	canvas.clear (disabled ? C_FACE_DN : C_FIELD);
	canvas.frameRect (0, 0, width, height, C_BORDER);
	if (hasFocus && !disabled) canvas.frameRect (1, 1, width - 2, height - 2, C_ACCENT);
	int i = 0, line = 0; while (line < top && buf[i]) { if (buf[i] == '\n') line++; i++; }
	for (int r = 0; r < rows; r++)
	{
		int le = lineEnd (i); char vis[160]; int j = 0;
		for (int c = i + left; c < le && j < cols; c++) vis[j++] = buf[c];
		vis[j] = '\0';
		if (j > 0) canvas.text (pad, 2 + r * fh, vis, disabled ? C_DIS : C_TEXT);
		if (buf[le] != '\n') break;
		i = le + 1;
	}
	if (hasFocus && !disabled)
	{
		int cl = 0; for (int c = 0; c < caret; c++) if (buf[c] == '\n') cl++;
		int cc = caret - lineStart (caret);
		int cx = pad + (cc - left) * fw, cy = 2 + (cl - top) * fh;
		if (cy >= 0 && cy < height - 2 && cx >= pad && cx < width - 1) canvas.fillRect (cx, cy, 1, fh, C_ACCENT);
	}
}

bool Textarea::onMouse (int mx, int my, int bl, int, int, int wheel)
{
	if (mx < 0) { pressed = false; return false; }
	if (disabled) return true;
	if (wheel)					// scroll ~3 lines per notch (forward = up)
	{
		int lines = 1; for (int i = 0; i < len; i++) if (buf[i] == '\n') lines++;
		int mt = lines - rows; if (mt < 0) mt = 0;
		int nt = top - wheel * 3;
		if (nt < 0) nt = 0; if (nt > mt) nt = mt;
		if (nt != top) { top = nt; invalidate (true); }
		return true;
	}
	if (bl && !pressed)
	{
		pressed = true; setFocus ();
		const int pad = 4, fw = wk_fw (), fh = wk_fh ();
		int row = (my - 2) / fh, col = (mx - pad) / fw;
		if (row < 0) row = 0;
		if (col < 0) col = 0;
		int wantLine = top + row, wantCol = left + col, i = 0, line = 0;
		while (line < wantLine && buf[i]) { if (buf[i] == '\n') line++; i++; }
		int le = lineEnd (i), c = i + wantCol; if (c > le) c = le; caret = c;
		ensureVisible ((height - 4) / fh, (width - 2 * pad) / fw);
		invalidate (true);
	}
	else if (!bl) pressed = false;
	return true;
}

bool Textarea::onKey (long k)
{
	if (k >= 32 && k <= 126)     { if (!readonly) insertAt ((int) k); }
	else if (k == KEY_ENTER)     { if (!readonly) insertAt ('\n'); }
	else if (k == KEY_TAB)       { if (!readonly) { insertAt (' '); insertAt (' '); } }
	else if (k == KEY_BACKSPACE) { if (!readonly && caret > 0) { deleteAt (caret - 1); caret--; } }
	else if (k == KEY_DEL)       { if (!readonly) deleteAt (caret); }
	else if (k == KEY_LEFT)      { if (caret > 0) caret--; }
	else if (k == KEY_RIGHT)     { if (caret < len) caret++; }
	else if (k == KEY_HOME)        caret = lineStart (caret);
	else if (k == KEY_END)         caret = lineEnd (caret);
	else if (k == KEY_UP)
	{
		int ls = lineStart (caret), col = caret - ls;
		if (ls > 0) { int pls = lineStart (ls - 1), ple = ls - 1, c = pls + col; caret = c > ple ? ple : c; }
	}
	else if (k == KEY_DOWN)
	{
		int ls = lineStart (caret), col = caret - ls, le = lineEnd (caret);
		if (buf[le] == '\n') { int nls = le + 1, nle = lineEnd (nls), c = nls + col; caret = c > nle ? nle : c; }
	}
	else return false;
	ensureVisible ((height - 4) / wk_fh (), (width - 8) / wk_fw ());
	invalidate (true); return true;
}

} // namespace wtk
