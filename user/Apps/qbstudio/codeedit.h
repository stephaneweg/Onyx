//
// codeedit.h -- QBStudio's code editor: BASIC (Main.bas, the generated Main.form.bas) and the .form text, in the
// monospaced face, the language's colours, the line numbers, the indentation's guides, the lines with a problem
// marked (a red dot in the margin, the text underlined), undo / redo, the clipboard, and the completion: after a
// control's name and a dot its properties and methods, Ctrl+Space the names (the app gives them: g_complete).
// The words of BASIC are written in capitals as you type (as QBasic does). Courier's code editor is its model.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef _qbstudio_codeedit_h
#define _qbstudio_codeedit_h

#include "uikit/uikit.h"
#include "systemkit/systemkit.h"
#include "form.h"

namespace qs {
using namespace uikit;

enum { LANG_BASIC, LANG_FORM };
static TextFace *g_mono = 0;				// the code's face (DejaVu Sans Mono), 0: the UI's
static TextFace *g_ui = 0;				// the UI's (the completion's list)
static bool (*g_isKeyword) (const char *w, int n);	// a word of BASIC? (the app: bas::wordList)

// A completion: what may come here
struct Compl { char name[40]; char kind; char detail[40]; };	// kind: 'p' property, 'm' method, 'c' control, 's' a SUB, 'k' a word
typedef void (*ComplFn) (const char *object, Vec<Compl> &out);	// object: the name before the dot ("" = none)
static ComplFn g_complete;

static inline int iclamp (int v, int a, int b) { return v < a ? a : v > b ? b : v; }
static inline bool word_ch (char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '$' || c == '%' || c == '!' || c == '#' || c == '&'; }
static inline bool name_ch (char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }
static inline char upc (char c) { return c >= 'a' && c <= 'z' ? (char) (c - 32) : c; }
// Ctrl held -- alone: Ctrl + Alt is AltGr as a PC sends it (a remote keyboard: VNC, Onyx Remote), and types its
// character (# @ { [ ...) like any other key
static inline bool mod_ctrl () { unsigned m = kapi_get_modifiers (); return (m & MOD_CTRL) != 0 && (m & MOD_ALT) == 0; }
static inline bool mod_shift () { return (kapi_get_modifiers () & MOD_SHIFT) != 0; }

// The colours (on the field's colour: darker in a dark theme)
enum { TK_TEXT, TK_KEYWORD, TK_STR, TK_NUM, TK_COMMENT, TK_OBJECT, TK_MEMBER, TK_KIND, TK_NAME, TK_KEY, TK_FLAG, TK_PUNCT };
static inline unsigned tk_color (int k)
{
	static const unsigned C[] = { 0, 0x000F5FB8, 0x00B5530B, 0x006A3FB5, 0x00808890, 0x00146C7A, 0x000E7A8A, 0x000F5FB8, 0x00202020, 0x00A0226E, 0x00146C7A, 0 };
	bool dark = uk_bright (C_FIELD) < 110;
	if (k == TK_TEXT || k == TK_NAME) return C_FIELD_TEXT;
	if (k == TK_PUNCT) return uk_mix (C_FIELD, C_FIELD_TEXT, 170);
	return dark ? uk_mix (C[k], 0xFFFFFF, 110) : C[k];
}

// A text that grows (the editor's)
struct TextBuf
{
	char *b; int n, cap;
	TextBuf () : b (0), n (0), cap (0) { need (64); b[0] = 0; }
	~TextBuf () { free (b); }
	void need (int m) { if (m + 1 <= cap) return; int c = cap ? cap * 2 : 256; while (c < m + 1) c *= 2; b = (char *) realloc (b, c); cap = c; }
	void set (const char *s, int l = -1) { if (l < 0) l = (int) strlen (s); need (l); memcpy (b, s, l); n = l; b[n] = 0; }
	void insert (int at, const char *s, int l) { need (n + l); memmove (b + at + l, b + at, n - at + 1); memcpy (b + at, s, l); n += l; }
	void erase (int at, int l) { memmove (b + at, b + at + l, n - at - l + 1); n -= l; }
private:
	TextBuf (const TextBuf &);
	TextBuf &operator= (const TextBuf &);
};

class CodeEdit : public Widget
{
public:
	bool readonly; int lang;
	Action onChange;				// the text changed
	Action onCaret;					// the caret moved (the app's status bar, its lists)
	Vec<int> marks;					// lines (1-based) with a problem
	int hiLine;					// a line lit (1-based; 0 none): the line an error points at

	CodeEdit (int l, int t, int w, int h)
		: Widget (l, t, w, h), readonly (false), lang (LANG_BASIC), onChange (0), onCaret (0), hiLine (0),
		  m_caret (0), m_anchor (-1), m_top (0), m_left (0), m_goal (-1), m_drag (0), m_lastClick (0), m_lastEdit (0), m_editKind (0), m_maxCols (0),
		  m_popup (false), m_psel (0), m_ptop (0), m_pstart (0)
	{ canFocus = true; relines (); }
	~CodeEdit () { clearUndo (); for (int i = 0; i < m_redo.n; i++) free (m_redo[i].text); }

	const char *text () const { return m_t.b; }
	int length () const { return m_t.n; }
	void setText (const char *s)
	{
		m_t.set (s ? s : ""); m_caret = 0; m_anchor = -1; m_top = 0; m_left = 0; m_popup = false;
		clearUndo (); relines (); invalidate (true);
	}
	// The whole text replaced as one step that can be undone (the caret put at `caret`)
	void replaceAll (const char *s, int caret)
	{
		snap (3); m_t.set (s); m_caret = iclamp (caret, 0, m_t.n); m_anchor = -1; changed ();
		ensureCaret (true);
	}
	int caret () const { return m_caret; }
	int caretLine () const { return lineOf (m_caret); }	// 0-based
	int caretCol () const { return colOf (m_lines[lineOf (m_caret)], m_caret); }
	int lineCount () const { return m_lines.n; }
	int lineStart (int ln) const { return m_lines[iclamp (ln, 0, m_lines.n - 1)]; }
	// The caret at a line's start (0-based), shown in the middle; select: the whole line chosen
	void gotoLine (int ln, bool select = false)
	{
		ln = iclamp (ln, 0, m_lines.n - 1);
		m_caret = m_lines[ln];
		while (m_caret < lineEnd (ln) && (m_t.b[m_caret] == ' ' || m_t.b[m_caret] == '\t')) m_caret++;
		m_anchor = select ? lineEnd (ln) : -1;
		if (select) { int c = m_caret; m_caret = m_anchor; m_anchor = c; }
		m_popup = false; ensureCaret (true); invalidate (true); caretMoved ();
	}
	void setCaret (int p) { m_caret = iclamp (p, 0, m_t.n); m_anchor = -1; ensureCaret (true); invalidate (true); caretMoved (); }
	bool hasSel () const { return m_anchor >= 0 && m_anchor != m_caret; }
	void selectAll () { m_anchor = 0; m_caret = m_t.n; invalidate (true); }
	void copy () { if (hasSel ()) clip_set_text_n (m_t.b + selA (), selB () - selA ()); }
	void cut () { if (readonly || !hasSel ()) return; snap (3); copy (); delSel (); changed (); }
	void paste ()
	{
		if (readonly) return;
		char *b = (char *) malloc (1 << 18); if (!b) return;
		int n = clip_get_text (b, 1 << 18);
		if (n > 0) { int w = 0; for (int i = 0; i < n; i++) if (b[i] != '\r') b[w++] = b[i]; snap (3); insert (b, w); }
		free (b);
	}
	void undo ()
	{
		if (!m_undo.n) return;
		Snap now = { strdup (m_t.b), m_caret }; m_redo.push (now);
		Snap u = m_undo[m_undo.n - 1]; m_undo.n--;
		m_t.set (u.text); m_caret = iclamp (u.caret, 0, m_t.n); free (u.text);
		m_anchor = -1; m_editKind = 0; relines (); ensureCaret (false); invalidate (true);
		if (onChange) onChange (*this);
	}
	void redo ()
	{
		if (!m_redo.n) return;
		Snap now = { strdup (m_t.b), m_caret }; m_undo.push (now);
		Snap u = m_redo[m_redo.n - 1]; m_redo.n--;
		m_t.set (u.text); m_caret = iclamp (u.caret, 0, m_t.n); free (u.text);
		m_anchor = -1; m_editKind = 0; relines (); ensureCaret (false); invalidate (true);
		if (onChange) onChange (*this);
	}
	// The next `w` from the caret on (no case), chosen and shown; false: none
	bool find (const char *w)
	{
		int L = (int) strlen (w); if (!L) return false;
		int start = hasSel () ? selB () : m_caret;
		for (int pass = 0; pass < 2; pass++)
		{
			for (int i = pass ? 0 : start; i + L <= m_t.n; i++)
			{
				int k = 0; while (k < L && upc (m_t.b[i + k]) == upc (w[k])) k++;
				if (k == L) { m_anchor = i; m_caret = i + L; ensureCaret (true); invalidate (true); caretMoved (); return true; }
			}
		}
		return false;
	}
	void insertText (const char *s) { if (readonly) return; snap (3); insert (s, (int) strlen (s)); }

	// ---- geometry ----------------------------------------------------------------------------------------------------
	int cw () { UkFaceScope fs (g_mono); int w = uk_tw ("M"); return w < 1 ? 8 : w; }
	int lh () { UkFaceScope fs (g_mono); return uk_fh () + 3; }
	int gutter () { int d = 1, n = m_lines.n; while (n >= 10) { n /= 10; d++; } return imax (2, d) * cw () + 22; }
	int viewRows () { return imax (1, (height - 8) / lh ()); }
	bool vbar () { return m_lines.n > (height - 8) / lh (); }
	int textW () { return width - gutter () - (vbar () ? UK_SBW : 0); }

	void onDraw () override
	{
		UkFaceScope fs (g_mono);
		int fh = uk_fh (), LH = lh (), CW = cw (), G = gutter ();
		bool focus = hasFocus && !disabled;
		unsigned bg = readonly ? uk_mix (C_FIELD, C_BG, 90) : C_FIELD;
		canvas.clear (bg);
		int rows = viewRows ();
		clampScroll ();
		int tw_ = textW ();
		canvas.fillRect (0, 0, G - 10, height, uk_mix (bg, C_FIELD_TEXT, 10));
		canvas.fillRect (G - 10, 0, 1, height, uk_mix (bg, C_FIELD_TEXT, 36));
		Canvas clip; clip.adopt (canvas.px + G, imax (1, tw_), height, canvas.stride);
		int cl = lineOf (m_caret);
		int sa = selA (), sb = selB ();
		unsigned faint = uk_mix (bg, C_FIELD_TEXT, 90);
		for (int r = 0; r < rows + 1; r++)
		{
			int ln = m_top + r;
			if (ln >= m_lines.n) break;
			int y = 4 + r * LH;
			int ls = m_lines[ln], le = lineEnd (ln);
			bool marked = marks.find (ln + 1) >= 0;
			if (hiLine == ln + 1) clip.fillRect (0, y, tw_, LH, uk_mix (bg, 0xF0C020, 90));
			else if (focus && ln == cl && !hasSel ()) clip.fillRect (0, y, tw_, LH, uk_mix (bg, C_ACCENT, 14));
			char b[16]; snprintf (b, sizeof b, "%d", ln + 1);
			uk_text (canvas, G - 16 - uk_tw (b), y + 1, b, ln == cl ? C_FIELD_TEXT : faint);
			if (marked) { uk_bead (canvas, G - 9 - 3, y + LH / 2 - 3, 7, 0xD03B2B); }
			// the indentation's guides
			{
				int ind = 0; while (ls + ind < le && m_t.b[ls + ind] == ' ') ind++;
				if (ls + ind == le) ind = guideOf (ln);
				for (int c = 0; c + 2 <= ind; c += 2)
				{
					int x = c * CW - m_left + CW / 2;
					if (x >= 0) for (int q = 0; q < LH; q += 2) clip.fillRect (x, y + q, 1, 1, uk_mix (bg, C_FIELD_TEXT, 40));
				}
			}
			if (hasSel () && sb > ls && sa <= le)
			{
				int a = imax (sa, ls), e = imin (sb, le);
				int xa = colOf (ls, a) * CW - m_left, xb = colOf (ls, e) * CW - m_left + (sb > le ? CW / 2 : 0);
				clip.fillRect (xa, y, imax (2, xb - xa), LH, uk_mix (C_FIELD, C_ACCENT, focus ? 80 : 50));
			}
			drawLine (clip, ls, le, y + (LH - fh) / 2, CW);
			if (marked)
			{
				int a = ls; while (a < le && m_t.b[a] == ' ') a++;
				int xa = colOf (ls, a) * CW - m_left, xb = colOf (ls, le) * CW - m_left;
				for (int x = xa; x < imax (xb, xa + CW); x++) clip.fillRect (x, y + LH - 2 - ((x / 2) & 1), 1, 1, 0xD03B2B);
			}
			if (focus && ln == cl)
			{
				int cx = colOf (ls, m_caret) * CW - m_left;
				clip.fillRect (cx, y, readonly ? 1 : 2, LH, readonly ? faint : C_ACCENT);
			}
		}
		if (vbar ())
		{
			UkThumb t = uk_thumb (m_lines.n, rows, m_top, height);
			uk_draw_vscroll (canvas, width - UK_SBW, 0, UK_SBW, height, t, bg, m_drag == 2);
		}
		if (m_popup) drawPopup ();
	}

	bool onMouse (int mx, int my, int bl, int, int, int wheel) override
	{
		if (mx < 0) { if (!bl) m_drag = 0; return false; }
		if (wheel)
		{
			if (m_popup && inPopup (mx, my)) { m_ptop = iclamp (m_ptop - wheel, 0, imax (0, m_items.n - POP_ROWS)); invalidate (true); return true; }
			m_top -= wheel * 3; clampScroll (); invalidate (true); return true;
		}
		int G = gutter ();
		if (mx >= G && mx < width - UK_SBW) uk_cursor (KAPI_CURSOR_TEXT);
		if (bl && !pressed)
		{
			pressed = true;
			if (!hasFocus) setFocus ();
			if (m_popup)
			{
				if (inPopup (mx, my)) { int i = m_ptop + (my - m_py - 3) / popRowH (); if (i >= 0 && i < m_items.n) { m_psel = i; accept (); } return true; }
				m_popup = false;
			}
			if (vbar () && mx >= width - UK_SBW) { m_drag = 2; dragV (my); return true; }
			int p = posAt (mx, my);
			unsigned now = kapi_get_ticks ();
			if (now - m_lastClick < 35 && p == m_caret)
			{
				int a = p, b = p; const char *s = m_t.b;
				while (a > 0 && word_ch (s[a - 1])) a--;
				while (b < m_t.n && word_ch (s[b])) b++;
				m_anchor = a; m_caret = b; m_lastClick = 0;
			}
			else
			{
				if (mod_shift ()) { if (m_anchor < 0) m_anchor = m_caret; } else m_anchor = p;
				m_caret = p; m_drag = 1; m_lastClick = now;
			}
			m_goal = -1;
			invalidate (true); caretMoved ();
		}
		else if (bl && m_drag == 1) { int p = posAt (mx, my); if (p != m_caret) { m_caret = p; ensureCaret (false); invalidate (true); caretMoved (); } }
		else if (bl && m_drag == 2) dragV (my);
		else if (!bl) { pressed = false; m_drag = 0; if (m_anchor == m_caret) m_anchor = -1; }
		return true;
	}

	bool onKey (long k) override
	{
		bool shift = mod_shift (), ctrl = mod_ctrl ();
		if (m_popup && popupKey (k)) return true;
		const char *s = m_t.b;
		if (k == 1) { selectAll (); return true; }
		if (k == 3) { copy (); return true; }
		if (k == 24) { cut (); return true; }
		if (k == 22) { paste (); return true; }
		if (k == 26) { undo (); return true; }
		if (k == 25) { redo (); return true; }
		if (k == ' ' && ctrl) { openPopup (true); return true; }
		if (k == KEY_LEFT || k == KEY_RIGHT || k == KEY_UP || k == KEY_DOWN || k == KEY_HOME || k == KEY_END || k == KEY_PGUP || k == KEY_PGDN)
		{
			int c = m_caret, ln = lineOf (c), ls = m_lines[ln], le = lineEnd (ln), n = m_t.n;
			if (!shift && hasSel () && (k == KEY_LEFT || k == KEY_RIGHT)) c = k == KEY_LEFT ? selA () : selB ();
			else switch (k)
			{
			case KEY_LEFT: c = c > 0 ? c - 1 : 0; if (ctrl) { while (c > 0 && !word_ch (s[c])) c--; while (c > 0 && word_ch (s[c - 1])) c--; } break;
			case KEY_RIGHT: c = c < n ? c + 1 : n; if (ctrl) { while (c < n && word_ch (s[c])) c++; while (c < n && !word_ch (s[c]) && s[c] != '\n') c++; } break;
			case KEY_HOME:
				if (ctrl) c = 0;
				else { int f = ls; while (f < le && (s[f] == ' ' || s[f] == '\t')) f++; c = c == f ? ls : f; }
				break;
			case KEY_END: c = ctrl ? n : le; break;
			default:
			{
				int d = k == KEY_UP ? -1 : k == KEY_DOWN ? 1 : k == KEY_PGUP ? -viewRows () : viewRows ();
				if (m_goal < 0) m_goal = colOf (ls, c);
				int nl = iclamp (ln + d, 0, m_lines.n - 1);
				if (k == KEY_PGUP || k == KEY_PGDN) m_top = iclamp (m_top + d, 0, imax (0, m_lines.n - viewRows ()));
				c = atCol (nl, m_goal);
				if (nl == ln && d < 0) c = 0;
				if (nl == ln && d > 0) c = n;
				break;
			}
			}
			if (k != KEY_UP && k != KEY_DOWN && k != KEY_PGUP && k != KEY_PGDN) m_goal = -1;
			if (shift) { if (m_anchor < 0) m_anchor = m_caret; } else m_anchor = -1;
			m_caret = c;
			ensureCaret (false); invalidate (true); caretMoved ();
			return true;
		}
		if (readonly) return false;
		m_goal = -1;
		if (k == KEY_BACKSPACE)
		{
			snap (2);
			if (hasSel ()) delSel ();
			else if (m_caret > 0)
			{
				int p = m_caret - 1;
				int ls = m_lines[lineOf (m_caret)];
				bool allSp = true; for (int i = ls; i < m_caret; i++) if (s[i] != ' ') allSp = false;
				if (allSp && m_caret - ls >= 2) p = m_caret - ((m_caret - ls) % 2 ? 1 : 2);
				m_t.erase (p, m_caret - p); m_caret = p;
			}
			else return true;
			changed (); return true;
		}
		if (k == KEY_DEL)
		{
			snap (2);
			if (hasSel ()) delSel ();
			else if (m_caret < m_t.n) m_t.erase (m_caret, 1);
			else return true;
			changed (); return true;
		}
		if (k == KEY_ENTER)
		{
			snap (3);
			capitalise (m_caret);
			s = m_t.b;
			int ls = m_lines[lineOf (m_caret)];
			char ind[80]; int ni = 0; ind[ni++] = '\n';
			for (int i = ls; i < m_caret && (s[i] == ' ' || s[i] == '\t') && ni < 70; i++) ind[ni++] = s[i];
			if (opensBlock (ls, m_caret)) { ind[ni++] = ' '; ind[ni++] = ' '; }
			insert (ind, ni);
			return true;
		}
		if (k == KEY_TAB)
		{
			snap (3);
			if (hasSel () || shift)
			{
				// the lines chosen: two spaces more (Shift: fewer)
				int a = lineOf (selA ()), b = lineOf (selB () > selA () && selB () == m_lines[lineOf (selB ())] ? selB () - 1 : selB ());
				for (int ln = b; ln >= a; ln--)
				{
					int p = m_lines[ln];
					if (shift) { int q = 0; while (q < 2 && m_t.b[p + q] == ' ') q++; if (q) m_t.erase (p, q); }
					else m_t.insert (p, "  ", 2);
					relines ();
				}
				if (hasSel ()) { m_anchor = m_lines[a]; m_caret = lineEnd (b); }
				changed (); return true;
			}
			insert ("  ", 2); return true;
		}
		char u[4]; int un = uk_u8_key (k, u);
		if (un > 0 && !ctrl)
		{
			snap (1);
			if (!word_ch (u[0])) capitalise (m_caret);
			insert (u, un);
			if (u[0] == '.' && lang == LANG_BASIC) openPopup (false);
			return true;
		}
		return false;
	}
	void resizeTo (int w, int h) override { Widget::resizeTo (w, h); clampScroll (); }

	// The word under the caret's left (a name), and the object before its dot ("" none)
	void wordAt (int p, char *obj, int ocap, int *wordStart)
	{
		int a = p; while (a > 0 && name_ch (m_t.b[a - 1])) a--;
		*wordStart = a; obj[0] = 0;
		if (a > 0 && m_t.b[a - 1] == '.')
		{
			int e = a - 1, b = e; while (b > 0 && name_ch (m_t.b[b - 1])) b--;
			int l = imin (e - b, ocap - 1); memcpy (obj, m_t.b + b, l); obj[l] = 0;
		}
	}

private:
	struct Snap { char *text; int caret; };
	TextBuf m_t;
	Vec<int> m_lines;
	int m_caret, m_anchor, m_top, m_left, m_goal, m_drag;
	unsigned m_lastClick, m_lastEdit; int m_editKind;
	int m_maxCols;
	Vec<Snap> m_undo, m_redo;
	// the completion's list
	enum { POP_ROWS = 8, POP_W = 300 };
	bool m_popup; int m_psel, m_ptop, m_pstart, m_px, m_py;
	Vec<Compl> m_all, m_items;

	void clearUndo () { for (int i = 0; i < m_undo.n; i++) free (m_undo[i].text); m_undo.clear (); for (int i = 0; i < m_redo.n; i++) free (m_redo[i].text); m_redo.clear (); }
	int selA () const { return hasSel () ? imin (m_anchor, m_caret) : m_caret; }
	int selB () const { return hasSel () ? imax (m_anchor, m_caret) : m_caret; }
	void delSel () { int a = selA (), b = selB (); m_t.erase (a, b - a); m_caret = a; m_anchor = -1; }
	void insert (const char *s, int n) { if (hasSel ()) delSel (); m_t.insert (m_caret, s, n); m_caret += n; m_anchor = -1; changed (); }
	void changed () { relines (); ensureCaret (false); invalidate (true); if (onChange) onChange (*this); caretMoved (); }
	void caretMoved () { if (onCaret) onCaret (*this); if (m_popup) refilter (); }
	// An undo step before an edit: 1 typing, 2 deleting, 3 a paste / a new line (always a step)
	void snap (int kind)
	{
		unsigned now = kapi_get_ticks ();
		if (kind == m_editKind && kind != 3 && now - m_lastEdit < 100) { m_lastEdit = now; return; }
		m_editKind = kind; m_lastEdit = now;
		Snap s = { strdup (m_t.b), m_caret };
		m_undo.push (s);
		if (m_undo.n > 100) { free (m_undo[0].text); m_undo.erase (0); }
		for (int i = 0; i < m_redo.n; i++) free (m_redo[i].text);
		m_redo.clear ();
	}
	void relines ()
	{
		m_lines.clear (); m_lines.push (0);
		int maxc = 0, col = 0;
		for (int i = 0; i < m_t.n; i++)
		{
			if (m_t.b[i] == '\n') { m_lines.push (i + 1); if (col > maxc) maxc = col; col = 0; }
			else col++;
		}
		if (col > maxc) maxc = col;
		m_maxCols = maxc;
		if (m_caret > m_t.n) m_caret = m_t.n;
	}
	int lineEnd (int ln) const { return ln + 1 < m_lines.n ? m_lines[ln + 1] - 1 : m_t.n; }
	int lineOf (int p) const
	{
		int lo = 0, hi = m_lines.n - 1;
		while (lo < hi) { int mid = (lo + hi + 1) / 2; if (m_lines[mid] <= p) lo = mid; else hi = mid - 1; }
		return lo;
	}
	int colOf (int ls, int p) const { return p - ls; }
	int atCol (int ln, int col) const { int ls = m_lines[ln], le = lineEnd (ln); return imin (ls + col, le); }
	// a blank line's guides: those of the next line with text
	int guideOf (int ln) const
	{
		for (int l = ln + 1; l < m_lines.n && l < ln + 50; l++)
		{
			int ls = m_lines[l], le = lineEnd (l), i = 0;
			while (ls + i < le && m_t.b[ls + i] == ' ') i++;
			if (ls + i < le) return i;
		}
		return 0;
	}
	int posAt (int mx, int my)
	{
		int ln = iclamp (m_top + (my - 4) / lh (), 0, m_lines.n - 1);
		int CW = cw ();
		return atCol (ln, imax (0, (mx - gutter () + m_left + CW / 2) / CW));
	}
	void clampScroll ()
	{
		m_top = iclamp (m_top, 0, imax (0, m_lines.n - viewRows ()));
		m_left = iclamp (m_left, 0, imax (0, (m_maxCols + 2) * cw () - textW ()));
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
		int rows = viewRows ();
		UkThumb t = uk_thumb (m_lines.n, rows, m_top, height);
		m_top = (int) uk_thumb_pos (my, height, m_lines.n, rows, t.h);
		clampScroll (); invalidate (true);
	}

	// ---- BASIC's help as you type ---------------------------------------------------------------------------------------
	// Is p in a string or a comment of its line?
	bool inStringOrComment (int ls, int p) const
	{
		bool q = false;
		for (int i = ls; i < p; i++)
		{
			char c = m_t.b[i];
			if (c == '"') q = !q;
			else if (!q && c == '\'') return true;
			else if (!q && lang == LANG_FORM && c == '#') return true;
		}
		return q;
	}
	// The word just before p in capitals, when it is one of BASIC's (not after a dot: a property)
	void capitalise (int p)
	{
		if (lang != LANG_BASIC || !g_isKeyword) return;
		int a = p; while (a > 0 && word_ch (m_t.b[a - 1])) a--;
		if (a == p || (a > 0 && m_t.b[a - 1] == '.')) return;
		int ls = m_lines[lineOf (p)];
		if (inStringOrComment (ls, a)) return;
		if (!g_isKeyword (m_t.b + a, p - a)) return;
		for (int i = a; i < p; i++) m_t.b[i] = upc (m_t.b[i]);
	}
	// Does the line [ls, e) open a block (its next line indented)?
	bool opensBlock (int ls, int e) const
	{
		const char *s = m_t.b; int i = ls; while (i < e && s[i] == ' ') i++;
		char w[16]; int k = 0; while (i < e && name_ch (s[i]) && k < 15) w[k++] = upc (s[i++]); w[k] = 0;
		if (lang == LANG_FORM)
		{
			static const char *const C[] = { "WINDOW", "MENU", "COLUMN", "ROW", "GRID", "GROUP", "CANVAS", "TOOLBAR", 0 };
			for (int q = 0; C[q]; q++) if (!strcmp (w, C[q])) return true;
			return false;
		}
		static const char *const B[] = { "SUB", "FUNCTION", "FOR", "DO", "WHILE", "SELECT", "TYPE", "ELSE", "ELSEIF", "CASE", "PROPERTY", "CLASS", "INTERFACE", "VIRTUAL", "OVERRIDE", 0 };
		for (int q = 0; B[q]; q++) if (!strcmp (w, B[q])) return true;
		if (!strcmp (w, "IF"))			// a block IF: THEN ends the line
		{
			int j = e; while (j > ls && s[j - 1] == ' ') j--;
			return j - ls >= 4 && upc (s[j - 1]) == 'N' && upc (s[j - 2]) == 'E' && upc (s[j - 3]) == 'H' && upc (s[j - 4]) == 'T';
		}
		return false;
	}

	// ---- completion -------------------------------------------------------------------------------------------------------
	int popRowH () { return uk_fh () + 4; }
	bool inPopup (int mx, int my) { return mx >= m_px && mx < m_px + POP_W && my >= m_py && my < m_py + POP_ROWS * popRowH () + 6; }
	void openPopup (bool names)
	{
		if (!g_complete) return;
		char obj[40]; int ws;
		wordAt (m_caret, obj, sizeof obj, &ws);
		if (!names && !obj[0]) return;
		m_all.clear (); g_complete (obj, m_all);
		if (!m_all.n) return;
		m_pstart = ws; m_popup = true; m_psel = 0; m_ptop = 0;
		refilter ();
	}
	void refilter ()
	{
		if (m_caret < m_pstart || lineOf (m_caret) != lineOf (m_pstart)) { m_popup = false; invalidate (true); return; }
		for (int i = m_pstart; i < m_caret; i++) if (!name_ch (m_t.b[i])) { m_popup = false; invalidate (true); return; }
		int L = m_caret - m_pstart;
		m_items.clear ();
		for (int i = 0; i < m_all.n; i++)
		{
			int k = 0; while (k < L && upc (m_all[i].name[k]) == upc (m_t.b[m_pstart + k])) k++;
			if (k == L) m_items.push (m_all[i]);
		}
		if (!m_items.n) { m_popup = false; invalidate (true); return; }
		m_psel = iclamp (m_psel, 0, m_items.n - 1);
		invalidate (true);
	}
	void accept ()
	{
		if (!m_popup || m_psel >= m_items.n) return;
		const char *n = m_items[m_psel].name;
		snap (3);
		m_t.erase (m_pstart, m_caret - m_pstart); m_caret = m_pstart;
		m_popup = false;
		insert (n, (int) strlen (n));
	}
	bool popupKey (long k)
	{
		if (k == 27) { m_popup = false; invalidate (true); return true; }
		if (k == KEY_UP || k == KEY_DOWN || k == KEY_PGUP || k == KEY_PGDN)
		{
			int d = k == KEY_UP ? -1 : k == KEY_DOWN ? 1 : k == KEY_PGUP ? -POP_ROWS : POP_ROWS;
			m_psel = iclamp (m_psel + d, 0, m_items.n - 1);
			if (m_psel < m_ptop) m_ptop = m_psel;
			if (m_psel >= m_ptop + POP_ROWS) m_ptop = m_psel - POP_ROWS + 1;
			invalidate (true); return true;
		}
		if (k == KEY_ENTER || k == KEY_TAB) { accept (); return true; }
		return false;
	}
	void drawPopup ()
	{
		int LH = lh (), CW = cw ();
		int ln = lineOf (m_pstart);
		m_px = gutter () + colOf (m_lines[ln], m_pstart) * CW - m_left - 4;
		m_py = 4 + (ln - m_top + 1) * LH + 2;
		UkFaceScope back (g_ui);			// (the UI's face for the list)
		int RH = popRowH (), n = imin (POP_ROWS, m_items.n), h = n * RH + 6;
		if (m_py + h > height) m_py = 4 + (ln - m_top) * LH - h - 2;
		if (m_px + POP_W > width - UK_SBW) m_px = width - UK_SBW - POP_W;
		if (m_px < 0) m_px = 0;
		uk_popup (canvas, m_px, m_py, POP_W, h, 0, C_FIELD);
		m_ptop = iclamp (m_ptop, 0, imax (0, m_items.n - POP_ROWS));
		for (int r = 0; r < n; r++)
		{
			int i = m_ptop + r; const Compl &c = m_items[i];
			int y = m_py + 3 + r * RH;
			unsigned ink = C_FIELD_TEXT;
			if (i == m_psel) { uk_hilite (canvas, m_px + 3, y, POP_W - 6, RH, 3); ink = uk_hilite_ink (); }
			unsigned kc = c.kind == 'p' ? 0x2E7FA8 : c.kind == 'm' ? 0xB5530B : c.kind == 'c' ? 0x146C7A : c.kind == 's' ? 0x6A3FB5 : 0x0F5FB8;
			canvas.fillRect (m_px + 8, y + RH / 2 - 6, 12, 12, kc);
			char t[2] = { c.kind, 0 };
			uk_text_c (canvas, m_px + 8, y + RH / 2 - 6, 12, 12, t, 0xFFFFFF, 2);
			uk_text (canvas, m_px + 26, y + 2, c.name, ink, i == m_psel ? 2 : 0);
			int dw = uk_tw (c.detail);
			uk_text (canvas, m_px + POP_W - 10 - dw, y + 2, c.detail, i == m_psel ? ink : uk_mix (C_FIELD, C_FIELD_TEXT, 130));
		}
	}

	// ---- a line's colours -------------------------------------------------------------------------------------------------
	void run (Canvas &cv, int a, int b, int k, int y, int CW, int ls)
	{
		unsigned c = tk_color (k);
		int style = k == TK_KIND || k == TK_NAME ? 2 : 0;
		if (k == TK_KEYWORD && lang == LANG_BASIC) style = 2;
		char g[2] = { 0, 0 };
		for (int i = a; i < b; i++)
		{
			int x = (i - ls) * CW - m_left;
			if (x < -CW) continue;
			if (x > cv.w) break;
			if (m_t.b[i] == ' ') continue;
			g[0] = m_t.b[i];
			uk_text (cv, x, y, g, c, style);
		}
	}
	void drawLine (Canvas &cv, int ls, int le, int y, int CW)
	{
		const char *s = m_t.b;
		int i = ls;
		if (lang == LANG_FORM)
		{
			bool first = true;
			while (i < le)
			{
				char c = s[i]; int e = i + 1, k = TK_TEXT;
				if (c == '#') { e = le; k = TK_COMMENT; }
				else if (c == '"') { e = i + 1; while (e < le && s[e] != '"') e++; if (e < le) e++; k = TK_STR; first = false; }
				else if (c == ' ') { while (e < le && s[e] == ' ') e++; }
				else
				{
					while (e < le && s[e] != ' ' && s[e] != '=') e++;
					if (e < le && s[e] == '=') { run (cv, i, e + 1, TK_KEY, y, CW, ls); i = e + 1; int f = i; if (f < le && s[f] == '"') { f++; while (f < le && s[f] != '"') f++; if (f < le) f++; } else while (f < le && s[f] != ' ') f++; run (cv, i, f, TK_NUM, y, CW, ls); i = f; continue; }
					if (first) k = c == '-' ? TK_PUNCT : TK_KIND;
					else if (is_flag_word (s + i, e - i)) k = TK_FLAG;
					else k = TK_NAME;
					first = false;
				}
				run (cv, i, e, k, y, CW, ls);
				i = e;
			}
			return;
		}
		while (i < le)
		{
			char c = s[i]; int e = i + 1, k = TK_TEXT;
			if (c == '\'') { e = le; k = TK_COMMENT; }
			else if (c == '"') { while (e < le && s[e] != '"') e++; if (e < le) e++; k = TK_STR; }
			else if ((c >= '0' && c <= '9') || (c == '.' && e < le && s[e] >= '0' && s[e] <= '9' && (i == ls || !name_ch (s[i - 1]))))
			{ while (e < le && ((s[e] >= '0' && s[e] <= '9') || s[e] == '.' || s[e] == 'E' || s[e] == 'e' || s[e] == '#' || s[e] == '!')) e++; k = TK_NUM; }
			else if (name_ch (c))
			{
				while (e < le && word_ch (s[e])) e++;
				if (e - i == 3 && upc (c) == 'R' && upc (s[i + 1]) == 'E' && upc (s[i + 2]) == 'M') { e = le; k = TK_COMMENT; }
				else if (i > ls && s[i - 1] == '.') k = TK_MEMBER;
				else if (e < le && s[e] == '.') k = TK_OBJECT;
				else if (g_isKeyword && g_isKeyword (s + i, e - i)) k = TK_KEYWORD;
			}
			else if (c == '(' || c == ')' || c == ',' || c == ':' || c == ';') k = TK_PUNCT;
			run (cv, i, e, k, y, CW, ls);
			i = e;
			if ((i - ls) * CW - m_left > cv.w) break;
		}
	}
	static bool is_flag_word (const char *s, int n) { char w[24]; if (n > 23) return false; memcpy (w, s, n); w[n] = 0; return is_flag (w); }
};

} // namespace qs

#endif
