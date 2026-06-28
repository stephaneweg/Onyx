//
// wtk/splitter.h -- a two-pane container with a draggable divider ("grip"). Add the
// two content panes with setPanes(); the splitter sizes them and paints/handles a
// GRIP-px divider between, re-laying out automatically when it is resized (so a
// parent collapse/resize cascades straight through). Dragging the grip moves the
// split. Two orientations:
//   HSplitter -- panes side by side  (vertical grip, drag left/right)
//   VSplitter -- panes stacked       (horizontal grip, drag up/down)
//
#ifndef _wtk_splitter_h
#define _wtk_splitter_h

#include "wtk/widget.h"

namespace wtk {

class SplitGrip;			// internal draggable divider (defined in splitter.cpp)

class Splitter : public Widget
{
public:
	bool	 vertical;		// true = stacked (VSplitter); false = side by side (HSplitter)
	int	 split;			// size of the FIRST pane along the split axis (px)
	int	 grip;			// divider thickness (px)
	int	 minA, minB;		// minimum pane sizes along the split axis
	unsigned gripCol, gripHi;	// divider colour (idle / hover)
	unsigned bg;			// backdrop (normally hidden: panes + grip tile us)

	Splitter (int l, int t, int w, int h, bool vertical_, int split_ = -1,
		  unsigned bg_ = C_BG);
	void setPanes (Widget *a, Widget *b);	// adopt the two content panes (call once)
	void setSplit (int s);			// move the divider (clamped to mins), relayout
	void layout () override;
	void onDraw () override;

private:
	Widget	  *paneA, *paneB;
	SplitGrip *gripW;
};

class HSplitter : public Splitter
{ public: HSplitter (int l, int t, int w, int h, int split = -1, unsigned bg = C_BG)
	: Splitter (l, t, w, h, false, split, bg) {} };

class VSplitter : public Splitter
{ public: VSplitter (int l, int t, int w, int h, int split = -1, unsigned bg = C_BG)
	: Splitter (l, t, w, h, true, split, bg) {} };

} // namespace wtk

#endif
