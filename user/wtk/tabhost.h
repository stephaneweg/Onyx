//
// wtk/tabhost.h -- a section that hosts several "tasks" (apps) and shows ONE at a
// time, chosen from a popup menu in its header (the activity-shell tab strip, popup
// variant). addTab() adopts a content widget; the header Dropdown lists the tab
// titles; selecting one shows that content (the others are hidden, not destroyed).
// The content fills the area below the header and is resized with the host, so the
// hosted app stays passive about geometry.
//
#ifndef _wtk_tabhost_h
#define _wtk_tabhost_h

#include "wtk/panel.h"
#include "wtk/dropdown.h"

namespace wtk {

class TabHost : public Panel
{
public:
	static const int MAXTABS = 8;
	int headerH;				// height of the header strip (holds the popup)

	TabHost (int l, int t, int w, int h, int headerH_ = 26, unsigned bg = C_BG);
	int  addTab (const char *title, Widget *content);	// -> tab index, or -1 if full
	void select (int i);			// show tab i (hide the previous)
	int  count   () const { return ntabs; }
	int  current () const { return active; }
	void layout () override;

private:
	Dropdown   *picker;			// the task popup menu
	Widget	   *content[MAXTABS];
	char	    titles[MAXTABS][32];
	const char *titlePtr[MAXTABS];		// stable pointers into titles[] for the Dropdown
	int	    ntabs, active;
	static void onPick (Widget &w);		// Dropdown cb -> select the picked tab
};

} // namespace wtk

#endif
