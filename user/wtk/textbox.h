//
// wtk/textbox.h -- single-line editable field; click to position caret, type to edit; Ctrl+V pastes
// the clipboard at the caret, Ctrl+C / Ctrl+X copy / cut the whole field (not a password). (With a text
// face installed -- wtk/text.h -- its text is UTF-8 and the caret follows the glyphs' widths.)
//
#ifndef _wtk_textbox_h
#define _wtk_textbox_h

#include "wtk/widget.h"

namespace wtk {

class Textbox : public Widget
{
public:
	enum { TEXT_CAP = 512 };		// the buffer; how much of it is used: maxLen
	char	 text[TEXT_CAP]; int caret; bool password; Action cb;
	int	 maxLen;			// the most bytes typed or set (63; up to TEXT_CAP - 1: a chat line)
	int	 padR;				// px kept free at the right (a Combobox's arrow)
	int	 vstart;			// the first character shown (set by onDraw)
	Textbox (int l, int t, int w, int h, const char *s = "", Action cb_ = 0);
	void setText (const char *s);
	void onDraw () override;
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override;
	bool onKey (long k) override;
private:
	// With a proportional face installed (wtk/text.h): the text is UTF-8, measured; vstart is then
	// the byte the field shows from.
	int  shown (char *d) const;		// what is shown (a password's '*'s) -> its length
	int  shownAt (int i) const;		// byte i of the text -> its place in what is shown
	int  textAt (int d) const;		// ... and back
	void drawFace ();
	bool keyFace (long k);
	bool clipKey (long k);			// Ctrl+C / X / V -> true if taken
};

} // namespace wtk

#endif
