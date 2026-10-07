//
// uikit/codeedit.h -- CodeEdit: a code editor. BASIC (or QBStudio's .form text) in a monospaced face, the
// language's colours, the line numbers, the indentation's guides, the lines with a problem marked (a red dot in
// the margin, the text underlined), a line lit (an error's, a debugger's current line), undo / redo, the
// clipboard, and the completion (after a name and a dot, or Ctrl+Space: the program gives the items). The words
// of the language are written in capitals as you type (as QBasic does) when isKeyword is set. Made from
// QBStudio's editor (2026-10-06) for every program that edits code: QBStudio, Turtle Quest.
//
//   CodeEdit *ed = new CodeEdit (x, y, w, h);
//   ed->mono = myMonoFace; ed->isKeyword = is_basic_word;	// (0: the UI's face; no keyword colours)
//   ed->setText (src); ed->onChange = changed;
//   ed->hiLine = 4; ed->invalidate (true);				// line 4 lit (1-based; 0 none)
//   ed->clearMarks (); ed->addMark (7);			// line 7 has a problem
//
// Keys: the arrows (Ctrl: by word), Home / End (Ctrl: the text's), Page Up / Down, Shift: select; Ctrl+A, C, X,
// V, Z, Y; Tab / Shift+Tab: indent / unindent the lines chosen; Enter keeps the indentation (two spaces more
// after a line that opens a block); Ctrl+Space: the completion. Mouse: click, drag, double click (a word), the
// wheel, the scroll bar.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors (docs/LICENSING.md).
//
#ifndef _uikit_codeedit_h
#define _uikit_codeedit_h

#include "uikit/widget.h"

namespace uikit {

enum { CODE_BASIC = 0, CODE_FORM = 1 };			// CodeEdit::lang (CODE_FORM: QBStudio's .form text)
struct CodeEditState;					// (the editor's text, lines, undo: codeedit.cpp)

class CodeEdit : public Widget
{
public:
	bool	 readonly;
	int	 lang;					// CODE_BASIC / CODE_FORM
	int	 hiLine;				// a line lit (1-based; 0 none)
	Action	 onChange;				// the text changed
	Action	 onCaret;				// the caret moved
	TextFace *mono;					// the code's face (0: the UI's)
	TextFace *ui;					// the completion list's face (0: the UI's)
	bool	 (*isKeyword) (const char *w, int n);	// a word of the language? (colours, capitals; 0: none)
	bool	 (*isFlag) (const char *w, int n);	// CODE_FORM: a flag word (0: none)
	// The completion: called with the name before the dot ("" for Ctrl+Space); it calls addCompletion for each
	// item -- kind: 'p' property, 'm' method, 'c' control, 's' a SUB, 'k' a word, 'f' a function, 't' a type...
	void	 (*complete) (CodeEdit &ed, const char *object);
	void	*user;					// the program's (e.g. the document this editor shows)

	CodeEdit (int l, int t, int w, int h);
	~CodeEdit () override;

	const char *text () const;
	int  length () const;
	void setText (const char *s);			// a new text: no undo, the caret at the start
	void replaceAll (const char *s, int caret);	// the whole text replaced as one step that can be undone
	void insertText (const char *s);		// at the caret, replacing the selection
	int  caret () const;
	int  caretLine () const;			// 0-based
	int  caretCol () const;
	int  lineCount () const;
	int  lineStart (int ln) const;			// the offset of a line (0-based)
	void gotoLine (int ln, bool select = false);	// the caret at a line's text (0-based), shown in the middle
	void setCaret (int p);
	void showLine (int ln);				// a line (0-based) scrolled into view, the caret left where it is
	bool hasSel () const;
	void selectAll ();
	void copy ();
	void cut ();
	void paste ();
	void undo ();
	void redo ();
	bool find (const char *w);			// the next w (any case) from the caret, chosen; false: none
	void wordAt (int p, char *obj, int ocap, int *wordStart);	// the name before p, and the object before its dot
	// The lines with a problem (1-based)
	void clearMarks ();
	void addMark (int line);
	bool hasMark (int line) const;
	void addCompletion (const char *name, char kind, const char *detail);	// (from complete)

	void onDraw () override;
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override;
	bool onKey (long k) override;
	void resizeTo (int w, int h) override;
private:
	friend struct CodeEditOps;
	CodeEditState *m;
	unsigned long m_reserved[4] = { 0, 0, 0, 0 };	// (uikit/abi.h)
};

} // namespace uikit

#endif
