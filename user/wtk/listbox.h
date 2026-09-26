//
// wtk/listbox.h -- ListBox: a scrollable list of text items with a selection (WPF
// ListBox). Click selects (onSelect), double-click or Enter activates (onActivate);
// Up / Down / Page Up / Page Down / Home / End move; the wheel and the scrollbar (drag
// the thumb or click the track) scroll. Items are copied (64 chars max each).
//
#ifndef _wtk_listbox_h
#define _wtk_listbox_h

#include "wtk/widget.h"

namespace wtk {

class ListBox : public Widget
{
public:
	int count, sel, top; Action onSelect, onActivate;
	ListBox (int l, int t, int w, int h, Action onSelect_ = 0, Action onActivate_ = 0);
	~ListBox ();
	void add (const char *s);
	void clear ();
	const char *item (int i) const { return (i >= 0 && i < count) ? m_items[i] : ""; }
	void setSel (int i);			// select + scroll into view (no callback)
	void onDraw () override;
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override;
	bool onKey (long k) override;
private:
	char (*m_items)[64]; int m_cap;
	unsigned m_lastClick; int m_lastIdx; bool m_thumb;
	int rows () const;
	void scrollTo (int t);
	void pick (int i, bool fire);
};

} // namespace wtk

#endif
