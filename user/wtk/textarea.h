//
// wtk/textarea.h -- multi-line editable text (own '\n'-separated buffer), caret-driven
// vertical+horizontal scroll, click to position. Clipped to its own canvas.
// Selection: Shift + arrows / Home / End / Page Up / Down (kapi_get_modifiers), a mouse
// drag, Shift+click, Ctrl+A. Typing, Enter, Backspace and Delete replace / remove it;
// Ctrl+C / Ctrl+X / Ctrl+V copy / cut / paste through the system clipboard (unless the
// app's menu takes those shortcuts first -- then it can call copy () / cut () / paste ()).
//
#ifndef _wtk_textarea_h
#define _wtk_textarea_h

#include "wtk/widget.h"

namespace wtk {

class Textarea : public Widget
{
public:
	char *buf; int cap, len, caret, top, left, rows, cols; bool readonly, barDrag;
	int anchor;					// selection = [anchor, caret) either way; -1 = none
	Textarea (int l, int t, int w, int h, int capacity);
	~Textarea () override;
	const char *content () const { return buf; }
	void setContent (const char *s);
	void insertText (const char *s);		// insert at the caret (clipboard paste)
	void gotoLine (int line);			// caret to the start of line (0-based), scrolled into view
	int  caretLine () const;			// 0-based line / column of the caret
	int  caretCol () const { return caret - lineStart (caret); }
	bool hasSelection () const { return anchor >= 0 && anchor != caret; }
	int  selStart () const { return hasSelection () ? (anchor < caret ? anchor : caret) : caret; }
	int  selEnd () const { return hasSelection () ? (anchor < caret ? caret : anchor) : caret; }
	int  selectedText (char *out, int cap) const;	// copies the selection; returns its length
	void deleteSelection ();
	void selectAll ();
	void copy ();					// selection -> clipboard
	void cut ();
	void paste ();					// clipboard text -> caret (replaces the selection)
	void onDraw () override;
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override;
	bool onKey (long k) override;
private:
	int  lineStart (int i) const { while (i > 0 && buf[i - 1] != '\n') i--; return i; }
	int  lineEnd   (int i) const { while (buf[i] && buf[i] != '\n') i++; return i; }
	void insertAt (int ch);
	void moveCaret (long k);			// one navigation key (no selection logic)
	void deleteAt (int i);
	void ensureVisible (int vr, int vc);
};

} // namespace wtk

#endif
