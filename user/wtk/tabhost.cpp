#include "wtk/tabhost.h"

namespace wtk {

TabHost::TabHost (int l, int t, int w, int h, int headerH_, unsigned bg)
  : Panel (l, t, w, h, bg), headerH (headerH_), ntabs (0), active (-1)
{
	for (int i = 0; i < MAXTABS; i++) { content[i] = 0; titles[i][0] = '\0'; titlePtr[i] = titles[i]; }
	picker = new Dropdown (6, (headerH - (headerH - 8)) / 2, 180, headerH - 8,
			       titlePtr, 0, 0, onPick);
	addChild (picker);
}

int TabHost::addTab (const char *title, Widget *c)
{
	if (ntabs >= MAXTABS || c == 0) return -1;
	int i = ntabs++;
	int k = 0; for (; title && title[k] && k < 31; k++) titles[i][k] = title[k];
	titles[i][k] = '\0';
	content[i] = c;
	c->hidden = true;				// shown only when selected
	addChild (c);
	picker->bringToFront ();			// keep the popup on top (its list overlays content)
	picker->setOptions (titlePtr, ntabs, active < 0 ? 0 : active);
	if (active < 0) select (0);			// the first tab becomes active
	else		layout ();
	return i;
}

void TabHost::select (int i)
{
	if (i < 0 || i >= ntabs) return;
	if (active >= 0 && content[active]) content[active]->hidden = true;
	active = i;
	content[i]->hidden = false;
	picker->sel = i;				// keep the popup label in sync
	picker->invalidate (true);
	layout ();
	invalidate (true);
}

void TabHost::layout ()
{
	if (picker)
	{
		int dw = width - 12; if (dw > 220) dw = 220; if (dw < 40) dw = 40;
		picker->left = 6; picker->top = (headerH - picker->rowH) / 2;
		picker->resizeTo (dw, picker->height);	// preserve the open/closed height
	}
	if (active >= 0 && content[active])
	{
		content[active]->left = 0; content[active]->top = headerH;
		content[active]->resizeTo (width, height - headerH);
	}
}

void TabHost::onPick (Widget &w)
{
	Dropdown &d = (Dropdown &) w;
	TabHost  *host = (TabHost *) d.parent;		// the Dropdown is always our direct child
	if (host) host->select (d.sel);
}

} // namespace wtk
