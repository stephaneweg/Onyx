//
// wtk/textbox.h -- single-line editable field; click to position caret, type to edit. (With a text
// face installed -- wtk/text.h -- its text is UTF-8 and the caret follows the glyphs' widths.)
//
#ifndef _wtk_textbox_h
#define _wtk_textbox_h

#include "wtk/widget.h"

namespace wtk {

class Textbox : public Widget
{
public:
	char	 text[64]; int caret; bool password; Action cb;
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
};

} // namespace wtk

#endif
