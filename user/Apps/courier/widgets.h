//
// widgets.h -- Courier's controls, drawn with uikit's painter and the theme's colours:
//   LineEdit   a line of text of any length ({{variables}} shown as pills: known / unknown, their
//              value in a tooltip), selection, clipboard
//   CodeEdit   a code editor / viewer (the mono face): line numbers, JSON / XML / HTML / JavaScript
//              colours, selection, clipboard, undo, auto-indent, both scroll bars
//   KVTable    an editable table of key / value (/ description) rows with their check boxes, a blank
//              row always last, a Bulk Edit mode (key:value lines); read-only for a response's headers
//   TabBar     text tabs underlined in the accent (with counts); RadioRow; Btn (primary, secondary,
//              ghost); MethodPicker; DocTabs (the open requests); Spinner
//
#ifndef _courier_widgets_h
#define _courier_widgets_h

#include "uikit/uikit.h"
#include "uikit/toolbar.h"
#include "systemkit/systemkit.h"
#include "vars.h"

namespace cr {

using namespace uikit;

// ---- shared state: the faces, the variables' scope ----------------------------------------------------------------
static TextFace *g_sans = 0;			// the UI's text (FreeType's DejaVu Sans, else the bitmap font)
static TextFace *g_mono = 0;			// the code's (DejaVu Sans Mono), 0: the UI's
static Scope g_scope;				// the variables the open tab sees

// ---- colours --------------------------------------------------------------------------------------------------------
static inline bool dark_field () { return uk_bright (C_FIELD) < 110; }
static inline unsigned c_dim () { return uk_mix (C_FIELD, C_FIELD_TEXT, 150); }		// secondary text on a field
static inline unsigned c_faint () { return uk_mix (C_FIELD, C_FIELD_TEXT, 90); }	// placeholders
static inline unsigned c_line () { return uk_mix (C_FIELD, C_FIELD_TEXT, 36); }		// rules on a field
static inline unsigned c_bgline () { return uk_mix (C_BG, C_TEXT, 44); }		// rules on the face
static inline unsigned c_hover () { return uk_mix (C_FIELD, C_ACCENT, 22); }		// a row pointed
static inline unsigned c_sel () { return uk_mix (C_FIELD, C_ACCENT, 70); }		// a text selection
static inline unsigned c_panel () { return uk_mix (C_BG, C_FIELD, 150); }		// a light panel (the sidebar)
// a colour readable on the field (lightened when the field is dark)
static inline unsigned on_field (unsigned c) { return dark_field () ? uk_mix (c, 0xFFFFFF, 110) : c; }
static inline unsigned method_color (const char *m)
{
	static const unsigned C[M_COUNT] = { 0x00007F31, 0x00AD7A03, 0x000053B8, 0x00623497, 0x008E1A10, 0x00007F31, 0x00A61468 };
	return on_field (C[method_index (m)]);
}
static const unsigned C_OK = 0x00007F31, C_WARN = 0x00AD7A03, C_BAD = 0x00C0392B, C_VAR = 0x00C25E00;
static inline unsigned status_color (int s)
{
	if (s < 0) return on_field (C_BAD);
	if (s < 300) return on_field (C_OK);
	if (s < 400) return on_field (0x000053B8);
	if (s < 500) return on_field (C_WARN);
	return on_field (C_BAD);
}

// ---- text ----------------------------------------------------------------------------------------------------------
static inline int tw (const char *s, int style = 0) { return uk_tw (s, style); }
static inline void text_at (Canvas &cv, int x, int y, const char *s, unsigned c, int style = 0) { uk_text (cv, x, y, s, c, style); }
// in a box of height h, vertically centred
static inline void text_v (Canvas &cv, int x, int y, int h, const char *s, unsigned c, int style = 0) { uk_text (cv, x, y + (h - uk_fh ()) / 2, s, c, style); }
// cut to w px with "..."
static inline void text_fit (Canvas &cv, int x, int y, int h, int w, const char *s, unsigned c, int style = 0)
{
	if (w <= 4) return;
	char b[512]; uk_text_fit (s, w, b, sizeof b, style);
	text_v (cv, x, y, h, b, c, style);
}
// n bytes of s at (x, y), {{variables}} as pills (their scope's colour); the canvas clips
static inline void draw_rich (Canvas &cv, int x, int y, const char *s, int n, unsigned ink, bool vars, int style = 0)
{
	if (n <= 0) return;
	Str seg;
	int i = 0, px = x;
	while (i < n)
	{
		int ns, nl, end, j = i;
		bool found = false;
		if (vars)
			for (; j < n; j++) if (s[j] == '{' && var_at (s, n, j, &ns, &nl, &end)) { found = true; break; }
		if (!found) j = n;
		if (j > i)
		{
			seg.set (s + i, j - i);
			if (px < cv.w) uk_text (cv, px, y, seg.c (), ink, style);
			px += uk_tw_n (s + i, j - i, style);
		}
		if (!found) break;
		seg.set (s + j, end - j);
		int w = uk_tw (seg.c (), style);
		bool known = lookup_var (g_scope, s + ns, nl, 0) != VS_NONE;
		unsigned vc = on_field (known ? C_VAR : C_BAD);
		if (px < cv.w && px + w > 0)
		{
			uk_rbox (cv, px - 1, y, w + 2, uk_fh (), 3, uk_mix (C_FIELD, vc, 34), uk_mix (C_FIELD, vc, 34));
			uk_text (cv, px, y, seg.c (), vc, style);
		}
		px += w;
		i = end;
	}
}
// the {{variable}} of s at byte i (a tooltip: "name = value  (environment)")
static inline bool var_tip (Str &tip, const char *s, int n, int i)
{
	for (int j = imax (0, i - 200); j <= i && j < n; j++)
	{
		int ns, nl, end;
		if (s[j] == '{' && var_at (s, n, j, &ns, &nl, &end) && i < end)
		{
			Str v; int sc = lookup_var (g_scope, s + ns, nl, &v);
			tip.clear (); tip.add (s + ns, nl);
			if (sc == VS_NONE) tip.add (": not defined (in no environment, collection or globals)");
			else if (sc == VS_DYNAMIC) tip.add (": made when the request is sent");
			else
			{
				Str shown; shown.set (v.c (), imin (v.len (), 80)); if (v.len () > 80) shown.add ("...");
				tip.add (" = "); tip.add (shown.c ()); tip.add ("   ("); tip.add (VS_NAMES[sc]); tip.add (")");
			}
			return true;
		}
	}
	return false;
}

// ---- a caret / selection in a UTF-8 line ------------------------------------------------------------------------------
static inline bool word_char (char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || (unsigned char) c >= 0x80; }
static inline bool ctrl_held () { return (kapi_get_modifiers () & MOD_CTRL) != 0; }
static inline bool shift_held () { return (kapi_get_modifiers () & MOD_SHIFT) != 0; }

// the byte of s[0..n) whose caret is nearest x px (a binary search on the prefixes' widths)
static inline int pos_at_x (const char *s, int n, int x, int style = 0)
{
	if (x <= 0 || n <= 0) return 0;
	if (!uk_textface ()) return uk_tpos (s, n, x, style);
	int lo = 0, hi = n;				// the widest prefix <= x
	while (lo < hi)
	{
		int mid = (lo + hi + 1) / 2;
		while (mid > lo && ((unsigned char) s[mid] & 0xC0) == 0x80) mid--;
		if (mid <= lo) { mid = uk_u8_next (s, lo, n); if (mid > hi) break; }
		if (uk_tw_n (s, mid, style) <= x) lo = mid; else hi = mid - 1;
		while (hi > lo && hi < n && ((unsigned char) s[hi] & 0xC0) == 0x80) hi--;
	}
	int nx = uk_u8_next (s, lo, n);
	if (nx > lo && nx <= n)
	{
		int a = uk_tw_n (s, lo, style), b = uk_tw_n (s, nx, style);
		if (x - a > b - x) return nx;
	}
	return lo;
}

// ================================================================================================================
// LineEdit
// ================================================================================================================
class LineEdit : public Widget
{
public:
	Str text;
	int caret, anchor;			// selection = [anchor, caret) either way; -1 none
	bool vars;				// {{variables}} as pills
	bool framed;				// a sunken field (else flat: a table's cell)
	bool readonly, password;
	int style;				// the text's style (1 italic, 2 bold)
	unsigned ink;				// 0: the field's text colour
	const char *placeholder;
	TextFace *face;				// its own face (a title's), 0: the UI's
	bool hoverFrame;			// flat: an outline when pointed (an editable title)
	Action onChange, onEnter, onBlur;
	bool (*onPaste) (LineEdit &, const char *);	// true: taken (a cURL command pasted in the URL)
	bool (*onKeyHook) (LineEdit &, long);	// a key before the field (Tab, Up / Down in a table): true taken
	int padL, padR;

	LineEdit (int l, int t, int w, int h, const char *s = "")
		: Widget (l, t, w, h), text (s), caret (0), anchor (-1), vars (true), framed (true), readonly (false), password (false),
		  style (0), ink (0), placeholder (0), face (0), hoverFrame (false), onChange (0), onEnter (0), onBlur (0), onPaste (0), onKeyHook (0), padL (8), padR (8),
		  m_scroll (0), m_drag (false), m_lastClick (0), m_wasFocus (false)
	{ canFocus = true; caret = text.len (); }

	void setText (const char *s) { text.set (s ? s : ""); caret = imin (caret, text.len ()); anchor = -1; m_scroll = 0; invalidate (true); }
	void selectAll () { anchor = 0; caret = text.len (); invalidate (true); }
	bool hasSel () const { return anchor >= 0 && anchor != caret; }
	int selA () const { return hasSel () ? imin (anchor, caret) : caret; }
	int selB () const { return hasSel () ? imax (anchor, caret) : caret; }

	void onDraw () override
	{
		UkFaceScope fs (face);
		int fh = uk_fh ();
		bool focus = hasFocus && !disabled;
		if (!framed && !focus && hover && !readonly) { canvas.clear (bgColor ()); }
		if (focus != m_wasFocus) { if (!focus && onBlur) { m_wasFocus = focus; onBlur (*this); } m_wasFocus = focus; }
		canvas.clear (bgColor ());
		if (framed) uk_sunken (canvas, 0, 0, width, height, 5, disabled ? uk_tone (C_FACE, 150) : C_FIELD, focus);
		else if (focus) { canvas.clear (C_FIELD); uk_rline (canvas, 0, 0, width, height, 3, C_ACCENT, 200); }
		else if (hover && hoverFrame && !readonly) uk_rline (canvas, 0, 0, width, height, 3, c_line (), 255);
		int ty = (height - fh) / 2;
		int area = imax (1, width - padL - padR);
		const char *s = text.c (); int n = text.len ();
		Str masked;
		if (password) { for (int i = 0; i < n; i = uk_u8_next (s, i, n)) masked.add ('*'); }
		int cx = caretX ();
		// the scroll: the caret in view
		if (focus)
		{
			if (cx - m_scroll > area - 2) m_scroll = cx - area + 2;
			if (cx - m_scroll < 0) m_scroll = cx;
		}
		else m_scroll = 0;
		int full = uk_tw_n (password ? masked.c () : s, password ? masked.len () : n, style);
		if (m_scroll > imax (0, full - area + 2)) m_scroll = imax (0, full - area + 2);
		Canvas clip; clip.adopt (canvas.px + padL, area, height, canvas.stride);
		int x0 = -m_scroll;
		unsigned tc = disabled ? C_DIS : ink ? ink : C_FIELD_TEXT;
		if (n == 0 && placeholder && !focus) uk_text (clip, 0, ty, placeholder, c_faint (), 0);
		if (focus && hasSel ())
		{
			int a = xOf (selA ()), b = xOf (selB ());
			clip.fillRect (x0 + a, ty - 1, b - a, fh + 2, c_sel ());
		}
		if (password) uk_text (clip, x0, ty, masked.c (), tc, style);
		else draw_rich (clip, x0, ty, s, n, tc, vars, style);
		if (focus && !readonly) clip.fillRect (x0 + cx, ty - 1, 1, fh + 2, C_ACCENT);
		else if (focus) clip.fillRect (x0 + cx, ty - 1, 1, fh + 2, c_faint ());
		drawExtra ();
	}
	virtual void drawExtra () {}

	bool onMouse (int mx, int my, int bl, int, int, int wheel) override
	{
		UkFaceScope fs (face);
		if (mx < 0) { m_drag = false; pressed = false; if (hover) { hover = false; if (!framed) invalidate (true); } return false; }
		if (!hover) { hover = true; if (!framed) invalidate (true); }
		if (wheel) return false;
		// a {{variable}} under the pointer: its value as the tooltip
		if (!bl && vars)
		{
			int i = posAtMx (mx);
			if (var_tip (m_tip, text.c (), text.len (), imin (i, text.len () - 1)) || var_tip (m_tip, text.c (), text.len (), imax (0, i - 1)))
				tip = m_tip.c ();
			else tip = 0;
		}
		if (disabled) return true;
		if (bl && !pressed)
		{
			pressed = true;
			if (!hasFocus) setFocus ();
			int i = posAtMx (mx);
			unsigned now = kapi_get_ticks ();
			if (now - m_lastClick < 35 && i == caret)
			{
				// a double click: the word
				int a = i, b = i; const char *s = text.c ();
				while (a > 0 && word_char (s[a - 1])) a--;
				while (b < text.len () && word_char (s[b])) b++;
				anchor = a; caret = b;
				m_lastClick = 0;
			}
			else
			{
				if (shift_held ()) { if (anchor < 0) anchor = caret; } else anchor = i;
				caret = i; m_drag = true; m_lastClick = now;
			}
			invalidate (true);
		}
		else if (bl && m_drag)
		{
			int i = posAtMx (mx);
			if (i != caret) { caret = i; invalidate (true); }
		}
		else if (!bl) { pressed = false; m_drag = false; if (anchor == caret) anchor = -1; }
		return true;
	}

	bool onKey (long k) override
	{
		if (onKeyHook && onKeyHook (*this, k)) return true;
		const char *s = text.c (); int n = text.len ();
		bool shift = shift_held (), ctrl = ctrl_held ();
		if (k == 1) { selectAll (); return true; }				// ^A
		if (k == 3) { if (hasSel () && !password) clip_set_text_n (s + selA (), selB () - selA ()); return true; }	// ^C
		if (k == 24) { if (hasSel () && !readonly) { if (!password) clip_set_text_n (s + selA (), selB () - selA ()); delSel (); changed (); } return true; }
		if (k == 22)								// ^V
		{
			if (readonly) return true;
			char *b = (char *) malloc (256 * 1024);
			if (!b) return true;
			int len = clip_get_text (b, 256 * 1024);
			if (len > 0)
			{
				if (!(onPaste && onPaste (*this, b)))
				{
					for (int i = 0; i < len; i++) if (b[i] == '\r' || b[i] == '\n' || b[i] == '\t') b[i] = ' ';
					insert (b, len);
				}
			}
			free (b);
			return true;
		}
		if (k == KEY_ENTER) { if (onEnter) onEnter (*this); return onEnter != 0; }
		if (k == KEY_LEFT || k == KEY_RIGHT || k == KEY_HOME || k == KEY_END)
		{
			int c = caret;
			if (!shift && hasSel () && (k == KEY_LEFT || k == KEY_RIGHT)) c = k == KEY_LEFT ? selA () : selB ();
			else if (k == KEY_LEFT)
			{
				c = uk_u8_prev (s, c);
				if (ctrl) { while (c > 0 && !word_char (s[c])) c--; while (c > 0 && word_char (s[c - 1])) c--; }
			}
			else if (k == KEY_RIGHT)
			{
				c = uk_u8_next (s, c, n);
				if (ctrl) { while (c < n && word_char (s[c])) c++; while (c < n && !word_char (s[c])) c++; }
			}
			else if (k == KEY_HOME) c = 0;
			else c = n;
			if (shift) { if (anchor < 0) anchor = caret; } else anchor = -1;
			caret = c;
			invalidate (true);
			return true;
		}
		if (readonly) return false;
		if (k == KEY_BACKSPACE)
		{
			if (hasSel ()) delSel ();
			else if (caret > 0) { int p = uk_u8_prev (s, caret); if (ctrl) { while (p > 0 && !word_char (s[p])) p--; while (p > 0 && word_char (s[p - 1])) p--; } text.erase (p, caret - p); caret = p; }
			else return true;
			changed (); return true;
		}
		if (k == KEY_DEL)
		{
			if (hasSel ()) delSel ();
			else if (caret < n) { int e = uk_u8_next (s, caret, n); text.erase (caret, e - caret); }
			else return true;
			changed (); return true;
		}
		char u[4]; int un = uk_textface () ? uk_u8_key (k, u) : ((k >= 32 && k <= 126) || (k >= 0xA0 && k <= 0xFF) ? (u[0] = (char) k, 1) : 0);
		if (un > 0 && !ctrl) { insert (u, un); return true; }
		return false;
	}

	void insert (const char *s, int n)
	{
		if (readonly) return;
		if (hasSel ()) delSel ();
		text.insert (caret, s, n); caret += n; anchor = -1;
		changed ();
	}
	int caretX () { return xOf (caret); }
	int xOf (int i)
	{
		if (!password) return uk_tw_n (text.c (), i, style);
		int n = 0; for (int k = 0; k < i && k < text.len (); k = uk_u8_next (text.c (), k, text.len ())) n++;
		return n * uk_tw ("*", style);
	}
	int posAtMx (int mx)
	{
		int x = mx - padL + m_scroll;
		if (password) { int cw = imax (1, uk_tw ("*", style)), c = (x + cw / 2) / cw, k = 0; for (int i = 0; i < c && k < text.len (); i++) k = uk_u8_next (text.c (), k, text.len ()); return k; }
		return pos_at_x (text.c (), text.len (), x, style);
	}
private:
	int m_scroll; bool m_drag; unsigned m_lastClick; bool m_wasFocus;
	Str m_tip;
	void delSel () { int a = selA (), b = selB (); text.erase (a, b - a); caret = a; anchor = -1; }
	void changed () { invalidate (true); if (onChange) onChange (*this); }
};

// ================================================================================================================
// the code's colours: a token kind -> a colour
// ================================================================================================================
enum { TK_TEXT, TK_KEY, TK_STR, TK_NUM, TK_LIT, TK_PUNCT, TK_TAG, TK_ATTR, TK_COMMENT, TK_KEYWORD, TK_VAR, TK_BADVAR };
static inline unsigned tk_color (int k)
{
	static const unsigned C[] = { 0, 0x00A0226E, 0x000B7A3E, 0x00B5530B, 0x006A3FB5, 0, 0x000F5FB8, 0x00B5530B, 0x00808890, 0x000F5FB8, C_VAR, C_BAD };
	if (k == TK_TEXT) return C_FIELD_TEXT;
	if (k == TK_PUNCT) return uk_mix (C_FIELD, C_FIELD_TEXT, 170);
	return on_field (C[k]);
}
enum { LANG_TEXT, LANG_JSON, LANG_XML, LANG_HTML, LANG_JS };

// ================================================================================================================
// CodeEdit
// ================================================================================================================
class CodeEdit : public Widget
{
public:
	bool readonly, lineNumbers, vars, wrapHint;
	int lang;
	Action onChange;
	const char *placeholder;

	CodeEdit (int l, int t, int w, int h)
		: Widget (l, t, w, h), readonly (false), lineNumbers (true), vars (false), wrapHint (false), lang (LANG_TEXT), onChange (0), placeholder (0),
		  m_caret (0), m_anchor (-1), m_top (0), m_left (0), m_goalCol (-1), m_drag (0), m_lastClick (0), m_lastEdit (0), m_editKind (0), m_maxCols (0)
	{ canFocus = true; relines (); }

	const char *text () const { return m_text.c (); }
	int length () const { return m_text.len (); }
	void setText (const char *s, int n = -1)
	{
		m_text.set (s ? s : "", n);
		m_caret = 0; m_anchor = -1; m_top = 0; m_left = 0;
		m_undo.clear (); m_redo.clear ();
		relines (); invalidate (true);
	}
	// (taken over: a big response without a copy)
	void adopt (Str &s) { m_text = s; m_caret = 0; m_anchor = -1; m_top = 0; m_left = 0; m_undo.clear (); m_redo.clear (); relines (); invalidate (true); }
	void selectAll () { m_anchor = 0; m_caret = m_text.len (); invalidate (true); }
	void copy () { if (hasSel ()) clip_set_text_n (m_text.c () + selA (), selB () - selA ()); else clip_set_text_n (m_text.c (), m_text.len ()); }
	int lines () const { return m_lines.size (); }
	// find `w` from the caret on (no case), selected and shown; false: not found
	bool find (const char *w, bool fromStart = false)
	{
		int start = fromStart ? 0 : (hasSel () ? selB () : m_caret);
		int at = s_findi (m_text.c () + start, m_text.len () - start, w);
		if (at < 0 && start > 0) { start = 0; at = s_findi (m_text.c (), m_text.len (), w); }
		if (at < 0) return false;
		m_anchor = start + at; m_caret = start + at + (int) strlen (w);
		ensureCaret (true); invalidate (true);
		return true;
	}

	// ---- geometry --------------------------------------------------------------------------------------------------
	int cw () { UkFaceScope fs (g_mono); int w = uk_tw ("M"); return w < 1 ? 8 : w; }
	int lh () { UkFaceScope fs (g_mono); return uk_fh () + 3; }
	int gutter () { if (!lineNumbers) return 8; int d = 1, n = m_lines.size (); while (n >= 10) { n /= 10; d++; } return imax (2, d) * cw () + 20; }
	int viewRows () { return imax (1, (height - 8 - (hbarShown () ? UK_SBW : 0)) / lh ()); }
	bool vbarShown () { return m_lines.size () > (height - 8) / lh (); }
	bool hbarShown () { return (m_maxCols + 2) * cw () > width - gutter () - (vbarShown () ? UK_SBW : 0); }
	int textW () { return width - gutter () - (vbarShown () ? UK_SBW : 0); }

	void onDraw () override
	{
		UkFaceScope fs (g_mono);
		int fh = uk_fh (), LH = lh (), CW = cw (), G = gutter ();
		bool focus = hasFocus && !disabled;
		canvas.clear (C_FIELD);
		int rows = viewRows ();
		clampScroll ();
		int tw_ = textW ();
		unsigned gutBg = uk_mix (C_FIELD, C_FIELD_TEXT, 10);
		if (lineNumbers) { canvas.fillRect (0, 0, G - 8, height, gutBg); canvas.fillRect (G - 8, 0, 1, height, c_line ()); }
		Canvas clip; clip.adopt (canvas.px + G, imax (1, tw_), height, canvas.stride);
		if (m_text.len () == 0 && placeholder && !focus) uk_text (clip, 2, 4, placeholder, c_faint (), 1);
		int caretLine = lineOf (m_caret);
		int sa = selA (), sb = selB ();
		for (int r = 0; r < rows + 1; r++)
		{
			int ln = m_top + r;
			if (ln >= m_lines.size ()) break;
			int y = 4 + r * LH;
			int ls = m_lines[ln], le = lineEnd (ln);
			if (focus && !readonly && ln == caretLine && !hasSel ()) clip.fillRect (0, y, tw_, LH, uk_mix (C_FIELD, C_ACCENT, 12));
			if (lineNumbers)
			{
				char b[16]; snprintf (b, sizeof b, "%d", ln + 1);
				uk_text (canvas, G - 12 - uk_tw (b), y + 1, b, ln == caretLine && focus ? C_FIELD_TEXT : c_faint ());
			}
			// the selection on this line
			if (hasSel () && sb > ls && sa <= le)
			{
				int a = imax (sa, ls), b = imin (sb, le);
				int xa = colOf (ls, a) * CW - m_left, xb = colOf (ls, b) * CW - m_left + (sb > le ? CW / 2 : 0);
				clip.fillRect (xa, y, imax (2, xb - xa), LH, c_sel ());
			}
			drawLine (clip, ls, le, y + (LH - fh) / 2, CW);
			if (focus && ln == caretLine)
			{
				int cx = colOf (ls, m_caret) * CW - m_left;
				clip.fillRect (cx, y, readonly ? 1 : 2, LH, readonly ? c_faint () : C_ACCENT);
			}
		}
		// the scroll bars
		if (vbarShown ())
		{
			int total = m_lines.size (), trackH = height - (hbarShown () ? UK_SBW : 0);
			UkThumb t = uk_thumb (total, rows, m_top, trackH);
			uk_draw_vscroll (canvas, width - UK_SBW, 0, UK_SBW, trackH, t, C_FIELD, m_drag == 2);
		}
		if (hbarShown ())
		{
			int total = (m_maxCols + 2) * CW, trackW = tw_;
			UkThumb t = uk_thumb (total, tw_, m_left, trackW);
			uk_scroll_bar (canvas, G, height - UK_SBW, trackW, UK_SBW, false, t.y, t.h < trackW ? t.h : 0, C_FIELD, m_drag == 3 ? UK_HOT : UK_NORMAL);
		}
		if (framed_) uk_rline (canvas, 0, 0, width, height, 0, focus ? C_ACCENT : c_line ());
	}
	bool framed_ = false;

	bool onMouse (int mx, int my, int bl, int, int, int wheel) override
	{
		if (mx < 0) { if (!bl) m_drag = 0; return false; }
		if (wheel)
		{
			if (shift_held ()) { m_left = imax (0, m_left + wheel * cw () * 4); }
			else m_top += wheel * 3;
			clampScroll (); invalidate (true);
			return true;
		}
		int G = gutter ();
		bool vb = vbarShown (), hb = hbarShown ();
		if (bl && !pressed)
		{
			pressed = true;
			if (!hasFocus) setFocus ();
			if (vb && mx >= width - UK_SBW) { m_drag = 2; dragV (my); return true; }
			if (hb && my >= height - UK_SBW && mx >= G) { m_drag = 3; dragH (mx); return true; }
			int p = posAt (mx, my);
			unsigned now = kapi_get_ticks ();
			if (now - m_lastClick < 35 && p == m_caret)
			{
				int a = p, b = p; const char *s = m_text.c ();
				while (a > 0 && word_char (s[a - 1])) a--;
				while (b < m_text.len () && word_char (s[b])) b++;
				m_anchor = a; m_caret = b; m_lastClick = 0;
			}
			else
			{
				if (shift_held ()) { if (m_anchor < 0) m_anchor = m_caret; } else m_anchor = p;
				m_caret = p; m_drag = 1; m_lastClick = now;
			}
			m_goalCol = -1;
			invalidate (true);
		}
		else if (bl && m_drag == 1) { int p = posAt (mx, my); if (p != m_caret) { m_caret = p; ensureCaret (false); invalidate (true); } }
		else if (bl && m_drag == 2) dragV (my);
		else if (bl && m_drag == 3) dragH (mx);
		else if (!bl) { pressed = false; m_drag = 0; if (m_anchor == m_caret) m_anchor = -1; }
		return true;
	}

	bool onKey (long k) override
	{
		bool shift = shift_held (), ctrl = ctrl_held ();
		const char *s = m_text.c ();
		if (k == 1) { selectAll (); return true; }
		if (k == 3) { copy (); return true; }
		if (k == 24) { if (!readonly && hasSel ()) { snap (3); copy (); delSel (); changed (); } return true; }
		if (k == 22)
		{
			if (readonly) return true;
			char *b = (char *) malloc (1 << 20);
			if (!b) return true;
			int n = clip_get_text (b, 1 << 20);
			if (n > 0)
			{
				int w = 0; for (int i = 0; i < n; i++) if (b[i] != '\r') b[w++] = b[i];		// (CRLF -> LF)
				snap (3); insert (b, w);
			}
			free (b);
			return true;
		}
		if (k == 26) { undo (); return true; }					// ^Z
		if (k == 25) { redo (); return true; }					// ^Y
		int nav = -1;
		if (k == KEY_LEFT || k == KEY_RIGHT || k == KEY_UP || k == KEY_DOWN || k == KEY_HOME || k == KEY_END || k == KEY_PGUP || k == KEY_PGDN) nav = (int) k;
		if (nav >= 0)
		{
			int c = m_caret, ln = lineOf (c), ls = m_lines[ln], le = lineEnd (ln), n = m_text.len ();
			if (!shift && hasSel () && (k == KEY_LEFT || k == KEY_RIGHT)) c = k == KEY_LEFT ? selA () : selB ();
			else switch (k)
			{
			case KEY_LEFT: c = c > 0 ? uk_u8_prev (s, c) : 0; if (ctrl) { while (c > 0 && !word_char (s[c])) c--; while (c > 0 && word_char (s[c - 1])) c--; } break;
			case KEY_RIGHT: c = uk_u8_next (s, c, n); if (ctrl) { while (c < n && word_char (s[c])) c++; while (c < n && !word_char (s[c]) && s[c] != '\n') c++; } break;
			case KEY_HOME:
				if (ctrl) c = 0;
				else { int f = ls; while (f < le && (s[f] == ' ' || s[f] == '\t')) f++; c = c == f ? ls : f; }
				break;
			case KEY_END: c = ctrl ? n : le; break;
			case KEY_UP: case KEY_DOWN: case KEY_PGUP: case KEY_PGDN:
			{
				int d = k == KEY_UP ? -1 : k == KEY_DOWN ? 1 : k == KEY_PGUP ? -viewRows () : viewRows ();
				if (m_goalCol < 0) m_goalCol = colOf (ls, c);
				int nl = iclamp (ln + d, 0, m_lines.size () - 1);
				if (k == KEY_PGUP || k == KEY_PGDN) m_top = iclamp (m_top + d, 0, imax (0, m_lines.size () - viewRows ()));
				c = atCol (nl, m_goalCol);
				if (nl == ln && d < 0) c = 0;
				if (nl == ln && d > 0) c = n;
				break;
			}
			}
			if (k != KEY_UP && k != KEY_DOWN && k != KEY_PGUP && k != KEY_PGDN) m_goalCol = -1;
			if (shift) { if (m_anchor < 0) m_anchor = m_caret; } else m_anchor = -1;
			m_caret = c;
			ensureCaret (false); invalidate (true);
			return true;
		}
		if (readonly) return false;
		m_goalCol = -1;
		if (k == KEY_BACKSPACE)
		{
			snap (2);
			if (hasSel ()) delSel ();
			else if (m_caret > 0)
			{
				int p = uk_u8_prev (s, m_caret);
				// (an indent of spaces: back to the previous stop)
				int ls = m_lines[lineOf (m_caret)];
				bool allSp = true; for (int i = ls; i < m_caret; i++) if (s[i] != ' ') allSp = false;
				if (allSp && m_caret - ls >= 2 && !ctrl) p = m_caret - ((m_caret - ls) % 2 ? 1 : 2);
				m_text.erase (p, m_caret - p); m_caret = p;
			}
			else return true;
			changed (); return true;
		}
		if (k == KEY_DEL)
		{
			snap (2);
			if (hasSel ()) delSel ();
			else if (m_caret < m_text.len ()) { int e = uk_u8_next (s, m_caret, m_text.len ()); m_text.erase (m_caret, e - m_caret); }
			else return true;
			changed (); return true;
		}
		if (k == KEY_ENTER)
		{
			snap (3);
			// the line's indent, one more after { or [ (and the closing one on its own line)
			int ls = m_lines[lineOf (m_caret)];
			Str ind ("\n");
			for (int i = ls; i < m_caret && (s[i] == ' ' || s[i] == '\t'); i++) ind.add (s[i]);
			char before = 0; for (int i = m_caret - 1; i >= ls; i--) if (s[i] != ' ') { before = s[i]; break; }
			char after = m_caret < m_text.len () ? s[m_caret] : 0;
			if (before == '{' || before == '[' || (before == '>' && lang >= LANG_XML && lang <= LANG_HTML && m_caret >= 2 && s[m_caret - 2] != '/'))
			{
				Str more (ind.c ()); more.add ("  ");
				if ((before == '{' && after == '}') || (before == '[' && after == ']'))
				{ Str both (more.c ()); both.add (ind.c ()); insert (both.c (), both.len ()); m_caret -= ind.len (); invalidate (true); return true; }
				insert (more.c (), more.len ());
				return true;
			}
			insert (ind.c (), ind.len ());
			return true;
		}
		if (k == KEY_TAB)
		{
			snap (3);
			if (shift)
			{
				// the line's indent: 2 fewer
				int ls = m_lines[lineOf (m_caret)], n = 0;
				while (n < 2 && s[ls + n] == ' ') n++;
				if (n) { m_text.erase (ls, n); m_caret = imax (ls, m_caret - n); changed (); }
				return true;
			}
			insert ("  ", 2); return true;
		}
		char u[4]; int un = uk_u8_key (k, u);
		if (un > 0 && !ctrl)
		{
			snap (1);
			// a closing bracket on a line of spaces: one indent less
			if ((u[0] == '}' || u[0] == ']') && !hasSel ())
			{
				int ls = m_lines[lineOf (m_caret)];
				bool allSp = m_caret - ls >= 2; for (int i = ls; i < m_caret; i++) if (s[i] != ' ') allSp = false;
				if (allSp) { m_text.erase (m_caret - 2, 2); m_caret -= 2; }
			}
			insert (u, un);
			return true;
		}
		return false;
	}

	void insert (const char *s, int n)
	{
		if (hasSel ()) delSel ();
		m_text.insert (m_caret, s, n); m_caret += n; m_anchor = -1;
		changed ();
	}
	void undo ()
	{
		if (!m_undo.size ()) return;
		Snap now; now.text = m_text; now.caret = m_caret; m_redo.push (now);
		Snap &u = m_undo.last (); m_text = u.text; m_caret = u.caret; m_undo.remove (m_undo.size () - 1);
		m_anchor = -1; m_editKind = 0; relines (); ensureCaret (false); invalidate (true);
		if (onChange) onChange (*this);
	}
	void redo ()
	{
		if (!m_redo.size ()) return;
		Snap now; now.text = m_text; now.caret = m_caret; m_undo.push (now);
		Snap &u = m_redo.last (); m_text = u.text; m_caret = u.caret; m_redo.remove (m_redo.size () - 1);
		m_anchor = -1; m_editKind = 0; relines (); ensureCaret (false); invalidate (true);
		if (onChange) onChange (*this);
	}
	void resizeTo (int w, int h) override { Widget::resizeTo (w, h); clampScroll (); }

private:
	struct Snap { Str text; int caret; };
	Str m_text;
	Vec<int> m_lines;
	int m_caret, m_anchor, m_top, m_left, m_goalCol, m_drag;
	unsigned m_lastClick, m_lastEdit; int m_editKind;
	int m_maxCols;
	Vec<Snap> m_undo, m_redo;

	bool hasSel () const { return m_anchor >= 0 && m_anchor != m_caret; }
	int selA () const { return hasSel () ? imin (m_anchor, m_caret) : m_caret; }
	int selB () const { return hasSel () ? imax (m_anchor, m_caret) : m_caret; }
	void delSel () { int a = selA (), b = selB (); m_text.erase (a, b - a); m_caret = a; m_anchor = -1; }
	void changed () { relines (); ensureCaret (false); invalidate (true); if (onChange) onChange (*this); }
	// an undo step before an edit: kind 1 typing, 2 deleting, 3 a paste / a new line (always a step)
	void snap (int kind)
	{
		unsigned now = kapi_get_ticks ();
		if (kind == m_editKind && kind != 3 && now - m_lastEdit < 100) { m_lastEdit = now; return; }
		m_editKind = kind; m_lastEdit = now;
		Snap s; s.text = m_text; s.caret = m_caret;
		m_undo.push (s);
		if (m_undo.size () > 60) m_undo.remove (0);
		m_redo.clear ();
	}
	void relines ()
	{
		m_lines.clear (); m_lines.push (0);
		const char *s = m_text.c (); int n = m_text.len (), maxc = 0, col = 0;
		for (int i = 0; i < n; i++)
		{
			if (s[i] == '\n') { m_lines.push (i + 1); if (col > maxc) maxc = col; col = 0; }
			else if (s[i] == '\t') col = (col / 4 + 1) * 4;
			else if (((unsigned char) s[i] & 0xC0) != 0x80) col++;
		}
		if (col > maxc) maxc = col;
		m_maxCols = maxc;
		if (m_caret > n) m_caret = n;
	}
	int lineEnd (int ln) const { return ln + 1 < m_lines.size () ? m_lines[ln + 1] - 1 : m_text.len (); }
	int lineOf (int p) const
	{
		int lo = 0, hi = m_lines.size () - 1;
		while (lo < hi) { int mid = (lo + hi + 1) / 2; if (m_lines[mid] <= p) lo = mid; else hi = mid - 1; }
		return lo;
	}
	int colOf (int ls, int p) const
	{
		const char *s = m_text.c (); int col = 0;
		for (int i = ls; i < p; i++)
		{
			if (s[i] == '\t') col = (col / 4 + 1) * 4;
			else if (((unsigned char) s[i] & 0xC0) != 0x80) col++;
		}
		return col;
	}
	int atCol (int ln, int col) const
	{
		const char *s = m_text.c (); int ls = m_lines[ln], le = lineEnd (ln), c = 0, i = ls;
		while (i < le)
		{
			int nc = s[i] == '\t' ? (c / 4 + 1) * 4 : c + 1;
			if (nc > col) { if (col - c > nc - col) i = uk_u8_next (s, i, le); break; }
			c = nc; i = uk_u8_next (s, i, le);
		}
		return i;
	}
	int posAt (int mx, int my)
	{
		int ln = iclamp (m_top + (my - 4) / lh (), 0, m_lines.size () - 1);
		int x = mx - gutter () + m_left;
		int CW = cw ();
		return atCol (ln, imax (0, (x + CW / 2) / CW));
	}
	void clampScroll ()
	{
		m_top = iclamp (m_top, 0, imax (0, m_lines.size () - viewRows ()));
		int maxLeft = imax (0, (m_maxCols + 2) * cw () - textW ());
		m_left = iclamp (m_left, 0, maxLeft);
	}
	void ensureCaret (bool centre)
	{
		int ln = lineOf (m_caret), rows = viewRows ();
		if (ln < m_top) m_top = centre ? imax (0, ln - rows / 2) : ln;
		if (ln >= m_top + rows) m_top = centre ? ln - rows / 2 : ln - rows + 1;
		int CW = cw (), x = colOf (m_lines[ln], m_caret) * CW, w = textW ();
		if (x < m_left) m_left = imax (0, x - CW * 4);
		if (x > m_left + w - CW * 2) m_left = x - w + CW * 6;
		clampScroll ();
	}
	void dragV (int my)
	{
		int trackH = height - (hbarShown () ? UK_SBW : 0), rows = viewRows ();
		UkThumb t = uk_thumb (m_lines.size (), rows, m_top, trackH);
		m_top = (int) uk_thumb_pos (my, trackH, m_lines.size (), rows, t.h);
		clampScroll (); invalidate (true);
	}
	void dragH (int mx)
	{
		int G = gutter (), tw_ = textW (), total = (m_maxCols + 2) * cw ();
		UkThumb t = uk_thumb (total, tw_, m_left, tw_);
		m_left = (int) uk_thumb_pos (mx - G, tw_, total, tw_, t.h);
		clampScroll (); invalidate (true);
	}

	// ---- a line's colours --------------------------------------------------------------------------------------------
	// a run of the line [a, b) in colour kind k, from column *col (tabs expanded)
	void run (Canvas &cv, int a, int b, int k, int y, int CW, int *col)
	{
		const char *s = m_text.c ();
		unsigned c = tk_color (k);
		int i = a;
		while (i < b)
		{
			int j = i; while (j < b && s[j] != '\t') j++;
			if (j > i)
			{
				int x = *col * CW - m_left;
				// the characters shown only (a long minified line)
				int skip = 0;
				if (x < -CW) { skip = (-x) / CW - 1; }
				int p = i, cc = 0;
				while (p < j && cc < skip) { p = uk_u8_next (s, p, j); cc++; }
				int ncol = 0; for (int q = i; q < j; q++) if (((unsigned char) s[q] & 0xC0) != 0x80) ncol++;
				int xs = (*col + cc) * CW - m_left;
				if (xs < cv.w && p < j)
				{
					int e = p, shown = 0, room = (cv.w - xs) / CW + 2;
					while (e < j && shown < room) { e = uk_u8_next (s, e, j); shown++; }
					if (k == TK_VAR || k == TK_BADVAR)
					{
						int w = shown * CW;
						uk_rbox (cv, xs - 1, y, w + 2, uk_fh (), 3, uk_mix (C_FIELD, c, 34), uk_mix (C_FIELD, c, 34));
					}
					// a glyph a column (the face's advance is not a whole number of pixels)
					char g[5]; int gx = xs;
					for (int q = p; q < e; )
					{
						int l = uk_u8_len (s + q, e - q);
						if (s[q] != ' ') { for (int z = 0; z < l; z++) g[z] = s[q + z]; g[l] = 0; uk_text (cv, gx, y, g, c); }
						q += l; gx += CW;
					}
				}
				*col += ncol;
			}
			if (j < b) { *col = (*col / 4 + 1) * 4; j++; }
			i = j;
		}
	}
	void drawLine (Canvas &cv, int ls, int le, int y, int CW)
	{
		const char *s = m_text.c ();
		int col = 0, i = ls;
		int nstart = 0, nlen = 0, nend = 0;
		while (i < le)
		{
			// {{variables}} first (a body being edited)
			if (vars && s[i] == '{' && var_at (s + ls, le - ls, i - ls, &nstart, &nlen, &nend))
			{
				int e = ls + nend;
				bool known = lookup_var (g_scope, s + ls + nstart, nlen, 0) != VS_NONE;
				run (cv, i, e, known ? TK_VAR : TK_BADVAR, y, CW, &col);
				i = e; continue;
			}
			int k = TK_TEXT, e = i + 1;
			char c = s[i];
			if (lang == LANG_JSON || lang == LANG_JS)
			{
				if (c == '"' || (c == '\'' && lang == LANG_JS) || (c == '`' && lang == LANG_JS))
				{
					e = i + 1; while (e < le && s[e] != c) { if (s[e] == '\\') e++; e++; }
					if (e < le) e++;
					k = TK_STR;
					if (lang == LANG_JSON) { int q = e; while (q < le && s[q] == ' ') q++; if (q < le && s[q] == ':') k = TK_KEY; }
				}
				else if (lang == LANG_JS && c == '/' && i + 1 < le && s[i + 1] == '/') { e = le; k = TK_COMMENT; }
				else if ((c >= '0' && c <= '9') || (c == '-' && i + 1 < le && s[i + 1] >= '0' && s[i + 1] <= '9'))
				{ e = i + 1; while (e < le && ((s[e] >= '0' && s[e] <= '9') || s[e] == '.' || s[e] == 'e' || s[e] == 'E' || s[e] == '+' || s[e] == '-')) e++; k = TK_NUM; }
				else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$')
				{
					e = i + 1; while (e < le && (word_char (s[e]) || s[e] == '$') && s[e] != '-') e++;
					Str w; w.set (s + i, e - i);
					static const char *const KW[] = { "const", "let", "var", "function", "return", "if", "else", "for", "while", "new",
									"async", "await", "import", "from", "export", "class", "this", "typeof", 0 };
					if (w.eq ("true") || w.eq ("false") || w.eq ("null") || w.eq ("undefined")) k = TK_LIT;
					else if (lang == LANG_JS) for (int q = 0; KW[q]; q++) if (w.eq (KW[q])) k = TK_KEYWORD;
				}
				else if (c == '{' || c == '}' || c == '[' || c == ']' || c == ':' || c == ',') k = TK_PUNCT;
			}
			else if (lang == LANG_XML || lang == LANG_HTML)
			{
				if (c == '<' && i + 3 < le && s[i + 1] == '!' && s[i + 2] == '-' && s[i + 3] == '-')
				{
					e = i + 4; while (e + 2 < le && !(s[e] == '-' && s[e + 1] == '-' && s[e + 2] == '>')) e++;
					e = imin (le, e + 3); k = TK_COMMENT;
				}
				else if (c == '<')
				{
					// the tag's name, then its attributes
					e = i + 1; while (e < le && (s[e] == '/' || s[e] == '?' || s[e] == '!')) e++;
					while (e < le && s[e] != ' ' && s[e] != '>' && s[e] != '/' && s[e] != '\t') e++;
					run (cv, i, e, TK_TAG, y, CW, &col);
					i = e;
					while (i < le && s[i] != '>')
					{
						int a = i;
						if (s[i] == '"' || s[i] == '\'')
						{ char q = s[i]; int f = i + 1; while (f < le && s[f] != q) f++; if (f < le) f++; run (cv, a, f, TK_STR, y, CW, &col); i = f; continue; }
						if (s[i] == ' ' || s[i] == '=' || s[i] == '/' || s[i] == '?' || s[i] == '\t') { run (cv, a, a + 1, s[i] == '/' || s[i] == '?' ? TK_TAG : TK_PUNCT, y, CW, &col); i++; continue; }
						int f = i; while (f < le && s[f] != '=' && s[f] != ' ' && s[f] != '>' && s[f] != '/') f++;
						run (cv, a, f, TK_ATTR, y, CW, &col); i = f;
					}
					if (i < le) { run (cv, i, i + 1, TK_TAG, y, CW, &col); i++; }
					continue;
				}
				else { e = i + 1; while (e < le && s[e] != '<' && s[e] != '{') e++; }
			}
			else { e = i + 1; while (e < le && s[e] != '{') e++; }
			run (cv, i, e, k, y, CW, &col);
			i = e;
			if (col * CW - m_left > cv.w) break;
		}
	}
};

// ================================================================================================================
// Btn: a button of the modern look
// ================================================================================================================
enum { BTN_SECONDARY, BTN_PRIMARY, BTN_GHOST, BTN_DANGER };
class Btn : public Widget
{
public:
	Str text; int kind, glyph, tglyph; Action cb; bool on;
	Btn (int l, int t, int w, int h, const char *s, int kind_ = BTN_SECONDARY, Action cb_ = 0)
		: Widget (l, t, w, h), text (s), kind (kind_), glyph (-1), tglyph (-1), cb (cb_), on (false) {}
	Btn *setGlyph (int g) { glyph = g; invalidate (true); return this; }	// a uk_glyph WKG_*
	Btn *setTool (int g) { tglyph = g; invalidate (true); return this; }	// a toolbar icon WKT_*
	void setText (const char *s) { text = s; invalidate (true); }
	static int widthFor (const char *s, bool icon) { return uk_tw (s, 2) + 28 + (icon ? 20 : 0); }
	void onDraw () override
	{
		unsigned bg = bgColor ();
		canvas.clear (bg);
		int st = disabled ? UK_DISABLED : pressed && hover ? UK_PRESSED : hover ? UK_HOT : UK_NORMAL;
		unsigned ink;
		if (kind == BTN_PRIMARY || kind == BTN_DANGER)
		{
			unsigned base = kind == BTN_DANGER ? C_BAD : C_ACCENT;
			if (disabled) base = uk_mix (base, bg, 140);
			unsigned top = st == UK_PRESSED ? uk_tone (base, 110) : st == UK_HOT ? uk_tone (base, 150) : uk_tone (base, 138);
			unsigned bot = st == UK_PRESSED ? uk_tone (base, 100) : uk_tone (base, 118);
			uk_rbox (canvas, 0, 0, width, height, 6, top, bot);
			uk_rline (canvas, 0, 0, width, height, 6, uk_tone (base, 90), 120);
			ink = uk_ink_on (base);
		}
		else if (kind == BTN_GHOST)
		{
			if (on) uk_rbox (canvas, 0, 0, width, height, 6, uk_mix (bg, C_ACCENT, 50), uk_mix (bg, C_ACCENT, 50));
			else if (st == UK_HOT || st == UK_PRESSED) uk_rbox (canvas, 0, 0, width, height, 6, uk_mix (bg, C_TEXT, st == UK_PRESSED ? 40 : 22), uk_mix (bg, C_TEXT, st == UK_PRESSED ? 40 : 22));
			ink = disabled ? uk_mix (bg, uk_ink_for (bg), 110) : uk_ink_for (bg);
		}
		else
		{
			uk_raised (canvas, 0, 0, width, height, 6, C_BUTTON, st);
			ink = disabled ? uk_mix (C_BUTTON, C_BUTTON_TEXT, 110) : C_BUTTON_TEXT;
		}
		int iw = (glyph >= 0 || tglyph >= 0) ? 18 : 0;
		int txw = text.len () ? uk_tw (text.c (), kind == BTN_PRIMARY ? 2 : 0) : 0;
		int gap = iw && txw ? 6 : 0;
		int x = (width - iw - gap - txw) / 2 + (st == UK_PRESSED ? 1 : 0);
		int y = st == UK_PRESSED ? 1 : 0;
		if (glyph >= 0) uk_glyph (canvas, glyph, x + 8, height / 2 + y, 10, ink);
		if (tglyph >= 0) uk_tool_glyph (canvas, tglyph, x + 1, (height - 16) / 2 + y, 16, ink);
		if (txw) text_v (canvas, x + iw + gap, y, height, text.c (), ink, kind == BTN_PRIMARY ? 2 : 0);
	}
	bool onMouse (int mx, int, int bl, int, int, int) override
	{
		if (mx < 0) { if (hover || pressed) { hover = pressed = false; invalidate (true); } return false; }
		if (!hover) { hover = true; invalidate (true); }
		if (disabled) return true;
		if (bl && !pressed) { pressed = true; invalidate (true); }
		else if (!bl && pressed) { pressed = false; invalidate (true); if (cb) cb (*this); }
		return true;
	}
};

// ================================================================================================================
// TabBar: text tabs, the chosen one underlined in the accent
// ================================================================================================================
class TabBar : public Widget
{
public:
	enum { MAXT = 12 };
	Str label[MAXT]; int count[MAXT]; bool dot[MAXT]; unsigned badge[MAXT];
	int n, sel; Action onChange; unsigned bg; bool line;
	TabBar (int l, int t, int w, int h, unsigned bg_) : Widget (l, t, w, h), n (0), sel (0), onChange (0), bg (bg_), line (true), m_hot (-1)
	{ for (int i = 0; i < MAXT; i++) { count[i] = -1; dot[i] = false; badge[i] = 0; } }
	unsigned bgColor () override { return bg; }
	int add (const char *s) { if (n >= MAXT) return -1; label[n] = s; n++; invalidate (true); return n - 1; }
	void setCount (int i, int c) { if (i >= 0 && i < n && count[i] != c) { count[i] = c; invalidate (true); } }
	void setDot (int i, bool d) { if (i >= 0 && i < n && dot[i] != d) { dot[i] = d; invalidate (true); } }
	void select (int i, bool fire) { if (i < 0 || i >= n) return; bool ch = i != sel; sel = i; invalidate (true); if (ch && fire && onChange) onChange (*this); }
	int tabW (int i)
	{
		int w = uk_tw (label[i].c (), 0) + 24;
		if (count[i] >= 0) { char b[16]; snprintf (b, sizeof b, "%d", count[i]); w += uk_tw (b) + 10; }
		if (dot[i]) w += 10;
		return w;
	}
	int tabX (int i) { int x = 4; for (int k = 0; k < i; k++) x += tabW (k); return x; }
	int at (int mx) { for (int i = 0; i < n; i++) { int x = tabX (i); if (mx >= x && mx < x + tabW (i)) return i; } return -1; }
	void onDraw () override
	{
		canvas.clear (bg);
		unsigned ink = uk_ink_for (bg);
		if (line) canvas.fillRect (0, height - 1, width, 1, uk_mix (bg, ink, 36));
		for (int i = 0; i < n; i++)
		{
			int x = tabX (i), w = tabW (i);
			bool s = i == sel;
			unsigned c = s ? ink : uk_mix (bg, ink, i == m_hot ? 210 : 160);
			text_v (canvas, x + 12, 0, height - 2, label[i].c (), c, s ? 2 : 0);
			int tx = x + 12 + uk_tw (label[i].c (), s ? 2 : 0) + 4;
			if (count[i] >= 0)
			{
				char b[16]; snprintf (b, sizeof b, "%d", count[i]);
				unsigned bc = badge[i] ? badge[i] : on_field (C_OK);
				text_v (canvas, tx + 2, -4, height - 2, b, bc, 0);
				tx += uk_tw (b) + 6;
			}
			if (dot[i]) uk_glyph (canvas, WKG_DOT, tx + 3, height / 2 - 1, 6, on_field (C_OK));
			if (s) uk_rbox (canvas, x + 6, height - 3, w - 12, 3, 1, C_ACCENT, C_ACCENT);
		}
	}
	bool onMouse (int mx, int, int bl, int, int, int) override
	{
		if (mx < 0) { if (m_hot >= 0) { m_hot = -1; invalidate (true); } return false; }
		int h = at (mx);
		if (h != m_hot) { m_hot = h; invalidate (true); }
		if (bl && !pressed) { pressed = true; if (h >= 0) select (h, true); }
		else if (!bl) pressed = false;
		return h >= 0;
	}
private:
	int m_hot;
};

// ================================================================================================================
// RadioRow: radio buttons in a row
// ================================================================================================================
class RadioRow : public Widget
{
public:
	enum { MAXR = 8 };
	const char *label[MAXR]; int n, sel; Action onChange; unsigned bg;
	RadioRow (int l, int t, int w, int h, const char *const *labels, int n_, unsigned bg_) : Widget (l, t, w, h), n (imin (n_, MAXR)), sel (0), onChange (0), bg (bg_), m_hot (-1)
	{ for (int i = 0; i < n; i++) label[i] = labels[i]; }
	unsigned bgColor () override { return bg; }
	int itemX (int i) { int x = 0; for (int k = 0; k < i; k++) x += uk_tw (label[k]) + 44; return x; }
	void onDraw () override
	{
		canvas.clear (bg);
		unsigned ink = uk_ink_for (bg);
		for (int i = 0; i < n; i++)
		{
			int x = itemX (i);
			uk_radio_mark (canvas, x, (height - 14) / 2, 14, i == sel, i == m_hot ? UK_HOT : UK_NORMAL);
			text_v (canvas, x + 20, 0, height, label[i], ink, 0);
		}
	}
	bool onMouse (int mx, int, int bl, int, int, int) override
	{
		if (mx < 0) { if (m_hot >= 0) { m_hot = -1; invalidate (true); } return false; }
		int h = -1;
		for (int i = 0; i < n; i++) { int x = itemX (i); if (mx >= x && mx < x + uk_tw (label[i]) + 30) h = i; }
		if (h != m_hot) { m_hot = h; invalidate (true); }
		if (bl && !pressed) { pressed = true; if (h >= 0 && h != sel) { sel = h; invalidate (true); if (onChange) onChange (*this); } }
		else if (!bl) pressed = false;
		return true;
	}
private:
	int m_hot;
};

// ================================================================================================================
// Choice: a drop-down drawn as a flat button (its value, a chevron), the list a PopupMenu
// ================================================================================================================
class Choice : public Widget
{
public:
	const char *const *opts; int n, sel; Action onChange; bool colorMethod, flat; const char *prefix;
	Choice (int l, int t, int w, int h, const char *const *o, int n_, int sel_ = 0)
		: Widget (l, t, w, h), opts (o), n (n_), sel (sel_), onChange (0), colorMethod (false), flat (false), prefix (0) {}
	void setOptions (const char *const *o, int n_, int s) { opts = o; n = n_; sel = s; invalidate (true); }
	const char *value () const { return sel >= 0 && sel < n ? opts[sel] : ""; }
	void onDraw () override
	{
		unsigned bg = bgColor ();
		canvas.clear (bg);
		int st = pressed ? UK_PRESSED : hover ? UK_HOT : UK_NORMAL;
		unsigned face = colorMethod ? C_FIELD : C_BUTTON;
		if (flat) { if (st != UK_NORMAL) uk_rbox (canvas, 0, 0, width, height, 5, uk_mix (bg, C_TEXT, 22), uk_mix (bg, C_TEXT, 22)); face = bg; }
		else if (colorMethod) uk_sunken (canvas, 0, 0, width, height, 5, C_FIELD, false);
		else uk_raised (canvas, 0, 0, width, height, 5, face, st);
		unsigned ink = colorMethod ? method_color (value ()) : flat ? uk_ink_for (bg) : C_BUTTON_TEXT;
		Str s; if (prefix) s.add (prefix); s.add (value ());
		text_fit (canvas, 10, 0, height, width - 34, s.c (), ink, colorMethod ? 2 : 0);
		uk_glyph (canvas, WKG_CHEV_DOWN, width - 14, height / 2, 9, flat ? uk_mix (bg, ink, 180) : ink);
	}
	bool onMouse (int mx, int, int bl, int, int, int) override
	{
		if (mx < 0) { if (hover) { hover = false; invalidate (true); } return false; }
		if (!hover) { hover = true; invalidate (true); }
		if (disabled) return true;
		if (bl && !pressed) { pressed = true; invalidate (true); }
		else if (!bl && pressed)
		{
			pressed = false; invalidate (true);
			int ax = 0, ay = 0; for (Widget *w = this; w; w = w->parent) { ax += w->left; ay += w->top; }
			Root *r = Root::current ();
			if (r) { ax -= r->left; ay -= r->top; }
			PopupMenu m (ax, ay + height + 2);
			for (int i = 0; i < n && i < 16; i++) m.add (opts[i], i + 1);
			int id = m.run ();
			hover = false;
			if (id > 0 && id - 1 != sel) { sel = id - 1; invalidate (true); if (onChange) onChange (*this); }
		}
		return true;
	}
};

// ================================================================================================================
// KVTable
// ================================================================================================================
class KVTable : public Widget
{
public:
	KVList *rows;				// the model's list (a blank row kept last when editable)
	bool readonly, showDesc, formMode, bulkMode;
	const char *colName[3];			// "Key", "Value", "Description"
	const char *ph[3];			// the blank row's placeholders
	Action onChange;
	void *user;
	int rowH, headH;

	KVTable (int l, int t, int w, int h)
		: Widget (l, t, w, h), rows (0), readonly (false), showDesc (true), formMode (false), bulkMode (false), onChange (0), user (0),
		  rowH (30), headH (30), m_top (0), m_edRow (-1), m_edCol (-1), m_hot (-1), m_hotCell (-1), m_ed (0), m_bulk (0), m_empty ("")
	{
		colName[0] = "Key"; colName[1] = "Value"; colName[2] = "Description";
		ph[0] = "Key"; ph[1] = "Value"; ph[2] = "Description";
		m_ed = new LineEdit (0, 0, 10, 10);
		m_ed->framed = false; m_ed->hidden = true; m_ed->onChange = edChanged; m_ed->onKeyHook = edKey; m_ed->padL = 6; m_ed->padR = 6;
		addChild (m_ed);
		m_bulk = new CodeEdit (0, 0, 10, 10);
		m_bulk->hidden = true; m_bulk->lineNumbers = false; m_bulk->onChange = bulkChanged; m_bulk->framed_ = true;
		addChild (m_bulk);
	}
	unsigned bgColor () override { return C_FIELD; }
	void setRows (KVList *r) { stopEdit (); rows = r; m_top = 0; ensureBlank (); if (bulkMode) toBulk (); invalidate (true); }
	void refresh () { ensureBlank (); if (m_edRow >= 0 && rows && m_edRow < rows->size ()) { KV &kv = (*rows)[m_edRow]; Str *s = cell (kv, m_edCol); if (s && !s->eq (m_ed->text.c ())) m_ed->setText (s->c ()); } invalidate (true); }
	int count () const { if (!rows) return 0; int c = 0; for (int i = 0; i < rows->size (); i++) if (!(*rows)[i].blank () && (*rows)[i].on) c++; return c; }
	void setBulk (bool on)
	{
		if (on == bulkMode || readonly) return;
		stopEdit ();
		bulkMode = on;
		if (on) toBulk (); else fromBulk ();
		m_bulk->hidden = !on;
		layout (); invalidate (true);
	}
	void layout () override
	{
		if (!m_ed || !m_bulk) return;
		m_bulk->left = 0; m_bulk->top = 0; m_bulk->resizeTo (width, height);
		placeEditor ();
	}

	// the columns' x and widths
	int cols () { return showDesc ? 3 : 2; }
	int checkW () { return readonly ? 0 : 34; }
	int delW () { return readonly ? 0 : 30; }
	void colBox (int c, int *x, int *w)
	{
		int avail = width - checkW () - delW () - (vbar () ? UK_SBW : 0);
		int w0 = showDesc ? avail * 30 / 100 : avail * 38 / 100;
		int w1 = showDesc ? avail * 42 / 100 : avail - w0;
		int w2 = avail - w0 - w1;
		int ws[3] = { w0, w1, w2 };
		int xx = checkW ();
		for (int i = 0; i < c; i++) xx += ws[i];
		*x = xx; *w = ws[c];
	}
	int nrows () { return rows ? rows->size () : 0; }
	int visRows () { return imax (1, (height - headH) / rowH); }
	bool vbar () { return nrows () > visRows (); }

	void onDraw () override
	{
		canvas.clear (C_FIELD);
		if (bulkMode) return;
		unsigned hdrBg = uk_mix (C_FIELD, C_FIELD_TEXT, 10);
		canvas.fillRect (0, 0, width, headH, hdrBg);
		canvas.fillRect (0, headH - 1, width, 1, c_line ());
		for (int c = 0; c < cols (); c++)
		{
			int x, w; colBox (c, &x, &w);
			text_fit (canvas, x + 8, 0, headH, w - 12, colName[c], c_dim (), 2);
			if (c > 0) canvas.fillRect (x, 0, 1, height, c_line ());
		}
		if (!readonly) canvas.fillRect (checkW (), 0, 1, height, c_line ());
		if (!rows) return;
		int vr = visRows ();
		m_top = iclamp (m_top, 0, imax (0, nrows () - vr));
		Canvas clip; clip.adopt (canvas.px + headH * canvas.stride, width, imax (1, height - headH), canvas.stride);
		for (int r = 0; r <= vr; r++)
		{
			int i = m_top + r;
			if (i >= nrows ()) break;
			int y = r * rowH;
			KV &kv = (*rows)[i];
			bool ghost = !readonly && i == nrows () - 1 && kv.blank ();
			if (i == m_hot && !ghost) clip.fillRect (0, y, width - (vbar () ? UK_SBW : 0), rowH, c_hover ());
			clip.fillRect (0, y + rowH - 1, width, 1, c_line ());
			if (!readonly && !ghost) uk_check_mark (clip, (checkW () - 16) / 2, y + (rowH - 16) / 2, 16, kv.on, UK_NORMAL);
			for (int c = 0; c < cols (); c++)
			{
				int x, w; colBox (c, &x, &w);
				if (i == m_edRow && c == m_edCol) continue;
				Str *s = cell (kv, c);
				int tx = x + 8, tw_ = w - 14;
				if (formMode && c == 0 && !ghost) tw_ -= 46;
				if (formMode && c == 1 && kv.file && !ghost)
				{
					// a file: its name in a chip, "Select" when none
					const char *nm = kv.value.empty () ? "Select file..." : base_name (kv.value.c ());
					int cw_ = imin (w - 14, uk_tw (nm) + 30);
					uk_rbox (clip, tx - 2, y + 4, cw_, rowH - 8, 5, uk_mix (C_FIELD, C_ACCENT, 30), uk_mix (C_FIELD, C_ACCENT, 30));
					uk_tool_glyph (clip, WKT_OPEN, tx + 2, y + (rowH - 14) / 2, 14, C_FIELD_TEXT);
					text_fit (clip, tx + 20, y, rowH, cw_ - 24, nm, C_FIELD_TEXT, 0);
					continue;
				}
				if (!s || s->empty ())
				{
					if (ghost || (c < 2 && !readonly)) text_fit (clip, tx, y, rowH, tw_, ph[c], c_faint (), 0);
					continue;
				}
				Canvas cc; int cx0 = iclamp (tx, 0, clip.w), cw0 = iclamp (tw_, 0, clip.w - cx0);
				if (cw0 <= 0) continue;
				cc.adopt (clip.px + y * clip.stride + cx0, cw0, rowH, clip.stride);
				unsigned ink = kv.on || readonly ? C_FIELD_TEXT : c_faint ();
				// one line: a value with new lines shows them as spaces
				Str one; one.set (s->c (), imin (s->len (), 2000));
				for (int q = 0; q < one.len (); q++) if (one.c ()[q] == '\n' || one.c ()[q] == '\r') one.data ()[q] = ' ';
				draw_rich (cc, 0, (rowH - uk_fh ()) / 2, one.c (), one.len (), ink, !readonly, readonly && c == 0 ? 2 : 0);
			}
			if (formMode && !ghost)
			{
				int x, w; colBox (0, &x, &w);
				int bx = x + w - 50;
				text_v (clip, bx, y, rowH, kv.file ? "File" : "Text", c_dim ());
				uk_glyph (clip, WKG_CHEV_DOWN, bx + 38, y + rowH / 2, 8, c_dim ());
			}
			if (!readonly && !ghost && i == m_hot)
				uk_glyph (clip, WKG_CLOSE, width - (vbar () ? UK_SBW : 0) - delW () / 2, y + rowH / 2, 9, c_dim ());
		}
		if (vbar ())
		{
			UkThumb t = uk_thumb (nrows (), vr, m_top, height - headH);
			uk_draw_vscroll (canvas, width - UK_SBW, headH, UK_SBW, height - headH, t, C_FIELD);
		}
	}

	bool onMouse (int mx, int my, int bl, int, int, int wheel) override
	{
		if (bulkMode) return false;
		if (mx < 0) { if (m_hot >= 0) { m_hot = -1; invalidate (true); } return false; }
		if (wheel) { m_top += wheel; m_top = iclamp (m_top, 0, imax (0, nrows () - visRows ())); placeEditor (); invalidate (true); return true; }
		int r = my < headH ? -1 : m_top + (my - headH) / rowH;
		if (r >= nrows ()) r = -1;
		if (r != m_hot) { m_hot = r; invalidate (true); }
		// a value's full text as the tooltip (a long one cut in the cell)
		tip = 0;
		if (r >= 0 && rows)
		{
			int c = colAt (mx);
			Str *s = c >= 0 ? cell ((*rows)[r], c) : 0;
			if (s && !s->empty ())
			{
				int x, w; colBox (c, &x, &w);
				m_tip.clear ();
				if (!readonly && strstr (s->c (), "{{"))		// the variable under the pointer
				{
					int p = pos_at_x (s->c (), s->len (), mx - x - 8);
					var_tip (m_tip, s->c (), s->len (), imin (p, s->len () - 1));
				}
				else if (uk_tw (s->c ()) > w - 14) { m_tip.set (s->c (), imin (s->len (), 120)); if (s->len () > 120) m_tip.add ("..."); }
				if (!m_tip.empty ()) tip = m_tip.c ();
			}
		}
		if (bl && !pressed)
		{
			pressed = true;
			if (r < 0 || !rows) return true;
			KV &kv = (*rows)[r];
			bool ghost = !readonly && r == nrows () - 1 && kv.blank ();
			if (readonly)
			{
				// a click copies the row's value (a double one: "key: value")
				unsigned now = kapi_get_ticks ();
				Str t; if (now - m_lastClick < 35 && m_lastRow == r) { t.add (kv.key.c ()); t.add (": "); } t.add (kv.value.c ());
				clip_set_text_n (t.c (), t.len ());
				m_lastClick = now; m_lastRow = r;
				return true;
			}
			if (mx < checkW () && !ghost) { kv.on = !kv.on; stopEdit (); changed (); return true; }
			if (mx >= width - (vbar () ? UK_SBW : 0) - delW () && !ghost) { stopEdit (); rows->remove (r); ensureBlank (); m_hot = -1; changed (); return true; }
			int c = colAt (mx);
			if (c < 0) return true;
			if (formMode && !ghost)
			{
				int x, w; colBox (0, &x, &w);
				if (c == 0 && mx >= x + w - 54) { kv.file = !kv.file; kv.value.clear (); stopEdit (); changed (); return true; }
				if (c == 1 && kv.file)
				{
					stopEdit ();
					char p[256];
					if (uk_file_open (p, sizeof p, "SD:/")) { kv.value = p; changed (); }
					return true;
				}
			}
			startEdit (r, c, mx);
		}
		else if (!bl) pressed = false;
		return true;
	}
	void startEdit (int r, int c, int mx = -1)
	{
		if (!rows || r < 0 || r >= nrows ()) return;
		KV &kv = (*rows)[r];
		if (formMode && c == 1 && kv.file) return;
		m_edRow = r; m_edCol = c;
		Str *s = cell (kv, c);
		m_ed->setText (s ? s->c () : "");
		m_ed->placeholder = ph[c];
		// (a row out of view: scrolled to)
		if (r < m_top) m_top = r;
		if (r >= m_top + visRows ()) m_top = r - visRows () + 1;
		placeEditor ();
		m_ed->hidden = false;
		m_ed->setFocus ();
		if (mx >= 0) { int x, w; colBox (c, &x, &w); m_ed->caret = m_ed->posAtMx (mx - x); m_ed->anchor = -1; }
		invalidate (true);
	}
	void stopEdit ()
	{
		if (m_edRow < 0) return;
		m_edRow = m_edCol = -1;
		m_ed->hidden = true;
		invalidate (true);
	}
	void onFocusGone () { stopEdit (); }

	// the editor's row / column change as the user types (Tab, Enter, Up / Down)
	static bool edKey (LineEdit &e, long k)
	{
		KVTable *t = (KVTable *) e.parent;
		if (k == KEY_TAB)
		{
			int c = t->m_edCol + (shift_held () ? -1 : 1), r = t->m_edRow;
			if (c >= t->cols ()) { c = 0; r++; }
			if (c < 0) { c = t->cols () - 1; r--; }
			if (r < 0 || r >= t->nrows ()) { t->stopEdit (); return true; }
			t->startEdit (r, c); t->m_ed->selectAll ();
			return true;
		}
		if (k == KEY_ENTER || k == KEY_DOWN)
		{
			int r = t->m_edRow + 1;
			if (r >= t->nrows ()) { if (k == KEY_ENTER) t->stopEdit (); return true; }
			t->startEdit (r, t->m_edCol); t->m_ed->caret = t->m_ed->text.len ();
			return true;
		}
		if (k == KEY_UP) { if (t->m_edRow > 0) { t->startEdit (t->m_edRow - 1, t->m_edCol); t->m_ed->caret = t->m_ed->text.len (); } return true; }
		if (k == 27) { t->stopEdit (); return true; }
		return false;
	}
	static void edChanged (Widget &w)
	{
		LineEdit &e = (LineEdit &) w;
		KVTable *t = (KVTable *) e.parent;
		if (!t->rows || t->m_edRow < 0 || t->m_edRow >= t->nrows ()) return;
		Str *s = t->cell ((*t->rows)[t->m_edRow], t->m_edCol);
		if (s) *s = e.text;
		t->ensureBlank ();
		t->changed ();
	}
	static void bulkChanged (Widget &w)
	{
		KVTable *t = (KVTable *) w.parent;
		t->fromBulkText ();
		if (t->onChange) t->onChange (*t);
	}
	void changed () { invalidate (true); if (onChange) onChange (*this); }

private:
	int m_top, m_edRow, m_edCol, m_hot, m_hotCell;
	LineEdit *m_ed;
	CodeEdit *m_bulk;
	Str m_empty, m_tip;
	unsigned m_lastClick = 0; int m_lastRow = -1;

	Str *cell (KV &kv, int c) { return c == 0 ? &kv.key : c == 1 ? &kv.value : c == 2 ? &kv.desc : 0; }
	int colAt (int mx) { for (int c = 0; c < cols (); c++) { int x, w; colBox (c, &x, &w); if (mx >= x && mx < x + w) return c; } return -1; }
	void ensureBlank ()
	{
		if (!rows || readonly) return;
		if (!rows->size () || !(*rows)[rows->size () - 1].blank ()) rows->push (KV ());
		// (only one blank row at the end)
		while (rows->size () >= 2 && (*rows)[rows->size () - 1].blank () && (*rows)[rows->size () - 2].blank () && m_edRow != rows->size () - 2)
			rows->remove (rows->size () - 1);
	}
	void placeEditor ()
	{
		if (m_edRow < 0) return;
		int x, w; colBox (m_edCol, &x, &w);
		int y = headH + (m_edRow - m_top) * rowH;
		if (y < headH || y + rowH > height) { m_ed->hidden = true; return; }
		m_ed->left = x + 2; m_ed->top = y + 2;
		int ew = w - 4; if (formMode && m_edCol == 0) ew -= 50;
		m_ed->resizeTo (imax (10, ew), rowH - 4);
		m_ed->hidden = false;
	}
	// Bulk Edit: "key:value" a line, a disabled row "//key:value"
	void toBulk ()
	{
		Str t;
		if (rows)
			for (int i = 0; i < rows->size (); i++)
			{
				KV &kv = (*rows)[i];
				if (kv.blank ()) continue;
				if (!kv.on) t.add ("//");
				t.add (kv.key.c ()); t.add (':'); t.add (kv.value.c ()); t.add ('\n');
			}
		m_bulk->setText (t.c ());
	}
	void fromBulk () { fromBulkText (); ensureBlank (); }
	void fromBulkText ()
	{
		if (!rows) return;
		KVList old = *rows;
		rows->clear ();
		const char *p = m_bulk->text ();
		while (*p)
		{
			const char *e = p; while (*e && *e != '\n') e++;
			Str line; s_trim (line, p, (int) (e - p));
			if (!line.empty ())
			{
				KV kv; const char *s = line.c ();
				if (s[0] == '/' && s[1] == '/') { kv.on = false; s += 2; }
				const char *c = strchr (s, ':');
				if (c) { s_trim (kv.key, s, (int) (c - s)); s_trim (kv.value, c + 1); } else s_trim (kv.key, s);
				// (the descriptions kept by key)
				for (int i = 0; i < old.size (); i++) if (old[i].key.eq (kv.key.c ())) { kv.desc = old[i].desc; kv.file = old[i].file; break; }
				rows->push (kv);
			}
			if (!*e) break;
			p = e + 1;
		}
		if (!readonly) rows->push (KV ());
	}
};

// ================================================================================================================
// DocTabs: the open requests (and environments, collections), as a browser's tabs
// ================================================================================================================
struct DocTabInfo { Str title; Str method; bool dirty; int kind; };	// kind: 0 request, 1 environment, 2 collection
class DocTabs : public Widget
{
public:
	Vec<DocTabInfo> tabs;
	int sel;
	void (*onSelect) (int), (*onClose) (int), (*onNew) (), (*onContext) (int, int, int);
	DocTabs (int l, int t, int w, int h) : Widget (l, t, w, h), sel (-1), onSelect (0), onClose (0), onNew (0), onContext (0), m_hot (-1), m_hotClose (false), m_hotNew (false) {}
	int tabW ()
	{
		int n = imax (1, tabs.size ());
		int w = (width - 48) / n;
		return iclamp (w, 96, 220);
	}
	int firstShown ()
	{
		int w = tabW (), room = imax (1, (width - 48) / w);
		if (sel < room) return 0;
		return sel - room + 1;
	}
	void onDraw () override
	{
		unsigned bg = C_BG;
		canvas.clear (bg);
		canvas.fillRect (0, height - 1, width, 1, c_bgline ());
		int w = tabW (), f = firstShown (), x = 0;
		for (int i = f; i < tabs.size (); i++)
		{
			if (x + w > width - 44) break;
			DocTabInfo &t = tabs[i];
			bool s = i == sel, h = i == m_hot;
			if (s)
			{
				uk_rbox (canvas, x, 3, w, height - 3, 7, C_FIELD, C_FIELD, 255, UK_TL | UK_TR);
				uk_rline (canvas, x, 3, w, height - 2, 7, c_bgline (), 255, UK_TL | UK_TR);
				canvas.fillRect (x + 1, height - 1, w - 2, 1, C_FIELD);
				uk_rbox (canvas, x + 1, 3, w - 2, 2, 1, C_ACCENT, C_ACCENT);
			}
			else
			{
				if (h) uk_rbox (canvas, x + 2, 5, w - 4, height - 9, 6, uk_mix (bg, C_TEXT, 18), uk_mix (bg, C_TEXT, 18));
				if (i + 1 != sel && i + 1 < tabs.size ()) canvas.fillRect (x + w - 1, 11, 1, height - 22, c_bgline ());
			}
			unsigned ink = s ? C_FIELD_TEXT : uk_ink_for (bg);
			unsigned face = s ? C_FIELD : bg;
			int tx = x + 12;
			if (t.kind == 0)
			{
				const char *m = t.method.c ();
				char mb[8]; s_copy (mb, s_eq (m, "DELETE") ? "DEL" : s_eq (m, "OPTIONS") ? "OPT" : m, sizeof mb);
				unsigned mc = s ? method_color (m) : uk_mix (face, uk_ink_on (face) == 0 ? method_color (m) : 0xFFFFFF, 200);
				text_v (canvas, tx, 2, height, mb, mc, 2);
				tx += uk_tw (mb, 2) + 6;
			}
			else
			{
				// an environment: an eye-like mark; a collection: a folder
				unsigned mc = uk_mix (face, ink, 170);
				if (t.kind == 1) { uk_rline (canvas, tx, height / 2 - 4, 14, 10, 5, mc); uk_glyph (canvas, WKG_DOT, tx + 7, height / 2 + 1, 5, mc); }
				else { uk_rbox (canvas, tx, height / 2 - 4, 6, 3, 1, mc, mc); uk_rbox (canvas, tx, height / 2 - 2, 14, 9, 2, mc, mc); }
				tx += 20;
			}
			int room = x + w - 26 - tx;
			text_fit (canvas, tx, 2, height, room, t.title.c (), ink, 0);
			int cx = x + w - 15, cy = height / 2 + 1;
			if (h && m_hotClose) uk_rbox (canvas, cx - 8, cy - 8, 16, 16, 4, uk_mix (face, ink, 40), uk_mix (face, ink, 40));
			if (h || s) uk_glyph (canvas, WKG_CLOSE, cx, cy, 8, uk_mix (face, ink, 190));
			else if (t.dirty) uk_glyph (canvas, WKG_DOT, cx, cy, 7, C_ACCENT);
			if ((h || s) && t.dirty && !(h && m_hotClose)) { canvas.fillRect (cx - 8, cy - 8, 16, 16, face); uk_glyph (canvas, WKG_DOT, cx, cy, 7, on_field (C_VAR)); }
			x += w;
		}
		// "+": a new request
		int nx = x + 6;
		if (m_hotNew) uk_rbox (canvas, nx, 7, 28, height - 14, 6, uk_mix (bg, C_TEXT, 24), uk_mix (bg, C_TEXT, 24));
		uk_glyph (canvas, WKG_PLUS, nx + 14, height / 2 + 1, 11, uk_ink_for (bg));
		m_newX = nx;
	}
	bool onMouse (int mx, int my, int bl, int br, int bm, int) override
	{
		if (mx < 0) { if (m_hot >= 0 || m_hotNew) { m_hot = -1; m_hotNew = false; invalidate (true); } return false; }
		int w = tabW (), f = firstShown ();
		int i = f + mx / w;
		bool overNew = mx >= m_newX && mx < m_newX + 28;
		if (i >= tabs.size () || overNew || (mx / w) * w + w > width - 44) i = -1;
		bool cl = i >= 0 && mx >= (i - f) * w + w - 24 && mx < (i - f) * w + w - 6;
		if (i != m_hot || cl != m_hotClose || overNew != m_hotNew) { m_hot = i; m_hotClose = cl; m_hotNew = overNew; invalidate (true); }
		if ((bl || br || bm) && !pressed)
		{
			pressed = true;
			if (overNew && bl) { if (onNew) onNew (); return true; }
			if (i < 0) return true;
			if (bm || (bl && cl)) { if (onClose) onClose (i); return true; }
			if (br) { if (onContext) onContext (i, mx, my); return true; }
			if (onSelect) onSelect (i);
		}
		else if (!bl && !br && !bm) pressed = false;
		return true;
	}
private:
	int m_hot; bool m_hotClose, m_hotNew; int m_newX = 0;
};

// ================================================================================================================
// Spinner: a ring turning (a request on its way)
// ================================================================================================================
static inline void draw_spinner (Canvas &cv, int cx, int cy, int r, int phase, unsigned c, unsigned bg)
{
	for (int i = 0; i < 12; i++)
	{
		int a = (i * 30 + phase * 30) % 360;
		// (sin / cos of multiples of 30 degrees, x 1000)
		static const int S[12] = { 0, 500, 866, 1000, 866, 500, 0, -500, -866, -1000, -866, -500 };
		int k = (a / 30) % 12;
		int x = cx + S[k] * r / 1000, y = cy - S[(k + 3) % 12] * r / 1000;
		int alpha = 40 + i * 18;
		unsigned col = uk_mix (bg, c, alpha);
		uk_rbox (cv, x - 2, y - 2, 5, 5, 2, col, col);
	}
}

// ---- a small empty state: an icon-like drawing and two lines ----------------------------------------------------
static inline void empty_state (Canvas &cv, int x, int y, int w, int h, const char *title, const char *line, unsigned bg)
{
	int cx = x + w / 2, cy = y + h / 2 - 20;
	unsigned c = uk_mix (bg, uk_ink_for (bg), 70);
	// a paper plane: a courier's
	for (int i = 0; i < 26; i++)
	{
		int lx = cx - 26 + i, top = cy - 12 + i / 2, bot = cy + 14 - i / 3;
		cv.fillRect (lx, top, 1, imax (1, bot - top), c);
	}
	for (int i = 0; i < 26; i++) { int lx = cx + i; cv.fillRect (lx, cy - 12 + 13 - i / 2, 1, 2 + i / 3, uk_mix (bg, c, 180)); }
	uk_text (cv, cx - uk_tw (title, 2) / 2, cy + 30, title, uk_mix (bg, uk_ink_for (bg), 190), 2);
	if (line) uk_text (cv, cx - uk_tw (line) / 2, cy + 30 + uk_fh () + 6, line, uk_mix (bg, uk_ink_for (bg), 140));
}

} // namespace cr

#endif
