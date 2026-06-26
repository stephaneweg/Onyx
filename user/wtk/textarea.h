//
// wtk/textarea.h -- multi-line editable text (own '\n'-separated buffer), caret-driven
// vertical+horizontal scroll, click to position. Clipped to its own canvas.
//
#ifndef _wtk_textarea_h
#define _wtk_textarea_h

#include "wtk/widget.h"

namespace wtk {

class Textarea : public Widget
{
public:
	char *buf; int cap, len, caret, top, left, rows, cols; bool readonly, barDrag;
	Textarea (int l, int t, int w, int h, int capacity);
	~Textarea () override;
	const char *content () const { return buf; }
	void setContent (const char *s);
	void onDraw () override;
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override;
	bool onKey (long k) override;
private:
	int  lineStart (int i) const { while (i > 0 && buf[i - 1] != '\n') i--; return i; }
	int  lineEnd   (int i) const { while (buf[i] && buf[i] != '\n') i++; return i; }
	void insertAt (int ch);
	void deleteAt (int i);
	void ensureVisible (int vr, int vc);
};

} // namespace wtk

#endif
