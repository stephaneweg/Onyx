//
// uikit/tabstrip.h -- TabStrip: a row of tabs over a view (a terminal's shells, a reader's documents):
// each a title, a close cross, a mark (a dot: something runs, something new), and a "+" after them.
// A click picks a tab; the wheel, or selectNext (Ctrl+Tab, Ctrl+PgUp / PgDn: the app's keys), steps
// through them. The cross or a middle click ASKS for a tab's close: onClose fires with `closing` set,
// and the app decides (a question first, its own state freed) and calls remove (). The strip only shows
// the tabs: the app shows the chosen one's content. Each tab carries a pointer for the app (data ()),
// moved with it when a tab before it goes. The titles are copied; a long one is cut with an ellipsis.
//
//   TabStrip *ts = new TabStrip (0, 0, w, 30, onTab);
//   ts->onClose = onTabClose; ts->onNew = onNewTab;
//   int i = ts->add ("SD:/", myTab); ts->select (i);
//
// MIT licence (Onyx).
//
#ifndef _uikit_tabstrip_h
#define _uikit_tabstrip_h

#include "uikit/widget.h"

namespace uikit {

class TabStrip : public Widget
{
public:
	static const int MAXTABS = 32;
	int	 selected;			// the chosen tab (-1: none)
	int	 closing;			// the tab whose close was asked (onClose's)
	bool	 closable;			// a close cross on each tab (true)
	bool	 newButton;			// the "+" after the tabs (true)
	unsigned activeFace;			// the chosen tab's face: the content's background below it,
						// which it opens onto (0: the window's, C_BG)
	Action	 onChange;			// `selected` changed (a click, the wheel, selectNext)
	Action	 onClose;			// a close asked: the cross, a middle click (closing = the tab)
	Action	 onNew;				// the "+"

	TabStrip (int l, int t, int w, int h, Action change = 0);
	int  add (const char *title, void *data = 0);	// at the end -> its index (-1: full)
	void remove (int i);			// the tabs after it move down; selected follows its tab
						// (the removed one's: the tab now there, else the one before)
	int  count () const { return m_n; }
	void setTitle (int i, const char *title);
	const char *title (int i) const { return i >= 0 && i < m_n ? m_title[i] : ""; }
	void *data (int i) const { return i >= 0 && i < m_n ? m_data[i] : 0; }
	void setData (int i, void *p) { if (i >= 0 && i < m_n) m_data[i] = p; }
	void setMark (int i, bool on);		// the dot before the title
	void select (int i, bool fire = false);	// repainted; onChange if fire and it changed
	void selectNext (int d, bool fire = true);	// d = +1 / -1, round
	int  tabAt (int mx) const;		// the tab under x (-1: none)

	void onDraw () override;
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override;
private:
	char	 m_title[MAXTABS][48];
	void	*m_data[MAXTABS];
	bool	 m_mark[MAXTABS];
	int	 m_n, m_hot, m_hotPart;		// the tab under the pointer; 1 its cross, 2 the "+"
	int	 m_x[MAXTABS + 1], m_plusX;	// the tabs' edges (m_x[i] .. m_x[i + 1] - gap), the "+"'s
	bool	 m_mid;				// the middle button held (one close a press)
	void	 place ();
	bool	 inCross (int i, int mx, int my) const;
};

} // namespace uikit

#endif
