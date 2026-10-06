//
// uikit/codeedit.cpp -- CodeEdit (uikit/codeedit.h): a code editor, QBStudio's made a widget of the kit.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#include "uikit/codeedit.h"
#include "uikit/text.h"
#include "uikit/paint.h"
#include "systemkit/systemkit.h"
#include "sysclip.h"

namespace uikit {

// ---- small helpers (the library has no libc) -------------------------------------------------------------------------
static inline int imx (int a, int b) { return a > b ? a : b; }
static inline int imn (int a, int b) { return a < b ? a : b; }
static inline int iclamp (int v, int a, int b) { return v < a ? a : v > b ? b : v; }
static inline int slen (const char *s) { int n = 0; while (s && s[n]) n++; return n; }
static inline void mcopy (char *d, const char *s, int n) { for (int i = 0; i < n; i++) d[i] = s[i]; }
static inline void mmove (char *d, const char *s, int n) { if (d < s) for (int i = 0; i < n; i++) d[i] = s[i]; else for (int i = n - 1; i >= 0; i--) d[i] = s[i]; }
static inline void scpy (char *d, const char *s, int cap) { int i = 0; if (s) for (; s[i] && i < cap - 1; i++) d[i] = s[i]; if (cap > 0) d[i] = 0; }
static inline bool seq (const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static inline char *sdup (const char *s) { int n = slen (s); char *d = new char[n + 1]; mcopy (d, s, n + 1); return d; }
static inline bool word_ch (char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '$' || c == '%' || c == '!' || c == '#' || c == '&'; }
static inline bool name_ch (char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }
static inline char upc (char c) { return c >= 'a' && c <= 'z' ? (char) (c - 32) : c; }
// Ctrl held -- alone: Ctrl + Alt is AltGr as a PC sends it (a remote keyboard), and types its character
static inline bool mod_ctrl () { unsigned m = kapi_get_modifiers (); return (m & MOD_CTRL) != 0 && (m & MOD_ALT) == 0; }
static inline bool mod_shift () { return (kapi_get_modifiers () & MOD_SHIFT) != 0; }
static void num_text (int v, char *o) { char t[12]; int k = 0; do { t[k++] = (char) ('0' + v % 10); v /= 10; } while (v); int n = 0; while (k) o[n++] = t[--k]; o[n] = 0; }

// A growable array of plain values
template <class T> struct Arr
{
	T *d = 0; int n = 0, cap = 0;
	~Arr () { delete [] d; }
	void push (const T &v) { if (n == cap) { int c = cap ? cap * 2 : 16; T *nd = new T[c]; for (int i = 0; i < n; i++) nd[i] = d[i]; delete [] d; d = nd; cap = c; } d[n++] = v; }
	void erase (int i) { for (int k = i; k + 1 < n; k++) d[k] = d[k + 1]; n--; }
	int find (const T &v) const { for (int i = 0; i < n; i++) if (d[i] == v) return i; return -1; }
	void clear () { n = 0; }
	T &operator[] (int i) { return d[i]; }
	const T &operator[] (int i) const { return d[i]; }
};

// The colours (on the field's colour: lighter in a dark theme)
enum { TK_TEXT, TK_KEYWORD, TK_STR, TK_NUM, TK_COMMENT, TK_OBJECT, TK_MEMBER, TK_KIND, TK_NAME, TK_KEY, TK_FLAG, TK_PUNCT };
static unsigned tk_color (int k)
{
	static const unsigned C[] = { 0, 0x000F5FB8, 0x00B5530B, 0x006A3FB5, 0x00808890, 0x00146C7A, 0x000E7A8A, 0x000F5FB8, 0x00202020, 0x00A0226E, 0x00146C7A, 0 };
	bool dark = uk_bright (C_FIELD) < 110;
	if (k == TK_TEXT || k == TK_NAME) return C_FIELD_TEXT;
	if (k == TK_PUNCT) return uk_mix (C_FIELD, C_FIELD_TEXT, 170);
	return dark ? uk_mix (C[k], 0xFFFFFF, 110) : C[k];
}

struct Compl { char name[40]; char kind; char detail[100]; };
struct Snap { char *text; int caret; };

struct CodeEditState
{
	// the text
	char *b = 0; int n = 0, cap = 0;
	Arr<int> lines, marks;
	int caret = 0, anchor = -1, top = 0, left = 0, goal = -1, drag = 0;
	unsigned lastClick = 0, lastEdit = 0; int editKind = 0;
	int maxCols = 0;
	Arr<Snap> undo, redo;
	// the completion's list
	enum { POP_ROWS = 8, POP_W = 300, POP_WMAX = 640 };
	int pw = POP_W;
	bool popup = false; int psel = 0, ptop = 0, pstart = 0, px = 0, py = 0;
	Arr<Compl> all, items;

	CodeEditState () { need (64); b[0] = 0; }
	~CodeEditState () { delete [] b; clearUndo (); }
	void need (int m) { if (m + 1 <= cap) return; int c = cap ? cap * 2 : 256; while (c < m + 1) c *= 2; char *nb = new char[c]; if (b) mcopy (nb, b, n + 1); delete [] b; b = nb; cap = c; }
	void set (const char *s) { int l = slen (s); need (l); mcopy (b, s, l); n = l; b[n] = 0; }
	void ins (int at, const char *s, int l) { need (n + l); mmove (b + at + l, b + at, n - at + 1); mcopy (b + at, s, l); n += l; }
	void del (int at, int l) { mmove (b + at, b + at + l, n - at - l + 1); n -= l; }
	void clearUndo () { for (int i = 0; i < undo.n; i++) delete [] undo[i].text; undo.clear (); for (int i = 0; i < redo.n; i++) delete [] redo[i].text; redo.clear (); }
	bool hasSel () const { return anchor >= 0 && anchor != caret; }
	int selA () const { return hasSel () ? imn (anchor, caret) : caret; }
	int selB () const { return hasSel () ? imx (anchor, caret) : caret; }
	int lineEnd (int ln) const { return ln + 1 < lines.n ? lines[ln + 1] - 1 : n; }
	int lineOf (int p) const
	{
		int lo = 0, hi = lines.n - 1;
		while (lo < hi) { int mid = (lo + hi + 1) / 2; if (lines[mid] <= p) lo = mid; else hi = mid - 1; }
		return lo;
	}
	// Columns count characters: a UTF-8 character (a comment's accents) is one column, whatever its bytes
	static bool cont (char c) { return ((unsigned char) c & 0xC0) == 0x80; }
	int col (int ls, int p) const { int c = 0; for (int i = ls; i < p; i++) if (!cont (b[i])) c++; return c; }
	int atCol (int ln, int cl) const
	{
		int ls = lines[ln], le = lineEnd (ln), p = ls, c = 0;
		while (p < le && c < cl) { p++; while (p < le && cont (b[p])) p++; c++; }
		return p;
	}
	int prevChar (int p) const { if (p > 0) p--; while (p > 0 && cont (b[p])) p--; return p; }
	int nextChar (int p) const { if (p < n) p++; while (p < n && cont (b[p])) p++; return p; }
	void relines ()
	{
		lines.clear (); lines.push (0);
		int maxc = 0, col = 0;
		for (int i = 0; i < n; i++)
		{
			if (b[i] == '\n') { lines.push (i + 1); if (col > maxc) maxc = col; col = 0; }
			else if (!cont (b[i])) col++;
		}
		if (col > maxc) maxc = col;
		maxCols = maxc;
		if (caret > n) caret = n;
	}
	// a blank line's guides: those of the next line with text
	int guideOf (int ln) const
	{
		for (int l = ln + 1; l < lines.n && l < ln + 50; l++)
		{
			int ls = lines[l], le = lineEnd (l), i = 0;
			while (ls + i < le && b[ls + i] == ' ') i++;
			if (ls + i < le) return i;
		}
		return 0;
	}
};

// ---- the editor ------------------------------------------------------------------------------------------------------
CodeEdit::CodeEdit (int l, int t, int w, int h)
	: Widget (l, t, w, h), readonly (false), lang (CODE_BASIC), hiLine (0), onChange (0), onCaret (0), mono (0), ui (0),
	  isKeyword (0), isFlag (0), complete (0), user (0), m (new CodeEditState)
{ canFocus = true; m->relines (); }
CodeEdit::~CodeEdit () { delete m; }

const char *CodeEdit::text () const { return m->b; }
int CodeEdit::length () const { return m->n; }
int CodeEdit::caret () const { return m->caret; }
int CodeEdit::caretLine () const { return m->lineOf (m->caret); }
int CodeEdit::caretCol () const { return m->col (m->lines[m->lineOf (m->caret)], m->caret); }
int CodeEdit::lineCount () const { return m->lines.n; }
int CodeEdit::lineStart (int ln) const { return m->lines[iclamp (ln, 0, m->lines.n - 1)]; }
bool CodeEdit::hasSel () const { return m->hasSel (); }
void CodeEdit::clearMarks () { m->marks.clear (); invalidate (true); }
void CodeEdit::addMark (int line) { if (m->marks.find (line) < 0) m->marks.push (line); invalidate (true); }
bool CodeEdit::hasMark (int line) const { return m->marks.find (line) >= 0; }

// (the private steps, on the state and the widget)
struct CodeEditOps
{
	CodeEdit &e; CodeEditState &s;
	CodeEditOps (CodeEdit &ed) : e (ed), s (*ed.m) {}
	int cw () { UkFaceScope fs (e.mono); int w = uk_tw ("M"); return w < 1 ? 8 : w; }
	int lh () { UkFaceScope fs (e.mono); return uk_fh () + 3; }
	int gutter () { int d = 1, n = s.lines.n; while (n >= 10) { n /= 10; d++; } return imx (2, d) * cw () + 22; }
	int viewRows () { return imx (1, (e.height - 8) / lh ()); }
	bool vbar () { return s.lines.n > (e.height - 8) / lh (); }
	int textW () { return e.width - gutter () - (vbar () ? UK_SBW : 0); }
	void clampScroll ()
	{
		s.top = iclamp (s.top, 0, imx (0, s.lines.n - viewRows ()));
		s.left = iclamp (s.left, 0, imx (0, (s.maxCols + 2) * cw () - textW ()));
	}
	void ensureCaret (bool centre)
	{
		int ln = s.lineOf (s.caret), rows = viewRows ();
		if (ln < s.top) s.top = centre ? imx (0, ln - rows / 2) : ln;
		if (ln >= s.top + rows) s.top = centre ? ln - rows / 2 : ln - rows + 1;
		int CW = cw (), x = s.col (s.lines[ln], s.caret) * CW, w = textW ();
		if (x < s.left) s.left = imx (0, x - CW * 4);
		if (x > s.left + w - CW * 2) s.left = x - w + CW * 6;
		clampScroll ();
	}
	int posAt (int mx, int my)
	{
		int ln = iclamp (s.top + (my - 4) / lh (), 0, s.lines.n - 1);
		int CW = cw ();
		return s.atCol (ln, imx (0, (mx - gutter () + s.left + CW / 2) / CW));
	}
	void dragV (int my)
	{
		int rows = viewRows ();
		UkThumb t = uk_thumb (s.lines.n, rows, s.top, e.height);
		s.top = (int) uk_thumb_pos (my, e.height, s.lines.n, rows, t.h);
		clampScroll (); e.invalidate (true);
	}
	// An undo step before an edit: 1 typing, 2 deleting, 3 a paste / a new line (always a step)
	void snap (int kind)
	{
		unsigned now = kapi_get_ticks ();
		if (kind == s.editKind && kind != 3 && now - s.lastEdit < 100) { s.lastEdit = now; return; }
		s.editKind = kind; s.lastEdit = now;
		Snap sn = { sdup (s.b), s.caret };
		s.undo.push (sn);
		if (s.undo.n > 100) { delete [] s.undo[0].text; s.undo.erase (0); }
		for (int i = 0; i < s.redo.n; i++) delete [] s.redo[i].text;
		s.redo.clear ();
	}
	void caretMoved () { if (e.onCaret) e.onCaret (e); if (s.popup) refilter (); }
	void changed () { s.relines (); ensureCaret (false); e.invalidate (true); if (e.onChange) e.onChange (e); caretMoved (); }
	void delSel () { int a = s.selA (), b = s.selB (); s.del (a, b - a); s.caret = a; s.anchor = -1; }
	void insert (const char *t, int n) { if (s.hasSel ()) delSel (); s.ins (s.caret, t, n); s.caret += n; s.anchor = -1; changed (); }

	// ---- BASIC's help as you type
	bool inStringOrComment (int ls, int p) const
	{
		bool q = false;
		for (int i = ls; i < p; i++)
		{
			char c = s.b[i];
			if (c == '"') q = !q;
			else if (!q && c == '\'') return true;
			else if (!q && e.lang == CODE_FORM && c == '#') return true;
		}
		return q;
	}
	// The word just before p in capitals, when it is one of the language's (not after a dot: a property)
	void capitalise (int p)
	{
		if (e.lang != CODE_BASIC || !e.isKeyword) return;
		int a = p; while (a > 0 && word_ch (s.b[a - 1])) a--;
		if (a == p || (a > 0 && s.b[a - 1] == '.')) return;
		int ls = s.lines[s.lineOf (p)];
		if (inStringOrComment (ls, a)) return;
		if (!e.isKeyword (s.b + a, p - a)) return;
		for (int i = a; i < p; i++) s.b[i] = upc (s.b[i]);
	}
	// Does the line [ls, e) open a block (its next line indented)?
	bool opensBlock (int ls, int end) const
	{
		const char *t = s.b; int i = ls; while (i < end && t[i] == ' ') i++;
		char w[16]; int k = 0; while (i < end && name_ch (t[i]) && k < 15) w[k++] = upc (t[i++]); w[k] = 0;
		if (e.lang == CODE_FORM)
		{
			static const char *const C[] = { "WINDOW", "MENU", "COLUMN", "ROW", "GRID", "GROUP", "CANVAS", "TOOLBAR", 0 };
			for (int q = 0; C[q]; q++) if (seq (w, C[q])) return true;
			return false;
		}
		static const char *const B[] = { "SUB", "FUNCTION", "FOR", "DO", "WHILE", "SELECT", "TYPE", "ELSE", "ELSEIF", "CASE", "PROPERTY", "CLASS", "INTERFACE", "VIRTUAL", "OVERRIDE", "REPEAT", 0 };
		for (int q = 0; B[q]; q++) if (seq (w, B[q])) return true;
		if (seq (w, "IF"))			// a block IF: THEN ends the line
		{
			int j = end; while (j > ls && t[j - 1] == ' ') j--;
			return j - ls >= 4 && upc (t[j - 1]) == 'N' && upc (t[j - 2]) == 'E' && upc (t[j - 3]) == 'H' && upc (t[j - 4]) == 'T';
		}
		return false;
	}

	// ---- completion
	int popRowH () { return uk_fh () + 4; }
	bool inPopup (int mx, int my) { return mx >= s.px && mx < s.px + s.pw && my >= s.py && my < s.py + CodeEditState::POP_ROWS * popRowH () + 6; }
	void openPopup (bool names)
	{
		if (!e.complete) return;
		char obj[40]; int ws;
		e.wordAt (s.caret, obj, sizeof obj, &ws);
		if (!names && !obj[0]) return;
		s.all.clear (); e.complete (e, obj);
		if (!s.all.n) return;
		s.pstart = ws; s.popup = true; s.psel = 0; s.ptop = 0;
		refilter ();
	}
	void refilter ()
	{
		if (s.caret < s.pstart || s.lineOf (s.caret) != s.lineOf (s.pstart)) { s.popup = false; e.invalidate (true); return; }
		for (int i = s.pstart; i < s.caret; i++) if (!name_ch (s.b[i])) { s.popup = false; e.invalidate (true); return; }
		int L = s.caret - s.pstart;
		s.items.clear ();
		for (int i = 0; i < s.all.n; i++)
		{
			int k = 0; while (k < L && upc (s.all[i].name[k]) == upc (s.b[s.pstart + k])) k++;
			if (k == L) s.items.push (s.all[i]);
		}
		if (!s.items.n) { s.popup = false; e.invalidate (true); return; }
		s.psel = iclamp (s.psel, 0, s.items.n - 1);
		{
			UkFaceScope back (e.ui);
			s.pw = CodeEditState::POP_W;
			for (int i = 0; i < s.items.n; i++) s.pw = imx (s.pw, 26 + uk_tw (s.items[i].name) + 24 + uk_tw (s.items[i].detail) + 10);
			s.pw = imn (s.pw, imn (CodeEditState::POP_WMAX, imx (CodeEditState::POP_W, e.width - UK_SBW)));
		}
		e.invalidate (true);
	}
	void accept ()
	{
		if (!s.popup || s.psel >= s.items.n) return;
		char nm[40]; scpy (nm, s.items[s.psel].name, sizeof nm);
		snap (3);
		s.del (s.pstart, s.caret - s.pstart); s.caret = s.pstart;
		s.popup = false;
		insert (nm, slen (nm));
	}
	bool popupKey (long k)
	{
		const int R = CodeEditState::POP_ROWS;
		if (k == 27) { s.popup = false; e.invalidate (true); return true; }
		if (k == KEY_UP || k == KEY_DOWN || k == KEY_PGUP || k == KEY_PGDN)
		{
			int d = k == KEY_UP ? -1 : k == KEY_DOWN ? 1 : k == KEY_PGUP ? -R : R;
			s.psel = iclamp (s.psel + d, 0, s.items.n - 1);
			if (s.psel < s.ptop) s.ptop = s.psel;
			if (s.psel >= s.ptop + R) s.ptop = s.psel - R + 1;
			e.invalidate (true); return true;
		}
		if (k == KEY_ENTER || k == KEY_TAB) { accept (); return true; }
		return false;
	}
	void drawPopup ()
	{
		const int R = CodeEditState::POP_ROWS;
		int LH = lh (), CW = cw ();
		int ln = s.lineOf (s.pstart);
		s.px = gutter () + s.col (s.lines[ln], s.pstart) * CW - s.left - 4;
		s.py = 4 + (ln - s.top + 1) * LH + 2;
		UkFaceScope back (e.ui);			// (the UI's face for the list)
		int RH = popRowH (), n = imn (R, s.items.n), h = n * RH + 6;
		if (s.py + h > e.height) s.py = 4 + (ln - s.top) * LH - h - 2;
		if (s.px + s.pw > e.width - UK_SBW) s.px = e.width - UK_SBW - s.pw;
		if (s.px < 0) s.px = 0;
		Canvas &cv = e.canvas;
		uk_popup (cv, s.px, s.py, s.pw, h, 0, C_FIELD);
		s.ptop = iclamp (s.ptop, 0, imx (0, s.items.n - R));
		for (int r = 0; r < n; r++)
		{
			int i = s.ptop + r; const Compl &c = s.items[i];
			int y = s.py + 3 + r * RH;
			unsigned ink = C_FIELD_TEXT;
			if (i == s.psel) { uk_hilite (cv, s.px + 3, y, s.pw - 6, RH, 3); ink = uk_hilite_ink (); }
			unsigned kc = c.kind == 'p' ? 0x2E7FA8 : c.kind == 'm' ? 0xB5530B : c.kind == 'c' ? 0x146C7A : c.kind == 's' ? 0x6A3FB5 : 0x0F5FB8;
			cv.fillRect (s.px + 8, y + RH / 2 - 6, 12, 12, kc);
			char t[2] = { c.kind, 0 };
			uk_text_c (cv, s.px + 8, y + RH / 2 - 6, 12, 12, t, 0xFFFFFF, 2);
			uk_text (cv, s.px + 26, y + 2, c.name, ink, i == s.psel ? 2 : 0);
			// (what does not fit beside the name is cut: a kit's function and its many arguments)
			char det[100]; scpy (det, c.detail, sizeof det);
			int room = s.pw - 10 - (26 + uk_tw (c.name) + 12), dl = slen (det);
			while (dl > 3 && uk_tw (det) > room) { det[--dl] = 0; det[dl - 1] = det[dl - 2] = det[dl - 3] = '.'; }
			int dw = uk_tw (det);
			uk_text (cv, s.px + s.pw - 10 - dw, y + 2, det, i == s.psel ? ink : uk_mix (C_FIELD, C_FIELD_TEXT, 130));
		}
	}

	// ---- a line's colours
	void run (Canvas &cv, int a, int b, int k, int y, int CW, int ls)
	{
		unsigned c = tk_color (k);
		int style = k == TK_KIND || k == TK_NAME ? 2 : 0;
		if (k == TK_KEYWORD && e.lang == CODE_BASIC) style = 2;
		char g[5];
		int x = s.col (ls, a) * CW - s.left;
		for (int i = a; i < b; x += CW)
		{
			int k = 1; while (i + k < b && k < 4 && CodeEditState::cont (s.b[i + k])) k++;
			if (x > cv.w) break;
			if (x >= -CW && s.b[i] != ' ') { mcopy (g, s.b + i, k); g[k] = 0; uk_text (cv, x, y, g, c, style); }
			i += k;
		}
	}
	void drawLine (Canvas &cv, int ls, int le, int y, int CW)
	{
		const char *t = s.b;
		int i = ls;
		if (e.lang == CODE_FORM)
		{
			bool first = true;
			while (i < le)
			{
				char c = t[i]; int en = i + 1, k = TK_TEXT;
				if (c == '#') { en = le; k = TK_COMMENT; }
				else if (c == '"') { en = i + 1; while (en < le && t[en] != '"') en++; if (en < le) en++; k = TK_STR; first = false; }
				else if (c == ' ') { while (en < le && t[en] == ' ') en++; }
				else
				{
					while (en < le && t[en] != ' ' && t[en] != '=') en++;
					if (en < le && t[en] == '=')
					{
						run (cv, i, en + 1, TK_KEY, y, CW, ls); i = en + 1;
						int f = i;
						if (f < le && t[f] == '"') { f++; while (f < le && t[f] != '"') f++; if (f < le) f++; }
						else while (f < le && t[f] != ' ') f++;
						run (cv, i, f, TK_NUM, y, CW, ls); i = f; continue;
					}
					if (first) k = c == '-' ? TK_PUNCT : TK_KIND;
					else if (e.isFlag && e.isFlag (t + i, en - i)) k = TK_FLAG;
					else k = TK_NAME;
					first = false;
				}
				run (cv, i, en, k, y, CW, ls);
				i = en;
			}
			return;
		}
		while (i < le)
		{
			char c = t[i]; int en = i + 1, k = TK_TEXT;
			if (c == '\'') { en = le; k = TK_COMMENT; }
			else if (c == '"') { while (en < le && t[en] != '"') en++; if (en < le) en++; k = TK_STR; }
			else if ((c >= '0' && c <= '9') || (c == '.' && en < le && t[en] >= '0' && t[en] <= '9' && (i == ls || !name_ch (t[i - 1]))))
			{ while (en < le && ((t[en] >= '0' && t[en] <= '9') || t[en] == '.' || t[en] == 'E' || t[en] == 'e' || t[en] == '#' || t[en] == '!')) en++; k = TK_NUM; }
			else if (name_ch (c))
			{
				while (en < le && word_ch (t[en])) en++;
				if (en - i == 3 && upc (c) == 'R' && upc (t[i + 1]) == 'E' && upc (t[i + 2]) == 'M') { en = le; k = TK_COMMENT; }
				else if (i > ls && t[i - 1] == '.') k = TK_MEMBER;
				else if (en < le && t[en] == '.') k = TK_OBJECT;
				else if (e.isKeyword && e.isKeyword (t + i, en - i)) k = TK_KEYWORD;
			}
			else if (c == '(' || c == ')' || c == ',' || c == ':' || c == ';') k = TK_PUNCT;
			run (cv, i, en, k, y, CW, ls);
			i = en;
			if (s.col (ls, i) * CW - s.left > cv.w) break;
		}
	}
};

void CodeEdit::setText (const char *s)
{
	m->set (s ? s : ""); m->caret = 0; m->anchor = -1; m->top = 0; m->left = 0; m->popup = false;
	m->clearUndo (); m->relines (); invalidate (true);
}
void CodeEdit::replaceAll (const char *s, int caret)
{
	CodeEditOps o (*this);
	o.snap (3); m->set (s ? s : ""); m->caret = iclamp (caret, 0, m->n); m->anchor = -1; o.changed ();
	o.ensureCaret (true);
}
void CodeEdit::insertText (const char *s) { if (readonly) return; CodeEditOps o (*this); o.snap (3); o.insert (s, slen (s)); }
void CodeEdit::gotoLine (int ln, bool select)
{
	CodeEditOps o (*this);
	ln = iclamp (ln, 0, m->lines.n - 1);
	m->caret = m->lines[ln];
	while (m->caret < m->lineEnd (ln) && (m->b[m->caret] == ' ' || m->b[m->caret] == '\t')) m->caret++;
	m->anchor = select ? m->lineEnd (ln) : -1;
	if (select) { int c = m->caret; m->caret = m->anchor; m->anchor = c; }
	m->popup = false; o.ensureCaret (true); invalidate (true); o.caretMoved ();
}
void CodeEdit::setCaret (int p) { CodeEditOps o (*this); m->caret = iclamp (p, 0, m->n); m->anchor = -1; o.ensureCaret (true); invalidate (true); o.caretMoved (); }
void CodeEdit::showLine (int ln)
{
	CodeEditOps o (*this);
	int rows = o.viewRows ();
	if (ln < m->top || ln >= m->top + rows) { m->top = imx (0, ln - rows / 2); o.clampScroll (); invalidate (true); }
}
void CodeEdit::selectAll () { m->anchor = 0; m->caret = m->n; invalidate (true); }
void CodeEdit::copy () { if (m->hasSel () && uk_clip_ready ()) clip_set_text_n (m->b + m->selA (), m->selB () - m->selA ()); }
void CodeEdit::cut () { if (readonly || !m->hasSel ()) return; CodeEditOps o (*this); o.snap (3); copy (); o.delSel (); o.changed (); }
void CodeEdit::paste ()
{
	if (readonly || !uk_clip_ready ()) return;
	const int CAP = 1 << 18;
	char *b = new char[CAP];
	int n = clip_get_text (b, CAP);
	if (n > 0) { int w = 0; for (int i = 0; i < n; i++) if (b[i] != '\r') b[w++] = b[i]; CodeEditOps o (*this); o.snap (3); o.insert (b, w); }
	delete [] b;
}
void CodeEdit::undo ()
{
	if (!m->undo.n) return;
	CodeEditOps o (*this);
	Snap now = { sdup (m->b), m->caret }; m->redo.push (now);
	Snap u = m->undo[m->undo.n - 1]; m->undo.n--;
	m->set (u.text); m->caret = iclamp (u.caret, 0, m->n); delete [] u.text;
	m->anchor = -1; m->editKind = 0; m->relines (); o.ensureCaret (false); invalidate (true);
	if (onChange) onChange (*this);
}
void CodeEdit::redo ()
{
	if (!m->redo.n) return;
	CodeEditOps o (*this);
	Snap now = { sdup (m->b), m->caret }; m->undo.push (now);
	Snap u = m->redo[m->redo.n - 1]; m->redo.n--;
	m->set (u.text); m->caret = iclamp (u.caret, 0, m->n); delete [] u.text;
	m->anchor = -1; m->editKind = 0; m->relines (); o.ensureCaret (false); invalidate (true);
	if (onChange) onChange (*this);
}
bool CodeEdit::find (const char *w)
{
	int L = slen (w); if (!L) return false;
	CodeEditOps o (*this);
	int start = m->hasSel () ? m->selB () : m->caret;
	for (int pass = 0; pass < 2; pass++)
		for (int i = pass ? 0 : start; i + L <= m->n; i++)
		{
			int k = 0; while (k < L && upc (m->b[i + k]) == upc (w[k])) k++;
			if (k == L) { m->anchor = i; m->caret = i + L; o.ensureCaret (true); invalidate (true); o.caretMoved (); return true; }
		}
	return false;
}
void CodeEdit::wordAt (int p, char *obj, int ocap, int *wordStart)
{
	int a = p; while (a > 0 && name_ch (m->b[a - 1])) a--;
	*wordStart = a; obj[0] = 0;
	if (a > 0 && m->b[a - 1] == '.')
	{
		int e = a - 1, b = e; while (b > 0 && name_ch (m->b[b - 1])) b--;
		int l = imn (e - b, ocap - 1); mcopy (obj, m->b + b, l); obj[l] = 0;
	}
}
void CodeEdit::addCompletion (const char *name, char kind, const char *detail)
{
	Compl c; scpy (c.name, name, sizeof c.name); c.kind = kind; scpy (c.detail, detail ? detail : "", sizeof c.detail);
	m->all.push (c);
}

void CodeEdit::onDraw ()
{
	CodeEditOps o (*this);
	CodeEditState &s = *m;
	UkFaceScope fs (mono);
	int fh = uk_fh (), LH = o.lh (), CW = o.cw (), G = o.gutter ();
	bool focus = hasFocus && !disabled;
	unsigned bg = readonly ? uk_mix (C_FIELD, C_BG, 90) : C_FIELD;
	canvas.clear (bg);
	int rows = o.viewRows ();
	o.clampScroll ();
	int tw_ = o.textW ();
	canvas.fillRect (0, 0, G - 10, height, uk_mix (bg, C_FIELD_TEXT, 10));
	canvas.fillRect (G - 10, 0, 1, height, uk_mix (bg, C_FIELD_TEXT, 36));
	Canvas clip; clip.adopt (canvas.px + G, imx (1, tw_), height, canvas.stride);
	int cl = s.lineOf (s.caret);
	int sa = s.selA (), sb = s.selB ();
	unsigned faint = uk_mix (bg, C_FIELD_TEXT, 90);
	for (int r = 0; r < rows + 1; r++)
	{
		int ln = s.top + r;
		if (ln >= s.lines.n) break;
		int y = 4 + r * LH;
		int ls = s.lines[ln], le = s.lineEnd (ln);
		bool marked = s.marks.find (ln + 1) >= 0;
		if (hiLine == ln + 1) clip.fillRect (0, y, tw_, LH, uk_mix (bg, 0xF0C020, 90));
		else if (focus && ln == cl && !s.hasSel ()) clip.fillRect (0, y, tw_, LH, uk_mix (bg, C_ACCENT, 14));
		char b[16]; num_text (ln + 1, b);
		uk_text (canvas, G - 16 - uk_tw (b), y + 1, b, ln == cl || hiLine == ln + 1 ? C_FIELD_TEXT : faint);
		if (hiLine == ln + 1) uk_bead (canvas, G - 9 - 3, y + LH / 2 - 3, 7, 0xE0A010);
		if (marked) uk_bead (canvas, G - 9 - 3, y + LH / 2 - 3, 7, 0xD03B2B);
		// the indentation's guides
		{
			int ind = 0; while (ls + ind < le && s.b[ls + ind] == ' ') ind++;
			if (ls + ind == le) ind = s.guideOf (ln);
			for (int c = 0; c + 2 <= ind; c += 2)
			{
				int x = c * CW - s.left + CW / 2;
				if (x >= 0) for (int q = 0; q < LH; q += 2) clip.fillRect (x, y + q, 1, 1, uk_mix (bg, C_FIELD_TEXT, 40));
			}
		}
		if (s.hasSel () && sb > ls && sa <= le)
		{
			int a = imx (sa, ls), e = imn (sb, le);
			int xa = s.col (ls, a) * CW - s.left, xb = s.col (ls, e) * CW - s.left + (sb > le ? CW / 2 : 0);
			clip.fillRect (xa, y, imx (2, xb - xa), LH, uk_mix (C_FIELD, C_ACCENT, focus ? 80 : 50));
		}
		o.drawLine (clip, ls, le, y + (LH - fh) / 2, CW);
		if (marked)
		{
			int a = ls; while (a < le && s.b[a] == ' ') a++;
			int xa = s.col (ls, a) * CW - s.left, xb = s.col (ls, le) * CW - s.left;
			for (int x = xa; x < imx (xb, xa + CW); x++) clip.fillRect (x, y + LH - 2 - ((x / 2) & 1), 1, 1, 0xD03B2B);
		}
		if (focus && ln == cl)
		{
			int cx = s.col (ls, s.caret) * CW - s.left;
			clip.fillRect (cx, y, readonly ? 1 : 2, LH, readonly ? faint : C_ACCENT);
		}
	}
	if (o.vbar ())
	{
		UkThumb t = uk_thumb (s.lines.n, rows, s.top, height);
		uk_draw_vscroll (canvas, width - UK_SBW, 0, UK_SBW, height, t, bg, s.drag == 2);
	}
	if (s.popup) o.drawPopup ();
}

bool CodeEdit::onMouse (int mx, int my, int bl, int, int, int wheel)
{
	CodeEditOps o (*this);
	CodeEditState &s = *m;
	if (mx < 0) { if (!bl) s.drag = 0; return false; }
	if (wheel)
	{
		if (s.popup && o.inPopup (mx, my)) { s.ptop = iclamp (s.ptop - wheel, 0, imx (0, s.items.n - CodeEditState::POP_ROWS)); invalidate (true); return true; }
		s.top -= wheel * 3; o.clampScroll (); invalidate (true); return true;
	}
	int G = o.gutter ();
	if (mx >= G && mx < width - UK_SBW) uk_cursor (KAPI_CURSOR_TEXT);
	if (bl && !pressed)
	{
		pressed = true;
		if (!hasFocus) setFocus ();
		if (s.popup)
		{
			if (o.inPopup (mx, my)) { int i = s.ptop + (my - s.py - 3) / o.popRowH (); if (i >= 0 && i < s.items.n) { s.psel = i; o.accept (); } return true; }
			s.popup = false;
		}
		if (o.vbar () && mx >= width - UK_SBW) { s.drag = 2; o.dragV (my); return true; }
		int p = o.posAt (mx, my);
		unsigned now = kapi_get_ticks ();
		if (now - s.lastClick < 35 && p == s.caret)
		{
			int a = p, b = p;
			while (a > 0 && word_ch (s.b[a - 1])) a--;
			while (b < s.n && word_ch (s.b[b])) b++;
			s.anchor = a; s.caret = b; s.lastClick = 0;
		}
		else
		{
			if (mod_shift ()) { if (s.anchor < 0) s.anchor = s.caret; } else s.anchor = p;
			s.caret = p; s.drag = 1; s.lastClick = now;
		}
		s.goal = -1;
		invalidate (true); o.caretMoved ();
	}
	else if (bl && s.drag == 1) { int p = o.posAt (mx, my); if (p != s.caret) { s.caret = p; o.ensureCaret (false); invalidate (true); o.caretMoved (); } }
	else if (bl && s.drag == 2) o.dragV (my);
	else if (!bl) { pressed = false; s.drag = 0; if (s.anchor == s.caret) s.anchor = -1; }
	return true;
}

bool CodeEdit::onKey (long k)
{
	CodeEditOps o (*this);
	CodeEditState &s = *m;
	bool shift = mod_shift (), ctrl = mod_ctrl ();
	if (s.popup && o.popupKey (k)) return true;
	if (k == 1) { selectAll (); return true; }
	if (k == 3) { copy (); return true; }
	if (k == 24) { cut (); return true; }
	if (k == 22) { paste (); return true; }
	if (k == 26) { undo (); return true; }
	if (k == 25) { redo (); return true; }
	if (k == ' ' && ctrl) { o.openPopup (true); return true; }
	const char *t = s.b;
	if (k == KEY_LEFT || k == KEY_RIGHT || k == KEY_UP || k == KEY_DOWN || k == KEY_HOME || k == KEY_END || k == KEY_PGUP || k == KEY_PGDN)
	{
		int c = s.caret, ln = s.lineOf (c), ls = s.lines[ln], le = s.lineEnd (ln), n = s.n;
		if (!shift && s.hasSel () && (k == KEY_LEFT || k == KEY_RIGHT)) c = k == KEY_LEFT ? s.selA () : s.selB ();
		else switch (k)
		{
		case KEY_LEFT: c = s.prevChar (c); if (ctrl) { while (c > 0 && !word_ch (t[c])) c--; while (c > 0 && word_ch (t[c - 1])) c--; } break;
		case KEY_RIGHT: c = s.nextChar (c); if (ctrl) { while (c < n && word_ch (t[c])) c++; while (c < n && !word_ch (t[c]) && t[c] != '\n') c++; } break;
		case KEY_HOME:
			if (ctrl) c = 0;
			else { int f = ls; while (f < le && (t[f] == ' ' || t[f] == '\t')) f++; c = c == f ? ls : f; }
			break;
		case KEY_END: c = ctrl ? n : le; break;
		default:
		{
			int d = k == KEY_UP ? -1 : k == KEY_DOWN ? 1 : k == KEY_PGUP ? -o.viewRows () : o.viewRows ();
			if (s.goal < 0) s.goal = s.col (ls, c);
			int nl = iclamp (ln + d, 0, s.lines.n - 1);
			if (k == KEY_PGUP || k == KEY_PGDN) s.top = iclamp (s.top + d, 0, imx (0, s.lines.n - o.viewRows ()));
			c = s.atCol (nl, s.goal);
			if (nl == ln && d < 0) c = 0;
			if (nl == ln && d > 0) c = n;
			break;
		}
		}
		if (k != KEY_UP && k != KEY_DOWN && k != KEY_PGUP && k != KEY_PGDN) s.goal = -1;
		if (shift) { if (s.anchor < 0) s.anchor = s.caret; } else s.anchor = -1;
		s.caret = c;
		o.ensureCaret (false); invalidate (true); o.caretMoved ();
		return true;
	}
	if (readonly) return false;
	s.goal = -1;
	if (k == KEY_BACKSPACE)
	{
		o.snap (2);
		if (s.hasSel ()) o.delSel ();
		else if (s.caret > 0)
		{
			int p = s.prevChar (s.caret);
			int ls = s.lines[s.lineOf (s.caret)];
			bool allSp = true; for (int i = ls; i < s.caret; i++) if (t[i] != ' ') allSp = false;
			if (allSp && s.caret - ls >= 2) p = s.caret - ((s.caret - ls) % 2 ? 1 : 2);
			s.del (p, s.caret - p); s.caret = p;
		}
		else return true;
		o.changed (); return true;
	}
	if (k == KEY_DEL)
	{
		o.snap (2);
		if (s.hasSel ()) o.delSel ();
		else if (s.caret < s.n) s.del (s.caret, s.nextChar (s.caret) - s.caret);
		else return true;
		o.changed (); return true;
	}
	if (k == KEY_ENTER)
	{
		o.snap (3);
		o.capitalise (s.caret);
		t = s.b;
		int ls = s.lines[s.lineOf (s.caret)];
		char ind[80]; int ni = 0; ind[ni++] = '\n';
		for (int i = ls; i < s.caret && (t[i] == ' ' || t[i] == '\t') && ni < 70; i++) ind[ni++] = t[i];
		if (o.opensBlock (ls, s.caret)) { ind[ni++] = ' '; ind[ni++] = ' '; }
		o.insert (ind, ni);
		return true;
	}
	if (k == KEY_TAB)
	{
		o.snap (3);
		if (s.hasSel () || shift)
		{
			// the lines chosen: two spaces more (Shift: fewer)
			int a = s.lineOf (s.selA ()), b = s.lineOf (s.selB () > s.selA () && s.selB () == s.lines[s.lineOf (s.selB ())] ? s.selB () - 1 : s.selB ());
			for (int ln = b; ln >= a; ln--)
			{
				int p = s.lines[ln];
				if (shift) { int q = 0; while (q < 2 && s.b[p + q] == ' ') q++; if (q) s.del (p, q); }
				else s.ins (p, "  ", 2);
				s.relines ();
			}
			if (s.hasSel ()) { s.anchor = s.lines[a]; s.caret = s.lineEnd (b); }
			o.changed (); return true;
		}
		o.insert ("  ", 2); return true;
	}
	char u[4]; int un = uk_u8_key (k, u);
	if (un > 0 && !ctrl)
	{
		o.snap (1);
		if (!word_ch (u[0])) o.capitalise (s.caret);
		o.insert (u, un);
		if (u[0] == '.' && lang == CODE_BASIC) o.openPopup (false);
		return true;
	}
	return false;
}
void CodeEdit::resizeTo (int w, int h) { Widget::resizeTo (w, h); CodeEditOps o (*this); o.clampScroll (); }

} // namespace uikit
