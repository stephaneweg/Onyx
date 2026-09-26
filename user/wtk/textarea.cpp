#include "wtk/textarea.h"
#include "wtk/menu.h"		// WK_CTRL
#include "clipboard.h"
// operator new[]/delete[] resolve at link from the app's onyxpp.hpp (see canvas.cpp).

namespace wtk {

Textarea::Textarea (int l, int t, int w, int h, int capacity)
  : Widget (l, t, w, h), cap (capacity < 16 ? 16 : capacity),
    len (0), caret (0), top (0), left (0), rows (1), cols (1), readonly (false), barDrag (false), anchor (-1),
    ownColors (false), colBg (0), colText (0), colCaret (0), colSel (0)
{ canFocus = true; buf = new char[cap]; buf[0] = '\0'; }

Textarea::~Textarea () { delete [] buf; }

void Textarea::setContent (const char *s)
{ anchor = -1; len = 0; caret = 0; top = 0; left = 0; if (s) while (s[len] && len < cap - 1) { buf[len] = s[len]; len++; } buf[len] = '\0'; invalidate (true); }

void Textarea::insertAt (int ch)
{ if (len >= cap - 1) return; for (int i = len; i > caret; i--) buf[i] = buf[i - 1]; buf[caret] = (char) ch; len++; caret++; buf[len] = '\0'; }

void Textarea::insertText (const char *s)
{
	if (s == 0) return;
	if (hasSelection () && !readonly) deleteSelection ();
	for (int i = 0; s[i]; i++) if (s[i] != '\r') insertAt ((unsigned char) s[i]);
	invalidate (true);
}

int Textarea::selectedText (char *out, int cap) const
{
	int s = selStart (), e = selEnd (), n = 0;
	for (int i = s; i < e && n < cap - 1; i++) out[n++] = buf[i];
	out[n] = '\0';
	return n;
}

void Textarea::deleteSelection ()
{
	if (!hasSelection ()) { anchor = -1; return; }
	int s = selStart (), e = selEnd (), n = e - s;
	for (int i = s; i + n <= len; i++) buf[i] = buf[i + n];
	len -= n; caret = s; anchor = -1;
	invalidate (true);
}

void Textarea::selectAll () { anchor = 0; caret = len; invalidate (true); }

void Textarea::copy ()
{
	if (!hasSelection ()) return;
	clip_set_text_n (buf + selStart (), selEnd () - selStart ());
}
void Textarea::cut () { if (readonly || !hasSelection ()) return; copy (); deleteSelection (); }
void Textarea::paste ()
{
	if (readonly) return;
	static char b[8192];
	if (clip_get_text (b, sizeof b)) insertText (b);
}

void Textarea::gotoLine (int line)
{
	int i = 0, l = 0;
	while (l < line && buf[i]) { if (buf[i] == '\n') l++; i++; }
	caret = i;
	ensureVisible ((height - 4) / wk_fh (), (width - 8 - WK_SBW) / wk_fw ());
	invalidate (true);
}

int Textarea::caretLine () const
{ int l = 0; for (int i = 0; i < caret; i++) if (buf[i] == '\n') l++; return l; }

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
	cols = (width - 2 * pad - WK_SBW) / fw; if (cols < 1) cols = 1; if (cols > 159) cols = 159;
	unsigned bgc = ownColors ? colBg : C_FIELD, txc = ownColors ? colText : C_TEXT, crc = ownColors ? colCaret : C_ACCENT;
	canvas.clear (disabled ? C_FACE_DN : bgc);
	canvas.frameRect (0, 0, width, height, C_BORDER);
	if (hasFocus && !disabled) canvas.frameRect (1, 1, width - 2, height - 2, C_ACCENT);
	int i = 0, line = 0; while (line < top && buf[i]) { if (buf[i] == '\n') line++; i++; }
	int ss = selStart (), se = selEnd (); bool sel = hasSelection ();
	for (int r = 0; r < rows; r++)
	{
		int le = lineEnd (i); char vis[160]; int j = 0;
		if (sel && ss <= le && se > i)			// this line's part of the selection
		{
			int a = ss > i ? ss - i : 0, b = (se < le ? se - i : le - i + 1) ;	// (+1: the line break)
			a -= left; b -= left;
			if (a < 0) a = 0;
			if (b > cols) b = cols;
			if (b > a) canvas.fillRect (pad + a * fw, 2 + r * fh, (b - a) * fw, fh, ownColors ? colSel : hasFocus ? 0x00355070 : 0x00303A48);
		}
		for (int c = i + left; c < le && j < cols; c++) vis[j++] = buf[c];
		vis[j] = '\0';
		if (j > 0) canvas.text (pad, 2 + r * fh, vis, disabled ? C_DIS : txc);
		if (buf[le] != '\n') break;
		i = le + 1;
	}
	if (hasFocus && !disabled)
	{
		int cl = 0; for (int c = 0; c < caret; c++) if (buf[c] == '\n') cl++;
		int cc = caret - lineStart (caret);
		int cx = pad + (cc - left) * fw, cy = 2 + (cl - top) * fh;
		if (cy >= 0 && cy < height - 2 && cx >= pad && cx < width - 1) canvas.fillRect (cx, cy, ownColors ? 2 : 1, fh, crc);
	}

	// Auto vertical scrollbar: shown only when the text is taller than the view.
	int totalLines = 1; for (int c = 0; c < len; c++) if (buf[c] == '\n') totalLines++;
	int trackH = height - 2;
	WkThumb th = wk_thumb ((long) totalLines * fh, (long) rows * fh, (long) top * fh, trackH);
	if (th.show)
		wk_draw_vscroll (canvas, width - WK_SBW - 1, 1, WK_SBW, trackH, th,
				 C_FACE_DN, barDrag ? C_FACE_HI : C_FACE);
}

bool Textarea::onMouse (int mx, int my, int bl, int, int, int wheel)
{
	if (mx < 0) { pressed = false; barDrag = false; return false; }
	if (disabled) return true;
	int fh = wk_fh ();
	int lines = 1; for (int i = 0; i < len; i++) if (buf[i] == '\n') lines++;
	int mt = lines - rows; if (mt < 0) mt = 0;
	if (wheel)					// kernel pre-scaled the notch; forward = up
	{
		int nt = top - wheel;
		if (nt < 0) nt = 0;
		if (nt > mt) nt = mt;
		if (nt != top) { top = nt; invalidate (true); }
		return true;
	}
	// Scrollbar geometry + a px->line helper for dragging the thumb.
	int trackH = height - 2;
	WkThumb th = wk_thumb ((long) lines * fh, (long) rows * fh, (long) top * fh, trackH);
	bool overBar = th.show && mx >= width - WK_SBW - 1;
	if (bl && barDrag)				// continue an in-progress thumb drag
	{
		long pp = wk_thumb_pos (my - 1, trackH, (long) lines * fh, (long) rows * fh, th.h);
		int nt = (int) ((pp + fh / 2) / fh); if (nt < 0) nt = 0; if (nt > mt) nt = mt;
		if (nt != top) { top = nt; invalidate (true); }
		return true;
	}
	if (bl && !pressed)
	{
		pressed = true; setFocus ();
		if (overBar)				// grab the thumb
		{
			barDrag = true;
			long pp = wk_thumb_pos (my - 1, trackH, (long) lines * fh, (long) rows * fh, th.h);
			int nt = (int) ((pp + fh / 2) / fh); if (nt < 0) nt = 0; if (nt > mt) nt = mt;
			top = nt; invalidate (true);
			return true;
		}
		const int pad = 4, fw = wk_fw ();
		int row = (my - 2) / fh, col = (mx - pad) / fw;
		if (row < 0) row = 0;
		if (col < 0) col = 0;
		int wantLine = top + row, wantCol = left + col, i = 0, line = 0;
		while (line < wantLine && buf[i]) { if (buf[i] == '\n') line++; i++; }
		int le = lineEnd (i), c = i + wantCol; if (c > le) c = le;
		if (kapi_get_modifiers () & MOD_SHIFT) { if (anchor < 0) anchor = caret; }	// Shift+click: extend
		else anchor = c;						// a drag selects from here
		caret = c;
		ensureVisible ((height - 4) / fh, (width - 2 * pad - WK_SBW) / fw);
		invalidate (true);
	}
	else if (bl && pressed && !barDrag)			// drag: extend the selection
	{
		const int pad = 4, fw = wk_fw ();
		int row = (my - 2) / fh, col = (mx - pad) / fw;
		if (row < 0) { row = 0; if (top > 0) top--; }
		if (row >= rows) { row = rows - 1; if (top < mt) top++; }
		if (col < 0) col = 0;
		int wantLine = top + row, wantCol = left + col, i = 0, line = 0;
		while (line < wantLine && buf[i]) { if (buf[i] == '\n') line++; i++; }
		int le = lineEnd (i), c = i + wantCol; if (c > le) c = le;
		if (c != caret) { caret = c; ensureVisible ((height - 4) / fh, (width - 2 * pad - WK_SBW) / fw); invalidate (true); }
	}
	else if (!bl) { pressed = false; barDrag = false; if (anchor == caret) anchor = -1; }
	return true;
}

void Textarea::moveCaret (long k)
{
	int vr = (height - 4) / wk_fh (); if (vr < 1) vr = 1;
	switch (k)
	{
	case KEY_LEFT:  if (caret > 0) caret--; break;
	case KEY_RIGHT: if (caret < len) caret++; break;
	case KEY_HOME:  caret = lineStart (caret); break;
	case KEY_END:   caret = lineEnd (caret); break;
	case KEY_UP: case KEY_PGUP:
		for (int n = k == KEY_UP ? 1 : vr; n > 0; n--)
		{
			int ls = lineStart (caret), col = caret - ls;
			if (ls == 0) { if (k == KEY_PGUP) caret = 0; break; }
			int pls = lineStart (ls - 1), ple = ls - 1, c = pls + col; caret = c > ple ? ple : c;
		}
		break;
	case KEY_DOWN: case KEY_PGDN:
		for (int n = k == KEY_DOWN ? 1 : vr; n > 0; n--)
		{
			int ls = lineStart (caret), col = caret - ls, le = lineEnd (caret);
			if (buf[le] != '\n') { if (k == KEY_PGDN) caret = len; break; }
			int nls = le + 1, nle = lineEnd (nls), c = nls + col; caret = c > nle ? nle : c;
		}
		break;
	}
}

bool Textarea::onKey (long k)
{
	bool nav = k == KEY_LEFT || k == KEY_RIGHT || k == KEY_UP || k == KEY_DOWN || k == KEY_HOME || k == KEY_END
		   || k == KEY_PGUP || k == KEY_PGDN;
	if (nav)
	{
		bool shift = (kapi_get_modifiers () & MOD_SHIFT) != 0;
		if (shift) { if (anchor < 0) anchor = caret; }
		else if (hasSelection () && (k == KEY_LEFT || k == KEY_RIGHT))	// collapse to that side
		{ caret = k == KEY_LEFT ? selStart () : selEnd (); anchor = -1; k = 0; }
		else anchor = -1;
		if (k) moveCaret (k);
	}
	else if (k == WK_CTRL ('A')) selectAll ();
	else if (k == WK_CTRL ('C')) copy ();
	else if (k == WK_CTRL ('X')) cut ();
	else if (k == WK_CTRL ('V')) paste ();
	else if ((k >= 32 && k <= 126) || (k >= 0xA0 && k <= 0xFF)) { if (!readonly) { deleteSelection (); insertAt ((int) k); } }
	else if (k == KEY_ENTER)     { if (!readonly) { deleteSelection (); insertAt ('\n'); } }
	else if (k == KEY_TAB)       { if (!readonly) { deleteSelection (); insertAt (' '); insertAt (' '); } }
	else if (k == KEY_BACKSPACE) { if (!readonly) { if (hasSelection ()) deleteSelection (); else if (caret > 0) { deleteAt (caret - 1); caret--; } } }
	else if (k == KEY_DEL)       { if (!readonly) { if (hasSelection ()) deleteSelection (); else deleteAt (caret); } }
	else return false;
	if (!nav && k != WK_CTRL ('A') && k != WK_CTRL ('C')) { if (!hasSelection ()) anchor = -1; }
	ensureVisible ((height - 4) / wk_fh (), (width - 8 - WK_SBW) / wk_fw ());
	invalidate (true); return true;
}

} // namespace wtk
